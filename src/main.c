#include "pbm.h"
#include "td2130.h"
#include "usb_transport.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(FILE *f) {
    fprintf(f, "Usage:\n"
        "  td2130 devices\n"
        "  td2130 status\n"
        "  td2130 print FILE.pbm [--media 57|58|50x15] [--margin DOTS] [--no-rotate] [--dry-run FILE.bin]\n");
}

static int status_command(void) {
    char error[256]; td_usb *usb = NULL; uint8_t raw[32]; td_status status;
    if (td_usb_open(&usb, error, sizeof(error)) ||
        td_usb_read_status(usb, raw, 3000, error, sizeof(error))) {
        fprintf(stderr, "error: %s\n", error); td_usb_close(usb); return 1;
    }
    td_usb_close(usb);
    if (td2130_parse_status(&status, raw)) { fprintf(stderr, "error: invalid TD-2130N status\n"); return 1; }
    char state[64];
    printf("TD-2130N: %s, media=%u mm, type=0x%02x, length=%u mm, status=0x%02x\n",
           td2130_status_error(&status, state, sizeof(state)), status.media_width_mm,
           status.media_type, status.media_length_mm, status.status_type);
    return status.error1 || status.error2;
}

static int write_file(const char *path, const uint8_t *data, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    int rc = fwrite(data, 1, len, f) == len ? 0 : -1;
    if (fclose(f)) rc = -1;
    return rc;
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "devices")) return td_usb_list() == 0 ? 0 : 1;
    if (argc == 2 && !strcmp(argv[1], "status")) return status_command();
    if (argc < 3 || strcmp(argv[1], "print")) { usage(stderr); return 2; }
    unsigned media = 58, media_length = 0, margin = 35; int rotate = 1; const char *dry = NULL;
    for (int i = 3; i < argc; ++i) {
        if (!strcmp(argv[i], "--rotate")) rotate = 1;
        else if (!strcmp(argv[i], "--no-rotate")) rotate = 0;
        else if (!strcmp(argv[i], "--media") && i + 1 < argc) {
            const char *value = argv[++i];
            if (!strcmp(value, "50x15")) { media = 50; media_length = 15; margin = 0; }
            else { media = (unsigned)strtoul(value, NULL, 10); media_length = 0; }
        }
        else if (!strcmp(argv[i], "--margin") && i + 1 < argc) margin = (unsigned)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--dry-run") && i + 1 < argc) dry = argv[++i];
        else { usage(stderr); return 2; }
    }
    char error[256]; pbm_image image; td_buffer job; td_buffer_init(&job);
    if (pbm_read(argv[2], &image, error, sizeof(error))) { fprintf(stderr, "error: %s\n", error); return 1; }
    if (td2130_build_job(&job, image.pixels, image.width, image.height, image.stride,
                         media, media_length, margin, rotate)) {
        fprintf(stderr, "error: unsupported dimensions (50x15 requires a PBM no wider than 554 and exactly 106 rows)\n"); pbm_free(&image); return 1;
    }
    pbm_free(&image);
    int rc = 0;
    if (dry) {
        if (write_file(dry, job.data, job.len)) { fprintf(stderr, "error: cannot write %s: %s\n", dry, strerror(errno)); rc = 1; }
        else printf("wrote %zu-byte Brother Raster job to %s\n", job.len, dry);
    } else {
        td_usb *usb = NULL; uint8_t raw[32]; td_status status;
        if (td_usb_open(&usb, error, sizeof(error))) { fprintf(stderr, "error: %s\n", error); rc = 1; }
        else if (td_usb_read_status(usb, raw, 3000, error, sizeof(error))) {
            fprintf(stderr, "error: %s\n", error); rc = 1;
        } else if (td2130_parse_status(&status, raw)) {
            fprintf(stderr, "error: invalid TD-2130N status response\n"); rc = 1;
        } else if (status.error1 || status.error2) {
            char state[64]; fprintf(stderr, "printer not ready: %s\n", td2130_status_error(&status, state, sizeof(state))); rc = 1;
        } else if (td_usb_write(usb, job.data, job.len, error, sizeof(error))) {
            fprintf(stderr, "error: %s\n", error); rc = 1;
        } else printf("sent %zu-byte print job\n", job.len);
        td_usb_close(usb);
    }
    td_buffer_free(&job);
    return rc;
}
