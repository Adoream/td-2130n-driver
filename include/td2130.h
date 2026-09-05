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
#define TD2130_MEDIA_DEFINITION_SIZE 132u

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
    unsigned printable_width_dots;  /* zero derives from physical media */
    unsigned printable_height_dots; /* zero derives from physical media */
    unsigned head_offset_dots;      /* used with printable_width_dots */
} td_print_options;

typedef enum {
    TD_MEDIA_CONTINUOUS = 0,
    TD_MEDIA_DIE_CUT = 1,
    TD_MEDIA_BLACK_MARK = 2
} td_media_sensor;

typedef struct {
    double width_mm;
    double height_mm;
    double gap_mm;
    double top_mm;
    double bottom_mm;
    double left_mm;
    double right_mm;
    double mark_length_mm;
    double mark_offset_mm;
    unsigned dpi;       /* zero selects the TD-2130N default (300) */
    unsigned head_dots; /* zero selects 672 at 300 dpi, 448 at 203 dpi */
    td_media_sensor sensor;
} td_media_definition;

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
int td2130_build_media_definition(
    uint8_t out[TD2130_MEDIA_DEFINITION_SIZE],
    const td_media_definition *media);

#endif
