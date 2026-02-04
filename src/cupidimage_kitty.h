#ifndef CUPIDIMAGE_KITTY_H
#define CUPIDIMAGE_KITTY_H

#include <stddef.h>
#include <stdint.h>

/* Base64 encode data, returns number of bytes written to out (excluding null terminator) */
int kitty_base64_encode(const uint8_t *data, size_t size, char *out, size_t outcap);

/* Detect if running in Kitty terminal */
int cupidimage_is_kitty_terminal(void);

#endif
