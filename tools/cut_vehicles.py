"""Cuts the cars Outbound uses out of minzinn's "Pixel Vehicles" pack
(assets/Vehicles, https://minzinn.itch.io/pixelvehicles) into assets/sprites/Vehicles/,
where the game's sprite loader finds them as "vehicles/<model>_<colour>" and
"vehicles/wreck/<kind>_<model>[_<colour>]".

The pack draws its cars at about twice the world's pixel size, so every frame is shrunk
on the way in (see SCALE) (a 2x2 block is opaque when most of it is, with the average colour of its
opaque pixels: the same rule the furniture uses). Each drivable car becomes one strip
of its 48 turning frames (frame 0 faces east, then clockwise in 7.5 degree steps),
trimmed to the smallest box that holds every frame and stays centred on the car.
Wrecks become strips of their 8 frames.

Run from the project folder:  python tools/cut_vehicles.py
"""
import glob
import os

import numpy as np
from PIL import Image

SRC = "assets/Vehicles"
OUT = "assets/sprites/Vehicles"

COLOURS = ["Black", "Blue", "Brown", "Green", "Magenta", "Red", "White", "Yellow"]

# Everyday cars only: no luxury, sports or show cars. (folder, key)
DRIVABLE = [
    ("MICRO TOPDOWN", "micro"),
    ("HATCHBACK TOPDOWN", "hatchback"),
    ("SEDAN TOPDOWN", "sedan"),
    ("WAGON TOPDOWN", "wagon"),
    ("PICKUP TOPDOWN", "pickup"),
    ("JEEP TOP DOWN", "jeep"),
    ("MINIVAN TOPDOWN", "minivan"),
    ("SUV TOPDOWN", "suv"),
    ("VAN TOP DOWN", "van"),
    ("BOX TRUCK TOPDOWN", "boxtruck"),
]

# Street clutter for the cities: burnt-out shells and overgrown abandoned cars.
WRECK_MODELS = [
    ("Sedan", "sedan"), ("HatchBack", "hatchback"), ("Van", "van"), ("Pickup", "pickup"),
    ("Minivan", "minivan"), ("Wagon", "wagon"), ("SUV", "suv"), ("Box Truck", "boxtruck"),
    ("Police", "police"), ("Taxi", "taxi"), ("Ambulance", "ambulance"), ("Civic", "civic"),
    ("Micro", "micro"), ("Jeep", "jeep"),
]
WRECK_COLOURS = ["White", "Blue", "Red", "Brown"]


# How much each model is shrunk (0.11v). The pack draws its models at different scales
# (the Micro, the Jeep and the Civic much smaller than the rest), so each gets its own
# factor to come out at a believable size next to a person (about 16 px tall): a
# small car about 28 px long, a family car 42-46, a truck 55.
SCALE = {"micro": 0.72, "hatchback": 0.5, "sedan": 0.52, "wagon": 0.52, "pickup": 0.54, "jeep": 0.66,
         "minivan": 0.54, "suv": 0.58, "van": 0.6, "boxtruck": 0.54, "civic": 0.62, "police": 0.52,
         "taxi": 0.52, "ambulance": 0.54}


def shrink(img, s):
    """Scales down by `s`: area-averaged colour of the opaque pixels, and a pixel is
    opaque when at least half of what it covers was."""
    if abs(s - 0.5) < 1e-6:
        return halve(img)
    a = np.asarray(img.convert("RGBA")).astype(np.float32) / 255.0
    w, h = max(1, int(round(img.width * s))), max(1, int(round(img.height * s)))
    alpha = (a[..., 3] >= 0.5).astype(np.float32)
    pre = a[..., :3] * alpha[..., None]
    def rs(ch):
        return np.asarray(Image.fromarray(ch.astype(np.float32), "F").resize((w, h), Image.BOX))
    al = rs(alpha)
    rgb = np.stack([rs(pre[..., c]) for c in range(3)], axis=-1)
    out = np.zeros((h, w, 4), np.uint8)
    keep = al >= 0.5
    out[keep, :3] = np.clip(rgb[keep] / al[keep][:, None] * 255.0, 0, 255).astype(np.uint8)
    out[keep, 3] = 255
    return Image.fromarray(out, "RGBA")


def halve(img):
    a = np.asarray(img.convert("RGBA")).astype(np.int32)
    h, w = a.shape[0] // 2 * 2, a.shape[1] // 2 * 2
    a = a[:h, :w]
    blocks = a.reshape(h // 2, 2, w // 2, 2, 4).transpose(0, 2, 1, 3, 4).reshape(h // 2, w // 2, 4, 4)
    opaque = blocks[..., 3] >= 128
    n = opaque.sum(axis=2)
    rgb = (blocks[..., :3] * opaque[..., None]).sum(axis=2)
    out = np.zeros((h // 2, w // 2, 4), np.uint8)
    keep = n >= 2
    out[keep, :3] = (rgb[keep] / n[keep][:, None]).astype(np.uint8)
    out[keep, 3] = 255
    return Image.fromarray(out, "RGBA")


def centred_box(frames):
    """The smallest box round every frame's opaque pixels that keeps the frame's own
    centre (where the car turns about) in its middle."""
    fw, fh = frames[0].size
    cx, cy = fw / 2, fh / 2
    rx = ry = 0
    for f in frames:
        b = f.getbbox()
        if not b:
            continue
        rx = max(rx, cx - b[0], b[2] - cx)
        ry = max(ry, cy - b[1], b[3] - cy)
    rx, ry = int(np.ceil(rx)) + 2, int(np.ceil(ry)) + 2
    rx += rx % 2
    ry += ry % 2
    return (int(cx - rx), int(cy - ry), int(cx + rx), int(cy + ry))


def strip(files, name, scale=0.5):
    frames = [Image.open(f).convert("RGBA") for f in files]
    box = centred_box(frames)
    # An even box keeps the car's middle on a whole pixel after scaling.
    cells = [shrink(f.crop(box), scale) for f in frames]
    w, h = cells[0].size
    sheet = Image.new("RGBA", (w * len(cells), h), (0, 0, 0, 0))
    for i, c in enumerate(cells):
        sheet.paste(c, (i * w, 0))
    path = os.path.join(OUT, name + "-Sheet%d.png" % len(cells))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    sheet.save(path)
    return w, h


def main():
    os.makedirs(OUT, exist_ok=True)
    for folder, key in DRIVABLE:
        for col in COLOURS:
            files = sorted(glob.glob(os.path.join(SRC, folder, col, "SEPARATED", "*_All_*.png")))
            if len(files) != 48:
                print("skip", folder, col, len(files))
                continue
            w, h = strip(files, "%s_%s" % (key, col.lower()), SCALE.get(key, 0.5))
        print(key, "frame", w, "x", h)
    for kind in ("Abandoned", "Burnt"):
        for folder, key in WRECK_MODELS:
            base = os.path.join(SRC, "Wreckage", kind, folder)
            if not os.path.isdir(base):
                # the pack spells a few folders differently between the two kinds
                alt = [d for d in os.listdir(os.path.join(SRC, "Wreckage", kind)) if d.lower() == folder.lower()]
                if not alt:
                    print("no", kind, folder)
                    continue
                base = os.path.join(SRC, "Wreckage", kind, alt[0])
            direct = sorted(f for f in glob.glob(os.path.join(base, "*.png")) if "sheet" not in f.lower())
            if direct:
                strip(direct, "Wreck/%s_%s" % (kind.lower(), key), SCALE.get(key, 0.5))
                continue
            for col in WRECK_COLOURS:
                files = sorted(f for f in glob.glob(os.path.join(base, col, "*.png")) if "sheet" not in f.lower())
                if len(files) == 8:
                    strip(files, "Wreck/%s_%s_%s" % (kind.lower(), key, col.lower()), SCALE.get(key, 0.5))


if __name__ == "__main__":
    main()
