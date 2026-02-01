#include "cupidimage.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

static void set_err(char *err, size_t errcap, const char *msg) {
    if (err && errcap) {
        snprintf(err, errcap, "%s", msg);
    }
}

static uint32_t read_be32(const unsigned char *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

struct bitstream {
    const uint8_t *data;
    size_t size;
    size_t pos;
    uint32_t bitbuf;
    int bitcount;
};

static int bs_fill(struct bitstream *bs, int need) {
    while (bs->bitcount < need && bs->pos < bs->size) {
        bs->bitbuf |= (uint32_t)bs->data[bs->pos++] << bs->bitcount;
        bs->bitcount += 8;
    }
    return bs->bitcount >= need;
}

static uint32_t bs_read(struct bitstream *bs, int bits) {
    if (!bs_fill(bs, bits)) {
        return 0;
    }
    uint32_t val = bs->bitbuf & ((1u << bits) - 1u);
    bs->bitbuf >>= bits;
    bs->bitcount -= bits;
    return val;
}

static void bs_align(struct bitstream *bs) {
    int drop = bs->bitcount & 7;
    if (drop) {
        bs->bitbuf >>= drop;
        bs->bitcount -= drop;
    }
}

static unsigned reverse_bits(unsigned v, int bits) {
    unsigned r = 0;
    for (int i = 0; i < bits; i++) {
        r = (r << 1) | (v & 1u);
        v >>= 1;
    }
    return r;
}

typedef struct huff_table {
    int maxbits;
    uint16_t *table;
} huff_table;

static void huff_free(huff_table *ht) {
    free(ht->table);
    ht->table = NULL;
    ht->maxbits = 0;
}

static int huff_build(huff_table *ht, const uint8_t *lengths, int num, int maxbits) {
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
        unsigned rev = reverse_bits((unsigned)sym_code, len);
        unsigned fill = 1u << (maxlen - len);
        for (unsigned i = 0; i < fill; i++) {
            unsigned idx = (i << len) | rev;
            ht->table[idx] = (uint16_t)((len << 9) | sym);
        }
    }

    return 1;
}

static int huff_decode(struct bitstream *bs, const huff_table *ht, int *sym) {
    if (!bs_fill(bs, ht->maxbits)) {
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

static int build_fixed_tables(huff_table *litlen, huff_table *dist) {
    uint8_t litlen_lengths[288];
    uint8_t dist_lengths[32];

    for (int i = 0; i <= 143; i++) {
        litlen_lengths[i] = 8;
    }
    for (int i = 144; i <= 255; i++) {
        litlen_lengths[i] = 9;
    }
    for (int i = 256; i <= 279; i++) {
        litlen_lengths[i] = 7;
    }
    for (int i = 280; i <= 287; i++) {
        litlen_lengths[i] = 8;
    }
    for (int i = 0; i < 32; i++) {
        dist_lengths[i] = 5;
    }

    if (!huff_build(litlen, litlen_lengths, 288, 15)) {
        return 0;
    }
    if (!huff_build(dist, dist_lengths, 32, 15)) {
        huff_free(litlen);
        return 0;
    }
    return 1;
}

static int build_dynamic_tables(struct bitstream *bs, huff_table *litlen, huff_table *dist) {
    int hlit = (int)bs_read(bs, 5) + 257;
    int hdist = (int)bs_read(bs, 5) + 1;
    int hclen = (int)bs_read(bs, 4) + 4;
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
        clen_lengths[order[i]] = (uint8_t)bs_read(bs, 3);
    }

    huff_table clen_table = {0, NULL};
    if (!huff_build(&clen_table, clen_lengths, 19, 7)) {
        return 0;
    }

    int total = hlit + hdist;
    uint8_t lengths[316];
    int idx = 0;
    while (idx < total) {
        int sym = 0;
        if (!huff_decode(bs, &clen_table, &sym)) {
            huff_free(&clen_table);
            return 0;
        }
        if (sym <= 15) {
            lengths[idx++] = (uint8_t)sym;
        } else if (sym == 16) {
            if (idx == 0) {
                huff_free(&clen_table);
                return 0;
            }
            int repeat = (int)bs_read(bs, 2) + 3;
            uint8_t prev = lengths[idx - 1];
            for (int i = 0; i < repeat && idx < total; i++) {
                lengths[idx++] = prev;
            }
        } else if (sym == 17) {
            int repeat = (int)bs_read(bs, 3) + 3;
            for (int i = 0; i < repeat && idx < total; i++) {
                lengths[idx++] = 0;
            }
        } else if (sym == 18) {
            int repeat = (int)bs_read(bs, 7) + 11;
            for (int i = 0; i < repeat && idx < total; i++) {
                lengths[idx++] = 0;
            }
        } else {
            huff_free(&clen_table);
            return 0;
        }
    }

    huff_free(&clen_table);

    if (!huff_build(litlen, lengths, hlit, 15)) {
        return 0;
    }
    if (!huff_build(dist, lengths + hlit, hdist, 15)) {
        huff_free(litlen);
        return 0;
    }
    return 1;
}

static int deflate_decode(struct bitstream *bs, uint8_t *out, size_t outcap, size_t *outlen) {
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
        if (!bs_fill(bs, 3)) {
            return 0;
        }
        final_block = (int)bs_read(bs, 1);
        int btype = (int)bs_read(bs, 2);

        if (btype == 0) {
            bs_align(bs);
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
            huff_table litlen = {0, NULL};
            huff_table dist = {0, NULL};
            int ok = 0;
            if (btype == 1) {
                ok = build_fixed_tables(&litlen, &dist);
            } else {
                ok = build_dynamic_tables(bs, &litlen, &dist);
            }
            if (!ok) {
                huff_free(&litlen);
                huff_free(&dist);
                return 0;
            }

            while (1) {
                int sym = 0;
                if (!huff_decode(bs, &litlen, &sym)) {
                    huff_free(&litlen);
                    huff_free(&dist);
                    return 0;
                }
                if (sym < 256) {
                    if (outpos >= outcap) {
                        huff_free(&litlen);
                        huff_free(&dist);
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
                        length += (int)bs_read(bs, extra);
                    }

                    int dist_sym = 0;
                    if (!huff_decode(bs, &dist, &dist_sym)) {
                        huff_free(&litlen);
                        huff_free(&dist);
                        return 0;
                    }
                    if (dist_sym > 29) {
                        huff_free(&litlen);
                        huff_free(&dist);
                        return 0;
                    }
                    int distance = dist_base[dist_sym];
                    int dist_ext = dist_extra[dist_sym];
                    if (dist_ext) {
                        distance += (int)bs_read(bs, dist_ext);
                    }
                    if (distance <= 0 || (size_t)distance > outpos) {
                        huff_free(&litlen);
                        huff_free(&dist);
                        return 0;
                    }
                    if (outpos + (size_t)length > outcap) {
                        huff_free(&litlen);
                        huff_free(&dist);
                        return 0;
                    }
                    for (int i = 0; i < length; i++) {
                        out[outpos] = out[outpos - (size_t)distance];
                        outpos++;
                    }
                } else {
                    huff_free(&litlen);
                    huff_free(&dist);
                    return 0;
                }
            }

            huff_free(&litlen);
            huff_free(&dist);
        } else {
            return 0;
        }
    }

    *outlen = outpos;
    return 1;
}

static int zlib_decompress(const uint8_t *data, size_t size, uint8_t *out, size_t outcap, size_t *outlen) {
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

    struct bitstream bs;
    bs.data = data + 2;
    bs.size = size - 2;
    bs.pos = 0;
    bs.bitbuf = 0;
    bs.bitcount = 0;

    if (!deflate_decode(&bs, out, outcap, outlen)) {
        return 0;
    }

    return 1;
}

static uint8_t paeth(uint8_t a, uint8_t b, uint8_t c) {
    int p = (int)a + (int)b - (int)c;
    int pa = abs(p - (int)a);
    int pb = abs(p - (int)b);
    int pc = abs(p - (int)c);
    if (pa <= pb && pa <= pc) {
        return a;
    }
    if (pb <= pc) {
        return b;
    }
    return c;
}

static int unfilter(const uint8_t *raw, size_t raw_size, uint8_t *recon, size_t rowbytes,
                    uint32_t height, int bpp) {
    size_t expected = (rowbytes + 1) * (size_t)height;
    if (raw_size < expected) {
        return 0;
    }

    for (uint32_t y = 0; y < height; y++) {
        const uint8_t *in = raw + y * (rowbytes + 1) + 1;
        uint8_t *out = recon + y * rowbytes;
        uint8_t filter = raw[y * (rowbytes + 1)];

        switch (filter) {
        case 0:
            memcpy(out, in, rowbytes);
            break;
        case 1:
            for (size_t x = 0; x < rowbytes; x++) {
                uint8_t left = (x >= (size_t)bpp) ? out[x - (size_t)bpp] : 0;
                out[x] = (uint8_t)(in[x] + left);
            }
            break;
        case 2:
            for (size_t x = 0; x < rowbytes; x++) {
                uint8_t up = (y > 0) ? recon[(y - 1) * rowbytes + x] : 0;
                out[x] = (uint8_t)(in[x] + up);
            }
            break;
        case 3:
            for (size_t x = 0; x < rowbytes; x++) {
                uint8_t left = (x >= (size_t)bpp) ? out[x - (size_t)bpp] : 0;
                uint8_t up = (y > 0) ? recon[(y - 1) * rowbytes + x] : 0;
                out[x] = (uint8_t)(in[x] + (uint8_t)(((int)left + (int)up) / 2));
            }
            break;
        case 4:
            for (size_t x = 0; x < rowbytes; x++) {
                uint8_t left = (x >= (size_t)bpp) ? out[x - (size_t)bpp] : 0;
                uint8_t up = (y > 0) ? recon[(y - 1) * rowbytes + x] : 0;
                uint8_t up_left = (y > 0 && x >= (size_t)bpp) ? recon[(y - 1) * rowbytes + x - (size_t)bpp] : 0;
                out[x] = (uint8_t)(in[x] + paeth(left, up, up_left));
            }
            break;
        default:
            return 0;
        }
    }

    return 1;
}

static int calc_rowbytes(uint32_t width, uint8_t color_type, uint8_t bit_depth, int channels,
                         size_t *rowbytes_out, int *bpp_out) {
    if (color_type == 3 || (color_type == 0 && bit_depth < 8)) {
        size_t bits_per_row = (size_t)width * (size_t)bit_depth;
        if (width > 0 && bits_per_row / (size_t)bit_depth != (size_t)width) {
            return 0;
        }
        size_t rowbytes = (bits_per_row + 7u) / 8u;
        *rowbytes_out = rowbytes;
        *bpp_out = 1;
        return 1;
    }
    size_t bytes_per_pixel = (size_t)channels;
    if (bit_depth == 16) {
        bytes_per_pixel *= 2u;
    }
    size_t rowbytes = (size_t)width * bytes_per_pixel;
    if (width > 0 && rowbytes / bytes_per_pixel != (size_t)width) {
        return 0;
    }
    *rowbytes_out = rowbytes;
    *bpp_out = (int)bytes_per_pixel;
    return 1;
}

static uint8_t palette_index_at(const uint8_t *row, uint32_t x, uint8_t bit_depth) {
    if (bit_depth == 8) {
        return row[x];
    }
    if (bit_depth == 4) {
        uint8_t byte = row[x / 2u];
        return (x % 2u == 0) ? (byte >> 4) : (byte & 0x0Fu);
    }
    if (bit_depth == 2) {
        uint8_t byte = row[x / 4u];
        unsigned shift = 6u - 2u * (x % 4u);
        return (uint8_t)((byte >> shift) & 0x03u);
    }
    uint8_t byte = row[x / 8u];
    unsigned shift = 7u - (x % 8u);
    return (uint8_t)((byte >> shift) & 0x01u);
}

static uint8_t gray_sample_at(const uint8_t *row, uint32_t x, uint8_t bit_depth) {
    if (bit_depth == 8) {
        return row[x];
    }
    if (bit_depth == 4) {
        uint8_t byte = row[x / 2u];
        return (x % 2u == 0) ? (byte >> 4) : (byte & 0x0Fu);
    }
    if (bit_depth == 2) {
        uint8_t byte = row[x / 4u];
        unsigned shift = 6u - 2u * (x % 4u);
        return (uint8_t)((byte >> shift) & 0x03u);
    }
    uint8_t byte = row[x / 8u];
    unsigned shift = 7u - (x % 8u);
    return (uint8_t)((byte >> shift) & 0x01u);
}

static uint8_t sample16_to_8(uint16_t v) {
    return (uint8_t)((v + 128u) / 257u);
}

int cupidimage_load_png(const unsigned char *data, size_t size, cupidimage_image *out,
                        char *err, size_t errcap) {
    if (!data || !out) {
        set_err(err, errcap, "invalid arguments");
        return 0;
    }

    memset(out, 0, sizeof(*out));

    static const unsigned char sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (size < 8 || memcmp(data, sig, 8) != 0) {
        set_err(err, errcap, "not a PNG");
        return 0;
    }

    uint32_t width = 0;
    uint32_t height = 0;
    uint8_t bit_depth = 0;
    uint8_t color_type = 0;
    uint8_t compression = 0;
    uint8_t filter = 0;
    uint8_t interlace = 0;

    uint8_t palette[256 * 3];
    uint8_t palette_alpha[256];
    int palette_entries = 0;
    int trns_entries = 0;
    for (int i = 0; i < 256; i++) {
        palette_alpha[i] = 255;
    }

    uint8_t *idat = NULL;
    size_t idat_size = 0;

    size_t off = 8;
    while (off + 8 <= size) {
        uint32_t length = read_be32(data + off);
        const unsigned char *type = data + off + 4;
        off += 8;
        if (off + length + 4 > size) {
            free(idat);
            set_err(err, errcap, "truncated PNG");
            return 0;
        }

        const unsigned char *chunk = data + off;

        if (memcmp(type, "IHDR", 4) == 0) {
            if (length != 13) {
                free(idat);
                set_err(err, errcap, "invalid IHDR");
                return 0;
            }
            width = read_be32(chunk);
            height = read_be32(chunk + 4);
            bit_depth = chunk[8];
            color_type = chunk[9];
            compression = chunk[10];
            filter = chunk[11];
            interlace = chunk[12];
        } else if (memcmp(type, "PLTE", 4) == 0) {
            if (length == 0 || length % 3 != 0 || length / 3 > 256) {
                free(idat);
                set_err(err, errcap, "invalid PLTE");
                return 0;
            }
            palette_entries = (int)(length / 3);
            memcpy(palette, chunk, length);
        } else if (memcmp(type, "tRNS", 4) == 0) {
            if (length > 256) {
                free(idat);
                set_err(err, errcap, "invalid tRNS");
                return 0;
            }
            trns_entries = (int)length;
            memcpy(palette_alpha, chunk, length);
        } else if (memcmp(type, "IDAT", 4) == 0) {
            uint8_t *next = (uint8_t *)realloc(idat, idat_size + length);
            if (!next) {
                free(idat);
                set_err(err, errcap, "out of memory");
                return 0;
            }
            idat = next;
            memcpy(idat + idat_size, chunk, length);
            idat_size += length;
        } else if (memcmp(type, "IEND", 4) == 0) {
            break;
        }

        off += length + 4;
    }

    if (width == 0 || height == 0) {
        free(idat);
        set_err(err, errcap, "missing IHDR");
        return 0;
    }
    if (idat_size == 0) {
        free(idat);
        set_err(err, errcap, "missing IDAT");
        return 0;
    }
    if (compression != 0 || filter != 0 || (interlace != 0 && interlace != 1)) {
        free(idat);
        set_err(err, errcap, "unsupported PNG method");
        return 0;
    }
    if (color_type == 3) {
        if (!(bit_depth == 1 || bit_depth == 2 || bit_depth == 4 || bit_depth == 8)) {
            free(idat);
            set_err(err, errcap, "unsupported palette bit depth");
            return 0;
        }
        if (palette_entries == 0) {
            free(idat);
            set_err(err, errcap, "missing PLTE");
            return 0;
        }
        if (trns_entries > palette_entries) {
            free(idat);
            set_err(err, errcap, "invalid tRNS");
            return 0;
        }
    } else if (color_type == 0) {
        if (!(bit_depth == 1 || bit_depth == 2 || bit_depth == 4 || bit_depth == 8 || bit_depth == 16)) {
            free(idat);
            set_err(err, errcap, "unsupported grayscale bit depth");
            return 0;
        }
    } else {
        if (!(bit_depth == 8 || bit_depth == 16)) {
            free(idat);
            set_err(err, errcap, "only 8-bit or 16-bit PNG supported");
            return 0;
        }
    }

    int channels = 0;
    switch (color_type) {
    case 0: channels = 1; break;
    case 2: channels = 3; break;
    case 3: channels = 1; break;
    case 4: channels = 2; break;
    case 6: channels = 4; break;
    default:
        free(idat);
        set_err(err, errcap, "unsupported color type");
        return 0;
    }

    size_t rowbytes = 0;
    int bpp = 0;
    if (!calc_rowbytes(width, color_type, bit_depth, channels, &rowbytes, &bpp)) {
        free(idat);
        set_err(err, errcap, "image too large");
        return 0;
    }

    size_t rgba_size = (size_t)width * (size_t)height * 4u;
    if (rgba_size / 4u != (size_t)width * (size_t)height) {
        free(idat);
        set_err(err, errcap, "image too large");
        return 0;
    }

    if (interlace == 0) {
        size_t raw_size = (rowbytes + 1) * (size_t)height;
        if (raw_size / (rowbytes + 1) != (size_t)height) {
            free(idat);
            set_err(err, errcap, "image too large");
            return 0;
        }

        uint8_t *raw = (uint8_t *)malloc(raw_size);
        if (!raw) {
            free(idat);
            set_err(err, errcap, "out of memory");
            return 0;
        }

        size_t outlen = 0;
        if (!zlib_decompress(idat, idat_size, raw, raw_size, &outlen) || outlen != raw_size) {
            free(idat);
            free(raw);
            set_err(err, errcap, "zlib decode failed");
            return 0;
        }
        free(idat);

        uint8_t *recon = (uint8_t *)malloc(rowbytes * (size_t)height);
        if (!recon) {
            free(raw);
            set_err(err, errcap, "out of memory");
            return 0;
        }

        if (!unfilter(raw, raw_size, recon, rowbytes, height, bpp)) {
            free(raw);
            free(recon);
            set_err(err, errcap, "PNG filter failed");
            return 0;
        }
        free(raw);

        uint8_t *rgba = (uint8_t *)malloc(rgba_size);
        if (!rgba) {
            free(recon);
            set_err(err, errcap, "out of memory");
            return 0;
        }

        for (uint32_t y = 0; y < height; y++) {
            const uint8_t *src = recon + (size_t)y * rowbytes;
            uint8_t *dst = rgba + (size_t)y * (size_t)width * 4u;
        for (uint32_t x = 0; x < width; x++) {
            if (color_type == 0) {
                if (bit_depth == 16) {
                    size_t off = (size_t)x * 2u;
                    uint16_t g16 = (uint16_t)((src[off] << 8) | src[off + 1]);
                    uint8_t g = sample16_to_8(g16);
                    dst[x * 4 + 0] = g;
                    dst[x * 4 + 1] = g;
                    dst[x * 4 + 2] = g;
                    dst[x * 4 + 3] = 255;
                } else if (bit_depth == 8) {
                    uint8_t g = src[x];
                    dst[x * 4 + 0] = g;
                    dst[x * 4 + 1] = g;
                    dst[x * 4 + 2] = g;
                    dst[x * 4 + 3] = 255;
                } else {
                    uint8_t v = gray_sample_at(src, x, bit_depth);
                    unsigned maxv = (1u << bit_depth) - 1u;
                    uint8_t g = (uint8_t)((v * 255u + maxv / 2u) / maxv);
                    dst[x * 4 + 0] = g;
                    dst[x * 4 + 1] = g;
                    dst[x * 4 + 2] = g;
                    dst[x * 4 + 3] = 255;
                }
                } else if (color_type == 2) {
                    if (bit_depth == 16) {
                        size_t off = (size_t)x * 6u;
                        uint16_t r16 = (uint16_t)((src[off] << 8) | src[off + 1]);
                        uint16_t g16 = (uint16_t)((src[off + 2] << 8) | src[off + 3]);
                        uint16_t b16 = (uint16_t)((src[off + 4] << 8) | src[off + 5]);
                        dst[x * 4 + 0] = sample16_to_8(r16);
                        dst[x * 4 + 1] = sample16_to_8(g16);
                        dst[x * 4 + 2] = sample16_to_8(b16);
                        dst[x * 4 + 3] = 255;
                    } else {
                        dst[x * 4 + 0] = src[x * 3 + 0];
                        dst[x * 4 + 1] = src[x * 3 + 1];
                        dst[x * 4 + 2] = src[x * 3 + 2];
                        dst[x * 4 + 3] = 255;
                    }
                } else if (color_type == 3) {
                    uint8_t idx = palette_index_at(src, x, bit_depth);
                    if (idx >= (uint8_t)palette_entries) {
                        free(recon);
                        free(rgba);
                        set_err(err, errcap, "palette index out of range");
                        return 0;
                    }
                    dst[x * 4 + 0] = palette[(int)idx * 3 + 0];
                    dst[x * 4 + 1] = palette[(int)idx * 3 + 1];
                    dst[x * 4 + 2] = palette[(int)idx * 3 + 2];
                    dst[x * 4 + 3] = palette_alpha[idx];
                } else if (color_type == 4) {
                    if (bit_depth == 16) {
                        size_t off = (size_t)x * 4u;
                        uint16_t g16 = (uint16_t)((src[off] << 8) | src[off + 1]);
                        uint16_t a16 = (uint16_t)((src[off + 2] << 8) | src[off + 3]);
                        dst[x * 4 + 0] = sample16_to_8(g16);
                        dst[x * 4 + 1] = sample16_to_8(g16);
                        dst[x * 4 + 2] = sample16_to_8(g16);
                        dst[x * 4 + 3] = sample16_to_8(a16);
                    } else {
                        uint8_t g = src[x * 2 + 0];
                        uint8_t a = src[x * 2 + 1];
                        dst[x * 4 + 0] = g;
                        dst[x * 4 + 1] = g;
                        dst[x * 4 + 2] = g;
                        dst[x * 4 + 3] = a;
                    }
                } else {
                    if (bit_depth == 16) {
                        size_t off = (size_t)x * 8u;
                        uint16_t r16 = (uint16_t)((src[off] << 8) | src[off + 1]);
                        uint16_t g16 = (uint16_t)((src[off + 2] << 8) | src[off + 3]);
                        uint16_t b16 = (uint16_t)((src[off + 4] << 8) | src[off + 5]);
                        uint16_t a16 = (uint16_t)((src[off + 6] << 8) | src[off + 7]);
                        dst[x * 4 + 0] = sample16_to_8(r16);
                        dst[x * 4 + 1] = sample16_to_8(g16);
                        dst[x * 4 + 2] = sample16_to_8(b16);
                        dst[x * 4 + 3] = sample16_to_8(a16);
                    } else {
                        dst[x * 4 + 0] = src[x * 4 + 0];
                        dst[x * 4 + 1] = src[x * 4 + 1];
                        dst[x * 4 + 2] = src[x * 4 + 2];
                        dst[x * 4 + 3] = src[x * 4 + 3];
                    }
                }
            }
        }

        free(recon);

        out->width = width;
        out->height = height;
        out->rgba = rgba;
        return 1;
    }

    static const int pass_x[7] = {0, 4, 0, 2, 0, 1, 0};
    static const int pass_y[7] = {0, 0, 4, 0, 2, 0, 1};
    static const int pass_dx[7] = {8, 8, 4, 4, 2, 2, 1};
    static const int pass_dy[7] = {8, 8, 8, 4, 4, 2, 2};

    size_t total_raw = 0;
    for (int p = 0; p < 7; p++) {
        uint32_t pw = (width > (uint32_t)pass_x[p]) ? (uint32_t)((width - pass_x[p] + pass_dx[p] - 1) / pass_dx[p]) : 0;
        uint32_t ph = (height > (uint32_t)pass_y[p]) ? (uint32_t)((height - pass_y[p] + pass_dy[p] - 1) / pass_dy[p]) : 0;
        if (pw == 0 || ph == 0) {
            continue;
        }
        size_t pass_rowbytes = 0;
        int pass_bpp = 0;
        if (!calc_rowbytes(pw, color_type, bit_depth, channels, &pass_rowbytes, &pass_bpp)) {
            free(idat);
            set_err(err, errcap, "image too large");
            return 0;
        }
        size_t pass_size = (pass_rowbytes + 1) * (size_t)ph;
        if (pass_size / (pass_rowbytes + 1) != (size_t)ph) {
            free(idat);
            set_err(err, errcap, "image too large");
            return 0;
        }
        if (total_raw + pass_size < total_raw) {
            free(idat);
            set_err(err, errcap, "image too large");
            return 0;
        }
        total_raw += pass_size;
    }

    uint8_t *raw = (uint8_t *)malloc(total_raw);
    if (!raw) {
        free(idat);
        set_err(err, errcap, "out of memory");
        return 0;
    }
    size_t outlen = 0;
    if (!zlib_decompress(idat, idat_size, raw, total_raw, &outlen) || outlen != total_raw) {
        free(idat);
        free(raw);
        set_err(err, errcap, "zlib decode failed");
        return 0;
    }
    free(idat);

    uint8_t *rgba = (uint8_t *)malloc(rgba_size);
    if (!rgba) {
        free(raw);
        set_err(err, errcap, "out of memory");
        return 0;
    }

    size_t offset = 0;
    for (int p = 0; p < 7; p++) {
        uint32_t pw = (width > (uint32_t)pass_x[p]) ? (uint32_t)((width - pass_x[p] + pass_dx[p] - 1) / pass_dx[p]) : 0;
        uint32_t ph = (height > (uint32_t)pass_y[p]) ? (uint32_t)((height - pass_y[p] + pass_dy[p] - 1) / pass_dy[p]) : 0;
        if (pw == 0 || ph == 0) {
            continue;
        }
        size_t pass_rowbytes = 0;
        int pass_bpp = 0;
        if (!calc_rowbytes(pw, color_type, bit_depth, channels, &pass_rowbytes, &pass_bpp)) {
            free(raw);
            free(rgba);
            set_err(err, errcap, "image too large");
            return 0;
        }
        size_t pass_raw_size = (pass_rowbytes + 1) * (size_t)ph;
        if (offset + pass_raw_size > total_raw) {
            free(raw);
            free(rgba);
            set_err(err, errcap, "truncated PNG");
            return 0;
        }

        uint8_t *recon = (uint8_t *)malloc(pass_rowbytes * (size_t)ph);
        if (!recon) {
            free(raw);
            free(rgba);
            set_err(err, errcap, "out of memory");
            return 0;
        }
        if (!unfilter(raw + offset, pass_raw_size, recon, pass_rowbytes, ph, pass_bpp)) {
            free(raw);
            free(rgba);
            free(recon);
            set_err(err, errcap, "PNG filter failed");
            return 0;
        }
        offset += pass_raw_size;

        for (uint32_t y = 0; y < ph; y++) {
            const uint8_t *src = recon + (size_t)y * pass_rowbytes;
            uint32_t dest_y = (uint32_t)pass_y[p] + y * (uint32_t)pass_dy[p];
            for (uint32_t x = 0; x < pw; x++) {
                uint32_t dest_x = (uint32_t)pass_x[p] + x * (uint32_t)pass_dx[p];
                uint8_t *dst = rgba + ((size_t)dest_y * (size_t)width + (size_t)dest_x) * 4u;
                if (color_type == 0) {
                    if (bit_depth == 16) {
                        size_t off = (size_t)x * 2u;
                        uint16_t g16 = (uint16_t)((src[off] << 8) | src[off + 1]);
                        uint8_t g = sample16_to_8(g16);
                        dst[0] = g;
                        dst[1] = g;
                        dst[2] = g;
                        dst[3] = 255;
                    } else if (bit_depth == 8) {
                        uint8_t g = src[x];
                        dst[0] = g;
                        dst[1] = g;
                        dst[2] = g;
                        dst[3] = 255;
                    } else {
                        uint8_t v = gray_sample_at(src, x, bit_depth);
                        unsigned maxv = (1u << bit_depth) - 1u;
                        uint8_t g = (uint8_t)((v * 255u + maxv / 2u) / maxv);
                        dst[0] = g;
                        dst[1] = g;
                        dst[2] = g;
                        dst[3] = 255;
                    }
                } else if (color_type == 2) {
                    if (bit_depth == 16) {
                        size_t off = (size_t)x * 6u;
                        uint16_t r16 = (uint16_t)((src[off] << 8) | src[off + 1]);
                        uint16_t g16 = (uint16_t)((src[off + 2] << 8) | src[off + 3]);
                        uint16_t b16 = (uint16_t)((src[off + 4] << 8) | src[off + 5]);
                        dst[0] = sample16_to_8(r16);
                        dst[1] = sample16_to_8(g16);
                        dst[2] = sample16_to_8(b16);
                        dst[3] = 255;
                    } else {
                        dst[0] = src[x * 3 + 0];
                        dst[1] = src[x * 3 + 1];
                        dst[2] = src[x * 3 + 2];
                        dst[3] = 255;
                    }
                } else if (color_type == 3) {
                    uint8_t idx = palette_index_at(src, x, bit_depth);
                    if (idx >= (uint8_t)palette_entries) {
                        free(raw);
                        free(rgba);
                        free(recon);
                        set_err(err, errcap, "palette index out of range");
                        return 0;
                    }
                    dst[0] = palette[(int)idx * 3 + 0];
                    dst[1] = palette[(int)idx * 3 + 1];
                    dst[2] = palette[(int)idx * 3 + 2];
                    dst[3] = palette_alpha[idx];
                } else if (color_type == 4) {
                    if (bit_depth == 16) {
                        size_t off = (size_t)x * 4u;
                        uint16_t g16 = (uint16_t)((src[off] << 8) | src[off + 1]);
                        uint16_t a16 = (uint16_t)((src[off + 2] << 8) | src[off + 3]);
                        dst[0] = sample16_to_8(g16);
                        dst[1] = sample16_to_8(g16);
                        dst[2] = sample16_to_8(g16);
                        dst[3] = sample16_to_8(a16);
                    } else {
                        uint8_t g = src[x * 2 + 0];
                        uint8_t a = src[x * 2 + 1];
                        dst[0] = g;
                        dst[1] = g;
                        dst[2] = g;
                        dst[3] = a;
                    }
                } else {
                    if (bit_depth == 16) {
                        size_t off = (size_t)x * 8u;
                        uint16_t r16 = (uint16_t)((src[off] << 8) | src[off + 1]);
                        uint16_t g16 = (uint16_t)((src[off + 2] << 8) | src[off + 3]);
                        uint16_t b16 = (uint16_t)((src[off + 4] << 8) | src[off + 5]);
                        uint16_t a16 = (uint16_t)((src[off + 6] << 8) | src[off + 7]);
                        dst[0] = sample16_to_8(r16);
                        dst[1] = sample16_to_8(g16);
                        dst[2] = sample16_to_8(b16);
                        dst[3] = sample16_to_8(a16);
                    } else {
                        dst[0] = src[x * 4 + 0];
                        dst[1] = src[x * 4 + 1];
                        dst[2] = src[x * 4 + 2];
                        dst[3] = src[x * 4 + 3];
                    }
                }
            }
        }
        free(recon);
    }

    free(raw);
    out->width = width;
    out->height = height;
    out->rgba = rgba;
    return 1;
}

int cupidimage_load_png_file(const char *path, cupidimage_image *out, char *err, size_t errcap) {
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

    int ok = cupidimage_load_png(buf, (size_t)fsize, out, err, errcap);
    free(buf);
    return ok;
}

void cupidimage_free(cupidimage_image *img) {
    if (!img) {
        return;
    }
    free(img->rgba);
    img->rgba = NULL;
    img->width = 0;
    img->height = 0;
}

static void write_ansi_bg(FILE *out, uint8_t r, uint8_t g, uint8_t b) {
    fprintf(out, "\x1b[48;2;%u;%u;%um", r, g, b);
}

int cupidimage_render_ansi(const cupidimage_image *img, FILE *out, int max_width, int max_height) {
    if (!img || !img->rgba || !out || img->width == 0 || img->height == 0) {
        return 0;
    }

    int width = (int)img->width;
    int height = (int)img->height;
    double scale = 1.0;
    if (max_width > 0 && width > max_width) {
        double s = (double)max_width / (double)width;
        if (s < scale) {
            scale = s;
        }
    }
    if (max_height > 0 && height > max_height) {
        double s = (double)max_height / (double)height;
        if (s < scale) {
            scale = s;
        }
    }

    int out_w = (int)(width * scale);
    int out_h = (int)(height * scale);
    if (out_w < 1) out_w = 1;
    if (out_h < 1) out_h = 1;

    uint8_t last_r = 0, last_g = 0, last_b = 0;
    int have_last = 0;

    for (int y = 0; y < out_h; y++) {
        int src_y = (int)((long long)y * height / out_h);
        for (int x = 0; x < out_w; x++) {
            int src_x = (int)((long long)x * width / out_w);
            const uint8_t *px = img->rgba + ((size_t)src_y * (size_t)width + (size_t)src_x) * 4u;
            uint8_t a = px[3];
            unsigned inv = 255u - (unsigned)a;
            uint8_t r = (uint8_t)(((unsigned)px[0] * (unsigned)a + 255u * inv) / 255u);
            uint8_t g = (uint8_t)(((unsigned)px[1] * (unsigned)a + 255u * inv) / 255u);
            uint8_t b = (uint8_t)(((unsigned)px[2] * (unsigned)a + 255u * inv) / 255u);

            if (!have_last || r != last_r || g != last_g || b != last_b) {
                write_ansi_bg(out, r, g, b);
                last_r = r;
                last_g = g;
                last_b = b;
                have_last = 1;
            }
            fputc(' ', out);
        }
        fprintf(out, "\x1b[0m\n");
        have_last = 0;
    }

    return 1;
}
