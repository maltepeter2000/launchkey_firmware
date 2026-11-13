/* CC pages: encoder mode Custom 1 gets the CC pages of the Pd controller layer (pdoly8 tools/ctlayer.py)
 * instead of the stock 2 pages.
 *
 * Active while the encoder layout is Custom 1 (g_enc_layout = 6), Shift is up and no menu has
 * captured the encoder:
 *   encoders 1..8      the CCs of the page (page_cc), 0..127, on the Keys Channel (feature 0x64):
 *                      one channel per instrument, so changing the keys channel switches the
 *                      instrument for keys and encoders together. 20..119 without 32, 38, 64..69,
 *                      84, 88, 96..101 (bank / data LSB, pedals, portamento control, high-res
 *                      velocity, NRPN / RPN): 10 pages of 8 and page 11 with 4 (encoders 5..8 off).
 *                      Every page remembers its eight values (RAM only).
 *   feedback           a CC on the Keys Channel coming in on the MIDI port (from Pd: presets,
 *                      editor) sets the value of its encoder, so the next turn goes on from there.
 *   sync request       when the channel differs from the one used last (power-on, keys channel
 *                      changed) the first turn sends CC 3 = 127 first: the Pd layer answers with all
 *                      its values as CC, which land here as feedback.
 *   up / down buttons  previous / next page, handled by the stock firmware as before. Three
 *                      hooks (cfw/hooks.txt) widen it: cfw_enc_page_count reports CCP_PAGES
 *                      pages for Custom 1 (button LEDs and limits follow), cfw_enc_set_page
 *                      stores pages above 1, cfw_enc_page_popup shows the page and its CCs.
 * The CCs go out on the MIDI port (USB + DIN/TRS), like the stock custom modes. The stock
 * assignments of Custom 1 (Components) are not used while this is active; Custom 2..4 and
 * all other encoder modes are untouched.
 * The input dispatchers in cfw_init.c ask ccp_io_event / ccp_enc_event after the Turing
 * Machine mode has passed on an event, so in pad mode Custom 4 only encoder 8 lands here;
 * cfw_midi_rx asks ccp_midi_rx first (it only reads, the message goes on to the stock code).
 */
#include <stdint.h>
#include "fw_api.h"
#include "tm_mode.h"
#include "cc_pages.h"

#define CCP_PAGES         11
#define CCP_STEP          1           /* value change per encoder tick */
#define CCP_SYNC_CC       3           /* undefined in MIDI 1.0: "send me your values" for the Pd layer */

#define ENC_FIRST         56          /* encoder 1 */
#define ENC_COUNT         8
#define ENC_LAYOUT_CUSTOM1 6
#define FEAT_SHIFT        0x3F
#define FEAT_KEYS_CHANNEL 0x64        /* 0-based */
#define MIDI_PORT         1           /* g_app_ports[1] = USB MIDI port (+ DIN via link) */

/* The pages of the Pd controller layer; 0 = no CC at this place. */
static const uint8_t page_cc[CCP_PAGES][ENC_COUNT] = {
    { 20,  21,  22,  23,  24,  25,  26,  27},
    { 28,  29,  30,  31,  33,  34,  35,  36},
    { 37,  39,  40,  41,  42,  43,  44,  45},
    { 46,  47,  48,  49,  50,  51,  52,  53},
    { 54,  55,  56,  57,  58,  59,  60,  61},
    { 62,  63,  70,  71,  72,  73,  74,  75},
    { 76,  77,  78,  79,  80,  81,  82,  83},
    { 85,  86,  87,  89,  90,  91,  92,  93},
    { 94,  95, 102, 103, 104, 105, 106, 107},
    {108, 109, 110, 111, 112, 113, 114, 115},
    {116, 117, 118, 119,   0,   0,   0,   0},
};

static uint8_t value[CCP_PAGES][ENC_COUNT];
static uint8_t owned;                 /* encoders whose current touch belongs to this mode */
static uint8_t last_ch;               /* channel + 1 of the last CC sent, 0 = none yet */

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
    const uint8_t *cc = page_cc[new_page];
    int last = ENC_COUNT - 1;
    while (last > 0 && !cc[last])
        last--;
    p = put_num(put_str(put_num(put_str(title, "CC Page "), new_page + 1), "/"), CCP_PAGES);
    *p = 0;
    p = put_num(put_str(put_num(put_str(text, "CC "), cc[0]), " - "), cc[last]);
    *p = 0;
    disp_set_screen(1, 0);              /* same sequence as the stock page popup */
    disp_text(1, 0, title);
    disp_text(1, 1, text);
    ui_refresh_nav_leds();
}

/* ---- input (called from the dispatchers in cfw_init.c) ---- */

static uint32_t channel(void)
{
    fw_param_t p;
    confsys_get_param(FW_GROUP_FEATURE, FEAT_KEYS_CHANNEL, &p);
    return p.value & 15;
}

static void show_value(uint32_t pg, uint32_t enc, uint32_t ch)
{
    char title[20], text[8], *p;
    p = put_num(put_str(put_num(put_str(title, "P"), pg + 1), " Ch"), ch + 1);
    if (page_cc[pg][enc]) {
        p = put_num(put_str(p, " CC"), page_cc[pg][enc]);
        *p = 0;
        p = put_num(text, value[pg][enc]);
    } else {
        *p = 0;
        p = put_str(text, "-");
    }
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
    uint32_t pg = page(), enc = id - ENC_FIRST, ch = channel();
    uint32_t cc = page_cc[pg][enc];
    int v = value[pg][enc] + delta * CCP_STEP;
    v = v < 0 ? 0 : v > 127 ? 127 : v;
    midi_port_t *port = g_app_ports[MIDI_PORT];
    if (cc && v != value[pg][enc]) {
        value[pg][enc] = (uint8_t)v;
        if (port) {
            if (last_ch != ch + 1) {                /* new channel: ask the Pd layer for its values */
                midi_send3(port, 0xB0 | ch, CCP_SYNC_CC, 127);
                last_ch = (uint8_t)(ch + 1);
            }
            midi_send3(port, 0xB0 | ch, cc, (uint32_t)v);
        }
    }
    show_value(pg, enc, ch);
    return 1;
}

/* MIDI port input (from cfw_midi_rx, before the stock handler): a CC on the Keys Channel sets
 * the value of the encoder that sends it. Only reads: returns 0, the message goes on. */
int ccp_midi_rx(uint32_t status, uint32_t d1, uint32_t d2)
{
    if ((status & 0xF0) != 0xB0 || d1 < 20 || d1 > 119 || d2 > 127 || (status & 15) != channel())
        return 0;
    for (uint32_t pg = 0; pg < CCP_PAGES; pg++)
        for (uint32_t e = 0; e < ENC_COUNT; e++)
            if (page_cc[pg][e] == d1) {
                value[pg][e] = (uint8_t)d2;
                return 0;
            }
    return 0;
}
