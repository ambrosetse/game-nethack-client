"""Tests for make_illustrated_tileset.py.

    python3 -m unittest discover -s tools -p "test_*.py"

The tileset must be deterministic (CI compares a fresh regeneration with the
committed files), at least 32px, cover every glyph through its glyph_map JSON,
and draw something in every non-blank cell.
"""

import json
import os
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
SCRIPT = os.path.join(TOOLS, "make_illustrated_tileset.py")
GLYPHS = os.path.join(ROOT, "build", "GLYPHS.tsv")
HEADER = "glyph\tsymidx\tcolor\tttychar\tcategory\tsymname\tname"
UNEXPLORED_OR_NOTHING = {"S_unexplored", "S_nothing"}

# A few rows of the dumped glyph table (renumbered densely from 0).
SAMPLE = [
    ("124", "3", "97", "0", "S_ANT", "giant ant"),
    ("127", "15", "100", "0", "S_DOG", "little dog"),
    ("127", "15", "100", "1", "S_DOG", "tame little dog"),
    ("124", "11", "97", "1", "S_ANT", "tame killer bee"),
]


def write_tsv(path, rows):
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(HEADER + "\n")
        for i, r in enumerate(rows):
            fh.write("\t".join((str(i),) + tuple(r)) + "\n")


def sample_of_real_table():
    """One row per (category, symname) of build/GLYPHS.tsv."""
    seen, rows = set(), []
    with open(GLYPHS, encoding="utf-8") as fh:
        fh.readline()
        for line in fh:
            vals = line.rstrip("\n").split("\t")
            if len(vals) < 7:
                continue
            key = (vals[4], vals[5])
            if key not in seen:
                seen.add(key)
                rows.append(tuple(vals[1:7]))
    return rows


def read_png_rgba(path):
    """Decode an 8-bit RGBA, non-interlaced PNG (what pngio writes)."""
    with open(path, "rb") as fh:
        data = fh.read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    pos, idat, w, h = 8, b"", 0, 0
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if kind == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", body[:10])
            assert (depth, ctype) == (8, 6), "expected 8-bit RGBA"
        elif kind == b"IDAT":
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride = w * 4
    out = bytearray(h * stride)
    prev = bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            a = line[x - 4] if x >= 4 else 0
            b = prev[x]
            c = prev[x - 4] if x >= 4 else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + b) & 255
            elif f == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 255
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return w, h, bytes(out)


class IllustratedTilesetTest(unittest.TestCase):
    def generate(self, rows, out):
        tsv = os.path.join(out, "GLYPHS.tsv")
        write_tsv(tsv, rows)
        subprocess.run([sys.executable, SCRIPT, tsv, out], check=True,
                       stdout=subprocess.DEVNULL)
        with open(os.path.join(out, "glyph_map_illustrated.json"), "rb") as fh:
            js = fh.read()
        with open(os.path.join(out, "tiles_illustrated.png"), "rb") as fh:
            png = fh.read()
        return js, png

    def check(self, rows):
        with tempfile.TemporaryDirectory() as a, tempfile.TemporaryDirectory() as b:
            js1, png1 = self.generate(rows, a)
            js2, png2 = self.generate(rows, b)
            self.assertEqual(js1, js2, "glyph map differs between runs")
            self.assertEqual(png1, png2, "atlas differs between runs")

            data = json.loads(js1)
            self.assertEqual(data["tileset"], "illustrated")
            tile = data["tile_size"]
            cols = data["columns"]
            self.assertGreaterEqual(tile, 32)
            self.assertEqual(data["count"], len(rows))
            cells = data["cells"]
            self.assertEqual(len(cells), len(rows), "every glyph needs a cell")

            w, h, px = read_png_rgba(os.path.join(a, "tiles_illustrated.png"))
            self.assertEqual(w, cols * tile)
            grid = (w // tile) * (h // tile)
            for g, cell in enumerate(cells):
                self.assertTrue(0 <= cell < grid, "glyph %d: cell %d outside" % (g, cell))
                if rows[g][4] in UNEXPLORED_OR_NOTHING:
                    continue
                x0, y0 = (cell % cols) * tile, (cell // cols) * tile
                opaque = sum(
                    1 for y in range(y0, y0 + tile) for x in range(x0, x0 + tile)
                    if px[(y * w + x) * 4 + 3])
                self.assertGreater(opaque, tile * tile // 8,
                                   "glyph %d (%s) is nearly empty" % (g, rows[g][5]))

    def test_sample(self):
        self.check(SAMPLE)

    @unittest.skipUnless(os.path.exists(GLYPHS), "build/GLYPHS.tsv not dumped")
    def test_every_symbol_of_the_real_table(self):
        self.check(sample_of_real_table())


if __name__ == "__main__":
    unittest.main()
