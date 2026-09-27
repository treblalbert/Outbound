#include "items.h"
#include "audio.h"
#include "lang.h"
#include "sprites.h"

using namespace Sprites;

static const ItemDef DEFS[IT_COUNT] = {
    {"Nothing", Cat::None, 0, 1, WHITE, 0, 0, ""},
    {"Scrap Metal", Cat::Valuable, 8, 20, I_SCRAP, 0, 0, "Bent metal. Sells for a little."},
    {"Copper Wires", Cat::Valuable, 15, 10, I_WIRES, 0, 0, "A coil of copper wire."},
    {"Bolts", Cat::Valuable, 6, 30, I_BOLTS, 0, 0, "Assorted nuts and bolts."},
    {"Duct Tape", Cat::Valuable, 12, 10, I_TAPE, 0, 0, "Fixes everything. Almost."},
    {"Battery", Cat::Valuable, 30, 5, I_BATTERY, 0, 1, "Still holds a charge."},
    {"Circuit Board", Cat::Valuable, 55, 5, I_CIRCUIT, 0, 1, "Salvaged electronics."},
    {"Gold Watch", Cat::Valuable, 140, 3, I_WATCH, 0, 2, "Ticking. Worth a lot."},
    {"Graphics Card", Cat::Valuable, 380, 1, I_GPU, 0, 3, "Traders go crazy for these."},
    {"Jewelry", Cat::Valuable, 220, 3, I_JEWELRY, 0, 2, "A ring with a pink gem."},
    {"Canned Food", Cat::Valuable, 10, 10, I_FOOD, 0, 0, "Beans. Always beans."},
    {"Med Supplies", Cat::Valuable, 35, 5, I_MEDSUP, 0, 1, "Sterile supplies. Trade good."},
    {"Fuel Can", Cat::Valuable, 45, 2, I_FUEL, 0, 1, "Twelve litres. E beside your car to pour it in."},
    {"Gun Parts", Cat::Valuable, 75, 5, I_GUNPARTS, 0, 2, "Springs, pins and receivers."},
    {"Intel Drive", Cat::Valuable, 650, 1, I_INTEL, 0, 3, "Encrypted data. Extremely valuable."},
    {"Bandage", Cat::Medical, 10, 5, I_BANDAGE, 25, 0, "Heals 25 HP. [H] or right-click."},
    {"Medkit", Cat::Medical, 45, 3, I_MEDKIT, 70, 1, "Heals 70 HP. [H] or right-click."},
    {"Grenade", Cat::Throwable, 40, 4, I_GRENADE, 0, 1, "Throw with [G]. Destroys terrain."},
    {"9mm Rounds", Cat::Ammo, 1, 120, I_AMMO_LIGHT, 0, 0, "Pistol and SMG ammo."},
    {"12ga Shells", Cat::Ammo, 2, 40, I_AMMO_SHELL, 0, 0, "Shotgun ammo."},
    {"5.56 Rounds", Cat::Ammo, 2, 120, I_AMMO_RIFLE, 0, 1, "Assault rifle ammo."},
    {".338 Rounds", Cat::Ammo, 6, 30, I_AMMO_SNIPER, 0, 2, "Sniper rifle ammo."},
    {"Rocket", Cat::Ammo, 60, 4, I_ROCKET, 0, 3, "Launcher ammo. Big boom."},
    {"Pistol", Cat::Weapon, 80, 1, I_PISTOL, 0, 0, "Reliable sidearm. 9mm."},
    {"SMG", Cat::Weapon, 220, 1, I_SMG, 0, 1, "Fast firing. 9mm."},
    {"Shotgun", Cat::Weapon, 200, 1, I_SHOTGUN, 0, 1, "Tight 8-pellet spread. 12ga."},
    {"Assault Rifle", Cat::Weapon, 420, 1, I_RIFLE, 0, 2, "Accurate, automatic. 5.56."},
    {"Sniper Rifle", Cat::Weapon, 600, 1, I_SNIPER, 0, 3, "Huge damage, long range. .338."},
    {"Rocket Launcher", Cat::Weapon, 900, 1, I_LAUNCHER, 0, 3, "Explosive. Levels buildings."},
    {"Light Vest", Cat::Armor, 150, 1, I_VEST_LIGHT, 60, 1, "Absorbs damage until broken."},
    {"Heavy Armor", Cat::Armor, 420, 1, I_VEST_HEAVY, 150, 2, "Heavy plates. Absorbs a lot."},
    {"Small Backpack", Cat::Backpack, 90, 1, I_PACK_SMALL, 4, 1, "+4 inventory slots."},
    {"Large Backpack", Cat::Backpack, 260, 1, I_PACK_LARGE, 8, 2, "+8 inventory slots."},
    {"Revolver", Cat::Weapon, 260, 1, I_REVOLVER, 0, 2, "Hard-hitting six shot sidearm."},
    {"Carbine", Cat::Weapon, 340, 1, I_CARBINE, 0, 2, "Light semi-auto rifle. 5.56."},
    {"M92", Cat::Weapon, 380, 1, I_PISTOL, 0, 3, "Match-grade sidearm. Big magazine, quick hands. 9mm.", true},
    {"Luger", Cat::Weapon, 420, 1, I_PISTOL, 0, 3, "Old, precise and hard-hitting. 9mm.", true},
    {".357 Magnum", Cat::Weapon, 600, 1, I_REVOLVER, 0, 3, "Hand cannon. Few things take more than two. 9mm.", true},
    {"MP5", Cat::Weapon, 560, 1, I_SMG, 0, 3, "Fast, steady and accurate for an SMG. 9mm.", true},
    {"M15", Cat::Weapon, 800, 1, I_CARBINE, 0, 3, "Precise semi-auto rifle, quick follow-up shots. 5.56.", true},
    {"AK-47", Cat::Weapon, 950, 1, I_RIFLE, 0, 3, "Heavy-hitting automatic rifle. 5.56.", true},
    {"M24", Cat::Weapon, 1400, 1, I_SNIPER, 0, 3, "Bolt-action marksman rifle. Pinpoint, devastating. .338.", true},
    {"Baseball Bat", Cat::Melee, 30, 1, I_SCRAP, 42, 1, "Put it in your melee slot and [F] swings it: harder, further, a harder knock-back, and it breaks through walls, fences and trees."},
    {"Canned Soup", Cat::Valuable, 14, 10, I_FOOD, 0, 0, "Tomato, by the smell. Trade good."},
};

//                        ammo            dmg   rate  mag  reload spread pel speed range auto   expl   tile  shake sound
static const WeaponDef W_PISTOL   = {IT_AMMO_LIGHT,  22,  4.0f, 12, 1.2f, 0.05f,  1, 460, 300, false, false, 1.0f, 1.0f, Snd::pistol};
static const WeaponDef W_REVOLVER = {IT_AMMO_LIGHT,  48,  2.2f,  6, 1.8f, 0.035f, 1, 560, 360, false, false, 1.5f, 2.2f, Snd::pistol};
static const WeaponDef W_SMG      = {IT_AMMO_LIGHT,  15, 11.0f, 30, 1.8f, 0.09f,  1, 480, 260, true,  false, 0.8f, 1.0f, Snd::smg};
static const WeaponDef W_SHOTGUN  = {IT_AMMO_SHELL,  15,  1.5f,  6, 2.2f, 0.075f, 8, 470, 260, false, false, 1.4f, 3.0f, Snd::shotgun};
static const WeaponDef W_CARBINE  = {IT_AMMO_RIFLE,  38,  4.2f, 15, 1.7f, 0.025f, 1, 680, 480, false, false, 1.4f, 1.6f, Snd::rifle};
static const WeaponDef W_RIFLE    = {IT_AMMO_RIFLE,  28,  8.0f, 30, 2.2f, 0.04f,  1, 600, 420, true,  false, 1.2f, 1.5f, Snd::rifle};
static const WeaponDef W_SNIPER   = {IT_AMMO_SNIPER, 120, 0.9f,  5, 3.0f, 0.006f, 1, 950, 700, false, false, 3.0f, 4.0f, Snd::sniper};
static const WeaponDef W_LAUNCHER = {IT_ROCKET,      160, 0.8f,  1, 2.6f, 0.02f,  1, 300, 500, false, true,  0.0f, 6.0f, Snd::launcher};
// Elite guns: clearly better than what they upgrade, without making them a new class.
static const WeaponDef W_M92      = {IT_AMMO_LIGHT,  27,  5.5f, 17, 1.0f, 0.04f,  1, 500, 320, false, false, 1.1f, 1.0f, Snd::pistol};
static const WeaponDef W_LUGER    = {IT_AMMO_LIGHT,  36,  4.5f, 10, 1.1f, 0.022f, 1, 540, 380, false, false, 1.2f, 1.2f, Snd::pistol};
static const WeaponDef W_MAGNUM   = {IT_AMMO_LIGHT,  72,  2.5f,  6, 1.5f, 0.022f, 1, 640, 420, false, false, 1.8f, 2.6f, Snd::pistol};
static const WeaponDef W_MP5      = {IT_AMMO_LIGHT,  19, 13.0f, 32, 1.5f, 0.06f,  1, 530, 300, true,  false, 0.9f, 1.0f, Snd::smg};
static const WeaponDef W_M15      = {IT_AMMO_RIFLE,  48,  5.5f, 20, 1.5f, 0.016f, 1, 730, 540, false, false, 1.5f, 1.6f, Snd::rifle};
static const WeaponDef W_AK47     = {IT_AMMO_RIFLE,  36,  9.0f, 30, 2.0f, 0.036f, 1, 640, 460, true,  false, 1.4f, 1.8f, Snd::rifle};
static const WeaponDef W_M24      = {IT_AMMO_SNIPER, 175, 1.1f,  5, 2.6f, 0.004f, 1, 1050, 820, false, false, 3.5f, 4.0f, Snd::sniper};

const ItemDef& itemDef(int id) { return DEFS[(id >= 0 && id < IT_COUNT) ? id : 0]; }

const WeaponDef* weaponDef(int id) {
    switch (id) {
    case IT_PISTOL: return &W_PISTOL;
    case IT_REVOLVER: return &W_REVOLVER;
    case IT_SMG: return &W_SMG;
    case IT_SHOTGUN: return &W_SHOTGUN;
    case IT_CARBINE: return &W_CARBINE;
    case IT_RIFLE: return &W_RIFLE;
    case IT_SNIPER: return &W_SNIPER;
    case IT_LAUNCHER: return &W_LAUNCHER;
    case IT_M92: return &W_M92;
    case IT_LUGER: return &W_LUGER;
    case IT_MAGNUM: return &W_MAGNUM;
    case IT_MP5: return &W_MP5;
    case IT_M15: return &W_M15;
    case IT_AK47: return &W_AK47;
    case IT_M24: return &W_M24;
    }
    return nullptr;
}

int baseWeapon(int id) {
    switch (id) {
    case IT_M92: case IT_LUGER: return IT_PISTOL;
    case IT_MAGNUM: return IT_REVOLVER;
    case IT_MP5: return IT_SMG;
    case IT_M15: return IT_CARBINE;
    case IT_AK47: return IT_RIFLE;
    case IT_M24: return IT_SNIPER;
    }
    return id;
}

int eliteOf(int id) {
    switch (id) {
    case IT_PISTOL: return IT_M92;     // the Luger is rolled as the pistol's other upgrade
    case IT_REVOLVER: return IT_MAGNUM;
    case IT_SMG: return IT_MP5;
    case IT_CARBINE: return IT_M15;
    case IT_RIFLE: return IT_AK47;
    case IT_SNIPER: return IT_M24;
    }
    return IT_NONE;
}

Item makeItem(int id, int count) {
    Item it;
    it.id = (int16_t)id;
    it.count = (int16_t)std::max(1, count);
    if (const WeaponDef* w = weaponDef(id)) it.data = w->magSize;
    if (itemDef(id).cat == Cat::Armor) it.data = itemDef(id).param;
    return it;
}

int itemValue(const Item& it) {
    if (it.empty()) return 0;
    const ItemDef& d = itemDef(it.id);
    if (d.cat == Cat::Armor && d.param > 0) return std::max(5, d.value * it.data / d.param);
    // What the crafter fitted adds a little to what a trader pays.
    int mods = weaponDef(it.id) ? ((it.flags & ITEMF_LASER) ? 120 : 0) + ((it.flags & ITEMF_MAG_EXT) ? 80 : 0) + ((it.flags & ITEMF_MAG_DRUM) ? 220 : 0) : 0;
    if (hasTier(it) && !d.elite) {
        static const float VALUE[4] = {0.8f, 1.0f, 1.3f, 1.7f};
        return (int)(d.value * VALUE[std::clamp(itemTier(it), -1, 2) + 1]) * it.count + mods;
    }
    return d.value * it.count + mods;
}

bool hasTier(const Item& it) { return !it.empty() && weaponDef(it.id) != nullptr; }

int magSizeOf(const Item& it) {
    const WeaponDef* w = weaponDef(it.id);
    if (!w) return 0;
    if (it.flags & ITEMF_MAG_DRUM) return w->magSize * 2;
    if (it.flags & ITEMF_MAG_EXT) return (w->magSize * 3 + 1) / 2;
    return w->magSize;
}

int itemTier(const Item& it) {
    if (!hasTier(it)) return TIER_UNCOMMON;
    if (itemDef(it.id).elite) return TIER_LEGENDARY;
    return std::clamp((int)it.tier, (int)TIER_COMMON, (int)TIER_EPIC);
}

const char* tierName(int tier) {
    switch (tier) {
    case TIER_COMMON: return "Common";
    case TIER_RARE: return "Rare";
    case TIER_EPIC: return "Epic";
    case TIER_LEGENDARY: return "Legendary";
    default: return "Uncommon";
    }
}

Color tierColor(int tier) {
    switch (tier) {
    case TIER_COMMON: return Color(0.68f, 0.68f, 0.72f);
    case TIER_RARE: return Color(0.30f, 0.62f, 1.00f);
    case TIER_EPIC: return Color(0.74f, 0.40f, 1.00f);
    case TIER_LEGENDARY: return Color(1.00f, 0.74f, 0.20f);
    default: return Color(0.42f, 0.86f, 0.34f);
    }
}

// Legendary guns are the elite ones, whose own stats already set them apart.
float tierDamage(int tier) { return tier == TIER_COMMON ? 0.88f : tier == TIER_RARE ? 1.10f : tier == TIER_EPIC ? 1.22f : 1.0f; }
float tierSpread(int tier) { return tier == TIER_COMMON ? 1.15f : tier == TIER_RARE ? 0.92f : tier == TIER_EPIC ? 0.85f : 1.0f; }
float tierReload(int tier) { return tier == TIER_COMMON ? 1.10f : tier == TIER_RARE ? 0.95f : tier == TIER_EPIC ? 0.90f : 1.0f; }

float laserLength(int tier) {
    switch (tier) {
    case TIER_COMMON: return 150;
    case TIER_RARE: return 215;
    case TIER_EPIC: return 250;
    case TIER_LEGENDARY: return 290;
    default: return 180;
    }
}

static int g_lootDay = 99;
void setLootDay(int day) { g_lootDay = std::max(1, day); }
int lootDay() { return g_lootDay; }
float eliteGate() { return clampf((g_lootDay - 3) / 5.0f, 0, 1); }

int rollWeaponTier(Rng& rng, float q) {
    // 0..1 is the outside world; the catacombs go up to 2, where epic and rare guns
    // are common and plain ones rare.
    q = clampf(q, 0, 2);
    // The days gate it (0.12v): on day 1 even the best spot rolls like a middling
    // one, epics only start on day 3 and reach their full odds around day 8, and rare
    // guns are scarce at first. Roughly: day 1 no epics, ~6% rare; day 3 ~2% epic,
    // ~12% rare; day 5 ~6% / 18%; day 8+ as before.
    float prog = clampf((g_lootDay - 1) / 9.0f, 0, 1);
    q = std::min(q, 0.3f + 1.7f * prog);
    float epicGate = clampf((g_lootDay - 2) / 6.0f, 0, 1);
    float rareGate = 0.35f + 0.65f * prog;
    float epic = (0.04f + 0.08f * q) * epicGate, rare = (0.14f + 0.14f * q) * rareGate, common = std::max(0.02f, 0.36f - 0.20f * q);
    float r = rng.f();
    if (r < epic) return TIER_EPIC;
    if (r < epic + rare) return TIER_RARE;
    if (r < epic + rare + common) return TIER_COMMON;
    return TIER_UNCOMMON;
}

std::string itemLabel(const Item& it) {
    std::string s = T(itemDef(it.id).name);
    if (it.count > 1) s += " x" + std::to_string(it.count);
    return s;
}

int addToSlots(std::vector<Item>& slots, Item it, int usableSlots) {
    if (it.empty()) return 0;
    int n = usableSlots < 0 ? (int)slots.size() : std::min(usableSlots, (int)slots.size());
    int stack = itemDef(it.id).stack;
    int left = it.count;
    if (stack > 1) {
        for (int i = 0; i < n && left > 0; i++) {
            if (slots[i].id != it.id || slots[i].count >= stack) continue;
            int add = std::min(left, stack - slots[i].count);
            slots[i].count += add;
            left -= add;
        }
    }
    for (int i = 0; i < n && left > 0; i++) {
        if (!slots[i].empty()) continue;
        int add = std::min(left, stack);
        slots[i] = it;
        slots[i].count = (int16_t)add;
        left -= add;
    }
    return left;
}

int countInSlots(const std::vector<Item>& slots, int id, int usableSlots) {
    int n = usableSlots < 0 ? (int)slots.size() : std::min(usableSlots, (int)slots.size());
    int c = 0;
    for (int i = 0; i < n; i++) if (slots[i].id == id) c += slots[i].count;
    return c;
}

int takeFromSlots(std::vector<Item>& slots, int id, int amount, int usableSlots) {
    int n = usableSlots < 0 ? (int)slots.size() : std::min(usableSlots, (int)slots.size());
    int taken = 0;
    for (int i = n - 1; i >= 0 && taken < amount; i--) {
        if (slots[i].id != id) continue;
        int t = std::min<int>(amount - taken, slots[i].count);
        slots[i].count -= t;
        taken += t;
        if (slots[i].count <= 0) slots[i] = Item();
    }
    return taken;
}

static int pickByRarity(Rng& rng, float q, const std::vector<int>& ids) {
    float weights[4] = {10.0f, 4.0f + 6.0f * q, 0.8f + 5.0f * q, 0.15f + 2.5f * q};
    float total = 0;
    for (int id : ids) total += weights[itemDef(id).rarity];
    float r = rng.f() * total;
    for (int id : ids) {
        r -= weights[itemDef(id).rarity];
        if (r <= 0) return id;
    }
    return ids.back();
}

Item rollLoot(Rng& rng, float q, LootKind kind) {
    // category weights: valuable, medical, ammo, weapon, armor, backpack, throwable
    float w[7];
    switch (kind) {
    case LootKind::Crate:    { float v[7] = {45, 10, 25, 8, 3, 3, 6}; std::copy(v, v + 7, w); break; }
    case LootKind::Locker:   { float v[7] = {30, 8, 25, 15, 12, 10, 4}; std::copy(v, v + 7, w); break; }
    case LootKind::Cabinet:  { float v[7] = {70, 20, 10, 0, 0, 0, 0}; std::copy(v, v + 7, w); break; }
    case LootKind::Military: { float v[7] = {8, 10, 35, 20, 12, 5, 15}; std::copy(v, v + 7, w); break; }
    case LootKind::Toolbox:  { float v[7] = {90, 0, 10, 0, 0, 0, 0}; std::copy(v, v + 7, w); break; }
    case LootKind::Bag:      { float v[7] = {40, 20, 30, 2, 0, 2, 8}; std::copy(v, v + 7, w); break; }
    default:                 { float v[7] = {50, 12, 20, 5, 3, 3, 4}; std::copy(v, v + 7, w); break; }
    }
    float total = 0;
    for (float x : w) total += x;
    float r = rng.f() * total;
    int cat = 0;
    for (; cat < 6; cat++) { r -= w[cat]; if (r <= 0) break; }

    int id = IT_SCRAP;
    switch (cat) {
    case 0:
        if (kind == LootKind::Toolbox) id = pickByRarity(rng, q, {IT_SCRAP, IT_WIRES, IT_BOLTS, IT_TAPE, IT_GUNPARTS, IT_BATTERY});
        else if (kind == LootKind::Military) id = pickByRarity(rng, q, {IT_BATTERY, IT_CIRCUIT, IT_GUNPARTS, IT_INTEL});
        else id = pickByRarity(rng, q, {IT_SCRAP, IT_WIRES, IT_BOLTS, IT_TAPE, IT_BATTERY, IT_CIRCUIT, IT_WATCH, IT_GPU,
                                       IT_JEWELRY, IT_FOOD, IT_SOUP, IT_MEDSUP, IT_FUEL, IT_GUNPARTS, IT_INTEL, IT_BAT});
        break;
    case 1: id = pickByRarity(rng, q, {IT_BANDAGE, IT_MEDKIT}); break;
    case 2: id = pickByRarity(rng, q, {IT_AMMO_LIGHT, IT_AMMO_SHELL, IT_AMMO_RIFLE, IT_AMMO_SNIPER, IT_ROCKET}); break;
    case 3: {
        id = pickByRarity(rng, q, {IT_PISTOL, IT_SMG, IT_SHOTGUN, IT_RIFLE, IT_SNIPER, IT_LAUNCHER});
        // Now and then it is an elite gun instead: about 4% of guns found, a little more
        // in better loot, from day 4 on (eliteGate). Pistols can turn into any of the three elite handguns and
        // rifles into either elite rifle.
        if (eliteOf(id) != IT_NONE && rng.chance((0.03f + 0.03f * q) * eliteGate())) {
            if (id == IT_PISTOL) { int r = rng.irange(0, 2); id = r == 0 ? IT_M92 : r == 1 ? IT_LUGER : IT_MAGNUM; }
            else if (id == IT_RIFLE) id = rng.chance(0.5f) ? IT_AK47 : IT_M15;
            else id = eliteOf(id);
        }
        break;
    }
    case 4: id = pickByRarity(rng, q, {IT_VEST_LIGHT, IT_VEST_HEAVY}); break;
    case 5: id = pickByRarity(rng, q, {IT_PACK_SMALL, IT_PACK_LARGE}); break;
    default: id = IT_GRENADE; break;
    }

    Item it = makeItem(id, 1);
    const ItemDef& d = itemDef(id);
    switch (id) {
    case IT_AMMO_LIGHT: it.count = (int16_t)rng.irange(12, 40); break;
    case IT_AMMO_SHELL: it.count = (int16_t)rng.irange(5, 14); break;
    case IT_AMMO_RIFLE: it.count = (int16_t)rng.irange(10, 30); break;
    case IT_AMMO_SNIPER: it.count = (int16_t)rng.irange(3, 10); break;
    case IT_ROCKET: it.count = (int16_t)rng.irange(1, 2); break;
    case IT_GRENADE: it.count = (int16_t)rng.irange(1, 2); break;
    default:
        if (d.stack > 1 && d.rarity == 0) it.count = (int16_t)rng.irange(1, std::min(d.stack, 4));
        break;
    }
    if (const WeaponDef* wd = weaponDef(id)) {
        it.data = rng.irange(0, wd->magSize);
        if (!d.elite) it.tier = (int8_t)rollWeaponTier(rng, q);
    }
    if (d.cat == Cat::Armor) it.data = (int)(d.param * rng.range(0.4f, 1.0f));
    return it;
}
