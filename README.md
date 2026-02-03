cupidimage is a dependency-free C99 library for decoding common image formats and rendering them in a terminal using ANSI 24-bit color. It also ships with a small CLI for quick previews.

Supported formats (decoder status):
- PNG: non-interlaced and Adam7; color types 0/2/3/4/6 with 1/2/4/8/16-bit depths; palette + tRNS.
- JPEG: baseline/progressive DCT, Huffman-coded, 8-bit precision; grayscale/YCbCr/CMYK/YCCK; sampling up to 2x2. No arithmetic coding or >2x2 sampling.
- WebP: VP8 lossy keyframes; VP8X still images; VP8L lossless with transforms/color cache; ALPH chunk method 0. No animation or interframes.
- GIF: single-frame decode and animated GIF composition; palettes, interlacing, transparency; disposal methods 0-3; loop counts and per-frame delays.
- BMP: Windows/OS/2; BI_RGB/BI_RLE8/BI_RLE4/BI_BITFIELDS/BI_JPEG/BI_PNG; 1/4/8/16/24/32-bit; top-down/bottom-up; pre-multiplied alpha conversion.
- ICO/CUR: directory enumeration + page-based decode; PNG-compressed and DIB icons at 1/4/8/24/32-bit; AND mask transparency; CUR hotspots.
- TIFF: Classic + BigTIFF; byte orders II/MM; compression none/LZW/PackBits/Deflate/JPEG/CCITT RLE/G3/G4; photometric bilevel/grayscale(+alpha)/RGB/RGBA/palette/CMYK/YCbCr/Lab; strips/tiles; planar 1/2; predictor=2; orientation 1-8; multi-page IFDs (SubIFD pages via page APIs).

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
ar rcs bin/libcupidimage.a obj/cupidimage.o obj/cupidimage_png.o obj/cupidimage_jpeg.o obj/cupidimage_webp.o obj/cupidimage_webp_tables.o obj/cupidimage_webp_lossless.o obj/cupidimage_gif.o obj/cupidimage_bmp.o obj/cupidimage_ico.o obj/cupidimage_tiff.o
```

Build the CLI test app:
```sh
cc -Isrc src/cupidimage.c src/cupidimage_png.c src/cupidimage_jpeg.c src/cupidimage_webp.c src/cupidimage_webp_tables.c src/cupidimage_webp_lossless.c src/cupidimage_gif.c src/cupidimage_bmp.c src/cupidimage_ico.c src/cupidimage_tiff.c src/cupidimage_cli.c -o bin/cupidimage
```

Use the CLI:
```sh
./bin/cupidimage test.png 120 60
./bin/cupidimage test.jpg 120 60
./bin/cupidimage test.webp 120 60
```

Link in your app (static library):
```sh
cc -Isrc your_app.c -L/usr/local/lib -lcupidimage -o your_app
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

TIFF page APIs:
```c
int count = 0;
if (cupidimage_get_tiff_page_count(data, size, &count, err, sizeof(err))) {
    cupidimage_load_tiff_page(data, size, &img, 0, err, sizeof(err));
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
```

Quick BMP smoke test (expects failures for invalid samples):
```sh
scripts/test_bmp_assets.sh
```

Quick ICO/CUR smoke test (expects failures for invalid samples):
```sh
scripts/test_ico_assets.sh
```


Todo
- [ ] For each image format provide option for checkerboard or white for alpha
- [ ] TGA
- [ ] SVG
- [ ] PDF
- [ ] HEIC / HEIF
