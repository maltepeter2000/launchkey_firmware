# Launchkey Mini MK4 Custom Firmware

Custom firmware for the **Novation Launchkey Mini MK4** (25 and 37 keys), built by patching the official
firmware version 1.3 build 1563. It adds:

- **Turing Machine mode** – a generative sequencer inspired by the Music Thing Turing Machine, on pad mode
  Custom 4
- **Step sequencer** – an SH-101 style step sequencer on pad mode Custom 3, clocked by a MIDI note
- **CC pages** – encoder mode Custom 1 has 12 pages of 8 CCs (CC 20–115) instead of 2
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

## Step sequencer (pad mode Custom 3)

> **Not yet tested on hardware.** Built and host-tested (`make cfw-test`) only.

An SH-101 style step sequencer: notes are entered one step at a time and played back one step
per clock. Select pad mode **Custom 3** (hold Shift and press the 7th pad of the bottom row).

| Control | Function |
| --- | --- |
| **Record** | Start recording (the old sequence is cleared). Press again to finish. |
| **Keys** | While recording: every key press stores its note as the next step. |
| **Shift + Oct −** | While recording: store a **rest**. |
| **Shift + Oct +** | While recording: store a **tie** (the previous note keeps sounding). |
| **Play** | Start / stop playback. Playback always starts at step 1. |

- **Length:** the sequence is as long as the steps entered, up to 100. Recording ends by itself
  when the memory is full.
- **Clock:** send MIDI note **C0 (note number 24, middle C = C3)** on any channel to the
  Launchkey's **MIDI port** over USB (the first port, not the DAW port). Every note on advances
  one step; the note off ends the note, so the clock's gate length is the sequence's gate
  length. Nothing plays without that clock.
- **Pads:** the 16 pads show 16 steps (top row 1–8, bottom row 9–16) and follow the current
  step in pages of 16. Green = note, blue = rest, amber = tie; the playing step is bright, and
  while recording the next free step is red. The pads have no function when pressed.
- **Notes** go out on the **Keys Channel** (Settings) via USB MIDI and the TRS MIDI out,
  velocity 100. A chord entered with one key press stores its first note.
- The sequence is kept while the device is on, also when you change the pad mode; leaving
  Custom 3 stops playback and recording. It is **not saved** at power-off.
- In this mode Play and Record do not send their usual MIDI messages. Shift + Play / Record,
  Shift + pads (mode selection) and the plain Octave buttons work as usual; Shift + Oct −/+ is
  Transpose again whenever you are not recording.
- The clock note, velocity and maximum length are constants at the top of `cfw/seq_mode.c`
  and `cfw/seq_core.h`.

---

## CC pages (encoder mode Custom 1)

> **Not yet tested on hardware.** Built and host-tested (`make cfw-test`) only.

Select encoder mode **Custom 1** as usual (hold Shift and press the 5th pad of the top row). The
**up / down buttons** next to the encoders step through **12 pages** instead of 2; the display
shows the page and its CCs, and the button LEDs show whether there is a page above or below.

| Page | Encoders 1–8 send |
| --- | --- |
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
