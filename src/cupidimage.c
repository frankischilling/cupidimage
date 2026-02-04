#include "cupidimage.h"

#include <stdlib.h>

void cupidimage_free(cupidimage_image *img) {
    if (!img) {
        return;
    }
    free(img->rgba);
    img->rgba = NULL;
    img->width = 0;
    img->height = 0;
    img->hotspot_x = 0;
    img->hotspot_y = 0;
}

static void write_ansi_bg(FILE *out, uint8_t r, uint8_t g, uint8_t b) {
    fprintf(out, "\x1b[48;2;%u;%u;%um", r, g, b);
}

static void sample_blended_rgb(const cupidimage_image *img, int src_x, int src_y,
                               unsigned *r, unsigned *g, unsigned *b) {
    const uint8_t *px = img->rgba + ((size_t)src_y * (size_t)img->width + (size_t)src_x) * 4u;
    unsigned a = (unsigned)px[3];
    unsigned inv = 255u - a;
    *r += ((unsigned)px[0] * a + 255u * inv) / 255u;
    *g += ((unsigned)px[1] * a + 255u * inv) / 255u;
    *b += ((unsigned)px[2] * a + 255u * inv) / 255u;
}

int cupidimage_render_ansi(const cupidimage_image *img, FILE *out, int max_width, int max_height) {
    if (!img || !img->rgba || !out || img->width == 0 || img->height == 0) {
        return 0;
    }

    int width = (int)img->width;
    int height = (int)img->height;
    double scale = 1.0;
    if (max_width > 0 && width > max_width) {
        double s = (double)max_width / (double)width;
        if (s < scale) {
            scale = s;
        }
    }
    if (max_height > 0 && height > max_height) {
        double s = (double)max_height / (double)height;
        if (s < scale) {
            scale = s;
        }
    }

    int out_w = (int)(width * scale);
    int out_h = (int)(height * scale);
    if (out_w < 1) out_w = 1;
    if (out_h < 1) out_h = 1;
    int downsampling = (out_w < width) || (out_h < height);

    uint8_t last_r = 0, last_g = 0, last_b = 0;
    int have_last = 0;

    for (int y = 0; y < out_h; y++) {
        for (int x = 0; x < out_w; x++) {
            uint8_t r = 0, g = 0, b = 0;
            if (!downsampling) {
                int src_y = (int)((long long)y * height / out_h);
                int src_x = (int)((long long)x * width / out_w);
                unsigned ar = 0, ag = 0, ab = 0;
                sample_blended_rgb(img, src_x, src_y, &ar, &ag, &ab);
                r = (uint8_t)ar;
                g = (uint8_t)ag;
                b = (uint8_t)ab;
            } else {
                /* Supersample each output cell to reduce aliasing when fitting. */
                const int sx_count = 4;
                const int sy_count = 4;
                double x0 = (double)x * (double)width / (double)out_w;
                double x1 = (double)(x + 1) * (double)width / (double)out_w;
                double y0 = (double)y * (double)height / (double)out_h;
                double y1 = (double)(y + 1) * (double)height / (double)out_h;
                unsigned sum_r = 0, sum_g = 0, sum_b = 0;
                unsigned samples = 0;
                for (int sy = 0; sy < sy_count; sy++) {
                    double py = y0 + ((double)sy + 0.5) * (y1 - y0) / (double)sy_count;
                    int src_y = (int)py;
                    if (src_y < 0) src_y = 0;
                    if (src_y >= height) src_y = height - 1;
                    for (int sx = 0; sx < sx_count; sx++) {
                        double px = x0 + ((double)sx + 0.5) * (x1 - x0) / (double)sx_count;
                        int src_x = (int)px;
                        if (src_x < 0) src_x = 0;
                        if (src_x >= width) src_x = width - 1;
                        sample_blended_rgb(img, src_x, src_y, &sum_r, &sum_g, &sum_b);
                        samples++;
                    }
                }
                if (samples == 0) {
                    samples = 1;
                }
                r = (uint8_t)(sum_r / samples);
                g = (uint8_t)(sum_g / samples);
                b = (uint8_t)(sum_b / samples);
            }

            if (!have_last || r != last_r || g != last_g || b != last_b) {
                write_ansi_bg(out, r, g, b);
                last_r = r;
                last_g = g;
                last_b = b;
                have_last = 1;
            }
            fputc(' ', out);
        }
        fprintf(out, "\x1b[0m\n");
        have_last = 0;
    }

    return 1;
}
