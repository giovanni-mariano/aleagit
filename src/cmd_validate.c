// SPDX-FileCopyrightText: 2026 Giovanni MARIANO
//
// SPDX-License-Identifier: MPL-2.0

#include "git_helpers.h"
#include "geom_load.h"
#include "util.h"
#include <alea.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int validate_system(alea_system_t* sys, const char* path) {
    int errors = 0;

    ag_color_printf(COL_BOLD, "Validating %s\n", path);

    /* Print summary */
    size_t nc = alea_cell_count(sys);
    size_t ns = alea_surface_count(sys);
    size_t nu = alea_universe_count(sys);
    printf("  cells: %zu, surfaces: %zu, universes: %zu\n", nc, ns, nu);

    /* Build indices for overlap check */
    if (alea_build_universe_index(sys) < 0) {
        ag_error("  failed to build universe index");
        errors++;
    }
    if (alea_build_spatial_index(sys) < 0) {
        ag_error("  failed to build spatial index");
        errors++;
    }

    /* Check overlaps */
    int pairs[256];
    int noverlaps = alea_find_overlaps(sys, pairs, 128);
    if (noverlaps > 0) {
        ag_color_printf(COL_RED, "  %d overlap(s) detected:\n", noverlaps);
        for (int i = 0; i < noverlaps && i < 128; i++) {
            printf("    cell %d <-> cell %d\n", pairs[i * 2], pairs[i * 2 + 1]);
        }
        errors += noverlaps;
    } else {
        ag_color_printf(COL_GREEN, "  no overlaps detected\n");
    }

    return errors;
}

int cmd_validate(int argc, char** argv) {
    bool pre_commit = false;
    const char* file = NULL;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--pre-commit") == 0)
            pre_commit = true;
        else if (strcmp(argv[i], "--") == 0 && i + 1 < argc)
            file = argv[i + 1];
        else if (argv[i][0] != '-')
            file = argv[i];
    }

    git_repository* repo = ag_repo_open();
    if (!repo) return 1;

    int total_errors = 0;

    if (pre_commit) {
        /* Validate staged geometry files */
        git_index* index = NULL;
        if (git_repository_index(&index, repo) < 0) {
            ag_error("cannot read git index");
            git_repository_free(repo);
            return 1;
        }

        size_t entry_count = git_index_entrycount(index);
        for (size_t i = 0; i < entry_count; i++) {
            const git_index_entry* entry = git_index_get_byindex(index, i);
            if (!entry) continue;

            bool is_geom = false;
            const char* exts[] = {".inp", ".i", ".mcnp", ".xml", NULL};
            for (int e = 0; exts[e]; e++) {
                if (ag_str_ends_with(entry->path, exts[e])) { is_geom = true; break; }
            }
            if (!is_geom) continue;

            /* Read staged content */
            size_t len = 0;
            char* data = ag_read_staged_blob(repo, entry->path, &len);
            if (!data) continue;

            geom_format_t fmt = ag_detect_format(entry->path, data, len);
            alea_system_t* sys = ag_load_geometry_buffer(data, len, fmt);
            free(data);

            if (!sys) {
                ag_error("failed to parse %s", entry->path);
                total_errors++;
                continue;
            }

            total_errors += validate_system(sys, entry->path);
            alea_destroy(sys);
        }

        git_index_free(index);
    } else if (file) {
        /* Validate a specific file from disk */
        alea_system_t* sys = ag_load_geometry_file(file);
        if (!sys) {
            ag_error("failed to parse %s", file);
            total_errors = 1;
        } else {
            total_errors = validate_system(sys, file);
            alea_destroy(sys);
        }
    } else {
        /* Validate all geometry files in HEAD */
        git_commit* head = ag_resolve_commit(repo, "HEAD");
        if (!head) {
            git_repository_free(repo);
            return 1;
        }

        ag_file_list_t* files = ag_find_geometry_files(repo, head);
        if (files) {
            for (size_t i = 0; i < files->count; i++) {
                alea_system_t* sys = ag_load_geometry_commit(repo, head, files->paths[i]);
                if (!sys) {
                    ag_error("failed to parse %s", files->paths[i]);
                    total_errors++;
                    continue;
                }
                total_errors += validate_system(sys, files->paths[i]);
                alea_destroy(sys);
            }
            ag_file_list_free(files);
        }
        git_commit_free(head);
    }

    git_repository_free(repo);

    if (total_errors > 0) {
        printf("\n");
        ag_color_printf(COL_RED, "Validation failed with %d error(s).\n", total_errors);
        return 1;
    }

    printf("\n");
    ag_color_printf(COL_GREEN, "Validation passed.\n");
    return 0;
}
