/* Graphics from the host, see lk_gfx.h. */
#include "lk_gfx.h"
#ifndef LKG_HOST_TEST
#include "fw_api.h"
#endif

static uint8_t fb[LKG_PAGES][LKG_W];
#ifndef LKG_HOST_TEST
static uint32_t shown_until, next_draw;
#endif
static uint8_t active;

uint8_t *lkg_bitmap(void) { return &fb[0][0]; }

uint32_t lkg_unpack(const uint8_t *in, uint32_t n_in, uint8_t *out, uint32_t max)
{
    uint32_t k = 0;
    for (uint32_t i = 0; i < n_in && k < max; ) {
        uint8_t hi = in[i++];
        for (uint32_t b = 0; b < 7 && i < n_in && k < max; b++, i++)
            out[k++] = (uint8_t)(in[i] | (((hi >> b) & 1u) << 7));
    }
    return k;
}

#ifndef LKG_HOST_TEST
static void draw(void)
{
    void *ctx = disp_dl_ctx();
    for (uint32_t p = 0; p < LKG_PAGES; p++)
        for (uint32_t h = 0; h < 2; h++) {
            dl_image_desc_t d = {&fb[p][h * 64], 64, 1};
            dl_draw_ptr(ctx, &d, h * 64, (LKG_TOP + p) * 8);
        }
}
#endif

int lkg_sysex(const uint8_t *msg, uint32_t len)
{
    static const uint8_t head[] = {0xF0, 0x7D, 0x4C, 0x4B};
    if (len < 6)
        return 0;
    for (uint32_t i = 0; i < 4; i++)
        if (msg[i] != head[i])
            return 0;
    const uint8_t cmd = msg[4];
    const uint32_t end = msg[len - 1] == 0xF7 ? len - 1 : len;   /* data without F7 */
    if (cmd == 0x02) {
        if (end < 8)
            return 1;
        uint32_t page = msg[5], x = msg[6], n = msg[7];
        if (page >= LKG_PAGES || x >= LKG_W || n == 0 || n > 32)
            return 1;
        if (x + n > LKG_W)
            n = LKG_W - x;
        lkg_unpack(msg + 8, end - 8, &fb[page][x], n);
        return 1;
    }
    if (cmd == 0x04) {
        for (uint32_t p = 0; p < LKG_PAGES; p++)
            for (uint32_t x = 0; x < LKG_W; x++)
                fb[p][x] = 0;
        return 1;
    }
    if (cmd == 0x03) {
#ifndef LKG_HOST_TEST
        char title[17];
        uint32_t k = 0;
        for (uint32_t i = 5; i < end && k < 16; i++)
            title[k++] = (msg[i] >= 0x20 && msg[i] < 0x7F) ? (char)msg[i] : '?';
        title[k] = 0;
        ui_popup_begin();
        disp_set_screen(1, 0);
        disp_text(1, 0, title);
        disp_text(1, 1, "");
        const uint32_t now = g_sys_ms;
        shown_until = now + LKG_SHOW_MS;
        next_draw = now + 20;             /* after the popup has drawn its fields */
#endif
        active = 1;
        return 1;
    }
    return 0;
}

void lkg_poll(void)
{
#ifndef LKG_HOST_TEST
    if (!active)
        return;
    const uint32_t now = g_sys_ms;
    if ((int32_t)(now - shown_until) >= 0) {
        active = 0;
        return;
    }
    if ((int32_t)(now - next_draw) >= 0) {
        draw();
        next_draw = now + LKG_REDRAW_MS;
    }
#endif
}
