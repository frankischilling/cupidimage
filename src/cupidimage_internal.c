#include "cupidimage_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

void cupidimage_set_err(char *err, size_t errcap, const char *msg) {
    if (err && errcap) {
        snprintf(err, errcap, "%s", msg ? msg : "error");
    }
}

int cupidimage_read_file_bytes(const char *path,
                               unsigned char **data,
                               size_t *size,
                               char *err,
                               size_t errcap) {
    if (!path || !data || !size) {
        cupidimage_set_err(err, errcap, "invalid arguments");
        return 0;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        cupidimage_set_err(err, errcap, "failed to open file");
        return 0;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        cupidimage_set_err(err, errcap, "failed to seek file");
        return 0;
    }
    long fsize = ftell(f);
    if (fsize <= 0) {
        fclose(f);
        cupidimage_set_err(err, errcap, "empty file");
        return 0;
    }
    if ((unsigned long)fsize > SIZE_MAX) {
        fclose(f);
        cupidimage_set_err(err, errcap, "file too large");
        return 0;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        cupidimage_set_err(err, errcap, "failed to seek file");
        return 0;
    }

    unsigned char *buf = (unsigned char *)malloc((size_t)fsize);
    if (!buf) {
        fclose(f);
        cupidimage_set_err(err, errcap, "out of memory");
        return 0;
    }

    size_t nread = fread(buf, 1, (size_t)fsize, f);
    fclose(f);
    if (nread != (size_t)fsize) {
        free(buf);
        cupidimage_set_err(err, errcap, "failed to read file");
        return 0;
    }

    *data = buf;
    *size = (size_t)fsize;
    return 1;
}

int cupidimage_load_image_file_via_memory(const char *path,
                                          cupidimage_image *out,
                                          char *err,
                                          size_t errcap,
                                          cupidimage_image_loader_fn loader) {
    if (!path || !out || !loader) {
        cupidimage_set_err(err, errcap, "invalid arguments");
        return 0;
    }

    unsigned char *buf = NULL;
    size_t size = 0;
    if (!cupidimage_read_file_bytes(path, &buf, &size, err, errcap)) {
        return 0;
    }

    int ok = loader(buf, size, out, err, errcap);
    free(buf);
    return ok;
}

int cupidimage_load_animation_file_via_memory(const char *path,
                                              cupidimage_animation *out,
                                              char *err,
                                              size_t errcap,
                                              cupidimage_animation_loader_fn loader) {
    if (!path || !out || !loader) {
        cupidimage_set_err(err, errcap, "invalid arguments");
        return 0;
    }

    unsigned char *buf = NULL;
    size_t size = 0;
    if (!cupidimage_read_file_bytes(path, &buf, &size, err, errcap)) {
        return 0;
    }

    int ok = loader(buf, size, out, err, errcap);
    free(buf);
    return ok;
}

/* Deflate bit writer */
typedef struct {
    uint8_t *out;
    size_t outcap;
    size_t pos;
    uint32_t bitbuf;
    int bitcount;
    int overflow;
} deflate_writer;

static void deflate_put_bits(deflate_writer *w, uint32_t bits, int nbits) {
    w->bitbuf |= bits << w->bitcount;
    w->bitcount += nbits;

    while (w->bitcount >= 8) {
        if (w->pos >= w->outcap) {
            w->overflow = 1;
            return;
        }
        w->out[w->pos++] = (uint8_t)(w->bitbuf & 0xFF);
        w->bitbuf >>= 8;
        w->bitcount -= 8;
    }
}

static void deflate_flush_bits(deflate_writer *w) {
    if (w->bitcount > 0) {
        if (w->pos >= w->outcap) {
            w->overflow = 1;
            return;
        }
        w->out[w->pos++] = (uint8_t)(w->bitbuf & 0xFF);
    }
    w->bitbuf = 0;
    w->bitcount = 0;
}

/* Static Huffman codes for literals (RFC 1951) */
static const uint16_t static_lit_codes[288] = {
    /* 0-143: 8 bits, 00110000-10111111 */
    0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,
    0x0038, 0x0039, 0x003A, 0x003B, 0x003C, 0x003D, 0x003E, 0x003F,
    0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,
    0x0048, 0x0049, 0x004A, 0x004B, 0x004C, 0x004D, 0x004E, 0x004F,
    0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057,
    0x0058, 0x0059, 0x005A, 0x005B, 0x005C, 0x005D, 0x005E, 0x005F,
    0x0060, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067,
    0x0068, 0x0069, 0x006A, 0x006B, 0x006C, 0x006D, 0x006E, 0x006F,
    0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077,
    0x0078, 0x0079, 0x007A, 0x007B, 0x007C, 0x007D, 0x007E, 0x007F,
    0x0080, 0x0081, 0x0082, 0x0083, 0x0084, 0x0085, 0x0086, 0x0087,
    0x0088, 0x0089, 0x008A, 0x008B, 0x008C, 0x008D, 0x008E, 0x008F,
    0x0090, 0x0091, 0x0092, 0x0093, 0x0094, 0x0095, 0x0096, 0x0097,
    0x0098, 0x0099, 0x009A, 0x009B, 0x009C, 0x009D, 0x009E, 0x009F,
    0x00A0, 0x00A1, 0x00A2, 0x00A3, 0x00A4, 0x00A5, 0x00A6, 0x00A7,
    0x00A8, 0x00A9, 0x00AA, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x00AF,
    0x00B0, 0x00B1, 0x00B2, 0x00B3, 0x00B4, 0x00B5, 0x00B6, 0x00B7,
    0x00B8, 0x00B9, 0x00BA, 0x00BB, 0x00BC, 0x00BD, 0x00BE, 0x00BF,
    /* 144-255: 9 bits, 110010000-111111111 */
    0x0190, 0x0191, 0x0192, 0x0193, 0x0194, 0x0195, 0x0196, 0x0197,
    0x0198, 0x0199, 0x019A, 0x019B, 0x019C, 0x019D, 0x019E, 0x019F,
    0x01A0, 0x01A1, 0x01A2, 0x01A3, 0x01A4, 0x01A5, 0x01A6, 0x01A7,
    0x01A8, 0x01A9, 0x01AA, 0x01AB, 0x01AC, 0x01AD, 0x01AE, 0x01AF,
    0x01B0, 0x01B1, 0x01B2, 0x01B3, 0x01B4, 0x01B5, 0x01B6, 0x01B7,
    0x01B8, 0x01B9, 0x01BA, 0x01BB, 0x01BC, 0x01BD, 0x01BE, 0x01BF,
    0x01C0, 0x01C1, 0x01C2, 0x01C3, 0x01C4, 0x01C5, 0x01C6, 0x01C7,
    0x01C8, 0x01C9, 0x01CA, 0x01CB, 0x01CC, 0x01CD, 0x01CE, 0x01CF,
    0x01D0, 0x01D1, 0x01D2, 0x01D3, 0x01D4, 0x01D5, 0x01D6, 0x01D7,
    0x01D8, 0x01D9, 0x01DA, 0x01DB, 0x01DC, 0x01DD, 0x01DE, 0x01DF,
    0x01E0, 0x01E1, 0x01E2, 0x01E3, 0x01E4, 0x01E5, 0x01E6, 0x01E7,
    0x01E8, 0x01E9, 0x01EA, 0x01EB, 0x01EC, 0x01ED, 0x01EE, 0x01EF,
    0x01F0, 0x01F1, 0x01F2, 0x01F3, 0x01F4, 0x01F5, 0x01F6, 0x01F7,
    0x01F8, 0x01F9, 0x01FA, 0x01FB, 0x01FC, 0x01FD, 0x01FE, 0x01FF,
    /* 256-279: 7 bits, 0000000-0010111 */
    0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007,
    0x0008, 0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x000E, 0x000F,
    0x0010, 0x0011, 0x0012, 0x0013, 0x0014, 0x0015, 0x0016, 0x0017,
    /* 280-287: 8 bits, 11000000-11000111 */
    0x00C0, 0x00C1, 0x00C2, 0x00C3, 0x00C4, 0x00C5, 0x00C6, 0x00C7
};

static const uint8_t static_lit_lens[288] = {
    8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,
    9,9,9,9,9,9,9,9, 9,9,9,9,9,9,9,9, 9,9,9,9,9,9,9,9, 9,9,9,9,9,9,9,9,
    9,9,9,9,9,9,9,9, 9,9,9,9,9,9,9,9, 9,9,9,9,9,9,9,9, 9,9,9,9,9,9,9,9,
    9,9,9,9,9,9,9,9, 9,9,9,9,9,9,9,9, 9,9,9,9,9,9,9,9, 9,9,9,9,9,9,9,9,
    7,7,7,7,7,7,7,7, 7,7,7,7,7,7,7,7, 7,7,7,7,7,7,7,7, 8,8,8,8,8,8,8,8
};

/* Reverse bits for Huffman coding */
static uint32_t reverse_bits(uint32_t val, int nbits) {
    uint32_t result = 0;
    for (int i = 0; i < nbits; i++) {
        result = (result << 1) | (val & 1);
        val >>= 1;
    }
    return result;
}

/* Calculate Adler-32 checksum */
static uint32_t adler32(const uint8_t *data, size_t len) {
    uint32_t a = 1, b = 0;
    const uint32_t MOD_ADLER = 65521;
    
    for (size_t i = 0; i < len; i++) {
        a = (a + data[i]) % MOD_ADLER;
        b = (b + a) % MOD_ADLER;
    }
    
    return (b << 16) | a;
}

int cupidimage_deflate_compress(const uint8_t *data, size_t size,
                                uint8_t *out, size_t outcap,
                                size_t *outlen) {
    if (outcap < 6) {
        return 0;
    }

    /* Zlib header */
    out[0] = 0x78;  /* CMF: deflate, 32k window */
    out[1] = 0x9C;  /* FLG: default compression, checksum */

    deflate_writer w = {0};
    w.out = out;
    w.outcap = outcap - 4;  /* reserve 4 bytes for Adler32 */
    w.pos = 2;

    /* DEFLATE block header: BFINAL=1, BTYPE=01 (static Huffman) */
    deflate_put_bits(&w, 0x01, 1);  /* final block */
    deflate_put_bits(&w, 0x01, 2);  /* static Huffman */

    /* Encode literals */
    for (size_t i = 0; i < size; i++) {
        uint8_t sym = data[i];
        uint32_t code = reverse_bits(static_lit_codes[sym], static_lit_lens[sym]);
        deflate_put_bits(&w, code, static_lit_lens[sym]);
        if (w.overflow) {
            return 0;
        }
    }

    /* End of block (code 256) */
    uint32_t eob_code = reverse_bits(static_lit_codes[256], static_lit_lens[256]);
    deflate_put_bits(&w, eob_code, static_lit_lens[256]);

    deflate_flush_bits(&w);
    
    if (w.overflow) {
        return 0;
    }

    /* Adler32 checksum (big-endian) */
    if (w.pos + 4 > outcap) {
        return 0;
    }
    uint32_t checksum = adler32(data, size);
    out[w.pos++] = (uint8_t)((checksum >> 24) & 0xFF);
    out[w.pos++] = (uint8_t)((checksum >> 16) & 0xFF);
    out[w.pos++] = (uint8_t)((checksum >> 8) & 0xFF);
    out[w.pos++] = (uint8_t)(checksum & 0xFF);

    *outlen = w.pos;
    return 1;
}
