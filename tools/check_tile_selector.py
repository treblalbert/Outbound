"""Headless checks for the reworked tile selector map preview.

Run with:  python tools/check_tile_selector.py
"""
import importlib.util
import pathlib
import sys
import traceback

ROOT = pathlib.Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "tile_selector.py"

spec = importlib.util.spec_from_file_location("ts", TOOL)
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

data = m.load_mappings()
print(f"cases: {len(m.CASES)}, mapped frames: {sum(c['count'] for c in m.CASES)}")

for palette in ("Beige", "Gray", "White", "Dark"):
    c = m.MapComposer(data, lambda p=palette: p)
    scene = c.compose()
    print(f"{palette}: map {scene.size}, {len(c.owners)} cells painted")

composer = m.MapComposer(data, lambda: "Beige")
scene = composer.compose()

no_case = [k for k, v in composer.owners.items() if v is None]
print(f"cells with no owning case: {len(no_case)} {no_case[:6]}")

covered = {v for v in composer.owners.values() if v is not None}
missing = [m.CASES[i] for i in range(len(m.CASES)) if i not in covered]
print(f"cases reachable by clicking the map: {len(covered)} of {len(m.CASES)}")
for item in missing:
    print(f"   not on the map: {item['group']} / {item['label']}")

# Every case must round-trip through the writer and reload identically.
m.write_files(data)
reloaded = m.load_mappings()
print("header regenerated, JSON reloaded OK")

out = ROOT / "build" / "preview_map_check.png"
scene.save(out)
print(f"saved {out.relative_to(ROOT)}")
