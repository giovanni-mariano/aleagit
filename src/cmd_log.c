#include "git_helpers.h"
#include "geom_load.h"
#include "geom_fingerprint.h"
#include "util.h"
#include <alea.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct {
    git_repository* repo;
    const char*     path;
    int             filter_cell;     /* -1 = no filter */
    int             filter_surface;  /* -1 = no filter */
    int             max_entries;
    int             count;
} log_ctx_t;

/* Find a cell fingerprint by cell_id in a set */
static const ag_cell_fp_t* find_cell_fp(const ag_fingerprint_set_t* fp, int cell_id) {
    for (size_t i = 0; i < fp->cell_count; i++) {
        if (fp->cells[i].cell_id == cell_id) return &fp->cells[i];
    }
    return NULL;
}

/* Find a surface fingerprint by surface_id in a set */
static const ag_surface_fp_t* find_surface_fp(const ag_fingerprint_set_t* fp, int surface_id) {
    for (size_t i = 0; i < fp->surface_count; i++) {
        if (fp->surfaces[i].surface_id == surface_id) return &fp->surfaces[i];
    }
    return NULL;
}

static int log_callback(git_commit* commit, const char* path,
                         const git_oid* blob_oid, void* payload) {
    log_ctx_t* ctx = payload;
    (void)blob_oid;

    if (ctx->max_entries > 0 && ctx->count >= ctx->max_entries)
        return 1; /* stop */

    /* Load geometry at this commit */
    alea_system_t* sys = ag_load_geometry_commit(ctx->repo, commit, path);
    if (!sys) return 0; /* skip on error, continue walking */

    ag_fingerprint_set_t* fp = ag_fingerprint(sys);
    alea_destroy(sys);
    if (!fp) return 0;

    /* If filtering by element, check if element exists */
    bool show = true;
    if (ctx->filter_cell >= 0) {
        show = find_cell_fp(fp, ctx->filter_cell) != NULL;
    }
    if (ctx->filter_surface >= 0) {
        show = find_surface_fp(fp, ctx->filter_surface) != NULL;
    }

    ag_fingerprint_set_free(fp);
    if (!show) return 0;

    /* Print commit info */
    char* sha = ag_short_oid(git_commit_id(commit));
    const git_signature* author = git_commit_author(commit);
    const char* msg = git_commit_message(commit);

    /* Format time */
    time_t t = author->when.time;
    struct tm* tm = localtime(&t);
    char timebuf[64];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M", tm);

    ag_color_printf(COL_YELLOW, "%s", sha);
    printf(" %s ", timebuf);
    ag_color_printf(COL_BOLD, "%s", author->name);

    /* Print first line of message */
    const char* nl = strchr(msg, '\n');
    if (nl)
        printf(" %.*s\n", (int)(nl - msg), msg);
    else
        printf(" %s\n", msg);

    free(sha);
    ctx->count++;
    return 0;
}

int cmd_log(int argc, char** argv) {
    const char* file = NULL;
    int filter_cell = -1;
    int filter_surface = -1;
    int max_entries = 50;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--cell") == 0 && i + 1 < argc) {
            filter_cell = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--surface") == 0 && i + 1 < argc) {
            filter_surface = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            max_entries = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--") == 0 && i + 1 < argc) {
            file = argv[i + 1];
            break;
        } else if (argv[i][0] != '-') {
            file = argv[i];
        }
    }

    git_repository* repo = ag_repo_open();
    if (!repo) return 1;

    /* If no file specified, find first geometry file */
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

    if (filter_cell >= 0)
        printf("History for cell %d in %s:\n\n", filter_cell, file);
    else if (filter_surface >= 0)
        printf("History for surface %d in %s:\n\n", filter_surface, file);
    else
        printf("History for %s:\n\n", file);

    log_ctx_t ctx = {
        .repo = repo,
        .path = file,
        .filter_cell = filter_cell,
        .filter_surface = filter_surface,
        .max_entries = max_entries,
        .count = 0
    };

    ag_walk_history(repo, file, log_callback, &ctx);

    if (ctx.count == 0)
        printf("  (no commits found)\n");

    if (files) ag_file_list_free(files);
    git_repository_free(repo);
    return 0;
}
