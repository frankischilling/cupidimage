#include "cupidimage.h"
#include "cupidimage_internal.h"

#include <ctype.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_err(char *err, size_t errcap, const char *msg) {
    if (err && errcap) {
        snprintf(err, errcap, "%s", msg);
    }
}

typedef struct {
    int obj_num;
    int gen_num;
    size_t offset;
} pdf_xref_entry;

typedef struct {
    pdf_xref_entry *entries;
    int count;
    int root_obj;
    int info_obj;
} pdf_xref;

typedef struct {
    const unsigned char *data;
    size_t len;
    unsigned char *owned;
} pdf_object_view;

static int pdf_inflate_auto(const unsigned char *data, size_t size,
                            unsigned char **out_data, size_t *out_len);
static int extract_dict_int(const unsigned char *data, size_t start, size_t end,
                            const char *key, int *value);
static int buffer_contains(const unsigned char *data, size_t start, size_t end, const char *needle);
static int extract_stream(const unsigned char *data, size_t obj_start, size_t obj_end,
                          const unsigned char **stream_data, size_t *stream_len,
                          size_t *stream_keyword_pos);

static int find_eof_marker(const unsigned char *data, size_t size, size_t *eof_pos) {
    if (size < 6) return 0;
    for (size_t i = size - 6; i > 0; i--) {
        if (memcmp(&data[i], "%%EOF", 5) == 0) {
            *eof_pos = i;
            return 1;
        }
    }
    return 0;
}

static int find_startxref(const unsigned char *data, size_t eof_pos, size_t *xref_offset) {
    if (eof_pos < 20) return 0;
    const char *search = "startxref";
    for (size_t i = eof_pos - 20; i < eof_pos; i++) {
        if (memcmp(&data[i], search, 9) == 0) {
            size_t j = i + 9;
            while (j < eof_pos && (data[j] == ' ' || data[j] == '\n' || data[j] == '\r')) j++;
            *xref_offset = 0;
            while (j < eof_pos && data[j] >= '0' && data[j] <= '9') {
                *xref_offset = *xref_offset * 10 + (size_t)(data[j] - '0');
                j++;
            }
            return 1;
        }
    }
    return 0;
}

static int pdf_check_header(const unsigned char *data, size_t size) {
    if (size < 8) return 0;
    if (memcmp(data, "%PDF-", 5) != 0) return 0;
    return 1;
}

static int parse_xref_table(const unsigned char *data, size_t size, size_t xref_pos,
                            pdf_xref *xref, char *err, size_t errcap) {
    if (xref_pos + 4 > size) {
        set_err(err, errcap, "pdf: xref offset out of bounds");
        return 0;
    }

    if (memcmp(&data[xref_pos], "xref", 4) != 0) {
        xref->entries = (pdf_xref_entry *)calloc(20, sizeof(pdf_xref_entry));
        if (!xref->entries) {
            set_err(err, errcap, "pdf: out of memory");
            return 0;
        }
        xref->count = 20;
        for (int i = 0; i < 20; i++) {
            xref->entries[i].obj_num = i;
            xref->entries[i].gen_num = 0;
            xref->entries[i].offset = 0;
        }
        return 1;
    }

    size_t pos = xref_pos + 4;
    while (pos < size && (data[pos] == ' ' || data[pos] == '\n' || data[pos] == '\r')) pos++;

    int first_obj = 0, count = 0;
    while (pos < size && data[pos] >= '0' && data[pos] <= '9') {
        first_obj = first_obj * 10 + (data[pos] - '0');
        pos++;
    }
    while (pos < size && (data[pos] == ' ' || data[pos] == '\n' || data[pos] == '\r')) pos++;
    while (pos < size && data[pos] >= '0' && data[pos] <= '9') {
        count = count * 10 + (data[pos] - '0');
        pos++;
    }
    while (pos < size && (data[pos] == ' ' || data[pos] == '\n' || data[pos] == '\r')) pos++;

    if (count <= 0 || count > 100000) {
        set_err(err, errcap, "pdf: invalid xref count");
        return 0;
    }

    xref->entries = (pdf_xref_entry *)calloc((size_t)count, sizeof(pdf_xref_entry));
    if (!xref->entries) {
        set_err(err, errcap, "pdf: out of memory");
        return 0;
    }
    xref->count = count;

    for (int i = 0; i < count; i++) {
        size_t offset = 0;
        int gen = 0;
        char inuse = 'f';

        for (int d = 0; d < 10 && pos < size; d++) {
            if (data[pos] >= '0' && data[pos] <= '9') {
                offset = offset * 10 + (size_t)(data[pos] - '0');
            }
            pos++;
        }
        while (pos < size && data[pos] == ' ') pos++;

        for (int d = 0; d < 5 && pos < size; d++) {
            if (data[pos] >= '0' && data[pos] <= '9') {
                gen = gen * 10 + (data[pos] - '0');
            }
            pos++;
        }
        while (pos < size && data[pos] == ' ') pos++;

        if (pos < size) {
            inuse = (char)data[pos++];
        }
        while (pos < size && (data[pos] == ' ' || data[pos] == '\n' || data[pos] == '\r')) pos++;

        xref->entries[i].obj_num = first_obj + i;
        xref->entries[i].gen_num = gen;
        xref->entries[i].offset = (inuse == 'n') ? offset : 0;
    }

    return 1;
}

static int parse_trailer(const unsigned char *data, size_t size, size_t xref_pos,
                         pdf_xref *xref, char *err, size_t errcap) {
    size_t pos = xref_pos;
    size_t trailer_pos = 0;
    size_t search_limit = xref_pos + 10000;
    if (search_limit > size) search_limit = size;

    while (pos < search_limit) {
        if (memcmp(&data[pos], "trailer", 7) == 0) {
            trailer_pos = pos;
            break;
        }
        pos++;
    }

    if (trailer_pos == 0) {
        pos = xref_pos;
        while (pos < search_limit && data[pos] != '<') pos++;
        if (pos + 1 >= search_limit || data[pos + 1] != '<') {
            set_err(err, errcap, "pdf: trailer dictionary not found");
            return 0;
        }
        pos += 2;
    } else {
        pos = trailer_pos + 7;
        while (pos < size && data[pos] != '<') pos++;
        if (pos + 1 >= size || data[pos + 1] != '<') {
            set_err(err, errcap, "pdf: trailer dictionary not found");
            return 0;
        }
        pos += 2;
    }

    xref->root_obj = 1;
    while (pos < size - 1) {
        if (data[pos] == '>' && data[pos + 1] == '>') break;

        if (memcmp(&data[pos], "/Root", 5) == 0) {
            pos += 5;
            while (pos < size && (data[pos] == ' ' || data[pos] == '\n' || data[pos] == '\r')) pos++;
            int obj_num = 0;
            while (pos < size && data[pos] >= '0' && data[pos] <= '9') {
                obj_num = obj_num * 10 + (data[pos] - '0');
                pos++;
            }
            xref->root_obj = obj_num;
        }
        pos++;
    }

    return 1;
}

static void pdf_object_view_release(pdf_object_view *view) {
    if (!view) return;
    free(view->owned);
    view->owned = NULL;
    view->data = NULL;
    view->len = 0;
}

static int parse_obj_header_at(const unsigned char *data, size_t size, size_t pos,
                               int *obj_num, size_t *header_end) {
    size_t p = pos;
    if (p >= size || data[p] < '0' || data[p] > '9') {
        return 0;
    }
    if (p > 0 && data[p - 1] != '\n' && data[p - 1] != '\r') {
        return 0;
    }
    int num = 0;
    while (p < size && data[p] >= '0' && data[p] <= '9') {
        num = num * 10 + (data[p] - '0');
        p++;
    }
    if (p >= size || data[p] != ' ') {
        return 0;
    }
    while (p < size && data[p] == ' ') p++;
    if (p >= size || data[p] < '0' || data[p] > '9') {
        return 0;
    }
    while (p < size && data[p] >= '0' && data[p] <= '9') p++;
    if (p >= size || data[p] != ' ') {
        return 0;
    }
    while (p < size && data[p] == ' ') p++;
    if (p + 3 > size || memcmp(&data[p], "obj", 3) != 0) {
        return 0;
    }
    p += 3;
    if (p < size && !isspace((unsigned char)data[p])) {
        return 0;
    }
    *obj_num = num;
    *header_end = p;
    return 1;
}

static int find_endobj(const unsigned char *data, size_t size, size_t from, size_t *endobj_pos) {
    size_t p = from;
    while (p + 6 <= size) {
        if (memcmp(&data[p], "stream", 6) == 0) {
            int preceded_ok = (p == from) || isspace((unsigned char)data[p - 1]) || data[p - 1] == '>';
            if (preceded_ok) {
                size_t stream_start = p + 6;
                if (stream_start < size && data[stream_start] == '\r') stream_start++;
                if (stream_start < size && data[stream_start] == '\n') stream_start++;

                int declared_len = 0;
                if (extract_dict_int(data, from, p, "/Length", &declared_len) &&
                    declared_len > 0 && stream_start + (size_t)declared_len <= size) {
                    p = stream_start + (size_t)declared_len;
                    while (p < size && (data[p] == ' ' || data[p] == '\t' || data[p] == '\r' || data[p] == '\n')) p++;
                    if (p + 9 <= size && memcmp(&data[p], "endstream", 9) == 0) {
                        p += 9;
                    }
                    continue;
                }

                size_t q = stream_start;
                while (q + 9 <= size && memcmp(&data[q], "endstream", 9) != 0) q++;
                if (q + 9 <= size) {
                    p = q + 9;
                    continue;
                }
                return 0;
            }
        }
        if (memcmp(&data[p], "endobj", 6) == 0) {
            *endobj_pos = p;
            return 1;
        }
        p++;
    }
    return 0;
}

static int extract_dict_int(const unsigned char *data, size_t start, size_t end,
                            const char *key, int *value) {
    size_t key_len = strlen(key);
    if (end <= start || end - start <= key_len) {
        return 0;
    }
    for (size_t pos = start; pos + key_len <= end; pos++) {
        if (memcmp(&data[pos], key, key_len) != 0) {
            continue;
        }
        pos += key_len;
        while (pos < end && (data[pos] == ' ' || data[pos] == '\n' || data[pos] == '\r' || data[pos] == '\t')) pos++;
        int sign = 1;
        int v = 0;
        if (pos < end && data[pos] == '-') {
            sign = -1;
            pos++;
        }
        int got = 0;
        while (pos < end && data[pos] >= '0' && data[pos] <= '9') {
            got = 1;
            v = v * 10 + (data[pos] - '0');
            pos++;
        }
        if (got) {
            size_t look = pos;
            while (look < end && (data[look] == ' ' || data[look] == '\n' || data[look] == '\r' || data[look] == '\t')) {
                look++;
            }
            if (look < end && data[look] >= '0' && data[look] <= '9') {
                while (look < end && data[look] >= '0' && data[look] <= '9') {
                    look++;
                }
                while (look < end && (data[look] == ' ' || data[look] == '\n' || data[look] == '\r' || data[look] == '\t')) {
                    look++;
                }
                if (look < end && data[look] == 'R') {
                    continue;
                }
            }
            *value = v * sign;
            return 1;
        }
    }
    return 0;
}

static int parse_int_token(const unsigned char *data, size_t size, size_t *pos, int *out) {
    while (*pos < size && (data[*pos] == ' ' || data[*pos] == '\n' || data[*pos] == '\r' || data[*pos] == '\t')) {
        (*pos)++;
    }
    if (*pos >= size || data[*pos] < '0' || data[*pos] > '9') {
        return 0;
    }
    int v = 0;
    while (*pos < size && data[*pos] >= '0' && data[*pos] <= '9') {
        v = v * 10 + (data[*pos] - '0');
        (*pos)++;
    }
    *out = v;
    return 1;
}

static int extract_stream_from_object(const unsigned char *obj_data, size_t obj_len,
                                      const unsigned char **stream_data, size_t *stream_len,
                                      size_t *stream_keyword_pos) {
    size_t pos = 0;
    while (pos + 6 <= obj_len && memcmp(&obj_data[pos], "stream", 6) != 0) pos++;
    if (pos + 6 > obj_len) return 0;

    if (stream_keyword_pos) {
        *stream_keyword_pos = pos;
    }

    pos += 6;
    if (pos < obj_len && obj_data[pos] == '\r') pos++;
    if (pos < obj_len && obj_data[pos] == '\n') pos++;

    *stream_data = &obj_data[pos];

    int declared_len = 0;
    if (extract_dict_int(obj_data, 0, pos, "/Length", &declared_len) &&
        declared_len > 0 && (size_t)declared_len <= obj_len - pos) {
        *stream_len = (size_t)declared_len;
        return 1;
    }

    size_t end_pos = pos;
    while (end_pos + 9 <= obj_len && memcmp(&obj_data[end_pos], "endstream", 9) != 0) end_pos++;
    if (end_pos + 9 > obj_len) return 0;

    *stream_len = end_pos - pos;
    return 1;
}

static int extract_object_from_objstm(const unsigned char *data, size_t size, int target_obj,
                                      pdf_object_view *view) {
    for (size_t pos = 0; pos < size; pos++) {
        int obj_num = 0;
        size_t obj_start = 0;
        if (!parse_obj_header_at(data, size, pos, &obj_num, &obj_start)) {
            continue;
        }

        size_t obj_end = 0;
        if (!find_endobj(data, size, obj_start, &obj_end)) {
            continue;
        }

        if (!buffer_contains(data, obj_start, obj_end, "/Type") ||
            !buffer_contains(data, obj_start, obj_end, "/ObjStm")) {
            pos = obj_end;
            continue;
        }

        int n_objects = 0;
        int first_offset = 0;
        if (!extract_dict_int(data, obj_start, obj_end, "/N", &n_objects) ||
            !extract_dict_int(data, obj_start, obj_end, "/First", &first_offset) ||
            n_objects <= 0 || n_objects > 200000 || first_offset < 0) {
            pos = obj_end;
            continue;
        }

        const unsigned char *stream_data = NULL;
        size_t stream_len = 0;
        if (!extract_stream(data, obj_start, obj_end, &stream_data, &stream_len, NULL)) {
            pos = obj_end;
            continue;
        }

        unsigned char *decoded = NULL;
        size_t decoded_len = 0;
        if (buffer_contains(data, obj_start, obj_end, "/FlateDecode")) {
            if (!pdf_inflate_auto(stream_data, stream_len, &decoded, &decoded_len)) {
                pos = obj_end;
                continue;
            }
        } else {
            decoded = (unsigned char *)malloc(stream_len);
            if (!decoded) {
                return 0;
            }
            memcpy(decoded, stream_data, stream_len);
            decoded_len = stream_len;
        }

        if ((size_t)first_offset >= decoded_len) {
            free(decoded);
            pos = obj_end;
            continue;
        }

        int *ids = (int *)malloc((size_t)n_objects * sizeof(int));
        int *offs = (int *)malloc((size_t)n_objects * sizeof(int));
        if (!ids || !offs) {
            free(ids);
            free(offs);
            free(decoded);
            return 0;
        }

        size_t hp = 0;
        int ok_hdr = 1;
        for (int i = 0; i < n_objects; i++) {
            if (!parse_int_token(decoded, decoded_len, &hp, &ids[i]) ||
                !parse_int_token(decoded, decoded_len, &hp, &offs[i])) {
                ok_hdr = 0;
                break;
            }
        }
        if (!ok_hdr) {
            free(ids);
            free(offs);
            free(decoded);
            pos = obj_end;
            continue;
        }

        for (int i = 0; i < n_objects; i++) {
            if (ids[i] != target_obj) {
                continue;
            }
            size_t s = (size_t)first_offset + (size_t)offs[i];
            size_t e = decoded_len;
            if (i + 1 < n_objects) {
                e = (size_t)first_offset + (size_t)offs[i + 1];
            }
            if (s >= decoded_len || e > decoded_len || e <= s) {
                continue;
            }
            unsigned char *obj_copy = (unsigned char *)malloc(e - s);
            if (!obj_copy) {
                free(ids);
                free(offs);
                free(decoded);
                return 0;
            }
            memcpy(obj_copy, decoded + s, e - s);
            free(ids);
            free(offs);
            free(decoded);
            view->data = obj_copy;
            view->len = e - s;
            view->owned = obj_copy;
            return 1;
        }

        free(ids);
        free(offs);
        free(decoded);
        pos = obj_end;
    }

    return 0;
}

static int get_object_content(const unsigned char *data, size_t size, int obj_num,
                              pdf_object_view *view) {
    view->data = NULL;
    view->len = 0;
    view->owned = NULL;

    for (size_t pos = 0; pos < size; pos++) {
        int num = 0;
        size_t obj_start = 0;
        if (!parse_obj_header_at(data, size, pos, &num, &obj_start)) {
            continue;
        }
        size_t obj_end = 0;
        if (!find_endobj(data, size, obj_start, &obj_end)) {
            continue;
        }
        if (num == obj_num) {
            view->data = &data[obj_start];
            view->len = obj_end - obj_start;
            view->owned = NULL;
            return 1;
        }
        pos = obj_end;
    }

    return extract_object_from_objstm(data, size, obj_num, view);
}

static int extract_dict_ref(const unsigned char *data, size_t start, size_t end,
                            const char *key, int *obj_num) {
    size_t key_len = strlen(key);
    if (end <= start || end - start <= key_len) {
        return 0;
    }
    for (size_t pos = start; pos < end - key_len; pos++) {
        if (memcmp(&data[pos], key, key_len) == 0) {
            pos += key_len;
            while (pos < end && (data[pos] == ' ' || data[pos] == '\n' || data[pos] == '\r' || data[pos] == '\t')) pos++;
            if (pos < end && data[pos] == '[') {
                pos++;
                while (pos < end && (data[pos] == ' ' || data[pos] == '\n' || data[pos] == '\r' || data[pos] == '\t')) pos++;
            }
            *obj_num = 0;
            while (pos < end && data[pos] >= '0' && data[pos] <= '9') {
                *obj_num = *obj_num * 10 + (data[pos] - '0');
                pos++;
            }
            return *obj_num > 0;
        }
    }
    return 0;
}

static int extract_dict_name(const unsigned char *data, size_t start, size_t end,
                             const char *key, char *out, size_t out_cap) {
    size_t key_len = strlen(key);
    if (!out || out_cap == 0 || end <= start || end - start <= key_len) {
        return 0;
    }
    for (size_t pos = start; pos + key_len < end; pos++) {
        if (memcmp(&data[pos], key, key_len) != 0) {
            continue;
        }
        pos += key_len;
        while (pos < end && (data[pos] == ' ' || data[pos] == '\n' || data[pos] == '\r' || data[pos] == '\t')) pos++;
        if (pos >= end || data[pos] != '/') {
            return 0;
        }
        pos++;
        size_t n = 0;
        while (pos < end &&
               !isspace((unsigned char)data[pos]) &&
               data[pos] != '/' && data[pos] != '<' && data[pos] != '>' &&
               data[pos] != '[' && data[pos] != ']' && data[pos] != '(' && data[pos] != ')') {
            if (n + 1 < out_cap) {
                out[n++] = (char)data[pos];
            }
            pos++;
        }
        out[n] = '\0';
        return n > 0;
    }
    return 0;
}

static int extract_dict_refs(const unsigned char *data, size_t start, size_t end, const char *key,
                             int *objs, int max_objs, int *out_count) {
    size_t key_len = strlen(key);
    if (!objs || max_objs <= 0 || !out_count || end <= start || end - start <= key_len) {
        return 0;
    }
    *out_count = 0;

    for (size_t pos = start; pos + key_len < end; pos++) {
        if (memcmp(&data[pos], key, key_len) != 0) {
            continue;
        }
        pos += key_len;
        while (pos < end && (data[pos] == ' ' || data[pos] == '\n' || data[pos] == '\r' || data[pos] == '\t')) pos++;

        int in_array = 0;
        if (pos < end && data[pos] == '[') {
            in_array = 1;
            pos++;
        }

        while (pos < end) {
            while (pos < end && (data[pos] == ' ' || data[pos] == '\n' || data[pos] == '\r' || data[pos] == '\t')) pos++;
            if (pos >= end) {
                break;
            }
            if (in_array && data[pos] == ']') {
                break;
            }
            if (data[pos] < '0' || data[pos] > '9') {
                if (!in_array) {
                    break;
                }
                pos++;
                continue;
            }

            int obj = 0;
            while (pos < end && data[pos] >= '0' && data[pos] <= '9') {
                obj = obj * 10 + (data[pos] - '0');
                pos++;
            }
            while (pos < end && (data[pos] == ' ' || data[pos] == '\n' || data[pos] == '\r' || data[pos] == '\t')) pos++;

            int gen = 0;
            int have_gen = 0;
            while (pos < end && data[pos] >= '0' && data[pos] <= '9') {
                have_gen = 1;
                gen = gen * 10 + (data[pos] - '0');
                pos++;
            }
            (void)gen;
            while (pos < end && (data[pos] == ' ' || data[pos] == '\n' || data[pos] == '\r' || data[pos] == '\t')) pos++;
            if (have_gen && pos < end && data[pos] == 'R') {
                pos++;
            }

            if (obj > 0 && *out_count < max_objs) {
                objs[*out_count] = obj;
                (*out_count)++;
            }

            if (!in_array) {
                break;
            }
        }

        return *out_count > 0;
    }

    return 0;
}

static int get_pages_obj(const unsigned char *data, size_t size, pdf_xref *xref,
                         int *pages_obj, char *err, size_t errcap) {
    (void)xref;
    pdf_object_view catalog = {0};
    if (!get_object_content(data, size, xref->root_obj, &catalog)) {
        set_err(err, errcap, "pdf: catalog object not found");
        return 0;
    }

    if (!extract_dict_ref(catalog.data, 0, catalog.len, "/Pages", pages_obj)) {
        pdf_object_view_release(&catalog);
        set_err(err, errcap, "pdf: /Pages not found in catalog");
        return 0;
    }

    pdf_object_view_release(&catalog);
    return 1;
}

static int get_pages_count_from_obj(const unsigned char *data, size_t size, int pages_obj,
                                    int *count, char *err, size_t errcap) {
    if (!count) {
        set_err(err, errcap, "pdf: invalid pages count output");
        return 0;
    }
    *count = 0;

    pdf_object_view pages = {0};
    if (!get_object_content(data, size, pages_obj, &pages)) {
        set_err(err, errcap, "pdf: pages object not found");
        return 0;
    }

    int c = 0;
    if (extract_dict_int(pages.data, 0, pages.len, "/Count", &c) && c > 0) {
        *count = c;
        pdf_object_view_release(&pages);
        return 1;
    }

    pdf_object_view_release(&pages);
    set_err(err, errcap, "pdf: /Count not found in pages");
    return 0;
}

static int find_page_in_tree(const unsigned char *data, size_t size, int obj_num,
                             int target_index, int *scan_index, int *page_obj,
                             int depth, char *err, size_t errcap) {
    if (depth > 64) {
        set_err(err, errcap, "pdf: pages tree too deep");
        return 0;
    }

    pdf_object_view node = {0};
    if (!get_object_content(data, size, obj_num, &node)) {
        set_err(err, errcap, "pdf: pages tree object not found");
        return 0;
    }

    char type_name[32];
    type_name[0] = '\0';
    (void)extract_dict_name(node.data, 0, node.len, "/Type", type_name, sizeof(type_name));

    if (strcmp(type_name, "Page") == 0) {
        if (*scan_index == target_index) {
            *page_obj = obj_num;
            pdf_object_view_release(&node);
            return 1;
        }
        (*scan_index)++;
        pdf_object_view_release(&node);
        return 1;
    }

    int kids[256];
    int kid_count = 0;
    if (!extract_dict_refs(node.data, 0, node.len, "/Kids",
                           kids, (int)(sizeof(kids) / sizeof(kids[0])),
                           &kid_count) || kid_count <= 0) {
        pdf_object_view_release(&node);
        set_err(err, errcap, "pdf: /Kids not found");
        return 0;
    }

    for (int i = 0; i < kid_count; i++) {
        if (!find_page_in_tree(data, size, kids[i], target_index, scan_index, page_obj,
                               depth + 1, err, errcap)) {
            pdf_object_view_release(&node);
            return 0;
        }
        if (*page_obj > 0) {
            pdf_object_view_release(&node);
            return 1;
        }
    }

    pdf_object_view_release(&node);
    return 1;
}

static int get_page_obj_by_index(const unsigned char *data, size_t size, pdf_xref *xref,
                                 int pages_obj, int page_index, int *page_obj,
                                 char *err, size_t errcap) {
    (void)xref;
    if (page_index < 0) {
        set_err(err, errcap, "pdf: invalid page index");
        return 0;
    }

    int page_count = 0;
    if (!get_pages_count_from_obj(data, size, pages_obj, &page_count, err, errcap)) {
        return 0;
    }
    if (page_index >= page_count) {
        set_err(err, errcap, "pdf: page index out of range");
        return 0;
    }

    int scan_index = 0;
    *page_obj = 0;
    if (!find_page_in_tree(data, size, pages_obj, page_index, &scan_index, page_obj, 0, err, errcap)) {
        return 0;
    }

    if (*page_obj <= 0) {
        set_err(err, errcap, "pdf: page not found");
        return 0;
    }

    return 1;
}

static int parse_pdf_number(const unsigned char *data, size_t end, size_t *pos, double *out) {
    size_t p = *pos;
    int saw_digit = 0;
    int saw_dot = 0;

    if (p < end && (data[p] == '+' || data[p] == '-')) {
        p++;
    }
    while (p < end) {
        if (data[p] >= '0' && data[p] <= '9') {
            saw_digit = 1;
            p++;
            continue;
        }
        if (data[p] == '.' && !saw_dot) {
            saw_dot = 1;
            p++;
            continue;
        }
        break;
    }
    if (!saw_digit) {
        return 0;
    }

    char tmp[64];
    size_t n = p - *pos;
    if (n >= sizeof(tmp)) {
        n = sizeof(tmp) - 1;
    }
    memcpy(tmp, &data[*pos], n);
    tmp[n] = '\0';
    *out = strtod(tmp, NULL);
    *pos = p;
    return 1;
}

static int get_page_mediabox(const unsigned char *data, size_t size, pdf_xref *xref,
                             int page_obj, int *width, int *height,
                             char *err, size_t errcap) {
    (void)xref;
    pdf_object_view page = {0};
    if (!get_object_content(data, size, page_obj, &page)) {
        set_err(err, errcap, "pdf: page object not found");
        return 0;
    }

    size_t pos = 0;
    while (pos + 9 <= page.len && memcmp(&page.data[pos], "/MediaBox", 9) != 0) pos++;
    if (pos + 9 > page.len) {
        pdf_object_view_release(&page);
        *width = 612;
        *height = 792;
        return 1;
    }
    pos += 9;

    while (pos < page.len && page.data[pos] != '[') pos++;
    if (pos >= page.len) {
        pdf_object_view_release(&page);
        *width = 612;
        *height = 792;
        return 1;
    }
    pos++;

    double vals[4] = {0.0, 0.0, 612.0, 792.0};
    for (int i = 0; i < 4; i++) {
        while (pos < page.len && isspace((unsigned char)page.data[pos])) pos++;
        if (!parse_pdf_number(page.data, page.len, &pos, &vals[i])) {
            pdf_object_view_release(&page);
            *width = 612;
            *height = 792;
            return 1;
        }
    }

    int w = (int)(vals[2] - vals[0]);
    int h = (int)(vals[3] - vals[1]);
    if (w <= 0 || h <= 0) {
        w = 612;
        h = 792;
    }
    *width = w;
    *height = h;
    pdf_object_view_release(&page);
    return 1;
}

static int extract_stream(const unsigned char *data, size_t obj_start, size_t obj_end,
                          const unsigned char **stream_data, size_t *stream_len,
                          size_t *stream_keyword_pos) {
    size_t pos = obj_start;
    while (pos + 6 <= obj_end && memcmp(&data[pos], "stream", 6) != 0) pos++;
    if (pos + 6 > obj_end) return 0;

    if (stream_keyword_pos) {
        *stream_keyword_pos = pos;
    }

    pos += 6;
    if (pos < obj_end && data[pos] == '\r') pos++;
    if (pos < obj_end && data[pos] == '\n') pos++;

    *stream_data = &data[pos];

    int declared_len = 0;
    if (extract_dict_int(data, obj_start, pos, "/Length", &declared_len) &&
        declared_len > 0 && (size_t)declared_len <= obj_end - pos) {
        *stream_len = (size_t)declared_len;
        return 1;
    }

    size_t end_pos = pos;
    while (end_pos + 9 <= obj_end && memcmp(&data[end_pos], "endstream", 9) != 0) end_pos++;
    if (end_pos + 9 > obj_end) return 0;

    *stream_len = end_pos - pos;
    return 1;
}

static int buffer_contains(const unsigned char *data, size_t start, size_t end, const char *needle) {
    size_t needle_len = strlen(needle);
    if (end <= start || end - start < needle_len) {
        return 0;
    }
    for (size_t i = start; i + needle_len <= end; i++) {
        if (memcmp(&data[i], needle, needle_len) == 0) {
            return 1;
        }
    }
    return 0;
}

static int get_page_content_stream(const unsigned char *data, size_t size, pdf_xref *xref,
                                   int page_obj, unsigned char **stream_data,
                                   size_t *stream_len, int *is_flate,
                                   char *err, size_t errcap) {
    (void)xref;
    pdf_object_view page = {0};
    if (!get_object_content(data, size, page_obj, &page)) {
        set_err(err, errcap, "pdf: page object not found");
        return 0;
    }

    int content_objs[128];
    int content_count = 0;
    if (!extract_dict_refs(page.data, 0, page.len, "/Contents",
                           content_objs, (int)(sizeof(content_objs) / sizeof(content_objs[0])),
                           &content_count)) {
        pdf_object_view_release(&page);
        *stream_data = NULL;
        *stream_len = 0;
        *is_flate = 0;
        return 1;
    }
    pdf_object_view_release(&page);

    unsigned char *combined = NULL;
    size_t combined_len = 0;

    for (int i = 0; i < content_count; i++) {
        pdf_object_view contents = {0};
        if (!get_object_content(data, size, content_objs[i], &contents)) {
            free(combined);
            set_err(err, errcap, "pdf: contents object not found");
            return 0;
        }

        size_t stream_kw_pos = 0;
        const unsigned char *stream_ptr = NULL;
        size_t raw_len = 0;
        if (!extract_stream_from_object(contents.data, contents.len, &stream_ptr, &raw_len, &stream_kw_pos)) {
            pdf_object_view_release(&contents);
            continue;
        }

        int stream_is_flate = 0;
        if (buffer_contains(contents.data, 0, stream_kw_pos, "/FlateDecode") ||
            buffer_contains(contents.data, 0, stream_kw_pos, "/Fl")) {
            stream_is_flate = 1;
        }

        const unsigned char *decoded_ptr = stream_ptr;
        size_t decoded_len = raw_len;
        unsigned char *decoded_owned = NULL;
        if (stream_is_flate) {
            if (!pdf_inflate_auto(stream_ptr, raw_len, &decoded_owned, &decoded_len)) {
                pdf_object_view_release(&contents);
                free(combined);
                set_err(err, errcap, "pdf: failed to decode Flate stream");
                return 0;
            }
            decoded_ptr = decoded_owned;
        }

        if (decoded_len > 0) {
            size_t extra = decoded_len + ((combined_len > 0) ? 1u : 0u);
            unsigned char *tmp = (unsigned char *)realloc(combined, combined_len + extra);
            if (!tmp) {
                free(decoded_owned);
                pdf_object_view_release(&contents);
                free(combined);
                set_err(err, errcap, "pdf: out of memory");
                return 0;
            }
            combined = tmp;
            if (combined_len > 0) {
                combined[combined_len++] = '\n';
            }
            memcpy(combined + combined_len, decoded_ptr, decoded_len);
            combined_len += decoded_len;
        }

        free(decoded_owned);
        pdf_object_view_release(&contents);
    }

    *stream_data = combined;
    *stream_len = combined_len;
    *is_flate = 0;

    return 1;
}

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t pos;
    uint32_t bitbuf;
    int bitcount;
} pdf_bitstream;

static int pdf_bs_fill(pdf_bitstream *bs, int need) {
    while (bs->bitcount < need && bs->pos < bs->size) {
        bs->bitbuf |= (uint32_t)bs->data[bs->pos++] << bs->bitcount;
        bs->bitcount += 8;
    }
    return bs->bitcount >= need;
}

static uint32_t pdf_bs_read(pdf_bitstream *bs, int bits) {
    if (!pdf_bs_fill(bs, bits)) {
        return 0;
    }
    uint32_t val = bs->bitbuf & ((1u << bits) - 1u);
    bs->bitbuf >>= bits;
    bs->bitcount -= bits;
    return val;
}

static void pdf_bs_align(pdf_bitstream *bs) {
    int drop = bs->bitcount & 7;
    if (drop) {
        bs->bitbuf >>= drop;
        bs->bitcount -= drop;
    }
}

static unsigned pdf_reverse_bits(unsigned v, int bits) {
    unsigned r = 0;
    for (int i = 0; i < bits; i++) {
        r = (r << 1) | (v & 1u);
        v >>= 1;
    }
    return r;
}

typedef struct {
    int maxbits;
    uint16_t *table;
} pdf_huff_table;

static void pdf_huff_free(pdf_huff_table *ht) {
    free(ht->table);
    ht->table = NULL;
    ht->maxbits = 0;
}

static int pdf_huff_build(pdf_huff_table *ht, const uint8_t *lengths, int num, int maxbits) {
    int count[16] = {0};
    int maxlen = 0;
    for (int i = 0; i < num; i++) {
        if (lengths[i] > maxbits) {
            return 0;
        }
        if (lengths[i]) {
            count[lengths[i]]++;
            if (lengths[i] > maxlen) {
                maxlen = lengths[i];
            }
        }
    }
    if (maxlen == 0) {
        return 0;
    }

    int next_code[16];
    int code = 0;
    count[0] = 0;
    for (int bits = 1; bits <= maxbits; bits++) {
        code = (code + count[bits - 1]) << 1;
        next_code[bits] = code;
    }

    ht->maxbits = maxlen;
    ht->table = (uint16_t *)malloc(((size_t)1u << maxlen) * sizeof(uint16_t));
    if (!ht->table) {
        return 0;
    }
    size_t table_size = (size_t)1u << maxlen;
    for (size_t i = 0; i < table_size; i++) {
        ht->table[i] = 0xFFFFu;
    }

    for (int sym = 0; sym < num; sym++) {
        int len = lengths[sym];
        if (!len) {
            continue;
        }
        int sym_code = next_code[len]++;
        unsigned rev = pdf_reverse_bits((unsigned)sym_code, len);
        unsigned fill = 1u << (maxlen - len);
        for (unsigned i = 0; i < fill; i++) {
            unsigned idx = (i << len) | rev;
            ht->table[idx] = (uint16_t)((len << 9) | sym);
        }
    }

    return 1;
}

static int pdf_huff_decode(pdf_bitstream *bs, const pdf_huff_table *ht, int *sym) {
    if (!pdf_bs_fill(bs, ht->maxbits)) {
        return 0;
    }
    uint32_t idx = bs->bitbuf & ((1u << ht->maxbits) - 1u);
    uint16_t val = ht->table[idx];
    if (val == 0xFFFFu) {
        return 0;
    }
    int len = val >> 9;
    *sym = val & 0x1FF;
    bs->bitbuf >>= len;
    bs->bitcount -= len;
    return 1;
}

static int pdf_build_fixed_tables(pdf_huff_table *litlen, pdf_huff_table *dist) {
    uint8_t litlen_lengths[288];
    uint8_t dist_lengths[32];

    for (int i = 0; i <= 143; i++) litlen_lengths[i] = 8;
    for (int i = 144; i <= 255; i++) litlen_lengths[i] = 9;
    for (int i = 256; i <= 279; i++) litlen_lengths[i] = 7;
    for (int i = 280; i <= 287; i++) litlen_lengths[i] = 8;
    for (int i = 0; i < 32; i++) dist_lengths[i] = 5;

    if (!pdf_huff_build(litlen, litlen_lengths, 288, 15)) {
        return 0;
    }
    if (!pdf_huff_build(dist, dist_lengths, 32, 15)) {
        pdf_huff_free(litlen);
        return 0;
    }
    return 1;
}

static int pdf_build_dynamic_tables(pdf_bitstream *bs, pdf_huff_table *litlen, pdf_huff_table *dist) {
    int hlit = (int)pdf_bs_read(bs, 5) + 257;
    int hdist = (int)pdf_bs_read(bs, 5) + 1;
    int hclen = (int)pdf_bs_read(bs, 4) + 4;
    if (hlit > 286 || hdist > 30) {
        return 0;
    }

    static const int order[19] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
    };

    uint8_t clen_lengths[19];
    for (int i = 0; i < 19; i++) {
        clen_lengths[i] = 0;
    }
    for (int i = 0; i < hclen; i++) {
        clen_lengths[order[i]] = (uint8_t)pdf_bs_read(bs, 3);
    }

    pdf_huff_table clen_table = {0, NULL};
    if (!pdf_huff_build(&clen_table, clen_lengths, 19, 7)) {
        return 0;
    }

    int total = hlit + hdist;
    uint8_t lengths[316];
    int idx = 0;
    while (idx < total) {
        int sym = 0;
        if (!pdf_huff_decode(bs, &clen_table, &sym)) {
            pdf_huff_free(&clen_table);
            return 0;
        }
        if (sym <= 15) {
            lengths[idx++] = (uint8_t)sym;
        } else if (sym == 16) {
            if (idx == 0) {
                pdf_huff_free(&clen_table);
                return 0;
            }
            int repeat = (int)pdf_bs_read(bs, 2) + 3;
            uint8_t prev = lengths[idx - 1];
            for (int i = 0; i < repeat && idx < total; i++) {
                lengths[idx++] = prev;
            }
        } else if (sym == 17) {
            int repeat = (int)pdf_bs_read(bs, 3) + 3;
            for (int i = 0; i < repeat && idx < total; i++) {
                lengths[idx++] = 0;
            }
        } else if (sym == 18) {
            int repeat = (int)pdf_bs_read(bs, 7) + 11;
            for (int i = 0; i < repeat && idx < total; i++) {
                lengths[idx++] = 0;
            }
        } else {
            pdf_huff_free(&clen_table);
            return 0;
        }
    }

    pdf_huff_free(&clen_table);

    if (!pdf_huff_build(litlen, lengths, hlit, 15)) {
        return 0;
    }
    if (!pdf_huff_build(dist, lengths + hlit, hdist, 15)) {
        pdf_huff_free(litlen);
        return 0;
    }
    return 1;
}

static int pdf_deflate_decode(pdf_bitstream *bs, uint8_t *out, size_t outcap, size_t *outlen) {
    static const int length_base[29] = {
        3, 4, 5, 6, 7, 8, 9, 10,
        11, 13, 15, 17, 19, 23, 27, 31,
        35, 43, 51, 59, 67, 83, 99, 115,
        131, 163, 195, 227, 258
    };
    static const int length_extra[29] = {
        0, 0, 0, 0, 0, 0, 0, 0,
        1, 1, 1, 1, 2, 2, 2, 2,
        3, 3, 3, 3, 4, 4, 4, 4,
        5, 5, 5, 5, 0
    };
    static const int dist_base[30] = {
        1, 2, 3, 4, 5, 7, 9, 13,
        17, 25, 33, 49, 65, 97, 129, 193,
        257, 385, 513, 769, 1025, 1537, 2049, 3073,
        4097, 6145, 8193, 12289, 16385, 24577
    };
    static const int dist_extra[30] = {
        0, 0, 0, 0, 1, 1, 2, 2,
        3, 3, 4, 4, 5, 5, 6, 6,
        7, 7, 8, 8, 9, 9, 10, 10,
        11, 11, 12, 12, 13, 13
    };

    size_t outpos = 0;
    int final_block = 0;

    while (!final_block) {
        if (!pdf_bs_fill(bs, 3)) {
            return 0;
        }
        final_block = (int)pdf_bs_read(bs, 1);
        int btype = (int)pdf_bs_read(bs, 2);

        if (btype == 0) {
            pdf_bs_align(bs);
            if (bs->pos + 4 > bs->size) {
                return 0;
            }
            uint16_t len = (uint16_t)(bs->data[bs->pos] | (bs->data[bs->pos + 1] << 8));
            uint16_t nlen = (uint16_t)(bs->data[bs->pos + 2] | (bs->data[bs->pos + 3] << 8));
            bs->pos += 4;
            if ((uint16_t)~len != nlen) {
                return 0;
            }
            if (bs->pos + len > bs->size) {
                return 0;
            }
            if (outpos + len > outcap) {
                return 0;
            }
            memcpy(out + outpos, bs->data + bs->pos, len);
            bs->pos += len;
            outpos += len;
        } else if (btype == 1 || btype == 2) {
            pdf_huff_table litlen = {0, NULL};
            pdf_huff_table dist = {0, NULL};
            int ok = 0;
            if (btype == 1) {
                ok = pdf_build_fixed_tables(&litlen, &dist);
            } else {
                ok = pdf_build_dynamic_tables(bs, &litlen, &dist);
            }
            if (!ok) {
                pdf_huff_free(&litlen);
                pdf_huff_free(&dist);
                return 0;
            }

            while (1) {
                int sym = 0;
                if (!pdf_huff_decode(bs, &litlen, &sym)) {
                    pdf_huff_free(&litlen);
                    pdf_huff_free(&dist);
                    return 0;
                }
                if (sym < 256) {
                    if (outpos >= outcap) {
                        pdf_huff_free(&litlen);
                        pdf_huff_free(&dist);
                        return 0;
                    }
                    out[outpos++] = (uint8_t)sym;
                } else if (sym == 256) {
                    break;
                } else if (sym <= 285) {
                    int len_idx = sym - 257;
                    int length = length_base[len_idx];
                    int extra = length_extra[len_idx];
                    if (extra) {
                        length += (int)pdf_bs_read(bs, extra);
                    }

                    int dist_sym = 0;
                    if (!pdf_huff_decode(bs, &dist, &dist_sym)) {
                        pdf_huff_free(&litlen);
                        pdf_huff_free(&dist);
                        return 0;
                    }
                    if (dist_sym > 29) {
                        pdf_huff_free(&litlen);
                        pdf_huff_free(&dist);
                        return 0;
                    }
                    int distance = dist_base[dist_sym];
                    int dist_ext = dist_extra[dist_sym];
                    if (dist_ext) {
                        distance += (int)pdf_bs_read(bs, dist_ext);
                    }
                    if (distance <= 0 || (size_t)distance > outpos) {
                        pdf_huff_free(&litlen);
                        pdf_huff_free(&dist);
                        return 0;
                    }
                    if (outpos + (size_t)length > outcap) {
                        pdf_huff_free(&litlen);
                        pdf_huff_free(&dist);
                        return 0;
                    }
                    for (int i = 0; i < length; i++) {
                        out[outpos] = out[outpos - (size_t)distance];
                        outpos++;
                    }
                } else {
                    pdf_huff_free(&litlen);
                    pdf_huff_free(&dist);
                    return 0;
                }
            }

            pdf_huff_free(&litlen);
            pdf_huff_free(&dist);
        } else {
            return 0;
        }
    }

    *outlen = outpos;
    return 1;
}

static int pdf_zlib_decompress(const uint8_t *data, size_t size, uint8_t *out, size_t outcap, size_t *outlen) {
    if (size < 2) {
        return 0;
    }
    uint8_t cmf = data[0];
    uint8_t flg = data[1];
    if ((cmf & 0x0F) != 8) {
        return 0;
    }
    if (((cmf << 8) + flg) % 31 != 0) {
        return 0;
    }
    if (flg & 0x20) {
        return 0;
    }

    pdf_bitstream bs;
    bs.data = data + 2;
    bs.size = size - 2;
    bs.pos = 0;
    bs.bitbuf = 0;
    bs.bitcount = 0;

    return pdf_deflate_decode(&bs, out, outcap, outlen);
}

static int pdf_inflate(const uint8_t *data, size_t size, uint8_t *out, size_t outcap, size_t *outlen) {
    if (pdf_zlib_decompress(data, size, out, outcap, outlen)) {
        return 1;
    }

    pdf_bitstream bs;
    bs.data = data;
    bs.size = size;
    bs.pos = 0;
    bs.bitbuf = 0;
    bs.bitcount = 0;
    return pdf_deflate_decode(&bs, out, outcap, outlen);
}

static int pdf_inflate_auto(const unsigned char *data, size_t size,
                            unsigned char **out_data, size_t *out_len) {
    const size_t max_cap = 64u * 1024u * 1024u;
    size_t cap = size * 4u + 1024u;
    if (cap < 4096u) {
        cap = 4096u;
    }

    while (cap <= max_cap) {
        unsigned char *buf = (unsigned char *)malloc(cap);
        if (!buf) {
            return 0;
        }

        size_t len = 0;
        if (pdf_inflate(data, size, buf, cap, &len)) {
            *out_data = buf;
            *out_len = len;
            return 1;
        }

        free(buf);
        if (cap > max_cap / 2u) {
            break;
        }
        cap *= 2u;
    }

    return 0;
}

static const uint8_t pdf_font8x8_basic[128][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x7E,0x81,0xA5,0x81,0xBD,0x99,0x81,0x7E},
    {0x7E,0xFF,0xDB,0xFF,0xC3,0xE7,0xFF,0x7E},
    {0x6C,0xFE,0xFE,0xFE,0x7C,0x38,0x10,0x00},
    {0x10,0x38,0x7C,0xFE,0x7C,0x38,0x10,0x00},
    {0x38,0x7C,0x38,0xFE,0xFE,0xD6,0x10,0x38},
    {0x10,0x38,0x7C,0xFE,0xFE,0x7C,0x10,0x38},
    {0x00,0x00,0x18,0x3C,0x3C,0x18,0x00,0x00},
    {0xFF,0xFF,0xE7,0xC3,0xC3,0xE7,0xFF,0xFF},
    {0x00,0x3C,0x66,0x42,0x42,0x66,0x3C,0x00},
    {0xFF,0xC3,0x99,0xBD,0xBD,0x99,0xC3,0xFF},
    {0x0F,0x07,0x0F,0x7D,0xCC,0xCC,0xCC,0x78},
    {0x3C,0x66,0x66,0x66,0x3C,0x18,0x7E,0x18},
    {0x3F,0x33,0x3F,0x30,0x30,0x70,0xF0,0xE0},
    {0x7F,0x63,0x7F,0x63,0x63,0x67,0xE6,0xC0},
    {0x99,0x5A,0x3C,0xE7,0xE7,0x3C,0x5A,0x99},
    {0x80,0xE0,0xF8,0xFE,0xF8,0xE0,0x80,0x00},
    {0x02,0x0E,0x3E,0xFE,0x3E,0x0E,0x02,0x00},
    {0x18,0x3C,0x7E,0x18,0x18,0x7E,0x3C,0x18},
    {0x66,0x66,0x66,0x66,0x66,0x00,0x66,0x00},
    {0x7F,0xDB,0xDB,0x7B,0x1B,0x1B,0x1B,0x00},
    {0x3E,0x63,0x38,0x6C,0x6C,0x38,0xCC,0x7F},
    {0x00,0x00,0x00,0x00,0x7E,0x7E,0x7E,0x00},
    {0x18,0x3C,0x7E,0x18,0x7E,0x3C,0x18,0xFF},
    {0x18,0x3C,0x7E,0x18,0x18,0x18,0x18,0x00},
    {0x18,0x18,0x18,0x18,0x7E,0x3C,0x18,0x00},
    {0x00,0x18,0x0C,0xFE,0x0C,0x18,0x00,0x00},
    {0x00,0x30,0x60,0xFE,0x60,0x30,0x00,0x00},
    {0x00,0x00,0xC0,0xC0,0xC0,0xFE,0x00,0x00},
    {0x00,0x24,0x66,0xFF,0x66,0x24,0x00,0x00},
    {0x00,0x18,0x3C,0x7E,0xFF,0xFF,0x00,0x00},
    {0x00,0xFF,0xFF,0x7E,0x3C,0x18,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00},
    {0x36,0x36,0x12,0x00,0x00,0x00,0x00,0x00},
    {0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00},
    {0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0x00},
    {0x00,0x63,0x33,0x18,0x0C,0x66,0x63,0x00},
    {0x1C,0x36,0x1C,0x6E,0x3B,0x33,0x6E,0x00},
    {0x06,0x06,0x03,0x00,0x00,0x00,0x00,0x00},
    {0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00},
    {0x06,0x0C,0x18,0x18,0x18,0x0C,0x06,0x00},
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00},
    {0x00,0x0C,0x0C,0x3F,0x0C,0x0C,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x06},
    {0x00,0x00,0x00,0x3F,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x00},
    {0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00},
    {0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0x00},
    {0x0C,0x0E,0x0C,0x0C,0x0C,0x0C,0x3F,0x00},
    {0x1E,0x33,0x30,0x1C,0x06,0x33,0x3F,0x00},
    {0x1E,0x33,0x30,0x1C,0x30,0x33,0x1E,0x00},
    {0x38,0x3C,0x36,0x33,0x7F,0x30,0x78,0x00},
    {0x3F,0x03,0x1F,0x30,0x30,0x33,0x1E,0x00},
    {0x1C,0x06,0x03,0x1F,0x33,0x33,0x1E,0x00},
    {0x3F,0x33,0x30,0x18,0x0C,0x0C,0x0C,0x00},
    {0x1E,0x33,0x33,0x1E,0x33,0x33,0x1E,0x00},
    {0x1E,0x33,0x33,0x3E,0x30,0x18,0x0E,0x00},
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x00},
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x06},
    {0x18,0x0C,0x06,0x03,0x06,0x0C,0x18,0x00},
    {0x00,0x00,0x3F,0x00,0x00,0x3F,0x00,0x00},
    {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00},
    {0x1E,0x33,0x30,0x18,0x0C,0x00,0x0C,0x00},
    {0x3E,0x63,0x7B,0x7B,0x7B,0x03,0x1E,0x00},
    {0x0C,0x1E,0x33,0x33,0x3F,0x33,0x33,0x00},
    {0x3F,0x66,0x66,0x3E,0x66,0x66,0x3F,0x00},
    {0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x00},
    {0x1F,0x36,0x66,0x66,0x66,0x36,0x1F,0x00},
    {0x7F,0x46,0x16,0x1E,0x16,0x46,0x7F,0x00},
    {0x7F,0x46,0x16,0x1E,0x16,0x06,0x0F,0x00},
    {0x3C,0x66,0x03,0x03,0x73,0x66,0x7C,0x00},
    {0x33,0x33,0x33,0x3F,0x33,0x33,0x33,0x00},
    {0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00},
    {0x78,0x30,0x30,0x30,0x33,0x33,0x1E,0x00},
    {0x67,0x66,0x36,0x1E,0x36,0x66,0x67,0x00},
    {0x0F,0x06,0x06,0x06,0x46,0x66,0x7F,0x00},
    {0x63,0x77,0x7F,0x7F,0x6B,0x63,0x63,0x00},
    {0x63,0x67,0x6F,0x7B,0x73,0x63,0x63,0x00},
    {0x1C,0x36,0x63,0x63,0x63,0x36,0x1C,0x00},
    {0x3F,0x66,0x66,0x3E,0x06,0x06,0x0F,0x00},
    {0x1E,0x33,0x33,0x33,0x3B,0x1E,0x38,0x00},
    {0x3F,0x66,0x66,0x3E,0x36,0x66,0x67,0x00},
    {0x1E,0x33,0x07,0x0E,0x38,0x33,0x1E,0x00},
    {0x3F,0x2D,0x0C,0x0C,0x0C,0x0C,0x1E,0x00},
    {0x33,0x33,0x33,0x33,0x33,0x33,0x3F,0x00},
    {0x33,0x33,0x33,0x33,0x33,0x1E,0x0C,0x00},
    {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00},
    {0x63,0x63,0x36,0x1C,0x1C,0x36,0x63,0x00},
    {0x33,0x33,0x33,0x1E,0x0C,0x0C,0x1E,0x00},
    {0x7F,0x63,0x31,0x18,0x4C,0x66,0x7F,0x00},
    {0x1E,0x06,0x06,0x06,0x06,0x06,0x1E,0x00},
    {0x03,0x06,0x0C,0x18,0x30,0x60,0x40,0x00},
    {0x1E,0x18,0x18,0x18,0x18,0x18,0x1E,0x00},
    {0x08,0x1C,0x36,0x63,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF},
    {0x0C,0x0C,0x18,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x1E,0x30,0x3E,0x33,0x6E,0x00},
    {0x07,0x06,0x06,0x3E,0x66,0x66,0x3B,0x00},
    {0x00,0x00,0x1E,0x33,0x03,0x33,0x1E,0x00},
    {0x38,0x30,0x30,0x3E,0x33,0x33,0x6E,0x00},
    {0x00,0x00,0x1E,0x33,0x3F,0x03,0x1E,0x00},
    {0x1C,0x36,0x06,0x0F,0x06,0x06,0x0F,0x00},
    {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x1F},
    {0x07,0x06,0x36,0x6E,0x66,0x66,0x67,0x00},
    {0x0C,0x00,0x0E,0x0C,0x0C,0x0C,0x1E,0x00},
    {0x18,0x00,0x1C,0x18,0x18,0x18,0x1B,0x0E},
    {0x07,0x06,0x66,0x36,0x1E,0x36,0x67,0x00},
    {0x0E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00},
    {0x00,0x00,0x33,0x7F,0x7F,0x6B,0x63,0x00},
    {0x00,0x00,0x1F,0x33,0x33,0x33,0x33,0x00},
    {0x00,0x00,0x1E,0x33,0x33,0x33,0x1E,0x00},
    {0x00,0x00,0x3B,0x66,0x66,0x3E,0x06,0x0F},
    {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x78},
    {0x00,0x00,0x3B,0x6E,0x66,0x06,0x0F,0x00},
    {0x00,0x00,0x3E,0x03,0x1E,0x30,0x1F,0x00},
    {0x08,0x0C,0x3E,0x0C,0x0C,0x2C,0x18,0x00},
    {0x00,0x00,0x33,0x33,0x33,0x33,0x6E,0x00},
    {0x00,0x00,0x33,0x33,0x33,0x1E,0x0C,0x00},
    {0x00,0x00,0x63,0x63,0x6B,0x7F,0x36,0x00},
    {0x00,0x00,0x63,0x36,0x1C,0x36,0x63,0x00},
    {0x00,0x00,0x33,0x33,0x33,0x3E,0x30,0x1F},
    {0x00,0x00,0x3F,0x19,0x0C,0x26,0x3F,0x00},
    {0x38,0x0C,0x0C,0x07,0x0C,0x0C,0x38,0x00},
    {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00},
    {0x07,0x0C,0x0C,0x38,0x0C,0x0C,0x07,0x00},
    {0x6E,0x3B,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}
};

typedef struct {
    float x0;
    float y0;
    float x1;
    float y1;
} pdf_seg;

typedef struct {
    pdf_seg segs[4096];
    int seg_count;
    float current_x;
    float current_y;
    float subpath_start_x;
    float subpath_start_y;
    int has_current;
    int has_subpath;
} pdf_path;

typedef struct {
    float ctm[6];
    uint8_t fill_r;
    uint8_t fill_g;
    uint8_t fill_b;
    uint8_t stroke_r;
    uint8_t stroke_g;
    uint8_t stroke_b;
    float line_width_pts;
} pdf_graphics_state;

typedef struct {
    cupidimage_image *img;
    int page_height_pts;
    float line_x_pts;
    float line_y_pts;
    float text_x_pts;
    float text_y_pts;
    float font_size_pts;
    float leading_pts;
    pdf_graphics_state gs;
    pdf_graphics_state gs_stack[32];
    int gs_stack_len;
    pdf_path path;
    int in_text;
} pdf_render_state;

static int pdf_is_ws(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\0';
}

static int pdf_is_delim(unsigned char c) {
    return c == '(' || c == ')' || c == '<' || c == '>' || c == '[' || c == ']' ||
           c == '{' || c == '}' || c == '/' || c == '%';
}

static void pdf_skip_ws_and_comments(const unsigned char *data, size_t size, size_t *pos) {
    while (*pos < size) {
        if (pdf_is_ws(data[*pos])) {
            (*pos)++;
            continue;
        }
        if (data[*pos] == '%') {
            while (*pos < size && data[*pos] != '\n' && data[*pos] != '\r') {
                (*pos)++;
            }
            continue;
        }
        break;
    }
}

static unsigned char pdf_clamp_u8(double v) {
    if (v < 0.0) v = 0.0;
    if (v > 1.0) v = 1.0;
    return (unsigned char)(v * 255.0 + 0.5);
}

static int pdf_parse_number_token(const unsigned char *data, size_t size, size_t *pos, double *out) {
    size_t p = *pos;
    int saw_digit = 0;
    int saw_dot = 0;

    if (p < size && (data[p] == '+' || data[p] == '-')) {
        p++;
    }
    while (p < size) {
        if (data[p] >= '0' && data[p] <= '9') {
            saw_digit = 1;
            p++;
            continue;
        }
        if (data[p] == '.' && !saw_dot) {
            saw_dot = 1;
            p++;
            continue;
        }
        break;
    }

    if (!saw_digit) {
        return 0;
    }

    char tmp[64];
    size_t n = p - *pos;
    if (n >= sizeof(tmp)) {
        n = sizeof(tmp) - 1;
    }
    memcpy(tmp, &data[*pos], n);
    tmp[n] = '\0';
    *out = strtod(tmp, NULL);
    *pos = p;
    return 1;
}

static int pdf_parse_name_token(const unsigned char *data, size_t size, size_t *pos,
                                char **out_str, size_t *out_len) {
    if (*pos >= size || data[*pos] != '/') {
        return 0;
    }
    size_t start = *pos + 1;
    size_t p = start;
    while (p < size && !pdf_is_ws(data[p]) && !pdf_is_delim(data[p])) {
        p++;
    }

    size_t len = p - start;
    char *s = (char *)malloc(len + 1);
    if (!s) {
        return 0;
    }
    memcpy(s, &data[start], len);
    s[len] = '\0';

    *out_str = s;
    *out_len = len;
    *pos = p;
    return 1;
}

static int pdf_parse_literal_string(const unsigned char *data, size_t size, size_t *pos,
                                    char **out_str, size_t *out_len) {
    if (*pos >= size || data[*pos] != '(') {
        return 0;
    }

    size_t p = *pos + 1;
    int depth = 1;
    size_t cap = 64;
    size_t len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        return 0;
    }

    while (p < size) {
        unsigned char c = data[p++];

        if (c == '\\') {
            if (p >= size) break;
            unsigned char e = data[p++];
            unsigned char outc = e;
            if (e == 'n') outc = '\n';
            else if (e == 'r') outc = '\r';
            else if (e == 't') outc = '\t';
            else if (e == 'b') outc = '\b';
            else if (e == 'f') outc = '\f';
            else if (e == '\\' || e == '(' || e == ')') outc = e;
            else if (e == '\n' || e == '\r') {
                if (e == '\r' && p < size && data[p] == '\n') p++;
                continue;
            } else if (e >= '0' && e <= '7') {
                int v = e - '0';
                for (int i = 0; i < 2 && p < size && data[p] >= '0' && data[p] <= '7'; i++) {
                    v = (v << 3) + (data[p] - '0');
                    p++;
                }
                outc = (unsigned char)v;
            }

            if (len + 1 >= cap) {
                size_t ncap = cap * 2;
                char *n = (char *)realloc(buf, ncap);
                if (!n) {
                    free(buf);
                    return 0;
                }
                buf = n;
                cap = ncap;
            }
            buf[len++] = (char)outc;
            continue;
        }

        if (c == '(') {
            depth++;
            if (len + 1 >= cap) {
                size_t ncap = cap * 2;
                char *n = (char *)realloc(buf, ncap);
                if (!n) {
                    free(buf);
                    return 0;
                }
                buf = n;
                cap = ncap;
            }
            buf[len++] = (char)c;
            continue;
        }

        if (c == ')') {
            depth--;
            if (depth == 0) {
                *pos = p;
                buf[len] = '\0';
                *out_str = buf;
                *out_len = len;
                return 1;
            }
            if (len + 1 >= cap) {
                size_t ncap = cap * 2;
                char *n = (char *)realloc(buf, ncap);
                if (!n) {
                    free(buf);
                    return 0;
                }
                buf = n;
                cap = ncap;
            }
            buf[len++] = (char)c;
            continue;
        }

        if (len + 1 >= cap) {
            size_t ncap = cap * 2;
            char *n = (char *)realloc(buf, ncap);
            if (!n) {
                free(buf);
                return 0;
            }
            buf = n;
            cap = ncap;
        }
        buf[len++] = (char)c;
    }

    free(buf);
    return 0;
}

static int pdf_parse_hex_string(const unsigned char *data, size_t size, size_t *pos,
                                char **out_str, size_t *out_len) {
    if (*pos >= size || data[*pos] != '<') {
        return 0;
    }
    if (*pos + 1 < size && data[*pos + 1] == '<') {
        return 0;
    }

    size_t p = *pos + 1;
    size_t cap = 64;
    size_t len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        return 0;
    }

    int have_hi = 0;
    int hi = 0;
    while (p < size) {
        unsigned char c = data[p++];
        if (c == '>') {
            break;
        }
        if (isspace(c)) {
            continue;
        }

        int v = -1;
        if (c >= '0' && c <= '9') v = c - '0';
        else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
        if (v < 0) {
            free(buf);
            return 0;
        }

        if (!have_hi) {
            hi = v;
            have_hi = 1;
        } else {
            unsigned char outc = (unsigned char)((hi << 4) | v);
            if (len + 1 >= cap) {
                size_t ncap = cap * 2;
                char *n = (char *)realloc(buf, ncap);
                if (!n) {
                    free(buf);
                    return 0;
                }
                buf = n;
                cap = ncap;
            }
            buf[len++] = (char)outc;
            have_hi = 0;
        }
    }

    if (have_hi) {
        unsigned char outc = (unsigned char)(hi << 4);
        if (len + 1 >= cap) {
            size_t ncap = cap * 2;
            char *n = (char *)realloc(buf, ncap);
            if (!n) {
                free(buf);
                return 0;
            }
            buf = n;
            cap = ncap;
        }
        buf[len++] = (char)outc;
    }

    buf[len] = '\0';
    *out_str = buf;
    *out_len = len;
    *pos = p;
    return 1;
}

static int pdf_parse_array_token(const unsigned char *data, size_t size, size_t *pos,
                                 char **out_str, size_t *out_len) {
    if (*pos >= size || data[*pos] != '[') {
        return 0;
    }

    size_t start = *pos + 1;
    size_t p = start;
    int depth = 1;

    while (p < size) {
        unsigned char c = data[p];
        if (c == '%') {
            while (p < size && data[p] != '\n' && data[p] != '\r') p++;
            continue;
        }
        if (c == '(') {
            size_t tmp = p;
            char *dummy = NULL;
            size_t dlen = 0;
            if (!pdf_parse_literal_string(data, size, &tmp, &dummy, &dlen)) {
                return 0;
            }
            free(dummy);
            p = tmp;
            continue;
        }
        if (c == '[') {
            depth++;
            p++;
            continue;
        }
        if (c == ']') {
            depth--;
            if (depth == 0) {
                size_t len = p - start;
                char *s = (char *)malloc(len + 1);
                if (!s) {
                    return 0;
                }
                memcpy(s, &data[start], len);
                s[len] = '\0';
                *out_str = s;
                *out_len = len;
                *pos = p + 1;
                return 1;
            }
            p++;
            continue;
        }
        p++;
    }

    return 0;
}

static void pdf_set_pixel(cupidimage_image *img, int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    if (!img || !img->rgba) {
        return;
    }
    if (x < 0 || y < 0 || x >= (int)img->width || y >= (int)img->height) {
        return;
    }
    size_t idx = ((size_t)y * img->width + (size_t)x) * 4u;
    img->rgba[idx + 0] = r;
    img->rgba[idx + 1] = g;
    img->rgba[idx + 2] = b;
    img->rgba[idx + 3] = 255;
}

static void pdf_draw_rect(cupidimage_image *img, int x, int y, int w, int h,
                          uint8_t r, uint8_t g, uint8_t b) {
    if (w <= 0 || h <= 0) {
        return;
    }
    int x0 = x;
    int y0 = y;
    int x1 = x + w;
    int y1 = y + h;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int)img->width) x1 = (int)img->width;
    if (y1 > (int)img->height) y1 = (int)img->height;

    for (int yy = y0; yy < y1; yy++) {
        for (int xx = x0; xx < x1; xx++) {
            pdf_set_pixel(img, xx, yy, r, g, b);
        }
    }
}

static void pdf_matrix_identity(float m[6]) {
    m[0] = 1.0f; m[1] = 0.0f; m[2] = 0.0f;
    m[3] = 1.0f; m[4] = 0.0f; m[5] = 0.0f;
}

static void pdf_matrix_concat(float dst[6], const float left[6]) {
    float a = left[0] * dst[0] + left[2] * dst[1];
    float b = left[1] * dst[0] + left[3] * dst[1];
    float c = left[0] * dst[2] + left[2] * dst[3];
    float d = left[1] * dst[2] + left[3] * dst[3];
    float e = left[0] * dst[4] + left[2] * dst[5] + left[4];
    float f = left[1] * dst[4] + left[3] * dst[5] + left[5];
    dst[0] = a;
    dst[1] = b;
    dst[2] = c;
    dst[3] = d;
    dst[4] = e;
    dst[5] = f;
}

static void pdf_user_to_pixel(const pdf_render_state *st, float ux, float uy, float *px, float *py) {
    float tx = st->gs.ctm[0] * ux + st->gs.ctm[2] * uy + st->gs.ctm[4];
    float ty = st->gs.ctm[1] * ux + st->gs.ctm[3] * uy + st->gs.ctm[5];
    float s = 96.0f / 72.0f;
    *px = tx * s;
    *py = ((float)st->page_height_pts - ty) * s;
}

static void pdf_path_clear(pdf_path *p) {
    if (!p) return;
    p->seg_count = 0;
    p->has_current = 0;
    p->has_subpath = 0;
    p->current_x = 0.0f;
    p->current_y = 0.0f;
    p->subpath_start_x = 0.0f;
    p->subpath_start_y = 0.0f;
}

static void pdf_path_add_seg(pdf_path *p, float x0, float y0, float x1, float y1) {
    if (!p) return;
    if (p->seg_count >= (int)(sizeof(p->segs) / sizeof(p->segs[0]))) return;
    p->segs[p->seg_count].x0 = x0;
    p->segs[p->seg_count].y0 = y0;
    p->segs[p->seg_count].x1 = x1;
    p->segs[p->seg_count].y1 = y1;
    p->seg_count++;
}

static void pdf_path_move_to(pdf_path *p, float x, float y) {
    if (!p) return;
    p->current_x = x;
    p->current_y = y;
    p->subpath_start_x = x;
    p->subpath_start_y = y;
    p->has_current = 1;
    p->has_subpath = 1;
}

static void pdf_path_line_to(pdf_path *p, float x, float y) {
    if (!p) return;
    if (!p->has_current) {
        pdf_path_move_to(p, x, y);
        return;
    }
    pdf_path_add_seg(p, p->current_x, p->current_y, x, y);
    p->current_x = x;
    p->current_y = y;
}

static void pdf_path_close(pdf_path *p) {
    if (!p || !p->has_current || !p->has_subpath) return;
    if (fabsf(p->current_x - p->subpath_start_x) > 1e-4f ||
        fabsf(p->current_y - p->subpath_start_y) > 1e-4f) {
        pdf_path_add_seg(p, p->current_x, p->current_y, p->subpath_start_x, p->subpath_start_y);
    }
    p->current_x = p->subpath_start_x;
    p->current_y = p->subpath_start_y;
}

static float pdf_point_to_segment_dist_sq(float px, float py, float x0, float y0, float x1, float y1) {
    float vx = x1 - x0;
    float vy = y1 - y0;
    float wx = px - x0;
    float wy = py - y0;
    float c1 = vx * wx + vy * wy;
    if (c1 <= 0.0f) {
        float dx = px - x0;
        float dy = py - y0;
        return dx * dx + dy * dy;
    }
    float c2 = vx * vx + vy * vy;
    if (c2 <= 1e-8f) {
        float dx = px - x0;
        float dy = py - y0;
        return dx * dx + dy * dy;
    }
    if (c1 >= c2) {
        float dx = px - x1;
        float dy = py - y1;
        return dx * dx + dy * dy;
    }
    float t = c1 / c2;
    float qx = x0 + t * vx;
    float qy = y0 + t * vy;
    float dx = px - qx;
    float dy = py - qy;
    return dx * dx + dy * dy;
}

static void pdf_stroke_path(pdf_render_state *st) {
    if (!st || !st->img || st->path.seg_count <= 0) return;

    float px_per_pt = 96.0f / 72.0f;
    float half_w = st->gs.line_width_pts * px_per_pt * 0.5f;
    if (half_w < 0.5f) half_w = 0.5f;
    float half_w_sq = half_w * half_w;

    for (int i = 0; i < st->path.seg_count; i++) {
        float x0, y0, x1, y1;
        pdf_user_to_pixel(st, st->path.segs[i].x0, st->path.segs[i].y0, &x0, &y0);
        pdf_user_to_pixel(st, st->path.segs[i].x1, st->path.segs[i].y1, &x1, &y1);

        int min_x = (int)floorf(fminf(x0, x1) - half_w - 1.0f);
        int min_y = (int)floorf(fminf(y0, y1) - half_w - 1.0f);
        int max_x = (int)ceilf(fmaxf(x0, x1) + half_w + 1.0f);
        int max_y = (int)ceilf(fmaxf(y0, y1) + half_w + 1.0f);

        if (min_x < 0) min_x = 0;
        if (min_y < 0) min_y = 0;
        if (max_x >= (int)st->img->width) max_x = (int)st->img->width - 1;
        if (max_y >= (int)st->img->height) max_y = (int)st->img->height - 1;

        for (int y = min_y; y <= max_y; y++) {
            float py = (float)y + 0.5f;
            for (int x = min_x; x <= max_x; x++) {
                float px = (float)x + 0.5f;
                float d2 = pdf_point_to_segment_dist_sq(px, py, x0, y0, x1, y1);
                if (d2 <= half_w_sq) {
                    pdf_set_pixel(st->img, x, y, st->gs.stroke_r, st->gs.stroke_g, st->gs.stroke_b);
                }
            }
        }
    }
}

static int pdf_scanline_intersects(float x, float y, const pdf_seg *seg) {
    float y0 = seg->y0;
    float y1 = seg->y1;
    /* Ensure consistent edge handling: include lower endpoint, exclude upper endpoint */
    if ((y0 < y && y1 >= y) || (y1 < y && y0 >= y)) {
        /* Skip horizontal edges (y0 == y1) to avoid double-counting */
        if (y0 == y1) {
            return 0;
        }
        float t = (y - y0) / (y1 - y0);
        float ix = seg->x0 + t * (seg->x1 - seg->x0);
        return ix > x;
    }
    return 0;
}

static void pdf_fill_path(pdf_render_state *st) {
    if (!st || !st->img || st->path.seg_count <= 0) return;

    float min_x = 1e9f, min_y = 1e9f, max_x = -1e9f, max_y = -1e9f;
    pdf_seg xformed[4096];
    int n = st->path.seg_count;
    if (n > (int)(sizeof(xformed) / sizeof(xformed[0]))) {
        n = (int)(sizeof(xformed) / sizeof(xformed[0]));
    }

    for (int i = 0; i < n; i++) {
        pdf_user_to_pixel(st, st->path.segs[i].x0, st->path.segs[i].y0, &xformed[i].x0, &xformed[i].y0);
        pdf_user_to_pixel(st, st->path.segs[i].x1, st->path.segs[i].y1, &xformed[i].x1, &xformed[i].y1);
        if (xformed[i].x0 < min_x) min_x = xformed[i].x0;
        if (xformed[i].x1 < min_x) min_x = xformed[i].x1;
        if (xformed[i].x0 > max_x) max_x = xformed[i].x0;
        if (xformed[i].x1 > max_x) max_x = xformed[i].x1;
        if (xformed[i].y0 < min_y) min_y = xformed[i].y0;
        if (xformed[i].y1 < min_y) min_y = xformed[i].y1;
        if (xformed[i].y0 > max_y) max_y = xformed[i].y0;
        if (xformed[i].y1 > max_y) max_y = xformed[i].y1;
    }

    int x0 = (int)floorf(min_x);
    int y0 = (int)floorf(min_y);
    int x1 = (int)ceilf(max_x);
    int y1 = (int)ceilf(max_y);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int)st->img->width) x1 = (int)st->img->width;
    if (y1 > (int)st->img->height) y1 = (int)st->img->height;

    for (int y = y0; y < y1; y++) {
        float py = (float)y + 0.5f;
        for (int x = x0; x < x1; x++) {
            float px = (float)x + 0.5f;
            int crossings = 0;
            for (int i = 0; i < n; i++) {
                crossings += pdf_scanline_intersects(px, py, &xformed[i]);
            }
            if (crossings & 1) {
                pdf_set_pixel(st->img, x, y, st->gs.fill_r, st->gs.fill_g, st->gs.fill_b);
            }
        }
    }
}

static int pdf_is_nonwhite_pixel(const unsigned char *px) {
    return px[0] < 250 || px[1] < 250 || px[2] < 250 || px[3] < 250;
}

static void pdf_trim_whitespace(cupidimage_image *img) {
    if (!img || !img->rgba || img->width == 0 || img->height == 0) {
        return;
    }

    uint32_t w = img->width;
    uint32_t h = img->height;
    size_t *row_mass = (size_t *)calloc(h, sizeof(size_t));
    size_t *col_mass = (size_t *)calloc(w, sizeof(size_t));
    if (!row_mass || !col_mass) {
        free(row_mass);
        free(col_mass);
        return;
    }

    size_t total_mass = 0;
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            const unsigned char *px = img->rgba + ((size_t)y * (size_t)w + (size_t)x) * 4u;
            if (!pdf_is_nonwhite_pixel(px)) {
                continue;
            }
            row_mass[y]++;
            col_mass[x]++;
            total_mass++;
        }
    }

    if (total_mass == 0) {
        free(row_mass);
        free(col_mass);
        return;
    }

    size_t cut_mass_y = total_mass / 300u;  /* Trim only sparse edge noise. */
    size_t cut_mass_x = total_mass / 300u;

    uint32_t top = 0;
    size_t acc = 0;
    while (top + 1 < h && acc + row_mass[top] <= cut_mass_y) {
        acc += row_mass[top];
        top++;
    }

    uint32_t bottom = h - 1u;
    acc = 0;
    while (bottom > top && acc + row_mass[bottom] <= cut_mass_y) {
        acc += row_mass[bottom];
        bottom--;
    }

    uint32_t left = 0;
    acc = 0;
    while (left + 1 < w && acc + col_mass[left] <= cut_mass_x) {
        acc += col_mass[left];
        left++;
    }

    uint32_t right = w - 1u;
    acc = 0;
    while (right > left && acc + col_mass[right] <= cut_mass_x) {
        acc += col_mass[right];
        right--;
    }

    /* Keep a page-like framing: don't trim more than ~1/6 from any side. */
    uint32_t max_trim_y = h / 6u;
    uint32_t max_trim_x = w / 6u;
    if (top > max_trim_y) {
        top = max_trim_y;
    }
    if (left > max_trim_x) {
        left = max_trim_x;
    }
    uint32_t trimmed_bottom = (h - 1u) - bottom;
    if (trimmed_bottom > max_trim_y) {
        bottom = (h - 1u) - max_trim_y;
    }
    uint32_t trimmed_right = (w - 1u) - right;
    if (trimmed_right > max_trim_x) {
        right = (w - 1u) - max_trim_x;
    }

    free(row_mass);
    free(col_mass);

    if (right <= left || bottom <= top) {
        return;
    }

    uint32_t pad = 16u;
    if (left > pad) left -= pad; else left = 0;
    if (top > pad) top -= pad; else top = 0;
    if (right + pad < w) right += pad; else right = w - 1u;
    if (bottom + pad < h) bottom += pad; else bottom = h - 1u;

    uint32_t new_w = right - left + 1u;
    uint32_t new_h = bottom - top + 1u;
    if (new_w >= w || new_h >= h) {
        return;
    }

    unsigned char *cropped = (unsigned char *)malloc((size_t)new_w * (size_t)new_h * 4u);
    if (!cropped) {
        return;
    }

    for (uint32_t y = 0; y < new_h; y++) {
        const unsigned char *src = img->rgba + ((size_t)(top + y) * (size_t)w + (size_t)left) * 4u;
        unsigned char *dst = cropped + (size_t)y * (size_t)new_w * 4u;
        memcpy(dst, src, (size_t)new_w * 4u);
    }

    free(img->rgba);
    img->rgba = cropped;
    img->width = new_w;
    img->height = new_h;
}

static void pdf_draw_glyph(pdf_render_state *st, unsigned char c) {
    if (!st || !st->img || !st->img->rgba) {
        return;
    }

    const uint8_t *glyph = pdf_font8x8_basic[(c < 128u) ? c : (unsigned char)'?'];

    float px_per_pt = 96.0f / 72.0f;
    float font_px_f = st->font_size_pts * px_per_pt;
    if (font_px_f < 6.0f) {
        font_px_f = 6.0f;
    }

    int font_px = (int)(font_px_f + 0.5f);
    int scale_x = (int)((font_px_f * 0.6f) / 8.0f + 0.5f);
    int scale_y = (int)(font_px_f / 8.0f + 0.5f);
    if (scale_x < 1) scale_x = 1;
    if (scale_y < 1) scale_y = 1;

    int base_x = (int)(st->text_x_pts * px_per_pt + 0.5f);
    int baseline_y = (int)(((float)st->page_height_pts - st->text_y_pts) * px_per_pt + 0.5f);
    int top_y = baseline_y - font_px;

    for (int gy = 0; gy < 8; gy++) {
        for (int gx = 0; gx < 8; gx++) {
            if ((glyph[gy] & (1u << gx)) == 0) {
                continue;
            }
            int rx = base_x + gx * scale_x;
            int ry = top_y + gy * scale_y;
            pdf_draw_rect(st->img, rx, ry, scale_x, scale_y,
                          st->gs.fill_r, st->gs.fill_g, st->gs.fill_b);
        }
    }
}

static float pdf_char_advance_em(unsigned char c) {
    if (c == ' ') return 0.278f;
    if (c >= '0' && c <= '9') return 0.556f;
    if (c >= 'A' && c <= 'Z') {
        if (c == 'M' || c == 'W') return 0.889f;
        if (c == 'I') return 0.333f;
        return 0.667f;
    }
    if (c >= 'a' && c <= 'z') {
        if (c == 'm') return 0.833f;
        if (c == 'w') return 0.722f;
        if (c == 'i' || c == 'l') return 0.278f;
        return 0.5f;
    }
    if (c == '.' || c == ',' || c == ':' || c == ';' || c == '!' || c == '\'') return 0.278f;
    if (c == '(' || c == ')' || c == '[' || c == ']') return 0.333f;
    if (c == '-') return 0.333f;
    if (c == '/') return 0.278f;
    if (c == '?') return 0.556f;
    return 0.5f;
}

static void pdf_render_text_string(pdf_render_state *st, const char *s, size_t len) {
    if (!st || !s || !st->in_text) {
        return;
    }

    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\r' || c == '\n') {
            float lead = (st->leading_pts > 0.0f) ? st->leading_pts : st->font_size_pts * 1.2f;
            st->line_y_pts -= lead;
            st->text_x_pts = st->line_x_pts;
            st->text_y_pts = st->line_y_pts;
            continue;
        }
        if (c < 32u) {
            continue;
        }
        if (c >= 127u) {
            st->text_x_pts += st->font_size_pts * 0.5f;
            continue;
        }
        pdf_draw_glyph(st, c);
        st->text_x_pts += st->font_size_pts * pdf_char_advance_em(c);
    }
}

static void pdf_render_tj_array(pdf_render_state *st, const char *arr, size_t len) {
    size_t pos = 0;
    const unsigned char *data = (const unsigned char *)arr;

    while (pos < len) {
        pdf_skip_ws_and_comments(data, len, &pos);
        if (pos >= len) {
            break;
        }

        if (data[pos] == '(') {
            char *txt = NULL;
            size_t txt_len = 0;
            if (pdf_parse_literal_string(data, len, &pos, &txt, &txt_len)) {
                pdf_render_text_string(st, txt, txt_len);
                free(txt);
                continue;
            }
            break;
        }

        if (data[pos] == '<' && !(pos + 1 < len && data[pos + 1] == '<')) {
            char *txt = NULL;
            size_t txt_len = 0;
            if (pdf_parse_hex_string(data, len, &pos, &txt, &txt_len)) {
                pdf_render_text_string(st, txt, txt_len);
                free(txt);
                continue;
            }
            break;
        }

        if (data[pos] == '+' || data[pos] == '-' || data[pos] == '.' ||
            (data[pos] >= '0' && data[pos] <= '9')) {
            double num = 0.0;
            if (pdf_parse_number_token(data, len, &pos, &num)) {
                st->text_x_pts += (float)(-num * st->font_size_pts / 1000.0);
                continue;
            }
        }

        pos++;
    }
}

typedef enum {
    PDF_OPERAND_NUM,
    PDF_OPERAND_NAME,
    PDF_OPERAND_STRING,
    PDF_OPERAND_ARRAY
} pdf_operand_type;

typedef struct {
    pdf_operand_type type;
    double num;
    char *str;
    size_t len;
} pdf_operand;

static void pdf_clear_operands(pdf_operand *ops, int *count) {
    if (!ops || !count) {
        return;
    }
    for (int i = 0; i < *count; i++) {
        free(ops[i].str);
        ops[i].str = NULL;
        ops[i].len = 0;
        ops[i].num = 0.0;
    }
    *count = 0;
}

static int pdf_expect_num(const pdf_operand *op) {
    return op && op->type == PDF_OPERAND_NUM;
}

static int pdf_expect_str(const pdf_operand *op) {
    return op && (op->type == PDF_OPERAND_STRING || op->type == PDF_OPERAND_ARRAY || op->type == PDF_OPERAND_NAME);
}

static void pdf_execute_operator(pdf_render_state *st, const char *op,
                                 pdf_operand *ops, int *count) {
    int n = *count;

    if (strcmp(op, "BT") == 0) {
        st->in_text = 1;
        st->line_x_pts = 0.0f;
        st->line_y_pts = 0.0f;
        st->text_x_pts = 0.0f;
        st->text_y_pts = 0.0f;
    } else if (strcmp(op, "ET") == 0) {
        st->in_text = 0;
    } else if (strcmp(op, "Tf") == 0) {
        if (n >= 2 && pdf_expect_str(&ops[n - 2]) && pdf_expect_num(&ops[n - 1])) {
            float fs = (float)ops[n - 1].num;
            if (fs > 0.1f && fs < 500.0f) {
                st->font_size_pts = fs;
            }
        }
    } else if (strcmp(op, "Td") == 0) {
        if (n >= 2 && pdf_expect_num(&ops[n - 2]) && pdf_expect_num(&ops[n - 1])) {
            st->line_x_pts += (float)ops[n - 2].num;
            st->line_y_pts += (float)ops[n - 1].num;
            st->text_x_pts = st->line_x_pts;
            st->text_y_pts = st->line_y_pts;
        }
    } else if (strcmp(op, "TD") == 0) {
        if (n >= 2 && pdf_expect_num(&ops[n - 2]) && pdf_expect_num(&ops[n - 1])) {
            float ty = (float)ops[n - 1].num;
            st->leading_pts = -ty;
            st->line_x_pts += (float)ops[n - 2].num;
            st->line_y_pts += ty;
            st->text_x_pts = st->line_x_pts;
            st->text_y_pts = st->line_y_pts;
        }
    } else if (strcmp(op, "Tm") == 0) {
        if (n >= 6 && pdf_expect_num(&ops[n - 2]) && pdf_expect_num(&ops[n - 1])) {
            st->line_x_pts = (float)ops[n - 2].num;
            st->line_y_pts = (float)ops[n - 1].num;
            st->text_x_pts = st->line_x_pts;
            st->text_y_pts = st->line_y_pts;
        }
    } else if (strcmp(op, "T*") == 0) {
        float lead = (st->leading_pts > 0.0f) ? st->leading_pts : st->font_size_pts * 1.2f;
        st->line_y_pts -= lead;
        st->text_x_pts = st->line_x_pts;
        st->text_y_pts = st->line_y_pts;
    } else if (strcmp(op, "TL") == 0) {
        if (n >= 1 && pdf_expect_num(&ops[n - 1])) {
            st->leading_pts = (float)ops[n - 1].num;
        }
    } else if (strcmp(op, "rg") == 0) {
        if (n >= 3 && pdf_expect_num(&ops[n - 3]) && pdf_expect_num(&ops[n - 2]) && pdf_expect_num(&ops[n - 1])) {
            st->gs.fill_r = pdf_clamp_u8(ops[n - 3].num);
            st->gs.fill_g = pdf_clamp_u8(ops[n - 2].num);
            st->gs.fill_b = pdf_clamp_u8(ops[n - 1].num);
        }
    } else if (strcmp(op, "RG") == 0) {
        if (n >= 3 && pdf_expect_num(&ops[n - 3]) && pdf_expect_num(&ops[n - 2]) && pdf_expect_num(&ops[n - 1])) {
            st->gs.stroke_r = pdf_clamp_u8(ops[n - 3].num);
            st->gs.stroke_g = pdf_clamp_u8(ops[n - 2].num);
            st->gs.stroke_b = pdf_clamp_u8(ops[n - 1].num);
        }
    } else if (strcmp(op, "g") == 0) {
        if (n >= 1 && pdf_expect_num(&ops[n - 1])) {
            unsigned char v = pdf_clamp_u8(ops[n - 1].num);
            st->gs.fill_r = v;
            st->gs.fill_g = v;
            st->gs.fill_b = v;
        }
    } else if (strcmp(op, "G") == 0) {
        if (n >= 1 && pdf_expect_num(&ops[n - 1])) {
            unsigned char v = pdf_clamp_u8(ops[n - 1].num);
            st->gs.stroke_r = v;
            st->gs.stroke_g = v;
            st->gs.stroke_b = v;
        }
    } else if (strcmp(op, "w") == 0) {
        if (n >= 1 && pdf_expect_num(&ops[n - 1])) {
            float w = (float)ops[n - 1].num;
            if (w > 0.0f && w < 1000.0f) {
                st->gs.line_width_pts = w;
            }
        }
    } else if (strcmp(op, "cm") == 0) {
        if (n >= 6 &&
            pdf_expect_num(&ops[n - 6]) && pdf_expect_num(&ops[n - 5]) &&
            pdf_expect_num(&ops[n - 4]) && pdf_expect_num(&ops[n - 3]) &&
            pdf_expect_num(&ops[n - 2]) && pdf_expect_num(&ops[n - 1])) {
            float m[6];
            m[0] = (float)ops[n - 6].num;
            m[1] = (float)ops[n - 5].num;
            m[2] = (float)ops[n - 4].num;
            m[3] = (float)ops[n - 3].num;
            m[4] = (float)ops[n - 2].num;
            m[5] = (float)ops[n - 1].num;
            pdf_matrix_concat(st->gs.ctm, m);
        }
    } else if (strcmp(op, "q") == 0) {
        if (st->gs_stack_len < (int)(sizeof(st->gs_stack) / sizeof(st->gs_stack[0]))) {
            st->gs_stack[st->gs_stack_len++] = st->gs;
        }
    } else if (strcmp(op, "Q") == 0) {
        if (st->gs_stack_len > 0) {
            st->gs = st->gs_stack[--st->gs_stack_len];
        }
    } else if (strcmp(op, "m") == 0) {
        if (n >= 2 && pdf_expect_num(&ops[n - 2]) && pdf_expect_num(&ops[n - 1])) {
            pdf_path_move_to(&st->path, (float)ops[n - 2].num, (float)ops[n - 1].num);
        }
    } else if (strcmp(op, "l") == 0) {
        if (n >= 2 && pdf_expect_num(&ops[n - 2]) && pdf_expect_num(&ops[n - 1])) {
            pdf_path_line_to(&st->path, (float)ops[n - 2].num, (float)ops[n - 1].num);
        }
    } else if (strcmp(op, "h") == 0) {
        pdf_path_close(&st->path);
    } else if (strcmp(op, "re") == 0) {
        if (n >= 4 &&
            pdf_expect_num(&ops[n - 4]) && pdf_expect_num(&ops[n - 3]) &&
            pdf_expect_num(&ops[n - 2]) && pdf_expect_num(&ops[n - 1])) {
            float x = (float)ops[n - 4].num;
            float y = (float)ops[n - 3].num;
            float w = (float)ops[n - 2].num;
            float h = (float)ops[n - 1].num;
            pdf_path_move_to(&st->path, x, y);
            pdf_path_line_to(&st->path, x + w, y);
            pdf_path_line_to(&st->path, x + w, y + h);
            pdf_path_line_to(&st->path, x, y + h);
            pdf_path_close(&st->path);
        }
    } else if (strcmp(op, "n") == 0) {
        pdf_path_clear(&st->path);
    } else if (strcmp(op, "S") == 0) {
        pdf_stroke_path(st);
        pdf_path_clear(&st->path);
    } else if (strcmp(op, "s") == 0) {
        pdf_path_close(&st->path);
        pdf_stroke_path(st);
        pdf_path_clear(&st->path);
    } else if (strcmp(op, "f") == 0 || strcmp(op, "f*") == 0) {
        pdf_fill_path(st);
        pdf_path_clear(&st->path);
    } else if (strcmp(op, "B") == 0 || strcmp(op, "B*") == 0) {
        pdf_fill_path(st);
        pdf_stroke_path(st);
        pdf_path_clear(&st->path);
    } else if (strcmp(op, "b") == 0 || strcmp(op, "b*") == 0) {
        pdf_path_close(&st->path);
        pdf_fill_path(st);
        pdf_stroke_path(st);
        pdf_path_clear(&st->path);
    } else if (strcmp(op, "W") == 0 || strcmp(op, "W*") == 0) {
        /* Clipping is ignored in this minimal renderer. */
    } else if (strcmp(op, "cs") == 0 || strcmp(op, "CS") == 0 || strcmp(op, "sc") == 0 ||
               strcmp(op, "SC") == 0 || strcmp(op, "scn") == 0 || strcmp(op, "SCN") == 0) {
        /* Color space operators are ignored; direct gray/RGB operators are handled above. */
    } else if (strcmp(op, "Tj") == 0) {
        if (n >= 1 && ops[n - 1].type == PDF_OPERAND_STRING) {
            pdf_render_text_string(st, ops[n - 1].str, ops[n - 1].len);
        }
    } else if (strcmp(op, "TJ") == 0) {
        if (n >= 1 && ops[n - 1].type == PDF_OPERAND_ARRAY) {
            pdf_render_tj_array(st, ops[n - 1].str, ops[n - 1].len);
        }
    } else if (strcmp(op, "'") == 0) {
        if (n >= 1 && ops[n - 1].type == PDF_OPERAND_STRING) {
            float lead = (st->leading_pts > 0.0f) ? st->leading_pts : st->font_size_pts * 1.2f;
            st->line_y_pts -= lead;
            st->text_x_pts = st->line_x_pts;
            st->text_y_pts = st->line_y_pts;
            pdf_render_text_string(st, ops[n - 1].str, ops[n - 1].len);
        }
    } else if (strcmp(op, "\"") == 0) {
        if (n >= 3 && ops[n - 1].type == PDF_OPERAND_STRING) {
            float lead = (st->leading_pts > 0.0f) ? st->leading_pts : st->font_size_pts * 1.2f;
            st->line_y_pts -= lead;
            st->text_x_pts = st->line_x_pts;
            st->text_y_pts = st->line_y_pts;
            pdf_render_text_string(st, ops[n - 1].str, ops[n - 1].len);
        }
    }

    pdf_clear_operands(ops, count);
}

static int pdf_render_content_stream(const unsigned char *stream, size_t stream_len,
                                     cupidimage_image *img, int page_height_pts,
                                     char *err, size_t errcap) {
    pdf_render_state st;
    st.img = img;
    st.page_height_pts = page_height_pts;
    st.line_x_pts = 0.0f;
    st.line_y_pts = 0.0f;
    st.text_x_pts = 0.0f;
    st.text_y_pts = 0.0f;
    st.font_size_pts = 12.0f;
    st.leading_pts = 0.0f;
    pdf_matrix_identity(st.gs.ctm);
    st.gs.fill_r = 0;
    st.gs.fill_g = 0;
    st.gs.fill_b = 0;
    st.gs.stroke_r = 0;
    st.gs.stroke_g = 0;
    st.gs.stroke_b = 0;
    st.gs.line_width_pts = 1.0f;
    st.gs_stack_len = 0;
    pdf_path_clear(&st.path);
    st.in_text = 0;

    pdf_operand ops[64];
    int op_count = 0;
    memset(ops, 0, sizeof(ops));

    size_t pos = 0;
    while (pos < stream_len) {
        pdf_skip_ws_and_comments(stream, stream_len, &pos);
        if (pos >= stream_len) {
            break;
        }

        unsigned char c = stream[pos];

        if (c == '(') {
            char *txt = NULL;
            size_t txt_len = 0;
            if (!pdf_parse_literal_string(stream, stream_len, &pos, &txt, &txt_len)) {
                set_err(err, errcap, "pdf: malformed content string");
                pdf_clear_operands(ops, &op_count);
                return 0;
            }
            if (op_count < (int)(sizeof(ops) / sizeof(ops[0]))) {
                ops[op_count].type = PDF_OPERAND_STRING;
                ops[op_count].str = txt;
                ops[op_count].len = txt_len;
                op_count++;
            } else {
                free(txt);
            }
            continue;
        }

        if (c == '<' && !(pos + 1 < stream_len && stream[pos + 1] == '<')) {
            char *txt = NULL;
            size_t txt_len = 0;
            if (!pdf_parse_hex_string(stream, stream_len, &pos, &txt, &txt_len)) {
                set_err(err, errcap, "pdf: malformed hex string");
                pdf_clear_operands(ops, &op_count);
                return 0;
            }
            if (op_count < (int)(sizeof(ops) / sizeof(ops[0]))) {
                ops[op_count].type = PDF_OPERAND_STRING;
                ops[op_count].str = txt;
                ops[op_count].len = txt_len;
                op_count++;
            } else {
                free(txt);
            }
            continue;
        }

        if (c == '[') {
            char *arr = NULL;
            size_t arr_len = 0;
            if (!pdf_parse_array_token(stream, stream_len, &pos, &arr, &arr_len)) {
                set_err(err, errcap, "pdf: malformed array token");
                pdf_clear_operands(ops, &op_count);
                return 0;
            }
            if (op_count < (int)(sizeof(ops) / sizeof(ops[0]))) {
                ops[op_count].type = PDF_OPERAND_ARRAY;
                ops[op_count].str = arr;
                ops[op_count].len = arr_len;
                op_count++;
            } else {
                free(arr);
            }
            continue;
        }

        if (c == '/') {
            char *name = NULL;
            size_t name_len = 0;
            if (!pdf_parse_name_token(stream, stream_len, &pos, &name, &name_len)) {
                set_err(err, errcap, "pdf: malformed name token");
                pdf_clear_operands(ops, &op_count);
                return 0;
            }
            if (op_count < (int)(sizeof(ops) / sizeof(ops[0]))) {
                ops[op_count].type = PDF_OPERAND_NAME;
                ops[op_count].str = name;
                ops[op_count].len = name_len;
                op_count++;
            } else {
                free(name);
            }
            continue;
        }

        if (c == '+' || c == '-' || c == '.' || (c >= '0' && c <= '9')) {
            double num = 0.0;
            if (pdf_parse_number_token(stream, stream_len, &pos, &num)) {
                if (op_count < (int)(sizeof(ops) / sizeof(ops[0]))) {
                    ops[op_count].type = PDF_OPERAND_NUM;
                    ops[op_count].num = num;
                    ops[op_count].str = NULL;
                    ops[op_count].len = 0;
                    op_count++;
                }
                continue;
            }
        }

        size_t start = pos;
        while (pos < stream_len && !pdf_is_ws(stream[pos]) && !pdf_is_delim(stream[pos])) {
            pos++;
        }
        if (pos > start) {
            size_t tok_len = pos - start;
            char opbuf[16];
            if (tok_len >= sizeof(opbuf)) {
                tok_len = sizeof(opbuf) - 1;
            }
            memcpy(opbuf, &stream[start], tok_len);
            opbuf[tok_len] = '\0';
            pdf_execute_operator(&st, opbuf, ops, &op_count);
            continue;
        }

        pos++;
    }

    pdf_clear_operands(ops, &op_count);
    return 1;
}

static int cupidimage_load_pdf_internal(const unsigned char *data, size_t size,
                                        cupidimage_image *img,
                                        int page_index,
                                        char *err, size_t errcap) {
    if (!data || !img) {
        set_err(err, errcap, "pdf: null pointer");
        return 0;
    }
    if (!pdf_check_header(data, size)) {
        set_err(err, errcap, "pdf: invalid header");
        return 0;
    }

    size_t eof_pos = 0;
    if (!find_eof_marker(data, size, &eof_pos)) {
        set_err(err, errcap, "pdf: EOF marker not found");
        return 0;
    }

    size_t xref_offset = 0;
    if (!find_startxref(data, eof_pos, &xref_offset)) {
        set_err(err, errcap, "pdf: startxref not found");
        return 0;
    }

    pdf_xref xref = {0};
    if (!parse_xref_table(data, size, xref_offset, &xref, err, errcap)) {
        return 0;
    }

    if (!parse_trailer(data, size, xref_offset, &xref, err, errcap)) {
        free(xref.entries);
        return 0;
    }

    int pages_obj = 0;
    if (!get_pages_obj(data, size, &xref, &pages_obj, err, errcap)) {
        free(xref.entries);
        return 0;
    }

    int page_obj = 0;
    if (!get_page_obj_by_index(data, size, &xref, pages_obj, page_index, &page_obj, err, errcap)) {
        free(xref.entries);
        return 0;
    }

    int page_width_pts = 0, page_height_pts = 0;
    if (!get_page_mediabox(data, size, &xref, page_obj, &page_width_pts, &page_height_pts, err, errcap)) {
        free(xref.entries);
        return 0;
    }

    int width = (page_width_pts * 96) / 72;
    int height = (page_height_pts * 96) / 72;
    if (width <= 0 || height <= 0) {
        free(xref.entries);
        set_err(err, errcap, "pdf: invalid page dimensions");
        return 0;
    }

    size_t pixel_count = (size_t)width * (size_t)height;
    if (height > 0 && pixel_count / (size_t)height != (size_t)width) {
        free(xref.entries);
        set_err(err, errcap, "pdf: image too large");
        return 0;
    }
    if (pixel_count > SIZE_MAX / 4u) {
        free(xref.entries);
        set_err(err, errcap, "pdf: image too large");
        return 0;
    }

    img->width = (uint32_t)width;
    img->height = (uint32_t)height;
    img->rgba = (unsigned char *)calloc(pixel_count * 4u, 1);
    if (!img->rgba) {
        free(xref.entries);
        set_err(err, errcap, "pdf: out of memory");
        return 0;
    }

    for (int i = 0; i < width * height; i++) {
        img->rgba[i * 4 + 0] = 255;
        img->rgba[i * 4 + 1] = 255;
        img->rgba[i * 4 + 2] = 255;
        img->rgba[i * 4 + 3] = 255;
    }

    unsigned char *stream_data = NULL;
    size_t stream_len = 0;
    int is_flate = 0;
    if (!get_page_content_stream(data, size, &xref, page_obj, &stream_data, &stream_len, &is_flate, err, errcap)) {
        free(xref.entries);
        free(img->rgba);
        img->rgba = NULL;
        return 0;
    }

    if (stream_data && stream_len > 0) {
        const unsigned char *render_data = stream_data;
        size_t render_len = stream_len;
        unsigned char *inflated = NULL;

        if (is_flate) {
            size_t inflated_len = 0;
            if (!pdf_inflate_auto(stream_data, stream_len, &inflated, &inflated_len)) {
                free(stream_data);
                free(xref.entries);
                free(img->rgba);
                img->rgba = NULL;
                set_err(err, errcap, "pdf: failed to decode Flate stream");
                return 0;
            }
            render_data = inflated;
            render_len = inflated_len;
        }

        if (!pdf_render_content_stream(render_data, render_len, img, page_height_pts, err, errcap)) {
            free(inflated);
            free(stream_data);
            free(xref.entries);
            free(img->rgba);
            img->rgba = NULL;
            return 0;
        }

        free(inflated);
        free(stream_data);
    }

    pdf_trim_whitespace(img);

    img->hotspot_x = 0;
    img->hotspot_y = 0;

    free(xref.entries);
    return 1;
}

int cupidimage_load_pdf(const unsigned char *data, size_t size,
                        cupidimage_image *img, char *err, size_t errcap) {
    return cupidimage_load_pdf_internal(data, size, img, 0, err, errcap);
}

int cupidimage_load_pdf_page(const unsigned char *data, size_t size,
                             cupidimage_image *img, int page_index,
                             char *err, size_t errcap) {
    return cupidimage_load_pdf_internal(data, size, img, page_index, err, errcap);
}

int cupidimage_get_pdf_page_count(const unsigned char *data, size_t size,
                                  int *count, char *err, size_t errcap) {
    if (!data || !count) {
        set_err(err, errcap, "pdf: null pointer");
        return 0;
    }
    if (!pdf_check_header(data, size)) {
        set_err(err, errcap, "pdf: invalid header");
        return 0;
    }

    size_t eof_pos = 0;
    if (!find_eof_marker(data, size, &eof_pos)) {
        set_err(err, errcap, "pdf: EOF marker not found");
        return 0;
    }

    size_t xref_offset = 0;
    if (!find_startxref(data, eof_pos, &xref_offset)) {
        set_err(err, errcap, "pdf: startxref not found");
        return 0;
    }

    pdf_xref xref = {0};
    if (!parse_xref_table(data, size, xref_offset, &xref, err, errcap)) {
        return 0;
    }

    if (!parse_trailer(data, size, xref_offset, &xref, err, errcap)) {
        free(xref.entries);
        return 0;
    }

    int pages_obj = 0;
    if (!get_pages_obj(data, size, &xref, &pages_obj, err, errcap)) {
        free(xref.entries);
        return 0;
    }

    int page_count = 0;
    if (!get_pages_count_from_obj(data, size, pages_obj, &page_count, err, errcap)) {
        free(xref.entries);
        return 0;
    }

    *count = page_count;
    free(xref.entries);
    return 1;
}

int cupidimage_load_pdf_page_file(const char *path, cupidimage_image *img,
                                  int page_index,
                                  char *err, size_t errcap) {
    unsigned char *buf = NULL;
    size_t size = 0;
    if (!cupidimage_read_file_bytes(path, &buf, &size, err, errcap)) {
        return 0;
    }
    int ok = cupidimage_load_pdf_page(buf, size, img, page_index, err, errcap);
    free(buf);
    return ok;
}

int cupidimage_get_pdf_page_count_file(const char *path,
                                       int *count,
                                       char *err, size_t errcap) {
    unsigned char *buf = NULL;
    size_t size = 0;
    if (!cupidimage_read_file_bytes(path, &buf, &size, err, errcap)) {
        return 0;
    }
    int ok = cupidimage_get_pdf_page_count(buf, size, count, err, errcap);
    free(buf);
    return ok;
}

int cupidimage_load_pdf_file(const char *path, cupidimage_image *img,
                             char *err, size_t errcap) {
    return cupidimage_load_pdf_page_file(path, img, 0, err, errcap);
}
