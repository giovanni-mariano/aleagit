// SPDX-FileCopyrightText: 2026 Giovanni MARIANO
//
// SPDX-License-Identifier: MPL-2.0

#ifndef ALEAGIT_H
#define ALEAGIT_H

#define ALEAGIT_VERSION_MAJOR 0
#define ALEAGIT_VERSION_MINOR 1
#define ALEAGIT_VERSION_PATCH 0
#define ALEAGIT_VERSION_STRING "0.1.0"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Forward declarations */
typedef struct alea_system alea_system_t;

/* Geometry format */
typedef enum {
    GEOM_FORMAT_UNKNOWN = 0,
    GEOM_FORMAT_MCNP,
    GEOM_FORMAT_OPENMC
} geom_format_t;

/* Diff change types */
typedef enum {
    DIFF_UNCHANGED = 0,
    DIFF_ADDED,
    DIFF_REMOVED,
    DIFF_MODIFIED
} diff_change_t;

/* Cell change flags (bitfield) */
#define CELL_CHG_MATERIAL   (1 << 0)
#define CELL_CHG_DENSITY    (1 << 1)
#define CELL_CHG_REGION     (1 << 2)
#define CELL_CHG_UNIVERSE   (1 << 3)
#define CELL_CHG_FILL       (1 << 4)
#define CELL_CHG_LATTICE    (1 << 5)

/* Surface change flags (bitfield) */
#define SURF_CHG_TYPE       (1 << 0)
#define SURF_CHG_DATA       (1 << 1)
#define SURF_CHG_BOUNDARY   (1 << 2)

/* Geometry file extensions recognized */
static const char* const GEOM_EXTENSIONS[] = {
    ".inp", ".i", ".mcnp", ".xml", NULL
};

#endif /* ALEAGIT_H */
