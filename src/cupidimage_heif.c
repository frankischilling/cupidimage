#include "cupidimage.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <limits.h>
#include <math.h>

#if defined(__GNUC__)
#define HEIF_UNUSED __attribute__((unused))
#else
#define HEIF_UNUSED
#endif

static void set_err(char *err, size_t errcap, const char *msg) {
    if (err && errcap) {
        snprintf(err, errcap, "%s", msg);
    }
}

static int heif_debug_enabled(void) {
    const char *val = getenv("CUPIDIMAGE_HEIF_DEBUG");
    return val && val[0] != '\0' && strcmp(val, "0") != 0;
}

static void heif_debugf(const char *fmt, ...) {
    if (!heif_debug_enabled()) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

static uint16_t read_be16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint64_t read_be64(const uint8_t *p) {
    return ((uint64_t)read_be32(p) << 32) | (uint64_t)read_be32(p + 4);
}

#define HEIF_FOURCC(a, b, c, d) \
    ((uint32_t)(uint8_t)(a) << 24 | (uint32_t)(uint8_t)(b) << 16 | \
     (uint32_t)(uint8_t)(c) << 8 | (uint32_t)(uint8_t)(d))

typedef struct heif_box {
    uint64_t size;
    uint32_t type;
    size_t header_size;
} heif_box;

typedef struct heif_extent {
    uint64_t offset;
    uint64_t length;
} heif_extent;

typedef struct heif_item_location {
    uint32_t item_id;
    uint16_t data_reference_index;
    uint16_t construction_method;
    uint16_t extent_count;
    uint64_t base_offset;
    heif_extent *extents;
} heif_item_location;

typedef struct heif_item_info {
    uint32_t item_id;
    char item_type[5];
} heif_item_info;

typedef struct heif_property {
    uint32_t type;
    const uint8_t *data;
    size_t size;
} heif_property;

typedef struct heif_item_props {
    uint32_t item_id;
    uint16_t *prop_indices;
    uint16_t prop_count;
} heif_item_props;

typedef struct heif_item_ref {
    uint32_t from_id;
    uint32_t type;
    uint32_t *to_ids;
    uint16_t to_count;
} heif_item_ref;

typedef struct heif_context {
    heif_item_location *locs;
    size_t loc_count;
    heif_item_info *infos;
    size_t info_count;
    size_t info_cap;
    heif_property *props;
    size_t prop_count;
    size_t prop_cap;
    heif_item_props *item_props;
    size_t item_props_count;
    heif_item_ref *refs;
    size_t ref_count;
    size_t ref_cap;
    uint32_t primary_item_id;
    int has_pitm;
    const uint8_t *idat;
    size_t idat_size;
    int is_heif;
} heif_context;

static void heif_free_context(heif_context *ctx) {
    if (!ctx) {
        return;
    }
    if (ctx->locs) {
        for (size_t i = 0; i < ctx->loc_count; i++) {
            free(ctx->locs[i].extents);
        }
    }
    if (ctx->item_props) {
        for (size_t i = 0; i < ctx->item_props_count; i++) {
            free(ctx->item_props[i].prop_indices);
        }
    }
    free(ctx->locs);
    free(ctx->infos);
    free(ctx->props);
    free(ctx->item_props);
    if (ctx->refs) {
        for (size_t i = 0; i < ctx->ref_count; i++) {
            free(ctx->refs[i].to_ids);
        }
    }
    free(ctx->refs);
    memset(ctx, 0, sizeof(*ctx));
}

static void heif_free_locs(heif_item_location *locs, size_t count) {
    if (!locs) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        free(locs[i].extents);
    }
    free(locs);
}

static int read_box_header(const uint8_t *data, size_t size, size_t pos, heif_box *box) {
    if (!box || pos + 8 > size) {
        return 0;
    }
    uint32_t small_size = read_be32(data + pos);
    uint32_t type = read_be32(data + pos + 4);
    uint64_t box_size = small_size;
    size_t header_size = 8;
    if (small_size == 1) {
        if (pos + 16 > size) {
            return 0;
        }
        box_size = read_be64(data + pos + 8);
        header_size = 16;
    } else if (small_size == 0) {
        box_size = (uint64_t)(size - pos);
    }
    if (box_size < header_size || box_size > (uint64_t)(size - pos)) {
        return 0;
    }
    box->size = box_size;
    box->type = type;
    box->header_size = header_size;
    return 1;
}

static int is_heif_brand(uint32_t brand) {
    switch (brand) {
        case HEIF_FOURCC('h', 'e', 'i', 'c'):
        case HEIF_FOURCC('h', 'e', 'i', 'x'):
        case HEIF_FOURCC('h', 'e', 'v', 'c'):
        case HEIF_FOURCC('h', 'e', 'v', 'x'):
        case HEIF_FOURCC('m', 'i', 'f', '1'):
            return 1;
        default:
            return 0;
    }
}

static int parse_ftyp(const uint8_t *data, size_t size, heif_context *ctx, char *err, size_t errcap) {
    if (size < 8) {
        set_err(err, errcap, "invalid HEIF ftyp");
        return 0;
    }
    uint32_t major = read_be32(data);
    if (is_heif_brand(major)) {
        ctx->is_heif = 1;
        return 1;
    }
    size_t pos = 8;
    while (pos + 4 <= size) {
        uint32_t brand = read_be32(data + pos);
        if (is_heif_brand(brand)) {
            ctx->is_heif = 1;
            return 1;
        }
        pos += 4;
    }
    return 1;
}

static int heif_add_item_info(heif_context *ctx, const heif_item_info *info, char *err, size_t errcap) {
    if (ctx->info_count == ctx->info_cap) {
        size_t new_cap = ctx->info_cap ? ctx->info_cap * 2 : 8;
        if (new_cap < ctx->info_count + 1) {
            new_cap = ctx->info_count + 1;
        }
        if (new_cap > SIZE_MAX / sizeof(*ctx->infos)) {
            set_err(err, errcap, "HEIF item info overflow");
            return 0;
        }
        heif_item_info *next = (heif_item_info *)realloc(ctx->infos, new_cap * sizeof(*ctx->infos));
        if (!next) {
            set_err(err, errcap, "out of memory");
            return 0;
        }
        ctx->infos = next;
        ctx->info_cap = new_cap;
    }
    ctx->infos[ctx->info_count++] = *info;
    return 1;
}

static int heif_add_property(heif_context *ctx, const heif_property *prop, char *err, size_t errcap) {
    if (ctx->prop_count == ctx->prop_cap) {
        size_t new_cap = ctx->prop_cap ? ctx->prop_cap * 2 : 8;
        if (new_cap < ctx->prop_count + 1) {
            new_cap = ctx->prop_count + 1;
        }
        if (new_cap > SIZE_MAX / sizeof(*ctx->props)) {
            set_err(err, errcap, "HEIF property overflow");
            return 0;
        }
        heif_property *next = (heif_property *)realloc(ctx->props, new_cap * sizeof(*ctx->props));
        if (!next) {
            set_err(err, errcap, "out of memory");
            return 0;
        }
        ctx->props = next;
        ctx->prop_cap = new_cap;
    }
    ctx->props[ctx->prop_count++] = *prop;
    return 1;
}

static heif_item_props *heif_find_item_props(heif_context *ctx, uint32_t item_id) {
    for (size_t i = 0; i < ctx->item_props_count; i++) {
        if (ctx->item_props[i].item_id == item_id) {
            return &ctx->item_props[i];
        }
    }
    return NULL;
}

static const heif_item_props *heif_find_item_props_const(const heif_context *ctx, uint32_t item_id) {
    for (size_t i = 0; i < ctx->item_props_count; i++) {
        if (ctx->item_props[i].item_id == item_id) {
            return &ctx->item_props[i];
        }
    }
    return NULL;
}

static int heif_add_item_prop_index(heif_context *ctx, uint32_t item_id, uint16_t prop_index,
                                    char *err, size_t errcap) {
    if (prop_index == 0) {
        return 1;
    }
    heif_item_props *entry = heif_find_item_props(ctx, item_id);
    if (!entry) {
        size_t new_count = ctx->item_props_count + 1;
        if (new_count > SIZE_MAX / sizeof(*ctx->item_props)) {
            set_err(err, errcap, "HEIF item props overflow");
            return 0;
        }
        heif_item_props *next = (heif_item_props *)realloc(ctx->item_props, new_count * sizeof(*ctx->item_props));
        if (!next) {
            set_err(err, errcap, "out of memory");
            return 0;
        }
        ctx->item_props = next;
        entry = &ctx->item_props[ctx->item_props_count++];
        memset(entry, 0, sizeof(*entry));
        entry->item_id = item_id;
    }
    if (entry->prop_count >= UINT16_MAX) {
        set_err(err, errcap, "HEIF item property list too large");
        return 0;
    }
    uint16_t *next_indices = (uint16_t *)realloc(entry->prop_indices,
                                                (size_t)(entry->prop_count + 1) * sizeof(*entry->prop_indices));
    if (!next_indices) {
        set_err(err, errcap, "out of memory");
        return 0;
    }
    entry->prop_indices = next_indices;
    entry->prop_indices[entry->prop_count++] = prop_index;
    return 1;
}

static int heif_add_item_ref(heif_context *ctx, uint32_t from_id, uint32_t type,
                             const uint32_t *to_ids, uint16_t to_count,
                             char *err, size_t errcap) {
    if (!ctx || !to_ids || to_count == 0) {
        return 1;
    }
    for (size_t i = 0; i < ctx->ref_count; i++) {
        if (ctx->refs[i].from_id == from_id && ctx->refs[i].type == type) {
            size_t new_count = (size_t)ctx->refs[i].to_count + to_count;
            if (new_count > UINT16_MAX || new_count > SIZE_MAX / sizeof(*ctx->refs[i].to_ids)) {
                set_err(err, errcap, "HEIF ref count too large");
                return 0;
            }
            uint32_t *next = (uint32_t *)realloc(ctx->refs[i].to_ids, new_count * sizeof(*next));
            if (!next) {
                set_err(err, errcap, "out of memory");
                return 0;
            }
            memcpy(next + ctx->refs[i].to_count, to_ids, (size_t)to_count * sizeof(*to_ids));
            ctx->refs[i].to_ids = next;
            ctx->refs[i].to_count = (uint16_t)new_count;
            return 1;
        }
    }
    if (ctx->ref_count == ctx->ref_cap) {
        size_t next_cap = ctx->ref_cap ? ctx->ref_cap * 2 : 8;
        heif_item_ref *next = (heif_item_ref *)realloc(ctx->refs, next_cap * sizeof(*next));
        if (!next) {
            set_err(err, errcap, "out of memory");
            return 0;
        }
        ctx->refs = next;
        ctx->ref_cap = next_cap;
    }
    heif_item_ref *ref = &ctx->refs[ctx->ref_count++];
    memset(ref, 0, sizeof(*ref));
    ref->from_id = from_id;
    ref->type = type;
    ref->to_ids = (uint32_t *)malloc((size_t)to_count * sizeof(*ref->to_ids));
    if (!ref->to_ids) {
        set_err(err, errcap, "out of memory");
        return 0;
    }
    memcpy(ref->to_ids, to_ids, (size_t)to_count * sizeof(*to_ids));
    ref->to_count = to_count;
    return 1;
}

static const heif_item_ref *heif_find_item_ref(const heif_context *ctx, uint32_t from_id, uint32_t type) {
    if (!ctx || !ctx->refs) {
        return NULL;
    }
    for (size_t i = 0; i < ctx->ref_count; i++) {
        if (ctx->refs[i].from_id == from_id && ctx->refs[i].type == type) {
            return &ctx->refs[i];
        }
    }
    return NULL;
}

static int parse_infe(const uint8_t *data, size_t size, heif_context *ctx, char *err, size_t errcap) {
    if (size < 4) {
        set_err(err, errcap, "invalid HEIF infe");
        return 0;
    }
    uint8_t version = data[0];
    size_t pos = 4;
    heif_item_info info;
    memset(&info, 0, sizeof(info));
    if (version == 0 || version == 1) {
        if (pos + 4 > size) {
            set_err(err, errcap, "truncated HEIF infe");
            return 0;
        }
        info.item_id = read_be16(data + pos);
        pos += 4;
        info.item_type[0] = '\0';
    } else {
        if (version == 2) {
            if (pos + 2 > size) {
                set_err(err, errcap, "truncated HEIF infe");
                return 0;
            }
            info.item_id = read_be16(data + pos);
            pos += 2;
        } else {
            if (pos + 4 > size) {
                set_err(err, errcap, "truncated HEIF infe");
                return 0;
            }
            info.item_id = read_be32(data + pos);
            pos += 4;
        }
        if (pos + 2 + 4 > size) {
            set_err(err, errcap, "truncated HEIF infe");
            return 0;
        }
        pos += 2;
        memcpy(info.item_type, data + pos, 4);
        info.item_type[4] = '\0';
    }
    int ok = heif_add_item_info(ctx, &info, err, errcap);
    if (ok && info.item_type[0] != '\0') {
        heif_debugf("heif infe: item_id=%u type=%.4s\n", info.item_id, info.item_type);
    } else if (ok) {
        heif_debugf("heif infe: item_id=%u type=\"\"\n", info.item_id);
    }
    return ok;
}

static int parse_iinf(const uint8_t *data, size_t size, heif_context *ctx, char *err, size_t errcap) {
    if (size < 4) {
        set_err(err, errcap, "invalid HEIF iinf");
        return 0;
    }
    uint8_t version = data[0];
    size_t pos = 4;
    if (version == 0) {
        if (pos + 2 > size) {
            set_err(err, errcap, "truncated HEIF iinf");
            return 0;
        }
        pos += 2;
    } else {
        if (pos + 4 > size) {
            set_err(err, errcap, "truncated HEIF iinf");
            return 0;
        }
        pos += 4;
    }
    while (pos + 8 <= size) {
        heif_box box;
        if (!read_box_header(data, size, pos, &box)) {
            set_err(err, errcap, "invalid HEIF box");
            return 0;
        }
        if (box.type == HEIF_FOURCC('i', 'n', 'f', 'e')) {
            size_t payload_pos = pos + box.header_size;
            size_t payload_size = (size_t)(box.size - box.header_size);
            if (!parse_infe(data + payload_pos, payload_size, ctx, err, errcap)) {
                return 0;
            }
        }
        pos += (size_t)box.size;
    }
    return 1;
}

static int parse_ipco(const uint8_t *data, size_t size, heif_context *ctx, char *err, size_t errcap) {
    size_t pos = 0;
    while (pos + 8 <= size) {
        heif_box box;
        if (!read_box_header(data, size, pos, &box)) {
            set_err(err, errcap, "invalid HEIF box");
            return 0;
        }
        size_t payload_pos = pos + box.header_size;
        size_t payload_size = (size_t)(box.size - box.header_size);
        heif_property prop;
        prop.type = box.type;
        prop.data = data + payload_pos;
        prop.size = payload_size;
        if (!heif_add_property(ctx, &prop, err, errcap)) {
            return 0;
        }
        pos += (size_t)box.size;
    }
    return 1;
}

static int parse_ipma(const uint8_t *data, size_t size, heif_context *ctx, char *err, size_t errcap) {
    if (size < 4) {
        set_err(err, errcap, "invalid HEIF ipma");
        return 0;
    }
    uint8_t version = data[0];
    uint32_t flags = ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) | (uint32_t)data[3];
    size_t pos = 4;
    uint32_t entry_count = 0;
    if (version == 0) {
        if (pos + 2 > size) {
            set_err(err, errcap, "truncated HEIF ipma");
            return 0;
        }
        entry_count = read_be16(data + pos);
        pos += 2;
    } else {
        if (pos + 4 > size) {
            set_err(err, errcap, "truncated HEIF ipma");
            return 0;
        }
        entry_count = read_be32(data + pos);
        pos += 4;
    }
    int index_size_16 = (flags & 1u) != 0;
    heif_debugf("heif ipma: version=%u flags=0x%06x entry_count=%u index_size=%d\n",
                version, flags, entry_count, index_size_16 ? 16 : 8);
    for (uint32_t i = 0; i < entry_count; i++) {
        uint32_t item_id = 0;
        if (version == 0) {
            if (pos + 2 > size) {
                set_err(err, errcap, "truncated HEIF ipma");
                return 0;
            }
            item_id = read_be16(data + pos);
            pos += 2;
        } else {
            if (pos + 4 > size) {
                set_err(err, errcap, "truncated HEIF ipma");
                return 0;
            }
            item_id = read_be32(data + pos);
            pos += 4;
        }
        if (pos + 1 > size) {
            set_err(err, errcap, "truncated HEIF ipma");
            return 0;
        }
        uint8_t association_count = data[pos++];
        heif_debugf("heif ipma: item_id=%u association_count=%u\n", item_id, association_count);
        for (uint8_t j = 0; j < association_count; j++) {
            uint16_t raw_index = 0;
            if (index_size_16) {
                if (pos + 2 > size) {
                    set_err(err, errcap, "truncated HEIF ipma");
                    return 0;
                }
                raw_index = read_be16(data + pos);
                pos += 2;
                raw_index &= 0x7FFFu;
            } else {
                if (pos + 1 > size) {
                    set_err(err, errcap, "truncated HEIF ipma");
                    return 0;
                }
                raw_index = data[pos++];
                raw_index &= 0x7Fu;
            }
            heif_debugf("heif ipma: item_id=%u raw_index=%u\n", item_id, raw_index);
            if (!heif_add_item_prop_index(ctx, item_id, raw_index, err, errcap)) {
                return 0;
            }
        }
    }
    return 1;
}

static int parse_iprp(const uint8_t *data, size_t size, heif_context *ctx, char *err, size_t errcap) {
    size_t pos = 0;
    while (pos + 8 <= size) {
        heif_box box;
        if (!read_box_header(data, size, pos, &box)) {
            set_err(err, errcap, "invalid HEIF box");
            return 0;
        }
        size_t payload_pos = pos + box.header_size;
        size_t payload_size = (size_t)(box.size - box.header_size);
        if (box.type == HEIF_FOURCC('i', 'p', 'c', 'o')) {
            if (!parse_ipco(data + payload_pos, payload_size, ctx, err, errcap)) {
                return 0;
            }
        } else if (box.type == HEIF_FOURCC('i', 'p', 'm', 'a')) {
            if (!parse_ipma(data + payload_pos, payload_size, ctx, err, errcap)) {
                return 0;
            }
        }
        pos += (size_t)box.size;
    }
    return 1;
}

static int parse_iref_box(const uint8_t *data, size_t size, uint32_t type, uint8_t version,
                          heif_context *ctx, char *err, size_t errcap) {
    uint32_t from_id = 0;
    uint16_t ref_count = 0;
    size_t pos = 0;
    if (version == 0) {
        if (size < 4) {
            set_err(err, errcap, "truncated HEIF iref");
            return 0;
        }
        from_id = read_be16(data + pos);
        pos += 2;
        ref_count = read_be16(data + pos);
        pos += 2;
        if (pos + (size_t)ref_count * 2u > size) {
            set_err(err, errcap, "truncated HEIF iref");
            return 0;
        }
        if (ref_count == 0) {
            return 1;
        }
        uint32_t *to_ids = (uint32_t *)malloc((size_t)ref_count * sizeof(*to_ids));
        if (!to_ids) {
            set_err(err, errcap, "out of memory");
            return 0;
        }
        for (uint16_t i = 0; i < ref_count; i++) {
            to_ids[i] = read_be16(data + pos);
            pos += 2;
        }
        int ok = heif_add_item_ref(ctx, from_id, type, to_ids, ref_count, err, errcap);
        free(to_ids);
        return ok;
    }
    if (version == 1) {
        if (size < 6) {
            set_err(err, errcap, "truncated HEIF iref");
            return 0;
        }
        from_id = read_be32(data + pos);
        pos += 4;
        ref_count = read_be16(data + pos);
        pos += 2;
        if (pos + (size_t)ref_count * 4u > size) {
            set_err(err, errcap, "truncated HEIF iref");
            return 0;
        }
        if (ref_count == 0) {
            return 1;
        }
        uint32_t *to_ids = (uint32_t *)malloc((size_t)ref_count * sizeof(*to_ids));
        if (!to_ids) {
            set_err(err, errcap, "out of memory");
            return 0;
        }
        for (uint16_t i = 0; i < ref_count; i++) {
            to_ids[i] = read_be32(data + pos);
            pos += 4;
        }
        int ok = heif_add_item_ref(ctx, from_id, type, to_ids, ref_count, err, errcap);
        free(to_ids);
        return ok;
    }
    if (version == 2) {
        if (size < 10) {
            set_err(err, errcap, "truncated HEIF iref");
            return 0;
        }
        uint64_t from64 = read_be64(data + pos);
        pos += 8;
        ref_count = read_be16(data + pos);
        pos += 2;
        if (pos + (size_t)ref_count * 8u > size) {
            set_err(err, errcap, "truncated HEIF iref");
            return 0;
        }
        if (from64 > UINT32_MAX) {
            set_err(err, errcap, "unsupported HEIF item id");
            return 0;
        }
        from_id = (uint32_t)from64;
        if (ref_count == 0) {
            return 1;
        }
        uint32_t *to_ids = (uint32_t *)malloc((size_t)ref_count * sizeof(*to_ids));
        if (!to_ids) {
            set_err(err, errcap, "out of memory");
            return 0;
        }
        for (uint16_t i = 0; i < ref_count; i++) {
            uint64_t to64 = read_be64(data + pos);
            pos += 8;
            if (to64 > UINT32_MAX) {
                free(to_ids);
                set_err(err, errcap, "unsupported HEIF item id");
                return 0;
            }
            to_ids[i] = (uint32_t)to64;
        }
        int ok = heif_add_item_ref(ctx, from_id, type, to_ids, ref_count, err, errcap);
        free(to_ids);
        return ok;
    }
    set_err(err, errcap, "unsupported HEIF iref version");
    return 0;
}

static int parse_iref(const uint8_t *data, size_t size, heif_context *ctx, char *err, size_t errcap) {
    if (size < 4) {
        set_err(err, errcap, "invalid HEIF iref");
        return 0;
    }
    uint8_t version = data[0];
    size_t pos = 4;
    while (pos + 8 <= size) {
        heif_box box;
        if (!read_box_header(data, size, pos, &box)) {
            set_err(err, errcap, "invalid HEIF iref box");
            return 0;
        }
        size_t payload_pos = pos + box.header_size;
        size_t payload_size = (size_t)(box.size - box.header_size);
        if (!parse_iref_box(data + payload_pos, payload_size, box.type, version, ctx, err, errcap)) {
            return 0;
        }
        pos += (size_t)box.size;
    }
    return 1;
}

static int read_var_size(const uint8_t *data, size_t size, size_t *pos, uint8_t bytes,
                         uint64_t *out, char *err, size_t errcap) {
    if (bytes == 0) {
        *out = 0;
        return 1;
    }
    if (bytes > 8) {
        set_err(err, errcap, "unsupported HEIF field size");
        return 0;
    }
    if (*pos + bytes > size) {
        set_err(err, errcap, "truncated HEIF box");
        return 0;
    }
    uint64_t val = 0;
    for (uint8_t i = 0; i < bytes; i++) {
        val = (val << 8) | data[*pos + i];
    }
    *pos += bytes;
    *out = val;
    return 1;
}

static int parse_iloc(const uint8_t *data, size_t size, heif_context *ctx, char *err, size_t errcap) {
    if (size < 6) {
        set_err(err, errcap, "invalid HEIF iloc");
        return 0;
    }
    uint8_t version = data[0];
    size_t pos = 4;
    uint8_t tmp = data[pos++];
    uint8_t offset_size = tmp >> 4;
    uint8_t length_size = tmp & 0x0F;
    tmp = data[pos++];
    uint8_t base_offset_size = tmp >> 4;
    uint8_t index_size = 0;
    if (version == 1 || version == 2) {
        index_size = tmp & 0x0F;
    }
    if (offset_size > 8 || length_size > 8 || base_offset_size > 8 || index_size > 8) {
        set_err(err, errcap, "unsupported HEIF iloc sizes");
        return 0;
    }
    uint32_t item_count = 0;
    if (version < 2) {
        if (pos + 2 > size) {
            set_err(err, errcap, "truncated HEIF iloc");
            return 0;
        }
        item_count = read_be16(data + pos);
        pos += 2;
    } else {
        if (pos + 4 > size) {
            set_err(err, errcap, "truncated HEIF iloc");
            return 0;
        }
        item_count = read_be32(data + pos);
        pos += 4;
    }
    if (item_count && SIZE_MAX / (size_t)item_count < sizeof(*ctx->locs)) {
        set_err(err, errcap, "HEIF iloc item count too large");
        return 0;
    }
    heif_item_location *locs = (heif_item_location *)calloc((size_t)item_count, sizeof(*locs));
    if (!locs && item_count) {
        set_err(err, errcap, "out of memory");
        return 0;
    }

    for (uint32_t i = 0; i < item_count; i++) {
        heif_item_location *loc = &locs[i];
        if (version < 2) {
            if (pos + 2 > size) {
                heif_free_locs(locs, item_count);
                set_err(err, errcap, "truncated HEIF iloc");
                return 0;
            }
            loc->item_id = read_be16(data + pos);
            pos += 2;
        } else {
            if (pos + 4 > size) {
                heif_free_locs(locs, item_count);
                set_err(err, errcap, "truncated HEIF iloc");
                return 0;
            }
            loc->item_id = read_be32(data + pos);
            pos += 4;
        }
        if (version == 1 || version == 2) {
            if (pos + 2 > size) {
                heif_free_locs(locs, item_count);
                set_err(err, errcap, "truncated HEIF iloc");
                return 0;
            }
            uint16_t tmp16 = read_be16(data + pos);
            pos += 2;
            loc->construction_method = (uint16_t)(tmp16 & 0x0FFFu);
        }
        if (pos + 2 > size) {
            heif_free_locs(locs, item_count);
            set_err(err, errcap, "truncated HEIF iloc");
            return 0;
        }
        loc->data_reference_index = read_be16(data + pos);
        pos += 2;
        if (!read_var_size(data, size, &pos, base_offset_size, &loc->base_offset, err, errcap)) {
            heif_free_locs(locs, item_count);
            return 0;
        }
        if (pos + 2 > size) {
            heif_free_locs(locs, item_count);
            set_err(err, errcap, "truncated HEIF iloc");
            return 0;
        }
        loc->extent_count = read_be16(data + pos);
        pos += 2;
        if (loc->extent_count > 0) {
            if (loc->extent_count && SIZE_MAX / (size_t)loc->extent_count < sizeof(*loc->extents)) {
                heif_free_locs(locs, item_count);
                set_err(err, errcap, "HEIF extent count too large");
                return 0;
            }
            loc->extents = (heif_extent *)calloc(loc->extent_count, sizeof(*loc->extents));
            if (!loc->extents) {
                heif_free_locs(locs, item_count);
                set_err(err, errcap, "out of memory");
                return 0;
            }
        }
        for (uint16_t e = 0; e < loc->extent_count; e++) {
            if (version == 1 || version == 2) {
                uint64_t ignore = 0;
                if (!read_var_size(data, size, &pos, index_size, &ignore, err, errcap)) {
                    heif_free_locs(locs, item_count);
                    return 0;
                }
            }
            if (!read_var_size(data, size, &pos, offset_size, &loc->extents[e].offset, err, errcap)) {
                heif_free_locs(locs, item_count);
                return 0;
            }
            if (!read_var_size(data, size, &pos, length_size, &loc->extents[e].length, err, errcap)) {
                heif_free_locs(locs, item_count);
                return 0;
            }
        }
        heif_debugf("heif iloc: item_id=%u method=%u base=0x%llx extents=%u",
                    loc->item_id, loc->construction_method,
                    (unsigned long long)loc->base_offset, loc->extent_count);
        for (uint16_t e = 0; e < loc->extent_count; e++) {
            heif_debugf(" [off=0x%llx len=0x%llx]",
                        (unsigned long long)loc->extents[e].offset,
                        (unsigned long long)loc->extents[e].length);
        }
        heif_debugf("\n");
    }

    for (size_t i = 0; i < ctx->loc_count; i++) {
        free(ctx->locs[i].extents);
    }
    free(ctx->locs);
    ctx->locs = locs;
    ctx->loc_count = item_count;
    return 1;
}

static int parse_pitm(const uint8_t *data, size_t size, heif_context *ctx, char *err, size_t errcap) {
    if (size < 4) {
        set_err(err, errcap, "invalid HEIF pitm");
        return 0;
    }
    uint8_t version = data[0];
    size_t pos = 4;
    if (version == 0) {
        if (pos + 2 > size) {
            set_err(err, errcap, "truncated HEIF pitm");
            return 0;
        }
        ctx->primary_item_id = read_be16(data + pos);
    } else {
        if (pos + 4 > size) {
            set_err(err, errcap, "truncated HEIF pitm");
            return 0;
        }
        ctx->primary_item_id = read_be32(data + pos);
    }
    ctx->has_pitm = 1;
    return 1;
}

static int parse_meta(const uint8_t *data, size_t size, heif_context *ctx, char *err, size_t errcap) {
    if (size < 4) {
        set_err(err, errcap, "invalid HEIF meta");
        return 0;
    }
    size_t pos = 4;
    while (pos + 8 <= size) {
        heif_box box;
        if (!read_box_header(data, size, pos, &box)) {
            set_err(err, errcap, "invalid HEIF box");
            return 0;
        }
        size_t payload_pos = pos + box.header_size;
        size_t payload_size = (size_t)(box.size - box.header_size);
        if (box.type == HEIF_FOURCC('i', 'i', 'n', 'f')) {
            if (!parse_iinf(data + payload_pos, payload_size, ctx, err, errcap)) {
                return 0;
            }
        } else if (box.type == HEIF_FOURCC('i', 'l', 'o', 'c')) {
            if (!parse_iloc(data + payload_pos, payload_size, ctx, err, errcap)) {
                return 0;
            }
        } else if (box.type == HEIF_FOURCC('i', 'r', 'e', 'f')) {
            if (!parse_iref(data + payload_pos, payload_size, ctx, err, errcap)) {
                return 0;
            }
        } else if (box.type == HEIF_FOURCC('i', 'p', 'r', 'p')) {
            if (!parse_iprp(data + payload_pos, payload_size, ctx, err, errcap)) {
                return 0;
            }
        } else if (box.type == HEIF_FOURCC('p', 'i', 't', 'm')) {
            if (!parse_pitm(data + payload_pos, payload_size, ctx, err, errcap)) {
                return 0;
            }
        } else if (box.type == HEIF_FOURCC('i', 'd', 'a', 't')) {
            ctx->idat = data + payload_pos;
            ctx->idat_size = payload_size;
        }
        pos += (size_t)box.size;
    }
    return 1;
}

static int heif_parse_container(const uint8_t *data, size_t size, heif_context *ctx,
                                char *err, size_t errcap) {
    size_t pos = 0;
    while (pos + 8 <= size) {
        heif_box box;
        if (!read_box_header(data, size, pos, &box)) {
            set_err(err, errcap, "invalid HEIF box");
            return 0;
        }
        size_t payload_pos = pos + box.header_size;
        size_t payload_size = (size_t)(box.size - box.header_size);
        if (box.type == HEIF_FOURCC('f', 't', 'y', 'p')) {
            if (!parse_ftyp(data + payload_pos, payload_size, ctx, err, errcap)) {
                return 0;
            }
        } else if (box.type == HEIF_FOURCC('m', 'e', 't', 'a')) {
            if (!parse_meta(data + payload_pos, payload_size, ctx, err, errcap)) {
                return 0;
            }
        } else if (box.type == HEIF_FOURCC('i', 'd', 'a', 't')) {
            ctx->idat = data + payload_pos;
            ctx->idat_size = payload_size;
        }
        pos += (size_t)box.size;
    }
    if (!ctx->is_heif) {
        set_err(err, errcap, "not a HEIF");
        return 0;
    }
    return 1;
}

static const heif_item_location *heif_find_location(const heif_context *ctx, uint32_t item_id) {
    for (size_t i = 0; i < ctx->loc_count; i++) {
        if (ctx->locs[i].item_id == item_id) {
            return &ctx->locs[i];
        }
    }
    return NULL;
}

static const heif_item_info *heif_find_info(const heif_context *ctx, uint32_t item_id) {
    for (size_t i = 0; i < ctx->info_count; i++) {
        if (ctx->infos[i].item_id == item_id) {
            return &ctx->infos[i];
        }
    }
    return NULL;
}

static const heif_property *heif_find_item_property(const heif_context *ctx, uint32_t item_id, uint32_t type) {
    if (!ctx->item_props || ctx->prop_count == 0) {
        return NULL;
    }
    for (size_t i = 0; i < ctx->item_props_count; i++) {
        if (ctx->item_props[i].item_id != item_id) {
            continue;
        }
        const heif_item_props *props = &ctx->item_props[i];
        for (uint16_t j = 0; j < props->prop_count; j++) {
            uint16_t idx = props->prop_indices[j];
            if (idx == 0 || idx > ctx->prop_count) {
                continue;
            }
            const heif_property *prop = &ctx->props[idx - 1];
            if (prop->type == type) {
                return prop;
            }
        }
        break;
    }
    return NULL;
}

static void heif_fourcc_to_str(uint32_t v, char out[5]) {
    out[0] = (char)((v >> 24) & 0xFFu);
    out[1] = (char)((v >> 16) & 0xFFu);
    out[2] = (char)((v >> 8) & 0xFFu);
    out[3] = (char)(v & 0xFFu);
    out[4] = '\0';
}

static void heif_debug_item_props(const heif_context *ctx, uint32_t item_id) {
    if (!heif_debug_enabled()) {
        return;
    }
    const heif_item_props *props = heif_find_item_props_const(ctx, item_id);
    if (!props) {
        heif_debugf("heif: item_id=%u props=none\n", item_id);
        return;
    }
    heif_debugf("heif: item_id=%u props=%u\n", item_id, props->prop_count);
    for (uint16_t j = 0; j < props->prop_count; j++) {
        uint16_t idx = props->prop_indices[j];
        if (idx == 0 || idx > ctx->prop_count) {
            continue;
        }
        const heif_property *prop = &ctx->props[idx - 1];
        char tag[5];
        heif_fourcc_to_str(prop->type, tag);
        heif_debugf("heif: item_id=%u prop[%u]=%s size=%zu\n",
                    item_id, (unsigned)j, tag, prop->size);
    }
}

static int heif_is_hevc_item(const heif_item_info *info);

static const heif_property *heif_find_any_hvcc(const heif_context *ctx) {
    if (!ctx) {
        return NULL;
    }
    for (size_t i = 0; i < ctx->info_count; i++) {
        const heif_item_info *info = &ctx->infos[i];
        if (!heif_is_hevc_item(info)) {
            continue;
        }
        const heif_property *p = heif_find_item_property(ctx, info->item_id,
                                                         HEIF_FOURCC('h', 'v', 'c', 'C'));
        if (p) {
            return p;
        }
    }
    for (size_t i = 0; i < ctx->prop_count; i++) {
        if (ctx->props[i].type == HEIF_FOURCC('h', 'v', 'c', 'C')) {
            return &ctx->props[i];
        }
    }
    return NULL;
}

static int heif_extract_item_data(const uint8_t *data, size_t size, const heif_context *ctx,
                                  uint32_t item_id, uint8_t **out_buf, size_t *out_size,
                                  char *err, size_t errcap) {
    const heif_item_location *loc = heif_find_location(ctx, item_id);
    if (!loc) {
        set_err(err, errcap, "missing HEIF item location");
        return 0;
    }
    if (loc->data_reference_index != 0) {
        set_err(err, errcap, "unsupported HEIF data reference");
        return 0;
    }
    if (loc->construction_method > 1) {
        set_err(err, errcap, "unsupported HEIF construction method");
        return 0;
    }
    uint64_t total = 0;
    for (uint16_t i = 0; i < loc->extent_count; i++) {
        if (UINT64_MAX - total < loc->extents[i].length) {
            set_err(err, errcap, "HEIF extent overflow");
            return 0;
        }
        total += loc->extents[i].length;
    }
    if (total > SIZE_MAX) {
        set_err(err, errcap, "HEIF item too large");
        return 0;
    }
    uint8_t *buf = NULL;
    if (total > 0) {
        buf = (uint8_t *)malloc((size_t)total);
        if (!buf) {
            set_err(err, errcap, "out of memory");
            return 0;
        }
    }
    size_t write_pos = 0;
    for (uint16_t i = 0; i < loc->extent_count; i++) {
        uint64_t offset = loc->base_offset + loc->extents[i].offset;
        uint64_t length = loc->extents[i].length;
        if (length == 0) {
            continue;
        }
        const uint8_t *src = NULL;
        size_t src_size = 0;
        if (loc->construction_method == 0) {
            if (offset > size || length > (uint64_t)(size - (size_t)offset)) {
                free(buf);
                set_err(err, errcap, "truncated HEIF data");
                return 0;
            }
            src = data + (size_t)offset;
            src_size = (size_t)length;
        } else {
            if (!ctx->idat) {
                free(buf);
                set_err(err, errcap, "missing HEIF idat");
                return 0;
            }
            if (offset > ctx->idat_size || length > (uint64_t)(ctx->idat_size - (size_t)offset)) {
                free(buf);
                set_err(err, errcap, "truncated HEIF idat");
                return 0;
            }
            src = ctx->idat + (size_t)offset;
            src_size = (size_t)length;
        }
        if (write_pos + src_size > (size_t)total) {
            free(buf);
            set_err(err, errcap, "HEIF item size mismatch");
            return 0;
        }
        memcpy(buf + write_pos, src, src_size);
        write_pos += src_size;
    }
    *out_buf = buf;
    *out_size = (size_t)total;
    heif_debugf("heif extract: item_id=%u total=%llu bytes\n",
                item_id, (unsigned long long)total);
    return 1;
}

static int heif_is_hevc_item(const heif_item_info *info) {
    if (!info || info->item_type[0] == '\0') {
        return 1;
    }
    if (memcmp(info->item_type, "hvc1", 4) == 0) {
        return 1;
    }
    if (memcmp(info->item_type, "hev1", 4) == 0) {
        return 1;
    }
    return 0;
}

static int heif_is_grid_item(const heif_item_info *info) {
    if (!info || info->item_type[0] == '\0') {
        return 0;
    }
    return memcmp(info->item_type, "grid", 4) == 0;
}

static int heif_parse_grid(const uint8_t *data, size_t size, uint32_t *rows, uint32_t *cols,
                           uint32_t *grid_w, uint32_t *grid_h,
                           uint32_t **tile_ids, uint16_t *tile_count,
                           char *err, size_t errcap) {
    if (!data || !rows || !cols || !tile_ids || !tile_count) {
        set_err(err, errcap, "invalid HEIF grid item");
        return 0;
    }
    if (grid_w) {
        *grid_w = 0;
    }
    if (grid_h) {
        *grid_h = 0;
    }
    if (size < 4) {
        set_err(err, errcap, "invalid HEIF grid item");
        return 0;
    }
    uint8_t version = data[0];
    if (version > 1) {
        set_err(err, errcap, "unsupported HEIF grid version");
        return 0;
    }
    uint8_t flags = data[1];
    size_t pos = 2;
    uint8_t rows_minus_one = data[pos++];
    uint8_t cols_minus_one = data[pos++];
    *rows = (uint32_t)rows_minus_one + 1u;
    *cols = (uint32_t)cols_minus_one + 1u;
    if (*rows == 0 || *cols == 0) {
        set_err(err, errcap, "invalid HEIF grid dimensions");
        return 0;
    }
    if (flags & 1u) {
        if (size < pos + 8) {
            set_err(err, errcap, "invalid HEIF grid item");
            return 0;
        }
        if (grid_w) {
            *grid_w = read_be32(data + pos);
        }
        if (grid_h) {
            *grid_h = read_be32(data + pos + 4);
        }
        pos += 8;
    } else {
        if (size < pos + 4) {
            set_err(err, errcap, "invalid HEIF grid item");
            return 0;
        }
        if (grid_w) {
            *grid_w = (uint32_t)read_be16(data + pos);
        }
        if (grid_h) {
            *grid_h = (uint32_t)read_be16(data + pos + 2);
        }
        pos += 4;
    }
    uint64_t count = (uint64_t)(*rows) * (uint64_t)(*cols);
    if (count > UINT16_MAX) {
        set_err(err, errcap, "HEIF grid too many tiles");
        return 0;
    }
    *tile_count = (uint16_t)count;
    heif_debugf("heif grid: size=%zu version=%u rows=%u cols=%u tiles=%u\n",
                size, version, *rows, *cols, *tile_count);
    size_t remaining = (size > pos) ? (size - pos) : 0;
    size_t id_bytes = 0;
    int id_size = 0;

    if (remaining == (size_t)(*tile_count) * 2u || remaining == (size_t)(*tile_count) * 4u) {
        id_bytes = remaining;
    } else if (remaining >= 8) {
        pos += 8;
        remaining = (size > pos) ? (size - pos) : 0;
        if (remaining == (size_t)(*tile_count) * 2u || remaining == (size_t)(*tile_count) * 4u) {
            id_bytes = remaining;
        }
    }

    if (id_bytes == (size_t)(*tile_count) * 2u) {
        id_size = 2;
    } else if (id_bytes == (size_t)(*tile_count) * 4u) {
        id_size = 4;
    }

    heif_debugf("heif grid: payload_remaining=%zu id_size=%d id_bytes=%zu\n",
                remaining, id_size, id_bytes);

    if (id_size > 0) {
        uint32_t *ids = (uint32_t *)malloc((size_t)(*tile_count) * sizeof(*ids));
        if (!ids) {
            set_err(err, errcap, "out of memory");
            return 0;
        }
        for (uint16_t i = 0; i < *tile_count; i++) {
            if (id_size == 2) {
                ids[i] = read_be16(data + pos);
                pos += 2;
            } else {
                ids[i] = read_be32(data + pos);
                pos += 4;
            }
        }
        heif_debugf("heif grid: parsed %u tile ids (id_size=%d)\n", *tile_count, id_size);
        *tile_ids = ids;
        return 1;
    }

    heif_debugf("heif grid: no tile ids in payload\n");
    *tile_ids = NULL;
    return 1;
}

static int heif_read_ispe(const heif_property *prop, uint32_t *w, uint32_t *h) {
    if (!prop || !w || !h || prop->size < 12) {
        return 0;
    }
    *w = read_be32(prop->data + 4);
    *h = read_be32(prop->data + 8);
    return (*w != 0 && *h != 0);
}

static int heif_read_irot(const heif_property *prop, uint32_t *rot) {
    if (!prop || !rot || prop->size < 5) {
        return 0;
    }
    *rot = (uint32_t)(prop->data[4] & 3u);
    return 1;
}

static int heif_read_imir(const heif_property *prop, uint32_t *axis) {
    if (!prop || !axis || prop->size < 5) {
        return 0;
    }
    *axis = (uint32_t)(prop->data[4] & 1u);
    return 1;
}

typedef struct heif_clap {
    double width;
    double height;
    double horiz_off;
    double vert_off;
} heif_clap;

typedef struct heif_colr {
    uint8_t has_nclx;
    uint8_t colour_primaries;
    uint8_t transfer_characteristics;
    uint8_t matrix_coefficients;
    uint8_t full_range_flag;
} heif_colr;

static int heif_read_clap(const heif_property *prop, heif_clap *clap) {
    if (!prop || !clap || prop->size < 36) {
        return 0;
    }
    const uint8_t *p = prop->data + 4;
    uint32_t w_n = read_be32(p);
    uint32_t w_d = read_be32(p + 4);
    uint32_t h_n = read_be32(p + 8);
    uint32_t h_d = read_be32(p + 12);
    int32_t h_off_n = (int32_t)read_be32(p + 16);
    uint32_t h_off_d = read_be32(p + 20);
    int32_t v_off_n = (int32_t)read_be32(p + 24);
    uint32_t v_off_d = read_be32(p + 28);
    if (w_d == 0 || h_d == 0 || h_off_d == 0 || v_off_d == 0) {
        return 0;
    }
    clap->width = (double)w_n / (double)w_d;
    clap->height = (double)h_n / (double)h_d;
    clap->horiz_off = (double)h_off_n / (double)h_off_d;
    clap->vert_off = (double)v_off_n / (double)v_off_d;
    return 1;
}

static int heif_read_colr_nclx(const heif_property *prop, heif_colr *colr) {
    if (!prop || !colr || prop->size < 11) {
        return 0;
    }
    uint32_t colour_type = read_be32(prop->data);
    if (colour_type != HEIF_FOURCC('n', 'c', 'l', 'x')) {
        return 0;
    }
    colr->has_nclx = 1;
    colr->colour_primaries = (uint8_t)read_be16(prop->data + 4);
    colr->transfer_characteristics = (uint8_t)read_be16(prop->data + 6);
    colr->matrix_coefficients = (uint8_t)read_be16(prop->data + 8);
    colr->full_range_flag = (prop->data[10] & 0x80u) ? 1u : 0u;
    return 1;
}

static int heif_apply_clap(cupidimage_image *img, const heif_clap *clap,
                           char *err, size_t errcap) {
    if (!img || !img->rgba || !clap) {
        return 1;
    }
    if (clap->width <= 0.0 || clap->height <= 0.0) {
        return 1;
    }
    double left = ((double)img->width - clap->width) * 0.5 + clap->horiz_off;
    double top = ((double)img->height - clap->height) * 0.5 + clap->vert_off;
    int crop_x = (int)((left >= 0.0) ? (left + 0.5) : (left - 0.5));
    int crop_y = (int)((top >= 0.0) ? (top + 0.5) : (top - 0.5));
    int crop_w = (int)((clap->width >= 0.0) ? (clap->width + 0.5) : (clap->width - 0.5));
    int crop_h = (int)((clap->height >= 0.0) ? (clap->height + 0.5) : (clap->height - 0.5));
    if (crop_w <= 0 || crop_h <= 0) {
        return 1;
    }
    if (crop_x < 0) {
        crop_w += crop_x;
        crop_x = 0;
    }
    if (crop_y < 0) {
        crop_h += crop_y;
        crop_y = 0;
    }
    if (crop_x + crop_w > (int)img->width) {
        crop_w = (int)img->width - crop_x;
    }
    if (crop_y + crop_h > (int)img->height) {
        crop_h = (int)img->height - crop_y;
    }
    if (crop_w <= 0 || crop_h <= 0) {
        return 1;
    }
    size_t pixels = (size_t)crop_w * (size_t)crop_h;
    if (pixels > SIZE_MAX / 4u) {
        set_err(err, errcap, "HEIF clap too large");
        return 0;
    }
    uint8_t *dst = (uint8_t *)malloc(pixels * 4u);
    if (!dst) {
        set_err(err, errcap, "out of memory");
        return 0;
    }
    for (int y = 0; y < crop_h; y++) {
        size_t src_row = ((size_t)(crop_y + y) * img->width + (size_t)crop_x) * 4u;
        size_t dst_row = (size_t)y * (size_t)crop_w * 4u;
        memcpy(dst + dst_row, img->rgba + src_row, (size_t)crop_w * 4u);
    }
    free(img->rgba);
    img->rgba = dst;
    img->width = (uint32_t)crop_w;
    img->height = (uint32_t)crop_h;
    return 1;
}

static int heif_apply_mirror(cupidimage_image *img, uint32_t axis, char *err, size_t errcap) {
    if (!img || !img->rgba || img->width == 0 || img->height == 0) {
        return 1;
    }
    size_t pixels = (size_t)img->width * (size_t)img->height;
    if (pixels > SIZE_MAX / 4u) {
        set_err(err, errcap, "HEIF mirror too large");
        return 0;
    }
    uint8_t *dst = (uint8_t *)malloc(pixels * 4u);
    if (!dst) {
        set_err(err, errcap, "out of memory");
        return 0;
    }
    uint32_t w = img->width;
    uint32_t h = img->height;
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            uint32_t src_x = (axis == 0) ? (w - 1u - x) : x;
            uint32_t src_y = (axis == 1) ? (h - 1u - y) : y;
            const uint8_t *src = img->rgba + ((size_t)src_y * w + src_x) * 4u;
            uint8_t *dst_px = dst + ((size_t)y * w + x) * 4u;
            memcpy(dst_px, src, 4u);
        }
    }
    free(img->rgba);
    img->rgba = dst;
    return 1;
}

static int heif_apply_rotation(cupidimage_image *img, uint32_t rot, char *err, size_t errcap) {
    if (!img || !img->rgba || img->width == 0 || img->height == 0) {
        return 1;
    }
    rot &= 3u;
    if (rot == 0) {
        return 1;
    }
    uint32_t w = img->width;
    uint32_t h = img->height;
    uint32_t new_w = (rot == 1 || rot == 3) ? h : w;
    uint32_t new_h = (rot == 1 || rot == 3) ? w : h;
    size_t pixels = (size_t)new_w * (size_t)new_h;
    if (pixels > SIZE_MAX / 4u) {
        set_err(err, errcap, "HEIF rotation too large");
        return 0;
    }
    uint8_t *dst = (uint8_t *)malloc(pixels * 4u);
    if (!dst) {
        set_err(err, errcap, "out of memory");
        return 0;
    }
    for (uint32_t y = 0; y < new_h; y++) {
        for (uint32_t x = 0; x < new_w; x++) {
            uint32_t src_x = 0;
            uint32_t src_y = 0;
            if (rot == 1) {
                src_x = y;
                src_y = h - 1u - x;
            } else if (rot == 2) {
                src_x = w - 1u - x;
                src_y = h - 1u - y;
            } else {
                src_x = w - 1u - y;
                src_y = x;
            }
            const uint8_t *src = img->rgba + ((size_t)src_y * w + src_x) * 4u;
            uint8_t *dst_px = dst + ((size_t)y * new_w + x) * 4u;
            memcpy(dst_px, src, 4u);
        }
    }
    free(img->rgba);
    img->rgba = dst;
    img->width = new_w;
    img->height = new_h;
    return 1;
}

static uint16_t heif_read_u16_exif(const uint8_t *p, int le) {
    if (le) {
        return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
    }
    return (uint16_t)p[1] | ((uint16_t)p[0] << 8);
}

static uint32_t heif_read_u32_exif(const uint8_t *p, int le) {
    if (le) {
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
               ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }
    return (uint32_t)p[3] | ((uint32_t)p[2] << 8) |
           ((uint32_t)p[1] << 16) | ((uint32_t)p[0] << 24);
}

static int heif_read_exif_orientation(const uint8_t *data, size_t size, uint16_t *out) {
    if (!data || !out || size < 8) {
        return 0;
    }
    uint32_t off = read_be32(data);
    size_t base = 4;
    if (off > size - base || size - base - off < 8) {
        return 0;
    }
    const uint8_t *tiff = data + base + off;
    size_t tiff_rem = size - (size_t)(tiff - data);
    if (tiff_rem >= 6 && memcmp(tiff, "Exif\0\0", 6) == 0) {
        tiff += 6;
        if (size - (size_t)(tiff - data) < 8) {
            return 0;
        }
    }
    int le = 0;
    if (tiff[0] == 'I' && tiff[1] == 'I') {
        le = 1;
    } else if (tiff[0] == 'M' && tiff[1] == 'M') {
        le = 0;
    } else {
        return 0;
    }
    uint16_t magic = heif_read_u16_exif(tiff + 2, le);
    if (magic != 42) {
        return 0;
    }
    uint32_t ifd_off = heif_read_u32_exif(tiff + 4, le);
    if (ifd_off > size - off - 2) {
        return 0;
    }
    const uint8_t *ifd = tiff + ifd_off;
    uint16_t count = heif_read_u16_exif(ifd, le);
    size_t need = 2u + (size_t)count * 12u;
    if ((size_t)(ifd - tiff) + need > size - off) {
        return 0;
    }
    for (uint16_t i = 0; i < count; i++) {
        const uint8_t *ent = ifd + 2u + (size_t)i * 12u;
        uint16_t tag = heif_read_u16_exif(ent, le);
        uint16_t type = heif_read_u16_exif(ent + 2, le);
        uint32_t num = heif_read_u32_exif(ent + 4, le);
        uint32_t val = heif_read_u32_exif(ent + 8, le);
        if (tag != 0x0112 || type != 3 || num == 0) {
            continue;
        }
        uint16_t orient = 0;
        if (num * 2u <= 4u) {
            orient = le ? (uint16_t)(val & 0xFFFFu) : (uint16_t)(val >> 16);
        } else {
            if (val > size - off - 2) {
                return 0;
            }
            const uint8_t *vp = tiff + val;
            orient = heif_read_u16_exif(vp, le);
        }
        if (orient >= 1 && orient <= 8) {
            *out = orient;
            return 1;
        }
        return 0;
    }
    return 0;
}

static int heif_find_exif_item_id(const heif_context *ctx, uint32_t item_id, uint32_t *exif_id) {
    if (!ctx || !exif_id) {
        return 0;
    }
    for (size_t i = 0; i < ctx->ref_count; i++) {
        if (ctx->refs[i].from_id != item_id) {
            continue;
        }
        for (uint16_t j = 0; j < ctx->refs[i].to_count; j++) {
            uint32_t to_id = ctx->refs[i].to_ids[j];
            const heif_item_info *info = heif_find_info(ctx, to_id);
            if (info && memcmp(info->item_type, "Exif", 4) == 0) {
                *exif_id = to_id;
                return 1;
            }
        }
    }
    uint32_t found = 0;
    uint32_t count = 0;
    for (size_t i = 0; i < ctx->info_count; i++) {
        if (memcmp(ctx->infos[i].item_type, "Exif", 4) == 0) {
            found = ctx->infos[i].item_id;
            count++;
        }
    }
    if (count == 1) {
        *exif_id = found;
        return 1;
    }
    return 0;
}

static int heif_apply_exif_orientation(const unsigned char *data, size_t size,
                                       const heif_context *ctx, uint32_t item_id,
                                       cupidimage_image *img, char *err, size_t errcap) {
    if (ctx && ctx->has_pitm && item_id != ctx->primary_item_id) {
        return 1;
    }
    uint32_t exif_id = 0;
    if (!heif_find_exif_item_id(ctx, item_id, &exif_id)) {
        return 1;
    }
    uint8_t *exif_data = NULL;
    size_t exif_size = 0;
    if (!heif_extract_item_data(data, size, ctx, exif_id, &exif_data, &exif_size, err, errcap)) {
        return 0;
    }
    uint16_t orient = 0;
    int ok = heif_read_exif_orientation(exif_data, exif_size, &orient);
    free(exif_data);
    if (!ok) {
        return 1;
    }
    heif_debugf("heif: apply exif orientation=%u\n", (unsigned)orient);
    if (orient == 2 || orient == 5 || orient == 7) {
        if (!heif_apply_mirror(img, 0, err, errcap)) {
            return 0;
        }
    } else if (orient == 4) {
        if (!heif_apply_mirror(img, 1, err, errcap)) {
            return 0;
        }
    }
    if (orient == 3) {
        if (!heif_apply_rotation(img, 2, err, errcap)) {
            return 0;
        }
    } else if (orient == 5) {
        if (!heif_apply_rotation(img, 3, err, errcap)) {
            return 0;
        }
    } else if (orient == 6) {
        if (!heif_apply_rotation(img, 1, err, errcap)) {
            return 0;
        }
    } else if (orient == 7) {
        if (!heif_apply_rotation(img, 1, err, errcap)) {
            return 0;
        }
    } else if (orient == 8) {
        if (!heif_apply_rotation(img, 3, err, errcap)) {
            return 0;
        }
    }
    return 1;
}

static int heif_apply_item_transforms(const heif_context *ctx, uint32_t item_id,
                                      cupidimage_image *img, int *out_orient_applied,
                                      char *err, size_t errcap) {
    heif_debug_item_props(ctx, item_id);
    if (out_orient_applied) {
        *out_orient_applied = 0;
    }
    const heif_item_props *props = heif_find_item_props_const(ctx, item_id);
    if (!props || props->prop_count == 0) {
        return 1;
    }
    for (uint16_t j = 0; j < props->prop_count; j++) {
        uint16_t idx = props->prop_indices[j];
        if (idx == 0 || idx > ctx->prop_count) {
            continue;
        }
        const heif_property *prop = &ctx->props[idx - 1];
        if (prop->type == HEIF_FOURCC('i', 'r', 'o', 't')) {
            uint32_t rot = 0;
            if (heif_read_irot(prop, &rot)) {
                heif_debugf("heif: apply irot=%u\n", rot);
                if (out_orient_applied) {
                    *out_orient_applied = 1;
                }
                if (!heif_apply_rotation(img, rot, err, errcap)) {
                    return 0;
                }
            }
        } else if (prop->type == HEIF_FOURCC('i', 'm', 'i', 'r')) {
            uint32_t axis = 0;
            if (heif_read_imir(prop, &axis)) {
                heif_debugf("heif: apply imir axis=%u\n", axis);
                if (out_orient_applied) {
                    *out_orient_applied = 1;
                }
                if (!heif_apply_mirror(img, axis, err, errcap)) {
                    return 0;
                }
            }
        } else if (prop->type == HEIF_FOURCC('c', 'l', 'a', 'p')) {
            heif_clap clap;
            if (heif_read_clap(prop, &clap)) {
                heif_debugf("heif: apply clap w=%.2f h=%.2f off=(%.2f,%.2f)\n",
                            clap.width, clap.height, clap.horiz_off, clap.vert_off);
                if (!heif_apply_clap(img, &clap, err, errcap)) {
                    return 0;
                }
            }
        }
    }
    return 1;
}

static void heif_infer_grid_layout(uint32_t ispe_w, uint32_t ispe_h,
                                   uint32_t tile_w, uint32_t tile_h,
                                   uint16_t tile_count,
                                   uint32_t *rows, uint32_t *cols) {
    if (!rows || !cols || tile_count == 0) {
        return;
    }
    if (ispe_w && ispe_h && tile_w && tile_h) {
        uint32_t cols_guess = (ispe_w + tile_w - 1u) / tile_w;
        uint32_t rows_guess = (ispe_h + tile_h - 1u) / tile_h;
        if (rows_guess && cols_guess && rows_guess * cols_guess == tile_count) {
            *rows = rows_guess;
            *cols = cols_guess;
            return;
        }
    }
    double target = 0.0;
    int use_target = 0;
    if (ispe_w && ispe_h) {
        target = (double)ispe_w / (double)ispe_h;
        use_target = 1;
    }
    uint32_t best_rows = 1;
    uint32_t best_cols = tile_count;
    double best_score = 1e30;
    for (uint32_t c = 1; c <= tile_count; c++) {
        if (tile_count % c != 0) {
            continue;
        }
        uint32_t r = tile_count / c;
        double score = use_target ? fabs(((double)c / (double)r) - target)
                                  : fabs((double)c - (double)r);
        if (score < best_score) {
            best_score = score;
            best_rows = r;
            best_cols = c;
        }
    }
    *rows = best_rows;
    *cols = best_cols;
}

#define HEVC_MAX_SPS 32
#define HEVC_MAX_PPS 256
#define HEVC_MAX_SHORT_TERM_RPS 64

typedef struct hevc_bitstream {
    const uint8_t *data;
    size_t size;
    size_t pos;
    uint64_t bitbuf;
    int bitcount;
} hevc_bitstream;

typedef struct hevc_sps {
    int valid;
    uint32_t width;
    uint32_t height;
    uint32_t display_width;
    uint32_t display_height;
    uint32_t pic_width_in_ctus;
    uint32_t pic_height_in_ctus;
    uint32_t ctu_size;
    uint8_t log2_min_luma_coding_block_size_minus3;
    uint8_t log2_diff_max_min_luma_coding_block_size;
    uint8_t chroma_format_idc;
    uint8_t separate_colour_plane_flag;
    uint8_t bit_depth_luma;
    uint8_t bit_depth_chroma;
    uint8_t log2_max_pic_order_cnt_lsb_minus4;
    uint8_t log2_min_luma_transform_block_size_minus2;
    uint8_t log2_diff_max_min_luma_transform_block_size;
    uint8_t max_transform_hierarchy_depth_intra;
    uint8_t sample_adaptive_offset_enabled_flag;
    uint8_t sps_temporal_mvp_enabled_flag;
    uint8_t long_term_ref_pics_present_flag;
    uint8_t num_short_term_ref_pic_sets;
    uint8_t num_delta_pocs[HEVC_MAX_SHORT_TERM_RPS];
    uint8_t vui_present_flag;
    uint8_t vui_video_full_range_flag;
    uint8_t vui_colour_primaries;
    uint8_t vui_transfer_characteristics;
    uint8_t vui_matrix_coefficients;
} hevc_sps;

typedef struct hevc_pps {
    int valid;
    uint32_t pps_id;
    uint32_t sps_id;
    uint8_t dependent_slice_segments_enabled_flag;
    uint8_t output_flag_present_flag;
    uint8_t num_extra_slice_header_bits;
    uint8_t sign_data_hiding_enabled_flag;
    uint8_t transform_skip_enabled_flag;
    uint8_t tiles_enabled_flag;
    uint8_t entropy_coding_sync_enabled_flag;
    uint8_t cabac_init_present_flag;
    uint8_t pps_slice_chroma_qp_offsets_present_flag;
    uint8_t transquant_bypass_enabled_flag;
    uint8_t cu_qp_delta_enabled_flag;
    uint8_t diff_cu_qp_delta_depth;
    int8_t pps_cb_qp_offset;
    int8_t pps_cr_qp_offset;
    uint8_t deblocking_filter_control_present_flag;
    uint8_t deblocking_filter_override_enabled_flag;
    uint8_t pps_deblocking_filter_disabled_flag;
    uint8_t pps_loop_filter_across_slices_enabled_flag;
    uint8_t slice_segment_header_extension_present_flag;
    int8_t init_qp_minus26;
    uint16_t num_tile_columns;
    uint16_t num_tile_rows;
    uint8_t uniform_spacing_flag;
    uint16_t *tile_col_width_minus1;
    uint16_t *tile_row_height_minus1;
} hevc_pps;

typedef struct hevc_sao {
    uint8_t type; /* 0=off,1=band,2=edge */
    uint8_t band_pos;
    uint8_t eo_class;
    int8_t offset[4];
} hevc_sao;

typedef struct hevc_sao_ctu {
    hevc_sao luma;
    hevc_sao cb;
    hevc_sao cr;
} hevc_sao_ctu;

typedef struct hevc_context {
    hevc_sps sps[HEVC_MAX_SPS];
    hevc_pps pps[HEVC_MAX_PPS];
    int have_sps;
    int have_pps;
    uint32_t last_sps_id;
    int length_size;
    int did_traverse;
    int last_slice_type;
    int slice_qp;
    uint8_t sign_data_hiding_enabled_flag;
    uint16_t *frame_luma;
    uint16_t *frame_cb;
    uint16_t *frame_cr;
    uint32_t frame_stride;
    uint32_t chroma_stride;
    uint32_t frame_width;
    uint32_t frame_height;
    uint32_t chroma_width;
    uint32_t chroma_height;
    int slice_cb_qp_offset;
    int slice_cr_qp_offset;
    uint8_t *mode_map;
    uint32_t mode_stride;
    uint8_t *qp_map;
    uint8_t *residual_map;
    uint32_t map_stride;
    uint32_t map_height;
    uint8_t *chroma_cbf_cb_map;
    uint8_t *chroma_cbf_cr_map;
    uint32_t chroma_cbf_stride;
    uint32_t chroma_cbf_height;
    uint32_t chroma_root_w;
    uint32_t chroma_root_h;
    uint8_t *qp_delta_coded_map;
    uint32_t qp_delta_stride;
    uint32_t qp_delta_height;
    uint32_t qp_block_size;
    hevc_sao_ctu *sao_map;
    uint32_t sao_stride;
    uint32_t sao_height;
} hevc_context;

static void hevc_context_init(hevc_context *ctx) {
    memset(ctx, 0, sizeof(*ctx));
}

static void hevc_pps_clear_tiles(hevc_pps *pps) {
    if (!pps) {
        return;
    }
    free(pps->tile_col_width_minus1);
    free(pps->tile_row_height_minus1);
    pps->tile_col_width_minus1 = NULL;
    pps->tile_row_height_minus1 = NULL;
    pps->num_tile_columns = 0;
    pps->num_tile_rows = 0;
    pps->uniform_spacing_flag = 0;
}

static void hevc_context_free(hevc_context *ctx) {
    if (!ctx) {
        return;
    }
    for (size_t i = 0; i < HEVC_MAX_PPS; i++) {
        hevc_pps_clear_tiles(&ctx->pps[i]);
    }
    free(ctx->frame_luma);
    free(ctx->frame_cb);
    free(ctx->frame_cr);
    free(ctx->mode_map);
    free(ctx->qp_map);
    free(ctx->residual_map);
    free(ctx->chroma_cbf_cb_map);
    free(ctx->chroma_cbf_cr_map);
    free(ctx->qp_delta_coded_map);
    free(ctx->sao_map);
    ctx->frame_luma = NULL;
    ctx->frame_cb = NULL;
    ctx->frame_cr = NULL;
    ctx->frame_stride = 0;
    ctx->frame_width = 0;
    ctx->frame_height = 0;
    ctx->chroma_stride = 0;
    ctx->chroma_width = 0;
    ctx->chroma_height = 0;
    ctx->mode_map = NULL;
    ctx->mode_stride = 0;
    ctx->qp_map = NULL;
    ctx->residual_map = NULL;
    ctx->map_stride = 0;
    ctx->map_height = 0;
    ctx->chroma_cbf_cb_map = NULL;
    ctx->chroma_cbf_cr_map = NULL;
    ctx->chroma_cbf_stride = 0;
    ctx->chroma_cbf_height = 0;
    ctx->chroma_root_w = 0;
    ctx->chroma_root_h = 0;
    ctx->qp_delta_coded_map = NULL;
    ctx->qp_delta_stride = 0;
    ctx->qp_delta_height = 0;
    ctx->qp_block_size = 0;
    ctx->sao_map = NULL;
    ctx->sao_stride = 0;
    ctx->sao_height = 0;
}

static int hevc_parse_hvcc(const uint8_t *data, size_t size, hevc_context *ctx,
                           char *err, size_t errcap);
static int hevc_parse_nals(const uint8_t *data, size_t size, hevc_context *ctx,
                           char *err, size_t errcap);
static int hevc_output_to_image(hevc_context *ctx, const heif_colr *colr,
                                cupidimage_image *out,
                                char *err, size_t errcap);
static int hevc_log2_size(int size);

static int heif_decode_item_image(const unsigned char *data, size_t size, const heif_context *ctx,
                                  uint32_t item_id, cupidimage_image *out,
                                  char *err, size_t errcap);
static int heif_find_item_colr(const heif_context *ctx, uint32_t item_id, heif_colr *colr);

static int heif_decode_grid_item(const unsigned char *data, size_t size, const heif_context *ctx,
                                 uint32_t item_id, cupidimage_image *out,
                                 char *err, size_t errcap) {
    uint8_t *grid_data = NULL;
    size_t grid_size = 0;
    if (!heif_extract_item_data(data, size, ctx, item_id, &grid_data, &grid_size, err, errcap)) {
        return 0;
    }
    heif_debugf("heif grid item: item_id=%u grid_size=%zu\n", item_id, grid_size);
    if (heif_debug_enabled()) {
        size_t dump = grid_size < 32 ? grid_size : 32;
        heif_debugf("heif grid item bytes:");
        for (size_t i = 0; i < dump; i++) {
            heif_debugf(" %02x", grid_data[i]);
        }
        heif_debugf("\n");
    }
    uint32_t rows = 0;
    uint32_t cols = 0;
    uint32_t grid_w = 0;
    uint32_t grid_h = 0;
    uint32_t *tile_ids = NULL;
    uint16_t tile_count = 0;
    if (!heif_parse_grid(grid_data, grid_size, &rows, &cols, &grid_w, &grid_h,
                         &tile_ids, &tile_count, err, errcap)) {
        free(grid_data);
        return 0;
    }
    free(grid_data);

    const heif_property *ispe = heif_find_item_property(ctx, item_id, HEIF_FOURCC('i', 's', 'p', 'e'));
    uint32_t ispe_w = 0;
    uint32_t ispe_h = 0;
    if (heif_read_ispe(ispe, &ispe_w, &ispe_h)) {
        heif_debugf("heif grid: ispe %ux%u\n", ispe_w, ispe_h);
    } else if (grid_w && grid_h) {
        ispe_w = grid_w;
        ispe_h = grid_h;
        heif_debugf("heif grid: output %ux%u\n", ispe_w, ispe_h);
    }

    const uint32_t *grid_ids = tile_ids;
    uint16_t grid_count = tile_count;
    if (!grid_ids || grid_count < (uint16_t)(rows * cols)) {
        heif_debugf("heif grid: tile_ids missing or short (have=%u need=%u), checking dimg refs\n",
                    grid_count, (uint16_t)(rows * cols));
        const heif_item_ref *ref = heif_find_item_ref(ctx, item_id, HEIF_FOURCC('d', 'i', 'm', 'g'));
        if (!ref) {
            for (size_t i = 0; i < ctx->ref_count; i++) {
                if (ctx->refs[i].type == HEIF_FOURCC('d', 'i', 'm', 'g') &&
                    ctx->refs[i].to_count > 0) {
                    ref = &ctx->refs[i];
                    break;
                }
            }
        }
        if (!ref || ref->to_count == 0) {
            heif_debugf("heif grid: missing dimg refs (ref_count=%zu)\n", ctx->ref_count);
            for (size_t i = 0; i < ctx->ref_count; i++) {
                heif_debugf("heif ref[%zu]: type=0x%08x from=%u to_count=%u\n",
                            i, ctx->refs[i].type, ctx->refs[i].from_id, ctx->refs[i].to_count);
            }
            free(tile_ids);
            set_err(err, errcap, "missing HEIF grid references");
            return 0;
        }
        grid_ids = ref->to_ids;
        grid_count = ref->to_count;
        if ((uint64_t)rows * (uint64_t)cols != (uint64_t)grid_count) {
            heif_debugf("heif grid: using dimg refs (%u ids), layout will be inferred\n", grid_count);
        } else {
            heif_debugf("heif grid: using dimg refs (%u ids)\n", grid_count);
        }
    }

    size_t tile_total = grid_count;
    cupidimage_image *tiles = (cupidimage_image *)calloc(tile_total, sizeof(*tiles));
    if (!tiles) {
        set_err(err, errcap, "out of memory");
        free(tile_ids);
        return 0;
    }
    uint32_t tile_w = 0;
    uint32_t tile_h = 0;
    for (size_t i = 0; i < tile_total; i++) {
        if (!heif_decode_item_image(data, size, ctx, grid_ids[i], &tiles[i], err, errcap)) {
            for (size_t j = 0; j < tile_total; j++) {
                free(tiles[j].rgba);
            }
            free(tiles);
            free(tile_ids);
            return 0;
        }
        if (i == 0) {
            tile_w = tiles[i].width;
            tile_h = tiles[i].height;
        } else if (tiles[i].width != tile_w || tiles[i].height != tile_h) {
            set_err(err, errcap, "HEIF grid tile size mismatch");
            for (size_t j = 0; j < tile_total; j++) {
                free(tiles[j].rgba);
            }
            free(tiles);
            free(tile_ids);
            return 0;
        }
    }

    if (tile_w == 0 || tile_h == 0) {
        set_err(err, errcap, "invalid HEIF grid tile size");
        for (size_t j = 0; j < tile_total; j++) {
            free(tiles[j].rgba);
        }
        free(tiles);
        free(tile_ids);
        return 0;
    }
    if (rows == 0 || cols == 0 || (uint64_t)rows * (uint64_t)cols != (uint64_t)tile_total) {
        uint32_t old_rows = rows;
        uint32_t old_cols = cols;
        heif_infer_grid_layout(ispe_w, ispe_h, tile_w, tile_h, (uint16_t)tile_total, &rows, &cols);
        heif_debugf("heif grid: inferred layout rows=%u cols=%u (was %u x %u)\n",
                    rows, cols, old_rows, old_cols);
    }
    if (tile_w > UINT32_MAX / cols || tile_h > UINT32_MAX / rows) {
        set_err(err, errcap, "HEIF grid size overflow");
        for (size_t j = 0; j < tile_total; j++) {
            free(tiles[j].rgba);
        }
        free(tiles);
        free(tile_ids);
        return 0;
    }
    uint32_t out_w = tile_w * cols;
    uint32_t out_h = tile_h * rows;
    if (ispe_w && ispe_h) {
        if (ispe_w > 0 && ispe_w <= out_w) {
            out_w = ispe_w;
        }
        if (ispe_h > 0 && ispe_h <= out_h) {
            out_h = ispe_h;
        }
    }
    size_t pixels = (size_t)out_w * (size_t)out_h;
    if (pixels > SIZE_MAX / 4u) {
        set_err(err, errcap, "HEIF grid too large");
        for (size_t j = 0; j < tile_total; j++) {
            free(tiles[j].rgba);
        }
        free(tiles);
        free(tile_ids);
        return 0;
    }
    uint8_t *rgba = (uint8_t *)malloc(pixels * 4u);
    if (!rgba) {
        set_err(err, errcap, "out of memory");
        for (size_t j = 0; j < tile_total; j++) {
            free(tiles[j].rgba);
        }
        free(tiles);
        free(tile_ids);
        return 0;
    }

    for (uint32_t r = 0; r < rows; r++) {
        for (uint32_t c = 0; c < cols; c++) {
            size_t idx = (size_t)r * cols + c;
            const uint8_t *src = tiles[idx].rgba;
            if (!src) {
                continue;
            }
            uint32_t x0 = c * tile_w;
            uint32_t y0 = r * tile_h;
            if (x0 >= out_w || y0 >= out_h) {
                continue;
            }
            uint32_t copy_w = tile_w;
            uint32_t copy_h = tile_h;
            if (x0 + copy_w > out_w) {
                copy_w = out_w - x0;
            }
            if (y0 + copy_h > out_h) {
                copy_h = out_h - y0;
            }
            for (uint32_t y = 0; y < copy_h; y++) {
                size_t dst_row = ((size_t)y0 + y) * (size_t)out_w;
                size_t src_row = (size_t)y * (size_t)tile_w;
                memcpy(rgba + (dst_row + (size_t)x0) * 4u,
                       src + src_row * 4u,
                       (size_t)copy_w * 4u);
            }
        }
    }

    for (size_t j = 0; j < tile_total; j++) {
        free(tiles[j].rgba);
    }
    free(tiles);
    free(tile_ids);

    out->width = out_w;
    out->height = out_h;
    out->rgba = rgba;
    out->hotspot_x = 0;
    out->hotspot_y = 0;
    return 1;
}

static int heif_find_item_colr(const heif_context *ctx, uint32_t item_id, heif_colr *colr) {
    if (!ctx || !colr) {
        return 0;
    }
    memset(colr, 0, sizeof(*colr));
    const heif_property *prop =
        heif_find_item_property(ctx, item_id, HEIF_FOURCC('c', 'o', 'l', 'r'));
    if (prop && heif_read_colr_nclx(prop, colr)) {
        return 1;
    }
    if (ctx->has_pitm && ctx->primary_item_id != item_id) {
        prop = heif_find_item_property(ctx, ctx->primary_item_id, HEIF_FOURCC('c', 'o', 'l', 'r'));
        if (prop && heif_read_colr_nclx(prop, colr)) {
            return 1;
        }
    }
    return 0;
}

static int heif_decode_item_image(const unsigned char *data, size_t size, const heif_context *ctx,
                                  uint32_t item_id, cupidimage_image *out,
                                  char *err, size_t errcap) {
    const heif_item_info *info = heif_find_info(ctx, item_id);
    if (info && !heif_is_grid_item(info) && !heif_is_hevc_item(info)) {
        set_err(err, errcap, "unsupported HEIF item type");
        return 0;
    }
    cupidimage_image tmp;
    memset(&tmp, 0, sizeof(tmp));
    if (info && heif_is_grid_item(info)) {
        if (!heif_decode_grid_item(data, size, ctx, item_id, &tmp, err, errcap)) {
            return 0;
        }
    } else {
        uint8_t *item_data = NULL;
        size_t item_size = 0;
        if (!heif_extract_item_data(data, size, ctx, item_id, &item_data, &item_size, err, errcap)) {
            return 0;
        }
        hevc_context hctx;
        hevc_context_init(&hctx);
        const heif_property *hvcc = heif_find_item_property(ctx, item_id, HEIF_FOURCC('h', 'v', 'c', 'C'));
        if (!hvcc) {
            hvcc = heif_find_any_hvcc(ctx);
            if (hvcc) {
                heif_debugf("heif: using global hvcC for item_id=%u\n", item_id);
            }
        }
        heif_debugf("heif: item_id=%u hvcc=%s\n", item_id, hvcc ? "yes" : "no");
        if (hvcc) {
            if (!hevc_parse_hvcc(hvcc->data, hvcc->size, &hctx, err, errcap)) {
                free(item_data);
                hevc_context_free(&hctx);
                return 0;
            }
        }
        if (!hevc_parse_nals(item_data, item_size, &hctx, err, errcap)) {
            free(item_data);
            hevc_context_free(&hctx);
            return 0;
        }
        heif_colr colr;
        heif_colr *colr_ptr = NULL;
        if (heif_find_item_colr(ctx, item_id, &colr)) {
            colr_ptr = &colr;
        }
        int ok = hevc_output_to_image(&hctx, colr_ptr, &tmp, err, errcap);
        free(item_data);
        hevc_context_free(&hctx);
        if (!ok) {
            return 0;
        }
    }
    int orient_applied = 0;
    if (!heif_apply_item_transforms(ctx, item_id, &tmp, &orient_applied, err, errcap)) {
        free(tmp.rgba);
        return 0;
    }
    if (!orient_applied) {
        if (!heif_apply_exif_orientation(data, size, ctx, item_id, &tmp, err, errcap)) {
            free(tmp.rgba);
            return 0;
        }
    }
    *out = tmp;
    return 1;
}

static int hevc_bs_fill(hevc_bitstream *bs, int need) {
    while (bs->bitcount < need) {
        if (bs->pos >= bs->size) {
            return 0;
        }
        bs->bitbuf = (bs->bitbuf << 8) | (uint64_t)bs->data[bs->pos++];
        bs->bitcount += 8;
    }
    return 1;
}

static int hevc_bs_exhausted(const hevc_bitstream *bs) {
    return bs->pos >= bs->size && bs->bitcount < 8;
}

static int hevc_bs_read_bits(hevc_bitstream *bs, int bits, uint32_t *out) {
    if (bits == 0) {
        *out = 0;
        return 1;
    }
    if (bits < 0 || bits > 32) {
        return 0;
    }
    if (!hevc_bs_fill(bs, bits)) {
        return 0;
    }
    int shift = bs->bitcount - bits;
    uint32_t mask = (bits == 32) ? 0xFFFFFFFFu : ((1u << bits) - 1u);
    *out = (uint32_t)((bs->bitbuf >> shift) & mask);
    bs->bitcount -= bits;
    if (bs->bitcount == 0) {
        bs->bitbuf = 0;
    } else if (bs->bitcount < 64) {
        bs->bitbuf &= ((1ull << bs->bitcount) - 1ull);
    }
    return 1;
}

static int hevc_bs_read_bit(hevc_bitstream *bs, uint32_t *out) {
    return hevc_bs_read_bits(bs, 1, out);
}

static int hevc_bs_read_ue(hevc_bitstream *bs, uint32_t *out) {
    uint32_t bit = 0;
    int leading_zero_bits = 0;
    while (1) {
        if (!hevc_bs_read_bit(bs, &bit)) {
            return 0;
        }
        if (bit) {
            break;
        }
        leading_zero_bits++;
        if (leading_zero_bits > 31) {
            return 0;
        }
    }
    if (leading_zero_bits == 0) {
        *out = 0;
        return 1;
    }
    uint32_t rest = 0;
    if (!hevc_bs_read_bits(bs, leading_zero_bits, &rest)) {
        return 0;
    }
    *out = ((1u << leading_zero_bits) - 1u) + rest;
    return 1;
}

static int hevc_bs_read_se(hevc_bitstream *bs, int32_t *out) {
    uint32_t ue = 0;
    if (!hevc_bs_read_ue(bs, &ue)) {
        return 0;
    }
    if (ue & 1u) {
        *out = (int32_t)((ue + 1u) >> 1);
    } else {
        *out = -(int32_t)(ue >> 1);
    }
    return 1;
}

static int hevc_bs_skip_bits(hevc_bitstream *bs, int bits) {
    uint32_t tmp = 0;
    while (bits > 0) {
        int step = bits > 32 ? 32 : bits;
        if (!hevc_bs_read_bits(bs, step, &tmp)) {
            return 0;
        }
        bits -= step;
    }
    return 1;
}

static int hevc_bs_align(hevc_bitstream *bs) {
    int mod = bs->bitcount & 7;
    if (mod == 0) {
        return 1;
    }
    uint32_t tmp = 0;
    return hevc_bs_read_bits(bs, mod, &tmp);
}

static int hevc_parse_profile_tier_level(hevc_bitstream *bs, int max_sub_layers_minus1) {
    uint32_t tmp = 0;
    if (max_sub_layers_minus1 < 0 || max_sub_layers_minus1 > 7) {
        return 0;
    }
    if (!hevc_bs_read_bits(bs, 2, &tmp)) return 0;
    if (!hevc_bs_read_bits(bs, 1, &tmp)) return 0;
    if (!hevc_bs_read_bits(bs, 5, &tmp)) return 0;
    if (!hevc_bs_read_bits(bs, 32, &tmp)) return 0;
    if (!hevc_bs_read_bits(bs, 16, &tmp)) return 0;
    if (!hevc_bs_read_bits(bs, 32, &tmp)) return 0;
    if (!hevc_bs_read_bits(bs, 8, &tmp)) return 0;

    uint8_t sub_profile[8] = {0};
    uint8_t sub_level[8] = {0};
    for (int i = 0; i < max_sub_layers_minus1; i++) {
        if (!hevc_bs_read_bit(bs, &tmp)) return 0;
        sub_profile[i] = (uint8_t)tmp;
        if (!hevc_bs_read_bit(bs, &tmp)) return 0;
        sub_level[i] = (uint8_t)tmp;
    }
    if (max_sub_layers_minus1 > 0) {
        for (int i = max_sub_layers_minus1; i < 8; i++) {
            if (!hevc_bs_read_bits(bs, 2, &tmp)) return 0;
        }
    }
    for (int i = 0; i < max_sub_layers_minus1; i++) {
        if (sub_profile[i]) {
            if (!hevc_bs_read_bits(bs, 2, &tmp)) return 0;
            if (!hevc_bs_read_bits(bs, 1, &tmp)) return 0;
            if (!hevc_bs_read_bits(bs, 5, &tmp)) return 0;
            if (!hevc_bs_read_bits(bs, 32, &tmp)) return 0;
            if (!hevc_bs_read_bits(bs, 16, &tmp)) return 0;
            if (!hevc_bs_read_bits(bs, 32, &tmp)) return 0;
        }
        if (sub_level[i]) {
            if (!hevc_bs_read_bits(bs, 8, &tmp)) return 0;
        }
    }
    return 1;
}

typedef struct hevc_cabac_ctx {
    uint8_t state;
    uint8_t mps;
} hevc_cabac_ctx;

typedef struct hevc_cabac {
    hevc_bitstream *bs;
    uint32_t code;
    uint16_t range;
} hevc_cabac;

static const uint8_t hevc_cabac_range_lps[64][4] = {
    {128, 176, 208, 240}, {128, 167, 197, 227}, {128, 158, 187, 216}, {123, 150, 178, 205},
    {116, 142, 169, 195}, {111, 135, 160, 185}, {105, 128, 152, 175}, {100, 122, 144, 166},
    {95, 116, 137, 158},  {90, 110, 130, 150},  {85, 104, 123, 142},  {81, 99, 117, 135},
    {77, 94, 111, 128},   {73, 89, 105, 122},   {69, 85, 100, 116},   {66, 80, 95, 110},
    {62, 76, 90, 104},    {59, 72, 86, 99},     {56, 69, 81, 94},     {53, 65, 77, 89},
    {51, 62, 73, 85},     {48, 59, 69, 80},     {46, 56, 66, 76},     {43, 53, 63, 72},
    {41, 50, 59, 69},     {39, 48, 56, 65},     {37, 45, 54, 62},     {35, 43, 51, 59},
    {33, 41, 48, 56},     {32, 39, 46, 53},     {30, 37, 43, 50},     {29, 35, 41, 48},
    {27, 33, 39, 45},     {26, 31, 37, 43},     {24, 30, 35, 41},     {23, 28, 33, 39},
    {22, 27, 32, 37},     {21, 26, 30, 35},     {20, 24, 29, 33},     {19, 23, 27, 31},
    {18, 22, 26, 30},     {17, 21, 25, 28},     {16, 20, 23, 27},     {15, 19, 22, 25},
    {14, 18, 21, 24},     {14, 17, 20, 23},     {13, 16, 19, 22},     {12, 15, 18, 21},
    {12, 14, 17, 20},     {11, 14, 16, 19},     {11, 13, 15, 18},     {10, 12, 15, 17},
    {10, 12, 14, 16},     {9, 11, 13, 15},      {9, 11, 12, 14},      {8, 10, 12, 14},
    {8, 9, 11, 13},       {7, 9, 11, 12},       {7, 9, 10, 12},       {7, 8, 10, 11},
    {6, 8, 9, 11},        {6, 7, 9, 10},        {6, 7, 8, 9},         {2, 2, 2, 2}
};

static const uint8_t hevc_cabac_trans_mps[64] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
    17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32,
    33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48,
    49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 62, 63
};

static const uint8_t hevc_cabac_trans_lps[64] = {
    0, 0, 1, 2, 2, 4, 4, 5, 6, 7, 8, 9, 9, 11, 11, 12,
    13, 13, 15, 15, 16, 16, 18, 18, 19, 19, 21, 21, 22, 22, 23, 24,
    24, 25, 26, 26, 27, 27, 28, 29, 29, 30, 30, 31, 32, 32, 33, 33,
    34, 35, 35, 36, 36, 37, 37, 38, 39, 39, 40, 40, 41, 42, 42, 63
};

static HEIF_UNUSED int hevc_cabac_init(hevc_cabac *cabac, hevc_bitstream *bs) {
    memset(cabac, 0, sizeof(*cabac));
    cabac->bs = bs;
    cabac->range = 510;
    cabac->code = 0;
    for (int i = 0; i < 9; i++) {
        uint32_t bit = 0;
        if (!hevc_bs_read_bits(bs, 1, &bit)) {
            return 0;
        }
        cabac->code = (cabac->code << 1) | bit;
    }
    return 1;
}

static int hevc_cabac_read_bit(hevc_cabac *cabac, uint32_t *bit) {
    return hevc_bs_read_bits(cabac->bs, 1, bit);
}

static int hevc_cabac_decode_bypass(hevc_cabac *cabac, uint32_t *bit) {
    uint32_t inbit = 0;
    if (!hevc_cabac_read_bit(cabac, &inbit)) {
        return 0;
    }
    cabac->code = (cabac->code << 1) | inbit;
    if (cabac->code >= ((uint32_t)cabac->range << 7)) {
        cabac->code -= ((uint32_t)cabac->range << 7);
        *bit = 1;
    } else {
        *bit = 0;
    }
    return 1;
}

static HEIF_UNUSED int hevc_cabac_decode_terminate(hevc_cabac *cabac, uint32_t *bit) {
    cabac->range -= 2;
    if (cabac->code >= ((uint32_t)cabac->range << 7)) {
        *bit = 1;
        return 1;
    }
    *bit = 0;
    return 1;
}

static HEIF_UNUSED int hevc_cabac_decode_bin(hevc_cabac *cabac, hevc_cabac_ctx *ctx, uint32_t *bit) {
    int idx = ctx->state;
    if (idx < 0) idx = 0;
    if (idx > 63) idx = 63;
    uint16_t range_lps = hevc_cabac_range_lps[idx][(cabac->range >> 6) & 3];
    uint16_t range_mps = cabac->range - range_lps;
    uint32_t scaled_range = (uint32_t)range_mps << 7;
    if (cabac->code < scaled_range) {
        cabac->range = range_mps;
        ctx->state = hevc_cabac_trans_mps[idx];
        *bit = ctx->mps;
    } else {
        cabac->code -= scaled_range;
        cabac->range = range_lps;
        if (idx == 0) {
            ctx->mps ^= 1u;
        }
        ctx->state = hevc_cabac_trans_lps[idx];
        *bit = ctx->mps ^ 1u;
    }
    while (cabac->range < 256) {
        cabac->range <<= 1;
        uint32_t inbit = 0;
        if (!hevc_cabac_read_bit(cabac, &inbit)) {
            return 0;
        }
        cabac->code = (cabac->code << 1) | inbit;
    }
    return 1;
}

#define HEVC_SIG_COEFF_CTXS 44

typedef struct hevc_cabac_state {
    hevc_cabac_ctx split_cu_flag[3];
    hevc_cabac_ctx split_tu_flag[3];
    hevc_cabac_ctx intra_split_flag;
    hevc_cabac_ctx prev_intra_pred_flag;
    hevc_cabac_ctx mpm_idx;
    hevc_cabac_ctx sig_coeff[HEVC_SIG_COEFF_CTXS];
    hevc_cabac_ctx last_sig_x[18];
    hevc_cabac_ctx last_sig_y[18];
    hevc_cabac_ctx coded_sub_block_flag[4];
    hevc_cabac_ctx greater1[24];
    hevc_cabac_ctx greater2[6];
    hevc_cabac_ctx intra_chroma_pred_mode;
    hevc_cabac_ctx cbf_luma[2];
    hevc_cabac_ctx cbf_cb[2];
    hevc_cabac_ctx cbf_cr[2];
    hevc_cabac_ctx cu_qp_delta_abs[2];
    hevc_cabac_ctx sao_merge_left;
    hevc_cabac_ctx sao_merge_up;
    hevc_cabac_ctx sao_type_idx_luma;
    hevc_cabac_ctx sao_type_idx_chroma;
    hevc_cabac_ctx sao_offset_abs[4];
} hevc_cabac_state;

static void hevc_cabac_init_ctx_value(hevc_cabac_ctx *ctx, int init_value, int qp) {
    int m = init_value >> 4;
    int n = init_value & 15;
    int init_state = ((m * qp) >> 4) + n;
    if (init_state < 1) init_state = 1;
    if (init_state > 126) init_state = 126;
    if (init_state >= 64) {
        ctx->state = (uint8_t)(63 - (init_state - 64));
        ctx->mps = 1;
    } else {
        ctx->state = (uint8_t)init_state;
        ctx->mps = 0;
    }
}

static void hevc_init_cabac_state(hevc_cabac_state *st, int slice_type, int qp) {
    static const uint8_t last_sig_init[3][18] = {
        {110, 110, 124, 125, 140, 153, 125, 127, 140, 109, 111, 143, 127, 111, 79, 108, 123, 63},
        {125, 110, 94, 110, 95, 79, 125, 111, 110, 78, 110, 111, 111, 95, 94, 108, 123, 108},
        {125, 110, 124, 110, 95, 94, 125, 111, 111, 79, 125, 126, 111, 111, 79, 108, 123, 93}
    };
    static const uint8_t coded_sb_init[3][4] = {
        {91, 171, 134, 141},
        {121, 140, 61, 154},
        {121, 140, 61, 154}
    };
    static const uint8_t greater1_init[3][24] = {
        {140, 92, 137, 138, 140, 152, 138, 139, 153, 74, 149, 92, 139, 107, 122, 152, 140, 179, 166, 182, 140, 227, 122, 197},
        {154, 196, 196, 167, 154, 152, 167, 182, 182, 134, 149, 136, 153, 121, 136, 137, 169, 194, 166, 167, 154, 167, 137, 182},
        {154, 196, 167, 167, 154, 152, 167, 182, 182, 134, 149, 136, 153, 121, 136, 122, 169, 208, 166, 167, 154, 152, 167, 182}
    };
    static const uint8_t greater2_init[3][6] = {
        {138, 153, 136, 167, 152, 152},
        {107, 167, 91, 122, 107, 167},
        {107, 167, 91, 107, 107, 167}
    };
    static const uint8_t sig_coeff_init[3][HEVC_SIG_COEFF_CTXS] = {
        {111, 111, 125, 110, 110, 94, 124, 108, 124, 107, 125, 141, 179, 153, 125, 107,
         125, 141, 179, 153, 125, 107, 125, 141, 179, 153, 125, 140, 139, 182, 182, 152,
         136, 152, 136, 153, 136, 139, 111, 136, 139, 111, 141, 111},
        {155, 154, 139, 153, 139, 123, 123, 63, 153, 166, 183, 140, 136, 153, 154, 166,
         183, 140, 136, 153, 154, 166, 183, 140, 136, 153, 154, 170, 153, 123, 123, 107,
         121, 107, 121, 167, 151, 183, 140, 151, 183, 140, 140, 140},
        {170, 154, 139, 153, 139, 123, 123, 63, 124, 166, 183, 140, 136, 153, 154, 166,
         183, 140, 136, 153, 154, 166, 183, 140, 136, 153, 154, 170, 153, 138, 138, 122,
         121, 122, 121, 167, 151, 183, 140, 151, 183, 140, 140, 140}
    };
    static const uint8_t split_cu_init[3][3] = {
        {154, 154, 154},
        {201, 154, 154},
        {201, 154, 154}
    };
    static const uint8_t split_tu_init[3][3] = {
        {154, 153, 138},
        {79, 124, 138},
        {79, 224, 167}
    };
    static const uint8_t part_mode_init[3][4] = {
        {154, 154, 184, 63},
        {154, 154, 154, 152},
        {154, 154, 183, 152}
    };
    static const uint8_t prev_intra_init[3] = {139, 139, 139};
    static const uint8_t mpm_idx_init[3] = {154, 110, 154};
    static const uint8_t intra_chroma_init[3] = {154, 122, 137};
    static const uint8_t cbf_luma_init[3][2] = {
        {138, 111},
        {94, 153},
        {122, 153}
    };
    static const uint8_t cbf_cb_init[3][2] = {
        {141, 94},
        {111, 149},
        {111, 149}
    };
    static const uint8_t cbf_cr_init[3][2] = {
        {138, 182},
        {107, 167},
        {92, 167}
    };
    static const uint8_t cu_qp_delta_abs_init[3][2] = {
        {154, 154},
        {154, 154},
        {154, 154}
    };
    static const uint8_t sao_merge_left_init[3] = {153, 153, 153};
    static const uint8_t sao_merge_up_init[3] = {153, 153, 153};
    static const uint8_t sao_type_luma_init[3] = {200, 185, 160};
    static const uint8_t sao_type_chroma_init[3] = {200, 185, 160};
    static const uint8_t sao_offset_abs_init[3][4] = {
        {154, 154, 154, 154},
        {154, 154, 154, 154},
        {154, 154, 154, 154}
    };
    int init_type = 0;
    if (slice_type == 2) {
        init_type = 0;
    } else if (slice_type == 1) {
        init_type = 1;
    } else {
        init_type = 2;
    }
    if (qp < 0) qp = 0;
    if (qp > 51) qp = 51;
    memset(st, 0, sizeof(*st));
    for (int i = 0; i < 18; i++) {
        hevc_cabac_init_ctx_value(&st->last_sig_x[i], last_sig_init[init_type][i], qp);
        hevc_cabac_init_ctx_value(&st->last_sig_y[i], last_sig_init[init_type][i], qp);
    }
    for (int i = 0; i < 4; i++) {
        hevc_cabac_init_ctx_value(&st->coded_sub_block_flag[i], coded_sb_init[init_type][i], qp);
    }
    for (int i = 0; i < 24; i++) {
        hevc_cabac_init_ctx_value(&st->greater1[i], greater1_init[init_type][i], qp);
    }
    for (int i = 0; i < 6; i++) {
        hevc_cabac_init_ctx_value(&st->greater2[i], greater2_init[init_type][i], qp);
    }
    for (int i = 0; i < HEVC_SIG_COEFF_CTXS; i++) {
        hevc_cabac_init_ctx_value(&st->sig_coeff[i], sig_coeff_init[init_type][i], qp);
    }
    for (int i = 0; i < 3; i++) {
        hevc_cabac_init_ctx_value(&st->split_cu_flag[i], split_cu_init[init_type][i], qp);
        hevc_cabac_init_ctx_value(&st->split_tu_flag[i], split_tu_init[init_type][i], qp);
    }
    hevc_cabac_init_ctx_value(&st->intra_split_flag, part_mode_init[init_type][0], qp);
    hevc_cabac_init_ctx_value(&st->prev_intra_pred_flag, prev_intra_init[init_type], qp);
    hevc_cabac_init_ctx_value(&st->mpm_idx, mpm_idx_init[init_type], qp);
    hevc_cabac_init_ctx_value(&st->intra_chroma_pred_mode, intra_chroma_init[init_type], qp);
    for (int i = 0; i < 2; i++) {
        hevc_cabac_init_ctx_value(&st->cbf_luma[i], cbf_luma_init[init_type][i], qp);
        hevc_cabac_init_ctx_value(&st->cbf_cb[i], cbf_cb_init[init_type][i], qp);
        hevc_cabac_init_ctx_value(&st->cbf_cr[i], cbf_cr_init[init_type][i], qp);
        hevc_cabac_init_ctx_value(&st->cu_qp_delta_abs[i], cu_qp_delta_abs_init[init_type][i], qp);
    }
    hevc_cabac_init_ctx_value(&st->sao_merge_left, sao_merge_left_init[init_type], qp);
    hevc_cabac_init_ctx_value(&st->sao_merge_up, sao_merge_up_init[init_type], qp);
    hevc_cabac_init_ctx_value(&st->sao_type_idx_luma, sao_type_luma_init[init_type], qp);
    hevc_cabac_init_ctx_value(&st->sao_type_idx_chroma, sao_type_chroma_init[init_type], qp);
    for (int i = 0; i < 4; i++) {
        hevc_cabac_init_ctx_value(&st->sao_offset_abs[i], sao_offset_abs_init[init_type][i], qp);
    }
}

static HEIF_UNUSED int hevc_cabac_decode_ue_bypass(hevc_cabac *cabac, uint32_t *val) {
    uint32_t zeros = 0;
    uint32_t bit = 0;
    while (1) {
        if (!hevc_cabac_decode_bypass(cabac, &bit)) {
            return 0;
        }
        if (bit) {
            break;
        }
        zeros++;
        if (zeros > 31) {
            return 0;
        }
    }
    if (zeros == 0) {
        *val = 0;
        return 1;
    }
    uint32_t suffix = 0;
    for (uint32_t i = 0; i < zeros; i++) {
        if (!hevc_cabac_decode_bypass(cabac, &bit)) {
            return 0;
        }
        suffix = (suffix << 1) | bit;
    }
    *val = ((1u << zeros) - 1u) + suffix;
    return 1;
}

static int hevc_decode_cu_qp_delta(hevc_cabac *cabac, hevc_cabac_state *st, int *delta) {
    static int fail_count = 0;
    uint32_t bin = 0;
    uint32_t abs_level = 0;
    if (!hevc_cabac_decode_bin(cabac, &st->cu_qp_delta_abs[0], &bin)) {
        if (fail_count < 3) {
            heif_debugf("cu_qp_delta FAIL at bin0: bs_pos=%zu bs_size=%zu bitcount=%d\n",
                       cabac->bs->pos, cabac->bs->size, cabac->bs->bitcount);
            fail_count++;
        }
        return 0;
    }
    if (bin == 0) {
        *delta = 0;
        return 1;
    }
    abs_level = 1;
    if (!hevc_cabac_decode_bin(cabac, &st->cu_qp_delta_abs[1], &bin)) {
        if (fail_count < 3) {
            heif_debugf("cu_qp_delta FAIL at bin1: bs_pos=%zu bs_size=%zu bitcount=%d\n",
                       cabac->bs->pos, cabac->bs->size, cabac->bs->bitcount);
            fail_count++;
        }
        return 0;
    }
    if (bin != 0) {
        abs_level = 2;
        int loop_count = 0;
        while (1) {
            if (!hevc_cabac_decode_bypass(cabac, &bin)) {
                if (fail_count < 3) {
                    heif_debugf("cu_qp_delta FAIL at bypass loop_count=%d: bs_pos=%zu bs_size=%zu bitcount=%d\n",
                               loop_count, cabac->bs->pos, cabac->bs->size, cabac->bs->bitcount);
                    fail_count++;
                }
                return 0;
            }
            if (bin == 0) {
                break;
            }
            abs_level++;
            loop_count++;
            if (abs_level > 51) {
                abs_level = 51;
                break;
            }
        }
    }
    uint32_t sign = 0;
    if (!hevc_cabac_decode_bypass(cabac, &sign)) {
        if (fail_count < 3) {
            heif_debugf("cu_qp_delta FAIL at sign: bs_pos=%zu bs_size=%zu bitcount=%d\n",
                       cabac->bs->pos, cabac->bs->size, cabac->bs->bitcount);
            fail_count++;
        }
        return 0;
    }
    *delta = sign ? -(int)abs_level : (int)abs_level;
    return 1;
}

static int hevc_decode_sao_type(hevc_cabac *cabac, hevc_cabac_ctx *ctx, uint32_t *type) {
    uint32_t bin = 0;
    if (!hevc_cabac_decode_bin(cabac, ctx, &bin)) {
        return 0;
    }
    if (bin == 0) {
        *type = 0;
        return 1;
    }
    if (!hevc_cabac_decode_bypass(cabac, &bin)) {
        return 0;
    }
    *type = bin ? 2u : 1u;
    return 1;
}

static int hevc_decode_sao_offset_abs(hevc_cabac *cabac, hevc_cabac_state *st, uint32_t *val) {
    uint32_t bin = 0;
    for (uint32_t k = 0; k < 7; k++) {
        int ctx = (k < 4) ? (int)k : 3;
        if (!hevc_cabac_decode_bin(cabac, &st->sao_offset_abs[ctx], &bin)) {
            return 0;
        }
        if (bin == 0) {
            *val = k;
            return 1;
        }
    }
    *val = 7;
    return 1;
}

static int hevc_decode_sao_component(hevc_cabac *cabac, hevc_cabac_state *st,
                                     hevc_sao *sao, int bit_depth, int is_chroma) {
    uint32_t type = 0;
    if (!hevc_decode_sao_type(cabac,
                              is_chroma ? &st->sao_type_idx_chroma : &st->sao_type_idx_luma,
                              &type)) {
        return 0;
    }
    sao->type = (uint8_t)type;
    sao->band_pos = 0;
    sao->eo_class = 0;
    for (int i = 0; i < 4; i++) {
        sao->offset[i] = 0;
    }
    if (type == 0) {
        return 1;
    }
    int max_offset = (1 << (bit_depth - 5)) - 1;
    if (max_offset > 7) max_offset = 7;
    if (max_offset < 0) max_offset = 0;
    for (int i = 0; i < 4; i++) {
        uint32_t abs_val = 0;
        if (!hevc_decode_sao_offset_abs(cabac, st, &abs_val)) {
            return 0;
        }
        if ((int)abs_val > max_offset) {
            abs_val = (uint32_t)max_offset;
        }
        if (abs_val > 0) {
            uint32_t sign = 0;
            if (!hevc_cabac_decode_bypass(cabac, &sign)) {
                return 0;
            }
            sao->offset[i] = sign ? -(int8_t)abs_val : (int8_t)abs_val;
        }
    }
    if (type == 1) {
        uint32_t band_pos = 0;
        for (int i = 0; i < 5; i++) {
            uint32_t b = 0;
            if (!hevc_cabac_decode_bypass(cabac, &b)) {
                return 0;
            }
            band_pos = (band_pos << 1) | b;
        }
        sao->band_pos = (uint8_t)(band_pos & 31u);
    } else {
        uint32_t eo_class = 0;
        for (int i = 0; i < 2; i++) {
            uint32_t b = 0;
            if (!hevc_cabac_decode_bypass(cabac, &b)) {
                return 0;
            }
            eo_class = (eo_class << 1) | b;
        }
        sao->eo_class = (uint8_t)(eo_class & 3u);
    }
    return 1;
}

static int hevc_decode_sao_ctu(hevc_context *ctx, hevc_cabac *cabac, hevc_cabac_state *st,
                               const hevc_sps *sps,
                               int sao_luma, int sao_chroma,
                               uint32_t cx, uint32_t cy) {
    if (!ctx || !sps || !ctx->sao_map) {
        return 1;
    }
    hevc_sao_ctu *cur = &ctx->sao_map[cy * ctx->sao_stride + cx];
    if (!sao_luma && !sao_chroma) {
        memset(cur, 0, sizeof(*cur));
        return 1;
    }
    uint32_t merge_left = 0;
    if (cx > 0) {
        if (!hevc_cabac_decode_bin(cabac, &st->sao_merge_left, &merge_left)) {
            return 0;
        }
    }
    if (merge_left) {
        *cur = ctx->sao_map[cy * ctx->sao_stride + (cx - 1)];
        return 1;
    }
    uint32_t merge_up = 0;
    if (cy > 0) {
        if (!hevc_cabac_decode_bin(cabac, &st->sao_merge_up, &merge_up)) {
            return 0;
        }
    }
    if (merge_up) {
        *cur = ctx->sao_map[(cy - 1) * ctx->sao_stride + cx];
        return 1;
    }
    if (sao_luma) {
        if (!hevc_decode_sao_component(cabac, st, &cur->luma,
                                       (int)sps->bit_depth_luma, 0)) {
            return 0;
        }
    } else {
        memset(&cur->luma, 0, sizeof(cur->luma));
    }
    if (sao_chroma && sps->chroma_format_idc != 0) {
        if (!hevc_decode_sao_component(cabac, st, &cur->cb,
                                       (int)sps->bit_depth_chroma, 1) ||
            !hevc_decode_sao_component(cabac, st, &cur->cr,
                                       (int)sps->bit_depth_chroma, 1)) {
            return 0;
        }
    } else {
        memset(&cur->cb, 0, sizeof(cur->cb));
        memset(&cur->cr, 0, sizeof(cur->cr));
    }
    return 1;
}

static int hevc_decode_coeff_abs_level_remaining(hevc_cabac *cabac, int cRiceParam, uint32_t *val) {
    uint32_t prefix = 0;
    uint32_t bit = 0;
    while (prefix < 31) {
        if (!hevc_cabac_decode_bypass(cabac, &bit)) {
            return 0;
        }
        if (bit == 0) {
            break;
        }
        prefix++;
    }
    if (prefix < 3) {
        uint32_t suffix = 0;
        for (int i = 0; i < cRiceParam; i++) {
            if (!hevc_cabac_decode_bypass(cabac, &bit)) {
                return 0;
            }
            suffix = (suffix << 1) | bit;
        }
        *val = (prefix << cRiceParam) + suffix;
        return 1;
    }
    uint32_t prefix_minus3 = prefix - 3;
    uint32_t suffix = 0;
    int suffix_bits = (int)prefix_minus3 + cRiceParam;
    for (int i = 0; i < suffix_bits; i++) {
        if (!hevc_cabac_decode_bypass(cabac, &bit)) {
            return 0;
        }
        suffix = (suffix << 1) | bit;
    }
    *val = (((1u << prefix_minus3) + 3u - 1u) << cRiceParam) + suffix;
    return 1;
}

static int hevc_skip_short_term_ref_pic_set(hevc_bitstream *bs, int st_rps_idx,
                                            int num_rps, uint8_t *num_delta_pocs) {
    uint32_t inter_ref_pic_set_prediction_flag = 0;
    if (st_rps_idx != 0) {
        if (!hevc_bs_read_bit(bs, &inter_ref_pic_set_prediction_flag)) {
            return 0;
        }
    }
    if (inter_ref_pic_set_prediction_flag) {
        uint32_t delta_idx_minus1 = 0;
        if (st_rps_idx == num_rps) {
            if (!hevc_bs_read_ue(bs, &delta_idx_minus1)) {
                return 0;
            }
        }
        int ref_idx = st_rps_idx - (int)delta_idx_minus1 - 1;
        if (ref_idx < 0) {
            ref_idx = 0;
        }
        int num_delta = 0;
        if (ref_idx < HEVC_MAX_SHORT_TERM_RPS) {
            num_delta = num_delta_pocs[ref_idx];
        }
        uint32_t delta_rps_sign = 0;
        uint32_t abs_delta_rps_minus1 = 0;
        if (!hevc_bs_read_bit(bs, &delta_rps_sign) ||
            !hevc_bs_read_ue(bs, &abs_delta_rps_minus1)) {
            return 0;
        }
        (void)delta_rps_sign;
        (void)abs_delta_rps_minus1;
        int num_ref_idc = num_delta + 1;
        int count = 0;
        for (int j = 0; j < num_ref_idc; j++) {
            uint32_t used = 0;
            if (!hevc_bs_read_bit(bs, &used)) {
                return 0;
            }
            if (!used) {
                uint32_t use_delta = 0;
                if (!hevc_bs_read_bit(bs, &use_delta)) {
                    return 0;
                }
                if (use_delta) {
                    count++;
                }
            } else {
                count++;
            }
        }
        if (st_rps_idx < HEVC_MAX_SHORT_TERM_RPS) {
            num_delta_pocs[st_rps_idx] = (uint8_t)count;
        }
        return 1;
    }
    uint32_t num_negative = 0;
    uint32_t num_positive = 0;
    if (!hevc_bs_read_ue(bs, &num_negative) ||
        !hevc_bs_read_ue(bs, &num_positive)) {
        return 0;
    }
    for (uint32_t i = 0; i < num_negative; i++) {
        uint32_t tmp = 0;
        if (!hevc_bs_read_ue(bs, &tmp) || !hevc_bs_read_bit(bs, &tmp)) {
            return 0;
        }
    }
    for (uint32_t i = 0; i < num_positive; i++) {
        uint32_t tmp = 0;
        if (!hevc_bs_read_ue(bs, &tmp) || !hevc_bs_read_bit(bs, &tmp)) {
            return 0;
        }
    }
    if (st_rps_idx < HEVC_MAX_SHORT_TERM_RPS) {
        uint32_t total = num_negative + num_positive;
        if (total > 255) total = 255;
        num_delta_pocs[st_rps_idx] = (uint8_t)total;
    }
    return 1;
}

static int hevc_skip_sub_layer_hrd_parameters(hevc_bitstream *bs, int cpb_cnt_minus1,
                                              int sub_pic_hrd_params_present_flag) {
    for (int i = 0; i <= cpb_cnt_minus1; i++) {
        uint32_t tmp = 0;
        if (!hevc_bs_read_ue(bs, &tmp) || !hevc_bs_read_ue(bs, &tmp)) {
            return 0;
        }
        if (sub_pic_hrd_params_present_flag) {
            if (!hevc_bs_read_ue(bs, &tmp) || !hevc_bs_read_ue(bs, &tmp)) {
                return 0;
            }
        }
        if (!hevc_bs_read_bit(bs, &tmp)) {
            return 0;
        }
    }
    return 1;
}

static int hevc_skip_hrd_parameters(hevc_bitstream *bs, int common_inf_present_flag,
                                    int max_num_sub_layers_minus1) {
    uint32_t tmp = 0;
    uint32_t nal_hrd_parameters_present_flag = 0;
    uint32_t vcl_hrd_parameters_present_flag = 0;
    uint32_t sub_pic_hrd_params_present_flag = 0;
    if (common_inf_present_flag) {
        if (!hevc_bs_read_bit(bs, &nal_hrd_parameters_present_flag) ||
            !hevc_bs_read_bit(bs, &vcl_hrd_parameters_present_flag)) {
            return 0;
        }
        if (nal_hrd_parameters_present_flag || vcl_hrd_parameters_present_flag) {
            if (!hevc_bs_read_bit(bs, &sub_pic_hrd_params_present_flag)) {
                return 0;
            }
            if (sub_pic_hrd_params_present_flag) {
                uint32_t tmp = 0;
                if (!hevc_bs_read_bits(bs, 8, &tmp) ||
                    !hevc_bs_read_bits(bs, 5, &tmp) ||
                    !hevc_bs_read_bit(bs, &tmp) ||
                    !hevc_bs_read_bits(bs, 5, &tmp)) {
                    return 0;
                }
            }
            uint32_t tmp = 0;
            if (!hevc_bs_read_bits(bs, 4, &tmp) ||
                !hevc_bs_read_bits(bs, 4, &tmp)) {
                return 0;
            }
            if (sub_pic_hrd_params_present_flag) {
                if (!hevc_bs_read_bits(bs, 4, &tmp)) {
                    return 0;
                }
            }
            if (!hevc_bs_read_bits(bs, 5, &tmp) ||
                !hevc_bs_read_bits(bs, 5, &tmp) ||
                !hevc_bs_read_bits(bs, 5, &tmp)) {
                return 0;
            }
        }
    }
    for (int i = 0; i <= max_num_sub_layers_minus1; i++) {
        uint32_t fixed_pic_rate_general_flag = 0;
        uint32_t fixed_pic_rate_within_cvs_flag = 0;
        uint32_t low_delay_hrd_flag = 0;
        uint32_t cpb_cnt_minus1 = 0;
        if (!hevc_bs_read_bit(bs, &fixed_pic_rate_general_flag)) {
            return 0;
        }
        if (!fixed_pic_rate_general_flag) {
            if (!hevc_bs_read_bit(bs, &fixed_pic_rate_within_cvs_flag)) {
                return 0;
            }
        } else {
            fixed_pic_rate_within_cvs_flag = 1;
        }
        if (fixed_pic_rate_within_cvs_flag) {
            if (!hevc_bs_read_ue(bs, &tmp)) {
                return 0;
            }
            low_delay_hrd_flag = 0;
        } else {
            if (!hevc_bs_read_bit(bs, &low_delay_hrd_flag)) {
                return 0;
            }
        }
        if (!low_delay_hrd_flag) {
            if (!hevc_bs_read_ue(bs, &cpb_cnt_minus1)) {
                return 0;
            }
        }
        if (nal_hrd_parameters_present_flag) {
            if (!hevc_skip_sub_layer_hrd_parameters(bs, (int)cpb_cnt_minus1,
                                                    (int)sub_pic_hrd_params_present_flag)) {
                return 0;
            }
        }
        if (vcl_hrd_parameters_present_flag) {
            if (!hevc_skip_sub_layer_hrd_parameters(bs, (int)cpb_cnt_minus1,
                                                    (int)sub_pic_hrd_params_present_flag)) {
                return 0;
            }
        }
    }
    return 1;
}

static int hevc_parse_vui(hevc_bitstream *bs, int max_num_sub_layers_minus1,
                          uint8_t *video_full_range_flag,
                          uint8_t *colour_primaries,
                          uint8_t *transfer_characteristics,
                          uint8_t *matrix_coefficients) {
    uint32_t tmp = 0;
    uint32_t aspect_ratio_info_present_flag = 0;
    if (!hevc_bs_read_bit(bs, &aspect_ratio_info_present_flag)) {
        return 0;
    }
    if (aspect_ratio_info_present_flag) {
        if (!hevc_bs_read_bits(bs, 8, &tmp)) {
            return 0;
        }
        if (tmp == 255) {
            if (!hevc_bs_read_bits(bs, 16, &tmp) ||
                !hevc_bs_read_bits(bs, 16, &tmp)) {
                return 0;
            }
        }
    }
    uint32_t overscan_info_present_flag = 0;
    if (!hevc_bs_read_bit(bs, &overscan_info_present_flag)) {
        return 0;
    }
    if (overscan_info_present_flag) {
        if (!hevc_bs_read_bit(bs, &tmp)) {
            return 0;
        }
    }
    uint32_t video_signal_type_present_flag = 0;
    if (!hevc_bs_read_bit(bs, &video_signal_type_present_flag)) {
        return 0;
    }
    if (video_signal_type_present_flag) {
        if (!hevc_bs_read_bits(bs, 3, &tmp)) {
            return 0;
        }
        uint32_t full_range = 0;
        if (!hevc_bs_read_bit(bs, &full_range)) {
            return 0;
        }
        *video_full_range_flag = (uint8_t)full_range;
        uint32_t colour_description_present_flag = 0;
        if (!hevc_bs_read_bit(bs, &colour_description_present_flag)) {
            return 0;
        }
        if (colour_description_present_flag) {
            uint32_t cp = 0, tc = 0, mc = 0;
            if (!hevc_bs_read_bits(bs, 8, &cp) ||
                !hevc_bs_read_bits(bs, 8, &tc) ||
                !hevc_bs_read_bits(bs, 8, &mc)) {
                return 0;
            }
            *colour_primaries = (uint8_t)cp;
            *transfer_characteristics = (uint8_t)tc;
            *matrix_coefficients = (uint8_t)mc;
        }
    }
    uint32_t chroma_loc_info_present_flag = 0;
    if (!hevc_bs_read_bit(bs, &chroma_loc_info_present_flag)) {
        return 0;
    }
    if (chroma_loc_info_present_flag) {
        if (!hevc_bs_read_ue(bs, &tmp) || !hevc_bs_read_ue(bs, &tmp)) {
            return 0;
        }
    }
    if (!hevc_bs_read_bit(bs, &tmp) ||
        !hevc_bs_read_bit(bs, &tmp) ||
        !hevc_bs_read_bit(bs, &tmp)) {
        return 0;
    }
    uint32_t default_display_window_flag = 0;
    if (!hevc_bs_read_bit(bs, &default_display_window_flag)) {
        return 0;
    }
    if (default_display_window_flag) {
        if (!hevc_bs_read_ue(bs, &tmp) ||
            !hevc_bs_read_ue(bs, &tmp) ||
            !hevc_bs_read_ue(bs, &tmp) ||
            !hevc_bs_read_ue(bs, &tmp)) {
            return 0;
        }
    }
    uint32_t vui_timing_info_present_flag = 0;
    if (!hevc_bs_read_bit(bs, &vui_timing_info_present_flag)) {
        return 0;
    }
    if (vui_timing_info_present_flag) {
        if (!hevc_bs_read_bits(bs, 32, &tmp) ||
            !hevc_bs_read_bits(bs, 32, &tmp)) {
            return 0;
        }
        uint32_t vui_poc_proportional_to_timing_flag = 0;
        if (!hevc_bs_read_bit(bs, &vui_poc_proportional_to_timing_flag)) {
            return 0;
        }
        if (vui_poc_proportional_to_timing_flag) {
            if (!hevc_bs_read_ue(bs, &tmp)) {
                return 0;
            }
        }
        uint32_t vui_hrd_parameters_present_flag = 0;
        if (!hevc_bs_read_bit(bs, &vui_hrd_parameters_present_flag)) {
            return 0;
        }
        if (vui_hrd_parameters_present_flag) {
            if (!hevc_skip_hrd_parameters(bs, 1, max_num_sub_layers_minus1)) {
                return 0;
            }
        }
    }
    uint32_t bitstream_restriction_flag = 0;
    if (!hevc_bs_read_bit(bs, &bitstream_restriction_flag)) {
        return 0;
    }
    if (bitstream_restriction_flag) {
        if (!hevc_bs_read_bit(bs, &tmp) ||
            !hevc_bs_read_bit(bs, &tmp) ||
            !hevc_bs_read_bit(bs, &tmp)) {
            return 0;
        }
        if (!hevc_bs_read_ue(bs, &tmp) ||
            !hevc_bs_read_ue(bs, &tmp) ||
            !hevc_bs_read_ue(bs, &tmp) ||
            !hevc_bs_read_ue(bs, &tmp) ||
            !hevc_bs_read_ue(bs, &tmp)) {
            return 0;
        }
    }
    return 1;
}

static const int hevc_inv_quant_scale[6] = {40, 45, 51, 57, 64, 72};

static const uint8_t hevc_default_scaling_list_4x4_intra[16] = {
    6, 13, 20, 28,
    13, 20, 28, 32,
    20, 28, 32, 37,
    28, 32, 37, 42
};

static const uint8_t hevc_default_scaling_list_4x4_inter[16] = {
    10, 14, 20, 24,
    14, 20, 24, 27,
    20, 24, 27, 30,
    24, 27, 30, 34
};

static const uint8_t hevc_default_scaling_list_8x8_intra[64] = {
    6, 10, 13, 16, 18, 23, 25, 27,
    10, 11, 16, 18, 23, 25, 27, 29,
    13, 16, 18, 23, 25, 27, 29, 31,
    16, 18, 23, 25, 27, 29, 31, 33,
    18, 23, 25, 27, 29, 31, 33, 36,
    23, 25, 27, 29, 31, 33, 36, 38,
    25, 27, 29, 31, 33, 36, 38, 40,
    27, 29, 31, 33, 36, 38, 40, 42
};

static const uint8_t hevc_default_scaling_list_8x8_inter[64] = {
    9, 13, 15, 17, 19, 21, 22, 24,
    13, 13, 17, 19, 21, 22, 24, 25,
    15, 17, 19, 21, 22, 24, 25, 27,
    17, 19, 21, 22, 24, 25, 27, 28,
    19, 21, 22, 24, 25, 27, 28, 30,
    21, 22, 24, 25, 27, 28, 30, 32,
    22, 24, 25, 27, 28, 30, 32, 33,
    24, 25, 27, 28, 30, 32, 33, 35
};

static const uint8_t hevc_default_scaling_list_16x16_intra[64] = {
    16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 17, 16, 17, 16, 17, 18,
    17, 18, 18, 17, 18, 21, 19, 20,
    21, 20, 19, 21, 24, 22, 22, 24,
    24, 22, 22, 24, 25, 25, 27, 30,
    27, 25, 25, 29, 31, 35, 35, 31,
    29, 36, 41, 44, 41, 36, 47, 54,
    54, 47, 65, 70, 65, 88, 88, 115
};

static const uint8_t hevc_default_scaling_list_16x16_inter[64] = {
    16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 17, 17, 17, 17, 17, 18,
    18, 18, 18, 18, 18, 20, 20, 20,
    20, 20, 20, 20, 24, 24, 24, 24,
    24, 24, 24, 24, 25, 25, 25, 25,
    25, 25, 25, 28, 28, 28, 28, 28,
    28, 33, 33, 33, 33, 33, 41, 41,
    41, 41, 54, 54, 54, 71, 71, 91
};

static int hevc_scaling_value(int size, int intra, int x, int y) {
    if (size == 4) {
        const uint8_t *scaling = intra ? hevc_default_scaling_list_4x4_intra
                                        : hevc_default_scaling_list_4x4_inter;
        return scaling[y * 4 + x];
    }
    if (size == 8) {
        const uint8_t *scaling = intra ? hevc_default_scaling_list_8x8_intra
                                        : hevc_default_scaling_list_8x8_inter;
        return scaling[y * 8 + x];
    }
    const uint8_t *base = intra ? hevc_default_scaling_list_16x16_intra
                                : hevc_default_scaling_list_16x16_inter;
    int shift = (size == 16) ? 1 : 2;
    int bx = x >> shift;
    int by = y >> shift;
    int val = base[by * 8 + bx];
    if (x == 0 && y == 0) {
        val += 8;
    }
    return val;
}

static void hevc_dequantize_block(const int16_t *qcoeffs, int32_t *dst, int size,
                                  int qp, int bit_depth, int intra) {
    int log2_size = hevc_log2_size(size);
    int shift = 15 - bit_depth - log2_size;
    if (shift < 0) {
        shift = 0;
    }
    int add = (shift > 0) ? (1 << (shift - 1)) : 0;
    int per = qp / 6;
    int rem = qp % 6;
    int total = size * size;
    int scale = hevc_inv_quant_scale[rem];
    /* Use flat scaling matrix value of 16 when scaling lists not present */
    const int flat_scale = 16;
    (void)intra;
    for (int i = 0; i < total; i++) {
        int level = qcoeffs[i];
        if (level == 0) {
            dst[i] = 0;
            continue;
        }
        int64_t val = (int64_t)level * scale * flat_scale;
        if (shift > 0) {
            val = (val + add) >> shift;
        }
        if (per > 0) {
            val <<= per;
        }
        if (val > INT32_MAX) val = INT32_MAX;
        if (val < INT32_MIN) val = INT32_MIN;
        dst[i] = (int32_t)val;
    }
}

static int hevc_parse_sps(const uint8_t *rbsp, size_t rbsp_size, hevc_context *ctx,
                          char *err, size_t errcap) {
    hevc_bitstream bs;
    memset(&bs, 0, sizeof(bs));
    bs.data = rbsp;
    bs.size = rbsp_size;
    uint32_t tmp = 0;
    if (!hevc_bs_read_bits(&bs, 4, &tmp)) {
        set_err(err, errcap, "invalid HEVC SPS");
        return 0;
    }
    if (!hevc_bs_read_bits(&bs, 3, &tmp)) {
        set_err(err, errcap, "invalid HEVC SPS");
        return 0;
    }
    int max_sub_layers_minus1 = (int)tmp;
    if (!hevc_bs_read_bits(&bs, 1, &tmp)) {
        set_err(err, errcap, "invalid HEVC SPS");
        return 0;
    }
    if (!hevc_parse_profile_tier_level(&bs, max_sub_layers_minus1)) {
        set_err(err, errcap, "invalid HEVC SPS profile");
        return 0;
    }
    uint32_t sps_id = 0;
    if (!hevc_bs_read_ue(&bs, &sps_id)) {
        set_err(err, errcap, "invalid HEVC SPS");
        return 0;
    }
    if (sps_id >= HEVC_MAX_SPS) {
        set_err(err, errcap, "HEVC SPS id out of range");
        return 0;
    }
    uint32_t chroma_format_idc = 0;
    if (!hevc_bs_read_ue(&bs, &chroma_format_idc)) {
        set_err(err, errcap, "invalid HEVC SPS");
        return 0;
    }
    uint32_t separate_colour_plane_flag = 0;
    if (chroma_format_idc == 3) {
        if (!hevc_bs_read_bit(&bs, &separate_colour_plane_flag)) {
            set_err(err, errcap, "invalid HEVC SPS");
            return 0;
        }
    }
    uint32_t width = 0;
    uint32_t height = 0;
    if (!hevc_bs_read_ue(&bs, &width) || !hevc_bs_read_ue(&bs, &height)) {
        set_err(err, errcap, "invalid HEVC SPS");
        return 0;
    }
    uint32_t conformance_window_flag = 0;
    if (!hevc_bs_read_bit(&bs, &conformance_window_flag)) {
        set_err(err, errcap, "invalid HEVC SPS");
        return 0;
    }
    uint32_t conf_left = 0, conf_right = 0, conf_top = 0, conf_bottom = 0;
    if (conformance_window_flag) {
        if (!hevc_bs_read_ue(&bs, &conf_left) ||
            !hevc_bs_read_ue(&bs, &conf_right) ||
            !hevc_bs_read_ue(&bs, &conf_top) ||
            !hevc_bs_read_ue(&bs, &conf_bottom)) {
            set_err(err, errcap, "invalid HEVC SPS");
            return 0;
        }
    }
    uint32_t bit_depth_luma_minus8 = 0;
    uint32_t bit_depth_chroma_minus8 = 0;
    if (!hevc_bs_read_ue(&bs, &bit_depth_luma_minus8) ||
        !hevc_bs_read_ue(&bs, &bit_depth_chroma_minus8)) {
        set_err(err, errcap, "invalid HEVC SPS");
        return 0;
    }
    uint32_t log2_max_pic_order_cnt_lsb_minus4 = 0;
    if (!hevc_bs_read_ue(&bs, &log2_max_pic_order_cnt_lsb_minus4)) {
        set_err(err, errcap, "invalid HEVC SPS");
        return 0;
    }
    (void)log2_max_pic_order_cnt_lsb_minus4;
    uint32_t sub_layer_ordering_info_present_flag = 0;
    if (!hevc_bs_read_bit(&bs, &sub_layer_ordering_info_present_flag)) {
        set_err(err, errcap, "invalid HEVC SPS");
        return 0;
    }
    int start_layer = sub_layer_ordering_info_present_flag ? 0 : max_sub_layers_minus1;
    for (int i = start_layer; i <= max_sub_layers_minus1; i++) {
        uint32_t tmp_ue = 0;
        if (!hevc_bs_read_ue(&bs, &tmp_ue) ||
            !hevc_bs_read_ue(&bs, &tmp_ue) ||
            !hevc_bs_read_ue(&bs, &tmp_ue)) {
            set_err(err, errcap, "invalid HEVC SPS");
            return 0;
        }
    }
    uint32_t log2_min_luma_coding_block_size_minus3 = 0;
    uint32_t log2_diff_max_min_luma_coding_block_size = 0;
    if (!hevc_bs_read_ue(&bs, &log2_min_luma_coding_block_size_minus3) ||
        !hevc_bs_read_ue(&bs, &log2_diff_max_min_luma_coding_block_size)) {
        set_err(err, errcap, "invalid HEVC SPS");
        return 0;
    }

    uint32_t log2_min_luma_transform_block_size_minus2 = 0;
    uint32_t log2_diff_max_min_luma_transform_block_size = 0;
    uint32_t max_transform_hierarchy_depth_inter = 0;
    uint32_t max_transform_hierarchy_depth_intra = 0;
    if (!hevc_bs_read_ue(&bs, &log2_min_luma_transform_block_size_minus2) ||
        !hevc_bs_read_ue(&bs, &log2_diff_max_min_luma_transform_block_size) ||
        !hevc_bs_read_ue(&bs, &max_transform_hierarchy_depth_inter) ||
        !hevc_bs_read_ue(&bs, &max_transform_hierarchy_depth_intra)) {
        set_err(err, errcap, "invalid HEVC SPS");
        return 0;
    }
    (void)max_transform_hierarchy_depth_inter;

    uint32_t scaling_list_enabled_flag = 0;
    if (!hevc_bs_read_bit(&bs, &scaling_list_enabled_flag)) {
        set_err(err, errcap, "invalid HEVC SPS");
        return 0;
    }
    if (scaling_list_enabled_flag) {
        uint32_t sps_scaling_list_data_present_flag = 0;
        if (!hevc_bs_read_bit(&bs, &sps_scaling_list_data_present_flag)) {
            set_err(err, errcap, "invalid HEVC SPS");
            return 0;
        }
        if (sps_scaling_list_data_present_flag) {
            set_err(err, errcap, "unsupported HEVC scaling list");
            return 0;
        }
    }
    uint32_t amp_enabled_flag = 0;
    if (!hevc_bs_read_bit(&bs, &amp_enabled_flag)) {
        set_err(err, errcap, "invalid HEVC SPS");
        return 0;
    }
    (void)amp_enabled_flag;
    uint32_t sample_adaptive_offset_enabled_flag = 0;
    if (!hevc_bs_read_bit(&bs, &sample_adaptive_offset_enabled_flag)) {
        set_err(err, errcap, "invalid HEVC SPS");
        return 0;
    }
    uint32_t pcm_enabled_flag = 0;
    if (!hevc_bs_read_bit(&bs, &pcm_enabled_flag)) {
        set_err(err, errcap, "invalid HEVC SPS");
        return 0;
    }
    if (pcm_enabled_flag) {
        if (!hevc_bs_read_bits(&bs, 4, &tmp) ||
            !hevc_bs_read_bits(&bs, 4, &tmp)) {
            set_err(err, errcap, "invalid HEVC SPS");
            return 0;
        }
        if (!hevc_bs_read_ue(&bs, &tmp) ||
            !hevc_bs_read_ue(&bs, &tmp)) {
            set_err(err, errcap, "invalid HEVC SPS");
            return 0;
        }
        if (!hevc_bs_read_bit(&bs, &tmp)) {
            set_err(err, errcap, "invalid HEVC SPS");
            return 0;
        }
    }

    uint32_t num_short_term_ref_pic_sets = 0;
    if (!hevc_bs_read_ue(&bs, &num_short_term_ref_pic_sets)) {
        set_err(err, errcap, "invalid HEVC SPS");
        return 0;
    }
    if (num_short_term_ref_pic_sets > HEVC_MAX_SHORT_TERM_RPS) {
        set_err(err, errcap, "unsupported HEVC RPS count");
        return 0;
    }
    uint8_t num_delta_pocs[HEVC_MAX_SHORT_TERM_RPS];
    memset(num_delta_pocs, 0, sizeof(num_delta_pocs));
    for (uint32_t i = 0; i < num_short_term_ref_pic_sets; i++) {
        if (!hevc_skip_short_term_ref_pic_set(&bs, (int)i, (int)num_short_term_ref_pic_sets,
                                              num_delta_pocs)) {
            set_err(err, errcap, "invalid HEVC SPS");
            return 0;
        }
    }

    uint32_t long_term_ref_pics_present_flag = 0;
    if (!hevc_bs_read_bit(&bs, &long_term_ref_pics_present_flag)) {
        set_err(err, errcap, "invalid HEVC SPS");
        return 0;
    }
    if (long_term_ref_pics_present_flag) {
        uint32_t num_long_term_ref_pics_sps = 0;
        if (!hevc_bs_read_ue(&bs, &num_long_term_ref_pics_sps)) {
            set_err(err, errcap, "invalid HEVC SPS");
            return 0;
        }
        int lsb_bits = (int)log2_max_pic_order_cnt_lsb_minus4 + 4;
        for (uint32_t i = 0; i < num_long_term_ref_pics_sps; i++) {
            if (!hevc_bs_read_bits(&bs, lsb_bits, &tmp) ||
                !hevc_bs_read_bit(&bs, &tmp)) {
                set_err(err, errcap, "invalid HEVC SPS");
                return 0;
            }
        }
    }

    uint32_t sps_temporal_mvp_enabled_flag = 0;
    if (!hevc_bs_read_bit(&bs, &sps_temporal_mvp_enabled_flag)) {
        set_err(err, errcap, "invalid HEVC SPS");
        return 0;
    }
    uint32_t strong_intra_smoothing_enabled_flag = 0;
    if (!hevc_bs_read_bit(&bs, &strong_intra_smoothing_enabled_flag)) {
        set_err(err, errcap, "invalid HEVC SPS");
        return 0;
    }
    (void)strong_intra_smoothing_enabled_flag;
    uint32_t vui_parameters_present_flag = 0;
    uint8_t vui_video_full_range_flag = 1;
    uint8_t vui_colour_primaries = 2;
    uint8_t vui_transfer_characteristics = 2;
    uint8_t vui_matrix_coefficients = 5;
    if (!hevc_bs_read_bit(&bs, &vui_parameters_present_flag)) {
        set_err(err, errcap, "invalid HEVC SPS");
        return 0;
    }
    if (vui_parameters_present_flag) {
        if (!hevc_parse_vui(&bs, max_sub_layers_minus1,
                            &vui_video_full_range_flag,
                            &vui_colour_primaries,
                            &vui_transfer_characteristics,
                            &vui_matrix_coefficients)) {
            set_err(err, errcap, "invalid HEVC SPS VUI");
            return 0;
        }
    }

    hevc_sps *sps = &ctx->sps[sps_id];
    memset(sps, 0, sizeof(*sps));
    sps->valid = 1;
    sps->width = width;
    sps->height = height;
    sps->chroma_format_idc = (uint8_t)chroma_format_idc;
    sps->separate_colour_plane_flag = (uint8_t)separate_colour_plane_flag;
    sps->bit_depth_luma = (uint8_t)(bit_depth_luma_minus8 + 8);
    sps->bit_depth_chroma = (uint8_t)(bit_depth_chroma_minus8 + 8);
    sps->log2_min_luma_coding_block_size_minus3 = (uint8_t)log2_min_luma_coding_block_size_minus3;
    sps->log2_diff_max_min_luma_coding_block_size = (uint8_t)log2_diff_max_min_luma_coding_block_size;
    sps->log2_max_pic_order_cnt_lsb_minus4 = (uint8_t)log2_max_pic_order_cnt_lsb_minus4;
    sps->log2_min_luma_transform_block_size_minus2 = (uint8_t)log2_min_luma_transform_block_size_minus2;
    sps->log2_diff_max_min_luma_transform_block_size =
        (uint8_t)log2_diff_max_min_luma_transform_block_size;
    sps->max_transform_hierarchy_depth_intra = (uint8_t)max_transform_hierarchy_depth_intra;
    sps->sample_adaptive_offset_enabled_flag = (uint8_t)sample_adaptive_offset_enabled_flag;
    sps->sps_temporal_mvp_enabled_flag = (uint8_t)sps_temporal_mvp_enabled_flag;
    sps->long_term_ref_pics_present_flag = (uint8_t)long_term_ref_pics_present_flag;
    sps->num_short_term_ref_pic_sets = (uint8_t)num_short_term_ref_pic_sets;
    memset(sps->num_delta_pocs, 0, sizeof(sps->num_delta_pocs));
    for (uint32_t i = 0; i < num_short_term_ref_pic_sets; i++) {
        sps->num_delta_pocs[i] = num_delta_pocs[i];
    }
    sps->vui_present_flag = (uint8_t)vui_parameters_present_flag;
    sps->vui_video_full_range_flag = vui_video_full_range_flag;
    sps->vui_colour_primaries = vui_colour_primaries;
    sps->vui_transfer_characteristics = vui_transfer_characteristics;
    sps->vui_matrix_coefficients = vui_matrix_coefficients;
    heif_debugf("VUI: full_range=%u primaries=%u transfer=%u matrix=%u\n",
               vui_video_full_range_flag, vui_colour_primaries,
               vui_transfer_characteristics, vui_matrix_coefficients);

    uint32_t sub_width_c = 1;
    uint32_t sub_height_c = 1;
    if (chroma_format_idc == 1) {
        sub_width_c = 2;
        sub_height_c = 2;
    } else if (chroma_format_idc == 2) {
        sub_width_c = 2;
        sub_height_c = 1;
    } else if (chroma_format_idc == 3) {
        sub_width_c = 1;
        sub_height_c = 1;
    }
    if (chroma_format_idc == 0) {
        sub_width_c = 1;
        sub_height_c = 1;
    }
    uint64_t disp_w = width;
    uint64_t disp_h = height;
    if (conformance_window_flag) {
        uint64_t crop_x = (uint64_t)(conf_left + conf_right) * sub_width_c;
        uint64_t crop_y = (uint64_t)(conf_top + conf_bottom) * sub_height_c;
        if (crop_x <= disp_w) {
            disp_w -= crop_x;
        }
        if (crop_y <= disp_h) {
            disp_h -= crop_y;
        }
    }
    sps->display_width = (uint32_t)disp_w;
    sps->display_height = (uint32_t)disp_h;
    uint32_t log2_ctu = log2_min_luma_coding_block_size_minus3 +
                        log2_diff_max_min_luma_coding_block_size + 3u;
    if (log2_ctu < 3 || log2_ctu > 6) {
        set_err(err, errcap, "unsupported HEVC CTU size");
        return 0;
    }
    sps->ctu_size = 1u << log2_ctu;
    sps->pic_width_in_ctus = (sps->width + sps->ctu_size - 1u) / sps->ctu_size;
    sps->pic_height_in_ctus = (sps->height + sps->ctu_size - 1u) / sps->ctu_size;
    ctx->have_sps = 1;
    ctx->last_sps_id = sps_id;
    return 1;
}

static int hevc_parse_pps(const uint8_t *rbsp, size_t rbsp_size, hevc_context *ctx,
                          char *err, size_t errcap) {
    hevc_bitstream bs;
    memset(&bs, 0, sizeof(bs));
    bs.data = rbsp;
    bs.size = rbsp_size;
    uint16_t *tile_col_width_minus1 = NULL;
    uint16_t *tile_row_height_minus1 = NULL;
    uint32_t num_tile_columns = 0;
    uint32_t num_tile_rows = 0;
    uint32_t uniform_spacing_flag = 0;
    int ok = 0;
    uint32_t pps_id = 0;
    uint32_t sps_id = 0;
    if (!hevc_bs_read_ue(&bs, &pps_id) || !hevc_bs_read_ue(&bs, &sps_id)) {
        set_err(err, errcap, "invalid HEVC PPS");
        goto cleanup;
    }
    uint32_t dependent_slice_segments_enabled_flag = 0;
    uint32_t output_flag_present_flag = 0;
    uint32_t num_extra_slice_header_bits = 0;
    uint32_t sign_data_hiding_enabled_flag = 0;
    uint32_t cabac_init_present_flag = 0;
    if (!hevc_bs_read_bit(&bs, &dependent_slice_segments_enabled_flag) ||
        !hevc_bs_read_bit(&bs, &output_flag_present_flag) ||
        !hevc_bs_read_bits(&bs, 3, &num_extra_slice_header_bits) ||
        !hevc_bs_read_bit(&bs, &sign_data_hiding_enabled_flag) ||
        !hevc_bs_read_bit(&bs, &cabac_init_present_flag)) {
        set_err(err, errcap, "invalid HEVC PPS");
        goto cleanup;
    }
    (void)sign_data_hiding_enabled_flag;
    uint32_t tmp_ue = 0;
    if (!hevc_bs_read_ue(&bs, &tmp_ue) || !hevc_bs_read_ue(&bs, &tmp_ue)) {
        set_err(err, errcap, "invalid HEVC PPS");
        goto cleanup;
    }
    int32_t init_qp_minus26 = 0;
    if (!hevc_bs_read_se(&bs, &init_qp_minus26)) {
        set_err(err, errcap, "invalid HEVC PPS");
        goto cleanup;
    }
    uint32_t constrained_intra_pred_flag = 0;
    uint32_t transform_skip_enabled_flag = 0;
    if (!hevc_bs_read_bit(&bs, &constrained_intra_pred_flag) ||
        !hevc_bs_read_bit(&bs, &transform_skip_enabled_flag)) {
        set_err(err, errcap, "invalid HEVC PPS");
        goto cleanup;
    }
    (void)constrained_intra_pred_flag;
    uint32_t cu_qp_delta_enabled_flag = 0;
    if (!hevc_bs_read_bit(&bs, &cu_qp_delta_enabled_flag)) {
        set_err(err, errcap, "invalid HEVC PPS");
        goto cleanup;
    }
    uint32_t diff_cu_qp_delta_depth = 0;
    if (cu_qp_delta_enabled_flag) {
        if (!hevc_bs_read_ue(&bs, &diff_cu_qp_delta_depth)) {
            set_err(err, errcap, "invalid HEVC PPS");
            goto cleanup;
        }
    }
    int32_t pps_cb_qp_offset = 0;
    int32_t pps_cr_qp_offset = 0;
    if (!hevc_bs_read_se(&bs, &pps_cb_qp_offset) ||
        !hevc_bs_read_se(&bs, &pps_cr_qp_offset)) {
        set_err(err, errcap, "invalid HEVC PPS");
        goto cleanup;
    }
    uint32_t pps_slice_chroma_qp_offsets_present_flag = 0;
    if (!hevc_bs_read_bit(&bs, &pps_slice_chroma_qp_offsets_present_flag)) {
        set_err(err, errcap, "invalid HEVC PPS");
        goto cleanup;
    }
    uint32_t weighted_pred_flag = 0;
    uint32_t weighted_bipred_flag = 0;
    uint32_t transquant_bypass_enabled_flag = 0;
    if (!hevc_bs_read_bit(&bs, &weighted_pred_flag) ||
        !hevc_bs_read_bit(&bs, &weighted_bipred_flag) ||
        !hevc_bs_read_bit(&bs, &transquant_bypass_enabled_flag)) {
        set_err(err, errcap, "invalid HEVC PPS");
        goto cleanup;
    }
    (void)weighted_pred_flag;
    (void)weighted_bipred_flag;
    uint32_t tiles_enabled_flag = 0;
    uint32_t entropy_coding_sync_enabled_flag = 0;
    if (!hevc_bs_read_bit(&bs, &tiles_enabled_flag) ||
        !hevc_bs_read_bit(&bs, &entropy_coding_sync_enabled_flag)) {
        set_err(err, errcap, "invalid HEVC PPS");
        goto cleanup;
    }
    if (tiles_enabled_flag) {
        uint32_t num_tile_columns_minus1 = 0;
        uint32_t num_tile_rows_minus1 = 0;
        if (!hevc_bs_read_ue(&bs, &num_tile_columns_minus1) ||
            !hevc_bs_read_ue(&bs, &num_tile_rows_minus1) ||
            !hevc_bs_read_bit(&bs, &uniform_spacing_flag)) {
            set_err(err, errcap, "invalid HEVC PPS");
            goto cleanup;
        }
        num_tile_columns = num_tile_columns_minus1 + 1u;
        num_tile_rows = num_tile_rows_minus1 + 1u;
        if (num_tile_columns == 0 || num_tile_rows == 0 ||
            num_tile_columns > UINT16_MAX || num_tile_rows > UINT16_MAX) {
            set_err(err, errcap, "invalid HEVC PPS tile layout");
            goto cleanup;
        }
        if (!uniform_spacing_flag) {
            if (num_tile_columns_minus1) {
                tile_col_width_minus1 =
                    (uint16_t *)calloc(num_tile_columns_minus1, sizeof(uint16_t));
                if (!tile_col_width_minus1) {
                    set_err(err, errcap, "out of memory");
                    goto cleanup;
                }
            }
            if (num_tile_rows_minus1) {
                tile_row_height_minus1 =
                    (uint16_t *)calloc(num_tile_rows_minus1, sizeof(uint16_t));
                if (!tile_row_height_minus1) {
                    set_err(err, errcap, "out of memory");
                    goto cleanup;
                }
            }
            for (uint32_t i = 0; i < num_tile_columns_minus1; i++) {
                if (!hevc_bs_read_ue(&bs, &tmp_ue) || tmp_ue > UINT16_MAX) {
                    set_err(err, errcap, "invalid HEVC PPS");
                    goto cleanup;
                }
                tile_col_width_minus1[i] = (uint16_t)tmp_ue;
            }
            for (uint32_t i = 0; i < num_tile_rows_minus1; i++) {
                if (!hevc_bs_read_ue(&bs, &tmp_ue) || tmp_ue > UINT16_MAX) {
                    set_err(err, errcap, "invalid HEVC PPS");
                    goto cleanup;
                }
                tile_row_height_minus1[i] = (uint16_t)tmp_ue;
            }
        }
        if (!hevc_bs_read_bit(&bs, &tmp_ue)) {
            set_err(err, errcap, "invalid HEVC PPS");
            goto cleanup;
        }
    }
    uint32_t pps_loop_filter_across_slices_enabled_flag = 0;
    if (!hevc_bs_read_bit(&bs, &pps_loop_filter_across_slices_enabled_flag)) {
        set_err(err, errcap, "invalid HEVC PPS");
        goto cleanup;
    }
    uint32_t deblocking_filter_control_present_flag = 0;
    uint32_t deblocking_filter_override_enabled_flag = 0;
    uint32_t pps_deblocking_filter_disabled_flag = 0;
    if (!hevc_bs_read_bit(&bs, &deblocking_filter_control_present_flag)) {
        set_err(err, errcap, "invalid HEVC PPS");
        goto cleanup;
    }
    if (deblocking_filter_control_present_flag) {
        if (!hevc_bs_read_bit(&bs, &deblocking_filter_override_enabled_flag) ||
            !hevc_bs_read_bit(&bs, &pps_deblocking_filter_disabled_flag)) {
            set_err(err, errcap, "invalid HEVC PPS");
            goto cleanup;
        }
        if (!pps_deblocking_filter_disabled_flag) {
            int32_t tmp_se = 0;
            if (!hevc_bs_read_se(&bs, &tmp_se) ||
                !hevc_bs_read_se(&bs, &tmp_se)) {
                set_err(err, errcap, "invalid HEVC PPS");
                goto cleanup;
            }
        }
    }
    uint32_t pps_scaling_list_data_present_flag = 0;
    if (!hevc_bs_read_bit(&bs, &pps_scaling_list_data_present_flag)) {
        set_err(err, errcap, "invalid HEVC PPS");
        goto cleanup;
    }
    if (pps_scaling_list_data_present_flag) {
        set_err(err, errcap, "unsupported HEVC scaling list");
        goto cleanup;
    }
    if (!hevc_bs_read_bit(&bs, &tmp_ue) ||
        !hevc_bs_read_ue(&bs, &tmp_ue)) {
        set_err(err, errcap, "invalid HEVC PPS");
        goto cleanup;
    }
    uint32_t slice_segment_header_extension_present_flag = 0;
    if (!hevc_bs_read_bit(&bs, &slice_segment_header_extension_present_flag)) {
        set_err(err, errcap, "invalid HEVC PPS");
        goto cleanup;
    }
    if (slice_segment_header_extension_present_flag) {
        set_err(err, errcap, "unsupported HEVC PPS extension");
        goto cleanup;
    }
    if (pps_id >= HEVC_MAX_PPS || sps_id >= HEVC_MAX_SPS) {
        set_err(err, errcap, "HEVC PPS id out of range");
        goto cleanup;
    }
    hevc_pps *pps = &ctx->pps[pps_id];
    hevc_pps_clear_tiles(pps);
    pps->valid = 1;
    pps->pps_id = pps_id;
    pps->sps_id = sps_id;
    pps->dependent_slice_segments_enabled_flag = (uint8_t)dependent_slice_segments_enabled_flag;
    pps->output_flag_present_flag = (uint8_t)output_flag_present_flag;
    pps->num_extra_slice_header_bits = (uint8_t)(num_extra_slice_header_bits & 7u);
    pps->sign_data_hiding_enabled_flag = (uint8_t)sign_data_hiding_enabled_flag;
    pps->transform_skip_enabled_flag = (uint8_t)transform_skip_enabled_flag;
    pps->tiles_enabled_flag = (uint8_t)tiles_enabled_flag;
    pps->entropy_coding_sync_enabled_flag = (uint8_t)entropy_coding_sync_enabled_flag;
    pps->cabac_init_present_flag = (uint8_t)cabac_init_present_flag;
    pps->pps_slice_chroma_qp_offsets_present_flag =
        (uint8_t)pps_slice_chroma_qp_offsets_present_flag;
    pps->transquant_bypass_enabled_flag = (uint8_t)transquant_bypass_enabled_flag;
    pps->cu_qp_delta_enabled_flag = (uint8_t)cu_qp_delta_enabled_flag;
    pps->diff_cu_qp_delta_depth = (uint8_t)diff_cu_qp_delta_depth;
    pps->pps_cb_qp_offset = (int8_t)pps_cb_qp_offset;
    pps->pps_cr_qp_offset = (int8_t)pps_cr_qp_offset;
    pps->deblocking_filter_control_present_flag =
        (uint8_t)deblocking_filter_control_present_flag;
    pps->deblocking_filter_override_enabled_flag =
        (uint8_t)deblocking_filter_override_enabled_flag;
    pps->pps_deblocking_filter_disabled_flag =
        (uint8_t)pps_deblocking_filter_disabled_flag;
    pps->pps_loop_filter_across_slices_enabled_flag =
        (uint8_t)pps_loop_filter_across_slices_enabled_flag;
    pps->slice_segment_header_extension_present_flag =
        (uint8_t)slice_segment_header_extension_present_flag;
    pps->init_qp_minus26 = (int8_t)init_qp_minus26;
    pps->num_tile_columns = (uint16_t)num_tile_columns;
    pps->num_tile_rows = (uint16_t)num_tile_rows;
    pps->uniform_spacing_flag = (uint8_t)uniform_spacing_flag;
    pps->tile_col_width_minus1 = tile_col_width_minus1;
    pps->tile_row_height_minus1 = tile_row_height_minus1;
    tile_col_width_minus1 = NULL;
    tile_row_height_minus1 = NULL;
    ctx->have_pps = 1;
    ok = 1;
cleanup:
    if (!ok) {
        free(tile_col_width_minus1);
        free(tile_row_height_minus1);
    }
    return ok;
}

static int hevc_nal_to_rbsp(const uint8_t *nal, size_t nal_size,
                            uint8_t **rbsp_out, size_t *rbsp_size,
                            size_t **ebsp_to_rbsp_out, size_t *ebsp_size_out);

static int hevc_parse_hvcc(const uint8_t *data, size_t size, hevc_context *ctx,
                           char *err, size_t errcap) {
    if (size < 23) {
        set_err(err, errcap, "invalid HEVC hvcC");
        return 0;
    }
    if (data[0] != 1) {
        set_err(err, errcap, "unsupported HEVC hvcC version");
        return 0;
    }
    int length_size = (data[21] & 0x03) + 1;
    if (length_size < 1 || length_size > 4) {
        set_err(err, errcap, "invalid HEVC hvcC length size");
        return 0;
    }
    ctx->length_size = length_size;
    uint8_t num_arrays = data[22];
    size_t pos = 23;
    for (uint8_t i = 0; i < num_arrays; i++) {
        if (pos + 3 > size) {
            set_err(err, errcap, "truncated HEVC hvcC");
            return 0;
        }
        uint8_t nal_type = data[pos] & 0x3F;
        uint16_t num_nalus = read_be16(data + pos + 1);
        pos += 3;
        for (uint16_t j = 0; j < num_nalus; j++) {
            if (pos + 2 > size) {
                set_err(err, errcap, "truncated HEVC hvcC");
                return 0;
            }
            uint16_t nal_len = read_be16(data + pos);
            pos += 2;
            if (pos + nal_len > size) {
                set_err(err, errcap, "truncated HEVC hvcC");
                return 0;
            }
            const uint8_t *nal = data + pos;
            size_t nal_size = nal_len;
            pos += nal_len;
            if (nal_type == 33 || nal_type == 34 || nal_type == 32) {
                uint8_t *rbsp = NULL;
                size_t rbsp_size = 0;
                if (!hevc_nal_to_rbsp(nal, nal_size, &rbsp, &rbsp_size, NULL, NULL)) {
                    set_err(err, errcap, "out of memory");
                    return 0;
                }
                int ok = 1;
                if (nal_type == 33) {
                    ok = hevc_parse_sps(rbsp, rbsp_size, ctx, err, errcap);
                } else if (nal_type == 34) {
                    ok = hevc_parse_pps(rbsp, rbsp_size, ctx, err, errcap);
                }
                free(rbsp);
                if (!ok) {
                    return 0;
                }
            }
        }
    }
    heif_debugf("hevc: hvcc parsed: have_sps=%d have_pps=%d length_size=%d\n",
                ctx->have_sps, ctx->have_pps, ctx->length_size);
    return 1;
}

static uint32_t hevc_ceil_log2_u64(uint64_t v) {
    if (v <= 1) {
        return 0;
    }
    uint32_t bits = 0;
    uint64_t x = v - 1;
    while (x) {
        bits++;
        x >>= 1;
    }
    return bits;
}

static void hevc_traverse_tus(uint32_t x, uint32_t y, uint32_t size) {
    (void)x;
    (void)y;
    (void)size;
}

static uint8_t hevc_clip_u8(int v) {
    if (v < 0) {
        return 0;
    }
    if (v > 255) {
        return 255;
    }
    return (uint8_t)v;
}

static int hevc_clip_bd(int v, int bit_depth);

static void hevc_intra_pred_planar(uint16_t *dst, int stride, const uint16_t *ref,
                                   int size, int bit_depth) {
    uint16_t top_last = ref[size];
    uint16_t left_last = ref[2 * size];
    int log2 = 0;
    int tmp = size;
    while (tmp > 1) {
        log2++;
        tmp >>= 1;
    }
    for (int y = 0; y < size; y++) {
        uint16_t left_y = ref[size + 1 + y];
        for (int x = 0; x < size; x++) {
            uint16_t top_x = ref[1 + x];
            int val = (size - 1 - x) * (int)left_y + (x + 1) * (int)top_last +
                      (size - 1 - y) * (int)top_x + (y + 1) * (int)left_last;
            val = (val + size) >> (log2 + 1);
            dst[y * stride + x] = (uint16_t)hevc_clip_bd(val, bit_depth);
        }
    }
}

static void hevc_intra_pred_dc(uint16_t *dst, int stride, const uint16_t *ref,
                               int size, int bit_depth) {
    int sum = 0;
    for (int i = 0; i < size; i++) {
        sum += ref[1 + i];
        sum += ref[size + 1 + i];
    }
    int val = (sum + size) / (2 * size);
    val = hevc_clip_bd(val, bit_depth);
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            dst[y * stride + x] = (uint16_t)val;
        }
    }
}

static void hevc_intra_pred_angular(uint16_t *dst, int stride, const uint16_t *ref,
                                    int size, int mode, int bit_depth) {
    static const int angle_table[35] = {
        0, 0,
        32, 26, 21, 17, 13, 9, 5, 2, 0,
        -2, -5, -9, -13, -17, -21, -26, -32,
        -26, -21, -17, -13, -9, -5, -2, 0,
        2, 5, 9, 13, 17, 21, 26, 32
    };
    int angle = angle_table[mode];
    int vertical_mode = (mode >= 18);
    uint16_t ref_main[128];
    int ref_count = 2 * size + 1;
    if (ref_count > (int)sizeof(ref_main)) {
        ref_count = (int)sizeof(ref_main);
    }
    for (int i = 0; i < ref_count; i++) {
        if (i <= size) {
            ref_main[i] = ref[i];
        } else {
            ref_main[i] = ref[vertical_mode ? size : 2 * size];
        }
    }
    if (angle == 0) {
        if (vertical_mode) {
            for (int y = 0; y < size; y++) {
                for (int x = 0; x < size; x++) {
                    dst[y * stride + x] = ref[1 + x];
                }
            }
        } else {
            for (int y = 0; y < size; y++) {
                for (int x = 0; x < size; x++) {
                    dst[y * stride + x] = ref[size + 1 + y];
                }
            }
        }
        return;
    }
    for (int y = 0; y < size; y++) {
        int pos = (y + 1) * angle;
        int idx = pos >> 5;
        int fract = pos & 31;
        for (int x = 0; x < size; x++) {
            int ref_idx = x + idx + 1;
            if (ref_idx < 0) {
                ref_idx = 0;
            }
            if (ref_idx + 1 >= ref_count) {
                ref_idx = ref_count - 2;
            }
            int a = (int)ref_main[ref_idx];
            int b = (int)ref_main[ref_idx + 1];
            int val = ((32 - fract) * a + fract * b + 16) >> 5;
            if (vertical_mode) {
                dst[y * stride + x] = (uint16_t)hevc_clip_bd(val, bit_depth);
            } else {
                dst[x * stride + y] = (uint16_t)hevc_clip_bd(val, bit_depth);
            }
        }
    }
}

static void hevc_intra_predict_block(uint16_t *dst, int stride,
                                     const uint16_t *ref, int size,
                                     int mode, int bit_depth) {
    if (mode == 0) {
        hevc_intra_pred_planar(dst, stride, ref, size, bit_depth);
        return;
    }
    if (mode == 1) {
        hevc_intra_pred_dc(dst, stride, ref, size, bit_depth);
        return;
    }
    if (mode < 2 || mode > 34) {
        hevc_intra_pred_dc(dst, stride, ref, size, bit_depth);
        return;
    }
    hevc_intra_pred_angular(dst, stride, ref, size, mode, bit_depth);
}

static const int16_t hevc_dct4[4][4] = {
    {64, 64, 64, 64},
    {83, 36, -36, -83},
    {64, -64, -64, 64},
    {36, -83, 83, -36}
};

static const int16_t hevc_dst4[4][4] = {
    {29, 55, 74, 84},
    {74, 74, 0, -74},
    {84, -29, -74, 55},
    {55, -84, 74, -29}
};

static const int16_t hevc_dct8[8][8] = {
    {64, 64, 64, 64, 64, 64, 64, 64},
    {89, 75, 50, 18, -18, -50, -75, -89},
    {83, 36, -36, -83, -83, -36, 36, 83},
    {75, -18, -89, -50, 50, 89, 18, -75},
    {64, -64, -64, 64, 64, -64, -64, 64},
    {50, -89, 18, 75, -75, -18, 89, -50},
    {36, -83, 83, -36, -36, 83, -83, 36},
    {18, -50, 75, -89, 89, -75, 50, -18}
};

static const int16_t hevc_dct16[16][16] = {
    {64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64},
    {90, 87, 80, 70, 57, 43, 25, 9, -9, -25, -43, -57, -70, -80, -87, -90},
    {89, 75, 50, 18, -18, -50, -75, -89, -89, -75, -50, -18, 18, 50, 75, 89},
    {87, 57, 9, -43, -80, -90, -70, -25, 25, 70, 90, 80, 43, -9, -57, -87},
    {83, 36, -36, -83, -83, -36, 36, 83, 83, 36, -36, -83, -83, -36, 36, 83},
    {80, 9, -70, -87, -25, 57, 90, 43, -43, -90, -57, 25, 87, 70, -9, -80},
    {75, -18, -89, -50, 50, 89, 18, -75, -75, 18, 89, 50, -50, -89, -18, 75},
    {70, -43, -87, 9, 90, 25, -80, -57, 57, 80, -25, -90, -9, 87, 43, -70},
    {64, -64, -64, 64, 64, -64, -64, 64, 64, -64, -64, 64, 64, -64, -64, 64},
    {57, -80, -25, 90, -9, -87, 43, 70, -70, -43, 87, 9, -90, 25, 80, -57},
    {50, -89, 18, 75, -75, -18, 89, -50, -50, 89, -18, -75, 75, 18, -89, 50},
    {43, -90, 57, 25, -87, 70, 9, -80, 80, -9, -70, 87, -25, -57, 90, -43},
    {36, -83, 83, -36, -36, 83, -83, 36, 36, -83, 83, -36, -36, 83, -83, 36},
    {25, -70, 90, -80, 43, 9, -57, 87, -87, 57, -9, -43, 80, -90, 70, -25},
    {18, -50, 75, -89, 89, -75, 50, -18, -18, 50, -75, 89, -89, 75, -50, 18},
    {9, -25, 43, -57, 70, -80, 87, -90, 90, -87, 80, -70, 57, -43, 25, -9}
};

static const int16_t hevc_dct32[32][32] = {
    {64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64},
    {90, 90, 88, 85, 82, 78, 73, 67, 61, 54, 46, 38, 31, 22, 13, 4, -4, -13, -22, -31, -38, -46, -54, -61, -67, -73, -78, -82, -85, -88, -90, -90},
    {90, 87, 80, 70, 57, 43, 25, 9, -9, -25, -43, -57, -70, -80, -87, -90, -90, -87, -80, -70, -57, -43, -25, -9, 9, 25, 43, 57, 70, 80, 87, 90},
    {90, 82, 67, 46, 22, -4, -31, -54, -73, -85, -90, -88, -78, -61, -38, -13, 13, 38, 61, 78, 88, 90, 85, 73, 54, 31, 4, -22, -46, -67, -82, -90},
    {89, 75, 50, 18, -18, -50, -75, -89, -89, -75, -50, -18, 18, 50, 75, 89, 89, 75, 50, 18, -18, -50, -75, -89, -89, -75, -50, -18, 18, 50, 75, 89},
    {88, 67, 31, -13, -54, -82, -90, -78, -46, -4, 38, 73, 90, 85, 61, 22, -22, -61, -85, -90, -73, -38, 4, 46, 78, 90, 82, 54, 13, -31, -67, -88},
    {87, 57, 9, -43, -80, -90, -70, -25, 25, 70, 90, 80, 43, -9, -57, -87, -87, -57, -9, 43, 80, 90, 70, 25, -25, -70, -90, -80, -43, 9, 57, 87},
    {85, 46, -13, -67, -90, -73, -22, 38, 82, 88, 54, -4, -61, -90, -78, -31, 31, 78, 90, 61, 4, -54, -88, -82, -38, 22, 73, 90, 67, 13, -46, -85},
    {83, 36, -36, -83, -83, -36, 36, 83, 83, 36, -36, -83, -83, -36, 36, 83, 83, 36, -36, -83, -83, -36, 36, 83, 83, 36, -36, -83, -83, -36, 36, 83},
    {82, 22, -54, -90, -61, 13, 78, 85, 31, -46, -90, -67, 4, 73, 88, 38, -38, -88, -73, -4, 67, 90, 46, -31, -85, -78, -13, 61, 90, 54, -22, -82},
    {80, 9, -70, -87, -25, 57, 90, 43, -43, -90, -57, 25, 87, 70, -9, -80, -80, -9, 70, 87, 25, -57, -90, -43, 43, 90, 57, -25, -87, -70, 9, 80},
    {78, -4, -82, -73, 13, 85, 67, -22, -88, -61, 31, 90, 54, -38, -90, -46, 46, 90, 38, -54, -90, -31, 61, 88, 22, -67, -85, -13, 73, 82, 4, -78},
    {75, -18, -89, -50, 50, 89, 18, -75, -75, 18, 89, 50, -50, -89, -18, 75, 75, -18, -89, -50, 50, 89, 18, -75, -75, 18, 89, 50, -50, -89, -18, 75},
    {73, -31, -90, -22, 78, 67, -38, -90, -13, 82, 61, -46, -88, -4, 85, 54, -54, -85, 4, 88, 46, -61, -82, 13, 90, 38, -67, -78, 22, 90, 31, -73},
    {70, -43, -87, 9, 90, 25, -80, -57, 57, 80, -25, -90, -9, 87, 43, -70, -70, 43, 87, -9, -90, -25, 80, 57, -57, -80, 25, 90, 9, -87, -43, 70},
    {67, -54, -78, 38, 85, -22, -90, 4, 90, 13, -88, -31, 82, 46, -73, -61, 61, 73, -46, -82, 31, 88, -13, -90, -4, 90, 22, -85, -38, 78, 54, -67},
    {64, -64, -64, 64, 64, -64, -64, 64, 64, -64, -64, 64, 64, -64, -64, 64, 64, -64, -64, 64, 64, -64, -64, 64, 64, -64, -64, 64, 64, -64, -64, 64},
    {61, -73, -46, 82, 31, -88, -13, 90, -4, -90, 22, 85, -38, -78, 54, 67, -67, -54, 78, 38, -85, -22, 90, 4, -90, 13, 88, -31, -82, 46, 73, -61},
    {57, -80, -25, 90, -9, -87, 43, 70, -70, -43, 87, 9, -90, 25, 80, -57, -57, 80, 25, -90, 9, 87, -43, -70, 70, 43, -87, -9, 90, -25, -80, 57},
    {54, -85, -4, 88, -46, -61, 82, 13, -90, 38, 67, -78, -22, 90, -31, -73, 73, 31, -90, 22, 78, -67, -38, 90, -13, -82, 61, 46, -88, 4, 85, -54},
    {50, -89, 18, 75, -75, -18, 89, -50, -50, 89, -18, -75, 75, 18, -89, 50, 50, -89, 18, 75, -75, -18, 89, -50, -50, 89, -18, -75, 75, 18, -89, 50},
    {46, -90, 38, 54, -90, 31, 61, -88, 22, 67, -85, 13, 73, -82, 4, 78, -78, -4, 82, -73, -13, 85, -67, -22, 88, -61, -31, 90, -54, -38, 90, -46},
    {43, -90, 57, 25, -87, 70, 9, -80, 80, -9, -70, 87, -25, -57, 90, -43, -43, 90, -57, -25, 87, -70, -9, 80, -80, 9, 70, -87, 25, 57, -90, 43},
    {38, -88, 73, -4, -67, 90, -46, -31, 85, -78, 13, 61, -90, 54, 22, -82, 82, -22, -54, 90, -61, -13, 78, -85, 31, 46, -90, 67, 4, -73, 88, -38},
    {36, -83, 83, -36, -36, 83, -83, 36, 36, -83, 83, -36, -36, 83, -83, 36, 36, -83, 83, -36, -36, 83, -83, 36, 36, -83, 83, -36, -36, 83, -83, 36},
    {31, -78, 90, -61, 4, 54, -88, 82, -38, -22, 73, -90, 67, -13, -46, 85, -85, 46, 13, -67, 90, -73, 22, 38, -82, 88, -54, -4, 61, -90, 78, -31},
    {25, -70, 90, -80, 43, 9, -57, 87, -87, 57, -9, -43, 80, -90, 70, -25, -25, 70, -90, 80, -43, -9, 57, -87, 87, -57, 9, 43, -80, 90, -70, 25},
    {22, -61, 85, -90, 73, -38, -4, 46, -78, 90, -82, 54, -13, -31, 67, -88, 88, -67, 31, 13, -54, 82, -90, 78, -46, 4, 38, -73, 90, -85, 61, -22},
    {18, -50, 75, -89, 89, -75, 50, -18, -18, 50, -75, 89, -89, 75, -50, 18, 18, -50, 75, -89, 89, -75, 50, -18, -18, 50, -75, 89, -89, 75, -50, 18},
    {13, -38, 61, -78, 88, -90, 85, -73, 54, -31, 4, 22, -46, 67, -82, 90, -90, 82, -67, 46, -22, -4, 31, -54, 73, -85, 90, -88, 78, -61, 38, -13},
    {9, -25, 43, -57, 70, -80, 87, -90, 90, -87, 80, -70, 57, -43, 25, -9, -9, 25, -43, 57, -70, 80, -87, 90, -90, 87, -80, 70, -57, 43, -25, 9},
    {4, -13, 22, -31, 38, -46, 54, -61, 67, -73, 78, -82, 85, -88, 90, -90, 90, -90, 88, -85, 82, -78, 73, -67, 61, -54, 46, -38, 31, -22, 13, -4}
};

static int32_t hevc_round_shift(int64_t v, int shift) {
    if (shift <= 0) {
        if (v > INT32_MAX) return INT32_MAX;
        if (v < INT32_MIN) return INT32_MIN;
        return (int32_t)v;
    }
    int64_t add = 1ll << (shift - 1);
    if (v >= 0) {
        v += add;
    } else {
        v -= add;
    }
    v >>= shift;
    if (v > INT32_MAX) v = INT32_MAX;
    if (v < INT32_MIN) v = INT32_MIN;
    return (int32_t)v;
}

static void hevc_inverse_transform_1d(const int32_t *src, int32_t *dst,
                                      const int16_t *mat, int size, int shift) {
    for (int i = 0; i < size; i++) {
        int64_t sum = 0;
        const int16_t *row = mat + i * size;
        for (int k = 0; k < size; k++) {
            sum += (int64_t)row[k] * src[k];
        }
        dst[i] = hevc_round_shift(sum, shift);
    }
}

static void hevc_inverse_transform_block(const int32_t *coeffs, uint16_t *pred,
                                         uint16_t *dst,
                                         int stride, int size, int bit_depth,
                                         int use_dst) {
    if (size != 4 && size != 8 && size != 16 && size != 32) {
        return;
    }
    const int16_t *mat = NULL;
    if (size == 4) {
        mat = use_dst ? &hevc_dst4[0][0] : &hevc_dct4[0][0];
    } else if (size == 8) {
        mat = &hevc_dct8[0][0];
    } else if (size == 16) {
        mat = &hevc_dct16[0][0];
    } else {
        mat = &hevc_dct32[0][0];
    }
    /* Check if all coefficients are zero - fast path */
    int has_nonzero = 0;
    for (int i = 0; i < size * size; i++) {
        if (coeffs[i] != 0) {
            has_nonzero = 1;
            break;
        }
    }
    if (!has_nonzero) {
        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                dst[y * stride + x] = pred[y * stride + x];
            }
        }
        return;
    }

    int shift1 = 7;
    int shift2 = 20 - bit_depth;
    if (shift2 < 0) shift2 = 0;

    int32_t tmp[32 * 32];
    int32_t tmp2[32 * 32];

    /* First pass: transform rows */
    for (int y = 0; y < size; y++) {
        hevc_inverse_transform_1d(coeffs + y * size, tmp + y * size, mat, size, shift1);
    }

    /* Transpose */
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            tmp2[x * size + y] = tmp[y * size + x];
        }
    }

    /* Second pass: transform columns (rows after transpose) */
    for (int x = 0; x < size; x++) {
        hevc_inverse_transform_1d(tmp2 + x * size, tmp + x * size, mat, size, shift2);
    }

    /* Transpose back and add prediction */
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            int residual = tmp[x * size + y];
            int val = (int)pred[y * stride + x] + residual;
            dst[y * stride + x] = (uint16_t)hevc_clip_bd(val, bit_depth);
        }
    }
}

static int hevc_get_mode(const hevc_context *ctx, uint32_t x, uint32_t y) {
    if (!ctx->mode_map || x >= ctx->frame_width || y >= ctx->frame_height) {
        return 1;
    }
    uint32_t bx = x / 4u;
    uint32_t by = y / 4u;
    if (by >= ctx->mode_stride) {
        return 1;
    }
    return ctx->mode_map[by * ctx->mode_stride + bx];
}

static void hevc_set_mode(hevc_context *ctx, uint32_t x, uint32_t y, uint32_t size, int mode) {
    if (!ctx->mode_map) {
        return;
    }
    uint32_t start_x = x / 4u;
    uint32_t start_y = y / 4u;
    uint32_t blocks = (size + 3u) / 4u;
    for (uint32_t by = 0; by < blocks; by++) {
        for (uint32_t bx = 0; bx < blocks; bx++) {
            uint32_t idx = (start_y + by) * ctx->mode_stride + (start_x + bx);
            if (start_y + by < (ctx->frame_height + 3u) / 4u &&
                start_x + bx < ctx->mode_stride) {
                ctx->mode_map[idx] = (uint8_t)mode;
            }
        }
    }
}

static void hevc_set_qp_map(hevc_context *ctx, uint32_t x, uint32_t y, uint32_t size, int qp) {
    if (!ctx->qp_map) {
        return;
    }
    if (qp < 0) qp = 0;
    if (qp > 51) qp = 51;
    uint32_t start_x = x / 4u;
    uint32_t start_y = y / 4u;
    uint32_t blocks = (size + 3u) / 4u;
    for (uint32_t by = 0; by < blocks; by++) {
        if (start_y + by >= ctx->map_height) {
            continue;
        }
        for (uint32_t bx = 0; bx < blocks; bx++) {
            if (start_x + bx >= ctx->map_stride) {
                continue;
            }
            ctx->qp_map[(start_y + by) * ctx->map_stride + (start_x + bx)] = (uint8_t)qp;
        }
    }
}

static void hevc_set_residual_map(hevc_context *ctx, uint32_t x, uint32_t y, uint32_t size, int has_residual) {
    if (!ctx->residual_map) {
        return;
    }
    uint32_t start_x = x / 4u;
    uint32_t start_y = y / 4u;
    uint32_t blocks = (size + 3u) / 4u;
    uint8_t val = has_residual ? 1u : 0u;
    for (uint32_t by = 0; by < blocks; by++) {
        if (start_y + by >= ctx->map_height) {
            continue;
        }
        for (uint32_t bx = 0; bx < blocks; bx++) {
            if (start_x + bx >= ctx->map_stride) {
                continue;
            }
            ctx->residual_map[(start_y + by) * ctx->map_stride + (start_x + bx)] = val;
        }
    }
}

static int hevc_get_qp_map(const hevc_context *ctx, uint32_t bx, uint32_t by, int fallback_qp) {
    if (!ctx->qp_map || bx >= ctx->map_stride || by >= ctx->map_height) {
        return fallback_qp;
    }
    return (int)ctx->qp_map[by * ctx->map_stride + bx];
}

static int hevc_get_chroma_qp(const hevc_context *ctx, const hevc_pps *pps,
                              int qp_y, int bit_depth, int chroma_format_idc, int is_cb);
static int hevc_clamp_int(int v, int lo, int hi);

static int hevc_get_residual_map(const hevc_context *ctx, uint32_t bx, uint32_t by) {
    if (!ctx->residual_map || bx >= ctx->map_stride || by >= ctx->map_height) {
        return 0;
    }
    return ctx->residual_map[by * ctx->map_stride + bx] != 0;
}

static int hevc_get_intra_map(const hevc_context *ctx, uint32_t bx, uint32_t by) {
    if (!ctx->mode_map || bx >= ctx->mode_stride || by >= ctx->map_height) {
        return 1;
    }
    return ctx->mode_map[by * ctx->mode_stride + bx] != 0;
}

static int hevc_sao_sign(int v) {
    return (v > 0) - (v < 0);
}

static void hevc_apply_sao_plane(uint16_t *dst, const uint16_t *src, uint32_t stride,
                                 uint32_t frame_w, uint32_t frame_h,
                                 uint32_t x0, uint32_t y0, uint32_t w, uint32_t h,
                                 int bit_depth, const hevc_sao *sao) {
    if (!sao || sao->type == 0) {
        return;
    }
    int max_val = (1 << bit_depth) - 1;
    if (sao->type == 1) {
        int shift = bit_depth - 5;
        if (shift < 0) shift = 0;
        for (uint32_t y = 0; y < h; y++) {
            uint32_t py = y0 + y;
            for (uint32_t x = 0; x < w; x++) {
                uint32_t px = x0 + x;
                int v = src[py * stride + px];
                int band = (shift > 0) ? (v >> shift) : v;
                int idx = (band - sao->band_pos) & 31;
                if (idx < 4) {
                    v = hevc_clamp_int(v + sao->offset[idx], 0, max_val);
                }
                dst[py * stride + px] = (uint16_t)v;
            }
        }
        return;
    }
    for (uint32_t y = 0; y < h; y++) {
        uint32_t py = y0 + y;
        if (py == 0 || py + 1 >= frame_h) {
            continue;
        }
        for (uint32_t x = 0; x < w; x++) {
            uint32_t px = x0 + x;
            if (px == 0 || px + 1 >= frame_w) {
                continue;
            }
            int c = src[py * stride + px];
            int a = 0;
            int b = 0;
            if (sao->eo_class == 0) {
                a = src[py * stride + (px - 1)];
                b = src[py * stride + (px + 1)];
            } else if (sao->eo_class == 1) {
                a = src[(py - 1) * stride + px];
                b = src[(py + 1) * stride + px];
            } else if (sao->eo_class == 2) {
                a = src[(py - 1) * stride + (px + 1)];
                b = src[(py + 1) * stride + (px - 1)];
            } else {
                a = src[(py - 1) * stride + (px - 1)];
                b = src[(py + 1) * stride + (px + 1)];
            }
            int edge_type = hevc_sao_sign(c - a) + hevc_sao_sign(c - b);
            int idx = -1;
            if (edge_type == -2) idx = 0;
            else if (edge_type == -1) idx = 1;
            else if (edge_type == 1) idx = 2;
            else if (edge_type == 2) idx = 3;
            if (idx >= 0) {
                int v = hevc_clamp_int(c + sao->offset[idx], 0, max_val);
                dst[py * stride + px] = (uint16_t)v;
            }
        }
    }
}

static void hevc_apply_sao(hevc_context *ctx, const hevc_sps *sps,
                           int sao_luma, int sao_chroma) {
    if (!ctx || !sps || !ctx->sao_map) {
        return;
    }
    if (!sao_luma && !sao_chroma) {
        return;
    }
    size_t luma_total = (size_t)ctx->frame_stride * ctx->frame_height;
    uint16_t *src_luma = (uint16_t *)malloc(luma_total * sizeof(uint16_t));
    if (!src_luma) {
        return;
    }
    memcpy(src_luma, ctx->frame_luma, luma_total * sizeof(uint16_t));
    uint16_t *src_cb = NULL;
    uint16_t *src_cr = NULL;
    if (ctx->frame_cb && ctx->frame_cr && sao_chroma) {
        size_t chroma_total = (size_t)ctx->chroma_stride * ctx->chroma_height;
        src_cb = (uint16_t *)malloc(chroma_total * sizeof(uint16_t));
        src_cr = (uint16_t *)malloc(chroma_total * sizeof(uint16_t));
        if (!src_cb || !src_cr) {
            free(src_luma);
            free(src_cb);
            free(src_cr);
            return;
        }
        memcpy(src_cb, ctx->frame_cb, chroma_total * sizeof(uint16_t));
        memcpy(src_cr, ctx->frame_cr, chroma_total * sizeof(uint16_t));
    }
    uint32_t ctu = sps->ctu_size;
    if (ctu < 8) ctu = 8;
    uint32_t sub_w = 1;
    uint32_t sub_h = 1;
    if (sps->chroma_format_idc == 1) {
        sub_w = 2;
        sub_h = 2;
    } else if (sps->chroma_format_idc == 2) {
        sub_w = 2;
        sub_h = 1;
    }
    for (uint32_t cy = 0; cy < ctx->sao_height; cy++) {
        for (uint32_t cx = 0; cx < ctx->sao_stride; cx++) {
            const hevc_sao_ctu *ctu_sao = &ctx->sao_map[cy * ctx->sao_stride + cx];
            uint32_t x0 = cx * ctu;
            uint32_t y0 = cy * ctu;
            uint32_t w = (x0 + ctu <= ctx->frame_width) ? ctu : (ctx->frame_width - x0);
            uint32_t h = (y0 + ctu <= ctx->frame_height) ? ctu : (ctx->frame_height - y0);
            if (sao_luma && ctu_sao->luma.type) {
                hevc_apply_sao_plane(ctx->frame_luma, src_luma, ctx->frame_stride,
                                     ctx->frame_width, ctx->frame_height,
                                     x0, y0, w, h, (int)sps->bit_depth_luma,
                                     &ctu_sao->luma);
            }
            if (sao_chroma && src_cb && src_cr && sps->chroma_format_idc != 0) {
                uint32_t cx0 = x0 / sub_w;
                uint32_t cy0 = y0 / sub_h;
                uint32_t cw = (w + sub_w - 1u) / sub_w;
                uint32_t ch = (h + sub_h - 1u) / sub_h;
                if (ctu_sao->cb.type) {
                    hevc_apply_sao_plane(ctx->frame_cb, src_cb, ctx->chroma_stride,
                                         ctx->chroma_width, ctx->chroma_height,
                                         cx0, cy0, cw, ch, (int)sps->bit_depth_chroma,
                                         &ctu_sao->cb);
                }
                if (ctu_sao->cr.type) {
                    hevc_apply_sao_plane(ctx->frame_cr, src_cr, ctx->chroma_stride,
                                         ctx->chroma_width, ctx->chroma_height,
                                         cx0, cy0, cw, ch, (int)sps->bit_depth_chroma,
                                         &ctu_sao->cr);
                }
            }
        }
    }
    free(src_luma);
    free(src_cb);
    free(src_cr);
}

static void hevc_build_mpm_list(int left_mode, int top_mode, int *mpm) {
    if (left_mode == top_mode) {
        mpm[0] = left_mode;
        mpm[1] = (left_mode == 0) ? 1 : 0;
        mpm[2] = (left_mode == 1) ? 0 : 1;
    } else {
        mpm[0] = left_mode;
        mpm[1] = top_mode;
        if (left_mode != 0 && top_mode != 0) {
            mpm[2] = 0;
        } else if (left_mode != 1 && top_mode != 1) {
            mpm[2] = 1;
        } else {
            mpm[2] = 2;
        }
    }
}

static int hevc_decode_intra_mode(hevc_context *ctx, hevc_cabac *cabac,
                                  hevc_cabac_state *st, uint32_t x, uint32_t y,
                                  int *mode) {
    int left_mode = (x >= 4) ? hevc_get_mode(ctx, x - 4, y) : 1;
    int top_mode = (y >= 4) ? hevc_get_mode(ctx, x, y - 4) : 1;
    int mpm[3];
    hevc_build_mpm_list(left_mode, top_mode, mpm);
    uint32_t prev_flag = 0;
    if (!hevc_cabac_decode_bin(cabac, &st->prev_intra_pred_flag, &prev_flag)) {
        if (cabac && cabac->bs) {
            heif_debugf("hevc: intra pred flag decode failed at (%u,%u) pos=%zu bits=%d\n",
                        x, y, cabac->bs->pos, cabac->bs->bitcount);
        }
        return 0;
    }
    if (prev_flag) {
        uint32_t idx = 0;
        if (!hevc_cabac_decode_bin(cabac, &st->mpm_idx, &idx)) {
            if (cabac && cabac->bs) {
                heif_debugf("hevc: mpm idx decode failed at (%u,%u) pos=%zu bits=%d\n",
                            x, y, cabac->bs->pos, cabac->bs->bitcount);
            }
            return 0;
        }
        uint32_t idx2 = 0;
        if (idx) {
            if (!hevc_cabac_decode_bypass(cabac, &idx2)) {
                return 0;
            }
            idx = 1u + idx2;
        }
        if (idx > 2) idx = 2;
        *mode = mpm[idx];
        return 1;
    }
    uint32_t rem = 0;
    for (int i = 0; i < 5; i++) {
        uint32_t b = 0;
        if (!hevc_cabac_decode_bypass(cabac, &b)) {
            return 0;
        }
        rem = (rem << 1) | b;
    }
    int count = 0;
    for (int m = 0; m < 35; m++) {
        if (m == mpm[0] || m == mpm[1] || m == mpm[2]) {
            continue;
        }
        if (count == (int)rem) {
            *mode = m;
            return 1;
        }
        count++;
    }
    *mode = 1;
    return 1;
}

static int hevc_decode_intra_chroma_mode(hevc_cabac *cabac, hevc_cabac_state *st,
                                         int luma_mode, int *mode) {
    int derived_mode = (luma_mode == 0 || luma_mode == 1 ||
                        luma_mode == 10 || luma_mode == 26) ? luma_mode : 1;
    uint32_t flag = 0;
    if (!hevc_cabac_decode_bin(cabac, &st->intra_chroma_pred_mode, &flag)) {
        return 0;
    }
    if (!flag) {
        *mode = derived_mode;
        return 1;
    }
    uint32_t value = 0;
    for (int i = 0; i < 2; i++) {
        uint32_t b = 0;
        if (!hevc_cabac_decode_bypass(cabac, &b)) {
            return 0;
        }
        value = (value << 1) | b;
    }
    switch (value & 3u) {
        case 0: *mode = 0; break;   /* planar */
        case 1: *mode = 26; break;  /* vertical */
        case 2: *mode = 10; break;  /* horizontal */
        case 3: *mode = 1; break;   /* DC */
        default: *mode = derived_mode; break;
    }
    return 1;
}

enum {
    HEVC_SCAN_DIAG = 0,
    HEVC_SCAN_HOR = 1,
    HEVC_SCAN_VER = 2
};

static int hevc_scan_idx_from_mode(int mode) {
    if (mode >= 6 && mode <= 14) {
        return HEVC_SCAN_HOR;
    }
    if (mode >= 22 && mode <= 30) {
        return HEVC_SCAN_VER;
    }
    return HEVC_SCAN_DIAG;
}

static void hevc_build_scan(int size, uint8_t *xs, uint8_t *ys, int scan_idx) {
    int idx = 0;
    if (scan_idx == HEVC_SCAN_HOR) {
        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                xs[idx] = (uint8_t)x;
                ys[idx] = (uint8_t)y;
                idx++;
            }
        }
        return;
    }
    if (scan_idx == HEVC_SCAN_VER) {
        for (int x = 0; x < size; x++) {
            for (int y = 0; y < size; y++) {
                xs[idx] = (uint8_t)x;
                ys[idx] = (uint8_t)y;
                idx++;
            }
        }
        return;
    }
    /* HEVC up-right diagonal scan:
     * For each diagonal (indexed by x+y sum), we scan from
     * bottom-left to top-right (increasing x, decreasing y).
     */
    for (int diag = 0; diag < 2 * size - 1; diag++) {
        int x_start = (diag < size) ? 0 : diag - size + 1;
        int y_start = (diag < size) ? diag : size - 1;
        int x = x_start;
        int y = y_start;
        while (x < size && y >= 0) {
            xs[idx] = (uint8_t)x;
            ys[idx] = (uint8_t)y;
            idx++;
            x++;
            y--;
        }
    }
}

static int hevc_log2_size(int size) {
    if (size == 4) return 2;
    if (size == 8) return 3;
    if (size == 16) return 4;
    if (size == 32) return 5;
    return 2;
}

static void hevc_last_sig_prefix_params(int log2_size, int *max_prefix, int *suffix_bits) {
    if (log2_size <= 2) {
        *max_prefix = 3;
        *suffix_bits = 0;
    } else if (log2_size == 3) {
        *max_prefix = 5;
        *suffix_bits = 1;
    } else if (log2_size == 4) {
        *max_prefix = 5;
        *suffix_bits = 2;
    } else {
        *max_prefix = 6;
        *suffix_bits = 4;
    }
}

static int hevc_last_sig_ctx_idx(int log2_size, int bin_idx, int is_chroma) {
    if (is_chroma) {
        if (bin_idx <= 0) return 15;
        if (bin_idx == 1) return 16;
        return 17;
    }
    int ctx_shift = (log2_size <= 3) ? 0 : (log2_size - 3);
    int ctx_offset = 3 * (log2_size - 2) + ((log2_size - 1) >> 2);
    int ctx = (bin_idx >> ctx_shift) + ctx_offset;
    if (ctx > 17) ctx = 17;
    if (ctx < 0) ctx = 0;
    return ctx;
}

static int hevc_decode_last_sig_coord(hevc_cabac *cabac, hevc_cabac_ctx *ctxs,
                                      int log2_size, int is_chroma,
                                      uint32_t *coord) {
    int max_prefix = 0;
    int suffix_bits = 0;
    hevc_last_sig_prefix_params(log2_size, &max_prefix, &suffix_bits);
    uint32_t prefix = 0;
    for (int bin = 0; bin < max_prefix; bin++) {
        uint32_t b = 0;
        int ctx_idx = hevc_last_sig_ctx_idx(log2_size, bin, is_chroma);
        if (!hevc_cabac_decode_bin(cabac, &ctxs[ctx_idx], &b)) {
            return 0;
        }
        if (b == 0) {
            *coord = prefix;
            return 1;
        }
        prefix++;
    }
    uint32_t suffix = 0;
    for (int i = 0; i < suffix_bits; i++) {
        uint32_t b = 0;
        if (!hevc_cabac_decode_bypass(cabac, &b)) {
            return 0;
        }
        suffix = (suffix << 1) | b;
    }
    *coord = prefix + suffix;
    return 1;
}

static int hevc_decode_last_sig_position(hevc_cabac *cabac, hevc_cabac_state *st,
                                         int size, int is_chroma,
                                         uint32_t *last_x, uint32_t *last_y) {
    int log2_size = hevc_log2_size(size);
    if (!hevc_decode_last_sig_coord(cabac, st->last_sig_x, log2_size, is_chroma, last_x)) {
        return 0;
    }
    if (!hevc_decode_last_sig_coord(cabac, st->last_sig_y, log2_size, is_chroma, last_y)) {
        return 0;
    }
    if (*last_x >= (uint32_t)size) *last_x = (uint32_t)size - 1;
    if (*last_y >= (uint32_t)size) *last_y = (uint32_t)size - 1;
    return 1;
}

static int hevc_sig_ctx_set(int size, int is_chroma) {
    (void)is_chroma;
    if (size == 4) {
        return 0;
    }
    if (size == 8) {
        return 1;
    }
    return 2;
}

static int hevc_sig_ctx_idx(int pos_in_sb, int size, int is_chroma) {
    int ctx_set = hevc_sig_ctx_set(size, is_chroma);
    if (pos_in_sb < 0) pos_in_sb = 0;
    if (pos_in_sb > 15) pos_in_sb = 15;
    int ctx = ctx_set * 16 + pos_in_sb;
    if (ctx >= HEVC_SIG_COEFF_CTXS) {
        ctx = HEVC_SIG_COEFF_CTXS - 1;
    }
    return ctx;
}

static int hevc_greater1_ctx_idx(int size, int is_chroma, int c1) {
    int ctx_set = 0;
    if (is_chroma) {
        ctx_set = (size == 4) ? 4 : 5;
    } else {
        if (size == 4) {
            ctx_set = 0;
        } else if (size == 8) {
            ctx_set = 1;
        } else if (size == 16) {
            ctx_set = 2;
        } else {
            ctx_set = 3;
        }
    }
    if (c1 < 0) c1 = 0;
    if (c1 > 3) c1 = 3;
    int ctx = ctx_set * 4 + c1;
    if (ctx > 23) ctx = 23;
    return ctx;
}

static int hevc_greater2_ctx_idx(int size, int is_chroma) {
    int ctx_set = 0;
    if (is_chroma) {
        ctx_set = (size == 4) ? 4 : 5;
    } else {
        if (size == 4) {
            ctx_set = 0;
        } else if (size == 8) {
            ctx_set = 1;
        } else if (size == 16) {
            ctx_set = 2;
        } else {
            ctx_set = 3;
        }
    }
    if (ctx_set > 5) ctx_set = 5;
    return ctx_set;
}

static int hevc_decode_coeffs(hevc_cabac *cabac, hevc_cabac_state *st, int16_t *coeffs,
                              int size, int is_chroma, int sign_data_hiding_enabled,
                              int scan_idx) {
    if (size != 4 && size != 8 && size != 16 && size != 32) {
        return 0;
    }
    int total = size * size;
    uint8_t xs[total];
    uint8_t ys[total];
    uint8_t sub_xs[16];
    uint8_t sub_ys[16];
    uint8_t sub_pos[16];
    hevc_build_scan(size, xs, ys, scan_idx);
    /* In HEVC, coefficient group scan uses scan_idx, but within each 4x4 group
       the scan is always diagonal. */
    hevc_build_scan(4, sub_xs, sub_ys, HEVC_SCAN_DIAG);
    for (int i = 0; i < 16; i++) {
        sub_pos[sub_ys[i] * 4 + sub_xs[i]] = (uint8_t)i;
    }
    uint32_t last_x = 0;
    uint32_t last_y = 0;
    if (!hevc_decode_last_sig_position(cabac, st, size, is_chroma, &last_x, &last_y)) {
        return 0;
    }
    int last_idx = 0;
    for (int i = 0; i < total; i++) {
        if (xs[i] == last_x && ys[i] == last_y) {
            last_idx = i;
            break;
        }
    }
    uint8_t sig[total];
    memset(sig, 0, (size_t)total);
    int sb_w = size / 4;
    int sb_h = size / 4;
    int sb_count = sb_w * sb_h;
    uint8_t sb_scan_x[sb_count];
    uint8_t sb_scan_y[sb_count];
    hevc_build_scan(sb_w, sb_scan_x, sb_scan_y, scan_idx);
    int last_sb_x = (int)(last_x / 4u);
    int last_sb_y = (int)(last_y / 4u);
    for (int sb = sb_count - 1; sb >= 0; sb--) {
        int sbx = sb_scan_x[sb];
        int sby = sb_scan_y[sb];
        int is_last_sb = (sbx == last_sb_x && sby == last_sb_y);
        uint32_t coded_flag = 1;
        if (!is_last_sb) {
            int ctx = (sbx > 0) + ((sby > 0) ? 2 : 0);
            if (!hevc_cabac_decode_bin(cabac, &st->coded_sub_block_flag[ctx], &coded_flag)) {
                return 0;
            }
        }
        if (!coded_flag) {
            continue;
        }
        for (int i = 0; i < total; i++) {
            if ((int)(xs[i] / 4) != sbx || (int)(ys[i] / 4) != sby) {
                continue;
            }
            if (i == last_idx) {
                sig[i] = 1;
                continue;
            }
            if (i > last_idx && is_last_sb) {
                continue;
            }
            uint32_t flag = 0;
            int pos_in_sb = sub_pos[(ys[i] & 3u) * 4u + (xs[i] & 3u)];
            int ctx_idx = hevc_sig_ctx_idx(pos_in_sb, size, is_chroma);
            if (!hevc_cabac_decode_bin(cabac, &st->sig_coeff[ctx_idx], &flag)) {
                return 0;
            }
            sig[i] = (uint8_t)flag;
        }
    }
    memset(coeffs, 0, (size_t)total * sizeof(*coeffs));
    for (int sb = sb_count - 1; sb >= 0; sb--) {
        int sbx = sb_scan_x[sb];
        int sby = sb_scan_y[sb];
        int first_sig = -1;
        int last_sig = -1;
        int num_sig = 0;
        for (int i = 0; i < total; i++) {
            if ((int)(xs[i] / 4) != sbx || (int)(ys[i] / 4) != sby) {
                continue;
            }
            if (!sig[i]) {
                continue;
            }
            if (first_sig < 0) {
                first_sig = i;
            }
            last_sig = i;
            num_sig++;
        }
        if (num_sig == 0) {
            continue;
        }
        int sign_hidden = 0;
        if (sign_data_hiding_enabled && num_sig >= 2 && (last_sig - first_sig) > 3) {
            sign_hidden = 1;
        }
        int c1 = 1;
        int c2 = 0;
        int cRiceParam = 0;
        int sum_abs = 0;
        int16_t *first_coeff_ptr = NULL;
        int sig_count = 0;
        for (int i = last_sig; i >= first_sig; i--) {
            if ((int)(xs[i] / 4) != sbx || (int)(ys[i] / 4) != sby) {
                continue;
            }
            if (!sig[i]) {
                continue;
            }
            uint32_t greater1 = 0;
            if (sig_count < 8) {
                int g1_ctx = hevc_greater1_ctx_idx(size, is_chroma, c1);
                if (!hevc_cabac_decode_bin(cabac, &st->greater1[g1_ctx], &greater1)) {
                    return 0;
                }
            }
            int base_level = 1 + (int)greater1;
            uint32_t greater2 = 0;
            if (greater1 && c2 == 0) {
                int g2_ctx = hevc_greater2_ctx_idx(size, is_chroma);
                if (!hevc_cabac_decode_bin(cabac, &st->greater2[g2_ctx], &greater2)) {
                    return 0;
                }
                base_level += (int)greater2;
                c2 = 1;
            }
            if (greater1) {
                c1 = 0;
            } else if (c1 < 4) {
                c1++;
            }
            if (base_level == 1) {
                cRiceParam = 0;
            }
            uint32_t rem = 0;
            if (!hevc_decode_coeff_abs_level_remaining(cabac, cRiceParam, &rem)) {
                return 0;
            }
            if ((uint32_t)(base_level + (int)rem) > (uint32_t)(3 << cRiceParam) && cRiceParam < 4) {
                cRiceParam++;
            }
            int level = base_level + (int)rem;
            sum_abs += level;
            int coeff_index = ys[i] * size + xs[i];
            if (sign_hidden && i == first_sig) {
                coeffs[coeff_index] = (int16_t)level;
                first_coeff_ptr = &coeffs[coeff_index];
            } else {
                uint32_t sign = 0;
                if (!hevc_cabac_decode_bypass(cabac, &sign)) {
                    return 0;
                }
                if (sign) {
                    level = -level;
                }
                coeffs[coeff_index] = (int16_t)level;
            }
            sig_count++;
        }
        if (sign_hidden && first_coeff_ptr) {
            if ((sum_abs & 1) == 0) {
                *first_coeff_ptr = (int16_t)(-*first_coeff_ptr);
            }
        }
    }
    return 1;
}

static int hevc_decode_chroma_plane(hevc_context *ctx, hevc_cabac *cabac, hevc_cabac_state *st,
                                    uint16_t *plane, uint32_t stride,
                                    uint32_t x, uint32_t y, uint32_t size,
                                    int mode, int bit_depth, int cbf,
                                    int qp, const hevc_pps *pps, int is_cb,
                                    char *err, size_t errcap) {
    if (!plane || size == 0) {
        return 1;
    }
    if (x + size > ctx->chroma_width || y + size > ctx->chroma_height) {
        return 1;
    }
    uint16_t pred[32 * 32];
    uint16_t recon[32 * 32];
    int16_t qcoeffs[32 * 32];
    int32_t coeffs[32 * 32];
    uint16_t ref[2 * 32 + 1];
    uint16_t mid = (uint16_t)(1u << (bit_depth - 1));
    ref[0] = (x > 0 && y > 0) ? plane[(y - 1) * stride + (x - 1)] : mid;
    for (uint32_t i = 0; i < size; i++) {
        if (y > 0 && (x + i) < ctx->chroma_width) {
            ref[1 + i] = plane[(y - 1) * stride + (x + i)];
        } else {
            ref[1 + i] = mid;
        }
        if (x > 0 && (y + i) < ctx->chroma_height) {
            ref[size + 1 + i] = plane[(y + i) * stride + (x - 1)];
        } else {
            ref[size + 1 + i] = mid;
        }
    }
    hevc_intra_predict_block(pred, (int)size, ref, (int)size, mode, bit_depth);
    if (cbf) {
        if (hevc_bs_exhausted(cabac->bs)) {
            heif_debugf("chroma coeffs: bitstream exhausted, using prediction only (pos=%zu size=%zu bits=%d)\n",
                       cabac->bs->pos, cabac->bs->size, cabac->bs->bitcount);
            for (uint32_t j = 0; j < size; j++) {
                for (uint32_t ii = 0; ii < size; ii++) {
                    if ((x + ii) < ctx->chroma_width && (y + j) < ctx->chroma_height) {
                        plane[(y + j) * stride + (x + ii)] = pred[j * size + ii];
                    }
                }
            }
            return 1;
        }
        int scan_idx = hevc_scan_idx_from_mode(mode);
        if (!hevc_decode_coeffs(cabac, st, qcoeffs, (int)size, 1,
                                ctx->sign_data_hiding_enabled_flag, scan_idx)) {
            if (cabac && cabac->bs) {
                heif_debugf("hevc coeffs fail (chroma) at (%u,%u) size=%u mode=%d scan=%d pos=%zu bits=%d\n",
                           x, y, size, mode, scan_idx, cabac->bs->pos, cabac->bs->bitcount);
            }
            if (hevc_bs_exhausted(cabac->bs)) {
                heif_debugf("chroma coeffs: decode failed at end, using prediction only (pos=%zu size=%zu bits=%d)\n",
                           cabac->bs->pos, cabac->bs->size, cabac->bs->bitcount);
                for (uint32_t j = 0; j < size; j++) {
                    for (uint32_t ii = 0; ii < size; ii++) {
                        if ((x + ii) < ctx->chroma_width && (y + j) < ctx->chroma_height) {
                            plane[(y + j) * stride + (x + ii)] = pred[j * size + ii];
                        }
                    }
                }
                return 1;
            }
            set_err(err, errcap, "HEVC chroma coefficient decode failed");
            return 0;
        }
        int qp_chroma = hevc_get_chroma_qp(ctx, pps, qp, bit_depth,
                                           ctx->sps[ctx->last_sps_id].chroma_format_idc, is_cb);
        static int qp_count = 0;
        if (qp_count < 10) {
            heif_debugf("chroma dequant: qp=%d qp_chroma=%d qcoeffs[0]=%d\n", qp, qp_chroma, qcoeffs[0]);
            qp_count++;
        }
        hevc_dequantize_block(qcoeffs, coeffs, (int)size, qp_chroma, bit_depth, 1);
        int has_residual = 0;
        for (uint32_t i = 0; i < size * size; i++) {
            if (coeffs[i] != 0) {
                has_residual = 1;
                break;
            }
        }
        static int resid_count = 0;
        if (resid_count < 10) {
            heif_debugf("chroma cbf=1 at (%u,%u): has_residual=%d pred[0]=%u coeffs[0]=%d plane[%u]=%u\n",
                       x, y, has_residual, pred[0], coeffs[0],
                       (y * stride + x), plane[y * stride + x]);
            resid_count++;
        }
        if (has_residual) {
            hevc_inverse_transform_block(coeffs, pred, recon, (int)size, (int)size, bit_depth, 0);
            static int recon_count = 0;
            if (recon_count < 5) {
                heif_debugf("BEFORE write: plane[%u]=%u recon[0]=%u\n",
                           (y * stride + x), plane[y * stride + x], recon[0]);
            }
            for (uint32_t j = 0; j < size; j++) {
                for (uint32_t ii = 0; ii < size; ii++) {
                    if ((x + ii) < ctx->chroma_width && (y + j) < ctx->chroma_height) {
                        plane[(y + j) * stride + (x + ii)] = recon[j * size + ii];
                    }
                }
            }
            if (recon_count < 5) {
                heif_debugf("AFTER write: plane[%u]=%u\n", (y * stride + x), plane[y * stride + x]);
                recon_count++;
            }
            return 1;
        }
    }
    for (uint32_t j = 0; j < size; j++) {
        for (uint32_t ii = 0; ii < size; ii++) {
            if ((x + ii) < ctx->chroma_width && (y + j) < ctx->chroma_height) {
                plane[(y + j) * stride + (x + ii)] = pred[j * size + ii];
            }
        }
    }
    return 1;
}

static void hevc_copy_pred_plane(uint16_t *plane, uint32_t stride,
                                 uint32_t x, uint32_t y, uint32_t size,
                                 const uint16_t *pred) {
    size_t row_bytes = (size_t)size * sizeof(uint16_t);
    for (uint32_t j = 0; j < size; j++) {
        memcpy(plane + (y + j) * stride + x, pred + j * size, row_bytes);
    }
}

static int hevc_clamp_int(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static const uint8_t hevc_chroma_qp_table[58] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
    29, 30, 31, 32, 33, 33, 34, 34, 35, 35, 36, 36, 37, 37,
    38, 38, 39, 39, 40, 40, 41, 41, 42, 42, 43, 43, 44, 44
};

static int hevc_map_chroma_qp(int qp_y, int qp_offset, int bit_depth, int chroma_format_idc) {
    int qp_bd_offset = 6 * (bit_depth - 8);
    if (qp_bd_offset < 0) {
        qp_bd_offset = 0;
    }
    int qp = qp_y + qp_offset;
    int qp_prime = hevc_clamp_int(qp, 0, 57);
    int mapped = qp_prime;
    if (chroma_format_idc == 1 || chroma_format_idc == 2) {
        mapped = hevc_chroma_qp_table[qp_prime];
    }
    mapped += qp_bd_offset;
    return hevc_clamp_int(mapped, 0, 57 + qp_bd_offset);
}

static int hevc_get_chroma_qp(const hevc_context *ctx, const hevc_pps *pps,
                              int qp_y, int bit_depth, int chroma_format_idc, int is_cb) {
    int offset = 0;
    if (pps) {
        offset += is_cb ? (int)pps->pps_cb_qp_offset : (int)pps->pps_cr_qp_offset;
    }
    if (ctx) {
        offset += is_cb ? ctx->slice_cb_qp_offset : ctx->slice_cr_qp_offset;
    }
    return hevc_map_chroma_qp(qp_y, offset, bit_depth, chroma_format_idc);
}

#define HEVC_MAX_QP 51
#define HEVC_DEFAULT_INTRA_TC_OFFSET 2

static const uint8_t hevc_tc_table[HEVC_MAX_QP + 1 + HEVC_DEFAULT_INTRA_TC_OFFSET] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3,
    3, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 8, 9, 10, 11, 13,
    14, 16, 18, 20, 22, 24
};

static const uint8_t hevc_beta_table[HEVC_MAX_QP + 1] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 20, 22, 24,
    26, 28, 30, 32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56,
    58, 60, 62, 64
};

static int hevc_clip_bd(int v, int bit_depth) {
    int max_val = (1 << bit_depth) - 1;
    return hevc_clamp_int(v, 0, max_val);
}

static int hevc_calc_dp(const uint16_t *src, int offset) {
    return abs((int)src[-offset * 3] - 2 * (int)src[-offset * 2] + (int)src[-offset]);
}

static int hevc_calc_dq(const uint16_t *src, int offset) {
    return abs((int)src[0] - 2 * (int)src[offset] + (int)src[offset * 2]);
}

static int hevc_use_strong_filtering(int offset, int d, int beta, int tc, const uint16_t *src) {
    int m0 = src[-offset * 4];
    int m3 = src[-offset * 1];
    int m4 = src[0];
    int m7 = src[offset * 3];
    if (abs(m0 - m3) + abs(m7 - m4) < (beta >> 3) &&
        d < (beta >> 2) &&
        abs(m3 - m4) < ((tc * 5 + 1) >> 1)) {
        return 1;
    }
    return 0;
}

static void hevc_pel_filter_luma(uint16_t *src, int offset, int tc, int sw,
                                 int part_p_no_filter, int part_q_no_filter,
                                 int thr_cut, int filter_p, int filter_q,
                                 int bit_depth) {
    int m0 = (int)src[-offset * 4];
    int m1 = (int)src[-offset * 3];
    int m2 = (int)src[-offset * 2];
    int m3 = (int)src[-offset * 1];
    int m4 = (int)src[0];
    int m5 = (int)src[offset * 1];
    int m6 = (int)src[offset * 2];
    int m7 = (int)src[offset * 3];

    if (sw) {
        src[-offset * 3] = (uint16_t)hevc_clip_bd(hevc_clamp_int(
            (2 * m0 + 3 * m1 + m2 + m3 + m4 + 4) >> 3, m3 - 2 * tc, m3 + 2 * tc), bit_depth);
        src[-offset * 2] = (uint16_t)hevc_clip_bd(hevc_clamp_int(
            (m1 + m2 + m3 + m4 + 2) >> 2, m3 - 2 * tc, m3 + 2 * tc), bit_depth);
        src[-offset * 1] = (uint16_t)hevc_clip_bd(hevc_clamp_int(
            (m2 + m3 + m4 + 2) >> 2, m3 - 2 * tc, m3 + 2 * tc), bit_depth);
        src[0] = (uint16_t)hevc_clip_bd(hevc_clamp_int(
            (m2 + m3 + m4 + 2) >> 2, m4 - 2 * tc, m4 + 2 * tc), bit_depth);
        src[offset * 1] = (uint16_t)hevc_clip_bd(hevc_clamp_int(
            (m3 + m4 + m5 + 2) >> 2, m4 - 2 * tc, m4 + 2 * tc), bit_depth);
        src[offset * 2] = (uint16_t)hevc_clip_bd(hevc_clamp_int(
            (m3 + m4 + m5 + m6 + 2) >> 2, m4 - 2 * tc, m4 + 2 * tc), bit_depth);
        src[offset * 3] = (uint16_t)hevc_clip_bd(hevc_clamp_int(
            (m3 + m4 + m5 + 3 * m6 + 2 * m7 + 4) >> 3, m4 - 2 * tc, m4 + 2 * tc), bit_depth);
    } else {
        int delta = (9 * (m4 - m3) - 3 * (m5 - m2) + 8) >> 4;
        if (abs(delta) < thr_cut) {
            delta = hevc_clamp_int(delta, -tc, tc);
            if (!part_p_no_filter) {
                m3 = hevc_clip_bd(m3 + delta, bit_depth);
                src[-offset * 1] = (uint16_t)m3;
            }
            if (!part_q_no_filter) {
                m4 = hevc_clip_bd(m4 - delta, bit_depth);
                src[0] = (uint16_t)m4;
            }
            int tc2 = tc >> 1;
            if (filter_p && !part_p_no_filter) {
                int delta_p = hevc_clamp_int((((m1 + m3 + 1) >> 1) - m2 + delta) >> 1,
                                             -tc2, tc2);
                m2 = hevc_clip_bd(m2 + delta_p, bit_depth);
                src[-offset * 2] = (uint16_t)m2;
            }
            if (filter_q && !part_q_no_filter) {
                int delta_q = hevc_clamp_int((((m6 + m4 + 1) >> 1) - m5 - delta) >> 1,
                                             -tc2, tc2);
                m5 = hevc_clip_bd(m5 + delta_q, bit_depth);
                src[offset * 1] = (uint16_t)m5;
            }
        }
    }

    if (part_p_no_filter) {
        src[-offset * 1] = (uint16_t)m3;
        src[-offset * 2] = (uint16_t)m2;
        src[-offset * 3] = (uint16_t)m1;
    }
    if (part_q_no_filter) {
        src[0] = (uint16_t)m4;
        src[offset * 1] = (uint16_t)m5;
        src[offset * 2] = (uint16_t)m6;
    }
}

static void hevc_pel_filter_chroma(uint16_t *src, int offset, int tc,
                                   int part_p_no_filter, int part_q_no_filter,
                                   int bit_depth) {
    int p1 = (int)src[-offset * 2];
    int p0 = (int)src[-offset * 1];
    int q0 = (int)src[0];
    int q1 = (int)src[offset * 1];
    int delta = (((q0 - p0) * 4) + p1 - q1 + 4) >> 3;

    if (part_p_no_filter || part_q_no_filter) {
        delta = 0;
    }

    delta = hevc_clamp_int(delta, -tc, tc);
    if (!part_p_no_filter) {
        p0 = hevc_clip_bd(p0 + delta, bit_depth);
        src[-offset * 1] = (uint16_t)p0;
    }
    if (!part_q_no_filter) {
        q0 = hevc_clip_bd(q0 - delta, bit_depth);
        src[0] = (uint16_t)q0;
    }
}

static int hevc_calc_bs(const hevc_context *ctx, uint32_t bx0, uint32_t by0, uint32_t bx1, uint32_t by1) {
    int intra = hevc_get_intra_map(ctx, bx0, by0) || hevc_get_intra_map(ctx, bx1, by1);
    if (intra) {
        return 2;
    }
    if (hevc_get_residual_map(ctx, bx0, by0) || hevc_get_residual_map(ctx, bx1, by1)) {
        return 1;
    }
    return 0;
}

static int hevc_calc_edge_qp(const hevc_context *ctx, uint32_t bx0, uint32_t by0,
                             uint32_t bx1, uint32_t by1, int slice_qp) {
    int qp0 = hevc_get_qp_map(ctx, bx0, by0, slice_qp);
    int qp1 = hevc_get_qp_map(ctx, bx1, by1, slice_qp);
    int qp = (qp0 + qp1 + 1) >> 1;
    if (qp < 0) qp = 0;
    if (qp > 51) qp = 51;
    return qp;
}

static void hevc_deblock_luma_plane(const hevc_context *ctx, uint16_t *plane, uint32_t stride,
                                    uint32_t width, uint32_t height,
                                    int bit_depth, int slice_qp,
                                    int beta_offset_div2, int tc_offset_div2) {
    if (!plane || width < 4 || height < 4) {
        return;
    }
    int bitdepth_scale = 1 << (bit_depth - 8);

    for (uint32_t x = 8; x + 3 < width; x += 8) {
        if (x < 4) {
            continue;
        }
        uint32_t bx = x / 4u;
        for (uint32_t y = 0; y + 3 < height; y += 4) {
            uint32_t by = y / 4u;
            if (bx == 0) {
                continue;
            }
            int bs = hevc_calc_bs(ctx, bx - 1, by, bx, by);
            if (bs == 0) {
                continue;
            }
            int qp = hevc_calc_edge_qp(ctx, bx - 1, by, bx, by, slice_qp);
            int qp_base = hevc_clamp_int(qp + 6 * (bit_depth - 8), 0, HEVC_MAX_QP);
            int qp_tc = hevc_clamp_int(qp_base + HEVC_DEFAULT_INTRA_TC_OFFSET + (tc_offset_div2 << 1),
                                       0, HEVC_MAX_QP + HEVC_DEFAULT_INTRA_TC_OFFSET);
            int qp_beta = hevc_clamp_int(qp_base + (beta_offset_div2 << 1), 0, HEVC_MAX_QP);
            int tc = (int)hevc_tc_table[qp_tc] * bitdepth_scale;
            int beta = (int)hevc_beta_table[qp_beta] * bitdepth_scale;
            if (tc == 0 && beta == 0) {
                continue;
            }
            int side_threshold = (beta + (beta >> 1)) >> 3;
            int thr_cut = tc * 10;
            uint16_t *src0 = plane + y * stride + x;
            uint16_t *src3 = plane + (y + 3) * stride + x;
            int dp0 = hevc_calc_dp(src0, 1);
            int dq0 = hevc_calc_dq(src0, 1);
            int dp3 = hevc_calc_dp(src3, 1);
            int dq3 = hevc_calc_dq(src3, 1);
            int d0 = dp0 + dq0;
            int d3 = dp3 + dq3;
            int dp = dp0 + dp3;
            int dq = dq0 + dq3;
            int d = d0 + d3;
            if (d < beta) {
                int filter_p = dp < side_threshold;
                int filter_q = dq < side_threshold;
                int sw = (bs == 2) ? hevc_use_strong_filtering(1, 2 * d0, beta, tc, src0) : 0;
                for (int i = 0; i < 4; i++) {
                    hevc_pel_filter_luma(plane + (y + (uint32_t)i) * stride + x, 1, tc, sw,
                                         0, 0, thr_cut, filter_p, filter_q, bit_depth);
                }
            }
        }
    }

    for (uint32_t y = 8; y + 3 < height; y += 8) {
        if (y < 4) {
            continue;
        }
        uint32_t by = y / 4u;
        for (uint32_t x = 0; x + 3 < width; x += 4) {
            uint32_t bx = x / 4u;
            if (by == 0) {
                continue;
            }
            int bs = hevc_calc_bs(ctx, bx, by - 1, bx, by);
            if (bs == 0) {
                continue;
            }
            int qp = hevc_calc_edge_qp(ctx, bx, by - 1, bx, by, slice_qp);
            int qp_base = hevc_clamp_int(qp + 6 * (bit_depth - 8), 0, HEVC_MAX_QP);
            int qp_tc = hevc_clamp_int(qp_base + HEVC_DEFAULT_INTRA_TC_OFFSET + (tc_offset_div2 << 1),
                                       0, HEVC_MAX_QP + HEVC_DEFAULT_INTRA_TC_OFFSET);
            int qp_beta = hevc_clamp_int(qp_base + (beta_offset_div2 << 1), 0, HEVC_MAX_QP);
            int tc = (int)hevc_tc_table[qp_tc] * bitdepth_scale;
            int beta = (int)hevc_beta_table[qp_beta] * bitdepth_scale;
            if (tc == 0 && beta == 0) {
                continue;
            }
            int side_threshold = (beta + (beta >> 1)) >> 3;
            int thr_cut = tc * 10;
            uint16_t *src0 = plane + y * stride + x;
            uint16_t *src3 = plane + y * stride + (x + 3);
            int dp0 = hevc_calc_dp(src0, (int)stride);
            int dq0 = hevc_calc_dq(src0, (int)stride);
            int dp3 = hevc_calc_dp(src3, (int)stride);
            int dq3 = hevc_calc_dq(src3, (int)stride);
            int d0 = dp0 + dq0;
            int d3 = dp3 + dq3;
            int dp = dp0 + dp3;
            int dq = dq0 + dq3;
            int d = d0 + d3;
            if (d < beta) {
                int filter_p = dp < side_threshold;
                int filter_q = dq < side_threshold;
                int sw = (bs == 2) ? hevc_use_strong_filtering((int)stride, 2 * d0, beta, tc, src0) : 0;
                for (int i = 0; i < 4; i++) {
                    hevc_pel_filter_luma(plane + y * stride + (x + (uint32_t)i),
                                         (int)stride, tc, sw,
                                         0, 0, thr_cut, filter_p, filter_q, bit_depth);
                }
            }
        }
    }
}

static void hevc_deblock_chroma_plane(const hevc_context *ctx, const hevc_pps *pps,
                                      uint16_t *plane, uint32_t stride,
                                      uint32_t width, uint32_t height,
                                      int bit_depth, int slice_qp,
                                      int tc_offset_div2,
                                      uint32_t step_x, uint32_t step_y,
                                      uint32_t sub_w, uint32_t sub_h,
                                      int is_cb, int chroma_format_idc) {
    if (!plane || width < 2 || height < 2) {
        return;
    }

    if (step_x == 0) step_x = 4;
    if (step_y == 0) step_y = 4;

    int bitdepth_scale = 1 << (bit_depth - 8);

    for (uint32_t x = step_x; x + 1 < width; x += step_x) {
        if (x < 2) {
            continue;
        }
        uint32_t lx = x * sub_w;
        uint32_t bx = lx / 4u;
        for (uint32_t y = 0; y < height; y++) {
            uint32_t ly = y * sub_h;
            uint32_t by = ly / 4u;
            if (bx == 0) {
                continue;
            }
            int bs = hevc_calc_bs(ctx, bx - 1, by, bx, by);
            if (bs == 0) {
                continue;
            }
            int qp = hevc_calc_edge_qp(ctx, bx - 1, by, bx, by, slice_qp);
            int qp_chroma = hevc_get_chroma_qp(ctx, pps, qp, bit_depth, chroma_format_idc, is_cb);
            int qp_tc = hevc_clamp_int(qp_chroma + HEVC_DEFAULT_INTRA_TC_OFFSET + (tc_offset_div2 << 1),
                                       0, HEVC_MAX_QP + HEVC_DEFAULT_INTRA_TC_OFFSET);
            int tc = (int)hevc_tc_table[qp_tc] * bitdepth_scale;
            if (tc == 0) {
                continue;
            }
            hevc_pel_filter_chroma(plane + y * stride + x, 1, tc, 0, 0, bit_depth);
        }
    }

    for (uint32_t y = step_y; y + 1 < height; y += step_y) {
        if (y < 2) {
            continue;
        }
        uint32_t ly = y * sub_h;
        uint32_t by = ly / 4u;
        for (uint32_t x = 0; x < width; x++) {
            uint32_t lx = x * sub_w;
            uint32_t bx = lx / 4u;
            if (by == 0) {
                continue;
            }
            int bs = hevc_calc_bs(ctx, bx, by - 1, bx, by);
            if (bs == 0) {
                continue;
            }
            int qp = hevc_calc_edge_qp(ctx, bx, by - 1, bx, by, slice_qp);
            int qp_chroma = hevc_get_chroma_qp(ctx, pps, qp, bit_depth, chroma_format_idc, is_cb);
            int qp_tc = hevc_clamp_int(qp_chroma + HEVC_DEFAULT_INTRA_TC_OFFSET + (tc_offset_div2 << 1),
                                       0, HEVC_MAX_QP + HEVC_DEFAULT_INTRA_TC_OFFSET);
            int tc = (int)hevc_tc_table[qp_tc] * bitdepth_scale;
            if (tc == 0) {
                continue;
            }
            hevc_pel_filter_chroma(plane + y * stride + x, (int)stride, tc, 0, 0, bit_depth);
        }
    }
}

static void hevc_deblock_frame(hevc_context *ctx, const hevc_sps *sps,
                               const hevc_pps *pps,
                               int slice_qp, int beta_offset_div2, int tc_offset_div2) {
    if (!ctx || !sps || !ctx->frame_luma) {
        return;
    }
    int bit_depth = (int)sps->bit_depth_luma;
    hevc_deblock_luma_plane(ctx, ctx->frame_luma, ctx->frame_stride,
                            ctx->frame_width, ctx->frame_height,
                            bit_depth, slice_qp, beta_offset_div2, tc_offset_div2);

    if (ctx->frame_cb && ctx->frame_cr) {
        uint32_t sub_w = 1;
        uint32_t sub_h = 1;
        if (sps->chroma_format_idc == 1) {
            sub_w = 2;
            sub_h = 2;
        } else if (sps->chroma_format_idc == 2) {
            sub_w = 2;
            sub_h = 1;
        }
        uint32_t step_x = 8 / sub_w;
        uint32_t step_y = 8 / sub_h;
        if (step_x == 0) step_x = 4;
        if (step_y == 0) step_y = 4;
        int cbit_depth = (int)sps->bit_depth_chroma;
        hevc_deblock_chroma_plane(ctx, pps, ctx->frame_cb, ctx->chroma_stride,
                                  ctx->chroma_width, ctx->chroma_height,
                                  cbit_depth, slice_qp, tc_offset_div2,
                                  step_x, step_y, sub_w, sub_h,
                                  1, sps->chroma_format_idc);
        hevc_deblock_chroma_plane(ctx, pps, ctx->frame_cr, ctx->chroma_stride,
                                  ctx->chroma_width, ctx->chroma_height,
                                  cbit_depth, slice_qp, tc_offset_div2,
                                  step_x, step_y, sub_w, sub_h,
                                  0, sps->chroma_format_idc);
    }
}

static int hevc_decode_tu(hevc_context *ctx, hevc_cabac *cabac, hevc_cabac_state *st,
                          uint32_t x, uint32_t y, uint32_t size, int depth,
                          int luma_mode, int chroma_mode,
                          int bit_depth, int *cur_qp, int *cu_qp_delta_coded,
                          const hevc_pps *pps, char *err, size_t errcap) {
    const hevc_sps *sps = &ctx->sps[ctx->last_sps_id];
    int min_tu = 1 << ((int)sps->log2_min_luma_transform_block_size_minus2 + 2);
    if (min_tu < 4) {
        min_tu = 4;
    }
    if (size > (uint32_t)min_tu && depth < (int)sps->max_transform_hierarchy_depth_intra) {
        uint32_t split = 0;
        if (!hevc_cabac_decode_bin(cabac, &st->split_tu_flag[depth < 3 ? depth : 2], &split)) {
            set_err(err, errcap, "HEVC TU split decode failed");
            return 0;
        }
        if (split && size > (uint32_t)min_tu) {
            uint32_t half = size / 2;
            if (!hevc_decode_tu(ctx, cabac, st, x, y, half, depth + 1, luma_mode, chroma_mode,
                                bit_depth, cur_qp, cu_qp_delta_coded, pps, err, errcap)) return 0;
            if (!hevc_decode_tu(ctx, cabac, st, x + half, y, half, depth + 1, luma_mode, chroma_mode,
                                bit_depth, cur_qp, cu_qp_delta_coded, pps, err, errcap)) return 0;
            if (!hevc_decode_tu(ctx, cabac, st, x, y + half, half, depth + 1, luma_mode, chroma_mode,
                                bit_depth, cur_qp, cu_qp_delta_coded, pps, err, errcap)) return 0;
            if (!hevc_decode_tu(ctx, cabac, st, x + half, y + half, half, depth + 1, luma_mode, chroma_mode,
                                bit_depth, cur_qp, cu_qp_delta_coded, pps, err, errcap)) return 0;
            return 1;
        }
    }
    if (size > 32) {
        size = 32;
    }
    if ((int)size < min_tu) {
        size = (uint32_t)min_tu;
    }
    uint32_t cbf_luma = 0;
    uint32_t cbf_cb = 0;
    uint32_t cbf_cr = 0;
    int chroma_decode_here = 0;
    int cbf_ctx = (depth > 0) ? 1 : 0;
    if (!hevc_cabac_decode_bin(cabac, &st->cbf_luma[cbf_ctx], &cbf_luma)) {
        set_err(err, errcap, "HEVC cbf_luma decode failed");
        return 0;
    }
    if (sps->chroma_format_idc != 0 && ctx->frame_cb && ctx->frame_cr) {
        uint32_t root_w = ctx->chroma_root_w;
        uint32_t root_h = ctx->chroma_root_h;
        if (root_w == 0 || root_h == 0 || !ctx->chroma_cbf_cb_map || !ctx->chroma_cbf_cr_map) {
            if (!hevc_cabac_decode_bin(cabac, &st->cbf_cb[cbf_ctx], &cbf_cb) ||
                !hevc_cabac_decode_bin(cabac, &st->cbf_cr[cbf_ctx], &cbf_cr)) {
                set_err(err, errcap, "HEVC cbf_chroma decode failed");
                return 0;
            }
            chroma_decode_here = 1;
        } else {
            uint32_t root_x = x / root_w;
            uint32_t root_y = y / root_h;
            int below_root = (size < root_w || size < root_h);
            int is_root_tl = ((x % root_w) == 0 && (y % root_h) == 0);
            if (root_x < ctx->chroma_cbf_stride && root_y < ctx->chroma_cbf_height) {
                size_t root_idx = (size_t)root_y * ctx->chroma_cbf_stride + root_x;
                if (!below_root || is_root_tl) {
                    if (!hevc_cabac_decode_bin(cabac, &st->cbf_cb[cbf_ctx], &cbf_cb) ||
                        !hevc_cabac_decode_bin(cabac, &st->cbf_cr[cbf_ctx], &cbf_cr)) {
                        set_err(err, errcap, "HEVC cbf_chroma decode failed");
                        return 0;
                    }
                    ctx->chroma_cbf_cb_map[root_idx] = (uint8_t)cbf_cb;
                    ctx->chroma_cbf_cr_map[root_idx] = (uint8_t)cbf_cr;
                    chroma_decode_here = 1;
                } else {
                    cbf_cb = ctx->chroma_cbf_cb_map[root_idx];
                    cbf_cr = ctx->chroma_cbf_cr_map[root_idx];
                }
            } else {
                if (!hevc_cabac_decode_bin(cabac, &st->cbf_cb[cbf_ctx], &cbf_cb) ||
                    !hevc_cabac_decode_bin(cabac, &st->cbf_cr[cbf_ctx], &cbf_cr)) {
                    set_err(err, errcap, "HEVC cbf_chroma decode failed");
                    return 0;
                }
                chroma_decode_here = 1;
            }
        }
    }
    int qp_block_coded = 0;
    size_t qp_block_idx = 0;
    if (ctx->qp_delta_coded_map && ctx->qp_block_size) {
        uint32_t bx = x / ctx->qp_block_size;
        uint32_t by = y / ctx->qp_block_size;
        if (bx < ctx->qp_delta_stride && by < ctx->qp_delta_height) {
            qp_block_idx = (size_t)by * ctx->qp_delta_stride + bx;
            qp_block_coded = ctx->qp_delta_coded_map[qp_block_idx] != 0;
        }
    } else if (cu_qp_delta_coded && *cu_qp_delta_coded) {
        qp_block_coded = 1;
    }
    if (pps && pps->cu_qp_delta_enabled_flag && cur_qp &&
        !qp_block_coded && (cbf_luma || cbf_cb || cbf_cr)) {
        if (cabac->bs->pos >= cabac->bs->size && cabac->bs->bitcount < 8) {
            heif_debugf("cu_qp_delta: bitstream exhausted, skipping (pos=%zu size=%zu bits=%d)\n",
                       cabac->bs->pos, cabac->bs->size, cabac->bs->bitcount);
            if (cur_qp) {
                *cur_qp = *cur_qp;
            }
        } else {
            int delta = 0;
            if (!hevc_decode_cu_qp_delta(cabac, st, &delta)) {
                set_err(err, errcap, "HEVC cu_qp_delta decode failed");
                return 0;
            }
            int qp = *cur_qp + delta;
            if (qp < 0) qp = 0;
            if (qp > 51) qp = 51;
            *cur_qp = qp;
            if (ctx->qp_delta_coded_map && ctx->qp_block_size &&
                qp_block_idx < (size_t)ctx->qp_delta_stride * ctx->qp_delta_height) {
                ctx->qp_delta_coded_map[qp_block_idx] = 1;
            }
        }
        if (cu_qp_delta_coded) {
            *cu_qp_delta_coded = 1;
        }
    }
    int qp = cur_qp ? *cur_qp : ctx->slice_qp;
    hevc_set_qp_map(ctx, x, y, size, qp);
    uint16_t pred[32 * 32];
    uint16_t recon[32 * 32];
    int16_t qcoeffs[32 * 32];
    int32_t coeffs[32 * 32];
    uint16_t ref[2 * 32 + 1];
    if (x + size > ctx->frame_width || y + size > ctx->frame_height) {
        return 1;
    }
    uint16_t mid = (uint16_t)(1u << (bit_depth - 1));
    ref[0] = (x > 0 && y > 0) ? ctx->frame_luma[(y - 1) * ctx->frame_stride + (x - 1)] : mid;
    for (uint32_t i = 0; i < size; i++) {
        if (y > 0 && (x + i) < ctx->frame_width) {
            ref[1 + i] = ctx->frame_luma[(y - 1) * ctx->frame_stride + (x + i)];
        } else {
            ref[1 + i] = mid;
        }
        if (x > 0 && (y + i) < ctx->frame_height) {
            ref[size + 1 + i] = ctx->frame_luma[(y + i) * ctx->frame_stride + (x - 1)];
        } else {
            ref[size + 1 + i] = mid;
        }
    }
    hevc_intra_predict_block(pred, (int)size, ref, (int)size, luma_mode, bit_depth);
    int has_residual = 0;
    if (cbf_luma) {
        int decoded_coeffs = 0;
        if (hevc_bs_exhausted(cabac->bs)) {
            static int luma_exhausted_count = 0;
            if (luma_exhausted_count < 5) {
                heif_debugf("luma coeffs: bitstream exhausted, using prediction only (pos=%zu size=%zu bits=%d)\n",
                           cabac->bs->pos, cabac->bs->size, cabac->bs->bitcount);
                luma_exhausted_count++;
            }
        } else {
            int scan_idx = hevc_scan_idx_from_mode(luma_mode);
            if (!hevc_decode_coeffs(cabac, st, qcoeffs, (int)size, 0,
                                    ctx->sign_data_hiding_enabled_flag, scan_idx)) {
                if (cabac && cabac->bs) {
                    heif_debugf("hevc coeffs fail (luma) at (%u,%u) size=%u mode=%d scan=%d pos=%zu bits=%d\n",
                               x, y, size, luma_mode, scan_idx, cabac->bs->pos, cabac->bs->bitcount);
                }
                if (hevc_bs_exhausted(cabac->bs)) {
                    static int luma_failed_count = 0;
                    if (luma_failed_count < 5) {
                        heif_debugf("luma coeffs: decode failed at end, using prediction only (pos=%zu size=%zu bits=%d)\n",
                                   cabac->bs->pos, cabac->bs->size, cabac->bs->bitcount);
                        luma_failed_count++;
                    }
                } else {
                    set_err(err, errcap, "HEVC coefficient decode failed");
                    return 0;
                }
            } else {
                decoded_coeffs = 1;
            }
        }
        if (decoded_coeffs) {
            hevc_dequantize_block(qcoeffs, coeffs, (int)size, qp, bit_depth, 1);
            for (uint32_t i = 0; i < size * size; i++) {
                if (coeffs[i] != 0) {
                    has_residual = 1;
                    break;
                }
            }
        }
        if (has_residual) {
            int use_dst = (size == 4);
            hevc_inverse_transform_block(coeffs, pred, recon, (int)size, (int)size, bit_depth, use_dst);
            for (uint32_t j = 0; j < size; j++) {
                for (uint32_t ii = 0; ii < size; ii++) {
                    if ((x + ii) < ctx->frame_width && (y + j) < ctx->frame_height) {
                        ctx->frame_luma[(y + j) * ctx->frame_stride + (x + ii)] = recon[j * size + ii];
                    }
                }
            }
        } else {
            hevc_copy_pred_plane(ctx->frame_luma, ctx->frame_stride, x, y, size, pred);
        }
    } else {
        hevc_copy_pred_plane(ctx->frame_luma, ctx->frame_stride, x, y, size, pred);
    }
    hevc_set_residual_map(ctx, x, y, size, has_residual);
    if (ctx->frame_cb && ctx->frame_cr && chroma_decode_here) {
        static int chroma_decode_count = 0;
        if (chroma_decode_count < 5) {
            heif_debugf("hevc_decode_tu: decoding chroma at (%u,%u) size=%u cbf_cb=%u cbf_cr=%u\n",
                       x, y, size, cbf_cb, cbf_cr);
            chroma_decode_count++;
        }
        uint32_t sub_w = 1;
        uint32_t sub_h = 1;
        if (ctx->sps[ctx->last_sps_id].chroma_format_idc == 1) {
            sub_w = 2;
            sub_h = 2;
        } else if (ctx->sps[ctx->last_sps_id].chroma_format_idc == 2) {
            sub_w = 2;
            sub_h = 1;
        }
        uint32_t cx = x / sub_w;
        uint32_t cy = y / sub_h;
        uint32_t csize = size / sub_w;
        if (csize < 4) {
            csize = 4;
        }
        if (!hevc_decode_chroma_plane(ctx, cabac, st, ctx->frame_cb, ctx->chroma_stride,
                                      cx, cy, csize, chroma_mode,
                                      ctx->sps[ctx->last_sps_id].bit_depth_chroma, (int)cbf_cb,
                                      qp, pps, 1,
                                      err, errcap)) {
            return 0;
        }
        if (!hevc_decode_chroma_plane(ctx, cabac, st, ctx->frame_cr, ctx->chroma_stride,
                                      cx, cy, csize, chroma_mode,
                                      ctx->sps[ctx->last_sps_id].bit_depth_chroma, (int)cbf_cr,
                                      qp, pps, 0,
                                      err, errcap)) {
            return 0;
        }
    }
    return 1;
}

static int hevc_decode_cu(hevc_context *ctx, hevc_cabac *cabac, hevc_cabac_state *st,
                          uint32_t x, uint32_t y, uint32_t size, int depth,
                          int bit_depth, int *cur_qp, int *cu_qp_delta_coded,
                          int qp_delta_log2, const hevc_pps *pps,
                          char *err, size_t errcap) {
    if (x >= ctx->frame_width || y >= ctx->frame_height) {
        return 1;
    }
    if (cabac->bs->pos >= cabac->bs->size && cabac->bs->bitcount < 8) {
        heif_debugf("hevc_decode_cu: bitstream exhausted at (%u,%u), stopping decode\n", x, y);
        return 1;
    }
    int local_qp_delta_coded = cu_qp_delta_coded ? *cu_qp_delta_coded : 0;
    int *qp_delta_flag = cu_qp_delta_coded;
    if (pps && pps->cu_qp_delta_enabled_flag) {
        int log2_size = hevc_log2_size((int)size);
        if (log2_size == qp_delta_log2) {
            local_qp_delta_coded = 0;
            qp_delta_flag = &local_qp_delta_coded;
        }
    }

    if (size > 8) {
        uint32_t split = 0;
        if (!hevc_cabac_decode_bin(cabac, &st->split_cu_flag[depth < 3 ? depth : 2], &split)) {
            set_err(err, errcap, "HEVC CU split decode failed");
            return 0;
        }
        if (split) {
            uint32_t half = size / 2;
            if (!hevc_decode_cu(ctx, cabac, st, x, y, half, depth + 1, bit_depth,
                                cur_qp, qp_delta_flag, qp_delta_log2, pps, err, errcap)) return 0;
            if (!hevc_decode_cu(ctx, cabac, st, x + half, y, half, depth + 1, bit_depth,
                                cur_qp, qp_delta_flag, qp_delta_log2, pps, err, errcap)) return 0;
            if (!hevc_decode_cu(ctx, cabac, st, x, y + half, half, depth + 1, bit_depth,
                                cur_qp, qp_delta_flag, qp_delta_log2, pps, err, errcap)) return 0;
            if (!hevc_decode_cu(ctx, cabac, st, x + half, y + half, half, depth + 1, bit_depth,
                                cur_qp, qp_delta_flag, qp_delta_log2, pps, err, errcap)) return 0;
            return 1;
        }
    }
    uint32_t split_intra = 0;
    if (size >= 8) {
        if (!hevc_cabac_decode_bin(cabac, &st->intra_split_flag, &split_intra)) {
            set_err(err, errcap, "HEVC intra split decode failed");
            return 0;
        }
    }
    if (split_intra && size >= 8) {
        uint32_t half = size / 2;
        for (uint32_t by = 0; by < 2; by++) {
            for (uint32_t bx = 0; bx < 2; bx++) {
                uint32_t px = x + bx * half;
                uint32_t py = y + by * half;
                int mode = 1;
                int chroma_mode = 1;
                if (!hevc_decode_intra_mode(ctx, cabac, st, px, py, &mode)) {
                    if (hevc_bs_exhausted(cabac->bs)) {
                        heif_debugf("hevc_decode_cu: intra mode decode failed at end (%u,%u), stopping decode\n",
                                   px, py);
                        return 1;
                    }
                    set_err(err, errcap, "HEVC intra mode decode failed");
                    return 0;
                }
                if (ctx->sps[ctx->last_sps_id].chroma_format_idc != 0) {
                    if (cabac->bs->pos >= cabac->bs->size && cabac->bs->bitcount < 8) {
                        heif_debugf("intra chroma mode: bitstream exhausted, using default mode\n");
                        chroma_mode = (mode == 0 || mode == 1 || mode == 10 || mode == 26) ? mode : 1;
                    } else if (!hevc_decode_intra_chroma_mode(cabac, st, mode, &chroma_mode)) {
                        if (hevc_bs_exhausted(cabac->bs)) {
                            heif_debugf("intra chroma mode: decode failed at end, using default mode\n");
                            chroma_mode = (mode == 0 || mode == 1 || mode == 10 || mode == 26) ? mode : 1;
                        } else {
                            set_err(err, errcap, "HEVC intra chroma mode decode failed");
                            return 0;
                        }
                    }
                } else {
                chroma_mode = (mode == 0 || mode == 1 || mode == 10 || mode == 26) ? mode : 1;
            }
                hevc_set_mode(ctx, px, py, half, mode);
                if (!hevc_decode_tu(ctx, cabac, st, px, py, half, 0, mode, chroma_mode,
                                    bit_depth, cur_qp, qp_delta_flag, pps, err, errcap)) {
                    return 0;
                }
            }
        }
    } else {
        int mode = 1;
        int chroma_mode = 1;
        if (!hevc_decode_intra_mode(ctx, cabac, st, x, y, &mode)) {
            if (hevc_bs_exhausted(cabac->bs)) {
                heif_debugf("hevc_decode_cu: intra mode decode failed at end (%u,%u), stopping decode\n",
                           x, y);
                return 1;
            }
            set_err(err, errcap, "HEVC intra mode decode failed");
            return 0;
        }
        if (ctx->sps[ctx->last_sps_id].chroma_format_idc != 0) {
            if (cabac->bs->pos >= cabac->bs->size && cabac->bs->bitcount < 8) {
                heif_debugf("intra chroma mode: bitstream exhausted, using default mode\n");
                chroma_mode = (mode == 0 || mode == 1 || mode == 10 || mode == 26) ? mode : 1;
            } else if (!hevc_decode_intra_chroma_mode(cabac, st, mode, &chroma_mode)) {
                if (hevc_bs_exhausted(cabac->bs)) {
                    heif_debugf("intra chroma mode: decode failed at end, using default mode\n");
                    chroma_mode = (mode == 0 || mode == 1 || mode == 10 || mode == 26) ? mode : 1;
                } else {
                    set_err(err, errcap, "HEVC intra chroma mode decode failed");
                    return 0;
                }
            }
        } else {
            chroma_mode = (mode == 0 || mode == 1 || mode == 10 || mode == 26) ? mode : 1;
        }
        hevc_set_mode(ctx, x, y, size, mode);
        if (!hevc_decode_tu(ctx, cabac, st, x, y, size, 0, mode, chroma_mode,
                            bit_depth, cur_qp, qp_delta_flag, pps, err, errcap)) {
            return 0;
        }
    }
    return 1;
}

static int hevc_validate_entry_offsets(const uint32_t *entry_offsets, uint32_t count,
                                       size_t base_size) {
    size_t prev = 0;
    for (uint32_t i = 0; i < count; i++) {
        size_t off = (size_t)entry_offsets[i];
        if (off <= prev || off > base_size) {
            return 0;
        }
        prev = off;
    }
    return 1;
}

static int hevc_compute_tile_bounds(const hevc_sps *sps, const hevc_pps *pps,
                                    uint32_t **col_bd_out, uint32_t **row_bd_out,
                                    char *err, size_t errcap) {
    if (!sps || !pps || !pps->tiles_enabled_flag) {
        if (col_bd_out) *col_bd_out = NULL;
        if (row_bd_out) *row_bd_out = NULL;
        return 1;
    }
    uint32_t num_cols = pps->num_tile_columns;
    uint32_t num_rows = pps->num_tile_rows;
    if (num_cols == 0 || num_rows == 0 ||
        sps->pic_width_in_ctus == 0 || sps->pic_height_in_ctus == 0) {
        set_err(err, errcap, "invalid HEVC tile layout");
        return 0;
    }
    uint32_t *col_bd = (uint32_t *)malloc((size_t)(num_cols + 1u) * sizeof(uint32_t));
    uint32_t *row_bd = (uint32_t *)malloc((size_t)(num_rows + 1u) * sizeof(uint32_t));
    if (!col_bd || !row_bd) {
        free(col_bd);
        free(row_bd);
        set_err(err, errcap, "out of memory");
        return 0;
    }
    if (pps->uniform_spacing_flag) {
        for (uint32_t i = 0; i <= num_cols; i++) {
            col_bd[i] = (uint32_t)(((uint64_t)i * sps->pic_width_in_ctus) / num_cols);
        }
        for (uint32_t i = 0; i <= num_rows; i++) {
            row_bd[i] = (uint32_t)(((uint64_t)i * sps->pic_height_in_ctus) / num_rows);
        }
    } else {
        if (num_cols > 1 && !pps->tile_col_width_minus1) {
            free(col_bd);
            free(row_bd);
            set_err(err, errcap, "missing HEVC tile column widths");
            return 0;
        }
        if (num_rows > 1 && !pps->tile_row_height_minus1) {
            free(col_bd);
            free(row_bd);
            set_err(err, errcap, "missing HEVC tile row heights");
            return 0;
        }
        col_bd[0] = 0;
        uint32_t sum_cols = 0;
        for (uint32_t i = 0; i + 1u < num_cols; i++) {
            uint32_t width = (uint32_t)pps->tile_col_width_minus1[i] + 1u;
            if (width == 0) {
                free(col_bd);
                free(row_bd);
                set_err(err, errcap, "invalid HEVC tile column width");
                return 0;
            }
            sum_cols += width;
            col_bd[i + 1u] = sum_cols;
        }
        col_bd[num_cols] = sps->pic_width_in_ctus;
        row_bd[0] = 0;
        uint32_t sum_rows = 0;
        for (uint32_t i = 0; i + 1u < num_rows; i++) {
            uint32_t height = (uint32_t)pps->tile_row_height_minus1[i] + 1u;
            if (height == 0) {
                free(col_bd);
                free(row_bd);
                set_err(err, errcap, "invalid HEVC tile row height");
                return 0;
            }
            sum_rows += height;
            row_bd[i + 1u] = sum_rows;
        }
        row_bd[num_rows] = sps->pic_height_in_ctus;
        if (sum_cols >= sps->pic_width_in_ctus || sum_rows >= sps->pic_height_in_ctus) {
            free(col_bd);
            free(row_bd);
            set_err(err, errcap, "invalid HEVC tile layout");
            return 0;
        }
    }
    for (uint32_t i = 0; i + 1u <= num_cols; i++) {
        if (i + 1u <= num_cols && col_bd[i] >= col_bd[i + 1u]) {
            free(col_bd);
            free(row_bd);
            set_err(err, errcap, "invalid HEVC tile columns");
            return 0;
        }
    }
    for (uint32_t i = 0; i + 1u <= num_rows; i++) {
        if (i + 1u <= num_rows && row_bd[i] >= row_bd[i + 1u]) {
            free(col_bd);
            free(row_bd);
            set_err(err, errcap, "invalid HEVC tile rows");
            return 0;
        }
    }
    if (col_bd[num_cols] != sps->pic_width_in_ctus ||
        row_bd[num_rows] != sps->pic_height_in_ctus) {
        free(col_bd);
        free(row_bd);
        set_err(err, errcap, "invalid HEVC tile bounds");
        return 0;
    }
    *col_bd_out = col_bd;
    *row_bd_out = row_bd;
    return 1;
}

static uint64_t hevc_calc_substream_count(const hevc_sps *sps, const hevc_pps *pps,
                                          int use_tiles, int use_wpp,
                                          const uint32_t *row_bd) {
    if (!use_tiles && !use_wpp) {
        return 1;
    }
    if (use_tiles) {
        uint32_t num_cols = pps ? pps->num_tile_columns : 0;
        uint32_t num_rows = pps ? pps->num_tile_rows : 0;
        if (!use_wpp) {
            return (uint64_t)num_cols * (uint64_t)num_rows;
        }
        if (!row_bd || num_cols == 0 || num_rows == 0) {
            return 0;
        }
        uint64_t total = 0;
        for (uint32_t r = 0; r < num_rows; r++) {
            uint32_t tile_h = row_bd[r + 1u] - row_bd[r];
            total += (uint64_t)tile_h * (uint64_t)num_cols;
        }
        return total;
    }
    if (use_wpp && sps) {
        return (uint64_t)sps->pic_height_in_ctus;
    }
    return 1;
}

static int hevc_decode_substream(hevc_context *ctx, const hevc_sps *sps, const hevc_pps *pps,
                                 hevc_bitstream *bs, int slice_type, int slice_qp,
                                 int slice_sao_luma_flag, int slice_sao_chroma_flag,
                                 uint32_t x0_ctu, uint32_t y0_ctu,
                                 uint32_t width_ctu, uint32_t height_ctu,
                                 int qp_delta_log2, int *cur_qp,
                                 hevc_cabac_state *prev_row_ctx, int *prev_row_valid,
                                 int use_row_sync,
                                 char *err, size_t errcap) {
    hevc_cabac cabac;
    hevc_cabac_state st;
    if (!hevc_cabac_init(&cabac, bs)) {
        set_err(err, errcap, "HEVC CABAC init failed");
        return 0;
    }
    hevc_init_cabac_state(&st, slice_type, slice_qp);
    if (use_row_sync && prev_row_ctx && prev_row_valid && *prev_row_valid) {
        st = *prev_row_ctx;
    }
    int row_ctx_saved = 0;
    for (uint32_t ry = 0; ry < height_ctu; ry++) {
        uint32_t cy = y0_ctu + ry;
        for (uint32_t rx = 0; rx < width_ctu; rx++) {
            uint32_t cx = x0_ctu + rx;
            if (cabac.bs->pos >= cabac.bs->size && cabac.bs->bitcount < 16) {
                heif_debugf("Bitstream exhausted at row %u col %u, ending substream early\n",
                           cy, cx);
                return 1;
            }
            uint32_t x = cx * sps->ctu_size;
            uint32_t y = cy * sps->ctu_size;
            if ((slice_sao_luma_flag || slice_sao_chroma_flag) &&
                !hevc_decode_sao_ctu(ctx, &cabac, &st, sps,
                                     slice_sao_luma_flag, slice_sao_chroma_flag, cx, cy)) {
                set_err(err, errcap, "HEVC SAO decode failed");
                return 0;
            }
            int cu_qp_delta_coded = 0;
            if (!hevc_decode_cu(ctx, &cabac, &st, x, y, sps->ctu_size, 0, sps->bit_depth_luma,
                                cur_qp, &cu_qp_delta_coded, qp_delta_log2, pps, err, errcap)) {
                return 0;
            }
            if (use_row_sync && !row_ctx_saved && prev_row_ctx && prev_row_valid) {
                if (width_ctu == 1u || rx > 0) {
                    *prev_row_ctx = st;
                    *prev_row_valid = 1;
                    row_ctx_saved = 1;
                }
            }
            uint32_t end_flag = 0;
            if (!hevc_cabac_decode_terminate(&cabac, &end_flag)) {
                set_err(err, errcap, "HEVC slice end decode failed");
                return 0;
            }
            if (end_flag) {
                return 2;
            }
        }
    }
    return 1;
}

static int hevc_decode_intra_slice_data(hevc_context *ctx, const hevc_sps *sps,
                                        const hevc_pps *pps, hevc_bitstream *bs,
                                        uint32_t slice_address,
                                        int slice_type, int slice_qp,
                                        int slice_sao_luma_flag, int slice_sao_chroma_flag,
                                        const uint32_t *entry_offsets, uint32_t num_entry_offsets,
                                        char *err, size_t errcap) {
    if (!ctx->frame_luma) {
        if (sps->display_width == 0 || sps->display_height == 0) {
            set_err(err, errcap, "invalid HEVC frame size");
            return 0;
        }
        size_t stride = sps->display_width;
        size_t total = stride * sps->display_height;
        if (total > SIZE_MAX) {
            set_err(err, errcap, "HEVC frame too large");
            return 0;
        }
        ctx->frame_luma = (uint16_t *)malloc(total * sizeof(uint16_t));
        if (!ctx->frame_luma) {
            set_err(err, errcap, "out of memory");
            return 0;
        }
        ctx->frame_stride = (uint32_t)stride;
        ctx->frame_width = sps->display_width;
        ctx->frame_height = sps->display_height;
        heif_debugf("Frame: %ux%u stride=%u\n", ctx->frame_width, ctx->frame_height, ctx->frame_stride);
        uint16_t mid_luma = (uint16_t)(1u << (sps->bit_depth_luma - 1));
        for (size_t i = 0; i < total; i++) {
            ctx->frame_luma[i] = mid_luma;
        }

        if (sps->chroma_format_idc != 0) {
            uint32_t sub_w = 1;
            uint32_t sub_h = 1;
            if (sps->chroma_format_idc == 1) {
                sub_w = 2;
                sub_h = 2;
            } else if (sps->chroma_format_idc == 2) {
                sub_w = 2;
                sub_h = 1;
            }
            ctx->chroma_width = (sps->display_width + sub_w - 1u) / sub_w;
            ctx->chroma_height = (sps->display_height + sub_h - 1u) / sub_h;
            ctx->chroma_stride = ctx->chroma_width;
            heif_debugf("Chroma: %ux%u stride=%u (sub=%ux%u)\n",
                       ctx->chroma_width, ctx->chroma_height, ctx->chroma_stride, sub_w, sub_h);
            size_t chroma_total = (size_t)ctx->chroma_stride * ctx->chroma_height;
            ctx->frame_cb = (uint16_t *)malloc(chroma_total * sizeof(uint16_t));
            ctx->frame_cr = (uint16_t *)malloc(chroma_total * sizeof(uint16_t));
            if (!ctx->frame_cb || !ctx->frame_cr) {
                set_err(err, errcap, "out of memory");
                return 0;
            }
            uint16_t mid_chroma = (uint16_t)(1u << (sps->bit_depth_chroma - 1));
            for (size_t i = 0; i < chroma_total; i++) {
                ctx->frame_cb[i] = mid_chroma;
                ctx->frame_cr[i] = mid_chroma;
            }
        }

        uint32_t mode_stride = (ctx->frame_width + 3u) / 4u;
        uint32_t mode_height = (ctx->frame_height + 3u) / 4u;
        size_t mode_total = (size_t)mode_stride * mode_height;
        ctx->mode_map = (uint8_t *)malloc(mode_total);
        if (!ctx->mode_map) {
            set_err(err, errcap, "out of memory");
            return 0;
        }
        ctx->mode_stride = mode_stride;
        memset(ctx->mode_map, 1, mode_total);

        ctx->map_stride = mode_stride;
        ctx->map_height = mode_height;
        ctx->qp_map = (uint8_t *)malloc(mode_total);
        ctx->residual_map = (uint8_t *)malloc(mode_total);
        if (!ctx->qp_map || !ctx->residual_map) {
            set_err(err, errcap, "out of memory");
            return 0;
        }
        memset(ctx->qp_map, (uint8_t)hevc_clamp_int(slice_qp, 0, 51), mode_total);
        memset(ctx->residual_map, 0, mode_total);
    }

    ctx->slice_qp = slice_qp;
    if (ctx->map_stride == 0 || ctx->map_height == 0) {
        ctx->map_stride = (ctx->frame_width + 3u) / 4u;
        ctx->map_height = (ctx->frame_height + 3u) / 4u;
    }
    size_t map_total = (size_t)ctx->map_stride * ctx->map_height;
    if ((!ctx->qp_map || !ctx->residual_map) && map_total) {
        free(ctx->qp_map);
        free(ctx->residual_map);
        ctx->qp_map = (uint8_t *)malloc(map_total);
        ctx->residual_map = (uint8_t *)malloc(map_total);
        if (!ctx->qp_map || !ctx->residual_map) {
            set_err(err, errcap, "out of memory");
            return 0;
        }
    }
    if (ctx->qp_map && map_total) {
        memset(ctx->qp_map, (uint8_t)hevc_clamp_int(slice_qp, 0, 51), map_total);
    }
    if (ctx->residual_map && map_total) {
        memset(ctx->residual_map, 0, map_total);
    }
    if (sps->chroma_format_idc != 0) {
        uint32_t sub_w = 1;
        uint32_t sub_h = 1;
        if (sps->chroma_format_idc == 1) {
            sub_w = 2;
            sub_h = 2;
        } else if (sps->chroma_format_idc == 2) {
            sub_w = 2;
            sub_h = 1;
        }
        int min_tu = 1 << ((int)sps->log2_min_luma_transform_block_size_minus2 + 2);
        if (min_tu < 4) {
            min_tu = 4;
        }
        ctx->chroma_root_w = (uint32_t)min_tu * sub_w;
        ctx->chroma_root_h = (uint32_t)min_tu * sub_h;
        if (ctx->chroma_root_w == 0 || ctx->chroma_root_h == 0) {
            ctx->chroma_root_w = 0;
            ctx->chroma_root_h = 0;
        }
        if (ctx->chroma_root_w && ctx->chroma_root_h) {
            uint32_t stride = (ctx->frame_width + ctx->chroma_root_w - 1u) / ctx->chroma_root_w;
            uint32_t height = (ctx->frame_height + ctx->chroma_root_h - 1u) / ctx->chroma_root_h;
            size_t chroma_total = (size_t)stride * height;
            if (!ctx->chroma_cbf_cb_map || !ctx->chroma_cbf_cr_map ||
                ctx->chroma_cbf_stride != stride || ctx->chroma_cbf_height != height) {
                free(ctx->chroma_cbf_cb_map);
                free(ctx->chroma_cbf_cr_map);
                ctx->chroma_cbf_cb_map = (uint8_t *)malloc(chroma_total);
                ctx->chroma_cbf_cr_map = (uint8_t *)malloc(chroma_total);
                if (!ctx->chroma_cbf_cb_map || !ctx->chroma_cbf_cr_map) {
                    set_err(err, errcap, "out of memory");
                    return 0;
                }
                ctx->chroma_cbf_stride = stride;
                ctx->chroma_cbf_height = height;
            }
            if (ctx->chroma_cbf_cb_map && ctx->chroma_cbf_cr_map) {
                memset(ctx->chroma_cbf_cb_map, 0, chroma_total);
                memset(ctx->chroma_cbf_cr_map, 0, chroma_total);
            }
        }
    } else {
        ctx->chroma_root_w = 0;
        ctx->chroma_root_h = 0;
        ctx->chroma_cbf_stride = 0;
        ctx->chroma_cbf_height = 0;
        free(ctx->chroma_cbf_cb_map);
        free(ctx->chroma_cbf_cr_map);
        ctx->chroma_cbf_cb_map = NULL;
        ctx->chroma_cbf_cr_map = NULL;
    }
    uint32_t ctu = sps->ctu_size;
    if (ctu < 8) {
        ctu = 8;
    }
    if (!ctx->sao_map &&
        (slice_sao_luma_flag || slice_sao_chroma_flag) &&
        sps->pic_width_in_ctus > 0 && sps->pic_height_in_ctus > 0) {
        ctx->sao_stride = sps->pic_width_in_ctus;
        ctx->sao_height = sps->pic_height_in_ctus;
        size_t sao_total = (size_t)ctx->sao_stride * ctx->sao_height;
        ctx->sao_map = (hevc_sao_ctu *)calloc(sao_total, sizeof(hevc_sao_ctu));
        if (!ctx->sao_map) {
            set_err(err, errcap, "out of memory");
            return 0;
        }
    } else if (ctx->sao_map) {
        ctx->sao_stride = sps->pic_width_in_ctus;
        ctx->sao_height = sps->pic_height_in_ctus;
    }
    uint64_t num_ctus = (uint64_t)sps->pic_width_in_ctus * (uint64_t)sps->pic_height_in_ctus;
    if (slice_address >= num_ctus) {
        set_err(err, errcap, "invalid HEVC slice address");
        return 0;
    }
    int log2_min_cb = 3 + (int)sps->log2_min_luma_coding_block_size_minus3;
    int log2_ctu = log2_min_cb + (int)sps->log2_diff_max_min_luma_coding_block_size;
    int qp_delta_log2 = log2_ctu;
    if (!pps || !pps->cu_qp_delta_enabled_flag) {
        qp_delta_log2 = -1;
    } else if ((int)pps->diff_cu_qp_delta_depth <= log2_ctu) {
        qp_delta_log2 = log2_ctu - (int)pps->diff_cu_qp_delta_depth;
    }
    if (pps && pps->cu_qp_delta_enabled_flag && qp_delta_log2 >= 0) {
        uint32_t block_size = 1u << qp_delta_log2;
        if (block_size == 0) {
            block_size = 1;
        }
        ctx->qp_block_size = block_size;
        uint32_t stride = (ctx->frame_width + block_size - 1u) / block_size;
        uint32_t height = (ctx->frame_height + block_size - 1u) / block_size;
        size_t total = (size_t)stride * height;
        if (!ctx->qp_delta_coded_map || ctx->qp_delta_stride != stride ||
            ctx->qp_delta_height != height) {
            free(ctx->qp_delta_coded_map);
            ctx->qp_delta_coded_map = (uint8_t *)malloc(total);
            if (!ctx->qp_delta_coded_map) {
                set_err(err, errcap, "out of memory");
                return 0;
            }
            ctx->qp_delta_stride = stride;
            ctx->qp_delta_height = height;
        }
        if (ctx->qp_delta_coded_map) {
            memset(ctx->qp_delta_coded_map, 0, total);
        }
    } else {
        ctx->qp_block_size = 0;
        ctx->qp_delta_stride = 0;
        ctx->qp_delta_height = 0;
        free(ctx->qp_delta_coded_map);
        ctx->qp_delta_coded_map = NULL;
    }

    int cur_qp = slice_qp;
    int want_tiles = pps && pps->tiles_enabled_flag;
    int want_wpp = pps && pps->entropy_coding_sync_enabled_flag;
    int use_tiles = want_tiles;
    int use_wpp = want_wpp;
    int allow_entry_offsets = 0;
    const char *entry_env = getenv("CUPIDIMAGE_HEIF_ENTRY_OFFSETS");
    if (entry_env) {
        if (strcmp(entry_env, "force") == 0 || strcmp(entry_env, "1") == 0 ||
            strcmp(entry_env, "on") == 0) {
            allow_entry_offsets = 1;
        } else if (strcmp(entry_env, "0") == 0 || strcmp(entry_env, "none") == 0) {
            allow_entry_offsets = 0;
        }
    }
    if (!allow_entry_offsets) {
        use_tiles = 0;
        use_wpp = 0;
    }
    uint32_t *col_bd = NULL;
    uint32_t *row_bd = NULL;
    if (use_tiles) {
        if (!hevc_compute_tile_bounds(sps, pps, &col_bd, &row_bd, err, errcap)) {
            return 0;
        }
    }
    if (slice_address != 0) {
        use_tiles = 0;
        use_wpp = 0;
    }
    const uint8_t *base = bs->data + bs->pos;
    size_t base_size = bs->size - bs->pos;
    if (use_tiles || use_wpp) {
        uint64_t substreams = hevc_calc_substream_count(sps, pps, use_tiles, use_wpp, row_bd);
        uint64_t expected_offsets = substreams > 0 ? (substreams - 1u) : 0;
        if (heif_debug_enabled()) {
            heif_debugf("hevc: entry_offsets=%u expected=%llu base=%zu tiles=%d wpp=%d\n",
                        num_entry_offsets, (unsigned long long)expected_offsets,
                        base_size, use_tiles, use_wpp);
            if (num_entry_offsets) {
                uint32_t dump = num_entry_offsets < 8u ? num_entry_offsets : 8u;
                heif_debugf("hevc: entry_offsets first %u:", dump);
                for (uint32_t i = 0; i < dump; i++) {
                    heif_debugf(" %u", entry_offsets[i]);
                }
                heif_debugf(" ... last %u\n", entry_offsets[num_entry_offsets - 1u]);
            }
        }
        if (num_entry_offsets != expected_offsets) {
            if (use_tiles && use_wpp) {
                uint64_t tiles_only = hevc_calc_substream_count(sps, pps, 1, 0, row_bd);
                uint64_t tiles_only_expected = tiles_only > 0 ? (tiles_only - 1u) : 0;
                if (num_entry_offsets == tiles_only_expected) {
                    use_wpp = 0;
                } else {
                    uint64_t wpp_only = hevc_calc_substream_count(sps, pps, 0, 1, NULL);
                    uint64_t wpp_only_expected = wpp_only > 0 ? (wpp_only - 1u) : 0;
                    if (num_entry_offsets == wpp_only_expected) {
                        use_tiles = 0;
                    } else {
                        use_tiles = 0;
                        use_wpp = 0;
                    }
                }
            } else {
                use_tiles = 0;
                use_wpp = 0;
            }
        }
        if ((use_tiles || use_wpp) && num_entry_offsets > 0 &&
            !hevc_validate_entry_offsets(entry_offsets, num_entry_offsets, base_size)) {
            heif_debugf("hevc: entry offsets invalid, falling back\n");
            use_tiles = 0;
            use_wpp = 0;
        }
    }
    if (use_tiles || use_wpp) {
        uint64_t sub_idx = 0;
        int end_slice = 0;
        if (use_tiles) {
            uint32_t num_cols = pps->num_tile_columns;
            uint32_t num_rows = pps->num_tile_rows;
            for (uint32_t tr = 0; tr < num_rows && !end_slice; tr++) {
                uint32_t y0_ctu = row_bd[tr];
                uint32_t tile_h = row_bd[tr + 1u] - y0_ctu;
                for (uint32_t tc = 0; tc < num_cols && !end_slice; tc++) {
                    uint32_t x0_ctu = col_bd[tc];
                    uint32_t tile_w = col_bd[tc + 1u] - x0_ctu;
                    hevc_cabac_state prev_row_ctx;
                    int prev_row_valid = 0;
                    if (use_wpp) {
                        for (uint32_t row = 0; row < tile_h; row++) {
                            size_t start = (sub_idx == 0) ? 0 : (size_t)entry_offsets[sub_idx - 1u];
                            size_t end = (sub_idx < num_entry_offsets) ?
                                (size_t)entry_offsets[sub_idx] : base_size;
                            hevc_bitstream cur_bs;
                            memset(&cur_bs, 0, sizeof(cur_bs));
                            cur_bs.data = base;
                            cur_bs.size = end;
                            cur_bs.pos = start;
                            int rc = hevc_decode_substream(ctx, sps, pps, &cur_bs,
                                                           slice_type, slice_qp,
                                                           slice_sao_luma_flag, slice_sao_chroma_flag,
                                                           x0_ctu, y0_ctu + row,
                                                           tile_w, 1u,
                                                           qp_delta_log2, &cur_qp,
                                                           &prev_row_ctx, &prev_row_valid, 1,
                                                           err, errcap);
                            sub_idx++;
                            if (rc == 0) {
                                free(col_bd);
                                free(row_bd);
                                return 0;
                            }
                            if (rc == 2) {
                                end_slice = 1;
                                break;
                            }
                        }
                    } else {
                        size_t start = (sub_idx == 0) ? 0 : (size_t)entry_offsets[sub_idx - 1u];
                        size_t end = (sub_idx < num_entry_offsets) ?
                            (size_t)entry_offsets[sub_idx] : base_size;
                        hevc_bitstream cur_bs;
                        memset(&cur_bs, 0, sizeof(cur_bs));
                        cur_bs.data = base;
                        cur_bs.size = end;
                        cur_bs.pos = start;
                        int rc = hevc_decode_substream(ctx, sps, pps, &cur_bs,
                                                       slice_type, slice_qp,
                                                       slice_sao_luma_flag, slice_sao_chroma_flag,
                                                       x0_ctu, y0_ctu,
                                                       tile_w, tile_h,
                                                       qp_delta_log2, &cur_qp,
                                                       NULL, NULL, 0,
                                                       err, errcap);
                        sub_idx++;
                        if (rc == 0) {
                            free(col_bd);
                            free(row_bd);
                            return 0;
                        }
                        if (rc == 2) {
                            end_slice = 1;
                        }
                    }
                }
            }
        } else {
            hevc_cabac_state prev_row_ctx;
            int prev_row_valid = 0;
            for (uint32_t row = 0; row < sps->pic_height_in_ctus; row++) {
                size_t start = (sub_idx == 0) ? 0 : (size_t)entry_offsets[sub_idx - 1u];
                size_t end = (sub_idx < num_entry_offsets) ?
                    (size_t)entry_offsets[sub_idx] : base_size;
                hevc_bitstream cur_bs;
                memset(&cur_bs, 0, sizeof(cur_bs));
                cur_bs.data = base;
                cur_bs.size = end;
                cur_bs.pos = start;
                int rc = hevc_decode_substream(ctx, sps, pps, &cur_bs,
                                               slice_type, slice_qp,
                                               slice_sao_luma_flag, slice_sao_chroma_flag,
                                               0u, row,
                                               sps->pic_width_in_ctus, 1u,
                                               qp_delta_log2, &cur_qp,
                                               &prev_row_ctx, &prev_row_valid, 1,
                                               err, errcap);
                sub_idx++;
                if (rc == 0) {
                    free(col_bd);
                    free(row_bd);
                    return 0;
                }
                if (rc == 2) {
                    break;
                }
            }
        }
        free(col_bd);
        free(row_bd);
        return 1;
    }
    free(col_bd);
    free(row_bd);
    hevc_cabac cabac;
    hevc_cabac_state st;
    if (!hevc_cabac_init(&cabac, bs)) {
        set_err(err, errcap, "HEVC CABAC init failed");
        return 0;
    }
    hevc_init_cabac_state(&st, slice_type, slice_qp);
    for (uint64_t addr = slice_address; addr < num_ctus; addr++) {
        if (cabac.bs->pos >= cabac.bs->size && cabac.bs->bitcount < 16) {
            heif_debugf("Bitstream exhausted at CTU %llu/%llu, ending slice early\n",
                       (unsigned long long)addr, (unsigned long long)num_ctus);
            break;
        }
        uint32_t cx = (uint32_t)(addr % sps->pic_width_in_ctus);
        uint32_t cy = (uint32_t)(addr / sps->pic_width_in_ctus);
        uint32_t x = cx * ctu;
        uint32_t y = cy * ctu;
        if ((slice_sao_luma_flag || slice_sao_chroma_flag) &&
            !hevc_decode_sao_ctu(ctx, &cabac, &st, sps,
                                 slice_sao_luma_flag, slice_sao_chroma_flag, cx, cy)) {
            set_err(err, errcap, "HEVC SAO decode failed");
            return 0;
        }
        int cu_qp_delta_coded = 0;
        if (!hevc_decode_cu(ctx, &cabac, &st, x, y, ctu, 0, sps->bit_depth_luma,
                            &cur_qp, &cu_qp_delta_coded, qp_delta_log2, pps, err, errcap)) {
            return 0;
        }
        uint32_t end_flag = 0;
        if (!hevc_cabac_decode_terminate(&cabac, &end_flag)) {
            set_err(err, errcap, "HEVC slice end decode failed");
            return 0;
        }
        if (end_flag) {
            break;
        }
    }
    return 1;
}

static void hevc_traverse_ctus(const hevc_sps *sps) {
    if (!sps || sps->ctu_size == 0 || sps->pic_width_in_ctus == 0 || sps->pic_height_in_ctus == 0) {
        return;
    }
    for (uint32_t cy = 0; cy < sps->pic_height_in_ctus; cy++) {
        for (uint32_t cx = 0; cx < sps->pic_width_in_ctus; cx++) {
            hevc_traverse_tus(cx * sps->ctu_size, cy * sps->ctu_size, sps->ctu_size);
        }
    }
}

static size_t hevc_find_ebsp_pos(const size_t *ebsp_to_rbsp, size_t ebsp_size, size_t rbsp_pos) {
    if (!ebsp_to_rbsp) {
        return rbsp_pos;
    }
    size_t last = ebsp_size;
    for (size_t i = 0; i <= ebsp_size; i++) {
        if (ebsp_to_rbsp[i] == rbsp_pos) {
            last = i;
        } else if (ebsp_to_rbsp[i] > rbsp_pos) {
            break;
        }
    }
    return last;
}

static int hevc_parse_slice_header(const uint8_t *rbsp, size_t rbsp_size,
                                   const size_t *ebsp_to_rbsp, size_t ebsp_size,
                                   hevc_context *ctx, int nal_type,
                                   char *err, size_t errcap) {
    hevc_bitstream bs;
    memset(&bs, 0, sizeof(bs));
    bs.data = rbsp;
    bs.size = rbsp_size;
    uint32_t *entry_offsets = NULL;
    uint32_t num_entry_point_offsets = 0;
    int ok = 0;
    uint32_t first_slice_segment_in_pic_flag = 0;
    if (!hevc_bs_read_bit(&bs, &first_slice_segment_in_pic_flag)) {
        set_err(err, errcap, "invalid HEVC slice header");
        return 0;
    }
    if (nal_type >= 16 && nal_type <= 21) {
        uint32_t no_output_of_prior_pics_flag = 0;
        if (!hevc_bs_read_bit(&bs, &no_output_of_prior_pics_flag)) {
            set_err(err, errcap, "invalid HEVC slice header");
            return 0;
        }
        (void)no_output_of_prior_pics_flag;
    }
    uint32_t pps_id = 0;
    if (!hevc_bs_read_ue(&bs, &pps_id)) {
        set_err(err, errcap, "invalid HEVC slice header");
        return 0;
    }
    if (pps_id >= HEVC_MAX_PPS || !ctx->pps[pps_id].valid) {
        set_err(err, errcap, "missing HEVC PPS");
        return 0;
    }
    const hevc_pps *pps = &ctx->pps[pps_id];
    ctx->sign_data_hiding_enabled_flag = pps->sign_data_hiding_enabled_flag;
    if (pps->sps_id >= HEVC_MAX_SPS || !ctx->sps[pps->sps_id].valid) {
        set_err(err, errcap, "missing HEVC SPS");
        return 0;
    }
    const hevc_sps *sps = &ctx->sps[pps->sps_id];
    uint32_t dependent_slice_segment_flag = 0;
    if (!first_slice_segment_in_pic_flag && pps->dependent_slice_segments_enabled_flag) {
        if (!hevc_bs_read_bit(&bs, &dependent_slice_segment_flag)) {
            set_err(err, errcap, "invalid HEVC slice header");
            return 0;
        }
    }
    if (dependent_slice_segment_flag) {
        set_err(err, errcap, "unsupported HEVC dependent slice");
        return 0;
    }
    uint32_t slice_segment_address = 0;
    if (!first_slice_segment_in_pic_flag) {
        uint64_t num_ctus = (uint64_t)sps->pic_width_in_ctus * (uint64_t)sps->pic_height_in_ctus;
        uint32_t addr_bits = hevc_ceil_log2_u64(num_ctus);
        if (addr_bits > 0) {
            if (!hevc_bs_read_bits(&bs, (int)addr_bits, &slice_segment_address)) {
                set_err(err, errcap, "invalid HEVC slice header");
                return 0;
            }
        }
    }
    uint32_t slice_type = 0;
    if (!hevc_bs_skip_bits(&bs, (int)pps->num_extra_slice_header_bits)) {
        set_err(err, errcap, "invalid HEVC slice header");
        return 0;
    }
    if (!hevc_bs_read_ue(&bs, &slice_type)) {
        set_err(err, errcap, "invalid HEVC slice header");
        return 0;
    }
    ctx->last_slice_type = (int)slice_type;
    if (pps->output_flag_present_flag) {
        uint32_t pic_output_flag = 0;
        if (!hevc_bs_read_bit(&bs, &pic_output_flag)) {
            set_err(err, errcap, "invalid HEVC slice header");
            return 0;
        }
        (void)pic_output_flag;
    }
    if (sps->separate_colour_plane_flag) {
        uint32_t colour_plane_id = 0;
        if (!hevc_bs_read_bits(&bs, 2, &colour_plane_id)) {
            set_err(err, errcap, "invalid HEVC slice header");
            return 0;
        }
        (void)colour_plane_id;
    }
    int is_idr = (nal_type == 19 || nal_type == 20);
    if (!is_idr) {
        int poc_bits = (int)sps->log2_max_pic_order_cnt_lsb_minus4 + 4;
        uint32_t slice_pic_order_cnt_lsb = 0;
        if (poc_bits > 0) {
            if (!hevc_bs_read_bits(&bs, poc_bits, &slice_pic_order_cnt_lsb)) {
                set_err(err, errcap, "invalid HEVC slice header");
                return 0;
            }
        }
        (void)slice_pic_order_cnt_lsb;
        if (sps->num_short_term_ref_pic_sets > 0) {
            uint32_t short_term_ref_pic_set_sps_flag = 0;
            if (!hevc_bs_read_bit(&bs, &short_term_ref_pic_set_sps_flag)) {
                set_err(err, errcap, "invalid HEVC slice header");
                return 0;
            }
            if (short_term_ref_pic_set_sps_flag) {
                if (sps->num_short_term_ref_pic_sets > 1) {
                    uint32_t idx_bits = hevc_ceil_log2_u64(sps->num_short_term_ref_pic_sets);
                    uint32_t st_rps_idx = 0;
                    if (idx_bits > 0 &&
                        !hevc_bs_read_bits(&bs, (int)idx_bits, &st_rps_idx)) {
                        set_err(err, errcap, "invalid HEVC slice header");
                        return 0;
                    }
                }
            } else {
                uint8_t tmp_delta_pocs[HEVC_MAX_SHORT_TERM_RPS + 1];
                memset(tmp_delta_pocs, 0, sizeof(tmp_delta_pocs));
                for (uint32_t i = 0; i < sps->num_short_term_ref_pic_sets; i++) {
                    tmp_delta_pocs[i] = sps->num_delta_pocs[i];
                }
                if (!hevc_skip_short_term_ref_pic_set(&bs,
                                                      (int)sps->num_short_term_ref_pic_sets,
                                                      (int)sps->num_short_term_ref_pic_sets,
                                                      tmp_delta_pocs)) {
                    set_err(err, errcap, "invalid HEVC slice header");
                    return 0;
                }
            }
        }
        if (sps->long_term_ref_pics_present_flag) {
            uint32_t num_long_term_sps = 0;
            if (!hevc_bs_read_ue(&bs, &num_long_term_sps)) {
                set_err(err, errcap, "invalid HEVC slice header");
                return 0;
            }
            int poc_bits = (int)sps->log2_max_pic_order_cnt_lsb_minus4 + 4;
            for (uint32_t i = 0; i < num_long_term_sps; i++) {
                uint32_t tmp = 0;
                if (!hevc_bs_read_bits(&bs, poc_bits, &tmp) ||
                    !hevc_bs_read_bit(&bs, &tmp)) {
                    set_err(err, errcap, "invalid HEVC slice header");
                    return 0;
                }
            }
        }
    }
    if (sps->sps_temporal_mvp_enabled_flag && slice_type != 2) {
        uint32_t slice_temporal_mvp_enabled_flag = 0;
        if (!hevc_bs_read_bit(&bs, &slice_temporal_mvp_enabled_flag)) {
            set_err(err, errcap, "invalid HEVC slice header");
            return 0;
        }
        (void)slice_temporal_mvp_enabled_flag;
    }
    uint32_t slice_sao_luma_flag = 0;
    uint32_t slice_sao_chroma_flag = 0;
    if (sps->sample_adaptive_offset_enabled_flag) {
        if (!hevc_bs_read_bit(&bs, &slice_sao_luma_flag) ||
            !hevc_bs_read_bit(&bs, &slice_sao_chroma_flag)) {
            set_err(err, errcap, "invalid HEVC slice header");
            return 0;
        }
    }
    if (slice_type != 2) {
        set_err(err, errcap, "unsupported HEVC slice type");
        return 0;
    }
    if (pps->cabac_init_present_flag && slice_type != 2) {
        uint32_t cabac_init_flag = 0;
        if (!hevc_bs_read_bit(&bs, &cabac_init_flag)) {
            set_err(err, errcap, "invalid HEVC slice header");
            return 0;
        }
    }
    int32_t slice_qp_delta = 0;
    if (!hevc_bs_read_se(&bs, &slice_qp_delta)) {
        set_err(err, errcap, "invalid HEVC slice header");
        return 0;
    }
    int slice_qp = 26 + (int)pps->init_qp_minus26 + (int)slice_qp_delta;
    heif_debugf("hevc: slice addr=%u type=%u qp=%d\n",
                slice_segment_address, slice_type, slice_qp);
    ctx->slice_cb_qp_offset = 0;
    ctx->slice_cr_qp_offset = 0;
    if (pps->pps_slice_chroma_qp_offsets_present_flag) {
        int32_t cb_off = 0;
        int32_t cr_off = 0;
        if (!hevc_bs_read_se(&bs, &cb_off) ||
            !hevc_bs_read_se(&bs, &cr_off)) {
            set_err(err, errcap, "invalid HEVC slice header");
            return 0;
        }
        ctx->slice_cb_qp_offset = (int)cb_off;
        ctx->slice_cr_qp_offset = (int)cr_off;
    }
    uint32_t slice_deblocking_filter_disabled_flag = 0;
    int beta_offset_div2 = 0;
    int tc_offset_div2 = 0;
    if (pps->deblocking_filter_control_present_flag) {
        uint32_t deblocking_filter_override_flag = 0;
        if (pps->deblocking_filter_override_enabled_flag) {
            if (!hevc_bs_read_bit(&bs, &deblocking_filter_override_flag)) {
                set_err(err, errcap, "invalid HEVC slice header");
                return 0;
            }
        }
        if (deblocking_filter_override_flag || pps->pps_deblocking_filter_disabled_flag) {
            if (!hevc_bs_read_bit(&bs, &slice_deblocking_filter_disabled_flag)) {
                set_err(err, errcap, "invalid HEVC slice header");
                return 0;
            }
            if (!slice_deblocking_filter_disabled_flag) {
                if (!hevc_bs_read_se(&bs, &beta_offset_div2) ||
                    !hevc_bs_read_se(&bs, &tc_offset_div2)) {
                    set_err(err, errcap, "invalid HEVC slice header");
                    return 0;
                }
            }
        }
    }
    if (pps->pps_loop_filter_across_slices_enabled_flag &&
        (slice_sao_luma_flag || slice_sao_chroma_flag || !slice_deblocking_filter_disabled_flag)) {
        uint32_t slice_loop_filter_across_slices_enabled_flag = 0;
        if (!hevc_bs_read_bit(&bs, &slice_loop_filter_across_slices_enabled_flag)) {
            set_err(err, errcap, "invalid HEVC slice header");
            return 0;
        }
        (void)slice_loop_filter_across_slices_enabled_flag;
    }
    if (pps->tiles_enabled_flag || pps->entropy_coding_sync_enabled_flag) {
        if (!hevc_bs_read_ue(&bs, &num_entry_point_offsets)) {
            set_err(err, errcap, "invalid HEVC slice header");
            return 0;
        }
        if (num_entry_point_offsets > 0) {
            uint32_t offset_len_minus1 = 0;
            if (!hevc_bs_read_ue(&bs, &offset_len_minus1)) {
                set_err(err, errcap, "invalid HEVC slice header");
                return 0;
            }
            uint32_t offset_len = offset_len_minus1 + 1;
            if (offset_len > 32) {
                set_err(err, errcap, "invalid HEVC slice header");
                return 0;
            }
            entry_offsets = (uint32_t *)malloc((size_t)num_entry_point_offsets * sizeof(uint32_t));
            if (!entry_offsets) {
                set_err(err, errcap, "out of memory");
                return 0;
            }
            uint32_t cumulative = 0;
            for (uint32_t i = 0; i < num_entry_point_offsets; i++) {
                uint32_t tmp = 0;
                if (!hevc_bs_read_bits(&bs, (int)offset_len, &tmp)) {
                    set_err(err, errcap, "invalid HEVC slice header");
                    goto cleanup;
                }
                uint32_t delta = tmp + 1u;
                cumulative += delta;
                entry_offsets[i] = cumulative;
            }
        }
    }
    if (pps->slice_segment_header_extension_present_flag) {
        uint32_t ext_len = 0;
        if (!hevc_bs_read_ue(&bs, &ext_len)) {
            set_err(err, errcap, "invalid HEVC slice header");
            goto cleanup;
        }
        if (ext_len > 0) {
            if (!hevc_bs_skip_bits(&bs, (int)ext_len * 8)) {
                set_err(err, errcap, "invalid HEVC slice header");
                goto cleanup;
            }
        }
    }
    if (!hevc_bs_align(&bs)) {
        set_err(err, errcap, "invalid HEVC slice header");
        goto cleanup;
    }
    if (entry_offsets && num_entry_point_offsets > 0 &&
        ebsp_to_rbsp && ebsp_size > 0) {
        size_t rbsp_start = bs.pos;
        size_t base_size = rbsp_size > rbsp_start ? (rbsp_size - rbsp_start) : 0;
        if (entry_offsets[num_entry_point_offsets - 1u] > base_size) {
            size_t ebsp_start = hevc_find_ebsp_pos(ebsp_to_rbsp, ebsp_size, rbsp_start);
            for (uint32_t i = 0; i < num_entry_point_offsets; i++) {
                size_t ebsp_off = ebsp_start + (size_t)entry_offsets[i];
                if (ebsp_off > ebsp_size) {
                    ebsp_off = ebsp_size;
                }
                size_t rbsp_off = ebsp_to_rbsp[ebsp_off];
                if (rbsp_off < rbsp_start) {
                    rbsp_off = rbsp_start;
                }
                entry_offsets[i] = (uint32_t)(rbsp_off - rbsp_start);
            }
        }
    }
    if (!hevc_decode_intra_slice_data(ctx, sps, pps, &bs, slice_segment_address,
                                      (int)slice_type, slice_qp,
                                      (int)slice_sao_luma_flag, (int)slice_sao_chroma_flag,
                                      entry_offsets, num_entry_point_offsets,
                                      err, errcap)) {
        goto cleanup;
    }
    if (!slice_deblocking_filter_disabled_flag) {
        hevc_deblock_frame(ctx, sps, pps, slice_qp, beta_offset_div2, tc_offset_div2);
    }
    if (slice_sao_luma_flag || slice_sao_chroma_flag) {
        hevc_apply_sao(ctx, sps, (int)slice_sao_luma_flag, (int)slice_sao_chroma_flag);
    }
    hevc_traverse_ctus(sps);
    ok = 1;
cleanup:
    free(entry_offsets);
    return ok;
}

static int hevc_parse_nal_header(const uint8_t *nal, size_t nal_size,
                                 int *nal_type, int *temporal_id) {
    if (nal_size < 2) {
        return 0;
    }
    uint16_t hdr = (uint16_t)((uint16_t)nal[0] << 8) | nal[1];
    if (hdr & 0x8000u) {
        return 0;
    }
    int type = (hdr >> 9) & 0x3F;
    int tid = hdr & 0x7;
    if (tid == 0) {
        return 0;
    }
    if (nal_type) {
        *nal_type = type;
    }
    if (temporal_id) {
        *temporal_id = tid;
    }
    return 1;
}

static int hevc_nal_to_rbsp(const uint8_t *nal, size_t nal_size,
                            uint8_t **rbsp_out, size_t *rbsp_size,
                            size_t **ebsp_to_rbsp_out, size_t *ebsp_size_out) {
    if (nal_size < 2) {
        return 0;
    }
    size_t payload_size = nal_size - 2;
    size_t max_size = payload_size;
    uint8_t *rbsp = NULL;
    if (max_size > 0) {
        rbsp = (uint8_t *)malloc(max_size);
        if (!rbsp) {
            return 0;
        }
    }
    size_t *ebsp_to_rbsp = NULL;
    if (ebsp_to_rbsp_out) {
        ebsp_to_rbsp = (size_t *)malloc((payload_size + 1u) * sizeof(size_t));
        if (!ebsp_to_rbsp) {
            free(rbsp);
            return 0;
        }
    }
    size_t src = 2;
    size_t dst = 0;
    size_t ebsp_pos = 0;
    int zero_count = 0;
    while (src < nal_size) {
        if (ebsp_to_rbsp) {
            ebsp_to_rbsp[ebsp_pos] = dst;
        }
        uint8_t b = nal[src++];
        ebsp_pos++;
        if (zero_count == 2 && b == 0x03) {
            zero_count = 0;
            continue;
        }
        rbsp[dst++] = b;
        if (b == 0x00) {
            zero_count++;
        } else {
            zero_count = 0;
        }
    }
    if (ebsp_to_rbsp) {
        ebsp_to_rbsp[payload_size] = dst;
    }
    *rbsp_out = rbsp;
    *rbsp_size = dst;
    if (ebsp_to_rbsp_out) {
        *ebsp_to_rbsp_out = ebsp_to_rbsp;
    } else {
        free(ebsp_to_rbsp);
    }
    if (ebsp_size_out) {
        *ebsp_size_out = payload_size;
    }
    return 1;
}

static int hevc_has_annexb_start_code(const uint8_t *data, size_t size) {
    size_t limit = size < 64 ? size : 64;
    for (size_t i = 0; i + 3 <= limit; i++) {
        if (data[i] == 0x00 && data[i + 1] == 0x00 &&
            (data[i + 2] == 0x01 || (i + 3 < limit && data[i + 2] == 0x00 && data[i + 3] == 0x01))) {
            return 1;
        }
    }
    return 0;
}

static int hevc_next_annexb_nal(const uint8_t *data, size_t size, size_t *offset,
                                const uint8_t **nal, size_t *nal_size) {
    size_t pos = *offset;
    while (pos + 3 <= size) {
        if (data[pos] == 0x00 && data[pos + 1] == 0x00 &&
            (data[pos + 2] == 0x01 || (pos + 3 <= size && data[pos + 2] == 0x00 && data[pos + 3] == 0x01))) {
            break;
        }
        pos++;
    }
    if (pos + 3 > size) {
        return 0;
    }
    size_t start = pos + (data[pos + 2] == 0x01 ? 3u : 4u);
    pos = start;
    size_t next = pos;
    while (next + 3 <= size) {
        if (data[next] == 0x00 && data[next + 1] == 0x00 &&
            (data[next + 2] == 0x01 || (next + 3 <= size && data[next + 2] == 0x00 && data[next + 3] == 0x01))) {
            break;
        }
        next++;
    }
    size_t end = (next + 3 <= size) ? next : size;
    *offset = end;
    *nal = data + start;
    *nal_size = (end > start) ? (end - start) : 0;
    return 1;
}

static uint32_t hevc_read_length(const uint8_t *data, int len_size) {
    uint32_t val = 0;
    for (int i = 0; i < len_size; i++) {
        val = (val << 8) | data[i];
    }
    return val;
}

static int hevc_try_length_size(const uint8_t *data, size_t size, int len_size) {
    size_t pos = 0;
    int nal_count = 0;
    if (len_size <= 0 || len_size > 4) {
        return 0;
    }
    while (pos + (size_t)len_size <= size) {
        uint32_t nal_len = hevc_read_length(data + pos, len_size);
        pos += (size_t)len_size;
        if (nal_len == 0 || nal_len > size - pos) {
            return 0;
        }
        int nal_type = 0;
        if (!hevc_parse_nal_header(data + pos, nal_len, &nal_type, NULL)) {
            return 0;
        }
        pos += nal_len;
        nal_count++;
        if (nal_count > 8 && pos == size) {
            return 1;
        }
    }
    return pos == size;
}

static int hevc_detect_stream(const uint8_t *data, size_t size, int *is_annexb, int *len_size) {
    if (hevc_has_annexb_start_code(data, size)) {
        *is_annexb = 1;
        *len_size = 0;
        return 1;
    }
    int candidates[] = {4, 2, 1};
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        if (hevc_try_length_size(data, size, candidates[i])) {
            *is_annexb = 0;
            *len_size = candidates[i];
            return 1;
        }
    }
    return 0;
}

static int hevc_parse_nals(const uint8_t *data, size_t size, hevc_context *ctx,
                           char *err, size_t errcap) {
    int is_annexb = 0;
    int len_size = 0;
    if (ctx->length_size > 0) {
        is_annexb = 0;
        len_size = ctx->length_size;
    } else {
        if (!hevc_detect_stream(data, size, &is_annexb, &len_size)) {
            set_err(err, errcap, "unsupported HEVC stream format");
            return 0;
        }
    }
    size_t offset = 0;
    while (offset < size) {
        const uint8_t *nal = NULL;
        size_t nal_size = 0;
        if (is_annexb) {
            if (!hevc_next_annexb_nal(data, size, &offset, &nal, &nal_size)) {
                break;
            }
        } else {
            if (offset + (size_t)len_size > size) {
                break;
            }
            uint32_t nlen = hevc_read_length(data + offset, len_size);
            offset += (size_t)len_size;
            if (nlen == 0 || nlen > size - offset) {
                set_err(err, errcap, "invalid HEVC NAL length");
                return 0;
            }
            nal = data + offset;
            nal_size = nlen;
            offset += nlen;
        }
        if (!nal || nal_size < 2) {
            continue;
        }
        int nal_type = 0;
        if (!hevc_parse_nal_header(nal, nal_size, &nal_type, NULL)) {
            set_err(err, errcap, "invalid HEVC NAL header");
            return 0;
        }
        if (nal_type == 33 || nal_type == 34) {
            uint8_t *rbsp = NULL;
            size_t rbsp_size = 0;
            if (!hevc_nal_to_rbsp(nal, nal_size, &rbsp, &rbsp_size, NULL, NULL)) {
                set_err(err, errcap, "out of memory");
                return 0;
            }
            int ok = 1;
            if (nal_type == 33) {
                ok = hevc_parse_sps(rbsp, rbsp_size, ctx, err, errcap);
            } else if (nal_type == 34) {
                ok = hevc_parse_pps(rbsp, rbsp_size, ctx, err, errcap);
            }
            free(rbsp);
            if (!ok) {
                return 0;
            }
        } else if (nal_type >= 0 && nal_type <= 31) {
            uint8_t *rbsp = NULL;
            size_t rbsp_size = 0;
            size_t *ebsp_to_rbsp = NULL;
            size_t ebsp_size = 0;
            if (!hevc_nal_to_rbsp(nal, nal_size, &rbsp, &rbsp_size,
                                  &ebsp_to_rbsp, &ebsp_size)) {
                set_err(err, errcap, "out of memory");
                return 0;
            }
            int ok = hevc_parse_slice_header(rbsp, rbsp_size,
                                             ebsp_to_rbsp, ebsp_size,
                                             ctx, nal_type, err, errcap);
            free(ebsp_to_rbsp);
            free(rbsp);
            if (!ok) {
                return 0;
            }
        }
    }
    if (!ctx->have_sps) {
        set_err(err, errcap, "missing HEVC SPS");
        return 0;
    }
    return 1;
}

static void hevc_yuv_to_rgb(int y, int cb, int cr, int full_range, int matrix,
                            int bit_depth_y, int bit_depth_c,
                            uint8_t *r, uint8_t *g, uint8_t *b) {
    int max_y = (1 << bit_depth_y) - 1;
    int max_c = (1 << bit_depth_c) - 1;
    int shift_y = bit_depth_y - 8;
    int shift_c = bit_depth_c - 8;
    int yy = y;
    int cbv = 0;
    int crv = 0;
    if (full_range) {
        int mid_c = 1 << (bit_depth_c - 1);
        cbv = cb - mid_c;
        crv = cr - mid_c;
    } else {
        int y_min = 16 << shift_y;
        int y_range = 219 << shift_y;
        int cy = y - y_min;
        if (cy < 0) cy = 0;
        yy = (int)(((int64_t)cy * max_y + (y_range / 2)) / y_range);
        yy = hevc_clamp_int(yy, 0, max_y);
        int c_mid = 128 << shift_c;
        int c_range = 224 << shift_c;
        int c = cb - c_mid;
        int cabs = c < 0 ? -c : c;
        int cscaled = (int)(((int64_t)cabs * max_c + (c_range / 2)) / c_range);
        if (cscaled > max_c) cscaled = max_c;
        cbv = c < 0 ? -cscaled : cscaled;
        c = cr - c_mid;
        cabs = c < 0 ? -c : c;
        cscaled = (int)(((int64_t)cabs * max_c + (c_range / 2)) / c_range);
        if (cscaled > max_c) cscaled = max_c;
        crv = c < 0 ? -cscaled : cscaled;
    }
    if (bit_depth_c != bit_depth_y) {
        if (max_c > 0) {
            int64_t cb_scaled = (int64_t)cbv * max_y;
            int64_t cr_scaled = (int64_t)crv * max_y;
            if (cb_scaled >= 0) cb_scaled += max_c / 2;
            else cb_scaled -= max_c / 2;
            if (cr_scaled >= 0) cr_scaled += max_c / 2;
            else cr_scaled -= max_c / 2;
            cbv = (int)(cb_scaled / max_c);
            crv = (int)(cr_scaled / max_c);
        }
    }
    int r_cr = 91881;
    int g_cb = 22553;
    int g_cr = 46802;
    int b_cb = 116130;
    if (matrix == 1) {
        r_cr = 103206;
        g_cb = 12276;
        g_cr = 30679;
        b_cb = 121609;
    } else if (matrix == 9 || matrix == 10) {
        r_cr = 96639;
        g_cb = 10784;
        g_cr = 37444;
        b_cb = 123299;
    }
    int rr = yy + (int)(((int64_t)r_cr * crv) >> 16);
    int gg = yy - (int)(((int64_t)g_cb * cbv + (int64_t)g_cr * crv) >> 16);
    int bb = yy + (int)(((int64_t)b_cb * cbv) >> 16);
    rr = hevc_clamp_int(rr, 0, max_y);
    gg = hevc_clamp_int(gg, 0, max_y);
    bb = hevc_clamp_int(bb, 0, max_y);
    if (shift_y > 0) {
        int add = 1 << (shift_y - 1);
        rr = (rr + add) >> shift_y;
        gg = (gg + add) >> shift_y;
        bb = (bb + add) >> shift_y;
    }
    *r = hevc_clip_u8(rr);
    *g = hevc_clip_u8(gg);
    *b = hevc_clip_u8(bb);
}

static int hevc_output_to_image(hevc_context *ctx, const heif_colr *colr,
                                cupidimage_image *out,
                                char *err, size_t errcap) {
    if (!ctx->frame_luma || ctx->frame_width == 0 || ctx->frame_height == 0) {
        set_err(err, errcap, "HEVC decode produced no output");
        return 0;
    }
    size_t width = ctx->frame_width;
    size_t height = ctx->frame_height;
    if (width > UINT32_MAX || height > UINT32_MAX) {
        set_err(err, errcap, "HEVC frame size too large");
        return 0;
    }
    size_t pixels = width * height;
    if (pixels > SIZE_MAX / 4) {
        set_err(err, errcap, "HEVC frame too large");
        return 0;
    }
    uint8_t *rgba = (uint8_t *)malloc(pixels * 4);
    if (!rgba) {
        set_err(err, errcap, "out of memory");
        return 0;
    }
    int force_plane = 0; /* 0=normal, 1=Y, 2=Cb, 3=Cr */
    const char *plane_env = getenv("CUPIDIMAGE_HEIF_PLANE");
    if (plane_env && plane_env[0] != '\0') {
        if (strcmp(plane_env, "y") == 0 || strcmp(plane_env, "Y") == 0) {
            force_plane = 1;
        } else if (strcmp(plane_env, "cb") == 0 || strcmp(plane_env, "Cb") == 0 ||
                   strcmp(plane_env, "CB") == 0) {
            force_plane = 2;
        } else if (strcmp(plane_env, "cr") == 0 || strcmp(plane_env, "Cr") == 0 ||
                   strcmp(plane_env, "CR") == 0) {
            force_plane = 3;
        }
    }
    if (force_plane != 1 && ctx->frame_cb && ctx->frame_cr) {
        const hevc_sps *sps = &ctx->sps[ctx->last_sps_id];
        int full_range = sps->vui_video_full_range_flag ? 1 : 0;
        int matrix = (int)sps->vui_matrix_coefficients;
        if (colr && colr->has_nclx) {
            full_range = colr->full_range_flag ? 1 : 0;
            matrix = (int)colr->matrix_coefficients;
        }
        int swap_cbcr = 0;
        const char *swap_env = getenv("CUPIDIMAGE_HEIF_SWAP_CBCR");
        if (swap_env && swap_env[0] != '\0' && strcmp(swap_env, "0") != 0) {
            swap_cbcr = 1;
        }
        const char *stats_env = getenv("CUPIDIMAGE_HEIF_STATS");
        if (stats_env && stats_env[0] != '\0' && strcmp(stats_env, "0") != 0) {
            uint16_t y_min = 0xFFFF, y_max = 0;
            uint16_t cb_min = 0xFFFF, cb_max = 0;
            uint16_t cr_min = 0xFFFF, cr_max = 0;
            size_t y_total = (size_t)ctx->frame_stride * ctx->frame_height;
            size_t c_total = (size_t)ctx->chroma_stride * ctx->chroma_height;
            for (size_t i = 0; i < y_total; i++) {
                uint16_t v = ctx->frame_luma[i];
                if (v < y_min) y_min = v;
                if (v > y_max) y_max = v;
            }
            for (size_t i = 0; i < c_total; i++) {
                uint16_t v = ctx->frame_cb[i];
                if (v < cb_min) cb_min = v;
                if (v > cb_max) cb_max = v;
                v = ctx->frame_cr[i];
                if (v < cr_min) cr_min = v;
                if (v > cr_max) cr_max = v;
            }
            heif_debugf("hevc: plane ranges Y[%u..%u] Cb[%u..%u] Cr[%u..%u]\n",
                        y_min, y_max, cb_min, cb_max, cr_min, cr_max);
        }
        uint32_t sub_w = 1;
        uint32_t sub_h = 1;
        if (ctx->sps[ctx->last_sps_id].chroma_format_idc == 1) {
            sub_w = 2;
            sub_h = 2;
        } else if (ctx->sps[ctx->last_sps_id].chroma_format_idc == 2) {
            sub_w = 2;
            sub_h = 1;
        }
        static int sample_logged = 0;
        int bit_depth_y = (int)sps->bit_depth_luma;
        int bit_depth_c = (int)sps->bit_depth_chroma;
        int shift_y = bit_depth_y > 8 ? (bit_depth_y - 8) : 0;
        int shift_c = bit_depth_c > 8 ? (bit_depth_c - 8) : 0;
        int add_y = shift_y > 0 ? (1 << (shift_y - 1)) : 0;
        int add_c = shift_c > 0 ? (1 << (shift_c - 1)) : 0;
        for (uint32_t y = 0; y < ctx->frame_height; y++) {
            uint32_t cy = y / sub_h;
            for (uint32_t x = 0; x < ctx->frame_width; x++) {
                uint32_t cx = x / sub_w;
                uint16_t yy_raw = ctx->frame_luma[(size_t)y * ctx->frame_stride + x];
                uint16_t cb_raw = ctx->frame_cb[(size_t)cy * ctx->chroma_stride + cx];
                uint16_t cr_raw = ctx->frame_cr[(size_t)cy * ctx->chroma_stride + cx];
                if (swap_cbcr) {
                    uint16_t tmp = cb_raw;
                    cb_raw = cr_raw;
                    cr_raw = tmp;
                }
                int yy_dbg = shift_y > 0 ? (int)((yy_raw + add_y) >> shift_y) : (int)yy_raw;
                int cb_dbg = shift_c > 0 ? (int)((cb_raw + add_c) >> shift_c) : (int)cb_raw;
                int cr_dbg = shift_c > 0 ? (int)((cr_raw + add_c) >> shift_c) : (int)cr_raw;
                if (!sample_logged) {
                    if ((x == ctx->frame_width/2 && y == ctx->frame_height/2) ||
                        (x == ctx->frame_width/4 && y == ctx->frame_height/4) ||
                        (x == ctx->frame_width*3/4 && y == ctx->frame_height*3/4)) {
                        heif_debugf("YUV sample at (%u,%u): Y=%u Cb=%d Cr=%d (raw: %u %u) -> full_range=%d matrix=%d\n",
                                   x, y, yy_dbg, cb_dbg - 128, cr_dbg - 128,
                                   cb_raw, cr_raw,
                                   full_range, matrix);
                    }
                    if (x > ctx->frame_width*3/4 && y > ctx->frame_height*3/4) {
                        sample_logged = 1;
                    }
                }
                uint8_t r = 0, g = 0, b = 0;
                hevc_yuv_to_rgb((int)yy_raw, (int)cb_raw, (int)cr_raw,
                                full_range, matrix, bit_depth_y, bit_depth_c,
                                &r, &g, &b);
                size_t idx = ((size_t)y * ctx->frame_width + x) * 4u;
                rgba[idx + 0] = r;
                rgba[idx + 1] = g;
                rgba[idx + 2] = b;
                rgba[idx + 3] = 255;
            }
        }
    } else {
        int bit_depth = (int)ctx->sps[ctx->last_sps_id].bit_depth_luma;
        int shift = bit_depth > 8 ? (bit_depth - 8) : 0;
        int add = shift > 0 ? (1 << (shift - 1)) : 0;
        int chroma_bit_depth = (int)ctx->sps[ctx->last_sps_id].bit_depth_chroma;
        int cshift = chroma_bit_depth > 8 ? (chroma_bit_depth - 8) : 0;
        int cadd = cshift > 0 ? (1 << (cshift - 1)) : 0;
        uint32_t sub_w = 1;
        uint32_t sub_h = 1;
        if (ctx->sps[ctx->last_sps_id].chroma_format_idc == 1) {
            sub_w = 2;
            sub_h = 2;
        } else if (ctx->sps[ctx->last_sps_id].chroma_format_idc == 2) {
            sub_w = 2;
            sub_h = 1;
        }
        for (uint32_t y = 0; y < ctx->frame_height; y++) {
            uint32_t cy = y / sub_h;
            for (uint32_t x = 0; x < ctx->frame_width; x++) {
                uint32_t cx = x / sub_w;
                uint8_t v = 0;
                if (force_plane == 2 && ctx->frame_cb) {
                    uint16_t c = ctx->frame_cb[(size_t)cy * ctx->chroma_stride + cx];
                    v = cshift > 0 ? (uint8_t)((c + cadd) >> cshift) : (uint8_t)c;
                } else if (force_plane == 3 && ctx->frame_cr) {
                    uint16_t c = ctx->frame_cr[(size_t)cy * ctx->chroma_stride + cx];
                    v = cshift > 0 ? (uint8_t)((c + cadd) >> cshift) : (uint8_t)c;
                } else {
                    uint16_t y_raw = ctx->frame_luma[(size_t)y * ctx->frame_stride + x];
                    v = shift > 0 ? (uint8_t)((y_raw + add) >> shift) : (uint8_t)y_raw;
                }
                size_t idx = ((size_t)y * ctx->frame_width + x) * 4u;
                rgba[idx + 0] = v;
                rgba[idx + 1] = v;
                rgba[idx + 2] = v;
                rgba[idx + 3] = 255;
            }
        }
    }
    out->width = (uint32_t)width;
    out->height = (uint32_t)height;
    out->rgba = rgba;
    out->hotspot_x = 0;
    out->hotspot_y = 0;
    return 1;
}

int cupidimage_load_heif(const unsigned char *data, size_t size, cupidimage_image *out,
                         char *err, size_t errcap) {
    if (!data || !out) {
        set_err(err, errcap, "invalid arguments");
        return 0;
    }
    heif_context ctx;
    memset(&ctx, 0, sizeof(ctx));
    if (!heif_parse_container(data, size, &ctx, err, errcap)) {
        heif_free_context(&ctx);
        return 0;
    }
    if (!ctx.has_pitm) {
        heif_free_context(&ctx);
        set_err(err, errcap, "missing HEIF primary item");
        return 0;
    }
    int ok = heif_decode_item_image(data, size, &ctx, ctx.primary_item_id, out, err, errcap);
    heif_free_context(&ctx);
    return ok;
}

int cupidimage_load_heif_file(const char *path, cupidimage_image *out,
                              char *err, size_t errcap) {
    if (!path || !out) {
        set_err(err, errcap, "invalid arguments");
        return 0;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        set_err(err, errcap, "failed to open file");
        return 0;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        set_err(err, errcap, "failed to seek file");
        return 0;
    }
    long fsize = ftell(f);
    if (fsize <= 0) {
        fclose(f);
        set_err(err, errcap, "empty file");
        return 0;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        set_err(err, errcap, "failed to seek file");
        return 0;
    }
    unsigned char *buf = (unsigned char *)malloc((size_t)fsize);
    if (!buf) {
        fclose(f);
        set_err(err, errcap, "out of memory");
        return 0;
    }
    size_t nread = fread(buf, 1, (size_t)fsize, f);
    fclose(f);
    if (nread != (size_t)fsize) {
        free(buf);
        set_err(err, errcap, "failed to read file");
        return 0;
    }
    int ok = cupidimage_load_heif(buf, (size_t)fsize, out, err, errcap);
    free(buf);
    return ok;
}

int cupidimage_heif_get_item_count(const unsigned char *data, size_t size,
                                   int *count, char *err, size_t errcap) {
    if (!data || !count) {
        set_err(err, errcap, "invalid arguments");
        return 0;
    }
    heif_context ctx;
    memset(&ctx, 0, sizeof(ctx));
    if (!heif_parse_container(data, size, &ctx, err, errcap)) {
        heif_free_context(&ctx);
        return 0;
    }
    if (ctx.info_count > (size_t)INT32_MAX) {
        heif_free_context(&ctx);
        set_err(err, errcap, "HEIF item count too large");
        return 0;
    }
    *count = (int)ctx.info_count;
    heif_free_context(&ctx);
    return 1;
}

int cupidimage_load_heif_item(const unsigned char *data, size_t size,
                              cupidimage_image *out, int item_index,
                              char *err, size_t errcap) {
    if (!data || !out) {
        set_err(err, errcap, "invalid arguments");
        return 0;
    }
    heif_context ctx;
    memset(&ctx, 0, sizeof(ctx));
    if (!heif_parse_container(data, size, &ctx, err, errcap)) {
        heif_free_context(&ctx);
        return 0;
    }
    if (item_index < 0 || (size_t)item_index >= ctx.info_count) {
        heif_free_context(&ctx);
        set_err(err, errcap, "HEIF item index out of range");
        return 0;
    }
    uint32_t item_id = ctx.infos[item_index].item_id;
    int ok = heif_decode_item_image(data, size, &ctx, item_id, out, err, errcap);
    heif_free_context(&ctx);
    return ok;
}

int cupidimage_load_heif_depth(const unsigned char *data, size_t size,
                               cupidimage_image *out, char *err, size_t errcap) {
    if (!data || !out) {
        set_err(err, errcap, "invalid arguments");
        return 0;
    }
    (void)size;
    set_err(err, errcap, "HEIF depth not implemented");
    return 0;
}

int cupidimage_load_heif_sequence(const unsigned char *data, size_t size,
                                  cupidimage_animation *out,
                                  char *err, size_t errcap) {
    if (!data || !out) {
        set_err(err, errcap, "invalid arguments");
        return 0;
    }
    (void)size;
    set_err(err, errcap, "HEIF sequence not implemented");
    return 0;
}
