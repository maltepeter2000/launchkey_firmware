/* Graphics from the host (branch display-gfx): a 128 x 48 bitmap below the popup's first line.
 *
 *   F0 7D 4C 4B 02 page x n <packed> F7   n bytes (1..32) into the bitmap: page 0..5 (8 pixel rows
 *                                         each, LSB top), columns x .. x+n-1; packed = MIDI 7-bit
 *                                         groups (a byte of high bits, then up to 7 low bytes)
 *   F0 7D 4C 4B 03 F7                     show: the bitmap in pages 2..7 of the display (over an
 *                                         empty popup, opened by the first show), redrawn for
 *                                         LKG_SHOW_MS after the last show; text goes into the bitmap
 *   F0 7D 4C 4B 04 F7                     clear the bitmap
 *
 * The bitmap is drawn with the display list's pointer image (opcode 0x4p, dl_draw_ptr), in 64 x 8
 * pieces as the stock test pattern; see NOTES "Graphics on the display". */
#ifndef LK_GFX_H
#define LK_GFX_H
#include <stdint.h>

#define LKG_PAGES    6                   /* bitmap pages (48 rows) */
#define LKG_W        128
#define LKG_TOP      2                   /* first display page of the bitmap */
#define LKG_SHOW_MS  2000                /* shown this long after the last show */
#define LKG_REDRAW_MS 100                /* drawn again this often while shown (over popup redraws) */

/* 1 when msg (F0 .. F7) was a graphics message (handled), 0 otherwise. */
int lkg_sysex(const uint8_t *msg, uint32_t len);
/* main loop: draws while shown */
void lkg_poll(void);
/* display text came: stop drawing the picture over it (the next show opens the popup again) */
void lkg_stop(void);

/* for host tests: unpack MIDI 7-bit groups, returns bytes written (at most max) */
uint32_t lkg_unpack(const uint8_t *in, uint32_t n_in, uint8_t *out, uint32_t max);
uint8_t *lkg_bitmap(void);
#endif
