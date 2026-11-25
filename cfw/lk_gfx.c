/* Graphics from the host, see lk_gfx.h. */
#include "lk_gfx.h"
#ifndef LKG_HOST_TEST
#include "fw_api.h"
#include "cc_pages.h"
#else
static void ccp_host_text(void) {}
#endif

static uint8_t fb[LKG_PAGES][LKG_W];
#ifndef LKG_HOST_TEST
static const uint8_t blank[LKG_TOP * LKG_W];   /* flash: clears the rows above the bitmap */
#endif
#ifndef LKG_HOST_TEST
/* Time: the SysTick counter at 0x2000150C stays 0 on the device (hardware test 2026-11-25), so
 * the Cortex-M4 cycle counter (DWT CYCCNT) is used, counted in units of 2^16 cycles
 * at the measured core clock. */
#define DEMCR     (*(volatile uint32_t *)0xE000EDFC)
#define DWT_CTRL  (*(volatile uint32_t *)0xE0001000)
#define DWT_CYC   (*(volatile uint32_t *)0xE0001004)
#define LKG_CPU_HZ 84000000u     /* measured 2026-11-25: 338 M cycles in about 4.0 s */
#define LKG_MS(ms) ((uint32_t)((uint64_t)(ms) * LKG_CPU_HZ / 1000u))
static uint32_t now_cyc(void)
{
    if (!(DWT_CTRL & 1u)) {
        DEMCR |= 1u << 24;               /* TRCENA */
        DWT_CYC = 0;
        DWT_CTRL |= 1u;                  /* CYCCNTENA */
    }
    return DWT_CYC;
}
static uint32_t shown_until, next_draw, n_poll;
static uint16_t n_draw, n_busy;           /* diagnostics (06) */
static uint8_t variant;                   /* 07: how to draw, see draw() */
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
/* 1 when drawn; 0 when the display list is busy (try again on the next pass) */
static int draw(void)
{
    void *ctx = disp_dl_ctx();
    if (!ctx || !dl_idle(ctx)) {
        n_busy++;
        return 0;
    }
    n_draw++;
    if (variant == 1) {                   /* fill rectangle: pages 2..7, whole width */
        dl_fill_rect(ctx, 0, 128, LKG_TOP, LKG_PAGES);
        return 1;
    }
    if (variant == 2) {                   /* a flash image: the boot logo (0x27F) at y 16 */
        dl_draw_image(ctx, 0x27F, 0, LKG_TOP * 8);
        return 1;
    }
    if (variant == 0) {                   /* default: the whole bitmap as one pointer image (works) */
        dl_image_desc_t d = {&fb[0][0], LKG_W, LKG_PAGES};
        dl_draw_ptr(ctx, &d, 0, LKG_TOP * 8);
        dl_image_desc_t b = {blank, LKG_W, LKG_TOP};   /* rest of a popup text above it */
        dl_draw_ptr(ctx, &b, 0, 0);
        return 1;
    }
    for (uint32_t p = 0; p < LKG_PAGES; p++)
        for (uint32_t h = 0; h < 2; h++) {
            dl_image_desc_t d = {&fb[p][h * 64], 64, 1};
            dl_draw_ptr(ctx, &d, h * 64, variant == 3 ? LKG_TOP + p : (LKG_TOP + p) * 8);   /* 3: y in pages (wrong) */
        }
    return 1;
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
    if (cmd >= 0x02 && cmd <= 0x04)
        ccp_host_text();                  /* the instrument answers: no CC popup of our own (cc_pages.c) */
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
#ifndef LKG_HOST_TEST
    if (cmd == 0x06) {                    /* diagnostics: reply F0 7D 4C 4B 06 <u32 as 5 x 7 bits> ... F7 */
        uint8_t *ctx = (uint8_t *)disp_dl_ctx();
        uint32_t v[10];
        v[0] = now_cyc(); v[1] = (uint32_t)ctx; v[2] = n_draw; v[3] = n_busy; v[4] = active;
        v[5] = ctx ? dl_idle(ctx) : 9;
        v[6] = ctx ? *(uint32_t *)(ctx + 8) : 0;         /* writer ring read */
        v[7] = ctx ? *(uint32_t *)(ctx + 12) : 0;        /* writer ring write */
        v[8] = ctx ? ctx[0x2DD] | (ctx[0x2DE] << 8) : 0;
        v[9] = n_poll;
        midi_port_t *port = g_app_ports[1];
        midi_put(port, 0xF0); midi_put(port, 0x7D); midi_put(port, 0x4C); midi_put(port, 0x4B); midi_put(port, 0x06);
        for (uint32_t i = 0; i < 10; i++)
            for (uint32_t k = 0; k < 5; k++)
                midi_put(port, (v[i] >> (28 - 7 * k)) & 0x7F);
        midi_put(port, 0xF7);
        return 1;
    }
    if (cmd == 0x07) {                    /* draw variant: 0 tiles 64x8 (y px), 1 fill, 2 flash logo, 3 tiles (y pages), 4 one image */
        variant = end > 5 ? msg[5] : 0;
        return 1;
    }
#endif
    if (cmd == 0x03) {
#ifndef LKG_HOST_TEST
        /* the popup only once (empty: its text would sit in the middle of the bitmap and every
         * text change redraws it over the bitmap); later shows only keep it up. The host draws
         * its own text into the bitmap. */
        const uint32_t now = now_cyc();
        if (!active) {
            ui_popup_begin();
            disp_set_screen(1, 0);
            disp_text(1, 0, "");
            disp_text(1, 1, "");
            next_draw = now + LKG_MS(20); /* after the popup has drawn its fields */
        }
        shown_until = now + LKG_MS(LKG_SHOW_MS);
#endif
        active = 1;
        return 1;
    }
    return 0;
}

void lkg_stop(void) { active = 0; }

void lkg_poll(void)
{
#ifndef LKG_HOST_TEST
    n_poll++;
    if (!active)
        return;
    const uint32_t now = now_cyc();
    if ((int32_t)(now - shown_until) >= 0) {
        active = 0;
        return;
    }
    if ((int32_t)(now - next_draw) >= 0 && draw())
        next_draw = now + LKG_MS(LKG_REDRAW_MS);
#endif
}
