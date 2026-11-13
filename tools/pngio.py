"""Minimal pure-Python PNG reader/writer (RGBA8) used by the tile tools.

Only the standard library is used so the tools run anywhere Python 3 runs,
including the CI runners, without installing Pillow.
"""

import struct
import zlib


class Image:
    """An RGBA8 image stored as one bytearray, row-major."""

    def __init__(self, width, height, fill=(0, 0, 0, 0)):
        self.width = width
        self.height = height
        self.data = bytearray(bytes(fill) * (width * height))

    def fill_rect(self, x, y, w, h, rgba):
        x0, y0 = max(0, x), max(0, y)
        x1, y1 = min(self.width, x + w), min(self.height, y + h)
        if x1 <= x0 or y1 <= y0:
            return
        row = bytes(rgba) * (x1 - x0)
        for yy in range(y0, y1):
            o = (yy * self.width + x0) * 4
            self.data[o : o + len(row)] = row

    def set(self, x, y, rgba):
        if 0 <= x < self.width and 0 <= y < self.height:
            o = (y * self.width + x) * 4
            self.data[o : o + 4] = bytes(rgba)

    def get(self, x, y):
        o = (y * self.width + x) * 4
        return tuple(self.data[o : o + 4])

    def blit(self, src, dx, dy):
        for yy in range(src.height):
            if not 0 <= dy + yy < self.height:
                continue
            so = yy * src.width * 4
            do = ((dy + yy) * self.width + dx) * 4
            self.data[do : do + src.width * 4] = src.data[so : so + src.width * 4]


def _chunk(kind, payload):
    out = struct.pack(">I", len(payload)) + kind + payload
    return out + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)


def write_png(path, img):
    raw = bytearray()
    stride = img.width * 4
    for y in range(img.height):
        raw.append(0)
        raw += img.data[y * stride : (y + 1) * stride]
    with open(path, "wb") as fh:
        fh.write(b"\x89PNG\r\n\x1a\n")
        fh.write(
            _chunk(b"IHDR", struct.pack(">IIBBBBB", img.width, img.height, 8, 6, 0, 0, 0))
        )
        fh.write(_chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        fh.write(_chunk(b"IEND", b""))


def read_png(path):
    """Read an 8-bit RGB or RGBA, non-interlaced PNG (what write_png emits)."""
    with open(path, "rb") as fh:
        blob = fh.read()
    if blob[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("%s: not a PNG" % path)
    pos, idat, w = 8, bytearray(), 0
    while pos < len(blob):
        (n,) = struct.unpack(">I", blob[pos : pos + 4])
        kind = blob[pos + 4 : pos + 8]
        body = blob[pos + 8 : pos + 8 + n]
        pos += 12 + n
        if kind == b"IHDR":
            w, h, depth, ctype, _, _, inter = struct.unpack(">IIBBBBB", body)
            if depth != 8 or ctype not in (2, 6) or inter:
                raise ValueError("%s: unsupported PNG format" % path)
        elif kind == b"IDAT":
            idat += body
    bpp = 4 if ctype == 6 else 3
    raw = zlib.decompress(bytes(idat))
    img = Image(w, h)
    stride = w * bpp
    prev = bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1 : (y + 1) * (stride + 1)])
        for i in range(stride if f else 0):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        if bpp == 4:
            img.data[y * w * 4 : (y + 1) * w * 4] = line
        else:
            for x in range(w):
                img.set(x, y, tuple(line[x * 3 : x * 3 + 3]) + (255,))
        prev = line
    return img
