#!/bin/bash
# Development build script with strict safety and debugging flags
# This compiles everything with maximum warnings, error checking, and sanitizers

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${YELLOW}=== CupidImage Development Build ===${NC}"
echo "Compiling with strict safety flags, sanitizers, and debug symbols..."

# Compiler settings
CC="${CC:-gcc}"
AR="${AR:-ar}"

# Strict development flags
DEV_CFLAGS="-std=c99 -g3 -O0 \
-Wall -Wextra -Werror \
-Wpedantic \
-Wformat=2 \
-Wformat-security \
-Wnull-dereference \
-Wstack-protector \
-Wtrampolines \
-Warray-bounds \
-Wcast-align \
-Wcast-qual \
-Wconversion \
-Wsign-conversion \
-Wstrict-overflow=4 \
-Wundef \
-Wunused \
-Wuninitialized \
-Wshadow \
-Wpointer-arith \
-Wwrite-strings \
-Wswitch-default \
-Wswitch-enum \
-Wmissing-declarations \
-Wmissing-prototypes \
-Wstrict-prototypes \
-Wredundant-decls \
-Wdouble-promotion \
-Wfloat-equal \
-Wvla \
-fstack-protector-strong \
-fno-omit-frame-pointer \
-D_FORTIFY_SOURCE=2"

# Sanitizer flags (detect memory errors, undefined behavior, etc.)
SANITIZER_FLAGS="-fsanitize=address -fsanitize=undefined -fsanitize=leak -fsanitize=bounds -fsanitize=null -fsanitize=return"

# Combined flags
CFLAGS="${DEV_CFLAGS} ${SANITIZER_FLAGS}"
LDLIBS="-lm"
INCLUDES="-Isrc -I."

# Directories
OBJDIR="obj"
BINDIR="bin"
SRCDIR="src"

# Clean previous build
echo -e "${YELLOW}Cleaning previous build...${NC}"
rm -rf ${OBJDIR}/*.o ${BINDIR}/libcupidimage.a ${BINDIR}/cupidimage 2>/dev/null || true

# Create directories
mkdir -p ${OBJDIR}
mkdir -p ${BINDIR}

# Source files for library
LIB_SOURCES=(
    "${SRCDIR}/cupidimage.c"
    "${SRCDIR}/cupidimage_internal.c"
    "${SRCDIR}/cupidimage_png.c"
    "${SRCDIR}/cupidimage_jpeg.c"
    "${SRCDIR}/cupidimage_webp.c"
    "${SRCDIR}/cupidimage_webp_tables.c"
    "${SRCDIR}/cupidimage_webp_lossless.c"
    "${SRCDIR}/cupidimage_gif.c"
    "${SRCDIR}/cupidimage_bmp.c"
    "${SRCDIR}/cupidimage_ico.c"
    "${SRCDIR}/cupidimage_tiff.c"
    "${SRCDIR}/cupidimage_tga.c"
    "${SRCDIR}/cupidimage_svg.c"
    "${SRCDIR}/cupidimage_svg_base.c"
)

# Compile library object files
echo -e "${YELLOW}Compiling library object files...${NC}"
for src in "${LIB_SOURCES[@]}"; do
    obj="${OBJDIR}/$(basename ${src%.c}.o)"
    echo "  Compiling $(basename $src)..."
    if ${CC} ${CFLAGS} ${INCLUDES} -c $src -o $obj; then
        echo -e "    ${GREEN}✓${NC} $(basename $src)"
    else
        echo -e "    ${RED}✗ FAILED${NC} $(basename $src)"
        exit 1
    fi
done

# Create static library
echo -e "${YELLOW}Creating static library...${NC}"
LIBOBJ=$(ls ${OBJDIR}/*.o)
if ${AR} rcs ${BINDIR}/libcupidimage.a ${LIBOBJ}; then
    echo -e "  ${GREEN}✓${NC} libcupidimage.a created"
else
    echo -e "  ${RED}✗ FAILED${NC} to create library"
    exit 1
fi

# Compile CLI executable
echo -e "${YELLOW}Compiling CLI executable...${NC}"
CLI_SOURCES="${SRCDIR}/cupidimage.c ${SRCDIR}/cupidimage_internal.c ${SRCDIR}/cupidimage_png.c ${SRCDIR}/cupidimage_jpeg.c ${SRCDIR}/cupidimage_webp.c ${SRCDIR}/cupidimage_webp_tables.c ${SRCDIR}/cupidimage_webp_lossless.c ${SRCDIR}/cupidimage_gif.c ${SRCDIR}/cupidimage_bmp.c ${SRCDIR}/cupidimage_ico.c ${SRCDIR}/cupidimage_tiff.c ${SRCDIR}/cupidimage_tga.c ${SRCDIR}/cupidimage_svg.c ${SRCDIR}/cupidimage_svg_base.c ${SRCDIR}/cupidimage_cli.c"

if ${CC} ${CFLAGS} ${INCLUDES} ${CLI_SOURCES} -o ${BINDIR}/cupidimage ${LDLIBS}; then
    echo -e "  ${GREEN}✓${NC} cupidimage CLI built"
else
    echo -e "  ${RED}✗ FAILED${NC} to build CLI"
    exit 1
fi

echo ""
echo -e "${GREEN}=== Build Successful ===${NC}"
echo "Binary: ${BINDIR}/cupidimage"
echo "Library: ${BINDIR}/libcupidimage.a"
echo ""
echo -e "${YELLOW}Build Configuration:${NC}"
echo "  - Debug symbols: Enabled (g3)"
echo "  - Optimization: Disabled (O0)"
echo "  - All warnings: Enabled"
echo "  - Warnings as errors: Enabled"
echo "  - AddressSanitizer: Enabled"
echo "  - UndefinedBehaviorSanitizer: Enabled"
echo "  - LeakSanitizer: Enabled"
echo "  - Stack protector: Enabled"
echo ""
echo -e "${YELLOW}Usage:${NC}"
echo "  Run tests: ./scripts/test_svg_assets.sh"
echo "  Run CLI: ${BINDIR}/cupidimage <input> <output>"
echo ""
echo -e "${YELLOW}Note:${NC} Sanitizers will report any memory errors, leaks, or undefined behavior at runtime."
