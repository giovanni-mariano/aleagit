// SPDX-FileCopyrightText: 2026 Giovanni MARIANO
//
// SPDX-License-Identifier: MPL-2.0

#include "util.h"
#include <git2.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define ag_mkdir(path) _mkdir(path)
#else
#define ag_mkdir(path) mkdir(path, 0755)
#endif

static const char* GITATTRIBUTES_CONTENT =
    "# AleaGit - geometry-aware version control\n"
    "*.inp  diff=mcnp\n"
    "*.i    diff=mcnp\n"
    "*.mcnp diff=mcnp\n"
    "*.xml  diff=openmc\n";

static const char* PRE_COMMIT_HOOK =
    "#!/bin/sh\n"
    "# AleaGit pre-commit hook: validate geometry files before commit\n"
    "exec aleagit validate --pre-commit\n";

int cmd_init(int argc, char** argv) {
    bool install_hook = false;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--hook") == 0)
            install_hook = true;
    }

    /* Initialize or open repo */
    git_repository* repo = NULL;
    int err = git_repository_open_ext(&repo, ".", 0, NULL);
    if (err < 0) {
        err = git_repository_init(&repo, ".", 0);
        if (err < 0) {
            ag_error("failed to initialize git repository");
            return 1;
        }
        printf("Initialized git repository.\n");
    } else {
        printf("Git repository already exists.\n");
    }

    /* Write .gitattributes */
    const char* workdir = git_repository_workdir(repo);
    char path[4096];
    snprintf(path, sizeof(path), "%s.gitattributes", workdir);

    FILE* f = fopen(path, "r");
    if (f) {
        /* Check if our content is already there */
        char buf[1024] = {0};
        size_t n = fread(buf, 1, sizeof(buf) - 1, f);
        fclose(f);
        buf[n] = '\0';
        if (strstr(buf, "diff=mcnp")) {
            printf(".gitattributes already configured.\n");
        } else {
            f = fopen(path, "a");
            if (f) {
                fprintf(f, "\n%s", GITATTRIBUTES_CONTENT);
                fclose(f);
                printf("Updated .gitattributes with geometry file patterns.\n");
            }
        }
    } else {
        f = fopen(path, "w");
        if (f) {
            fputs(GITATTRIBUTES_CONTENT, f);
            fclose(f);
            printf("Created .gitattributes with geometry file patterns.\n");
        }
    }

    /* Install pre-commit hook */
    if (install_hook) {
        snprintf(path, sizeof(path), "%shooks", git_repository_path(repo));
        ag_mkdir(path);
        snprintf(path, sizeof(path), "%shooks/pre-commit", git_repository_path(repo));

        f = fopen(path, "w");
        if (f) {
            fputs(PRE_COMMIT_HOOK, f);
            fclose(f);
            chmod(path, 0755);
            printf("Installed pre-commit hook.\n");
        } else {
            ag_warn("could not write pre-commit hook");
        }
    }

    git_repository_free(repo);
    return 0;
}
