/* CC pages for encoder mode Custom 1: entry points used by cfw_init.c (dispatchers).
 * The hooks cfw_enc_page_count / cfw_enc_set_page / cfw_enc_page_popup are listed in hooks.txt. */
#ifndef CC_PAGES_H
#define CC_PAGES_H

#include <stdint.h>

/* Results as for tm_io_event / tm_enc_event (tm_mode.h). */
int ccp_io_event(int list, uint32_t id);
int ccp_enc_event(int list, uint32_t id, int32_t delta);
int ccp_midi_rx(uint32_t status, uint32_t d1, uint32_t d2);   /* feedback: reads only, returns 0 */

uint32_t cfw_enc_page_count(uint32_t layout);
void cfw_enc_set_page(uint32_t layout, uint32_t new_page);
void cfw_enc_page_popup(uint32_t new_page);

#endif
