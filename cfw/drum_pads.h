/* Drum pad mode: the 16 pads send notes 36..51 row by row (top row 36..43, bottom row
 * 44..51), for a fixed control layout (e.g. the Pd instruments on Bela, control channel =
 * Drums Channel). Channel and velocity stay stock.
 *
 * Pad LEDs from the host: in pad mode Drum, a note on the MIDI port on the Drums Channel with
 * note 36..51 sets that pad's LED instead of the stock note feedback:
 *   velocity = colour + 32 * mode   colour 0..31 (DRUM_PADS_PALETTE), mode 0 static,
 *                                   1 flash, 2 pulse, 3 = static (reserved)
 *   velocity 0 / note off           back to the stock LED
 * Entering pad mode Drum sends CC 3 = 127 on the Drums Channel (LED refresh request), leaving
 * it hands all pads back to the stock LEDs. */
#ifndef DRUM_PADS_H
#define DRUM_PADS_H

#include <stdint.h>

#define DRUM_PADS_BASE_NOTE 36
#define DRUM_PADS_SYNC_CC   3

/* Rewrites the note numbers of pads 0..15 in table (the stock per-control output table,
 * g_ctl_param) when pad_mode is the Drum layout. Returns the number of entries changed. */
int drum_pads_apply(uint32_t pad_mode, uint32_t *table);

/* Colour index 0..31 -> 0xRRGGBB: 0..15 bright, 16..31 the same colours dimmed. */
uint32_t drum_pads_color(uint32_t index);

/* MIDI port input (from cfw_midi_rx, before the stock handler). Returns 1 when the message
 * was a pad LED command (then it is not passed on). */
int drum_pads_midi_rx(uint32_t status, uint32_t d1, uint32_t d2);

void drum_pads_poll(void);   /* main loop */

#endif
