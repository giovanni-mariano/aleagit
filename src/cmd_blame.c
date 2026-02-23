// SPDX-FileCopyrightText: 2026 Giovanni MARIANO
//
// SPDX-License-Identifier: MPL-2.0

#include "git_helpers.h"
#include "geom_load.h"
#include "geom_fingerprint.h"
#include "util.h"
#include <alea.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

/* Cell blame entry */
typedef struct {
    int       cell_id;
    char      sha[8];
    char      author[64];
    char      date[20];
} cell_blame_t;

/* Surface blame entry */
typedef struct {
    int       surface_id;
    char      sha[8];
    char      author[64];
    char      date[20];
} surface_blame_t;

typedef struct {
    git_repository*       repo;
    const char*           path;
    ag_fingerprint_set_t* current_fp;
    cell_blame_t*         cell_blames;
    surface_blame_t*      surf_blames;
    size_t                nc, ns;
    bool                  first;
} blame_walk_t;

static int blame_walk_cb(git_commit* commit, const char* p,
                         const git_oid* blob_oid, void* payload) {
    blame_walk_t* w = payload;
    (void)p; (void)blob_oid;

    const git_signature* author = git_commit_author(commit);
    char* sha = ag_short_oid(git_commit_id(commit));
    time_t t = author->when.time;
    struct tm* tm = localtime(&t);
    char timebuf[20];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d", tm);

    if (w->first) {
        /* HEAD commit: set as default blame for everything */
        for (size_t i = 0; i < w->nc; i++) {
            strncpy(w->cell_blames[i].sha, sha, 7);
            w->cell_blames[i].sha[7] = '\0';
            strncpy(w->cell_blames[i].author, author->name, 63);
            w->cell_blames[i].author[63] = '\0';
            strncpy(w->cell_blames[i].date, timebuf, 19);
            w->cell_blames[i].date[19] = '\0';
        }
        for (size_t i = 0; i < w->ns; i++) {
            strncpy(w->surf_blames[i].sha, sha, 7);
            w->surf_blames[i].sha[7] = '\0';
            strncpy(w->surf_blames[i].author, author->name, 63);
            w->surf_blames[i].author[63] = '\0';
            strncpy(w->surf_blames[i].date, timebuf, 19);
            w->surf_blames[i].date[19] = '\0';
        }
        w->first = false;
        free(sha);
        return 0;
    }

    /* Load this commit's geometry and fingerprint */
    alea_system_t* sys = ag_load_geometry_commit(w->repo, commit, w->path);
    if (!sys) { free(sha); return 0; }

    ag_fingerprint_set_t* old_fp = ag_fingerprint(sys);
    alea_destroy(sys);
    if (!old_fp) { free(sha); return 0; }

    /* For each element: if it existed in old with same fingerprint,
       blame goes further back */
    for (size_t i = 0; i < w->nc; i++) {
        int cid = w->cell_blames[i].cell_id;
        for (size_t j = 0; j < old_fp->cell_count; j++) {
            if (old_fp->cells[j].cell_id == cid) {
                if (ag_cell_fp_compare(&w->current_fp->cells[i], &old_fp->cells[j]) == 0) {
                    strncpy(w->cell_blames[i].sha, sha, 7);
                    w->cell_blames[i].sha[7] = '\0';
                    strncpy(w->cell_blames[i].author, author->name, 63);
                    w->cell_blames[i].author[63] = '\0';
                    strncpy(w->cell_blames[i].date, timebuf, 19);
                    w->cell_blames[i].date[19] = '\0';
                }
                break;
            }
        }
    }

    for (size_t i = 0; i < w->ns; i++) {
        int sid = w->surf_blames[i].surface_id;
        for (size_t j = 0; j < old_fp->surface_count; j++) {
            if (old_fp->surfaces[j].surface_id == sid) {
                if (ag_surface_fp_compare(&w->current_fp->surfaces[i], &old_fp->surfaces[j]) == 0) {
                    strncpy(w->surf_blames[i].sha, sha, 7);
                    w->surf_blames[i].sha[7] = '\0';
                    strncpy(w->surf_blames[i].author, author->name, 63);
                    w->surf_blames[i].author[63] = '\0';
                    strncpy(w->surf_blames[i].date, timebuf, 19);
                    w->surf_blames[i].date[19] = '\0';
                }
                break;
            }
        }
    }

    ag_fingerprint_set_free(old_fp);
    free(sha);
    return 0;
}

int cmd_blame(int argc, char** argv) {
    const char* file = NULL;
    int target_cell = -1;
    int target_surface = -1;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--cell") == 0 && i + 1 < argc) {
            target_cell = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--surface") == 0 && i + 1 < argc) {
            target_surface = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--") == 0 && i + 1 < argc) {
            file = argv[i + 1];
            break;
        } else if (argv[i][0] != '-') {
            file = argv[i];
        }
    }

    git_repository* repo = ag_repo_open();
    if (!repo) return 1;

    ag_file_list_t* files = NULL;
    if (!file) {
        git_commit* head = ag_resolve_commit(repo, "HEAD");
        if (head) {
            files = ag_find_geometry_files(repo, head);
            if (files && files->count > 0)
                file = files->paths[0];
            git_commit_free(head);
        }
    }

    if (!file) {
        ag_error("no geometry file specified or found");
        if (files) ag_file_list_free(files);
        git_repository_free(repo);
        return 1;
    }

    git_commit* head = ag_resolve_commit(repo, "HEAD");
    if (!head) {
        if (files) ag_file_list_free(files);
        git_repository_free(repo);
        return 1;
    }

    alea_system_t* head_sys = ag_load_geometry_commit(repo, head, file);
    if (!head_sys) {
        ag_error("cannot load %s at HEAD", file);
        git_commit_free(head);
        if (files) ag_file_list_free(files);
        git_repository_free(repo);
        return 1;
    }

    ag_fingerprint_set_t* head_fp = ag_fingerprint(head_sys);
    alea_destroy(head_sys);

    size_t nc = head_fp->cell_count;
    size_t ns = head_fp->surface_count;
    cell_blame_t* cell_blames = calloc(nc, sizeof(cell_blame_t));
    surface_blame_t* surf_blames = calloc(ns, sizeof(surface_blame_t));

    for (size_t i = 0; i < nc; i++)
        cell_blames[i].cell_id = head_fp->cells[i].cell_id;
    for (size_t i = 0; i < ns; i++)
        surf_blames[i].surface_id = head_fp->surfaces[i].surface_id;

    blame_walk_t wd = {
        .repo = repo, .path = file,
        .current_fp = head_fp,
        .cell_blames = cell_blames, .surf_blames = surf_blames,
        .nc = nc, .ns = ns,
        .first = true
    };

    ag_walk_history(repo, file, blame_walk_cb, &wd);

    /* Print results */
    if (target_cell >= 0) {
        for (size_t i = 0; i < nc; i++) {
            if (cell_blames[i].cell_id == target_cell) {
                printf("cell %d: %s %s %s\n",
                       target_cell, cell_blames[i].sha,
                       cell_blames[i].date, cell_blames[i].author);
                break;
            }
        }
    } else if (target_surface >= 0) {
        for (size_t i = 0; i < ns; i++) {
            if (surf_blames[i].surface_id == target_surface) {
                printf("surface %d: %s %s %s\n",
                       target_surface, surf_blames[i].sha,
                       surf_blames[i].date, surf_blames[i].author);
                break;
            }
        }
    } else {
        ag_color_printf(COL_BOLD, "Surfaces:\n");
        for (size_t i = 0; i < ns; i++) {
            ag_color_printf(COL_YELLOW, "  %s", surf_blames[i].sha);
            printf(" %s %-20s surface %d\n",
                   surf_blames[i].date, surf_blames[i].author,
                   surf_blames[i].surface_id);
        }

        printf("\n");
        ag_color_printf(COL_BOLD, "Cells:\n");
        for (size_t i = 0; i < nc; i++) {
            ag_color_printf(COL_YELLOW, "  %s", cell_blames[i].sha);
            printf(" %s %-20s cell %d (mat %d)\n",
                   cell_blames[i].date, cell_blames[i].author,
                   cell_blames[i].cell_id,
                   head_fp->cells[i].material_id);
        }
    }

    free(cell_blames);
    free(surf_blames);
    ag_fingerprint_set_free(head_fp);
    git_commit_free(head);
    if (files) ag_file_list_free(files);
    git_repository_free(repo);
    return 0;
}
