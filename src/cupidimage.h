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

typedef struct cupidimage_animation {
    uint32_t width;
    uint32_t height;
    uint32_t frame_count;
    uint32_t loop_count;
    uint32_t *delays;
    cupidimage_image *frames;
} cupidimage_animation;

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
int cupidimage_load_gif(const unsigned char *data, size_t size, cupidimage_image *out,
                        char *err, size_t errcap);
int cupidimage_load_gif_file(const char *path, cupidimage_image *out,
                             char *err, size_t errcap);
int cupidimage_load_gif_animation(const unsigned char *data, size_t size,
                                  cupidimage_animation *out,
                                  char *err, size_t errcap);
int cupidimage_load_gif_animation_file(const char *path,
                                       cupidimage_animation *out,
                                       char *err, size_t errcap);
int cupidimage_load_image(const unsigned char *data, size_t size, cupidimage_image *out,
                          char *err, size_t errcap);
int cupidimage_load_image_file(const char *path, cupidimage_image *out,
                               char *err, size_t errcap);
void cupidimage_free(cupidimage_image *img);
void cupidimage_free_animation(cupidimage_animation *anim);

int cupidimage_render_ansi(const cupidimage_image *img, FILE *out,
                           int max_width, int max_height);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* CUPIDIMAGE_H */
