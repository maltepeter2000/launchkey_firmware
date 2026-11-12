/* Step sequencer core (SH-101 style step entry): step memory, recording and clocked
 * playback with up to SEQ_VOICES notes, an accent and SEQ_LANES controller values per step.
 * Pure C, no firmware dependencies, so it is unit-tested on the host
 * (cfw/test/test_seq_core.c). */
#ifndef SEQ_CORE_H
#define SEQ_CORE_H

#include <stdint.h>

#define SEQ_VOICES     4
#define SEQ_LANES      3
#define SEQ_STEP_BYTES (SEQ_VOICES + SEQ_LANES)
#define SEQ_STEPS      32       /* step capacity (accent bits: 4 bytes) */

#define SEQ_REST       0x80     /* first slot of a step: 0..127 = note, SEQ_REST, SEQ_TIE */
#define SEQ_TIE        0x81     /* the previous notes keep sounding through this step */
#define SEQ_NONE       0xFF     /* further note slots and controller slots: value or SEQ_NONE */

enum { SEQ_STOPPED, SEQ_RECORDING, SEQ_PLAYING };
enum { SEQ_DIR_FORWARD, SEQ_DIR_REVERSE, SEQ_DIR_ALTERNATE, SEQ_DIR_RANDOM, SEQ_DIR_COUNT };

/* Everything that is kept at power-off: one settings-store record (<= 248 bytes). The core
 * uses len, start, dir, accent and steps; the other fields belong to the mode (seq_mode.c). */
typedef struct {
    uint8_t len;                /* steps of the sequence, 0 = empty */
    uint8_t start;              /* first step played */
    uint8_t dir;                /* SEQ_DIR_* */
    uint8_t sync;               /* 0 = clock note, 1 = MIDI clock */
    uint8_t rate;               /* MIDI clock: index of the division */
    uint8_t amount;             /* accent amount: plain notes play at 127 - amount */
    uint8_t cc[SEQ_LANES];      /* controller numbers of the lanes */
    uint8_t accent[SEQ_STEPS / 8];
    uint8_t steps[SEQ_STEPS * SEQ_STEP_BYTES];  /* per step: SEQ_VOICES notes, SEQ_LANES values */
} seq_data_t;

_Static_assert(sizeof(seq_data_t) <= 248, "one settings-store record");

typedef struct {
    seq_data_t *d;
    uint8_t state;              /* SEQ_STOPPED / SEQ_RECORDING / SEQ_PLAYING */
    uint8_t rec;                /* recording: step the next entry goes to */
    int8_t  pos;                /* playing: step of the last clock, -1 = waiting for the first */
    int8_t  next;               /* playing: step of the next clock */
    uint8_t down;               /* alternate: currently running backwards */
    uint8_t n_sounding;         /* notes the sequencer is sounding */
    uint8_t sounding[SEQ_VOICES];
    uint16_t rng;
} seq_t;

/* What the caller has to send after a call: the offs, the controllers, then the ons. */
typedef struct {
    uint8_t n_off, n_on;
    uint8_t accent;             /* the ons are accented */
    uint8_t off[SEQ_VOICES], on[SEQ_VOICES];
    uint8_t cc[SEQ_LANES];      /* value per lane or SEQ_NONE */
} seq_out_t;

/* Sets up the sequencer with an empty sequence in d. */
void seq_init(seq_t *s, seq_data_t *d, uint16_t seed);
/* Makes d consistent (after it was loaded from the settings store). */
void seq_sanitize(seq_t *s);

/* Empties the sequence (stops playback; a recording goes on at step 1). */
void seq_clear(seq_t *s, seq_out_t *out);
/* Starts recording at step 1 (stops playback). The sequence is kept: entries replace the
 * notes of its steps and make it longer once they pass its end. */
void seq_record_start(seq_t *s, seq_out_t *out);
/* Stores the notes of a step (one note 0..127, SEQ_REST or SEQ_TIE) at the record position
 * and moves on; accent and controller values of the step are kept. Returns 1 if stored.
 * A step that does not fit any more ends the recording. */
int seq_record_add(seq_t *s, uint8_t value);
/* Adds a note to the step entered last (chord). Returns 1 if stored: only while recording,
 * onto a note step with a free slot, and not twice. */
int seq_record_add_note(seq_t *s, uint8_t note);
/* Moves the record position to a step of the sequence or to the first step after it.
 * Returns 1 if moved. */
int seq_record_seek(seq_t *s, int step);
/* Ends recording. */
void seq_record_stop(seq_t *s);

/* Step attributes (step 0..SEQ_STEPS-1). */
int  seq_accent(const seq_t *s, int step);
void seq_set_accent(seq_t *s, int step, int on);
uint8_t seq_cc(const seq_t *s, int step, int lane);         /* value or SEQ_NONE */
void seq_set_cc(seq_t *s, int step, int lane, uint8_t value);

/* Length 1..SEQ_STEPS (steps never entered are rests; shortening keeps the steps) and first
 * step 0..len-1. Both are limited to their range. */
void seq_set_len(seq_t *s, int len);
void seq_set_start(seq_t *s, int start);
/* Direction SEQ_DIR_*; takes effect with the next clock. */
void seq_set_dir(seq_t *s, int dir);

/* Starts playback with the next clock (ends recording). Returns 0 if the sequence is empty. */
int seq_play_start(seq_t *s, seq_out_t *out);
/* Stops playback or recording and releases the sounding notes. */
void seq_stop(seq_t *s, seq_out_t *out);

/* Clock on = move to the next step (direction d->dir, steps start..len-1) and play it; clock
 * off = end of the gate (notes are held if the next step is a tie). Both do nothing unless
 * playing. */
void seq_clock_on(seq_t *s, seq_out_t *out);
void seq_clock_off(seq_t *s, seq_out_t *out);

/* First slot of a step: note, SEQ_REST or SEQ_TIE. */
uint8_t seq_step_kind(const seq_t *s, int step);
/* Number of notes of a step (0 for rest / tie); copies them to notes[] if not NULL. */
int seq_step_notes(const seq_t *s, int step, uint8_t *notes);

#endif
