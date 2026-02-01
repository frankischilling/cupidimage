#include "cupidimage.h"

#include <stdio.h>
#include <stdlib.h>

static void usage(const char *prog) {
    fprintf(stderr, "usage: %s <image-file> [max_width] [max_height]\n", prog);
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

    cupidimage_image img;
    char err[128];
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
