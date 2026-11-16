#!/usr/bin/env python3
"""Generate godot/locale/*.po and messages.pot from tools/locale_table.txt.

    python3 tools/build_locale.py [TABLE] [OUT_DIR]

The table is pipe separated: key|en|zh_TW|zh_CN|ko|ja.  Lines starting with
'#' and blank lines are ignored.  Keys are used as msgid (Godot looks up
tr("KEY")), so every language - English included - gets a .po file.
"""

import os
import sys

LANGS = ["en", "zh_TW", "zh_CN", "ko", "ja"]
HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_TABLE = os.path.join(HERE, "locale_table.txt")
DEFAULT_OUT = os.path.join(HERE, "..", "godot", "locale")


def read_table(path):
    rows = []
    seen = set()
    with open(path, encoding="utf-8") as f:
        for n, line in enumerate(f, 1):
            line = line.rstrip("\n")
            if not line.strip() or line.startswith("#"):
                continue
            cols = line.split("|")
            if len(cols) != len(LANGS) + 1:
                raise ValueError("%s:%d: expected %d columns, got %d"
                                 % (path, n, len(LANGS) + 1, len(cols)))
            key = cols[0].strip()
            if key in seen:
                raise ValueError("%s:%d: duplicate key %s" % (path, n, key))
            seen.add(key)
            rows.append((key, [c.strip() for c in cols[1:]]))
    return rows


def po_escape(s):
    return s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")


def header(lang):
    lines = [
        'msgid ""',
        'msgstr ""',
        '"Project-Id-Version: NetHack Godot\\n"',
        '"MIME-Version: 1.0\\n"',
        '"Content-Type: text/plain; charset=UTF-8\\n"',
        '"Content-Transfer-Encoding: 8bit\\n"',
    ]
    if lang:
        lines.append('"Language: %s\\n"' % lang)
    return "\n".join(lines)


def render(rows, lang_index):
    out = [header(LANGS[lang_index] if lang_index is not None else None)]
    for key, values in rows:
        out.append("")
        out.append('msgid "%s"' % po_escape(key))
        text = values[lang_index] if lang_index is not None else ""
        out.append('msgstr "%s"' % po_escape(text))
    return "\n".join(out) + "\n"


def build(table, out_dir):
    rows = read_table(table)
    os.makedirs(out_dir, exist_ok=True)
    written = []
    for i, lang in enumerate(LANGS):
        path = os.path.join(out_dir, lang + ".po")
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(render(rows, i))
        written.append(path)
    pot = os.path.join(out_dir, "messages.pot")
    with open(pot, "w", encoding="utf-8", newline="\n") as f:
        f.write(render(rows, None))
    written.append(pot)
    return rows, written


def main(argv):
    table = argv[1] if len(argv) > 1 else DEFAULT_TABLE
    out_dir = argv[2] if len(argv) > 2 else DEFAULT_OUT
    rows, written = build(table, out_dir)
    print("%d keys -> %s" % (len(rows), ", ".join(os.path.basename(p) for p in written)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
