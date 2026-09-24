"""Makes clean city paving tiles (0.11v) for assets/sprites/TileOverrides/.

The green background sheet only has its pavers round a manhole: frame 8 carries a
corner of the manhole's stone frame at its bottom right, frame 10 at its bottom left.
Each gets that corner patched from the other, which is clean there, giving two plain
paver tiles ("tileoverrides/pavement_0", "_1") that tile with each other.

Run from the project folder:  python tools/make_pavement.py
"""
from PIL import Image

SHEET = "assets/sprites/Tiles/Background_Green_TileSet.png"
OUT = "assets/sprites/TileOverrides/pavement_%d.png"

im = Image.open(SHEET).convert("RGBA")


def frame(f):
    x, y = (f % 24) * 16, (f // 24) * 16
    return im.crop((x, y, x + 16, y + 16))


a, b = frame(8), frame(10)
clean0 = a.copy()
clean0.paste(b.crop((10, 11, 16, 16)), (10, 11))
clean1 = b.copy()
clean1.paste(a.crop((0, 11, 6, 16)), (0, 11))
clean0.save(OUT % 0)
clean1.save(OUT % 1)
print("ok")
