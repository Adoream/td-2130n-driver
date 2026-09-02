#include "pbm.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int token(FILE *f, char *buf, size_t size) {
    int c;
    do {
        c = fgetc(f);
        if (c == '#') while ((c = fgetc(f)) != '\n' && c != EOF) {}
    } while (isspace(c));
    if (c == EOF) return -1;
    size_t n = 0;
    do {
        if (n + 1 < size) buf[n++] = (char)c;
        c = fgetc(f);
    } while (c != EOF && !isspace(c));
    buf[n] = '\0';
    return n ? 0 : -1;
}

int pbm_read(const char *path, pbm_image *out, char *error, unsigned error_size) {
    memset(out, 0, sizeof(*out));
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(error, error_size, "cannot open %s", path); return -1; }
    char t[64];
    int rc = -1;
    if (token(f, t, sizeof(t)) || strcmp(t, "P4") != 0) {
        snprintf(error, error_size, "only binary PBM (P4) is supported"); goto done;
    }
    if (token(f, t, sizeof(t))) goto malformed;
    char *end;
    unsigned long w = strtoul(t, &end, 10);
    if (*end || !w || w > 672) goto malformed;
    if (token(f, t, sizeof(t))) goto malformed;
    unsigned long h = strtoul(t, &end, 10);
    if (*end || !h || h > 11811) goto malformed;
    out->width = (unsigned)w;
    out->height = (unsigned)h;
    out->stride = (out->width + 7) / 8;
    size_t bytes = (size_t)out->stride * out->height;
    out->pixels = malloc(bytes);
    if (!out->pixels) { snprintf(error, error_size, "out of memory"); goto done; }
    if (fread(out->pixels, 1, bytes, f) != bytes) {
        snprintf(error, error_size, "truncated PBM pixel data"); pbm_free(out); goto done;
    }
    rc = 0;
    goto done;
malformed:
    snprintf(error, error_size, "malformed or unsupported PBM header");
done:
    fclose(f);
    return rc;
}

void pbm_free(pbm_image *image) {
    free(image->pixels);
    memset(image, 0, sizeof(*image));
}
