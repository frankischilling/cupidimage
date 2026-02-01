#ifndef CUPIDIMAGE_H
#define CUPIDIMAGE_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cupidimage_image {
    uint32_t width;
    uint32_t height;
    uint8_t *rgba;
} cupidimage_image;

int cupidimage_load_png(const unsigned char *data, size_t size, cupidimage_image *out,
                        char *err, size_t errcap);
int cupidimage_load_png_file(const char *path, cupidimage_image *out,
                             char *err, size_t errcap);
int cupidimage_load_jpeg(const unsigned char *data, size_t size, cupidimage_image *out,
                         char *err, size_t errcap);
int cupidimage_load_jpeg_file(const char *path, cupidimage_image *out,
                              char *err, size_t errcap);
int cupidimage_load_webp(const unsigned char *data, size_t size, cupidimage_image *out,
                         char *err, size_t errcap);
int cupidimage_load_webp_file(const char *path, cupidimage_image *out,
                              char *err, size_t errcap);
int cupidimage_load_image(const unsigned char *data, size_t size, cupidimage_image *out,
                          char *err, size_t errcap);
int cupidimage_load_image_file(const char *path, cupidimage_image *out,
                               char *err, size_t errcap);
void cupidimage_free(cupidimage_image *img);

int cupidimage_render_ansi(const cupidimage_image *img, FILE *out,
                           int max_width, int max_height);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* CUPIDIMAGE_H */
