#define _POSIX_C_SOURCE 200809L

#include "cupidimage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void usage(const char *prog) {
    fprintf(stderr, "usage: %s <image-file> [max_width] [max_height]\n", prog);
}

static int is_gif_file(const char *path) {
    unsigned char sig[6];
    FILE *f = fopen(path, "rb");
    if (!f) {
        return 0;
    }
    size_t nread = fread(sig, 1, sizeof(sig), f);
    fclose(f);
    if (nread != sizeof(sig)) {
        return 0;
    }
    return memcmp(sig, "GIF87a", 6) == 0 || memcmp(sig, "GIF89a", 6) == 0;
}

static void sleep_ms(unsigned ms) {
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
    nanosleep(&ts, NULL);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    int maxw = 0;
    int maxh = 0;
    if (argc >= 3) {
        maxw = atoi(argv[2]);
    }
    if (argc >= 4) {
        maxh = atoi(argv[3]);
    }

    char err[128];
    if (is_gif_file(argv[1])) {
        cupidimage_animation anim;
        if (!cupidimage_load_gif_animation_file(argv[1], &anim, err, sizeof(err))) {
            fprintf(stderr, "load error: %s\n", err);
            return 1;
        }
        if (anim.frame_count == 0) {
            fprintf(stderr, "load error: missing GIF frame\n");
            cupidimage_free_animation(&anim);
            return 1;
        }
        if (anim.frame_count == 1) {
            if (!cupidimage_render_ansi(&anim.frames[0], stdout, maxw, maxh)) {
                fprintf(stderr, "render error\n");
                cupidimage_free_animation(&anim);
                return 1;
            }
            cupidimage_free_animation(&anim);
            return 0;
        }

        fputs("\x1b[?25l\x1b[2J", stdout);
        uint32_t loops = anim.loop_count;
        if (loops == 0) {
            loops = 0xFFFFFFFFu;
        }
        for (uint32_t loop = 0; loop < loops; loop++) {
            for (uint32_t i = 0; i < anim.frame_count; i++) {
                fputs("\x1b[H", stdout);
                if (!cupidimage_render_ansi(&anim.frames[i], stdout, maxw, maxh)) {
                    fprintf(stderr, "render error\n");
                    cupidimage_free_animation(&anim);
                    fputs("\x1b[?25h", stdout);
                    return 1;
                }
                fflush(stdout);
                sleep_ms(anim.delays[i]);
            }
        }
        fputs("\x1b[?25h", stdout);
        cupidimage_free_animation(&anim);
        return 0;
    }

    cupidimage_image img;
    if (!cupidimage_load_image_file(argv[1], &img, err, sizeof(err))) {
        fprintf(stderr, "load error: %s\n", err);
        return 1;
    }

    if (!cupidimage_render_ansi(&img, stdout, maxw, maxh)) {
        fprintf(stderr, "render error\n");
        cupidimage_free(&img);
        return 1;
    }

    cupidimage_free(&img);
    return 0;
}
