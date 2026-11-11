/* Host tests for cfw/seq_mode.c: the stock firmware functions it calls are mocked here. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../fw_api.h"
#include "../tm_mode.h"
#include "../seq_core.h"
#include "../seq_mode.h"

static int failures, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* ---- mocked stock firmware ---- */

static int port_obj;
midi_port_t *g_app_ports[2] = {0, (midi_port_t *)&port_obj};

static uint32_t feat[128];
static uint16_t blocked;                    /* pads captured by a menu */
static uint32_t led_rgb[16];
static uint8_t led_mode[16];
static int n_popup;
static char field[2][32];

#define MAX_SENT 64
static uint32_t sent[MAX_SENT][3];
static int n_sent;

void confsys_get_param(uint32_t group, uint32_t id, fw_param_t *out)
{
    CHECK(group == FW_GROUP_FEATURE && id < 128);
    out->value = feat[id & 127];
    out->flags = 0;
}
uint32_t ctl_is_blocked(uint32_t id) { return id < 16 && (blocked >> id & 1); }
void led_override_color(uint32_t id, uint32_t rgb) { CHECK(id < 16); led_rgb[id & 15] = rgb; }
void led_override(uint32_t id, uint32_t mode) { CHECK(id < 16); led_mode[id & 15] = (uint8_t)mode; }
void ui_popup_begin(void) { n_popup++; }
void disp_set_screen(uint32_t screen, uint32_t template_id) { CHECK(screen == 1 && template_id == 0); }
void disp_text(uint32_t screen, uint32_t f, const char *str)
{
    CHECK(screen == 1 && f < 2 && strlen(str) <= 14);    /* longest text the Turing mode shows */
    if (f < 2)
        snprintf(field[f], sizeof field[f], "%s", str);
}
void midi_send3(midi_port_t *port, uint32_t status, uint32_t d1, uint32_t d2)
{
    CHECK(port == g_app_ports[1] && n_sent < MAX_SENT);
    if (n_sent < MAX_SENT) {
        sent[n_sent][0] = status; sent[n_sent][1] = d1; sent[n_sent][2] = d2;
        n_sent++;
    }
}

/* ---- helpers ---- */

#define PAD_MODE 0x1D
#define SHIFT    0x3F
#define KEYS_CH  0x64
#define PLAY     0x48
#define RECORD   0x4A
#define OCT_DOWN 0x20
#define OCT_UP   0x21
#define KEY(n)   (0x80 + (n))
#define CLOCK    24

#define GREEN_ON  0x00FF00u
#define GREEN_DIM 0x001800u
#define BLUE_ON   0x0000FFu
#define BLUE_DIM  0x000018u
#define AMBER_ON  0xFFA000u
#define AMBER_DIM 0x181000u
#define RED       0xFF0000u

static void set_feat(uint32_t id, uint32_t value)
{
    feat[id] = value;
    seq_feat_changed(id, value);
}

/* press + release; returns what the press did */
static int tap(uint32_t id)
{
    int r = seq_io_event(0, id);
    CHECK(seq_io_event(3, id) == r);
    CHECK(seq_io_event(2, id) == r);
    return r;
}

/* a key press as the stock firmware reports it: io press, then the note it sends */
static void key(int n, int note)
{
    CHECK(seq_io_event(0, KEY(n)) == TM_PASS);
    seq_note_out(KEY(n), feat[KEYS_CH], (uint32_t)note, 90);
    CHECK(seq_io_event(2, KEY(n)) == TM_PASS);
    seq_note_out(KEY(n), feat[KEYS_CH], (uint32_t)note, 0);
}

static void shift_tap(uint32_t id, int expect)
{
    set_feat(SHIFT, 127);
    CHECK(tap(id) == expect);
    set_feat(SHIFT, 0);
}

static int clock_on(void)  { return seq_midi_rx(0x90, CLOCK, 100); }
static int clock_off(void) { return seq_midi_rx(0x80, CLOCK, 0); }

static int sent_is(int i, uint32_t status, uint32_t d1, uint32_t d2)
{
    return i < n_sent && sent[i][0] == status && sent[i][1] == d1 && sent[i][2] == d2;
}

int main(void)
{
    seq_mode_init();

    /* ---- another pad mode: nothing is touched ---- */
    feat[PAD_MODE] = 5;
    seq_mode_poll();
    CHECK(tap(PLAY) == TM_PASS && tap(RECORD) == TM_PASS && tap(3) == TM_PASS);
    shift_tap(OCT_DOWN, TM_PASS);
    key(0, 60);
    CHECK(clock_on() == 0 && clock_off() == 0);
    CHECK(seq_midi_rx(0x90, 60, 100) == 0);
    seq_mode_poll();
    CHECK(n_sent == 0 && n_popup == 0);
    for (int i = 0; i < 16; i++)
        CHECK(led_rgb[i] == 0 && led_mode[i] == 0);

    /* ---- pad mode Custom 3 ---- */
    set_feat(PAD_MODE, 7);
    seq_mode_poll();
    CHECK(n_popup == 1 && !strcmp(field[0], "SH-101 Seq") && !strcmp(field[1], "0 steps"));
    for (int i = 0; i < 16; i++)
        CHECK(led_rgb[i] == 0 && led_mode[i] == LED_STATIC);
    CHECK(tap(0) == TM_CONSUME && tap(15) == TM_CONSUME);        /* pads: owned, no function */
    key(0, 60);                                                   /* not recording: ignored */
    shift_tap(OCT_DOWN, TM_PASS);                                 /* transpose as usual */
    shift_tap(OCT_UP, TM_PASS);
    CHECK(tap(OCT_DOWN) == TM_PASS && tap(OCT_UP) == TM_PASS);    /* octave as usual */
    CHECK(tap(PLAY) == TM_CONSUME);
    seq_mode_poll();
    CHECK(!strcmp(field[1], "Empty"));
    CHECK(clock_on() == 1 && clock_off() == 1 && n_sent == 0);    /* clock note is swallowed */
    CHECK(seq_midi_rx(0x90, 25, 100) == 0 && seq_midi_rx(0xB0, CLOCK, 1) == 0);

    /* ---- recording ---- */
    CHECK(tap(RECORD) == TM_CONSUME);
    seq_mode_poll();
    CHECK(!strcmp(field[0], "Record") && !strcmp(field[1], "Step 1"));
    CHECK(led_rgb[0] == RED && led_rgb[1] == 0);
    key(0, 48);
    seq_mode_poll();
    CHECK(!strcmp(field[1], "1: Note 48") && led_rgb[0] == GREEN_DIM && led_rgb[1] == RED);
    CHECK(tap(OCT_DOWN) == TM_PASS && tap(OCT_UP) == TM_PASS);    /* without Shift: octave */
    shift_tap(OCT_DOWN, TM_CONSUME);                              /* rest */
    seq_mode_poll();
    CHECK(!strcmp(field[1], "2: Rest") && led_rgb[1] == BLUE_DIM && led_rgb[2] == RED);
    key(4, 55);
    shift_tap(OCT_UP, TM_CONSUME);                                /* tie */
    seq_mode_poll();
    CHECK(!strcmp(field[1], "4: Tie") && led_rgb[2] == GREEN_DIM && led_rgb[3] == AMBER_DIM && led_rgb[4] == RED);

    /* a chord (several notes from one key press) is one step: its first note */
    CHECK(seq_io_event(0, KEY(7)) == TM_PASS);
    seq_note_out(KEY(7), 0, 60, 80);
    seq_note_out(KEY(7), 0, 64, 80);
    seq_note_out(KEY(7), 0, 67, 80);
    seq_note_out(KEY(7), 0x10, 7, 80);                            /* a CC from a control */
    seq_mode_poll();
    CHECK(!strcmp(field[1], "5: Note 60") && led_rgb[4] == GREEN_DIM && led_rgb[5] == RED && led_rgb[6] == 0);

    CHECK(clock_on() == 1 && n_sent == 0);                        /* no playback while recording */
    CHECK(tap(RECORD) == TM_CONSUME);                             /* second press: done */
    seq_mode_poll();
    CHECK(!strcmp(field[0], "SH-101 Seq") && !strcmp(field[1], "5 steps"));
    CHECK(led_rgb[0] == GREEN_DIM && led_rgb[1] == BLUE_DIM && led_rgb[3] == AMBER_DIM
          && led_rgb[4] == GREEN_DIM && led_rgb[5] == 0);
    key(0, 50);                                                   /* not recording any more */
    shift_tap(OCT_DOWN, TM_PASS);
    seq_mode_poll();
    CHECK(!strcmp(field[1], "5 steps") && led_rgb[5] == 0);
    CHECK(clock_on() == 1 && n_sent == 0);                        /* no playback before Play */

    /* ---- playback: 48, rest, 55, tie, 60 ---- */
    feat[KEYS_CH] = 2;
    CHECK(tap(PLAY) == TM_CONSUME);
    seq_mode_poll();
    CHECK(!strcmp(field[1], "Play") && n_sent == 0);
    clock_on();
    CHECK(n_sent == 1 && sent_is(0, 0x92, 48, 100));
    seq_mode_poll();
    CHECK(led_rgb[0] == GREEN_ON && led_rgb[1] == BLUE_DIM);
    clock_off();
    CHECK(n_sent == 2 && sent_is(1, 0x82, 48, 0));
    clock_on();                                                   /* rest */
    seq_mode_poll();
    CHECK(n_sent == 2 && led_rgb[0] == GREEN_DIM && led_rgb[1] == BLUE_ON);
    clock_off();
    clock_on();
    CHECK(n_sent == 3 && sent_is(2, 0x92, 55, 100));
    feat[KEYS_CH] = 5;                                            /* note off stays on its channel */
    clock_off();
    CHECK(n_sent == 3);                                           /* tie follows: held */
    clock_on();
    seq_mode_poll();
    CHECK(n_sent == 3 && led_rgb[3] == AMBER_ON);
    clock_off();
    CHECK(n_sent == 4 && sent_is(3, 0x82, 55, 0));
    clock_on();
    CHECK(n_sent == 5 && sent_is(4, 0x95, 60, 100));
    CHECK(seq_midi_rx(0x93, CLOCK, 0) == 1);                      /* note on, velocity 0 = off */
    CHECK(n_sent == 6 && sent_is(5, 0x85, 60, 0));
    clock_on();                                                   /* loops to step 1 */
    CHECK(n_sent == 7 && sent_is(6, 0x95, 48, 100));

    /* Shift held: pads are left to the Shift overlay, Shift + Play is stock */
    for (int i = 0; i < 16; i++)
        led_rgb[i] = 0x123456;
    set_feat(SHIFT, 127);
    clock_off();
    clock_on();
    seq_mode_poll();
    CHECK(led_rgb[0] == 0x123456 && led_rgb[1] == 0x123456);
    CHECK(tap(PLAY) == TM_PASS && tap(RECORD) == TM_PASS && tap(2) == TM_PASS);
    set_feat(SHIFT, 0);
    seq_mode_poll();
    CHECK(led_rgb[0] == GREEN_DIM && led_rgb[1] == BLUE_ON);

    /* a menu captures pads: they are not drawn, and redrawn when it lets go */
    CHECK(tap(0x1A) == TM_PASS);                                  /* some other button: menus win */
    blocked = 0x0003;
    seq_mode_poll();
    led_rgb[0] = led_rgb[1] = 0x654321;
    clock_off();
    clock_on();
    seq_mode_poll();
    CHECK(led_rgb[0] == 0x654321 && led_rgb[1] == 0x654321 && led_rgb[2] == GREEN_ON);
    CHECK(tap(0) == TM_PASS && tap(2) == TM_CONSUME);
    blocked = 0;
    seq_mode_poll();
    CHECK(led_rgb[0] == GREEN_DIM && led_rgb[1] == BLUE_DIM && led_rgb[2] == GREEN_ON);

    /* Play again stops and releases the note */
    n_sent = 0;
    CHECK(tap(PLAY) == TM_CONSUME);
    seq_mode_poll();
    CHECK(!strcmp(field[1], "Stop") && n_sent == 1 && sent_is(0, 0x85, 55, 0));
    CHECK(led_rgb[2] == GREEN_DIM);
    clock_on();
    CHECK(n_sent == 1);

    /* leaving the pad mode stops playback and frees the pads; the sequence is kept */
    tap(PLAY);
    clock_on();
    CHECK(n_sent == 2 && sent_is(1, 0x95, 48, 100));
    set_feat(PAD_MODE, 8);
    CHECK(n_sent == 3 && sent_is(2, 0x85, 48, 0));
    for (int i = 0; i < 16; i++)
        CHECK(led_mode[i] == LED_RELEASE);
    CHECK(clock_on() == 0 && tap(PLAY) == TM_PASS && tap(0) == TM_PASS);
    set_feat(PAD_MODE, 7);
    seq_mode_poll();
    CHECK(!strcmp(field[1], "5 steps") && led_rgb[0] == GREEN_DIM && led_mode[0] == LED_STATIC);

    /* Record while playing: stops the note, clears the sequence */
    tap(PLAY);
    clock_on();
    n_sent = 0;
    tap(RECORD);
    CHECK(n_sent == 1 && sent_is(0, 0x85, 48, 0));
    seq_mode_poll();
    CHECK(led_rgb[0] == RED && led_rgb[1] == 0);

    /* pages of 16 steps; Play ends the recording */
    for (int i = 0; i < 20; i++)
        key(i % 25, 40 + i);
    seq_mode_poll();
    CHECK(!strcmp(field[1], "20: Note 59"));
    CHECK(led_rgb[0] == GREEN_DIM && led_rgb[3] == GREEN_DIM && led_rgb[4] == RED && led_rgb[5] == 0);
    n_sent = 0;
    CHECK(tap(PLAY) == TM_CONSUME);
    seq_mode_poll();
    CHECK(!strcmp(field[1], "Play"));
    for (int i = 0; i < 16; i++)
        CHECK(led_rgb[i] == (i == 0 ? GREEN_ON : GREEN_DIM));
    for (int i = 0; i < 20; i++) {
        clock_on();
        clock_off();
    }
    seq_mode_poll();
    CHECK(n_sent == 40 && sent_is(38, 0x95, 59, 100) && sent_is(39, 0x85, 59, 0));
    CHECK(led_rgb[3] == GREEN_ON && led_rgb[0] == GREEN_DIM && led_rgb[4] == 0);
    tap(PLAY);

    /* the memory holds SEQ_MAX_STEPS steps; recording ends by itself when it is full */
    tap(RECORD);
    for (int i = 0; i < SEQ_MAX_STEPS + 5; i++)
        key(i % 25, 30 + i % 60);
    seq_mode_poll();
    CHECK(!strcmp(field[0], "SH-101 Seq") && !strcmp(field[1], "100 steps"));
    CHECK(tap(RECORD) == TM_CONSUME);                             /* starts a new recording */
    seq_mode_poll();
    CHECK(!strcmp(field[1], "Step 1") && led_rgb[0] == RED);

    printf("seq_mode: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
