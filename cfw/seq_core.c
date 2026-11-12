/* Step sequencer core, see seq_core.h. */
#include "seq_core.h"

static uint8_t *slot(const seq_t *s, int step)
{
    return s->d->steps + step * SEQ_STEP_BYTES;
}

static void out_clear(seq_out_t *out)
{
    out->n_off = 0;
    out->n_on = 0;
    out->accent = 0;
    for (int i = 0; i < SEQ_LANES; i++)
        out->cc[i] = SEQ_NONE;
}

static void release(seq_t *s, seq_out_t *out)
{
    for (int i = 0; i < s->n_sounding; i++)
        out->off[out->n_off++] = s->sounding[i];
    s->n_sounding = 0;
}

static void wipe(seq_t *s)
{
    seq_data_t *d = s->d;
    d->len = 0;
    d->start = 0;
    for (unsigned i = 0; i < sizeof d->accent; i++)
        d->accent[i] = 0;
    for (int i = 0; i < SEQ_STEPS; i++) {
        uint8_t *p = slot(s, i);
        p[0] = SEQ_REST;
        for (int k = 1; k < SEQ_STEP_BYTES; k++)
            p[k] = SEQ_NONE;
    }
}

void seq_init(seq_t *s, seq_data_t *d, uint16_t seed)
{
    s->d = d;
    s->state = SEQ_STOPPED;
    s->rec = 0;
    s->pos = -1;
    s->next = -1;
    s->down = 0;
    s->n_sounding = 0;
    s->rng = seed ? seed : 0xACE1;
    d->dir = SEQ_DIR_FORWARD;
    wipe(s);
}

void seq_sanitize(seq_t *s)
{
    seq_data_t *d = s->d;
    if (d->len > SEQ_STEPS)
        d->len = SEQ_STEPS;
    if (d->start >= d->len)
        d->start = 0;
    if (d->dir >= SEQ_DIR_COUNT)
        d->dir = SEQ_DIR_FORWARD;
    for (int i = 0; i < SEQ_STEPS; i++) {
        uint8_t *p = slot(s, i);
        if (p[0] > SEQ_TIE)
            p[0] = SEQ_REST;
        for (int k = 1; k < SEQ_STEP_BYTES; k++)
            if (p[k] > 127 || (k < SEQ_VOICES && p[0] > 127))
                p[k] = SEQ_NONE;
    }
    if (s->rec > d->len)
        s->rec = d->len;
    if (s->state == SEQ_PLAYING && !d->len)
        s->state = SEQ_STOPPED;
}

void seq_clear(seq_t *s, seq_out_t *out)
{
    out_clear(out);
    release(s, out);
    wipe(s);
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
    for (int i = 0; i < SEQ_VOICES; i++)
        if (p[i] <= 127) {
            if (notes)
                notes[n] = p[i];
            n++;
        }
    return n;
}

int seq_accent(const seq_t *s, int step)
{
    return s->d->accent[step >> 3] >> (step & 7) & 1;
}

void seq_set_accent(seq_t *s, int step, int on)
{
    uint8_t bit = (uint8_t)(1u << (step & 7));
    if (step < 0 || step >= SEQ_STEPS)
        return;
    if (on)
        s->d->accent[step >> 3] |= bit;
    else
        s->d->accent[step >> 3] &= (uint8_t)~bit;
}

uint8_t seq_cc(const seq_t *s, int step, int lane)
{
    return slot(s, step)[SEQ_VOICES + lane];
}

void seq_set_cc(seq_t *s, int step, int lane, uint8_t value)
{
    if (step < 0 || step >= SEQ_STEPS || lane < 0 || lane >= SEQ_LANES)
        return;
    slot(s, step)[SEQ_VOICES + lane] = value <= 127 ? value : SEQ_NONE;
}

void seq_set_len(seq_t *s, int len)
{
    seq_data_t *d = s->d;
    d->len = (uint8_t)(len < 1 ? 1 : len > SEQ_STEPS ? SEQ_STEPS : len);
    if (d->start >= d->len)
        d->start = (uint8_t)(d->len - 1);
    if (s->rec > d->len)
        s->rec = d->len;
}

void seq_set_start(seq_t *s, int start)
{
    seq_data_t *d = s->d;
    int hi = d->len ? d->len - 1 : 0;
    d->start = (uint8_t)(start < 0 ? 0 : start > hi ? hi : start);
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
    if (s->rec >= SEQ_STEPS) {
        s->state = SEQ_STOPPED;         /* memory full */
        return 0;
    }
    uint8_t *p = slot(s, s->rec++);
    p[0] = value;
    for (int i = 1; i < SEQ_VOICES; i++)
        p[i] = SEQ_NONE;
    if (s->rec > s->d->len)
        s->d->len = s->rec;
    return 1;
}

int seq_record_add_note(seq_t *s, uint8_t note)
{
    if (s->state != SEQ_RECORDING || !s->rec || note > 127)
        return 0;
    uint8_t *p = slot(s, s->rec - 1);
    if (p[0] > 127)
        return 0;                       /* rest or tie */
    for (int i = 0; i < SEQ_VOICES; i++) {
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
    if (s->state != SEQ_RECORDING || step < 0 || step >= SEQ_STEPS || step > s->d->len)
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
    s->next = -1;
    s->down = 0;
    s->state = s->d->len ? SEQ_PLAYING : SEQ_STOPPED;
    return s->d->len != 0;
}

void seq_stop(seq_t *s, seq_out_t *out)
{
    out_clear(out);
    release(s, out);
    s->pos = -1;
    s->state = SEQ_STOPPED;
}

static unsigned rnd(seq_t *s)
{
    uint16_t x = s->rng;                /* xorshift16 */
    x ^= (uint16_t)(x << 7);
    x ^= (uint16_t)(x >> 9);
    x ^= (uint16_t)(x << 8);
    s->rng = x;
    return x;
}

/* Step after `pos` (-1 = none yet) within lo..hi. */
static int advance(seq_t *s, int pos, int lo, int hi)
{
    int inside = pos >= lo && pos <= hi;
    if (lo == hi)
        return lo;
    switch (s->d->dir) {
    case SEQ_DIR_REVERSE:
        return inside && pos > lo ? pos - 1 : hi;
    case SEQ_DIR_ALTERNATE:
        if (!inside) {
            s->down = 0;
            return lo;
        }
        if (pos == hi)
            s->down = 1;
        else if (pos == lo)
            s->down = 0;
        return s->down ? pos - 1 : pos + 1;
    case SEQ_DIR_RANDOM:
        return lo + (int)(rnd(s) % (unsigned)(hi - lo + 1));
    default:
        return inside && pos < hi ? pos + 1 : lo;
    }
}

void seq_set_dir(seq_t *s, int dir)
{
    const seq_data_t *d = s->d;
    if (dir < 0 || dir >= SEQ_DIR_COUNT)
        return;
    s->d->dir = (uint8_t)dir;
    if (s->state == SEQ_PLAYING && d->len && s->pos >= 0) {     /* the step planned next */
        int hi = d->len - 1, lo = d->start <= hi ? d->start : hi;
        s->next = (int8_t)advance(s, s->pos, lo, hi);
    }
}

void seq_clock_on(seq_t *s, seq_out_t *out)
{
    const seq_data_t *d = s->d;
    out_clear(out);
    if (s->state != SEQ_PLAYING || !d->len)
        return;
    int hi = d->len - 1, lo = d->start <= hi ? d->start : hi;
    int pos = s->next;
    if (pos < lo || pos > hi)           /* first clock, or the range has changed */
        pos = advance(s, s->pos, lo, hi);
    s->pos = (int8_t)pos;
    s->next = (int8_t)advance(s, pos, lo, hi);
    for (int i = 0; i < SEQ_LANES; i++)
        out->cc[i] = seq_cc(s, pos, i);
    if (seq_step_kind(s, pos) == SEQ_TIE)
        return;                 /* whatever sounds keeps sounding */
    release(s, out);            /* also covers clocks that never send a note off */
    out->n_on = (uint8_t)seq_step_notes(s, pos, out->on);
    out->accent = (uint8_t)seq_accent(s, pos);
    s->n_sounding = out->n_on;
    for (int i = 0; i < out->n_on; i++)
        s->sounding[i] = out->on[i];
}

void seq_clock_off(seq_t *s, seq_out_t *out)
{
    const seq_data_t *d = s->d;
    out_clear(out);
    if (s->state != SEQ_PLAYING || !d->len || !s->n_sounding)
        return;
    if (s->next < 0 || s->next >= d->len || seq_step_kind(s, s->next) != SEQ_TIE)
        release(s, out);
}
