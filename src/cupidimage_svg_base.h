#ifndef CUPIDIMAGE_SVG_BASE_H
#define CUPIDIMAGE_SVG_BASE_H

#include <stddef.h>

typedef struct svg_matrix {
    float a;
    float b;
    float c;
    float d;
    float e;
    float f;
} svg_matrix;

int svg_strcasecmp(const char *a, const char *b);
int svg_strncasecmp(const char *a, const char *b, size_t n);
int svg_strcasestr_simple(const char *haystack, const char *needle);

char *svg_strdup(const char *s);
char *svg_strndup(const char *s, size_t len);

void svg_matrix_identity(svg_matrix *m);
void svg_matrix_multiply(const svg_matrix *a, const svg_matrix *b, svg_matrix *out);
void svg_matrix_translate(svg_matrix *m, float tx, float ty);
void svg_matrix_scale(svg_matrix *m, float sx, float sy);
void svg_matrix_rotate(svg_matrix *m, float angle_deg);
void svg_matrix_skewx(svg_matrix *m, float angle_deg);
void svg_matrix_skewy(svg_matrix *m, float angle_deg);
int svg_matrix_invert(const svg_matrix *m, svg_matrix *out);
void svg_matrix_transform_point(const svg_matrix *m, float *x, float *y);

int svg_is_name_char(int c);
void svg_lowercase(char *s);
const char *svg_local_name(const char *name);

#endif
