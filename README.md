cupidimage is a tiny, dependency-free C library for rendering PNG, JPEG, WebP, and GIF images in a terminal using ANSI 24-bit colors.

Supported PNG features:
- PNG (RFC 2083), non-interlaced and Adam7 interlaced
- Color types: grayscale (0), RGB (2), grayscale+alpha (4), RGBA (6) at 8-bit or 16-bit depth
- Indexed-color (palette) PNGs (color type 3) with bit depths 1/2/4/8 and tRNS alpha

Supported JPEG features:
- JPEG (ISO/IEC 10918-1) baseline or progressive DCT, Huffman-coded, 8-bit precision
- Grayscale, YCbCr (3-component), and CMYK/YCCK (4-component) images
- Sampling factors up to 2x2 (e.g., 4:2:0, 4:2:2, 4:4:4)
- Uses the standard JPEG Huffman tables (Annex K) if a file omits DHT segments

Not supported: arithmetic coding or sampling factors above 2x2.

WebP status:
- VP8 lossy keyframe decode (single-partition streams only) with loop filter.
- VP8X container support for still images and VP8L lossless decode with transforms/color cache.
- ALPH chunk method 0 (raw alpha) supported for VP8.
- Not supported (priority order): animations, interframes, multi-partition streams, ALPH filtering/preprocessing, ICC/EXIF/XMP.

GIF status:
- GIF87a/GIF89a single-frame decode and animated GIF composition.
- Global/local palettes, interlacing, transparency, and disposal methods 0-3 (4-7 treated as no-dispose).
- Netscape/ANIMEXTS loop counts honored for animation playback.
- Pixel aspect ratio and color resolution parsed and exposed in animation metadata.
- Graphic Control Extension user-input flags captured per frame.
- Delay=0 honored as "no delay" (no clamping).

BMP status:
- Windows BMP (all versions) and OS/2 BMP support
- Compression: BI_RGB (uncompressed), BI_RLE8, BI_RLE4, BI_BITFIELDS, BI_JPEG, BI_PNG
- Bit depths: 1-bit (monochrome), 4-bit/8-bit (indexed), 16-bit/24-bit/32-bit (RGB/RGBA)
- Headers: BITMAPCOREHEADER (OS/2), BITMAPINFOHEADER, V2/V3/V4/V5
- Top-down and bottom-up orientation support
- Auto-conversion of pre-multiplied alpha to straight alpha

Todo

- [ ] For each image foramt provide option for checkerboard or white for alpha
- [ ] TIFF
- [ ] ICO
- [ ] HEIC / HEIF
- [ ] SVG
- [ ] PDF

Build (static library):
```sh
cc -Isrc -c src/cupidimage.c -o obj/cupidimage.o
cc -Isrc -c src/cupidimage_jpeg.c -o obj/cupidimage_jpeg.o
cc -Isrc -c src/cupidimage_webp.c -o obj/cupidimage_webp.o
cc -Isrc -c src/cupidimage_webp_tables.c -o obj/cupidimage_webp_tables.o
cc -Isrc -c src/cupidimage_webp_lossless.c -o obj/cupidimage_webp_lossless.o
cc -Isrc -c src/cupidimage_gif.c -o obj/cupidimage_gif.o
cc -Isrc -c src/cupidimage_bmp.c -o obj/cupidimage_bmp.o
ar rcs bin/libcupidimage.a obj/cupidimage.o obj/cupidimage_jpeg.o obj/cupidimage_webp.o obj/cupidimage_webp_tables.o obj/cupidimage_webp_lossless.o obj/cupidimage_gif.o obj/cupidimage_bmp.o
```

Enable JPEG debug logging:
```sh
cc -DCUPIDIMAGE_JPEG_DEBUG -Isrc -c src/cupidimage_jpeg.c -o obj/cupidimage_jpeg.o
```

Enable BMP debug logging:
```sh
cc -DCUPIDIMAGE_BMP_DEBUG -Isrc -c src/cupidimage_bmp.c -o obj/cupidimage_bmp.o
```

Build the CLI test app:
```sh
cc -Isrc src/cupidimage.c src/cupidimage_jpeg.c src/cupidimage_webp.c src/cupidimage_webp_tables.c src/cupidimage_webp_lossless.c src/cupidimage_gif.c src/cupidimage_bmp.c src/cupidimage_cli.c -o bin/cupidimage
```

Run:
```sh
./bin/cupidimage test.png 120 60
./bin/cupidimage test.jpg 120 60
./bin/cupidimage test.webp 120 60
```

Quick BMP smoke test (expects failures for invalid samples):
```sh
scripts/test_bmp_assets.sh
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
