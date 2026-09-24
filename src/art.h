// Maps game concepts onto the sprite pack in assets/sprites, with the built-in
// procedural art as a fallback for anything the pack does not cover.
#pragma once
#include "assets.h"
#include "core.h"
#include <vector>

struct World;
struct WorldProp;

namespace Art {

enum class Dir { Down, Up, Right, Left };
enum class Anim { Idle, Run, Shoot, Reload, Death, Attack, Walk };

struct Piece {
    const Assets::Sprite* sprite = nullptr;
    int frame = 0;
    bool flipX = false;
    float scale = 1;
    // Wind: how many pixels the top row leans sideways. Rows shift in whole pixels,
    // more the higher they are, and the trunk/base never moves.
    float sway = 0;
    bool valid() const { return sprite && sprite->valid(); }
};

void init();
bool packLoaded();                       // true when real art was found

Dir dirFromAngle(float angle);

// ---- terrain ----
Assets::TileRef ground(int groundType, uint8_t variant);
// The tile to draw at (x,y), joining its ground to the neighbouring ground types
// with corner art wherever the pack provides it.
Assets::TileRef groundTile(const ::World& w, int x, int y, float time);
// True when the road tile at (x, y) has pack art for its edges (so none need drawing).
bool roadEdgeTile(const ::World& w, int x, int y);
// neighbourMask bits: 1 = same wall left, 2 = right, 4 = up, 8 = down;
// 16/32/64/128 mark a generated building's left/right/top/bottom perimeter.
Piece wallTile(int solidType, uint8_t variant, int neighbourMask = 0);
// One cell of a building roof. col 0/1/2 = left, middle, right; row 0..4 = top edge,
// upper slats, ridge, lower slats, bottom edge.
Assets::TileRef roofTile(uint8_t style, int col, int row);
Piece door(uint8_t variant);             // marks a way into a roofed building
// 0.12v: a flat concrete roof, drawn from the building's own facade sheet (0..3).
Assets::TileRef flatRoofTile(uint8_t sheet, int col, int row, uint8_t variant);

// ---- ground overlays (Tile::overlay, 0.12v) ----
// Flat art laid over a tile's ground: road paint, heaps of garbage, grass creeping onto
// paving. Ids are ranges; the generator adds the frame it wants to the base.
enum Overlay : uint8_t {
    OV_NONE = 0,
    OV_CROSS_EW = 1,       // 1..3 zebra stripes for a road running left-right (a column of them)
    OV_CROSS_NS = 4,       // 4..6 zebra stripes for a road running up-down (a row of them)
    OV_PARKING = 7,        // 7..18 a pair of parking bays, 4 x 3 tiles, row by row
    OV_GARBAGE = 20,       // 20..48 Garbage_TileSet frames 0..28
    OV_GRASSTOP = 50,      // 50..97 Grass_On-Top_TileSet frames 0..47
    OV_COUNT = 98
};
Assets::TileRef overlayTile(uint8_t overlay);

// ---- world props (bottom-anchored, may be taller than one tile) ----
// `tone` (Tone) picks the palette: one colouring per stand of trees rather than a
// random one per tree. TONE_AUTO keeps the old mix.
Piece tree(uint8_t variant, float time = 0, uint8_t tone = 0);
Piece bush(uint8_t variant, float time = 0, uint8_t tone = 0);
Piece rock(uint8_t variant);
Piece barrel(uint8_t variant);
// tone 0: an ordinary (or rusted) car; TONE_GREEN / DARK / BLEAK: one overgrown with that grass.
Piece car(uint8_t variant, uint8_t tone = 0);
// A wreck from the vehicle pack (0.11v): `variant` picks the car, `dir` 0..7 which way
// it faces (east, then clockwise).
Piece wreck(uint8_t variant, int dir);
int wreckCount();
Piece streetLight(uint8_t variant);
// Tile::worldDeco: grass tufts, flowers, litter. The top three bits pick the kind
// (DecoKind), the low five which one; tufts and moss follow `tone`.
enum DecoKind : uint8_t { DK_LEGACY, DK_TUFT, DK_FLOWER, DK_FOREST, DK_JUNK, DK_PEBBLE, DK_MOSS, DK_POSTER };
inline uint8_t decoCode(int kind, int which) { return (uint8_t)((kind << 5) | (which & 31)); }
Piece groundDeco(uint8_t code, uint8_t tone = 0);

// ---- standing objects (0.12v) ----
// Street furniture, clutter and rooftop gear from the pack, placed by the world's
// dressing pass as PROP_OBJECT / PROP_WALLDECO / roof props.
enum ObjectKind : uint8_t {
    OB_NONE, OB_BENCH_DOWN, OB_BENCH_UP, OB_BENCH_SIDE, OB_HYDRANT, OB_TRASH_CAN, OB_DUMPSTER, OB_VENDING,
    OB_STOP_DOWN, OB_STOP_UP, OB_STOP_SIDE, OB_BARREL, OB_TIRES, OB_PALLET, OB_CART, OB_CONE,
    OB_CONTAINER_V, OB_CONTAINER_H, OB_FRIDGE, OB_WASHER, OB_TRUNK, OB_STUMP, OB_BOULDER, OB_TRACTOR,
    OB_MOTORBIKE, OB_JUNK,
    // on a roof
    OB_HVAC, OB_VENT, OB_ANTENNA, OB_ROOF_HOLE, OB_DUCT,
    // on a wall
    OB_WINDOW, OB_WINDOW_BROKEN, OB_WINDOW_BOARDED, OB_POSTER, OB_GRAFFITI, OB_IVY, OB_AWNING, OB_SHOPFRONT,
    OB_COUNT
};
// `pick` chooses among the kind's art (colours, styles); `overgrown` 0 plain, 1 green,
// 2 dark green, 3 bleak yellow, where the pack draws it that way.
Piece object(int kind, int pick, int overgrown = 0);
int objectChoices(int kind);
// WorldProp::frame packing for objects: which one in the low six bits, overgrowth above.
inline uint8_t objectFrame(int pick, int overgrown) { return (uint8_t)((pick & 63) | ((overgrown & 3) << 6)); }
// The art of any prop that stands in the way with its pixels (cars, wrecks, objects).
Piece propArt(const ::WorldProp& p);
Piece stump();
// The bunker hatch, lid up; `closed` = slammed shut and sealed (a horde, the night).
Piece hatch(bool closed = false);
Piece containerArt(int kind, uint8_t variant);
Piece corpse(uint8_t variant);
Piece furniture(int which);

// ---- characters ----
// Human raiders and the player share the pack's character art; `enemy` picks the
// copy with the shirt dyed red.
// `shirt` is the player's chosen colour (Assets::shirt), ignored for enemies.
Piece humanBody(Dir d, Anim a, int frame, bool holdingGun, bool enemy = false, int shirt = 0);
Piece humanGun(Dir d, Anim a, int frame, int weaponItem);
Piece helmet(Dir d, int frame);
Piece zombie(int kind, Dir d, Anim a, int frame);
// A horde zombie going down; the frame clamps on the last, lying, one.
Piece zombieDeath(int kind, bool left, int frame);
Piece muzzleFlash(Dir d, int frame);
Piece bulletSprite(int weaponItem);

// ---- items & UI ----
const Assets::Sprite* itemIcon(int itemId);
Piece uiPiece(const char* name);

}  // namespace Art
