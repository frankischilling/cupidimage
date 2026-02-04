#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT_DIR/bin/cupidimage"
LIB="$ROOT_DIR/bin/libcupidimage.a"
ASSETS_DIR="$ROOT_DIR/assets/svg"

if [[ ! -x "$BIN" ]]; then
  echo "cupidimage CLI not found at $BIN" >&2
  echo "Build first: make cli" >&2
  exit 1
fi

if [[ ! -f "$LIB" ]]; then
  echo "Static library not found at $LIB" >&2
  echo "Build first: make lib" >&2
  exit 1
fi

if [[ ! -d "$ASSETS_DIR" ]]; then
  echo "assets/svg directory not found at $ASSETS_DIR" >&2
  exit 1
fi

echo "[svg-regressions] smoke loading all SVG assets..."
shopt -s nullglob
files=("$ASSETS_DIR"/*.svg)
if (( ${#files[@]} == 0 )); then
  echo "No SVG files found in $ASSETS_DIR" >&2
  exit 1
fi

fail=0
for file in "${files[@]}"; do
  if ! timeout 20s "$BIN" "$file" 96 48 >/dev/null; then
    echo "FAIL smoke: $file"
    fail=$((fail + 1))
  fi
done
if (( fail > 0 )); then
  echo "[svg-regressions] smoke failed: $fail file(s)" >&2
  exit 1
fi

echo "[svg-regressions] running pixel probes..."
PROBE_C="/tmp/svg_regression_probe.c"
PROBE_BIN="/tmp/svg_regression_probe"
cat >"$PROBE_C" <<'EOF'
#include "src/cupidimage.h"
#include <stdio.h>
#include <stdlib.h>

static int near_u8(unsigned v, unsigned target, unsigned tol) {
    if (v > target) return (v - target) <= tol;
    return (target - v) <= tol;
}

static int check_px(const char *path, int x, int y,
                    unsigned tr, unsigned tg, unsigned tb, unsigned ta,
                    unsigned tol, const char *label) {
    cupidimage_image img;
    char err[256];
    if (!cupidimage_load_image_file(path, &img, err, sizeof(err))) {
        fprintf(stderr, "load fail %s: %s\n", path, err);
        return 0;
    }
    if (x < 0 || y < 0 || (unsigned)x >= img.width || (unsigned)y >= img.height) {
        fprintf(stderr, "oob pixel %s %d,%d (%ux%u)\n", path, x, y, img.width, img.height);
        cupidimage_free(&img);
        return 0;
    }
    size_t i = ((size_t)y * img.width + (size_t)x) * 4u;
    unsigned r = img.rgba[i + 0];
    unsigned g = img.rgba[i + 1];
    unsigned b = img.rgba[i + 2];
    unsigned a = img.rgba[i + 3];
    int ok = near_u8(r, tr, tol) && near_u8(g, tg, tol) &&
             near_u8(b, tb, tol) && near_u8(a, ta, tol);
    if (!ok) {
        fprintf(stderr, "pixel mismatch %-22s got %3u %3u %3u %3u expected %3u %3u %3u %3u +/- %u\n",
                label, r, g, b, a, tr, tg, tb, ta, tol);
    }
    cupidimage_free(&img);
    return ok;
}

static int check_px_svg_time(const char *path, float time_s, int x, int y,
                             unsigned tr, unsigned tg, unsigned tb, unsigned ta,
                             unsigned tol, const char *label) {
    cupidimage_svg_options opts;
    opts.width = 0;
    opts.height = 0;
    opts.scale = 1.0f;
    opts.dpi = 96.0f;
    opts.animation_time = time_s;
    opts.supersampling = 2;
    opts.background_alpha = 0;

    cupidimage_image img;
    char err[256];
    if (!cupidimage_load_svg_file_with_options(path, &img, &opts, err, sizeof(err))) {
        fprintf(stderr, "load fail %s @ %.2fs: %s\n", path, (double)time_s, err);
        return 0;
    }
    if (x < 0 || y < 0 || (unsigned)x >= img.width || (unsigned)y >= img.height) {
        fprintf(stderr, "oob pixel %s @ %.2fs %d,%d (%ux%u)\n",
                path, (double)time_s, x, y, img.width, img.height);
        cupidimage_free(&img);
        return 0;
    }
    size_t i = ((size_t)y * img.width + (size_t)x) * 4u;
    unsigned r = img.rgba[i + 0];
    unsigned g = img.rgba[i + 1];
    unsigned b = img.rgba[i + 2];
    unsigned a = img.rgba[i + 3];
    int ok = near_u8(r, tr, tol) && near_u8(g, tg, tol) &&
             near_u8(b, tb, tol) && near_u8(a, ta, tol);
    if (!ok) {
        fprintf(stderr, "pixel mismatch %-22s got %3u %3u %3u %3u expected %3u %3u %3u %3u +/- %u\n",
                label, r, g, b, a, tr, tg, tb, ta, tol);
    }
    cupidimage_free(&img);
    return ok;
}

int main(void) {
    int ok = 1;
    ok &= check_px("assets/svg/hsl_use_masktype.svg", 40, 40, 255, 148, 26, 255, 20, "hsl color");
    ok &= check_px("assets/svg/use_opacity.svg", 112, 40, 230, 57, 70, 255, 25, "use attr override");
    ok &= check_px("assets/svg/use_visibility.svg", 185, 25, 241, 250, 238, 255, 15, "display none bg");
    ok &= check_px("assets/svg/symbol_marker_css.svg", 250, 105, 255, 209, 102, 255, 25, "css child selector");
    ok &= check_px("assets/svg/text_marker_orient.svg", 34, 40, 29, 53, 87, 255, 30, "marker start reverse");
    ok &= check_px("assets/svg/text_marker_orient.svg", 26, 40, 255, 255, 255, 255, 20, "marker reverse background");
    ok &= check_px("assets/svg/symbol_use_par.svg", 260, 40, 255, 183, 3, 255, 25, "symbol preserve ratio");
    ok &= check_px("assets/svg/css_attr_selectors.svg", 30, 95, 239, 97, 131, 255, 30, "css attr selector");
    ok &= check_px("assets/svg/css_sibling_bullet_blend.svg", 110, 30, 6, 214, 160, 255, 30, "css adjacent sibling");
    ok &= check_px("assets/svg/css_sibling_bullet_blend.svg", 190, 30, 239, 71, 111, 255, 30, "css general sibling");
    ok &= check_px("assets/svg/css_sibling_bullet_blend.svg", 26, 104, 17, 24, 39, 255, 35, "utf8 bullet glyph");
    ok &= check_px("assets/svg/image_data_uri.svg", 60, 50, 251, 133, 0, 255, 20, "image data uri");
    ok &= check_px("assets/svg/image_data_uri.svg", 220, 50, 88, 152, 255, 255, 25, "image data uri base64");
    ok &= check_px("assets/svg/filter_feimage_data_uri.svg", 40, 40, 58, 134, 255, 255, 25, "filter feImage data uri");
    ok &= check_px("assets/svg/text_cdata.svg", 24, 32, 17, 17, 17, 255, 25, "text cdata");
    ok &= check_px_svg_time("assets/svg/animation_basic.svg", 0.0f, 15, 25, 249, 115, 22, 255, 25, "animate x t=0");
    ok &= check_px_svg_time("assets/svg/animation_basic.svg", 2.0f, 55, 25, 249, 115, 22, 255, 25, "animate x t=2");
    ok &= check_px_svg_time("assets/svg/animation_basic.svg", 5.0f, 95, 25, 249, 115, 22, 255, 25, "animate x freeze");
    ok &= check_px_svg_time("assets/svg/animation_basic.svg", 0.0f, 55, 15, 37, 99, 235, 255, 25, "animateTransform t=0");
    ok &= check_px_svg_time("assets/svg/animation_basic.svg", 1.0f, 55, 35, 37, 99, 235, 255, 25, "animateTransform t=1");
    ok &= check_px_svg_time("assets/svg/animation_basic.svg", 3.0f, 55, 55, 37, 99, 235, 255, 25, "animateTransform freeze");
    ok &= check_px_svg_time("assets/svg/css_keyframes_basic.svg", 0.0f, 15, 60, 37, 99, 235, 255, 25, "css keyframes move t=0");
    ok &= check_px_svg_time("assets/svg/css_keyframes_basic.svg", 1.0f, 55, 60, 37, 99, 235, 255, 25, "css keyframes move t=1");
    ok &= check_px_svg_time("assets/svg/css_keyframes_basic.svg", 0.0f, 18, 20, 251, 133, 0, 255, 25, "css keyframes stop t=0");
    ok &= check_px_svg_time("assets/svg/css_keyframes_basic.svg", 1.0f, 18, 20, 34, 197, 94, 255, 35, "css keyframes stop t=1");
    return ok ? 0 : 1;
}
EOF

cc -O2 -Wall -Wextra -std=c99 -I"$ROOT_DIR" -I"$ROOT_DIR/src" "$PROBE_C" \
  $(if nm "$LIB" 2>/dev/null | grep -q "__asan_init"; then echo "-fsanitize=address,undefined,leak"; fi) \
  -L"$ROOT_DIR/bin" -lcupidimage -lm -o "$PROBE_BIN"

if ! "$PROBE_BIN"; then
  echo "[svg-regressions] pixel probes failed" >&2
  exit 1
fi

echo "[svg-regressions] OK"
