"""Plain dirt for the overworld, made from the pack's own grass/dirt edge pieces.

Background_Green_TileSet.png draws a full ring of grass-to-dirt edges (frames
294..393) but leaves its centre slot (319) empty, and the sheet's other plain dirt
(frame 39) is a darker, redder earth, so every edge showed a seam. These fills are
stitched from the all-dirt quarters of the ring's inner corners, so they match the
edges exactly. Written to assets/sprites/TileOverrides/ (found as
"tileoverrides/dirt_fill_<n>").

Run from the project folder:  python tools/make_ground_fills.py
"""
from PIL import Image

SHEET = "assets/sprites/Tiles/Background_Green_TileSet.png"
OUT = "assets/sprites/TileOverrides/dirt_fill_%d.png"
COLS = 24

sheet = Image.open(SHEET).convert("RGBA")


def quarter(frame, qx, qy):
    c, r = frame % COLS, frame // COLS
    return sheet.crop((c * 16 + qx * 8, r * 16 + qy * 8, c * 16 + qx * 8 + 8, r * 16 + qy * 8 + 8))


# frame -> which of its quarters are pure dirt
#   368: grass bottom-right   369: grass bottom-left
#   392: grass top-right      393: grass top-left
fills = [
    {(0, 0): 368, (1, 0): 368, (0, 1): 368, (1, 1): 369},
    {(0, 0): 392, (1, 0): 393, (0, 1): 393, (1, 1): 393},
    {(0, 0): 369, (1, 0): 369, (0, 1): 392, (1, 1): 369},
]
for n, parts in enumerate(fills):
    t = Image.new("RGBA", (16, 16))
    for (qx, qy), frame in parts.items():
        t.paste(quarter(frame, qx, qy), (qx * 8, qy * 8))
    # The corners' grass outline leaves a few dark blue-grey pixels near the middle:
    # swap each for the nearest real dirt pixel, so the fill is only earth.
    px = t.load()
    def dirt(c):
        r, g, b, a = c
        return a > 0 and r > b + 6 and r > g + 6
    for y in range(16):
        for x in range(16):
            if dirt(px[x, y]):
                continue
            for rad in range(1, 16):
                cand = [(x + dx, y + dy) for dy in range(-rad, rad + 1) for dx in range(-rad, rad + 1)
                        if 0 <= x + dx < 16 and 0 <= y + dy < 16 and max(abs(dx), abs(dy)) == rad and dirt(px[x + dx, y + dy])]
                if cand:
                    px[x, y] = px[cand[(x * 7 + y * 3) % len(cand)]]
                    break
    t.save(OUT % n)
print("wrote", len(fills), "dirt fills")
