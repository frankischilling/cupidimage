#include "cupidimage_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

void cupidimage_set_err(char *err, size_t errcap, const char *msg) {
    if (err && errcap) {
        snprintf(err, errcap, "%s", msg ? msg : "error");
    }
}

int cupidimage_read_file_bytes(const char *path,
                               unsigned char **data,
                               size_t *size,
                               char *err,
                               size_t errcap) {
    if (!path || !data || !size) {
        cupidimage_set_err(err, errcap, "invalid arguments");
        return 0;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        cupidimage_set_err(err, errcap, "failed to open file");
        return 0;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        cupidimage_set_err(err, errcap, "failed to seek file");
        return 0;
    }
    long fsize = ftell(f);
    if (fsize <= 0) {
        fclose(f);
        cupidimage_set_err(err, errcap, "empty file");
        return 0;
    }
    if ((unsigned long)fsize > SIZE_MAX) {
        fclose(f);
        cupidimage_set_err(err, errcap, "file too large");
        return 0;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        cupidimage_set_err(err, errcap, "failed to seek file");
        return 0;
    }

    unsigned char *buf = (unsigned char *)malloc((size_t)fsize);
    if (!buf) {
        fclose(f);
        cupidimage_set_err(err, errcap, "out of memory");
        return 0;
    }

    size_t nread = fread(buf, 1, (size_t)fsize, f);
    fclose(f);
    if (nread != (size_t)fsize) {
        free(buf);
        cupidimage_set_err(err, errcap, "failed to read file");
        return 0;
    }

    *data = buf;
    *size = (size_t)fsize;
    return 1;
}

int cupidimage_load_image_file_via_memory(const char *path,
                                          cupidimage_image *out,
                                          char *err,
                                          size_t errcap,
                                          cupidimage_image_loader_fn loader) {
    if (!path || !out || !loader) {
        cupidimage_set_err(err, errcap, "invalid arguments");
        return 0;
    }

    unsigned char *buf = NULL;
    size_t size = 0;
    if (!cupidimage_read_file_bytes(path, &buf, &size, err, errcap)) {
        return 0;
    }

    int ok = loader(buf, size, out, err, errcap);
    free(buf);
    return ok;
}

int cupidimage_load_animation_file_via_memory(const char *path,
                                              cupidimage_animation *out,
                                              char *err,
                                              size_t errcap,
                                              cupidimage_animation_loader_fn loader) {
    if (!path || !out || !loader) {
        cupidimage_set_err(err, errcap, "invalid arguments");
        return 0;
    }

    unsigned char *buf = NULL;
    size_t size = 0;
    if (!cupidimage_read_file_bytes(path, &buf, &size, err, errcap)) {
        return 0;
    }

    int ok = loader(buf, size, out, err, errcap);
    free(buf);
    return ok;
}
