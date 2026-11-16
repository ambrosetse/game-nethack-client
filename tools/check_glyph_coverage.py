#!/usr/bin/env python3
"""Test: every glyph maps to a tile.

    check_glyph_coverage.py GLYPHS.tsv TILES_DIR

For each tileset found in TILES_DIR (tiles.png + glyph_map.json, and
tiles_classic.png + glyph_map_classic.json when present) check that

  * the map lists exactly as many glyphs as the core reports,
  * every glyph's cell lies inside the atlas,
  * every cell except "unexplored"/"nothing" contains visible pixels.

Exit status 0 on success, 1 on any failure.
"""

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_tileset as mt  # noqa: E402
from pngio import read_png  # noqa: E402


def check(rows, png_path, map_path):
    errors = []
    with open(map_path, encoding="utf-8") as fh:
        gmap = json.load(fh)
    img = read_png(png_path)
    tile, cols = gmap["tile_size"], gmap["columns"]
    cells = gmap["cells"]
    if gmap["count"] != len(rows) or len(cells) != len(rows):
        errors.append(
            "%s: %d cells for %d glyphs" % (map_path, len(cells), len(rows))
        )
    ncells = (img.width // tile) * (img.height // tile)
    for r in rows:
        g = int(r["glyph"])
        if g >= len(cells):
            errors.append("glyph %d (%s) has no cell" % (g, r["name"]))
            continue
        c = cells[g]
        if not 0 <= c < ncells:
            errors.append("glyph %d (%s) cell %d outside atlas" % (g, r["name"], c))
            continue
        if int(r["category"]) in (mt.UNEXPLORED, mt.NOTHING):
            continue
        x0, y0 = (c % cols) * tile, (c // cols) * tile
        visible = False
        for y in range(y0, y0 + tile, 2):
            o = (y * img.width + x0) * 4
            if any(img.data[o + 3 : o + tile * 4 : 4]):
                visible = True
                break
        if not visible:
            errors.append("glyph %d (%s) has an empty tile" % (g, r["name"]))
    return errors


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    rows = mt.read_glyphs(argv[1])
    sets = [("tiles.png", "glyph_map.json")]
    if os.path.exists(os.path.join(argv[2], "glyph_map_classic.json")):
        sets.append(("tiles_classic.png", "glyph_map_classic.json"))
    failed = False
    for png, gmap in sets:
        errs = check(rows, os.path.join(argv[2], png), os.path.join(argv[2], gmap))
        for e in errs[:50]:
            print("FAIL:", e)
        if errs:
            failed = True
            print("%s: %d problems" % (gmap, len(errs)))
        else:
            print("%s: all %d glyphs map to a tile" % (gmap, len(rows)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
