/* Host tests for cfw/cc_pages.c: the stock firmware functions it calls are mocked here. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../fw_api.h"
#include "../tm_mode.h"
#include "../cc_pages.h"

static int failures, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* ---- mocked stock firmware ---- */

uint32_t g_enc_layout;
uint32_t g_enc_page[4];
static int port_obj;
midi_port_t *g_app_ports[2] = {0, (midi_port_t *)&port_obj};

static uint32_t shift, keys_ch, blocked_id = 0xFF;
static int n_refresh, n_nav_leds, n_popup_begin, n_stock_popup, n_stock_set, n_sent;
static uint32_t stock_set_layout, stock_set_page, stock_popup_page;
static uint32_t sent_status, sent_d1, sent_d2;
static uint8_t sent_log[64][3]; static int n_log;
static midi_port_t *sent_port;
static char field[2][32];

void confsys_get_param(uint32_t group, uint32_t id, fw_param_t *out)
{
    CHECK(group == FW_GROUP_FEATURE && (id == 0x3F || id == 0x64));
    out->value = id == 0x3F ? shift : keys_ch;
    out->flags = 0;
}
uint32_t ctl_is_blocked(uint32_t id) { return id == blocked_id; }
void enc_set_page(uint32_t layout, uint32_t page) { n_stock_set++; stock_set_layout = layout; stock_set_page = page; }
void enc_assign_refresh(void) { n_refresh++; }
void ui_show_enc_page(uint32_t page) { n_stock_popup++; stock_popup_page = page; }
void ui_refresh_nav_leds(void) { n_nav_leds++; }
void ui_popup_begin(void) { n_popup_begin++; }
void disp_set_screen(uint32_t screen, uint32_t template_id) { CHECK(screen == 1 && template_id == 0); }
void disp_text(uint32_t screen, uint32_t f, const char *str)
{
    CHECK(screen == 1 && f < 2 && strlen(str) <= 14);    /* longest text the Turing mode shows */
    if (f < 2)
        snprintf(field[f], sizeof field[f], "%s", str);
}
void midi_send3(midi_port_t *port, uint32_t status, uint32_t d1, uint32_t d2)
{
    n_sent++;
    sent_port = port; sent_status = status; sent_d1 = d1; sent_d2 = d2;
    if (n_log < 64) { sent_log[n_log][0] = (uint8_t)status; sent_log[n_log][1] = (uint8_t)d1; sent_log[n_log][2] = (uint8_t)d2; n_log++; }
}

#define ENC(n) (56 + (n) - 1)       /* control id of encoder n (1..8) */

/* the CC pages of the Pd controller layer: 20..119 without the reserved numbers */
static int reserved(int cc)
{
    return cc == 32 || cc == 38 || (cc >= 64 && cc <= 69) || cc == 84 || cc == 88 || (cc >= 96 && cc <= 101);
}
static int expect_cc(uint32_t pg, int e)        /* e = 0..7, 0 = none */
{
    int k = 0;
    for (int cc = 20; cc < 120; cc++) {
        if (reserved(cc))
            continue;
        if (k == (int)pg * 8 + e)
            return cc;
        k++;
    }
    return 0;
}

int main(void)
{
    /* pages per encoder layout */
    CHECK(cfw_enc_page_count(0) == 11 && cfw_enc_page_count(6) == 11);
    CHECK(cfw_enc_page_count(7) == 2 && cfw_enc_page_count(8) == 2 && cfw_enc_page_count(9) == 2);
    for (uint32_t l = 1; l <= 5; l++)
        CHECK(cfw_enc_page_count(l) == 1);
    CHECK(cfw_enc_page_count(10) == 1 && cfw_enc_page_count(17) == 1);

    /* storing the page: Custom 2..4 go to the stock function, Custom 1 is stored here */
    cfw_enc_set_page(7, 1);
    CHECK(n_stock_set == 1 && stock_set_layout == 7 && stock_set_page == 1 && n_refresh == 0);
    cfw_enc_set_page(6, 5);
    CHECK(g_enc_page[0] == 5 && n_refresh == 1 && n_stock_set == 1);
    cfw_enc_set_page(6, 5);
    CHECK(n_refresh == 1);                      /* unchanged: no re-assignment */
    cfw_enc_set_page(0, 14);
    CHECK(g_enc_page[0] == 10 && n_refresh == 2);
    CHECK(g_enc_page[1] == 0 && g_enc_page[2] == 0 && g_enc_page[3] == 0);

    /* page popup */
    g_enc_layout = 7;
    cfw_enc_page_popup(1);
    CHECK(n_stock_popup == 1 && stock_popup_page == 1 && n_nav_leds == 0);
    g_enc_layout = 6;
    cfw_enc_page_popup(1);
    CHECK(n_stock_popup == 1 && n_nav_leds == 1);
    CHECK(!strcmp(field[0], "CC Page 2/11") && !strcmp(field[1], "CC 28 - 36"));
    cfw_enc_page_popup(0);
    CHECK(!strcmp(field[0], "CC Page 1/11") && !strcmp(field[1], "CC 20 - 27"));
    cfw_enc_page_popup(10);
    CHECK(!strcmp(field[0], "CC Page 11/11") && !strcmp(field[1], "CC 116 - 119"));

    /* encoders: only in Custom 1 */
    g_enc_page[0] = 0;
    g_enc_layout = 2;
    CHECK(ccp_enc_event(0, ENC(1), 1) == 0 && n_sent == 0);
    g_enc_layout = 7;
    CHECK(ccp_enc_event(0, ENC(1), 1) == 0 && n_sent == 0);
    g_enc_layout = 6;
    CHECK(ccp_enc_event(0, 55, 1) == 0 && ccp_enc_event(0, 64, 1) == 0 && n_sent == 0);

    /* first turn: sync request (CC 3 = 127) on the keys channel, then the CC */
    CHECK(ccp_enc_event(0, ENC(1), 3) == 1);
    CHECK(n_sent == 2 && n_log == 2 && sent_log[0][0] == 0xB0 && sent_log[0][1] == 3 && sent_log[0][2] == 127);
    CHECK(sent_port == g_app_ports[1] && sent_status == 0xB0 && sent_d1 == 20 && sent_d2 == 3);
    CHECK(!strcmp(field[0], "P1 Ch1 CC20") && !strcmp(field[1], "3") && n_popup_begin == 1);
    n_sent = 0;
    CHECK(ccp_enc_event(0, ENC(1), 0xFFFF) == 1);       /* -1 arrives as u16; no second sync */
    CHECK(n_sent == 1 && sent_d1 == 20 && sent_d2 == 2);
    CHECK(ccp_enc_event(0, ENC(1), -50) == 1);
    CHECK(n_sent == 2 && sent_d2 == 0);
    CHECK(ccp_enc_event(0, ENC(1), -1) == 1 && n_sent == 2);    /* at the limit: nothing sent */
    CHECK(ccp_enc_event(0, ENC(8), 200) == 1);
    CHECK(n_sent == 3 && sent_d1 == 27 && sent_d2 == 127);
    CHECK(ccp_enc_event(0, ENC(8), 1) == 1 && n_sent == 3);

    /* the other turn lists are swallowed without sending */
    CHECK(ccp_enc_event(1, ENC(2), 1) == 1 && ccp_enc_event(2, ENC(2), 1) == 1 && n_sent == 3);

    /* Shift + encoder and encoders captured by a menu stay with the stock firmware */
    shift = 127;
    CHECK(ccp_enc_event(0, ENC(2), 1) == 0 && n_sent == 3);
    shift = 0;
    blocked_id = ENC(2);
    CHECK(ccp_enc_event(0, ENC(2), 1) == 0 && n_sent == 3);
    CHECK(ccp_enc_event(0, ENC(3), 1) == 1 && n_sent == 4 && sent_d1 == 22);
    blocked_id = 0xFF;

    /* every page has its own CCs and remembers its values; page 11 has 4 */
    cfw_enc_set_page(6, 10);
    CHECK(ccp_enc_event(0, ENC(4), 64) == 1);
    CHECK(sent_d1 == 119 && sent_d2 == 64 && !strcmp(field[0], "P11 Ch1 CC119") && !strcmp(field[1], "64"));
    n_sent = 0;
    CHECK(ccp_enc_event(0, ENC(5), 5) == 1 && n_sent == 0 && !strcmp(field[0], "P11 Ch1") && !strcmp(field[1], "-"));
    cfw_enc_set_page(6, 0);
    CHECK(ccp_enc_event(0, ENC(8), -27) == 1);
    CHECK(sent_d1 == 27 && sent_d2 == 100);
    cfw_enc_set_page(6, 10);
    CHECK(ccp_enc_event(0, ENC(4), 1) == 1);
    CHECK(sent_d1 == 119 && sent_d2 == 65);

    uint8_t seen[128] = {0};
    int n_cc = 0;
    for (uint32_t pg = 0; pg < 11; pg++) {
        cfw_enc_set_page(6, pg);
        for (int e = 1; e <= 8; e++) {
            n_sent = 0;
            ccp_enc_event(0, ENC(e), 1);
            ccp_enc_event(0, ENC(e), -1);
            int want = pg == 10 && e > 4 ? 0 : expect_cc(pg, e - 1);
            if (!want) {
                CHECK(n_sent == 0);
                continue;
            }
            CHECK(n_sent >= 1 && sent_d1 == (uint32_t)want && !seen[want]);
            seen[want] = 1;
            n_cc++;
        }
    }
    CHECK(n_cc == 84);
    for (int cc = 0; cc < 128; cc++)
        CHECK(seen[cc] == (cc >= 20 && cc <= 119 && !reserved(cc)));

    /* feedback: a CC on the keys channel sets the encoder, other channels and CCs do nothing */
    cfw_enc_set_page(6, 1);
    CHECK(ccp_midi_rx(0xB0, 29, 90) == 0);              /* page 2, encoder 2 */
    CHECK(ccp_midi_rx(0xB1, 30, 90) == 0);              /* other channel */
    CHECK(ccp_midi_rx(0x90, 31, 90) == 0);              /* a note */
    CHECK(ccp_midi_rx(0xB0, 32, 90) == 0);              /* not on a page */
    n_sent = 0;
    CHECK(ccp_enc_event(0, ENC(2), 1) == 1 && n_sent == 1 && sent_d1 == 29 && sent_d2 == 91);
    CHECK(ccp_enc_event(0, ENC(3), 1) == 1 && sent_d1 == 30 && sent_d2 == 1);
    CHECK(ccp_enc_event(0, ENC(4), 1) == 1 && sent_d1 == 31 && sent_d2 == 1);

    /* keys channel changed: the next turn asks again (CC 3 on the new channel), CCs follow it */
    keys_ch = 4;
    n_log = 0; n_sent = 0;
    CHECK(ccp_midi_rx(0xB4, 33, 50) == 0);
    CHECK(ccp_enc_event(0, ENC(5), 1) == 1);
    CHECK(n_log == 2 && sent_log[0][0] == 0xB4 && sent_log[0][1] == 3 && sent_log[1][0] == 0xB4
          && sent_log[1][1] == 33 && sent_log[1][2] == 51);
    CHECK(!strcmp(field[0], "P2 Ch5 CC33"));
    keys_ch = 0;
    /* raw encoder events: owned from press to release, even if the mode changes in between */
    g_enc_layout = 6;
    CHECK(ccp_io_event(0, 3) == TM_PASS && ccp_io_event(0, 36) == TM_PASS);    /* pad, down button */
    CHECK(ccp_io_event(0, ENC(4)) == TM_ENC_ONLY);
    CHECK(ccp_io_event(3, ENC(4)) == TM_ENC_ONLY);
    CHECK(ccp_io_event(3, ENC(5)) == TM_PASS);          /* never pressed */
    g_enc_layout = 3;
    CHECK(ccp_io_event(2, ENC(4)) == TM_ENC_ONLY);      /* its release still belongs to us */
    CHECK(ccp_io_event(3, ENC(4)) == TM_PASS);
    CHECK(ccp_io_event(0, ENC(4)) == TM_PASS && ccp_io_event(2, ENC(4)) == TM_PASS);
    g_enc_layout = 6;
    shift = 127;
    CHECK(ccp_io_event(0, ENC(1)) == TM_PASS);
    shift = 0;
    CHECK(ccp_io_event(0, ENC(1)) == TM_ENC_ONLY && ccp_io_event(0, ENC(8)) == TM_ENC_ONLY);
    CHECK(ccp_io_event(2, ENC(1)) == TM_ENC_ONLY && ccp_io_event(3, ENC(1)) == TM_PASS);
    CHECK(ccp_io_event(1, ENC(8)) == TM_ENC_ONLY);

    printf("cc_pages: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
