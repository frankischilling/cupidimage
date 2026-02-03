#!/usr/bin/env bash
set -euo pipefail

OUTDIR="assets/tga"
mkdir -p "$OUTDIR"

OUTDIR="$OUTDIR" python3 - <<'PY'
import os
import struct
from pathlib import Path

outdir = Path(os.environ.get("OUTDIR", "assets/tga"))
outdir.mkdir(parents=True, exist_ok=True)

SIGNATURE = b"TRUEVISION-XFILE.\0"


def pack_header(*, id_length, color_map_type, image_type,
                cm_first, cm_length, cm_entry_size,
                x_origin, y_origin, width, height,
                pixel_depth, descriptor):
    return struct.pack(
        '<BBBHHBHHHHBB',
        id_length, color_map_type, image_type,
        cm_first, cm_length, cm_entry_size,
        x_origin, y_origin, width, height,
        pixel_depth, descriptor
    )


def iter_coords(width, height, origin):
    y_range = range(height - 1, -1, -1) if origin in (0, 1) else range(height)
    x_range = range(width - 1, -1, -1) if origin in (1, 3) else range(width)
    for y in y_range:
        for x in x_range:
            yield x, y


def rgba_pattern(width, height):
    pixels = []
    denom_x = max(1, width - 1)
    denom_y = max(1, height - 1)
    denom_a = max(1, (width - 1) + (height - 1))
    for y in range(height):
        row = []
        for x in range(width):
            r = int(255 * x / denom_x)
            g = int(255 * y / denom_y)
            b = (r ^ g) & 255
            a = int(255 * (x + y) / denom_a)
            row.append((r, g, b, a))
        pixels.append(row)
    return pixels


def gray_pattern(width, height):
    pixels = []
    denom_x = max(1, width - 1)
    denom_y = max(1, height - 1)
    for y in range(height):
        row = []
        for x in range(width):
            v = int(255 * (x + y) / (denom_x + denom_y))
            row.append(v)
        pixels.append(row)
    return pixels


def encode_palette(entries, entry_size_bits):
    out = bytearray()
    for r, g, b, a in entries:
        if entry_size_bits == 15:
            v = ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)
            out += struct.pack('<H', v)
        elif entry_size_bits == 16:
            v = ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)
            if a >= 128:
                v |= 0x8000
            out += struct.pack('<H', v)
        elif entry_size_bits == 24:
            out += bytes([b, g, r])
        elif entry_size_bits == 32:
            out += bytes([b, g, r, a])
        else:
            raise ValueError("unsupported palette entry size")
    return bytes(out)


def encode_pixels_rgba(pixels, width, height, origin, pixel_depth, *, mode=None):
    out = bytearray()
    for x, y in iter_coords(width, height, origin):
        r, g, b, a = pixels[y][x]
        if pixel_depth == 15:
            v = ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)
            out += struct.pack('<H', v)
        elif pixel_depth == 16:
            if mode == "565":
                v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
            else:
                v = ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)
                if a >= 128:
                    v |= 0x8000
            out += struct.pack('<H', v)
        elif pixel_depth == 24:
            out += bytes([b, g, r])
        elif pixel_depth == 32:
            out += bytes([b, g, r, a])
        else:
            raise ValueError("unsupported pixel depth")
    return bytes(out)


def encode_pixels_gray(pixels, width, height, origin, pixel_depth):
    out = bytearray()
    for x, y in iter_coords(width, height, origin):
        v = pixels[y][x]
        if pixel_depth == 8:
            out.append(v & 255)
        elif pixel_depth == 16:
            out += struct.pack('<H', v * 257)
        else:
            raise ValueError("unsupported gray depth")
    return bytes(out)


def encode_indices(indices, width, height, origin, pixel_depth):
    out = bytearray()
    for x, y in iter_coords(width, height, origin):
        idx = indices[y][x]
        if pixel_depth == 8:
            out.append(idx & 255)
        elif pixel_depth == 16:
            out += struct.pack('<H', idx & 0xFFFF)
        else:
            raise ValueError("unsupported index depth")
    return bytes(out)


def rle_encode(raw_bytes, bytes_per_pixel):
    pixels = [raw_bytes[i * bytes_per_pixel:(i + 1) * bytes_per_pixel]
              for i in range(len(raw_bytes) // bytes_per_pixel)]
    out = bytearray()
    i = 0
    n = len(pixels)
    while i < n:
        run_len = 1
        while i + run_len < n and pixels[i + run_len] == pixels[i] and run_len < 128:
            run_len += 1
        if run_len >= 2:
            out.append(0x80 | (run_len - 1))
            out += pixels[i]
            i += run_len
            continue
        start = i
        i += 1
        while i < n:
            run_len = 1
            while i + run_len < n and pixels[i + run_len] == pixels[i] and run_len < 128:
                run_len += 1
            if run_len >= 2 or (i - start) >= 128:
                break
            i += 1
        count = i - start
        out.append(count - 1)
        for j in range(start, start + count):
            out += pixels[j]
    return bytes(out)


def make_extension(*, author="", comments="", timestamp=None,
                   job_id="", job_time=None, software_id="",
                   version_num=0, version_letter=0,
                   key_color=0, par=(0, 0), gamma=(0, 0),
                   color_corr_offset=0, postage_offset=0,
                   scanline_offset=0, attributes_type=0):
    ext = bytearray(495)
    ext[0:2] = struct.pack('<H', 495)
    ext[2:43] = author.encode('ascii', 'ignore')[:41].ljust(41, b'\x00')
    ext[43:367] = comments.encode('ascii', 'ignore')[:324].ljust(324, b'\x00')
    if timestamp is None:
        timestamp = (0, 0, 0, 0, 0, 0)
    for i, v in enumerate(timestamp):
        ext[367 + i * 2:369 + i * 2] = struct.pack('<H', v)
    ext[379:420] = job_id.encode('ascii', 'ignore')[:41].ljust(41, b'\x00')
    if job_time is None:
        job_time = (0, 0, 0)
    for i, v in enumerate(job_time):
        ext[420 + i * 2:422 + i * 2] = struct.pack('<H', v)
    ext[426:467] = software_id.encode('ascii', 'ignore')[:41].ljust(41, b'\x00')
    ext[467:469] = struct.pack('<H', version_num)
    ext[469] = version_letter
    ext[470:474] = struct.pack('<I', key_color)
    ext[474:476] = struct.pack('<H', par[0])
    ext[476:478] = struct.pack('<H', par[1])
    ext[478:480] = struct.pack('<H', gamma[0])
    ext[480:482] = struct.pack('<H', gamma[1])
    ext[482:486] = struct.pack('<I', color_corr_offset)
    ext[486:490] = struct.pack('<I', postage_offset)
    ext[490:494] = struct.pack('<I', scanline_offset)
    ext[494] = attributes_type
    return bytes(ext)


def write_tga(path, *, header, id_bytes=b"", cmap=b"", image=b"",
              extension=None, cc_table=None, postage=None):
    data = bytearray()
    data += header
    data += id_bytes
    data += cmap
    data += image

    cc_offset = 0
    if cc_table:
        cc_offset = len(data)
        data += cc_table

    postage_offset = 0
    if postage:
        postage_offset = len(data)
        data += postage

    if extension is not None:
        ext_offset = len(data)
        ext = make_extension(**extension, color_corr_offset=cc_offset, postage_offset=postage_offset)
        data += ext
        data += struct.pack('<II', ext_offset, 0)
        data += SIGNATURE

    path.write_bytes(data)


def palette_256():
    entries = []
    for i in range(256):
        r = (i & 0xE0)
        g = (i & 0x1C) << 3
        b = (i & 0x03) * 85
        entries.append((r, g, b, 255))
    return entries


def palette_16():
    cols = [
        (0, 0, 0, 255), (128, 0, 0, 255), (0, 128, 0, 255), (128, 128, 0, 255),
        (0, 0, 128, 255), (128, 0, 128, 255), (0, 128, 128, 255), (192, 192, 192, 255),
        (128, 128, 128, 255), (255, 0, 0, 255), (0, 255, 0, 255), (255, 255, 0, 255),
        (0, 0, 255, 255), (255, 0, 255, 255), (0, 255, 255, 255), (255, 255, 255, 255)
    ]
    return cols


def index_pattern(width, height, colors):
    indices = []
    for y in range(height):
        row = []
        for x in range(width):
            row.append((x + y) % colors)
        indices.append(row)
    return indices


def make_cc_table():
    out = bytearray()
    for i in range(256):
        r = min(255, int(i * 1.1))
        g = min(255, int(i * 0.85))
        b = min(255, int(i * 0.6))
        a = i
        out += struct.pack('<H', a * 257)
        out += struct.pack('<H', r * 257)
        out += struct.pack('<H', g * 257)
        out += struct.pack('<H', b * 257)
    return bytes(out)


def make_postage(pixels):
    h = len(pixels)
    w = len(pixels[0]) if h else 0
    data = bytearray([w & 255, h & 255])
    for y in range(h):
        for x in range(w):
            r, g, b, _ = pixels[y][x]
            data += bytes([b, g, r])
    return bytes(data)


# Core format coverage
w, h = 16, 10
rgba = rgba_pattern(w, h)

gray = gray_pattern(w, h)

# Type 0
header = pack_header(id_length=0, color_map_type=0, image_type=0,
                     cm_first=0, cm_length=0, cm_entry_size=0,
                     x_origin=0, y_origin=0, width=0, height=0,
                     pixel_depth=0, descriptor=0)
write_tga(outdir / "type0-no-image.tga", header=header)

# Type 1 color mapped 8-bit
pal = palette_256()
indices = index_pattern(w, h, 256)
image = encode_indices(indices, w, h, origin=0, pixel_depth=8)
header = pack_header(id_length=0, color_map_type=1, image_type=1,
                     cm_first=0, cm_length=256, cm_entry_size=24,
                     x_origin=0, y_origin=0, width=w, height=h,
                     pixel_depth=8, descriptor=0)
write_tga(outdir / "type1-colormap-8bit.tga", header=header,
          cmap=encode_palette(pal, 24), image=image)

# Type 2 truecolor 24-bit
image = encode_pixels_rgba(rgba, w, h, origin=0, pixel_depth=24)
header = pack_header(id_length=0, color_map_type=0, image_type=2,
                     cm_first=0, cm_length=0, cm_entry_size=0,
                     x_origin=0, y_origin=0, width=w, height=h,
                     pixel_depth=24, descriptor=0)
write_tga(outdir / "type2-truecolor-24bit.tga", header=header, image=image)

# Type 2 truecolor 32-bit
image = encode_pixels_rgba(rgba, w, h, origin=0, pixel_depth=32)
header = pack_header(id_length=0, color_map_type=0, image_type=2,
                     cm_first=0, cm_length=0, cm_entry_size=0,
                     x_origin=0, y_origin=0, width=w, height=h,
                     pixel_depth=32, descriptor=8)
write_tga(outdir / "type2-truecolor-32bit.tga", header=header, image=image)

# Type 3 grayscale 8-bit
image = encode_pixels_gray(gray, w, h, origin=0, pixel_depth=8)
header = pack_header(id_length=0, color_map_type=0, image_type=3,
                     cm_first=0, cm_length=0, cm_entry_size=0,
                     x_origin=0, y_origin=0, width=w, height=h,
                     pixel_depth=8, descriptor=0)
write_tga(outdir / "type3-grayscale-8bit.tga", header=header, image=image)

# Type 3 grayscale 16-bit
image = encode_pixels_gray(gray, w, h, origin=0, pixel_depth=16)
header = pack_header(id_length=0, color_map_type=0, image_type=3,
                     cm_first=0, cm_length=0, cm_entry_size=0,
                     x_origin=0, y_origin=0, width=w, height=h,
                     pixel_depth=16, descriptor=0)
write_tga(outdir / "type3-grayscale-16bit.tga", header=header, image=image)

# Type 9 RLE color mapped
raw = encode_indices(indices, w, h, origin=0, pixel_depth=8)
image = rle_encode(raw, 1)
header = pack_header(id_length=0, color_map_type=1, image_type=9,
                     cm_first=0, cm_length=256, cm_entry_size=24,
                     x_origin=0, y_origin=0, width=w, height=h,
                     pixel_depth=8, descriptor=0)
write_tga(outdir / "type9-rle-colormap.tga", header=header,
          cmap=encode_palette(pal, 24), image=image)

# Type 10 RLE truecolor 24-bit
raw = encode_pixels_rgba(rgba, w, h, origin=0, pixel_depth=24)
image = rle_encode(raw, 3)
header = pack_header(id_length=0, color_map_type=0, image_type=10,
                     cm_first=0, cm_length=0, cm_entry_size=0,
                     x_origin=0, y_origin=0, width=w, height=h,
                     pixel_depth=24, descriptor=0)
write_tga(outdir / "type10-rle-truecolor.tga", header=header, image=image)

# Type 11 RLE grayscale
raw = encode_pixels_gray(gray, w, h, origin=0, pixel_depth=8)
image = rle_encode(raw, 1)
header = pack_header(id_length=0, color_map_type=0, image_type=11,
                     cm_first=0, cm_length=0, cm_entry_size=0,
                     x_origin=0, y_origin=0, width=w, height=h,
                     pixel_depth=8, descriptor=0)
write_tga(outdir / "type11-rle-grayscale.tga", header=header, image=image)

# Bit depth variants
image = encode_pixels_rgba(rgba, w, h, origin=0, pixel_depth=15)
header = pack_header(id_length=0, color_map_type=0, image_type=2,
                     cm_first=0, cm_length=0, cm_entry_size=0,
                     x_origin=0, y_origin=0, width=w, height=h,
                     pixel_depth=15, descriptor=0)
write_tga(outdir / "15bit-rgb555.tga", header=header, image=image)

image = encode_pixels_rgba(rgba, w, h, origin=0, pixel_depth=16, mode="565")
header = pack_header(id_length=0, color_map_type=0, image_type=2,
                     cm_first=0, cm_length=0, cm_entry_size=0,
                     x_origin=0, y_origin=0, width=w, height=h,
                     pixel_depth=16, descriptor=0)
write_tga(outdir / "16bit-rgb565.tga", header=header, image=image)

indices16 = index_pattern(w, h, 16)
image = encode_indices(indices16, w, h, origin=0, pixel_depth=16)
header = pack_header(id_length=0, color_map_type=1, image_type=1,
                     cm_first=0, cm_length=16, cm_entry_size=24,
                     x_origin=0, y_origin=0, width=w, height=h,
                     pixel_depth=16, descriptor=0)
write_tga(outdir / "16bit-colormap.tga", header=header,
          cmap=encode_palette(palette_16(), 24), image=image)

# Origin/orientation
origins = {
    "origin-bottom-left.tga": 0,
    "origin-bottom-right.tga": 1,
    "origin-top-left.tga": 2,
    "origin-top-right.tga": 3,
}
for name, origin in origins.items():
    descriptor = (origin << 4) & 0x30
    image = encode_pixels_rgba(rgba, w, h, origin=origin, pixel_depth=24)
    header = pack_header(id_length=0, color_map_type=0, image_type=2,
                         cm_first=0, cm_length=0, cm_entry_size=0,
                         x_origin=0, y_origin=0, width=w, height=h,
                         pixel_depth=24, descriptor=descriptor)
    write_tga(outdir / name, header=header, image=image)

# TGA 2.0 extensions
meta = {
    "author": "CupidImage",
    "comments": "Test TGA v2 metadata\nLine2\nLine3\nLine4",
    "timestamp": (2, 3, 2026, 12, 34, 56),
    "job_id": "JOB-42",
    "job_time": (1, 2, 3),
    "software_id": "cupidimage",
    "version_num": 100,
    "version_letter": ord('a'),
    "key_color": 0xFF00FF00,
    "par": (1, 1),
    "gamma": (0, 0),
    "attributes_type": 3,
}
image = encode_pixels_rgba(rgba, w, h, origin=0, pixel_depth=24)
header = pack_header(id_length=0, color_map_type=0, image_type=2,
                     cm_first=0, cm_length=0, cm_entry_size=0,
                     x_origin=0, y_origin=0, width=w, height=h,
                     pixel_depth=24, descriptor=0)
write_tga(outdir / "v2-with-metadata.tga", header=header, image=image, extension=meta)

thumb = make_postage(rgba_pattern(8, 8))
meta_thumb = dict(meta)
image = encode_pixels_rgba(rgba, w, h, origin=0, pixel_depth=24)
write_tga(outdir / "v2-with-thumbnail.tga", header=header, image=image,
          extension=meta_thumb, postage=thumb)

meta_gamma = dict(meta)
meta_gamma["gamma"] = (22, 10)
image = encode_pixels_rgba(rgba, w, h, origin=0, pixel_depth=24)
write_tga(outdir / "v2-with-gamma.tga", header=header, image=image,
          extension=meta_gamma)

meta_cc = dict(meta)
cc_table = make_cc_table()
image = encode_pixels_rgba(rgba, w, h, origin=0, pixel_depth=24)
write_tga(outdir / "v2-with-colorcorrect.tga", header=header, image=image,
          extension=meta_cc, cc_table=cc_table)

# Edge cases
image = encode_pixels_rgba(rgba_pattern(1, 1), 1, 1, origin=0, pixel_depth=24)
header = pack_header(id_length=0, color_map_type=0, image_type=2,
                     cm_first=0, cm_length=0, cm_entry_size=0,
                     x_origin=0, y_origin=0, width=1, height=1,
                     pixel_depth=24, descriptor=0)
write_tga(outdir / "1x1-minimal.tga", header=header, image=image)

image = encode_pixels_rgba(rgba_pattern(13, 7), 13, 7, origin=0, pixel_depth=24)
header = pack_header(id_length=0, color_map_type=0, image_type=2,
                     cm_first=0, cm_length=0, cm_entry_size=0,
                     x_origin=0, y_origin=0, width=13, height=7,
                     pixel_depth=24, descriptor=0)
write_tga(outdir / "odd-dimensions.tga", header=header, image=image)

# RLE mixed packets
mix_pixels = rgba_pattern(w, h)
# force some repeats
for y in range(h):
    for x in range(0, w, 4):
        mix_pixels[y][x] = mix_pixels[y][0]
raw = encode_pixels_rgba(mix_pixels, w, h, origin=0, pixel_depth=24)
image = rle_encode(raw, 3)
header = pack_header(id_length=0, color_map_type=0, image_type=10,
                     cm_first=0, cm_length=0, cm_entry_size=0,
                     x_origin=0, y_origin=0, width=w, height=h,
                     pixel_depth=24, descriptor=0)
write_tga(outdir / "rle-mixed-packets.tga", header=header, image=image)

# Large image ID
id_bytes = bytes([i & 255 for i in range(255)])
image = encode_pixels_rgba(rgba, w, h, origin=0, pixel_depth=24)
header = pack_header(id_length=len(id_bytes), color_map_type=0, image_type=2,
                     cm_first=0, cm_length=0, cm_entry_size=0,
                     x_origin=0, y_origin=0, width=w, height=h,
                     pixel_depth=24, descriptor=0)
write_tga(outdir / "large-imageid.tga", header=header, id_bytes=id_bytes, image=image)

# Validation failures
header = pack_header(id_length=0, color_map_type=0, image_type=4,
                     cm_first=0, cm_length=0, cm_entry_size=0,
                     x_origin=0, y_origin=0, width=w, height=h,
                     pixel_depth=24, descriptor=0)
write_tga(outdir / "invalid-type.tga", header=header)

header = pack_header(id_length=0, color_map_type=0, image_type=2,
                     cm_first=0, cm_length=0, cm_entry_size=0,
                     x_origin=0, y_origin=0, width=w, height=h,
                     pixel_depth=24, descriptor=0)
write_tga(outdir / "invalid-signature.tga", header=header, image=image,
          extension={"author": "bad", "attributes_type": 3, "gamma": (0, 0)},
          cc_table=b"" )
# corrupt footer signature
p = outdir / "invalid-signature.tga"
blob = bytearray(p.read_bytes())
if len(blob) >= 18:
    blob[-18:] = b"INVALID-SIGNATURE."
    p.write_bytes(blob)

# Truncated pixel data
p = outdir / "truncated.tga"
header = pack_header(id_length=0, color_map_type=0, image_type=2,
                     cm_first=0, cm_length=0, cm_entry_size=0,
                     x_origin=0, y_origin=0, width=w, height=h,
                     pixel_depth=24, descriptor=0)
write_tga(p, header=header, image=image[:10])

# Huge dimensions
header = pack_header(id_length=0, color_map_type=0, image_type=2,
                     cm_first=0, cm_length=0, cm_entry_size=0,
                     x_origin=0, y_origin=0, width=65535, height=65535,
                     pixel_depth=24, descriptor=0)
write_tga(outdir / "huge-dimensions.tga", header=header)

# RLE overflow (declares more pixels than available)
raw = encode_pixels_rgba(rgba, w, h, origin=0, pixel_depth=24)
# craft invalid RLE packet
bad_rle = bytes([0xFF]) + raw[:3]
header = pack_header(id_length=0, color_map_type=0, image_type=10,
                     cm_first=0, cm_length=0, cm_entry_size=0,
                     x_origin=0, y_origin=0, width=w, height=h,
                     pixel_depth=24, descriptor=0)
write_tga(outdir / "rle-overflow.tga", header=header, image=bad_rle)

print(f"Wrote TGA assets to {outdir}")
PY
