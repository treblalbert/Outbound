#pragma once
#include "core.h"
#include "items.h"
#include "render.h"
#include "vehicle.h"
#include "world.h"
#include <string>
#include <vector>

struct GLFWwindow;

// ---- tuning ---------------------------------------------------------------
constexpr float DAY_START_MIN = 6 * 60;     // 06:00
constexpr float CURFEW_MIN = 22 * 60;       // 22:00, the night comes
constexpr float LATEST_DEPART_MIN = 21 * 60;
constexpr float GAME_MINUTES_PER_SEC = 1.6f; // 16 in-game hours ~= 10 real minutes
constexpr int STASH_SLOTS = 60;
constexpr int PRIVATE_STASH_SLOTS = 24;   // your own corner of the bunker, shared or not
constexpr int SAVE_SLOTS = 3;
// Base 8, Deep Pockets up to +16 on its own, plus a large backpack's 8. The hard cap
// must sit above what every route can reach: 8 + 16 + 8 = 32 is the fully-paid-for
// loadout, so the array (and save) is sized past that at 48.
constexpr int INV_MAX_SLOTS = 48;
constexpr int GAMEPLAY_INV_MAX = 32;   // 8 base + 16 Deep Pockets + 8 large pack
constexpr int BASE_INV_SLOTS = 8;

enum class Scene { Menu, Credits, Controls, Intro, Slots, Base, Raid, Defense, Lobby, Splash, LocalLobby };

enum class Panel {
    None, Inventory, Loot, Map, Pause, Controls, Stash, Trader, Workbench, Bed, ExitConfirm, Summary, ConfirmNewGame, QuitConfirm, Mission, Recruit, Tutorial, Options, CryptIntro, Crafter,
    Mechanic, MechanicTalk,  // 0.11v: the mechanic's yard, and meeting him
    TraderTalk               // 0.11v: the trader brings the first dungeon locator
};

enum UpgradeId { UP_VITALITY, UP_ENDURANCE, UP_POCKETS, UP_AGILITY, UP_STEADY, UP_NIGHTEYE, UP_TOUGH, UP_LASER, UP_COUNT };

struct UpgradeDef {
    const char* name;
    const char* desc;
    int maxLevel;
    int baseCost;
};
const UpgradeDef& upgradeDef(int id);
int upgradeCost(int id, int currentLevel);

// ---- base defense -----------------------------------------------------------
// Turrets stand in the compound around the hatch. The compound is laid out the same
// way every day, so turrets are stored as tile offsets from the hatch and rebuilt
// into whatever world the day generates.
enum TurretType : int { TT_GUN, TT_AUTO, TT_FLAME, TT_LASER, TT_ROCKET, TT_COUNT };
constexpr int TURRET_MAX_LEVEL = 5;
constexpr int MAX_TURRETS = 24;
constexpr int BUILD_RADIUS = 8;           // tiles from the hatch, inside the fence

struct TurretDef {
    const char* name;
    const char* desc;
    int unlockCost;       // one-off research cost at the defense console
    int unlockWave;       // hordes you must have repelled first
    int buildCost;
    float damage, rate, range, hp;
    int color;
};
const TurretDef& turretDef(int type);

struct Turret {
    int dx = 0, dy = 0;          // tile offset from the hatch
    int type = TT_GUN;
    int level = 1;
    float hp = -1;               // -1 = full; 0 = wrecked until repaired
    // Runtime only.
    float angle = -PI / 2, cd = 0, retargetT = 0, flashT = 0, hurtT = 0, beamT = 0;
    Vec2 beamEnd;
    int target = -1;
};

// ---- barricades (0.12v): walls and gates built round the hatch from the pack's
// Objects/Buildable art. The dead have to break them to get past.
enum BarricadeType : int { BT_WOOD_WALL, BT_WOOD_GATE, BT_REINF_WALL, BT_REINF_GATE, BT_COUNT };
struct BarricadeDef {
    const char* name;
    const char* desc;
    const char* icon;       // UI/Inventory/Objects
    int cost, hp;
    bool gate, reinforced;
    int unlockHordes;       // hordes repelled before it can be built
};
const BarricadeDef& barricadeDef(int type);
struct Barricade {
    int dx = 0, dy = 0;     // tile offset from the hatch
    int type = BT_WOOD_WALL;
    float hp = -1;          // -1 = full; 0 = broken until repaired
    float hurtT = 0;        // runtime
};
constexpr int MAX_BARRICADES = 60;
float barricadeMaxHp(const Barricade& b);
int barricadeRepairCost(const Barricade& b);

enum DefenseUpgrade { DU_FIREPOWER, DU_RATE, DU_PLATING, DU_FORTIFY, DU_BOUNTY, DU_COUNT };
const UpgradeDef& defenseUpgradeDef(int id);
int defenseUpgradeCost(int id, int currentLevel);

struct TurretStats { float damage, rate, range, maxHp; };
TurretStats turretStats(const Turret& t);
int turretUpgradeCost(const Turret& t);   // cost to go to the next level
int turretRepairCost(const Turret& t);
int turretSellValue(const Turret& t);
float baseMaxHp();
int hordeSize(int hordeNumber);
void rollNextHorde();                     // schedules the next horde from now
// Minutes since 00:00 on day 1: the clock hordes are scheduled on.
float absMinutes();
// A horde scheduled for tonight, between 22:00 and 06:00. It holds the night off:
// no sleeping through it, and the dead stay killable until it has been fought.
bool hordeDueTonight();
// A horde still to come before 06:00 tomorrow: sleeping only lasts until it lands.
bool hordeBeforeMorning();
// "13:40" today, or "Day 3 13:40" further out.
std::string hordeWhen();
// Game time left until the next horde, e.g. "1d 05h", "3h 20m", "12 min", "now".
std::string hordeCountdown();
// Palette colour for that countdown: calm while it is far off, red when it is close.
int hordeCountdownColor();

// ---- hired help -------------------------------------------------------------
constexpr int SQUAD_MAX = 3;
constexpr int HIRE_TIERS = 5;
struct HireTier {
    const char* name;
    int price;
    int minDay;
    int weapon;
    float hp;
    float damageMul;
    float spread;         // extra aim error in radians
    float sight;
    bool helmet;
};
const HireTier& hireTier(int tier);

struct Hireling {
    std::string name;
    int tier = 0;
    float hp = 100;
    int kills = 0;
    bool guard = false;          // true = holds the base, false = follows you
    // Runtime only.
    Vec2 pos, lastPos, post;
    float angle = 0, fireCd = 0, reloadT = 0, hurtT = 0, flashT = 0, retargetT = 0, stuckT = 0, unstickT = 0;
    float meleeT = 9;            // 0.12v: since their last punch (the dead too close to shoot)
    Vec2 unstickDir;
    int mag = 0;
    int target = -1;
    bool dead = false;
    // Co-op (and the bunker guards): whose they are, and whether they are out in the
    // world right now rather than down in the bunker with their owner.
    int owner = 0;
    bool out = false;
    int rideCar = -1;            // 0.11v: riding in that player's car (shooting from it), -1 on foot
    float maxHp() const;
};

// One selected contract. The board offers three a day, rolled from the save's seed,
// and they get bigger and pay more as the days go on.
//   MT_TECH    bring specific electronics / valuables    (delivered)
//   MT_BOUNTY  kill hostiles, often one specific kind    (counted as you go)
//   MT_WEAPON  bring a specific weapon                   (delivered)
//   MT_SUPPLY  bring specific supplies                   (delivered)
//   MT_GEAR    bring armor, a backpack or medkits        (delivered)
//   MT_CULL    kill horde zombies                        (counted as you go)
// Delivered contracts are handed over at the mission board: the items are taken
// from your stash, pockets or hands. Up to two different items can be asked for.
enum MissionType { MT_TECH, MT_BOUNTY, MT_WEAPON, MT_SUPPLY, MT_GEAR, MT_CULL, MT_COUNT };
struct DayMission {
    int day = 0;               // which day this mission belongs to (0 = none yet)
    int type = 0;
    int target = 0;            // count for itemId, or kills
    int reward = 0;
    int collected = 0;         // kills so far (kill contracts only)
    int itemId = IT_NONE;
    int itemId2 = IT_NONE;     // optional second item
    int target2 = 0;
    int enemy = -1;            // bounty: EnemyType to hunt, -1 = any raider
    bool claimed = false;
    bool deliver() const { return type != MT_BOUNTY && type != MT_CULL; }
    bool active() const { return target > 0 && !claimed; }
};
// How many of an item you could hand over right now (stash, pockets, hands, armor).
int missionHave(int itemId);
bool missionDone(const DayMission& m);
// "Graphics Card 0/1  Battery 2/3" or "Heavies 1/4".
std::string missionProgress(const DayMission& m);

// What you did outside today. The world itself is rebuilt from the day's seed, so
// only the things you changed have to be remembered: where you have been, who you
// killed, and which containers you emptied.
struct DayMemory {
    int day = 0;                                    // 0 = nothing remembered
    std::vector<uint8_t> explored;                  // one bit per tile
    std::vector<uint8_t> spawnDead;                 // one byte per generated spawn
    std::vector<int> openedIdx;                     // generated containers you opened
    std::vector<std::vector<Item>> openedItems;     // what is left in them
    std::vector<Container> dropped;                 // corpses and bags made during the raid
    void clear() { *this = DayMemory(); }
};

// Where you stood when the game was closed mid-raid, so Continue drops you back
// outside at the same minute instead of restarting the day. The world is rebuilt
// from the day's seed plus DayMemory; this holds the rest.
struct RaidResume {
    bool valid = false;
    Vec2 pos;
    float angle = 0;
    float startMin = 0;          // when this trip out began, for the summary
    int kills = 0;
    int hordeN = 0, hordeLeft = 0;   // a horde still coming or on the map
    struct Foe { int idx; Vec2 pos; float hp; };
    std::vector<Foe> foes;       // generated enemies still alive, where they stood
};

// How a save is played, picked when the game is started (0.9v).
//   Normal:   dying costs a random half of what you carried.
//   Hardcore: dying costs all of it, everything hits harder, and the minimap, the way
//             home and where your friends are have to be bought back at the trader.
enum Difficulty { DIFF_NORMAL, DIFF_HARDCORE, DIFF_COUNT };
//   Normal: raiders roam the zone.  Zombies: only the dead, many more, in packs.
enum GameMode { MODE_NORMAL, MODE_ZOMBIES, MODE_COUNT };
// Hardcore's unlockables, sold by the trader.
enum HardcoreUnlock { HC_MAP, HC_HOME, HC_TEAM, HC_COUNT };
struct HardcoreUnlockDef { const char* name; const char* desc; int cost; };
const HardcoreUnlockDef& hardcoreUnlockDef(int id);

struct Profile {
    uint64_t worldSeed = 0;
    int difficulty = DIFF_NORMAL;
    int gameMode = MODE_NORMAL;
    // Rivals (co-op only, 0.10v): every player has a bunker of their own somewhere in
    // the world, anyone can shoot anyone, nothing shows where the others are, and
    // there are no hordes to defend against together.
    bool rivals = false;
    // Who you are (0.10v): shirt colour (Assets::shirt) and the name other players see
    // in co-op ("" = your Steam name).
    int shirt = 0;
    std::string charName;
    // The catacombs: the first-time explanation has been shown, and which of today's
    // catacombs you died in (closed to you until tomorrow: bit per catacomb).
    bool cryptIntroSeen = false;
    // The day you bought a dungeon locator for: it points at that day's catacombs.
    int locatorDay = 0;
    bool locatorGift = false;       // 0.11v: the trader has brought you your first one (day 3)
    bool hasLocator() const { return locatorDay == day; }
    int locatorPrice() const { return hardcore() ? 500 : 250; }
    int cryptBanDay = 0, cryptBanMask = 0;
    bool cryptBanned(int idx) const { return cryptBanDay == day && idx >= 0 && (cryptBanMask >> idx) & 1; }
    // Catacombs finished today (0.11v): you were down there with the way back open,
    // which only the last room can do. Their markers are no use any more.
    int cryptDoneDay = 0, cryptDoneMask = 0;
    bool cryptDone(int idx) const { return cryptDoneDay == day && idx >= 0 && (cryptDoneMask >> idx) & 1; }
    bool hcUnlock[HC_COUNT] = {};   // fixed per save; the daily world is mix64(worldSeed ^ day)
    // Cars (0.11v). What the mechanic has sold you (colour, health and fuel of each;
    // -1 = full), which one you take out, and where it was left today. Overnight, or
    // after you die, he tows it back to his yard.
    struct OwnedCar { bool owned = false; int color = 0; float hp = -1, fuel = -1; };
    OwnedCar cars[CAR_MODELS];
    int activeCar = -1;
    bool mechanicMet = false;
    int carDay = 0, carRev = 0;
    Vec2 carPos;
    float carAngle = 0;
    bool carOut() const { return activeCar >= 0 && carDay == day && carRev == dayRev; }
    // Mixed into this character's contract board and hire names, so co-op players
    // sharing a world each get their own offers. 0 for the host / solo.
    uint64_t missionSalt = 0;
    DayMemory dayMem;
    int day = 1;
    // Which layout of today's world you are on. 0 is the one you first stepped out
    // into, and a death on the same day rolls the next one, so you never walk back
    // out into the world that just killed you. Extracting normally leaves it alone,
    // which is what keeps the day reversible: sleep advances the day and resets it.
    int dayRev = 0;
    float timeMin = DAY_START_MIN;
    int money = 250;
    int up[UP_COUNT] = {};
    std::vector<Item> stash = std::vector<Item>(STASH_SLOTS);            // in co-op: the host's is everyone's
    std::vector<Item> privStash = std::vector<Item>(PRIVATE_STASH_SLOTS);
    std::vector<Item> inv = std::vector<Item>(INV_MAX_SLOTS);
    Item weapons[2];
    Item armor;
    Item backpack;
    int curWeapon = 0;
    float hp = 100;
    int raids = 0, extractions = 0, deaths = 0, kills = 0;
    int earned = 0;
    bool inRaid = false;
    RaidResume resume;        // only meaningful while inRaid
    DayMission mission;       // today's mission; rerolled when the day changes
    int missionsCompleted = 0;
    bool laserUnlocked = false;
    bool laserOwned = false;     // 0.11v: has ever had a laser fitted (opens the Laser Focus upgrade)
    bool laserOn = false;        // before 0.7v: one switch for every gun (read from old saves only)

    // Base defense. `hordeNum` is the next horde's number; `hordesRepelled` gates
    // turret unlocks. nextHordeAt is on the absolute clock (see absMinutes), so a
    // horde can be scheduled days ahead.
    std::vector<Turret> turrets;
    std::vector<Barricade> barricades;   // 0.12v
    bool turretUnlocked[TT_COUNT] = {true, false, false, false, false};
    int defUp[DU_COUNT] = {};
    float baseHp = -1;           // -1 = full
    int hordeNum = 1;
    int hordesRepelled = 0;
    int hordeKills = 0;
    float nextHordeAt = 1440 + 12 * 60;   // day 2, around noon
    // The day whose night was spent fighting a horde. That night stays an ordinary
    // dark one (no immortal dead) and the hatch opens again once the horde is gone.
    int safeNight = 0;
    std::vector<Hireling> squad;
    int hires = 0;               // total ever hired, seeds the next name
    bool tutorialDone = false;   // has had the bunker tour
    void giveDefaultTurrets();

    // Active reload (Gears of War style). Starting a reload fills a bar with a marked
    // "active" band; tapping reload again while the marker sits inside that band gives a
    // perfect reload and loads a stronger magazine. Mistime it early and the reload is
    // slower and plain. magBonus rides on the magazine that got loaded, and is cleared
    // when that magazine is next swapped out.
    enum class ActivePhase { None, Filling, Perfect, Failed };
    ActivePhase activePhase = ActivePhase::None;
    float activeMarker = 0;        // 0..1 progress through the reload bar
    float activeZoneA = 0.5f;      // band start
    float activeZoneB = 0.7f;      // band end
    float activeResultT = 0;       // how long the PERFECT/FAILED flash stays up
    float magBonus = 1.0f;         // damage multiplier on the loaded magazine

    int invCapacity() const;
    bool hardcore() const { return difficulty == DIFF_HARDCORE; }
    bool zombieMode() const { return gameMode == MODE_ZOMBIES; }
    // What the HUD may show: always on Normal, bought on Hardcore.
    bool hasMap() const { return !hardcore() || hcUnlock[HC_MAP]; }
    bool hasHomeMarker() const { return !hardcore() || hcUnlock[HC_HOME]; }
    bool hasTeamMarkers() const { return !hardcore() || hcUnlock[HC_TEAM]; }
    float maxHp() const { return 110.0f + up[UP_VITALITY] * 20.0f; }
    float maxStamina() const { return 100.0f + up[UP_ENDURANCE] * 25.0f; }
    float moveMul() const { return 1.0f + up[UP_AGILITY] * 0.06f; }
    // The laser in use right now: fitted to the gun in hand (0.11v: per gun, at the
    // crafter) and switched on.
    bool laserActive() const { const Item& w = weapons[curWeapon]; return !w.empty() && (w.flags & ITEMF_LASER) && !(w.flags & ITEMF_LASER_OFF); }
    float spreadMul() const { return (0.92f - up[UP_STEADY] * 0.12f) * (laserActive() ? (0.78f - up[UP_LASER] * 0.06f) : 1.0f); }
    float reloadMul() const { return 1.0f - up[UP_STEADY] * 0.10f; }
    float lightMul() const { return 1.0f + up[UP_NIGHTEYE] * 0.25f; }
    float damageMul() const { return 1.0f - up[UP_TOUGH] * 0.07f; }
};

struct Player {
    Vec2 pos;
    float angle = 0;
    float stamina = 100;
    float fireCd = 0;
    float reloadT = 0;
    float hurtT = 0;
    float healCd = 0;
    float stepT = 0;
    bool sprinting = false;
    bool exhausted = false;
    bool moving = false;
    float flashT = 0;
    // Bleeding: a bullet now and then opens a wound that drains health slowly until a
    // bandage or medkit closes it, or it stops by itself. Never fatal on its own.
    float bleedT = 0;          // seconds of bleeding left, 0 = not bleeding
    float bleedImmuneT = 0;    // a fresh dressing holds for a while
    float bleedDripT = 0;
    // 0.12v: what the body is doing besides running and shooting (Art::Anim Punch,
    // PickUp; Shoot for the gun's own recoil), and for how long.
    int act = 0;               // 0 none, 1 shoot, 2 punch/swing, 3 pick up
    float actT = 9;
    float meleeCd = 0;
    float padR3T = -1;         // a controller's R3: tap to hit, hold for the laser
    bool padR3Held = false;
};

constexpr float BLEED_CHANCE = 0.07f;      // per bullet that hits you (half with armor on)
constexpr float BLEED_RATE = 0.45f;        // health per second
constexpr float BLEED_FLOOR = 10.0f;       // it stops draining here: a nuisance, not a killer

enum class AIState { Idle, Alert, Combat };

struct Enemy {
    EnemyType type = EnemyType::Scav;
    Vec2 pos, home, wanderTarget, lastSeen, lastPos;
    float hp = 50, maxHp = 50;
    float angle = 0;
    AIState state = AIState::Idle;
    float alertT = 0, fireCd = 0, reloadT = 0, reactT = 0, losT = 0, wanderT = 0;
    float strafeT = 0, strafeDir = 1, hurtT = 0, meleeCd = 0, stuckT = 0, speedMul = 1;
    Vec2 unstickDir;
    float unstickT = 0;
    bool canSee = false;
    int weapon = IT_NONE;
    int mag = 0;
    uint8_t artVariant = 0;
    float flashT = 0;
    bool dead = false;
    // Horde zombies: which of the three zombie kinds, and what they are going for
    // (-1 the hatch, -2 the player, 0.. a hireling, 1000.. a turret).
    int zkind = 0;
    int ztarget = -1;
    float retargetT = 0;
    int spawnIdx = -1;         // index into World::spawns, -1 for anything spawned later
    bool roamer = false;       // Zombies mode: one of the zone's own dead, not a horde's
    // Co-op: the id guests know this enemy by, and which player it is after.
    uint32_t netId = 0;
    int tslot = -1;
    // 0.11v: shot by someone it could not see (a merc out of its sight): for a while it
    // looks further, so it finds them and answers.
    float provokedT = 0;
    int patrol = -1;           // a city gang on patrol (World::patrols), -1 none
    // 0.12v: the axe zombie throws its axe, fights bare-handed, and takes it back up.
    bool noAxe = false;
    float axeCd = 3, takeT = -1;
};

struct Bullet {
    Vec2 pos, vel;
    float damage = 10, rangeLeft = 300, tileMul = 1;
    bool fromPlayer = true, explosive = false, pierce = false;
    bool turret = false;       // fired by base defenses: never hurts terrain or friends
    int weapon = IT_NONE;
    std::vector<int> hitList;
    // Co-op: who fired it (a player slot, 10+slot for their merc, -1 anyone else).
    // A cosmetic bullet is a copy of someone else's shot: it flies and stops but
    // hurts nothing. noTiles leaves walls to the host; reportHits sends hits to it.
    int owner = -1;
    bool cosmetic = false, noTiles = false, reportHits = false;
    Vec2 origin;               // where it was fired from (who shot is who gets answered)
};

struct Grenade { Vec2 pos, vel; float fuse = 1.6f; int owner = -1; bool cosmetic = false; };

struct Particle {
    Vec2 pos, vel;
    float life = 1, maxLife = 1, size = 1, drag = 3;
    int color = P_WHITE;
    bool glow = false;
};

struct Decal { Vec2 pos; int sprite; float angle; };
struct FloatText { Vec2 pos; std::string text; int color; float life; };
struct Flash { Vec2 pos; float radius, life, maxLife, intensity; Color color; };
struct Message { std::string text; int color; float life; };

struct RaidSummary {
    bool died = false;
    std::string cause;
    int kills = 0;
    int value = 0;
    float minutes = 0;
    int dayAfter = 1;
    std::string hordeNote;     // how a horde went while you were not there to see it
    int lost = 0, carried = 0; // things (stacks, guns, gear) lost on dying, out of how many
    bool slept = false;        // the note is all there is: a horde hit while you slept
};

struct Game {
    GLFWwindow* window = nullptr;
    int winW = 1280, winH = 720;
    bool quit = false;
    float realTime = 0;
    float frameDt = 0;       // this frame, for draw-time animation

    Scene scene = Scene::Menu;
    Panel panel = Panel::None;

    Profile prof;
    World world;
    World baseWorld;
    World menuWorld;

    Player player;
    std::vector<Enemy> enemies;
    std::vector<Bullet> bullets;
    std::vector<Grenade> grenades;
    std::vector<Particle> particles;
    std::vector<Decal> decals;
    std::vector<FloatText> floatTexts;
    std::vector<Flash> flashes;
    std::vector<Message> messages;

    Vec2 cam;
    Vec2 drawCam;             // camera actually used this frame (includes shake)
    float shake = 0;

    int lootContainer = -1;
    float searchT = 0;
    bool nightFallen = false;
    float shadeSpawnT = 0;
    int warnStage = 0;
    float flowT = 0, revealT = 0, mapUploadT = 0;
    float raidStartMin = 0;
    int raidKills = 0;
    Texture mapTex;
    int mapTexW = 0, mapTexH = 0;

    RaidSummary summary;
    LightingParams lighting;

    std::vector<std::string> credits;
    bool devNoSave = false;   // set by the dev launch flags so testing never writes a slot
    // After leaving someone else's co-op game, G.prof is their copy of our character:
    // never write it over a local slot. Loading or starting a slot clears it.
    bool coopNoSave = false;
    bool devClean = false;    // --promo: no cursor, no mouse lean, for screenshots
    bool devNoHud = false;    // --nohud: nothing over the raid but the world (trailer shots)
    bool devGod = false;      // --godmode: nothing can hurt you (long trailer takes)
    int saveSlot = 0;         // which of the SAVE_SLOTS files this session writes to
    int pendingSlot = 0;      // slot awaiting confirmation in the menu
    int traderPage = 0;
    std::string notice;       // one-line feedback shown in base panels
    float noticeT = 0;
};

extern Game G;

// ---- scenes ---------------------------------------------------------------
void menu_init();
// The "Made with the Freeman engine" card shown when the game starts.
void menu_splash();
void menu_update(float dt);
void menu_draw();

void base_enter(bool fromRaid);
void base_update(float dt);
void base_draw();

void raid_start();
// Continue a raid saved mid-day (Profile::resume), at the same time and place.
void raid_resume();
// Records the raid in progress and writes the save, so closing the game mid-raid
// can be picked up later. A player already going down finishes dying instead.
void raid_saveState();
void raid_update(float dt);
void raid_draw();

void defense_enter();
void defense_update(float dt);
void defense_draw();
// One change to the shared defenses (never touches money). a/b/c: DO_BUILD type,dx,dy;
// DO_UNLOCK type; DO_RESEARCH upgrade id; DO_UPGRADE/DO_REPAIR/DO_SELL -,dx,dy.
// DO_BARR_BUILD type,dx,dy; DO_BARR_SELL/DO_BARR_REPAIR -,dx,dy (0.12v).
enum DefenseOp { DO_BUILD, DO_UNLOCK, DO_REPAIR_ALL, DO_RESEARCH, DO_UPGRADE, DO_REPAIR, DO_SELL, DO_REPAIR_BASE,
                 DO_BARR_BUILD, DO_BARR_SELL, DO_BARR_REPAIR, DO_BARR_REPAIR_ALL };
bool defense_apply(int op, int a, int b, int c);

// The world for today's layout, shared by the raid and the defense editor.
uint64_t todaySeed();
// Writes the saved turrets into a freshly generated world as solid tiles.
void placeTurretsInWorld(World& w);
// A horde due on a day you slept through still comes: it is fought at the bunker by
// your turrets and guards while you are not there. Returns what happened, or "".
std::string raid_missedHorde();
// Primitive-drawn turret art, shared by the raid and the defense editor.
void drawTurret(const Turret& t, Vec2 tileCenter, float alpha);
// Where people's feet are this frame (0.12v): the grass under them is drawn trodden.
void setSteppers(const std::vector<Vec2>& feet);
// How far open a gate tile is, 0..1 (raid.cpp keeps it moving).
float gateOpenness(int tx, int ty);

// ---- shared helpers -------------------------------------------------------
// What you wake up holding after dying (or after abandoning a raid): nothing you
// were carrying, but always a sidearm with a full magazine and spare rounds, so a
// death is a setback rather than a dead end.
void grantStarterKit();
// What dying costs (see Difficulty): the lost things are taken off the character and
// returned (a co-op body holds them). Never leaves you without a gun. Fills in
// G.summary.lost / carried.
std::vector<Item> applyDeathLoss();
const char* difficultyName(int d);
const char* gameModeName(int m);

// A slot's headline numbers, read without disturbing the loaded profile.
struct SaveInfo {
    bool exists = false;
    int day = 1, money = 0, raids = 0, extractions = 0, kills = 0;
    bool inRaid = false;       // closed while outside; Continue resumes the raid
    float timeMin = 0;
    int difficulty = DIFF_NORMAL, gameMode = MODE_NORMAL;
    bool rivals = false;
};
SaveInfo save_info(int slot);

void new_game(int difficulty = DIFF_NORMAL, int mode = MODE_NORMAL, const std::string& name = "", int shirt = 0, bool rivals = false);
// A character name as typed: printable, trimmed, at most 16 characters.
std::string cleanCharName(const std::string& s);
// A profile as save-file text and back (also how co-op characters travel).
std::string profileToText(const Profile& p);
bool profileFromText(const std::string& text, Profile& out);
// Where a hosted slot keeps its co-op guests' characters.
std::string coopGuestDir();
// Opens a web page in the player's browser (a new tab on the web build).
void openUrl(const std::string& url);
bool save_game();
bool load_game(int slot);
int last_slot();           // the slot played last, if its save is still there; -1 if not
bool save_exists(int slot);
bool delete_save(int slot);
bool any_save_exists();
void migrate_saves();      // moves a pre-slots save into slot 1

// ---- co-op hooks (raid.cpp / base.cpp); see coop.h
namespace Net { struct Reader; }
bool raid_worldLive();
void raid_coopTick(float dt);              // host: run the outside while not in it
std::string raid_coopOffscreenHorde();     // host: a horde nobody is outside for
void raid_coopPlayersChanged();            // host: someone joined or left
void raid_coopEnded();                     // session over: forget shared world state
void raid_netMessage(int slot, uint8_t type, Net::Reader& r);
void raid_netSend(float dt);
void raid_hordeState(bool& active, int& n, int& left);
void raid_setHordeState(bool active, int n, int left, bool nightFallen);   // guest
void raid_forceHome();                     // back to the bunker, keeping what you carry
bool raid_localDowned(float& bleedLeft);
void raid_bigText(const std::string& text, int color);
// Other players' characters: queued with the scene sprites, then their name tags.
void drawNetPlayers(uint8_t where);
// includeSelf: local co-op draws your own name too, so everyone can tell who is who.
void drawNetPlayerTags(uint8_t where, Vec2 cam, bool includeSelf = false);
// ---- local co-op (0.12v, see local.h): a seat's own state, swapped in for its turn.
void raid_swapSeat(int seat);
void base_swapSeat(int seat);
void raid_seatJoin(Vec2 near);      // (in the seat's turn) a player joined outside
void base_seatJoin(Vec2 near);      // ... or in the bunker
void raid_seatLeave();              // (in the seat's turn) that player drops out
bool raid_localOut();               // bled out: waiting for the others to come home
void raid_localEnded();             // the session is over: back to one player
int defense_seat();                 // which seat opened the defense console
std::string base_coopSleep(bool nightRanOut);   // host: everyone slept; returns a note
void base_coopWoke(const std::string& note, bool nightRanOut);   // guest: the host's sleep
// A horde is due or under way: everyone underground is sent up to fight it after a
// short warning. Called by the bunker and the defense console; true once it has gone.
bool base_hordeCall(float dt);
// Hardcore (solo): the clock keeps running in the bunker and at the defense console.
void base_hardcoreClock(float dt);
void base_drawHordeCall();         // its warning banner
bool raid_hordeOn();                       // a horde is out there right now
int raid_localCrypt();                     // which catacomb you are in, -1 none
int raid_localRide();                      // whose car you are in (0.11v), -1 on foot
// Rivals: where that player's own hatch is in today's world.
Vec2 rivalHatch(int slot);
// The hatch you use: your own in Rivals, the shared one otherwise.
Vec2 myHatch();

// Within-day raid memory (raid.cpp).
void captureDayMemory();
void applyDayMemory();

void pushMessage(const std::string& text, int color = P_WHITE);
void setNotice(const std::string& text);
void rollDailyMission();               // fresh mission for G.prof.day (base.cpp)
void missionAddLoot(int itemId);       // track a looted item (base.cpp)
void missionAddKill(int enemyType);    // track a kill (base.cpp)
const char* missionTitle(const DayMission& mission);
float ambientBrightness(float minutes);
Color ambientColor(float minutes);

// World drawing shared by menu, base and raid.
void drawWorldTiles(World& w, Vec2 cam, float timeSec);
void drawTileSolids(World& w, Vec2 cam, float timeSec);
// Catacomb spike traps: up (and hurting) for a moment every couple of seconds.
bool spikesUp(const WorldProp& p, float t);
// Roofs go on last so they hide whatever is inside; the one you are standing under
// fades away. Call after sceneFlush(), still inside the world layer.
void drawRoofs(World& w, Vec2 cam, Vec2 viewer, float dt);
// Set by the raid: how far each roof tile is see-through right now because you are
// looking into the building from outside (0 = solid .. 1 = gone). nullptr = never.
extern float (*g_roofPeek)(int tx, int ty);

// Sprites that stand on the ground are collected and drawn back-to-front so tall
// art (trees, cars, characters) overlaps correctly.
namespace Art { struct Piece; }
void sceneBegin();
void sceneAdd(const Art::Piece& piece, Vec2 baseline, Color tint = Color(), float scale = 1, float yBias = 0);
void sceneAddCentered(const Art::Piece& piece, Vec2 center, Color tint = Color(), float scale = 1, float yBias = 0);
void sceneAddSprite(int fallbackSpriteId, Vec2 center, Color tint = Color(), float angle = 0, float scale = 1);
void sceneFlush();
// The sprite just added lies flat (a body, a bag): it casts no reflection.
void sceneNoReflect();
// The sprite just added has this many empty pixel rows under what it shows (a car's
// turning frames): its shadow, reflection and contact shade start above them.
void sceneLift(float rows);
float spriteEmptyRowsBelow(const Assets::Sprite* s, int frame);
// Sun shadows for the scene being drawn: the world whose roofs keep the sun off what is
// under them (nullptr indoors: no sun at all), and the sun for this time of day.
void sceneSetWorld(const World* w);
void setSunForTime(float minutes);
// How far on a lamp is at this time of day (0 off .. 1 on). Every lamp has its own
// moment to come on at dusk and go off at dawn, picked from `seed`, and flickers as it
// warms up; a dark storm switches them on early. `bright` is the day's light (0..1).
float lampOn(float minutes, float bright, uint32_t seed);

// Inventory / equipment helpers (inventory.cpp).
bool equipFrom(std::vector<Item>& src, int index);
bool unequip(int slot, std::vector<Item>* dest = nullptr, int destSlots = -1);   // 0,1 weapons 2 armor 3 backpack
bool useItemAt(std::vector<Item>& src, int index);
int moveItem(std::vector<Item>& src, int index, std::vector<Item>& dst, int dstSlots);
void compactInventory();
void sortInventory();

enum class InvMode { Raid, Base, Loot, Stash, Trader };
void drawInventoryPanel(float x, float y, InvMode mode, std::vector<Item>* other, int otherSlots);
void drawSlotGrid(float x, float y, std::vector<Item>& slots, int count, int cols, InvMode mode, bool isOther);
