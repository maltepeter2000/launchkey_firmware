/* Host unit tests for seq_core (make cfw-test). */
#include <stdio.h>
#include "../seq_core.h"

static int failures, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* one-voice expectations: -1 = nothing */
#define OUT(o, off_, on_) (((off_) < 0 ? (o).n_off == 0 : (o).n_off == 1 && (o).off[0] == (off_)) \
                           && ((on_) < 0 ? (o).n_on == 0 : (o).n_on == 1 && (o).on[0] == (on_)))
#define SOUNDS(s, n) ((n) < 0 ? (s).n_sounding == 0 : (s).n_sounding == 1 && (s).sounding[0] == (n))

#define MAX_STEPS 100
static uint8_t mem[MAX_STEPS * SEQ_VOICES_MAX];

static void record(seq_t *s, const uint8_t *steps, int n)
{
    seq_out_t o;
    seq_clear(s, &o);
    seq_record_start(s, &o);
    for (int i = 0; i < n; i++)
        seq_record_add(s, steps[i]);
    seq_record_stop(s);
}

static void test_record(void)
{
    seq_t s;
    seq_out_t o;
    seq_init(&s, mem, MAX_STEPS, 1);
    CHECK(s.state == SEQ_STOPPED && s.len == 0 && SOUNDS(s, -1));
    CHECK(seq_record_add(&s, 60) == 0 && s.len == 0);       /* not recording */

    seq_record_start(&s, &o);
    CHECK(s.state == SEQ_RECORDING && OUT(o, -1, -1));
    CHECK(seq_record_add(&s, 60) && seq_record_add(&s, SEQ_REST) && seq_record_add(&s, SEQ_TIE));
    CHECK(seq_record_add(&s, 0) && seq_record_add(&s, 127));
    CHECK(seq_record_add(&s, 0x82) == 0 && seq_record_add(&s, 200) == 0);
    CHECK(s.len == 5);
    seq_record_stop(&s);
    CHECK(s.state == SEQ_STOPPED && s.len == 5);            /* last step entered = length */
    CHECK(mem[0] == 60 && mem[1] == SEQ_REST && mem[2] == SEQ_TIE && mem[4] == 127);
    CHECK(seq_record_add(&s, 61) == 0 && s.len == 5);

    seq_record_start(&s, &o);                               /* the sequence is kept */
    CHECK(s.len == 5 && s.state == SEQ_RECORDING && s.rec == 0);
    seq_record_stop(&s);
    CHECK(s.len == 5 && mem[0] == 60);
    seq_record_start(&s, &o);                               /* entries overwrite from step 1 */
    CHECK(seq_record_add(&s, 70) && seq_record_add(&s, 71) && s.len == 5);
    seq_record_stop(&s);
    CHECK(s.len == 5 && mem[0] == 70 && mem[1] == 71 && mem[2] == SEQ_TIE);

    seq_clear(&s, &o);                                      /* reset */
    CHECK(s.len == 0 && s.state == SEQ_STOPPED && OUT(o, -1, -1));
    seq_record_start(&s, &o);
    seq_record_add(&s, 50);
    seq_clear(&s, &o);                                      /* while recording: goes on at step 1 */
    CHECK(s.len == 0 && s.rec == 0 && s.state == SEQ_RECORDING);
    for (int i = 0; i < MAX_STEPS - 1; i++)
        CHECK(seq_record_add(&s, (uint8_t)(i & 0x7F)) && s.state == SEQ_RECORDING);
    CHECK(seq_record_add(&s, 1) && s.len == MAX_STEPS && s.state == SEQ_RECORDING);
    CHECK(seq_record_add(&s, 2) == 0 && s.len == MAX_STEPS && s.state == SEQ_STOPPED);  /* full */
    CHECK(seq_record_add_note(&s, 3) == 0);                 /* one voice: no chords */
}

static void test_play(void)
{
    static const uint8_t steps[] = {60, 62, SEQ_REST, 64};
    seq_t s;
    seq_out_t o;
    seq_init(&s, mem, MAX_STEPS, 1);

    CHECK(seq_play_start(&s, &o) == 0 && s.state == SEQ_STOPPED);   /* empty */
    seq_clock_on(&s, &o);
    CHECK(OUT(o, -1, -1));

    record(&s, steps, 4);
    seq_clock_on(&s, &o);
    CHECK(OUT(o, -1, -1) && s.pos == -1);                           /* clock without Play */

    CHECK(seq_play_start(&s, &o) == 1 && s.state == SEQ_PLAYING && OUT(o, -1, -1));
    seq_clock_off(&s, &o);
    CHECK(OUT(o, -1, -1));                                          /* nothing sounding yet */
    seq_clock_on(&s, &o);
    CHECK(OUT(o, -1, 60) && s.pos == 0);
    seq_clock_off(&s, &o);
    CHECK(OUT(o, 60, -1));
    seq_clock_off(&s, &o);
    CHECK(OUT(o, -1, -1));
    seq_clock_on(&s, &o);
    CHECK(OUT(o, -1, 62) && s.pos == 1);
    seq_clock_on(&s, &o);                                           /* clock without note off */
    CHECK(OUT(o, 62, -1) && s.pos == 2 && SOUNDS(s, -1));            /* rest */
    seq_clock_off(&s, &o);
    CHECK(OUT(o, -1, -1));
    seq_clock_on(&s, &o);
    CHECK(OUT(o, -1, 64) && s.pos == 3);
    seq_clock_on(&s, &o);                                           /* wraps, legato off -> on */
    CHECK(OUT(o, 64, 60) && s.pos == 0);

    seq_stop(&s, &o);
    CHECK(OUT(o, 60, -1) && s.state == SEQ_STOPPED && SOUNDS(s, -1));
    seq_stop(&s, &o);
    CHECK(OUT(o, -1, -1));
    seq_clock_on(&s, &o);
    CHECK(OUT(o, -1, -1));

    seq_play_start(&s, &o);                                         /* always from step 1 */
    seq_clock_on(&s, &o);
    CHECK(OUT(o, -1, 60) && s.pos == 0);

    seq_record_start(&s, &o);                                       /* Record while playing */
    CHECK(OUT(o, 60, -1) && s.state == SEQ_RECORDING && s.rec == 0);
    seq_clock_on(&s, &o);
    CHECK(OUT(o, -1, -1));
    seq_record_add(&s, 48);
    CHECK(seq_play_start(&s, &o) == 1 && s.state == SEQ_PLAYING && s.len == 4);   /* Play ends recording */
    seq_clock_on(&s, &o);
    CHECK(OUT(o, -1, 48));
    seq_clock_off(&s, &o);
    CHECK(OUT(o, 48, -1));
    seq_clear(&s, &o);                                              /* reset while playing */
    CHECK(s.state == SEQ_STOPPED && s.len == 0);
    seq_clock_on(&s, &o);
    CHECK(OUT(o, 48, -1) == 0 && o.n_on == 0);
    seq_clock_on(&s, &o);
    seq_clear(&s, &o);
    static const uint8_t one[] = {48};
    record(&s, one, 1);
    seq_play_start(&s, &o);
    seq_clock_on(&s, &o);
    seq_clear(&s, &o);
    CHECK(OUT(o, 48, -1));                                          /* releases the note */
    record(&s, one, 1);
    seq_play_start(&s, &o);
    seq_clock_on(&s, &o);
    seq_clock_off(&s, &o);
    CHECK(OUT(o, 48, -1));                                          /* one step: next is itself */
}

static void test_ties(void)
{
    static const uint8_t steps[] = {60, SEQ_TIE, SEQ_TIE, 62, SEQ_REST, SEQ_TIE};
    seq_t s;
    seq_out_t o;
    seq_init(&s, mem, MAX_STEPS, 1);
    record(&s, steps, 6);
    seq_play_start(&s, &o);

    seq_clock_on(&s, &o);
    CHECK(OUT(o, -1, 60));
    seq_clock_off(&s, &o);
    CHECK(OUT(o, -1, -1) && SOUNDS(s, 60));      /* next is a tie: held */
    seq_clock_on(&s, &o);
    CHECK(OUT(o, -1, -1) && s.pos == 1);
    seq_clock_off(&s, &o);
    CHECK(OUT(o, -1, -1) && SOUNDS(s, 60));
    seq_clock_on(&s, &o);
    CHECK(OUT(o, -1, -1) && s.pos == 2);
    seq_clock_off(&s, &o);
    CHECK(OUT(o, 60, -1));                      /* next is a note: gate ends */
    seq_clock_on(&s, &o);
    CHECK(OUT(o, -1, 62));
    seq_clock_off(&s, &o);
    CHECK(OUT(o, 62, -1));
    seq_clock_on(&s, &o);
    CHECK(OUT(o, -1, -1) && s.pos == 4);        /* rest */
    seq_clock_off(&s, &o);
    seq_clock_on(&s, &o);
    CHECK(OUT(o, -1, -1) && s.pos == 5 && SOUNDS(s, -1));   /* tie after a rest stays silent */
    seq_clock_off(&s, &o);
    seq_clock_on(&s, &o);
    CHECK(OUT(o, -1, 60) && s.pos == 0);

    /* a tie on step 1 holds the last step's note over the loop point */
    static const uint8_t loop[] = {SEQ_TIE, 67};
    record(&s, loop, 2);
    seq_play_start(&s, &o);
    seq_clock_on(&s, &o);
    CHECK(OUT(o, -1, -1));
    seq_clock_off(&s, &o);
    seq_clock_on(&s, &o);
    CHECK(OUT(o, -1, 67));
    seq_clock_off(&s, &o);
    CHECK(OUT(o, -1, -1) && SOUNDS(s, 67));
    seq_clock_on(&s, &o);
    CHECK(OUT(o, -1, -1) && s.pos == 0 && SOUNDS(s, 67));
    seq_clock_off(&s, &o);
    CHECK(OUT(o, 67, -1));
    seq_clock_on(&s, &o);
    CHECK(OUT(o, -1, 67));
    seq_stop(&s, &o);
    CHECK(OUT(o, 67, -1));
}

static void test_long(void)
{
    seq_t s;
    seq_out_t o;
    seq_init(&s, mem, MAX_STEPS, 1);
    seq_record_start(&s, &o);
    for (int i = 0; i < MAX_STEPS; i++)
        seq_record_add(&s, (uint8_t)(20 + i));
    seq_play_start(&s, &o);
    for (int lap = 0; lap < 3; lap++)
        for (int i = 0; i < MAX_STEPS; i++) {
            seq_clock_on(&s, &o);
            CHECK(OUT(o, -1, 20 + i) && s.pos == i);
            seq_clock_off(&s, &o);
            CHECK(OUT(o, 20 + i, -1));
        }
}

static int same(const uint8_t *a, int n, int a0, int a1, int a2, int a3)
{
    const int e[4] = {a0, a1, a2, a3};
    int count = 0;
    while (count < 4 && e[count] >= 0)
        count++;
    if (n != count)
        return 0;
    for (int i = 0; i < n; i++)
        if (a[i] != e[i])
            return 0;
    return 1;
}

static void test_poly(void)
{
    seq_t s;
    seq_out_t o;
    uint8_t notes[SEQ_VOICES_MAX];
    seq_init(&s, mem, 8, SEQ_VOICES_MAX);
    CHECK(seq_record_add_note(&s, 60) == 0);                /* not recording */
    seq_record_start(&s, &o);
    CHECK(seq_record_add_note(&s, 60) == 0);                /* no step yet */

    CHECK(seq_record_add(&s, 60) && seq_record_add_note(&s, 64) && seq_record_add_note(&s, 67));
    CHECK(seq_record_add_note(&s, 64) == 0);                /* already in the step */
    CHECK(seq_record_add_note(&s, 72) && seq_record_add_note(&s, 76) == 0);   /* 4 voices */
    CHECK(seq_step_notes(&s, 0, notes) == 4 && same(notes, 4, 60, 64, 67, 72));
    CHECK(seq_record_add(&s, 50) && seq_step_notes(&s, 1, notes) == 1 && notes[0] == 50);
    CHECK(seq_record_add(&s, SEQ_TIE) && seq_record_add_note(&s, 51) == 0);
    CHECK(seq_record_add(&s, SEQ_REST) && seq_record_add_note(&s, 51) == 0);
    CHECK(seq_step_notes(&s, 2, notes) == 0 && seq_step_notes(&s, 3, 0) == 0);
    CHECK(seq_step_kind(&s, 0) == 60 && seq_step_kind(&s, 2) == SEQ_TIE && seq_step_kind(&s, 3) == SEQ_REST);
    CHECK(seq_record_add(&s, 40) && seq_record_add_note(&s, 47));
    seq_record_stop(&s);
    CHECK(s.len == 5 && seq_record_add_note(&s, 48) == 0);

    /* chord, 50 tied, rest, 40+47 */
    seq_play_start(&s, &o);
    seq_clock_on(&s, &o);
    CHECK(o.n_off == 0 && same(o.on, o.n_on, 60, 64, 67, 72) && s.n_sounding == 4);
    seq_clock_off(&s, &o);
    CHECK(same(o.off, o.n_off, 60, 64, 67, 72) && o.n_on == 0 && s.n_sounding == 0);
    seq_clock_on(&s, &o);
    CHECK(same(o.on, o.n_on, 50, -1, -1, -1));
    seq_clock_off(&s, &o);
    CHECK(o.n_off == 0 && s.n_sounding == 1);               /* tie follows */
    seq_clock_on(&s, &o);
    CHECK(o.n_off == 0 && o.n_on == 0);
    seq_clock_off(&s, &o);
    CHECK(same(o.off, o.n_off, 50, -1, -1, -1));
    seq_clock_on(&s, &o);
    CHECK(o.n_off == 0 && o.n_on == 0 && s.pos == 3);       /* rest */
    seq_clock_on(&s, &o);
    CHECK(same(o.on, o.n_on, 40, 47, -1, -1));
    seq_clock_on(&s, &o);                                   /* no note off: chord to chord */
    CHECK(same(o.off, o.n_off, 40, 47, -1, -1) && same(o.on, o.n_on, 60, 64, 67, 72));
    seq_stop(&s, &o);
    CHECK(same(o.off, o.n_off, 60, 64, 67, 72) && s.n_sounding == 0);

    /* a tie holds the whole chord; the memory limit ends the recording */
    seq_clear(&s, &o);
    seq_record_start(&s, &o);
    for (int i = 0; i < 8; i++) {
        CHECK(seq_record_add(&s, i == 1 ? SEQ_TIE : (uint8_t)(30 + i)));
        if (i == 0)
            seq_record_add_note(&s, 37);
    }
    CHECK(s.state == SEQ_RECORDING && seq_record_add_note(&s, 99));   /* last step still open */
    CHECK(seq_record_add(&s, 1) == 0 && s.state == SEQ_STOPPED && s.len == 8);
    seq_play_start(&s, &o);
    seq_clock_on(&s, &o);
    seq_clock_off(&s, &o);
    seq_clock_on(&s, &o);
    CHECK(o.n_off == 0 && o.n_on == 0 && s.n_sounding == 2);
    seq_clock_off(&s, &o);
    CHECK(same(o.off, o.n_off, 30, 37, -1, -1));
    for (int i = 2; i < 8; i++)
        seq_clock_on(&s, &o);
    CHECK(same(o.on, o.n_on, 37, 99, -1, -1) && s.pos == 7);
}

static void test_seek(void)
{
    static const uint8_t steps[] = {60, 62, 64, 65, 67};
    seq_t s;
    seq_out_t o;
    uint8_t notes[SEQ_VOICES_MAX];
    seq_init(&s, mem, 8, SEQ_VOICES_MAX);
    CHECK(seq_record_seek(&s, 0) == 0);                     /* not recording */
    record(&s, steps, 5);
    seq_record_start(&s, &o);
    seq_record_add_note(&s, 50);
    CHECK(seq_step_notes(&s, 0, 0) == 1);                   /* nothing entered yet: no chord */
    CHECK(seq_record_seek(&s, 6) == 0 && seq_record_seek(&s, -1) == 0 && seq_record_seek(&s, 8) == 0);

    /* change step 3, then step 4 follows; the length is kept */
    CHECK(seq_record_seek(&s, 2) && s.rec == 2 && s.len == 5);
    CHECK(seq_record_add(&s, 72) && seq_record_add_note(&s, 76) && s.rec == 3);
    CHECK(seq_record_add(&s, SEQ_REST) && s.rec == 4 && s.len == 5);
    seq_record_stop(&s);
    CHECK(s.len == 5 && seq_step_kind(&s, 0) == 60 && seq_step_kind(&s, 1) == 62 && seq_step_kind(&s, 4) == 67);
    CHECK(seq_step_notes(&s, 2, notes) == 2 && notes[0] == 72 && notes[1] == 76);
    CHECK(seq_step_kind(&s, 3) == SEQ_REST);

    /* a chord step overwritten by one note loses its other notes */
    seq_record_start(&s, &o);
    CHECK(seq_record_seek(&s, 2) && seq_record_add(&s, 40));
    CHECK(seq_step_notes(&s, 2, notes) == 1 && notes[0] == 40);

    /* the first free step can be selected: appending makes the sequence longer */
    CHECK(seq_record_seek(&s, 5) && seq_record_add(&s, 41) && seq_record_add(&s, 42) && s.len == 7);
    CHECK(seq_record_seek(&s, 7) && seq_record_add(&s, 43) && s.len == 8);
    CHECK(seq_record_seek(&s, 8) == 0);                     /* beyond the memory */
    CHECK(seq_record_add(&s, 44) == 0 && s.state == SEQ_STOPPED && s.len == 8);

    /* recording again starts at step 1 and keeps the rest */
    seq_record_start(&s, &o);
    seq_record_add(&s, 10);
    CHECK(seq_play_start(&s, &o) && s.len == 8);            /* Play ends the recording */
    seq_clock_on(&s, &o);
    CHECK(same(o.on, o.n_on, 10, -1, -1, -1));
    seq_clock_on(&s, &o);
    CHECK(same(o.on, o.n_on, 62, -1, -1, -1));
}

int main(void)
{
    test_record();
    test_play();
    test_ties();
    test_long();
    test_poly();
    test_seek();
    printf("seq_core: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
