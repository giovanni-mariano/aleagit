#include "geom_fingerprint.h"
#include "aleagit.h"
#include <alea.h>
#include <alea_types.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* FNV-1a 64-bit */
#define FNV_OFFSET 14695981039346656037ULL
#define FNV_PRIME  1099511628211ULL

static uint64_t fnv_init(void) { return FNV_OFFSET; }

static uint64_t fnv_feed(uint64_t h, const void* data, size_t len) {
    const uint8_t* p = data;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= FNV_PRIME;
    }
    return h;
}

static uint64_t fnv_int(uint64_t h, int64_t v) {
    return fnv_feed(h, &v, sizeof(v));
}

static uint64_t fnv_double(uint64_t h, double v) {
    /* Discretize to ~1e-6 precision to tolerate floating-point noise */
    int64_t iv = (int64_t)round(v * 1e6);
    return fnv_int(h, iv);
}

/* Recursively hash a CSG tree */
static uint64_t hash_tree(const alea_system_t* sys, uint32_t node) {
    if (node == UINT32_MAX) return fnv_init();

    alea_operation_t op = alea_node_operation(sys, node);
    uint64_t h = fnv_init();

    if (op == ALEA_OP_PRIMITIVE) {
        int sid = alea_node_surface_id(sys, node);
        int sense = alea_node_sense(sys, node);
        h = fnv_int(h, sid);
        h = fnv_int(h, sense);
    } else {
        h = fnv_int(h, (int64_t)op);
        uint32_t left  = alea_node_left(sys, node);
        uint32_t right = alea_node_right(sys, node);
        uint64_t lh = hash_tree(sys, left);
        uint64_t rh = hash_tree(sys, right);
        h = fnv_feed(h, &lh, sizeof(lh));
        h = fnv_feed(h, &rh, sizeof(rh));
    }
    return h;
}

/* Hash lattice fill array */
static uint64_t hash_lattice(const alea_cell_info_t* info) {
    uint64_t h = fnv_init();
    h = fnv_int(h, info->lat_type);
    if (info->lat_type == 0) return h;

    for (int i = 0; i < 6; i++)
        h = fnv_int(h, info->lat_fill_dims[i]);
    for (int i = 0; i < 3; i++) {
        h = fnv_double(h, info->lat_pitch[i]);
        h = fnv_double(h, info->lat_lower_left[i]);
    }
    if (info->lat_fill && info->lat_fill_count > 0) {
        for (size_t i = 0; i < info->lat_fill_count; i++)
            h = fnv_int(h, info->lat_fill[i]);
    }
    return h;
}

static int cmp_cell_fp(const void* a, const void* b) {
    return ((const ag_cell_fp_t*)a)->cell_id - ((const ag_cell_fp_t*)b)->cell_id;
}

static int cmp_surface_fp(const void* a, const void* b) {
    return ((const ag_surface_fp_t*)a)->surface_id - ((const ag_surface_fp_t*)b)->surface_id;
}

ag_fingerprint_set_t* ag_fingerprint(const alea_system_t* sys) {
    ag_fingerprint_set_t* fp = calloc(1, sizeof(*fp));
    if (!fp) return NULL;

    /* Cells */
    size_t nc = alea_cell_count(sys);
    fp->cells = calloc(nc, sizeof(ag_cell_fp_t));
    fp->cell_count = nc;

    for (size_t i = 0; i < nc; i++) {
        alea_cell_info_t info;
        memset(&info, 0, sizeof(info));
        if (alea_cell_get_info(sys, i, &info) < 0) continue;

        fp->cells[i].cell_id       = info.cell_id;
        fp->cells[i].material_id   = info.material_id;
        fp->cells[i].density        = info.density;
        fp->cells[i].universe_id   = info.universe_id;
        fp->cells[i].fill_universe = info.fill_universe;
        fp->cells[i].lat_type      = info.lat_type;
        fp->cells[i].tree_hash     = hash_tree(sys, info.root);
        fp->cells[i].lattice_hash  = hash_lattice(&info);
    }

    qsort(fp->cells, fp->cell_count, sizeof(ag_cell_fp_t), cmp_cell_fp);

    /* Surfaces */
    size_t ns = alea_surface_count(sys);
    fp->surfaces = calloc(ns, sizeof(ag_surface_fp_t));
    fp->surface_count = ns;

    for (size_t i = 0; i < ns; i++) {
        int surface_id = 0;
        alea_primitive_type_t ptype = 0;
        alea_boundary_type_t btype = 0;
        alea_node_id_t pos_node = 0, neg_node = 0;

        alea_surface_get(sys, i, &surface_id, &ptype, &pos_node, &neg_node, &btype);

        fp->surfaces[i].surface_id    = surface_id;
        fp->surfaces[i].primitive_type = (int)ptype;
        fp->surfaces[i].boundary_type  = (int)btype;

        /* Hash the primitive data by treating it as an array of doubles */
        alea_primitive_data_t pdata;
        memset(&pdata, 0, sizeof(pdata));
        uint64_t h = fnv_init();
        h = fnv_int(h, (int64_t)ptype);
        if (pos_node != ALEA_NODE_ID_INVALID &&
            alea_node_primitive_data(sys, pos_node, &pdata) == 0) {
            /* Hash as doubles to avoid padding issues in union.
               All primitive data structs are composed of doubles. */
            size_t ndoubles = sizeof(pdata) / sizeof(double);
            const double* dp = (const double*)&pdata;
            for (size_t d = 0; d < ndoubles; d++)
                h = fnv_double(h, dp[d]);
        }
        fp->surfaces[i].data_hash = h;
    }

    qsort(fp->surfaces, fp->surface_count, sizeof(ag_surface_fp_t), cmp_surface_fp);

    return fp;
}

void ag_fingerprint_set_free(ag_fingerprint_set_t* fp) {
    if (!fp) return;
    free(fp->cells);
    free(fp->surfaces);
    free(fp);
}

int ag_cell_fp_compare(const ag_cell_fp_t* a, const ag_cell_fp_t* b) {
    if (a->material_id != b->material_id) return 1;
    if (a->universe_id != b->universe_id) return 1;
    if (a->fill_universe != b->fill_universe) return 1;
    if (a->lat_type != b->lat_type) return 1;
    if (a->tree_hash != b->tree_hash) return 1;
    if (a->lattice_hash != b->lattice_hash) return 1;
    /* Compare density with tolerance */
    double dd = fabs(a->density - b->density);
    if (dd > 1e-6 && dd > fabs(a->density) * 1e-6) return 1;
    return 0;
}

int ag_surface_fp_compare(const ag_surface_fp_t* a, const ag_surface_fp_t* b) {
    if (a->primitive_type != b->primitive_type) return 1;
    if (a->boundary_type != b->boundary_type) return 1;
    if (a->data_hash != b->data_hash) return 1;
    return 0;
}

uint32_t ag_cell_fp_diff(const ag_cell_fp_t* a, const ag_cell_fp_t* b) {
    uint32_t flags = 0;
    if (a->material_id != b->material_id) flags |= CELL_CHG_MATERIAL;
    double dd = fabs(a->density - b->density);
    if (dd > 1e-6 && dd > fabs(a->density) * 1e-6) flags |= CELL_CHG_DENSITY;
    if (a->tree_hash != b->tree_hash) flags |= CELL_CHG_REGION;
    if (a->universe_id != b->universe_id) flags |= CELL_CHG_UNIVERSE;
    if (a->fill_universe != b->fill_universe) flags |= CELL_CHG_FILL;
    if (a->lattice_hash != b->lattice_hash) flags |= CELL_CHG_LATTICE;
    return flags;
}

uint32_t ag_surface_fp_diff(const ag_surface_fp_t* a, const ag_surface_fp_t* b) {
    uint32_t flags = 0;
    if (a->primitive_type != b->primitive_type) flags |= SURF_CHG_TYPE;
    if (a->data_hash != b->data_hash) flags |= SURF_CHG_DATA;
    if (a->boundary_type != b->boundary_type) flags |= SURF_CHG_BOUNDARY;
    return flags;
}
