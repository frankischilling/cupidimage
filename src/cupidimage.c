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

    uint8_t last_r = 0, last_g = 0, last_b = 0;
    int have_last = 0;

    for (int y = 0; y < out_h; y++) {
        int src_y = (int)((long long)y * height / out_h);
        for (int x = 0; x < out_w; x++) {
            int src_x = (int)((long long)x * width / out_w);
            const uint8_t *px = img->rgba + ((size_t)src_y * (size_t)width + (size_t)src_x) * 4u;
            uint8_t a = px[3];
            unsigned inv = 255u - (unsigned)a;
            uint8_t r = (uint8_t)(((unsigned)px[0] * (unsigned)a + 255u * inv) / 255u);
            uint8_t g = (uint8_t)(((unsigned)px[1] * (unsigned)a + 255u * inv) / 255u);
            uint8_t b = (uint8_t)(((unsigned)px[2] * (unsigned)a + 255u * inv) / 255u);

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
