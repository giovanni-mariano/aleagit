#include "git_helpers.h"
#include "geom_load.h"
#include "util.h"
#include <alea.h>
#include <stdio.h>
#include <string.h>

int cmd_summary(int argc, char** argv) {
    const char* rev = "HEAD";
    const char* file = NULL;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--") == 0 && i + 1 < argc) {
            file = argv[i + 1];
            break;
        }
        if (argv[i][0] != '-')
            rev = argv[i];
    }

    git_repository* repo = ag_repo_open();
    if (!repo) return 1;

    git_commit* commit = ag_resolve_commit(repo, rev);
    if (!commit) {
        git_repository_free(repo);
        return 1;
    }

    char* sha = ag_short_oid(git_commit_id(commit));

    /* If no file specified, find all geometry files */
    ag_file_list_t* files = NULL;
    if (!file) {
        files = ag_find_geometry_files(repo, commit);
        if (!files || files->count == 0) {
            ag_error("no geometry files found at %s", sha);
            free(sha);
            if (files) ag_file_list_free(files);
            git_commit_free(commit);
            git_repository_free(repo);
            return 1;
        }
    }

    size_t nfiles = file ? 1 : files->count;
    for (size_t fi = 0; fi < nfiles; fi++) {
        const char* path = file ? file : files->paths[fi];

        alea_system_t* sys = ag_load_geometry_commit(repo, commit, path);
        if (!sys) {
            ag_warn("failed to load '%s' at %s", path, sha);
            continue;
        }

        ag_color_printf(COL_BOLD, "%s", path);
        printf(" @ %s\n", sha);

        alea_print_summary(sys);
        printf("\n");

        alea_destroy(sys);
    }

    free(sha);
    if (files) ag_file_list_free(files);
    git_commit_free(commit);
    git_repository_free(repo);
    return 0;
}
