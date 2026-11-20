/* Host tests for cfw/lk_text.c (parsing only). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../lk_text.h"

static int failures, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static uint32_t build(uint8_t *m, const char *t, const char *v, int with_sep)
{
    uint32_t n = 0;
    const uint8_t h[] = {0xF0, 0x7D, 0x4C, 0x4B, 0x01};
    memcpy(m, h, sizeof h); n = sizeof h;
    while (*t) m[n++] = (uint8_t)*t++;
    if (with_sep) { m[n++] = 0; while (*v) m[n++] = (uint8_t)*v++; }
    m[n++] = 0xF7;
    return n;
}

int main(void)
{
    uint8_t m[128];
    char t[LKT_MAX + 1], v[LKT_MAX + 1];
    uint32_t n = build(m, "VCF freq", "64", 1);
    CHECK(lkt_parse(m, n, t, v) == 1);
    CHECK(strcmp(t, "VCF freq") == 0 && strcmp(v, "64") == 0);

    n = build(m, "Init", "", 0);                         /* title only */
    CHECK(lkt_parse(m, n, t, v) == 1 && strcmp(t, "Init") == 0 && v[0] == 0);

    n = build(m, "A very long title of a patch", "and a long value text too", 1);
    CHECK(lkt_parse(m, n, t, v) == 1 && strlen(t) == LKT_MAX && strlen(v) == LKT_MAX);
    CHECK(strncmp(t, "A very long title ", LKT_MAX) == 0);

    n = build(m, "x", "y", 1);
    m[5] = 0x7F; m[7] = 0x01;                            /* non-printable -> '?' */
    CHECK(lkt_parse(m, n, t, v) == 1 && strcmp(t, "?") == 0 && strcmp(v, "?") == 0);

    n = build(m, "x", "y", 1);
    CHECK(lkt_parse(m, n - 1, t, v) == 1 && strcmp(v, "y") == 0);   /* without F7 */

    const uint8_t other[] = {0xF0, 0x00, 0x20, 0x29, 0x02, 0x13, 0x04, 0xF7};
    CHECK(lkt_parse(other, sizeof other, t, v) == 0);
    const uint8_t juno[] = {0xF0, 0x41, 0x30, 0x00, 0xF7};
    CHECK(lkt_parse(juno, sizeof juno, t, v) == 0);
    CHECK(lkt_parse(m, 3, t, v) == 0);                    /* too short */

    printf("test_lk_text: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
