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
volatile uint32_t g_clock_ticks;

static uint32_t feat[128];
static uint32_t blocked;                    /* controls captured by a menu (bit = id, pads and encoders) */
static uint32_t led_rgb[16];
static uint8_t led_mode[16];
static int n_popup;
static char field[2][32];

#define MAX_SENT 128
static uint32_t sent[MAX_SENT][3];
static int n_sent;

/* settings store */
static struct { uint32_t key; seq_data_t *buf; uint32_t size, delay; int dirty; } store[4];
static int n_store, n_loaded_cb;
static void (*loaded_cb)(void);

void confsys_get_param(uint32_t group, uint32_t id, fw_param_t *out)
{
    CHECK(group == FW_GROUP_FEATURE && id < 128);
    out->value = feat[id & 127];
    out->flags = 0;
}
uint32_t ctl_is_blocked(uint32_t id) { return id < 64 && id != 25 && (blocked >> (id & 31) & 1) && (id < 16 || id >= 56); }
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
void confstore_register(uint32_t key, void *buf, uint32_t size, uint32_t delay_ms)
{
    CHECK(n_store < 3 && size <= 248);
    store[n_store].key = key;
    store[n_store].buf = buf;
    store[n_store].size = size;
    store[n_store].delay = delay_ms;
    n_store++;
}
void confstore_add_loaded_cb(void (*fn)(void)) { loaded_cb = fn; n_loaded_cb++; }
void confstore_mark_dirty(uint32_t key)
{
    int found = 0;
    for (int i = 0; i < n_store; i++)
        if (store[i].key == key) {
            store[i].dirty++;
            found = 1;
        }
    CHECK(found);                           /* the stock function asserts on unknown keys */
}

/* ---- helpers ---- */

#define PAD_MODE 0x1D
#define SHIFT    0x3F
#define PLAY     0x48
#define RECORD   0x4A
#define OCT_DOWN 0x20
#define OCT_UP   0x21
#define ENC(n)   (56 + (n) - 1)             /* encoder 1..8 */
#define KEY(n)   (0x80 + (n))
#define CLOCK    24
#define KEYS_CH  9                          /* channel of the keys: irrelevant for the sequencers */

#define YELLOW   0xFFC000u
#define TIE_Y    0x302400u
#define ORANGE   0xFF4000u
#define TIE_RED  0x500000u
#define WHITE    0xFFFFFFu
#define CURSOR   0x404040u

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
static void down(int n, int note)
{
    CHECK(seq_io_event(0, KEY(n)) == TM_PASS);
    seq_note_out(KEY(n), KEYS_CH, (uint32_t)note, 90);
}

static void up(int n, int note)
{
    CHECK(seq_io_event(2, KEY(n)) == TM_PASS);
    seq_note_out(KEY(n), KEYS_CH, (uint32_t)note, 0);
}

static void key(int n, int note)
{
    down(n, note);
    up(n, note);
}

static void shift_tap(uint32_t id, int expect)
{
    set_feat(SHIFT, 127);
    CHECK(tap(id) == expect);
    set_feat(SHIFT, 0);
}

/* an encoder turned by `delta` ticks = `delta` detents; returns 1 if the sequencer took it */
static int turn(int enc, int delta)
{
    uint32_t id = ENC(enc);
    int r = seq_io_event(0, id);
    int e = seq_enc_event(0, id, delta & 0xFFFF);
    CHECK(seq_enc_event(1, id, delta & 0xFFFF) == e);
    CHECK(seq_enc_event(2, id, delta & 0xFFFF) == e);
    CHECK(seq_io_event(2, id) == r);
    CHECK((r == TM_ENC_ONLY) == e && (r == TM_PASS) == !e);
    return e;
}

/* clock note on / off on MIDI channel ch (0..15) */
static int clock_on(int ch)  { return seq_midi_rx(0x90 | (uint32_t)ch, CLOCK, 100); }
static int clock_off(int ch) { return seq_midi_rx(0x80 | (uint32_t)ch, CLOCK, 0); }

/* n MIDI clock ticks, the main loop running after each */
static void ticks(int n)
{
    while (n--) {
        g_clock_ticks++;
        seq_mode_poll();
    }
}

static int sent_is(int i, uint32_t status, uint32_t d1, uint32_t d2)
{
    return i < n_sent && sent[i][0] == status && sent[i][1] == d1 && sent[i][2] == d2;
}

static int text_is(const char *s)
{
    seq_mode_poll();
    return !strcmp(field[1], s);
}

static int dirty(int i)
{
    int n = store[i].dirty;
    store[i].dirty = 0;
    return n;
}

int main(void)
{
    g_clock_ticks = 0xFFFFFFF0;             /* wraps during the test */
    seq_mode_init();
    CHECK(n_store == 3 && n_loaded_cb == 1);
    for (int i = 0; i < 3; i++) {
        CHECK(store[i].size == sizeof(seq_data_t) && store[i].delay == 3000);
        CHECK(store[i].key != store[(i + 1) % 3].key && store[i].buf != store[(i + 1) % 3].buf);
        CHECK(store[i].buf->len == 0 && store[i].buf->sync == 0 && store[i].buf->amount == 27);
    }

    /* ---- another pad mode: nothing is touched ---- */
    feat[PAD_MODE] = 8;
    seq_mode_poll();
    CHECK(tap(PLAY) == TM_PASS && tap(RECORD) == TM_PASS && tap(3) == TM_PASS);
    shift_tap(OCT_DOWN, TM_PASS);
    shift_tap(RECORD, TM_PASS);
    shift_tap(PLAY, TM_PASS);
    key(0, 60);
    for (int e = 1; e <= 8; e++)
        CHECK(turn(e, 1) == 0);
    for (int ch = 0; ch < 16; ch++)
        CHECK(clock_on(ch) == 0 && clock_off(ch) == 0);
    ticks(30);
    CHECK(n_sent == 0 && n_popup == 0);
    for (int i = 0; i < 16; i++)
        CHECK(led_rgb[i] == 0 && led_mode[i] == 0);

    /* ---- pad mode Custom 1 = sequencer 1 ---- */
    set_feat(PAD_MODE, 5);
    seq_mode_poll();
    CHECK(n_popup == 1 && !strcmp(field[0], "SH-101 Seq 1") && !strcmp(field[1], "0 steps"));
    for (int i = 0; i < 16; i++)
        CHECK(led_rgb[i] == 0 && led_mode[i] == LED_STATIC);
    CHECK(tap(0) == TM_CONSUME && tap(15) == TM_CONSUME);        /* pads: owned, no function */
    key(0, 60);                                                   /* not recording: ignored */
    shift_tap(OCT_DOWN, TM_PASS);                                 /* transpose as usual */
    shift_tap(OCT_UP, TM_PASS);
    shift_tap(PLAY, TM_PASS);                                     /* stock */
    CHECK(tap(OCT_DOWN) == TM_PASS && tap(OCT_UP) == TM_PASS);    /* octave as usual */
    CHECK(tap(PLAY) == TM_CONSUME);
    CHECK(text_is("Empty"));
    CHECK(clock_on(0) == 0 && clock_off(0) == 0 && n_sent == 0);  /* not playing: note passes */
    CHECK(dirty(0) == 0);

    /* ---- recording ---- */
    CHECK(tap(RECORD) == TM_CONSUME);
    CHECK(text_is("Rec step 1") && !strcmp(field[0], "SH-101 Seq 1"));
    CHECK(led_rgb[0] == CURSOR && led_mode[0] == LED_PULSE && led_rgb[1] == 0 && led_mode[1] == LED_STATIC);
    key(0, 48);
    CHECK(text_is("1: Note 48") && led_rgb[0] == YELLOW && led_mode[0] == LED_STATIC && led_rgb[1] == CURSOR);
    CHECK(dirty(0) == 1 && dirty(1) == 0);
    CHECK(tap(OCT_DOWN) == TM_PASS && tap(OCT_UP) == TM_PASS);    /* without Shift: octave */
    shift_tap(OCT_DOWN, TM_CONSUME);                              /* rest */
    CHECK(text_is("2: Rest") && led_rgb[1] == 0 && led_mode[1] == LED_STATIC && led_rgb[2] == CURSOR);
    key(4, 55);
    shift_tap(OCT_UP, TM_CONSUME);                                /* tie */
    CHECK(text_is("4: Tie") && led_rgb[2] == YELLOW && led_rgb[3] == TIE_Y && led_rgb[4] == CURSOR);

    down(0, 60);                                                  /* step 5: chord */
    down(4, 64);
    seq_note_out(KEY(4), 0x10, 7, 80);                            /* a CC from a control */
    seq_note_out(3, 0, 61, 80);                                   /* a pad note */
    CHECK(text_is("5: 2 notes") && led_rgb[4] == YELLOW && led_rgb[5] == CURSOR);
    up(0, 60);                                                    /* one key still held */
    down(7, 67);
    down(9, 70);
    down(11, 72);                                                 /* a fifth note is dropped */
    down(4, 64);                                                  /* as is a note twice */
    CHECK(text_is("5: 4 notes") && led_rgb[5] == CURSOR && led_rgb[6] == 0);
    up(4, 64); up(7, 67); up(9, 70); up(11, 72); up(4, 64);
    CHECK(dirty(0) == 7);

    /* accent: Shift + Play, for the step entered last or chosen with a pad */
    shift_tap(PLAY, TM_CONSUME);
    CHECK(text_is("5: Accent on") && led_rgb[4] == ORANGE && dirty(0) == 1);
    shift_tap(PLAY, TM_CONSUME);
    CHECK(text_is("5: Accent off") && led_rgb[4] == YELLOW);
    shift_tap(PLAY, TM_CONSUME);
    CHECK(tap(3) == TM_CONSUME);                                  /* the tie */
    CHECK(text_is("Rec step 4") && led_rgb[3] == CURSOR && led_mode[3] == LED_PULSE && led_rgb[4] == ORANGE);
    shift_tap(PLAY, TM_CONSUME);
    CHECK(text_is("4: Accent on"));
    CHECK(tap(5) == TM_CONSUME);                                  /* first free step */
    CHECK(text_is("Rec step 6") && led_rgb[3] == TIE_RED && led_mode[3] == LED_STATIC && led_rgb[5] == CURSOR);
    CHECK(tap(7) == TM_CONSUME);                                  /* beyond the end: no move */
    CHECK(text_is("Rec step 6") && led_rgb[5] == CURSOR && led_rgb[7] == 0);
    dirty(0);

    CHECK(clock_on(0) == 0 && n_sent == 0);                       /* no playback while recording */
    CHECK(tap(RECORD) == TM_CONSUME);                             /* second press: done */
    CHECK(text_is("5 steps"));
    CHECK(led_rgb[0] == YELLOW && led_rgb[1] == 0 && led_rgb[2] == YELLOW && led_rgb[3] == TIE_RED
          && led_rgb[4] == ORANGE && led_rgb[5] == 0);
    key(0, 50);                                                   /* not recording any more */
    key(0, CLOCK);                                                /* not playing: no clock */
    shift_tap(OCT_DOWN, TM_PASS);
    shift_tap(PLAY, TM_PASS);
    CHECK(tap(3) == TM_CONSUME);                                  /* pads select only while recording */
    CHECK(text_is("5 steps") && led_rgb[5] == 0 && led_rgb[3] == TIE_RED && dirty(0) == 0);

    /* ---- playback on channel 1: 48, rest, 55, tie, accented chord ---- */
    CHECK(tap(PLAY) == TM_CONSUME);
    CHECK(text_is("Play") && n_sent == 0);
    CHECK(clock_on(1) == 0 && clock_on(2) == 0 && clock_on(3) == 0 && n_sent == 0);
    CHECK(clock_on(0) == 1);
    CHECK(n_sent == 1 && sent_is(0, 0x90, 48, 100));
    seq_mode_poll();
    CHECK(led_rgb[0] == WHITE && led_rgb[2] == YELLOW);
    CHECK(clock_off(0) == 1);
    CHECK(n_sent == 2 && sent_is(1, 0x80, 48, 0));
    clock_on(0);                                                  /* rest */
    seq_mode_poll();
    CHECK(n_sent == 2 && led_rgb[0] == YELLOW && led_rgb[1] == WHITE);
    clock_off(0);
    clock_on(0);
    CHECK(n_sent == 3 && sent_is(2, 0x90, 55, 100));
    clock_off(0);
    CHECK(n_sent == 3);                                           /* tie follows: held */
    clock_on(0);
    seq_mode_poll();
    CHECK(n_sent == 3 && led_rgb[3] == WHITE);
    clock_off(0);
    CHECK(n_sent == 4 && sent_is(3, 0x80, 55, 0));
    clock_on(0);
    CHECK(n_sent == 8 && sent_is(4, 0x90, 60, 127) && sent_is(5, 0x90, 64, 127)
          && sent_is(6, 0x90, 67, 127) && sent_is(7, 0x90, 70, 127));
    CHECK(seq_midi_rx(0x90, CLOCK, 0) == 1);                      /* note on, velocity 0 = off */
    CHECK(n_sent == 12 && sent_is(8, 0x80, 60, 0) && sent_is(11, 0x80, 70, 0));
    CHECK(seq_midi_rx(0x90, 25, 100) == 0 && seq_midi_rx(0xB0, CLOCK, 1) == 0 && n_sent == 12);
    clock_on(0);                                                  /* loops to step 1 */
    CHECK(n_sent == 13 && sent_is(12, 0x90, 48, 100));

    /* the clock note played on the keys steps the selected sequencer */
    n_sent = 0;
    CHECK(seq_io_event(0, KEY(0)) == TM_PASS);
    seq_note_out(KEY(0), KEYS_CH, CLOCK, 70);                     /* rest */
    CHECK(n_sent == 1 && sent_is(0, 0x80, 48, 0));
    seq_note_out(KEY(0), KEYS_CH, CLOCK, 0);
    seq_note_out(KEY(0), KEYS_CH, CLOCK, 70);                     /* 55 */
    CHECK(n_sent == 2 && sent_is(1, 0x90, 55, 100));
    seq_note_out(KEY(3), KEYS_CH, CLOCK + 1, 70);                 /* other keys just play */
    seq_note_out(3, KEYS_CH, CLOCK, 70);                          /* not a key */
    seq_note_out(KEY(0), 0x19, CLOCK, 70);                        /* a CC */
    CHECK(n_sent == 2);
    CHECK(seq_io_event(2, KEY(0)) == TM_PASS);
    seq_mode_poll();
    CHECK(led_rgb[2] == WHITE && led_rgb[0] == YELLOW);

    /* ---- encoders 1..5: rate, direction, accent amount, first and last step ---- */
    CHECK(turn(3, -5) == 1);
    CHECK(text_is("Accent +22") && dirty(0) == 1);
    turn(3, -100);
    CHECK(text_is("Accent +7"));
    clock_off(0);                                                 /* 55 is held: tie */
    clock_on(0);
    clock_on(0);                                                  /* accented chord */
    clock_on(0);
    CHECK(sent_is(n_sent - 1, 0x90, 48, 120) && sent_is(n_sent - 2, 0x80, 70, 0));
    turn(3, 100);
    CHECK(text_is("Accent +27"));
    turn(2, 1);
    CHECK(text_is("Reverse"));
    n_sent = 0;
    clock_on(0);                                                  /* from step 1 back to step 5 */
    CHECK(n_sent == 5 && sent_is(1, 0x90, 60, 127));
    turn(2, 1);
    CHECK(text_is("Alternate"));
    turn(2, 5);
    CHECK(text_is("Random"));
    turn(2, -9);
    CHECK(text_is("Forward"));
    turn(4, 2);
    CHECK(text_is("First step 3"));
    turn(4, 9);
    CHECK(text_is("First step 5"));
    turn(5, 3);                                                   /* longer than entered */
    CHECK(text_is("Last step 8") && led_rgb[7] == 0);
    n_sent = 0;
    clock_on(0);                                                  /* step 6: a rest */
    seq_mode_poll();
    CHECK(n_sent == 4 && led_rgb[5] == WHITE);
    clock_on(0); clock_on(0); clock_on(0);                        /* 7, 8, then step 5 again */
    CHECK(n_sent == 8 && sent_is(4, 0x90, 60, 127));
    turn(5, 40);
    CHECK(text_is("Last step 32"));
    turn(5, -27);
    turn(4, -9);
    CHECK(text_is("First step 1") && store[0].buf->len == 5 && store[0].buf->start == 0);
    turn(1, -1);
    CHECK(text_is("Clock /1.5"));
    turn(1, -20);
    CHECK(text_is("Clock /7"));
    turn(1, 30);
    CHECK(text_is("Clock x7"));
    turn(1, -7);
    CHECK(text_is("Clock x1") && dirty(0) > 10);
    set_feat(SHIFT, 127);                                         /* Shift + encoder 1..5: stock */
    for (int e = 1; e <= 5; e++)
        CHECK(turn(e, 1) == 0);
    set_feat(SHIFT, 0);
    CHECK(dirty(0) == 0);

    /* ---- encoders 6..8: controllers ---- */
    n_sent = 0;
    CHECK(turn(6, 10) == 1);                                      /* not recording: just sent */
    CHECK(n_sent == 1 && sent_is(0, 0xB0, 74, 10) && text_is("CC74: 10") && dirty(0) == 0);
    turn(7, 200);
    turn(7, 3);                                                   /* at the limit: not sent again */
    turn(8, -3);
    CHECK(n_sent == 2 && sent_is(1, 0xB0, 71, 127) && text_is("CC1: 0"));
    CHECK(led_rgb[0] == YELLOW);
    set_feat(SHIFT, 127);
    CHECK(turn(8, 6) == 1);
    CHECK(text_is("Knob 8: CC 7") && dirty(0) == 1 && n_sent == 2);
    turn(6, 500);
    CHECK(text_is("Knob 6: CC 127"));
    turn(6, -53);
    CHECK(text_is("Knob 6: CC 74"));
    set_feat(SHIFT, 0);
    dirty(0);

    /* while recording the value goes into the edit step and the pads show the lane */
    tap(PLAY);                                                    /* stop */
    tap(RECORD);
    n_sent = 0;
    turn(6, 5);                                                   /* step 1 (record position) */
    CHECK(n_sent == 1 && sent_is(0, 0xB0, 74, 15) && text_is("CC74 S1: 15") && dirty(0) == 1);
    CHECK(led_rgb[0] == 0x33 && led_mode[0] == LED_STATIC && led_rgb[1] == 0 && led_rgb[4] == 0);
    key(0, 49);                                                   /* a note ends the lane view */
    CHECK(text_is("1: Note 49") && led_rgb[0] == YELLOW && led_rgb[1] == CURSOR);
    turn(6, 112);                                                 /* step 1 again: entered last */
    CHECK(text_is("CC74 S1: 127") && led_rgb[0] == 0xFF && led_rgb[1] == 0);
    tap(2);
    turn(8, 0);                                                   /* step 3, lane 3, value 0 */
    CHECK(text_is("CC7 S3: 0") && led_rgb[2] == 0x18 && led_rgb[0] == 0);
    ticks(47);
    CHECK(led_rgb[2] == 0x18);
    ticks(1);                                                     /* back to the steps */
    CHECK(led_rgb[2] == CURSOR && led_rgb[0] == YELLOW && led_rgb[4] == ORANGE);
    tap(RECORD);
    CHECK(text_is("5 steps"));
    turn(6, -27);                                                 /* not recording: nothing stored */
    dirty(0);
    tap(PLAY);
    n_sent = 0;
    clock_on(0);                                                  /* controllers before the notes */
    CHECK(n_sent == 2 && sent_is(0, 0xB0, 74, 127) && sent_is(1, 0x90, 49, 100));
    clock_on(0);
    clock_on(0);
    CHECK(n_sent == 5 && sent_is(2, 0x80, 49, 0) && sent_is(3, 0xB0, 7, 0) && sent_is(4, 0x90, 55, 100));
    tap(PLAY);

    /* ---- MIDI clock: Shift + Record while not recording ---- */
    shift_tap(RECORD, TM_CONSUME);
    CHECK(text_is("MIDI clock") && dirty(0) == 1 && store[0].buf->sync == 1);
    n_sent = 0;
    tap(PLAY);                                                    /* first step right away */
    CHECK(text_is("Play") && n_sent == 2 && sent_is(1, 0x90, 49, 100));
    CHECK(clock_on(0) == 0 && n_sent == 2);                       /* the clock note is ignored */
    seq_note_out(KEY(0), KEYS_CH, CLOCK, 70);
    CHECK(n_sent == 2);
    ticks(2);
    CHECK(n_sent == 2);
    ticks(1);                                                     /* half a 1/16: gate ends */
    CHECK(n_sent == 3 && sent_is(2, 0x80, 49, 0));
    ticks(2);
    CHECK(n_sent == 3);
    ticks(1);                                                     /* step 2: rest */
    CHECK(n_sent == 3 && led_rgb[1] == WHITE);
    ticks(6);
    CHECK(n_sent == 5 && sent_is(4, 0x90, 55, 100));              /* step 3 with its controller */
    ticks(3);
    CHECK(n_sent == 5);                                           /* tie follows */
    ticks(6);
    CHECK(n_sent == 6 && sent_is(5, 0x80, 55, 0));
    ticks(3);
    CHECK(n_sent == 10 && sent_is(6, 0x90, 60, 127));
    turn(1, 2);                                                   /* x2: a step every 3 ticks */
    CHECK(text_is("Clock x2"));
    n_sent = 0;
    ticks(15);                                                    /* 5 steps: back on the chord */
    CHECK(sent_is(n_sent - 1, 0x90, 70, 127));
    turn(1, 5);                                                   /* x7: 7 steps in 6 ticks */
    n_sent = 0;
    {
        int ons = 0;
        ticks(30);
        for (int i = 0; i < n_sent; i++)
            ons += sent[i][0] == 0x90 && (sent[i][1] == 49 || sent[i][1] == 55);
        CHECK(ons == 14);                                         /* 35 steps = 7 laps */
    }
    turn(1, -9);                                                  /* /2: a step every 12 ticks */
    CHECK(text_is("Clock /2"));
    tap(PLAY);
    n_sent = 0;
    tap(PLAY);
    CHECK(n_sent == 2);
    ticks(5);
    CHECK(n_sent == 2);
    ticks(1);
    CHECK(n_sent == 3 && sent_is(2, 0x80, 49, 0));
    ticks(5);
    CHECK(n_sent == 3 && led_rgb[0] == WHITE);
    ticks(1);
    CHECK(led_rgb[1] == WHITE);
    turn(1, 1);                                                   /* /1.5: a step every 9 ticks */
    n_sent = 0;
    ticks(9 * 5);
    {
        int ons = 0;
        for (int i = 0; i < n_sent; i++)
            ons += sent[i][0] == 0x90;
        CHECK(ons == 6);                                          /* 55, chord, 49 */
    }
    turn(1, 1);

    /* in the background too, next to a sequencer on its clock note */
    set_feat(PAD_MODE, 8);
    for (int i = 0; i < 16; i++)
        CHECK(led_mode[i] == LED_RELEASE);
    n_sent = 0;
    ticks(6 * 5);
    CHECK(n_sent == 14);
    CHECK(turn(1, 1) == 0 && tap(PLAY) == TM_PASS && tap(0) == TM_PASS);
    set_feat(PAD_MODE, 6);
    CHECK(text_is("0 steps") && !strcmp(field[0], "SH-101 Seq 2"));
    for (int i = 0; i < 16; i++)
        CHECK(led_rgb[i] == 0 && led_mode[i] == LED_STATIC);
    tap(RECORD);
    key(0, 72);
    key(1, 74);
    tap(PLAY);                                                    /* ends recording, plays */
    CHECK(text_is("Play") && dirty(1) == 2 && dirty(0) > 0);
    n_sent = 0;
    CHECK(clock_on(1) == 1 && n_sent == 1 && sent_is(0, 0x91, 72, 100));
    CHECK(clock_on(0) == 0 && clock_on(2) == 0);
    seq_mode_poll();
    CHECK(led_rgb[0] == WHITE && led_rgb[1] == YELLOW && led_rgb[4] == 0);
    CHECK(clock_off(1) == 1 && n_sent == 2 && sent_is(1, 0x81, 72, 0));
    ticks(6 * 5);
    CHECK(n_sent == 2 + 14);
    seq_note_out(KEY(0), KEYS_CH, CLOCK, 70);                     /* key clock: sequencer 2 only */
    CHECK(sent_is(n_sent - 1, 0x91, 74, 100));
    turn(6, 3);                                                   /* its own controller numbers */
    CHECK(sent_is(n_sent - 1, 0xB1, 74, 103));

    /* Custom 3 = sequencer 3, channel 3 */
    set_feat(PAD_MODE, 7);
    CHECK(text_is("0 steps") && !strcmp(field[0], "SH-101 Seq 3"));
    tap(RECORD);
    key(0, 36);
    tap(PLAY);
    n_sent = 0;
    CHECK(clock_on(2) == 1 && n_sent == 1 && sent_is(0, 0x92, 36, 100));
    CHECK(tap(PLAY) == TM_CONSUME);                               /* stops sequencer 3 only */
    CHECK(n_sent == 2 && sent_is(1, 0x82, 36, 0));
    CHECK(clock_on(2) == 0 && clock_on(1) == 1);
    CHECK(dirty(2) == 1);

    /* back in Custom 1: its state is shown; Shift + Record returns to the clock note */
    set_feat(PAD_MODE, 5);
    CHECK(text_is("5 steps, play") && !strcmp(field[0], "SH-101 Seq 1") && led_mode[0] == LED_STATIC);
    shift_tap(RECORD, TM_CONSUME);
    CHECK(text_is("Clock note C0") && store[0].buf->sync == 0);
    n_sent = 0;
    ticks(24);
    CHECK(n_sent == 0);
    CHECK(clock_on(0) == 1 && n_sent == 2 && sent_is(1, 0x90, 49, 100));   /* from the start */

    /* Shift held: pads are left to the Shift overlay */
    for (int i = 0; i < 16; i++)
        led_rgb[i] = 0x123456;
    set_feat(SHIFT, 127);
    clock_off(0);
    clock_on(0);
    seq_mode_poll();
    CHECK(led_rgb[0] == 0x123456 && led_rgb[1] == 0x123456);
    CHECK(tap(PLAY) == TM_PASS && tap(2) == TM_PASS);
    set_feat(SHIFT, 0);
    seq_mode_poll();
    CHECK(led_rgb[0] == YELLOW && led_rgb[1] == WHITE);

    /* a menu captures pads and encoders: not drawn / taken, and redrawn when it lets go */
    CHECK(tap(0x1A) == TM_PASS);                                  /* some other button: menus win */
    blocked = 0x0003 | 1u << (ENC(2) & 31);
    seq_mode_poll();
    led_rgb[0] = led_rgb[1] = 0x654321;
    clock_off(0);
    clock_on(0);
    seq_mode_poll();
    CHECK(led_rgb[0] == 0x654321 && led_rgb[1] == 0x654321 && led_rgb[2] == WHITE);
    CHECK(tap(0) == TM_PASS && tap(2) == TM_CONSUME);
    CHECK(turn(2, 1) == 0 && turn(3, 0) == 1);
    set_feat(PAD_MODE, 5);                                        /* reselected: ours again */
    CHECK(turn(2, 0) == 1);
    blocked = 0;
    seq_mode_poll();
    CHECK(led_rgb[0] == YELLOW && led_rgb[1] == 0 && led_rgb[2] == WHITE);

    /* Play again stops and releases the note */
    n_sent = 0;
    CHECK(tap(PLAY) == TM_CONSUME);
    CHECK(text_is("Stop") && n_sent == 1 && sent_is(0, 0x80, 55, 0) && led_rgb[2] == YELLOW);
    CHECK(clock_on(0) == 0 && n_sent == 1);

    /* ---- Shift + Record while recording empties the sequence ---- */
    tap(RECORD);
    key(0, 50);
    dirty(0);
    shift_tap(RECORD, TM_CONSUME);
    CHECK(text_is("Cleared") && dirty(0) == 1 && store[0].buf->len == 0);
    CHECK(led_rgb[0] == CURSOR && led_rgb[1] == 0 && led_rgb[4] == 0);
    key(0, 53);
    CHECK(text_is("1: Note 53") && led_rgb[0] == YELLOW && led_rgb[1] == CURSOR);
    CHECK(clock_on(1) == 1);                                      /* the others are untouched */

    /* leaving the pad mode ends the recording */
    set_feat(PAD_MODE, 6);
    set_feat(PAD_MODE, 5);
    key(0, 54);
    CHECK(text_is("1 step") && led_rgb[0] == YELLOW && led_rgb[1] == 0);

    /* ---- pages of 16 steps; the memory holds 32 ---- */
    tap(RECORD);
    for (int i = 0; i < 20; i++)
        key(i % 25, 40 + i);
    CHECK(text_is("20: Note 59"));
    CHECK(led_rgb[0] == YELLOW && led_rgb[3] == YELLOW && led_rgb[4] == CURSOR && led_rgb[5] == 0);
    tap(1);                                                       /* step 18 */
    CHECK(text_is("Rec step 18") && led_rgb[1] == CURSOR && led_rgb[3] == YELLOW);
    key(0, 90);
    for (int i = 18; i < 32; i++)
        key(i % 25, 40 + i);
    CHECK(text_is("32: Note 71"));
    for (int i = 0; i < 16; i++)
        CHECK(led_rgb[i] == YELLOW && led_mode[i] == LED_STATIC);  /* no free step to point at */
    shift_tap(PLAY, TM_CONSUME);
    CHECK(text_is("32: Accent on") && led_rgb[15] == ORANGE);
    key(0, 99);                                                   /* full: recording ends */
    CHECK(text_is("32 steps"));
    key(0, 99);
    n_sent = 0;
    tap(PLAY);
    CHECK(text_is("Play"));
    for (int i = 0; i < 32; i++)
        clock_on(0);
    seq_mode_poll();
    CHECK(n_sent == 63 && sent_is(34, 0x90, 90, 100) && sent_is(62, 0x90, 71, 127));
    CHECK(led_rgb[15] == WHITE && led_rgb[0] == YELLOW);
    tap(PLAY);

    /* ---- settings store: records read after start-up are checked and shown ---- */
    {
        seq_data_t *d = store[0].buf;
        memset(d, 0xFF, sizeof *d);
        d->len = 3;
        d->start = 7;
        d->steps[0] = 60;
        d->steps[SEQ_STEP_BYTES] = SEQ_TIE;
        d->steps[2 * SEQ_STEP_BYTES] = 62;
        d->accent[0] = 4;
        loaded_cb();
        CHECK(d->start == 0 && d->dir == 0 && d->sync == 1 && d->amount == 27 && d->cc[0] == 127);
        seq_mode_poll();
        CHECK(led_rgb[0] == YELLOW && led_rgb[1] == TIE_Y && led_rgb[2] == ORANGE && led_rgb[3] == 0);
        turn(1, 0);
        CHECK(text_is("Clock x1"));
        d->amount = 0;
        loaded_cb();
        CHECK(d->amount == 7);
        n_sent = 0;
        tap(PLAY);
        CHECK(n_sent == 1 && sent_is(0, 0x90, 60, 120));
        tap(PLAY);
    }

    /* the three sequences are independent */
    set_feat(PAD_MODE, 6);
    CHECK(text_is("2 steps, play") && led_rgb[2] == 0);
    set_feat(PAD_MODE, 7);
    CHECK(text_is("1 step") && led_rgb[0] == YELLOW && led_rgb[1] == 0);

    printf("seq_mode: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
