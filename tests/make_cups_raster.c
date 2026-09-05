#include <cups/raster.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    cups_raster_t *r = cupsRasterOpen(STDOUT_FILENO, CUPS_RASTER_WRITE);
    if (!r) return 1;
    cups_page_header2_t h;
    memset(&h, 0, sizeof(h));
    h.HWResolution[0] = h.HWResolution[1] = 300;
    if (argc > 1 && !strcmp(argv[1], "portrait40x60")) {
        h.PageSize[0] = 113; h.PageSize[1] = 170;
        h.cupsPageSize[0] = 113.386f; h.cupsPageSize[1] = 170.079f;
        h.cupsWidth = 436; h.cupsHeight = 638;
    } else if (argc > 1 && !strcmp(argv[1], "edge")) {
        h.PageSize[0] = 170; h.PageSize[1] = 113;
        h.cupsPageSize[0] = 170.079f; h.cupsPageSize[1] = 113.386f;
        h.cupsWidth = 673; h.cupsHeight = 402;
    } else if (argc > 1 && !strcmp(argv[1], "custom")) {
        h.PageSize[0] = 113; h.PageSize[1] = 57;
        h.cupsPageSize[0] = 113.386f; h.cupsPageSize[1] = 56.693f;
        h.cupsWidth = 472; h.cupsHeight = 236;
    } else {
        h.PageSize[0] = 142; h.PageSize[1] = 43;
        h.cupsPageSize[0] = 141.732f; h.cupsPageSize[1] = 42.520f;
        h.cupsWidth = argc > 1 && !strcmp(argv[1], "physical") ? 591 : 554;
        h.cupsHeight = argc > 1 && !strcmp(argv[1], "physical") ? 177 : 106;
    }
    h.cupsBitsPerColor = h.cupsBitsPerPixel = 8;
    h.cupsBytesPerLine = h.cupsWidth;
    h.cupsColorOrder = CUPS_ORDER_CHUNKED;
    h.cupsColorSpace = CUPS_CSPACE_K;
    if (!cupsRasterWriteHeader2(r, &h)) { cupsRasterClose(r); return 1; }
    unsigned char row[673];
    for (unsigned y = 0; y < h.cupsHeight; ++y) {
        memset(row, 0, sizeof(row));
        if (y == 0 || y + 1 == h.cupsHeight) memset(row, 0xff, sizeof(row));
        else { row[0] = 0xff; row[h.cupsWidth - 1] = 0xff; }
        if (cupsRasterWritePixels(r, row, h.cupsBytesPerLine) != h.cupsBytesPerLine) {
            cupsRasterClose(r); return 1;
        }
    }
    cupsRasterClose(r);
    return 0;
}
