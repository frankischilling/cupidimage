#include "cupidimage_webp_lossless.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

#define VP8L_MAX_CODE_LENGTH 15

static void set_err(char *err, size_t errcap, const char *msg) {
    if (err && errcap) {
        snprintf(err, errcap, "%s", msg);
    }
}

typedef struct vp8l_bit_reader {
    const uint8_t *data;
    size_t size;
    size_t pos;
    uint64_t bitbuf;
    int bitcount;
} vp8l_bit_reader;

static void vp8l_br_init(vp8l_bit_reader *br, const uint8_t *data, size_t size) {
    br->data = data;
    br->size = size;
    br->pos = 0;
    br->bitbuf = 0;
    br->bitcount = 0;
}

static int vp8l_br_fill(vp8l_bit_reader *br, int bits) {
    while (br->bitcount < bits && br->pos < br->size) {
        br->bitbuf |= (uint64_t)br->data[br->pos++] << br->bitcount;
        br->bitcount += 8;
    }
    return br->bitcount >= bits;
}

static uint32_t vp8l_br_read(vp8l_bit_reader *br, int bits) {
    if (bits == 0) {
        return 0;
    }
    if (!vp8l_br_fill(br, bits)) {
        return 0;
    }
    uint32_t val = (uint32_t)(br->bitbuf & ((1ULL << bits) - 1ULL));
    br->bitbuf >>= bits;
    br->bitcount -= bits;
    return val;
}

static uint32_t vp8l_br_peek(vp8l_bit_reader *br, int bits) {
    if (!vp8l_br_fill(br, bits)) {
        return 0;
    }
    return (uint32_t)(br->bitbuf & ((1ULL << bits) - 1ULL));
}

static void vp8l_br_drop(vp8l_bit_reader *br, int bits) {
    br->bitbuf >>= bits;
    br->bitcount -= bits;
}

static unsigned reverse_bits(unsigned v, int bits) {
    unsigned r = 0;
    for (int i = 0; i < bits; i++) {
        r = (r << 1) | (v & 1u);
        v >>= 1;
    }
    return r;
}

typedef struct vp8l_huff {
    int maxbits;
    uint32_t *table;
} vp8l_huff;

static void huff_free(vp8l_huff *h) {
    free(h->table);
    h->table = NULL;
    h->maxbits = 0;
}

static int huff_build(vp8l_huff *h, const uint8_t *lengths, int num, int maxbits) {
    int count[VP8L_MAX_CODE_LENGTH + 1] = {0};
    int maxlen = 0;
    for (int i = 0; i < num; i++) {
        if (lengths[i] > maxbits) {
            return 0;
        }
        if (lengths[i]) {
            count[lengths[i]]++;
            if (lengths[i] > maxlen) {
                maxlen = lengths[i];
            }
        }
    }

    if (maxlen == 0) {
        h->maxbits = 1;
        h->table = (uint32_t *)malloc(2 * sizeof(uint32_t));
        if (!h->table) {
            return 0;
        }
        h->table[0] = (1u << 24) | 0u;
        h->table[1] = (1u << 24) | 0u;
        return 1;
    }

    int next_code[VP8L_MAX_CODE_LENGTH + 1];
    int code = 0;
    count[0] = 0;
    for (int bits = 1; bits <= maxbits; bits++) {
        code = (code + count[bits - 1]) << 1;
        next_code[bits] = code;
    }

    h->maxbits = maxlen;
    size_t table_size = (size_t)1u << maxlen;
    h->table = (uint32_t *)malloc(table_size * sizeof(uint32_t));
    if (!h->table) {
        return 0;
    }
    for (size_t i = 0; i < table_size; i++) {
        h->table[i] = 0;
    }

    for (int sym = 0; sym < num; sym++) {
        int len = lengths[sym];
        if (!len) {
            continue;
        }
        int sym_code = next_code[len]++;
        unsigned rev = reverse_bits((unsigned)sym_code, len);
        unsigned fill = 1u << (maxlen - len);
        for (unsigned i = 0; i < fill; i++) {
            unsigned idx = (i << len) | rev;
            h->table[idx] = ((uint32_t)len << 24) | (uint32_t)sym;
        }
    }
    return 1;
}

static int huff_decode(vp8l_bit_reader *br, const vp8l_huff *h, int *sym) {
    if (!vp8l_br_fill(br, h->maxbits)) {
        return 0;
    }
    uint32_t idx = vp8l_br_peek(br, h->maxbits);
    uint32_t val = h->table[idx];
    if (val == 0) {
        return 0;
    }
    int len = (int)(val >> 24);
    *sym = (int)(val & 0xFFFFFFu);
    vp8l_br_drop(br, len);
    return 1;
}

static const uint8_t k_code_length_code_order[19] = {
    17, 18, 0, 1, 2, 3, 4, 5, 16, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
};

static int read_code_lengths(vp8l_bit_reader *br, vp8l_huff *code_len_table,
                             int max_symbol, uint8_t *lengths) {
    int idx = 0;
    int last_nonzero = 8;
    while (idx < max_symbol) {
        int sym = 0;
        if (!huff_decode(br, code_len_table, &sym)) {
            return 0;
        }
        if (sym <= 15) {
            lengths[idx++] = (uint8_t)sym;
            if (sym != 0) {
                last_nonzero = sym;
            }
        } else if (sym == 16) {
            int repeat = 3 + (int)vp8l_br_read(br, 2);
            int val = last_nonzero;
            for (int i = 0; i < repeat && idx < max_symbol; i++) {
                lengths[idx++] = (uint8_t)val;
            }
        } else if (sym == 17) {
            int repeat = 3 + (int)vp8l_br_read(br, 3);
            for (int i = 0; i < repeat && idx < max_symbol; i++) {
                lengths[idx++] = 0;
            }
        } else if (sym == 18) {
            int repeat = 11 + (int)vp8l_br_read(br, 7);
            for (int i = 0; i < repeat && idx < max_symbol; i++) {
                lengths[idx++] = 0;
            }
        } else {
            return 0;
        }
    }
    return 1;
}

static int read_prefix_code(vp8l_bit_reader *br, int alphabet_size, vp8l_huff *out) {
    uint8_t *lengths = (uint8_t *)calloc((size_t)alphabet_size, 1);
    if (!lengths) {
        return 0;
    }

    int simple = (int)vp8l_br_read(br, 1);
    if (simple) {
        int num_symbols = (int)vp8l_br_read(br, 1) + 1;
        int first_8bits = (int)vp8l_br_read(br, 1);
        int symbol0 = (int)vp8l_br_read(br, first_8bits ? 8 : 1);
        if (symbol0 >= alphabet_size) {
            free(lengths);
            return 0;
        }
        lengths[symbol0] = 1;
        if (num_symbols == 2) {
            int symbol1 = (int)vp8l_br_read(br, 8);
            if (symbol1 >= alphabet_size) {
                free(lengths);
                return 0;
            }
            lengths[symbol1] = 1;
        }
    } else {
        int num_code_lengths = 4 + (int)vp8l_br_read(br, 4);
        uint8_t code_len_lengths[19];
        memset(code_len_lengths, 0, sizeof(code_len_lengths));
        for (int i = 0; i < num_code_lengths; i++) {
            code_len_lengths[k_code_length_code_order[i]] = (uint8_t)vp8l_br_read(br, 3);
        }
        vp8l_huff code_len_table = {0, NULL};
        if (!huff_build(&code_len_table, code_len_lengths, 19, VP8L_MAX_CODE_LENGTH)) {
            free(lengths);
            return 0;
        }

        int max_symbol = 0;
        if (vp8l_br_read(br, 1) == 0) {
            max_symbol = alphabet_size;
        } else {
            int length_nbits = 2 + 2 * (int)vp8l_br_read(br, 3);
            max_symbol = 2 + (int)vp8l_br_read(br, length_nbits);
            if (max_symbol > alphabet_size) {
                huff_free(&code_len_table);
                free(lengths);
                return 0;
            }
        }

        if (!read_code_lengths(br, &code_len_table, max_symbol, lengths)) {
            huff_free(&code_len_table);
            free(lengths);
            return 0;
        }
        huff_free(&code_len_table);
    }

    int ok = huff_build(out, lengths, alphabet_size, VP8L_MAX_CODE_LENGTH);
    free(lengths);
    return ok;
}

static const int8_t k_distance_map[120][2] = {
    {0,1}, {1,0}, {1,1}, {-1,1}, {0,2}, {2,0}, {1,2},
    {-1,2}, {2,1}, {-2,1}, {2,2}, {-2,2}, {0,3}, {3,0},
    {1,3}, {-1,3}, {3,1}, {-3,1}, {2,3}, {-2,3}, {3,2},
    {-3,2}, {0,4}, {4,0}, {1,4}, {-1,4}, {4,1}, {-4,1},
    {3,3}, {-3,3}, {2,4}, {-2,4}, {4,2}, {-4,2}, {0,5},
    {3,4}, {-3,4}, {4,3}, {-4,3}, {5,0}, {1,5}, {-1,5},
    {5,1}, {-5,1}, {2,5}, {-2,5}, {5,2}, {-5,2}, {4,4},
    {-4,4}, {3,5}, {-3,5}, {5,3}, {-5,3}, {0,6}, {6,0},
    {1,6}, {-1,6}, {6,1}, {-6,1}, {2,6}, {-2,6}, {6,2},
    {-6,2}, {4,5}, {-4,5}, {5,4}, {-5,4}, {3,6}, {-3,6},
    {6,3}, {-6,3}, {0,7}, {7,0}, {1,7}, {-1,7}, {5,5},
    {-5,5}, {7,1}, {-7,1}, {4,6}, {-4,6}, {6,4}, {-6,4},
    {2,7}, {-2,7}, {7,2}, {-7,2}, {3,7}, {-3,7}, {7,3},
    {-7,3}, {5,6}, {-5,6}, {6,5}, {-6,5}, {8,0}, {4,7},
    {-4,7}, {7,4}, {-7,4}, {8,1}, {8,2}, {6,6}, {-6,6},
    {8,3}, {5,7}, {-5,7}, {7,5}, {-7,5}, {8,4}, {6,7},
    {-6,7}, {7,6}, {-7,6}, {8,5}, {7,7}, {-7,7}, {8,6},
    {8,7}
};

static int prefix_code_value(vp8l_bit_reader *br, int code) {
    if (code < 4) {
        return code + 1;
    }
    int extra = (code - 2) >> 1;
    int offset = (2 + (code & 1)) << extra;
    int val = offset + (int)vp8l_br_read(br, extra) + 1;
    return val;
}

static int distance_to_offset(int distance_code, int width) {
    if (distance_code <= 0) {
        return 1;
    }
    if (distance_code <= 120) {
        int x = k_distance_map[distance_code - 1][0];
        int y = k_distance_map[distance_code - 1][1];
        int dist = x + y * width;
        if (dist < 1) dist = 1;
        return dist;
    }
    return distance_code - 120;
}

static uint32_t color_cache_hash(uint32_t argb, int bits) {
    if (bits <= 0) {
        return 0;
    }
    return (uint32_t)((argb * 0x1e35a7bdU) >> (32 - bits));
}

typedef struct vp8l_prefix_group {
    vp8l_huff codes[5];
} vp8l_prefix_group;

static void prefix_group_free(vp8l_prefix_group *g) {
    for (int i = 0; i < 5; i++) {
        huff_free(&g->codes[i]);
    }
}

static int decode_image_data(vp8l_bit_reader *br, int width, int height, int use_meta_prefix,
                             uint32_t **out_pixels, char *err, size_t errcap);

static int decode_image_data(vp8l_bit_reader *br, int width, int height, int use_meta_prefix,
                             uint32_t **out_pixels, char *err, size_t errcap) {
    if (width <= 0 || height <= 0) {
        set_err(err, errcap, "invalid VP8L dimensions");
        return 0;
    }
    if ((size_t)width > SIZE_MAX / (size_t)height) {
        set_err(err, errcap, "VP8L dimensions overflow");
        return 0;
    }
    size_t pixel_count = (size_t)width * (size_t)height;

    int color_cache_bits = 0;
    int color_cache_size = 0;
    uint32_t *color_cache = NULL;
    if (vp8l_br_read(br, 1)) {
        color_cache_bits = (int)vp8l_br_read(br, 4);
        if (color_cache_bits < 1 || color_cache_bits > 11) {
            set_err(err, errcap, "invalid VP8L color cache bits");
            return 0;
        }
        color_cache_size = 1 << color_cache_bits;
        color_cache = (uint32_t *)calloc((size_t)color_cache_size, sizeof(uint32_t));
        if (!color_cache) {
            set_err(err, errcap, "out of memory");
            return 0;
        }
    }

    uint16_t *meta_prefix = NULL;
    int prefix_bits = 0;
    int prefix_w = 0;
    int prefix_h = 0;
    int num_groups = 1;

    if (use_meta_prefix) {
        int meta = (int)vp8l_br_read(br, 1);
        if (meta) {
            prefix_bits = (int)vp8l_br_read(br, 3) + 2;
            int scale = 1 << prefix_bits;
            prefix_w = (width + scale - 1) >> prefix_bits;
            prefix_h = (height + scale - 1) >> prefix_bits;
            uint32_t *entropy_pixels = NULL;
            if (!decode_image_data(br, prefix_w, prefix_h, 0, &entropy_pixels, err, errcap)) {
                free(color_cache);
                return 0;
            }
            meta_prefix = (uint16_t *)malloc((size_t)prefix_w * (size_t)prefix_h * sizeof(uint16_t));
            if (!meta_prefix) {
                free(color_cache);
                free(entropy_pixels);
                set_err(err, errcap, "out of memory");
                return 0;
            }
            num_groups = 0;
            for (int i = 0; i < prefix_w * prefix_h; i++) {
                uint16_t code = (uint16_t)((entropy_pixels[i] >> 8) & 0xFFFFu);
                meta_prefix[i] = code;
                if ((int)code + 1 > num_groups) {
                    num_groups = (int)code + 1;
                }
            }
            free(entropy_pixels);
        }
    }

    if (num_groups <= 0) {
        free(color_cache);
        free(meta_prefix);
        set_err(err, errcap, "invalid VP8L prefix groups");
        return 0;
    }

    vp8l_prefix_group *groups = (vp8l_prefix_group *)calloc((size_t)num_groups, sizeof(vp8l_prefix_group));
    if (!groups) {
        free(color_cache);
        free(meta_prefix);
        set_err(err, errcap, "out of memory");
        return 0;
    }

    int g_alphabet = 256 + 24 + color_cache_size;
    int alphabet_sizes[5] = {g_alphabet, 256, 256, 256, 40};
    for (int g = 0; g < num_groups; g++) {
        for (int c = 0; c < 5; c++) {
            if (!read_prefix_code(br, alphabet_sizes[c], &groups[g].codes[c])) {
                for (int i = 0; i <= g; i++) {
                    prefix_group_free(&groups[i]);
                }
                free(groups);
                free(color_cache);
                free(meta_prefix);
                set_err(err, errcap, "invalid VP8L prefix code");
                return 0;
            }
        }
    }

    uint32_t *pixels = (uint32_t *)malloc(pixel_count * sizeof(uint32_t));
    if (!pixels) {
        for (int g = 0; g < num_groups; g++) {
            prefix_group_free(&groups[g]);
        }
        free(groups);
        free(color_cache);
        free(meta_prefix);
        set_err(err, errcap, "out of memory");
        return 0;
    }

    size_t pos = 0;
    while (pos < pixel_count) {
        int x = (int)(pos % (size_t)width);
        int y = (int)(pos / (size_t)width);
        int group = 0;
        if (meta_prefix) {
            int gx = x >> prefix_bits;
            int gy = y >> prefix_bits;
            int idx = gy * prefix_w + gx;
            if (idx < 0 || idx >= prefix_w * prefix_h) {
                set_err(err, errcap, "invalid VP8L meta prefix index");
                free(pixels);
                for (int g = 0; g < num_groups; g++) {
                    prefix_group_free(&groups[g]);
                }
                free(groups);
                free(color_cache);
                free(meta_prefix);
                return 0;
            }
            group = (int)meta_prefix[idx];
            if (group < 0 || group >= num_groups) {
                set_err(err, errcap, "invalid VP8L prefix group");
                free(pixels);
                for (int g = 0; g < num_groups; g++) {
                    prefix_group_free(&groups[g]);
                }
                free(groups);
                free(color_cache);
                free(meta_prefix);
                return 0;
            }
        }
        vp8l_prefix_group *pg = &groups[group];

        int gsym = 0;
        if (!huff_decode(br, &pg->codes[0], &gsym)) {
            set_err(err, errcap, "VP8L decode failed (green)" );
            free(pixels);
            for (int g = 0; g < num_groups; g++) {
                prefix_group_free(&groups[g]);
            }
            free(groups);
            free(color_cache);
            free(meta_prefix);
            return 0;
        }

        if (gsym < 256) {
            int rsym = 0, bsym = 0, asym = 0;
            if (!huff_decode(br, &pg->codes[1], &rsym) ||
                !huff_decode(br, &pg->codes[2], &bsym) ||
                !huff_decode(br, &pg->codes[3], &asym)) {
                set_err(err, errcap, "VP8L decode failed (color)" );
                free(pixels);
                for (int g = 0; g < num_groups; g++) {
                    prefix_group_free(&groups[g]);
                }
                free(groups);
                free(color_cache);
                free(meta_prefix);
                return 0;
            }
            uint32_t color = ((uint32_t)(asym & 0xFF) << 24) |
                             ((uint32_t)(rsym & 0xFF) << 16) |
                             ((uint32_t)(gsym & 0xFF) << 8) |
                             ((uint32_t)(bsym & 0xFF));
            pixels[pos] = color;
            if (color_cache) {
                uint32_t idx = color_cache_hash(color, color_cache_bits);
                color_cache[idx] = color;
            }
            pos++;
        } else if (gsym < 256 + 24) {
            int length = prefix_code_value(br, gsym - 256);
            int dist_sym = 0;
            if (!huff_decode(br, &pg->codes[4], &dist_sym)) {
                set_err(err, errcap, "VP8L decode failed (distance)" );
                free(pixels);
                for (int g = 0; g < num_groups; g++) {
                    prefix_group_free(&groups[g]);
                }
                free(groups);
                free(color_cache);
                free(meta_prefix);
                return 0;
            }
            int dist_code = prefix_code_value(br, dist_sym);
            int dist = distance_to_offset(dist_code, width);
            if ((size_t)dist > pos) {
                set_err(err, errcap, "VP8L invalid distance" );
                free(pixels);
                for (int g = 0; g < num_groups; g++) {
                    prefix_group_free(&groups[g]);
                }
                free(groups);
                free(color_cache);
                free(meta_prefix);
                return 0;
            }
            for (int k = 0; k < length && pos < pixel_count; k++) {
                uint32_t color = pixels[pos - (size_t)dist];
                pixels[pos] = color;
                if (color_cache) {
                    uint32_t idx = color_cache_hash(color, color_cache_bits);
                    color_cache[idx] = color;
                }
                pos++;
            }
        } else {
            if (!color_cache) {
                set_err(err, errcap, "VP8L cache reference without cache");
                free(pixels);
                for (int g = 0; g < num_groups; g++) {
                    prefix_group_free(&groups[g]);
                }
                free(groups);
                free(meta_prefix);
                return 0;
            }
            int cache_idx = gsym - (256 + 24);
            if (cache_idx < 0 || cache_idx >= color_cache_size) {
                set_err(err, errcap, "VP8L cache index out of range");
                free(pixels);
                for (int g = 0; g < num_groups; g++) {
                    prefix_group_free(&groups[g]);
                }
                free(groups);
                free(color_cache);
                free(meta_prefix);
                return 0;
            }
            uint32_t color = color_cache[cache_idx];
            pixels[pos] = color;
            if (color_cache) {
                uint32_t idx = color_cache_hash(color, color_cache_bits);
                color_cache[idx] = color;
            }
            pos++;
        }
    }

    for (int g = 0; g < num_groups; g++) {
        prefix_group_free(&groups[g]);
    }
    free(groups);
    free(color_cache);
    free(meta_prefix);

    *out_pixels = pixels;
    return 1;
}

static uint8_t clamp_u8(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

static uint32_t avg2_color(uint32_t a, uint32_t b) {
    uint8_t aA = (a >> 24) & 0xFF;
    uint8_t aR = (a >> 16) & 0xFF;
    uint8_t aG = (a >> 8) & 0xFF;
    uint8_t aB = a & 0xFF;
    uint8_t bA = (b >> 24) & 0xFF;
    uint8_t bR = (b >> 16) & 0xFF;
    uint8_t bG = (b >> 8) & 0xFF;
    uint8_t bB = b & 0xFF;
    return ((uint32_t)((aA + bA) >> 1) << 24) |
           ((uint32_t)((aR + bR) >> 1) << 16) |
           ((uint32_t)((aG + bG) >> 1) << 8) |
           ((uint32_t)((aB + bB) >> 1));
}

static uint32_t add_argb(uint32_t a, uint32_t b) {
    uint8_t aA = (a >> 24) & 0xFF;
    uint8_t aR = (a >> 16) & 0xFF;
    uint8_t aG = (a >> 8) & 0xFF;
    uint8_t aB = a & 0xFF;
    uint8_t bA = (b >> 24) & 0xFF;
    uint8_t bR = (b >> 16) & 0xFF;
    uint8_t bG = (b >> 8) & 0xFF;
    uint8_t bB = b & 0xFF;
    return ((uint32_t)((aA + bA) & 0xFF) << 24) |
           ((uint32_t)((aR + bR) & 0xFF) << 16) |
           ((uint32_t)((aG + bG) & 0xFF) << 8) |
           ((uint32_t)((aB + bB) & 0xFF));
}

static uint32_t clamp_add_sub(uint32_t a, uint32_t b, uint32_t c) {
    int aA = (a >> 24) & 0xFF;
    int aR = (a >> 16) & 0xFF;
    int aG = (a >> 8) & 0xFF;
    int aB = a & 0xFF;
    int bA = (b >> 24) & 0xFF;
    int bR = (b >> 16) & 0xFF;
    int bG = (b >> 8) & 0xFF;
    int bB = b & 0xFF;
    int cA = (c >> 24) & 0xFF;
    int cR = (c >> 16) & 0xFF;
    int cG = (c >> 8) & 0xFF;
    int cB = c & 0xFF;
    return ((uint32_t)clamp_u8(aA + bA - cA) << 24) |
           ((uint32_t)clamp_u8(aR + bR - cR) << 16) |
           ((uint32_t)clamp_u8(aG + bG - cG) << 8) |
           ((uint32_t)clamp_u8(aB + bB - cB));
}

static uint32_t clamp_add_sub_half(uint32_t a, uint32_t b) {
    int aA = (a >> 24) & 0xFF;
    int aR = (a >> 16) & 0xFF;
    int aG = (a >> 8) & 0xFF;
    int aB = a & 0xFF;
    int bA = (b >> 24) & 0xFF;
    int bR = (b >> 16) & 0xFF;
    int bG = (b >> 8) & 0xFF;
    int bB = b & 0xFF;
    return ((uint32_t)clamp_u8(aA + ((aA - bA) >> 1)) << 24) |
           ((uint32_t)clamp_u8(aR + ((aR - bR) >> 1)) << 16) |
           ((uint32_t)clamp_u8(aG + ((aG - bG) >> 1)) << 8) |
           ((uint32_t)clamp_u8(aB + ((aB - bB) >> 1)));
}

static uint32_t select_color(uint32_t a, uint32_t b, uint32_t c) {
    int aA = (a >> 24) & 0xFF;
    int aR = (a >> 16) & 0xFF;
    int aG = (a >> 8) & 0xFF;
    int aB = a & 0xFF;
    int bA = (b >> 24) & 0xFF;
    int bR = (b >> 16) & 0xFF;
    int bG = (b >> 8) & 0xFF;
    int bB = b & 0xFF;
    int cA = (c >> 24) & 0xFF;
    int cR = (c >> 16) & 0xFF;
    int cG = (c >> 8) & 0xFF;
    int cB = c & 0xFF;

    int pA = aA + bA - cA;
    int pR = aR + bR - cR;
    int pG = aG + bG - cG;
    int pB = aB + bB - cB;

    int dA = abs(pA - aA) + abs(pR - aR) + abs(pG - aG) + abs(pB - aB);
    int dB = abs(pA - bA) + abs(pR - bR) + abs(pG - bG) + abs(pB - bB);

    return (dA <= dB) ? a : b;
}

static uint32_t predictor_value(int mode, uint32_t L, uint32_t T, uint32_t TL, uint32_t TR) {
    switch (mode) {
    case 0: /* black */
        return 0xFF000000u;
    case 1: /* L */
        return L;
    case 2: /* T */
        return T;
    case 3: /* TR */
        return TR;
    case 4: /* TL */
        return TL;
    case 5: /* average2(L, TR), T */
        return avg2_color(avg2_color(L, TR), T);
    case 6: /* average2(L, TL) */
        return avg2_color(L, TL);
    case 7: /* average2(L, T) */
        return avg2_color(L, T);
    case 8: /* average2(TL, T) */
        return avg2_color(TL, T);
    case 9: /* average2(T, TR) */
        return avg2_color(T, TR);
    case 10: /* average2(average2(L, TL), average2(T, TR)) */
        return avg2_color(avg2_color(L, TL), avg2_color(T, TR));
    case 11: /* select */
        return select_color(L, T, TL);
    case 12: /* clamp add subtract full */
        return clamp_add_sub(L, T, TL);
    case 13: /* clamp add subtract half */
        return clamp_add_sub_half(avg2_color(L, T), TL);
    default:
        return L;
    }
}

static void apply_predictor_transform(uint32_t *pixels, int width, int height,
                                      const uint32_t *pred_image, int size_bits, int pred_width) {
    if (!pred_image) {
        return;
    }
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = y * width + x;
            uint32_t pred;
            if (x == 0 && y == 0) {
                pred = 0xFF000000u;
            } else if (y == 0) {
                pred = pixels[idx - 1];
            } else if (x == 0) {
                pred = pixels[idx - width];
            } else {
                int bx = x >> size_bits;
                int by = y >> size_bits;
                int pidx = by * pred_width + bx;
                int mode = (int)((pred_image[pidx] >> 8) & 0xFFu);
                if (mode > 13) {
                    mode = 0;
                }
                uint32_t L = pixels[idx - 1];
                uint32_t T = pixels[idx - width];
                uint32_t TL = pixels[idx - width - 1];
                uint32_t TR;
                if (x + 1 < width) {
                    TR = pixels[idx - width + 1];
                } else {
                    TR = pixels[idx - width - (width - 1)];
                }
                pred = predictor_value(mode, L, T, TL, TR);
            }
            pixels[idx] = add_argb(pixels[idx], pred);
        }
    }
}

static void apply_color_transform(uint32_t *pixels, int width, int height,
                                  const uint32_t *transform, int size_bits, int trans_width) {
    if (!transform) {
        return;
    }
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = y * width + x;
            int bx = x >> size_bits;
            int by = y >> size_bits;
            uint32_t t = transform[by * trans_width + bx];
            int8_t green_to_red = (int8_t)(t & 0xFF);
            int8_t green_to_blue = (int8_t)((t >> 8) & 0xFF);
            int8_t red_to_blue = (int8_t)((t >> 16) & 0xFF);

            uint32_t c = pixels[idx];
            uint8_t a = (c >> 24) & 0xFF;
            uint8_t r = (c >> 16) & 0xFF;
            uint8_t g = (c >> 8) & 0xFF;
            uint8_t b = c & 0xFF;

            int8_t g_s = (int8_t)g;
            int delta_gr = (green_to_red * g_s) >> 5;
            int tmp_r = (r + delta_gr) & 0xFF;
            int delta_gb = (green_to_blue * g_s) >> 5;
            int8_t r_s = (int8_t)tmp_r;
            int delta_rb = (red_to_blue * r_s) >> 5;
            int tmp_b = (b + delta_gb + delta_rb) & 0xFF;

            pixels[idx] = ((uint32_t)a << 24) | ((uint32_t)tmp_r << 16) | ((uint32_t)g << 8) | (uint32_t)tmp_b;
        }
    }
}

static void apply_subtract_green(uint32_t *pixels, int width, int height) {
    size_t count = (size_t)width * (size_t)height;
    for (size_t i = 0; i < count; i++) {
        uint32_t c = pixels[i];
        uint8_t a = (c >> 24) & 0xFF;
        uint8_t r = (c >> 16) & 0xFF;
        uint8_t g = (c >> 8) & 0xFF;
        uint8_t b = c & 0xFF;
        r = (uint8_t)((r + g) & 0xFF);
        b = (uint8_t)((b + g) & 0xFF);
        pixels[i] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    }
}

static int apply_color_indexing(uint32_t **pixels, int *width, int height,
                                const uint32_t *color_table, int color_table_size, int width_bits,
                                int orig_width, char *err, size_t errcap) {
    int packed_width = *width;
    int scale = 1 << width_bits;
    int new_width = orig_width;
    int expected_packed = (orig_width + scale - 1) >> width_bits;
    if (packed_width != expected_packed) {
        set_err(err, errcap, "VP8L color index width mismatch");
        return 0;
    }
    if (new_width <= 0 || (size_t)new_width > SIZE_MAX / (size_t)height) {
        set_err(err, errcap, "VP8L invalid color index width");
        return 0;
    }
    uint32_t *expanded = (uint32_t *)malloc((size_t)new_width * (size_t)height * sizeof(uint32_t));
    if (!expanded) {
        set_err(err, errcap, "out of memory");
        return 0;
    }

    int bits_per_pixel = 8 >> width_bits;
    int mask = (1 << bits_per_pixel) - 1;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < new_width; x++) {
            int packed_x = x >> width_bits;
            int shift = bits_per_pixel * (x & (scale - 1));
            uint32_t packed = (*pixels)[y * packed_width + packed_x];
            int index = (int)((packed >> 8) & 0xFFu);
            int palette_index = (index >> shift) & mask;
            uint32_t color = 0;
            if (palette_index >= 0 && palette_index < color_table_size) {
                color = color_table[palette_index];
            }
            expanded[(size_t)y * (size_t)new_width + (size_t)x] = color;
        }
    }

    free(*pixels);
    *pixels = expanded;
    *width = new_width;
    return 1;
}

typedef enum {
    TRANS_PREDICTOR = 0,
    TRANS_COLOR = 1,
    TRANS_SUBGREEN = 2,
    TRANS_INDEX = 3
} transform_type;

typedef struct vp8l_transform {
    transform_type type;
    int size_bits;
    int width;
    int height;
    uint32_t *data;
    int width_bits;
    int color_table_size;
    uint32_t *color_table;
    int orig_width;
} vp8l_transform;

static void transform_free(vp8l_transform *t) {
    free(t->data);
    t->data = NULL;
    free(t->color_table);
    t->color_table = NULL;
}

int cupidimage_decode_vp8l(const unsigned char *data, size_t size,
                            cupidimage_image *out, char *err, size_t errcap) {
    if (!data || !out) {
        set_err(err, errcap, "invalid arguments");
        return 0;
    }
    memset(out, 0, sizeof(*out));

    if (size < 5) {
        set_err(err, errcap, "invalid VP8L frame");
        return 0;
    }

    vp8l_bit_reader br;
    vp8l_br_init(&br, data, size);
    uint32_t signature = vp8l_br_read(&br, 8);
    if (signature != 0x2F) {
        set_err(err, errcap, "invalid VP8L signature");
        return 0;
    }

    int width = (int)vp8l_br_read(&br, 14) + 1;
    int height = (int)vp8l_br_read(&br, 14) + 1;
    (void)vp8l_br_read(&br, 1); /* alpha used */
    int version = (int)vp8l_br_read(&br, 3);
    if (version != 0) {
        set_err(err, errcap, "unsupported VP8L version");
        return 0;
    }

    int base_width = width;
    int base_height = height;

    vp8l_transform transforms[4];
    int transform_count = 0;
    memset(transforms, 0, sizeof(transforms));

    while (vp8l_br_read(&br, 1)) {
        int type = (int)vp8l_br_read(&br, 2);
        if (transform_count >= 4) {
            set_err(err, errcap, "too many VP8L transforms");
            return 0;
        }
        vp8l_transform *t = &transforms[transform_count++];
        t->type = (transform_type)type;

        if (t->type == TRANS_PREDICTOR || t->type == TRANS_COLOR) {
            t->size_bits = (int)vp8l_br_read(&br, 3) + 2;
            int scale = 1 << t->size_bits;
            t->width = (width + scale - 1) >> t->size_bits;
            t->height = (height + scale - 1) >> t->size_bits;
            if (!decode_image_data(&br, t->width, t->height, 0, &t->data, err, errcap)) {
                for (int i = 0; i < transform_count; i++) {
                    transform_free(&transforms[i]);
                }
                return 0;
            }
        } else if (t->type == TRANS_SUBGREEN) {
            t->size_bits = 0;
        } else if (t->type == TRANS_INDEX) {
            t->color_table_size = (int)vp8l_br_read(&br, 8) + 1;
            t->orig_width = width;
            if (t->color_table_size <= 0) {
                set_err(err, errcap, "invalid VP8L color table size");
                for (int i = 0; i < transform_count; i++) {
                    transform_free(&transforms[i]);
                }
                return 0;
            }
            int width_bits = 0;
            if (t->color_table_size <= 2) {
                width_bits = 3;
            } else if (t->color_table_size <= 4) {
                width_bits = 2;
            } else if (t->color_table_size <= 16) {
                width_bits = 1;
            } else {
                width_bits = 0;
            }
            t->width_bits = width_bits;

            uint32_t *table_raw = NULL;
            if (!decode_image_data(&br, t->color_table_size, 1, 0, &table_raw, err, errcap)) {
                for (int i = 0; i < transform_count; i++) {
                    transform_free(&transforms[i]);
                }
                return 0;
            }
            t->color_table = (uint32_t *)malloc((size_t)t->color_table_size * sizeof(uint32_t));
            if (!t->color_table) {
                free(table_raw);
                for (int i = 0; i < transform_count; i++) {
                    transform_free(&transforms[i]);
                }
                set_err(err, errcap, "out of memory");
                return 0;
            }
            uint32_t prev = 0;
            for (int i = 0; i < t->color_table_size; i++) {
                uint32_t c = table_raw[i];
                uint8_t a = (uint8_t)(((prev >> 24) & 0xFF) + ((c >> 24) & 0xFF));
                uint8_t r = (uint8_t)(((prev >> 16) & 0xFF) + ((c >> 16) & 0xFF));
                uint8_t g = (uint8_t)(((prev >> 8) & 0xFF) + ((c >> 8) & 0xFF));
                uint8_t b = (uint8_t)((prev & 0xFF) + (c & 0xFF));
                t->color_table[i] = ((uint32_t)a << 24) | ((uint32_t)r << 16) |
                                    ((uint32_t)g << 8) | (uint32_t)b;
                prev = t->color_table[i];
            }
            free(table_raw);

            int scale = 1 << width_bits;
            width = (width + scale - 1) >> width_bits;
        } else {
            set_err(err, errcap, "unknown VP8L transform");
            for (int i = 0; i < transform_count; i++) {
                transform_free(&transforms[i]);
            }
            return 0;
        }
    }

    uint32_t *pixels = NULL;
    if (!decode_image_data(&br, width, height, 1, &pixels, err, errcap)) {
        for (int i = 0; i < transform_count; i++) {
            transform_free(&transforms[i]);
        }
        return 0;
    }

    int img_width = width;
    int img_height = height;
    for (int i = transform_count - 1; i >= 0; i--) {
        vp8l_transform *t = &transforms[i];
        switch (t->type) {
        case TRANS_PREDICTOR:
            apply_predictor_transform(pixels, img_width, img_height, t->data, t->size_bits, t->width);
            break;
        case TRANS_COLOR:
            apply_color_transform(pixels, img_width, img_height, t->data, t->size_bits, t->width);
            break;
        case TRANS_SUBGREEN:
            apply_subtract_green(pixels, img_width, img_height);
            break;
        case TRANS_INDEX:
            if (!apply_color_indexing(&pixels, &img_width, img_height,
                                      t->color_table, t->color_table_size, t->width_bits,
                                      t->orig_width, err, errcap)) {
                for (int j = 0; j < transform_count; j++) {
                    transform_free(&transforms[j]);
                }
                free(pixels);
                return 0;
            }
            break;
        default:
            break;
        }
    }

    for (int i = 0; i < transform_count; i++) {
        transform_free(&transforms[i]);
    }

    if (img_width != base_width || img_height != base_height) {
        free(pixels);
        set_err(err, errcap, "VP8L transform size mismatch");
        return 0;
    }

    out->width = (uint32_t)base_width;
    out->height = (uint32_t)base_height;
    size_t out_count = (size_t)base_width * (size_t)base_height;
    out->rgba = (uint8_t *)malloc(out_count * 4u);
    if (!out->rgba) {
        free(pixels);
        set_err(err, errcap, "out of memory");
        return 0;
    }

    for (size_t i = 0; i < out_count; i++) {
        uint32_t c = pixels[i];
        out->rgba[i * 4 + 0] = (uint8_t)((c >> 16) & 0xFF);
        out->rgba[i * 4 + 1] = (uint8_t)((c >> 8) & 0xFF);
        out->rgba[i * 4 + 2] = (uint8_t)(c & 0xFF);
        out->rgba[i * 4 + 3] = (uint8_t)((c >> 24) & 0xFF);
    }

    free(pixels);
    return 1;
}
