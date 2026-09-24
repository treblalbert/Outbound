#pragma once
#include "core.h"
#include "items.h"
#include <vector>

constexpr int TILE = 16;

enum Ground : uint8_t {
    G_GRASS, G_DIRT, G_SAND, G_WATER, G_ROAD, G_BRIDGE, G_FLOOR_WOOD, G_FLOOR_CONCRETE, G_FLOOR_TILE,
    G_RUBBLE, G_BASE_FLOOR, G_CRYPT,
    G_PAVEMENT,                // 0.11v: city paving (sidewalks, plazas, the mechanic's yard)
    G_VOID,                    // 0.11v: nothing at all round an upper floor
    G_WASTE,                   // 0.12v: dead grey-brown scrub (the Bleak-Yellow sheet)
    G_COUNT
};

enum Solid : uint8_t {
    S_NONE, S_TREE, S_BUSH, S_ROCK, S_WALL_BRICK, S_WALL_CONCRETE, S_WALL_WOOD, S_CRATE, S_FENCE,
    S_SANDBAG, S_CONTAINER, S_BUNKER, S_BOUNDARY, S_FURNITURE, S_CAR, S_POLE, S_DOOR, S_DOOR_OPEN, S_TURRET,
    S_CRYPT_WALL, S_CRYPT_PROP, S_CRYPT_GATE,
    S_VOID,                    // 0.11v: the black round an upper floor
    S_STAIRS,                  // 0.11v: a flight of stairs (drawn by its PROP_STAIRS)
    S_COUNT
};

// Loot container kinds (also selects the artwork).
enum ContKind : int { CK_CRATE = 1, CK_LOCKER, CK_CABINET, CK_MILITARY, CK_TOOLBOX, CK_BAG, CK_CORPSE, CK_CHEST, CK_URN };

// Large scenery that covers several tiles and is drawn from one anchor point.
enum PropKind : uint8_t {
    PROP_CAR, PROP_STREETLIGHT,
    // The catacombs (0.10v): the stair arch down (outside) and back up (inside), and
    // what dresses the halls. `variant` of a crypt door / exit is its dungeon.
    PROP_CRYPT_DOOR, PROP_CRYPT_EXIT, PROP_TORCH, PROP_CANDLE, PROP_PILLAR, PROP_COFFIN, PROP_SPIKES,
    PROP_CRYPT_GATE,           // 0.11v: the portcullis on the way back (variant = dungeon)
    // 0.11v cars: a wreck from the vehicle pack (variant = which one, frame = which way
    // it faces), stairs between the floors of a city building (variant = stairway), and
    // the mechanic's workshop sign.
    PROP_WRECK, PROP_STAIRS, PROP_GARAGE,
    // 0.12v dressing: a standing object from the pack (variant = Art::ObjectKind, frame =
    // which one of its kind, plus its overgrowth in the top bits; see Art::object). Its
    // tiles are reserved like a car's and it blocks with its pixels.
    PROP_OBJECT,
    // Flat on a building's front wall (windows, posters, graffiti, ivy, shop glass); gone
    // with the wall tile it hangs on. Same variant / frame as PROP_OBJECT.
    PROP_WALLDECO,
};
struct WorldProp {
    Vec2 pos;                  // baseline (bottom centre)
    uint8_t kind = PROP_CAR;
    uint8_t variant = 0;
    bool flipX = false;        // mirrored art (a street light on the far side of the road)
    uint8_t frame = 0;         // a wreck's facing (0..7); an object's pick and overgrowth
};

// A vegetation / overgrowth palette (Tile::tone): which of the pack's colourings the
// trees, bushes, tufts and overgrown props on a tile use. TONE_AUTO follows the ground.
enum Tone : uint8_t { TONE_AUTO, TONE_GREEN, TONE_DARK, TONE_BLEAK, TONE_ORANGE, TONE_YELLOW, TONE_RED, TONE_COUNT };

// Tile::flags
enum TileFlag : uint8_t {
    TF_KERB = 1,               // a planter (grass or earth) edged with a kerb where it ends
};

// ---- the bigger world (0.11v) ------------------------------------------------------
// From day 5 the mechanic has cars for sale, and the outside grows to five times its
// area, with cities out on the edge of it: concrete, tall buildings, gangs on patrol,
// better loot. Roads get wider and clearer from then on too.
constexpr int CITY_DAY = 5;
// The catacombs open on day 3 (0.11v): the trader brings you a locator that morning.
constexpr int CRYPT_DAY = 3;
inline int outsideSize(int day) { return day >= CITY_DAY ? 536 : 240; }

// A city: its streets and blocks, in tiles.
struct CityZone {
    int x0 = 0, y0 = 0, w = 0, h = 0;
    // 0.12v: where its streets run (the left / top tile of each, `street` tiles wide) and
    // how overgrown it is, 0 (kept) .. 1 (the grass has most of it back).
    std::vector<int> streetX, streetY;
    int street = 4;
    float overgrowth = 0;
    bool contains(int tx, int ty) const { return tx >= x0 && ty >= y0 && tx < x0 + w && ty < y0 + h; }
};
// One block between a city's streets and what was made of it (0.12v), for the dressing.
enum BlockKind : uint8_t { BLK_BUILDINGS, BLK_PARKING, BLK_PARK, BLK_RUIN, BLK_DEPOT };
struct CityBlock { int x0 = 0, y0 = 0, w = 0, h = 0; uint8_t kind = BLK_BUILDINGS; uint8_t yard = 0; int city = 0; };
// What a building is (0.12v), so the dressing can furnish its surroundings to suit.
enum BuildingKind : uint8_t { BK_CABIN, BK_TOWN, BK_WAREHOUSE, BK_MILITARY, BK_FARM, BK_CITY, BK_GARAGE, BK_BUNKER, BK_FLOOR };

// An upper floor of a city building. Floors are laid out off the map (like the
// catacombs), each the same size as the building below it; stairs join them.
struct Floor {
    int x0 = 0, y0 = 0, w = 0, h = 0;   // this floor, in tiles
    int bx = 0, by = 0;                 // the building it is on top of (its ground floor's corner)
    int level = 1;
    bool contains(int tx, int ty) const { return tx >= x0 && ty >= y0 && tx < x0 + w && ty < y0 + h; }
};
// One end of a flight of stairs: stand at `at` and press E to come out at `to`.
struct Stairway { Vec2 at, to; bool up = true; int floor = -1; };

// A gang walking the city streets together: they follow the route loop, one after the
// other, until something starts a fight.
struct PatrolRoute { std::vector<Vec2> points; };

// The mechanic (0.11v): his yard is always just east of the bunker compound.
inline int garageTx(int homeTx) { return homeTx + 17; }
inline int garageTy(int homeTy) { return homeTy - 1; }

struct SolidInfo {
    int sprite;
    int hp;              // -1 = indestructible
    bool blocksBullets;
    bool leavesRubble;
    int mapColor;
    // Radius in pixels of the part that actually gets in your way, measured from
    // the tile centre. 0 means the obstacle fills its whole tile. A tree is mostly
    // canopy, so only its trunk should stop you or a bullet.
    float radius;
    // Pebbles are scenery: they are still solid tiles, so they draw and can be blown
    // away, but there is nothing there to walk around.
    bool blocksMove = true;
};
const SolidInfo& solidInfo(int s);

struct Tile {
    uint8_t ground = G_GRASS;
    uint8_t solid = S_NONE;
    uint8_t variant = 0;
    uint8_t explored = 0;
    uint8_t deco = 0;          // built-in sprite drawn on top (base furniture), 0 = none
    uint8_t worldDeco = 0;     // scenery detail from the art pack, 0 = none
    int16_t hp = 0;
    int16_t container = -1;
    // Furniture in a building (FURN_PIECES index + 1, 0 = none, FURN_REST = the other
    // tiles of a wide piece, drawn by the tile at its left).
    uint8_t furn = 0;
    // 0.12v dressing: flat art laid over the ground (Art::overlayTile: road paint, garbage,
    // grass creeping over paving), the vegetation palette, and TileFlag bits.
    uint8_t overlay = 0;
    uint8_t tone = TONE_AUTO;
    uint8_t flags = 0;
};

// Furniture pieces that dress building interiors (the "furniture/..." sprites).
enum FurnPlace : uint8_t { FP_WALL, FP_FREE, FP_RUG };
struct FurnPiece { const char* key; uint8_t w; uint8_t place; };
extern const FurnPiece FURN_PIECES[];
extern const int FURN_PIECE_COUNT;
constexpr uint8_t FURN_REST = 255;

enum class EnemyType : int { Scav, Bandit, Heavy, Sniper, Shade, Zombie };

struct Container {
    Vec2 pos;
    int tx = -1, ty = -1;       // -1 when not bound to a tile (corpses, bags)
    int kind = CK_CRATE;
    uint8_t variant = 0;
    std::vector<Item> items;
    bool searched = false;
    float searchTime = 0.8f;
    bool removed = false;
};

struct EnemySpawn { Vec2 pos; EnemyType type; bool crypt = false; int patrol = -1; };

// A roofed structure. The roof covers the footprint except its bottom row, so the
// front wall and any door in it stay visible from outside.
struct Building {
    int x0 = 0, y0 = 0, w = 0, h = 0;
    uint8_t style = 0;
    float reveal = 0;          // 0 = roof drawn, 1 = faded out because you are inside
    int walls = 0;             // perimeter wall tiles at build time; the roof falls in once enough are gone
    // Way in. Marked with door art on top of the roof, which is otherwise the only
    // thing telling you where a building can be entered.
    static constexpr int MAX_DOORS = 8;
    int16_t doorX[MAX_DOORS] = {};
    int16_t doorY[MAX_DOORS] = {};
    uint8_t doorCount = 0;
    // 0.12v: which facade sheet its walls use, and a flat concrete roof (the city's)
    // instead of the pitched one, with its rooftop clutter (World::roofProps).
    uint8_t sheet = 0;
    uint8_t kind = BK_CABIN;
    bool flatRoof = false;
    int roofProp0 = 0, roofPropN = 0;

    bool isDoor(int tx, int ty) const {
        for (int i = 0; i < doorCount; i++)
            if (doorX[i] == tx && doorY[i] == ty) return true;
        return false;
    }
};

// A catacomb: a sealed maze of rooms beside the outside map (same world, its own
// corner of it), reached by a stair arch somewhere outside.
// How hard the catacombs are on a day: 0 on day 1, rising to 1 by day 20 (and
// staying there). Everything below scales with it: rooms, crowds, damage, health.
inline float cryptTier(int day) { float t = (day - 1) / 19.0f; return t < 0 ? 0 : t > 1 ? 1 : t; }

struct Dungeon {
    int x0 = 0, y0 = 0, w = 0, h = 0;   // its area, in tiles
    Vec2 door;                          // outside: where you stand to go down
    Vec2 arrive;                        // inside: where you come in (by the way out)
    Vec2 exit;                          // inside: where you stand to climb out
    // The way back (0.11v): a hall from the last room to the first, sealed by a
    // portcullis in the last room's wall that is only raised from inside that room.
    // gateX is its left tile (4 wide: frame, two bars, frame), gateY the wall row,
    // gateDir +1 when the room is below it (top wall), -1 above (bottom wall).
    int gateX = -1, gateY = 0, gateDir = 0;
    float gateAnim = 0;                 // runtime: 0 down .. 1 fully raised (drawing)
    bool hasGate() const { return gateX >= 0; }
    // Where you stand to work it, on the room's side.
    Vec2 gateFront() const { return {(gateX + 2) * (float)16, (gateY + gateDir) * (float)16 + 8 + gateDir * 4.0f}; }
    bool contains(int tx, int ty) const { return tx >= x0 && ty >= y0 && tx < x0 + w && ty < y0 + h; }
};

// Things that move but still stand in the way (the cars people drive, 0.11v): the raid
// sets these so every collides()/move() respects them without knowing what they are.
extern bool (*g_dynamicBlock)(float x, float y, float r);
extern float (*g_dynamicOverlap)(float x, float y, float r);

struct World {
    int w = 0, h = 0;
    int outW = 0, outH = 0;             // the outside map; beyond it only catacombs
    std::vector<Dungeon> dungeons;
    std::vector<Tile> tiles;
    std::vector<Container> containers;
    std::vector<EnemySpawn> spawns;
    std::vector<WorldProp> props;
    std::vector<Building> buildings;
    // 0.12v: what stands on the flat roofs (HVAC units, vents, antennas), drawn with the
    // roof and faded with it; each building owns a run of it (Building::roofProp0/N).
    std::vector<WorldProp> roofProps;
    // 0.11v: the cities, the upper floors of their buildings and the stairs between,
    // and the gangs' patrol routes (EnemySpawn::patrol indexes them).
    std::vector<CityZone> cities;
    std::vector<CityBlock> blocks;      // 0.12v: every city block, for the dressing
    std::vector<Floor> floors;
    std::vector<Stairway> stairs;
    std::vector<PatrolRoute> patrols;
    int day = 1;
    // Which prop (index into props) covers a tile, for the ones collided with by their
    // pixels (parked cars, wrecks); -1 none. Built by indexProps().
    std::vector<int16_t> propAt;
    void indexProps();
    Vec2 homePos;
    int homeTx = 0, homeTy = 0;
    uint64_t seed = 0;
    // Zombies mode (set before generate): every raider post becomes a pack of the dead
    // (EnemyType::Zombie spawns) and more packs roam the open ground.
    static bool zombieSpawns;
    // Set before generate: build today's catacombs too (raid worlds only).
    static bool withCrypts;
    // Where a point is: -1 outside, 0.. a catacomb, 100.. an upper floor (100 + index),
    // -2 nowhere (the rock between). Two things can only fight in the same place.
    int areaAt(Vec2 p) const { return areaAtTile(toTile(p.x), toTile(p.y)); }
    int areaAtTile(int tx, int ty) const {
        if (tx < outW && ty < outH) return -1;
        for (size_t i = 0; i < dungeons.size(); i++) if (dungeons[i].contains(tx, ty)) return (int)i;
        for (size_t i = 0; i < floors.size(); i++) if (floors[i].contains(tx, ty)) return 100 + (int)i;
        return -2;
    }
    int floorAt(Vec2 p) const { int a = areaAt(p); return a >= 100 ? a - 100 : -1; }
    // Where a point on an upper floor is on the map: over the building it is part of.
    Vec2 surfacePos(Vec2 p) const {
        int f = floorAt(p);
        if (f < 0) return p;
        const Floor& fl = floors[f];
        return p + Vec2((float)(fl.bx - fl.x0) * 16, (float)(fl.by - fl.y0) * 16);
    }
    int cityAt(int tx, int ty) const { for (size_t i = 0; i < cities.size(); i++) if (cities[i].contains(tx, ty)) return (int)i; return -1; }
    // How good the loot is here, 0..1: better the further out, and in the cities.
    float lootQuality(int tx, int ty) const;
    // Which catacomb a point / tile is in, -1 outside.
    int dungeonAt(Vec2 p) const { return dungeonAtTile(toTile(p.x), toTile(p.y)); }
    int dungeonAtTile(int tx, int ty) const {
        if (tx < outW && ty < outH) return -1;
        for (size_t i = 0; i < dungeons.size(); i++) if (dungeons[i].contains(tx, ty)) return (int)i;
        return -1;
    }

    // Minimap image (RGBA, 1 pixel per tile).
    std::vector<uint8_t> mapPixels;
    bool mapDirty = true;

    // Flow field toward a target (used by enemy pathing).
    int flowX0 = 0, flowY0 = 0, flowSize = 0;
    std::vector<int16_t> flow;

    bool inBounds(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h; }
    Tile& at(int x, int y) { return tiles[y * w + x]; }
    const Tile& at(int x, int y) const { return tiles[y * w + x]; }
    static int toTile(float v) { return (int)std::floor(v / TILE); }
    static Vec2 tileCenter(int x, int y) { return {x * TILE + TILE * 0.5f, y * TILE + TILE * 0.5f}; }

    void generate(uint64_t seed, int day);
    void generateBase();

    bool blocksMove(int tx, int ty, bool ghost = false) const;
    bool blocksBullet(int tx, int ty) const;          // whole tile
    bool blocksBulletAt(Vec2 p) const;                // respects SolidInfo::radius
    float blockRadius(int tx, int ty) const;
    bool collides(float x, float y, float r, bool ghost = false) const;
    Vec2 move(Vec2 pos, Vec2 delta, float r, bool ghost = false) const;
    bool lineOfSight(Vec2 a, Vec2 b) const;
    // Puts furniture in every generated building (after loot and raiders are placed).
    void furnishBuildings(size_t from = 0);
    bool openDoorNear(Vec2 pos, float radius);
    // The catacomb shortcut's portcullis (see Dungeon::gateX).
    bool gateOpen(int dungeon) const;
    void openGate(int dungeon);
    int gateNear(Vec2 pos, float radius) const;     // dungeon index, -1 none
    bool closeDoorNear(Vec2 pos, float radius);

    // Returns true when the tile was destroyed.
    bool damageTile(int tx, int ty, float dmg);
    void destroyTile(int tx, int ty);

    void computeFlow(Vec2 target, int size);
    bool flowDir(Vec2 pos, Vec2& dir) const;

    void reveal(Vec2 pos, int radius);
    void updateMapPixel(int tx, int ty);
    void rebuildMap();

    int addContainer(Vec2 pos, int kind, int tx, int ty, uint8_t variant = 0);
    // A street light standing on (x, y) with its foot on the pole, arm variant 0 side,
    // 1 up, 2 down; false when the tile is taken.
    bool placeStreetLight(int x, int y, int variant, bool flip);
};

// The dressing pass (dress.cpp, 0.12v): run last by World::generate on its own dice, so
// nothing it adds moves the day's buildings, loot or raiders. Colours the vegetation in
// stands, strews the ground with detail, paints the roads, furnishes the streets, yards
// and roofs, and lets the grass creep back over the paving.
void dressWorld(World& w, uint64_t seed);
