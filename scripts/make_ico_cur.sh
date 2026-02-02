#!/usr/bin/env bash
set -euo pipefail

OUTDIR="ico"
VENV=".icogen-venv"

mkdir -p "$OUTDIR"

if [[ ! -d "$VENV" ]]; then
  python3 -m venv "$VENV"
fi

# shellcheck disable=SC1091
source "$VENV/bin/activate"
python -m pip -q install --upgrade pip
python -m pip -q install pillow numpy

OUTDIR="$OUTDIR" python - <<'PY'
import os, struct, zlib
from pathlib import Path
import numpy as np
from PIL import Image

outdir = Path(os.environ.get("OUTDIR", "ico"))
outdir.mkdir(parents=True, exist_ok=True)

def rgba_pattern(size):
    w = h = size
    y, x = np.mgrid[0:h, 0:w]
    r = (x * 255 // max(1, w-1)).astype(np.uint8)
    g = (y * 255 // max(1, h-1)).astype(np.uint8)
    b = ((x ^ y) & 255).astype(np.uint8)
    # radial-ish alpha
    cx, cy = (w-1)/2.0, (h-1)/2.0
    dist = np.sqrt((x-cx)**2 + (y-cy)**2)
    a = (255 - (dist * 255 / max(1.0, dist.max()))).clip(0,255).astype(np.uint8)
    arr = np.stack([r,g,b,a], axis=-1)
    return Image.fromarray(arr, mode="RGBA")

def save_png(img: Image.Image, path: Path):
    img.save(path, format="PNG", optimize=False)

def png_bytes(img: Image.Image) -> bytes:
    import io
    buf = io.BytesIO()
    img.save(buf, format="PNG", optimize=False)
    return buf.getvalue()

# -----------------------
# ICO building primitives
# -----------------------

def dib_bgra_bytes(img: Image.Image) -> bytes:
    # ICO BMP entries store pixels as BGRA, bottom-up.
    # We'll write a BITMAPINFOHEADER + pixels + AND mask.
    rgba = np.array(img.convert("RGBA"), dtype=np.uint8)
    h, w, _ = rgba.shape
    bgra = rgba[..., [2,1,0,3]].copy()  # BGRA
    # bottom-up
    bgra = bgra[::-1, :, :]
    # 32-bit rows are already 4-byte aligned
    pixel_bytes = bgra.tobytes()

    # AND mask: 1bpp, rows padded to 32 bits. For 32-bit icons, mask can be all 0.
    and_stride = ((w + 31) // 32) * 4
    and_mask = b"\x00" * (and_stride * h)

    biSize = 40
    biWidth = w
    biHeight = h * 2  # color + mask
    biPlanes = 1
    biBitCount = 32
    biCompression = 0
    biSizeImage = len(pixel_bytes) + len(and_mask)
    biXPelsPerMeter = 2835
    biYPelsPerMeter = 2835
    biClrUsed = 0
    biClrImportant = 0
    header = struct.pack("<IIIHHIIIIII",
                         biSize, biWidth, biHeight, biPlanes, biBitCount,
                         biCompression, biSizeImage, biXPelsPerMeter, biYPelsPerMeter,
                         biClrUsed, biClrImportant)
    return header + pixel_bytes + and_mask

def dib_24bpp_with_and(img: Image.Image) -> bytes:
    # 24-bit BGR with explicit AND mask from alpha (transparent where alpha==0)
    rgba = np.array(img.convert("RGBA"), dtype=np.uint8)
    h, w, _ = rgba.shape

    bgr = rgba[..., [2,1,0]].copy()[::-1, :, :]  # bottom-up
    # 24-bit rows padded to 4 bytes
    rowbytes = w * 3
    stride = (rowbytes + 3) & ~3
    pad = stride - rowbytes
    pixel = bytearray()
    for y in range(h):
        pixel += bgr[y].tobytes()
        if pad:
            pixel += b"\x00" * pad

    # AND mask: 1 means transparent
    and_stride = ((w + 31) // 32) * 4
    and_rows = bytearray(and_stride * h)
    alpha = rgba[..., 3]
    # ICO AND mask is also bottom-up to match BMP
    for y in range(h):
        yy = h - 1 - y
        for x in range(w):
            transparent = 1 if alpha[yy, x] == 0 else 0
            if transparent:
                byte_i = y * and_stride + (x // 8)
                bit = 7 - (x % 8)
                and_rows[byte_i] |= (1 << bit)

    biSize = 40
    biWidth = w
    biHeight = h * 2
    biPlanes = 1
    biBitCount = 24
    biCompression = 0
    biSizeImage = len(pixel) + len(and_rows)
    header = struct.pack("<IIIHHIIIIII",
                         biSize, biWidth, biHeight, biPlanes, biBitCount,
                         biCompression, biSizeImage, 2835, 2835, 0, 0)
    return header + bytes(pixel) + bytes(and_rows)

def paletted_dib(imgP: Image.Image, bpp: int) -> bytes:
    # Create 1/4/8-bpp paletted DIB + AND mask (all opaque)
    # imgP must be mode "P" with palette.
    imgP = imgP.copy()
    w, h = imgP.size
    pal = imgP.getpalette() or []
    # palette entries in ICO BMP are BGRA quads
    colors = 1 << bpp
    pal += [0] * (colors * 3 - len(pal))
    quad = bytearray()
    for i in range(colors):
        r, g, b = pal[i*3:i*3+3]
        quad += bytes([b & 255, g & 255, r & 255, 0])

    # Indices, bottom-up, rows padded to 4 bytes
    idx = np.array(imgP, dtype=np.uint8)[::-1, :]
    if bpp == 8:
        rowbytes = w
        stride = (rowbytes + 3) & ~3
        pad = stride - rowbytes
        pixel = bytearray()
        for y in range(h):
            pixel += idx[y].tobytes()
            if pad: pixel += b"\x00" * pad
    elif bpp == 4:
        rowbytes = (w + 1) // 2
        stride = (rowbytes + 3) & ~3
        pad = stride - rowbytes
        pixel = bytearray()
        for y in range(h):
            row = idx[y]
            packed = bytearray()
            for x in range(0, w, 2):
                hi = int(row[x]) & 0x0F
                lo = int(row[x+1]) & 0x0F if x+1 < w else 0
                packed.append((hi << 4) | lo)
            pixel += packed
            if pad: pixel += b"\x00" * pad
    elif bpp == 1:
        rowbytes = (w + 7) // 8
        stride = ((rowbytes + 3) & ~3)
        pad = stride - rowbytes
        pixel = bytearray()
        for y in range(h):
            row = idx[y]
            packed = bytearray()
            for x in range(0, w, 8):
                byte = 0
                for j in range(8):
                    if x+j < w and (int(row[x+j]) & 1):
                        byte |= 1 << (7-j)
                packed.append(byte)
            pixel += packed
            if pad: pixel += b"\x00" * pad
    else:
        raise ValueError("bpp must be 1,4,8")

    # AND mask all 0 (opaque)
    and_stride = ((w + 31) // 32) * 4
    and_mask = b"\x00" * (and_stride * h)

    biSize = 40
    biWidth = w
    biHeight = h * 2
    biPlanes = 1
    biBitCount = bpp
    biCompression = 0
    biSizeImage = len(quad) + len(pixel) + len(and_mask)
    header = struct.pack("<IIIHHIIIIII",
                         biSize, biWidth, biHeight, biPlanes, biBitCount,
                         biCompression, biSizeImage, 2835, 2835, colors, colors)
    return header + bytes(quad) + bytes(pixel) + and_mask

def ico_write(path: Path, images, *, icon_type=1, hotspots=None, force_dir_sizes=None, mismatch_meta=False):
    """
    images: list of (w,h, payload_bytes, is_png)
    icon_type: 1=ICO, 2=CUR
    hotspots: list of (x,y) for CUR entries (same length as images)
    force_dir_sizes: list of (w_byte, h_byte) to force directory width/height byte fields (0 for 256)
    mismatch_meta: if True, intentionally mismatch directory bytes vs actual image dimensions
    """
    count = len(images)
    if hotspots is None:
        hotspots = [(0,0)] * count
    if force_dir_sizes is None:
        force_dir_sizes = [None] * count

    # Header
    parts = []
    parts.append(struct.pack("<HHH", 0, icon_type, count))

    # Directory placeholders
    dir_entries = []
    data_blobs = []
    offset = 6 + 16 * count

    for i, (w, h, payload, is_png) in enumerate(images):
        size = len(payload)
        w_byte = 0 if w == 256 else w
        h_byte = 0 if h == 256 else h

        if force_dir_sizes[i] is not None:
            w_byte, h_byte = force_dir_sizes[i]

        if mismatch_meta:
            # flip 16/32 for fun (if possible)
            if w in (16,32): w_byte = 32 if w == 16 else 16
            if h in (16,32): h_byte = 32 if h == 16 else 16

        color_count = 0  # 0 if >=8bpp or PNG
        reserved = 0
        if icon_type == 2:
            planes_or_hotx = hotspots[i][0] & 0xFFFF
            bpp_or_hoty = hotspots[i][1] & 0xFFFF
        else:
            planes_or_hotx = 1
            # best-effort bpp from BMP header if not PNG
            if is_png:
                bpp_or_hoty = 32
            else:
                # BITMAPINFOHEADER biBitCount at offset 14
                bpp_or_hoty = struct.unpack_from("<H", payload, 14)[0]

        entry = struct.pack("<BBBBHHII",
                            w_byte & 0xFF, h_byte & 0xFF, color_count, reserved,
                            planes_or_hotx, bpp_or_hoty, size, offset)
        dir_entries.append(entry)
        data_blobs.append(payload)
        offset += size

    parts.extend(dir_entries)
    parts.extend(data_blobs)
    path.write_bytes(b"".join(parts))

# -----------------------
# Required outputs
# -----------------------

# 1) Simple 32×32 32-bit RGBA (BMP/DIB inside ICO)
img32 = rgba_pattern(32)
ico_write(outdir / "simple-32x32-rgba.ico",
          [(32, 32, dib_bgra_bytes(img32), False)])

# 2) Multi-icon file (16, 32, 48, 256) with 256 as PNG (common)
img16 = rgba_pattern(16)
img48 = rgba_pattern(48)
img256 = rgba_pattern(256)
ico_write(outdir / "multi-16-32-48-256.ico", [
    (16, 16, dib_bgra_bytes(img16), False),
    (32, 32, dib_bgra_bytes(img32), False),
    (48, 48, dib_bgra_bytes(img48), False),
    (256, 256, png_bytes(img256), True),  # PNG-compressed entry
])

# 3) Legacy formats: 1-bit, 4-bit, 8-bit
base = rgba_pattern(32).convert("RGB")
# 8-bit palette
p8 = base.convert("P", palette=Image.Palette.ADAPTIVE, colors=256)
# 4-bit palette (16 colors)
p4 = base.convert("P", palette=Image.Palette.ADAPTIVE, colors=16)
# 1-bit monochrome
p1 = base.convert("1").convert("P")  # ensure palette indices 0/1

ico_write(outdir / "legacy-1bit-4bit-8bit.ico", [
    (32, 32, paletted_dib(p1, 1), False),
    (32, 32, paletted_dib(p4, 4), False),
    (32, 32, paletted_dib(p8, 8), False),
])

# 4) 24-bit RGB icon with AND mask transparency
# Make alpha hard 0/255 to clearly test AND behavior
img24 = rgba_pattern(32)
arr = np.array(img24, dtype=np.uint8)
arr[..., 3] = (arr[..., 3] > 127).astype(np.uint8) * 255
img24 = Image.fromarray(arr, "RGBA")
ico_write(outdir / "rgb24-with-andmask.ico",
          [(32, 32, dib_24bpp_with_and(img24), False)])

# 5) PNG-compressed icon within ICO container (single 256 PNG)
ico_write(outdir / "png-compressed-256.ico",
          [(256, 256, png_bytes(img256), True)])

# 6) CUR file with hotspot coordinates (16x16 + 32x32)
ico_write(outdir / "cursor-hotspot.cur", [
    (16, 16, dib_bgra_bytes(img16), False),
    (32, 32, dib_bgra_bytes(img32), False),
], icon_type=2, hotspots=[(2,3), (5,6)])

# 7) Edge cases:
# 7a) 256×256 icon (0 in directory) (already covered above; also make BMP-form 256 to test 0-size encoding)
# Using PNG is most common; but here we force directory w/h bytes to 0,0 explicitly.
ico_write(outdir / "edge-256-dir-zero.ico",
          [(256, 256, png_bytes(img256), True)],
          force_dir_sizes=[(0,0)])

# 7b) mismatched metadata: directory says 16x16 but data is 32x32
ico_write(outdir / "edge-mismatched-metadata.ico",
          [(32, 32, dib_bgra_bytes(img32), False)],
          force_dir_sizes=[(16,16)])

# 8) Invalid samples
good = (outdir / "simple-32x32-rgba.ico").read_bytes()

# truncated file
(outdir / "invalid-truncated.ico").write_bytes(good[:40])

# bad magic number (reserved != 0)
bad = bytearray(good)
bad[0:2] = b"\x01\x00"
(outdir / "invalid-bad-magic.ico").write_bytes(bytes(bad))

# zero icon count
zero_count = bytearray(good)
zero_count[4:6] = b"\x00\x00"
(outdir / "invalid-zero-count.ico").write_bytes(bytes(zero_count))

# bonus: wrong type (neither 1 nor 2)
wrong_type = bytearray(good)
wrong_type[2:4] = b"\x03\x00"
(outdir / "invalid-wrong-type.ico").write_bytes(bytes(wrong_type))

(outdir / "MANIFEST.txt").write_text("\n".join(sorted(p.name for p in outdir.glob("*"))) + "\n")
print(f"Wrote ICO/CUR files to: {outdir.resolve()}")
PY

echo "Done. ICO/CUR files are in: $OUTDIR/"
echo "See $OUTDIR/MANIFEST.txt for the list."
