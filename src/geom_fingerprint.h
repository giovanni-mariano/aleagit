// SPDX-FileCopyrightText: 2026 Giovanni MARIANO
//
// SPDX-License-Identifier: MPL-2.0

#ifndef ALEAGIT_GEOM_FINGERPRINT_H
#define ALEAGIT_GEOM_FINGERPRINT_H

#include <stdint.h>
#include <stddef.h>

typedef struct alea_system alea_system_t;

/* Cell fingerprint */
typedef struct {
    int     cell_id;
    int     material_id;
    int     universe_id;
    int     fill_universe;
    int     lat_type;
    double  density;
    uint64_t tree_hash;
    uint64_t lattice_hash;
} ag_cell_fp_t;

/* Surface fingerprint */
typedef struct {
    int     surface_id;
    int     primitive_type;
    int     boundary_type;
    uint64_t data_hash;
} ag_surface_fp_t;

/* Fingerprint set for an entire geometry */
typedef struct {
    ag_cell_fp_t*    cells;
    size_t           cell_count;
    ag_surface_fp_t* surfaces;
    size_t           surface_count;
} ag_fingerprint_set_t;

/* Build fingerprints for all cells and surfaces. Caller must free with ag_fingerprint_set_free(). */
ag_fingerprint_set_t* ag_fingerprint(const alea_system_t* sys);

void ag_fingerprint_set_free(ag_fingerprint_set_t* fp);

/* Compare two cell fingerprints. Returns 0 if equal. */
int ag_cell_fp_compare(const ag_cell_fp_t* a, const ag_cell_fp_t* b);

/* Compare two surface fingerprints. Returns 0 if equal. */
int ag_surface_fp_compare(const ag_surface_fp_t* a, const ag_surface_fp_t* b);

/* Return bitfield of what changed between two cell fingerprints (CELL_CHG_*) */
uint32_t ag_cell_fp_diff(const ag_cell_fp_t* a, const ag_cell_fp_t* b);

/* Return bitfield of what changed between two surface fingerprints (SURF_CHG_*) */
uint32_t ag_surface_fp_diff(const ag_surface_fp_t* a, const ag_surface_fp_t* b);

#endif /* ALEAGIT_GEOM_FINGERPRINT_H */
