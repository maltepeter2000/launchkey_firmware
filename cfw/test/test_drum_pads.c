/* Host tests for cfw/drum_pads.c. */
#include <stdint.h>
#include <stdio.h>
#include "../fw_api.h"
#include "../drum_pads.h"

static int failures, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

uint32_t g_ctl_param[256];
static uint32_t pad_mode;

void confsys_get_param(uint32_t group, uint32_t id, fw_param_t *out)
{
    CHECK(group == FW_GROUP_FEATURE && id == 0x1D);
    out->value = pad_mode;
    out->flags = 0;
}

/* a stock drum layout: notes on channel 10 (group 9), bottom-row-first 4x2 blocks */
static void stock_drum(void)
{
    static const uint8_t stock[16] = {40, 41, 42, 43, 48, 49, 50, 51, 36, 37, 38, 39, 44, 45, 46, 47};
    for (int i = 0; i < 256; i++) g_ctl_param[i] = 0;
    for (int i = 0; i < 16; i++) g_ctl_param[i] = 0x20000000u | (9u << 16) | stock[i];
    g_ctl_param[16] = 0x20000000u | (9u << 16) | 99;          /* not a pad: untouched */
}

int main(void)
{
    /* drum mode: rows */
    stock_drum();
    pad_mode = 1;
    drum_pads_poll();
    for (int i = 0; i < 16; i++) {
        CHECK((g_ctl_param[i] & 0xFFFF) == (uint32_t)(36 + i));
        CHECK((g_ctl_param[i] >> 16) == ((0x20000000u | (9u << 16)) >> 16));   /* type + channel kept */
    }
    CHECK((g_ctl_param[16] & 0xFFFF) == 99);
    CHECK(drum_pads_apply(1, g_ctl_param) == 0);              /* already done: no writes */

    /* other pad modes: untouched */
    stock_drum();
    for (uint32_t m = 0; m < 10; m++) {
        if (m == 1) continue;
        pad_mode = m;
        drum_pads_poll();
        CHECK((g_ctl_param[0] & 0xFFFF) == 40);
    }

    /* not notes (e.g. CC group 0x1x) or not a param entry: untouched */
    stock_drum();
    g_ctl_param[3] = 0x20000000u | (0x19u << 16) | 20;
    g_ctl_param[4] = 0x40000000u | (9u << 16) | 48;
    drum_pads_apply(1, g_ctl_param);
    CHECK((g_ctl_param[3] & 0xFFFF) == 20);
    CHECK((g_ctl_param[4] & 0xFFFF) == 48);
    CHECK((g_ctl_param[5] & 0xFFFF) == 41);

    printf("test_drum_pads: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
