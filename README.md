cupidimage is a tiny, dependency-free C library for rendering PNG and JPEG images in a terminal using ANSI 24-bit colors.

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

Build (static library):
```sh
cc -Isrc -c src/cupidimage.c -o obj/cupidimage.o
cc -Isrc -c src/cupidimage_jpeg.c -o obj/cupidimage_jpeg.o
ar rcs bin/libcupidimage.a obj/cupidimage.o obj/cupidimage_jpeg.o
```

Enable JPEG debug logging:
```sh
cc -DCUPIDIMAGE_JPEG_DEBUG -Isrc -c src/cupidimage_jpeg.c -o obj/cupidimage_jpeg.o
```

Build the CLI test app:
```sh
cc -Isrc src/cupidimage.c src/cupidimage_jpeg.c src/cupidimage_cli.c -o bin/cupidimage
```

Run:
```sh
./bin/cupidimage test.png 120 60
./bin/cupidimage test.jpg 120 60
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
