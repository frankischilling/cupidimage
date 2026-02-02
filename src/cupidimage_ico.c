#include "cupidimage.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef CUPIDIMAGE_ICO_DEBUG
#define ICO_DEBUG(...) fprintf(stderr, "ICO: " __VA_ARGS__)
#else
#define ICO_DEBUG(...) ((void)0)
#endif

#define ICO_TYPE_ICON 1u
#define ICO_TYPE_CURSOR 2u
#define ICO_MAX_COUNT 256u

#define BI_RGB 0u
#define BI_PNG 5u

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

static int ico_read_file(const char *path, unsigned char **data, size_t *size,
                         char *err, size_t errcap) {
    if (!path || !data || !size) {
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
    if ((unsigned long)fsize > SIZE_MAX) {
        fclose(f);
        set_err(err, errcap, "file too large");
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

    *data = buf;
    *size = (size_t)fsize;
    return 1;
}

static int ico_parse_directory(const unsigned char *data, size_t size,
                               cupidimage_ico_entry **entries_out,
                               int *count_out,
                               int *is_cursor_out,
                               char *err, size_t errcap) {
    if (!data || !entries_out || !count_out) {
        set_err(err, errcap, "invalid arguments");
        return 0;
    }

    if (size < 6) {
        set_err(err, errcap, "file too small");
        return 0;
    }

    uint16_t reserved = read_le16(data);
    uint16_t type = read_le16(data + 2);
    uint16_t count = read_le16(data + 4);
    if (type != ICO_TYPE_ICON && type != ICO_TYPE_CURSOR) {
        set_err(err, errcap, "invalid ICO/CUR header");
        return 0;
    }
    if (count == 0 || count > ICO_MAX_COUNT) {
        set_err(err, errcap, "invalid icon count");
        return 0;
    }

    size_t dir_bytes = (size_t)count * 16u;
    if (size < 6u + dir_bytes) {
        set_err(err, errcap, "file too small");
        return 0;
    }

    if (reserved != 0) {
        ICO_DEBUG("warning: reserved field=%u\n", reserved);
    }

    cupidimage_ico_entry *entries = (cupidimage_ico_entry *)calloc(count, sizeof(*entries));
    if (!entries) {
        set_err(err, errcap, "out of memory");
        return 0;
    }

    ICO_DEBUG("type=%s count=%u\n", (type == ICO_TYPE_CURSOR) ? "CUR" : "ICO", count);
    for (uint16_t i = 0; i < count; i++) {
        size_t off = 6u + (size_t)i * 16u;
        cupidimage_ico_entry entry;
        entry.width = data[off + 0];
        entry.height = data[off + 1];
        entry.colors = data[off + 2];
        entry.reserved = data[off + 3];
        entry.planes = read_le16(data + off + 4);
        entry.bitcount = read_le16(data + off + 6);
        entry.size = read_le32(data + off + 8);
        entry.offset = read_le32(data + off + 12);

        if (entry.reserved != 0) {
            ICO_DEBUG("warning: entry %u reserved=%u\n", i, entry.reserved);
        }

        if (entry.offset >= size) {
            free(entries);
            set_err(err, errcap, "invalid icon directory");
            return 0;
        }
        if (entry.size == 0 || entry.size > size - entry.offset) {
            free(entries);
            set_err(err, errcap, "truncated icon data");
            return 0;
        }

        entries[i] = entry;
#ifdef CUPIDIMAGE_ICO_DEBUG
        uint32_t w = entry.width ? entry.width : 256u;
        uint32_t h = entry.height ? entry.height : 256u;
        ICO_DEBUG("entry %u: %ux%u size=%u offset=%u\n", i, w, h, entry.size, entry.offset);
#endif
    }

    *entries_out = entries;
    *count_out = (int)count;
    if (is_cursor_out) {
        *is_cursor_out = (type == ICO_TYPE_CURSOR) ? 1 : 0;
    }
    return 1;
}

static int ico_find_png(const unsigned char *data, size_t size, size_t start,
                        const unsigned char **png_data, size_t *png_size) {
    if (start + 4u > size) {
        return 0;
    }
    for (size_t i = start; i + 4u <= size; i++) {
        if (data[i] == 0x89 && data[i + 1] == 0x50 && data[i + 2] == 0x4E && data[i + 3] == 0x47) {
            *png_data = data + i;
            *png_size = size - i;
            return 1;
        }
    }
    return 0;
}

static int ico_decode_dib(const unsigned char *data, size_t size,
                          const cupidimage_ico_entry *entry,
                          int is_cursor,
                          cupidimage_image *out,
                          char *err, size_t errcap) {
    (void)is_cursor;
    const unsigned char *buf = data + entry->offset;
    size_t buf_size = entry->size;
    if (buf_size < 40) {
        set_err(err, errcap, "invalid DIB header size");
        return 0;
    }
    if (buf_size > size - entry->offset) {
        set_err(err, errcap, "truncated icon data");
        return 0;
    }

    uint32_t dib_size = read_le32(buf);
    if (dib_size < 40) {
        set_err(err, errcap, "invalid DIB header size");
        return 0;
    }
    if (dib_size > buf_size) {
        set_err(err, errcap, "truncated icon data");
        return 0;
    }

    int32_t width_s = (int32_t)read_le32(buf + 4);
    int32_t height_s = (int32_t)read_le32(buf + 8);
    if (width_s <= 0) {
        set_err(err, errcap, "invalid icon dimensions");
        return 0;
    }
    int top_down = 0;
    uint32_t height_raw = 0;
    if (height_s < 0) {
        top_down = 1;
        height_raw = (uint32_t)(-height_s);
    } else {
        height_raw = (uint32_t)height_s;
    }
    if (height_raw < 2) {
        set_err(err, errcap, "invalid icon dimensions");
        return 0;
    }
    uint32_t height = height_raw / 2u;
    if (height == 0) {
        set_err(err, errcap, "invalid icon dimensions");
        return 0;
    }
    if (height_raw % 2u != 0) {
        ICO_DEBUG("warning: DIB height not double (%u)\n", height_raw);
    }

    uint32_t width = (uint32_t)width_s;
    uint16_t planes = read_le16(buf + 12);
    uint16_t bitcount = read_le16(buf + 14);
    uint32_t compression = read_le32(buf + 16);
    uint32_t clr_used = read_le32(buf + 32);

    if (planes != 1) {
        ICO_DEBUG("warning: planes=%u\n", planes);
    }

    uint32_t dir_w = entry->width ? entry->width : 256u;
    uint32_t dir_h = entry->height ? entry->height : 256u;
    if (dir_w != width || dir_h != height) {
        ICO_DEBUG("warning: directory size %ux%u != DIB %ux%u\n", dir_w, dir_h, width, height);
    }
    if (!is_cursor && entry->bitcount && entry->bitcount != bitcount) {
        ICO_DEBUG("warning: directory bitcount %u != DIB %u\n", entry->bitcount, bitcount);
    }

    if (compression == BI_PNG) {
        const unsigned char *png_data = NULL;
        size_t png_size = 0;
        if (!ico_find_png(buf, buf_size, dib_size, &png_data, &png_size)) {
            set_err(err, errcap, "missing PNG data");
            return 0;
        }
        ICO_DEBUG("delegating to PNG decoder (BI_PNG)\n");
        return cupidimage_load_png(png_data, png_size, out, err, errcap);
    }

    if (compression != BI_RGB) {
        set_err(err, errcap, "unsupported DIB compression");
        return 0;
    }

    if (bitcount != 1 && bitcount != 4 && bitcount != 8 && bitcount != 24 && bitcount != 32) {
        set_err(err, errcap, "unsupported bit depth");
        return 0;
    }

    uint32_t palette_entries = 0;
    uint8_t palette[256 * 4];
    size_t palette_bytes = 0;
    if (bitcount <= 8) {
        uint32_t max_entries = 1u << bitcount;
        if (clr_used) {
            palette_entries = clr_used;
        } else if (entry->colors) {
            palette_entries = entry->colors;
        } else {
            palette_entries = max_entries;
        }
        if (palette_entries == 0) {
            set_err(err, errcap, "invalid color palette");
            return 0;
        }
        if (palette_entries > max_entries) {
            ICO_DEBUG("warning: palette entries %u > max %u\n", palette_entries, max_entries);
            palette_entries = max_entries;
        }
        palette_bytes = (size_t)palette_entries * 4u;
        if (dib_size + palette_bytes > buf_size) {
            set_err(err, errcap, "truncated icon data");
            return 0;
        }
        if (entry->colors && entry->colors != palette_entries) {
            ICO_DEBUG("warning: directory colors %u != palette %u\n", entry->colors, palette_entries);
        }
        memset(palette, 0, sizeof(palette));
        for (uint32_t i = 0; i < palette_entries; i++) {
            const uint8_t *p = buf + dib_size + i * 4u;
            palette[i * 4u + 0u] = p[2];
            palette[i * 4u + 1u] = p[1];
            palette[i * 4u + 2u] = p[0];
            palette[i * 4u + 3u] = 255;
        }
    }

    uint64_t row_bits = (uint64_t)width * (uint64_t)bitcount;
    uint64_t row_stride64 = ((row_bits + 31u) / 32u) * 4u;
    if (row_stride64 > SIZE_MAX) {
        set_err(err, errcap, "invalid icon dimensions");
        return 0;
    }
    size_t row_stride = (size_t)row_stride64;
    if (height > 0 && row_stride > SIZE_MAX / (size_t)height) {
        set_err(err, errcap, "invalid icon dimensions");
        return 0;
    }
    size_t xor_offset = dib_size + palette_bytes;
    size_t xor_size = row_stride * (size_t)height;
    if (xor_offset + xor_size > buf_size) {
        set_err(err, errcap, "truncated icon data");
        return 0;
    }

    uint64_t and_stride64 = ((uint64_t)width + 31u) / 32u * 4u;
    if (and_stride64 > SIZE_MAX) {
        set_err(err, errcap, "invalid icon dimensions");
        return 0;
    }
    size_t and_stride = (size_t)and_stride64;
    if (height > 0 && and_stride > SIZE_MAX / (size_t)height) {
        set_err(err, errcap, "invalid icon dimensions");
        return 0;
    }
    size_t and_size = and_stride * (size_t)height;
    const uint8_t *and_data = NULL;
    size_t and_offset = xor_offset + xor_size;
    if (and_offset + and_size <= buf_size) {
        and_data = buf + and_offset;
    } else {
        ICO_DEBUG("warning: missing AND mask\n");
    }

    size_t pixels = (size_t)width * (size_t)height;
    if (pixels == 0 || pixels > SIZE_MAX / 4u) {
        set_err(err, errcap, "invalid icon dimensions");
        return 0;
    }

    out->rgba = (uint8_t *)malloc(pixels * 4u);
    if (!out->rgba) {
        set_err(err, errcap, "out of memory");
        return 0;
    }
    out->width = width;
    out->height = height;

    const uint8_t *xor_data = buf + xor_offset;
    int has_alpha = 0;
    for (uint32_t row = 0; row < height; row++) {
        uint32_t dst_y = top_down ? row : (height - 1u - row);
        const uint8_t *src = xor_data + (size_t)row * row_stride;
        uint8_t *dst = out->rgba + (size_t)dst_y * (size_t)width * 4u;
        if (bitcount <= 8) {
            for (uint32_t x = 0; x < width; x++) {
                uint8_t idx = 0;
                if (bitcount == 8) {
                    idx = src[x];
                } else if (bitcount == 4) {
                    uint8_t byte = src[x / 2u];
                    idx = (x & 1u) ? (byte & 0x0Fu) : (uint8_t)(byte >> 4);
                } else if (bitcount == 1) {
                    uint8_t byte = src[x / 8u];
                    unsigned shift = 7u - (x & 7u);
                    idx = (uint8_t)((byte >> shift) & 0x01u);
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
                dst[x * 4u + 3u] = 255;
            }
        } else if (bitcount == 24) {
            for (uint32_t x = 0; x < width; x++) {
                const uint8_t *p = src + x * 3u;
                dst[x * 4u + 0u] = p[2];
                dst[x * 4u + 1u] = p[1];
                dst[x * 4u + 2u] = p[0];
                dst[x * 4u + 3u] = 255;
            }
        } else if (bitcount == 32) {
            for (uint32_t x = 0; x < width; x++) {
                const uint8_t *p = src + x * 4u;
                uint8_t a = p[3];
                if (a != 255) {
                    has_alpha = 1;
                }
                dst[x * 4u + 0u] = p[2];
                dst[x * 4u + 1u] = p[1];
                dst[x * 4u + 2u] = p[0];
                dst[x * 4u + 3u] = a;
            }
        }
    }

    if (and_data) {
        int apply = (bitcount != 32) || !has_alpha;
        if (apply) {
            for (uint32_t y = 0; y < height; y++) {
                uint32_t src_y = top_down ? y : (height - 1u - y);
                const uint8_t *mask_row = and_data + (size_t)src_y * and_stride;
                uint8_t *dst = out->rgba + (size_t)y * (size_t)width * 4u;
                for (uint32_t x = 0; x < width; x++) {
                    uint8_t byte = mask_row[x / 8u];
                    unsigned shift = 7u - (x & 7u);
                    int bit = (byte >> shift) & 0x01u;
                    if (bit) {
                        dst[x * 4u + 3u] = 0;
                    } else if (bitcount != 32) {
                        dst[x * 4u + 3u] = 255;
                    }
                }
            }
        }
    }

    size_t expected = xor_offset + xor_size + and_size;
    if (expected != buf_size) {
        ICO_DEBUG("warning: entry size %zu != expected %zu\n", buf_size, expected);
    }

    return 1;
}

static int ico_decode_entry(const unsigned char *data, size_t size,
                            const cupidimage_ico_entry *entry,
                            int is_cursor,
                            cupidimage_image *out,
                            char *err, size_t errcap) {
    size_t offset = entry->offset;
    size_t entry_size = entry->size;
    if (offset > size || entry_size > size - offset) {
        set_err(err, errcap, "truncated icon data");
        return 0;
    }

    const unsigned char *buf = data + offset;
    size_t buf_size = entry_size;

    if (buf_size >= 4 && buf[0] == 0x89 && buf[1] == 0x50 && buf[2] == 0x4E && buf[3] == 0x47) {
        ICO_DEBUG("delegating to PNG decoder (signature)\n");
        if (!cupidimage_load_png(buf, buf_size, out, err, errcap)) {
            return 0;
        }
        out->hotspot_x = is_cursor ? entry->planes : 0;
        out->hotspot_y = is_cursor ? entry->bitcount : 0;
        return 1;
    }

    ICO_DEBUG("decoding DIB\n");
    if (!ico_decode_dib(data, size, entry, is_cursor, out, err, errcap)) {
        return 0;
    }

    out->hotspot_x = is_cursor ? entry->planes : 0;
    out->hotspot_y = is_cursor ? entry->bitcount : 0;
    return 1;
}

static int ico_load_page_mem(const unsigned char *data, size_t size,
                             int page,
                             cupidimage_image *out,
                             char *err, size_t errcap) {
    if (!data || !out) {
        set_err(err, errcap, "invalid arguments");
        return 0;
    }

    memset(out, 0, sizeof(*out));

    cupidimage_ico_entry *entries = NULL;
    int count = 0;
    int is_cursor = 0;
    if (!ico_parse_directory(data, size, &entries, &count, &is_cursor, err, errcap)) {
        return 0;
    }

    if (page < 0 || page >= count) {
        free(entries);
        set_err(err, errcap, "page index out of range");
        return 0;
    }

    cupidimage_ico_entry entry = entries[page];
    free(entries);

    return ico_decode_entry(data, size, &entry, is_cursor, out, err, errcap);
}

int cupidimage_load_ico(const unsigned char *data, size_t size,
                        cupidimage_image *out,
                        char *err, size_t errcap) {
    return ico_load_page_mem(data, size, 0, out, err, errcap);
}

int cupidimage_load_ico_page(const char *path,
                             int page,
                             cupidimage_image *out,
                             char *err, size_t errcap) {
    if (!path || !out) {
        set_err(err, errcap, "invalid arguments");
        return 0;
    }

    unsigned char *data = NULL;
    size_t size = 0;
    if (!ico_read_file(path, &data, &size, err, errcap)) {
        return 0;
    }

    int ok = ico_load_page_mem(data, size, page, out, err, errcap);
    free(data);
    return ok;
}

int cupidimage_ico_get_directory(const char *path,
                                 cupidimage_ico_entry **entries,
                                 int *count,
                                 int *is_cursor,
                                 char *err, size_t errcap) {
    if (!path || !entries || !count) {
        set_err(err, errcap, "invalid arguments");
        return 0;
    }

    *entries = NULL;
    *count = 0;

    unsigned char *data = NULL;
    size_t size = 0;
    if (!ico_read_file(path, &data, &size, err, errcap)) {
        return 0;
    }

    int ok = ico_parse_directory(data, size, entries, count, is_cursor, err, errcap);
    free(data);
    return ok;
}
