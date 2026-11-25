"""Graphics test for the branch display-gfx: sends a 128 x 48 bitmap to the Launchkey (cfw/lk_gfx.h).

    uv run python tools/gfx_test.py --list          MIDI ports
    uv run python tools/gfx_test.py pattern         border, grid and diagonal (still image)
    uv run python tools/gfx_test.py env             an envelope morphing for 10 s (animation)
    uv run python tools/gfx_test.py clear
    uv run python tools/gfx_test.py diag            the device's state (draws, busy, display list)
    uv run python tools/gfx_test.py variants        each draw variant for 4 s with its state: watch the display
    --port NAME   output port (default: the Launchkey's MIDI port)
    --fps N       frames per second for env (default 10; the device redraws every 100 ms)
"""
import argparse
import math
import sys
import time

W, PAGES = 128, 6
H = PAGES * 8
HEAD = [0xF0, 0x7D, 0x4C, 0x4B]


def pack7(data):
    out = []
    for i in range(0, len(data), 7):
        g = data[i:i + 7]
        out.append(sum(((b >> 7) & 1) << k for k, b in enumerate(g)))
        out += [b & 0x7F for b in g]
    return out


# 5 x 7 pixel font (rows top to bottom, MSB left): digits, capitals, a few signs
FONT = {
    '0': [0xe, 0x11, 0x13, 0x15, 0x19, 0x11, 0xe],
    '1': [0x4, 0xc, 0x4, 0x4, 0x4, 0x4, 0xe],
    '2': [0xe, 0x11, 0x1, 0x2, 0x4, 0x8, 0x1f],
    '3': [0x1e, 0x1, 0x1, 0xe, 0x1, 0x1, 0x1e],
    '4': [0x2, 0x6, 0xa, 0x12, 0x1f, 0x2, 0x2],
    '5': [0x1f, 0x10, 0x1e, 0x1, 0x1, 0x11, 0xe],
    '6': [0x6, 0x8, 0x10, 0x1e, 0x11, 0x11, 0xe],
    '7': [0x1f, 0x1, 0x2, 0x4, 0x8, 0x8, 0x8],
    '8': [0xe, 0x11, 0x11, 0xe, 0x11, 0x11, 0xe],
    '9': [0xe, 0x11, 0x11, 0xf, 0x1, 0x2, 0xc],
    'A': [0xe, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11],
    'B': [0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e],
    'C': [0xe, 0x11, 0x10, 0x10, 0x10, 0x11, 0xe],
    'D': [0x1c, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1c],
    'E': [0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f],
    'F': [0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10],
    'G': [0xe, 0x11, 0x10, 0x17, 0x11, 0x11, 0xf],
    'H': [0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11],
    'I': [0xe, 0x4, 0x4, 0x4, 0x4, 0x4, 0xe],
    'J': [0x7, 0x2, 0x2, 0x2, 0x2, 0x12, 0xc],
    'K': [0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11],
    'L': [0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f],
    'M': [0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11],
    'N': [0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11],
    'O': [0xe, 0x11, 0x11, 0x11, 0x11, 0x11, 0xe],
    'P': [0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10],
    'Q': [0xe, 0x11, 0x11, 0x11, 0x15, 0x12, 0xd],
    'R': [0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11],
    'S': [0xf, 0x10, 0x10, 0xe, 0x1, 0x1, 0x1e],
    'T': [0x1f, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4],
    'U': [0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0xe],
    'V': [0x11, 0x11, 0x11, 0x11, 0x11, 0xa, 0x4],
    'W': [0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0xa],
    'X': [0x11, 0x11, 0xa, 0x4, 0xa, 0x11, 0x11],
    'Y': [0x11, 0x11, 0x11, 0xa, 0x4, 0x4, 0x4],
    'Z': [0x1f, 0x1, 0x2, 0x4, 0x8, 0x10, 0x1f],
    ' ': [0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0],
    '-': [0x0, 0x0, 0x0, 0x1f, 0x0, 0x0, 0x0],
    '.': [0x0, 0x0, 0x0, 0x0, 0x0, 0xc, 0xc],
    ':': [0x0, 0xc, 0xc, 0x0, 0xc, 0xc, 0x0],
    '/': [0x1, 0x1, 0x2, 0x4, 0x8, 0x10, 0x10],
    '%': [0x18, 0x19, 0x2, 0x4, 0x8, 0x13, 0x3],
    '>': [0x8, 0x4, 0x2, 0x1, 0x2, 0x4, 0x8],
}


class Bitmap:
    def __init__(self):
        self.px = [[0] * W for _ in range(H)]

    def set(self, x, y):
        if 0 <= x < W and 0 <= y < H:
            self.px[int(y)][int(x)] = 1

    def line(self, x0, y0, x1, y1):
        n = int(max(abs(x1 - x0), abs(y1 - y0))) + 1
        for i in range(n + 1):
            t = i / n
            self.set(round(x0 + (x1 - x0) * t), round(y0 + (y1 - y0) * t))

    def text(self, x, y, t):
        """5 x 7 font, capitals, 6 pixels a character; returns the x after the text"""
        for ch in t.upper():
            for r, bits in enumerate(FONT.get(ch, FONT[" "])):
                for c in range(5):
                    if bits >> (4 - c) & 1:
                        self.set(x + c, y + r)
            x += 6
        return x

    def pages(self):
        """SSD1306 pages: per page and column one byte, LSB = top row"""
        return [[sum(self.px[p * 8 + b][x] << b for b in range(8)) for x in range(W)] for p in range(PAGES)]


def messages(bm, show=True):
    msgs = []
    for p, row in enumerate(bm.pages()):
        for x in range(0, W, 32):
            msgs.append(HEAD + [0x02, p, x, 32] + pack7(row[x:x + 32]) + [0xF7])
    if show:
        msgs.append(HEAD + [0x03, 0xF7])
    return msgs


def pattern():
    bm = Bitmap()
    for x in range(W):
        bm.set(x, 0); bm.set(x, H - 1)
    for y in range(H):
        bm.set(0, y); bm.set(W - 1, y)
    for x in range(0, W, 16):
        for y in range(0, H, 2):
            bm.set(x, y)
    bm.line(0, H - 1, W - 1, 0)
    for k in range(8):                                   # 8 small squares: 1..8 px, to check the scale
        for dx in range(k + 1):
            for dy in range(k + 1):
                bm.set(4 + k * 14 + dx, 4 + dy)
    return bm


def envelope(a, d, s, r, title=""):
    """ADSR as a polyline below a text line: times as fractions of the width, s 0..1"""
    bm = Bitmap()
    bm.text(0, 0, title)
    top, bot = 11, H - 3
    y = lambda v: bot - v * (bot - top)
    xa, xd = a * 40, a * 40 + d * 40
    xs = xd + 24
    xr = min(W - 1, xs + r * 40)
    bm.line(0, y(0), xa, y(1))
    bm.line(xa, y(1), xd, y(s))
    bm.line(xd, y(s), xs, y(s))
    bm.line(xs, y(s), xr, y(0))
    for x in range(0, W, 4):
        bm.set(x, bot + 2)
    return bm


NAMES = ["cycles", "ctx", "draws", "busy", "active", "idle", "ring rd", "ring wr", "flags 2DD|2DE<<8", "polls"]
VARIANTS = {0: "one image 128 x 48 (default)", 1: "fill rectangle pages 2-7", 2: "flash image (boot logo)",
            3: "tiles 64 x 8, y in pages", 4: "tiles 64 x 8, y in pixels"}


def open_in(name):
    import mido
    names = mido.get_input_names()
    cand = [n for n in names if "Launchkey" in n and "DAW" not in n] or [n for n in names if "Launchkey" in n]
    if name:
        cand = [n for n in names if name.replace(" In", "") in n] or cand
    if not cand:
        sys.exit(f"no Launchkey input in {names}")
    return mido.open_input(cand[0])


def diag(port, inp):
    import mido
    for _ in inp.iter_pending():
        pass
    send(port, [HEAD + [0x06, 0xF7]])
    t0 = time.time()
    while time.time() - t0 < 1.0:
        for m in inp.iter_pending():
            if m.type == "sysex" and list(m.data[:4]) == [0x7D, 0x4C, 0x4B, 0x06]:
                d = list(m.data[4:])
                vals = [sum(d[i * 5 + k] << (28 - 7 * k) for k in range(5)) for i in range(len(d) // 5)]
                print("  " + ", ".join(f"{n} {v:#x}" if n in ("ctx", "flags 2DD|2DE<<8") else f"{n} {v}"
                                       for n, v in zip(NAMES, vals)))
                return vals
        time.sleep(0.01)
    print("  no answer (diag needs the test firmware with command 06)")


def open_port(name):
    import mido
    names = mido.get_output_names()
    if name is None:
        cand = [n for n in names if "Launchkey" in n and "DAW" not in n] or [n for n in names if "Launchkey" in n]
        if not cand:
            sys.exit(f"no Launchkey port in {names}; use --port")
        name = cand[0]
    print("port:", name)
    return mido.open_output(name)


def send(port, msgs):
    import mido
    for m in msgs:
        port.send(mido.Message("sysex", data=m[1:-1]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("what", nargs="?", default="pattern", choices=["pattern", "env", "clear", "diag", "variants"])
    ap.add_argument("--port")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--fps", type=float, default=10)
    a = ap.parse_args()
    if a.list:
        import mido
        print("\n".join(mido.get_output_names()))
        return
    port = open_port(a.port)
    if a.what == "diag":
        diag(port, open_in(a.port))
        return
    if a.what == "variants":
        inp = open_in(a.port)
        for v, txt in VARIANTS.items():
            print(f"variant {v}: {txt}")
            send(port, [HEAD + [0x07, v, 0xF7]])
            send(port, messages(pattern()))
            time.sleep(1.5)
            diag(port, inp)
            time.sleep(2.5)
        send(port, [HEAD + [0x07, 0, 0xF7]])
        return
    if a.what == "clear":
        send(port, [HEAD + [0x04, 0xF7]])
    elif a.what == "pattern":
        bm = pattern()
        send(port, messages(bm))
    else:
        t0 = time.time()
        while time.time() - t0 < 10:
            t = time.time() - t0
            m = 0.5 - 0.5 * math.cos(t * 0.8)
            bm = envelope(0.05 + 0.9 * m, 0.3 + 0.5 * (1 - m), 0.2 + 0.6 * m, 0.2 + 0.8 * m,
                          f"ENV 1 MORPH {int(m * 127)}")
            send(port, messages(bm))
            time.sleep(1 / a.fps)


if __name__ == "__main__":
    main()
