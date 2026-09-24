#pragma once
#include "core.h"
#include <string>
#include <vector>

enum ItemId : int {
    IT_NONE = 0,
    IT_SCRAP, IT_WIRES, IT_BOLTS, IT_TAPE, IT_BATTERY, IT_CIRCUIT, IT_WATCH, IT_GPU, IT_JEWELRY,
    IT_FOOD, IT_MEDSUP, IT_FUEL, IT_GUNPARTS, IT_INTEL,
    IT_BANDAGE, IT_MEDKIT, IT_GRENADE,
    IT_AMMO_LIGHT, IT_AMMO_SHELL, IT_AMMO_RIFLE, IT_AMMO_SNIPER, IT_ROCKET,
    IT_PISTOL, IT_SMG, IT_SHOTGUN, IT_RIFLE, IT_SNIPER, IT_LAUNCHER,
    IT_VEST_LIGHT, IT_VEST_HEAVY, IT_PACK_SMALL, IT_PACK_LARGE,
    IT_REVOLVER, IT_CARBINE,
    // Elite guns: better versions of the ones above, never sold, only found.
    IT_M92, IT_LUGER, IT_MAGNUM, IT_MP5, IT_M15, IT_AK47, IT_M24,
    // 0.12v
    IT_BAT, IT_SOUP,
    IT_COUNT
};

enum class Cat { None, Valuable, Medical, Throwable, Ammo, Weapon, Armor, Backpack, Melee };   // Melee: 0.12v, its own slot

struct ItemDef {
    const char* name;
    Cat cat;
    int value;     // trader sell price per unit
    int stack;     // max stack size
    int sprite;
    int param;     // heal amount / armor points / backpack slots
    int rarity;    // 0 common .. 3 very rare
    const char* desc;
    bool elite = false;   // a special, better-than-normal gun that can only be found
};

struct WeaponDef {
    int ammo;
    float damage;
    float fireRate;     // shots per second
    int magSize;
    float reloadTime;
    float spread;       // radians
    int pellets;
    float bulletSpeed;
    float range;
    bool automatic;
    bool explosive;
    float tileDamage;   // multiplier vs terrain
    float shake;
    int sound;
};

struct Item {
    int16_t id = IT_NONE;
    int16_t count = 0;
    int32_t data = 0;  // weapon: rounds in magazine, armor: durability
    int8_t tier = 0;   // weapons: see ItemTier (0 = uncommon, the ordinary shop gun)
    uint8_t flags = 0; // see ItemFlag
    bool empty() const { return id == IT_NONE || count <= 0; }
};

// Weapon tiers, coloured like Fortnite's. Uncommon (green) is how a gun has always
// worked and what the trader sells; guns found in the world roll anything from
// common to epic; the elite guns are always legendary (gold) on their own stats.
// Per-gun switches and the crafter's work on it (0.11v). A laser pointer is fitted to
// one gun (ITEMF_LASER) and has its own on/off, stored as "off" so it starts on. The
// magazine can be extended once (+50%) and then turned into a drum (double).
enum ItemFlag : uint8_t { ITEMF_LASER_OFF = 1, ITEMF_LASER = 2, ITEMF_MAG_EXT = 4, ITEMF_MAG_DRUM = 8 };
// Rounds a gun's magazine holds, with its magazine upgrade.
int magSizeOf(const Item& it);
// How far a laser reaches on a gun of this tier (pixels).
float laserLength(int tier);

enum ItemTier : int { TIER_COMMON = -1, TIER_UNCOMMON = 0, TIER_RARE = 1, TIER_EPIC = 2, TIER_LEGENDARY = 3 };
bool hasTier(const Item& it);                 // weapons only
int itemTier(const Item& it);                 // the effective tier (elite guns: legendary)
const char* tierName(int tier);               // "Rare"
Color tierColor(int tier);
float tierDamage(int tier);                   // multipliers on the gun's own stats
float tierSpread(int tier);
float tierReload(int tier);
// A tier for a gun found in the world; quality 0..1 as for rollLoot (up to 2 in the catacombs).
int rollWeaponTier(Rng& rng, float quality);
// The day the loot is rolled for (0.12v): better tiers and the elite guns open up as
// the days go by, so the first days are spent with plain guns wherever you look.
// World::generate sets it; outside a raid it stays at the default (no limit).
void setLootDay(int day);
int lootDay();
// 0 before day 4, rising to 1 by day 8: how often an elite gun turns up, relative to full.
float eliteGate();

const ItemDef& itemDef(int id);
const WeaponDef* weaponDef(int id);
// The ordinary gun an elite one is a better version of (itself for everything else):
// it decides the art in hand and weapon-specific behaviour.
int baseWeapon(int id);
// The elite version of an ordinary gun, or IT_NONE when there is none.
int eliteOf(int id);
Item makeItem(int id, int count = 1);
int itemValue(const Item& it);
std::string itemLabel(const Item& it);

// Adds to a slot list (stacking first). Returns the amount that did not fit.
int addToSlots(std::vector<Item>& slots, Item it, int usableSlots = -1);
int countInSlots(const std::vector<Item>& slots, int id, int usableSlots = -1);
int takeFromSlots(std::vector<Item>& slots, int id, int amount, int usableSlots = -1);

// Random loot. quality 0..1 raises rarity odds. kind biases categories.
enum class LootKind { Generic, Crate, Locker, Cabinet, Military, Toolbox, Bag };
Item rollLoot(Rng& rng, float quality, LootKind kind);
