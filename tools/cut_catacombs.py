"""Cuts the pieces Outbound uses out of Szadi Art's "Rogue Fantasy Catacombs" pack
(assets/Catacombs, https://szadiart.itch.io/rogue-fantasy-catacombs) into
assets/sprites/Catacombs/, where the game's sprite loader finds them as
"catacombs/<name>". Animated pieces become horizontal "-SheetN" strips.

Run from the project folder:  python tools/cut_catacombs.py
"""
import os
from PIL import Image

SRC = "assets/Catacombs"
OUT = "assets/sprites/Catacombs"
os.makedirs(OUT, exist_ok=True)

main = Image.open(os.path.join(SRC, "mainlevbuild.png")).convert("RGBA")
deco = Image.open(os.path.join(SRC, "decorative.png")).convert("RGBA")


def cut(img, x, y, w, h, name):
    img.crop((x, y, x + w, y + h)).save(os.path.join(OUT, name + ".png"))


def cut_clear_black(img, x, y, name):
    """A wall-top rim: the stone border stays, the black middle becomes see-through,
    so it can be laid over the plain black wall top."""
    t = img.crop((x, y, x + 16, y + 16))
    px = t.load()
    for j in range(16):
        for i in range(16):
            r, g, b, a = px[i, j]
            if r < 22 and g < 22 and b < 22:
                px[i, j] = (0, 0, 0, 0)
    t.save(os.path.join(OUT, name + ".png"))


def strip(files, w, h, name):
    """Frames of different sizes, bottom-centred in w x h cells, side by side."""
    frames = [Image.open(os.path.join(SRC, f)).convert("RGBA") for f in files]
    sheet = Image.new("RGBA", (w * len(frames), h), (0, 0, 0, 0))
    for i, f in enumerate(frames):
        sheet.alpha_composite(f, (i * w + (w - f.width) // 2, h - f.height))
    sheet.save(os.path.join(OUT, "%s-Sheet%d.png" % (name, len(frames))))


# ---- floor: plain slabs, brick paving and cracked stone (one colourway)
floors = [(736, 208), (752, 208), (736, 224), (752, 224), (736, 240), (752, 240),
          (736, 272), (752, 272), (736, 288), (752, 288),
          (736, 368), (752, 368), (736, 384), (752, 384)]
for i, (x, y) in enumerate(floors):
    cut(main, x, y, 16, 16, "floor_%d" % i)

# ---- walls: brick face (a wall seen from the front), the black top of a wall and
# the stone rims around it
for i, (x, y) in enumerate([(96, 144), (112, 144), (128, 144), (96, 160), (112, 160), (128, 160)]):
    cut(main, x, y, 16, 16, "wall_face_%d" % i)
cut(main, 112, 128, 16, 16, "wall_face_top")          # the face's upper row, under the lip
cut(main, 160, 80, 16, 16, "wall_cap")
cut_clear_black(main, 160, 48, "rim_n")
cut_clear_black(main, 64, 80, "rim_w")
cut_clear_black(main, 240, 80, "rim_e")
cut_clear_black(main, 112, 112, "rim_s")

# ---- the stair arch: the way down (outside) and back up (inside)
cut(main, 880, 16, 64, 88, "stairs")

# ---- animated lights and traps
strip(["torch_1.png", "torch_2.png", "torch_3.png", "torch_4.png"], 16, 16, "torch")
strip(["candleA_01.png", "candleA_02.png", "candleA_03.png", "candleA_04.png"], 8, 16, "candle_a")
strip(["candleB_01.png", "candleB_02.png", "candleB_03.png", "candleB_04.png"], 16, 16, "candle_b")
strip(["spike_0.png", "spike_1.png", "spike_2.png", "spike_3.png", "spike_4.png"], 16, 16, "spikes")

# ---- props (decorative.png)
for i, x in enumerate([0, 16, 32, 48, 64]):
    cut(deco, x, 16, 16, 48, "pillar_%d" % i)
cut(deco, 48, 64, 32, 48, "coffin_0")
cut(deco, 80, 64, 32, 48, "coffin_1")
for i, x in enumerate([128, 144, 168, 200]):
    cut(deco, x, 68, 16, 28, "chest_%d" % i)
    cut(deco, x, 97, 16, 17, "chest_open_%d" % i)
for i, (x, w) in enumerate([(144, 16), (160, 16), (176, 16)]):
    cut(deco, x, 124, w, 20, "urn_%d" % i)
    cut(deco, x, 144, w, 16, "urn_broken_%d" % i)
    cut(deco, x, 172, w, 20, "urn_%d" % (i + 3))
    cut(deco, x, 192, w, 16, "urn_broken_%d" % (i + 3))
for i, x in enumerate([160, 176, 192, 208]):
    cut(deco, x, 16, 16, 16, "candles_%d" % i)

print("cut into", OUT, ":", len(os.listdir(OUT)), "files")

# ---- 0.11v: the stair arch standing outside, without the sheet's black behind it
# (the black is flood-filled away from the edges, so the dark stairwell inside the
# arch stays), and the portcullis sealing the last room's way back (its frame and
# its bars apart, so the bars can be raised).
def clear_outer_black(img):
    px = img.load()
    w, h = img.size
    todo = [(x, 0) for x in range(w)] + [(0, y) for y in range(h)] + [(w - 1, y) for y in range(h)]
    seen = set()
    while todo:
        x, y = todo.pop()
        if (x, y) in seen or not (0 <= x < w and 0 <= y < h):
            continue
        seen.add((x, y))
        r, g, b, a = px[x, y]
        if a == 0 or (r < 16 and g < 16 and b < 16):
            px[x, y] = (0, 0, 0, 0)
            todo += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
    return img

clear_outer_black(main.crop((880, 0, 944, 105))).save(os.path.join(OUT, "stairs_out.png"))
gate = main.crop((512, 32, 576, 93))              # 64 x 61: lintel, posts, bars, bottom rail
frame = gate.copy()
fp = frame.load()
for y in range(12, 61):
    for x in range(9, 55):
        fp[x, y] = (0, 0, 0, 0)
frame.save(os.path.join(OUT, "gate_frame.png"))
# The pack's window has two bars: a portcullis wants a row of them, and a cross rail.
src = gate.crop((9, 12, 55, 61))
bars = Image.new("RGBA", src.size, (0, 0, 0, 0))
bar = src.crop((13, 0, 17, 45))
rail = src.crop((0, 45, 46, 49))
for x in range(1, 46 - 3, 9):
    bars.alpha_composite(bar, (x, 0))
bars.alpha_composite(rail, (0, 45))
bars.alpha_composite(rail.crop((0, 0, 46, 3)), (0, 18))
bars.save(os.path.join(OUT, "gate_bars.png"))
print("0.11v pieces cut")
