/* Host tests for cfw/drum_pads.c. */
#include <stdint.h>
#include <stdio.h>
#include "../fw_api.h"
#include "../drum_pads.h"

static int failures, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

uint32_t g_ctl_param[256];
static uint32_t pad_mode;

/* LED override and MIDI out stubs */
static uint32_t led_rgb[16], led_mode[16], led_calls;
static uint32_t sent[3], sent_count;
midi_port_t *g_app_ports[2];
void led_override_color(uint32_t id, uint32_t rgb) { CHECK(id < 16); led_rgb[id & 15] = rgb; }
void led_override(uint32_t id, uint32_t mode) { CHECK(id < 16); led_mode[id & 15] = mode; led_calls++; }
void midi_send3(midi_port_t *port, uint32_t status, uint32_t d1, uint32_t d2)
{
    CHECK(port == g_app_ports[1]);
    sent[0] = status; sent[1] = d1; sent[2] = d2; sent_count++;
}
static void leds_reset(void)
{
    for (int i = 0; i < 16; i++) { led_rgb[i] = 0xDEAD; led_mode[i] = 99; }
    led_calls = 0;
}

void confsys_get_param(uint32_t group, uint32_t id, fw_param_t *out)
{
    CHECK(group == FW_GROUP_FEATURE && id == 0x1D);
    out->value = pad_mode;
    out->flags = 0;
}

/* a stock drum layout: notes on channel 10 (group 9), bottom-row-first 4x2 blocks */
static void stock_drum(void)
{
    static const uint8_t stock[16] = {40, 41, 42, 43, 48, 49, 50, 51, 36, 37, 38, 39, 44, 45, 46, 47};
    for (int i = 0; i < 256; i++) g_ctl_param[i] = 0;
    for (int i = 0; i < 16; i++) g_ctl_param[i] = 0x20000000u | (9u << 16) | stock[i];
    g_ctl_param[16] = 0x20000000u | (9u << 16) | 99;          /* not a pad: untouched */
}

int main(void)
{
    /* drum mode: rows */
    stock_drum();
    pad_mode = 1;
    drum_pads_poll();
    for (int i = 0; i < 16; i++) {
        CHECK((g_ctl_param[i] & 0xFFFF) == (uint32_t)(36 + i));
        CHECK((g_ctl_param[i] >> 16) == ((0x20000000u | (9u << 16)) >> 16));   /* type + channel kept */
    }
    CHECK((g_ctl_param[16] & 0xFFFF) == 99);
    CHECK(drum_pads_apply(1, g_ctl_param) == 0);              /* already done: no writes */

    /* other pad modes: untouched */
    stock_drum();
    for (uint32_t m = 0; m < 10; m++) {
        if (m == 1) continue;
        pad_mode = m;
        drum_pads_poll();
        CHECK((g_ctl_param[0] & 0xFFFF) == 40);
    }

    /* not notes (e.g. CC group 0x1x) or not a param entry: untouched */
    stock_drum();
    g_ctl_param[3] = 0x20000000u | (0x19u << 16) | 20;
    g_ctl_param[4] = 0x40000000u | (9u << 16) | 48;
    drum_pads_apply(1, g_ctl_param);
    CHECK((g_ctl_param[3] & 0xFFFF) == 20);
    CHECK((g_ctl_param[4] & 0xFFFF) == 48);
    CHECK((g_ctl_param[5] & 0xFFFF) == 41);

    /* palette */
    CHECK(drum_pads_color(0) == 0x000000);
    CHECK(drum_pads_color(1) == 0xFFFFFF);
    CHECK(drum_pads_color(7) == 0x0040FF);
    CHECK(drum_pads_color(17) == 0x0F0F0F);           /* dim white */
    CHECK(drum_pads_color(16 + 2) == 0x0F0000);       /* dim red */

    /* pad LEDs: leave drum mode first so the next poll is an entry */
    static int port;
    g_app_ports[1] = (midi_port_t *)&port;
    stock_drum();
    pad_mode = 0;
    drum_pads_poll();
    leds_reset();
    CHECK(drum_pads_midi_rx(0x99, 36, 7) == 0);        /* not in drum mode: passed on */
    CHECK(led_calls == 0);

    sent_count = 0;
    pad_mode = 1;
    drum_pads_poll();                                   /* entry: refresh request */
    CHECK(sent_count == 1 && sent[0] == 0xB9 && sent[1] == 3 && sent[2] == 127);
    drum_pads_poll();
    CHECK(sent_count == 1);                             /* only once */

    CHECK(drum_pads_midi_rx(0x99, 36, 7) == 1);        /* pad 0 blue static */
    CHECK(led_rgb[0] == 0x0040FF && led_mode[0] == 0);
    CHECK(drum_pads_midi_rx(0x99, 51, 32 + 3) == 1);   /* pad 15 orange flash */
    CHECK(led_rgb[15] == 0xFF4000 && led_mode[15] == 1);
    CHECK(drum_pads_midi_rx(0x99, 44, 64 + 16 + 5) == 1); /* pad 8 dim green pulse */
    CHECK(led_rgb[8] == 0x000F00 && led_mode[8] == 2);
    CHECK(drum_pads_midi_rx(0x99, 45, 96 + 2) == 1);   /* mode 3 = static */
    CHECK(led_mode[9] == 0);
    CHECK(drum_pads_midi_rx(0x99, 37, 0) == 1);        /* velocity 0 on a free pad: no call */
    CHECK(led_mode[1] == 99);

    /* not for the pads: passed on, nothing drawn */
    leds_reset();
    CHECK(drum_pads_midi_rx(0x98, 36, 7) == 0);        /* other channel */
    CHECK(drum_pads_midi_rx(0x99, 35, 7) == 0);        /* below the pads */
    CHECK(drum_pads_midi_rx(0x99, 52, 7) == 0);        /* above */
    CHECK(drum_pads_midi_rx(0xB9, 36, 7) == 0);        /* CC */
    CHECK(led_calls == 0);

    /* release */
    CHECK(drum_pads_midi_rx(0x99, 36, 0) == 1);
    CHECK(led_mode[0] == 3);
    CHECK(drum_pads_midi_rx(0x89, 51, 64) == 1);       /* note off */
    CHECK(led_mode[15] == 3);

    /* leaving drum mode releases the rest (pads 8 and 9), only those */
    leds_reset();
    pad_mode = 5;
    drum_pads_poll();
    CHECK(led_calls == 2 && led_mode[8] == 3 && led_mode[9] == 3);
    leds_reset();
    drum_pads_poll();
    CHECK(led_calls == 0);
    CHECK(drum_pads_midi_rx(0x99, 36, 7) == 0);

    printf("test_drum_pads: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
