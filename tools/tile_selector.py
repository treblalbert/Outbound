"""Visual editor for every tileset frame mapping consumed by the world renderer.

The left pane lists every case the renderer can ask for, the middle pane is the
real tileset with each frame numbered, and the right pane is a preview map built
by the same rules the game uses.  Clicking a tile in the preview map selects the
case that owns it, so a tile that looks wrong in the world can be traced back to
its mapping in one click.
"""

from __future__ import annotations

import argparse
import copy
import json
import shutil
import subprocess
import threading
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

from PIL import Image, ImageDraw, ImageTk


ROOT = Path(__file__).resolve().parents[1]
JSON_PATH = ROOT / "assets" / "tile_mappings.json"
HEADER_PATH = ROOT / "src" / "tile_mappings.generated.h"
TILE_SIZE = 16
OVERRIDE_DIR = ROOT / "assets" / "sprites" / "TileOverrides"
PREVIEW_TILE = 16  # source pixels per tile in the composed scene

BUILDING_SHEETS = {
    "Beige": "assets/sprites/Tiles/Buildings/Buildings_beige_TileSet.png",
    "Gray": "assets/sprites/Tiles/Buildings/Buildings_gray_TileSet.png",
    "White": "assets/sprites/Tiles/Buildings/Buildings_white_TileSet.png",
    "Dark": "assets/sprites/Tiles/Buildings/Buildings_dark_TileSet.png",
}

# Every background sheet shares the pack's 24x17 frame layout, so a frame index
# means the same thing in all of them. The game only ever reads the lush
# (Green) and dry (Dark-Green) sheets, but the other two are selectable here so
# a whole biome can be swapped in without leaving the editor.
BG_SHEET = "assets/sprites/Tiles/Background_Green_TileSet.png"
BG_DRY_SHEET = "assets/sprites/Tiles/Background_Dark-Green_TileSet.png"
BACKGROUND_COLS = 24

DEFAULTS = {
    "version": 2,
    "building": {
        "facade": [5, 6, 8, 18, 21, 31, 32, 34],
        "connected": [14, 48, 43, 44, 66, 74, 69, 44, 1, 8, 4, 44, 14, 14, 14, 44],
        "floor": [59, 59, 59, 59],
    },
    "fence": {"connected": [4, 5, 4, 14, 6, 9, 10, 14, 6, 9, 10, 6, 5, 5, 5, 4]},
    "brick": {"connected": [6, 6, 6, 16, 6, 17, 15, 0, 5, 5, 0, 6, 6, 6, 6, 6]},
    "roof": [
        [[0, 1, 2], [16, 17, 18], [32, 33, 34], [48, 49, 50], [64, 65, 66]],
        [[8, 9, 10], [24, 25, 26], [40, 41, 42], [56, 57, 58], [72, 73, 74]],
    ],
    "background": {
        "grass": [5, 5, 5, 5, 5],
        "grass_detail": [3, 51, 52],
        "dirt": 39,
        "road": [221, 221],
        "grass_dirt": [5, 344, 342, 343, 296, 320, 368, 368, 294, 369, 318, 369, 295, 392, 393, -1],
    },
    # Sheets the renderer falls back to when a hand-picked frame is refused. They
    # are offered here so an atlas that reclassifies tiles can be inspected,
    # but they live outside the C++ header and the JSON keeps them optional.
    "extras": {
        "interior": [48, 50, 62, 64, 74, 76],
    },
    "sheet_overrides": {},
}

MASK_NAMES = []
for mask in range(16):
    sides = [name for bit, name in ((1, "left"), (2, "right"), (4, "up"), (8, "down")) if mask & bit]
    MASK_NAMES.append(" + ".join(sides) if sides else "isolated")


# Corner masks for the grass<->earth seam. Bit 1 = top-left, 2 = top-right,
# 4 = bottom-left, 8 = bottom-right, and a set bit means that corner is earth.
# This is the renderer's own bit order (see groundTile in src/art.cpp).
CORNER_NAMES = []
for mask in range(16):
    corners = [name for bit, name in ((1, "NW"), (2, "NE"), (4, "SW"), (8, "SE")) if mask & bit]
    CORNER_NAMES.append(" + ".join(corners) if corners else "no earth")

# Ground ids, kept in step with the Ground enum in src/world.h.
G_GRASS, G_DIRT, G_SAND, G_WATER, G_ROAD, G_BRIDGE = range(6)
G_FLOOR_WOOD, G_FLOOR_CONCRETE, G_FLOOR_TILE, G_RUBBLE, G_BASE_FLOOR = range(6, 11)

# Which surface wins when four tiles meet at one corner, exactly as
# groundPriority() orders them. Man-made beats earth beats grass.
GROUND_PRIORITY = {G_GRASS: 0, G_WATER: 1, G_SAND: 2, G_DIRT: 3, G_RUBBLE: 4,
                   G_ROAD: 5, G_BRIDGE: 5, G_FLOOR_WOOD: 6, G_FLOOR_CONCRETE: 6,
                   G_FLOOR_TILE: 6, G_BASE_FLOOR: 6}


def get_value(data, path):
    value = data
    for part in path:
        value = value[part]
    return value


def set_value(data, path, value):
    target = data
    for part in path[:-1]:
        target = target[part]
    target[path[-1]] = value


def ensure_path(data, path, default):
    """Create the container chain for an optional case, returning the value slot."""
    target = data
    for part in path[:-1]:
        target = target.setdefault(part, {})
    if path[-1] not in target:
        target[path[-1]] = copy.deepcopy(default)
    return target[path[-1]]


def cases():
    result = []

    def add(group, label, path, sheet, sheet_group, indices=None, count=1,
            clearable=False, palette=False, optional=False, ground=None, solid=None,
            mask_kind=None, mask=None):
        result.append({"group": group, "label": label, "path": path, "sheet": sheet,
                       "sheet_group": sheet_group, "indices": list(indices or range(count)),
                       "count": count, "clearable": clearable, "palette": palette,
                       "optional": optional, "ground": ground, "solid": solid,
                       "mask_kind": mask_kind, "mask": mask})

    beige = BUILDING_SHEETS["Beige"]
    bg = BG_SHEET

    # ---- terrain fills: every slot the renderer reads ----------------------
    for index in range(5):
        add("Ground / grass", f"grass variation {index + 1}",
            ("background", "grass", index), BG_SHEET, "background")
    for index in range(3):
        add("Ground / grass detail", f"detail variation {index + 1}",
            ("background", "grass_detail", index), BG_SHEET, "background")
    add("Ground / dirt", "dirt", ("background", "dirt"), BG_SHEET, "background")
    for index, label in enumerate(("horizontal road", "vertical road")):
        add("Ground / road", label, ("background", "road", index), BG_SHEET, "background")
    for index in range(16):
        corners = CORNER_NAMES[index]
        add("Ground / grass-dirt transition", f"mask {index:02d}: {corners}",
            ("background", "grass_dirt", index), BG_SHEET, "background",
            clearable=True)

    # ---- building shell ----------------------------------------------------
    facade_names = ("top-left corner", "top edge", "top-right corner", "left edge",
                    "right edge", "bottom-left corner", "bottom edge", "bottom-right corner")
    for index, label in enumerate(facade_names):
        add("Buildings / facade", label, ("building", "facade", index), beige,
            "building", count=1, palette=True, solid="wood")
    for index, label in enumerate(MASK_NAMES):
        add("Buildings / connected wall", f"mask {index:02d}: {label}",
            ("building", "connected", index), beige, "building", count=1,
            palette=True, solid="wood", mask_kind="neighbour", mask=index)
    for index, label in enumerate(("wood floor", "concrete floor", "tile floor", "base floor")):
        add("Buildings / floor", label, ("building", "floor", index), beige,
            "building", count=1, palette=True)

    # ---- walls -------------------------------------------------------------
    for index, label in enumerate(MASK_NAMES):
        add("Brick wall", f"mask {index:02d}: {label}",
            ("brick", "connected", index), "assets/sprites/Tiles/Brick-Wall_TileSet.png", "brick", count=1,
            solid="brick", mask_kind="neighbour", mask=index)
    for index, label in enumerate(MASK_NAMES):
        add("Wire fence", f"mask {index:02d}: {label}",
            ("fence", "connected", index), "assets/sprites/Tiles/Wire-Fence/Wire-Fence_TileSet.png", "fence", count=1,
            solid="fence", mask_kind="neighbour", mask=index)

    # ---- roof: every style/row/col slot the renderer reads -----------------
    row_names = ("top", "upper middle", "middle", "lower middle", "bottom")
    col_names = ("left", "middle", "right")
    for style in range(2):
        for row, row_name in enumerate(row_names):
            for col, col_name in enumerate(col_names):
                add(f"Roof / style {style + 1}", f"{row_name} / {col_name}",
                    ("roof", style, row, col), "assets/sprites/Tiles/Roof_TileSet.png", "roof", count=1,
                    mask_kind="roof", mask=(style, row, col))
    return result
CASES = cases()

# Bits used for connected-wall / transition masks, matching MASK_NAMES above.
LEFT, RIGHT, UP, DOWN = 1, 2, 4, 8

# Default sheet per preview group, mirroring the cases() definitions.
CASE_SHEETS = {
    "building": BUILDING_SHEETS["Beige"],
    "fence": "assets/sprites/Tiles/Wire-Fence/Wire-Fence_TileSet.png",
    "brick": "assets/sprites/Tiles/Brick-Wall_TileSet.png",
    "roof": "assets/sprites/Tiles/Roof_TileSet.png",
    "background": BG_SHEET,
}


class MapComposer:
    """Preview map built with the renderer's own rules (src/art.cpp, src/game.cpp).

    Every tile painted records its owning case in ``owners``, so clicking the
    map selects that case. Rows 0..GALLERY_Y-1 are a world slice (grass, earth,
    road, houses, brick/fence runs); rows GALLERY_Y..HEIGHT-1 are a gallery of
    standalone swatches for the few cases no natural layout can show.
    """

    WIDTH = 56   # tiles
    HEIGHT = 40  # tiles
    GALLERY_Y = 33

    def __init__(self, data, palette_getter):
        self.data = data
        self.palette_getter = palette_getter
        self._cache = {}
        self.owners = {}          # (x, y) -> case index (the last thing painted there)
        self.roof_alpha = {}      # (x, y) -> alpha for roof tiles
        self.annotations = []     # (x, y, text) markers drawn over the map

    def invalidate(self):
        self._cache.clear()

    # -- sheets -------------------------------------------------------------
    def _sheet_path(self, group):
        override = self.data.get("sheet_overrides", {}).get(group, "")
        if group == "background_dry":
            if override:
                return ROOT / override
            return ROOT / BG_DRY_SHEET
        if override:
            return ROOT / override
        if group == "building":
            return ROOT / BUILDING_SHEETS[self.palette_getter()]
        return ROOT / CASE_SHEETS.get(group, CASE_SHEETS["background"])

    def _sheet(self, group):
        image = self._cache.get(group)
        if image is None:
            try:
                image = Image.open(self._sheet_path(group)).convert("RGBA")
            except (FileNotFoundError, OSError):
                image = None
            self._cache[group] = image
        return image

    def _tile(self, group, frame):
        """Return one 16x16 tile of a sheet; magenta when unmapped or missing."""
        image = self._sheet(group)
        if image is None or frame is None or frame < 0:
            return Image.new("RGBA", (TILE_SIZE, TILE_SIZE), "#3a003a")
        cols = max(1, image.width // TILE_SIZE)
        if frame >= cols * (image.height // TILE_SIZE):
            return Image.new("RGBA", (TILE_SIZE, TILE_SIZE), "#3a003a")
        col, row = frame % cols, frame // cols
        return image.crop((col * TILE_SIZE, row * TILE_SIZE,
                           (col + 1) * TILE_SIZE, (row + 1) * TILE_SIZE))

    # -- mapping lookups ----------------------------------------------------
    def _g(self, section, key, default=-1):
        return self.data.get(section, {}).get(key, default)

    def _frame(self, path, default=-1):
        node = self.data
        for part in path:
            if not isinstance(node, dict) or part not in node:
                return default
            node = node[part]
        if isinstance(node, (int, float)):
            return int(node)
        return default

    def _list(self, path, default=()):
        node = self.data
        for part in path:
            if not isinstance(node, dict) or part not in node:
                return list(default)
            node = node[part]
        return list(node) if isinstance(node, list) else list(default)

    def _case_index(self, predicate):
        for number, item in enumerate(CASES):
            if predicate(item):
                return number
        return None

    def _case_for(self, key, value):
        """The tree entry whose path continues past ``key`` with ``value``."""
        key = tuple(key)
        for number, item in enumerate(CASES):
            path = item["path"]
            if len(path) > len(key) and path[:len(key)] == key and path[len(key)] == value:
                return number
        return None

    def _case_for_list(self, key, index):
        """The tree entry for element ``index`` of the list stored at ``key``.

        Covers both shapes the table uses: a case whose whole path *is* the key
        (ground fills and seams keep their frames in one list) and a case whose
        path continues one step past it (building floors and roof tiles are one
        entry per frame).
        """
        key = tuple(key)
        for number, item in enumerate(CASES):
            path = item["path"]
            if path == key and index < len(item["indices"]):
                return number
            if (len(path) == len(key) + 1 and path[:len(key)] == key
                    and path[len(key)] == index):
                return number
        return None


    # -- the map ------------------------------------------------------------
    def compose(self):
        w, h = self.WIDTH, self.HEIGHT
        self.owners = {}
        self.wall_cells = []
        self.ground = self._ground_map(w, h)
        self.probes = []          # (x, y, case_index) revealed-but-hidden cells
        self.solid = [[None] * w for _ in range(h)]
        self.buildings = self._buildings()
        scene = Image.new("RGBA", (w * TILE_SIZE, h * TILE_SIZE), "#101010")
        self._paint_ground(scene)
        self._paint_walls(scene)
        self._paint_roofs(scene)
        self._paint_probes(scene)
        self._paint_gallery(scene)
        return scene

    def case_at(self, tx, ty):
        return self.owners.get((tx, ty))

    def _ground_map(self, w, h):
        """Grass and earth, shaped so every seam corner mask actually occurs.

        Dirt wins the four tiles sharing a corner exactly as the renderer's
        groundPriority does, so masks 0 (no earth) and 15 (all earth) never
        occur and the seam art only has to cover 1..14.
        """
        ground = [[G_GRASS] * w for _ in range(h)]

        def fill(x0, y0, x1, y1, g=G_DIRT):
            for y in range(y0, y1 + 1):
                for x in range(x0, x1 + 1):
                    ground[y][x] = g

        # A road across the map with a one-tile dirt shoulder either side, so
        # asphalt never meets grass directly -- the rule world generation uses.
        for x in range(0, w):
            ground[13][x] = G_ROAD
            ground[14][x] = G_ROAD
        fill(0, 12, w - 1, 12)
        fill(0, 15, w - 1, 15)

        # Broad patches, islands, stems and notches: between them these reach
        # every corner mask a grass tile can meaningfully see (1..7).
        fill(0, 0, 10, 5)
        fill(24, 0, 28, 4)
        fill(3, 20, 20, 27)
        fill(33, 19, 47, 22)
        fill(30, 30, 45, 36)
        fill(41, 8, w - 1, 11)
        fill(15, 8, 15, 11)          # a stem: the north/south edge masks
        fill(20, 30, 20, 33)
        ground[30][20] = G_GRASS     # hole in the stem: the diagonal mask
        fill(24, 8, 27, 8)
        ground[8][25] = G_GRASS      # a notch: the all-but-one mask

        # Water with a bridge over it, so both have their own frames.
        fill(45, 24, 51, 29, G_WATER)
        for x in range(46, 51):
            ground[26][x] = G_BRIDGE
        return ground


    def _buildings(self):
        """(x0, y0, w, h, roof style) for every preview house."""
        return [
            (2, 2, 9, 8, 0),
            (15, 5, 9, 9, 1),
            (28, 3, 10, 8, 0),
            (42, 6, 9, 9, 1),
            (2, 16, 10, 8, 1),
            (30, 17, 12, 8, 0),
        ]

    def _in_building(self, x, y):
        for (bx, by, bw, bh, _s) in self.buildings:
            if bx <= x < bx + bw and by <= y < by + bh:
                return (bx, by, bw, bh)
        return None

    def _wall_kind(self, x, y):
        if not (0 <= x < self.WIDTH and 0 <= y < self.HEIGHT):
            return None
        return self.solid[y][x]

    def _in_gallery(self, x, y):
        return y >= self.GALLERY_Y

    def _paint_ground(self, scene):
        grass = self._list(("background", "grass"), (5,))
        detail = self._list(("background", "grass_detail"), ())
        transitions = self._list(("background", "grass_dirt"), tuple([-1] * 16))
        road = self._list(("background", "road"), (172, 197))

        grass_case = self._case_for_list(("background", "grass"), 0)
        detail_case = self._case_for_list(("background", "grass_detail"), 0)
        dirt_case = self._case_for_list(("background", "dirt"), 0)
        road_case = self._case_for_list(("background", "road"), 0)
        seam_case = self._case_for_list(("background", "grass_dirt"), 0)
        bridge_case = self._case_for_list(("extras", "bridge"), 0)
        water_case = self._case_for_list(("extras", "water"), 0)

        for y in range(self.GALLERY_Y):
            for x in range(self.WIDTH):
                kind = self.ground[y][x]
                # A building footprint paints its own floor, so the terrain
                # beneath it is not drawn at all.
                if self._in_building(x, y) and kind not in (G_ROAD, G_WATER, G_BRIDGE):
                    continue
                case = grass_case
                if kind == G_WATER:
                    frame = self._frame(("extras", "water", 0), 172)
                    case = water_case
                elif kind == G_BRIDGE:
                    frame = self._frame(("extras", "bridge", 0), 172)
                    case = bridge_case
                elif kind == G_ROAD:
                    frame = road[(x + y) % max(1, len(road))]
                    case = road_case
                elif kind == G_DIRT:
                    frame = self._frame(("background", "dirt"), 39)
                    case = dirt_case
                else:
                    frame = grass[(x + y * 3) % max(1, len(grass))]
                    if detail and (x * 7 + y * 11) % 23 == 7:
                        frame, case = detail[(x + y) % len(detail)], detail_case
                    # The renderer's corner mask: a corner bit is set when the
                    # highest-priority surface at that shared corner is earth.
                    mask = 0
                    here = GROUND_PRIORITY.get(kind, 0)
                    earth = GROUND_PRIORITY[G_DIRT]
                    for bit, (dx, dy) in ((1, (-1, -1)), (2, (0, -1)), (4, (-1, 0)), (8, (0, 0))):
                        best = here
                        for (ox, oy) in ((dx, dy), (dx + 1, dy), (dx, dy + 1), (dx + 1, dy + 1)):
                            gx, gy = x + ox, y + oy
                            if 0 <= gx < self.WIDTH and 0 <= gy < self.HEIGHT:
                                best = max(best, GROUND_PRIORITY.get(self.ground[gy][gx], 0))
                        if best == earth:
                            mask |= bit
                    if mask and mask < 15 and mask < len(transitions) and transitions[mask] >= 0:
                        frame, case = transitions[mask], seam_case
                scene.paste(self._tile("background", frame), (x * TILE_SIZE, y * TILE_SIZE))
                self.owners[(x, y)] = case

    def _paint_gallery(self, scene):
        """Bottom strip of swatches for the cases no natural layout can show.

        The gallery is informational: clicking still jumps to the case, but the
        swatches are painted standalone rather than by the renderer rule.
        """
        draw = ImageDraw.Draw(scene)
        y0 = self.GALLERY_Y
        # A divider so the gallery reads as a separate strip.
        draw.rectangle([0, y0 * TILE_SIZE, self.WIDTH * TILE_SIZE - 1,
                        y0 * TILE_SIZE + 1], fill=(255, 223, 0, 255))
        x, y = 1, y0 + 1
        connected = self._list(("building", "connected"), tuple(range(16)))

        def swatch(group, frame, case_key, case_value):
            nonlocal x, y
            if x + 1 >= self.WIDTH:
                x, y = 1, y + 1
            if y >= self.HEIGHT:
                return
            scene.paste(self._tile(group, frame), (x * TILE_SIZE, y * TILE_SIZE))
            case = self._case_for(case_key, case_value)
            if case is not None:
                self.owners[(x, y)] = case
            x += 1

        # Interior partitions have no perimeter side, so they never appear on a
        # house edge: every mask gets its own swatch here.
        for mask in range(16):
            frame = connected[mask] if mask < len(connected) else -1
            swatch("building", frame, ("building", "connected"), mask)


    def _house_wall_cells(self):
        """Every perimeter cell of every building, as a set for masking."""
        cells = set()
        for (bx, by, bw, bh, _s) in self.buildings:
            for x in range(bx, bx + bw):
                cells |= {(x, by), (x, by + bh - 1)}
            for y in range(by, by + bh):
                cells |= {(bx, y), (bx + bw - 1, y)}
        return cells

    def _paint_walls(self, scene):
        """Floors, facades, brick and fence runs (each with its own mask rule)."""
        for (bx, by, bw, bh, _style) in self.buildings:
            for y in range(by, by + bh):
                for x in range(bx, bx + bw):
                    edge = x in (bx, bx + bw - 1) or y in (by, by + bh - 1)
                    self.solid[y][x] = "facade" if edge else None

        # A brick compound and a wire pen, so both families show straight runs,
        # all four corners and a couple of T junctions.
        self._wall_run("brick", 46, 18, 8, "h")
        self._wall_run("brick", 46, 23, 8, "h")
        self._wall_run("brick", 46, 18, 6, "v")
        self._wall_run("brick", 53, 18, 6, "v")
        self._wall_run("brick", 49, 20, 3, "v")     # a T off the north wall
        self._wall_run("fence", 15, 17, 12, "h")
        self._wall_run("fence", 15, 22, 12, "h")
        self._wall_run("fence", 15, 17, 6, "v")
        self._wall_run("fence", 26, 17, 6, "v")

        for (bx, by, bw, bh, _style) in self.buildings:
            self._paint_house(scene, bx, by, bw, bh)

        frames_cache = {}
        for (x, y, kind) in self.wall_cells:
            mask = 0
            for (dx, dy, bit) in ((-1, 0, LEFT), (1, 0, RIGHT), (0, -1, UP), (0, 1, DOWN)):
                if self._wall_kind(x + dx, y + dy) == kind:
                    mask |= bit
            if kind not in frames_cache:
                frames_cache[kind] = self._list((kind, "connected"), tuple(range(16)))
            frames = frames_cache[kind]
            frame = frames[mask] if 0 <= mask < len(frames) else -1
            scene.paste(self._tile(kind, frame), (x * TILE_SIZE, y * TILE_SIZE))
            self.owners[(x, y)] = self._case_for((kind, "connected"), mask)

    def _wall_run(self, kind, x0, y0, length, axis):
        for i in range(length):
            x, y = (x0 + i, y0) if axis == "h" else (x0, y0 + i)
            if 0 <= x < self.WIDTH and 0 <= y < self.HEIGHT and self.solid[y][x] is None:
                self.solid[y][x] = kind
                self.wall_cells.append((x, y, kind))

    def _paint_house(self, scene, bx, by, bw, bh):
        """One house: facade ring outside, floor inside, like the real renderer.

        The facade index is the renderer's own priority order (corners before
        edges), with the four storage slots cycling through the floor frames.
        """
        facade = self._list(("building", "facade"), tuple(range(8)))
        floors = self._list(("building", "floor"), (0,))
        for y in range(by, by + bh):
            for x in range(bx, bx + bw):
                on_left, on_right = x == bx, x == bx + bw - 1
                on_top, on_bottom = y == by, y == by + bh - 1
                if on_top and on_left: index = 0
                elif on_top and on_right: index = 2
                elif on_bottom and on_left: index = 5
                elif on_bottom and on_right: index = 7
                elif on_top: index = 1
                elif on_bottom: index = 6
                elif on_left: index = 3
                elif on_right: index = 4
                else:
                    index = None
                if index is None:
                    which = (x + y) % max(1, len(floors))
                    scene.paste(self._tile("building", floors[which]),
                                (x * TILE_SIZE, y * TILE_SIZE))
                    self.owners[(x, y)] = self._case_for(("building", "floor"), which)
                else:
                    frame = facade[index] if index < len(facade) else -1
                    scene.paste(self._tile("building", frame), (x * TILE_SIZE, y * TILE_SIZE))
                    self.owners[(x, y)] = self._case_for(("building", "facade"), index)

    def _paint_roofs(self, scene):
        """One roof over each house, with the renderer's own row/col sampling.

        Columns come from tile x (left edge -> left, right edge -> right,
        everything between -> middle); rows come from the roof height (top and
        bottom edges, the ridge in the middle, slats between), and the bottom
        footprint row stays uncovered so the front wall keeps showing.
        """
        roof_data = self.data.get("roof", [])
        for (bx, by, bw, bh, style) in self.buildings:
            block = roof_data[style % max(1, len(roof_data))] if roof_data else []
            rh = bh - 1
            if rh < 1:
                continue
            for ry in range(rh):
                row = (2 if rh == 1 else 0 if ry == 0 else 4 if ry == rh - 1
                       else 2 if ry == rh // 2 else 1 if ry < rh // 2 else 3)
                for rx in range(bw):
                    col = 1 if bw <= 1 else 0 if rx == 0 else 2 if rx == bw - 1 else 1
                    x, y = bx + rx, by + ry
                    frame = block[row][col] if row < len(block) and col < len(block[row]) else -1
                    scene.alpha_composite(self._tile("roof", frame), (x * TILE_SIZE, y * TILE_SIZE))
                    case = self._case_for(("roof", style, row), col)
                    if case is not None:
                        self.owners[(x, y)] = case

    def _paint_probes(self, scene):
        """Reveal cells the roof is hiding: interior walls and the floor rows.

        A small circular peephole is knocked out of each roof so the walls and
        floor underneath stay visible -- with hatched drilling and corner
        chevrons pointing at them because a hole is how the game marks them.
        Clicking a probed tile still selects the wall or floor case beneath it.
        """
        draw = ImageDraw.Draw(scene)
        for (bx, by, bw, bh, _style) in self.buildings:
            cx, cy = bx + bw // 2, by + (bh - 1) // 2
            r = max(1, min(bw, bh - 1) // 3)
            # The peephole outline, with the renderer's own corner chevrons.
            draw.ellipse([cx * TILE_SIZE - r * TILE_SIZE, cy * TILE_SIZE - r * TILE_SIZE,
                          (cx + 1) * TILE_SIZE + r * TILE_SIZE, (cy + 1) * TILE_SIZE + r * TILE_SIZE],
                         outline=(255, 223, 0, 255), width=2)
            for (dx, dy) in ((-1, -1), (1, -1), (-1, 1), (1, 1)):
                px, py = (cx + dx * (r + 1)) * TILE_SIZE + 8, (cy + dy * (r + 1)) * TILE_SIZE + 8
                draw.line([(px - 5 - dx * 3, py - dy * 3), (px + dx * 3, py + dy * 3)], fill=(255, 223, 0, 255), width=2)
                draw.line([(px - dx * 3, py - 5 - dy * 3), (px + dx * 3, py + dy * 3)], fill=(255, 223, 0, 255), width=2)
            self.probes.append((cx, cy, self.owners.get((cx, cy))))



def load_mappings():
    """Load the JSON over the defaults, tolerating a missing or older file.

    Anything the JSON omits keeps its default, so a mapping written by an older
    version of the editor still loads once new cases are added.
    """
    data = copy.deepcopy(DEFAULTS)
    if not JSON_PATH.exists():
        return data
    try:
        loaded = json.loads(JSON_PATH.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return data
    if not isinstance(loaded, dict):
        return data
    for section in ("building", "fence", "brick", "roof", "background",
                    "extras", "sheet_overrides"):
        if section in loaded:
            data[section] = copy.deepcopy(loaded[section])
    data["version"] = DEFAULTS["version"]
    return data


def cpp_list(values):
    return ", ".join(str(int(value)) for value in values)


def write_files(data):
    JSON_PATH.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    b = data["building"]
    bg = data["background"]
    roof_lines = []
    for style in data["roof"]:
        rows = ", ".join("{" + cpp_list(row) + "}" for row in style)
        roof_lines.append("    {" + rows + "}")
    overrides = data.get("sheet_overrides", {})
    def sheet_key(group):
        value = str(overrides.get(group, ""))
        if value.lower().startswith("assets/sprites/"):
            value = value[14:]
        value = value.replace("\\", "/").rsplit(".", 1)[0].lower()
        return value
    header = f'''#pragma once

// Generated by tools/tile_selector.py. Edit through the selector so the JSON and
// this compile-time header stay in sync.
namespace TileMappings {{

inline constexpr const char* BUILDING_SHEET = "{sheet_key("building")}";
inline constexpr const char* FENCE_SHEET = "{sheet_key("fence")}";
inline constexpr const char* BRICK_SHEET = "{sheet_key("brick")}";
inline constexpr const char* ROOF_SHEET = "{sheet_key("roof")}";
inline constexpr const char* BACKGROUND_SHEET = "{sheet_key("background")}";

inline constexpr int BUILDING_FACADE[8] = {{{cpp_list(b["facade"])}}};
inline constexpr int BUILDING_CONNECTED[16] = {{{cpp_list(b["connected"])}}};
inline constexpr int BUILDING_FLOOR[4] = {{{cpp_list(b["floor"])}}};
inline constexpr int FENCE_CONNECTED[16] = {{{cpp_list(data["fence"]["connected"])}}};
inline constexpr int BRICK_CONNECTED[16] = {{{cpp_list(data["brick"]["connected"])}}};

// Two roof styles, five vertical bands, three horizontal positions.
inline constexpr int ROOF[2][5][3] = {{
{',\n'.join(roof_lines)}
}};

inline constexpr int GRASS[5] = {{{cpp_list(bg["grass"])}}};
inline constexpr int GRASS_DETAIL[3] = {{{cpp_list(bg["grass_detail"])}}};
inline constexpr int DIRT = {int(bg["dirt"])};
inline constexpr int ROAD[2] = {{{cpp_list(bg["road"])}}};
inline constexpr int GRASS_DIRT[16] = {{{cpp_list(bg["grass_dirt"])}}};

}}  // namespace TileMappings
'''
    HEADER_PATH.write_text(header, encoding="utf-8")


class TileSelector(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Outbound World Tile Selector")
        self.geometry("1660x860")
        self.minsize(1200, 620)
        self.data = load_mappings()
        self.by_id = {}
        self.current = None
        self.photo = None
        self.original = None
        self.zoom = tk.IntVar(value=3)
        self.palette = tk.StringVar(value="Beige")
        self.status = tk.StringVar(value="Select a named world case, then click its correct tile.")
        self.info = tk.StringVar(value="No case selected")
        self.scene_info = tk.StringVar(value="Scene preview")
        self.preview_process = None
        self.composer = MapComposer(self.data, lambda: self.palette.get())
        self.scene_photo = None
        self.preview_zoom = tk.IntVar(value=2)
        self.protocol("WM_DELETE_WINDOW", self.close_editor)
        self._build_ui()
        self._populate_tree()

    def _build_ui(self):
        toolbar = ttk.Frame(self, padding=8)
        toolbar.pack(fill="x")
        ttk.Button(toolbar, text="Save mappings", command=self.save).pack(side="left")
        ttk.Button(toolbar, text="Save & Build game", command=self.save_and_build).pack(side="left", padx=(6, 16))
        ttk.Button(toolbar, text="Launch / restart game", command=self.launch_preview).pack(side="left")
        ttk.Button(toolbar, text="Clear mapping", command=self.clear_current).pack(side="left")
        ttk.Button(toolbar, text="Choose tileset...", command=self.choose_tileset).pack(side="left", padx=(6, 0))
        ttk.Label(toolbar, text="Building palette:").pack(side="left", padx=(20, 4))
        palette = ttk.Combobox(toolbar, textvariable=self.palette, values=list(BUILDING_SHEETS), width=10, state="readonly")
        palette.pack(side="left")
        palette.bind("<<ComboboxSelected>>", lambda _event: (self.show_sheet(), self.refresh_scene()))
        ttk.Label(toolbar, text="Zoom:").pack(side="left", padx=(20, 4))
        zoom = ttk.Combobox(toolbar, textvariable=self.zoom, values=(1, 2, 3, 4, 5), width=3, state="readonly")
        zoom.pack(side="left")
        zoom.bind("<<ComboboxSelected>>", lambda _event: self.show_sheet())

        pane = ttk.Panedwindow(self, orient="horizontal")
        pane.pack(fill="both", expand=True, padx=8)
        left = ttk.Frame(pane)
        right = ttk.Frame(pane)
        scene = ttk.Frame(pane)
        pane.add(left, weight=1)
        pane.add(right, weight=3)
        pane.add(scene, weight=2)

        ttk.Label(left, text="World cases actually used", font=("Segoe UI", 10, "bold")).pack(anchor="w", pady=(0, 5))
        self.tree = ttk.Treeview(left, show="tree", selectmode="browse")
        tree_scroll = ttk.Scrollbar(left, orient="vertical", command=self.tree.yview)
        self.tree.configure(yscrollcommand=tree_scroll.set)
        self.tree.pack(side="left", fill="both", expand=True)
        tree_scroll.pack(side="right", fill="y")
        self.tree.bind("<<TreeviewSelect>>", self.select_case)

        ttk.Label(right, textvariable=self.info, font=("Segoe UI", 11, "bold")).pack(anchor="w", pady=(0, 5))
        canvas_frame = ttk.Frame(right)
        canvas_frame.pack(fill="both", expand=True)
        self.canvas = tk.Canvas(canvas_frame, background="#202020", highlightthickness=0)
        xscroll = ttk.Scrollbar(canvas_frame, orient="horizontal", command=self.canvas.xview)
        yscroll = ttk.Scrollbar(canvas_frame, orient="vertical", command=self.canvas.yview)
        self.canvas.configure(xscrollcommand=xscroll.set, yscrollcommand=yscroll.set)
        self.canvas.grid(row=0, column=0, sticky="nsew")
        yscroll.grid(row=0, column=1, sticky="ns")
        xscroll.grid(row=1, column=0, sticky="ew")
        canvas_frame.rowconfigure(0, weight=1)
        canvas_frame.columnconfigure(0, weight=1)
        self.canvas.bind("<Button-1>", self.click_tile)

        ttk.Label(scene, textvariable=self.scene_info, font=("Segoe UI", 11, "bold")).pack(anchor="w", pady=(0, 5))
        scene_bar = ttk.Frame(scene)
        scene_bar.pack(fill="x")
        ttk.Label(scene_bar, text="Representative scene — how it all looks together").pack(side="left")
        ttk.Label(scene_bar, text="Zoom:").pack(side="left", padx=(16, 4))
        scene_zoom = ttk.Combobox(scene_bar, textvariable=self.preview_zoom, values=(1, 2, 3, 4), width=3, state="readonly")
        scene_zoom.pack(side="left")
        scene_zoom.bind("<<ComboboxSelected>>", lambda _event: self.refresh_scene())
        scene_frame = ttk.Frame(scene)
        scene_frame.pack(fill="both", expand=True)
        self.scene_canvas = tk.Canvas(scene_frame, background="#101010", highlightthickness=0)
        scene_x = ttk.Scrollbar(scene_frame, orient="horizontal", command=self.scene_canvas.xview)
        scene_y = ttk.Scrollbar(scene_frame, orient="vertical", command=self.scene_canvas.yview)
        self.scene_canvas.configure(xscrollcommand=scene_x.set, yscrollcommand=scene_y.set)
        self.scene_canvas.grid(row=0, column=0, sticky="nsew")
        scene_y.grid(row=0, column=1, sticky="ns")
        scene_x.grid(row=1, column=0, sticky="ew")
        scene_frame.rowconfigure(0, weight=1)
        scene_frame.columnconfigure(0, weight=1)
        self.refresh_scene()

        status = ttk.Label(self, textvariable=self.status, padding=8, anchor="w")
        status.pack(fill="x")
        self.output = tk.Text(self, height=7, state="disabled", font=("Consolas", 9), wrap="word")
        self.output.pack(fill="x", padx=8, pady=(0, 8))

    def refresh_scene(self):
        """Rebuild the combined scene image from the current mappings."""
        self.composer.data = self.data
        self.composer.invalidate()
        try:
            scene = self.composer.compose()
        except Exception as exc:  # a broken sheet must not take the editor down
            self.scene_info.set(f"Scene preview unavailable: {exc}")
            return
        scale = self.preview_zoom.get()
        shown = scene.resize((scene.width * scale, scene.height * scale), Image.Resampling.NEAREST)
        self.scene_photo = ImageTk.PhotoImage(shown)
        self.scene_canvas.delete("all")
        self.scene_canvas.create_image(0, 0, anchor="nw", image=self.scene_photo)
        self.scene_canvas.configure(scrollregion=(0, 0, shown.width, shown.height))
        self.scene_info.set("Scene preview — representative result of the current mappings")

    def _populate_tree(self):
        groups = {}
        for number, item in enumerate(CASES):
            parent = groups.get(item["group"])
            if parent is None:
                parent = self.tree.insert("", "end", text=item["group"], open=False)
                groups[item["group"]] = parent
            value = get_value(self.data, item["path"])
            item_id = self.tree.insert(parent, "end", text=f'{item["label"]}  [tile {value}]')
            self.by_id[item_id] = number

    def select_case(self, _event=None):
        selection = self.tree.selection()
        if not selection or selection[0] not in self.by_id:
            return
        self.current = self.by_id[selection[0]]
        self.show_sheet()

    def sheet_for_current(self):
        item = CASES[self.current]
        override = self.data.get("sheet_overrides", {}).get(item.get("sheet_group", ""), "")
        if override:
            return override
        if item["palette"]:
            return BUILDING_SHEETS[self.palette.get()]
        return item["sheet"]

    def choose_tileset(self):
        if self.current is None:
            messagebox.showinfo("Choose a case first", "Select a world case before choosing a tileset.")
            return
        item = CASES[self.current]
        group = item.get("sheet_group")
        if not group:
            return
        selected = filedialog.askopenfilename(
            title=f"Choose tileset for {item['group']}",
            initialdir=str(ROOT / "assets" / "sprites" / "Tiles"),
            filetypes=(("PNG tilesets", "*.png"), ("All files", "*.*")),
        )
        if not selected:
            return
        source = Path(selected)
        OVERRIDE_DIR.mkdir(parents=True, exist_ok=True)
        destination = OVERRIDE_DIR / f"{group}_TileSet.png"
        try:
            shutil.copy2(source, destination)
        except OSError as exc:
            messagebox.showerror("Could not copy tileset", str(exc))
            return
        # Store the runtime asset key, not an absolute machine-specific path.
        relative = f"assets/sprites/TileOverrides/{group}_TileSet.png"
        self.data.setdefault("sheet_overrides", {})[group] = relative
        self.refresh_current_tree_label()
        self.show_sheet()
        self.refresh_scene()
        self.status.set(f"Using selected tileset {source.name} for all {group} cases. Save & Build to apply it in-game.")

    def show_sheet(self):
        if self.current is None:
            return
        item = CASES[self.current]
        sheet = self.sheet_for_current()
        path = ROOT / sheet
        # Every case carries its renderer sheet.  Building cases are the only
        # exceptions: the game deliberately picks one of its four palette
        # sheets at runtime, so the palette control selects that exact variant.
        try:
            self.original = Image.open(path).convert("RGBA")
        except (FileNotFoundError, OSError) as exc:
            self.canvas.delete("all")
            self.info.set(f'{item["group"]} — {item["label"]} — missing sheet')
            self.status.set(f"Could not open renderer sheet {sheet}: {exc}")
            return
        scale = self.zoom.get()
        shown = self.original.resize((self.original.width * scale, self.original.height * scale), Image.Resampling.NEAREST)
        self.photo = ImageTk.PhotoImage(shown)
        self.canvas.delete("all")
        self.canvas.create_image(0, 0, anchor="nw", image=self.photo)
        cell = TILE_SIZE * scale
        cols, rows = self.original.width // TILE_SIZE, self.original.height // TILE_SIZE
        for row in range(rows):
            for col in range(cols):
                x, y = col * cell, row * cell
                self.canvas.create_rectangle(x, y, x + cell, y + cell, outline="#777777", width=1)
                self.canvas.create_text(x + 3, y + 2, anchor="nw", text=str(row * cols + col),
                                        fill="white", font=("Consolas", max(7, scale * 3), "bold"))
        value = get_value(self.data, item["path"])
        if 0 <= value < cols * rows:
            col, row = value % cols, value // cols
            self.canvas.create_rectangle(col * cell + 1, row * cell + 1,
                                         (col + 1) * cell - 1, (row + 1) * cell - 1,
                                         outline="#ffdf00", width=max(2, scale))
            self.canvas.xview_moveto(max(0, (col * cell - 100) / max(1, shown.width)))
            self.canvas.yview_moveto(max(0, (row * cell - 100) / max(1, shown.height)))
            location = f"tile {value} (column {col}, row {row})"
        else:
            location = "no tile assigned"
        self.canvas.configure(scrollregion=(0, 0, shown.width, shown.height))
        self.info.set(f'{item["group"]} — {item["label"]} — {location} — {sheet}')
        self.status.set(f"Renderer sheet: {sheet}. Click a grid cell to assign it to this case.")

    def click_tile(self, event):
        if self.current is None or self.original is None:
            return
        cell = TILE_SIZE * self.zoom.get()
        col = int(self.canvas.canvasx(event.x) // cell)
        row = int(self.canvas.canvasy(event.y) // cell)
        cols, rows = self.original.width // TILE_SIZE, self.original.height // TILE_SIZE
        if not (0 <= col < cols and 0 <= row < rows):
            return
        set_value(self.data, CASES[self.current]["path"], row * cols + col)
        self.refresh_current_tree_label()
        self.show_sheet()
        self.refresh_scene()
        self.status.set(f"Assigned tile {row * cols + col} (column {col}, row {row}). Save when finished.")

    def clear_current(self):
        if self.current is None:
            return
        item = CASES[self.current]
        if not item["clearable"]:
            messagebox.showinfo("Required mapping", "This case must have a tile. Only optional transitions can be cleared.")
            return
        set_value(self.data, item["path"], -1)
        self.refresh_current_tree_label()
        self.show_sheet()
        self.refresh_scene()

    def refresh_current_tree_label(self):
        selected = self.tree.selection()[0]
        item = CASES[self.current]
        value = get_value(self.data, item["path"])
        self.tree.item(selected, text=f'{item["label"]}  [tile {value}]')

    def log(self, message):
        self.output.configure(state="normal")
        self.output.insert("end", message)
        self.output.see("end")
        self.output.configure(state="disabled")

    def save(self, quiet=False):
        try:
            write_files(self.data)
        except Exception as exc:
            messagebox.showerror("Could not save", str(exc))
            return False
        self.status.set("Saved assets/tile_mappings.json and generated src/tile_mappings.generated.h")
        if not quiet:
            self.log("Mappings saved.\n")
        return True

    def save_and_build(self):
        if not self.save(quiet=True):
            return
        self.log("\nBuilding game...\n")
        self.status.set("Building Outbound...")
        threading.Thread(target=self._build_worker, daemon=True).start()

    def _build_worker(self):
        flags = subprocess.CREATE_NO_WINDOW if hasattr(subprocess, "CREATE_NO_WINDOW") else 0
        process = subprocess.Popen(["cmd", "/c", str(ROOT / "build.bat")], cwd=ROOT, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, text=True, creationflags=flags)
        for line in process.stdout:
            self.after(0, self.log, line)
        code = process.wait()
        result = "Build complete: bin/Outbound.exe" if code == 0 else f"Build failed (exit code {code})"
        self.after(0, self.status.set, result)
        if code == 0:
            self.after(0, self.launch_preview)
            self.after(0, messagebox.showinfo, "Build complete", "The mappings were saved and bin/Outbound.exe was rebuilt.")

    def launch_preview(self):
        """Run the game beside the editor; rebuilding transparently restarts it."""
        if self.preview_process is not None and self.preview_process.poll() is None:
            self.preview_process.terminate()
            try:
                self.preview_process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.preview_process.kill()
        executable = ROOT / "bin" / "Outbound.exe"
        if not executable.exists():
            messagebox.showwarning("Game not built", "Build the game once before launching the preview.")
            return
        flags = subprocess.CREATE_NEW_PROCESS_GROUP if hasattr(subprocess, "CREATE_NEW_PROCESS_GROUP") else 0
        self.preview_process = subprocess.Popen([str(executable)], cwd=ROOT, creationflags=flags)
        self.status.set("Game preview is running. Save & Build will restart it with the new mappings.")

    def close_editor(self):
        if self.preview_process is not None and self.preview_process.poll() is None:
            self.preview_process.terminate()
        self.destroy()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--generate-only", action="store_true", help="regenerate the C++ header without opening the UI")
    args = parser.parse_args()
    if args.generate_only:
        write_files(load_mappings())
        print(f"Generated {HEADER_PATH}")
        return
    TileSelector().mainloop()


if __name__ == "__main__":
    main()
