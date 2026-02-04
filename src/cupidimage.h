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

typedef struct cupidimage_tga_metadata {
    char author[41];
    char comments[324];
    uint16_t timestamp[6];
    char job_id[41];
    uint16_t job_time[3];
    char software_id[41];
    uint16_t software_version_number;
    uint8_t software_version_letter;
    uint32_t key_color;
    uint16_t pixel_aspect_ratio_numerator;
    uint16_t pixel_aspect_ratio_denominator;
    uint16_t gamma_numerator;
    uint16_t gamma_denominator;
    uint32_t color_correction_offset;
    uint32_t postage_stamp_offset;
    uint32_t scan_line_offset;
    uint8_t attributes_type;
    uint8_t thumbnail_width;
    uint8_t thumbnail_height;
    uint8_t *thumbnail_rgba;
} cupidimage_tga_metadata;

typedef struct cupidimage_svg_options {
    uint32_t width;
    uint32_t height;
    float scale;
    float dpi;
    float animation_time;
    uint8_t supersampling;
    uint8_t background_alpha;
} cupidimage_svg_options;

typedef struct cupidimage_kitty_options {
    uint32_t image_id;           /* 0 = auto-generate */
    uint32_t placement_id;       /* 0 = auto-generate */
    int compression;             /* 1 = zlib (default), 0 = none */
    int delete_previous;         /* 1 = delete before render, 0 = keep (default) */
} cupidimage_kitty_options;

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
int cupidimage_load_tga(const unsigned char *data, size_t size,
                        cupidimage_image *out, char *err, size_t errcap);
int cupidimage_load_tga_file(const char *path, cupidimage_image *out,
                             char *err, size_t errcap);
int cupidimage_load_tga_with_metadata(const unsigned char *data, size_t size,
                                      cupidimage_image *out,
                                      cupidimage_tga_metadata *meta,
                                      int apply_corrections,
                                      char *err, size_t errcap);
void cupidimage_free_tga_metadata(cupidimage_tga_metadata *meta);
int cupidimage_load_pdf(const unsigned char *data, size_t size,
                        cupidimage_image *out, char *err, size_t errcap);
int cupidimage_load_pdf_file(const char *path, cupidimage_image *out,
                             char *err, size_t errcap);
int cupidimage_load_pdf_page(const unsigned char *data, size_t size,
                             cupidimage_image *out,
                             int page_index,
                             char *err, size_t errcap);
int cupidimage_load_pdf_page_file(const char *path, cupidimage_image *out,
                                  int page_index,
                                  char *err, size_t errcap);
int cupidimage_get_pdf_page_count(const unsigned char *data, size_t size,
                                  int *count,
                                  char *err, size_t errcap);
int cupidimage_get_pdf_page_count_file(const char *path,
                                       int *count,
                                       char *err, size_t errcap);
int cupidimage_load_svg(const unsigned char *data, size_t size, cupidimage_image *out,
                        char *err, size_t errcap);
int cupidimage_load_svg_with_options(const unsigned char *data, size_t size,
                                     cupidimage_image *out,
                                     const cupidimage_svg_options *opts,
                                     char *err, size_t errcap);
int cupidimage_load_svg_file(const char *path, cupidimage_image *out,
                             char *err, size_t errcap);
int cupidimage_load_svg_file_with_options(const char *path, cupidimage_image *out,
                                          const cupidimage_svg_options *opts,
                                          char *err, size_t errcap);
int cupidimage_svg_get_dimensions(const unsigned char *data, size_t size,
                                  uint32_t *width, uint32_t *height,
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

/* Kitty graphics protocol */
int cupidimage_is_kitty_terminal(void);

int cupidimage_render_kitty(const cupidimage_image *img, FILE *out,
                           uint32_t term_width, uint32_t term_height,
                           char *err, size_t errcap);

int cupidimage_render_kitty_with_options(const cupidimage_image *img, FILE *out,
                                        uint32_t term_width, uint32_t term_height,
                                        const cupidimage_kitty_options *opts,
                                        char *err, size_t errcap);

int cupidimage_render_kitty_animation(const cupidimage_animation *anim, FILE *out,
                                     uint32_t term_width, uint32_t term_height,
                                     char *err, size_t errcap);

int cupidimage_render_kitty_animation_with_options(const cupidimage_animation *anim, FILE *out,
                                                   uint32_t term_width, uint32_t term_height,
                                                   const cupidimage_kitty_options *opts,
                                                   char *err, size_t errcap);

int cupidimage_kitty_delete_image(FILE *out, uint32_t image_id,
                                 char *err, size_t errcap);

int cupidimage_kitty_delete_placement(FILE *out, uint32_t image_id,
                                     uint32_t placement_id,
                                     char *err, size_t errcap);

int cupidimage_kitty_delete_all(FILE *out, char *err, size_t errcap);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* CUPIDIMAGE_H */
