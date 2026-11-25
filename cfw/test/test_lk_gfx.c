/* Host test of the graphics SysEx: 7-bit unpacking and bitmap writes (cfw/lk_gfx.c). */
#include <stdio.h>
#include <string.h>
#include "../lk_gfx.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

/* the encoder the host uses (tools/gfx_test.py) */
static unsigned pack(const unsigned char *in, unsigned n, unsigned char *out)
{
    unsigned k = 0;
    for (unsigned i = 0; i < n; i += 7) {
        unsigned char hi = 0;
        unsigned g = n - i < 7 ? n - i : 7;
        for (unsigned b = 0; b < g; b++) hi |= (unsigned char)(((in[i + b] >> 7) & 1) << b);
        out[k++] = hi;
        for (unsigned b = 0; b < g; b++) out[k++] = in[i + b] & 0x7F;
    }
    return k;
}

int main(void)
{
    unsigned char raw[32], enc[40], msg[60], dec[32];
    for (int i = 0; i < 32; i++) raw[i] = (unsigned char)(i * 37 + 200);
    unsigned ne = pack(raw, 32, enc);
    CHECK(ne == 37);
    CHECK(lkg_unpack(enc, ne, dec, 32) == 32);
    CHECK(memcmp(raw, dec, 32) == 0);
    /* a page write */
    unsigned k = 0;
    msg[k++] = 0xF0; msg[k++] = 0x7D; msg[k++] = 0x4C; msg[k++] = 0x4B; msg[k++] = 0x02;
    msg[k++] = 3; msg[k++] = 100; msg[k++] = 32;
    memcpy(msg + k, enc, ne); k += ne; msg[k++] = 0xF7;
    CHECK(lkg_sysex(msg, k) == 1);
    CHECK(memcmp(lkg_bitmap() + 3 * LKG_W + 100, raw, 28) == 0);   /* clipped at column 127 */
    CHECK(lkg_bitmap()[3 * LKG_W + 99] == 0);
    /* clear */
    unsigned char clr[] = {0xF0, 0x7D, 0x4C, 0x4B, 0x04, 0xF7};
    CHECK(lkg_sysex(clr, sizeof clr) == 1);
    CHECK(lkg_bitmap()[3 * LKG_W + 100] == 0);
    /* not ours */
    unsigned char txt[] = {0xF0, 0x7D, 0x4C, 0x4B, 0x01, 'a', 0, 'b', 0xF7};
    CHECK(lkg_sysex(txt, sizeof txt) == 0);
    unsigned char other[] = {0xF0, 0x00, 0x20, 0x29, 0x02, 0x13, 0xF7};
    CHECK(lkg_sysex(other, sizeof other) == 0);
    printf("test_lk_gfx: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
