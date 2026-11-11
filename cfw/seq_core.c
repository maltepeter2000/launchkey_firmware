/* SH-101 style step sequencer core, see seq_core.h. */
#include "seq_core.h"

static void out_clear(seq_out_t *out)
{
    out->off = -1;
    out->on = -1;
}

static void release(seq_t *s, seq_out_t *out)
{
    out->off = s->note;
    s->note = -1;
}

void seq_init(seq_t *s)
{
    s->len = 0;
    s->state = SEQ_STOPPED;
    s->pos = -1;
    s->note = -1;
}

void seq_record_start(seq_t *s, seq_out_t *out)
{
    out_clear(out);
    release(s, out);
    s->len = 0;
    s->pos = -1;
    s->state = SEQ_RECORDING;
}

int seq_record_add(seq_t *s, uint8_t value)
{
    if (s->state != SEQ_RECORDING || s->len >= SEQ_MAX_STEPS)
        return 0;
    if (value > 127 && value != SEQ_REST && value != SEQ_TIE)
        return 0;
    s->step[s->len++] = value;
    if (s->len == SEQ_MAX_STEPS)
        s->state = SEQ_STOPPED;
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
    uint8_t v = s->step[s->pos];
    if (v == SEQ_TIE)
        return;                 /* whatever sounds keeps sounding */
    release(s, out);            /* also covers clocks that never send a note off */
    if (v <= 127) {
        out->on = (int8_t)v;
        s->note = (int8_t)v;
    }
}

void seq_clock_off(seq_t *s, seq_out_t *out)
{
    out_clear(out);
    if (s->state != SEQ_PLAYING || !s->len || s->note < 0)
        return;
    if (s->step[(s->pos + 1) % s->len] != SEQ_TIE)
        release(s, out);
}
