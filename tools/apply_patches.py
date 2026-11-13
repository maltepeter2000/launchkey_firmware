#!/usr/bin/env python3
"""Apply byte patches to a raw firmware image and stamp the patch level into the version.

Usage: apply_patches.py in.bin out.bin [--meta in.bin.json] patches/*.patch

Patch file format (one edit per line, '#' starts a comment):

    <address> <original bytes hex> <new bytes hex>

    0x08012344  0b4b  00bf          # replace ldr r3,[pc,#44] with nop
    0x080372b4  -     70470000      # '-' = extend/overwrite free space (must be 0xFF or past end)

Addresses are absolute (image is linked at 0x08008000). Every original byte
sequence is verified before writing, so a patch never silently applies to the
wrong firmware version. The first comment line of a patch file is its title.

Version: the firmware version is major.minor.build.extra (1.3.1563.1717 for the
stock image, u32s at g_fw_version 0x080081F0, also 6 bytes in the SysEx header).
The 4th field is replaced by the number of applied patches, so every added patch
bumps it: boot screen shows "Ver: 1.3.1563.<n>". With --meta, out.bin.json is
written (header version bytes updated + patch list) for `lkfw.py pack --meta`.
"""
import argparse
import json
import struct
import sys
from pathlib import Path

BASE = 0x08008000
VERSION_ADDR = 0x080081F0          # g_fw_version: 4 x u32
STOCK_VERSION = (1, 3, 1563, 1717)


def parse(path: Path):
    """Return (title, [(lineno, addr, orig bytes or None for '-', new bytes)])."""
    title, edits = None, []
    for lineno, raw in enumerate(path.read_text().splitlines(), 1):
        code, _, comment = raw.partition("#")
        if title is None and not code.strip() and comment.strip():
            title = comment.strip()
        line = code.split()
        if not line:
            continue
        if len(line) != 3:
            sys.exit(f"{path}:{lineno}: expected '<addr> <orig|-> <new>'")
        orig = None if line[1] == "-" else bytes.fromhex(line[1])
        edits.append((lineno, int(line[0], 16), orig, bytes.fromhex(line[2])))
    return title or path.stem, edits


def status(image: bytes, path: Path):
    """'applied', 'not applied' or 'partial' for a patch file against an image."""
    _, edits = parse(path)
    hits = [image[a - BASE:a - BASE + len(new)] == new for _, a, _, new in edits]
    return "applied" if all(hits) else "partial" if any(hits) else "not applied"


def apply(image: bytearray, path: Path):
    for lineno, addr, orig, new in parse(path)[1]:
        off = addr - BASE
        if off < 0:
            sys.exit(f"{path}:{lineno}: address 0x{addr:08x} is below image base")
        if off + len(new) > len(image):
            image.extend(b"\xFF" * (off + len(new) - len(image)))
        cur = bytes(image[off:off + len(new)])
        if orig is None:
            if cur != b"\xFF" * len(new):
                sys.exit(f"{path}:{lineno}: 0x{addr:08x} is not free (found {cur.hex()})")
        elif cur != orig:
            sys.exit(f"{path}:{lineno}: 0x{addr:08x} expected {orig.hex()}, found {cur.hex()}")
        image[off:off + len(new)] = new
        print(f"  {path.name}:{lineno}  0x{addr:08x}  {cur.hex()} -> {new.hex()}")


def read_version(image: bytes):
    return struct.unpack_from("<4I", image, VERSION_ADDR - BASE)


def version_bytes(v):
    """6 SysEx header bytes: major, minor, build hi/lo, extra hi/lo (7-bit each)."""
    return bytes([v[0], v[1], v[2] >> 7, v[2] & 0x7F, v[3] >> 7, v[3] & 0x7F])


def stamp_version(image: bytearray, level: int):
    cur = read_version(image)
    if cur != STOCK_VERSION:
        sys.exit(f"g_fw_version is {cur}, expected stock {STOCK_VERSION} – refusing to stamp")
    v = (*STOCK_VERSION[:3], level)
    struct.pack_into("<4I", image, VERSION_ADDR - BASE, *v)
    return v


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("inp")
    ap.add_argument("out")
    ap.add_argument("patches", nargs="*", type=Path)
    ap.add_argument("--meta", type=Path, help="stock .bin.json; writes <out>.json with the new version")
    a = ap.parse_intermixed_args()   # --meta may come before the patch list (Python < 3.12.7)

    image = bytearray(Path(a.inp).read_bytes())
    for p in a.patches:
        apply(image, p)
    v = stamp_version(image, len(a.patches))
    Path(a.out).write_bytes(image)
    print(f"{a.out}: {len(image)} bytes, version {'.'.join(map(str, v))}")

    if a.meta:
        meta = json.loads(a.meta.read_text())
        for k in ("size", "crc", "source"):  # describe the stock image; pack recomputes size/CRC
            meta.pop(k, None)
        vb = version_bytes(v)
        start, info = bytes.fromhex(meta["start_payload"]), bytes.fromhex(meta["info_prefix"])
        meta["start_payload"] = (start[:2] + vb).hex()
        meta["info_prefix"] = (info[:1] + vb).hex()
        meta["version"] = ".".join(map(str, v))
        meta["patches"] = [{"file": p.name, "title": parse(p)[0]} for p in a.patches]
        Path(a.out + ".json").write_text(json.dumps(meta, indent=2) + "\n")


if __name__ == "__main__":
    main()
