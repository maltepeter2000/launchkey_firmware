# Analysis notes – Launchkey Mini MK4 fw 1563

## Confirmed

- SysEx container decoded; see CLAUDE.md. `make verify` and the CRC check both pass.
- Load address 0x08008000 confirmed: VTOR write in Reset_Handler, and the most Thumb pointers resolve to function prologues at this base.
- 56 external IRQ vectors populated → STM32F4 family (F401/F411-class likely, not confirmed).
- Hopper (via REA) finds 2096 procedures.

## Open questions

- [ ] Exact MCU part / flash size (look for the DBGMCU IDCODE read at 0xE0042000 and the flash-size register).
- [x] Where settings are stored: in an **external SPI NOR flash** (`serstor_hw.c` @~0x08011B24 issues 0x02 page
      program, 0x03 read, 0x20/0x52/0xD8 erase, 0x05 status). The app never unlocks internal flash (no FLASH_KEYR
      keys in the image; FLASH regs only touched for ACR latency/caches), so updates are staged in SPI flash and
      installed by the bootloader. -> 0x080372B3..0x0803FFFF of the 0x38000 app slot is free for code
      (36 173 bytes). Exact SPI address map of the settings / update staging area still unknown.
- [x] Bootloader installs a grown image: Phase 0 cfw build (193 332 bytes, code at 0x080372B4, hook replacing
      `bl feat_scale_init`) flashed 2026-11-10, device boots and works normally -> code past the old end runs.
- [x] ID bytes `01 03 0c 1b 0d 35` = firmware version: 1.3, build 1563 (0x0c<<7|0x1b), 1717 (0x0d<<7|0x35).
      Same values as u32s at 0x080081F0 (`g_fw_version`). Still unknown whether the bootloader checks them.
      App side: boot screen prints all 4 fields; inquiry reply (0x080115B6) sends major, minor, build only;
      0x7C header version is just stored in the update state (0x0801B92C), not compared. Patched builds put the
      patch count in field 4 (see apply_patches.py).
- [x] Does the bootloader accept an image larger than the original (code-cave growth)? Yes (see above).
- [x] MIDI input dispatch / SysEx handler: see "MIDI input, keys and buttons".
- [ ] Recovery: bootloader mode button combo, in case a patched image doesn't boot.

## Memory budget (fw 1563)

- Flash: app slot 0x08008000..0x0803FFFF = 0x38000 (updater limit), image 193 203 bytes (84 %), **35.3 KiB free**.
- RAM (64 KiB): RW data 0x20000000..0x200015D0 (decompressed from the image end), zero-init
  0x200015D0..0x2000F518 (scatter table @0x08033544), leaves **2 792 bytes** up to 0x20010000 for the main stack
  (initial SP 0x2000FFF8). Any new feature must find RAM inside existing buffers or share that headroom.
- cfw RAM: 0x2000F520..0x2000F860 (`cfw/cfw.ld`, 832 bytes, 828 used; 576 of them are the three step
  sequences). It was 256 bytes up to 0x2000F620 when the stack was measured (see "Stack" below), so the
  untouched stack area is now >= 896 bytes. More sequencer steps cost stack margin: 4 bytes per step.

## Modes and layouts (fw 1563)

- Layout handle = `{u32 index; u8 type}` (type 1 = pads, 2 = encoders, 3 = faders; `layout_type` @0x08029CFC).
- `layout_select(handle, kind)` @0x080159C0 stores the index in a feature param: **0x1D pad mode, 0x1E encoder
  mode, 0x1F fader mode** (`confsys_set_param(0xD0, id, {index, 0}, 2)`), so mode changes reach feature listeners.
- `ui_layout_name` @0x0802AE30: pad layouts 5..8 (or 0) = "Custom 1..4"; encoder layouts 6..9 (or 0) =
  "Custom 1..4" -> **encoder Custom 4 = 0x1E value 9**.
- Feature listener slots: 6 of 8 used (`feat_add_listener`), filters 5 of 8.
- Relative input (encoders, inputrelative.cpp): three listener lists of max 4, notified with (id, value, delta):
  `relin_notify_pos` @0x08020E48 (list 0x2000F210, 2 users), `relin_notify_hires` @0x08020E56 (0x2000F224, 1 user),
  `relin_notify_step` @0x08020E8A (0x2000F238, 2 users). Registered at 0x08015FB6.. and 0x08016636...
- **Control ids (probe 2 on hardware, 2026-11-11):** pads 0..7 = top row (left->right), 8..15 = bottom row;
  encoders 1..8 = 56..63 (0x38..0x3F); Shift = 25 (0x19), mirrored in feature param 0x3F (127/0).
  io lists: A = press (value = 14-bit velocity; encoders: touch), C = release, B/D = pressure / value stream.
  relin pos list = every tick with delta, step list = per detent (~2 ticks).
- Encoder mode select = Shift + top-row pad: pad 7 -> 0x1E = 9 (Custom 4), pad 6 -> 8 (Custom 3). The feature
  listener sees it (also 0x45 = 0 each time, meaning unknown). Custom 4 enc 1 sends CC 51 ch 1 by default.
- Mode select with Shift: **top-row pads 0..7 -> encoder mode** (pad n -> 0x1E = n + 2, pad 7 = Custom 4 = 9),
  **bottom-row pads 8..15 -> pad mode** (pad 15 -> 0x1D = 8 = pad Custom 4, pad 12 -> 5 = Custom 1).
  0x1E/0x1D only *report* the mode: vetoing 0x1E with a feature filter does not stop the layout switch.
- `ctl_block` is not usable to own controls across Shift: the Shift overlay captures/releases controls and
  clears the block flags on release. cfw instead replaces the input dispatchers (io lists A..D 0x0801B530/
  B54C/B574/B592, encoder lists 0x08020E48/E56/E8A, all start with a 4-byte push -> `b.w`) and decides per
  event (press owns a control until its release). Verified on hardware (probe 3.3).
- Port mapping: g_app_ports[0] = DAW port, g_app_ports[1] = MIDI port.
- **Keys Channel = feature param 0x64** (0-based). Encoder relin events pass value and delta as u16
  (delta -1 = 0xFFFF) -> sign-extend. Raw encoder io events (A/C/D) must still reach inputrelative's io
  listeners (0x08020E97 / EBB / EDB) or no relin events are generated.
- Feature flags at runtime (pad Custom 4 selected, 2026-11-11): defined 02-07 1b-1f 3c-3f 44-47 49-4f 53-59 5c
  64-77 79-7d; persistent 3c 3d 3e 4a 4f 64-77 79; free 00 01 08-1a 20-3b 40-43 48 50-52 5a 5b 5d-63 78 7e 7f.
- **Clock:** `clock_poll` @0x08013C60 (main loop, before input) keeps state at 0x20000104: +0 running, +2 external
  sync, +8 pending external F8s (`clock_ext_tick` @0x08013C50 adds one per received F8), +0xC accumulator,
  +0x10 tick period in us (clamped 1000..1000000), **+0x20 = 24 PPQN tick counter `g_clock_ticks` 0x20000124**.
  Each ms the accumulator gains 1000 us; reaching the period increments the counter (internal), or each
  pending F8 does (external). Free-running. MIDI Clock Out (`clock_out_poll` @0x08014492, main loop) sends
  one F8 per counter step -> cfw steps from the same counter in cfw_poll.
  External sync is automatic: the first pending F8 switches to external (0x08013BD6, resets the tempo
  filters), 250 ms without F8 (0x3D08F us @0x08013E2C) switch back to the internal tempo. F8 / FA / FB / FC
  are taken from both USB ports (`midi_rx_realtime`); after realtime on the DAW port, realtime on the MIDI
  port is ignored for 0.5 s (flag 0x2000F50B, 0x080309BA). FA / FB set, FC clears the running flag (+0).
- Stack: >= 1472 bytes of the painted area (0x2000F620..) never touched -> peak stack use ~1 KB.
- `ctl_block(id)` @0x08016038 / `ctl_unblock` @0x08016048 / `ctl_is_blocked` @0x08016058: per-control flag
  (table 0x2000C410) that makes the app listeners ignore the control; ui_menumgr uses it to capture controls
  (`0x08025712` capture = block + 0x0801BD28(id, 1), `0x08025726` release = unblock + 0x0801BC92(id, 3)).
- MIDI out: `midi_put(port, byte)` @0x08013F8E (USB-MIDI packetiser, SysEx of any length), `midi_send3(port,
  status, d1, d2)` @0x08013F6E. App ports (objects on main's stack) at 0x2000EE40 / 0x2000EE44.
- Main loop @0x0801041A (calls io poll 0x08014502, confstore_poll x5); all input listeners run there.

## Display (fw 1563)

- 128x64 OLED, SSD1306-style page format (byte = 8 vertical px, LSB top).
- UI layer: screens built from templates (`g_screen_templates` @0x08032994, 8 bytes/entry: nfields, ?, ptr to
  12-byte field descriptors: x u16?, width, page, pages, ..., buf offset @9, buf len @10).
  `disp_set_screen(screen, template)`, `disp_set_field_text(screen, field, str)`, `disp_set_field_num(screen, field, val)`.
  Field type 1 = text, 2 = image id, 3 = 3-word struct.
- `disp_render_template` turns fields into a display-list byte stream (ring buffer);
  `dl_interpret` @0x0801244C decodes it locally (opcode 0x7x = image) and pulls bitmaps from flash.
- Image table `g_image_table` @0x0802DF94, 640 entries: `off:19 | pages:5 | width:8`, data base 0x0800E600.
  `tools/bitmap.py list/export/patch`.
- Boot screen `ui_show_boot_screen` @0x08016E04 (called from 0x080103E6): template 11 =
  image 0x27F (128x48 Novation logo) + text line "Ver: " + version at page 6.
- Strings: `str_get(id)` @0x08015B0C, 226 English strings from 0x0802E994 (0xA7 "Launchkey", 0xA8 "Ver: ").

## Settings storage / feature params (fw 1563)

- `confsys_set_param(group, id, {value, flags}*, r3)` @0x08016C88 / `confsys_get_param` @0x08016C50 dispatch on
  `(group >> 4) & 0xF`; group 0xD0 = "feature" params (midiobj_feature.cpp) -> `feat_set_param` @0x08026D64.
- 128 feature params, flag byte per id in `g_feat_flags` @0x2000E094: bit0 = defined, bit1 = **persistent**,
  bit2 = 14-bit (low 7 bits at +0x20). A set call with flags bit0 is a *definition* (first one wins, no filters/
  listeners); persistent defs are skipped once the store has loaded.
- Persistent values live in `g_feat_persist` @0x2000DD8C (0x84 bytes, magic 0xAA at +0x80), registered with
  `confstore_register(key 0xFEA70BE5, ptr, 0x84, 3000 ms)`. Volatile ones live in @0x2000E014.
  A changed persistent value calls `confstore_mark_dirty` -> written to flash ~3 s later.
- On boot `confstore_poll` loads the blob, then `feat_on_store_loaded` @0x08026CAC re-sets every defined param
  (non-definition set), so filters/listeners see the restored values.
- Settings menu params are defined with flags = 3 (feature_settings.cpp, e.g. 0x0801EFF8).
- RAM 0x200015D0..0x2000F518 is zero-initialised at startup (scatter table @0x08033544).

## Scale (fw 1563)

- Feature params: 0x4A scale on/off (0 / 0x7F), 0x3E scale type (1..0x1E, 12-byte semitone masks @0x08031DE0, max 8 notes),
  0x3D root note (0..11), 0x3C scale mode (0..2: snap / filter / easy).
- `feat_scale_init` @0x0801F750 defines them (flags=1, default off / Major / C / 0); `feat_scale_filter` @0x0801F6B0
  pushes values into the scale engine `g_scale` @0x2000ECE4 (scale.cpp: +0 enabled, +4 12-byte map,
  +0x10 mode, +0x14 root, +0x18 listeners).

## Turing Machine mode (cfw, pad mode Custom 4)

- `cfw/tm_mode.c` + `cfw/tm_core.c`. Top pads = register bits 0..7 (bit 0 newest = pad 1, green; press flips),
  bottom pads = scale notes (white; press toggles), encoders 1..7 = probability, length, rate (4/1..1/32,
  CW = faster), octave, range, gate, trigger heads. A step plays a note when any trigger head reads a 1:
  heads at positions 0, s, 2s, ... below the loop length, s = 1 (Full, default) / 2 / 3 / 4 / 6 / 8 / 16
  (1/16 = first position only; a loop shorter than s also leaves only position 0). The note lasts gate % of
  the step (5..95 %) and is always released before the next step (no ties). Encoder 8 follows the encoder mode.
- Pitch: register value (bits 0..7) picks a note from the full scale (range counts all scale notes); a note
  disabled on the bottom pads is replaced by the closest enabled note in pitch (tie -> lower). All off = rest.
- Top pad 1 = play/pause (yellow; bright = bit 0 is 1). The mode is entered paused. Bit flips (all top pads,
  pad 1 green then) only while the hold pad is down.
- Bottom pads 8..14 = scale notes 1..7 (8th note of 8-note scales always on); **pad 15 = hold** (pink): while
  held the top row shows a frozen copy of the register while it keeps running; a top-pad press flips the bit
  it showed, at position (pad + steps since freezing) mod length.
- Menus: controls captured by a menu (ctl_is_blocked) go to the menu - except after (re)selecting pad Custom 4:
  Scale/Arp screens keep encoders captured after leaving them, so selecting the mode sets a priority flag
  that wins over captures until any other button (not Shift/pads/encoders) is pressed.
- Notes: MIDI port (g_app_ports[1]) on the Keys Channel (0x64), velocity 100. **DIN/TRS out:** main() inits a
  third port (stack sp+0x50, raw-byte mode, serial ring -> USART6 @31250 baud via DMA) and links it to the MIDI
  port (`port+0x18`, `0x08013EE8`); `midi_send3` walks that chain, so MIDI-port notes also go out on DIN.
- Play (0x48) is play/pause too: taken in `tm_io_event` while the mode is active and Shift is up (without
  the menu check: buttons are always captured by the UI), so its stock MIDI Start / Stop is not sent.
- **Persistence:** feature params 0x50 prob, 0x51 length, 0x52 rate, 0x5A octave, 0x5B range, 0x5D gate,
  0x5E trigger heads, 0x5F note mask (7 bits), 0x60 marker (0x5A), defined with flags 3 in cfw_init. Marker
  missing (first boot / older settings record) -> defaults are stored. Ids 0x20..0x3F avoided: they alias the
  LSBs of 14-bit params 0x00..0x1F.

## Encoder pages (fw 1563)

- `g_enc_layout` @0x2000F4E8 = current encoder layout (2..5 Plugin/Mixer/Sends/Transport, 6..9 Custom 1..4),
  written by `enc_set_layout` @0x0801CAC0 (0 is stored as 6). `enc_assign_refresh` @0x0801C8C4 switches on it
  (tbb @0x0801C8E0, layouts 6..9 -> 0x0801C8F0) and assigns the 8 encoders: custom layouts load each control
  from the custom-mode data through `enc_assign_custom` @0x0801C7AE; `enc_page_ctl` @0x0801C760 maps encoder
  ids 0x38..0x3F to 0x68..0x6F when the page is not 0 (= controls 9..16 of the Components layout).
- Page per custom layout: `g_enc_page[4]` @0x2000F39C (u32 each, RAM only). `enc_get_page(layout)` @0x0801CB7A,
  `enc_set_page(layout, page)` @0x0801CB3A (limits to 1, stores, refreshes), `enc_page_count(layout)`
  @0x0801CBA6 (2 for layouts 0 and 6..9, else 1).
- Up/down buttons next to the encoders = controls **0x25 (up, page - 1)** and **0x24 (down, page + 1)**.
  Handler `ui_nav_button` @0x08023436, page part from 0x080235A2: get page, check against enc_page_count,
  `enc_set_page`, then `ui_show_enc_page(page)` @0x080233BA (popup string 0x3F "Custom Controls" + string
  0x40 + page: "1 - 8" / "9 - 16") which ends in `ui_refresh_nav_leds` @0x080231CA (up lit if page > 0, down lit
  if page < count - 1). In the non-custom layouts the two buttons are ordinary mapped controls (0x080233E2).
- Control mappings: table `g_ctl_map` @0x20002D1C, 20 bytes per control id (+0 number, +2 min, +4 max, +0xE
  type; type 2 looks like a plain CC 0..127, see 0x0801C7F6), `ctl_map_get(id)` @0x0801E1AA,
  `ctl_map_set(id, map)` @0x0801E1B8. Static analysis only: the value of an encoder seems to be kept per
  physical control, not per page (0x08015BBE rescales it when a mapping changes).
- **cfw CC pages** (`cfw/cc_pages.c`, Custom 1 only): hooks replace enc_page_count (12 pages), the enc_set_page
  call and the ui_show_enc_page call in the handler; encoder turns are taken in the relin dispatchers (after
  the Turing Machine mode) and sent as CC 20 + page * 8 + encoder on channel 1 with `midi_send3`. Values:
  96 bytes of cfw RAM. **Status: built, host-tested (`make cfw-test`), not yet tested on hardware.**

## MIDI input, keys and buttons (fw 1563)

Static analysis for the step sequencer; nothing in this section is confirmed on hardware yet.

- **USB MIDI in:** `midi_rx_poll(parser, ring)` @0x08014418 runs in the main loop (called at 0x080104C4),
  feeds USB-MIDI packets to `usbmidi_rx_packet` @0x0801431C / bytes to `midi_rx_byte` @0x0801424A. main()
  registers per USB cable a message handler, a SysEx handler and the port as context (0x0801412A):
  cable 0 = **MIDI port**: `midi_port_rx_msg(port, status, d1, d2)` @0x0803048C, SysEx @0x080304C6;
  cable 1 = DAW port: `daw_port_rx_msg` @0x080302F6, SysEx `port_rx_sysex` @0x08030342 (chain of ~25
  handlers, each returns 1 when it took the message; the firmware updater 0x08030E00 is one of them).
- `midi_port_rx_msg`: optional echo (0x080302D0), then `midi_port_rx_notes` @0x080317AA (note on/off, CC,
  PC -> confsys params group = channel / 0x10 | channel / 0x40 | channel, i.e. the note and CC state the LEDs
  follow), then `midi_rx_realtime` @0x08030958 (F8 -> clock_ext_tick, FA/FB/FC). **cfw hook:** the
  `bl midi_port_rx_notes` at 0x0803049A -> `cfw_midi_rx` (clock note of the step sequencer).
- **Keys = control ids 0x80..0xFF** (key index + 0x80), scanned by `keybed_scan` @0x0801AB5E, which calls the
  io dispatchers like the other scanners: A(id, 16-bit velocity) on press, C(id) on release, B(id, pressure).
  `io_poll` @0x08014502 runs six scanners: generic 0x0801A71C (ids 0..0x7F by type from
  `g_ctl_scan_types` @0x0803217C), keys, pads 0x0801AD5E, faders 0x0801AFFC, touch strips 0x0801B1D8
  (ids 0x36 / 0x37), pedal 0x0801B39A (0x78 / 0x79). `ctl_info(id)` @0x0801DFA4 reads `g_ctl_types`
  @0x0802FED0 (ids < 0x80: 1 pad, 2 relative, 3 fader, 4 button, 5 touch strip, 6 pedal).
- **Control -> MIDI:** io listener A `ctl_press` @0x08015C6C copies the 20-byte map (`g_ctl_map`, 256 entries)
  into one of 16 slots, runs the press chain `inmidi_press_chain` @0x08020CBA (inputmidi_listener.cpp: up to
  8 {press, ?, release} handlers at 0x2000E7DC, registered with 0x08020DCE; ui_menumgr, scale, chord, arp
  consume here) and then the output functions (0x08015C38). Notes: `ctl_out_note` @0x0801FBD2 sets the
  confsys param (group = channel, id = note, value = velocity) and calls
  `inmidi_out_notify(ctl, group, number, value)` @0x08020C4C -> **note-out listeners** at 0x2000EFB8 (max 8,
  count at +0x20, `inmidi_add_out_listener` @0x08020D94; 1 used by the UI @0x08016530, which treats
  group < 0x10 with value != 0 as a note on). Releases notify with value 0 (0x0801FB64).
  `note_on(channel, note, velocity14, source)` @0x0801FE0A / `note_off` @0x0801FE66 are used by the timed
  sources (0x080165A8 with `layout_channel(2)` @0x08025878, strum 0x0802C184) and do not notify.
- **Buttons** (type 4) are captured by ui_menumgr: its press handler 0x080255A8 (in the press chain) passes
  `ui_shifted_id(id)` @0x0801BDC8 to `ui_button_press` @0x080163E0 / `ui_button_release` @0x0801645E, which
  call every UI module. With Shift (state at 0x2000F506) ids are translated by `g_shift_id_table`
  @0x080300D0: 0x20 -> 0x64, 0x21 -> 0x65, 0x22 -> 0x63, 0x23 -> 0x62, 0x40 -> 0x66, 0x41 -> 0x67,
  0x42 -> 0x12, 0x43 -> 0x13, 0x1C -> 0x1E, 0x1E -> 0x60, 0x1F -> 0x61. The io dispatchers see the raw ids.
- **Button ids:** 0x20 / 0x21 = **Octave - / +** (`ui_octave_press` @0x08021300; 0x64 / 0x65 = Transpose),
  0x22 / 0x23 = pad up / down, 0x24 / 0x25 = encoder down / up, 0x10 / 0x11 = the two buttons next to the
  pads, 0x19 = Shift, **0x48 = Play** (0x08022710 sends MIDI Start FA / Stop FC), **0x4A = Record**.
  `g_button_cc_table` @0x0803227C (12 {id, CC} pairs, DAW mapping; +0x60 and +0xC0 are the tables of the
  other two DAW modes): 0x40 / 0x41 = track right / left (CC 103 / 102), 0x44..0x47 = CC 74..77 (Capture
  MIDI, Quantise, Metronome, Undo), 0x48 = CC 115 Play, 0x49 = 116 Stop, 0x4A = 117 Record, 0x4B = 118 Loop.
  Record is identified by this table only.

## Step sequencers (cfw, pad modes Custom 1, 2 and 3)

- `cfw/seq_mode.c` + `cfw/seq_core.c` (host tests `cfw/test/test_seq_core.c`, `test_seq_mode.c`). Three
  instances of the core, 4 voices and 48 steps each (4 bytes per step: note / 0x80 rest / 0x81 tie, further
  slots note or 0xFF), RAM only. Instance n (1..3) = feature 0x1D value 4 + n (pad Custom 1..3) = MIDI channel n.
- The pad mode only selects the instance the controls, keys and pads belong to (`seq`); playback of all
  instances goes on whatever pad mode is selected. Leaving a pad mode ends that instance's recording.
- Record (0x4A, with and without Shift) and Play (0x48, Shift up) are consumed in the io dispatchers while a
  sequencer pad mode is selected; Oct - / Oct + (0x20 / 0x21) only with Shift while recording (rest / tie).
  Record keeps the sequence and records from step 1 (`rec`), overwriting; Shift + Record = `seq_clear`.
  Pads are consumed; while recording a pad moves the record position (`seq_record_seek`, steps 0..len on
  the page shown).
- Chords: `seq_io_event` counts the keys held (ids >= 0x80); the note-out listener `seq_note_out` stores a
  key's note as a new step, or adds it to the last step while another key is still down
  (`seq_record_add_note`). A button or pad press ends the chord. Keys that do not sound directly (Arp on)
  record nothing.
- Clock: `cfw_midi_rx` passes note on / off of note 24 on the MIDI port to `seq_midi_rx`; on channel n it
  clocks instance n if that is playing (the note is swallowed then, otherwise it goes to the stock handler).
  Note on = next step, note off = gate end unless the next step is a tie. Notes go out with `midi_send3` on
  g_app_ports[1] on channel n, velocity 100. While the selected instance plays, `seq_note_out` treats note
  24 from a key (ctl >= 0x80; value 0 = release, any Keys Channel) as its clock.
- The listener and the MIDI handler only change state; pads (LED override layer) and the popup are drawn
  from `seq_mode_poll` in the main loop for the selected instance. cfw_init registers one feature listener
  for both modes (`feat_changed`) to save a slot. **Status: built, host-tested, not yet tested on hardware.**

## Patches

- `010-boot-logo.patch` – generated from `assets/boot_logo.txt`: adds an inverted "CFW" badge right of the
  Novation logo (160 bytes, data only, image size unchanged). **Status: built, not yet tested on hardware.**
- `020-persist-scale.patch` – 1 byte at 0x0801F75E: scale param definitions use flags 3 instead of 1, so on/off,
  type, root and mode are stored in the persistent feature blob and restored at power-on (same mechanism as the
  Settings menu). Devices with an existing settings record read 0 there on first boot = factory defaults.
  **Status: built, not yet tested on hardware.**

## Update protocol (app side, handler 0x08030E00)

- 0x71 (needs `02 13`) -> state 1; 0x7C header -> state 2, stores size/CRC/version; then (immediately or after
  confirmation, "Press to Upgrade") state 3 and a ready ack `00 20 29 00 72 n0 n1 n2` (n = next offset >> 5).
- Every 0x72 block is written and acked with n+1; 0x73 (vector head) finishes; status reply `... 00 73 st 7F 7F`.
- Max image 0x38000. UI strings: "Entering Upgrade", "Press to Upgrade", "Receiving App...", "Upgrade Failed".
- [ ] Which button confirms "Press to Upgrade", and when is the confirmation skipped (flag checked in 0x7C path)?
- [ ] Meaning of the 0x73 status byte.
