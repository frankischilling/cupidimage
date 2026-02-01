#include "cupidimage.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef CUPIDIMAGE_JPEG_DEBUG
#define JPEG_DEBUG(...) fprintf(stderr, __VA_ARGS__)
#else
#define JPEG_DEBUG(...) do { } while (0)
#endif

static void set_err(char *err, size_t errcap, const char *msg) {
    if (err && errcap) {
        snprintf(err, errcap, "%s", msg);
    }
}

static uint16_t read_be16(const unsigned char *p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

typedef struct jpeg_huff {
    uint8_t bits[16];
    uint8_t huffval[256];
    int mincode[17];
    int maxcode[18];
    int valptr[17];
    int num;
    int valid;
} jpeg_huff;

typedef struct jpeg_comp {
    int id;
    int h;
    int v;
    int tq;
    int dc_table;
    int ac_table;
    int dc_pred;
    int blocks_w;
    int blocks_h;
    int32_t *coeffs;
} jpeg_comp;

typedef struct jpeg_bitstream {
    const uint8_t *data;
    size_t size;
    size_t pos;
    uint32_t bitbuf;
    int bitcount;
    int marker;
} jpeg_bitstream;

static const uint8_t zigzag[64] = {
    0, 1, 8, 16, 9, 2, 3, 10,
    17, 24, 32, 25, 18, 11, 4, 5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

static const uint8_t std_dc_luminance_bits[16] = {
    0x00, 0x01, 0x05, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const uint8_t std_dc_luminance_val[12] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
    0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B
};

static const uint8_t std_ac_luminance_bits[16] = {
    0x00, 0x02, 0x01, 0x03, 0x03, 0x02, 0x04, 0x03,
    0x05, 0x05, 0x04, 0x04, 0x00, 0x00, 0x01, 0x7D
};

static const uint8_t std_ac_luminance_val[162] = {
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12,
    0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
    0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xA1, 0x08,
    0x23, 0x42, 0xB1, 0xC1, 0x15, 0x52, 0xD1, 0xF0,
    0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0A, 0x16,
    0x17, 0x18, 0x19, 0x1A, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2A, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
    0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
    0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
    0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
    0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
    0x7A, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
    0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
    0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
    0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6,
    0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5,
    0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4,
    0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE1, 0xE2,
    0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA,
    0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8,
    0xF9, 0xFA
};

static const uint8_t std_dc_chrominance_bits[16] = {
    0x00, 0x03, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const uint8_t std_dc_chrominance_val[12] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
    0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B
};

static const uint8_t std_ac_chrominance_bits[16] = {
    0x00, 0x02, 0x01, 0x02, 0x04, 0x04, 0x03, 0x04,
    0x07, 0x05, 0x04, 0x04, 0x00, 0x01, 0x02, 0x77
};

static const uint8_t std_ac_chrominance_val[162] = {
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21,
    0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
    0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
    0xA1, 0xB1, 0xC1, 0x09, 0x23, 0x33, 0x52, 0xF0,
    0x15, 0x62, 0x72, 0xD1, 0x0A, 0x16, 0x24, 0x34,
    0xE1, 0x25, 0xF1, 0x17, 0x18, 0x19, 0x1A, 0x26,
    0x27, 0x28, 0x29, 0x2A, 0x35, 0x36, 0x37, 0x38,
    0x39, 0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
    0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
    0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
    0x69, 0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
    0x79, 0x7A, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96,
    0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5,
    0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4,
    0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3,
    0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2,
    0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA,
    0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9,
    0xEA, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8,
    0xF9, 0xFA
};

#define IDCT_BITS 14
#define IDCT_SHIFT (IDCT_BITS * 2 + 2)

static const int16_t idct_tab[8][8] = {
    { 11585,  11585,  11585,  11585,  11585,  11585,  11585,  11585},
    { 16069,  13623,   9102,   3196,  -3196,  -9102, -13623, -16069},
    { 15137,   6270,  -6270, -15137, -15137,  -6270,   6270,  15137},
    { 13623,  -3196, -16069,  -9102,   9102,  16069,   3196, -13623},
    { 11585, -11585, -11585,  11585,  11585, -11585, -11585,  11585},
    {  9102, -16069,   3196,  13623, -13623,  -3196,  16069,  -9102},
    {  6270, -15137,  15137,  -6270,  -6270,  15137, -15137,   6270},
    {  3196,  -9102,  13623, -16069,  16069, -13623,   9102,  -3196}
};

static void idct_8x8(const int32_t *block, uint8_t *out, int stride) {
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            int64_t sum = 0;
            for (int v = 0; v < 8; v++) {
                const int16_t tv = idct_tab[v][y];
                for (int u = 0; u < 8; u++) {
                    sum += (int64_t)block[v * 8 + u] * (int64_t)idct_tab[u][x] * (int64_t)tv;
                }
            }
            int val = (int)((sum + ((int64_t)1 << (IDCT_SHIFT - 1))) >> IDCT_SHIFT);
            val += 128;
            if (val < 0) val = 0;
            if (val > 255) val = 255;
            out[y * stride + x] = (uint8_t)val;
        }
    }
}

static void huff_build(jpeg_huff *h, const uint8_t *bits, const uint8_t *vals, int num) {
    memcpy(h->bits, bits, 16);
    memcpy(h->huffval, vals, (size_t)num);
    h->num = num;

    int code = 0;
    int k = 0;
    for (int i = 1; i <= 16; i++) {
        int count = bits[i - 1];
        if (count == 0) {
            h->mincode[i] = -1;
            h->maxcode[i] = -1;
        } else {
            h->mincode[i] = code;
            h->valptr[i] = k;
            code += count;
            h->maxcode[i] = code - 1;
            k += count;
        }
        code <<= 1;
    }
    h->maxcode[17] = 0xFFFF;
    h->valid = 1;
}

static void jpeg_apply_default_huffman(jpeg_huff dc_tables[4], jpeg_huff ac_tables[4]) {
    if (!dc_tables[0].valid) {
        huff_build(&dc_tables[0], std_dc_luminance_bits, std_dc_luminance_val,
                   (int)(sizeof(std_dc_luminance_val) / sizeof(std_dc_luminance_val[0])));
    }
    if (!ac_tables[0].valid) {
        huff_build(&ac_tables[0], std_ac_luminance_bits, std_ac_luminance_val,
                   (int)(sizeof(std_ac_luminance_val) / sizeof(std_ac_luminance_val[0])));
    }
    if (!dc_tables[1].valid) {
        huff_build(&dc_tables[1], std_dc_chrominance_bits, std_dc_chrominance_val,
                   (int)(sizeof(std_dc_chrominance_val) / sizeof(std_dc_chrominance_val[0])));
    }
    if (!ac_tables[1].valid) {
        huff_build(&ac_tables[1], std_ac_chrominance_bits, std_ac_chrominance_val,
                   (int)(sizeof(std_ac_chrominance_val) / sizeof(std_ac_chrominance_val[0])));
    }
}

static void jpeg_free_coeffs(jpeg_comp *comps, int num_comp) {
    for (int i = 0; i < num_comp; i++) {
        free(comps[i].coeffs);
        comps[i].coeffs = NULL;
    }
}

static int bs_read_byte_raw(jpeg_bitstream *bs) {
    if (bs->pos >= bs->size) {
        return -1;
    }
    return bs->data[bs->pos++];
}

static int bs_read_byte_stuffed(jpeg_bitstream *bs) {
    if (bs->pos >= bs->size) {
        return -1;
    }
    int b = bs->data[bs->pos++];
    if (b != 0xFF) {
        return b;
    }
    if (bs->pos >= bs->size) {
        return -1;
    }
    int b2 = bs->data[bs->pos++];
    while (b2 == 0xFF) {
        if (bs->pos >= bs->size) {
            return -1;
        }
        b2 = bs->data[bs->pos++];
    }
    if (b2 == 0x00) {
        return 0xFF;
    }
    bs->marker = b2;
    return -1;
}

static int bs_read_bit(jpeg_bitstream *bs) {
    if (bs->bitcount == 0) {
        int byte = bs_read_byte_stuffed(bs);
        if (byte < 0) {
            return -1;
        }
        bs->bitbuf = (uint32_t)byte;
        bs->bitcount = 8;
    }
    int bit = (bs->bitbuf >> 7) & 1u;
    bs->bitbuf <<= 1;
    bs->bitcount--;
    return bit;
}

static int bs_read_bits(jpeg_bitstream *bs, int n) {
    int val = 0;
    for (int i = 0; i < n; i++) {
        int bit = bs_read_bit(bs);
        if (bit < 0) {
            return -1;
        }
        val = (val << 1) | bit;
    }
    return val;
}

static int huff_decode(jpeg_bitstream *bs, const jpeg_huff *h, int *sym) {
    int code = 0;
    for (int i = 1; i <= 16; i++) {
        int bit = bs_read_bit(bs);
        if (bit < 0) {
            return 0;
        }
        code = (code << 1) | bit;
        if (h->maxcode[i] >= 0 && code <= h->maxcode[i]) {
            int idx = h->valptr[i] + (code - h->mincode[i]);
            if (idx < 0 || idx >= h->num) {
                return 0;
            }
            *sym = h->huffval[idx];
            return 1;
        }
    }
    return 0;
}

static int receive_extend(jpeg_bitstream *bs, int s, int *ok) {
    if (s == 0) {
        *ok = 1;
        return 0;
    }
    int v = bs_read_bits(bs, s);
    if (v < 0) {
        *ok = 0;
        return 0;
    }
    int vt = 1 << (s - 1);
    if (v < vt) {
        v -= (1 << s) - 1;
    }
    *ok = 1;
    return v;
}

static int decode_block(jpeg_bitstream *bs, const jpeg_huff *dc, const jpeg_huff *ac,
                        const int *qt, int *dc_pred, int32_t *block) {
    for (int i = 0; i < 64; i++) {
        block[i] = 0;
    }

    int sym = 0;
    if (!huff_decode(bs, dc, &sym)) {
        return 0;
    }
    int ok = 0;
    int diff = receive_extend(bs, sym, &ok);
    if (!ok) {
        return 0;
    }
    *dc_pred += diff;
    block[0] = (int32_t)(*dc_pred) * (int32_t)qt[0];

    int k = 1;
    while (k < 64) {
        if (!huff_decode(bs, ac, &sym)) {
            return 0;
        }
        if (sym == 0) {
            break;
        }
        if (sym == 0xF0) {
            k += 16;
            continue;
        }
        int run = sym >> 4;
        int size = sym & 0x0F;
        k += run;
        if (k >= 64) {
            return 0;
        }
        int coeff = receive_extend(bs, size, &ok);
        if (!ok) {
            return 0;
        }
        block[zigzag[k]] = (int32_t)coeff * (int32_t)qt[zigzag[k]];
        k++;
    }

    return 1;
}

static int consume_restart(jpeg_bitstream *bs, int expected) {
    bs->bitcount = 0;
    bs->bitbuf = 0;

    int b = 0;
    do {
        b = bs_read_byte_raw(bs);
        if (b < 0) {
            return 0;
        }
    } while (b != 0xFF);

    do {
        b = bs_read_byte_raw(bs);
        if (b < 0) {
            return 0;
        }
    } while (b == 0xFF);

    if (b < 0xD0 || b > 0xD7) {
        return 0;
    }
    if (expected >= 0 && b != expected) {
        return 0;
    }
    return 1;
}

static int decode_dc_first(jpeg_bitstream *bs, jpeg_comp *c, const jpeg_huff *dc,
                           int al, int32_t *block) {
    int sym = 0;
    if (!huff_decode(bs, dc, &sym)) {
        return 0;
    }
    int ok = 0;
    int diff = receive_extend(bs, sym, &ok);
    if (!ok) {
        return 0;
    }
    c->dc_pred += diff;
    block[0] = (int32_t)(c->dc_pred << al);
    return 1;
}

static int decode_dc_refine(jpeg_bitstream *bs, int al, int32_t *block) {
    int bit = bs_read_bit(bs);
    if (bit < 0) {
        return 0;
    }
    if (bit) {
        int32_t add = (int32_t)1 << al;
        if (block[0] >= 0) {
            block[0] += add;
        } else {
            block[0] -= add;
        }
    }
    return 1;
}

static int decode_ac_first(jpeg_bitstream *bs, const jpeg_huff *ac,
                           int ss, int se, int al, int32_t *block,
                           int *eobrun) {
    int k = ss;
    if (*eobrun > 0) {
        (*eobrun)--;
        return 1;
    }
    while (k <= se) {
        int sym = 0;
        if (!huff_decode(bs, ac, &sym)) {
            return 0;
        }
        int run = sym >> 4;
        int size = sym & 0x0F;
        if (size == 0) {
            if (run == 15) {
                k += 16;
                continue;
            }
            int extra = 0;
            if (run > 0) {
                extra = bs_read_bits(bs, run);
                if (extra < 0) {
                    return 0;
                }
            }
            *eobrun = (1 << run) + extra - 1;
            break;
        }
        k += run;
        if (k > se) {
            return 0;
        }
        int ok = 0;
        int coeff = receive_extend(bs, size, &ok);
        if (!ok) {
            return 0;
        }
        block[zigzag[k]] = (int32_t)coeff << al;
        k++;
    }
    return 1;
}

static int refine_coeff(jpeg_bitstream *bs, int32_t *coef, int p1) {
    int bit = bs_read_bit(bs);
    if (bit < 0) {
        return 0;
    }
    if (bit) {
        *coef += (*coef >= 0) ? p1 : -p1;
    }
    return 1;
}

static int decode_ac_refine(jpeg_bitstream *bs, const jpeg_huff *ac,
                            int ss, int se, int al, int32_t *block,
                            int *eobrun) {
    int p1 = 1 << al;
    int p2 = -p1;

    int k = ss;
    if (*eobrun > 0) {
        for (; k <= se; k++) {
            int idx = zigzag[k];
            if (block[idx] != 0) {
                if (!refine_coeff(bs, &block[idx], p1)) {
                    return 0;
                }
            }
        }
        (*eobrun)--;
        return 1;
    }

    while (k <= se) {
        int sym = 0;
        if (!huff_decode(bs, ac, &sym)) {
            return 0;
        }
        int run = sym >> 4;
        int size = sym & 0x0F;
        if (size == 0) {
            if (run == 15) {
                for (int i = 0; i < 16 && k <= se; i++, k++) {
                    int idx = zigzag[k];
                    if (block[idx] != 0) {
                        if (!refine_coeff(bs, &block[idx], p1)) {
                            return 0;
                        }
                    }
                }
                continue;
            }
            int extra = 0;
            if (run > 0) {
                extra = bs_read_bits(bs, run);
                if (extra < 0) {
                    return 0;
                }
            }
            *eobrun = (1 << run) + extra - 1;
            for (; k <= se; k++) {
                int idx = zigzag[k];
                if (block[idx] != 0) {
                    if (!refine_coeff(bs, &block[idx], p1)) {
                        return 0;
                    }
                }
            }
            break;
        }
        if (size != 1) {
            return 0;
        }
        int bit = bs_read_bit(bs);
        if (bit < 0) {
            return 0;
        }
        int32_t newcoef = bit ? p1 : p2;
        while (k <= se) {
            int idx = zigzag[k];
            if (block[idx] != 0) {
                if (!refine_coeff(bs, &block[idx], p1)) {
                    return 0;
                }
            } else if (run == 0) {
                block[idx] = newcoef;
                k++;
                break;
            } else {
                run--;
            }
            k++;
        }
    }
    return 1;
}

static int jpeg_decode_progressive_scan(const uint8_t *data, size_t size,
                                        jpeg_comp *comps, int num_comp,
                                        const int *scan_comp, int scan_count,
                                        int ss, int se, int ah, int al,
                                        int mcu_cols, int mcu_rows,
                                        int hmax, int vmax,
                                        const jpeg_huff dc_tables[4],
                                        const jpeg_huff ac_tables[4],
                                        int restart_interval,
                                        size_t *consumed,
                                        char *err, size_t errcap) {
    (void)hmax;
    (void)vmax;
    jpeg_bitstream bs;
    bs.data = data;
    bs.size = size;
    bs.pos = 0;
    bs.bitbuf = 0;
    bs.bitcount = 0;
    bs.marker = 0;

    for (int i = 0; i < scan_count; i++) {
        int ci = scan_comp[i];
        if (ci < 0 || ci >= num_comp) {
            set_err(err, errcap, "invalid scan component");
            return 0;
        }
        if (comps[ci].dc_table < 0 || comps[ci].dc_table > 3 ||
            comps[ci].ac_table < 0 || comps[ci].ac_table > 3) {
            set_err(err, errcap, "invalid huffman table");
            return 0;
        }
        if (ss == 0 && se == 0) {
            if (!dc_tables[comps[ci].dc_table].valid) {
                set_err(err, errcap, "missing huffman table");
                return 0;
            }
        } else {
            if (!ac_tables[comps[ci].ac_table].valid) {
                set_err(err, errcap, "missing huffman table");
                return 0;
            }
        }
    }
    if (ss == 0 && ah == 0) {
        for (int i = 0; i < scan_count; i++) {
            comps[scan_comp[i]].dc_pred = 0;
        }
    }

    int eobrun = 0;
    int mcu_count = 0;
    int rst_index = 0;

    if (scan_count == 1) {
        jpeg_comp *c = &comps[scan_comp[0]];
        for (int by = 0; by < c->blocks_h; by++) {
            for (int bx = 0; bx < c->blocks_w; bx++) {
                int32_t *block = c->coeffs + ((by * c->blocks_w + bx) * 64);
                if (ss == 0 && se == 0) {
                    if (ah == 0) {
                        if (!decode_dc_first(&bs, c, &dc_tables[c->dc_table], al, block)) {
                            set_err(err, errcap, "jpeg decode failed");
                            return 0;
                        }
                    } else {
                        if (!decode_dc_refine(&bs, al, block)) {
                            set_err(err, errcap, "jpeg decode failed");
                            return 0;
                        }
                    }
                } else {
                    if (ah == 0) {
                        if (!decode_ac_first(&bs, &ac_tables[c->ac_table],
                                             ss, se, al, block, &eobrun)) {
                            set_err(err, errcap, "jpeg decode failed");
                            return 0;
                        }
                    } else {
                        if (!decode_ac_refine(&bs, &ac_tables[c->ac_table],
                                              ss, se, al, block, &eobrun)) {
                            set_err(err, errcap, "jpeg decode failed");
                            return 0;
                        }
                    }
                }

                mcu_count++;
                if (restart_interval > 0 && (mcu_count % restart_interval) == 0) {
                    int expected = 0xD0 + (rst_index & 7);
                    if (!consume_restart(&bs, expected)) {
                        set_err(err, errcap, "jpeg restart marker error");
                        return 0;
                    }
                    rst_index++;
                    eobrun = 0;
                    for (int i = 0; i < num_comp; i++) {
                        comps[i].dc_pred = 0;
                    }
                }
            }
        }
    } else {
        for (int my = 0; my < mcu_rows; my++) {
            for (int mx = 0; mx < mcu_cols; mx++) {
                for (int si = 0; si < scan_count; si++) {
                    jpeg_comp *c = &comps[scan_comp[si]];
                    for (int by = 0; by < c->v; by++) {
                        for (int bx = 0; bx < c->h; bx++) {
                            int block_x = mx * c->h + bx;
                            int block_y = my * c->v + by;
                            int32_t *block = c->coeffs +
                                ((block_y * c->blocks_w + block_x) * 64);
                            if (ss == 0 && se == 0) {
                                if (ah == 0) {
                                    if (!decode_dc_first(&bs, c, &dc_tables[c->dc_table], al, block)) {
                                        set_err(err, errcap, "jpeg decode failed");
                                        return 0;
                                    }
                                } else {
                                    if (!decode_dc_refine(&bs, al, block)) {
                                        set_err(err, errcap, "jpeg decode failed");
                                        return 0;
                                    }
                                }
                            } else {
                                if (ah == 0) {
                                    if (!decode_ac_first(&bs, &ac_tables[c->ac_table],
                                                         ss, se, al, block, &eobrun)) {
                                        set_err(err, errcap, "jpeg decode failed");
                                        return 0;
                                    }
                                } else {
                                    if (!decode_ac_refine(&bs, &ac_tables[c->ac_table],
                                                          ss, se, al, block, &eobrun)) {
                                        set_err(err, errcap, "jpeg decode failed");
                                        return 0;
                                    }
                                }
                            }
                        }
                    }
                }

                mcu_count++;
                if (restart_interval > 0 && (mcu_count % restart_interval) == 0) {
                    int expected = 0xD0 + (rst_index & 7);
                    if (!consume_restart(&bs, expected)) {
                        set_err(err, errcap, "jpeg restart marker error");
                        return 0;
                    }
                    rst_index++;
                    eobrun = 0;
                    for (int i = 0; i < num_comp; i++) {
                        comps[i].dc_pred = 0;
                    }
                }
            }
        }
    }

    if (bs.marker != 0 && bs.pos >= 2) {
        *consumed = bs.pos - 2;
    } else {
        *consumed = bs.pos;
    }
    return 1;
}

static int jpeg_progressive_finish(jpeg_comp *comps, int num_comp,
                                   int width, int height,
                                   int mcu_cols, int mcu_rows,
                                   int hmax, int vmax,
                                   const int qtables[4][64],
                                   int adobe_transform,
                                   cupidimage_image *out,
                                   char *err, size_t errcap) {
    size_t rgba_size = (size_t)width * (size_t)height * 4u;
    if (rgba_size / 4u != (size_t)width * (size_t)height) {
        set_err(err, errcap, "image too large");
        return 0;
    }

    uint8_t *rgba = (uint8_t *)malloc(rgba_size);
    if (!rgba) {
        set_err(err, errcap, "out of memory");
        return 0;
    }

    int mcu_w = hmax * 8;
    int mcu_h = vmax * 8;

    uint8_t comp_buf[4][16 * 16];
    int comp_w[4] = {0, 0, 0, 0};
    int comp_h[4] = {0, 0, 0, 0};
    for (int i = 0; i < num_comp; i++) {
        comp_w[i] = comps[i].h * 8;
        comp_h[i] = comps[i].v * 8;
    }

    int idx_y = 0;
    int idx_cb = (num_comp > 1) ? 1 : 0;
    int idx_cr = (num_comp > 2) ? 2 : 0;
    int idx_k = (num_comp > 3) ? 3 : 0;
    for (int i = 0; i < num_comp; i++) {
        if (comps[i].id == 1) idx_y = i;
        if (comps[i].id == 2) idx_cb = i;
        if (comps[i].id == 3) idx_cr = i;
        if (comps[i].id == 4) idx_k = i;
    }

    int32_t deq[64];
    uint8_t block_out[64];

    for (int my = 0; my < mcu_rows; my++) {
        for (int mx = 0; mx < mcu_cols; mx++) {
            for (int ci = 0; ci < num_comp; ci++) {
                jpeg_comp *c = &comps[ci];
                int cw = comp_w[ci];
                for (int by = 0; by < c->v; by++) {
                    for (int bx = 0; bx < c->h; bx++) {
                        int block_x = mx * c->h + bx;
                        int block_y = my * c->v + by;
                        int32_t *block = c->coeffs +
                            ((block_y * c->blocks_w + block_x) * 64);
                        for (int i = 0; i < 64; i++) {
                            deq[i] = block[i] * qtables[c->tq][i];
                        }
                        idct_8x8(deq, block_out, 8);
                        for (int y = 0; y < 8; y++) {
                            memcpy(&comp_buf[ci][(by * 8 + y) * cw + bx * 8],
                                   &block_out[y * 8], 8);
                        }
                    }
                }
            }

            for (int y = 0; y < mcu_h; y++) {
                int out_y = my * mcu_h + y;
                if (out_y >= height) {
                    continue;
                }
                for (int x = 0; x < mcu_w; x++) {
                    int out_x = mx * mcu_w + x;
                    if (out_x >= width) {
                        continue;
                    }

                    uint8_t r, g, b;
                    if (num_comp == 1) {
                        int yw = comp_w[idx_y];
                        int yh = comp_h[idx_y];
                        int yy = y * yh / mcu_h;
                        int xx = x * yw / mcu_w;
                        uint8_t yv = comp_buf[idx_y][yy * yw + xx];
                        r = yv;
                        g = yv;
                        b = yv;
                    } else if (num_comp == 3) {
                        int yw = comp_w[idx_y];
                        int yh = comp_h[idx_y];
                        int cbw = comp_w[idx_cb];
                        int cbh = comp_h[idx_cb];
                        int crw = comp_w[idx_cr];
                        int crh = comp_h[idx_cr];

                        int yy = y * yh / mcu_h;
                        int yx = x * yw / mcu_w;
                        int cby = y * cbh / mcu_h;
                        int cbx = x * cbw / mcu_w;
                        int cry = y * crh / mcu_h;
                        int crx = x * crw / mcu_w;

                        int Y = comp_buf[idx_y][yy * yw + yx];
                        int Cb = comp_buf[idx_cb][cby * cbw + cbx] - 128;
                        int Cr = comp_buf[idx_cr][cry * crw + crx] - 128;

                        int rtmp = Y + ((91881 * Cr) >> 16);
                        int gtmp = Y - ((22554 * Cb + 46802 * Cr) >> 16);
                        int btmp = Y + ((116130 * Cb) >> 16);

                        if (rtmp < 0) rtmp = 0;
                        if (rtmp > 255) rtmp = 255;
                        if (gtmp < 0) gtmp = 0;
                        if (gtmp > 255) gtmp = 255;
                        if (btmp < 0) btmp = 0;
                        if (btmp > 255) btmp = 255;

                        r = (uint8_t)rtmp;
                        g = (uint8_t)gtmp;
                        b = (uint8_t)btmp;
                    } else {
                        int yw = comp_w[idx_y];
                        int yh = comp_h[idx_y];
                        int cbw = comp_w[idx_cb];
                        int cbh = comp_h[idx_cb];
                        int crw = comp_w[idx_cr];
                        int crh = comp_h[idx_cr];
                        int kw = comp_w[idx_k];
                        int kh = comp_h[idx_k];

                        int yy = y * yh / mcu_h;
                        int yx = x * yw / mcu_w;
                        int cby = y * cbh / mcu_h;
                        int cbx = x * cbw / mcu_w;
                        int cry = y * crh / mcu_h;
                        int crx = x * crw / mcu_w;
                        int ky = y * kh / mcu_h;
                        int kx = x * kw / mcu_w;

                        uint8_t k = comp_buf[idx_k][ky * kw + kx];
                        if (adobe_transform == 2) {
                            int Y = comp_buf[idx_y][yy * yw + yx];
                            int Cb = comp_buf[idx_cb][cby * cbw + cbx] - 128;
                            int Cr = comp_buf[idx_cr][cry * crw + crx] - 128;

                            int rtmp = Y + ((91881 * Cr) >> 16);
                            int gtmp = Y - ((22554 * Cb + 46802 * Cr) >> 16);
                            int btmp = Y + ((116130 * Cb) >> 16);

                            if (rtmp < 0) rtmp = 0;
                            if (rtmp > 255) rtmp = 255;
                            if (gtmp < 0) gtmp = 0;
                            if (gtmp > 255) gtmp = 255;
                            if (btmp < 0) btmp = 0;
                            if (btmp > 255) btmp = 255;

                            int r2 = rtmp - k;
                            int g2 = gtmp - k;
                            int b2 = btmp - k;
                            if (r2 < 0) r2 = 0;
                            if (g2 < 0) g2 = 0;
                            if (b2 < 0) b2 = 0;
                            r = (uint8_t)r2;
                            g = (uint8_t)g2;
                            b = (uint8_t)b2;
                        } else {
                            int c = comp_buf[idx_y][yy * yw + yx];
                            int m = comp_buf[idx_cb][cby * cbw + cbx];
                            int yv = comp_buf[idx_cr][cry * crw + crx];

                            int r2 = 255 - (c + k);
                            int g2 = 255 - (m + k);
                            int b2 = 255 - (yv + k);
                            if (r2 < 0) r2 = 0;
                            if (g2 < 0) g2 = 0;
                            if (b2 < 0) b2 = 0;
                            if (r2 > 255) r2 = 255;
                            if (g2 > 255) g2 = 255;
                            if (b2 > 255) b2 = 255;
                            r = (uint8_t)r2;
                            g = (uint8_t)g2;
                            b = (uint8_t)b2;
                        }
                    }

                    size_t dst = ((size_t)out_y * (size_t)width + (size_t)out_x) * 4u;
                    rgba[dst + 0] = r;
                    rgba[dst + 1] = g;
                    rgba[dst + 2] = b;
                    rgba[dst + 3] = 255;
                }
            }
        }
    }

    out->width = (uint32_t)width;
    out->height = (uint32_t)height;
    out->rgba = rgba;
    return 1;
}

static int jpeg_decode_scan(const uint8_t *data, size_t size,
                            jpeg_comp *comps, int num_comp,
                            int width, int height,
                            int hmax, int vmax,
                            const int qtables[4][64],
                            const jpeg_huff dc_tables[4],
                            const jpeg_huff ac_tables[4],
                            int restart_interval,
                            int adobe_transform,
                            size_t *consumed,
                            cupidimage_image *out,
                            char *err, size_t errcap) {
    jpeg_bitstream bs;
    bs.data = data;
    bs.size = size;
    bs.pos = 0;
    bs.bitbuf = 0;
    bs.bitcount = 0;
    bs.marker = 0;

    for (int i = 0; i < num_comp; i++) {
        comps[i].dc_pred = 0;
        if (!dc_tables[comps[i].dc_table].valid || !ac_tables[comps[i].ac_table].valid) {
            set_err(err, errcap, "missing huffman table");
            return 0;
        }
    }

    size_t rgba_size = (size_t)width * (size_t)height * 4u;
    if (rgba_size / 4u != (size_t)width * (size_t)height) {
        set_err(err, errcap, "image too large");
        return 0;
    }

    uint8_t *rgba = (uint8_t *)malloc(rgba_size);
    if (!rgba) {
        set_err(err, errcap, "out of memory");
        return 0;
    }

    int mcu_w = hmax * 8;
    int mcu_h = vmax * 8;
    int mcu_cols = (width + mcu_w - 1) / mcu_w;
    int mcu_rows = (height + mcu_h - 1) / mcu_h;

    uint8_t comp_buf[4][16 * 16];
    int comp_w[4] = {0, 0, 0, 0};
    int comp_h[4] = {0, 0, 0, 0};
    for (int i = 0; i < num_comp; i++) {
        comp_w[i] = comps[i].h * 8;
        comp_h[i] = comps[i].v * 8;
    }

    int idx_y = 0;
    int idx_cb = (num_comp > 1) ? 1 : 0;
    int idx_cr = (num_comp > 2) ? 2 : 0;
    int idx_k = (num_comp > 3) ? 3 : 0;
    for (int i = 0; i < num_comp; i++) {
        if (comps[i].id == 1) idx_y = i;
        if (comps[i].id == 2) idx_cb = i;
        if (comps[i].id == 3) idx_cr = i;
        if (comps[i].id == 4) idx_k = i;
    }

    int32_t block[64];
    uint8_t block_out[64];

    int mcu_count = 0;
    int rst_index = 0;

    for (int my = 0; my < mcu_rows; my++) {
        for (int mx = 0; mx < mcu_cols; mx++) {
            for (int ci = 0; ci < num_comp; ci++) {
                jpeg_comp *c = &comps[ci];
                int cw = comp_w[ci];

                for (int by = 0; by < c->v; by++) {
                    for (int bx = 0; bx < c->h; bx++) {
                        if (!decode_block(&bs,
                                          &dc_tables[c->dc_table],
                                          &ac_tables[c->ac_table],
                                          qtables[c->tq],
                                          &c->dc_pred,
                                          block)) {
                            free(rgba);
                            set_err(err, errcap, "jpeg decode failed");
                            return 0;
                        }
                        idct_8x8(block, block_out, 8);
                        for (int y = 0; y < 8; y++) {
                            memcpy(&comp_buf[ci][(by * 8 + y) * cw + bx * 8],
                                   &block_out[y * 8], 8);
                        }
                    }
                }
            }

            for (int y = 0; y < mcu_h; y++) {
                int out_y = my * mcu_h + y;
                if (out_y >= height) {
                    continue;
                }
                for (int x = 0; x < mcu_w; x++) {
                    int out_x = mx * mcu_w + x;
                    if (out_x >= width) {
                        continue;
                    }

                    uint8_t r, g, b;
                    if (num_comp == 1) {
                        int yw = comp_w[idx_y];
                        int yh = comp_h[idx_y];
                        int yy = y * yh / mcu_h;
                        int xx = x * yw / mcu_w;
                        uint8_t yv = comp_buf[idx_y][yy * yw + xx];
                        r = yv;
                        g = yv;
                        b = yv;
                    } else if (num_comp == 3) {
                        int yw = comp_w[idx_y];
                        int yh = comp_h[idx_y];
                        int cbw = comp_w[idx_cb];
                        int cbh = comp_h[idx_cb];
                        int crw = comp_w[idx_cr];
                        int crh = comp_h[idx_cr];

                        int yy = y * yh / mcu_h;
                        int yx = x * yw / mcu_w;
                        int cby = y * cbh / mcu_h;
                        int cbx = x * cbw / mcu_w;
                        int cry = y * crh / mcu_h;
                        int crx = x * crw / mcu_w;

                        int Y = comp_buf[idx_y][yy * yw + yx];
                        int Cb = comp_buf[idx_cb][cby * cbw + cbx] - 128;
                        int Cr = comp_buf[idx_cr][cry * crw + crx] - 128;

                        int rtmp = Y + ((91881 * Cr) >> 16);
                        int gtmp = Y - ((22554 * Cb + 46802 * Cr) >> 16);
                        int btmp = Y + ((116130 * Cb) >> 16);

                        if (rtmp < 0) rtmp = 0;
                        if (rtmp > 255) rtmp = 255;
                        if (gtmp < 0) gtmp = 0;
                        if (gtmp > 255) gtmp = 255;
                        if (btmp < 0) btmp = 0;
                        if (btmp > 255) btmp = 255;

                        r = (uint8_t)rtmp;
                        g = (uint8_t)gtmp;
                        b = (uint8_t)btmp;
                    } else {
                        int yw = comp_w[idx_y];
                        int yh = comp_h[idx_y];
                        int cbw = comp_w[idx_cb];
                        int cbh = comp_h[idx_cb];
                        int crw = comp_w[idx_cr];
                        int crh = comp_h[idx_cr];
                        int kw = comp_w[idx_k];
                        int kh = comp_h[idx_k];

                        int yy = y * yh / mcu_h;
                        int yx = x * yw / mcu_w;
                        int cby = y * cbh / mcu_h;
                        int cbx = x * cbw / mcu_w;
                        int cry = y * crh / mcu_h;
                        int crx = x * crw / mcu_w;
                        int ky = y * kh / mcu_h;
                        int kx = x * kw / mcu_w;

                        uint8_t k = comp_buf[idx_k][ky * kw + kx];
                        if (adobe_transform == 2) {
                            int Y = comp_buf[idx_y][yy * yw + yx];
                            int Cb = comp_buf[idx_cb][cby * cbw + cbx] - 128;
                            int Cr = comp_buf[idx_cr][cry * crw + crx] - 128;

                            int rtmp = Y + ((91881 * Cr) >> 16);
                            int gtmp = Y - ((22554 * Cb + 46802 * Cr) >> 16);
                            int btmp = Y + ((116130 * Cb) >> 16);

                            if (rtmp < 0) rtmp = 0;
                            if (rtmp > 255) rtmp = 255;
                            if (gtmp < 0) gtmp = 0;
                            if (gtmp > 255) gtmp = 255;
                            if (btmp < 0) btmp = 0;
                            if (btmp > 255) btmp = 255;

                            int r2 = rtmp - k;
                            int g2 = gtmp - k;
                            int b2 = btmp - k;
                            if (r2 < 0) r2 = 0;
                            if (g2 < 0) g2 = 0;
                            if (b2 < 0) b2 = 0;
                            r = (uint8_t)r2;
                            g = (uint8_t)g2;
                            b = (uint8_t)b2;
                        } else {
                            int c = comp_buf[idx_y][yy * yw + yx];
                            int m = comp_buf[idx_cb][cby * cbw + cbx];
                            int yv = comp_buf[idx_cr][cry * crw + crx];

                            int r2 = 255 - (c + k);
                            int g2 = 255 - (m + k);
                            int b2 = 255 - (yv + k);
                            if (r2 < 0) r2 = 0;
                            if (g2 < 0) g2 = 0;
                            if (b2 < 0) b2 = 0;
                            if (r2 > 255) r2 = 255;
                            if (g2 > 255) g2 = 255;
                            if (b2 > 255) b2 = 255;
                            r = (uint8_t)r2;
                            g = (uint8_t)g2;
                            b = (uint8_t)b2;
                        }
                    }

                    size_t dst = ((size_t)out_y * (size_t)width + (size_t)out_x) * 4u;
                    rgba[dst + 0] = r;
                    rgba[dst + 1] = g;
                    rgba[dst + 2] = b;
                    rgba[dst + 3] = 255;
                }
            }

            mcu_count++;
            if (restart_interval > 0 && (mcu_count % restart_interval) == 0) {
                int expected = 0xD0 + (rst_index & 7);
                if (!consume_restart(&bs, expected)) {
                    free(rgba);
                    set_err(err, errcap, "jpeg restart marker error");
                    return 0;
                }
                rst_index++;
                for (int i = 0; i < num_comp; i++) {
                    comps[i].dc_pred = 0;
                }
            }
        }
    }

    out->width = (uint32_t)width;
    out->height = (uint32_t)height;
    out->rgba = rgba;
    if (bs.marker != 0 && bs.pos >= 2) {
        *consumed = bs.pos - 2;
    } else {
        *consumed = bs.pos;
    }
    return 1;
}

int cupidimage_load_jpeg(const unsigned char *data, size_t size, cupidimage_image *out,
                         char *err, size_t errcap) {
    jpeg_huff dc_tables[4] = {0};
    jpeg_huff ac_tables[4] = {0};
    int qtables[4][64];
    int qvalid[4] = {0, 0, 0, 0};
    jpeg_comp comps[3];
    int num_comp = 0;
    int width = 0;
    int height = 0;
    int precision = 0;
    int hmax = 0;
    int vmax = 0;
    int restart_interval = 0;
    int progressive = 0;
    int adobe_transform = 0;
    int coeffs_allocated = 0;
    int mcu_cols = 0;
    int mcu_rows = 0;
    size_t pos = 2;
    int saw_sof = 0;
    int saw_sos = 0;

    if (!data || !out) {
        set_err(err, errcap, "invalid arguments");
        goto fail;
    }

    memset(out, 0, sizeof(*out));

    if (size < 2 || data[0] != 0xFF || data[1] != 0xD8) {
        set_err(err, errcap, "not a JPEG");
        goto fail;
    }

    memset(comps, 0, sizeof(comps));

    while (pos + 1 < size) {
        if (data[pos] != 0xFF) {
            JPEG_DEBUG("jpeg: invalid marker prefix at %zu (0x%02X)\n", pos, data[pos]);
            set_err(err, errcap, "invalid jpeg marker");
            goto fail;
        }
        while (pos < size && data[pos] == 0xFF) {
            pos++;
        }
        if (pos >= size) {
            break;
        }
        uint8_t marker = data[pos++];
        JPEG_DEBUG("jpeg: marker 0x%02X at %zu\n", marker, pos - 1);
        if (marker == 0xD9) {
            break;
        }

        if (marker == 0xDA) {
            if (pos + 2 > size) {
                set_err(err, errcap, "truncated jpeg");
                goto fail;
            }
            uint16_t length = read_be16(data + pos);
            pos += 2;
            if (length < 2 || pos + length - 2 > size) {
                set_err(err, errcap, "invalid SOS");
                goto fail;
            }
            if (!saw_sof) {
                set_err(err, errcap, "missing SOF");
                goto fail;
            }
            size_t hdr_end = pos + length - 2;
            int ns = data[pos++];
            if (ns < 1 || ns > num_comp) {
                set_err(err, errcap, "unsupported scan components");
                goto fail;
            }
            int scan_comp[3];
            for (int i = 0; i < ns; i++) {
                int cid = data[pos++];
                int tdta = data[pos++];
                int dc_sel = (tdta >> 4) & 0x0F;
                int ac_sel = tdta & 0x0F;
                int found = 0;
                for (int c = 0; c < num_comp; c++) {
                    if (comps[c].id == cid) {
                        comps[c].dc_table = dc_sel;
                        comps[c].ac_table = ac_sel;
                        scan_comp[i] = c;
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    set_err(err, errcap, "scan component not found");
                    goto fail;
                }
            }
            int ss = data[pos++];
            int se = data[pos++];
            int ah_al = data[pos++];
            int ah = (ah_al >> 4) & 0x0F;
            int al = ah_al & 0x0F;
            JPEG_DEBUG("jpeg: SOS ns=%d ss=%d se=%d ah=%d al=%d progressive=%d\n",
                       ns, ss, se, ah, al, progressive);
            if (pos != hdr_end) {
                set_err(err, errcap, "invalid SOS length");
                goto fail;
            }

            for (int i = 0; i < num_comp; i++) {
                if (comps[i].tq < 0 || comps[i].tq > 3 || !qvalid[comps[i].tq]) {
                    set_err(err, errcap, "missing quant table");
                    goto fail;
                }
            }

            jpeg_apply_default_huffman(dc_tables, ac_tables);

            saw_sos = 1;
            size_t consumed = 0;
            if (!progressive) {
                if (ns != num_comp || ss != 0 || se != 63 || ah != 0 || al != 0) {
                    set_err(err, errcap, "unsupported JPEG scan");
                    goto fail;
                }
                if (!jpeg_decode_scan(data + pos, size - pos,
                                      comps, num_comp,
                                      width, height,
                                      hmax, vmax,
                                      qtables,
                                      dc_tables, ac_tables,
                                      restart_interval,
                                      adobe_transform,
                                      &consumed,
                                      out,
                                      err, errcap)) {
                    goto fail;
                }
                JPEG_DEBUG("jpeg: baseline scan consumed %zu bytes\n", consumed);
                return 1;
            }

            if (ss > se || se > 63 || al > 13 || ah > 13 || (ah > 0 && al >= ah)) {
                set_err(err, errcap, "unsupported JPEG scan");
                goto fail;
            }
            if (ss != 0 || se != 0) {
                if (ns != 1 || ss == 0) {
                    set_err(err, errcap, "invalid AC scan");
                    goto fail;
                }
            }

            if (!jpeg_decode_progressive_scan(data + pos, size - pos,
                                              comps, num_comp,
                                              scan_comp, ns,
                                              ss, se, ah, al,
                                              mcu_cols, mcu_rows,
                                              hmax, vmax,
                                              dc_tables, ac_tables,
                                              restart_interval,
                                              &consumed,
                                              err, errcap)) {
                goto fail;
            }
            JPEG_DEBUG("jpeg: progressive scan consumed %zu bytes\n", consumed);
            pos += consumed;
            while (pos + 1 < size) {
                if (data[pos] == 0xFF && data[pos + 1] != 0x00 && data[pos + 1] != 0xFF) {
                    break;
                }
                pos++;
            }
            JPEG_DEBUG("jpeg: resynced to %zu\n", pos);
            continue;
        }

        if (pos + 2 > size) {
            set_err(err, errcap, "truncated jpeg");
            goto fail;
        }
        uint16_t length = read_be16(data + pos);
        pos += 2;
        if (length < 2 || pos + length - 2 > size) {
            set_err(err, errcap, "invalid segment length");
            goto fail;
        }
        const unsigned char *seg = data + pos;

        if (marker == 0xDB) {
            JPEG_DEBUG("jpeg: DQT length=%u\n", length);
            size_t off = 0;
            size_t seg_len = (size_t)length - 2u;
            while (off + 1 < seg_len) {
                uint8_t pq_tq = seg[off++];
                int pq = (pq_tq >> 4) & 0x0F;
                int tq = pq_tq & 0x0F;
                if (tq > 3) {
                    set_err(err, errcap, "invalid quant table");
                    goto fail;
                }
                if (pq == 0) {
                    if (off + 64 > seg_len) {
                        set_err(err, errcap, "invalid quant table length");
                        goto fail;
                    }
                    for (int i = 0; i < 64; i++) {
                        qtables[tq][zigzag[i]] = seg[off++];
                    }
                } else if (pq == 1) {
                    if (off + 128 > seg_len) {
                        set_err(err, errcap, "invalid quant table length");
                        goto fail;
                    }
                    for (int i = 0; i < 64; i++) {
                        int v = (seg[off] << 8) | seg[off + 1];
                        qtables[tq][zigzag[i]] = v;
                        off += 2;
                    }
                } else {
                    set_err(err, errcap, "unsupported quant precision");
                    goto fail;
                }
                qvalid[tq] = 1;
            }
        } else if (marker == 0xC4) {
            JPEG_DEBUG("jpeg: DHT length=%u\n", length);
            size_t off = 0;
            size_t seg_len = (size_t)length - 2u;
            while (off + 1 < seg_len) {
                uint8_t tc_th = seg[off++];
                int tc = (tc_th >> 4) & 0x0F;
                int th = tc_th & 0x0F;
                if (th > 3 || tc > 1) {
                    set_err(err, errcap, "invalid huffman table");
                    goto fail;
                }
                if (off + 16 > seg_len) {
                    set_err(err, errcap, "invalid huffman table length");
                    goto fail;
                }
                uint8_t bits[16];
                int count = 0;
                for (int i = 0; i < 16; i++) {
                    bits[i] = seg[off++];
                    count += bits[i];
                }
                if (off + (size_t)count > seg_len) {
                    set_err(err, errcap, "invalid huffman table length");
                    goto fail;
                }
                if (tc == 0) {
                    huff_build(&dc_tables[th], bits, seg + off, count);
                } else {
                    huff_build(&ac_tables[th], bits, seg + off, count);
                }
                off += (size_t)count;
            }
        } else if (marker == 0xEE) {
            if (length >= 14) {
                if (seg[0] == 'A' && seg[1] == 'd' && seg[2] == 'o' && seg[3] == 'b' && seg[4] == 'e') {
                    adobe_transform = seg[11];
                    JPEG_DEBUG("jpeg: Adobe APP14 transform=%d\n", adobe_transform);
                }
            }
        } else if (marker == 0xC2 || marker == 0xC0) {
            JPEG_DEBUG("jpeg: %s length=%u\n", marker == 0xC2 ? "SOF2" : "SOF0", length);
            if (length < 8) {
                set_err(err, errcap, "invalid SOF");
                goto fail;
            }
            progressive = (marker == 0xC2);
            precision = seg[0];
            height = (seg[1] << 8) | seg[2];
            width = (seg[3] << 8) | seg[4];
            num_comp = seg[5];
            if (precision != 8) {
                set_err(err, errcap, "only 8-bit JPEG supported");
                goto fail;
            }
            if (num_comp != 1 && num_comp != 3 && num_comp != 4) {
                set_err(err, errcap, "unsupported component count");
                goto fail;
            }
            if (length < 8 + num_comp * 3) {
                set_err(err, errcap, "invalid SOF length");
                goto fail;
            }
            hmax = 0;
            vmax = 0;
            for (int i = 0; i < num_comp; i++) {
                int idx = 6 + i * 3;
                comps[i].id = seg[idx];
                int hv = seg[idx + 1];
                comps[i].h = (hv >> 4) & 0x0F;
                comps[i].v = hv & 0x0F;
                comps[i].tq = seg[idx + 2] & 0x0F;
                if (comps[i].h < 1 || comps[i].v < 1) {
                    set_err(err, errcap, "invalid sampling factors");
                    goto fail;
                }
                if (comps[i].h > hmax) hmax = comps[i].h;
                if (comps[i].v > vmax) vmax = comps[i].v;
            }
            if (hmax > 2 || vmax > 2) {
                set_err(err, errcap, "unsupported sampling factors");
                goto fail;
            }
            mcu_cols = (width + hmax * 8 - 1) / (hmax * 8);
            mcu_rows = (height + vmax * 8 - 1) / (vmax * 8);
            if (progressive && !coeffs_allocated) {
                for (int i = 0; i < num_comp; i++) {
                    comps[i].blocks_w = mcu_cols * comps[i].h;
                    comps[i].blocks_h = mcu_rows * comps[i].v;
                    size_t blocks = (size_t)comps[i].blocks_w * (size_t)comps[i].blocks_h;
                    if (blocks == 0 || blocks > SIZE_MAX / (64u * sizeof(int32_t))) {
                        set_err(err, errcap, "image too large");
                        goto fail;
                    }
                    comps[i].coeffs = (int32_t *)calloc(blocks * 64u, sizeof(int32_t));
                    if (!comps[i].coeffs) {
                        set_err(err, errcap, "out of memory");
                        goto fail;
                    }
                }
                coeffs_allocated = 1;
            }
            saw_sof = 1;
        } else if (marker == 0xDD) {
            JPEG_DEBUG("jpeg: DRI length=%u\n", length);
            if (length != 4) {
                set_err(err, errcap, "invalid DRI");
                goto fail;
            }
            restart_interval = (seg[0] << 8) | seg[1];
        }

        pos += length - 2;
    }

    if (progressive && saw_sos) {
        if (!jpeg_progressive_finish(comps, num_comp,
                                     width, height,
                                     mcu_cols, mcu_rows,
                                     hmax, vmax,
                                     qtables,
                                     adobe_transform,
                                     out,
                                     err, errcap)) {
            goto fail;
        }
        if (coeffs_allocated) {
            jpeg_free_coeffs(comps, num_comp);
            coeffs_allocated = 0;
        }
        return 1;
    }

    if (!saw_sos) {
        set_err(err, errcap, "missing SOS");
    }
fail:
    if (coeffs_allocated) {
        jpeg_free_coeffs(comps, num_comp);
    }
    return 0;
}

int cupidimage_load_jpeg_file(const char *path, cupidimage_image *out,
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

    int ok = cupidimage_load_jpeg(buf, (size_t)fsize, out, err, errcap);
    free(buf);
    return ok;
}

int cupidimage_load_image(const unsigned char *data, size_t size, cupidimage_image *out,
                          char *err, size_t errcap) {
    if (!data || !out) {
        set_err(err, errcap, "invalid arguments");
        return 0;
    }
    if (size >= 8 && data[0] == 137 && data[1] == 80 && data[2] == 78 && data[3] == 71) {
        return cupidimage_load_png(data, size, out, err, errcap);
    }
    if (size >= 6 && (memcmp(data, "GIF87a", 6) == 0 || memcmp(data, "GIF89a", 6) == 0)) {
        return cupidimage_load_gif(data, size, out, err, errcap);
    }
    if (size >= 2 && data[0] == 'B' && data[1] == 'M') {
        return cupidimage_load_bmp(data, size, out, err, errcap);
    }
    if (size >= 12 && data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F' &&
        data[8] == 'W' && data[9] == 'E' && data[10] == 'B' && data[11] == 'P') {
        return cupidimage_load_webp(data, size, out, err, errcap);
    }
    if (size >= 2 && data[0] == 0xFF && data[1] == 0xD8) {
        return cupidimage_load_jpeg(data, size, out, err, errcap);
    }
    set_err(err, errcap, "unknown image format");
    return 0;
}

int cupidimage_load_image_file(const char *path, cupidimage_image *out,
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

    int ok = cupidimage_load_image(buf, (size_t)fsize, out, err, errcap);
    free(buf);
    return ok;
}
