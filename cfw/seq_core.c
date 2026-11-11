/* Step sequencer core, see seq_core.h. */
#include "seq_core.h"

static uint8_t *slot(const seq_t *s, int step)
{
    return s->steps + step * s->voices;
}

static void out_clear(seq_out_t *out)
{
    out->n_off = 0;
    out->n_on = 0;
}

static void release(seq_t *s, seq_out_t *out)
{
    for (int i = 0; i < s->n_sounding; i++)
        out->off[out->n_off++] = s->sounding[i];
    s->n_sounding = 0;
}

void seq_init(seq_t *s, uint8_t *steps, uint8_t max, uint8_t voices)
{
    s->steps = steps;
    s->max = max;
    s->voices = voices;
    s->len = 0;
    s->state = SEQ_STOPPED;
    s->pos = -1;
    s->rec = 0;
    s->n_sounding = 0;
}

void seq_clear(seq_t *s, seq_out_t *out)
{
    out_clear(out);
    release(s, out);
    s->len = 0;
    s->rec = 0;
    s->pos = -1;
    if (s->state == SEQ_PLAYING)
        s->state = SEQ_STOPPED;
}

uint8_t seq_step_kind(const seq_t *s, int step)
{
    return slot(s, step)[0];
}

int seq_step_notes(const seq_t *s, int step, uint8_t *notes)
{
    const uint8_t *p = slot(s, step);
    int n = 0;
    for (int i = 0; i < s->voices; i++)
        if (p[i] <= 127) {
            if (notes)
                notes[n] = p[i];
            n++;
        }
    return n;
}

void seq_record_start(seq_t *s, seq_out_t *out)
{
    out_clear(out);
    release(s, out);
    s->pos = -1;
    s->rec = 0;
    s->state = SEQ_RECORDING;
}

int seq_record_add(seq_t *s, uint8_t value)
{
    if (s->state != SEQ_RECORDING)
        return 0;
    if (value > 127 && value != SEQ_REST && value != SEQ_TIE)
        return 0;
    if (s->rec >= s->max) {
        s->state = SEQ_STOPPED;         /* memory full */
        return 0;
    }
    uint8_t *p = slot(s, s->rec++);
    p[0] = value;
    for (int i = 1; i < s->voices; i++)
        p[i] = SEQ_NONE;
    if (s->rec > s->len)
        s->len = s->rec;
    return 1;
}

int seq_record_add_note(seq_t *s, uint8_t note)
{
    if (s->state != SEQ_RECORDING || !s->rec || note > 127)
        return 0;
    uint8_t *p = slot(s, s->rec - 1);
    if (p[0] > 127)
        return 0;                       /* rest or tie */
    for (int i = 0; i < s->voices; i++) {
        if (p[i] == note)
            return 0;
        if (p[i] == SEQ_NONE) {
            p[i] = note;
            return 1;
        }
    }
    return 0;
}

int seq_record_seek(seq_t *s, int step)
{
    if (s->state != SEQ_RECORDING || step < 0 || step >= s->max || step > s->len)
        return 0;
    s->rec = (uint8_t)step;
    return 1;
}

void seq_record_stop(seq_t *s)
{
    if (s->state == SEQ_RECORDING)
        s->state = SEQ_STOPPED;
}

int seq_play_start(seq_t *s, seq_out_t *out)
{
    out_clear(out);
    release(s, out);
    s->pos = -1;
    s->state = s->len ? SEQ_PLAYING : SEQ_STOPPED;
    return s->len != 0;
}

void seq_stop(seq_t *s, seq_out_t *out)
{
    out_clear(out);
    release(s, out);
    s->pos = -1;
    s->state = SEQ_STOPPED;
}

void seq_clock_on(seq_t *s, seq_out_t *out)
{
    out_clear(out);
    if (s->state != SEQ_PLAYING || !s->len)
        return;
    s->pos = (int8_t)((s->pos + 1) % s->len);
    if (seq_step_kind(s, s->pos) == SEQ_TIE)
        return;                 /* whatever sounds keeps sounding */
    release(s, out);            /* also covers clocks that never send a note off */
    out->n_on = (uint8_t)seq_step_notes(s, s->pos, out->on);
    s->n_sounding = out->n_on;
    for (int i = 0; i < out->n_on; i++)
        s->sounding[i] = out->on[i];
}

void seq_clock_off(seq_t *s, seq_out_t *out)
{
    out_clear(out);
    if (s->state != SEQ_PLAYING || !s->len || !s->n_sounding)
        return;
    if (seq_step_kind(s, (s->pos + 1) % s->len) != SEQ_TIE)
        release(s, out);
}
