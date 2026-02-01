#include "cupidimage.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static void set_err(char *err, size_t errcap, const char *msg) {
    if (err && errcap) {
        snprintf(err, errcap, "%s", msg);
    }
}

static uint16_t read_le16(const unsigned char *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

typedef struct gif_bitstream {
    const uint8_t *data;
    size_t size;
    size_t pos;
    uint32_t bitbuf;
    int bitcount;
} gif_bitstream;

static int gif_bs_read(gif_bitstream *bs, int bits, int *out) {
    while (bs->bitcount < bits) {
        if (bs->pos >= bs->size) {
            return 0;
        }
        bs->bitbuf |= (uint32_t)bs->data[bs->pos++] << bs->bitcount;
        bs->bitcount += 8;
    }
    *out = (int)(bs->bitbuf & ((1u << bits) - 1u));
    bs->bitbuf >>= bits;
    bs->bitcount -= bits;
    return 1;
}

static int gif_read_color_table(const uint8_t *data, size_t size, size_t *off,
                                int entries, uint8_t *palette,
                                char *err, size_t errcap) {
    size_t bytes = (size_t)entries * 3u;
    if (*off + bytes > size) {
        set_err(err, errcap, "truncated GIF");
        return 0;
    }
    memcpy(palette, data + *off, bytes);
    *off += bytes;
    return 1;
}

static int gif_skip_subblocks(const uint8_t *data, size_t size, size_t *off,
                              char *err, size_t errcap) {
    while (1) {
        if (*off >= size) {
            set_err(err, errcap, "truncated GIF");
            return 0;
        }
        uint8_t len = data[(*off)++];
        if (len == 0) {
            break;
        }
        if (*off + len > size) {
            set_err(err, errcap, "truncated GIF");
            return 0;
        }
        *off += len;
    }
    return 1;
}

static int gif_read_subblocks(const uint8_t *data, size_t size, size_t *off,
                              uint8_t **out, size_t *out_size,
                              char *err, size_t errcap) {
    size_t pos = *off;
    size_t total = 0;
    while (1) {
        if (pos >= size) {
            set_err(err, errcap, "truncated GIF");
            return 0;
        }
        uint8_t len = data[pos++];
        if (len == 0) {
            break;
        }
        if (pos + len > size) {
            set_err(err, errcap, "truncated GIF");
            return 0;
        }
        if (total + len < total) {
            set_err(err, errcap, "GIF data too large");
            return 0;
        }
        total += len;
        pos += len;
    }

    uint8_t *buf = NULL;
    if (total > 0) {
        buf = (uint8_t *)malloc(total);
        if (!buf) {
            set_err(err, errcap, "out of memory");
            return 0;
        }
    }

    pos = *off;
    size_t outpos = 0;
    while (1) {
        uint8_t len = data[pos++];
        if (len == 0) {
            break;
        }
        memcpy(buf + outpos, data + pos, len);
        outpos += len;
        pos += len;
    }

    *off = pos;
    *out = buf;
    *out_size = total;
    return 1;
}

static int gif_lzw_decode(const uint8_t *data, size_t size, int min_code_size,
                          uint8_t *out, size_t outcap, size_t *outlen) {
    if (min_code_size < 2 || min_code_size > 8) {
        return 0;
    }

    uint16_t prefix[4096];
    uint8_t suffix[4096];
    uint8_t stack[4096];

    int clear_code = 1 << min_code_size;
    int end_code = clear_code + 1;
    int next_code = clear_code + 2;
    int code_size = min_code_size + 1;
    int code_mask = (1 << code_size) - 1;
    int old_code = -1;

    gif_bitstream bs;
    bs.data = data;
    bs.size = size;
    bs.pos = 0;
    bs.bitbuf = 0;
    bs.bitcount = 0;

    size_t outpos = 0;
    while (1) {
        int code = 0;
        if (!gif_bs_read(&bs, code_size, &code)) {
            return 0;
        }
        if (code == clear_code) {
            next_code = clear_code + 2;
            code_size = min_code_size + 1;
            code_mask = (1 << code_size) - 1;
            old_code = -1;
            continue;
        }
        if (code == end_code) {
            break;
        }

        int sp = 0;
        int cur = code;
        int special = 0;
        if (cur == next_code) {
            if (old_code == -1) {
                return 0;
            }
            special = 1;
            cur = old_code;
        }

        if (cur < clear_code) {
            stack[sp++] = (uint8_t)cur;
        } else if (cur < next_code) {
            while (cur >= clear_code) {
                stack[sp++] = suffix[cur];
                cur = prefix[cur];
                if (sp >= 4096) {
                    return 0;
                }
            }
            stack[sp++] = (uint8_t)cur;
        } else {
            return 0;
        }

        uint8_t first = stack[sp - 1];

        for (int i = sp - 1; i >= 0; i--) {
            if (outpos >= outcap) {
                return 0;
            }
            out[outpos++] = stack[i];
        }
        if (special) {
            if (outpos >= outcap) {
                return 0;
            }
            out[outpos++] = first;
        }

        if (old_code != -1) {
            if (next_code < 4096) {
                prefix[next_code] = (uint16_t)old_code;
                suffix[next_code] = first;
                next_code++;
                if (next_code == code_mask + 1 && code_size < 12) {
                    code_size++;
                    code_mask = (1 << code_size) - 1;
                }
            }
        }

        old_code = code;
    }

    *outlen = outpos;
    return 1;
}

static int gif_deinterlace(uint8_t *out, const uint8_t *in, uint32_t w, uint32_t h) {
    static const int offsets[4] = {0, 4, 2, 1};
    static const int steps[4] = {8, 8, 4, 2};
    size_t src = 0;
    for (int pass = 0; pass < 4; pass++) {
        for (uint32_t y = (uint32_t)offsets[pass]; y < h; y += (uint32_t)steps[pass]) {
            size_t row = (size_t)y * (size_t)w;
            if (src + w > (size_t)w * (size_t)h) {
                return 0;
            }
            memcpy(out + row, in + src, w);
            src += w;
        }
    }
    return 1;
}

static void gif_fill_rect(uint8_t *canvas, uint32_t canvas_w,
                          uint32_t left, uint32_t top,
                          uint32_t w, uint32_t h,
                          uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    for (uint32_t y = 0; y < h; y++) {
        uint8_t *row = canvas + ((size_t)(top + y) * (size_t)canvas_w + (size_t)left) * 4u;
        for (uint32_t x = 0; x < w; x++) {
            row[x * 4 + 0] = r;
            row[x * 4 + 1] = g;
            row[x * 4 + 2] = b;
            row[x * 4 + 3] = a;
        }
    }
}

static int gif_blit_indices(uint8_t *canvas, uint32_t canvas_w,
                            uint32_t left, uint32_t top,
                            uint32_t w, uint32_t h,
                            const uint8_t *indices,
                            const uint8_t *palette, int palette_entries,
                            int transparent, uint8_t transparent_index) {
    for (uint32_t y = 0; y < h; y++) {
        uint8_t *row = canvas + ((size_t)(top + y) * (size_t)canvas_w + (size_t)left) * 4u;
        const uint8_t *src = indices + (size_t)y * (size_t)w;
        for (uint32_t x = 0; x < w; x++) {
            uint8_t idx = src[x];
            if (transparent && idx == transparent_index) {
                continue;
            }
            if (idx >= palette_entries) {
                return 0;
            }
            row[x * 4 + 0] = palette[(int)idx * 3 + 0];
            row[x * 4 + 1] = palette[(int)idx * 3 + 1];
            row[x * 4 + 2] = palette[(int)idx * 3 + 2];
            row[x * 4 + 3] = 255;
        }
    }
    return 1;
}

static void gif_apply_disposal(uint8_t *canvas, const uint8_t *restore,
                               uint32_t canvas_w,
                               uint32_t left, uint32_t top,
                               uint32_t w, uint32_t h,
                               int disposal,
                               uint8_t bg_r, uint8_t bg_g, uint8_t bg_b, uint8_t bg_a) {
    /* Method 0: no disposal specified (leave pixels) */
    /* Method 1: do not dispose (leave pixels) */
    /* Method 2: restore to background color */
    /* Method 3: restore to previous */
    /* Methods 4-7: undefined, treat as do not dispose */
    if (disposal == 2) {
        gif_fill_rect(canvas, canvas_w, left, top, w, h, bg_r, bg_g, bg_b, bg_a);
    } else if (disposal == 3 && restore) {
        for (uint32_t y = 0; y < h; y++) {
            size_t row_off = ((size_t)(top + y) * (size_t)canvas_w + (size_t)left) * 4u;
            memcpy(canvas + row_off, restore + row_off, (size_t)w * 4u);
        }
    }
}

int cupidimage_load_gif(const unsigned char *data, size_t size, cupidimage_image *out,
                        char *err, size_t errcap) {
    if (!data || !out) {
        set_err(err, errcap, "invalid arguments");
        return 0;
    }

    memset(out, 0, sizeof(*out));

    if (size < 13 || (memcmp(data, "GIF87a", 6) != 0 && memcmp(data, "GIF89a", 6) != 0)) {
        set_err(err, errcap, "not a GIF");
        return 0;
    }

    uint32_t width = read_le16(data + 6);
    uint32_t height = read_le16(data + 8);
    if (width == 0 || height == 0) {
        set_err(err, errcap, "invalid GIF size");
        return 0;
    }

    uint8_t packed = data[10];
    int gct_flag = (packed & 0x80) != 0;
    int gct_size = 1 << ((packed & 0x07) + 1);
    uint8_t bg_index = data[11];
    int color_resolution = (packed >> 4) & 0x07;
    uint8_t pixel_aspect_ratio = data[12];
    (void)color_resolution;
    (void)pixel_aspect_ratio;

    size_t off = 13;
    uint8_t global_palette[256 * 3];
    int global_entries = 0;
    if (gct_flag) {
        if (!gif_read_color_table(data, size, &off, gct_size, global_palette, err, errcap)) {
            return 0;
        }
        global_entries = gct_size;
    }

    size_t pixel_count = (size_t)width * (size_t)height;
    if (pixel_count / (size_t)width != (size_t)height) {
        set_err(err, errcap, "image too large");
        return 0;
    }
    size_t rgba_size = pixel_count * 4u;
    if (rgba_size / 4u != pixel_count) {
        set_err(err, errcap, "image too large");
        return 0;
    }

    uint8_t bg_r = 255, bg_g = 255, bg_b = 255, bg_a = 0;
    if (global_entries > 0 && bg_index < (uint8_t)global_entries) {
        bg_r = global_palette[(int)bg_index * 3 + 0];
        bg_g = global_palette[(int)bg_index * 3 + 1];
        bg_b = global_palette[(int)bg_index * 3 + 2];
        bg_a = 255;
    }

    uint8_t *canvas = (uint8_t *)malloc(rgba_size);
    if (!canvas) {
        set_err(err, errcap, "out of memory");
        return 0;
    }
    gif_fill_rect(canvas, width, 0, 0, width, height, bg_r, bg_g, bg_b, bg_a);

    int gce_transparent = 0;
    uint8_t gce_trans_index = 0;

    int found_frame = 0;
    while (off < size) {
        uint8_t introducer = data[off++];
        if (introducer == 0x3B) {
            break;
        }
        if (introducer == 0x21) {
            if (off >= size) {
                free(canvas);
                set_err(err, errcap, "truncated GIF");
                return 0;
            }
            uint8_t label = data[off++];
            if (label == 0xF9) {
                if (off + 6 > size) {
                    free(canvas);
                    set_err(err, errcap, "truncated GIF");
                    return 0;
                }
                uint8_t block_size = data[off++];
                if (block_size != 4 || off + 5 > size) {
                    free(canvas);
                    set_err(err, errcap, "invalid GIF GCE");
                    return 0;
                }
                uint8_t packed_gce = data[off++];
                uint16_t delay = read_le16(data + off);
                off += 2;
                uint8_t trans_index = data[off++];
                uint8_t terminator = data[off++];
                if (terminator != 0) {
                    free(canvas);
                    set_err(err, errcap, "invalid GIF GCE");
                    return 0;
                }
                gce_transparent = packed_gce & 0x01;
                gce_trans_index = trans_index;
                (void)delay;
            } else if (label == 0xFF) {
                if (off >= size) {
                    free(canvas);
                    set_err(err, errcap, "truncated GIF");
                    return 0;
                }
                uint8_t block_size = data[off++];
                if (off + block_size > size) {
                    free(canvas);
                    set_err(err, errcap, "truncated GIF");
                    return 0;
                }
                off += block_size;
                if (!gif_skip_subblocks(data, size, &off, err, errcap)) {
                    free(canvas);
                    return 0;
                }
            } else {
                if (!gif_skip_subblocks(data, size, &off, err, errcap)) {
                    free(canvas);
                    return 0;
                }
            }
            continue;
        }
        if (introducer == 0x2C) {
            if (off + 9 > size) {
                free(canvas);
                set_err(err, errcap, "truncated GIF");
                return 0;
            }
            uint16_t left = read_le16(data + off); off += 2;
            uint16_t top = read_le16(data + off); off += 2;
            uint16_t w = read_le16(data + off); off += 2;
            uint16_t h = read_le16(data + off); off += 2;
            uint8_t img_packed = data[off++];
            int lct_flag = (img_packed & 0x80) != 0;
            int interlaced = (img_packed & 0x40) != 0;
            int lct_size = 1 << ((img_packed & 0x07) + 1);

            if (left + w > width || top + h > height) {
                free(canvas);
                set_err(err, errcap, "invalid GIF frame");
                return 0;
            }
            const uint8_t *palette = global_palette;
            int palette_entries = global_entries;
            uint8_t local_palette[256 * 3];
            if (lct_flag) {
                if (!gif_read_color_table(data, size, &off, lct_size, local_palette, err, errcap)) {
                    free(canvas);
                    return 0;
                }
                palette = local_palette;
                palette_entries = lct_size;
            }
            if (palette_entries == 0) {
                free(canvas);
                set_err(err, errcap, "missing GIF color table");
                return 0;
            }

            if (off >= size) {
                free(canvas);
                set_err(err, errcap, "truncated GIF");
                return 0;
            }
            int lzw_min_code = data[off++];
            uint8_t *lz_data = NULL;
            size_t lz_size = 0;
            if (!gif_read_subblocks(data, size, &off, &lz_data, &lz_size, err, errcap)) {
                free(canvas);
                return 0;
            }

            size_t frame_pixels = (size_t)w * (size_t)h;
            if (frame_pixels / (size_t)w != (size_t)h) {
                free(canvas);
                free(lz_data);
                set_err(err, errcap, "image too large");
                return 0;
            }
            uint8_t *indices = (uint8_t *)malloc(frame_pixels);
            if (!indices) {
                free(canvas);
                free(lz_data);
                set_err(err, errcap, "out of memory");
                return 0;
            }
            size_t decoded = 0;
            if (!gif_lzw_decode(lz_data, lz_size, lzw_min_code, indices, frame_pixels, &decoded) ||
                decoded < frame_pixels) {
                free(canvas);
                free(lz_data);
                free(indices);
                set_err(err, errcap, "GIF LZW decode failed");
                return 0;
            }
            free(lz_data);

            uint8_t *ordered = indices;
            if (interlaced) {
                uint8_t *tmp = (uint8_t *)malloc(frame_pixels);
                if (!tmp) {
                    free(canvas);
                    free(indices);
                    set_err(err, errcap, "out of memory");
                    return 0;
                }
                if (!gif_deinterlace(tmp, indices, w, h)) {
                    free(canvas);
                    free(indices);
                    free(tmp);
                    set_err(err, errcap, "GIF interlace failed");
                    return 0;
                }
                free(indices);
                ordered = tmp;
            }


            if (!gif_blit_indices(canvas, width, left, top, w, h,
                                  ordered, palette, palette_entries,
                                  gce_transparent, gce_trans_index)) {
                free(canvas);
                free(ordered);
                set_err(err, errcap, "palette index out of range");
                return 0;
            }
            free(ordered);

            uint8_t *rgba = (uint8_t *)malloc(rgba_size);
            if (!rgba) {
                free(canvas);
                set_err(err, errcap, "out of memory");
                return 0;
            }
            memcpy(rgba, canvas, rgba_size);
            out->width = width;
            out->height = height;
            out->rgba = rgba;
            found_frame = 1;
            break;
        }

        free(canvas);
        set_err(err, errcap, "invalid GIF block");
        return 0;
    }

    free(canvas);
    if (!found_frame) {
        set_err(err, errcap, "missing GIF frame");
        return 0;
    }
    return 1;
}

int cupidimage_load_gif_file(const char *path, cupidimage_image *out,
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

    int ok = cupidimage_load_gif(buf, (size_t)fsize, out, err, errcap);
    free(buf);
    return ok;
}

void cupidimage_free_animation(cupidimage_animation *anim) {
    if (!anim) {
        return;
    }
    if (anim->frames) {
        for (uint32_t i = 0; i < anim->frame_count; i++) {
            free(anim->frames[i].rgba);
        }
    }
    free(anim->frames);
    free(anim->delays);
    free(anim->user_input_flags);
    anim->frames = NULL;
    anim->delays = NULL;
    anim->user_input_flags = NULL;
    anim->frame_count = 0;
    anim->width = 0;
    anim->height = 0;
    anim->loop_count = 0;
    anim->pixel_aspect_ratio = 0;
    anim->color_resolution = 0;
}

int cupidimage_load_gif_animation(const unsigned char *data, size_t size,
                                  cupidimage_animation *out,
                                  char *err, size_t errcap) {
    if (!data || !out) {
        set_err(err, errcap, "invalid arguments");
        return 0;
    }

    memset(out, 0, sizeof(*out));

    if (size < 13 || (memcmp(data, "GIF87a", 6) != 0 && memcmp(data, "GIF89a", 6) != 0)) {
        set_err(err, errcap, "not a GIF");
        return 0;
    }

    uint32_t width = read_le16(data + 6);
    uint32_t height = read_le16(data + 8);
    if (width == 0 || height == 0) {
        set_err(err, errcap, "invalid GIF size");
        return 0;
    }

    uint8_t packed = data[10];
    int gct_flag = (packed & 0x80) != 0;
    int gct_size = 1 << ((packed & 0x07) + 1);
    uint8_t bg_index = data[11];
    int color_resolution = (packed >> 4) & 0x07;
    uint8_t pixel_aspect_ratio = data[12];

    size_t off = 13;
    uint8_t global_palette[256 * 3];
    int global_entries = 0;
    if (gct_flag) {
        if (!gif_read_color_table(data, size, &off, gct_size, global_palette, err, errcap)) {
            return 0;
        }
        global_entries = gct_size;
    }

    size_t pixel_count = (size_t)width * (size_t)height;
    if (pixel_count / (size_t)width != (size_t)height) {
        set_err(err, errcap, "image too large");
        return 0;
    }
    size_t rgba_size = pixel_count * 4u;
    if (rgba_size / 4u != pixel_count) {
        set_err(err, errcap, "image too large");
        return 0;
    }

    uint8_t bg_r = 255, bg_g = 255, bg_b = 255, bg_a = 0;
    if (global_entries > 0 && bg_index < (uint8_t)global_entries) {
        bg_r = global_palette[(int)bg_index * 3 + 0];
        bg_g = global_palette[(int)bg_index * 3 + 1];
        bg_b = global_palette[(int)bg_index * 3 + 2];
        bg_a = 255;
    }

    uint8_t *canvas = (uint8_t *)malloc(rgba_size);
    uint8_t *restore = (uint8_t *)malloc(rgba_size);
    if (!canvas || !restore) {
        free(canvas);
        free(restore);
        set_err(err, errcap, "out of memory");
        return 0;
    }
    gif_fill_rect(canvas, width, 0, 0, width, height, bg_r, bg_g, bg_b, bg_a);

    int gce_disposal = 0;
    int gce_transparent = 0;
    uint8_t gce_trans_index = 0;
    uint16_t gce_delay = 0;
    int gce_user_input = 0;

    int prev_disposal = 0;
    uint32_t prev_left = 0, prev_top = 0, prev_w = 0, prev_h = 0;
    int have_prev = 0;

    uint32_t loop_count = 1;

    uint32_t frame_cap = 0;
    uint32_t frame_count = 0;
    cupidimage_image *frames = NULL;
    uint32_t *delays = NULL;
    uint8_t *user_input_flags = NULL;

    while (off < size) {
        uint8_t introducer = data[off++];
        if (introducer == 0x3B) {
            break;
        }
        if (introducer == 0x21) {
            if (off >= size) {
                set_err(err, errcap, "truncated GIF");
                goto fail;
            }
            uint8_t label = data[off++];
            if (label == 0xF9) {
                if (off + 6 > size) {
                    set_err(err, errcap, "truncated GIF");
                    goto fail;
                }
                uint8_t block_size = data[off++];
                if (block_size != 4 || off + 5 > size) {
                    set_err(err, errcap, "invalid GIF GCE");
                    goto fail;
                }
                uint8_t packed_gce = data[off++];
                gce_delay = read_le16(data + off);
                off += 2;
                gce_trans_index = data[off++];
                uint8_t terminator = data[off++];
                if (terminator != 0) {
                    set_err(err, errcap, "invalid GIF GCE");
                    goto fail;
                }
                gce_disposal = (packed_gce >> 2) & 0x07;
                gce_user_input = (packed_gce >> 1) & 0x01;
                gce_transparent = packed_gce & 0x01;
            } else if (label == 0xFF) {
                if (off >= size) {
                    set_err(err, errcap, "truncated GIF");
                    goto fail;
                }
                uint8_t block_size = data[off++];
                if (off + block_size > size) {
                    set_err(err, errcap, "truncated GIF");
                    goto fail;
                }
                const uint8_t *app = data + off;
                off += block_size;
                uint8_t *app_data = NULL;
                size_t app_size = 0;
                if (!gif_read_subblocks(data, size, &off, &app_data, &app_size, err, errcap)) {
                    goto fail;
                }
                if (block_size == 11 &&
                    (memcmp(app, "NETSCAPE2.0", 11) == 0 || memcmp(app, "ANIMEXTS1.0", 11) == 0)) {
                    if (app_size >= 3 && app_data[0] == 1) {
                        loop_count = read_le16(app_data + 1);
                    }
                }
                free(app_data);
            } else {
                if (!gif_skip_subblocks(data, size, &off, err, errcap)) {
                    goto fail;
                }
            }
            continue;
        }

        if (introducer != 0x2C) {
            set_err(err, errcap, "invalid GIF block");
            goto fail;
        }

        if (off + 9 > size) {
            set_err(err, errcap, "truncated GIF");
            goto fail;
        }
        uint16_t left = read_le16(data + off); off += 2;
        uint16_t top = read_le16(data + off); off += 2;
        uint16_t w = read_le16(data + off); off += 2;
        uint16_t h = read_le16(data + off); off += 2;
        uint8_t img_packed = data[off++];
        int lct_flag = (img_packed & 0x80) != 0;
        int interlaced = (img_packed & 0x40) != 0;
        int lct_size = 1 << ((img_packed & 0x07) + 1);

        if (left + w > width || top + h > height) {
            set_err(err, errcap, "invalid GIF frame");
            goto fail;
        }

        const uint8_t *palette = global_palette;
        int palette_entries = global_entries;
        uint8_t local_palette[256 * 3];
        if (lct_flag) {
            if (!gif_read_color_table(data, size, &off, lct_size, local_palette, err, errcap)) {
                goto fail;
            }
            palette = local_palette;
            palette_entries = lct_size;
        }
        if (palette_entries == 0) {
            set_err(err, errcap, "missing GIF color table");
            goto fail;
        }

        if (off >= size) {
            set_err(err, errcap, "truncated GIF");
            goto fail;
        }
        int lzw_min_code = data[off++];
        uint8_t *lz_data = NULL;
        size_t lz_size = 0;
        if (!gif_read_subblocks(data, size, &off, &lz_data, &lz_size, err, errcap)) {
            goto fail;
        }

        size_t frame_pixels = (size_t)w * (size_t)h;
        if (frame_pixels / (size_t)w != (size_t)h) {
            free(lz_data);
            set_err(err, errcap, "image too large");
            goto fail;
        }
        uint8_t *indices = (uint8_t *)malloc(frame_pixels);
        if (!indices) {
            free(lz_data);
            set_err(err, errcap, "out of memory");
            goto fail;
        }
        size_t decoded = 0;
        if (!gif_lzw_decode(lz_data, lz_size, lzw_min_code, indices, frame_pixels, &decoded) ||
            decoded < frame_pixels) {
            free(lz_data);
            free(indices);
            set_err(err, errcap, "GIF LZW decode failed");
            goto fail;
        }
        free(lz_data);

        uint8_t *ordered = indices;
        if (interlaced) {
            uint8_t *tmp = (uint8_t *)malloc(frame_pixels);
            if (!tmp) {
                free(indices);
                set_err(err, errcap, "out of memory");
                goto fail;
            }
            if (!gif_deinterlace(tmp, indices, w, h)) {
                free(indices);
                free(tmp);
                set_err(err, errcap, "GIF interlace failed");
                goto fail;
            }
            free(indices);
            ordered = tmp;
        }



        if (have_prev) {
            gif_apply_disposal(canvas, restore, width, prev_left, prev_top, prev_w, prev_h,
                               prev_disposal, bg_r, bg_g, bg_b, bg_a);
        }
        if (gce_disposal == 3) {
            memcpy(restore, canvas, rgba_size);
        }

        if (!gif_blit_indices(canvas, width, left, top, w, h,
                              ordered, palette, palette_entries,
                              gce_transparent, gce_trans_index)) {
            free(ordered);
            set_err(err, errcap, "palette index out of range");
            goto fail;
        }
        free(ordered);

        if (frame_count == frame_cap) {
            uint32_t new_cap = frame_cap ? frame_cap * 2u : 4u;
            cupidimage_image *new_frames = (cupidimage_image *)realloc(frames, new_cap * sizeof(*frames));
            uint32_t *new_delays = (uint32_t *)realloc(delays, new_cap * sizeof(*delays));
            uint8_t *new_flags = (uint8_t *)realloc(user_input_flags, new_cap * sizeof(*new_flags));
            if (!new_frames || !new_delays || !new_flags) {
                free(new_frames);
                free(new_delays);
                free(new_flags);
                set_err(err, errcap, "out of memory");
                goto fail;
            }
            frames = new_frames;
            delays = new_delays;
            user_input_flags = new_flags;
            frame_cap = new_cap;
        }

        uint8_t *frame_rgba = (uint8_t *)malloc(rgba_size);
        if (!frame_rgba) {
            set_err(err, errcap, "out of memory");
            goto fail;
        }
        memcpy(frame_rgba, canvas, rgba_size);
        frames[frame_count].width = width;
        frames[frame_count].height = height;
        frames[frame_count].rgba = frame_rgba;
        uint32_t delay_ms = (uint32_t)gce_delay * 10u;
        delays[frame_count] = delay_ms;
        user_input_flags[frame_count] = (uint8_t)gce_user_input;
        frame_count++;

        prev_disposal = gce_disposal;
        prev_left = left;
        prev_top = top;
        prev_w = w;
        prev_h = h;
        have_prev = 1;

        gce_disposal = 0;
        gce_user_input = 0;
        gce_transparent = 0;
        gce_trans_index = 0;
        gce_delay = 0;
    }

    if (frame_count == 0) {
        set_err(err, errcap, "missing GIF frame");
        goto fail;
    }

    free(canvas);
    free(restore);
    out->width = width;
    out->height = height;
    out->frame_count = frame_count;
    out->loop_count = loop_count;
    out->frames = frames;
    out->delays = delays;
    out->pixel_aspect_ratio = pixel_aspect_ratio;
    out->color_resolution = (uint8_t)color_resolution;
    out->user_input_flags = user_input_flags;
    return 1;

fail:
    free(canvas);
    free(restore);
    if (frames) {
        for (uint32_t i = 0; i < frame_count; i++) {
            free(frames[i].rgba);
        }
    }
    free(frames);
    free(delays);
    free(user_input_flags);
    return 0;
}

int cupidimage_load_gif_animation_file(const char *path,
                                       cupidimage_animation *out,
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

    int ok = cupidimage_load_gif_animation(buf, (size_t)fsize, out, err, errcap);
    free(buf);
    return ok;
}
