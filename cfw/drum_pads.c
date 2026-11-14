/* Drum pad mode with the pads in rows: see drum_pads.h and NOTES "Drum pads". */
#include "drum_pads.h"
#include "fw_api.h"

#define FEAT_PAD_MODE   0x1D    /* pad layout index (layout_select) */
#define PAD_MODE_DRUM   1       /* ui_layout_name: pad layout 1 = string 0xCA "Drum" */
#define N_PADS          16      /* control ids 0..7 top row, 8..15 bottom row */

/* g_ctl_param entry: bits 29..31 = 1 (confsys param), bits 16..23 = group (< 0x10 = note on
 * that channel), bits 0..15 = id (the note number). */
#define ENTRY_TYPE_MASK 0xE0000000u
#define ENTRY_TYPE_PARAM 0x20000000u

int drum_pads_apply(uint32_t pad_mode, uint32_t *table)
{
    int changed = 0;
    if (pad_mode != PAD_MODE_DRUM)
        return 0;
    for (uint32_t id = 0; id < N_PADS; id++) {
        uint32_t e = table[id];
        if ((e & ENTRY_TYPE_MASK) != ENTRY_TYPE_PARAM)
            continue;
        if (((e >> 16) & 0xF0) != 0)            /* not a note group */
            continue;
        uint32_t note = DRUM_PADS_BASE_NOTE + id;
        if ((e & 0xFFFF) != note) {
            table[id] = (e & 0xFFFF0000u) | note;
            changed++;
        }
    }
    return changed;
}

void drum_pads_poll(void)
{
    fw_param_t p;
    confsys_get_param(FW_GROUP_FEATURE, FEAT_PAD_MODE, &p);
    drum_pads_apply(p.value, g_ctl_param);
}
