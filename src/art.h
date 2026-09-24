// Maps game concepts onto the sprite pack in assets/sprites, with the built-in
// procedural art as a fallback for anything the pack does not cover.
#pragma once
#include "assets.h"
#include "core.h"
#include <vector>

struct World;

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

// ---- world props (bottom-anchored, may be taller than one tile) ----
Piece tree(uint8_t variant, float time = 0);
Piece bush(uint8_t variant, float time = 0);
Piece rock(uint8_t variant);
Piece barrel(uint8_t variant);
Piece car(uint8_t variant);
// A wreck from the vehicle pack (0.11v): `variant` picks the car, `dir` 0..7 which way
// it faces (east, then clockwise).
Piece wreck(uint8_t variant, int dir);
int wreckCount();
Piece streetLight(uint8_t variant);
Piece groundDeco(uint8_t variant);       // grass tufts, flowers, litter
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
