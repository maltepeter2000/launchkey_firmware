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

#define MAX_SENT 128
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
#define KEYS_CH  0x64      /* channel of the keys: irrelevant for the sequencers */
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

/* a key going down / up with the note it sends */
static void down(int n, int note)
{
    CHECK(seq_io_event(0, KEY(n)) == TM_PASS);
    seq_note_out(KEY(n), feat[KEYS_CH], (uint32_t)note, 90);
}

static void up(int n, int note)
{
    CHECK(seq_io_event(2, KEY(n)) == TM_PASS);
    seq_note_out(KEY(n), feat[KEYS_CH], (uint32_t)note, 0);
}

static void shift_tap(uint32_t id, int expect)
{
    set_feat(SHIFT, 127);
    CHECK(tap(id) == expect);
    set_feat(SHIFT, 0);
}

/* clock note on / off on MIDI channel ch (0..15) */
static int clock_on(int ch)  { return seq_midi_rx(0x90 | (uint32_t)ch, CLOCK, 100); }
static int clock_off(int ch) { return seq_midi_rx(0x80 | (uint32_t)ch, CLOCK, 0); }

static int sent_is(int i, uint32_t status, uint32_t d1, uint32_t d2)
{
    return i < n_sent && sent[i][0] == status && sent[i][1] == d1 && sent[i][2] == d2;
}

int main(void)
{
    seq_mode_init();
    feat[KEYS_CH] = 9;

    /* ---- another pad mode: nothing is touched ---- */
    feat[PAD_MODE] = 8;
    seq_mode_poll();
    CHECK(tap(PLAY) == TM_PASS && tap(RECORD) == TM_PASS && tap(3) == TM_PASS);
    shift_tap(OCT_DOWN, TM_PASS);
    shift_tap(RECORD, TM_PASS);
    key(0, 60);
    for (int ch = 0; ch < 16; ch++)
        CHECK(clock_on(ch) == 0 && clock_off(ch) == 0);
    seq_mode_poll();
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
    CHECK(tap(OCT_DOWN) == TM_PASS && tap(OCT_UP) == TM_PASS);    /* octave as usual */
    CHECK(tap(PLAY) == TM_CONSUME);
    seq_mode_poll();
    CHECK(!strcmp(field[1], "Empty"));
    CHECK(clock_on(0) == 0 && clock_off(0) == 0 && n_sent == 0);  /* not playing: note passes */

    /* ---- recording ---- */
    CHECK(tap(RECORD) == TM_CONSUME);
    seq_mode_poll();
    CHECK(!strcmp(field[0], "SH-101 Seq 1") && !strcmp(field[1], "Rec step 1"));
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

    down(0, 60);                                                  /* step 5: chord */
    down(4, 64);
    seq_note_out(KEY(4), 0x10, 7, 80);                            /* a CC from a control */
    seq_note_out(3, 0, 61, 80);                                   /* a pad note */
    seq_mode_poll();
    CHECK(!strcmp(field[1], "5: 2 notes") && led_rgb[4] == GREEN_DIM && led_rgb[5] == RED);
    up(0, 60);                                                    /* one key still held */
    down(7, 67);
    down(9, 70);
    down(11, 72);                                                 /* a fifth note is dropped */
    down(4, 64);                                                  /* as is a note twice */
    seq_mode_poll();
    CHECK(!strcmp(field[1], "5: 4 notes") && led_rgb[5] == RED && led_rgb[6] == 0);
    up(4, 64); up(7, 67); up(9, 70); up(11, 72); up(4, 64);

    CHECK(clock_on(0) == 0 && n_sent == 0);                       /* no playback while recording */
    CHECK(tap(RECORD) == TM_CONSUME);                             /* second press: done */
    seq_mode_poll();
    CHECK(!strcmp(field[1], "5 steps"));
    CHECK(led_rgb[0] == GREEN_DIM && led_rgb[1] == BLUE_DIM && led_rgb[3] == AMBER_DIM
          && led_rgb[4] == GREEN_DIM && led_rgb[5] == 0);
    key(0, 50);                                                   /* not recording any more */
    key(0, CLOCK);                                                /* not playing: no clock */
    shift_tap(OCT_DOWN, TM_PASS);
    CHECK(tap(3) == TM_CONSUME);                                  /* pads select only while recording */
    seq_mode_poll();
    CHECK(!strcmp(field[1], "5 steps") && led_rgb[5] == 0 && led_rgb[3] == AMBER_DIM);

    /* ---- playback on channel 1: 48, rest, 55, tie, chord ---- */
    CHECK(tap(PLAY) == TM_CONSUME);
    seq_mode_poll();
    CHECK(!strcmp(field[1], "Play") && n_sent == 0);
    CHECK(clock_on(1) == 0 && clock_on(2) == 0 && clock_on(3) == 0 && n_sent == 0);
    CHECK(clock_on(0) == 1);
    CHECK(n_sent == 1 && sent_is(0, 0x90, 48, 100));
    seq_mode_poll();
    CHECK(led_rgb[0] == GREEN_ON && led_rgb[1] == BLUE_DIM);
    CHECK(clock_off(0) == 1);
    CHECK(n_sent == 2 && sent_is(1, 0x80, 48, 0));
    clock_on(0);                                                  /* rest */
    seq_mode_poll();
    CHECK(n_sent == 2 && led_rgb[0] == GREEN_DIM && led_rgb[1] == BLUE_ON);
    clock_off(0);
    clock_on(0);
    CHECK(n_sent == 3 && sent_is(2, 0x90, 55, 100));
    clock_off(0);
    CHECK(n_sent == 3);                                           /* tie follows: held */
    clock_on(0);
    seq_mode_poll();
    CHECK(n_sent == 3 && led_rgb[3] == AMBER_ON);
    clock_off(0);
    CHECK(n_sent == 4 && sent_is(3, 0x80, 55, 0));
    clock_on(0);
    CHECK(n_sent == 8 && sent_is(4, 0x90, 60, 100) && sent_is(5, 0x90, 64, 100)
          && sent_is(6, 0x90, 67, 100) && sent_is(7, 0x90, 70, 100));
    CHECK(seq_midi_rx(0x90, CLOCK, 0) == 1);                      /* note on, velocity 0 = off */
    CHECK(n_sent == 12 && sent_is(8, 0x80, 60, 0) && sent_is(11, 0x80, 70, 0));
    CHECK(seq_midi_rx(0x90, 25, 100) == 0 && seq_midi_rx(0xB0, CLOCK, 1) == 0 && n_sent == 12);
    clock_on(0);                                                  /* loops to step 1 */
    CHECK(n_sent == 13 && sent_is(12, 0x90, 48, 100));

    /* the clock note played on the keys steps the selected sequencer (press = next step,
     * release = gate end), whatever channel the keys are on */
    n_sent = 0;
    CHECK(seq_io_event(0, KEY(0)) == TM_PASS);
    seq_note_out(KEY(0), 9, CLOCK, 70);                           /* rest */
    CHECK(n_sent == 1 && sent_is(0, 0x80, 48, 0));
    seq_note_out(KEY(0), 9, CLOCK, 0);
    seq_note_out(KEY(0), 9, CLOCK, 70);                           /* 55 */
    CHECK(n_sent == 2 && sent_is(1, 0x90, 55, 100));
    seq_note_out(KEY(3), 9, CLOCK + 1, 70);                       /* other keys just play */
    seq_note_out(KEY(3), 9, CLOCK + 1, 0);
    seq_note_out(3, 9, CLOCK, 70);                                /* not a key */
    seq_note_out(KEY(0), 0x19, CLOCK, 70);                        /* a CC */
    CHECK(n_sent == 2);
    CHECK(seq_io_event(2, KEY(0)) == TM_PASS);
    seq_mode_poll();
    CHECK(led_rgb[2] == GREEN_ON && led_rgb[0] == GREEN_DIM);

    /* ---- background: sequencer 1 keeps playing in Custom 2, which has its own sequence ---- */
    set_feat(PAD_MODE, 6);
    CHECK(n_sent == 2);                                           /* nothing released */
    seq_mode_poll();
    CHECK(!strcmp(field[0], "SH-101 Seq 2") && !strcmp(field[1], "0 steps"));
    for (int i = 0; i < 16; i++)
        CHECK(led_rgb[i] == 0 && led_mode[i] == LED_STATIC);
    CHECK(clock_on(0) == 1);                                      /* tie: 55 held */
    CHECK(clock_off(0) == 1 && n_sent == 3 && sent_is(2, 0x80, 55, 0));
    seq_mode_poll();
    for (int i = 0; i < 16; i++)
        CHECK(led_rgb[i] == 0);                                   /* not this sequencer's pads */
    seq_note_out(KEY(0), 9, CLOCK, 70);                           /* key clock: sequencer 2 only */
    CHECK(n_sent == 3);
    tap(RECORD);
    key(0, 72);
    key(1, 74);
    tap(PLAY);                                                    /* ends recording, plays */
    seq_mode_poll();
    CHECK(!strcmp(field[0], "SH-101 Seq 2") && !strcmp(field[1], "Play"));
    n_sent = 0;
    CHECK(clock_on(1) == 1 && n_sent == 1 && sent_is(0, 0x91, 72, 100));
    CHECK(clock_on(0) == 1 && n_sent == 5 && sent_is(1, 0x90, 60, 100));   /* seq 1: chord */
    seq_mode_poll();
    CHECK(led_rgb[0] == GREEN_ON && led_rgb[1] == GREEN_DIM && led_rgb[4] == 0);
    CHECK(clock_off(1) == 1 && n_sent == 6 && sent_is(5, 0x81, 72, 0));
    CHECK(clock_on(2) == 0);                                      /* sequencer 3 is not playing */

    /* a pad mode without sequencer: pads are freed, both go on */
    set_feat(PAD_MODE, 8);
    for (int i = 0; i < 16; i++)
        CHECK(led_mode[i] == LED_RELEASE);
    CHECK(tap(PLAY) == TM_PASS && tap(0) == TM_PASS && n_sent == 6);
    n_sent = 0;
    CHECK(clock_on(1) == 1 && n_sent == 1 && sent_is(0, 0x91, 74, 100));
    CHECK(clock_on(0) == 1 && n_sent == 6 && sent_is(1, 0x80, 60, 0) && sent_is(5, 0x90, 48, 100));
    seq_note_out(KEY(0), 9, CLOCK, 70);                           /* no key clock here */
    CHECK(n_sent == 6);

    /* Custom 3 = sequencer 3, channel 3 */
    set_feat(PAD_MODE, 7);
    seq_mode_poll();
    CHECK(!strcmp(field[0], "SH-101 Seq 3") && !strcmp(field[1], "0 steps"));
    tap(RECORD);
    key(0, 36);
    tap(PLAY);
    n_sent = 0;
    CHECK(clock_on(2) == 1 && n_sent == 1 && sent_is(0, 0x92, 36, 100));
    CHECK(tap(PLAY) == TM_CONSUME);                               /* stops sequencer 3 only */
    CHECK(n_sent == 2 && sent_is(1, 0x82, 36, 0));
    CHECK(clock_on(2) == 0 && clock_on(1) == 1 && clock_on(0) == 1);

    /* back in Custom 1: its state is shown */
    set_feat(PAD_MODE, 5);
    seq_mode_poll();
    CHECK(!strcmp(field[0], "SH-101 Seq 1") && !strcmp(field[1], "5 steps, play"));
    CHECK(led_rgb[0] == GREEN_DIM && led_rgb[1] == BLUE_ON && led_mode[0] == LED_STATIC);

    /* Shift held: pads are left to the Shift overlay, Shift + Play is stock */
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
    CHECK(led_rgb[1] == BLUE_DIM && led_rgb[2] == GREEN_ON);

    /* a menu captures pads: they are not drawn, and redrawn when it lets go */
    CHECK(tap(0x1A) == TM_PASS);                                  /* some other button: menus win */
    blocked = 0x0003;
    seq_mode_poll();
    led_rgb[0] = led_rgb[1] = 0x654321;
    clock_off(0);
    clock_on(0);
    seq_mode_poll();
    CHECK(led_rgb[0] == 0x654321 && led_rgb[1] == 0x654321 && led_rgb[3] == AMBER_ON);
    CHECK(tap(0) == TM_PASS && tap(2) == TM_CONSUME);
    blocked = 0;
    seq_mode_poll();
    CHECK(led_rgb[0] == GREEN_DIM && led_rgb[1] == BLUE_DIM && led_rgb[3] == AMBER_ON);

    /* Play again stops and releases the note */
    n_sent = 0;
    CHECK(tap(PLAY) == TM_CONSUME);
    seq_mode_poll();
    CHECK(!strcmp(field[1], "Stop") && n_sent == 1 && sent_is(0, 0x80, 55, 0));
    CHECK(led_rgb[3] == AMBER_DIM);
    CHECK(clock_on(0) == 0 && n_sent == 1);

    /* ---- Record keeps the sequence; a pad chooses the step to record from ---- */
    tap(PLAY);
    clock_on(0);
    n_sent = 0;
    tap(RECORD);                                                  /* while playing: note released */
    CHECK(n_sent == 1 && sent_is(0, 0x80, 48, 0));
    seq_mode_poll();
    CHECK(!strcmp(field[1], "Rec step 1"));
    CHECK(led_rgb[0] == RED && led_rgb[1] == BLUE_DIM && led_rgb[4] == GREEN_DIM && led_rgb[5] == 0);
    CHECK(tap(2) == TM_CONSUME);
    seq_mode_poll();
    CHECK(!strcmp(field[1], "Rec step 3"));
    CHECK(led_rgb[0] == GREEN_DIM && led_rgb[2] == RED && led_rgb[3] == AMBER_DIM);
    key(0, 57);                                                   /* replaces step 3 */
    seq_mode_poll();
    CHECK(!strcmp(field[1], "3: Note 57") && led_rgb[2] == GREEN_DIM && led_rgb[3] == RED);
    CHECK(tap(7) == TM_CONSUME);                                  /* beyond the end: no move */
    seq_mode_poll();
    CHECK(!strcmp(field[1], "3: Note 57") && led_rgb[3] == RED && led_rgb[7] == 0);
    tap(4);                                                       /* the chord becomes one note */
    down(0, 62);
    tap(4);                                                       /* a pad ends the chord */
    down(1, 65);                                                  /* so this replaces 62 */
    up(0, 62); up(1, 65);
    seq_mode_poll();
    CHECK(!strcmp(field[1], "5: Note 65") && led_rgb[5] == RED);
    tap(5);                                                       /* first free step */
    seq_mode_poll();
    CHECK(!strcmp(field[1], "Rec step 6"));
    key(0, 40);
    tap(RECORD);
    seq_mode_poll();
    CHECK(!strcmp(field[1], "6 steps") && led_rgb[5] == GREEN_DIM && led_rgb[6] == 0);
    tap(PLAY);
    n_sent = 0;
    for (int i = 0; i < 6; i++) {
        clock_on(0);
        clock_off(0);
    }
    CHECK(n_sent == 8 && sent_is(0, 0x90, 48, 100) && sent_is(2, 0x90, 57, 100)
          && sent_is(3, 0x80, 57, 0) && sent_is(4, 0x90, 65, 100) && sent_is(6, 0x90, 40, 100));

    /* ---- Shift + Record empties the sequence ---- */
    clock_on(0);
    n_sent = 0;
    shift_tap(RECORD, TM_CONSUME);
    CHECK(n_sent == 1 && sent_is(0, 0x80, 48, 0));
    seq_mode_poll();
    CHECK(!strcmp(field[1], "Cleared"));
    for (int i = 0; i < 16; i++)
        CHECK(led_rgb[i] == 0);
    CHECK(clock_on(0) == 0);                                      /* stopped */
    tap(PLAY);
    seq_mode_poll();
    CHECK(!strcmp(field[1], "Empty"));
    CHECK(clock_on(1) == 1);                                      /* the others are untouched */
    tap(RECORD);
    key(0, 50);
    key(1, 52);
    shift_tap(RECORD, TM_CONSUME);                                /* while recording: from step 1 */
    seq_mode_poll();
    CHECK(!strcmp(field[1], "Cleared") && led_rgb[0] == RED && led_rgb[1] == 0);
    key(0, 53);
    seq_mode_poll();
    CHECK(!strcmp(field[1], "1: Note 53") && led_rgb[1] == RED);

    /* leaving the pad mode ends the recording */
    set_feat(PAD_MODE, 6);
    set_feat(PAD_MODE, 5);
    key(0, 54);
    seq_mode_poll();
    CHECK(!strcmp(field[1], "1 step") && led_rgb[0] == GREEN_DIM && led_rgb[1] == 0);

    /* ---- pages of 16 steps; pads select on the page shown ---- */
    tap(RECORD);
    for (int i = 0; i < 20; i++)
        key(i % 25, 40 + i);
    seq_mode_poll();
    CHECK(!strcmp(field[1], "20: Note 59"));
    CHECK(led_rgb[0] == GREEN_DIM && led_rgb[3] == GREEN_DIM && led_rgb[4] == RED && led_rgb[5] == 0);
    tap(1);                                                       /* step 18 */
    seq_mode_poll();
    CHECK(!strcmp(field[1], "Rec step 18") && led_rgb[1] == RED && led_rgb[3] == GREEN_DIM);
    key(0, 90);
    n_sent = 0;
    CHECK(tap(PLAY) == TM_CONSUME);                               /* Play ends the recording */
    seq_mode_poll();
    CHECK(!strcmp(field[1], "Play"));
    for (int i = 0; i < 16; i++)
        CHECK(led_rgb[i] == (i == 0 ? GREEN_ON : GREEN_DIM));
    for (int i = 0; i < 20; i++) {
        clock_on(0);
        clock_off(0);
    }
    seq_mode_poll();
    CHECK(n_sent == 40 && sent_is(34, 0x90, 90, 100) && sent_is(38, 0x90, 59, 100)
          && sent_is(39, 0x80, 59, 0));
    CHECK(led_rgb[3] == GREEN_ON && led_rgb[0] == GREEN_DIM && led_rgb[4] == 0);
    tap(PLAY);

    /* the memory holds 48 steps; recording ends by itself when it is full */
    shift_tap(RECORD, TM_CONSUME);
    tap(RECORD);
    for (int i = 0; i < 48; i++)
        key(i % 25, 30 + i);
    seq_mode_poll();
    CHECK(!strcmp(field[1], "48: Note 77"));
    for (int i = 0; i < 16; i++)
        CHECK(led_rgb[i] == GREEN_DIM);                           /* no free step to point at */
    key(0, 99);
    seq_mode_poll();
    CHECK(!strcmp(field[1], "48 steps"));
    key(0, 99);
    tap(PLAY);
    seq_mode_poll();
    CHECK(!strcmp(field[1], "Play"));
    n_sent = 0;
    for (int i = 0; i < 48; i++)
        clock_on(0);
    CHECK(n_sent == 95);
    tap(PLAY);

    /* the three sequences are independent */
    set_feat(PAD_MODE, 6);
    seq_mode_poll();
    CHECK(!strcmp(field[1], "2 steps, play") && led_rgb[2] == 0);
    set_feat(PAD_MODE, 7);
    seq_mode_poll();
    CHECK(!strcmp(field[1], "1 step") && led_rgb[0] == GREEN_DIM && led_rgb[1] == 0);

    printf("seq_mode: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
