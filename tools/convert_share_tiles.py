#!/usr/bin/env python3
"""Build the optional "Classic" tileset from NetHack's own win/share tiles.

    convert_share_tiles.py GLYPHS.tsv SHARE_DIR OUT_DIR

Reads monsters.txt, objects.txt and other.txt (NetHack's 16x16 text tile
format) from SHARE_DIR, matches every glyph in GLYPHS.tsv to a tile and
writes an atlas with the same layout as make_tileset.py (one 32x32 cell per
glyph, nearest-neighbour upscaled):

    OUT_DIR/tiles_classic.png
    OUT_DIR/glyph_map_classic.json

Glyphs with no counterpart in the share files (for example the "remembered
unseen monster" marker) get the procedurally drawn tile from make_tileset.py
so the map still covers every glyph.  The share tiles are distributed under
the NetHack General Public License (see assets/LICENSES.md).
"""

import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_tileset as mt  # noqa: E402
from pngio import Image, write_png  # noqa: E402

HEADER = re.compile(r"^# tile (\d+) \((.*)\)\s*$")
PALETTE = re.compile(r"^(\S) = \((\d+), (\d+), (\d+)\)\s*$")
SRC = 16


def read_share(path):
    """Return [(name, pixels)] where pixels is a list of 16 rows of RGBA."""
    palette, tiles = {}, []
    with open(path, encoding="latin-1") as fh:
        lines = [ln.rstrip("\n") for ln in fh]
    i = 0
    while i < len(lines):
        ln = lines[i]
        m = PALETTE.match(ln)
        if m:
            palette[m.group(1)] = (int(m.group(2)), int(m.group(3)), int(m.group(4)))
            i += 1
            continue
        m = HEADER.match(ln)
        if m:
            name = m.group(2)
            j = i + 1
            while j < len(lines) and lines[j].strip() != "{":
                j += 1
            rows = []
            j += 1
            while j < len(lines) and lines[j].strip() != "}":
                text = lines[j].strip()
                rows.append(
                    [
                        (0, 0, 0, 0) if c == "." else palette.get(c, (255, 0, 255)) + (255,)
                        for c in text
                    ]
                )
                j += 1
            tiles.append((name, rows))
            i = j + 1
            continue
        i += 1
    return tiles


def paste(img, cx, cy, rows, grey=False):
    for y, row in enumerate(rows[:SRC]):
        for x, px in enumerate(row[:SRC]):
            if grey and px[3]:
                v = (px[0] * 3 + px[1] * 6 + px[2]) // 10
                px = (v, v, v, 255)
            img.fill_rect(cx + x * 2, cy + y * 2, 2, 2, px)


def build_index(share_dir):
    mons = read_share(os.path.join(share_dir, "monsters.txt"))
    objs = read_share(os.path.join(share_dir, "objects.txt"))
    other = read_share(os.path.join(share_dir, "other.txt"))
    mon_by_name = {}
    for name, px in mons:
        base, _, sex = name.rpartition(",")
        if not base:
            base = name
        mon_by_name.setdefault(base, px)
        mon_by_name.setdefault(base + "," + sex, px)
    obj_by_name = {}
    for name, px in objs:
        obj_by_name.setdefault(name, px)
        if " / " in name:
            desc, real = name.split(" / ", 1)
            obj_by_name.setdefault(real, px)
            obj_by_name.setdefault(desc, px)
    return mon_by_name, obj_by_name, other


THEMES = ("mines", "gehennom", "knox", "sokoban")


def match_tiles(rows, mon_by_name, obj_by_name, other):
    """Return {glyph: (pixels, grey)} for every glyph we can match."""
    out = {}
    # Non-themed cmap glyphs appear in defsyms order, as do other.txt tiles
    # 0..77 (stone .. trapped chest) and 110..119 (dig beam .. valid
    # position); zaps, swallow, explosions and warnings are matched by
    # number below.
    plain_cmap_tiles = list(range(0, 78)) + list(range(110, 120))
    plain_seen = 0
    theme_seen = {t: 0 for t in THEMES}
    for r in rows:
        g, cat, name = int(r["glyph"]), int(r["category"]), r["name"]
        tile = None
        grey = False
        if cat in (mt.MONSTER, mt.PET, mt.DETECTED, mt.RIDDEN):
            base = re.sub(r"^(tame|detected|ridden) ", "", name)
            tile = mon_by_name.get(base)
        elif cat == mt.STATUE:
            base = re.sub(r"^statue of ", "", name).replace(" (pile)", "")
            tile, grey = mon_by_name.get(base), True
        elif cat == mt.BODY:
            tile = obj_by_name.get("corpse")
        elif cat == mt.OBJECT:
            base = name.replace(" (pile)", "")
            tile = obj_by_name.get(base) or obj_by_name.get("generic " + base)
        elif cat == mt.CMAP:
            theme = next((t for t in THEMES if name.endswith("(" + t + ")")), None)
            if theme:
                k = theme_seen[theme]
                theme_seen[theme] += 1
                idx = 199 + 11 * THEMES.index(theme) + k
            else:
                idx = (
                    plain_cmap_tiles[plain_seen]
                    if plain_seen < len(plain_cmap_tiles)
                    else None
                )
                plain_seen += 1
            if idx is not None and idx < len(other):
                tile = other[idx][1]
        elif cat == mt.ZAP:
            t, d = (int(v) for v in re.findall(r"\d+", name)[-2:])
            tile = other[78 + t * 4 + d][1]
        elif cat == mt.SWALLOW:
            n = int(re.findall(r"\((\d+)\)", name)[-1])
            tile = other[120 + n][1]
        elif cat == mt.EXPLODE:
            t, p = (int(v) for v in re.findall(r"\d+", name)[-2:])
            tile = other[128 + t * 9 + p][1]
        elif cat == mt.WARNING:
            tile = other[191 + int(re.findall(r"\d+", name)[-1])][1]
        elif cat == mt.UNEXPLORED:
            tile = other[197][1]
        elif cat == mt.NOTHING:
            tile = other[198][1]
        if tile:
            out[g] = (tile, grey)
    return out


def main(argv):
    if len(argv) != 4:
        print(__doc__)
        return 2
    rows = mt.read_glyphs(argv[1])
    mon_by_name, obj_by_name, other = build_index(argv[2])
    matched = match_tiles(rows, mon_by_name, obj_by_name, other)
    os.makedirs(argv[3], exist_ok=True)
    w, h = mt.atlas_size(len(rows))
    img = Image(w, h)
    for r in rows:
        g = int(r["glyph"])
        x, y = mt.cell_origin(g)
        if g in matched:
            img.fill_rect(x, y, mt.TILE, mt.TILE, (0, 0, 0, 255))
            paste(img, x, y, *matched[g])
        else:
            mt.draw_tile(img, x, y, r)
    write_png(os.path.join(argv[3], "tiles_classic.png"), img)
    mt.write_map(os.path.join(argv[3], "glyph_map_classic.json"), rows, "classic")
    print(
        "classic tileset: %d/%d glyphs from win/share, %d procedural"
        % (len(matched), len(rows), len(rows) - len(matched))
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
