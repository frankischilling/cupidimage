cupidimage is a dependency-free C99 library for decoding common image formats and rendering them in a terminal using ANSI 24-bit color blocks. It also ships with a small CLI for quick previews.

Terminal graphics output currently supports:
- ANSI truecolor escape sequences (`48;2;R;G;B` background colors)
- No Kitty/iTerm inline image protocol support yet
- No SIXEL support yet

Supported formats (decoder status):
- SVG: static subset with shapes (rect/circle/ellipse/line/polyline/polygon/path including arc commands), embedded images (`<image>` with `data:` URIs and local file hrefs), basic text (`<text>/<tspan>/<textPath>` via built-in bitmap font; includes `dominant-baseline`/`alignment-baseline` handling and UTF-8 bullet `•` fallback glyph), presentation + inline styles + basic `<style>` selectors (descendant, child `>`, adjacent `+`, and general sibling `~` combinators, plus basic `[attr]`/`[attr=value]` matching and `display`/`visibility`), transforms, viewBox/preserveAspectRatio, linear/radial gradients, patterns, `hsl()/hsla()` colors, stroke caps/joins/dash, clipPath, luminance and alpha masks (`mask-type`), basic `<use>` references for primitive shapes and `<symbol>` (with symbol `preserveAspectRatio` handling and wrapper style attributes), basic SVG markers (`marker-start`/`marker-mid`/`marker-end`, including `orient=\"auto-start-reverse\"` and improved join-angle placement), and filter primitives: `feGaussianBlur`, `feOffset`, `feColorMatrix`, `feComponentTransfer`, `feMorphology`, `feConvolveMatrix`, `feTurbulence`, `feDisplacementMap`, `feDiffuseLighting`, `feSpecularLighting`, `feTile`, `feImage` (including `data:` URIs), `feFlood`, `feBlend`, `feComposite`, `feMerge` (including Source/Background/FillPaint/StrokePaint filter inputs and primitive subregions). Basic sampled animation support for SMIL (`<set>`, `<animate>`, `<animateTransform>`) and CSS keyframes (subset: `transform`, `stroke-dashoffset`, `stop-color`) via `cupidimage_svg_options.animation_time` / CLI `--svg-time`. Supersampled rasterization for terminal preview. No scripting/DOM, no external web fetches, no custom font loading.
- PNG: non-interlaced and Adam7; color types 0/2/3/4/6 with 1/2/4/8/16-bit depths; palette + tRNS.
- JPEG: baseline/progressive DCT, Huffman-coded, 8-bit precision; grayscale/YCbCr/CMYK/YCCK; sampling up to 2x2. No arithmetic coding or >2x2 sampling.
- WebP: VP8 lossy keyframes; VP8X still images; VP8L lossless with transforms/color cache; ALPH chunk method 0. No animation or interframes.
- GIF: single-frame decode and animated GIF composition; palettes, interlacing, transparency; disposal methods 0-3; loop counts and per-frame delays.
- BMP: Windows/OS/2; BI_RGB/BI_RLE8/BI_RLE4/BI_BITFIELDS/BI_JPEG/BI_PNG; 1/4/8/16/24/32-bit; top-down/bottom-up; pre-multiplied alpha conversion.
- ICO/CUR: directory enumeration + page-based decode; PNG-compressed and DIB icons at 1/4/8/24/32-bit; AND mask transparency; CUR hotspots.
- TIFF: Classic + BigTIFF; byte orders II/MM; compression none/LZW/PackBits/Deflate/JPEG/CCITT RLE/G3/G4; photometric bilevel/grayscale(+alpha)/RGB/RGBA/palette/CMYK/YCbCr/Lab; strips/tiles; planar 1/2; predictor=2; orientation 1-8; multi-page IFDs (SubIFD pages via page APIs).
- TGA: types 0-3/9-11; uncompressed/RLE; 8/15/16/24/32-bit; all origins; color-mapped/RGB/grayscale; TGA 2.0 metadata + thumbnail + gamma/color correction.

Build (Makefile):
```sh
make
```

Build via script:
```sh
scripts/make.sh
scripts/make.sh lib
scripts/make.sh cli
```

Build just the static library or CLI:
```sh
make lib
make cli
```

Install (static library + header):
```sh
sudo install -d /usr/local/lib /usr/local/include
sudo install -m 644 bin/libcupidimage.a /usr/local/lib/
sudo install -m 644 src/cupidimage.h /usr/local/include/
```

Install via script (supports uninstall):
```sh
scripts/install.sh
PREFIX=/usr/local scripts/install.sh
PREFIX=/usr/local scripts/install.sh --uninstall
```

Build (manual, static library):
```sh
cc -Isrc -c src/cupidimage.c -o obj/cupidimage.o
cc -Isrc -c src/cupidimage_png.c -o obj/cupidimage_png.o
cc -Isrc -c src/cupidimage_jpeg.c -o obj/cupidimage_jpeg.o
cc -Isrc -c src/cupidimage_webp.c -o obj/cupidimage_webp.o
cc -Isrc -c src/cupidimage_webp_tables.c -o obj/cupidimage_webp_tables.o
cc -Isrc -c src/cupidimage_webp_lossless.c -o obj/cupidimage_webp_lossless.o
cc -Isrc -c src/cupidimage_gif.c -o obj/cupidimage_gif.o
cc -Isrc -c src/cupidimage_bmp.c -o obj/cupidimage_bmp.o
cc -Isrc -c src/cupidimage_ico.c -o obj/cupidimage_ico.o
cc -Isrc -c src/cupidimage_tiff.c -o obj/cupidimage_tiff.o
cc -Isrc -c src/cupidimage_tga.c -o obj/cupidimage_tga.o
cc -Isrc -c src/cupidimage_svg.c -o obj/cupidimage_svg.o
ar rcs bin/libcupidimage.a obj/cupidimage.o obj/cupidimage_png.o obj/cupidimage_jpeg.o obj/cupidimage_webp.o obj/cupidimage_webp_tables.o obj/cupidimage_webp_lossless.o obj/cupidimage_gif.o obj/cupidimage_bmp.o obj/cupidimage_ico.o obj/cupidimage_tiff.o obj/cupidimage_tga.o obj/cupidimage_svg.o
```

Build the CLI test app:
```sh
cc -Isrc src/cupidimage.c src/cupidimage_png.c src/cupidimage_jpeg.c src/cupidimage_webp.c src/cupidimage_webp_tables.c src/cupidimage_webp_lossless.c src/cupidimage_gif.c src/cupidimage_bmp.c src/cupidimage_ico.c src/cupidimage_tiff.c src/cupidimage_tga.c src/cupidimage_svg.c src/cupidimage_cli.c -o bin/cupidimage -lm
```

Use the CLI:
```sh
./bin/cupidimage test.png 120 60
./bin/cupidimage test.jpg 120 60
./bin/cupidimage test.webp 120 60
./bin/cupidimage --fit test.png
./bin/cupidimage --svg-time 1.5 assets/svg/animation_basic.svg
```

When stdout is an interactive terminal, SVG files containing SMIL animation tags (`<animate>`, `<animateTransform>`, `<set>`) or CSS `animation`/`@keyframes` now auto-play.
Use `--svg-time` to render a single sampled frame instead.

Link in your app (static library):
```sh
cc -Isrc your_app.c -L/usr/local/lib -lcupidimage -lm -o your_app
```

Example usage:
```c
#include "cupidimage.h"

int main(int argc, char **argv) {
    if (argc < 2) return 1;
    cupidimage_image img;
    char err[128];
    if (!cupidimage_load_image_file(argv[1], &img, err, sizeof(err))) {
        fprintf(stderr, "load error: %s\n", err);
        return 1;
    }
    cupidimage_render_ansi(&img, stdout, 120, 60);
    cupidimage_free(&img);
    return 0;
}
```

GIF animation usage:
```c
cupidimage_animation anim;
if (!cupidimage_load_gif_animation_file("anim.gif", &anim, err, sizeof(err))) {
    fprintf(stderr, "load error: %s\n", err);
    return 1;
}
/* render anim.frames[i] with anim.delays[i] */
cupidimage_free_animation(&anim);
```

SVG sampled animation usage:
```c
cupidimage_image img;
cupidimage_svg_options opts = {0};
opts.scale = 1.0f;
opts.dpi = 96.0f;
opts.supersampling = 2;
opts.animation_time = 1.5f; /* sample at 1.5 seconds */
if (!cupidimage_load_svg_file_with_options("anim.svg", &img, &opts, err, sizeof(err))) {
    fprintf(stderr, "load error: %s\n", err);
}
```

TIFF page APIs:
```c
int count = 0;
if (cupidimage_get_tiff_page_count(data, size, &count, err, sizeof(err))) {
    cupidimage_load_tiff_page(data, size, &img, 0, err, sizeof(err));
}
```

TGA metadata APIs:
```c
cupidimage_tga_metadata meta;
if (cupidimage_load_tga_with_metadata(data, size, &img, &meta, 1, err, sizeof(err))) {
    /* use img + meta */
    cupidimage_free_tga_metadata(&meta);
}
```

ICO/CUR directory APIs:
```c
cupidimage_ico_entry *entries = NULL;
int count = 0;
int is_cursor = 0;
if (cupidimage_ico_get_directory("icons.ico", &entries, &count, &is_cursor, err, sizeof(err))) {
    cupidimage_load_ico_page("icons.ico", 0, &img, err, sizeof(err));
    free(entries);
}
```

Debug logging:
```sh
cc -DCUPIDIMAGE_JPEG_DEBUG -Isrc -c src/cupidimage_jpeg.c -o obj/cupidimage_jpeg.o
cc -DCUPIDIMAGE_BMP_DEBUG -Isrc -c src/cupidimage_bmp.c -o obj/cupidimage_bmp.o
cc -DCUPIDIMAGE_TIFF_DEBUG -Isrc -c src/cupidimage_tiff.c -o obj/cupidimage_tiff.o
cc -DCUPIDIMAGE_ICO_DEBUG -Isrc -c src/cupidimage_ico.c -o obj/cupidimage_ico.o
cc -DCUPIDIMAGE_TGA_DEBUG -Isrc -c src/cupidimage_tga.c -o obj/cupidimage_tga.o
```

Quick BMP smoke test (expects failures for invalid samples):
```sh
scripts/test_bmp_assets.sh
```

Quick ICO/CUR smoke test (expects failures for invalid samples):
```sh
scripts/test_ico_assets.sh
```

Quick TGA smoke test (expects failures for invalid samples):
```sh
scripts/test_tga_assets.sh
```

Quick TIFF smoke test (expects failures for invalid samples):
```sh
scripts/test_tiff_assets.sh
```

Quick SVG smoke test:
```sh
scripts/make_test_svgs.sh
scripts/test_svg_assets.sh
```

Non-interactive SVG regression checks:
```sh
scripts/test_svg_regressions.sh
make test-svg-regressions
```


Todo
- [ ] For each image format provide option for checkerboard or white for alpha
- [ ] PDF
- [ ] HEIC / HEIF
