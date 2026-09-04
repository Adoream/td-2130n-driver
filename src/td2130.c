#include "td2130.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void put_u16le(uint8_t *p, unsigned value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

/* This deliberately mirrors the legacy tool's decimal fixed-point path:
   truncate first, perform integer division, then round the requested number
   of decimal places. */
static int legacy_mm_to_dots(double mm, int decimals, unsigned dpi) {
    int scale = decimals == 1 ? 10 : 1000;
    int divisor = decimals == 1 ? 10 : 1000;
    int value = ((int)(mm * scale) * (int)dpi * 10) / 254;
    return (value + divisor / 2) / divisor;
}

int td2130_build_media_definition(
    uint8_t out[TD2130_MEDIA_DEFINITION_SIZE],
    const td_media_definition *m) {
    static const uint8_t command[] = {0x1b, 0x69, 0x55, 0x77, 0x01, 0x3f};
    uint8_t *p;
    int width, height, gap, left, right, top, bottom, mark_length, mark_offset;
    int centered_width, head_offset;
    unsigned dpi, head_dots;

    if (!out || !m || m->sensor < TD_MEDIA_CONTINUOUS ||
        m->sensor > TD_MEDIA_BLACK_MARK || m->width_mm <= 0.0 ||
        m->width_mm > 255.0 || m->height_mm < 0.0 ||
        m->height_mm > 65535.0 || m->gap_mm < 0.0 ||
        m->top_mm < 0.0 || m->bottom_mm < 0.0 || m->left_mm < 0.0 ||
        m->right_mm < 0.0 || m->mark_length_mm < 0.0 ||
        m->mark_offset_mm < 0.0 || m->left_mm + m->right_mm >= m->width_mm ||
        m->height_mm <= 0.0 || m->top_mm + m->bottom_mm >= m->height_mm)
        return -1;

    dpi = m->dpi ? m->dpi : 300u;
    head_dots = m->head_dots ? m->head_dots : (dpi == 203u ? 448u : 672u);
    if ((dpi != 203u && dpi != 300u) || head_dots == 0 || head_dots > 65535u)
        return -1;

    memset(out, 0, TD2130_MEDIA_DEFINITION_SIZE);
    memcpy(out, command, sizeof(command));
    p = out + sizeof(command);

    width = legacy_mm_to_dots(m->width_mm, 1, dpi);
    height = legacy_mm_to_dots(m->height_mm, 1, dpi);
    gap = legacy_mm_to_dots(m->gap_mm, 3, dpi);
    left = legacy_mm_to_dots(m->left_mm, 3, dpi);
    right = legacy_mm_to_dots(m->right_mm, 3, dpi);
    top = legacy_mm_to_dots(m->top_mm, 3, dpi);
    bottom = legacy_mm_to_dots(m->bottom_mm, 3, dpi);
    mark_length = legacy_mm_to_dots(m->mark_length_mm, 3, dpi);
    mark_offset = legacy_mm_to_dots(m->mark_offset_mm, 3, dpi);

    centered_width = legacy_mm_to_dots(m->width_mm - 2.0 * m->left_mm, 1, dpi);
    head_offset = ((int)head_dots - centered_width) / 2;
    if (head_offset < 0)
        return -1;

    p[0] = m->sensor == TD_MEDIA_CONTINUOUS ? 4 : 5;
    p[1] = (uint8_t)m->width_mm;
    put_u16le(p + 2, (unsigned)m->height_mm);
    put_u16le(p + 5, (unsigned)head_offset);
    put_u16le(p + 7, (unsigned)(width - left - right));
    put_u16le(p + 9, m->sensor == TD_MEDIA_CONTINUOUS
                           ? 0u : (unsigned)(height - top - bottom));

    if (m->sensor == TD_MEDIA_CONTINUOUS) {
        (void)snprintf((char *)p + 0x4c, 16, "%dmm", (int)m->width_mm);
        (void)snprintf((char *)p + 0x5c, 16, "%.1f\"", m->width_mm / 25.4);
    } else {
        (void)snprintf((char *)p + 0x4c, 16, "%dmm x %dmm",
                       (int)m->width_mm, (int)m->height_mm);
        (void)snprintf((char *)p + 0x5c, 16, "%.1f\" x %.1f\"",
                       m->width_mm / 25.4, m->height_mm / 25.4);
    }

    put_u16le(p + 0x6e, m->sensor == TD_MEDIA_CONTINUOUS
                              ? 0u : (unsigned)(height + gap));
    put_u16le(p + 0x72, (unsigned)top);
    if (m->sensor == TD_MEDIA_BLACK_MARK) {
        put_u16le(p + 0x74, (unsigned)mark_offset);
        put_u16le(p + 0x76, (unsigned)mark_length);
    }
    p[0x78] = (uint8_t)m->sensor;
    put_u16le(p + 0x79, (unsigned)bottom);
    return 0;
}

void td_buffer_init(td_buffer *b) { memset(b, 0, sizeof(*b)); }

void td_buffer_free(td_buffer *b) {
    free(b->data);
    memset(b, 0, sizeof(*b));
}

int td_buffer_append(td_buffer *b, const void *data, size_t len) {
    if (len > SIZE_MAX - b->len) return -1;
    size_t needed = b->len + len;
    if (needed > b->cap) {
        size_t cap = b->cap ? b->cap : 512;
        while (cap < needed) {
            if (cap > SIZE_MAX / 2) { cap = needed; break; }
            cap *= 2;
        }
        void *p = realloc(b->data, cap);
        if (!p) return -1;
        b->data = p;
        b->cap = cap;
    }
    memcpy(b->data + b->len, data, len);
    b->len = needed;
    return 0;
}

static int add(td_buffer *b, const uint8_t *p, size_t n) {
    return td_buffer_append(b, p, n);
}

unsigned td2130_printable_width(unsigned media_width_mm, unsigned media_length_mm) {
    if (media_width_mm == 58) return 648;
    if (media_width_mm == 57) return 638;
    if (!media_length_mm || media_width_mm < 19 || media_width_mm > 63) return 0;
    double dots = ((double)media_width_mm - 3.1) * 300.0 / 25.4;
    unsigned result = (unsigned)(dots + 0.5);
    return result > 660 ? 660 : result;
}

unsigned td2130_printable_height(unsigned media_width_mm, unsigned media_length_mm) {
    (void)media_width_mm;
    if (media_length_mm < 7 || media_length_mm > 255) return 0;
    return (unsigned)(((double)media_length_mm - 6.0) * 300.0 / 25.4 + 0.5);
}

static bool get_pixel(const uint8_t *bitmap, unsigned stride, unsigned x,
                      unsigned y) {
    return (bitmap[(size_t)y * stride + x / 8] & (uint8_t)(0x80u >> (x % 8))) != 0;
}

static size_t packbits(const uint8_t *src, size_t len, uint8_t *dst) {
    size_t i = 0, o = 0;
    while (i < len) {
        size_t run = 1;
        while (i + run < len && run < 128 && src[i + run] == src[i]) run++;
        if (run >= 2) {
            dst[o++] = (uint8_t)(1 - (int)run);
            dst[o++] = src[i];
            i += run;
            continue;
        }
        size_t start = i++;
        while (i < len && i - start < 128) {
            run = 1;
            while (i + run < len && run < 128 && src[i + run] == src[i]) run++;
            if (run >= 2) break;
            i++;
        }
        dst[o++] = (uint8_t)(i - start - 1);
        memcpy(dst + o, src + start, i - start);
        o += i - start;
    }
    return o;
}

int td2130_build_job_ex(td_buffer *out, const uint8_t *bitmap, unsigned width,
                        unsigned height, unsigned stride,
                        const td_print_options *options) {
    if (!options) return -1;
    unsigned media_width_mm = options->media_width_mm;
    unsigned media_length_mm = options->media_length_mm;
    unsigned margin_dots = options->margin_dots;
    unsigned max_width = td2130_printable_width(media_width_mm, media_length_mm);
    unsigned fixed_rows = td2130_printable_height(media_width_mm, media_length_mm);
    bool die_cut = media_length_mm != 0;
    if (!out || !bitmap || !width || !height || stride < (width + 7) / 8 ||
        !max_width || width > max_width || height > 11811 ||
        (!die_cut && height < 142) || (die_cut && height != fixed_rows) ||
        (die_cut && margin_dots != 0) || margin_dots > 1500) return -1;

    const uint8_t init[202] = {[200] = 0x1b, [201] = 0x40};
    const uint8_t raster_mode[] = {0x1b, 0x69, 0x61, 0x01};
    uint8_t flags = (uint8_t)(0x86 | (die_cut ? 0x08 : 0) | (options->quality ? 0x40 : 0));
    uint8_t print_info[] = {0x1b, 0x69, 0x7a,
        flags, die_cut ? 0x0b : 0x0a,
        (uint8_t)media_width_mm, (uint8_t)media_length_mm,
        (uint8_t)height, (uint8_t)(height >> 8),
        (uint8_t)(height >> 16), (uint8_t)(height >> 24), 0x00, 0x00};
    const uint8_t mode[] = {0x1b, 0x69, 0x4d,
        (uint8_t)((options->rotate_180 ? 0x08 : 0) | (options->peeler ? 0x10 : 0))};
    uint8_t margin[] = {0x1b, 0x69, 0x64,
        (uint8_t)margin_dots, (uint8_t)(margin_dots >> 8)};
    const uint8_t compression[] = {0x4d, options->compress ? 0x02 : 0x00};

    if (add(out, init, sizeof(init)) || add(out, raster_mode, sizeof(raster_mode)) ||
        add(out, print_info, sizeof(print_info)) || add(out, mode, sizeof(mode)) ||
        add(out, margin, sizeof(margin)) ||
        add(out, compression, sizeof(compression))) return -1;

    unsigned head_offset = (TD2130_HEAD_DOTS - max_width) / 2;
    unsigned image_offset = head_offset + (max_width - width) / 2;
    for (unsigned y = 0; y < height; ++y) {
        uint8_t row[TD2130_RASTER_BYTES] = {0};
        for (unsigned x = 0; x < width; ++x) {
            unsigned source_x = options->mirror ? width - 1 - x : x;
            if (get_pixel(bitmap, stride, source_x, y)) {
                /* Brother numbers head dots from the physical right edge,
                   opposite to PBM's conventional left-to-right X axis. */
                unsigned dot = TD2130_HEAD_DOTS - 1u - (image_offset + x);
                row[dot / 8] |= (uint8_t)(0x80u >> (dot % 8));
            }
        }
        if (options->compress) {
            bool zero = true;
            for (unsigned i = 0; i < sizeof(row); ++i) if (row[i]) { zero = false; break; }
            if (zero) { const uint8_t z = 0x5a; if (add(out, &z, 1)) return -1; }
            else {
                uint8_t packed[TD2130_RASTER_BYTES + 2];
                size_t n = packbits(row, sizeof(row), packed);
                uint8_t head[] = {0x67, 0x00, (uint8_t)n};
                if (add(out, head, sizeof(head)) || add(out, packed, n)) return -1;
            }
        } else {
            uint8_t head[] = {0x67, 0x00, TD2130_RASTER_BYTES};
            if (add(out, head, sizeof(head)) || add(out, row, sizeof(row))) return -1;
        }
    }
    const uint8_t print_and_feed = 0x1a;
    return add(out, &print_and_feed, 1);
}

int td2130_build_job(td_buffer *out, const uint8_t *bitmap, unsigned width,
                     unsigned height, unsigned stride, unsigned media_width_mm,
                     unsigned media_length_mm, unsigned margin_dots,
                     bool rotate_180) {
    td_print_options options = {media_width_mm, media_length_mm, margin_dots,
                                rotate_180, false, false, true, false};
    return td2130_build_job_ex(out, bitmap, width, height, stride, &options);
}

int td2130_parse_status(td_status *out, const uint8_t raw[TD2130_STATUS_SIZE]) {
    if (!out || !raw || raw[0] != 0x80 || raw[1] != 0x20 ||
        raw[2] != 0x42 || raw[3] != 0x35 || raw[4] != 0x36) return -1;
    memcpy(out->raw, raw, TD2130_STATUS_SIZE);
    out->error1 = raw[8];
    out->error2 = raw[9];
    out->media_width_mm = raw[10];
    out->media_type = raw[11];
    out->media_length_mm = raw[17];
    out->status_type = raw[18];
    return 0;
}

const char *td2130_status_error(const td_status *s, char *buf, size_t size) {
    const char *msg = "ready";
    if (s->error1 & 0x01) msg = "no media";
    else if (s->error1 & 0x02) msg = "end of die-cut media";
    else if (s->error1 & 0x10) msg = "printer in use";
    else if (s->error2 & 0x01) msg = "wrong media";
    else if (s->error2 & 0x04) msg = "communication error";
    else if (s->error2 & 0x10) msg = "cover open";
    else if (s->error2 & 0x40) msg = "media cannot be fed";
    else if (s->error2 & 0x80) msg = "system error";
    if (size) snprintf(buf, size, "%s", msg);
    return buf;
}
