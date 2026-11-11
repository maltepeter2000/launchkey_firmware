/* SH-101 style step sequencer (replaces pad mode Custom 3).
 *
 * Active while feature param 0x1D (pad mode) = 7:
 *   Record       starts recording (the old sequence is cleared); pressed again it ends
 *                recording. The steps entered so far are the sequence.
 *   keys         while recording every key press stores its note as the next step (the key
 *                still plays as usual)
 *   Shift + Oct- while recording: stores a rest
 *   Shift + Oct+ while recording: stores a tie (the previous note keeps sounding)
 *   Play         starts / stops playback (ends recording)
 *   clock        MIDI note SEQ_CLOCK_NOTE received on the MIDI port (USB): every note on
 *                advances one step, its note off ends the gate (not before a tie)
 *   pads         16 steps per page (page follows the current step): green note, blue rest,
 *                amber tie; bright = step playing, red = next step to record
 * Notes go out on the MIDI port (USB + DIN/TRS) on the Keys Channel. The sequence lives in
 * RAM only. Leaving the pad mode stops playback and recording.
 *
 * Record, Play and (with Shift, while recording) Oct-/Oct+ are taken in the io dispatchers;
 * the pads do nothing in this mode. Key notes come from the stock note-out listener list
 * (seq_note_out), the clock from the hook in the MIDI port's message handler (seq_midi_rx).
 * Both only change state; pads and display are updated from the main loop (seq_mode_poll).
 */
#include <stdint.h>
#include "fw_api.h"
#include "seq_core.h"
#include "seq_mode.h"
#include "tm_mode.h"

#define SEQ_CLOCK_NOTE    24          /* C0 (middle C = C3 = 60), any channel */
#define SEQ_VELOCITY      100

#define FEAT_PAD_MODE     0x1D
#define FEAT_SHIFT        0x3F
#define FEAT_KEYS_CHANNEL 0x64        /* Settings > Keys Channel, 0-based */
#define PAD_CUSTOM3       7

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

enum { SHOW_NONE, SHOW_MODE, SHOW_RECORD, SHOW_STEP, SHOW_LENGTH, SHOW_PLAY, SHOW_STOP, SHOW_EMPTY };

static seq_t seq;
static struct {
    uint8_t active, shift;
    uint8_t priority;       /* pad Custom 3 (re)selected: our pads win over a stale menu capture */
    uint8_t key_pending;    /* a key was pressed while recording: its first note is the step */
    uint8_t redraw;         /* pads need drawing (seq_mode_poll) */
    uint8_t show;           /* popup to show (seq_mode_poll), SHOW_* */
    uint8_t channel;        /* channel of the sounding note */
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

static void send(const seq_out_t *out)
{
    midi_port_t *port = g_app_ports[MIDI_PORT];
    if (!port)
        return;
    if (out->off >= 0)
        midi_send3(port, 0x80 | st.channel, (uint32_t)out->off, 0);
    if (out->on >= 0) {
        st.channel = (uint8_t)(feat_get(FEAT_KEYS_CHANNEL) & 0x0F);
        midi_send3(port, 0x90 | st.channel, (uint32_t)out->on, SEQ_VELOCITY);
    }
}

/* Step the pads centre on: the next step while recording, the playing step otherwise. */
static int cursor(void)
{
    if (seq.state == SEQ_RECORDING)
        return seq.len;
    if (seq.state == SEQ_PLAYING && seq.pos >= 0)
        return seq.pos;
    return 0;
}

static void draw(void)
{
    int cur = cursor();
    int first = cur / PAD_COUNT * PAD_COUNT;
    for (int i = 0; i < PAD_COUNT; i++) {
        if (st.menu_pads >> i & 1)
            continue;           /* left to the menu; redrawn when it lets go */
        int idx = first + i;
        int lit = idx == cur && seq.state != SEQ_STOPPED;
        uint32_t rgb = LED_OFF;
        if (idx < seq.len) {
            uint8_t v = seq.step[idx];
            if (v == SEQ_REST)
                rgb = lit ? REST_ON : REST_DIM;
            else if (v == SEQ_TIE)
                rgb = lit ? TIE_ON : TIE_DIM;
            else
                rgb = lit ? NOTE_ON : NOTE_DIM;
        } else if (lit) {
            rgb = REC_CURSOR;   /* only recording has a cursor behind the last step */
        }
        led_override_color((uint32_t)i, rgb);
        led_override((uint32_t)i, LED_STATIC);
    }
}

static void show(int what)
{
    char text[20], *p = text;
    const char *title = "SH-101 Seq";
    switch (what) {
    case SHOW_RECORD:
        title = "Record";
        p = put_str(p, "Step 1");
        break;
    case SHOW_STEP: {
        title = "Record";
        if (!seq.len)
            return;
        uint8_t v = seq.step[seq.len - 1];
        p = put_str(put_num(p, seq.len), ": ");
        if (v == SEQ_REST)
            p = put_str(p, "Rest");
        else if (v == SEQ_TIE)
            p = put_str(p, "Tie");
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
    default:                    /* SHOW_MODE, SHOW_LENGTH */
        p = put_str(put_num(p, seq.len), " steps");
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

static void set_active(int on)
{
    if (on == st.active)
        return;
    st.active = (uint8_t)on;
    if (on) {
        update(SHOW_MODE);
    } else {
        seq_out_t out;
        seq_stop(&seq, &out);
        send(&out);
        st.key_pending = 0;
        st.redraw = 0;
        st.show = SHOW_NONE;
        for (uint32_t id = 0; id < PAD_COUNT; id++)
            led_override(id, LED_RELEASE);
    }
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
    if (!st.active)
        return 0;
    if (id < PAD_COUNT)
        return !st.shift && !menu_owns(id);
    if (id == CTL_PLAY || id == CTL_RECORD)
        return !st.shift;       /* Shift + Play / Record keep their stock functions */
    return st.shift && seq.state == SEQ_RECORDING;     /* Shift + Oct-/Oct+ */
}

static void pressed(uint32_t id)
{
    seq_out_t out;
    switch (id) {
    case CTL_RECORD:
        if (seq.state == SEQ_RECORDING) {
            seq_record_stop(&seq);
            update(SHOW_LENGTH);
        } else {
            seq_record_start(&seq, &out);
            send(&out);
            update(SHOW_RECORD);
        }
        break;
    case CTL_PLAY:
        if (seq.state == SEQ_PLAYING) {
            seq_stop(&seq, &out);
            send(&out);
            update(SHOW_STOP);
        } else {
            update(seq_play_start(&seq, &out) ? SHOW_PLAY : SHOW_EMPTY);
            send(&out);
        }
        break;
    case CTL_OCT_DOWN:
    case CTL_OCT_UP:
        if (seq_record_add(&seq, id == CTL_OCT_DOWN ? SEQ_REST : SEQ_TIE))
            update(SHOW_STEP);
        break;
    default:                    /* pads: no function */
        break;
    }
}

/* list: 0 = press (A), 1 = B, 2 = release (C), 3 = D. Returns TM_PASS or TM_CONSUME. */
int seq_io_event(int list, uint32_t id)
{
    if (id >= KEY_FIRST) {
        if (list == 0)
            st.key_pending = st.active && seq.state == SEQ_RECORDING;
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

/* Stock note-out listener: (control, group, number, value) for every message a control
 * sends; group < 0x10 = note on that channel, value = velocity (0 = note off). */
void seq_note_out(uint32_t ctl, uint32_t group, uint32_t number, uint32_t value)
{
    (void)ctl;
    if (!st.key_pending || !value || (group & 0xF0))
        return;
    st.key_pending = 0;         /* one step per key press (chords: first note) */
    if (seq_record_add(&seq, (uint8_t)(number & 0x7F)))
        update(seq.state == SEQ_RECORDING ? SHOW_STEP : SHOW_LENGTH);
}

/* Channel message received on the MIDI port. Returns 1 if it was the clock note. */
int seq_midi_rx(uint32_t status, uint32_t d1, uint32_t d2)
{
    uint32_t kind = status & 0xF0;
    if (!st.active || (kind != 0x90 && kind != 0x80) || d1 != SEQ_CLOCK_NOTE)
        return 0;
    seq_out_t out;
    if (kind == 0x90 && d2) {
        seq_clock_on(&seq, &out);
        if (seq.state == SEQ_PLAYING)
            st.redraw = 1;
    } else {
        seq_clock_off(&seq, &out);
    }
    send(&out);
    return 1;
}

void seq_feat_changed(uint32_t id, uint32_t value)
{
    switch (id) {
    case FEAT_PAD_MODE:
        set_active(value == PAD_CUSTOM3);
        if (value == PAD_CUSTOM3) {     /* (re)selected: take the pads back from any menu */
            st.priority = 1;
            st.menu_pads = 0;
            st.redraw = 1;
        }
        break;
    case FEAT_SHIFT:
        st.shift = value != 0;
        if (!st.shift && st.active)     /* the Shift overlay uses the LED override layer too */
            st.redraw = 1;
        break;
    }
}

void seq_mode_init(void)
{
    seq_init(&seq);
}

/* Main loop (after input handling). */
void seq_mode_poll(void)
{
    static uint8_t started;
    if (!started) {
        started = 1;
        set_active(feat_get(FEAT_PAD_MODE) == PAD_CUSTOM3);
    }
    if (!st.active)
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
