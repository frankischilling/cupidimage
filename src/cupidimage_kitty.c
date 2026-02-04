#include "cupidimage_kitty.h"
#include "cupidimage.h"
#include "cupidimage_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char base64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int kitty_base64_encode(const uint8_t *data, size_t size, char *out, size_t outcap) {
    if (size == 0 || data == NULL) {
        if (outcap > 0) {
            out[0] = '\0';
        }
        return 0;
    }

    size_t outlen = ((size + 2) / 3) * 4;
    if (outcap < outlen + 1) {
        return -1;  /* buffer too small */
    }

    size_t i = 0;
    size_t j = 0;

    while (i + 2 < size) {
        uint32_t n = ((uint32_t)data[i] << 16) | ((uint32_t)data[i+1] << 8) | data[i+2];
        out[j++] = base64_table[(n >> 18) & 0x3F];
        out[j++] = base64_table[(n >> 12) & 0x3F];
        out[j++] = base64_table[(n >> 6) & 0x3F];
        out[j++] = base64_table[n & 0x3F];
        i += 3;
    }

    if (i < size) {
        uint32_t n = (uint32_t)data[i] << 16;
        if (i + 1 < size) {
            n |= (uint32_t)data[i+1] << 8;
        }

        out[j++] = base64_table[(n >> 18) & 0x3F];
        out[j++] = base64_table[(n >> 12) & 0x3F];
        out[j++] = (i + 1 < size) ? base64_table[(n >> 6) & 0x3F] : '=';
        out[j++] = '=';
    }

    out[j] = '\0';
    return (int)j;
}

int cupidimage_is_kitty_terminal(void) {
    const char *kitty_window = getenv("KITTY_WINDOW_ID");
    if (kitty_window != NULL && kitty_window[0] != '\0') {
        return 1;
    }

    const char *term = getenv("TERM");
    if (term != NULL && strstr(term, "kitty") != NULL) {
        return 1;
    }

    return 0;
}

static void set_err(char *err, size_t errcap, const char *msg) {
    if (err && errcap > 0) {
        snprintf(err, errcap, "%s", msg);
    }
}

int cupidimage_kitty_delete_all(FILE *out, char *err, size_t errcap) {
    if (!out) {
        set_err(err, errcap, "null output file");
        return 0;
    }

    /* Delete all: a=d (delete action) */
    fprintf(out, "\033_Ga=d\033\\");
    return 1;
}

int cupidimage_kitty_delete_image(FILE *out, uint32_t image_id,
                                 char *err, size_t errcap) {
    if (!out) {
        set_err(err, errcap, "null output file");
        return 0;
    }

    /* Delete image: a=d,d=i (delete by image ID) */
    fprintf(out, "\033_Ga=d,d=i,i=%u\033\\", image_id);
    return 1;
}

int cupidimage_kitty_delete_placement(FILE *out, uint32_t image_id,
                                     uint32_t placement_id,
                                     char *err, size_t errcap) {
    if (!out) {
        set_err(err, errcap, "null output file");
        return 0;
    }

    /* Delete placement: a=d,d=p (delete by placement ID) */
    fprintf(out, "\033_Ga=d,d=p,i=%u,p=%u\033\\", image_id, placement_id);
    return 1;
}

static uint32_t next_image_id = 1;
static uint32_t next_placement_id = 1;

int cupidimage_render_kitty_with_options(const cupidimage_image *img, FILE *out,
                                        uint32_t term_width, uint32_t term_height,
                                        const cupidimage_kitty_options *opts,
                                        char *err, size_t errcap) {
    if (!img || !img->rgba || !out) {
        set_err(err, errcap, "invalid arguments");
        return 0;
    }

    uint32_t img_id = opts->image_id ? opts->image_id : next_image_id++;
    uint32_t place_id = opts->placement_id ? opts->placement_id : next_placement_id++;

    if (opts->delete_previous) {
        cupidimage_kitty_delete_all(out, NULL, 0);
    }

    /* Calculate display dimensions in cells */
    uint32_t display_cols = 0;
    uint32_t display_rows = 0;
    
    /* Use provided dimensions directly - let user control the size */
    if (term_width > 0) {
        display_cols = term_width;
    }
    if (term_height > 0) {
        display_rows = term_height;
    }

    /* Prepare RGBA data */
    size_t data_size = (size_t)img->width * img->height * 4;
    uint8_t *compressed = NULL;
    size_t compressed_size = 0;
    const uint8_t *payload = img->rgba;
    size_t payload_size = data_size;
    int use_compression = opts->compression;

    if (use_compression) {
        /* Allocate compression buffer - worst case expansion is ~12.5% for
         * incompressible data with static Huffman (9 bits per byte worst case) */
        size_t max_compressed = data_size + (data_size / 8) + 64;
        compressed = malloc(max_compressed);
        if (!compressed) {
            set_err(err, errcap, "out of memory");
            return 0;
        }

        if (!cupidimage_deflate_compress(img->rgba, data_size, compressed,
                                        max_compressed, &compressed_size)) {
            free(compressed);
            set_err(err, errcap, "compression failed");
            return 0;
        }

        /* If compression made data larger, don't use it */
        if (compressed_size >= data_size) {
            free(compressed);
            compressed = NULL;
            use_compression = 0;
        } else {
            payload = compressed;
            payload_size = compressed_size;
        }
    }

    /* Base64 encode */
    size_t b64_size = ((payload_size + 2) / 3) * 4 + 1;
    char *b64 = malloc(b64_size);
    if (!b64) {
        free(compressed);
        set_err(err, errcap, "out of memory");
        return 0;
    }

    int encoded = kitty_base64_encode(payload, payload_size, b64, b64_size);
    if (encoded < 0) {
        free(compressed);
        free(b64);
        set_err(err, errcap, "base64 encoding failed");
        return 0;
    }

    /* Send in chunks (Kitty has 4096 byte limit per escape sequence payload)
     * The limit applies to control_data + semicolon + base64_data combined.
     * Per spec: "All chunks, except the last, must have a size that is a multiple of 4" */
    const size_t first_chunk_size = 3800;  /* Conservative: leave ~250 bytes for header */
    const size_t chunk_size = 4088;        /* Continuation header is tiny: "m=X;" uses ~8 bytes */
    size_t offset = 0;

    while (offset < (size_t)encoded) {
        size_t remaining = (size_t)encoded - offset;
        size_t max_chunk = (offset == 0) ? first_chunk_size : chunk_size;
        size_t this_chunk = remaining > max_chunk ? max_chunk : remaining;
        int is_last = (offset + this_chunk >= (size_t)encoded);
        
        /* Non-final chunks must be a multiple of 4 bytes */
        if (!is_last && (this_chunk % 4) != 0) {
            this_chunk = this_chunk & ~(size_t)3;  /* Round down to multiple of 4 */
            is_last = 0;  /* Re-check after adjustment */
        }

        if (offset == 0) {
            /* First chunk - include all metadata */
            if (use_compression) {
                if (display_cols > 0 && display_rows > 0) {
                    fprintf(out, "\033_Gf=32,o=z,s=%u,v=%u,c=%u,r=%u,i=%u,p=%u,a=T,q=1,m=%d;",
                            img->width, img->height, display_cols, display_rows,
                            img_id, place_id, is_last ? 0 : 1);
                } else if (display_cols > 0) {
                    fprintf(out, "\033_Gf=32,o=z,s=%u,v=%u,c=%u,i=%u,p=%u,a=T,q=1,m=%d;",
                            img->width, img->height, display_cols,
                            img_id, place_id, is_last ? 0 : 1);
                } else if (display_rows > 0) {
                    fprintf(out, "\033_Gf=32,o=z,s=%u,v=%u,r=%u,i=%u,p=%u,a=T,q=1,m=%d;",
                            img->width, img->height, display_rows,
                            img_id, place_id, is_last ? 0 : 1);
                } else {
                    fprintf(out, "\033_Gf=32,o=z,s=%u,v=%u,i=%u,p=%u,a=T,q=1,m=%d;",
                            img->width, img->height, img_id, place_id, is_last ? 0 : 1);
                }
            } else {
                if (display_cols > 0 && display_rows > 0) {
                    fprintf(out, "\033_Gf=32,s=%u,v=%u,c=%u,r=%u,i=%u,p=%u,a=T,q=1,m=%d;",
                            img->width, img->height, display_cols, display_rows,
                            img_id, place_id, is_last ? 0 : 1);
                } else if (display_cols > 0) {
                    fprintf(out, "\033_Gf=32,s=%u,v=%u,c=%u,i=%u,p=%u,a=T,q=1,m=%d;",
                            img->width, img->height, display_cols,
                            img_id, place_id, is_last ? 0 : 1);
                } else if (display_rows > 0) {
                    fprintf(out, "\033_Gf=32,s=%u,v=%u,r=%u,i=%u,p=%u,a=T,q=1,m=%d;",
                            img->width, img->height, display_rows,
                            img_id, place_id, is_last ? 0 : 1);
                } else {
                    fprintf(out, "\033_Gf=32,s=%u,v=%u,i=%u,p=%u,a=T,q=1,m=%d;",
                            img->width, img->height, img_id, place_id, is_last ? 0 : 1);
                }
            }
        } else {
            /* Continuation chunk */
            fprintf(out, "\033_Gm=%d;", is_last ? 0 : 1);
        }
        
        /* Write the base64 data directly */
        fwrite(b64 + offset, 1, this_chunk, out);
        fprintf(out, "\033\\");
        fflush(out);

        offset += this_chunk;
    }

    free(compressed);
    free(b64);
    return 1;
}

int cupidimage_render_kitty(const cupidimage_image *img, FILE *out,
                           uint32_t term_width, uint32_t term_height,
                           char *err, size_t errcap) {
    cupidimage_kitty_options opts = {0};
    opts.compression = 1;  /* enable by default */
    return cupidimage_render_kitty_with_options(img, out, term_width, term_height,
                                               &opts, err, errcap);
}

int cupidimage_render_kitty_animation_with_options(
    const cupidimage_animation *anim, FILE *out,
    uint32_t term_width, uint32_t term_height,
    const cupidimage_kitty_options *opts,
    char *err, size_t errcap) {

    if (!anim || !out || !anim->frames || anim->frame_count == 0) {
        set_err(err, errcap, "invalid arguments or no animation data");
        return 0;
    }

    (void)term_width;
    (void)term_height;

    uint32_t img_id = opts->image_id ? opts->image_id : next_image_id++;
    uint32_t place_id = opts->placement_id ? opts->placement_id : next_placement_id++;

    if (opts->delete_previous) {
        cupidimage_kitty_delete_all(out, NULL, 0);
    }

    /* First, transmit and display the base image (first frame) */
    cupidimage_kitty_options first_opts = *opts;
    first_opts.image_id = img_id;
    first_opts.placement_id = place_id;
    
    if (!cupidimage_render_kitty_with_options(&anim->frames[0], out,
                                              term_width, term_height,
                                              &first_opts, err, errcap)) {
        return 0;
    }

    /* If only one frame, we're done */
    if (anim->frame_count == 1) {
        return 1;
    }

    /* Transmit additional frames using frame action (a=f) */
    for (uint32_t i = 1; i < anim->frame_count; i++) {
        const cupidimage_image *frame = &anim->frames[i];
        
        /* Prepare frame data */
        size_t data_size = (size_t)frame->width * frame->height * 4;
        uint8_t *compressed = NULL;
        size_t compressed_size = 0;
        const uint8_t *payload = frame->rgba;
        size_t payload_size = data_size;
        int use_compression = opts->compression;

        if (use_compression) {
            size_t max_compressed = data_size + (data_size / 8) + 64;
            compressed = malloc(max_compressed);
            if (!compressed) {
                set_err(err, errcap, "out of memory");
                return 0;
            }

            if (!cupidimage_deflate_compress(frame->rgba, data_size, compressed,
                                            max_compressed, &compressed_size)) {
                free(compressed);
                set_err(err, errcap, "compression failed");
                return 0;
            }

            if (compressed_size >= data_size) {
                free(compressed);
                compressed = NULL;
                use_compression = 0;
            } else {
                payload = compressed;
                payload_size = compressed_size;
            }
        }

        /* Base64 encode */
        size_t b64_size = ((payload_size + 2) / 3) * 4 + 1;
        char *b64 = malloc(b64_size);
        if (!b64) {
            free(compressed);
            set_err(err, errcap, "out of memory");
            return 0;
        }

        int encoded = kitty_base64_encode(payload, payload_size, b64, b64_size);
        if (encoded < 0) {
            free(compressed);
            free(b64);
            set_err(err, errcap, "base64 encoding failed");
            return 0;
        }

        /* Send frame data in chunks with a=f */
        const size_t first_chunk_size = 3700;  /* Extra room for frame header */
        const size_t chunk_size = 4088;
        size_t offset = 0;
        uint32_t gap_ms = anim->delays ? anim->delays[i] : 40;

        while (offset < (size_t)encoded) {
            size_t remaining = (size_t)encoded - offset;
            size_t max_chunk = (offset == 0) ? first_chunk_size : chunk_size;
            size_t this_chunk = remaining > max_chunk ? max_chunk : remaining;
            int is_last = (offset + this_chunk >= (size_t)encoded);
            
            if (!is_last && (this_chunk % 4) != 0) {
                this_chunk = this_chunk & ~(size_t)3;
                is_last = 0;
            }

            if (offset == 0) {
                /* First chunk of frame - include frame metadata */
                if (use_compression) {
                    fprintf(out, "\033_Ga=f,i=%u,o=z,s=%u,v=%u,z=%u,q=1,m=%d;",
                            img_id, frame->width, frame->height, gap_ms, is_last ? 0 : 1);
                } else {
                    fprintf(out, "\033_Ga=f,i=%u,s=%u,v=%u,z=%u,q=1,m=%d;",
                            img_id, frame->width, frame->height, gap_ms, is_last ? 0 : 1);
                }
            } else {
                /* Continuation chunk - must include a=f */
                fprintf(out, "\033_Ga=f,m=%d;", is_last ? 0 : 1);
            }
            
            fwrite(b64 + offset, 1, this_chunk, out);
            fprintf(out, "\033\\");
            fflush(out);

            offset += this_chunk;
        }

        free(compressed);
        free(b64);
    }

    /* Set the gap for the first frame (root frame) */
    uint32_t first_gap = anim->delays ? anim->delays[0] : 40;
    fprintf(out, "\033_Ga=a,i=%u,r=1,z=%u,q=1\033\\", img_id, first_gap);

    /* Start the animation: s=3 (run normally), v=1 (loop infinitely) */
    fprintf(out, "\033_Ga=a,i=%u,s=3,v=1,q=1\033\\", img_id);
    fflush(out);

    return 1;
}

int cupidimage_render_kitty_animation(const cupidimage_animation *anim, FILE *out,
                                     uint32_t term_width, uint32_t term_height,
                                     char *err, size_t errcap) {
    cupidimage_kitty_options opts = {0};
    opts.compression = 1;
    return cupidimage_render_kitty_animation_with_options(anim, out,
                                                         term_width, term_height,
                                                         &opts, err, errcap);
}
