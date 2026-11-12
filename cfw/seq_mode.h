/* SH-101 style step sequencers (pad modes Custom 1..3): entry points used by cfw_init.c. */
#ifndef SEQ_MODE_H
#define SEQ_MODE_H

#include <stdint.h>

void seq_mode_init(void);
void seq_mode_poll(void);
void seq_feat_changed(uint32_t id, uint32_t value);
/* Results as for tm_io_event / tm_enc_event (tm_mode.h). */
int seq_io_event(int list, uint32_t id);
int seq_enc_event(int list, uint32_t id, int32_t delta);
/* Listener for the stock note-out list (inmidi_add_out_listener). */
void seq_note_out(uint32_t ctl, uint32_t group, uint32_t number, uint32_t value);
/* Channel message from the MIDI port; 1 = consumed (clock note). */
int seq_midi_rx(uint32_t status, uint32_t d1, uint32_t d2);

#endif
