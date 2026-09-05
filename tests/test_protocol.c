#include "td2130.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    uint8_t media[TD2130_MEDIA_DEFINITION_SIZE];
    td_media_definition continuous = {
        .width_mm = 30, .height_mm = 30, .gap_mm = 3,
        .top_mm = 3, .bottom_mm = 3, .left_mm = 1.5, .right_mm = 1.5,
        .sensor = TD_MEDIA_CONTINUOUS
    };
    assert(td2130_build_media_definition(media, &continuous) == 0);
    assert(memcmp(media, "\033iUw\001\077", 6) == 0);
    assert(media[6] == 4 && media[7] == 30);
    assert(media[10] == 30); /* continuous roll width */
    assert(media[11] == 176 && media[12] == 0); /* centered head offset */
    assert(media[13] == 62 && media[14] == 1);  /* printable width: 318 */
    assert(media[15] == 0 && media[16] == 0);
    assert(strcmp((char *)media + 6 + 0x4c, "30mm") == 0);
    assert(media[6 + 0x78] == 0);

    td_media_definition edge = continuous;
    edge.width_mm = 60;
    edge.height_mm = 40;
    assert(td2130_build_media_definition(media, &edge) == 0);
    assert(media[13] == 160 && media[14] == 2); /* clamp 673 to 672 dots */

    td_media_definition marked = continuous;
    marked.sensor = TD_MEDIA_BLACK_MARK;
    marked.mark_length_mm = 5;
    marked.mark_offset_mm = 2;
    assert(td2130_build_media_definition(media, &marked) == 0);
    assert(media[6] == 5 && media[6 + 0x78] == 2);
    assert(media[10] == 34); /* die-cut backing roll width */
    assert(media[6 + 0x6e] == 133 && media[6 + 0x6f] == 1); /* 354 + 35 */
    assert(media[6 + 0x74] == 24 && media[6 + 0x75] == 0);
    assert(media[6 + 0x76] == 59 && media[6 + 0x77] == 0);

    td_media_definition media203 = continuous;
    media203.dpi = 203;
    assert(td2130_build_media_definition(media, &media203) == 0);
    assert(media[11] == 116 && media[12] == 0); /* (448 - 216) / 2 */
    assert(media[13] == 216 && media[14] == 0);

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

    td_print_options compressed = {50, 15, 0, true, true, true, false, true,
                                   0, 0, 0};
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
