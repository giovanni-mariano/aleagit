// SPDX-FileCopyrightText: 2026 Giovanni MARIANO
//
// SPDX-License-Identifier: MPL-2.0

#include "git_helpers.h"
#include "geom_load.h"
#include "visual_diff.h"
#include "util.h"
#include <alea.h>
#include <alea_types.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <float.h>

/* Compute inner bbox (skip graveyard cells) for viewport calculation */
static void compute_inner_bbox_cmd(const alea_system_t* sys, alea_bbox_t* out) {
    out->min_x = out->min_y = out->min_z =  DBL_MAX;
    out->max_x = out->max_y = out->max_z = -DBL_MAX;

    bool found = false;
    size_t nc = alea_cell_count(sys);
    for (size_t i = 0; i < nc; i++) {
        alea_cell_info_t info;
        if (alea_cell_get_info(sys, i, &info) < 0) continue;
        /* Skip graveyard: universe=0, material=0, fill=-1 */
        if (info.universe_id == 0 && info.material_id == 0 &&
            info.fill_universe == -1)
            continue;
        found = true;
        if (info.bbox.min_x < out->min_x) out->min_x = info.bbox.min_x;
        if (info.bbox.max_x > out->max_x) out->max_x = info.bbox.max_x;
        if (info.bbox.min_y < out->min_y) out->min_y = info.bbox.min_y;
        if (info.bbox.max_y > out->max_y) out->max_y = info.bbox.max_y;
        if (info.bbox.min_z < out->min_z) out->min_z = info.bbox.min_z;
        if (info.bbox.max_z > out->max_z) out->max_z = info.bbox.max_z;
    }

    if (!found) {
        for (size_t i = 0; i < nc; i++) {
            alea_cell_info_t info;
            if (alea_cell_get_info(sys, i, &info) < 0) continue;
            if (info.bbox.min_x < out->min_x) out->min_x = info.bbox.min_x;
            if (info.bbox.max_x > out->max_x) out->max_x = info.bbox.max_x;
            if (info.bbox.min_y < out->min_y) out->min_y = info.bbox.min_y;
            if (info.bbox.max_y > out->max_y) out->max_y = info.bbox.max_y;
            if (info.bbox.min_z < out->min_z) out->min_z = info.bbox.min_z;
            if (info.bbox.max_z > out->max_z) out->max_z = info.bbox.max_z;
        }
    }

    double clamp = 1000.0;
    if (out->min_x < -clamp) out->min_x = -clamp;
    if (out->max_x >  clamp) out->max_x =  clamp;
    if (out->min_y < -clamp) out->min_y = -clamp;
    if (out->max_y >  clamp) out->max_y =  clamp;
    if (out->min_z < -clamp) out->min_z = -clamp;
    if (out->max_z >  clamp) out->max_z =  clamp;
}

int cmd_diff_visual(int argc, char** argv) {
    const char* rev1 = NULL;
    const char* rev2 = NULL;
    const char* file = NULL;
    const char* prefix = "aleagit_diff";
    int width = 0;

    /* Axis/position options */
    bool z_set = false, y_set = false, x_set = false;
    double z_val = 0, y_val = 0, x_val = 0;
    ag_slice_axis_t forced_axis = AG_AXIS_Z;
    bool axis_forced = false;
    bool all_axes = false;
    bool no_contours = false;

    /* Parse arguments */
    bool after_dashdash = false;
    for (int i = 0; i < argc; i++) {
        if (after_dashdash) { file = argv[i]; break; }
        if (strcmp(argv[i], "--") == 0) { after_dashdash = true; continue; }
        if (strcmp(argv[i], "--visual") == 0 || strcmp(argv[i], "-v") == 0) continue;
        if (strcmp(argv[i], "--prefix") == 0 && i + 1 < argc) { prefix = argv[++i]; continue; }
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) { prefix = argv[++i]; continue; }
        if (strcmp(argv[i], "--z") == 0 && i + 1 < argc) {
            z_val = atof(argv[++i]); z_set = true; continue;
        }
        if (strcmp(argv[i], "--y") == 0 && i + 1 < argc) {
            y_val = atof(argv[++i]); y_set = true; continue;
        }
        if (strcmp(argv[i], "--x") == 0 && i + 1 < argc) {
            x_val = atof(argv[++i]); x_set = true; continue;
        }
        if (strcmp(argv[i], "--axis") == 0 && i + 1 < argc) {
            const char* ax = argv[++i];
            if (ax[0] == 'X' || ax[0] == 'x') { forced_axis = AG_AXIS_X; axis_forced = true; }
            else if (ax[0] == 'Y' || ax[0] == 'y') { forced_axis = AG_AXIS_Y; axis_forced = true; }
            else if (ax[0] == 'Z' || ax[0] == 'z') { forced_axis = AG_AXIS_Z; axis_forced = true; }
            else { ag_error("unknown axis '%s' (use X, Y, or Z)", ax); return 1; }
            continue;
        }
        if (strcmp(argv[i], "--all") == 0) { all_axes = true; continue; }
        if (strcmp(argv[i], "--no-contours") == 0) { no_contours = true; continue; }
        if (strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
            width = atoi(argv[++i]); continue;
        }
        if (argv[i][0] == '-') continue;
        if (!rev1) rev1 = argv[i];
        else if (!rev2) rev2 = argv[i];
    }

    /* --z implies axis Z, --y implies axis Y, --x implies axis X */
    if (z_set && !axis_forced) { forced_axis = AG_AXIS_Z; axis_forced = true; }
    if (y_set && !axis_forced) { forced_axis = AG_AXIS_Y; axis_forced = true; }
    if (x_set && !axis_forced) { forced_axis = AG_AXIS_X; axis_forced = true; }

    git_repository* repo = ag_repo_open();
    if (!repo) return 1;

    git_commit* c1 = NULL;
    git_commit* c2 = NULL;
    bool workdir_mode = false;

    if (!rev1 && !rev2) {
        c1 = ag_resolve_commit(repo, "HEAD");
        workdir_mode = true;
    } else if (rev1 && !rev2) {
        c1 = ag_resolve_commit(repo, rev1);
        workdir_mode = true;
    } else {
        c1 = ag_resolve_commit(repo, rev1);
        c2 = ag_resolve_commit(repo, rev2);
    }

    if (!c1) { git_repository_free(repo); return 1; }

    /* Find file to diff */
    if (!file) {
        ag_file_list_t* files = ag_find_geometry_files(repo, c1);
        if (files && files->count > 0)
            file = files->paths[0];
        if (!file) {
            ag_error("no geometry file specified or found");
            if (files) ag_file_list_free(files);
            git_commit_free(c1);
            if (c2) git_commit_free(c2);
            git_repository_free(repo);
            return 1;
        }
    }

    alea_system_t* old_sys = ag_load_geometry_commit(repo, c1, file);
    alea_system_t* new_sys = workdir_mode
        ? ag_load_geometry_workdir(repo, file)
        : ag_load_geometry_commit(repo, c2, file);

    if (!old_sys || !new_sys) {
        ag_error("failed to load geometry for visual diff");
        if (old_sys) alea_destroy(old_sys);
        if (new_sys) alea_destroy(new_sys);
        git_commit_free(c1);
        if (c2) git_commit_free(c2);
        git_repository_free(repo);
        return 1;
    }

    printf("Generating visual diff for %s...\n", file);
    int rc;

    if (all_axes) {
        /* --all: produce all 3 axes */
        rc = ag_visual_diff_all(old_sys, new_sys, prefix);
    } else if (axis_forced || z_set || y_set || x_set) {
        /* Explicit axis/position: build opts struct */
        alea_bbox_t bb1, bb2;
        compute_inner_bbox_cmd(old_sys, &bb1);
        compute_inner_bbox_cmd(new_sys, &bb2);

        ag_visual_opts_t opts;
        opts.axis = forced_axis;
        opts.draw_contours = !no_contours;

        /* Determine slice position */
        if (forced_axis == AG_AXIS_Z && z_set) opts.slice_pos = z_val;
        else if (forced_axis == AG_AXIS_Y && y_set) opts.slice_pos = y_val;
        else if (forced_axis == AG_AXIS_X && x_set) opts.slice_pos = x_val;
        else {
            /* Axis forced but no position: use midpoint of inner bbox */
            switch (forced_axis) {
                case AG_AXIS_X: opts.slice_pos = (fmin(bb1.min_x, bb2.min_x) + fmax(bb1.max_x, bb2.max_x)) * 0.5; break;
                case AG_AXIS_Y: opts.slice_pos = (fmin(bb1.min_y, bb2.min_y) + fmax(bb1.max_y, bb2.max_y)) * 0.5; break;
                case AG_AXIS_Z: opts.slice_pos = (fmin(bb1.min_z, bb2.min_z) + fmax(bb1.max_z, bb2.max_z)) * 0.5; break;
            }
        }

        /* Compute in-plane viewport from inner bbox */
        switch (forced_axis) {
            case AG_AXIS_Z: /* u=x, v=y */
                opts.u_min = fmin(bb1.min_x, bb2.min_x);
                opts.u_max = fmax(bb1.max_x, bb2.max_x);
                opts.v_min = fmin(bb1.min_y, bb2.min_y);
                opts.v_max = fmax(bb1.max_y, bb2.max_y);
                break;
            case AG_AXIS_Y: /* u=x, v=z */
                opts.u_min = fmin(bb1.min_x, bb2.min_x);
                opts.u_max = fmax(bb1.max_x, bb2.max_x);
                opts.v_min = fmin(bb1.min_z, bb2.min_z);
                opts.v_max = fmax(bb1.max_z, bb2.max_z);
                break;
            case AG_AXIS_X: /* u=y, v=z */
                opts.u_min = fmin(bb1.min_y, bb2.min_y);
                opts.u_max = fmax(bb1.max_y, bb2.max_y);
                opts.v_min = fmin(bb1.min_z, bb2.min_z);
                opts.v_max = fmax(bb1.max_z, bb2.max_z);
                break;
        }

        /* 10% padding */
        double du = (opts.u_max - opts.u_min) * 0.1;
        double dv = (opts.v_max - opts.v_min) * 0.1;
        opts.u_min -= du; opts.u_max += du;
        opts.v_min -= dv; opts.v_max += dv;

        opts.width = width > 0 ? width : 800;
        double aspect = (opts.v_max - opts.v_min) / (opts.u_max - opts.u_min);
        opts.height = (int)(opts.width * aspect);
        if (opts.height < 100) opts.height = 100;
        if (opts.height > 4000) opts.height = 4000;

        rc = ag_visual_diff(old_sys, new_sys, prefix, &opts);
    } else {
        /* Full auto mode: smart selection */
        rc = ag_visual_diff(old_sys, new_sys, prefix, NULL);
    }

    alea_destroy(old_sys);
    alea_destroy(new_sys);
    git_commit_free(c1);
    if (c2) git_commit_free(c2);
    git_repository_free(repo);
    return rc;
}
