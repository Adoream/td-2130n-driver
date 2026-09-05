#include "td2130.h"

#include <errno.h>
#include <getopt.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef TD2130_MEDIA_ROOT_DEFAULT
#ifdef __APPLE__
#define TD2130_MEDIA_ROOT_DEFAULT "/Library/Printers/TD2130N/Media"
#else
#define TD2130_MEDIA_ROOT_DEFAULT "/usr/local/share/brother/PTouch/td2130n"
#endif
#endif

#define TD2130_CUSTOM_REGISTRY "custom_media.conf"

static void usage(FILE *f) {
    fprintf(f,
        "usage: td2130-paper -P queue -n name -w mm [-h mm]\n"
        "       [-g mm] [-t mm] [-b mm] [-l mm] [-r mm]\n"
        "       -S 0|1|2 [-m mm] [-o mm] [-d 203|300] [-H dots] [-O file]\n"
        "       [--install-root directory] [--media-root directory] [--ppd file]\n"
        "  S=0 continuous, S=1 die-cut/gap, S=2 black-mark\n"
        "  Custom-media data defaults to " TD2130_MEDIA_ROOT_DEFAULT "\n"
        "  TD2130_MEDIA_ROOT or --media-root can override that location.\n");
}

static int mkdir_one(const char *path) {
    return mkdir(path, 0755) == 0 || errno == EEXIST ? 0 : -1;
}

static int make_parents(const char *path) {
    char copy[1024];
    size_t n = strlen(path);
    if (n >= sizeof(copy)) return -1;
    memcpy(copy, path, n + 1);
    for (char *p = copy + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir_one(copy) != 0) return -1;
            *p = '/';
        }
    }
    return 0;
}

static char *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    long n;
    char *data;
    if (!f || fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 ||
        fseek(f, 0, SEEK_SET) != 0) {
        if (f) fclose(f);
        return NULL;
    }
    data = malloc((size_t)n + 1);
    if (!data || fread(data, 1, (size_t)n, f) != (size_t)n) {
        free(data); fclose(f); return NULL;
    }
    data[n] = '\0';
    fclose(f);
    *size = (size_t)n;
    return data;
}

static int file_contains(const char *path, const char *needle) {
    size_t size;
    char *data = read_file(path, &size);
    int found = data && strstr(data, needle) != NULL;
    (void)size;
    free(data);
    return found;
}

static int ensure_custom_registry(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f) {
        fclose(f);
        return 0;
    }
    if (errno != ENOENT) return -1;
    f = fopen(path, "wb");
    if (!f) return -1;
    static const char initial[] =
        "# TD-2130N custom media registry\n"
        "[CustomTape]\n"
        "[CustomTapeEnd]\n";
    int failed = fwrite(initial, 1, sizeof(initial) - 1, f) != sizeof(initial) - 1;
    if (fclose(f) != 0) failed = 1;
    return failed ? -1 : 0;
}

/* Remove the old line(s) for this stable ID and insert replacement text
   immediately before a format-specific marker. */
static int update_text_file(const char *path, const char *id,
                            const char *marker, const char *addition) {
    size_t old_size, out_size = 0;
    char *old = read_file(path, &old_size);
    char *out;
    const char *p;
    size_t kind_len = strcspn(addition, " \t\r\n");
    int inserted = 0;
    if (!old) return -1;
    out = malloc(old_size + strlen(addition) + 2);
    if (!out) { free(old); return -1; }
    p = old;
    while (*p) {
        const char *end = strchr(p, '\n');
        size_t len = end ? (size_t)(end - p + 1) : strlen(p);
        const char *found_id = strstr(p, id);
        int same_kind = len >= kind_len && !strncmp(p, addition, kind_len) &&
                        (p[kind_len] == ' ' || p[kind_len] == '\t' ||
                         p[kind_len] == ':' || p[kind_len] == '/' ||
                         p[kind_len] == '\r' || p[kind_len] == '\n');
        int contains_id = same_kind && found_id && found_id < p + len;
        int is_marker = marker && len >= strlen(marker) && !strncmp(p, marker, strlen(marker));
        if (!inserted && is_marker) {
            memcpy(out + out_size, addition, strlen(addition));
            out_size += strlen(addition);
            inserted = 1;
        }
        if (!contains_id) {
            memcpy(out + out_size, p, len);
            out_size += len;
        }
        p += len;
    }
    if (!inserted && !marker) {
        memcpy(out + out_size, addition, strlen(addition));
        out_size += strlen(addition);
        inserted = 1;
    }
    free(old);
    if (!inserted) { free(out); errno = EINVAL; return -1; }
    char temp[1060];
    if (snprintf(temp, sizeof(temp), "%s.tmp.%ld", path, (long)getpid()) >= (int)sizeof(temp)) {
        free(out); errno = ENAMETOOLONG; return -1;
    }
    FILE *f = fopen(temp, "wb");
    if (!f) { free(out); return -1; }
    int failed = fwrite(out, 1, out_size, f) != out_size || fclose(f) != 0;
    free(out);
    if (failed || rename(temp, path) != 0) { unlink(temp); return -1; }
    return 0;
}

/* Remove stale PPD constraint lines created by older versions of this tool. */
static int remove_ppd_lines(const char *path, const char *prefix, const char *needle) {
    size_t old_size, out_size = 0;
    char *old = read_file(path, &old_size);
    char *out;
    const char *p;
    size_t prefix_len = strlen(prefix);
    if (!old) return -1;
    out = malloc(old_size + 1);
    if (!out) { free(old); return -1; }
    p = old;
    while (*p) {
        const char *end = strchr(p, '\n');
        size_t len = end ? (size_t)(end - p + 1) : strlen(p);
        int has_needle = 0;
        size_t needle_len = strlen(needle);
        if (needle_len <= len) {
            for (size_t i = 0; i + needle_len <= len; ++i) {
                if (!memcmp(p + i, needle, needle_len)) {
                    has_needle = 1;
                    break;
                }
            }
        }
        int remove = len >= prefix_len && !strncmp(p, prefix, prefix_len) &&
                     has_needle;
        if (!remove) {
            memcpy(out + out_size, p, len);
            out_size += len;
        }
        p += len;
    }
    free(old);

    char temp[1060];
    if (snprintf(temp, sizeof(temp), "%s.tmp.%ld", path, (long)getpid()) >=
        (int)sizeof(temp)) {
        free(out); errno = ENAMETOOLONG; return -1;
    }
    FILE *f = fopen(temp, "wb");
    if (!f) { free(out); return -1; }
    int failed = fwrite(out, 1, out_size, f) != out_size || fclose(f) != 0;
    free(out);
    if (failed || rename(temp, path) != 0) { unlink(temp); return -1; }
    return 0;
}

static unsigned media_hash(const char *name, const td_media_definition *m) {
    unsigned h = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)name; *p; ++p)
        h = (h ^ *p) * 16777619u;
    h = (h ^ (unsigned)(m->width_mm * 100)) * 16777619u;
    h = (h ^ (unsigned)(m->height_mm * 100)) * 16777619u;
    h = (h ^ (unsigned)m->sensor) * 16777619u;
    return h;
}

static int register_media(const char *install_root, const char *media_root,
                          const char *queue, const char *name,
                          const char *ppd_arg, const td_media_definition *m,
                          const uint8_t data[TD2130_MEDIA_DEFINITION_SIZE]) {
    char base[1024], path[1200], ppd[1200], id[16], line[4096];
    unsigned hash = media_hash(name, m);
    double pt = 72.0 / 25.4;

    snprintf(id, sizeof(id), "BrL%02X%02X%03X%01X%04X",
             (unsigned)strlen(name), hash & 0xff, (hash >> 8) & 0xfff,
             (hash >> 20) & 0xf, (hash >> 4) & 0xffff);

    /* Custom-media binaries are runtime data, not immutable INF resources.
     * Keep them in one shared path that works on both Linux and macOS.  A
     * staging install root is prepended only when --media-root is not used. */
    if (media_root && *media_root) {
        if (snprintf(base, sizeof(base), "%s", media_root) >= (int)sizeof(base)) {
            errno = ENAMETOOLONG;
            return -1;
        }
    } else if (install_root && *install_root && strcmp(install_root, "/")) {
        if (snprintf(base, sizeof(base), "%s%s", install_root,
                     TD2130_MEDIA_ROOT_DEFAULT) >= (int)sizeof(base)) {
            errno = ENAMETOOLONG;
            return -1;
        }
    } else {
        if (snprintf(base, sizeof(base), "%s", TD2130_MEDIA_ROOT_DEFAULT) >=
            (int)sizeof(base)) {
            errno = ENAMETOOLONG;
            return -1;
        }
    }

    if (make_parents(base) != 0 || mkdir_one(base) != 0) return -1;
    if (snprintf(path, sizeof(path), "%s/customtape", base) >= (int)sizeof(path)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    if (mkdir_one(path) != 0) return -1;
    if (snprintf(path, sizeof(path), "%s/customtape/%s.bin", base, name) >=
        (int)sizeof(path)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    FILE *bin = fopen(path, "wb");
    if (!bin) return -1;
    int bin_failed = fwrite(data, 1, TD2130_MEDIA_DEFINITION_SIZE, bin) !=
                     TD2130_MEDIA_DEFINITION_SIZE;
    if (fclose(bin) != 0) bin_failed = 1;
    if (bin_failed) return -1;

    /* Only the generated custom-media overlay remains external.  The legacy
     * brtd2130nfunc/paperinftd2130npt1/ImagingArea/PaperDimension INF files
     * are no longer required here. */
    if (snprintf(path, sizeof(path), "%s/%s", base, TD2130_CUSTOM_REGISTRY) >=
        (int)sizeof(path)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    if (ensure_custom_registry(path) != 0) return -1;
    if (snprintf(line, sizeof(line), "%s/%s\n", id, name) >= (int)sizeof(line)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    if (update_text_file(path, id, "[CustomTapeEnd]", line) != 0) return -1;

    if (ppd_arg) {
        if (snprintf(ppd, sizeof(ppd), "%s", ppd_arg) >= (int)sizeof(ppd)) {
            errno = ENAMETOOLONG;
            return -1;
        }
    } else if (install_root && *install_root && strcmp(install_root, "/")) {
        if (snprintf(ppd, sizeof(ppd), "%s/etc/cups/ppd/%s.ppd",
                     install_root, queue) >= (int)sizeof(ppd)) {
            errno = ENAMETOOLONG;
            return -1;
        }
    } else {
        if (snprintf(ppd, sizeof(ppd), "/etc/cups/ppd/%s.ppd", queue) >=
            (int)sizeof(ppd)) {
            errno = ENAMETOOLONG;
            return -1;
        }
    }

    snprintf(line, sizeof(line),
             "*PageSize %s/%s:\t\"<</PageSize [%.3f %.3f] /ImagingBBox null>> setpagedevice\"\n",
             id, name, m->width_mm * pt, m->height_mm * pt);
    if (update_text_file(ppd, id, "*CloseUI: *PageSize", line) != 0) return -1;
    snprintf(line, sizeof(line),
             "*PageRegion %s/%s:\t\"<</PageSize [%.3f %.3f] /ImagingBBox null>> setpagedevice\"\n",
             id, name, m->width_mm * pt, m->height_mm * pt);
    if (update_text_file(ppd, id, "*CloseUI: *PageRegion", line) != 0) return -1;
    snprintf(line, sizeof(line), "*ImageableArea %s/%s:\t\"%.2f %.2f %.2f %.2f\"\n", id, name,
             m->left_mm * pt, m->bottom_mm * pt,
             (m->width_mm - m->right_mm) * pt, (m->height_mm - m->top_mm) * pt);
    if (update_text_file(ppd, id, "*DefaultPaperDimension", line) != 0) return -1;
    snprintf(line, sizeof(line), "*PaperDimension %s/%s:\t\"%.2f %.2f\"\n", id, name,
             m->width_mm * pt, m->height_mm * pt);
    const char *paper_marker = file_contains(ppd, "*HWMargins:")
                                   ? "*HWMargins:" : "*OpenUI *BrMargin";
    if (update_text_file(ppd, id, paper_marker, line) != 0) return -1;
    /* Older versions added a UIConstraints line for every Feed value when
     * registering die-cut/black-mark media.  Since Feed has no "None" choice
     * in this PPD (and defaults to 3), that makes the custom PageSize
     * unselectable.  Feed is ignored for non-continuous media by cups_filter,
     * so no PPD constraint is necessary.  Also clean any stale constraints
     * created by an older td2130-paper when this media is re-registered. */
    if (remove_ppd_lines(ppd, "*UIConstraints:", id) != 0) return -1;
    fprintf(stderr, "registered media %s as %s; data=%s; ppd=%s\n",
            name, id, base, ppd);
    return 0;
}

static int number(const char *s, double *out) {
    char *end;
    errno = 0;
    *out = strtod(s, &end);
    return errno == 0 && end != s && *end == '\0' ? 0 : -1;
}

int main(int argc, char **argv) {
    td_media_definition m = {0};
    const char *queue = NULL, *name = NULL, *output = NULL;
    const char *install_root = NULL, *media_root = NULL, *ppd = NULL;
    uint8_t data[TD2130_MEDIA_DEFINITION_SIZE];
    char default_output[1024];
    FILE *fp;
    int opt;

    static const struct option long_options[] = {
        {"install-root", required_argument, NULL, 1000},
        {"ppd", required_argument, NULL, 1001},
        {"media-root", required_argument, NULL, 1002},
        {NULL, 0, NULL, 0}
    };
    while ((opt = getopt_long(argc, argv, "P:n:w:h:g:t:b:l:r:S:m:o:d:H:O:?",
                              long_options, NULL)) != -1) {
        double *dst = NULL;
        switch (opt) {
        case 'P': queue = optarg; continue;
        case 'n': name = optarg; continue;
        case 'w': dst = &m.width_mm; break;
        case 'h': dst = &m.height_mm; break;
        case 'g': dst = &m.gap_mm; break;
        case 't': dst = &m.top_mm; break;
        case 'b': dst = &m.bottom_mm; break;
        case 'l': dst = &m.left_mm; break;
        case 'r': dst = &m.right_mm; break;
        case 'm': dst = &m.mark_length_mm; break;
        case 'o': dst = &m.mark_offset_mm; break;
        case 'S': {
            double sensor;
            if (number(optarg, &sensor) != 0 || sensor != (int)sensor ||
                sensor < 0 || sensor > 2) {
                fprintf(stderr, "invalid sensor type: %s\n", optarg);
                return 2;
            }
            m.sensor = (td_media_sensor)(int)sensor;
            continue;
        }
        case 'd': {
            double value;
            if (number(optarg, &value) != 0 || (value != 203 && value != 300)) {
                fprintf(stderr, "resolution must be 203 or 300 dpi\n");
                return 2;
            }
            m.dpi = (unsigned)value;
            continue;
        }
        case 'H': {
            double value;
            if (number(optarg, &value) != 0 || value < 1 || value > 65535 ||
                value != (unsigned)value) {
                fprintf(stderr, "invalid print-head width: %s\n", optarg);
                return 2;
            }
            m.head_dots = (unsigned)value;
            continue;
        }
        case 'O': output = optarg; continue;
        case 1000: install_root = optarg; continue;
        case 1001: ppd = optarg; continue;
        case 1002: media_root = optarg; continue;
        default: usage(opt == '?' ? stderr : stdout); return opt == '?' ? 2 : 0;
        }
        if (number(optarg, dst) != 0) {
            fprintf(stderr, "invalid number: %s\n", optarg);
            return 2;
        }
    }

    if (!queue || !name || !*name || m.width_mm == 0.0 || strlen(name) > 99 ||
        strchr(name, '/') || strchr(name, ':') || strchr(name, '\n') || strchr(queue, '/')) {
        usage(stderr);
        return 2;
    }
    if (td2130_build_media_definition(data, &m) != 0) {
        fprintf(stderr, "invalid or unsupported paper geometry\n");
        return 2;
    }
    if ((install_root || media_root) &&
        register_media(install_root,
                       media_root ? media_root : getenv("TD2130_MEDIA_ROOT"),
                       queue, name, ppd, &m, data) != 0) {
        fprintf(stderr, "registration failed: %s\n", strerror(errno));
        return 1;
    }
    if (!output && !install_root && !media_root) {
        (void)snprintf(default_output, sizeof(default_output), "%s.bin", name);
        output = default_output;
    }
    if (!output) return 0;
    fp = fopen(output, "wb");
    if (!fp) {
        fprintf(stderr, "%s: %s\n", output, strerror(errno));
        return 1;
    }
    if (fwrite(data, 1, sizeof(data), fp) != sizeof(data) || fclose(fp) != 0) {
        fprintf(stderr, "%s: write failed\n", output);
        return 1;
    }
    fprintf(stderr, "created %s for queue %s (%zu bytes)\n",
            output, queue, sizeof(data));
    return 0;
}
