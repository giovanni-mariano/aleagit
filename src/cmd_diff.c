#include "git_helpers.h"
#include "geom_load.h"
#include "geom_fingerprint.h"
#include "geom_diff.h"
#include "util.h"
#include <alea.h>
#include <stdio.h>
#include <string.h>

int cmd_diff_visual(int argc, char** argv);

int cmd_diff(int argc, char** argv) {
    const char* rev1 = NULL;
    const char* rev2 = NULL;
    const char* file = NULL;
    bool visual = false;

    /* Parse arguments */
    int positional = 0;
    bool after_dashdash = false;

    for (int i = 0; i < argc; i++) {
        if (after_dashdash) {
            file = argv[i];
            break;
        }
        if (strcmp(argv[i], "--") == 0) {
            after_dashdash = true;
            continue;
        }
        if (strcmp(argv[i], "--visual") == 0 || strcmp(argv[i], "-v") == 0) {
            visual = true;
            continue;
        }
        if (argv[i][0] == '-') continue;

        if (positional == 0) rev1 = argv[i];
        else if (positional == 1) rev2 = argv[i];
        positional++;
    }

    /* Default behavior:
       - no revs:       HEAD vs working tree
       - one rev:        that rev vs working tree
       - two revs:       rev1 vs rev2  */

    if (visual) {
        return cmd_diff_visual(argc, argv);
    }

    git_repository* repo = ag_repo_open();
    if (!repo) return 1;

    /* Find geometry files to diff */
    ag_file_list_t* geom_files = NULL;
    git_commit* c1 = NULL;
    git_commit* c2 = NULL;
    bool workdir_mode = false;

    if (!rev1 && !rev2) {
        /* HEAD vs workdir */
        c1 = ag_resolve_commit(repo, "HEAD");
        if (!c1) { git_repository_free(repo); return 1; }
        workdir_mode = true;
    } else if (rev1 && !rev2) {
        /* rev1 vs workdir */
        c1 = ag_resolve_commit(repo, rev1);
        if (!c1) { git_repository_free(repo); return 1; }
        workdir_mode = true;
    } else {
        /* rev1 vs rev2 */
        c1 = ag_resolve_commit(repo, rev1);
        c2 = ag_resolve_commit(repo, rev2);
        if (!c1 || !c2) {
            if (c1) git_commit_free(c1);
            if (c2) git_commit_free(c2);
            git_repository_free(repo);
            return 1;
        }
    }

    /* Determine files to diff */
    const char* paths[64];
    int npath = 0;

    if (file) {
        paths[npath++] = file;
    } else {
        /* Find all geometry files in old commit */
        geom_files = ag_find_geometry_files(repo, c1);
        if (geom_files) {
            for (size_t i = 0; i < geom_files->count && npath < 64; i++)
                paths[npath++] = geom_files->paths[i];
        }
    }

    int rc = 0;
    for (int fi = 0; fi < npath; fi++) {
        const char* path = paths[fi];

        alea_system_t* old_sys = ag_load_geometry_commit(repo, c1, path);
        alea_system_t* new_sys = NULL;

        if (workdir_mode)
            new_sys = ag_load_geometry_workdir(repo, path);
        else
            new_sys = ag_load_geometry_commit(repo, c2, path);

        if (!old_sys && !new_sys) {
            continue;
        }

        /* Handle added/removed files */
        if (!old_sys) {
            ag_color_printf(COL_GREEN, "New file: %s\n", path);
            if (new_sys) {
                alea_print_summary(new_sys);
                alea_destroy(new_sys);
            }
            printf("\n");
            continue;
        }
        if (!new_sys) {
            ag_color_printf(COL_RED, "Deleted file: %s\n", path);
            alea_destroy(old_sys);
            printf("\n");
            continue;
        }

        ag_fingerprint_set_t* old_fp = ag_fingerprint(old_sys);
        ag_fingerprint_set_t* new_fp = ag_fingerprint(new_sys);

        if (old_fp && new_fp) {
            ag_diff_result_t* diff = ag_diff(old_fp, new_fp);
            if (diff && (diff->cell_count > 0 || diff->surface_count > 0)) {
                char old_label[256], new_label[256];
                char* sha1 = ag_short_oid(git_commit_id(c1));
                if (workdir_mode) {
                    snprintf(old_label, sizeof(old_label), "%s (%s)", path, sha1);
                    snprintf(new_label, sizeof(new_label), "%s (working tree)", path);
                } else {
                    char* sha2 = ag_short_oid(git_commit_id(c2));
                    snprintf(old_label, sizeof(old_label), "%s (%s)", path, sha1);
                    snprintf(new_label, sizeof(new_label), "%s (%s)", path, sha2);
                    free(sha2);
                }
                ag_diff_print(diff, old_label, new_label);
                printf("\n");
                free(sha1);
            }
            ag_diff_result_free(diff);
        }

        ag_fingerprint_set_free(old_fp);
        ag_fingerprint_set_free(new_fp);
        alea_destroy(old_sys);
        alea_destroy(new_sys);
    }

    if (geom_files) ag_file_list_free(geom_files);
    git_commit_free(c1);
    if (c2) git_commit_free(c2);
    git_repository_free(repo);
    return rc;
}
