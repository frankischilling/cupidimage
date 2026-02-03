#define _POSIX_C_SOURCE 200809L

#include "cupidimage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <sys/ioctl.h>
#include <unistd.h>

static void usage(const char *prog) {
    fprintf(stderr, "usage: %s [--fit] <image-file> [max_width] [max_height]\n", prog);
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

static int parse_int(const char *s, int *out) {
    if (!s || !*s) {
        return 0;
    }
    char *end = NULL;
    long val = strtol(s, &end, 10);
    if (!end || *end != '\0') {
        return 0;
    }
    if (val < INT_MIN || val > INT_MAX) {
        return 0;
    }
    *out = (int)val;
    return 1;
}

static void apply_fit(int *maxw, int *maxh) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0) {
        return;
    }
    if (*maxw <= 0 && ws.ws_col > 0) {
        *maxw = (int)ws.ws_col;
    }
    if (*maxh <= 0 && ws.ws_row > 1) {
        *maxh = (int)ws.ws_row - 1;
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    const char *path = NULL;
    int maxw = 0;
    int maxh = 0;
    int fit = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--fit") == 0) {
            fit = 1;
            continue;
        }
        if (!path) {
            path = argv[i];
            continue;
        }
        if (maxw == 0 && parse_int(argv[i], &maxw)) {
            continue;
        }
        if (maxh == 0 && parse_int(argv[i], &maxh)) {
            continue;
        }
        usage(argv[0]);
        return 1;
    }

    if (!path) {
        usage(argv[0]);
        return 1;
    }

    if (fit) {
        apply_fit(&maxw, &maxh);
    }

    char err[128];
    if (is_gif_file(path)) {
        cupidimage_animation anim;
        if (!cupidimage_load_gif_animation_file(path, &anim, err, sizeof(err))) {
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
    if (!cupidimage_load_image_file(path, &img, err, sizeof(err))) {
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
