/* SH-101 style step sequencers on pad modes Custom 1, 2 and 3: three independent
 * sequencers with SEQ_STEPS steps; a step holds up to 4 notes, an accent and one value for
 * each of three controllers.
 *
 * Sequencer n (1..3) plays on MIDI channel n. It keeps playing in the background when
 * another pad mode is selected. The pad mode (feature param 0x1D = 5 / 6 / 7) chooses the
 * sequencer the controls, encoders and pads belong to:
 *   Record         starts recording at step 1 / ends it. The sequence is kept: entries
 *                  replace the notes of its steps and make it longer once they pass its end
 *   keys           while recording every key press stores its note as the next step (the
 *                  key still plays as usual). Keys pressed while another key is still held
 *                  join that step as a chord; the next step starts once all are released
 *   Shift + Oct-   while recording: stores a rest
 *   Shift + Oct+   while recording: stores a tie (the previous notes keep sounding)
 *   Shift + Play   while recording: accent on / off for the edit step (velocity 127)
 *   Shift + Record while recording: empties the sequence; otherwise: clock source
 *                  (clock note <-> MIDI clock)
 *   pads           16 steps per page (page follows the current step): yellow note, dark
 *                  yellow tie, orange accented note, dark red accented tie, off = rest;
 *                  white = step playing, pulsing = record position. While recording a pad
 *                  press moves the record position to its step
 *   Play           starts / stops playback (ends recording). Sends MIDI Start when the
 *                  first sequencer starts and MIDI Stop when the last one stops
 *   encoder 1      MIDI clock rate: /7 .. /1.5, x1 (= 1/16), x1.5 .. x7
 *   encoder 2      direction: forward, reverse, alternate, random
 *   encoder 3      accent amount: plain notes play at velocity 127 - amount (100..120)
 *   encoder 4, 5   first and last step played (the last step may lie beyond the steps
 *                  entered: those are rests)
 *   encoder 6..8   controller lanes: sends the controller; while recording the value is
 *                  also stored in the edit step and the pads show the lane in blue. With
 *                  Shift: controller number
 * The edit step is the step entered last, or the step chosen with a pad / at record start.
 *
 * Clock: either MIDI note SEQ_CLOCK_NOTE (every note on advances one step, its note off ends
 * the gate, not before a tie; received on the MIDI port (USB) on the sequencer's channel, or
 * played on the keys for the selected sequencer), or the 24 PPQN clock counter (internal
 * tempo or external MIDI clock) divided by the rate, gate = half a step.
 * MIDI Start / Continue received on USB start every stopped sequencer that has a sequence,
 * MIDI Stop stops all (seq_transport, called from the hooks in cfw_init.c).
 * Notes go out on the MIDI port (USB + DIN/TRS). Sequences and settings are stored in the
 * settings store (one record per sequencer, see NOTES "Settings store"). Leaving a
 * sequencer's pad mode ends its recording; its playback goes on.
 *
 * Buttons, pads and encoders are taken in the io dispatchers. Key notes come from the stock
 * note-out listener list (seq_note_out), the clock note from the hook in the MIDI port's
 * message handler (seq_midi_rx). Both only change state; pads and display are updated from
 * the main loop (seq_mode_poll).
 */
#include <stdint.h>
#include "fw_api.h"
#include "seq_core.h"
#include "seq_mode.h"
#include "tm_mode.h"

#define SEQ_COUNT         3           /* pad modes Custom 1..3 = MIDI channels 1..3 */
#define SEQ_CLOCK_NOTE    24          /* C0 (middle C = C3 = 60) */
#define SEQ_ACCENT_VEL    127
#define AMOUNT_MIN        7           /* plain notes at 120 */
#define AMOUNT_MAX        27          /* plain notes at 100 */
#define STEP_TICKS        6           /* rate x1 = 1/16 note at 24 PPQN */
#define RATE_COUNT        15
#define RATE_X1           7
#define CC_VIEW_TICKS     48          /* pads show a controller lane this long after a turn */
#define STORE_KEY         0x5E9C0F01u /* settings-store keys: STORE_KEY + sequencer index */
#define STORE_DELAY_MS    3000

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

enum { ENC_RATE, ENC_DIR, ENC_ACCENT, ENC_START, ENC_LEN, ENC_CC };   /* ENC_CC.. = lanes */

#define NOTE_RGB          0xFFC000    /* yellow */
#define TIE_RGB           0x302400    /* dark yellow */
#define ACCENT_RGB        0xFF4000    /* orange */
#define ACCENT_TIE_RGB    0x500000    /* dark red */
#define PLAY_RGB          0xFFFFFF
#define CURSOR_RGB        0x404040
#define LED_OFF           0x000000

/* slots in st.owned: pads 0..15, the buttons, then the encoders */
enum { SLOT_PLAY = PAD_COUNT, SLOT_RECORD, SLOT_OCT_DOWN, SLOT_OCT_UP, SLOT_ENC };

enum { SHOW_NONE, SHOW_MODE, SHOW_RECORD, SHOW_STEP, SHOW_LENGTH, SHOW_PLAY, SHOW_STOP,
       SHOW_EMPTY, SHOW_CLEARED, SHOW_ACCENT, SHOW_SYNC, SHOW_ENC, SHOW_CC_VALUE,
       SHOW_CC_NUMBER };

/* MIDI clock rates: a step every STEP_TICKS * den / num clock ticks. */
static const uint8_t rate_tab[RATE_COUNT][2] = {
    {1, 7}, {1, 6}, {1, 5}, {1, 4}, {1, 3}, {1, 2}, {2, 3}, {1, 1},
    {3, 2}, {2, 1}, {3, 1}, {4, 1}, {5, 1}, {6, 1}, {7, 1},
};
static const char *const rate_names[RATE_COUNT] = {
    "/7", "/6", "/5", "/4", "/3", "/2", "/1.5", "x1", "x1.5", "x2", "x3", "x4", "x5", "x6", "x7",
};
static const char *const dir_names[SEQ_DIR_COUNT] = {"Forward", "Reverse", "Alternate", "Random"};

static seq_data_t data[SEQ_COUNT];
static seq_t seqs[SEQ_COUNT];
static struct {
    uint8_t acc;            /* MIDI clock: ticks * num since the last step */
    uint8_t gate;           /* the gate of the last step is still open */
} run[SEQ_COUNT];
static seq_t *seq;          /* sequencer of the selected pad mode, NULL = none */
static struct {
    uint8_t shift;
    uint8_t priority;       /* pad mode (re)selected: our controls win over a stale menu capture */
    uint8_t held;           /* keys currently down */
    uint8_t chord_open;     /* notes join the step entered last until all keys are up */
    uint8_t entered;        /* recording: a step was entered since the position was set */
    uint8_t redraw;         /* pads need drawing (seq_mode_poll) */
    uint8_t show;           /* popup to show (seq_mode_poll), SHOW_* */
    uint8_t show_arg;       /* its step / encoder / lane */
    uint8_t cc_view;        /* 1 + lane the pads show, 0 = steps */
    uint8_t cc_val[SEQ_LANES];  /* last value sent per lane */
    uint16_t menu_pads;     /* pads currently captured by a menu (not drawn) */
    uint32_t owned;         /* controls whose current press belongs to this mode (slot bits) */
    uint32_t last_clock;    /* g_clock_ticks seen last */
    uint32_t cc_until;      /* g_clock_ticks at which the lane view ends */
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

static int clamp(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static int index_of(const seq_t *s)
{
    return (int)(s - seqs);
}

/* The sequence or its settings changed: have them written to the settings store. */
static void dirty(const seq_t *s)
{
    confstore_mark_dirty(STORE_KEY + (uint32_t)index_of(s));
}

/* Limits the fields of a record the core does not look at. */
static void sanitize(seq_t *s)
{
    seq_data_t *d = s->d;
    seq_sanitize(s);
    d->sync = d->sync != 0;
    if (d->rate >= RATE_COUNT)
        d->rate = RATE_X1;
    d->amount = (uint8_t)clamp(d->amount, AMOUNT_MIN, AMOUNT_MAX);
    for (int i = 0; i < SEQ_LANES; i++)
        d->cc[i] &= 0x7F;
}

/* ---- output ---- */

static void send(const seq_t *s, const seq_out_t *out)
{
    midi_port_t *port = g_app_ports[MIDI_PORT];
    const seq_data_t *d = s->d;
    uint32_t channel = (uint32_t)index_of(s);
    if (!port)
        return;
    for (int i = 0; i < out->n_off; i++)
        midi_send3(port, 0x80 | channel, out->off[i], 0);
    for (int i = 0; i < SEQ_LANES; i++)
        if (out->cc[i] != SEQ_NONE)
            midi_send3(port, 0xB0 | channel, d->cc[i], out->cc[i]);
    for (int i = 0; i < out->n_on; i++)
        midi_send3(port, 0x90 | channel, out->on[i],
                   out->accent ? SEQ_ACCENT_VEL : (uint32_t)(SEQ_ACCENT_VEL - d->amount));
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
    int cur = cursor();         /* = SEQ_STEPS while recording with a full memory */
    return (cur < SEQ_STEPS ? cur : SEQ_STEPS - 1) / PAD_COUNT * PAD_COUNT;
}

/* Recording: the step accent and controller values go to, -1 = none. */
static int edit_step(void)
{
    if (seq->state != SEQ_RECORDING)
        return -1;
    int step = st.entered ? seq->rec - 1 : seq->rec;
    return step >= 0 && step < SEQ_STEPS ? step : -1;
}

static void draw(void)
{
    int first = page_first();
    for (int i = 0; i < PAD_COUNT; i++) {
        if (st.menu_pads >> i & 1)
            continue;           /* left to the menu; redrawn when it lets go */
        int idx = first + i;
        uint32_t rgb = LED_OFF, mode = LED_STATIC;
        if (idx >= SEQ_STEPS) {
            /* off */
        } else if (st.cc_view) {                /* a controller lane: brightness = value */
            uint8_t v = seq_cc(seq, idx, st.cc_view - 1);
            if (v != SEQ_NONE)
                rgb = 0x18 + v * 0xE7u / 127;
        } else if (seq->state == SEQ_RECORDING && idx == seq->rec) {
            rgb = CURSOR_RGB;
            mode = LED_PULSE;
        } else if (seq->state == SEQ_PLAYING && idx == seq->pos) {
            rgb = PLAY_RGB;
        } else if (idx < seq->d->len) {
            uint8_t v = seq_step_kind(seq, idx);
            int accent = seq_accent(seq, idx);
            if (v == SEQ_TIE)
                rgb = accent ? ACCENT_TIE_RGB : TIE_RGB;
            else if (v != SEQ_REST)
                rgb = accent ? ACCENT_RGB : NOTE_RGB;
        }
        led_override_color((uint32_t)i, rgb);
        led_override((uint32_t)i, mode);
    }
}

static void show(int what, int arg)
{
    const seq_data_t *d = seq->d;
    char title[20], text[24], *p = text;
    *put_num(put_str(title, "SH-101 Seq "), (uint32_t)index_of(seq) + 1) = 0;
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
    case SHOW_ACCENT:
        p = put_str(put_num(p, (uint32_t)arg + 1), seq_accent(seq, arg) ? ": Accent on" : ": Accent off");
        break;
    case SHOW_SYNC:
        p = put_str(p, d->sync ? "MIDI clock" : "Clock note C0");
        break;
    case SHOW_ENC:
        switch (arg) {
        case ENC_RATE:   p = put_str(put_str(p, "Clock "), rate_names[d->rate]); break;
        case ENC_DIR:    p = put_str(p, dir_names[d->dir]); break;
        case ENC_ACCENT: p = put_num(put_str(p, "Accent +"), d->amount); break;
        case ENC_START:  p = put_num(put_str(p, "First step "), (uint32_t)d->start + 1); break;
        default:         p = put_num(put_str(p, "Last step "), d->len); break;
        }
        break;
    case SHOW_CC_VALUE: {       /* arg = lane */
        int step = edit_step();
        p = put_num(put_str(p, "CC"), d->cc[arg]);
        if (step >= 0)
            p = put_num(put_str(p, " S"), (uint32_t)step + 1);
        p = put_num(put_str(p, ": "), st.cc_val[arg]);
        break;
    }
    case SHOW_CC_NUMBER:        /* arg = lane */
        p = put_num(put_str(put_num(put_str(p, "Knob "), (uint32_t)arg + ENC_CC + 1), ": CC "), d->cc[arg]);
        break;
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
        p = put_str(put_num(p, d->len), d->len == 1 ? " step" : " steps");
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

static void update_arg(int what, int arg)
{
    st.show = (uint8_t)what;
    st.show_arg = (uint8_t)arg;
    st.redraw = 1;
}

static void update(int what)
{
    update_arg(what, 0);
}

/* ---- clock ---- */

/* Clock on (next step) / off (gate end) for one sequencer. */
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

static int playing_count(void)
{
    int n = 0;
    for (int i = 0; i < SEQ_COUNT; i++)
        n += seqs[i].state == SEQ_PLAYING;
    return n;
}

/* MIDI Start / Stop (0xFA / 0xFC) on the MIDI port. */
static void send_realtime(uint32_t status)
{
    if (g_app_ports[MIDI_PORT])
        midi_send3(g_app_ports[MIDI_PORT], status, 0, 0);
}

/* Starts playback. With the MIDI clock the first step plays right away (now = 1) or with
 * the next clock tick (now = 0: started by a MIDI Start, whose first clock is the downbeat). */
static int play(seq_t *s, int now)
{
    seq_out_t out;
    int ok = seq_play_start(s, &out);
    send(s, &out);
    s->rng ^= (uint16_t)g_clock_ticks;
    if (!s->rng)
        s->rng = 1;
    if (ok && s->d->sync) {
        const uint8_t *rate = rate_tab[s->d->rate];
        run[index_of(s)].acc = now ? 0 : (uint8_t)(rate[1] * STEP_TICKS - rate[0]);
        run[index_of(s)].gate = (uint8_t)now;
        if (now)
            clock(s, 1);
    }
    return ok;
}

/* MIDI Start / Continue (start = 1) or Stop (0) was received: starts every sequencer that
 * has a sequence and is neither playing nor recording, or stops every one that plays. */
void seq_transport(int start)
{
    seq_out_t out;
    for (int i = 0; i < SEQ_COUNT; i++) {
        seq_t *s = &seqs[i];
        if (start ? s->state != SEQ_STOPPED || !play(s, 0) : s->state != SEQ_PLAYING)
            continue;
        if (!start) {
            seq_stop(s, &out);
            send(s, &out);
        }
        if (s == seq)
            update(start ? SHOW_PLAY : SHOW_STOP);
    }
}

/* Steps the sequencers that follow the MIDI clock (all of them, selected or not). */
static void run_clocks(void)
{
    uint32_t now = g_clock_ticks;
    uint32_t elapsed = now - st.last_clock;
    st.last_clock = now;
    if (elapsed > 384)          /* clock jumped (restart / long stall): resync */
        elapsed = 1;
    for (int i = 0; i < SEQ_COUNT; i++) {
        seq_t *s = &seqs[i];
        if (s->state != SEQ_PLAYING || !s->d->sync)
            continue;
        unsigned num = rate_tab[s->d->rate][0], period = rate_tab[s->d->rate][1] * STEP_TICKS;
        for (uint32_t n = elapsed; n; n--) {
            unsigned acc = run[i].acc + num;
            if (acc >= period) {
                do {
                    acc -= period;
                    clock(s, 1);
                } while (acc >= period);
                run[i].gate = 1;
            } else if (run[i].gate && acc * 2 >= period) {
                run[i].gate = 0;
                clock(s, 0);
            }
            run[i].acc = (uint8_t)acc;
        }
    }
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
    st.cc_view = 0;
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
    if (id >= ENC_FIRST && id <= ENC_LAST)
        return (int)(SLOT_ENC + id - ENC_FIRST);
    switch (id) {
    case CTL_PLAY:     return SLOT_PLAY;
    case CTL_RECORD:   return SLOT_RECORD;
    case CTL_OCT_DOWN: return SLOT_OCT_DOWN;
    case CTL_OCT_UP:   return SLOT_OCT_UP;
    default:           return -1;
    }
}

/* This press / turn belongs to the sequencer. */
static int wants(uint32_t id)
{
    if (!seq)
        return 0;
    if (id < PAD_COUNT)
        return !st.shift && !menu_owns(id);
    if (id >= ENC_FIRST && id <= ENC_LAST)      /* with Shift: controller numbers only */
        return !menu_owns(id) && (!st.shift || id >= ENC_FIRST + ENC_CC);
    if (id == CTL_RECORD)
        return 1;               /* with Shift: empty the sequence / clock source */
    if (id == CTL_PLAY)         /* Shift + Play: accent while recording, else stock */
        return !st.shift || seq->state == SEQ_RECORDING;
    return st.shift && seq->state == SEQ_RECORDING;    /* Shift + Oct-/Oct+ */
}

static void pressed(uint32_t id)
{
    seq_out_t out;
    st.chord_open = 0;
    st.cc_view = 0;
    switch (id) {
    case CTL_RECORD:
        if (st.shift && seq->state == SEQ_RECORDING) {
            seq_clear(seq, &out);
            send(seq, &out);
            st.entered = 0;
            dirty(seq);
            update(SHOW_CLEARED);
        } else if (st.shift) {
            seq->d->sync = !seq->d->sync;
            if (seq->state == SEQ_PLAYING)
                play(seq, 1);   /* from the start with the new clock */
            dirty(seq);
            update(SHOW_SYNC);
        } else if (seq->state == SEQ_RECORDING) {
            seq_record_stop(seq);
            update(SHOW_LENGTH);
        } else {
            seq_record_start(seq, &out);
            send(seq, &out);
            st.held = 0;        /* keys still down do not count for chords */
            st.entered = 0;
            update(SHOW_RECORD);
        }
        break;
    case CTL_PLAY:
        if (st.shift) {
            int step = edit_step();
            if (step >= 0) {
                seq_set_accent(seq, step, !seq_accent(seq, step));
                dirty(seq);
                update_arg(SHOW_ACCENT, step);
            }
        } else if (seq->state == SEQ_PLAYING) {
            seq_stop(seq, &out);
            send(seq, &out);
            if (!playing_count())
                send_realtime(0xFC);    /* the last one stopped: MIDI Stop */
            update(SHOW_STOP);
        } else {
            if (seq->d->len && !playing_count())
                send_realtime(0xFA);    /* the first one starts: MIDI Start */
            update(play(seq, 1) ? SHOW_PLAY : SHOW_EMPTY);
        }
        break;
    case CTL_OCT_DOWN:
    case CTL_OCT_UP:
        if (seq_record_add(seq, id == CTL_OCT_DOWN ? SEQ_REST : SEQ_TIE)) {
            st.entered = 1;
            dirty(seq);
            update(SHOW_STEP);
        } else {
            update(SHOW_LENGTH);
        }
        break;
    default:                    /* pads: while recording, record from this step on */
        if (seq_record_seek(seq, page_first() + (int)id)) {
            st.entered = 0;
            update(SHOW_RECORD);
        }
        break;
    }
}

/* list: 0 = press (A), 1 = B, 2 = release (C), 3 = D.
 * Returns TM_PASS, TM_CONSUME or (encoders) TM_ENC_ONLY. */
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
        if (list == 0 && id != CTL_SHIFT)
            st.priority = 0;    /* any other button may open a menu: menus win again */
        return TM_PASS;
    }
    uint32_t bit = 1u << slot;
    if (list == 0) {            /* the press decides who owns the control until its release */
        int take = wants(id);
        st.owned = take ? st.owned | bit : st.owned & ~bit;
        if (take && slot < SLOT_ENC)
            pressed(id);
    }
    if (!(st.owned & bit))
        return TM_PASS;
    if (list == 2)
        st.owned &= ~bit;
    return slot < SLOT_ENC ? TM_CONSUME : TM_ENC_ONLY;
}

/* list: 0 = pos (every tick), 1 = hires, 2 = step (per detent). Returns 1 if consumed. */
int seq_enc_event(int list, uint32_t id, int32_t delta)
{
    delta = (int16_t)delta;     /* the encoder module passes deltas as u16 (-1 = 0xFFFF) */
    if (id < ENC_FIRST || id > ENC_LAST || !wants(id))
        return 0;
    seq_data_t *d = seq->d;
    int enc = (int)(id - ENC_FIRST), lane = enc - ENC_CC;
    if (lane >= 0 && !st.shift) {       /* controller value: sent, and stored while recording */
        if (list != 0)
            return 1;
        int step = edit_step();
        uint8_t v = (uint8_t)clamp(st.cc_val[lane] + delta, 0, 127);
        if (v != st.cc_val[lane] && g_app_ports[MIDI_PORT])
            midi_send3(g_app_ports[MIDI_PORT], 0xB0 | (uint32_t)index_of(seq), d->cc[lane], v);
        st.cc_val[lane] = v;
        if (step >= 0) {
            seq_set_cc(seq, step, lane, v);
            dirty(seq);
            st.cc_view = (uint8_t)(lane + 1);
            st.cc_until = g_clock_ticks + CC_VIEW_TICKS;
        }
        update_arg(SHOW_CC_VALUE, lane);
        return 1;
    }
    if (list != 2)
        return 1;
    if (lane >= 0) {
        d->cc[lane] = (uint8_t)clamp(d->cc[lane] + delta, 0, 127);
        update_arg(SHOW_CC_NUMBER, lane);
    } else {
        switch (enc) {
        case ENC_RATE:   d->rate = (uint8_t)clamp(d->rate + delta, 0, RATE_COUNT - 1); break;
        case ENC_DIR:    seq_set_dir(seq, clamp(d->dir + delta, 0, SEQ_DIR_COUNT - 1)); break;
        case ENC_ACCENT: d->amount = (uint8_t)clamp(d->amount + delta, AMOUNT_MIN, AMOUNT_MAX); break;
        case ENC_START:  seq_set_start(seq, d->start + delta); break;
        default:         seq_set_len(seq, d->len + delta); break;
        }
        st.cc_view = 0;
        update_arg(SHOW_ENC, enc);
    }
    dirty(seq);
    return 1;
}

/* Stock note-out listener: (control, group, number, value) for every message a control
 * sends; group < 0x10 = note on that channel, value = velocity (0 = note off). */
void seq_note_out(uint32_t ctl, uint32_t group, uint32_t number, uint32_t value)
{
    if (!seq || ctl < KEY_FIRST || (group & 0xF0))
        return;
    number &= 0x7F;
    if (seq->state == SEQ_PLAYING) {
        if (number == SEQ_CLOCK_NOTE && !seq->d->sync)
            clock(seq, value != 0);     /* the clock note played on the keys */
        return;
    }
    if (seq->state != SEQ_RECORDING || !value)
        return;
    st.cc_view = 0;
    if (st.chord_open) {        /* joins the step of the keys still held */
        if (seq_record_add_note(seq, (uint8_t)number)) {
            dirty(seq);
            update(SHOW_STEP);
        }
    } else {
        st.chord_open = (uint8_t)seq_record_add(seq, (uint8_t)number);
        if (st.chord_open) {
            st.entered = 1;
            dirty(seq);
        }
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
    if (s->state != SEQ_PLAYING || s->d->sync)
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

/* Settings store: the records have been read into data[]. */
static void store_loaded(void)
{
    for (int i = 0; i < SEQ_COUNT; i++)
        sanitize(&seqs[i]);
    st.redraw = 1;
}

/* Called at feature-init time, before the settings store is read. */
void seq_mode_init(void)
{
    for (int i = 0; i < SEQ_COUNT; i++) {
        seq_data_t *d = &data[i];
        seq_init(&seqs[i], d, (uint16_t)(0xACE1 + 0x1357 * i));
        d->rate = RATE_X1;
        d->amount = AMOUNT_MAX;
        d->cc[0] = 74;                  /* cutoff, resonance, modulation */
        d->cc[1] = 71;
        d->cc[2] = 1;
        confstore_register(STORE_KEY + (uint32_t)i, d, sizeof *d, STORE_DELAY_MS);
    }
    confstore_add_loaded_cb(store_loaded);
}

/* Main loop (after input handling). */
void seq_mode_poll(void)
{
    static uint8_t started;
    if (!started) {
        started = 1;
        st.last_clock = g_clock_ticks;
        set_mode(feat_get(FEAT_PAD_MODE));
    }
    run_clocks();
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
    if (st.cc_view && (int32_t)(g_clock_ticks - st.cc_until) >= 0) {
        st.cc_view = 0;
        st.redraw = 1;
    }
    if (st.redraw && !st.shift) {       /* the Shift overlay owns the pad LEDs while held */
        st.redraw = 0;
        draw();
    }
    if (st.show) {
        show(st.show, st.show_arg);
        st.show = SHOW_NONE;
    }
}
