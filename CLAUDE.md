# Launchkey Mini MK4 firmware workbench

Goal: analyse Novation Launchkey Mini MK4 firmware (v1563) and add functions by patching the binary.
README.md is the user manual (features, build/flash, recovery) – keep it user-facing; technical and agent
details belong here and in `analysis/NOTES.md`.

## Layout

- `firmware/original/` – untouched vendor `.syx` files (never edit)
- `tools/lkfw.py` – SysEx codec: `unpack`, `pack`, `info`, `verify` (pack recomputes size + CRC)
- `tools/mkelf.py` – wraps the raw image in an ARM ELF (base 0x08008000, Thumb, vector + `analysis/symbols.txt` names)
- `tools/apply_patches.py` – applies `patches/*.patch` (`<addr> <orig hex|-> <new hex>`, orig bytes are verified;
  first comment line = patch title) and sets version field 4 to the patch count: stock 1.3.1563.1717 -> 1.3.1563.<n>
  (image `g_fw_version` + SysEx header; boot screen shows it, device inquiry only carries 1.3.1563)
- `analysis/symbols.txt` – names we discover; rebuilt into the ELF by `make`
- `analysis/NOTES.md` – findings log (keep updated)
- `build/` – generated (gitignored): `.bin`, `.bin.json`, `.elf`, `.s`, `patched.syx`, `cfw.elf`, `cfw.map`
- `make` / `make verify` / `make disasm` / `make patched` / `make inquiry` / `make upload` / `make restore` /
  `make cfw` / `make cfw-test` / `make clean`
- Python runs through **uv** (`pyproject.toml`, `uv.lock`, `.python-version` = 3.12): `uv run python tools/...`;
  add deps with `uv add` (commit both files). Tools are plain scripts (`[tool.uv] package = false`).
- `tools/send_syx.py` – MIDI upload with per-block ack handshake; `--inquiry`, `--list`, `--dry-run`.
  Prints the file's version and which `patches/*.patch` are present in the image (checked byte-wise).
- `tools/bitmap.py` – `list` / `export <id>` / `patch <id> art.txt [--title]` for OLED images (image table
  `g_image_table` 0x0802DF94); `assets/boot_logo.txt` (image 0x27F) -> `patches/010-boot-logo.patch` via make.
- `cfw/` – C code for the free flash after the image (`cfw/cfw.ld`: code from 0x080372B4, 976 B RAM @0x2000F520,
  no initialised .data, cfw_init zeroes .bss). Firmware functions: `cfw/fw_api.h` + `cfw/fw_api.ld`; hook sites:
  `cfw/hooks.txt` (`<addr> bl|b.w <symbol> <orig hex>`). `make cfw` (xcrun clang + Homebrew `ld.lld`) ->
  `tools/mkcave.py` -> `patches/100-cfw-code.patch`, `patches/101-cfw-hooks.patch`. `make cfw-test` runs the
  host unit tests (`cfw/test/`).
- Turing Machine mode = pad mode Custom 4 (`cfw/tm_mode.c`, `cfw/tm_core.c`, input dispatchers in
  `cfw/cfw_init.c`); behaviour, persistence ids and integration details: NOTES "Turing Machine mode" and
  "Modes and layouts". A SysEx debug probe (`cfw/probe.c`, `tools/probe_listen.py`) existed until commit
  350285f – restore it from git when hardware debugging needs event logs.
- Step sequencers = pad modes Custom 1..3 = three instances on MIDI channels 1..3, 32 steps (4 notes, accent,
  3 CC values per step), playing in the background (`cfw/seq_mode.c`, `cfw/seq_core.c`, host tests
  `cfw/test/test_seq_*.c`): SH-101 style step entry (Record, keys, Shift + Oct -/+ = rest / tie, Shift +
  Play = accent, pad = record position, Shift + Record = clear while recording, else clock source), Play,
  encoders (rate, direction, accent amount, first / last step, 3 CC lanes), clocked by MIDI note 24 on the
  instance's channel or by the MIDI clock, stored in the settings store (NOTES "Settings store"). MIDI input path, key
  and button ids: NOTES "MIDI input, keys and buttons" and "Step sequencers".
- CC pages = encoder mode Custom 1 with 12 pages of CCs (`cfw/cc_pages.c`, host test `cfw/test/test_cc_pages.c`);
  stock page handling and the three hooks: NOTES "Encoder pages".

## Container format (verified: round-trip byte-identical, CRC matches)

- Messages `F0 00 20 29 00 <cmd> … F7` (Novation ID).
- `0x71` start: `02 13` (checked by the app) + version `01 03 0c 1b 0d 35` = 1.3, build 1563, 1717.
- `0x7C` info: `00` + same 6 bytes + size (8 nibbles) + CRC (8 nibbles).
- `0x72` data: 37 7-bit bytes = MSB-first bitstream → 32 image bytes (+3 zero bits). Carries image[0x20:], last chunk 0xFF-padded.
- `0x73` final data: image[0x00:0x20] (vector-table head sent last).
- CRC = CRC-32/MPEG-2 (poly 04C11DB7, init FFFFFFFF, no reflect/xorout) over image[:size].
- Manual: `uv run python tools/lkfw.py info|unpack|pack|verify ...` (pack takes `--meta <bin>.json`).

## Target facts

- ARM Cortex-M4 (Thumb-2), STM32F4-family (FLASH regs @0x40023C00, RCC @0x40023800, USB OTG_FS @0x50000000).
- App linked at **0x08008000** (Reset_Handler writes VTOR=0x08008000); bootloader occupies 0x08000000–0x08007FFF and is NOT part of the update.
- Initial SP 0x2000FFF8 (64 KiB SRAM). Stock image ends 0x080372B3 (193 203 bytes). Reset_Handler 0x08033481.
- Settings live in an external SPI flash; the app never writes internal flash, so 0x080372B4..0x0803FFFF of the
  0x38000 app slot is free for cfw code (verified: grown images install and run). RAM headroom is ~2.7 KiB.
- Source-file strings: confsys.c, confsys_init.c, extdata_*.c, io_listener.c, muxio_hw_data.c, serstor_hw.c.

## Analysis tooling

- REA MCP: Hopper provider works on `build/lkmini4-1563.elf` (`open_binary` with `provider_id: "hopper"`).
  Raw-binary mode needs Ghidra (not installed). Re-run `make` and reopen after editing `symbols.txt`.
  Hopper's decompiler handles tbb/tbh switch tables poorly – read the disassembly there.
- `make disasm` -> `build/lkmini4-1563.s` (llvm-objdump); grep it for callers (`bl 0x...`) and literal-pool loads.
- Add every identified function/global to `analysis/symbols.txt` (`<addr> <name> [func|data]`) and findings to NOTES.
- `xcrun clang -target thumbv7em-none-eabi -mcpu=cortex-m4` compiles (also for quick byte encodings of a few
  instructions: assemble with `-x assembler`, then `xcrun llvm-objdump -d`). Linker: Homebrew `lld` (`ld.lld`).
- Raw patch bytes are little-endian Thumb halfwords (`00bf` = nop, `7047` = bx lr).

## Update protocol on the device (fw 1563, handler 0x08030E00)

- The running app performs the update on the **MIDI** port; no bootloader mode needed.
- Device acks: after the header (maybe after user confirmation, "Press to Upgrade") and after every 0x72 block it sends
  `F0 00 20 29 00 72 <n lo,mid,hi 7-bit> F7` (n increments by one per block); final `F0 00 20 29 00 73 <status> 7F 7F F7`
  (status 7F after every successful upload so far).
- Max image size 0x38000. Device inquiry reply: family `41 01` = 25-key, `42 01` = 37-key; same firmware for both.
- Connected test device: Launchkey Mini MK4 25, fw 1.3 build 1563.

## Working with the device (agent)

- Flash only when the user asks for it, and only files that passed `make patched` (CRC + round-trip verify).
- `make upload` prompts y/N, which the agent shell can't answer: run
  `uv run python -u tools/send_syx.py build/patched.syx --yes --confirm-timeout 300 > build/upload.log 2>&1`
  (`-u` so the log shows progress). The user must press "Press to Upgrade" on the device; if they miss it, the
  uploader is still waiting and nothing was written – kill it and start a new upload.
- After an upload the device re-enumerates for a few seconds; poll `send_syx.py --inquiry` until it answers.
- Commits go directly on `main` (the user's practice), only when the user asks.
