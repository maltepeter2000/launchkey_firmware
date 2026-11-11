/* SH-101 style step sequencers on pad modes Custom 1, 2 and 3: three independent
 * sequencers with up to 4 notes per step and SEQ_STEPS steps each.
 *
 * Sequencer n (1..3) plays on MIDI channel n and is clocked on MIDI channel n. It keeps
 * playing in the background when another pad mode is selected. The pad mode (feature param
 * 0x1D = 5 / 6 / 7) chooses the sequencer the controls and pads belong to:
 *   Record         starts recording at step 1 / ends it. The sequence is kept: entries
 *                  overwrite its steps and make it longer once they pass its end
 *   Shift + Record empties the sequence
 *   keys           while recording every key press stores its note as the next step (the
 *                  key still plays as usual). Keys pressed while another key is still held
 *                  join that step as a chord; the next step starts once all are released
 *   Shift + Oct-   while recording: stores a rest
 *   Shift + Oct+   while recording: stores a tie (the previous notes keep sounding)
 *   pads           16 steps per page (page follows the current step): green note, blue rest,
 *                  amber tie; bright = step playing, red = step the next entry goes to.
 *                  While recording a pad press moves the record position to its step
 *   Play           starts / stops playback (ends recording)
 *   clock          MIDI note SEQ_CLOCK_NOTE: every note on advances one step, its note off
 *                  ends the gate (not before a tie). Received on the MIDI port (USB) on the
 *                  sequencer's channel, or played on the keys for the selected sequencer
 * Notes go out on the MIDI port (USB + DIN/TRS). The sequences live in RAM only. Leaving a
 * sequencer's pad mode ends its recording; its playback goes on.
 *
 * Record, Play, the pads and (with Shift, while recording) Oct-/Oct+ are taken in the io
 * dispatchers. Key notes come from the stock note-out listener list (seq_note_out), the
 * clock from the hook in the MIDI port's message handler (seq_midi_rx). Both only change
 * state; pads and display are updated from the main loop (seq_mode_poll).
 */
#include <stdint.h>
#include "fw_api.h"
#include "seq_core.h"
#include "seq_mode.h"
#include "tm_mode.h"

#define SEQ_COUNT         3           /* pad modes Custom 1..3 = MIDI channels 1..3 */
#define SEQ_STEPS         48
#define SEQ_CLOCK_NOTE    24          /* C0 (middle C = C3 = 60) */
#define SEQ_VELOCITY      100

#define FEAT_PAD_MODE     0x1D
#define FEAT_SHIFT        0x3F
#define PAD_CUSTOM1       5           /* feature 0x1D: 5..8 = pad Custom 1..4 */

#define PAD_COUNT         16
#define CTL_SHIFT         25
#define CTL_OCT_DOWN      0x20
#define CTL_OCT_UP        0x21
#define CTL_PLAY          0x48
#define CTL_RECORD        0x4A
#define ENC_FIRST         56
#define ENC_LAST          63
#define KEY_FIRST         0x80        /* keybed: control ids 0x80.. */
#define MIDI_PORT         1           /* g_app_ports[1] = USB MIDI port (+ DIN via link) */

#define NOTE_ON           0x00FF00
#define NOTE_DIM          0x001800
#define REST_ON           0x0000FF
#define REST_DIM          0x000018
#define TIE_ON            0xFFA000
#define TIE_DIM           0x181000
#define REC_CURSOR        0xFF0000
#define LED_OFF           0x000000

/* slots in st.owned: pads 0..15, then the buttons */
enum { SLOT_PLAY = PAD_COUNT, SLOT_RECORD, SLOT_OCT_DOWN, SLOT_OCT_UP };

enum { SHOW_NONE, SHOW_MODE, SHOW_RECORD, SHOW_STEP, SHOW_LENGTH, SHOW_PLAY, SHOW_STOP,
       SHOW_EMPTY, SHOW_CLEARED };

static uint8_t mem[SEQ_COUNT][SEQ_STEPS * SEQ_VOICES_MAX];
static seq_t seqs[SEQ_COUNT];
static seq_t *seq;          /* sequencer of the selected pad mode, NULL = none */
static struct {
    uint8_t shift;
    uint8_t priority;       /* pad mode (re)selected: our pads win over a stale menu capture */
    uint8_t held;           /* keys currently down */
    uint8_t chord_open;     /* notes join the step entered last until all keys are up */
    uint8_t redraw;         /* pads need drawing (seq_mode_poll) */
    uint8_t show;           /* popup to show (seq_mode_poll), SHOW_* */
    uint16_t menu_pads;     /* pads currently captured by a menu (not drawn) */
    uint32_t owned;         /* controls whose current press belongs to this mode (slot bits) */
} st;

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

static uint32_t feat_get(uint32_t id)
{
    fw_param_t p;
    confsys_get_param(FW_GROUP_FEATURE, id, &p);
    return p.value;
}

static int menu_owns(uint32_t id)
{
    return !st.priority && ctl_is_blocked(id);
}

/* ---- output ---- */

static void send(const seq_t *s, const seq_out_t *out)
{
    midi_port_t *port = g_app_ports[MIDI_PORT];
    uint32_t channel = (uint32_t)(s - seqs);
    if (!port)
        return;
    for (int i = 0; i < out->n_off; i++)
        midi_send3(port, 0x80 | channel, out->off[i], 0);
    for (int i = 0; i < out->n_on; i++)
        midi_send3(port, 0x90 | channel, out->on[i], SEQ_VELOCITY);
}

/* Step the pads centre on: the record position while recording, the playing step otherwise. */
static int cursor(void)
{
    if (seq->state == SEQ_RECORDING)
        return seq->rec;
    if (seq->state == SEQ_PLAYING && seq->pos >= 0)
        return seq->pos;
    return 0;
}

/* First step of the page the pads show. */
static int page_first(void)
{
    int cur = cursor();         /* = max while recording with a full memory */
    return (cur < seq->max ? cur : seq->max - 1) / PAD_COUNT * PAD_COUNT;
}

static void draw(void)
{
    int cur = cursor();
    int first = page_first();
    for (int i = 0; i < PAD_COUNT; i++) {
        if (st.menu_pads >> i & 1)
            continue;           /* left to the menu; redrawn when it lets go */
        int idx = first + i;
        uint32_t rgb = LED_OFF;
        if (idx == cur && seq->state == SEQ_RECORDING) {
            if (idx < seq->max)
                rgb = REC_CURSOR;
        } else if (idx < seq->len) {
            int lit = idx == cur && seq->state == SEQ_PLAYING;
            uint8_t v = seq_step_kind(seq, idx);
            if (v == SEQ_REST)
                rgb = lit ? REST_ON : REST_DIM;
            else if (v == SEQ_TIE)
                rgb = lit ? TIE_ON : TIE_DIM;
            else
                rgb = lit ? NOTE_ON : NOTE_DIM;
        }
        led_override_color((uint32_t)i, rgb);
        led_override((uint32_t)i, LED_STATIC);
    }
}

static void show(int what)
{
    char title[20], text[20], *p = text;
    *put_num(put_str(title, "SH-101 Seq "), (uint32_t)(seq - seqs) + 1) = 0;
    switch (what) {
    case SHOW_RECORD:           /* record position: after Record or a pad */
        p = put_num(put_str(p, "Rec step "), (uint32_t)seq->rec + 1);
        break;
    case SHOW_STEP: {           /* the step entered last */
        if (!seq->rec)
            return;
        uint8_t v = seq_step_kind(seq, seq->rec - 1);
        int notes = seq_step_notes(seq, seq->rec - 1, 0);
        p = put_str(put_num(p, seq->rec), ": ");
        if (v == SEQ_REST)
            p = put_str(p, "Rest");
        else if (v == SEQ_TIE)
            p = put_str(p, "Tie");
        else if (notes > 1)
            p = put_str(put_num(p, (uint32_t)notes), " notes");
        else
            p = put_num(put_str(p, "Note "), v);
        break;
    }
    case SHOW_PLAY:
        p = put_str(p, "Play");
        break;
    case SHOW_STOP:
        p = put_str(p, "Stop");
        break;
    case SHOW_EMPTY:
        p = put_str(p, "Empty");
        break;
    case SHOW_CLEARED:
        p = put_str(p, "Cleared");
        break;
    default:                    /* SHOW_MODE, SHOW_LENGTH */
        p = put_str(put_num(p, seq->len), seq->len == 1 ? " step" : " steps");
        if (what == SHOW_MODE && seq->state == SEQ_PLAYING)
            p = put_str(p, ", play");
        break;
    }
    *p = 0;
    ui_popup_begin();
    disp_set_screen(1, 0);
    disp_text(1, 0, title);
    disp_text(1, 1, text);
}

static void update(int what)
{
    st.show = (uint8_t)what;
    st.redraw = 1;
}

/* ---- mode switching ---- */

static void set_mode(uint32_t pad_mode)
{
    seq_t *next = pad_mode >= PAD_CUSTOM1 && pad_mode < PAD_CUSTOM1 + SEQ_COUNT
                      ? &seqs[pad_mode - PAD_CUSTOM1] : 0;
    if (next == seq)
        return;
    if (seq)
        seq_record_stop(seq);   /* its keys are gone; playback goes on in the background */
    st.chord_open = 0;
    st.redraw = 0;
    st.show = SHOW_NONE;
    if (!next)
        for (uint32_t id = 0; id < PAD_COUNT; id++)
            led_override(id, LED_RELEASE);
    seq = next;
    if (seq)
        update(SHOW_MODE);
}

/* ---- input (called from cfw_init.c) ---- */

static int ctl_slot(uint32_t id)
{
    if (id < PAD_COUNT)
        return (int)id;
    switch (id) {
    case CTL_PLAY:     return SLOT_PLAY;
    case CTL_RECORD:   return SLOT_RECORD;
    case CTL_OCT_DOWN: return SLOT_OCT_DOWN;
    case CTL_OCT_UP:   return SLOT_OCT_UP;
    default:           return -1;
    }
}

/* This press belongs to the sequencer. */
static int wants(uint32_t id)
{
    if (!seq)
        return 0;
    if (id < PAD_COUNT)
        return !st.shift && !menu_owns(id);
    if (id == CTL_RECORD)
        return 1;               /* with Shift: empty the sequence */
    if (id == CTL_PLAY)
        return !st.shift;       /* Shift + Play keeps its stock function */
    return st.shift && seq->state == SEQ_RECORDING;    /* Shift + Oct-/Oct+ */
}

static void pressed(uint32_t id)
{
    seq_out_t out;
    st.chord_open = 0;
    switch (id) {
    case CTL_RECORD:
        if (st.shift) {
            seq_clear(seq, &out);
            send(seq, &out);
            update(SHOW_CLEARED);
        } else if (seq->state == SEQ_RECORDING) {
            seq_record_stop(seq);
            update(SHOW_LENGTH);
        } else {
            seq_record_start(seq, &out);
            send(seq, &out);
            st.held = 0;        /* keys still down do not count for chords */
            update(SHOW_RECORD);
        }
        break;
    case CTL_PLAY:
        if (seq->state == SEQ_PLAYING) {
            seq_stop(seq, &out);
            send(seq, &out);
            update(SHOW_STOP);
        } else {
            update(seq_play_start(seq, &out) ? SHOW_PLAY : SHOW_EMPTY);
            send(seq, &out);
        }
        break;
    case CTL_OCT_DOWN:
    case CTL_OCT_UP:
        update(seq_record_add(seq, id == CTL_OCT_DOWN ? SEQ_REST : SEQ_TIE) ? SHOW_STEP : SHOW_LENGTH);
        break;
    default:                    /* pads: while recording, record from this step on */
        if (seq_record_seek(seq, page_first() + (int)id))
            update(SHOW_RECORD);
        break;
    }
}

/* list: 0 = press (A), 1 = B, 2 = release (C), 3 = D. Returns TM_PASS or TM_CONSUME. */
int seq_io_event(int list, uint32_t id)
{
    if (id >= KEY_FIRST) {
        if (list == 0) {
            if (!st.held)
                st.chord_open = 0;
            if (st.held < 255)
                st.held++;
        } else if (list == 2) {
            if (st.held)
                st.held--;
            if (!st.held)
                st.chord_open = 0;      /* all keys up: the next note is a new step */
        }
        return TM_PASS;         /* the key plays as usual; seq_note_out gets its note */
    }
    int slot = ctl_slot(id);
    if (slot < 0) {
        if (list == 0 && id != CTL_SHIFT && (id < ENC_FIRST || id > ENC_LAST))
            st.priority = 0;    /* any other button may open a menu: menus win again */
        return TM_PASS;
    }
    uint32_t bit = 1u << slot;
    if (list == 0) {            /* the press decides who owns the control until its release */
        int take = wants(id);
        st.owned = take ? st.owned | bit : st.owned & ~bit;
        if (take)
            pressed(id);
    }
    if (!(st.owned & bit))
        return TM_PASS;
    if (list == 2)
        st.owned &= ~bit;
    return TM_CONSUME;
}

/* Clock note on (next step) / off (gate end) for one sequencer. */
static void clock(seq_t *s, int on)
{
    seq_out_t out;
    if (on) {
        seq_clock_on(s, &out);
        if (s == seq)
            st.redraw = 1;
    } else {
        seq_clock_off(s, &out);
    }
    send(s, &out);
}

/* Stock note-out listener: (control, group, number, value) for every message a control
 * sends; group < 0x10 = note on that channel, value = velocity (0 = note off). */
void seq_note_out(uint32_t ctl, uint32_t group, uint32_t number, uint32_t value)
{
    if (!seq || ctl < KEY_FIRST || (group & 0xF0))
        return;
    number &= 0x7F;
    if (seq->state == SEQ_PLAYING) {
        if (number == SEQ_CLOCK_NOTE)
            clock(seq, value != 0);     /* the clock note played on the keys */
        return;
    }
    if (seq->state != SEQ_RECORDING || !value)
        return;
    if (st.chord_open) {        /* joins the step of the keys still held */
        if (seq_record_add_note(seq, (uint8_t)number))
            update(SHOW_STEP);
    } else {
        st.chord_open = (uint8_t)seq_record_add(seq, (uint8_t)number);
        update(st.chord_open ? SHOW_STEP : SHOW_LENGTH);
    }
}

/* Channel message received on the MIDI port. Returns 1 if it was the clock note of a
 * playing sequencer (channel n clocks sequencer n, whatever pad mode is selected). */
int seq_midi_rx(uint32_t status, uint32_t d1, uint32_t d2)
{
    uint32_t kind = status & 0xF0, channel = status & 0x0F;
    if ((kind != 0x90 && kind != 0x80) || d1 != SEQ_CLOCK_NOTE || channel >= SEQ_COUNT)
        return 0;
    seq_t *s = &seqs[channel];
    if (s->state != SEQ_PLAYING)
        return 0;
    clock(s, kind == 0x90 && d2);
    return 1;
}

void seq_feat_changed(uint32_t id, uint32_t value)
{
    switch (id) {
    case FEAT_PAD_MODE:
        set_mode(value);
        if (seq) {                      /* (re)selected: take the pads back from any menu */
            st.priority = 1;
            st.menu_pads = 0;
            st.redraw = 1;
        }
        break;
    case FEAT_SHIFT:
        st.shift = value != 0;
        if (!st.shift && seq)           /* the Shift overlay uses the LED override layer too */
            st.redraw = 1;
        break;
    }
}

void seq_mode_init(void)
{
    for (int i = 0; i < SEQ_COUNT; i++)
        seq_init(&seqs[i], mem[i], SEQ_STEPS, SEQ_VOICES_MAX);
}

/* Main loop (after input handling). */
void seq_mode_poll(void)
{
    static uint8_t started;
    if (!started) {
        started = 1;
        set_mode(feat_get(FEAT_PAD_MODE));
    }
    if (!seq)
        return;
    uint16_t menu = 0;
    for (uint32_t id = 0; id < PAD_COUNT; id++)
        if (menu_owns(id))
            menu |= (uint16_t)(1u << id);
    if (menu != st.menu_pads) {
        if (st.menu_pads & (uint16_t)~menu)
            st.redraw = 1;
        st.menu_pads = menu;
    }
    if (st.redraw && !st.shift) {       /* the Shift overlay owns the pad LEDs while held */
        st.redraw = 0;
        draw();
    }
    if (st.show) {
        show(st.show);
        st.show = SHOW_NONE;
    }
}
