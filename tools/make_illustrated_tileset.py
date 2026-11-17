#!/usr/bin/env python3
"""Generate the "illustrated" NetHack-Godot tileset.

    make_illustrated_tileset.py GLYPHS.tsv OUT_DIR

Same input and layout as make_tileset.py (32x32 cells, COLUMNS per row,
cell index == glyph index), written as

    OUT_DIR/tiles_illustrated.png
    OUT_DIR/glyph_map_illustrated.json

Creatures and items are painted icons - shaded, outlined shapes with a gloss
highlight - set in a dark rounded frame with a bevel and a coloured radial
glow behind them.  Terrain is full-bleed and textured (stone blocks,
flagstones, water, lava ...).  Every pixel is computed here from geometric
shapes, gradients and a hash-based noise: no image is read, so the result is
original work (CC0, see godot/assets/LICENSES.md), and the same GLYPHS.tsv
always gives the same bytes.
"""

import math
import os
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_tileset import (  # noqa: E402
    BODY, CMAP, DETECTED, EXPLODE, FONT, INVISIBLE, MONSTER, NOTHING, OBJECT,
    PALETTE, PET, RIDDEN, STATUE, SWALLOW, TILE, UNEXPLORED, WALL_THEME,
    WARNING, ZAP, atlas_size, cell_origin, read_glyphs, write_map,
)
from pngio import Image, write_png  # noqa: E402

OUTLINE = (14, 10, 18)
WHITE = (255, 255, 255)
GOLD = (235, 190, 60)
SILVER = (200, 205, 215)
WOOD = (125, 80, 40)
SKIN = (225, 180, 140)
THEMES = ("mines", "gehennom", "knox", "sokoban")


# --- small maths helpers ---------------------------------------------------

def clamp(v):
    return 0.0 if v < 0 else (255.0 if v > 255 else v)


def mul(c, f):
    return (clamp(c[0] * f), clamp(c[1] * f), clamp(c[2] * f))


def mix(a, b, t):
    return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t,
            a[2] + (b[2] - a[2]) * t)


def lum(c):
    return 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2]


def visible(c):
    """Lift very dark colours so they read on the dark frame."""
    return mix(c, (150, 150, 170), 0.45) if lum(c) < 70 else c


def hash2(x, y, seed=0):
    """Deterministic noise in [0, 1)."""
    h = (x * 374761393 + y * 668265263 + seed * 2246822519) & 0xFFFFFFFF
    h = ((h ^ (h >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((h ^ (h >> 16)) & 0xFFFF) / 65536.0


def name_hash(s):
    return zlib.crc32(s.encode("utf-8"))


def seg_dist(x, y, x0, y0, x1, y1):
    dx, dy = x1 - x0, y1 - y0
    ll = dx * dx + dy * dy
    t = 0.0 if ll == 0 else max(0.0, min(1.0, ((x - x0) * dx + (y - y0) * dy) / ll))
    return math.hypot(x - x0 - t * dx, y - y0 - t * dy)


def rrect_sdf(x, y, cx, cy, hx, hy, r):
    qx = abs(x - cx) - hx + r
    qy = abs(y - cy) - hy + r
    return math.hypot(max(qx, 0.0), max(qy, 0.0)) + min(max(qx, qy), 0.0) - r


# --- canvas -----------------------------------------------------------------

class Canvas:
    """A TILE x TILE straight-alpha float RGBA buffer (rgb 0..255, a 0..1)."""

    def __init__(self, fill=None):
        if fill is None:
            self.px = [[0.0, 0.0, 0.0, 0.0] for _ in range(TILE * TILE)]
        else:
            self.px = [[fill[0], fill[1], fill[2], 1.0] for _ in range(TILE * TILE)]

    def copy(self):
        c = Canvas.__new__(Canvas)
        c.px = [p[:] for p in self.px]
        return c

    def over(self, i, r, g, b, a):
        d = self.px[i]
        if a >= 0.999:
            d[0], d[1], d[2], d[3] = r, g, b, 1.0
            return
        da = d[3] * (1.0 - a)
        oa = a + da
        if oa <= 0:
            return
        d[0] = (r * a + d[0] * da) / oa
        d[1] = (g * a + d[1] * da) / oa
        d[2] = (b * a + d[2] * da) / oa
        d[3] = oa

    def paint(self, fn, box=None, ss=3):
        """Composite fn(x, y) -> rgb | (r, g, b, a) | None, supersampled."""
        if box is None:
            x0, y0, x1, y1 = 0, 0, TILE, TILE
        else:
            x0 = max(0, int(math.floor(box[0])))
            y0 = max(0, int(math.floor(box[1])))
            x1 = min(TILE, int(math.ceil(box[2])))
            y1 = min(TILE, int(math.ceil(box[3])))
        offs = [(k + 0.5) / ss for k in range(ss)]
        n = float(ss * ss)
        for py in range(y0, y1):
            for px in range(x0, x1):
                r = g = b = a = 0.0
                for oy in offs:
                    for ox in offs:
                        c = fn(px + ox, py + oy)
                        if c is None:
                            continue
                        ca = c[3] if len(c) > 3 else 1.0
                        r += c[0] * ca
                        g += c[1] * ca
                        b += c[2] * ca
                        a += ca
                if a > 0:
                    self.over(py * TILE + px, r / a, g / a, b / a, a / n)

    def layer(self, other, dx=0, dy=0):
        for y in range(TILE):
            sy = y - dy
            if not 0 <= sy < TILE:
                continue
            for x in range(TILE):
                sx = x - dx
                if 0 <= sx < TILE:
                    s = other.px[sy * TILE + sx]
                    if s[3] > 0:
                        self.over(y * TILE + x, s[0], s[1], s[2], s[3])

    def mapped(self, fn):
        """A copy with every pixel's rgb passed through fn."""
        c = self.copy()
        for p in c.px:
            if p[3] > 0:
                p[0], p[1], p[2] = fn((p[0], p[1], p[2]))
        return c

    def rotated(self):
        """A copy turned 90 degrees clockwise."""
        c = Canvas()
        for y in range(TILE):
            for x in range(TILE):
                c.px[y * TILE + x] = self.px[(TILE - 1 - x) * TILE + y][:]
        return c

    def to_bytes(self):
        out = bytearray(TILE * TILE * 4)
        for i, p in enumerate(self.px):
            o = i * 4
            out[o] = int(clamp(p[0]) + 0.5)
            out[o + 1] = int(clamp(p[1]) + 0.5)
            out[o + 2] = int(clamp(p[2]) + 0.5)
            out[o + 3] = int(max(0.0, min(1.0, p[3])) * 255 + 0.5)
        return bytes(out)


# --- shaded primitives ------------------------------------------------------

def ball(cv, cx, cy, rx, ry, color, outline=1.0, gloss=True, alpha=1.0):
    """A lit ellipse: light from the upper left, dark rim, gloss spot."""
    m = min(rx, ry)

    def fn(x, y):
        u = (x - cx) / rx
        v = (y - cy) / ry
        d = u * u + v * v
        if d > 1.0:
            return None
        if outline and (1.0 - math.sqrt(d)) * m < outline:
            return OUTLINE + (alpha,)
        c = mul(color, 1.18 - 0.5 * math.hypot(u + 0.35, v + 0.45))
        if gloss:
            h = (u + 0.38) ** 2 + (v + 0.48) ** 2
            if h < 0.06:
                c = mix(c, WHITE, 0.6 * (1.0 - h / 0.06))
        return c + (alpha,)

    cv.paint(fn, (cx - rx - 1, cy - ry - 1, cx + rx + 1, cy + ry + 1))


def rrect(cv, x0, y0, x1, y1, r, color, outline=1.0, alpha=1.0):
    cx, cy, hx, hy = (x0 + x1) / 2, (y0 + y1) / 2, (x1 - x0) / 2, (y1 - y0) / 2

    def fn(x, y):
        s = rrect_sdf(x, y, cx, cy, hx, hy, r)
        if s > 0:
            return None
        if outline and s > -outline:
            return OUTLINE + (alpha,)
        t = (y - y0) / max(1.0, y1 - y0)
        return mul(color, 1.2 - 0.45 * t) + (alpha,)

    cv.paint(fn, (x0 - 1, y0 - 1, x1 + 1, y1 + 1))


def poly(cv, pts, color, outline=1.0, alpha=1.0):
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    top, bottom = min(ys), max(ys)
    edges = list(zip(pts, pts[1:] + pts[:1]))

    def fn(x, y):
        inside = False
        for (ax, ay), (bx, by) in edges:
            if (ay > y) != (by > y) and x < ax + (bx - ax) * (y - ay) / (by - ay):
                inside = not inside
        if not inside:
            return None
        if outline:
            for (ax, ay), (bx, by) in edges:
                if seg_dist(x, y, ax, ay, bx, by) < outline:
                    return OUTLINE + (alpha,)
        t = (y - top) / max(1.0, bottom - top)
        return mul(color, 1.2 - 0.45 * t) + (alpha,)

    cv.paint(fn, (min(xs) - 1, top - 1, max(xs) + 1, bottom + 1))


def stroke(cv, x0, y0, x1, y1, w, color, outline=0.8, alpha=1.0):
    """A rounded tube from (x0, y0) to (x1, y1), w pixels thick."""
    hw = w / 2.0

    def fn(x, y):
        d = seg_dist(x, y, x0, y0, x1, y1)
        if d > hw:
            return None
        if outline and d > hw - outline:
            return OUTLINE + (alpha,)
        return mul(color, 1.15 - 0.45 * d / hw) + (alpha,)

    cv.paint(fn, (min(x0, x1) - hw - 1, min(y0, y1) - hw - 1,
                  max(x0, x1) + hw + 1, max(y0, y1) + hw + 1))


def ring(cv, cx, cy, r0, r1, color, alpha=1.0):
    def fn(x, y):
        d = math.hypot(x - cx, y - cy)
        if d < r0 or d > r1:
            return None
        if d < r0 + 0.8 or d > r1 - 0.8:
            return OUTLINE + (alpha,)
        f = 1.15 - 0.4 * ((y - cy) / r1 + 1.0) / 2.0
        return mul(color, f) + (alpha,)

    cv.paint(fn, (cx - r1 - 1, cy - r1 - 1, cx + r1 + 1, cy + r1 + 1))


def glow(cv, cx, cy, radius, color, strength=1.0, core=0.0):
    """An additive-looking soft light (alpha falls off with distance)."""
    def fn(x, y):
        d = math.hypot(x - cx, y - cy) / radius
        if d >= 1.0:
            return None
        a = strength * (1.0 - d) ** 1.6
        c = mix(color, WHITE, core * max(0.0, 1.0 - d * 2.5))
        return c + (min(1.0, a),)

    cv.paint(fn, (cx - radius, cy - radius, cx + radius, cy + radius), ss=1)


def text(cv, x, y, ch, color, shadow=True):
    """The make_tileset 5x7 font at 1px, with a dark outline."""
    rows = FONT.get(ord(ch)) if isinstance(ch, str) else FONT.get(ch)
    if rows is None:
        rows = FONT[ord("?")]
    on = set()
    for ry, bits in enumerate(rows):
        for rx in range(5):
            if bits & (0x10 >> rx):
                on.add((rx, ry))
    if shadow:
        for rx, ry in on:
            for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1), (1, 1)):
                px, py = x + rx + dx, y + ry + dy
                if (rx + dx, ry + dy) not in on and 0 <= px < TILE and 0 <= py < TILE:
                    cv.over(py * TILE + px, OUTLINE[0], OUTLINE[1], OUTLINE[2], 0.9)
    for rx, ry in on:
        px, py = x + rx, y + ry
        if 0 <= px < TILE and 0 <= py < TILE:
            cv.over(py * TILE + px, color[0], color[1], color[2], 1.0)


def eyes(cv, x0, x1, y, r=1.3, iris=(20, 10, 10)):
    for x in (x0, x1):
        ball(cv, x, y, r, r, (250, 245, 230), outline=0.5, gloss=False)
        ball(cv, x + 0.3, y + 0.2, r * 0.55, r * 0.55, iris, outline=0, gloss=False)


# --- frames and backgrounds -------------------------------------------------

_frames = {}


def frame(glow_color, bevel=None):
    """The dark rounded cell with a bevel and a radial glow behind the icon."""
    key = (tuple(int(v) for v in glow_color), bevel)
    if key in _frames:
        return _frames[key]
    cv = Canvas()
    base = (12, 12, 18)

    def fn(x, y):
        s = rrect_sdf(x, y, 16, 16, 15.5, 15.5, 5)
        if s > 0:
            return None
        if s > -1.3:
            if bevel is not None:
                return bevel + (1.0,)
            return ((86, 86, 104) if x + y < 32 else (4, 4, 8)) + (1.0,)
        if s > -2.2:
            return (6, 6, 10, 0.95)
        d = math.hypot(x - 16, y - 15) / 14.5
        g = max(0.0, 1.0 - d) ** 1.5 * 0.8
        c = (base[0] + glow_color[0] * g, base[1] + glow_color[1] * g,
             base[2] + glow_color[2] * g)
        return c + (0.93,)

    cv.paint(fn, ss=2)
    _frames[key] = cv
    return cv


def theme_of(name):
    for t in THEMES:
        if "(" + t + ")" in name:
            return t
    return ""


def noise_fill(cv, base, amount, seed, scale=1):
    for y in range(TILE):
        for x in range(TILE):
            n = hash2(x // scale, y // scale, seed) - 0.5
            p = cv.px[y * TILE + x]
            c = mul(base, 1.0 + amount * n)
            p[0], p[1], p[2], p[3] = c[0], c[1], c[2], 1.0


def stone_blocks(base, seed):
    cv = Canvas()
    for y in range(TILE):
        row = y // 8
        off = 8 if row % 2 else 0
        for x in range(TILE):
            bx = (x + off) // 16
            lx, ly = (x + off) % 16, y % 8
            f = 0.85 + 0.3 * hash2(bx, row, seed) + 0.12 * (hash2(x, y, seed + 1) - 0.5)
            if ly == 7 or lx == 15:
                f = 0.35
            elif ly == 0 or lx == 0:
                f *= 1.25
            elif ly == 6 or lx == 14:
                f *= 0.75
            c = mul(base, f)
            cv.px[y * TILE + x] = [c[0], c[1], c[2], 1.0]
    return cv


def flagstones(base, seed, size=16):
    cv = Canvas()
    for y in range(TILE):
        for x in range(TILE):
            bx, by = x // size, y // size
            lx, ly = x % size, y % size
            f = 0.88 + 0.18 * hash2(bx, by, seed) + 0.14 * (hash2(x, y, seed + 7) - 0.5)
            if lx == size - 1 or ly == size - 1:
                f = 0.5
            elif lx == 0 or ly == 0:
                f *= 1.12
            c = mul(base, f)
            cv.px[y * TILE + x] = [c[0], c[1], c[2], 1.0]
    return cv


FLOOR = (78, 74, 82)
THEME_FLOOR = {"": FLOOR, "mines": (96, 78, 58), "gehennom": (96, 52, 44),
               "knox": (88, 82, 70), "sokoban": (66, 78, 104)}


def floor(theme="", dark=False):
    cv = flagstones(THEME_FLOOR.get(theme, FLOOR), 11)
    if dark:
        cv = cv.mapped(lambda c: mul(c, 0.45))
    return cv


# --- creatures -------------------------------------------------------------

def humanoid(cv, col, s=1.0, bulky=False):
    def P(x, y):
        return 16 + (x - 16) * s, 29 + (y - 29) * s

    w = 1.3 if bulky else 1.0
    legc = mul(col, 0.6)
    for lx0, lx1 in ((13.5, 12.5), (18.5, 19.5)):
        a, b = P(lx0, 22), P(lx1, 28)
        stroke(cv, a[0], a[1], b[0], b[1], 3.6 * s * w, legc)
    for ax0, ax1 in ((10.5, 7.5), (21.5, 24.5)):
        a, b = P(ax0, 14), P(ax1, 21)
        stroke(cv, a[0], a[1], b[0], b[1], 3.2 * s * w, mul(col, 0.85))
    pts = [P(16 - 5.5 * w, 12.5), P(16 + 5.5 * w, 12.5), P(16 + 6.5 * w, 24),
           P(16 - 6.5 * w, 24)]
    poly(cv, pts, col)
    hx, hy = P(16, 8.5)
    hr = 4.6 * s
    ball(cv, hx, hy, hr, hr, mix(col, SKIN, 0.55))
    eyes(cv, hx - 1.8 * s, hx + 1.8 * s, hy + 0.3, 0.9 * s + 0.3)
    return (hx, hy, hr), P(16, 18)


def quadruped(cv, col, big=False):
    k = 1.2 if big else 1.0
    legc = mul(col, 0.65)
    for x in (10, 13, 19, 22):
        stroke(cv, x, 19, x - 0.5, 27, 3.2, legc)
    stroke(cv, 7.5, 17, 4, 12, 2.4, mul(col, 0.8))
    ball(cv, 16, 18, 9 * k, 5.5 * k, col)
    hx, hy, hr = 24.5, 12.5, 4.6
    poly(cv, [(22, 9.5), (23.5, 5), (25, 9)], mul(col, 0.8), outline=0.7)
    ball(cv, hx, hy, hr, 4.0, col)
    ball(cv, hx + 3.2, hy + 1.6, 1.8, 1.4, mul(col, 0.7), gloss=False)
    eyes(cv, hx - 0.8, hx + 1.8, hy - 0.8, 1.0)
    return (hx, hy, hr), (16, 18)


def insect(cv, col, spider=False):
    legc = mul(col, 0.55)
    if spider:
        for i, dy in enumerate((-4, -1.5, 1.5, 4)):
            for side in (-1, 1):
                ex = 16 + side * (11 - abs(dy) * 0.3)
                stroke(cv, 16, 19 + dy * 0.5, 16 + side * 7, 15 + dy, 1.8, legc)
                stroke(cv, 16 + side * 7, 15 + dy, ex, 22 + dy * 1.2, 1.6, legc)
        ball(cv, 16, 21, 7, 6, col)
        ball(cv, 16, 13, 4, 3.5, mul(col, 0.9))
        eyes(cv, 14.6, 17.4, 12.5, 0.9, iris=(200, 20, 20))
        return (16, 13, 4), (16, 21)
    for x in (12, 16, 20):
        stroke(cv, x, 19, x - 3, 26, 1.6, legc)
        stroke(cv, x, 19, x + 2, 26, 1.6, legc)
    ball(cv, 8.5, 19, 4.5, 4, col)
    ball(cv, 15.5, 18.5, 3.5, 3, col)
    ball(cv, 22.5, 16.5, 4, 3.8, col)
    stroke(cv, 24, 13.5, 27, 7, 1.2, legc, outline=0)
    stroke(cv, 22, 13.5, 21, 7, 1.2, legc, outline=0)
    eyes(cv, 22, 25, 16, 0.9)
    return (22.5, 16.5, 4), (15.5, 18.5)


def blob(cv, col, alpha=1.0):
    ball(cv, 16, 20, 10.5, 8, col, alpha=alpha)
    ball(cv, 12, 23, 3, 2, mul(col, 1.15), outline=0, gloss=False, alpha=alpha * 0.6)
    eyes(cv, 13, 19, 17, 1.5)
    return (16, 15, 6), (16, 20)


def fungus(cv, col):
    rrect(cv, 13, 15, 19, 27, 2, (220, 210, 190))
    ball(cv, 16, 14, 11, 7, col)
    for x, y in ((11, 12), (17, 10), (21, 14)):
        ball(cv, x, y, 1.6, 1.3, (245, 240, 225), outline=0, gloss=False)
    return (16, 11, 7), (16, 18)


def floating_eye(cv, col):
    ball(cv, 16, 16, 11, 11, (245, 245, 240))
    ball(cv, 16.5, 16.5, 6, 6, col, gloss=False)
    ball(cv, 16.5, 16.5, 2.6, 2.6, (10, 10, 14), outline=0, gloss=False)
    ball(cv, 13.5, 13.5, 1.6, 1.6, WHITE, outline=0, gloss=False)
    return (16, 9, 5), (16, 16)


def vortex(cv, col):
    for k in range(3):
        r = 11 - k * 3.5
        for i in range(10):
            a0 = i * 0.7 + k * 1.3
            a1 = a0 + 0.55
            stroke(cv, 16 + r * math.cos(a0), 16 + r * 0.7 * math.sin(a0),
                   16 + (r - 1.2) * math.cos(a1), 16 + (r - 1.2) * 0.7 * math.sin(a1),
                   2.2, mix(col, WHITE, 0.15 * k), outline=0.5)
    glow(cv, 16, 16, 7, mix(col, WHITE, 0.5), 0.8, core=0.6)
    return (16, 10, 5), (16, 16)


def serpent(cv, col, human=False):
    pts = [(5 + t * 3.1, 21 + 3.5 * math.sin(t * 1.05)) for t in range(7)]
    for i, (x, y) in enumerate(pts):
        r = 2.6 + i * 0.2
        ball(cv, x, y, r, r, col if i % 2 else mul(col, 0.85), gloss=False)
    hx, hy = 25, 12.5
    stroke(cv, pts[-1][0], pts[-1][1], hx, hy + 2, 4.5, col)
    if human:
        ball(cv, hx, hy, 4.4, 4.4, mix(col, SKIN, 0.6))
        eyes(cv, hx - 1.7, hx + 1.7, hy, 1.0)
        return (hx, hy, 4.4), (16, 21)
    ball(cv, hx, hy, 4.4, 3.4, col)
    stroke(cv, hx + 3.5, hy + 1.5, hx + 6.5, hy + 2.5, 1.0, (220, 30, 40), outline=0)
    eyes(cv, hx - 1.2, hx + 1.6, hy - 0.8, 0.9, iris=(180, 120, 0))
    return (hx, hy, 4.4), (16, 21)


def wings(cv, col, cy=15, span=13, alpha=0.95):
    wc = mul(col, 0.8)
    poly(cv, [(15, cy), (16 - span, cy - 7), (16 - span + 2, cy + 2),
              (16 - span + 5, cy + 0.5), (13, cy + 5)], wc, alpha=alpha)
    poly(cv, [(17, cy), (16 + span, cy - 7), (16 + span - 2, cy + 2),
              (16 + span - 5, cy + 0.5), (19, cy + 5)], wc, alpha=alpha)


def flyer(cv, col):
    wings(cv, col, 15)
    ball(cv, 16, 18, 4.2, 6.5, col)
    ball(cv, 16, 10.5, 3.8, 3.6, col)
    poly(cv, [(15, 12), (17, 12), (16, 15)], (230, 170, 40), outline=0.5)
    eyes(cv, 14.4, 17.6, 10, 0.9)
    return (16, 10.5, 3.8), (16, 18)


def dragon(cv, col):
    wings(cv, col, 12, 15)
    q = quadruped(cv, col, big=True)
    poly(cv, [(12, 12.5), (14, 9.5), (16, 12.5), (18, 9.5), (20, 12.5)],
         mul(col, 0.7), outline=0.6)
    return q


def ghost(cv, col, dark=False):
    c = (60, 60, 80) if dark else mix(col, (235, 240, 255), 0.6)
    pts = [(9, 27), (9, 13), (12, 6.5), (16, 5), (20, 6.5), (23, 13), (23, 27),
           (20.5, 24), (18, 27), (16, 24), (14, 27), (11.5, 24)]
    poly(cv, pts, c, alpha=0.82)
    if dark:
        for x in (13, 19):
            glow(cv, x, 13, 2.6, (255, 60, 60), 1.0, core=0.4)
    else:
        for x in (13, 19):
            ball(cv, x, 13, 1.6, 2.2, (20, 20, 30), outline=0, gloss=False)
    return (16, 9, 5), (16, 17)


def golem(cv, col):
    legc = mul(col, 0.7)
    rrect(cv, 10, 21, 15, 28, 1.5, legc)
    rrect(cv, 17, 21, 22, 28, 1.5, legc)
    rrect(cv, 5, 12, 10, 22, 1.5, mul(col, 0.85))
    rrect(cv, 22, 12, 27, 22, 1.5, mul(col, 0.85))
    rrect(cv, 9, 11, 23, 23, 2, col)
    rrect(cv, 11.5, 3, 20.5, 11, 2, col)
    eyes(cv, 14, 18, 7, 1.0, iris=(220, 120, 0))
    return (16, 7, 4.5), (16, 17)


def light(cv, col):
    for i in range(8):
        a = i * math.pi / 4
        stroke(cv, 16 + 5 * math.cos(a), 16 + 5 * math.sin(a),
               16 + 13 * math.cos(a), 16 + 13 * math.sin(a), 1.6,
               mix(col, WHITE, 0.4), outline=0)
    glow(cv, 16, 16, 14, col, 0.9, core=0.5)
    ball(cv, 16, 16, 5.5, 5.5, mix(col, WHITE, 0.5))
    return (16, 11, 5), (16, 16)


def piercer(cv, col):
    poly(cv, [(16, 27), (9, 7), (23, 7)], col)
    eyes(cv, 14, 18, 12, 1.1)
    return (16, 9, 5), (16, 15)


def chest(cv, col):
    rrect(cv, 6, 14, 26, 27, 2, col)
    rrect(cv, 6, 8, 26, 15, 3, mul(col, 1.1))
    rrect(cv, 14.5, 12.5, 17.5, 17.5, 1, GOLD, outline=0.6)
    return (16, 11, 5), (16, 18)


def fish(cv, col):
    poly(cv, [(4, 12), (9, 18), (4, 24)], mul(col, 0.8))
    ball(cv, 17, 18, 10, 6.5, col)
    poly(cv, [(14, 12), (19, 6), (21, 13)], mul(col, 0.8), outline=0.7)
    for x in (15, 18):
        stroke(cv, x, 15, x - 1, 21, 0.8, mul(col, 0.6), outline=0)
    eyes(cv, 22, 22, 16.5, 1.3)
    return (22, 16, 4), (17, 18)


def mouth_horns(cv, head, col):
    hx, hy, hr = head
    for side in (-1, 1):
        poly(cv, [(hx + side * hr * 0.4, hy - hr * 0.6),
                  (hx + side * hr * 1.3, hy - hr * 1.9),
                  (hx + side * hr * 0.95, hy - hr * 0.3)], col, outline=0.6)


HUMANOIDS = {
    "@": (1.0, False), "K": (0.95, False), "G": (0.75, False),
    "h": (0.8, False), "k": (0.78, False), "o": (0.9, False),
    "O": (1.1, True), "T": (1.1, True), "H": (1.15, True), "V": (1.0, False),
    "Z": (0.95, False), "M": (0.95, False), "L": (1.0, False),
    "n": (0.9, False), "l": (0.7, False), "Q": (0.9, False),
    "Y": (1.0, True), "U": (1.1, True), "z": (1.0, True), "&": (1.05, False),
    "A": (1.0, False), "C": (1.1, True), "i": (0.65, False), "g": (0.7, False),
}


def creature(cv, letter, col, name):
    """Paint the monster for class `letter`; returns (head, body) anchors."""
    n = name.lower()
    if letter == "A":
        wings(cv, (240, 240, 255), 13, 14)
        h = humanoid(cv, col)
        ring(cv, h[0][0], h[0][1] - 6, 2.5, 4, GOLD)
        return h
    if letter in ("&", "i"):
        if letter == "i" or "bat" in n:
            wings(cv, col, 14, 12)
        h = humanoid(cv, col, *HUMANOIDS[letter])
        mouth_horns(cv, h[0], (230, 220, 200))
        return h
    if letter == "g":
        wings(cv, mul(col, 0.8), 14, 11)
        return humanoid(cv, col, *HUMANOIDS[letter])
    if letter in HUMANOIDS:
        return humanoid(cv, col, *HUMANOIDS[letter])
    if letter in ("d", "f", "q", "r", "u", "R", "c" if "chickatrice" in n else "_"):
        return quadruped(cv, col, big=letter in ("q", "u"))
    if letter == "c":
        return flyer(cv, col)
    if letter in ("D", "J"):
        return dragon(cv, col)
    if letter in ("B",):
        return flyer(cv, col)
    if letter in ("a", "x"):
        return insect(cv, col)
    if letter == "s":
        return insect(cv, col, spider=True)
    if letter in ("S", "w", "~"):
        return serpent(cv, col)
    if letter == "N":
        return serpent(cv, col, human=True)
    if letter == ";":
        return fish(cv, col)
    if letter == "F":
        return fungus(cv, col)
    if letter == "e":
        return floating_eye(cv, col)
    if letter == "v":
        return vortex(cv, col)
    if letter == "y":
        return light(cv, col)
    if letter == "p":
        return piercer(cv, col)
    if letter == "'":
        return golem(cv, col)
    if letter in (" ", "W"):
        return ghost(cv, col, dark=letter == "W")
    if letter in ("m", "]"):
        return chest(cv, mix(col, WOOD, 0.5))
    if letter == "j":
        return blob(cv, col, alpha=0.75)
    return blob(cv, col)


_sprites = {}


def monster_sprite(letter, color, name):
    """Cached creature sprite (transparent background, no badge)."""
    acc = name_hash(name) % 5
    key = (letter, color, acc)
    if key in _sprites:
        return _sprites[key]
    cv = Canvas()
    col = visible(PALETTE[color % 16])
    head, body = creature(cv, letter, col, name)
    hx, hy, hr = head
    if acc == 1 and letter not in ("e", "v", "y", "F"):
        mouth_horns(cv, head, mix(col, (240, 230, 210), 0.6))
    elif acc == 2:
        for k, (dx, dy) in enumerate(((-3, -1), (2, 1.5), (-0.5, 3))):
            ball(cv, body[0] + dx, body[1] + dy, 1.3, 1.1, mul(col, 0.6),
                 outline=0, gloss=False)
    elif acc == 3 and letter not in ("e", "v", "y", "F"):
        poly(cv, [(hx - hr * 0.8, hy - hr * 0.5), (hx, hy - hr * 1.7),
                  (hx + hr * 0.8, hy - hr * 0.5)], mix(col, (220, 40, 40), 0.6),
             outline=0.6)
    _sprites[key] = cv
    return cv


def badge(cv, letter, col):
    rrect(cv, 23.5, 22.5, 30.5, 31, 2, (16, 16, 24), outline=0.7)
    text(cv, 25, 23, letter, mix(visible(col), WHITE, 0.25), shadow=False)


def heart(cv):
    ball(cv, 5, 6, 2.4, 2.4, (240, 60, 100), outline=0.6, gloss=False)
    ball(cv, 9, 6, 2.4, 2.4, (240, 60, 100), outline=0.6, gloss=False)
    poly(cv, [(2.8, 7), (11.2, 7), (7, 11.5)], (240, 60, 100), outline=0)
    ball(cv, 4.5, 5.5, 0.9, 0.9, (255, 200, 210), outline=0, gloss=False)


def saddle(cv):
    poly(cv, [(10, 14), (22, 14), (21, 19), (11, 19)], (140, 80, 35), outline=0.7)
    stroke(cv, 16, 19, 16, 25, 1.0, (60, 40, 20), outline=0)
    rrect(cv, 14.5, 24, 17.5, 26.5, 1, SILVER, outline=0.5)


def pile_marker(cv):
    for k in range(3):
        rrect(cv, 24, 2.5 + k * 2.6, 30, 4.8 + k * 2.6, 1, (235, 225, 200), outline=0.5)


# --- objects ---------------------------------------------------------------

OBJ_GLOW = {
    "!": (150, 60, 220), "?": (230, 130, 40), "+": (130, 60, 220),
    "/": (140, 70, 230), "=": (60, 120, 240), '"': (230, 140, 40),
    ")": (220, 120, 40), "[": (60, 110, 230), "%": (60, 170, 70),
    "(": (220, 130, 40), "*": (60, 130, 250), "$": (240, 190, 40),
    "`": (120, 120, 130), "0": (120, 120, 130), "_": (120, 120, 140),
    ".": (60, 180, 80),
}


def has(n, *words):
    return any(w in n for w in words)


def weapon(cv, col, n):
    metal = mix(SILVER, col, 0.35)
    if has(n, "bow") and not has(n, "crossbow bolt"):
        for i in range(8):
            a0 = -1.2 + i * 0.3
            a1 = a0 + 0.3
            stroke(cv, 10 + 12 * math.cos(a0), 16 + 12 * math.sin(a0),
                   10 + 12 * math.cos(a1), 16 + 12 * math.sin(a1), 2.6, WOOD)
        stroke(cv, 13.5, 4.5, 13.5, 27.5, 0.7, (230, 230, 220), outline=0)
        return
    if has(n, "arrow", "dart", "bolt", "ya", "shuriken"):
        if has(n, "shuriken"):
            poly(cv, [(16, 5), (19, 13), (27, 16), (19, 19), (16, 27), (13, 19),
                      (5, 16), (13, 13)], metal)
            ball(cv, 16, 16, 2, 2, OUTLINE, outline=0, gloss=False)
            return
        stroke(cv, 7, 25, 23, 9, 1.8, WOOD)
        poly(cv, [(26, 6), (20, 9.5), (22.5, 12)], metal, outline=0.6)
        poly(cv, [(7, 25), (4, 24), (6, 21), (9, 23)], (220, 60, 60), outline=0.5)
        return
    if has(n, "axe"):
        stroke(cv, 9, 27, 20, 6, 2.6, WOOD)
        poly(cv, [(16, 8), (25, 6), (27, 14), (22, 17), (18, 14)], metal)
        return
    if has(n, "spear", "lance", "pike", "halberd", "glaive", "trident", "fork",
           "guisarme", "bardiche", "voulge", "partisan", "ranseur", "spetum",
           "bec de", "lucern", "naginata", "polearm", "javelin"):
        stroke(cv, 6, 27, 22, 10, 2.2, WOOD)
        poly(cv, [(27, 4), (20, 9), (23, 12)], metal, outline=0.6)
        return
    if has(n, "mace", "club", "hammer", "flail", "morning star", "staff",
           "quarterstaff", "aklys", "whip", "rubber hose", "cudgel"):
        stroke(cv, 8, 27, 18, 12, 2.6, WOOD)
        ball(cv, 20.5, 9.5, 5, 5, metal)
        for a in range(0, 360, 60):
            r = math.radians(a)
            ball(cv, 20.5 + 5.2 * math.cos(r), 9.5 + 5.2 * math.sin(r), 1.2, 1.2,
                 metal, outline=0.5, gloss=False)
        return
    if has(n, "sling", "boomerang", "chakram"):
        poly(cv, [(6, 22), (16, 8), (26, 22), (22, 23), (16, 14), (10, 23)], WOOD)
        return
    short = has(n, "dagger", "knife", "stiletto", "athame", "scalpel", "crysknife")
    tip = (23, 8) if short else (26, 5)
    poly(cv, [(10.5, 19.5), (tip[0] - 1.5, tip[1] - 0.5), tip,
              (tip[0] + 0.5, tip[1] + 1.5), (12.5, 21.5)], metal, outline=0.8)
    stroke(cv, 8, 17, 15, 24, 2.2, GOLD)
    stroke(cv, 10.5, 21.5, 6.5, 25.5, 2.4, WOOD)
    ball(cv, 5.5, 26.5, 1.8, 1.8, GOLD, outline=0.6, gloss=False)


def armour(cv, col, n):
    c = mix(col, SILVER, 0.2)
    if has(n, "helm", "hat", "cap", "fedora", "cornuthaum", "dunce", "kabuto", "visor"):
        if has(n, "cornuthaum", "dunce"):
            poly(cv, [(16, 3), (24, 25), (8, 25)], c)
            return
        ball(cv, 16, 18, 10, 9, c)
        rrect(cv, 4, 18, 28, 23, 2, mul(c, 0.85))
        return
    if has(n, "boots", "shoes"):
        poly(cv, [(8, 6), (16, 6), (16, 20), (26, 21), (26, 27), (8, 27)], c)
        return
    if has(n, "glove", "gauntlet"):
        poly(cv, [(9, 27), (9, 12), (11, 6), (13, 12), (14, 5), (16, 12), (17, 5),
                  (19, 12), (20, 7), (22, 13), (25, 14), (22, 27)], c)
        return
    if has(n, "shield"):
        poly(cv, [(6, 6), (26, 6), (25, 17), (16, 28), (7, 17)], c)
        poly(cv, [(13, 10), (19, 10), (18.5, 17), (16, 21), (13.5, 17)],
             mix(c, GOLD, 0.6), outline=0.6)
        return
    if has(n, "cloak", "robe", "mantle", "apron", "tunic", "coat"):
        poly(cv, [(12, 5), (20, 5), (26, 27), (16, 25), (6, 27)], c)
        ball(cv, 16, 6.5, 2, 2, GOLD, outline=0.6, gloss=False)
        return
    if has(n, "shirt"):
        poly(cv, [(10, 6), (22, 6), (28, 12), (24, 15), (23, 27), (9, 27), (8, 15),
                  (4, 12)], c)
        return
    poly(cv, [(9, 6), (13, 6), (16, 9), (19, 6), (23, 6), (27, 11), (24, 14),
              (24, 27), (8, 27), (8, 14), (5, 11)], c)
    stroke(cv, 16, 11, 16, 24, 1.0, mul(c, 0.7), outline=0)
    stroke(cv, 10, 19, 22, 19, 1.0, mul(c, 0.7), outline=0)


def tool(cv, col, n):
    if has(n, "chest", "box", "icebox"):
        chest(cv, mix(col, WOOD, 0.5))
        return
    if has(n, "candle"):
        rrect(cv, 13, 12, 19, 27, 1.5, mix(col, (240, 235, 210), 0.6))
        glow(cv, 16, 8, 6, (255, 180, 60), 0.9, core=0.6)
        poly(cv, [(16, 4), (18, 9), (16, 11), (14, 9)], (255, 220, 120), outline=0)
        return
    if has(n, "lamp", "lantern"):
        glow(cv, 16, 16, 12, (255, 190, 80), 0.6, core=0.3)
        ball(cv, 16, 19, 8, 6, mix(col, GOLD, 0.5))
        stroke(cv, 23, 19, 27, 15, 2.2, mix(col, GOLD, 0.5))
        poly(cv, [(16, 9), (18, 14), (14, 14)], (255, 220, 120), outline=0)
        return
    if has(n, "key", "lock pick", "credit card"):
        ring(cv, 10, 11, 2.5, 5.5, GOLD)
        stroke(cv, 14, 15, 25, 26, 2.4, GOLD)
        stroke(cv, 22, 23, 25, 20, 2.0, GOLD)
        return
    if has(n, "pick-axe", "mattock"):
        stroke(cv, 8, 27, 20, 9, 2.4, WOOD)
        poly(cv, [(8, 7), (20, 7), (28, 13), (19, 10), (10, 10)], SILVER)
        return
    if has(n, "horn", "whistle", "flute", "bugle", "harp", "drum"):
        poly(cv, [(5, 12), (24, 8), (27, 4), (28, 16), (24, 13), (6, 16)],
             mix(col, GOLD, 0.5))
        return
    if has(n, "mirror", "crystal ball", "lenses", "blindfold", "towel"):
        ball(cv, 16, 14, 8, 8, mix(col, (180, 220, 255), 0.6))
        stroke(cv, 16, 22, 16, 28, 3, WOOD)
        return
    poly(cv, [(10, 11), (22, 11), (27, 26), (5, 26)], mix(col, (150, 110, 70), 0.6))
    stroke(cv, 11, 10, 21, 10, 2.6, mix(col, (150, 110, 70), 0.4))
    stroke(cv, 13, 8, 19, 8, 1.4, (200, 170, 90), outline=0.4)


def food(cv, col, n):
    if has(n, "egg"):
        ball(cv, 16, 17, 7, 9, (245, 238, 220))
        return
    if has(n, "apple", "orange", "melon", "pear", "banana", "carrot", "lichen",
           "kelp", "eucalyptus", "slime mold", "fruit"):
        ball(cv, 16, 18, 9, 8.5, col)
        stroke(cv, 16, 10, 17, 5, 1.6, WOOD)
        poly(cv, [(17, 7), (24, 4), (21, 9)], (60, 170, 60), outline=0.6)
        return
    if has(n, "tin"):
        rrect(cv, 8, 9, 24, 26, 3, SILVER)
        rrect(cv, 8, 13, 24, 21, 1, mix(col, (200, 60, 60), 0.5), outline=0.5)
        return
    if has(n, "ration", "wafer", "pancake", "cake", "cookie", "candy", "cream pie"):
        rrect(cv, 6, 11, 26, 24, 3, mix(col, (200, 160, 100), 0.5))
        stroke(cv, 8, 17.5, 24, 17.5, 1.0, (120, 80, 40), outline=0)
        return
    stroke(cv, 18, 18, 26, 26, 3.2, (240, 235, 220))
    ball(cv, 26.5, 25.5, 2.3, 2.3, (240, 235, 220), gloss=False)
    ball(cv, 13.5, 13.5, 8, 7, mix(col, (170, 80, 50), 0.5))


def gem(cv, col, n):
    if has(n, "rock", "stone", "flint", "loadstone", "touchstone") or lum(col) < 80:
        grey = mix(col, (140, 135, 130), 0.6)
        poly(cv, [(8, 14), (14, 8), (22, 9), (26, 16), (23, 25), (12, 26), (6, 21)], grey)
        stroke(cv, 13, 15, 18, 20, 0.8, mul(grey, 0.6), outline=0)
        return
    c = visible(col)
    poly(cv, [(9, 12), (13, 7), (19, 7), (23, 12), (16, 27)], c)
    poly(cv, [(9, 12), (23, 12), (16, 27)], mul(c, 0.8), outline=0.6)
    poly(cv, [(13, 7), (16, 12), (19, 7)], mix(c, WHITE, 0.4), outline=0.5)
    glow(cv, 12, 9, 3.5, WHITE, 0.7)


def potion(cv, col):
    rrect(cv, 13.5, 3.5, 18.5, 7, 1, WOOD, outline=0.6)
    rrect(cv, 13, 6.5, 19, 13, 1.5, (190, 210, 230), outline=0.8, alpha=0.9)
    ball(cv, 16, 19.5, 8.5, 8.5, (190, 210, 230), alpha=0.9)
    liquid = visible(col)

    def fn(x, y):
        if y < 16.5:
            return None
        d = math.hypot(x - 16, y - 19.5)
        if d > 7.2:
            return None
        f = 1.15 - 0.4 * (y - 16.5) / 10
        return mul(liquid, f) + (0.95,)

    cv.paint(fn, (8, 16, 24, 28))
    glow(cv, 12.5, 16.5, 2.5, WHITE, 0.9)


def scroll(cv, col):
    paper = (228, 210, 165)
    rrect(cv, 8, 8, 24, 25, 1, paper)
    stroke(cv, 6.5, 8, 25.5, 8, 4.2, mul(paper, 0.9))
    stroke(cv, 6.5, 25, 25.5, 25, 4.2, mul(paper, 0.85))
    for y in (13, 16, 19):
        stroke(cv, 11, y, 21 - (y % 3), y, 0.9, (110, 85, 60), outline=0)
    ball(cv, 20, 21.5, 2.5, 2.5, mix(visible(col), (200, 40, 40), 0.5), outline=0.6)


def spellbook(cv, col):
    c = visible(col)
    rrect(cv, 9, 6, 26, 27, 1.5, (235, 230, 210))
    rrect(cv, 6, 5, 23, 27, 2, c)
    stroke(cv, 8.5, 6, 8.5, 26, 1.4, mul(c, 0.6), outline=0)
    ball(cv, 15, 15, 3.5, 3.5, GOLD, outline=0.6)


def wand(cv, col):
    stroke(cv, 8, 25, 22, 9, 3.2, mix(visible(col), WOOD, 0.4))
    glow(cv, 23, 8, 6, mix(visible(col), WHITE, 0.5), 0.9, core=0.7)
    for x, y in ((26, 12), (19, 5), (27, 5)):
        ball(cv, x, y, 0.9, 0.9, WHITE, outline=0, gloss=False)


def ring_icon(cv, col):
    ring(cv, 16, 19, 5, 8.5, GOLD)
    gemc = visible(col)
    ball(cv, 16, 10, 3.6, 3.2, gemc)


def amulet(cv, col):
    for (x0, y0, x1, y1) in ((9, 5, 16, 17), (23, 5, 16, 17)):
        for k in range(6):
            t0, t1 = k / 6.0, (k + 0.6) / 6.0
            stroke(cv, x0 + (x1 - x0) * t0, y0 + (y1 - y0) * t0,
                   x0 + (x1 - x0) * t1, y0 + (y1 - y0) * t1, 1.4, GOLD, outline=0.4)
    ball(cv, 16, 21.5, 5.5, 5.5, GOLD)
    ball(cv, 16, 21.5, 3, 3, visible(col))


def coins(cv):
    for x, y in ((11, 22), (21, 23), (16, 19), (12, 15), (20, 14), (16, 10)):
        ball(cv, x, y, 5, 3, GOLD)
        stroke(cv, x - 2, y, x + 2, y, 0.8, mul(GOLD, 0.7), outline=0)


def boulder(cv, col):
    c = mix(col, (130, 125, 120), 0.7)
    ball(cv, 16, 17, 12, 11, c)
    for (x0, y0, x1, y1) in ((10, 12, 14, 18), (18, 20, 23, 23), (19, 9, 21, 13)):
        stroke(cv, x0, y0, x1, y1, 0.9, mul(c, 0.55), outline=0)


def chain(cv, col):
    c = mix(col, (150, 150, 160), 0.6)
    for k in range(4):
        ring(cv, 7 + k * 6, 16 + (2 if k % 2 else -2), 1.6, 3.6, c)


def object_icon(cv, ch, col, n):
    if ch == "!":
        potion(cv, col)
    elif ch == "?":
        scroll(cv, col)
    elif ch == "+":
        spellbook(cv, col)
    elif ch == "/":
        wand(cv, col)
    elif ch == "=":
        ring_icon(cv, col)
    elif ch == '"':
        amulet(cv, col)
    elif ch == ")":
        weapon(cv, col, n)
    elif ch == "[":
        armour(cv, col, n)
    elif ch == "%":
        food(cv, col, n)
    elif ch == "(":
        tool(cv, col, n)
    elif ch == "*":
        gem(cv, col, n)
    elif ch == "$":
        coins(cv)
    elif ch in ("`", "0"):
        if has(n, "heavy iron ball"):
            ball(cv, 16, 17, 10, 10, (60, 62, 70))
        else:
            boulder(cv, col)
    elif ch == "_":
        chain(cv, col)
    elif ch == ".":
        ball(cv, 16, 18, 9, 6, visible(col), alpha=0.85)
    else:
        ball(cv, 16, 17, 8, 8, visible(col))


# --- terrain ----------------------------------------------------------------

def stairs(cv, up):
    for k in range(4):
        y0 = 6 + k * 5
        x0 = 6 + (3 - k if up else k) * 2
        rrect(cv, x0, y0, 32 - x0, y0 + 5, 1, mul((150, 145, 140), 0.75 + 0.1 * k),
              outline=0.7)
    poly(cv, [(16, 1.5 if up else 30.5), (11, 6 if up else 26), (21, 6 if up else 26)],
         (250, 220, 90), outline=0.6)


def ladder(cv, up):
    for x in (10, 22):
        stroke(cv, x, 3, x, 29, 2.4, WOOD)
    for y in range(6, 29, 5):
        stroke(cv, 10, y, 22, y, 1.8, mul(WOOD, 1.2))
    poly(cv, [(26, 3 if up else 29), (23, 8 if up else 24), (29, 8 if up else 24)],
         (250, 220, 90), outline=0.6)


TRAP_COLOR = (200, 60, 60)


def trap_icon(cv, sym, col):
    s = sym
    if "pit" in s or "hole" in s or "trap_door" in s:
        ball(cv, 16, 17, 11, 8, (12, 10, 14), gloss=False)
        if "spiked" in s:
            for x in (11, 15, 19, 23):
                poly(cv, [(x - 1.5, 21), (x, 14), (x + 1.5, 21)], SILVER, outline=0.4)
        return
    if "web" in s:
        for a in range(0, 180, 30):
            r = math.radians(a)
            stroke(cv, 16 - 13 * math.cos(r), 16 - 13 * math.sin(r),
                   16 + 13 * math.cos(r), 16 + 13 * math.sin(r), 0.8,
                   (225, 225, 235), outline=0)
        for rr in (4, 8, 12):
            ring(cv, 16, 16, rr - 0.4, rr + 0.4, (225, 225, 235), alpha=0.8)
        return
    if "bear" in s:
        ring(cv, 16, 17, 6, 10, (140, 140, 150))
        for a in range(0, 360, 40):
            r = math.radians(a)
            poly(cv, [(16 + 6 * math.cos(r - 0.15), 17 + 6 * math.sin(r - 0.15)),
                      (16 + 3 * math.cos(r), 17 + 3 * math.sin(r)),
                      (16 + 6 * math.cos(r + 0.15), 17 + 6 * math.sin(r + 0.15))],
                 SILVER, outline=0.3)
        return
    if "land_mine" in s:
        ball(cv, 16, 18, 8, 5, (70, 72, 60))
        glow(cv, 16, 15, 4, (255, 40, 40), 1.0, core=0.5)
        return
    if "teleport" in s or "portal" in s or "magic" in s or "polymorph" in s:
        for k in range(3):
            ring(cv, 16, 16, 3 + k * 3.5, 4.5 + k * 3.5,
                 mix(visible(col), WHITE, 0.2 * k), alpha=0.9)
        glow(cv, 16, 16, 12, visible(col), 0.5)
        return
    if "fire" in s:
        glow(cv, 16, 18, 12, (255, 120, 20), 0.8)
        poly(cv, [(16, 5), (22, 17), (19, 26), (13, 26), (10, 17)], (255, 160, 40),
             outline=0.6)
        poly(cv, [(16, 13), (19, 20), (16, 25), (13, 20)], (255, 235, 140), outline=0)
        return
    if "gas" in s or "rust" in s:
        c = (170, 90, 210) if "gas" in s else (190, 100, 40)
        for x, y, r in ((11, 18, 5), (20, 17, 6), (16, 12, 5)):
            ball(cv, x, y, r, r * 0.8, c, outline=0.6, alpha=0.85)
        return
    if "rock" in s or "boulder" in s:
        boulder(cv, (130, 125, 120))
        return
    if "squeaky" in s:
        rrect(cv, 4, 12, 28, 20, 1, WOOD)
        return
    if "arrow" in s or "dart" in s:
        stroke(cv, 7, 25, 23, 9, 1.8, WOOD)
        poly(cv, [(26, 6), (20, 9.5), (22.5, 12)], SILVER, outline=0.6)
        return
    if "statue" in s:
        rrect(cv, 11, 6, 21, 26, 3, (160, 160, 168))
        return
    if "vibrating" in s:
        glow(cv, 16, 16, 13, (200, 80, 255), 0.9, core=0.5)
        rrect(cv, 11, 11, 21, 21, 1, (220, 160, 255))
        return
    poly(cv, [(16, 6), (26, 16), (16, 26), (6, 16)], visible(col) if col else TRAP_COLOR)


def water(base, seed, crest):
    cv = Canvas()
    for y in range(TILE):
        for x in range(TILE):
            w = math.sin((x + 2.5 * math.sin(y * 0.45)) * 0.4 + y * 0.9 + seed)
            f = 0.85 + 0.12 * w + 0.08 * (hash2(x, y, seed) - 0.5)
            c = mul(base, f)
            if w > 0.93:
                c = mix(c, crest, 0.6)
            cv.px[y * TILE + x] = [c[0], c[1], c[2], 1.0]
    return cv


def terrain(row):
    """Full-bleed terrain tiles for the CMAP category."""
    sym = row["symname"]
    name = row["name"].lower()
    theme = theme_of(name)
    col = PALETTE[int(row["color"]) % 16]
    s = sym[2:] if sym.startswith("S_") else sym
    if s == "stone":
        cv = Canvas((0, 0, 0))
        noise_fill(cv, (26, 24, 30), 0.5, 3, 2)
        return cv
    if s.endswith("wall") or s in ("tlcorn", "trcorn", "blcorn", "brcorn", "crwall",
                                   "tuwall", "tdwall", "tlwall", "trwall"):
        base = WALL_THEME.get(theme, WALL_THEME[""])
        return stone_blocks(mix(base, (130, 125, 120), 0.3), 5)
    if s in ("room", "darkroom", "engroom"):
        cv = floor(theme, dark=s == "darkroom")
        if s == "engroom":
            for y in (12, 16, 20):
                stroke(cv, 9, y, 23, y, 0.8, (40, 36, 44), outline=0)
        return cv
    if s in ("corr", "litcorr", "engrcorr"):
        cv = Canvas((0, 0, 0))
        noise_fill(cv, (70, 66, 62) if s == "litcorr" else (48, 46, 46), 0.6, 17, 2)
        return cv
    if s in ("ndoor",):
        cv = floor(theme)
        rrect(cv, 0, 0, 4, 32, 0, WOOD, outline=0.6)
        rrect(cv, 28, 0, 32, 32, 0, WOOD, outline=0.6)
        return cv
    if s in ("vodoor", "hodoor"):
        cv = floor(theme)
        if s == "vodoor":
            rrect(cv, 0, 2, 32, 8, 1, WOOD)
        else:
            rrect(cv, 2, 0, 8, 32, 1, WOOD)
        return cv
    if s in ("vcdoor", "hcdoor"):
        cv = Canvas((60, 40, 22))
        for x in range(0, 32, 8):
            rrect(cv, x, 0, x + 8, 32, 1, mul(WOOD, 0.9 + 0.08 * (x % 16 == 0)),
                  outline=0.7)
        for y in (7, 24):
            stroke(cv, 0, y, 32, y, 2.2, (90, 90, 100))
        ball(cv, 24, 16, 1.8, 1.8, GOLD, outline=0.5)
        return cv
    if s in ("vodbridge", "hodbridge"):
        cv = Canvas((60, 40, 22))
        for y in range(0, 32, 6):
            rrect(cv, 0, y, 32, y + 6, 1, WOOD, outline=0.6)
        return cv
    if s in ("vcdbridge", "hcdbridge"):
        cv = water((30, 60, 130), 2, (150, 190, 240))
        rrect(cv, 0, 12, 32, 20, 0, (60, 40, 22))
        return cv
    if s in ("upstair", "dnstair", "brupstair", "brdnstair"):
        cv = floor(theme)
        stairs(cv, "up" in s)
        return cv
    if s in ("upladder", "dnladder", "brupladder", "brdnladder"):
        cv = floor(theme)
        ladder(cv, "up" in s)
        return cv
    if s == "altar":
        cv = floor(theme)
        glow(cv, 16, 16, 15, visible(col), 0.7)
        rrect(cv, 5, 12, 27, 26, 2, (180, 175, 170))
        rrect(cv, 3, 9, 29, 13, 1, (215, 210, 200))
        ball(cv, 16, 18, 3, 3, visible(col))
        return cv
    if s == "grave":
        cv = Canvas((0, 0, 0))
        noise_fill(cv, (55, 75, 45), 0.5, 21, 2)
        poly(cv, [(9, 28), (9, 11), (12, 6), (20, 6), (23, 11), (23, 28)], (150, 150, 155))
        stroke(cv, 16, 10, 16, 20, 1.2, (80, 80, 90), outline=0)
        stroke(cv, 12.5, 13.5, 19.5, 13.5, 1.2, (80, 80, 90), outline=0)
        return cv
    if s == "throne":
        cv = floor(theme)
        rrect(cv, 9, 3, 23, 22, 3, mix(GOLD, (200, 40, 40), 0.25))
        rrect(cv, 6, 17, 26, 28, 2, GOLD)
        ball(cv, 16, 9, 2.5, 2.5, (220, 40, 60))
        return cv
    if s == "sink":
        cv = floor(theme)
        ball(cv, 16, 18, 11, 8, (200, 205, 215))
        ball(cv, 16, 18, 7, 4.5, (90, 100, 120), outline=0.6, gloss=False)
        stroke(cv, 16, 4, 16, 11, 2.2, SILVER)
        return cv
    if s == "fountain":
        cv = floor(theme)
        ball(cv, 16, 20, 12, 8, (170, 170, 180))
        ball(cv, 16, 20, 9, 5.5, (60, 120, 220), outline=0.6)
        stroke(cv, 16, 6, 16, 18, 2, (190, 220, 255), outline=0)
        glow(cv, 16, 7, 5, (200, 230, 255), 0.8)
        return cv
    if s in ("pool", "water"):
        return water((30, 70, 170) if s == "pool" else (24, 50, 140), 1, (170, 210, 255))
    if s == "lava":
        cv = Canvas()
        for y in range(TILE):
            for x in range(TILE):
                w = math.sin(x * 0.5 + 2 * math.sin(y * 0.4)) * math.cos(y * 0.35 - x * 0.1)
                f = 0.75 + 0.35 * w + 0.15 * (hash2(x, y, 9) - 0.5)
                c = mix((150, 30, 5), (255, 200, 60), max(0.0, min(1.0, f - 0.4)))
                cv.px[y * TILE + x] = [c[0], c[1], c[2], 1.0]
        return cv
    if s == "lavawall":
        cv = stone_blocks((150, 50, 15), 13)
        return cv
    if s == "ice":
        cv = Canvas((0, 0, 0))
        noise_fill(cv, (170, 210, 230), 0.15, 4, 3)
        for (x0, y0, x1, y1) in ((3, 8, 14, 3), (12, 26, 28, 15), (20, 6, 29, 2)):
            stroke(cv, x0, y0, x1, y1, 1.0, (245, 250, 255), outline=0)
        return cv
    if s == "tree":
        cv = Canvas((0, 0, 0))
        noise_fill(cv, (40, 80, 35), 0.5, 6, 2)
        rrect(cv, 14, 18, 18, 29, 1, WOOD)
        ball(cv, 16, 13, 11, 10, (50, 140, 55))
        ball(cv, 12, 11, 3, 3, (80, 180, 80), outline=0, gloss=False)
        return cv
    if s == "bars":
        cv = floor(theme, dark=True)
        for x in (5, 11, 16, 21, 27):
            stroke(cv, x, 0, x, 32, 2.6, (130, 135, 150))
        stroke(cv, 0, 6, 32, 6, 2.2, (110, 115, 130))
        stroke(cv, 0, 26, 32, 26, 2.2, (110, 115, 130))
        return cv
    if s in ("air", "cloud", "poisoncloud"):
        base = {"air": (110, 160, 230), "cloud": (110, 115, 130),
                "poisoncloud": (90, 140, 60)}[s]
        cv = Canvas(base)
        for x, y, r in ((9, 12, 7), (21, 10, 8), (15, 22, 9), (27, 24, 6)):
            ball(cv, x, y, r, r * 0.75, mix(base, WHITE, 0.45), outline=0, alpha=0.8)
        return cv
    if s.endswith("_trap") or s in ("web", "hole", "trap_door", "magic_portal",
                                    "vibrating_square", "level_teleporter",
                                    "teleportation_trap", "pit", "spiked_pit"):
        cv = floor(theme)
        trap_icon(cv, s, col)
        return cv
    # beams, sparkles and other effect symbols drawn on the map: transparent
    cv = Canvas()
    effect(cv, s, col)
    return cv


# --- effects ---------------------------------------------------------------

BEAM_DIRS = {"vbeam": (0, 1), "hbeam": (1, 0), "lslant": (1, 1), "rslant": (1, -1)}
POS = {"tl": (-1, -1), "tc": (0, -1), "tr": (1, -1), "ml": (-1, 0), "mc": (0, 0),
       "mr": (1, 0), "bl": (-1, 1), "bc": (0, 1), "br": (1, 1)}


def beam(cv, d, col):
    dx, dy = d
    ln = math.hypot(dx, dy)
    nx, ny = -dy / ln, dx / ln
    c = visible(col)

    def fn(x, y):
        dist = abs((x - 16) * nx + (y - 16) * ny)
        if dist > 7:
            return None
        a = math.exp(-dist * dist / 6.0)
        return mix(c, WHITE, max(0.0, 1.0 - dist / 1.8)) + (a,)

    cv.paint(fn, ss=2)


def effect(cv, s, col):
    c = visible(col)
    if s in BEAM_DIRS:
        beam(cv, BEAM_DIRS[s], col)
    elif s in ("digbeam", "flashbeam"):
        glow(cv, 16, 16, 15, c, 1.0, core=0.7)
    elif s.startswith("boom"):
        ball(cv, 16, 16, 9, 5, mix(WOOD, c, 0.3))
    elif s.startswith("ss"):
        for k, (x, y) in enumerate(((10, 10), (22, 12), (16, 22))):
            glow(cv, x, y, 6 - k, c, 1.0, core=0.8)
    elif s == "goodpos":
        ring(cv, 16, 16, 9, 11, (90, 230, 120), alpha=0.9)
    elif s == "hcdbridge" or s == "vcdbridge":
        pass
    else:
        glow(cv, 16, 16, 12, c, 0.6)
        ch = int(ord("?"))
        text(cv, 14, 12, ch, mix(c, WHITE, 0.3))


def explosion(row):
    s = row["symname"]
    pos = POS.get(s.rsplit("_", 1)[-1], (0, 0))
    col = visible(PALETTE[int(row["color"]) % 16])
    cx, cy = 16 - pos[0] * 32, 16 - pos[1] * 32
    cv = Canvas()

    def fn(x, y):
        d = math.hypot(x - cx, y - cy) / 44.0
        if d >= 1.0:
            return None
        n = hash2(int(x) // 3, int(y) // 3, 31) - 0.5
        t = max(0.0, min(1.0, d + 0.18 * n))
        c = mix(mix(WHITE, col, min(1.0, t * 2.2)), mul(col, 0.4), max(0.0, t * 1.6 - 0.6))
        return c + (min(1.0, (1.0 - t) * 1.8),)

    cv.paint(fn, ss=1)
    return cv


def swallow(row):
    s = row["symname"]
    pos = POS.get(s.rsplit("_", 1)[-1], (0, 0))
    col = visible(PALETTE[int(row["color"]) % 16])
    cx, cy = 16 - pos[0] * 32, 16 - pos[1] * 32
    cv = Canvas()
    for y in range(TILE):
        for x in range(TILE):
            d = math.hypot(x - cx, y - cy)
            rib = 0.5 + 0.5 * math.sin(d * 0.55)
            f = 0.35 + 0.45 * rib + 0.1 * (hash2(x, y, 23) - 0.5)
            c = mul(col, f)
            cv.px[y * TILE + x] = [c[0], c[1], c[2], 1.0]
    return cv


def warning(row):
    col = visible(PALETTE[int(row["color"]) % 16])
    cv = Canvas()
    glow(cv, 16, 16, 15, col, 0.9, core=0.3)
    ball(cv, 16, 16, 7, 7, mul(col, 0.8))
    text(cv, 14, 13, int(row["ttychar"]) & 0xFF, WHITE)
    return cv


def invisible():
    cv = Canvas()

    def fn(x, y):
        d = math.hypot(x - 16, y - 16)
        if abs(d - 11) > 1.0:
            return None
        a = math.atan2(y - 16, x - 16)
        if math.sin(a * 8) < 0:
            return None
        return (200, 205, 230, 0.8)

    cv.paint(fn)
    glow(cv, 16, 16, 10, (160, 170, 210), 0.35)
    text(cv, 14, 12, "?", (230, 235, 255))
    return cv


# --- glyph cells -------------------------------------------------------------

def monster_letter(row):
    ch = int(row["ttychar"]) & 0xFF
    return chr(ch) if 32 <= ch <= 126 else "?"


def dead_name(name, prefix, suffix):
    n = name.replace(" (pile)", "")
    if prefix and n.startswith(prefix):
        n = n[len(prefix):]
    if suffix and n.endswith(suffix):
        n = n[: -len(suffix)]
    return n


class Painter:
    def __init__(self, rows):
        # monster name -> (class letter, colour), for corpses and statues
        self.monsters = {}
        for r in rows:
            if int(r["category"]) == MONSTER:
                self.monsters.setdefault(r["name"], (monster_letter(r), int(r["color"])))
        self.cache = {}

    def cell(self, row):
        cat = int(row["category"])
        name = row["name"]
        pile = "(pile)" in name
        color = int(row["color"])
        if cat in (MONSTER, PET, DETECTED, RIDDEN):
            base = dead_name(name, {PET: "tame ", DETECTED: "detected ",
                                    RIDDEN: "ridden "}.get(cat, ""), "")
            key = ("mon", cat, monster_letter(row), color, name_hash(base) % 5)
            return self._cached(key, lambda: self._monster(row, cat, base))
        if cat == BODY:
            base = dead_name(name, "", " corpse")
            letter, mcol = self.monsters.get(base, ("?", color))
            key = ("body", letter, color, name_hash(base) % 5, pile)
            return self._cached(key, lambda: self._body(letter, color, base, pile))
        if cat == STATUE:
            base = dead_name(name, "statue of ", "")
            letter, mcol = self.monsters.get(base, ("?", 7))
            key = ("statue", letter, name_hash(base) % 5, pile)
            return self._cached(key, lambda: self._statue(letter, mcol, base, pile))
        if cat == OBJECT:
            ch = chr(int(row["ttychar"]) & 0xFF)
            key = ("obj", ch, color, dead_name(name, "", ""), pile)
            return self._cached(key, lambda: self._object(ch, color, name, pile))
        if cat == CMAP:
            key = ("cmap", row["symname"], color, theme_of(name.lower()))
            return self._cached(key, lambda: terrain(row))
        if cat == ZAP:
            key = ("zap", row["symname"], color)
            return self._cached(key, lambda: self._zap(row))
        if cat == EXPLODE:
            key = ("expl", row["symname"], color)
            return self._cached(key, lambda: explosion(row))
        if cat == SWALLOW:
            key = ("swal", row["symname"], color)
            return self._cached(key, lambda: swallow(row))
        if cat == WARNING:
            key = ("warn", row["ttychar"], color)
            return self._cached(key, lambda: warning(row))
        if cat == INVISIBLE:
            return self._cached(("invis",), invisible)
        return self._cached(("black",), lambda: Canvas((0, 0, 0)))

    def _cached(self, key, make):
        if key not in self.cache:
            self.cache[key] = make().to_bytes()
        return self.cache[key]

    def _monster(self, row, cat, base):
        letter = monster_letter(row)
        color = int(row["color"])
        col = visible(PALETTE[color % 16])
        bevel = (80, 230, 255) if cat == DETECTED else None
        cv = frame(mix(col, (60, 60, 80), 0.35), bevel).copy()
        cv.layer(monster_sprite(letter, color, base))
        if cat == RIDDEN:
            saddle(cv)
        if cat == PET:
            heart(cv)
        badge(cv, letter, col)
        return cv

    def _body(self, letter, color, base, pile):
        cv = frame((110, 30, 30)).copy()
        ball(cv, 16, 25, 12, 3.5, (120, 15, 20), outline=0, gloss=False, alpha=0.85)
        sprite = monster_sprite(letter, color, base).rotated()
        cv.layer(sprite.mapped(lambda c: mix(mul(c, 0.7), (110, 90, 80), 0.35)), 0, 3)
        if pile:
            pile_marker(cv)
        return cv

    def _statue(self, letter, mcol, base, pile):
        cv = frame((110, 110, 125)).copy()
        stone = (170, 170, 178)
        sprite = monster_sprite(letter, mcol, base).mapped(
            lambda c: mul(stone, 0.35 + 0.8 * lum(c) / 255.0))
        cv.layer(sprite, 0, -2)
        rrect(cv, 6, 26, 26, 30.5, 1, (140, 140, 150), outline=0.7)
        if pile:
            pile_marker(cv)
        return cv

    def _object(self, ch, color, name, pile):
        col = PALETTE[color % 16]
        glow_c = OBJ_GLOW.get(ch, (100, 100, 120))
        cv = frame(glow_c).copy()
        object_icon(cv, ch, col, name.lower())
        if pile:
            pile_marker(cv)
        return cv

    def _zap(self, row):
        s = row["symname"][2:]
        cv = Canvas()
        beam(cv, BEAM_DIRS.get(s, (1, 0)), PALETTE[int(row["color"]) % 16])
        return cv


def build(rows):
    w, h = atlas_size(len(rows))
    img = Image(w, h)
    painter = Painter(rows)
    stride = TILE * 4
    for r in rows:
        x, y = cell_origin(int(r["glyph"]))
        data = painter.cell(r)
        for ty in range(TILE):
            o = ((y + ty) * w + x) * 4
            img.data[o : o + stride] = data[ty * stride : (ty + 1) * stride]
    return img


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    rows = read_glyphs(argv[1])
    os.makedirs(argv[2], exist_ok=True)
    img = build(rows)
    write_png(os.path.join(argv[2], "tiles_illustrated.png"), img)
    write_map(os.path.join(argv[2], "glyph_map_illustrated.json"), rows, "illustrated")
    print("wrote %d illustrated tiles (%dx%d) to %s" % (len(rows), img.width, img.height,
                                                        argv[2]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
