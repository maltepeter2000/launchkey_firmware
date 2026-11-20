/* Drum pad mode with the pads in rows and host-driven pad LEDs: see drum_pads.h and NOTES
 * "Drum pads". */
#include "drum_pads.h"
#include "fw_api.h"

#define FEAT_PAD_MODE   0x1D    /* pad layout index (layout_select) */
#define PAD_MODE_DRUM   1       /* ui_layout_name: pad layout 1 = string 0xCA "Drum" */
#define N_PADS          16      /* control ids 0..7 top row, 8..15 bottom row */
#define MIDI_PORT       1       /* g_app_ports[1] = USB MIDI port */

/* g_ctl_param entry: bits 29..31 = 1 (confsys param), bits 16..23 = group (< 0x10 = note on
 * that channel), bits 0..15 = id (the note number). */
#define ENTRY_TYPE_MASK 0xE0000000u
#define ENTRY_TYPE_PARAM 0x20000000u

static const uint32_t palette[16] = {
    0x000000,   /*  0 off      */
    0xFFFFFF,   /*  1 white    */
    0xFF0000,   /*  2 red      */
    0xFF4000,   /*  3 orange   */
    0xFFC000,   /*  4 yellow   */
    0x00FF00,   /*  5 green    */
    0x00FFC0,   /*  6 cyan     */
    0x0040FF,   /*  7 blue     */
    0x8000FF,   /*  8 purple   */
    0xFF0080,   /*  9 pink     */
    0x80FF00,   /* 10 lime     */
    0x00A0A0,   /* 11 teal     */
    0xFF00FF,   /* 12 magenta  */
    0xFF8000,   /* 13 amber    */
    0x00A0FF,   /* 14 sky      */
    0xFFA060,   /* 15 warm     */
};

static uint16_t owned;          /* pads with an LED override from the host */
static uint8_t in_drum;         /* pad mode Drum seen by the last poll */

uint32_t drum_pads_color(uint32_t index)
{
    uint32_t rgb = palette[index & 15];
    if (index & 16)             /* dimmed: a quarter */
        rgb = (rgb >> 2) & 0x3F3F3F;
    return rgb;
}

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

/* MIDI channel of the pads (the Drums Channel), from pad 0's output entry; -1 if no notes. */
static int pad_channel(void)
{
    uint32_t e = g_ctl_param[0];
    if ((e & ENTRY_TYPE_MASK) != ENTRY_TYPE_PARAM || ((e >> 16) & 0xF0) != 0)
        return -1;
    return (int)((e >> 16) & 0xF);
}

int drum_pads_midi_rx(uint32_t status, uint32_t d1, uint32_t d2)
{
    uint32_t kind = status & 0xF0;
    if (!in_drum || (kind != 0x90 && kind != 0x80))
        return 0;
    if ((int)(status & 0xF) != pad_channel())
        return 0;
    if (d1 < DRUM_PADS_BASE_NOTE || d1 >= DRUM_PADS_BASE_NOTE + N_PADS)
        return 0;
    uint32_t id = d1 - DRUM_PADS_BASE_NOTE;
    if (kind == 0x80 || d2 == 0) {
        if (owned & (1u << id)) {
            led_override(id, LED_RELEASE);
            owned &= (uint16_t)~(1u << id);
        }
        return 1;
    }
    uint32_t mode = (d2 >> 5) & 3;
    if (mode == 3)
        mode = LED_STATIC;
    led_override_color(id, drum_pads_color(d2 & 31));
    led_override(id, mode);
    owned |= (uint16_t)(1u << id);
    return 1;
}

void drum_pads_poll(void)
{
    fw_param_t p;
    confsys_get_param(FW_GROUP_FEATURE, FEAT_PAD_MODE, &p);
    drum_pads_apply(p.value, g_ctl_param);
    uint8_t drum = p.value == PAD_MODE_DRUM;
    if (drum == in_drum)
        return;
    in_drum = drum;
    if (drum) {                                 /* entered: ask the host for the LEDs */
        int ch = pad_channel();
        if (ch >= 0)
            midi_send3(g_app_ports[MIDI_PORT], 0xB0u | (uint32_t)ch, DRUM_PADS_SYNC_CC, 127);
    } else {                                    /* left: stock LEDs again */
        for (uint32_t id = 0; id < N_PADS; id++)
            if (owned & (1u << id))
                led_override(id, LED_RELEASE);
        owned = 0;
    }
}
