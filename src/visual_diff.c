// SPDX-FileCopyrightText: 2026 Giovanni MARIANO
//
// SPDX-License-Identifier: MPL-2.0

#define _USE_MATH_DEFINES
#include "visual_diff.h"
#include "bmp_writer.h"
#include "util.h"
#include <alea.h>
#include <alea_types.h>
#include <alea_slice.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <float.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ------------------------------------------------------------------ */
/*  Helpers                                                           */
/* ------------------------------------------------------------------ */

static const char* axis_name(ag_slice_axis_t a) {
    switch (a) {
        case AG_AXIS_X: return "X";
        case AG_AXIS_Y: return "Y";
        case AG_AXIS_Z: return "Z";
    }
    return "?";
}

static const char* axis_coord(ag_slice_axis_t a) {
    switch (a) {
        case AG_AXIS_X: return "x";
        case AG_AXIS_Y: return "y";
        case AG_AXIS_Z: return "z";
    }
    return "?";
}

/* ------------------------------------------------------------------ */
/*  Bounding box utilities                                            */
/* ------------------------------------------------------------------ */

static bool is_graveyard_cell(const alea_cell_info_t* info) {
    return info->universe_id == 0 &&
           info->material_id == 0 &&
           info->fill_universe == -1;
}

/* Inner bbox: skip graveyard (universe=0, material=0, fill=-1) cells */
static void compute_inner_bbox(const alea_system_t* sys, alea_bbox_t* out) {
    out->min_x = out->min_y = out->min_z =  DBL_MAX;
    out->max_x = out->max_y = out->max_z = -DBL_MAX;

    bool found = false;
    size_t nc = alea_cell_count(sys);
    for (size_t i = 0; i < nc; i++) {
        alea_cell_info_t info;
        if (alea_cell_get_info(sys, i, &info) < 0) continue;
        if (is_graveyard_cell(&info)) continue;

        found = true;
        if (info.bbox.min_x < out->min_x) out->min_x = info.bbox.min_x;
        if (info.bbox.max_x > out->max_x) out->max_x = info.bbox.max_x;
        if (info.bbox.min_y < out->min_y) out->min_y = info.bbox.min_y;
        if (info.bbox.max_y > out->max_y) out->max_y = info.bbox.max_y;
        if (info.bbox.min_z < out->min_z) out->min_z = info.bbox.min_z;
        if (info.bbox.max_z > out->max_z) out->max_z = info.bbox.max_z;
    }

    if (!found) {
        /* Fall back to full bbox */
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

    /* Clamp infinite extents */
    double clamp = 1000.0;
    if (out->min_x < -clamp) out->min_x = -clamp;
    if (out->max_x >  clamp) out->max_x =  clamp;
    if (out->min_y < -clamp) out->min_y = -clamp;
    if (out->max_y >  clamp) out->max_y =  clamp;
    if (out->min_z < -clamp) out->min_z = -clamp;
    if (out->max_z >  clamp) out->max_z =  clamp;
}

/* Merge two bboxes (union) */
static void bbox_union(const alea_bbox_t* a, const alea_bbox_t* b, alea_bbox_t* out) {
    out->min_x = fmin(a->min_x, b->min_x);
    out->max_x = fmax(a->max_x, b->max_x);
    out->min_y = fmin(a->min_y, b->min_y);
    out->max_y = fmax(a->max_y, b->max_y);
    out->min_z = fmin(a->min_z, b->min_z);
    out->max_z = fmax(a->max_z, b->max_z);
}

/* Get axis range from bbox */
static void bbox_axis_range(const alea_bbox_t* bb, ag_slice_axis_t axis,
                            double* lo, double* hi) {
    switch (axis) {
        case AG_AXIS_X: *lo = bb->min_x; *hi = bb->max_x; break;
        case AG_AXIS_Y: *lo = bb->min_y; *hi = bb->max_y; break;
        case AG_AXIS_Z: *lo = bb->min_z; *hi = bb->max_z; break;
    }
}

/* Get in-plane u,v ranges from bbox for a given axis */
static void bbox_uv_range(const alea_bbox_t* bb, ag_slice_axis_t axis,
                           double* u_min, double* u_max,
                           double* v_min, double* v_max) {
    switch (axis) {
        case AG_AXIS_Z: /* u=x, v=y */
            *u_min = bb->min_x; *u_max = bb->max_x;
            *v_min = bb->min_y; *v_max = bb->max_y;
            break;
        case AG_AXIS_Y: /* u=x, v=z */
            *u_min = bb->min_x; *u_max = bb->max_x;
            *v_min = bb->min_z; *v_max = bb->max_z;
            break;
        case AG_AXIS_X: /* u=y, v=z */
            *u_min = bb->min_y; *u_max = bb->max_y;
            *v_min = bb->min_z; *v_max = bb->max_z;
            break;
    }
}

/* ------------------------------------------------------------------ */
/*  Color mapping                                                     */
/* ------------------------------------------------------------------ */

/* Deterministic color from material/cell id */
static void id_to_color(int id, uint8_t* r, uint8_t* g, uint8_t* b) {
    if (id <= 0) { *r = 40; *g = 40; *b = 40; return; }
    unsigned h = (unsigned)id * 2654435761u;
    *r = 80 + (h & 0x7F);
    *g = 80 + ((h >> 8) & 0x7F);
    *b = 80 + ((h >> 16) & 0x7F);
}

/* Diff overlay colors */
static const uint8_t COL_DIFF_ADDED[3]     = {50, 200, 50};   /* green */
static const uint8_t COL_DIFF_REMOVED[3]   = {200, 50, 50};   /* red */
static const uint8_t COL_DIFF_MATERIAL[3]  = {200, 200, 50};  /* yellow */
static const uint8_t COL_DIFF_STRUCTURE[3] = {50, 200, 200};   /* cyan */
static const uint8_t COL_CONTOUR[3]        = {20, 20, 20};

/* ------------------------------------------------------------------ */
/*  Multi-axis grid rendering                                         */
/* ------------------------------------------------------------------ */

static void render_grid_axis(const alea_system_t* sys,
                             ag_slice_axis_t axis,
                             double u_min, double u_max, int nu,
                             double v_min, double v_max, int nv,
                             double slice_pos,
                             uint8_t* pixels,
                             int* cells_out, int* mats_out) {
    int n = nu * nv;
    int* cells = malloc(n * sizeof(int));
    int* mats  = malloc(n * sizeof(int));
    if (!cells || !mats) { free(cells); free(mats); return; }

    alea_slice_view_t view;
    alea_slice_view_axis(&view, (int)axis, slice_pos,
                         u_min, u_max, v_min, v_max);
    alea_find_cells_grid(sys, &view, nu, nv, -1, cells, mats, NULL);

    for (int i = 0; i < n; i++) {
        uint8_t r, g, b;
        id_to_color(mats[i], &r, &g, &b);
        pixels[i * 3 + 0] = r;
        pixels[i * 3 + 1] = g;
        pixels[i * 3 + 2] = b;
    }

    if (cells_out) memcpy(cells_out, cells, n * sizeof(int));
    if (mats_out)  memcpy(mats_out,  mats,  n * sizeof(int));

    free(cells);
    free(mats);
}

/* ------------------------------------------------------------------ */
/*  Smart slice position selection                                    */
/* ------------------------------------------------------------------ */

#define SAMPLE_RES 32
#define N_SAMPLES  20

typedef struct {
    double pos;
    int diff_count;
    int geom_count;
} slice_score_t;

static void find_best_slice_for_axis(const alea_system_t* old_sys,
                                     const alea_system_t* new_sys,
                                     ag_slice_axis_t axis,
                                     const alea_bbox_t* inner_bb,
                                     double* out_pos,
                                     int* out_diff,
                                     int* out_geom) {
    double ax_lo, ax_hi;
    bbox_axis_range(inner_bb, axis, &ax_lo, &ax_hi);

    double u_min, u_max, v_min, v_max;
    bbox_uv_range(inner_bb, axis, &u_min, &u_max, &v_min, &v_max);

    int n = SAMPLE_RES * SAMPLE_RES;
    int* cells_old = malloc(n * sizeof(int));
    int* cells_new = malloc(n * sizeof(int));
    int* mats_old  = malloc(n * sizeof(int));
    int* mats_new  = malloc(n * sizeof(int));
    if (!cells_old || !cells_new || !mats_old || !mats_new) {
        free(cells_old); free(cells_new);
        free(mats_old);  free(mats_new);
        *out_pos = (ax_lo + ax_hi) * 0.5;
        *out_diff = 0;
        *out_geom = 0;
        return;
    }

    slice_score_t best = { (ax_lo + ax_hi) * 0.5, -1, 0 };

    for (int s = 0; s < N_SAMPLES; s++) {
        double t = (N_SAMPLES == 1) ? 0.5 : (double)s / (N_SAMPLES - 1);
        double pos = ax_lo + t * (ax_hi - ax_lo);

        /* Render coarse grids */
        alea_slice_view_t view;
        alea_slice_view_axis(&view, (int)axis, pos,
                             u_min, u_max, v_min, v_max);
        alea_find_cells_grid(old_sys, &view, SAMPLE_RES, SAMPLE_RES, -1,
                             cells_old, mats_old, NULL);
        alea_find_cells_grid(new_sys, &view, SAMPLE_RES, SAMPLE_RES, -1,
                             cells_new, mats_new, NULL);

        int diff_count = 0, geom_count = 0;
        for (int i = 0; i < n; i++) {
            bool has_geom = (cells_old[i] > 0) || (cells_new[i] > 0);
            if (has_geom) geom_count++;
            if (cells_old[i] != cells_new[i] || mats_old[i] != mats_new[i])
                diff_count++;
        }

        if (diff_count > best.diff_count ||
            (diff_count == best.diff_count && geom_count > best.geom_count)) {
            best.pos = pos;
            best.diff_count = diff_count;
            best.geom_count = geom_count;
        }
    }

    free(cells_old); free(cells_new);
    free(mats_old);  free(mats_new);

    *out_pos  = best.pos;
    *out_diff = best.diff_count;
    *out_geom = best.geom_count;
}

/* ------------------------------------------------------------------ */
/*  Contour rasterization                                             */
/* ------------------------------------------------------------------ */

static void stamp_pixel(uint8_t* pixels, int w, int h,
                        double u_min, double u_max,
                        double v_min, double v_max,
                        double u, double v,
                        const uint8_t color[3]) {
    int ix = (int)((u - u_min) / (u_max - u_min) * (w - 1) + 0.5);
    int iy = (int)((v - v_min) / (v_max - v_min) * (h - 1) + 0.5);
    if (ix < 0 || ix >= w || iy < 0 || iy >= h) return;
    int idx = (iy * w + ix) * 3;
    pixels[idx + 0] = color[0];
    pixels[idx + 1] = color[1];
    pixels[idx + 2] = color[2];
}

/* Pixel size in coordinate space */
static double pixel_size(double range_min, double range_max, int n) {
    return (range_max - range_min) / n;
}

static void rasterize_line_segment(uint8_t* pixels, int w, int h,
                                   double u_min, double u_max,
                                   double v_min, double v_max,
                                   double u0, double v0,
                                   double u1, double v1,
                                   const uint8_t color[3]) {
    double pu = pixel_size(u_min, u_max, w);
    double pv = pixel_size(v_min, v_max, h);
    double step = fmin(pu, pv) * 0.5;

    double du = u1 - u0, dv = v1 - v0;
    double len = sqrt(du * du + dv * dv);
    if (len < 1e-12) {
        stamp_pixel(pixels, w, h, u_min, u_max, v_min, v_max, u0, v0, color);
        return;
    }

    int nsteps = (int)(len / step) + 1;
    for (int i = 0; i <= nsteps; i++) {
        double t = (double)i / nsteps;
        double u = u0 + t * du;
        double v = v0 + t * dv;
        stamp_pixel(pixels, w, h, u_min, u_max, v_min, v_max, u, v, color);
    }
}

/* Clip an infinite line to the viewport, return false if entirely outside */
static bool clip_line(double u_min, double u_max, double v_min, double v_max,
                      double pu, double pv, double du, double dv,
                      double* out_u0, double* out_v0,
                      double* out_u1, double* out_v1) {
    /* Find t range where line is inside the viewport */
    double t_lo = -1e12, t_hi = 1e12;

    if (fabs(du) > 1e-15) {
        double t1 = (u_min - pu) / du;
        double t2 = (u_max - pu) / du;
        if (t1 > t2) { double tmp = t1; t1 = t2; t2 = tmp; }
        if (t1 > t_lo) t_lo = t1;
        if (t2 < t_hi) t_hi = t2;
    } else {
        if (pu < u_min || pu > u_max) return false;
    }

    if (fabs(dv) > 1e-15) {
        double t1 = (v_min - pv) / dv;
        double t2 = (v_max - pv) / dv;
        if (t1 > t2) { double tmp = t1; t1 = t2; t2 = tmp; }
        if (t1 > t_lo) t_lo = t1;
        if (t2 < t_hi) t_hi = t2;
    } else {
        if (pv < v_min || pv > v_max) return false;
    }

    if (t_lo >= t_hi) return false;

    *out_u0 = pu + t_lo * du;
    *out_v0 = pv + t_lo * dv;
    *out_u1 = pu + t_hi * du;
    *out_v1 = pv + t_hi * dv;
    return true;
}

static void stamp_contours(uint8_t* pixels, int w, int h,
                           double u_min, double u_max,
                           double v_min, double v_max,
                           const alea_system_t* sys,
                           ag_slice_axis_t axis,
                           double slice_pos) {
    alea_slice_view_t view;
    alea_slice_view_axis(&view, (int)axis, slice_pos,
                         u_min, u_max, v_min, v_max);
    alea_slice_curves_t* curves = alea_get_slice_curves(sys, &view);
    if (!curves) return;

    double pu = pixel_size(u_min, u_max, w);
    double pv = pixel_size(v_min, v_max, h);
    double step = fmin(pu, pv) * 0.5;

    size_t nc = alea_slice_curves_count(curves);
    for (size_t ci = 0; ci < nc; ci++) {
        alea_curve_t c;
        if (alea_slice_curves_get(curves, ci, &c) < 0) continue;

        switch (c.type) {
            case ALEA_CURVE_LINE: {
                double u0, v0, u1, v1;
                if (clip_line(u_min, u_max, v_min, v_max,
                              c.data.line.point[0], c.data.line.point[1],
                              c.data.line.direction[0], c.data.line.direction[1],
                              &u0, &v0, &u1, &v1)) {
                    rasterize_line_segment(pixels, w, h, u_min, u_max,
                                           v_min, v_max, u0, v0, u1, v1,
                                           COL_CONTOUR);
                }
                break;
            }
            case ALEA_CURVE_LINE_SEGMENT: {
                double du = c.data.line.direction[0];
                double dv = c.data.line.direction[1];
                double u0 = c.data.line.point[0] + c.t_min * du;
                double v0 = c.data.line.point[1] + c.t_min * dv;
                double u1 = c.data.line.point[0] + c.t_max * du;
                double v1 = c.data.line.point[1] + c.t_max * dv;
                rasterize_line_segment(pixels, w, h, u_min, u_max,
                                       v_min, v_max, u0, v0, u1, v1,
                                       COL_CONTOUR);
                break;
            }
            case ALEA_CURVE_CIRCLE: {
                double cx = c.data.circle.center[0];
                double cy = c.data.circle.center[1];
                double r = c.data.circle.radius;
                double circ = 2.0 * M_PI * r;
                int nsteps = (int)(circ / step) + 1;
                if (nsteps < 32) nsteps = 32;
                for (int i = 0; i <= nsteps; i++) {
                    double theta = 2.0 * M_PI * i / nsteps;
                    double u = cx + r * cos(theta);
                    double v = cy + r * sin(theta);
                    stamp_pixel(pixels, w, h, u_min, u_max, v_min, v_max,
                                u, v, COL_CONTOUR);
                }
                break;
            }
            case ALEA_CURVE_ARC: {
                double cx = c.data.circle.center[0];
                double cy = c.data.circle.center[1];
                double r = c.data.circle.radius;
                double t0 = c.t_min, t1 = c.t_max;
                double arc_len = r * fabs(t1 - t0);
                int nsteps = (int)(arc_len / step) + 1;
                if (nsteps < 16) nsteps = 16;
                for (int i = 0; i <= nsteps; i++) {
                    double theta = t0 + (t1 - t0) * i / nsteps;
                    double u = cx + r * cos(theta);
                    double v = cy + r * sin(theta);
                    stamp_pixel(pixels, w, h, u_min, u_max, v_min, v_max,
                                u, v, COL_CONTOUR);
                }
                break;
            }
            case ALEA_CURVE_ELLIPSE: {
                double cx = c.data.ellipse.center[0];
                double cy = c.data.ellipse.center[1];
                double a  = c.data.ellipse.semi_a;
                double b  = c.data.ellipse.semi_b;
                double alpha = c.data.ellipse.angle;
                double ca = cos(alpha), sa = sin(alpha);
                double approx_circ = M_PI * (3.0*(a+b) - sqrt((3*a+b)*(a+3*b)));
                int nsteps = (int)(approx_circ / step) + 1;
                if (nsteps < 64) nsteps = 64;
                for (int i = 0; i <= nsteps; i++) {
                    double theta = 2.0 * M_PI * i / nsteps;
                    double lu = a * cos(theta);
                    double lv = b * sin(theta);
                    double u = cx + lu * ca - lv * sa;
                    double v = cy + lu * sa + lv * ca;
                    stamp_pixel(pixels, w, h, u_min, u_max, v_min, v_max,
                                u, v, COL_CONTOUR);
                }
                break;
            }
            case ALEA_CURVE_ELLIPSE_ARC: {
                double cx = c.data.ellipse.center[0];
                double cy = c.data.ellipse.center[1];
                double a  = c.data.ellipse.semi_a;
                double b  = c.data.ellipse.semi_b;
                double alpha = c.data.ellipse.angle;
                double ca = cos(alpha), sa = sin(alpha);
                double t0 = c.t_min, t1 = c.t_max;
                double avg_r = (a + b) * 0.5;
                double arc_len = avg_r * fabs(t1 - t0);
                int nsteps = (int)(arc_len / step) + 1;
                if (nsteps < 16) nsteps = 16;
                for (int i = 0; i <= nsteps; i++) {
                    double theta = t0 + (t1 - t0) * i / nsteps;
                    double lu = a * cos(theta);
                    double lv = b * sin(theta);
                    double u = cx + lu * ca - lv * sa;
                    double v = cy + lu * sa + lv * ca;
                    stamp_pixel(pixels, w, h, u_min, u_max, v_min, v_max,
                                u, v, COL_CONTOUR);
                }
                break;
            }
            case ALEA_CURVE_POLYGON: {
                int nv = c.data.polygon.count;
                for (int i = 0; i < nv; i++) {
                    int j = (i + 1) % nv;
                    if (!c.data.polygon.closed && j == 0 && i == nv - 1) break;
                    rasterize_line_segment(pixels, w, h, u_min, u_max,
                                           v_min, v_max,
                                           c.data.polygon.vertices[i][0],
                                           c.data.polygon.vertices[i][1],
                                           c.data.polygon.vertices[j][0],
                                           c.data.polygon.vertices[j][1],
                                           COL_CONTOUR);
                }
                break;
            }
            case ALEA_CURVE_PARALLEL_LINES: {
                double du = c.data.parallel_lines.direction[0];
                double dv = c.data.parallel_lines.direction[1];
                /* Line through point1 */
                double u0a, v0a, u1a, v1a;
                if (clip_line(u_min, u_max, v_min, v_max,
                              c.data.parallel_lines.point1[0],
                              c.data.parallel_lines.point1[1],
                              du, dv, &u0a, &v0a, &u1a, &v1a)) {
                    rasterize_line_segment(pixels, w, h, u_min, u_max,
                                           v_min, v_max, u0a, v0a, u1a, v1a,
                                           COL_CONTOUR);
                }
                /* Line through point2 */
                double u0b, v0b, u1b, v1b;
                if (clip_line(u_min, u_max, v_min, v_max,
                              c.data.parallel_lines.point2[0],
                              c.data.parallel_lines.point2[1],
                              du, dv, &u0b, &v0b, &u1b, &v1b)) {
                    rasterize_line_segment(pixels, w, h, u_min, u_max,
                                           v_min, v_max, u0b, v0b, u1b, v1b,
                                           COL_CONTOUR);
                }
                break;
            }
            default:
                break;
        }
    }

    alea_slice_curves_free(curves);
}

/* ------------------------------------------------------------------ */
/*  Diff overlay computation                                          */
/* ------------------------------------------------------------------ */

static void compute_diff_overlay(uint8_t* pix_diff,
                                 const int* cells_old, const int* cells_new,
                                 const int* mats_old, const int* mats_new,
                                 size_t npix) {
    for (size_t i = 0; i < npix; i++) {
        int co = cells_old[i], cn = cells_new[i];
        int mo = mats_old[i],  mn = mats_new[i];

        if (co == cn && mo == mn) {
            uint8_t r, g, b;
            id_to_color(mo, &r, &g, &b);
            pix_diff[i*3+0] = r / 3;
            pix_diff[i*3+1] = g / 3;
            pix_diff[i*3+2] = b / 3;
        } else if (co <= 0 && cn > 0) {
            memcpy(&pix_diff[i*3], COL_DIFF_ADDED, 3);
        } else if (co > 0 && cn <= 0) {
            memcpy(&pix_diff[i*3], COL_DIFF_REMOVED, 3);
        } else if (co != cn && mo != mn) {
            memcpy(&pix_diff[i*3], COL_DIFF_MATERIAL, 3);
        } else if (co != cn) {
            memcpy(&pix_diff[i*3], COL_DIFF_STRUCTURE, 3);
        } else {
            memcpy(&pix_diff[i*3], COL_DIFF_MATERIAL, 3);
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Render one axis                                                   */
/* ------------------------------------------------------------------ */

static int render_one_axis(const alea_system_t* old_sys,
                           const alea_system_t* new_sys,
                           const char* prefix,
                           ag_slice_axis_t axis,
                           double slice_pos,
                           double u_min, double u_max,
                           double v_min, double v_max,
                           int w, int h,
                           bool draw_contours) {
    size_t npix = (size_t)w * h;
    uint8_t* pix_old  = calloc(npix * 3, 1);
    uint8_t* pix_new  = calloc(npix * 3, 1);
    uint8_t* pix_diff = calloc(npix * 3, 1);
    int* cells_old = malloc(npix * sizeof(int));
    int* cells_new = malloc(npix * sizeof(int));
    int* mats_old  = malloc(npix * sizeof(int));
    int* mats_new  = malloc(npix * sizeof(int));

    if (!pix_old || !pix_new || !pix_diff ||
        !cells_old || !cells_new || !mats_old || !mats_new) {
        ag_error("out of memory for visual diff");
        free(pix_old); free(pix_new); free(pix_diff);
        free(cells_old); free(cells_new);
        free(mats_old); free(mats_new);
        return -1;
    }

    render_grid_axis(old_sys, axis, u_min, u_max, w, v_min, v_max, h,
                     slice_pos, pix_old, cells_old, mats_old);
    render_grid_axis(new_sys, axis, u_min, u_max, w, v_min, v_max, h,
                     slice_pos, pix_new, cells_new, mats_new);

    compute_diff_overlay(pix_diff, cells_old, cells_new,
                         mats_old, mats_new, npix);

    if (draw_contours) {
        stamp_contours(pix_old, w, h, u_min, u_max, v_min, v_max,
                       old_sys, axis, slice_pos);
        stamp_contours(pix_new, w, h, u_min, u_max, v_min, v_max,
                       new_sys, axis, slice_pos);
        /* Diff: contours from both systems */
        stamp_contours(pix_diff, w, h, u_min, u_max, v_min, v_max,
                       old_sys, axis, slice_pos);
        stamp_contours(pix_diff, w, h, u_min, u_max, v_min, v_max,
                       new_sys, axis, slice_pos);
    }

    const char* ax = axis_name(axis);
    char path[1024];

    snprintf(path, sizeof(path), "%s_%s_before.bmp", prefix, ax);
    ag_write_bmp(path, pix_old, w, h);
    printf("  wrote %s\n", path);

    snprintf(path, sizeof(path), "%s_%s_after.bmp", prefix, ax);
    ag_write_bmp(path, pix_new, w, h);
    printf("  wrote %s\n", path);

    snprintf(path, sizeof(path), "%s_%s_diff.bmp", prefix, ax);
    ag_write_bmp(path, pix_diff, w, h);
    printf("  wrote %s\n", path);

    free(pix_old); free(pix_new); free(pix_diff);
    free(cells_old); free(cells_new);
    free(mats_old); free(mats_new);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Auto-select best axis+position                                    */
/* ------------------------------------------------------------------ */

static void auto_select(const alea_system_t* old_sys,
                        const alea_system_t* new_sys,
                        const alea_bbox_t* inner_bb,
                        ag_slice_axis_t* out_axis,
                        double* out_pos,
                        int* out_diff,
                        int* out_geom) {
    int best_diff = -1, best_geom = 0;
    ag_slice_axis_t best_axis = AG_AXIS_Z;
    double best_pos = 0;

    for (int a = 0; a < 3; a++) {
        ag_slice_axis_t axis = (ag_slice_axis_t)a;
        double pos;
        int diff, geom;
        find_best_slice_for_axis(old_sys, new_sys, axis, inner_bb,
                                 &pos, &diff, &geom);
        if (diff > best_diff || (diff == best_diff && geom > best_geom)) {
            best_axis = axis;
            best_pos  = pos;
            best_diff = diff;
            best_geom = geom;
        }
    }

    *out_axis = best_axis;
    *out_pos  = best_pos;
    *out_diff = best_diff;
    *out_geom = best_geom;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

int ag_visual_diff(const alea_system_t* old_sys,
                   const alea_system_t* new_sys,
                   const char* prefix,
                   const ag_visual_opts_t* opts) {

    /* Build indices */
    alea_build_universe_index((alea_system_t*)old_sys);
    alea_build_spatial_index((alea_system_t*)old_sys);
    alea_build_universe_index((alea_system_t*)new_sys);
    alea_build_spatial_index((alea_system_t*)new_sys);

    if (opts) {
        return render_one_axis(old_sys, new_sys, prefix,
                               opts->axis, opts->slice_pos,
                               opts->u_min, opts->u_max,
                               opts->v_min, opts->v_max,
                               opts->width, opts->height,
                               opts->draw_contours);
    }

    /* Auto mode: compute inner bbox, find best axis+position */
    alea_bbox_t bb_old, bb_new, inner;
    compute_inner_bbox(old_sys, &bb_old);
    compute_inner_bbox(new_sys, &bb_new);
    bbox_union(&bb_old, &bb_new, &inner);

    ag_slice_axis_t axis;
    double pos;
    int diff_count, geom_count;
    auto_select(old_sys, new_sys, &inner, &axis, &pos, &diff_count, &geom_count);

    if (diff_count <= 0)
        printf("No visual differences detected\n");

    printf("Auto-selected: %s-slice at %s = %.4g (%d diff pixels / %d geometry pixels)\n",
           axis_name(axis), axis_coord(axis), pos, diff_count, geom_count);

    /* Compute viewport */
    double u_min, u_max, v_min, v_max;
    bbox_uv_range(&inner, axis, &u_min, &u_max, &v_min, &v_max);

    /* 10% padding */
    double du = (u_max - u_min) * 0.1;
    double dv = (v_max - v_min) * 0.1;
    u_min -= du; u_max += du;
    v_min -= dv; v_max += dv;

    int w = 800;
    double aspect = (v_max - v_min) / (u_max - u_min);
    int h = (int)(w * aspect);
    if (h < 100) h = 100;
    if (h > 4000) h = 4000;

    return render_one_axis(old_sys, new_sys, prefix, axis, pos,
                           u_min, u_max, v_min, v_max, w, h, true);
}

int ag_visual_diff_all(const alea_system_t* old_sys,
                       const alea_system_t* new_sys,
                       const char* prefix) {

    /* Build indices */
    alea_build_universe_index((alea_system_t*)old_sys);
    alea_build_spatial_index((alea_system_t*)old_sys);
    alea_build_universe_index((alea_system_t*)new_sys);
    alea_build_spatial_index((alea_system_t*)new_sys);

    alea_bbox_t bb_old, bb_new, inner;
    compute_inner_bbox(old_sys, &bb_old);
    compute_inner_bbox(new_sys, &bb_new);
    bbox_union(&bb_old, &bb_new, &inner);

    int rc = 0;
    for (int a = 0; a < 3; a++) {
        ag_slice_axis_t axis = (ag_slice_axis_t)a;

        double pos;
        int diff_count, geom_count;
        find_best_slice_for_axis(old_sys, new_sys, axis, &inner,
                                 &pos, &diff_count, &geom_count);

        printf("Auto-selected: %s-slice at %s = %.4g (%d diff pixels / %d geometry pixels)\n",
               axis_name(axis), axis_coord(axis), pos, diff_count, geom_count);

        double u_min, u_max, v_min, v_max;
        bbox_uv_range(&inner, axis, &u_min, &u_max, &v_min, &v_max);

        double du = (u_max - u_min) * 0.1;
        double dv = (v_max - v_min) * 0.1;
        u_min -= du; u_max += du;
        v_min -= dv; v_max += dv;

        int w = 800;
        double aspect = (v_max - v_min) / (u_max - u_min);
        int h = (int)(w * aspect);
        if (h < 100) h = 100;
        if (h > 4000) h = 4000;

        int r = render_one_axis(old_sys, new_sys, prefix, axis, pos,
                                u_min, u_max, v_min, v_max, w, h, true);
        if (r != 0) rc = r;
    }

    return rc;
}
