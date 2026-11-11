/* CC pages: encoder mode Custom 1 gets CCP_PAGES pages of 8 CCs instead of the stock 2.
 *
 * Active while the encoder layout is Custom 1 (g_enc_layout = 6), Shift is up and no menu has
 * captured the encoder:
 *   encoders 1..8      CC (CCP_FIRST_CC + page * 8 + encoder) on channel CCP_CHANNEL, 0..127.
 *                      Every page remembers its eight values (RAM only, 0 after power-on).
 *   up / down buttons  previous / next page, handled by the stock firmware as before. Three
 *                      hooks (cfw/hooks.txt) widen it: cfw_enc_page_count reports CCP_PAGES
 *                      pages for Custom 1 (button LEDs and limits follow), cfw_enc_set_page
 *                      stores pages above 1, cfw_enc_page_popup shows the page and its CCs.
 * The CCs go out on the MIDI port (USB + DIN/TRS), like the stock custom modes. The stock
 * assignments of Custom 1 (Components) are not used while this is active; Custom 2..4 and
 * all other encoder modes are untouched.
 * The input dispatchers in cfw_init.c ask ccp_io_event / ccp_enc_event after the Turing
 * Machine mode has passed on an event, so in pad mode Custom 4 only encoder 8 lands here.
 */
#include <stdint.h>
#include "fw_api.h"
#include "tm_mode.h"
#include "cc_pages.h"

#define CCP_PAGES         12
#define CCP_FIRST_CC      20          /* page 1 = CC 20..27, ... page 12 = CC 108..115 */
#define CCP_CHANNEL       0           /* 0-based: 0 = MIDI channel 1 */
#define CCP_STEP          1           /* value change per encoder tick */

#define ENC_FIRST         56          /* encoder 1 */
#define ENC_COUNT         8
#define ENC_LAYOUT_CUSTOM1 6
#define FEAT_SHIFT        0x3F
#define MIDI_PORT         1           /* g_app_ports[1] = USB MIDI port (+ DIN via link) */

_Static_assert(CCP_FIRST_CC + CCP_PAGES * ENC_COUNT <= 120, "CCs 120..127 are channel mode messages");

static uint8_t value[CCP_PAGES][ENC_COUNT];
static uint8_t owned;                 /* encoders whose current touch belongs to this mode */

/* ---- helpers ---- */

static char *put_str(char *p, const char *s)
{
    while (*s)
        *p++ = *s++;
    return p;
}

static char *put_num(char *p, uint32_t v)
{
    char tmp[10];
    int n = 0;
    do {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n)
        *p++ = tmp[--n];
    return p;
}

static int is_custom1(uint32_t layout)
{
    return layout == 0 || layout == ENC_LAYOUT_CUSTOM1;     /* the stock code treats 0 like 6 */
}

static int active(void)
{
    return g_enc_layout == ENC_LAYOUT_CUSTOM1;
}

static uint32_t page(void)
{
    uint32_t p = g_enc_page[0];
    return p < CCP_PAGES ? p : CCP_PAGES - 1;
}

static int shift_down(void)
{
    fw_param_t p;
    confsys_get_param(FW_GROUP_FEATURE, FEAT_SHIFT, &p);
    return p.value != 0;
}

/* The encoder is ours right now (not Shift + encoder, not captured by a menu). */
static int takes(uint32_t id)
{
    return active() && !shift_down() && !ctl_is_blocked(id);
}

/* ---- hooks into the stock page handling (cfw/hooks.txt) ---- */

/* Replaces enc_page_count (0x0801CBA6): pages of an encoder layout. Stock: 2 for the custom
 * layouts (0, 6..9), 1 for all others. The up/down handler and the button LEDs use it. */
uint32_t cfw_enc_page_count(uint32_t layout)
{
    if (is_custom1(layout))
        return CCP_PAGES;
    return layout >= 7 && layout <= 9 ? 2 : 1;
}

/* Replaces the call to enc_set_page in the up/down handler (0x080235D2). The stock function
 * limits the page to 1, so Custom 1 is stored here; the rest is what it does itself. */
void cfw_enc_set_page(uint32_t layout, uint32_t new_page)
{
    if (!is_custom1(layout)) {
        enc_set_page(layout, new_page);
        return;
    }
    if (new_page >= CCP_PAGES)
        new_page = CCP_PAGES - 1;
    if (g_enc_page[0] != new_page) {
        g_enc_page[0] = new_page;
        enc_assign_refresh();
    }
}

/* Replaces the call to ui_show_enc_page in the up/down handler (0x080235D8). The stock popup
 * only has texts for two pages ("1 - 8", "9 - 16"). */
void cfw_enc_page_popup(uint32_t new_page)
{
    if (!active()) {
        ui_show_enc_page(new_page);
        return;
    }
    if (new_page >= CCP_PAGES)
        new_page = CCP_PAGES - 1;
    char title[20], text[20], *p;
    uint32_t cc = CCP_FIRST_CC + new_page * ENC_COUNT;
    p = put_num(put_str(put_num(put_str(title, "CC Page "), new_page + 1), "/"), CCP_PAGES);
    *p = 0;
    p = put_num(put_str(put_num(put_str(text, "CC "), cc), " - "), cc + ENC_COUNT - 1);
    *p = 0;
    disp_set_screen(1, 0);              /* same sequence as the stock page popup */
    disp_text(1, 0, title);
    disp_text(1, 1, text);
    ui_refresh_nav_leds();
}

/* ---- input (called from the dispatchers in cfw_init.c) ---- */

static void show_value(uint32_t pg, uint32_t enc)
{
    char title[20], text[8], *p;
    p = put_num(put_str(put_num(put_str(title, "P"), pg + 1), "  CC "), CCP_FIRST_CC + pg * ENC_COUNT + enc);
    *p = 0;
    p = put_num(text, value[pg][enc]);
    *p = 0;
    ui_popup_begin();
    disp_set_screen(1, 0);
    disp_text(1, 0, title);
    disp_text(1, 1, text);
}

/* list: 0 = press (A), 1 = B, 2 = release (C), 3 = D. Returns TM_PASS or TM_ENC_ONLY: an
 * encoder we own is hidden from the stock listeners from its press to its release, only the
 * encoder module still gets the raw events (it produces the turn events). */
int ccp_io_event(int list, uint32_t id)
{
    if (id < ENC_FIRST || id >= ENC_FIRST + ENC_COUNT)
        return TM_PASS;
    uint8_t bit = (uint8_t)(1u << (id - ENC_FIRST));
    if (list == 0)
        owned = takes(id) ? owned | bit : owned & (uint8_t)~bit;
    if (!(owned & bit))
        return TM_PASS;
    if (list == 2)
        owned &= (uint8_t)~bit;
    return TM_ENC_ONLY;
}

/* list: 0 = pos (every tick), 1 = hires, 2 = step (per detent). Returns 1 if consumed. */
int ccp_enc_event(int list, uint32_t id, int32_t delta)
{
    delta = (int16_t)delta;     /* the encoder module passes deltas as u16 (-1 = 0xFFFF) */
    if (id < ENC_FIRST || id >= ENC_FIRST + ENC_COUNT || !takes(id))
        return 0;
    if (list != 0)
        return 1;
    uint32_t pg = page(), enc = id - ENC_FIRST;
    int v = value[pg][enc] + delta * CCP_STEP;
    v = v < 0 ? 0 : v > 127 ? 127 : v;
    if (v != value[pg][enc]) {
        value[pg][enc] = (uint8_t)v;
        if (g_app_ports[MIDI_PORT])
            midi_send3(g_app_ports[MIDI_PORT], 0xB0 | CCP_CHANNEL,
                       CCP_FIRST_CC + pg * ENC_COUNT + enc, (uint32_t)v);
    }
    show_value(pg, enc);
    return 1;
}
