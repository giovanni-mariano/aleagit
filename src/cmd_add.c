#include "git_helpers.h"
#include "util.h"
#include <stdio.h>
#include <string.h>

static void print_usage(void) {
    printf("Usage: aleagit add <file>...\n");
    printf("       aleagit add -A|--all\n\n");
    printf("Stage files for the next commit.\n\n");
    printf("Options:\n");
    printf("  -A, --all    Stage all new, modified, and deleted files\n");
    printf("  -h, --help   Show this help\n");
}

int cmd_add(int argc, char** argv) {
    if (argc == 0) {
        ag_error("no files specified (use -A to stage all changes)");
        print_usage();
        return 1;
    }

    /* Check for --help / -h */
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        }
    }

    bool stage_all = false;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-A") == 0 || strcmp(argv[i], "--all") == 0) {
            stage_all = true;
            break;
        }
    }

    git_repository* repo = ag_repo_open();
    if (!repo) return 1;

    int rc = 1;
    git_index* index = NULL;

    if (git_repository_index(&index, repo) < 0) {
        ag_error("failed to open index");
        goto cleanup;
    }

    int staged = 0;

    if (stage_all) {
        /* Stage all new/modified files */
        char* all_pattern[] = {"."};
        git_strarray paths = {all_pattern, 1};
        if (git_index_add_all(index, &paths, 0, NULL, NULL) < 0) {
            ag_error("failed to stage new/modified files");
            goto cleanup;
        }
        /* Also pick up deletions */
        git_strarray empty = {NULL, 0};
        if (git_index_update_all(index, &empty, NULL, NULL) < 0) {
            ag_error("failed to stage deleted files");
            goto cleanup;
        }
        staged = -1; /* flag: report "all changes" */
    } else {
        for (int i = 0; i < argc; i++) {
            if (git_index_add_bypath(index, argv[i]) < 0) {
                const git_error* e = git_error_last();
                ag_error("failed to add '%s': %s", argv[i],
                         e ? e->message : "unknown error");
                goto cleanup;
            }
            staged++;
        }
    }

    if (git_index_write(index) < 0) {
        ag_error("failed to write index");
        goto cleanup;
    }

    if (staged == -1)
        ag_color_printf(COL_GREEN, "Staged all changes.\n");
    else
        ag_color_printf(COL_GREEN, "Staged %d file%s.\n",
                        staged, staged == 1 ? "" : "s");

    rc = 0;

cleanup:
    if (index) git_index_free(index);
    git_repository_free(repo);
    return rc;
}
