#pragma once
#include <cstdint>
#include <vector>

// Every sprite can be replaced by dropping assets/sprites/<name>.png (16x16 recommended).
#define SPRITE_LIST \
    S(WHITE) S(GRASS0) S(GRASS1) S(GRASS2) S(DIRT) S(SAND) S(WATER0) S(WATER1) S(ROAD) S(BRIDGE) \
    S(FLOOR_WOOD) S(FLOOR_CONCRETE) S(FLOOR_TILE) S(RUBBLE) S(BASE_FLOOR) \
    S(WALL_BRICK) S(WALL_CONCRETE) S(WALL_WOOD) S(TREE) S(ROCK) S(CRATE) S(FENCE) S(SANDBAG) \
    S(BOUNDARY) S(BUNKER_WALL) S(HATCH) S(STUMP) S(BUSH) \
    S(CRACK1) S(CRACK2) S(CRACK3) \
    S(C_CRATE) S(C_LOCKER) S(C_CABINET) S(C_MILCRATE) S(C_BAG) S(C_CORPSE) S(C_TOOLBOX) \
    S(PLAYER) S(SCAV) S(BANDIT) S(HEAVY) S(SNIPER) S(SHADE) S(TRADER) \
    S(BED) S(STASH) S(TERMINAL) S(WORKBENCH) S(LAMP) S(TABLE) S(RUG) S(PLANT) S(EXIT_LADDER) \
    S(CIRCLE) S(RING) S(BULLET) S(GRENADE) S(ROCKET) S(PARTICLE) S(BLOOD0) S(BLOOD1) S(SCORCH) \
    S(CROSSHAIR) S(ARROW) S(HOME_ICON) S(SUN) S(MOON) \
    S(I_SCRAP) S(I_WIRES) S(I_BOLTS) S(I_TAPE) S(I_BATTERY) S(I_CIRCUIT) S(I_WATCH) S(I_GPU) \
    S(I_JEWELRY) S(I_FOOD) S(I_MEDSUP) S(I_FUEL) S(I_GUNPARTS) S(I_INTEL) \
    S(I_BANDAGE) S(I_MEDKIT) S(I_GRENADE) \
    S(I_AMMO_LIGHT) S(I_AMMO_SHELL) S(I_AMMO_RIFLE) S(I_AMMO_SNIPER) S(I_ROCKET) \
    S(I_PISTOL) S(I_REVOLVER) S(I_SMG) S(I_SHOTGUN) S(I_CARBINE) S(I_RIFLE) S(I_SNIPER) S(I_LAUNCHER) \
    S(I_VEST_LIGHT) S(I_VEST_HEAVY) S(I_PACK_SMALL) S(I_PACK_LARGE) S(I_COIN)

namespace Assets { struct Sprite; }

namespace Sprites {
#define S(n) n,
enum Id : int { SPRITE_LIST COUNT };
#undef S

constexpr int SIZE = 16;

const char* name(int id);
// Paints the built-in 16x16 art and hands it to the asset atlas (call before Assets::load).
void registerFallbacks();
// Caches atlas locations (call after Assets::load).
void resolve();
void uv(int id, float& u0, float& v0, float& u1, float& v1);
// Built-in art for a slot, used when the asset pack has nothing better.
const Assets::Sprite* fallback(int id);
}  // namespace Sprites
