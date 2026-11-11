/* Host unit tests for seq_core (make cfw-test). */
#include <stdio.h>
#include "../seq_core.h"

static int failures, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

#define OUT(o, off_, on_) ((o).off == (off_) && (o).on == (on_))

static void record(seq_t *s, const uint8_t *steps, int n)
{
    seq_out_t o;
    seq_record_start(s, &o);
    for (int i = 0; i < n; i++)
        seq_record_add(s, steps[i]);
    seq_record_stop(s);
}

static void test_record(void)
{
    seq_t s;
    seq_out_t o;
    seq_init(&s);
    CHECK(s.state == SEQ_STOPPED && s.len == 0 && s.note == -1);
    CHECK(seq_record_add(&s, 60) == 0 && s.len == 0);       /* not recording */

    seq_record_start(&s, &o);
    CHECK(s.state == SEQ_RECORDING && OUT(o, -1, -1));
    CHECK(seq_record_add(&s, 60) && seq_record_add(&s, SEQ_REST) && seq_record_add(&s, SEQ_TIE));
    CHECK(seq_record_add(&s, 0) && seq_record_add(&s, 127));
    CHECK(seq_record_add(&s, 0x82) == 0 && seq_record_add(&s, 200) == 0);
    CHECK(s.len == 5);
    seq_record_stop(&s);
    CHECK(s.state == SEQ_STOPPED && s.len == 5);            /* last step entered = length */
    CHECK(s.step[0] == 60 && s.step[1] == SEQ_REST && s.step[2] == SEQ_TIE && s.step[4] == 127);
    CHECK(seq_record_add(&s, 61) == 0 && s.len == 5);

    seq_record_start(&s, &o);                               /* a new recording clears the old one */
    CHECK(s.len == 0 && s.state == SEQ_RECORDING);
    for (int i = 0; i < SEQ_MAX_STEPS - 1; i++)
        CHECK(seq_record_add(&s, (uint8_t)(i & 0x7F)) && s.state == SEQ_RECORDING);
    CHECK(seq_record_add(&s, 1) && s.len == SEQ_MAX_STEPS && s.state == SEQ_STOPPED);  /* full */
    CHECK(seq_record_add(&s, 2) == 0 && s.len == SEQ_MAX_STEPS);
}

static void test_play(void)
{
    static const uint8_t steps[] = {60, 62, SEQ_REST, 64};
    seq_t s;
    seq_out_t o;
    seq_init(&s);

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
    CHECK(OUT(o, 62, -1) && s.pos == 2 && s.note == -1);            /* rest */
    seq_clock_off(&s, &o);
    CHECK(OUT(o, -1, -1));
    seq_clock_on(&s, &o);
    CHECK(OUT(o, -1, 64) && s.pos == 3);
    seq_clock_on(&s, &o);                                           /* wraps, legato off -> on */
    CHECK(OUT(o, 64, 60) && s.pos == 0);

    seq_stop(&s, &o);
    CHECK(OUT(o, 60, -1) && s.state == SEQ_STOPPED && s.note == -1);
    seq_stop(&s, &o);
    CHECK(OUT(o, -1, -1));
    seq_clock_on(&s, &o);
    CHECK(OUT(o, -1, -1));

    seq_play_start(&s, &o);                                         /* always from step 1 */
    seq_clock_on(&s, &o);
    CHECK(OUT(o, -1, 60) && s.pos == 0);

    seq_record_start(&s, &o);                                       /* Record while playing */
    CHECK(OUT(o, 60, -1) && s.state == SEQ_RECORDING && s.len == 0);
    seq_clock_on(&s, &o);
    CHECK(OUT(o, -1, -1));
    seq_record_add(&s, 48);
    CHECK(seq_play_start(&s, &o) == 1 && s.state == SEQ_PLAYING);   /* Play ends recording */
    seq_clock_on(&s, &o);
    CHECK(OUT(o, -1, 48));
    seq_clock_off(&s, &o);
    CHECK(OUT(o, 48, -1));                                          /* one step: next is itself */
}

static void test_ties(void)
{
    static const uint8_t steps[] = {60, SEQ_TIE, SEQ_TIE, 62, SEQ_REST, SEQ_TIE};
    seq_t s;
    seq_out_t o;
    seq_init(&s);
    record(&s, steps, 6);
    seq_play_start(&s, &o);

    seq_clock_on(&s, &o);
    CHECK(OUT(o, -1, 60));
    seq_clock_off(&s, &o);
    CHECK(OUT(o, -1, -1) && s.note == 60);      /* next is a tie: held */
    seq_clock_on(&s, &o);
    CHECK(OUT(o, -1, -1) && s.pos == 1);
    seq_clock_off(&s, &o);
    CHECK(OUT(o, -1, -1) && s.note == 60);
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
    CHECK(OUT(o, -1, -1) && s.pos == 5 && s.note == -1);   /* tie after a rest stays silent */
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
    CHECK(OUT(o, -1, -1) && s.note == 67);
    seq_clock_on(&s, &o);
    CHECK(OUT(o, -1, -1) && s.pos == 0 && s.note == 67);
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
    seq_init(&s);
    seq_record_start(&s, &o);
    for (int i = 0; i < SEQ_MAX_STEPS; i++)
        seq_record_add(&s, (uint8_t)(20 + i));
    seq_play_start(&s, &o);
    for (int lap = 0; lap < 3; lap++)
        for (int i = 0; i < SEQ_MAX_STEPS; i++) {
            seq_clock_on(&s, &o);
            CHECK(o.on == 20 + i && s.pos == i);
            seq_clock_off(&s, &o);
            CHECK(o.off == 20 + i);
        }
}

int main(void)
{
    test_record();
    test_play();
    test_ties();
    test_long();
    printf("seq_core: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
