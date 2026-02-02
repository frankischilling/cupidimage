#include "cupidimage.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

#ifdef CUPIDIMAGE_TIFF_DEBUG
#define TIFF_DEBUG(...) fprintf(stderr, "TIFF: " __VA_ARGS__)
#else
#define TIFF_DEBUG(...) ((void)0)
#endif

#define TIFF_TAG_IMAGE_WIDTH 256
#define TIFF_TAG_IMAGE_LENGTH 257
#define TIFF_TAG_BITS_PER_SAMPLE 258
#define TIFF_TAG_COMPRESSION 259
#define TIFF_TAG_PHOTOMETRIC 262
#define TIFF_TAG_FILL_ORDER 266
#define TIFF_TAG_STRIP_OFFSETS 273
#define TIFF_TAG_ORIENTATION 274
#define TIFF_TAG_SAMPLES_PER_PIXEL 277
#define TIFF_TAG_ROWS_PER_STRIP 278
#define TIFF_TAG_STRIP_BYTE_COUNTS 279
#define TIFF_TAG_PLANAR_CONFIGURATION 284
#define TIFF_TAG_GROUP3_OPTIONS 292
#define TIFF_TAG_GROUP4_OPTIONS 293
#define TIFF_TAG_PREDICTOR 317
#define TIFF_TAG_COLOR_MAP 320
#define TIFF_TAG_SUB_IFD 330
#define TIFF_TAG_TILE_WIDTH 322
#define TIFF_TAG_TILE_LENGTH 323
#define TIFF_TAG_TILE_OFFSETS 324
#define TIFF_TAG_TILE_BYTE_COUNTS 325
#define TIFF_TAG_EXTRA_SAMPLES 338
#define TIFF_TAG_SAMPLE_FORMAT 339
#define TIFF_TAG_JPEG_TABLES 347
#define TIFF_TAG_JPEG_INTERCHANGE_FORMAT 513
#define TIFF_TAG_JPEG_INTERCHANGE_FORMAT_LENGTH 514
#define TIFF_TAG_YCBCR_COEFFICIENTS 529
#define TIFF_TAG_YCBCR_SUBSAMPLING 530
#define TIFF_TAG_YCBCR_POSITIONING 531
#define TIFF_TAG_REFERENCE_BLACK_WHITE 532

#define TIFF_COMPRESSION_NONE 1
#define TIFF_COMPRESSION_CCITT_RLE 2
#define TIFF_COMPRESSION_CCITT_G3 3
#define TIFF_COMPRESSION_CCITT_G4 4
#define TIFF_COMPRESSION_LZW 5
#define TIFF_COMPRESSION_JPEG 7
#define TIFF_COMPRESSION_DEFLATE 8
#define TIFF_COMPRESSION_ADOBE_DEFLATE 32946
#define TIFF_COMPRESSION_PACKBITS 32773

#define TIFF_PHOTOMETRIC_WHITE_IS_ZERO 0
#define TIFF_PHOTOMETRIC_BLACK_IS_ZERO 1
#define TIFF_PHOTOMETRIC_RGB 2
#define TIFF_PHOTOMETRIC_PALETTE 3
#define TIFF_PHOTOMETRIC_CMYK 5
#define TIFF_PHOTOMETRIC_YCBCR 6
#define TIFF_PHOTOMETRIC_CIELAB 8

#define TIFF_PLANAR_CHUNKY 1
#define TIFF_PLANAR_SEPARATE 2

#define TIFF_EXTRA_SAMPLE_ASSOC_ALPHA 1
#define TIFF_EXTRA_SAMPLE_UNASSOC_ALPHA 2

#define TIFF_SAMPLE_FORMAT_UINT 1

#define TIFF_FILL_ORDER_MSB2LSB 1
#define TIFF_FILL_ORDER_LSB2MSB 2

static void set_err(char *err, size_t errcap, const char *msg) {
    if (err && errcap) {
        snprintf(err, errcap, "%s", msg);
    }
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

typedef struct tiff_context {
    const uint8_t *data;
    size_t size;
    int big_endian;
    int big_tiff;
    uint64_t first_ifd;
    size_t entry_size;
    size_t value_size;
} tiff_context;

static uint16_t read_u16(const tiff_context *ctx, const uint8_t *p) {
    if (ctx->big_endian) {
        return (uint16_t)((p[0] << 8) | p[1]);
    }
    return (uint16_t)(p[0] | (p[1] << 8));
}

static void write_u16(const tiff_context *ctx, uint8_t *p, uint16_t v) {
    if (ctx->big_endian) {
        p[0] = (uint8_t)(v >> 8);
        p[1] = (uint8_t)(v & 0xFFu);
    } else {
        p[0] = (uint8_t)(v & 0xFFu);
        p[1] = (uint8_t)(v >> 8);
    }
}

static uint32_t read_u32(const tiff_context *ctx, const uint8_t *p) {
    if (ctx->big_endian) {
        return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
    }
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t read_u64(const tiff_context *ctx, const uint8_t *p) {
    if (ctx->big_endian) {
        return ((uint64_t)p[0] << 56) | ((uint64_t)p[1] << 48) | ((uint64_t)p[2] << 40) |
               ((uint64_t)p[3] << 32) | ((uint64_t)p[4] << 24) | ((uint64_t)p[5] << 16) |
               ((uint64_t)p[6] << 8) | (uint64_t)p[7];
    }
    return (uint64_t)p[0] | ((uint64_t)p[1] << 8) | ((uint64_t)p[2] << 16) |
           ((uint64_t)p[3] << 24) | ((uint64_t)p[4] << 32) | ((uint64_t)p[5] << 40) |
           ((uint64_t)p[6] << 48) | ((uint64_t)p[7] << 56);
}

static size_t tiff_type_size(uint16_t type) {
    switch (type) {
        case 1: /* BYTE */
        case 2: /* ASCII */
        case 6: /* SBYTE */
        case 7: /* UNDEFINED */
            return 1;
        case 3: /* SHORT */
        case 8: /* SSHORT */
            return 2;
        case 4: /* LONG */
        case 9: /* SLONG */
        case 11: /* FLOAT */
            return 4;
        case 5: /* RATIONAL */
        case 10: /* SRATIONAL */
        case 12: /* DOUBLE */
        case 16: /* LONG8 */
        case 17: /* SLONG8 */
        case 18: /* IFD8 */
            return 8;
        default:
            return 0;
    }
}

typedef struct tiff_entry {
    uint16_t tag;
    uint16_t type;
    uint64_t count;
    const uint8_t *value_ptr;
    size_t value_size;
} tiff_entry;

static int tiff_read_entry(const tiff_context *ctx, const uint8_t *entry_ptr, tiff_entry *entry,
                           char *err, size_t errcap) {
    entry->tag = read_u16(ctx, entry_ptr);
    entry->type = read_u16(ctx, entry_ptr + 2);
    if (ctx->big_tiff) {
        entry->count = read_u64(ctx, entry_ptr + 4);
    } else {
        entry->count = read_u32(ctx, entry_ptr + 4);
    }

    size_t type_size = tiff_type_size(entry->type);
    if (type_size == 0) {
        entry->value_ptr = NULL;
        entry->value_size = 0;
        return 1;
    }

    if (entry->count > 0 && entry->count > (uint64_t)(SIZE_MAX / type_size)) {
        set_err(err, errcap, "TIFF tag value too large");
        return 0;
    }
    entry->value_size = (size_t)entry->count * type_size;

    uint64_t value_offset = 0;
    if (ctx->big_tiff) {
        value_offset = read_u64(ctx, entry_ptr + 12);
    } else {
        value_offset = read_u32(ctx, entry_ptr + 8);
    }

    if (entry->value_size <= ctx->value_size) {
        entry->value_ptr = entry_ptr + (ctx->big_tiff ? 12 : 8);
        return 1;
    }

    if (value_offset > ctx->size || value_offset + entry->value_size > ctx->size) {
        set_err(err, errcap, "TIFF tag value offset out of range");
        return 0;
    }
    entry->value_ptr = ctx->data + (size_t)value_offset;
    return 1;
}

static int tiff_entry_get_u64(const tiff_context *ctx, const tiff_entry *entry, uint64_t index, uint64_t *out) {
    if (!entry->value_ptr || index >= entry->count) {
        return 0;
    }

    size_t type_size = tiff_type_size(entry->type);
    size_t offset = (size_t)index * type_size;
    if (offset + type_size > entry->value_size) {
        return 0;
    }
    const uint8_t *p = entry->value_ptr + offset;

    switch (entry->type) {
        case 1: /* BYTE */
        case 2: /* ASCII */
        case 6: /* SBYTE */
        case 7: /* UNDEFINED */
            *out = p[0];
            return 1;
        case 3: /* SHORT */
        case 8: /* SSHORT */
        {
            uint16_t v = read_u16(ctx, p);
            if (ctx->big_endian && entry->count == 1 && entry->value_size <= ctx->value_size) {
                uint16_t tail = (uint16_t)((p[2] << 8) | p[3]);
                if (v == 0 && tail != 0) {
                    v = tail;
                }
            }
            *out = v;
            return 1;
        }
        case 4: /* LONG */
        case 9: /* SLONG */
        case 11: /* FLOAT */
            *out = read_u32(ctx, p);
            return 1;
        case 5: /* RATIONAL */
        case 10: /* SRATIONAL */
        case 12: /* DOUBLE */
        case 16: /* LONG8 */
        case 17: /* SLONG8 */
        case 18: /* IFD8 */
            *out = read_u64(ctx, p);
            return 1;
        default:
            return 0;
    }
}

typedef struct tiff_image_info {
    uint32_t width;
    uint32_t height;
    uint16_t bits_per_sample[8];
    uint16_t bits_count;
    uint16_t samples_per_pixel;
    uint16_t compression;
    uint16_t photometric;
    uint16_t planar_config;
    uint32_t rows_per_strip;
    uint16_t fill_order;
    uint16_t predictor;
    uint16_t sample_format;
    uint16_t orientation;
    uint16_t ycbcr_subsampling[2];
    uint16_t ycbcr_positioning;
    uint32_t group3_options;
    uint32_t group4_options;
    uint16_t extra_samples[8];
    uint16_t extra_count;
    uint64_t *strip_offsets;
    uint64_t *strip_byte_counts;
    uint64_t strip_offsets_count;
    uint64_t strip_byte_counts_count;
    uint64_t *tile_offsets;
    uint64_t *tile_byte_counts;
    uint64_t tile_offsets_count;
    uint64_t tile_byte_counts_count;
    uint32_t tile_width;
    uint32_t tile_length;
    uint16_t *color_map;
    uint64_t color_map_count;
    const uint8_t *jpeg_tables;
    size_t jpeg_tables_size;
    uint64_t jpeg_if_offset;
    uint64_t jpeg_if_bytecount;
} tiff_image_info;

static void tiff_info_init(tiff_image_info *info) {
    memset(info, 0, sizeof(*info));
    info->samples_per_pixel = 1;
    info->compression = TIFF_COMPRESSION_NONE;
    info->photometric = 0xFFFFu;
    info->planar_config = TIFF_PLANAR_CHUNKY;
    info->fill_order = TIFF_FILL_ORDER_MSB2LSB;
    info->predictor = 1;
    info->sample_format = TIFF_SAMPLE_FORMAT_UINT;
    info->orientation = 1;
    info->ycbcr_subsampling[0] = 2;
    info->ycbcr_subsampling[1] = 2;
    info->ycbcr_positioning = 1;
}

static void tiff_info_free(tiff_image_info *info) {
    free(info->strip_offsets);
    free(info->strip_byte_counts);
    free(info->tile_offsets);
    free(info->tile_byte_counts);
    free(info->color_map);
    info->strip_offsets = NULL;
    info->strip_byte_counts = NULL;
    info->tile_offsets = NULL;
    info->tile_byte_counts = NULL;
    info->color_map = NULL;
}

static int tiff_parse_header(const unsigned char *data, size_t size, tiff_context *ctx,
                             char *err, size_t errcap) {
    if (size < 8) {
        set_err(err, errcap, "truncated TIFF header");
        return 0;
    }

    if (data[0] == 'I' && data[1] == 'I') {
        ctx->big_endian = 0;
    } else if (data[0] == 'M' && data[1] == 'M') {
        ctx->big_endian = 1;
    } else {
        set_err(err, errcap, "invalid TIFF byte order");
        return 0;
    }

    ctx->data = data;
    ctx->size = size;

    uint16_t version = read_u16(ctx, data + 2);
    if (version == 42) {
        ctx->big_tiff = 0;
        ctx->entry_size = 12;
        ctx->value_size = 4;
        uint32_t off = read_u32(ctx, data + 4);
        if (off == 0 || off >= size) {
            set_err(err, errcap, "invalid TIFF IFD offset");
            return 0;
        }
        ctx->first_ifd = off;
        return 1;
    }

    if (version == 43) {
        if (size < 16) {
            set_err(err, errcap, "truncated BigTIFF header");
            return 0;
        }
        uint16_t bytesize = read_u16(ctx, data + 4);
        uint16_t zero = read_u16(ctx, data + 6);
        if (bytesize != 8 || zero != 0) {
            set_err(err, errcap, "invalid BigTIFF header");
            return 0;
        }
        uint64_t off = read_u64(ctx, data + 8);
        if (off == 0 || off >= size) {
            set_err(err, errcap, "invalid BigTIFF IFD offset");
            return 0;
        }
        ctx->big_tiff = 1;
        ctx->entry_size = 20;
        ctx->value_size = 8;
        ctx->first_ifd = off;
        return 1;
    }

    set_err(err, errcap, "unsupported TIFF version");
    return 0;
}

static int tiff_read_ifd_header(const tiff_context *ctx, uint64_t offset,
                                uint64_t *entry_count, uint64_t *next_ifd,
                                char *err, size_t errcap) {
    if (offset >= ctx->size) {
        set_err(err, errcap, "invalid TIFF IFD offset");
        return 0;
    }

    uint64_t count = 0;
    size_t count_size = ctx->big_tiff ? 8 : 2;
    if (offset + count_size > ctx->size) {
        set_err(err, errcap, "truncated TIFF IFD");
        return 0;
    }

    if (ctx->big_tiff) {
        count = read_u64(ctx, ctx->data + offset);
    } else {
        count = read_u16(ctx, ctx->data + offset);
    }

    uint64_t entries_size = count * (uint64_t)ctx->entry_size;
    uint64_t entries_start = offset + count_size;
    uint64_t after_entries = entries_start + entries_size;
    uint64_t next_offset_size = ctx->big_tiff ? 8 : 4;

    if (count > 0 && entries_size / (uint64_t)ctx->entry_size != count) {
        set_err(err, errcap, "TIFF IFD entry overflow");
        return 0;
    }

    if (after_entries + next_offset_size > ctx->size) {
        set_err(err, errcap, "truncated TIFF IFD entries");
        return 0;
    }

    uint64_t next = 0;
    if (ctx->big_tiff) {
        next = read_u64(ctx, ctx->data + after_entries);
    } else {
        next = read_u32(ctx, ctx->data + after_entries);
    }
    if (next != 0 && next >= ctx->size) {
        set_err(err, errcap, "invalid TIFF next IFD offset");
        return 0;
    }

    *entry_count = count;
    *next_ifd = next;
    return 1;
}

static int tiff_ifd_list_append(uint64_t **list, size_t *count, size_t *cap, uint64_t off) {
    if (*count >= *cap) {
        size_t next_cap = (*cap == 0) ? 8 : (*cap * 2u);
        uint64_t *next = (uint64_t *)realloc(*list, next_cap * sizeof(uint64_t));
        if (!next) {
            return 0;
        }
        *list = next;
        *cap = next_cap;
    }
    (*list)[(*count)++] = off;
    return 1;
}

static int tiff_collect_subifds(const tiff_context *ctx, uint64_t ifd_offset,
                                uint64_t **list, size_t *count, size_t *cap,
                                char *err, size_t errcap) {
    uint64_t entry_count = 0;
    uint64_t next_ifd = 0;
    if (!tiff_read_ifd_header(ctx, ifd_offset, &entry_count, &next_ifd, err, errcap)) {
        return 0;
    }
    (void)next_ifd;

    uint64_t entries_start = ifd_offset + (ctx->big_tiff ? 8 : 2);
    for (uint64_t i = 0; i < entry_count; i++) {
        uint64_t entry_off = entries_start + i * (uint64_t)ctx->entry_size;
        if (entry_off + ctx->entry_size > ctx->size) {
            set_err(err, errcap, "truncated TIFF IFD entry");
            return 0;
        }
        tiff_entry entry;
        if (!tiff_read_entry(ctx, ctx->data + entry_off, &entry, err, errcap)) {
            return 0;
        }
        if (entry.tag != TIFF_TAG_SUB_IFD) {
            continue;
        }
        for (uint64_t j = 0; j < entry.count; j++) {
            uint64_t off = 0;
            if (!tiff_entry_get_u64(ctx, &entry, j, &off)) {
                set_err(err, errcap, "invalid SubIFD");
                return 0;
            }
            if (off == 0 || off >= ctx->size) {
                set_err(err, errcap, "invalid SubIFD offset");
                return 0;
            }
            if (!tiff_ifd_list_append(list, count, cap, off)) {
                set_err(err, errcap, "out of memory");
                return 0;
            }
        }
    }
    return 1;
}

static int tiff_parse_image_info(const tiff_context *ctx, uint64_t ifd_offset,
                                 tiff_image_info *info, char *err, size_t errcap) {
    uint64_t entry_count = 0;
    uint64_t next_ifd = 0;
    if (!tiff_read_ifd_header(ctx, ifd_offset, &entry_count, &next_ifd, err, errcap)) {
        return 0;
    }
    (void)next_ifd;

    uint64_t entries_start = ifd_offset + (ctx->big_tiff ? 8 : 2);
    for (uint64_t i = 0; i < entry_count; i++) {
        uint64_t entry_off = entries_start + i * (uint64_t)ctx->entry_size;
        if (entry_off + ctx->entry_size > ctx->size) {
            set_err(err, errcap, "truncated TIFF IFD entry");
            return 0;
        }
        tiff_entry entry;
        if (!tiff_read_entry(ctx, ctx->data + entry_off, &entry, err, errcap)) {
            return 0;
        }

        switch (entry.tag) {
            case TIFF_TAG_IMAGE_WIDTH: {
                uint64_t v = 0;
                if (tiff_entry_get_u64(ctx, &entry, 0, &v)) {
                    info->width = (v > UINT32_MAX) ? 0 : (uint32_t)v;
                }
                break;
            }
            case TIFF_TAG_IMAGE_LENGTH: {
                uint64_t v = 0;
                if (tiff_entry_get_u64(ctx, &entry, 0, &v)) {
                    info->height = (v > UINT32_MAX) ? 0 : (uint32_t)v;
                }
                break;
            }
            case TIFF_TAG_BITS_PER_SAMPLE: {
                if (entry.count > 8) {
                    set_err(err, errcap, "too many BitsPerSample values");
                    return 0;
                }
                info->bits_count = (uint16_t)entry.count;
                for (uint64_t j = 0; j < entry.count; j++) {
                    uint64_t v = 0;
                    if (!tiff_entry_get_u64(ctx, &entry, j, &v)) {
                        set_err(err, errcap, "invalid BitsPerSample");
                        return 0;
                    }
                    info->bits_per_sample[j] = (uint16_t)v;
                }
                break;
            }
            case TIFF_TAG_COMPRESSION: {
                uint64_t v = 0;
                if (tiff_entry_get_u64(ctx, &entry, 0, &v)) {
                    info->compression = (uint16_t)v;
                }
                break;
            }
            case TIFF_TAG_PHOTOMETRIC: {
                uint64_t v = 0;
                if (tiff_entry_get_u64(ctx, &entry, 0, &v)) {
                    info->photometric = (uint16_t)v;
                }
                break;
            }
            case TIFF_TAG_FILL_ORDER: {
                uint64_t v = 0;
                if (tiff_entry_get_u64(ctx, &entry, 0, &v)) {
                    info->fill_order = (uint16_t)v;
                }
                break;
            }
            case TIFF_TAG_STRIP_OFFSETS: {
                free(info->strip_offsets);
                info->strip_offsets = NULL;
                info->strip_offsets_count = entry.count;
                if (entry.count == 0 || entry.count > SIZE_MAX / sizeof(uint64_t)) {
                    set_err(err, errcap, "invalid StripOffsets");
                    return 0;
                }
                info->strip_offsets = (uint64_t *)malloc((size_t)entry.count * sizeof(uint64_t));
                if (!info->strip_offsets) {
                    set_err(err, errcap, "out of memory");
                    return 0;
                }
                for (uint64_t j = 0; j < entry.count; j++) {
                    uint64_t v = 0;
                    if (!tiff_entry_get_u64(ctx, &entry, j, &v)) {
                        set_err(err, errcap, "invalid StripOffsets");
                        return 0;
                    }
                    info->strip_offsets[j] = v;
                }
                break;
            }
            case TIFF_TAG_STRIP_BYTE_COUNTS: {
                free(info->strip_byte_counts);
                info->strip_byte_counts = NULL;
                if (entry.count == 0 || entry.count > SIZE_MAX / sizeof(uint64_t)) {
                    set_err(err, errcap, "invalid StripByteCounts");
                    return 0;
                }
                info->strip_byte_counts_count = entry.count;
                info->strip_byte_counts = (uint64_t *)malloc((size_t)entry.count * sizeof(uint64_t));
                if (!info->strip_byte_counts) {
                    set_err(err, errcap, "out of memory");
                    return 0;
                }
                for (uint64_t j = 0; j < entry.count; j++) {
                    uint64_t v = 0;
                    if (!tiff_entry_get_u64(ctx, &entry, j, &v)) {
                        set_err(err, errcap, "invalid StripByteCounts");
                        return 0;
                    }
                    info->strip_byte_counts[j] = v;
                }
                break;
            }
            case TIFF_TAG_SAMPLES_PER_PIXEL: {
                uint64_t v = 0;
                if (tiff_entry_get_u64(ctx, &entry, 0, &v)) {
                    info->samples_per_pixel = (uint16_t)v;
                }
                break;
            }
            case TIFF_TAG_ROWS_PER_STRIP: {
                uint64_t v = 0;
                if (tiff_entry_get_u64(ctx, &entry, 0, &v)) {
                    info->rows_per_strip = (v > UINT32_MAX) ? 0 : (uint32_t)v;
                }
                break;
            }
            case TIFF_TAG_PLANAR_CONFIGURATION: {
                uint64_t v = 0;
                if (tiff_entry_get_u64(ctx, &entry, 0, &v)) {
                    info->planar_config = (uint16_t)v;
                }
                break;
            }
            case TIFF_TAG_GROUP3_OPTIONS: {
                uint64_t v = 0;
                if (tiff_entry_get_u64(ctx, &entry, 0, &v)) {
                    info->group3_options = (uint32_t)v;
                }
                break;
            }
            case TIFF_TAG_GROUP4_OPTIONS: {
                uint64_t v = 0;
                if (tiff_entry_get_u64(ctx, &entry, 0, &v)) {
                    info->group4_options = (uint32_t)v;
                }
                break;
            }
            case TIFF_TAG_PREDICTOR: {
                uint64_t v = 0;
                if (tiff_entry_get_u64(ctx, &entry, 0, &v)) {
                    info->predictor = (uint16_t)v;
                }
                break;
            }
            case TIFF_TAG_COLOR_MAP: {
                free(info->color_map);
                info->color_map = NULL;
                if (entry.count == 0 || entry.count > SIZE_MAX / sizeof(uint16_t)) {
                    set_err(err, errcap, "invalid ColorMap");
                    return 0;
                }
                info->color_map = (uint16_t *)malloc((size_t)entry.count * sizeof(uint16_t));
                if (!info->color_map) {
                    set_err(err, errcap, "out of memory");
                    return 0;
                }
                info->color_map_count = entry.count;
                for (uint64_t j = 0; j < entry.count; j++) {
                    uint64_t v = 0;
                    if (!tiff_entry_get_u64(ctx, &entry, j, &v)) {
                        set_err(err, errcap, "invalid ColorMap");
                        return 0;
                    }
                    info->color_map[j] = (uint16_t)v;
                }
                break;
            }
            case TIFF_TAG_TILE_WIDTH: {
                uint64_t v = 0;
                if (tiff_entry_get_u64(ctx, &entry, 0, &v)) {
                    info->tile_width = (v > UINT32_MAX) ? 0 : (uint32_t)v;
                }
                break;
            }
            case TIFF_TAG_TILE_LENGTH: {
                uint64_t v = 0;
                if (tiff_entry_get_u64(ctx, &entry, 0, &v)) {
                    info->tile_length = (v > UINT32_MAX) ? 0 : (uint32_t)v;
                }
                break;
            }
            case TIFF_TAG_TILE_OFFSETS: {
                free(info->tile_offsets);
                info->tile_offsets = NULL;
                info->tile_offsets_count = entry.count;
                if (entry.count == 0 || entry.count > SIZE_MAX / sizeof(uint64_t)) {
                    set_err(err, errcap, "invalid TileOffsets");
                    return 0;
                }
                info->tile_offsets = (uint64_t *)malloc((size_t)entry.count * sizeof(uint64_t));
                if (!info->tile_offsets) {
                    set_err(err, errcap, "out of memory");
                    return 0;
                }
                for (uint64_t j = 0; j < entry.count; j++) {
                    uint64_t v = 0;
                    if (!tiff_entry_get_u64(ctx, &entry, j, &v)) {
                        set_err(err, errcap, "invalid TileOffsets");
                        return 0;
                    }
                    info->tile_offsets[j] = v;
                }
                break;
            }
            case TIFF_TAG_TILE_BYTE_COUNTS: {
                free(info->tile_byte_counts);
                info->tile_byte_counts = NULL;
                info->tile_byte_counts_count = entry.count;
                if (entry.count == 0 || entry.count > SIZE_MAX / sizeof(uint64_t)) {
                    set_err(err, errcap, "invalid TileByteCounts");
                    return 0;
                }
                info->tile_byte_counts = (uint64_t *)malloc((size_t)entry.count * sizeof(uint64_t));
                if (!info->tile_byte_counts) {
                    set_err(err, errcap, "out of memory");
                    return 0;
                }
                for (uint64_t j = 0; j < entry.count; j++) {
                    uint64_t v = 0;
                    if (!tiff_entry_get_u64(ctx, &entry, j, &v)) {
                        set_err(err, errcap, "invalid TileByteCounts");
                        return 0;
                    }
                    info->tile_byte_counts[j] = v;
                }
                break;
            }
            case TIFF_TAG_EXTRA_SAMPLES: {
                if (entry.count > 8) {
                    set_err(err, errcap, "too many ExtraSamples values");
                    return 0;
                }
                info->extra_count = (uint16_t)entry.count;
                for (uint64_t j = 0; j < entry.count; j++) {
                    uint64_t v = 0;
                    if (!tiff_entry_get_u64(ctx, &entry, j, &v)) {
                        set_err(err, errcap, "invalid ExtraSamples");
                        return 0;
                    }
                    info->extra_samples[j] = (uint16_t)v;
                }
                break;
            }
            case TIFF_TAG_SAMPLE_FORMAT: {
                uint64_t v = 0;
                if (tiff_entry_get_u64(ctx, &entry, 0, &v)) {
                    info->sample_format = (uint16_t)v;
                }
                break;
            }
            case TIFF_TAG_ORIENTATION: {
                uint64_t v = 0;
                if (tiff_entry_get_u64(ctx, &entry, 0, &v)) {
                    info->orientation = (uint16_t)v;
                }
                break;
            }
            case TIFF_TAG_YCBCR_SUBSAMPLING: {
                uint64_t v0 = 0;
                uint64_t v1 = 0;
                if (entry.count >= 2 &&
                    tiff_entry_get_u64(ctx, &entry, 0, &v0) &&
                    tiff_entry_get_u64(ctx, &entry, 1, &v1)) {
                    info->ycbcr_subsampling[0] = (uint16_t)v0;
                    info->ycbcr_subsampling[1] = (uint16_t)v1;
                }
                break;
            }
            case TIFF_TAG_YCBCR_POSITIONING: {
                uint64_t v = 0;
                if (tiff_entry_get_u64(ctx, &entry, 0, &v)) {
                    info->ycbcr_positioning = (uint16_t)v;
                }
                break;
            }
            case TIFF_TAG_JPEG_TABLES: {
                info->jpeg_tables = entry.value_ptr;
                info->jpeg_tables_size = entry.value_size;
                break;
            }
            case TIFF_TAG_JPEG_INTERCHANGE_FORMAT: {
                uint64_t v = 0;
                if (tiff_entry_get_u64(ctx, &entry, 0, &v)) {
                    info->jpeg_if_offset = v;
                }
                break;
            }
            case TIFF_TAG_JPEG_INTERCHANGE_FORMAT_LENGTH: {
                uint64_t v = 0;
                if (tiff_entry_get_u64(ctx, &entry, 0, &v)) {
                    info->jpeg_if_bytecount = v;
                }
                break;
            }
            default:
                break;
        }
    }

    if (info->width == 0 || info->height == 0) {
        set_err(err, errcap, "invalid TIFF dimensions");
        return 0;
    }
    if (info->photometric == 0xFFFFu) {
        set_err(err, errcap, "missing PhotometricInterpretation");
        return 0;
    }
    if (info->planar_config != TIFF_PLANAR_CHUNKY && info->planar_config != TIFF_PLANAR_SEPARATE) {
        set_err(err, errcap, "invalid TIFF planar configuration");
        return 0;
    }
    if (info->predictor != 1 && info->predictor != 2) {
        set_err(err, errcap, "TIFF predictor not supported");
        return 0;
    }
    if (info->sample_format == 0) {
        info->sample_format = TIFF_SAMPLE_FORMAT_UINT;
    }
    if (info->sample_format != TIFF_SAMPLE_FORMAT_UINT &&
        info->sample_format != 2 &&
        info->sample_format != 3) {
        set_err(err, errcap, "TIFF sample format not supported");
        return 0;
    }
    if ((info->tile_offsets == NULL || info->tile_offsets_count == 0) &&
        (info->strip_offsets == NULL || info->strip_offsets_count == 0)) {
        set_err(err, errcap, "missing StripOffsets/TileOffsets");
        return 0;
    }

    if (info->rows_per_strip == 0) {
        info->rows_per_strip = info->height;
    }
    if (info->rows_per_strip > info->height) {
        info->rows_per_strip = info->height;
    }

    if (info->compression != TIFF_COMPRESSION_NONE) {
        if (info->tile_offsets && info->tile_offsets_count > 0) {
            if (info->tile_byte_counts == NULL) {
                set_err(err, errcap, "missing TileByteCounts");
                return 0;
            }
        } else if (info->strip_byte_counts == NULL) {
            set_err(err, errcap, "missing StripByteCounts");
            return 0;
        }
    }

    return 1;
}

static int tiff_packbits_decode(const uint8_t *src, size_t src_size,
                                uint8_t *dst, size_t dst_size,
                                char *err, size_t errcap) {
    size_t pos = 0;
    size_t out = 0;
    while (out < dst_size) {
        if (pos >= src_size) {
            set_err(err, errcap, "truncated PackBits data");
            return 0;
        }
        int8_t n = (int8_t)src[pos++];
        if (n >= 0) {
            size_t count = (size_t)n + 1u;
            if (pos + count > src_size || out + count > dst_size) {
                set_err(err, errcap, "invalid PackBits data");
                return 0;
            }
            memcpy(dst + out, src + pos, count);
            pos += count;
            out += count;
        } else if (n != -128) {
            size_t count = (size_t)(1 - n);
            if (pos >= src_size || out + count > dst_size) {
                set_err(err, errcap, "invalid PackBits data");
                return 0;
            }
            uint8_t v = src[pos++];
            for (size_t i = 0; i < count; i++) {
                dst[out++] = v;
            }
        }
    }
    return 1;
}

typedef struct tiff_bitstream {
    const uint8_t *data;
    size_t size;
    size_t pos;
    uint32_t bitbuf;
    int bitcount;
} tiff_bitstream;

static int tiff_bs_fill(tiff_bitstream *bs, int need) {
    while (bs->bitcount < need && bs->pos < bs->size) {
        bs->bitbuf |= (uint32_t)bs->data[bs->pos++] << bs->bitcount;
        bs->bitcount += 8;
    }
    return bs->bitcount >= need;
}

static uint32_t tiff_bs_read(tiff_bitstream *bs, int bits) {
    if (!tiff_bs_fill(bs, bits)) {
        return 0;
    }
    uint32_t val = bs->bitbuf & ((1u << bits) - 1u);
    bs->bitbuf >>= bits;
    bs->bitcount -= bits;
    return val;
}

static void tiff_bs_align(tiff_bitstream *bs) {
    int drop = bs->bitcount & 7;
    if (drop) {
        bs->bitbuf >>= drop;
        bs->bitcount -= drop;
    }
}

static unsigned tiff_reverse_bits(unsigned v, int bits) {
    unsigned r = 0;
    for (int i = 0; i < bits; i++) {
        r = (r << 1) | (v & 1u);
        v >>= 1;
    }
    return r;
}

typedef struct tiff_huff_table {
    int maxbits;
    uint16_t *table;
} tiff_huff_table;

static void tiff_huff_free(tiff_huff_table *ht) {
    free(ht->table);
    ht->table = NULL;
    ht->maxbits = 0;
}

static int tiff_huff_build(tiff_huff_table *ht, const uint8_t *lengths, int num, int maxbits) {
    int count[16] = {0};
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
        return 0;
    }

    int next_code[16];
    int code = 0;
    count[0] = 0;
    for (int bits = 1; bits <= maxbits; bits++) {
        code = (code + count[bits - 1]) << 1;
        next_code[bits] = code;
    }

    ht->maxbits = maxlen;
    ht->table = (uint16_t *)malloc(((size_t)1u << maxlen) * sizeof(uint16_t));
    if (!ht->table) {
        return 0;
    }
    size_t table_size = (size_t)1u << maxlen;
    for (size_t i = 0; i < table_size; i++) {
        ht->table[i] = 0xFFFFu;
    }

    for (int sym = 0; sym < num; sym++) {
        int len = lengths[sym];
        if (!len) {
            continue;
        }
        int sym_code = next_code[len]++;
        unsigned rev = tiff_reverse_bits((unsigned)sym_code, len);
        unsigned fill = 1u << (maxlen - len);
        for (unsigned i = 0; i < fill; i++) {
            unsigned idx = (i << len) | rev;
            ht->table[idx] = (uint16_t)((len << 9) | sym);
        }
    }

    return 1;
}

static int tiff_huff_decode(tiff_bitstream *bs, const tiff_huff_table *ht, int *sym) {
    if (!tiff_bs_fill(bs, ht->maxbits)) {
        return 0;
    }
    uint32_t idx = bs->bitbuf & ((1u << ht->maxbits) - 1u);
    uint16_t val = ht->table[idx];
    if (val == 0xFFFFu) {
        return 0;
    }
    int len = val >> 9;
    *sym = val & 0x1FF;
    bs->bitbuf >>= len;
    bs->bitcount -= len;
    return 1;
}

static int tiff_build_fixed_tables(tiff_huff_table *litlen, tiff_huff_table *dist) {
    uint8_t litlen_lengths[288];
    uint8_t dist_lengths[32];

    for (int i = 0; i <= 143; i++) {
        litlen_lengths[i] = 8;
    }
    for (int i = 144; i <= 255; i++) {
        litlen_lengths[i] = 9;
    }
    for (int i = 256; i <= 279; i++) {
        litlen_lengths[i] = 7;
    }
    for (int i = 280; i <= 287; i++) {
        litlen_lengths[i] = 8;
    }
    for (int i = 0; i < 32; i++) {
        dist_lengths[i] = 5;
    }

    if (!tiff_huff_build(litlen, litlen_lengths, 288, 15)) {
        return 0;
    }
    if (!tiff_huff_build(dist, dist_lengths, 32, 15)) {
        tiff_huff_free(litlen);
        return 0;
    }
    return 1;
}

static int tiff_build_dynamic_tables(tiff_bitstream *bs, tiff_huff_table *litlen, tiff_huff_table *dist) {
    int hlit = (int)tiff_bs_read(bs, 5) + 257;
    int hdist = (int)tiff_bs_read(bs, 5) + 1;
    int hclen = (int)tiff_bs_read(bs, 4) + 4;
    if (hlit > 286 || hdist > 30) {
        return 0;
    }

    static const int order[19] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
    };

    uint8_t clen_lengths[19];
    for (int i = 0; i < 19; i++) {
        clen_lengths[i] = 0;
    }
    for (int i = 0; i < hclen; i++) {
        clen_lengths[order[i]] = (uint8_t)tiff_bs_read(bs, 3);
    }

    tiff_huff_table clen_table = {0, NULL};
    if (!tiff_huff_build(&clen_table, clen_lengths, 19, 7)) {
        return 0;
    }

    int total = hlit + hdist;
    uint8_t lengths[316];
    int idx = 0;
    while (idx < total) {
        int sym = 0;
        if (!tiff_huff_decode(bs, &clen_table, &sym)) {
            tiff_huff_free(&clen_table);
            return 0;
        }
        if (sym <= 15) {
            lengths[idx++] = (uint8_t)sym;
        } else if (sym == 16) {
            if (idx == 0) {
                tiff_huff_free(&clen_table);
                return 0;
            }
            int repeat = (int)tiff_bs_read(bs, 2) + 3;
            uint8_t prev = lengths[idx - 1];
            for (int i = 0; i < repeat && idx < total; i++) {
                lengths[idx++] = prev;
            }
        } else if (sym == 17) {
            int repeat = (int)tiff_bs_read(bs, 3) + 3;
            for (int i = 0; i < repeat && idx < total; i++) {
                lengths[idx++] = 0;
            }
        } else if (sym == 18) {
            int repeat = (int)tiff_bs_read(bs, 7) + 11;
            for (int i = 0; i < repeat && idx < total; i++) {
                lengths[idx++] = 0;
            }
        } else {
            tiff_huff_free(&clen_table);
            return 0;
        }
    }

    tiff_huff_free(&clen_table);

    if (!tiff_huff_build(litlen, lengths, hlit, 15)) {
        return 0;
    }
    if (!tiff_huff_build(dist, lengths + hlit, hdist, 15)) {
        tiff_huff_free(litlen);
        return 0;
    }
    return 1;
}

static int tiff_deflate_decode(tiff_bitstream *bs, uint8_t *out, size_t outcap, size_t *outlen) {
    static const int length_base[29] = {
        3, 4, 5, 6, 7, 8, 9, 10,
        11, 13, 15, 17, 19, 23, 27, 31,
        35, 43, 51, 59, 67, 83, 99, 115,
        131, 163, 195, 227, 258
    };
    static const int length_extra[29] = {
        0, 0, 0, 0, 0, 0, 0, 0,
        1, 1, 1, 1, 2, 2, 2, 2,
        3, 3, 3, 3, 4, 4, 4, 4,
        5, 5, 5, 5, 0
    };
    static const int dist_base[30] = {
        1, 2, 3, 4, 5, 7, 9, 13,
        17, 25, 33, 49, 65, 97, 129, 193,
        257, 385, 513, 769, 1025, 1537, 2049, 3073,
        4097, 6145, 8193, 12289, 16385, 24577
    };
    static const int dist_extra[30] = {
        0, 0, 0, 0, 1, 1, 2, 2,
        3, 3, 4, 4, 5, 5, 6, 6,
        7, 7, 8, 8, 9, 9, 10, 10,
        11, 11, 12, 12, 13, 13
    };

    size_t outpos = 0;
    int final_block = 0;

    while (!final_block) {
        if (!tiff_bs_fill(bs, 3)) {
            return 0;
        }
        final_block = (int)tiff_bs_read(bs, 1);
        int btype = (int)tiff_bs_read(bs, 2);

        if (btype == 0) {
            tiff_bs_align(bs);
            if (bs->pos + 4 > bs->size) {
                return 0;
            }
            uint16_t len = (uint16_t)(bs->data[bs->pos] | (bs->data[bs->pos + 1] << 8));
            uint16_t nlen = (uint16_t)(bs->data[bs->pos + 2] | (bs->data[bs->pos + 3] << 8));
            bs->pos += 4;
            if ((uint16_t)~len != nlen) {
                return 0;
            }
            if (bs->pos + len > bs->size) {
                return 0;
            }
            if (outpos + len > outcap) {
                return 0;
            }
            memcpy(out + outpos, bs->data + bs->pos, len);
            bs->pos += len;
            outpos += len;
        } else if (btype == 1 || btype == 2) {
            tiff_huff_table litlen = {0, NULL};
            tiff_huff_table dist = {0, NULL};
            int ok = 0;
            if (btype == 1) {
                ok = tiff_build_fixed_tables(&litlen, &dist);
            } else {
                ok = tiff_build_dynamic_tables(bs, &litlen, &dist);
            }
            if (!ok) {
                tiff_huff_free(&litlen);
                tiff_huff_free(&dist);
                return 0;
            }

            while (1) {
                int sym = 0;
                if (!tiff_huff_decode(bs, &litlen, &sym)) {
                    tiff_huff_free(&litlen);
                    tiff_huff_free(&dist);
                    return 0;
                }
                if (sym < 256) {
                    if (outpos >= outcap) {
                        tiff_huff_free(&litlen);
                        tiff_huff_free(&dist);
                        return 0;
                    }
                    out[outpos++] = (uint8_t)sym;
                } else if (sym == 256) {
                    break;
                } else if (sym <= 285) {
                    int len_idx = sym - 257;
                    int length = length_base[len_idx];
                    int extra = length_extra[len_idx];
                    if (extra) {
                        length += (int)tiff_bs_read(bs, extra);
                    }

                    int dist_sym = 0;
                    if (!tiff_huff_decode(bs, &dist, &dist_sym)) {
                        tiff_huff_free(&litlen);
                        tiff_huff_free(&dist);
                        return 0;
                    }
                    if (dist_sym > 29) {
                        tiff_huff_free(&litlen);
                        tiff_huff_free(&dist);
                        return 0;
                    }
                    int distance = dist_base[dist_sym];
                    int dist_ext = dist_extra[dist_sym];
                    if (dist_ext) {
                        distance += (int)tiff_bs_read(bs, dist_ext);
                    }
                    if (distance <= 0 || (size_t)distance > outpos) {
                        tiff_huff_free(&litlen);
                        tiff_huff_free(&dist);
                        return 0;
                    }
                    if (outpos + (size_t)length > outcap) {
                        tiff_huff_free(&litlen);
                        tiff_huff_free(&dist);
                        return 0;
                    }
                    for (int i = 0; i < length; i++) {
                        out[outpos] = out[outpos - (size_t)distance];
                        outpos++;
                    }
                } else {
                    tiff_huff_free(&litlen);
                    tiff_huff_free(&dist);
                    return 0;
                }
            }

            tiff_huff_free(&litlen);
            tiff_huff_free(&dist);
        } else {
            return 0;
        }
    }

    *outlen = outpos;
    return 1;
}

static int tiff_zlib_decompress(const uint8_t *data, size_t size, uint8_t *out, size_t outcap, size_t *outlen) {
    if (size < 2) {
        return 0;
    }
    uint8_t cmf = data[0];
    uint8_t flg = data[1];
    if ((cmf & 0x0F) != 8) {
        return 0;
    }
    if (((cmf << 8) + flg) % 31 != 0) {
        return 0;
    }
    if (flg & 0x20) {
        return 0;
    }

    tiff_bitstream bs;
    bs.data = data + 2;
    bs.size = size - 2;
    bs.pos = 0;
    bs.bitbuf = 0;
    bs.bitcount = 0;

    if (!tiff_deflate_decode(&bs, out, outcap, outlen)) {
        return 0;
    }

    return 1;
}

static int tiff_inflate(const uint8_t *data, size_t size, uint8_t *out, size_t outcap, size_t *outlen) {
    if (tiff_zlib_decompress(data, size, out, outcap, outlen)) {
        return 1;
    }

    tiff_bitstream bs;
    bs.data = data;
    bs.size = size;
    bs.pos = 0;
    bs.bitbuf = 0;
    bs.bitcount = 0;
    return tiff_deflate_decode(&bs, out, outcap, outlen);
}

typedef struct tiff_lzw {
    const uint8_t *data;
    size_t size;
    size_t pos;
    uint32_t bitbuf;
    int bitcount;
} tiff_lzw;

static int tiff_lzw_read_code(tiff_lzw *lz, int code_size, uint32_t *out) {
    while (lz->bitcount < code_size) {
        if (lz->pos >= lz->size) {
            return 0;
        }
        lz->bitbuf = (lz->bitbuf << 8) | lz->data[lz->pos++];
        lz->bitcount += 8;
    }
    lz->bitcount -= code_size;
    *out = (lz->bitbuf >> lz->bitcount) & ((1u << code_size) - 1u);
    return 1;
}

static int tiff_lzw_decode(const uint8_t *src, size_t src_size,
                           uint8_t *dst, size_t dst_size,
                           char *err, size_t errcap) {
    uint16_t prefix[4096];
    uint8_t suffix[4096];
    uint8_t stack[4096];

    for (int i = 0; i < 4096; i++) {
        prefix[i] = 0;
        suffix[i] = 0;
    }

    const int clear_code = 256;
    const int end_code = 257;
    int code_size = 9;
    int next_code = 258;
    int early_change = 1;

    tiff_lzw lz;
    lz.data = src;
    lz.size = src_size;
    lz.pos = 0;
    lz.bitbuf = 0;
    lz.bitcount = 0;

    size_t out = 0;
    int old_code = -1;
    uint8_t first_char = 0;

    while (1) {
        uint32_t code = 0;
        if (!tiff_lzw_read_code(&lz, code_size, &code)) {
            break;
        }

        if ((int)code == clear_code) {
            code_size = 9;
            next_code = 258;
            old_code = -1;
            continue;
        }
        if ((int)code == end_code) {
            break;
        }

        uint32_t curr = code;
        int stack_top = 0;

        if (old_code == -1) {
            if (code >= 256) {
                set_err(err, errcap, "invalid LZW stream");
                return 0;
            }
            if (out >= dst_size) {
                set_err(err, errcap, "LZW output overflow");
                return 0;
            }
            dst[out++] = (uint8_t)code;
            first_char = (uint8_t)code;
            old_code = (int)code;
            continue;
        }

        if (code >= (uint32_t)next_code) {
            if ((int)code != next_code) {
                set_err(err, errcap, "invalid LZW code");
                return 0;
            }
            curr = (uint32_t)old_code;
            stack[stack_top++] = first_char;
        }

        while (curr >= 256) {
            if (stack_top >= 4096) {
                set_err(err, errcap, "LZW stack overflow");
                return 0;
            }
            stack[stack_top++] = suffix[curr];
            curr = prefix[curr];
        }
        if (stack_top >= 4096) {
            set_err(err, errcap, "LZW stack overflow");
            return 0;
        }
        stack[stack_top++] = (uint8_t)curr;
        first_char = (uint8_t)curr;

        while (stack_top > 0) {
            if (out >= dst_size) {
                set_err(err, errcap, "LZW output overflow");
                return 0;
            }
            dst[out++] = stack[--stack_top];
        }

        if (next_code < 4096) {
            prefix[next_code] = (uint16_t)old_code;
            suffix[next_code] = first_char;
            next_code++;
            if (next_code >= ((1 << code_size) - early_change) && code_size < 12) {
                code_size++;
            }
        }

        old_code = (int)code;
    }

    if (out != dst_size) {
        set_err(err, errcap, "LZW output size mismatch");
        return 0;
    }
    return 1;
}

typedef struct ccitt_code {
    uint16_t code;
    uint8_t length;
    uint16_t run;
} ccitt_code;

static const ccitt_code ccitt_white_term[] = {
    {0x35, 8, 0}, {0x7, 6, 1}, {0x7, 4, 2}, {0x8, 4, 3},
    {0xB, 4, 4}, {0xC, 4, 5}, {0xE, 4, 6}, {0xF, 4, 7},
    {0x13, 5, 8}, {0x14, 5, 9}, {0x7, 5, 10}, {0x8, 5, 11},
    {0x8, 6, 12}, {0x3, 6, 13}, {0x34, 6, 14}, {0x35, 6, 15},
    {0x2A, 6, 16}, {0x2B, 6, 17}, {0x27, 7, 18}, {0xC, 7, 19},
    {0x8, 7, 20}, {0x17, 7, 21}, {0x3, 7, 22}, {0x4, 7, 23},
    {0x28, 7, 24}, {0x2B, 7, 25}, {0x13, 7, 26}, {0x24, 7, 27},
    {0x18, 7, 28}, {0x2, 8, 29}, {0x3, 8, 30}, {0x1A, 8, 31},
    {0x1B, 8, 32}, {0x12, 8, 33}, {0x13, 8, 34}, {0x14, 8, 35},
    {0x15, 8, 36}, {0x16, 8, 37}, {0x17, 8, 38}, {0x28, 8, 39},
    {0x29, 8, 40}, {0x2A, 8, 41}, {0x2B, 8, 42}, {0x2C, 8, 43},
    {0x2D, 8, 44}, {0x4, 8, 45}, {0x5, 8, 46}, {0xA, 8, 47},
    {0xB, 8, 48}, {0x52, 8, 49}, {0x53, 8, 50}, {0x54, 8, 51},
    {0x55, 8, 52}, {0x24, 8, 53}, {0x25, 8, 54}, {0x58, 8, 55},
    {0x59, 8, 56}, {0x5A, 8, 57}, {0x5B, 8, 58}, {0x4A, 8, 59},
    {0x4B, 8, 60}, {0x32, 8, 61}, {0x33, 8, 62}, {0x34, 8, 63}
};

static const ccitt_code ccitt_white_makeup[] = {
    {0x1B, 5, 64}, {0x12, 5, 128}, {0x17, 6, 192}, {0x37, 7, 256},
    {0x36, 8, 320}, {0x37, 8, 384}, {0x64, 8, 448}, {0x65, 8, 512},
    {0x68, 8, 576}, {0x67, 8, 640}, {0xCC, 9, 704}, {0xCD, 9, 768},
    {0xD2, 9, 832}, {0xD3, 9, 896}, {0xD4, 9, 960}, {0xD5, 9, 1024},
    {0xD6, 9, 1088}, {0xD7, 9, 1152}, {0xD8, 9, 1216}, {0xD9, 9, 1280},
    {0xDA, 9, 1344}, {0xDB, 9, 1408}, {0x98, 9, 1472}, {0x99, 9, 1536},
    {0x9A, 9, 1600}, {0x18, 6, 1664}, {0x9B, 9, 1728}, {0x8, 11, 1792},
    {0xC, 11, 1856}, {0xD, 11, 1920}, {0x12, 12, 1984}, {0x13, 12, 2048},
    {0x14, 12, 2112}, {0x15, 12, 2176}, {0x16, 12, 2240}, {0x17, 12, 2304},
    {0x1C, 12, 2368}, {0x1D, 12, 2432}, {0x1E, 12, 2496}, {0x1F, 12, 2560}
};

static const ccitt_code ccitt_black_term[] = {
    {0x37, 10, 0}, {0x2, 3, 1}, {0x3, 2, 2}, {0x2, 2, 3},
    {0x3, 3, 4}, {0x3, 4, 5}, {0x2, 4, 6}, {0x3, 5, 7},
    {0x5, 6, 8}, {0x4, 6, 9}, {0x4, 7, 10}, {0x5, 7, 11},
    {0x7, 7, 12}, {0x4, 8, 13}, {0x7, 8, 14}, {0x18, 9, 15},
    {0x17, 10, 16}, {0x18, 10, 17}, {0x8, 10, 18}, {0x67, 11, 19},
    {0x68, 11, 20}, {0x6C, 11, 21}, {0x37, 11, 22}, {0x28, 11, 23},
    {0x17, 11, 24}, {0x18, 11, 25}, {0xCA, 12, 26}, {0xCB, 12, 27},
    {0xCC, 12, 28}, {0xCD, 12, 29}, {0x68, 12, 30}, {0x69, 12, 31},
    {0x6A, 12, 32}, {0x6B, 12, 33}, {0xD2, 12, 34}, {0xD3, 12, 35},
    {0xD4, 12, 36}, {0xD5, 12, 37}, {0xD6, 12, 38}, {0xD7, 12, 39},
    {0x6C, 12, 40}, {0x6D, 12, 41}, {0xDA, 12, 42}, {0xDB, 12, 43},
    {0x54, 12, 44}, {0x55, 12, 45}, {0x56, 12, 46}, {0x57, 12, 47},
    {0x64, 12, 48}, {0x65, 12, 49}, {0x52, 12, 50}, {0x53, 12, 51},
    {0x24, 12, 52}, {0x37, 12, 53}, {0x38, 12, 54}, {0x27, 12, 55},
    {0x28, 12, 56}, {0x58, 12, 57}, {0x59, 12, 58}, {0x2B, 12, 59},
    {0x2C, 12, 60}, {0x5A, 12, 61}, {0x66, 12, 62}, {0x67, 12, 63}
};

static const ccitt_code ccitt_black_makeup[] = {
    {0xF, 10, 64}, {0xC8, 12, 128}, {0xC9, 12, 192}, {0x5B, 12, 256},
    {0x33, 12, 320}, {0x34, 12, 384}, {0x35, 12, 448}, {0x6C, 13, 512},
    {0x6D, 13, 576}, {0x4A, 13, 640}, {0x4B, 13, 704}, {0x4C, 13, 768},
    {0x4D, 13, 832}, {0x72, 13, 896}, {0x73, 13, 960}, {0x74, 13, 1024},
    {0x75, 13, 1088}, {0x76, 13, 1152}, {0x77, 13, 1216}, {0x52, 13, 1280},
    {0x53, 13, 1344}, {0x54, 13, 1408}, {0x55, 13, 1472}, {0x5A, 13, 1536},
    {0x5B, 13, 1600}, {0x64, 13, 1664}, {0x65, 13, 1728}, {0x8, 11, 1792},
    {0xC, 11, 1856}, {0xD, 11, 1920}, {0x12, 12, 1984}, {0x13, 12, 2048},
    {0x14, 12, 2112}, {0x15, 12, 2176}, {0x16, 12, 2240}, {0x17, 12, 2304},
    {0x1C, 12, 2368}, {0x1D, 12, 2432}, {0x1E, 12, 2496}, {0x1F, 12, 2560}
};

typedef struct ccitt_bitstream {
    const uint8_t *data;
    size_t size;
    size_t pos;
    int bit;
    int fill_order;
} ccitt_bitstream;

static int ccitt_read_bit(ccitt_bitstream *bs, int *out) {
    if (bs->pos >= bs->size) {
        return 0;
    }
    uint8_t byte = bs->data[bs->pos];
    int shift = bs->fill_order == TIFF_FILL_ORDER_LSB2MSB ? bs->bit : (7 - bs->bit);
    *out = (byte >> shift) & 1;
    bs->bit++;
    if (bs->bit == 8) {
        bs->bit = 0;
        bs->pos++;
    }
    return 1;
}

static int ccitt_decode_single(ccitt_bitstream *bs,
                               const ccitt_code *term, size_t term_len,
                               const ccitt_code *makeup, size_t makeup_len,
                               uint32_t *run, int *is_makeup) {
    uint32_t code = 0;
    for (int len = 1; len <= 13; len++) {
        int bit = 0;
        if (!ccitt_read_bit(bs, &bit)) {
            return 0;
        }
        code = (code << 1) | (uint32_t)bit;
        for (size_t i = 0; i < term_len; i++) {
            if (term[i].length == len && term[i].code == code) {
                *run = term[i].run;
                *is_makeup = 0;
                return 1;
            }
        }
        for (size_t i = 0; i < makeup_len; i++) {
            if (makeup[i].length == len && makeup[i].code == code) {
                *run = makeup[i].run;
                *is_makeup = 1;
                return 1;
            }
        }
    }
    return 0;
}

static int ccitt_decode_run(ccitt_bitstream *bs, int color, uint32_t *run) {
    const ccitt_code *term = color ? ccitt_black_term : ccitt_white_term;
    const ccitt_code *makeup = color ? ccitt_black_makeup : ccitt_white_makeup;
    size_t term_len = color ? (sizeof(ccitt_black_term) / sizeof(ccitt_black_term[0]))
                            : (sizeof(ccitt_white_term) / sizeof(ccitt_white_term[0]));
    size_t makeup_len = color ? (sizeof(ccitt_black_makeup) / sizeof(ccitt_black_makeup[0]))
                              : (sizeof(ccitt_white_makeup) / sizeof(ccitt_white_makeup[0]));
    uint32_t total = 0;
    while (1) {
        uint32_t val = 0;
        int is_makeup = 0;
        if (!ccitt_decode_single(bs, term, term_len, makeup, makeup_len, &val, &is_makeup)) {
            return 0;
        }
        total += val;
        if (!is_makeup) {
            break;
        }
    }
    *run = total;
    return 1;
}

static void ccitt_set_run(uint8_t *row, uint32_t start, uint32_t run, int color) {
    for (uint32_t i = 0; i < run; i++) {
        uint32_t x = start + i;
        uint8_t mask = (uint8_t)(0x80u >> (x & 7u));
        if (color) {
            row[x / 8u] |= mask;
        } else {
            row[x / 8u] &= (uint8_t)~mask;
        }
    }
}

static int ccitt_get_bit(const uint8_t *row, uint32_t x) {
    uint8_t mask = (uint8_t)(0x80u >> (x & 7u));
    return (row[x / 8u] & mask) ? 1 : 0;
}

static uint32_t ccitt_next_transition(const uint8_t *row, uint32_t width, uint32_t start, int color) {
    uint32_t x = start;
    while (x < width) {
        if (ccitt_get_bit(row, x) != color) {
            break;
        }
        x++;
    }
    return x;
}

static int ccitt_skip_eol(ccitt_bitstream *bs) {
    int zeros = 0;
    for (;;) {
        int bit = 0;
        if (!ccitt_read_bit(bs, &bit)) {
            return 0;
        }
        if (bit == 0) {
            zeros++;
            if (zeros > 11) {
                zeros = 11;
            }
        } else {
            if (zeros >= 11) {
                return 1;
            }
            zeros = 0;
        }
    }
}

static int ccitt_decode_1d_line(ccitt_bitstream *bs, uint32_t width, uint8_t *row) {
    uint32_t x = 0;
    int color = 0;
    while (x < width) {
        uint32_t run = 0;
        if (!ccitt_decode_run(bs, color, &run)) {
            return 0;
        }
        if (x + run > width) {
            return 0;
        }
        ccitt_set_run(row, x, run, color);
        x += run;
        color = !color;
    }
    return 1;
}

enum {
    CCITT_2D_PASS = 0,
    CCITT_2D_HORIZ = 1,
    CCITT_2D_VERT = 2
};

static int ccitt_decode_2d_code(ccitt_bitstream *bs, int *type, int *val) {
    uint32_t code = 0;
    for (int len = 1; len <= 7; len++) {
        int bit = 0;
        if (!ccitt_read_bit(bs, &bit)) {
            return 0;
        }
        code = (code << 1) | (uint32_t)bit;
        if (len == 1 && code == 1) {
            *type = CCITT_2D_VERT;
            *val = 0;
            return 1;
        }
        if (len == 3 && code == 0x3) {
            *type = CCITT_2D_VERT;
            *val = 1;
            return 1;
        }
        if (len == 3 && code == 0x2) {
            *type = CCITT_2D_VERT;
            *val = -1;
            return 1;
        }
        if (len == 3 && code == 0x1) {
            *type = CCITT_2D_HORIZ;
            *val = 0;
            return 1;
        }
        if (len == 4 && code == 0x1) {
            *type = CCITT_2D_PASS;
            *val = 0;
            return 1;
        }
        if (len == 6 && code == 0x3) {
            *type = CCITT_2D_VERT;
            *val = 2;
            return 1;
        }
        if (len == 6 && code == 0x2) {
            *type = CCITT_2D_VERT;
            *val = -2;
            return 1;
        }
        if (len == 7 && code == 0x3) {
            *type = CCITT_2D_VERT;
            *val = 3;
            return 1;
        }
        if (len == 7 && code == 0x2) {
            *type = CCITT_2D_VERT;
            *val = -3;
            return 1;
        }
        if (len == 7 && code == 0x1) {
            return 0;
        }
    }
    return 0;
}

static int ccitt_decode_2d_line(ccitt_bitstream *bs, uint32_t width,
                                const uint8_t *ref, uint8_t *row) {
    uint32_t a0 = 0;
    int color = 0;
    while (a0 < width) {
        int type = 0;
        int val = 0;
        if (!ccitt_decode_2d_code(bs, &type, &val)) {
            return 0;
        }
        if (type == CCITT_2D_PASS) {
            uint32_t b1 = ccitt_next_transition(ref, width, a0, color);
            uint32_t b2 = ccitt_next_transition(ref, width, b1, !color);
            if (b2 < a0 || b2 > width) {
                return 0;
            }
            a0 = b2;
        } else if (type == CCITT_2D_HORIZ) {
            uint32_t run1 = 0;
            uint32_t run2 = 0;
            if (!ccitt_decode_run(bs, color, &run1) ||
                !ccitt_decode_run(bs, !color, &run2)) {
                return 0;
            }
            if (a0 + run1 + run2 > width) {
                return 0;
            }
            ccitt_set_run(row, a0, run1, color);
            a0 += run1;
            ccitt_set_run(row, a0, run2, !color);
            a0 += run2;
            color = color;
        } else {
            uint32_t b1 = ccitt_next_transition(ref, width, a0, color);
            int32_t a1 = (int32_t)b1 + val;
            if (a1 < 0) {
                a1 = 0;
            }
            if ((uint32_t)a1 > width) {
                return 0;
            }
            ccitt_set_run(row, a0, (uint32_t)a1 - a0, color);
            a0 = (uint32_t)a1;
            color = !color;
        }
    }
    return 1;
}

static int tiff_ccitt_decode(const uint8_t *src, size_t src_size,
                             uint8_t *dst, size_t dst_size,
                             uint32_t width, uint32_t height,
                             uint16_t compression, uint32_t group3_options,
                             int fill_order, char *err, size_t errcap) {
    size_t row_bytes = ((size_t)width + 7u) / 8u;
    if (dst_size < row_bytes * (size_t)height) {
        set_err(err, errcap, "invalid CCITT buffer");
        return 0;
    }
    memset(dst, 0, row_bytes * (size_t)height);

    ccitt_bitstream bs;
    bs.data = src;
    bs.size = src_size;
    bs.pos = 0;
    bs.bit = 0;
    bs.fill_order = fill_order;

    int use_2d = 0;
    if (compression == TIFF_COMPRESSION_CCITT_G4) {
        use_2d = 1;
    } else if (compression == TIFF_COMPRESSION_CCITT_G3) {
        if (group3_options & 0x1u) {
            use_2d = 1;
        }
    }

    uint8_t *prev = (uint8_t *)malloc(row_bytes);
    if (!prev) {
        set_err(err, errcap, "out of memory");
        return 0;
    }
    memset(prev, 0, row_bytes);

    for (uint32_t y = 0; y < height; y++) {
        uint8_t *row = dst + (size_t)y * row_bytes;
        if (compression == TIFF_COMPRESSION_CCITT_G3 && (group3_options & 0x4u)) {
            if (!ccitt_skip_eol(&bs)) {
                free(prev);
                set_err(err, errcap, "invalid CCITT EOL");
                return 0;
            }
        }
        if (use_2d && y > 0) {
            if (!ccitt_decode_2d_line(&bs, width, prev, row)) {
                free(prev);
                set_err(err, errcap, "invalid CCITT 2D data");
                return 0;
            }
        } else {
            if (!ccitt_decode_1d_line(&bs, width, row)) {
                free(prev);
                set_err(err, errcap, "invalid CCITT data");
                return 0;
            }
        }
        memcpy(prev, row, row_bytes);
    }

    free(prev);
    return 1;
}

static uint8_t sample16_to_8(uint16_t v) {
    return (uint8_t)((v + 128u) / 257u);
}

static uint32_t tiff_read_sample(const tiff_context *ctx, const uint8_t *row, uint32_t index,
                                 uint16_t bps, uint16_t fill_order) {
    if (bps == 0 || bps > 32) {
        return 0;
    }
    if ((bps & 7u) == 0) {
        uint32_t bytes = bps / 8u;
        const uint8_t *p = row + (size_t)index * bytes;
        uint32_t v = 0;
        if (ctx->big_endian) {
            for (uint32_t i = 0; i < bytes; i++) {
                v = (v << 8) | p[i];
            }
        } else {
            for (uint32_t i = 0; i < bytes; i++) {
                v |= (uint32_t)p[i] << (8u * i);
            }
        }
        return v;
    }

    uint32_t bitpos = index * bps;
    uint32_t v = 0;
    for (uint16_t i = 0; i < bps; i++) {
        uint32_t pos = bitpos + i;
        uint8_t byte = row[pos / 8u];
        uint32_t bit = pos & 7u;
        if (fill_order == TIFF_FILL_ORDER_LSB2MSB) {
            v = (v << 1) | ((byte >> bit) & 1u);
        } else {
            v = (v << 1) | ((byte >> (7u - bit)) & 1u);
        }
    }
    return v;
}

static uint8_t tiff_scale_sample(uint32_t v, uint16_t bps) {
    if (bps == 0) {
        return 0;
    }
    if (bps == 8) {
        return (uint8_t)v;
    }
    if (bps == 16) {
        return sample16_to_8((uint16_t)v);
    }
    if (bps >= 32) {
        uint64_t maxv = 0xFFFFFFFFu;
        return (uint8_t)(((uint64_t)v * 255u + maxv / 2u) / maxv);
    }
    uint32_t maxv = (1u << bps) - 1u;
    return (uint8_t)(((uint64_t)v * 255u + maxv / 2u) / maxv);
}

static uint8_t tiff_clamp8(int v) {
    if (v < 0) {
        return 0;
    }
    if (v > 255) {
        return 255;
    }
    return (uint8_t)v;
}

static uint8_t tiff_scale_float(double v) {
    if (v <= 0.0) {
        return 0;
    }
    if (v <= 1.0) {
        return (uint8_t)(v * 255.0 + 0.5);
    }
    if (v < 255.0) {
        return (uint8_t)(v + 0.5);
    }
    return 255;
}

static double tiff_read_sample_float(const tiff_context *ctx, const uint8_t *row,
                                     uint32_t index, uint16_t bps) {
    if (bps == 32) {
        const uint8_t *p = row + (size_t)index * 4u;
        uint32_t u = read_u32(ctx, p);
        float f;
        memcpy(&f, &u, sizeof(float));
        return (double)f;
    }
    if (bps == 64) {
        const uint8_t *p = row + (size_t)index * 8u;
        uint64_t u = read_u64(ctx, p);
        double d;
        memcpy(&d, &u, sizeof(double));
        return d;
    }
    return 0.0;
}

static int tiff_apply_predictor_row(const tiff_context *ctx, uint8_t *row, size_t row_bytes,
                                    uint16_t samples, uint16_t bps) {
    if (bps == 8) {
        size_t stride = (size_t)samples;
        if (stride == 0) {
            return 0;
        }
        for (size_t i = stride; i < row_bytes; i++) {
            row[i] = (uint8_t)(row[i] + row[i - stride]);
        }
        return 1;
    }
    if (bps == 16) {
        size_t stride = (size_t)samples * 2u;
        if (stride == 0) {
            return 0;
        }
        for (size_t i = stride; i + 1 < row_bytes; i += 2) {
            uint16_t v = read_u16(ctx, row + i);
            uint16_t prev = read_u16(ctx, row + i - stride);
            v = (uint16_t)(v + prev);
            write_u16(ctx, row + i, v);
        }
        return 1;
    }
    return 0;
}

static int tiff_apply_predictor(const tiff_context *ctx, const tiff_image_info *info,
                                uint8_t *raw, size_t row_bytes, char *err, size_t errcap) {
    if (info->predictor != 2) {
        return 1;
    }
    if (info->bits_per_sample[0] != 8 && info->bits_per_sample[0] != 16) {
        set_err(err, errcap, "unsupported TIFF predictor bit depth");
        return 0;
    }
    for (uint32_t y = 0; y < info->height; y++) {
        uint8_t *row = raw + (size_t)y * row_bytes;
        if (!tiff_apply_predictor_row(ctx, row, row_bytes, info->samples_per_pixel,
                                      info->bits_per_sample[0])) {
            set_err(err, errcap, "TIFF predictor failed");
            return 0;
        }
    }
    return 1;
}

static int tiff_decompress_block(const tiff_context *ctx, const tiff_image_info *info,
                                 const uint8_t *src, size_t src_size,
                                 uint8_t *dst, size_t dst_size,
                                 uint32_t width, uint32_t rows,
                                 char *err, size_t errcap) {
    (void)ctx;
    switch (info->compression) {
        case TIFF_COMPRESSION_NONE:
            if (src_size < dst_size) {
                set_err(err, errcap, "truncated TIFF data");
                return 0;
            }
            memcpy(dst, src, dst_size);
            return 1;
        case TIFF_COMPRESSION_LZW:
            return tiff_lzw_decode(src, src_size, dst, dst_size, err, errcap);
        case TIFF_COMPRESSION_PACKBITS:
            return tiff_packbits_decode(src, src_size, dst, dst_size, err, errcap);
        case TIFF_COMPRESSION_DEFLATE:
        case TIFF_COMPRESSION_ADOBE_DEFLATE: {
            size_t outlen = 0;
            if (!tiff_inflate(src, src_size, dst, dst_size, &outlen) || outlen != dst_size) {
                set_err(err, errcap, "deflate decode failed");
                return 0;
            }
            return 1;
        }
        case TIFF_COMPRESSION_CCITT_RLE:
        case TIFF_COMPRESSION_CCITT_G3:
        case TIFF_COMPRESSION_CCITT_G4:
            return tiff_ccitt_decode(src, src_size, dst, dst_size, width, rows,
                                     info->compression, info->group3_options,
                                     info->fill_order, err, errcap);
        default:
            set_err(err, errcap, "unsupported TIFF compression");
            return 0;
    }
}

static int tiff_decode_strips_chunky(const tiff_context *ctx, const tiff_image_info *info,
                                     uint8_t *raw, size_t raw_size,
                                     size_t row_bytes, char *err, size_t errcap) {
    uint64_t strips_expected = (info->height + info->rows_per_strip - 1u) / info->rows_per_strip;
    uint64_t strip_count = info->strip_offsets_count;
    if (strip_count < strips_expected) {
        set_err(err, errcap, "invalid number of strips");
        return 0;
    }
    if (strip_count > strips_expected) {
        strip_count = strips_expected;
    }

    for (uint64_t i = 0; i < strip_count; i++) {
        uint64_t row_start = i * (uint64_t)info->rows_per_strip;
        if (row_start >= info->height) {
            break;
        }
        uint32_t strip_rows = info->rows_per_strip;
        if (row_start + strip_rows > info->height) {
            strip_rows = info->height - (uint32_t)row_start;
        }
        size_t expected = 0;
        if (mul_overflow_size_t(row_bytes, (size_t)strip_rows, &expected)) {
            set_err(err, errcap, "invalid TIFF strip size");
            return 0;
        }
        size_t dst_off = 0;
        if (mul_overflow_size_t(row_bytes, (size_t)row_start, &dst_off)) {
            set_err(err, errcap, "invalid TIFF strip size");
            return 0;
        }
        if (dst_off + expected > raw_size) {
            set_err(err, errcap, "TIFF strip out of bounds");
            return 0;
        }

        uint64_t off = info->strip_offsets[i];
        if (off >= ctx->size) {
            set_err(err, errcap, "TIFF strip offset out of range");
            return 0;
        }
        size_t src_size = expected;
        if (info->compression != TIFF_COMPRESSION_NONE) {
            if (!info->strip_byte_counts || i >= info->strip_byte_counts_count) {
                set_err(err, errcap, "invalid StripByteCounts");
                return 0;
            }
            src_size = (size_t)info->strip_byte_counts[i];
            if (src_size == 0) {
                set_err(err, errcap, "invalid StripByteCounts");
                return 0;
            }
        } else if (info->strip_byte_counts && i < info->strip_byte_counts_count) {
            if (info->strip_byte_counts[i] < expected) {
                set_err(err, errcap, "truncated TIFF strip");
                return 0;
            }
        }
        if (off + src_size > ctx->size) {
            set_err(err, errcap, "truncated TIFF strip");
            return 0;
        }

        if (!tiff_decompress_block(ctx, info, ctx->data + (size_t)off, src_size,
                                   raw + dst_off, expected, info->width, strip_rows,
                                   err, errcap)) {
            return 0;
        }
    }

    return 1;
}

static int tiff_decode_strips_planar(const tiff_context *ctx, const tiff_image_info *info,
                                     uint8_t *raw, size_t raw_size,
                                     size_t row_bytes, size_t bytes_per_sample,
                                     char *err, size_t errcap) {
    (void)raw_size;
    if ((info->bits_per_sample[0] & 7u) != 0) {
        set_err(err, errcap, "planar TIFF requires byte-aligned samples");
        return 0;
    }

    uint64_t strips_per_plane = (info->height + info->rows_per_strip - 1u) / info->rows_per_strip;
    uint64_t required = strips_per_plane * info->samples_per_pixel;
    if (info->strip_offsets_count < required) {
        set_err(err, errcap, "invalid number of planar strips");
        return 0;
    }

    size_t row_bytes_plane = 0;
    if (mul_overflow_size_t((size_t)info->width, bytes_per_sample, &row_bytes_plane)) {
        set_err(err, errcap, "invalid planar row size");
        return 0;
    }

    size_t max_strip = 0;
    if (mul_overflow_size_t(row_bytes_plane, (size_t)info->rows_per_strip, &max_strip)) {
        set_err(err, errcap, "invalid planar strip size");
        return 0;
    }
    uint8_t *plane_buf = (uint8_t *)malloc(max_strip);
    if (!plane_buf) {
        set_err(err, errcap, "out of memory");
        return 0;
    }

    for (uint16_t s = 0; s < info->samples_per_pixel; s++) {
        for (uint64_t strip = 0; strip < strips_per_plane; strip++) {
            uint64_t row_start = strip * (uint64_t)info->rows_per_strip;
            if (row_start >= info->height) {
                break;
            }
            uint32_t strip_rows = info->rows_per_strip;
            if (row_start + strip_rows > info->height) {
                strip_rows = info->height - (uint32_t)row_start;
            }
            size_t expected = 0;
            if (mul_overflow_size_t(row_bytes_plane, (size_t)strip_rows, &expected)) {
                free(plane_buf);
                set_err(err, errcap, "invalid planar strip size");
                return 0;
            }

            uint64_t idx = (uint64_t)s * strips_per_plane + strip;
            uint64_t off = info->strip_offsets[idx];
            if (off >= ctx->size) {
                free(plane_buf);
                set_err(err, errcap, "TIFF strip offset out of range");
                return 0;
            }
            size_t src_size = expected;
            if (info->compression != TIFF_COMPRESSION_NONE) {
                if (!info->strip_byte_counts || idx >= info->strip_byte_counts_count) {
                    free(plane_buf);
                    set_err(err, errcap, "invalid StripByteCounts");
                    return 0;
                }
                src_size = (size_t)info->strip_byte_counts[idx];
                if (src_size == 0) {
                    free(plane_buf);
                    set_err(err, errcap, "invalid StripByteCounts");
                    return 0;
                }
            } else if (info->strip_byte_counts && idx < info->strip_byte_counts_count) {
                if (info->strip_byte_counts[idx] < expected) {
                    free(plane_buf);
                    set_err(err, errcap, "truncated TIFF strip");
                    return 0;
                }
            }
            if (off + src_size > ctx->size) {
                free(plane_buf);
                set_err(err, errcap, "truncated TIFF strip");
                return 0;
            }

            if (!tiff_decompress_block(ctx, info, ctx->data + (size_t)off, src_size,
                                       plane_buf, expected, info->width, strip_rows,
                                       err, errcap)) {
                free(plane_buf);
                return 0;
            }

            if (info->predictor == 2) {
                for (uint32_t r = 0; r < strip_rows; r++) {
                    if (!tiff_apply_predictor_row(ctx, plane_buf + (size_t)r * row_bytes_plane,
                                                  row_bytes_plane, 1, info->bits_per_sample[0])) {
                        free(plane_buf);
                        set_err(err, errcap, "TIFF predictor failed");
                        return 0;
                    }
                }
            }

            for (uint32_t r = 0; r < strip_rows; r++) {
                uint32_t y = (uint32_t)row_start + r;
                if (y >= info->height) {
                    break;
                }
                uint8_t *dst_row = raw + (size_t)y * row_bytes;
                const uint8_t *src_row = plane_buf + (size_t)r * row_bytes_plane;
                for (uint32_t x = 0; x < info->width; x++) {
                    size_t dst = (size_t)x * (size_t)info->samples_per_pixel * bytes_per_sample +
                                 (size_t)s * bytes_per_sample;
                    memcpy(dst_row + dst, src_row + (size_t)x * bytes_per_sample, bytes_per_sample);
                }
            }
        }
    }

    free(plane_buf);
    if (raw_size == 0) {
        return 0;
    }
    return 1;
}

static int tiff_decode_tiles_chunky(const tiff_context *ctx, const tiff_image_info *info,
                                    uint8_t *raw, size_t raw_size,
                                    size_t row_bytes, size_t bytes_per_pixel,
                                    char *err, size_t errcap) {
    (void)raw_size;
    if (info->tile_width == 0 || info->tile_length == 0) {
        set_err(err, errcap, "invalid TIFF tile size");
        return 0;
    }
    if ((info->bits_per_sample[0] & 7u) != 0) {
        set_err(err, errcap, "tiled TIFF requires byte-aligned samples");
        return 0;
    }

    uint64_t tiles_across = (info->width + info->tile_width - 1u) / info->tile_width;
    uint64_t tiles_down = (info->height + info->tile_length - 1u) / info->tile_length;
    uint64_t tiles_total = tiles_across * tiles_down;
    if (info->tile_offsets_count < tiles_total) {
        set_err(err, errcap, "invalid number of tiles");
        return 0;
    }

    size_t tile_row_bytes = 0;
    if (mul_overflow_size_t((size_t)info->tile_width, bytes_per_pixel, &tile_row_bytes)) {
        set_err(err, errcap, "invalid tile row size");
        return 0;
    }
    size_t tile_buf_size = 0;
    if (mul_overflow_size_t(tile_row_bytes, (size_t)info->tile_length, &tile_buf_size)) {
        set_err(err, errcap, "invalid tile buffer size");
        return 0;
    }

    uint8_t *tile_buf = (uint8_t *)malloc(tile_buf_size);
    if (!tile_buf) {
        set_err(err, errcap, "out of memory");
        return 0;
    }

    for (uint64_t t = 0; t < tiles_total; t++) {
        uint64_t off = info->tile_offsets[t];
        if (off >= ctx->size) {
            free(tile_buf);
            set_err(err, errcap, "TIFF tile offset out of range");
            return 0;
        }
        size_t src_size = tile_buf_size;
        if (info->compression != TIFF_COMPRESSION_NONE) {
            if (!info->tile_byte_counts || t >= info->tile_byte_counts_count) {
                free(tile_buf);
                set_err(err, errcap, "invalid TileByteCounts");
                return 0;
            }
            src_size = (size_t)info->tile_byte_counts[t];
            if (src_size == 0) {
                free(tile_buf);
                set_err(err, errcap, "invalid TileByteCounts");
                return 0;
            }
        } else if (info->tile_byte_counts && t < info->tile_byte_counts_count) {
            if (info->tile_byte_counts[t] < tile_buf_size) {
                free(tile_buf);
                set_err(err, errcap, "truncated TIFF tile");
                return 0;
            }
        }
        if (off + src_size > ctx->size) {
            free(tile_buf);
            set_err(err, errcap, "truncated TIFF tile");
            return 0;
        }

        if (!tiff_decompress_block(ctx, info, ctx->data + (size_t)off, src_size,
                                   tile_buf, tile_buf_size,
                                   info->tile_width, info->tile_length,
                                   err, errcap)) {
            free(tile_buf);
            return 0;
        }

        if (info->predictor == 2) {
            for (uint32_t r = 0; r < info->tile_length; r++) {
                if (!tiff_apply_predictor_row(ctx, tile_buf + (size_t)r * tile_row_bytes,
                                              tile_row_bytes, info->samples_per_pixel,
                                              info->bits_per_sample[0])) {
                    free(tile_buf);
                    set_err(err, errcap, "TIFF predictor failed");
                    return 0;
                }
            }
        }

        uint32_t tile_x = (uint32_t)(t % tiles_across) * info->tile_width;
        uint32_t tile_y = (uint32_t)(t / tiles_across) * info->tile_length;
        uint32_t copy_w = info->tile_width;
        uint32_t copy_h = info->tile_length;
        if (tile_x + copy_w > info->width) {
            copy_w = info->width - tile_x;
        }
        if (tile_y + copy_h > info->height) {
            copy_h = info->height - tile_y;
        }

        for (uint32_t r = 0; r < copy_h; r++) {
            const uint8_t *src_row = tile_buf + (size_t)r * tile_row_bytes;
            uint8_t *dst_row = raw + (size_t)(tile_y + r) * row_bytes +
                               (size_t)tile_x * bytes_per_pixel;
            memcpy(dst_row, src_row, (size_t)copy_w * bytes_per_pixel);
        }
    }

    free(tile_buf);
    if (raw_size == 0) {
        return 0;
    }
    return 1;
}

static int tiff_decode_tiles_planar(const tiff_context *ctx, const tiff_image_info *info,
                                    uint8_t *raw, size_t raw_size,
                                    size_t row_bytes, size_t bytes_per_sample,
                                    char *err, size_t errcap) {
    (void)raw_size;
    if (info->tile_width == 0 || info->tile_length == 0) {
        set_err(err, errcap, "invalid TIFF tile size");
        return 0;
    }
    if ((info->bits_per_sample[0] & 7u) != 0) {
        set_err(err, errcap, "tiled TIFF requires byte-aligned samples");
        return 0;
    }

    uint64_t tiles_across = (info->width + info->tile_width - 1u) / info->tile_width;
    uint64_t tiles_down = (info->height + info->tile_length - 1u) / info->tile_length;
    uint64_t tiles_per_plane = tiles_across * tiles_down;
    uint64_t required = tiles_per_plane * info->samples_per_pixel;
    if (info->tile_offsets_count < required) {
        set_err(err, errcap, "invalid number of tiles");
        return 0;
    }

    size_t tile_row_bytes = 0;
    if (mul_overflow_size_t((size_t)info->tile_width, bytes_per_sample, &tile_row_bytes)) {
        set_err(err, errcap, "invalid tile row size");
        return 0;
    }
    size_t tile_buf_size = 0;
    if (mul_overflow_size_t(tile_row_bytes, (size_t)info->tile_length, &tile_buf_size)) {
        set_err(err, errcap, "invalid tile buffer size");
        return 0;
    }

    uint8_t *tile_buf = (uint8_t *)malloc(tile_buf_size);
    if (!tile_buf) {
        set_err(err, errcap, "out of memory");
        return 0;
    }

    for (uint16_t s = 0; s < info->samples_per_pixel; s++) {
        for (uint64_t t = 0; t < tiles_per_plane; t++) {
            uint64_t idx = (uint64_t)s * tiles_per_plane + t;
            uint64_t off = info->tile_offsets[idx];
            if (off >= ctx->size) {
                free(tile_buf);
                set_err(err, errcap, "TIFF tile offset out of range");
                return 0;
            }
            size_t src_size = tile_buf_size;
            if (info->compression != TIFF_COMPRESSION_NONE) {
                if (!info->tile_byte_counts || idx >= info->tile_byte_counts_count) {
                    free(tile_buf);
                    set_err(err, errcap, "invalid TileByteCounts");
                    return 0;
                }
                src_size = (size_t)info->tile_byte_counts[idx];
                if (src_size == 0) {
                    free(tile_buf);
                    set_err(err, errcap, "invalid TileByteCounts");
                    return 0;
                }
            } else if (info->tile_byte_counts && idx < info->tile_byte_counts_count) {
                if (info->tile_byte_counts[idx] < tile_buf_size) {
                    free(tile_buf);
                    set_err(err, errcap, "truncated TIFF tile");
                    return 0;
                }
            }
            if (off + src_size > ctx->size) {
                free(tile_buf);
                set_err(err, errcap, "truncated TIFF tile");
                return 0;
            }

            if (!tiff_decompress_block(ctx, info, ctx->data + (size_t)off, src_size,
                                       tile_buf, tile_buf_size,
                                       info->tile_width, info->tile_length,
                                       err, errcap)) {
                free(tile_buf);
                return 0;
            }

            if (info->predictor == 2) {
                for (uint32_t r = 0; r < info->tile_length; r++) {
                    if (!tiff_apply_predictor_row(ctx, tile_buf + (size_t)r * tile_row_bytes,
                                                  tile_row_bytes, 1, info->bits_per_sample[0])) {
                        free(tile_buf);
                        set_err(err, errcap, "TIFF predictor failed");
                        return 0;
                    }
                }
            }

            uint32_t tile_x = (uint32_t)(t % tiles_across) * info->tile_width;
            uint32_t tile_y = (uint32_t)(t / tiles_across) * info->tile_length;
            uint32_t copy_w = info->tile_width;
            uint32_t copy_h = info->tile_length;
            if (tile_x + copy_w > info->width) {
                copy_w = info->width - tile_x;
            }
            if (tile_y + copy_h > info->height) {
                copy_h = info->height - tile_y;
            }

            for (uint32_t r = 0; r < copy_h; r++) {
                const uint8_t *src_row = tile_buf + (size_t)r * tile_row_bytes;
                uint8_t *dst_row = raw + (size_t)(tile_y + r) * row_bytes;
                for (uint32_t x = 0; x < copy_w; x++) {
                    size_t dst = (size_t)(tile_x + x) * (size_t)info->samples_per_pixel * bytes_per_sample +
                                 (size_t)s * bytes_per_sample;
                    memcpy(dst_row + dst, src_row + (size_t)x * bytes_per_sample, bytes_per_sample);
                }
            }
        }
    }

    free(tile_buf);
    if (raw_size == 0) {
        return 0;
    }
    return 1;
}

static int tiff_decode_image(const tiff_context *ctx, const tiff_image_info *info,
                             uint8_t *raw, size_t raw_size,
                             size_t row_bytes, size_t bytes_per_pixel,
                             char *err, size_t errcap) {
    if (info->tile_offsets && info->tile_offsets_count > 0) {
        if (info->planar_config == TIFF_PLANAR_CHUNKY) {
            return tiff_decode_tiles_chunky(ctx, info, raw, raw_size, row_bytes, bytes_per_pixel,
                                            err, errcap);
        }
        return tiff_decode_tiles_planar(ctx, info, raw, raw_size, row_bytes,
                                        bytes_per_pixel / info->samples_per_pixel, err, errcap);
    }

    if (info->planar_config == TIFF_PLANAR_CHUNKY) {
        return tiff_decode_strips_chunky(ctx, info, raw, raw_size, row_bytes, err, errcap);
    }
    return tiff_decode_strips_planar(ctx, info, raw, raw_size, row_bytes,
                                     bytes_per_pixel / info->samples_per_pixel, err, errcap);
}

static int tiff_build_jpeg_stream(const uint8_t *tables, size_t tables_size,
                                  const uint8_t *data, size_t data_size,
                                  uint8_t **out, size_t *out_size,
                                  char *err, size_t errcap) {
    int tables_has_soi = tables_size >= 2 && tables[0] == 0xFF && tables[1] == 0xD8;
    int tables_has_eoi = tables_size >= 2 &&
                         tables[tables_size - 2] == 0xFF && tables[tables_size - 1] == 0xD9;
    size_t tables_start = tables_has_soi ? 2u : 0u;
    size_t tables_end = tables_has_eoi ? (tables_size - 2u) : tables_size;

    int data_has_soi = data_size >= 2 && data[0] == 0xFF && data[1] == 0xD8;
    int data_has_eoi = data_size >= 2 &&
                       data[data_size - 2] == 0xFF && data[data_size - 1] == 0xD9;
    size_t data_start = data_has_soi ? 2u : 0u;
    size_t data_end = data_size;

    size_t total = 2u + (tables_end - tables_start) + (data_end - data_start) + (data_has_eoi ? 0u : 2u);
    uint8_t *buf = (uint8_t *)malloc(total);
    if (!buf) {
        set_err(err, errcap, "out of memory");
        return 0;
    }

    size_t pos = 0;
    buf[pos++] = 0xFF;
    buf[pos++] = 0xD8;
    if (tables_end > tables_start) {
        memcpy(buf + pos, tables + tables_start, tables_end - tables_start);
        pos += tables_end - tables_start;
    }
    if (data_end > data_start) {
        memcpy(buf + pos, data + data_start, data_end - data_start);
        pos += data_end - data_start;
    }
    if (!data_has_eoi) {
        buf[pos++] = 0xFF;
        buf[pos++] = 0xD9;
    }

    *out = buf;
    *out_size = pos;
    return 1;
}

static int tiff_decode_jpeg_stream(const unsigned char *data, size_t size,
                                   uint32_t expected_w, uint32_t expected_h,
                                   uint8_t *rgba, size_t rgba_size,
                                   char *err, size_t errcap) {
    cupidimage_image img;
    if (!cupidimage_load_jpeg(data, size, &img, err, errcap)) {
        return 0;
    }
    if (img.width != expected_w || img.height != expected_h) {
        cupidimage_free(&img);
        set_err(err, errcap, "JPEG strip dimensions mismatch");
        return 0;
    }
    size_t need = (size_t)img.width * (size_t)img.height * 4u;
    if (need > rgba_size) {
        cupidimage_free(&img);
        set_err(err, errcap, "invalid RGBA buffer");
        return 0;
    }
    memcpy(rgba, img.rgba, need);
    cupidimage_free(&img);
    return 1;
}

static int tiff_decode_jpeg_image(const tiff_context *ctx, const tiff_image_info *info,
                                  uint8_t **out_rgba,
                                  uint32_t *out_width, uint32_t *out_height,
                                  char *err, size_t errcap) {
    if (info->tile_offsets && info->tile_offsets_count > 0) {
        set_err(err, errcap, "JPEG-in-TIFF tiles not supported");
        return 0;
    }

    if (info->jpeg_if_offset && info->jpeg_if_bytecount) {
        if (info->jpeg_if_offset + info->jpeg_if_bytecount > ctx->size) {
            set_err(err, errcap, "invalid JPEG interchange data");
            return 0;
        }
        const unsigned char *jpeg_data = ctx->data + (size_t)info->jpeg_if_offset;
        size_t jpeg_size = (size_t)info->jpeg_if_bytecount;
        cupidimage_image img;
        if (!cupidimage_load_jpeg(jpeg_data, jpeg_size, &img, err, errcap)) {
            return 0;
        }
        *out_rgba = img.rgba;
        *out_width = img.width;
        *out_height = img.height;
        return 1;
    }

    uint64_t strips_expected = (info->height + info->rows_per_strip - 1u) / info->rows_per_strip;
    if (info->strip_offsets_count < strips_expected) {
        set_err(err, errcap, "invalid number of strips");
        return 0;
    }
    if (!info->strip_byte_counts || info->strip_byte_counts_count < strips_expected) {
        set_err(err, errcap, "missing StripByteCounts");
        return 0;
    }

    size_t rgba_size = (size_t)info->width * (size_t)info->height * 4u;
    uint8_t *rgba = (uint8_t *)malloc(rgba_size);
    if (!rgba) {
        set_err(err, errcap, "out of memory");
        return 0;
    }

    for (uint64_t i = 0; i < strips_expected; i++) {
        uint64_t row_start = i * (uint64_t)info->rows_per_strip;
        if (row_start >= info->height) {
            break;
        }
        uint32_t strip_rows = info->rows_per_strip;
        if (row_start + strip_rows > info->height) {
            strip_rows = info->height - (uint32_t)row_start;
        }

        uint64_t off = info->strip_offsets[i];
        uint64_t count = info->strip_byte_counts[i];
        if (count == 0 || off + count > ctx->size) {
            free(rgba);
            set_err(err, errcap, "truncated JPEG strip");
            return 0;
        }
        const uint8_t *src = ctx->data + (size_t)off;
        size_t src_size = (size_t)count;

        uint8_t *stream = NULL;
        size_t stream_size = 0;
        if (!tiff_build_jpeg_stream(info->jpeg_tables, info->jpeg_tables_size,
                                    src, src_size, &stream, &stream_size, err, errcap)) {
            free(rgba);
            return 0;
        }

        size_t strip_rgba_size = (size_t)info->width * (size_t)strip_rows * 4u;
        uint8_t *strip_rgba = rgba + (size_t)row_start * (size_t)info->width * 4u;
        if (!tiff_decode_jpeg_stream(stream, stream_size, info->width, strip_rows,
                                     strip_rgba, strip_rgba_size, err, errcap)) {
            free(stream);
            free(rgba);
            return 0;
        }
        free(stream);
    }

    *out_rgba = rgba;
    *out_width = info->width;
    *out_height = info->height;
    return 1;
}

static int tiff_decode_ycbcr_subsampled(const tiff_context *ctx, const tiff_image_info *info,
                                        uint8_t *rgba, size_t rgba_size,
                                        char *err, size_t errcap) {
    if (info->planar_config != TIFF_PLANAR_CHUNKY) {
        set_err(err, errcap, "YCbCr planar TIFF not supported");
        return 0;
    }
    if (info->samples_per_pixel < 3) {
        set_err(err, errcap, "invalid YCbCr SamplesPerPixel");
        return 0;
    }
    if (info->predictor == 2) {
        set_err(err, errcap, "YCbCr predictor not supported");
        return 0;
    }
    if (info->tile_offsets && info->tile_offsets_count > 0) {
        set_err(err, errcap, "YCbCr tiled TIFF not supported");
        return 0;
    }
    if (info->bits_per_sample[0] != 8) {
        set_err(err, errcap, "YCbCr subsampling requires 8-bit samples");
        return 0;
    }
    uint32_t hsub = info->ycbcr_subsampling[0];
    uint32_t vsub = info->ycbcr_subsampling[1];
    if (hsub == 0 || vsub == 0) {
        set_err(err, errcap, "invalid YCbCr subsampling");
        return 0;
    }

    size_t expected_rgba = (size_t)info->width * (size_t)info->height * 4u;
    if (rgba_size < expected_rgba) {
        set_err(err, errcap, "invalid RGBA buffer");
        return 0;
    }

    uint64_t strips_expected = (info->height + info->rows_per_strip - 1u) / info->rows_per_strip;
    if (info->strip_offsets_count < strips_expected) {
        set_err(err, errcap, "invalid number of strips");
        return 0;
    }

    for (uint64_t i = 0; i < strips_expected; i++) {
        uint64_t row_start = i * (uint64_t)info->rows_per_strip;
        if (row_start >= info->height) {
            break;
        }
        uint32_t strip_rows = info->rows_per_strip;
        if (row_start + strip_rows > info->height) {
            strip_rows = info->height - (uint32_t)row_start;
        }

        uint64_t blocks_across = (info->width + hsub - 1u) / hsub;
        uint64_t blocks_down = (strip_rows + vsub - 1u) / vsub;
        size_t block_samples = (size_t)hsub * (size_t)vsub + 2u;
        size_t expected = 0;
        if (mul_overflow_size_t((size_t)blocks_across, (size_t)blocks_down, &expected) ||
            mul_overflow_size_t(expected, block_samples, &expected)) {
            set_err(err, errcap, "invalid YCbCr strip size");
            return 0;
        }

        uint64_t off = info->strip_offsets[i];
        if (off >= ctx->size) {
            set_err(err, errcap, "TIFF strip offset out of range");
            return 0;
        }
        size_t src_size = expected;
        if (info->compression != TIFF_COMPRESSION_NONE) {
            if (!info->strip_byte_counts || i >= info->strip_byte_counts_count) {
                set_err(err, errcap, "invalid StripByteCounts");
                return 0;
            }
            src_size = (size_t)info->strip_byte_counts[i];
            if (src_size == 0) {
                set_err(err, errcap, "invalid StripByteCounts");
                return 0;
            }
        } else if (info->strip_byte_counts && i < info->strip_byte_counts_count) {
            if (info->strip_byte_counts[i] < expected) {
                set_err(err, errcap, "truncated TIFF strip");
                return 0;
            }
        }
        if (off + src_size > ctx->size) {
            set_err(err, errcap, "truncated TIFF strip");
            return 0;
        }

        uint8_t *buf = (uint8_t *)malloc(expected);
        if (!buf) {
            set_err(err, errcap, "out of memory");
            return 0;
        }
        if (!tiff_decompress_block(ctx, info, ctx->data + (size_t)off, src_size,
                                   buf, expected, info->width, strip_rows, err, errcap)) {
            free(buf);
            return 0;
        }

        size_t pos = 0;
        for (uint64_t by = 0; by < blocks_down; by++) {
            for (uint64_t bx = 0; bx < blocks_across; bx++) {
                uint8_t yblock[16];
                size_t ycount = (size_t)hsub * (size_t)vsub;
                if (ycount > sizeof(yblock)) {
                    free(buf);
                    set_err(err, errcap, "unsupported YCbCr subsampling");
                    return 0;
                }
                for (size_t k = 0; k < ycount; k++) {
                    if (pos >= expected) {
                        free(buf);
                        set_err(err, errcap, "truncated YCbCr data");
                        return 0;
                    }
                    yblock[k] = buf[pos++];
                }
                if (pos + 1 >= expected) {
                    free(buf);
                    set_err(err, errcap, "truncated YCbCr data");
                    return 0;
                }
                uint8_t cb = buf[pos++];
                uint8_t cr = buf[pos++];
                int cbi = (int)cb - 128;
                int cri = (int)cr - 128;
                for (uint32_t ry = 0; ry < vsub; ry++) {
                    uint32_t y = (uint32_t)row_start + (uint32_t)by * vsub + ry;
                    if (y >= info->height) {
                        continue;
                    }
                    for (uint32_t rx = 0; rx < hsub; rx++) {
                        uint32_t x = (uint32_t)bx * hsub + rx;
                        if (x >= info->width) {
                            continue;
                        }
                        uint8_t yv = yblock[(size_t)ry * hsub + rx];
                        int ri = (int)yv + (int)(1.402f * cri);
                        int gi = (int)yv - (int)(0.344136f * cbi) - (int)(0.714136f * cri);
                        int bi = (int)yv + (int)(1.772f * cbi);
                        size_t dst = ((size_t)y * (size_t)info->width + x) * 4u;
                        rgba[dst + 0] = tiff_clamp8(ri);
                        rgba[dst + 1] = tiff_clamp8(gi);
                        rgba[dst + 2] = tiff_clamp8(bi);
                        rgba[dst + 3] = 255;
                    }
                }
            }
        }

        free(buf);
    }

    return 1;
}

static int tiff_convert_to_rgba(const tiff_context *ctx, const tiff_image_info *info,
                                const uint8_t *raw, size_t row_bytes,
                                uint8_t *rgba, size_t rgba_size,
                                char *err, size_t errcap) {
    uint16_t bps = info->bits_per_sample[0];
    int photometric = info->photometric;
    int samples = info->samples_per_pixel;

    size_t expected_rgba = (size_t)info->width * (size_t)info->height * 4u;
    if (rgba_size < expected_rgba) {
        set_err(err, errcap, "invalid RGBA buffer");
        return 0;
    }

    if (bps == 0) {
        set_err(err, errcap, "unsupported TIFF bit depth");
        return 0;
    }
    if (info->sample_format == 3) {
        if (bps != 32 && bps != 64) {
            set_err(err, errcap, "unsupported TIFF float bit depth");
            return 0;
        }
    } else if (bps > 32) {
        set_err(err, errcap, "unsupported TIFF bit depth");
        return 0;
    }

    int base_samples = 1;
    if (photometric == TIFF_PHOTOMETRIC_RGB) {
        base_samples = 3;
    } else if (photometric == TIFF_PHOTOMETRIC_CMYK) {
        base_samples = 4;
    } else if (photometric == TIFF_PHOTOMETRIC_YCBCR) {
        base_samples = 3;
    } else if (photometric == TIFF_PHOTOMETRIC_CIELAB) {
        base_samples = 3;
    }
    if (samples < base_samples) {
        set_err(err, errcap, "invalid TIFF samples per pixel");
        return 0;
    }

    int alpha_index = -1;
    int premultiplied = 0;
    for (uint16_t i = 0; i < info->extra_count; i++) {
        if (info->extra_samples[i] == TIFF_EXTRA_SAMPLE_ASSOC_ALPHA ||
            info->extra_samples[i] == TIFF_EXTRA_SAMPLE_UNASSOC_ALPHA) {
            alpha_index = base_samples + (int)i;
            premultiplied = (info->extra_samples[i] == TIFF_EXTRA_SAMPLE_ASSOC_ALPHA);
            break;
        }
    }
    if (alpha_index < 0 && samples == base_samples + 1) {
        alpha_index = base_samples;
    }
    if (alpha_index >= samples) {
        alpha_index = -1;
    }

    uint32_t palette_len = 0;
    if (photometric == TIFF_PHOTOMETRIC_PALETTE) {
        if (!info->color_map || info->color_map_count == 0) {
            set_err(err, errcap, "missing TIFF ColorMap");
            return 0;
        }
        if ((info->color_map_count % 3u) != 0) {
            set_err(err, errcap, "invalid TIFF ColorMap");
            return 0;
        }
        palette_len = (uint32_t)(info->color_map_count / 3u);
    }

    int float_samples = (info->sample_format == 3);
    for (uint32_t y = 0; y < info->height; y++) {
        const uint8_t *row = raw + (size_t)y * row_bytes;
        for (uint32_t x = 0; x < info->width; x++) {
            uint8_t r = 0;
            uint8_t g = 0;
            uint8_t b = 0;
            uint8_t a = 255;

            if (photometric == TIFF_PHOTOMETRIC_RGB) {
                if (float_samples) {
                    r = tiff_scale_float(tiff_read_sample_float(ctx, row, x * samples + 0, bps));
                    g = tiff_scale_float(tiff_read_sample_float(ctx, row, x * samples + 1, bps));
                    b = tiff_scale_float(tiff_read_sample_float(ctx, row, x * samples + 2, bps));
                } else {
                    uint32_t r0 = tiff_read_sample(ctx, row, x * samples + 0, bps, info->fill_order);
                    uint32_t g0 = tiff_read_sample(ctx, row, x * samples + 1, bps, info->fill_order);
                    uint32_t b0 = tiff_read_sample(ctx, row, x * samples + 2, bps, info->fill_order);
                    r = tiff_scale_sample(r0, bps);
                    g = tiff_scale_sample(g0, bps);
                    b = tiff_scale_sample(b0, bps);
                }
            } else if (photometric == TIFF_PHOTOMETRIC_BLACK_IS_ZERO ||
                       photometric == TIFF_PHOTOMETRIC_WHITE_IS_ZERO) {
                uint8_t gray = 0;
                if (float_samples) {
                    gray = tiff_scale_float(tiff_read_sample_float(ctx, row, x * samples, bps));
                } else {
                    uint32_t v = tiff_read_sample(ctx, row, x * samples, bps, info->fill_order);
                    gray = tiff_scale_sample(v, bps);
                }
                if (photometric == TIFF_PHOTOMETRIC_WHITE_IS_ZERO) {
                    gray = (uint8_t)(255u - gray);
                }
                r = g = b = gray;
            } else if (photometric == TIFF_PHOTOMETRIC_PALETTE) {
                uint32_t idx = tiff_read_sample(ctx, row, x * samples, bps, info->fill_order);
                if (idx >= palette_len) {
                    set_err(err, errcap, "invalid TIFF palette index");
                    return 0;
                }
                uint16_t pr = info->color_map[idx];
                uint16_t pg = info->color_map[idx + palette_len];
                uint16_t pb = info->color_map[idx + palette_len * 2u];
                r = sample16_to_8(pr);
                g = sample16_to_8(pg);
                b = sample16_to_8(pb);
            } else if (photometric == TIFF_PHOTOMETRIC_CMYK) {
                uint8_t c, m, yv, k;
                if (float_samples) {
                    c = tiff_scale_float(tiff_read_sample_float(ctx, row, x * samples + 0, bps));
                    m = tiff_scale_float(tiff_read_sample_float(ctx, row, x * samples + 1, bps));
                    yv = tiff_scale_float(tiff_read_sample_float(ctx, row, x * samples + 2, bps));
                    k = tiff_scale_float(tiff_read_sample_float(ctx, row, x * samples + 3, bps));
                } else {
                    c = tiff_scale_sample(tiff_read_sample(ctx, row, x * samples + 0, bps, info->fill_order), bps);
                    m = tiff_scale_sample(tiff_read_sample(ctx, row, x * samples + 1, bps, info->fill_order), bps);
                    yv = tiff_scale_sample(tiff_read_sample(ctx, row, x * samples + 2, bps, info->fill_order), bps);
                    k = tiff_scale_sample(tiff_read_sample(ctx, row, x * samples + 3, bps, info->fill_order), bps);
                }
                r = (uint8_t)(((255u - c) * (255u - k) + 127u) / 255u);
                g = (uint8_t)(((255u - m) * (255u - k) + 127u) / 255u);
                b = (uint8_t)(((255u - yv) * (255u - k) + 127u) / 255u);
            } else if (photometric == TIFF_PHOTOMETRIC_YCBCR) {
                uint8_t yv = tiff_scale_sample(tiff_read_sample(ctx, row, x * samples + 0, bps, info->fill_order), bps);
                uint8_t cb = tiff_scale_sample(tiff_read_sample(ctx, row, x * samples + 1, bps, info->fill_order), bps);
                uint8_t cr = tiff_scale_sample(tiff_read_sample(ctx, row, x * samples + 2, bps, info->fill_order), bps);
                int cbi = (int)cb - 128;
                int cri = (int)cr - 128;
                int ri = (int)yv + (int)(1.402f * cri);
                int gi = (int)yv - (int)(0.344136f * cbi) - (int)(0.714136f * cri);
                int bi = (int)yv + (int)(1.772f * cbi);
                r = tiff_clamp8(ri);
                g = tiff_clamp8(gi);
                b = tiff_clamp8(bi);
            } else if (photometric == TIFF_PHOTOMETRIC_CIELAB) {
                uint8_t l8 = tiff_scale_sample(tiff_read_sample(ctx, row, x * samples + 0, bps, info->fill_order), bps);
                uint8_t a8 = tiff_scale_sample(tiff_read_sample(ctx, row, x * samples + 1, bps, info->fill_order), bps);
                uint8_t b8 = tiff_scale_sample(tiff_read_sample(ctx, row, x * samples + 2, bps, info->fill_order), bps);
                double L = ((double)l8 * 100.0) / 255.0;
                double a = (double)a8 - 128.0;
                double bb = (double)b8 - 128.0;
                double fy = (L + 16.0) / 116.0;
                double fx = fy + (a / 500.0);
                double fz = fy - (bb / 200.0);
                double xr = (fx * fx * fx > 0.008856) ? fx * fx * fx : (fx - 16.0 / 116.0) / 7.787;
                double yr = (fy * fy * fy > 0.008856) ? fy * fy * fy : (fy - 16.0 / 116.0) / 7.787;
                double zr = (fz * fz * fz > 0.008856) ? fz * fz * fz : (fz - 16.0 / 116.0) / 7.787;
                double X = xr * 0.95047;
                double Y = yr * 1.00000;
                double Z = zr * 1.08883;
                double rr = X * 3.2406 + Y * -1.5372 + Z * -0.4986;
                double gg = X * -0.9689 + Y * 1.8758 + Z * 0.0415;
                double bb2 = X * 0.0557 + Y * -0.2040 + Z * 1.0570;
                r = tiff_clamp8((int)(rr * 255.0 + 0.5));
                g = tiff_clamp8((int)(gg * 255.0 + 0.5));
                b = tiff_clamp8((int)(bb2 * 255.0 + 0.5));
            } else {
                set_err(err, errcap, "unsupported TIFF photometric");
                return 0;
            }

            if (alpha_index >= 0) {
                if (float_samples) {
                    a = tiff_scale_float(tiff_read_sample_float(ctx, row, x * samples + (uint32_t)alpha_index, bps));
                } else {
                    uint32_t av = tiff_read_sample(ctx, row, x * samples + (uint32_t)alpha_index,
                                                   bps, info->fill_order);
                    a = tiff_scale_sample(av, bps);
                }
                if (premultiplied && a > 0 && a < 255) {
                    r = (uint8_t)((r * 255u + a / 2u) / a);
                    g = (uint8_t)((g * 255u + a / 2u) / a);
                    b = (uint8_t)((b * 255u + a / 2u) / a);
                }
            }

            size_t dst = ((size_t)y * (size_t)info->width + x) * 4u;
            rgba[dst + 0] = r;
            rgba[dst + 1] = g;
            rgba[dst + 2] = b;
            rgba[dst + 3] = a;
        }
    }

    return 1;
}

static int tiff_apply_orientation_rgba(uint8_t **rgba, uint32_t *width, uint32_t *height,
                                       uint16_t orientation, char *err, size_t errcap) {
    if (orientation == 1) {
        return 1;
    }
    if (orientation < 1 || orientation > 8) {
        set_err(err, errcap, "invalid TIFF orientation");
        return 0;
    }

    uint32_t src_w = *width;
    uint32_t src_h = *height;
    uint32_t dst_w = (orientation >= 5) ? src_h : src_w;
    uint32_t dst_h = (orientation >= 5) ? src_w : src_h;
    size_t dst_size = (size_t)dst_w * (size_t)dst_h * 4u;
    uint8_t *dst = (uint8_t *)malloc(dst_size);
    if (!dst) {
        set_err(err, errcap, "out of memory");
        return 0;
    }

    for (uint32_t y = 0; y < dst_h; y++) {
        for (uint32_t x = 0; x < dst_w; x++) {
            uint32_t sx = 0;
            uint32_t sy = 0;
            switch (orientation) {
                case 1: sx = x; sy = y; break;
                case 2: sx = src_w - 1u - x; sy = y; break;
                case 3: sx = src_w - 1u - x; sy = src_h - 1u - y; break;
                case 4: sx = x; sy = src_h - 1u - y; break;
                case 5: sx = y; sy = x; break;
                case 6: sx = y; sy = src_h - 1u - x; break;
                case 7: sx = src_w - 1u - y; sy = src_h - 1u - x; break;
                case 8: sx = src_w - 1u - y; sy = x; break;
                default: sx = x; sy = y; break;
            }
            size_t src_idx = ((size_t)sy * (size_t)src_w + sx) * 4u;
            size_t dst_idx = ((size_t)y * (size_t)dst_w + x) * 4u;
            dst[dst_idx + 0] = (*rgba)[src_idx + 0];
            dst[dst_idx + 1] = (*rgba)[src_idx + 1];
            dst[dst_idx + 2] = (*rgba)[src_idx + 2];
            dst[dst_idx + 3] = (*rgba)[src_idx + 3];
        }
    }

    free(*rgba);
    *rgba = dst;
    *width = dst_w;
    *height = dst_h;
    return 1;
}

static int tiff_load_page(const unsigned char *data, size_t size,
                          int page_index,
                          cupidimage_image *out,
                          char *err, size_t errcap) {
    tiff_context ctx;
    if (!tiff_parse_header(data, size, &ctx, err, errcap)) {
        return 0;
    }

    if (page_index < 0) {
        set_err(err, errcap, "invalid TIFF page index");
        return 0;
    }

    uint64_t *ifd_list = NULL;
    size_t ifd_count = 0;
    size_t ifd_cap = 0;
    uint64_t *sub_list = NULL;
    size_t sub_count = 0;
    size_t sub_cap = 0;

    uint64_t ifd_offset = ctx.first_ifd;
    for (int guard = 0; guard < 10000 && ifd_offset != 0; guard++) {
        if (!tiff_ifd_list_append(&ifd_list, &ifd_count, &ifd_cap, ifd_offset)) {
            free(ifd_list);
            free(sub_list);
            set_err(err, errcap, "out of memory");
            return 0;
        }
        if (!tiff_collect_subifds(&ctx, ifd_offset, &sub_list, &sub_count, &sub_cap, err, errcap)) {
            free(ifd_list);
            free(sub_list);
            return 0;
        }
        uint64_t entry_count = 0;
        uint64_t next_ifd = 0;
        if (!tiff_read_ifd_header(&ctx, ifd_offset, &entry_count, &next_ifd, err, errcap)) {
            free(ifd_list);
            free(sub_list);
            return 0;
        }
        ifd_offset = next_ifd;
    }

    for (size_t i = 0; i < sub_count; i++) {
        if (!tiff_ifd_list_append(&ifd_list, &ifd_count, &ifd_cap, sub_list[i])) {
            free(ifd_list);
            free(sub_list);
            set_err(err, errcap, "out of memory");
            return 0;
        }
    }
    free(sub_list);

    if ((size_t)page_index >= ifd_count) {
        free(ifd_list);
        set_err(err, errcap, "TIFF page index out of range");
        return 0;
    }
    ifd_offset = ifd_list[page_index];
    free(ifd_list);

    tiff_image_info info;
    tiff_info_init(&info);
    if (!tiff_parse_image_info(&ctx, ifd_offset, &info, err, errcap)) {
        tiff_info_free(&info);
        return 0;
    }

    if (info.bits_count == 0) {
        if ((info.photometric == TIFF_PHOTOMETRIC_WHITE_IS_ZERO ||
             info.photometric == TIFF_PHOTOMETRIC_BLACK_IS_ZERO) &&
            info.samples_per_pixel == 1) {
            info.bits_count = 1;
            info.bits_per_sample[0] = 1;
        } else {
            set_err(err, errcap, "missing BitsPerSample");
            tiff_info_free(&info);
            return 0;
        }
    }

    uint16_t bps = info.bits_per_sample[0];
    if (bps == 0) {
        set_err(err, errcap, "unsupported BitsPerSample");
        tiff_info_free(&info);
        return 0;
    }
    if (info.sample_format == 3) {
        if (bps != 32 && bps != 64) {
            set_err(err, errcap, "unsupported float BitsPerSample");
            tiff_info_free(&info);
            return 0;
        }
    } else if (bps > 32) {
        set_err(err, errcap, "unsupported BitsPerSample");
        tiff_info_free(&info);
        return 0;
    }
    for (uint16_t i = 1; i < info.bits_count; i++) {
        if (info.bits_per_sample[i] != bps) {
            set_err(err, errcap, "unsupported TIFF bit depths");
            tiff_info_free(&info);
            return 0;
        }
    }

    if (info.bits_count > 1 && info.bits_count != info.samples_per_pixel) {
        set_err(err, errcap, "invalid BitsPerSample count");
        tiff_info_free(&info);
        return 0;
    }

    if (info.samples_per_pixel == 0 || info.samples_per_pixel > 8) {
        set_err(err, errcap, "invalid SamplesPerPixel");
        tiff_info_free(&info);
        return 0;
    }

    if (info.photometric == TIFF_PHOTOMETRIC_RGB) {
        if (info.samples_per_pixel < 3) {
            set_err(err, errcap, "invalid RGB SamplesPerPixel");
            tiff_info_free(&info);
            return 0;
        }
        if (info.samples_per_pixel > 4) {
            set_err(err, errcap, "unsupported RGB samples");
            tiff_info_free(&info);
            return 0;
        }
    } else if (info.photometric == TIFF_PHOTOMETRIC_CMYK) {
        if (info.samples_per_pixel < 4) {
            set_err(err, errcap, "invalid CMYK SamplesPerPixel");
            tiff_info_free(&info);
            return 0;
        }
    } else if (info.photometric == TIFF_PHOTOMETRIC_YCBCR) {
        if (info.samples_per_pixel < 3) {
            set_err(err, errcap, "invalid YCbCr SamplesPerPixel");
            tiff_info_free(&info);
            return 0;
        }
    } else if (info.photometric == TIFF_PHOTOMETRIC_CIELAB) {
        if (info.samples_per_pixel < 3) {
            set_err(err, errcap, "invalid CIELAB SamplesPerPixel");
            tiff_info_free(&info);
            return 0;
        }
    } else if (info.photometric == TIFF_PHOTOMETRIC_PALETTE) {
        if (bps != 1 && bps != 2 && bps != 4 && bps != 8) {
            set_err(err, errcap, "unsupported palette bit depth");
            tiff_info_free(&info);
            return 0;
        }
        if (info.samples_per_pixel != 1) {
            set_err(err, errcap, "palette TIFF with multiple samples not supported");
            tiff_info_free(&info);
            return 0;
        }
    } else if (info.photometric == TIFF_PHOTOMETRIC_WHITE_IS_ZERO ||
               info.photometric == TIFF_PHOTOMETRIC_BLACK_IS_ZERO) {
        if (info.samples_per_pixel > 2) {
            set_err(err, errcap, "unsupported grayscale samples");
            tiff_info_free(&info);
            return 0;
        }
        if (info.sample_format == 3) {
            if (bps != 32 && bps != 64) {
                set_err(err, errcap, "unsupported grayscale bit depth");
                tiff_info_free(&info);
                return 0;
            }
        } else if (bps != 1 && bps != 2 && bps != 4 && bps != 8 && bps != 16 && bps != 32) {
            set_err(err, errcap, "unsupported grayscale bit depth");
            tiff_info_free(&info);
            return 0;
        }
    } else {
        set_err(err, errcap, "unsupported TIFF photometric");
        tiff_info_free(&info);
        return 0;
    }

    if (info.fill_order != TIFF_FILL_ORDER_MSB2LSB && info.fill_order != TIFF_FILL_ORDER_LSB2MSB) {
        set_err(err, errcap, "invalid TIFF FillOrder");
        tiff_info_free(&info);
        return 0;
    }

    if (info.compression != TIFF_COMPRESSION_NONE &&
        info.compression != TIFF_COMPRESSION_LZW &&
        info.compression != TIFF_COMPRESSION_PACKBITS &&
        info.compression != TIFF_COMPRESSION_DEFLATE &&
        info.compression != TIFF_COMPRESSION_ADOBE_DEFLATE &&
        info.compression != TIFF_COMPRESSION_JPEG &&
        info.compression != TIFF_COMPRESSION_CCITT_RLE &&
        info.compression != TIFF_COMPRESSION_CCITT_G3 &&
        info.compression != TIFF_COMPRESSION_CCITT_G4) {
        set_err(err, errcap, "unsupported TIFF compression");
        tiff_info_free(&info);
        return 0;
    }

    if (info.strip_byte_counts && info.strip_byte_counts_count < info.strip_offsets_count) {
        set_err(err, errcap, "invalid StripByteCounts count");
        tiff_info_free(&info);
        return 0;
    }
    if (info.tile_byte_counts && info.tile_byte_counts_count < info.tile_offsets_count) {
        set_err(err, errcap, "invalid TileByteCounts count");
        tiff_info_free(&info);
        return 0;
    }

    if ((info.compression == TIFF_COMPRESSION_CCITT_RLE ||
         info.compression == TIFF_COMPRESSION_CCITT_G3 ||
         info.compression == TIFF_COMPRESSION_CCITT_G4) &&
        (bps != 1 || info.samples_per_pixel != 1)) {
        set_err(err, errcap, "CCITT compression requires 1-bit bilevel");
        tiff_info_free(&info);
        return 0;
    }

    if (info.compression == TIFF_COMPRESSION_JPEG) {
        uint8_t *jpeg_rgba = NULL;
        uint32_t jpeg_w = 0;
        uint32_t jpeg_h = 0;
        if (!tiff_decode_jpeg_image(&ctx, &info, &jpeg_rgba, &jpeg_w, &jpeg_h, err, errcap)) {
            tiff_info_free(&info);
            return 0;
        }
        if (!tiff_apply_orientation_rgba(&jpeg_rgba, &jpeg_w, &jpeg_h, info.orientation, err, errcap)) {
            free(jpeg_rgba);
            tiff_info_free(&info);
            return 0;
        }
        out->width = jpeg_w;
        out->height = jpeg_h;
        out->rgba = jpeg_rgba;
        tiff_info_free(&info);
        return 1;
    }

    if (info.photometric == TIFF_PHOTOMETRIC_YCBCR &&
        (info.ycbcr_subsampling[0] != 1 || info.ycbcr_subsampling[1] != 1)) {
        size_t rgba_size = 0;
        if (mul_overflow_size_t((size_t)info.width, (size_t)info.height, &rgba_size) ||
            mul_overflow_size_t(rgba_size, 4u, &rgba_size)) {
            set_err(err, errcap, "invalid TIFF image size");
            tiff_info_free(&info);
            return 0;
        }
        uint8_t *rgba = (uint8_t *)malloc(rgba_size);
        if (!rgba) {
            set_err(err, errcap, "out of memory");
            tiff_info_free(&info);
            return 0;
        }
        if (!tiff_decode_ycbcr_subsampled(&ctx, &info, rgba, rgba_size, err, errcap)) {
            free(rgba);
            tiff_info_free(&info);
            return 0;
        }
        uint32_t out_w = info.width;
        uint32_t out_h = info.height;
        if (!tiff_apply_orientation_rgba(&rgba, &out_w, &out_h, info.orientation, err, errcap)) {
            free(rgba);
            tiff_info_free(&info);
            return 0;
        }
        out->width = out_w;
        out->height = out_h;
        out->rgba = rgba;
        tiff_info_free(&info);
        return 1;
    }

    size_t row_bytes = 0;
    size_t bits_per_row = 0;
    if (mul_overflow_size_t((size_t)info.width, (size_t)info.samples_per_pixel, &bits_per_row) ||
        mul_overflow_size_t(bits_per_row, (size_t)bps, &bits_per_row)) {
        set_err(err, errcap, "invalid TIFF row size");
        tiff_info_free(&info);
        return 0;
    }
    row_bytes = (bits_per_row + 7u) / 8u;

    size_t raw_size = 0;
    if (mul_overflow_size_t(row_bytes, (size_t)info.height, &raw_size)) {
        set_err(err, errcap, "invalid TIFF image size");
        tiff_info_free(&info);
        return 0;
    }

    size_t bytes_per_pixel = 0;
    if ((bps & 7u) == 0) {
        if (mul_overflow_size_t((size_t)info.samples_per_pixel, (size_t)(bps / 8u), &bytes_per_pixel)) {
            set_err(err, errcap, "invalid TIFF pixel size");
            tiff_info_free(&info);
            return 0;
        }
    }

    if (info.planar_config == TIFF_PLANAR_SEPARATE && bytes_per_pixel == 0) {
        set_err(err, errcap, "planar TIFF requires byte-aligned samples");
        tiff_info_free(&info);
        return 0;
    }

    size_t rgba_size = 0;
    if (mul_overflow_size_t((size_t)info.width, (size_t)info.height, &rgba_size) ||
        mul_overflow_size_t(rgba_size, 4u, &rgba_size)) {
        set_err(err, errcap, "invalid TIFF image size");
        tiff_info_free(&info);
        return 0;
    }

    uint8_t *raw = (uint8_t *)malloc(raw_size);
    if (!raw) {
        set_err(err, errcap, "out of memory");
        tiff_info_free(&info);
        return 0;
    }

    if (!tiff_decode_image(&ctx, &info, raw, raw_size, row_bytes, bytes_per_pixel, err, errcap)) {
        free(raw);
        tiff_info_free(&info);
        return 0;
    }

    if (info.predictor == 2 &&
        info.planar_config == TIFF_PLANAR_CHUNKY &&
        (info.tile_offsets == NULL || info.tile_offsets_count == 0)) {
        if (!tiff_apply_predictor(&ctx, &info, raw, row_bytes, err, errcap)) {
            free(raw);
            tiff_info_free(&info);
            return 0;
        }
    }

    uint8_t *rgba = (uint8_t *)malloc(rgba_size);
    if (!rgba) {
        free(raw);
        tiff_info_free(&info);
        set_err(err, errcap, "out of memory");
        return 0;
    }

    if (!tiff_convert_to_rgba(&ctx, &info, raw, row_bytes, rgba, rgba_size, err, errcap)) {
        free(raw);
        free(rgba);
        tiff_info_free(&info);
        return 0;
    }

    free(raw);
    uint32_t out_w = info.width;
    uint32_t out_h = info.height;
    if (!tiff_apply_orientation_rgba(&rgba, &out_w, &out_h, info.orientation, err, errcap)) {
        free(rgba);
        tiff_info_free(&info);
        return 0;
    }
    tiff_info_free(&info);

    out->width = out_w;
    out->height = out_h;
    out->rgba = rgba;
    return 1;
}

int cupidimage_load_tiff_page(const unsigned char *data, size_t size,
                              cupidimage_image *out,
                              int page_index,
                              char *err, size_t errcap) {
    if (!data || !out) {
        set_err(err, errcap, "invalid arguments");
        return 0;
    }

    memset(out, 0, sizeof(*out));
    return tiff_load_page(data, size, page_index, out, err, errcap);
}

int cupidimage_load_tiff(const unsigned char *data, size_t size,
                         cupidimage_image *out,
                         char *err, size_t errcap) {
    return cupidimage_load_tiff_page(data, size, out, 0, err, errcap);
}

int cupidimage_get_tiff_page_count(const unsigned char *data, size_t size,
                                   int *count,
                                   char *err, size_t errcap) {
    if (!data || !count) {
        set_err(err, errcap, "invalid arguments");
        return 0;
    }

    tiff_context ctx;
    if (!tiff_parse_header(data, size, &ctx, err, errcap)) {
        return 0;
    }

    uint64_t *ifd_list = NULL;
    size_t ifd_count = 0;
    size_t ifd_cap = 0;
    uint64_t *sub_list = NULL;
    size_t sub_count = 0;
    size_t sub_cap = 0;

    uint64_t ifd_offset = ctx.first_ifd;
    for (int guard = 0; guard < 10000 && ifd_offset != 0; guard++) {
        if (!tiff_ifd_list_append(&ifd_list, &ifd_count, &ifd_cap, ifd_offset)) {
            free(ifd_list);
            free(sub_list);
            set_err(err, errcap, "out of memory");
            return 0;
        }
        if (!tiff_collect_subifds(&ctx, ifd_offset, &sub_list, &sub_count, &sub_cap, err, errcap)) {
            free(ifd_list);
            free(sub_list);
            return 0;
        }
        uint64_t entry_count = 0;
        uint64_t next_ifd = 0;
        if (!tiff_read_ifd_header(&ctx, ifd_offset, &entry_count, &next_ifd, err, errcap)) {
            free(ifd_list);
            free(sub_list);
            return 0;
        }
        ifd_offset = next_ifd;
    }

    for (size_t i = 0; i < sub_count; i++) {
        if (!tiff_ifd_list_append(&ifd_list, &ifd_count, &ifd_cap, sub_list[i])) {
            free(ifd_list);
            free(sub_list);
            set_err(err, errcap, "out of memory");
            return 0;
        }
    }
    free(sub_list);

    *count = (int)ifd_count;
    free(ifd_list);
    return 1;
}

int cupidimage_load_tiff_file(const char *path,
                              cupidimage_image *out,
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

    int ok = cupidimage_load_tiff(buf, (size_t)fsize, out, err, errcap);
    free(buf);
    return ok;
}
