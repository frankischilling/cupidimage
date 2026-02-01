#ifndef CUPIDIMAGE_WEBP_LOSSLESS_H
#define CUPIDIMAGE_WEBP_LOSSLESS_H

#include <stddef.h>
#include <stdint.h>

#include "cupidimage.h"

int cupidimage_decode_vp8l(const unsigned char *data, size_t size,
                            cupidimage_image *out, char *err, size_t errcap);

#endif /* CUPIDIMAGE_WEBP_LOSSLESS_H */
