// SPDX-FileCopyrightText: 2026 Giovanni MARIANO
//
// SPDX-License-Identifier: MPL-2.0

#include "aleagit.h"
#include "util.h"
#include <git2.h>
#include <stdio.h>
#include <string.h>

/* Command handlers (defined in cmd_*.c) */
int cmd_init(int argc, char** argv);
int cmd_summary(int argc, char** argv);
int cmd_status(int argc, char** argv);
int cmd_diff(int argc, char** argv);
int cmd_log(int argc, char** argv);
int cmd_blame(int argc, char** argv);
int cmd_validate(int argc, char** argv);
int cmd_add(int argc, char** argv);
int cmd_commit(int argc, char** argv);

typedef struct {
    const char* name;
    int (*handler)(int argc, char** argv);
    const char* description;
} aleagit_command_t;

static const aleagit_command_t commands[] = {
    {"init",     cmd_init,     "Initialize repo with geometry-aware settings"},
    {"summary",  cmd_summary,  "Print cell/surface/universe counts at a revision"},
    {"status",   cmd_status,   "Geometry-aware status of changed files"},
    {"diff",     cmd_diff,     "Semantic diff between revisions [--visual]"},
    {"log",      cmd_log,      "Per-element change history [--cell N] [--surface N]"},
    {"blame",    cmd_blame,    "Who last modified each element"},
    {"validate", cmd_validate, "Parse check + overlap detection [--pre-commit]"},
    {"add",      cmd_add,      "Stage files for commit"},
    {"commit",   cmd_commit,   "Commit with geometry change info [-m msg] [-a]"},
    {NULL, NULL, NULL}
};

static void print_usage(void) {
    printf("aleagit %s - geometry-aware version control for nuclear models\n\n",
           ALEAGIT_VERSION_STRING);
    printf("Usage: aleagit <command> [options]\n\n");
    printf("Commands:\n");
    for (int i = 0; commands[i].name; i++) {
        printf("  %-12s %s\n", commands[i].name, commands[i].description);
    }
    printf("\nRun 'aleagit <command> --help' for command-specific help.\n");
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-V") == 0) {
        printf("aleagit %s\n", ALEAGIT_VERSION_STRING);
        return 0;
    }

    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0 ||
        strcmp(argv[1], "help") == 0) {
        print_usage();
        return 0;
    }

    git_libgit2_init();

    int rc = 1;
    for (int i = 0; commands[i].name; i++) {
        if (strcmp(argv[1], commands[i].name) == 0) {
            rc = commands[i].handler(argc - 2, argv + 2);
            goto done;
        }
    }

    ag_error("unknown command '%s'", argv[1]);
    print_usage();

done:
    git_libgit2_shutdown();
    return rc;
}
