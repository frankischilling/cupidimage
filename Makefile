CC ?= cc
AR ?= ar
CFLAGS ?= -O2 -Wall -Wextra -std=c99
LDLIBS ?= -lm
INCLUDES = -Isrc -I.

OBJDIR = obj
BINDIR = bin
SRCDIR = src

LIBOBJ = $(OBJDIR)/cupidimage.o $(OBJDIR)/cupidimage_png.o $(OBJDIR)/cupidimage_jpeg.o $(OBJDIR)/cupidimage_webp.o $(OBJDIR)/cupidimage_webp_tables.o $(OBJDIR)/cupidimage_webp_lossless.o $(OBJDIR)/cupidimage_gif.o $(OBJDIR)/cupidimage_bmp.o $(OBJDIR)/cupidimage_ico.o $(OBJDIR)/cupidimage_tiff.o $(OBJDIR)/cupidimage_tga.o $(OBJDIR)/cupidimage_svg.o
LIB = $(BINDIR)/libcupidimage.a
CLI = $(BINDIR)/cupidimage

.PHONY: all lib cli clean

all: lib cli

lib: $(LIB)

cli: $(CLI)

$(LIB): $(LIBOBJ)
	@mkdir -p $(BINDIR)
	$(AR) rcs $@ $(LIBOBJ)

$(CLI): $(SRCDIR)/cupidimage.c $(SRCDIR)/cupidimage_png.c $(SRCDIR)/cupidimage_jpeg.c $(SRCDIR)/cupidimage_webp.c $(SRCDIR)/cupidimage_webp_tables.c $(SRCDIR)/cupidimage_webp_lossless.c $(SRCDIR)/cupidimage_gif.c $(SRCDIR)/cupidimage_bmp.c $(SRCDIR)/cupidimage_ico.c $(SRCDIR)/cupidimage_tiff.c $(SRCDIR)/cupidimage_tga.c $(SRCDIR)/cupidimage_svg.c $(SRCDIR)/cupidimage_cli.c | $(BINDIR)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@ $(LDLIBS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJDIR):
	@mkdir -p $(OBJDIR)

$(BINDIR):
	@mkdir -p $(BINDIR)

clean:
	rm -f $(OBJDIR)/*.o $(LIB) $(CLI)
