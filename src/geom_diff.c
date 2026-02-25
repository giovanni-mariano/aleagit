// SPDX-FileCopyrightText: 2026 Giovanni MARIANO
//
// SPDX-License-Identifier: MPL-2.0

#include "geom_diff.h"
#include "util.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

ag_diff_result_t* ag_diff(const ag_fingerprint_set_t* old_fp,
                          const ag_fingerprint_set_t* new_fp) {
    ag_diff_result_t* r = calloc(1, sizeof(*r));
    if (!r) return NULL;

    /* --- Surface diff (two-pointer merge on sorted arrays) --- */
    size_t max_surfs = old_fp->surface_count + new_fp->surface_count;
    r->surfaces = calloc(max_surfs, sizeof(ag_surface_diff_t));
    size_t si = 0;

    size_t oi = 0, ni = 0;
    while (oi < old_fp->surface_count || ni < new_fp->surface_count) {
        const ag_surface_fp_t* o = oi < old_fp->surface_count ? &old_fp->surfaces[oi] : NULL;
        const ag_surface_fp_t* n = ni < new_fp->surface_count ? &new_fp->surfaces[ni] : NULL;

        if (o && n && o->surface_id == n->surface_id) {
            if (ag_surface_fp_compare(o, n) != 0) {
                r->surfaces[si].change = DIFF_MODIFIED;
                r->surfaces[si].id     = o->surface_id;
                r->surfaces[si].flags  = ag_surface_fp_diff(o, n);
                r->surfaces[si].old_fp = *o;
                r->surfaces[si].new_fp = *n;
                si++;
                r->surfs_modified++;
            }
            oi++; ni++;
        } else if (!n || (o && o->surface_id < n->surface_id)) {
            r->surfaces[si].change = DIFF_REMOVED;
            r->surfaces[si].id     = o->surface_id;
            r->surfaces[si].old_fp = *o;
            si++;
            r->surfs_removed++;
            oi++;
        } else {
            r->surfaces[si].change = DIFF_ADDED;
            r->surfaces[si].id     = n->surface_id;
            r->surfaces[si].new_fp = *n;
            si++;
            r->surfs_added++;
            ni++;
        }
    }
    r->surface_count = si;

    /* --- Cell diff --- */
    size_t max_cells = old_fp->cell_count + new_fp->cell_count;
    r->cells = calloc(max_cells, sizeof(ag_cell_diff_t));
    size_t ci = 0;

    oi = 0; ni = 0;
    while (oi < old_fp->cell_count || ni < new_fp->cell_count) {
        const ag_cell_fp_t* o = oi < old_fp->cell_count ? &old_fp->cells[oi] : NULL;
        const ag_cell_fp_t* n = ni < new_fp->cell_count ? &new_fp->cells[ni] : NULL;

        if (o && n && o->cell_id == n->cell_id) {
            if (ag_cell_fp_compare(o, n) != 0) {
                r->cells[ci].change = DIFF_MODIFIED;
                r->cells[ci].id     = o->cell_id;
                r->cells[ci].flags  = ag_cell_fp_diff(o, n);
                r->cells[ci].old_fp = *o;
                r->cells[ci].new_fp = *n;
                ci++;
                r->cells_modified++;
            }
            oi++; ni++;
        } else if (!n || (o && o->cell_id < n->cell_id)) {
            r->cells[ci].change = DIFF_REMOVED;
            r->cells[ci].id     = o->cell_id;
            r->cells[ci].old_fp = *o;
            ci++;
            r->cells_removed++;
            oi++;
        } else {
            r->cells[ci].change = DIFF_ADDED;
            r->cells[ci].id     = n->cell_id;
            r->cells[ci].new_fp = *n;
            ci++;
            r->cells_added++;
            ni++;
        }
    }
    r->cell_count = ci;

    return r;
}

void ag_diff_result_free(ag_diff_result_t* result) {
    if (!result) return;
    free(result->cells);
    free(result->surfaces);
    free(result);
}

static const char* prim_type_name(int ptype) {
    /* CSG_PRIMITIVE_PLANE = 1 (enum starts at 1) */
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

void ag_diff_print(const ag_diff_result_t* result,
                   const char* old_label, const char* new_label) {
    if (!result) return;

    bool has_changes = result->surface_count > 0 || result->cell_count > 0;
    if (!has_changes) {
        printf("No structural changes.\n");
        return;
    }

    ag_color_printf(COL_BOLD, "--- %s\n", old_label ? old_label : "a");
    ag_color_printf(COL_BOLD, "+++ %s\n", new_label ? new_label : "b");
    printf("\n");

    /* Surfaces */
    if (result->surface_count > 0) {
        ag_color_printf(COL_BOLD, "Surfaces:\n");
        for (size_t i = 0; i < result->surface_count; i++) {
            const ag_surface_diff_t* d = &result->surfaces[i];
            switch (d->change) {
                case DIFF_ADDED:
                    ag_color_printf(COL_GREEN, "  + surface %d: %s\n",
                                    d->id, prim_type_name(d->new_fp.primitive_type));
                    break;
                case DIFF_REMOVED:
                    ag_color_printf(COL_RED, "  - surface %d: %s\n",
                                    d->id, prim_type_name(d->old_fp.primitive_type));
                    break;
                case DIFF_MODIFIED: {
                    printf("  ");
                    ag_color_printf(COL_YELLOW, "~ surface %d:", d->id);
                    if (d->flags & SURF_CHG_TYPE)
                        printf(" type %s -> %s",
                               prim_type_name(d->old_fp.primitive_type),
                               prim_type_name(d->new_fp.primitive_type));
                    if (d->flags & SURF_CHG_DATA)
                        printf(" geometry changed");
                    if (d->flags & SURF_CHG_BOUNDARY)
                        printf(" boundary changed");
                    printf("\n");
                    break;
                }
                default: break;
            }
        }
        printf("\n");
    }

    /* Cells */
    if (result->cell_count > 0) {
        ag_color_printf(COL_BOLD, "Cells:\n");
        for (size_t i = 0; i < result->cell_count; i++) {
            const ag_cell_diff_t* d = &result->cells[i];
            switch (d->change) {
                case DIFF_ADDED:
                    ag_color_printf(COL_GREEN,
                        "  + cell %d: mat %d, density %.4g, universe %d\n",
                        d->id, d->new_fp.material_id, d->new_fp.density,
                        d->new_fp.universe_id);
                    break;
                case DIFF_REMOVED:
                    ag_color_printf(COL_RED,
                        "  - cell %d: mat %d, density %.4g, universe %d\n",
                        d->id, d->old_fp.material_id, d->old_fp.density,
                        d->old_fp.universe_id);
                    break;
                case DIFF_MODIFIED: {
                    printf("  ");
                    ag_color_printf(COL_YELLOW, "~ cell %d:", d->id);
                    if (d->flags & CELL_CHG_MATERIAL)
                        printf(" material %d -> %d",
                               d->old_fp.material_id, d->new_fp.material_id);
                    if (d->flags & CELL_CHG_DENSITY)
                        printf(" density %.4g -> %.4g",
                               d->old_fp.density, d->new_fp.density);
                    if (d->flags & CELL_CHG_REGION)
                        printf(" region changed");
                    if (d->flags & CELL_CHG_UNIVERSE)
                        printf(" universe %d -> %d",
                               d->old_fp.universe_id, d->new_fp.universe_id);
                    if (d->flags & CELL_CHG_FILL)
                        printf(" fill %d -> %d",
                               d->old_fp.fill_universe, d->new_fp.fill_universe);
                    if (d->flags & CELL_CHG_LATTICE)
                        printf(" lattice changed");
                    printf("\n");
                    break;
                }
                default: break;
            }
        }
        printf("\n");
    }

    /* Summary line */
    ag_color_printf(COL_BOLD, "Summary: ");
    printf("%d cells changed (", result->cells_added + result->cells_removed + result->cells_modified);
    ag_color_printf(COL_GREEN, "%d added", result->cells_added);
    printf(", ");
    ag_color_printf(COL_RED, "%d removed", result->cells_removed);
    printf(", ");
    ag_color_printf(COL_YELLOW, "%d modified", result->cells_modified);
    printf("), %d surfaces changed (", result->surfs_added + result->surfs_removed + result->surfs_modified);
    ag_color_printf(COL_GREEN, "%d added", result->surfs_added);
    printf(", ");
    ag_color_printf(COL_RED, "%d removed", result->surfs_removed);
    printf(", ");
    ag_color_printf(COL_YELLOW, "%d modified", result->surfs_modified);
    printf(")\n");
}
