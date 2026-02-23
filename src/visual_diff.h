#ifndef ALEAGIT_VISUAL_DIFF_H
#define ALEAGIT_VISUAL_DIFF_H

#include <stdbool.h>

typedef struct alea_system alea_system_t;

typedef enum {
    AG_AXIS_X = 0,
    AG_AXIS_Y = 1,
    AG_AXIS_Z = 2
} ag_slice_axis_t;

typedef struct {
    ag_slice_axis_t axis;
    double slice_pos;          /* position along slicing axis */
    double u_min, u_max;       /* in-plane axis 1: Z->x, Y->x, X->y */
    double v_min, v_max;       /* in-plane axis 2: Z->y, Y->z, X->z */
    int    width, height;
    bool   draw_contours;
} ag_visual_opts_t;

/* Generate visual diff BMP images for a single axis.
   Creates: <prefix>_<axis>_before.bmp, <prefix>_<axis>_after.bmp, <prefix>_<axis>_diff.bmp
   If opts is NULL, auto-selects the best axis and slice position.
   Returns 0 on success. */
int ag_visual_diff(const alea_system_t* old_sys,
                   const alea_system_t* new_sys,
                   const char* prefix,
                   const ag_visual_opts_t* opts);

/* Generate visual diff BMP images for all 3 orthogonal axes.
   Creates 9 BMPs: 3 per axis (before, after, diff).
   Returns 0 on success. */
int ag_visual_diff_all(const alea_system_t* old_sys,
                       const alea_system_t* new_sys,
                       const char* prefix);

#endif /* ALEAGIT_VISUAL_DIFF_H */
