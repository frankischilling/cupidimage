#include "cupidimage.h"
#include "cupidimage_internal.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

#ifdef CUPIDIMAGE_BMP_DEBUG
#define BMP_DEBUG(...) fprintf(stderr, "BMP: " __VA_ARGS__)
#else
#define BMP_DEBUG(...) ((void)0)
#endif

#define BI_RGB 0u
#define BI_RLE8 1u
#define BI_RLE4 2u
#define BI_BITFIELDS 3u
#define BI_JPEG 4u
#define BI_PNG 5u
#define BI_ALPHABITFIELDS 6u

#define BMP_MAX_DIM 65535u
#define LCS_GM_IMAGES 4u

static void set_err(char *err, size_t errcap, const char *msg) {
    if (err && errcap) {
        snprintf(err, errcap, "%s", msg);
    }
    fprintf(stderr, "BMP error: %s\n", msg);
}

static uint16_t read_le16(const unsigned char *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t read_le32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

typedef struct bmp_mask_info {
    uint32_t mask;
    int shift;
    int bits;
} bmp_mask_info;

static void bmp_mask_init(bmp_mask_info *mi, uint32_t mask) {
    mi->mask = mask;
    mi->shift = 0;
    mi->bits = 0;
    if (mask == 0) {
        return;
    }
    int shift = 0;
    while ((mask & 1u) == 0u) {
        mask >>= 1u;
        shift++;
    }
    int bits = 0;
    while (mask & 1u) {
        bits++;
        mask >>= 1u;
    }
    mi->shift = shift;
    mi->bits = bits;
}

static uint8_t bmp_mask_extract(uint32_t value, const bmp_mask_info *mi) {
    if (!mi->mask || mi->bits <= 0) {
        return 0;
    }
    uint32_t v = (value & mi->mask) >> (uint32_t)mi->shift;
    if (mi->bits >= 8) {
        return (uint8_t)(v >> (uint32_t)(mi->bits - 8));
    }
    uint32_t maxv = (1u << (uint32_t)mi->bits) - 1u;
    return (uint8_t)((v * 255u + maxv / 2u) / maxv);
}

#ifdef CUPIDIMAGE_BMP_DEBUG
static const char *bmp_header_name(uint32_t size) {
    switch (size) {
        case 12: return "BITMAPCOREHEADER";
        case 40: return "BITMAPINFOHEADER";
        case 52: return "BITMAPV2INFOHEADER";
        case 56: return "BITMAPV3INFOHEADER";
        case 108: return "BITMAPV4HEADER";
        case 124: return "BITMAPV5HEADER";
        default: return "UNKNOWN";
    }
}

static const char *bmp_compression_name(uint32_t comp) {
    switch (comp) {
        case BI_RGB: return "BI_RGB";
        case BI_RLE8: return "BI_RLE8";
        case BI_RLE4: return "BI_RLE4";
        case BI_BITFIELDS: return "BI_BITFIELDS";
        case BI_JPEG: return "BI_JPEG";
        case BI_PNG: return "BI_PNG";
        case BI_ALPHABITFIELDS: return "BI_ALPHABITFIELDS";
        default: return "UNKNOWN";
    }
}
#endif

static int bmp_decode_rle8(const uint8_t *src, size_t size,
                           uint32_t width, uint32_t height, int top_down,
                           uint8_t *out, char *err, size_t errcap) {
    size_t pos = 0;
    uint32_t x = 0;
    uint32_t y = 0;
    int done = 0;
    while (!done) {
        if (pos + 1 >= size) {
            set_err(err, errcap, "truncated pixel data");
            return 0;
        }
        uint8_t count = src[pos++];
        uint8_t value = src[pos++];
        if (count) {
            if (x + count > width) {
                set_err(err, errcap, "invalid RLE stream");
                return 0;
            }
            if (y >= height) {
                set_err(err, errcap, "invalid RLE stream");
                return 0;
            }
            uint32_t dst_y = top_down ? y : (height - 1u - y);
            size_t base = (size_t)dst_y * (size_t)width + x;
            for (uint8_t i = 0; i < count; i++) {
                out[base + i] = value;
            }
            x += count;
        } else {
            if (value == 0) {
                BMP_DEBUG("RLE8: EOL\n");
                x = 0;
                y++;
            } else if (value == 1) {
                BMP_DEBUG("RLE8: EOB\n");
                done = 1;
            } else if (value == 2) {
                if (pos + 1 >= size) {
                    set_err(err, errcap, "truncated pixel data");
                    return 0;
                }
                uint8_t dx = src[pos++];
                uint8_t dy = src[pos++];
                BMP_DEBUG("RLE8: delta %u,%u\n", dx, dy);
                if (x + dx > width || y + dy > height) {
                    set_err(err, errcap, "RLE delta out of bounds");
                    return 0;
                }
                x += dx;
                y += dy;
            } else {
                uint8_t run = value;
                BMP_DEBUG("RLE8: absolute run %u\n", run);
                if (pos + run > size) {
                    set_err(err, errcap, "truncated pixel data");
                    return 0;
                }
                if (x + run > width) {
                    set_err(err, errcap, "invalid RLE stream");
                    return 0;
                }
                if (y >= height) {
                    set_err(err, errcap, "invalid RLE stream");
                    return 0;
                }
                uint32_t dst_y = top_down ? y : (height - 1u - y);
                size_t base = (size_t)dst_y * (size_t)width + x;
                for (uint8_t i = 0; i < run; i++) {
                    out[base + i] = src[pos + i];
                }
                x += run;
                pos += run;
                if (run & 1u) {
                    if (pos >= size) {
                        set_err(err, errcap, "truncated pixel data");
                        return 0;
                    }
                    pos++;
                }
            }
        }
        if (y > height) {
            set_err(err, errcap, "invalid RLE stream");
            return 0;
        }
        if (y == height) {
            done = 1;
        }
    }
    return 1;
}

static int bmp_decode_rle4(const uint8_t *src, size_t size,
                           uint32_t width, uint32_t height, int top_down,
                           uint8_t *out, char *err, size_t errcap) {
    size_t pos = 0;
    uint32_t x = 0;
    uint32_t y = 0;
    int done = 0;
    while (!done) {
        if (pos + 1 >= size) {
            set_err(err, errcap, "truncated pixel data");
            return 0;
        }
        uint8_t count = src[pos++];
        uint8_t value = src[pos++];
        if (count) {
            if (x + count > width) {
                set_err(err, errcap, "invalid RLE stream");
                return 0;
            }
            if (y >= height) {
                set_err(err, errcap, "invalid RLE stream");
                return 0;
            }
            uint8_t hi = (uint8_t)(value >> 4);
            uint8_t lo = (uint8_t)(value & 0x0Fu);
            uint32_t dst_y = top_down ? y : (height - 1u - y);
            size_t base = (size_t)dst_y * (size_t)width + x;
            for (uint8_t i = 0; i < count; i++) {
                out[base + i] = (i & 1u) ? lo : hi;
            }
            x += count;
        } else {
            if (value == 0) {
                BMP_DEBUG("RLE4: EOL\n");
                x = 0;
                y++;
            } else if (value == 1) {
                BMP_DEBUG("RLE4: EOB\n");
                done = 1;
            } else if (value == 2) {
                if (pos + 1 >= size) {
                    set_err(err, errcap, "truncated pixel data");
                    return 0;
                }
                uint8_t dx = src[pos++];
                uint8_t dy = src[pos++];
                BMP_DEBUG("RLE4: delta %u,%u\n", dx, dy);
                if (x + dx > width || y + dy > height) {
                    set_err(err, errcap, "RLE delta out of bounds");
                    return 0;
                }
                x += dx;
                y += dy;
            } else {
                uint8_t run = value;
                BMP_DEBUG("RLE4: absolute run %u\n", run);
                size_t bytes = (run + 1u) / 2u;
                if (pos + bytes > size) {
                    set_err(err, errcap, "truncated pixel data");
                    return 0;
                }
                if (x + run > width) {
                    set_err(err, errcap, "invalid RLE stream");
                    return 0;
                }
                if (y >= height) {
                    set_err(err, errcap, "invalid RLE stream");
                    return 0;
                }
                uint32_t dst_y = top_down ? y : (height - 1u - y);
                size_t base = (size_t)dst_y * (size_t)width + x;
                for (uint8_t i = 0; i < run; i++) {
                    uint8_t byte = src[pos + i / 2u];
                    out[base + i] = (i & 1u) ? (byte & 0x0Fu) : (uint8_t)(byte >> 4);
                }
                x += run;
                pos += bytes;
                if (bytes & 1u) {
                    if (pos >= size) {
                        set_err(err, errcap, "truncated pixel data");
                        return 0;
                    }
                    pos++;
                }
            }
        }
        if (y > height) {
            set_err(err, errcap, "invalid RLE stream");
            return 0;
        }
        if (y == height) {
            done = 1;
        }
    }
    return 1;
}

static int bmp_should_unpremultiply(const uint8_t *rgba, size_t pixels, int metadata_premultiplied) {
    if (metadata_premultiplied) {
        return 1;
    }
    int saw_alpha = 0;
    for (size_t i = 0; i < pixels; i++) {
        uint8_t a = rgba[i * 4u + 3u];
        if (a < 255) {
            saw_alpha = 1;
            uint8_t r = rgba[i * 4u + 0u];
            uint8_t g = rgba[i * 4u + 1u];
            uint8_t b = rgba[i * 4u + 2u];
            if (r > a || g > a || b > a) {
                return 0;
            }
        }
    }
    return saw_alpha ? 1 : 0;
}

static void bmp_unpremultiply(uint8_t *rgba, size_t pixels) {
    for (size_t i = 0; i < pixels; i++) {
        uint8_t *p = rgba + i * 4u;
        uint8_t a = p[3];
        if (a == 0) {
            p[0] = 0;
            p[1] = 0;
            p[2] = 0;
            continue;
        }
        uint32_t r = (uint32_t)p[0] * 255u;
        uint32_t g = (uint32_t)p[1] * 255u;
        uint32_t b = (uint32_t)p[2] * 255u;
        p[0] = (uint8_t)((r + a / 2u) / a);
        p[1] = (uint8_t)((g + a / 2u) / a);
        p[2] = (uint8_t)((b + a / 2u) / a);
    }
}

int cupidimage_load_bmp(const unsigned char *data, size_t size,
                        cupidimage_image *out, char *err, size_t errcap) {
    if (!data || !out) {
        set_err(err, errcap, "invalid arguments");
        return 0;
    }

    memset(out, 0, sizeof(*out));

    if (size < 18) {
        set_err(err, errcap, "file too small");
        return 0;
    }
    if (data[0] != 'B' || data[1] != 'M') {
        set_err(err, errcap, "invalid BMP signature");
        return 0;
    }

    uint32_t file_size = read_le32(data + 2);
    uint32_t data_offset = read_le32(data + 10);
    if (file_size && (file_size > size || file_size < 14)) {
        set_err(err, errcap, "file too small");
        return 0;
    }
    size_t size_bound = file_size ? (size_t)file_size : size;
    if (data_offset >= size_bound) {
        set_err(err, errcap, "invalid data offset");
        return 0;
    }

    uint32_t dib_size = read_le32(data + 14);
    if (dib_size != 12 && dib_size != 40 && dib_size != 52 && dib_size != 56 &&
        dib_size != 108 && dib_size != 124) {
        set_err(err, errcap, "unknown header version");
        return 0;
    }
    if (14u + dib_size > size_bound) {
        set_err(err, errcap, "file too small");
        return 0;
    }
    if (file_size && file_size < 14u + dib_size) {
        set_err(err, errcap, "file too small");
        return 0;
    }

    BMP_DEBUG("signature='BM', file_size=%u, data_offset=%u\n", file_size, data_offset);
    BMP_DEBUG("header_size=%u (%s)\n", dib_size, bmp_header_name(dib_size));

    const uint8_t *dib = data + 14;
    uint32_t width = 0;
    uint32_t height = 0;
    int top_down = 0;
    uint16_t planes = 0;
    uint16_t bit_count = 0;
    uint32_t compression = BI_RGB;
    uint32_t clr_used = 0;
    uint32_t mask_r = 0;
    uint32_t mask_g = 0;
    uint32_t mask_b = 0;
    uint32_t mask_a = 0;
    int has_masks = 0;
    uint32_t cstype = 0;
    uint32_t intent = 0;
    uint32_t profile_data = 0;
    uint32_t profile_size = 0;

    if (dib_size == 12) {
        width = read_le16(dib + 4);
        height = read_le16(dib + 6);
        planes = read_le16(dib + 8);
        bit_count = read_le16(dib + 10);
        compression = BI_RGB;
    } else {
        int32_t w = (int32_t)read_le32(dib + 4);
        int32_t h = (int32_t)read_le32(dib + 8);
        planes = read_le16(dib + 12);
        bit_count = read_le16(dib + 14);
        compression = read_le32(dib + 16);
        clr_used = read_le32(dib + 32);

        if (w <= 0) {
            set_err(err, errcap, "invalid dimensions");
            return 0;
        }
        if (h == 0 || h == INT32_MIN) {
            set_err(err, errcap, "invalid dimensions");
            return 0;
        }
        if (h < 0) {
            top_down = 1;
            h = -h;
        }
        width = (uint32_t)w;
        height = (uint32_t)h;

        if (dib_size >= 52) {
            mask_r = read_le32(dib + 40);
            mask_g = read_le32(dib + 44);
            mask_b = read_le32(dib + 48);
            has_masks = 1;
        }
        if (dib_size >= 56) {
            mask_a = read_le32(dib + 52);
        }
        if (dib_size >= 108) {
            cstype = read_le32(dib + 56);
            (void)cstype;
        }
        if (dib_size >= 124) {
            intent = read_le32(dib + 108);
            profile_data = read_le32(dib + 112);
            profile_size = read_le32(dib + 116);
            (void)intent;
            (void)profile_data;
            (void)profile_size;
        }
    }

    if (width == 0 || height == 0 || width > BMP_MAX_DIM || height > BMP_MAX_DIM) {
        set_err(err, errcap, "invalid dimensions");
        return 0;
    }
    if (planes != 1) {
        set_err(err, errcap, "invalid BMP header");
        return 0;
    }

    if (bit_count != 1 && bit_count != 4 && bit_count != 8 && bit_count != 16 &&
        bit_count != 24 && bit_count != 32) {
        set_err(err, errcap, "unsupported bit depth");
        return 0;
    }

    if (compression == BI_RLE8) {
        if (bit_count != 8) {
            set_err(err, errcap, "invalid compression for bit depth");
            return 0;
        }
    } else if (compression == BI_RLE4) {
        if (bit_count != 4) {
            set_err(err, errcap, "invalid compression for bit depth");
            return 0;
        }
    } else if (compression == BI_BITFIELDS || compression == BI_ALPHABITFIELDS) {
        if (bit_count != 16 && bit_count != 32) {
            set_err(err, errcap, "invalid compression for bit depth");
            return 0;
        }
    } else if (compression != BI_RGB && compression != BI_JPEG && compression != BI_PNG) {
        set_err(err, errcap, "invalid compression for bit depth");
        return 0;
    }

    BMP_DEBUG("dimensions=%ux%u (%s), bit_depth=%u, compression=%s\n",
              width, height, top_down ? "top-down" : "bottom-up",
              bit_count, bmp_compression_name(compression));

    size_t palette_offset = 14u + dib_size;
    if ((compression == BI_BITFIELDS || compression == BI_ALPHABITFIELDS) && dib_size == 40) {
        size_t mask_bytes = (compression == BI_ALPHABITFIELDS) ? 16u : 12u;
        if (palette_offset + mask_bytes > size_bound) {
            set_err(err, errcap, "file too small");
            return 0;
        }
        if (palette_offset + mask_bytes > data_offset) {
            set_err(err, errcap, "invalid data offset");
            return 0;
        }
        mask_r = read_le32(data + palette_offset);
        mask_g = read_le32(data + palette_offset + 4);
        mask_b = read_le32(data + palette_offset + 8);
        if (mask_bytes == 16u) {
            mask_a = read_le32(data + palette_offset + 12);
        }
        has_masks = 1;
        palette_offset += mask_bytes;
    }

    if (has_masks && (mask_r || mask_g || mask_b)) {
        BMP_DEBUG("bitfields masks r=0x%08x g=0x%08x b=0x%08x a=0x%08x\n",
                  mask_r, mask_g, mask_b, mask_a);
    }

    if (dib_size >= 108) {
        BMP_DEBUG("color_space=0x%08x\n", cstype);
    }
    if (dib_size >= 124) {
        BMP_DEBUG("icc_profile_offset=%u size=%u intent=0x%08x\n",
                  profile_data, profile_size, intent);
    }

    if (compression == BI_JPEG) {
        BMP_DEBUG("delegating to JPEG decoder\n");
        if (data_offset >= size_bound) {
            set_err(err, errcap, "invalid data offset");
            return 0;
        }
        return cupidimage_load_jpeg(data + data_offset, size_bound - data_offset, out, err, errcap);
    }
    if (compression == BI_PNG) {
        BMP_DEBUG("delegating to PNG decoder\n");
        if (data_offset >= size_bound) {
            set_err(err, errcap, "invalid data offset");
            return 0;
        }
        return cupidimage_load_png(data + data_offset, size_bound - data_offset, out, err, errcap);
    }

    uint32_t palette_entries = 0;
    uint8_t palette[256 * 4];
    if (bit_count <= 8) {
        uint32_t max_entries = 1u << bit_count;
        if (dib_size == 12) {
            palette_entries = max_entries;
        } else {
            palette_entries = clr_used ? clr_used : max_entries;
        }
        if (palette_entries == 0) {
            set_err(err, errcap, "missing color palette");
            return 0;
        }
        if (palette_entries > max_entries) {
            set_err(err, errcap, "palette too large");
            return 0;
        }
        size_t entry_size = (dib_size == 12) ? 3u : 4u;
        size_t palette_bytes = (size_t)palette_entries * entry_size;
        if (palette_offset + palette_bytes > size_bound) {
            set_err(err, errcap, "file too small");
            return 0;
        }
        if (palette_offset + palette_bytes > data_offset) {
            set_err(err, errcap, "palette too large");
            return 0;
        }
        memset(palette, 0, sizeof(palette));
        int any_alpha = 0;
        for (uint32_t i = 0; i < palette_entries; i++) {
            const uint8_t *entry = data + palette_offset + i * entry_size;
            uint8_t b = entry[0];
            uint8_t g = entry[1];
            uint8_t r = entry[2];
            uint8_t a = 255;
            if (entry_size == 4u) {
                a = entry[3];
                if (a != 0) {
                    any_alpha = 1;
                }
            }
            size_t idx = (size_t)i * 4u;
            palette[idx + 0] = r;
            palette[idx + 1] = g;
            palette[idx + 2] = b;
            palette[idx + 3] = a;
        }
        if (entry_size == 4u && !any_alpha && dib_size < 108) {
            for (uint32_t i = 0; i < palette_entries; i++) {
                palette[i * 4u + 3u] = 255;
            }
        }
        BMP_DEBUG("palette_size=%u entries\n", palette_entries);
    }

    size_t pixels = (size_t)width * (size_t)height;
    if (width == 0 || height == 0 || pixels > SIZE_MAX / 4u) {
        set_err(err, errcap, "invalid dimensions");
        return 0;
    }
    size_t out_size = pixels * 4u;
    out->rgba = (uint8_t *)malloc(out_size);
    if (!out->rgba) {
        set_err(err, errcap, "out of memory");
        return 0;
    }
    out->width = width;
    out->height = height;
    BMP_DEBUG("allocating %zu bytes for RGBA output\n", out_size);

    int use_bitfields = 0;
    if ((bit_count == 16 || bit_count == 32) &&
        (compression == BI_BITFIELDS || compression == BI_ALPHABITFIELDS ||
         (has_masks && (mask_r || mask_g || mask_b)))) {
        use_bitfields = 1;
        if (mask_r == 0 || mask_g == 0 || mask_b == 0) {
            free(out->rgba);
            out->rgba = NULL;
            set_err(err, errcap, "invalid compression for bit depth");
            return 0;
        }
    }

    bmp_mask_info rmask;
    bmp_mask_info gmask;
    bmp_mask_info bmask;
    bmp_mask_info amask;
    bmp_mask_init(&rmask, mask_r);
    bmp_mask_init(&gmask, mask_g);
    bmp_mask_init(&bmask, mask_b);
    bmp_mask_init(&amask, mask_a);

    int metadata_premultiplied = 0;
    if ((dib_size >= 108 && cstype == LCS_GM_IMAGES) || (dib_size >= 124 && intent == LCS_GM_IMAGES)) {
        metadata_premultiplied = 1;
    }

    if (compression == BI_RLE8 || compression == BI_RLE4) {
        if (bit_count > 8 || palette_entries == 0) {
            free(out->rgba);
            out->rgba = NULL;
            set_err(err, errcap, "missing color palette");
            return 0;
        }
        if (data_offset >= size_bound) {
            free(out->rgba);
            out->rgba = NULL;
            set_err(err, errcap, "invalid data offset");
            return 0;
        }
        uint8_t *indices = (uint8_t *)calloc(pixels, 1u);
        if (!indices) {
            free(out->rgba);
            out->rgba = NULL;
            set_err(err, errcap, "out of memory");
            return 0;
        }
        int ok = 0;
        if (compression == BI_RLE8) {
            ok = bmp_decode_rle8(data + data_offset, size_bound - data_offset,
                                 width, height, top_down, indices, err, errcap);
        } else {
            ok = bmp_decode_rle4(data + data_offset, size_bound - data_offset,
                                 width, height, top_down, indices, err, errcap);
        }
        if (!ok) {
            free(indices);
            free(out->rgba);
            out->rgba = NULL;
            return 0;
        }
        for (uint32_t y = 0; y < height; y++) {
            for (uint32_t x = 0; x < width; x++) {
                uint8_t idx = indices[(size_t)y * (size_t)width + x];
                if (idx >= palette_entries) {
                    free(indices);
                    free(out->rgba);
                    out->rgba = NULL;
                    set_err(err, errcap, "invalid palette index");
                    return 0;
                }
                const uint8_t *p = palette + (size_t)idx * 4u;
                size_t out_idx = ((size_t)y * (size_t)width + x) * 4u;
                out->rgba[out_idx + 0] = p[0];
                out->rgba[out_idx + 1] = p[1];
                out->rgba[out_idx + 2] = p[2];
                out->rgba[out_idx + 3] = p[3];
            }
        }
        free(indices);
        return 1;
    }

    uint64_t row_bits = (uint64_t)width * (uint64_t)bit_count;
    uint64_t row_stride64 = ((row_bits + 31u) / 32u) * 4u;
    if (row_stride64 > SIZE_MAX) {
        free(out->rgba);
        out->rgba = NULL;
        set_err(err, errcap, "invalid dimensions");
        return 0;
    }
    size_t row_stride = (size_t)row_stride64;
    if (height > 0 && row_stride > SIZE_MAX / (size_t)height) {
        free(out->rgba);
        out->rgba = NULL;
        set_err(err, errcap, "invalid dimensions");
        return 0;
    }
    size_t pixel_bytes = row_stride * (size_t)height;
    if (data_offset + pixel_bytes > size_bound) {
        free(out->rgba);
        out->rgba = NULL;
        set_err(err, errcap, "truncated pixel data");
        return 0;
    }

    const uint8_t *pixel_data = data + data_offset;
    if (bit_count <= 8) {
        for (uint32_t row = 0; row < height; row++) {
            uint32_t dst_y = top_down ? row : (height - 1u - row);
            const uint8_t *src_row = pixel_data + (size_t)row * row_stride;
            uint8_t *dst = out->rgba + (size_t)dst_y * (size_t)width * 4u;
            for (uint32_t x = 0; x < width; x++) {
                uint8_t idx = 0;
                if (bit_count == 8) {
                    idx = src_row[x];
                } else if (bit_count == 4) {
                    uint8_t byte = src_row[x / 2u];
                    idx = (x & 1u) ? (byte & 0x0Fu) : (uint8_t)(byte >> 4);
                } else if (bit_count == 1) {
                    uint8_t byte = src_row[x / 8u];
                    unsigned shift = 7u - (x % 8u);
                    idx = (uint8_t)(((uint32_t)byte >> shift) & 0x01u);
                }
                if (idx >= palette_entries) {
                    free(out->rgba);
                    out->rgba = NULL;
                    set_err(err, errcap, "invalid palette index");
                    return 0;
                }
                const uint8_t *p = palette + (size_t)idx * 4u;
                dst[x * 4u + 0u] = p[0];
                dst[x * 4u + 1u] = p[1];
                dst[x * 4u + 2u] = p[2];
                dst[x * 4u + 3u] = p[3];
            }
        }
        return 1;
    }

    if (bit_count == 16) {
        bmp_mask_info rm = rmask;
        bmp_mask_info gm = gmask;
        bmp_mask_info bm = bmask;
        bmp_mask_info am = amask;
        int alpha_bits = 0;
        if (!use_bitfields) {
            bmp_mask_init(&rm, 0x7C00u);
            bmp_mask_init(&gm, 0x03E0u);
            bmp_mask_init(&bm, 0x001Fu);
            bmp_mask_init(&am, 0u);
        }
        alpha_bits = am.bits;
        for (uint32_t row = 0; row < height; row++) {
            uint32_t dst_y = top_down ? row : (height - 1u - row);
            const uint8_t *src_row = pixel_data + (size_t)row * row_stride;
            uint8_t *dst = out->rgba + (size_t)dst_y * (size_t)width * 4u;
            for (uint32_t x = 0; x < width; x++) {
                uint32_t pixel = (uint32_t)src_row[x * 2u] | ((uint32_t)src_row[x * 2u + 1u] << 8);
                uint8_t r = bmp_mask_extract(pixel, &rm);
                uint8_t g = bmp_mask_extract(pixel, &gm);
                uint8_t b = bmp_mask_extract(pixel, &bm);
                uint8_t a = (alpha_bits > 0) ? bmp_mask_extract(pixel, &am) : 255;
                dst[x * 4u + 0u] = r;
                dst[x * 4u + 1u] = g;
                dst[x * 4u + 2u] = b;
                dst[x * 4u + 3u] = a;
            }
        }
        if (alpha_bits > 0 && bmp_should_unpremultiply(out->rgba, pixels, metadata_premultiplied)) {
            BMP_DEBUG("converting premultiplied alpha to straight\n");
            bmp_unpremultiply(out->rgba, pixels);
        }
        return 1;
    }

    if (bit_count == 24) {
        for (uint32_t row = 0; row < height; row++) {
            uint32_t dst_y = top_down ? row : (height - 1u - row);
            const uint8_t *src_row = pixel_data + (size_t)row * row_stride;
            uint8_t *dst = out->rgba + (size_t)dst_y * (size_t)width * 4u;
            for (uint32_t x = 0; x < width; x++) {
                const uint8_t *px = src_row + x * 3u;
                dst[x * 4u + 2u] = px[0];
                dst[x * 4u + 1u] = px[1];
                dst[x * 4u + 0u] = px[2];
                dst[x * 4u + 3u] = 255;
            }
        }
        return 1;
    }

    if (bit_count == 32) {
        bmp_mask_info rm = rmask;
        bmp_mask_info gm = gmask;
        bmp_mask_info bm = bmask;
        bmp_mask_info am = amask;
        int alpha_bits = 0;
        if (use_bitfields) {
            alpha_bits = am.bits;
        }
        for (uint32_t row = 0; row < height; row++) {
            uint32_t dst_y = top_down ? row : (height - 1u - row);
            const uint8_t *src_row = pixel_data + (size_t)row * row_stride;
            uint8_t *dst = out->rgba + (size_t)dst_y * (size_t)width * 4u;
            for (uint32_t x = 0; x < width; x++) {
                const uint8_t *px = src_row + x * 4u;
                if (use_bitfields) {
                    uint32_t pixel = (uint32_t)px[0] | ((uint32_t)px[1] << 8) |
                                     ((uint32_t)px[2] << 16) | ((uint32_t)px[3] << 24);
                    uint8_t r = bmp_mask_extract(pixel, &rm);
                    uint8_t g = bmp_mask_extract(pixel, &gm);
                    uint8_t b = bmp_mask_extract(pixel, &bm);
                    uint8_t a = (alpha_bits > 0) ? bmp_mask_extract(pixel, &am) : 255;
                    dst[x * 4u + 0u] = r;
                    dst[x * 4u + 1u] = g;
                    dst[x * 4u + 2u] = b;
                    dst[x * 4u + 3u] = a;
                } else {
                    dst[x * 4u + 2u] = px[0];
                    dst[x * 4u + 1u] = px[1];
                    dst[x * 4u + 0u] = px[2];
                    dst[x * 4u + 3u] = 255;
                }
            }
        }
        if (use_bitfields && alpha_bits > 0 &&
            bmp_should_unpremultiply(out->rgba, pixels, metadata_premultiplied)) {
            BMP_DEBUG("converting premultiplied alpha to straight\n");
            bmp_unpremultiply(out->rgba, pixels);
        }
        return 1;
    }

    free(out->rgba);
    out->rgba = NULL;
    set_err(err, errcap, "unsupported bit depth");
    return 0;
}

int cupidimage_load_bmp_file(const char *path, cupidimage_image *out,
                             char *err, size_t errcap) {
    return cupidimage_load_image_file_via_memory(path, out, err, errcap, cupidimage_load_bmp);
}
