#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    const char *option;
    const char *key;
    enum { VALUE_RANGE, VALUE_ENUM, VALUE_MEDIA } kind;
    const char *known_values;
    long minimum;
    long maximum;
} option_map;

typedef struct {
    const option_map *definition;
    char value[128];
} setting;

typedef struct {
    const char *data;
    size_t length;
} text_span;

static const option_map options[] = {
    {"-copy", "Copies", VALUE_RANGE, NULL, 1, 10},
    {"-cutlabel", "CutLabel", VALUE_RANGE, NULL, 0, 30},
    {"-cutend", "CutAtEnd", VALUE_ENUM, "OFF,ON", 0, 0},
    {"-trimtape", "Trimtape", VALUE_ENUM, "OFF,ON", 0, 0},
    {"-compress", "Compress", VALUE_ENUM, "OFF,ON", 0, 0},
    {"-brit", "Brightness", VALUE_RANGE, NULL, -50, 50},
    {"-cont", "Contrast", VALUE_RANGE, NULL, -50, 50},
    {"-half", "Halftone", VALUE_ENUM, "ERROR,BINARY,DITHER", 0, 0},
    {"-mirro", "MirrorPrinting", VALUE_ENUM, "OFF,ON", 0, 0},
    {"-rotate", "RotatePrinting", VALUE_ENUM, "OFF,ON", 0, 0},
    {"-peeler", "Peeler", VALUE_ENUM, "OFF,ON", 0, 0},
    {"-quality", "Quality", VALUE_ENUM, "SPEED,QUALITY", 0, 0},
    /* 203 ppi is a project extension for related 448-dot hardware. */
    {"-reso", "Resolution", VALUE_ENUM, "203,300", 0, 0},
    {"-feed", "Feed", VALUE_RANGE, NULL, 3, 30},
    {"-media", "MediaSize", VALUE_MEDIA,
     "30x30,40x40,40x50,40x60,50x30,51x26,60x60,57X1,58X1", 0, 0},
    {"-collate", "Collate", VALUE_ENUM, "OFF,ON", 0, 0},
    {NULL, NULL, VALUE_ENUM, NULL, 0, 0}
};

static void usage(FILE *f) {
    fputs("usage: td2130-config -P queue [options]\n"
          "  -copy 1..10       -cutlabel 0..30\n"
          "  -cutend OFF|ON    -trimtape OFF|ON\n"
          "  -compress OFF|ON  -brit -50..50  -cont -50..50\n"
          "  -half ERROR|BINARY|DITHER\n"
          "  -mirro OFF|ON     -rotate OFF|ON  -peeler OFF|ON\n"
          "  -quality SPEED|QUALITY  -reso 203|300\n"
          "  -feed 3..30       -media name     -collate OFF|ON\n"
          "  --root dir        redirect /opt paths into a staging root\n"
          "  --rc-file file    edit this rc file (-rcfile is also accepted)\n"
          "  --func-file file  validate against this function-definition file\n"
          "  --show            print the resulting/current configuration\n", f);
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

static const option_map *mapped_option(const char *option) {
    for (size_t i = 0; options[i].option; ++i)
        if (!strcmp(option, options[i].option)) return &options[i];
    return NULL;
}

static int parse_integer(const char *text, long *value) {
    char *end;
    errno = 0;
    *value = strtol(text, &end, 10);
    return errno == 0 && end != text && *end == '\0' ? 0 : -1;
}

static char *trim(char *s) {
    while (*s == ' ' || *s == '\t') ++s;
    char *end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' ||
                       end[-1] == '\r' || end[-1] == '\n')) --end;
    *end = '\0';
    return s;
}

static size_t line_length_without_cr(const char *line, const char *newline) {
    size_t length = newline ? (size_t)(newline - line) : strlen(line);
    if (length && line[length - 1] == '\r') --length;
    return length;
}

static text_span trim_span(text_span span) {
    while (span.length && (*span.data == ' ' || *span.data == '\t')) {
        ++span.data;
        --span.length;
    }
    while (span.length && (span.data[span.length - 1] == ' ' ||
                           span.data[span.length - 1] == '\t'))
        --span.length;
    return span;
}

static text_span line_span(const char *line, const char *newline) {
    text_span span = {line, line_length_without_cr(line, newline)};
    return trim_span(span);
}

static int span_equals(text_span span, const char *text) {
    size_t length = strlen(text);
    return span.length == length && !strncmp(span.data, text, length);
}

static int span_key_value(text_span line, const char *key, text_span *value) {
    const char *equal = memchr(line.data, '=', line.length);
    if (!equal) return 0;
    text_span name = {line.data, (size_t)(equal - line.data)};
    name = trim_span(name);
    if (!span_equals(name, key)) return 0;
    value->data = equal + 1;
    value->length = line.length - (size_t)(equal + 1 - line.data);
    *value = trim_span(*value);
    return 1;
}

static int in_list(const char *list, const char *value) {
    char copy[1024];
    if (strlen(list) >= sizeof(copy)) return 0;
    strcpy(copy, list);
    char *body = trim(copy);
    if (*body == '{') ++body;
    char *end = strrchr(body, '}');
    if (end) *end = '\0';
    body = trim(body);
    if (*body == '"') {
        ++body;
        end = strrchr(body, '"');
        if (end) *end = '\0';
        char *tilde = strchr(body, '~');
        long number, minimum, maximum;
        if (!tilde || parse_integer(value, &number) != 0) return 0;
        *tilde++ = '\0';
        return parse_integer(trim(body), &minimum) == 0 &&
               parse_integer(trim(tilde), &maximum) == 0 &&
               number >= minimum && number <= maximum;
    }
    for (char *token = strtok(body, ","); token; token = strtok(NULL, ","))
        if (!strcmp(trim(token), value)) return 1;
    return 0;
}

static int selection_rule(const char *func, const char *key,
                          char *rule, size_t rule_size) {
    bool section = false;
    const char *p = func;
    while (*p) {
        const char *end = strchr(p, '\n');
        text_span line = line_span(p, end), value;
        if (span_equals(line, "[SelectionItem]")) section = true;
        else if (section && line.length && line.data[0] == '[') break;
        else if (section && span_key_value(line, key, &value)) {
            if (value.length >= rule_size) return -1;
            memcpy(rule, value.data, value.length);
            rule[value.length] = '\0';
            return 0;
        }
        p = end ? end + 1 : p + strlen(p);
    }
    return -1;
}

static int is_hex_string(const char *text, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        if (!((text[i] >= '0' && text[i] <= '9') ||
              (text[i] >= 'A' && text[i] <= 'F') ||
              (text[i] >= 'a' && text[i] <= 'f')))
            return 0;
    }
    return 1;
}

static int custom_media(const char *func, const char *value,
                        char normalized[128]) {
    bool section = false;
    int matches = 0;
    const char *p = func;
    while (*p) {
        const char *end = strchr(p, '\n');
        text_span line = line_span(p, end);
        const char *line_data = line.data;
        size_t len = line.length;
        if (span_equals(line, "[CustomTape]"))
            section = true;
        else if (section && len && line_data[0] == '[') break;
        else if (section && len) {
            const char *slash = memchr(line_data, '/', len);
            const char *colon = memchr(line_data, ':', len);
            size_t id_len = slash ? (size_t)(slash - line_data) : 0;
            size_t name_len = slash ? len - id_len - 1 : 0;
            if (colon && slash && colon > slash) name_len = (size_t)(colon - slash - 1);
            int hi = len > 4 && line_data[3] >= '0' && line_data[3] <= '9' ? line_data[3] - '0' :
                     len > 4 && line_data[3] >= 'A' && line_data[3] <= 'F' ? line_data[3] - 'A' + 10 :
                     len > 4 && line_data[3] >= 'a' && line_data[3] <= 'f' ? line_data[3] - 'a' + 10 : -1;
            int lo = len > 4 && line_data[4] >= '0' && line_data[4] <= '9' ? line_data[4] - '0' :
                     len > 4 && line_data[4] >= 'A' && line_data[4] <= 'F' ? line_data[4] - 'A' + 10 :
                     len > 4 && line_data[4] >= 'a' && line_data[4] <= 'f' ? line_data[4] - 'a' + 10 : -1;
            bool valid_id = slash && id_len == 15 && !strncmp(line_data, "BrL", 3) &&
                            is_hex_string(line_data + 3, 12) && hi >= 0 && lo >= 0 &&
                            name_len > 0 && name_len == (size_t)(hi * 16 + lo);
            if (valid_id && ((strlen(value) == id_len && !strncmp(value, line_data, id_len)) ||
                          (strlen(value) == name_len && !strncmp(value, slash + 1, name_len)))) {
                if (id_len >= 128) return -1;
                if (matches) return -1;
                memcpy(normalized, line_data, id_len);
                normalized[id_len] = '\0';
                ++matches;
            }
        }
        p = end ? end + 1 : p + strlen(p);
    }
    return matches ? 1 : 0;
}

static int validate_setting(const char *func, const option_map *definition,
                            const char *value, char normalized[128]) {
    char rule[1024];
    long number;
    if (selection_rule(func, definition->key, rule, sizeof(rule)) != 0) return 4;
    if (definition->kind == VALUE_RANGE) {
        if (parse_integer(value, &number) != 0 || number < definition->minimum ||
            number > definition->maximum)
            return 12;
        snprintf(normalized, 128, "%ld", number);
        return in_list(rule, normalized) ? 0 : 12;
    }
    if (definition->kind == VALUE_ENUM) {
        if (!in_list(definition->known_values, value)) return 2;
        if (!in_list(rule, value)) return 11;
        snprintf(normalized, 128, "%s", value);
        return 0;
    }
    if (in_list(definition->known_values, value)) {
        if (!in_list(rule, value)) return 11;
        snprintf(normalized, 128, "%s", value);
        return 0;
    }
    return custom_media(func, value, normalized) == 1 ? 0 : 2;
}

static size_t model_section_count(const char *data) {
    size_t count = 0;
    const char *p = data;
    while (*p) {
        const char *end = strchr(p, '\n');
        text_span line = line_span(p, end);
        if (span_equals(line, "[td2130n]")) ++count;
        p = end ? end + 1 : p + strlen(p);
    }
    return count;
}

static char *replace_setting(const char *input, const char *key,
                             const char *value) {
    size_t capacity = strlen(input) + strlen(key) + strlen(value) + 32;
    char *out = malloc(capacity);
    size_t used = 0;
    bool section = false, replaced = false, inserted = false;
    const char *p = input;
    const char *preferred_eol = strstr(input, "\r\n") ? "\r\n" : "\n";
    if (!out) return NULL;
    while (*p) {
        const char *end = strchr(p, '\n');
        size_t len = end ? (size_t)(end - p + 1) : strlen(p);
        size_t content_len = line_length_without_cr(p, end);
        text_span line = line_span(p, end), ignored_value;
        bool header = line.length && line.data[0] == '[';
        bool target_header = span_equals(line, "[td2130n]");
        if (section && header && !target_header && !replaced && !inserted) {
            used += (size_t)snprintf(out + used, capacity - used, "%s=%s%s",
                                     key, value, preferred_eol);
            inserted = true;
        }
        if (header) section = target_header;
        bool target_key = section && span_key_value(line, key, &ignored_value);
        if (target_key) {
            if (!replaced) {
                const char *line_eol = !end ? "" :
                                       content_len < (size_t)(end - p) ? "\r\n" : "\n";
                used += (size_t)snprintf(out + used, capacity - used, "%s=%s%s",
                                         key, value, line_eol);
                replaced = true;
            }
        } else {
            memcpy(out + used, p, len);
            used += len;
        }
        p += len;
    }
    if (!replaced && !inserted)
        used += (size_t)snprintf(out + used, capacity - used,
                                "%s%s=%s%s",
                                used && out[used - 1] != '\n' ? preferred_eol : "",
                                key, value, preferred_eol);
    out[used] = '\0';
    return out;
}

static int write_temporary(char path[1200], const char *data, mode_t mode) {
    int fd = mkstemp(path);
    if (fd < 0) return -1;
    FILE *f = fdopen(fd, "wb");
    if (!f) { close(fd); unlink(path); return -1; }
    size_t size = strlen(data);
    int failed = fchmod(fd, mode) != 0 || fwrite(data, 1, size, f) != size ||
                 fflush(f) != 0 || fsync(fd) != 0;
    if (fclose(f) != 0) failed = 1;
    if (failed) { unlink(path); return -1; }
    return 0;
}

static int save_atomic(const char *path, const char *data) {
    char temporary[1200], backup_temporary[1200], backup[1200];
    struct stat st;
    size_t old_size;
    char *old_data;
    if (lstat(path, &st) != 0 || !S_ISREG(st.st_mode)) return -1;
    old_data = read_file(path, &old_size);
    (void)old_size;
    if (!old_data) return -1;
    if (snprintf(temporary, sizeof(temporary), "%s.new.XXXXXX", path) >=
            (int)sizeof(temporary) ||
        snprintf(backup_temporary, sizeof(backup_temporary), "%s.old.XXXXXX", path) >=
            (int)sizeof(backup_temporary) ||
        snprintf(backup, sizeof(backup), "%s.old", path) >= (int)sizeof(backup)) {
        free(old_data);
        errno = ENAMETOOLONG; return -1;
    }
    mode_t mode = st.st_mode & 0777;
    if (write_temporary(temporary, data, mode) != 0 ||
        write_temporary(backup_temporary, old_data, mode) != 0) {
        int saved = errno;
        unlink(temporary); unlink(backup_temporary); free(old_data);
        errno = saved; return -1;
    }
    free(old_data);
    if (rename(backup_temporary, backup) != 0 || rename(temporary, path) != 0) {
        int saved = errno;
        unlink(temporary); unlink(backup_temporary);
        errno = saved; return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    const char *root = "/", *rc_path = NULL, *func_path = NULL, *queue = NULL;
    char default_rc[1024], default_func[1024];
    setting changes[32];
    size_t change_count = 0, rc_size, func_size;
    bool show = false;

    if (argc == 1) { usage(stdout); return 0; }
    if (argc < 3 || strcmp(argv[1], "-P")) { usage(stderr); return 2; }
    queue = argv[2];
    if (!*queue) { usage(stderr); return 2; }
    for (int i = 3; i < argc;) {
        if (!strcmp(argv[i], "--show")) { show = true; ++i; continue; }
        if ((!strcmp(argv[i], "--root") || !strcmp(argv[i], "--rc-file") ||
             !strcmp(argv[i], "-rcfile") || !strcmp(argv[i], "--func-file")) && i + 1 < argc) {
            if (!strcmp(argv[i], "--root")) root = argv[i + 1];
            else if (!strcmp(argv[i], "--func-file")) func_path = argv[i + 1];
            else rc_path = argv[i + 1];
            i += 2;
            continue;
        }
        const option_map *definition = mapped_option(argv[i]);
        if (!definition || i + 1 >= argc || change_count == 32) {
            fprintf(stderr, "td2130-config: invalid option: %s\n", argv[i]);
            return 2;
        }
        size_t value_length = strlen(argv[i + 1]);
        bool invalid_value = value_length >= sizeof(changes[change_count].value);
        for (size_t j = 0; !invalid_value && j < value_length; ++j) {
            unsigned char c = (unsigned char)argv[i + 1][j];
            if (c < 0x20 || c == 0x7f) invalid_value = true;
        }
        if (invalid_value) {
            fprintf(stderr, "td2130-config: invalid value for %s\n", argv[i]);
            return 2;
        }
        changes[change_count].definition = definition;
        memcpy(changes[change_count].value, argv[i + 1], value_length + 1);
        ++change_count;
        i += 2;
    }
    const char *slash = !strcmp(root, "/") ? "" : root;
    if (!func_path) {
        int length = snprintf(default_func, sizeof(default_func),
                              "%s/opt/brother/PTouch/td2130n/inf/brtd2130nfunc",
                              slash);
        if (length < 0 || length >= (int)sizeof(default_func)) {
            fprintf(stderr, "td2130-config: function-file path is too long\n");
            return 1;
        }
        func_path = default_func;
    }
    if (!rc_path) {
        int length = snprintf(default_rc, sizeof(default_rc),
                              "%s/opt/brother/PTouch/td2130n/inf/brtd2130nrc",
                              slash);
        if (length < 0 || length >= (int)sizeof(default_rc)) {
            fprintf(stderr, "td2130-config: rc-file path is too long\n");
            return 1;
        }
        rc_path = default_rc;
    }
    char *func = read_file(func_path, &func_size);
    char *rc = read_file(rc_path, &rc_size);
    if (!func || !rc) {
        fprintf(stderr, "td2130-config: cannot open %s: %s\n",
                !func ? func_path : rc_path, strerror(errno));
        free(func); free(rc); return 1;
    }
    if (memchr(func, '\0', func_size) || memchr(rc, '\0', rc_size)) {
        fprintf(stderr, "td2130-config: configuration files must be plain text\n");
        free(func); free(rc); return 1;
    }
    if (model_section_count(rc) != 1) {
        fprintf(stderr, "td2130-config: rc file must contain exactly one [td2130n] section\n");
        free(func); free(rc); return 5;
    }
    for (size_t i = 0; i < change_count; ++i) {
        char normalized[128];
        const char *key = changes[i].definition->key;
        int validation = validate_setting(func, changes[i].definition,
                                          changes[i].value, normalized);
        if (validation != 0) {
            if (validation == 12)
                fprintf(stderr, "td2130-config: %s is out of range: %s\n",
                        key, changes[i].value);
            else if (validation == 11)
                fprintf(stderr, "td2130-config: %s value is not permitted: %s\n",
                        key, changes[i].value);
            else if (validation == 2)
                fprintf(stderr, "td2130-config: invalid %s value: %s\n",
                        key, changes[i].value);
            else
                fprintf(stderr, "td2130-config: schema has no %s rule\n",
                        key);
            free(func); free(rc); return validation;
        }
        char *updated = replace_setting(rc, key, normalized);
        if (!updated) { free(func); free(rc); return 1; }
        free(rc);
        rc = updated;
    }
    free(func);
    if (change_count && save_atomic(rc_path, rc) != 0) {
        fprintf(stderr, "td2130-config: cannot save %s: %s\n", rc_path, strerror(errno));
        free(rc); return 1;
    }
    if (show) fputs(rc, stdout);
    else if (!change_count) usage(stdout);
    (void)queue;
    free(rc);
    return 0;
}
