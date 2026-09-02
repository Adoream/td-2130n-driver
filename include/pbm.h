#ifndef PBM_H
#define PBM_H

#include <stdint.h>

typedef struct {
    unsigned width;
    unsigned height;
    unsigned stride;
    uint8_t *pixels;
} pbm_image;

int pbm_read(const char *path, pbm_image *out, char *error, unsigned error_size);
void pbm_free(pbm_image *image);

#endif
