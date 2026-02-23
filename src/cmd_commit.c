#include "git_helpers.h"
#include "geom_load.h"
#include "geom_fingerprint.h"
#include "geom_diff.h"
#include "util.h"
#include <alea.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ */
/*  Primitive type name (mirrored from geom_diff.c)                   */
/* ------------------------------------------------------------------ */

static const char* prim_type_name(int ptype) {
    switch (ptype) {
        case 1:  return "plane";
        case 2:  return "sphere";
        case 3:  return "cylinder_x";
        case 4:  return "cylinder_y";
        case 5:  return "cylinder_z";
        case 6:  return "cone_x";
        case 7:  return "cone_y";
        case 8:  return "cone_z";
        case 9:  return "box";
        case 10: return "quadric";
        case 11: return "torus_x";
        case 12: return "torus_y";
        case 13: return "torus_z";
        case 14: return "rcc";
        case 15: return "box_general";
        case 16: return "sph";
        case 17: return "trc";
        case 18: return "ell";
        case 19: return "rec";
        case 20: return "wed";
        case 21: return "rhp";
        case 22: return "arb";
        default: return "unknown";
    }
}

/* ------------------------------------------------------------------ */
/*  Dynamic string buffer                                             */
/* ------------------------------------------------------------------ */

typedef struct {
    char*  data;
    size_t len;
    size_t cap;
} strbuf_t;

static void sb_init(strbuf_t* sb) {
    sb->cap = 1024;
    sb->data = malloc(sb->cap);
    sb->data[0] = '\0';
    sb->len = 0;
}

static void sb_appendf(strbuf_t* sb, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int needed = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (needed < 0) return;

    while (sb->len + (size_t)needed + 1 > sb->cap) {
        sb->cap *= 2;
        sb->data = realloc(sb->data, sb->cap);
    }

    va_start(ap, fmt);
    vsnprintf(sb->data + sb->len, sb->cap - sb->len, fmt, ap);
    va_end(ap);
    sb->len += (size_t)needed;
}

static void sb_free(strbuf_t* sb) {
    free(sb->data);
    sb->data = NULL;
    sb->len = sb->cap = 0;
}

/* ------------------------------------------------------------------ */
/*  Geometry file check                                               */
/* ------------------------------------------------------------------ */

static bool is_geometry_file(const char* path) {
    static const char* exts[] = {".inp", ".i", ".mcnp", ".xml", NULL};
    for (int i = 0; exts[i]; i++) {
        if (ag_str_ends_with(path, exts[i])) return true;
    }
    return false;
}

/* ------------------------------------------------------------------ */
/*  Load geometry from the index (staged content)                     */
/* ------------------------------------------------------------------ */

static alea_system_t* load_staged_geometry(git_repository* repo,
                                           const char* path) {
    size_t len;
    char* data = ag_read_staged_blob(repo, path, &len);
    if (!data) return NULL;

    geom_format_t fmt = ag_detect_format(path, data, len);
    alea_system_t* sys = ag_load_geometry_buffer(data, len, fmt);
    free(data);
    return sys;
}

/* ------------------------------------------------------------------ */
/*  Format geometry diff as commit trailer text                       */
/* ------------------------------------------------------------------ */

#define MAX_DETAIL_LINES 30

static void format_diff_trailer(strbuf_t* sb,
                                const char* path,
                                const ag_diff_result_t* diff) {
    sb_appendf(sb, "Geometry-Change: %s\n", path);
    sb_appendf(sb, "  cells: +%d -%d ~%d | surfaces: +%d -%d ~%d\n",
               diff->cells_added, diff->cells_removed, diff->cells_modified,
               diff->surfs_added, diff->surfs_removed, diff->surfs_modified);

    int detail_count = 0;
    size_t total_details = diff->surface_count + diff->cell_count;

    /* Surface details */
    for (size_t i = 0; i < diff->surface_count && detail_count < MAX_DETAIL_LINES; i++) {
        const ag_surface_diff_t* d = &diff->surfaces[i];
        switch (d->change) {
            case DIFF_ADDED:
                sb_appendf(sb, "  + surface %d (%s)\n",
                           d->id, prim_type_name(d->new_fp.primitive_type));
                detail_count++;
                break;
            case DIFF_REMOVED:
                sb_appendf(sb, "  - surface %d (%s)\n",
                           d->id, prim_type_name(d->old_fp.primitive_type));
                detail_count++;
                break;
            case DIFF_MODIFIED:
                sb_appendf(sb, "  ~ surface %d:", d->id);
                if (d->flags & SURF_CHG_TYPE)
                    sb_appendf(sb, " type %s -> %s",
                               prim_type_name(d->old_fp.primitive_type),
                               prim_type_name(d->new_fp.primitive_type));
                if (d->flags & SURF_CHG_DATA)
                    sb_appendf(sb, " coefficients changed");
                if (d->flags & SURF_CHG_BOUNDARY)
                    sb_appendf(sb, " boundary changed");
                sb_appendf(sb, "\n");
                detail_count++;
                break;
            default:
                break;
        }
    }

    /* Cell details */
    for (size_t i = 0; i < diff->cell_count && detail_count < MAX_DETAIL_LINES; i++) {
        const ag_cell_diff_t* d = &diff->cells[i];
        switch (d->change) {
            case DIFF_ADDED:
                sb_appendf(sb, "  + cell %d (mat %d, universe %d)\n",
                           d->id, d->new_fp.material_id, d->new_fp.universe_id);
                detail_count++;
                break;
            case DIFF_REMOVED:
                sb_appendf(sb, "  - cell %d (mat %d, universe %d)\n",
                           d->id, d->old_fp.material_id, d->old_fp.universe_id);
                detail_count++;
                break;
            case DIFF_MODIFIED:
                sb_appendf(sb, "  ~ cell %d:", d->id);
                if (d->flags & CELL_CHG_MATERIAL)
                    sb_appendf(sb, " material %d -> %d",
                               d->old_fp.material_id, d->new_fp.material_id);
                if (d->flags & CELL_CHG_DENSITY)
                    sb_appendf(sb, " density %.4g -> %.4g",
                               d->old_fp.density, d->new_fp.density);
                if (d->flags & CELL_CHG_REGION)
                    sb_appendf(sb, " region changed");
                if (d->flags & CELL_CHG_UNIVERSE)
                    sb_appendf(sb, " universe %d -> %d",
                               d->old_fp.universe_id, d->new_fp.universe_id);
                if (d->flags & CELL_CHG_FILL)
                    sb_appendf(sb, " fill %d -> %d",
                               d->old_fp.fill_universe, d->new_fp.fill_universe);
                if (d->flags & CELL_CHG_LATTICE)
                    sb_appendf(sb, " lattice changed");
                sb_appendf(sb, "\n");
                detail_count++;
                break;
            default:
                break;
        }
    }

    if ((size_t)detail_count < total_details) {
        sb_appendf(sb, "  ... and %zu more\n",
                   total_details - (size_t)detail_count);
    }
}

static void format_new_file_trailer(strbuf_t* sb,
                                    const char* path,
                                    const alea_system_t* sys) {
    size_t nc = alea_cell_count(sys);
    size_t ns = alea_surface_count(sys);
    sb_appendf(sb, "Geometry-New: %s (%zu cells, %zu surfaces)\n", path, nc, ns);
}

static void format_deleted_trailer(strbuf_t* sb, const char* path) {
    sb_appendf(sb, "Geometry-Deleted: %s\n", path);
}

/* ------------------------------------------------------------------ */
/*  Print console summary (colored)                                   */
/* ------------------------------------------------------------------ */

static void print_diff_summary(const char* path,
                               const ag_diff_result_t* diff) {
    printf("  %s: ", path);
    ag_color_printf(COL_DIM, "cells ");
    ag_color_printf(COL_GREEN, "+%d ", diff->cells_added);
    ag_color_printf(COL_RED, "-%d ", diff->cells_removed);
    ag_color_printf(COL_YELLOW, "~%d", diff->cells_modified);
    ag_color_printf(COL_DIM, " | surfaces ");
    ag_color_printf(COL_GREEN, "+%d ", diff->surfs_added);
    ag_color_printf(COL_RED, "-%d ", diff->surfs_removed);
    ag_color_printf(COL_YELLOW, "~%d", diff->surfs_modified);
    printf("\n");

    /* Print up to 10 detail lines to console */
    int shown = 0;
    for (size_t i = 0; i < diff->surface_count && shown < 10; i++) {
        const ag_surface_diff_t* d = &diff->surfaces[i];
        switch (d->change) {
            case DIFF_ADDED:
                ag_color_printf(COL_GREEN, "    + surface %d (%s)\n",
                                d->id, prim_type_name(d->new_fp.primitive_type));
                shown++; break;
            case DIFF_REMOVED:
                ag_color_printf(COL_RED, "    - surface %d (%s)\n",
                                d->id, prim_type_name(d->old_fp.primitive_type));
                shown++; break;
            case DIFF_MODIFIED: {
                printf("    ");
                ag_color_printf(COL_YELLOW, "~ surface %d:", d->id);
                if (d->flags & SURF_CHG_TYPE)
                    printf(" type %s -> %s",
                           prim_type_name(d->old_fp.primitive_type),
                           prim_type_name(d->new_fp.primitive_type));
                if (d->flags & SURF_CHG_DATA) printf(" coefficients changed");
                if (d->flags & SURF_CHG_BOUNDARY) printf(" boundary changed");
                printf("\n");
                shown++; break;
            }
            default: break;
        }
    }
    for (size_t i = 0; i < diff->cell_count && shown < 10; i++) {
        const ag_cell_diff_t* d = &diff->cells[i];
        switch (d->change) {
            case DIFF_ADDED:
                ag_color_printf(COL_GREEN, "    + cell %d (mat %d, universe %d)\n",
                                d->id, d->new_fp.material_id, d->new_fp.universe_id);
                shown++; break;
            case DIFF_REMOVED:
                ag_color_printf(COL_RED, "    - cell %d (mat %d, universe %d)\n",
                                d->id, d->old_fp.material_id, d->old_fp.universe_id);
                shown++; break;
            case DIFF_MODIFIED: {
                printf("    ");
                ag_color_printf(COL_YELLOW, "~ cell %d:", d->id);
                if (d->flags & CELL_CHG_MATERIAL)
                    printf(" material %d -> %d",
                           d->old_fp.material_id, d->new_fp.material_id);
                if (d->flags & CELL_CHG_DENSITY)
                    printf(" density %.4g -> %.4g",
                           d->old_fp.density, d->new_fp.density);
                if (d->flags & CELL_CHG_REGION) printf(" region changed");
                if (d->flags & CELL_CHG_UNIVERSE)
                    printf(" universe %d -> %d",
                           d->old_fp.universe_id, d->new_fp.universe_id);
                if (d->flags & CELL_CHG_FILL)
                    printf(" fill %d -> %d",
                           d->old_fp.fill_universe, d->new_fp.fill_universe);
                if (d->flags & CELL_CHG_LATTICE) printf(" lattice changed");
                printf("\n");
                shown++; break;
            }
            default: break;
        }
    }
    size_t total = diff->surface_count + diff->cell_count;
    if (total > 10)
        printf("    ... and %zu more\n", total - 10);
}

/* ------------------------------------------------------------------ */
/*  Main command                                                      */
/* ------------------------------------------------------------------ */

int cmd_commit(int argc, char** argv) {
    const char* message = NULL;
    bool stage_all = false;

    /* Parse arguments */
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
            message = argv[++i]; continue;
        }
        if (strcmp(argv[i], "-a") == 0) {
            stage_all = true; continue;
        }
    }

    if (!message) {
        ag_error("commit message required: aleagit commit -m \"message\"");
        return 1;
    }

    git_repository* repo = ag_repo_open();
    if (!repo) return 1;

    int rc = 1;
    git_index* index = NULL;
    git_signature* sig = NULL;
    git_tree* tree = NULL;
    git_commit* head_commit = NULL;

    /* Get index */
    if (git_repository_index(&index, repo) < 0) {
        ag_error("failed to open index");
        goto cleanup;
    }

    /* -a: stage all modified tracked files */
    if (stage_all) {
        git_strarray paths = {NULL, 0};
        if (git_index_update_all(index, &paths, NULL, NULL) < 0) {
            ag_error("failed to stage modified files");
            goto cleanup;
        }
        if (git_index_write(index) < 0) {
            ag_error("failed to write index");
            goto cleanup;
        }
    }

    /* Check for staged changes */
    git_status_list* status_list = NULL;
    git_status_options sopts = GIT_STATUS_OPTIONS_INIT;
    sopts.show = GIT_STATUS_SHOW_INDEX_ONLY;
    if (git_status_list_new(&status_list, repo, &sopts) < 0) {
        ag_error("failed to read status");
        goto cleanup;
    }

    size_t nstaged = git_status_list_entrycount(status_list);
    if (nstaged == 0) {
        ag_error("nothing to commit (no staged changes)");
        git_status_list_free(status_list);
        goto cleanup;
    }

    /* Resolve HEAD (may be NULL for initial commit) */
    git_oid head_oid;
    bool has_head = (git_reference_name_to_id(&head_oid, repo, "HEAD") == 0);
    if (has_head) {
        git_commit_lookup(&head_commit, repo, &head_oid);
    }

    /* Collect staged geometry files and compute diffs */
    strbuf_t trailer;
    sb_init(&trailer);
    bool has_geom_changes = false;

    for (size_t i = 0; i < nstaged; i++) {
        const git_status_entry* se = git_status_byindex(status_list, i);
        if (!se->head_to_index) continue;

        const char* path = se->head_to_index->new_file.path;
        unsigned st = se->status;

        if (!is_geometry_file(path)) continue;

        if (st & GIT_STATUS_INDEX_DELETED) {
            /* Geometry file deleted */
            if (has_geom_changes) sb_appendf(&trailer, "\n");
            format_deleted_trailer(&trailer, path);
            has_geom_changes = true;

            printf("  %s: ", path);
            ag_color_printf(COL_RED, "deleted\n");
            continue;
        }

        if (st & GIT_STATUS_INDEX_NEW) {
            /* New geometry file */
            alea_system_t* new_sys = load_staged_geometry(repo, path);
            if (new_sys) {
                if (has_geom_changes) sb_appendf(&trailer, "\n");
                format_new_file_trailer(&trailer, path, new_sys);
                has_geom_changes = true;

                size_t nc = alea_cell_count(new_sys);
                size_t ns = alea_surface_count(new_sys);
                printf("  %s: ", path);
                ag_color_printf(COL_GREEN, "new file (%zu cells, %zu surfaces)\n",
                                nc, ns);
                alea_destroy(new_sys);
            }
            continue;
        }

        if (st & GIT_STATUS_INDEX_MODIFIED) {
            /* Modified geometry file — compute semantic diff */
            alea_system_t* old_sys = has_head
                ? ag_load_geometry_commit(repo, head_commit, path)
                : NULL;
            alea_system_t* new_sys = load_staged_geometry(repo, path);

            if (old_sys && new_sys) {
                ag_fingerprint_set_t* old_fp = ag_fingerprint(old_sys);
                ag_fingerprint_set_t* new_fp = ag_fingerprint(new_sys);

                if (old_fp && new_fp) {
                    ag_diff_result_t* diff = ag_diff(old_fp, new_fp);
                    if (diff && (diff->cell_count > 0 || diff->surface_count > 0)) {
                        if (has_geom_changes) sb_appendf(&trailer, "\n");
                        format_diff_trailer(&trailer, path, diff);
                        has_geom_changes = true;
                        print_diff_summary(path, diff);
                    } else if (diff) {
                        printf("  %s: ", path);
                        ag_color_printf(COL_DIM, "no structural changes\n");
                    }
                    ag_diff_result_free(diff);
                }

                ag_fingerprint_set_free(old_fp);
                ag_fingerprint_set_free(new_fp);
            } else if (new_sys) {
                /* Couldn't load old — treat as new */
                if (has_geom_changes) sb_appendf(&trailer, "\n");
                format_new_file_trailer(&trailer, path, new_sys);
                has_geom_changes = true;
            }

            if (old_sys) alea_destroy(old_sys);
            if (new_sys) alea_destroy(new_sys);
        }
    }

    git_status_list_free(status_list);

    /* Build full commit message */
    strbuf_t full_msg;
    sb_init(&full_msg);
    sb_appendf(&full_msg, "%s", message);

    if (has_geom_changes) {
        sb_appendf(&full_msg, "\n\n");
        sb_appendf(&full_msg, "%s", trailer.data);
    }

    /* Create the commit */
    git_oid tree_oid;
    if (git_index_write_tree(&tree_oid, index) < 0) {
        ag_error("failed to write tree from index");
        sb_free(&full_msg);
        sb_free(&trailer);
        goto cleanup;
    }

    if (git_tree_lookup(&tree, repo, &tree_oid) < 0) {
        ag_error("failed to look up tree");
        sb_free(&full_msg);
        sb_free(&trailer);
        goto cleanup;
    }

    if (git_signature_default(&sig, repo) < 0) {
        /* Fallback if no user.name/email configured */
        if (git_signature_now(&sig, "aleagit", "aleagit@localhost") < 0) {
            ag_error("failed to create signature (set user.name and user.email in git config)");
            sb_free(&full_msg);
            sb_free(&trailer);
            goto cleanup;
        }
    }

    git_oid commit_oid;
    const git_commit* parents[1] = { head_commit };
    int nparents = has_head ? 1 : 0;

    int err = git_commit_create(&commit_oid, repo, "HEAD",
                                sig, sig, NULL,
                                full_msg.data,
                                tree, nparents, parents);
    if (err < 0) {
        const git_error* e = git_error_last();
        ag_error("failed to create commit: %s", e ? e->message : "unknown error");
        sb_free(&full_msg);
        sb_free(&trailer);
        goto cleanup;
    }

    /* Success — print result */
    char* sha = ag_short_oid(&commit_oid);
    printf("\n");
    ag_color_printf(COL_BOLD, "[%s]", sha);
    printf(" %s\n", message);
    free(sha);

    sb_free(&full_msg);
    sb_free(&trailer);
    rc = 0;

cleanup:
    if (tree) git_tree_free(tree);
    if (sig) git_signature_free(sig);
    if (head_commit) git_commit_free(head_commit);
    if (index) git_index_free(index);
    git_repository_free(repo);
    return rc;
}
