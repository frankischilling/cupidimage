#include "cupidimage_svg_base.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

int svg_strcasecmp(const char *a, const char *b) {
    if (!a || !b) {
        return a ? 1 : (b ? -1 : 0);
    }
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) {
            return ca - cb;
        }
        a++;
        b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

int svg_strncasecmp(const char *a, const char *b, size_t n) {
    if (!a || !b) {
        return a ? 1 : (b ? -1 : 0);
    }
    for (size_t i = 0; i < n; i++) {
        int ca = tolower((unsigned char)a[i]);
        int cb = tolower((unsigned char)b[i]);
        if (ca != cb || a[i] == '\0' || b[i] == '\0') {
            return ca - cb;
        }
    }
    return 0;
}

int svg_strcasestr_simple(const char *haystack, const char *needle) {
    if (!haystack || !needle || !*needle) {
        return 0;
    }
    size_t nlen = strlen(needle);
    for (const char *p = haystack; *p; p++) {
        if (svg_strncasecmp(p, needle, nlen) == 0) {
            return 1;
        }
    }
    return 0;
}

char *svg_strdup(const char *s) {
    if (!s) {
        return NULL;
    }
    size_t len = strlen(s);
    char *out = (char *)malloc(len + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, s, len);
    out[len] = '\0';
    return out;
}

char *svg_strndup(const char *s, size_t len) {
    if (!s) {
        return NULL;
    }
    char *out = (char *)malloc(len + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, s, len);
    out[len] = '\0';
    return out;
}

void svg_matrix_identity(svg_matrix *m) {
    m->a = 1.0f;
    m->b = 0.0f;
    m->c = 0.0f;
    m->d = 1.0f;
    m->e = 0.0f;
    m->f = 0.0f;
}

void svg_matrix_multiply(const svg_matrix *a, const svg_matrix *b, svg_matrix *out) {
    svg_matrix r;
    r.a = a->a * b->a + a->c * b->b;
    r.b = a->b * b->a + a->d * b->b;
    r.c = a->a * b->c + a->c * b->d;
    r.d = a->b * b->c + a->d * b->d;
    r.e = a->a * b->e + a->c * b->f + a->e;
    r.f = a->b * b->e + a->d * b->f + a->f;
    *out = r;
}

void svg_matrix_translate(svg_matrix *m, float tx, float ty) {
    svg_matrix t;
    svg_matrix_identity(&t);
    t.e = tx;
    t.f = ty;
    svg_matrix_multiply(&t, m, m);
}

void svg_matrix_scale(svg_matrix *m, float sx, float sy) {
    svg_matrix s;
    svg_matrix_identity(&s);
    s.a = sx;
    s.d = sy;
    svg_matrix_multiply(&s, m, m);
}

void svg_matrix_rotate(svg_matrix *m, float angle_deg) {
    float r = angle_deg * (float)M_PI / 180.0f;
    float c = cosf(r);
    float s = sinf(r);
    svg_matrix rot;
    svg_matrix_identity(&rot);
    rot.a = c;
    rot.c = -s;
    rot.b = s;
    rot.d = c;
    svg_matrix_multiply(&rot, m, m);
}

void svg_matrix_skewx(svg_matrix *m, float angle_deg) {
    float r = angle_deg * (float)M_PI / 180.0f;
    svg_matrix t;
    svg_matrix_identity(&t);
    t.c = tanf(r);
    svg_matrix_multiply(&t, m, m);
}

void svg_matrix_skewy(svg_matrix *m, float angle_deg) {
    float r = angle_deg * (float)M_PI / 180.0f;
    svg_matrix t;
    svg_matrix_identity(&t);
    t.b = tanf(r);
    svg_matrix_multiply(&t, m, m);
}

int svg_matrix_invert(const svg_matrix *m, svg_matrix *out) {
    float det = m->a * m->d - m->b * m->c;
    if (fabsf(det) < 1e-12f) {
        return 0;
    }
    float inv = 1.0f / det;
    svg_matrix r;
    r.a = m->d * inv;
    r.b = -m->b * inv;
    r.c = -m->c * inv;
    r.d = m->a * inv;
    r.e = (m->c * m->f - m->d * m->e) * inv;
    r.f = (m->b * m->e - m->a * m->f) * inv;
    *out = r;
    return 1;
}

void svg_matrix_transform_point(const svg_matrix *m, float *x, float *y) {
    float nx = m->a * (*x) + m->c * (*y) + m->e;
    float ny = m->b * (*x) + m->d * (*y) + m->f;
    *x = nx;
    *y = ny;
}

int svg_is_name_char(int c) {
    return isalnum(c) || c == ':' || c == '-' || c == '_';
}

void svg_lowercase(char *s) {
    if (!s) {
        return;
    }
    for (; *s; s++) {
        *s = (char)tolower((unsigned char)*s);
    }
}

const char *svg_local_name(const char *name) {
    const char *colon = name ? strrchr(name, ':') : NULL;
    return colon ? colon + 1 : name;
}
