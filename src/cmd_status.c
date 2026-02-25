// SPDX-FileCopyrightText: 2026 Giovanni MARIANO
//
// SPDX-License-Identifier: MPL-2.0

#include "git_helpers.h"
#include "geom_load.h"
#include "geom_fingerprint.h"
#include "geom_diff.h"
#include "util.h"
#include <alea.h>
#include <stdio.h>
#include <string.h>

int cmd_status(int argc, char** argv) {
    (void)argc; (void)argv;

    git_repository* repo = ag_repo_open();
    if (!repo) return 1;

    /* Get HEAD commit */
    git_commit* head = ag_resolve_commit(repo, "HEAD");
    if (!head) {
        printf("No commits yet.\n");
        git_repository_free(repo);
        return 0;
    }

    /* Find geometry files that have changed */
    git_status_list* status = NULL;
    git_status_options opts = GIT_STATUS_OPTIONS_INIT;
    opts.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
    opts.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED |
                 GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS;

    if (git_status_list_new(&status, repo, &opts) < 0) {
        git_commit_free(head);
        git_repository_free(repo);
        return 1;
    }

    bool any_changes = false;
    size_t n = git_status_list_entrycount(status);

    for (size_t i = 0; i < n; i++) {
        const git_status_entry* se = git_status_byindex(status, i);

        /* Check both index and workdir paths */
        const char* path = NULL;
        const char* status_label = NULL;
        bool is_new = false;

        if (se->head_to_index) {
            path = se->head_to_index->new_file.path;
            unsigned s = se->status;
            if (s & GIT_STATUS_INDEX_NEW)      { status_label = "new file"; is_new = true; }
            else if (s & GIT_STATUS_INDEX_MODIFIED) status_label = "modified (staged)";
            else if (s & GIT_STATUS_INDEX_DELETED)  status_label = "deleted";
        }
        if (!path && se->index_to_workdir) {
            path = se->index_to_workdir->new_file.path;
            unsigned s = se->status;
            if (s & GIT_STATUS_WT_NEW)         { status_label = "untracked"; is_new = true; }
            else if (s & GIT_STATUS_WT_MODIFIED)    status_label = "modified";
            else if (s & GIT_STATUS_WT_DELETED)     status_label = "deleted";
        }

        if (!path) continue;

        /* Check if geometry file */
        bool is_geom = false;
        const char* exts[] = {".inp", ".i", ".mcnp", ".xml", NULL};
        for (int e = 0; exts[e]; e++) {
            if (ag_str_ends_with(path, exts[e])) { is_geom = true; break; }
        }
        if (!is_geom) continue;

        if (!any_changes) {
            ag_color_printf(COL_BOLD, "Geometry file changes:\n\n");
            any_changes = true;
        }

        if (is_new || (se->status & (GIT_STATUS_INDEX_DELETED | GIT_STATUS_WT_DELETED))) {
            /* Can't diff new or deleted files structurally */
            printf("  %-20s %s\n", status_label, path);
            continue;
        }

        /* Load both versions and do structural diff */
        alea_system_t* old_sys = ag_load_geometry_commit(repo, head, path);
        alea_system_t* new_sys = ag_load_geometry_workdir(repo, path);

        if (old_sys && new_sys) {
            ag_fingerprint_set_t* old_fp = ag_fingerprint(old_sys);
            ag_fingerprint_set_t* new_fp = ag_fingerprint(new_sys);

            if (old_fp && new_fp) {
                ag_diff_result_t* diff = ag_diff(old_fp, new_fp);
                if (diff) {
                    int total = diff->cells_added + diff->cells_removed + diff->cells_modified +
                                diff->surfs_added + diff->surfs_removed + diff->surfs_modified;
                    if (total > 0) {
                        printf("  %-20s %s  ", status_label, path);
                        ag_color_printf(COL_DIM, "[");
                        if (diff->cells_added + diff->cells_removed + diff->cells_modified > 0) {
                            printf("cells: ");
                            if (diff->cells_added)   ag_color_printf(COL_GREEN, "%d added ", diff->cells_added);
                            if (diff->cells_removed)  ag_color_printf(COL_RED, "%d removed ", diff->cells_removed);
                            if (diff->cells_modified) ag_color_printf(COL_YELLOW, "%d modified ", diff->cells_modified);
                        }
                        if (diff->surfs_added + diff->surfs_removed + diff->surfs_modified > 0) {
                            printf("surfs: ");
                            if (diff->surfs_added)   ag_color_printf(COL_GREEN, "%d added ", diff->surfs_added);
                            if (diff->surfs_removed)  ag_color_printf(COL_RED, "%d removed ", diff->surfs_removed);
                            if (diff->surfs_modified) ag_color_printf(COL_YELLOW, "%d modified ", diff->surfs_modified);
                        }
                        ag_color_printf(COL_DIM, "]");
                        printf("\n");
                    } else {
                        printf("  %-20s %s  ", status_label, path);
                        ag_color_printf(COL_DIM, "[no structural changes]");
                        printf("\n");
                    }
                    ag_diff_result_free(diff);
                }
            }
            ag_fingerprint_set_free(old_fp);
            ag_fingerprint_set_free(new_fp);
        } else {
            printf("  %-20s %s\n", status_label, path);
        }

        if (old_sys) alea_destroy(old_sys);
        if (new_sys) alea_destroy(new_sys);
    }

    if (!any_changes)
        printf("No geometry file changes.\n");

    git_status_list_free(status);
    git_commit_free(head);
    git_repository_free(repo);
    return 0;
}
