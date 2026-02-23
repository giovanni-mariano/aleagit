// SPDX-FileCopyrightText: 2026 Giovanni MARIANO
//
// SPDX-License-Identifier: MPL-2.0

#ifndef ALEAGIT_GEOM_LOAD_H
#define ALEAGIT_GEOM_LOAD_H

#include "aleagit.h"
#include <git2.h>

/* Detect format from filename and/or content */
geom_format_t ag_detect_format(const char* path, const char* data, size_t len);

/* Load geometry from an in-memory buffer.
   Returns a new alea_system_t* or NULL on error. */
alea_system_t* ag_load_geometry_buffer(const char* data, size_t len,
                                       geom_format_t format);

/* Load geometry from a file path on disk. */
alea_system_t* ag_load_geometry_file(const char* path);

/* Load geometry from a blob at a specific commit. */
alea_system_t* ag_load_geometry_commit(git_repository* repo,
                                       git_commit* commit,
                                       const char* path);

/* Load geometry from the working tree (on disk, relative to repo root). */
alea_system_t* ag_load_geometry_workdir(git_repository* repo, const char* path);

#endif /* ALEAGIT_GEOM_LOAD_H */
