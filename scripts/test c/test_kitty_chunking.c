#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>
#include "../src/cupidimage.h"

int main(void) {
    cupidimage_image img = {0};
    img.width = 100;
    img.height = 100;
    img.rgba = calloc(100 * 100 * 4, 1);
    assert(img.rgba);

    /* Fill with red */
    for (int i = 0; i < 100 * 100; i++) {
        img.rgba[i * 4 + 0] = 255;
        img.rgba[i * 4 + 3] = 255;
    }

    FILE *f = tmpfile();
    char err[256] = {0};

    int ret = cupidimage_render_kitty(&img, f, 80, 24, err, sizeof(err));
    printf("Render returned: %d\n", ret);
    if (!ret) {
        printf("Error: %s\n", err);
    }

    /* Check output size */
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    printf("Output size: %ld bytes\n", size);
    
    /* Should have multiple chunks for 100x100 image */
    assert(size > 1000);

    free(img.rgba);
    fclose(f);
    printf("Chunking test: PASS\n");
    return 0;
}
