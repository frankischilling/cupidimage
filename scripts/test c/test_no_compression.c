#include <stdio.h>
#include <stdlib.h>
#include "../src/cupidimage.h"

int main(void) {
    cupidimage_image img;
    char err[256];
    
    if (!cupidimage_load_image_file("assets/bmp/1x1.bmp", &img, err, sizeof(err))) {
        fprintf(stderr, "Load error: %s\n", err);
        return 1;
    }
    
    fprintf(stderr, "Loaded image: %ux%u\n", img.width, img.height);
    
    cupidimage_kitty_options opts = {0};
    opts.compression = 0;  /* Disable compression for testing */
    
    fprintf(stderr, "Rendering without compression...\n");
    if (!cupidimage_render_kitty_with_options(&img, stdout, 80, 24, &opts, err, sizeof(err))) {
        fprintf(stderr, "Render error: %s\n", err);
        cupidimage_free(&img);
        return 1;
    }
    
    fprintf(stderr, "Done\n");
    cupidimage_free(&img);
    return 0;
}
