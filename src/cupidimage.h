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
    uint16_t hotspot_x;
    uint16_t hotspot_y;
} cupidimage_image;

typedef struct cupidimage_animation {
    uint32_t width;
    uint32_t height;
    uint32_t frame_count;
    uint32_t loop_count;
    uint32_t *delays;
    cupidimage_image *frames;
    uint8_t pixel_aspect_ratio; /* GIF: (value + 15) / 64 when non-zero */
    uint8_t color_resolution;   /* GIF: 0-7 (1-8 bits per component) */
    uint8_t *user_input_flags;  /* GIF: per-frame user input flag */
} cupidimage_animation;

typedef struct cupidimage_ico_entry {
    uint8_t width;
    uint8_t height;
    uint8_t colors;
    uint8_t reserved;
    uint16_t planes;
    uint16_t bitcount;
    uint32_t size;
    uint32_t offset;
} cupidimage_ico_entry;

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
int cupidimage_load_bmp(const unsigned char *data, size_t size, cupidimage_image *out,
                        char *err, size_t errcap);
int cupidimage_load_bmp_file(const char *path, cupidimage_image *out,
                             char *err, size_t errcap);
int cupidimage_load_ico(const unsigned char *data, size_t size,
                        cupidimage_image *out,
                        char *err, size_t errcap);
int cupidimage_load_ico_page(const char *path,
                             int page,
                             cupidimage_image *out,
                             char *err, size_t errcap);
int cupidimage_ico_get_directory(const char *path,
                                 cupidimage_ico_entry **entries,
                                 int *count,
                                 int *is_cursor,
                                 char *err, size_t errcap);
int cupidimage_load_tiff(const unsigned char *data, size_t size, cupidimage_image *out,
                         char *err, size_t errcap);
int cupidimage_load_tiff_file(const char *path, cupidimage_image *out,
                              char *err, size_t errcap);
int cupidimage_load_tiff_page(const unsigned char *data, size_t size,
                              cupidimage_image *out,
                              int page_index,
                              char *err, size_t errcap);
int cupidimage_get_tiff_page_count(const unsigned char *data, size_t size,
                                   int *count,
                                   char *err, size_t errcap);
int cupidimage_load_heif(const unsigned char *data, size_t size, cupidimage_image *out,
                         char *err, size_t errcap);
int cupidimage_load_heif_file(const char *path, cupidimage_image *out,
                              char *err, size_t errcap);
int cupidimage_heif_get_item_count(const unsigned char *data, size_t size,
                                   int *count, char *err, size_t errcap);
int cupidimage_load_heif_item(const unsigned char *data, size_t size,
                              cupidimage_image *out, int item_index,
                              char *err, size_t errcap);
int cupidimage_load_heif_depth(const unsigned char *data, size_t size,
                               cupidimage_image *out, char *err, size_t errcap);
int cupidimage_load_heif_sequence(const unsigned char *data, size_t size,
                                  cupidimage_animation *out,
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
