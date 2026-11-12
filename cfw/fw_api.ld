/* Stock firmware (fw 1563) functions and data used by cfw code.
 * Addresses come from analysis/symbols.txt; functions carry the Thumb bit. */

/* feature params (confsys) */
PROVIDE(confsys_get_param  = 0x08016C50 | 1);
PROVIDE(confsys_set_param  = 0x08016C88 | 1);
PROVIDE(feat_add_listener  = 0x08016DF6 | 1);
PROVIDE(feat_scale_init    = 0x0801F750 | 1);

/* display / text */
PROVIDE(fmt_version        = 0x08027F2A | 1);
PROVIDE(str_append         = 0x08027DE4 | 1);

/* data */
PROVIDE(g_scale_intervals  = 0x08031DE0);

/* main loop / UI */
PROVIDE(confstore_poll     = 0x08016FEE | 1);
PROVIDE(confstore_register = 0x08016E7A | 1);
PROVIDE(confstore_add_loaded_cb = 0x08016F7E | 1);
PROVIDE(confstore_mark_dirty = 0x08016F9E | 1);
PROVIDE(disp_set_screen    = 0x080185C0 | 1);
PROVIDE(disp_text          = 0x0801867C | 1);
PROVIDE(ui_popup_begin     = 0x08024F5E | 1);

/* input listeners: io_listener lists A..D (max 8 each), relative-input lists (max 4 each) */
PROVIDE(io_add_listener_a  = 0x080144F2 | 1);
PROVIDE(io_add_listener_b  = 0x080144F6 | 1);
PROVIDE(io_add_listener_c  = 0x080144FA | 1);
PROVIDE(io_add_listener_d  = 0x080144FE | 1);
PROVIDE(relin_add_pos_listener  = 0x080210EA | 1);
PROVIDE(relin_add_step_listener = 0x0802112A | 1);

/* MIDI out (port objects: see g_app_ports) */
PROVIDE(midi_send3         = 0x08013F6E | 1);
PROVIDE(midi_put           = 0x08013F8E | 1);
PROVIDE(g_app_ports        = 0x2000EE40);   /* 0x2000EE24 + 0x1C: port 1, port 2 (set by main) */

/* controls / LEDs */
PROVIDE(ctl_block          = 0x08016038 | 1);
PROVIDE(ctl_unblock        = 0x08016048 | 1);
PROVIDE(led_override_color = 0x0801BC6E | 1);
PROVIDE(led_override       = 0x0801BC92 | 1);

/* io_listener lists: u8 count + 8 listener pointers (dispatchers replaced by cfw_io_dispatch_*) */
PROVIDE(g_io_list_a        = 0x2000F024);
PROVIDE(g_io_list_b        = 0x2000F048);
PROVIDE(g_io_list_c        = 0x2000F06C);
PROVIDE(g_io_list_d        = 0x2000F090);
PROVIDE(feat_add_filter    = 0x08016DF2 | 1);

/* relative-input (encoder) lists: same layout as io lists, max 4 (dispatchers replaced by cfw_relin_dispatch_*) */
PROVIDE(g_relin_list_pos   = 0x2000F210);
PROVIDE(g_relin_list_hires = 0x2000F224);
PROVIDE(g_relin_list_step  = 0x2000F238);

/* clock: free-running 24 PPQN tick counter (internal tempo or external MIDI clock) */
PROVIDE(g_clock_ticks      = 0x20000124);
PROVIDE(clock_rt_start     = 0x08013C12 | 1);
PROVIDE(clock_rt_continue  = 0x08013C24 | 1);
PROVIDE(clock_rt_stop      = 0x08013C1A | 1);
PROVIDE(ctl_is_blocked     = 0x08016058 | 1);

/* inputrelative's io listeners (turn raw encoder events into relin events) */
PROVIDE(relin_io_listener_a = 0x08020E97);
PROVIDE(relin_io_listener_c = 0x08020EBB);
PROVIDE(relin_io_listener_d = 0x08020EDB);

/* encoder layouts / pages (cc_pages.c) */
PROVIDE(g_enc_layout        = 0x2000F4E8);
PROVIDE(g_enc_page          = 0x2000F39C);
PROVIDE(enc_set_page        = 0x0801CB3A | 1);
PROVIDE(enc_assign_refresh  = 0x0801C8C4 | 1);
PROVIDE(ui_show_enc_page    = 0x080233BA | 1);
PROVIDE(ui_refresh_nav_leds = 0x080231CA | 1);

/* MIDI in / note-out listeners (seq_mode.c) */
PROVIDE(midi_port_rx_notes      = 0x080317AA | 1);
PROVIDE(inmidi_add_out_listener = 0x08020D94 | 1);
