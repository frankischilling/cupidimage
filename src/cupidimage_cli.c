#define _POSIX_C_SOURCE 200809L

#include "cupidimage.h"

#include <ctype.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <sys/ioctl.h>
#include <unistd.h>

static void usage(const char *prog) {
    fprintf(stderr, "usage: %s [--fit] [--svg-time seconds] [--pdf-page number] <image-file> [max_width] [max_height]\n", prog);
}

static int is_gif_file(const char *path) {
    unsigned char sig[6];
    FILE *f = fopen(path, "rb");
    if (!f) {
        return 0;
    }
    size_t nread = fread(sig, 1, sizeof(sig), f);
    fclose(f);
    if (nread != sizeof(sig)) {
        return 0;
    }
    return memcmp(sig, "GIF87a", 6) == 0 || memcmp(sig, "GIF89a", 6) == 0;
}

static void sleep_ms(unsigned ms) {
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
    nanosleep(&ts, NULL);
}

static int monotonic_seconds(double *out) {
    if (!out) {
        return 0;
    }
    struct timespec ts;
#ifdef CLOCK_MONOTONIC
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        *out = (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
        return 1;
    }
#endif
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
        *out = (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
        return 1;
    }
    return 0;
}

static int parse_int(const char *s, int *out) {
    if (!s || !*s) {
        return 0;
    }
    char *end = NULL;
    long val = strtol(s, &end, 10);
    if (!end || *end != '\0') {
        return 0;
    }
    if (val < INT_MIN || val > INT_MAX) {
        return 0;
    }
    *out = (int)val;
    return 1;
}

static int parse_float(const char *s, float *out) {
    if (!s || !*s || !out) {
        return 0;
    }
    char *end = NULL;
    float val = strtof(s, &end);
    if (!end || *end != '\0') {
        return 0;
    }
    *out = val;
    return 1;
}

static int path_has_svg_extension(const char *path) {
    if (!path) {
        return 0;
    }
    const char *dot = strrchr(path, '.');
    if (!dot) {
        return 0;
    }
    return strcmp(dot, ".svg") == 0 || strcmp(dot, ".SVG") == 0;
}

static int path_has_pdf_extension(const char *path) {
    if (!path) {
        return 0;
    }
    const char *dot = strrchr(path, '.');
    if (!dot) {
        return 0;
    }
    return strcmp(dot, ".pdf") == 0 || strcmp(dot, ".PDF") == 0;
}

static int read_file_text(const char *path, char **out_text, size_t *out_size) {
    if (!path || !out_text || !out_size) {
        return 0;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        return 0;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }
    long end = ftell(f);
    if (end < 0) {
        fclose(f);
        return 0;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }
    size_t size = (size_t)end;
    char *buf = (char *)malloc(size + 1u);
    if (!buf) {
        fclose(f);
        return 0;
    }
    size_t nread = fread(buf, 1u, size, f);
    fclose(f);
    if (nread != size) {
        free(buf);
        return 0;
    }
    buf[size] = '\0';
    *out_text = buf;
    *out_size = size;
    return 1;
}

static int ci_eq_char(char a, char b) {
    return tolower((unsigned char)a) == tolower((unsigned char)b);
}

static int ci_starts_with(const char *s, const char *prefix) {
    if (!s || !prefix) {
        return 0;
    }
    while (*prefix) {
        if (!*s || !ci_eq_char(*s, *prefix)) {
            return 0;
        }
        s++;
        prefix++;
    }
    return 1;
}

static int is_name_char(char c) {
    unsigned char uc = (unsigned char)c;
    return isalnum(uc) || c == '_' || c == '-' || c == ':' || c == '.';
}

static int extract_attr_value_ci(const char *start, const char *end,
                                 const char *attr, char *out, size_t out_cap) {
    if (!start || !end || !attr || !out || out_cap == 0 || end <= start) {
        return 0;
    }
    size_t attr_len = strlen(attr);
    const char *p = start;
    while (p < end) {
        if ((size_t)(end - p) >= attr_len && ci_starts_with(p, attr)) {
            char before = (p > start) ? p[-1] : '\0';
            char after = ((size_t)(end - p) > attr_len) ? p[attr_len] : '\0';
            if (!is_name_char(before) && !is_name_char(after)) {
                const char *q = p + attr_len;
                while (q < end && isspace((unsigned char)*q)) {
                    q++;
                }
                if (q >= end || *q != '=') {
                    p++;
                    continue;
                }
                q++;
                while (q < end && isspace((unsigned char)*q)) {
                    q++;
                }
                if (q >= end || (*q != '"' && *q != '\'')) {
                    p++;
                    continue;
                }
                char quote = *q++;
                const char *val_start = q;
                while (q < end && *q != quote) {
                    q++;
                }
                if (q >= end) {
                    return 0;
                }
                size_t n = (size_t)(q - val_start);
                if (n >= out_cap) {
                    n = out_cap - 1u;
                }
                memcpy(out, val_start, n);
                out[n] = '\0';
                return 1;
            }
        }
        p++;
    }
    return 0;
}

static int parse_clock_seconds(const char *s, float *out) {
    if (!s || !*s || !out) {
        return 0;
    }
    char *end = NULL;
    float v = strtof(s, &end);
    if (!end || end == s) {
        return 0;
    }
    while (*end && isspace((unsigned char)*end)) {
        end++;
    }
    if (ci_starts_with(end, "ms")) {
        v /= 1000.0f;
        end += 2;
    } else if (*end == 's' || *end == 'S') {
        end++;
    } else if (*end == 'm' || *end == 'M') {
        v *= 60.0f;
        end++;
    } else if (*end == 'h' || *end == 'H') {
        v *= 3600.0f;
        end++;
    }
    while (*end && isspace((unsigned char)*end)) {
        end++;
    }
    if (*end) {
        return 0;
    }
    if (v < 0.0f) {
        v = 0.0f;
    }
    *out = v;
    return 1;
}

static int parse_repeat_count(const char *s, float *count, int *indefinite) {
    if (!count || !indefinite) {
        return 0;
    }
    *count = 1.0f;
    *indefinite = 0;
    if (!s || !*s) {
        return 1;
    }
    while (*s && isspace((unsigned char)*s)) {
        s++;
    }
    if (ci_starts_with(s, "indefinite")) {
        *indefinite = 1;
        return 1;
    }
    char *end = NULL;
    float v = strtof(s, &end);
    if (!end || end == s) {
        return 0;
    }
    while (*end && isspace((unsigned char)*end)) {
        end++;
    }
    if (*end || v <= 0.0f) {
        return 0;
    }
    *count = v;
    return 1;
}

static const char *find_ci_substr(const char *haystack, const char *needle) {
    if (!haystack || !needle || !*needle) {
        return NULL;
    }
    size_t nlen = strlen(needle);
    const char *p = haystack;
    while (*p) {
        size_t i = 0;
        while (i < nlen && p[i] && ci_eq_char(p[i], needle[i])) {
            i++;
        }
        if (i == nlen) {
            return p;
        }
        p++;
    }
    return NULL;
}

static int parse_css_animation_shorthand_timing(const char *value,
                                                float *duration, float *delay,
                                                float *repeat, int *indefinite) {
    if (!value || !duration || !delay || !repeat || !indefinite) {
        return 0;
    }
    *duration = 0.0f;
    *delay = 0.0f;
    *repeat = 1.0f;
    *indefinite = 0;
    int have_duration = 0;

    const char *p = value;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) {
            p++;
        }
        if (!*p || *p == ',' || *p == ';' || *p == '}') {
            break;
        }
        const char *start = p;
        int paren_depth = 0;
        while (*p) {
            char c = *p;
            if (c == '(') {
                paren_depth++;
            } else if (c == ')' && paren_depth > 0) {
                paren_depth--;
            } else if ((isspace((unsigned char)c) || c == ',' || c == ';' || c == '}') && paren_depth == 0) {
                break;
            }
            p++;
        }
        const char *end = p;
        while (end > start && isspace((unsigned char)end[-1])) {
            end--;
        }
        if (end > start) {
            char tok[128];
            size_t n = (size_t)(end - start);
            if (n >= sizeof(tok)) {
                n = sizeof(tok) - 1u;
            }
            memcpy(tok, start, n);
            tok[n] = '\0';
            float secs = 0.0f;
            if (parse_clock_seconds(tok, &secs)) {
                if (!have_duration) {
                    *duration = secs;
                    have_duration = 1;
                } else {
                    *delay = secs;
                }
            } else if (ci_starts_with(tok, "infinite")) {
                *indefinite = 1;
            } else {
                char *num_end = NULL;
                float rep = strtof(tok, &num_end);
                if (num_end && *num_end == '\0' && rep > 0.0f) {
                    *repeat = rep;
                }
            }
        }
    }
    return have_duration;
}

static int analyze_css_animation_timeline(const char *text, float *duration_s, int *indefinite) {
    if (!text || !duration_s || !indefinite) {
        return 0;
    }
    int found = 0;
    float max_end = 0.0f;
    int has_indefinite = 0;
    const char *p = text;
    while ((p = find_ci_substr(p, "animation:")) != NULL) {
        p += 10;
        while (*p && isspace((unsigned char)*p)) {
            p++;
        }
        const char *end = p;
        int paren_depth = 0;
        while (*end) {
            char c = *end;
            if (c == '(') {
                paren_depth++;
            } else if (c == ')' && paren_depth > 0) {
                paren_depth--;
            } else if ((c == ';' || c == '}') && paren_depth == 0) {
                break;
            }
            end++;
        }
        char *value = NULL;
        if (end > p) {
            value = (char *)malloc((size_t)(end - p) + 1u);
            if (value) {
                memcpy(value, p, (size_t)(end - p));
                value[end - p] = '\0';
            }
        }
        if (value) {
            float dur = 0.0f, delay = 0.0f, repeat = 1.0f;
            int ind = 0;
            if (parse_css_animation_shorthand_timing(value, &dur, &delay, &repeat, &ind) && dur > 0.0f) {
                found = 1;
                if (ind) {
                    has_indefinite = 1;
                } else {
                    float stop = delay + dur * repeat;
                    if (stop > max_end) {
                        max_end = stop;
                    }
                }
            }
            free(value);
        }
        p = end;
        if (*p) {
            p++;
        }
    }
    *duration_s = max_end > 0.05f ? max_end : 2.0f;
    *indefinite = has_indefinite;
    return found;
}

static int analyze_svg_animation_timeline(const char *text, float *duration_s, int *indefinite) {
    if (!text || !duration_s || !indefinite) {
        return 0;
    }
    int found = 0;
    float max_end = 0.0f;
    int has_indefinite = 0;
    const char *p = text;
    while ((p = strchr(p, '<')) != NULL) {
        p++;
        while (*p && isspace((unsigned char)*p)) {
            p++;
        }
        if (!*p || *p == '/' || *p == '!' || *p == '?') {
            continue;
        }
        if (!ci_starts_with(p, "animate") && !ci_starts_with(p, "set")) {
            continue;
        }
        const char *gt = p;
        char quote = '\0';
        while (*gt) {
            if (quote) {
                if (*gt == quote) {
                    quote = '\0';
                }
            } else if (*gt == '"' || *gt == '\'') {
                quote = *gt;
            } else if (*gt == '>') {
                break;
            }
            gt++;
        }
        if (!*gt) {
            break;
        }
        found = 1;
        char dur_buf[64] = {0};
        char begin_buf[64] = {0};
        char repeat_buf[64] = {0};
        float begin = 0.0f;
        float dur = 0.0f;
        float repeat = 1.0f;
        int rep_indefinite = 0;
        if (extract_attr_value_ci(p, gt, "begin", begin_buf, sizeof(begin_buf))) {
            parse_clock_seconds(begin_buf, &begin);
        }
        if (extract_attr_value_ci(p, gt, "dur", dur_buf, sizeof(dur_buf))) {
            parse_clock_seconds(dur_buf, &dur);
        } else {
            dur = 0.001f;
        }
        if (extract_attr_value_ci(p, gt, "repeatCount", repeat_buf, sizeof(repeat_buf))) {
            parse_repeat_count(repeat_buf, &repeat, &rep_indefinite);
        }
        if (rep_indefinite) {
            has_indefinite = 1;
        } else {
            float end = begin + dur * repeat;
            if (end > max_end) {
                max_end = end;
            }
        }
        p = gt + 1;
    }
    float css_duration = 0.0f;
    int css_indefinite = 0;
    int css_found = analyze_css_animation_timeline(text, &css_duration, &css_indefinite);

    if (css_found && css_duration > max_end) {
        max_end = css_duration;
    }
    if (css_indefinite) {
        has_indefinite = 1;
    }
    *duration_s = max_end > 0.05f ? max_end : 2.0f;
    *indefinite = has_indefinite;
    return found || css_found;
}

static int render_svg_animation(const char *path, int maxw, int maxh, char *err, size_t errcap) {
    char *text = NULL;
    size_t text_size = 0;
    if (!read_file_text(path, &text, &text_size)) {
        return 0;
    }
    (void)text_size;
    float timeline_s = 0.0f;
    int indefinite = 0;
    int has_animation = analyze_svg_animation_timeline(text, &timeline_s, &indefinite);
    if (!has_animation) {
        int has_smil = find_ci_substr(text, "<animate") != NULL || find_ci_substr(text, "<set") != NULL;
        int has_css = find_ci_substr(text, "@keyframes") != NULL || find_ci_substr(text, "animation:") != NULL;
        if (has_smil || has_css) {
            has_animation = 1;
            if (timeline_s <= 0.05f) {
                timeline_s = 3.0f;
            }
            if (!indefinite && find_ci_substr(text, "infinite") != NULL) {
                indefinite = 1;
            }
        }
    }
    free(text);
    if (!has_animation) {
        return 0;
    }
    /* Note: Removed isatty() check to allow animation playback in all terminals.
     * The original check was too restrictive and prevented animations from playing
     * in valid terminal environments (WSL, tmux, screen, IDE terminals, etc.).
     * If the terminal doesn't support ANSI escape codes, the output will simply
     * be garbled, which is acceptable for edge cases. */

    const unsigned frame_ms = 33u;
    const float frame_dt = (float)frame_ms / 1000.0f;
    uint32_t loops = indefinite ? 0xFFFFFFFFu : 1u;
    fputs("\x1b[?25l\x1b[2J", stdout);
    for (uint32_t loop = 0; loop < loops; loop++) {
        uint32_t frame_index = 0;
        double cycle_start = 0.0;
        int have_clock = monotonic_seconds(&cycle_start);
        while (1) {
            float t = 0.0f;
            if (have_clock) {
                double now = 0.0;
                if (monotonic_seconds(&now)) {
                    t = (float)(now - cycle_start);
                } else {
                    have_clock = 0;
                    t = (float)frame_index * frame_dt;
                }
            } else {
                t = (float)frame_index * frame_dt;
            }
            if (t > timeline_s + FLT_EPSILON) {
                break;
            }
            cupidimage_svg_options opts;
            opts.width = 0;
            opts.height = 0;
            opts.scale = 1.0f;
            opts.dpi = 96.0f;
            opts.animation_time = t;
            opts.supersampling = 2;
            opts.background_alpha = 0;

            cupidimage_image img;
            if (!cupidimage_load_svg_file_with_options(path, &img, &opts, err, errcap)) {
                fputs("\x1b[?25h", stdout);
                return -1;
            }
            fputs("\x1b[H", stdout);
            if (!cupidimage_render_ansi(&img, stdout, maxw, maxh)) {
                cupidimage_free(&img);
                fputs("\x1b[?25h", stdout);
                snprintf(err, errcap, "render error");
                return -1;
            }
            cupidimage_free(&img);
            fflush(stdout);
            frame_index++;
            if (have_clock) {
                double target = cycle_start + (double)frame_index * (double)frame_dt;
                double now = 0.0;
                if (monotonic_seconds(&now) && target > now) {
                    double delta = target - now;
                    unsigned wait_ms = (unsigned)(delta * 1000.0);
                    if (wait_ms > 0u) {
                        sleep_ms(wait_ms);
                    }
                }
            } else {
                sleep_ms(frame_ms);
            }
        }
    }
    fputs("\x1b[?25h", stdout);
    return 1;
}

static void apply_fit(int *maxw, int *maxh) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0) {
        return;
    }
    if (*maxw <= 0 && ws.ws_col > 0) {
        *maxw = (int)ws.ws_col;
    }
    if (*maxh <= 0 && ws.ws_row > 1) {
        *maxh = (int)ws.ws_row - 1;
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    const char *path = NULL;
    int maxw = 0;
    int maxh = 0;
    int fit = 0;
    int has_svg_time = 0;
    float svg_time = 0.0f;
    int has_pdf_page = 0;
    int pdf_page = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--fit") == 0) {
            fit = 1;
            continue;
        }
        if (strcmp(argv[i], "--svg-time") == 0) {
            if (i + 1 >= argc || !parse_float(argv[i + 1], &svg_time)) {
                usage(argv[0]);
                return 1;
            }
            has_svg_time = 1;
            i++;
            continue;
        }
        if (strcmp(argv[i], "--pdf-page") == 0) {
            if (i + 1 >= argc || !parse_int(argv[i + 1], &pdf_page) || pdf_page <= 0) {
                usage(argv[0]);
                return 1;
            }
            has_pdf_page = 1;
            i++;
            continue;
        }
        if (!path) {
            path = argv[i];
            continue;
        }
        if (maxw == 0 && parse_int(argv[i], &maxw)) {
            continue;
        }
        if (maxh == 0 && parse_int(argv[i], &maxh)) {
            continue;
        }
        usage(argv[0]);
        return 1;
    }

    if (!path) {
        usage(argv[0]);
        return 1;
    }

    if (fit) {
        apply_fit(&maxw, &maxh);
    }

    char err[128];
    if (is_gif_file(path)) {
        cupidimage_animation anim;
        if (!cupidimage_load_gif_animation_file(path, &anim, err, sizeof(err))) {
            fprintf(stderr, "load error: %s\n", err);
            return 1;
        }
        if (anim.frame_count == 0) {
            fprintf(stderr, "load error: missing GIF frame\n");
            cupidimage_free_animation(&anim);
            return 1;
        }
        if (anim.frame_count == 1) {
            if (!cupidimage_render_ansi(&anim.frames[0], stdout, maxw, maxh)) {
                fprintf(stderr, "render error\n");
                cupidimage_free_animation(&anim);
                return 1;
            }
            cupidimage_free_animation(&anim);
            return 0;
        }

        fputs("\x1b[?25l\x1b[2J", stdout);
        uint32_t loops = anim.loop_count;
        if (loops == 0) {
            loops = 0xFFFFFFFFu;
        }
        for (uint32_t loop = 0; loop < loops; loop++) {
            for (uint32_t i = 0; i < anim.frame_count; i++) {
                fputs("\x1b[H", stdout);
                if (!cupidimage_render_ansi(&anim.frames[i], stdout, maxw, maxh)) {
                    fprintf(stderr, "render error\n");
                    cupidimage_free_animation(&anim);
                    fputs("\x1b[?25h", stdout);
                    return 1;
                }
                fflush(stdout);
                sleep_ms(anim.delays[i]);
            }
        }
        fputs("\x1b[?25h", stdout);
        cupidimage_free_animation(&anim);
        return 0;
    }

    if (!has_svg_time && path_has_svg_extension(path)) {
        int anim_res = render_svg_animation(path, maxw, maxh, err, sizeof(err));
        if (anim_res < 0) {
            fprintf(stderr, "load error: %s\n", err);
            return 1;
        }
        if (anim_res > 0) {
            return 0;
        }
    }

    if (path_has_pdf_extension(path)) {
        int page_count = 0;
        if (!cupidimage_get_pdf_page_count_file(path, &page_count, err, sizeof(err))) {
            fprintf(stderr, "load error: %s\n", err);
            return 1;
        }
        if (page_count <= 0) {
            fprintf(stderr, "load error: pdf has no pages\n");
            return 1;
        }

        int start_page = 0;
        int end_page = page_count - 1;
        if (has_pdf_page) {
            start_page = pdf_page - 1;
            end_page = start_page;
            if (start_page < 0 || start_page >= page_count) {
                fprintf(stderr, "load error: pdf page out of range (1-%d)\n", page_count);
                return 1;
            }
        }

        for (int page = start_page; page <= end_page; page++) {
            cupidimage_image img;
            if (!cupidimage_load_pdf_page_file(path, &img, page, err, sizeof(err))) {
                fprintf(stderr, "load error: %s\n", err);
                return 1;
            }

            if (page_count > 1 && !has_pdf_page) {
                fprintf(stderr, "[pdf page %d/%d]\n", page + 1, page_count);
            }
            if (!cupidimage_render_ansi(&img, stdout, maxw, maxh)) {
                fprintf(stderr, "render error\n");
                cupidimage_free(&img);
                return 1;
            }
            cupidimage_free(&img);

            if (page < end_page) {
                fputc('\n', stdout);
            }
        }
        return 0;
    }

    if (has_pdf_page) {
        fprintf(stderr, "load error: --pdf-page is only valid for PDF files\n");
        return 1;
    }

    cupidimage_image img;
    int loaded = 0;
    if (has_svg_time && path_has_svg_extension(path)) {
        cupidimage_svg_options opts;
        opts.width = 0;
        opts.height = 0;
        opts.scale = 1.0f;
        opts.dpi = 96.0f;
        opts.animation_time = svg_time;
        opts.supersampling = 2;
        opts.background_alpha = 0;
        loaded = cupidimage_load_svg_file_with_options(path, &img, &opts, err, sizeof(err));
    } else {
        loaded = cupidimage_load_image_file(path, &img, err, sizeof(err));
    }
    if (!loaded) {
        fprintf(stderr, "load error: %s\n", err);
        return 1;
    }

    if (!cupidimage_render_ansi(&img, stdout, maxw, maxh)) {
        fprintf(stderr, "render error\n");
        cupidimage_free(&img);
        return 1;
    }

    cupidimage_free(&img);
    return 0;
}
