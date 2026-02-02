#!/usr/bin/env bash
set -euo pipefail

OUTDIR="tiff"
VENV=".tiffgen-venv"

mkdir -p "$OUTDIR"

if [[ ! -d "$VENV" ]]; then
  python3 -m venv "$VENV"
fi

# shellcheck disable=SC1091
source "$VENV/bin/activate"
python -m pip -q install --upgrade pip
python -m pip -q install numpy tifffile imagecodecs

OUTDIR="$OUTDIR" python - <<'PY'
import os
from pathlib import Path
import struct
import numpy as np
import tifffile as tf

outdir = Path(os.environ.get("OUTDIR", "tiff"))
outdir.mkdir(parents=True, exist_ok=True)

def save(name: str, data, **kw):
    path = outdir / name
    tf.imwrite(path, data, metadata=None, **kw)
    return path

def save_or_skip(name: str, data, **kw):
    try:
        return save(name, data, **kw)
    except NotImplementedError as e:
        (outdir / f"{name}.SKIPPED.txt").write_text(f"Skipped: {e}\n")
        return None

def rgb_pattern_u8(h, w):
    y, x = np.mgrid[0:h, 0:w]
    r = (x % 256).astype(np.uint8)
    g = (y % 256).astype(np.uint8)
    b = ((x ^ y) % 256).astype(np.uint8)
    return np.stack([r, g, b], axis=-1)

def rgba_pattern_u8(h, w):
    rgb = rgb_pattern_u8(h, w)
    a = (np.linspace(0, 255, w, dtype=np.uint8)[None, :]).repeat(h, axis=0)
    return np.concatenate([rgb, a[..., None]], axis=-1)

def gray_pattern_u8(h, w):
    x = np.linspace(0, 255, w, dtype=np.uint8)[None, :].repeat(h, axis=0)
    y = np.linspace(0, 255, h, dtype=np.uint8)[:, None].repeat(w, axis=1)
    return ((x // 2) + (y // 2)).astype(np.uint8)

def to_u16(img_u8):
    return (img_u8.astype(np.uint16) * 257)

def palette_colormap_256():
    cmap = np.zeros((3, 256), dtype=np.uint16)
    cmap[0] = np.linspace(0, 65535, 256, dtype=np.uint16)
    cmap[1] = np.linspace(65535, 0, 256, dtype=np.uint16)
    cmap[2] = np.roll(cmap[0], 128)
    return cmap

def indexed_pattern(h, w, levels):
    y, x = np.mgrid[0:h, 0:w]
    idx = ((x // max(1, (w // levels))) + (y // max(1, (h // levels)))) % levels
    return idx.astype(np.uint8)

def bilevel_pattern(h, w):
    y, x = np.mgrid[0:h, 0:w]
    return ((x ^ y) & 1).astype(np.uint8)

def write_minimal_gray8_tiff(path: Path, w: int, h: int, byteorder: str):
    assert byteorder in ("II", "MM")
    le = (byteorder == "II")
    endian = "<" if le else ">"
    magic = 42

    data = gray_pattern_u8(h, w).tobytes(order="C")
    header_size = 8
    pixel_offset = header_size
    ifd_offset = pixel_offset + len(data)

    if ifd_offset % 2:
        pad = b"\x00"
        ifd_offset += 1
    else:
        pad = b""

    TYPE_SHORT = 3
    TYPE_LONG = 4

    entries = []
    def ifd_entry(tag, typ, count, value):
        entries.append((tag, typ, count, value))

    ifd_entry(256, TYPE_LONG, 1, w)            # ImageWidth
    ifd_entry(257, TYPE_LONG, 1, h)            # ImageLength
    ifd_entry(258, TYPE_SHORT, 1, 8)           # BitsPerSample
    ifd_entry(259, TYPE_SHORT, 1, 1)           # Compression (none)
    ifd_entry(262, TYPE_SHORT, 1, 1)           # Photometric (minisblack)
    ifd_entry(273, TYPE_LONG, 1, pixel_offset) # StripOffsets
    ifd_entry(277, TYPE_SHORT, 1, 1)           # SamplesPerPixel
    ifd_entry(278, TYPE_LONG, 1, h)            # RowsPerStrip
    ifd_entry(279, TYPE_LONG, 1, len(data))    # StripByteCounts

    with path.open("wb") as f:
        f.write(byteorder.encode("ascii"))
        f.write(struct.pack(endian + "H", magic))
        f.write(struct.pack(endian + "I", ifd_offset))
        f.write(data)
        f.write(pad)

        f.write(struct.pack(endian + "H", len(entries)))
        for tag, typ, count, val in entries:
            f.write(struct.pack(endian + "H", tag))
            f.write(struct.pack(endian + "H", typ))
            f.write(struct.pack(endian + "I", count))
            if typ == TYPE_SHORT and count == 1:
                if le:
                    f.write(struct.pack(endian + "H", val) + b"\x00\x00")
                else:
                    f.write(b"\x00\x00" + struct.pack(endian + "H", val))
            else:
                f.write(struct.pack(endian + "I", val))
        f.write(struct.pack(endian + "I", 0))

# ----------------------------
# Phase 1: Core coverage
# ----------------------------

H, W = 64, 96

g8  = gray_pattern_u8(H, W)
g16 = to_u16(g8)

rgb8  = rgb_pattern_u8(H, W)
rgb16 = to_u16(rgb8)

rgba8  = rgba_pattern_u8(H, W)
rgba16 = to_u16(rgba8)

# Uncompressed RGB/RGBA/Grayscale at 8-bit and 16-bit (both byte orders)
for bo, bo_name in (("<", "ii"), (">", "mm")):
    save(f"uncompressed-gray8-{bo_name}.tif",  g8,  photometric="minisblack", compression=None, byteorder=bo, rowsperstrip=H)
    save(f"uncompressed-gray16-{bo_name}.tif", g16, photometric="minisblack", compression=None, byteorder=bo, rowsperstrip=H)

    save(f"uncompressed-rgb8-{bo_name}.tif",   rgb8,  photometric="rgb", compression=None, byteorder=bo, rowsperstrip=H)
    save(f"uncompressed-rgb16-{bo_name}.tif",  rgb16, photometric="rgb", compression=None, byteorder=bo, rowsperstrip=H)

    # NOTE: tifffile extrasamples enum names: 'unassalpha' (2) or 'assocalpha' (1)
    save(f"uncompressed-rgba8-{bo_name}.tif",  rgba8,  photometric="rgb",
         extrasamples="unassalpha", compression=None, byteorder=bo, rowsperstrip=H)
    save(f"uncompressed-rgba16-{bo_name}.tif", rgba16, photometric="rgb",
         extrasamples="unassalpha", compression=None, byteorder=bo, rowsperstrip=H)

# LZW-compressed images
lzw_rep = np.tile(gray_pattern_u8(64, 2048), (16, 1))
rng = np.random.default_rng(123)
lzw_rnd = rng.integers(0, 256, size=(512, 1024), dtype=np.uint8)

save("lzw-gray8-repetitive-ii.tif", lzw_rep, photometric="minisblack", compression="lzw", byteorder="<", rowsperstrip=32)
save("lzw-gray8-random-ii.tif",     lzw_rnd, photometric="minisblack", compression="lzw", byteorder="<", rowsperstrip=32)

save("lzw-rgb8-ii.tif",  rgb8,  photometric="rgb", compression="lzw", byteorder="<", rowsperstrip=16)
save("lzw-rgba8-ii.tif", rgba8, photometric="rgb", extrasamples="unassalpha", compression="lzw", byteorder="<", rowsperstrip=16)

# PackBits-compressed images
save("packbits-gray8-ii.tif", g8,    photometric="minisblack", compression="packbits", byteorder="<", rowsperstrip=8)
save("packbits-rgb8-ii.tif",  rgb8,  photometric="rgb",        compression="packbits", byteorder="<", rowsperstrip=8)
save("packbits-rgba8-ii.tif", rgba8, photometric="rgb", extrasamples="unassalpha", compression="packbits", byteorder="<", rowsperstrip=8)

# Palette images (1/2/4/8-bit depths)
cmap = palette_colormap_256()
for bits, levels in ((1, 2), (2, 4), (4, 16), (8, 256)):
    idx = indexed_pattern(64, 64, levels)
    writer = save_or_skip if bits < 8 else save
    writer(f"palette-{bits}bit-ii.tif", idx, photometric="palette",
           colormap=cmap, bitspersample=bits, compression=None, byteorder="<", rowsperstrip=32)

# Bilevel images (1-bit, both photometric interpretations)
bilevel = bilevel_pattern(64, 64).astype(np.uint8)
save_or_skip("bilevel-1bit-minisblack-ii.tif", bilevel, photometric="minisblack", bitspersample=1, compression=None, byteorder="<", rowsperstrip=64)
save_or_skip("bilevel-1bit-miniswhite-ii.tif", bilevel, photometric="miniswhite", bitspersample=1, compression=None, byteorder="<", rowsperstrip=64)

# Strip configurations
base = gray_pattern_u8(80, 113)
save("strips-single-strip-ii.tif",   base, photometric="minisblack", compression="lzw", byteorder="<", rowsperstrip=80)
save("strips-rowsperstrip-1-ii.tif", base, photometric="minisblack", compression="lzw", byteorder="<", rowsperstrip=1)
save("strips-rowsperstrip-7-ii.tif", base, photometric="minisblack", compression="lzw", byteorder="<", rowsperstrip=7)
save("strips-rowsperstrip-32-ii.tif",base, photometric="minisblack", compression="lzw", byteorder="<", rowsperstrip=32)

# Edge cases
save("edge-1x1-gray8-ii.tif", np.array([[128]], dtype=np.uint8), photometric="minisblack", compression=None, byteorder="<", rowsperstrip=1)
save("edge-wide-100000x1-gray8-ii.tif", gray_pattern_u8(1, 100000), photometric="minisblack", compression=None, byteorder="<", rowsperstrip=1)
save("edge-tall-1x100000-gray8-ii.tif", gray_pattern_u8(100000, 1), photometric="minisblack", compression=None, byteorder="<", rowsperstrip=128)

# Missing optional tags (hand-written minimal TIFFs)
write_minimal_gray8_tiff(outdir / "missing-optional-tags-gray8-ii.tif", 17, 9, "II")
write_minimal_gray8_tiff(outdir / "missing-optional-tags-gray8-mm.tif", 17, 9, "MM")

# ----------------------------
# Phase 2+ (best-effort extras)
# ----------------------------

save("deflate-gray8-predictor2-ii.tif", g8, photometric="minisblack", compression="deflate", predictor=2, byteorder="<", rowsperstrip=16)
save("deflate-rgb8-predictor2-ii.tif",  rgb8, photometric="rgb",       compression="deflate", predictor=2, byteorder="<", rowsperstrip=16)
save("lzw-gray8-predictor2-ii.tif",     g8, photometric="minisblack", compression="lzw", predictor=2, byteorder="<", rowsperstrip=16)

pages = [gray_pattern_u8(64, 64),
         np.rot90(gray_pattern_u8(64, 64)),
         rng.integers(0, 256, size=(64, 64), dtype=np.uint8)]
tf.imwrite(outdir / "multipage-gray8-lzw-ii.tif", pages, photometric="minisblack", compression="lzw", byteorder="<", metadata=None)

save("tiled-rgb8-none-ii.tif",     rgb8, photometric="rgb", compression=None,     byteorder="<", tile=(64, 64))
save("tiled-rgb8-deflate-ii.tif",  rgb8, photometric="rgb", compression="deflate", predictor=2, byteorder="<", tile=(64, 64))

def rgb_to_ycbcr_u8(rgb):
    r = rgb[..., 0].astype(np.float32)
    g = rgb[..., 1].astype(np.float32)
    b = rgb[..., 2].astype(np.float32)
    y  =  0.299*r + 0.587*g + 0.114*b
    cb = -0.168736*r - 0.331264*g + 0.5*b + 128.0
    cr =  0.5*r - 0.418688*g - 0.081312*b + 128.0
    ycbcr = np.stack([y, cb, cr], axis=-1)
    return np.clip(np.round(ycbcr), 0, 255).astype(np.uint8)

ycbcr = rgb_to_ycbcr_u8(rgb_pattern_u8(256, 256))
for subs, name in [((2,2), "420"), ((2,1), "422"), ((1,1), "444")]:
    try:
        save(f"ycbcr-{name}-jpeg-ii.tif", ycbcr, photometric="ycbcr", compression="jpeg",
             subsampling=subs, byteorder="<", rowsperstrip=16)
    except Exception as e:
        (outdir / f"ycbcr-{name}-jpeg-ii.SKIPPED.txt").write_text(f"Skipped: {e}\n")

(outdir / "MANIFEST.txt").write_text("\n".join(sorted(p.name for p in outdir.glob("*"))) + "\n")
print(f"Wrote TIFFs to: {outdir.resolve()}")
PY

echo "Done. TIFFs are in: $OUTDIR/"
