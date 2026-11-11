/* Step sequencer core (SH-101 style step entry): step memory, recording and clocked
 * playback, with 1..SEQ_VOICES_MAX notes per step. Pure C, no firmware dependencies, so it
 * is unit-tested on the host (cfw/test/test_seq_core.c). */
#ifndef SEQ_CORE_H
#define SEQ_CORE_H

#include <stdint.h>

#define SEQ_VOICES_MAX 4
#define SEQ_REST       0x80     /* first slot of a step: 0..127 = note, SEQ_REST, SEQ_TIE */
#define SEQ_TIE        0x81     /* the previous notes keep sounding through this step */
#define SEQ_NONE       0xFF     /* further slots: note or SEQ_NONE */

enum { SEQ_STOPPED, SEQ_RECORDING, SEQ_PLAYING };

typedef struct {
    uint8_t *steps;     /* max * voices bytes, `voices` slots per step */
    uint8_t max;        /* step capacity */
    uint8_t voices;     /* notes per step, 1..SEQ_VOICES_MAX */
    uint8_t len;        /* steps of the sequence */
    uint8_t state;      /* SEQ_STOPPED / SEQ_RECORDING / SEQ_PLAYING */
    uint8_t rec;        /* recording: step the next entry goes to */
    int8_t  pos;        /* playing: step of the last clock, -1 = waiting for the first clock */
    uint8_t n_sounding; /* notes the sequencer is sounding */
    uint8_t sounding[SEQ_VOICES_MAX];
} seq_t;

/* Notes the caller has to send after a call: first the offs, then the ons. */
typedef struct {
    uint8_t n_off, n_on;
    uint8_t off[SEQ_VOICES_MAX], on[SEQ_VOICES_MAX];
} seq_out_t;

void seq_init(seq_t *s, uint8_t *steps, uint8_t max, uint8_t voices);

/* Empties the sequence (stops playback; a recording goes on at step 1). */
void seq_clear(seq_t *s, seq_out_t *out);
/* Starts recording at step 1 (stops playback). The sequence is kept: entries overwrite its
 * steps and make it longer once they pass its end. */
void seq_record_start(seq_t *s, seq_out_t *out);
/* Stores a step (note 0..127, SEQ_REST or SEQ_TIE) at the record position and moves on.
 * Returns 1 if stored. A step that does not fit any more ends the recording. */
int seq_record_add(seq_t *s, uint8_t value);
/* Adds a note to the step entered last (chord). Returns 1 if stored: only while recording,
 * onto a note step with a free slot, and not twice. */
int seq_record_add_note(seq_t *s, uint8_t note);
/* Moves the record position to a step of the sequence or to the first free step.
 * Returns 1 if moved. */
int seq_record_seek(seq_t *s, int step);
/* Ends recording. */
void seq_record_stop(seq_t *s);

/* Starts playback at step 1 with the next clock (ends recording). Returns 0 if the sequence
 * is empty. */
int seq_play_start(seq_t *s, seq_out_t *out);
/* Stops playback or recording and releases the sounding notes. */
void seq_stop(seq_t *s, seq_out_t *out);

/* Clock on = advance one step and play it; clock off = end of the gate (notes are held if
 * the next step is a tie). Both do nothing unless playing. */
void seq_clock_on(seq_t *s, seq_out_t *out);
void seq_clock_off(seq_t *s, seq_out_t *out);

/* First slot of a step: note, SEQ_REST or SEQ_TIE. */
uint8_t seq_step_kind(const seq_t *s, int step);
/* Number of notes of a step (0 for rest / tie); copies them to notes[] if not NULL. */
int seq_step_notes(const seq_t *s, int step, uint8_t *notes);

#endif
