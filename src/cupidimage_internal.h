#ifndef CUPIDIMAGE_INTERNAL_H
#define CUPIDIMAGE_INTERNAL_H

#include "cupidimage.h"

#include <stddef.h>

typedef int (*cupidimage_image_loader_fn)(const unsigned char *data, size_t size,
                                          cupidimage_image *out,
                                          char *err, size_t errcap);
typedef int (*cupidimage_animation_loader_fn)(const unsigned char *data, size_t size,
                                              cupidimage_animation *out,
                                              char *err, size_t errcap);

void cupidimage_set_err(char *err, size_t errcap, const char *msg);

int cupidimage_read_file_bytes(const char *path,
                               unsigned char **data,
                               size_t *size,
                               char *err,
                               size_t errcap);

int cupidimage_load_image_file_via_memory(const char *path,
                                          cupidimage_image *out,
                                          char *err,
                                          size_t errcap,
                                          cupidimage_image_loader_fn loader);

int cupidimage_load_animation_file_via_memory(const char *path,
                                              cupidimage_animation *out,
                                              char *err,
                                              size_t errcap,
                                              cupidimage_animation_loader_fn loader);

#endif
