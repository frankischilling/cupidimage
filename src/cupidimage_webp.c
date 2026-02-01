#include "cupidimage.h"
#include "cupidimage_webp_tables.h"
#include "cupidimage_webp_lossless.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static void set_err(char *err, size_t errcap, const char *msg) {
    if (err && errcap) {
        snprintf(err, errcap, "%s", msg);
    }
}

static uint32_t read_le32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t read_le24(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
}

static uint8_t clamp_u8(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

static int parse_vp8_keyframe(const unsigned char *data, size_t size,
                              uint16_t *out_w, uint16_t *out_h,
                              size_t *out_payload_off,
                              uint32_t *out_first_part_size) {
    if (size < 10) {
        return 0;
    }
    uint32_t tag = (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16);
    int frame_type = tag & 1;
    if (frame_type != 0) {
        return 0;
    }
    uint32_t first_part_size = (tag >> 5) & 0x7FFFFu;
    if (data[3] != 0x9D || data[4] != 0x01 || data[5] != 0x2A) {
        return 0;
    }
    uint16_t width = (uint16_t)(data[6] | ((data[7] & 0x3F) << 8));
    uint16_t height = (uint16_t)(data[8] | ((data[9] & 0x3F) << 8));
    *out_w = width;
    *out_h = height;
    *out_payload_off = 3;
    if (out_first_part_size) {
        *out_first_part_size = first_part_size;
    }
    return 1;
}

typedef struct vp8_bool_decoder {
    const uint8_t *input;
    size_t input_len;
    uint32_t range;
    uint32_t value;
    int bit_count;
} vp8_bool_decoder;

static void vp8_br_init(vp8_bool_decoder *br, const uint8_t *data, size_t size) {
    br->input = NULL;
    br->input_len = 0;
    br->range = 255;
    br->value = 0;
    br->bit_count = 0;
    if (size >= 2) {
        br->value = ((uint32_t)data[0] << 8) | data[1];
        br->input = data + 2;
        br->input_len = size - 2;
    } else if (size == 1) {
        br->value = (uint32_t)data[0] << 8;
        br->input = data + 1;
        br->input_len = 0;
    }
}

static int vp8_br_read_bit(vp8_bool_decoder *br, int prob) {
    uint32_t split = 1 + (((br->range - 1) * (uint32_t)prob) >> 8);
    uint32_t bigsplit = split << 8;
    int bit;
    if (br->value >= bigsplit) {
        bit = 1;
        br->range -= split;
        br->value -= bigsplit;
    } else {
        bit = 0;
        br->range = split;
    }

    while (br->range < 128) {
        br->range <<= 1;
        br->value <<= 1;
        br->bit_count++;
        if (br->bit_count == 8) {
            br->bit_count = 0;
            if (br->input_len) {
                br->value |= *br->input++;
                br->input_len--;
            }
        }
    }
    return bit;
}

static uint32_t vp8_br_read_bits(vp8_bool_decoder *br, int bits) {
    uint32_t v = 0;
    for (int i = bits - 1; i >= 0; i--) {
        v |= (uint32_t)vp8_br_read_bit(br, 128) << i;
    }
    return v;
}

static int vp8_br_read_signed(vp8_bool_decoder *br, int bits) {
    int v = (int)vp8_br_read_bits(br, bits);
    int sign = vp8_br_read_bit(br, 128);
    return sign ? -v : v;
}

enum {
    DCT_0 = 0,
    DCT_1 = 1,
    DCT_2 = 2,
    DCT_3 = 3,
    DCT_4 = 4,
    DCT_CAT1 = 5,
    DCT_CAT2 = 6,
    DCT_CAT3 = 7,
    DCT_CAT4 = 8,
    DCT_CAT5 = 9,
    DCT_CAT6 = 10,
    DCT_EOB = 11
};

enum {
    DC_PRED = 0,
    V_PRED = 1,
    H_PRED = 2,
    TM_PRED = 3,
    B_PRED = 4
};

enum {
    B_DC_PRED = 0,
    B_TM_PRED = 1,
    B_VE_PRED = 2,
    B_HE_PRED = 3,
    B_LD_PRED = 4,
    B_RD_PRED = 5,
    B_VR_PRED = 6,
    B_VL_PRED = 7,
    B_HD_PRED = 8,
    B_HU_PRED = 9
};

static const int16_t coeff_tree[22] = {
    -DCT_EOB,  2,
    -DCT_0,    4,
    -DCT_1,    6,
     8,       12,
    -DCT_2,   10,
    -DCT_3,   -DCT_4,
    14,       16,
    -DCT_CAT1, -DCT_CAT2,
    18,       20,
    -DCT_CAT3, -DCT_CAT4,
    -DCT_CAT5, -DCT_CAT6
};

static const int16_t ymode_tree[8] = {
    -DC_PRED, 2,
    -V_PRED, 4,
    -H_PRED, 6,
    -TM_PRED, -B_PRED
};

static const int16_t uv_mode_tree[6] = {
    -DC_PRED, 2,
    -V_PRED, 4,
    -H_PRED, -TM_PRED
};

static const int16_t bmode_tree[18] = {
    -B_DC_PRED, 2,
    4, 6,
    -B_TM_PRED, -B_VE_PRED,
    8, 12,
    -B_HE_PRED, 10,
    -B_RD_PRED, -B_VR_PRED,
    14, 16,
    -B_LD_PRED, -B_VL_PRED,
    -B_HD_PRED, -B_HU_PRED
};

static const int16_t segment_tree[6] = {
    -0, 2,
    -1, 4,
    -2, -3
};

static const uint8_t pcat1[2]  = {159, 0};
static const uint8_t pcat2[3]  = {165, 145, 0};
static const uint8_t pcat3[4]  = {173, 148, 140, 0};
static const uint8_t pcat4[5]  = {176, 155, 140, 135, 0};
static const uint8_t pcat5[6]  = {180, 157, 141, 134, 130, 0};
static const uint8_t pcat6[12] = {254, 254, 243, 230, 196, 177, 153, 140, 133, 130, 129, 0};

static const uint8_t kf_ymode_prob[4] = {145, 156, 163, 128};
static const uint8_t kf_uv_mode_prob[3] = {142, 114, 183};

static const uint8_t zigzag[16] = {
    0, 1, 4, 8,
    5, 2, 3, 6,
    9, 12, 13, 10,
    7, 11, 14, 15
};

static const uint8_t coeff_bands[16] = {
    0, 1, 2, 3,
    6, 4, 5, 6,
    6, 6, 6, 6,
    6, 6, 6, 7
};

static const uint8_t left_context_index[25] = {
    0, 0, 0, 0,
    1, 1, 1, 1,
    2, 2, 2, 2,
    3, 3, 3, 3,
    4, 4, 5, 5,
    6, 6, 7, 7,
    8
};

static const uint8_t above_context_index[25] = {
    0, 1, 2, 3,
    0, 1, 2, 3,
    0, 1, 2, 3,
    0, 1, 2, 3,
    4, 5, 4, 5,
    6, 7, 6, 7,
    8
};

static int vp8_read_tree(vp8_bool_decoder *br, const int16_t *tree, const uint8_t *probs) {
    int i = 0;
    while ((i = tree[i + vp8_br_read_bit(br, probs[i >> 1])]) > 0) {
    }
    return -i;
}

static int vp8_read_tree_skip_eob(vp8_bool_decoder *br, const int16_t *tree, const uint8_t *probs) {
    int i = 2;
    while ((i = tree[i + vp8_br_read_bit(br, probs[i >> 1])]) > 0) {
    }
    return -i;
}

static int avg2p(int x, int y) {
    return (x + y + 1) >> 1;
}

static int avg3p(int x, int y, int z) {
    return (x + 2 * y + z + 2) >> 2;
}

static void subblock_intra_predict(uint8_t *dst, int stride,
                                   const uint8_t *A, const uint8_t *L, uint8_t P,
                                   int mode) {
    int i, r;
    int E[13];
    E[0] = L[3];
    E[1] = L[2];
    E[2] = L[1];
    E[3] = L[0];
    E[4] = P;
    E[5] = A[0];
    E[6] = A[1];
    E[7] = A[2];
    E[8] = A[3];
    E[9] = A[4];
    E[10] = A[5];
    E[11] = A[6];
    E[12] = A[7];

    switch (mode) {
    case B_DC_PRED:
        r = (E[0] + E[1] + E[2] + E[3] + E[5] + E[6] + E[7] + E[8] + 4) >> 3;
        for (i = 0; i < 4; i++) {
            memset(dst + i * stride, r, 4);
        }
        break;
    case B_TM_PRED:
        for (i = 0; i < 4; i++) {
            dst[i * stride + 0] = clamp_u8(L[i] + A[0] - P);
            dst[i * stride + 1] = clamp_u8(L[i] + A[1] - P);
            dst[i * stride + 2] = clamp_u8(L[i] + A[2] - P);
            dst[i * stride + 3] = clamp_u8(L[i] + A[3] - P);
        }
        break;
    case B_VE_PRED:
        dst[0] = dst[1] = dst[2] = dst[3] = avg3p(E[4], E[5], E[6]);
        dst[stride + 0] = dst[stride + 1] = dst[stride + 2] = dst[stride + 3] = avg3p(E[5], E[6], E[7]);
        dst[stride * 2 + 0] = dst[stride * 2 + 1] = dst[stride * 2 + 2] = dst[stride * 2 + 3] = avg3p(E[6], E[7], E[8]);
        dst[stride * 3 + 0] = dst[stride * 3 + 1] = dst[stride * 3 + 2] = dst[stride * 3 + 3] = avg3p(E[7], E[8], E[9]);
        break;
    case B_HE_PRED:
        dst[0] = dst[stride] = dst[stride * 2] = dst[stride * 3] = avg3p(E[4], E[3], E[2]);
        dst[1] = dst[stride + 1] = dst[stride * 2 + 1] = dst[stride * 3 + 1] = avg3p(E[3], E[2], E[1]);
        dst[2] = dst[stride + 2] = dst[stride * 2 + 2] = dst[stride * 3 + 2] = avg3p(E[2], E[1], E[0]);
        dst[3] = dst[stride + 3] = dst[stride * 2 + 3] = dst[stride * 3 + 3] = avg3p(E[1], E[0], E[0]);
        break;
    case B_LD_PRED:
        dst[0] = avg3p(E[5], E[6], E[7]);
        dst[1] = dst[stride + 0] = avg3p(E[6], E[7], E[8]);
        dst[2] = dst[stride + 1] = dst[stride * 2 + 0] = avg3p(E[7], E[8], E[9]);
        dst[3] = dst[stride + 2] = dst[stride * 2 + 1] = dst[stride * 3 + 0] = avg3p(E[8], E[9], E[10]);
        dst[stride + 3] = dst[stride * 2 + 2] = dst[stride * 3 + 1] = avg3p(E[9], E[10], E[11]);
        dst[stride * 2 + 3] = dst[stride * 3 + 2] = avg3p(E[10], E[11], E[12]);
        dst[stride * 3 + 3] = avg3p(E[11], E[12], E[12]);
        break;
    case B_RD_PRED:
        dst[3] = avg3p(E[2], E[1], E[0]);
        dst[2] = dst[stride + 3] = avg3p(E[3], E[2], E[1]);
        dst[1] = dst[stride + 2] = dst[stride * 2 + 3] = avg3p(E[4], E[3], E[2]);
        dst[0] = dst[stride + 1] = dst[stride * 2 + 2] = dst[stride * 3 + 3] = avg3p(E[5], E[4], E[3]);
        dst[stride + 0] = dst[stride * 2 + 1] = dst[stride * 3 + 2] = avg3p(E[6], E[5], E[4]);
        dst[stride * 2 + 0] = dst[stride * 3 + 1] = avg3p(E[7], E[6], E[5]);
        dst[stride * 3 + 0] = avg3p(E[8], E[7], E[6]);
        break;
    case B_VR_PRED: {
        int a = avg2p(E[4], E[5]);
        int b = avg2p(E[5], E[6]);
        int c = avg2p(E[6], E[7]);
        int d = avg2p(E[7], E[8]);
        int e = avg3p(E[3], E[4], E[5]);
        int f = avg3p(E[4], E[5], E[6]);
        int g = avg3p(E[5], E[6], E[7]);
        int h = avg3p(E[6], E[7], E[8]);
        dst[0] = b;
        dst[1] = c;
        dst[2] = d;
        dst[3] = d;
        dst[stride + 0] = a;
        dst[stride + 1] = b;
        dst[stride + 2] = c;
        dst[stride + 3] = d;
        dst[stride * 2 + 0] = e;
        dst[stride * 2 + 1] = f;
        dst[stride * 2 + 2] = g;
        dst[stride * 2 + 3] = h;
        dst[stride * 3 + 0] = avg3p(E[2], E[3], E[4]);
        dst[stride * 3 + 1] = avg3p(E[3], E[4], E[5]);
        dst[stride * 3 + 2] = avg3p(E[4], E[5], E[6]);
        dst[stride * 3 + 3] = avg3p(E[5], E[6], E[7]);
        break;
    }
    case B_VL_PRED: {
        int a = avg2p(E[5], E[6]);
        int b = avg2p(E[6], E[7]);
        int c = avg2p(E[7], E[8]);
        int d = avg2p(E[8], E[9]);
        int e = avg3p(E[5], E[6], E[7]);
        int f = avg3p(E[6], E[7], E[8]);
        int g = avg3p(E[7], E[8], E[9]);
        int h = avg3p(E[8], E[9], E[10]);
        dst[0] = a;
        dst[1] = b;
        dst[2] = c;
        dst[3] = d;
        dst[stride + 0] = e;
        dst[stride + 1] = f;
        dst[stride + 2] = g;
        dst[stride + 3] = h;
        dst[stride * 2 + 0] = avg3p(E[6], E[7], E[8]);
        dst[stride * 2 + 1] = avg3p(E[7], E[8], E[9]);
        dst[stride * 2 + 2] = avg3p(E[8], E[9], E[10]);
        dst[stride * 2 + 3] = avg3p(E[9], E[10], E[11]);
        dst[stride * 3 + 0] = avg3p(E[7], E[8], E[9]);
        dst[stride * 3 + 1] = avg3p(E[8], E[9], E[10]);
        dst[stride * 3 + 2] = avg3p(E[9], E[10], E[11]);
        dst[stride * 3 + 3] = avg3p(E[10], E[11], E[12]);
        break;
    }
    case B_HD_PRED: {
        int a = avg2p(E[3], E[4]);
        int b = avg2p(E[2], E[3]);
        int c = avg2p(E[1], E[2]);
        int d = avg2p(E[0], E[1]);
        int e = avg3p(E[4], E[3], E[2]);
        int f = avg3p(E[3], E[2], E[1]);
        int g = avg3p(E[2], E[1], E[0]);
        dst[0] = a;
        dst[1] = e;
        dst[2] = b;
        dst[3] = f;
        dst[stride + 0] = b;
        dst[stride + 1] = f;
        dst[stride + 2] = c;
        dst[stride + 3] = g;
        dst[stride * 2 + 0] = c;
        dst[stride * 2 + 1] = g;
        dst[stride * 2 + 2] = d;
        dst[stride * 2 + 3] = d;
        dst[stride * 3 + 0] = d;
        dst[stride * 3 + 1] = d;
        dst[stride * 3 + 2] = d;
        dst[stride * 3 + 3] = d;
        break;
    }
    case B_HU_PRED: {
        int a = avg2p(E[3], E[2]);
        int b = avg2p(E[2], E[1]);
        int c = avg2p(E[1], E[0]);
        int d = avg2p(E[0], E[0]);
        int e = avg3p(E[3], E[2], E[1]);
        int f = avg3p(E[2], E[1], E[0]);
        int g = avg3p(E[1], E[0], E[0]);
        dst[0] = e;
        dst[1] = a;
        dst[2] = b;
        dst[3] = c;
        dst[stride + 0] = f;
        dst[stride + 1] = b;
        dst[stride + 2] = c;
        dst[stride + 3] = d;
        dst[stride * 2 + 0] = g;
        dst[stride * 2 + 1] = c;
        dst[stride * 2 + 2] = d;
        dst[stride * 2 + 3] = d;
        dst[stride * 3 + 0] = d;
        dst[stride * 3 + 1] = d;
        dst[stride * 3 + 2] = d;
        dst[stride * 3 + 3] = d;
        break;
    }
    default:
        break;
    }
}

static void predict_16x16(uint8_t *dst, int stride, const uint8_t *above, const uint8_t *left,
                          int has_above, int has_left, uint8_t top_left, int mode) {
    if (mode == V_PRED) {
        for (int y = 0; y < 16; y++) {
            memcpy(dst + y * stride, above, 16);
        }
        return;
    }
    if (mode == H_PRED) {
        for (int y = 0; y < 16; y++) {
            memset(dst + y * stride, left[y], 16);
        }
        return;
    }
    if (mode == TM_PRED) {
        for (int y = 0; y < 16; y++) {
            for (int x = 0; x < 16; x++) {
                dst[y * stride + x] = clamp_u8(left[y] + above[x] - top_left);
            }
        }
        return;
    }

    int avg = 128;
    if (has_above && has_left) {
        int sum = 0;
        for (int i = 0; i < 16; i++) {
            sum += above[i] + left[i];
        }
        avg = (sum + 16) >> 5;
    } else if (has_above) {
        int sum = 0;
        for (int i = 0; i < 16; i++) {
            sum += above[i];
        }
        avg = (sum + 8) >> 4;
    } else if (has_left) {
        int sum = 0;
        for (int i = 0; i < 16; i++) {
            sum += left[i];
        }
        avg = (sum + 8) >> 4;
    }
    for (int y = 0; y < 16; y++) {
        memset(dst + y * stride, avg, 16);
    }
}

static void predict_8x8(uint8_t *dst, int stride, const uint8_t *above, const uint8_t *left,
                        int has_above, int has_left, uint8_t top_left, int mode) {
    if (mode == V_PRED) {
        for (int y = 0; y < 8; y++) {
            memcpy(dst + y * stride, above, 8);
        }
        return;
    }
    if (mode == H_PRED) {
        for (int y = 0; y < 8; y++) {
            memset(dst + y * stride, left[y], 8);
        }
        return;
    }
    if (mode == TM_PRED) {
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                dst[y * stride + x] = clamp_u8(left[y] + above[x] - top_left);
            }
        }
        return;
    }

    int avg = 128;
    if (has_above && has_left) {
        int sum = 0;
        for (int i = 0; i < 8; i++) {
            sum += above[i] + left[i];
        }
        avg = (sum + 8) >> 4;
    } else if (has_above) {
        int sum = 0;
        for (int i = 0; i < 8; i++) {
            sum += above[i];
        }
        avg = (sum + 4) >> 3;
    } else if (has_left) {
        int sum = 0;
        for (int i = 0; i < 8; i++) {
            sum += left[i];
        }
        avg = (sum + 4) >> 3;
    }
    for (int y = 0; y < 8; y++) {
        memset(dst + y * stride, avg, 8);
    }
}

static uint8_t get_top_left(const uint8_t *plane, int stride, int x, int y,
                             int width, int height) {
    if (y < 0) {
        return 127;
    }
    if (x < 0) {
        return 129;
    }
    if (x >= width) {
        x = width - 1;
    }
    if (y >= height) {
        y = height - 1;
    }
    return plane[y * stride + x];
}

static void get_above_row(const uint8_t *plane, int stride, int x, int y,
                          int width, int height, int count, uint8_t top_default, uint8_t *out) {
    if (y < 0) {
        for (int i = 0; i < count; i++) {
            out[i] = top_default;
        }
        return;
    }
    if (y >= height) {
        y = height - 1;
    }
    for (int i = 0; i < count; i++) {
        int xi = x + i;
        if (xi < 0) {
            out[i] = top_default;
        } else if (xi >= width) {
            out[i] = plane[y * stride + (width - 1)];
        } else {
            out[i] = plane[y * stride + xi];
        }
    }
}

static void get_left_col(const uint8_t *plane, int stride, int x, int y,
                         int width, int height, int count, uint8_t left_default, uint8_t *out) {
    if (x < 0) {
        for (int i = 0; i < count; i++) {
            out[i] = left_default;
        }
        return;
    }
    if (x >= width) {
        x = width - 1;
    }
    for (int i = 0; i < count; i++) {
        int yi = y + i;
        if (yi < 0) {
            out[i] = left_default;
        } else if (yi >= height) {
            out[i] = plane[(height - 1) * stride + x];
        } else {
            out[i] = plane[yi * stride + x];
        }
    }
}

static void dequant_block(int16_t *coeffs, int dc, int ac, int apply_dc) {
    if (apply_dc) {
        coeffs[0] = (int16_t)(coeffs[0] * dc);
    }
    for (int i = 1; i < 16; i++) {
        coeffs[i] = (int16_t)(coeffs[i] * ac);
    }
}

static void idct4x4(const int16_t *input, int16_t *output) {
    static const int cospi8sqrt2minus1 = 20091;
    static const int sinpi8sqrt2 = 35468;
    int32_t temp[16];

    for (int i = 0; i < 4; i++) {
        int a1 = input[0 + i] + input[8 + i];
        int b1 = input[0 + i] - input[8 + i];
        int t1 = (input[4 + i] * sinpi8sqrt2) >> 16;
        int t2 = input[12 + i] + ((input[12 + i] * cospi8sqrt2minus1) >> 16);
        int c1 = t1 - t2;
        t1 = input[4 + i] + ((input[4 + i] * cospi8sqrt2minus1) >> 16);
        t2 = (input[12 + i] * sinpi8sqrt2) >> 16;
        int d1 = t1 + t2;
        temp[0 + i] = a1 + d1;
        temp[12 + i] = a1 - d1;
        temp[4 + i] = b1 + c1;
        temp[8 + i] = b1 - c1;
    }

    for (int i = 0; i < 4; i++) {
        int a1 = temp[0 + 4 * i] + temp[2 + 4 * i];
        int b1 = temp[0 + 4 * i] - temp[2 + 4 * i];
        int t1 = (temp[1 + 4 * i] * sinpi8sqrt2) >> 16;
        int t2 = temp[3 + 4 * i] + ((temp[3 + 4 * i] * cospi8sqrt2minus1) >> 16);
        int c1 = t1 - t2;
        t1 = temp[1 + 4 * i] + ((temp[1 + 4 * i] * cospi8sqrt2minus1) >> 16);
        t2 = (temp[3 + 4 * i] * sinpi8sqrt2) >> 16;
        int d1 = t1 + t2;
        output[0 + 4 * i] = (int16_t)((a1 + d1 + 4) >> 3);
        output[3 + 4 * i] = (int16_t)((a1 - d1 + 4) >> 3);
        output[1 + 4 * i] = (int16_t)((b1 + c1 + 4) >> 3);
        output[2 + 4 * i] = (int16_t)((b1 - c1 + 4) >> 3);
    }
}

static void wht4x4(const int16_t *input, int16_t *output) {
    int32_t tmp[16];
    for (int i = 0; i < 4; i++) {
        int a1 = input[i] + input[12 + i];
        int b1 = input[4 + i] + input[8 + i];
        int c1 = input[4 + i] - input[8 + i];
        int d1 = input[i] - input[12 + i];
        tmp[i] = a1 + b1;
        tmp[4 + i] = c1 + d1;
        tmp[8 + i] = a1 - b1;
        tmp[12 + i] = d1 - c1;
    }
    for (int i = 0; i < 4; i++) {
        int a1 = tmp[4 * i] + tmp[4 * i + 3];
        int b1 = tmp[4 * i + 1] + tmp[4 * i + 2];
        int c1 = tmp[4 * i + 1] - tmp[4 * i + 2];
        int d1 = tmp[4 * i] - tmp[4 * i + 3];
        output[4 * i] = (int16_t)((a1 + b1 + 3) >> 3);
        output[4 * i + 1] = (int16_t)((c1 + d1 + 3) >> 3);
        output[4 * i + 2] = (int16_t)((a1 - b1 + 3) >> 3);
        output[4 * i + 3] = (int16_t)((d1 - c1 + 3) >> 3);
    }
}

static int clamp_level(int level) {
    if (level < 0) return 0;
    if (level > 63) return 63;
    return level;
}

static int filter_level_for_mb(int base_level, int seg_enabled, int seg_abs, const int seg_lf[4], int seg_id) {
    int level = base_level;
    if (seg_enabled) {
        if (seg_abs) {
            level = seg_lf[seg_id & 3];
        } else {
            level += seg_lf[seg_id & 3];
        }
    }
    return clamp_level(level);
}

static void filter_params(int level, int sharpness, int *limit, int *interior, int *hev) {
    int lim = level;
    int ilim = level;
    if (sharpness > 0) {
        lim >>= 1;
        ilim >>= 1;
    }
    if (sharpness > 4) {
        lim >>= 1;
        ilim >>= 1;
    }
    if (lim < 1) lim = 1;
    if (ilim < 1) ilim = 1;
    *limit = lim;
    *interior = ilim;
    *hev = (level >= 40) ? 2 : (level >= 20 ? 1 : 0);
}

static void filter_sample(uint8_t *p1, uint8_t *p0, uint8_t *q0, uint8_t *q1,
                          int limit, int interior, int hev_thr, int simple) {
    int P1 = *p1;
    int P0 = *p0;
    int Q0 = *q0;
    int Q1 = *q1;
    int mask = (abs(P0 - Q0) * 2 + abs(P1 - Q1) / 2 <= limit) &&
               (abs(P1 - P0) <= interior) &&
               (abs(Q1 - Q0) <= interior);
    if (!mask) {
        return;
    }
    int hev = (abs(P1 - P0) > hev_thr) || (abs(Q1 - Q0) > hev_thr);
    int delta = ((Q0 - P0) * 3 + (P1 - Q1) + 4) >> 3;
    if (delta > 127) delta = 127;
    if (delta < -128) delta = -128;
    P0 = clamp_u8(P0 + delta);
    Q0 = clamp_u8(Q0 - delta);
    if (!simple && !hev) {
        int delta2 = (P1 - Q1 + 1) >> 1;
        P1 = clamp_u8(P1 + delta2);
        Q1 = clamp_u8(Q1 - delta2);
    }
    *p1 = (uint8_t)P1;
    *p0 = (uint8_t)P0;
    *q0 = (uint8_t)Q0;
    *q1 = (uint8_t)Q1;
}

static void filter_edge_vertical(uint8_t *plane, int stride, int x, int y, int len,
                                 int limit, int interior, int hev, int simple, int width) {
    if (x < 2 || x + 1 >= width) {
        return;
    }
    for (int i = 0; i < len; i++) {
        uint8_t *row = plane + (y + i) * stride;
        filter_sample(&row[x - 2], &row[x - 1], &row[x], &row[x + 1], limit, interior, hev, simple);
    }
}

static void filter_edge_horizontal(uint8_t *plane, int stride, int x, int y, int len,
                                   int limit, int interior, int hev, int simple, int height) {
    if (y < 2 || y + 1 >= height) {
        return;
    }
    for (int i = 0; i < len; i++) {
        uint8_t *col = plane + (y - 2) * stride + (x + i);
        filter_sample(col, col + stride, col + 2 * stride, col + 3 * stride, limit, interior, hev, simple);
    }
}

static void loop_filter_plane(uint8_t *plane, int stride, int width, int height,
                              int mb_cols, int mb_rows, const uint8_t *seg_ids,
                              int base_level, int sharpness, int filter_type,
                              int seg_enabled, int seg_abs, const int seg_lf[4], int is_uv) {
    int mb_size = is_uv ? 8 : 16;
    int edge_step = 4;
    int simple = (filter_type != 0);

    for (int mb_y = 0; mb_y < mb_rows; mb_y++) {
        for (int mb_x = 0; mb_x < mb_cols; mb_x++) {
            size_t mb_index = (size_t)mb_y * (size_t)mb_cols + (size_t)mb_x;
            int level = filter_level_for_mb(base_level, seg_enabled, seg_abs, seg_lf, seg_ids[mb_index]);
            if (level == 0) {
                continue;
            }
            int limit = 0, interior = 0, hev = 0;
            filter_params(level, sharpness, &limit, &interior, &hev);

            int base_x = mb_x * mb_size;
            int base_y = mb_y * mb_size;
            int max_x = base_x + mb_size;
            int max_y = base_y + mb_size;
            if (max_x > width) max_x = width;
            if (max_y > height) max_y = height;
            int len_v = max_y - base_y;
            int len_h = max_x - base_x;

            if (base_x > 0) {
                int left_level = filter_level_for_mb(base_level, seg_enabled, seg_abs, seg_lf,
                                                     seg_ids[mb_index - 1]);
                int use_level = left_level > level ? left_level : level;
                int lim2 = 0, int2 = 0, hev2 = 0;
                filter_params(use_level, sharpness, &lim2, &int2, &hev2);
                filter_edge_vertical(plane, stride, base_x, base_y, len_v, lim2, int2, hev2, simple, width);
            }
            for (int x = base_x + edge_step; x < max_x; x += edge_step) {
                filter_edge_vertical(plane, stride, x, base_y, len_v, limit, interior, hev, simple, width);
            }

            if (base_y > 0) {
                int top_level = filter_level_for_mb(base_level, seg_enabled, seg_abs, seg_lf,
                                                    seg_ids[mb_index - mb_cols]);
                int use_level = top_level > level ? top_level : level;
                int lim2 = 0, int2 = 0, hev2 = 0;
                filter_params(use_level, sharpness, &lim2, &int2, &hev2);
                filter_edge_horizontal(plane, stride, base_x, base_y, len_h, lim2, int2, hev2, simple, height);
            }
            for (int y = base_y + edge_step; y < max_y; y += edge_step) {
                filter_edge_horizontal(plane, stride, base_x, y, len_h, limit, interior, hev, simple, height);
            }
        }
    }
}

typedef struct vp8_dequant {
    int y1_dc;
    int y1_ac;
    int y2_dc;
    int y2_ac;
    int uv_dc;
    int uv_ac;
} vp8_dequant;

static int clamp_q(int q) {
    if (q < 0) return 0;
    if (q > 127) return 127;
    return q;
}

static void dequant_init(vp8_dequant *dq, int qindex, int y1_dc_delta,
                         int y2_dc_delta, int y2_ac_delta,
                         int uv_dc_delta, int uv_ac_delta) {
    dq->y1_dc = cupidimage_vp8_dc_qlookup[clamp_q(qindex + y1_dc_delta)];
    dq->y1_ac = cupidimage_vp8_ac_qlookup[clamp_q(qindex)];
    dq->y2_dc = cupidimage_vp8_dc_qlookup[clamp_q(qindex + y2_dc_delta)] * 2;
    dq->y2_ac = (cupidimage_vp8_ac_qlookup[clamp_q(qindex + y2_ac_delta)] * 155) / 100;
    if (dq->y2_ac < 8) {
        dq->y2_ac = 8;
    }
    dq->uv_dc = cupidimage_vp8_dc_qlookup[clamp_q(qindex + uv_dc_delta)];
    dq->uv_ac = cupidimage_vp8_ac_qlookup[clamp_q(qindex + uv_ac_delta)];
}

static void decode_entropy_header(vp8_bool_decoder *br,
                                  uint8_t coeff_probs[4][8][3][11],
                                  int *coeff_skip_enabled,
                                  uint8_t *coeff_skip_prob) {
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 8; j++) {
            for (int k = 0; k < 3; k++) {
                for (int l = 0; l < 11; l++) {
                    if (vp8_br_read_bit(br, cupidimage_vp8_coeff_update_probs[i][j][k][l])) {
                        coeff_probs[i][j][k][l] = (uint8_t)vp8_br_read_bits(br, 8);
                    }
                }
            }
        }
    }
    *coeff_skip_enabled = vp8_br_read_bit(br, 128);
    if (*coeff_skip_enabled) {
        *coeff_skip_prob = (uint8_t)vp8_br_read_bits(br, 8);
    }
}

static int decode_block_coeffs(vp8_bool_decoder *br,
                               const uint8_t coeff_probs[4][8][3][11],
                               int block_type, int ctx, int start_coeff,
                               int16_t *coeffs) {
    int prev_zero = 0;
    int has_coeff = 0;
    int ctx3 = ctx;
    for (int i = 0; i < 16; i++) {
        coeffs[i] = 0;
    }
    for (int si = start_coeff; si < 16; si++) {
        const uint8_t *probs = coeff_probs[block_type][coeff_bands[si]][ctx3];
        int token = prev_zero ? vp8_read_tree_skip_eob(br, coeff_tree, probs)
                              : vp8_read_tree(br, coeff_tree, probs);
        if (token == DCT_EOB) {
            break;
        }

        int abs_val = 0;
        if (token == DCT_0) {
            abs_val = 0;
        } else if (token == DCT_1 || token == DCT_2 || token == DCT_3 || token == DCT_4) {
            abs_val = token;
        } else if (token == DCT_CAT1) {
            abs_val = 5 + vp8_br_read_bit(br, pcat1[0]);
        } else if (token == DCT_CAT2) {
            abs_val = 7 + ((vp8_br_read_bit(br, pcat2[0]) << 1) | vp8_br_read_bit(br, pcat2[1]));
        } else if (token == DCT_CAT3) {
            abs_val = 11 + ((vp8_br_read_bit(br, pcat3[0]) << 2) | (vp8_br_read_bit(br, pcat3[1]) << 1) |
                            vp8_br_read_bit(br, pcat3[2]));
        } else if (token == DCT_CAT4) {
            abs_val = 19 + ((vp8_br_read_bit(br, pcat4[0]) << 3) | (vp8_br_read_bit(br, pcat4[1]) << 2) |
                            (vp8_br_read_bit(br, pcat4[2]) << 1) | vp8_br_read_bit(br, pcat4[3]));
        } else if (token == DCT_CAT5) {
            abs_val = 35 + ((vp8_br_read_bit(br, pcat5[0]) << 4) | (vp8_br_read_bit(br, pcat5[1]) << 3) |
                            (vp8_br_read_bit(br, pcat5[2]) << 2) | (vp8_br_read_bit(br, pcat5[3]) << 1) |
                            vp8_br_read_bit(br, pcat5[4]));
        } else if (token == DCT_CAT6) {
            abs_val = 67 + ((vp8_br_read_bit(br, pcat6[0]) << 10) | (vp8_br_read_bit(br, pcat6[1]) << 9) |
                            (vp8_br_read_bit(br, pcat6[2]) << 8) | (vp8_br_read_bit(br, pcat6[3]) << 7) |
                            (vp8_br_read_bit(br, pcat6[4]) << 6) | (vp8_br_read_bit(br, pcat6[5]) << 5) |
                            (vp8_br_read_bit(br, pcat6[6]) << 4) | (vp8_br_read_bit(br, pcat6[7]) << 3) |
                            (vp8_br_read_bit(br, pcat6[8]) << 2) | (vp8_br_read_bit(br, pcat6[9]) << 1) |
                            vp8_br_read_bit(br, pcat6[10]));
        }

        if (abs_val) {
            int sign = vp8_br_read_bit(br, 128);
            if (sign) {
                abs_val = -abs_val;
            }
            coeffs[zigzag[si]] = (int16_t)abs_val;
            has_coeff = 1;
        }

        if (abs_val == 0) {
            ctx3 = 0;
        } else if (abs_val == 1 || abs_val == -1) {
            ctx3 = 1;
        } else {
            ctx3 = 2;
        }
        prev_zero = (abs_val == 0);
    }
    return has_coeff;
}

static void yuv_to_rgba(const uint8_t *y_plane, const uint8_t *u_plane, const uint8_t *v_plane,
                        const uint8_t *alpha, int width, int height, int uv_stride,
                        uint8_t *rgba) {
    for (int y = 0; y < height; y++) {
        int uv_y = (y >> 1) * uv_stride;
        for (int x = 0; x < width; x++) {
            int Y = y_plane[y * width + x];
            int U = u_plane[uv_y + (x >> 1)] - 128;
            int V = v_plane[uv_y + (x >> 1)] - 128;
            int R = Y + ((91881 * V) >> 16);
            int G = Y - ((22554 * U + 46802 * V) >> 16);
            int B = Y + ((116130 * U) >> 16);
            uint8_t A = alpha ? alpha[y * width + x] : 255;
            size_t idx = ((size_t)y * (size_t)width + (size_t)x) * 4;
            rgba[idx + 0] = clamp_u8(R);
            rgba[idx + 1] = clamp_u8(G);
            rgba[idx + 2] = clamp_u8(B);
            rgba[idx + 3] = A;
        }
    }
}

static int cupidimage_decode_vp8(const unsigned char *data, size_t size,
                                 const uint8_t *alpha, size_t alpha_size,
                                 cupidimage_image *out,
                                 char *err, size_t errcap) {
    if (size < 10) {
        set_err(err, errcap, "invalid VP8 frame");
        return 0;
    }

    uint16_t width = 0;
    uint16_t height = 0;
    size_t payload_off = 0;
    uint32_t first_part_size = 0;
    if (!parse_vp8_keyframe(data, size, &width, &height, &payload_off, &first_part_size)) {
        set_err(err, errcap, "unsupported VP8 frame");
        return 0;
    }

    size_t header_off = 10;
    if (header_off >= size) {
        set_err(err, errcap, "truncated VP8 frame");
        return 0;
    }
    if (header_off + first_part_size > size) {
        set_err(err, errcap, "truncated VP8 partition");
        return 0;
    }

    if (alpha && alpha_size < (size_t)width * (size_t)height) {
        set_err(err, errcap, "truncated ALPH data");
        return 0;
    }

    vp8_bool_decoder br;
    vp8_br_init(&br, data + header_off, first_part_size);

    (void)vp8_br_read_bit(&br, 128); /* colorspace */
    (void)vp8_br_read_bit(&br, 128); /* clamp */

    int seg_enabled = vp8_br_read_bit(&br, 128);
    int seg_update_map = 0;
    int seg_update_data = 0;
    int seg_abs = 0;
    int seg_q[4] = {0, 0, 0, 0};
    int seg_lf[4] = {0, 0, 0, 0};
    uint8_t seg_prob[3] = {255, 255, 255};

    if (seg_enabled) {
        seg_update_map = vp8_br_read_bit(&br, 128);
        seg_update_data = vp8_br_read_bit(&br, 128);
        if (seg_update_data) {
            seg_abs = vp8_br_read_bit(&br, 128);
            for (int i = 0; i < 4; i++) {
                if (vp8_br_read_bit(&br, 128)) {
                    seg_q[i] = vp8_br_read_signed(&br, 7);
                }
            }
            for (int i = 0; i < 4; i++) {
                if (vp8_br_read_bit(&br, 128)) {
                    seg_lf[i] = vp8_br_read_signed(&br, 6);
                }
            }
        }
        if (seg_update_map) {
            for (int i = 0; i < 3; i++) {
                if (vp8_br_read_bit(&br, 128)) {
                    seg_prob[i] = (uint8_t)vp8_br_read_bits(&br, 8);
                } else {
                    seg_prob[i] = 255;
                }
            }
        }
    }

    int filter_type = vp8_br_read_bit(&br, 128);
    int loop_filter_level = (int)vp8_br_read_bits(&br, 6);
    int sharpness = (int)vp8_br_read_bits(&br, 3);

    int loop_filter_delta_enabled = vp8_br_read_bit(&br, 128);
    if (loop_filter_delta_enabled) {
        int update = vp8_br_read_bit(&br, 128);
        if (update) {
            for (int i = 0; i < 4; i++) {
                if (vp8_br_read_bit(&br, 128)) {
                    (void)vp8_br_read_signed(&br, 6);
                }
            }
            for (int i = 0; i < 4; i++) {
                if (vp8_br_read_bit(&br, 128)) {
                    (void)vp8_br_read_signed(&br, 6);
                }
            }
        }
    }

    int num_partitions_log2 = (int)vp8_br_read_bits(&br, 2);
    if (num_partitions_log2 != 0) {
        set_err(err, errcap, "unsupported VP8 partitions");
        return 0;
    }

    int q_index = (int)vp8_br_read_bits(&br, 7);
    int y1_dc_delta = vp8_br_read_bit(&br, 128) ? vp8_br_read_signed(&br, 4) : 0;
    int y2_dc_delta = vp8_br_read_bit(&br, 128) ? vp8_br_read_signed(&br, 4) : 0;
    int y2_ac_delta = vp8_br_read_bit(&br, 128) ? vp8_br_read_signed(&br, 4) : 0;
    int uv_dc_delta = vp8_br_read_bit(&br, 128) ? vp8_br_read_signed(&br, 4) : 0;
    int uv_ac_delta = vp8_br_read_bit(&br, 128) ? vp8_br_read_signed(&br, 4) : 0;

    uint8_t coeff_probs[4][8][3][11];
    memcpy(coeff_probs, cupidimage_vp8_coeff_probs, sizeof(coeff_probs));
    int coeff_skip_enabled = 0;
    uint8_t coeff_skip_prob = 0;
    decode_entropy_header(&br, coeff_probs, &coeff_skip_enabled, &coeff_skip_prob);

    int mb_cols = (width + 15) / 16;
    int mb_rows = (height + 15) / 16;
    size_t mb_count = (size_t)mb_cols * (size_t)mb_rows;

    uint8_t *y_modes = (uint8_t *)malloc(mb_count);
    uint8_t *uv_modes = (uint8_t *)malloc(mb_count);
    uint8_t *skip_flags = (uint8_t *)malloc(mb_count);
    uint8_t *seg_ids = (uint8_t *)malloc(mb_count);
    uint8_t *b_modes = (uint8_t *)malloc(mb_count * 16);

    if (!y_modes || !uv_modes || !skip_flags || !seg_ids || !b_modes) {
        free(y_modes);
        free(uv_modes);
        free(skip_flags);
        free(seg_ids);
        free(b_modes);
        set_err(err, errcap, "out of memory");
        return 0;
    }

    uint8_t *above_bmode = (uint8_t *)malloc((size_t)mb_cols * 4);
    if (!above_bmode) {
        free(y_modes);
        free(uv_modes);
        free(skip_flags);
        free(seg_ids);
        free(b_modes);
        set_err(err, errcap, "out of memory");
        return 0;
    }
    memset(above_bmode, B_DC_PRED, (size_t)mb_cols * 4);

    for (int mb_y = 0; mb_y < mb_rows; mb_y++) {
        uint8_t left_bmode[4] = {B_DC_PRED, B_DC_PRED, B_DC_PRED, B_DC_PRED};
        for (int mb_x = 0; mb_x < mb_cols; mb_x++) {
            size_t mb_index = (size_t)mb_y * (size_t)mb_cols + (size_t)mb_x;
            int seg_id = 0;
            if (seg_enabled && seg_update_map) {
                seg_id = vp8_read_tree(&br, segment_tree, seg_prob);
            }
            seg_ids[mb_index] = (uint8_t)seg_id;

            int skip = 0;
            if (coeff_skip_enabled) {
                skip = vp8_br_read_bit(&br, coeff_skip_prob);
            }
            skip_flags[mb_index] = (uint8_t)skip;

            int y_mode = vp8_read_tree(&br, ymode_tree, kf_ymode_prob);
            y_modes[mb_index] = (uint8_t)y_mode;

            uint8_t *bmode_ptr = b_modes + mb_index * 16;
            if (y_mode == B_PRED) {
                for (int by = 0; by < 4; by++) {
                    for (int bx = 0; bx < 4; bx++) {
                        int idx = by * 4 + bx;
                        int above_mode = (by == 0) ? above_bmode[mb_x * 4 + bx] : bmode_ptr[idx - 4];
                        int left_mode = (bx == 0) ? left_bmode[by] : bmode_ptr[idx - 1];
                        int bmode = vp8_read_tree(&br, bmode_tree, cupidimage_vp8_kf_bmode_prob[above_mode][left_mode]);
                        bmode_ptr[idx] = (uint8_t)bmode;
                    }
                }
            } else {
                int fill_mode = B_DC_PRED;
                if (y_mode == V_PRED) fill_mode = B_VE_PRED;
                else if (y_mode == H_PRED) fill_mode = B_HE_PRED;
                else if (y_mode == TM_PRED) fill_mode = B_TM_PRED;
                for (int i = 0; i < 16; i++) {
                    bmode_ptr[i] = (uint8_t)fill_mode;
                }
            }

            for (int i = 0; i < 4; i++) {
                above_bmode[mb_x * 4 + i] = bmode_ptr[12 + i];
                left_bmode[i] = bmode_ptr[i * 4 + 3];
            }

            int uv_mode = vp8_read_tree(&br, uv_mode_tree, kf_uv_mode_prob);
            uv_modes[mb_index] = (uint8_t)uv_mode;
        }
    }

    (void)payload_off;
    (void)seg_update_data;
    (void)seg_lf;
    (void)loop_filter_delta_enabled;

    vp8_dequant dqf[4];
    for (int i = 0; i < 4; i++) {
        int q = q_index;
        if (seg_enabled) {
            if (seg_abs) {
                q = seg_q[i];
            } else {
                q += seg_q[i];
            }
        }
        dequant_init(&dqf[i], q, y1_dc_delta, y2_dc_delta, y2_ac_delta, uv_dc_delta, uv_ac_delta);
    }

    uint8_t *y_plane = (uint8_t *)malloc((size_t)width * (size_t)height);
    int uv_w = (width + 1) >> 1;
    int uv_h = (height + 1) >> 1;
    uint8_t *u_plane = (uint8_t *)malloc((size_t)uv_w * (size_t)uv_h);
    uint8_t *v_plane = (uint8_t *)malloc((size_t)uv_w * (size_t)uv_h);
    if (!y_plane || !u_plane || !v_plane) {
        free(y_modes);
        free(uv_modes);
        free(skip_flags);
        free(seg_ids);
        free(b_modes);
        free(above_bmode);
        free(y_plane);
        free(u_plane);
        free(v_plane);
        set_err(err, errcap, "out of memory");
        return 0;
    }
    memset(y_plane, 0, (size_t)width * (size_t)height);
    memset(u_plane, 128, (size_t)uv_w * (size_t)uv_h);
    memset(v_plane, 128, (size_t)uv_w * (size_t)uv_h);

    uint8_t *above_ctx = (uint8_t *)calloc((size_t)mb_cols * 9, 1);
    if (!above_ctx) {
        free(y_modes);
        free(uv_modes);
        free(skip_flags);
        free(seg_ids);
        free(b_modes);
        free(above_bmode);
        free(y_plane);
        free(u_plane);
        free(v_plane);
        set_err(err, errcap, "out of memory");
        return 0;
    }

    for (int mb_y = 0; mb_y < mb_rows; mb_y++) {
        uint8_t left_ctx[9] = {0};
        for (int mb_x = 0; mb_x < mb_cols; mb_x++) {
            size_t mb_index = (size_t)mb_y * (size_t)mb_cols + (size_t)mb_x;
            int has_y2 = (y_modes[mb_index] != B_PRED);
            const vp8_dequant *dq = &dqf[seg_ids[mb_index] & 3];
            int skip = skip_flags[mb_index];

            int16_t y2_coeffs[16];
            int16_t y2_out[16];
            if (has_y2 && !skip) {
                int block_idx = 24;
                int ctx = left_ctx[left_context_index[block_idx]] + above_ctx[mb_x * 9 + above_context_index[block_idx]];
                int nonzero = decode_block_coeffs(&br, coeff_probs, 1, ctx, 0, y2_coeffs);
                left_ctx[left_context_index[block_idx]] = (uint8_t)nonzero;
                above_ctx[mb_x * 9 + above_context_index[block_idx]] = (uint8_t)nonzero;
                dequant_block(y2_coeffs, dq->y2_dc, dq->y2_ac, 1);
                wht4x4(y2_coeffs, y2_out);
            } else {
                for (int i = 0; i < 16; i++) {
                    y2_out[i] = 0;
                }
                if (has_y2) {
                    int block_idx = 24;
                    left_ctx[left_context_index[block_idx]] = 0;
                    above_ctx[mb_x * 9 + above_context_index[block_idx]] = 0;
                }
            }

            uint8_t above_row[16];
            uint8_t left_col[16];
            uint8_t top_left = get_top_left(y_plane, width, mb_x * 16 - 1, mb_y * 16 - 1, width, height);
            get_above_row(y_plane, width, mb_x * 16, mb_y * 16 - 1, width, height, 16, 127, above_row);
            get_left_col(y_plane, width, mb_x * 16 - 1, mb_y * 16, width, height, 16, 129, left_col);

            uint8_t pred_y[16 * 16];
            if (y_modes[mb_index] != B_PRED) {
                predict_16x16(pred_y, 16, above_row, left_col, mb_y > 0, mb_x > 0, top_left, y_modes[mb_index]);
            }

            for (int by = 0; by < 4; by++) {
                for (int bx = 0; bx < 4; bx++) {
                    int block = by * 4 + bx;
                    int16_t coeffs[16];
                    int nonzero = 0;
                    if (!skip) {
                        int ctx = left_ctx[left_context_index[block]] + above_ctx[mb_x * 9 + above_context_index[block]];
                        int start_coeff = has_y2 ? 1 : 0;
                        int block_type = has_y2 ? 0 : 3;
                        nonzero = decode_block_coeffs(&br, coeff_probs, block_type, ctx, start_coeff, coeffs);
                        if (has_y2) {
                            coeffs[0] = y2_out[block];
                            dequant_block(coeffs, 1, dq->y1_ac, 0);
                        } else {
                            dequant_block(coeffs, dq->y1_dc, dq->y1_ac, 1);
                        }
                        left_ctx[left_context_index[block]] = (uint8_t)nonzero;
                        above_ctx[mb_x * 9 + above_context_index[block]] = (uint8_t)nonzero;
                    } else {
                        for (int i = 0; i < 16; i++) {
                            coeffs[i] = 0;
                        }
                        left_ctx[left_context_index[block]] = 0;
                        above_ctx[mb_x * 9 + above_context_index[block]] = 0;
                    }

                    uint8_t block_pred[16];
                    if (y_modes[mb_index] == B_PRED) {
                        uint8_t A[8];
                        uint8_t L[4];
                        int x = mb_x * 16 + bx * 4;
                        int y = mb_y * 16 + by * 4;
                        uint8_t p = get_top_left(y_plane, width, x - 1, y - 1, width, height);
                        get_above_row(y_plane, width, x, y - 1, width, height, 8, 127, A);
                        get_left_col(y_plane, width, x - 1, y, width, height, 4, 129, L);
                        subblock_intra_predict(block_pred, 4, A, L, p, b_modes[mb_index * 16 + block]);
                    } else {
                        for (int iy = 0; iy < 4; iy++) {
                            memcpy(block_pred + iy * 4, pred_y + (by * 4 + iy) * 16 + bx * 4, 4);
                        }
                    }

                    int16_t diff[16];
                    idct4x4(coeffs, diff);
                    int out_x = mb_x * 16 + bx * 4;
                    int out_y = mb_y * 16 + by * 4;
                    for (int iy = 0; iy < 4; iy++) {
                        if (out_y + iy >= height) {
                            continue;
                        }
                        for (int ix = 0; ix < 4; ix++) {
                            if (out_x + ix >= width) {
                                continue;
                            }
                            int val = block_pred[iy * 4 + ix] + diff[iy * 4 + ix];
                            y_plane[(out_y + iy) * width + (out_x + ix)] = clamp_u8(val);
                        }
                    }
                }
            }

            uint8_t above_u[8];
            uint8_t left_u[8];
            uint8_t above_v[8];
            uint8_t left_v[8];
            int uv_x = mb_x * 8;
            int uv_y = mb_y * 8;
            uint8_t uv_top_left_u = get_top_left(u_plane, uv_w, uv_x - 1, uv_y - 1, uv_w, uv_h);
            uint8_t uv_top_left_v = get_top_left(v_plane, uv_w, uv_x - 1, uv_y - 1, uv_w, uv_h);
            get_above_row(u_plane, uv_w, uv_x, uv_y - 1, uv_w, uv_h, 8, 127, above_u);
            get_left_col(u_plane, uv_w, uv_x - 1, uv_y, uv_w, uv_h, 8, 129, left_u);
            get_above_row(v_plane, uv_w, uv_x, uv_y - 1, uv_w, uv_h, 8, 127, above_v);
            get_left_col(v_plane, uv_w, uv_x - 1, uv_y, uv_w, uv_h, 8, 129, left_v);

            uint8_t pred_u[64];
            uint8_t pred_v[64];
            predict_8x8(pred_u, 8, above_u, left_u, mb_y > 0, mb_x > 0, uv_top_left_u, uv_modes[mb_index]);
            predict_8x8(pred_v, 8, above_v, left_v, mb_y > 0, mb_x > 0, uv_top_left_v, uv_modes[mb_index]);

            for (int uvb = 0; uvb < 4; uvb++) {
                int bx = uvb & 1;
                int by = uvb >> 1;
                int block_idx_u = 16 + uvb;
                int block_idx_v = 20 + uvb;

                int16_t coeffs_u[16];
                int16_t coeffs_v[16];
                int nonzero_u = 0;
                int nonzero_v = 0;
                if (!skip) {
                    int ctx_u = left_ctx[left_context_index[block_idx_u]] + above_ctx[mb_x * 9 + above_context_index[block_idx_u]];
                    nonzero_u = decode_block_coeffs(&br, coeff_probs, 2, ctx_u, 0, coeffs_u);
                    dequant_block(coeffs_u, dq->uv_dc, dq->uv_ac, 1);
                    left_ctx[left_context_index[block_idx_u]] = (uint8_t)nonzero_u;
                    above_ctx[mb_x * 9 + above_context_index[block_idx_u]] = (uint8_t)nonzero_u;

                    int ctx_v = left_ctx[left_context_index[block_idx_v]] + above_ctx[mb_x * 9 + above_context_index[block_idx_v]];
                    nonzero_v = decode_block_coeffs(&br, coeff_probs, 2, ctx_v, 0, coeffs_v);
                    dequant_block(coeffs_v, dq->uv_dc, dq->uv_ac, 1);
                    left_ctx[left_context_index[block_idx_v]] = (uint8_t)nonzero_v;
                    above_ctx[mb_x * 9 + above_context_index[block_idx_v]] = (uint8_t)nonzero_v;
                } else {
                    for (int i = 0; i < 16; i++) {
                        coeffs_u[i] = 0;
                        coeffs_v[i] = 0;
                    }
                    left_ctx[left_context_index[block_idx_u]] = 0;
                    above_ctx[mb_x * 9 + above_context_index[block_idx_u]] = 0;
                    left_ctx[left_context_index[block_idx_v]] = 0;
                    above_ctx[mb_x * 9 + above_context_index[block_idx_v]] = 0;
                }

                int16_t diff_u[16];
                int16_t diff_v[16];
                idct4x4(coeffs_u, diff_u);
                idct4x4(coeffs_v, diff_v);

                int out_x = uv_x + bx * 4;
                int out_y = uv_y + by * 4;
                for (int iy = 0; iy < 4; iy++) {
                    if (out_y + iy >= uv_h) {
                        continue;
                    }
                    for (int ix = 0; ix < 4; ix++) {
                        if (out_x + ix >= uv_w) {
                            continue;
                        }
                        int pred_idx = (by * 4 + iy) * 8 + bx * 4 + ix;
                        int val_u = pred_u[pred_idx] + diff_u[iy * 4 + ix];
                        int val_v = pred_v[pred_idx] + diff_v[iy * 4 + ix];
                        u_plane[(out_y + iy) * uv_w + (out_x + ix)] = clamp_u8(val_u);
                        v_plane[(out_y + iy) * uv_w + (out_x + ix)] = clamp_u8(val_v);
                    }
                }
            }
        }
    }

    out->width = width;
    out->height = height;
    out->rgba = (uint8_t *)malloc((size_t)width * (size_t)height * 4u);
    if (!out->rgba) {
        free(y_modes);
        free(uv_modes);
        free(skip_flags);
        free(seg_ids);
        free(b_modes);
        free(above_bmode);
        free(above_ctx);
        free(y_plane);
        free(u_plane);
        free(v_plane);
        set_err(err, errcap, "out of memory");
        return 0;
    }

    if (loop_filter_level > 0) {
        loop_filter_plane(y_plane, width, width, height, mb_cols, mb_rows, seg_ids,
                          loop_filter_level, sharpness, filter_type,
                          seg_enabled, seg_abs, seg_lf, 0);
        loop_filter_plane(u_plane, uv_w, uv_w, uv_h, mb_cols, mb_rows, seg_ids,
                          loop_filter_level, sharpness, filter_type,
                          seg_enabled, seg_abs, seg_lf, 1);
        loop_filter_plane(v_plane, uv_w, uv_w, uv_h, mb_cols, mb_rows, seg_ids,
                          loop_filter_level, sharpness, filter_type,
                          seg_enabled, seg_abs, seg_lf, 1);
    }

    yuv_to_rgba(y_plane, u_plane, v_plane, alpha, width, height, uv_w, out->rgba);

    free(y_modes);
    free(uv_modes);
    free(skip_flags);
    free(seg_ids);
    free(b_modes);
    free(above_bmode);
    free(above_ctx);
    free(y_plane);
    free(u_plane);
    free(v_plane);
    return 1;
}

int cupidimage_load_webp(const unsigned char *data, size_t size, cupidimage_image *out,
                         char *err, size_t errcap) {
    if (!data || !out) {
        set_err(err, errcap, "invalid arguments");
        return 0;
    }
    memset(out, 0, sizeof(*out));

    if (size < 12 || memcmp(data, "RIFF", 4) != 0 || memcmp(data + 8, "WEBP", 4) != 0) {
        set_err(err, errcap, "not a WebP");
        return 0;
    }

    const unsigned char *vp8 = NULL;
    size_t vp8_size = 0;
    const unsigned char *vp8l = NULL;
    size_t vp8l_size = 0;
    const unsigned char *alph = NULL;
    size_t alph_size = 0;
    int has_vp8x = 0;
    uint8_t vp8x_flags = 0;
    uint32_t vp8x_width = 0;
    uint32_t vp8x_height = 0;

    size_t off = 12;
    while (off + 8 <= size) {
        const unsigned char *chunk = data + off;
        uint32_t csize = read_le32(chunk + 4);
        off += 8;
        if (off + csize > size) {
            set_err(err, errcap, "truncated WebP");
            return 0;
        }
        if (memcmp(chunk, "VP8X", 4) == 0) {
            if (csize < 10) {
                set_err(err, errcap, "invalid VP8X chunk");
                return 0;
            }
            has_vp8x = 1;
            vp8x_flags = data[off];
            vp8x_width = read_le24(data + off + 4) + 1;
            vp8x_height = read_le24(data + off + 7) + 1;
        } else if (memcmp(chunk, "VP8 ", 4) == 0) {
            vp8 = data + off;
            vp8_size = csize;
        } else if (memcmp(chunk, "VP8L", 4) == 0) {
            vp8l = data + off;
            vp8l_size = csize;
        } else if (memcmp(chunk, "ALPH", 4) == 0) {
            alph = data + off;
            alph_size = csize;
        }
        off += csize + (csize & 1u);
    }

    if (has_vp8x && (vp8x_flags & 0x02)) {
        set_err(err, errcap, "animated WebP not supported");
        return 0;
    }

    if (vp8l && vp8l_size > 0) {
        return cupidimage_decode_vp8l(vp8l, vp8l_size, out, err, errcap);
    }

    if (!vp8 || vp8_size < 10) {
        set_err(err, errcap, "missing VP8 chunk");
        return 0;
    }

    uint16_t width = 0;
    uint16_t height = 0;
    size_t payload_off = 0;
    if (!parse_vp8_keyframe(vp8, vp8_size, &width, &height, &payload_off, NULL)) {
        set_err(err, errcap, "unsupported VP8 frame");
        return 0;
    }
    (void)payload_off;

    const uint8_t *alpha_plane = NULL;
    size_t alpha_plane_size = 0;
    if (alph && alph_size > 0) {
        if (alph_size < 1) {
            set_err(err, errcap, "invalid ALPH chunk");
            return 0;
        }
        uint8_t alph_header = alph[0];
        int method = alph_header & 0x03;
        int filter = (alph_header >> 2) & 0x03;
        int preproc = (alph_header >> 4) & 0x03;
        if (method != 0 || filter != 0 || preproc != 0) {
            set_err(err, errcap, "unsupported ALPH compression");
            return 0;
        }
        alpha_plane = alph + 1;
        alpha_plane_size = alph_size - 1;
        if (alpha_plane_size < (size_t)width * (size_t)height) {
            set_err(err, errcap, "truncated ALPH data");
            return 0;
        }
    }

    (void)vp8x_width;
    (void)vp8x_height;
    return cupidimage_decode_vp8(vp8, vp8_size, alpha_plane, alpha_plane_size, out, err, errcap);
}

int cupidimage_load_webp_file(const char *path, cupidimage_image *out,
                              char *err, size_t errcap) {
    if (!path || !out) {
        set_err(err, errcap, "invalid arguments");
        return 0;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        set_err(err, errcap, "failed to open file");
        return 0;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        set_err(err, errcap, "failed to seek file");
        return 0;
    }
    long fsize = ftell(f);
    if (fsize <= 0) {
        fclose(f);
        set_err(err, errcap, "empty file");
        return 0;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        set_err(err, errcap, "failed to seek file");
        return 0;
    }

    unsigned char *buf = (unsigned char *)malloc((size_t)fsize);
    if (!buf) {
        fclose(f);
        set_err(err, errcap, "out of memory");
        return 0;
    }

    size_t nread = fread(buf, 1, (size_t)fsize, f);
    fclose(f);
    if (nread != (size_t)fsize) {
        free(buf);
        set_err(err, errcap, "failed to read file");
        return 0;
    }

    int ok = cupidimage_load_webp(buf, (size_t)fsize, out, err, errcap);
    free(buf);
    return ok;
}
