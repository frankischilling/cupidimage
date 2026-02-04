#include "cupidimage.h"
#include "cupidimage_internal.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <math.h>

#ifdef CUPIDIMAGE_TGA_DEBUG
#define TGA_DEBUG(...) fprintf(stderr, "TGA: " __VA_ARGS__)
#else
#define TGA_DEBUG(...) ((void)0)
#endif

#define TGA_MAX_DIM 65535u
#define TGA_HEADER_SIZE 18
#define TGA_FOOTER_SIZE 26
#define TGA_EXTENSION_AREA_SIZE 495
#define TGA_COLOR_CORRECTION_SIZE 2048

typedef struct tga_header {
    uint8_t id_length;
    uint8_t color_map_type;
    uint8_t image_type;
    uint16_t cm_first_entry;
    uint16_t cm_length;
    uint8_t cm_entry_size;
    uint16_t x_origin;
    uint16_t y_origin;
    uint16_t width;
    uint16_t height;
    uint8_t pixel_depth;
    uint8_t image_descriptor;
} tga_header;

typedef struct tga_extension_info {
    int has_extension;
    uint16_t gamma_num;
    uint16_t gamma_denom;
    uint32_t color_correction_offset;
    uint32_t postage_stamp_offset;
    uint8_t attributes_type;
} tga_extension_info;

static void set_err(char *err, size_t errcap, const char *msg) {
    if (err && errcap) {
        snprintf(err, errcap, "%s", msg);
    }
}

static uint16_t read_le16(const unsigned char *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t read_le32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int mul_overflow_size_t(size_t a, size_t b, size_t *out) {
    if (a == 0 || b == 0) {
        *out = 0;
        return 0;
    }
    if (a > SIZE_MAX / b) {
        return 1;
    }
    *out = a * b;
    return 0;
}

static uint8_t scale_5_to_8(uint8_t v) {
    return (uint8_t)((v * 255u + 15u) / 31u);
}

static uint8_t scale_6_to_8(uint8_t v) {
    return (uint8_t)((v * 255u + 31u) / 63u);
}

static uint8_t scale_16_to_8(uint16_t v) {
    return (uint8_t)(((uint32_t)v * 255u + 32767u) / 65535u);
}

static void tga_convert_rgb555_to_rgba(uint8_t *dest, uint16_t value) {
    uint8_t r = (uint8_t)((value >> 10) & 0x1Fu);
    uint8_t g = (uint8_t)((value >> 5) & 0x1Fu);
    uint8_t b = (uint8_t)(value & 0x1Fu);
    dest[0] = scale_5_to_8(r);
    dest[1] = scale_5_to_8(g);
    dest[2] = scale_5_to_8(b);
    dest[3] = 255;
}

static void tga_convert_rgb5551_to_rgba(uint8_t *dest, uint16_t value, int use_alpha) {
    uint8_t r = (uint8_t)((value >> 10) & 0x1Fu);
    uint8_t g = (uint8_t)((value >> 5) & 0x1Fu);
    uint8_t b = (uint8_t)(value & 0x1Fu);
    dest[0] = scale_5_to_8(r);
    dest[1] = scale_5_to_8(g);
    dest[2] = scale_5_to_8(b);
    if (use_alpha) {
        dest[3] = (value & 0x8000u) ? 255 : 0;
    } else {
        dest[3] = 255;
    }
}

static void tga_convert_rgb565_to_rgba(uint8_t *dest, uint16_t value) {
    uint8_t r = (uint8_t)((value >> 11) & 0x1Fu);
    uint8_t g = (uint8_t)((value >> 5) & 0x3Fu);
    uint8_t b = (uint8_t)(value & 0x1Fu);
    dest[0] = scale_5_to_8(r);
    dest[1] = scale_6_to_8(g);
    dest[2] = scale_5_to_8(b);
    dest[3] = 255;
}

static void tga_convert_gray8_to_rgba(uint8_t *dest, uint8_t gray) {
    dest[0] = gray;
    dest[1] = gray;
    dest[2] = gray;
    dest[3] = 255;
}

static void tga_convert_gray16_to_rgba(uint8_t *dest, uint16_t gray) {
    uint8_t v = scale_16_to_8(gray);
    dest[0] = v;
    dest[1] = v;
    dest[2] = v;
    dest[3] = 255;
}

static int tga_parse_header(const unsigned char *data, size_t size,
                            tga_header *hdr, char *err, size_t errcap) {
    if (size < TGA_HEADER_SIZE) {
        set_err(err, errcap, "file too small for TGA header");
        return 0;
    }
    hdr->id_length = data[0];
    hdr->color_map_type = data[1];
    hdr->image_type = data[2];
    hdr->cm_first_entry = read_le16(data + 3);
    hdr->cm_length = read_le16(data + 5);
    hdr->cm_entry_size = data[7];
    hdr->x_origin = read_le16(data + 8);
    hdr->y_origin = read_le16(data + 10);
    hdr->width = read_le16(data + 12);
    hdr->height = read_le16(data + 14);
    hdr->pixel_depth = data[16];
    hdr->image_descriptor = data[17];
    return 1;
}

static int tga_validate_header(const tga_header *hdr, char *err, size_t errcap) {
    if (hdr->color_map_type > 1) {
        set_err(err, errcap, "invalid color map type");
        return 0;
    }
    if (hdr->image_type == 0) {
        return 1;
    }

    if (hdr->width == 0 || hdr->height == 0) {
        set_err(err, errcap, "invalid dimensions");
        return 0;
    }

    if ((hdr->image_type >= 4 && hdr->image_type <= 8) || hdr->image_type > 11) {
        set_err(err, errcap, "unsupported TGA image type");
        return 0;
    }

    if (hdr->image_type == 1 || hdr->image_type == 9) {
        if (hdr->color_map_type != 1) {
            set_err(err, errcap, "missing color map");
            return 0;
        }
        if (hdr->pixel_depth != 8 && hdr->pixel_depth != 16) {
            set_err(err, errcap, "invalid pixel depth for color-mapped TGA");
            return 0;
        }
        if (hdr->cm_length == 0) {
            set_err(err, errcap, "missing color map");
            return 0;
        }
        if (hdr->cm_entry_size != 15 && hdr->cm_entry_size != 16 &&
            hdr->cm_entry_size != 24 && hdr->cm_entry_size != 32) {
            set_err(err, errcap, "invalid color map entry size");
            return 0;
        }
    } else if (hdr->image_type == 2 || hdr->image_type == 10) {
        if (hdr->color_map_type != 0) {
            set_err(err, errcap, "unexpected color map");
            return 0;
        }
        if (hdr->pixel_depth != 15 && hdr->pixel_depth != 16 &&
            hdr->pixel_depth != 24 && hdr->pixel_depth != 32) {
            set_err(err, errcap, "invalid pixel depth for true-color TGA");
            return 0;
        }
    } else if (hdr->image_type == 3 || hdr->image_type == 11) {
        if (hdr->color_map_type != 0) {
            set_err(err, errcap, "unexpected color map");
            return 0;
        }
        if (hdr->pixel_depth != 8 && hdr->pixel_depth != 16) {
            set_err(err, errcap, "invalid pixel depth for grayscale TGA");
            return 0;
        }
    }

    uint8_t alpha_bits = (uint8_t)(hdr->image_descriptor & 0x0Fu);
    if (hdr->image_type == 1 || hdr->image_type == 3 || hdr->image_type == 9 || hdr->image_type == 11) {
        if (alpha_bits != 0) {
            set_err(err, errcap, "invalid alpha bits");
            return 0;
        }
    } else if (hdr->image_type == 2 || hdr->image_type == 10) {
        if (hdr->pixel_depth == 15 && alpha_bits != 0) {
            set_err(err, errcap, "invalid alpha bits");
            return 0;
        }
        if (hdr->pixel_depth == 16 && alpha_bits > 1) {
            set_err(err, errcap, "invalid alpha bits");
            return 0;
        }
        if (hdr->pixel_depth == 24 && alpha_bits != 0) {
            set_err(err, errcap, "invalid alpha bits");
            return 0;
        }
        if (hdr->pixel_depth == 32 && alpha_bits != 0 && alpha_bits != 8) {
            set_err(err, errcap, "invalid alpha bits");
            return 0;
        }
    }

    return 1;
}

static int tga_load_colormap(const unsigned char *data, size_t size, size_t *offset,
                             const tga_header *hdr, uint8_t **palette_out,
                             uint32_t *palette_count,
                             char *err, size_t errcap) {
    if (hdr->color_map_type != 1) {
        *palette_out = NULL;
        *palette_count = 0;
        return 1;
    }

    uint32_t total_entries = (uint32_t)hdr->cm_first_entry + (uint32_t)hdr->cm_length;
    if (hdr->cm_length == 0 || total_entries < hdr->cm_length) {
        set_err(err, errcap, "invalid color map length");
        return 0;
    }

    uint8_t entry_size = hdr->cm_entry_size;
    uint8_t entry_bytes = (uint8_t)((entry_size + 7u) / 8u);
    if (entry_bytes != 2 && entry_bytes != 3 && entry_bytes != 4) {
        set_err(err, errcap, "invalid color map entry size");
        return 0;
    }

    size_t cmap_bytes = (size_t)hdr->cm_length * (size_t)entry_bytes;
    if (*offset + cmap_bytes > size) {
        set_err(err, errcap, "truncated color map");
        return 0;
    }

    size_t palette_bytes = 0;
    if (mul_overflow_size_t((size_t)total_entries, 4u, &palette_bytes)) {
        set_err(err, errcap, "color map too large");
        return 0;
    }

    uint8_t *palette = (uint8_t *)calloc(1, palette_bytes);
    if (!palette) {
        set_err(err, errcap, "out of memory");
        return 0;
    }

    for (uint32_t i = 0; i < hdr->cm_length; i++) {
        size_t src_off = *offset + (size_t)i * entry_bytes;
        uint32_t idx = (uint32_t)hdr->cm_first_entry + i;
        uint8_t *dest = palette + (size_t)idx * 4u;
        if (entry_size == 15) {
            uint16_t v = read_le16(data + src_off);
            tga_convert_rgb555_to_rgba(dest, v);
        } else if (entry_size == 16) {
            uint16_t v = read_le16(data + src_off);
            tga_convert_rgb5551_to_rgba(dest, v, 1);
        } else if (entry_size == 24) {
            dest[2] = data[src_off + 0];
            dest[1] = data[src_off + 1];
            dest[0] = data[src_off + 2];
            dest[3] = 255;
        } else {
            dest[2] = data[src_off + 0];
            dest[1] = data[src_off + 1];
            dest[0] = data[src_off + 2];
            dest[3] = data[src_off + 3];
        }
    }

    *offset += cmap_bytes;
    *palette_out = palette;
    *palette_count = total_entries;
    return 1;
}

static int tga_decode_rle(uint8_t *dest, const unsigned char *src, size_t src_size,
                          uint32_t pixel_count, uint8_t bytes_per_pixel,
                          char *err, size_t errcap) {
    size_t src_pos = 0;
    size_t out_pixels = 0;

    while (out_pixels < pixel_count) {
        if (src_pos >= src_size) {
            set_err(err, errcap, "truncated RLE data");
            return 0;
        }
        uint8_t header = src[src_pos++];
        uint32_t count = (uint32_t)(header & 0x7Fu) + 1u;
        if (header & 0x80u) {
            if (src_pos + bytes_per_pixel > src_size) {
                set_err(err, errcap, "truncated RLE data");
                return 0;
            }
            if (out_pixels + count > pixel_count) {
                set_err(err, errcap, "RLE overflow");
                return 0;
            }
            const uint8_t *px = src + src_pos;
            for (uint32_t i = 0; i < count; i++) {
                memcpy(dest + (out_pixels + i) * bytes_per_pixel, px, bytes_per_pixel);
            }
            src_pos += bytes_per_pixel;
            out_pixels += count;
        } else {
            size_t bytes = (size_t)count * bytes_per_pixel;
            if (src_pos + bytes > src_size) {
                set_err(err, errcap, "truncated RLE data");
                return 0;
            }
            if (out_pixels + count > pixel_count) {
                set_err(err, errcap, "RLE overflow");
                return 0;
            }
            memcpy(dest + out_pixels * bytes_per_pixel, src + src_pos, bytes);
            src_pos += bytes;
            out_pixels += count;
        }
    }

    return 1;
}

static void tga_flip_vertical(uint8_t *rgba, uint32_t width, uint32_t height) {
    size_t row_bytes = (size_t)width * 4u;
    for (uint32_t y = 0; y < height / 2u; y++) {
        uint8_t *row_top = rgba + (size_t)y * row_bytes;
        uint8_t *row_bottom = rgba + (size_t)(height - 1u - y) * row_bytes;
        for (size_t i = 0; i < row_bytes; i++) {
            uint8_t tmp = row_top[i];
            row_top[i] = row_bottom[i];
            row_bottom[i] = tmp;
        }
    }
}

static void tga_mirror_horizontal(uint8_t *rgba, uint32_t width, uint32_t height) {
    for (uint32_t y = 0; y < height; y++) {
        uint8_t *row = rgba + (size_t)y * (size_t)width * 4u;
        for (uint32_t x = 0; x < width / 2u; x++) {
            uint8_t *left = row + (size_t)x * 4u;
            uint8_t *right = row + (size_t)(width - 1u - x) * 4u;
            uint8_t tmp0 = left[0];
            uint8_t tmp1 = left[1];
            uint8_t tmp2 = left[2];
            uint8_t tmp3 = left[3];
            left[0] = right[0];
            left[1] = right[1];
            left[2] = right[2];
            left[3] = right[3];
            right[0] = tmp0;
            right[1] = tmp1;
            right[2] = tmp2;
            right[3] = tmp3;
        }
    }
}

static int tga_parse_footer(const unsigned char *data, size_t size,
                            uint32_t *ext_offset, uint32_t *dev_offset) {
    if (size < TGA_FOOTER_SIZE) {
        return 0;
    }
    const unsigned char *footer = data + size - TGA_FOOTER_SIZE;
    if (memcmp(footer + 8, "TRUEVISION-XFILE.\0", 18) != 0) {
        return 0;
    }
    *ext_offset = read_le32(footer);
    *dev_offset = read_le32(footer + 4);
    return 1;
}

static int tga_parse_extension_area(const unsigned char *data, size_t size,
                                    uint32_t offset, cupidimage_tga_metadata *meta,
                                    tga_extension_info *info,
                                    char *err, size_t errcap) {
    if (offset == 0) {
        return 1;
    }
    if ((size_t)offset + TGA_EXTENSION_AREA_SIZE > size) {
        set_err(err, errcap, "extension area out of bounds");
        return 0;
    }

    const unsigned char *p = data + offset;
    uint16_t ext_size = read_le16(p);
    if (ext_size != TGA_EXTENSION_AREA_SIZE) {
        set_err(err, errcap, "invalid extension area size");
        return 0;
    }

    if (info) {
        info->has_extension = 1;
        info->gamma_num = read_le16(p + 478);
        info->gamma_denom = read_le16(p + 480);
        info->color_correction_offset = read_le32(p + 482);
        info->postage_stamp_offset = read_le32(p + 486);
        info->attributes_type = p[494];
    }

    if (meta) {
        memcpy(meta->author, p + 2, 41);
        meta->author[40] = '\0';
        memcpy(meta->comments, p + 43, 324);
        meta->comments[323] = '\0';
        for (int i = 0; i < 6; i++) {
            meta->timestamp[i] = read_le16(p + 367 + i * 2);
        }
        memcpy(meta->job_id, p + 379, 41);
        meta->job_id[40] = '\0';
        for (int i = 0; i < 3; i++) {
            meta->job_time[i] = read_le16(p + 420 + i * 2);
        }
        memcpy(meta->software_id, p + 426, 41);
        meta->software_id[40] = '\0';
        meta->software_version_number = read_le16(p + 467);
        meta->software_version_letter = p[469];
        meta->key_color = read_le32(p + 470);
        meta->pixel_aspect_ratio_numerator = read_le16(p + 474);
        meta->pixel_aspect_ratio_denominator = read_le16(p + 476);
        meta->gamma_numerator = read_le16(p + 478);
        meta->gamma_denominator = read_le16(p + 480);
        meta->color_correction_offset = read_le32(p + 482);
        meta->postage_stamp_offset = read_le32(p + 486);
        meta->scan_line_offset = read_le32(p + 490);
        meta->attributes_type = p[494];
    }

    return 1;
}

static int tga_decode_thumbnail(const unsigned char *data, size_t size,
                                uint32_t offset, cupidimage_tga_metadata *meta,
                                char *err, size_t errcap) {
    if (!meta || offset == 0) {
        return 1;
    }
    if ((size_t)offset + 2 > size) {
        set_err(err, errcap, "truncated thumbnail");
        return 0;
    }
    uint8_t w = data[offset];
    uint8_t h = data[offset + 1];
    meta->thumbnail_width = w;
    meta->thumbnail_height = h;
    if (w == 0 || h == 0) {
        return 1;
    }

    size_t pixel_count = (size_t)w * (size_t)h;
    size_t src_bytes = 0;
    if (mul_overflow_size_t(pixel_count, 3u, &src_bytes)) {
        set_err(err, errcap, "thumbnail too large");
        return 0;
    }

    if ((size_t)offset + 2 + src_bytes > size) {
        set_err(err, errcap, "truncated thumbnail");
        return 0;
    }

    size_t rgba_bytes = 0;
    if (mul_overflow_size_t(pixel_count, 4u, &rgba_bytes)) {
        set_err(err, errcap, "thumbnail too large");
        return 0;
    }

    uint8_t *rgba = (uint8_t *)malloc(rgba_bytes);
    if (!rgba) {
        set_err(err, errcap, "out of memory");
        return 0;
    }

    const uint8_t *src = data + offset + 2;
    for (size_t i = 0; i < pixel_count; i++) {
        const uint8_t *px = src + i * 3u;
        uint8_t *dst = rgba + i * 4u;
        dst[0] = px[2];
        dst[1] = px[1];
        dst[2] = px[0];
        dst[3] = 255;
    }

    meta->thumbnail_rgba = rgba;
    return 1;
}

static void tga_apply_gamma(uint8_t *rgba, size_t pixel_count,
                            uint16_t gamma_num, uint16_t gamma_denom) {
    if (gamma_num == 0 || gamma_denom == 0) {
        return;
    }
    double gamma = (double)gamma_num / (double)gamma_denom;
    if (gamma <= 0.0) {
        return;
    }
    double inv_gamma = 1.0 / gamma;
    uint8_t lut[256];
    for (int i = 0; i < 256; i++) {
        double v = (double)i / 255.0;
        double corrected = pow(v, inv_gamma);
        int out = (int)(corrected * 255.0 + 0.5);
        if (out < 0) out = 0;
        if (out > 255) out = 255;
        lut[i] = (uint8_t)out;
    }

    for (size_t i = 0; i < pixel_count; i++) {
        uint8_t *px = rgba + i * 4u;
        px[0] = lut[px[0]];
        px[1] = lut[px[1]];
        px[2] = lut[px[2]];
    }
}

static int tga_apply_color_correction(uint8_t *rgba, size_t pixel_count,
                                      const unsigned char *data, size_t size,
                                      uint32_t table_offset,
                                      int apply_alpha,
                                      char *err, size_t errcap) {
    if (table_offset == 0) {
        return 1;
    }
    if ((size_t)table_offset + TGA_COLOR_CORRECTION_SIZE > size) {
        set_err(err, errcap, "color correction table out of bounds");
        return 0;
    }

    const unsigned char *p = data + table_offset;
    uint8_t lut_r[256];
    uint8_t lut_g[256];
    uint8_t lut_b[256];
    uint8_t lut_a[256];

    for (int i = 0; i < 256; i++) {
        uint16_t a = read_le16(p + i * 8 + 0);
        uint16_t r = read_le16(p + i * 8 + 2);
        uint16_t g = read_le16(p + i * 8 + 4);
        uint16_t b = read_le16(p + i * 8 + 6);
        lut_a[i] = scale_16_to_8(a);
        lut_r[i] = scale_16_to_8(r);
        lut_g[i] = scale_16_to_8(g);
        lut_b[i] = scale_16_to_8(b);
    }

    for (size_t i = 0; i < pixel_count; i++) {
        uint8_t *px = rgba + i * 4u;
        px[0] = lut_r[px[0]];
        px[1] = lut_g[px[1]];
        px[2] = lut_b[px[2]];
        if (apply_alpha) {
            px[3] = lut_a[px[3]];
        }
    }

    return 1;
}

static void tga_zero_output(cupidimage_image *out) {
    out->width = 0;
    out->height = 0;
    out->rgba = NULL;
    out->hotspot_x = 0;
    out->hotspot_y = 0;
}

int cupidimage_load_tga_with_metadata(const unsigned char *data, size_t size,
                                      cupidimage_image *out,
                                      cupidimage_tga_metadata *meta,
                                      int apply_corrections,
                                      char *err, size_t errcap) {
    if (!data || !out) {
        set_err(err, errcap, "invalid arguments");
        return 0;
    }

    if (meta) {
        memset(meta, 0, sizeof(*meta));
    }

    tga_zero_output(out);

    tga_header hdr;
    if (!tga_parse_header(data, size, &hdr, err, errcap)) {
        return 0;
    }
    if (!tga_validate_header(&hdr, err, errcap)) {
        return 0;
    }

    size_t offset = TGA_HEADER_SIZE;
    if (offset + hdr.id_length > size) {
        set_err(err, errcap, "truncated image ID");
        return 0;
    }
    offset += hdr.id_length;

    tga_extension_info extinfo;
    memset(&extinfo, 0, sizeof(extinfo));

    uint32_t ext_offset = 0;
    uint32_t dev_offset = 0;
    (void)dev_offset;
    if (tga_parse_footer(data, size, &ext_offset, &dev_offset)) {
        if (ext_offset > 0) {
            if (!tga_parse_extension_area(data, size, ext_offset, meta, &extinfo, err, errcap)) {
                return 0;
            }
        }
    } else if (size >= TGA_EXTENSION_AREA_SIZE) {
        uint32_t guess = (uint32_t)(size - TGA_EXTENSION_AREA_SIZE);
        if (read_le16(data + guess) == TGA_EXTENSION_AREA_SIZE) {
            if (!tga_parse_extension_area(data, size, guess, meta, &extinfo, err, errcap)) {
                return 0;
            }
        }
    }

    if (hdr.image_type == 0) {
        if (meta && extinfo.postage_stamp_offset > 0) {
            if (!tga_decode_thumbnail(data, size, extinfo.postage_stamp_offset, meta, err, errcap)) {
                cupidimage_free_tga_metadata(meta);
                return 0;
            }
        }
        return 1;
    }

    uint8_t *palette = NULL;
    uint32_t palette_count = 0;
    if (hdr.color_map_type == 1) {
        if (!tga_load_colormap(data, size, &offset, &hdr, &palette, &palette_count, err, errcap)) {
            return 0;
        }
    }

    size_t pixel_count = 0;
    if (mul_overflow_size_t(hdr.width, hdr.height, &pixel_count) || pixel_count == 0) {
        free(palette);
        set_err(err, errcap, "invalid dimensions");
        return 0;
    }

    uint8_t bytes_per_pixel = (uint8_t)((hdr.pixel_depth + 7u) / 8u);
    size_t raw_size = 0;
    if (mul_overflow_size_t(pixel_count, bytes_per_pixel, &raw_size)) {
        free(palette);
        set_err(err, errcap, "image too large");
        return 0;
    }

    const uint8_t *raw = NULL;
    uint8_t *raw_buf = NULL;

    int is_rle = (hdr.image_type >= 9);
    if (is_rle) {
        raw_buf = (uint8_t *)malloc(raw_size);
        if (!raw_buf) {
            free(palette);
            set_err(err, errcap, "out of memory");
            return 0;
        }
        if (!tga_decode_rle(raw_buf, data + offset, size - offset,
                            (uint32_t)pixel_count, bytes_per_pixel,
                            err, errcap)) {
            free(raw_buf);
            free(palette);
            return 0;
        }
        raw = raw_buf;
    } else {
        if (offset + raw_size > size) {
            free(palette);
            set_err(err, errcap, "truncated pixel data");
            return 0;
        }
        raw = data + offset;
    }

    size_t rgba_size = 0;
    if (mul_overflow_size_t(pixel_count, 4u, &rgba_size)) {
        free(raw_buf);
        free(palette);
        set_err(err, errcap, "image too large");
        return 0;
    }

    uint8_t *rgba = (uint8_t *)malloc(rgba_size);
    if (!rgba) {
        free(raw_buf);
        free(palette);
        set_err(err, errcap, "out of memory");
        return 0;
    }

    uint8_t alpha_bits = (uint8_t)(hdr.image_descriptor & 0x0Fu);
    int use_alpha_bit = 0;
    if (hdr.pixel_depth == 16) {
        if (extinfo.has_extension) {
            use_alpha_bit = (extinfo.attributes_type != 0);
        } else {
            use_alpha_bit = (alpha_bits > 0);
        }
    }
    int use_alpha = (alpha_bits > 0);
    if (extinfo.has_extension && extinfo.attributes_type == 0) {
        use_alpha = 0;
    }

    uint8_t base_type = hdr.image_type;
    if (base_type >= 9) {
        base_type = (uint8_t)(base_type - 8);
    }

    int ok = 1;
    if (base_type == 1) {
        if (!palette || palette_count == 0) {
            set_err(err, errcap, "missing color map");
            ok = 0;
        } else if (hdr.pixel_depth == 8) {
            for (size_t i = 0; i < pixel_count; i++) {
                uint8_t idx = raw[i];
                if (idx >= palette_count) {
                    set_err(err, errcap, "color map index out of range");
                    ok = 0;
                    break;
                }
                memcpy(rgba + i * 4u, palette + (size_t)idx * 4u, 4u);
            }
        } else if (hdr.pixel_depth == 16) {
            for (size_t i = 0; i < pixel_count; i++) {
                uint16_t idx = read_le16(raw + i * 2u);
                if (idx >= palette_count) {
                    set_err(err, errcap, "color map index out of range");
                    ok = 0;
                    break;
                }
                memcpy(rgba + i * 4u, palette + (size_t)idx * 4u, 4u);
            }
        } else {
            set_err(err, errcap, "invalid pixel depth for color-mapped TGA");
            ok = 0;
        }
    } else if (base_type == 2) {
        if (hdr.pixel_depth == 15) {
            for (size_t i = 0; i < pixel_count; i++) {
                uint16_t v = read_le16(raw + i * 2u);
                tga_convert_rgb555_to_rgba(rgba + i * 4u, v);
            }
        } else if (hdr.pixel_depth == 16) {
            for (size_t i = 0; i < pixel_count; i++) {
                uint16_t v = read_le16(raw + i * 2u);
                if (use_alpha_bit) {
                    tga_convert_rgb5551_to_rgba(rgba + i * 4u, v, use_alpha);
                } else {
                    tga_convert_rgb565_to_rgba(rgba + i * 4u, v);
                }
            }
        } else if (hdr.pixel_depth == 24) {
            for (size_t i = 0; i < pixel_count; i++) {
                const uint8_t *src = raw + i * 3u;
                uint8_t *dst = rgba + i * 4u;
                dst[0] = src[2];
                dst[1] = src[1];
                dst[2] = src[0];
                dst[3] = 255;
            }
        } else if (hdr.pixel_depth == 32) {
            for (size_t i = 0; i < pixel_count; i++) {
                const uint8_t *src = raw + i * 4u;
                uint8_t *dst = rgba + i * 4u;
                dst[0] = src[2];
                dst[1] = src[1];
                dst[2] = src[0];
                dst[3] = use_alpha ? src[3] : 255;
            }
        } else {
            set_err(err, errcap, "invalid pixel depth for true-color TGA");
            ok = 0;
        }
    } else if (base_type == 3) {
        if (hdr.pixel_depth == 8) {
            for (size_t i = 0; i < pixel_count; i++) {
                tga_convert_gray8_to_rgba(rgba + i * 4u, raw[i]);
            }
        } else if (hdr.pixel_depth == 16) {
            for (size_t i = 0; i < pixel_count; i++) {
                uint16_t v = read_le16(raw + i * 2u);
                tga_convert_gray16_to_rgba(rgba + i * 4u, v);
            }
        } else {
            set_err(err, errcap, "invalid pixel depth for grayscale TGA");
            ok = 0;
        }
    } else {
        set_err(err, errcap, "unsupported TGA image type");
        ok = 0;
    }

    free(raw_buf);
    raw_buf = NULL;
    free(palette);
    palette = NULL;

    if (!ok) {
        free(rgba);
        return 0;
    }

    uint8_t origin = (uint8_t)((hdr.image_descriptor >> 4) & 0x03u);
    if ((origin & 0x02u) == 0) {
        tga_flip_vertical(rgba, hdr.width, hdr.height);
    }
    if (origin & 0x01u) {
        tga_mirror_horizontal(rgba, hdr.width, hdr.height);
    }

    if (apply_corrections) {
        tga_apply_gamma(rgba, pixel_count, extinfo.gamma_num, extinfo.gamma_denom);
        if (!tga_apply_color_correction(rgba, pixel_count, data, size,
                                        extinfo.color_correction_offset,
                                        use_alpha, err, errcap)) {
            free(rgba);
            if (meta) {
                cupidimage_free_tga_metadata(meta);
            }
            return 0;
        }
    }

    if (meta && extinfo.postage_stamp_offset > 0) {
        if (!tga_decode_thumbnail(data, size, extinfo.postage_stamp_offset, meta, err, errcap)) {
            free(rgba);
            cupidimage_free_tga_metadata(meta);
            return 0;
        }
    }

    out->width = hdr.width;
    out->height = hdr.height;
    out->rgba = rgba;
    out->hotspot_x = 0;
    out->hotspot_y = 0;
    return 1;
}

int cupidimage_load_tga(const unsigned char *data, size_t size,
                        cupidimage_image *out, char *err, size_t errcap) {
    return cupidimage_load_tga_with_metadata(data, size, out, NULL, 1, err, errcap);
}

int cupidimage_load_tga_file(const char *path, cupidimage_image *out,
                             char *err, size_t errcap) {
    return cupidimage_load_image_file_via_memory(path, out, err, errcap, cupidimage_load_tga);
}

void cupidimage_free_tga_metadata(cupidimage_tga_metadata *meta) {
    if (!meta) {
        return;
    }
    free(meta->thumbnail_rgba);
    memset(meta, 0, sizeof(*meta));
}
