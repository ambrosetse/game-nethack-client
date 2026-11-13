#!/usr/bin/env python3
"""Download the bundled CJK UI font (Noto Sans CJK, SIL OFL 1.1).

    python3 tools/fetch_fonts.py [OUT_DIR]

The font is ~16 MB, so it is fetched at build time (CI runs this before the
Godot export) instead of being stored in git.  The download is pinned to the
noto-cjk release tag below and verified against the git blob SHA-1 of that
tag, so a changed or truncated file is rejected.  One full "SC" OTF carries
every Han, Hangul and Kana glyph, which covers en, zh_TW, zh_CN, ko and ja.
Licence: see assets/LICENSES.md (the OFL text is fetched as fonts/LICENSE).
"""

import hashlib
import os
import sys
import urllib.request

TAG = "Sans2.004"
FILES = [
    # (path in notofonts/noto-cjk, git blob sha1 at TAG)
    (
        "Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf",
        "dc15562470b4f842321894787a0d066879ccff8b",
    ),
    # SIL Open Font License text shipped next to the font
    ("LICENSE", "d952d62c065f3f35fb83a173496e90b21525aef3"),
]
URL = "https://raw.githubusercontent.com/notofonts/noto-cjk/%s/%s"
HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_OUT = os.path.join(HERE, "..", "godot", "assets", "fonts")


def blob_sha1(data):
    h = hashlib.sha1()
    h.update(b"blob %d\0" % len(data))
    h.update(data)
    return h.hexdigest()


def fetch(path, sha, out_dir):
    dest = os.path.join(out_dir, os.path.basename(path))
    if os.path.exists(dest):
        with open(dest, "rb") as fh:
            if blob_sha1(fh.read()) == sha:
                print("ok      %s" % dest)
                return True
    print("fetch   %s" % (URL % (TAG, path)))
    with urllib.request.urlopen(URL % (TAG, path), timeout=300) as resp:
        data = resp.read()
    got = blob_sha1(data)
    if got != sha:
        print("ERROR: %s has blob sha %s, expected %s" % (path, got, sha))
        return False
    os.makedirs(out_dir, exist_ok=True)
    with open(dest, "wb") as fh:
        fh.write(data)
    print("wrote   %s (%d bytes)" % (dest, len(data)))
    return True


def main(argv):
    out_dir = argv[1] if len(argv) > 1 else DEFAULT_OUT
    ok = all(fetch(p, s, out_dir) for p, s in FILES)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
