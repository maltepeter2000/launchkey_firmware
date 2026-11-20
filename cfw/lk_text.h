/* Display text from the host: a SysEx message on the MIDI port puts two lines into the stock
 * two-line popup (the one of the CC pages and the encoder-mode switch).
 *
 *   F0 7D 4C 4B 01 <title> 00 <value> F7      7D = non-commercial ID, 4C 4B = "LK", 01 = popup
 *
 * title and value are ASCII (20..7E), up to LKT_MAX characters each (longer is cut); without the
 * 00 the whole text is the title and the value line is empty. Pd sends it for the parameter
 * that was turned (title = its name, value = its value) and for patch changes. Every other
 * SysEx goes on to the stock handler. */
#ifndef LK_TEXT_H
#define LK_TEXT_H

#include <stdint.h>

#define LKT_MAX 18

/* 1 when msg (F0 .. F7, len bytes) was a display message: title and value hold the two lines
 * (0-terminated). 0 for anything else. No display calls: testable on the host. */
int lkt_parse(const uint8_t *msg, uint32_t len, char *title, char *value);

#endif
