/* SH-101 style step sequencer core: step memory, recording and clocked playback.
 * Pure C, no firmware dependencies, so it is unit-tested on the host
 * (cfw/test/test_seq_core.c). */
#ifndef SEQ_CORE_H
#define SEQ_CORE_H

#include <stdint.h>

#define SEQ_MAX_STEPS  100      /* like the SH-101 */
#define SEQ_REST       0x80     /* step values: 0..127 = note, SEQ_REST, SEQ_TIE */
#define SEQ_TIE        0x81     /* the previous note keeps sounding through this step */

enum { SEQ_STOPPED, SEQ_RECORDING, SEQ_PLAYING };

typedef struct {
    uint8_t step[SEQ_MAX_STEPS];
    uint8_t len;        /* recorded steps; while recording also the index of the next step */
    uint8_t state;      /* SEQ_STOPPED / SEQ_RECORDING / SEQ_PLAYING */
    int8_t  pos;        /* playing: step of the last clock, -1 = waiting for the first clock */
    int8_t  note;       /* note the sequencer is sounding, -1 = none */
} seq_t;

/* Notes the caller has to send after a call: first off (if >= 0), then on (if >= 0). */
typedef struct {
    int8_t off, on;
} seq_out_t;

void seq_init(seq_t *s);

/* Clears the sequence and starts recording (stops playback). */
void seq_record_start(seq_t *s, seq_out_t *out);
/* Appends a step (note 0..127, SEQ_REST or SEQ_TIE). Returns 1 if stored. Recording ends by
 * itself when the memory is full. */
int seq_record_add(seq_t *s, uint8_t value);
/* Ends recording; the sequence is as long as the steps entered. */
void seq_record_stop(seq_t *s);

/* Starts playback at step 1 with the next clock (ends recording). Returns 0 if the sequence
 * is empty. */
int seq_play_start(seq_t *s, seq_out_t *out);
/* Stops playback or recording and releases the sounding note. */
void seq_stop(seq_t *s, seq_out_t *out);

/* Clock on = advance one step and play it; clock off = end of the gate (a note is held if
 * the next step is a tie). Both do nothing unless playing. */
void seq_clock_on(seq_t *s, seq_out_t *out);
void seq_clock_off(seq_t *s, seq_out_t *out);

#endif
