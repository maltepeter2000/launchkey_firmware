/* cfw entry points called from hooks in the stock firmware (see cfw/hooks.txt). */
#include <stdint.h>
#include "fw_api.h"
#include "tm_mode.h"
#include "cc_pages.h"
#include "seq_mode.h"

extern uint32_t __cfw_bss_start[], __cfw_bss_end[];

/* Hook: replaces `bl feat_scale_init` at the end of feature init (0x0801592C). */
void cfw_init(void)
{
    feat_scale_init();
    for (uint32_t *p = __cfw_bss_start; p < __cfw_bss_end; p++)
        *p = 0;
    tm_mode_init();
    seq_mode_init();
}

/* One feature listener for all cfw modes (the stock list has 8 slots, 6 used). */
static void feat_changed(uint32_t id, uint32_t value)
{
    tm_feat_changed(id, value);
    seq_feat_changed(id, value);
}

/* Hook: replaces one `bl confstore_poll` in the main loop (0x0801048A). */
void cfw_poll(void)
{
    static uint8_t started;
    confstore_poll();
    if (!started) {             /* first pass: all stock init is done */
        started = 1;
        feat_add_listener(feat_changed);
        inmidi_add_out_listener(seq_note_out);
    }
    tm_mode_poll();
    seq_mode_poll();
}

/* Hook: replaces `bl midi_port_rx_notes` in the MIDI port's message handler (0x0803049A).
 * The sequencer takes its clock note; everything else goes to the stock handler. */
uint32_t cfw_midi_rx(midi_port_t *port, uint32_t status, uint32_t d1, uint32_t d2)
{
    if (seq_midi_rx(status, d1, d2))
        return 1;
    return midi_port_rx_notes(port, status, d1, d2);
}

/* Hooks: replace the calls for MIDI Start / Continue / Stop in midi_rx_realtime (0x08030968,
 * 0x08030972, 0x0803097C; both USB ports end up there). The stock clock still gets them. */
void cfw_rt_start(void)
{
    clock_rt_start();
    seq_transport(1);
}

void cfw_rt_continue(void)
{
    clock_rt_continue();
    seq_transport(1);
}

void cfw_rt_stop(void)
{
    clock_rt_stop();
    seq_transport(0);
}

/* Hook: replaces `bl fmt_version` in ui_show_boot_screen (0x08016E42). Visible proof that cave code runs. */
uint32_t cfw_boot_version(char *buf, uint32_t pos, uint32_t size, const uint32_t *ver)
{
    pos = fmt_version(buf, pos, size, ver);
    return str_append(buf, pos, size, "+cfw");
}

/* ---- input dispatchers: replace the stock ones (see cfw/hooks.txt) ---- */

/* Turing Machine mode first; what it passes on may belong to the step sequencers
 * (seq_mode.c: buttons, pads and, in their pad modes, the encoders) or the CC pages
 * (cc_pages.c). */
static int io_event(int list, uint32_t id, uint32_t v)
{
    int r = tm_io_event(list, id, v);
    if (r == TM_PASS)
        r = seq_io_event(list, id);
    return r == TM_PASS ? ccp_io_event(list, id) : r;
}

static int enc_event(int list, uint32_t id, int32_t d)
{
    return tm_enc_event(list, id, d) || seq_enc_event(list, id, d) || ccp_enc_event(list, id, d);
}

typedef void (*io2_fn)(uint32_t, uint32_t);
typedef void (*io1_fn)(uint32_t);

static void call_list2(const io_list_t *l, uint32_t id, uint32_t v)
{
    for (unsigned i = 0; i < l->count; i++)
        ((io2_fn)l->fn[i])(id, v);
}

void cfw_io_dispatch_a(uint32_t id, uint32_t v)
{
    int r = io_event(0, id, v);
    if (r == TM_PASS)
        call_list2(&g_io_list_a, id, v);
    else if (r == TM_ENC_ONLY)
        relin_io_listener_a(id, v);
}

void cfw_io_dispatch_b(uint32_t id, uint32_t v)
{
    if (io_event(1, id, v) == TM_PASS)
        call_list2(&g_io_list_b, id, v);
}

void cfw_io_dispatch_d(uint32_t id, uint32_t v)
{
    int r = io_event(3, id, v);
    if (r == TM_PASS)
        call_list2(&g_io_list_d, id, v);
    else if (r == TM_ENC_ONLY)
        relin_io_listener_d(id, v);
}

void cfw_io_dispatch_c(uint32_t id)
{
    int r = io_event(2, id, 0);
    if (r == TM_ENC_ONLY) {
        relin_io_listener_c(id);
    } else if (r == TM_PASS) {
        for (unsigned i = 0; i < g_io_list_c.count; i++)
            ((io1_fn)g_io_list_c.fn[i])(id);
    }
}

static void call_list3(const io_list_t *l, uint32_t id, uint32_t v, int32_t d)
{
    for (unsigned i = 0; i < l->count; i++)
        ((relin_listener_fn)l->fn[i])(id, v, d);
}

void cfw_relin_dispatch_pos(uint32_t id, uint32_t v, int32_t d)
{
    if (!enc_event(0, id, d))
        call_list3(&g_relin_list_pos, id, v, d);
}

void cfw_relin_dispatch_hires(uint32_t id, uint32_t v, int32_t d)
{
    if (!enc_event(1, id, d))
        call_list3(&g_relin_list_hires, id, v, d);
}

void cfw_relin_dispatch_step(uint32_t id, uint32_t v, int32_t d)
{
    if (!enc_event(2, id, d))
        call_list3(&g_relin_list_step, id, v, d);
}
