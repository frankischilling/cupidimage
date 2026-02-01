CC ?= cc
AR ?= ar
CFLAGS ?= -O2 -Wall -Wextra -std=c99
INCLUDES = -Isrc -I.

OBJDIR = obj
BINDIR = bin
SRCDIR = src

LIBOBJ = $(OBJDIR)/cupidimage.o $(OBJDIR)/cupidimage_jpeg.o $(OBJDIR)/cupidimage_webp.o $(OBJDIR)/cupidimage_webp_tables.o $(OBJDIR)/cupidimage_webp_lossless.o $(OBJDIR)/cupidimage_gif.o
LIB = $(BINDIR)/libcupidimage.a
CLI = $(BINDIR)/cupidimage

.PHONY: all lib cli clean

all: lib cli

lib: $(LIB)

cli: $(CLI)

$(LIB): $(LIBOBJ)
	@mkdir -p $(BINDIR)
	$(AR) rcs $@ $(LIBOBJ)

$(CLI): $(SRCDIR)/cupidimage.c $(SRCDIR)/cupidimage_jpeg.c $(SRCDIR)/cupidimage_webp.c $(SRCDIR)/cupidimage_webp_tables.c $(SRCDIR)/cupidimage_webp_lossless.c $(SRCDIR)/cupidimage_gif.c $(SRCDIR)/cupidimage_cli.c | $(BINDIR)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJDIR):
	@mkdir -p $(OBJDIR)

$(BINDIR):
	@mkdir -p $(BINDIR)

clean:
	rm -f $(OBJDIR)/*.o $(LIB) $(CLI)
