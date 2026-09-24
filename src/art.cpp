#include "art.h"
#include "items.h"
#include "sprites.h"
#include "tile_mappings.generated.h"
#include "world.h"
#include <cstdio>

using Assets::Sprite;

namespace Art {

namespace {

std::vector<const Sprite*> g_trees, g_bushes, g_rocks, g_barrels, g_cars, g_lights, g_deco, g_wrecks;
std::vector<const Sprite*> g_doors;
std::vector<const Sprite*> g_containers[8];
std::vector<Assets::TileRef> g_fill[(int)Assets::Fill::COUNT];
const Sprite* g_bg = nullptr;      // Tiles/Background_Green_TileSet, the lush ground sheet
const Sprite* g_bgDry = nullptr;   // Background_Dark-Green, same layout, the drier biome
const Sprite* g_roof = nullptr;
const Sprite* g_stump = nullptr;
const Sprite* g_hatch = nullptr;
const Sprite* g_hatchClosed = nullptr;
const Sprite* g_fence = nullptr;
const Sprite* g_brick = nullptr;
std::vector<const Sprite*> g_buildingWalls;
const Sprite* g_wallWood = nullptr;
const Sprite* g_wallMetal = nullptr;
const Sprite* g_itemIcons[IT_COUNT] = {};
bool g_packLoaded = false;

const char* DIR_NAME[4] = {"down", "up", "side", "side-left"};

// Character sheets: [dir][anim]
const Sprite* g_shirtIdle[Assets::SHIRT_COUNT][4][2];   // shirts 1.. (0 is g_body*)
const Sprite* g_shirtRun[Assets::SHIRT_COUNT][4][2];
const Sprite* g_shirtDeath[Assets::SHIRT_COUNT][4];
const Sprite* g_bodyIdle[4][2];   // [dir][hands?]
const Sprite* g_bodyRun[4][2];
const Sprite* g_bodyDeath[4];
// The same, dyed red for raiders (see Assets::addRaw).
const Sprite* g_foeIdle[4][2];
const Sprite* g_foeRun[4][2];
const Sprite* g_foeDeath[4];
const Sprite* g_gun[4][3][3];     // [dir][weaponClass][anim: idle/shoot/reload]
const Sprite* g_helmet[4];
const Sprite* g_fire[4];
const Sprite* g_zombie[3][4][3];  // [kind][dir][anim: idle/walk/attack]
const Sprite* g_zombieDeath[3][2];  // [kind][0 side, 1 side-left]

void add(std::vector<const Sprite*>& v, const std::string& key) {
    if (const Sprite* s = Assets::find(key)) v.push_back(s);
}

const Sprite* pick(const std::vector<const Sprite*>& v, uint8_t variant) {
    if (v.empty()) return nullptr;
    return v[variant % v.size()];
}

Piece piece(const Sprite* s, int frame = 0, bool flip = false) {
    Piece p;
    p.sprite = s;
    p.frame = frame;
    p.flipX = flip;
    return p;
}

int weaponClass(int itemId) {
    switch (baseWeapon(itemId)) {
    case IT_PISTOL: case IT_REVOLVER: return 1;     // pistol art
    case IT_SHOTGUN: return 2;                      // shotgun art
    default: return 0;                              // rifle / "gun" art
    }
}

void loadCharacters() {
    const char* gunFolder[3] = {"gun", "pistol", "shotgun"};
    for (int d = 0; d < 4; d++) {
        std::string dir = DIR_NAME[d];
        g_bodyIdle[d][1] = Assets::find("character/main/idle/character_" + dir + "_idle");
        g_bodyIdle[d][0] = Assets::find("character/main/idle/character_" + dir + "_idle_no-hands");
        g_bodyRun[d][1] = Assets::find("character/main/run/character_" + dir + "_run");
        g_bodyRun[d][0] = Assets::find("character/main/run/character_" + dir + "_run_no-hands");
        g_bodyDeath[d] = Assets::find("character/main/death/character_" + dir + "_death1");
        g_foeIdle[d][1] = Assets::find("character/main_enemy/idle/character_" + dir + "_idle");
        g_foeIdle[d][0] = Assets::find("character/main_enemy/idle/character_" + dir + "_idle_no-hands");
        g_foeRun[d][1] = Assets::find("character/main_enemy/run/character_" + dir + "_run");
        g_foeRun[d][0] = Assets::find("character/main_enemy/run/character_" + dir + "_run_no-hands");
        g_foeDeath[d] = Assets::find("character/main_enemy/death/character_" + dir + "_death1");
        for (int s = 1; s < Assets::SHIRT_COUNT; s++) {
            std::string k = "character/main_c" + std::to_string(s) + "/";
            g_shirtIdle[s][d][1] = Assets::find(k + "idle/character_" + dir + "_idle");
            g_shirtIdle[s][d][0] = Assets::find(k + "idle/character_" + dir + "_idle_no-hands");
            g_shirtRun[s][d][1] = Assets::find(k + "run/character_" + dir + "_run");
            g_shirtRun[s][d][0] = Assets::find(k + "run/character_" + dir + "_run_no-hands");
            g_shirtDeath[s][d] = Assets::find(k + "death/character_" + dir + "_death1");
        }
        g_fire[d] = Assets::find("character/guns/fire/fire_" + dir);
        if (!g_fire[d]) g_fire[d] = Assets::search({"guns/fire/fire_", dir});
        std::string helmetDir = (d <= 1) ? "up-and-down" : dir;
        g_helmet[d] = Assets::search({"character/helmet/helmet_" + helmetDir, "idle-and-run"});
        for (int w = 0; w < 3; w++) {
            std::string f = gunFolder[w];
            g_gun[d][w][0] = Assets::search({"character/guns/" + f + "/", "_" + dir + "_", "idle-and-run"});
            g_gun[d][w][1] = Assets::search({"character/guns/" + f + "/", "_" + dir + "_", "shoot"});
            g_gun[d][w][2] = Assets::search({"character/guns/" + f + "/", "_" + dir + "_", "reload"});
        }
    }
    // The pack only draws people dying side-on. Facing up or down uses the side
    // sheet too, so a death always looks like the same person falling over.
    if (!g_bodyDeath[0]) g_bodyDeath[0] = g_bodyDeath[2];
    if (!g_bodyDeath[1]) g_bodyDeath[1] = g_bodyDeath[3];
    if (!g_foeDeath[0]) g_foeDeath[0] = g_foeDeath[2];
    if (!g_foeDeath[1]) g_foeDeath[1] = g_foeDeath[3];
    for (int s = 1; s < Assets::SHIRT_COUNT; s++) {
        if (!g_shirtDeath[s][0]) g_shirtDeath[s][0] = g_shirtDeath[s][2];
        if (!g_shirtDeath[s][1]) g_shirtDeath[s][1] = g_shirtDeath[s][3];
    }
    const char* zombieDir[3] = {"zombie_small", "zombie_big", "zombie_axe"};
    for (int k = 0; k < 3; k++) {
        std::string base = std::string("enemies/") + zombieDir[k] + "/";
        // Prefer the bloody second death; the axe zombie only has it without its axe.
        const char* sides[2] = {"_side_", "_side-left_"};
        for (int sd = 0; sd < 2; sd++) {
            const Sprite* s = Assets::search({base + zombieDir[k] + sides[sd], "second-death"});
            if (!s) s = Assets::search({base + zombieDir[k] + sides[sd], "first-death"});
            if (!s) s = Assets::search({base, sides[sd], "death"});
            g_zombieDeath[k][sd] = s;
        }
    }
    for (int k = 0; k < 3; k++) {
        for (int d = 0; d < 4; d++) {
            std::string base = std::string("enemies/") + zombieDir[k] + "/";
            std::string dir = std::string("_") + DIR_NAME[d] + "_";
            g_zombie[k][d][0] = Assets::search({base, dir, "idle"});
            g_zombie[k][d][1] = Assets::search({base, dir, "walk"});
            g_zombie[k][d][2] = Assets::search({base, dir, "first-attack"});
            if (!g_zombie[k][d][1]) g_zombie[k][d][1] = g_zombie[k][d][0];
            if (!g_zombie[k][d][2]) g_zombie[k][d][2] = g_zombie[k][d][1];
        }
    }
}

void loadProps() {
    const char* palettes[3] = {"green", "dark-green", "bleak-yellow"};
    for (const char* p : palettes) {
        std::string base = std::string("objects/nature/") + p + "/";
        for (int i = 1; i <= 10; i++) {
            if (const Sprite* s = Assets::search({base + "tree_" + std::to_string(i) + "_"})) g_trees.push_back(s);
        }
        for (int i = 1; i <= 2; i++)
            if (const Sprite* s = Assets::search({base + "bush_" + std::to_string(i) + "_"})) g_bushes.push_back(s);
        if (const Sprite* s = Assets::search({base, "rocks/rock-grass"})) g_rocks.push_back(s);
        for (int i = 1; i <= 5; i++)
            if (const Sprite* s = Assets::find(base + "grass_" + std::to_string(i) + "_" + p)) g_deco.push_back(s);
    }
    if (const Sprite* s = Assets::find("objects/nature/flowers_mashrooms_other-nature-stuff/tree_4_dry_green")) g_trees.push_back(s);
    for (int i = 1; i <= 7; i++)
        add(g_rocks, "objects/nature/flowers_mashrooms_other-nature-stuff/rocks/rock_" + std::to_string(i));
    const char* flowers[] = {"flowers_1", "flowers_2", "flowers_3", "flower_1", "flower_2"};
    const char* colors[] = {"blue", "purple", "red", "yellow"};
    for (const char* f : flowers)
        for (const char* c : colors)
            add(g_deco, std::string("objects/nature/flowers_mashrooms_other-nature-stuff/") + f + "_" + c);
    const char* litter[] = {"mushroom", "mushrooms_1_yellow", "mushrooms_2_red", "stick", "stick_leaves"};
    for (const char* l : litter) add(g_deco, std::string("objects/nature/flowers_mashrooms_other-nature-stuff/") + l);
    const char* junk[] = {"cardboard_1", "cardboard_2", "chips-pack_red", "chips-pack_yellow", "trash-bag_1",
                          "trash-bag_2", "gray-brick", "gray-brick_debris", "pallet_1", "pallet_2", "tire_1",
                          "traffic-cone", "iron-beam", "exhaust-pipe", "manhole", "metal-plates"};
    for (const char* j : junk) add(g_deco, std::string("objects/") + j);

    const char* barrels[] = {"barrel_blue_1", "barrel_red_1", "barrel_rust_blue_1", "barrel_rust_red_1",
                             "barrel_blue_2", "barrel_red_2"};
    for (const char* b : barrels) add(g_barrels, std::string("objects/") + b);

    const char* carSets[] = {"car_1", "car_2", "car_3", "car_4", "car_6", "car_7", "car_8"};
    const char* carColors[] = {"blue", "gray", "green", "orange", "red", "yellow"};
    for (const char* set : carSets)
        for (const char* col : carColors) {
            if (const Sprite* s = Assets::search({"objects/vehicles/normal/", std::string(set) + "_", std::string("_") + col}))
                g_cars.push_back(s);
            if (const Sprite* s = Assets::search({"objects/vehicles/rust/", std::string(set) + "_", std::string("_") + col}))
                g_cars.push_back(s);
        }
    // Wrecks from the vehicle pack (tools/cut_vehicles.py), for the city streets.
    for (const auto& kv : Assets::keysWithPrefix("vehicles/wreck/")) g_wrecks.push_back(kv);
    const char* lights[] = {"street-light_1_side", "street-light_2_up", "street-light_3_down"};
    for (const char* l : lights) add(g_lights, std::string("objects/") + l);

    // Doors: closed, open, a second closed style, its open leaf. The furniture pack's
    // (BitGlow) when it is there, else the world pack's.
    const char* doors[] = {"furniture/door_brown", "furniture/door_brown_hole", "furniture/door_dark", "furniture/door_dark_hole"};
    for (const char* d : doors) add(g_doors, d);
    if (g_doors.size() < 4) {
        g_doors.clear();
        const char* old[] = {"objects/buildings/door_1_beige", "objects/buildings/door_2_ajar_beige",
                             "objects/buildings/door_4_metal", "objects/buildings/door_2_ajar_beige"};
        for (const char* d : old) add(g_doors, d);
    }

    auto selected = [](const char* overrideKey, const char* fallback) -> const Sprite* {
        if (overrideKey && *overrideKey) {
            if (const Sprite* custom = Assets::find(overrideKey)) return custom;
        }
        return Assets::find(fallback);
    };
    g_bg = selected(TileMappings::BACKGROUND_SHEET, "tiles/background_green_tileset");
    g_bgDry = Assets::find("tiles/background_dark-green_tileset");
    if (!g_bgDry) g_bgDry = g_bg;
    g_roof = selected(TileMappings::ROOF_SHEET, "tiles/roof_tileset");
    g_stump = Assets::find("objects/nature/flowers_mashrooms_other-nature-stuff/stump_1");
    g_hatch = Assets::find("objects/buildings/hatch_1_open");
    g_hatchClosed = Assets::find("objects/buildings/hatch_1_closed");
    g_fence = selected(TileMappings::FENCE_SHEET, "tiles/wire-fence/wire-fence_tileset");
    g_brick = selected(TileMappings::BRICK_SHEET, "tiles/brick-wall_tileset");
    if (TileMappings::BUILDING_SHEET[0]) {
        if (const Sprite* custom = Assets::find(TileMappings::BUILDING_SHEET)) g_buildingWalls.push_back(custom);
    }
    if (g_buildingWalls.empty()) {
        for (const char* color : {"beige", "gray", "white", "dark"})
            add(g_buildingWalls, std::string("tiles/buildings/buildings_") + color + "_tileset");
    }
    g_wallWood = Assets::search({"buildable/wooden/wooden-wall_middle"});
    g_wallMetal = Assets::search({"buildable/reinforced/reinforced_wooden-wall_middle"});

    // Loot containers by kind (see LootKind order used by the world).
    const char* crates[] = {"objects/pickable/ammo-crate_green", "objects/pickable/ammo-crate_blue", "objects/pallet_1"};
    for (const char* c : crates) add(g_containers[1], c);
    const char* lockers[] = {"objects/refrigerator", "objects/vending-machine_blue", "objects/vending-machine_red"};
    for (const char* c : lockers) add(g_containers[2], c);
    const char* cabinets[] = {"objects/washing-machine", "objects/garbage-bin_1", "objects/garbage-bin_2",
                              "objects/garbage-bin_3", "objects/garbage-bin_4"};
    for (const char* c : cabinets) add(g_containers[3], c);
    const char* military[] = {"objects/container/container_1_gray_vertical", "objects/container/container_5_red_vertical",
                              "objects/container/container_9_green_vertical"};
    for (const char* c : military) add(g_containers[4], c);
    const char* toolbox[] = {"objects/trash-can_1", "objects/trash-can_2", "objects/shopping-cart"};
    for (const char* c : toolbox) add(g_containers[5], c);
    const char* bags[] = {"objects/trash-bag_1", "objects/trash-bag_2", "objects/cardboard_1"};
    for (const char* c : bags) add(g_containers[6], c);
}

// Keep each ground type on one source tileset so the terrain reads as one place.
void buildFills() {
    const Sprite* bg = Assets::find("tiles/background_green_tileset");
    for (int c = 0; c < (int)Assets::Fill::COUNT; c++) {
        const auto& all = Assets::fills((Assets::Fill)c);
        const Sprite* preferred = nullptr;
        if (c == (int)Assets::Fill::Grass || c == (int)Assets::Fill::Asphalt ||
            c == (int)Assets::Fill::Soil || c == (int)Assets::Fill::Stone)
            preferred = bg;
        for (const auto& ref : all) {
            if (preferred && ref.sprite != preferred) continue;
            g_fill[c].push_back(ref);
            if (g_fill[c].size() >= 6) break;
        }
        if (g_fill[c].empty())
            for (const auto& ref : all) {
                g_fill[c].push_back(ref);
                if (g_fill[c].size() >= 6) break;
            }
    }
}

void loadIcons() {
    auto set = [&](int item, const std::string& key) {
        if (const Sprite* s = Assets::find("ui/inventory/objects/" + key)) g_itemIcons[item] = s;
    };
    set(IT_BANDAGE, "icon_bandage");
    set(IT_MEDKIT, "icon_first-aid-kit_red");
    set(IT_MEDSUP, "icon_first-aid-kit_white");
    set(IT_FOOD, "icon_canned-food");
    set(IT_PISTOL, "icon_pistol");
    set(IT_REVOLVER, "icon_pistol");
    set(IT_SMG, "icon_gun");
    set(IT_RIFLE, "icon_gun");
    set(IT_CARBINE, "icon_gun");
    set(IT_SNIPER, "icon_gun");
    set(IT_LAUNCHER, "icon_gun");
    set(IT_SHOTGUN, "icon_shotgun");
    // Elite guns: their own art (assets/MoreWeapons, by greenpixels).
    auto setElite = [&](int item, const char* key) {
        if (const Sprite* s = Assets::find(std::string("moreweapons/") + key)) g_itemIcons[item] = s;
        else g_itemIcons[item] = g_itemIcons[baseWeapon(item)];
    };
    setElite(IT_M92, "m92");
    setElite(IT_LUGER, "luger");
    setElite(IT_MAGNUM, "revolver");
    setElite(IT_MP5, "mp5");
    setElite(IT_M15, "m15");
    setElite(IT_AK47, "ak47");
    setElite(IT_M24, "m24");
    set(IT_AMMO_LIGHT, "icon_bullet-box_blue");
    set(IT_AMMO_SHELL, "icon_bullet-box_red");
    set(IT_AMMO_RIFLE, "icon_bullet-box_green");
    set(IT_AMMO_SNIPER, "icon_bullet-crate_blue");
    set(IT_ROCKET, "icon_bullet-crate_red");
    set(IT_SCRAP, "icon_rock");
}

}  // namespace

void init() {
    buildFills();
    loadCharacters();
    loadProps();
    loadIcons();
    g_packLoaded = !g_trees.empty() || g_bodyIdle[0][1] != nullptr;
    std::fprintf(stderr, "[art] trees %zu cars %zu rocks %zu deco %zu pack=%d\n", g_trees.size(), g_cars.size(),
                 g_rocks.size(), g_deco.size(), (int)g_packLoaded);
    for (int d = 0; d < 4; d++)
        std::fprintf(stderr, "[art] dir %s: idle %d idleNoHands %d run %d gun %d pistol %d shotgun %d helmet %d zombie %d\n",
                     DIR_NAME[d], g_bodyIdle[d][1] != nullptr, g_bodyIdle[d][0] != nullptr, g_bodyRun[d][1] != nullptr,
                     g_gun[d][0][0] != nullptr, g_gun[d][1][0] != nullptr, g_gun[d][2][0] != nullptr,
                     g_helmet[d] != nullptr, g_zombie[0][d][1] != nullptr);
}

bool packLoaded() { return g_packLoaded; }

Dir dirFromAngle(float angle) {
    float a = std::fmod(angle + 2 * PI, 2 * PI);
    if (a < PI * 0.25f || a >= PI * 1.75f) return Dir::Right;
    if (a < PI * 0.75f) return Dir::Down;
    if (a < PI * 1.25f) return Dir::Left;
    return Dir::Up;
}

// ---------------------------------------------------------------- terrain
// The pack's Background_*_TileSet sheets share one layout, so ground tiles are
// addressed by frame index rather than guessed from colour. Guessing picked up
// sidewalk stripes and wood floors as though they were fields, which is what made
// the terrain look like wallpaper with chequered seams.
//
// Frame = row * 24 + column in Tiles/Background_Green_TileSet.png.
namespace {

const int* F_GRASS = TileMappings::GRASS;               // plain field, sparse tufts
const int* F_GRASS_DETAIL = TileMappings::GRASS_DETAIL; // denser clumps, used sparingly
const int F_DIRT[] = {TileMappings::DIRT};               // bare brown earth
const int* F_ROAD = TileMappings::ROAD;                  // plain asphalt

// Corner autotiles joining grass to bare earth: the 3x3 ring at columns 6-8,
// rows 12-14, plus the inner corners at columns 8-9, rows 15-16. Index is the
// corner mask, bit 1 = top-left, 2 = top-right, 4 = bottom-left, 8 = bottom-right,
// a set bit meaning that corner is earth. Masks 6 and 9 are the two diagonals,
// which the sheet does not draw; they borrow the nearest three-corner tile so the
// seam still closes.
const int* W_GRASS_DIRT = TileMappings::GRASS_DIRT;

// Which surface wins when four tiles meet at one corner: man-made over bare earth
// over grass, so paths and roads keep a continuous edge.
int groundPriority(int g) {
    switch (g) {
    case G_GRASS: return 0;
    case G_WATER: return 1;
    case G_SAND: return 2;
    case G_DIRT: return 3;
    case G_RUBBLE: return 4;
    case G_ROAD:
    case G_BRIDGE:
    case G_PAVEMENT: return 5;
    case G_FLOOR_WOOD:
    case G_FLOOR_CONCRETE:
    case G_FLOOR_TILE:
    case G_BASE_FLOOR: return 6;
    }
    return 0;
}

// Each background sheet carries its own grass, its own earth and its own seam
// art, so a seam is always drawn from the same sheet as the ground it joins.
const Sprite* vegSheet(int groundType) {
    if (groundType == G_SAND && g_bgDry && g_bgDry->valid()) return g_bgDry;
    return g_bg;
}

Assets::TileRef bgTile(const Sprite* sheet, int frame) {
    Assets::TileRef r;
    if (sheet && sheet->valid() && frame >= 0 && frame < sheet->frameCount()) {
        r.sprite = sheet;
        r.frame = frame;
    }
    return r;
}

// The hand-picked fill for an outdoor ground type; invalid for anything else, so
// interiors and rubble keep coming from the colour-sorted Fill buckets.
Assets::TileRef handFill(int groundType, uint8_t variant) {
    if (!g_bg || !g_bg->valid()) return Assets::TileRef();
    const int* set = nullptr;
    int n = 0;
    switch (groundType) {
    case G_GRASS:
    case G_SAND: set = F_GRASS; n = 5; break;      // lush or dry, same frames, other sheet
    case G_DIRT: return Assets::TileRef();          // see ground(): stitched fills that match the edges
    case G_ROAD:
    case G_BRIDGE: set = F_ROAD; n = 2; break;
    default: return Assets::TileRef();
    }
    // Mostly the first tile. Scattering the rest stops a large field reading as one
    // flat colour without turning it into a chequerboard.
    int frame = set[0];
    if (n > 1 && variant % 5 == 1) frame = set[1 + (variant / 5) % (n - 1)];
    if ((groundType == G_GRASS || groundType == G_SAND) && variant % 23 == 7)
        frame = F_GRASS_DETAIL[(variant / 23) % 3];
    return bgTile(vegSheet(groundType), frame);
}

}  // namespace

Assets::TileRef ground(int groundType, uint8_t variant) {
    using Assets::Fill;
    if (Assets::TileRef hand = handFill(groundType, variant); hand.valid()) return hand;
    auto pickFill = [&](Fill f, int offset, int spread) -> Assets::TileRef {
        const auto& v = g_fill[(int)f];
        if (v.empty()) return Assets::TileRef();
        int n = std::min((int)v.size(), std::max(1, spread));
        // Mostly the flattest tile so terrain does not look like a chequerboard.
        int idx = (n > 1 && variant % 6 == 0) ? (offset + 1 + variant % (n - 1)) : offset;
        return v[idx % v.size()];
    };
    auto buildingFloor = [&](int which) -> Assets::TileRef {
        Assets::TileRef ref;
        const Sprite* s = pick(g_buildingWalls, variant);
        if (!s || !s->valid()) return ref;
        ref.sprite = s;
        ref.frame = std::clamp(TileMappings::BUILDING_FLOOR[which], 0, s->frameCount() - 1);
        return ref;
    };
    auto mappedBackground = [&](const Sprite* sheet, int frame) -> Assets::TileRef {
        Assets::TileRef ref;
        if (!sheet || !sheet->valid()) return ref;
        ref.sprite = sheet;
        ref.frame = std::clamp(frame, 0, sheet->frameCount() - 1);
        return ref;
    };
    switch (groundType) {
    case G_GRASS: {
        int frame = TileMappings::GRASS[variant % 5];
        return mappedBackground(g_bg, frame).valid() ? mappedBackground(g_bg, frame) : pickFill(Fill::Grass, 0, 3);
    }
    case G_DIRT: {
        // Plain earth stitched from the grass/dirt edge pieces themselves
        // (tools/make_ground_fills.py): the sheet's own dirt (TileMappings::DIRT) is a
        // darker earth that left a visible seam inside every edge.
        static const Sprite* fills[3] = {Assets::find("tileoverrides/dirt_fill_0"), Assets::find("tileoverrides/dirt_fill_1"),
                                         Assets::find("tileoverrides/dirt_fill_2")};
        if (const Sprite* f = fills[variant % 3]) { Assets::TileRef r; r.sprite = f; r.frame = 0; return r; }
        return mappedBackground(g_bg, TileMappings::DIRT).valid() ? mappedBackground(g_bg, TileMappings::DIRT) : pickFill(Fill::Soil, 0, 2);
    }
    case G_SAND: {
        int frame = TileMappings::GRASS[variant % 5];
        return mappedBackground(g_bgDry, frame).valid() ? mappedBackground(g_bgDry, frame) : pickFill(Fill::Stone, 0, 2);
    }
    case G_ROAD: {
        int frame = TileMappings::ROAD[variant & 1];
        return mappedBackground(g_bg, frame).valid() ? mappedBackground(g_bg, frame) : pickFill(Fill::Asphalt, 0, 2);
    }
    case G_BRIDGE: return pickFill(Fill::Asphalt, 1, 1);
    case G_WATER: return pickFill(Fill::Asphalt, 3, 1);
    case G_FLOOR_WOOD: if (auto r = buildingFloor(0); r.valid()) return r; return pickFill(Fill::Interior, 0, 2);
    case G_FLOOR_CONCRETE: if (auto r = buildingFloor(1); r.valid()) return r; return pickFill(Fill::Interior, 2, 2);
    case G_FLOOR_TILE: if (auto r = buildingFloor(2); r.valid()) return r; return pickFill(Fill::Interior, 4, 2);
    case G_RUBBLE: return pickFill(Fill::Garbage, 0, 4);
    case G_BASE_FLOOR: if (auto r = buildingFloor(3); r.valid()) return r; return pickFill(Fill::Interior, 1, 1);
    case G_PAVEMENT: {
        // City paving (0.11v): plain pavers (tools/make_pavement.py), a manhole now and then.
        static const Sprite* pave[2] = {Assets::find("tileoverrides/pavement_0"), Assets::find("tileoverrides/pavement_1")};
        if (variant % 251 == 7 && mappedBackground(g_bg, 33).valid()) return mappedBackground(g_bg, 33);
        if (const Sprite* s = pave[(variant / 7) % 3 == 0 ? 1 : 0]) { Assets::TileRef r; r.sprite = s; r.frame = 0; return r; }
        return pickFill(Fill::Stone, 0, 2);
    }
    }
    return Assets::TileRef();
}

// Asphalt with its shoulder: the sheet draws the dark worn edge along each side and
// round each outer corner, for a road any number of tiles wide. Frames in
// Background_Green_TileSet: 168 172 173 / 192 221 197 / 265 241 245.
static int roadFrame(bool n, bool s, bool wv, bool e) {
    if (n && s && wv && e) return 221;          // middle of the road
    if (!n && s && wv && e) return 172;         // top edge
    if (n && !s && wv && e) return 241;         // bottom edge
    if (n && s && !wv && e) return 192;         // left edge
    if (n && s && wv && !e) return 197;         // right edge
    if (!n && !wv && s && e) return 168;        // top-left corner
    if (!n && !e && s && wv) return 173;        // top-right corner
    if (!s && !wv && n && e) return 265;        // bottom-left corner
    if (!s && !e && n && wv) return 245;        // bottom-right corner
    return -1;                                  // a strip one tile wide, or a stub end
}

bool roadEdgeTile(const ::World& w, int x, int y) {
    auto road = [&](int tx, int ty) { return w.inBounds(tx, ty) && (w.at(tx, ty).ground == G_ROAD || w.at(tx, ty).ground == G_BRIDGE); };
    return roadFrame(road(x, y - 1), road(x, y + 1), road(x - 1, y), road(x + 1, y)) >= 0;
}

Assets::TileRef groundTile(const ::World& w, int x, int y, float time) {
    const Tile& self = w.at(x, y);
    if (self.ground == G_WATER)   // animated, never autotiled
        return ground(self.ground, (uint8_t)((int)(time * 1.5f) + x + y));
    if (self.ground == G_ROAD && g_bg && g_bg->valid()) {
        auto road = [&](int tx, int ty) { return w.inBounds(tx, ty) && (w.at(tx, ty).ground == G_ROAD || w.at(tx, ty).ground == G_BRIDGE); };
        int f = roadFrame(road(x, y - 1), road(x, y + 1), road(x - 1, y), road(x + 1, y));
        if (f >= 0) return bgTile(g_bg, f);
    }

    // Grass against bare earth, and lush grass against dry grass, are joined with the
    // sheets' corner rings; anything else meets with a plain edge.
    bool vegetation = self.ground == G_GRASS || self.ground == G_SAND;
    if (g_bg && g_bg->valid() && (vegetation || self.ground == G_DIRT)) {
        auto groundAt = [&](int tx, int ty) -> int {
            tx = std::clamp(tx, 0, w.w - 1);
            ty = std::clamp(ty, 0, w.h - 1);
            return w.at(tx, ty).ground;
        };
        // Corner c is shared by four tiles; the highest priority surface there wins.
        static const int CX[4][4] = {{-1, 0, -1, 0}, {0, 1, 0, 1}, {-1, 0, -1, 0}, {0, 1, 0, 1}};
        static const int CY[4][4] = {{-1, -1, 0, 0}, {-1, -1, 0, 0}, {0, 0, 1, 1}, {0, 0, 1, 1}};
        int corner[4];
        bool dirt = false, lush = false, dry = false, other = false;
        for (int c = 0; c < 4; c++) {
            int best = self.ground;
            for (int k = 0; k < 4; k++) {
                int g = groundAt(x + CX[c][k], y + CY[c][k]);
                if (groundPriority(g) > groundPriority(best)) best = g;
            }
            corner[c] = best;
            dirt |= best == G_DIRT;
            lush |= best == G_GRASS;
            dry |= best == G_SAND;
            other |= best != G_DIRT && best != G_GRASS && best != G_SAND;
        }
        if (!other && dirt) {
            // Grass (lush or dry) against bare earth: that grass's sheet has the ring.
            // Where earth and both grasses meet in one tile, the tile's own grass
            // takes the earth edge (the earth seam is the one that shows).
            int mask = 0;
            for (int c = 0; c < 4; c++) if (corner[c] == G_DIRT) mask |= 1 << c;
            int veg = lush && dry ? (self.ground == G_SAND ? G_SAND : G_GRASS) : lush ? G_GRASS : dry ? G_SAND : -1;
            if (mask == 15) return ground(G_DIRT, self.variant);   // grass swallowed by earth on every corner
            if (veg >= 0) {
                Assets::TileRef ref = bgTile(vegSheet(veg), W_GRASS_DIRT[mask]);
                if (ref.valid()) return ref;
            }
        } else if (!other && !dirt && lush && dry) {
            // Lush grass against the drier grass: the green sheet draws that edge too,
            // as a second ring laid out like the earth one (frames +3, inner corners +2),
            // whose dark grass is exactly the dry sheet's. No more square patches.
            int mask = 0;
            for (int c = 0; c < 4; c++) if (corner[c] == G_SAND) mask |= 1 << c;
            if (mask == 15) return ground(G_SAND, self.variant);
            int f = W_GRASS_DIRT[mask];
            if (f >= 0) {
                f += (f == 368 || f == 369 || f == 392 || f == 393) ? 2 : 3;
                Assets::TileRef ref = bgTile(g_bg, f);
                if (ref.valid()) return ref;
            }
        }
    }
    return ground(self.ground, self.variant);
}


Piece wallTile(int solidType, uint8_t variant, int neighbourMask) {
    auto connectedFrame = [](const Sprite* s, int mask) {
        if (!s || !s->valid()) return 0;
        // The intact facade occupies columns 4..9 / rows 0..5 of Buildings_*.
        // Select its real directional edges and four authored corners when this is
        // a generated building perimeter (high bits supplied by drawTileSolids).
        const int LEFT = 16, RIGHT = 32, TOP = 64, BOTTOM = 128;
        if ((mask & (LEFT | TOP)) == (LEFT | TOP)) return std::min(TileMappings::BUILDING_FACADE[0], s->frameCount() - 1);
        if ((mask & (RIGHT | TOP)) == (RIGHT | TOP)) return std::min(TileMappings::BUILDING_FACADE[2], s->frameCount() - 1);
        if ((mask & (LEFT | BOTTOM)) == (LEFT | BOTTOM)) return std::min(TileMappings::BUILDING_FACADE[5], s->frameCount() - 1);
        if ((mask & (RIGHT | BOTTOM)) == (RIGHT | BOTTOM)) return std::min(TileMappings::BUILDING_FACADE[7], s->frameCount() - 1);
        if (mask & TOP) return std::min(TileMappings::BUILDING_FACADE[1], s->frameCount() - 1);
        if (mask & BOTTOM) return std::min(TileMappings::BUILDING_FACADE[6], s->frameCount() - 1);
        if (mask & LEFT) return std::min(TileMappings::BUILDING_FACADE[3], s->frameCount() - 1);
        if (mask & RIGHT) return std::min(TileMappings::BUILDING_FACADE[4], s->frameCount() - 1);

        // Interior partitions do not have a perimeter side; use the corresponding
        // authored straight/corner segment instead of a random tile.
        return std::min(TileMappings::BUILDING_CONNECTED[mask & 15], (int)s->frames.size() - 1);
    };
    switch (solidType) {
    case S_WALL_WOOD:
    case S_WALL_CONCRETE:
        if (!g_buildingWalls.empty()) {
            const Sprite* s = pick(g_buildingWalls, variant);
            return piece(s, connectedFrame(s, neighbourMask));
        }
        return piece(solidType == S_WALL_WOOD ? g_wallWood : g_wallMetal);
    case S_BUNKER: return piece(g_wallMetal);
    case S_FENCE:
        if (g_fence && g_fence->valid()) {
            return piece(g_fence, std::min(TileMappings::FENCE_CONNECTED[neighbourMask & 15], (int)g_fence->frames.size() - 1));
        }
        return Piece();
    default: break;
    }
    if (g_brick && g_brick->valid()) {
        Piece p = piece(g_brick, TileMappings::BRICK_CONNECTED[neighbourMask & 15]);
        p.frame = std::min(p.frame, (int)g_brick->frames.size() - 1);
        return p;
    }
    const auto& bricks = g_fill[(int)Assets::Fill::Brick];
    if (!bricks.empty()) {
        const Assets::TileRef& t = bricks[variant % bricks.size()];
        Piece p;
        p.sprite = t.sprite;
        // The brick sheet includes capped ends and corners. Preserve the chosen
        // palette sheet but select its connected piece from the neighbour mask.
        int mask = neighbourMask & 15;
        p.frame = TileMappings::BRICK_CONNECTED[mask];
        p.frame = std::min(p.frame, (int)t.sprite->frames.size() - 1);
        return p;
    }
    return Piece();
}

Assets::TileRef roofTile(uint8_t style, int col, int row) {
    Assets::TileRef ref;
    if (!g_roof || !g_roof->valid() || g_roof->cols < 3) return ref;
    ref.sprite = g_roof;
    ref.frame = std::clamp(TileMappings::ROOF[style % 2][std::clamp(row, 0, 4)][std::clamp(col, 0, 2)],
                           0, g_roof->frameCount() - 1);
    return ref;
}

Piece door(uint8_t v) { return piece(pick(g_doors, v)); }
// A slow wind wave rolls across the map, so neighbouring trees lean together but
// not in lockstep; a quicker flutter on top keeps the leaves alive between gusts.
static float windAt(uint8_t v, float time, float speed) {
    float phase = (v % 13) * 0.48f;
    float wave = std::sin(time * 0.9f * speed + phase);
    float gust = std::sin(time * 0.37f + phase * 0.3f) * 0.5f + 0.5f;     // 0..1, strength
    float flutter = std::sin(time * 3.1f * speed + phase * 2.7f) * 0.35f;
    return wave * (0.6f + gust * 0.8f) + flutter;
}

Piece tree(uint8_t v, float time) {
    Piece p = piece(pick(g_trees, v));
    if (p.valid() && time > 0) p.sway = windAt(v, time, 0.9f + (v % 5) * 0.06f) * 1.6f;
    return p;
}
Piece bush(uint8_t v, float time) {
    Piece p = piece(pick(g_bushes, v));
    if (p.valid() && time > 0) p.sway = windAt((uint8_t)(v * 7 + 3), time, 1.3f) * 0.9f;
    return p;
}
Piece rock(uint8_t v) { return piece(pick(g_rocks, v)); }
Piece barrel(uint8_t v) { return piece(pick(g_barrels, v)); }
Piece car(uint8_t v) { return piece(pick(g_cars, v)); }
Piece wreck(uint8_t v, int dir) {
    const Sprite* s = pick(g_wrecks, v);
    return s ? piece(s, ((dir % 8) + 8) % 8) : Piece();
}
int wreckCount() { return (int)g_wrecks.size(); }
Piece streetLight(uint8_t v) { return piece(pick(g_lights, v)); }
Piece groundDeco(uint8_t v) { return piece(pick(g_deco, v)); }
Piece stump() { return piece(g_stump); }
Piece hatch(bool closed) { return piece(closed && g_hatchClosed ? g_hatchClosed : g_hatch); }

Piece containerArt(int kind, uint8_t variant) {
    int k = std::clamp(kind, 0, 7);
    const Sprite* s = pick(g_containers[k], variant);
    if (!s) s = pick(g_containers[1], variant);
    return piece(s);
}

// Bodies left by people are always the pack's human death art, lying on one side
// or the other: the last frame of character_side(-left)_death1.
Piece corpse(uint8_t variant) {
    // Bit 0x80 marks a raider's body, drawn in their red.
    const Sprite* s = (variant & 0x80) ? g_foeDeath[(variant & 1) ? 3 : 2] : nullptr;
    if (!s) s = g_bodyDeath[(variant & 1) ? 3 : 2];
    if (!s) s = g_bodyDeath[(variant & 1) ? 2 : 3];
    if (!s) return Piece();
    return piece(s, s->frameCount() - 1);
}

Piece zombieDeath(int kind, bool left, int frame) {
    int k = std::clamp(kind, 0, 2);
    const Sprite* s = g_zombieDeath[k][left ? 1 : 0];
    bool flip = false;
    if (!s) { s = g_zombieDeath[k][left ? 0 : 1]; flip = true; }
    if (!s) return Piece();
    return piece(s, std::clamp(frame, 0, s->frameCount() - 1), flip);
}

Piece furniture(int which) {
    static const char* keys[] = {"objects/bench_1_down", "objects/table", "objects/refrigerator",
                                 "objects/washing-machine", "objects/vending-machine_blue"};
    int n = (int)(sizeof(keys) / sizeof(keys[0]));
    return piece(Assets::find(keys[std::clamp(which, 0, n - 1)]));
}

Piece humanBody(Dir d, Anim a, int frame, bool holdingGun, bool enemy, int shirt) {
    int di = (int)d;
    int hands = holdingGun ? 0 : 1;
    const Sprite* s = nullptr;
    if (enemy) {
        if (a == Anim::Death) s = g_foeDeath[di];
        else if (a == Anim::Run) s = g_foeRun[di][hands];
        else s = g_foeIdle[di][hands];
    } else if (shirt > 0 && shirt < Assets::SHIRT_COUNT) {
        if (a == Anim::Death) s = g_shirtDeath[shirt][di];
        else if (a == Anim::Run) s = g_shirtRun[shirt][di][hands];
        else s = g_shirtIdle[shirt][di][hands];
    }
    if (!s) {
        if (a == Anim::Death) s = g_bodyDeath[di];
        else if (a == Anim::Run) s = g_bodyRun[di][hands];
        else s = g_bodyIdle[di][hands];
    }
    if (!s) s = g_bodyIdle[di][1];
    // A death plays once and stays on its last frame (lying on the ground); sprite
    // frames otherwise wrap round, which made the fall loop.
    if (a == Anim::Death && s && s->valid()) frame = std::clamp(frame, 0, s->frameCount() - 1);
    return piece(s, frame);
}

Piece humanGun(Dir d, Anim a, int frame, int weaponItem) {
    if (weaponItem == IT_NONE) return Piece();
    int di = (int)d, wc = weaponClass(weaponItem);
    int ai = a == Anim::Shoot ? 1 : a == Anim::Reload ? 2 : 0;
    const Sprite* s = g_gun[di][wc][ai];
    if (!s) s = g_gun[di][wc][0];
    if (!s) s = g_gun[di][0][0];
    return piece(s, frame);
}

Piece helmet(Dir d, int frame) { return piece(g_helmet[(int)d], frame); }

Piece zombie(int kind, Dir d, Anim a, int frame) {
    int k = std::clamp(kind, 0, 2), di = (int)d;
    int ai = a == Anim::Attack ? 2 : (a == Anim::Walk || a == Anim::Run) ? 1 : 0;
    return piece(g_zombie[k][di][ai], frame);
}

Piece muzzleFlash(Dir d, int frame) { return piece(g_fire[(int)d], frame); }

Piece bulletSprite(int weaponItem) {
    const char* key = weaponItem == IT_SHOTGUN ? "character/guns/bullets/shotgun-bullet"
                     : weaponItem == IT_PISTOL ? "character/guns/bullets/pistol-bullet_bullet"
                                               : "character/guns/bullets/gun-bullet_bullet";
    return piece(Assets::find(key));
}

const Assets::Sprite* itemIcon(int itemId) {
    if (itemId <= IT_NONE || itemId >= IT_COUNT) return nullptr;
    return g_itemIcons[itemId];
}

Piece uiPiece(const char* name) { return piece(Assets::find(std::string("ui/") + name)); }

}  // namespace Art
