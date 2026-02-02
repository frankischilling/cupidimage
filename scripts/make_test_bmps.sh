#!/usr/bin/env bash
set -euo pipefail

OUTDIR="bmp"
mkdir -p "$OUTDIR"

python3 - <<'PY'
import os, struct

outdir = os.environ.get("OUTDIR", "bmp")
os.makedirs(outdir, exist_ok=True)

def pack_file_header(file_size, off_bits, signature=b'BM'):
    return struct.pack('<2sIHHI', signature, file_size, 0, 0, off_bits)

def stride_bytes(width, bpp):
    return ((width * bpp + 31) // 32) * 4

def pack_info_header(width, height_signed, bpp, compression=0, size_image=0,
                     xppm=2835, yppm=2835, clr_used=0, clr_important=0, dib_size=40):
    return struct.pack('<IiiHHIIiiII',
                       dib_size, width, height_signed, 1, bpp, compression,
                       size_image, xppm, yppm, clr_used, clr_important)

def make_palette_bgra(entries):
    pal = b''
    for r, g, b in entries:
        pal += struct.pack('<BBBB', b & 255, g & 255, r & 255, 0)
    return pal

def palette_16():
    cols = [
        (0,0,0),(128,0,0),(0,128,0),(128,128,0),
        (0,0,128),(128,0,128),(0,128,128),(192,192,192),
        (128,128,128),(255,0,0),(0,255,0),(255,255,0),
        (0,0,255),(255,0,255),(0,255,255),(255,255,255)
    ]
    return make_palette_bgra(cols)

def palette_256():
    cols = []
    for i in range(256):
        r = (i & 0xE0)
        g = (i & 0x1C) << 3
        b = (i & 0x03) * 85
        cols.append((r, g, b))
    return make_palette_bgra(cols)

def pack_pixels_24(width, height, *, top_down=False, pattern="grad"):
    stride = stride_bytes(width, 24)
    pad = stride - width * 3
    rows = []
    for y in range(height):
        row = bytearray()
        for x in range(width):
            if pattern == "grad":
                r = int(255 * x / max(1, width - 1))
                g = int(255 * y / max(1, height - 1))
                b = (r ^ g) & 255
            elif pattern == "checker":
                v = 255 if ((x // 2 + y // 2) % 2) == 0 else 0
                r = g = b = v
            else:
                r = (x * 29 + y * 7) & 255
                g = (x * 3 + y * 31) & 255
                b = (x * 17 + y * 13) & 255
            row += bytes([b, g, r])  # BGR
        row += b'\x00' * pad
        rows.append(bytes(row))
    return b''.join(rows if top_down else rows[::-1])

def pack_pixels_32(width, height, *, top_down=False):
    rows = []
    for y in range(height):
        row = bytearray()
        for x in range(width):
            r = int(255 * x / max(1, width - 1))
            g = int(255 * y / max(1, height - 1))
            b = (r + g) // 2
            a = int(255 * ((x + y) / max(1, (width - 1) + (height - 1))))
            row += bytes([b, g, r, a])  # BGRA
        rows.append(bytes(row))
    return b''.join(rows if top_down else rows[::-1])

def pack_pixels_16_rgb555(width, height, *, top_down=False):
    stride = stride_bytes(width, 16)
    pad = stride - width * 2
    rows = []
    for y in range(height):
        row = bytearray()
        for x in range(width):
            r = int(255 * x / max(1, width - 1))
            g = int(255 * y / max(1, height - 1))
            b = (r ^ (g * 3)) & 255
            v = ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)  # 5-5-5
            row += struct.pack('<H', v)
        row += b'\x00' * pad
        rows.append(bytes(row))
    return b''.join(rows if top_down else rows[::-1])

def pack_pixels_16_rgb565(width, height, *, top_down=False):
    stride = stride_bytes(width, 16)
    pad = stride - width * 2
    rows = []
    for y in range(height):
        row = bytearray()
        for x in range(width):
            r = int(255 * x / max(1, width - 1))
            g = int(255 * y / max(1, height - 1))
            b = (x * 37 + y * 17) & 255
            v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)  # 5-6-5
            row += struct.pack('<H', v)
        row += b'\x00' * pad
        rows.append(bytes(row))
    return b''.join(rows if top_down else rows[::-1])

def pack_pixels_8(width, height, *, top_down=False, spread=False):
    stride = stride_bytes(width, 8)
    pad = stride - width
    rows = []
    for y in range(height):
        row = bytearray()
        for x in range(width):
            idx = ((x * 17 + y * 31) & 255) if spread else ((x + y) & 15)
            row.append(idx)
        row += b'\x00' * pad
        rows.append(bytes(row))
    return b''.join(rows if top_down else rows[::-1])

def pack_pixels_4(width, height, *, top_down=False):
    stride = stride_bytes(width, 4)
    row_bytes = (width + 1) // 2
    pad = stride - row_bytes
    rows = []
    for y in range(height):
        bts = bytearray()
        for x in range(0, width, 2):
            hi = (x + y) & 15
            lo = ((x + y + 1) & 15) if x + 1 < width else 0
            bts.append((hi << 4) | lo)
        bts += b'\x00' * pad
        rows.append(bytes(bts))
    return b''.join(rows if top_down else rows[::-1])

def pack_pixels_1(width, height, *, top_down=False):
    stride = stride_bytes(width, 1)
    row_bytes = (width + 7) // 8
    pad = stride - row_bytes
    rows = []
    for y in range(height):
        # checkerboard
        bits = [1 if ((x // 2 + y // 2) % 2) == 0 else 0 for x in range(width)]
        bts = bytearray()
        for i in range(0, width, 8):
            byte = 0
            for j in range(8):
                if i + j < width and bits[i + j]:
                    byte |= 1 << (7 - j)  # leftmost pixel in MSB
            bts.append(byte)
        bts += b'\x00' * pad
        rows.append(bytes(bts))
    return b''.join(rows if top_down else rows[::-1])

def write_bmp_info(path, width, height, bpp, pixel_data, *,
                   palette=b'', compression=0, clr_used=0, masks=b'',
                   dib_header=None, signature=b'BM', top_down=False, extra_tail=b''):
    h_signed = -height if top_down else height
    if dib_header is None:
        dib_header = pack_info_header(width, h_signed, bpp,
                                      compression=compression,
                                      size_image=len(pixel_data),
                                      clr_used=clr_used,
                                      dib_size=40)
    dib_size = struct.unpack('<I', dib_header[:4])[0]
    off_bits = 14 + dib_size + len(masks) + len(palette)
    file_size = off_bits + len(pixel_data) + len(extra_tail)

    with open(path, 'wb') as f:
        f.write(pack_file_header(file_size, off_bits, signature=signature))
        f.write(dib_header)
        if masks:
            f.write(masks)
        if palette:
            f.write(palette)
        f.write(pixel_data)
        if extra_tail:
            f.write(extra_tail)

def pack_core_header(width, height, bpp):
    return struct.pack('<IHHHH', 12, width, height, 1, bpp)

def write_bmp_core(path, width, height, bpp, pixel_data, *, palette=b'', signature=b'BM'):
    core = pack_core_header(width, height, bpp)
    off_bits = 14 + len(core) + len(palette)
    file_size = off_bits + len(pixel_data)
    with open(path, 'wb') as f:
        f.write(pack_file_header(file_size, off_bits, signature=signature))
        f.write(core)
        if palette:
            f.write(palette)
        f.write(pixel_data)

def rle8_encode_rows(rows):  # rows are in FILE order (bottom-up for positive height)
    out = bytearray()
    for row in rows:
        n = len(row)
        if n > 255:
            raise ValueError("row too wide for simple absolute RLE8")
        out += bytes([0, n])     # escape + absolute count
        out += row
        if n & 1:
            out += b'\x00'       # pad to word boundary
        out += b'\x00\x00'       # EOL
    out += b'\x00\x01'           # EOF
    return bytes(out)

def rle4_pack_row_nibbles(nibs):
    packed = bytearray()
    for i in range(0, len(nibs), 2):
        hi = nibs[i] & 15
        lo = (nibs[i + 1] & 15) if i + 1 < len(nibs) else 0
        packed.append((hi << 4) | lo)
    return bytes(packed)

def rle4_encode_rows(rows_nibs):
    out = bytearray()
    for nibs in rows_nibs:
        n = len(nibs)
        if n > 255:
            raise ValueError("row too wide for simple absolute RLE4")
        out += bytes([0, n])          # escape + absolute pixel count
        packed = rle4_pack_row_nibbles(nibs)
        out += packed
        if len(packed) & 1:
            out += b'\x00'            # pad to word boundary
        out += b'\x00\x00'            # EOL
    out += b'\x00\x01'                # EOF
    return bytes(out)

def pack_v4_header(width, height_signed, bpp, compression, size_image,
                   redmask, greenmask, bluemask, alphamask,
                   cs_type=0x73524742):  # 'sRGB'
    endpoints = b'\x00' * 36
    gamma = b'\x00' * 12
    return (struct.pack('<IiiHHIIiiIIIIIII',
                        108, width, height_signed, 1, bpp, compression, size_image,
                        2835, 2835, 0, 0,
                        redmask, greenmask, bluemask, alphamask, cs_type)
            + endpoints + gamma)

def pack_v5_header(width, height_signed, bpp, compression, size_image,
                   redmask, greenmask, bluemask, alphamask,
                   cs_type, intent, profile_data_offset, profile_size):
    endpoints = b'\x00' * 36
    gamma = b'\x00' * 12
    base = (struct.pack('<IiiHHIIiiIIIIIII',
                        124, width, height_signed, 1, bpp, compression, size_image,
                        2835, 2835, 0, 0,
                        redmask, greenmask, bluemask, alphamask, cs_type)
            + endpoints + gamma)
    tail = struct.pack('<IIII', intent, profile_data_offset, profile_size, 0)
    return base + tail

# -------------------------------
# Required Test Cases
# -------------------------------

# Bit Depth Coverage
write_bmp_info(os.path.join(outdir, '1bit-mono.bmp'),
               13, 9, 1, pack_pixels_1(13, 9),
               palette=make_palette_bgra([(0,0,0), (255,255,255)]), clr_used=2)

write_bmp_info(os.path.join(outdir, '4bit-16color.bmp'),
               17, 9, 4, pack_pixels_4(17, 9),
               palette=palette_16(), clr_used=16)

write_bmp_info(os.path.join(outdir, '8bit-256color.bmp'),
               32, 16, 8, pack_pixels_8(32, 16, spread=False),
               palette=palette_256(), clr_used=256)

write_bmp_info(os.path.join(outdir, '16bit-rgb555.bmp'),
               16, 16, 16, pack_pixels_16_rgb555(16, 16),
               compression=0)

write_bmp_info(os.path.join(outdir, '24bit-rgb.bmp'),
               16, 16, 24, pack_pixels_24(16, 16, pattern="grad"),
               compression=0)

write_bmp_info(os.path.join(outdir, '32bit-rgba.bmp'),
               16, 16, 32, pack_pixels_32(16, 16),
               compression=0)

# Compression Types
w, h = 16, 8
rows_top = [bytes(((x * 17 + y * 31) & 255) for x in range(w)) for y in range(h)]
rows_bottom = rows_top[::-1]  # bottom-up storage
rle8 = rle8_encode_rows(rows_bottom)
write_bmp_info(os.path.join(outdir, 'rle8-compressed.bmp'),
               w, h, 8, rle8,
               palette=palette_256(), clr_used=256, compression=1)

w, h = 16, 8
rows_nibs_top = [[(x + y * 3) & 15 for x in range(w)] for y in range(h)]
rows_nibs_bottom = rows_nibs_top[::-1]
rle4 = rle4_encode_rows(rows_nibs_bottom)
write_bmp_info(os.path.join(outdir, 'rle4-compressed.bmp'),
               w, h, 4, rle4,
               palette=palette_16(), clr_used=16, compression=2)

# BI_BITFIELDS with custom masks
masks16 = struct.pack('<III', 0xF800, 0x07E0, 0x001F)  # RGB565
write_bmp_info(os.path.join(outdir, 'bitfields-16bit.bmp'),
               16, 16, 16, pack_pixels_16_rgb565(16, 16),
               masks=masks16, compression=3)

# "BI_BITFIELDS" 32-bit with RGBA masks (writes 4 masks; common in the wild for parser tests)
masks32 = struct.pack('<IIII', 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
write_bmp_info(os.path.join(outdir, 'bitfields-32bit.bmp'),
               16, 16, 32, pack_pixels_32(16, 16),
               masks=masks32, compression=3)

# Header Versions
write_bmp_core(os.path.join(outdir, 'os2-v1.bmp'),
               7, 5, 24, pack_pixels_24(7, 5, pattern="checker"))

write_bmp_info(os.path.join(outdir, 'win3-v1.bmp'),
               7, 5, 24, pack_pixels_24(7, 5, pattern="checker"),
               compression=0)

# V4 header (colorspace info)
rm, gm, bm, am = 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000
pix32 = pack_pixels_32(8, 8)
v4 = pack_v4_header(8, 8, 32, 3, len(pix32), rm, gm, bm, am, cs_type=0x73524742)  # 'sRGB'
write_bmp_info(os.path.join(outdir, 'v4-header.bmp'),
               8, 8, 32, pix32,
               dib_header=v4, compression=3)

# V5 header (embedded ICC profile; dummy profile bytes)
icc = (b'ICC_PROFILE\x00' + b'\x00' * (128 - len(b'ICC_PROFILE\x00')))
pix32_2 = pack_pixels_32(8, 8)
profile_off = 124 + len(pix32_2)  # offset from start of BITMAPV5HEADER to profile data
v5 = pack_v5_header(8, 8, 32, 3, len(pix32_2), rm, gm, bm, am,
                    cs_type=0x4D424544,     # 'MBED' (PROFILE_EMBEDDED)
                    intent=4,               # LCS_GM_IMAGES
                    profile_data_offset=profile_off,
                    profile_size=len(icc))
write_bmp_info(os.path.join(outdir, 'v5-header.bmp'),
               8, 8, 32, pix32_2,
               dib_header=v5, compression=3, extra_tail=icc)

# Orientation
write_bmp_info(os.path.join(outdir, 'bottom-up.bmp'),
               9, 7, 24, pack_pixels_24(9, 7, pattern="grad"),
               compression=0, top_down=False)

write_bmp_info(os.path.join(outdir, 'top-down.bmp'),
               9, 7, 24, pack_pixels_24(9, 7, top_down=True, pattern="grad"),
               compression=0, top_down=True)

# Edge Cases
write_bmp_info(os.path.join(outdir, '1x1.bmp'),
               1, 1, 24, pack_pixels_24(1, 1),
               compression=0)

write_bmp_info(os.path.join(outdir, 'odd-width.bmp'),
               3, 2, 24, pack_pixels_24(3, 2, pattern="checker"),
               compression=0)

write_bmp_info(os.path.join(outdir, 'large-palette.bmp'),
               32, 16, 8, pack_pixels_8(32, 16, spread=True),
               palette=palette_256(), clr_used=256, compression=0)

# Validation Tests (should fail)
good = open(os.path.join(outdir, '24bit-rgb.bmp'), 'rb').read()

with open(os.path.join(outdir, 'invalid-signature.bmp'), 'wb') as f:
    f.write(b'ZZ' + good[2:])

with open(os.path.join(outdir, 'truncated.bmp'), 'wb') as f:
    f.write(good[:40])  # cut short

# huge dimensions: claims enormous width/height, no pixel data
W, H = 70000, 70000
dib = pack_info_header(W, H, 24, compression=0, size_image=0, clr_used=0, dib_size=40)
off_bits = 14 + 40
fh = pack_file_header(off_bits, off_bits, signature=b'BM')
with open(os.path.join(outdir, 'huge-dimensions.bmp'), 'wb') as f:
    f.write(fh)
    f.write(dib)

PY

echo "Wrote BMP test files into ./$OUTDIR/"
