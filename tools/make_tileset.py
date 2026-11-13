#!/usr/bin/env python3
"""Generate the original NetHack-Godot tileset.

    make_tileset.py GLYPHS.tsv OUT_DIR

GLYPHS.tsv is written by ``nethack_native_test --dump-glyphs`` and lists every
glyph the linked NetHack core knows about (glyph, symidx, color, ttychar,
category, symname, name).  The tool draws one 32x32 cell per glyph with an
original 5x7 bitmap font and category-specific decoration, and writes

    OUT_DIR/tiles.png       atlas, COLUMNS cells per row
    OUT_DIR/glyph_map.json  {"tile_size", "columns", "count", "cells": [...],
                             "names": [...]}  cells[glyph] = atlas cell index

All artwork is produced procedurally by this script (no third-party pixels);
see assets/LICENSES.md.
"""

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pngio import Image, write_png  # noqa: E402

TILE = 32
COLUMNS = 100
SCALE = 3
GX, GY = 8, 5  # glyph offset inside the cell (15x21 glyph)

# Glyph categories (keep in sync with native/port/nhgodot_glyphs.c)
(
    MONSTER, PET, DETECTED, RIDDEN, INVISIBLE, BODY, STATUE, OBJECT, CMAP,
    ZAP, SWALLOW, EXPLODE, WARNING, UNEXPLORED, NOTHING,
) = range(15)

# NetHack CLR_* colours 0..15
PALETTE = [
    (40, 48, 72),     # black (lifted so it shows on dark floors)
    (200, 40, 40),    # red
    (40, 170, 60),    # green
    (150, 100, 40),   # brown
    (50, 80, 210),    # blue
    (170, 50, 170),   # magenta
    (40, 160, 170),   # cyan
    (180, 180, 180),  # gray
    (200, 200, 200),  # no colour
    (255, 140, 30),   # orange
    (90, 240, 90),    # bright green
    (250, 230, 60),   # yellow
    (100, 140, 255),  # bright blue
    (250, 100, 250),  # bright magenta
    (100, 240, 250),  # bright cyan
    (255, 255, 255),  # white
]

# Original 5x7 font, ASCII 32..126; each row is 5 bits, bit 4 = leftmost.
FONT_ROWS = """
20 00000000000000  21 04040404040004  22 0A0A0A00000000  23 0A0A1F0A1F0A0A
24 040F140E051E04  25 18190204081303  26 0C12140815120D  27 04040800000000
28 02040808080402  29 08040202020408  2A 0004150E150400  2B 0004041F040400
2C 000000000C0408  2D 0000001F000000  2E 00000000000C0C  2F 00010204081000
30 0E11131519110E  31 040C040404040E  32 0E11010204081F  33 1F02040201110E
34 02060A121F0202  35 1F101E0101110E  36 0608101E11110E  37 1F010204080808
38 0E11110E11110E  39 0E11110F01020C  3A 000C0C000C0C00  3B 000C0C000C0408
3C 02040810080402  3D 00001F001F0000  3E 08040201020408  3F 0E110102040004
40 0E11010D15150E  41 0E1111111F1111  42 1E11111E11111E  43 0E11101010110E
44 1C12111111121C  45 1F10101E10101F  46 1F10101E101010  47 0E1110171111 0F
48 1111111F111111  49 0E04040404040E  4A 0702020202120C  4B 11121418141211
4C 1010101010101F  4D 111B1515111111  4E 11111915131111  4F 0E11111111110E
50 1E11111E101010  51 0E11111115120D  52 1E11111E141211  53 0F10100E01011E
54 1F040404040404  55 1111111111110E  56 1111111111 0A04 57 1111111515150A
58 11110A040A1111  59 1111110A040404  5A 1F01020408101F  5B 0E08080808080E
5C 00100804020100  5D 0E02020202020E  5E 040A1100000000  5F 0000000000001F
60 08040200000000  61 00000E010F110F  62 10101619 11111E 63 00000E1010110E
64 01010D1311110F  65 00000E111F100E  66 0609081C080808  67 000F11110F010E
68 10101619111111  69 04000C0404040E  6A 0200060202120C  6B 10101214181412
6C 0C04040404040E  6D 00001A15151111  6E 00001619111111  6F 00000E1111110E
70 00001E111E1010  71 00000D130F0101  72 00001619101010  73 00000E100E011E
74 08081C08080906  75 0000111111130D  76 0000111111 0A04 77 0000111115150A
78 0000110A040A11  79 000011110F010E  7A 00001F0204081F  7B 02040408040402
7C 04040404040404  7D 08040402040408  7E 00000815020000
"""


def _parse_font():
    font = {}
    toks = FONT_ROWS.split()
    i = 0
    while i < len(toks):
        code = int(toks[i], 16)
        i += 1
        hexrows = ""
        while len(hexrows) < 14:
            hexrows += toks[i]
            i += 1
        font[code] = [int(hexrows[k : k + 2], 16) for k in range(0, 14, 2)]
    return font


FONT = _parse_font()


def rgba(c, a=255):
    return (c[0], c[1], c[2], a)


def shade(c, f):
    return tuple(max(0, min(255, int(v * f))) for v in c[:3])


def draw_char(img, ox, oy, ch, color, scale=SCALE, shadow=True):
    rows = FONT.get(ch) or FONT[ord("?")]
    for ry, bits in enumerate(rows):
        for rx in range(5):
            if bits & (0x10 >> rx):
                x = ox + rx * scale
                y = oy + ry * scale
                if shadow:
                    img.fill_rect(x + 1, y + 1, scale, scale, (0, 0, 0, 200))
    for ry, bits in enumerate(rows):
        for rx in range(5):
            if bits & (0x10 >> rx):
                img.fill_rect(ox + rx * scale, oy + ry * scale, scale, scale, rgba(color))


def border(img, x, y, color, w=2):
    img.fill_rect(x, y, TILE, w, color)
    img.fill_rect(x, y + TILE - w, TILE, w, color)
    img.fill_rect(x, y, w, TILE, color)
    img.fill_rect(x + TILE - w, y, w, TILE, color)


def bricks(img, x, y, base):
    dark = rgba(shade(base, 0.55))
    img.fill_rect(x, y, TILE, TILE, rgba(base))
    for row in range(4):
        yy = y + row * 8
        img.fill_rect(x, yy + 7, TILE, 1, dark)
        off = 0 if row % 2 == 0 else 8
        for col in range(off, TILE, 16):
            img.fill_rect(x + col, yy, 1, 7, dark)


WALL_THEME = {
    "": (88, 82, 96),
    "mines": (110, 84, 60),
    "gehennom": (110, 40, 36),
    "knox": (120, 110, 60),
    "sokoban": (50, 80, 120),
}


def background(name, category):
    """Return (kind, rgb) for a cell's background."""
    n = name.lower()
    if category == CMAP:
        if "wall" in n or n.startswith("stone"):
            theme = ""
            for t in ("mines", "gehennom", "knox", "sokoban"):
                if "(" + t + ")" in n:
                    theme = t
            if n.startswith("stone") and "wall" not in n:
                return ("flat", (24, 22, 28))
            return ("bricks", WALL_THEME[theme])
        if "lava" in n:
            return ("flat", (150, 50, 10))
        if "water" in n or "pool" in n or "moat" in n:
            return ("flat", (20, 50, 130))
        if "ice" in n:
            return ("flat", (150, 200, 220))
        if "tree" in n:
            return ("flat", (20, 70, 30))
        if "cloud" in n or "air" in n:
            return ("flat", (70, 80, 100))
        if "corridor" in n:
            return ("flat", (36, 34, 40))
        if "dark part" in n:
            return ("flat", (20, 20, 26))
        if "door" in n or "drawbridge" in n:
            return ("flat", (60, 44, 30))
        return ("flat", (46, 44, 52))
    if category == ZAP:
        return ("flat", (40, 30, 60))
    if category == EXPLODE:
        return ("flat", (90, 50, 20))
    if category == SWALLOW:
        return ("flat", (80, 30, 50))
    if category == WARNING:
        return ("flat", (60, 20, 20))
    if category in (UNEXPLORED, NOTHING):
        return ("flat", (0, 0, 0))
    return ("clear", None)


def draw_tile(img, cx, cy, row):
    """Draw one glyph cell at pixel (cx, cy).  row = TSV dict."""
    cat = int(row["category"])
    name = row["name"]
    color = PALETTE[int(row["color"]) % 16]
    ch = int(row["ttychar"]) & 0xFF
    kind, bg = background(name, cat)
    if kind == "bricks":
        bricks(img, cx, cy, bg)
    elif kind == "flat":
        img.fill_rect(cx, cy, TILE, TILE, rgba(bg))
    else:
        img.fill_rect(cx, cy, TILE, TILE, (0, 0, 0, 0))
    if cat in (UNEXPLORED, NOTHING):
        return
    if ch < 32 or ch > 126:
        ch = ord("?")
    if kind == "bricks":
        color = (235, 230, 220)
    draw_char(img, cx + GX, cy + GY, ch, color)
    # category decorations
    if cat == PET:
        heart = [0x0A, 0x1F, 0x1F, 0x0E, 0x04]
        for ry, bits in enumerate(heart):
            for rx in range(5):
                if bits & (0x10 >> rx):
                    img.fill_rect(cx + 24 + rx, cy + 2 + ry, 1, 1, (255, 60, 90, 255))
    elif cat == DETECTED:
        border(img, cx, cy, (80, 230, 255, 255), 1)
    elif cat == RIDDEN:
        img.fill_rect(cx + 6, cy + 27, 20, 3, (160, 100, 40, 255))
    elif cat == INVISIBLE:
        border(img, cx, cy, (160, 160, 160, 160), 1)
    elif cat == BODY:
        img.fill_rect(cx + 2, cy + 28, 8, 2, (150, 30, 30, 255))
    elif cat == STATUE:
        img.fill_rect(cx + 4, cy + 28, 24, 3, (150, 150, 160, 255))
    elif cat == WARNING:
        border(img, cx, cy, rgba(color), 2)
    if "(pile)" in name:
        for k in range(3):
            img.fill_rect(cx + 25, cy + 22 + k * 3, 5, 2, (230, 230, 230, 255))


def read_glyphs(path):
    rows = []
    with open(path, encoding="utf-8") as fh:
        header = fh.readline().rstrip("\n").split("\t")
        for line in fh:
            line = line.rstrip("\n")
            if not line:
                continue
            vals = line.split("\t")
            vals += [""] * (len(header) - len(vals))
            rows.append(dict(zip(header, vals)))
    rows.sort(key=lambda r: int(r["glyph"]))
    for i, r in enumerate(rows):
        if int(r["glyph"]) != i:
            raise SystemExit("glyph table is not dense at %d" % i)
    return rows


def atlas_size(count):
    rows = (count + COLUMNS - 1) // COLUMNS
    return COLUMNS * TILE, max(1, rows) * TILE


def cell_origin(index):
    return (index % COLUMNS) * TILE, (index // COLUMNS) * TILE


def write_map(path, rows, source):
    data = {
        "tileset": source,
        "tile_size": TILE,
        "columns": COLUMNS,
        "count": len(rows),
        "cells": list(range(len(rows))),
        "names": [r["name"] for r in rows],
    }
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(data, fh, separators=(",", ":"))


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    rows = read_glyphs(argv[1])
    os.makedirs(argv[2], exist_ok=True)
    w, h = atlas_size(len(rows))
    img = Image(w, h)
    for r in rows:
        x, y = cell_origin(int(r["glyph"]))
        draw_tile(img, x, y, r)
    write_png(os.path.join(argv[2], "tiles.png"), img)
    write_map(os.path.join(argv[2], "glyph_map.json"), rows, "original")
    print("wrote %d tiles (%dx%d) to %s" % (len(rows), w, h, argv[2]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
