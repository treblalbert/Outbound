"""Makes plain flat-roof concrete for the city buildings (0.12v), in
assets/sprites/TileOverrides/.

The facade sheets (Tiles/Buildings/Buildings_<colour>_TileSet.png) draw a flat roof
ring at columns 5..8, rows 0..2, but its only inside tile (frame 19) has a crack
drawn across it, and repeated over a whole roof that crack becomes a grid. These
fills take that tile's own concrete colour: one clean, two with a few worn specks in
the crack's shade. The game uses them for the inside of a roof and keeps the cracked
frame for now and then ("tileoverrides/flatroof_<colour>_<n>").

Run from the project folder:  python tools/make_flat_roof.py
"""
import random

from PIL import Image

COLOURS = ["beige", "gray", "white", "dark"]
SHEET = "assets/sprites/Tiles/Buildings/Buildings_%s_TileSet.png"
OUT = "assets/sprites/TileOverrides/flatroof_%s_%d.png"
COLS = 13

for colour in COLOURS:
    sheet = Image.open(SHEET % colour).convert("RGBA")
    c, r = 19 % COLS, 19 // COLS
    tile = sheet.crop((c * 16, r * 16, c * 16 + 16, r * 16 + 16))
    counts = {}
    for y in range(16):
        for x in range(16):
            p = tile.getpixel((x, y))
            counts[p] = counts.get(p, 0) + 1
    ranked = sorted(counts, key=lambda p: -counts[p])
    concrete, speck = ranked[0], ranked[1] if len(ranked) > 1 else ranked[0]
    rng = random.Random(colour)
    for n in range(3):
        t = Image.new("RGBA", (16, 16), concrete)
        for _ in range(0 if n == 0 else 3 + n):
            x, y = rng.randrange(1, 15), rng.randrange(1, 15)
            t.putpixel((x, y), speck)
            if rng.random() < 0.5:
                t.putpixel((x + 1, y), speck)
        t.save(OUT % (colour, n))
print("ok")
