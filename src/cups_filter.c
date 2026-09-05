#include "td2130.h"

#include <cups/cups.h>
#include <cups/raster.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#ifndef TD2130_MEDIA_ROOT_DEFAULT
#ifdef __APPLE__
#define TD2130_MEDIA_ROOT_DEFAULT "/Library/Printers/TD2130N/Media"
#else
#define TD2130_MEDIA_ROOT_DEFAULT "/usr/local/share/brother/PTouch/td2130n"
#endif
#endif

#define TD2130_CUSTOM_REGISTRY "custom_media.conf"

/* Built-in standard-media registry.  The historical
 * /opt/brother/PTouch/td2130n/inf directory is never required.  Generated
 * custom-media data lives under TD2130_MEDIA_ROOT_DEFAULT by default;
 * TD2130_MEDIA_ROOT can override the directory at runtime. */
static const char embedded_brtd2130nfunc[] =
    "[td2130n]\n"
    "\n"
    "[default]\n"
    "CutLabel={0}\n"
    "Trimtape={OFF}\n"
    "Compress={OFF}\n"
    "Collate={OFF}\n"
    "Copies={1}\n"
    "#AutoCut={ON}\n"
    "CutAtEnd={OFF}\n"
    "Brightness={0}\n"
    "Contrast={0}\n"
    "Halftone={ERROR}\n"
    "MirrorPrinting={OFF}\n"
    "RotatePrinting={OFF}\n"
    "Peeler={OFF}\n"
    "Quality={SPEED}\n"
    "Resolution={300}\n"
    "Feed={3}\n"
    "MediaSize={50x30}\n"
    "\n"
    "[SelectionItem]\n"
    "CutLabel={\"0~30\"}\n"
    "Compress={OFF,ON}\n"
    "Collate={OFF,ON}\n"
    "Copies={\"1~10\"}\n"
    "#AutoCut={OFF,ON}\n"
    "CutAtEnd={OFF}\n"
    "Trimtape={OFF,ON}\n"
    "Brightness={\"-50~50\"}\n"
    "Contrast={\"-50~50\"}\n"
    "Halftone={ERROR,BINARY,DITHER}\n"
    "MirrorPrinting={OFF,ON}\n"
    "RotatePrinting={OFF,ON}\n"
    "Peeler={OFF,ON}\n"
    "Quality={SPEED,QUALITY}\n"
    "Resolution={300}\n"
    "Feed={\"3~30\"}\n"
    "MediaSize={30x30,40x40,40x50,40x60,50x30,51x26,60x60,57X1,58X1}\n"
    "[Constraint]\n"
    "\n"
    "[CustomTape]\n"
    "[CustomTapeEnd]\n";

static unsigned points_to_mm(float points) {
    return (unsigned)(points * 25.4 / 72.0 + 0.5);
}

static unsigned get_u16le(const uint8_t *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static void put_u16le(uint8_t *p, unsigned value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
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

static int registry_media_name(const char *registry, const char *choice,
                               char name[128]) {
    size_t id_len = strlen(choice);
    const char *p = registry;
    name[0] = '\0';
    while (*p) {
        const char *end = strchr(p, '\n');
        size_t length = end ? (size_t)(end - p) : strlen(p);
        if (length && p[length - 1] == '\r') --length;
        if (length > id_len + 1 && !strncmp(p, choice, id_len) &&
            p[id_len] == '/') {
            size_t name_len = length - id_len - 1;
            if (!name_len || name_len >= 128) return -1;
            memcpy(name, p + id_len + 1, name_len);
            name[name_len] = '\0';
            return strchr(name, '/') ? -1 : 1;
        }
        p = end ? end + 1 : p + length;
    }
    return 0;
}

static int load_registered_media(const char *choice,
                                 uint8_t data[TD2130_MEDIA_DEFINITION_SIZE]) {
    const char *root = getenv("TD2130_MEDIA_ROOT");
    char registry_path[1024], binary[1024], name[128];
    const char *registries[] = {TD2130_CUSTOM_REGISTRY, "brtd2130nfunc"};
    int found = 0;

    if (!choice || strncmp(choice, "BrL", 3)) return 0;
    if (!root || !*root) root = TD2130_MEDIA_ROOT_DEFAULT;

    /* Prefer the small generated overlay used by td2130-paper.  The legacy
     * brtd2130nfunc filename is accepted as a compatibility fallback when a
     * caller points TD2130_MEDIA_ROOT at an older registry. */
    for (size_t i = 0; i < sizeof(registries) / sizeof(registries[0]); ++i) {
        FILE *f;
        if (snprintf(registry_path, sizeof(registry_path), "%s/%s",
                     root, registries[i]) >= (int)sizeof(registry_path))
            return -1;
        f = fopen(registry_path, "rb");
        if (!f) continue;
        if (fseek(f, 0, SEEK_END) != 0) { fclose(f); continue; }
        long length = ftell(f);
        if (length < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); continue; }
        size_t registry_size = (size_t)length;
        char *registry = malloc(registry_size + 1);
        if (!registry) { fclose(f); return -1; }
        if (fread(registry, 1, registry_size, f) != registry_size) {
            free(registry); fclose(f); continue;
        }
        fclose(f);
        registry[registry_size] = '\0';
        if (!memchr(registry, '\0', registry_size))
            found = registry_media_name(registry, choice, name);
        free(registry);
        if (found == 1) break;
        if (found < 0) return -1;
    }

    /* Retain the embedded registry as the final schema fallback.  The stock
     * registry contains no BrL payload, so a generated medium still requires
     * its external .bin file. */
    if (found != 1) {
        found = registry_media_name(embedded_brtd2130nfunc, choice, name);
        if (found != 1) return -1;
    }

    if (snprintf(binary, sizeof(binary), "%s/customtape/%s.bin", root, name) >=
        (int)sizeof(binary)) return -1;
    FILE *f = fopen(binary, "rb");
    if (!f) return -1;
    size_t n = fread(data, 1, TD2130_MEDIA_DEFINITION_SIZE, f);
    int extra = fgetc(f);
    fclose(f);
    return n == TD2130_MEDIA_DEFINITION_SIZE && extra == EOF &&
           !memcmp(data, "\033iUw\001\077", 6) ? 1 : -1;
}

static int build_standard_media(unsigned width, unsigned length, bool continuous,
                                uint8_t data[TD2130_MEDIA_DEFINITION_SIZE]) {
    td_media_definition media = {
        .width_mm = width,
        .height_mm = continuous ? 0.0 : length,
        .gap_mm = continuous ? 0.0 : 3.0,
        .top_mm = continuous ? 0.0 : 3.0,
        .bottom_mm = continuous ? 0.0 : 3.0,
        .left_mm = width == 60 ? 2.05 : 1.55,
        .right_mm = width == 60 ? 2.05 : 1.55,
        .sensor = continuous ? TD_MEDIA_CONTINUOUS : TD_MEDIA_DIE_CUT
    };
    return td2130_build_media_definition(data, &media);
}

static int is_on(const char *value) {
    return value && (!strcasecmp(value, "true") || !strcasecmp(value, "on") ||
                     !strcasecmp(value, "yes") || !strcmp(value, "1"));
}

static const char *job_option2(const char *a, const char *b, int count,
                               cups_option_t *options) {
    const char *v = cupsGetOption(a, count, options);
    return v ? v : cupsGetOption(b, count, options);
}

/* argv[5] only guarantees the options attached to this job.  Queue defaults
 * changed with lpadmin, lpoptions, or the CUPS web UI live in the queue PPD.
 * CUPS exposes that queue-specific file through PPD. */
static const char *effective_option(const char *a, const char *b, int count,
                                    cups_option_t *options, int default_count,
                                    cups_option_t *defaults) {
    const char *value = job_option2(a, b, count, options);
    return value ? value : job_option2(a, b, default_count, defaults);
}

/* Read *DefaultFoo: Choice records from the queue-specific PPD.  This avoids
 * deprecated libcups PPD APIs while retaining compatibility with CUPS 1.x/2.x
 * filter execution. */
static int load_ppd_defaults(const char *path, cups_option_t **defaults) {
    FILE *f = fopen(path, "rb");
    char line[4096];
    int count = 0;
    if (!f) return -1;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "*Default", 8)) continue;
        char *colon = strchr(line + 8, ':');
        if (!colon) continue;
        char *value = colon + 1;
        while (isspace((unsigned char)*value)) ++value;
        char *end = value + strlen(value);
        while (end > value && isspace((unsigned char)end[-1])) --end;
        if (colon == line + 8 || end == value) continue;
        *colon = '\0';
        *end = '\0';
        count = cupsAddOption(line + 8, value, count, defaults);
    }
    int failed = ferror(f);
    fclose(f);
    if (failed) {
        cupsFreeOptions(count, *defaults);
        *defaults = NULL;
        return -1;
    }
    return count;
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

    /* CUPS can retain the portrait media raster dimensions while rendering a
     * landscape document inside it. Label printers cannot physically rotate
     * a sheet, so rotate whenever source and target aspect orientations differ. */
    bool swapped = (source_width < source_height && target_width > target_height) ||
                   (source_width > source_height && target_width < target_height);
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
    cups_option_t *defaults = NULL;
    int num_defaults = 0;
    const char *ppd_path = getenv("PPD");
    if (ppd_path && *ppd_path) {
        num_defaults = load_ppd_defaults(ppd_path, &defaults);
        if (num_defaults < 0) {
            fprintf(stderr, "WARNING: Cannot open queue PPD %s; using job/built-in defaults\n",
                    ppd_path);
            num_defaults = 0;
        }
    }
#define OPTION(a, b) effective_option((a), (b), num_options, options, \
                                      num_defaults, defaults)
    const char *rotate_value = OPTION("Rotate180", "BrRotate");
    bool rotate = !rotate_value || is_on(rotate_value);
    bool mirror = is_on(OPTION("MirrorPrint", "BrMirror"));
    bool peeler = is_on(OPTION("Peeler", "BrPeeler"));
    bool compress = is_on(OPTION("Compress", "BrCompress"));
    const char *priority = OPTION("Priority", "BrPriority");
    bool quality = priority && strstr(priority, "Quality") != NULL;
    const char *half = OPTION("Halftone", "BrHalftonePattern");
    if (!half) half = "ErrorDiffusion";
    if (strstr(half, "Binary")) half = "Binary";
    else if (strstr(half, "Dither")) half = "Dither";
    else half = "ErrorDiffusion";
    const char *bright_value = OPTION("Brightness", "BrBrightness");
    const char *contrast_value = OPTION("Contrast", "BrContrast");
    const char *orientation_value = OPTION("orientation-requested", "Orientation");
    int orientation = orientation_value ? atoi(orientation_value) : 3;
    bool landscape = orientation == 4 || orientation == 5;
    int brightness = bright_value ? atoi(bright_value) : 0;
    int contrast = contrast_value ? atoi(contrast_value) : 0;
    const char *page_size_option = job_option2("PageSize", "media",
                                                num_options, options);
    if (!page_size_option)
        page_size_option = job_option2("PageSize", "PageRegion",
                                       num_defaults, defaults);
    uint8_t media_definition[TD2130_MEDIA_DEFINITION_SIZE];
    int have_media_definition = load_registered_media(page_size_option, media_definition);
    if (have_media_definition < 0) {
        fprintf(stderr, "ERROR: Cannot load registered media definition for %s\n",
                page_size_option ? page_size_option : "(unknown)");
        cupsFreeOptions(num_defaults, defaults);
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
        if (have_media_definition) {
            /* The registered definition is authoritative for custom media.
             * CUPS page dimensions are floating point and may be rounded by
             * an application or rasterizer. */
            media_width = media_definition[7];
            media_length = get_u16le(media_definition + 8);
        }
        if (landscape && !have_media_definition && !continuous) {
            unsigned swap = media_width;
            media_width = media_length;
            media_length = swap;

            /* macOS often represents a custom W×H landscape medium as the
             * built-in H×W PageSize plus orientation-requested=4/5. Prefer a
             * registered default whose physical dimensions match W×H. */
            const char *default_page = cupsGetOption("PageSize", num_defaults,
                                                      defaults);
            uint8_t registered[TD2130_MEDIA_DEFINITION_SIZE];
            int registered_result = load_registered_media(default_page, registered);
            if (registered_result == 1 && registered[7] == media_width &&
                get_u16le(registered + 8) == media_length) {
                memcpy(media_definition, registered, sizeof(media_definition));
                have_media_definition = 1;
            }
        }
        if (continuous) media_length = 0;
        if (media_width < 19 || media_width > 63 || (!continuous && (media_length < 7 || media_length > 255))) {
            fprintf(stderr, "ERROR: Unsupported custom media %u x %u mm\n", media_width, media_length);
            result = 1; break;
        }
        bool custom_media = have_media_definition == 1;
        if (!custom_media &&
            build_standard_media(media_width, media_length, continuous,
                                 media_definition) != 0) {
            fprintf(stderr, "ERROR: Cannot build media definition for %ux%u mm\n",
                    media_width, media_length);
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
        unsigned target_width = custom_media
                                    ? get_u16le(media_definition + 13)
                                    : td2130_printable_width(media_width, media_length);
        unsigned target_height = continuous ? h.cupsHeight
                                 : custom_media
                                    ? get_u16le(media_definition + 15)
                                    : td2130_printable_height(media_width, media_length);
        unsigned custom_head_offset = custom_media
                                          ? get_u16le(media_definition + 11) : 0;
        if (custom_media && custom_head_offset <= TD2130_HEAD_DOTS &&
            target_width == TD2130_HEAD_DOTS - custom_head_offset + 1) {
            /* Accept media files produced by the previous one-dot rounding
             * behavior, and send the corrected width to the printer too. */
            --target_width;
            put_u16le(media_definition + 13, target_width);
            fprintf(stderr, "WARNING: Corrected registered media width to %u dots\n",
                    target_width);
        }
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
        const char *margin_value = OPTION("Feed", "BrMargin");
        unsigned margin = continuous ?
            (unsigned)(((margin_value ? atoi(margin_value) : 3) * 3000 + 127) / 254) : 0;
        fprintf(stderr, "DEBUG: TD-2130N page %u: media=%ux%u mm raster=%ux%u -> %ux%u orientation=%d rotate180=%s compress=%s\n",
                page, media_width, media_length, h.cupsWidth, h.cupsHeight,
                target_width, target_height, orientation, rotate ? "yes" : "no",
                compress ? "yes" : "no");
        td_print_options settings = {media_width, media_length, margin, rotate,
                                     mirror, peeler, quality, compress,
                                     custom_media ? target_width : 0,
                                     custom_media && !continuous
                                         ? target_height : 0,
                                     custom_head_offset};
        for (int copy = 0; copy < copies; ++copy) {
            td_buffer job; td_buffer_init(&job);
            if (td2130_build_job_ex(&job, normalized, target_width, target_height,
                                    normalized_stride, &settings)) {
                fprintf(stderr, "ERROR: Raster dimensions do not match media printable area\n");
                td_buffer_free(&job); free(normalized); result = 1; goto done;
            }
            int write_failed;
            {
                /* Raster mode must be selected before the per-page media
                 * definition. Always send it so changing rolls cannot inherit
                 * the preceding job's media state. */
                write_failed = write_all(STDOUT_FILENO, job.data, 206) ||
                               write_all(STDOUT_FILENO, media_definition,
                                         sizeof(media_definition)) ||
                               write_all(STDOUT_FILENO, job.data + 206,
                                         job.len - 206);
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
    cupsFreeOptions(num_defaults, defaults);
    cupsFreeOptions(num_options, options);
    if (fd != STDIN_FILENO) close(fd);
    return result;
#undef OPTION
}
