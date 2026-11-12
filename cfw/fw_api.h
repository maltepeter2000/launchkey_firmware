/* Stock firmware (fw 1563) API used by cfw code. Addresses: cfw/fw_api.ld. */
#ifndef FW_API_H
#define FW_API_H

#include <stdint.h>

/* Feature parameter value as passed to confsys_{get,set}_param. flags on set:
 * bit0 = definition (first one wins, no listeners), bit1 = persistent. */
typedef struct {
    uint32_t value;
    uint32_t flags;
} fw_param_t;

#define FW_GROUP_FEATURE 0xD0

/* group 0xD0 = feature params; ids: 0x4A scale on/off, 0x3E scale type, 0x3D root, 0x3C scale mode */
void confsys_get_param(uint32_t group, uint32_t id, fw_param_t *out);
void confsys_set_param(uint32_t group, uint32_t id, const fw_param_t *in, uint32_t r3);

/* filter(id, old, new) runs before a (non-definition) feature param change is stored and
 * returns the value to store; returning old vetoes the change. 5 of 8 slots used. */
void feat_add_filter(uint32_t (*filter)(uint32_t id, uint32_t old, uint32_t value));

/* listener(id, value) called after a feature param changed */
void feat_add_listener(void (*listener)(uint32_t id, uint32_t value));

void feat_scale_init(void);

/* Appends "a.b.c.d" from ver[4] to buf at pos (bounded by size); returns the new pos. */
uint32_t fmt_version(char *buf, uint32_t pos, uint32_t size, const uint32_t *ver);

/* Appends str to the NUL-terminated buf (bounded by size, always terminated); returns the new length. */
uint32_t str_append(char *buf, uint32_t pos, uint32_t size, const char *str);

/* 30 x 12 bytes, index = scale type (1..30); non-zero = semitone in scale */
extern const uint8_t g_scale_intervals[][12];

/* Main loop: called several times per pass; cfw_poll hooks one of these calls. */
void confstore_poll(void);

/* Settings store (external SPI flash, see NOTES "Settings store"): a registered buffer of up
 * to 248 bytes is filled from its record at boot and written delay_ms after it was marked
 * dirty. Register at feature-init time (before the store is read); loaded callbacks (4 of 8
 * used by stock fw) run once all records are in. */
void confstore_register(uint32_t key, void *buf, uint32_t size, uint32_t delay_ms);
void confstore_add_loaded_cb(void (*fn)(void));
void confstore_mark_dirty(uint32_t key);

/* Display: screen 1 + template 0 = two-line popup (field 0 title, field 1 value), as used by
 * the encoder-mode switch. disp_text sets a field and redraws. ui_popup_begin notifies the UI
 * that a popup starts (called by the stock popups before drawing). */
void disp_set_screen(uint32_t screen, uint32_t template_id);
void disp_text(uint32_t screen, uint32_t field, const char *str);
void ui_popup_begin(void);

/* Input listeners (all called from the main loop). io lists: control id < 0x78. */
typedef void (*io_listener_fn)(uint32_t id, uint32_t value);
void io_add_listener_a(io_listener_fn fn);   /* 7 of 8 slots used by stock fw */
void io_add_listener_b(io_listener_fn fn);   /* 1 of 8 */
void io_add_listener_c(void (*fn)(uint32_t id));  /* 5 of 8 */
void io_add_listener_d(io_listener_fn fn);   /* 7 of 8 */

/* The io lists themselves (count + listeners); cfw replaces the stock dispatchers
 * (0x0801B530 A, 0x0801B54C B, 0x0801B574 C, 0x0801B592 D) to filter events. */
typedef struct {
    uint8_t count;
    void *fn[8];
} io_list_t;
extern io_list_t g_io_list_a, g_io_list_b, g_io_list_c, g_io_list_d;

/* The encoder module (inputrelative.cpp) derives relin events from raw encoder io events
 * through these io listeners (registered on lists A, C, D). */
void relin_io_listener_a(uint32_t id, uint32_t value);
void relin_io_listener_c(uint32_t id);
void relin_io_listener_d(uint32_t id, uint32_t value);

/* Encoders: (encoder id, accumulated value, delta). Lists use io_list_t (max 4 entries);
 * cfw replaces the dispatchers (0x08020E48 pos, 0x08020E56 hires, 0x08020E8A step). */
typedef void (*relin_listener_fn)(uint32_t id, uint32_t value, int32_t delta);
extern io_list_t g_relin_list_pos, g_relin_list_hires, g_relin_list_step;
void relin_add_pos_listener(relin_listener_fn fn);   /* 2 of 4 */
void relin_add_step_listener(relin_listener_fn fn);  /* 2 of 4 */

/* MIDI out. A port is a USB-MIDI packetiser object living on main()'s stack; the two
 * app ports are stored at g_app_ports[0..1]. midi_put streams bytes (SysEx F0..F7 of
 * any length, packed into USB-MIDI packets); midi_send3 sends one 3-byte message. */
typedef struct midi_port midi_port_t;
extern midi_port_t *g_app_ports[2];   /* written by main() at init */
void midi_put(midi_port_t *port, uint32_t byte);
void midi_send3(midi_port_t *port, uint32_t status, uint32_t d1, uint32_t d2);

/* MIDI in. midi_port_rx_notes is the MIDI port's (USB cable 0) handler for note on/off, CC
 * and program change, called from the main loop by the port's message handler (0x0803048C)
 * with (port, status, data1, data2); returns 1 if it handled the message. cfw hooks that
 * call (cfw_midi_rx). */
uint32_t midi_port_rx_notes(midi_port_t *port, uint32_t status, uint32_t d1, uint32_t d2);

/* Note-out listeners (inputmidi_listener.cpp, 1 of 8 used): called with (control id, group,
 * number, value) for every message a control sends; group < 0x10 = note on channel `group`,
 * value = velocity (0 = note off). Keys are control ids 0x80.. */
void inmidi_add_out_listener(void (*fn)(uint32_t ctl, uint32_t group, uint32_t number, uint32_t value));

/* Controls: ids 0..15 = pads (0..7 top row), 56..63 = encoders 1..8, 25 = Shift,
 * 0x20 / 0x21 = Octave - / +, 0x48 = Play, 0x4A = Record, 0x80.. = keys (see NOTES).
 * ctl_block makes the stock app ignore a control (no notes / CCs); the input listeners
 * still see it. */
void ctl_block(uint32_t id);
void ctl_unblock(uint32_t id);
/* Also set while a menu (Settings, Shift overlay, ...) has captured the control
 * (ui_menumgr capture @0x08025712). */
uint32_t ctl_is_blocked(uint32_t id);

/* LED override layer (drawn instead of the pad mode's colours while active):
 * led_override(id, 0 static / 1 flash / 2 pulse) turns it on, led_override(id, 3) off.
 * Colours are 0xRRGGBB. */
#define LED_STATIC  0
#define LED_FLASH   1
#define LED_PULSE   2
#define LED_RELEASE 3
void led_override_color(uint32_t id, uint32_t rgb);
void led_override(uint32_t id, uint32_t mode);

/* Clock: 24 PPQN tick counter, advanced by clock_poll (0x08013C60) in the main loop from the
 * internal tempo (period in us at 0x20000114) or counted external MIDI clocks. Poll it from
 * the main loop and step on the difference, like the MIDI Clock Out sender (0x08014492). */
extern volatile uint32_t g_clock_ticks;
/* MIDI Start / Continue / Stop as received (called by midi_rx_realtime 0x08030958): Start and
 * Continue set the clock's running flag and switch it to external sync, Stop clears the flag.
 * The tick counter is not reset. */
void clock_rt_start(void);
void clock_rt_continue(void);
void clock_rt_stop(void);

/* Encoder layouts and pages (see NOTES "Encoder pages"). g_enc_layout: 2..5 = Plugin, Mixer,
 * Sends, Transport, 6..9 = Custom 1..4. g_enc_page[n] = page of Custom n+1 (stock: 0 or 1). */
extern uint32_t g_enc_layout;
extern uint32_t g_enc_page[4];
/* Stores the page (limited to 1) and re-assigns the encoders if it changed. */
void enc_set_page(uint32_t layout, uint32_t page);
/* Re-assigns the encoder controls for the current layout and page. */
void enc_assign_refresh(void);
/* Popup "Custom Controls" / "1 - 8" or "9 - 16", then ui_refresh_nav_leds. */
void ui_show_enc_page(uint32_t page);
/* Redraws the LEDs of the navigation buttons (pad and encoder up/down). */
void ui_refresh_nav_leds(void);

#endif
