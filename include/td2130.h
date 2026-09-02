#ifndef TD2130_H
#define TD2130_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TD2130_USB_VID 0x04f9u
#define TD2130_USB_PID 0x2058u
#define TD2130_HEAD_DOTS 672u
#define TD2130_RASTER_BYTES 84u
#define TD2130_STATUS_SIZE 32u

typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
} td_buffer;

typedef struct {
    uint8_t raw[TD2130_STATUS_SIZE];
    uint8_t error1;
    uint8_t error2;
    uint8_t media_width_mm;
    uint8_t media_type;
    uint8_t media_length_mm;
    uint8_t status_type;
} td_status;

typedef struct {
    unsigned media_width_mm;
    unsigned media_length_mm; /* zero means continuous media */
    unsigned margin_dots;
    bool rotate_180;
    bool mirror;
    bool peeler;
    bool quality;
    bool compress;
} td_print_options;

void td_buffer_init(td_buffer *b);
void td_buffer_free(td_buffer *b);
int td_buffer_append(td_buffer *b, const void *data, size_t len);
unsigned td2130_printable_width(unsigned media_width_mm, unsigned media_length_mm);
unsigned td2130_printable_height(unsigned media_width_mm, unsigned media_length_mm);

int td2130_build_job(td_buffer *out, const uint8_t *bitmap, unsigned width,
                     unsigned height, unsigned stride, unsigned media_width_mm,
                     unsigned media_length_mm, unsigned margin_dots,
                     bool rotate_180);
int td2130_build_job_ex(td_buffer *out, const uint8_t *bitmap, unsigned width,
                        unsigned height, unsigned stride,
                        const td_print_options *options);
int td2130_parse_status(td_status *out, const uint8_t raw[TD2130_STATUS_SIZE]);
const char *td2130_status_error(const td_status *status, char *buf, size_t size);

#endif
