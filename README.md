# Launchkey Mini MK4 Custom Firmware

Custom firmware for the **Novation Launchkey Mini MK4** (25 and 37 keys), built by patching the official
firmware version 1.3 build 1563. It adds:

- **Turing Machine mode** – a generative sequencer inspired by the Music Thing Turing Machine, on pad mode
  Custom 4
- **Step sequencers** – three SH-101 style step sequencers on pad modes Custom 1, 2 and 3 (MIDI channels
  1, 2 and 3): up to four notes, an accent and three controller values per step, clocked by a MIDI note or
  the MIDI clock, saved at power-off
- **CC pages** – encoder mode Custom 1 has the 11 CC pages of the Pd controller layer (CC 20–119 without reserved numbers) on the keys channel, with feedback from Pd, instead of 2
- **Scale memory** – the scale (on/off, type, root note, mode) is restored at power-on
- A **CFW badge** on the boot logo, and the version shown at boot gets a `+cfw` suffix

> ⚠️ **Use at your own risk.** Flashing modified firmware can leave the device unusable and may void your
> warranty. Read [Safety & recovery](#safety--recovery) before your first upload.

---

## Turing Machine mode

Select it like any pad mode: **hold Shift and press the 8th pad of the bottom row (pad mode Custom 4)**.
The display shows "Turing Machine". The mode starts **paused**.

A 16-bit shift register loops and, depending on the probability, flips the bit that comes round again. Its
value picks a note from the current scale, and trigger heads decide whether a step plays a note.

### Pads

| Pad | Function |
|---|---|
| Top row, pad 1 (yellow) | **Play / pause.** Also shows register bit 1 (bright = 1, dim = 0). |
| **Play** button | **Play / pause** as well. It does not send its usual MIDI Start / Stop in this mode. |
| Top row, pads 2–8 (green) | Register bits 2–8, moving one pad to the right per step. Dark = beyond the loop length. |
| Bottom row, pads 1–7 (white) | **Scale notes** (pad 1 = root). Press to switch a note on (bright) or off (dim). A disabled note is replaced by the closest enabled note. With Scale off all pads are lit and all 12 semitones are used. |
| Bottom row, pad 8 (pink) | **Hold.** While held, the top row stops moving on screen (the sequence keeps running) and pressing a top pad **flips that bit**; pad 1 turns green then. |

### Encoders

| Encoder | Parameter | Range |
|---|---|---|
| 1 | Probability | right = locked loop, middle = random, left = locked and inverted (twice as long) |
| 2 | Length | 2–16 steps |
| 3 | Rate (turn right = faster) | 4/1, 2/1, 1/1, 1/2, 1/4, 1/4T, 1/8, 1/8T, 1/16, 1/16T, 1/32 |
| 4 | Octave | 0–8 |
| 5 | Range | 1–36 scale notes |
| 6 | Gate length | 5–95 % of a step (notes are never tied) |
| 7 | Trigger heads | Full, 1/2, 1/3, 1/4, 1/6, 1/8, 1/16 – a step plays a note when any head (register position 1, then every n-th, within the loop) reads a 1. Full = nearly every step, 1/16 = only when bit 1 is set. |
| 8 | follows the selected encoder mode | |

Turning an encoder shows its value on the display.

### Good to know

- **Tempo** follows the device: the internal tempo, or external MIDI clock when connected.
- **Notes** go out on the **Keys Channel** (Settings) via USB MIDI and the TRS MIDI out, velocity 100.
- **All parameters and the scale-note selection are saved** and restored at power-on (about 3 seconds after
  the last change). The running pattern and play/pause are not saved.
- **Shift still works:** Shift + pads selects pad and encoder modes as usual, and menus (Settings, Scale, Arp)
  get the encoders while they are open. If a Scale or Arp screen keeps the encoders, select pad Custom 4 again.
- The 8th note of 8-note scales (Half-Whole Dim, Whole-Half Dim, 8-tone Spanish) is always on.

---

## Step sequencers (pad modes Custom 1, 2 and 3)

> **Not yet tested on hardware.** Built and host-tested (`make cfw-test`) only.

Three independent SH-101 style step sequencers: notes are entered one step at a time and played
back one step per clock. Each has its own sequence of up to **32 steps**; a step holds up to
**4 notes**, an **accent** and one value for each of **three controllers (CCs)**.

| Pad mode | Select with | Sequencer | MIDI channel |
| --- | --- | --- | --- |
| Custom 1 | Shift + 5th pad of the bottom row | 1 | 1 |
| Custom 2 | Shift + 6th pad of the bottom row | 2 | 2 |
| Custom 3 | Shift + 7th pad of the bottom row | 3 | 3 |

The pad mode chooses which sequencer the buttons, keys, pads and encoders belong to. A sequencer
that is playing **keeps playing in the background** when you select another pad mode (also
Custom 4, Drum or DAW), so all three can run at once.

### Buttons, keys and pads

| Control | Function |
| --- | --- |
| **Record** | Start recording at step 1. The sequence is kept: what you enter replaces the notes of its steps, and makes it longer once you pass its end. Press again to finish. |
| **Keys** | While recording: every key press stores its note as the next step. Keys pressed while another key is still held join the same step as a chord (up to 4 notes); the next step starts once all keys are released, so let go between steps. |
| **Shift + Oct −** | While recording: store a **rest**. |
| **Shift + Oct +** | While recording: store a **tie** (the previous notes keep sounding). |
| **Shift + Play** | While recording: **accent** on / off for the *edit step* (see below). Accented notes play at velocity 127. |
| **Pads** | While recording: press a pad to continue recording **from that step**, e.g. to change single notes. Any step of the sequence or the first step after it can be chosen. |
| **Shift + Record** | While recording: **reset**, empties the sequence. Otherwise: switches the **clock** between the clock note and the MIDI clock. |
| **Play** | Start / stop playback. Sends **MIDI Start** when the first of the three sequencers starts and **MIDI Stop** when the last one stops. |

The **edit step** is the step you entered last, or, right after pressing Record or a pad, the step
at the record position. Accent and controller values go to that step. Entering notes only
changes the notes of a step, never its accent or controller values.

### Encoders

While pad mode Custom 1, 2 or 3 is selected the eight encoders belong to the sequencer (the
encoder mode is not used then):

| Encoder | Function |
| --- | --- |
| 1 | **Clock rate** for the MIDI clock: /7, /6, /5, /4, /3, /2, /1.5, x1, x1.5, x2, x3, x4, x5, x6, x7. x1 is one step per 1/16 note; "/" is slower, "x" faster. |
| 2 | **Direction:** Forward, Reverse, Alternate, Random. |
| 3 | **Accent amount** +7 … +27: plain notes play at velocity 127 minus the amount (120 … 100). |
| 4 | **First step** played. |
| 5 | **Last step** played, 1 … 32. It may lie beyond the steps you entered; those steps are rests. Shortening does not delete steps. |
| 6, 7, 8 | **Controllers.** Turning sends the CC. While recording the value is also stored in the edit step and sent whenever that step plays. If you do not turn them, no controller is recorded. |
| Shift + 6, 7, 8 | The **CC number** of that encoder (default 74, 71, 1). |

### Pads

The 16 pads show 16 steps (top row 1–8, bottom row 9–16) and follow the current step in pages of 16.

| Colour | Step |
| --- | --- |
| off | rest (or beyond the last step) |
| yellow | note |
| dark yellow | tie |
| orange | accented note |
| dark red | accented tie |
| white | the step playing |
| pulsing grey | the record position |
| blue | while you turn encoder 6, 7 or 8 during recording: the values of that controller, brighter = higher. The normal colours return about a second later or with the next entry. |

A pad selects a step on the page shown; to reach the second page, enter steps until the page changes.

### Clock

- **Clock note** (default): MIDI note **C0 (note number 24, middle C = C3)**. Every note on
  advances one step; the note off ends the notes, so the clock's gate length is the sequence's
  gate length. It can come
  - over USB, sent to the Launchkey's **MIDI port** (the first port, not the DAW port) on the
    sequencer's channel: C0 on channel 1 steps sequencer 1, and so on, whatever pad mode is
    selected, or
  - from the **keys**: while the sequencer of the selected pad mode is playing, the key that
    plays C0 steps it (use the Octave buttons to reach it). That key also sounds its own C0.
- **MIDI clock** (Shift + Record): the sequencer runs by itself on the Launchkey's clock, which
  follows an external MIDI clock received over USB and otherwise the internal tempo. Play
  starts with step 1 right away, the rate is set with encoder 1, notes last half a step. The
  clock note is ignored in this setting. Rates that do not divide the 24 clock ticks per
  quarter note evenly (x5, x7 ...) are slightly uneven.

### MIDI Start / Stop

- **Received** (over USB, either port): MIDI Start or Continue starts every sequencer that has
  a sequence and is stopped, in any pad mode; MIDI Stop stops all of them. A sequencer that is
  recording is left alone. With the MIDI clock, step 1 plays on the first clock after Start.
- **Sent** (USB MIDI port and TRS MIDI out): the Play button sends MIDI Start when it starts
  the first sequencer and MIDI Stop when it stops the last one. Sequencers started from
  outside send nothing.
- If your DAW echoes the Launchkey's MIDI back to it, the Start sent by Play comes back and
  starts the other sequencers too; switch the echo (MIDI thru) off for that port.

### Other

- **Notes and controllers** go out on the sequencer's channel via USB MIDI and the TRS MIDI out.
- **Saved at power-off:** the three sequences with accents, controller values and the encoder
  settings are stored a few seconds after the last change (switching off right after an entry
  loses it). After power-on all sequencers are stopped.
- Leaving a sequencer's pad mode ends its recording.
- A controller value cannot be removed from a single step; reset the sequence to remove them.
- In these modes Play and Record do not send their usual MIDI messages, and Shift + Record
  loses its usual function. Shift + Play (unless recording), Shift + pads (mode selection) and
  the plain Octave buttons work as usual; Shift + Oct −/+ is Transpose again whenever you are
  not recording.
- Pad mode Custom 1 is no longer available as a custom pad layout, and the CC pages of encoder
  mode Custom 1 are only reachable while another pad mode (Drum, DAW, Custom 4) is selected.
- The clock note and the number of sequencers are constants at the top of `cfw/seq_mode.c`.

---

## CC pages (encoder mode Custom 1)

> **Not yet tested on hardware.** Built and host-tested (`make cfw-test`) only.

Made for the controller layer of Peter's Pd instruments (`tools/ctlayer.py` in PDoly800): **one MIDI
channel per instrument, the same CC pages for every instrument.** Select encoder mode **Custom 1**
as usual (hold Shift and press the 5th pad of the top row). The **up / down buttons** next to the
encoders step through **11 pages**; the display shows the page and its CCs, and the button LEDs
show whether there is a page above or below.

| Page | Encoders 1–8 send |
| --- | --- |
| 1 | CC 20–27 |
| 2 | CC 28 29 30 31 33 34 35 36 |
| 3 | CC 37 39 40 41 42 43 44 45 |
| 4 | CC 46–53 |
| 5 | CC 54–61 |
| 6 | CC 62 63 70 71 72 73 74 75 |
| 7 | CC 76–83 |
| 8 | CC 85 86 87 89 90 91 92 93 |
| 9 | CC 94 95 102 103 104 105 106 107 |
| 10 | CC 108–115 |
| 11 | CC 116–119 (encoders 5–8 send nothing) |

Left out are the numbers with a fixed meaning: 32 and 38 (bank / data LSB), 64–69 (pedals),
84 (portamento control), 88 (high-res velocity), 96–101 (data increment, NRPN, RPN).

- **Channel = the Keys Channel.** The encoders send on the same MIDI channel as the keys, so
  changing the keys channel switches the instrument for both. Turning an encoder shows page,
  channel, CC number and value.
- **Feedback:** a CC on the keys channel that comes in on the MIDI port sets the value of the
  encoder that sends it. When a preset changes in Pd, the Pd layer sends the new values back and
  the next turn goes on from the right place – no jumps.
- **Sync request:** the first turn after power-on or after changing the keys channel first sends
  **CC 3 = 127** on that channel; the Pd layer answers with all its values.
- **Every page remembers its eight values** (RAM only).
- The assignments made for Custom 1 in Novation Components are not used while this firmware is
  installed. Custom 2–4 and all other encoder modes work as before (2 pages).
- Pages, sync CC and step size are at the top of `cfw/cc_pages.c`.
- In pad mode Custom 4 (Turing Machine) encoders 1–7 belong to the Turing Machine; only
  encoder 8 sends the CC of the current page.

--- | --- |
| 1 | CC 20–27 |
| 2 | CC 28–35 |
| 3 | CC 36–43 |
| … | … |
| 12 | CC 108–115 |

- Values are absolute (0–127) on **MIDI channel 1**, via USB MIDI and the TRS MIDI out. Turning
  an encoder shows page, CC number and value.
- **Every page remembers its eight values**, so there are no jumps after changing the page. The
  values start at 0 after power-on and are not updated by incoming MIDI.
- The assignments made for Custom 1 in Novation Components are not used while this firmware is
  installed. Custom 2–4 and all other encoder modes work as before (2 pages).
- Number of pages, first CC, channel and step size are the `CCP_*` constants at the top of
  `cfw/cc_pages.c`.
- In pad mode Custom 4 (Turing Machine) encoders 1–7 belong to the Turing Machine; only
  encoder 8 sends the CC of the current page.

---

## Requirements

| Tool | Install |
|---|---|
| [uv](https://docs.astral.sh/uv/) (Python and dependencies) | `brew install uv` |
| `make`, `clang` (Xcode command line tools) | `xcode-select --install` |
| `ld.lld` (links the custom code) | `brew install lld` |

One-time setup:

```sh
uv sync
```

---

## Build and flash

```sh
make inquiry      # check the connection: "Launchkey Mini MK4 25, firmware 1.3 build 1563"
make patched      # build build/patched.syx (all checks must pass)
make upload       # flash it (asks for confirmation first)
make restore      # flash the original Novation firmware again
```

`make upload` lists the version and the features (patches) contained in the file before asking. Then:

1. The device may show **"Press to Upgrade"** – press it within the timeout. The uploader waits 120 seconds
   (`--confirm-timeout 300` for longer).
2. The upload takes about 30 seconds and the device restarts by itself.
3. The boot screen shows `Ver: 1.3.1563.<n>+cfw`, where `<n>` is the number of patches.

Before uploading:

- Connect the Launchkey **directly** (no hub) and keep the computer awake.
- **Quit every app that uses the Launchkey** (DAWs, Novation Components, browser tabs with Web MIDI).

If the uploader reports **no "ready"**, nothing has been written: check the points above and run
`make upload` again. If it stops **in the middle**, don't unplug the device – run `make upload` or
`make restore` again right away.

Uploader options (`uv run python tools/send_syx.py build/patched.syx [options]`):

| Option | Effect |
|---|---|
| `--dry-run` | only check the file and list its contents |
| `--yes` | don't ask for confirmation |
| `--confirm-timeout 300` | wait longer for "Press to Upgrade" |
| `--port "MK4 37"` | choose the port by name |
| `--list` | list all MIDI ports |

---

## Customising the boot logo

`assets/boot_logo.txt` is the 128×48 boot logo as ASCII art (`#` = pixel on, `.` = off). Edit it without
changing its size, then `make patched`. The original logo is in `assets/boot_logo.orig.txt`.

---

## Safety & recovery

- The update only rewrites the application area. The bootloader is never touched.
- Uploads only start after the device replies "ready" and stop at the first missing acknowledgement.
- To go back to the official firmware: `make restore`, or use
  [Novation Components](https://components.novationmusic.com).
- If the device doesn't start after an upload: unplug and reconnect it. If it still doesn't start, it needs
  Novation's bootloader/update mode (a button combination held while connecting USB – see Novation's support
  pages for the Launchkey Mini MK4) and then the original firmware.
- Never flash a file that `make patched` did not build and check.
