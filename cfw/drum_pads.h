/* Drum pad mode: the 16 pads send notes 36..51 row by row (top row 36..43, bottom row
 * 44..51), for a fixed control layout (e.g. the Pd TX81Z on Bela, control channel = Drums
 * Channel). Channel, velocity and LEDs stay stock. */
#ifndef DRUM_PADS_H
#define DRUM_PADS_H

#include <stdint.h>

#define DRUM_PADS_BASE_NOTE 36

/* Rewrites the note numbers of pads 0..15 in table (the stock per-control output table,
 * g_ctl_param) when pad_mode is the Drum layout. Returns the number of entries changed. */
int drum_pads_apply(uint32_t pad_mode, uint32_t *table);

void drum_pads_poll(void);   /* main loop */

#endif
