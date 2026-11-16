#!/usr/bin/env python3
"""Test: the committed tilesets match a fresh regeneration.

    check_tiles_current.py COMMITTED_DIR REGENERATED_DIR

The tilesets under godot/assets/tiles/ are committed.  CI regenerates them
from the core's glyph table into a scratch directory and runs this script,
which fails when any committed file is missing or differs from the
regenerated one.  PNGs are compared by decoded pixels (not bytes) so a
different zlib build cannot cause a false failure; glyph maps are compared
as parsed JSON.

Exit status 0 on success, 1 on any difference.
"""

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pngio import read_png  # noqa: E402

FILES = (
    "tiles.png",
    "glyph_map.json",
    "tiles_classic.png",
    "glyph_map_classic.json",
)


def same(committed, regenerated):
    if committed.endswith(".png"):
        a, b = read_png(committed), read_png(regenerated)
        return (a.width, a.height, bytes(a.data)) == (b.width, b.height, bytes(b.data))
    with open(committed, encoding="utf-8") as fa, open(regenerated, encoding="utf-8") as fb:
        return json.load(fa) == json.load(fb)


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    failed = False
    for name in FILES:
        committed = os.path.join(argv[1], name)
        regenerated = os.path.join(argv[2], name)
        if not os.path.exists(regenerated):
            print("FAIL: %s was not regenerated" % regenerated)
            failed = True
        elif not os.path.exists(committed):
            print("FAIL: %s is not committed" % committed)
            failed = True
        elif not same(committed, regenerated):
            print("FAIL: %s differs from the regenerated file; regenerate and commit it" % committed)
            failed = True
        else:
            print("%s: up to date" % name)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
