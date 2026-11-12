#!/usr/bin/env python3
"""Prepare a NetHack playground for the headless native test.

    stage_gamedir.py DATA_DIR GAME_DIR

Copies every file listed in DATA_DIR/manifest.txt (godot/nethack_data, built
by SCons) into GAME_DIR, creates GAME_DIR/save and the empty score files.
This mirrors GameSession.stage_data() in godot/scripts/core/game_session.gd.
"""

import os
import shutil
import sys

KEEP = ["record", "logfile", "xlogfile", "perm", "livelog"]


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    data_dir, game_dir = argv[1], argv[2]
    os.makedirs(os.path.join(game_dir, "save"), exist_ok=True)
    with open(os.path.join(data_dir, "manifest.txt"), encoding="utf-8") as fh:
        names = [ln.strip() for ln in fh if ln.strip() and not ln.startswith("#")]
    for name in names:
        shutil.copy2(os.path.join(data_dir, name), os.path.join(game_dir, name))
    for name in KEEP:
        path = os.path.join(game_dir, name)
        if not os.path.exists(path):
            open(path, "wb").close()
    print("staged %d data files into %s" % (len(names), game_dir))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
