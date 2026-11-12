/* Host tests for cfw/seq_core.c. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../seq_core.h"

static int failures, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static seq_data_t d;
static seq_t s;
static seq_out_t out;

static void fresh(void)
{
    memset(&d, 0x55, sizeof d);
    memset(&s, 0x55, sizeof s);
    seq_init(&s, &d, 1);
}

/* records the steps given as a 0xFF-terminated list (single notes, SEQ_REST, SEQ_TIE) */
static void record(const uint8_t *steps)
{
    seq_record_start(&s, &out);
    for (; *steps != 0xFF; steps++)
        CHECK(seq_record_add(&s, *steps) == 1);
    seq_record_stop(&s);
}

static int no_cc(void)
{
    return out.cc[0] == SEQ_NONE && out.cc[1] == SEQ_NONE && out.cc[2] == SEQ_NONE;
}

static void test_init(void)
{
    fresh();
    CHECK(s.state == SEQ_STOPPED && d.len == 0 && d.start == 0 && d.dir == SEQ_DIR_FORWARD);
    for (int i = 0; i < SEQ_STEPS; i++) {
        CHECK(seq_step_kind(&s, i) == SEQ_REST && seq_step_notes(&s, i, 0) == 0);
        CHECK(!seq_accent(&s, i));
        for (int l = 0; l < SEQ_LANES; l++)
            CHECK(seq_cc(&s, i, l) == SEQ_NONE);
    }
    CHECK(seq_play_start(&s, &out) == 0 && s.state == SEQ_STOPPED);
    seq_clock_on(&s, &out);
    CHECK(out.n_on == 0 && out.n_off == 0 && no_cc());
    CHECK(seq_record_add(&s, 60) == 0 && seq_record_add_note(&s, 60) == 0);
    CHECK(seq_record_seek(&s, 0) == 0);
}

static void test_record_play(void)
{
    static const uint8_t steps[] = {48, SEQ_REST, 55, SEQ_TIE, 60, 0xFF};
    fresh();
    record(steps);
    CHECK(d.len == 5 && s.state == SEQ_STOPPED);
    CHECK(seq_step_kind(&s, 0) == 48 && seq_step_kind(&s, 1) == SEQ_REST && seq_step_kind(&s, 3) == SEQ_TIE);
    seq_clock_on(&s, &out);                     /* not playing */
    CHECK(out.n_on == 0);
    CHECK(seq_play_start(&s, &out) == 1 && s.state == SEQ_PLAYING && s.pos == -1);

    seq_clock_on(&s, &out);
    CHECK(s.pos == 0 && out.n_off == 0 && out.n_on == 1 && out.on[0] == 48 && !out.accent && no_cc());
    seq_clock_off(&s, &out);
    CHECK(out.n_off == 1 && out.off[0] == 48 && out.n_on == 0);
    seq_clock_off(&s, &out);                    /* nothing sounding */
    CHECK(out.n_off == 0);
    seq_clock_on(&s, &out);                     /* rest */
    CHECK(s.pos == 1 && out.n_on == 0 && out.n_off == 0);
    seq_clock_off(&s, &out);
    seq_clock_on(&s, &out);
    CHECK(s.pos == 2 && out.n_on == 1 && out.on[0] == 55);
    seq_clock_off(&s, &out);                    /* a tie follows: held */
    CHECK(out.n_off == 0);
    seq_clock_on(&s, &out);                     /* tie */
    CHECK(s.pos == 3 && out.n_on == 0 && out.n_off == 0);
    seq_clock_off(&s, &out);
    CHECK(out.n_off == 1 && out.off[0] == 55);
    seq_clock_on(&s, &out);
    CHECK(s.pos == 4 && out.on[0] == 60);
    seq_clock_on(&s, &out);                     /* no clock off: released by the next step */
    CHECK(s.pos == 0 && out.n_off == 1 && out.off[0] == 60 && out.n_on == 1 && out.on[0] == 48);
    seq_stop(&s, &out);
    CHECK(s.state == SEQ_STOPPED && out.n_off == 1 && out.off[0] == 48);
    seq_clock_on(&s, &out);
    CHECK(out.n_on == 0);

    /* recording again keeps the sequence: entries replace notes, the length only grows */
    seq_record_start(&s, &out);
    CHECK(s.state == SEQ_RECORDING && s.rec == 0 && d.len == 5);
    CHECK(seq_record_add(&s, 50) == 1 && d.len == 5 && seq_step_kind(&s, 0) == 50);
    CHECK(seq_record_add(&s, 200) == 0 && seq_record_add(&s, 0x82) == 0 && s.rec == 1);
    CHECK(seq_record_seek(&s, 5) == 1 && seq_record_seek(&s, 6) == 0 && seq_record_seek(&s, -1) == 0);
    CHECK(seq_record_add(&s, 62) == 1 && d.len == 6);
    CHECK(seq_record_seek(&s, 2) == 1 && s.rec == 2);
    CHECK(seq_record_add(&s, SEQ_REST) == 1 && seq_step_kind(&s, 2) == SEQ_REST && d.len == 6);
    seq_record_stop(&s);
    CHECK(s.state == SEQ_STOPPED && seq_record_seek(&s, 0) == 0);

    /* clear */
    seq_play_start(&s, &out);
    seq_clock_on(&s, &out);
    seq_clear(&s, &out);
    CHECK(out.n_off == 1 && out.off[0] == 50 && d.len == 0 && s.state == SEQ_STOPPED);
    CHECK(seq_step_kind(&s, 0) == SEQ_REST);
    seq_record_start(&s, &out);
    seq_record_add(&s, 40);
    seq_clear(&s, &out);
    CHECK(s.state == SEQ_RECORDING && s.rec == 0 && d.len == 0);
}

static void test_chords(void)
{
    uint8_t notes[SEQ_VOICES];
    fresh();
    CHECK(seq_record_add_note(&s, 60) == 0);
    seq_record_start(&s, &out);
    CHECK(seq_record_add_note(&s, 60) == 0);            /* no step yet */
    seq_record_add(&s, 60);
    CHECK(seq_record_add_note(&s, 64) == 1 && seq_record_add_note(&s, 64) == 0);
    CHECK(seq_record_add_note(&s, 60) == 0 && seq_record_add_note(&s, 128) == 0);
    CHECK(seq_record_add_note(&s, 67) == 1 && seq_record_add_note(&s, 70) == 1);
    CHECK(seq_record_add_note(&s, 72) == 0);            /* full */
    CHECK(seq_step_notes(&s, 0, notes) == 4 && notes[0] == 60 && notes[1] == 64 && notes[3] == 70);
    seq_record_add(&s, SEQ_TIE);
    CHECK(seq_record_add_note(&s, 50) == 0);            /* not onto a tie */
    seq_record_add(&s, 40);
    seq_record_seek(&s, 0);
    seq_record_add(&s, 41);                             /* replaces the whole chord */
    CHECK(seq_step_notes(&s, 0, notes) == 1 && notes[0] == 41);
    seq_record_add_note(&s, 45);
    seq_play_start(&s, &out);
    seq_clock_on(&s, &out);
    CHECK(out.n_on == 2 && out.on[0] == 41 && out.on[1] == 45);
    seq_clock_off(&s, &out);
    CHECK(out.n_off == 0);                              /* tie follows */
    seq_clock_on(&s, &out);
    seq_clock_off(&s, &out);
    CHECK(out.n_off == 2 && out.off[0] == 41 && out.off[1] == 45);
}

static void test_attributes(void)
{
    static const uint8_t steps[] = {48, 50, SEQ_TIE, 0xFF};
    fresh();
    record(steps);
    seq_set_accent(&s, 1, 1);
    seq_set_accent(&s, 2, 1);
    seq_set_accent(&s, SEQ_STEPS - 1, 1);
    seq_set_accent(&s, SEQ_STEPS, 1);                   /* ignored */
    seq_set_accent(&s, -1, 1);
    CHECK(!seq_accent(&s, 0) && seq_accent(&s, 1) && seq_accent(&s, 2) && seq_accent(&s, SEQ_STEPS - 1));
    seq_set_accent(&s, SEQ_STEPS - 1, 0);
    CHECK(!seq_accent(&s, SEQ_STEPS - 1) && seq_accent(&s, 1));
    seq_set_cc(&s, 0, 0, 100);
    seq_set_cc(&s, 2, 2, 0);
    seq_set_cc(&s, 1, 1, 127);
    seq_set_cc(&s, 1, 3, 5);                            /* ignored */
    seq_set_cc(&s, SEQ_STEPS, 0, 5);
    CHECK(seq_cc(&s, 0, 0) == 100 && seq_cc(&s, 0, 1) == SEQ_NONE && seq_cc(&s, 2, 2) == 0);
    CHECK(seq_step_notes(&s, 1, 0) == 1 && seq_step_kind(&s, 1) == 50);     /* notes untouched */

    seq_play_start(&s, &out);
    seq_clock_on(&s, &out);
    CHECK(out.on[0] == 48 && !out.accent && out.cc[0] == 100 && out.cc[1] == SEQ_NONE && out.cc[2] == SEQ_NONE);
    seq_clock_on(&s, &out);
    CHECK(out.on[0] == 50 && out.accent && out.cc[0] == SEQ_NONE && out.cc[1] == 127);
    seq_clock_on(&s, &out);                             /* tie: controllers still go out */
    CHECK(out.n_on == 0 && !out.accent && out.cc[2] == 0);
    seq_stop(&s, &out);

    /* entering notes keeps accent and controllers */
    seq_record_start(&s, &out);
    seq_record_add(&s, 30);
    seq_record_add(&s, SEQ_REST);
    seq_record_stop(&s);
    CHECK(seq_cc(&s, 0, 0) == 100 && seq_accent(&s, 1) && seq_cc(&s, 1, 1) == 127);
    seq_set_cc(&s, 0, 0, SEQ_NONE);
    CHECK(seq_cc(&s, 0, 0) == SEQ_NONE);
    seq_clear(&s, &out);
    CHECK(!seq_accent(&s, 1) && seq_cc(&s, 1, 1) == SEQ_NONE && seq_cc(&s, 2, 2) == SEQ_NONE);
}

static void test_range(void)
{
    static const uint8_t steps[] = {40, 41, 42, 43, 0xFF};
    static const uint8_t fwd[] = {41, 42, 41, 42};
    fresh();
    seq_set_start(&s, 3);
    CHECK(d.start == 0);                                /* empty */
    record(steps);
    seq_set_len(&s, 6);                                 /* longer than entered: rests */
    CHECK(d.len == 6 && seq_step_kind(&s, 5) == SEQ_REST);
    seq_set_len(&s, 100);
    CHECK(d.len == SEQ_STEPS);
    seq_set_len(&s, 0);
    CHECK(d.len == 1);
    seq_set_len(&s, 3);
    CHECK(d.len == 3 && seq_step_kind(&s, 3) == 43);    /* shortening keeps the steps */
    seq_set_start(&s, 9);
    CHECK(d.start == 2);
    seq_set_start(&s, -4);
    CHECK(d.start == 0);
    seq_set_start(&s, 1);
    seq_play_start(&s, &out);
    for (int i = 0; i < 4; i++) {
        seq_clock_on(&s, &out);
        CHECK(out.n_on == 1 && out.on[0] == fwd[i]);
    }
    seq_set_len(&s, 1);                                 /* start follows */
    CHECK(d.start == 0);
    seq_clock_on(&s, &out);
    seq_clock_on(&s, &out);
    CHECK(s.pos == 0 && out.on[0] == 40);
    seq_set_len(&s, 4);
    seq_set_start(&s, 2);                               /* range moved away from the position */
    seq_clock_on(&s, &out);
    CHECK(s.pos == 2);
    seq_clock_on(&s, &out);
    seq_clock_on(&s, &out);
    CHECK(s.pos == 2);
    seq_record_start(&s, &out);
    seq_record_seek(&s, 4);
    seq_set_len(&s, 2);                                 /* record position stays reachable */
    CHECK(s.rec == 2);
}

static void test_directions(void)
{
    static const uint8_t steps[] = {40, 41, 42, 43, 0xFF};
    static const uint8_t rev[] = {43, 42, 41, 40, 43, 42};
    static const uint8_t alt[] = {40, 41, 42, 43, 42, 41, 40, 41, 42};
    static const uint8_t alt2[] = {41, 42, 41, 42};
    int seen[4] = {0};
    fresh();
    record(steps);
    d.dir = SEQ_DIR_REVERSE;
    seq_play_start(&s, &out);
    for (unsigned i = 0; i < sizeof rev; i++) {
        seq_clock_on(&s, &out);
        CHECK(out.on[0] == rev[i]);
    }
    d.dir = SEQ_DIR_ALTERNATE;
    seq_play_start(&s, &out);
    for (unsigned i = 0; i < sizeof alt; i++) {
        seq_clock_on(&s, &out);
        CHECK(out.on[0] == alt[i]);
    }
    seq_set_start(&s, 1);
    seq_set_len(&s, 3);
    seq_play_start(&s, &out);
    for (unsigned i = 0; i < sizeof alt2; i++) {
        seq_clock_on(&s, &out);
        CHECK(out.on[0] == alt2[i]);
    }
    seq_set_start(&s, 2);                               /* one step: stays */
    for (int i = 0; i < 3; i++) {
        seq_clock_on(&s, &out);
        CHECK(out.on[0] == 42);
    }
    seq_set_start(&s, 0);
    seq_set_len(&s, 4);
    d.dir = SEQ_DIR_RANDOM;
    seq_play_start(&s, &out);
    for (int i = 0; i < 200; i++) {
        seq_clock_on(&s, &out);
        CHECK(s.pos >= 0 && s.pos < 4 && out.on[0] == 40 + s.pos);
        seen[s.pos]++;
    }
    CHECK(seen[0] > 20 && seen[1] > 20 && seen[2] > 20 && seen[3] > 20);
    seq_set_start(&s, 2);
    for (int i = 0; i < 50; i++) {
        seq_clock_on(&s, &out);
        CHECK(s.pos >= 2 && s.pos < 4);
    }

    /* a new direction counts from the next clock on */
    seq_set_start(&s, 0);
    seq_set_dir(&s, SEQ_DIR_FORWARD);
    seq_set_dir(&s, 7);                                 /* ignored */
    CHECK(d.dir == SEQ_DIR_FORWARD);
    seq_play_start(&s, &out);
    seq_clock_on(&s, &out);
    seq_clock_on(&s, &out);                             /* at 41 */
    seq_set_dir(&s, SEQ_DIR_REVERSE);
    seq_clock_on(&s, &out);
    CHECK(out.on[0] == 40);
    seq_clock_on(&s, &out);
    CHECK(out.on[0] == 43);
    seq_set_dir(&s, SEQ_DIR_FORWARD);
    seq_clock_on(&s, &out);
    CHECK(out.on[0] == 40);

    /* ties look at the step that really comes next */
    static const uint8_t tied[] = {40, SEQ_TIE, 42, 0xFF};
    fresh();
    record(tied);
    d.dir = SEQ_DIR_REVERSE;
    seq_play_start(&s, &out);
    seq_clock_on(&s, &out);                             /* 42, next is the tie */
    CHECK(out.on[0] == 42);
    seq_clock_off(&s, &out);
    CHECK(out.n_off == 0);
    seq_clock_on(&s, &out);                             /* tie, next is 40 */
    seq_clock_off(&s, &out);
    CHECK(out.n_off == 1 && out.off[0] == 42);
    seq_clock_on(&s, &out);                             /* 40, next is 42 */
    seq_clock_off(&s, &out);
    CHECK(out.n_off == 1 && out.off[0] == 40);
}

static void test_full_and_sanitize(void)
{
    fresh();
    seq_record_start(&s, &out);
    for (int i = 0; i < SEQ_STEPS; i++)
        CHECK(seq_record_add(&s, (uint8_t)(20 + i)) == 1);
    CHECK(d.len == SEQ_STEPS && s.state == SEQ_RECORDING && s.rec == SEQ_STEPS);
    CHECK(seq_record_add_note(&s, 100) == 1);
    CHECK(seq_record_add(&s, 99) == 0 && s.state == SEQ_STOPPED && d.len == SEQ_STEPS);
    CHECK(seq_step_kind(&s, SEQ_STEPS - 1) == 20 + SEQ_STEPS - 1);

    /* a record with nonsense in it */
    memset(&d, 0xEE, sizeof d);
    s.state = SEQ_PLAYING;
    seq_sanitize(&s);
    CHECK(d.len == SEQ_STEPS && d.start == 0 && d.dir == SEQ_DIR_FORWARD);
    for (int i = 0; i < SEQ_STEPS; i++) {
        CHECK(seq_step_kind(&s, i) == SEQ_REST && seq_step_notes(&s, i, 0) == 0);
        CHECK(seq_cc(&s, i, 0) == SEQ_NONE && seq_cc(&s, i, 2) == SEQ_NONE);
    }
    for (int i = 0; i < 2 * SEQ_STEPS; i++) {
        seq_clock_on(&s, &out);
        CHECK(out.n_on == 0 && s.pos == i % SEQ_STEPS);
    }
    d.len = 0;
    seq_sanitize(&s);
    CHECK(s.state == SEQ_STOPPED);
    /* a valid record is left alone */
    static const uint8_t steps[] = {40, SEQ_TIE, 0xFF};
    seq_data_t copy;
    fresh();
    record(steps);
    seq_set_cc(&s, 1, 1, 9);
    seq_set_accent(&s, 0, 1);
    d.dir = SEQ_DIR_RANDOM;
    copy = d;
    seq_sanitize(&s);
    CHECK(!memcmp(&copy, &d, sizeof d));
}

int main(void)
{
    test_init();
    test_record_play();
    test_chords();
    test_attributes();
    test_range();
    test_directions();
    test_full_and_sanitize();
    printf("seq_core: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
