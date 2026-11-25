"""Graphics test for the branch display-gfx: sends a 128 x 48 bitmap to the Launchkey (cfw/lk_gfx.h).

    uv run python tools/gfx_test.py --list          MIDI ports
    uv run python tools/gfx_test.py pattern         border, grid and diagonal (still image)
    uv run python tools/gfx_test.py env             an envelope morphing for 10 s (animation)
    uv run python tools/gfx_test.py clear
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

    def pages(self):
        """SSD1306 pages: per page and column one byte, LSB = top row"""
        return [[sum(self.px[p * 8 + b][x] << b for b in range(8)) for x in range(W)] for p in range(PAGES)]


def messages(bm, title=None):
    msgs = []
    for p, row in enumerate(bm.pages()):
        for x in range(0, W, 32):
            msgs.append(HEAD + [0x02, p, x, 32] + pack7(row[x:x + 32]) + [0xF7])
    if title is not None:
        msgs.append(HEAD + [0x03] + [ord(c) for c in title[:16]] + [0xF7])
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


def envelope(a, d, s, r):
    """ADSR as a polyline in the bitmap: times as fractions of the width, s 0..1"""
    bm = Bitmap()
    top, bot = 2, H - 3
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
    ap.add_argument("what", nargs="?", default="pattern", choices=["pattern", "env", "clear"])
    ap.add_argument("--port")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--fps", type=float, default=10)
    a = ap.parse_args()
    if a.list:
        import mido
        print("\n".join(mido.get_output_names()))
        return
    port = open_port(a.port)
    if a.what == "clear":
        send(port, [HEAD + [0x04, 0xF7]])
    elif a.what == "pattern":
        send(port, messages(pattern(), "Pattern"))
    else:
        t0 = time.time(); last_title = 0
        while time.time() - t0 < 10:
            t = time.time() - t0
            m = 0.5 - 0.5 * math.cos(t * 0.8)
            bm = envelope(0.05 + 0.9 * m, 0.3 + 0.5 * (1 - m), 0.2 + 0.6 * m, 0.2 + 0.8 * m)
            title = None
            if t - last_title > 1.0 or last_title == 0:      # show (popup) once a second keeps it up
                title, last_title = f"Env 1 {int(m * 127):3d}", t
            send(port, messages(bm, title))
            time.sleep(1 / a.fps)


if __name__ == "__main__":
    main()
