#define _POSIX_C_SOURCE 200809L

#include "cupidimage_sixel.h"
#include "cupidimage.h"
#include "cupidimage_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <time.h>
#endif

static void set_err(char *err, size_t errcap, const char *msg) {
    if (err && errcap > 0) {
        strncpy(err, msg, errcap - 1);
        err[errcap - 1] = '\0';
    }
}

/* Octree node for color quantization */
typedef struct octree_node {
    uint8_t is_leaf;
    uint32_t pixel_count;
    uint64_t red_sum;
    uint64_t green_sum;
    uint64_t blue_sum;
    struct octree_node *children[8];
    struct octree_node *next;
} octree_node;

typedef struct {
    octree_node *root;
    octree_node *reducible[8];
    uint32_t leaf_count;
    uint32_t max_colors;
} octree_quantizer;

static octree_node *octree_create_node(int level, int is_leaf) {
    (void)level;  /* Unused but kept for API consistency */
    octree_node *node = (octree_node *)calloc(1, sizeof(octree_node));
    if (!node) return NULL;
    node->is_leaf = (uint8_t)is_leaf;
    return node;
}

static void octree_free(octree_node *node) {
    if (!node) return;
    for (int i = 0; i < 8; i++) {
        octree_free(node->children[i]);
    }
    free(node);
}

static void octree_add_color(octree_quantizer *qt, octree_node **node_ptr, uint8_t r, uint8_t g, uint8_t b, int level) {
    if (!*node_ptr) {
        int is_leaf = (level == 8);
        *node_ptr = octree_create_node(level, is_leaf);
        if (!*node_ptr) return;
        
        if (!is_leaf && level < 8) {
            (*node_ptr)->next = qt->reducible[level];
            qt->reducible[level] = *node_ptr;
        }
        
        if (is_leaf) {
            qt->leaf_count++;
        }
    }
    
    octree_node *node = *node_ptr;
    
    if (node->is_leaf) {
        node->pixel_count++;
        node->red_sum += r;
        node->green_sum += g;
        node->blue_sum += b;
    } else {
        int shift = 7 - level;
        int idx = ((r >> shift) & 1) << 2 | ((g >> shift) & 1) << 1 | ((b >> shift) & 1);
        octree_add_color(qt, &node->children[idx], r, g, b, level + 1);
    }
}

static void octree_reduce(octree_quantizer *qt) {
    /* Find deepest level with reducible nodes */
    int level = 7;
    while (level >= 0 && !qt->reducible[level]) {
        level--;
    }
    
    if (level < 0) return;
    
    octree_node *node = qt->reducible[level];
    qt->reducible[level] = node->next;
    
    uint32_t pixel_count = 0;
    uint64_t red_sum = 0;
    uint64_t green_sum = 0;
    uint64_t blue_sum = 0;
    
    for (int i = 0; i < 8; i++) {
        if (node->children[i]) {
            pixel_count += node->children[i]->pixel_count;
            red_sum += node->children[i]->red_sum;
            green_sum += node->children[i]->green_sum;
            blue_sum += node->children[i]->blue_sum;
            
            if (node->children[i]->is_leaf) {
                qt->leaf_count--;
            }
            
            octree_free(node->children[i]);
            node->children[i] = NULL;
        }
    }
    
    node->is_leaf = 1;
    node->pixel_count = pixel_count;
    node->red_sum = red_sum;
    node->green_sum = green_sum;
    node->blue_sum = blue_sum;
    qt->leaf_count++;
}

static void octree_get_palette_recursive(octree_node *node, uint8_t *palette, int *index) {
    if (!node) return;
    
    if (node->is_leaf && node->pixel_count > 0) {
        int idx = *index;
        palette[idx * 3 + 0] = (uint8_t)(node->red_sum / node->pixel_count);
        palette[idx * 3 + 1] = (uint8_t)(node->green_sum / node->pixel_count);
        palette[idx * 3 + 2] = (uint8_t)(node->blue_sum / node->pixel_count);
        node->pixel_count = (uint32_t)((*index) + 1);  /* Store palette index + 1 */
        (*index)++;
    } else {
        for (int i = 0; i < 8; i++) {
            octree_get_palette_recursive(node->children[i], palette, index);
        }
    }
}

static int quantize_colors(const uint8_t *rgba, uint32_t width, uint32_t height,
                           uint8_t *palette, uint32_t max_colors, uint32_t *palette_size,
                           int use_transparency, uint8_t *indexed) {
    octree_quantizer qt;
    memset(&qt, 0, sizeof(qt));
    qt.max_colors = max_colors;
    
    /* Reserve color 0 for transparency if needed */
    int palette_offset = use_transparency ? 1 : 0;
    uint32_t effective_max = max_colors - (uint32_t)palette_offset;
    
    /* Build octree */
    for (uint32_t i = 0; i < width * height; i++) {
        uint8_t a = rgba[i * 4 + 3];
        
        if (use_transparency && a < 128) {
            continue;  /* Skip transparent pixels */
        }
        
        octree_add_color(&qt, &qt.root, rgba[i * 4 + 0], rgba[i * 4 + 1], rgba[i * 4 + 2], 0);
        
        while (qt.leaf_count > effective_max) {
            octree_reduce(&qt);
        }
    }
    
    /* Extract palette */
    int idx = palette_offset;
    octree_get_palette_recursive(qt.root, palette + palette_offset * 3, &idx);
    *palette_size = (uint32_t)idx;
    
    if (use_transparency) {
        palette[0] = 0;
        palette[1] = 0;
        palette[2] = 0;
    }
    
    octree_free(qt.root);
    
    /* Map pixels to palette indices using nearest-color search */
    for (uint32_t i = 0; i < width * height; i++) {
        uint8_t a = rgba[i * 4 + 3];
        
        if (use_transparency && a < 128) {
            indexed[i] = 0;
        } else {
            uint8_t r = rgba[i * 4 + 0];
            uint8_t g = rgba[i * 4 + 1];
            uint8_t b = rgba[i * 4 + 2];
            
            /* Find nearest color in palette */
            int best_dist = INT_MAX;
            uint8_t best_idx = 0;
            
            for (uint32_t p = 0; p < *palette_size; p++) {
                int dr = (int)r - palette[p * 3 + 0];
                int dg = (int)g - palette[p * 3 + 1];
                int db = (int)b - palette[p * 3 + 2];
                int dist = dr * dr + dg * dg + db * db;
                
                if (dist < best_dist) {
                    best_dist = dist;
                    best_idx = (uint8_t)p;
                }
            }
            
            indexed[i] = best_idx;
        }
    }
    
    return 1;
}

static void apply_floyd_steinberg_dither(uint8_t *rgba, uint32_t width, uint32_t height,
                                         const uint8_t *palette, uint32_t palette_size,
                                         uint8_t *indexed) {
    int *error_r = (int *)calloc(width * height, sizeof(int));
    int *error_g = (int *)calloc(width * height, sizeof(int));
    int *error_b = (int *)calloc(width * height, sizeof(int));
    
    if (!error_r || !error_g || !error_b) {
        free(error_r);
        free(error_g);
        free(error_b);
        return;
    }
    
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint32_t i = y * width + x;
            
            int r = (int)rgba[i * 4 + 0] + error_r[i];
            int g = (int)rgba[i * 4 + 1] + error_g[i];
            int b = (int)rgba[i * 4 + 2] + error_b[i];
            
            r = r < 0 ? 0 : (r > 255 ? 255 : r);
            g = g < 0 ? 0 : (g > 255 ? 255 : g);
            b = b < 0 ? 0 : (b > 255 ? 255 : b);
            
            /* Find nearest color */
            uint8_t best_idx = indexed[i];
            int best_dist = INT_MAX;
            
            for (uint32_t p = 0; p < palette_size; p++) {
                int dr = r - palette[p * 3 + 0];
                int dg = g - palette[p * 3 + 1];
                int db = b - palette[p * 3 + 2];
                int dist = dr * dr + dg * dg + db * db;
                
                if (dist < best_dist) {
                    best_dist = dist;
                    best_idx = (uint8_t)p;
                }
            }
            
            indexed[i] = best_idx;
            
            int er = r - palette[best_idx * 3 + 0];
            int eg = g - palette[best_idx * 3 + 1];
            int eb = b - palette[best_idx * 3 + 2];
            
            /* Distribute error: Floyd-Steinberg */
            if (x + 1 < width) {
                uint32_t idx = i + 1;
                error_r[idx] += (er * 7) / 16;
                error_g[idx] += (eg * 7) / 16;
                error_b[idx] += (eb * 7) / 16;
            }
            
            if (y + 1 < height) {
                if (x > 0) {
                    uint32_t idx = i + width - 1;
                    error_r[idx] += (er * 3) / 16;
                    error_g[idx] += (eg * 3) / 16;
                    error_b[idx] += (eb * 3) / 16;
                }
                
                uint32_t idx = i + width;
                error_r[idx] += (er * 5) / 16;
                error_g[idx] += (eg * 5) / 16;
                error_b[idx] += (eb * 5) / 16;
                
                if (x + 1 < width) {
                    idx = i + width + 1;
                    error_r[idx] += (er * 1) / 16;
                    error_g[idx] += (eg * 1) / 16;
                    error_b[idx] += (eb * 1) / 16;
                }
            }
        }
    }
    
    free(error_r);
    free(error_g);
    free(error_b);
}

static void apply_atkinson_dither(uint8_t *rgba, uint32_t width, uint32_t height,
                                  const uint8_t *palette, uint32_t palette_size,
                                  uint8_t *indexed) {
    int *error_r = (int *)calloc(width * height, sizeof(int));
    int *error_g = (int *)calloc(width * height, sizeof(int));
    int *error_b = (int *)calloc(width * height, sizeof(int));
    
    if (!error_r || !error_g || !error_b) {
        free(error_r);
        free(error_g);
        free(error_b);
        return;
    }
    
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint32_t i = y * width + x;
            
            int r = (int)rgba[i * 4 + 0] + error_r[i];
            int g = (int)rgba[i * 4 + 1] + error_g[i];
            int b = (int)rgba[i * 4 + 2] + error_b[i];
            
            r = r < 0 ? 0 : (r > 255 ? 255 : r);
            g = g < 0 ? 0 : (g > 255 ? 255 : g);
            b = b < 0 ? 0 : (b > 255 ? 255 : b);
            
            /* Find nearest color */
            uint8_t best_idx = indexed[i];
            int best_dist = INT_MAX;
            
            for (uint32_t p = 0; p < palette_size; p++) {
                int dr = r - palette[p * 3 + 0];
                int dg = g - palette[p * 3 + 1];
                int db = b - palette[p * 3 + 2];
                int dist = dr * dr + dg * dg + db * db;
                
                if (dist < best_dist) {
                    best_dist = dist;
                    best_idx = (uint8_t)p;
                }
            }
            
            indexed[i] = best_idx;
            
            int er = r - palette[best_idx * 3 + 0];
            int eg = g - palette[best_idx * 3 + 1];
            int eb = b - palette[best_idx * 3 + 2];
            
            /* Distribute error: Atkinson (diffuse 6/8 of error) */
            int positions[6][2] = {{1,0}, {2,0}, {-1,1}, {0,1}, {1,1}, {0,2}};
            
            for (int p = 0; p < 6; p++) {
                int nx = (int)x + positions[p][0];
                int ny = (int)y + positions[p][1];
                
                if (nx >= 0 && nx < (int)width && ny >= 0 && ny < (int)height) {
                    uint32_t idx = (uint32_t)ny * width + (uint32_t)nx;
                    error_r[idx] += er / 8;
                    error_g[idx] += eg / 8;
                    error_b[idx] += eb / 8;
                }
            }
        }
    }
    
    free(error_r);
    free(error_g);
    free(error_b);
}

static void apply_ordered_dither(uint8_t *rgba, uint32_t width, uint32_t height,
                                 const uint8_t *palette, uint32_t palette_size,
                                 uint8_t *indexed) {
    /* 4x4 Bayer matrix */
    static const int bayer4x4[4][4] = {
        { 0,  8,  2, 10},
        {12,  4, 14,  6},
        { 3, 11,  1,  9},
        {15,  7, 13,  5}
    };
    
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint32_t i = y * width + x;
            
            int threshold = bayer4x4[y % 4][x % 4] * 16;
            
            int r = (int)rgba[i * 4 + 0] + threshold - 128;
            int g = (int)rgba[i * 4 + 1] + threshold - 128;
            int b = (int)rgba[i * 4 + 2] + threshold - 128;
            
            r = r < 0 ? 0 : (r > 255 ? 255 : r);
            g = g < 0 ? 0 : (g > 255 ? 255 : g);
            b = b < 0 ? 0 : (b > 255 ? 255 : b);
            
            /* Find nearest color */
            uint8_t best_idx = indexed[i];
            int best_dist = INT_MAX;
            
            for (uint32_t p = 0; p < palette_size; p++) {
                int dr = r - palette[p * 3 + 0];
                int dg = g - palette[p * 3 + 1];
                int db = b - palette[p * 3 + 2];
                int dist = dr * dr + dg * dg + db * db;
                
                if (dist < best_dist) {
                    best_dist = dist;
                    best_idx = (uint8_t)p;
                }
            }
            
            indexed[i] = best_idx;
        }
    }
}

static int encode_sixel(FILE *out, const uint8_t *indexed, uint32_t width, uint32_t height,
                        const uint8_t *palette, uint32_t palette_size,
                        float pixel_aspect_ratio) {
    (void)pixel_aspect_ratio;  /* Reserved for future use */
    
    /* Start Sixel sequence: DCS P1 ; P2 ; P3 q
     * P1 = pixel aspect ratio (0 = default 2:1)
     * P2 = background select (0 = leave at current, 1 = set to color 0, 2 = set to current)
     * P3 = horizontal grid size (0 = default)
     */
    fprintf(out, "\033Pq");
    
    /* Raster attributes: "Pan;Pad;Ph;Pv
     * Pan = aspect ratio numerator
     * Pad = aspect ratio denominator
     * Ph = horizontal extent in pixels
     * Pv = vertical extent in pixels
     */
    fprintf(out, "\"1;1;%u;%u", width, height);
    
    /* Define palette */
    for (uint32_t i = 0; i < palette_size; i++) {
        int r = (palette[i * 3 + 0] * 100) / 255;
        int g = (palette[i * 3 + 1] * 100) / 255;
        int b = (palette[i * 3 + 2] * 100) / 255;
        fprintf(out, "#%u;2;%d;%d;%d", i, r, g, b);
    }
    
    /* Encode image data in 6-pixel-tall bands */
    uint32_t num_bands = (height + 5) / 6;
    
    for (uint32_t band = 0; band < num_bands; band++) {
        uint32_t band_y = band * 6;
        int first_color_in_band = 1;
        
        for (uint32_t color = 0; color < palette_size; color++) {
            /* Check if this color is used in this band */
            int color_used = 0;
            for (uint32_t x = 0; x < width && !color_used; x++) {
                for (int bit = 0; bit < 6; bit++) {
                    uint32_t y = band_y + (uint32_t)bit;
                    if (y < height) {
                        uint32_t idx = y * width + x;
                        if (indexed[idx] == (uint8_t)color) {
                            color_used = 1;
                            break;
                        }
                    }
                }
            }
            
            if (!color_used) continue;
            
            /* Carriage return before each color except the first */
            if (!first_color_in_band) {
                fputc('$', out);
            }
            first_color_in_band = 0;
            
            fprintf(out, "#%u", color);
            
            int run_char = -1;
            int run_count = 0;
            
            for (uint32_t x = 0; x < width; x++) {
                /* Build sixel character for this column */
                int sixel = 0;
                
                for (int bit = 0; bit < 6; bit++) {
                    uint32_t y = band_y + (uint32_t)bit;
                    if (y < height) {
                        uint32_t idx = y * width + x;
                        if (indexed[idx] == (uint8_t)color) {
                            sixel |= (1 << bit);
                        }
                    }
                }
                
                int ch = sixel + 63;  /* Sixel encoding: add 63 to get ASCII char */
                
                if (ch == run_char) {
                    run_count++;
                } else {
                    /* Flush previous run */
                    if (run_count > 0) {
                        if (run_count > 3) {
                            fprintf(out, "!%d%c", run_count, run_char);
                        } else {
                            for (int i = 0; i < run_count; i++) {
                                fputc(run_char, out);
                            }
                        }
                    }
                    run_char = ch;
                    run_count = 1;
                }
            }
            
            /* Flush remaining run */
            if (run_count > 0) {
                if (run_count > 3) {
                    fprintf(out, "!%d%c", run_count, run_char);
                } else {
                    for (int i = 0; i < run_count; i++) {
                        fputc(run_char, out);
                    }
                }
            }
        }
        
        /* Newline to move to next band */
        if (band + 1 < num_bands) {
            fputc('-', out);
        }
    }
    
    /* End Sixel sequence */
    fprintf(out, "\033\\");
    
    /* Add newline after Sixel for proper cursor positioning */
    fputc('\n', out);
    fflush(out);
    
    return 1;
}

int cupidimage_is_sixel_terminal(void) {
    /* Windows Terminal */
    const char *wt = getenv("WT_SESSION");
    if (wt && *wt) {
        return 1;
    }
    
    const char *term = getenv("TERM");
    if (term) {
        if (strstr(term, "mlterm") || strstr(term, "yaft") ||
            strstr(term, "foot") || strstr(term, "xterm") ||
            strstr(term, "contour")) {
            return 1;
        }
    }
    
    /* Konsole */
    const char *konsole = getenv("KONSOLE_VERSION");
    if (konsole && atoi(konsole) >= 220000) {
        return 1;
    }
    
    const char *vte = getenv("VTE_VERSION");
    if (vte && atoi(vte) >= 5200) {
        return 1;
    }
    
    /* iTerm2 */
    const char *iterm = getenv("TERM_PROGRAM");
    if (iterm && strcmp(iterm, "iTerm.app") == 0) {
        return 1;
    }
    
    return 0;
}

int cupidimage_render_sixel_with_options(const cupidimage_image *img, FILE *out,
                                        uint32_t term_width, uint32_t term_height,
                                        const cupidimage_sixel_options *opts,
                                        char *err, size_t errcap) {
    (void)term_width;   /* Reserved for future scaling */
    (void)term_height;  /* Reserved for future scaling */
    if (!img || !img->rgba || !out) {
        set_err(err, errcap, "invalid arguments");
        return 0;
    }
    
    if (img->width == 0 || img->height == 0) {
        set_err(err, errcap, "invalid image dimensions");
        return 0;
    }
    
    /* Get options or defaults */
    uint32_t max_colors = opts && opts->max_colors > 0 ? opts->max_colors : 256;
    if (max_colors < 2) max_colors = 2;
    if (max_colors > 256) max_colors = 256;
    
    uint8_t dither_mode = opts ? opts->dither_mode : 1;
    uint8_t use_transparency = opts ? opts->use_transparency : 0;
    uint32_t bg_color = opts ? opts->background_color : 0xFFFFFF;
    int delete_previous = opts ? opts->delete_previous : 0;
    
    if (delete_previous) {
        fprintf(out, "\033[2J\033[H");
    }
    
    /* Blend alpha with background if not using transparency */
    uint8_t *rgba_copy = (uint8_t *)malloc(img->width * img->height * 4);
    if (!rgba_copy) {
        set_err(err, errcap, "memory allocation failed");
        return 0;
    }
    
    memcpy(rgba_copy, img->rgba, img->width * img->height * 4);
    
    if (!use_transparency) {
        uint8_t bg_r = (bg_color >> 16) & 0xFF;
        uint8_t bg_g = (bg_color >> 8) & 0xFF;
        uint8_t bg_b = bg_color & 0xFF;
        
        for (uint32_t i = 0; i < img->width * img->height; i++) {
            uint8_t a = rgba_copy[i * 4 + 3];
            if (a < 255) {
                rgba_copy[i * 4 + 0] = (uint8_t)((rgba_copy[i * 4 + 0] * a + bg_r * (255U - a)) / 255U);
                rgba_copy[i * 4 + 1] = (uint8_t)((rgba_copy[i * 4 + 1] * a + bg_g * (255U - a)) / 255U);
                rgba_copy[i * 4 + 2] = (uint8_t)((rgba_copy[i * 4 + 2] * a + bg_b * (255U - a)) / 255U);
                rgba_copy[i * 4 + 3] = 255;
            }
        }
    }
    
    /* Quantize colors */
    uint8_t *palette = (uint8_t *)malloc(max_colors * 3);
    uint8_t *indexed = (uint8_t *)malloc(img->width * img->height);
    
    if (!palette || !indexed) {
        free(rgba_copy);
        free(palette);
        free(indexed);
        set_err(err, errcap, "memory allocation failed");
        return 0;
    }
    
    uint32_t palette_size = 0;
    if (!quantize_colors(rgba_copy, img->width, img->height, palette, max_colors,
                         &palette_size, use_transparency, indexed)) {
        free(rgba_copy);
        free(palette);
        free(indexed);
        set_err(err, errcap, "color quantization failed");
        return 0;
    }
    
    /* Apply dithering */
    if (dither_mode == 1) {
        apply_floyd_steinberg_dither(rgba_copy, img->width, img->height, palette, palette_size, indexed);
    } else if (dither_mode == 2) {
        apply_atkinson_dither(rgba_copy, img->width, img->height, palette, palette_size, indexed);
    } else if (dither_mode == 3) {
        apply_ordered_dither(rgba_copy, img->width, img->height, palette, palette_size, indexed);
    }
    
    /* Encode and output */
    float aspect = opts && opts->pixel_aspect_ratio > 0 ? opts->pixel_aspect_ratio : 2.0f;
    int result = encode_sixel(out, indexed, img->width, img->height, palette, palette_size, aspect);
    
    free(rgba_copy);
    free(palette);
    free(indexed);
    
    return result;
}

int cupidimage_render_sixel(const cupidimage_image *img, FILE *out,
                            uint32_t term_width, uint32_t term_height,
                            char *err, size_t errcap) {
    return cupidimage_render_sixel_with_options(img, out, term_width, term_height, NULL, err, errcap);
}

static void sleep_ms(unsigned ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000;
    nanosleep(&ts, NULL);
#endif
}

int cupidimage_render_sixel_animation_with_options(const cupidimage_animation *anim, FILE *out,
                                                   uint32_t term_width, uint32_t term_height,
                                                   const cupidimage_sixel_options *opts,
                                                   char *err, size_t errcap) {
    if (!anim || !anim->frames || !out) {
        set_err(err, errcap, "invalid arguments");
        return 0;
    }
    
    if (anim->frame_count == 0) {
        set_err(err, errcap, "no frames in animation");
        return 0;
    }
    
    /* loop_count 0 means infinite loop - use a large number */
    uint32_t loop_count = anim->loop_count == 0 ? 0xFFFFFFFFu : anim->loop_count;
    
    /* Hide cursor during animation */
    fprintf(out, "\033[?25l");
    
    /* Save cursor position */
    fprintf(out, "\0337");
    fflush(out);
    
    for (uint32_t loop = 0; loop < loop_count; loop++) {
        for (uint32_t i = 0; i < anim->frame_count; i++) {
            /* Restore cursor to saved position for each frame */
            fprintf(out, "\0338");
            
            /* Render frame */
            if (!cupidimage_render_sixel_with_options(&anim->frames[i], out,
                                                      term_width, term_height, opts, err, errcap)) {
                fprintf(out, "\033[?25h");  /* Show cursor on error */
                return 0;
            }
            
            /* Delay - delays are already in milliseconds */
            if (i + 1 < anim->frame_count || loop + 1 < loop_count) {
                uint32_t delay_ms = anim->delays ? anim->delays[i] : 100;
                if (delay_ms < 20) delay_ms = 100;  /* Minimum delay */
                sleep_ms(delay_ms);
            }
        }
    }
    
    /* Show cursor */
    fprintf(out, "\033[?25h");
    fflush(out);
    
    return 1;
}

int cupidimage_render_sixel_animation(const cupidimage_animation *anim, FILE *out,
                                     uint32_t term_width, uint32_t term_height,
                                     char *err, size_t errcap) {
    return cupidimage_render_sixel_animation_with_options(anim, out, term_width, term_height,
                                                          NULL, err, errcap);
}
