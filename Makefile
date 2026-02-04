CC ?= cc
AR ?= ar
CFLAGS ?= -O2 -Wall -Wextra -std=c99
LDLIBS ?= -lm
INCLUDES = -Isrc -I.

OBJDIR = obj
BINDIR = bin
SRCDIR = src

LIBOBJ = $(OBJDIR)/cupidimage.o $(OBJDIR)/cupidimage_internal.o $(OBJDIR)/cupidimage_png.o $(OBJDIR)/cupidimage_jpeg.o $(OBJDIR)/cupidimage_webp.o $(OBJDIR)/cupidimage_webp_tables.o $(OBJDIR)/cupidimage_webp_lossless.o $(OBJDIR)/cupidimage_gif.o $(OBJDIR)/cupidimage_bmp.o $(OBJDIR)/cupidimage_ico.o $(OBJDIR)/cupidimage_tiff.o $(OBJDIR)/cupidimage_tga.o $(OBJDIR)/cupidimage_pdf.o $(OBJDIR)/cupidimage_svg.o $(OBJDIR)/cupidimage_svg_base.o $(OBJDIR)/cupidimage_kitty.o
LIB = $(BINDIR)/libcupidimage.a
CLI = $(BINDIR)/cupidimage

.PHONY: all lib cli test-svg test-svg-regressions test-pdf clean

all: lib cli

lib: $(LIB)

cli: $(CLI)

test-svg: cli
	./scripts/test_svg_assets.sh

test-svg-regressions: cli lib
	./scripts/test_svg_regressions.sh

test-pdf: cli
	./scripts/test_pdf_assets.sh

$(LIB): $(LIBOBJ)
	@mkdir -p $(BINDIR)
	$(AR) rcs $@ $(LIBOBJ)

$(CLI): $(SRCDIR)/cupidimage.c $(SRCDIR)/cupidimage_internal.c $(SRCDIR)/cupidimage_png.c $(SRCDIR)/cupidimage_jpeg.c $(SRCDIR)/cupidimage_webp.c $(SRCDIR)/cupidimage_webp_tables.c $(SRCDIR)/cupidimage_webp_lossless.c $(SRCDIR)/cupidimage_gif.c $(SRCDIR)/cupidimage_bmp.c $(SRCDIR)/cupidimage_ico.c $(SRCDIR)/cupidimage_tiff.c $(SRCDIR)/cupidimage_tga.c $(SRCDIR)/cupidimage_pdf.c $(SRCDIR)/cupidimage_svg.c $(SRCDIR)/cupidimage_svg_base.c $(SRCDIR)/cupidimage_kitty.c $(SRCDIR)/cupidimage_cli.c | $(BINDIR)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@ $(LDLIBS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJDIR):
	@mkdir -p $(OBJDIR)

$(BINDIR):
	@mkdir -p $(BINDIR)

clean:
	rm -f $(OBJDIR)/*.o $(LIB) $(CLI)
