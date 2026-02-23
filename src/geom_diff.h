// SPDX-FileCopyrightText: 2026 Giovanni MARIANO
//
// SPDX-License-Identifier: MPL-2.0

#ifndef ALEAGIT_GEOM_DIFF_H
#define ALEAGIT_GEOM_DIFF_H

#include "aleagit.h"
#include "geom_fingerprint.h"
#include <stddef.h>

/* A single diff entry for a cell */
typedef struct {
    diff_change_t change;
    int           id;         /* cell_id */
    uint32_t      flags;      /* CELL_CHG_* bitfield (for MODIFIED) */
    ag_cell_fp_t  old_fp;     /* valid if REMOVED or MODIFIED */
    ag_cell_fp_t  new_fp;     /* valid if ADDED or MODIFIED */
} ag_cell_diff_t;

/* A single diff entry for a surface */
typedef struct {
    diff_change_t   change;
    int             id;       /* surface_id */
    uint32_t        flags;    /* SURF_CHG_* bitfield */
    ag_surface_fp_t old_fp;
    ag_surface_fp_t new_fp;
} ag_surface_diff_t;

/* Complete structural diff result */
typedef struct {
    ag_cell_diff_t*    cells;
    size_t             cell_count;
    ag_surface_diff_t* surfaces;
    size_t             surface_count;

    /* Summary counts */
    int cells_added, cells_removed, cells_modified;
    int surfs_added, surfs_removed, surfs_modified;
} ag_diff_result_t;

/* Compute structural diff between two fingerprint sets.
   Caller must ag_diff_result_free(). */
ag_diff_result_t* ag_diff(const ag_fingerprint_set_t* old_fp,
                          const ag_fingerprint_set_t* new_fp);

void ag_diff_result_free(ag_diff_result_t* result);

/* Print the diff to stdout in text format */
void ag_diff_print(const ag_diff_result_t* result,
                   const char* old_label, const char* new_label);

#endif /* ALEAGIT_GEOM_DIFF_H */
