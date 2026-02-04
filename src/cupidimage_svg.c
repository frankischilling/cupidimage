#include "cupidimage.h"
#include "cupidimage_internal.h"
#include "cupidimage_svg_base.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wfloat-conversion"
#pragma GCC diagnostic ignored "-Wfloat-equal"
#pragma GCC diagnostic ignored "-Wshadow"
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SVG_MAX_ATTR 64
#define SVG_MAX_DEPTH 64
#define SVG_MAX_DASH 16
#define SVG_MAX_TRANSFER_TABLE 64
#define SVG_MAX_INFO_ATTR 32

typedef struct svg_attr {
    char *name;
    char *value;
} svg_attr;

typedef struct svg_tag {
    char *name;
    svg_attr attrs[SVG_MAX_ATTR];
    char override_names[SVG_MAX_ATTR][64];
    char override_values[SVG_MAX_ATTR][128];
    uint8_t has_override_name[SVG_MAX_ATTR];
    uint8_t has_override_value[SVG_MAX_ATTR];
    int attr_count;
    int is_end;
    int is_self_closing;
} svg_tag;

typedef enum {
    SVG_PAINT_NONE,
    SVG_PAINT_COLOR,
    SVG_PAINT_LINEAR_GRADIENT,
    SVG_PAINT_RADIAL_GRADIENT,
    SVG_PAINT_PATTERN
} svg_paint_type;

typedef enum {
    SVG_LINECAP_BUTT,
    SVG_LINECAP_ROUND,
    SVG_LINECAP_SQUARE
} svg_linecap;

typedef enum {
    SVG_LINEJOIN_MITER,
    SVG_LINEJOIN_ROUND,
    SVG_LINEJOIN_BEVEL
} svg_linejoin;

typedef enum {
    SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX,
    SVG_GRADIENT_UNITS_USER_SPACE
} svg_gradient_units;

typedef enum {
    SVG_SPREAD_PAD,
    SVG_SPREAD_REFLECT,
    SVG_SPREAD_REPEAT
} svg_spread_method;

typedef enum {
    SVG_MASK_TYPE_LUMINANCE,
    SVG_MASK_TYPE_ALPHA
} svg_mask_type;

typedef struct svg_grad_coord {
    float value;
    int is_percent;
} svg_grad_coord;

typedef struct svg_gradient_stop {
    float offset;
    uint32_t color;
} svg_gradient_stop;

typedef struct svg_gradient {
    char *id;
    char *href;
    svg_paint_type type;
    svg_gradient_units units;
    svg_spread_method spread;
    svg_matrix transform;
    svg_matrix inv_transform;
    int has_transform;
    int has_units;
    int has_spread;
    int has_coords;
    int has_focal;
    svg_grad_coord x1, y1, x2, y2;
    svg_grad_coord cx, cy, r, fx, fy;
    svg_gradient_stop *stops;
    int stop_count;
} svg_gradient;

typedef struct svg_path_def {
    char *id;
    char *d;
} svg_path_def;

typedef struct svg_use_def {
    char *id;
    char *tag_name;
    svg_attr attrs[SVG_MAX_ATTR];
    int attr_count;
} svg_use_def;

typedef struct svg_symbol {
    char *id;
    char *content;
    float vb_x;
    float vb_y;
    float vb_w;
    float vb_h;
    int vb_ok;
    int preserve_none;
    float align_x;
    float align_y;
} svg_symbol;

typedef struct svg_marker {
    char *id;
    char *content;
    float ref_x;
    float ref_y;
    float marker_w;
    float marker_h;
    int units_stroke_width;
    int orient_auto;
    int orient_auto_start_reverse;
    float orient_angle;
    float vb_x;
    float vb_y;
    float vb_w;
    float vb_h;
    int vb_ok;
    int preserve_none;
    float align_x;
    float align_y;
} svg_marker;

typedef struct svg_pattern {
    char *id;
    char *href;
    svg_gradient_units units;
    svg_gradient_units content_units;
    int has_units;
    int has_content_units;
    int has_coords;
    svg_grad_coord x;
    svg_grad_coord y;
    svg_grad_coord width;
    svg_grad_coord height;
    svg_matrix transform;
    svg_matrix inv_transform;
    int has_transform;
    char *content;
    uint8_t *rgba;
    uint32_t tile_w;
    uint32_t tile_h;
    float cached_bbox_x;
    float cached_bbox_y;
    float cached_bbox_w;
    float cached_bbox_h;
    int has_cached_bbox;
    int rendered;
    int rendering;
} svg_pattern;

typedef struct svg_clip_path {
    char *id;
    char *href;
    svg_gradient_units units;
    int has_units;
    svg_matrix transform;
    svg_matrix inv_transform;
    int has_transform;
    char *content;
    uint8_t *rgba;
    uint32_t mask_w;
    uint32_t mask_h;
    float cached_bbox_x;
    float cached_bbox_y;
    float cached_bbox_w;
    float cached_bbox_h;
    int has_cached_bbox;
    int rendered;
    int rendering;
} svg_clip_path;

typedef struct svg_mask {
    char *id;
    char *href;
    svg_gradient_units units;
    svg_gradient_units content_units;
    int has_units;
    int has_content_units;
    int has_coords;
    svg_grad_coord x;
    svg_grad_coord y;
    svg_grad_coord width;
    svg_grad_coord height;
    svg_matrix transform;
    svg_matrix inv_transform;
    int has_transform;
    int type;
    int has_type;
    char *content;
    uint8_t *rgba;
    uint32_t mask_w;
    uint32_t mask_h;
    float cached_bbox_x;
    float cached_bbox_y;
    float cached_bbox_w;
    float cached_bbox_h;
    int has_cached_bbox;
    int rendered;
    int rendering;
} svg_mask;

typedef struct svg_filter {
    char *id;
    char *content;
    svg_gradient_units units;
    int has_units;
    int has_coords;
    svg_grad_coord x;
    svg_grad_coord y;
    svg_grad_coord width;
    svg_grad_coord height;
} svg_filter;

typedef struct svg_defs {
    svg_gradient *gradients;
    int gradient_count;
    int gradient_cap;
    svg_pattern *patterns;
    int pattern_count;
    int pattern_cap;
    struct svg_path_def *paths;
    int path_count;
    int path_cap;
    struct svg_use_def *uses;
    int use_count;
    int use_cap;
    struct svg_symbol *symbols;
    int symbol_count;
    int symbol_cap;
    struct svg_marker *markers;
    int marker_count;
    int marker_cap;
    svg_clip_path *clips;
    int clip_count;
    int clip_cap;
    svg_mask *masks;
    int mask_count;
    int mask_cap;
    svg_filter *filters;
    int filter_count;
    int filter_cap;
} svg_defs;

typedef struct svg_css_decl {
    int prop;
    char *value;
} svg_css_decl;

typedef struct svg_selector_part {
    char *tag;
    char *id;
    char **classes;
    int class_count;
    char **attr_names;
    char **attr_values;
    uint8_t *attr_has_value;
    int attr_count;
} svg_selector_part;

typedef struct svg_selector {
    svg_selector_part *parts;
    int *combinators;
    int part_count;
    int specificity;
} svg_selector;

typedef struct svg_css_rule {
    svg_selector *selectors;
    int selector_count;
    svg_css_decl *decls;
    int decl_count;
    int order;
} svg_css_rule;

typedef enum {
    SVG_CSS_ANIM_PROP_NONE = 0,
    SVG_CSS_ANIM_PROP_TRANSFORM,
    SVG_CSS_ANIM_PROP_STROKE_DASHOFFSET,
    SVG_CSS_ANIM_PROP_STOP_COLOR
} svg_css_anim_prop;

typedef enum {
    SVG_CSS_ANIM_DIR_NORMAL = 0,
    SVG_CSS_ANIM_DIR_REVERSE,
    SVG_CSS_ANIM_DIR_ALTERNATE,
    SVG_CSS_ANIM_DIR_ALTERNATE_REVERSE
} svg_css_anim_direction;

typedef enum {
    SVG_CSS_ANIM_FILL_NONE = 0,
    SVG_CSS_ANIM_FILL_FORWARDS,
    SVG_CSS_ANIM_FILL_BACKWARDS,
    SVG_CSS_ANIM_FILL_BOTH
} svg_css_anim_fill_mode;

typedef struct svg_css_keyframe_step {
    float offset;
    char *transform;
    float number;
    uint32_t color;
} svg_css_keyframe_step;

typedef struct svg_css_keyframes {
    char *name;
    int prop;
    svg_css_keyframe_step *steps;
    int step_count;
    int step_cap;
} svg_css_keyframes;

typedef struct svg_css_animation_binding {
    char *target_id;
    char *name;
    float duration;
    float delay;
    float repeat_count;
    int repeat_indefinite;
    int direction;
    int fill_mode;
} svg_css_animation_binding;

typedef struct svg_css {
    svg_css_rule *rules;
    int rule_count;
    int rule_cap;
    svg_css_keyframes *keyframes;
    int keyframe_count;
    int keyframe_cap;
    svg_css_animation_binding *animations;
    int animation_count;
    int animation_cap;
} svg_css;

typedef struct svg_style {
    svg_paint_type fill_type;
    svg_paint_type stroke_type;
    uint32_t fill_color;
    uint32_t stroke_color;
    const svg_gradient *fill_gradient;
    const svg_gradient *stroke_gradient;
    svg_pattern *fill_pattern;
    svg_pattern *stroke_pattern;
    svg_clip_path *clip_path;
    svg_mask *mask;
    svg_marker *marker_start;
    svg_marker *marker_mid;
    svg_marker *marker_end;
    const svg_filter *filter;
    uint32_t color;
    float fill_opacity;
    float stroke_opacity;
    float opacity;
    float stroke_width;
    int stroke_linecap;
    int stroke_linejoin;
    float stroke_miterlimit;
    float stroke_dasharray[SVG_MAX_DASH];
    int stroke_dashcount;
    float stroke_dashoffset;
    int fill_rule_evenodd;
    float font_size;
    float letter_spacing;
    float word_spacing;
    int text_anchor;
    int bold;
    int italic;
    int dominant_baseline;
    int display_none;
    int visibility_hidden;
} svg_style;

typedef struct svg_bbox {
    float x;
    float y;
    float w;
    float h;
} svg_bbox;

static void svg_bbox_init(svg_bbox *bbox) {
    if (!bbox) {
        return;
    }
    bbox->x = 0.0f;
    bbox->y = 0.0f;
    bbox->w = -1.0f;
    bbox->h = -1.0f;
}

static void svg_bbox_add_rect(svg_bbox *bbox, float x, float y, float w, float h) {
    if (!bbox || w < 0.0f || h < 0.0f) {
        return;
    }
    if (bbox->w < 0.0f || bbox->h < 0.0f) {
        bbox->x = x;
        bbox->y = y;
        bbox->w = w;
        bbox->h = h;
        return;
    }
    float minx = fminf(bbox->x, x);
    float miny = fminf(bbox->y, y);
    float maxx = fmaxf(bbox->x + bbox->w, x + w);
    float maxy = fmaxf(bbox->y + bbox->h, y + h);
    bbox->x = minx;
    bbox->y = miny;
    bbox->w = maxx - minx;
    bbox->h = maxy - miny;
}

static int svg_bbox_valid(const svg_bbox *bbox) {
    return bbox && bbox->w > 0.0f && bbox->h > 0.0f;
}

static void svg_bbox_expand(svg_bbox *bbox, float amount) {
    if (!bbox || amount <= 0.0f) {
        return;
    }
    if (bbox->w < 0.0f || bbox->h < 0.0f) {
        return;
    }
    bbox->x -= amount;
    bbox->y -= amount;
    bbox->w += amount * 2.0f;
    bbox->h += amount * 2.0f;
}

typedef struct svg_segment {
    float x0, y0, x1, y1;
} svg_segment;

typedef struct svg_segments {
    svg_segment *stroke;
    size_t stroke_count;
    size_t stroke_cap;
    svg_segment *fill;
    size_t fill_count;
    size_t fill_cap;
    float minx;
    float miny;
    float maxx;
    float maxy;
} svg_segments;

typedef struct svg_text_state {
    svg_style style;
    svg_matrix transform;
    int preserve;
} svg_text_state;

typedef struct svg_text_path_state {
    svg_segments segs;
    float *seg_lengths;
    size_t seg_count;
    float total_len;
    float pos;
    int active;
    svg_bbox bbox;
} svg_text_path_state;

typedef enum {
    SVG_FILTER_OP_BLUR,
    SVG_FILTER_OP_OFFSET,
    SVG_FILTER_OP_COLOR_MATRIX,
    SVG_FILTER_OP_FLOOD,
    SVG_FILTER_OP_BLEND,
    SVG_FILTER_OP_COMPOSITE,
    SVG_FILTER_OP_MERGE,
    SVG_FILTER_OP_COMPONENT_TRANSFER,
    SVG_FILTER_OP_MORPHOLOGY,
    SVG_FILTER_OP_CONVOLVE,
    SVG_FILTER_OP_TURBULENCE,
    SVG_FILTER_OP_DISPLACEMENT,
    SVG_FILTER_OP_DIFFUSE_LIGHTING,
    SVG_FILTER_OP_SPECULAR_LIGHTING,
    SVG_FILTER_OP_TILE,
    SVG_FILTER_OP_IMAGE
} svg_filter_op_type;

typedef enum {
    SVG_FILTER_IN_CURRENT,
    SVG_FILTER_IN_SOURCE_GRAPHIC,
    SVG_FILTER_IN_SOURCE_ALPHA,
    SVG_FILTER_IN_RESULT,
    SVG_FILTER_IN_BACKGROUND_IMAGE,
    SVG_FILTER_IN_BACKGROUND_ALPHA,
    SVG_FILTER_IN_FILL_PAINT,
    SVG_FILTER_IN_STROKE_PAINT
} svg_filter_input;

typedef struct svg_filter_ref {
    int type;
    int index;
} svg_filter_ref;

typedef enum {
    SVG_TRANSFER_IDENTITY,
    SVG_TRANSFER_TABLE,
    SVG_TRANSFER_DISCRETE,
    SVG_TRANSFER_LINEAR,
    SVG_TRANSFER_GAMMA
} svg_transfer_type;

typedef enum {
    SVG_MORPHOLOGY_ERODE,
    SVG_MORPHOLOGY_DILATE
} svg_morphology_op;

typedef enum {
    SVG_EDGE_NONE,
    SVG_EDGE_DUPLICATE,
    SVG_EDGE_WRAP
} svg_edge_mode;

typedef enum {
    SVG_DISPLACE_R,
    SVG_DISPLACE_G,
    SVG_DISPLACE_B,
    SVG_DISPLACE_A
} svg_displace_channel;

typedef struct svg_transfer_func {
    int type;
    float table[SVG_MAX_TRANSFER_TABLE];
    int table_count;
    float slope;
    float intercept;
    float amplitude;
    float exponent;
    float offset;
} svg_transfer_func;

typedef enum {
    SVG_BLEND_NORMAL,
    SVG_BLEND_MULTIPLY,
    SVG_BLEND_SCREEN,
    SVG_BLEND_DARKEN,
    SVG_BLEND_LIGHTEN,
    SVG_BLEND_OVERLAY,
    SVG_BLEND_HARDLIGHT,
    SVG_BLEND_SOFTLIGHT
} svg_blend_mode;

typedef enum {
    SVG_COMPOSITE_OVER,
    SVG_COMPOSITE_IN,
    SVG_COMPOSITE_OUT,
    SVG_COMPOSITE_ATOP,
    SVG_COMPOSITE_XOR,
    SVG_COMPOSITE_ARITHMETIC
} svg_composite_op;

typedef struct svg_filter_op {
    svg_filter_op_type type;
    float a;
    float b;
    float c;
    float d;
    uint32_t color;
    float matrix[20];
    int has_matrix;
    svg_filter_ref in1;
    svg_filter_ref in2;
    int mode;
    int merge_count;
    svg_filter_ref merge_inputs[8];
    int result_id;
    svg_transfer_func tr;
    svg_transfer_func tg;
    svg_transfer_func tb;
    svg_transfer_func ta;
    int has_region;
    svg_grad_coord rx;
    svg_grad_coord ry;
    svg_grad_coord rwidth;
    svg_grad_coord rheight;
    float *kernel;
    int kernel_count;
    int order_x;
    int order_y;
    int target_x;
    int target_y;
    float divisor;
    float bias;
    int preserve_alpha;
    float turb_freq_x;
    float turb_freq_y;
    int turb_octaves;
    int turb_seed;
    int turb_fractal;
    int displace_x_channel;
    int displace_y_channel;
    float surface_scale;
    float diffuse_constant;
    float specular_constant;
    float specular_exponent;
    float light_x;
    float light_y;
    float light_z;
    char *image_href;
    float img_x;
    float img_y;
    float img_w;
    float img_h;
    int has_image_geom;
} svg_filter_op;

typedef struct svg_join_tri {
    float ax, ay;
    float bx, by;
    float cx, cy;
} svg_join_tri;

typedef struct svg_filter_region {
    int x0, y0;
    int x1, y1;
    int valid;
} svg_filter_region;

typedef struct svg_render_ctx {
    uint32_t width;
    uint32_t height;
    uint32_t ss;
    float vb_x;
    float vb_y;
    float vb_w;
    float vb_h;
    float dpi;
    float anim_time;
    uint8_t *hi_rgba;
} svg_render_ctx;

typedef struct svg_preamble {
    int found_svg;
    float width_attr;
    float height_attr;
    int width_ok;
    int height_ok;
    float vb_x;
    float vb_y;
    float vb_w;
    float vb_h;
    int vb_ok;
    int preserve_none;
    float align_x;
    float align_y;
} svg_preamble;

typedef struct svg_elem_info {
    const char *tag;
    const char *id;
    const char *class_attr;
    const char *attr_names[SVG_MAX_INFO_ATTR];
    const char *attr_values[SVG_MAX_INFO_ATTR];
    int attr_count;
} svg_elem_info;

typedef struct svg_stack_item {
    svg_style style;
    svg_matrix transform;
    svg_elem_info info;
} svg_stack_item;

typedef struct svg_sibling_frame {
    svg_elem_info *items;
    int count;
    int cap;
} svg_sibling_frame;

static void set_err(char *err, size_t errcap, const char *msg) {
    if (err && errcap) {
        snprintf(err, errcap, "%s", msg);
    }
}

static int svg_hex_value(int c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static int svg_base64_value(int c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static int svg_decode_data_payload(const char *payload, int is_base64,
                                   unsigned char **out, size_t *out_size) {
    if (!payload || !out || !out_size) {
        return 0;
    }
    *out = NULL;
    *out_size = 0;
    size_t len = strlen(payload);
    if (!is_base64) {
        unsigned char *buf = (unsigned char *)malloc(len + 1u);
        if (!buf) {
            return 0;
        }
        size_t w = 0;
        for (size_t i = 0; i < len; i++) {
            unsigned char c = (unsigned char)payload[i];
            if (c == '%' && i + 2 < len) {
                int hi = svg_hex_value((unsigned char)payload[i + 1]);
                int lo = svg_hex_value((unsigned char)payload[i + 2]);
                if (hi >= 0 && lo >= 0) {
                    c = (unsigned char)((hi << 4) | lo);
                    i += 2;
                }
            }
            buf[w++] = c;
        }
        *out = buf;
        *out_size = w;
        return 1;
    }

    size_t cap = (len / 4u) * 3u + 4u;
    unsigned char *buf = (unsigned char *)malloc(cap);
    if (!buf) {
        return 0;
    }
    int vals[4];
    int vcount = 0;
    size_t w = 0;
    int saw_padding = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)payload[i];
        if (isspace(c)) {
            continue;
        }
        if (c == '=') {
            vals[vcount++] = -2;
            saw_padding = 1;
        } else {
            int v = svg_base64_value(c);
            if (v < 0 || saw_padding) {
                free(buf);
                return 0;
            }
            vals[vcount++] = v;
        }
        if (vcount == 4) {
            if (vals[0] < 0 || vals[1] < 0) {
                free(buf);
                return 0;
            }
            if (w + 3u > cap) {
                size_t new_cap = cap * 2u;
                unsigned char *tmp = (unsigned char *)realloc(buf, new_cap);
                if (!tmp) {
                    free(buf);
                    return 0;
                }
                buf = tmp;
                cap = new_cap;
            }
            buf[w++] = (unsigned char)((vals[0] << 2) | (vals[1] >> 4));
            if (vals[2] != -2) {
                if (vals[2] < 0) {
                    free(buf);
                    return 0;
                }
                buf[w++] = (unsigned char)(((vals[1] & 0x0F) << 4) | (vals[2] >> 2));
                if (vals[3] != -2) {
                    if (vals[3] < 0) {
                        free(buf);
                        return 0;
                    }
                    buf[w++] = (unsigned char)(((vals[2] & 0x03) << 6) | vals[3]);
                }
            }
            vcount = 0;
        }
    }
    if (vcount != 0) {
        free(buf);
        return 0;
    }
    *out = buf;
    *out_size = w;
    return 1;
}

static int svg_load_href_image(const char *href, cupidimage_image *img) {
    if (!href || !img) {
        return 0;
    }
    memset(img, 0, sizeof(*img));
    if (*href == '#') {
        return 0;
    }
    if (svg_strncasecmp(href, "http://", 7) == 0 || svg_strncasecmp(href, "https://", 8) == 0) {
        return 0;
    }
    if (svg_strncasecmp(href, "data:", 5) == 0) {
        const char *comma = strchr(href, ',');
        if (!comma || comma <= href + 5) {
            return 0;
        }
        size_t meta_len = (size_t)(comma - (href + 5));
        char *meta = svg_strndup(href + 5, meta_len);
        if (!meta) {
            return 0;
        }
        int is_base64 = svg_strcasestr_simple(meta, ";base64");
        free(meta);
        unsigned char *payload = NULL;
        size_t payload_size = 0;
        if (!svg_decode_data_payload(comma + 1, is_base64, &payload, &payload_size)) {
            return 0;
        }
        char errbuf[128];
        int ok = cupidimage_load_image(payload, payload_size, img, errbuf, sizeof(errbuf));
        free(payload);
        return ok;
    }
    char errbuf[128];
    return cupidimage_load_image_file(href, img, errbuf, sizeof(errbuf));
}

static const char *svg_get_attr(const svg_tag *tag, const char *name) {
    if (!tag || !name) {
        return NULL;
    }
    for (int i = 0; i < tag->attr_count; i++) {
        const char *attr_name = svg_local_name(tag->attrs[i].name);
        if (attr_name && svg_strcasecmp(attr_name, name) == 0) {
            return tag->attrs[i].value;
        }
    }
    return NULL;
}

static const char *svg_get_attr_exact(const svg_tag *tag, const char *name) {
    if (!tag || !name) {
        return NULL;
    }
    for (int i = 0; i < tag->attr_count; i++) {
        if (tag->attrs[i].name && strcmp(tag->attrs[i].name, name) == 0) {
            return tag->attrs[i].value;
        }
    }
    return NULL;
}

static void svg_elem_info_from_tag(svg_elem_info *info, const char *local, const svg_tag *tag) {
    if (!info) {
        return;
    }
    memset(info, 0, sizeof(*info));
    info->tag = local;
    if (!tag) {
        return;
    }
    info->id = svg_get_attr(tag, "id");
    info->class_attr = svg_get_attr(tag, "class");
    int count = tag->attr_count;
    if (count > SVG_MAX_INFO_ATTR) {
        count = SVG_MAX_INFO_ATTR;
    }
    info->attr_count = count;
    for (int i = 0; i < count; i++) {
        info->attr_names[i] = tag->attrs[i].name;
        info->attr_values[i] = tag->attrs[i].value;
    }
}

static void svg_sibling_frames_free(svg_sibling_frame *frames, int depth_cap) {
    if (!frames || depth_cap <= 0) {
        return;
    }
    for (int i = 0; i < depth_cap; i++) {
        free(frames[i].items);
        frames[i].items = NULL;
        frames[i].count = 0;
        frames[i].cap = 0;
    }
}

static int svg_sibling_frame_push(svg_sibling_frame *frame, const svg_elem_info *info) {
    if (!frame || !info) {
        return 0;
    }
    if (frame->count >= frame->cap) {
        int new_cap = frame->cap ? frame->cap * 2 : 16;
        svg_elem_info *n = (svg_elem_info *)realloc(frame->items, (size_t)new_cap * sizeof(svg_elem_info));
        if (!n) {
            return 0;
        }
        frame->items = n;
        frame->cap = new_cap;
    }
    frame->items[frame->count++] = *info;
    return 1;
}

static void svg_skip_separators(const char **p) {
    while (**p && (isspace((unsigned char)**p) || **p == ',')) {
        (*p)++;
    }
}

static int svg_parse_number(const char **p, float *out) {
    svg_skip_separators(p);
    if (!**p) {
        return 0;
    }
    char *end = NULL;
    double v = strtod(*p, &end);
    if (end == *p) {
        return 0;
    }
    *out = (float)v;
    *p = end;
    return 1;
}

static int svg_parse_transform_number(const char **p, float *out) {
    if (!svg_parse_number(p, out)) {
        return 0;
    }
    while (**p && isspace((unsigned char)**p)) {
        (*p)++;
    }
    if (**p == '%') {
        (*p)++;
        return 1;
    }
    while (**p && isalpha((unsigned char)**p)) {
        (*p)++;
    }
    return 1;
}

static float svg_parse_length(const char *s, float base, float dpi, int *ok) {
    if (ok) {
        *ok = 0;
    }
    if (!s) {
        return 0.0f;
    }
    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (!*s) {
        return 0.0f;
    }
    char *end = NULL;
    double val = strtod(s, &end);
    if (end == s) {
        return 0.0f;
    }
    while (isspace((unsigned char)*end)) {
        end++;
    }
    double out = val;
    if (*end == '%') {
        if (base <= 0.0f) {
            if (ok) {
                *ok = 0;
            }
            return 0.0f;
        }
        out = base * val / 100.0;
        end++;
    } else if (strncmp(end, "px", 2) == 0) {
        end += 2;
    } else if (strncmp(end, "pt", 2) == 0) {
        out = val * (dpi / 72.0);
        end += 2;
    } else if (strncmp(end, "pc", 2) == 0) {
        out = val * (dpi / 6.0);
        end += 2;
    } else if (strncmp(end, "in", 2) == 0) {
        out = val * dpi;
        end += 2;
    } else if (strncmp(end, "cm", 2) == 0) {
        out = val * (dpi / 2.54);
        end += 2;
    } else if (strncmp(end, "mm", 2) == 0) {
        out = val * (dpi / 25.4);
        end += 2;
    }
    if (ok) {
        *ok = 1;
    }
    return (float)out;
}

static int svg_parse_length_value(const char **p, float base, float dpi, float *out) {
    if (!p || !*p || !out) {
        return 0;
    }
    svg_skip_separators(p);
    if (!**p) {
        return 0;
    }
    char *end = NULL;
    double val = strtod(*p, &end);
    if (end == *p) {
        return 0;
    }
    const char *q = end;
    while (isspace((unsigned char)*q)) {
        q++;
    }
    double outv = val;
    if (*q == '%') {
        if (base > 0.0f) {
            outv = base * val / 100.0;
        }
        q++;
    } else if (strncmp(q, "px", 2) == 0) {
        q += 2;
    } else if (strncmp(q, "pt", 2) == 0) {
        outv = val * (dpi / 72.0);
        q += 2;
    } else if (strncmp(q, "pc", 2) == 0) {
        outv = val * (dpi / 6.0);
        q += 2;
    } else if (strncmp(q, "in", 2) == 0) {
        outv = val * dpi;
        q += 2;
    } else if (strncmp(q, "cm", 2) == 0) {
        outv = val * (dpi / 2.54);
        q += 2;
    } else if (strncmp(q, "mm", 2) == 0) {
        outv = val * (dpi / 25.4);
        q += 2;
    }
    *out = (float)outv;
    *p = q;
    return 1;
}

static int svg_parse_opacity_value(const char *s, float *out) {
    if (!s || !out) {
        return 0;
    }
    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (!*s) {
        return 0;
    }
    char *end = NULL;
    double v = strtod(s, &end);
    if (end == s) {
        return 0;
    }
    while (isspace((unsigned char)*end)) {
        end++;
    }
    if (*end == '%') {
        v /= 100.0;
        end++;
    }
    while (isspace((unsigned char)*end)) {
        end++;
    }
    if (*end) {
        return 0;
    }
    if (v < 0.0) {
        v = 0.0;
    } else if (v > 1.0) {
        v = 1.0;
    }
    *out = (float)v;
    return 1;
}

static int svg_parse_viewbox(const char *s, float *x, float *y, float *w, float *h) {
    if (!s) {
        return 0;
    }
    const char *p = s;
    float vals[4];
    for (int i = 0; i < 4; i++) {
        if (!svg_parse_number(&p, &vals[i])) {
            return 0;
        }
    }
    *x = vals[0];
    *y = vals[1];
    *w = vals[2];
    *h = vals[3];
    return 1;
}

static void svg_parse_preserve_aspect_ratio(const char *s, int *preserve_none, float *align_x, float *align_y) {
    if (preserve_none) {
        *preserve_none = 0;
    }
    if (align_x) {
        *align_x = 0.5f;
    }
    if (align_y) {
        *align_y = 0.5f;
    }
    if (!s) {
        return;
    }
    const char *tok = s;
    while (*tok && isspace((unsigned char)*tok)) {
        tok++;
    }
    if (strncmp(tok, "none", 4) == 0) {
        if (preserve_none) {
            *preserve_none = 1;
        }
        return;
    }
    if (strncmp(tok, "xMinYMin", 8) == 0) { if (align_x) *align_x = 0.0f; if (align_y) *align_y = 0.0f; }
    else if (strncmp(tok, "xMidYMin", 8) == 0) { if (align_x) *align_x = 0.5f; if (align_y) *align_y = 0.0f; }
    else if (strncmp(tok, "xMaxYMin", 8) == 0) { if (align_x) *align_x = 1.0f; if (align_y) *align_y = 0.0f; }
    else if (strncmp(tok, "xMinYMid", 8) == 0) { if (align_x) *align_x = 0.0f; if (align_y) *align_y = 0.5f; }
    else if (strncmp(tok, "xMidYMid", 8) == 0) { if (align_x) *align_x = 0.5f; if (align_y) *align_y = 0.5f; }
    else if (strncmp(tok, "xMaxYMid", 8) == 0) { if (align_x) *align_x = 1.0f; if (align_y) *align_y = 0.5f; }
    else if (strncmp(tok, "xMinYMax", 8) == 0) { if (align_x) *align_x = 0.0f; if (align_y) *align_y = 1.0f; }
    else if (strncmp(tok, "xMidYMax", 8) == 0) { if (align_x) *align_x = 0.5f; if (align_y) *align_y = 1.0f; }
    else if (strncmp(tok, "xMaxYMax", 8) == 0) { if (align_x) *align_x = 1.0f; if (align_y) *align_y = 1.0f; }
}

static int svg_next_tag(char **p, svg_tag *tag);
static int svg_parse_transform(const char *value, svg_matrix *out);

static int svg_parse_clock_seconds(const char *s, float *out) {
    if (!s || !out) {
        return 0;
    }
    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (!*s) {
        return 0;
    }
    char *end = NULL;
    double v = strtod(s, &end);
    if (end == s) {
        return 0;
    }
    while (isspace((unsigned char)*end)) {
        end++;
    }
    if (svg_strncasecmp(end, "ms", 2) == 0) {
        v /= 1000.0;
        end += 2;
    } else if (*end == 's') {
        end++;
    } else if (*end == 'm') {
        v *= 60.0;
        end++;
    } else if (*end == 'h') {
        v *= 3600.0;
        end++;
    }
    while (isspace((unsigned char)*end)) {
        end++;
    }
    if (*end) {
        return 0;
    }
    *out = (float)v;
    return 1;
}

static int svg_parse_repeat_count(const char *s, float *out, int *indefinite) {
    if (!out || !indefinite) {
        return 0;
    }
    *out = 1.0f;
    *indefinite = 0;
    if (!s || !*s) {
        return 1;
    }
    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (svg_strcasecmp(s, "indefinite") == 0) {
        *indefinite = 1;
        return 1;
    }
    char *end = NULL;
    double v = strtod(s, &end);
    if (end == s) {
        return 0;
    }
    while (isspace((unsigned char)*end)) {
        end++;
    }
    if (*end) {
        return 0;
    }
    if (v <= 0.0) {
        return 0;
    }
    *out = (float)v;
    return 1;
}

static int svg_get_animation_progress(const svg_tag *anim, float time_s, float *out_progress) {
    if (!anim || !out_progress) {
        return 0;
    }
    float begin = 0.0f;
    float dur = 0.0f;
    if (!svg_parse_clock_seconds(svg_get_attr(anim, "dur"), &dur) || dur <= 0.0f) {
        return 0;
    }
    const char *begin_attr = svg_get_attr(anim, "begin");
    if (begin_attr && *begin_attr) {
        const char *semi = strchr(begin_attr, ';');
        if (semi) {
            begin_attr = svg_strndup(begin_attr, (size_t)(semi - begin_attr));
        }
        if (!svg_parse_clock_seconds(begin_attr, &begin)) {
            if (semi) {
                free((char *)begin_attr);
            }
            begin = 0.0f;
        }
        if (semi) {
            free((char *)begin_attr);
        }
    }
    if (time_s < begin) {
        return 0;
    }
    float local_t = time_s - begin;
    float repeat = 1.0f;
    int indefinite = 0;
    svg_parse_repeat_count(svg_get_attr(anim, "repeatCount"), &repeat, &indefinite);
    if (indefinite) {
        float t = fmodf(local_t, dur);
        if (t < 0.0f) t += dur;
        *out_progress = dur > 0.0f ? (t / dur) : 0.0f;
        return 1;
    }
    float total = dur * repeat;
    if (local_t >= total) {
        const char *fill = svg_get_attr(anim, "fill");
        if (fill && svg_strcasecmp(fill, "freeze") == 0) {
            *out_progress = 1.0f;
            return 1;
        }
        return 0;
    }
    float t = fmodf(local_t, dur);
    if (t < 0.0f) t += dur;
    *out_progress = dur > 0.0f ? (t / dur) : 0.0f;
    return 1;
}

static int svg_parse_number_list(const char *s, float *vals, int max_vals, int *out_count) {
    if (!s || !vals || max_vals <= 0 || !out_count) {
        return 0;
    }
    const char *p = s;
    int n = 0;
    while (n < max_vals) {
        float v = 0.0f;
        if (!svg_parse_number(&p, &v)) {
            break;
        }
        vals[n++] = v;
    }
    *out_count = n;
    return n > 0;
}

static int svg_parse_number_values_list(const char *s, float *vals, int max_vals, int *out_count) {
    if (!s || !vals || max_vals <= 0 || !out_count) {
        return 0;
    }
    int n = 0;
    const char *p = s;
    while (*p && n < max_vals) {
        while (*p && isspace((unsigned char)*p)) {
            p++;
        }
        const char *start = p;
        while (*p && *p != ';') {
            p++;
        }
        const char *end = p;
        while (end > start && isspace((unsigned char)end[-1])) {
            end--;
        }
        if (end > start) {
            char *tok = svg_strndup(start, (size_t)(end - start));
            if (!tok) {
                return 0;
            }
            char *tokp = tok;
            float v = 0.0f;
            if (svg_parse_number((const char **)&tokp, &v)) {
                vals[n++] = v;
            }
            free(tok);
        }
        if (*p == ';') {
            p++;
        }
    }
    *out_count = n;
    return n > 0;
}

static int svg_tag_set_attr(svg_tag *tag, const char *name, const char *value) {
    if (!tag || !name || !value) {
        return 0;
    }
    for (int i = 0; i < tag->attr_count; i++) {
        const char *local = svg_local_name(tag->attrs[i].name);
        if (local && svg_strcasecmp(local, name) == 0) {
            snprintf(tag->override_values[i], sizeof(tag->override_values[i]), "%s", value);
            tag->attrs[i].value = tag->override_values[i];
            tag->has_override_value[i] = 1;
            return 1;
        }
    }
    if (tag->attr_count >= SVG_MAX_ATTR) {
        return 0;
    }
    snprintf(tag->override_names[tag->attr_count], sizeof(tag->override_names[tag->attr_count]), "%s", name);
    tag->attrs[tag->attr_count].name = tag->override_names[tag->attr_count];
    tag->has_override_name[tag->attr_count] = 1;
    snprintf(tag->override_values[tag->attr_count], sizeof(tag->override_values[tag->attr_count]), "%s", value);
    tag->attrs[tag->attr_count].value = tag->override_values[tag->attr_count];
    tag->has_override_value[tag->attr_count] = 1;
    tag->attr_count++;
    return 1;
}

static void svg_format_number(char *buf, size_t cap, float v) {
    if (!buf || cap == 0) {
        return;
    }
    snprintf(buf, cap, "%.6g", (double)v);
}

static int svg_animate_numeric(const svg_tag *anim, const char *attr_name, svg_tag *target, float progress) {
    const char *from_s = svg_get_attr(anim, "from");
    const char *to_s = svg_get_attr(anim, "to");
    const char *by_s = svg_get_attr(anim, "by");
    const char *values_s = svg_get_attr(anim, "values");
    float out = 0.0f;
    int ok = 0;
    if (values_s && *values_s) {
        float vals[32];
        int count = 0;
        if (svg_parse_number_values_list(values_s, vals, 32, &count) && count > 0) {
            if (count == 1) {
                out = vals[0];
            } else {
                float segf = progress * (float)(count - 1);
                int seg = (int)floorf(segf);
                if (seg < 0) seg = 0;
                if (seg >= count - 1) {
                    seg = count - 2;
                }
                float t = segf - (float)seg;
                out = vals[seg] + (vals[seg + 1] - vals[seg]) * t;
            }
            ok = 1;
        }
    }
    if (!ok && to_s && *to_s) {
        float to_v = 0.0f;
        const char *to_p = to_s;
        if (svg_parse_number(&to_p, &to_v)) {
            float from_v = 0.0f;
            int have_from = 0;
            if (from_s && *from_s) {
                const char *from_p = from_s;
                if (svg_parse_number(&from_p, &from_v)) {
                    have_from = 1;
                }
            }
            if (!have_from) {
                const char *base_s = svg_get_attr(target, attr_name);
                if (base_s) {
                    const char *base_p = base_s;
                    if (svg_parse_number(&base_p, &from_v)) {
                        have_from = 1;
                    }
                }
            }
            if (!have_from) {
                from_v = 0.0f;
            }
            out = from_v + (to_v - from_v) * progress;
            ok = 1;
        }
    }
    if (!ok && by_s && *by_s) {
        float by_v = 0.0f;
        const char *by_p = by_s;
        if (svg_parse_number(&by_p, &by_v)) {
            float from_v = 0.0f;
            if (from_s && *from_s) {
                const char *from_p = from_s;
                svg_parse_number(&from_p, &from_v);
            } else {
                const char *base_s = svg_get_attr(target, attr_name);
                if (base_s) {
                    const char *base_p = base_s;
                    svg_parse_number(&base_p, &from_v);
                }
            }
            out = from_v + by_v * progress;
            ok = 1;
        }
    }
    if (!ok) {
        return 0;
    }
    char num[64];
    svg_format_number(num, sizeof(num), out);
    return svg_tag_set_attr(target, attr_name, num);
}

static int svg_animate_transform(const svg_tag *anim, svg_tag *target, float progress) {
    const char *type = svg_get_attr(anim, "type");
    if (!type || !*type) {
        return 0;
    }
    const char *from_s = svg_get_attr(anim, "from");
    const char *to_s = svg_get_attr(anim, "to");
    const char *by_s = svg_get_attr(anim, "by");
    float from_v[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float to_v[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float by_v[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    int from_n = 0, to_n = 0, by_n = 0;
    if (from_s) svg_parse_number_list(from_s, from_v, 4, &from_n);
    if (to_s) svg_parse_number_list(to_s, to_v, 4, &to_n);
    if (by_s) svg_parse_number_list(by_s, by_v, 4, &by_n);
    char out[128];
    out[0] = '\0';
    if (svg_strcasecmp(type, "translate") == 0) {
        float fx = from_n > 0 ? from_v[0] : 0.0f;
        float fy = from_n > 1 ? from_v[1] : 0.0f;
        float x = fx;
        float y = fy;
        if (to_n > 0) {
            float tx = to_v[0];
            float ty = to_n > 1 ? to_v[1] : 0.0f;
            x = fx + (tx - fx) * progress;
            y = fy + (ty - fy) * progress;
        } else if (by_n > 0) {
            float bx = by_v[0];
            float by = by_n > 1 ? by_v[1] : 0.0f;
            x = fx + bx * progress;
            y = fy + by * progress;
        } else {
            return 0;
        }
        snprintf(out, sizeof(out), "translate(%.6g %.6g)", (double)x, (double)y);
    } else if (svg_strcasecmp(type, "scale") == 0) {
        float fx = from_n > 0 ? from_v[0] : 1.0f;
        float fy = from_n > 1 ? from_v[1] : fx;
        float x = fx;
        float y = fy;
        if (to_n > 0) {
            float tx = to_v[0];
            float ty = to_n > 1 ? to_v[1] : tx;
            x = fx + (tx - fx) * progress;
            y = fy + (ty - fy) * progress;
        } else if (by_n > 0) {
            float bx = by_v[0];
            float by = by_n > 1 ? by_v[1] : bx;
            x = fx + bx * progress;
            y = fy + by * progress;
        } else {
            return 0;
        }
        snprintf(out, sizeof(out), "scale(%.6g %.6g)", (double)x, (double)y);
    } else if (svg_strcasecmp(type, "rotate") == 0) {
        float fa = from_n > 0 ? from_v[0] : 0.0f;
        float a = fa;
        float cx = from_n > 1 ? from_v[1] : 0.0f;
        float cy = from_n > 2 ? from_v[2] : 0.0f;
        if (to_n > 0) {
            float ta = to_v[0];
            a = fa + (ta - fa) * progress;
            if (from_n > 2 && to_n > 2) {
                cx = from_v[1] + (to_v[1] - from_v[1]) * progress;
                cy = from_v[2] + (to_v[2] - from_v[2]) * progress;
            }
        } else if (by_n > 0) {
            a = fa + by_v[0] * progress;
        } else {
            return 0;
        }
        if (from_n > 2 || to_n > 2) {
            snprintf(out, sizeof(out), "rotate(%.6g %.6g %.6g)", (double)a, (double)cx, (double)cy);
        } else {
            snprintf(out, sizeof(out), "rotate(%.6g)", (double)a);
        }
    } else if (svg_strcasecmp(type, "skewx") == 0 || svg_strcasecmp(type, "skewy") == 0) {
        float fa = from_n > 0 ? from_v[0] : 0.0f;
        float a = fa;
        if (to_n > 0) {
            a = fa + (to_v[0] - fa) * progress;
        } else if (by_n > 0) {
            a = fa + by_v[0] * progress;
        } else {
            return 0;
        }
        snprintf(out, sizeof(out), "%s(%.6g)",
                 svg_strcasecmp(type, "skewx") == 0 ? "skewX" : "skewY", (double)a);
    } else {
        return 0;
    }
    return svg_tag_set_attr(target, "transform", out);
}

static void svg_apply_animation_children(svg_tag *target, const char *target_local, char *child_start, float time_s) {
    if (!target || !target_local || !child_start || time_s < 0.0f) {
        return;
    }
    if (!svg_strcasestr_simple(child_start, "<animate") &&
        !svg_strcasestr_simple(child_start, "<set")) {
        return;
    }
    char *scan = svg_strdup(child_start);
    if (!scan) {
        return;
    }
    char *q = scan;
    svg_tag tag;
    int nested = 0;
    while (svg_next_tag(&q, &tag)) {
        const char *local = svg_local_name(tag.name);
        if (tag.is_end) {
            if (svg_strcasecmp(local, target_local) == 0) {
                if (nested == 0) {
                    break;
                }
                nested--;
            }
            continue;
        }
        if (svg_strcasecmp(local, target_local) == 0 && !tag.is_self_closing) {
            nested++;
            continue;
        }
        if (nested > 0) {
            continue;
        }
        if (svg_strcasecmp(local, "set") == 0) {
            float prog = 0.0f;
            if (!svg_get_animation_progress(&tag, time_s, &prog)) {
                continue;
            }
            const char *attr_name = svg_get_attr(&tag, "attributeName");
            const char *to = svg_get_attr(&tag, "to");
            if (attr_name && to && *attr_name && *to) {
                svg_tag_set_attr(target, attr_name, to);
            }
            continue;
        }
        if (svg_strcasecmp(local, "animate") == 0) {
            float prog = 0.0f;
            if (!svg_get_animation_progress(&tag, time_s, &prog)) {
                continue;
            }
            const char *attr_name = svg_get_attr(&tag, "attributeName");
            if (!attr_name || !*attr_name) {
                continue;
            }
            if (!svg_animate_numeric(&tag, attr_name, target, prog)) {
                const char *values = svg_get_attr(&tag, "values");
                if (values) {
                    const char *start = values;
                    const char *end = values;
                    const char *chosen = values;
                    int count = 0;
                    while (*end) {
                        if (*end == ';') {
                            count++;
                        }
                        end++;
                    }
                    count++;
                    int idx = (int)floorf(prog * (float)count);
                    if (idx >= count) idx = count - 1;
                    int cur = 0;
                    start = values;
                    end = values;
                    while (*end) {
                        if (*end == ';') {
                            if (cur == idx) {
                                break;
                            }
                            cur++;
                            start = end + 1;
                        }
                        end++;
                    }
                    if (cur == idx) {
                        chosen = start;
                    }
                    if (chosen) {
                        size_t n = 0;
                        while (chosen[n] && chosen[n] != ';') n++;
                        while (n > 0 && isspace((unsigned char)chosen[n - 1])) n--;
                        while (*chosen && isspace((unsigned char)*chosen)) chosen++;
                        char *val = svg_strndup(chosen, n);
                        if (val) {
                            svg_tag_set_attr(target, attr_name, val);
                            free(val);
                        }
                    }
                }
            }
            continue;
        }
        if (svg_strcasecmp(local, "animatetransform") == 0) {
            float prog = 0.0f;
            if (!svg_get_animation_progress(&tag, time_s, &prog)) {
                continue;
            }
            svg_animate_transform(&tag, target, prog);
            continue;
        }
    }
    free(scan);
}

static const svg_css_keyframes *svg_css_find_keyframes(const svg_css *css, const char *name) {
    if (!css || !name || !*name) {
        return NULL;
    }
    for (int i = 0; i < css->keyframe_count; i++) {
        if (css->keyframes[i].name && strcmp(css->keyframes[i].name, name) == 0) {
            return &css->keyframes[i];
        }
    }
    return NULL;
}

static float svg_css_apply_anim_direction(int direction, int cycle, float phase) {
    int reverse = 0;
    if (direction == SVG_CSS_ANIM_DIR_REVERSE) {
        reverse = 1;
    } else if (direction == SVG_CSS_ANIM_DIR_ALTERNATE) {
        reverse = (cycle & 1) != 0;
    } else if (direction == SVG_CSS_ANIM_DIR_ALTERNATE_REVERSE) {
        reverse = (cycle & 1) == 0;
    }
    return reverse ? (1.0f - phase) : phase;
}

static int svg_css_anim_progress(const svg_css_animation_binding *anim, float time_s, float *out_progress) {
    if (!anim || !out_progress || anim->duration <= 0.0f) {
        return 0;
    }
    float t = time_s - anim->delay;
    if (t < 0.0f) {
        if (anim->fill_mode == SVG_CSS_ANIM_FILL_BACKWARDS || anim->fill_mode == SVG_CSS_ANIM_FILL_BOTH) {
            *out_progress = svg_css_apply_anim_direction(anim->direction, 0, 0.0f);
            return 1;
        }
        return 0;
    }
    if (anim->repeat_indefinite) {
        float cyclef = floorf(t / anim->duration);
        int cycle = (int)cyclef;
        float phase = (t - cyclef * anim->duration) / anim->duration;
        if (phase < 0.0f) phase = 0.0f;
        if (phase > 1.0f) phase = 1.0f;
        *out_progress = svg_css_apply_anim_direction(anim->direction, cycle, phase);
        return 1;
    }
    float repeats = anim->repeat_count > 0.0f ? anim->repeat_count : 1.0f;
    float total = anim->duration * repeats;
    if (t >= total) {
        if (anim->fill_mode != SVG_CSS_ANIM_FILL_FORWARDS && anim->fill_mode != SVG_CSS_ANIM_FILL_BOTH) {
            return 0;
        }
        float wholef = floorf(repeats);
        int cycle = 0;
        float phase = 1.0f;
        if (fabsf(repeats - wholef) < 1e-6f) {
            cycle = (int)(wholef > 1.0f ? wholef - 1.0f : 0.0f);
            phase = 1.0f;
        } else {
            cycle = (int)wholef;
            phase = repeats - wholef;
        }
        *out_progress = svg_css_apply_anim_direction(anim->direction, cycle, phase);
        return 1;
    }
    float cyclef = floorf(t / anim->duration);
    int cycle = (int)cyclef;
    float phase = (t - cyclef * anim->duration) / anim->duration;
    if (phase < 0.0f) phase = 0.0f;
    if (phase > 1.0f) phase = 1.0f;
    *out_progress = svg_css_apply_anim_direction(anim->direction, cycle, phase);
    return 1;
}

static int svg_css_keyframe_range(const svg_css_keyframes *kf, float progress,
                                  const svg_css_keyframe_step **a, const svg_css_keyframe_step **b, float *u) {
    if (!kf || kf->step_count <= 0 || !a || !b || !u) {
        return 0;
    }
    const svg_css_keyframe_step *steps = kf->steps;
    int n = kf->step_count;
    if (progress <= steps[0].offset) {
        *a = &steps[0];
        *b = &steps[0];
        *u = 0.0f;
        return 1;
    }
    if (progress >= steps[n - 1].offset) {
        *a = &steps[n - 1];
        *b = &steps[n - 1];
        *u = 0.0f;
        return 1;
    }
    for (int i = 0; i < n - 1; i++) {
        if (progress >= steps[i].offset && progress <= steps[i + 1].offset) {
            *a = &steps[i];
            *b = &steps[i + 1];
            float span = steps[i + 1].offset - steps[i].offset;
            *u = (span > 0.0f) ? (progress - steps[i].offset) / span : 0.0f;
            if (*u < 0.0f) *u = 0.0f;
            if (*u > 1.0f) *u = 1.0f;
            return 1;
        }
    }
    *a = &steps[n - 1];
    *b = &steps[n - 1];
    *u = 0.0f;
    return 1;
}

static void svg_css_apply_animations_to_tag(svg_tag *tag, const svg_css *css, float time_s) {
    if (!tag || !css || css->animation_count <= 0) {
        return;
    }
    const char *id = svg_get_attr(tag, "id");
    if (!id || !*id) {
        return;
    }
    for (int i = 0; i < css->animation_count; i++) {
        const svg_css_animation_binding *anim = &css->animations[i];
        if (!anim->target_id || strcmp(anim->target_id, id) != 0) {
            continue;
        }
        float progress = 0.0f;
        if (!svg_css_anim_progress(anim, time_s, &progress)) {
            continue;
        }
        const svg_css_keyframes *kf = svg_css_find_keyframes(css, anim->name);
        if (!kf) {
            continue;
        }
        const svg_css_keyframe_step *a = NULL;
        const svg_css_keyframe_step *b = NULL;
        float u = 0.0f;
        if (!svg_css_keyframe_range(kf, progress, &a, &b, &u) || !a || !b) {
            continue;
        }
        if (kf->prop == SVG_CSS_ANIM_PROP_TRANSFORM) {
            const char *av = a->transform;
            const char *bv = b->transform;
            if (av && bv) {
                svg_matrix ma;
                svg_matrix mb;
                if (svg_parse_transform(av, &ma) && svg_parse_transform(bv, &mb)) {
                    svg_matrix m;
                    m.a = ma.a + (mb.a - ma.a) * u;
                    m.b = ma.b + (mb.b - ma.b) * u;
                    m.c = ma.c + (mb.c - ma.c) * u;
                    m.d = ma.d + (mb.d - ma.d) * u;
                    m.e = ma.e + (mb.e - ma.e) * u;
                    m.f = ma.f + (mb.f - ma.f) * u;
                    char buf[160];
                    snprintf(buf, sizeof(buf), "matrix(%.6g %.6g %.6g %.6g %.6g %.6g)",
                             (double)m.a, (double)m.b, (double)m.c, (double)m.d, (double)m.e, (double)m.f);
                    svg_tag_set_attr(tag, "transform", buf);
                } else {
                    svg_tag_set_attr(tag, "transform", u < 0.5f ? av : bv);
                }
            }
        } else if (kf->prop == SVG_CSS_ANIM_PROP_STROKE_DASHOFFSET) {
            float v = a->number + (b->number - a->number) * u;
            char buf[64];
            snprintf(buf, sizeof(buf), "%.6g", (double)v);
            svg_tag_set_attr(tag, "stroke-dashoffset", buf);
        } else if (kf->prop == SVG_CSS_ANIM_PROP_STOP_COLOR) {
            uint32_t ca = a->color;
            uint32_t cb = b->color;
            uint8_t ar = (uint8_t)(ca >> 24);
            uint8_t ag = (uint8_t)(ca >> 16);
            uint8_t ab = (uint8_t)(ca >> 8);
            uint8_t aa = (uint8_t)(ca & 0xFFu);
            uint8_t br = (uint8_t)(cb >> 24);
            uint8_t bg = (uint8_t)(cb >> 16);
            uint8_t bb = (uint8_t)(cb >> 8);
            uint8_t ba = (uint8_t)(cb & 0xFFu);
            uint8_t r = (uint8_t)lroundf(ar + (br - ar) * u);
            uint8_t g = (uint8_t)lroundf(ag + (bg - ag) * u);
            uint8_t bch = (uint8_t)lroundf(ab + (bb - ab) * u);
            uint8_t a8 = (uint8_t)lroundf(aa + (ba - aa) * u);
            char buf[96];
            if (a8 == 255) {
                snprintf(buf, sizeof(buf), "#%02x%02x%02x", r, g, bch);
            } else {
                snprintf(buf, sizeof(buf), "rgba(%u,%u,%u,%.6g)", r, g, bch, (double)(a8 / 255.0f));
            }
            svg_tag_set_attr(tag, "stop-color", buf);
        }
    }
}

typedef enum {
    SVG_PROP_FILL,
    SVG_PROP_STROKE,
    SVG_PROP_STROKE_WIDTH,
    SVG_PROP_STROKE_LINECAP,
    SVG_PROP_STROKE_LINEJOIN,
    SVG_PROP_STROKE_MITERLIMIT,
    SVG_PROP_STROKE_DASHARRAY,
    SVG_PROP_STROKE_DASHOFFSET,
    SVG_PROP_CLIP_PATH,
    SVG_PROP_MASK,
    SVG_PROP_MARKER,
    SVG_PROP_MARKER_START,
    SVG_PROP_MARKER_MID,
    SVG_PROP_MARKER_END,
    SVG_PROP_FILTER,
    SVG_PROP_OPACITY,
    SVG_PROP_FILL_OPACITY,
    SVG_PROP_STROKE_OPACITY,
    SVG_PROP_FILL_RULE,
    SVG_PROP_COLOR,
    SVG_PROP_FONT_SIZE,
    SVG_PROP_TEXT_ANCHOR,
    SVG_PROP_FONT_WEIGHT,
    SVG_PROP_FONT_STYLE,
    SVG_PROP_DOMINANT_BASELINE,
    SVG_PROP_LETTER_SPACING,
    SVG_PROP_WORD_SPACING,
    SVG_PROP_DISPLAY,
    SVG_PROP_VISIBILITY,
    SVG_PROP_COUNT
} svg_property;

static void svg_style_init(svg_style *style) {
    style->fill_type = SVG_PAINT_COLOR;
    style->stroke_type = SVG_PAINT_NONE;
    style->fill_color = 0x000000FFu;
    style->stroke_color = 0x000000FFu;
    style->fill_gradient = NULL;
    style->stroke_gradient = NULL;
    style->fill_pattern = NULL;
    style->stroke_pattern = NULL;
    style->clip_path = NULL;
    style->mask = NULL;
    style->marker_start = NULL;
    style->marker_mid = NULL;
    style->marker_end = NULL;
    style->filter = NULL;
    style->color = 0x000000FFu;
    style->fill_opacity = 1.0f;
    style->stroke_opacity = 1.0f;
    style->opacity = 1.0f;
    style->stroke_width = 1.0f;
    style->stroke_linecap = SVG_LINECAP_BUTT;
    style->stroke_linejoin = SVG_LINEJOIN_MITER;
    style->stroke_miterlimit = 4.0f;
    for (int i = 0; i < SVG_MAX_DASH; i++) {
        style->stroke_dasharray[i] = 0.0f;
    }
    style->stroke_dashcount = 0;
    style->stroke_dashoffset = 0.0f;
    style->fill_rule_evenodd = 0;
    style->font_size = 16.0f;
    style->letter_spacing = 0.0f;
    style->word_spacing = 0.0f;
    style->text_anchor = 0;
    style->bold = 0;
    style->italic = 0;
    style->dominant_baseline = 0;
    style->display_none = 0;
    style->visibility_hidden = 0;
}

typedef struct svg_named_color {
    const char *name;
    uint32_t rgba;
} svg_named_color;

static const svg_named_color svg_named_colors[] = {
    {"aliceblue", 0xF0F8FFFFu},
    {"antiquewhite", 0xFAEBD7FFu},
    {"aqua", 0x00FFFFFFu},
    {"aquamarine", 0x7FFFD4FFu},
    {"azure", 0xF0FFFFFFu},
    {"beige", 0xF5F5DCFFu},
    {"bisque", 0xFFE4C4FFu},
    {"black", 0x000000FFu},
    {"blanchedalmond", 0xFFEBCDFFu},
    {"blue", 0x0000FFFFu},
    {"blueviolet", 0x8A2BE2FFu},
    {"brown", 0xA52A2AFFu},
    {"burlywood", 0xDEB887FFu},
    {"cadetblue", 0x5F9EA0FFu},
    {"chartreuse", 0x7FFF00FFu},
    {"chocolate", 0xD2691EFFu},
    {"coral", 0xFF7F50FFu},
    {"cornflowerblue", 0x6495EDFFu},
    {"cornsilk", 0xFFF8DCFFu},
    {"crimson", 0xDC143CFFu},
    {"cyan", 0x00FFFFFFu},
    {"darkblue", 0x00008BFFu},
    {"darkcyan", 0x008B8BFFu},
    {"darkgoldenrod", 0xB8860BFFu},
    {"darkgray", 0xA9A9A9FFu},
    {"darkgreen", 0x006400FFu},
    {"darkgrey", 0xA9A9A9FFu},
    {"darkkhaki", 0xBDB76BFFu},
    {"darkmagenta", 0x8B008BFFu},
    {"darkolivegreen", 0x556B2FFFu},
    {"darkorange", 0xFF8C00FFu},
    {"darkorchid", 0x9932CCFFu},
    {"darkred", 0x8B0000FFu},
    {"darksalmon", 0xE9967AFFu},
    {"darkseagreen", 0x8FBC8FFFu},
    {"darkslateblue", 0x483D8BFFu},
    {"darkslategray", 0x2F4F4FFFu},
    {"darkslategrey", 0x2F4F4FFFu},
    {"darkturquoise", 0x00CED1FFu},
    {"darkviolet", 0x9400D3FFu},
    {"deeppink", 0xFF1493FFu},
    {"deepskyblue", 0x00BFFFFFu},
    {"dimgray", 0x696969FFu},
    {"dimgrey", 0x696969FFu},
    {"dodgerblue", 0x1E90FFFFu},
    {"firebrick", 0xB22222FFu},
    {"floralwhite", 0xFFFAF0FFu},
    {"forestgreen", 0x228B22FFu},
    {"fuchsia", 0xFF00FFFFu},
    {"gainsboro", 0xDCDCDCFFu},
    {"ghostwhite", 0xF8F8FFFFu},
    {"gold", 0xFFD700FFu},
    {"goldenrod", 0xDAA520FFu},
    {"gray", 0x808080FFu},
    {"green", 0x008000FFu},
    {"greenyellow", 0xADFF2FFFu},
    {"grey", 0x808080FFu},
    {"honeydew", 0xF0FFF0FFu},
    {"hotpink", 0xFF69B4FFu},
    {"indianred", 0xCD5C5CFFu},
    {"indigo", 0x4B0082FFu},
    {"ivory", 0xFFFFF0FFu},
    {"khaki", 0xF0E68CFFu},
    {"lavender", 0xE6E6FAFFu},
    {"lavenderblush", 0xFFF0F5FFu},
    {"lawngreen", 0x7CFC00FFu},
    {"lemonchiffon", 0xFFFACDFFu},
    {"lightblue", 0xADD8E6FFu},
    {"lightcoral", 0xF08080FFu},
    {"lightcyan", 0xE0FFFFFFu},
    {"lightgoldenrodyellow", 0xFAFAD2FFu},
    {"lightgray", 0xD3D3D3FFu},
    {"lightgreen", 0x90EE90FFu},
    {"lightgrey", 0xD3D3D3FFu},
    {"lightpink", 0xFFB6C1FFu},
    {"lightsalmon", 0xFFA07AFFu},
    {"lightseagreen", 0x20B2AAFFu},
    {"lightskyblue", 0x87CEFAFFu},
    {"lightslategray", 0x778899FFu},
    {"lightslategrey", 0x778899FFu},
    {"lightsteelblue", 0xB0C4DEFFu},
    {"lightyellow", 0xFFFFE0FFu},
    {"lime", 0x00FF00FFu},
    {"limegreen", 0x32CD32FFu},
    {"linen", 0xFAF0E6FFu},
    {"magenta", 0xFF00FFFFu},
    {"maroon", 0x800000FFu},
    {"mediumaquamarine", 0x66CDAAFFu},
    {"mediumblue", 0x0000CDFFu},
    {"mediumorchid", 0xBA55D3FFu},
    {"mediumpurple", 0x9370DBFFu},
    {"mediumseagreen", 0x3CB371FFu},
    {"mediumslateblue", 0x7B68EEFFu},
    {"mediumspringgreen", 0x00FA9AFFu},
    {"mediumturquoise", 0x48D1CCFFu},
    {"mediumvioletred", 0xC71585FFu},
    {"midnightblue", 0x191970FFu},
    {"mintcream", 0xF5FFFAFFu},
    {"mistyrose", 0xFFE4E1FFu},
    {"moccasin", 0xFFE4B5FFu},
    {"navajowhite", 0xFFDEADFFu},
    {"navy", 0x000080FFu},
    {"oldlace", 0xFDF5E6FFu},
    {"olive", 0x808000FFu},
    {"olivedrab", 0x6B8E23FFu},
    {"orange", 0xFFA500FFu},
    {"orangered", 0xFF4500FFu},
    {"orchid", 0xDA70D6FFu},
    {"palegoldenrod", 0xEEE8AAFFu},
    {"palegreen", 0x98FB98FFu},
    {"paleturquoise", 0xAFEEEEFFu},
    {"palevioletred", 0xDB7093FFu},
    {"papayawhip", 0xFFEFD5FFu},
    {"peachpuff", 0xFFDAB9FFu},
    {"peru", 0xCD853FFFu},
    {"pink", 0xFFC0CBFFu},
    {"plum", 0xDDA0DDFFu},
    {"powderblue", 0xB0E0E6FFu},
    {"purple", 0x800080FFu},
    {"rebeccapurple", 0x663399FFu},
    {"red", 0xFF0000FFu},
    {"rosybrown", 0xBC8F8FFFu},
    {"royalblue", 0x4169E1FFu},
    {"saddlebrown", 0x8B4513FFu},
    {"salmon", 0xFA8072FFu},
    {"sandybrown", 0xF4A460FFu},
    {"seagreen", 0x2E8B57FFu},
    {"seashell", 0xFFF5EEFFu},
    {"sienna", 0xA0522DFFu},
    {"silver", 0xC0C0C0FFu},
    {"skyblue", 0x87CEEBFFu},
    {"slateblue", 0x6A5ACDFFu},
    {"slategray", 0x708090FFu},
    {"slategrey", 0x708090FFu},
    {"snow", 0xFFFAFAFFu},
    {"springgreen", 0x00FF7FFFu},
    {"steelblue", 0x4682B4FFu},
    {"tan", 0xD2B48CFFu},
    {"teal", 0x008080FFu},
    {"thistle", 0xD8BFD8FFu},
    {"tomato", 0xFF6347FFu},
    {"turquoise", 0x40E0D0FFu},
    {"violet", 0xEE82EEFFu},
    {"wheat", 0xF5DEB3FFu},
    {"white", 0xFFFFFFFFu},
    {"whitesmoke", 0xF5F5F5FFu},
    {"yellow", 0xFFFF00FFu},
    {"yellowgreen", 0x9ACD32FFu}
};

static const uint8_t svg_font8x8_basic[128][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x7E,0x81,0xA5,0x81,0xBD,0x99,0x81,0x7E},
    {0x7E,0xFF,0xDB,0xFF,0xC3,0xE7,0xFF,0x7E},
    {0x6C,0xFE,0xFE,0xFE,0x7C,0x38,0x10,0x00},
    {0x10,0x38,0x7C,0xFE,0x7C,0x38,0x10,0x00},
    {0x38,0x7C,0x38,0xFE,0xFE,0xD6,0x10,0x38},
    {0x10,0x38,0x7C,0xFE,0xFE,0x7C,0x10,0x38},
    {0x00,0x00,0x18,0x3C,0x3C,0x18,0x00,0x00},
    {0xFF,0xFF,0xE7,0xC3,0xC3,0xE7,0xFF,0xFF},
    {0x00,0x3C,0x66,0x42,0x42,0x66,0x3C,0x00},
    {0xFF,0xC3,0x99,0xBD,0xBD,0x99,0xC3,0xFF},
    {0x0F,0x07,0x0F,0x7D,0xCC,0xCC,0xCC,0x78},
    {0x3C,0x66,0x66,0x66,0x3C,0x18,0x7E,0x18},
    {0x3F,0x33,0x3F,0x30,0x30,0x70,0xF0,0xE0},
    {0x7F,0x63,0x7F,0x63,0x63,0x67,0xE6,0xC0},
    {0x99,0x5A,0x3C,0xE7,0xE7,0x3C,0x5A,0x99},
    {0x80,0xE0,0xF8,0xFE,0xF8,0xE0,0x80,0x00},
    {0x02,0x0E,0x3E,0xFE,0x3E,0x0E,0x02,0x00},
    {0x18,0x3C,0x7E,0x18,0x18,0x7E,0x3C,0x18},
    {0x66,0x66,0x66,0x66,0x66,0x00,0x66,0x00},
    {0x7F,0xDB,0xDB,0x7B,0x1B,0x1B,0x1B,0x00},
    {0x3E,0x63,0x38,0x6C,0x6C,0x38,0xCC,0x7F},
    {0x00,0x00,0x00,0x00,0x7E,0x7E,0x7E,0x00},
    {0x18,0x3C,0x7E,0x18,0x7E,0x3C,0x18,0xFF},
    {0x18,0x3C,0x7E,0x18,0x18,0x18,0x18,0x00},
    {0x18,0x18,0x18,0x18,0x7E,0x3C,0x18,0x00},
    {0x00,0x18,0x0C,0xFE,0x0C,0x18,0x00,0x00},
    {0x00,0x30,0x60,0xFE,0x60,0x30,0x00,0x00},
    {0x00,0x00,0xC0,0xC0,0xC0,0xFE,0x00,0x00},
    {0x00,0x24,0x66,0xFF,0x66,0x24,0x00,0x00},
    {0x00,0x18,0x3C,0x7E,0xFF,0xFF,0x00,0x00},
    {0x00,0xFF,0xFF,0x7E,0x3C,0x18,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00},
    {0x36,0x36,0x12,0x00,0x00,0x00,0x00,0x00},
    {0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00},
    {0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0x00},
    {0x00,0x63,0x33,0x18,0x0C,0x66,0x63,0x00},
    {0x1C,0x36,0x1C,0x6E,0x3B,0x33,0x6E,0x00},
    {0x06,0x06,0x03,0x00,0x00,0x00,0x00,0x00},
    {0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00},
    {0x06,0x0C,0x18,0x18,0x18,0x0C,0x06,0x00},
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00},
    {0x00,0x0C,0x0C,0x3F,0x0C,0x0C,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x06},
    {0x00,0x00,0x00,0x3F,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x00},
    {0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00},
    {0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0x00},
    {0x0C,0x0E,0x0C,0x0C,0x0C,0x0C,0x3F,0x00},
    {0x1E,0x33,0x30,0x1C,0x06,0x33,0x3F,0x00},
    {0x1E,0x33,0x30,0x1C,0x30,0x33,0x1E,0x00},
    {0x38,0x3C,0x36,0x33,0x7F,0x30,0x78,0x00},
    {0x3F,0x03,0x1F,0x30,0x30,0x33,0x1E,0x00},
    {0x1C,0x06,0x03,0x1F,0x33,0x33,0x1E,0x00},
    {0x3F,0x33,0x30,0x18,0x0C,0x0C,0x0C,0x00},
    {0x1E,0x33,0x33,0x1E,0x33,0x33,0x1E,0x00},
    {0x1E,0x33,0x33,0x3E,0x30,0x18,0x0E,0x00},
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x00},
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x06},
    {0x18,0x0C,0x06,0x03,0x06,0x0C,0x18,0x00},
    {0x00,0x00,0x3F,0x00,0x00,0x3F,0x00,0x00},
    {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00},
    {0x1E,0x33,0x30,0x18,0x0C,0x00,0x0C,0x00},
    {0x3E,0x63,0x7B,0x7B,0x7B,0x03,0x1E,0x00},
    {0x0C,0x1E,0x33,0x33,0x3F,0x33,0x33,0x00},
    {0x3F,0x66,0x66,0x3E,0x66,0x66,0x3F,0x00},
    {0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x00},
    {0x1F,0x36,0x66,0x66,0x66,0x36,0x1F,0x00},
    {0x7F,0x46,0x16,0x1E,0x16,0x46,0x7F,0x00},
    {0x7F,0x46,0x16,0x1E,0x16,0x06,0x0F,0x00},
    {0x3C,0x66,0x03,0x03,0x73,0x66,0x7C,0x00},
    {0x33,0x33,0x33,0x3F,0x33,0x33,0x33,0x00},
    {0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00},
    {0x78,0x30,0x30,0x30,0x33,0x33,0x1E,0x00},
    {0x67,0x66,0x36,0x1E,0x36,0x66,0x67,0x00},
    {0x0F,0x06,0x06,0x06,0x46,0x66,0x7F,0x00},
    {0x63,0x77,0x7F,0x7F,0x6B,0x63,0x63,0x00},
    {0x63,0x67,0x6F,0x7B,0x73,0x63,0x63,0x00},
    {0x1C,0x36,0x63,0x63,0x63,0x36,0x1C,0x00},
    {0x3F,0x66,0x66,0x3E,0x06,0x06,0x0F,0x00},
    {0x1E,0x33,0x33,0x33,0x3B,0x1E,0x38,0x00},
    {0x3F,0x66,0x66,0x3E,0x36,0x66,0x67,0x00},
    {0x1E,0x33,0x07,0x0E,0x38,0x33,0x1E,0x00},
    {0x3F,0x2D,0x0C,0x0C,0x0C,0x0C,0x1E,0x00},
    {0x33,0x33,0x33,0x33,0x33,0x33,0x3F,0x00},
    {0x33,0x33,0x33,0x33,0x33,0x1E,0x0C,0x00},
    {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00},
    {0x63,0x63,0x36,0x1C,0x1C,0x36,0x63,0x00},
    {0x33,0x33,0x33,0x1E,0x0C,0x0C,0x1E,0x00},
    {0x7F,0x63,0x31,0x18,0x4C,0x66,0x7F,0x00},
    {0x1E,0x06,0x06,0x06,0x06,0x06,0x1E,0x00},
    {0x03,0x06,0x0C,0x18,0x30,0x60,0x40,0x00},
    {0x1E,0x18,0x18,0x18,0x18,0x18,0x1E,0x00},
    {0x08,0x1C,0x36,0x63,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF},
    {0x0C,0x0C,0x18,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x1E,0x30,0x3E,0x33,0x6E,0x00},
    {0x07,0x06,0x06,0x3E,0x66,0x66,0x3B,0x00},
    {0x00,0x00,0x1E,0x33,0x03,0x33,0x1E,0x00},
    {0x38,0x30,0x30,0x3E,0x33,0x33,0x6E,0x00},
    {0x00,0x00,0x1E,0x33,0x3F,0x03,0x1E,0x00},
    {0x1C,0x36,0x06,0x0F,0x06,0x06,0x0F,0x00},
    {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x1F},
    {0x07,0x06,0x36,0x6E,0x66,0x66,0x67,0x00},
    {0x0C,0x00,0x0E,0x0C,0x0C,0x0C,0x1E,0x00},
    {0x18,0x00,0x1C,0x18,0x18,0x18,0x1B,0x0E},
    {0x07,0x06,0x66,0x36,0x1E,0x36,0x67,0x00},
    {0x0E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00},
    {0x00,0x00,0x33,0x7F,0x7F,0x6B,0x63,0x00},
    {0x00,0x00,0x1F,0x33,0x33,0x33,0x33,0x00},
    {0x00,0x00,0x1E,0x33,0x33,0x33,0x1E,0x00},
    {0x00,0x00,0x3B,0x66,0x66,0x3E,0x06,0x0F},
    {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x78},
    {0x00,0x00,0x3B,0x6E,0x66,0x06,0x0F,0x00},
    {0x00,0x00,0x3E,0x03,0x1E,0x30,0x1F,0x00},
    {0x08,0x0C,0x3E,0x0C,0x0C,0x2C,0x18,0x00},
    {0x00,0x00,0x33,0x33,0x33,0x33,0x6E,0x00},
    {0x00,0x00,0x33,0x33,0x33,0x1E,0x0C,0x00},
    {0x00,0x00,0x63,0x63,0x6B,0x7F,0x36,0x00},
    {0x00,0x00,0x63,0x36,0x1C,0x36,0x63,0x00},
    {0x00,0x00,0x33,0x33,0x33,0x3E,0x30,0x1F},
    {0x00,0x00,0x3F,0x19,0x0C,0x26,0x3F,0x00},
    {0x38,0x0C,0x0C,0x07,0x0C,0x0C,0x38,0x00},
    {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00},
    {0x07,0x0C,0x0C,0x38,0x0C,0x0C,0x07,0x00},
    {0x6E,0x3B,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}
};

static float svg_clamp01(float v) {
    if (v < 0.0f) {
        return 0.0f;
    }
    if (v > 1.0f) {
        return 1.0f;
    }
    return v;
}

static float svg_hue_to_rgb_component(float p, float q, float t) {
    if (t < 0.0f) {
        t += 1.0f;
    } else if (t > 1.0f) {
        t -= 1.0f;
    }
    if (t < (1.0f / 6.0f)) {
        return p + (q - p) * 6.0f * t;
    }
    if (t < 0.5f) {
        return q;
    }
    if (t < (2.0f / 3.0f)) {
        return p + (q - p) * ((2.0f / 3.0f) - t) * 6.0f;
    }
    return p;
}

static void svg_hsl_to_rgb(float h_deg, float s, float l, float *r, float *g, float *b) {
    if (!r || !g || !b) {
        return;
    }
    s = svg_clamp01(s);
    l = svg_clamp01(l);
    float h = h_deg / 360.0f;
    h = h - floorf(h);
    if (s <= 1e-6f) {
        *r = l;
        *g = l;
        *b = l;
        return;
    }
    float q = (l < 0.5f) ? (l * (1.0f + s)) : (l + s - l * s);
    float p = 2.0f * l - q;
    *r = svg_hue_to_rgb_component(p, q, h + (1.0f / 3.0f));
    *g = svg_hue_to_rgb_component(p, q, h);
    *b = svg_hue_to_rgb_component(p, q, h - (1.0f / 3.0f));
}

static int svg_parse_color_component(const char **p, float *out, int *is_percent) {
    if (!p || !*p || !out) {
        return 0;
    }
    svg_skip_separators(p);
    if (!**p) {
        return 0;
    }
    char *end = NULL;
    double v = strtod(*p, &end);
    if (end == *p) {
        return 0;
    }
    while (isspace((unsigned char)*end)) {
        end++;
    }
    int percent = 0;
    if (*end == '%') {
        percent = 1;
        end++;
    }
    *out = (float)v;
    if (is_percent) {
        *is_percent = percent;
    }
    *p = end;
    return 1;
}

static int svg_parse_hue_component(const char **p, float *out_deg) {
    if (!p || !*p || !out_deg) {
        return 0;
    }
    svg_skip_separators(p);
    if (!**p) {
        return 0;
    }
    char *end = NULL;
    double v = strtod(*p, &end);
    if (end == *p) {
        return 0;
    }
    while (isspace((unsigned char)*end)) {
        end++;
    }
    if (svg_strncasecmp(end, "deg", 3) == 0) {
        end += 3;
    } else if (svg_strncasecmp(end, "grad", 4) == 0) {
        v *= 0.9;
        end += 4;
    } else if (svg_strncasecmp(end, "rad", 3) == 0) {
        v = v * (180.0 / M_PI);
        end += 3;
    } else if (svg_strncasecmp(end, "turn", 4) == 0) {
        v *= 360.0;
        end += 4;
    }
    *out_deg = (float)v;
    *p = end;
    return 1;
}

static int svg_parse_alpha_component(const char **p, float *out_alpha) {
    float v = 0.0f;
    int is_percent = 0;
    if (!svg_parse_color_component(p, &v, &is_percent)) {
        return 0;
    }
    float alpha = 0.0f;
    if (is_percent) {
        alpha = v / 100.0f;
    } else if (v <= 1.0f) {
        alpha = v;
    } else {
        alpha = v / 255.0f;
    }
    *out_alpha = svg_clamp01(alpha);
    return 1;
}

static int svg_parse_color(const char *s, uint32_t *out, int *is_none) {
    if (is_none) {
        *is_none = 0;
    }
    if (!s) {
        return 0;
    }
    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (!*s) {
        return 0;
    }
    if (svg_strcasecmp(s, "none") == 0) {
        if (is_none) {
            *is_none = 1;
        }
        return 1;
    }
    if (*s == '#') {
        s++;
        size_t len = strlen(s);
        if (len == 3 || len == 4) {
            int r = isxdigit((unsigned char)s[0]) ? (int)strtol((char[]){s[0], 0}, NULL, 16) : -1;
            int g = isxdigit((unsigned char)s[1]) ? (int)strtol((char[]){s[1], 0}, NULL, 16) : -1;
            int b = isxdigit((unsigned char)s[2]) ? (int)strtol((char[]){s[2], 0}, NULL, 16) : -1;
            int a = (len == 4) ? (isxdigit((unsigned char)s[3]) ? (int)strtol((char[]){s[3], 0}, NULL, 16) : -1) : 15;
            if (r < 0 || g < 0 || b < 0 || a < 0) {
                return 0;
            }
            r = r * 17;
            g = g * 17;
            b = b * 17;
            a = a * 17;
            *out = ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | (uint32_t)a;
            return 1;
        }
        if (len == 6 || len == 8) {
            unsigned long v = strtoul(s, NULL, 16);
            if (len == 6) {
                v = (v << 8) | 0xFFu;
            }
            *out = (uint32_t)v;
            return 1;
        }
        return 0;
    }
    if (svg_strncasecmp(s, "rgb(", 4) == 0 || svg_strncasecmp(s, "rgba(", 5) == 0) {
        int has_alpha_func = (tolower((unsigned char)s[3]) == 'a');
        const char *p = s + (has_alpha_func ? 5 : 4);
        float rgb[3] = {0, 0, 0};
        for (int i = 0; i < 3; i++) {
            float v = 0.0f;
            int percent = 0;
            if (!svg_parse_color_component(&p, &v, &percent)) {
                return 0;
            }
            if (percent) {
                v = v * 2.55f;
            }
            rgb[i] = v;
        }
        float alpha = 1.0f;
        svg_skip_separators(&p);
        if (*p == '/') {
            p++;
            if (!svg_parse_alpha_component(&p, &alpha)) {
                return 0;
            }
        } else if (has_alpha_func) {
            if (!svg_parse_alpha_component(&p, &alpha)) {
                return 0;
            }
        }
        int r = (int)lroundf(fmaxf(0.0f, fminf(255.0f, rgb[0])));
        int g = (int)lroundf(fmaxf(0.0f, fminf(255.0f, rgb[1])));
        int b = (int)lroundf(fmaxf(0.0f, fminf(255.0f, rgb[2])));
        int a = (int)lroundf(svg_clamp01(alpha) * 255.0f);
        *out = ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | (uint32_t)(a & 0xFF);
        return 1;
    }
    if (svg_strncasecmp(s, "hsl(", 4) == 0 || svg_strncasecmp(s, "hsla(", 5) == 0) {
        int has_alpha_func = (tolower((unsigned char)s[3]) == 'a');
        const char *p = s + (has_alpha_func ? 5 : 4);
        float h = 0.0f;
        if (!svg_parse_hue_component(&p, &h)) {
            return 0;
        }
        float s_comp = 0.0f;
        float l_comp = 0.0f;
        int s_percent = 0;
        int l_percent = 0;
        if (!svg_parse_color_component(&p, &s_comp, &s_percent)) {
            return 0;
        }
        if (!svg_parse_color_component(&p, &l_comp, &l_percent)) {
            return 0;
        }
        float sat = s_percent ? (s_comp / 100.0f) : s_comp;
        float lig = l_percent ? (l_comp / 100.0f) : l_comp;
        if (!s_percent && sat > 1.0f) {
            sat /= 100.0f;
        }
        if (!l_percent && lig > 1.0f) {
            lig /= 100.0f;
        }
        float alpha = 1.0f;
        svg_skip_separators(&p);
        if (*p == '/') {
            p++;
            if (!svg_parse_alpha_component(&p, &alpha)) {
                return 0;
            }
        } else if (has_alpha_func) {
            if (!svg_parse_alpha_component(&p, &alpha)) {
                return 0;
            }
        }
        float rf = 0.0f, gf = 0.0f, bf = 0.0f;
        svg_hsl_to_rgb(h, sat, lig, &rf, &gf, &bf);
        int r = (int)lroundf(svg_clamp01(rf) * 255.0f);
        int g = (int)lroundf(svg_clamp01(gf) * 255.0f);
        int b = (int)lroundf(svg_clamp01(bf) * 255.0f);
        int a = (int)lroundf(svg_clamp01(alpha) * 255.0f);
        *out = ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | (uint32_t)(a & 0xFF);
        return 1;
    }
    for (size_t i = 0; i < sizeof(svg_named_colors) / sizeof(svg_named_colors[0]); i++) {
        if (svg_strcasecmp(s, svg_named_colors[i].name) == 0) {
            *out = svg_named_colors[i].rgba;
            return 1;
        }
    }
    return 0;
}

static const svg_gradient *svg_defs_find_gradient(const svg_defs *defs, const char *id);
static const svg_pattern *svg_defs_find_pattern(const svg_defs *defs, const char *id);
static const svg_path_def *svg_defs_find_path(const svg_defs *defs, const char *id);
static const svg_symbol *svg_defs_find_symbol(const svg_defs *defs, const char *id);
static svg_marker *svg_defs_find_marker(svg_defs *defs, const char *id);
static svg_clip_path *svg_defs_find_clip(svg_defs *defs, const char *id);
static svg_mask *svg_defs_find_mask(svg_defs *defs, const char *id);
static const svg_filter *svg_defs_find_filter(const svg_defs *defs, const char *id);
static int svg_render(svg_render_ctx *ctx, const unsigned char *data, size_t size, char *err, size_t errcap,
                      const cupidimage_svg_options *opts);
static void svg_segments_free(svg_segments *segs);
static char *svg_text_normalize(const char *text, int preserve);
static float svg_measure_line(const svg_style *style, const char *text, size_t len);
static void svg_draw_samples_segments(svg_render_ctx *ctx, const svg_style *style, const svg_matrix *m,
                                      const svg_segments *segs);

static int svg_property_id(const char *name) {
    if (!name) {
        return -1;
    }
    if (strcmp(name, "fill") == 0) return SVG_PROP_FILL;
    if (strcmp(name, "stroke") == 0) return SVG_PROP_STROKE;
    if (strcmp(name, "stroke-width") == 0) return SVG_PROP_STROKE_WIDTH;
    if (strcmp(name, "stroke-linecap") == 0) return SVG_PROP_STROKE_LINECAP;
    if (strcmp(name, "stroke-linejoin") == 0) return SVG_PROP_STROKE_LINEJOIN;
    if (strcmp(name, "stroke-miterlimit") == 0) return SVG_PROP_STROKE_MITERLIMIT;
    if (strcmp(name, "stroke-dasharray") == 0) return SVG_PROP_STROKE_DASHARRAY;
    if (strcmp(name, "stroke-dashoffset") == 0) return SVG_PROP_STROKE_DASHOFFSET;
    if (strcmp(name, "clip-path") == 0) return SVG_PROP_CLIP_PATH;
    if (strcmp(name, "mask") == 0) return SVG_PROP_MASK;
    if (strcmp(name, "marker") == 0) return SVG_PROP_MARKER;
    if (strcmp(name, "marker-start") == 0) return SVG_PROP_MARKER_START;
    if (strcmp(name, "marker-mid") == 0) return SVG_PROP_MARKER_MID;
    if (strcmp(name, "marker-end") == 0) return SVG_PROP_MARKER_END;
    if (strcmp(name, "filter") == 0) return SVG_PROP_FILTER;
    if (strcmp(name, "opacity") == 0) return SVG_PROP_OPACITY;
    if (strcmp(name, "fill-opacity") == 0) return SVG_PROP_FILL_OPACITY;
    if (strcmp(name, "stroke-opacity") == 0) return SVG_PROP_STROKE_OPACITY;
    if (strcmp(name, "fill-rule") == 0) return SVG_PROP_FILL_RULE;
    if (strcmp(name, "color") == 0) return SVG_PROP_COLOR;
    if (strcmp(name, "font-size") == 0) return SVG_PROP_FONT_SIZE;
    if (strcmp(name, "text-anchor") == 0) return SVG_PROP_TEXT_ANCHOR;
    if (strcmp(name, "font-weight") == 0) return SVG_PROP_FONT_WEIGHT;
    if (strcmp(name, "font-style") == 0) return SVG_PROP_FONT_STYLE;
    if (strcmp(name, "dominant-baseline") == 0) return SVG_PROP_DOMINANT_BASELINE;
    if (strcmp(name, "alignment-baseline") == 0) return SVG_PROP_DOMINANT_BASELINE;
    if (strcmp(name, "letter-spacing") == 0) return SVG_PROP_LETTER_SPACING;
    if (strcmp(name, "word-spacing") == 0) return SVG_PROP_WORD_SPACING;
    if (strcmp(name, "display") == 0) return SVG_PROP_DISPLAY;
    if (strcmp(name, "visibility") == 0) return SVG_PROP_VISIBILITY;
    return -1;
}

static int svg_parse_url_id(const char *value, char *out, size_t outcap) {
    if (!value || !out || outcap == 0) {
        return 0;
    }
    const char *p = value;
    while (isspace((unsigned char)*p)) {
        p++;
    }
    if (svg_strncasecmp(p, "url(", 4) != 0) {
        return 0;
    }
    p += 4;
    while (isspace((unsigned char)*p)) {
        p++;
    }
    if (*p == '#') {
        p++;
    }
    const char *start = p;
    while (*p && *p != ')' && !isspace((unsigned char)*p)) {
        p++;
    }
    size_t len = (size_t)(p - start);
    if (len == 0 || len >= outcap) {
        return 0;
    }
    memcpy(out, start, len);
    out[len] = '\0';
    return 1;
}

static int svg_parse_href_id(const char *value, char *out, size_t outcap) {
    if (!value || !out || outcap == 0) {
        return 0;
    }
    const char *p = value;
    while (isspace((unsigned char)*p)) {
        p++;
    }
    if (!*p) {
        return 0;
    }
    if (*p == '#') {
        p++;
        const char *start = p;
        while (*p && !isspace((unsigned char)*p)) {
            p++;
        }
        size_t len = (size_t)(p - start);
        if (len == 0 || len >= outcap) {
            return 0;
        }
        memcpy(out, start, len);
        out[len] = '\0';
        return 1;
    }
    if (svg_parse_url_id(value, out, outcap)) {
        return 1;
    }
    const char *hash = strchr(p, '#');
    if (hash && hash[1]) {
        hash++;
        const char *start = hash;
        while (*hash && !isspace((unsigned char)*hash)) {
            hash++;
        }
        size_t len = (size_t)(hash - start);
        if (len == 0 || len >= outcap) {
            return 0;
        }
        memcpy(out, start, len);
        out[len] = '\0';
        return 1;
    }
    return 0;
}

static int svg_should_skip_use_wrapper_attr(const char *name) {
    if (!name) {
        return 1;
    }
    const char *local = svg_local_name(name);
    if (!local) {
        return 1;
    }
    return strcmp(local, "href") == 0 || strcmp(local, "x") == 0 || strcmp(local, "y") == 0 ||
           strcmp(local, "width") == 0 || strcmp(local, "height") == 0 ||
           strcmp(local, "transform") == 0;
}

static char *svg_build_use_wrapper_attrs(const svg_tag *tag) {
    if (!tag) {
        return NULL;
    }
    size_t cap = 256;
    size_t len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        return NULL;
    }
    buf[0] = '\0';
    for (int i = 0; i < tag->attr_count; i++) {
        const char *name = tag->attrs[i].name;
        const char *value = tag->attrs[i].value ? tag->attrs[i].value : "";
        if (svg_should_skip_use_wrapper_attr(name)) {
            continue;
        }
        size_t need = strlen(name) + strlen(value) + 4u;
        if (len + need + 1u > cap) {
            size_t new_cap = cap * 2u;
            while (len + need + 1u > new_cap) {
                new_cap *= 2u;
            }
            char *n = (char *)realloc(buf, new_cap);
            if (!n) {
                free(buf);
                return NULL;
            }
            buf = n;
            cap = new_cap;
        }
        int n = snprintf(buf + len, cap - len, " %s=\"%s\"", name, value);
        if (n < 0) {
            free(buf);
            return NULL;
        }
        len += (size_t)n;
    }
    return buf;
}

static char *svg_wrap_with_group_attrs(const char *content, const char *attrs) {
    if (!content) {
        return NULL;
    }
    if (!attrs || !*attrs) {
        return svg_strdup(content);
    }
    const char *prefix = "<g";
    const char *mid = ">";
    const char *suffix = "</g>";
    size_t clen = strlen(content);
    size_t alen = strlen(attrs);
    size_t total = strlen(prefix) + alen + strlen(mid) + clen + strlen(suffix) + 1u;
    char *out = (char *)malloc(total);
    if (!out) {
        return NULL;
    }
    snprintf(out, total, "<g%s>%s</g>", attrs, content);
    return out;
}

static void svg_style_apply_property_id(svg_style *style, int prop, const char *value, float base_len,
                                        float dpi, const svg_defs *defs) {
    if (!style || !value) {
        return;
    }
    if (prop == SVG_PROP_FILL || prop == SVG_PROP_STROKE) {
        svg_paint_type *ptype = (prop == SVG_PROP_FILL) ? &style->fill_type : &style->stroke_type;
        uint32_t *pcolor = (prop == SVG_PROP_FILL) ? &style->fill_color : &style->stroke_color;
        const svg_gradient **pgrad = (prop == SVG_PROP_FILL) ? &style->fill_gradient : &style->stroke_gradient;
        svg_pattern **ppat = (prop == SVG_PROP_FILL) ? &style->fill_pattern : &style->stroke_pattern;
        if (svg_strcasecmp(value, "currentcolor") == 0) {
            *ptype = SVG_PAINT_COLOR;
            *pcolor = style->color;
            *pgrad = NULL;
            *ppat = NULL;
            return;
        }
        char url_id[96];
        if (svg_parse_url_id(value, url_id, sizeof(url_id))) {
            const svg_gradient *grad = defs ? svg_defs_find_gradient(defs, url_id) : NULL;
            if (grad) {
                *ptype = grad->type;
                *pgrad = grad;
                *ppat = NULL;
            } else {
                const svg_pattern *pat = defs ? svg_defs_find_pattern(defs, url_id) : NULL;
                if (pat) {
                    *ptype = SVG_PAINT_PATTERN;
                    *ppat = (svg_pattern *)pat;
                    *pgrad = NULL;
                } else {
                    *ptype = SVG_PAINT_NONE;
                    *pgrad = NULL;
                    *ppat = NULL;
                }
            }
            return;
        }
        int is_none = 0;
        uint32_t color = 0;
        if (svg_parse_color(value, &color, &is_none)) {
            if (is_none) {
                *ptype = SVG_PAINT_NONE;
                *pgrad = NULL;
                *ppat = NULL;
            } else {
                *ptype = SVG_PAINT_COLOR;
                *pcolor = color;
                *pgrad = NULL;
                *ppat = NULL;
            }
        }
        return;
    }
    if (prop == SVG_PROP_STROKE_WIDTH) {
        int ok = 0;
        float v = svg_parse_length(value, base_len, dpi, &ok);
        if (ok) {
            if (v < 0.0f) {
                v = 0.0f;
            }
            style->stroke_width = v;
        }
        return;
    }
    if (prop == SVG_PROP_STROKE_LINECAP) {
        if (svg_strcasecmp(value, "round") == 0) {
            style->stroke_linecap = SVG_LINECAP_ROUND;
        } else if (svg_strcasecmp(value, "square") == 0) {
            style->stroke_linecap = SVG_LINECAP_SQUARE;
        } else if (svg_strcasecmp(value, "butt") == 0) {
            style->stroke_linecap = SVG_LINECAP_BUTT;
        }
        return;
    }
    if (prop == SVG_PROP_STROKE_LINEJOIN) {
        if (svg_strcasecmp(value, "round") == 0) {
            style->stroke_linejoin = SVG_LINEJOIN_ROUND;
        } else if (svg_strcasecmp(value, "bevel") == 0) {
            style->stroke_linejoin = SVG_LINEJOIN_BEVEL;
        } else if (svg_strcasecmp(value, "miter") == 0) {
            style->stroke_linejoin = SVG_LINEJOIN_MITER;
        }
        return;
    }
    if (prop == SVG_PROP_STROKE_MITERLIMIT) {
        char *end = NULL;
        double v = strtod(value, &end);
        if (end != value) {
            if (v < 1.0) v = 1.0;
            style->stroke_miterlimit = (float)v;
        }
        return;
    }
    if (prop == SVG_PROP_STROKE_DASHARRAY) {
        if (svg_strcasecmp(value, "none") == 0) {
            style->stroke_dashcount = 0;
            return;
        }
        const char *p = value;
        int count = 0;
        while (*p) {
            float v = 0.0f;
            if (!svg_parse_length_value(&p, base_len, dpi, &v)) {
                break;
            }
            if (v < 0.0f) {
                v = 0.0f;
            }
            if (count < SVG_MAX_DASH) {
                style->stroke_dasharray[count++] = v;
            }
            svg_skip_separators(&p);
            if (!*p) {
                break;
            }
            if (*p == ',') {
                p++;
            }
        }
        if (count > 0 && (count & 1)) {
            int orig = count;
            for (int i = 0; i < orig && count < SVG_MAX_DASH; i++) {
                style->stroke_dasharray[count++] = style->stroke_dasharray[i];
            }
        }
        float sum = 0.0f;
        for (int i = 0; i < count; i++) {
            sum += style->stroke_dasharray[i];
        }
        style->stroke_dashcount = (sum > 0.0f) ? count : 0;
        return;
    }
    if (prop == SVG_PROP_STROKE_DASHOFFSET) {
        int ok = 0;
        float v = svg_parse_length(value, base_len, dpi, &ok);
        if (ok) {
            style->stroke_dashoffset = v;
        }
        return;
    }
    if (prop == SVG_PROP_CLIP_PATH) {
        if (svg_strcasecmp(value, "none") == 0) {
            style->clip_path = NULL;
            return;
        }
        char url_id[96];
        if (svg_parse_url_id(value, url_id, sizeof(url_id))) {
            style->clip_path = defs ? svg_defs_find_clip((svg_defs *)defs, url_id) : NULL;
        }
        return;
    }
    if (prop == SVG_PROP_MASK) {
        if (svg_strcasecmp(value, "none") == 0) {
            style->mask = NULL;
            return;
        }
        char url_id[96];
        if (svg_parse_url_id(value, url_id, sizeof(url_id))) {
            style->mask = defs ? svg_defs_find_mask((svg_defs *)defs, url_id) : NULL;
        }
        return;
    }
    if (prop == SVG_PROP_MARKER || prop == SVG_PROP_MARKER_START ||
        prop == SVG_PROP_MARKER_MID || prop == SVG_PROP_MARKER_END) {
        svg_marker *mk = NULL;
        if (svg_strcasecmp(value, "none") != 0) {
            char url_id[96];
            if (svg_parse_url_id(value, url_id, sizeof(url_id))) {
                mk = defs ? svg_defs_find_marker((svg_defs *)defs, url_id) : NULL;
            }
        }
        if (prop == SVG_PROP_MARKER) {
            style->marker_start = mk;
            style->marker_mid = mk;
            style->marker_end = mk;
        } else if (prop == SVG_PROP_MARKER_START) {
            style->marker_start = mk;
        } else if (prop == SVG_PROP_MARKER_MID) {
            style->marker_mid = mk;
        } else {
            style->marker_end = mk;
        }
        return;
    }
    if (prop == SVG_PROP_FILTER) {
        if (svg_strcasecmp(value, "none") == 0) {
            style->filter = NULL;
            return;
        }
        char url_id[96];
        if (svg_parse_url_id(value, url_id, sizeof(url_id))) {
            style->filter = defs ? svg_defs_find_filter(defs, url_id) : NULL;
        }
        return;
    }
    if (prop == SVG_PROP_OPACITY) {
        float o = 0.0f;
        if (svg_parse_opacity_value(value, &o)) {
            style->opacity = o;
        }
        return;
    }
    if (prop == SVG_PROP_FILL_OPACITY) {
        float o = 0.0f;
        if (svg_parse_opacity_value(value, &o)) {
            style->fill_opacity = o;
        }
        return;
    }
    if (prop == SVG_PROP_STROKE_OPACITY) {
        float o = 0.0f;
        if (svg_parse_opacity_value(value, &o)) {
            style->stroke_opacity = o;
        }
        return;
    }
    if (prop == SVG_PROP_FILL_RULE) {
        if (svg_strcasecmp(value, "evenodd") == 0) {
            style->fill_rule_evenodd = 1;
        } else if (svg_strcasecmp(value, "nonzero") == 0) {
            style->fill_rule_evenodd = 0;
        }
        return;
    }
    if (prop == SVG_PROP_COLOR) {
        uint32_t color = 0;
        int is_none = 0;
        if (svg_parse_color(value, &color, &is_none) && !is_none) {
            style->color = color;
        }
        return;
    }
    if (prop == SVG_PROP_FONT_SIZE) {
        int ok = 0;
        float v = svg_parse_length(value, base_len, dpi, &ok);
        if (ok && v > 0.0f) {
            style->font_size = v;
        }
        return;
    }
    if (prop == SVG_PROP_TEXT_ANCHOR) {
        if (svg_strcasecmp(value, "middle") == 0) {
            style->text_anchor = 1;
        } else if (svg_strcasecmp(value, "end") == 0) {
            style->text_anchor = 2;
        } else if (svg_strcasecmp(value, "start") == 0) {
            style->text_anchor = 0;
        }
        return;
    }
    if (prop == SVG_PROP_FONT_WEIGHT) {
        if (svg_strcasecmp(value, "bold") == 0) {
            style->bold = 1;
        } else if (isdigit((unsigned char)value[0])) {
            int weight = (int)strtol(value, NULL, 10);
            style->bold = weight >= 600;
        } else {
            style->bold = 0;
        }
        return;
    }
    if (prop == SVG_PROP_FONT_STYLE) {
        if (svg_strcasecmp(value, "italic") == 0 || svg_strcasecmp(value, "oblique") == 0) {
            style->italic = 1;
        } else if (svg_strcasecmp(value, "normal") == 0) {
            style->italic = 0;
        }
        return;
    }
    if (prop == SVG_PROP_DOMINANT_BASELINE) {
        if (svg_strcasecmp(value, "middle") == 0 || svg_strcasecmp(value, "central") == 0) {
            style->dominant_baseline = 1;
        } else if (svg_strcasecmp(value, "hanging") == 0 ||
                   svg_strcasecmp(value, "text-before-edge") == 0 ||
                   svg_strcasecmp(value, "before-edge") == 0) {
            style->dominant_baseline = 2;
        } else if (svg_strcasecmp(value, "alphabetic") == 0 ||
                   svg_strcasecmp(value, "auto") == 0 ||
                   svg_strcasecmp(value, "baseline") == 0 ||
                   svg_strcasecmp(value, "text-after-edge") == 0 ||
                   svg_strcasecmp(value, "after-edge") == 0) {
            style->dominant_baseline = 0;
        }
        return;
    }
    if (prop == SVG_PROP_LETTER_SPACING) {
        int ok = 0;
        float v = svg_parse_length(value, base_len, dpi, &ok);
        if (ok) {
            style->letter_spacing = v;
        }
        return;
    }
    if (prop == SVG_PROP_WORD_SPACING) {
        int ok = 0;
        float v = svg_parse_length(value, base_len, dpi, &ok);
        if (ok) {
            style->word_spacing = v;
        }
        return;
    }
    if (prop == SVG_PROP_DISPLAY) {
        if (svg_strcasecmp(value, "none") == 0) {
            style->display_none = 1;
        } else {
            style->display_none = 0;
        }
        return;
    }
    if (prop == SVG_PROP_VISIBILITY) {
        if (svg_strcasecmp(value, "hidden") == 0 || svg_strcasecmp(value, "collapse") == 0) {
            style->visibility_hidden = 1;
        } else if (svg_strcasecmp(value, "visible") == 0) {
            style->visibility_hidden = 0;
        }
        return;
    }
}

static void svg_style_apply_property(svg_style *style, const char *name, const char *value, float base_len,
                                     float dpi, const svg_defs *defs) {
    if (!style || !name || !value) {
        return;
    }
    int prop = svg_property_id(name);
    if (prop < 0) {
        return;
    }
    svg_style_apply_property_id(style, prop, value, base_len, dpi, defs);
}

static void svg_style_apply_style_attr(svg_style *style, const char *value, float base_len, float dpi,
                                       const svg_defs *defs) {
    if (!style || !value) {
        return;
    }
    const char *p = value;
    while (*p) {
        while (isspace((unsigned char)*p) || *p == ';') {
            p++;
        }
        if (!*p) {
            break;
        }
        const char *name_start = p;
        while (*p && *p != ':' && *p != ';') {
            p++;
        }
        if (*p != ':') {
            break;
        }
        const char *name_end = p;
        p++;
        const char *value_start = p;
        while (*p && *p != ';') {
            p++;
        }
        const char *value_end = p;
        while (name_end > name_start && isspace((unsigned char)name_end[-1])) {
            name_end--;
        }
        while (value_end > value_start && isspace((unsigned char)value_end[-1])) {
            value_end--;
        }
        if (name_end > name_start && value_end > value_start) {
            char name_buf[64];
            size_t nlen = (size_t)(name_end - name_start);
            if (nlen >= sizeof(name_buf)) {
                nlen = sizeof(name_buf) - 1;
            }
            memcpy(name_buf, name_start, nlen);
            name_buf[nlen] = '\0';
            svg_lowercase(name_buf);
            char value_buf[128];
            size_t vlen = (size_t)(value_end - value_start);
            if (vlen >= sizeof(value_buf)) {
                vlen = sizeof(value_buf) - 1;
            }
            memcpy(value_buf, value_start, vlen);
            value_buf[vlen] = '\0';
            svg_style_apply_property(style, name_buf, value_buf, base_len, dpi, defs);
        }
        if (*p == ';') {
            p++;
        }
    }
}

static void svg_blend_premul(uint8_t *dst, uint8_t sr, uint8_t sg, uint8_t sb, uint8_t sa) {
    uint32_t inv = 255u - sa;
    dst[0] = (uint8_t)(sr + (uint32_t)dst[0] * inv / 255u);
    dst[1] = (uint8_t)(sg + (uint32_t)dst[1] * inv / 255u);
    dst[2] = (uint8_t)(sb + (uint32_t)dst[2] * inv / 255u);
    dst[3] = (uint8_t)(sa + (uint32_t)dst[3] * inv / 255u);
}

static void svg_apply_presentation_attrs(svg_style *style, const svg_tag *tag, float base_len, float dpi,
                                         const svg_defs *defs) {
    for (int i = 0; i < tag->attr_count; i++) {
        const char *name = svg_local_name(tag->attrs[i].name);
        const char *value = tag->attrs[i].value;
        if (!name || !value) {
            continue;
        }
        if (strcmp(name, "style") == 0) {
            continue;
        }
        svg_style_apply_property(style, name, value, base_len, dpi, defs);
    }
}

static void svg_apply_inline_style_attr(svg_style *style, const svg_tag *tag, float base_len, float dpi,
                                        const svg_defs *defs) {
    const char *style_attr = svg_get_attr(tag, "style");
    if (style_attr) {
        svg_style_apply_style_attr(style, style_attr, base_len, dpi, defs);
    }
}

static int svg_parse_transform(const char *value, svg_matrix *out) {
    if (!value || !out) {
        return 0;
    }
    svg_matrix m;
    svg_matrix_identity(&m);
    const char *p = value;
    while (*p) {
        while (isspace((unsigned char)*p) || *p == ',') {
            p++;
        }
        if (!*p) {
            break;
        }
        if (strncmp(p, "matrix", 6) == 0) {
            p += 6;
            while (*p && *p != '(') p++;
            if (*p != '(') break;
            p++;
            float v[6];
            for (int i = 0; i < 6; i++) {
                if (!svg_parse_transform_number(&p, &v[i])) {
                    return 0;
                }
            }
            svg_matrix t = {v[0], v[1], v[2], v[3], v[4], v[5]};
            svg_matrix_multiply(&t, &m, &m);
        } else if (strncmp(p, "translate", 9) == 0) {
            p += 9;
            while (*p && *p != '(') p++;
            if (*p != '(') break;
            p++;
            float tx = 0.0f, ty = 0.0f;
            svg_parse_transform_number(&p, &tx);
            if (!svg_parse_transform_number(&p, &ty)) {
                ty = 0.0f;
            }
            svg_matrix t;
            svg_matrix_identity(&t);
            t.e = tx;
            t.f = ty;
            svg_matrix_multiply(&t, &m, &m);
        } else if (strncmp(p, "scale", 5) == 0) {
            p += 5;
            while (*p && *p != '(') p++;
            if (*p != '(') break;
            p++;
            float sx = 1.0f, sy = 1.0f;
            svg_parse_transform_number(&p, &sx);
            if (!svg_parse_transform_number(&p, &sy)) {
                sy = sx;
            }
            svg_matrix t;
            svg_matrix_identity(&t);
            t.a = sx;
            t.d = sy;
            svg_matrix_multiply(&t, &m, &m);
        } else if (strncmp(p, "rotate", 6) == 0) {
            p += 6;
            while (*p && *p != '(') p++;
            if (*p != '(') break;
            p++;
            float angle = 0.0f;
            if (!svg_parse_transform_number(&p, &angle)) {
                return 0;
            }
            float cx = 0.0f, cy = 0.0f;
            int has_center = svg_parse_transform_number(&p, &cx);
            if (has_center) {
                if (!svg_parse_transform_number(&p, &cy)) {
                    cy = 0.0f;
                }
            }
            svg_matrix t;
            svg_matrix_identity(&t);
            if (has_center) {
                svg_matrix_translate(&t, cx, cy);
                svg_matrix_rotate(&t, angle);
                svg_matrix_translate(&t, -cx, -cy);
            } else {
                svg_matrix_rotate(&t, angle);
            }
            svg_matrix_multiply(&t, &m, &m);
        } else if (strncmp(p, "skewx", 5) == 0) {
            p += 5;
            while (*p && *p != '(') p++;
            if (*p != '(') break;
            p++;
            float angle = 0.0f;
            if (!svg_parse_transform_number(&p, &angle)) {
                return 0;
            }
            svg_matrix t;
            svg_matrix_identity(&t);
            svg_matrix_skewx(&t, angle);
            svg_matrix_multiply(&t, &m, &m);
        } else if (strncmp(p, "skewy", 5) == 0) {
            p += 5;
            while (*p && *p != '(') p++;
            if (*p != '(') break;
            p++;
            float angle = 0.0f;
            if (!svg_parse_transform_number(&p, &angle)) {
                return 0;
            }
            svg_matrix t;
            svg_matrix_identity(&t);
            svg_matrix_skewy(&t, angle);
            svg_matrix_multiply(&t, &m, &m);
        } else {
            break;
        }
        while (*p && *p != ')') p++;
        if (*p == ')') {
            p++;
        }
    }
    *out = m;
    return 1;
}

static int svg_next_tag(char **p, svg_tag *tag) {
    char *s = *p;
    while (s && *s) {
        char *lt = strchr(s, '<');
        if (!lt) {
            *p = s + strlen(s);
            return 0;
        }
        s = lt + 1;
        if (*s == '!') {
            if (strncmp(s, "!--", 3) == 0) {
                char *end = strstr(s + 3, "-->");
                if (!end) {
                    *p = s + strlen(s);
                    return 0;
                }
                s = end + 3;
                continue;
            }
            if (strncmp(s, "![CDATA[", 8) == 0) {
                char *end = strstr(s + 8, "]]>");
                if (!end) {
                    *p = s + strlen(s);
                    return 0;
                }
                s = end + 3;
                continue;
            }
            char *end = strchr(s, '>');
            if (!end) {
                *p = s + strlen(s);
                return 0;
            }
            s = end + 1;
            continue;
        }
        if (*s == '?') {
            char *end = strstr(s, "?>");
            if (!end) {
                *p = s + strlen(s);
                return 0;
            }
            s = end + 2;
            continue;
        }
        break;
    }
    if (!s || !*s) {
        *p = s;
        return 0;
    }
    memset(tag, 0, sizeof(*tag));
    if (*s == '/') {
        tag->is_end = 1;
        s++;
    }
    while (isspace((unsigned char)*s)) {
        s++;
    }
    char *name_start = s;
    while (*s && svg_is_name_char(*s)) {
        s++;
    }
    if (s == name_start) {
        *p = s;
        return 0;
    }
    char *name_end = s;
    if (tag->is_end) {
        char *end = name_end;
        while (*end && *end != '>') {
            end++;
        }
        if (!*end) {
            *p = end;
            return 0;
        }
        *name_end = '\0';
        svg_lowercase(name_start);
        tag->name = name_start;
        *p = end + 1;
        return 1;
    }
    if (*s) {
        *s++ = '\0';
    }
    svg_lowercase(name_start);
    tag->name = name_start;
    while (*s) {
        while (isspace((unsigned char)*s)) {
            s++;
        }
        if (*s == '/' || *s == '>') {
            break;
        }
        if (!*s) {
            break;
        }
        if (tag->attr_count >= SVG_MAX_ATTR) {
            while (*s && *s != '>' && *s != '/') {
                s++;
            }
            break;
        }
        char *attr_name = s;
        while (*s && svg_is_name_char(*s)) {
            s++;
        }
        if (s == attr_name) {
            break;
        }
        char *attr_name_end = s;
        int had_eq = (*s == '=');
        if (*s) {
            *s++ = '\0';
        }
        svg_lowercase(attr_name);
        while (isspace((unsigned char)*s)) {
            s++;
        }
        char *attr_value = (char *)"";
        if (had_eq || *s == '=') {
            if (!had_eq) {
                s++;
            }
            while (isspace((unsigned char)*s)) {
                s++;
            }
            if (*s == '"' || *s == '\'') {
                char quote = *s++;
                attr_value = s;
                while (*s && *s != quote) {
                    s++;
                }
                if (*s) {
                    *s++ = '\0';
                }
            } else {
                attr_value = s;
                while (*s && !isspace((unsigned char)*s) && *s != '>' && *s != '/') {
                    s++;
                }
                if (*s) {
                    *s++ = '\0';
                }
            }
        }
        tag->attrs[tag->attr_count].name = attr_name;
        tag->attrs[tag->attr_count].value = attr_value;
        tag->attr_count++;
        if (s == attr_name_end) {
            break;
        }
    }
    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '/') {
        tag->is_self_closing = 1;
        s++;
    }
    if (*s == '>') {
        s++;
    }
    *p = s;
    return 1;
}

static void svg_defs_init(svg_defs *defs) {
    if (!defs) {
        return;
    }
    defs->gradients = NULL;
    defs->gradient_count = 0;
    defs->gradient_cap = 0;
    defs->patterns = NULL;
    defs->pattern_count = 0;
    defs->pattern_cap = 0;
    defs->paths = NULL;
    defs->path_count = 0;
    defs->path_cap = 0;
    defs->uses = NULL;
    defs->use_count = 0;
    defs->use_cap = 0;
    defs->symbols = NULL;
    defs->symbol_count = 0;
    defs->symbol_cap = 0;
    defs->markers = NULL;
    defs->marker_count = 0;
    defs->marker_cap = 0;
    defs->clips = NULL;
    defs->clip_count = 0;
    defs->clip_cap = 0;
    defs->masks = NULL;
    defs->mask_count = 0;
    defs->mask_cap = 0;
    defs->filters = NULL;
    defs->filter_count = 0;
    defs->filter_cap = 0;
}

static void svg_defs_free(svg_defs *defs) {
    if (!defs) {
        return;
    }
    for (int i = 0; i < defs->gradient_count; i++) {
        free(defs->gradients[i].id);
        free(defs->gradients[i].href);
        free(defs->gradients[i].stops);
    }
    for (int i = 0; i < defs->pattern_count; i++) {
        free(defs->patterns[i].id);
        free(defs->patterns[i].href);
        free(defs->patterns[i].content);
        free(defs->patterns[i].rgba);
    }
    for (int i = 0; i < defs->path_count; i++) {
        free(defs->paths[i].id);
        free(defs->paths[i].d);
    }
    for (int i = 0; i < defs->use_count; i++) {
        free(defs->uses[i].id);
        free(defs->uses[i].tag_name);
        for (int j = 0; j < defs->uses[i].attr_count; j++) {
            free(defs->uses[i].attrs[j].name);
            free(defs->uses[i].attrs[j].value);
        }
    }
    for (int i = 0; i < defs->symbol_count; i++) {
        free(defs->symbols[i].id);
        free(defs->symbols[i].content);
    }
    for (int i = 0; i < defs->marker_count; i++) {
        free(defs->markers[i].id);
        free(defs->markers[i].content);
    }
    for (int i = 0; i < defs->clip_count; i++) {
        free(defs->clips[i].id);
        free(defs->clips[i].href);
        free(defs->clips[i].content);
        free(defs->clips[i].rgba);
    }
    for (int i = 0; i < defs->mask_count; i++) {
        free(defs->masks[i].id);
        free(defs->masks[i].href);
        free(defs->masks[i].content);
        free(defs->masks[i].rgba);
    }
    for (int i = 0; i < defs->filter_count; i++) {
        free(defs->filters[i].id);
        free(defs->filters[i].content);
    }
    free(defs->gradients);
    defs->gradients = NULL;
    defs->gradient_count = 0;
    defs->gradient_cap = 0;
    free(defs->patterns);
    defs->patterns = NULL;
    defs->pattern_count = 0;
    defs->pattern_cap = 0;
    free(defs->paths);
    defs->paths = NULL;
    defs->path_count = 0;
    defs->path_cap = 0;
    free(defs->uses);
    defs->uses = NULL;
    defs->use_count = 0;
    defs->use_cap = 0;
    free(defs->symbols);
    defs->symbols = NULL;
    defs->symbol_count = 0;
    defs->symbol_cap = 0;
    free(defs->markers);
    defs->markers = NULL;
    defs->marker_count = 0;
    defs->marker_cap = 0;
    free(defs->clips);
    defs->clips = NULL;
    defs->clip_count = 0;
    defs->clip_cap = 0;
    free(defs->masks);
    defs->masks = NULL;
    defs->mask_count = 0;
    defs->mask_cap = 0;
    free(defs->filters);
    defs->filters = NULL;
    defs->filter_count = 0;
    defs->filter_cap = 0;
}

static void svg_css_init(svg_css *css) {
    if (!css) {
        return;
    }
    css->rules = NULL;
    css->rule_count = 0;
    css->rule_cap = 0;
    css->keyframes = NULL;
    css->keyframe_count = 0;
    css->keyframe_cap = 0;
    css->animations = NULL;
    css->animation_count = 0;
    css->animation_cap = 0;
}

static void svg_css_free(svg_css *css) {
    if (!css) {
        return;
    }
    for (int i = 0; i < css->rule_count; i++) {
        svg_css_rule *rule = &css->rules[i];
        for (int s = 0; s < rule->selector_count; s++) {
            svg_selector *sel = &rule->selectors[s];
            for (int p = 0; p < sel->part_count; p++) {
                svg_selector_part *part = &sel->parts[p];
                free(part->tag);
                free(part->id);
                for (int c = 0; c < part->class_count; c++) {
                    free(part->classes[c]);
                }
                free(part->classes);
                for (int a = 0; a < part->attr_count; a++) {
                    free(part->attr_names[a]);
                    free(part->attr_values[a]);
                }
                free(part->attr_names);
                free(part->attr_values);
                free(part->attr_has_value);
            }
            free(sel->parts);
            free(sel->combinators);
        }
        free(rule->selectors);
        for (int d = 0; d < rule->decl_count; d++) {
            free(rule->decls[d].value);
        }
        free(rule->decls);
    }
    free(css->rules);
    css->rules = NULL;
    css->rule_count = 0;
    css->rule_cap = 0;
    for (int i = 0; i < css->keyframe_count; i++) {
        svg_css_keyframes *kf = &css->keyframes[i];
        free(kf->name);
        for (int k = 0; k < kf->step_count; k++) {
            free(kf->steps[k].transform);
        }
        free(kf->steps);
    }
    free(css->keyframes);
    css->keyframes = NULL;
    css->keyframe_count = 0;
    css->keyframe_cap = 0;
    for (int i = 0; i < css->animation_count; i++) {
        free(css->animations[i].target_id);
        free(css->animations[i].name);
    }
    free(css->animations);
    css->animations = NULL;
    css->animation_count = 0;
    css->animation_cap = 0;
}

static svg_gradient *svg_defs_add_gradient(svg_defs *defs, const char *id, svg_paint_type type) {
    if (!defs || !id || !*id) {
        return NULL;
    }
    if (defs->gradient_count >= defs->gradient_cap) {
        int new_cap = defs->gradient_cap ? defs->gradient_cap * 2 : 16;
        svg_gradient *n = (svg_gradient *)realloc(defs->gradients, (size_t)new_cap * sizeof(svg_gradient));
        if (!n) {
            return NULL;
        }
        defs->gradients = n;
        defs->gradient_cap = new_cap;
    }
    svg_gradient *g = &defs->gradients[defs->gradient_count++];
    memset(g, 0, sizeof(*g));
    g->id = svg_strdup(id);
    g->type = type;
    g->units = SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX;
    g->spread = SVG_SPREAD_PAD;
    svg_matrix_identity(&g->transform);
    svg_matrix_identity(&g->inv_transform);
    g->has_transform = 0;
    g->has_units = 0;
    g->has_spread = 0;
    g->has_coords = 0;
    g->has_focal = 0;
    if (type == SVG_PAINT_LINEAR_GRADIENT) {
        g->x1.value = 0.0f;
        g->y1.value = 0.0f;
        g->x2.value = 1.0f;
        g->y2.value = 0.0f;
        g->x1.is_percent = 1;
        g->y1.is_percent = 1;
        g->x2.is_percent = 1;
        g->y2.is_percent = 1;
    } else {
        g->cx.value = 0.5f;
        g->cy.value = 0.5f;
        g->r.value = 0.5f;
        g->fx.value = 0.5f;
        g->fy.value = 0.5f;
        g->cx.is_percent = 1;
        g->cy.is_percent = 1;
        g->r.is_percent = 1;
        g->fx.is_percent = 1;
        g->fy.is_percent = 1;
    }
    return g;
}

static const svg_gradient *svg_defs_find_gradient(const svg_defs *defs, const char *id) {
    if (!defs || !id) {
        return NULL;
    }
    for (int i = 0; i < defs->gradient_count; i++) {
        if (defs->gradients[i].id && strcmp(defs->gradients[i].id, id) == 0) {
            return &defs->gradients[i];
        }
    }
    return NULL;
}

static svg_pattern *svg_defs_add_pattern(svg_defs *defs, const char *id) {
    if (!defs || !id || !*id) {
        return NULL;
    }
    if (defs->pattern_count >= defs->pattern_cap) {
        int new_cap = defs->pattern_cap ? defs->pattern_cap * 2 : 16;
        svg_pattern *n = (svg_pattern *)realloc(defs->patterns, (size_t)new_cap * sizeof(svg_pattern));
        if (!n) {
            return NULL;
        }
        defs->patterns = n;
        defs->pattern_cap = new_cap;
    }
    svg_pattern *p = &defs->patterns[defs->pattern_count++];
    memset(p, 0, sizeof(*p));
    p->id = svg_strdup(id);
    p->units = SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX;
    p->content_units = SVG_GRADIENT_UNITS_USER_SPACE;
    p->has_units = 0;
    p->has_content_units = 0;
    p->has_coords = 0;
    p->x.value = 0.0f;
    p->y.value = 0.0f;
    p->width.value = 0.0f;
    p->height.value = 0.0f;
    p->x.is_percent = 1;
    p->y.is_percent = 1;
    p->width.is_percent = 1;
    p->height.is_percent = 1;
    svg_matrix_identity(&p->transform);
    svg_matrix_identity(&p->inv_transform);
    p->has_transform = 0;
    p->content = NULL;
    p->rgba = NULL;
    p->tile_w = 0;
    p->tile_h = 0;
    p->cached_bbox_x = 0.0f;
    p->cached_bbox_y = 0.0f;
    p->cached_bbox_w = 0.0f;
    p->cached_bbox_h = 0.0f;
    p->has_cached_bbox = 0;
    p->rendered = 0;
    p->rendering = 0;
    return p;
}

static const svg_pattern *svg_defs_find_pattern(const svg_defs *defs, const char *id) {
    if (!defs || !id) {
        return NULL;
    }
    for (int i = 0; i < defs->pattern_count; i++) {
        if (defs->patterns[i].id && strcmp(defs->patterns[i].id, id) == 0) {
            return &defs->patterns[i];
        }
    }
    return NULL;
}

static svg_clip_path *svg_defs_add_clip(svg_defs *defs, const char *id) {
    if (!defs || !id || !*id) {
        return NULL;
    }
    if (defs->clip_count >= defs->clip_cap) {
        int new_cap = defs->clip_cap ? defs->clip_cap * 2 : 16;
        svg_clip_path *n = (svg_clip_path *)realloc(defs->clips, (size_t)new_cap * sizeof(svg_clip_path));
        if (!n) {
            return NULL;
        }
        defs->clips = n;
        defs->clip_cap = new_cap;
    }
    svg_clip_path *c = &defs->clips[defs->clip_count++];
    memset(c, 0, sizeof(*c));
    c->id = svg_strdup(id);
    c->units = SVG_GRADIENT_UNITS_USER_SPACE;
    c->has_units = 0;
    svg_matrix_identity(&c->transform);
    svg_matrix_identity(&c->inv_transform);
    c->has_transform = 0;
    c->content = NULL;
    c->rgba = NULL;
    c->mask_w = 0;
    c->mask_h = 0;
    c->cached_bbox_x = 0.0f;
    c->cached_bbox_y = 0.0f;
    c->cached_bbox_w = 0.0f;
    c->cached_bbox_h = 0.0f;
    c->has_cached_bbox = 0;
    c->rendered = 0;
    c->rendering = 0;
    return c;
}

static svg_mask *svg_defs_add_mask(svg_defs *defs, const char *id) {
    if (!defs || !id || !*id) {
        return NULL;
    }
    if (defs->mask_count >= defs->mask_cap) {
        int new_cap = defs->mask_cap ? defs->mask_cap * 2 : 16;
        svg_mask *n = (svg_mask *)realloc(defs->masks, (size_t)new_cap * sizeof(svg_mask));
        if (!n) {
            return NULL;
        }
        defs->masks = n;
        defs->mask_cap = new_cap;
    }
    svg_mask *m = &defs->masks[defs->mask_count++];
    memset(m, 0, sizeof(*m));
    m->id = svg_strdup(id);
    m->units = SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX;
    m->content_units = SVG_GRADIENT_UNITS_USER_SPACE;
    m->has_units = 0;
    m->has_content_units = 0;
    m->has_coords = 0;
    m->x.value = -0.1f;
    m->y.value = -0.1f;
    m->width.value = 1.2f;
    m->height.value = 1.2f;
    m->x.is_percent = 1;
    m->y.is_percent = 1;
    m->width.is_percent = 1;
    m->height.is_percent = 1;
    svg_matrix_identity(&m->transform);
    svg_matrix_identity(&m->inv_transform);
    m->has_transform = 0;
    m->type = SVG_MASK_TYPE_LUMINANCE;
    m->has_type = 0;
    m->content = NULL;
    m->rgba = NULL;
    m->mask_w = 0;
    m->mask_h = 0;
    m->cached_bbox_x = 0.0f;
    m->cached_bbox_y = 0.0f;
    m->cached_bbox_w = 0.0f;
    m->cached_bbox_h = 0.0f;
    m->has_cached_bbox = 0;
    m->rendered = 0;
    m->rendering = 0;
    return m;
}

static svg_filter *svg_defs_add_filter(svg_defs *defs, const char *id) {
    if (!defs || !id || !*id) {
        return NULL;
    }
    if (defs->filter_count >= defs->filter_cap) {
        int new_cap = defs->filter_cap ? defs->filter_cap * 2 : 16;
        svg_filter *n = (svg_filter *)realloc(defs->filters, (size_t)new_cap * sizeof(svg_filter));
        if (!n) {
            return NULL;
        }
        defs->filters = n;
        defs->filter_cap = new_cap;
    }
    svg_filter *f = &defs->filters[defs->filter_count++];
    memset(f, 0, sizeof(*f));
    f->id = svg_strdup(id);
    f->content = NULL;
    f->units = SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX;
    f->has_units = 0;
    f->has_coords = 0;
    f->x.value = -0.1f;
    f->y.value = -0.1f;
    f->width.value = 1.2f;
    f->height.value = 1.2f;
    f->x.is_percent = 1;
    f->y.is_percent = 1;
    f->width.is_percent = 1;
    f->height.is_percent = 1;
    return f;
}

static svg_clip_path *svg_defs_find_clip(svg_defs *defs, const char *id) {
    if (!defs || !id) {
        return NULL;
    }
    for (int i = 0; i < defs->clip_count; i++) {
        if (defs->clips[i].id && strcmp(defs->clips[i].id, id) == 0) {
            return &defs->clips[i];
        }
    }
    return NULL;
}

static svg_mask *svg_defs_find_mask(svg_defs *defs, const char *id) {
    if (!defs || !id) {
        return NULL;
    }
    for (int i = 0; i < defs->mask_count; i++) {
        if (defs->masks[i].id && strcmp(defs->masks[i].id, id) == 0) {
            return &defs->masks[i];
        }
    }
    return NULL;
}

static const svg_filter *svg_defs_find_filter(const svg_defs *defs, const char *id) {
    if (!defs || !id) {
        return NULL;
    }
    for (int i = 0; i < defs->filter_count; i++) {
        if (defs->filters[i].id && strcmp(defs->filters[i].id, id) == 0) {
            return &defs->filters[i];
        }
    }
    return NULL;
}

static svg_path_def *svg_defs_add_path(svg_defs *defs, const char *id, const char *d) {
    if (!defs || !id || !*id || !d) {
        return NULL;
    }
    for (int i = 0; i < defs->path_count; i++) {
        if (defs->paths[i].id && strcmp(defs->paths[i].id, id) == 0) {
            free(defs->paths[i].d);
            defs->paths[i].d = svg_strdup(d);
            return &defs->paths[i];
        }
    }
    if (defs->path_count >= defs->path_cap) {
        int new_cap = defs->path_cap ? defs->path_cap * 2 : 16;
        svg_path_def *n = (svg_path_def *)realloc(defs->paths, (size_t)new_cap * sizeof(svg_path_def));
        if (!n) {
            return NULL;
        }
        defs->paths = n;
        defs->path_cap = new_cap;
    }
    svg_path_def *pd = &defs->paths[defs->path_count++];
    pd->id = svg_strdup(id);
    pd->d = svg_strdup(d);
    return pd;
}

static const svg_path_def *svg_defs_find_path(const svg_defs *defs, const char *id) {
    if (!defs || !id) {
        return NULL;
    }
    for (int i = 0; i < defs->path_count; i++) {
        if (defs->paths[i].id && strcmp(defs->paths[i].id, id) == 0) {
            return &defs->paths[i];
        }
    }
    return NULL;
}

static int svg_tag_is_use_def_shape(const char *local) {
    if (!local) {
        return 0;
    }
    return strcmp(local, "rect") == 0 || strcmp(local, "circle") == 0 ||
           strcmp(local, "ellipse") == 0 || strcmp(local, "line") == 0 ||
           strcmp(local, "polyline") == 0 || strcmp(local, "polygon") == 0 ||
           strcmp(local, "path") == 0;
}

static svg_use_def *svg_defs_find_use(svg_defs *defs, const char *id) {
    if (!defs || !id) {
        return NULL;
    }
    for (int i = 0; i < defs->use_count; i++) {
        if (defs->uses[i].id && strcmp(defs->uses[i].id, id) == 0) {
            return &defs->uses[i];
        }
    }
    return NULL;
}

static svg_use_def *svg_defs_add_use(svg_defs *defs, const svg_tag *tag) {
    if (!defs || !tag) {
        return NULL;
    }
    const char *id = svg_get_attr(tag, "id");
    const char *local = svg_local_name(tag->name);
    if (!id || !*id || !svg_tag_is_use_def_shape(local)) {
        return NULL;
    }
    svg_use_def *ud = svg_defs_find_use(defs, id);
    if (!ud) {
        if (defs->use_count >= defs->use_cap) {
            int new_cap = defs->use_cap ? defs->use_cap * 2 : 16;
            svg_use_def *n = (svg_use_def *)realloc(defs->uses, (size_t)new_cap * sizeof(svg_use_def));
            if (!n) {
                return NULL;
            }
            defs->uses = n;
            defs->use_cap = new_cap;
        }
        ud = &defs->uses[defs->use_count++];
        memset(ud, 0, sizeof(*ud));
        ud->id = svg_strdup(id);
    } else {
        free(ud->tag_name);
        ud->tag_name = NULL;
        for (int i = 0; i < ud->attr_count; i++) {
            free(ud->attrs[i].name);
            free(ud->attrs[i].value);
            ud->attrs[i].name = NULL;
            ud->attrs[i].value = NULL;
        }
        ud->attr_count = 0;
    }
    ud->tag_name = svg_strdup(local);
    if (!ud->tag_name) {
        return NULL;
    }
    ud->attr_count = tag->attr_count;
    if (ud->attr_count > SVG_MAX_ATTR) {
        ud->attr_count = SVG_MAX_ATTR;
    }
    for (int i = 0; i < ud->attr_count; i++) {
        ud->attrs[i].name = svg_strdup(tag->attrs[i].name ? tag->attrs[i].name : "");
        ud->attrs[i].value = svg_strdup(tag->attrs[i].value ? tag->attrs[i].value : "");
        if (!ud->attrs[i].name || !ud->attrs[i].value) {
            for (int j = 0; j <= i; j++) {
                free(ud->attrs[j].name);
                free(ud->attrs[j].value);
                ud->attrs[j].name = NULL;
                ud->attrs[j].value = NULL;
            }
            ud->attr_count = 0;
            return NULL;
        }
    }
    return ud;
}

static svg_symbol *svg_defs_add_symbol(svg_defs *defs, const char *id) {
    if (!defs || !id || !*id) {
        return NULL;
    }
    for (int i = 0; i < defs->symbol_count; i++) {
        if (defs->symbols[i].id && strcmp(defs->symbols[i].id, id) == 0) {
            return &defs->symbols[i];
        }
    }
    if (defs->symbol_count >= defs->symbol_cap) {
        int new_cap = defs->symbol_cap ? defs->symbol_cap * 2 : 8;
        svg_symbol *n = (svg_symbol *)realloc(defs->symbols, (size_t)new_cap * sizeof(svg_symbol));
        if (!n) {
            return NULL;
        }
        defs->symbols = n;
        defs->symbol_cap = new_cap;
    }
    svg_symbol *sym = &defs->symbols[defs->symbol_count++];
    memset(sym, 0, sizeof(*sym));
    sym->id = svg_strdup(id);
    sym->align_x = 0.5f;
    sym->align_y = 0.5f;
    return sym;
}

static const svg_symbol *svg_defs_find_symbol(const svg_defs *defs, const char *id) {
    if (!defs || !id) {
        return NULL;
    }
    for (int i = 0; i < defs->symbol_count; i++) {
        if (defs->symbols[i].id && strcmp(defs->symbols[i].id, id) == 0) {
            return &defs->symbols[i];
        }
    }
    return NULL;
}

static svg_marker *svg_defs_add_marker(svg_defs *defs, const char *id) {
    if (!defs || !id || !*id) {
        return NULL;
    }
    for (int i = 0; i < defs->marker_count; i++) {
        if (defs->markers[i].id && strcmp(defs->markers[i].id, id) == 0) {
            return &defs->markers[i];
        }
    }
    if (defs->marker_count >= defs->marker_cap) {
        int new_cap = defs->marker_cap ? defs->marker_cap * 2 : 8;
        svg_marker *n = (svg_marker *)realloc(defs->markers, (size_t)new_cap * sizeof(svg_marker));
        if (!n) {
            return NULL;
        }
        defs->markers = n;
        defs->marker_cap = new_cap;
    }
    svg_marker *mk = &defs->markers[defs->marker_count++];
    memset(mk, 0, sizeof(*mk));
    mk->id = svg_strdup(id);
    mk->ref_x = 0.0f;
    mk->ref_y = 0.0f;
    mk->marker_w = 3.0f;
    mk->marker_h = 3.0f;
    mk->units_stroke_width = 1;
    mk->orient_auto = 0;
    mk->orient_auto_start_reverse = 0;
    mk->orient_angle = 0.0f;
    mk->align_x = 0.5f;
    mk->align_y = 0.5f;
    return mk;
}

static svg_marker *svg_defs_find_marker(svg_defs *defs, const char *id) {
    if (!defs || !id) {
        return NULL;
    }
    for (int i = 0; i < defs->marker_count; i++) {
        if (defs->markers[i].id && strcmp(defs->markers[i].id, id) == 0) {
            return &defs->markers[i];
        }
    }
    return NULL;
}

static int svg_parse_grad_coord(const char *s, svg_grad_coord *out, svg_gradient_units units) {
    if (!out || !s) {
        return 0;
    }
    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (!*s) {
        return 0;
    }
    char *end = NULL;
    double v = strtod(s, &end);
    if (end == s) {
        return 0;
    }
    int percent = 0;
    while (isspace((unsigned char)*end)) {
        end++;
    }
    if (*end == '%') {
        percent = 1;
        end++;
    }
    if (units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX) {
        out->value = percent ? (float)(v / 100.0) : (float)v;
        out->is_percent = 1;
    } else {
        out->value = percent ? (float)(v / 100.0) : (float)v;
        out->is_percent = percent;
    }
    return 1;
}

static int svg_parse_stop_offset(const char *s, float *out) {
    if (!s || !out) {
        return 0;
    }
    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (!*s) {
        return 0;
    }
    char *end = NULL;
    double v = strtod(s, &end);
    if (end == s) {
        return 0;
    }
    int percent = 0;
    while (isspace((unsigned char)*end)) {
        end++;
    }
    if (*end == '%') {
        percent = 1;
    }
    float off = percent ? (float)(v / 100.0) : (float)v;
    if (off < 0.0f) off = 0.0f;
    if (off > 1.0f) off = 1.0f;
    *out = off;
    return 1;
}

static void svg_stop_apply_style(const char *style, uint32_t *color, int *color_set, float *opacity,
                                 int *opacity_set) {
    if (!style) {
        return;
    }
    const char *p = style;
    while (*p) {
        while (isspace((unsigned char)*p) || *p == ';') {
            p++;
        }
        if (!*p) {
            break;
        }
        const char *name_start = p;
        while (*p && *p != ':' && *p != ';') {
            p++;
        }
        if (*p != ':') {
            break;
        }
        const char *name_end = p;
        p++;
        const char *value_start = p;
        while (*p && *p != ';') {
            p++;
        }
        const char *value_end = p;
        while (name_end > name_start && isspace((unsigned char)name_end[-1])) {
            name_end--;
        }
        while (value_end > value_start && isspace((unsigned char)value_end[-1])) {
            value_end--;
        }
        if (name_end > name_start && value_end > value_start) {
            char name_buf[64];
            size_t nlen = (size_t)(name_end - name_start);
            if (nlen >= sizeof(name_buf)) {
                nlen = sizeof(name_buf) - 1;
            }
            memcpy(name_buf, name_start, nlen);
            name_buf[nlen] = '\0';
            svg_lowercase(name_buf);
            char value_buf[128];
            size_t vlen = (size_t)(value_end - value_start);
            if (vlen >= sizeof(value_buf)) {
                vlen = sizeof(value_buf) - 1;
            }
            memcpy(value_buf, value_start, vlen);
            value_buf[vlen] = '\0';
            if (strcmp(name_buf, "stop-color") == 0) {
                uint32_t c = 0;
                int is_none = 0;
                if (svg_parse_color(value_buf, &c, &is_none) && !is_none) {
                    *color = c;
                    *color_set = 1;
                }
            } else if (strcmp(name_buf, "stop-opacity") == 0) {
                float o = 0.0f;
                if (svg_parse_opacity_value(value_buf, &o)) {
                    *opacity = o;
                    *opacity_set = 1;
                }
            }
        }
        if (*p == ';') {
            p++;
        }
    }
}

static int svg_gradient_add_stop(svg_gradient *g, float offset, uint32_t color) {
    if (!g) {
        return 0;
    }
    int idx = g->stop_count;
    svg_gradient_stop *n = (svg_gradient_stop *)realloc(g->stops, (size_t)(idx + 1) * sizeof(svg_gradient_stop));
    if (!n) {
        return 0;
    }
    g->stops = n;
    g->stops[idx].offset = offset;
    g->stops[idx].color = color;
    g->stop_count++;
    return 1;
}

static int svg_stop_parse(const svg_tag *tag, float *offset, uint32_t *color) {
    if (!tag || !offset || !color) {
        return 0;
    }
    float off = 0.0f;
    int off_ok = 0;
    const char *offset_attr = svg_get_attr(tag, "offset");
    if (offset_attr) {
        off_ok = svg_parse_stop_offset(offset_attr, &off);
    }
    if (!off_ok) {
        off = 0.0f;
    }
    uint32_t stop_color = 0x000000FFu;
    int color_set = 0;
    float stop_opacity = 1.0f;
    int opacity_set = 0;
    const char *color_attr = svg_get_attr(tag, "stop-color");
    if (color_attr) {
        uint32_t c = 0;
        int is_none = 0;
        if (svg_parse_color(color_attr, &c, &is_none) && !is_none) {
            stop_color = c;
            color_set = 1;
        }
    }
    const char *opacity_attr = svg_get_attr(tag, "stop-opacity");
    if (opacity_attr) {
        float o = 0.0f;
        if (svg_parse_opacity_value(opacity_attr, &o)) {
            stop_opacity = o;
            opacity_set = 1;
        }
    }
    const char *style_attr = svg_get_attr(tag, "style");
    if (style_attr) {
        svg_stop_apply_style(style_attr, &stop_color, &color_set, &stop_opacity, &opacity_set);
    }
    uint8_t a = (uint8_t)(stop_color & 0xFFu);
    float alpha = (a / 255.0f) * stop_opacity;
    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;
    a = (uint8_t)lroundf(alpha * 255.0f);
    stop_color = (stop_color & 0xFFFFFF00u) | a;
    *offset = off;
    *color = stop_color;
    (void)color_set;
    (void)opacity_set;
    return 1;
}

static int svg_stop_offset_cmp(const void *a, const void *b) {
    const svg_gradient_stop *sa = (const svg_gradient_stop *)a;
    const svg_gradient_stop *sb = (const svg_gradient_stop *)b;
    if (sa->offset < sb->offset) return -1;
    if (sa->offset > sb->offset) return 1;
    return 0;
}

static void svg_gradient_sort_stops(svg_gradient *g) {
    if (g && g->stop_count > 1) {
        qsort(g->stops, (size_t)g->stop_count, sizeof(svg_gradient_stop), svg_stop_offset_cmp);
    }
}

static void svg_gradient_inherit(svg_gradient *g, const svg_defs *defs) {
    if (!g || !g->href || !defs) {
        return;
    }
    const svg_gradient *ref = svg_defs_find_gradient(defs, g->href);
    if (!ref || ref == g) {
        return;
    }
    if (!g->has_units) {
        g->units = ref->units;
    }
    if (!g->has_spread) {
        g->spread = ref->spread;
    }
    if (!g->has_transform) {
        g->transform = ref->transform;
        g->inv_transform = ref->inv_transform;
        g->has_transform = ref->has_transform;
    }
    if (!g->has_coords) {
        if (g->type == SVG_PAINT_LINEAR_GRADIENT && ref->type == SVG_PAINT_LINEAR_GRADIENT) {
            g->x1 = ref->x1;
            g->y1 = ref->y1;
            g->x2 = ref->x2;
            g->y2 = ref->y2;
        } else if (g->type == SVG_PAINT_RADIAL_GRADIENT && ref->type == SVG_PAINT_RADIAL_GRADIENT) {
            g->cx = ref->cx;
            g->cy = ref->cy;
            g->r = ref->r;
        }
    }
    if (!g->has_focal && g->type == SVG_PAINT_RADIAL_GRADIENT && ref->type == SVG_PAINT_RADIAL_GRADIENT) {
        g->fx = ref->fx;
        g->fy = ref->fy;
    }
    if (g->stop_count == 0 && ref->stop_count > 0) {
        g->stops = (svg_gradient_stop *)malloc((size_t)ref->stop_count * sizeof(svg_gradient_stop));
        if (!g->stops) {
            return;
        }
        memcpy(g->stops, ref->stops, (size_t)ref->stop_count * sizeof(svg_gradient_stop));
        g->stop_count = ref->stop_count;
    }
}

static void svg_gradient_finalize(svg_gradient *g) {
    if (!g) {
        return;
    }
    if (g->has_transform) {
        if (!svg_matrix_invert(&g->transform, &g->inv_transform)) {
            g->has_transform = 0;
        }
    }
    svg_gradient_sort_stops(g);
}

static void svg_parse_gradient_tag(const svg_tag *tag, svg_gradient *g) {
    if (!tag || !g) {
        return;
    }
    const char *units = svg_get_attr(tag, "gradientUnits");
    if (units) {
        if (svg_strcasecmp(units, "userSpaceOnUse") == 0) {
            g->units = SVG_GRADIENT_UNITS_USER_SPACE;
            g->has_units = 1;
        } else if (svg_strcasecmp(units, "objectBoundingBox") == 0) {
            g->units = SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX;
            g->has_units = 1;
        }
    }
    const char *spread = svg_get_attr(tag, "spreadMethod");
    if (spread) {
        if (svg_strcasecmp(spread, "reflect") == 0) {
            g->spread = SVG_SPREAD_REFLECT;
            g->has_spread = 1;
        } else if (svg_strcasecmp(spread, "repeat") == 0) {
            g->spread = SVG_SPREAD_REPEAT;
            g->has_spread = 1;
        } else if (svg_strcasecmp(spread, "pad") == 0) {
            g->spread = SVG_SPREAD_PAD;
            g->has_spread = 1;
        }
    }
    const char *transform = svg_get_attr(tag, "gradientTransform");
    if (transform) {
        svg_matrix t;
        if (svg_parse_transform(transform, &t)) {
            g->transform = t;
            g->has_transform = 1;
        }
    }
    const char *href = svg_get_attr(tag, "href");
    char href_id[96];
    if (svg_parse_href_id(href, href_id, sizeof(href_id))) {
        free(g->href);
        g->href = svg_strdup(href_id);
    }
    if (g->type == SVG_PAINT_LINEAR_GRADIENT) {
        int has = 0;
        const char *x1 = svg_get_attr(tag, "x1");
        const char *y1 = svg_get_attr(tag, "y1");
        const char *x2 = svg_get_attr(tag, "x2");
        const char *y2 = svg_get_attr(tag, "y2");
        if (x1 && svg_parse_grad_coord(x1, &g->x1, g->units)) has = 1;
        if (y1 && svg_parse_grad_coord(y1, &g->y1, g->units)) has = 1;
        if (x2 && svg_parse_grad_coord(x2, &g->x2, g->units)) has = 1;
        if (y2 && svg_parse_grad_coord(y2, &g->y2, g->units)) has = 1;
        if (has) g->has_coords = 1;
    } else {
        int has = 0;
        const char *cx = svg_get_attr(tag, "cx");
        const char *cy = svg_get_attr(tag, "cy");
        const char *r = svg_get_attr(tag, "r");
        const char *fx = svg_get_attr(tag, "fx");
        const char *fy = svg_get_attr(tag, "fy");
        if (cx && svg_parse_grad_coord(cx, &g->cx, g->units)) has = 1;
        if (cy && svg_parse_grad_coord(cy, &g->cy, g->units)) has = 1;
        if (r && svg_parse_grad_coord(r, &g->r, g->units)) has = 1;
        if (fx && svg_parse_grad_coord(fx, &g->fx, g->units)) {
            g->has_focal = 1;
        }
        if (fy && svg_parse_grad_coord(fy, &g->fy, g->units)) {
            g->has_focal = 1;
        }
        if (has) g->has_coords = 1;
    }
}

static void svg_parse_pattern_tag(const svg_tag *tag, svg_pattern *p) {
    if (!tag || !p) {
        return;
    }
    const char *units = svg_get_attr(tag, "patternUnits");
    if (units) {
        if (svg_strcasecmp(units, "userSpaceOnUse") == 0) {
            p->units = SVG_GRADIENT_UNITS_USER_SPACE;
            p->has_units = 1;
        } else if (svg_strcasecmp(units, "objectBoundingBox") == 0) {
            p->units = SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX;
            p->has_units = 1;
        }
    }
    const char *content_units = svg_get_attr(tag, "patternContentUnits");
    if (content_units) {
        if (svg_strcasecmp(content_units, "userSpaceOnUse") == 0) {
            p->content_units = SVG_GRADIENT_UNITS_USER_SPACE;
            p->has_content_units = 1;
        } else if (svg_strcasecmp(content_units, "objectBoundingBox") == 0) {
            p->content_units = SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX;
            p->has_content_units = 1;
        }
    }
    const char *transform = svg_get_attr(tag, "patternTransform");
    if (transform) {
        svg_matrix t;
        if (svg_parse_transform(transform, &t)) {
            p->transform = t;
            p->has_transform = 1;
        }
    }
    const char *href = svg_get_attr(tag, "href");
    char href_id[96];
    if (svg_parse_href_id(href, href_id, sizeof(href_id))) {
        free(p->href);
        p->href = svg_strdup(href_id);
    }
    int has = 0;
    const char *x = svg_get_attr(tag, "x");
    const char *y = svg_get_attr(tag, "y");
    const char *w = svg_get_attr(tag, "width");
    const char *h = svg_get_attr(tag, "height");
    if (x && svg_parse_grad_coord(x, &p->x, p->units)) has = 1;
    if (y && svg_parse_grad_coord(y, &p->y, p->units)) has = 1;
    if (w && svg_parse_grad_coord(w, &p->width, p->units)) has = 1;
    if (h && svg_parse_grad_coord(h, &p->height, p->units)) has = 1;
    if (has) {
        p->has_coords = 1;
    }
}

static void svg_pattern_inherit(svg_pattern *p, const svg_defs *defs) {
    if (!p || !p->href || !defs) {
        return;
    }
    const svg_pattern *ref = svg_defs_find_pattern(defs, p->href);
    if (!ref || ref == p) {
        return;
    }
    if (!p->has_units) {
        p->units = ref->units;
    }
    if (!p->has_content_units) {
        p->content_units = ref->content_units;
    }
    if (!p->has_transform) {
        p->transform = ref->transform;
        p->inv_transform = ref->inv_transform;
        p->has_transform = ref->has_transform;
    }
    if (!p->has_coords) {
        p->x = ref->x;
        p->y = ref->y;
        p->width = ref->width;
        p->height = ref->height;
    }
    if (!p->content && ref->content) {
        p->content = svg_strdup(ref->content);
    }
}

static void svg_pattern_finalize(svg_pattern *p) {
    if (!p) {
        return;
    }
    if (p->has_transform) {
        if (!svg_matrix_invert(&p->transform, &p->inv_transform)) {
            p->has_transform = 0;
        }
    }
}

static void svg_parse_clip_tag(const svg_tag *tag, svg_clip_path *c) {
    if (!tag || !c) {
        return;
    }
    const char *units = svg_get_attr(tag, "clipPathUnits");
    if (units) {
        if (svg_strcasecmp(units, "userSpaceOnUse") == 0) {
            c->units = SVG_GRADIENT_UNITS_USER_SPACE;
            c->has_units = 1;
        } else if (svg_strcasecmp(units, "objectBoundingBox") == 0) {
            c->units = SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX;
            c->has_units = 1;
        }
    }
    const char *transform = svg_get_attr(tag, "transform");
    if (transform) {
        svg_matrix t;
        if (svg_parse_transform(transform, &t)) {
            c->transform = t;
            c->has_transform = 1;
        }
    }
    const char *href = svg_get_attr(tag, "href");
    char href_id[96];
    if (svg_parse_href_id(href, href_id, sizeof(href_id))) {
        free(c->href);
        c->href = svg_strdup(href_id);
    }
}

static void svg_clip_inherit(svg_clip_path *c, svg_defs *defs) {
    if (!c || !c->href || !defs) {
        return;
    }
    svg_clip_path *ref = svg_defs_find_clip(defs, c->href);
    if (!ref || ref == c) {
        return;
    }
    if (!c->has_units) {
        c->units = ref->units;
    }
    if (!c->has_transform) {
        c->transform = ref->transform;
        c->inv_transform = ref->inv_transform;
        c->has_transform = ref->has_transform;
    }
    if (!c->content && ref->content) {
        c->content = svg_strdup(ref->content);
    }
}

static void svg_clip_finalize(svg_clip_path *c) {
    if (!c) {
        return;
    }
    if (c->has_transform) {
        if (!svg_matrix_invert(&c->transform, &c->inv_transform)) {
            c->has_transform = 0;
        }
    }
}

static void svg_parse_mask_tag(const svg_tag *tag, svg_mask *m) {
    if (!tag || !m) {
        return;
    }
    const char *units = svg_get_attr(tag, "maskUnits");
    if (units) {
        if (svg_strcasecmp(units, "userSpaceOnUse") == 0) {
            m->units = SVG_GRADIENT_UNITS_USER_SPACE;
            m->has_units = 1;
        } else if (svg_strcasecmp(units, "objectBoundingBox") == 0) {
            m->units = SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX;
            m->has_units = 1;
        }
    }
    const char *content_units = svg_get_attr(tag, "maskContentUnits");
    if (content_units) {
        if (svg_strcasecmp(content_units, "userSpaceOnUse") == 0) {
            m->content_units = SVG_GRADIENT_UNITS_USER_SPACE;
            m->has_content_units = 1;
        } else if (svg_strcasecmp(content_units, "objectBoundingBox") == 0) {
            m->content_units = SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX;
            m->has_content_units = 1;
        }
    }
    const char *transform = svg_get_attr(tag, "transform");
    if (transform) {
        svg_matrix t;
        if (svg_parse_transform(transform, &t)) {
            m->transform = t;
            m->has_transform = 1;
        }
    }
    const char *href = svg_get_attr(tag, "href");
    char href_id[96];
    if (svg_parse_href_id(href, href_id, sizeof(href_id))) {
        free(m->href);
        m->href = svg_strdup(href_id);
    }
    int has = 0;
    const char *x = svg_get_attr(tag, "x");
    const char *y = svg_get_attr(tag, "y");
    const char *w = svg_get_attr(tag, "width");
    const char *h = svg_get_attr(tag, "height");
    if (x && svg_parse_grad_coord(x, &m->x, m->units)) has = 1;
    if (y && svg_parse_grad_coord(y, &m->y, m->units)) has = 1;
    if (w && svg_parse_grad_coord(w, &m->width, m->units)) has = 1;
    if (h && svg_parse_grad_coord(h, &m->height, m->units)) has = 1;
    if (has) {
        m->has_coords = 1;
    }
    const char *mask_type = svg_get_attr(tag, "mask-type");
    if (mask_type) {
        if (svg_strcasecmp(mask_type, "alpha") == 0) {
            m->type = SVG_MASK_TYPE_ALPHA;
            m->has_type = 1;
        } else if (svg_strcasecmp(mask_type, "luminance") == 0) {
            m->type = SVG_MASK_TYPE_LUMINANCE;
            m->has_type = 1;
        }
    }
}

static void svg_parse_filter_tag(const svg_tag *tag, svg_filter *f) {
    if (!tag || !f) {
        return;
    }
    const char *units = svg_get_attr(tag, "filterUnits");
    if (units) {
        if (svg_strcasecmp(units, "userSpaceOnUse") == 0) {
            f->units = SVG_GRADIENT_UNITS_USER_SPACE;
            f->has_units = 1;
        } else if (svg_strcasecmp(units, "objectBoundingBox") == 0) {
            f->units = SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX;
            f->has_units = 1;
        }
    }
    int has = 0;
    const char *x = svg_get_attr(tag, "x");
    const char *y = svg_get_attr(tag, "y");
    const char *w = svg_get_attr(tag, "width");
    const char *h = svg_get_attr(tag, "height");
    if (x && svg_parse_grad_coord(x, &f->x, f->units)) has = 1;
    if (y && svg_parse_grad_coord(y, &f->y, f->units)) has = 1;
    if (w && svg_parse_grad_coord(w, &f->width, f->units)) has = 1;
    if (h && svg_parse_grad_coord(h, &f->height, f->units)) has = 1;
    if (has) {
        f->has_coords = 1;
    }
}

static void svg_parse_symbol_tag(const svg_tag *tag, svg_symbol *sym) {
    if (!tag || !sym) {
        return;
    }
    sym->vb_ok = svg_parse_viewbox(svg_get_attr(tag, "viewBox"),
                                   &sym->vb_x, &sym->vb_y, &sym->vb_w, &sym->vb_h);
    sym->preserve_none = 0;
    sym->align_x = 0.5f;
    sym->align_y = 0.5f;
    svg_parse_preserve_aspect_ratio(svg_get_attr(tag, "preserveAspectRatio"),
                                    &sym->preserve_none, &sym->align_x, &sym->align_y);
}

static void svg_parse_marker_tag(const svg_tag *tag, svg_marker *mk, float base_len, float dpi) {
    if (!tag || !mk) {
        return;
    }
    int ok = 0;
    float v = 0.0f;
    v = svg_parse_length(svg_get_attr(tag, "refX"), base_len, dpi, &ok);
    if (ok) {
        mk->ref_x = v;
    }
    v = svg_parse_length(svg_get_attr(tag, "refY"), base_len, dpi, &ok);
    if (ok) {
        mk->ref_y = v;
    }
    v = svg_parse_length(svg_get_attr(tag, "markerWidth"), base_len, dpi, &ok);
    if (ok && v > 0.0f) {
        mk->marker_w = v;
    }
    v = svg_parse_length(svg_get_attr(tag, "markerHeight"), base_len, dpi, &ok);
    if (ok && v > 0.0f) {
        mk->marker_h = v;
    }
    const char *units = svg_get_attr(tag, "markerUnits");
    if (units) {
        if (svg_strcasecmp(units, "userspaceonuse") == 0) {
            mk->units_stroke_width = 0;
        } else if (svg_strcasecmp(units, "strokewidth") == 0) {
            mk->units_stroke_width = 1;
        }
    }
    const char *orient = svg_get_attr(tag, "orient");
    if (orient) {
        if (svg_strcasecmp(orient, "auto-start-reverse") == 0) {
            mk->orient_auto = 1;
            mk->orient_auto_start_reverse = 1;
        } else if (svg_strcasecmp(orient, "auto") == 0) {
            mk->orient_auto = 1;
            mk->orient_auto_start_reverse = 0;
        } else {
            char *end = NULL;
            double a = strtod(orient, &end);
            if (end != orient) {
                mk->orient_auto = 0;
                mk->orient_auto_start_reverse = 0;
                mk->orient_angle = (float)a;
            }
        }
    }
    mk->vb_ok = svg_parse_viewbox(svg_get_attr(tag, "viewBox"),
                                  &mk->vb_x, &mk->vb_y, &mk->vb_w, &mk->vb_h);
    mk->preserve_none = 0;
    mk->align_x = 0.5f;
    mk->align_y = 0.5f;
    svg_parse_preserve_aspect_ratio(svg_get_attr(tag, "preserveAspectRatio"),
                                    &mk->preserve_none, &mk->align_x, &mk->align_y);
}

static void svg_mask_inherit(svg_mask *m, svg_defs *defs) {
    if (!m || !m->href || !defs) {
        return;
    }
    svg_mask *ref = svg_defs_find_mask(defs, m->href);
    if (!ref || ref == m) {
        return;
    }
    if (!m->has_units) {
        m->units = ref->units;
    }
    if (!m->has_content_units) {
        m->content_units = ref->content_units;
    }
    if (!m->has_transform) {
        m->transform = ref->transform;
        m->inv_transform = ref->inv_transform;
        m->has_transform = ref->has_transform;
    }
    if (!m->has_type) {
        m->type = ref->type;
    }
    if (!m->has_coords) {
        m->x = ref->x;
        m->y = ref->y;
        m->width = ref->width;
        m->height = ref->height;
    }
    if (!m->content && ref->content) {
        m->content = svg_strdup(ref->content);
    }
}

static void svg_mask_finalize(svg_mask *m) {
    if (!m) {
        return;
    }
    if (m->has_transform) {
        if (!svg_matrix_invert(&m->transform, &m->inv_transform)) {
            m->has_transform = 0;
        }
    }
}

static void svg_text_path_clear(svg_text_path_state *tp) {
    if (!tp) {
        return;
    }
    free(tp->seg_lengths);
    tp->seg_lengths = NULL;
    tp->seg_count = 0;
    tp->total_len = 0.0f;
    tp->pos = 0.0f;
    tp->active = 0;
    svg_segments_free(&tp->segs);
}

static int svg_text_path_point(const svg_text_path_state *tp, float dist,
                               float *out_x, float *out_y, float *out_angle) {
    if (!tp || !out_x || !out_y || !out_angle || tp->seg_count == 0 || tp->total_len <= 0.0f) {
        return 0;
    }
    if (dist < 0.0f || dist > tp->total_len) {
        return 0;
    }
    float acc = 0.0f;
    for (size_t i = 0; i < tp->seg_count; i++) {
        float len = tp->seg_lengths[i];
        if (len <= 1e-6f) {
            acc += len;
            continue;
        }
        if (dist <= acc + len) {
            float t = (dist - acc) / len;
            float x0 = tp->segs.stroke[i].x0;
            float y0 = tp->segs.stroke[i].y0;
            float x1 = tp->segs.stroke[i].x1;
            float y1 = tp->segs.stroke[i].y1;
            float dx = x1 - x0;
            float dy = y1 - y0;
            *out_x = x0 + dx * t;
            *out_y = y0 + dy * t;
            *out_angle = atan2f(dy, dx);
            return 1;
        }
        acc += len;
    }
    return 0;
}

static float svg_textpath_measure_range(const char *start, const char *end, int preserve, const svg_style *style) {
    if (!start || !end || !style || end <= start) {
        return 0.0f;
    }
    size_t cap = (size_t)(end - start) + 1u;
    char *raw = (char *)malloc(cap);
    if (!raw) {
        return 0.0f;
    }
    size_t w = 0;
    const char *p = start;
    while (p < end && *p) {
        if (*p == '<') {
            const char *gt = strchr(p, '>');
            if (!gt || gt >= end) {
                break;
            }
            p = gt + 1;
            continue;
        }
        raw[w++] = *p++;
    }
    raw[w] = '\0';
    float width = 0.0f;
    char *norm = svg_text_normalize(raw, preserve);
    if (norm && *norm) {
        width = svg_measure_line(style, norm, strlen(norm));
    }
    free(norm);
    free(raw);
    return width;
}

static char *svg_css_strip_comments(const char *text) {
    if (!text) {
        return NULL;
    }
    size_t len = strlen(text);
    char *out = (char *)malloc(len + 1);
    if (!out) {
        return NULL;
    }
    size_t w = 0;
    for (size_t i = 0; i < len;) {
        if (text[i] == '/' && text[i + 1] == '*') {
            i += 2;
            while (i + 1 < len && !(text[i] == '*' && text[i + 1] == '/')) {
                i++;
            }
            if (i + 1 < len) {
                i += 2;
            }
            continue;
        }
        if (strncmp(text + i, "<![CDATA[", 9) == 0) {
            i += 9;
            continue;
        }
        if (strncmp(text + i, "]]>", 3) == 0) {
            i += 3;
            continue;
        }
        out[w++] = text[i++];
    }
    out[w] = '\0';
    return out;
}

static void svg_css_trim_span(const char **start, const char **end) {
    if (!start || !end || !*start || !*end) {
        return;
    }
    while (*start < *end && isspace((unsigned char)**start)) {
        (*start)++;
    }
    while (*end > *start && isspace((unsigned char)(*end)[-1])) {
        (*end)--;
    }
}

static char *svg_css_span_dup(const char *start, const char *end) {
    if (!start || !end || end <= start) {
        return NULL;
    }
    return svg_strndup(start, (size_t)(end - start));
}

static const char *svg_css_find_matching_brace(const char *p) {
    if (!p || *p != '{') {
        return NULL;
    }
    int depth = 0;
    char quote = '\0';
    for (; *p; p++) {
        if (quote) {
            if (*p == quote) {
                quote = '\0';
            } else if (*p == '\\' && p[1]) {
                p++;
            }
            continue;
        }
        if (*p == '"' || *p == '\'') {
            quote = *p;
            continue;
        }
        if (*p == '{') {
            depth++;
            continue;
        }
        if (*p == '}') {
            depth--;
            if (depth == 0) {
                return p;
            }
        }
    }
    return NULL;
}

static int svg_css_parse_label_offset(const char *s, float *out) {
    if (!s || !out) {
        return 0;
    }
    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (!*s) {
        return 0;
    }
    if (svg_strcasecmp(s, "from") == 0) {
        *out = 0.0f;
        return 1;
    }
    if (svg_strcasecmp(s, "to") == 0) {
        *out = 1.0f;
        return 1;
    }
    char *end = NULL;
    double v = strtod(s, &end);
    if (end == s) {
        return 0;
    }
    while (isspace((unsigned char)*end)) {
        end++;
    }
    if (*end == '%') {
        end++;
        v /= 100.0;
    }
    while (isspace((unsigned char)*end)) {
        end++;
    }
    if (*end) {
        return 0;
    }
    if (v < 0.0) {
        v = 0.0;
    } else if (v > 1.0) {
        v = 1.0;
    }
    *out = (float)v;
    return 1;
}

static int svg_css_extract_decl_value_span(const char *decls_start, const char *decls_end,
                                           const char *name, char **out_value) {
    if (!decls_start || !decls_end || decls_end <= decls_start || !name || !out_value) {
        return 0;
    }
    const char *p = decls_start;
    size_t name_len = strlen(name);
    while (p < decls_end) {
        while (p < decls_end && (isspace((unsigned char)*p) || *p == ';')) {
            p++;
        }
        if (p >= decls_end) {
            break;
        }
        const char *nstart = p;
        while (p < decls_end && *p != ':' && *p != ';') {
            p++;
        }
        if (p >= decls_end || *p != ':') {
            break;
        }
        const char *nend = p;
        p++;
        const char *vstart = p;
        int paren_depth = 0;
        char quote = '\0';
        while (p < decls_end) {
            char c = *p;
            if (quote) {
                if (c == quote) {
                    quote = '\0';
                } else if (c == '\\' && p + 1 < decls_end) {
                    p++;
                }
                p++;
                continue;
            }
            if (c == '"' || c == '\'') {
                quote = c;
                p++;
                continue;
            }
            if (c == '(') {
                paren_depth++;
                p++;
                continue;
            }
            if (c == ')' && paren_depth > 0) {
                paren_depth--;
                p++;
                continue;
            }
            if (c == ';' && paren_depth == 0) {
                break;
            }
            p++;
        }
        const char *vend = p;
        while (nend > nstart && isspace((unsigned char)nend[-1])) {
            nend--;
        }
        while (vend > vstart && isspace((unsigned char)vend[-1])) {
            vend--;
        }
        while (nstart < nend && isspace((unsigned char)*nstart)) {
            nstart++;
        }
        if ((size_t)(nend - nstart) == name_len && svg_strncasecmp(nstart, name, name_len) == 0) {
            *out_value = svg_css_span_dup(vstart, vend);
            return *out_value != NULL;
        }
        if (p < decls_end && *p == ';') {
            p++;
        }
    }
    return 0;
}

static int svg_css_add_keyframe_step(svg_css_keyframes *kf, const svg_css_keyframe_step *step) {
    if (!kf || !step) {
        return 0;
    }
    if (kf->step_count >= kf->step_cap) {
        int new_cap = kf->step_cap ? kf->step_cap * 2 : 8;
        svg_css_keyframe_step *n =
            (svg_css_keyframe_step *)realloc(kf->steps, (size_t)new_cap * sizeof(svg_css_keyframe_step));
        if (!n) {
            return 0;
        }
        kf->steps = n;
        kf->step_cap = new_cap;
    }
    kf->steps[kf->step_count++] = *step;
    return 1;
}

static int svg_css_add_keyframes(svg_css *css, svg_css_keyframes *kf) {
    if (!css || !kf || !kf->name || kf->step_count <= 0) {
        return 0;
    }
    if (css->keyframe_count >= css->keyframe_cap) {
        int new_cap = css->keyframe_cap ? css->keyframe_cap * 2 : 8;
        svg_css_keyframes *n =
            (svg_css_keyframes *)realloc(css->keyframes, (size_t)new_cap * sizeof(svg_css_keyframes));
        if (!n) {
            return 0;
        }
        css->keyframes = n;
        css->keyframe_cap = new_cap;
    }
    css->keyframes[css->keyframe_count++] = *kf;
    return 1;
}

static int svg_css_add_animation_binding(svg_css *css, svg_css_animation_binding *anim) {
    if (!css || !anim || !anim->target_id || !anim->name || anim->duration <= 0.0f) {
        return 0;
    }
    if (css->animation_count >= css->animation_cap) {
        int new_cap = css->animation_cap ? css->animation_cap * 2 : 16;
        svg_css_animation_binding *n = (svg_css_animation_binding *)realloc(
            css->animations, (size_t)new_cap * sizeof(svg_css_animation_binding));
        if (!n) {
            return 0;
        }
        css->animations = n;
        css->animation_cap = new_cap;
    }
    css->animations[css->animation_count++] = *anim;
    return 1;
}

static int svg_css_parse_animation_shorthand(const char *value, svg_css_animation_binding *anim) {
    if (!value || !anim) {
        return 0;
    }
    memset(anim, 0, sizeof(*anim));
    anim->repeat_count = 1.0f;
    anim->direction = SVG_CSS_ANIM_DIR_NORMAL;
    anim->fill_mode = SVG_CSS_ANIM_FILL_NONE;

    const char *p = value;
    int have_name = 0;
    int have_duration = 0;
    while (*p) {
        while (isspace((unsigned char)*p)) {
            p++;
        }
        if (!*p) {
            break;
        }
        const char *tstart = p;
        int paren_depth = 0;
        while (*p) {
            char c = *p;
            if (c == '(') {
                paren_depth++;
            } else if (c == ')' && paren_depth > 0) {
                paren_depth--;
            } else if (isspace((unsigned char)c) && paren_depth == 0) {
                break;
            }
            p++;
        }
        const char *tend = p;
        char *tok = svg_css_span_dup(tstart, tend);
        if (!tok) {
            return 0;
        }
        if (strchr(tok, ',')) {
            char *comma = strchr(tok, ',');
            *comma = '\0';
            while (comma > tok && isspace((unsigned char)comma[-1])) {
                comma--;
            }
            *comma = '\0';
            p = tend;
        }
        float secs = 0.0f;
        if (svg_parse_clock_seconds(tok, &secs)) {
            if (!have_duration) {
                anim->duration = secs;
                have_duration = 1;
            } else {
                anim->delay = secs;
            }
            free(tok);
            continue;
        }
        if (svg_strcasecmp(tok, "infinite") == 0) {
            anim->repeat_indefinite = 1;
            free(tok);
            continue;
        }
        char *num_end = NULL;
        float rep = strtof(tok, &num_end);
        if (num_end && *num_end == '\0' && rep > 0.0f) {
            anim->repeat_count = rep;
            free(tok);
            continue;
        }
        if (svg_strcasecmp(tok, "normal") == 0) {
            anim->direction = SVG_CSS_ANIM_DIR_NORMAL;
            free(tok);
            continue;
        }
        if (svg_strcasecmp(tok, "reverse") == 0) {
            anim->direction = SVG_CSS_ANIM_DIR_REVERSE;
            free(tok);
            continue;
        }
        if (svg_strcasecmp(tok, "alternate") == 0) {
            anim->direction = SVG_CSS_ANIM_DIR_ALTERNATE;
            free(tok);
            continue;
        }
        if (svg_strcasecmp(tok, "alternate-reverse") == 0) {
            anim->direction = SVG_CSS_ANIM_DIR_ALTERNATE_REVERSE;
            free(tok);
            continue;
        }
        if (svg_strcasecmp(tok, "forwards") == 0) {
            anim->fill_mode = SVG_CSS_ANIM_FILL_FORWARDS;
            free(tok);
            continue;
        }
        if (svg_strcasecmp(tok, "backwards") == 0) {
            anim->fill_mode = SVG_CSS_ANIM_FILL_BACKWARDS;
            free(tok);
            continue;
        }
        if (svg_strcasecmp(tok, "both") == 0) {
            anim->fill_mode = SVG_CSS_ANIM_FILL_BOTH;
            free(tok);
            continue;
        }
        if (svg_strcasecmp(tok, "none") == 0 ||
            svg_strcasecmp(tok, "linear") == 0 ||
            svg_strcasecmp(tok, "ease") == 0 ||
            svg_strcasecmp(tok, "ease-in") == 0 ||
            svg_strcasecmp(tok, "ease-out") == 0 ||
            svg_strcasecmp(tok, "ease-in-out") == 0 ||
            svg_strncasecmp(tok, "steps(", 6) == 0 ||
            svg_strncasecmp(tok, "cubic-bezier(", 13) == 0 ||
            svg_strcasecmp(tok, "running") == 0 ||
            svg_strcasecmp(tok, "paused") == 0) {
            free(tok);
            continue;
        }
        if (!have_name) {
            anim->name = tok;
            tok = NULL;
            have_name = 1;
        }
        free(tok);
    }
    if (!have_name || !have_duration || anim->duration <= 0.0f) {
        free(anim->name);
        anim->name = NULL;
        return 0;
    }
    return 1;
}

static int svg_css_parse_target_id(const char *selector, char *out, size_t out_cap) {
    if (!selector || !out || out_cap == 0) {
        return 0;
    }
    const char *p = selector;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (*p != '#') {
        return 0;
    }
    p++;
    const char *start = p;
    while (*p && (isalnum((unsigned char)*p) || *p == '_' || *p == '-' || *p == ':' || *p == '.')) {
        p++;
    }
    if (p == start) {
        return 0;
    }
    size_t n = (size_t)(p - start);
    if (n >= out_cap) {
        n = out_cap - 1u;
    }
    memcpy(out, start, n);
    out[n] = '\0';
    return 1;
}

static int svg_css_parse_keyframes_block(svg_css *css, const char *selector_start, const char *selector_end,
                                         const char *body_start, const char *body_end) {
    if (!css || !selector_start || !selector_end || !body_start || !body_end) {
        return 0;
    }
    const char *s = selector_start;
    const char *e = selector_end;
    svg_css_trim_span(&s, &e);
    if (e <= s) {
        return 0;
    }
    if (!(svg_strncasecmp(s, "@keyframes", 10) == 0 ||
          svg_strncasecmp(s, "@-webkit-keyframes", 18) == 0)) {
        return 0;
    }
    const char *name = s + (svg_strncasecmp(s, "@-webkit-keyframes", 18) == 0 ? 18 : 10);
    while (name < e && isspace((unsigned char)*name)) {
        name++;
    }
    const char *name_end = e;
    while (name_end > name && isspace((unsigned char)name_end[-1])) {
        name_end--;
    }
    if (name_end <= name) {
        return 0;
    }

    svg_css_keyframes kf;
    memset(&kf, 0, sizeof(kf));
    kf.name = svg_css_span_dup(name, name_end);
    if (!kf.name) {
        return 0;
    }
    kf.prop = SVG_CSS_ANIM_PROP_NONE;

    const char *p = body_start;
    while (p < body_end) {
        while (p < body_end && isspace((unsigned char)*p)) {
            p++;
        }
        if (p >= body_end) {
            break;
        }
        const char *label_start = p;
        while (p < body_end && *p != '{') {
            p++;
        }
        if (p >= body_end || *p != '{') {
            break;
        }
        const char *label_end = p;
        const char *close = svg_css_find_matching_brace(p);
        if (!close || close >= body_end) {
            break;
        }
        const char *decl_start = p + 1;
        const char *decl_end = close;

        char *transform_val = NULL;
        char *dash_val = NULL;
        char *color_val = NULL;
        svg_css_extract_decl_value_span(decl_start, decl_end, "transform", &transform_val);
        svg_css_extract_decl_value_span(decl_start, decl_end, "stroke-dashoffset", &dash_val);
        svg_css_extract_decl_value_span(decl_start, decl_end, "stop-color", &color_val);

        int prop = SVG_CSS_ANIM_PROP_NONE;
        if (transform_val && *transform_val) {
            prop = SVG_CSS_ANIM_PROP_TRANSFORM;
        } else if (dash_val && *dash_val) {
            prop = SVG_CSS_ANIM_PROP_STROKE_DASHOFFSET;
        } else if (color_val && *color_val) {
            prop = SVG_CSS_ANIM_PROP_STOP_COLOR;
        }
        if (prop != SVG_CSS_ANIM_PROP_NONE) {
            if (kf.prop == SVG_CSS_ANIM_PROP_NONE) {
                kf.prop = prop;
            }
            if (kf.prop == prop) {
                const char *ls = label_start;
                while (ls < label_end) {
                    while (ls < label_end && (isspace((unsigned char)*ls) || *ls == ',')) {
                        ls++;
                    }
                    const char *le = ls;
                    while (le < label_end && *le != ',') {
                        le++;
                    }
                    const char *lts = ls;
                    const char *lte = le;
                    svg_css_trim_span(&lts, &lte);
                    if (lte > lts) {
                        char *label = svg_css_span_dup(lts, lte);
                        float off = 0.0f;
                        if (label && svg_css_parse_label_offset(label, &off)) {
                            svg_css_keyframe_step step;
                            memset(&step, 0, sizeof(step));
                            step.offset = off;
                            if (prop == SVG_CSS_ANIM_PROP_TRANSFORM) {
                                step.transform = svg_strdup(transform_val);
                            } else if (prop == SVG_CSS_ANIM_PROP_STROKE_DASHOFFSET) {
                                char *num_end = NULL;
                                step.number = strtof(dash_val, &num_end);
                                if (!num_end || num_end == dash_val) {
                                    step.number = 0.0f;
                                }
                            } else if (prop == SVG_CSS_ANIM_PROP_STOP_COLOR) {
                                uint32_t col = 0;
                                int is_none = 0;
                                if (svg_parse_color(color_val, &col, &is_none) && !is_none) {
                                    step.color = col;
                                }
                            }
                            svg_css_add_keyframe_step(&kf, &step);
                        }
                        free(label);
                    }
                    ls = (le < label_end) ? (le + 1) : le;
                }
            }
        }
        free(transform_val);
        free(dash_val);
        free(color_val);
        p = close + 1;
    }

    for (int i = 0; i < kf.step_count - 1; i++) {
        for (int j = i + 1; j < kf.step_count; j++) {
            if (kf.steps[j].offset < kf.steps[i].offset) {
                svg_css_keyframe_step tmp = kf.steps[i];
                kf.steps[i] = kf.steps[j];
                kf.steps[j] = tmp;
            }
        }
    }
    if (kf.prop == SVG_CSS_ANIM_PROP_NONE || kf.step_count == 0) {
        free(kf.name);
        for (int i = 0; i < kf.step_count; i++) {
            free(kf.steps[i].transform);
        }
        free(kf.steps);
        return 0;
    }
    return svg_css_add_keyframes(css, &kf);
}

static void svg_css_parse_animation_bindings(svg_css *css, const char *selector_start, const char *selector_end,
                                             const char *decl_start, const char *decl_end) {
    if (!css || !selector_start || !selector_end || !decl_start || !decl_end) {
        return;
    }
    char *anim_value = NULL;
    if (!svg_css_extract_decl_value_span(decl_start, decl_end, "animation", &anim_value) || !anim_value) {
        return;
    }
    const char *sel = selector_start;
    while (sel < selector_end) {
        while (sel < selector_end && (isspace((unsigned char)*sel) || *sel == ',')) {
            sel++;
        }
        const char *send = sel;
        while (send < selector_end && *send != ',') {
            send++;
        }
        const char *ts = sel;
        const char *te = send;
        svg_css_trim_span(&ts, &te);
        if (te > ts) {
            char id[128];
            if (svg_css_parse_target_id(ts, id, sizeof(id))) {
                svg_css_animation_binding anim;
                if (svg_css_parse_animation_shorthand(anim_value, &anim)) {
                    anim.target_id = svg_strdup(id);
                    if (anim.target_id) {
                        svg_css_add_animation_binding(css, &anim);
                    } else {
                        free(anim.name);
                    }
                }
            }
        }
        sel = (send < selector_end) ? (send + 1) : send;
    }
    free(anim_value);
}

static int svg_css_parse_selector(const char *s, svg_selector *out) {
    if (!s || !out) {
        return 0;
    }
    svg_selector sel;
    memset(&sel, 0, sizeof(sel));
    const char *p = s;
    int pending_combinator = 0; /* 0: descendant, 1: child (>), 2: adjacent (+), 3: sibling (~) */
    while (*p) {
        while (isspace((unsigned char)*p)) {
            p++;
        }
        if (!*p) {
            break;
        }
        if (*p == '>' || *p == '+' || *p == '~') {
            pending_combinator = (*p == '>') ? 1 : (*p == '+' ? 2 : 3);
            p++;
            while (isspace((unsigned char)*p)) {
                p++;
            }
            if (!*p) {
                break;
            }
        }
        svg_selector_part part;
        memset(&part, 0, sizeof(part));
        int matched = 0;
        if (*p == '*') {
            p++;
            matched = 1;
        } else if (isalpha((unsigned char)*p)) {
            const char *start = p;
            while (*p && (isalnum((unsigned char)*p) || *p == '-' || *p == '_')) {
                p++;
            }
            part.tag = svg_strndup(start, (size_t)(p - start));
            if (part.tag) {
                svg_lowercase(part.tag);
            }
            sel.specificity += 1;
            matched = 1;
        }
        while (*p == '.' || *p == '#') {
            char type = *p++;
            const char *start = p;
            while (*p && (isalnum((unsigned char)*p) || *p == '-' || *p == '_')) {
                p++;
            }
            if (p == start) {
                break;
            }
            char *name = svg_strndup(start, (size_t)(p - start));
            if (!name) {
                return 0;
            }
            matched = 1;
            if (type == '#') {
                free(part.id);
                part.id = name;
                sel.specificity += 100;
            } else {
                char **classes = (char **)realloc(part.classes, (size_t)(part.class_count + 1) * sizeof(char *));
                if (!classes) {
                    free(name);
                    return 0;
                }
                part.classes = classes;
                part.classes[part.class_count++] = name;
                sel.specificity += 10;
            }
        }
        while (*p == '[') {
            p++;
            while (isspace((unsigned char)*p)) {
                p++;
            }
            const char *nstart = p;
            while (*p && (isalnum((unsigned char)*p) || *p == '-' || *p == '_' || *p == ':')) {
                p++;
            }
            if (p == nstart) {
                break;
            }
            char *aname = svg_strndup(nstart, (size_t)(p - nstart));
            if (!aname) {
                return 0;
            }
            svg_lowercase(aname);
            while (isspace((unsigned char)*p)) {
                p++;
            }
            int has_value = 0;
            char *avalue = NULL;
            if (*p == '=') {
                has_value = 1;
                p++;
                while (isspace((unsigned char)*p)) {
                    p++;
                }
                if (*p == '"' || *p == '\'') {
                    char q = *p++;
                    const char *vstart = p;
                    while (*p && *p != q) {
                        p++;
                    }
                    avalue = svg_strndup(vstart, (size_t)(p - vstart));
                    if (*p == q) {
                        p++;
                    }
                } else {
                    const char *vstart = p;
                    while (*p && *p != ']' && !isspace((unsigned char)*p)) {
                        p++;
                    }
                    avalue = svg_strndup(vstart, (size_t)(p - vstart));
                }
                if (!avalue) {
                    free(aname);
                    return 0;
                }
            }
            while (isspace((unsigned char)*p)) {
                p++;
            }
            if (*p == ']') {
                p++;
            }
            int new_count = part.attr_count + 1;
            char **names = (char **)malloc((size_t)new_count * sizeof(char *));
            char **values = (char **)malloc((size_t)new_count * sizeof(char *));
            uint8_t *flags = (uint8_t *)malloc((size_t)new_count * sizeof(uint8_t));
            if (!names || !values || !flags) {
                free(aname);
                free(avalue);
                free(names);
                free(values);
                free(flags);
                return 0;
            }
            for (int ai = 0; ai < part.attr_count; ai++) {
                names[ai] = part.attr_names[ai];
                values[ai] = part.attr_values[ai];
                flags[ai] = part.attr_has_value[ai];
            }
            free(part.attr_names);
            free(part.attr_values);
            free(part.attr_has_value);
            part.attr_names = names;
            part.attr_values = values;
            part.attr_has_value = flags;
            part.attr_names[part.attr_count] = aname;
            part.attr_values[part.attr_count] = avalue;
            part.attr_has_value[part.attr_count] = (uint8_t)has_value;
            part.attr_count = new_count;
            sel.specificity += 10;
            matched = 1;
        }
        if (!matched) {
            break;
        }
        int old_count = sel.part_count;
        svg_selector_part *parts = (svg_selector_part *)realloc(sel.parts, (size_t)(old_count + 1) * sizeof(svg_selector_part));
        if (!parts) {
            return 0;
        }
        sel.parts = parts;
        sel.parts[old_count] = part;
        sel.part_count = old_count + 1;
        if (old_count > 0) {
            int *combs = (int *)realloc(sel.combinators, (size_t)old_count * sizeof(int));
            if (!combs) {
                return 0;
            }
            sel.combinators = combs;
            sel.combinators[old_count - 1] = pending_combinator;
        }
        pending_combinator = 0;
        int saw_space = 0;
        while (isspace((unsigned char)*p)) {
            saw_space = 1;
            p++;
        }
        if (*p == '>' || *p == '+' || *p == '~') {
            pending_combinator = (*p == '>') ? 1 : (*p == '+' ? 2 : 3);
            p++;
        } else if (saw_space) {
            pending_combinator = 0;
        }
    }
    if (sel.part_count == 0) {
        return 0;
    }
    *out = sel;
    return 1;
}

static int svg_css_parse_decls(svg_css_rule *rule, const char *s) {
    if (!rule || !s) {
        return 0;
    }
    const char *p = s;
    while (*p) {
        while (isspace((unsigned char)*p) || *p == ';') {
            p++;
        }
        if (!*p) {
            break;
        }
        const char *name_start = p;
        while (*p && *p != ':' && *p != ';') {
            p++;
        }
        if (*p != ':') {
            break;
        }
        const char *name_end = p;
        p++;
        const char *value_start = p;
        while (*p && *p != ';') {
            p++;
        }
        const char *value_end = p;
        while (name_end > name_start && isspace((unsigned char)name_end[-1])) {
            name_end--;
        }
        while (value_end > value_start && isspace((unsigned char)value_end[-1])) {
            value_end--;
        }
        if (name_end > name_start && value_end > value_start) {
            char name_buf[64];
            size_t nlen = (size_t)(name_end - name_start);
            if (nlen >= sizeof(name_buf)) {
                nlen = sizeof(name_buf) - 1;
            }
            memcpy(name_buf, name_start, nlen);
            name_buf[nlen] = '\0';
            svg_lowercase(name_buf);
            int prop = svg_property_id(name_buf);
            if (prop >= 0) {
                char *val = svg_strndup(value_start, (size_t)(value_end - value_start));
                if (!val) {
                    return 0;
                }
                svg_css_decl *decls = (svg_css_decl *)realloc(rule->decls, (size_t)(rule->decl_count + 1) * sizeof(svg_css_decl));
                if (!decls) {
                    free(val);
                    return 0;
                }
                rule->decls = decls;
                rule->decls[rule->decl_count].prop = prop;
                rule->decls[rule->decl_count].value = val;
                rule->decl_count++;
            }
        }
        if (*p == ';') {
            p++;
        }
    }
    return rule->decl_count > 0;
}

static int svg_css_add_rule(svg_css *css, svg_css_rule *rule) {
    if (!css || !rule) {
        return 0;
    }
    if (css->rule_count >= css->rule_cap) {
        int new_cap = css->rule_cap ? css->rule_cap * 2 : 16;
        svg_css_rule *n = (svg_css_rule *)realloc(css->rules, (size_t)new_cap * sizeof(svg_css_rule));
        if (!n) {
            return 0;
        }
        css->rules = n;
        css->rule_cap = new_cap;
    }
    css->rules[css->rule_count++] = *rule;
    return 1;
}

static int svg_css_parse(svg_css *css, const char *text) {
    if (!css || !text) {
        return 0;
    }
    char *clean = svg_css_strip_comments(text);
    if (!clean) {
        return 0;
    }
    char *p = clean;
    while (*p) {
        while (isspace((unsigned char)*p)) {
            p++;
        }
        if (!*p) {
            break;
        }
        char *brace = strchr(p, '{');
        if (!brace) {
            break;
        }
        char *block_end = (char *)svg_css_find_matching_brace(brace);
        if (!block_end) {
            break;
        }
        const char *selector_start = p;
        const char *selector_end = brace;
        const char *body_start = brace + 1;
        const char *body_end = block_end;

        svg_css_parse_keyframes_block(css, selector_start, selector_end, body_start, body_end);
        svg_css_parse_animation_bindings(css, selector_start, selector_end, body_start, body_end);

        char *selectors_str = svg_css_span_dup(selector_start, selector_end);
        char *decls_str = svg_css_span_dup(body_start, body_end);
        if (!selectors_str || !decls_str) {
            free(selectors_str);
            free(decls_str);
            p = block_end + 1;
            continue;
        }
        const char *ss = selectors_str;
        const char *se = selectors_str + strlen(selectors_str);
        svg_css_trim_span(&ss, &se);
        if (se > ss && *ss == '@') {
            free(selectors_str);
            free(decls_str);
            p = block_end + 1;
            continue;
        }

        svg_css_rule rule;
        memset(&rule, 0, sizeof(rule));
        rule.order = css->rule_count;
        char *sel_p = selectors_str;
        while (*sel_p) {
            while (isspace((unsigned char)*sel_p) || *sel_p == ',') {
                sel_p++;
            }
            if (!*sel_p) {
                break;
            }
            char *sel_end = sel_p;
            while (*sel_end && *sel_end != ',') {
                sel_end++;
            }
            char *sel_str = svg_strndup(sel_p, (size_t)(sel_end - sel_p));
            if (sel_str) {
                svg_selector sel;
                memset(&sel, 0, sizeof(sel));
                if (svg_css_parse_selector(sel_str, &sel)) {
                    svg_selector *sels = (svg_selector *)realloc(rule.selectors, (size_t)(rule.selector_count + 1) * sizeof(svg_selector));
                    if (sels) {
                        rule.selectors = sels;
                        rule.selectors[rule.selector_count++] = sel;
                    }
                }
                free(sel_str);
            }
            if (*sel_end == ',') {
                sel_end++;
            }
            sel_p = sel_end;
        }
        if (rule.selector_count > 0 && svg_css_parse_decls(&rule, decls_str)) {
            svg_css_add_rule(css, &rule);
        } else {
            for (int s = 0; s < rule.selector_count; s++) {
                svg_selector *sel = &rule.selectors[s];
                for (int pidx = 0; pidx < sel->part_count; pidx++) {
                    svg_selector_part *part = &sel->parts[pidx];
                    free(part->tag);
                    free(part->id);
                    for (int c = 0; c < part->class_count; c++) {
                        free(part->classes[c]);
                    }
                    free(part->classes);
                    for (int a = 0; a < part->attr_count; a++) {
                        free(part->attr_names[a]);
                        free(part->attr_values[a]);
                    }
                    free(part->attr_names);
                    free(part->attr_values);
                    free(part->attr_has_value);
                }
                free(sel->parts);
                free(sel->combinators);
            }
            free(rule.selectors);
            for (int d = 0; d < rule.decl_count; d++) {
                free(rule.decls[d].value);
            }
            free(rule.decls);
        }
        free(selectors_str);
        free(decls_str);
        p = block_end + 1;
    }
    free(clean);
    return 1;
}

static int svg_class_match(const char *class_attr, const char *class_name) {
    if (!class_attr || !class_name) {
        return 0;
    }
    const char *p = class_attr;
    while (*p) {
        while (isspace((unsigned char)*p)) {
            p++;
        }
        if (!*p) {
            break;
        }
        const char *start = p;
        while (*p && !isspace((unsigned char)*p)) {
            p++;
        }
        size_t len = (size_t)(p - start);
        if (strlen(class_name) == len && strncmp(start, class_name, len) == 0) {
            return 1;
        }
    }
    return 0;
}

static int svg_selector_part_matches(const svg_selector_part *part, const svg_elem_info *info) {
    if (!part || !info) {
        return 0;
    }
    if (part->tag && (!info->tag || strcmp(part->tag, info->tag) != 0)) {
        return 0;
    }
    if (part->id && (!info->id || strcmp(part->id, info->id) != 0)) {
        return 0;
    }
    for (int i = 0; i < part->class_count; i++) {
        if (!svg_class_match(info->class_attr, part->classes[i])) {
            return 0;
        }
    }
    for (int i = 0; i < part->attr_count; i++) {
        const char *match_value = NULL;
        int found = 0;
        for (int a = 0; a < info->attr_count; a++) {
            const char *aname = svg_local_name(info->attr_names[a]);
            if (aname && svg_strcasecmp(aname, part->attr_names[i]) == 0) {
                found = 1;
                match_value = info->attr_values[a];
                break;
            }
        }
        if (!found) {
            return 0;
        }
        if (part->attr_has_value[i]) {
            if (!match_value || strcmp(match_value, part->attr_values[i]) != 0) {
                return 0;
            }
        }
    }
    return 1;
}

static int svg_selector_matches(const svg_selector *sel, const svg_elem_info *stack, int depth,
                                const svg_elem_info *siblings, int sibling_count) {
    if (!sel || !stack || depth <= 0) {
        return 0;
    }
    int part_idx = sel->part_count - 1;
    if (!svg_selector_part_matches(&sel->parts[part_idx], &stack[depth - 1])) {
        return 0;
    }
    part_idx--;
    int ancestor_idx = depth - 2;
    while (part_idx >= 0) {
        int comb = 0;
        if (sel->combinators) {
            comb = sel->combinators[part_idx];
        }
        if (comb == 2 || comb == 3) {
            int found = 0;
            if (siblings && sibling_count > 0) {
                if (comb == 2) {
                    const svg_elem_info *prev = &siblings[sibling_count - 1];
                    found = svg_selector_part_matches(&sel->parts[part_idx], prev);
                } else {
                    for (int i = sibling_count - 1; i >= 0; i--) {
                        if (svg_selector_part_matches(&sel->parts[part_idx], &siblings[i])) {
                            found = 1;
                            break;
                        }
                    }
                }
            }
            if (!found) {
                return 0;
            }
            part_idx--;
            continue;
        }
        if (comb == 1) {
            if (ancestor_idx < 0) {
                return 0;
            }
            if (!svg_selector_part_matches(&sel->parts[part_idx], &stack[ancestor_idx])) {
                return 0;
            }
            ancestor_idx--;
            part_idx--;
            continue;
        }
        int found_idx = -1;
        for (int i = ancestor_idx; i >= 0; i--) {
            if (svg_selector_part_matches(&sel->parts[part_idx], &stack[i])) {
                found_idx = i;
                break;
            }
        }
        if (found_idx < 0) {
            return 0;
        }
        ancestor_idx = found_idx - 1;
        part_idx--;
    }
    return 1;
}

static void svg_css_apply(svg_style *style, const svg_css *css, const svg_defs *defs,
                          const svg_elem_info *stack, int depth,
                          const svg_elem_info *siblings, int sibling_count,
                          float base_len, float dpi) {
    if (!style || !css || css->rule_count == 0 || !stack || depth <= 0) {
        return;
    }
    int best_spec[SVG_PROP_COUNT];
    int best_order[SVG_PROP_COUNT];
    for (int i = 0; i < SVG_PROP_COUNT; i++) {
        best_spec[i] = -1;
        best_order[i] = -1;
    }
    for (int r = 0; r < css->rule_count; r++) {
        const svg_css_rule *rule = &css->rules[r];
        for (int s = 0; s < rule->selector_count; s++) {
            const svg_selector *sel = &rule->selectors[s];
            if (!svg_selector_matches(sel, stack, depth, siblings, sibling_count)) {
                continue;
            }
            int spec = sel->specificity;
            for (int d = 0; d < rule->decl_count; d++) {
                int prop = rule->decls[d].prop;
                if (prop < 0 || prop >= SVG_PROP_COUNT) {
                    continue;
                }
                if (spec > best_spec[prop] || (spec == best_spec[prop] && rule->order >= best_order[prop])) {
                    svg_style_apply_property_id(style, prop, rule->decls[d].value, base_len, dpi, defs);
                    best_spec[prop] = spec;
                    best_order[prop] = rule->order;
                }
            }
        }
    }
}

static char *svg_find_close_tag(char *p, const char *tag_name, char **after) {
    if (!p || !tag_name) {
        return NULL;
    }
    size_t name_len = strlen(tag_name);
    while (*p) {
        char *lt = strchr(p, '<');
        if (!lt) {
            return NULL;
        }
        if (lt[1] == '/') {
            char *q = lt + 2;
            size_t i = 0;
            for (; i < name_len && q[i]; i++) {
                if (tolower((unsigned char)q[i]) != tolower((unsigned char)tag_name[i])) {
                    break;
                }
            }
            if (i == name_len) {
                char *gt = strchr(lt, '>');
                if (gt) {
                    if (after) {
                        *after = gt + 1;
                    }
                    return lt;
                }
                return lt;
            }
        }
        p = lt + 1;
    }
    return NULL;
}

static int svg_parse_preamble(const unsigned char *data, size_t size, svg_preamble *pre, svg_defs *defs,
                              svg_css *css, float dpi, float anim_time, char *err, size_t errcap) {
    if (!data || !pre) {
        set_err(err, errcap, "invalid arguments");
        return 0;
    }
    memset(pre, 0, sizeof(*pre));
    pre->align_x = 0.5f;
    pre->align_y = 0.5f;
    svg_defs_init(defs);
    svg_css_init(css);
    char *buf = (char *)malloc(size + 1);
    if (!buf) {
        set_err(err, errcap, "out of memory");
        return 0;
    }
    memcpy(buf, data, size);
    buf[size] = '\0';
    char *p = buf;
    svg_tag tag;
    svg_gradient *current_grad = NULL;
    while (svg_next_tag(&p, &tag)) {
        const char *local = svg_local_name(tag.name);
        if (!tag.is_end) {
            svg_defs_add_use(defs, &tag);
        }
        if (!tag.is_end && !pre->found_svg && strcmp(local, "svg") == 0) {
            pre->found_svg = 1;
            const char *w = svg_get_attr(&tag, "width");
            const char *h = svg_get_attr(&tag, "height");
            pre->width_attr = svg_parse_length(w, 0.0f, dpi, &pre->width_ok);
            pre->height_attr = svg_parse_length(h, 0.0f, dpi, &pre->height_ok);
            pre->vb_ok = svg_parse_viewbox(svg_get_attr(&tag, "viewBox"), &pre->vb_x, &pre->vb_y, &pre->vb_w, &pre->vb_h);
            svg_parse_preserve_aspect_ratio(svg_get_attr(&tag, "preserveAspectRatio"),
                                            &pre->preserve_none, &pre->align_x, &pre->align_y);
        }
        if (!tag.is_end && (strcmp(local, "lineargradient") == 0 || strcmp(local, "radialgradient") == 0)) {
            const char *id = svg_get_attr(&tag, "id");
            svg_paint_type type = strcmp(local, "lineargradient") == 0 ? SVG_PAINT_LINEAR_GRADIENT : SVG_PAINT_RADIAL_GRADIENT;
            svg_gradient *g = svg_defs_add_gradient(defs, id, type);
            if (g) {
                svg_parse_gradient_tag(&tag, g);
                current_grad = g;
            } else {
                current_grad = NULL;
            }
            if (tag.is_self_closing) {
                current_grad = NULL;
            }
            continue;
        }
        if (!tag.is_end && strcmp(local, "path") == 0) {
            const char *id = svg_get_attr(&tag, "id");
            const char *d = svg_get_attr(&tag, "d");
            if (id && d) {
                svg_defs_add_path(defs, id, d);
            }
        }
        if (!tag.is_end && strcmp(local, "pattern") == 0) {
            const char *id = svg_get_attr(&tag, "id");
            svg_pattern *pat = svg_defs_add_pattern(defs, id);
            if (pat) {
                svg_parse_pattern_tag(&tag, pat);
            }
            if (!tag.is_self_closing) {
                char *after = NULL;
                char *close = svg_find_close_tag(p, "pattern", &after);
                if (close) {
                    *close = '\0';
                    if (pat) {
                        free(pat->content);
                        pat->content = svg_strdup(p);
                    }
                    if (after) {
                        p = after;
                    }
                }
            }
            continue;
        }
        if (!tag.is_end && strcmp(local, "symbol") == 0) {
            const char *id = svg_get_attr(&tag, "id");
            svg_symbol *sym = svg_defs_add_symbol(defs, id);
            if (sym) {
                svg_parse_symbol_tag(&tag, sym);
            }
            if (!tag.is_self_closing) {
                char *after = NULL;
                char *close = svg_find_close_tag(p, "symbol", &after);
                if (close) {
                    *close = '\0';
                    if (sym) {
                        free(sym->content);
                        sym->content = svg_strdup(p);
                    }
                    if (after) {
                        p = after;
                    }
                }
            }
            continue;
        }
        if (!tag.is_end && strcmp(local, "marker") == 0) {
            const char *id = svg_get_attr(&tag, "id");
            svg_marker *mk = svg_defs_add_marker(defs, id);
            if (mk) {
                float base_len = pre->vb_ok ? ((pre->vb_w + pre->vb_h) * 0.5f) : 512.0f;
                svg_parse_marker_tag(&tag, mk, base_len, dpi);
            }
            if (!tag.is_self_closing) {
                char *after = NULL;
                char *close = svg_find_close_tag(p, "marker", &after);
                if (close) {
                    *close = '\0';
                    if (mk) {
                        free(mk->content);
                        mk->content = svg_strdup(p);
                    }
                    if (after) {
                        p = after;
                    }
                }
            }
            continue;
        }
        if (!tag.is_end && strcmp(local, "clippath") == 0) {
            const char *id = svg_get_attr(&tag, "id");
            svg_clip_path *clip = svg_defs_add_clip(defs, id);
            if (clip) {
                svg_parse_clip_tag(&tag, clip);
            }
            if (!tag.is_self_closing) {
                char *after = NULL;
                char *close = svg_find_close_tag(p, "clipPath", &after);
                if (close) {
                    *close = '\0';
                    if (clip) {
                        free(clip->content);
                        clip->content = svg_strdup(p);
                    }
                    if (after) {
                        p = after;
                    }
                }
            }
            continue;
        }
        if (!tag.is_end && strcmp(local, "mask") == 0) {
            const char *id = svg_get_attr(&tag, "id");
            svg_mask *mask = svg_defs_add_mask(defs, id);
            if (mask) {
                svg_parse_mask_tag(&tag, mask);
            }
            if (!tag.is_self_closing) {
                char *after = NULL;
                char *close = svg_find_close_tag(p, "mask", &after);
                if (close) {
                    *close = '\0';
                    if (mask) {
                        free(mask->content);
                        mask->content = svg_strdup(p);
                    }
                    if (after) {
                        p = after;
                    }
                }
            }
            continue;
        }
        if (!tag.is_end && strcmp(local, "filter") == 0) {
            const char *id = svg_get_attr(&tag, "id");
            svg_filter *filter = svg_defs_add_filter(defs, id);
            if (filter) {
                svg_parse_filter_tag(&tag, filter);
            }
            if (!tag.is_self_closing) {
                char *after = NULL;
                char *close = svg_find_close_tag(p, "filter", &after);
                if (close) {
                    *close = '\0';
                    if (filter) {
                        free(filter->content);
                        filter->content = svg_strdup(p);
                    }
                    if (after) {
                        p = after;
                    }
                }
            }
            continue;
        }
        if (tag.is_end && (strcmp(local, "lineargradient") == 0 || strcmp(local, "radialgradient") == 0)) {
            current_grad = NULL;
            continue;
        }
        if (current_grad && !tag.is_end && strcmp(local, "stop") == 0) {
            float off = 0.0f;
            uint32_t color = 0;
            svg_tag stop_tag = tag;
            if (anim_time >= 0.0f) {
                svg_css_apply_animations_to_tag(&stop_tag, css, anim_time);
            }
            if (svg_stop_parse(&stop_tag, &off, &color)) {
                svg_gradient_add_stop(current_grad, off, color);
            }
            continue;
        }
        if (!tag.is_end && strcmp(local, "style") == 0 && !tag.is_self_closing) {
            char *after = NULL;
            char *close = svg_find_close_tag(p, "style", &after);
            if (close) {
                *close = '\0';
                svg_css_parse(css, p);
                if (after) {
                    p = after;
                }
            }
            continue;
        }
    }
    for (int i = 0; i < defs->gradient_count; i++) {
        svg_gradient_inherit(&defs->gradients[i], defs);
        svg_gradient_finalize(&defs->gradients[i]);
    }
    for (int i = 0; i < defs->pattern_count; i++) {
        svg_pattern_inherit(&defs->patterns[i], defs);
        svg_pattern_finalize(&defs->patterns[i]);
    }
    for (int i = 0; i < defs->clip_count; i++) {
        svg_clip_inherit(&defs->clips[i], defs);
        svg_clip_finalize(&defs->clips[i]);
    }
    for (int i = 0; i < defs->mask_count; i++) {
        svg_mask_inherit(&defs->masks[i], defs);
        svg_mask_finalize(&defs->masks[i]);
    }
    free(buf);
    if (!pre->found_svg) {
        set_err(err, errcap, "not an SVG");
        return 0;
    }
    return 1;
}

static int svg_segments_add(svg_segments *segs, float x0, float y0, float x1, float y1, int to_stroke, int to_fill) {
    if (to_stroke) {
        if (segs->stroke_count >= segs->stroke_cap) {
            size_t new_cap = segs->stroke_cap ? segs->stroke_cap * 2 : 32;
            svg_segment *n = (svg_segment *)realloc(segs->stroke, new_cap * sizeof(svg_segment));
            if (!n) {
                return 0;
            }
            segs->stroke = n;
            segs->stroke_cap = new_cap;
        }
        segs->stroke[segs->stroke_count++] = (svg_segment){x0, y0, x1, y1};
    }
    if (to_fill) {
        if (segs->fill_count >= segs->fill_cap) {
            size_t new_cap = segs->fill_cap ? segs->fill_cap * 2 : 32;
            svg_segment *n = (svg_segment *)realloc(segs->fill, new_cap * sizeof(svg_segment));
            if (!n) {
                return 0;
            }
            segs->fill = n;
            segs->fill_cap = new_cap;
        }
        segs->fill[segs->fill_count++] = (svg_segment){x0, y0, x1, y1};
    }
    if (x0 < segs->minx) segs->minx = x0;
    if (y0 < segs->miny) segs->miny = y0;
    if (x0 > segs->maxx) segs->maxx = x0;
    if (y0 > segs->maxy) segs->maxy = y0;
    if (x1 < segs->minx) segs->minx = x1;
    if (y1 < segs->miny) segs->miny = y1;
    if (x1 > segs->maxx) segs->maxx = x1;
    if (y1 > segs->maxy) segs->maxy = y1;
    return 1;
}

static void svg_segments_init(svg_segments *segs) {
    segs->stroke = NULL;
    segs->stroke_count = 0;
    segs->stroke_cap = 0;
    segs->fill = NULL;
    segs->fill_count = 0;
    segs->fill_cap = 0;
    segs->minx = 1e30f;
    segs->miny = 1e30f;
    segs->maxx = -1e30f;
    segs->maxy = -1e30f;
}

static void svg_segments_free(svg_segments *segs) {
    free(segs->stroke);
    free(segs->fill);
    segs->stroke = NULL;
    segs->fill = NULL;
    segs->stroke_count = 0;
    segs->fill_count = 0;
    segs->stroke_cap = 0;
    segs->fill_cap = 0;
}

static void svg_segments_add_arc(svg_segments *segs, float cx, float cy, float rx, float ry,
                                 float start_ang, float end_ang, int steps) {
    if (!segs || steps < 1) {
        return;
    }
    float prevx = cx + cosf(start_ang) * rx;
    float prevy = cy + sinf(start_ang) * ry;
    for (int i = 1; i <= steps; i++) {
        float t = start_ang + (end_ang - start_ang) * ((float)i / (float)steps);
        float nx = cx + cosf(t) * rx;
        float ny = cy + sinf(t) * ry;
        svg_segments_add(segs, prevx, prevy, nx, ny, 1, 0);
        prevx = nx;
        prevy = ny;
    }
}

static void svg_segments_add_rect(svg_segments *segs, float x, float y, float w, float h, float rx, float ry) {
    if (!segs || w <= 0.0f || h <= 0.0f) {
        return;
    }
    if (rx < 0.0f) rx = 0.0f;
    if (ry < 0.0f) ry = 0.0f;
    if (rx > w * 0.5f) rx = w * 0.5f;
    if (ry > h * 0.5f) ry = h * 0.5f;
    if (rx <= 0.0f || ry <= 0.0f) {
        svg_segments_add(segs, x, y, x + w, y, 1, 0);
        svg_segments_add(segs, x + w, y, x + w, y + h, 1, 0);
        svg_segments_add(segs, x + w, y + h, x, y + h, 1, 0);
        svg_segments_add(segs, x, y + h, x, y, 1, 0);
        return;
    }
    int steps = (int)lroundf(fmaxf(rx, ry) * 0.5f);
    if (steps < 4) steps = 4;
    if (steps > 24) steps = 24;
    float left = x + rx;
    float right = x + w - rx;
    float top = y + ry;
    float bottom = y + h - ry;
    svg_segments_add(segs, left, y, right, y, 1, 0);
    svg_segments_add_arc(segs, right, top, rx, ry, -0.5f * (float)M_PI, 0.0f, steps);
    svg_segments_add(segs, x + w, top, x + w, bottom, 1, 0);
    svg_segments_add_arc(segs, right, bottom, rx, ry, 0.0f, 0.5f * (float)M_PI, steps);
    svg_segments_add(segs, right, y + h, left, y + h, 1, 0);
    svg_segments_add_arc(segs, left, bottom, rx, ry, 0.5f * (float)M_PI, (float)M_PI, steps);
    svg_segments_add(segs, x, bottom, x, top, 1, 0);
    svg_segments_add_arc(segs, left, top, rx, ry, (float)M_PI, 1.5f * (float)M_PI, steps);
}

static void svg_segments_add_ellipse(svg_segments *segs, float cx, float cy, float rx, float ry) {
    if (!segs || rx <= 0.0f || ry <= 0.0f) {
        return;
    }
    int steps = (int)lroundf(fmaxf(rx, ry) * 0.75f);
    if (steps < 16) steps = 16;
    if (steps > 128) steps = 128;
    float prevx = cx + rx;
    float prevy = cy;
    for (int i = 1; i <= steps; i++) {
        float t = (float)i / (float)steps * 2.0f * (float)M_PI;
        float nx = cx + cosf(t) * rx;
        float ny = cy + sinf(t) * ry;
        svg_segments_add(segs, prevx, prevy, nx, ny, 1, 0);
        prevx = nx;
        prevy = ny;
    }
}

static void svg_segments_add_arc_rot(svg_segments *segs, float x0, float y0, float x1, float y1,
                                     float rx, float ry, float x_axis_rotation,
                                     int large_arc, int sweep) {
    if (!segs) {
        return;
    }
    if (rx <= 0.0f || ry <= 0.0f) {
        svg_segments_add(segs, x0, y0, x1, y1, 1, 1);
        return;
    }
    float phi = x_axis_rotation * (float)M_PI / 180.0f;
    float cosphi = cosf(phi);
    float sinphi = sinf(phi);
    float dx = (x0 - x1) * 0.5f;
    float dy = (y0 - y1) * 0.5f;
    float x1p = cosphi * dx + sinphi * dy;
    float y1p = -sinphi * dx + cosphi * dy;
    rx = fabsf(rx);
    ry = fabsf(ry);
    float rx2 = rx * rx;
    float ry2 = ry * ry;
    float x1p2 = x1p * x1p;
    float y1p2 = y1p * y1p;
    float lambda = x1p2 / rx2 + y1p2 / ry2;
    if (lambda > 1.0f) {
        float scale = sqrtf(lambda);
        rx *= scale;
        ry *= scale;
        rx2 = rx * rx;
        ry2 = ry * ry;
    }
    float denom = rx2 * y1p2 + ry2 * x1p2;
    if (denom <= 1e-12f) {
        svg_segments_add(segs, x0, y0, x1, y1, 1, 1);
        return;
    }
    float sign = (large_arc == sweep) ? -1.0f : 1.0f;
    float numer = rx2 * ry2 - rx2 * y1p2 - ry2 * x1p2;
    if (numer < 0.0f) {
        numer = 0.0f;
    }
    float factor = sign * sqrtf(numer / denom);
    float cxp = factor * (rx * y1p / ry);
    float cyp = factor * (-ry * x1p / rx);
    float cx = cosphi * cxp - sinphi * cyp + (x0 + x1) * 0.5f;
    float cy = sinphi * cxp + cosphi * cyp + (y0 + y1) * 0.5f;
    float ux = (x1p - cxp) / rx;
    float uy = (y1p - cyp) / ry;
    float vx = (-x1p - cxp) / rx;
    float vy = (-y1p - cyp) / ry;
    float start_ang = atan2f(uy, ux);
    float delta = atan2f(ux * vy - uy * vx, ux * vx + uy * vy);
    if (!sweep && delta > 0.0f) {
        delta -= 2.0f * (float)M_PI;
    } else if (sweep && delta < 0.0f) {
        delta += 2.0f * (float)M_PI;
    }
    int segments = (int)ceilf(fabsf(delta) / (float)(M_PI / 8.0f));
    if (segments < 1) segments = 1;
    float prevx = x0;
    float prevy = y0;
    for (int i = 1; i <= segments; i++) {
        float ang = start_ang + delta * ((float)i / (float)segments);
        float cosang = cosf(ang);
        float sinang = sinf(ang);
        float px = cx + cosphi * rx * cosang - sinphi * ry * sinang;
        float py = cy + sinphi * rx * cosang + cosphi * ry * sinang;
        svg_segments_add(segs, prevx, prevy, px, py, 1, 1);
        prevx = px;
        prevy = py;
    }
}

static float svg_point_segment_distance(float px, float py, float x0, float y0, float x1, float y1) {
    float dx = x1 - x0;
    float dy = y1 - y0;
    float denom = dx * dx + dy * dy;
    if (denom <= 1e-12f) {
        float ux = px - x0;
        float uy = py - y0;
        return sqrtf(ux * ux + uy * uy);
    }
    float t = ((px - x0) * dx + (py - y0) * dy) / denom;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    float lx = x0 + t * dx;
    float ly = y0 + t * dy;
    float ux = px - lx;
    float uy = py - ly;
    return sqrtf(ux * ux + uy * uy);
}

static float svg_point_segment_distance_cap(float px, float py, float x0, float y0, float x1, float y1,
                                            float half, int start_cap, int end_cap) {
    float dx = x1 - x0;
    float dy = y1 - y0;
    float len2 = dx * dx + dy * dy;
    if (len2 <= 1e-12f) {
        float ux = px - x0;
        float uy = py - y0;
        return sqrtf(ux * ux + uy * uy);
    }
    float len = sqrtf(len2);
    float t = ((px - x0) * dx + (py - y0) * dy) / len2;
    float min_t = 0.0f;
    float max_t = 1.0f;
    if (start_cap == SVG_LINECAP_SQUARE) {
        min_t = -half / len;
    }
    if (end_cap == SVG_LINECAP_SQUARE) {
        max_t = 1.0f + half / len;
    }
    if (start_cap == SVG_LINECAP_BUTT && t < 0.0f) {
        return 1e30f;
    }
    if (end_cap == SVG_LINECAP_BUTT && t > 1.0f) {
        return 1e30f;
    }
    if (t < min_t) t = min_t;
    if (t > max_t) t = max_t;
    float lx = x0 + t * dx;
    float ly = y0 + t * dy;
    float ux = px - lx;
    float uy = py - ly;
    return sqrtf(ux * ux + uy * uy);
}

static int svg_point_in_segments(const svg_segments *segs, int evenodd, float px, float py) {
    int winding = 0;
    int crossings = 0;
    for (size_t i = 0; i < segs->fill_count; i++) {
        float x0 = segs->fill[i].x0;
        float y0 = segs->fill[i].y0;
        float x1 = segs->fill[i].x1;
        float y1 = segs->fill[i].y1;
        if (y0 == y1) {
            continue;
        }
        int cond = (y0 <= py && y1 > py) || (y1 <= py && y0 > py);
        if (!cond) {
            continue;
        }
        float xint = x0 + (py - y0) * (x1 - x0) / (y1 - y0);
        if (xint > px) {
            if (evenodd) {
                crossings++;
            } else {
                winding += (y1 > y0) ? 1 : -1;
            }
        }
    }
    if (evenodd) {
        return (crossings & 1) != 0;
    }
    return winding != 0;
}

static int svg_point_on_segments(const svg_segments *segs, float px, float py, float half_width) {
    float min_dist = 1e30f;
    for (size_t i = 0; i < segs->stroke_count; i++) {
        float d = svg_point_segment_distance(px, py, segs->stroke[i].x0, segs->stroke[i].y0,
                                             segs->stroke[i].x1, segs->stroke[i].y1);
        if (d < min_dist) {
            min_dist = d;
        }
    }
    return min_dist <= half_width;
}

static void svg_segments_endpoint_flags(const svg_segments *segs, uint8_t *start_open, uint8_t *end_open) {
    const float eps = 1e-3f;
    for (size_t i = 0; i < segs->stroke_count; i++) {
        int scount = 0;
        int ecount = 0;
        float sx = segs->stroke[i].x0;
        float sy = segs->stroke[i].y0;
        float ex = segs->stroke[i].x1;
        float ey = segs->stroke[i].y1;
        for (size_t j = 0; j < segs->stroke_count; j++) {
            float ax = segs->stroke[j].x0;
            float ay = segs->stroke[j].y0;
            float bx = segs->stroke[j].x1;
            float by = segs->stroke[j].y1;
            if (fabsf(sx - ax) <= eps && fabsf(sy - ay) <= eps) scount++;
            else if (fabsf(sx - bx) <= eps && fabsf(sy - by) <= eps) scount++;
            if (fabsf(ex - ax) <= eps && fabsf(ey - ay) <= eps) ecount++;
            else if (fabsf(ex - bx) <= eps && fabsf(ey - by) <= eps) ecount++;
        }
        start_open[i] = (scount <= 1);
        end_open[i] = (ecount <= 1);
    }
}

static int svg_point_in_triangle(float px, float py, const svg_join_tri *tri) {
    float d1 = (px - tri->bx) * (tri->ay - tri->by) - (tri->ax - tri->bx) * (py - tri->by);
    float d2 = (px - tri->cx) * (tri->by - tri->cy) - (tri->bx - tri->cx) * (py - tri->cy);
    float d3 = (px - tri->ax) * (tri->cy - tri->ay) - (tri->cx - tri->ax) * (py - tri->ay);
    int has_neg = (d1 < 0.0f) || (d2 < 0.0f) || (d3 < 0.0f);
    int has_pos = (d1 > 0.0f) || (d2 > 0.0f) || (d3 > 0.0f);
    return !(has_neg && has_pos);
}

static int svg_join_tri_add(svg_join_tri **tris, int *count, int *cap,
                            float ax, float ay, float bx, float by, float cx, float cy) {
    if (!tris || !count || !cap) {
        return 0;
    }
    if (*count >= *cap) {
        int new_cap = *cap ? *cap * 2 : 16;
        svg_join_tri *n = (svg_join_tri *)realloc(*tris, (size_t)new_cap * sizeof(svg_join_tri));
        if (!n) {
            return 0;
        }
        *tris = n;
        *cap = new_cap;
    }
    svg_join_tri *t = &(*tris)[(*count)++];
    t->ax = ax; t->ay = ay;
    t->bx = bx; t->by = by;
    t->cx = cx; t->cy = cy;
    return 1;
}

static void svg_build_join_tris(const svg_segments *segs, const svg_style *style,
                                const uint8_t *start_open, const uint8_t *end_open,
                                svg_join_tri **out_tris, int *out_count) {
    if (!segs || !style || !out_tris || !out_count) {
        return;
    }
    *out_tris = NULL;
    *out_count = 0;
    if (style->stroke_linejoin == SVG_LINEJOIN_ROUND || segs->stroke_count < 2) {
        return;
    }
    float half = style->stroke_width * 0.5f;
    if (half <= 0.0f) {
        return;
    }
    const float eps = 1e-3f;
    int cap = 0;
    for (size_t i = 0; i < segs->stroke_count; i++) {
        for (int endpoint = 0; endpoint < 2; endpoint++) {
            if (endpoint == 0 && start_open && start_open[i]) {
                continue;
            }
            if (endpoint == 1 && end_open && end_open[i]) {
                continue;
            }
            float jx = endpoint == 0 ? segs->stroke[i].x0 : segs->stroke[i].x1;
            float jy = endpoint == 0 ? segs->stroke[i].y0 : segs->stroke[i].y1;
            int found = 0;
            size_t j_idx = 0;
            int j_endpoint = 0;
            for (size_t j = i + 1; j < segs->stroke_count && !found; j++) {
                float sx = segs->stroke[j].x0;
                float sy = segs->stroke[j].y0;
                float ex = segs->stroke[j].x1;
                float ey = segs->stroke[j].y1;
                if (fabsf(jx - sx) <= eps && fabsf(jy - sy) <= eps) {
                    if (!start_open || !start_open[j]) {
                        found = 1;
                        j_idx = j;
                        j_endpoint = 0;
                    }
                } else if (fabsf(jx - ex) <= eps && fabsf(jy - ey) <= eps) {
                    if (!end_open || !end_open[j]) {
                        found = 1;
                        j_idx = j;
                        j_endpoint = 1;
                    }
                }
            }
            if (!found) {
                continue;
            }
            float d1x = endpoint == 0 ? (segs->stroke[i].x1 - jx) : (segs->stroke[i].x0 - jx);
            float d1y = endpoint == 0 ? (segs->stroke[i].y1 - jy) : (segs->stroke[i].y0 - jy);
            float d2x = j_endpoint == 0 ? (segs->stroke[j_idx].x1 - jx) : (segs->stroke[j_idx].x0 - jx);
            float d2y = j_endpoint == 0 ? (segs->stroke[j_idx].y1 - jy) : (segs->stroke[j_idx].y0 - jy);
            float len1 = sqrtf(d1x * d1x + d1y * d1y);
            float len2 = sqrtf(d2x * d2x + d2y * d2y);
            if (len1 <= 1e-6f || len2 <= 1e-6f) {
                continue;
            }
            d1x /= len1; d1y /= len1;
            d2x /= len2; d2y /= len2;
            float cross = d1x * d2y - d1y * d2x;
            if (fabsf(cross) <= 1e-6f) {
                continue;
            }
            float sign = cross >= 0.0f ? 1.0f : -1.0f;
            float n1x = -d1y * sign;
            float n1y = d1x * sign;
            float n2x = -d2y * sign;
            float n2y = d2x * sign;
            float p1x = jx + n1x * half;
            float p1y = jy + n1y * half;
            float p2x = jx + n2x * half;
            float p2y = jy + n2y * half;

            float ax = p1x, ay = p1y;
            float bx = p2x, by = p2y;
            float cx = jx, cy = jy;
            if (style->stroke_linejoin == SVG_LINEJOIN_MITER) {
                float det = d1x * d2y - d1y * d2x;
                if (fabsf(det) > 1e-6f) {
                    float rx = p2x - p1x;
                    float ry = p2y - p1y;
                    float t = (rx * d2y - ry * d2x) / det;
                    float mx = p1x + d1x * t;
                    float my = p1y + d1y * t;
                    float mlen = sqrtf((mx - jx) * (mx - jx) + (my - jy) * (my - jy));
                    if (mlen <= style->stroke_miterlimit * half) {
                        cx = mx;
                        cy = my;
                    }
                }
            }
            svg_join_tri_add(out_tris, out_count, &cap, ax, ay, bx, by, cx, cy);
        }
    }
}

static int svg_point_on_segments_cap(const svg_segments *segs, const svg_style *style, float px, float py,
                                     float half_width, const uint8_t *start_open, const uint8_t *end_open,
                                     const svg_join_tri *joins, int join_count) {
    float min_dist = 1e30f;
    for (size_t i = 0; i < segs->stroke_count; i++) {
        int join_cap = SVG_LINECAP_BUTT;
        if (style->stroke_linejoin == SVG_LINEJOIN_ROUND) {
            join_cap = SVG_LINECAP_ROUND;
        }
        int start_cap = start_open && start_open[i] ? style->stroke_linecap : join_cap;
        int end_cap = end_open && end_open[i] ? style->stroke_linecap : join_cap;
        float d = svg_point_segment_distance_cap(px, py, segs->stroke[i].x0, segs->stroke[i].y0,
                                                 segs->stroke[i].x1, segs->stroke[i].y1,
                                                 half_width, start_cap, end_cap);
        if (d < min_dist) {
            min_dist = d;
        }
    }
    if (min_dist <= half_width) {
        return 1;
    }
    for (int i = 0; i < join_count; i++) {
        if (svg_point_in_triangle(px, py, &joins[i])) {
            return 1;
        }
    }
    return 0;
}

static int svg_segments_build_dashed(const svg_segments *src, const svg_style *style, svg_segments *out) {
    if (!src || !style || !out || src->stroke_count == 0 || style->stroke_dashcount == 0) {
        return 0;
    }
    float dash[SVG_MAX_DASH];
    int count = style->stroke_dashcount;
    if (count > SVG_MAX_DASH) {
        count = SVG_MAX_DASH;
    }
    float pattern_len = 0.0f;
    for (int i = 0; i < count; i++) {
        dash[i] = style->stroke_dasharray[i];
        if (dash[i] < 0.0f) {
            dash[i] = 0.0f;
        }
        pattern_len += dash[i];
    }
    if (pattern_len <= 0.0f) {
        return 0;
    }
    svg_segments_init(out);
    float offset = style->stroke_dashoffset;
    if (pattern_len > 0.0f) {
        offset = fmodf(offset, pattern_len);
        if (offset < 0.0f) {
            offset += pattern_len;
        }
    }
    int idx = 0;
    int draw = 1;
    float remaining = dash[0];
    int guard = 0;
    while (remaining <= 1e-6f && guard < count) {
        idx = (idx + 1) % count;
        draw = !draw;
        remaining = dash[idx];
        guard++;
    }
    if (guard >= count) {
        return 0;
    }
    while (offset > remaining && remaining > 0.0f) {
        offset -= remaining;
        idx = (idx + 1) % count;
        draw = !draw;
        remaining = dash[idx];
        guard = 0;
        while (remaining <= 1e-6f && guard < count) {
            idx = (idx + 1) % count;
            draw = !draw;
            remaining = dash[idx];
            guard++;
        }
        if (guard >= count) {
            return 0;
        }
    }
    remaining -= offset;
    for (size_t i = 0; i < src->stroke_count; i++) {
        float x0 = src->stroke[i].x0;
        float y0 = src->stroke[i].y0;
        float x1 = src->stroke[i].x1;
        float y1 = src->stroke[i].y1;
        float dx = x1 - x0;
        float dy = y1 - y0;
        float seg_len = sqrtf(dx * dx + dy * dy);
        if (seg_len <= 1e-6f) {
            continue;
        }
        float t0 = 0.0f;
        float seg_rem = seg_len;
        while (seg_rem > 1e-6f) {
            float take = remaining < seg_rem ? remaining : seg_rem;
            float t1 = t0 + take / seg_len;
            if (draw && take > 1e-6f) {
                float sx = x0 + dx * t0;
                float sy = y0 + dy * t0;
                float ex = x0 + dx * t1;
                float ey = y0 + dy * t1;
                svg_segments_add(out, sx, sy, ex, ey, 1, 0);
            }
            t0 = t1;
            seg_rem -= take;
            remaining -= take;
            if (remaining <= 1e-6f) {
                idx = (idx + 1) % count;
                draw = !draw;
                remaining = dash[idx];
                guard = 0;
                while (remaining <= 1e-6f && guard < count) {
                    idx = (idx + 1) % count;
                    draw = !draw;
                    remaining = dash[idx];
                    guard++;
                }
                if (guard >= count) {
                    return out->stroke_count > 0;
                }
            }
        }
    }
    return out->stroke_count > 0;
}

static void svg_transform_bounds(const svg_matrix *m, float minx, float miny, float maxx, float maxy,
                                 float *out_minx, float *out_miny, float *out_maxx, float *out_maxy) {
    float x0 = minx, y0 = miny;
    float x1 = maxx, y1 = miny;
    float x2 = maxx, y2 = maxy;
    float x3 = minx, y3 = maxy;
    svg_matrix_transform_point(m, &x0, &y0);
    svg_matrix_transform_point(m, &x1, &y1);
    svg_matrix_transform_point(m, &x2, &y2);
    svg_matrix_transform_point(m, &x3, &y3);
    float mnx = fminf(fminf(x0, x1), fminf(x2, x3));
    float mny = fminf(fminf(y0, y1), fminf(y2, y3));
    float mxx = fmaxf(fmaxf(x0, x1), fmaxf(x2, x3));
    float mxy = fmaxf(fmaxf(y0, y1), fmaxf(y2, y3));
    *out_minx = mnx;
    *out_miny = mny;
    *out_maxx = mxx;
    *out_maxy = mxy;
}

static float svg_apply_spread(float t, svg_spread_method spread) {
    if (spread == SVG_SPREAD_REPEAT) {
        t = t - floorf(t);
    } else if (spread == SVG_SPREAD_REFLECT) {
        t = fmodf(t, 2.0f);
        if (t < 0.0f) {
            t += 2.0f;
        }
        if (t > 1.0f) {
            t = 2.0f - t;
        }
    } else {
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
    }
    return t;
}

static uint32_t svg_gradient_stop_color(const svg_gradient *g, float t) {
    if (!g || g->stop_count == 0) {
        return 0x00000000u;
    }
    if (t <= g->stops[0].offset) {
        return g->stops[0].color;
    }
    if (t >= g->stops[g->stop_count - 1].offset) {
        return g->stops[g->stop_count - 1].color;
    }
    for (int i = 0; i < g->stop_count - 1; i++) {
        float o0 = g->stops[i].offset;
        float o1 = g->stops[i + 1].offset;
        if (t >= o0 && t <= o1) {
            float span = o1 - o0;
            float u = span > 0.0f ? (t - o0) / span : 0.0f;
            uint32_t c0 = g->stops[i].color;
            uint32_t c1 = g->stops[i + 1].color;
            uint8_t r0 = (uint8_t)(c0 >> 24);
            uint8_t g0 = (uint8_t)(c0 >> 16);
            uint8_t b0 = (uint8_t)(c0 >> 8);
            uint8_t a0 = (uint8_t)(c0 & 0xFFu);
            uint8_t r1 = (uint8_t)(c1 >> 24);
            uint8_t g1 = (uint8_t)(c1 >> 16);
            uint8_t b1 = (uint8_t)(c1 >> 8);
            uint8_t a1 = (uint8_t)(c1 & 0xFFu);
            uint8_t r = (uint8_t)lroundf(r0 + (r1 - r0) * u);
            uint8_t gg = (uint8_t)lroundf(g0 + (g1 - g0) * u);
            uint8_t b = (uint8_t)lroundf(b0 + (b1 - b0) * u);
            uint8_t a = (uint8_t)lroundf(a0 + (a1 - a0) * u);
            return ((uint32_t)r << 24) | ((uint32_t)gg << 16) | ((uint32_t)b << 8) | (uint32_t)a;
        }
    }
    return g->stops[g->stop_count - 1].color;
}

static float svg_resolve_grad_coord(const svg_grad_coord *c, float base, float origin, svg_gradient_units units) {
    if (!c) {
        return origin;
    }
    if (units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX) {
        return origin + c->value * base;
    }
    if (c->is_percent) {
        return origin + c->value * base;
    }
    return c->value;
}

static int svg_matrix_to_string(const svg_matrix *m, char *buf, size_t cap) {
    if (!m || !buf || cap == 0) {
        return 0;
    }
    int n = snprintf(buf, cap, "matrix(%.6g %.6g %.6g %.6g %.6g %.6g)",
                     m->a, m->b, m->c, m->d, m->e, m->f);
    if (n < 0 || (size_t)n >= cap) {
        return 0;
    }
    return 1;
}

static int svg_clip_prepare(svg_clip_path *clip, const svg_render_ctx *ctx, const svg_bbox *bbox) {
    if (!clip || !ctx || !bbox || !clip->content || !*clip->content) {
        return 0;
    }
    if (clip->rendering) {
        return 0;
    }
    uint32_t out_w = ctx->width * ctx->ss;
    uint32_t out_h = ctx->height * ctx->ss;
    int needs_bbox = (clip->units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX);
    if (clip->rendered && clip->rgba && clip->mask_w == out_w && clip->mask_h == out_h) {
        if (!needs_bbox) {
            return 1;
        }
        if (clip->has_cached_bbox &&
            fabsf(clip->cached_bbox_x - bbox->x) < 1e-4f &&
            fabsf(clip->cached_bbox_y - bbox->y) < 1e-4f &&
            fabsf(clip->cached_bbox_w - bbox->w) < 1e-4f &&
            fabsf(clip->cached_bbox_h - bbox->h) < 1e-4f) {
            return 1;
        }
    }
    free(clip->rgba);
    clip->rgba = NULL;
    clip->rendered = 0;
    clip->rendering = 1;

    char header[256];
    int header_len = snprintf(header, sizeof(header),
                              "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%u\" height=\"%u\" "
                              "viewBox=\"%.6g %.6g %.6g %.6g\">",
                              out_w, out_h, ctx->vb_x, ctx->vb_y, ctx->vb_w, ctx->vb_h);
    if (header_len < 0 || header_len >= (int)sizeof(header)) {
        clip->rendering = 0;
        return 0;
    }
    char g1[192];
    int g1_len = 0;
    if (clip->has_transform) {
        char mat[96];
        if (svg_matrix_to_string(&clip->transform, mat, sizeof(mat))) {
            g1_len = snprintf(g1, sizeof(g1), "<g transform=\"%s\">", mat);
            if (g1_len < 0 || g1_len >= (int)sizeof(g1)) {
                clip->rendering = 0;
                return 0;
            }
        }
    }
    char g2[192];
    int g2_len = 0;
    if (needs_bbox) {
        float bw = bbox->w > 0.0f ? bbox->w : 1.0f;
        float bh = bbox->h > 0.0f ? bbox->h : 1.0f;
        g2_len = snprintf(g2, sizeof(g2),
                          "<g transform=\"translate(%.6g %.6g) scale(%.6g %.6g)\">",
                          bbox->x, bbox->y, bw, bh);
        if (g2_len < 0 || g2_len >= (int)sizeof(g2)) {
            clip->rendering = 0;
            return 0;
        }
    }
    const char *g2_close = needs_bbox ? "</g>" : "";
    const char *g1_close = (g1_len > 0) ? "</g>" : "";
    const char *footer = "</svg>";
    size_t content_len = strlen(clip->content);
    size_t total = (size_t)header_len + (size_t)g1_len + (size_t)g2_len + content_len +
                   strlen(g2_close) + strlen(g1_close) + strlen(footer) + 1u;
    char *svg = (char *)malloc(total);
    if (!svg) {
        clip->rendering = 0;
        return 0;
    }
    char *dst = svg;
    memcpy(dst, header, (size_t)header_len);
    dst += header_len;
    if (g1_len > 0) {
        memcpy(dst, g1, (size_t)g1_len);
        dst += g1_len;
    }
    if (g2_len > 0) {
        memcpy(dst, g2, (size_t)g2_len);
        dst += g2_len;
    }
    memcpy(dst, clip->content, content_len);
    dst += content_len;
    if (needs_bbox) {
        memcpy(dst, g2_close, strlen(g2_close));
        dst += strlen(g2_close);
    }
    if (g1_len > 0) {
        memcpy(dst, g1_close, strlen(g1_close));
        dst += strlen(g1_close);
    }
    memcpy(dst, footer, strlen(footer));
    dst += strlen(footer);
    *dst = '\0';

    cupidimage_svg_options opts;
    opts.width = out_w;
    opts.height = out_h;
    opts.scale = 1.0f;
    opts.dpi = ctx->dpi;
    opts.animation_time = ctx->anim_time;
    opts.supersampling = 1;
    opts.background_alpha = 0;

    svg_render_ctx clip_ctx;
    memset(&clip_ctx, 0, sizeof(clip_ctx));
    char errbuf[128];
    int ok = svg_render(&clip_ctx, (const unsigned char *)svg, strlen(svg), errbuf, sizeof(errbuf), &opts);
    free(svg);
    if (!ok) {
        clip->rendering = 0;
        return 0;
    }
    clip->rgba = clip_ctx.hi_rgba;
    clip->mask_w = clip_ctx.width * clip_ctx.ss;
    clip->mask_h = clip_ctx.height * clip_ctx.ss;
    clip->rendered = 1;
    clip->rendering = 0;
    if (needs_bbox) {
        clip->cached_bbox_x = bbox->x;
        clip->cached_bbox_y = bbox->y;
        clip->cached_bbox_w = bbox->w;
        clip->cached_bbox_h = bbox->h;
        clip->has_cached_bbox = 1;
    } else {
        clip->has_cached_bbox = 0;
    }
    return clip->rgba != NULL;
}

static int svg_mask_prepare(svg_mask *mask, const svg_render_ctx *ctx, const svg_bbox *bbox) {
    if (!mask || !ctx || !bbox || !mask->content || !*mask->content) {
        return 0;
    }
    if (mask->rendering) {
        return 0;
    }
    uint32_t out_w = ctx->width * ctx->ss;
    uint32_t out_h = ctx->height * ctx->ss;
    int needs_bbox = (mask->content_units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX);
    if (mask->rendered && mask->rgba && mask->mask_w == out_w && mask->mask_h == out_h) {
        if (!needs_bbox) {
            return 1;
        }
        if (mask->has_cached_bbox &&
            fabsf(mask->cached_bbox_x - bbox->x) < 1e-4f &&
            fabsf(mask->cached_bbox_y - bbox->y) < 1e-4f &&
            fabsf(mask->cached_bbox_w - bbox->w) < 1e-4f &&
            fabsf(mask->cached_bbox_h - bbox->h) < 1e-4f) {
            return 1;
        }
    }
    free(mask->rgba);
    mask->rgba = NULL;
    mask->rendered = 0;
    mask->rendering = 1;

    char header[256];
    int header_len = snprintf(header, sizeof(header),
                              "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%u\" height=\"%u\" "
                              "viewBox=\"%.6g %.6g %.6g %.6g\">",
                              out_w, out_h, ctx->vb_x, ctx->vb_y, ctx->vb_w, ctx->vb_h);
    if (header_len < 0 || header_len >= (int)sizeof(header)) {
        mask->rendering = 0;
        return 0;
    }
    char g1[192];
    int g1_len = 0;
    if (mask->has_transform) {
        char mat[96];
        if (svg_matrix_to_string(&mask->transform, mat, sizeof(mat))) {
            g1_len = snprintf(g1, sizeof(g1), "<g transform=\"%s\">", mat);
            if (g1_len < 0 || g1_len >= (int)sizeof(g1)) {
                mask->rendering = 0;
                return 0;
            }
        }
    }
    char g2[192];
    int g2_len = 0;
    if (needs_bbox) {
        float bw = bbox->w > 0.0f ? bbox->w : 1.0f;
        float bh = bbox->h > 0.0f ? bbox->h : 1.0f;
        g2_len = snprintf(g2, sizeof(g2),
                          "<g transform=\"translate(%.6g %.6g) scale(%.6g %.6g)\">",
                          bbox->x, bbox->y, bw, bh);
        if (g2_len < 0 || g2_len >= (int)sizeof(g2)) {
            mask->rendering = 0;
            return 0;
        }
    }
    const char *g2_close = needs_bbox ? "</g>" : "";
    const char *g1_close = (g1_len > 0) ? "</g>" : "";
    const char *footer = "</svg>";
    size_t content_len = strlen(mask->content);
    size_t total = (size_t)header_len + (size_t)g1_len + (size_t)g2_len + content_len +
                   strlen(g2_close) + strlen(g1_close) + strlen(footer) + 1u;
    char *svg = (char *)malloc(total);
    if (!svg) {
        mask->rendering = 0;
        return 0;
    }
    char *dst = svg;
    memcpy(dst, header, (size_t)header_len);
    dst += header_len;
    if (g1_len > 0) {
        memcpy(dst, g1, (size_t)g1_len);
        dst += g1_len;
    }
    if (g2_len > 0) {
        memcpy(dst, g2, (size_t)g2_len);
        dst += g2_len;
    }
    memcpy(dst, mask->content, content_len);
    dst += content_len;
    if (needs_bbox) {
        memcpy(dst, g2_close, strlen(g2_close));
        dst += strlen(g2_close);
    }
    if (g1_len > 0) {
        memcpy(dst, g1_close, strlen(g1_close));
        dst += strlen(g1_close);
    }
    memcpy(dst, footer, strlen(footer));
    dst += strlen(footer);
    *dst = '\0';

    cupidimage_svg_options opts;
    opts.width = out_w;
    opts.height = out_h;
    opts.scale = 1.0f;
    opts.dpi = ctx->dpi;
    opts.animation_time = ctx->anim_time;
    opts.supersampling = 1;
    opts.background_alpha = 0;

    svg_render_ctx mask_ctx;
    memset(&mask_ctx, 0, sizeof(mask_ctx));
    char errbuf[128];
    int ok = svg_render(&mask_ctx, (const unsigned char *)svg, strlen(svg), errbuf, sizeof(errbuf), &opts);
    free(svg);
    if (!ok) {
        mask->rendering = 0;
        return 0;
    }
    mask->rgba = mask_ctx.hi_rgba;
    mask->mask_w = mask_ctx.width * mask_ctx.ss;
    mask->mask_h = mask_ctx.height * mask_ctx.ss;
    mask->rendered = 1;
    mask->rendering = 0;
    if (needs_bbox) {
        mask->cached_bbox_x = bbox->x;
        mask->cached_bbox_y = bbox->y;
        mask->cached_bbox_w = bbox->w;
        mask->cached_bbox_h = bbox->h;
        mask->has_cached_bbox = 1;
    } else {
        mask->has_cached_bbox = 0;
    }
    return mask->rgba != NULL;
}

static uint8_t svg_sample_mask_alpha(svg_style *style, const svg_render_ctx *ctx, const svg_bbox *bbox,
                                     int sx, int sy) {
    if (!style || !ctx || !bbox) {
        return 255;
    }
    uint8_t alpha = 255;
    if (style->clip_path) {
        if (svg_clip_prepare(style->clip_path, ctx, bbox)) {
            if (sx < 0 || sy < 0 || (uint32_t)sx >= style->clip_path->mask_w ||
                (uint32_t)sy >= style->clip_path->mask_h) {
                return 0;
            }
            size_t idx = ((size_t)sy * style->clip_path->mask_w + (size_t)sx) * 4u;
            alpha = style->clip_path->rgba[idx + 3];
        }
    }
    if (style->mask) {
        if (svg_mask_prepare(style->mask, ctx, bbox)) {
            if (sx < 0 || sy < 0 || (uint32_t)sx >= style->mask->mask_w ||
                (uint32_t)sy >= style->mask->mask_h) {
                return 0;
            }
            size_t idx = ((size_t)sy * style->mask->mask_w + (size_t)sx) * 4u;
            uint8_t ma = style->mask->rgba[idx + 3];
            if (style->mask->type == SVG_MASK_TYPE_LUMINANCE) {
                uint8_t mr = style->mask->rgba[idx + 0];
                uint8_t mg = style->mask->rgba[idx + 1];
                uint8_t mb = style->mask->rgba[idx + 2];
                uint8_t lum = (uint8_t)((mr * 54u + mg * 183u + mb * 19u + 128u) >> 8);
                ma = (uint8_t)((lum * ma) / 255u);
            }
            alpha = (uint8_t)((alpha * ma) / 255u);
        }
    }
    return alpha;
}

static void svg_gaussian_blur(uint8_t *buf, uint32_t w, uint32_t h, float sigma_x, float sigma_y) {
    if (!buf || w == 0 || h == 0) {
        return;
    }
    if (sigma_x < 0.01f && sigma_y < 0.01f) {
        return;
    }
    uint8_t *tmp = (uint8_t *)malloc((size_t)w * (size_t)h * 4u);
    if (!tmp) {
        return;
    }
    const int max_radius = 96;
    if (sigma_x >= 0.01f) {
        int radius = (int)ceilf(3.0f * sigma_x);
        if (radius < 1) radius = 1;
        if (radius > max_radius) radius = max_radius;
        float sigma_eff = sigma_x;
        float max_sigma = (float)radius / 3.0f;
        if (sigma_eff > max_sigma) sigma_eff = max_sigma;
        int klen = radius * 2 + 1;
        float *kernel = (float *)malloc((size_t)klen * sizeof(float));
        if (!kernel) {
            free(tmp);
            return;
        }
        float sum = 0.0f;
        for (int i = -radius; i <= radius; i++) {
            float v = expf(-0.5f * (float)(i * i) / (sigma_eff * sigma_eff));
            kernel[i + radius] = v;
            sum += v;
        }
        if (sum > 0.0f) {
            for (int i = 0; i < klen; i++) {
                kernel[i] /= sum;
            }
        }
        for (uint32_t y = 0; y < h; y++) {
            for (uint32_t x = 0; x < w; x++) {
                float acc[4] = {0, 0, 0, 0};
                for (int k = -radius; k <= radius; k++) {
                    int sx = (int)x + k;
                    if (sx < 0) sx = 0;
                    if (sx >= (int)w) sx = (int)w - 1;
                    size_t idx = ((size_t)y * w + (size_t)sx) * 4u;
                    float wgt = kernel[k + radius];
                    acc[0] += buf[idx + 0] * wgt;
                    acc[1] += buf[idx + 1] * wgt;
                    acc[2] += buf[idx + 2] * wgt;
                    acc[3] += buf[idx + 3] * wgt;
                }
                size_t out = ((size_t)y * w + (size_t)x) * 4u;
                tmp[out + 0] = (uint8_t)lroundf(fminf(fmaxf(acc[0], 0.0f), 255.0f));
                tmp[out + 1] = (uint8_t)lroundf(fminf(fmaxf(acc[1], 0.0f), 255.0f));
                tmp[out + 2] = (uint8_t)lroundf(fminf(fmaxf(acc[2], 0.0f), 255.0f));
                tmp[out + 3] = (uint8_t)lroundf(fminf(fmaxf(acc[3], 0.0f), 255.0f));
            }
        }
        memcpy(buf, tmp, (size_t)w * (size_t)h * 4u);
        free(kernel);
    }
    if (sigma_y >= 0.01f) {
        int radius = (int)ceilf(3.0f * sigma_y);
        if (radius < 1) radius = 1;
        if (radius > max_radius) radius = max_radius;
        float sigma_eff = sigma_y;
        float max_sigma = (float)radius / 3.0f;
        if (sigma_eff > max_sigma) sigma_eff = max_sigma;
        int klen = radius * 2 + 1;
        float *kernel = (float *)malloc((size_t)klen * sizeof(float));
        if (!kernel) {
            free(tmp);
            return;
        }
        float sum = 0.0f;
        for (int i = -radius; i <= radius; i++) {
            float v = expf(-0.5f * (float)(i * i) / (sigma_eff * sigma_eff));
            kernel[i + radius] = v;
            sum += v;
        }
        if (sum > 0.0f) {
            for (int i = 0; i < klen; i++) {
                kernel[i] /= sum;
            }
        }
        for (uint32_t y = 0; y < h; y++) {
            for (uint32_t x = 0; x < w; x++) {
                float acc[4] = {0, 0, 0, 0};
                for (int k = -radius; k <= radius; k++) {
                    int sy = (int)y + k;
                    if (sy < 0) sy = 0;
                    if (sy >= (int)h) sy = (int)h - 1;
                    size_t idx = ((size_t)sy * w + (size_t)x) * 4u;
                    float wgt = kernel[k + radius];
                    acc[0] += buf[idx + 0] * wgt;
                    acc[1] += buf[idx + 1] * wgt;
                    acc[2] += buf[idx + 2] * wgt;
                    acc[3] += buf[idx + 3] * wgt;
                }
                size_t out = ((size_t)y * w + (size_t)x) * 4u;
                tmp[out + 0] = (uint8_t)lroundf(fminf(fmaxf(acc[0], 0.0f), 255.0f));
                tmp[out + 1] = (uint8_t)lroundf(fminf(fmaxf(acc[1], 0.0f), 255.0f));
                tmp[out + 2] = (uint8_t)lroundf(fminf(fmaxf(acc[2], 0.0f), 255.0f));
                tmp[out + 3] = (uint8_t)lroundf(fminf(fmaxf(acc[3], 0.0f), 255.0f));
            }
        }
        memcpy(buf, tmp, (size_t)w * (size_t)h * 4u);
        free(kernel);
    }
    free(tmp);
}

static int svg_find_filter_result(const char *name, char **names, int name_count) {
    if (!name || !*name || !names) {
        return -1;
    }
    for (int i = 0; i < name_count; i++) {
        if (names[i] && strcmp(names[i], name) == 0) {
            return i;
        }
    }
    return -1;
}

static svg_filter_ref svg_make_filter_ref(int type, int index) {
    svg_filter_ref ref;
    ref.type = type;
    ref.index = index;
    return ref;
}

static svg_filter_ref svg_parse_filter_ref(const char *s, char **names, int name_count) {
    if (!s || !*s) {
        return svg_make_filter_ref(SVG_FILTER_IN_CURRENT, -1);
    }
    if (svg_strcasecmp(s, "sourcegraphic") == 0) {
        return svg_make_filter_ref(SVG_FILTER_IN_SOURCE_GRAPHIC, -1);
    }
    if (svg_strcasecmp(s, "sourcealpha") == 0) {
        return svg_make_filter_ref(SVG_FILTER_IN_SOURCE_ALPHA, -1);
    }
    if (svg_strcasecmp(s, "backgroundimage") == 0) {
        return svg_make_filter_ref(SVG_FILTER_IN_BACKGROUND_IMAGE, -1);
    }
    if (svg_strcasecmp(s, "backgroundalpha") == 0) {
        return svg_make_filter_ref(SVG_FILTER_IN_BACKGROUND_ALPHA, -1);
    }
    if (svg_strcasecmp(s, "fillpaint") == 0) {
        return svg_make_filter_ref(SVG_FILTER_IN_FILL_PAINT, -1);
    }
    if (svg_strcasecmp(s, "strokepaint") == 0) {
        return svg_make_filter_ref(SVG_FILTER_IN_STROKE_PAINT, -1);
    }
    int idx = svg_find_filter_result(s, names, name_count);
    if (idx >= 0) {
        return svg_make_filter_ref(SVG_FILTER_IN_RESULT, idx);
    }
    return svg_make_filter_ref(SVG_FILTER_IN_CURRENT, -1);
}

static void svg_filter_op_init(svg_filter_op *op) {
    if (!op) {
        return;
    }
    memset(op, 0, sizeof(*op));
    op->result_id = -1;
    op->kernel = NULL;
    op->kernel_count = 0;
    op->has_region = 0;
    op->rx.value = 0.0f;
    op->ry.value = 0.0f;
    op->rwidth.value = 0.0f;
    op->rheight.value = 0.0f;
    op->rx.is_percent = 0;
    op->ry.is_percent = 0;
    op->rwidth.is_percent = 0;
    op->rheight.is_percent = 0;
    op->order_x = 0;
    op->order_y = 0;
    op->target_x = 0;
    op->target_y = 0;
    op->divisor = 0.0f;
    op->bias = 0.0f;
    op->preserve_alpha = 0;
    op->turb_freq_x = 0.0f;
    op->turb_freq_y = 0.0f;
    op->turb_octaves = 1;
    op->turb_seed = 0;
    op->turb_fractal = 0;
    op->displace_x_channel = SVG_DISPLACE_R;
    op->displace_y_channel = SVG_DISPLACE_G;
    op->surface_scale = 1.0f;
    op->diffuse_constant = 1.0f;
    op->specular_constant = 1.0f;
    op->specular_exponent = 1.0f;
    op->light_x = 0.0f;
    op->light_y = 0.0f;
    op->light_z = 1.0f;
    op->image_href = NULL;
    op->img_x = 0.0f;
    op->img_y = 0.0f;
    op->img_w = 0.0f;
    op->img_h = 0.0f;
    op->has_image_geom = 0;
}

static void svg_filter_ops_cleanup(svg_filter_op *ops, int count) {
    if (!ops || count <= 0) {
        return;
    }
    for (int i = 0; i < count; i++) {
        free(ops[i].kernel);
        ops[i].kernel = NULL;
        ops[i].kernel_count = 0;
        free(ops[i].image_href);
        ops[i].image_href = NULL;
    }
}

static void svg_color_matrix_identity(float *m) {
    for (int i = 0; i < 20; i++) {
        m[i] = 0.0f;
    }
    m[0] = 1.0f;
    m[6] = 1.0f;
    m[12] = 1.0f;
    m[18] = 1.0f;
}

static void svg_color_matrix_saturate(float s, float *m) {
    float ir = 0.213f;
    float ig = 0.715f;
    float ib = 0.072f;
    svg_color_matrix_identity(m);
    m[0] = ir + s * (1.0f - ir);
    m[1] = ig - s * ig;
    m[2] = ib - s * ib;
    m[5] = ir - s * ir;
    m[6] = ig + s * (1.0f - ig);
    m[7] = ib - s * ib;
    m[10] = ir - s * ir;
    m[11] = ig - s * ig;
    m[12] = ib + s * (1.0f - ib);
}

static void svg_color_matrix_huerotate(float angle_deg, float *m) {
    float ir = 0.213f;
    float ig = 0.715f;
    float ib = 0.072f;
    float a = angle_deg * (float)M_PI / 180.0f;
    float c = cosf(a);
    float s = sinf(a);
    svg_color_matrix_identity(m);
    m[0] = ir + c * (1.0f - ir) + s * (-ir);
    m[1] = ig + c * (-ig) + s * (-ig);
    m[2] = ib + c * (-ib) + s * (1.0f - ib);
    m[5] = ir + c * (-ir) + s * 0.143f;
    m[6] = ig + c * (1.0f - ig) + s * 0.140f;
    m[7] = ib + c * (-ib) + s * (-0.283f);
    m[10] = ir + c * (-ir) + s * (-(1.0f - ir));
    m[11] = ig + c * (-ig) + s * ig;
    m[12] = ib + c * (1.0f - ib) + s * ib;
}

static void svg_color_matrix_luminance_to_alpha(float *m) {
    for (int i = 0; i < 20; i++) {
        m[i] = 0.0f;
    }
    m[15] = 0.2126f;
    m[16] = 0.7152f;
    m[17] = 0.0722f;
}

static void svg_filter_assign_result(const svg_tag *tag, svg_filter_op *op,
                                     char ***names, int *name_count, int *name_cap) {
    if (!op) {
        return;
    }
    op->result_id = -1;
    if (!tag || !names || !name_count || !name_cap) {
        return;
    }
    const char *res = svg_get_attr(tag, "result");
    if (!res || !*res) {
        return;
    }
    int idx = svg_find_filter_result(res, *names, *name_count);
    if (idx < 0) {
        if (*name_count >= *name_cap) {
            int new_cap = *name_cap ? *name_cap * 2 : 8;
            char **n = (char **)realloc(*names, (size_t)new_cap * sizeof(char *));
            if (!n) {
                return;
            }
            *names = n;
            *name_cap = new_cap;
        }
        (*names)[*name_count] = svg_strdup(res);
        if (!(*names)[*name_count]) {
            return;
        }
        idx = *name_count;
        (*name_count)++;
    }
    op->result_id = idx;
}

static void svg_filter_parse_region(const svg_tag *tag, svg_filter_op *op, svg_gradient_units units) {
    if (!tag || !op) {
        return;
    }
    int has = 0;
    const char *x = svg_get_attr(tag, "x");
    const char *y = svg_get_attr(tag, "y");
    const char *w = svg_get_attr(tag, "width");
    const char *h = svg_get_attr(tag, "height");
    if (x && svg_parse_grad_coord(x, &op->rx, units)) has = 1;
    if (y && svg_parse_grad_coord(y, &op->ry, units)) has = 1;
    if (w && svg_parse_grad_coord(w, &op->rwidth, units)) has = 1;
    if (h && svg_parse_grad_coord(h, &op->rheight, units)) has = 1;
    if (has) {
        op->has_region = 1;
    }
}

static void svg_transfer_func_init(svg_transfer_func *func) {
    if (!func) {
        return;
    }
    func->type = SVG_TRANSFER_IDENTITY;
    func->table_count = 0;
    func->slope = 1.0f;
    func->intercept = 0.0f;
    func->amplitude = 1.0f;
    func->exponent = 1.0f;
    func->offset = 0.0f;
    for (int i = 0; i < SVG_MAX_TRANSFER_TABLE; i++) {
        func->table[i] = 0.0f;
    }
}

static float svg_transfer_apply(const svg_transfer_func *func, float v) {
    if (!func) {
        return v;
    }
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    float out = v;
    switch (func->type) {
    case SVG_TRANSFER_TABLE:
        if (func->table_count > 0) {
            float t = v * (float)(func->table_count - 1);
            int i0 = (int)floorf(t);
            int i1 = i0 + 1;
            if (i0 < 0) i0 = 0;
            if (i1 >= func->table_count) i1 = func->table_count - 1;
            float frac = t - (float)i0;
            float v0 = func->table[i0];
            float v1 = func->table[i1];
            out = v0 + (v1 - v0) * frac;
        }
        break;
    case SVG_TRANSFER_DISCRETE:
        if (func->table_count > 0) {
            int idx = (int)floorf(v * (float)func->table_count);
            if (idx < 0) idx = 0;
            if (idx >= func->table_count) idx = func->table_count - 1;
            out = func->table[idx];
        }
        break;
    case SVG_TRANSFER_LINEAR:
        out = func->slope * v + func->intercept;
        break;
    case SVG_TRANSFER_GAMMA:
        out = func->amplitude * powf(v, func->exponent) + func->offset;
        break;
    case SVG_TRANSFER_IDENTITY:
    default:
        out = v;
        break;
    }
    if (out < 0.0f) out = 0.0f;
    if (out > 1.0f) out = 1.0f;
    return out;
}

static void svg_parse_transfer_func(const svg_tag *tag, svg_transfer_func *func) {
    if (!tag || !func) {
        return;
    }
    svg_transfer_func_init(func);
    const char *type = svg_get_attr(tag, "type");
    if (!type || svg_strcasecmp(type, "identity") == 0) {
        func->type = SVG_TRANSFER_IDENTITY;
        return;
    }
    if (svg_strcasecmp(type, "table") == 0) {
        func->type = SVG_TRANSFER_TABLE;
        const char *vals = svg_get_attr(tag, "tableValues");
        if (vals) {
            const char *p = vals;
            while (*p && func->table_count < SVG_MAX_TRANSFER_TABLE) {
                float v = 0.0f;
                if (!svg_parse_number(&p, &v)) {
                    break;
                }
                if (v < 0.0f) v = 0.0f;
                if (v > 1.0f) v = 1.0f;
                func->table[func->table_count++] = v;
            }
        }
        return;
    }
    if (svg_strcasecmp(type, "discrete") == 0) {
        func->type = SVG_TRANSFER_DISCRETE;
        const char *vals = svg_get_attr(tag, "tableValues");
        if (vals) {
            const char *p = vals;
            while (*p && func->table_count < SVG_MAX_TRANSFER_TABLE) {
                float v = 0.0f;
                if (!svg_parse_number(&p, &v)) {
                    break;
                }
                if (v < 0.0f) v = 0.0f;
                if (v > 1.0f) v = 1.0f;
                func->table[func->table_count++] = v;
            }
        }
        return;
    }
    if (svg_strcasecmp(type, "linear") == 0) {
        func->type = SVG_TRANSFER_LINEAR;
        const char *slope = svg_get_attr(tag, "slope");
        const char *intercept = svg_get_attr(tag, "intercept");
        if (slope) func->slope = (float)strtod(slope, NULL);
        if (intercept) func->intercept = (float)strtod(intercept, NULL);
        return;
    }
    if (svg_strcasecmp(type, "gamma") == 0) {
        func->type = SVG_TRANSFER_GAMMA;
        const char *amplitude = svg_get_attr(tag, "amplitude");
        const char *exponent = svg_get_attr(tag, "exponent");
        const char *offset = svg_get_attr(tag, "offset");
        if (amplitude) func->amplitude = (float)strtod(amplitude, NULL);
        if (exponent) func->exponent = (float)strtod(exponent, NULL);
        if (offset) func->offset = (float)strtod(offset, NULL);
        return;
    }
    func->type = SVG_TRANSFER_IDENTITY;
}

static int svg_parse_filter_ops(const char *content, svg_filter_op **out_ops, int *out_count,
                                int *out_result_count, float base_len, float dpi,
                                svg_gradient_units units) {
    if (!content || !out_ops || !out_count) {
        return 0;
    }
    *out_ops = NULL;
    *out_count = 0;
    if (out_result_count) {
        *out_result_count = 0;
    }
    char *buf = svg_strdup(content);
    if (!buf) {
        return 0;
    }
    char *p = buf;
    svg_tag tag;
    int cap = 0;
    int merge_index = -1;
    int transfer_index = -1;
    int lighting_index = -1;
    char **names = NULL;
    int name_count = 0;
    int name_cap = 0;
    while (svg_next_tag(&p, &tag)) {
        if (tag.is_end) {
            const char *local_end = svg_local_name(tag.name);
            if (merge_index >= 0 && strcmp(local_end, "femerge") == 0) {
                merge_index = -1;
            }
            if (transfer_index >= 0 && strcmp(local_end, "fecomponenttransfer") == 0) {
                transfer_index = -1;
            }
            if (lighting_index >= 0 &&
                (strcmp(local_end, "fediffuselighting") == 0 ||
                 strcmp(local_end, "fespecularlighting") == 0)) {
                lighting_index = -1;
            }
            continue;
        }
        const char *local = svg_local_name(tag.name);
        if (strcmp(local, "femergenode") == 0 && merge_index >= 0) {
            svg_filter_op *op = &(*out_ops)[merge_index];
            if (op->merge_count < (int)(sizeof(op->merge_inputs) / sizeof(op->merge_inputs[0]))) {
                op->merge_inputs[op->merge_count++] = svg_parse_filter_ref(svg_get_attr(&tag, "in"),
                                                                          names, name_count);
            }
            continue;
        }
        if (strcmp(local, "fefuncr") == 0 && transfer_index >= 0) {
            svg_filter_op *op = &(*out_ops)[transfer_index];
            svg_parse_transfer_func(&tag, &op->tr);
            continue;
        }
        if (strcmp(local, "fefuncg") == 0 && transfer_index >= 0) {
            svg_filter_op *op = &(*out_ops)[transfer_index];
            svg_parse_transfer_func(&tag, &op->tg);
            continue;
        }
        if (strcmp(local, "fefuncb") == 0 && transfer_index >= 0) {
            svg_filter_op *op = &(*out_ops)[transfer_index];
            svg_parse_transfer_func(&tag, &op->tb);
            continue;
        }
        if (strcmp(local, "fefunca") == 0 && transfer_index >= 0) {
            svg_filter_op *op = &(*out_ops)[transfer_index];
            svg_parse_transfer_func(&tag, &op->ta);
            continue;
        }
        if (strcmp(local, "fedistantlight") == 0 && lighting_index >= 0) {
            svg_filter_op *op = &(*out_ops)[lighting_index];
            float az = 0.0f;
            float el = 0.0f;
            const char *azs = svg_get_attr(&tag, "azimuth");
            const char *els = svg_get_attr(&tag, "elevation");
            if (azs) az = (float)strtod(azs, NULL);
            if (els) el = (float)strtod(els, NULL);
            float azr = az * (float)M_PI / 180.0f;
            float elr = el * (float)M_PI / 180.0f;
            op->light_x = cosf(elr) * cosf(azr);
            op->light_y = cosf(elr) * sinf(azr);
            op->light_z = sinf(elr);
            continue;
        }
        if (strcmp(local, "femerge") == 0) {
            if (*out_count >= cap) {
                int new_cap = cap ? cap * 2 : 8;
                svg_filter_op *n = (svg_filter_op *)realloc(*out_ops, (size_t)new_cap * sizeof(svg_filter_op));
                if (!n) {
                    break;
                }
                *out_ops = n;
                cap = new_cap;
            }
            svg_filter_op *op = &(*out_ops)[*out_count];
            svg_filter_op_init(op);
            op->type = SVG_FILTER_OP_MERGE;
            op->merge_count = 0;
            op->result_id = -1;
            svg_filter_assign_result(&tag, op, &names, &name_count, &name_cap);
            svg_filter_parse_region(&tag, op, units);
            (*out_count)++;
            merge_index = *out_count - 1;
            continue;
        }
        if (strcmp(local, "fetile") == 0) {
            if (*out_count >= cap) {
                int new_cap = cap ? cap * 2 : 8;
                svg_filter_op *n = (svg_filter_op *)realloc(*out_ops, (size_t)new_cap * sizeof(svg_filter_op));
                if (!n) {
                    break;
                }
                *out_ops = n;
                cap = new_cap;
            }
            svg_filter_op *op = &(*out_ops)[*out_count];
            svg_filter_op_init(op);
            op->type = SVG_FILTER_OP_TILE;
            op->in1 = svg_parse_filter_ref(svg_get_attr(&tag, "in"), names, name_count);
            op->result_id = -1;
            svg_filter_assign_result(&tag, op, &names, &name_count, &name_cap);
            svg_filter_parse_region(&tag, op, units);
            (*out_count)++;
            continue;
        }
        if (strcmp(local, "feimage") == 0) {
            if (*out_count >= cap) {
                int new_cap = cap ? cap * 2 : 8;
                svg_filter_op *n = (svg_filter_op *)realloc(*out_ops, (size_t)new_cap * sizeof(svg_filter_op));
                if (!n) {
                    break;
                }
                *out_ops = n;
                cap = new_cap;
            }
            svg_filter_op *op = &(*out_ops)[*out_count];
            svg_filter_op_init(op);
            op->type = SVG_FILTER_OP_IMAGE;
            const char *href = svg_get_attr(&tag, "href");
            if (!href) {
                href = svg_get_attr(&tag, "xlink:href");
            }
            if (href && *href) {
                op->image_href = svg_strdup(href);
            }
            int ok = 0;
            const char *xs = svg_get_attr(&tag, "x");
            if (xs) {
                float v = svg_parse_length(xs, base_len, dpi, &ok);
                if (ok) {
                    op->img_x = v;
                    op->has_image_geom = 1;
                }
            }
            const char *ys = svg_get_attr(&tag, "y");
            if (ys) {
                float v = svg_parse_length(ys, base_len, dpi, &ok);
                if (ok) {
                    op->img_y = v;
                    op->has_image_geom = 1;
                }
            }
            const char *ws = svg_get_attr(&tag, "width");
            if (ws) {
                float v = svg_parse_length(ws, base_len, dpi, &ok);
                if (ok) {
                    op->img_w = v;
                    op->has_image_geom = 1;
                }
            }
            const char *hs = svg_get_attr(&tag, "height");
            if (hs) {
                float v = svg_parse_length(hs, base_len, dpi, &ok);
                if (ok) {
                    op->img_h = v;
                    op->has_image_geom = 1;
                }
            }
            op->result_id = -1;
            svg_filter_assign_result(&tag, op, &names, &name_count, &name_cap);
            svg_filter_parse_region(&tag, op, units);
            (*out_count)++;
            continue;
        }
        if (strcmp(local, "fecomponenttransfer") == 0) {
            if (*out_count >= cap) {
                int new_cap = cap ? cap * 2 : 8;
                svg_filter_op *n = (svg_filter_op *)realloc(*out_ops, (size_t)new_cap * sizeof(svg_filter_op));
                if (!n) {
                    break;
                }
                *out_ops = n;
                cap = new_cap;
            }
            svg_filter_op *op = &(*out_ops)[*out_count];
            svg_filter_op_init(op);
            op->type = SVG_FILTER_OP_COMPONENT_TRANSFER;
            op->in1 = svg_parse_filter_ref(svg_get_attr(&tag, "in"), names, name_count);
            svg_transfer_func_init(&op->tr);
            svg_transfer_func_init(&op->tg);
            svg_transfer_func_init(&op->tb);
            svg_transfer_func_init(&op->ta);
            op->result_id = -1;
            svg_filter_assign_result(&tag, op, &names, &name_count, &name_cap);
            svg_filter_parse_region(&tag, op, units);
            (*out_count)++;
            if (!tag.is_self_closing) {
                transfer_index = *out_count - 1;
            }
            continue;
        }
        if (strcmp(local, "feconvolvematrix") == 0) {
            const char *order = svg_get_attr(&tag, "order");
            const char *order_x = svg_get_attr(&tag, "orderX");
            const char *order_y = svg_get_attr(&tag, "orderY");
            int ox = 0, oy = 0;
            if (order) {
                const char *op = order;
                float v = 0.0f;
                if (svg_parse_number(&op, &v)) {
                    ox = (int)lroundf(v);
                    if (!svg_parse_number(&op, &v)) {
                        oy = ox;
                    } else {
                        oy = (int)lroundf(v);
                    }
                }
            }
            if (order_x) {
                ox = (int)lroundf(strtod(order_x, NULL));
            }
            if (order_y) {
                oy = (int)lroundf(strtod(order_y, NULL));
            }
            if (ox <= 0) ox = 3;
            if (oy <= 0) oy = 3;
            int kcount = ox * oy;
            const char *km = svg_get_attr(&tag, "kernelMatrix");
            if (!km) {
                continue;
            }
            float *kernel = (float *)malloc((size_t)kcount * sizeof(float));
            if (!kernel) {
                continue;
            }
            const char *kp = km;
            int filled = 0;
            for (int i = 0; i < kcount; i++) {
                float v = 0.0f;
                if (!svg_parse_number(&kp, &v)) {
                    break;
                }
                kernel[i] = v;
                filled++;
            }
            if (filled < kcount) {
                free(kernel);
                continue;
            }
            if (*out_count >= cap) {
                int new_cap = cap ? cap * 2 : 8;
                svg_filter_op *n = (svg_filter_op *)realloc(*out_ops, (size_t)new_cap * sizeof(svg_filter_op));
                if (!n) {
                    free(kernel);
                    break;
                }
                *out_ops = n;
                cap = new_cap;
            }
            svg_filter_op *op = &(*out_ops)[*out_count];
            svg_filter_op_init(op);
            op->type = SVG_FILTER_OP_CONVOLVE;
            op->kernel = kernel;
            op->kernel_count = kcount;
            op->order_x = ox;
            op->order_y = oy;
            const char *txs = svg_get_attr(&tag, "targetX");
            const char *tys = svg_get_attr(&tag, "targetY");
            op->target_x = txs ? (int)lroundf(strtod(txs, NULL)) : ox / 2;
            op->target_y = tys ? (int)lroundf(strtod(tys, NULL)) : oy / 2;
            const char *divs = svg_get_attr(&tag, "divisor");
            if (divs) {
                op->divisor = (float)strtod(divs, NULL);
            } else {
                float sum = 0.0f;
                for (int i = 0; i < kcount; i++) {
                    sum += kernel[i];
                }
                op->divisor = sum;
            }
            if (fabsf(op->divisor) < 1e-6f) {
                op->divisor = 1.0f;
            }
            const char *bias = svg_get_attr(&tag, "bias");
            if (bias) {
                op->bias = (float)strtod(bias, NULL);
            }
            const char *edge = svg_get_attr(&tag, "edgeMode");
            if (edge) {
                if (svg_strcasecmp(edge, "duplicate") == 0) op->mode = SVG_EDGE_DUPLICATE;
                else if (svg_strcasecmp(edge, "wrap") == 0) op->mode = SVG_EDGE_WRAP;
                else op->mode = SVG_EDGE_NONE;
            } else {
                op->mode = SVG_EDGE_NONE;
            }
            const char *pres = svg_get_attr(&tag, "preserveAlpha");
            if (pres && (svg_strcasecmp(pres, "true") == 0 || strcmp(pres, "1") == 0)) {
                op->preserve_alpha = 1;
            }
            op->in1 = svg_parse_filter_ref(svg_get_attr(&tag, "in"), names, name_count);
            op->result_id = -1;
            svg_filter_assign_result(&tag, op, &names, &name_count, &name_cap);
            svg_filter_parse_region(&tag, op, units);
            (*out_count)++;
            continue;
        }
        if (strcmp(local, "feturbulence") == 0) {
            if (*out_count >= cap) {
                int new_cap = cap ? cap * 2 : 8;
                svg_filter_op *n = (svg_filter_op *)realloc(*out_ops, (size_t)new_cap * sizeof(svg_filter_op));
                if (!n) {
                    break;
                }
                *out_ops = n;
                cap = new_cap;
            }
            svg_filter_op *op = &(*out_ops)[*out_count];
            svg_filter_op_init(op);
            op->type = SVG_FILTER_OP_TURBULENCE;
            const char *basef = svg_get_attr(&tag, "baseFrequency");
            if (basef) {
                const char *bp = basef;
                float fx = 0.0f, fy = 0.0f;
                if (svg_parse_number(&bp, &fx)) {
                    if (!svg_parse_number(&bp, &fy)) {
                        fy = fx;
                    }
                }
                op->turb_freq_x = fx;
                op->turb_freq_y = fy;
            }
            const char *oct = svg_get_attr(&tag, "numOctaves");
            if (oct) {
                int v = (int)lroundf(strtod(oct, NULL));
                if (v < 1) v = 1;
                if (v > 8) v = 8;
                op->turb_octaves = v;
            }
            const char *seed = svg_get_attr(&tag, "seed");
            if (seed) {
                op->turb_seed = (int)lroundf(strtod(seed, NULL));
            }
            const char *type = svg_get_attr(&tag, "type");
            if (type && svg_strcasecmp(type, "fractalnoise") == 0) {
                op->turb_fractal = 1;
            }
            op->result_id = -1;
            svg_filter_assign_result(&tag, op, &names, &name_count, &name_cap);
            svg_filter_parse_region(&tag, op, units);
            (*out_count)++;
            continue;
        }
        if (strcmp(local, "fedisplacementmap") == 0) {
            if (*out_count >= cap) {
                int new_cap = cap ? cap * 2 : 8;
                svg_filter_op *n = (svg_filter_op *)realloc(*out_ops, (size_t)new_cap * sizeof(svg_filter_op));
                if (!n) {
                    break;
                }
                *out_ops = n;
                cap = new_cap;
            }
            svg_filter_op *op = &(*out_ops)[*out_count];
            svg_filter_op_init(op);
            op->type = SVG_FILTER_OP_DISPLACEMENT;
            const char *scale = svg_get_attr(&tag, "scale");
            if (scale) {
                op->a = (float)strtod(scale, NULL);
            } else {
                op->a = 0.0f;
            }
            const char *xch = svg_get_attr(&tag, "xChannelSelector");
            const char *ych = svg_get_attr(&tag, "yChannelSelector");
            if (xch) {
                if (svg_strcasecmp(xch, "g") == 0) op->displace_x_channel = SVG_DISPLACE_G;
                else if (svg_strcasecmp(xch, "b") == 0) op->displace_x_channel = SVG_DISPLACE_B;
                else if (svg_strcasecmp(xch, "a") == 0) op->displace_x_channel = SVG_DISPLACE_A;
                else op->displace_x_channel = SVG_DISPLACE_R;
            }
            if (ych) {
                if (svg_strcasecmp(ych, "g") == 0) op->displace_y_channel = SVG_DISPLACE_G;
                else if (svg_strcasecmp(ych, "b") == 0) op->displace_y_channel = SVG_DISPLACE_B;
                else if (svg_strcasecmp(ych, "a") == 0) op->displace_y_channel = SVG_DISPLACE_A;
                else op->displace_y_channel = SVG_DISPLACE_R;
            }
            op->in1 = svg_parse_filter_ref(svg_get_attr(&tag, "in"), names, name_count);
            op->in2 = svg_parse_filter_ref(svg_get_attr(&tag, "in2"), names, name_count);
            op->result_id = -1;
            svg_filter_assign_result(&tag, op, &names, &name_count, &name_cap);
            svg_filter_parse_region(&tag, op, units);
            (*out_count)++;
            continue;
        }
        if (strcmp(local, "fediffuselighting") == 0 || strcmp(local, "fespecularlighting") == 0) {
            if (*out_count >= cap) {
                int new_cap = cap ? cap * 2 : 8;
                svg_filter_op *n = (svg_filter_op *)realloc(*out_ops, (size_t)new_cap * sizeof(svg_filter_op));
                if (!n) {
                    break;
                }
                *out_ops = n;
                cap = new_cap;
            }
            svg_filter_op *op = &(*out_ops)[*out_count];
            svg_filter_op_init(op);
            if (strcmp(local, "fediffuselighting") == 0) {
                op->type = SVG_FILTER_OP_DIFFUSE_LIGHTING;
            } else {
                op->type = SVG_FILTER_OP_SPECULAR_LIGHTING;
            }
            op->in1 = svg_parse_filter_ref(svg_get_attr(&tag, "in"), names, name_count);
            const char *surface = svg_get_attr(&tag, "surfaceScale");
            if (surface) op->surface_scale = (float)strtod(surface, NULL);
            const char *diff = svg_get_attr(&tag, "diffuseConstant");
            if (diff) op->diffuse_constant = (float)strtod(diff, NULL);
            const char *spec = svg_get_attr(&tag, "specularConstant");
            if (spec) op->specular_constant = (float)strtod(spec, NULL);
            const char *exp = svg_get_attr(&tag, "specularExponent");
            if (exp) op->specular_exponent = (float)strtod(exp, NULL);
            uint32_t color = 0xFFFFFFFFu;
            int is_none = 0;
            const char *lc = svg_get_attr(&tag, "lighting-color");
            if (lc && svg_parse_color(lc, &color, &is_none) && !is_none) {
                op->color = color;
            } else {
                op->color = 0xFFFFFFFFu;
            }
            op->result_id = -1;
            svg_filter_assign_result(&tag, op, &names, &name_count, &name_cap);
            svg_filter_parse_region(&tag, op, units);
            (*out_count)++;
            if (!tag.is_self_closing) {
                lighting_index = *out_count - 1;
            }
            continue;
        }
        if (strcmp(local, "fegaussianblur") == 0) {
            const char *sd = svg_get_attr(&tag, "stdDeviation");
            if (!sd) {
                continue;
            }
            float sx = 0.0f;
            float sy = 0.0f;
            const char *sp = sd;
            if (!svg_parse_number(&sp, &sx)) {
                continue;
            }
            if (!svg_parse_number(&sp, &sy)) {
                sy = sx;
            }
            if (*out_count >= cap) {
                int new_cap = cap ? cap * 2 : 8;
                svg_filter_op *n = (svg_filter_op *)realloc(*out_ops, (size_t)new_cap * sizeof(svg_filter_op));
                if (!n) {
                    break;
                }
                *out_ops = n;
                cap = new_cap;
            }
            svg_filter_op *op = &(*out_ops)[*out_count];
            svg_filter_op_init(op);
            op->type = SVG_FILTER_OP_BLUR;
            op->a = sx;
            op->b = sy;
            op->in1 = svg_parse_filter_ref(svg_get_attr(&tag, "in"), names, name_count);
            op->result_id = -1;
            svg_filter_assign_result(&tag, op, &names, &name_count, &name_cap);
            svg_filter_parse_region(&tag, op, units);
            (*out_count)++;
        } else if (strcmp(local, "feoffset") == 0) {
            const char *dxs = svg_get_attr(&tag, "dx");
            const char *dys = svg_get_attr(&tag, "dy");
            int okx = 0, oky = 0;
            float dx = svg_parse_length(dxs, base_len, dpi, &okx);
            float dy = svg_parse_length(dys, base_len, dpi, &oky);
            if (!okx) dx = 0.0f;
            if (!oky) dy = 0.0f;
            if (*out_count >= cap) {
                int new_cap = cap ? cap * 2 : 8;
                svg_filter_op *n = (svg_filter_op *)realloc(*out_ops, (size_t)new_cap * sizeof(svg_filter_op));
                if (!n) {
                    break;
                }
                *out_ops = n;
                cap = new_cap;
            }
            svg_filter_op *op = &(*out_ops)[*out_count];
            svg_filter_op_init(op);
            op->type = SVG_FILTER_OP_OFFSET;
            op->a = dx;
            op->b = dy;
            op->in1 = svg_parse_filter_ref(svg_get_attr(&tag, "in"), names, name_count);
            op->result_id = -1;
            svg_filter_assign_result(&tag, op, &names, &name_count, &name_cap);
            svg_filter_parse_region(&tag, op, units);
            (*out_count)++;
        } else if (strcmp(local, "fecolormatrix") == 0) {
            const char *type = svg_get_attr(&tag, "type");
            const char *vals = svg_get_attr(&tag, "values");
            float mat[20];
            int has_matrix = 0;
            if (!type || svg_strcasecmp(type, "matrix") == 0) {
                if (vals) {
                    const char *vp = vals;
                    for (int i = 0; i < 20; i++) {
                        if (!svg_parse_number(&vp, &mat[i])) {
                            break;
                        }
                        if (i == 19) {
                            has_matrix = 1;
                        }
                    }
                }
            } else if (svg_strcasecmp(type, "saturate") == 0) {
                float s = 1.0f;
                if (vals) {
                    const char *vp = vals;
                    if (!svg_parse_number(&vp, &s)) {
                        s = 1.0f;
                    }
                }
                svg_color_matrix_saturate(s, mat);
                has_matrix = 1;
            } else if (svg_strcasecmp(type, "huerotate") == 0) {
                float a = 0.0f;
                if (vals) {
                    const char *vp = vals;
                    if (!svg_parse_number(&vp, &a)) {
                        a = 0.0f;
                    }
                }
                svg_color_matrix_huerotate(a, mat);
                has_matrix = 1;
            } else if (svg_strcasecmp(type, "luminancetoalpha") == 0) {
                svg_color_matrix_luminance_to_alpha(mat);
                has_matrix = 1;
            }
            if (!has_matrix) {
                continue;
            }
            if (*out_count >= cap) {
                int new_cap = cap ? cap * 2 : 8;
                svg_filter_op *n = (svg_filter_op *)realloc(*out_ops, (size_t)new_cap * sizeof(svg_filter_op));
                if (!n) {
                    break;
                }
                *out_ops = n;
                cap = new_cap;
            }
            svg_filter_op *op = &(*out_ops)[*out_count];
            svg_filter_op_init(op);
            op->type = SVG_FILTER_OP_COLOR_MATRIX;
            op->has_matrix = 1;
            memcpy(op->matrix, mat, sizeof(mat));
            op->in1 = svg_parse_filter_ref(svg_get_attr(&tag, "in"), names, name_count);
            op->result_id = -1;
            svg_filter_assign_result(&tag, op, &names, &name_count, &name_cap);
            svg_filter_parse_region(&tag, op, units);
            (*out_count)++;
        } else if (strcmp(local, "feflood") == 0) {
            uint32_t color = 0x000000FFu;
            int is_none = 0;
            const char *col = svg_get_attr(&tag, "flood-color");
            if (col) {
                if (!svg_parse_color(col, &color, &is_none) || is_none) {
                    color = 0x00000000u;
                }
            }
            float opacity = 1.0f;
            const char *fo = svg_get_attr(&tag, "flood-opacity");
            if (fo) {
                svg_parse_opacity_value(fo, &opacity);
            }
            uint8_t a = (uint8_t)(color & 0xFFu);
            float alpha = (a / 255.0f) * opacity;
            if (alpha < 0.0f) alpha = 0.0f;
            if (alpha > 1.0f) alpha = 1.0f;
            a = (uint8_t)lroundf(alpha * 255.0f);
            color = (color & 0xFFFFFF00u) | a;
            if (*out_count >= cap) {
                int new_cap = cap ? cap * 2 : 8;
                svg_filter_op *n = (svg_filter_op *)realloc(*out_ops, (size_t)new_cap * sizeof(svg_filter_op));
                if (!n) {
                    break;
                }
                *out_ops = n;
                cap = new_cap;
            }
            svg_filter_op *op = &(*out_ops)[*out_count];
            svg_filter_op_init(op);
            op->type = SVG_FILTER_OP_FLOOD;
            op->color = color;
            op->result_id = -1;
            svg_filter_assign_result(&tag, op, &names, &name_count, &name_cap);
            svg_filter_parse_region(&tag, op, units);
            (*out_count)++;
        } else if (strcmp(local, "feblend") == 0) {
            const char *mode = svg_get_attr(&tag, "mode");
            int blend_mode = SVG_BLEND_NORMAL;
            if (mode) {
                if (svg_strcasecmp(mode, "multiply") == 0) blend_mode = SVG_BLEND_MULTIPLY;
                else if (svg_strcasecmp(mode, "screen") == 0) blend_mode = SVG_BLEND_SCREEN;
                else if (svg_strcasecmp(mode, "darken") == 0) blend_mode = SVG_BLEND_DARKEN;
                else if (svg_strcasecmp(mode, "lighten") == 0) blend_mode = SVG_BLEND_LIGHTEN;
                else if (svg_strcasecmp(mode, "overlay") == 0) blend_mode = SVG_BLEND_OVERLAY;
                else if (svg_strcasecmp(mode, "hard-light") == 0) blend_mode = SVG_BLEND_HARDLIGHT;
                else if (svg_strcasecmp(mode, "soft-light") == 0) blend_mode = SVG_BLEND_SOFTLIGHT;
            }
            if (*out_count >= cap) {
                int new_cap = cap ? cap * 2 : 8;
                svg_filter_op *n = (svg_filter_op *)realloc(*out_ops, (size_t)new_cap * sizeof(svg_filter_op));
                if (!n) {
                    break;
                }
                *out_ops = n;
                cap = new_cap;
            }
            svg_filter_op *op = &(*out_ops)[*out_count];
            svg_filter_op_init(op);
            op->type = SVG_FILTER_OP_BLEND;
            op->mode = blend_mode;
            op->in1 = svg_parse_filter_ref(svg_get_attr(&tag, "in"), names, name_count);
            op->in2 = svg_parse_filter_ref(svg_get_attr(&tag, "in2"), names, name_count);
            op->result_id = -1;
            svg_filter_assign_result(&tag, op, &names, &name_count, &name_cap);
            svg_filter_parse_region(&tag, op, units);
            (*out_count)++;
        } else if (strcmp(local, "fecomposite") == 0) {
            const char *opname = svg_get_attr(&tag, "operator");
            int comp = SVG_COMPOSITE_OVER;
            if (opname) {
                if (svg_strcasecmp(opname, "in") == 0) comp = SVG_COMPOSITE_IN;
                else if (svg_strcasecmp(opname, "out") == 0) comp = SVG_COMPOSITE_OUT;
                else if (svg_strcasecmp(opname, "atop") == 0) comp = SVG_COMPOSITE_ATOP;
                else if (svg_strcasecmp(opname, "xor") == 0) comp = SVG_COMPOSITE_XOR;
                else if (svg_strcasecmp(opname, "arithmetic") == 0) comp = SVG_COMPOSITE_ARITHMETIC;
            }
            float k1 = 0.0f, k2 = 0.0f, k3 = 0.0f, k4 = 0.0f;
            const char *k1s = svg_get_attr(&tag, "k1");
            const char *k2s = svg_get_attr(&tag, "k2");
            const char *k3s = svg_get_attr(&tag, "k3");
            const char *k4s = svg_get_attr(&tag, "k4");
            if (k1s) k1 = (float)strtod(k1s, NULL);
            if (k2s) k2 = (float)strtod(k2s, NULL);
            if (k3s) k3 = (float)strtod(k3s, NULL);
            if (k4s) k4 = (float)strtod(k4s, NULL);
            if (*out_count >= cap) {
                int new_cap = cap ? cap * 2 : 8;
                svg_filter_op *n = (svg_filter_op *)realloc(*out_ops, (size_t)new_cap * sizeof(svg_filter_op));
                if (!n) {
                    break;
                }
                *out_ops = n;
                cap = new_cap;
            }
            svg_filter_op *op = &(*out_ops)[*out_count];
            svg_filter_op_init(op);
            op->type = SVG_FILTER_OP_COMPOSITE;
            op->mode = comp;
            op->a = k1;
            op->b = k2;
            op->c = k3;
            op->d = k4;
            op->in1 = svg_parse_filter_ref(svg_get_attr(&tag, "in"), names, name_count);
            op->in2 = svg_parse_filter_ref(svg_get_attr(&tag, "in2"), names, name_count);
            op->result_id = -1;
            svg_filter_assign_result(&tag, op, &names, &name_count, &name_cap);
            svg_filter_parse_region(&tag, op, units);
            (*out_count)++;
        } else if (strcmp(local, "femorphology") == 0) {
            const char *opname = svg_get_attr(&tag, "operator");
            int morph = SVG_MORPHOLOGY_ERODE;
            if (opname && svg_strcasecmp(opname, "dilate") == 0) {
                morph = SVG_MORPHOLOGY_DILATE;
            }
            float rx = 0.0f, ry = 0.0f;
            const char *rad = svg_get_attr(&tag, "radius");
            if (rad) {
                const char *rp = rad;
                if (svg_parse_length_value(&rp, base_len, dpi, &rx)) {
                    if (!svg_parse_length_value(&rp, base_len, dpi, &ry)) {
                        ry = rx;
                    }
                }
            }
            if (*out_count >= cap) {
                int new_cap = cap ? cap * 2 : 8;
                svg_filter_op *n = (svg_filter_op *)realloc(*out_ops, (size_t)new_cap * sizeof(svg_filter_op));
                if (!n) {
                    break;
                }
                *out_ops = n;
                cap = new_cap;
            }
            svg_filter_op *op = &(*out_ops)[*out_count];
            svg_filter_op_init(op);
            op->type = SVG_FILTER_OP_MORPHOLOGY;
            op->mode = morph;
            op->a = rx;
            op->b = ry;
            op->in1 = svg_parse_filter_ref(svg_get_attr(&tag, "in"), names, name_count);
            op->result_id = -1;
            svg_filter_assign_result(&tag, op, &names, &name_count, &name_cap);
            svg_filter_parse_region(&tag, op, units);
            (*out_count)++;
        }
    }
    for (int i = 0; i < name_count; i++) {
        free(names[i]);
    }
    free(names);
    free(buf);
    if (out_result_count) {
        *out_result_count = name_count;
    }
    return *out_count > 0;
}

static uint8_t *svg_filter_resolve_input(const svg_filter_ref *ref, uint8_t *current,
                                         uint8_t *source, uint8_t *source_alpha,
                                         uint8_t *background, uint8_t *background_alpha,
                                         uint8_t *fill_paint, uint8_t *stroke_paint,
                                         uint8_t **results, int result_count) {
    if (!ref) {
        return current;
    }
    if (ref->type == SVG_FILTER_IN_SOURCE_GRAPHIC) {
        return source;
    }
    if (ref->type == SVG_FILTER_IN_SOURCE_ALPHA) {
        return source_alpha ? source_alpha : current;
    }
    if (ref->type == SVG_FILTER_IN_BACKGROUND_IMAGE) {
        return background ? background : current;
    }
    if (ref->type == SVG_FILTER_IN_BACKGROUND_ALPHA) {
        return background_alpha ? background_alpha : current;
    }
    if (ref->type == SVG_FILTER_IN_FILL_PAINT) {
        return fill_paint ? fill_paint : current;
    }
    if (ref->type == SVG_FILTER_IN_STROKE_PAINT) {
        return stroke_paint ? stroke_paint : current;
    }
    if (ref->type == SVG_FILTER_IN_RESULT) {
        if (results && ref->index >= 0 && ref->index < result_count && results[ref->index]) {
            return results[ref->index];
        }
        return current;
    }
    return current;
}

static void svg_filter_clip_region(uint8_t *buf, uint32_t w, uint32_t h, const svg_filter_region *region) {
    if (!buf || !region || !region->valid) {
        return;
    }
    int x0 = region->x0;
    int y0 = region->y0;
    int x1 = region->x1;
    int y1 = region->y1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= (int)w) x1 = (int)w - 1;
    if (y1 >= (int)h) y1 = (int)h - 1;
    for (uint32_t y = 0; y < h; y++) {
        if ((int)y < y0 || (int)y > y1) {
            memset(buf + (size_t)y * w * 4u, 0, (size_t)w * 4u);
            continue;
        }
        if (x0 > 0) {
            memset(buf + ((size_t)y * w + (size_t)0) * 4u, 0, (size_t)x0 * 4u);
        }
        if (x1 + 1 < (int)w) {
            size_t off = ((size_t)y * w + (size_t)(x1 + 1)) * 4u;
            memset(buf + off, 0, (size_t)(w - (uint32_t)(x1 + 1)) * 4u);
        }
    }
}

static uint8_t svg_clamp_u8(float v) {
    if (v < 0.0f) return 0;
    if (v > 255.0f) return 255;
    return (uint8_t)lroundf(v);
}

static float svg_channel_value(const uint8_t *buf, size_t idx, int channel) {
    float a = buf[idx + 3] / 255.0f;
    if (channel == SVG_DISPLACE_A) {
        return a;
    }
    if (a <= 0.0f) {
        return 0.0f;
    }
    if (channel == SVG_DISPLACE_G) {
        return (buf[idx + 1] / 255.0f) / a;
    }
    if (channel == SVG_DISPLACE_B) {
        return (buf[idx + 2] / 255.0f) / a;
    }
    return (buf[idx + 0] / 255.0f) / a;
}

static float svg_alpha_sample_clamped(const uint8_t *buf, uint32_t w, uint32_t h, int x, int y) {
    if (!buf || w == 0 || h == 0) {
        return 0.0f;
    }
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= (int)w) x = (int)w - 1;
    if (y >= (int)h) y = (int)h - 1;
    size_t idx = ((size_t)y * w + (size_t)x) * 4u;
    return buf[idx + 3] / 255.0f;
}

static uint32_t svg_noise_hash(int x, int y, int seed) {
    uint32_t h = (uint32_t)(x * 374761393 + y * 668265263) ^ (uint32_t)seed;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static float svg_noise2(float x, float y, int seed) {
    int x0 = (int)floorf(x);
    int y0 = (int)floorf(y);
    int x1 = x0 + 1;
    int y1 = y0 + 1;
    float fx = x - (float)x0;
    float fy = y - (float)y0;
    float u = fx * fx * (3.0f - 2.0f * fx);
    float v = fy * fy * (3.0f - 2.0f * fy);
    float v00 = (float)(svg_noise_hash(x0, y0, seed) & 0xFFFF) / 65535.0f;
    float v10 = (float)(svg_noise_hash(x1, y0, seed) & 0xFFFF) / 65535.0f;
    float v01 = (float)(svg_noise_hash(x0, y1, seed) & 0xFFFF) / 65535.0f;
    float v11 = (float)(svg_noise_hash(x1, y1, seed) & 0xFFFF) / 65535.0f;
    float i1 = v00 + (v10 - v00) * u;
    float i2 = v01 + (v11 - v01) * u;
    float val = i1 + (i2 - i1) * v;
    return val * 2.0f - 1.0f;
}

static void svg_compute_filter_region(const svg_filter *filter, const svg_render_ctx *ctx,
                                      const svg_bbox *bbox, const svg_matrix *m,
                                      svg_filter_region *out) {
    if (!out) {
        return;
    }
    out->valid = 0;
    if (!filter || !ctx || !bbox || !m) {
        return;
    }
    svg_bbox use_bbox = *bbox;
    if (!svg_bbox_valid(&use_bbox)) {
        use_bbox.x = ctx->vb_x;
        use_bbox.y = ctx->vb_y;
        use_bbox.w = ctx->vb_w;
        use_bbox.h = ctx->vb_h;
    }
    float bbox_w = use_bbox.w > 0.0f ? use_bbox.w : 1.0f;
    float bbox_h = use_bbox.h > 0.0f ? use_bbox.h : 1.0f;
    float base_w = (filter->units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX) ? bbox_w : ctx->vb_w;
    float base_h = (filter->units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX) ? bbox_h : ctx->vb_h;
    float origin_x = (filter->units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX) ? use_bbox.x : 0.0f;
    float origin_y = (filter->units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX) ? use_bbox.y : 0.0f;
    float fx = svg_resolve_grad_coord(&filter->x, base_w, origin_x, filter->units);
    float fy = svg_resolve_grad_coord(&filter->y, base_h, origin_y, filter->units);
    float fw = svg_resolve_grad_coord(&filter->width, base_w, 0.0f, filter->units);
    float fh = svg_resolve_grad_coord(&filter->height, base_h, 0.0f, filter->units);
    if (fw <= 0.0f || fh <= 0.0f) {
        return;
    }
    float dev_minx, dev_miny, dev_maxx, dev_maxy;
    svg_transform_bounds(m, fx, fy, fx + fw, fy + fh, &dev_minx, &dev_miny, &dev_maxx, &dev_maxy);
    uint32_t ss = ctx->ss;
    int sx0 = (int)floorf(dev_minx * (float)ss);
    int sy0 = (int)floorf(dev_miny * (float)ss);
    int sx1 = (int)ceilf(dev_maxx * (float)ss);
    int sy1 = (int)ceilf(dev_maxy * (float)ss);
    int max_sx = (int)(ctx->width * ss) - 1;
    int max_sy = (int)(ctx->height * ss) - 1;
    if (sx0 < 0) sx0 = 0;
    if (sy0 < 0) sy0 = 0;
    if (sx1 > max_sx) sx1 = max_sx;
    if (sy1 > max_sy) sy1 = max_sy;
    if (sx0 > sx1 || sy0 > sy1) {
        return;
    }
    out->x0 = sx0;
    out->y0 = sy0;
    out->x1 = sx1;
    out->y1 = sy1;
    out->valid = 1;
}

static void svg_compute_filter_op_region(const svg_filter_op *op, const svg_filter *filter,
                                         const svg_render_ctx *ctx, const svg_bbox *bbox,
                                         const svg_matrix *m, const svg_filter_region *parent,
                                         svg_filter_region *out) {
    if (!out || !parent) {
        return;
    }
    *out = *parent;
    if (!op || !filter || !ctx || !bbox || !m || !op->has_region) {
        return;
    }
    svg_bbox use_bbox = *bbox;
    if (!svg_bbox_valid(&use_bbox)) {
        use_bbox.x = ctx->vb_x;
        use_bbox.y = ctx->vb_y;
        use_bbox.w = ctx->vb_w;
        use_bbox.h = ctx->vb_h;
    }
    float bbox_w = use_bbox.w > 0.0f ? use_bbox.w : 1.0f;
    float bbox_h = use_bbox.h > 0.0f ? use_bbox.h : 1.0f;
    float base_w = (filter->units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX) ? bbox_w : ctx->vb_w;
    float base_h = (filter->units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX) ? bbox_h : ctx->vb_h;
    float origin_x = (filter->units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX) ? use_bbox.x : 0.0f;
    float origin_y = (filter->units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX) ? use_bbox.y : 0.0f;
    float fx = svg_resolve_grad_coord(&op->rx, base_w, origin_x, filter->units);
    float fy = svg_resolve_grad_coord(&op->ry, base_h, origin_y, filter->units);
    float fw = svg_resolve_grad_coord(&op->rwidth, base_w, 0.0f, filter->units);
    float fh = svg_resolve_grad_coord(&op->rheight, base_h, 0.0f, filter->units);
    if (fw <= 0.0f || fh <= 0.0f) {
        return;
    }
    float dev_minx, dev_miny, dev_maxx, dev_maxy;
    svg_transform_bounds(m, fx, fy, fx + fw, fy + fh, &dev_minx, &dev_miny, &dev_maxx, &dev_maxy);
    uint32_t ss = ctx->ss;
    int sx0 = (int)floorf(dev_minx * (float)ss);
    int sy0 = (int)floorf(dev_miny * (float)ss);
    int sx1 = (int)ceilf(dev_maxx * (float)ss);
    int sy1 = (int)ceilf(dev_maxy * (float)ss);
    int max_sx = (int)(ctx->width * ss) - 1;
    int max_sy = (int)(ctx->height * ss) - 1;
    if (sx0 < 0) sx0 = 0;
    if (sy0 < 0) sy0 = 0;
    if (sx1 > max_sx) sx1 = max_sx;
    if (sy1 > max_sy) sy1 = max_sy;
    if (sx0 > sx1 || sy0 > sy1) {
        return;
    }
    if (!parent->valid) {
        out->x0 = sx0;
        out->y0 = sy0;
        out->x1 = sx1;
        out->y1 = sy1;
        out->valid = 1;
        return;
    }
    if (sx0 > parent->x0) out->x0 = sx0;
    if (sy0 > parent->y0) out->y0 = sy0;
    if (sx1 < parent->x1) out->x1 = sx1;
    if (sy1 < parent->y1) out->y1 = sy1;
    out->valid = (out->x0 <= out->x1 && out->y0 <= out->y1);
}

static void svg_apply_filter_ops(const svg_filter *filter, svg_render_ctx *ctx, uint8_t *buf, float base_len,
                                 const svg_filter_region *region,
                                 const svg_bbox *bbox, const svg_matrix *m,
                                 uint8_t *background, uint8_t *background_alpha,
                                 uint8_t *fill_paint, uint8_t *stroke_paint) {
    if (!filter || !ctx || !buf || !filter->content || !*filter->content) {
        return;
    }
    svg_filter_op *ops = NULL;
    int count = 0;
    int result_count = 0;
    if (!svg_parse_filter_ops(filter->content, &ops, &count, &result_count,
                              base_len, ctx->dpi, filter->units)) {
        svg_filter_ops_cleanup(ops, count);
        free(ops);
        return;
    }
    uint32_t w = ctx->width * ctx->ss;
    uint32_t h = ctx->height * ctx->ss;
    float scale_x = ctx->vb_w > 0.0f ? (float)(ctx->width * ctx->ss) / ctx->vb_w : 1.0f;
    float scale_y = ctx->vb_h > 0.0f ? (float)(ctx->height * ctx->ss) / ctx->vb_h : 1.0f;
    size_t size = (size_t)w * (size_t)h * 4u;
    uint8_t *source = (uint8_t *)malloc(size);
    if (!source) {
        free(ops);
        return;
    }
    memcpy(source, buf, size);
    uint8_t *source_alpha = NULL;
    uint8_t **results = NULL;
    if (result_count > 0) {
        results = (uint8_t **)calloc((size_t)result_count, sizeof(uint8_t *));
    }
    for (int i = 0; i < count; i++) {
        svg_filter_op *op = &ops[i];
        svg_filter_region clip_region;
        if (region) {
            clip_region = *region;
        } else {
            clip_region.valid = 0;
            clip_region.x0 = 0;
            clip_region.y0 = 0;
            clip_region.x1 = (int)w - 1;
            clip_region.y1 = (int)h - 1;
        }
        if (op->has_region) {
            svg_filter_region parent_region = clip_region;
            svg_compute_filter_op_region(op, filter, ctx, bbox, m, &parent_region, &clip_region);
        }
        int need_alpha = (op->in1.type == SVG_FILTER_IN_SOURCE_ALPHA) ||
                         (op->in2.type == SVG_FILTER_IN_SOURCE_ALPHA);
        if (op->type == SVG_FILTER_OP_MERGE) {
            for (int m = 0; m < op->merge_count; m++) {
                if (op->merge_inputs[m].type == SVG_FILTER_IN_SOURCE_ALPHA) {
                    need_alpha = 1;
                    break;
                }
            }
        }
        if (need_alpha && !source_alpha) {
            source_alpha = (uint8_t *)malloc(size);
            if (source_alpha) {
                for (size_t j = 0; j < (size_t)w * (size_t)h; j++) {
                    uint8_t a = source[j * 4u + 3];
                    source_alpha[j * 4u + 0] = a;
                    source_alpha[j * 4u + 1] = a;
                    source_alpha[j * 4u + 2] = a;
                    source_alpha[j * 4u + 3] = a;
                }
            }
        }
        uint8_t *in1 = svg_filter_resolve_input(&op->in1, buf, source, source_alpha,
                                                background, background_alpha,
                                                fill_paint, stroke_paint, results, result_count);
        if (op->type == SVG_FILTER_OP_BLUR) {
            if (in1 != buf) {
                memcpy(buf, in1, size);
            }
            float sx = fabsf(op->a) * scale_x;
            float sy = fabsf(op->b) * scale_y;
            svg_gaussian_blur(buf, w, h, sx, sy);
            svg_filter_clip_region(buf, w, h, &clip_region);
        } else if (op->type == SVG_FILTER_OP_OFFSET) {
            int dx = (int)lroundf(op->a * scale_x);
            int dy = (int)lroundf(op->b * scale_y);
            if (dx != 0 || dy != 0 || in1 != buf) {
                uint8_t *tmp = (uint8_t *)malloc(size);
                if (!tmp) {
                    continue;
                }
                memset(tmp, 0, size);
                for (uint32_t y = 0; y < h; y++) {
                    int sy = (int)y - dy;
                    if (sy < 0 || sy >= (int)h) {
                        continue;
                    }
                    for (uint32_t x = 0; x < w; x++) {
                        int sx = (int)x - dx;
                        if (sx < 0 || sx >= (int)w) {
                            continue;
                        }
                        size_t src = ((size_t)sy * w + (size_t)sx) * 4u;
                        size_t dst = ((size_t)y * w + (size_t)x) * 4u;
                        tmp[dst + 0] = in1[src + 0];
                        tmp[dst + 1] = in1[src + 1];
                        tmp[dst + 2] = in1[src + 2];
                        tmp[dst + 3] = in1[src + 3];
                    }
                }
                memcpy(buf, tmp, size);
                free(tmp);
            }
            svg_filter_clip_region(buf, w, h, &clip_region);
        } else if (op->type == SVG_FILTER_OP_COLOR_MATRIX && op->has_matrix) {
            if (in1 != buf) {
                memcpy(buf, in1, size);
            }
            for (size_t j = 0; j < (size_t)w * (size_t)h; j++) {
                size_t idx = j * 4u;
                float a = buf[idx + 3] / 255.0f;
                float r = 0.0f, g = 0.0f, b = 0.0f;
                if (a > 0.0f) {
                    r = buf[idx + 0] / 255.0f / a;
                    g = buf[idx + 1] / 255.0f / a;
                    b = buf[idx + 2] / 255.0f / a;
                }
                float nr = op->matrix[0] * r + op->matrix[1] * g + op->matrix[2] * b + op->matrix[3] * a + op->matrix[4];
                float ng = op->matrix[5] * r + op->matrix[6] * g + op->matrix[7] * b + op->matrix[8] * a + op->matrix[9];
                float nb = op->matrix[10] * r + op->matrix[11] * g + op->matrix[12] * b + op->matrix[13] * a + op->matrix[14];
                float na = op->matrix[15] * r + op->matrix[16] * g + op->matrix[17] * b + op->matrix[18] * a + op->matrix[19];
                if (nr < 0.0f) nr = 0.0f;
                if (ng < 0.0f) ng = 0.0f;
                if (nb < 0.0f) nb = 0.0f;
                if (na < 0.0f) na = 0.0f;
                if (nr > 1.0f) nr = 1.0f;
                if (ng > 1.0f) ng = 1.0f;
                if (nb > 1.0f) nb = 1.0f;
                if (na > 1.0f) na = 1.0f;
                buf[idx + 3] = (uint8_t)lroundf(na * 255.0f);
                buf[idx + 0] = (uint8_t)lroundf(nr * na * 255.0f);
                buf[idx + 1] = (uint8_t)lroundf(ng * na * 255.0f);
                buf[idx + 2] = (uint8_t)lroundf(nb * na * 255.0f);
            }
            svg_filter_clip_region(buf, w, h, &clip_region);
        } else if (op->type == SVG_FILTER_OP_COMPONENT_TRANSFER) {
            if (in1 != buf) {
                memcpy(buf, in1, size);
            }
            for (size_t j = 0; j < (size_t)w * (size_t)h; j++) {
                size_t idx = j * 4u;
                float a = buf[idx + 3] / 255.0f;
                float r = 0.0f, g = 0.0f, b = 0.0f;
                if (a > 0.0f) {
                    r = buf[idx + 0] / 255.0f / a;
                    g = buf[idx + 1] / 255.0f / a;
                    b = buf[idx + 2] / 255.0f / a;
                }
                float nr = svg_transfer_apply(&op->tr, r);
                float ng = svg_transfer_apply(&op->tg, g);
                float nb = svg_transfer_apply(&op->tb, b);
                float na = svg_transfer_apply(&op->ta, a);
                if (nr < 0.0f) nr = 0.0f;
                if (ng < 0.0f) ng = 0.0f;
                if (nb < 0.0f) nb = 0.0f;
                if (na < 0.0f) na = 0.0f;
                if (nr > 1.0f) nr = 1.0f;
                if (ng > 1.0f) ng = 1.0f;
                if (nb > 1.0f) nb = 1.0f;
                if (na > 1.0f) na = 1.0f;
                buf[idx + 3] = (uint8_t)lroundf(na * 255.0f);
                buf[idx + 0] = (uint8_t)lroundf(nr * na * 255.0f);
                buf[idx + 1] = (uint8_t)lroundf(ng * na * 255.0f);
                buf[idx + 2] = (uint8_t)lroundf(nb * na * 255.0f);
            }
            svg_filter_clip_region(buf, w, h, &clip_region);
        } else if (op->type == SVG_FILTER_OP_CONVOLVE) {
            if (in1 != buf) {
                memcpy(buf, in1, size);
            }
            uint8_t *tmp = (uint8_t *)malloc(size);
            if (!tmp) {
                continue;
            }
            int ox = op->order_x > 0 ? op->order_x : 3;
            int oy = op->order_y > 0 ? op->order_y : 3;
            int tx = op->target_x;
            int ty = op->target_y;
            if (tx < 0) tx = 0;
            if (ty < 0) ty = 0;
            if (tx >= ox) tx = ox - 1;
            if (ty >= oy) ty = oy - 1;
            float divisor = (fabsf(op->divisor) < 1e-6f) ? 1.0f : op->divisor;
            for (uint32_t y = 0; y < h; y++) {
                for (uint32_t x = 0; x < w; x++) {
                    float acc_r = 0.0f;
                    float acc_g = 0.0f;
                    float acc_b = 0.0f;
                    float acc_a = 0.0f;
                    for (int ky = 0; ky < oy; ky++) {
                        int sy = (int)y + (ky - ty);
                        if (op->mode == SVG_EDGE_DUPLICATE) {
                            if (sy < 0) sy = 0;
                            if (sy >= (int)h) sy = (int)h - 1;
                        } else if (op->mode == SVG_EDGE_WRAP) {
                            if (sy < 0) sy = (sy % (int)h + (int)h) % (int)h;
                            if (sy >= (int)h) sy %= (int)h;
                        } else {
                            if (sy < 0 || sy >= (int)h) {
                                continue;
                            }
                        }
                        for (int kx = 0; kx < ox; kx++) {
                            int sx = (int)x + (kx - tx);
                            if (op->mode == SVG_EDGE_DUPLICATE) {
                                if (sx < 0) sx = 0;
                                if (sx >= (int)w) sx = (int)w - 1;
                            } else if (op->mode == SVG_EDGE_WRAP) {
                                if (sx < 0) sx = (sx % (int)w + (int)w) % (int)w;
                                if (sx >= (int)w) sx %= (int)w;
                            } else {
                                if (sx < 0 || sx >= (int)w) {
                                    continue;
                                }
                            }
                            size_t sidx = ((size_t)sy * w + (size_t)sx) * 4u;
                            float k = op->kernel ? op->kernel[ky * ox + kx] : 0.0f;
                            acc_r += (float)in1[sidx + 0] * k;
                            acc_g += (float)in1[sidx + 1] * k;
                            acc_b += (float)in1[sidx + 2] * k;
                            acc_a += (float)in1[sidx + 3] * k;
                        }
                    }
                    acc_r = acc_r / divisor + op->bias * 255.0f;
                    acc_g = acc_g / divisor + op->bias * 255.0f;
                    acc_b = acc_b / divisor + op->bias * 255.0f;
                    acc_a = acc_a / divisor + op->bias * 255.0f;
                    size_t didx = ((size_t)y * w + (size_t)x) * 4u;
                    if (op->preserve_alpha) {
                        uint8_t oa = in1[didx + 3];
                        tmp[didx + 3] = oa;
                        float a = oa / 255.0f;
                        tmp[didx + 0] = svg_clamp_u8(acc_r * a / 255.0f);
                        tmp[didx + 1] = svg_clamp_u8(acc_g * a / 255.0f);
                        tmp[didx + 2] = svg_clamp_u8(acc_b * a / 255.0f);
                    } else {
                        tmp[didx + 0] = svg_clamp_u8(acc_r);
                        tmp[didx + 1] = svg_clamp_u8(acc_g);
                        tmp[didx + 2] = svg_clamp_u8(acc_b);
                        tmp[didx + 3] = svg_clamp_u8(acc_a);
                    }
                }
            }
            memcpy(buf, tmp, size);
            free(tmp);
            svg_filter_clip_region(buf, w, h, &clip_region);
        } else if (op->type == SVG_FILTER_OP_TURBULENCE) {
            float fx = op->turb_freq_x;
            float fy = op->turb_freq_y;
            if (fx <= 0.0f) fx = 0.01f;
            if (fy <= 0.0f) fy = 0.01f;
            int octaves = op->turb_octaves;
            if (octaves < 1) octaves = 1;
            float max_amp = 0.0f;
            float amp = 1.0f;
            for (int o = 0; o < octaves; o++) {
                max_amp += amp;
                amp *= 0.5f;
            }
            float inv_sx = scale_x > 1e-6f ? 1.0f / scale_x : 1.0f;
            float inv_sy = scale_y > 1e-6f ? 1.0f / scale_y : 1.0f;
            for (uint32_t y = 0; y < h; y++) {
                for (uint32_t x = 0; x < w; x++) {
                    float ux = (float)x * inv_sx;
                    float uy = (float)y * inv_sy;
                    float sum = 0.0f;
                    float freqx = fx;
                    float freqy = fy;
                    float a = 1.0f;
                    for (int o = 0; o < octaves; o++) {
                        float n = svg_noise2(ux * freqx, uy * freqy, op->turb_seed + o * 17);
                        if (op->turb_fractal) {
                            sum += n * a;
                        } else {
                            sum += fabsf(n) * a;
                        }
                        a *= 0.5f;
                        freqx *= 2.0f;
                        freqy *= 2.0f;
                    }
                    float val = op->turb_fractal ? (sum / max_amp * 0.5f + 0.5f) : (sum / max_amp);
                    if (val < 0.0f) val = 0.0f;
                    if (val > 1.0f) val = 1.0f;
                    uint8_t c = (uint8_t)lroundf(val * 255.0f);
                    size_t idx = ((size_t)y * w + (size_t)x) * 4u;
                    buf[idx + 0] = c;
                    buf[idx + 1] = c;
                    buf[idx + 2] = c;
                    buf[idx + 3] = 255;
                }
            }
            svg_filter_clip_region(buf, w, h, &clip_region);
        } else if (op->type == SVG_FILTER_OP_DISPLACEMENT) {
            uint8_t *in2 = svg_filter_resolve_input(&op->in2, buf, source, source_alpha,
                                                    background, background_alpha,
                                                    fill_paint, stroke_paint, results, result_count);
            uint8_t *tmp = (uint8_t *)malloc(size);
            if (!tmp) {
                continue;
            }
            float scale_dx = op->a * scale_x;
            float scale_dy = op->a * scale_y;
            for (uint32_t y = 0; y < h; y++) {
                for (uint32_t x = 0; x < w; x++) {
                    size_t idx = ((size_t)y * w + (size_t)x) * 4u;
                    float cx = svg_channel_value(in2, idx, op->displace_x_channel);
                    float cy = svg_channel_value(in2, idx, op->displace_y_channel);
                    float dx = (cx - 0.5f) * scale_dx;
                    float dy = (cy - 0.5f) * scale_dy;
                    int sx = (int)lroundf((float)x + dx);
                    int sy = (int)lroundf((float)y + dy);
                    if (sx < 0) sx = 0;
                    if (sy < 0) sy = 0;
                    if (sx >= (int)w) sx = (int)w - 1;
                    if (sy >= (int)h) sy = (int)h - 1;
                    size_t sidx = ((size_t)sy * w + (size_t)sx) * 4u;
                    tmp[idx + 0] = in1[sidx + 0];
                    tmp[idx + 1] = in1[sidx + 1];
                    tmp[idx + 2] = in1[sidx + 2];
                    tmp[idx + 3] = in1[sidx + 3];
                }
            }
            memcpy(buf, tmp, size);
            free(tmp);
            svg_filter_clip_region(buf, w, h, &clip_region);
        } else if (op->type == SVG_FILTER_OP_DIFFUSE_LIGHTING ||
                   op->type == SVG_FILTER_OP_SPECULAR_LIGHTING) {
            if (in1 != buf) {
                memcpy(buf, in1, size);
            }
            float lx = op->light_x;
            float ly = op->light_y;
            float lz = op->light_z;
            float llen = sqrtf(lx * lx + ly * ly + lz * lz);
            if (llen <= 1e-6f) {
                lx = 0.0f; ly = 0.0f; lz = 1.0f;
                llen = 1.0f;
            }
            lx /= llen; ly /= llen; lz /= llen;
            uint8_t lr = (uint8_t)(op->color >> 24);
            uint8_t lg = (uint8_t)(op->color >> 16);
            uint8_t lb = (uint8_t)(op->color >> 8);
            float lrf = lr / 255.0f;
            float lgf = lg / 255.0f;
            float lbf = lb / 255.0f;
            for (uint32_t y = 0; y < h; y++) {
                for (uint32_t x = 0; x < w; x++) {
                    size_t idx = ((size_t)y * w + (size_t)x) * 4u;
                    int xi = (int)x;
                    int yi = (int)y;
                    float a00 = svg_alpha_sample_clamped(in1, w, h, xi - 1, yi - 1);
                    float a10 = svg_alpha_sample_clamped(in1, w, h, xi, yi - 1);
                    float a20 = svg_alpha_sample_clamped(in1, w, h, xi + 1, yi - 1);
                    float a01 = svg_alpha_sample_clamped(in1, w, h, xi - 1, yi);
                    float a21 = svg_alpha_sample_clamped(in1, w, h, xi + 1, yi);
                    float a02 = svg_alpha_sample_clamped(in1, w, h, xi - 1, yi + 1);
                    float a12 = svg_alpha_sample_clamped(in1, w, h, xi, yi + 1);
                    float a22 = svg_alpha_sample_clamped(in1, w, h, xi + 1, yi + 1);
                    float sx = (a20 + 2.0f * a21 + a22) - (a00 + 2.0f * a01 + a02);
                    float sy = (a02 + 2.0f * a12 + a22) - (a00 + 2.0f * a10 + a20);
                    float dx = sx * op->surface_scale;
                    float dy = sy * op->surface_scale;
                    float nx = -dx;
                    float ny = -dy;
                    float nz = 1.0f;
                    float nlen = sqrtf(nx * nx + ny * ny + nz * nz);
                    if (nlen > 1e-6f) {
                        nx /= nlen; ny /= nlen; nz /= nlen;
                    }
                    float intensity = 0.0f;
                    if (op->type == SVG_FILTER_OP_DIFFUSE_LIGHTING) {
                        float dotnl = nx * lx + ny * ly + nz * lz;
                        if (dotnl > 0.0f) {
                            intensity = dotnl * op->diffuse_constant;
                        }
                    } else {
                        float dotnl = nx * lx + ny * ly + nz * lz;
                        if (dotnl > 0.0f) {
                            float rz = 2.0f * dotnl * nz - lz;
                            float dotrv = rz;
                            if (dotrv > 0.0f) {
                                intensity = powf(dotrv, op->specular_exponent) * op->specular_constant;
                            }
                        }
                    }
                    float base_a = in1[idx + 3] / 255.0f;
                    float out_a = 0.0f;
                    float out_r = 0.0f;
                    float out_g = 0.0f;
                    float out_b = 0.0f;
                    if (op->type == SVG_FILTER_OP_DIFFUSE_LIGHTING) {
                        if (intensity < 0.0f) intensity = 0.0f;
                        if (intensity > 1.0f) intensity = 1.0f;
                        out_a = base_a;
                        out_r = lrf * intensity;
                        out_g = lgf * intensity;
                        out_b = lbf * intensity;
                    } else {
                        if (intensity < 0.0f) intensity = 0.0f;
                        if (intensity > 1.0f) intensity = 1.0f;
                        out_a = base_a * intensity;
                        out_r = lrf;
                        out_g = lgf;
                        out_b = lbf;
                    }
                    buf[idx + 3] = svg_clamp_u8(out_a * 255.0f);
                    buf[idx + 0] = svg_clamp_u8(out_r * out_a * 255.0f);
                    buf[idx + 1] = svg_clamp_u8(out_g * out_a * 255.0f);
                    buf[idx + 2] = svg_clamp_u8(out_b * out_a * 255.0f);
                }
            }
            svg_filter_clip_region(buf, w, h, &clip_region);
        } else if (op->type == SVG_FILTER_OP_FLOOD) {
            uint8_t a = (uint8_t)(op->color & 0xFFu);
            uint8_t r = (uint8_t)(op->color >> 24);
            uint8_t g = (uint8_t)(op->color >> 16);
            uint8_t b = (uint8_t)(op->color >> 8);
            uint8_t pr = (uint8_t)((r * a) / 255u);
            uint8_t pg = (uint8_t)((g * a) / 255u);
            uint8_t pb = (uint8_t)((b * a) / 255u);
            for (size_t j = 0; j < (size_t)w * (size_t)h; j++) {
                size_t idx = j * 4u;
                buf[idx + 0] = pr;
                buf[idx + 1] = pg;
                buf[idx + 2] = pb;
                buf[idx + 3] = a;
            }
            svg_filter_clip_region(buf, w, h, &clip_region);
        } else if (op->type == SVG_FILTER_OP_BLEND || op->type == SVG_FILTER_OP_COMPOSITE) {
            uint8_t *in2 = svg_filter_resolve_input(&op->in2, buf, source, source_alpha,
                                                    background, background_alpha,
                                                    fill_paint, stroke_paint, results, result_count);
            uint8_t *tmp = (uint8_t *)malloc(size);
            if (!tmp) {
                continue;
            }
            for (size_t j = 0; j < (size_t)w * (size_t)h; j++) {
                size_t idx = j * 4u;
                float a1 = in1[idx + 3] / 255.0f;
                float a2 = in2[idx + 3] / 255.0f;
                float r1 = 0.0f, g1 = 0.0f, b1 = 0.0f;
                float r2 = 0.0f, g2 = 0.0f, b2 = 0.0f;
                if (a1 > 0.0f) {
                    r1 = in1[idx + 0] / 255.0f / a1;
                    g1 = in1[idx + 1] / 255.0f / a1;
                    b1 = in1[idx + 2] / 255.0f / a1;
                }
                if (a2 > 0.0f) {
                    r2 = in2[idx + 0] / 255.0f / a2;
                    g2 = in2[idx + 1] / 255.0f / a2;
                    b2 = in2[idx + 2] / 255.0f / a2;
                }
                float out_a = a1 + a2 - a1 * a2;
                float out_r = 0.0f;
                float out_g = 0.0f;
                float out_b = 0.0f;
                if (op->type == SVG_FILTER_OP_BLEND) {
                    float br = r2;
                    float bg = g2;
                    float bb = b2;
                    if (op->mode == SVG_BLEND_MULTIPLY) {
                        br = r1 * r2;
                        bg = g1 * g2;
                        bb = b1 * b2;
                    } else if (op->mode == SVG_BLEND_SCREEN) {
                        br = 1.0f - (1.0f - r1) * (1.0f - r2);
                        bg = 1.0f - (1.0f - g1) * (1.0f - g2);
                        bb = 1.0f - (1.0f - b1) * (1.0f - b2);
                    } else if (op->mode == SVG_BLEND_DARKEN) {
                        br = fminf(r1, r2);
                        bg = fminf(g1, g2);
                        bb = fminf(b1, b2);
                    } else if (op->mode == SVG_BLEND_LIGHTEN) {
                        br = fmaxf(r1, r2);
                        bg = fmaxf(g1, g2);
                        bb = fmaxf(b1, b2);
                    } else if (op->mode == SVG_BLEND_OVERLAY) {
                        br = (r1 <= 0.5f) ? (2.0f * r1 * r2) : (1.0f - 2.0f * (1.0f - r1) * (1.0f - r2));
                        bg = (g1 <= 0.5f) ? (2.0f * g1 * g2) : (1.0f - 2.0f * (1.0f - g1) * (1.0f - g2));
                        bb = (b1 <= 0.5f) ? (2.0f * b1 * b2) : (1.0f - 2.0f * (1.0f - b1) * (1.0f - b2));
                    } else if (op->mode == SVG_BLEND_HARDLIGHT) {
                        br = (r2 <= 0.5f) ? (2.0f * r1 * r2) : (1.0f - 2.0f * (1.0f - r1) * (1.0f - r2));
                        bg = (g2 <= 0.5f) ? (2.0f * g1 * g2) : (1.0f - 2.0f * (1.0f - g1) * (1.0f - g2));
                        bb = (b2 <= 0.5f) ? (2.0f * b1 * b2) : (1.0f - 2.0f * (1.0f - b1) * (1.0f - b2));
                    } else if (op->mode == SVG_BLEND_SOFTLIGHT) {
                        float gr = (r1 <= 0.25f) ? (((16.0f * r1 - 12.0f) * r1 + 4.0f) * r1) : sqrtf(fmaxf(r1, 0.0f));
                        float gg = (g1 <= 0.25f) ? (((16.0f * g1 - 12.0f) * g1 + 4.0f) * g1) : sqrtf(fmaxf(g1, 0.0f));
                        float gb = (b1 <= 0.25f) ? (((16.0f * b1 - 12.0f) * b1 + 4.0f) * b1) : sqrtf(fmaxf(b1, 0.0f));
                        br = (r2 <= 0.5f) ? (r1 - (1.0f - 2.0f * r2) * r1 * (1.0f - r1))
                                          : (r1 + (2.0f * r2 - 1.0f) * (gr - r1));
                        bg = (g2 <= 0.5f) ? (g1 - (1.0f - 2.0f * g2) * g1 * (1.0f - g1))
                                          : (g1 + (2.0f * g2 - 1.0f) * (gg - g1));
                        bb = (b2 <= 0.5f) ? (b1 - (1.0f - 2.0f * b2) * b1 * (1.0f - b1))
                                          : (b1 + (2.0f * b2 - 1.0f) * (gb - b1));
                    }
                    float c1r = (float)in1[idx + 0] / 255.0f;
                    float c1g = (float)in1[idx + 1] / 255.0f;
                    float c1b = (float)in1[idx + 2] / 255.0f;
                    float c2r = (float)in2[idx + 0] / 255.0f;
                    float c2g = (float)in2[idx + 1] / 255.0f;
                    float c2b = (float)in2[idx + 2] / 255.0f;
                    float pr = c1r * (1.0f - a2) + c2r * (1.0f - a1) + br * a1 * a2;
                    float pg = c1g * (1.0f - a2) + c2g * (1.0f - a1) + bg * a1 * a2;
                    float pb = c1b * (1.0f - a2) + c2b * (1.0f - a1) + bb * a1 * a2;
                    if (out_a > 1e-6f) {
                        out_r = pr / out_a;
                        out_g = pg / out_a;
                        out_b = pb / out_a;
                    } else {
                        out_r = 0.0f;
                        out_g = 0.0f;
                        out_b = 0.0f;
                    }
                } else {
                    float ar = (float)in1[idx + 0] / 255.0f;
                    float ag = (float)in1[idx + 1] / 255.0f;
                    float ab = (float)in1[idx + 2] / 255.0f;
                    float br = (float)in2[idx + 0] / 255.0f;
                    float bg = (float)in2[idx + 1] / 255.0f;
                    float bb = (float)in2[idx + 2] / 255.0f;
                    float out_pr = 0.0f, out_pg = 0.0f, out_pb = 0.0f;
                    float out_pa = 0.0f;
                    if (op->mode == SVG_COMPOSITE_OVER) {
                        out_pr = ar + br * (1.0f - a1);
                        out_pg = ag + bg * (1.0f - a1);
                        out_pb = ab + bb * (1.0f - a1);
                        out_pa = a1 + a2 * (1.0f - a1);
                    } else if (op->mode == SVG_COMPOSITE_IN) {
                        out_pr = ar * a2;
                        out_pg = ag * a2;
                        out_pb = ab * a2;
                        out_pa = a1 * a2;
                    } else if (op->mode == SVG_COMPOSITE_OUT) {
                        out_pr = ar * (1.0f - a2);
                        out_pg = ag * (1.0f - a2);
                        out_pb = ab * (1.0f - a2);
                        out_pa = a1 * (1.0f - a2);
                    } else if (op->mode == SVG_COMPOSITE_ATOP) {
                        out_pr = ar * a2 + br * (1.0f - a1);
                        out_pg = ag * a2 + bg * (1.0f - a1);
                        out_pb = ab * a2 + bb * (1.0f - a1);
                        out_pa = a2;
                    } else if (op->mode == SVG_COMPOSITE_XOR) {
                        out_pr = ar * (1.0f - a2) + br * (1.0f - a1);
                        out_pg = ag * (1.0f - a2) + bg * (1.0f - a1);
                        out_pb = ab * (1.0f - a2) + bb * (1.0f - a1);
                        out_pa = a1 + a2 - 2.0f * a1 * a2;
                    } else if (op->mode == SVG_COMPOSITE_ARITHMETIC) {
                        out_pr = op->a * ar * br + op->b * ar + op->c * br + op->d;
                        out_pg = op->a * ag * bg + op->b * ag + op->c * bg + op->d;
                        out_pb = op->a * ab * bb + op->b * ab + op->c * bb + op->d;
                        out_pa = op->a * a1 * a2 + op->b * a1 + op->c * a2 + op->d;
                    }
                    if (out_pr < 0.0f) out_pr = 0.0f;
                    if (out_pg < 0.0f) out_pg = 0.0f;
                    if (out_pb < 0.0f) out_pb = 0.0f;
                    if (out_pa < 0.0f) out_pa = 0.0f;
                    if (out_pr > 1.0f) out_pr = 1.0f;
                    if (out_pg > 1.0f) out_pg = 1.0f;
                    if (out_pb > 1.0f) out_pb = 1.0f;
                    if (out_pa > 1.0f) out_pa = 1.0f;
                    tmp[idx + 0] = (uint8_t)lroundf(out_pr * 255.0f);
                    tmp[idx + 1] = (uint8_t)lroundf(out_pg * 255.0f);
                    tmp[idx + 2] = (uint8_t)lroundf(out_pb * 255.0f);
                    tmp[idx + 3] = (uint8_t)lroundf(out_pa * 255.0f);
                    continue;
                }
                if (out_a < 0.0f) out_a = 0.0f;
                if (out_a > 1.0f) out_a = 1.0f;
                if (out_r < 0.0f) out_r = 0.0f;
                if (out_g < 0.0f) out_g = 0.0f;
                if (out_b < 0.0f) out_b = 0.0f;
                if (out_r > 1.0f) out_r = 1.0f;
                if (out_g > 1.0f) out_g = 1.0f;
                if (out_b > 1.0f) out_b = 1.0f;
                tmp[idx + 3] = (uint8_t)lroundf(out_a * 255.0f);
                tmp[idx + 0] = (uint8_t)lroundf(out_r * out_a * 255.0f);
                tmp[idx + 1] = (uint8_t)lroundf(out_g * out_a * 255.0f);
                tmp[idx + 2] = (uint8_t)lroundf(out_b * out_a * 255.0f);
            }
            memcpy(buf, tmp, size);
            free(tmp);
            svg_filter_clip_region(buf, w, h, &clip_region);
        } else if (op->type == SVG_FILTER_OP_TILE) {
            if (in1 != buf) {
                memcpy(buf, in1, size);
            }
            if (clip_region.valid) {
                int x0 = clip_region.x0;
                int y0 = clip_region.y0;
                int x1 = clip_region.x1;
                int y1 = clip_region.y1;
                int tw = x1 - x0 + 1;
                int th = y1 - y0 + 1;
                if (tw > 0 && th > 0) {
                    for (int y = y0; y <= y1; y++) {
                        for (int x = x0; x <= x1; x++) {
                            int sx = x0 + ((x - x0) % tw);
                            int sy = y0 + ((y - y0) % th);
                            size_t sidx = ((size_t)sy * w + (size_t)sx) * 4u;
                            size_t didx = ((size_t)y * w + (size_t)x) * 4u;
                            buf[didx + 0] = in1[sidx + 0];
                            buf[didx + 1] = in1[sidx + 1];
                            buf[didx + 2] = in1[sidx + 2];
                            buf[didx + 3] = in1[sidx + 3];
                        }
                    }
                }
            }
            svg_filter_clip_region(buf, w, h, &clip_region);
        } else if (op->type == SVG_FILTER_OP_IMAGE) {
            memset(buf, 0, size);
            if (op->image_href && *op->image_href) {
                cupidimage_image img;
                if (svg_load_href_image(op->image_href, &img)) {
                    float draw_w = op->img_w;
                    float draw_h = op->img_h;
                    if (draw_w <= 0.0f) {
                        draw_w = (float)img.width / (scale_x > 0.0f ? scale_x : 1.0f);
                    }
                    if (draw_h <= 0.0f) {
                        draw_h = (float)img.height / (scale_y > 0.0f ? scale_y : 1.0f);
                    }
                    float draw_x = op->img_x;
                    float draw_y = op->img_y;
                    int px = (int)lroundf(draw_x * scale_x);
                    int py = (int)lroundf(draw_y * scale_y);
                    int pw = (int)lroundf(draw_w * scale_x);
                    int ph = (int)lroundf(draw_h * scale_y);
                    if (pw < 1) pw = 1;
                    if (ph < 1) ph = 1;
                    for (int y = 0; y < ph; y++) {
                        int dy = py + y;
                        if (dy < 0 || dy >= (int)h) continue;
                        int sy = (int)((float)y / (float)ph * (float)img.height);
                        if (sy < 0) sy = 0;
                        if (sy >= (int)img.height) sy = (int)img.height - 1;
                        for (int x = 0; x < pw; x++) {
                            int dx = px + x;
                            if (dx < 0 || dx >= (int)w) continue;
                            int sx = (int)((float)x / (float)pw * (float)img.width);
                            if (sx < 0) sx = 0;
                            if (sx >= (int)img.width) sx = (int)img.width - 1;
                            size_t sidx = ((size_t)sy * img.width + (size_t)sx) * 4u;
                            size_t didx = ((size_t)dy * w + (size_t)dx) * 4u;
                            uint8_t a = img.rgba[sidx + 3];
                            uint8_t pr = (uint8_t)((img.rgba[sidx + 0] * a) / 255u);
                            uint8_t pg = (uint8_t)((img.rgba[sidx + 1] * a) / 255u);
                            uint8_t pb = (uint8_t)((img.rgba[sidx + 2] * a) / 255u);
                            buf[didx + 0] = pr;
                            buf[didx + 1] = pg;
                            buf[didx + 2] = pb;
                            buf[didx + 3] = a;
                        }
                    }
                    cupidimage_free(&img);
                }
            }
            svg_filter_clip_region(buf, w, h, &clip_region);
        } else if (op->type == SVG_FILTER_OP_MORPHOLOGY) {
            int rx = (int)lroundf(fabsf(op->a) * scale_x);
            int ry = (int)lroundf(fabsf(op->b) * scale_y);
            if (rx < 0) rx = 0;
            if (ry < 0) ry = 0;
            if (rx == 0 && ry == 0) {
                if (in1 != buf) {
                    memcpy(buf, in1, size);
                }
            } else {
                uint8_t *src = (uint8_t *)malloc(size);
                if (!src) {
                    continue;
                }
                memcpy(src, in1, size);
                for (uint32_t y = 0; y < h; y++) {
                    for (uint32_t x = 0; x < w; x++) {
                        uint8_t best_r = (op->mode == SVG_MORPHOLOGY_DILATE) ? 0 : 255;
                        uint8_t best_g = (op->mode == SVG_MORPHOLOGY_DILATE) ? 0 : 255;
                        uint8_t best_b = (op->mode == SVG_MORPHOLOGY_DILATE) ? 0 : 255;
                        uint8_t best_a = (op->mode == SVG_MORPHOLOGY_DILATE) ? 0 : 255;
                        int y0 = (int)y - ry;
                        int y1 = (int)y + ry;
                        int x0 = (int)x - rx;
                        int x1 = (int)x + rx;
                        if (y0 < 0) y0 = 0;
                        if (x0 < 0) x0 = 0;
                        if (y1 >= (int)h) y1 = (int)h - 1;
                        if (x1 >= (int)w) x1 = (int)w - 1;
                        for (int yy = y0; yy <= y1; yy++) {
                            for (int xx = x0; xx <= x1; xx++) {
                                size_t sidx = ((size_t)yy * w + (size_t)xx) * 4u;
                                uint8_t sr = src[sidx + 0];
                                uint8_t sg = src[sidx + 1];
                                uint8_t sb = src[sidx + 2];
                                uint8_t sa = src[sidx + 3];
                                if (op->mode == SVG_MORPHOLOGY_DILATE) {
                                    if (sr > best_r) best_r = sr;
                                    if (sg > best_g) best_g = sg;
                                    if (sb > best_b) best_b = sb;
                                    if (sa > best_a) best_a = sa;
                                } else {
                                    if (sr < best_r) best_r = sr;
                                    if (sg < best_g) best_g = sg;
                                    if (sb < best_b) best_b = sb;
                                    if (sa < best_a) best_a = sa;
                                }
                            }
                        }
                        size_t didx = ((size_t)y * w + (size_t)x) * 4u;
                        buf[didx + 0] = best_r;
                        buf[didx + 1] = best_g;
                        buf[didx + 2] = best_b;
                        buf[didx + 3] = best_a;
                    }
                }
                free(src);
            }
            svg_filter_clip_region(buf, w, h, &clip_region);
        } else if (op->type == SVG_FILTER_OP_MERGE) {
            uint8_t *tmp = (uint8_t *)calloc(1u, size);
            if (!tmp) {
                continue;
            }
            int mcount = op->merge_count;
            if (mcount == 0) {
                mcount = 1;
                op->merge_inputs[0] = svg_make_filter_ref(SVG_FILTER_IN_SOURCE_GRAPHIC, -1);
            }
            for (int m = 0; m < mcount; m++) {
                uint8_t *inm = svg_filter_resolve_input(&op->merge_inputs[m], buf, source,
                                                        source_alpha, background, background_alpha,
                                                        fill_paint, stroke_paint, results, result_count);
                for (size_t j = 0; j < (size_t)w * (size_t)h; j++) {
                    size_t idx = j * 4u;
                    svg_blend_premul(tmp + idx, inm[idx + 0], inm[idx + 1], inm[idx + 2], inm[idx + 3]);
                }
            }
            memcpy(buf, tmp, size);
            free(tmp);
            svg_filter_clip_region(buf, w, h, &clip_region);
        }
        if (op->result_id >= 0 && results && op->result_id < result_count) {
            free(results[op->result_id]);
            results[op->result_id] = (uint8_t *)malloc(size);
            if (results[op->result_id]) {
                memcpy(results[op->result_id], buf, size);
            }
        }
    }
    if (results) {
        for (int i = 0; i < result_count; i++) {
            free(results[i]);
        }
        free(results);
    }
    svg_filter_ops_cleanup(ops, count);
    free(source_alpha);
    free(source);
    free(ops);
}

static void svg_composite_buffer(svg_render_ctx *ctx, const uint8_t *src) {
    if (!ctx || !ctx->hi_rgba || !src) {
        return;
    }
    size_t total = (size_t)ctx->width * (size_t)ctx->height * (size_t)ctx->ss * (size_t)ctx->ss;
    for (size_t i = 0; i < total; i++) {
        size_t idx = i * 4u;
        uint8_t sa = src[idx + 3];
        if (sa == 0) {
            continue;
        }
        svg_blend_premul(ctx->hi_rgba + idx, src[idx + 0], src[idx + 1], src[idx + 2], sa);
    }
}
static int svg_pattern_prepare(svg_pattern *p, const svg_render_ctx *ctx, const svg_bbox *bbox) {
    if (!p || !ctx || !bbox || !p->content || !*p->content) {
        return 0;
    }
    if (p->rendering) {
        return 0;
    }
    float bbox_w = bbox->w > 0.0f ? bbox->w : 1.0f;
    float bbox_h = bbox->h > 0.0f ? bbox->h : 1.0f;
    float base_w = (p->units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX) ? bbox_w : ctx->vb_w;
    float base_h = (p->units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX) ? bbox_h : ctx->vb_h;
    float origin_x = (p->units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX) ? bbox->x : 0.0f;
    float origin_y = (p->units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX) ? bbox->y : 0.0f;
    float tile_x = svg_resolve_grad_coord(&p->x, base_w, origin_x, p->units);
    float tile_y = svg_resolve_grad_coord(&p->y, base_h, origin_y, p->units);
    float tile_w = svg_resolve_grad_coord(&p->width, base_w, 0.0f, p->units);
    float tile_h = svg_resolve_grad_coord(&p->height, base_h, 0.0f, p->units);
    if (tile_w <= 0.0f || tile_h <= 0.0f) {
        return 0;
    }
    uint32_t px_w = (uint32_t)lroundf(tile_w * (float)ctx->ss);
    uint32_t px_h = (uint32_t)lroundf(tile_h * (float)ctx->ss);
    if (px_w < 1) px_w = 1;
    if (px_h < 1) px_h = 1;
    int needs_bbox = (p->units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX) ||
                     (p->content_units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX);
    if (p->rendered && p->rgba && p->tile_w == px_w && p->tile_h == px_h) {
        if (!needs_bbox) {
            return 1;
        }
        if (p->has_cached_bbox &&
            fabsf(p->cached_bbox_x - bbox->x) < 1e-4f &&
            fabsf(p->cached_bbox_y - bbox->y) < 1e-4f &&
            fabsf(p->cached_bbox_w - bbox->w) < 1e-4f &&
            fabsf(p->cached_bbox_h - bbox->h) < 1e-4f) {
            return 1;
        }
    }

    free(p->rgba);
    p->rgba = NULL;
    p->rendered = 0;
    p->rendering = 1;

    char header[256];
    int header_len = snprintf(header, sizeof(header),
                              "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%.6g\" height=\"%.6g\" "
                              "viewBox=\"%.6g %.6g %.6g %.6g\">",
                              tile_w, tile_h, tile_x, tile_y, tile_w, tile_h);
    if (header_len < 0 || header_len >= (int)sizeof(header)) {
        p->rendering = 0;
        return 0;
    }
    char gopen[160];
    int gopen_len = 0;
    if (p->content_units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX) {
        float cw = bbox->w > 0.0f ? bbox->w : 1.0f;
        float ch = bbox->h > 0.0f ? bbox->h : 1.0f;
        gopen_len = snprintf(gopen, sizeof(gopen),
                             "<g transform=\"translate(%.6g %.6g) scale(%.6g %.6g)\">",
                             bbox->x, bbox->y, cw, ch);
        if (gopen_len < 0 || gopen_len >= (int)sizeof(gopen)) {
            p->rendering = 0;
            return 0;
        }
    }
    const char *gclose = (p->content_units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX) ? "</g>" : "";
    const char *footer = "</svg>";
    size_t content_len = strlen(p->content);
    size_t gclose_len = strlen(gclose);
    size_t footer_len = strlen(footer);
    size_t total = (size_t)header_len + (size_t)gopen_len + content_len + gclose_len + footer_len + 1u;
    char *svg = (char *)malloc(total);
    if (!svg) {
        p->rendering = 0;
        return 0;
    }
    char *dst = svg;
    memcpy(dst, header, (size_t)header_len);
    dst += header_len;
    if (gopen_len > 0) {
        memcpy(dst, gopen, (size_t)gopen_len);
        dst += gopen_len;
    }
    if (content_len > 0) {
        memcpy(dst, p->content, content_len);
        dst += content_len;
    }
    if (gclose_len > 0) {
        memcpy(dst, gclose, gclose_len);
        dst += gclose_len;
    }
    memcpy(dst, footer, footer_len);
    dst += footer_len;
    *dst = '\0';

    cupidimage_svg_options opts;
    opts.width = px_w;
    opts.height = px_h;
    opts.scale = 1.0f;
    opts.dpi = ctx->dpi;
    opts.animation_time = ctx->anim_time;
    opts.supersampling = 1;
    opts.background_alpha = 0;

    svg_render_ctx tile_ctx;
    memset(&tile_ctx, 0, sizeof(tile_ctx));
    char errbuf[128];
    int ok = svg_render(&tile_ctx, (const unsigned char *)svg, strlen(svg), errbuf, sizeof(errbuf), &opts);
    free(svg);
    if (!ok) {
        p->rendering = 0;
        return 0;
    }
    p->rgba = tile_ctx.hi_rgba;
    p->tile_w = tile_ctx.width * tile_ctx.ss;
    p->tile_h = tile_ctx.height * tile_ctx.ss;
    p->rendered = 1;
    p->rendering = 0;
    if (needs_bbox) {
        p->cached_bbox_x = bbox->x;
        p->cached_bbox_y = bbox->y;
        p->cached_bbox_w = bbox->w;
        p->cached_bbox_h = bbox->h;
        p->has_cached_bbox = 1;
    } else {
        p->has_cached_bbox = 0;
    }
    return p->rgba != NULL;
}

static int svg_pattern_sample(svg_pattern *p, const svg_render_ctx *ctx, const svg_bbox *bbox,
                              float x, float y, uint8_t *pr, uint8_t *pg, uint8_t *pb, uint8_t *pa) {
    if (!p || !ctx || !bbox || !pr || !pg || !pb || !pa) {
        return 0;
    }
    if (!svg_pattern_prepare(p, ctx, bbox)) {
        return 0;
    }
    float px = x;
    float py = y;
    if (p->has_transform) {
        svg_matrix_transform_point(&p->inv_transform, &px, &py);
    }
    float bbox_w = bbox->w > 0.0f ? bbox->w : 1.0f;
    float bbox_h = bbox->h > 0.0f ? bbox->h : 1.0f;
    float base_w = (p->units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX) ? bbox_w : ctx->vb_w;
    float base_h = (p->units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX) ? bbox_h : ctx->vb_h;
    float origin_x = (p->units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX) ? bbox->x : 0.0f;
    float origin_y = (p->units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX) ? bbox->y : 0.0f;
    float tile_x = svg_resolve_grad_coord(&p->x, base_w, origin_x, p->units);
    float tile_y = svg_resolve_grad_coord(&p->y, base_h, origin_y, p->units);
    float tile_w = svg_resolve_grad_coord(&p->width, base_w, 0.0f, p->units);
    float tile_h = svg_resolve_grad_coord(&p->height, base_h, 0.0f, p->units);
    if (tile_w <= 0.0f || tile_h <= 0.0f || p->tile_w == 0 || p->tile_h == 0 || !p->rgba) {
        return 0;
    }
    float tx = (px - tile_x) / tile_w;
    float ty = (py - tile_y) / tile_h;
    tx = tx - floorf(tx);
    ty = ty - floorf(ty);
    if (tx < 0.0f) tx += 1.0f;
    if (ty < 0.0f) ty += 1.0f;
    uint32_t ix = (uint32_t)floorf(tx * (float)p->tile_w);
    uint32_t iy = (uint32_t)floorf(ty * (float)p->tile_h);
    if (ix >= p->tile_w) ix = p->tile_w - 1;
    if (iy >= p->tile_h) iy = p->tile_h - 1;
    size_t idx = ((size_t)iy * (size_t)p->tile_w + (size_t)ix) * 4u;
    *pr = p->rgba[idx + 0];
    *pg = p->rgba[idx + 1];
    *pb = p->rgba[idx + 2];
    *pa = p->rgba[idx + 3];
    return *pa > 0;
}

static uint32_t svg_gradient_sample(const svg_gradient *g, const svg_render_ctx *ctx, const svg_bbox *bbox,
                                    float x, float y) {
    if (!g || g->stop_count == 0 || !ctx || !bbox) {
        return 0x00000000u;
    }
    float px = x;
    float py = y;
    if (g->has_transform) {
        svg_matrix_transform_point(&g->inv_transform, &px, &py);
    }
    float bbox_w = bbox->w > 0.0f ? bbox->w : 1.0f;
    float bbox_h = bbox->h > 0.0f ? bbox->h : 1.0f;
    float base_w = (g->units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX) ? bbox_w : ctx->vb_w;
    float base_h = (g->units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX) ? bbox_h : ctx->vb_h;
    float origin_x = (g->units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX) ? bbox->x : 0.0f;
    float origin_y = (g->units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX) ? bbox->y : 0.0f;
    if (g->type == SVG_PAINT_LINEAR_GRADIENT) {
        float x1 = svg_resolve_grad_coord(&g->x1, base_w, origin_x, g->units);
        float y1 = svg_resolve_grad_coord(&g->y1, base_h, origin_y, g->units);
        float x2 = svg_resolve_grad_coord(&g->x2, base_w, origin_x, g->units);
        float y2 = svg_resolve_grad_coord(&g->y2, base_h, origin_y, g->units);
        float dx = x2 - x1;
        float dy = y2 - y1;
        float denom = dx * dx + dy * dy;
        float t = 0.0f;
        if (denom > 1e-12f) {
            t = ((px - x1) * dx + (py - y1) * dy) / denom;
        }
        t = svg_apply_spread(t, g->spread);
        return svg_gradient_stop_color(g, t);
    }
    float cx = svg_resolve_grad_coord(&g->cx, base_w, origin_x, g->units);
    float cy = svg_resolve_grad_coord(&g->cy, base_h, origin_y, g->units);
    float r = svg_resolve_grad_coord(&g->r, fmaxf(base_w, base_h), 0.0f, g->units);
    if (r <= 1e-6f) {
        return svg_gradient_stop_color(g, 1.0f);
    }
    float fx = svg_resolve_grad_coord(&g->fx, base_w, origin_x, g->units);
    float fy = svg_resolve_grad_coord(&g->fy, base_h, origin_y, g->units);
    float vx = px - fx;
    float vy = py - fy;
    float t = 0.0f;
    float vlen2 = vx * vx + vy * vy;
    if (vlen2 <= 1e-12f) {
        t = 0.0f;
    } else {
        float dx = fx - cx;
        float dy = fy - cy;
        float a = vlen2;
        float b = 2.0f * (dx * vx + dy * vy);
        float c = dx * dx + dy * dy - r * r;
        float disc = b * b - 4.0f * a * c;
        if (disc >= 0.0f) {
            float sqrt_disc = sqrtf(disc);
            float s1 = (-b + sqrt_disc) / (2.0f * a);
            float s2 = (-b - sqrt_disc) / (2.0f * a);
            float s = fmaxf(s1, s2);
            if (s > 0.0f) {
                t = 1.0f / s;
            } else {
                t = sqrtf(vlen2) / r;
            }
        } else {
            t = sqrtf(vlen2) / r;
        }
    }
    t = svg_apply_spread(t, g->spread);
    return svg_gradient_stop_color(g, t);
}

static int svg_prepare_const_paint(const svg_style *style, int is_stroke, uint8_t *pr, uint8_t *pg,
                                   uint8_t *pb, uint8_t *pa) {
    if (!style || !pr || !pg || !pb || !pa) {
        return 0;
    }
    svg_paint_type type = is_stroke ? style->stroke_type : style->fill_type;
    if (type != SVG_PAINT_COLOR) {
        return 0;
    }
    uint32_t color = is_stroke ? style->stroke_color : style->fill_color;
    uint8_t a = (uint8_t)(color & 0xFFu);
    float opacity = style->opacity * (is_stroke ? style->stroke_opacity : style->fill_opacity);
    if (opacity < 0.0f) opacity = 0.0f;
    if (opacity > 1.0f) opacity = 1.0f;
    float alpha = (a / 255.0f) * opacity;
    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;
    uint8_t oa = (uint8_t)lroundf(alpha * 255.0f);
    uint8_t r = (uint8_t)(color >> 24);
    uint8_t g = (uint8_t)(color >> 16);
    uint8_t b = (uint8_t)(color >> 8);
    *pa = oa;
    *pr = (uint8_t)((r * oa) / 255u);
    *pg = (uint8_t)((g * oa) / 255u);
    *pb = (uint8_t)((b * oa) / 255u);
    return oa > 0;
}

static int svg_sample_paint(const svg_style *style, const svg_render_ctx *ctx, int is_stroke, const svg_bbox *bbox,
                            float x, float y, uint8_t *pr, uint8_t *pg, uint8_t *pb, uint8_t *pa) {
    if (!style || !ctx || !bbox || !pr || !pg || !pb || !pa) {
        return 0;
    }
    svg_paint_type type = is_stroke ? style->stroke_type : style->fill_type;
    if (type == SVG_PAINT_NONE) {
        return 0;
    }
    if (type == SVG_PAINT_PATTERN) {
        svg_pattern *pat = is_stroke ? style->stroke_pattern : style->fill_pattern;
        uint8_t sr = 0, sg = 0, sb = 0, sa = 0;
        if (!svg_pattern_sample(pat, ctx, bbox, x, y, &sr, &sg, &sb, &sa)) {
            return 0;
        }
        float opacity = style->opacity * (is_stroke ? style->stroke_opacity : style->fill_opacity);
        if (opacity < 0.0f) opacity = 0.0f;
        if (opacity > 1.0f) opacity = 1.0f;
        uint8_t oa = (uint8_t)lroundf(sa * opacity);
        if (oa == 0) {
            return 0;
        }
        *pa = oa;
        *pr = (uint8_t)lroundf(sr * opacity);
        *pg = (uint8_t)lroundf(sg * opacity);
        *pb = (uint8_t)lroundf(sb * opacity);
        return 1;
    }
    uint32_t color = 0;
    if (type == SVG_PAINT_COLOR) {
        color = is_stroke ? style->stroke_color : style->fill_color;
    } else {
        const svg_gradient *grad = is_stroke ? style->stroke_gradient : style->fill_gradient;
        color = svg_gradient_sample(grad, ctx, bbox, x, y);
    }
    uint8_t a = (uint8_t)(color & 0xFFu);
    if (a == 0) {
        return 0;
    }
    float opacity = style->opacity * (is_stroke ? style->stroke_opacity : style->fill_opacity);
    if (opacity < 0.0f) opacity = 0.0f;
    if (opacity > 1.0f) opacity = 1.0f;
    float alpha = (a / 255.0f) * opacity;
    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;
    uint8_t oa = (uint8_t)lroundf(alpha * 255.0f);
    if (oa == 0) {
        return 0;
    }
    uint8_t r = (uint8_t)(color >> 24);
    uint8_t g = (uint8_t)(color >> 16);
    uint8_t b = (uint8_t)(color >> 8);
    *pa = oa;
    *pr = (uint8_t)((r * oa) / 255u);
    *pg = (uint8_t)((g * oa) / 255u);
    *pb = (uint8_t)((b * oa) / 255u);
    return 1;
}

static int svg_xml_space_preserve(const svg_tag *tag, int inherited) {
    const char *space = svg_get_attr_exact(tag, "xml:space");
    if (!space) {
        return inherited;
    }
    if (svg_strcasecmp(space, "preserve") == 0) {
        return 1;
    }
    if (svg_strcasecmp(space, "default") == 0) {
        return 0;
    }
    return inherited;
}

static int svg_decode_entity(const char *s, size_t *consumed) {
    if (!s || s[0] != '&') {
        return -1;
    }
    const char *p = s + 1;
    if (strncmp(p, "lt;", 3) == 0) {
        *consumed = 4;
        return '<';
    }
    if (strncmp(p, "gt;", 3) == 0) {
        *consumed = 4;
        return '>';
    }
    if (strncmp(p, "amp;", 4) == 0) {
        *consumed = 5;
        return '&';
    }
    if (strncmp(p, "quot;", 5) == 0) {
        *consumed = 6;
        return '"';
    }
    if (strncmp(p, "apos;", 5) == 0) {
        *consumed = 6;
        return '\'';
    }
    if (*p == '#') {
        p++;
        int base = 10;
        if (*p == 'x' || *p == 'X') {
            base = 16;
            p++;
        }
        char *end = NULL;
        long v = strtol(p, &end, base);
        if (end && *end == ';') {
            *consumed = (size_t)(end - s + 1);
            if (v < 0 || v > 255) {
                return '?';
            }
            return (int)v;
        }
    }
    return -1;
}

static char *svg_text_normalize(const char *text, int preserve) {
    if (!text) {
        return NULL;
    }
    size_t len = strlen(text);
    char *out = (char *)malloc(len + 1);
    if (!out) {
        return NULL;
    }
    size_t w = 0;
    int last_space = 0;
    for (size_t i = 0; i < len;) {
        char ch = text[i];
        if (ch == '&') {
            size_t consumed = 0;
            int decoded = svg_decode_entity(text + i, &consumed);
            if (decoded >= 0) {
                ch = (char)decoded;
                i += consumed;
            } else {
                i++;
            }
        } else {
            i++;
        }
        if (ch == '\r') {
            ch = '\n';
        } else if (ch == '\t') {
            ch = ' ';
        }
        if (!preserve) {
            if (isspace((unsigned char)ch)) {
                if (last_space || w == 0) {
                    continue;
                }
                ch = ' ';
                last_space = 1;
            } else {
                last_space = 0;
            }
        }
        out[w++] = ch;
    }
    if (!preserve) {
        while (w > 0 && out[w - 1] == ' ') {
            w--;
        }
    }
    out[w] = '\0';
    return out;
}

static const uint8_t *svg_get_glyph(unsigned char c) {
    if (c < 128) {
        return svg_font8x8_basic[c];
    }
    return svg_font8x8_basic['?'];
}

static int svg_utf8_decode_one(const char *s, uint32_t *cp, int *len) {
    if (!s || !*s || !cp || !len) {
        return 0;
    }
    unsigned char c0 = (unsigned char)s[0];
    if (c0 < 0x80) {
        *cp = c0;
        *len = 1;
        return 1;
    }
    if ((c0 & 0xE0u) == 0xC0u) {
        unsigned char c1 = (unsigned char)s[1];
        if ((c1 & 0xC0u) != 0x80u) return 0;
        *cp = ((uint32_t)(c0 & 0x1Fu) << 6) | (uint32_t)(c1 & 0x3Fu);
        *len = 2;
        return 1;
    }
    if ((c0 & 0xF0u) == 0xE0u) {
        unsigned char c1 = (unsigned char)s[1];
        unsigned char c2 = (unsigned char)s[2];
        if ((c1 & 0xC0u) != 0x80u || (c2 & 0xC0u) != 0x80u) return 0;
        *cp = ((uint32_t)(c0 & 0x0Fu) << 12) | ((uint32_t)(c1 & 0x3Fu) << 6) | (uint32_t)(c2 & 0x3Fu);
        *len = 3;
        return 1;
    }
    if ((c0 & 0xF8u) == 0xF0u) {
        unsigned char c1 = (unsigned char)s[1];
        unsigned char c2 = (unsigned char)s[2];
        unsigned char c3 = (unsigned char)s[3];
        if ((c1 & 0xC0u) != 0x80u || (c2 & 0xC0u) != 0x80u || (c3 & 0xC0u) != 0x80u) return 0;
        *cp = ((uint32_t)(c0 & 0x07u) << 18) | ((uint32_t)(c1 & 0x3Fu) << 12) |
              ((uint32_t)(c2 & 0x3Fu) << 6) | (uint32_t)(c3 & 0x3Fu);
        *len = 4;
        return 1;
    }
    return 0;
}

static const uint8_t svg_glyph_bullet[8] = {
    0x00, 0x00, 0x18, 0x3C, 0x3C, 0x18, 0x00, 0x00
};

static const uint8_t *svg_get_glyph_codepoint(uint32_t cp) {
    if (cp < 128u) {
        return svg_get_glyph((unsigned char)cp);
    }
    if (cp == 0x2022u || cp == 0x00B7u) {
        return svg_glyph_bullet;
    }
    return svg_font8x8_basic['?'];
}

static float svg_text_advance(const svg_style *style, uint32_t cp) {
    float base_w = style->font_size * 0.6f;
    float adv = base_w + style->letter_spacing;
    if (cp == (uint32_t)' ') {
        adv += style->word_spacing;
    }
    return adv;
}

static float svg_text_baseline_offset(const svg_style *style) {
    if (!style) {
        return 0.0f;
    }
    if (style->dominant_baseline == 1) {
        return style->font_size * 0.5f;
    }
    if (style->dominant_baseline == 2) {
        return style->font_size;
    }
    return 0.0f;
}

static float svg_measure_line(const svg_style *style, const char *text, size_t len) {
    if (!style || !text) {
        return 0.0f;
    }
    float width = 0.0f;
    size_t i = 0;
    while (i < len) {
        uint32_t cp = 0;
        int n = 0;
        if (!svg_utf8_decode_one(text + i, &cp, &n) || n <= 0 || i + (size_t)n > len) {
            i++;
            continue;
        }
        i += (size_t)n;
        if (cp == (uint32_t)'\n') {
            break;
        }
        if (cp < 32u) {
            continue;
        }
        width += svg_text_advance(style, cp);
    }
    return width;
}

static void svg_draw_glyph(svg_render_ctx *ctx, const svg_style *style, const svg_matrix *m, const svg_bbox *bbox,
                           const uint8_t *glyph, float x, float y, float scale_x, float scale_y) {
    if (!ctx || !style || !m || !glyph || !bbox) {
        return;
    }
    if (style->fill_type == SVG_PAINT_NONE) {
        return;
    }
    svg_matrix gm;
    svg_matrix_identity(&gm);
    svg_matrix_scale(&gm, scale_x, scale_y);
    if (style->italic) {
        svg_matrix_skewx(&gm, -12.0f);
    }
    svg_matrix_translate(&gm, x, y - style->font_size);
    svg_matrix total;
    svg_matrix_multiply(m, &gm, &total);
    float width = 8.0f * scale_x;
    float height = 8.0f * scale_y;
    float dev_minx, dev_miny, dev_maxx, dev_maxy;
    svg_transform_bounds(&total, 0.0f, 0.0f, width, height, &dev_minx, &dev_miny, &dev_maxx, &dev_maxy);
    if (dev_maxx < 0.0f || dev_maxy < 0.0f || dev_minx > (float)ctx->width || dev_miny > (float)ctx->height) {
        return;
    }
    svg_matrix inv;
    if (!svg_matrix_invert(&total, &inv)) {
        return;
    }
    uint32_t ss = ctx->ss;
    int sx0 = (int)floorf(dev_minx * (float)ss);
    int sy0 = (int)floorf(dev_miny * (float)ss);
    int sx1 = (int)ceilf(dev_maxx * (float)ss);
    int sy1 = (int)ceilf(dev_maxy * (float)ss);
    if (sx0 < 0) sx0 = 0;
    if (sy0 < 0) sy0 = 0;
    int max_sx = (int)(ctx->width * ss) - 1;
    int max_sy = (int)(ctx->height * ss) - 1;
    if (sx1 > max_sx) sx1 = max_sx;
    if (sy1 > max_sy) sy1 = max_sy;
    int fill_is_color = style->fill_type == SVG_PAINT_COLOR;
    uint8_t fill_pr = 0, fill_pg = 0, fill_pb = 0, fill_pa = 0;
    if (fill_is_color) {
        svg_prepare_const_paint(style, 0, &fill_pr, &fill_pg, &fill_pb, &fill_pa);
    }
    for (int sy = sy0; sy <= sy1; sy++) {
        float dy = ((float)sy + 0.5f) / (float)ss;
        for (int sx = sx0; sx <= sx1; sx++) {
            float dx = ((float)sx + 0.5f) / (float)ss;
            float gx = dx;
            float gy = dy;
            svg_matrix_transform_point(&inv, &gx, &gy);
            if (gx < 0.0f || gy < 0.0f || gx >= 8.0f || gy >= 8.0f) {
                continue;
            }
            int ix = (int)floorf(gx);
            int iy = (int)floorf(gy);
            if (ix < 0 || ix >= 8 || iy < 0 || iy >= 8) {
                continue;
            }
            if ((glyph[iy] & (1u << ix)) == 0) {
                continue;
            }
            uint8_t *dst = ctx->hi_rgba + ((size_t)sy * (size_t)(ctx->width * ss) + (size_t)sx) * 4u;
            uint8_t mask_alpha = svg_sample_mask_alpha((svg_style *)style, ctx, bbox, sx, sy);
            if (mask_alpha == 0) {
                continue;
            }
            if (fill_is_color) {
                if (fill_pa > 0) {
                    uint8_t pr = fill_pr;
                    uint8_t pg = fill_pg;
                    uint8_t pb = fill_pb;
                    uint8_t pa = fill_pa;
                    if (mask_alpha < 255) {
                        pr = (uint8_t)((pr * mask_alpha) / 255u);
                        pg = (uint8_t)((pg * mask_alpha) / 255u);
                        pb = (uint8_t)((pb * mask_alpha) / 255u);
                        pa = (uint8_t)((pa * mask_alpha) / 255u);
                    }
                    if (pa > 0) {
                        svg_blend_premul(dst, pr, pg, pb, pa);
                    }
                }
            } else {
                float ux = gx;
                float uy = gy;
                svg_matrix_transform_point(&gm, &ux, &uy);
                uint8_t pr, pg, pb, pa;
                if (svg_sample_paint(style, ctx, 0, bbox, ux, uy, &pr, &pg, &pb, &pa)) {
                    if (mask_alpha < 255) {
                        pr = (uint8_t)((pr * mask_alpha) / 255u);
                        pg = (uint8_t)((pg * mask_alpha) / 255u);
                        pb = (uint8_t)((pb * mask_alpha) / 255u);
                        pa = (uint8_t)((pa * mask_alpha) / 255u);
                    }
                    if (pa > 0) {
                        svg_blend_premul(dst, pr, pg, pb, pa);
                    }
                }
            }
        }
    }
}

static void svg_render_text_run(svg_render_ctx *ctx, const svg_style *style, const svg_matrix *m,
                                const char *text, float *pen_x, float *pen_y, svg_bbox *bbox_out) {
    if (!ctx || !style || !m || !text || !pen_x || !pen_y) {
        return;
    }
    float line_height = style->font_size * 1.2f;
    float scale_x = style->font_size * 0.6f / 8.0f;
    float scale_y = style->font_size / 8.0f;
    float baseline_off = svg_text_baseline_offset(style);
    const char *p = text;
    while (*p) {
        const char *line_start = p;
        while (*p && *p != '\n') {
            p++;
        }
        size_t line_len = (size_t)(p - line_start);
        float line_width = svg_measure_line(style, line_start, line_len);
        float line_x = *pen_x;
        if (style->text_anchor == 1) {
            line_x -= line_width * 0.5f;
        } else if (style->text_anchor == 2) {
            line_x -= line_width;
        }
        float line_y = *pen_y + baseline_off;
        if (bbox_out) {
            svg_bbox_add_rect(bbox_out, line_x, line_y - style->font_size, line_width, style->font_size);
        }
        svg_bbox bbox;
        bbox.x = line_x;
        bbox.y = line_y - style->font_size;
        bbox.w = line_width;
        bbox.h = style->font_size;
        float x = line_x;
        size_t i = 0;
        while (i < line_len) {
            uint32_t cp = 0;
            int n = 0;
            if (!svg_utf8_decode_one(line_start + i, &cp, &n) || n <= 0 || i + (size_t)n > line_len) {
                i++;
                continue;
            }
            i += (size_t)n;
            if (cp < 32u) {
                continue;
            }
            const uint8_t *glyph = svg_get_glyph_codepoint(cp);
            svg_draw_glyph(ctx, style, m, &bbox, glyph, x, line_y, scale_x, scale_y);
            if (style->bold) {
                svg_draw_glyph(ctx, style, m, &bbox, glyph, x + scale_x * 0.8f, line_y, scale_x, scale_y);
            }
            x += svg_text_advance(style, cp);
        }
        *pen_x = x;
        if (*p == '\n') {
            *pen_y += line_height;
            *pen_x = line_x;
            p++;
        }
    }
}

static void svg_render_text_run_path(svg_render_ctx *ctx, const svg_style *style, const svg_matrix *m,
                                     const char *text, svg_text_path_state *tp, svg_bbox *bbox_out) {
    if (!ctx || !style || !m || !text || !tp || !tp->active) {
        return;
    }
    if (bbox_out) {
        svg_bbox temp = tp->bbox;
        svg_bbox_expand(&temp, style->font_size * 0.5f);
        svg_bbox_add_rect(bbox_out, temp.x, temp.y, temp.w, temp.h);
    }
    float scale_x = style->font_size * 0.6f / 8.0f;
    float scale_y = style->font_size / 8.0f;
    float baseline_off = svg_text_baseline_offset(style);
    const char *p = text;
    while (*p) {
        uint32_t cp = 0;
        int n = 0;
        if (!svg_utf8_decode_one(p, &cp, &n) || n <= 0) {
            p++;
            continue;
        }
        p += n;
        if (cp == (uint32_t)'\n') {
            break;
        }
        if (cp < 32u) {
            continue;
        }
        float adv = svg_text_advance(style, cp);
        float sample_pos = tp->pos + adv * 0.5f;
        float gx = 0.0f;
        float gy = 0.0f;
        float ang = 0.0f;
        if (!svg_text_path_point(tp, sample_pos, &gx, &gy, &ang)) {
            return;
        }
        svg_matrix local;
        svg_matrix_identity(&local);
        svg_matrix_rotate(&local, ang * (180.0f / (float)M_PI));
        svg_matrix_translate(&local, gx, gy);
        svg_matrix glyph_m;
        svg_matrix_multiply(m, &local, &glyph_m);
        const uint8_t *glyph = svg_get_glyph_codepoint(cp);
        svg_draw_glyph(ctx, style, &glyph_m, &tp->bbox, glyph, 0.0f, style->font_size + baseline_off, scale_x, scale_y);
        if (style->bold) {
            svg_draw_glyph(ctx, style, &glyph_m, &tp->bbox, glyph, scale_x * 0.8f,
                           style->font_size + baseline_off, scale_x, scale_y);
        }
        tp->pos += adv;
    }
}

static void svg_draw_samples_rect(svg_render_ctx *ctx, const svg_style *style, const svg_matrix *m,
                                  float x, float y, float w, float h, float rx, float ry) {
    if (w <= 0.0f || h <= 0.0f) {
        return;
    }
    float half = style->stroke_width * 0.5f;
    float minx = x - half;
    float miny = y - half;
    float maxx = x + w + half;
    float maxy = y + h + half;
    float dev_minx, dev_miny, dev_maxx, dev_maxy;
    svg_transform_bounds(m, minx, miny, maxx, maxy, &dev_minx, &dev_miny, &dev_maxx, &dev_maxy);
    if (dev_maxx < 0.0f || dev_maxy < 0.0f || dev_minx > (float)ctx->width || dev_miny > (float)ctx->height) {
        return;
    }
    svg_matrix inv;
    if (!svg_matrix_invert(m, &inv)) {
        return;
    }
    if (rx < 0.0f) rx = 0.0f;
    if (ry < 0.0f) ry = 0.0f;
    if (rx > w * 0.5f) rx = w * 0.5f;
    if (ry > h * 0.5f) ry = h * 0.5f;
    float inner_rx = rx > half ? rx - half : 0.0f;
    float inner_ry = ry > half ? ry - half : 0.0f;

    uint32_t ss = ctx->ss;
    int sx0 = (int)floorf(dev_minx * (float)ss);
    int sy0 = (int)floorf(dev_miny * (float)ss);
    int sx1 = (int)ceilf(dev_maxx * (float)ss);
    int sy1 = (int)ceilf(dev_maxy * (float)ss);
    if (sx0 < 0) sx0 = 0;
    if (sy0 < 0) sy0 = 0;
    int max_sx = (int)(ctx->width * ss) - 1;
    int max_sy = (int)(ctx->height * ss) - 1;
    if (sx1 > max_sx) sx1 = max_sx;
    if (sy1 > max_sy) sy1 = max_sy;

    svg_bbox bbox = {x, y, w, h};
    int has_fill = style->fill_type != SVG_PAINT_NONE;
    int has_stroke = style->stroke_type != SVG_PAINT_NONE && style->stroke_width > 0.0f;
    int need_segment_stroke = has_stroke &&
                              (style->stroke_dashcount > 0 ||
                               style->stroke_linecap != SVG_LINECAP_BUTT ||
                               style->stroke_linejoin != SVG_LINEJOIN_MITER);
    int has_stroke_analytic = has_stroke && !need_segment_stroke;
    int fill_is_color = style->fill_type == SVG_PAINT_COLOR;
    int stroke_is_color = style->stroke_type == SVG_PAINT_COLOR;
    uint8_t fill_pr = 0, fill_pg = 0, fill_pb = 0, fill_pa = 0;
    uint8_t stroke_pr = 0, stroke_pg = 0, stroke_pb = 0, stroke_pa = 0;
    if (fill_is_color) {
        svg_prepare_const_paint(style, 0, &fill_pr, &fill_pg, &fill_pb, &fill_pa);
    }
    if (stroke_is_color) {
        svg_prepare_const_paint(style, 1, &stroke_pr, &stroke_pg, &stroke_pb, &stroke_pa);
    }

    float inner_x = x + half;
    float inner_y = y + half;
    float inner_w = w - style->stroke_width;
    float inner_h = h - style->stroke_width;

    for (int sy = sy0; sy <= sy1; sy++) {
        float dy = ((float)sy + 0.5f) / (float)ss;
        for (int sx = sx0; sx <= sx1; sx++) {
            float dx = ((float)sx + 0.5f) / (float)ss;
            float lx = dx;
            float ly = dy;
            svg_matrix_transform_point(&inv, &lx, &ly);
            int inside = 0;
            if (rx <= 0.0f || ry <= 0.0f) {
                if (lx >= x && lx <= x + w && ly >= y && ly <= y + h) {
                    inside = 1;
                }
            } else {
                float inner_left = x + rx;
                float inner_right = x + w - rx;
                float inner_top = y + ry;
                float inner_bottom = y + h - ry;
                if ((lx >= inner_left && lx <= inner_right && ly >= y && ly <= y + h) ||
                    (lx >= x && lx <= x + w && ly >= inner_top && ly <= inner_bottom)) {
                    inside = 1;
                } else {
                    float cx = lx < inner_left ? inner_left : inner_right;
                    float cy = ly < inner_top ? inner_top : inner_bottom;
                    float dx0 = lx - cx;
                    float dy0 = ly - cy;
                    float nx = dx0 / rx;
                    float ny = dy0 / ry;
                    if (nx * nx + ny * ny <= 1.0f) {
                        inside = 1;
                    }
                }
            }
            int in_stroke = 0;
            if (has_stroke_analytic) {
                if (inner_w <= 0.0f || inner_h <= 0.0f) {
                    in_stroke = inside;
                } else {
                    int inside_inner = 0;
                    if (inner_rx <= 0.0f || inner_ry <= 0.0f) {
                        if (lx >= inner_x && lx <= inner_x + inner_w && ly >= inner_y && ly <= inner_y + inner_h) {
                            inside_inner = 1;
                        }
                    } else {
                        float inner_left = inner_x + inner_rx;
                        float inner_right = inner_x + inner_w - inner_rx;
                        float inner_top = inner_y + inner_ry;
                        float inner_bottom = inner_y + inner_h - inner_ry;
                        if ((lx >= inner_left && lx <= inner_right && ly >= inner_y && ly <= inner_y + inner_h) ||
                            (lx >= inner_x && lx <= inner_x + inner_w && ly >= inner_top && ly <= inner_bottom)) {
                            inside_inner = 1;
                        } else {
                            float cx = lx < inner_left ? inner_left : inner_right;
                            float cy = ly < inner_top ? inner_top : inner_bottom;
                            float dx0 = lx - cx;
                            float dy0 = ly - cy;
                            float nx = dx0 / inner_rx;
                            float ny = dy0 / inner_ry;
                            if (nx * nx + ny * ny <= 1.0f) {
                                inside_inner = 1;
                            }
                        }
                    }
                    if (inside && !inside_inner) {
                        in_stroke = 1;
                    }
                }
            }
            uint8_t *dst = ctx->hi_rgba + ((size_t)sy * (size_t)(ctx->width * ss) + (size_t)sx) * 4u;
            uint8_t mask_alpha = 255;
            if ((has_fill && inside) || (has_stroke_analytic && in_stroke)) {
                mask_alpha = svg_sample_mask_alpha((svg_style *)style, ctx, &bbox, sx, sy);
            }
            if (has_fill && inside && mask_alpha > 0) {
                if (fill_is_color) {
                    if (fill_pa > 0) {
                        uint8_t pr = fill_pr;
                        uint8_t pg = fill_pg;
                        uint8_t pb = fill_pb;
                        uint8_t pa = fill_pa;
                        if (mask_alpha < 255) {
                            pr = (uint8_t)((pr * mask_alpha) / 255u);
                            pg = (uint8_t)((pg * mask_alpha) / 255u);
                            pb = (uint8_t)((pb * mask_alpha) / 255u);
                            pa = (uint8_t)((pa * mask_alpha) / 255u);
                        }
                        if (pa > 0) {
                            svg_blend_premul(dst, pr, pg, pb, pa);
                        }
                    }
                } else {
                    uint8_t pr, pg, pb, pa;
                    if (svg_sample_paint(style, ctx, 0, &bbox, lx, ly, &pr, &pg, &pb, &pa)) {
                        if (mask_alpha < 255) {
                            pr = (uint8_t)((pr * mask_alpha) / 255u);
                            pg = (uint8_t)((pg * mask_alpha) / 255u);
                            pb = (uint8_t)((pb * mask_alpha) / 255u);
                            pa = (uint8_t)((pa * mask_alpha) / 255u);
                        }
                        if (pa > 0) {
                            svg_blend_premul(dst, pr, pg, pb, pa);
                        }
                    }
                }
            }
            if (has_stroke_analytic && in_stroke && mask_alpha > 0) {
                if (stroke_is_color) {
                    if (stroke_pa > 0) {
                        uint8_t pr = stroke_pr;
                        uint8_t pg = stroke_pg;
                        uint8_t pb = stroke_pb;
                        uint8_t pa = stroke_pa;
                        if (mask_alpha < 255) {
                            pr = (uint8_t)((pr * mask_alpha) / 255u);
                            pg = (uint8_t)((pg * mask_alpha) / 255u);
                            pb = (uint8_t)((pb * mask_alpha) / 255u);
                            pa = (uint8_t)((pa * mask_alpha) / 255u);
                        }
                        if (pa > 0) {
                            svg_blend_premul(dst, pr, pg, pb, pa);
                        }
                    }
                } else {
                    uint8_t pr, pg, pb, pa;
                    if (svg_sample_paint(style, ctx, 1, &bbox, lx, ly, &pr, &pg, &pb, &pa)) {
                        if (mask_alpha < 255) {
                            pr = (uint8_t)((pr * mask_alpha) / 255u);
                            pg = (uint8_t)((pg * mask_alpha) / 255u);
                            pb = (uint8_t)((pb * mask_alpha) / 255u);
                            pa = (uint8_t)((pa * mask_alpha) / 255u);
                        }
                        if (pa > 0) {
                            svg_blend_premul(dst, pr, pg, pb, pa);
                        }
                    }
                }
            }
        }
    }
    if (need_segment_stroke) {
        svg_segments segs;
        svg_segments_init(&segs);
        svg_segments_add_rect(&segs, x, y, w, h, rx, ry);
        svg_style stroke_style = *style;
        stroke_style.fill_type = SVG_PAINT_NONE;
        stroke_style.fill_pattern = NULL;
        svg_draw_samples_segments(ctx, &stroke_style, m, &segs);
        svg_segments_free(&segs);
    }
}

static void svg_draw_samples_ellipse(svg_render_ctx *ctx, const svg_style *style, const svg_matrix *m,
                                     float cx, float cy, float rx, float ry) {
    if (rx <= 0.0f || ry <= 0.0f) {
        return;
    }
    float half = style->stroke_width * 0.5f;
    float minx = cx - rx - half;
    float miny = cy - ry - half;
    float maxx = cx + rx + half;
    float maxy = cy + ry + half;
    float dev_minx, dev_miny, dev_maxx, dev_maxy;
    svg_transform_bounds(m, minx, miny, maxx, maxy, &dev_minx, &dev_miny, &dev_maxx, &dev_maxy);
    if (dev_maxx < 0.0f || dev_maxy < 0.0f || dev_minx > (float)ctx->width || dev_miny > (float)ctx->height) {
        return;
    }
    svg_matrix inv;
    if (!svg_matrix_invert(m, &inv)) {
        return;
    }

    uint32_t ss = ctx->ss;
    int sx0 = (int)floorf(dev_minx * (float)ss);
    int sy0 = (int)floorf(dev_miny * (float)ss);
    int sx1 = (int)ceilf(dev_maxx * (float)ss);
    int sy1 = (int)ceilf(dev_maxy * (float)ss);
    if (sx0 < 0) sx0 = 0;
    if (sy0 < 0) sy0 = 0;
    int max_sx = (int)(ctx->width * ss) - 1;
    int max_sy = (int)(ctx->height * ss) - 1;
    if (sx1 > max_sx) sx1 = max_sx;
    if (sy1 > max_sy) sy1 = max_sy;

    svg_bbox bbox = {cx - rx, cy - ry, rx * 2.0f, ry * 2.0f};
    int has_fill = style->fill_type != SVG_PAINT_NONE;
    int has_stroke = style->stroke_type != SVG_PAINT_NONE && style->stroke_width > 0.0f;
    int need_segment_stroke = has_stroke &&
                              (style->stroke_dashcount > 0 ||
                               style->stroke_linecap != SVG_LINECAP_BUTT ||
                               style->stroke_linejoin != SVG_LINEJOIN_MITER);
    int has_stroke_analytic = has_stroke && !need_segment_stroke;
    int fill_is_color = style->fill_type == SVG_PAINT_COLOR;
    int stroke_is_color = style->stroke_type == SVG_PAINT_COLOR;
    uint8_t fill_pr = 0, fill_pg = 0, fill_pb = 0, fill_pa = 0;
    uint8_t stroke_pr = 0, stroke_pg = 0, stroke_pb = 0, stroke_pa = 0;
    if (fill_is_color) {
        svg_prepare_const_paint(style, 0, &fill_pr, &fill_pg, &fill_pb, &fill_pa);
    }
    if (stroke_is_color) {
        svg_prepare_const_paint(style, 1, &stroke_pr, &stroke_pg, &stroke_pb, &stroke_pa);
    }

    float rx_scale = 1.0f / rx;
    float ry_scale = 1.0f / ry;
    float max_r = fmaxf(rx, ry);
    float half_norm = max_r > 0.0f ? half / max_r : 0.0f;

    for (int sy = sy0; sy <= sy1; sy++) {
        float dy = ((float)sy + 0.5f) / (float)ss;
        for (int sx = sx0; sx <= sx1; sx++) {
            float dx = ((float)sx + 0.5f) / (float)ss;
            float lx = dx;
            float ly = dy;
            svg_matrix_transform_point(&inv, &lx, &ly);
            float ux = (lx - cx) * rx_scale;
            float uy = (ly - cy) * ry_scale;
            float t = ux * ux + uy * uy;
            int inside = t <= 1.0f;
            int in_stroke = 0;
            if (has_stroke_analytic) {
                float dist = fabsf(sqrtf(t) - 1.0f);
                if (dist <= half_norm) {
                    in_stroke = 1;
                }
            }
            uint8_t *dst = ctx->hi_rgba + ((size_t)sy * (size_t)(ctx->width * ss) + (size_t)sx) * 4u;
            uint8_t mask_alpha = 255;
            if ((has_fill && inside) || (has_stroke_analytic && in_stroke)) {
                mask_alpha = svg_sample_mask_alpha((svg_style *)style, ctx, &bbox, sx, sy);
            }
            if (has_fill && inside && mask_alpha > 0) {
                if (fill_is_color) {
                    if (fill_pa > 0) {
                        uint8_t pr = fill_pr;
                        uint8_t pg = fill_pg;
                        uint8_t pb = fill_pb;
                        uint8_t pa = fill_pa;
                        if (mask_alpha < 255) {
                            pr = (uint8_t)((pr * mask_alpha) / 255u);
                            pg = (uint8_t)((pg * mask_alpha) / 255u);
                            pb = (uint8_t)((pb * mask_alpha) / 255u);
                            pa = (uint8_t)((pa * mask_alpha) / 255u);
                        }
                        if (pa > 0) {
                            svg_blend_premul(dst, pr, pg, pb, pa);
                        }
                    }
                } else {
                    uint8_t pr, pg, pb, pa;
                    if (svg_sample_paint(style, ctx, 0, &bbox, lx, ly, &pr, &pg, &pb, &pa)) {
                        if (mask_alpha < 255) {
                            pr = (uint8_t)((pr * mask_alpha) / 255u);
                            pg = (uint8_t)((pg * mask_alpha) / 255u);
                            pb = (uint8_t)((pb * mask_alpha) / 255u);
                            pa = (uint8_t)((pa * mask_alpha) / 255u);
                        }
                        if (pa > 0) {
                            svg_blend_premul(dst, pr, pg, pb, pa);
                        }
                    }
                }
            }
            if (has_stroke_analytic && in_stroke && mask_alpha > 0) {
                if (stroke_is_color) {
                    if (stroke_pa > 0) {
                        uint8_t pr = stroke_pr;
                        uint8_t pg = stroke_pg;
                        uint8_t pb = stroke_pb;
                        uint8_t pa = stroke_pa;
                        if (mask_alpha < 255) {
                            pr = (uint8_t)((pr * mask_alpha) / 255u);
                            pg = (uint8_t)((pg * mask_alpha) / 255u);
                            pb = (uint8_t)((pb * mask_alpha) / 255u);
                            pa = (uint8_t)((pa * mask_alpha) / 255u);
                        }
                        if (pa > 0) {
                            svg_blend_premul(dst, pr, pg, pb, pa);
                        }
                    }
                } else {
                    uint8_t pr, pg, pb, pa;
                    if (svg_sample_paint(style, ctx, 1, &bbox, lx, ly, &pr, &pg, &pb, &pa)) {
                        if (mask_alpha < 255) {
                            pr = (uint8_t)((pr * mask_alpha) / 255u);
                            pg = (uint8_t)((pg * mask_alpha) / 255u);
                            pb = (uint8_t)((pb * mask_alpha) / 255u);
                            pa = (uint8_t)((pa * mask_alpha) / 255u);
                        }
                        if (pa > 0) {
                            svg_blend_premul(dst, pr, pg, pb, pa);
                        }
                    }
                }
            }
        }
    }
    if (need_segment_stroke) {
        svg_segments segs;
        svg_segments_init(&segs);
        svg_segments_add_ellipse(&segs, cx, cy, rx, ry);
        svg_style stroke_style = *style;
        stroke_style.fill_type = SVG_PAINT_NONE;
        stroke_style.fill_pattern = NULL;
        svg_draw_samples_segments(ctx, &stroke_style, m, &segs);
        svg_segments_free(&segs);
    }
}

static void svg_draw_samples_line(svg_render_ctx *ctx, const svg_style *style, const svg_matrix *m,
                                  float x0, float y0, float x1, float y1) {
    if (style->stroke_type == SVG_PAINT_NONE || style->stroke_width <= 0.0f) {
        return;
    }
    svg_segments segs;
    svg_segments_init(&segs);
    svg_segments_add(&segs, x0, y0, x1, y1, 1, 0);
    svg_draw_samples_segments(ctx, style, m, &segs);
    svg_segments_free(&segs);
}

static void svg_draw_samples_image(svg_render_ctx *ctx, const svg_style *style, const svg_matrix *m,
                                   const cupidimage_image *img,
                                   float x, float y, float w, float h,
                                   int preserve_none, float align_x, float align_y) {
    if (!ctx || !style || !m || !img || !img->rgba || img->width == 0 || img->height == 0) {
        return;
    }
    if (style->opacity <= 0.0f || w <= 0.0f || h <= 0.0f) {
        return;
    }

    float draw_w = w;
    float draw_h = h;
    float draw_x = x;
    float draw_y = y;
    if (!preserve_none) {
        float sx = draw_w / (float)img->width;
        float sy = draw_h / (float)img->height;
        float s = fminf(sx, sy);
        if (s <= 0.0f) {
            return;
        }
        float used_w = (float)img->width * s;
        float used_h = (float)img->height * s;
        draw_x += (draw_w - used_w) * align_x;
        draw_y += (draw_h - used_h) * align_y;
        draw_w = used_w;
        draw_h = used_h;
    }

    svg_matrix image_m;
    svg_matrix_identity(&image_m);
    svg_matrix_scale(&image_m, draw_w / (float)img->width, draw_h / (float)img->height);
    svg_matrix_translate(&image_m, draw_x, draw_y);
    svg_matrix full_m;
    svg_matrix_multiply(m, &image_m, &full_m);

    float dev_minx, dev_miny, dev_maxx, dev_maxy;
    svg_transform_bounds(&full_m, 0.0f, 0.0f, (float)img->width, (float)img->height,
                         &dev_minx, &dev_miny, &dev_maxx, &dev_maxy);
    if (dev_maxx < 0.0f || dev_maxy < 0.0f || dev_minx > (float)ctx->width || dev_miny > (float)ctx->height) {
        return;
    }

    svg_matrix inv;
    if (!svg_matrix_invert(&full_m, &inv)) {
        return;
    }

    uint32_t ss = ctx->ss;
    int sx0 = (int)floorf(dev_minx * (float)ss);
    int sy0 = (int)floorf(dev_miny * (float)ss);
    int sx1 = (int)ceilf(dev_maxx * (float)ss);
    int sy1 = (int)ceilf(dev_maxy * (float)ss);
    if (sx0 < 0) sx0 = 0;
    if (sy0 < 0) sy0 = 0;
    int max_sx = (int)(ctx->width * ss) - 1;
    int max_sy = (int)(ctx->height * ss) - 1;
    if (sx1 > max_sx) sx1 = max_sx;
    if (sy1 > max_sy) sy1 = max_sy;

    svg_bbox bbox = {draw_x, draw_y, draw_w, draw_h};
    float opacity = svg_clamp01(style->opacity);
    for (int sy = sy0; sy <= sy1; sy++) {
        float dy = ((float)sy + 0.5f) / (float)ss;
        for (int sx = sx0; sx <= sx1; sx++) {
            float dx = ((float)sx + 0.5f) / (float)ss;
            float ix = dx;
            float iy = dy;
            svg_matrix_transform_point(&inv, &ix, &iy);
            if (ix < 0.0f || iy < 0.0f || ix >= (float)img->width || iy >= (float)img->height) {
                continue;
            }
            int px = (int)floorf(ix);
            int py = (int)floorf(iy);
            if (px < 0 || py < 0 || px >= (int)img->width || py >= (int)img->height) {
                continue;
            }
            size_t sidx = ((size_t)py * (size_t)img->width + (size_t)px) * 4u;
            uint8_t sa = (uint8_t)lroundf((img->rgba[sidx + 3] / 255.0f) * opacity * 255.0f);
            if (sa == 0) {
                continue;
            }
            uint8_t pr = (uint8_t)((img->rgba[sidx + 0] * sa) / 255u);
            uint8_t pg = (uint8_t)((img->rgba[sidx + 1] * sa) / 255u);
            uint8_t pb = (uint8_t)((img->rgba[sidx + 2] * sa) / 255u);
            uint8_t mask_alpha = svg_sample_mask_alpha((svg_style *)style, ctx, &bbox, sx, sy);
            if (mask_alpha < 255) {
                pr = (uint8_t)((pr * mask_alpha) / 255u);
                pg = (uint8_t)((pg * mask_alpha) / 255u);
                pb = (uint8_t)((pb * mask_alpha) / 255u);
                sa = (uint8_t)((sa * mask_alpha) / 255u);
            }
            if (sa == 0) {
                continue;
            }
            uint8_t *dst = ctx->hi_rgba + ((size_t)sy * (size_t)(ctx->width * ss) + (size_t)sx) * 4u;
            svg_blend_premul(dst, pr, pg, pb, sa);
        }
    }
}

static void svg_draw_samples_segments(svg_render_ctx *ctx, const svg_style *style, const svg_matrix *m,
                                      const svg_segments *segs) {
    if (segs->stroke_count == 0 && segs->fill_count == 0) {
        return;
    }
    float half = style->stroke_width * 0.5f;
    float minx = segs->minx - half;
    float miny = segs->miny - half;
    float maxx = segs->maxx + half;
    float maxy = segs->maxy + half;
    float dev_minx, dev_miny, dev_maxx, dev_maxy;
    svg_transform_bounds(m, minx, miny, maxx, maxy, &dev_minx, &dev_miny, &dev_maxx, &dev_maxy);
    if (dev_maxx < 0.0f || dev_maxy < 0.0f || dev_minx > (float)ctx->width || dev_miny > (float)ctx->height) {
        return;
    }
    svg_matrix inv;
    if (!svg_matrix_invert(m, &inv)) {
        return;
    }

    uint32_t ss = ctx->ss;
    int sx0 = (int)floorf(dev_minx * (float)ss);
    int sy0 = (int)floorf(dev_miny * (float)ss);
    int sx1 = (int)ceilf(dev_maxx * (float)ss);
    int sy1 = (int)ceilf(dev_maxy * (float)ss);
    if (sx0 < 0) sx0 = 0;
    if (sy0 < 0) sy0 = 0;
    int max_sx = (int)(ctx->width * ss) - 1;
    int max_sy = (int)(ctx->height * ss) - 1;
    if (sx1 > max_sx) sx1 = max_sx;
    if (sy1 > max_sy) sy1 = max_sy;

    svg_bbox bbox;
    bbox.x = segs->minx;
    bbox.y = segs->miny;
    bbox.w = segs->maxx - segs->minx;
    bbox.h = segs->maxy - segs->miny;
    int has_fill = style->fill_type != SVG_PAINT_NONE && segs->fill_count > 0;
    int has_stroke = style->stroke_type != SVG_PAINT_NONE && style->stroke_width > 0.0f && segs->stroke_count > 0;
    svg_segments dash_segs;
    int using_dash = 0;
    const svg_segments *stroke_segs = segs;
    if (has_stroke && style->stroke_dashcount > 0) {
        if (svg_segments_build_dashed(segs, style, &dash_segs)) {
            stroke_segs = &dash_segs;
            using_dash = 1;
        } else {
            has_stroke = 0;
        }
    }
    if (has_stroke && stroke_segs->stroke_count == 0) {
        has_stroke = 0;
    }
    int fill_is_color = style->fill_type == SVG_PAINT_COLOR;
    int stroke_is_color = style->stroke_type == SVG_PAINT_COLOR;
    uint8_t fill_pr = 0, fill_pg = 0, fill_pb = 0, fill_pa = 0;
    uint8_t stroke_pr = 0, stroke_pg = 0, stroke_pb = 0, stroke_pa = 0;
    if (fill_is_color) {
        svg_prepare_const_paint(style, 0, &fill_pr, &fill_pg, &fill_pb, &fill_pa);
    }
    if (stroke_is_color) {
        svg_prepare_const_paint(style, 1, &stroke_pr, &stroke_pg, &stroke_pb, &stroke_pa);
    }

    uint8_t *start_open = NULL;
    uint8_t *end_open = NULL;
    if (has_stroke) {
        start_open = (uint8_t *)malloc(stroke_segs->stroke_count);
        end_open = (uint8_t *)malloc(stroke_segs->stroke_count);
        if (start_open && end_open) {
            svg_segments_endpoint_flags(stroke_segs, start_open, end_open);
        } else {
            free(start_open);
            free(end_open);
            start_open = NULL;
            end_open = NULL;
        }
    }
    svg_join_tri *join_tris = NULL;
    int join_count = 0;
    if (has_stroke && style->stroke_linejoin != SVG_LINEJOIN_ROUND) {
        svg_build_join_tris(stroke_segs, style, start_open, end_open, &join_tris, &join_count);
    }

    for (int sy = sy0; sy <= sy1; sy++) {
        float dy = ((float)sy + 0.5f) / (float)ss;
        for (int sx = sx0; sx <= sx1; sx++) {
            float dx = ((float)sx + 0.5f) / (float)ss;
            float lx = dx;
            float ly = dy;
            svg_matrix_transform_point(&inv, &lx, &ly);
            int inside = 0;
            int in_stroke = 0;
            if (has_fill) {
                inside = svg_point_in_segments(segs, style->fill_rule_evenodd, lx, ly);
            }
            if (has_stroke) {
                if (start_open && end_open) {
                    in_stroke = svg_point_on_segments_cap(stroke_segs, style, lx, ly, half,
                                                          start_open, end_open, join_tris, join_count);
                } else {
                    in_stroke = svg_point_on_segments(stroke_segs, lx, ly, half);
                }
            }
            uint8_t *dst = ctx->hi_rgba + ((size_t)sy * (size_t)(ctx->width * ss) + (size_t)sx) * 4u;
            uint8_t mask_alpha = 255;
            if ((has_fill && inside) || (has_stroke && in_stroke)) {
                mask_alpha = svg_sample_mask_alpha((svg_style *)style, ctx, &bbox, sx, sy);
            }
            if (has_fill && inside && mask_alpha > 0) {
                if (fill_is_color) {
                    if (fill_pa > 0) {
                        uint8_t pr = fill_pr;
                        uint8_t pg = fill_pg;
                        uint8_t pb = fill_pb;
                        uint8_t pa = fill_pa;
                        if (mask_alpha < 255) {
                            pr = (uint8_t)((pr * mask_alpha) / 255u);
                            pg = (uint8_t)((pg * mask_alpha) / 255u);
                            pb = (uint8_t)((pb * mask_alpha) / 255u);
                            pa = (uint8_t)((pa * mask_alpha) / 255u);
                        }
                        if (pa > 0) {
                            svg_blend_premul(dst, pr, pg, pb, pa);
                        }
                    }
                } else {
                    uint8_t pr, pg, pb, pa;
                    if (svg_sample_paint(style, ctx, 0, &bbox, lx, ly, &pr, &pg, &pb, &pa)) {
                        if (mask_alpha < 255) {
                            pr = (uint8_t)((pr * mask_alpha) / 255u);
                            pg = (uint8_t)((pg * mask_alpha) / 255u);
                            pb = (uint8_t)((pb * mask_alpha) / 255u);
                            pa = (uint8_t)((pa * mask_alpha) / 255u);
                        }
                        if (pa > 0) {
                            svg_blend_premul(dst, pr, pg, pb, pa);
                        }
                    }
                }
            }
            if (has_stroke && in_stroke && mask_alpha > 0) {
                if (stroke_is_color) {
                    if (stroke_pa > 0) {
                        uint8_t pr = stroke_pr;
                        uint8_t pg = stroke_pg;
                        uint8_t pb = stroke_pb;
                        uint8_t pa = stroke_pa;
                        if (mask_alpha < 255) {
                            pr = (uint8_t)((pr * mask_alpha) / 255u);
                            pg = (uint8_t)((pg * mask_alpha) / 255u);
                            pb = (uint8_t)((pb * mask_alpha) / 255u);
                            pa = (uint8_t)((pa * mask_alpha) / 255u);
                        }
                        if (pa > 0) {
                            svg_blend_premul(dst, pr, pg, pb, pa);
                        }
                    }
                } else {
                    uint8_t pr, pg, pb, pa;
                    if (svg_sample_paint(style, ctx, 1, &bbox, lx, ly, &pr, &pg, &pb, &pa)) {
                        if (mask_alpha < 255) {
                            pr = (uint8_t)((pr * mask_alpha) / 255u);
                            pg = (uint8_t)((pg * mask_alpha) / 255u);
                            pb = (uint8_t)((pb * mask_alpha) / 255u);
                            pa = (uint8_t)((pa * mask_alpha) / 255u);
                        }
                        if (pa > 0) {
                            svg_blend_premul(dst, pr, pg, pb, pa);
                        }
                    }
                }
            }
        }
    }
    free(start_open);
    free(end_open);
    free(join_tris);
    if (using_dash) {
        svg_segments_free(&dash_segs);
    }
}

static int svg_parse_points(const char *s, float **out_points, int *out_count) {
    if (!s || !out_points || !out_count) {
        return 0;
    }
    *out_points = NULL;
    *out_count = 0;
    const char *p = s;
    int cap = 0;
    while (1) {
        float x = 0.0f, y = 0.0f;
        if (!svg_parse_number(&p, &x)) {
            break;
        }
        if (!svg_parse_number(&p, &y)) {
            break;
        }
        if (*out_count >= cap) {
            int new_cap = cap ? cap * 2 : 32;
            float *n = (float *)realloc(*out_points, (size_t)new_cap * 2u * sizeof(float));
            if (!n) {
                free(*out_points);
                *out_points = NULL;
                *out_count = 0;
                return 0;
            }
            *out_points = n;
            cap = new_cap;
        }
        (*out_points)[(*out_count) * 2] = x;
        (*out_points)[(*out_count) * 2 + 1] = y;
        (*out_count)++;
    }
    return *out_count > 0;
}

static int svg_flatten_cubic(svg_segments *segs, float x0, float y0, float x1, float y1,
                             float x2, float y2, float x3, float y3, float tol, int depth) {
    float ux = 3.0f * x1 - 2.0f * x0 - x3;
    float uy = 3.0f * y1 - 2.0f * y0 - y3;
    float vx = 3.0f * x2 - 2.0f * x3 - x0;
    float vy = 3.0f * y2 - 2.0f * y3 - y0;
    float d = fmaxf(ux * ux + uy * uy, vx * vx + vy * vy);
    if (d <= tol * tol || depth > 10) {
        return svg_segments_add(segs, x0, y0, x3, y3, 1, 1);
    }
    float x01 = (x0 + x1) * 0.5f;
    float y01 = (y0 + y1) * 0.5f;
    float x12 = (x1 + x2) * 0.5f;
    float y12 = (y1 + y2) * 0.5f;
    float x23 = (x2 + x3) * 0.5f;
    float y23 = (y2 + y3) * 0.5f;
    float x012 = (x01 + x12) * 0.5f;
    float y012 = (y01 + y12) * 0.5f;
    float x123 = (x12 + x23) * 0.5f;
    float y123 = (y12 + y23) * 0.5f;
    float x0123 = (x012 + x123) * 0.5f;
    float y0123 = (y012 + y123) * 0.5f;
    if (!svg_flatten_cubic(segs, x0, y0, x01, y01, x012, y012, x0123, y0123, tol, depth + 1)) {
        return 0;
    }
    return svg_flatten_cubic(segs, x0123, y0123, x123, y123, x23, y23, x3, y3, tol, depth + 1);
}

static int svg_flatten_quad(svg_segments *segs, float x0, float y0, float x1, float y1,
                            float x2, float y2, float tol, int depth) {
    float ux = x0 - 2.0f * x1 + x2;
    float uy = y0 - 2.0f * y1 + y2;
    float d = ux * ux + uy * uy;
    if (d <= tol * tol || depth > 10) {
        return svg_segments_add(segs, x0, y0, x2, y2, 1, 1);
    }
    float x01 = (x0 + x1) * 0.5f;
    float y01 = (y0 + y1) * 0.5f;
    float x12 = (x1 + x2) * 0.5f;
    float y12 = (y1 + y2) * 0.5f;
    float x012 = (x01 + x12) * 0.5f;
    float y012 = (y01 + y12) * 0.5f;
    if (!svg_flatten_quad(segs, x0, y0, x01, y01, x012, y012, tol, depth + 1)) {
        return 0;
    }
    return svg_flatten_quad(segs, x012, y012, x12, y12, x2, y2, tol, depth + 1);
}

static int svg_parse_path(const char *d, svg_segments *segs) {
    if (!d) {
        return 0;
    }
    const char *p = d;
    char cmd = 0;
    float cx = 0.0f, cy = 0.0f;
    float sx = 0.0f, sy = 0.0f;
    float last_cx = 0.0f, last_cy = 0.0f;
    float last_qx = 0.0f, last_qy = 0.0f;
    int have_cubic = 0;
    int have_quad = 0;
    int subpath_open = 0;
    float tol = 0.25f;

    while (*p) {
        svg_skip_separators(&p);
        if (!*p) {
            break;
        }
        if (isalpha((unsigned char)*p)) {
            cmd = *p++;
        }
        if (!cmd) {
            break;
        }
        int relative = islower((unsigned char)cmd);
        char ucmd = (char)toupper((unsigned char)cmd);
        switch (ucmd) {
        case 'M': {
            float x = 0.0f, y = 0.0f;
            if (!svg_parse_number(&p, &x) || !svg_parse_number(&p, &y)) {
                return 1;
            }
            if (relative) {
                x += cx;
                y += cy;
            }
            if (subpath_open) {
                svg_segments_add(segs, cx, cy, sx, sy, 0, 1);
            }
            cx = x;
            cy = y;
            sx = x;
            sy = y;
            subpath_open = 1;
            have_cubic = 0;
            have_quad = 0;
            while (svg_parse_number(&p, &x) && svg_parse_number(&p, &y)) {
                if (relative) {
                    x += cx;
                    y += cy;
                }
                svg_segments_add(segs, cx, cy, x, y, 1, 1);
                cx = x;
                cy = y;
            }
            break;
        }
        case 'L': {
            float x = 0.0f, y = 0.0f;
            while (svg_parse_number(&p, &x) && svg_parse_number(&p, &y)) {
                if (relative) {
                    x += cx;
                    y += cy;
                }
                svg_segments_add(segs, cx, cy, x, y, 1, 1);
                cx = x;
                cy = y;
                have_cubic = 0;
                have_quad = 0;
            }
            break;
        }
        case 'H': {
            float x = 0.0f;
            while (svg_parse_number(&p, &x)) {
                if (relative) {
                    x += cx;
                }
                svg_segments_add(segs, cx, cy, x, cy, 1, 1);
                cx = x;
                have_cubic = 0;
                have_quad = 0;
            }
            break;
        }
        case 'V': {
            float y = 0.0f;
            while (svg_parse_number(&p, &y)) {
                if (relative) {
                    y += cy;
                }
                svg_segments_add(segs, cx, cy, cx, y, 1, 1);
                cy = y;
                have_cubic = 0;
                have_quad = 0;
            }
            break;
        }
        case 'C': {
            float x1, y1, x2, y2, x3, y3;
            while (svg_parse_number(&p, &x1) && svg_parse_number(&p, &y1) &&
                   svg_parse_number(&p, &x2) && svg_parse_number(&p, &y2) &&
                   svg_parse_number(&p, &x3) && svg_parse_number(&p, &y3)) {
                if (relative) {
                    x1 += cx; y1 += cy;
                    x2 += cx; y2 += cy;
                    x3 += cx; y3 += cy;
                }
                if (!svg_flatten_cubic(segs, cx, cy, x1, y1, x2, y2, x3, y3, tol, 0)) {
                    return 0;
                }
                cx = x3;
                cy = y3;
                last_cx = x2;
                last_cy = y2;
                have_cubic = 1;
                have_quad = 0;
            }
            break;
        }
        case 'S': {
            float x2, y2, x3, y3;
            while (svg_parse_number(&p, &x2) && svg_parse_number(&p, &y2) &&
                   svg_parse_number(&p, &x3) && svg_parse_number(&p, &y3)) {
                float x1 = cx;
                float y1 = cy;
                if (have_cubic) {
                    x1 = 2.0f * cx - last_cx;
                    y1 = 2.0f * cy - last_cy;
                }
                if (relative) {
                    x2 += cx; y2 += cy;
                    x3 += cx; y3 += cy;
                }
                if (!svg_flatten_cubic(segs, cx, cy, x1, y1, x2, y2, x3, y3, tol, 0)) {
                    return 0;
                }
                cx = x3;
                cy = y3;
                last_cx = x2;
                last_cy = y2;
                have_cubic = 1;
                have_quad = 0;
            }
            break;
        }
        case 'Q': {
            float x1, y1, x2, y2;
            while (svg_parse_number(&p, &x1) && svg_parse_number(&p, &y1) &&
                   svg_parse_number(&p, &x2) && svg_parse_number(&p, &y2)) {
                if (relative) {
                    x1 += cx; y1 += cy;
                    x2 += cx; y2 += cy;
                }
                if (!svg_flatten_quad(segs, cx, cy, x1, y1, x2, y2, tol, 0)) {
                    return 0;
                }
                cx = x2;
                cy = y2;
                last_qx = x1;
                last_qy = y1;
                have_quad = 1;
                have_cubic = 0;
            }
            break;
        }
        case 'T': {
            float x2, y2;
            while (svg_parse_number(&p, &x2) && svg_parse_number(&p, &y2)) {
                float x1 = cx;
                float y1 = cy;
                if (have_quad) {
                    x1 = 2.0f * cx - last_qx;
                    y1 = 2.0f * cy - last_qy;
                }
                if (relative) {
                    x2 += cx; y2 += cy;
                }
                if (!svg_flatten_quad(segs, cx, cy, x1, y1, x2, y2, tol, 0)) {
                    return 0;
                }
                cx = x2;
                cy = y2;
                last_qx = x1;
                last_qy = y1;
                have_quad = 1;
                have_cubic = 0;
            }
            break;
        }
        case 'A': {
            float rx, ry, xrot, large_arc, sweep, x, y;
            while (svg_parse_number(&p, &rx) && svg_parse_number(&p, &ry) &&
                   svg_parse_number(&p, &xrot) && svg_parse_number(&p, &large_arc) &&
                   svg_parse_number(&p, &sweep) && svg_parse_number(&p, &x) && svg_parse_number(&p, &y)) {
                if (relative) {
                    x += cx; y += cy;
                }
                svg_segments_add_arc_rot(segs, cx, cy, x, y, rx, ry, xrot,
                                         (int)lroundf(large_arc) != 0,
                                         (int)lroundf(sweep) != 0);
                cx = x;
                cy = y;
                have_cubic = 0;
                have_quad = 0;
            }
            break;
        }
        case 'Z': {
            if (subpath_open) {
                svg_segments_add(segs, cx, cy, sx, sy, 1, 1);
                cx = sx;
                cy = sy;
                subpath_open = 0;
            }
            have_cubic = 0;
            have_quad = 0;
            break;
        }
        default:
            return 1;
        }
    }
    if (subpath_open) {
        svg_segments_add(segs, cx, cy, sx, sy, 0, 1);
    }
    return 1;
}


static int svg_parse_length_attr(const svg_tag *tag, const char *name, float base, float dpi, float *out) {
    const char *val = svg_get_attr(tag, name);
    if (!val) {
        return 0;
    }
    int ok = 0;
    float v = svg_parse_length(val, base, dpi, &ok);
    if (ok) {
        *out = v;
        return 1;
    }
    return 0;
}
static void svg_render_text_element(svg_render_ctx *ctx, const svg_style *style, const svg_matrix *m,
                                    const svg_tag *text_tag, char **p,
                                    const svg_defs *defs, const svg_css *css,
                                    const svg_elem_info *stack_info, int stack_depth,
                                    float base_len, float dpi, svg_bbox *out_bbox) {
    if (!ctx || !style || !m || !text_tag || !p || !defs || !css || !stack_info) {
        return;
    }
    if (out_bbox) {
        svg_bbox_init(out_bbox);
    }
    float pen_x = 0.0f;
    float pen_y = 0.0f;
    float dx = 0.0f;
    float dy = 0.0f;
    if (!svg_parse_length_attr(text_tag, "x", ctx->vb_w, dpi, &pen_x)) {
        pen_x = 0.0f;
    }
    if (!svg_parse_length_attr(text_tag, "y", ctx->vb_h, dpi, &pen_y)) {
        pen_y = 0.0f;
    }
    svg_parse_length_attr(text_tag, "dx", ctx->vb_w, dpi, &dx);
    svg_parse_length_attr(text_tag, "dy", ctx->vb_h, dpi, &dy);
    pen_x += dx;
    pen_y += dy;
    int preserve = svg_xml_space_preserve(text_tag, 0);

    svg_text_state state_stack[SVG_MAX_DEPTH];
    int tdepth = 0;
    svg_style cur_style = *style;
    svg_matrix cur_m = *m;
    svg_text_path_state path_stack[SVG_MAX_DEPTH];
    int path_depth = 0;
    svg_text_path_state *cur_path = NULL;

    while (**p) {
        char *lt = strchr(*p, '<');
        if (!lt) {
            lt = *p + strlen(*p);
        }
        if (lt > *p) {
            char *seg = svg_strndup(*p, (size_t)(lt - *p));
            if (seg) {
                char *norm = svg_text_normalize(seg, preserve);
                if (norm && *norm) {
                    if (cur_path && cur_path->active) {
                        svg_render_text_run_path(ctx, &cur_style, &cur_m, norm, cur_path, out_bbox);
                    } else {
                        svg_render_text_run(ctx, &cur_style, &cur_m, norm, &pen_x, &pen_y, out_bbox);
                    }
                }
                free(norm);
                free(seg);
            }
        }
        if (!*lt) {
            *p = lt;
            break;
        }
        if (svg_strncasecmp(lt, "</text", 6) == 0) {
            char *tmp = lt;
            svg_tag end_tag;
            svg_next_tag(&tmp, &end_tag);
            *p = tmp;
            break;
        }
        if (strncmp(lt, "<![CDATA[", 9) == 0) {
            char *cend = strstr(lt + 9, "]]>");
            if (!cend) {
                *p = lt + strlen(lt);
                break;
            }
            if (cend > lt + 9) {
                char *seg = svg_strndup(lt + 9, (size_t)(cend - (lt + 9)));
                if (seg) {
                    char *norm = svg_text_normalize(seg, preserve);
                    if (norm && *norm) {
                        if (cur_path && cur_path->active) {
                            svg_render_text_run_path(ctx, &cur_style, &cur_m, norm, cur_path, out_bbox);
                        } else {
                            svg_render_text_run(ctx, &cur_style, &cur_m, norm, &pen_x, &pen_y, out_bbox);
                        }
                    }
                    free(norm);
                    free(seg);
                }
            }
            *p = cend + 3;
            continue;
        }
        char *tmp = lt;
        svg_tag inner;
        if (!svg_next_tag(&tmp, &inner)) {
            *p = tmp;
            break;
        }
        const char *lname = svg_local_name(inner.name);
        if (!inner.is_end && strcmp(lname, "tspan") == 0) {
            if (tdepth < SVG_MAX_DEPTH) {
                state_stack[tdepth].style = cur_style;
                state_stack[tdepth].transform = cur_m;
                state_stack[tdepth].preserve = preserve;
                tdepth++;
            }
            svg_style next_style = cur_style;
            svg_apply_presentation_attrs(&next_style, &inner, base_len, dpi, defs);
            svg_elem_info tspan_info;
            svg_elem_info_from_tag(&tspan_info, lname, &inner);
            svg_elem_info tmp_stack[SVG_MAX_DEPTH];
            int tmp_depth = stack_depth < SVG_MAX_DEPTH ? stack_depth : SVG_MAX_DEPTH;
            for (int i = 0; i < tmp_depth; i++) {
                tmp_stack[i] = stack_info[i];
            }
            if (tmp_depth < SVG_MAX_DEPTH) {
                tmp_stack[tmp_depth++] = tspan_info;
            }
            svg_css_apply(&next_style, css, defs, tmp_stack, tmp_depth, NULL, 0, base_len, dpi);
            svg_apply_inline_style_attr(&next_style, &inner, base_len, dpi, defs);
            preserve = svg_xml_space_preserve(&inner, preserve);
            svg_matrix nm = cur_m;
            const char *tr = svg_get_attr(&inner, "transform");
            if (tr) {
                svg_matrix t;
                if (svg_parse_transform(tr, &t)) {
                    svg_matrix tmpm;
                    svg_matrix_multiply(&nm, &t, &tmpm);
                    nm = tmpm;
                }
            }
            cur_style = next_style;
            cur_m = nm;
            float v = 0.0f;
            if (svg_parse_length_attr(&inner, "x", ctx->vb_w, dpi, &v)) {
                pen_x = v;
            }
            if (svg_parse_length_attr(&inner, "y", ctx->vb_h, dpi, &v)) {
                pen_y = v;
            }
            if (svg_parse_length_attr(&inner, "dx", ctx->vb_w, dpi, &v)) {
                pen_x += v;
            }
            if (svg_parse_length_attr(&inner, "dy", ctx->vb_h, dpi, &v)) {
                pen_y += v;
            }
        } else if (!inner.is_end && strcmp(lname, "textpath") == 0) {
            if (tdepth < SVG_MAX_DEPTH) {
                state_stack[tdepth].style = cur_style;
                state_stack[tdepth].transform = cur_m;
                state_stack[tdepth].preserve = preserve;
                tdepth++;
            }
            svg_style next_style = cur_style;
            svg_apply_presentation_attrs(&next_style, &inner, base_len, dpi, defs);
            svg_elem_info tpath_info;
            svg_elem_info_from_tag(&tpath_info, lname, &inner);
            svg_elem_info tmp_stack[SVG_MAX_DEPTH];
            int tmp_depth = stack_depth < SVG_MAX_DEPTH ? stack_depth : SVG_MAX_DEPTH;
            for (int i = 0; i < tmp_depth; i++) {
                tmp_stack[i] = stack_info[i];
            }
            if (tmp_depth < SVG_MAX_DEPTH) {
                tmp_stack[tmp_depth++] = tpath_info;
            }
            svg_css_apply(&next_style, css, defs, tmp_stack, tmp_depth, NULL, 0, base_len, dpi);
            svg_apply_inline_style_attr(&next_style, &inner, base_len, dpi, defs);
            preserve = svg_xml_space_preserve(&inner, preserve);
            cur_style = next_style;

            if (path_depth < SVG_MAX_DEPTH) {
                svg_text_path_state *tp = &path_stack[path_depth++];
                memset(tp, 0, sizeof(*tp));
                tp->active = 0;
                svg_segments_init(&tp->segs);
                const char *href = svg_get_attr(&inner, "href");
                if (!href) {
                    href = svg_get_attr(&inner, "xlink:href");
                }
                char idbuf[96];
                idbuf[0] = '\0';
                svg_parse_href_id(href, idbuf, sizeof(idbuf));
                const svg_path_def *pd = (idbuf[0] && defs) ? svg_defs_find_path(defs, idbuf) : NULL;
                if (pd && pd->d) {
                    if (svg_parse_path(pd->d, &tp->segs) && tp->segs.stroke_count > 0) {
                        tp->seg_count = tp->segs.stroke_count;
                        tp->seg_lengths = (float *)malloc(tp->seg_count * sizeof(float));
                        if (tp->seg_lengths) {
                            float total = 0.0f;
                            for (size_t i = 0; i < tp->seg_count; i++) {
                                float dx = tp->segs.stroke[i].x1 - tp->segs.stroke[i].x0;
                                float dy = tp->segs.stroke[i].y1 - tp->segs.stroke[i].y0;
                                float len = sqrtf(dx * dx + dy * dy);
                                tp->seg_lengths[i] = len;
                                total += len;
                            }
                            tp->total_len = total;
                            tp->bbox.x = tp->segs.minx;
                            tp->bbox.y = tp->segs.miny;
                            tp->bbox.w = tp->segs.maxx - tp->segs.minx;
                            tp->bbox.h = tp->segs.maxy - tp->segs.miny;
                            if (tp->total_len > 0.0f) {
                                tp->active = 1;
                            }
                        }
                    }
                }
                float offset = 0.0f;
                int offset_is_percent = 0;
                const char *start_offset = svg_get_attr(&inner, "startOffset");
                if (start_offset && tp->active) {
                    const char *sp = start_offset;
                    while (isspace((unsigned char)*sp)) {
                        sp++;
                    }
                    char *endp = NULL;
                    double v = strtod(sp, &endp);
                    if (endp != sp) {
                        while (isspace((unsigned char)*endp)) {
                            endp++;
                        }
                        if (*endp == '%') {
                            offset_is_percent = 1;
                            offset = (float)(v / 100.0);
                        } else {
                            int ok = 0;
                            offset = svg_parse_length(start_offset, base_len, dpi, &ok);
                            if (!ok) {
                                offset = 0.0f;
                            }
                        }
                    }
                }
                if (tp->active) {
                    if (offset_is_percent) {
                        tp->pos = offset * tp->total_len;
                    } else {
                        tp->pos = offset;
                    }
                    if (cur_style.text_anchor != 0) {
                        char *after = NULL;
                        char *close = svg_find_close_tag(tmp, "textpath", &after);
                        if (close) {
                            float text_len = svg_textpath_measure_range(tmp, close, preserve, &cur_style);
                            if (cur_style.text_anchor == 1) {
                                tp->pos -= text_len * 0.5f;
                            } else if (cur_style.text_anchor == 2) {
                                tp->pos -= text_len;
                            }
                        }
                    }
                }
                if (inner.is_self_closing) {
                    svg_text_path_clear(tp);
                    path_depth--;
                    cur_path = path_depth > 0 ? &path_stack[path_depth - 1] : NULL;
                    if (tdepth > 0) {
                        tdepth--;
                        cur_style = state_stack[tdepth].style;
                        cur_m = state_stack[tdepth].transform;
                        preserve = state_stack[tdepth].preserve;
                    }
                } else {
                    cur_path = tp;
                }
            } else if (inner.is_self_closing) {
                if (tdepth > 0) {
                    tdepth--;
                    cur_style = state_stack[tdepth].style;
                    cur_m = state_stack[tdepth].transform;
                    preserve = state_stack[tdepth].preserve;
                }
            }
        } else if (inner.is_end && strcmp(lname, "textpath") == 0) {
            if (path_depth > 0) {
                svg_text_path_clear(&path_stack[path_depth - 1]);
                path_depth--;
            }
            cur_path = path_depth > 0 ? &path_stack[path_depth - 1] : NULL;
            if (tdepth > 0) {
                tdepth--;
                cur_style = state_stack[tdepth].style;
                cur_m = state_stack[tdepth].transform;
                preserve = state_stack[tdepth].preserve;
            }
        } else if (inner.is_end && strcmp(lname, "tspan") == 0) {
            if (tdepth > 0) {
                tdepth--;
                cur_style = state_stack[tdepth].style;
                cur_m = state_stack[tdepth].transform;
                preserve = state_stack[tdepth].preserve;
            }
        }
        *p = tmp;
    }
}

static int svg_render_transformed_snippet(svg_render_ctx *ctx, const char *content,
                                          const svg_matrix *gm, float dpi) {
    if (!ctx || !content || !*content || !gm) {
        return 0;
    }
    float vb_x = ctx->vb_x;
    float vb_y = ctx->vb_y;
    float vb_w = ctx->vb_w > 0.0f ? ctx->vb_w : (float)ctx->width;
    float vb_h = ctx->vb_h > 0.0f ? ctx->vb_h : (float)ctx->height;
    int header_len = snprintf(NULL, 0,
                              "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%.6f\" height=\"%.6f\" "
                              "viewBox=\"%.6f %.6f %.6f %.6f\"><g transform=\"matrix(%.9g %.9g %.9g %.9g %.9g %.9g)\">",
                              vb_w, vb_h, vb_x, vb_y, vb_w, vb_h,
                              gm->a, gm->b, gm->c, gm->d, gm->e, gm->f);
    if (header_len <= 0) {
        return 0;
    }
    const char *footer = "</g></svg>";
    size_t content_len = strlen(content);
    size_t total = (size_t)header_len + content_len + strlen(footer) + 1u;
    char *svg = (char *)malloc(total);
    if (!svg) {
        return 0;
    }
    char *dst = svg;
    dst += (size_t)snprintf(dst, (size_t)header_len + 1u,
                            "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%.6f\" height=\"%.6f\" "
                            "viewBox=\"%.6f %.6f %.6f %.6f\"><g transform=\"matrix(%.9g %.9g %.9g %.9g %.9g %.9g)\">",
                            vb_w, vb_h, vb_x, vb_y, vb_w, vb_h,
                            gm->a, gm->b, gm->c, gm->d, gm->e, gm->f);
    memcpy(dst, content, content_len);
    dst += content_len;
    memcpy(dst, footer, strlen(footer));
    dst += strlen(footer);
    *dst = '\0';

    cupidimage_svg_options opts;
    opts.width = ctx->width;
    opts.height = ctx->height;
    opts.scale = 1.0f;
    opts.dpi = dpi;
    opts.animation_time = ctx->anim_time;
    opts.supersampling = (uint8_t)ctx->ss;
    opts.background_alpha = 0;

    svg_render_ctx tmp_ctx;
    memset(&tmp_ctx, 0, sizeof(tmp_ctx));
    char errbuf[128];
    int ok = svg_render(&tmp_ctx, (const unsigned char *)svg, strlen(svg), errbuf, sizeof(errbuf), &opts);
    free(svg);
    if (!ok || !tmp_ctx.hi_rgba) {
        free(tmp_ctx.hi_rgba);
        return 0;
    }
    svg_composite_buffer(ctx, tmp_ctx.hi_rgba);
    free(tmp_ctx.hi_rgba);
    return 1;
}

static float svg_segment_angle_deg(const svg_segment *seg) {
    if (!seg) {
        return 0.0f;
    }
    return atan2f(seg->y1 - seg->y0, seg->x1 - seg->x0) * (180.0f / (float)M_PI);
}

static float svg_join_angle_deg(const svg_segment *prev, const svg_segment *next) {
    if (!prev || !next) {
        return 0.0f;
    }
    float dx0 = prev->x1 - prev->x0;
    float dy0 = prev->y1 - prev->y0;
    float dx1 = next->x1 - next->x0;
    float dy1 = next->y1 - next->y0;
    float l0 = sqrtf(dx0 * dx0 + dy0 * dy0);
    float l1 = sqrtf(dx1 * dx1 + dy1 * dy1);
    if (l1 <= 1e-6f) {
        return svg_segment_angle_deg(prev);
    }
    if (l0 <= 1e-6f) {
        return svg_segment_angle_deg(next);
    }
    dx0 /= l0;
    dy0 /= l0;
    dx1 /= l1;
    dy1 /= l1;
    float bx = dx0 + dx1;
    float by = dy0 + dy1;
    float bl = sqrtf(bx * bx + by * by);
    if (bl <= 1e-6f) {
        return svg_segment_angle_deg(next);
    }
    return atan2f(by, bx) * (180.0f / (float)M_PI);
}

static void svg_draw_marker_instance(svg_render_ctx *ctx, const svg_marker *mk, const svg_matrix *m,
                                     float px, float py, float angle_deg, float stroke_width, float dpi) {
    if (!ctx || !mk || !m || !mk->content || !*mk->content) {
        return;
    }
    float mw = mk->marker_w > 0.0f ? mk->marker_w : 3.0f;
    float mh = mk->marker_h > 0.0f ? mk->marker_h : 3.0f;
    float vbw = mk->vb_ok && mk->vb_w > 0.0f ? mk->vb_w : mw;
    float vbh = mk->vb_ok && mk->vb_h > 0.0f ? mk->vb_h : mh;
    if (vbw <= 0.0f || vbh <= 0.0f) {
        return;
    }
    float sx = mw / vbw;
    float sy = mh / vbh;
    float ax = 0.0f;
    float ay = 0.0f;
    if (!mk->preserve_none) {
        float uni = fminf(sx, sy);
        ax = (mw - vbw * uni) * mk->align_x;
        ay = (mh - vbh * uni) * mk->align_y;
        sx = uni;
        sy = uni;
    }
    if (mk->units_stroke_width) {
        float sw = stroke_width > 0.0f ? stroke_width : 1.0f;
        sx *= sw;
        sy *= sw;
    }
    float orient = mk->orient_auto ? angle_deg : mk->orient_angle;

    svg_matrix gm = *m;
    svg_matrix t;
    svg_matrix_identity(&t);
    t.e = px;
    t.f = py;
    svg_matrix_multiply(&gm, &t, &gm);
    svg_matrix r;
    svg_matrix_identity(&r);
    svg_matrix_rotate(&r, orient);
    svg_matrix_multiply(&gm, &r, &gm);
    svg_matrix s;
    svg_matrix_identity(&s);
    s.a = sx;
    s.d = sy;
    svg_matrix_multiply(&gm, &s, &gm);
    if (ax != 0.0f || ay != 0.0f) {
        svg_matrix ta;
        svg_matrix_identity(&ta);
        ta.e = ax;
        ta.f = ay;
        svg_matrix_multiply(&gm, &ta, &gm);
    }
    if (mk->vb_ok) {
        svg_matrix tvb;
        svg_matrix_identity(&tvb);
        tvb.e = -mk->vb_x;
        tvb.f = -mk->vb_y;
        svg_matrix_multiply(&gm, &tvb, &gm);
    }
    svg_matrix tref;
    svg_matrix_identity(&tref);
    tref.e = -mk->ref_x;
    tref.f = -mk->ref_y;
    svg_matrix_multiply(&gm, &tref, &gm);
    svg_render_transformed_snippet(ctx, mk->content, &gm, dpi);
}

static void svg_draw_markers_for_segments(svg_render_ctx *ctx, const svg_style *style, const svg_matrix *m,
                                          const svg_segments *segs, float dpi) {
    if (!ctx || !style || !m || !segs || segs->stroke_count == 0) {
        return;
    }
    if (style->stroke_type == SVG_PAINT_NONE || style->stroke_width <= 0.0f) {
        return;
    }
    if (!style->marker_start && !style->marker_mid && !style->marker_end) {
        return;
    }
    const svg_segment *first = &segs->stroke[0];
    const svg_segment *last_seg = &segs->stroke[segs->stroke_count - 1];
    int is_closed = fabsf(first->x0 - last_seg->x1) < 1e-4f &&
                    fabsf(first->y0 - last_seg->y1) < 1e-4f;
    if (style->marker_start) {
        float a = is_closed && segs->stroke_count > 1
            ? svg_join_angle_deg(last_seg, first)
            : svg_segment_angle_deg(first);
        if (style->marker_start->orient_auto && style->marker_start->orient_auto_start_reverse) {
            a += 180.0f;
        }
        svg_draw_marker_instance(ctx, style->marker_start, m,
                                 first->x0, first->y0,
                                 a, style->stroke_width, dpi);
    }
    if (style->marker_mid && segs->stroke_count > 1) {
        for (size_t i = 1; i < segs->stroke_count; i++) {
            float a = svg_join_angle_deg(&segs->stroke[i - 1], &segs->stroke[i]);
            svg_draw_marker_instance(ctx, style->marker_mid, m,
                                     segs->stroke[i].x0, segs->stroke[i].y0,
                                     a, style->stroke_width, dpi);
        }
    }
    if (style->marker_end) {
        float a = is_closed && segs->stroke_count > 1
            ? svg_join_angle_deg(last_seg, first)
            : svg_segment_angle_deg(last_seg);
        svg_draw_marker_instance(ctx, style->marker_end, m,
                                 last_seg->x1, last_seg->y1,
                                 a, style->stroke_width, dpi);
    }
}

static int svg_draw_shape(svg_render_ctx *ctx, const svg_style *style, const svg_matrix *m,
                          const svg_tag *tag, const char *local, float dpi,
                          char *child_start, float anim_time,
                          const svg_defs *defs, svg_bbox *out_bbox) {
    if (!ctx || !style || !m || !tag || !local) {
        return 0;
    }
    svg_tag anim_tag = *tag;
    if (!tag->is_self_closing && child_start && anim_time >= 0.0f) {
        svg_apply_animation_children(&anim_tag, local, child_start, anim_time);
        tag = &anim_tag;
    }
    if (out_bbox) {
        svg_bbox_init(out_bbox);
    }
    if (strcmp(local, "rect") == 0) {
        int ok = 0;
        float x = svg_parse_length(svg_get_attr(tag, "x"), ctx->vb_w, dpi, &ok);
        if (!ok) x = 0.0f;
        float y = svg_parse_length(svg_get_attr(tag, "y"), ctx->vb_h, dpi, &ok);
        if (!ok) y = 0.0f;
        float w = svg_parse_length(svg_get_attr(tag, "width"), ctx->vb_w, dpi, &ok);
        float h = svg_parse_length(svg_get_attr(tag, "height"), ctx->vb_h, dpi, &ok);
        float rx = svg_parse_length(svg_get_attr(tag, "rx"), ctx->vb_w, dpi, &ok);
        float ry = svg_parse_length(svg_get_attr(tag, "ry"), ctx->vb_h, dpi, &ok);
        if (!ok) rx = 0.0f;
        if (!svg_get_attr(tag, "ry") && rx > 0.0f) {
            ry = rx;
        }
        if (out_bbox && w > 0.0f && h > 0.0f) {
            svg_bbox_add_rect(out_bbox, x, y, w, h);
            if (style->stroke_type != SVG_PAINT_NONE && style->stroke_width > 0.0f) {
                svg_bbox_expand(out_bbox, style->stroke_width * 0.5f);
            }
        }
        svg_draw_samples_rect(ctx, style, m, x, y, w, h, rx, ry);
        return 1;
    }
    if (strcmp(local, "circle") == 0) {
        int ok = 0;
        float cx = svg_parse_length(svg_get_attr(tag, "cx"), ctx->vb_w, dpi, &ok);
        if (!ok) cx = 0.0f;
        float cy = svg_parse_length(svg_get_attr(tag, "cy"), ctx->vb_h, dpi, &ok);
        if (!ok) cy = 0.0f;
        float r = svg_parse_length(svg_get_attr(tag, "r"), (ctx->vb_w + ctx->vb_h) * 0.5f, dpi, &ok);
        if (out_bbox && r > 0.0f) {
            svg_bbox_add_rect(out_bbox, cx - r, cy - r, r * 2.0f, r * 2.0f);
            if (style->stroke_type != SVG_PAINT_NONE && style->stroke_width > 0.0f) {
                svg_bbox_expand(out_bbox, style->stroke_width * 0.5f);
            }
        }
        svg_draw_samples_ellipse(ctx, style, m, cx, cy, r, r);
        return 1;
    }
    if (strcmp(local, "ellipse") == 0) {
        int ok = 0;
        float cx = svg_parse_length(svg_get_attr(tag, "cx"), ctx->vb_w, dpi, &ok);
        if (!ok) cx = 0.0f;
        float cy = svg_parse_length(svg_get_attr(tag, "cy"), ctx->vb_h, dpi, &ok);
        if (!ok) cy = 0.0f;
        float rx = svg_parse_length(svg_get_attr(tag, "rx"), ctx->vb_w, dpi, &ok);
        float ry = svg_parse_length(svg_get_attr(tag, "ry"), ctx->vb_h, dpi, &ok);
        if (out_bbox && rx > 0.0f && ry > 0.0f) {
            svg_bbox_add_rect(out_bbox, cx - rx, cy - ry, rx * 2.0f, ry * 2.0f);
            if (style->stroke_type != SVG_PAINT_NONE && style->stroke_width > 0.0f) {
                svg_bbox_expand(out_bbox, style->stroke_width * 0.5f);
            }
        }
        svg_draw_samples_ellipse(ctx, style, m, cx, cy, rx, ry);
        return 1;
    }
    if (strcmp(local, "line") == 0) {
        int ok = 0;
        float x1 = svg_parse_length(svg_get_attr(tag, "x1"), ctx->vb_w, dpi, &ok);
        float y1 = svg_parse_length(svg_get_attr(tag, "y1"), ctx->vb_h, dpi, &ok);
        float x2 = svg_parse_length(svg_get_attr(tag, "x2"), ctx->vb_w, dpi, &ok);
        float y2 = svg_parse_length(svg_get_attr(tag, "y2"), ctx->vb_h, dpi, &ok);
        if (out_bbox) {
            float minx = fminf(x1, x2);
            float miny = fminf(y1, y2);
            float maxx = fmaxf(x1, x2);
            float maxy = fmaxf(y1, y2);
            svg_bbox_add_rect(out_bbox, minx, miny, maxx - minx, maxy - miny);
            if (style->stroke_type != SVG_PAINT_NONE && style->stroke_width > 0.0f) {
                svg_bbox_expand(out_bbox, style->stroke_width * 0.5f);
            }
        }
        svg_draw_samples_line(ctx, style, m, x1, y1, x2, y2);
        if (style->marker_start || style->marker_mid || style->marker_end) {
            svg_segments marker_segs;
            svg_segments_init(&marker_segs);
            if (svg_segments_add(&marker_segs, x1, y1, x2, y2, 1, 0)) {
                svg_draw_markers_for_segments(ctx, style, m, &marker_segs, dpi);
            }
            svg_segments_free(&marker_segs);
        }
        return 1;
    }
    if (strcmp(local, "polyline") == 0 || strcmp(local, "polygon") == 0) {
        const char *points = svg_get_attr(tag, "points");
        float *pts = NULL;
        int count = 0;
        if (svg_parse_points(points, &pts, &count)) {
            svg_segments segs;
            svg_segments_init(&segs);
            float minx = 0.0f, miny = 0.0f, maxx = 0.0f, maxy = 0.0f;
            if (count > 0) {
                minx = maxx = pts[0];
                miny = maxy = pts[1];
                for (int i = 1; i < count; i++) {
                    float px = pts[i * 2];
                    float py = pts[i * 2 + 1];
                    if (px < minx) minx = px;
                    if (px > maxx) maxx = px;
                    if (py < miny) miny = py;
                    if (py > maxy) maxy = py;
                }
            }
            for (int i = 0; i < count - 1; i++) {
                svg_segments_add(&segs, pts[i * 2], pts[i * 2 + 1], pts[(i + 1) * 2], pts[(i + 1) * 2 + 1], 1, 1);
            }
            if (strcmp(local, "polygon") == 0 && count > 1) {
                svg_segments_add(&segs, pts[(count - 1) * 2], pts[(count - 1) * 2 + 1], pts[0], pts[1], 1, 1);
            } else if (strcmp(local, "polyline") == 0 && count > 1) {
                svg_segments_add(&segs, pts[(count - 1) * 2], pts[(count - 1) * 2 + 1], pts[0], pts[1], 0, 1);
            }
            if (out_bbox && count > 0) {
                svg_bbox_add_rect(out_bbox, minx, miny, maxx - minx, maxy - miny);
                if (style->stroke_type != SVG_PAINT_NONE && style->stroke_width > 0.0f) {
                    svg_bbox_expand(out_bbox, style->stroke_width * 0.5f);
                }
            }
            svg_draw_samples_segments(ctx, style, m, &segs);
            svg_draw_markers_for_segments(ctx, style, m, &segs, dpi);
            svg_segments_free(&segs);
        }
        free(pts);
        return 1;
    }
    if (strcmp(local, "path") == 0) {
        const char *d = svg_get_attr(tag, "d");
        svg_segments segs;
        svg_segments_init(&segs);
        if (svg_parse_path(d, &segs)) {
            if (out_bbox && segs.stroke_count > 0) {
                svg_bbox_add_rect(out_bbox, segs.minx, segs.miny, segs.maxx - segs.minx, segs.maxy - segs.miny);
                if (style->stroke_type != SVG_PAINT_NONE && style->stroke_width > 0.0f) {
                    svg_bbox_expand(out_bbox, style->stroke_width * 0.5f);
                }
            }
            svg_draw_samples_segments(ctx, style, m, &segs);
            svg_draw_markers_for_segments(ctx, style, m, &segs, dpi);
        }
        svg_segments_free(&segs);
        return 1;
    }
    if (strcmp(local, "image") == 0) {
        if (style->visibility_hidden) {
            return 1;
        }
        const char *href = svg_get_attr(tag, "href");
        if (!href) {
            href = svg_get_attr(tag, "xlink:href");
        }
        cupidimage_image img;
        if (!svg_load_href_image(href, &img)) {
            return 0;
        }
        int ok = 0;
        float x = svg_parse_length(svg_get_attr(tag, "x"), ctx->vb_w, dpi, &ok);
        if (!ok) {
            x = 0.0f;
        }
        float y = svg_parse_length(svg_get_attr(tag, "y"), ctx->vb_h, dpi, &ok);
        if (!ok) {
            y = 0.0f;
        }
        int w_ok = 0;
        int h_ok = 0;
        float w = svg_parse_length(svg_get_attr(tag, "width"), ctx->vb_w, dpi, &w_ok);
        float h = svg_parse_length(svg_get_attr(tag, "height"), ctx->vb_h, dpi, &h_ok);
        if (!w_ok) {
            w = (float)img.width;
            w_ok = 1;
        }
        if (!h_ok) {
            h = (float)img.height;
            h_ok = 1;
        }
        int preserve_none = 0;
        float align_x = 0.5f;
        float align_y = 0.5f;
        svg_parse_preserve_aspect_ratio(svg_get_attr(tag, "preserveAspectRatio"),
                                        &preserve_none, &align_x, &align_y);
        if (w_ok && h_ok && w > 0.0f && h > 0.0f) {
            if (out_bbox) {
                svg_bbox_add_rect(out_bbox, x, y, w, h);
            }
            svg_draw_samples_image(ctx, style, m, &img, x, y, w, h,
                                   preserve_none, align_x, align_y);
        }
        cupidimage_free(&img);
        return 1;
    }
    if (strcmp(local, "use") == 0) {
        const char *href = svg_get_attr(tag, "href");
        if (!href) {
            href = svg_get_attr(tag, "xlink:href");
        }
        char idbuf[96];
        if (!svg_parse_href_id(href, idbuf, sizeof(idbuf))) {
            return 0;
        }
        int ok = 0;
        float x = svg_parse_length(svg_get_attr(tag, "x"), ctx->vb_w, dpi, &ok);
        if (!ok) {
            x = 0.0f;
        }
        float y = svg_parse_length(svg_get_attr(tag, "y"), ctx->vb_h, dpi, &ok);
        if (!ok) {
            y = 0.0f;
        }
        svg_matrix use_m = *m;
        if (x != 0.0f || y != 0.0f) {
            svg_matrix t;
            svg_matrix_identity(&t);
            t.e = x;
            t.f = y;
            svg_matrix tm;
            svg_matrix_multiply(&use_m, &t, &tm);
            use_m = tm;
        }
        const svg_symbol *sym = defs ? svg_defs_find_symbol(defs, idbuf) : NULL;
        if (sym && sym->content && *sym->content) {
            int w_ok = 0;
            int h_ok = 0;
            float sw = svg_parse_length(svg_get_attr(tag, "width"), ctx->vb_w, dpi, &w_ok);
            float sh = svg_parse_length(svg_get_attr(tag, "height"), ctx->vb_h, dpi, &h_ok);
            float sx = 1.0f;
            float sy = 1.0f;
            float ax = 0.0f;
            float ay = 0.0f;
            if (sym->vb_ok && sym->vb_w > 0.0f && sym->vb_h > 0.0f) {
                if (w_ok && !h_ok) {
                    sh = sw * (sym->vb_h / sym->vb_w);
                    h_ok = 1;
                } else if (!w_ok && h_ok) {
                    sw = sh * (sym->vb_w / sym->vb_h);
                    w_ok = 1;
                }
                if (w_ok && h_ok && sw > 0.0f && sh > 0.0f) {
                    sx = sw / sym->vb_w;
                    sy = sh / sym->vb_h;
                    if (!sym->preserve_none) {
                        float uni = fminf(sx, sy);
                        ax = (sw - sym->vb_w * uni) * sym->align_x;
                        ay = (sh - sym->vb_h * uni) * sym->align_y;
                        sx = uni;
                        sy = uni;
                    }
                }
                if (sx != 1.0f || sy != 1.0f) {
                    svg_matrix s;
                    svg_matrix_identity(&s);
                    s.a = sx;
                    s.d = sy;
                    svg_matrix_multiply(&use_m, &s, &use_m);
                }
                if (ax != 0.0f || ay != 0.0f) {
                    svg_matrix ta;
                    svg_matrix_identity(&ta);
                    ta.e = ax;
                    ta.f = ay;
                    svg_matrix_multiply(&use_m, &ta, &use_m);
                }
                svg_matrix tvb;
                svg_matrix_identity(&tvb);
                tvb.e = -sym->vb_x;
                tvb.f = -sym->vb_y;
                svg_matrix_multiply(&use_m, &tvb, &use_m);
            }
            char *use_attrs = svg_build_use_wrapper_attrs(tag);
            char *wrapped = svg_wrap_with_group_attrs(sym->content, use_attrs);
            free(use_attrs);
            int rendered = wrapped ? svg_render_transformed_snippet(ctx, wrapped, &use_m, dpi) : 0;
            free(wrapped);
            if (rendered) {
                return 1;
            }
        }
        svg_use_def *ud = defs ? svg_defs_find_use((svg_defs *)defs, idbuf) : NULL;
        if (ud && ud->tag_name && ud->attr_count > 0) {
            svg_tag ref_tag;
            memset(&ref_tag, 0, sizeof(ref_tag));
            ref_tag.name = ud->tag_name;
            ref_tag.attr_count = ud->attr_count;
            for (int i = 0; i < ud->attr_count; i++) {
                ref_tag.attrs[i].name = ud->attrs[i].name;
                ref_tag.attrs[i].value = ud->attrs[i].value;
            }
            const char *ref_local = svg_local_name(ref_tag.name);
            svg_style ref_style = *style;
            float base_len = (ctx->vb_w + ctx->vb_h) * 0.5f;
            svg_apply_presentation_attrs(&ref_style, &ref_tag, base_len, dpi, defs);
            svg_apply_inline_style_attr(&ref_style, &ref_tag, base_len, dpi, defs);
            /* Let explicit properties on <use> override referenced-element styling. */
            svg_apply_presentation_attrs(&ref_style, tag, base_len, dpi, defs);
            svg_apply_inline_style_attr(&ref_style, tag, base_len, dpi, defs);
            if (ref_style.display_none) {
                return 1;
            }
            if (ref_style.visibility_hidden) {
                ref_style.fill_type = SVG_PAINT_NONE;
                ref_style.stroke_type = SVG_PAINT_NONE;
                ref_style.fill_gradient = NULL;
                ref_style.stroke_gradient = NULL;
                ref_style.fill_pattern = NULL;
                ref_style.stroke_pattern = NULL;
            }
            svg_bbox tmp_bbox;
            int drew = svg_draw_shape(ctx, &ref_style, &use_m, &ref_tag, ref_local, dpi,
                                      NULL, anim_time, defs, &tmp_bbox);
            if (drew && out_bbox && svg_bbox_valid(&tmp_bbox)) {
                *out_bbox = tmp_bbox;
            }
            return drew;
        }
        const svg_path_def *pd = defs ? svg_defs_find_path(defs, idbuf) : NULL;
        if (!pd || !pd->d) {
            return 0;
        }
        svg_segments segs;
        svg_segments_init(&segs);
        if (svg_parse_path(pd->d, &segs)) {
            if (out_bbox && segs.stroke_count > 0) {
                svg_bbox_add_rect(out_bbox, segs.minx + x, segs.miny + y,
                                  segs.maxx - segs.minx, segs.maxy - segs.miny);
                if (style->stroke_type != SVG_PAINT_NONE && style->stroke_width > 0.0f) {
                    svg_bbox_expand(out_bbox, style->stroke_width * 0.5f);
                }
            }
            svg_draw_samples_segments(ctx, style, &use_m, &segs);
            svg_segments_free(&segs);
            return 1;
        }
        svg_segments_free(&segs);
        return 0;
    }
    return 0;
}

static int svg_render(svg_render_ctx *ctx, const unsigned char *data, size_t size, char *err, size_t errcap,
                      const cupidimage_svg_options *opts) {
    float dpi = opts ? opts->dpi : 96.0f;
    float anim_time = opts ? opts->animation_time : 0.0f;
    if (dpi <= 0.0f) {
        dpi = 96.0f;
    }
    if (anim_time < 0.0f) {
        anim_time = 0.0f;
    }
    svg_defs defs;
    svg_css css;
    svg_preamble pre;
    if (!svg_parse_preamble(data, size, &pre, &defs, &css, dpi, anim_time, err, errcap)) {
        svg_defs_free(&defs);
        svg_css_free(&css);
        return 0;
    }
    float intrinsic_w = pre.width_ok ? pre.width_attr : (pre.vb_ok ? pre.vb_w : 512.0f);
    float intrinsic_h = pre.height_ok ? pre.height_attr : (pre.vb_ok ? pre.vb_h : 512.0f);
    if (intrinsic_w <= 0.0f || intrinsic_h <= 0.0f) {
        svg_defs_free(&defs);
        svg_css_free(&css);
        set_err(err, errcap, "invalid SVG dimensions");
        return 0;
    }
    float out_w = intrinsic_w;
    float out_h = intrinsic_h;
    if (opts) {
        if (opts->width > 0 && opts->height > 0) {
            out_w = (float)opts->width;
            out_h = (float)opts->height;
        } else if (opts->width > 0) {
            out_w = (float)opts->width;
            out_h = intrinsic_h * (out_w / intrinsic_w);
        } else if (opts->height > 0) {
            out_h = (float)opts->height;
            out_w = intrinsic_w * (out_h / intrinsic_h);
        }
        if (opts->scale > 0.0f) {
            out_w *= opts->scale;
            out_h *= opts->scale;
        }
    }
    if (out_w < 1.0f) out_w = 1.0f;
    if (out_h < 1.0f) out_h = 1.0f;
    ctx->width = (uint32_t)lroundf(out_w);
    ctx->height = (uint32_t)lroundf(out_h);
    ctx->ss = opts && opts->supersampling ? opts->supersampling : 2;
    if (ctx->ss != 1 && ctx->ss != 2 && ctx->ss != 4) {
        ctx->ss = 2;
    }
    ctx->dpi = dpi;
    ctx->anim_time = anim_time;
    if (!pre.vb_ok) {
        pre.vb_x = 0.0f;
        pre.vb_y = 0.0f;
        pre.vb_w = intrinsic_w;
        pre.vb_h = intrinsic_h;
    }
    ctx->vb_x = pre.vb_x;
    ctx->vb_y = pre.vb_y;
    ctx->vb_w = pre.vb_w;
    ctx->vb_h = pre.vb_h;

    size_t hi_w = (size_t)ctx->width * (size_t)ctx->ss;
    size_t hi_h = (size_t)ctx->height * (size_t)ctx->ss;
    size_t hi_size = hi_w * hi_h * 4u;
    if (hi_w == 0 || hi_h == 0 || hi_size / 4u != hi_w * hi_h) {
        svg_defs_free(&defs);
        svg_css_free(&css);
        set_err(err, errcap, "image too large");
        return 0;
    }
    ctx->hi_rgba = (uint8_t *)malloc(hi_size);
    if (!ctx->hi_rgba) {
        svg_defs_free(&defs);
        svg_css_free(&css);
        set_err(err, errcap, "out of memory");
        return 0;
    }
    uint8_t bg_alpha = opts ? opts->background_alpha : 0;
    uint8_t bg_r = 255;
    uint8_t bg_g = 255;
    uint8_t bg_b = 255;
    uint8_t bg_pr = (uint8_t)((bg_r * bg_alpha) / 255u);
    uint8_t bg_pg = (uint8_t)((bg_g * bg_alpha) / 255u);
    uint8_t bg_pb = (uint8_t)((bg_b * bg_alpha) / 255u);
    for (size_t i = 0; i < hi_w * hi_h; i++) {
        ctx->hi_rgba[i * 4u + 0] = bg_pr;
        ctx->hi_rgba[i * 4u + 1] = bg_pg;
        ctx->hi_rgba[i * 4u + 2] = bg_pb;
        ctx->hi_rgba[i * 4u + 3] = bg_alpha;
    }

    svg_matrix root_view;
    svg_matrix_identity(&root_view);
    float scale_x = ctx->width / pre.vb_w;
    float scale_y = ctx->height / pre.vb_h;
    if (pre.preserve_none) {
        svg_matrix_scale(&root_view, scale_x, scale_y);
        svg_matrix_translate(&root_view, -pre.vb_x * scale_x, -pre.vb_y * scale_y);
    } else {
        float scale = fminf(scale_x, scale_y);
        float extra_x = ctx->width - pre.vb_w * scale;
        float extra_y = ctx->height - pre.vb_h * scale;
        float tx = -pre.vb_x * scale + extra_x * pre.align_x;
        float ty = -pre.vb_y * scale + extra_y * pre.align_y;
        svg_matrix_scale(&root_view, scale, scale);
        svg_matrix_translate(&root_view, tx, ty);
    }

    char *buf = (char *)malloc(size + 1);
    if (!buf) {
        svg_defs_free(&defs);
        svg_css_free(&css);
        free(ctx->hi_rgba);
        ctx->hi_rgba = NULL;
        set_err(err, errcap, "out of memory");
        return 0;
    }
    memcpy(buf, data, size);
    buf[size] = '\0';
    char *p = buf;
    svg_tag tag;
    svg_stack_item stack[SVG_MAX_DEPTH];
    svg_sibling_frame sibling_frames[SVG_MAX_DEPTH];
    memset(sibling_frames, 0, sizeof(sibling_frames));
    int depth = 0;
    int root_pushed = 0;
    int skip_depth = 0;
    float base_len = (ctx->vb_w + ctx->vb_h) * 0.5f;

    while (svg_next_tag(&p, &tag)) {
        const char *local = svg_local_name(tag.name);
        if (tag.is_end) {
            if (skip_depth > 0) {
                skip_depth--;
                continue;
            }
            if (depth > 1) {
                int clear_level = depth;
                if (clear_level >= SVG_MAX_DEPTH) {
                    clear_level = SVG_MAX_DEPTH - 1;
                }
                sibling_frames[clear_level].count = 0;
                depth--;
            }
            continue;
        }
        if (!root_pushed) {
            if (strcmp(local, "svg") != 0) {
                continue;
            }
            svg_tag root_tag = tag;
            if (anim_time >= 0.0f) {
                svg_css_apply_animations_to_tag(&root_tag, &css, anim_time);
            }
            svg_style root_style;
            svg_style_init(&root_style);
            svg_apply_presentation_attrs(&root_style, &root_tag, base_len, dpi, &defs);
            svg_elem_info root_info;
            svg_elem_info_from_tag(&root_info, local, &root_tag);
            svg_elem_info tmp_stack[SVG_MAX_DEPTH];
            tmp_stack[0] = root_info;
            svg_css_apply(&root_style, &css, &defs, tmp_stack, 1, NULL, 0, base_len, dpi);
            svg_apply_inline_style_attr(&root_style, &root_tag, base_len, dpi, &defs);
            svg_matrix root_attr;
            svg_matrix_identity(&root_attr);
            const char *root_transform_attr = svg_get_attr(&root_tag, "transform");
            if (root_transform_attr) {
                svg_parse_transform(root_transform_attr, &root_attr);
            }
            svg_matrix root_total;
            svg_matrix_multiply(&root_view, &root_attr, &root_total);
            stack[depth].style = root_style;
            stack[depth].transform = root_total;
            stack[depth].info = root_info;
            depth = 1;
            root_pushed = 1;
            if (root_tag.is_self_closing) {
                depth = 0;
                root_pushed = 0;
            }
            continue;
        }
        if (skip_depth > 0) {
            if (!tag.is_self_closing) {
                skip_depth++;
            }
            continue;
        }
        if (strcmp(local, "style") == 0) {
            if (!tag.is_self_closing) {
                char *after = NULL;
                char *close = svg_find_close_tag(p, "style", &after);
                if (close && after) {
                    p = after;
                }
            }
            continue;
        }
        if (strcmp(local, "defs") == 0 || strcmp(local, "clippath") == 0 || strcmp(local, "mask") == 0 ||
            strcmp(local, "lineargradient") == 0 || strcmp(local, "radialgradient") == 0 ||
            strcmp(local, "pattern") == 0 || strcmp(local, "filter") == 0 ||
            strcmp(local, "symbol") == 0 || strcmp(local, "marker") == 0) {
            if (!tag.is_self_closing) {
                skip_depth = 1;
            }
            continue;
        }

        svg_tag animated_tag = tag;
        const svg_tag *draw_tag = &animated_tag;
        if (!tag.is_self_closing && anim_time >= 0.0f) {
            svg_apply_animation_children(&animated_tag, local, p, anim_time);
        }
        if (anim_time >= 0.0f) {
            svg_css_apply_animations_to_tag(&animated_tag, &css, anim_time);
        }

        svg_style style = stack[depth - 1].style;
        svg_apply_presentation_attrs(&style, draw_tag, base_len, dpi, &defs);
        svg_elem_info info;
        svg_elem_info_from_tag(&info, local, draw_tag);
        svg_elem_info tmp_stack[SVG_MAX_DEPTH];
        int tmp_depth = depth < SVG_MAX_DEPTH ? depth : SVG_MAX_DEPTH;
        for (int i = 0; i < tmp_depth; i++) {
            tmp_stack[i] = stack[i].info;
        }
        if (tmp_depth < SVG_MAX_DEPTH) {
            tmp_stack[tmp_depth++] = info;
        }
        int sib_level = depth;
        if (sib_level >= SVG_MAX_DEPTH) {
            sib_level = SVG_MAX_DEPTH - 1;
        }
        svg_sibling_frame *sib_frame = &sibling_frames[sib_level];
        svg_css_apply(&style, &css, &defs, tmp_stack, tmp_depth,
                      sib_frame->items, sib_frame->count, base_len, dpi);
        svg_apply_inline_style_attr(&style, draw_tag, base_len, dpi, &defs);
        if (!svg_sibling_frame_push(sib_frame, &info)) {
            continue;
        }

        svg_matrix m = stack[depth - 1].transform;
        const char *transform_attr = svg_get_attr(draw_tag, "transform");
        if (transform_attr) {
            svg_matrix t;
            if (svg_parse_transform(transform_attr, &t)) {
                svg_matrix tmp;
                svg_matrix_multiply(&m, &t, &tmp);
                m = tmp;
            }
        }

        if (style.display_none) {
            if (!tag.is_self_closing) {
                skip_depth = 1;
            }
            continue;
        }

        if (strcmp(local, "g") == 0 || strcmp(local, "svg") == 0) {
            if (!draw_tag->is_self_closing && depth < SVG_MAX_DEPTH) {
                stack[depth].style = style;
                stack[depth].transform = m;
                stack[depth].info = info;
                depth++;
            }
            continue;
        }

        if (style.filter && !style.visibility_hidden) {
            svg_render_ctx tmp_ctx = *ctx;
            size_t hi_w = (size_t)ctx->width * (size_t)ctx->ss;
            size_t hi_h = (size_t)ctx->height * (size_t)ctx->ss;
            size_t hi_size = hi_w * hi_h * 4u;
            tmp_ctx.hi_rgba = (uint8_t *)calloc(1u, hi_size);
            if (!tmp_ctx.hi_rgba) {
                continue;
            }
            int drew = 0;
            svg_bbox bbox;
            svg_bbox_init(&bbox);
            int need_fill = 0;
            int need_stroke = 0;
            int need_bg = 0;
            int need_bg_alpha = 0;
            if (style.filter && style.filter->content) {
                need_fill = svg_strcasestr_simple(style.filter->content, "fillpaint");
                need_stroke = svg_strcasestr_simple(style.filter->content, "strokepaint");
                need_bg = svg_strcasestr_simple(style.filter->content, "backgroundimage");
                need_bg_alpha = svg_strcasestr_simple(style.filter->content, "backgroundalpha");
            }
            uint8_t *fill_buf = NULL;
            uint8_t *stroke_buf = NULL;
            uint8_t *bg_buf = NULL;
            uint8_t *bg_alpha_buf = NULL;
            if (need_bg || need_bg_alpha) {
                bg_buf = (uint8_t *)malloc(hi_size);
                if (bg_buf) {
                    memcpy(bg_buf, ctx->hi_rgba, hi_size);
                }
                if (need_bg_alpha && bg_buf) {
                    bg_alpha_buf = (uint8_t *)malloc(hi_size);
                    if (bg_alpha_buf) {
                        for (size_t bi = 0; bi < hi_w * hi_h; bi++) {
                            uint8_t a = bg_buf[bi * 4u + 3];
                            bg_alpha_buf[bi * 4u + 0] = a;
                            bg_alpha_buf[bi * 4u + 1] = a;
                            bg_alpha_buf[bi * 4u + 2] = a;
                            bg_alpha_buf[bi * 4u + 3] = a;
                        }
                    }
                }
            }
            if (strcmp(local, "text") == 0) {
                svg_elem_info text_stack[SVG_MAX_DEPTH];
                int text_depth = depth < SVG_MAX_DEPTH ? depth : SVG_MAX_DEPTH;
                for (int i = 0; i < text_depth; i++) {
                    text_stack[i] = stack[i].info;
                }
                if (text_depth < SVG_MAX_DEPTH) {
                    text_stack[text_depth++] = info;
                }
                if (!draw_tag->is_self_closing) {
                    if (need_fill) {
                        svg_render_ctx fill_ctx = *ctx;
                        fill_ctx.hi_rgba = (uint8_t *)calloc(1u, hi_size);
                        if (fill_ctx.hi_rgba) {
                            svg_style fill_style = style;
                            fill_style.stroke_type = SVG_PAINT_NONE;
                            fill_style.stroke_gradient = NULL;
                            fill_style.stroke_pattern = NULL;
                            fill_style.stroke_width = 0.0f;
                            char *tp = p;
                            svg_render_text_element(&fill_ctx, &fill_style, &m, draw_tag, &tp, &defs, &css,
                                                    text_stack, text_depth, base_len, dpi, NULL);
                            fill_buf = fill_ctx.hi_rgba;
                        }
                    }
                    if (need_stroke) {
                        svg_render_ctx stroke_ctx = *ctx;
                        stroke_ctx.hi_rgba = (uint8_t *)calloc(1u, hi_size);
                        if (stroke_ctx.hi_rgba) {
                            svg_style stroke_style = style;
                            stroke_style.fill_type = SVG_PAINT_NONE;
                            stroke_style.fill_gradient = NULL;
                            stroke_style.fill_pattern = NULL;
                            char *tp = p;
                            svg_render_text_element(&stroke_ctx, &stroke_style, &m, draw_tag, &tp, &defs, &css,
                                                    text_stack, text_depth, base_len, dpi, NULL);
                            stroke_buf = stroke_ctx.hi_rgba;
                        }
                    }
                    char *tp = p;
                    svg_render_text_element(&tmp_ctx, &style, &m, draw_tag, &tp, &defs, &css,
                                            text_stack, text_depth, base_len, dpi, &bbox);
                    p = tp;
                }
                drew = 1;
            } else {
                drew = svg_draw_shape(&tmp_ctx, &style, &m, draw_tag, local, dpi,
                                      NULL, anim_time, &defs, &bbox);
                if (drew) {
                    if (need_fill) {
                        svg_render_ctx fill_ctx = *ctx;
                        fill_ctx.hi_rgba = (uint8_t *)calloc(1u, hi_size);
                        if (fill_ctx.hi_rgba) {
                            svg_style fill_style = style;
                            fill_style.stroke_type = SVG_PAINT_NONE;
                            fill_style.stroke_gradient = NULL;
                            fill_style.stroke_pattern = NULL;
                            fill_style.stroke_width = 0.0f;
                            svg_draw_shape(&fill_ctx, &fill_style, &m, draw_tag, local, dpi,
                                           NULL, anim_time, &defs, NULL);
                            fill_buf = fill_ctx.hi_rgba;
                        }
                    }
                    if (need_stroke) {
                        svg_render_ctx stroke_ctx = *ctx;
                        stroke_ctx.hi_rgba = (uint8_t *)calloc(1u, hi_size);
                        if (stroke_ctx.hi_rgba) {
                            svg_style stroke_style = style;
                            stroke_style.fill_type = SVG_PAINT_NONE;
                            stroke_style.fill_gradient = NULL;
                            stroke_style.fill_pattern = NULL;
                            svg_draw_shape(&stroke_ctx, &stroke_style, &m, draw_tag, local, dpi,
                                           NULL, anim_time, &defs, NULL);
                            stroke_buf = stroke_ctx.hi_rgba;
                        }
                    }
                }
            }
            if (drew) {
                svg_filter_region region;
                svg_compute_filter_region(style.filter, &tmp_ctx, &bbox, &m, &region);
                svg_apply_filter_ops(style.filter, &tmp_ctx, tmp_ctx.hi_rgba, base_len, &region,
                                     &bbox, &m, bg_buf, bg_alpha_buf, fill_buf, stroke_buf);
                svg_composite_buffer(ctx, tmp_ctx.hi_rgba);
            }
            free(tmp_ctx.hi_rgba);
            free(fill_buf);
            free(stroke_buf);
            free(bg_buf);
            free(bg_alpha_buf);
            continue;
        }

        if (strcmp(local, "text") == 0) {
            svg_elem_info text_stack[SVG_MAX_DEPTH];
            int text_depth = depth < SVG_MAX_DEPTH ? depth : SVG_MAX_DEPTH;
            for (int i = 0; i < text_depth; i++) {
                text_stack[i] = stack[i].info;
            }
            if (text_depth < SVG_MAX_DEPTH) {
                text_stack[text_depth++] = info;
            }
            if (!draw_tag->is_self_closing) {
                svg_style text_style = style;
                if (text_style.visibility_hidden) {
                    text_style.fill_type = SVG_PAINT_NONE;
                    text_style.stroke_type = SVG_PAINT_NONE;
                    text_style.fill_gradient = NULL;
                    text_style.stroke_gradient = NULL;
                    text_style.fill_pattern = NULL;
                    text_style.stroke_pattern = NULL;
                }
                svg_render_text_element(ctx, &text_style, &m, draw_tag, &p, &defs, &css,
                                        text_stack, text_depth, base_len, dpi, NULL);
            }
            continue;
        }

        svg_style draw_style = style;
        if (draw_style.visibility_hidden) {
            draw_style.fill_type = SVG_PAINT_NONE;
            draw_style.stroke_type = SVG_PAINT_NONE;
            draw_style.fill_gradient = NULL;
            draw_style.stroke_gradient = NULL;
            draw_style.fill_pattern = NULL;
            draw_style.stroke_pattern = NULL;
        }
        if (svg_draw_shape(ctx, &draw_style, &m, draw_tag, local, dpi, NULL, anim_time, &defs, NULL)) {
            continue;
        }
    }

    free(buf);
    svg_sibling_frames_free(sibling_frames, SVG_MAX_DEPTH);
    svg_defs_free(&defs);
    svg_css_free(&css);
    return 1;
}

int cupidimage_svg_get_dimensions(const unsigned char *data, size_t size,
                                  uint32_t *width, uint32_t *height,
                                  char *err, size_t errcap) {
    if (!data || !width || !height) {
        set_err(err, errcap, "invalid arguments");
        return 0;
    }
    char *buf = (char *)malloc(size + 1);
    if (!buf) {
        set_err(err, errcap, "out of memory");
        return 0;
    }
    memcpy(buf, data, size);
    buf[size] = '\0';
    char *p = buf;
    svg_tag tag;
    int found = 0;
    float w = 0.0f, h = 0.0f;
    int w_ok = 0, h_ok = 0;
    float vb_x = 0.0f, vb_y = 0.0f, vb_w = 0.0f, vb_h = 0.0f;
    int vb_ok = 0;
    while (svg_next_tag(&p, &tag)) {
        if (!tag.is_end && strcmp(svg_local_name(tag.name), "svg") == 0) {
            found = 1;
            float dpi = 96.0f;
            w = svg_parse_length(svg_get_attr(&tag, "width"), 0.0f, dpi, &w_ok);
            h = svg_parse_length(svg_get_attr(&tag, "height"), 0.0f, dpi, &h_ok);
            vb_ok = svg_parse_viewbox(svg_get_attr(&tag, "viewBox"), &vb_x, &vb_y, &vb_w, &vb_h);
            break;
        }
    }
    free(buf);
    if (!found) {
        set_err(err, errcap, "not an SVG");
        return 0;
    }
    float intrinsic_w = w_ok ? w : (vb_ok ? vb_w : 512.0f);
    float intrinsic_h = h_ok ? h : (vb_ok ? vb_h : 512.0f);
    if (intrinsic_w <= 0.0f || intrinsic_h <= 0.0f) {
        set_err(err, errcap, "invalid SVG dimensions");
        return 0;
    }
    *width = (uint32_t)lroundf(intrinsic_w);
    *height = (uint32_t)lroundf(intrinsic_h);
    return 1;
}

int cupidimage_load_svg_with_options(const unsigned char *data, size_t size,
                                     cupidimage_image *out,
                                     const cupidimage_svg_options *opts,
                                     char *err, size_t errcap) {
    if (!data || !out) {
        set_err(err, errcap, "invalid arguments");
        return 0;
    }
    svg_render_ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    int ok = svg_render(&ctx, data, size, err, errcap, opts);
    if (!ok) {
        free(ctx.hi_rgba);
        return 0;
    }

    size_t out_size = (size_t)ctx.width * (size_t)ctx.height * 4u;
    if (out_size / 4u != (size_t)ctx.width * (size_t)ctx.height) {
        free(ctx.hi_rgba);
        set_err(err, errcap, "image too large");
        return 0;
    }
    uint8_t *rgba = (uint8_t *)malloc(out_size);
    if (!rgba) {
        free(ctx.hi_rgba);
        set_err(err, errcap, "out of memory");
        return 0;
    }

    uint32_t ss = ctx.ss;
    size_t hi_w = (size_t)ctx.width * ss;
    size_t block = (size_t)ss * (size_t)ss;
    for (uint32_t y = 0; y < ctx.height; y++) {
        for (uint32_t x = 0; x < ctx.width; x++) {
            uint64_t sum_r = 0, sum_g = 0, sum_b = 0, sum_a = 0;
            size_t base = ((size_t)y * ss * hi_w) + ((size_t)x * ss);
            for (uint32_t sy = 0; sy < ss; sy++) {
                size_t row = base + (size_t)sy * hi_w;
                for (uint32_t sx = 0; sx < ss; sx++) {
                    size_t idx = (row + sx) * 4u;
                    sum_r += ctx.hi_rgba[idx + 0];
                    sum_g += ctx.hi_rgba[idx + 1];
                    sum_b += ctx.hi_rgba[idx + 2];
                    sum_a += ctx.hi_rgba[idx + 3];
                }
            }
            uint8_t a = (uint8_t)(sum_a / block);
            uint8_t r = 0, g = 0, b = 0;
            if (a > 0) {
                r = (uint8_t)((sum_r / block) * 255u / a);
                g = (uint8_t)((sum_g / block) * 255u / a);
                b = (uint8_t)((sum_b / block) * 255u / a);
            }
            size_t out_idx = ((size_t)y * ctx.width + x) * 4u;
            rgba[out_idx + 0] = r;
            rgba[out_idx + 1] = g;
            rgba[out_idx + 2] = b;
            rgba[out_idx + 3] = a;
        }
    }

    free(ctx.hi_rgba);
    out->width = ctx.width;
    out->height = ctx.height;
    out->rgba = rgba;
    out->hotspot_x = 0;
    out->hotspot_y = 0;
    return 1;
}

int cupidimage_load_svg(const unsigned char *data, size_t size, cupidimage_image *out,
                        char *err, size_t errcap) {
    cupidimage_svg_options opts;
    opts.width = 0;
    opts.height = 0;
    opts.scale = 1.0f;
    opts.dpi = 96.0f;
    opts.animation_time = 0.0f;
    opts.supersampling = 2;
    opts.background_alpha = 0;
    return cupidimage_load_svg_with_options(data, size, out, &opts, err, errcap);
}

int cupidimage_load_svg_file(const char *path, cupidimage_image *out,
                             char *err, size_t errcap) {
    return cupidimage_load_image_file_via_memory(path, out, err, errcap, cupidimage_load_svg);
}

int cupidimage_load_svg_file_with_options(const char *path, cupidimage_image *out,
                                          const cupidimage_svg_options *opts,
                                          char *err, size_t errcap) {
    if (!path || !out) {
        set_err(err, errcap, "invalid arguments");
        return 0;
    }
    unsigned char *data = NULL;
    size_t size = 0;
    if (!cupidimage_read_file_bytes(path, &data, &size, err, errcap)) {
        return 0;
    }
    int ok = cupidimage_load_svg_with_options(data, size, out, opts, err, errcap);
    free(data);
    return ok;
}

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
