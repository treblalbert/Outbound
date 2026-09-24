"""The game's own UI font (Sean Barrett's stb_easy_font, external/stb_easy_font.h),
ported to Python so trailer cards and captions are lettered exactly like the game.

    from trailer_font import text_image, text_width
    img = text_image("HOLD THE LINE", scale=8, color=(255, 247, 228))
"""
import os
import re

from PIL import Image

_HERE = os.path.dirname(os.path.abspath(__file__))
_HEADER = os.path.join(_HERE, "..", "external", "stb_easy_font.h")


def _numbers(block):
    return [int(n) for n in re.findall(r"-?\d+", block)]


def _load():
    src = open(_HEADER, encoding="utf-8", errors="replace").read()

    def table(name, end="};"):
        i = src.index(name)
        j = src.index(end, i)
        return src[i:j]

    info = _numbers(table("stb_easy_font_charinfo[96] = {"))[1:]   # drop the "96"
    info = [tuple(info[i:i + 3]) for i in range(0, len(info) - 2, 3)]
    hseg = _numbers(table("stb_easy_font_hseg[214] = {"))[1:]
    vseg = _numbers(table("stb_easy_font_vseg[253] = {"))[1:]
    return info, hseg, vseg


_INFO, _HSEG, _VSEG = _load()


def _segs(px, x, y, segs, vertical, color):
    """One character's horizontal or vertical strokes, as the game draws them."""
    for s in segs:
        ln = s & 7
        x += (s >> 3) & 1
        if not ln:
            continue
        y0 = y + (s >> 4)
        w, h = (1, ln) if vertical else (ln, 1)
        for yy in range(int(y0), int(y0 + h)):
            for xx in range(int(x), int(x + w)):
                px[xx, yy] = color
    return x


def text_width(s):
    w = 0
    for ch in s:
        c = ord(ch)
        if 32 <= c < 127:
            w += _INFO[c - 32][0] & 15
    return w


def _draw(px, s, ox, oy, color):
    x = ox
    for ch in s:
        c = ord(ch)
        if not (32 <= c < 127):
            continue
        i = c - 32
        advance, h0, v0 = _INFO[i]
        h1, v1 = _INFO[i + 1][1], _INFO[i + 1][2]
        _segs(px, x, oy, _HSEG[h0:h1], False, color)
        _segs(px, x, oy, _VSEG[v0:v1], True, color)
        x += advance & 15


def text_image(s, scale=8, color=(255, 247, 228), shadow=(20, 20, 24), pad=2):
    """A transparent image of `s`, with a one-pixel drop shadow like the game's."""
    w, h = text_width(s) + 2 + pad * 2, 12 + pad * 2
    base = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    px = base.load()
    if shadow:
        _draw(px, s, pad + 1, pad + 1, shadow + (255,))
    _draw(px, s, pad, pad, color + (255,))
    return base.resize((w * scale, h * scale), Image.NEAREST)
