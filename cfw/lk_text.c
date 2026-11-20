/* Display text from the host, see lk_text.h. */
#include "fw_api.h"
#include "lk_text.h"

static const uint8_t header[] = {0xF0, 0x7D, 0x4C, 0x4B, 0x01};

static uint32_t copy_line(const uint8_t *s, uint32_t n, char *out)
{
    uint32_t i = 0, k = 0;
    for (; i < n && s[i] != 0x00 && s[i] != 0xF7; i++)
        if (k < LKT_MAX)
            out[k++] = (s[i] >= 0x20 && s[i] < 0x7F) ? (char)s[i] : '?';
    out[k] = 0;
    return i;
}

int lkt_parse(const uint8_t *msg, uint32_t len, char *title, char *value)
{
    if (len < sizeof header)
        return 0;
    for (uint32_t i = 0; i < sizeof header; i++)
        if (msg[i] != header[i])
            return 0;
    const uint8_t *p = msg + sizeof header;
    uint32_t n = len - sizeof header;
    uint32_t used = copy_line(p, n, title);
    value[0] = 0;
    if (used < n && p[used] == 0x00)
        copy_line(p + used + 1, n - used - 1, value);
    return 1;
}

#ifndef LKT_HOST_TEST
/* Replaces the MIDI port's SysEx handler (its address in main's handler table, see hooks.txt). */
void cfw_midi_sysex(midi_port_t *port, const uint8_t *msg, uint32_t len)
{
    char title_buf[LKT_MAX + 1], value_buf[LKT_MAX + 1];   /* disp_text copies (as in cc_pages.c) */
    if (lkt_parse(msg, len, title_buf, value_buf)) {
        ui_popup_begin();
        disp_set_screen(1, 0);
        disp_text(1, 0, title_buf);
        disp_text(1, 1, value_buf);
        return;
    }
    port_rx_sysex(port, msg, len);
}
#endif
