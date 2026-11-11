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

static uint32_t shift, blocked_id = 0xFF;
static int n_refresh, n_nav_leds, n_popup_begin, n_stock_popup, n_stock_set, n_sent;
static uint32_t stock_set_layout, stock_set_page, stock_popup_page;
static uint32_t sent_status, sent_d1, sent_d2;
static midi_port_t *sent_port;
static char field[2][32];

void confsys_get_param(uint32_t group, uint32_t id, fw_param_t *out)
{
    CHECK(group == FW_GROUP_FEATURE && id == 0x3F);
    out->value = shift;
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
}

#define ENC(n) (56 + (n) - 1)       /* control id of encoder n (1..8) */

int main(void)
{
    /* pages per encoder layout */
    CHECK(cfw_enc_page_count(0) == 12 && cfw_enc_page_count(6) == 12);
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
    CHECK(g_enc_page[0] == 11 && n_refresh == 2);
    CHECK(g_enc_page[1] == 0 && g_enc_page[2] == 0 && g_enc_page[3] == 0);

    /* page popup */
    g_enc_layout = 7;
    cfw_enc_page_popup(1);
    CHECK(n_stock_popup == 1 && stock_popup_page == 1 && n_nav_leds == 0);
    g_enc_layout = 6;
    cfw_enc_page_popup(2);
    CHECK(n_stock_popup == 1 && n_nav_leds == 1);
    CHECK(!strcmp(field[0], "CC Page 3/12") && !strcmp(field[1], "CC 36 - 43"));
    cfw_enc_page_popup(0);
    CHECK(!strcmp(field[0], "CC Page 1/12") && !strcmp(field[1], "CC 20 - 27"));
    cfw_enc_page_popup(11);
    CHECK(!strcmp(field[0], "CC Page 12/12") && !strcmp(field[1], "CC 108 - 115"));

    /* encoders: only in Custom 1 */
    g_enc_page[0] = 0;
    g_enc_layout = 2;
    CHECK(ccp_enc_event(0, ENC(1), 1) == 0 && n_sent == 0);
    g_enc_layout = 7;
    CHECK(ccp_enc_event(0, ENC(1), 1) == 0 && n_sent == 0);
    g_enc_layout = 6;
    CHECK(ccp_enc_event(0, 55, 1) == 0 && ccp_enc_event(0, 64, 1) == 0 && n_sent == 0);

    CHECK(ccp_enc_event(0, ENC(1), 3) == 1);
    CHECK(n_sent == 1 && sent_port == g_app_ports[1] && sent_status == 0xB0 && sent_d1 == 20 && sent_d2 == 3);
    CHECK(!strcmp(field[0], "P1  CC 20") && !strcmp(field[1], "3") && n_popup_begin == 1);
    CHECK(ccp_enc_event(0, ENC(1), 0xFFFF) == 1);       /* -1 arrives as u16 */
    CHECK(n_sent == 2 && sent_d1 == 20 && sent_d2 == 2);
    CHECK(ccp_enc_event(0, ENC(1), -50) == 1);
    CHECK(n_sent == 3 && sent_d2 == 0);
    CHECK(ccp_enc_event(0, ENC(1), -1) == 1 && n_sent == 3);    /* at the limit: nothing sent */
    CHECK(ccp_enc_event(0, ENC(8), 200) == 1);
    CHECK(n_sent == 4 && sent_d1 == 27 && sent_d2 == 127);
    CHECK(ccp_enc_event(0, ENC(8), 1) == 1 && n_sent == 4);

    /* the other turn lists are swallowed without sending */
    CHECK(ccp_enc_event(1, ENC(2), 1) == 1 && ccp_enc_event(2, ENC(2), 1) == 1 && n_sent == 4);

    /* Shift + encoder and encoders captured by a menu stay with the stock firmware */
    shift = 127;
    CHECK(ccp_enc_event(0, ENC(2), 1) == 0 && n_sent == 4);
    shift = 0;
    blocked_id = ENC(2);
    CHECK(ccp_enc_event(0, ENC(2), 1) == 0 && n_sent == 4);
    CHECK(ccp_enc_event(0, ENC(3), 1) == 1 && n_sent == 5 && sent_d1 == 22);
    blocked_id = 0xFF;

    /* every page has its own CCs and remembers its values */
    cfw_enc_set_page(6, 11);
    CHECK(ccp_enc_event(0, ENC(8), 64) == 1);
    CHECK(sent_d1 == 115 && sent_d2 == 64 && !strcmp(field[0], "P12  CC 115") && !strcmp(field[1], "64"));
    cfw_enc_set_page(6, 0);
    CHECK(ccp_enc_event(0, ENC(8), -27) == 1);
    CHECK(sent_d1 == 27 && sent_d2 == 100);
    cfw_enc_set_page(6, 11);
    CHECK(ccp_enc_event(0, ENC(8), 1) == 1);
    CHECK(sent_d1 == 115 && sent_d2 == 65);

    uint8_t seen[128] = {0};
    for (uint32_t pg = 0; pg < 12; pg++) {
        cfw_enc_set_page(6, pg);
        for (int e = 1; e <= 8; e++) {
            n_sent = 0;
            ccp_enc_event(0, ENC(e), pg == 11 && e == 8 ? -1 : 1);
            CHECK(n_sent == 1 && sent_d1 == 20 + pg * 8 + (uint32_t)(e - 1) && sent_d1 < 128 && !seen[sent_d1 & 127]);
            seen[sent_d1 & 127] = 1;
        }
    }
    for (int cc = 0; cc < 128; cc++)
        CHECK(seen[cc] == (cc >= 20 && cc <= 115));

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
