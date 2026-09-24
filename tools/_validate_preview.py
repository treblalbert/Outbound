"""One-off headless checks for the reworked scene preview (deleted after use)."""
import importlib.util
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "tile_selector.py"

spec = importlib.util.spec_from_file_location("ts", TOOL)
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

data = m.load_mappings()
for palette in ("Beige", "Gray", "White", "Dark"):
    c = m.SceneComposer(data, lambda p=palette: p)
    scene = c.compose()
    print(f"{palette}: composed {scene.size}")
    if palette == "Beige":
        out = ROOT / "build" / "scene_preview_check.png"
        out.parent.mkdir(exist_ok=True)
        scene.save(out)
        print("  saved", out)

for group, x0, y0 in (("brick", 9, 2), ("fence", 9, 12)):
    cells = m.SceneComposer._perimeter_set(x0, y0, x0 + 6, y0 + 4)
    masks = set()
    for (x, y) in cells:
        mask = 0
        if (x - 1, y) in cells: mask |= 1
        if (x + 1, y) in cells: mask |= 2
        if (x, y - 1) in cells: mask |= 4
        if (x, y + 1) in cells: mask |= 8
        masks.add(mask)
    print(f"{group} perimeter masks: {sorted(masks)}")

c = m.SceneComposer(data, lambda: "Beige")
ground = c._ground_map(c.WIDTH, c.HEIGHT)
seams = set()
for y in range(len(ground)):
    for x in range(len(ground[0])):
        if ground[y][x] != "grass":
            continue
        mask = 0
        for bit, (dx, dy) in ((1, (-1, -1)), (2, (0, -1)), (4, (-1, 0)), (8, (0, 0))):
            if c._ground_kind(ground, x + dx, y + dy) == "dirt":
                mask |= bit
        if mask:
            seams.add(mask)
print("grass-dirt seams drawn:", sorted(seams), "missing:", sorted(set(range(1, 8)) - seams))

m.write_files(data)
print("header generation OK")
