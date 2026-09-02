#include "td2130.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    uint8_t bitmap[142]; memset(bitmap, 0, sizeof(bitmap)); bitmap[0] = 0x80;
    td_buffer b; td_buffer_init(&b);
    assert(td2130_build_job(&b, bitmap, 1, 142, 1, 58, 0, 35, false) == 0);
    assert(b.len == 202 + 4 + 13 + 4 + 5 + 2 + 142 * 87 + 1);
    assert(b.data[200] == 0x1b && b.data[201] == 0x40);
    assert(b.data[b.len - 1] == 0x1a);
    size_t raster = 202 + 4 + 13 + 4 + 5 + 2;
    assert(b.data[raster] == 0x67 && b.data[raster + 2] == 84);
    /* PBM x=0 is mapped to the opposite physical head direction. For a
       centered 1-pixel image this is head dot 336. */
    assert((b.data[raster + 3 + 336 / 8] & (0x80u >> (336 % 8))) != 0);
    td_buffer_free(&b);

    uint8_t die_cut[106]; memset(die_cut, 0, sizeof(die_cut));
    td_buffer_init(&b);
    assert(td2130_build_job(&b, die_cut, 1, 106, 1, 50, 15, 0, false) == 0);
    assert(b.data[209] == 0xce && b.data[210] == 0x0b);
    assert(b.data[211] == 50 && b.data[212] == 15);
    td_buffer_free(&b);

    uint8_t custom[165 * 55]; memset(custom, 0, sizeof(custom));
    td_buffer_init(&b);
    assert(td2130_build_job(&b, custom, 436, 165, 55, 40, 20, 0, true) == 0);
    td_buffer_free(&b);

    td_print_options compressed = {50, 15, 0, true, true, true, false, true};
    td_buffer_init(&b);
    assert(td2130_build_job_ex(&b, die_cut, 1, 106, 1, &compressed) == 0);
    assert(b.data[209] == 0x8e);
    assert(b.data[222] == 0x18);
    assert(b.data[229] == 0x02);
    assert(b.len == 230 + 106 + 1);
    td_buffer_free(&b);

    uint8_t raw[32] = {0x80, 0x20, 0x42, 0x35, 0x36};
    raw[10] = 58; raw[11] = 0x4a;
    td_status s; assert(td2130_parse_status(&s, raw) == 0);
    assert(s.media_width_mm == 58 && s.media_type == 0x4a);
    puts("protocol tests passed");
    return 0;
}
