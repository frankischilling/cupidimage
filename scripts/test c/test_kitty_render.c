#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>

typedef struct {
    uint32_t width;
    uint32_t height;
    uint8_t *rgba;
    uint16_t hotspot_x;
    uint16_t hotspot_y;
} cupidimage_image;

int cupidimage_render_kitty(const cupidimage_image *img, FILE *out,
                           uint32_t term_width, uint32_t term_height,
                           char *err, size_t errcap);

void test_render_small_image(void) {
    cupidimage_image img = {0};
    img.width = 2;
    img.height = 2;
    img.rgba = (uint8_t[]){255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,0,255};

    FILE *f = fmemopen(NULL, 4096, "w");
    char err[128] = {0};

    int ret = cupidimage_render_kitty(&img, f, 80, 24, err, sizeof(err));
    assert(ret == 1);

    fclose(f);
}

int main(void) {
    test_render_small_image();
    printf("test_render_small_image: PASS\n");
    return 0;
}
