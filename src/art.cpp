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
// 0.12v: the same trees and bushes grouped by colouring (Tone), overgrown cars by grass,
// and the flat details by kind (DecoKind).
std::vector<const Sprite*> g_treeTone[TONE_COUNT], g_bushTone[TONE_COUNT], g_carTone[4];
std::vector<const Sprite*> g_decoKind[8], g_tuft[4], g_moss[4], g_mossLight[4];
const Sprite* g_tuftStep[4][5] = {};   // 0.12v: grass 3-5 trodden flat (their Sheet2's second frame)
struct ObjArt { std::vector<Piece> plain, grown[3]; };
ObjArt g_obj[OB_COUNT];
std::vector<Assets::TileRef> g_overlay;
const Sprite* g_bgWaste = nullptr;  // Background_Bleak-Yellow, the dead scrub (G_WASTE)
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
// 0.12v: the rest of the character pack. Sets: 0 you, 1 raiders, 2.. the shirts.
constexpr int HUMAN_SETS = Assets::SHIRT_COUNT + 1;
const Sprite* g_humanPunch[HUMAN_SETS][4][2];
const Sprite* g_humanPick[HUMAN_SETS][4][2];
const Sprite* g_humanFall[HUMAN_SETS][4][3];   // death1..3 (side sheets)
const Sprite* g_gunRack[4];                     // the shotgun's pump after a shot
const Sprite* g_helmetAnim[4][3];               // punch, pick-up, death
const Sprite* g_bat[4][2];                      // idle-and-run, attack
const Sprite* g_zombieAlt[3][4];                // second attack
const Sprite* g_zombieFall[3][2][2];            // [kind][side][first/second death]
const Sprite* g_axeless[4][4];                  // the axe zombie without it: [dir][idle/walk/attack/taking]
const Sprite* g_axelessFall[2][2];
const Sprite* g_axe[4][3];                      // [dir][thrown/landing/landed]

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
    // Punch, pick-up and the three falls, for you, the raiders and every shirt.
    for (int set = 0; set < HUMAN_SETS; set++) {
        std::string k = set == 0 ? "character/main/" : set == 1 ? "character/main_enemy/" : "character/main_c" + std::to_string(set - 1) + "/";
        for (int d = 0; d < 4; d++) {
            std::string dir = DIR_NAME[d];
            g_humanPunch[set][d][1] = Assets::find(k + "punch/character_" + dir + "_punch");
            g_humanPunch[set][d][0] = Assets::find(k + "punch/character_" + dir + "_punch_no-hands");
            g_humanPick[set][d][1] = Assets::find(k + "pick-up/character_" + dir + "_pick-up");
            g_humanPick[set][d][0] = Assets::find(k + "pick-up/character_" + dir + "_pick-up_nohands");
            for (int f = 0; f < 3; f++) g_humanFall[set][d][f] = Assets::find(k + "death/character_" + dir + "_death" + std::to_string(f + 1));
        }
        for (int f = 0; f < 3; f++) {   // side-on only: up and down fall as the sides do
            if (!g_humanFall[set][0][f]) g_humanFall[set][0][f] = g_humanFall[set][2][f];
            if (!g_humanFall[set][1][f]) g_humanFall[set][1][f] = g_humanFall[set][3][f];
        }
    }
    for (int d = 0; d < 4; d++) {
        std::string dir = DIR_NAME[d];
        g_gunRack[d] = Assets::search({"character/guns/shotgun/", "_" + dir + "_", "racking"});
        std::string hd = d <= 1 ? "up-and-down" : dir;
        g_helmetAnim[d][0] = Assets::search({"character/helmet/helmet_" + hd, "punch"});
        g_helmetAnim[d][1] = Assets::search({"character/helmet/helmet_" + hd, "pick-up"});
        g_helmetAnim[d][2] = Assets::search({"character/helmet/helmet_" + (d == 3 ? std::string("side-left") : std::string("side")), "death"});
        g_bat[d][0] = Assets::search({"character/bat/bat_" + (d == 3 ? std::string("side-left") : dir), "idle-and-run"});
        g_bat[d][1] = Assets::search({"character/bat/bat_" + (d == 3 ? std::string("side-left") : dir), "attack"});
        const char* ax[4] = {"idle", "first-attack", "first-attack", "taking-axe"};
        for (int a = 0; a < 4; a++)
            g_axeless[d][a] = Assets::search({"enemies/zombie_axe/no-axe/zombie_axe_no-axe_" + (d == 3 ? std::string("side-left") : dir) + "_", ax[a]});
        std::string adir = d == 3 ? "side-left" : dir;
        g_axe[d][0] = d <= 1 ? Assets::find("enemies/zombie_axe/axe/axe_vertical_thrown") : Assets::find("enemies/zombie_axe/axe/axe_" + adir + "_thrown");
        g_axe[d][1] = Assets::find("enemies/zombie_axe/axe/axe_" + adir + "_landing");
        g_axe[d][2] = Assets::find("enemies/zombie_axe/axe/axe_" + adir + "_landed");
    }
    // The empty-handed axe zombie has no walk: it lurches along on its idle.
    for (int d = 0; d < 4; d++) {
        if (!g_axeless[d][0]) g_axeless[d][0] = g_axeless[d == 1 ? 0 : 2][0];
        if (!g_axeless[d][1]) g_axeless[d][1] = g_axeless[d][0];
        if (!g_axeless[d][2]) g_axeless[d][2] = g_axeless[2][2] ? g_axeless[2][2] : g_axeless[d][0];
        if (!g_axeless[d][3]) g_axeless[d][3] = g_axeless[d == 1 ? 0 : 2][3];
    }
    for (int sd = 0; sd < 2; sd++)
        for (int f = 0; f < 2; f++)
            g_axelessFall[sd][f] = Assets::search({"enemies/zombie_axe/no-axe/zombie_axe_no-axe", sd ? "_side-left_" : "_side_", f ? "second-death" : "first-death"});
    const char* zombieDir[3] = {"zombie_small", "zombie_big", "zombie_axe"};
    for (int k = 0; k < 3; k++) {
        std::string base = std::string("enemies/") + zombieDir[k] + "/";
        for (int d = 0; d < 4; d++) g_zombieAlt[k][d] = Assets::search({base, std::string("_") + DIR_NAME[d] + "_", "second-attack"});
        for (int sd = 0; sd < 2; sd++)
            for (int f = 0; f < 2; f++)
                g_zombieFall[k][sd][f] = Assets::search({base + zombieDir[k] + (sd ? "_side-left_" : "_side_"), f ? "second-death" : "first-death"});
    }
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
    const char* carColors[] = {"blue", "gray", "green", "orange", "red", "yellow", "light-green", "dark-blue"};   // the van and bus come in light green, rust in dark blue
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
    const char* crates[] = {"objects/pickable/ammo-crate_green", "objects/pickable/ammo-crate_blue", "objects/pickable/ammo-crate_red", "objects/pallet_1"};
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

// 0.12v: everything the dressing pass draws on. The pack colours its nature in six
// palettes and draws most street furniture three more times, overgrown with each grass.
const char* const GROWN[3] = {"green", "dark-green", "bleak-yellow"};

void loadDressing() {
    const char* tonePal[TONE_COUNT] = {nullptr, "green", "dark-green", "bleak-yellow", "orange", "yellow", "red"};
    for (int t = 1; t < TONE_COUNT; t++) {
        std::string base = std::string("objects/nature/") + tonePal[t] + "/";
        for (int i = 1; i <= 10; i++)
            if (const Sprite* s = Assets::search({base + "tree_" + std::to_string(i) + "_"})) g_treeTone[t].push_back(s);
        for (int i = 1; i <= 2; i++)
            if (const Sprite* s = Assets::search({base + "bush_" + std::to_string(i) + "_"})) g_bushTone[t].push_back(s);
    }
    g_treeTone[0] = g_trees;
    g_bushTone[0] = g_bushes;
    // A dead tree now and then among the green ones.
    if (const Sprite* s = Assets::find("objects/nature/flowers_mashrooms_other-nature-stuff/tree_4_dry_green")) g_treeTone[TONE_BLEAK].push_back(s);

    g_carTone[0] = g_cars;
    for (int g = 0; g < 3; g++)
        for (const Sprite* s : Assets::keysWithPrefix("objects/vehicles/overgrown/")) {
            const std::string& k = s->key;
            if (k.find(std::string("/") + GROWN[g] + "/") == std::string::npos) continue;
            // Tractors and motorbikes are objects of their own, not cars by the road.
            if (k.find("tractor") != std::string::npos || k.find("motorcycle") != std::string::npos) continue;
            g_carTone[g + 1].push_back(s);
        }

    // Flat details, by kind.
    std::string nat = "objects/nature/flowers_mashrooms_other-nature-stuff/";
    for (int g = 0; g < 3; g++) {
        std::string base = std::string("objects/nature/") + GROWN[g] + "/";
        for (int i = 1; i <= 5; i++) add(g_tuft[g + 1], base + "grass_" + std::to_string(i) + "_" + GROWN[g]);
        for (int i = 1; i <= 13; i++) add(g_moss[g + 1], base + "grass-on-top-of-things/moss-on-top_" + GROWN[g] + "_" + std::to_string(i));
        // The same with a lighter rim, which reads on dark asphalt (0.12v).
        for (int i = 1; i <= 13; i++)
            add(g_mossLight[g + 1], base + "grass-on-top-of-things/lighter-outline/moss-on-top_" + GROWN[g] + "_" + std::to_string(i) + "_lighter");
    }
    g_tuft[0] = g_tuft[1];
    g_moss[0] = g_moss[1];
    g_mossLight[0] = g_mossLight[1];
    for (int g = 0; g < 3; g++)
        for (int i = 3; i <= 5; i++)
            g_tuftStep[g + 1][i - 1] = Assets::find(std::string("objects/nature/") + GROWN[g] + "/grass_" + std::to_string(i) + "_stepping-on-animation_" + GROWN[g]);
    for (int i = 0; i < 5; i++) g_tuftStep[0][i] = g_tuftStep[1][i];
    for (const char* f : {"flowers_1", "flowers_2", "flowers_3", "flower_1", "flower_2"})
        for (const char* c : {"blue", "purple", "red", "yellow"}) add(g_decoKind[DK_FLOWER], nat + f + "_" + c);
    for (const char* f : {"mushroom", "mushrooms_1_yellow", "mushrooms_2_red", "stick", "stick_leaves", "stump_2_mushrooms"})
        add(g_decoKind[DK_FOREST], nat + f);
    for (const char* j : {"cardboard_1", "cardboard_2", "chips-pack_red", "chips-pack_yellow", "trash-bag_1", "trash-bag_2",
                          "gray-brick", "gray-brick_debris", "iron-beam", "exhaust-pipe", "tire_1", "traffic-cone",
                          "buildings/brick-wall_1_lying", "buildings/brick-wall_2_lying", "pallet_2"})
        add(g_decoKind[DK_JUNK], std::string("objects/") + j);
    for (int i = 1; i <= 7; i++) add(g_decoKind[DK_PEBBLE], nat + "rocks/rock_" + std::to_string(i));
    for (const char* j : {"buildings/layered-posters_1_for-ground-and-walls", "buildings/layered-posters_2_for-ground-and-walls", "manhole"})
        add(g_decoKind[DK_POSTER], std::string("objects/") + j);

    // Standing objects.
    auto plain = [](int kind, const std::string& key) {
        if (const Sprite* s = Assets::find(key)) g_obj[kind].plain.push_back(piece(s));
    };
    // `pattern` has one %s for the grass it is overgrown with (maybe twice).
    auto grown = [](int kind, const char* pattern) {
        for (int g = 0; g < 3; g++) {
            char key[200];
            std::snprintf(key, sizeof key, pattern, GROWN[g], GROWN[g]);
            if (const Sprite* s = Assets::find(key)) g_obj[kind].grown[g].push_back(piece(s));
        }
    };
    auto prefix = [](int kind, const std::string& pre, int g) {
        for (const Sprite* s : Assets::keysWithPrefix(pre)) (g < 0 ? g_obj[kind].plain : g_obj[kind].grown[g]).push_back(piece(s));
    };
    plain(OB_BENCH_DOWN, "objects/bench_1_down");  grown(OB_BENCH_DOWN, "objects/bench_2_down_overgrown_%s");
    plain(OB_BENCH_UP, "objects/bench_5_up");      grown(OB_BENCH_UP, "objects/bench_6_up_overgrown_%s");
    plain(OB_BENCH_SIDE, "objects/bench_3_side");  grown(OB_BENCH_SIDE, "objects/bench_4_side_overgrown_%s");
    plain(OB_HYDRANT, "objects/hydrant_1_red");    plain(OB_HYDRANT, "objects/hydrant_1_yellow");
    plain(OB_TRASH_CAN, "objects/trash-can_1");    plain(OB_TRASH_CAN, "objects/trash-can_2");
    for (int i = 1; i <= 4; i++) plain(OB_DUMPSTER, "objects/garbage-bin_" + std::to_string(i));
    plain(OB_VENDING, "objects/vending-machine_blue"); plain(OB_VENDING, "objects/vending-machine_red");
    grown(OB_VENDING, "objects/vending-machine_blue_overgrown_%s"); grown(OB_VENDING, "objects/vending-machine_red_overgrown_%s");
    plain(OB_STOP_DOWN, "objects/stop-sign_down_1"); grown(OB_STOP_DOWN, "objects/stop-sign_down_2_overgrown_%s");
    plain(OB_STOP_UP, "objects/stop-sign_up_5");     grown(OB_STOP_UP, "objects/stop-sign_up_6_overgrown_%s");
    plain(OB_STOP_SIDE, "objects/stop-sign_side_3"); grown(OB_STOP_SIDE, "objects/stop-sign_side_4_overgrown_%s");
    for (const char* b : {"blue_1", "red_1", "rust_blue_1", "rust_red_1", "blue_2", "red_2", "rust_blue_2", "rust_red_2"})
        plain(OB_BARREL, std::string("objects/barrel_") + b);
    plain(OB_TIRES, "objects/tire_1");
    grown(OB_TIRES, "objects/tire_2_grass_%s"); grown(OB_TIRES, "objects/2-tires_grass_%s");
    plain(OB_PALLET, "objects/pallet_1"); plain(OB_PALLET, "objects/pallet_2");
    plain(OB_CART, "objects/shopping-cart");
    plain(OB_CONE, "objects/traffic-cone");
    for (const char* c : {"1_gray", "5_red", "9_green"}) plain(OB_CONTAINER_V, std::string("objects/container/container_") + c + "_vertical");
    for (const char* c : {"3_gray", "7_red", "11_green"}) plain(OB_CONTAINER_H, std::string("objects/container/container_") + c + "_horizontal");
    for (const char* c : {"2_gray", "6_red", "10_green"})
        grown(OB_CONTAINER_V, (std::string("objects/container/container_") + c + "_vertical_overgrown_%s").c_str());
    for (const char* c : {"4_gray", "8_red", "12_green"})
        grown(OB_CONTAINER_H, (std::string("objects/container/container_") + c + "_horizontal_overgrown_%s").c_str());
    plain(OB_FRIDGE, "objects/refrigerator");
    plain(OB_WASHER, "objects/washing-machine");
    plain(OB_TRUNK, nat + "tree-trunk_1_green");
    grown(OB_TRUNK, "objects/nature/%s/tree-trunk_2_grass_%s");
    plain(OB_STUMP, nat + "stump_1"); plain(OB_STUMP, nat + "stump_2_mushrooms");
    for (int i = 1; i <= 7; i++) plain(OB_BOULDER, nat + "rocks/rock_" + std::to_string(i));
    grown(OB_BOULDER, "objects/nature/%s/rocks/rock-grass");
    prefix(OB_TRACTOR, "objects/vehicles/rust/car_5_rust_tractor/", -1);
    prefix(OB_MOTORBIKE, "objects/vehicles/normal/car_9_motorcycle/", -1);
    for (int g = 0; g < 3; g++) {
        prefix(OB_TRACTOR, std::string("objects/vehicles/overgrown/car_5_overgrown_tractor/") + GROWN[g] + "/", g);
        prefix(OB_MOTORBIKE, std::string("objects/vehicles/overgrown/car_9_motorcycle/") + GROWN[g] + "/", g);
    }
    plain(OB_JUNK, "objects/metal-plates"); plain(OB_JUNK, "objects/gray-brick_debris");
    plain(OB_HVAC, "objects/buildings/hvac"); grown(OB_HVAC, "objects/buildings/hvac_overgrown_%s");
    plain(OB_PAINTING, "furniture/painting_sunset"); plain(OB_PAINTING, "furniture/painting_hills");
    plain(OB_DOOR_BOARDED, "objects/buildings/door_3_boarded-up_beige"); plain(OB_DOOR_BOARDED, "objects/buildings/door_6_boarded-up_metal");
    for (const char* b : {"balcony_1_left", "balcony_2_right", "balcony_3_left_ladder-hole", "balcony_4_right_ladder-hole"})
        plain(OB_BALCONY, std::string("objects/buildings/") + b);
    plain(OB_LADDER, "objects/buildings/ladder_balcony_metal_1"); plain(OB_LADDER, "objects/buildings/ladder_balcony_metal_1_rusty");
    plain(OB_CELLAR, "objects/buildings/enterance_green"); grown(OB_CELLAR, "objects/buildings/enterance_%s");
    for (const char* v : {"air-vent_1", "air-vent_2_rusty", "air-vent_3", "air-vent_4_rusty"}) plain(OB_VENT, std::string("objects/buildings/") + v);
    plain(OB_ANTENNA, "objects/buildings/antenna_1"); plain(OB_ANTENNA, "objects/buildings/antenna_2");
    plain(OB_ROOF_HOLE, "objects/buildings/roof-hole_1_gray"); plain(OB_ROOF_HOLE, "objects/buildings/roof-hole_2_red");
    for (const char* d : {"duct_1_side", "duct_2_down", "duct_3_up"}) plain(OB_DUCT, std::string("objects/buildings/") + d);
    for (int i : {3, 4, 9, 10, 15, 16, 19, 20})
        if (const Sprite* s = Assets::search({"objects/windows/window_" + std::to_string(i) + "_"})) g_obj[OB_WINDOW].plain.push_back(piece(s));
    for (int i : {1, 2, 7, 8, 13, 14})
        if (const Sprite* s = Assets::search({"objects/windows/window_" + std::to_string(i) + "_"})) g_obj[OB_WINDOW_BROKEN].plain.push_back(piece(s));
    for (int i : {5, 6, 11, 12, 17, 18, 21})
        if (const Sprite* s = Assets::search({"objects/windows/window_" + std::to_string(i) + "_"})) g_obj[OB_WINDOW_BOARDED].plain.push_back(piece(s));
    plain(OB_POSTER, "objects/buildings/layered-posters_1_for-ground-and-walls");
    plain(OB_POSTER, "objects/buildings/layered-posters_2_for-ground-and-walls");
    // Awnings: pick = colour * 8 + width (0 = two tiles .. 4 = six tiles).
    for (const char* c : {"blue", "orange"})
        for (int i = 1; i <= 5; i++) plain(OB_AWNING, std::string("objects/buildings/awning_") + c + "_" + std::to_string(i));
    // Graffiti and shop glass are painted on the facade sheets' own wall colour, so they
    // come from each sheet: pick = sheet * 8 + which. Ivy is see-through, any sheet will do.
    for (const Sprite* sheet : g_buildingWalls) {
        for (int f : {0, 3, 26, 29, 65, 66}) g_obj[OB_GRAFFITI].plain.push_back(piece(sheet, f));
        for (int f : {9, 10, 11, 12, 22, 23, 24, 25}) g_obj[OB_SHOPFRONT].plain.push_back(piece(sheet, f));
    }
    if (!g_buildingWalls.empty())
        for (int f : {78, 79, 80, 81, 82, 91, 92, 93, 94, 104, 105, 106, 107}) g_obj[OB_IVY].plain.push_back(piece(g_buildingWalls[0], f));

    // Ground overlays (see Overlay).
    g_bgWaste = Assets::find("tiles/background_bleak-yellow_tileset");
    const Sprite* garbage = Assets::find("tiles/garbage_tileset");
    const Sprite* grassTop = Assets::find("tiles/grass_on-top_tileset");
    g_overlay.assign(OV_COUNT, Assets::TileRef());
    auto ov = [&](int id, const Sprite* sheet, int frame) {
        if (sheet && sheet->valid() && frame < sheet->frameCount() && id < OV_COUNT) { g_overlay[id].sprite = sheet; g_overlay[id].frame = frame; }
    };
    const int cross[6] = {169, 170, 171, 216, 240, 264};
    for (int i = 0; i < 6; i++) ov(OV_CROSS_EW + i, g_bg, cross[i]);
    const int bays[12] = {193, 194, 195, 196, 217, 218, 219, 220, 241, 242, 243, 244};
    for (int i = 0; i < 12; i++) ov(OV_PARKING + i, g_bg, bays[i]);
    for (int i = 0; i < 29; i++) ov(OV_GARBAGE + i, garbage, i);
    for (int i = 0; i < 48; i++) ov(OV_GRASSTOP + i, grassTop, i);
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
    set(IT_BAT, "icon_bat");
    set(IT_SOUP, "icon_canned-soup");
    // (0.12v tried showing a big stack of rounds as a crate, but the pack's blue and red
    // crates are already the .338 and rocket icons: a stack of 9mm turned into .338.
    // Every kind of round keeps its own icon.)
}

}  // namespace

void init() {
    buildFills();
    loadCharacters();
    loadProps();
    loadDressing();
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
    case G_WASTE: return 3;
    case G_DIRT: return 4;
    case G_RUBBLE: return 5;
    case G_ROAD:
    case G_BRIDGE:
    case G_PAVEMENT: return 6;
    case G_FLOOR_WOOD:
    case G_FLOOR_CONCRETE:
    case G_FLOOR_TILE:
    case G_BASE_FLOOR: return 7;
    }
    return 0;
}

// Each background sheet carries its own grass, its own earth and its own seam
// art, so a seam is always drawn from the same sheet as the ground it joins.
const Sprite* vegSheet(int groundType) {
    if (groundType == G_SAND && g_bgDry && g_bgDry->valid()) return g_bgDry;
    if (groundType == G_WASTE && g_bgWaste && g_bgWaste->valid()) return g_bgWaste;
    return g_bg;
}

bool isVeg(int g) { return g == G_GRASS || g == G_SAND || g == G_WASTE; }

// Kerbs (0.12v). Every background sheet draws a kerbstone along the edge of its paving,
// its grass and its earth, with outer corners, straight runs and inner corners. Index:
// [0] N+W, [1] N+E, [2] S+W, [3] S+E, [4] N, [5] S, [6] W, [7] E, then the inner corners
// where only a diagonal is open: [8] NW, [9] NE, [10] SW, [11] SE.
const int KERB_PAVE[12] = {6, 7, 30, 31, 57, 9, 34, 32, 58, 56, 10, 8};
const int KERB_GRASS[12] = {120, 121, 144, 145, 60, 12, 37, 35, 61, 59, 13, 11};
const int KERB_DIRT[12] = {41, 42, 65, 66, 68, 20, 45, 43, 69, 67, 21, 19};

// Which kerb piece a tile needs, given which sides (and diagonals) are open: -1 none.
int kerbIndex(bool n, bool s, bool w, bool e, bool nw, bool ne, bool sw, bool se) {
    if (n && s) { n = false; }          // a strip one tile wide: kerb the south side only
    if (w && e) { w = false; }
    if (n && w) return 0;
    if (n && e) return 1;
    if (s && w) return 2;
    if (s && e) return 3;
    if (n) return 4;
    if (s) return 5;
    if (w) return 6;
    if (e) return 7;
    if (nw) return 8;
    if (ne) return 9;
    if (sw) return 10;
    if (se) return 11;
    return -1;
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
    case G_SAND:
    case G_WASTE: set = F_GRASS; n = 5; break;     // lush, dry or dead: same frames, other sheet
    case G_DIRT: return Assets::TileRef();          // see ground(): stitched fills that match the edges
    case G_ROAD:
    case G_BRIDGE: set = F_ROAD; n = 2; break;
    default: return Assets::TileRef();
    }
    // Mostly the first tile. Scattering the rest stops a large field reading as one
    // flat colour without turning it into a chequerboard.
    int frame = set[0];
    if (n > 1 && variant % 5 == 1) frame = set[1 + (variant / 5) % (n - 1)];
    if (isVeg(groundType) && variant % 23 == 7)
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
    case G_WASTE: {
        int frame = TileMappings::GRASS[variant % 5];
        return mappedBackground(g_bgWaste, frame).valid() ? mappedBackground(g_bgWaste, frame) : pickFill(Fill::Stone, 0, 2);
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

// Where asphalt meets a kerb (the paving's or a planter's), the kerb is the edge: the
// road stays plain asphalt right up to it instead of drawing its worn shoulder.
static bool roadOrKerb(const ::World& w, int tx, int ty) {
    if (!w.inBounds(tx, ty)) return false;
    const Tile& t = w.at(tx, ty);
    return t.ground == G_ROAD || t.ground == G_BRIDGE || t.ground == G_PAVEMENT || (t.flags & TF_KERB);
}

bool roadEdgeTile(const ::World& w, int x, int y) {
    return roadFrame(roadOrKerb(w, x, y - 1), roadOrKerb(w, x, y + 1), roadOrKerb(w, x - 1, y), roadOrKerb(w, x + 1, y)) >= 0;
}

// A kerbed tile (paving by a road, a planter): the kerb goes along every side where
// what lies beyond is not the same kind of ground.
static Assets::TileRef kerbTile(const ::World& w, int x, int y) {
    const Tile& self = w.at(x, y);
    bool pave = self.ground == G_PAVEMENT;
    auto open = [&](int tx, int ty) {
        if (!w.inBounds(tx, ty)) return false;
        const Tile& o = w.at(tx, ty);
        if (pave) return o.ground == G_ROAD || o.ground == G_BRIDGE;          // paving: kerbed against the road
        return o.ground != self.ground || !(o.flags & TF_KERB);               // planter: against anything else
    };
    int k = kerbIndex(open(x, y - 1), open(x, y + 1), open(x - 1, y), open(x + 1, y),
                      open(x - 1, y - 1), open(x + 1, y - 1), open(x - 1, y + 1), open(x + 1, y + 1));
    if (k < 0) return Assets::TileRef();
    const int* set = pave ? KERB_PAVE : self.ground == G_DIRT ? KERB_DIRT : KERB_GRASS;
    return bgTile(pave || self.ground == G_DIRT ? g_bg : vegSheet(self.ground), set[k]);
}

Assets::TileRef groundTile(const ::World& w, int x, int y, float time) {
    const Tile& self = w.at(x, y);
    if (self.ground == G_WATER)   // animated, never autotiled
        return ground(self.ground, (uint8_t)((int)(time * 1.5f) + x + y));
    if (self.ground == G_ROAD && g_bg && g_bg->valid()) {
        int f = roadFrame(roadOrKerb(w, x, y - 1), roadOrKerb(w, x, y + 1), roadOrKerb(w, x - 1, y), roadOrKerb(w, x + 1, y));
        if (f >= 0) return bgTile(g_bg, f);
    }
    if ((self.ground == G_PAVEMENT || (self.flags & TF_KERB)) && g_bg && g_bg->valid()) {
        Assets::TileRef k = kerbTile(w, x, y);
        if (k.valid()) return k;
        if (self.flags & TF_KERB) return ground(self.ground, self.variant);   // inside a planter: no seams
    }

    // Grass against bare earth, and one grass against another, are joined with the
    // sheets' corner rings; anything else meets with a plain edge.
    bool vegetation = isVeg(self.ground);
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
        bool dirt = false, lush = false, dry = false, dead = false, other = false;
        for (int c = 0; c < 4; c++) {
            int best = self.ground;
            for (int k = 0; k < 4; k++) {
                int g = groundAt(x + CX[c][k], y + CY[c][k]);
                // A kerbed planter keeps its own edge: it does not bleed into the corners.
                int gx = std::clamp(x + CX[c][k], 0, w.w - 1), gy = std::clamp(y + CY[c][k], 0, w.h - 1);
                if (w.at(gx, gy).flags & TF_KERB) g = self.ground;
                if (groundPriority(g) > groundPriority(best)) best = g;
            }
            corner[c] = best;
            dirt |= best == G_DIRT;
            lush |= best == G_GRASS;
            dry |= best == G_SAND;
            dead |= best == G_WASTE;
            other |= best != G_DIRT && !isVeg(best);
        }
        int kinds = (int)lush + (int)dry + (int)dead;
        if (!other && dirt) {
            // Grass against bare earth: that grass's sheet has the ring. Where earth and
            // two grasses meet in one tile, the tile's own grass takes the earth edge
            // (the earth seam is the one that shows).
            int mask = 0;
            for (int c = 0; c < 4; c++) if (corner[c] == G_DIRT) mask |= 1 << c;
            int veg = isVeg(self.ground) && kinds > 1 ? self.ground : lush ? G_GRASS : dry ? G_SAND : dead ? G_WASTE : -1;
            if (mask == 15) return ground(G_DIRT, self.variant);   // grass swallowed by earth on every corner
            if (veg >= 0) {
                Assets::TileRef ref = bgTile(vegSheet(veg), W_GRASS_DIRT[mask]);
                if (ref.valid()) return ref;
            }
        } else if (!other && !dirt && kinds == 2 && dead) {
            // Dead scrub against either living grass: the bleak sheet draws both edges,
            // laid out like its earth ring, green three frames before it (inner corners
            // two) and dark green three after (two). The mask is the living corners.
            int live = lush ? G_GRASS : G_SAND;
            int mask = 0;
            for (int c = 0; c < 4; c++) if (corner[c] == live) mask |= 1 << c;
            if (mask == 15) return ground(live, self.variant);
            int f = W_GRASS_DIRT[mask];
            if (f >= 0 && g_bgWaste && g_bgWaste->valid()) {
                bool inner = f == 368 || f == 369 || f == 392 || f == 393;
                f += live == G_GRASS ? (inner ? -2 : -3) : (inner ? 2 : 3);
                Assets::TileRef ref = bgTile(g_bgWaste, f);
                if (ref.valid()) return ref;
            }
        } else if (!other && !dirt && lush && dry && !dead) {
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

Piece doorStyle(int style, int state, int pickN) {
    static const char* KEYS[5][3] = {
        {"furniture/door_brown", "furniture/door_brown_open", "furniture/door_brown_hole"},
        {"furniture/door_dark", "furniture/door_dark_open", "furniture/door_dark_hole"},
        {"furniture/door_white", "furniture/door_white_open", "furniture/door_white"},
        {"objects/buildings/door_1_beige", "objects/buildings/door_2_ajar_beige", "objects/buildings/door_1_beige"},
        {"objects/buildings/door_4_metal", "objects/buildings/door_2_ajar_beige", "objects/buildings/door_5_rusty_metal"},
    };
    int s = std::clamp(style, 0, 4), st = std::clamp(state, 0, 2);
    const char* key = KEYS[s][st];
    if (s == 4 && st == 0 && (pickN & 1)) key = "objects/buildings/door_5_rusty_metal";
    const Sprite* sp = Assets::find(key);
    if (!sp) return door((uint8_t)(st == 1 ? 1 : 0));
    return piece(sp);
}
// A slow wind wave rolls across the map, so neighbouring trees lean together but
// not in lockstep; a quicker flutter on top keeps the leaves alive between gusts.
static float windAt(uint8_t v, float time, float speed) {
    float phase = (v % 13) * 0.48f;
    float wave = std::sin(time * 0.9f * speed + phase);
    float gust = std::sin(time * 0.37f + phase * 0.3f) * 0.5f + 0.5f;     // 0..1, strength
    float flutter = std::sin(time * 3.1f * speed + phase * 2.7f) * 0.35f;
    return wave * (0.6f + gust * 0.8f) + flutter;
}

static const std::vector<const Sprite*>& toned(const std::vector<const Sprite*>* set, uint8_t tone) {
    int t = tone < TONE_COUNT ? tone : 0;
    return set[t].empty() ? set[0] : set[t];
}
Piece tree(uint8_t v, float time, uint8_t tone) {
    Piece p = piece(pick(toned(g_treeTone, tone), v));
    if (p.valid() && time > 0) p.sway = windAt(v, time, 0.9f + (v % 5) * 0.06f) * 1.6f;
    return p;
}
Piece bush(uint8_t v, float time, uint8_t tone) {
    Piece p = piece(pick(toned(g_bushTone, tone), v));
    if (p.valid() && time > 0) p.sway = windAt((uint8_t)(v * 7 + 3), time, 1.3f) * 0.9f;
    return p;
}
Piece rock(uint8_t v) { return piece(pick(g_rocks, v)); }
Piece barrel(uint8_t v) { return piece(pick(g_barrels, v)); }
Piece car(uint8_t v, uint8_t tone) {
    int t = tone >= 1 && tone <= 3 && !g_carTone[tone].empty() ? tone : 0;
    return piece(pick(g_carTone[t], v));
}

Piece object(int kind, int which, int overgrown) {
    if (kind <= OB_NONE || kind >= OB_COUNT) return Piece();
    const ObjArt& o = g_obj[kind];
    const std::vector<Piece>* set = &o.plain;
    if (overgrown >= 1 && overgrown <= 3 && !o.grown[overgrown - 1].empty()) set = &o.grown[overgrown - 1];
    if (set->empty())
        for (const auto& g : o.grown) if (!g.empty()) { set = &g; break; }
    if (set->empty()) return Piece();
    int n = (int)set->size();
    // Sheet-drawn wall art is laid out 8 to a facade sheet (see loadDressing).
    if (kind == OB_GRAFFITI || kind == OB_SHOPFRONT) {
        int per = kind == OB_GRAFFITI ? 6 : 8, sheets = std::max(1, n / per);
        return (*set)[((which >> 3) % sheets) * per + (which & 7) % per];
    }
    if (kind == OB_AWNING) {
        int colours = std::max(1, n / 5);
        return (*set)[((which >> 3) % colours) * 5 + std::min(4, which & 7)];
    }
    return (*set)[((which % n) + n) % n];
}
int objectChoices(int kind) {
    if (kind <= OB_NONE || kind >= OB_COUNT) return 0;
    return (int)g_obj[kind].plain.size();
}

Piece propArt(const ::WorldProp& p) {
    switch (p.kind) {
    case PROP_CAR: return car(p.variant, p.frame);
    case PROP_WRECK: return wreck(p.variant, p.frame);
    case PROP_OBJECT: {
        Piece o = object(p.variant, p.frame & 63, p.frame >> 6);
        o.flipX = o.flipX != p.flipX;
        return o;
    }
    default: return Piece();
    }
}

Assets::TileRef overlayTile(uint8_t id) {
    if (id >= g_overlay.size()) return Assets::TileRef();
    return g_overlay[id];
}

// The flat roof ring on the facade sheets (columns 5..8, rows 0..2); its middle cell
// (frame 20) is blank on the sheet, so the inside repeats the plain frame 19.
Assets::TileRef flatRoofTile(uint8_t sheet, int col, int row, uint8_t v) {
    Assets::TileRef r;
    if (g_buildingWalls.empty()) return r;
    const Sprite* s = g_buildingWalls[sheet % g_buildingWalls.size()];
    if (!s || !s->valid()) return r;
    static const int F[3][3] = {{5, 6, 8}, {18, 19, 21}, {31, 32, 34}};
    int f = F[std::clamp(row, 0, 2)][std::clamp(col, 0, 2)];
    if (row == 1 && col == 1 && v % 11 != 0) {
        // Plain concrete inside (tools/make_flat_roof.py), the cracked frame now and then.
        static std::vector<const Sprite*> fills[8];
        static bool loaded = false;
        if (!loaded) {
            loaded = true;
            for (size_t i = 0; i < g_buildingWalls.size() && i < 8; i++) {
                const std::string& k = g_buildingWalls[i]->key;   // "tiles/buildings/buildings_<colour>_tileset"
                size_t a = k.find("buildings_"), b = k.find("_tileset");
                if (a == std::string::npos || b == std::string::npos) continue;
                std::string colour = k.substr(a + 10, b - a - 10);
                for (int n = 0; n < 3; n++) add(fills[i], "tileoverrides/flatroof_" + colour + "_" + std::to_string(n));
            }
        }
        const auto& set = fills[(sheet % g_buildingWalls.size()) & 7];
        if (!set.empty()) {
            int n = (v / 11) % 4;   // mostly the clean one
            r.sprite = set[std::min((int)set.size() - 1, n < 2 ? 0 : n - 1)];
            r.frame = 0;
            return r;
        }
    }
    if (row == 0 && col == 1 && (v & 1)) f = 7;
    if (row == 2 && col == 1 && (v & 1)) f = 33;
    r.sprite = s;
    r.frame = std::min(f, s->frameCount() - 1);
    return r;
}
Piece wreck(uint8_t v, int dir) {
    const Sprite* s = pick(g_wrecks, v);
    return s ? piece(s, ((dir % 8) + 8) % 8) : Piece();
}
int wreckCount() { return (int)g_wrecks.size(); }
Piece streetLight(uint8_t v) { return piece(pick(g_lights, v)); }
Piece groundDeco(uint8_t code, uint8_t tone) {
    int kind = code >> 5, which = code & 31;
    // Tufts and moss take the grass's colour; the autumn stands keep the bleak ones.
    int g = tone == TONE_DARK ? 2 : tone == TONE_BLEAK ? 3 : 1;   // autumn turns the trees, not the grass
    switch (kind) {
    case DK_LEGACY: return piece(pick(g_deco, (uint8_t)which));
    case DK_TUFT: return piece(pick(g_tuft[g], (uint8_t)which));
    case DK_MOSS:
        // `which` 16 and up: the lighter-rimmed ones.
        if (which >= 16 && !g_mossLight[g].empty()) return piece(pick(g_mossLight[g], (uint8_t)(which - 16)));
        return piece(pick(g_moss[g], (uint8_t)which));
    default: return kind < 8 ? piece(pick(g_decoKind[kind], (uint8_t)which)) : Piece();
    }
}
Piece stump() { return piece(g_stump); }

Piece pickable(int itemId) {
    const char* key = nullptr;
    switch (baseWeapon(itemId)) {
    case IT_AMMO_LIGHT: key = "bullet-box_1_blue"; break;
    case IT_AMMO_SHELL: key = "bullet-box_1_red"; break;
    case IT_AMMO_RIFLE: key = "bullet-box_1_green"; break;
    case IT_AMMO_SNIPER: key = "ammo-crate_blue"; break;
    case IT_ROCKET: key = "ammo-crate_red"; break;
    case IT_BANDAGE: key = "bandage"; break;
    case IT_BAT: key = "bat"; break;
    case IT_FOOD: key = "canned-food"; break;
    case IT_SOUP: key = "canned-soup"; break;
    case IT_PISTOL: case IT_REVOLVER: key = "pistol"; break;
    case IT_SHOTGUN: key = "shotgun"; break;
    case IT_SMG: case IT_RIFLE: case IT_CARBINE: case IT_SNIPER: case IT_LAUNCHER: key = "gun"; break;
    default: break;
    }
    if (!key) return Piece();
    return piece(Assets::find(std::string("objects/pickable/") + key));
}

Piece groundDecoTrodden(uint8_t code, uint8_t tone) {
    if ((code >> 5) != DK_TUFT) return Piece();
    int g = tone == TONE_DARK ? 2 : tone == TONE_BLEAK ? 3 : 1;
    const auto& v = g_tuft[g];
    if (v.size() != 5) return Piece();
    const Sprite* s = g_tuftStep[g][(code & 31) % 5];
    if (!s || s->frameCount() < 2) return Piece();
    return piece(s, 1);
}

Piece ironFence(int mask) {
    static const Sprite* s = Assets::find("tiles/iron-fence_tileset");
    if (!s || !s->valid()) return Piece();
    // The sheet is one railed-in plot, 3 x 4: corners, top and bottom runs, the sides,
    // and in the middle a lone link.
    int f = 4;
    switch (mask & 15) {
    case 2 | 8: f = 0; break;
    case 1 | 8: f = 2; break;
    case 2 | 4: f = 9; break;
    case 1 | 4: f = 11; break;
    case 1: case 2: case 1 | 2: case 1 | 2 | 4: case 1 | 2 | 8: f = 1; break;
    case 4: case 8: case 4 | 8: case 4 | 8 | 1: f = 5; break;
    case 4 | 8 | 2: f = 3; break;
    default: f = 4; break;
    }
    return piece(s, std::min(f, s->frameCount() - 1));
}
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
    // Bit 0x80 marks a raider's body, drawn in their red; bits 1-2 the fall.
    int fall = (variant >> 1) & 3;
    if (fall > 0 && fall < 3) {
        const Sprite* f = g_humanFall[(variant & 0x80) ? 1 : 0][(variant & 1) ? 3 : 2][fall];
        if (f) return piece(f, f->frameCount() - 1);
    }
    const Sprite* s = (variant & 0x80) ? g_foeDeath[(variant & 1) ? 3 : 2] : nullptr;
    if (!s) s = g_bodyDeath[(variant & 1) ? 3 : 2];
    if (!s) s = g_bodyDeath[(variant & 1) ? 2 : 3];
    if (!s) return Piece();
    return piece(s, s->frameCount() - 1);
}

Piece zombieDeath(int kind, bool left, int frame, int fall, bool noAxe) {
    int k = std::clamp(kind, 0, 2);
    // The pack draws two deaths: a stagger and fall (first) and the bloody one (second).
    const Sprite* s = noAxe ? g_axelessFall[left ? 1 : 0][fall ? 1 : 0] : g_zombieFall[k][left ? 1 : 0][fall ? 1 : 0];
    bool flip = false;
    if (!s) s = g_zombieDeath[k][left ? 1 : 0];
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

Piece humanBody(Dir d, Anim a, int frame, bool holdingGun, bool enemy, int shirt, int fall) {
    int di = (int)d;
    int hands = holdingGun ? 0 : 1;
    const Sprite* s = nullptr;
    int set = enemy ? 1 : (shirt > 0 && shirt < Assets::SHIRT_COUNT ? shirt + 1 : 0);
    if (a == Anim::Punch || a == Anim::PickUp) {
        s = a == Anim::Punch ? g_humanPunch[set][di][hands] : g_humanPick[set][di][hands];
        if (!s) s = a == Anim::Punch ? g_humanPunch[0][di][hands] : g_humanPick[0][di][hands];
        if (s) return piece(s, std::clamp(frame, 0, s->frameCount() - 1));
        a = Anim::Idle;
    }
    if (a == Anim::Death && fall > 0) {
        s = g_humanFall[set][di][fall % 3];
        if (!s) s = g_humanFall[0][di][fall % 3];
        if (s) return piece(s, std::clamp(frame, 0, s->frameCount() - 1));
    }
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
    const Sprite* s = a == Anim::Rack && wc == 2 ? g_gunRack[di] : g_gun[di][wc][ai];
    if (a == Anim::Rack && !s) s = g_gun[di][wc][1];
    if (!s) s = g_gun[di][wc][0];
    if (!s) s = g_gun[di][0][0];
    return piece(s, frame);
}

Piece helmet(Dir d, int frame, Anim a) {
    int di = (int)d;
    const Sprite* s = a == Anim::Punch ? g_helmetAnim[di][0] : a == Anim::PickUp ? g_helmetAnim[di][1] : a == Anim::Death ? g_helmetAnim[di][2] : nullptr;
    if (s) return piece(s, std::clamp(frame, 0, s->frameCount() - 1));
    return piece(g_helmet[di], frame);
}

Piece helmetFall(bool left, int frame) {
    const Sprite* s = g_helmetAnim[left ? 3 : 2][2];
    if (!s) return Piece();
    return piece(s, std::clamp(frame, 0, s->frameCount() - 1));
}

Piece bat(Dir d, Anim a, int frame) {
    int di = (int)d;
    const Sprite* s = g_bat[di][a == Anim::Attack ? 1 : 0];
    if (!s) s = g_bat[di][0];
    bool flip = false;
    if (!s && d == Dir::Left) { s = g_bat[(int)Dir::Right][a == Anim::Attack ? 1 : 0]; flip = true; }
    if (!s) return Piece();
    return piece(s, a == Anim::Attack ? std::clamp(frame, 0, s->frameCount() - 1) : frame, flip);
}

Piece zombie(int kind, Dir d, Anim a, int frame, bool alt, bool noAxe) {
    int k = std::clamp(kind, 0, 2), di = (int)d;
    if (k == 2 && noAxe) {
        int ai = a == Anim::PickUp ? 3 : a == Anim::Attack ? 2 : (a == Anim::Walk || a == Anim::Run) ? 1 : 0;
        if (const Sprite* s = g_axeless[di][ai]) return piece(s, ai == 3 ? std::clamp(frame, 0, s->frameCount() - 1) : frame);
    }
    if (a == Anim::Attack && alt && g_zombieAlt[k][di]) return piece(g_zombieAlt[k][di], frame);
    int ai = a == Anim::Attack ? 2 : (a == Anim::Walk || a == Anim::Run) ? 1 : 0;
    return piece(g_zombie[k][di][ai], frame);
}

Piece thrownAxe(Dir d, int stage, int frame) {
    int di = (int)d;
    const Sprite* s = g_axe[di][std::clamp(stage, 0, 2)];
    if (!s) s = g_axe[2][std::clamp(stage, 0, 2)];
    if (!s) return Piece();
    return piece(s, stage == 0 ? frame : std::clamp(frame, 0, s->frameCount() - 1));
}

Piece muzzleFlash(Dir d, int frame) { return piece(g_fire[(int)d], frame); }

Piece bulletSprite(int weaponItem) {
    const char* key = weaponItem == IT_SHOTGUN ? "character/guns/bullets/shotgun-bullet"
                     : weaponItem == IT_PISTOL ? "character/guns/bullets/pistol-bullet_bullet"
                                               : "character/guns/bullets/gun-bullet_bullet";
    return piece(Assets::find(key));
}

const Assets::Sprite* itemIcon(int itemId, int count) {
    if (itemId <= IT_NONE || itemId >= IT_COUNT) return nullptr;
    (void)count;
    return g_itemIcons[itemId];
}

Piece uiPiece(const char* name) { return piece(Assets::find(std::string("ui/") + name)); }

}  // namespace Art
