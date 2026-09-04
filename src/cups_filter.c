#include "td2130.h"

#include <cups/cups.h>
#include <cups/raster.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

static unsigned points_to_mm(float points) {
    return (unsigned)(points * 25.4 / 72.0 + 0.5);
}

static int write_all(int fd, const uint8_t *data, size_t len) {
    while (len) {
        ssize_t n = write(fd, data, len);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        data += n;
        len -= (size_t)n;
    }
    return 0;
}

static int load_registered_media(const char *choice,
                                 uint8_t data[TD2130_MEDIA_DEFINITION_SIZE]) {
    const char *root = getenv("TD2130_MEDIA_ROOT");
    char registry[1024], binary[1024], line[512], name[128];
    FILE *f;
    if (!choice || strncmp(choice, "BrL", 3)) return 0;
    if (!root) root = "/opt/brother/PTouch/td2130n/inf";
    if (snprintf(registry, sizeof(registry), "%s/brtd2130nfunc", root) >=
        (int)sizeof(registry)) return -1;
    f = fopen(registry, "r");
    if (!f) return -1;
    name[0] = '\0';
    while (fgets(line, sizeof(line), f)) {
        size_t id_len = strlen(choice);
        if (!strncmp(line, choice, id_len) && line[id_len] == '/') {
            char *end;
            snprintf(name, sizeof(name), "%s", line + id_len + 1);
            end = strpbrk(name, "\r\n");
            if (end) *end = '\0';
            break;
        }
    }
    fclose(f);
    if (!name[0] || strchr(name, '/')) return -1;
    if (snprintf(binary, sizeof(binary), "%s/customtape/%s.bin", root, name) >=
        (int)sizeof(binary)) return -1;
    f = fopen(binary, "rb");
    if (!f) return -1;
    size_t n = fread(data, 1, TD2130_MEDIA_DEFINITION_SIZE, f);
    int extra = fgetc(f);
    fclose(f);
    return n == TD2130_MEDIA_DEFINITION_SIZE && extra == EOF &&
           !memcmp(data, "\033iUw\001\077", 6) ? 1 : -1;
}

static int is_on(const char *value) {
    return value && (!strcasecmp(value, "true") || !strcasecmp(value, "on") ||
                     !strcasecmp(value, "yes") || !strcmp(value, "1"));
}

static const char *option2(const char *a, const char *b, int count, cups_option_t *options) {
    const char *v = cupsGetOption(a, count, options);
    return v ? v : cupsGetOption(b, count, options);
}

static uint8_t *to_mono(const cups_page_header2_t *h, const uint8_t *input,
                        const char *halftone, int brightness, int contrast,
                        unsigned *stride_out) {
    unsigned stride = (h->cupsWidth + 7) / 8;
    uint8_t *out = calloc((size_t)stride, h->cupsHeight);
    if (!out) return NULL;
    *stride_out = stride;
    if (h->cupsBitsPerPixel == 1) {
        for (unsigned y = 0; y < h->cupsHeight; ++y)
            memcpy(out + (size_t)y * stride,
                   input + (size_t)y * h->cupsBytesPerLine, stride);
        return out;
    }
    static const unsigned bayer[4][4] = {
        {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}
    };
    int *errors = calloc(h->cupsWidth + 2, sizeof(int));
    int *next = calloc(h->cupsWidth + 2, sizeof(int));
    if (!errors || !next) { free(errors); free(next); free(out); return NULL; }
    for (unsigned y = 0; y < h->cupsHeight; ++y) {
        const uint8_t *row = input + (size_t)y * h->cupsBytesPerLine;
        memset(next, 0, (h->cupsWidth + 2) * sizeof(int));
        for (unsigned x = 0; x < h->cupsWidth; ++x) {
            int value = row[x]; /* CUPS K: 0=white, 255=black. */
            value = 128 + (value - 128) * (100 + contrast) / 100 - brightness * 255 / 100;
            int threshold = 128;
            if (halftone && !strcasecmp(halftone, "Dither")) threshold = (int)(bayer[y & 3][x & 3] * 16 + 8);
            if (halftone && !strcasecmp(halftone, "ErrorDiffusion")) value += errors[x + 1] / 16;
            if (value < 0) value = 0;
            if (value > 255) value = 255;
            int black = value >= threshold;
            if (black) out[(size_t)y * stride + x / 8] |= (uint8_t)(0x80u >> (x % 8));
            if (halftone && !strcasecmp(halftone, "ErrorDiffusion")) {
                int e = value - (black ? 255 : 0);
                errors[x + 2] += e * 7;
                next[x] += e * 3; next[x + 1] += e * 5; next[x + 2] += e;
            }
        }
        int *swap = errors; errors = next; next = swap;
    }
    free(errors); free(next);
    return out;
}

static int mono_pixel(const uint8_t *data, unsigned stride, unsigned x, unsigned y) {
    return (data[(size_t)y * stride + x / 8] & (uint8_t)(0x80u >> (x % 8))) != 0;
}

static uint8_t *normalize_mono(const uint8_t *source, unsigned source_width,
                               unsigned source_height, unsigned source_stride,
                               unsigned target_width, unsigned target_height,
                               unsigned *target_stride_out) {
    unsigned stride = (target_width + 7) / 8;
    uint8_t *target = calloc((size_t)stride, target_height);
    if (!target) return NULL;
    *target_stride_out = stride;

    bool swapped = source_width == target_height && source_height == target_width;
    for (unsigned y = 0; y < target_height; ++y) {
        for (unsigned x = 0; x < target_width; ++x) {
            int sx, sy;
            if (swapped) {
                sx = (int)y;
                sy = (int)source_height - 1 - (int)x;
            } else {
                sx = (int)x + ((int)source_width - (int)target_width) / 2;
                sy = (int)y + ((int)source_height - (int)target_height) / 2;
            }
            if (sx >= 0 && sy >= 0 && (unsigned)sx < source_width &&
                (unsigned)sy < source_height &&
                mono_pixel(source, source_stride, (unsigned)sx, (unsigned)sy))
                target[(size_t)y * stride + x / 8] |= (uint8_t)(0x80u >> (x % 8));
        }
    }
    return target;
}

int main(int argc, char **argv) {
    if (argc < 6 || argc > 7) {
        fprintf(stderr, "ERROR: CUPS filter arguments are invalid\n");
        return 1;
    }
    int fd = STDIN_FILENO;
    if (argc == 7) {
        fd = open(argv[6], O_RDONLY);
        if (fd < 0) { fprintf(stderr, "ERROR: Cannot open raster file: %s\n", strerror(errno)); return 1; }
    }
    cups_raster_t *raster = cupsRasterOpen(fd, CUPS_RASTER_READ);
    if (!raster) { fprintf(stderr, "ERROR: Cannot open CUPS raster stream\n"); if (fd != 0) close(fd); return 1; }

    int copies = atoi(argv[4]);
    if (copies < 1) copies = 1;
    cups_option_t *options = NULL;
    int num_options = cupsParseOptions(argv[5], 0, &options);
    const char *rotate_value = option2("Rotate180", "BrRotate", num_options, options);
    bool rotate = !rotate_value || is_on(rotate_value);
    bool mirror = is_on(option2("MirrorPrint", "BrMirror", num_options, options));
    bool peeler = is_on(option2("Peeler", "BrPeeler", num_options, options));
    bool compress = is_on(option2("Compress", "BrCompress", num_options, options));
    const char *priority = option2("Priority", "BrPriority", num_options, options);
    bool quality = priority && strstr(priority, "Quality") != NULL;
    const char *half = option2("Halftone", "BrHalftonePattern", num_options, options);
    if (!half) half = "ErrorDiffusion";
    if (strstr(half, "Binary")) half = "Binary";
    else if (strstr(half, "Dither")) half = "Dither";
    else half = "ErrorDiffusion";
    const char *bright_value = option2("Brightness", "BrBrightness", num_options, options);
    const char *contrast_value = option2("Contrast", "BrContrast", num_options, options);
    int brightness = bright_value ? atoi(bright_value) : 0;
    int contrast = contrast_value ? atoi(contrast_value) : 0;
    const char *page_size_option = cupsGetOption("PageSize", num_options, options);
    uint8_t media_definition[TD2130_MEDIA_DEFINITION_SIZE];
    int have_media_definition = load_registered_media(page_size_option, media_definition);
    if (have_media_definition < 0) {
        fprintf(stderr, "ERROR: Cannot load registered media definition for %s\n",
                page_size_option ? page_size_option : "(unknown)");
        cupsFreeOptions(num_options, options);
        cupsRasterClose(raster);
        if (fd != STDIN_FILENO) close(fd);
        return 1;
    }
    cups_page_header2_t h;
    unsigned page = 0;
    int result = 0;
    while (cupsRasterReadHeader2(raster, &h)) {
        page++;
        if ((h.cupsBitsPerPixel != 1 && h.cupsBitsPerPixel != 8) ||
            h.cupsColorOrder != CUPS_ORDER_CHUNKED) {
            fprintf(stderr, "ERROR: Page %u is not 1/8-bit chunked grayscale raster (bpp=%u)\n", page, h.cupsBitsPerPixel);
            result = 1; break;
        }
        unsigned media_width = points_to_mm(h.cupsPageSize[0]);
        unsigned media_length = points_to_mm(h.cupsPageSize[1]);
        const char *page_size = page_size_option;
        bool continuous = (page_size && (strstr(page_size, "57X1") || strstr(page_size, "58X1") ||
                                         strstr(page_size, "Roll57") || strstr(page_size, "Roll58")));
        if (have_media_definition && media_definition[6 + 0x78] == TD_MEDIA_CONTINUOUS)
            continuous = true;
        if (continuous) media_length = 0;
        if (media_width < 19 || media_width > 63 || (!continuous && (media_length < 7 || media_length > 255))) {
            fprintf(stderr, "ERROR: Unsupported custom media %u x %u mm\n", media_width, media_length);
            result = 1; break;
        }
        size_t bytes = (size_t)h.cupsBytesPerLine * h.cupsHeight;
        uint8_t *bitmap = malloc(bytes);
        if (!bitmap) { fprintf(stderr, "ERROR: Out of memory\n"); result = 1; break; }
        for (unsigned y = 0; y < h.cupsHeight; ++y) {
            if (cupsRasterReadPixels(raster, bitmap + (size_t)y * h.cupsBytesPerLine,
                                     h.cupsBytesPerLine) != h.cupsBytesPerLine) {
                fprintf(stderr, "ERROR: Truncated raster page %u\n", page);
                free(bitmap); result = 1; goto done;
            }
        }
        unsigned mono_stride = 0;
        uint8_t *mono = to_mono(&h, bitmap, half, brightness, contrast, &mono_stride);
        free(bitmap);
        if (!mono) { fprintf(stderr, "ERROR: Out of memory converting raster\n"); result = 1; goto done; }
        unsigned target_width = td2130_printable_width(media_width, media_length);
        unsigned target_height = continuous ? h.cupsHeight :
                                 td2130_printable_height(media_width, media_length);
        if (!target_width || !target_height) {
            fprintf(stderr, "ERROR: Cannot calculate printable dimensions for %ux%u mm\n",
                    media_width, media_length);
            free(mono); result = 1; goto done;
        }
        unsigned normalized_stride = 0;
        uint8_t *normalized = normalize_mono(mono, h.cupsWidth, h.cupsHeight,
                                             mono_stride, target_width, target_height,
                                             &normalized_stride);
        free(mono);
        if (!normalized) { fprintf(stderr, "ERROR: Out of memory normalizing raster\n"); result = 1; goto done; }
        const char *margin_value = option2("Feed", "BrMargin", num_options, options);
        unsigned margin = continuous ?
            (unsigned)(((margin_value ? atoi(margin_value) : 3) * 3000 + 127) / 254) : 0;
        fprintf(stderr, "DEBUG: TD-2130N page %u: media=%ux%u mm raster=%ux%u -> %ux%u rotate=%s compress=%s\n",
                page, media_width, media_length, h.cupsWidth, h.cupsHeight,
                target_width, target_height, rotate ? "yes" : "no",
                compress ? "yes" : "no");
        td_print_options settings = {media_width, media_length, margin, rotate,
                                     mirror, peeler, quality, compress};
        for (int copy = 0; copy < copies; ++copy) {
            td_buffer job; td_buffer_init(&job);
            if (td2130_build_job_ex(&job, normalized, target_width, target_height,
                                    normalized_stride, &settings)) {
                fprintf(stderr, "ERROR: Raster dimensions do not match media printable area\n");
                td_buffer_free(&job); free(normalized); result = 1; goto done;
            }
            int write_failed;
            if (have_media_definition) {
                write_failed = write_all(STDOUT_FILENO, job.data, 202) ||
                               write_all(STDOUT_FILENO, media_definition,
                                         sizeof(media_definition)) ||
                               write_all(STDOUT_FILENO, job.data + 202,
                                         job.len - 202);
            } else {
                write_failed = write_all(STDOUT_FILENO, job.data, job.len);
            }
            if (write_failed) {
                fprintf(stderr, "ERROR: Cannot write printer data: %s\n", strerror(errno));
                td_buffer_free(&job); free(normalized); result = 1; goto done;
            }
            td_buffer_free(&job);
        }
        free(normalized);
    }
done:
    cupsRasterClose(raster);
    cupsFreeOptions(num_options, options);
    if (fd != STDIN_FILENO) close(fd);
    return result;
}
