#!/usr/bin/env python3
"""Check that every translation key used by the Godot project is translated.

    python3 tools/check_translations.py [GODOT_DIR]

Keys are UPPER_SNAKE string literals with a known prefix found in .gd and
.tscn files (tr("KEY"), Label.text = "KEY", ...), plus the keys built at run
time from tables (ROLE_*, RACE_*, COND_* ...), which must already be in the
.po files.  Every key must have a non-empty msgstr in every locale/*.po.
Exit status 1 lists the missing ones.
"""

import os
import re
import sys

PREFIXES = ("GAME_", "MENU_", "CHARGEN_", "UI_", "ERROR_", "OPTIONS_", "HUD_",
            "DIALOG_", "ROLE_", "RACE_", "GENDER_", "ALIGN_", "COND_")
LANGS = ["en", "zh_TW", "zh_CN", "ko", "ja"]
KEY_RE = re.compile(r'"((?:%s)[A-Z0-9_]+)"' % "|".join(PREFIXES))
PO_RE = re.compile(r'^msgid "(.*)"\s*\nmsgstr "(.*)"', re.M)


def used_keys(godot_dir):
    keys = {}
    for root, dirs, files in os.walk(godot_dir):
        dirs[:] = [d for d in dirs if not d.startswith(".") and d != "addons"]
        for name in files:
            if not name.endswith((".gd", ".tscn")):
                continue
            path = os.path.join(root, name)
            with open(path, encoding="utf-8") as f:
                for m in KEY_RE.finditer(f.read()):
                    # keys ending in "_" are prefixes for run-time lookups
                    if not m.group(1).endswith("_"):
                        keys.setdefault(m.group(1), os.path.relpath(path, godot_dir))
    return keys


def po_entries(path):
    with open(path, encoding="utf-8") as f:
        return {k: v for k, v in PO_RE.findall(f.read()) if k}


def check(godot_dir):
    keys = used_keys(godot_dir)
    problems = []
    for lang in LANGS:
        path = os.path.join(godot_dir, "locale", lang + ".po")
        if not os.path.exists(path):
            problems.append("missing %s" % path)
            continue
        entries = po_entries(path)
        for key in sorted(keys):
            if not entries.get(key):
                problems.append("%s: %s (used in %s)" % (lang, key, keys[key]))
        # run-time keys (ROLE_ etc.) must be translated wherever en has them
        en = po_entries(os.path.join(godot_dir, "locale", "en.po")) \
            if lang != "en" and os.path.exists(os.path.join(godot_dir, "locale", "en.po")) else {}
        for key in sorted(set(en) - set(keys)):
            if not entries.get(key):
                problems.append("%s: %s (in en.po)" % (lang, key))
    return keys, problems


def check_core_text(godot_dir):
    """The core-text table parses (every row has all four translations with
    matching placeholders) and its generated outputs are current."""
    here = os.path.dirname(os.path.abspath(__file__))
    sys.path.insert(0, here)
    import build_core_text

    table = os.path.join(here, "core_text_table.txt")
    try:
        rows, js, inc = build_core_text.outputs(table)
    except ValueError as e:
        return 0, ["core_text_table.txt: %s" % e]
    problems = []
    for path, want in ((os.path.join(godot_dir, "locale", "core_text.json"), js),
                       (os.path.join(here, "..", "native", "bridge", "core_text_table.inc"), inc)):
        try:
            with open(path, encoding="utf-8") as f:
                have = f.read()
        except OSError:
            have = None
        if have != want:
            problems.append("%s is out of date: run python3 tools/build_core_text.py"
                            % os.path.relpath(path, os.path.join(here, "..")))
    return len(rows), problems


def main(argv):
    here = os.path.dirname(os.path.abspath(__file__))
    godot_dir = argv[1] if len(argv) > 1 else os.path.join(here, "..", "godot")
    keys, problems = check(godot_dir)
    core_rows, core_problems = check_core_text(godot_dir)
    problems += core_problems
    print("%d core-text rows" % core_rows)
    for p in problems:
        print("MISSING " + p)
    print("%d keys used, %d languages, %d problems" % (len(keys), len(LANGS), len(problems)))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
