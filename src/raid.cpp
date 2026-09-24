// The outside world: exploration, combat, looting and the deadly night.
#include "art.h"
#include "coop.h"
#include "net.h"
#include "prompt.h"
#include "options.h"
#include "atmosphere.h"
#include "audio.h"
#include "game.h"
#include "input.h"
#include "lang.h"
#include "local.h"
#include "sprites.h"
#include "ui.h"
#include "voice.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <ctime>
#include <functional>
#include <queue>
#include <unordered_map>

using namespace Sprites;

float g_devAim = -99;
float g_devThrottle = 0, g_devSteer = 0;
bool g_devPerf = false;
bool g_devRide = false;
bool g_devNoOcc = false;   // --nocc: the old box shadows for the flashlight (comparison)    // --ride (co-op tests): get into the host's car as soon as it is there   // --throttle= / --steer=: driving without hands (tests)

namespace {

struct EnemyDef {
    float hp, speed, sight, spread, fireMul, prefRange;
    int sprite;
    const char* name;
};

const EnemyDef ENEMY_DEFS[6] = {
    {60, 46, 170, 0.12f, 0.55f, 110, SCAV, "Scavenger"},
    {95, 52, 210, 0.08f, 0.65f, 130, BANDIT, "Bandit"},
    {240, 36, 170, 0.10f, 0.80f, 60, HEAVY, "Heavy"},
    {70, 38, 380, 0.025f, 0.45f, 300, SNIPER, "Sniper"},
    {55, 112, 99999, 0, 0, 0, SHADE, "Shade"},
    {45, 44, 90, 0, 0, 0, SHADE, "Zombie"},
};

// The three zombie kinds from the art pack, in the order Art::zombie() uses.
struct ZombieKind { float hp, speed, damage, attackCd, reach; int bounty; };
const ZombieKind ZOMBIE_KINDS[3] = {
    // 0.12v: twice the health and a fifth faster than before. All of them outpace you
    // walking (62); a sprint (96) outruns the big and the axe ones, never the small.
    {90, 108, 7, 0.8f, 11, 6},      // small: quick and fragile
    {520, 79, 24, 1.3f, 14, 25},    // big: soaks bullets, hits like a truck
    {220, 91, 13, 1.0f, 12, 12},    // axe
};

constexpr float PLAYER_R = 5;
constexpr float ENEMY_R = 5;
constexpr float ENEMY_DAMAGE_MUL = 0.42f;

// Every day out is deadlier than the last, which is what makes a day you have
// already cleared worth going back to.
float dayThreat() { return clampf(1.0f + (G.prof.day - 1) * 0.035f, 1.0f, 2.2f); }
constexpr float INTERACT_RANGE = 24;

Rng s_rng(1);

// While a horde is fought out of sight (you went down the hatch mid-attack), the same
// update code runs flat out with no player, sound or effects.
bool s_sim = false;
void sfx(int snd, float volume = 1, float pitch = 1) { if (!s_sim) Audio::play(snd, volume, pitch); }
void sfxAt(int snd, Vec2 pos, Vec2 listener, float volume = 1, float pitch = 1) {
    if (!s_sim) Audio::playAt(snd, pos, listener, volume, pitch);
}

// ---- Rivals: a hatch of your own, somewhere out in the world
bool rivalsOn() { return G.prof.rivals && Coop::active(); }
std::vector<Vec2> s_rivalHatch;     // per slot, for the world they were worked out for
uint64_t s_rivalHatchWorld = 0;
Vec2 findRivalHatch(int slot) {
    World& w = G.world;
    // Each slot's corner of the map comes from the save's seed, so your bunker is about
    // the same place every day; the exact spot is the nearest open ground that day.
    Rng r(mix64(G.prof.worldSeed ^ 0x817A15ull ^ ((uint64_t)slot * 0x9E3779B97F4A7C15ull)));
    float ang = slot * (2 * PI / Coop::MAX_PLAYERS) + r.range(-0.3f, 0.3f);
    float rad = std::min(w.outW, 240) * r.range(0.28f, 0.40f);
    int cx = std::clamp((int)(w.homeTx + std::cos(ang) * rad), 14, w.outW - 15);
    int cy = std::clamp((int)(w.homeTy + std::sin(ang) * rad), 14, w.outH - 15);
    for (int rr = 0; rr < 40; rr++)
        for (int y = cy - rr; y <= cy + rr; y++)
            for (int x = cx - rr; x <= cx + rr; x++) {
                if (std::max(std::abs(x - cx), std::abs(y - cy)) != rr || !w.inBounds(x, y)) continue;
                bool ok = true;
                for (int yy = y - 1; yy <= y + 2 && ok; yy++)
                    for (int xx = x - 1; xx <= x + 1 && ok; xx++) {
                        const Tile& t = w.at(xx, yy);
                        if (t.solid != S_NONE || t.ground == G_WATER || (t.ground >= G_FLOOR_WOOD && t.ground != G_WASTE)) ok = false;
                    }
                if (ok) return World::tileCenter(x, y);
            }
    return w.homePos;
}
Vec2 rivalHatchAt(int slot) {
    if (s_rivalHatchWorld != G.world.seed || s_rivalHatch.empty()) {
        s_rivalHatchWorld = G.world.seed;
        s_rivalHatch.clear();
        for (int i = 0; i < Coop::MAX_PLAYERS; i++) s_rivalHatch.push_back(findRivalHatch(i));
    }
    return s_rivalHatch[std::clamp(slot, 0, Coop::MAX_PLAYERS - 1)];
}
Vec2 hatchPos() { return rivalsOn() ? rivalHatchAt(Coop::localSlot()) : G.world.homePos; }
// Who shot you last in Rivals, and when (for the kill announcement).
int s_pvpAttacker = -1;
float s_pvpAttackT = 0;

// ---- the catacombs
int localCrypt() { return G.world.dungeonAt(G.player.pos); }
bool inCrypt(Vec2 p) { return G.world.dungeonAt(p) >= 0; }
// Two points in the same place: both outside, or both in the same catacomb.
bool sameArea(Vec2 a, Vec2 b) { return G.world.areaAt(a) == G.world.areaAt(b); }
// Someone else is on the surface (co-op): then the clock runs even with you below.
bool othersOnSurface() {
    if (!Coop::active()) return false;
    for (int i = 0; i < Coop::MAX_PLAYERS; i++) {
        const Coop::NetPlayer& np = Coop::player(i);
        if (i != (Coop::active() ? Coop::localSlot() : 0) && np.used && np.where == Coop::W_RAID && !np.inCrypt) return true;
    }
    return false;
}
float s_spikeCd = 0;

// ---- horde state (one raid only; the schedule itself lives in the profile)
bool s_hordeActive = false;
int s_hordeN = 0;              // number of the horde in progress
int s_hordePending = 0;        // still to come out of the corner
float s_hordeSpawnT = 0;
std::vector<Vec2> s_hordeSpawns;
std::vector<int> s_homeDist;   // path cost to the hatch per tile, -1 = unreachable
float s_homeDistT = 0;
int s_hordeKilled = 0;
int s_hordeEarned = 0;
float s_raidElapsed = 0;
std::string s_hordeNote;       // what happened to a horde fought without you
struct ZombieCorpse { Vec2 pos; int kind; bool left; float t; int fall = 1; bool noAxe = false; };
// An axe in flight, landing and lying where it fell (0.12v, the axe zombie's).
struct FlyingAxe { Vec2 pos, vel; float flight, t = 0; int stage = 0, dir = 2; uint32_t owner; float dmg; };
std::vector<FlyingAxe> s_axes;
std::vector<ZombieCorpse> s_zCorpses;
void resolveHordeOffscreen();

// A horde due tonight, one still being fought, or tonight's horde already beaten:
// the night stays dark but ordinary, with no immortal dead.
// A horde that is due but has not come out yet holds the night too: it used to be
// that a horde due just before 22:00 lost the race with the dark, never launched, and
// kept everyone being sent up into an immortal night.
bool nightHeld() {
    return s_hordeActive || hordeDueTonight() || G.prof.safeNight == G.prof.day || absMinutes() >= G.prof.nextHordeAt;
}
// The hatch will not open at night, nor at all while a horde is on: nobody opens a
// bunker door with the dead outside it. Beat the horde (or die) first.
bool hatchSealed() {
    return G.nightFallen || s_hordeActive;
}
bool s_nightHoldShown = false;
float s_autosaveT = 0;

// Night belongs to them: after 22:00 nothing that walks dead can be killed.
bool immune(const Enemy& e) {
    return G.nightFallen && (e.type == EnemyType::Shade || e.type == EnemyType::Zombie) && !inCrypt(e.pos);
}
float s_deathT = -1;
std::string s_deathCause;

// ---- co-op
uint32_t s_netIdNext = 1;
int s_dmgOwner = -1;          // credited for the damage being dealt right now: a player slot, 10+slot a merc of theirs, -1 the bunker
int s_shotOwner = -1;         // stamped on the bullets spawnBullets makes
float s_downT = -1;           // co-op: down and bleeding out, seconds left
float s_reviveT = 0;          // holding E on a downed teammate
int s_reviveSlot = -1;
bool s_worldLive = false;     // host: today's outside is built and running
uint64_t s_worldKey = 0;
int s_scaledFor = 1;          // players the raider count was scaled for
int s_scaledExtra = 0;        // extra raiders added for them so far
int s_hordeLeftNet = 0;       // guest: how many of the horde the host says are left
struct Target { Vec2 pos; int slot; };
std::vector<Target> s_targets;   // players the dead and the raiders can go for
std::vector<Hireling*> s_mercs;  // every player's mercenaries that are out right now
struct ShotRec { Vec2 origin; float angle; uint8_t weapon, flags; int8_t owner; };
enum ShotFlag : uint8_t { SF_PLAYER = 1, SF_EXPLOSIVE = 2, SF_TURRET = 4 };
std::vector<ShotRec> s_shotLog;  // host: shots to show the guests; guest: my shots for the host
struct MercView { int owner; std::string name; int tier; bool guard; Vec2 pos, target, lastPos; float angle, hp; bool flash, reload, hurt; float seen; bool ride = false; };
std::vector<MercView> s_mercViews;   // guest: everyone's mercenaries as the host last described them
void netEnemyDied(const Enemy& e);
void netExplosion(Vec2 pos, float radius, bool harmless);
void scaleRaiders();
void netInitShadows();

bool isGuest() { return Coop::guest(); }
int mySlot() { return Coop::active() ? Coop::localSlot() : 0; }
// The local player is outside, alive and standing: something the world can hurt.
// Local co-op (0.12v): this seat bled out and waits for the others to come home.
bool s_localOut = false;
bool localPresent() { return G.scene == Scene::Raid && !s_sim && s_deathT < 0 && s_downT < 0 && !s_localOut; }
// ---- cars (0.11v)
// Every car out in today's world: your own (driven by your game) and, in co-op,
// everyone else's as their owners last described them. One car per player.
std::vector<Car> s_cars;
int s_ride = -1;              // whose car I am in (my own: I am at the wheel), -1 on foot
float s_carSendT = 0;
float s_crashCd = 0;          // a crash sound (and hurt) only now and then
float s_carMsgT = 0;          // "slow down to get out" and the like, not every frame
Car* carOf(int owner) {
    for (Car& c : s_cars) if (c.owner == owner) return &c;
    return nullptr;
}
Car* myCar() { return carOf(mySlot()); }
bool driving() { return s_ride >= 0 && s_ride == mySlot(); }
// Whose car a player is in, -1 on foot.
int rideOfSlot(int slot) {
    if (slot == mySlot()) return s_ride;
    if (Coop::active() && slot >= 0 && slot < Coop::MAX_PLAYERS) return Coop::player(slot).ride;
    return -1;
}
// How many people are in someone's car (players only; mercs take what seats are left).
int playersAboard(int owner) {
    int n = s_ride == owner ? 1 : 0;
    if (Coop::active())
        for (int i = 0; i < Coop::MAX_PLAYERS; i++)
            if (i != mySlot() && Coop::player(i).used && Coop::player(i).where == Coop::W_RAID && Coop::player(i).ride == owner) n++;
    return n;
}
void damageCar(Car& c, float dmg);
void persistMyCar();
bool leaveCar(bool thrown);

// The world's hook for things that move but stand in the way: every car blocks
// walkers (and other cars), except the one being driven this moment.
bool carBlockHook(float x, float y, float r) {
    for (const Car& c : s_cars) {
        if (&c == g_carMoving) continue;
        if (carContains(c, Vec2(x, y), r)) return true;
    }
    return false;
}
float carOverlapHook(float x, float y, float r) {
    float o = 0;
    for (const Car& c : s_cars) {
        if (&c == g_carMoving) continue;
        o += carOverlap(c, Vec2(x, y), r);
    }
    return o;
}

// The mechanic's yard lies just east of the bunker compound (see World::generate).
bool mechanicOut() { return G.world.day >= CITY_DAY && G.world.outW > 240; }
Vec2 mechanicHome() { return World::tileCenter(G.world.homeTx + 17, G.world.homeTy + 1); }
// Where a player's car waits in the yard: a row of bays in front of the workshop.
Vec2 garageBay(int slot) {
    int s = std::clamp(slot, 0, Coop::MAX_PLAYERS - 1);
    return World::tileCenter(G.world.homeTx + 12 + (s % 4) * 3, G.world.homeTy + 4 + (s / 4) * 4) + Vec2(8, 0);
}

// The mechanic: he comes out to meet you once, then keeps to his yard.
enum MechState { MECH_IDLE, MECH_COMING, MECH_TALKING, MECH_GOING };
int s_mech = MECH_IDLE;
Vec2 s_mechPos, s_mechLast;
float s_mechAngle = PI / 2;
int s_talkPage = 0;
float s_talkT = 0;           // how long this page has been up (a held key must not skip it)

std::string s_bigText;
int s_bigColor = P_WHITE;
float s_bigT = 0;
float s_shadeSoundT = 0;

Vec2 camFloor() { return {std::floor(G.cam.x), std::floor(G.cam.y)}; }


// The UI is drawn at its own size while the world may be zoomed out (local co-op,
// 0.12v): a UI pixel covers zoom() world pixels.
Vec2 mouseWorld() { return camFloor() + Input::mouse() * R::zoom(); }
Vec2 toScreen(Vec2 world) { return (world - camFloor()) / R::zoom(); }

// True when a world point is inside the visible viewport. `inset` shrinks the test
// rectangle, so a positive value means "comfortably on screen", not just clipping it.
bool onScreen(Vec2 p, float inset = 0) {
    Vec2 c = camFloor();
    return p.x >= c.x + inset && p.y >= c.y + inset &&
           p.x <= c.x + R::viewW() - inset && p.y <= c.y + R::viewH() - inset;
}

void bigText(const std::string& t, int color, float time = 3.5f) {
    s_bigText = t;
    s_bigColor = color;
    s_bigT = time;
}

const char* containerName(int kind) {
    switch (kind) {
    case CK_CRATE: return "Supply Crate";
    case CK_LOCKER: return "Cabinet";
    case CK_CABINET: return "Appliance";
    case CK_MILITARY: return "Shipping Container";
    case CK_BAG: return "Trash Bag";
    case CK_CORPSE: return "Body";
    case CK_TOOLBOX: return "Bin";
    case CK_CHEST: return "Chest";
    case CK_URN: return "Urn";
    }
    return "Container";
}

bool containerEmpty(const Container& c) {
    return std::all_of(c.items.begin(), c.items.end(), [](const Item& i) { return i.empty(); });
}

// ---------------------------------------------------------------- effects
void addParticles(Vec2 pos, int n, int color, float spMin, float spMax, float lifeMin, float lifeMax, bool glow = false, float size = 1) {
    if (s_sim) return;
    for (int i = 0; i < n; i++) {
        Particle p;
        p.pos = pos;
        p.vel = fromAngle(s_rng.range(0, 2 * PI)) * s_rng.range(spMin, spMax);
        p.life = p.maxLife = s_rng.range(lifeMin, lifeMax);
        p.color = color;
        p.glow = glow;
        p.size = size;
        G.particles.push_back(p);
    }
}

// ---- blood: the pack's own splash (enemies/shot/shot_1, shot_2: three frames of a
// burst flying off to the right), played where something is hit, sprayed the way the
// hit was going. What lands stays on the ground as a stain (the BLOOD0/1 decals).
struct BloodFx { Vec2 pos; float t; bool flip; int sheet; };
std::vector<BloodFx> s_bloodFx;
const Assets::Sprite* s_bloodSheet[2] = {nullptr, nullptr};
bool s_bloodLoaded = false;

const Assets::Sprite* bloodSheet(int i) {
    if (!s_bloodLoaded) {
        s_bloodLoaded = true;
        s_bloodSheet[0] = Assets::find("enemies/shot/shot_1");
        s_bloodSheet[1] = Assets::find("enemies/shot/shot_2");
        if (!s_bloodSheet[0] || !s_bloodSheet[1]) std::fprintf(stderr, "[art] blood splash sheets missing (enemies/shot)\n");
    }
    return s_bloodSheet[i & 1];
}

// Flying drops (0.11v): thrown out of every hit the way it was going, they arc up,
// fall, and leave a speck on the ground where they land (or on the wall they hit),
// which fades away over the next minute.
struct BloodDrop { Vec2 pos, vel; float z, vz, size; uint8_t shade; };
struct BloodSpeck { Vec2 pos; float t, size; uint8_t shade; };
std::vector<BloodDrop> s_drops;
std::vector<BloodSpeck> s_specks;
constexpr float SPECK_LIFE = 60.0f;

Color bloodColor(uint8_t shade, float a = 1) {
    static const Color C[3] = {Color(0.58f, 0.06f, 0.08f), Color(0.45f, 0.03f, 0.05f), Color(0.70f, 0.10f, 0.10f)};
    return C[shade % 3].withA(a);
}

void bloodSplash(Vec2 pos, Vec2 dir) {
    if (s_sim) return;
    if (s_bloodFx.size() > 80) s_bloodFx.erase(s_bloodFx.begin());
    bool flip = dir.x < 0;
    s_bloodFx.push_back({pos + dir * 2.0f, 0, flip, (int)(s_rng.next() & 1)});
    // A second, smaller spurt for a fuller burst.
    s_bloodFx.push_back({pos + dir * 4.0f + Vec2(0, s_rng.range(-2, 2)), -0.04f, flip, (int)(s_rng.next() & 1)});
    if (s_drops.size() > 600) return;
    float base = angleOf(dir);
    int n = s_rng.irange(7, 12);
    for (int i = 0; i < n; i++) {
        BloodDrop d;
        bool mist = i >= n - 3;   // a few fine drops go every way
        float a = base + (mist ? s_rng.range(-PI, PI) : s_rng.range(-0.55f, 0.55f));
        d.pos = pos + dir * 2.0f;
        d.vel = fromAngle(a) * (mist ? s_rng.range(15, 40) : s_rng.range(35, 120));
        d.z = s_rng.range(3, 8);
        d.vz = s_rng.range(10, 70);
        d.size = s_rng.chance(0.3f) ? 2.0f : 1.0f;
        d.shade = (uint8_t)s_rng.irange(0, 2);
        s_drops.push_back(d);
    }
}

// Blood pools (0.11v): now and then a body leaves one. It spreads out pixel by pixel
// from under the body to its full size over several seconds, a shape of its own every
// time, with a fresher, brighter rim while it is still spreading. It is wet: it
// reflects whatever stands over it, like a puddle.
struct BloodPool {
    Vec2 pos;
    float t = 0, grow = 8;
    std::vector<int8_t> px, py;     // pixels in the order they are reached
    std::vector<float> at;          // when (0..1 of the spread) each is reached
};
std::vector<BloodPool> s_pools;

void spawnBloodPool(Vec2 pos, float chance = 0.45f) {
    if (s_sim || !s_rng.chance(chance)) return;
    if (G.world.blocksMove(World::toTile(pos.x), World::toTile(pos.y))) return;
    if (s_pools.size() >= 60) s_pools.erase(s_pools.begin());
    BloodPool p;
    p.pos = Vec2(std::floor(pos.x), std::floor(pos.y + 3));
    float rx = s_rng.range(6.5f, 11.0f), ry = rx * s_rng.range(0.5f, 0.7f);
    p.grow = s_rng.range(6.0f, 11.0f);
    uint32_t seed = s_rng.next();
    // A lumpy edge: the radius wobbles with the angle (a few lobes of random size).
    float lobe[5];
    for (float& l : lobe) l = s_rng.range(-0.22f, 0.22f);
    struct P { int8_t x, y; float a; };
    std::vector<P> pts;
    int R = (int)std::ceil(rx * 1.3f);
    for (int y = -R; y <= R; y++)
        for (int x = -R; x <= R; x++) {
            float ang = std::atan2((float)y, (float)x);
            float wob = 1.0f;
            for (int k = 0; k < 5; k++) wob += lobe[k] * std::sin(ang * (k + 1) + k * 1.7f);
            float d = std::sqrt((x / rx) * (x / rx) + (y / ry) * (y / ry)) / wob;
            // Pixel noise so the front creeps forward unevenly.
            d += ((hash2(x, y, seed) & 255) / 255.0f - 0.5f) * 0.12f;
            if (d >= 1.0f) continue;
            // Keep it on open ground.
            if (G.world.blocksMove(World::toTile(p.pos.x + x), World::toTile(p.pos.y + y))) continue;
            pts.push_back({(int8_t)x, (int8_t)y, d});
        }
    std::sort(pts.begin(), pts.end(), [](const P& a, const P& b) { return a.a < b.a; });
    for (const P& q : pts) { p.px.push_back(q.x); p.py.push_back(q.y); p.at.push_back(q.a); }
    s_pools.push_back(std::move(p));
}

// How far a pool has spread (0..1): fast at first, slowing as it thins out.
float poolSpread(const BloodPool& p) { float k = clampf(p.t / p.grow, 0, 1); return 1.0f - (1.0f - k) * (1.0f - k); }

void drawBloodPools() {
    for (const BloodPool& p : s_pools) {
        if (!onScreen(p.pos, -16)) continue;
        float s = poolSpread(p);
        for (size_t i = 0; i < p.at.size() && p.at[i] <= s; i++) {
            float x = p.pos.x + p.px[i], y = p.pos.y + p.py[i];
            bool rim = s < 1.0f && p.at[i] > s - 0.1f;
            bool edge = p.at[i] > 0.86f;
            Color c = rim ? Color(0.62f, 0.06f, 0.08f) : edge ? Color(0.36f, 0.02f, 0.04f) : Color(0.45f, 0.03f, 0.05f);
            R::rect(x, y, 1, 1, c);
            R::waterRect(x, y, 1, 1, Color(1.0f, 0.35f, 0.35f, edge ? 0.55f : 0.85f));
        }
    }
}

void updateBloodDrops(float dt) {
    for (BloodPool& p : s_pools) p.t += dt;
    for (BloodSpeck& s : s_specks) s.t += dt;
    s_specks.erase(std::remove_if(s_specks.begin(), s_specks.end(), [](const BloodSpeck& s) { return s.t > SPECK_LIFE; }), s_specks.end());
    for (BloodDrop& d : s_drops) {
        Vec2 np = d.pos + d.vel * dt;
        bool wall = G.world.blocksBulletAt(np);
        if (!wall) d.pos = np;
        d.vz -= 300.0f * dt;
        d.z += d.vz * dt;
        d.vel *= std::pow(0.4f, dt);
        if (d.z <= 0 || wall) {
            d.z = -1;
            if (s_specks.size() > 900) s_specks.erase(s_specks.begin(), s_specks.begin() + 100);
            s_specks.push_back({d.pos, 0, d.size + (s_rng.chance(0.25f) ? 1.0f : 0.0f), d.shade});
        }
    }
    s_drops.erase(std::remove_if(s_drops.begin(), s_drops.end(), [](const BloodDrop& d) { return d.z < 0; }), s_drops.end());
}
Vec2 randomDir() { return fromAngle(s_rng.range(0, 2 * PI)); }

void addFlash(Vec2 pos, float radius, float life, float intensity, Color color) {
    if (s_sim) return;
    G.flashes.push_back({pos, radius, life, life, intensity, color});
}

void addShake(float amount, Vec2 source) {
    if (s_sim) return;
    float d = dist(source, G.player.pos);
    G.shake = std::min(10.0f, G.shake + amount * clampf(1.0f - d / 400.0f, 0, 1));
}

void alertEnemies(Vec2 pos, float radius) {
    if (Coop::guest()) return;    // the host hears the shot and does this
    for (auto& e : G.enemies) {
        if (e.dead || e.type == EnemyType::Shade || (e.type == EnemyType::Zombie && !e.roamer)) continue;
        if (dist(e.pos, pos) > radius) continue;
        if (e.state != AIState::Combat) {
            e.state = AIState::Alert;
            e.lastSeen = pos;
            e.alertT = 6;
        }
    }
}

// ---------------------------------------------------------------- spawning
void spawnEnemy(EnemyType type, Vec2 pos) {
    const EnemyDef& d = ENEMY_DEFS[(int)type];
    Enemy e;
    e.type = type;
    e.pos = e.home = e.wanderTarget = e.lastPos = pos;
    e.hp = e.maxHp = d.hp * (1.0f + (G.prof.day - 1) * 0.06f) * (G.prof.hardcore() ? 1.3f : 1.0f);
    e.angle = s_rng.range(0, 2 * PI);
    e.losT = s_rng.range(0, 0.3f);
    e.artVariant = (uint8_t)s_rng.next();
    e.wanderT = s_rng.range(0, 3);
    switch (type) {
    case EnemyType::Scav: e.weapon = s_rng.chance(0.8f) ? IT_PISTOL : IT_SMG; break;
    case EnemyType::Bandit: e.weapon = s_rng.chance(0.55f) ? IT_SMG : IT_RIFLE; break;
    case EnemyType::Heavy: e.weapon = s_rng.chance(0.7f) ? IT_SHOTGUN : IT_RIFLE; break;
    case EnemyType::Sniper: e.weapon = IT_SNIPER; break;
    case EnemyType::Shade: case EnemyType::Zombie: e.weapon = IT_NONE; break;
    }
    if (const WeaponDef* w = weaponDef(e.weapon)) e.mag = w->magSize;
    e.netId = s_netIdNext++;
    G.enemies.push_back(e);
}

// ---------------------------------------------------------------- damage
void sendReward(int slot, int money, int enemyType, bool kill);

// Host: tell everyone who killed whom (Rivals), the way the headlines go out.
void announcePvpKill(int killer, int victim) {
    if (killer < 0 || killer >= Coop::MAX_PLAYERS || victim < 0 || victim >= Coop::MAX_PLAYERS) return;
    Coop::headline(T2("{0} KILLED {1}", Coop::player(killer).name, Coop::player(victim).name), P_CORAL);
    if (killer == mySlot()) { G.prof.kills++; G.raidKills++; }
    else sendReward(killer, 0, -1, true);
}

void playerDie(std::string cause) {
    if (Coop::active()) {
        // Down, not dead: a teammate has a while to get you back up.
        if (s_downT >= 0) return;
        // Hardcore in a catacomb: no reviving. Down is dead. Rivals: killed by a
        // player is dead too, there and then.
        bool pvpKill = rivalsOn() && s_pvpAttacker >= 0 && s_pvpAttackT > 0;
        s_downT = (G.prof.hardcore() && localCrypt() >= 0) || pvpKill ? 0.05f : 30;
        if (pvpKill) {
            Net::Writer w;
            w.u8(Coop::M_PVP_KILL);
            w.u8((uint8_t)s_pvpAttacker);
            w.u8((uint8_t)mySlot());
            if (Coop::host()) announcePvpKill(s_pvpAttacker, mySlot());
            else Coop::toHost(w, true);
            cause = T1("Killed by {0}.", Coop::player(s_pvpAttacker).name);
        }
        s_deathCause = cause;
        G.panel = Panel::None;
        G.player.reloadT = 0;
        addParticles(G.player.pos, 20, P_CORAL, 20, 80, 0.4f, 0.9f, false, 2);
        G.decals.push_back({G.player.pos, BLOOD0, s_rng.range(0, 6.28f)});
        spawnBloodPool(G.player.pos);
        sfx(Snd::hurt, 1.0f, 0.6f);
        bigText(T("YOU ARE DOWN"), P_CORAL, 3);
        return;
    }
    if (s_deathT >= 0) return;
    s_deathT = 3.0f;
    s_deathCause = cause;
    G.panel = Panel::None;
    addParticles(G.player.pos, 30, P_CORAL, 20, 90, 0.4f, 1.0f, false, 2);
    G.decals.push_back({G.player.pos, BLOOD0, s_rng.range(0, 6.28f)});
    spawnBloodPool(G.player.pos);
    sfx(Snd::hurt, 1.0f, 0.6f);
}

void damagePlayer(float dmg, const std::string& cause) {
    if (s_deathT >= 0 || s_downT >= 0 || G.devGod) return;
    Profile& p = G.prof;
    dmg *= p.damageMul();
    if (p.hardcore()) dmg *= 1.45f;
    // Everything hits harder in the catacombs: a little on day 1, a lot by day 20.
    if (localCrypt() >= 0) dmg *= 1.1f + 0.6f * cryptTier(p.day);
    // A bullet can open a wound. Armour makes it half as likely. Spikes always do,
    // fresh dressing or not.
    bool spike = cause == "Impaled on a spike trap.";
    if (spike && G.player.bleedT <= 0) G.player.bleedImmuneT = 0;
    if ((spike || cause == "Shot by a hostile.") && dmg >= 4 && G.player.bleedT <= 0 && G.player.bleedImmuneT <= 0 &&
        (spike || s_rng.chance(BLEED_CHANCE * (!p.armor.empty() && p.armor.data > 0 ? 0.5f : 1.0f)))) {
        G.player.bleedT = s_rng.range(45, 75);
        G.player.bleedDripT = 0;
        bigText(T("YOU ARE BLEEDING"), P_CORAL, 2.5f);
        pushMessage(T("You are bleeding! Use a bandage or medkit [H]."), P_CORAL);
    }
    if (!p.armor.empty() && p.armor.data > 0) {
        float absorbed = std::min(dmg * 0.6f, (float)p.armor.data);
        p.armor.data -= (int)std::ceil(absorbed);
        dmg -= absorbed;
        if (p.armor.data <= 0) {
            pushMessage(T("Your armor broke!"), P_CORAL);
            p.armor = Item();
        }
    }
    p.hp -= dmg;
    G.player.hurtT = 0.25f;
    G.shake = std::min(10.0f, G.shake + 2.5f);
    bloodSplash(G.player.pos, randomDir());
    sfx(Snd::hurt, 0.6f, s_rng.range(0.9f, 1.1f));
    if (p.hp <= 0) {
        p.hp = 0;
        playerDie(cause);
    }
}

// ---- who is out there
void buildTargets() {
    s_targets.clear();
    if (localPresent()) s_targets.push_back({G.player.pos, mySlot()});
    if (Coop::host())
        for (int i = 1; i < Coop::MAX_PLAYERS; i++) {
            const Coop::NetPlayer& np = Coop::player(i);
            if (np.used && np.where == Coop::W_RAID && !np.downed) s_targets.push_back({np.target, i});
        }
}

int nearestTarget(Vec2 from) {
    int best = -1;
    float bd = 1e18f;
    for (int i = 0; i < (int)s_targets.size(); i++) {
        if (!sameArea(s_targets[i].pos, from)) continue;
        float d = lengthSq(s_targets[i].pos - from);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

const Target* targetOf(int slot) {
    for (const Target& t : s_targets) if (t.slot == slot) return &t;
    return nullptr;
}

Vec2 slotPos(int slot) {
    if (slot == mySlot()) return G.player.pos;
    if (Coop::active() && slot >= 0 && slot < Coop::MAX_PLAYERS) return Coop::player(slot).target;
    return G.player.pos;
}

// Hurt whichever player that is: here, or over the wire to their game.
void hurtSlot(int slot, float dmg, const char* cause, int attacker = -1) {
    // In a car (0.11v): the car takes it, until it is a wreck.
    if (int ride = rideOfSlot(slot); ride >= 0)
        if (Car* c = carOf(ride); c && !c->wrecked) { damageCar(*c, dmg * 0.6f); return; }
    if (!Coop::active() || slot == mySlot()) {
        if (attacker >= 0) { s_pvpAttacker = attacker; s_pvpAttackT = 8; }
        damagePlayer(dmg, cause);
        return;
    }
    if (!Coop::host()) return;
    Net::Writer w;
    w.u8(Coop::M_DAMAGE);
    w.f32(dmg);
    w.str(cause);
    w.u8((uint8_t)(attacker + 1));
    Coop::sendReliable(slot, w);
}

// Rivals: my shot hit another player. The host hands out the damage.
void pvpHit(int target, float dmg) {
    const char* cause = "Shot by another player.";
    if (Coop::host()) { hurtSlot(target, dmg, cause, mySlot()); return; }
    Net::Writer w;
    w.u8(Coop::M_PVP_HIT);
    w.u8((uint8_t)target);
    w.f32(dmg);
    Coop::toHost(w, true);
}

// A kill or money earned by someone else's game: tell them.
void sendReward(int slot, int money, int enemyType, bool kill) {
    Net::Writer w;
    w.u8(Coop::M_REWARD);
    w.i32(money);
    w.i32(enemyType);
    w.u8(kill ? 1 : 0);
    Coop::sendReliable(slot, w);
}

// Where the one hurting this enemy is standing, so the enemy turns on them.
Vec2 s_dmgFrom;                // where the hit being dealt came from (0.11v), (0,0) unknown
Vec2 attackerPos() {
    if (s_dmgFrom.x != 0 || s_dmgFrom.y != 0) return s_dmgFrom;
    if (s_dmgOwner >= 0 && s_dmgOwner < 10) return slotPos(s_dmgOwner);
    return G.player.pos;
}

void killEnemy(Enemy& e) {
    e.dead = true;
    sfxAt(Snd::enemy_die, e.pos, G.player.pos, 0.7f, s_rng.range(0.8f, 1.2f));
    if (e.type == EnemyType::Shade) {
        addParticles(e.pos, 14, P_PURPLE, 10, 50, 0.4f, 0.9f, false, 2);
        return;
    }
    // Whose kill it is: a player, a player's merc, or (-1) the bunker's turrets, whose
    // bounties go to the host that owns them.
    int slot = s_dmgOwner >= 10 ? s_dmgOwner - 10 : std::max(0, s_dmgOwner);
    bool mine = !Coop::active() || slot == mySlot();
    if (Coop::host()) netEnemyDied(e);
    if (e.type == EnemyType::Zombie && e.roamer) {
        // One of the zone's dead (Zombies mode): a small bounty, and now and then
        // something it was still carrying.
        const ZombieKind& zk = ZOMBIE_KINDS[std::clamp(e.zkind, 0, 2)];
        int bounty = (int)std::round(zk.bounty * 0.8f * (1.0f + 0.03f * (G.prof.day - 1)));
        if (mine) {
            G.prof.money += bounty;
            G.prof.earned += bounty;
            G.prof.kills++;
        } else {
            sendReward(slot, bounty, (int)EnemyType::Zombie, true);
        }
        if (s_sim) return;
        if (mine) {
            G.raidKills++;
            missionAddKill((int)EnemyType::Zombie);
        }
        if (s_zCorpses.size() > 160) s_zCorpses.erase(s_zCorpses.begin());
        s_zCorpses.push_back({e.pos, e.zkind, std::cos(e.angle) < 0, 0, s_rng.chance(0.5f) ? 1 : 0, e.noAxe});
        addParticles(e.pos, 12, P_CORAL, 20, 70, 0.3f, 0.7f, false, 1);
        G.decals.push_back({e.pos, s_rng.chance(0.5f) ? BLOOD0 : BLOOD1, s_rng.range(0, 6.28f)});
        spawnBloodPool(e.pos);
        if (mine) G.floatTexts.push_back({e.pos + Vec2(0, -14), "+$" + std::to_string(bounty), P_YGREEN, 0.9f});
        Vec2 sp = G.world.surfacePos(e.pos);
        float q = G.world.lootQuality(World::toTile(sp.x), World::toTile(sp.y));
        bool loot = s_rng.chance(e.zkind == 1 ? 0.6f : 0.3f), ammo = s_rng.chance(0.3f);
        if (loot || ammo) {
            int id = G.world.addContainer(e.pos, CK_BAG, -1, -1, (uint8_t)(s_rng.next() & 0x7F));
            Container& c = G.world.containers[id];
            c.searchTime = 0.6f;
            if (loot) addToSlots(c.items, rollLoot(s_rng, q, LootKind::Bag));
            if (ammo) {
                static const int AMMO[] = {IT_AMMO_LIGHT, IT_AMMO_LIGHT, IT_AMMO_SHELL, IT_AMMO_RIFLE};
                int a = AMMO[s_rng.irange(0, 3)];
                addToSlots(c.items, makeItem(a, a == IT_AMMO_SHELL ? s_rng.irange(3, 8) : s_rng.irange(8, 22)));
            }
        }
        return;
    }
    if (e.type == EnemyType::Zombie) {
        Profile& p = G.prof;
        const ZombieKind& zk = ZOMBIE_KINDS[std::clamp(e.zkind, 0, 2)];
        int bounty = (int)std::round(zk.bounty * (1.0f + 0.04f * (s_hordeN - 1)) * (1.0f + 0.15f * p.defUp[DU_BOUNTY]));
        p.hordeKills++;
        s_hordeKilled++;
        s_hordeEarned += bounty;
        if (mine) {
            p.money += bounty;
            p.earned += bounty;
            p.kills++;
        } else {
            sendReward(slot, bounty, (int)EnemyType::Zombie, true);
        }
        if (s_sim) return;
        if (mine) {
            G.raidKills++;
            missionAddKill((int)EnemyType::Zombie);
        }
        if (s_zCorpses.size() > 160) s_zCorpses.erase(s_zCorpses.begin());
        s_zCorpses.push_back({e.pos, e.zkind, std::cos(e.angle) < 0, 0, s_rng.chance(0.5f) ? 1 : 0, e.noAxe});
        addParticles(e.pos, 12, P_CORAL, 20, 70, 0.3f, 0.7f, false, 1);
        G.decals.push_back({e.pos, s_rng.chance(0.5f) ? BLOOD0 : BLOOD1, s_rng.range(0, 6.28f)});
        spawnBloodPool(e.pos);
        if (mine) G.floatTexts.push_back({e.pos + Vec2(0, -14), "+$" + std::to_string(bounty), P_YGREEN, 0.9f});
        return;
    }
    if (mine) {
        G.prof.kills++;
        G.raidKills++;
        missionAddKill((int)e.type);
    } else {
        sendReward(slot, 0, (int)e.type, true);
    }
    addParticles(e.pos, 16, P_CORAL, 20, 80, 0.3f, 0.8f, false, 1);
    G.decals.push_back({e.pos, s_rng.chance(0.5f) ? BLOOD0 : BLOOD1, s_rng.range(0, 6.28f)});
    spawnBloodPool(e.pos);

    // 0x80: a raider's body; 0x40: a helmet rolls off it (see Art::corpse).
    bool helmeted = e.type == EnemyType::Heavy || e.type == EnemyType::Sniper || e.type == EnemyType::Bandit;
    int id = G.world.addContainer(e.pos, CK_CORPSE, -1, -1, (uint8_t)((s_rng.next() & 0x3F) | 0x80 | (helmeted ? 0x40 : 0)));
    Container& c = G.world.containers[id];
    c.searchTime = 0.9f;
    Vec2 sp = G.world.surfacePos(e.pos);
    float q = G.world.lootQuality(World::toTile(sp.x), World::toTile(sp.y));
    if (G.world.floorAt(e.pos) >= 0) q += 0.15f;   // the gangs holding the upper floors carry more
    bool below = inCrypt(e.pos);
    if (below) q = 1.0f;
    if (const WeaponDef* w = weaponDef(e.weapon)) {
        if (s_rng.chance(below ? 0.8f : 0.5f)) {
            Item wi = makeItem(e.weapon);
            wi.data = std::max(0, std::min(e.mag, w->magSize));
            wi.tier = (int8_t)rollWeaponTier(s_rng, below ? 1.7f : q * 0.7f);
            addToSlots(c.items, wi);
        }
        if (s_rng.chance(0.7f)) {
            Item ammo = makeItem(w->ammo, 1);
            int amt = w->ammo == IT_AMMO_SNIPER ? s_rng.irange(3, 8) : w->ammo == IT_AMMO_SHELL ? s_rng.irange(4, 10) : s_rng.irange(10, 28);
            ammo.count = (int16_t)amt;
            addToSlots(c.items, ammo);
        }
    }
    int n = s_rng.irange(0, 2);
    for (int i = 0; i < n; i++) addToSlots(c.items, rollLoot(s_rng, q, LootKind::Bag));
    if (e.type == EnemyType::Heavy && s_rng.chance(0.4f)) {
        Item v = makeItem(s_rng.chance(0.4f) ? IT_VEST_HEAVY : IT_VEST_LIGHT);
        v.data = (int)(itemDef(v.id).param * s_rng.range(0.2f, 0.7f));
        addToSlots(c.items, v);
    }
    if (s_rng.chance(0.3f)) addToSlots(c.items, makeItem(IT_BANDAGE, 1));
}

void damageEnemy(Enemy& e, float dmg, Vec2 dir);

// ---- melee (0.12v): a punch, or a swing of the bat if you carry one. Short reach, a
// wide arc in front of you, and a shove that buys a moment. It costs a little stamina.
bool carryingBat() {
    const Profile& p = G.prof;
    for (int i = 0; i < p.invCapacity(); i++)
        if (p.inv[i].id == IT_BAT) return true;
    return false;
}

void melee() {
    Player& pl = G.player;
    if (pl.meleeCd > 0 || s_ride >= 0 || s_downT >= 0 || s_deathT >= 0) return;
    bool bat = carryingBat();
    float reach = bat ? 26.0f : 19.0f, dmg = bat ? (float)itemDef(IT_BAT).param : 16.0f, push = bat ? 16.0f : 8.0f;
    float cost = bat ? 10.0f : 7.0f;
    if (pl.stamina < cost * 0.5f) return;
    pl.stamina = std::max(0.0f, pl.stamina - cost);
    pl.meleeCd = bat ? 0.62f : 0.42f;
    pl.act = 2;
    pl.actT = 0;
    sfx(Snd::melee, bat ? 0.7f : 0.5f, bat ? 0.8f : 1.15f);
    Vec2 f = fromAngle(pl.angle);
    bool hit = false;
    for (Enemy& e : G.enemies) {
        if (e.dead) continue;
        Vec2 d = e.pos - pl.pos;
        float l = length(d);
        if (l > reach + ENEMY_R || l < 0.01f || dot(d / l, f) < 0.35f) continue;
        hit = true;
        if (isGuest()) {
            Net::Writer w;
            w.u8(Coop::M_HIT);
            w.u32(e.netId);
            w.f32(dmg);
            w.f32(f.x);
            w.f32(f.y);
            Coop::toHost(w, true);
            bloodSplash(e.pos, f);
        } else {
            s_dmgOwner = mySlot();
            damageEnemy(e, dmg, f);
            s_dmgOwner = -1;
            if (!e.dead) {
                // Knocked back and put off its stroke.
                bool big = e.type == EnemyType::Heavy || (e.type == EnemyType::Zombie && e.zkind == 1);
                e.pos = G.world.move(e.pos, (d / l) * (big ? push * 0.35f : push), ENEMY_R);
                e.meleeCd = std::max(e.meleeCd, 0.45f);
            }
        }
    }
    if (hit) {
        sfx(Snd::hit, 0.6f, bat ? 0.7f : 0.9f);
        addShake(bat ? 2.0f : 1.0f, pl.pos);
    }
}

void damageEnemy(Enemy& e, float dmg, Vec2 dir) {
    if (e.dead) return;
    if (immune(e)) {
        // The bullet lands and does nothing, which is the whole point.
        addParticles(e.pos + dir * 2, 2, P_PURPLE, 10, 40, 0.1f, 0.3f);
        if (e.hurtT <= -0.8f && !s_sim) {
            G.floatTexts.push_back({e.pos + Vec2(0, -10), T("IMMUNE"), P_LAVENDER, 0.5f});
            e.hurtT = 0;
        }
        return;
    }
    e.hp -= dmg;
    e.hurtT = 0.12f;
    if (e.type == EnemyType::Shade) addParticles(e.pos + dir * 2, 4, P_PURPLE, 20, 70, 0.15f, 0.4f);
    else bloodSplash(e.pos, lengthSq(dir) > 0.01f ? normalize(dir) : randomDir());
    sfxAt(Snd::hit, e.pos, G.player.pos, 0.5f, s_rng.range(0.9f, 1.2f));
    if (e.type != EnemyType::Shade && e.type != EnemyType::Zombie) {
        e.state = AIState::Combat;
        e.lastSeen = attackerPos();
        e.alertT = 7;
        if (e.reactT > 0.2f) e.reactT = 0.2f;
        // Shot by a merc (or anyone) it has not seen: it looks for them (0.11v fix -
        // before, a raider hit by your people just stood there until you fired too).
        e.provokedT = 7;
        e.losT = 0;
    }
    if (!s_sim) G.floatTexts.push_back({e.pos + Vec2(0, -8), std::to_string((int)std::ceil(dmg)), P_WHITE, 0.6f});
    if (e.hp <= 0) killEnemy(e);
}

// The look and sound of an explosion, with none of the damage. Guests draw the
// host's explosions with this.
void explodeFx(Vec2 pos, float radius, bool harmless) {
    addParticles(pos, 26, P_ORANGE, 30, 150, 0.2f, 0.6f, true, 3);
    addParticles(pos, 18, P_YELLOW, 20, 110, 0.1f, 0.4f, true, 2);
    addParticles(pos, 20, P_PURPLE, 10, 60, 0.6f, 1.4f, false, 3);
    addFlash(pos, radius * 3.5f, 0.5f, 1.6f, pal(P_ORANGE));
    if (s_sim) return;
    G.decals.push_back({pos, SCORCH, s_rng.range(0, 6.28f)});
    addShake(harmless ? 3 : 9, pos);
    sfxAt(Snd::explosion, pos, G.player.pos, harmless ? 0.6f : 1.0f, s_rng.range(0.9f, 1.1f));
}

// `harmless` is a base defense rocket: it only hurts the hostiles it lands among, and
// leaves the ground, you and your own people alone.
void explode(Vec2 pos, float radius, float dmg, bool fromPlayer, bool harmless = false) {
    int r = harmless ? -1 : (int)std::ceil(radius / TILE);
    int cx = World::toTile(pos.x), cy = World::toTile(pos.y);
    for (int y = cy - r; y <= cy + r; y++)
        for (int x = cx - r; x <= cx + r; x++) {
            if (!G.world.inBounds(x, y)) continue;
            if (dist(World::tileCenter(x, y), pos) > radius) continue;
            Tile& t = G.world.at(x, y);
            if (t.solid != S_NONE && solidInfo(t.solid).hp > 0) {
                int col = solidInfo(t.solid).mapColor;
                G.world.destroyTile(x, y);
                addParticles(World::tileCenter(x, y), 5, col, 20, 90, 0.4f, 0.9f, false, 2);
            }
        }
    float reach = radius * 1.25f;
    for (auto& e : G.enemies) {
        if (e.dead) continue;
        float d = dist(e.pos, pos);
        if (d < reach) damageEnemy(e, dmg * (1.0f - d / reach), normalize(e.pos - pos));
    }
    if (!harmless) {
        // Every player in the blast, here or in their own game.
        for (const Target& t : s_targets) {
            float pd = dist(t.pos, pos);
            if (pd < reach) hurtSlot(t.slot, dmg * (1.0f - pd / reach) * (fromPlayer ? 0.7f : 1.0f), "Blown up.",
                                     fromPlayer && rivalsOn() && s_dmgOwner >= 0 && s_dmgOwner < Coop::MAX_PLAYERS && s_dmgOwner != t.slot ? s_dmgOwner : -1);
        }
    }
    for (auto& g : G.grenades) if (dist(g.pos, pos) < reach) g.fuse = std::min(g.fuse, 0.1f);
    // Cars in the blast (0.11v). A wreck going up does not set off its own tank twice.
    if (!harmless)
        for (size_t i = 0; i < s_cars.size(); i++) {
            Car& c = s_cars[i];
            float cd = dist(c.pos, pos);
            if (!c.wrecked && cd < reach + 16) damageCar(c, dmg * clampf(1.0f - cd / (reach + 16), 0.2f, 1.0f));
        }
    if (Coop::host()) netExplosion(pos, radius, harmless);
    explodeFx(pos, radius, harmless);
    if (!s_sim && !harmless) alertEnemies(pos, 600);
}

// ---- spent casings (0.12v): Character/Guns/Bullets. Kicked out of the gun's side,
// they bounce and lie where they land for a while; a shotgun drops its shell as the
// next one is racked in.
struct Casing { Vec2 pos, vel; float z, vz, t, delay, spin; int kind; };
std::vector<Casing> s_casings;

void ejectCasing(Vec2 origin, float angle, int weaponId) {
    int base = baseWeapon(weaponId);
    if (base == IT_LAUNCHER || s_sim) return;
    Casing c;
    c.kind = base == IT_SHOTGUN ? 2 : (base == IT_PISTOL || base == IT_REVOLVER || base == IT_SMG) ? 0 : 1;
    // Out of the right-hand side of the gun, a little back.
    Vec2 f = fromAngle(angle), side(-f.y, f.x);
    c.pos = origin - f * 4.0f;
    c.vel = side * s_rng.range(28, 48) - f * s_rng.range(4, 14);
    c.z = 6;
    c.vz = s_rng.range(30, 50);
    c.t = 0;
    c.delay = c.kind == 2 ? 0.28f : 0;
    c.spin = s_rng.range(-14, 14);
    if (s_casings.size() > 220) s_casings.erase(s_casings.begin());
    s_casings.push_back(c);
}

void updateCasings(float dt) {
    for (Casing& c : s_casings) {
        if (c.delay > 0) { c.delay -= dt; continue; }
        c.t += dt;
        if (c.z > 0 || c.vz > 0) {
            c.vz -= 260 * dt;
            c.z += c.vz * dt;
            c.pos += c.vel * dt;
            if (c.z <= 0) {
                c.z = 0;
                if (c.vz < -25) { c.vz = -c.vz * 0.35f; c.vel *= 0.5f; }
                else { c.vz = 0; c.vel = Vec2(); }
            }
        }
    }
    s_casings.erase(std::remove_if(s_casings.begin(), s_casings.end(), [](const Casing& c) { return c.t > 25; }), s_casings.end());
}

void drawCasings() {
    static const Assets::Sprite* art[3] = {Assets::find("character/guns/bullets/pistol-bullet_casting"),
                                           Assets::find("character/guns/bullets/gun-bullet_casing"),
                                           Assets::find("character/guns/bullets/shotgun-bullet")};
    for (const Casing& c : s_casings) {
        if (c.delay > 0) continue;
        const Assets::Sprite* s = art[c.kind];
        float a = clampf((25 - c.t) / 4.0f, 0, 1);
        float rot = c.z > 0 ? c.t * c.spin : c.spin;   // spinning in the air, lying still after
        if (s) R::spriteAt(*s, 0, c.pos + Vec2(0, -c.z), R::Pivot::Center, 1, Color(1, 1, 1, a), false, rot);
        else R::rect(std::floor(c.pos.x), std::floor(c.pos.y - c.z), 2, 1, pal(P_YELLOW, a));
    }
}

void spawnBullets(Vec2 origin, float angle, const WeaponDef& wd, int weaponId, float spreadMul, float extraSpread, bool fromPlayer, float dmgMul) {
    ejectCasing(origin, angle, weaponId);
    for (int i = 0; i < wd.pellets; i++) {
        float spread = (wd.spread * spreadMul + extraSpread) * (s_rng.f() + s_rng.f() - 1.0f);
        if (wd.pellets > 1) spread = (wd.spread * spreadMul) * (s_rng.f() * 2 - 1);
        Bullet b;
        b.pos = origin;
        float speed = wd.bulletSpeed * (wd.pellets > 1 ? s_rng.range(0.85f, 1.1f) : 1.0f);
        b.vel = fromAngle(angle + spread) * speed;
        b.damage = wd.damage * dmgMul;
        b.rangeLeft = wd.range * (wd.pellets > 1 ? s_rng.range(0.8f, 1.1f) : 1.0f);
        b.tileMul = wd.tileDamage;
        b.fromPlayer = fromPlayer;
        b.explosive = wd.explosive;
        b.pierce = baseWeapon(weaponId) == IT_SNIPER && fromPlayer;
        b.weapon = weaponId;
        b.owner = s_shotOwner;
        b.origin = origin;
        if (isGuest()) {
            // My own shot: it hits what it hits here and tells the host, and leaves
            // walls to the host. A rocket here is only for show; the host's copy is real.
            b.noTiles = true;
            if (b.explosive) b.cosmetic = true;
            else b.reportHits = true;
        }
        G.bullets.push_back(b);
    }
    if (Coop::active() && (Coop::host() || s_shotOwner == mySlot()))
        s_shotLog.push_back({origin, angle, (uint8_t)weaponId, (uint8_t)((fromPlayer ? SF_PLAYER : 0) | (wd.explosive ? SF_EXPLOSIVE : 0)), (int8_t)s_shotOwner});
    addFlash(origin, 60, 0.07f, 0.9f, pal(P_YELLOW));
    addParticles(origin, 3, P_YELLOW, 20, 60, 0.05f, 0.12f, true, 1);
}

// ---------------------------------------------------------------- interaction
enum class Interact { None, Container, Hatch, Door, Revive, CryptDoor, CryptExit, CryptGate, Mechanic, Car, Refuel, Stairs };

int doorStateNear(Vec2 pos, float radius) {
    int cx = World::toTile(pos.x), cy = World::toTile(pos.y);
    float best = radius * radius;
    int state = S_NONE;
    for (int y = cy - 2; y <= cy + 2; y++)
        for (int x = cx - 2; x <= cx + 2; x++) {
            if (!G.world.inBounds(x, y)) continue;
            int s = G.world.at(x, y).solid;
            if (s != S_DOOR && s != S_DOOR_OPEN) continue;
            float d = lengthSq(World::tileCenter(x, y) - pos);
            if (d < best) { best = d; state = s; }
        }
    return state;
}

bool doorwayOccupied(Vec2 pos, float radius) {
    int cx = World::toTile(pos.x), cy = World::toTile(pos.y);
    for (int y = cy - 2; y <= cy + 2; y++)
        for (int x = cx - 2; x <= cx + 2; x++) {
            if (!G.world.inBounds(x, y) || G.world.at(x, y).solid != S_DOOR_OPEN) continue;
            Vec2 c = World::tileCenter(x, y);
            if (dist(c, pos) > radius) continue;
            if (dist(c, G.player.pos) < PLAYER_R + 5.0f) return true;
            for (const Enemy& e : G.enemies)
                if (!e.dead && dist(c, e.pos) < ENEMY_R + 5.0f) return true;
        }
    return false;
}

void closeLoot();

// The way back's portcullis (0.11v): you are on the side that can raise it, the last
// room's.
bool gateFromInside(int idx) {
    if (idx < 0 || idx >= (int)G.world.dungeons.size()) return false;
    const Dungeon& d = G.world.dungeons[idx];
    float wallMid = d.gateY * (float)TILE + TILE * 0.5f;
    return d.gateDir > 0 ? G.player.pos.y > wallMid : G.player.pos.y < wallMid;
}

void raiseGate(int idx) {
    if (G.world.gateOpen(idx)) return;
    G.world.openGate(idx);
    const Dungeon& d = G.world.dungeons[idx];
    std::fprintf(stderr, "[crypt] gate %d raised%s\n", idx, isGuest() ? " (telling the host)" : "");
    // A guest tells the host, whose tiles are the real ones (everyone else hears it
    // from there as a tile change).
    if (isGuest()) {
        Net::Writer w;
        w.u8(Coop::M_DOOR);
        Vec2 gp = d.gateFront();
        w.f32(gp.x); w.f32(gp.y);
        w.u8(1);
        Coop::toHost(w, true);
    }
    Vec2 c((d.gateX + 2) * (float)TILE, d.gateY * (float)TILE + TILE * 0.5f);
    sfxAt(Snd::door, c, G.player.pos, 1.0f, 0.55f);
    if (dist(c, G.player.pos) < 200) {
        G.shake = std::min(10.0f, G.shake + 3.0f);
        addParticles(c + Vec2(0, 6), 18, P_BEIGE, 10, 50, 0.5f, 1.2f, false, 2);
        pushMessage(T("The gate grinds up. The way back to the stairs is open."), P_YELLOW);
    }
}

// Down the stairs into catacomb `idx`, or back up to its arch outside.
void enterCrypt(int idx, bool down) {
    if (idx < 0 || idx >= (int)G.world.dungeons.size()) return;
    const Dungeon& d = G.world.dungeons[idx];
    closeLoot();
    G.player.pos = down ? d.arrive : d.door + Vec2(0, 8);
    G.player.angle = down ? PI / 2 : PI / 2;
    G.cam = G.player.pos - Vec2(R::viewW() / 2.0f, R::viewH() / 2.0f);
    G.world.reveal(G.player.pos, 12);
    G.bullets.erase(std::remove_if(G.bullets.begin(), G.bullets.end(), [](const Bullet& b) { return b.fromPlayer && b.owner == mySlot(); }), G.bullets.end());
    sfx(Snd::door, 0.9f, down ? 0.8f : 1.0f);
    if (down) {
        bigText(T("THE CATACOMBS"), P_ORANGE, 3);
        if (!G.prof.cryptIntroSeen) G.panel = Panel::CryptIntro;
    } else {
        bigText(T("BACK ON THE SURFACE"), P_YELLOW, 2);
    }
}

// Up or down a flight of stairs in a city building (0.11v).
void useStairs(int i) {
    if (i < 0 || i >= (int)G.world.stairs.size()) return;
    const Stairway& s = G.world.stairs[i];
    closeLoot();
    G.player.pos = s.to;
    G.player.angle = PI / 2;
    G.cam = G.player.pos - Vec2(R::viewW() / 2.0f, R::viewH() / 2.0f);
    G.world.reveal(G.player.pos, 12);
    G.bullets.erase(std::remove_if(G.bullets.begin(), G.bullets.end(), [](const Bullet& b) { return b.fromPlayer && b.owner == mySlot(); }), G.bullets.end());
    sfx(Snd::step_in, 1.0f, 0.8f);
    sfx(Snd::step_in, 0.8f, 0.9f);
    int f = G.world.floorAt(s.to);
    bigText(f >= 0 ? T1("FLOOR {0}", std::to_string(G.world.floors[f].level)) : T("GROUND FLOOR"), P_WHITE, 1.4f);
}

// Everything within reach that E could act on: the hatch, the nearest door and every
// container close enough (crates, bags, bodies). With two or more, the HUD shows
// them as a list and the mouse wheel picks one instead of switching weapons.
struct InteractOpt {
    Interact kind = Interact::None;
    int container = -1;
    float d = 0;
    int key() const {
        if (kind == Interact::Car) return 100000 + container;
        if (kind == Interact::Stairs) return 200000 + container;
        return kind == Interact::Container ? 100 + container : kind == Interact::Revive ? 50 + container
             : kind == Interact::CryptDoor || kind == Interact::CryptExit || kind == Interact::CryptGate ? 20 + (int)kind * 8 + container : (int)kind;
    }
};
std::vector<InteractOpt> s_interOpts;
int s_interSel = 0;          // index into s_interOpts
int s_interSelKey = -1;      // what is selected, so the choice survives re-sorting

void gatherInteract() {
    s_interOpts.clear();
    Player& pl = G.player;
    // In a car (0.11v) there is only one thing to do: get out (see raid_update).
    if (s_deathT < 0 && s_ride < 0) {
        // The mechanic at his yard.
        if (s_mech == MECH_IDLE && mechanicOut() && dist(pl.pos, s_mechPos) < 26) s_interOpts.push_back({Interact::Mechanic, -1, dist(pl.pos, s_mechPos)});
        // A car within reach: yours to drive, a friend's to ride in.
        for (const Car& c : s_cars) {
            if (c.wrecked || carOverlap(c, pl.pos, 16) <= 0) continue;
            if (c.owner != mySlot() && (rivalsOn() || !Coop::active())) continue;
            s_interOpts.push_back({Interact::Car, c.owner, dist(pl.pos, c.pos) * 0.5f});
            if (c.owner == mySlot() && c.fuel < carModel(c.model).tank - 1 && countInSlots(G.prof.inv, IT_FUEL, G.prof.invCapacity()) > 0)
                s_interOpts.push_back({Interact::Refuel, -1, dist(pl.pos, c.pos)});
        }
        // Stairs between the floors of a city building.
        for (int i = 0; i < (int)G.world.stairs.size(); i++) {
            float d = dist(pl.pos, G.world.stairs[i].at);
            if (d < 20) s_interOpts.push_back({Interact::Stairs, i, d});
        }
    }
    if (s_deathT < 0 && s_ride < 0) {
        float dh = dist(pl.pos, hatchPos());
        if (dh < 20) s_interOpts.push_back({Interact::Hatch, -1, dh});
        for (int i = 0; i < (int)G.world.dungeons.size(); i++) {
            const Dungeon& d = G.world.dungeons[i];
            float a = dist(pl.pos, d.door), b = dist(pl.pos, d.exit);
            if (a < 22) s_interOpts.push_back({Interact::CryptDoor, i, a});
            if (b < 22) s_interOpts.push_back({Interact::CryptExit, i, b});
            // The way back's portcullis, while it is down: from either side (only the
            // last room's side can raise it).
            if (d.hasGate() && !G.world.gateOpen(i)) {
                Vec2 gc((d.gateX + 2) * (float)TILE, d.gateY * (float)TILE + TILE * 0.5f);
                float g = dist(pl.pos, gc);
                if (g < 30) s_interOpts.push_back({Interact::CryptGate, i, g});
            }
        }
        int px = World::toTile(pl.pos.x), py = World::toTile(pl.pos.y);
        float bestDoor = INTERACT_RANGE;
        for (int y = py - 2; y <= py + 2; y++)
            for (int x = px - 2; x <= px + 2; x++) {
                if (!G.world.inBounds(x, y)) continue;
                int so = G.world.at(x, y).solid;
                if (so != S_DOOR && so != S_DOOR_OPEN) continue;
                bestDoor = std::min(bestDoor, dist(World::tileCenter(x, y), pl.pos));
            }
        if (bestDoor < INTERACT_RANGE) s_interOpts.push_back({Interact::Door, -1, bestDoor});
        for (int i = 0; i < (int)G.world.containers.size(); i++) {
            const Container& c = G.world.containers[i];
            if (c.removed) continue;
            float d = dist(c.pos, pl.pos);
            if (d < INTERACT_RANGE) s_interOpts.push_back({Interact::Container, i, d});
        }
        // A teammate down within reach.
        if (Coop::active() && s_downT < 0)
            for (int i = 0; i < Coop::MAX_PLAYERS; i++) {
                const Coop::NetPlayer& np = Coop::player(i);
                if (i == mySlot() || !np.used || np.where != Coop::W_RAID || !np.downed) continue;
                if (G.prof.hardcore() && np.inCrypt) continue;   // Hardcore: no reviving below
                float d = dist(np.pos, pl.pos);
                if (d < INTERACT_RANGE) s_interOpts.push_back({Interact::Revive, i, d});
            }
    }
    // The hatch first, then nearest first, but a stable order so the list does not
    // shuffle under the cursor as you shuffle your feet.
    std::stable_sort(s_interOpts.begin(), s_interOpts.end(), [](const InteractOpt& a, const InteractOpt& b) {
        if ((a.kind == Interact::Hatch) != (b.kind == Interact::Hatch)) return a.kind == Interact::Hatch;
        return a.key() < b.key();
    });
    s_interSel = -1;
    for (int i = 0; i < (int)s_interOpts.size(); i++)
        if (s_interOpts[i].key() == s_interSelKey) s_interSel = i;
    if (s_interSel < 0) {
        // Nothing picked yet (or it went out of reach): default to the closest.
        s_interSel = 0;
        for (int i = 1; i < (int)s_interOpts.size(); i++)
            if (s_interOpts[i].d < s_interOpts[s_interSel].d) s_interSel = i;
    }
    if (s_interOpts.size() >= 2 && G.panel == Panel::None) {
        float sc = Input::scroll();
        int n = (int)s_interOpts.size();
        if (sc > 0 || Input::dpadPressed(0)) s_interSel = (s_interSel + n - 1) % n;
        else if (sc < 0 || Input::dpadPressed(1)) s_interSel = (s_interSel + 1) % n;
    }
    // With a list to pick from, the D-pad's up/down choose in it instead of walking.
    Input::setDpadWalk(!(s_interOpts.size() >= 2 && G.panel == Panel::None));
    s_interSelKey = s_interOpts.empty() ? -1 : s_interOpts[s_interSel].key();
}

// True while the wheel belongs to the interaction list rather than the weapons.
bool interactListActive() { return s_interOpts.size() >= 2 && G.panel == Panel::None; }

Interact findInteract(int& containerOut) {
    containerOut = -1;
    if (s_interOpts.empty()) return Interact::None;
    const InteractOpt& o = s_interOpts[std::clamp(s_interSel, 0, (int)s_interOpts.size() - 1)];
    if (o.kind == Interact::Container && (o.container < 0 || o.container >= (int)G.world.containers.size())) return Interact::None;
    containerOut = o.container;
    return o.kind;
}

void closeLoot() {
    if (G.lootContainer >= 0 && G.lootContainer < (int)G.world.containers.size()) {
        Container& c = G.world.containers[G.lootContainer];
        if (c.tx < 0 && c.searched && containerEmpty(c)) c.removed = true;
    }
    G.lootContainer = -1;
    if (G.panel == Panel::Loot) G.panel = Panel::None;
}

int carriedValue() {
    const Profile& p = G.prof;
    int v = 0;
    for (auto& it : p.inv) v += itemValue(it);
    v += itemValue(p.weapons[0]) + itemValue(p.weapons[1]) + itemValue(p.armor) + itemValue(p.backpack);
    return v;
}

void extract() {
    Profile& p = G.prof;
    persistMyCar();
    s_ride = -1;
    // In co-op the world goes on for whoever is still out there.
    if (!Coop::active()) resolveHordeOffscreen();
    if (!isGuest()) captureDayMemory();
    p.inRaid = false;
    p.extractions++;
    G.summary = RaidSummary();
    G.summary.died = false;
    G.summary.kills = G.raidKills;
    G.summary.value = carriedValue();
    G.summary.minutes = p.timeMin - G.raidStartMin;
    G.summary.dayAfter = p.day;
    G.summary.hordeNote = s_hordeNote;
    sfx(Snd::door, 0.9f);
    base_enter(true);
}

// Co-op: nobody got to you in time. Everything you carried is left on your body
// for your friends to pick up, and you wake in the bunker with a pistol - the day and
// the clock carry on for everyone.
void banCrypt();

void coopDeath() {
    Profile& p = G.prof;
    persistMyCar();
    s_ride = -1;
    banCrypt();
    int value = carriedValue();
    G.summary = RaidSummary();
    // What the difficulty takes (half, or all on Hardcore) stays on the body.
    std::vector<Item> carried = applyDeathLoss();
    for (size_t at = 0; at < carried.size(); at += 12) {
        std::vector<Item> chunk(carried.begin() + at, carried.begin() + std::min(carried.size(), at + 12));
        Vec2 pos = G.player.pos + Vec2((float)(at / 12) * 6.0f, 0);
        if (isGuest()) {
            Net::Writer w;
            w.u8(Coop::M_DROP);
            w.f32(pos.x); w.f32(pos.y);
            w.u8(CK_CORPSE);
            w.u8((uint8_t)chunk.size());
            for (const Item& it : chunk) { w.i16(it.id); w.i16(it.count); w.i32(it.data); w.u8((uint8_t)it.tier); w.u8(it.flags); }
            Coop::toHost(w, true);
        } else {
            int id = G.world.addContainer(pos, CK_CORPSE, -1, -1, (uint8_t)(s_rng.next() & 0x3F));
            Container& c = G.world.containers[id];
            c.searchTime = 0.9f;
            for (const Item& it : chunk) addToSlots(c.items, it);
        }
    }
    p.inRaid = false;
    p.deaths++;
    G.summary.died = true;
    G.summary.cause = T("You bled out before anyone reached you.");
    G.summary.kills = G.raidKills;
    G.summary.value = value;
    G.summary.minutes = p.timeMin - G.raidStartMin;
    G.summary.dayAfter = p.day;
    p.hp = p.maxHp();
    s_downT = -1;
    if (Local::active()) {
        // Local co-op: the rest of the group is still out there on this screen. This
        // player waits in the bunker until the others come home (or all bleed out).
        closeLoot();
        G.panel = Panel::None;
        s_localOut = true;
        pushMessage(T1("{0} bled out. What they lost is on their body.", Coop::player(mySlot()).name), P_CORAL);
        return;
    }
    pushMessage(T("You wake up in the bunker. What you lost is on your body."), P_YELLOW);
    base_enter(true);
}

void reviveLocal() {
    if (s_downT < 0) return;
    s_downT = -1;
    G.prof.hp = std::max(G.prof.hp, G.prof.maxHp() * 0.35f);
    bigText(T("BACK ON YOUR FEET"), P_YGREEN, 2);
    sfx(Snd::heal, 0.8f);
}

// Died in a catacomb: that one is closed to you for the rest of the day.
void banCrypt() {
    Profile& p = G.prof;
    int c = localCrypt();
    if (c < 0) return;
    if (p.cryptBanDay != p.day) { p.cryptBanDay = p.day; p.cryptBanMask = 0; }
    p.cryptBanMask |= 1 << c;
}

void finishDeath() {
    Profile& p = G.prof;
    persistMyCar();
    s_ride = -1;
    banCrypt();
    resolveHordeOffscreen();
    // Same day, new world. The day itself does not move on, but the layout rolls to
    // the next revision, so waking up with a pistol never drops you back into the
    // exact place that just killed you -- raiders, loot and roads are all elsewhere
    // now. Day memory belongs to one layout, so it starts empty again.
    p.dayRev++;
    p.dayMem.clear();
    p.inRaid = false;
    p.deaths++;
    int value = carriedValue();
    G.summary = RaidSummary();
    G.summary.died = true;
    G.summary.cause = s_deathCause;
    G.summary.kills = G.raidKills;
    G.summary.value = value;
    G.summary.minutes = p.timeMin - G.raidStartMin;
    // Half of what you carried stays out there (all of it on Hardcore). You never come
    // back without a gun, so losing a run never leaves you defenceless.
    applyDeathLoss();
    p.timeMin = DAY_START_MIN;
    p.hp = p.maxHp();
    G.summary.dayAfter = p.day;
    G.summary.hordeNote = s_hordeNote;
    pushMessage(G.prof.hardcore() ? T("You wake up in the bunker with nothing but a pistol.") : T("You wake up in the bunker. Half of what you carried is gone."), P_YELLOW);
    base_enter(true);
}

// ---------------------------------------------------------------- updates
void updateTime(float dt) {
    Profile& p = G.prof;
    // In a catacomb time stands still, unless a friend is up on the surface.
    if (!s_sim && localCrypt() >= 0 && !othersOnSurface()) return;
    p.timeMin += GAME_MINUTES_PER_SEC * dt;
    // A new day (co-op carries the flag across sleeps) is never dark.
    if (G.nightFallen && p.timeMin < CURFEW_MIN) G.nightFallen = false;
    if (s_sim) {
        // Ticking the world for others while underground: only nightfall matters.
        if (!G.nightFallen && p.timeMin >= CURFEW_MIN && !nightHeld()) {
            G.nightFallen = true;
            G.shadeSpawnT = 1.5f;
            s_hordeActive = false;
            s_hordePending = 0;
        }
        return;
    }
    const float stages[4] = {19 * 60.0f, 21 * 60.0f, 21 * 60 + 30.0f, 21 * 60 + 50.0f};
    const char* texts[4] = {"THE SUN IS SETTING - HEAD HOME SOON", "ONE HOUR UNTIL NIGHTFALL", "30 MINUTES LEFT - GET HOME!", "10 MINUTES! RUN!"};
    const int colors[4] = {P_ORANGE, P_ORANGE, P_CORAL, P_CORAL};
    while (G.warnStage < 4 && p.timeMin >= stages[G.warnStage]) {
        if (nightHeld() && p.timeMin - stages[G.warnStage] < 5) {
            // No curfew to race tonight: the horde is the deadline.
            if (G.warnStage == 0) {
                std::string msg = s_hordeActive ? T("NIGHT IS COMING - FINISH THE HORDE")
                                                : T1("HORDE TONIGHT AT {0}", hordeWhen());
                bigText(msg, P_ORANGE);
                pushMessage(msg, P_ORANGE);
                sfx(Snd::warning, 0.8f);
            }
        } else if (p.timeMin - stages[G.warnStage] < 5) {
            bigText(T(texts[G.warnStage]), colors[G.warnStage]);
            pushMessage(T(texts[G.warnStage]), colors[G.warnStage]);
            sfx(Snd::warning, 0.8f);
        }
        G.warnStage++;
    }
    if (!G.nightFallen && p.timeMin >= CURFEW_MIN && nightHeld()) {
        if (!s_nightHoldShown) {
            s_nightHoldShown = true;
            if (G.prof.safeNight != G.prof.day) {
                bigText(T("DARKNESS - BUT THE HORDE COMES FIRST"), P_ORANGE, 5);
                pushMessage(T("They can still die tonight. Hold the bunker, then sleep."), P_ORANGE);
                sfx(Snd::night, 0.6f, 1.2f);
            }
        }
    } else if (!G.nightFallen && p.timeMin >= CURFEW_MIN) {
        G.nightFallen = true;
        G.shadeSpawnT = 1.5f;
        bigText(T("NIGHT HAS FALLEN. THEY CANNOT DIE."), P_CORAL, 6);
        pushMessage(T("The bunker hatch is sealed for the night."), P_CORAL);
        pushMessage(T("Bullets are useless now. There is nowhere left to hide."), P_CORAL);
        if (s_hordeActive) {
            // The horde does not stop at dark; it just stops being killable.
            s_hordeActive = false;
            s_hordePending = 0;
        }
        sfx(Snd::night, 1.0f);
        addShake(6, G.player.pos);
    }
}

void updateNight(float dt) {
    if (!G.nightFallen) return;
    float past = G.prof.timeMin - CURFEW_MIN;
    G.shadeSpawnT -= dt;
    int shades = 0;
    for (auto& e : G.enemies) if (!e.dead && e.type == EnemyType::Shade) shades++;
    if (G.shadeSpawnT <= 0 && shades < 160 && !s_targets.empty()) {
        G.shadeSpawnT = std::max(0.12f, 0.55f - past * 0.018f);
        int n = 2 + (int)(past / 5);
        std::vector<Vec2> surface;
        for (const Target& t : s_targets) if (!inCrypt(t.pos)) surface.push_back(t.pos);
        if (surface.empty()) n = 0;
        for (int i = 0; i < n; i++) {
            // Round whoever is out in it.
            Vec2 around = surface[s_rng.next() % surface.size()];
            Vec2 pos = around + fromAngle(s_rng.range(0, 2 * PI)) * s_rng.range(210, 300);
            int fl = G.world.floorAt(around);
            if (fl >= 0) {
                // Up on a floor of a city building (0.11v): they come through the walls
                // of that floor. Being upstairs is no hiding place.
                const Floor& f = G.world.floors[fl];
                pos = World::tileCenter(f.x0 + s_rng.irange(1, f.w - 2), f.y0 + s_rng.irange(1, f.h - 2));
                if (dist(pos, around) < 48) pos = World::tileCenter(f.x0 + 1, f.y0 + 1);
            } else {
                pos.x = clampf(pos.x, 4.0f * TILE, (G.world.outW - 4.0f) * TILE);
                pos.y = clampf(pos.y, 4.0f * TILE, (G.world.outH - 4.0f) * TILE);
            }
            spawnEnemy(EnemyType::Shade, pos);
            G.enemies.back().speedMul = std::min(1.7f, 1.0f + past * 0.03f) * s_rng.range(0.9f, 1.15f);
        }
    }
    s_shadeSoundT -= dt;
    if (s_shadeSoundT <= 0 && localPresent()) {
        s_shadeSoundT = s_rng.range(0.8f, 2.0f);
        sfx(Snd::shade, 0.5f, s_rng.range(0.7f, 1.3f));
    }
}

// A little help for the stick: a hostile close to where you point pulls the aim
// most of the way onto it. Only things you can actually see, and only a narrow cone.
// Aim assist for controllers, which cannot point as finely as a mouse. Within a cone
// around where the stick points (wider up close) it locks onto the best visible
// hostile: nearly all the way when it is close to the line, most of the way at the
// edge. It sticks to the last target a little, so it does not flick between two.
// `cone` 0 uses the default; the idle lock below passes a wider one.
int s_assistTarget = -1;
uint32_t s_assistNet = 0;      // the same, by id (the list is re-packed every frame): the lock-on marker
float s_assistT = 0;           // how long it has held that target (the marker closes in)
float padAssist(float angle, float cone = 0) {
    const Player& pl = G.player;
    // Off, standard, strong (0.12v, in the controls): how wide it looks and how hard it pulls.
    int level = Options::aimAssist();
    if (level <= 0) { s_assistTarget = -1; s_assistNet = 0; return angle; }
    bool strong = level >= 2;
    const Item& held = G.prof.weapons[G.prof.curWeapon];
    float reach = !held.empty() && baseWeapon(held.id) == IT_SNIPER ? 460.0f : 340.0f;
    int bestI = -1;
    float bestScore = 1e9f, bestA = angle, bestDiff = 0;
    for (int i = 0; i < (int)G.enemies.size(); i++) {
        const Enemy& e = G.enemies[i];
        if (e.dead || immune(e)) continue;
        if (!sameArea(e.pos, pl.pos)) continue;
        float d = dist(e.pos, pl.pos);
        if (d < 10 || d > reach) continue;
        float a = angleOf(e.pos - pl.pos);
        float diff = std::fabs(std::remainder(a - angle, 2 * PI));
        float c = cone > 0 ? cone : clampf(0.55f - d * 0.0011f, 0.24f, 0.5f);   // ~30 deg close, ~14 far
        if (strong) c *= 1.45f;
        if (diff > c) continue;
        float score = diff / c + d / 600.0f - (i == s_assistTarget ? 0.35f : 0.0f);
        if (score < bestScore && G.world.lineOfSight(pl.pos, e.pos)) { bestScore = score; bestI = i; bestA = a; bestDiff = diff / c; }
    }
    uint32_t id = bestI >= 0 ? G.enemies[bestI].netId : 0;
    if (id != s_assistNet) s_assistT = 0;
    s_assistTarget = bestI;
    s_assistNet = id;
    if (bestI < 0) return angle;
    float diff = std::remainder(bestA - angle, 2 * PI);
    float pull = strong ? 1.0f : bestDiff < 0.5f ? 0.95f : 0.8f;
    return angle + diff * pull;
}

// The enemy aim assist is holding, or nullptr.
const Enemy* assistedEnemy() {
    if (!s_assistNet) return nullptr;
    for (const Enemy& e : G.enemies) if (e.netId == s_assistNet && !e.dead) return &e;
    return nullptr;
}

// Tap R (or click on an empty mag) to start a reload. Tapping again while the marker
// is inside the active band is a perfect reload: the magazine comes back with more
// punch. Tapping too early jams the reload and makes it slower. Ignoring the bar
// entirely just gives an ordinary reload.
void tryReload() {
    Profile& p = G.prof;
    Item& w = p.weapons[p.curWeapon];
    const WeaponDef* wd = weaponDef(w.id);
    if (!wd) return;

    // Second tap during a reload: this is the active-reload input.
    if (G.player.reloadT > 0 && p.activePhase == Profile::ActivePhase::Filling) {
        if (p.activeMarker >= p.activeZoneA && p.activeMarker <= p.activeZoneB) {
            p.activePhase = Profile::ActivePhase::Perfect;
            p.activeResultT = 0.9f;
            p.magBonus = 1.35f;
            // A clean reload is quick: cut whatever was left of the animation.
            G.player.reloadT = std::min(G.player.reloadT, 0.08f);
            sfx(Snd::click, 0.9f, 1.9f);
        } else {
            p.activePhase = Profile::ActivePhase::Failed;
            p.magBonus = 1.0f;
            // Botched reloads cost you time.
            G.player.reloadT += wd->reloadTime * p.reloadMul() * tierReload(itemTier(p.weapons[p.curWeapon])) * 0.6f;
            // Keep the jam flagged until the magazine seats, so the bar stays red the
            // whole way and the JAMMED tag gets a moment on screen once it does.
            p.activeResultT = G.player.reloadT + 0.25f;
            sfx(Snd::empty, 0.8f, 0.7f);
        }
        return;
    }

    if (G.player.reloadT > 0 || w.data >= magSizeOf(w)) return;
    if (countInSlots(p.inv, wd->ammo, p.invCapacity()) <= 0) {
        pushMessage(T1("No ammo: {0}", T(itemDef(wd->ammo).name)), P_CORAL);
        return;
    }
    G.player.reloadT = wd->reloadTime * p.reloadMul() * tierReload(itemTier(w));
    sfx(Snd::reload, 0.6f);

    // Roll a fresh active band for this reload. Better weapons get a slimmer window,
    // so a perfect reload stays a skill check rather than free damage.
    float width = wd->magSize >= 20 ? 0.20f : wd->magSize >= 8 ? 0.17f : 0.14f;
    float start = s_rng.range(0.40f, 0.78f - width);
    p.activeZoneA = start;
    p.activeZoneB = start + width;
    p.activeMarker = 0;
    p.activePhase = Profile::ActivePhase::Filling;
    p.activeResultT = 0;
    p.magBonus = 1.0f;          // a fresh magazine starts plain
}

void switchWeapon(int slot) {
    Profile& p = G.prof;
    if (slot == p.curWeapon || p.weapons[slot].empty()) return;
    p.curWeapon = slot;
    G.player.reloadT = 0;
    G.player.fireCd = std::max(G.player.fireCd, 0.25f);
    // The active reload belonged to the old gun; the other magazine carries its own.
    p.activePhase = Profile::ActivePhase::None;
    p.activeMarker = 0;
    p.magBonus = 1.0f;
    sfx(Snd::click, 0.5f, 0.8f);
}

void updatePlayer(float dt) {
    Player& pl = G.player;
    Profile& p = G.prof;
    pl.fireCd -= dt;
    pl.flashT -= dt;
    pl.hurtT -= dt;
    pl.healCd -= dt;
    bool panelBlocks = G.panel != Panel::None && G.panel != Panel::Map;

    Vec2 in;
    if (G.panel != Panel::Map || true) {
        if (Input::down(GLFW_KEY_W)) in.y -= 1;
        if (Input::down(GLFW_KEY_S)) in.y += 1;
        if (Input::down(GLFW_KEY_A)) in.x -= 1;
        if (Input::down(GLFW_KEY_D)) in.x += 1;
    }
    Vec2 stick = Input::moveAxis();
    if (lengthSq(stick) > lengthSq(in)) in = stick;
    // In a car the car moves you (0.11v); nobody walks off while the mechanic talks.
    if (s_ride >= 0 || G.panel == Panel::MechanicTalk) in = Vec2();
    pl.moving = lengthSq(in) > 0;
    if (pl.stamina <= 0.01f) pl.exhausted = true;
    if (pl.exhausted && pl.stamina >= p.maxStamina() * 0.25f) pl.exhausted = false;
    pl.sprinting = pl.moving && Input::down(GLFW_KEY_LEFT_SHIFT) && !pl.exhausted;
    float speed = 62.0f * p.moveMul();
    if (pl.sprinting) {
        speed *= 1.55f;
        pl.stamina = std::max(0.0f, pl.stamina - 26 * dt);
    } else {
        pl.stamina = std::min(p.maxStamina(), pl.stamina + (16 + p.up[UP_ENDURANCE] * 4) * dt);
    }
    if (p.armor.id == IT_VEST_HEAVY) speed *= 0.92f;
    pl.actT += dt;
    pl.meleeCd -= dt;
    // Local co-op: nobody walks off the shared screen (Local::leash is a no-op otherwise).
    if (pl.moving) pl.pos = Local::leash(pl.pos, G.world.move(pl.pos, normalize(in) * speed * dt, PLAYER_R));

    Input::setPadCursor(G.panel != Panel::None);
    // At the wheel both hands are busy: no aiming, shooting or throwing. A passenger
    // shoots out of the window as usual.
    if (driving()) panelBlocks = true;
    if (!panelBlocks) {
        Vec2 aim = Input::aimAxis();
        if (Input::usingPad()) {
            // Twin-stick: the right stick points the gun; let go and it keeps pointing
            // where it was, or follows your feet if you are walking and not aiming.
            if (lengthSq(aim) > 0) pl.angle = padAssist(angleOf(aim));
            else {
                // Stick let go: keep facing the way you were (or walking), but a
                // hostile roughly in front is picked up on its own.
                float base = pl.moving ? angleOf(in) : pl.angle;
                pl.angle = padAssist(base, 0.7f);
            }
        } else if (lengthSq(aim) > 0) pl.angle = angleOf(aim);
        else pl.angle = angleOf(mouseWorld() - pl.pos);
        if (g_devAim > -90) pl.angle = g_devAim;   // --aim=RADIANS (screenshots)
    }

    // Weapon handling.
    if (Input::pressed(GLFW_KEY_1)) switchWeapon(0);
    if (Input::pressed(GLFW_KEY_2)) switchWeapon(1);
    if (Input::pressed(GLFW_KEY_Q)) switchWeapon(1 - p.curWeapon);
    if (!panelBlocks && Input::scroll() != 0 && !interactListActive()) switchWeapon(1 - p.curWeapon);
    if (Input::pressed(GLFW_KEY_R)) tryReload();
    // A controller's R3 does two things (0.12v): tap it to hit, hold it for the laser.
    bool laserPress = !Input::usingPad() && Input::pressed(GLFW_KEY_L), meleePress = !Input::usingPad() && Input::pressed(GLFW_KEY_F);
    if (Input::usingPad()) {
        Player& me = G.player;
        if (Input::down(GLFW_KEY_L)) {
            if (me.padR3T < 0) { me.padR3T = 0; me.padR3Held = false; }
            me.padR3T += dt;
            if (me.padR3T >= 0.4f && !me.padR3Held) { me.padR3Held = true; laserPress = true; }
        } else {
            if (me.padR3T >= 0 && !me.padR3Held) meleePress = true;
            me.padR3T = -1;
        }
    }
    if (meleePress && !panelBlocks) melee();
    if (laserPress && !p.weapons[p.curWeapon].empty()) {
        // Each gun keeps its own switch, if the crafter fitted it a laser.
        if (p.weapons[p.curWeapon].flags & ITEMF_LASER) {
            p.weapons[p.curWeapon].flags ^= ITEMF_LASER_OFF;
            pushMessage(T(p.laserActive() ? "Laser on" : "Laser off"), P_CORAL);
        } else {
            pushMessage(T("No laser on this gun. The crafter in the bunker can fit one."), P_LAVENDER);
        }
    }

    Item& w = p.weapons[p.curWeapon];
    const WeaponDef* wd = weaponDef(w.id);
    if (pl.reloadT > 0) {
        float total = wd ? wd->reloadTime * p.reloadMul() * tierReload(itemTier(w)) : 0.001f;
        pl.reloadT -= dt;
        // Drive the active-reload marker along the bar while the reload runs.
        if (p.activePhase == Profile::ActivePhase::Filling && total > 0) {
            p.activeMarker = clampf(1.0f - pl.reloadT / total, 0, 1);
        }
        if (pl.reloadT <= 0 && wd) {
            int need = magSizeOf(w) - w.data;
            int got = takeFromSlots(p.inv, wd->ammo, need, p.invCapacity());
            w.data += got;
            sfx(Snd::reload_end, 0.7f);
            // Never tapped in the window (or already resolved): plain magazine.
            if (p.activePhase == Profile::ActivePhase::Filling) {
                p.activePhase = Profile::ActivePhase::None;
                p.magBonus = 1.0f;
            }
            p.activeMarker = 0;
        }
    }
    if (p.activeResultT > 0) {
        p.activeResultT -= dt;
        if (p.activeResultT <= 0) p.activePhase = Profile::ActivePhase::None;
    }
    if (wd && !panelBlocks && !UI::overPanel()) {
        // Gears lets you use the fire button for the active-reload tap, and that is what
        // people actually reach for mid-fight.
        if (pl.reloadT > 0 && p.activePhase == Profile::ActivePhase::Filling && Input::mousePressed(0)) {
            tryReload();
        }
        bool trigger = wd->automatic ? Input::mouseDown(0) : Input::mousePressed(0);
        if (trigger && pl.fireCd <= 0 && pl.reloadT <= 0) {
            if (w.data > 0) {
                float spreadMul = p.spreadMul() * (pl.sprinting ? 1.6f : pl.moving ? 1.15f : 1.0f) * tierSpread(itemTier(w));
                if (Input::usingPad()) spreadMul *= 0.8f;   // a controller cannot correct as finely
                Vec2 muzzle = pl.pos + fromAngle(pl.angle) * 9;
                if (Car* rc = carOf(s_ride)) muzzle = pl.pos + fromAngle(pl.angle) * (carModel(rc->model).halfWid + 5);
                // A perfect reload loads a hotter magazine; the bonus rides every shot
                // from it until the next reload.
                s_shotOwner = mySlot();
                spawnBullets(muzzle, pl.angle, *wd, w.id, spreadMul, 0, true, p.magBonus * tierDamage(itemTier(w)));
                s_shotOwner = -1;
                w.data--;
                pl.fireCd = 1.0f / wd->fireRate;
                pl.flashT = 0.09f;
                G.shake = std::min(10.0f, G.shake + wd->shake);
                sfx(wd->sound, 0.8f, s_rng.range(0.95f, 1.05f));
                alertEnemies(pl.pos, baseWeapon(w.id) == IT_SNIPER ? 550.0f : 380.0f);
            } else if (Input::mousePressed(0)) {
                sfx(Snd::empty, 0.6f);
                tryReload();
            }
        }
    }

    // Grenade.
    if (Input::pressed(GLFW_KEY_G) && !panelBlocks) {
        if (takeFromSlots(p.inv, IT_GRENADE, 1, p.invCapacity()) > 0) {
            // With a controller there is no cursor to throw at: lob it at whatever the aim
            // assist holds, or a fixed distance ahead.
            Vec2 target = mouseWorld();
            if (Input::usingPad()) {
                const Enemy* locked = assistedEnemy();
                target = locked ? locked->pos : pl.pos + fromAngle(pl.angle) * 120.0f;
            }
            float d = std::min(dist(target, pl.pos), 180.0f);
            Grenade g;
            g.pos = pl.pos + fromAngle(pl.angle) * 6;
            g.vel = fromAngle(pl.angle) * (d * 2.2f + 30);
            g.fuse = 1.6f;
            g.owner = mySlot();
            if (Coop::active()) {
                // Guests throw a look-alike and let the host's real one do the damage;
                // the host shows its own to everyone else.
                g.cosmetic = isGuest();
                Net::Writer m;
                m.u8(Coop::M_GRENADE);
                m.f32(g.pos.x); m.f32(g.pos.y); m.f32(g.vel.x); m.f32(g.vel.y); m.f32(g.fuse);
                m.i32(g.owner);
                if (isGuest()) Coop::toHost(m, true);
                else Coop::broadcast(m, true, Coop::W_RAID);
            }
            G.grenades.push_back(g);
            sfx(Snd::toss, 0.7f);
        } else {
            pushMessage(T("No grenades"), P_CORAL);
        }
    }

    // Bleeding.
    pl.bleedImmuneT -= dt;
    if (pl.bleedT > 0 && s_downT < 0 && s_deathT < 0) {
        pl.bleedT -= dt;
        if (p.hp > BLEED_FLOOR) p.hp = std::max(BLEED_FLOOR, p.hp - BLEED_RATE * dt);
        pl.bleedDripT -= dt;
        if (pl.bleedDripT <= 0) {
            pl.bleedDripT = s_rng.range(0.35f, 0.7f);
            addParticles(pl.pos + Vec2(s_rng.range(-3, 3), s_rng.range(-2, 4)), 2, P_CORAL, 4, 14, 0.4f, 0.8f);
        }
        if (pl.bleedT <= 0) {
            pl.bleedImmuneT = 20;
            pushMessage(T("The bleeding has stopped."), P_LGREEN);
        }
    }

    // Quick heal.
    if (Input::pressed(GLFW_KEY_H)) {
        float missing = p.maxHp() - p.hp;
        int prefer = missing > 45 ? IT_MEDKIT : IT_BANDAGE;
        int fallback = prefer == IT_MEDKIT ? IT_BANDAGE : IT_MEDKIT;
        int cap = p.invCapacity();
        int idx = -1;
        for (int pass = 0; pass < 2 && idx < 0; pass++)
            for (int i = 0; i < cap; i++)
                if (p.inv[i].id == (pass == 0 ? prefer : fallback)) { idx = i; break; }
        if (idx >= 0) useItemAt(p.inv, idx);
        else pushMessage(T("No medical items"), P_CORAL);
    }

    G.world.reveal(pl.pos, 18);
}

Vec2 pathDir(const Enemy& e, Vec2 target) {
    Vec2 dir;
    bool targetIsPlayer = dist(target, G.player.pos) < 64;
    if (targetIsPlayer && G.world.flowDir(e.pos, dir)) return dir;
    return normalize(target - e.pos);
}

Vec2 separation(const Enemy& e, size_t selfIndex) {
    Vec2 push;
    for (size_t i = 0; i < G.enemies.size(); i++) {
        if (i == selfIndex) continue;
        const Enemy& o = G.enemies[i];
        if (o.dead) continue;
        Vec2 d = e.pos - o.pos;
        float l2 = lengthSq(d);
        if (l2 < 100 && l2 > 0.001f) push += d / std::sqrt(l2);
    }
    return push;
}

// The dead shove each other (0.11v). After everyone has moved, any two zombies
// standing in each other are pushed apart, the heavier one giving less ground, so a
// horde piles up into a jostling crowd instead of walking the cost field single file.
// Buckets on a 16 px grid keep it cheap with hundreds of them.
void crowdZombies() {
    constexpr float MIN_D = 9.0f;
    static std::vector<std::pair<int64_t, int>> cells;
    cells.clear();
    auto key = [](int cx, int cy) { return ((int64_t)cy << 32) ^ (uint32_t)cx; };
    for (int i = 0; i < (int)G.enemies.size(); i++) {
        const Enemy& e = G.enemies[i];
        if (e.dead || e.type != EnemyType::Zombie) continue;
        cells.push_back({key((int)std::floor(e.pos.x / 16), (int)std::floor(e.pos.y / 16)), i});
    }
    if (cells.size() < 2) return;
    std::sort(cells.begin(), cells.end());
    static const float MASS[3] = {1.0f, 3.5f, 1.6f};
    std::vector<Vec2> push(G.enemies.size());
    for (const auto& [k, i] : cells) {
        const Enemy& a = G.enemies[i];
        int cx = (int)std::floor(a.pos.x / 16), cy = (int)std::floor(a.pos.y / 16);
        for (int oy = -1; oy <= 1; oy++)
            for (int ox = -1; ox <= 1; ox++) {
                int64_t nk = key(cx + ox, cy + oy);
                auto it = std::lower_bound(cells.begin(), cells.end(), std::make_pair(nk, -1));
                for (; it != cells.end() && it->first == nk; ++it) {
                    int j = it->second;
                    if (j <= i) continue;
                    const Enemy& b = G.enemies[j];
                    Vec2 d = a.pos - b.pos;
                    float l2 = lengthSq(d);
                    if (l2 >= MIN_D * MIN_D) continue;
                    float l = std::sqrt(l2);
                    // Exactly on top of each other: split them along something stable.
                    Vec2 n = l > 0.01f ? d / l : fromAngle((float)((i * 7 + j * 13) % 628) / 100.0f);
                    float over = MIN_D - l;
                    float ma = MASS[std::clamp(a.zkind, 0, 2)], mb = MASS[std::clamp(b.zkind, 0, 2)];
                    push[i] += n * (over * mb / (ma + mb));
                    push[j] -= n * (over * ma / (ma + mb));
                }
            }
    }
    bool ghost = G.nightFallen;
    for (const auto& [k, i] : cells) {
        Vec2 p = push[i];
        if (lengthSq(p) < 0.0001f) continue;
        float l = length(p);
        if (l > 3.0f) p *= 3.0f / l;   // a shove, not a teleport
        Enemy& e = G.enemies[i];
        e.pos = ghost && !inCrypt(e.pos) ? G.world.move(e.pos, p * 0.8f, ENEMY_R, true) : G.world.move(e.pos, p * 0.8f, 4.0f);
    }
}

// Each of the dead weaves a little as it comes, on its own beat, so a horde spreads
// across its way in rather than marching in a line.
Vec2 zombieWeave(const Enemy& e, Vec2 move) {
    float seed = e.artVariant * 0.61f + (e.netId % 97) * 0.37f + e.home.x * 0.013f;
    float s = std::sin(G.realTime * (0.7f + (e.artVariant % 5) * 0.12f) + seed * 6.28f);
    return normalize(move + Vec2(-move.y, move.x) * (s * 0.45f));
}

void turnToward(float& angle, float target, float rate, float dt) {
    float d = angleDiff(angle, target);
    float step = rate * dt;
    angle += clampf(d, -step, step);
}

// ---------------------------------------------------------------- base defense
Vec2 turretPos(const Turret& t) { return World::tileCenter(G.world.homeTx + t.dx, G.world.homeTy + t.dy); }
Vec2 turretHead(const Turret& t) { return turretPos(t) + Vec2(0, -2); }

// Path cost to the hatch from every tile of the map (Dijkstra, ten per open tile).
// Anything that can be broken is a way through at a price, so a horde goes round a
// wall when that is quicker, and through the fence or your turrets when it is not.
int pathCost(int x, int y) {
    const Tile& t = G.world.at(x, y);
    if (t.ground == G_WATER) return -1;
    switch (t.solid) {
    case S_NONE: case S_DOOR_OPEN: return 10;
    case S_BOUNDARY: case S_BUNKER: case S_CAR: case S_CONTAINER: case S_FURNITURE: return -1;
    case S_TURRET: return 90;
    // Barricades (0.12v): the tougher the wall, the further round it is worth going.
    case S_BARRICADE: case S_GATE: return barricadeDef(t.variant).reinforced ? 160 : 70;
    case S_GATE_OPEN: case S_FENCE_GATE_OPEN: return 10;
    default: break;
    }
    const SolidInfo& si = solidInfo(t.solid);
    if (!si.blocksMove || si.radius > 0) return 14;     // pebbles, trunks, poles: walk round
    if (si.hp < 0) return -1;
    return 10 + std::max<int>(t.hp, 10);               // bash through
}

// How far round the hatch the dead are routed and come from: the old map's size, even
// on the bigger one (0.11v), so a horde still arrives in minutes.
constexpr int HORDE_REACH = 118;
void computeHomeDist() {
    World& w = G.world;
    s_homeDist.assign(w.w * w.h, -1);
    using Node = std::pair<int, int>;
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> q;
    int start = w.homeTy * w.w + w.homeTx;
    s_homeDist[start] = 0;
    q.push({0, start});
    const int DX[4] = {1, -1, 0, 0}, DY[4] = {0, 0, 1, -1};
    while (!q.empty()) {
        auto [d, i] = q.top();
        q.pop();
        if (d > s_homeDist[i]) continue;
        int x = i % w.w, y = i / w.w;
        for (int k = 0; k < 4; k++) {
            int nx = x + DX[k], ny = y + DY[k];
            if (!w.inBounds(nx, ny) || nx >= w.outW || ny >= w.outH) continue;
            if (std::abs(nx - w.homeTx) > HORDE_REACH || std::abs(ny - w.homeTy) > HORDE_REACH) continue;
            int c = pathCost(nx, ny);
            if (c < 0) continue;
            int ni = ny * w.w + nx, nd = d + c;
            if (s_homeDist[ni] >= 0 && s_homeDist[ni] <= nd) continue;
            s_homeDist[ni] = nd;
            q.push({nd, ni});
        }
    }
}

int homeDistAt(int x, int y) {
    if (!G.world.inBounds(x, y) || s_homeDist.empty()) return -1;
    return s_homeDist[y * G.world.w + x];
}

// Next tile toward the hatch. False when this spot has no route (then go straight).
bool homeStep(Vec2 pos, int& bx, int& by) {
    int x = World::toTile(pos.x), y = World::toTile(pos.y);
    int cur = homeDistAt(x, y);
    if (cur <= 0) return false;
    int best = cur;
    bx = x; by = y;
    for (int oy = -1; oy <= 1; oy++)
        for (int ox = -1; ox <= 1; ox++) {
            if (!ox && !oy) continue;
            int v = homeDistAt(x + ox, y + oy);
            if (v < 0 || v >= best) continue;
            if (ox && oy) {   // never cut a corner past something solid
                if (G.world.blocksMove(x + ox, y) || G.world.blocksMove(x, y + oy)) continue;
            }
            best = v; bx = x + ox; by = y + oy;
        }
    return bx != x || by != y;
}

void damageBase(float dmg);
void damageHireling(Hireling& h, float dmg);

// ---- barricades and gates (0.12v)
Vec2 barricadePos(const Barricade& b) { return World::tileCenter(G.world.homeTx + b.dx, G.world.homeTy + b.dy); }
int barricadeAtTile(int tx, int ty) {
    const auto& bs = G.prof.barricades;
    for (int i = 0; i < (int)bs.size(); i++)
        if (G.world.homeTx + bs[i].dx == tx && G.world.homeTy + bs[i].dy == ty) return i;
    return -1;
}

void damageBarricade(int index, float dmg) {
    Barricade& b = G.prof.barricades[index];
    if (b.hp <= 0) return;
    b.hp -= dmg;
    b.hurtT = 0.12f;
    Vec2 c = barricadePos(b);
    addParticles(c + Vec2(0, -4), 3, P_TAN, 15, 45, 0.2f, 0.5f);
    sfxAt(Snd::tile_hit, c, G.player.pos, 0.35f, s_rng.range(0.6f, 0.8f));
    if (b.hp > 0) return;
    b.hp = 0;
    int tx = World::toTile(c.x), ty = World::toTile(c.y);
    if (G.world.inBounds(tx, ty)) { G.world.at(tx, ty).solid = S_NONE; G.world.updateMapPixel(tx, ty); }
    addParticles(c, 16, P_TAN, 20, 90, 0.3f, 0.9f, true, 2);
    addParticles(c, 8, P_ORANGE, 10, 50, 0.4f, 0.9f, false, 2);
    sfxAt(Snd::tile_break, c, G.player.pos, 0.9f, 0.8f);
    if (!s_sim) pushMessage(T1("A {0} was broken!", T(barricadeDef(b.type).name)), P_CORAL);
}

// Gates swing open when one of your side comes close and shut behind them - never
// on someone standing in the way. The frame of the swing is kept per tile.
std::unordered_map<int, float> s_gateT;
float gateOpenAt(int tx, int ty) {
    auto it = s_gateT.find(ty * G.world.w + tx);
    if (it != s_gateT.end()) return it->second;
    if (!G.world.inBounds(tx, ty)) return 0;
    int s = G.world.at(tx, ty).solid;
    return s == S_GATE_OPEN || s == S_FENCE_GATE_OPEN ? 1.0f : 0.0f;
}

void updateGates(float dt) {
    World& w = G.world;
    if (w.w <= 0) return;
    std::vector<Vec2> friends;
    for (const Target& t : s_targets) friends.push_back(t.pos);
    friends.push_back(G.player.pos);
    for (const Hireling* h : s_mercs) if (!h->dead) friends.push_back(h->pos);
    // The gates that matter this frame: any a friend is near, and any still swinging.
    std::vector<int> gates;
    auto isGate = [&](int s) { return s == S_GATE || s == S_GATE_OPEN || s == S_FENCE_GATE || s == S_FENCE_GATE_OPEN; };
    for (const Vec2& f : friends) {
        int fx = World::toTile(f.x), fy = World::toTile(f.y);
        for (int y = fy - 2; y <= fy + 2; y++)
            for (int x = fx - 2; x <= fx + 2; x++)
                if (w.inBounds(x, y) && isGate(w.at(x, y).solid)) gates.push_back(y * w.w + x);
    }
    for (const auto& kv : s_gateT) gates.push_back(kv.first);
    std::sort(gates.begin(), gates.end());
    gates.erase(std::unique(gates.begin(), gates.end()), gates.end());
    for (int idx : gates) {
        int x = idx % w.w, y = idx / w.w;
        Tile& t = w.at(x, y);
        if (!isGate(t.solid)) { s_gateT.erase(idx); continue; }   // broken down meanwhile
        bool fence = t.solid == S_FENCE_GATE || t.solid == S_FENCE_GATE_OPEN;
        Vec2 c = World::tileCenter(x, y);
        bool want = false, inWay = false;
        for (const Vec2& f : friends) {
            float d = dist(f, c);
            if (d < 26) want = true;
            if (d < 11) inWay = true;
        }
        for (const Enemy& e : G.enemies)
            if (!e.dead && dist(e.pos, c) < 11) { inWay = true; break; }
        float& o = s_gateT.try_emplace(idx, gateOpenAt(x, y)).first->second;
        bool wasOpen = o >= 0.5f;
        o = clampf(o + (want ? 3.0f : -3.0f) * dt, inWay && o >= 0.5f ? 0.5f : 0.0f, 1.0f);
        bool open = o >= 0.5f;
        if (open != wasOpen) {
            t.solid = fence ? (open ? S_FENCE_GATE_OPEN : S_FENCE_GATE) : (open ? S_GATE_OPEN : S_GATE);
            sfxAt(Snd::door, c, G.player.pos, 0.45f, fence ? 1.25f : 0.8f);
        }
        if (o <= 0 && !want) s_gateT.erase(idx);   // shut and nobody about: forget it
    }
}

void damageTurret(int index, float dmg) {
    Turret& t = G.prof.turrets[index];
    if (t.hp <= 0) return;
    t.hp -= dmg;
    t.hurtT = 0.12f;
    if (t.hp > 0) return;
    t.hp = 0;
    Vec2 c = turretPos(t);
    int tx = World::toTile(c.x), ty = World::toTile(c.y);
    if (G.world.inBounds(tx, ty)) { G.world.at(tx, ty).solid = S_NONE; G.world.updateMapPixel(tx, ty); }
    addParticles(c, 18, P_ORANGE, 20, 90, 0.3f, 0.8f, true, 2);
    addParticles(c, 12, P_PURPLE, 10, 50, 0.6f, 1.2f, false, 2);
    addFlash(c, 80, 0.3f, 1.2f, pal(P_ORANGE));
    sfxAt(Snd::tile_break, c, G.player.pos, 0.9f, 0.7f);
    if (!s_sim) pushMessage(T1("A {0} was wrecked!", T(turretDef(t.type).name)), P_CORAL);
}

void endHorde(bool repelled) {
    Profile& p = G.prof;
    if (!s_hordeActive) return;
    s_hordeActive = false;
    s_hordePending = 0;
    if (p.timeMin >= CURFEW_MIN) {
        // Fought in the dark: the rest of this night is an ordinary one, and bed is open.
        p.safeNight = p.day;
        if (!s_sim) pushMessage(T("The night is quiet now. Get to the hatch and sleep."), P_YGREEN);
    }
    if (repelled) {
        int bonus = (int)((60 + 40 * s_hordeN) * (1.0f + 0.15f * p.defUp[DU_BOUNTY]));
        p.money += bonus;
        p.earned += bonus;
        s_hordeEarned += bonus;
        p.hordesRepelled = std::max(p.hordesRepelled, s_hordeN);
        if (Coop::host())
            for (int i = 1; i < Coop::MAX_PLAYERS; i++)
                if (Coop::player(i).used) sendReward(i, bonus, -1, false);
        s_hordeNote = T2("Horde {0} repelled. Earned ${1}.", std::to_string(s_hordeN), std::to_string(s_hordeEarned)) +
                      (s_hordeNote.empty() ? "" : " " + s_hordeNote);
        if (!s_sim) {
            bigText(T1("HORDE {0} REPELLED", std::to_string(s_hordeN)), P_YGREEN, 4);
            pushMessage(T1("Defense bonus: ${0}", std::to_string(bonus)), P_YGREEN);
            sfx(Snd::sell, 0.9f, 0.8f);
        }
    }
}

// The bunker falls: they ransack it and wreck what they can on the way out.
void overrunBase() {
    Profile& p = G.prof;
    int lost = p.money / 4;
    p.money -= lost;
    if (Coop::host()) {
        Net::Writer w;
        w.u8(Coop::M_OVERRUN);
        Coop::broadcast(w, true);
    }
    p.baseHp = baseMaxHp() * 0.25f;
    for (size_t i = 0; i < p.turrets.size(); i++)
        if (p.turrets[i].hp > 0) p.turrets[i].hp = std::max(1.0f, p.turrets[i].hp * 0.5f);
    for (Enemy& e : G.enemies)
        if (e.type == EnemyType::Zombie && !e.roamer) e.dead = true;
    std::string deaths = s_hordeNote;
    endHorde(false);
    s_hordeNote = T2("Horde {0} overran the bunker. They took ${1}.", std::to_string(s_hordeN), std::to_string(lost)) +
                  (deaths.empty() ? "" : " " + deaths);
    if (!s_sim) {
        bigText(T("THE BUNKER HAS BEEN OVERRUN"), P_CORAL, 5);
        pushMessage(T1("They ransacked it: -${0}", std::to_string(lost)), P_CORAL);
        sfx(Snd::night, 0.8f, 1.3f);
        addShake(8, G.player.pos);
    }
}

void damageBase(float dmg) {
    Profile& p = G.prof;
    p.baseHp -= dmg;
    if (!s_sim && (int)(p.baseHp / 100) != (int)((p.baseHp + dmg) / 100))
        pushMessage(T("The bunker is under attack!"), P_CORAL);
    if (p.baseHp <= 0) overrunBase();
}

void spawnZombie(Vec2 pos, int kind) {
    const ZombieKind& zk = ZOMBIE_KINDS[kind];
    Enemy e;
    e.type = EnemyType::Zombie;
    e.zkind = kind;
    e.artVariant = (uint8_t)kind;
    e.pos = e.home = e.lastPos = pos;
    e.hp = e.maxHp = zk.hp * (1.0f + 0.12f * (s_hordeN - 1)) * (G.prof.hardcore() ? 1.25f : 1.0f);
    e.speedMul = s_rng.range(0.9f, 1.12f);
    e.angle = angleOf(G.world.homePos - pos);
    e.retargetT = s_rng.range(0, 0.3f);
    e.netId = s_netIdNext++;
    G.enemies.push_back(e);
}

// ---- the dead get through trees
// A tree in the way of one of the dead (or of a pack that is stuck on it) gets
// hacked down: they claw at the trunk until it falls. True while busy with one.
bool zombieChop(Enemy& e, Vec2 dir, float dmg, float attackCd) {
    if (lengthSq(dir) < 0.001f) return false;
    Vec2 ahead = e.pos + normalize(dir) * 10.0f;
    int tx = World::toTile(ahead.x), ty = World::toTile(ahead.y);
    for (int k = 0; k < 3; k++) {
        int x = tx + (k == 1 ? (dir.x > 0 ? 1 : -1) : 0), y = ty + (k == 2 ? (dir.y > 0 ? 1 : -1) : 0);
        if (!G.world.inBounds(x, y)) continue;
        const Tile& t = G.world.at(x, y);
        if (t.solid != S_TREE && t.solid != S_BUSH) continue;
        Vec2 c = World::tileCenter(x, y);
        if (dist(e.pos, c) > 18) continue;
        e.angle = angleOf(c - e.pos);
        if (e.meleeCd <= 0) {
            e.meleeCd = attackCd;
            int col = solidInfo(t.solid).mapColor;
            if (G.world.damageTile(x, y, dmg * 2.0f)) {
                addParticles(c, 10, col, 20, 70, 0.3f, 0.8f, false, 2);
                sfxAt(Snd::tile_break, c, G.player.pos, 0.6f, s_rng.range(0.8f, 1.0f));
            } else {
                addParticles(c, 3, P_TAN, 15, 40, 0.2f, 0.4f);
                sfxAt(Snd::tile_hit, c, G.player.pos, 0.35f, s_rng.range(0.7f, 0.9f));
            }
        }
        return true;
    }
    return false;
}

// ---- Zombies mode: the zone's own dead
// Most are the small quick kind, some carry an axe, a few are the big ones.
int roamerKind(uint64_t n) {
    uint32_t r = (uint32_t)(mix64(n * 0x9E3779B97F4A7C15ull ^ G.world.seed) % 100);
    return r < 58 ? 0 : r < 86 ? 2 : 1;
}

void spawnRoamer(Vec2 pos, int kind) {
    const ZombieKind& zk = ZOMBIE_KINDS[kind];
    Enemy e;
    e.type = EnemyType::Zombie;
    e.roamer = true;
    e.zkind = kind;
    e.artVariant = (uint8_t)kind;
    e.pos = e.home = e.lastPos = e.wanderTarget = pos;
    e.hp = e.maxHp = zk.hp * (1.0f + (G.prof.day - 1) * 0.05f) * (G.prof.hardcore() ? 1.25f : 1.0f);
    e.speedMul = s_rng.range(0.85f, 1.1f);
    e.angle = s_rng.range(0, 2 * PI);
    e.wanderT = s_rng.range(0, 4);
    e.retargetT = s_rng.range(0, 0.3f);
    e.netId = s_netIdNext++;
    G.enemies.push_back(e);
}

// Whoever is nearest to a roaming zombie, a player or a mercenary: -2 a player
// (tslot), 0.. a merc, -1 nothing within `range`.
int roamerTarget(Enemy& e, float range, bool needSight, Vec2& at) {
    int found = -1;
    float best = range;
    for (const Target& t : s_targets) {
        float d = dist(e.pos, t.pos);
        if (!sameArea(t.pos, e.pos)) continue;
        if (d < best && (!needSight || d < 40 || G.world.lineOfSight(e.pos, t.pos))) { best = d; found = -2; e.tslot = t.slot; at = t.pos; }
    }
    for (int i = 0; i < (int)s_mercs.size(); i++) {
        const Hireling& h = *s_mercs[i];
        if (h.dead) continue;
        float d = dist(e.pos, h.pos);
        if (d < best && (!needSight || d < 40 || G.world.lineOfSight(e.pos, h.pos))) { best = d; found = i; at = h.pos; }
    }
    return found;
}

float nearestTargetDist(Vec2 p) {
    float best = 1e9f;
    for (const Target& t : s_targets) if (sameArea(t.pos, p)) best = std::min(best, dist(p, t.pos));
    return best;
}

// They shamble around where they were left, in their packs, until something catches
// their eye (sight, or anyone right up close) or a gunshot draws them over. Then
// the whole pack comes.
void updateRoamer(size_t index, float dt) {
    Enemy& e = G.enemies[index];
    const ZombieKind& zk = ZOMBIE_KINDS[std::clamp(e.zkind, 0, 2)];
    float near = nearestTargetDist(e.pos);
    if (near > 720 && e.state == AIState::Idle) return;    // nobody about: they stand
    float dmg = zk.damage * dayThreat() * 0.8f;
    float speed = zk.speed * e.speedMul;

    e.retargetT -= dt;
    if (e.retargetT <= 0) {
        e.retargetT = 0.3f + s_rng.f() * 0.1f;
        Vec2 at;
        float sight = e.state == AIState::Combat ? 190.0f : 125.0f;
        e.ztarget = roamerTarget(e, sight, true, at);
        if (e.ztarget != -1) {
            if (e.state != AIState::Combat) {
                // A groan goes round the pack.
                for (Enemy& o : G.enemies)
                    if (!o.dead && o.roamer && &o != &e && o.state == AIState::Idle && dist(o.pos, e.pos) < 110) {
                        o.state = AIState::Alert;
                        o.lastSeen = at;
                        o.alertT = 8;
                    }
                sfxAt(Snd::shade, e.pos, G.player.pos, 0.35f, s_rng.range(0.9f, 1.15f));
            }
            e.state = AIState::Combat;
            e.lastSeen = at;
            e.alertT = 5;
        }
    }

    Vec2 goal = e.pos;
    float moveSpeed = 0;
    const Target* tt = e.ztarget == -2 ? targetOf(e.tslot) : nullptr;
    const Hireling* ht = e.ztarget >= 0 && e.ztarget < (int)s_mercs.size() && !s_mercs[e.ztarget]->dead ? s_mercs[e.ztarget] : nullptr;
    if (e.state == AIState::Combat && (tt || ht)) {
        goal = tt ? tt->pos : ht->pos;
        e.lastSeen = goal;
        if (dist(e.pos, goal) <= zk.reach) {
            e.angle = angleOf(goal - e.pos);
            if (e.meleeCd <= 0) {
                e.meleeCd = zk.attackCd;
                sfxAt(Snd::melee, e.pos, G.player.pos, 0.5f, s_rng.range(0.8f, 1.1f));
                if (tt) hurtSlot(e.tslot, dmg, "Eaten by the dead.");
                else damageHireling(*s_mercs[e.ztarget], dmg);
            }
            return;
        }
        moveSpeed = speed;
    } else if (e.state != AIState::Idle) {
        // Lost them, or heard something: go and look, then give up.
        e.alertT -= dt;
        goal = e.lastSeen;
        moveSpeed = speed * (e.state == AIState::Combat ? 0.9f : 0.7f);
        if (dist(e.pos, goal) < 10 || e.alertT <= 0) {
            e.state = AIState::Idle;
            e.home = e.pos;          // they stay wherever they ended up
            e.wanderT = s_rng.range(1, 3);
        }
    } else {
        e.wanderT -= dt;
        if (e.wanderT <= 0) {
            e.wanderT = s_rng.range(3, 7);
            e.wanderTarget = e.home + Vec2(s_rng.range(-36, 36), s_rng.range(-36, 36));
        }
        goal = e.wanderTarget;
        if (dist(e.pos, goal) > 4) moveSpeed = 14;
    }
    if (moveSpeed <= 0) return;

    Vec2 move = e.state == AIState::Combat ? pathDir(e, goal) : normalize(goal - e.pos);
    // Hunting and blocked by a tree: hack through it rather than shuffle about.
    if (e.state != AIState::Idle && e.stuckT > 0.3f && zombieChop(e, move, zk.damage, zk.attackCd)) return;
    if (e.unstickT > 0) { e.unstickT -= dt; move = e.unstickDir; }
    Vec2 before = e.pos;
    Vec2 vel = move * moveSpeed + separation(e, index) * 14.0f;
    e.pos = G.world.move(e.pos, vel * dt, 4.0f);
    turnToward(e.angle, angleOf(move), 6, dt);
    if (dist(before, e.pos) < moveSpeed * dt * 0.25f) {
        e.stuckT += dt;
        if (e.stuckT > 0.7f) {
            e.unstickDir = fromAngle(s_rng.range(0, 2 * PI));
            e.unstickT = 0.35f;
            e.stuckT = 0;
            if (e.state == AIState::Idle) e.wanderTarget = e.pos;
        }
    } else {
        e.stuckT = std::max(0.0f, e.stuckT - dt);
    }
}

// Which way they come from, seen from the hatch.
const char* cornerName(Vec2 p) {
    static const char* NAMES[8] = {"east", "south-east", "south", "south-west", "west", "north-west", "north", "north-east"};
    float a = angleOf(p - G.world.homePos);            // y grows downwards: +PI/2 is south
    int k = ((int)std::round(a / (PI / 4)) % 8 + 8) % 8;
    return NAMES[k];
}

// Somewhere along the edge of the map with a way in, picked fresh every horde (not
// from the day's seed, so they never pour out of the same spot twice), and the open
// ground there to spill out of.
void findHordeSpawns() {
    static Rng pick((uint64_t)std::time(nullptr) * 0x9E3779B97F4A7C15ull + (uint64_t)std::clock());
    World& w = G.world;
    computeHomeDist();
    s_homeDistT = 3;
    s_hordeSpawns.clear();
    const int inset = 10;
    // The edge they come from: the map's, or on the bigger map (0.11v) the edge of the
    // old one round the bunker, so they are not a quarter of an hour's walk away.
    int ex0 = std::max(0, w.homeTx - HORDE_REACH + 2), ey0 = std::max(0, w.homeTy - HORDE_REACH + 2);
    int ex1 = std::min(w.outW - 1, w.homeTx + HORDE_REACH - 2), ey1 = std::min(w.outH - 1, w.homeTy + HORDE_REACH - 2);
    int spanX = std::max(1, ex1 - ex0 - 2 * inset), spanY = std::max(1, ey1 - ey0 - 2 * inset);
    for (int attempt = 0; attempt < 24 && s_hordeSpawns.empty(); attempt++) {
        // A random point on the border band: any side, anywhere along it.
        int cx, cy;
        int side = (int)(pick.next() % 4);
        int along = (int)(pick.next() % (uint32_t)(side < 2 ? spanX : spanY));
        int depth = (int)(pick.next() % 6);
        if (side == 0) { cx = ex0 + inset + along; cy = ey0 + inset + depth; }
        else if (side == 1) { cx = ex0 + inset + along; cy = ey1 - inset - depth; }
        else if (side == 2) { cx = ex0 + inset + depth; cy = ey0 + inset + along; }
        else { cx = ex1 - inset - depth; cy = ey0 + inset + along; }
        // Not right on top of the bunker on a small map.
        if (dist(World::tileCenter(cx, cy), w.homePos) < 30 * TILE && attempt < 20) continue;
        for (int r = 0; r < 30 && s_hordeSpawns.size() < 40; r++)
            for (int y = cy - r; y <= cy + r; y++)
                for (int x = cx - r; x <= cx + r; x++) {
                    if (std::max(std::abs(x - cx), std::abs(y - cy)) != r) continue;
                    if (!w.inBounds(x, y) || w.blocksMove(x, y) || w.at(x, y).solid != S_NONE) continue;
                    if (homeDistAt(x, y) < 0) continue;
                    s_hordeSpawns.push_back(World::tileCenter(x, y));
                }
    }
}

void launchHorde() {
    Profile& p = G.prof;
    // A horde always gets fought as a horde: if the dark somehow got there first, it
    // lifts, and the immortal shades go with it.
    if (G.nightFallen) {
        G.nightFallen = false;
        for (Enemy& e : G.enemies) if (e.type == EnemyType::Shade) e.dead = true;
    }
    findHordeSpawns();
    rollNextHorde();
    if (s_hordeSpawns.empty()) {                // nowhere they could come from today
        if (p.timeMin >= CURFEW_MIN) p.safeNight = p.day;
        return;
    }
    s_hordeN = p.hordeNum++;
    s_hordeActive = true;
    // More of them for bigger groups (see Coop::enemyScale).
    s_hordePending = (int)std::round(hordeSize(s_hordeN) * Coop::enemyScale(mix64((uint64_t)s_hordeN * 0x9E37 ^ G.prof.worldSeed)));
    s_hordeSpawnT = 0;
    s_hordeKilled = 0;
    s_hordeEarned = 0;
    s_hordeNote.clear();
    bigText(T1("HORDE {0} INCOMING", std::to_string(s_hordeN)), P_CORAL, 5);
    pushMessage(T2("{0} zombies from the {1}. Defend the bunker!", std::to_string(s_hordePending), T(cornerName(s_hordeSpawns[0]))), P_ORANGE);
    sfx(Snd::warning, 1.0f, 0.7f);
}

// A horde that was under way when the game was closed: whatever was left of it
// pours out of a corner again. Its successor is already scheduled, so nothing re-rolls.
void resumeHorde(int n, int left) {
    findHordeSpawns();
    if (s_hordeSpawns.empty() || left <= 0) return;
    s_hordeN = n;
    s_hordeActive = true;
    s_hordePending = left;
    s_hordeSpawnT = 0;
    s_hordeKilled = 0;
    s_hordeEarned = 0;
    s_hordeNote.clear();
    bigText(T1("HORDE {0} IS STILL COMING", std::to_string(s_hordeN)), P_CORAL, 4);
}

void updateHordeSpawning(float dt) {
    if (!s_hordeActive) return;
    s_homeDistT -= dt;
    if (s_homeDistT <= 0) {
        computeHomeDist();
        s_homeDistT = s_sim ? 8.0f : 3.0f;
    }
    s_hordeSpawnT -= dt;
    int alive = 0;
    for (const Enemy& e : G.enemies) if (!e.dead && e.type == EnemyType::Zombie && !e.roamer) alive++;
    while (s_hordeSpawnT <= 0 && s_hordePending > 0 && alive < 220) {
        s_hordeSpawnT += 0.3f;
        for (int i = 0; i < 3 && s_hordePending > 0; i++, s_hordePending--, alive++) {
            float big = clampf((s_hordeN - 2) * 0.035f, 0, 0.22f);
            float axe = clampf((s_hordeN - 1) * 0.07f, 0, 0.40f);
            float r = s_rng.f();
            int kind = r < big ? 1 : r < big + axe ? 2 : 0;
            Vec2 at = s_hordeSpawns[s_rng.next() % s_hordeSpawns.size()] + Vec2(s_rng.range(-5, 5), s_rng.range(-5, 5));
            spawnZombie(at, kind);
        }
    }
    if (s_hordePending == 0 && alive == 0) endHorde(true);
}

// Horde zombies want the hatch. Whatever gets close enough on the way - you, your
// people, a turret in their path - gets torn into first.
void updateZombie(size_t index, float dt) {
    Enemy& e = G.enemies[index];
    const ZombieKind& zk = ZOMBIE_KINDS[std::clamp(e.zkind, 0, 2)];
    Player& pl = G.player;
    Profile& p = G.prof;
    float dmg = zk.damage * (1.0f + 0.05f * (s_hordeN - 1));
    float speed = zk.speed * e.speedMul;

    // The zone's own dead: their own ways by day. At night the ones near you join the
    // hunt like any other; the rest stay where they are.
    if (e.roamer) {
        if (!G.nightFallen || inCrypt(e.pos)) { updateRoamer(index, dt); return; }
        if (nearestTargetDist(e.pos) > 520) return;
    }

    if (G.nightFallen) {
        // Night: they stop caring about the bunker and come for you, through anything.
        int ti = nearestTarget(e.pos);
        if (ti < 0) return;
        Vec2 tp = s_targets[ti].pos;
        Vec2 dir = normalize(tp - e.pos);
        Vec2 wdir = dist(e.pos, tp) > 40 ? zombieWeave(e, dir) : dir;
        e.pos = G.world.move(e.pos, (wdir + separation(e, index) * 0.6f) * speed * 1.35f * dt, ENEMY_R, true);
        e.angle = angleOf(dir);
        if (dist(e.pos, tp) < zk.reach && e.meleeCd <= 0) {
            e.meleeCd = zk.attackCd;
            if (s_targets[ti].slot == mySlot()) sfx(Snd::melee, 0.6f);
            hurtSlot(s_targets[ti].slot, dmg * dayThreat(), "Taken by the night.");
        }
        return;
    }

    // ---- the axe zombie (0.12v): throws its axe at someone it can see a little way
    // off, then goes and takes it back up if it lies close enough; bare-handed it hits
    // for less.
    if (e.zkind == 2) {
        e.axeCd -= dt;
        if (e.takeT >= 0) {
            e.takeT -= dt;
            if (e.takeT < 0) { e.noAxe = false; e.axeCd = s_rng.range(5, 9); }
            return;
        }
        if (!e.noAxe && e.axeCd <= 0) {
            e.axeCd = 0.5f;
            for (const Target& t : s_targets) {
                float d = dist(e.pos, t.pos);
                if (d < 48 || d > 125 || !G.world.lineOfSight(e.pos, t.pos)) continue;
                FlyingAxe a;
                a.pos = e.pos + Vec2(0, -4);
                Vec2 aimAt = t.pos + (t.pos - e.pos) * 0.0f;
                a.vel = normalize(aimAt - e.pos) * 190.0f;
                a.flight = d / 190.0f;
                a.dir = (int)Art::dirFromAngle(angleOf(a.vel));
                a.owner = e.netId;
                a.dmg = dmg * 1.3f;
                s_axes.push_back(a);
                e.noAxe = true;
                e.angle = angleOf(a.vel);
                e.meleeCd = zk.attackCd;   // the throw is its swing
                sfxAt(Snd::toss, e.pos, pl.pos, 0.6f, 0.8f);
                return;
            }
        }
        if (e.noAxe) {
            dmg *= 0.6f;
            // Its axe, lying close: go and pick it up.
            for (size_t i = 0; i < s_axes.size(); i++) {
                FlyingAxe& a = s_axes[i];
                if (a.owner != e.netId || a.stage < 2) continue;
                float d = dist(a.pos, e.pos);
                if (d < 9) { e.takeT = 0.8f; s_axes.erase(s_axes.begin() + i); return; }
                if (d < 110 && nearestTargetDist(e.pos) > 36) {
                    Vec2 dir = normalize(a.pos - e.pos);
                    e.pos = G.world.move(e.pos, (dir + separation(e, index) * 0.5f) * speed * dt, ENEMY_R);
                    turnToward(e.angle, angleOf(dir), 6, dt);
                    return;
                }
            }
        }
    }

    // ---- choose what to go for
    e.retargetT -= dt;
    if (e.retargetT <= 0) {
        e.retargetT = 0.3f;
        e.ztarget = -1;
        float best = 90;
        for (const Target& t : s_targets) {
            float d = dist(e.pos, t.pos);
            if (d < best && G.world.lineOfSight(e.pos, t.pos)) { best = d; e.ztarget = -2; e.tslot = t.slot; }
        }
        for (int i = 0; i < (int)s_mercs.size(); i++) {
            const Hireling& h = *s_mercs[i];
            if (h.dead) continue;
            float d = dist(e.pos, h.pos);
            if (d < best && G.world.lineOfSight(e.pos, h.pos)) { best = d; e.ztarget = i; }
        }
        if (e.ztarget == -1) {
            float bt = 22;
            for (int i = 0; i < (int)p.turrets.size(); i++) {
                if (p.turrets[i].hp <= 0) continue;
                float d = dist(e.pos, turretPos(p.turrets[i]));
                if (d < bt) { bt = d; e.ztarget = 1000 + i; }
            }
        }
    }
    Vec2 goal = G.world.homePos;
    float reach = zk.reach + 6;
    const Target* tt = e.ztarget == -2 ? targetOf(e.tslot) : nullptr;
    if (tt) { goal = tt->pos; reach = zk.reach; }
    else if (e.ztarget >= 2000 && e.ztarget - 2000 < (int)p.barricades.size() && p.barricades[e.ztarget - 2000].hp > 0) { goal = barricadePos(p.barricades[e.ztarget - 2000]); reach = zk.reach + 9; }
    else if (e.ztarget >= 1000 && e.ztarget < 2000 && e.ztarget - 1000 < (int)p.turrets.size() && p.turrets[e.ztarget - 1000].hp > 0) { goal = turretPos(p.turrets[e.ztarget - 1000]); reach = zk.reach + 6; }
    else if (e.ztarget >= 0 && e.ztarget < (int)s_mercs.size() && !s_mercs[e.ztarget]->dead) { goal = s_mercs[e.ztarget]->pos; reach = zk.reach; }
    else e.ztarget = -1;

    float gd = dist(e.pos, goal);
    if (gd <= reach) {
        e.angle = angleOf(goal - e.pos);
        if (e.meleeCd <= 0) {
            e.meleeCd = zk.attackCd;
            sfxAt(Snd::melee, e.pos, pl.pos, 0.5f, s_rng.range(0.8f, 1.1f));
            if (e.ztarget == -2) hurtSlot(e.tslot, dmg, "Torn apart by the horde.");
            else if (e.ztarget >= 2000) damageBarricade(e.ztarget - 2000, dmg);
            else if (e.ztarget >= 1000) damageTurret(e.ztarget - 1000, dmg);
            else if (e.ztarget >= 0) damageHireling(*s_mercs[e.ztarget], dmg);
            else damageBase(dmg);
        }
        return;
    }

    // ---- move: straight at a close target, along the cost field toward the hatch
    Vec2 move;
    int nx = -1, ny = -1;
    if (e.ztarget != -1) move = normalize(goal - e.pos);
    else if (homeStep(e.pos, nx, ny)) {
        const Tile& next = G.world.at(nx, ny);
        const SolidInfo& si = solidInfo(next.solid);
        Vec2 nc = World::tileCenter(nx, ny);
        // Something breakable stands in the way: stop and break it.
        if (next.solid != S_NONE && si.blocksMove && (si.radius <= 0 || next.solid == S_TREE) && si.hp > 0 && dist(e.pos, nc) < 15) {
            e.angle = angleOf(nc - e.pos);
            if (e.meleeCd <= 0) {
                e.meleeCd = zk.attackCd;
                int col = si.mapColor;
                if (G.world.damageTile(nx, ny, dmg * 1.5f)) addParticles(nc, 8, col, 20, 70, 0.3f, 0.7f, false, 2);
                else addParticles(nc, 3, col, 15, 40, 0.2f, 0.4f);
                sfxAt(Snd::tile_hit, nc, pl.pos, 0.35f, s_rng.range(0.7f, 0.9f));
            }
            return;
        }
        if (next.solid == S_BARRICADE || next.solid == S_GATE) {
            int bi = barricadeAtTile(nx, ny);
            if (bi >= 0) e.ztarget = 2000 + bi;
        }
        if (next.solid == S_TURRET) {
            for (int i = 0; i < (int)p.turrets.size(); i++)
                if (G.world.homeTx + p.turrets[i].dx == nx && G.world.homeTy + p.turrets[i].dy == ny) e.ztarget = 1000 + i;
        }
        move = normalize(nc - e.pos);
    } else {
        move = normalize(goal - e.pos);
    }
    if (e.ztarget != -1 && e.stuckT > 0.3f && zombieChop(e, move, dmg, zk.attackCd)) return;
    if (e.unstickT > 0) { e.unstickT -= dt; move = e.unstickDir; }
    else if (gd > 40) move = zombieWeave(e, move);
    Vec2 before = e.pos;
    Vec2 vel = move * speed + separation(e, index) * 18.0f;
    e.pos = G.world.move(e.pos, vel * dt, 4.0f);
    turnToward(e.angle, angleOf(move), 6, dt);
    if (dist(before, e.pos) < speed * dt * 0.25f) {
        e.stuckT += dt;
        if (e.stuckT > 0.7f) {
            e.unstickDir = fromAngle(s_rng.range(0, 2 * PI));
            e.unstickT = 0.35f;
            e.stuckT = 0;
        }
    } else {
        e.stuckT = std::max(0.0f, e.stuckT - dt);
    }
}

// ---- turrets
bool validTarget(int i, Vec2 from, float range) {
    if (i < 0 || i >= (int)G.enemies.size()) return false;
    const Enemy& e = G.enemies[i];
    return !e.dead && !immune(e) && dist(e.pos, from) <= range + 6;
}

void fireTurret(Turret& t, const TurretStats& st) {
    Vec2 head = turretHead(t);
    Vec2 f = fromAngle(t.angle);
    Vec2 muzzle = head + f * 8.0f;
    switch (t.type) {
    case TT_GUN:
    case TT_AUTO:
    case TT_ROCKET: {
        Bullet b;
        bool rocket = t.type == TT_ROCKET;
        float spread = t.type == TT_AUTO ? 0.06f : rocket ? 0.02f : 0.03f;
        b.pos = muzzle;
        b.vel = fromAngle(t.angle + spread * (s_rng.f() + s_rng.f() - 1.0f)) * (rocket ? 260.0f : 560.0f);
        b.damage = st.damage;
        b.rangeLeft = st.range + 24;
        b.fromPlayer = true;
        b.turret = true;
        b.explosive = rocket;
        b.weapon = rocket ? IT_LAUNCHER : IT_NONE;
        G.bullets.push_back(b);
        if (Coop::host()) s_shotLog.push_back({muzzle, angleOf(b.vel), (uint8_t)(rocket ? IT_LAUNCHER : IT_NONE),
                                               (uint8_t)(SF_PLAYER | SF_TURRET | (rocket ? SF_EXPLOSIVE : 0)), -1});
        t.flashT = 0.08f;
        addFlash(muzzle, 50, 0.06f, 0.8f, pal(P_YELLOW));
        int snd = rocket ? Snd::launcher : t.type == TT_AUTO ? Snd::smg : Snd::pistol;
        sfxAt(snd, head, G.player.pos, rocket ? 0.5f : 0.3f, s_rng.range(1.05f, 1.2f));
        break;
    }
    case TT_LASER: {
        // A hitscan beam through everything in line, stopped only by a solid wall.
        Vec2 end = head;
        for (float d = 0; d < st.range; d += 3) {
            Vec2 pnt = head + f * d;
            if (d > 8 && G.world.blocksBulletAt(pnt)) break;
            end = pnt;
        }
        float len = dist(head, end);
        for (Enemy& e : G.enemies) {
            if (e.dead || immune(e)) continue;
            Vec2 rel = e.pos - head;
            float along = dot(rel, f);
            if (along < 0 || along > len + 4) continue;
            if (length(rel - f * along) < 6.0f) damageEnemy(e, st.damage, f);
        }
        t.beamT = 0.14f;
        t.beamEnd = end;
        addFlash(end, 40, 0.1f, 0.7f, pal(P_PINK));
        addParticles(end, 3, P_PINK, 10, 40, 0.1f, 0.25f, true);
        sfxAt(Snd::sniper, head, G.player.pos, 0.25f, 1.9f);
        break;
    }
    case TT_FLAME: {
        // Every tick burns whatever is in the cone.
        for (Enemy& e : G.enemies) {
            if (e.dead || immune(e)) continue;
            Vec2 rel = e.pos - head;
            float d = length(rel);
            if (d > st.range || std::fabs(angleDiff(t.angle, angleOf(rel))) > 0.42f) continue;
            if (!G.world.lineOfSight(head, e.pos)) continue;
            damageEnemy(e, st.damage, normalize(rel));
        }
        if (!s_sim) {
            for (int i = 0; i < 3; i++) {
                Particle pt;
                pt.pos = muzzle;
                pt.vel = fromAngle(t.angle + s_rng.range(-0.35f, 0.35f)) * s_rng.range(st.range * 1.3f, st.range * 1.9f);
                pt.life = pt.maxLife = s_rng.range(0.3f, 0.5f);
                pt.color = s_rng.chance(0.5f) ? P_ORANGE : P_YELLOW;
                pt.glow = true;
                pt.size = s_rng.chance(0.3f) ? 2 : 1;
                pt.drag = 2.5f;
                G.particles.push_back(pt);
            }
            if (s_rng.chance(0.12f)) sfxAt(Snd::explosion, head, G.player.pos, 0.12f, 2.6f);
        }
        t.flashT = 0.08f;
        addFlash(muzzle + f * 20, 70, 0.08f, 0.6f, pal(P_ORANGE));
        break;
    }
    }
}

void updateTurrets(float dt) {
    Profile& p = G.prof;
    for (Turret& t : p.turrets) {
        t.flashT -= dt;
        t.hurtT -= dt;
        t.beamT -= dt;
        t.cd -= dt;
        if (t.hp <= 0) continue;
        TurretStats st = turretStats(t);
        Vec2 head = turretHead(t);
        t.retargetT -= dt;
        if (t.retargetT <= 0 || !validTarget(t.target, head, st.range)) {
            t.retargetT = 0.2f;
            t.target = -1;
            float best = st.range;
            for (int i = 0; i < (int)G.enemies.size(); i++) {
                const Enemy& e = G.enemies[i];
                if (e.dead || immune(e)) continue;
                float d = dist(e.pos, head);
                if (d >= best || !G.world.lineOfSight(head, e.pos)) continue;
                best = d;
                t.target = i;
            }
        }
        if (t.target < 0) continue;
        const Enemy& e = G.enemies[t.target];
        // Lead the target by the shot's flight time so bullets meet walkers rather
        // than trail them. The beam and the flames arrive instantly.
        Vec2 aimAt = e.pos;
        if (t.type != TT_LASER && t.type != TT_FLAME && dt > 0) {
            float flight = dist(e.pos, head) / (t.type == TT_ROCKET ? 260.0f : 560.0f);
            aimAt += (e.pos - e.lastPos) / dt * flight;
        }
        float want = angleOf(aimAt - head);
        turnToward(t.angle, want, t.type == TT_ROCKET ? 4.0f : 8.0f, dt);
        float tol = t.type == TT_FLAME ? 0.5f : 0.2f;
        if (t.cd <= 0 && std::fabs(angleDiff(t.angle, want)) < tol) {
            t.cd = 1.0f / st.rate;
            fireTurret(t, st);
        }
    }
}

// ---- hired guns
void damageHireling(Hireling& h, float dmg) {
    if (h.dead) return;
    if (h.rideCar >= 0)
        if (Car* c = carOf(h.rideCar); c && !c->wrecked) { damageCar(*c, dmg * 0.6f); return; }
    h.hp -= dmg;
    h.hurtT = 0.2f;
    bloodSplash(h.pos, randomDir());
    if (h.hp > 0) return;
    h.hp = 0;
    h.dead = true;
    // Gone for good. What they carried is on the body for whoever finds it.
    int id = G.world.addContainer(h.pos, CK_CORPSE, -1, -1, (uint8_t)((s_rng.next() & 0x3F) | (hireTier(h.tier).helmet ? 0x40 : 0)));
    Container& c = G.world.containers[id];
    c.searchTime = 0.9f;
    const HireTier& ht = hireTier(h.tier);
    addToSlots(c.items, makeItem(ht.weapon));
    if (const WeaponDef* wd = weaponDef(ht.weapon)) addToSlots(c.items, makeItem(wd->ammo, 30));
    bool mine = !Coop::active() || h.owner == mySlot();
    if (Coop::host() && !mine) {
        Coop::player(h.owner).deadMercs.push_back(h.name);
        Net::Writer w;
        w.u8(Coop::M_MERC_DIED);
        w.str(h.name);
        Coop::sendReliable(h.owner, w);
    }
    if (!s_sim) {
        addParticles(h.pos, 20, P_CORAL, 20, 80, 0.3f, 0.8f, false, 2);
        G.decals.push_back({h.pos, BLOOD0, s_rng.range(0, 6.28f)});
        spawnBloodPool(h.pos);
        sfxAt(Snd::hurt, h.pos, G.player.pos, 0.8f, 0.8f);
        pushMessage(T1("{0} has been killed.", h.name), P_CORAL);
        if (mine) bigText(T1("{0} IS DEAD", h.name), P_CORAL, 3);
    } else if (mine) {
        s_hordeNote += (s_hordeNote.empty() ? "" : " ") + T1("{0} died.", h.name);
    }
}

Vec2 guardPost(int n) {
    static const int POSTS[8][2] = {{-5, 5}, {5, 5}, {0, -6}, {-6, -4}, {6, -4}, {0, 8}, {-8, 1}, {8, 1}};
    World& w = G.world;
    int px = w.homeTx + POSTS[n % 8][0], py = w.homeTy + POSTS[n % 8][1];
    for (int r = 0; r < 4; r++)
        for (int y = py - r; y <= py + r; y++)
            for (int x = px - r; x <= px + r; x++)
                if (w.inBounds(x, y) && !w.blocksMove(x, y) && w.at(x, y).solid == S_NONE) return World::tileCenter(x, y);
    return World::tileCenter(px, py);
}

// Is this merc's owner outside right now (so a follower should be out with them)?
bool ownerOutside(int owner) {
    if (owner == mySlot()) return G.scene == Scene::Raid && !s_sim;
    if (!Coop::host()) return false;
    const Coop::NetPlayer& np = Coop::player(owner);
    return np.used && np.where == Coop::W_RAID;
}

// Every player's squad, as one list: the host's own and each guest's (their
// characters live here). Guards stand their posts whenever the world runs; a
// follower comes out when its owner does and goes home when they do.
void rebuildMercs() {
    s_mercs.clear();
    std::vector<std::pair<int, std::vector<Hireling>*>> squads;
    squads.push_back({mySlot(), &G.prof.squad});
    if (Coop::host())
        for (int i = 1; i < Coop::MAX_PLAYERS; i++) {
            Coop::NetPlayer& np = Coop::player(i);
            if (np.used && np.prof) squads.push_back({i, &np.prof->squad});
        }
    int guardN = 0;
    for (auto& [owner, squad] : squads) {
        int followN = 0;
        for (Hireling& h : *squad) {
            h.owner = owner;
            if (h.dead) continue;
            bool want = h.guard || ownerOutside(owner);
            if (want && !h.out) {
                h.out = true;
                h.target = -1;
                h.fireCd = h.reloadT = h.hurtT = h.flashT = h.stuckT = h.unstickT = h.retargetT = 0;
                h.hp = std::min(h.hp, h.maxHp());
                if (const WeaponDef* wd = weaponDef(hireTier(h.tier).weapon)) h.mag = wd->magSize;
                if (h.guard) {
                    h.post = guardPost(guardN);
                    h.pos = h.post;
                    h.angle = angleOf(h.pos - G.world.homePos);
                } else {
                    Vec2 op = slotPos(owner);
                    h.pos = op + Vec2(-14.0f + 14.0f * followN, 12);
                    if (G.world.collides(h.pos.x, h.pos.y, PLAYER_R)) h.pos = op;
                    h.angle = PI / 2;
                }
                h.lastPos = h.pos;
            } else if (!want) {
                h.out = false;
            }
            if (h.guard) { h.post = guardPost(guardN); guardN++; }
            else followN++;
            if (h.out) s_mercs.push_back(&h);
        }
    }
}

void updateHireling(int index, float dt) {
    Hireling& h = *s_mercs[index];
    if (h.dead || !h.out) return;
    Player& pl = G.player;
    bool follower = !h.guard;
    Vec2 ownerPos = slotPos(h.owner);
    float ownerAngle = h.owner == mySlot() ? pl.angle : Coop::player(h.owner).angle;
    bool ownerDown = h.owner == mySlot() ? (s_deathT >= 0 || s_downT >= 0) : Coop::player(h.owner).downed;
    const HireTier& ht = hireTier(h.tier);
    const WeaponDef* wd = weaponDef(ht.weapon);
    h.lastPos = h.pos;
    h.fireCd -= dt;
    h.hurtT -= dt;
    h.flashT -= dt;
    if (h.reloadT > 0) {
        h.reloadT -= dt;
        if (h.reloadT <= 0 && wd) { h.mag = wd->magSize; sfxAt(Snd::reload_end, h.pos, pl.pos, 0.3f); }
    }

    // Pick the nearest living hostile they can see.
    h.retargetT -= dt;
    if (h.retargetT <= 0 || !validTarget(h.target, h.pos, ht.sight)) {
        h.retargetT = 0.25f;
        h.target = -1;
        float best = ht.sight;
        for (int i = 0; i < (int)G.enemies.size(); i++) {
            const Enemy& e = G.enemies[i];
            if (e.dead || immune(e) || e.type == EnemyType::Shade) continue;
            float d = dist(e.pos, h.pos);
            if (d >= best || !G.world.lineOfSight(h.pos, e.pos)) continue;
            best = d;
            h.target = i;
        }
    }

    // Riding in a car (0.11v): no walking, just shooting out of the windows.
    if (h.rideCar >= 0) {
        Car* c = carOf(h.rideCar);
        if (c && !c->wrecked) {
            int seat = 0;
            for (int i = 0; i < index; i++) if (s_mercs[i]->rideCar == h.rideCar) seat++;
            Vec2 f = fromAngle(c->angle), s(-f.y, f.x);
            h.pos = h.lastPos = c->pos - f * (float)(seat % 3 * 5 - 5) + s * (seat % 2 ? 4.0f : -4.0f);
            h.unstickT = h.stuckT = 0;
            if (h.target < 0 || !wd) return;
            const Enemy& e = G.enemies[h.target];
            float want = angleOf(e.pos - h.pos);
            turnToward(h.angle, want, 8, dt);
            if (h.mag <= 0) {
                if (h.reloadT <= 0) { h.reloadT = wd->reloadTime * 1.2f; sfxAt(Snd::reload, h.pos, pl.pos, 0.3f); }
                return;
            }
            if (h.reloadT <= 0 && h.fireCd <= 0 && std::fabs(angleDiff(h.angle, want)) < 0.3f && dist(e.pos, h.pos) < wd->range * 0.9f) {
                Vec2 muzzle = h.pos + fromAngle(h.angle) * (carModel(c->model).halfWid + 4);
                s_shotOwner = 10 + h.owner;
                spawnBullets(muzzle, h.angle, *wd, ht.weapon, 1.2f, ht.spread, true, ht.damageMul);
                s_shotOwner = -1;
                h.mag--;
                h.flashT = 0.09f;
                // A bumpy ride: they shoot a little slower from a moving car.
                h.fireCd = 1.0f / (wd->fireRate * 0.7f) * s_rng.range(0.85f, 1.25f);
                sfxAt(wd->sound, h.pos, pl.pos, 0.5f, s_rng.range(0.95f, 1.05f));
                alertEnemies(h.pos, 320);
            }
            return;
        }
        h.rideCar = -1;
    }
    // Where to stand: a slot behind their owner, or their post at the base.
    int slot = 0;
    for (int i = 0; i < index; i++) if (!s_mercs[i]->dead && s_mercs[i]->guard == h.guard && s_mercs[i]->owner == h.owner) slot++;
    if (follower && !sameArea(h.pos, ownerPos)) {
        // Their owner took the stairs: they follow down (or up) behind them.
        h.pos = h.lastPos = ownerPos + fromAngle(ownerAngle + PI + (slot - 1) * 0.75f) * 18.0f;
        if (G.world.collides(h.pos.x, h.pos.y, 5)) h.pos = h.lastPos = ownerPos;
    }
    Vec2 goal;
    if (follower) goal = ownerDown ? h.pos : ownerPos + fromAngle(ownerAngle + PI + (slot - 1) * 0.75f) * 22.0f;
    else goal = h.post;
    float gd = dist(h.pos, goal);
    if (follower && gd > 520 && !ownerDown) {
        // Lost them: they catch up off screen rather than wander the map alone.
        for (int k = 0; k < 8; k++) {
            Vec2 c = ownerPos + fromAngle(ownerAngle + PI + k * 0.8f) * 26.0f;
            if (!G.world.collides(c.x, c.y, PLAYER_R)) { h.pos = c; break; }
        }
        gd = dist(h.pos, goal);
    }
    Vec2 move;
    if (gd > (follower ? 16.0f : 6.0f)) {
        Vec2 dir;
        if (follower && h.owner == mySlot() && gd > 40 && G.world.flowDir(h.pos, dir)) move = dir;
        else move = normalize(goal - h.pos);
    }
    if (h.unstickT > 0) { h.unstickT -= dt; move = h.unstickDir; }
    if (lengthSq(move) > 0.0001f) {
        float speed = 64.0f * (gd > 90 ? 1.5f : 1.0f) * (h.target >= 0 ? 0.7f : 1.0f);
        if (G.world.openDoorNear(h.pos + normalize(move) * 8.0f, 18.0f)) sfxAt(Snd::door, h.pos, pl.pos, 0.4f);
        Vec2 push;
        for (const Hireling* o : s_mercs)
            if (o != &h && !o->dead && lengthSq(h.pos - o->pos) < 100) push += normalize(h.pos - o->pos);
        if (follower && lengthSq(h.pos - ownerPos) < 100) push += normalize(h.pos - ownerPos);
        Vec2 before = h.pos;
        h.pos = G.world.move(h.pos, (normalize(move) * speed + push * 20.0f) * dt, PLAYER_R);
        if (dist(before, h.pos) < speed * dt * 0.25f) {
            h.stuckT += dt;
            if (h.stuckT > 0.6f) { h.unstickDir = fromAngle(s_rng.range(0, 2 * PI)); h.unstickT = 0.4f; h.stuckT = 0; }
        } else {
            h.stuckT = std::max(0.0f, h.stuckT - dt);
        }
        if (h.target < 0) turnToward(h.angle, angleOf(move), 6, dt);
    }

    h.meleeT += dt;
    if (h.target < 0 || !wd) return;
    Enemy& e = G.enemies[h.target];
    float want = angleOf(e.pos - h.pos);
    turnToward(h.angle, want, 8, dt);
    // One of the dead right on top of them (0.12v): a punch to shove it off, then shoot.
    if (e.type == EnemyType::Zombie && !e.dead && dist(e.pos, h.pos) < 15 && h.meleeT > 0.8f) {
        h.meleeT = 0;
        Vec2 f = normalize(e.pos - h.pos);
        sfxAt(Snd::melee, h.pos, pl.pos, 0.4f, 1.1f);
        s_dmgOwner = 10 + h.owner;
        damageEnemy(e, 12.0f * ht.damageMul, f);
        s_dmgOwner = -1;
        if (!e.dead) {
            e.pos = G.world.move(e.pos, f * (e.zkind == 1 ? 3.0f : 8.0f), ENEMY_R);
            e.meleeCd = std::max(e.meleeCd, 0.4f);
        }
        return;
    }
    if (h.meleeT < 0.3f) return;
    if (h.mag <= 0) {
        if (h.reloadT <= 0) { h.reloadT = wd->reloadTime * 1.2f; sfxAt(Snd::reload, h.pos, pl.pos, 0.3f); }
        return;
    }
    if (h.reloadT <= 0 && h.fireCd <= 0 && std::fabs(angleDiff(h.angle, want)) < 0.25f && dist(e.pos, h.pos) < wd->range * 0.9f) {
        Vec2 muzzle = h.pos + fromAngle(h.angle) * 9;
        s_shotOwner = 10 + h.owner;
        spawnBullets(muzzle, h.angle, *wd, ht.weapon, 1.0f, ht.spread, true, ht.damageMul);
        s_shotOwner = -1;
        h.mag--;
        h.flashT = 0.09f;
        h.fireCd = 1.0f / (wd->fireRate * 0.75f) * s_rng.range(0.85f, 1.2f);
        sfxAt(wd->sound, h.pos, pl.pos, 0.5f, s_rng.range(0.95f, 1.05f));
        // Their gunfire is heard like yours (0.11v fix: raiders used to ignore it).
        alertEnemies(h.pos, 320);
    }
}

void updateSquad(float dt) {
    for (int i = 0; i < (int)s_mercs.size(); i++) updateHireling(i, dt);
}

// Anyone who died this frame is gone from their owner's roster; anything that was
// aiming at a hireling by index looks again.
// The rosters themselves are searched, not just who is out right now: a merc killed
// between two rebuilds of that list has already dropped out of it, and used to stay
// on the roster for good as a dead man standing in the bunker (fixed in 0.11v).
void buryHirelings() {
    auto bury = [](std::vector<Hireling>& sq) {
        size_t n = sq.size();
        sq.erase(std::remove_if(sq.begin(), sq.end(), [](const Hireling& h) { return h.dead; }), sq.end());
        return sq.size() != n;
    };
    bool any = bury(G.prof.squad);
    if (Coop::host())
        for (int i = 1; i < Coop::MAX_PLAYERS; i++)
            if (Coop::player(i).used && Coop::player(i).prof) any = bury(Coop::player(i).prof->squad) || any;
    if (!any) return;
    for (Enemy& e : G.enemies) {
        if (e.ztarget >= 0 && e.ztarget < 1000) e.ztarget = e.type == EnemyType::Zombie ? -1 : -2;
        e.retargetT = 0;
        e.losT = 0;
    }
    rebuildMercs();
}

// A fresh trip out: everyone's people get placed again rather than left wherever
// they stood last time.
void spawnSquad() {
    for (Hireling& h : G.prof.squad) h.out = false;
    rebuildMercs();
}

// ================================================================ cars (0.11v)
bool s_noCarHook = false;     // measuring the ground only, without the cars on it
bool noCarBlock(float x, float y, float r) { return !s_noCarHook && carBlockHook(x, y, r); }
float noCarOverlap(float x, float y, float r) { return s_noCarHook ? 0.0f : carOverlapHook(x, y, r); }

// A walker (you, a raider, a merc) caught against a car that drove into them is
// eased out of its way.
void pushOutOfCars(Vec2& p, float r, int skipOwner = -99) {
    for (const Car& c : s_cars) {
        if (c.owner == skipOwner) continue;
        float o = carOverlap(c, p, r);
        if (o <= 0) continue;
        Vec2 away = p - c.pos;
        if (lengthSq(away) < 0.01f) away = fromAngle(c.angle + PI / 2);
        Vec2 np = p + normalize(away) * std::min(o + 0.2f, 4.0f);
        s_noCarHook = true;
        bool free = !G.world.collides(np.x, np.y, r);
        s_noCarHook = false;
        if (free) p = np;
    }
}

// Profile <- my car, so it is where I left it next time (and saved with the raid).
void persistMyCar() {
    Car* c = myCar();
    Profile& p = G.prof;
    if (!c || p.activeCar != c->model || !p.cars[c->model].owned) return;
    Profile::OwnedCar& oc = p.cars[c->model];
    oc.hp = c->wrecked ? 0.0f : c->hp;
    oc.fuel = c->fuel;
    p.carDay = p.day;
    p.carRev = p.dayRev;
    p.carPos = c->pos;
    p.carAngle = c->angle;
}

// Puts my car in the world: where it was left today, else in its bay at the yard.
void spawnMyCar() {
    s_cars.erase(std::remove_if(s_cars.begin(), s_cars.end(), [](const Car& c) { return c.owner == mySlot(); }), s_cars.end());
    Profile& p = G.prof;
    if (s_ride == mySlot()) s_ride = -1;
    if (p.activeCar < 0 || !p.cars[p.activeCar].owned || !mechanicOut()) return;
    const Profile::OwnedCar& oc = p.cars[p.activeCar];
    const CarModel& m = carModel(p.activeCar);
    Car c;
    c.owner = mySlot();
    c.model = p.activeCar;
    c.color = oc.color;
    c.hp = oc.hp < 0 ? m.hp : std::min(m.hp, oc.hp);
    c.fuel = oc.fuel < 0 ? m.tank : std::min(m.tank, oc.fuel);
    bool out = p.carOut();
    if (c.hp <= 0) {
        // Wrecked: the burnt shell stays where it died today; otherwise it sits at the
        // mechanic's until he has rebuilt it.
        if (!out) return;
        c.wrecked = true;
        c.hp = 0;
    }
    c.pos = out ? p.carPos : garageBay(mySlot());
    c.angle = out ? p.carAngle : PI / 2;
    c.netPos = c.pos;
    c.netAngle = c.angle;
    s_cars.push_back(c);
    persistMyCar();
}

// Getting in: your own car puts you at the wheel; a friend's (co-op) in a seat.
void enterCar(int owner) {
    Car* c = carOf(owner);
    if (!c || c->wrecked) return;
    if (owner != mySlot()) {
        if (rivalsOn()) return;
        if (playersAboard(owner) >= carModel(c->model).seats) { pushMessage(T("No free seat in there."), P_CORAL); return; }
    }
    closeLoot();
    s_ride = owner;
    G.player.moving = false;
    if (owner == mySlot()) {
        c->driven = true;
        // The key turns; the engine catches (the drone comes up by itself).
        if (c->fuel > 0) { sfx(Snd::click, 0.8f, 0.6f); sfx(Snd::car_hit, 0.35f, 0.5f); }
        else sfx(Snd::empty, 0.8f, 0.7f);
        const CarModel& m = carModel(c->model);
        pushMessage(c->fuel <= 0 ? T("The tank is empty. Refuel it with a Fuel Can, or at the mechanic's.")
                                 : T2("{0}: {1} seats. Mind the fuel.", T(m.name), std::to_string(m.seats)), P_LAVENDER);
    } else {
        pushMessage(T1("You ride in {0}'s car. You can shoot from it.", Coop::player(owner).name), P_LAVENDER);
    }
    sfxAt(Snd::car_door, c->pos, G.player.pos, 0.9f, s_rng.range(0.95f, 1.1f));
}

// Getting out beside the car (left door first). `thrown`: no choice about it (it blew
// up, or it is gone), so out you go wherever there is room.
bool leaveCar(bool thrown) {
    Car* c = carOf(s_ride);
    if (!c) { s_ride = -1; return true; }
    const CarModel& m = carModel(c->model);
    if (!thrown && driving() && c->speed() > 45) {
        if (s_carMsgT <= 0) { pushMessage(T("Slow down to get out."), P_ORANGE); s_carMsgT = 1.5f; }
        return false;
    }
    Vec2 f = fromAngle(c->angle), s(-f.y, f.x);
    Vec2 tries[6] = {c->pos - s * (m.halfWid + 8), c->pos + s * (m.halfWid + 8), c->pos - f * (m.halfLen + 8), c->pos + f * (m.halfLen + 8),
                     c->pos - s * (m.halfWid + 16), c->pos + s * (m.halfWid + 16)};
    int pick = -1;
    for (int i = 0; i < 6 && pick < 0; i++)
        if (!G.world.collides(tries[i].x, tries[i].y, PLAYER_R) && G.world.areaAt(tries[i]) == G.world.areaAt(c->pos)) pick = i;
    if (pick < 0) {
        if (!thrown) { pushMessage(T("No room to open a door here."), P_ORANGE); return false; }
        pick = 0;
    }
    if (driving()) {
        c->driven = false;
        persistMyCar();
    }
    s_ride = -1;
    G.player.pos = tries[pick];
    G.player.angle = angleOf(tries[pick] - c->pos);
    sfxAt(Snd::car_door, c->pos, G.player.pos, 0.9f, s_rng.range(0.9f, 1.05f));
    return true;
}

void wreckCar(Car& c) {
    if (c.wrecked) return;
    c.wrecked = true;
    c.hp = 0;
    c.vel = Vec2();
    c.angVel = 0;
    c.driven = false;
    if (c.owner == mySlot()) {
        Profile& p = G.prof;
        if (p.cars[c.model].owned) p.cars[c.model].hp = 0;
        persistMyCar();
        bigText(T("YOUR CAR IS WRECKED"), P_CORAL, 3);
        pushMessage(T("The mechanic can rebuild it, for a price."), P_ORANGE);
    }
    if (s_ride == c.owner) leaveCar(true);   // thrown clear as it goes up
    // The tank goes up: it hurts whatever is round it. The host's world does the harm.
    if (!isGuest()) {
        s_dmgOwner = -1;
        explode(c.pos, 44, 38, false, false);
    } else {
        explodeFx(c.pos, 44, false);
    }
    for (int i = 0; i < 20; i++) addParticles(c.pos + Vec2(s_rng.range(-12, 12), s_rng.range(-8, 8)), 1, P_DARK, 5, 25, 1.0f, 2.2f, false, 3);
}

void damageCar(Car& c, float dmg) {
    if (c.wrecked || dmg <= 0) return;
    c.hurtT = 0.12f;
    if (c.owner == mySlot()) {
        if (G.devGod) return;
        c.hp -= dmg;
        if (c.hp <= 0) wreckCar(c);
    } else if (Coop::host()) {
        // Someone else's: their game keeps it. Tell them, and show it here meanwhile.
        Net::Writer w;
        w.u8(Coop::M_CAR_DMG);
        w.f32(dmg);
        Coop::sendReliable(c.owner, w);
        c.hp = std::max(1.0f, c.hp - dmg);
    }
}

// Host/solo: mercenaries whose owner is in a car ride along, as long as there is a
// seat left once the players are in. Everyone else walks.
void assignSeats() {
    int freeSeats[Coop::MAX_PLAYERS];
    for (int i = 0; i < Coop::MAX_PLAYERS; i++) {
        Car* c = carOf(i);
        freeSeats[i] = c && !c->wrecked ? carModel(c->model).seats - playersAboard(i) : 0;
    }
    for (Hireling* h : s_mercs) {
        int want = -1;
        if (!h->dead && !h->guard) {
            int r = rideOfSlot(h->owner);
            if (r >= 0 && r < Coop::MAX_PLAYERS && freeSeats[r] > 0) { want = r; freeSeats[r]--; }
        }
        if (h->rideCar >= 0 && want < 0) {
            // Out they get, beside the car.
            if (Car* c = carOf(h->rideCar)) {
                Vec2 f = fromAngle(c->angle), s(-f.y, f.x);
                const CarModel& m = carModel(c->model);
                Vec2 spots[4] = {c->pos + s * (m.halfWid + 9), c->pos - s * (m.halfWid + 9), c->pos - f * (m.halfLen + 9), c->pos + f * (m.halfLen + 9)};
                for (Vec2 sp : spots)
                    if (!G.world.collides(sp.x, sp.y, PLAYER_R)) { h->pos = h->lastPos = sp; break; }
            }
        }
        h->rideCar = want;
    }
}

// The driver's game: whatever the car hits at speed goes down, and is thrown aside.
struct RunHit { uint32_t id; float t; };
std::vector<RunHit> s_runHits;
void runOver(Car& c, float dt) {
    for (RunHit& r : s_runHits) r.t -= dt;
    s_runHits.erase(std::remove_if(s_runHits.begin(), s_runHits.end(), [](const RunHit& r) { return r.t <= 0; }), s_runHits.end());
    const CarModel& m = carModel(c.model);
    float spd = c.speed();
    Vec2 dir = spd > 1 ? c.vel / spd : fromAngle(c.angle);
    for (Enemy& e : G.enemies) {
        if (e.dead || !carContains(c, e.pos, ENEMY_R + 1.5f)) continue;
        bool hitAlready = false;
        for (const RunHit& r : s_runHits) hitAlready = hitAlready || r.id == e.netId;
        if (spd > 40 && !hitAlready) {
            s_runHits.push_back({e.netId, 0.6f});
            bool big = e.type == EnemyType::Heavy || (e.type == EnemyType::Zombie && e.zkind == 1);
            float dmg = (spd - 25) * 0.85f * m.mass;
            if (isGuest()) {
                Net::Writer w;
                w.u8(Coop::M_HIT);
                w.u32(e.netId);
                w.f32(dmg);
                w.f32(dir.x);
                w.f32(dir.y);
                Coop::toHost(w, true);
                bloodSplash(e.pos, dir);
            } else {
                s_dmgOwner = mySlot();
                damageEnemy(e, dmg, dir);
                s_dmgOwner = -1;
            }
            sfxAt(Snd::car_hit, e.pos, G.player.pos, 0.8f, s_rng.range(0.8f, 1.0f));
            addShake(big ? 3.0f : 1.5f, e.pos);
            c.vel *= 1.0f - (big ? 0.16f : 0.06f) / m.mass;
            damageCar(c, big ? 3.0f : 0.5f);
        }
        if (!isGuest() && !e.dead) {
            // Shoved out of the way, off to whichever side it was on.
            Vec2 side = normalize(e.pos - c.pos);
            Vec2 perp = fromAngle(c.angle + PI / 2);
            Vec2 push = perp * (dot(side, perp) >= 0 ? 1.0f : -1.0f) * 3.0f + dir * std::min(4.0f, spd * dt);
            e.pos = G.world.move(e.pos, push, ENEMY_R);
        }
    }
}

// My car, each frame: driven, or rolling to a stop where it was left.
void updateMyCar(float dt) {
    Car* c = myCar();
    if (!c) return;
    s_carMsgT -= dt;
    s_crashCd -= dt;
    CarInput in;
    bool atWheel = driving() && s_downT < 0 && s_deathT < 0;
    bool panelBlocks = G.panel != Panel::None && G.panel != Panel::Map;
    if (atWheel && !panelBlocks) {
        if (Input::down(GLFW_KEY_W)) in.throttle += 1;
        if (Input::down(GLFW_KEY_S)) in.throttle -= 1;
        if (Input::down(GLFW_KEY_A)) in.steer -= 1;
        if (Input::down(GLFW_KEY_D)) in.steer += 1;
        in.handbrake = Input::down(GLFW_KEY_SPACE);
        if (g_devThrottle != 0 || g_devSteer != 0) { in.throttle = g_devThrottle; in.steer = g_devSteer; }
        if (Input::usingPad()) {
            // Triggers for the pedals, the left stick to steer.
            float rt = Input::padTrigger(true), lt = Input::padTrigger(false);
            if (rt > 0.05f || lt > 0.05f) in.throttle = rt - lt;
            Vec2 st = Input::moveAxis();
            if (std::fabs(st.x) > 0.15f) in.steer = clampf(st.x * 1.2f, -1, 1);
        }
    }
    c->driven = driving();
    if (!c->driven && c->speed() < 0.5f && std::fabs(c->angVel) < 0.01f) { c->vel = Vec2(); return; }
    float fuelBefore = c->fuel;
    CarStepResult res;
    carStep(*c, G.world, in, dt, true, res);
    if (fuelBefore > 0 && c->fuel <= 0 && driving()) {
        bigText(T("OUT OF FUEL"), P_ORANGE, 3);
        pushMessage(T("Refuel with a Fuel Can (E beside the car), or at the mechanic's."), P_ORANGE);
    }
    // Driving through things.
    for (auto& t : res.smashed) {
        Vec2 at = World::tileCenter(t.first, t.second);
        addParticles(at, 8, P_TAN, 20, 90, 0.3f, 0.8f, false, 2);
        sfxAt(Snd::tile_break, at, G.player.pos, 0.7f, s_rng.range(0.85f, 1.1f));
    }
    if (!res.smashed.empty() && isGuest()) {
        Net::Writer w;
        w.u8(Coop::M_CAR_SMASH);
        w.u8((uint8_t)std::min<size_t>(res.smashed.size(), 32));
        for (size_t i = 0; i < res.smashed.size() && i < 32; i++) { w.u16((uint16_t)res.smashed[i].first); w.u16((uint16_t)res.smashed[i].second); }
        Coop::toHost(w, true);
    }
    // A crash: it hurts the car, and a hard one shakes up whoever is inside.
    if (res.impact > 55) {
        const CarModel& m = carModel(c->model);
        // Only a real crash does real harm: a bump or a scrape is nothing to a car.
        float dmg = std::max(0.0f, res.impact - 85) * 0.16f / std::sqrt(m.mass);
        damageCar(*c, dmg);
        if (s_crashCd <= 0) {
            s_crashCd = 0.35f;
            sfxAt(res.impact > 140 ? Snd::car_crash : Snd::car_hit, res.hitAt, G.player.pos, clampf(res.impact / 160.0f, 0.4f, 1.0f), s_rng.range(0.9f, 1.1f));
            addParticles(res.hitAt, 6 + (int)(res.impact / 30), P_BEIGE, 20, 90, 0.2f, 0.6f, false, 1);
            if (driving()) addShake(clampf(res.impact / 40.0f, 1, 8), c->pos);
        }
        if (res.impact > 190 && s_ride >= 0) damagePlayer((res.impact - 190) * 0.12f, "Killed in a car crash.");
    }
    if (driving()) runOver(*c, dt);
    // Tyres on the gravel and grass, now and then.
    c->tyreT -= dt;
    if (driving() && c->speed() > 60 && c->tyreT <= 0) {
        int tx = World::toTile(c->pos.x), ty = World::toTile(c->pos.y);
        int g = G.world.inBounds(tx, ty) ? G.world.at(tx, ty).ground : G_ROAD;
        bool rough = g == G_GRASS || g == G_DIRT || g == G_SAND || g == G_WASTE || g == G_RUBBLE;
        c->tyreT = rough ? 0.32f : 0.9f;
        if (rough || std::fabs(dot(c->vel, fromAngle(c->angle + PI / 2))) > 40)
            sfxAt(Snd::tyres, c->pos, G.player.pos, clampf(c->speed() / 250.0f, 0.2f, 0.6f), s_rng.range(0.8f, 1.05f));
    }
    persistMyCar();
}

// Engines and tyres (0.11v): the nearest running cars get a voice each, their pitch with
// their speed and the pedal, fading with distance; yours is always one of them.
void carSounds() {
    if (s_sim) return;
    Vec2 ear = G.player.pos;
    std::vector<const Car*> running;
    for (const Car& c : s_cars)
        if (!c.wrecked && c.driven && c.fuel > 0 && dist(c.pos, ear) < 600) running.push_back(&c);
    std::sort(running.begin(), running.end(), [&](const Car* a, const Car* b) {
        bool ma = a->owner == s_ride, mb = b->owner == s_ride;
        if (ma != mb) return ma;
        return dist(a->pos, ear) < dist(b->pos, ear);
    });
    float half = std::max(120.0f, R::viewW() * 0.5f);
    int voice = 0;
    for (const Car* c : running) {
        if (voice + 1 >= Audio::LOOP_VOICES) break;
        const CarModel& m = carModel(c->model);
        bool inside = c->owner == s_ride;
        float d = dist(c->pos, ear);
        float att = inside ? 1.0f : clampf(1.0f - d / 600.0f, 0, 1);
        att *= att;
        float pan = inside ? 0.0f : clampf((c->pos.x - ear.x) / half, -1, 1) * 0.9f;
        float sp = clampf(c->speed() / m.topSpeed, 0, 1.2f);
        // A heavier car growls lower. Idle is a slow drone; speed and the pedal rev it.
        float base = 0.17f / std::sqrt(m.mass);
        float pitch = base * (1.0f + 1.3f * sp + 0.35f * c->throttle);
        float gain = (inside ? 0.45f : 0.55f) * att * (0.55f + 0.3f * sp + 0.25f * c->throttle);
        Audio::setLoop(voice++, Audio::LOOP_ENGINE, gain, pitch, pan);
        // Tyres squealing: sliding sideways on the hard ground, or braking hard.
        float squeal = clampf((c->slip - 35.0f) / 90.0f, 0, 1);
        if (squeal > 0.02f && voice < Audio::LOOP_VOICES)
            Audio::setLoop(voice++, Audio::LOOP_SCREECH, squeal * 0.7f * att, 1.05f + 0.2f * sp, pan);
    }
}

// Every car: remote copies ease toward their owners' word; smoke from a damaged one.
void updateCars(float dt) {
    carSounds();
    for (Car& c : s_cars) {
        if (c.owner != mySlot()) {
            c.netT += dt;
            float k = std::min(1.0f, dt * 12.0f);
            Vec2 predicted = c.netPos + c.vel * std::min(c.netT, 0.25f);
            if (dist(c.pos, predicted) > 80) c.pos = predicted;
            else c.pos = c.pos + (predicted - c.pos) * k;
            c.angle += angleDiff(c.angle, c.netAngle) * k;
            c.hurtT -= dt;
        }
        const CarModel& m = carModel(c.model);
        c.smokeT -= dt;
        float frac = c.hp / m.hp;
        if (!c.wrecked && frac < 0.45f && c.smokeT <= 0 && onScreen(c.pos, -40)) {
            c.smokeT = frac < 0.2f ? 0.05f : 0.14f;
            Vec2 front = c.pos + fromAngle(c.angle) * (m.halfLen * 0.7f);
            Particle p;
            p.pos = front + Vec2(s_rng.range(-3, 3), -4);
            p.vel = Vec2(s_rng.range(-6, 6), s_rng.range(-22, -10));
            p.life = p.maxLife = s_rng.range(0.8f, 1.6f);
            p.color = frac < 0.2f ? P_DARK : P_LAVENDER;
            p.size = 2;
            p.drag = 1;
            G.particles.push_back(p);
            if (frac < 0.2f && s_rng.chance(0.4f)) addParticles(front, 1, P_ORANGE, 5, 20, 0.2f, 0.4f, true, 2);
        }
        if (c.wrecked && c.smokeT <= 0 && onScreen(c.pos, -40)) {
            c.smokeT = 0.2f;
            addParticles(c.pos + Vec2(s_rng.range(-8, 8), -6), 1, P_DARK, 4, 16, 1.2f, 2.4f, false, 2);
        }
    }
    if (isGuest())
        s_cars.erase(std::remove_if(s_cars.begin(), s_cars.end(), [](const Car& c) { return c.owner != mySlot() && c.netT > 3.0f; }), s_cars.end());
    // I am in someone else's car: I go where it goes. If it is gone (wrecked, towed
    // away, its owner left), I am out.
    if (s_ride >= 0) {
        Car* c = carOf(s_ride);
        if (!c || c->wrecked) leaveCar(true);
        else {
            G.player.pos = c->pos;
            if (driving()) G.player.angle = c->angle;
        }
    }
}

// ---- the mechanic -------------------------------------------------------------------
// He comes out to meet you the first time you step outside on day 5, and says his
// piece; after that he is always at his yard.
const char* MECH_TALK[] = {
    "Hey! You there, from the hatch! Hold up a second.",
    "Name's Rusty. I'm the mechanic around here - if it has wheels and still wants to run, I can make it run.",
    "My yard's just east of your bunker. Out the gate, turn right, you can't miss it. You can always find me there.",
    "Here's the deal: you help me, I help you. Bring me money and the parts you find out there - scrap, bolts, wires, batteries, circuit boards - and I'll get you a car.",
    "The first one's cheap: two grand for a little Micro. The better ones cost more and I'll want more parts for them, but they seat more of your people, take more punishment, go further on a tank and faster.",
    "And believe me, you're going to need one. The roads out there go a lot further now, all the way to the cities. Too far to walk there and back before dark.",
    "The cities are crawling with gangs, but that's where the good stuff is. High risk, better loot. Come see me when you're ready.",
};
constexpr int MECH_PAGES = sizeof(MECH_TALK) / sizeof(MECH_TALK[0]);

void resetMechanic() {
    s_mech = MECH_IDLE;
    s_mechPos = s_mechLast = mechanicHome();
    s_mechAngle = PI / 2;
    s_talkPage = 0;
    if (mechanicOut() && !G.prof.mechanicMet) s_mech = MECH_COMING;
}

void finishTalk() {
    G.panel = Panel::None;
    G.prof.mechanicMet = true;
    s_mech = MECH_GOING;
    pushMessage(T("The mechanic's yard is just east of the bunker. Press E beside him to see his cars."), P_YELLOW);
    if (!isGuest()) save_game();
    else Coop::sendProfile();
}

void updateMechanic(float dt) {
    if (!mechanicOut()) return;
    s_mechLast = s_mechPos;
    Vec2 home = mechanicHome();
    static float stuckT = 0, sideT = 0;
    static Vec2 sideDir;
    auto walk = [&](Vec2 to, float speed) {
        Vec2 d = to - s_mechPos;
        if (length(d) < 2) return;
        Vec2 dir = normalize(d);
        // Round walls the way the raiders go (the path field leads to you), and a
        // sidestep when he still bumps into something.
        Vec2 flow;
        if (!G.world.lineOfSight(s_mechPos, to) && dist(to, G.player.pos) < 8 && G.world.flowDir(s_mechPos, flow)) dir = flow;
        if (sideT > 0) { sideT -= dt; dir = normalize(dir + sideDir * 1.5f); }
        s_mechAngle = angleOf(dir);
        Vec2 before = s_mechPos;
        s_mechPos = G.world.move(s_mechPos, dir * std::min(speed * dt, length(d)), PLAYER_R);
        if (dist(before, s_mechPos) < speed * dt * 0.25f) {
            if ((stuckT += dt) > 0.4f) { stuckT = 0; sideT = 0.7f; sideDir = Vec2(-dir.y, dir.x) * (s_rng.chance(0.5f) ? 1.0f : -1.0f); }
        } else stuckT = 0;
    };
    switch (s_mech) {
    case MECH_COMING: {
        // Only on the surface: he waits if you went below or upstairs.
        if (!sameArea(G.player.pos, s_mechPos) || G.world.areaAt(G.player.pos) != -1) { walk(home, 60); break; }
        Vec2 at = G.player.pos;
        if (Car* c = carOf(s_ride)) at = c->pos;
        float d = dist(at, s_mechPos);
        if (d > 26 + (s_ride >= 0 ? 16 : 0)) walk(at, 88);
        else if (G.panel == Panel::None && s_deathT < 0 && s_downT < 0 && (s_ride < 0 || myCar() == nullptr || myCar()->speed() < 30)) {
            s_mech = MECH_TALKING;
            s_talkPage = 0;
            s_talkT = 0;
            closeLoot();
            G.panel = Panel::MechanicTalk;
            sfx(Snd::click, 0.6f, 0.8f);
        }
        break;
    }
    case MECH_TALKING:
        s_mechAngle = angleOf(G.player.pos - s_mechPos);
        if (G.panel != Panel::MechanicTalk) G.panel = Panel::MechanicTalk;   // nothing else until he is done
        break;
    case MECH_GOING:
        walk(home, 60);
        if (dist(s_mechPos, home) < 3) s_mech = MECH_IDLE;
        break;
    default: {
        if (dist(s_mechPos, home) > 3) walk(home, 50);
        else if (dist(G.player.pos, s_mechPos) < 90) s_mechAngle = angleOf(G.player.pos - s_mechPos);
        else s_mechAngle = PI / 2;
        break;
    }
    }
}

// ---- the mechanic's shop --------------------------------------------------------------
int s_shopSel = 0, s_shopColor = 0;
int pocketCount(int item) { return countInSlots(G.prof.inv, item, G.prof.invCapacity()); }
bool haveMats(const CarModel& m) {
    for (const CarMat& mt : m.mats)
        if (mt.item != IT_NONE && pocketCount(mt.item) < mt.count) return false;
    return true;
}
bool carNearYard() {
    Car* c = myCar();
    return c && dist(c->pos, mechanicHome()) < 14 * TILE;
}

void shopBuy(int model, int color) {
    Profile& p = G.prof;
    const CarModel& m = carModel(model);
    if (p.cars[model].owned || p.money < m.price || !haveMats(m)) return;
    if (s_ride >= 0) leaveCar(true);
    p.money -= m.price;
    for (const CarMat& mt : m.mats)
        if (mt.item != IT_NONE) takeFromSlots(p.inv, mt.item, mt.count, p.invCapacity());
    persistMyCar();
    p.cars[model] = Profile::OwnedCar{true, color, -1, -1};
    p.activeCar = model;
    p.carDay = 0;   // it waits in its bay
    spawnMyCar();
    bigText(T1("NEW CAR: {0}", T(m.name)), P_YGREEN, 3);
    pushMessage(T("It is waiting in the bay in front of the workshop. E beside it to get in."), P_YGREEN);
    sfx(Snd::sell, 0.8f, 0.8f);
    sfx(Snd::car_door, 0.8f);
    if (!isGuest()) save_game(); else Coop::sendProfile();
}

void shopTakeOut(int model) {
    Profile& p = G.prof;
    if (!p.cars[model].owned || p.activeCar == model) return;
    if (s_ride >= 0) leaveCar(true);
    persistMyCar();                       // the old one goes back into his yard as it is
    p.activeCar = model;
    p.carDay = 0;
    spawnMyCar();
    pushMessage(T1("The {0} is in the bay.", T(carModel(model).name)), P_YGREEN);
    sfx(Snd::car_door, 0.8f);
    if (!isGuest()) save_game(); else Coop::sendProfile();
}


// City gangs on patrol (0.11v): each gang follows a point walking its route round the
// blocks, in a loose bunch round it. The point waits while any of them is fighting.
struct PatrolRun { size_t next = 1; Vec2 anchor; float hold = 0; };
std::vector<PatrolRun> s_patrols;
void resetPatrols() {
    s_patrols.clear();
    for (const PatrolRoute& r : G.world.patrols) {
        PatrolRun pr;
        pr.anchor = r.points.empty() ? Vec2() : r.points[0];
        pr.next = r.points.size() > 1 ? 1 : 0;
        s_patrols.push_back(pr);
    }
}
void updatePatrols(float dt) {
    for (size_t i = 0; i < s_patrols.size() && i < G.world.patrols.size(); i++) {
        PatrolRun& pr = s_patrols[i];
        const std::vector<Vec2>& pts = G.world.patrols[i].points;
        if (pts.size() < 2) continue;
        if (pr.hold > 0) { pr.hold -= dt; continue; }
        Vec2 d = pts[pr.next % pts.size()] - pr.anchor;
        float l = length(d);
        if (l < 4) { pr.next = (pr.next + 1) % pts.size(); pr.hold = 1.5f; continue; }
        pr.anchor += d / l * std::min(l, 26.0f * dt);
    }
}

void updateEnemy(size_t index, float dt) {
    Enemy& e = G.enemies[index];
    const EnemyDef& def = ENEMY_DEFS[(int)e.type];
    Player& pl = G.player;
    // The nearest player is the one they care about.
    int ti = nearestTarget(e.pos);
    bool playerAlive = ti >= 0;
    Vec2 ppos = playerAlive ? s_targets[ti].pos : e.pos;
    int pslot = playerAlive ? s_targets[ti].slot : -1;
    Vec2 toP = ppos - e.pos;
    float d = playerAlive ? length(toP) : 1e9f;
    e.hurtT -= dt;
    e.flashT -= dt;
    e.fireCd -= dt;
    e.reactT -= dt;
    e.meleeCd -= dt;

    // A reload keeps ticking whatever the enemy is doing, so one started in cover
    // still finishes if the fight moves on. It is also the only time an enemy is
    // not shooting at you, so it is worth being able to see and plan around.
    const WeaponDef* weapon = weaponDef(e.weapon);
    if (weapon && e.reloadT > 0) {
        e.reloadT -= dt;
        if (e.reloadT <= 0) {
            e.mag = weapon->magSize;
            sfxAt(Snd::reload_end, e.pos, pl.pos, 0.35f);
        }
    }

    if (e.type == EnemyType::Zombie) {
        updateZombie(index, dt);
        return;
    }
    if (e.type == EnemyType::Shade) {
        Vec2 dir = playerAlive ? normalize(toP) : fromAngle(e.angle);
        Vec2 vel = (dir + separation(e, index) * 0.6f) * def.speed * e.speedMul;
        e.pos = G.world.move(e.pos, vel * dt, ENEMY_R, true);
        e.angle = angleOf(dir);
        if (playerAlive && d < 11 && e.meleeCd <= 0) {
            e.meleeCd = 0.55f;
            if (pslot == mySlot()) sfx(Snd::melee, 0.6f);
            hurtSlot(pslot, 12 * dayThreat(), "Taken by the night.");
        }
        return;
    }

    if (d > 900 && e.state == AIState::Idle) return;  // dormant far away

    // Raiders shoot at whoever they can see first: you, or one of your people.
    e.losT -= dt;
    e.provokedT -= dt;
    if (e.losT <= 0) {
        e.losT = 0.2f + s_rng.f() * 0.1f;
        // Shot at from out of its sight (a merc's longer reach): for a while it looks
        // as far as it has to, to find who it was and shoot back.
        float sight = e.provokedT > 0 ? std::max(def.sight, 440.0f) : def.sight;
        e.canSee = playerAlive && d < sight && G.world.lineOfSight(e.pos, ppos);
        e.ztarget = -2;
        e.tslot = pslot;
        float best = e.canSee ? d : sight;
        for (int i = 0; i < (int)s_mercs.size(); i++) {
            const Hireling& h = *s_mercs[i];
            if (h.dead || !sameArea(h.pos, e.pos)) continue;
            float dh = dist(e.pos, h.pos);
            // Whoever just shot it comes first.
            if (e.provokedT > 0 && dist(h.pos, e.lastSeen) < 40) dh *= 0.5f;
            if (dh < best && G.world.lineOfSight(e.pos, h.pos)) { best = dh; e.ztarget = i; e.canSee = true; }
        }
    }
    Vec2 tpos = ppos;
    if (e.ztarget >= 0 && e.ztarget < (int)s_mercs.size() && !s_mercs[e.ztarget]->dead) tpos = s_mercs[e.ztarget]->pos;
    toP = tpos - e.pos;
    d = length(toP);
    if (e.canSee) {
        if (e.state != AIState::Combat) {
            e.reactT = e.type == EnemyType::Sniper ? 0.9f : 0.5f;
            // One of a patrol spots someone: the whole gang turns.
            if (e.patrol >= 0)
                for (Enemy& o : G.enemies)
                    if (&o != &e && !o.dead && o.patrol == e.patrol && o.state == AIState::Idle) { o.state = AIState::Alert; o.lastSeen = tpos; o.alertT = 6; }
        }
        e.state = AIState::Combat;
        e.lastSeen = tpos;
        e.alertT = 6;
    }

    Vec2 move;
    float speed = def.speed;
    if (e.patrol >= 0 && e.patrol < (int)s_patrols.size() && e.state != AIState::Idle) s_patrols[e.patrol].hold = std::max(s_patrols[e.patrol].hold, 3.0f);
    switch (e.state) {
    case AIState::Idle: {
        if (e.patrol >= 0 && e.patrol < (int)s_patrols.size()) {
            // Walking the beat with the rest of the gang.
            PatrolRun& pr = s_patrols[e.patrol];
            Vec2 slot = pr.anchor + fromAngle(e.netId * 2.39996f) * (7.0f + (e.netId % 3) * 6.0f);
            Vec2 tw = slot - e.pos;
            float l = length(tw);
            if (l > 50) pr.hold = std::max(pr.hold, 0.3f);   // wait for the stragglers
            if (l > 5) {
                move = normalize(tw);
                speed *= l > 30 ? 0.8f : 0.5f;
                turnToward(e.angle, angleOf(tw), 5, dt);
            }
            break;
        }
        e.wanderT -= dt;
        if (e.wanderT <= 0) {
            e.wanderT = s_rng.range(2, 5);
            Vec2 t = e.home + Vec2(s_rng.range(-64, 64), s_rng.range(-64, 64));
            if (!G.world.collides(t.x, t.y, ENEMY_R) && s_rng.chance(0.6f)) e.wanderTarget = t;
            else e.wanderTarget = e.pos;
        }
        Vec2 tw = e.wanderTarget - e.pos;
        if (length(tw) > 4) {
            move = normalize(tw);
            speed *= 0.45f;
            turnToward(e.angle, angleOf(tw), 4, dt);
        }
        break;
    }
    case AIState::Alert: {
        Vec2 tl = e.lastSeen - e.pos;
        if (length(tl) > 10) {
            move = pathDir(e, e.lastSeen);
            speed *= 0.8f;
            turnToward(e.angle, angleOf(move), 5, dt);
        } else {
            e.angle += dt * 1.5f;
            e.alertT -= dt;
        }
        e.alertT -= dt * 0.3f;
        if (e.alertT <= 0) {
            e.state = AIState::Idle;
            e.home = e.pos;
            e.wanderTarget = e.pos;
        }
        break;
    }
    case AIState::Combat: {
        const WeaponDef* wd = weaponDef(e.weapon);
        // An empty magazine is reloaded whether or not they can still see you: the
        // pause costs the same either way, and it means a burst cannot leave a gun dry
        // for good just because the fight walked away from it.
        if (wd && e.reloadT <= 0 && e.mag <= 0) e.reloadT = wd->reloadTime * 1.3f;
        // While the magazine is out they plant their feet instead of walking you down.
        bool reloading = wd && e.reloadT > 0;
        if (e.canSee) {
            turnToward(e.angle, angleOf(toP), 7, dt);
            if (!reloading) {
                if (d > def.prefRange) move = pathDir(e, tpos);
                else if (d < def.prefRange * 0.45f) move = normalize(e.pos - tpos);
                e.strafeT -= dt;
                if (e.strafeT <= 0) {
                    e.strafeT = s_rng.range(0.6f, 1.6f);
                    e.strafeDir = s_rng.chance(0.35f) ? 0.0f : (s_rng.chance(0.5f) ? 1.0f : -1.0f);
                }
                Vec2 perp = normalize(Vec2(-toP.y, toP.x));
                move = move + perp * e.strafeDir * 0.7f;
                if (e.type == EnemyType::Sniper) move *= 0.3f;
            }
            // Only someone who could be on that player's screen shoots at them. A merc it
            // is fighting counts as its own viewer (0.11v fix: raiders would not fire
            // back at mercs away from the player's screen).
            Vec2 viewer = playerAlive && e.ztarget < 0 ? ppos : tpos;
            bool inView = std::fabs(e.pos.x - viewer.x) < R::viewW() * 0.5f - 10 && std::fabs(e.pos.y - viewer.y) < R::viewH() * 0.5f - 10;
            if (wd && !reloading &&
                e.reactT <= 0 && e.fireCd <= 0 && d < wd->range * 0.95f &&
                inView &&
                std::fabs(angleDiff(e.angle, angleOf(toP))) < 0.3f) {
                Vec2 muzzle = e.pos + fromAngle(e.angle) * 8;
                s_shotOwner = -1;
                spawnBullets(muzzle, e.angle, *wd, e.weapon, 1.0f, def.spread, false, ENEMY_DAMAGE_MUL * dayThreat());
                e.mag--;
                e.flashT = 0.09f;
                e.fireCd = 1.0f / (wd->fireRate * def.fireMul) * s_rng.range(0.7f, 1.4f);
                sfxAt(wd->sound, e.pos, pl.pos, 0.6f, s_rng.range(0.85f, 1.0f));
            }
        } else {
            e.alertT -= dt;
            if (length(e.lastSeen - e.pos) > 10 || d < 400) {
                move = pathDir(e, playerAlive ? ppos : e.lastSeen);
                turnToward(e.angle, angleOf(move), 5, dt);
            }
            if (e.alertT <= 0) {
                e.state = AIState::Alert;
                e.alertT = 4;
            }
        }
        break;
    }
    }

    if (e.unstickT > 0) {
        e.unstickT -= dt;
        move = e.unstickDir;
    }
    if (lengthSq(move) > 0.0001f) {
        // Doors participate in the flow field, then nearby NPCs operate them before
        // collision is resolved. This lets pursuit and wandering cross buildings.
        if (G.world.openDoorNear(e.pos + normalize(move) * 8.0f, 18.0f))
            sfxAt(Snd::door, e.pos, pl.pos, 0.55f);
        Vec2 before = e.pos;
        Vec2 vel = normalize(move) * speed + separation(e, index) * 25.0f;
        e.pos = G.world.move(e.pos, vel * dt, ENEMY_R);
        if (dist(before, e.pos) < speed * dt * 0.25f) {
            e.stuckT += dt;
            if (e.stuckT > 0.6f) {
                e.unstickDir = fromAngle(s_rng.range(0, 2 * PI));
                e.unstickT = 0.5f;
                e.stuckT = 0;
            }
        } else {
            e.stuckT = std::max(0.0f, e.stuckT - dt);
        }
    }
}

void updateBullets(float dt) {
    for (auto& b : G.bullets) {
        if (b.rangeLeft <= 0) continue;
        bool exploded = false;
        float travel = length(b.vel) * dt;
        int steps = std::max(1, (int)std::ceil(travel / 3.0f));
        Vec2 stepV = b.vel * (dt / steps);
        float stepLen = travel / steps;
        for (int s = 0; s < steps && b.rangeLeft > 0; s++) {
            b.pos += stepV;
            b.rangeLeft -= stepLen;
            int tx = World::toTile(b.pos.x), ty = World::toTile(b.pos.y);
            if (G.world.blocksBulletAt(b.pos)) {
                if (b.explosive) {
                    if (!b.cosmetic) { s_dmgOwner = b.owner; explode(b.pos - stepV, b.turret ? 40 : 52, b.damage, b.fromPlayer, b.turret); s_dmgOwner = -1; }
                } else if (b.turret || b.noTiles) {
                    addParticles(b.pos - stepV, 2, P_BEIGE, 10, 40, 0.1f, 0.25f);
                } else if (G.world.inBounds(tx, ty)) {
                    int col = solidInfo(G.world.at(tx, ty).solid).mapColor;
                    bool destroyed = G.world.damageTile(tx, ty, b.damage * b.tileMul);
                    addParticles(b.pos - stepV, destroyed ? 10 : 3, col, 15, destroyed ? 90 : 50, 0.2f, destroyed ? 0.8f : 0.35f, false, destroyed ? 2 : 1);
                    if (destroyed) sfxAt(Snd::tile_break, b.pos, G.player.pos, 0.7f, s_rng.range(0.9f, 1.1f));
                    else sfxAt(Snd::tile_hit, b.pos, G.player.pos, 0.3f, s_rng.range(0.8f, 1.3f));
                }
                b.rangeLeft = 0;
                break;
            }
            if (b.fromPlayer && rivalsOn() && b.owner >= 0 && b.owner < Coop::MAX_PLAYERS) {
                // Rivals: bullets find people too. The shooter's game decides the hit;
                // anyone else's copy of the shot just stops on the body.
                bool stopped = false;
                for (int i = 0; i < Coop::MAX_PLAYERS && !stopped; i++) {
                    if (i == b.owner) continue;
                    Vec2 pp;
                    if (i == mySlot()) { if (!localPresent()) continue; pp = G.player.pos; }
                    else {
                        const Coop::NetPlayer& np = Coop::player(i);
                        if (!np.used || np.where != Coop::W_RAID || np.downed) continue;
                        pp = np.pos;
                    }
                    if (lengthSq(pp - b.pos) >= 5.5f * 5.5f) continue;
                    if (!b.cosmetic && b.owner == mySlot() && i != mySlot()) pvpHit(i, b.damage * 0.8f);
                    bloodSplash(pp, normalize(b.vel));
                    b.rangeLeft = 0;
                    stopped = true;
                }
                if (stopped) break;
            }
            if (b.fromPlayer) {
                for (size_t i = 0; i < G.enemies.size(); i++) {
                    Enemy& e = G.enemies[i];
                    if (e.dead || lengthSq(e.pos - b.pos) > 8.0f * 8.0f) continue;
                    if (std::find(b.hitList.begin(), b.hitList.end(), (int)i) != b.hitList.end()) continue;
                    if (b.explosive) {
                        if (!b.cosmetic) { s_dmgOwner = b.owner; explode(b.pos, b.turret ? 40 : 52, b.damage, true, b.turret); s_dmgOwner = -1; }
                        exploded = true;
                        b.rangeLeft = 0;
                        break;
                    }
                    if (b.cosmetic) {
                        // Someone else's shot: the host decides what it did.
                        if (e.type == EnemyType::Shade) addParticles(b.pos, 2, P_PURPLE, 20, 60, 0.1f, 0.3f);
                        else bloodSplash(e.pos, normalize(b.vel));
                    } else if (b.reportHits) {
                        // My shot, on a guest: show the hit now and let the host apply it.
                        Net::Writer w;
                        w.u8(Coop::M_HIT);
                        w.u32(e.netId);
                        w.f32(b.damage);
                        Vec2 dir = normalize(b.vel);
                        w.f32(dir.x);
                        w.f32(dir.y);
                        Coop::toHost(w, true);
                        e.hurtT = 0.12f;
                        if (e.type == EnemyType::Shade) addParticles(e.pos + dir * 2, 4, P_PURPLE, 20, 70, 0.15f, 0.4f);
                        else bloodSplash(e.pos, dir);
                        sfxAt(Snd::hit, e.pos, G.player.pos, 0.5f, s_rng.range(0.9f, 1.2f));
                        G.floatTexts.push_back({e.pos + Vec2(0, -8), std::to_string((int)std::ceil(b.damage)), P_WHITE, 0.6f});
                    } else {
                        s_dmgOwner = b.owner;
                        s_dmgFrom = b.origin;
                        damageEnemy(e, b.damage, normalize(b.vel));
                        s_dmgFrom = Vec2();
                        s_dmgOwner = -1;
                    }
                    b.hitList.push_back((int)i);
                    if (!b.pierce) { b.rangeLeft = 0; break; }
                }
            } else if (b.cosmetic) {
                // A copy of a hostile's shot: it stops on me, but the host does the hurting.
                if (localPresent() && lengthSq(G.player.pos - b.pos) < 5.5f * 5.5f) b.rangeLeft = 0;
                for (const Car& c : s_cars)
                    if (!c.wrecked && carContains(c, b.pos)) { b.rangeLeft = 0; addParticles(b.pos, 2, P_BEIGE, 10, 40, 0.1f, 0.25f); break; }
            } else {
                bool hit = false;
                // A car takes the bullets meant for whoever is in it (0.11v).
                for (Car& c : s_cars) {
                    if (c.wrecked || !carContains(c, b.pos)) continue;
                    damageCar(c, b.damage * 0.5f);   // steel and glass take the edge off
                    addParticles(b.pos - normalize(b.vel) * 2.0f, 3, P_BEIGE, 15, 50, 0.1f, 0.3f);
                    sfxAt(s_rng.chance(0.3f) ? Snd::car_glass : Snd::car_hit, b.pos, G.player.pos, 0.35f, s_rng.range(1.0f, 1.3f));
                    b.rangeLeft = 0;
                    hit = true;
                    break;
                }
                if (hit) break;
                for (const Target& t : s_targets) {
                    if (lengthSq(t.pos - b.pos) >= 5.5f * 5.5f) continue;
                    hurtSlot(t.slot, b.damage, "Shot by a hostile.");
                    b.rangeLeft = 0;
                    hit = true;
                    break;
                }
                if (!hit)
                    for (Hireling* h : s_mercs) {
                        if (h->dead || lengthSq(h->pos - b.pos) > 5.5f * 5.5f) continue;
                        damageHireling(*h, b.damage);
                        b.rangeLeft = 0;
                        break;
                    }
            }
            if (b.rangeLeft <= 0 && b.explosive && !exploded && !b.cosmetic) {
                s_dmgOwner = b.owner;
                explode(b.pos, b.turret ? 40 : 52, b.damage, b.fromPlayer, b.turret);
                s_dmgOwner = -1;
            }
        }
    }
    G.bullets.erase(std::remove_if(G.bullets.begin(), G.bullets.end(), [](const Bullet& b) { return b.rangeLeft <= 0; }), G.bullets.end());
}

// A thrown grenade sails over what is low (cars, fences, sandbags, crates, furniture,
// turrets); only walls, doors, the rock below and tree trunks bounce it back.
bool grenadeBlocked(Vec2 p) {
    int tx = World::toTile(p.x), ty = World::toTile(p.y);
    if (!G.world.inBounds(tx, ty)) return true;
    switch (G.world.at(tx, ty).solid) {
    case S_WALL_BRICK: case S_WALL_CONCRETE: case S_WALL_WOOD: case S_DOOR: case S_BUNKER: case S_BOUNDARY:
    case S_CRYPT_WALL: case S_CRYPT_PROP: case S_CRYPT_GATE:
        return true;
    case S_TREE: return G.world.blocksBulletAt(p);
    default: return false;
    }
}

void updateGrenades(float dt) {
    for (auto& g : G.grenades) {
        g.fuse -= dt;
        Vec2 np = g.pos + Vec2(g.vel.x * dt, 0);
        if (grenadeBlocked(np)) g.vel.x *= -0.5f;
        else g.pos.x = np.x;
        np = g.pos + Vec2(0, g.vel.y * dt);
        if (grenadeBlocked(np)) g.vel.y *= -0.5f;
        else g.pos.y = np.y;
        g.vel *= std::pow(0.08f, dt);
        // A copy of someone else's grenade just goes away: the host's explosion follows.
        if (g.fuse <= 0 && !g.cosmetic) { s_dmgOwner = g.owner; explode(g.pos, 44, 120, true); s_dmgOwner = -1; }
    }
    G.grenades.erase(std::remove_if(G.grenades.begin(), G.grenades.end(), [](const Grenade& g) { return g.fuse <= 0; }), G.grenades.end());
}

void updateEffects(float dt) {
    updateBloodDrops(dt);
    updateCasings(dt);
    for (BloodFx& b : s_bloodFx) b.t += dt;
    s_bloodFx.erase(std::remove_if(s_bloodFx.begin(), s_bloodFx.end(), [](const BloodFx& b) { return b.t > 0.3f; }), s_bloodFx.end());
    // Teammates getting hurt bleed on everyone's screen too.
    if (Coop::active()) {
        static bool wasHurt[Coop::MAX_PLAYERS] = {};
        for (int i = 0; i < Coop::MAX_PLAYERS; i++) {
            const Coop::NetPlayer& np = Coop::player(i);
            bool hurt = i != mySlot() && np.used && np.where == Coop::W_RAID && np.hurtT > 0;
            if (hurt && !wasHurt[i]) bloodSplash(np.pos, randomDir());
            wasHurt[i] = hurt;
        }
    }
    for (auto& p : G.particles) {
        p.pos += p.vel * dt;
        p.vel *= std::pow(0.5f, dt * p.drag);
        p.life -= dt;
    }
    G.particles.erase(std::remove_if(G.particles.begin(), G.particles.end(), [](const Particle& p) { return p.life <= 0; }), G.particles.end());
    if (G.particles.size() > 3000) G.particles.erase(G.particles.begin(), G.particles.begin() + (G.particles.size() - 3000));
    for (auto& f : G.flashes) f.life -= dt;
    G.flashes.erase(std::remove_if(G.flashes.begin(), G.flashes.end(), [](const Flash& f) { return f.life <= 0; }), G.flashes.end());
    for (auto& t : G.floatTexts) { t.life -= dt; t.pos.y -= 20 * dt; }
    G.floatTexts.erase(std::remove_if(G.floatTexts.begin(), G.floatTexts.end(), [](const FloatText& t) { return t.life <= 0; }), G.floatTexts.end());
    for (auto& m : G.messages) m.life -= dt;
    G.messages.erase(std::remove_if(G.messages.begin(), G.messages.end(), [](const Message& m) { return m.life <= 0; }), G.messages.end());
    if (G.decals.size() > 400) G.decals.erase(G.decals.begin(), G.decals.begin() + 100);
    G.shake = std::max(0.0f, G.shake - dt * 18);
    s_bigT -= dt;
    for (auto& c : s_zCorpses) c.t += dt;
    s_zCorpses.erase(std::remove_if(s_zCorpses.begin(), s_zCorpses.end(), [](const ZombieCorpse& c) { return c.t > 30; }), s_zCorpses.end());
}

// You went down the hatch (or died) with a horde still coming: the fight goes on
// without you, run through the same code at a coarse step until it is decided.
void resolveHordeOffscreen() {
    if (!s_hordeActive) return;
    s_sim = true;
    buildTargets();
    rebuildMercs();
    const float dt = 0.1f;
    for (float t = 0; t < 420 && s_hordeActive; t += dt) {
        updateHordeSpawning(dt);
        for (size_t i = 0; i < G.enemies.size(); i++) {
            Enemy& e = G.enemies[i];
            if (e.dead || e.type != EnemyType::Zombie || e.roamer) continue;
            e.lastPos = e.pos;
            e.meleeCd -= dt;
            e.hurtT -= dt;
            updateZombie(i, dt);
        }
        G.enemies.erase(std::remove_if(G.enemies.begin(), G.enemies.end(), [](const Enemy& e) { return e.dead; }), G.enemies.end());
        crowdZombies();
        updateTurrets(dt);
        updateSquad(dt);
        updateBullets(dt);
        buryHirelings();
    }
    // Whatever is still out there after that has lost interest and wandered off.
    if (s_hordeActive) endHorde(true);
    s_sim = false;
    G.bullets.clear();
}

}  // namespace

// ---------------------------------------------------------------- public
void raid_dropItem(const Item& it) {
    if (it.empty()) return;
    if (Coop::guest()) {
        Net::Writer w;
        w.u8(Coop::M_DROP);
        w.f32(G.player.pos.x); w.f32(G.player.pos.y);
        w.u8(CK_BAG);
        w.u8(1);
        w.i16(it.id); w.i16(it.count); w.i32(it.data); w.u8((uint8_t)it.tier); w.u8(it.flags);
        Coop::toHost(w, true);
        pushMessage(T1("Dropped {0}", itemLabel(it)), P_BEIGE);
        return;
    }
    for (auto& c : G.world.containers) {
        if (c.removed || c.tx >= 0 || dist(c.pos, G.player.pos) > 12) continue;
        if (addToSlots(c.items, it) == 0) { pushMessage(T1("Dropped {0}", itemLabel(it)), P_BEIGE); return; }
    }
    int id = G.world.addContainer(G.player.pos, CK_BAG, -1, -1, (uint8_t)s_rng.next());
    G.world.containers[id].searched = true;
    addToSlots(G.world.containers[id].items, it);
    pushMessage(T1("Dropped {0}", itemLabel(it)), P_BEIGE);
}

// Dev (--bot): fire the equipped gun at the nearest enemy in range, through the
// normal shooting path, so co-op hits and kill credit can be watched.
void raid_devBotShoot() {
    Profile& p = G.prof;
    Item& w = p.weapons[p.curWeapon];
    const WeaponDef* wd = weaponDef(w.id);
    if (!wd || G.player.fireCd > 0) return;
    const Enemy* best = nullptr;
    float bd = 220;
    for (const Enemy& e : G.enemies)
        if (!e.dead && dist(e.pos, G.player.pos) < bd) { bd = dist(e.pos, G.player.pos); best = &e; }
    if (!best) return;
    G.player.angle = angleOf(best->pos - G.player.pos);
    if (bd < 24) { melee(); return; }   // close enough to hit
    G.player.act = 1;
    G.player.actT = 0;
    if (w.data <= 0) w.data = wd->magSize;
    s_shotOwner = mySlot();
    spawnBullets(G.player.pos + fromAngle(G.player.angle) * 9, G.player.angle, *wd, w.id, 0.5f, 0, true, 1.0f);
    s_shotOwner = -1;
    w.data--;
    G.player.fireCd = 1.0f / wd->fireRate;
    G.player.flashT = 0.09f;
    G.player.act = 1;
    G.player.actT = 0;
}

// Dev: a few zombies of each kind at the edge of the screen, walking in.
void raid_devZombies(int count) {
    s_hordeN = 1;
    for (int i = 0; i < count; i++) {
        // A ring of them coming in from every side, not just a line.
        Vec2 at = G.player.pos + fromAngle(-0.6f + i * 0.35f + (i / 18) * 0.17f) * ((G.devClean ? 230.0f : 140.0f) + (i % 3) * 12.0f + (i / 18) * 40.0f);
        if (G.world.collides(at.x, at.y, ENEMY_R)) continue;
        spawnZombie(at, i % 3);
    }
}

void raid_devSpawn(int count) {
    for (int i = 0; i < count; i++) {
        Vec2 p = G.player.pos + fromAngle(i * 1.1f) * (40.0f + i * 9.0f);
        EnemyType t = (EnemyType)(i % 4);
        spawnEnemy(t, p);
    }
}

// Number of containers the generator itself made; anything past this was created
// during the raid (corpses, dropped bags).
static size_t s_generatedContainers = 0;

void captureDayMemory() {
    DayMemory& m = G.prof.dayMem;
    m.clear();
    m.day = G.prof.day;
    World& w = G.world;

    m.explored.assign((w.tiles.size() + 7) / 8, 0);
    for (size_t i = 0; i < w.tiles.size(); i++)
        if (w.tiles[i].explored) m.explored[i / 8] |= (uint8_t)(1u << (i % 8));

    // Dead enemies are erased from the list as the raid goes, so a spawn counts as
    // killed unless someone still standing came from it.
    m.spawnDead.assign(w.spawns.size(), 1);
    for (const Enemy& e : G.enemies)
        if (!e.dead && e.spawnIdx >= 0 && e.spawnIdx < (int)w.spawns.size()) m.spawnDead[e.spawnIdx] = 0;

    for (size_t i = 0; i < w.containers.size(); i++) {
        const Container& c = w.containers[i];
        if (i < s_generatedContainers) {
            if (!c.searched && !c.removed) continue;
            m.openedIdx.push_back((int)i);
            m.openedItems.push_back(c.items);
        } else if (!c.removed) {
            m.dropped.push_back(c);
        }
    }
}

void applyDayMemory() {
    DayMemory& m = G.prof.dayMem;
    World& w = G.world;
    if (m.day != G.prof.day) {
        m.clear();
        return;
    }

    if (m.explored.size() == (w.tiles.size() + 7) / 8) {
        for (size_t i = 0; i < w.tiles.size(); i++)
            if (m.explored[i / 8] & (1u << (i % 8))) w.tiles[i].explored = 1;
        w.rebuildMap();
    }

    for (size_t i = 0; i < m.openedIdx.size(); i++) {
        int idx = m.openedIdx[i];
        if (idx < 0 || idx >= (int)w.containers.size()) continue;
        Container& c = w.containers[idx];
        c.searched = true;
        c.items = m.openedItems[i];
        if (idx < (int)s_generatedContainers && c.tx >= 0 && w.inBounds(c.tx, c.ty)) w.updateMapPixel(c.tx, c.ty);
    }

    for (const Container& c : m.dropped) {
        w.containers.push_back(c);
        int id = (int)w.containers.size() - 1;
        Container& nc = w.containers[id];
        if (nc.tx >= 0 && w.inBounds(nc.tx, nc.ty)) {
            Tile& t = w.at(nc.tx, nc.ty);
            t.solid = S_CONTAINER;
            t.container = (int16_t)id;
            t.hp = -1;
        }
    }

    for (size_t i = 0; i < m.spawnDead.size() && i < G.enemies.size(); i++)
        if (m.spawnDead[i]) G.enemies[i].dead = true;
}

static void localSeatsOut();
static void beginRaid(bool resume) {
    Profile& p = G.prof;
    p.inRaid = true;
    // One world per in-game day: leaving and coming back the same day returns you to
    // the same place. The layout only changes when the day does -- or when you die,
    // which rolls the next revision of the same day (see finishDeath).
    uint64_t seed = todaySeed();
    // Co-op host: while anyone has been out today the world keeps running, so walking
    // back out finds it as it is now rather than rebuilt.
    bool keepWorld = Coop::host() && s_worldLive && s_worldKey == seed;
    if (!keepWorld) {
    s_rng = Rng(seed ^ 0xABCDEF);
    World::zombieSpawns = p.zombieMode();
    World::withCrypts = true;
    G.world.generate(seed, p.day);
    placeTurretsInWorld(G.world);
    s_gateT.clear();
    s_axes.clear();
    s_casings.clear();
    G.enemies.clear();
    G.bullets.clear();
    G.grenades.clear();
    }
    G.particles.clear();
    G.decals.clear();
    s_drops.clear();
    s_specks.clear();
    s_pools.clear();
    // Cars (0.11v): the world's hooks, and whatever cars today's world had.
    g_dynamicBlock = noCarBlock;
    g_dynamicOverlap = noCarOverlap;
    if (!keepWorld) {
        s_cars.clear();
        resetPatrols();
    }
    s_ride = -1;
    G.floatTexts.clear();
    G.flashes.clear();
    G.messages.clear();
    s_generatedContainers = G.world.containers.size();
    {   // sanity check on how close the nearest hostile starts out
        float nearest = 1e9f;
        int within60 = 0;
        for (const EnemySpawn& s : G.world.spawns) {
            float d = dist(s.pos, G.world.homePos) / TILE;
            nearest = std::min(nearest, d);
            if (d < 60) within60++;
        }
        std::fprintf(stderr, "[raid] day %d: %zu spawns, nearest %.0ft, %d within 60t\n",
                     p.day, G.world.spawns.size(), nearest, within60);
    }
    if (!keepWorld && !isGuest()) {
        for (size_t i = 0; i < G.world.spawns.size(); i++) {
            const EnemySpawn& sp = G.world.spawns[i];
            if (sp.type == EnemyType::Zombie) spawnRoamer(sp.pos, roamerKind(i));
            else spawnEnemy(sp.type, sp.pos);
            Enemy& ne = G.enemies.back();
            ne.spawnIdx = (int)i;
            ne.patrol = sp.patrol;
            if (sp.crypt) {
                // The catacombs' own: tougher, and better armed.
                float tier = cryptTier(p.day);
                ne.hp = ne.maxHp = ne.maxHp * (1.05f + 0.55f * tier);
                // Better guns come later: rifles for the gangs, SMGs for the heavies.
                if (ne.type == EnemyType::Bandit && s_rng.chance(0.2f + 0.8f * tier)) ne.weapon = IT_RIFLE;
                if (ne.type == EnemyType::Heavy && s_rng.chance(0.5f * tier)) ne.weapon = IT_SMG;
                if (const WeaponDef* wd = weaponDef(ne.weapon)) ne.mag = wd->magSize;
            }
        }
        applyDayMemory();
        // A catacomb finished earlier today keeps its way back open.
        for (int i = 0; i < (int)G.world.dungeons.size(); i++) if (p.cryptDone(i)) G.world.openGate(i);
        if (Coop::host() && p.rivals)
            for (Enemy& e : G.enemies)
                for (int s = 0; s < Coop::MAX_PLAYERS; s++)
                    if (!e.dead && dist(e.pos, rivalHatchAt(s)) < 18 * TILE) e.dead = true;
        if (Coop::host()) {
            s_worldLive = true;
            s_worldKey = seed;
            s_scaledFor = 1;
            s_scaledExtra = 0;
            scaleRaiders();
            netInitShadows();
        }
    }
    if (isGuest()) {
        // The host runs this world; ask it for the tiles and containers as they are.
        G.enemies.clear();
        s_mercViews.clear();
        Net::Writer w;
        w.u8(Coop::M_WANT_FULL);
        Coop::toHost(w, true);
    }

    G.player = Player();
    G.player.pos = hatchPos() + Vec2(0, 18);
    G.player.stamina = p.maxStamina();
    G.player.angle = PI / 2;
    if (resume) {
        // Everyone who was still up stands where they stood when you left.
        const RaidResume& r = p.resume;
        for (const RaidResume::Foe& f : r.foes)
            for (Enemy& e : G.enemies)
                if (e.spawnIdx == f.idx && !e.dead) {
                    if (!G.world.collides(f.pos.x, f.pos.y, ENEMY_R)) e.pos = e.lastPos = f.pos;
                    e.hp = std::min(e.maxHp, std::max(1.0f, f.hp));
                    break;
                }
        if (!G.world.collides(r.pos.x, r.pos.y, PLAYER_R)) G.player.pos = r.pos;
        G.player.angle = r.angle;
    }
    Input::suppressFireUntilRelease();
    if (!Coop::active()) {
        s_hordeActive = false;
        s_hordePending = 0;
    }
    s_hordeNote.clear();
    if (!keepWorld) s_homeDist.clear();
    s_zCorpses.clear();
    s_downT = -1;
    s_reviveT = 0;
    s_raidElapsed = 0;
    s_nightHoldShown = false;
    s_autosaveT = 0;
    if (p.baseHp < 0 || p.baseHp > baseMaxHp()) p.baseHp = baseMaxHp();
    if (!keepWorld)
        for (Turret& t : p.turrets) {
            t.cd = t.flashT = t.hurtT = t.beamT = t.retargetT = 0;
            t.target = -1;
            t.angle = angleOf(Vec2((float)t.dx, (float)t.dy));
        }
    if (!isGuest()) spawnSquad();
    // No half-finished active reload should carry in from a previous run.
    p.activePhase = Profile::ActivePhase::None;
    p.activeMarker = 0;
    p.activeResultT = 0;
    p.magBonus = 1.0f;
    if (!Coop::active()) G.nightFallen = false;
    G.warnStage = 0;
    const float stages[4] = {19 * 60.0f, 21 * 60.0f, 21 * 60 + 30.0f, 21 * 60 + 50.0f};
    while (G.warnStage < 4 && p.timeMin >= stages[G.warnStage]) G.warnStage++;
    G.raidStartMin = resume ? p.resume.startMin : p.timeMin;
    G.raidKills = resume ? p.resume.kills : 0;
    G.lootContainer = -1;
    G.panel = Panel::None;
    G.scene = Scene::Raid;
    G.flowT = 0;
    s_deathT = -1;
    s_bigT = 0;
    G.cam = G.player.pos - Vec2(R::viewW() / 2.0f, R::viewH() / 2.0f);

    if (G.mapTexW != G.world.w || G.mapTexH != G.world.h) {
        if (G.mapTex.id) glDeleteTextures(1, &G.mapTex.id);
        G.mapTex = R::createTexture(G.world.w, G.world.h, G.world.mapPixels.data());
        G.mapTexW = G.world.w;
        G.mapTexH = G.world.h;
    }
    G.world.reveal(G.player.pos, 20);
    // Your car, where you left it (or at the mechanic's), and the mechanic himself.
    spawnMyCar();
    resetMechanic();
    if (mechanicOut() && G.prof.day == CITY_DAY && !resume && !G.prof.mechanicMet)
        pushMessage(T("The land has opened up: there are cities out past the fields now."), P_YELLOW);
    Atmo::reset();
    bigText(T("DAY") + " " + std::to_string(p.day) + " - " + fmtTime(p.timeMin), P_WHITE, 3);
    if (resume) {
        if (p.resume.hordeLeft > 0) resumeHorde(p.resume.hordeN, p.resume.hordeLeft);
        pushMessage(T("Back where you left off."), P_LAVENDER);
    } else {
        sfx(Snd::door, 0.9f);
        pushMessage(T("The world outside has changed."), P_LAVENDER);
        if (hordeDueTonight() && p.timeMin >= 19 * 60)
            pushMessage(T1("Horde tonight at {0}. Hold the bunker.", hordeWhen()), P_ORANGE);
        else
            pushMessage(T("Return to the bunker hatch before 22:00."), P_YELLOW);
    }
    p.resume = RaidResume();
    s_localOut = false;
    localSeatsOut();
    raid_saveState();
}

void raid_start() {
    G.prof.raids++;
    beginRaid(false);
}

void raid_resume() {
    if (!G.prof.resume.valid) { G.prof.inRaid = false; base_enter(false); return; }
    beginRaid(true);
}

int raid_localRide() { return G.scene == Scene::Raid ? s_ride : -1; }

void raid_saveState() {
    Profile& p = G.prof;
    if (G.scene != Scene::Raid || !p.inRaid) return;
    if (s_deathT >= 0) { finishDeath(); return; }
    persistMyCar();
    captureDayMemory();
    RaidResume r;
    r.valid = true;
    r.pos = G.player.pos;
    r.angle = G.player.angle;
    r.startMin = G.raidStartMin;
    r.kills = G.raidKills;
    if (s_hordeActive) {
        r.hordeN = s_hordeN;
        r.hordeLeft = s_hordePending;
        for (const Enemy& e : G.enemies) if (!e.dead && e.type == EnemyType::Zombie && !e.roamer) r.hordeLeft++;
    }
    for (const Enemy& e : G.enemies)
        if (!e.dead && e.spawnIdx >= 0) r.foes.push_back({e.spawnIdx, e.pos, e.hp});
    p.resume = std::move(r);
    save_game();
}

namespace {
// Everything in the world that is not a player: the host (and solo) runs this; a
// guest only mirrors it.
// Thrown axes (0.12v): fly to where their target stood, hurt whoever is there, stick
// in the ground (or a wall) and lie there until taken back up or the day moves on.
void updateAxes(float dt) {
    for (size_t i = 0; i < s_axes.size();) {
        FlyingAxe& a = s_axes[i];
        a.t += dt;
        if (a.stage == 0) {
            Vec2 next = a.pos + a.vel * dt;
            bool wall = G.world.blocksMove(World::toTile(next.x), World::toTile(next.y + 4));
            if (!wall) a.pos = next;
            if (wall || a.t >= a.flight) {
                a.stage = 1;
                a.t = 0;
                for (const Target& t : s_targets)
                    if (dist(t.pos, a.pos + Vec2(0, 4)) < 11) { hurtSlot(t.slot, a.dmg, "Cut down by a thrown axe."); break; }
                for (Hireling* h : s_mercs)
                    if (!h->dead && dist(h->pos, a.pos + Vec2(0, 4)) < 11) { damageHireling(*h, a.dmg); break; }
                sfxAt(Snd::tile_hit, a.pos, G.player.pos, 0.5f, 0.7f);
            }
        } else if (a.stage == 1 && a.t > 0.35f) {
            a.stage = 2;
            a.t = 0;
        } else if (a.stage == 2 && a.t > 40) {
            s_axes.erase(s_axes.begin() + i);
            continue;
        }
        i++;
    }
}

// Everyone else's feet in the puddles (0.12v).
void othersSplash() {
    if (localCrypt() >= 0) return;
    for (const Enemy& e : G.enemies)
        if (!e.dead && lengthSq(e.pos - e.lastPos) > 0.01f) Atmo::otherStep(G.world, e.pos + Vec2(0, 4), (uint32_t)e.netId * 2654435761u);
    for (const Hireling* h : s_mercs)
        if (!h->dead) Atmo::otherStep(G.world, h->pos + Vec2(0, 5), (uint32_t)(uintptr_t)h);
}

void worldSim(float dt) {
    buildTargets();
    rebuildMercs();
    assignSeats();
    updatePatrols(dt);
    updateNight(dt);
    updateHordeSpawning(dt);
    for (auto& e : G.enemies) e.lastPos = e.pos;
    for (size_t i = 0; i < G.enemies.size(); i++) if (!G.enemies[i].dead) updateEnemy(i, dt);
    G.enemies.erase(std::remove_if(G.enemies.begin(), G.enemies.end(), [](const Enemy& e) { return e.dead; }), G.enemies.end());
    crowdZombies();
    updateTurrets(dt);
    for (Barricade& b : G.prof.barricades) b.hurtT -= dt;
    updateGates(dt);
    othersSplash();
    updateAxes(dt);
    updateSquad(dt);
    buryHirelings();
    // Nobody stands inside a car that drove into them.
    if (!s_cars.empty()) {
        for (Enemy& e : G.enemies)
            if (!e.dead && e.type != EnemyType::Shade) pushOutOfCars(e.pos, ENEMY_R);
        for (Hireling* h : s_mercs)
            if (!h->dead && h->rideCar < 0) pushOutOfCars(h->pos, PLAYER_R);
    }
}

// A guest's copies drift toward where the host last put them.
void guestSim(float dt) {
    float k = std::min(1.0f, dt * 15.0f);
    for (Enemy& e : G.enemies) {
        e.lastPos = e.pos;
        if (dist(e.pos, e.home) > 64) e.pos = e.home;
        else e.pos = e.pos + (e.home - e.pos) * k;
        e.hurtT -= dt;
        e.flashT -= dt;
        e.meleeCd -= dt;
    }
    for (MercView& m : s_mercViews) {
        m.lastPos = m.pos;
        m.pos = dist(m.pos, m.target) > 64 ? m.target : m.pos + (m.target - m.pos) * k;
        m.seen += dt;
    }
    s_mercViews.erase(std::remove_if(s_mercViews.begin(), s_mercViews.end(), [](const MercView& m) { return m.seen > 1.0f; }), s_mercViews.end());
    for (Turret& t : G.prof.turrets) { t.flashT -= dt; t.beamT -= dt; }
    for (Barricade& b : G.prof.barricades) b.hurtT -= dt;
    updateGates(dt);
    othersSplash();
}
}  // namespace

// ---- one player's part of a raid frame (0.12v) ------------------------------------
// Split out of raid_update so local co-op can give every seat its own turn, in its own
// character and controls, while the world itself runs once.

// Escape and the mechanic's pages: closing panels, opening the pause menu.
static void playerMenuKeys() {
    if (G.panel == Panel::MechanicTalk) {
        // The mechanic has his say: one page at a time, and no walking off mid-sentence.
        s_talkT += G.frameDt;
        if (s_talkT > 0.35f && (Input::pressed(GLFW_KEY_E) || Input::keyPressed(GLFW_KEY_SPACE) || Input::keyPressed(GLFW_KEY_ENTER) || Input::mousePressed(0))) {
            Input::consumeMouse();
            sfx(Snd::click, 0.5f, 1.1f);
            s_talkT = 0;
            if (++s_talkPage >= MECH_PAGES) finishTalk();
        }
    } else if (Input::pressed(GLFW_KEY_ESCAPE)) {
        if (G.panel == Panel::Loot) closeLoot();
        else if (G.panel == Panel::Pause) G.panel = Panel::None;
        else if (G.panel == Panel::Options || G.panel == Panel::Controls) G.panel = Panel::Pause;
        else if (G.panel != Panel::None) G.panel = Panel::None;
        else if (s_deathT < 0) G.panel = Panel::Pause;
    }
}

static bool pausedPanel() {
    return G.panel == Panel::Pause || G.panel == Panel::Controls || G.panel == Panel::QuitConfirm || G.panel == Panel::Options;
}

// Tab and M: the inventory and the map.
static void playerPanelKeys(bool paused) {
    if (paused || s_downT >= 0) return;
    if (Input::pressed(GLFW_KEY_TAB)) {
        if (G.panel == Panel::Inventory) G.panel = Panel::None;
        else { closeLoot(); G.panel = Panel::Inventory; }
    }
    if (Input::pressed(GLFW_KEY_M)) {
        if (G.panel == Panel::Map) G.panel = Panel::None;
        else if (!G.prof.hasMap()) pushMessage(T("You have no map. Buy a map & compass from the trader."), P_ORANGE);
        else { closeLoot(); G.panel = Panel::Map; }
    }
}

// What a player's feet and position do: footsteps, finishing a catacomb, spike traps.
static void playerAmbient(float dt) {
    if (localCrypt() < 0) Atmo::footstep(G.world, G.player.pos + Vec2(0, 6), G.player.moving, dt);
    // Down in a catacomb whose way back is open: it is done for today.
    if (int c = localCrypt(); c >= 0 && G.world.gateOpen(c) && !G.prof.cryptDone(c)) {
        Profile& pp = G.prof;
        if (pp.cryptDoneDay != pp.day) { pp.cryptDoneDay = pp.day; pp.cryptDoneMask = 0; }
        pp.cryptDoneMask |= 1 << c;
    }
    // Spike traps in the catacombs' halls.
    s_spikeCd -= dt;
    if (localPresent() && s_spikeCd <= 0 && localCrypt() >= 0)
        for (const WorldProp& pr : G.world.props)
            if (pr.kind == PROP_SPIKES && dist(pr.pos, G.player.pos + Vec2(0, 4)) < 9 && spikesUp(pr, G.realTime)) {
                damagePlayer(9 + 12 * cryptTier(G.prof.day), "Impaled on a spike trap.");
                s_spikeCd = 0.7f;
                break;
            }
    // Footsteps: dirt and grass outside, a hard floor indoors and on the roads.
    if (G.player.moving && s_deathT < 0 && s_downT < 0) {
        G.player.stepT -= dt;
        if (G.player.stepT <= 0) {
            G.player.stepT = G.player.sprinting ? 0.27f : 0.38f;
            int tx = World::toTile(G.player.pos.x), ty = World::toTile(G.player.pos.y + 6);
            int g = G.world.inBounds(tx, ty) ? G.world.at(tx, ty).ground : G_GRASS;
            bool hard = g == G_FLOOR_WOOD || g == G_FLOOR_CONCRETE || g == G_FLOOR_TILE || g == G_ROAD || g == G_BRIDGE || g == G_BASE_FLOOR || g == G_CRYPT;
            sfx(hard ? Snd::step_in : Snd::step, G.player.sprinting ? 1.0f : 0.75f, s_rng.range(0.92f, 1.08f));
        }
    } else {
        G.player.stepT = 0.08f;
    }
}

// Local co-op: the group goes together. Whoever takes the stairs or the way down takes
// everyone still standing (and anyone down, to be carried along), each a step apart.
static void groupTravel(std::function<void()> go) {
    Local::atHome([go] {
        Local::forEach([&](int k) {
            if (s_localOut) return;
            if (s_ride >= 0) leaveCar(false);
            go();
            static const Vec2 OFF[4] = {{0, 0}, {12, 4}, {-12, 4}, {0, 10}};
            Vec2 at = G.player.pos + OFF[k & 3];
            if (!G.world.collides(at.x, at.y, PLAYER_R)) G.player.pos = at;
        });
    });
}

// Local co-op: home through the hatch, everybody at once (at home, after the turns).
static void localExtract() {
    Local::forEach([](int k) {
        if (k == 0) return;
        persistMyCar();
        s_ride = -1;
        closeLoot();
        Profile& p = G.prof;
        if (!s_localOut) {
            p.extractions++;
            G.summary = RaidSummary();
            G.summary.kills = G.raidKills;
            G.summary.value = carriedValue();
            G.summary.minutes = p.timeMin - G.raidStartMin;
            G.summary.dayAfter = p.day;
        }
        p.inRaid = false;
        s_localOut = false;
    });
    s_localOut = false;
    extract();
}

// Bleeding out, walking, shooting, the car, and whatever E does. False when the raid is
// over for this frame (home through the hatch, bled out). `worldCars` also moves every
// car and the mechanic (single player: once a frame from here).
static bool playerActions(float dt, bool paused, bool worldCars) {
    // Down in co-op: bleed out unless someone gets to you.
    if (s_downT >= 0) {
        s_downT -= dt;
        G.player.moving = false;
        if (s_downT <= 0) { coopDeath(); return false; }
    }
    gatherInteract();
    if (!paused && s_downT < 0) updatePlayer(dt);
    else G.player.moving = false;
    if (g_devRide && s_ride < 0 && isGuest())
        if (Car* hc = carOf(0)) { G.player.pos = hc->pos; enterCar(0); }
    // Cars and the mechanic (0.11v).
    updateMyCar(dt);
    if (worldCars) {
        updateCars(dt);
        updateMechanic(dt);
    }
    if (s_ride < 0) pushOutOfCars(G.player.pos, PLAYER_R);
    if (G.panel == Panel::Mechanic && (dist(G.player.pos, s_mechPos) > 70 || s_ride >= 0)) G.panel = Panel::None;

    int cidx;
    Interact inter = findInteract(cidx);
    // Reviving is held, not tapped.
    if (inter == Interact::Revive && !paused && Input::down(GLFW_KEY_E) && G.panel == Panel::None) {
        if (s_reviveSlot != cidx) { s_reviveSlot = cidx; s_reviveT = 0; }
        s_reviveT += dt;
        if (s_reviveT >= 2.5f) {
            s_reviveT = 0;
            if (Coop::host()) {
                Net::Writer w;
                w.u8(Coop::M_REVIVE);
                Coop::sendReliable(cidx, w);
            } else {
                Net::Writer w;
                w.u8(Coop::M_REVIVE_REQ);
                w.u8((uint8_t)cidx);
                Coop::toHost(w, true);
            }
            pushMessage(T1("You got {0} back up.", Coop::player(cidx).name), P_YGREEN);
            sfx(Snd::heal, 0.7f);
        }
    } else {
        s_reviveT = 0;
    }
    if (!paused && s_downT < 0 && Input::pressed(GLFW_KEY_E) && G.panel != Panel::MechanicTalk && G.panel != Panel::Mechanic && s_ride >= 0) {
        leaveCar(false);
    } else if (!paused && s_downT < 0 && Input::pressed(GLFW_KEY_E) && G.panel != Panel::MechanicTalk && G.panel != Panel::Mechanic) {
        if (G.panel == Panel::Loot) { if (Input::keyPressed(GLFW_KEY_E)) closeLoot(); }
        else if (inter == Interact::Mechanic) {
            closeLoot();
            G.panel = Panel::Mechanic;
            s_shopSel = G.prof.activeCar >= 0 ? G.prof.activeCar : 0;
            if (G.prof.activeCar < 0)
                for (int i = 0; i < CAR_MODELS; i++) if (!G.prof.cars[i].owned) { s_shopSel = i; break; }
            s_shopColor = G.prof.activeCar >= 0 ? G.prof.cars[G.prof.activeCar].color : (int)(s_rng.next() % CAR_COLORS);
            sfx(Snd::click, 0.6f);
        } else if (inter == Interact::Car) {
            enterCar(cidx);
        } else if (inter == Interact::Refuel) {
            Car* c = myCar();
            Profile& pp = G.prof;
            if (c && takeFromSlots(pp.inv, IT_FUEL, 1, pp.invCapacity()) > 0) {
                c->fuel = std::min(carModel(c->model).tank, c->fuel + FUEL_CAN_LITRES);
                persistMyCar();
                sfxAt(Snd::fuel, c->pos, G.player.pos, 0.9f);
                pushMessage(T1("Poured a Fuel Can in: {0} L in the tank.", std::to_string((int)std::round(c->fuel))), P_YELLOW);
            }
        } else if (inter == Interact::Stairs) {
            if (Local::active()) groupTravel([cidx] { useStairs(cidx); });
            else useStairs(cidx);
        }
        else if (inter == Interact::Hatch) {
            if (hatchSealed()) {
                pushMessage(G.nightFallen ? T("The hatch is sealed. There is no way in.")
                                          : T("The hatch stays shut until the horde is beaten."), P_CORAL);
            } else if (Local::active()) {
                Local::atHome([] { localExtract(); });
                return false;
            } else {
                extract();
                return false;
            }
        } else if (inter == Interact::CryptDoor) {
            if (G.prof.cryptBanned(cidx)) {
                pushMessage(T("You died down there today. The way down is closed to you until tomorrow."), P_CORAL);
            } else if (Local::active()) {
                groupTravel([cidx] { if (!G.prof.cryptBanned(cidx)) enterCrypt(cidx, true); });
            } else {
                enterCrypt(cidx, true);
            }
        } else if (inter == Interact::CryptExit) {
            if (Local::active()) groupTravel([cidx] { enterCrypt(cidx, false); });
            else enterCrypt(cidx, false);
        } else if (inter == Interact::CryptGate) {
            if (!gateFromInside(cidx)) {
                pushMessage(T("Sealed. It only opens from the other side."), P_CORAL);
                sfx(Snd::door, 0.5f, 0.6f);
            } else {
                raiseGate(cidx);
            }
        } else if (inter == Interact::Container) {
            G.lootContainer = cidx;
            G.searchT = 0;
            G.panel = Panel::Loot;
            G.player.act = 3;   // bends down to it (the pick-up animation)
            G.player.actT = 0;
            if (!G.world.containers[cidx].searched) sfx(Snd::search, 0.6f);
        } else if (inter == Interact::Door) {
            int state = doorStateNear(G.player.pos, INTERACT_RANGE);
            bool changed = false;
            if (state == S_DOOR && G.world.openDoorNear(G.player.pos, INTERACT_RANGE)) {
                sfx(Snd::door, 0.8f);
                changed = true;
            } else if (state == S_DOOR_OPEN) {
                if (doorwayOccupied(G.player.pos, INTERACT_RANGE)) {
                    pushMessage(T("The doorway is blocked."), P_BEIGE);
                } else if (G.world.closeDoorNear(G.player.pos, INTERACT_RANGE)) {
                    sfx(Snd::door, 0.8f, 0.85f);
                    changed = true;
                }
            }
            if (changed && isGuest()) {
                Net::Writer w;
                w.u8(Coop::M_DOOR);
                w.f32(G.player.pos.x); w.f32(G.player.pos.y);
                w.u8(state == S_DOOR ? 1 : 0);
                Coop::toHost(w, true);
            }
        }
    }
    if (G.panel == Panel::Loot) {
        if (G.lootContainer < 0 || G.lootContainer >= (int)G.world.containers.size()) closeLoot();
        else {
            Container& c = G.world.containers[G.lootContainer];
            if (dist(c.pos, G.player.pos) > 36 || c.removed) closeLoot();
            else if (!c.searched) {
                G.searchT += dt;
                if (G.searchT >= c.searchTime) c.searched = true;
            }
        }
    }
    return true;
}

static void uploadMap(float dt) {
    G.mapUploadT -= dt;
    if (G.world.mapDirty && G.mapUploadT <= 0 && G.mapTex.id) {
        R::updateTexture(G.mapTex, G.world.mapPixels.data());
        G.world.mapDirty = false;
        G.mapUploadT = 0.25f;
    }
}

// ---- local co-op outside (0.12v) --------------------------------------------------
// Each seat's own copy of the raid's one-player state, swapped with the globals on its
// turn (see Local::with). Seat 0's lives in the globals, like everything of the host's.
struct RaidSeat {
    float spikeCd = 0, deathT = -1, downT = -1, reviveT = 0;
    std::string deathCause;
    int reviveSlot = -1, ride = -1;
    float crashCd = 0, carMsgT = 0, talkT = 0;
    int talkPage = 0;
    std::vector<InteractOpt> interOpts;
    int interSel = 0, interSelKey = -1;
    int assistTarget = -1;
    uint32_t assistNet = 0;
    float assistT = 0;
    int shopSel = 0, shopColor = 0;
    int pvpAttacker = -1;
    float pvpAttackT = 0;
    bool localOut = false;
};
RaidSeat s_raidSeats[Local::MAX_SEATS];

// Seat state that must start clean on a new trip out (or on joining one).
static void resetSeatRaidState() {
    s_spikeCd = 0; s_deathT = -1; s_downT = -1; s_reviveT = 0; s_reviveSlot = -1;
    s_ride = -1; s_crashCd = 0; s_carMsgT = 0; s_talkT = 0; s_talkPage = 0;
    s_interOpts.clear(); s_interSel = 0; s_interSelKey = -1;
    s_assistTarget = -1; s_assistNet = 0; s_assistT = 0;
    s_pvpAttacker = -1; s_pvpAttackT = 0;
    s_localOut = false;
}

// Delivers what the world did to this seat's player (damage, rewards, a revive) while
// it was not its turn: the co-op messages a guest would have been sent.
static bool s_localDeliver = false;
static void deliverLocalInbox() {
    if (!Coop::local()) return;
    auto msgs = Coop::takeLocalInbox(mySlot());
    s_localDeliver = true;
    for (const auto& m : msgs) {
        if (m.empty()) continue;
        Net::Reader r(m.data() + 1, m.size() - 1);
        raid_netMessage(0, m[0], r);
    }
    s_localDeliver = false;
}

// A seat on its way out with the others (going out, or joining while they are out).
static void seatStepOut(Vec2 at) {
    resetSeatRaidState();
    Profile& p = G.prof;
    G.player = Player();
    G.player.pos = at;
    G.player.stamina = p.maxStamina();
    G.player.angle = PI / 2;
    for (int i = 0; i < 12; i++) {
        Vec2 c = at + fromAngle(i * 0.52f) * 16.0f;
        if (!G.world.collides(G.player.pos.x, G.player.pos.y, PLAYER_R)) break;
        G.player.pos = c;
    }
    if (p.hp <= 0) p.hp = p.maxHp();
    p.inRaid = true;
    p.activePhase = Profile::ActivePhase::None;
    p.activeMarker = 0;
    p.activeResultT = 0;
    p.magBonus = 1.0f;
    G.raidStartMin = p.timeMin;
    G.raidKills = 0;
    G.lootContainer = -1;
    G.panel = Panel::None;
    Input::suppressFireUntilRelease();
}

// Every other seat follows player 1 out of the hatch.
static void localSeatsOut() {
    if (!Local::active()) return;
    Vec2 at = G.player.pos;
    Local::forEach([&](int k) {
        if (k == 0) return;
        static const Vec2 OFF[4] = {{0, 0}, {14, 2}, {-14, 2}, {0, 14}};
        seatStepOut(at + OFF[k & 3]);
        G.prof.raids++;
        spawnMyCar();
    });
    Local::publishSeats();
}

// The camera for a shared screen: the middle of everyone still out there, pulled back
// (up to the zoom-out option's limit) until all of them fit.
static void groupCamera(float dt) {
    Vec2 lo, hi;
    if (!Local::groupBox(lo, hi)) { lo = hi = G.player.pos; }
    float W = (float)R::width(), H = (float)R::height();
    float need = std::max((hi.x - lo.x + 90) / std::max(1.0f, W), (hi.y - lo.y + 120) / std::max(1.0f, H));
    float z = clampf(need, 1.0f, Local::maxZoom());
    float cur = R::zoom();
    R::setZoom(cur + (z - cur) * (1.0f - std::exp(-3.0f * dt)));
    Vec2 mid = (lo + hi) * 0.5f;
    if (Car* rc = carOf(s_ride); rc && driving()) mid = mid + rc->vel * 0.25f;
    Vec2 target = mid - Vec2(R::viewW() / 2.0f, R::viewH() / 2.0f);
    G.cam = G.cam + (target - G.cam) * (1.0f - std::exp(-8.0f * dt));
}

static void localRaidUpdate(float dt) {
    // The world's clock, weather and hordes: once, for everybody.
    updateTime(dt);
    s_autosaveT += dt;
    if (s_autosaveT >= 30) { s_autosaveT = 0; raid_saveState(); }
    Atmo::update(dt, s_hordeActive);
    s_raidElapsed += dt;
    if (!s_hordeActive && absMinutes() >= G.prof.nextHordeAt && s_raidElapsed > 8) launchHorde();

    // Each player's turn, in their own character and controls. One menu at a time.
    Local::forEach([&](int k) {
        deliverLocalInbox();
        if (s_localOut) { G.player.moving = false; G.panel = Panel::None; return; }
        int owner = Local::uiOwner();
        Input::setPadMenu(G.panel != Panel::None && G.panel != Panel::Map);
        Input::setDpadWalk(false);
        if (Local::justJoined()) return;
        Panel before = G.panel;
        playerMenuKeys();
        bool paused = pausedPanel();
        playerPanelKeys(paused);
        s_pvpAttackT -= dt;
        playerAmbient(dt);
        playerActions(dt, paused, false);
        if (owner >= 0 && owner != k && before == Panel::None && G.panel != Panel::None && G.panel != Panel::Map) {
            if (G.panel == Panel::Loot) closeLoot();
            G.panel = Panel::None;
            pushMessage(T1("{0} is using a menu. Wait a moment.", Coop::player(owner).name), P_LAVENDER);
        }
    });
    if (G.scene != Scene::Raid || !Local::active()) { Local::runDeferred(); return; }

    // Everyone's cars and the mechanic, then the world.
    updateCars(dt);
    updateMechanic(dt);
    G.flowT -= dt;
    if (G.flowT <= 0) {
        G.flowT = 0.4f;
        Vec2 lo, hi;
        G.world.computeFlow(Local::groupBox(lo, hi) ? (lo + hi) * 0.5f : G.player.pos, 72);
    }
    worldSim(dt);
    updateBullets(dt);
    updateGrenades(dt);
    updateEffects(dt);
    Local::publishSeats();
    Local::runDeferred();
    if (G.scene != Scene::Raid) return;

    // Everybody bled out: the day out is over for the whole group.
    bool anyone = false;
    Local::forEach([&](int) { anyone = anyone || !s_localOut; });
    if (!anyone) {
        Local::forEach([](int k) { if (k != 0) s_localOut = false; });
        s_localOut = false;
        base_enter(true);
        return;
    }
    groupCamera(dt);
    uploadMap(dt);
}

void raid_update(float dt) {
    // --perf: how long the raid's own work takes, every five seconds.
    struct PerfLog { double t0 = 0, sum = 0, worst = 0; int n = 0; ~PerfLog() {} };
    static PerfLog perf;
    extern bool g_devPerf;
    double perfStart = g_devPerf ? glfwGetTime() : 0;
    struct PerfEnd { double s; ~PerfEnd() {
        if (!g_devPerf) return;
        double d = glfwGetTime() - s;
        perf.sum += d; perf.n++; perf.worst = std::max(perf.worst, d);
        if (s - perf.t0 > 5) {
            std::fprintf(stderr, "[perf] update avg %.2f ms, worst %.2f ms, %zu enemies\n", perf.sum / perf.n * 1000, perf.worst * 1000, G.enemies.size());
            perf = PerfLog(); perf.t0 = s;
        }
    } } perfEnd{perfStart};
    if (Local::active()) { localRaidUpdate(dt); return; }
    bool coop = Coop::active();
    playerMenuKeys();
    bool paused = pausedPanel();
    if (paused && !coop) return;  // true pause - but a shared world cannot stop for one player

    if (s_deathT >= 0) {
        s_deathT -= dt;
        if (localCrypt() < 0) G.prof.timeMin += GAME_MINUTES_PER_SEC * dt;
        buildTargets();
        for (size_t i = 0; i < G.enemies.size(); i++) if (!G.enemies[i].dead) updateEnemy(i, dt);
        updateBullets(dt);
        updateEffects(dt);
        if (s_deathT <= 0) finishDeath();
        return;
    }

    playerPanelKeys(paused);

    updateTime(dt);
    if (!isGuest()) {
        // Autosave now and then, so even a crash or a pulled plug resumes close by.
        s_autosaveT += dt;
        if (s_autosaveT >= 30) { s_autosaveT = 0; raid_saveState(); }
    }
    Atmo::update(dt, s_hordeActive);
    playerAmbient(dt);
    s_raidElapsed += dt;
    s_pvpAttackT -= dt;
    if (!isGuest() && !s_hordeActive && absMinutes() >= G.prof.nextHordeAt && s_raidElapsed > 8) launchHorde();

    if (!playerActions(dt, paused, true)) return;

    if (!isGuest()) {
        G.flowT -= dt;
        if (G.flowT <= 0) {
            G.flowT = 0.4f;
            G.world.computeFlow(G.player.pos, 72);
        }
        worldSim(dt);
    } else {
        guestSim(dt);
    }
    updateBullets(dt);
    updateGrenades(dt);
    updateEffects(dt);

    // Camera with a little look-ahead toward the cursor.
    Vec2 screen(R::viewW() / 2.0f, R::viewH() / 2.0f);
    // A sniper rifle (plain or elite) sees much further: the camera leans well out.
    const Item& held = G.prof.weapons[G.prof.curWeapon];
    bool scoped = !held.empty() && baseWeapon(held.id) == IT_SNIPER;
    Vec2 look = (Input::mouse() - screen) * (scoped ? 0.55f : 0.18f);
    // No leaning toward a mouse that is off in another window.
    if (G.panel != Panel::None || s_downT >= 0 || !glfwGetWindowAttrib(G.window, GLFW_FOCUSED) || G.devClean) look = Vec2();
    if (Car* rc = carOf(s_ride); rc && driving()) {
        // Driving: the camera looks down the road, further the faster you go.
        Vec2 ahead = rc->vel * 0.42f;
        if (length(ahead) > 150) ahead = normalize(ahead) * 150.0f;
        look = ahead;
    }
    Vec2 target = G.player.pos - screen + look;
    G.cam = G.cam + (target - G.cam) * (1.0f - std::exp(-(driving() ? 5.0f : 10.0f) * dt));
    uploadMap(dt);
}

// ---------------------------------------------------------------- drawing
namespace {

float gunLength(int id) {
    switch (baseWeapon(id)) {
    case IT_PISTOL: return 5;
    case IT_SMG: return 7;
    case IT_SHOTGUN: return 9;
    case IT_RIFLE: return 10;
    case IT_SNIPER: return 12;
    case IT_LAUNCHER: return 11;
    }
    return 0;
}

// Animated character art from the pack, falling back to the built-in sprite.
// `act` (0.12v): 1 a shot just fired (the gun's recoil, then a shotgun's pump), 2 a
// punch or a swing of the bat, 3 bending down to pick something up; `actT` seconds
// since it began. `bat`: a bat is carried (swung in a punch, held when no gun is).
void drawCharacter(int fallbackSprite, Vec2 pos, float angle, int weapon, bool hurt, bool moving,
                   float animTime, bool reloading, bool helmet, float scale = 1, Color bodyTint = Color(), bool enemy = false,
                   int shirt = 0, int act = 0, float actT = 9, bool bat = false) {
    Color tint = hurt ? Color(1.0f, 0.55f, 0.55f) : bodyTint;
    Art::Dir dir = Art::dirFromAngle(angle);
    Vec2 base = pos + Vec2(0, 8 * scale);
    Art::Anim bodyAnim = moving ? Art::Anim::Run : Art::Anim::Idle;
    int bodyFrame = (int)(animTime * (moving ? 12.0f : 6.0f));
    bool punching = act == 2 && actT < (bat ? 0.45f : 0.3f);
    bool picking = act == 3 && actT < 0.5f;
    bool swinging = punching && bat;
    bool handsFree = weapon == IT_NONE || picking || punching;
    Art::Anim helmetAnim = Art::Anim::Idle;
    Art::Piece body;
    if (picking) {
        body = Art::humanBody(dir, Art::Anim::PickUp, (int)(std::min(actT, 0.3f) * 16.0f), false, enemy, shirt);
        helmetAnim = Art::Anim::PickUp;
    } else if (punching && !bat) {
        body = Art::humanBody(dir, Art::Anim::Punch, (int)(actT * 20.0f), false, enemy, shirt);
        helmetAnim = Art::Anim::Punch;
    }
    if (!body.valid()) body = Art::humanBody(dir, bodyAnim, bodyFrame, !handsFree || swinging || (bat && weapon == IT_NONE), enemy, shirt);
    if (!body.valid()) {
        sceneAddSprite(fallbackSprite, pos, tint, 0, scale);
        return;
    }
    Art::Anim gunAnim = reloading ? Art::Anim::Reload : Art::Anim::Idle;
    int gunFrame = reloading ? (int)(animTime * 8.0f) : bodyFrame;
    if (!reloading && act == 1) {
        // The kick of the shot, then a shotgun racks the next shell in.
        bool shotgun = baseWeapon(weapon) == IT_SHOTGUN;
        if (actT < 0.12f) { gunAnim = Art::Anim::Shoot; gunFrame = (int)(actT * 40.0f); }
        else if (shotgun && actT < 0.55f) { gunAnim = Art::Anim::Rack; gunFrame = (int)((actT - 0.12f) * 16.0f); }
    }
    Art::Piece gun = handsFree ? Art::Piece() : Art::humanGun(dir, gunAnim, gunFrame, weapon);
    if (gun.valid() && (gunAnim == Art::Anim::Shoot || gunAnim == Art::Anim::Rack))
        gun.frame = std::min(gun.frame, gun.sprite->frameCount() - 1);
    // The bat takes the gun's place: swung, or carried when there is no gun.
    if (bat && (swinging || weapon == IT_NONE) && !picking) {
        gun = Art::bat(dir, swinging ? Art::Anim::Attack : Art::Anim::Idle, swinging ? (int)(actT * 18.0f) : bodyFrame);
    }
    // Elite guns are gilded, so you can tell one across the street.
    Color gunTint = tint;
    if (itemDef(weapon).elite) {
        float g = 0.85f + 0.15f * std::sin(animTime * 5.0f);
        gunTint = Color(tint.r * 1.0f, tint.g * 0.82f * g + 0.1f, tint.b * 0.35f, tint.a);
    }
    // The gun sheets are separate from the body, so place them by hand per facing.
    static const Vec2 GUN_OFFSET[4] = {{0, 5}, {0, -5}, {5, 2}, {-5, 2}};
    bool gunBehind = dir == Art::Dir::Up;
    Vec2 gunPos = pos + GUN_OFFSET[(int)dir] * scale;
    if (gun.valid() && gunBehind) sceneAddCentered(gun, gunPos, gunTint, scale, -0.02f);
    sceneAdd(body, base, tint, scale);
    if (gun.valid() && !gunBehind) sceneAddCentered(gun, gunPos, gunTint, scale, 0.02f);
    if (helmet) {
        Art::Piece h = Art::helmet(dir, helmetAnim == Art::Anim::Idle ? bodyFrame : (int)(actT * (picking ? 16.0f : 20.0f)), helmetAnim);
        if (h.valid()) sceneAdd(h, base + Vec2(0, -9 * scale), tint, scale, 0.03f);
    }
}


// ---- what stops the flashlight (0.11v): every solid thing near you, in its own shape.
// Walls and doors are their whole tile; trunks and poles are round; barrels, crates,
// furniture, containers, wrecks and cars are their own pixels.
void queueFlashlightOccluders() {
    const World& w = G.world;
    Vec2 from = G.player.pos;
    int r = 18;
    int cx = World::toTile(from.x), cy = World::toTile(from.y);
    for (int y = cy - r; y <= cy + r; y++)
        for (int x = cx - r; x <= cx + r; x++) {
            if (!w.inBounds(x, y)) continue;
            const Tile& t = w.at(x, y);
            if (t.solid == S_NONE) continue;
            float px = (float)x * TILE, py = (float)y * TILE;
            Vec2 base(px + TILE * 0.5f, py + TILE);
            switch (t.solid) {
            case S_TREE: R::occDisc(World::tileCenter(x, y), solidInfo(S_TREE).radius); break;
            case S_POLE: R::occDisc(Vec2(px + TILE * 0.5f, py + TILE - 3), 3); break;
            case S_CRATE: case S_SANDBAG: {
                Art::Piece p = Art::barrel(t.solid == S_CRATE ? t.variant : (uint8_t)(t.variant + 3));
                if (p.valid()) {
                    const Assets::Frame& f = p.sprite->frame(p.frame);
                    R::occSprite(f, base + Vec2(0, 2), f.w * p.scale, f.h * p.scale, p.flipX);
                } else R::occRect(px, py, TILE, TILE);
                break;
            }
            case S_CONTAINER: {
                if (t.container < 0 || t.container >= (int)w.containers.size()) break;
                const Container& c = w.containers[t.container];
                Art::Piece p = Art::containerArt(c.kind, c.variant);
                if (p.valid()) {
                    const Assets::Frame& f = p.sprite->frame(p.frame);
                    R::occSprite(f, base, f.w * p.scale, f.h * p.scale, p.flipX);
                } else R::occRect(px, py, TILE, TILE);
                break;
            }
            case S_FURNITURE:
                if (t.furn && t.furn != FURN_REST && t.furn <= FURN_PIECE_COUNT) {
                    const FurnPiece& fp = FURN_PIECES[t.furn - 1];
                    if (const Assets::Sprite* s = Assets::find(std::string("furniture/") + fp.key)) {
                        const Assets::Frame& f = s->frame(0);
                        R::occSprite(f, Vec2(px + fp.w * TILE * 0.5f, py + TILE), (float)f.w, (float)f.h, false);
                    }
                }
                break;
            case S_CAR: {
                if (w.propAt.size() != w.tiles.size()) break;
                int pi = w.propAt[(size_t)y * w.w + x];
                if (pi < 0) break;
                // Only once, from the prop's own bottom-left tile.
                const WorldProp& pr = w.props[pi];
                Art::Piece pc = Art::propArt(pr);
                if (!pc.valid()) break;
                const Assets::Sprite& s = *pc.sprite;
                if (x != World::toTile(pr.pos.x - s.w * 0.5f) || y != World::toTile(pr.pos.y - 0.01f)) break;
                R::occSprite(s.frame(pc.frame), pr.pos, (float)s.w, (float)s.h, pr.kind == PROP_OBJECT ? pc.flipX : pc.flipX != pr.flipX);
                break;
            }
            case S_STAIRS: case S_TURRET: break;   // flat, or shot over
            default:
                if (solidInfo(t.solid).blocksBullets && t.solid != S_DOOR_OPEN) R::occRect(px, py, TILE, TILE);
                break;
            }
        }
    // Cars out in the world, except the one you are sitting in.
    for (const Car& c : s_cars) {
        if (c.owner == s_ride || dist(c.pos, from) > r * TILE + 40) continue;
        const Assets::Sprite* s = carSprite(c.model, c.color);
        if (!s || c.wrecked) continue;
        const Assets::Frame& f = s->frame(carFrame(c.angle));
        R::occSprite(f, Vec2(std::floor(c.pos.x), std::floor(c.pos.y + s->h * 0.5f)), (float)f.w, (float)f.h, false);
    }
    R::flushOcc();
}

// ---- cars and the mechanic, drawn (0.11v)
void drawCars() {
    Vec2 cf(std::floor(G.cam.x), std::floor(G.cam.y));
    for (const Car& c : s_cars) {
        if (c.pos.x < cf.x - 80 || c.pos.y < cf.y - 80 || c.pos.x > cf.x + R::viewW() + 80 || c.pos.y > cf.y + R::viewH() + 80) continue;
        const Assets::Sprite* s = nullptr;
        int frame = 0;
        Color tint = c.hurtT > 0 ? Color(1.0f, 0.7f, 0.7f) : Color();
        if (c.wrecked) {
            // Its burnt-out shell, facing the nearest of the pack's eight ways.
            static const Assets::Sprite* burnt[CAR_MODELS] = {};
            static bool looked[CAR_MODELS] = {};
            if (!looked[c.model]) {
                looked[c.model] = true;
                burnt[c.model] = Assets::find(std::string("vehicles/wreck/burnt_") + carModel(c.model).key);
            }
            s = burnt[c.model];
            frame = ((int)std::lround(c.angle / (PI / 4)) % 8 + 8) % 8;
            if (!s) { s = carSprite(c.model, c.color); frame = carFrame(c.angle); tint = Color(0.25f, 0.22f, 0.22f); }
        } else {
            s = carSprite(c.model, c.color);
            frame = carFrame(c.angle);
        }
        if (!s) continue;
        float fh = (float)s->h;
        // Sorted by where its footprint reaches down the screen, not by its art's box.
        sceneAdd(Art::Piece{s, frame, false, 1.0f}, Vec2(std::floor(c.pos.x), std::floor(c.pos.y + fh * 0.5f)), tint, 1, -fh * 0.5f + carDepth(c));
        sceneLift(spriteEmptyRowsBelow(s, frame));
    }
}

void drawCarTags() {
    if (!Coop::active() || G.prof.rivals) return;
    for (const Car& c : s_cars) {
        if (!onScreen(c.pos, -40) || c.wrecked) continue;
        int aboard = playersAboard(c.owner);
        if (aboard == 0 && c.owner == mySlot()) continue;
        std::string name = c.owner == mySlot() ? T("Your car") : Coop::player(c.owner).name;
        if (aboard > 0) name += "  " + std::to_string(aboard) + "/" + std::to_string(carModel(c.model).seats);
        R::textShadow(name, std::floor(c.pos.x - R::textWidth(name) / 2), c.pos.y - carModel(c.model).halfWid - 30, pal(Coop::colorPal(c.owner), 0.9f));
    }
}

void drawMechanic() {
    if (!mechanicOut()) return;
    bool moving = lengthSq(s_mechPos - s_mechLast) > 0.01f;
    drawCharacter(PLAYER, s_mechPos, s_mechAngle, IT_NONE, false, moving, G.realTime * 0.8f, false, false, 1, Color(), false, 3);
}

void drawMechanicTag() {
    if (!mechanicOut() || !onScreen(s_mechPos, -30)) return;
    R::textShadow(T("Rusty"), std::floor(s_mechPos.x - R::textWidth(T("Rusty")) / 2), s_mechPos.y - 27, pal(P_ORANGE));
    std::string role = T("MECHANIC");
    R::text(role, std::floor(s_mechPos.x - R::textWidth(role) / 2), s_mechPos.y - 19, pal(P_TAN, 0.85f));
}

// While you are in a car: its speed, fuel, health and seats, and the controls.
void drawCarHud() {
    Car* c = carOf(s_ride);
    if (!c) return;
    const CarModel& m = carModel(c->model);
    float W = (float)R::width(), H = (float)R::height();
    float w = 170, h = 44, x = std::floor(W / 2 - w / 2), y = H - h - 26;
    R::rect(x, y, w, h, pal(P_DARK, 0.8f));
    R::rectOutline(x, y, w, h, pal(P_PURPLE));
    std::string title = T(m.name) + (c->owner == mySlot() ? "" : "  (" + Coop::player(c->owner).name + ")");
    R::text(title, x + 5, y + 4, pal(P_YELLOW));
    int kmh = (int)std::round(c->speed() * 0.55f);
    std::string sp = std::to_string(kmh) + " km/h";
    R::text(sp, x + w - 5 - R::textWidth(sp), y + 4, pal(P_WHITE));
    float fuelFrac = m.tank > 0 ? c->fuel / m.tank : 0;
    R::text(T("FUEL"), x + 5, y + 16, pal(P_BEIGE));
    UI::bar(x + 36, y + 17, 80, 4, fuelFrac, fuelFrac < 0.2f ? P_CORAL : P_YELLOW);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.0f / %.0f L", c->fuel, m.tank);
    R::text(buf, x + 120, y + 16, pal(fuelFrac < 0.2f ? P_CORAL : P_BEIGE));
    float hpFrac = c->hp / m.hp;
    R::text(T("BODY"), x + 5, y + 27, pal(P_BEIGE));
    UI::bar(x + 36, y + 28, 80, 4, hpFrac, hpFrac < 0.3f ? P_CORAL : P_LGREEN);
    std::string seats = T("Seats") + " " + std::to_string(playersAboard(c->owner)) + "/" + std::to_string(m.seats);
    R::text(seats, x + 120, y + 27, pal(P_LAVENDER));
    if (driving()) {
        Prompt::Hint hints[4] = {{Prompt::Accelerate, T("Drive")}, {Prompt::Brake, T("Brake")}, {Prompt::Handbrake, T("Handbrake")}, {Prompt::Interact, T("Get out")}};
        float rw = Prompt::rowWidth(hints, 4);
        Prompt::row(hints, 4, std::floor(W / 2 - rw / 2), H - 22, pal(P_WHITE));
    } else {
        Prompt::Hint hints[2] = {{Prompt::Shoot, T("Shoot from the car")}, {Prompt::Interact, T("Get out")}};
        float rw = Prompt::rowWidth(hints, 2);
        Prompt::row(hints, 2, std::floor(W / 2 - rw / 2), H - 22, pal(P_WHITE));
    }
}

// Meeting the mechanic: his piece, one page at a time. Nothing else until he is done.
void drawMechanicTalk() {
    float W = (float)R::width(), H = (float)R::height();
    float w = std::min(460.0f, W - 40), h = 92;
    float x = std::floor(W / 2 - w / 2), y = std::floor(H - h - 30);
    UI::panel(x, y, w, h, T("RUSTY - THE MECHANIC"));
    int page = std::clamp(s_talkPage, 0, MECH_PAGES - 1);
    UI::textWrap(T(MECH_TALK[page]), x + 10, y + 22, w - 20, pal(P_CREAM), 11);
    std::string pg = std::to_string(page + 1) + "/" + std::to_string(MECH_PAGES);
    R::text(pg, x + 8, y + h - 14, pal(P_LAVENDER));
    Prompt::label(Prompt::Interact, page + 1 < MECH_PAGES ? T("Continue") : T("See you around"), x + w - 110, y + h - 19, pal(P_YELLOW));
}

// His shop: the cars, cheapest first. Pick one, pick its colour, pay him in money and
// parts (from your pockets). Cars you own can be taken out, fixed and filled up here.
void drawMechanicShop() {
    Profile& p = G.prof;
    float W = (float)R::width(), H = (float)R::height();
    float w = std::min(520.0f, W - 16), h = std::min(300.0f, H - 40);
    float x = std::floor(W / 2 - w / 2), y = std::floor(H / 2 - h / 2);
    UI::panel(x, y, w, h, T("RUSTY'S YARD - CARS"));
    std::string cash = "$" + std::to_string(p.money);
    R::text(cash, x + w - 6 - R::textWidth(cash), y + 4, pal(P_YGREEN));
    s_shopSel = std::clamp(s_shopSel, 0, CAR_MODELS - 1);
    // ---- the list
    float lx = x + 6, ly = y + 18, lw = 170, rh = 24;
    for (int i = 0; i < CAR_MODELS; i++) {
        const CarModel& m = carModel(i);
        float ry = ly + i * rh;
        if (ry + rh > y + h - 6) break;
        bool sel = i == s_shopSel;
        const Profile::OwnedCar& oc = p.cars[i];
        R::rect(lx, ry, lw, rh - 2, pal(sel ? P_PURPLE : P_DARK, sel ? 0.8f : 0.55f));
        if (sel) R::rectOutline(lx, ry, lw, rh - 2, pal(P_YELLOW));
        if (const Assets::Sprite* s = carSprite(i, oc.owned ? oc.color : (sel ? s_shopColor : 6))) {
            const Assets::Frame& f = s->frame(4);
            float sc = std::min(1.0f, 20.0f / std::max(1, f.h));
            R::frame(f, lx + 2, ry + 1, f.w * sc, f.h * sc);
        }
        R::text(T(m.name), lx + 44, ry + 3, pal(P_WHITE));
        std::string tag = oc.owned ? (p.activeCar == i ? T("In use") : T("Owned")) : "$" + std::to_string(m.price);
        R::text(tag, lx + 44, ry + 12, pal(oc.owned ? P_MINT : (p.money >= m.price ? P_YGREEN : P_CORAL)));
        if (UI::hover(lx, ry, lw, rh - 2)) {
            UI::focusable(lx, ry, lw, rh - 2);
            if (Input::mousePressed(0) && !sel) { s_shopSel = i; s_shopColor = oc.owned ? oc.color : s_shopColor; Input::consumeMouse(); }
        }
    }
    // ---- the one picked
    int mi = s_shopSel;
    const CarModel& m = carModel(mi);
    const Profile::OwnedCar& oc = p.cars[mi];
    float rx = x + 184, rw = w - 190, ry = y + 18;
    int col = oc.owned ? oc.color : s_shopColor;
    // Turning slowly on the spot, in the colour chosen.
    if (const Assets::Sprite* s = carSprite(mi, col)) {
        int frame = carFrame(G.realTime * 0.7f);
        const Assets::Frame& f = s->frame(frame);
        R::rect(rx, ry, 76, 62, pal(P_DARK, 0.6f));
        float sc = std::floor(std::min(2.0f, std::min(72.0f / f.w, 58.0f / f.h)) * 2) / 2;
        R::frame(f, std::floor(rx + 38 - f.w * sc / 2), std::floor(ry + 31 - f.h * sc / 2), f.w * sc, f.h * sc);
    }
    R::text(T(m.name), rx + 82, ry + 1, pal(P_YELLOW));
    UI::textWrap(T(m.blurb), rx + 82, ry + 12, rw - 84, pal(P_BEIGE), 10);
    // Stats, as bars against the best car's.
    const CarModel& best = carModel(CAR_MODELS - 1);
    struct Stat { const char* name; float v, max; std::string text; };
    char tankTxt[16], spdTxt[16];
    std::snprintf(tankTxt, sizeof tankTxt, "%.0f L", m.tank);
    std::snprintf(spdTxt, sizeof spdTxt, "%d km/h", (int)std::round(m.topSpeed * 0.55f));
    Stat stats[5] = {{"Seats", (float)m.seats, (float)best.seats, std::to_string(m.seats)},
                     {"Health", m.hp, best.hp, std::to_string((int)m.hp)},
                     {"Fuel tank", m.tank, best.tank, tankTxt},
                     {"Top speed", m.topSpeed, 250, spdTxt},
                     {"Handling", m.turn * m.grip, 2.9f * 9.5f, ""}};
    float sy = ry + 66;
    for (int i = 0; i < 5; i++) {
        R::text(T(stats[i].name), rx, sy, pal(P_LAVENDER));
        UI::bar(rx + 62, sy + 1, 90, 4, stats[i].v / stats[i].max, P_YGREEN);
        R::text(stats[i].text, rx + 158, sy, pal(P_BEIGE));
        sy += 10;
    }
    sy += 4;
    auto action = [&](const std::string& label, bool enabled, int color) {
        bool clicked = UI::button(rx, sy, rw, 15, label, enabled, color);
        sy += 18;
        return clicked;
    };
    if (!oc.owned) {
        // Price: money, and the parts he wants, counted from your pockets.
        R::text(T("Price") + ":  $" + std::to_string(m.price), rx, sy, pal(p.money >= m.price ? P_YGREEN : P_CORAL));
        sy += 11;
        float mx = rx;
        for (const CarMat& mt : m.mats) {
            if (mt.item == IT_NONE) continue;
            int have = pocketCount(mt.item);
            UI::itemIcon(mt.item, mx, sy - 2, 12);
            std::string t = std::to_string(std::min(have, mt.count)) + "/" + std::to_string(mt.count);
            R::text(t, mx + 14, sy, pal(have >= mt.count ? P_YGREEN : P_CORAL));
            if (UI::hover(mx, sy - 2, 14 + R::textWidth(t), 12)) UI::tooltip(T(itemDef(mt.item).name), T("Found out in the world. Carried in your pockets."));
            mx += 22 + R::textWidth(t);
        }
        if (mx == rx) R::text(T("No parts needed for this one."), rx, sy, pal(P_LAVENDER));
        sy += 13;
        // The colour.
        R::text(T("Colour"), rx, sy + 2, pal(P_LAVENDER));
        for (int c = 0; c < CAR_COLORS; c++) {
            float cx = rx + 44 + c * 14;
            R::rect(cx, sy, 11, 11, carColorSwatch(c));
            R::rectOutline(cx - 1, sy - 1, 13, 13, c == s_shopColor ? pal(P_YELLOW) : pal(P_DARK));
            if (UI::hover(cx - 1, sy - 1, 13, 13)) {
                UI::focusable(cx - 1, sy - 1, 13, 13);
                UI::tooltip(T(carColorName(c)), "");
                if (Input::mousePressed(0)) { s_shopColor = c; Input::consumeMouse(); }
            }
        }
        sy += 16;
        bool can = p.money >= m.price && haveMats(m);
        if (action(can ? T1("Buy it: ${0}", std::to_string(m.price)) : T("Not enough money or parts"), can, P_YGREEN)) shopBuy(mi, s_shopColor);
    } else {
        Car* mine = myCar();
        bool active = p.activeCar == mi;
        float hpFrac = oc.hp < 0 ? 1.0f : oc.hp / m.hp;
        float fuel = oc.fuel < 0 ? m.tank : oc.fuel;
        if (active && mine && !mine->wrecked) { hpFrac = mine->hp / m.hp; fuel = mine->fuel; }
        char st[64];
        std::snprintf(st, sizeof st, "%s %d%%   %s %.0f/%.0f L", T("Body").c_str(), (int)std::round(hpFrac * 100), T("Fuel").c_str(), fuel, m.tank);
        R::text(st, rx, sy, pal(hpFrac <= 0 ? P_CORAL : P_BEIGE));
        sy += 13;
        if (!active) {
            if (action(T("Take this car out"), true, P_MINT)) shopTakeOut(mi);
        } else {
            bool here = mine && carNearYard();
            // Rebuilt or patched up.
            int fix = carRepairCost(mi, hpFrac);
            if (fix > 0) {
                bool okHere = here || !mine || hpFrac <= 0;
                if (action(hpFrac <= 0 ? T1("Rebuild the wreck: ${0}", std::to_string(fix)) : T1("Repair: ${0}", std::to_string(fix)),
                           p.money >= fix && okHere, P_YELLOW)) {
                    p.money -= fix;
                    p.cars[mi].hp = -1;
                    if (hpFrac <= 0 || !mine) { p.carDay = 0; spawnMyCar(); }
                    else mine->hp = m.hp;
                    persistMyCar();
                    sfx(Snd::tile_hit, 0.7f, 0.8f);
                    pushMessage(hpFrac <= 0 ? T("Rebuilt. It is waiting in its bay.") : T("Good as new."), P_YGREEN);
                    if (!isGuest()) save_game(); else Coop::sendProfile();
                }
            }
            // A full tank.
            int litres = (int)std::ceil(std::max(0.0f, m.tank - fuel));
            int cost = litres * FUEL_PRICE;
            if (litres > 0 && hpFrac > 0) {
                if (action(T2("Fill the tank: {0} L for ${1}", std::to_string(litres), std::to_string(cost)), p.money >= cost && here, P_ORANGE)) {
                    p.money -= cost;
                    if (mine) mine->fuel = m.tank;
                    p.cars[mi].fuel = m.tank;
                    persistMyCar();
                    sfx(Snd::fuel, 0.8f);
                    if (!isGuest()) save_game(); else Coop::sendProfile();
                }
            }
            // Towed back from wherever it was left.
            if (mine && !mine->wrecked && !here) {
                if (action(T("Tow it back to the yard: $150"), p.money >= 150 && mine->owner == mySlot() && s_ride != mySlot(), P_BLUE)) {
                    p.money -= 150;
                    if (s_ride >= 0) leaveCar(true);
                    mine->pos = garageBay(mySlot());
                    mine->angle = PI / 2;
                    mine->vel = Vec2();
                    persistMyCar();
                    pushMessage(T("Towed back to the yard."), P_BLUE);
                }
            }
            if (!here && mine && fix > 0 && hpFrac > 0) R::text(T("Bring it to the yard to fix or fill it."), rx, sy, pal(P_LAVENDER));
        }
        // A new coat of paint.
        sy = std::max(sy, y + h - 44);
        R::text(T("Repaint ($250)"), rx, sy + 2, pal(P_LAVENDER));
        for (int c = 0; c < CAR_COLORS; c++) {
            float cx = rx + 80 + c * 14;
            R::rect(cx, sy, 11, 11, carColorSwatch(c));
            R::rectOutline(cx - 1, sy - 1, 13, 13, c == oc.color ? pal(P_YELLOW) : pal(P_DARK));
            if (UI::hover(cx - 1, sy - 1, 13, 13) && c != oc.color) {
                UI::focusable(cx - 1, sy - 1, 13, 13);
                UI::tooltip(T(carColorName(c)), p.money >= 250 ? "" : T("Not enough money"));
                if (Input::mousePressed(0) && p.money >= 250) {
                    Input::consumeMouse();
                    p.money -= 250;
                    p.cars[mi].color = c;
                    if (active && mine) mine->color = c;
                    sfx(Snd::sell, 0.6f, 1.2f);
                    if (!isGuest()) save_game(); else Coop::sendProfile();
                }
            }
        }
    }
    R::text(T("Parts are taken from your pockets."), x + 6, y + h - 12, pal(P_LAVENDER));
    if (UI::button(x + w - 66, y + h - 20, 60, 15, T("Close")) || UI::panelClose()) G.panel = Panel::None;
}

// Aim assist's target (0.12v): four corners closing in on it as the lock settles.
void drawLockOn() {
    const Enemy* e = assistedEnemy();
    if (!e || driving()) return;
    s_assistT += G.frameDt;
    Vec2 c = toScreen(e->pos) + Vec2(0, -1);
    float k = clampf(s_assistT / 0.18f, 0, 1);
    float r = std::floor(lerpf(13.0f, 8.0f, k));
    Color col = pal(P_CORAL, 0.55f + 0.45f * k);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sy = -1; sy <= 1; sy += 2) {
            float x = std::floor(c.x + sx * r), y = std::floor(c.y + sy * r);
            R::rect(sx < 0 ? x : x - 2, y, 3, 1, col);
            R::rect(x, sy < 0 ? y : y - 2, 1, 3, col);
        }
}

void drawZombie(const Enemy& e, float animTime) {
    Color tint = e.hurtT > 0 ? Color(1.0f, 0.55f, 0.55f) : Color();
    Art::Dir dir = Art::dirFromAngle(e.angle);
    bool attacking = e.meleeCd > 0.35f;
    bool moving = lengthSq(e.pos - e.lastPos) > 0.01f;
    // Standing still they sway on their idle; each swing picks one of the pack's two.
    Art::Anim anim = attacking ? Art::Anim::Attack : moving ? Art::Anim::Walk : Art::Anim::Idle;
    int kind = e.artVariant % 3;
    bool alt = ((e.netId + (uint32_t)(G.realTime / 1.3f)) & 1) != 0;
    int frame = (int)(animTime * 9.0f);
    if (e.takeT >= 0) { anim = Art::Anim::PickUp; frame = (int)((0.8f - e.takeT) * 10.0f); }
    Art::Piece z = Art::zombie(kind, dir, anim, frame, alt, e.noAxe);
    Vec2 base = e.pos + Vec2(0, 8);
    if (z.valid()) sceneAdd(z, base, tint, kind == 1 ? 1.15f : 1.0f);
    else sceneAddSprite(SHADE, e.pos, tint);
}

void drawMinimap(float x, float y, float size) {
    Profile& p = G.prof;
    R::rect(x - 2, y - 2, size + 4, size + 4, pal(P_DARK));
    R::rectOutline(x - 2, y - 2, size + 4, size + 4, pal(P_PURPLE));
    float tiles = size;  // one tile per pixel
    float ptx = G.player.pos.x / TILE, pty = G.player.pos.y / TILE;
    float u0 = (ptx - tiles / 2) / G.world.w, v0 = (pty - tiles / 2) / G.world.h;
    float u1 = (ptx + tiles / 2) / G.world.w, v1 = (pty + tiles / 2) / G.world.h;
    R::setTexture(G.mapTex.id);
    R::quad({x, y}, {x + size, y}, {x + size, y + size}, {x, y + size}, u0, v0, u1, v1, Color());
    R::setTexture(0);
    Vec2 center(x + size / 2, y + size / 2);
    if (p.hasHomeMarker() && localCrypt() < 0) {
        Vec2 home = (hatchPos() - G.world.surfacePos(G.player.pos)) / (float)TILE;
        Vec2 hp = center + home;
        hp.x = clampf(hp.x, x + 3, x + size - 3);
        hp.y = clampf(hp.y, y + 3, y + size - 3);
        R::rect(hp.x - 2, hp.y - 2, 5, 5, pal(P_DARK));
        R::rect(hp.x - 1, hp.y - 1, 3, 3, pal(P_CORAL));
    }
    auto dot = [&](Vec2 world, int color) {
        Vec2 m = center + (world - G.player.pos) / (float)TILE;
        if (m.x < x || m.y < y || m.x >= x + size - 1 || m.y >= y + size - 1) return;
        R::rect(std::floor(m.x), std::floor(m.y), 1, 1, pal(color));
    };
    for (const Enemy& e : G.enemies)
        if (!e.dead && e.type == EnemyType::Zombie && !e.roamer) dot(e.pos, P_CORAL);
    if (isGuest()) { for (const MercView& m : s_mercViews) if (m.owner == mySlot()) dot(m.pos, P_MINT); }
    else for (const Hireling* h : s_mercs) if (!h->dead && h->owner == mySlot()) dot(h->pos, P_MINT);
    if (Coop::active() && p.hasTeamMarkers() && !p.rivals)
        for (int i = 0; i < Coop::MAX_PLAYERS; i++) {
            const Coop::NetPlayer& np = Coop::player(i);
            if (i == mySlot() || !np.used || np.where != Coop::W_RAID) continue;
            Vec2 m = center + (np.pos - G.player.pos) / (float)TILE;
            m.x = clampf(m.x, x + 1, x + size - 3);
            m.y = clampf(m.y, y + 1, y + size - 3);
            R::rect(std::floor(m.x), std::floor(m.y), 2, 2, pal(Coop::colorPal(i)));
        }
    R::rect(center.x - 1, center.y - 1, 3, 3, pal(P_WHITE));
    R::line(center, center + fromAngle(G.player.angle) * 5, 1, pal(P_WHITE));
}

// HUD pieces the crosshair is over fade out of the way (so a fight behind them can be
// seen), and fade back once it leaves. One smoothed alpha per piece.
enum HudPart { HUD_LEFT, HUD_CLOCK, HUD_MESSAGES, HUD_WEAPON, HUD_MINIMAP, HUD_COUNT };
float s_hudAlpha[HUD_COUNT] = {1, 1, 1, 1, 1};
float s_leftColumnBottom = 60;

float hudFade(int part, float x, float y, float w, float h) {
    Vec2 aim = Input::mouse();
    if (Input::usingPad()) aim = toScreen(G.player.pos) + fromAngle(G.player.angle) * 38.0f;
    const float pad = 6;
    bool over = G.panel == Panel::None && s_deathT < 0 && aim.x >= x - pad && aim.y >= y - pad && aim.x < x + w + pad && aim.y < y + h + pad;
    float target = over ? 0.18f : 1.0f;
    float& a = s_hudAlpha[part];
    a += (target - a) * (1.0f - std::exp(-10.0f * G.frameDt));
    R::setAlpha(a);
    return a;
}

// The pack's own HUD art (0.12v): a UI sprite at native size, `frac` of its width shown
// (from the left), for bars that fill. False when the pack does not have it.
static bool hudArt(const char* key, float x, float y, float frac = 1, Color c = Color()) {
    const Assets::Sprite* s = Assets::find(std::string("ui/") + key);
    if (!s || !s->valid()) return false;
    Assets::Frame f = s->frame(0);
    frac = clampf(frac, 0, 1);
    if (frac <= 0) return true;
    f.u1 = f.u0 + (f.u1 - f.u0) * frac;
    f.w = (int)std::round(f.w * frac);
    R::frame(f, x, y, s->w * frac, (float)s->h, c);
    return true;
}

// A magazine's rounds as a strip of the pack's bullet indicators, full then spent,
// fitted into `width` (a mark stands for several rounds in a big magazine).
static void drawBulletStrip(const Item& w, float x, float y, float width) {
    const WeaponDef* wd = weaponDef(w.id);
    if (!wd) return;
    int base = baseWeapon(w.id);
    const char* kind = base == IT_SHOTGUN ? "shotgun" : (base == IT_PISTOL || base == IT_REVOLVER) ? "pistol" : "gun";
    std::string full = std::string("ui/bullet indicators/small/") + kind + "-bullet_small";
    const Assets::Sprite* on = Assets::find(full);
    const Assets::Sprite* off = Assets::find(full + "_empty");
    if (!on || !off) return;
    int mag = std::max(1, magSizeOf(w));
    int fit = std::max(1, (int)(width / (on->w + 1)));
    int per = (mag + fit - 1) / fit;                  // rounds per mark
    int marks = (mag + per - 1) / per, lit = (std::max(0, w.data) + per - 1) / per;
    for (int i = 0; i < marks; i++) {
        const Assets::Sprite* s = i < lit ? on : off;
        R::frame(s->frame(0), x + i * (on->w + 1.0f), y, (float)s->w, (float)s->h);
    }
}

// The interaction prompt (one line, or a list to pick from), the revive bar and being
// down, around `at`: the middle of the screen, or in local co-op each player's own spot.
static void drawPlayerPrompts(float W, float H, Vec2 at) {
    (void)W; (void)H;
    // Interaction prompt: one line, or a list to pick from with the wheel.
    if (G.panel != Panel::Loot && G.panel != Panel::MechanicTalk && G.panel != Panel::Mechanic && !s_interOpts.empty()) {
        auto label = [](const InteractOpt& o, int& color) {
            color = P_WHITE;
            if (o.kind == Interact::Hatch) {
                if (hatchSealed()) { color = P_CORAL; return T("The hatch is sealed..."); }
                color = P_YELLOW;
                return T("Enter bunker (end raid)");
            }
            if (o.kind == Interact::Door)
                return T(doorStateNear(G.player.pos, INTERACT_RANGE) == S_DOOR_OPEN ? "Close door" : "Open door");
            if (o.kind == Interact::CryptDoor) {
                if (G.prof.cryptBanned(o.container)) { color = P_CORAL; return T("The catacombs (closed to you today)"); }
                color = P_ORANGE;
                return T("Go down into the catacombs");
            }
            if (o.kind == Interact::CryptExit) { color = P_YELLOW; return T("Climb back to the surface"); }
            if (o.kind == Interact::CryptGate) {
                if (!gateFromInside(o.container)) { color = P_CORAL; return T("Sealed gate"); }
                color = P_ORANGE;
                return T("Raise the gate");
            }
            if (o.kind == Interact::Revive) {
                color = P_YGREEN;
                return T1("Hold to revive {0}", Coop::player(o.container).name);
            }
            if (o.kind == Interact::Mechanic) { color = P_ORANGE; return T("Talk to Rusty (cars)"); }
            if (o.kind == Interact::Refuel) { color = P_YELLOW; return T("Pour in a Fuel Can"); }
            if (o.kind == Interact::Stairs) {
                bool up = o.container >= 0 && o.container < (int)G.world.stairs.size() && G.world.stairs[o.container].up;
                return up ? T("Go upstairs") : T("Go downstairs");
            }
            if (o.kind == Interact::Car) {
                const Car* c = carOf(o.container);
                if (c && c->owner == mySlot()) {
                    color = P_YGREEN;
                    return c->fuel <= 0 ? T("Get in (no fuel)") : T1("Drive the {0}", T(carModel(c->model).name));
                }
                color = P_MINT;
                return T1("Ride with {0}", Coop::player(o.container).name);
            }
            const Container& c = G.world.containers[o.container];
            std::string s = (c.searched ? T("Open ") : T("Search ")) + T(containerName(c.kind));
            if (c.searched && containerEmpty(c)) { s += T(" (empty)"); color = P_LAVENDER; }
            return s;
        };
        int n = (int)s_interOpts.size();
        if (n == 1) {
            int col;
            std::string s = label(s_interOpts[0], col);
            bool sealed = s_interOpts[0].kind == Interact::Hatch && hatchSealed();
            if (sealed) R::textCentered(s, at.x, at.y + 26, pal(col));
            else Prompt::labelCentered(Prompt::Interact, s, at.x, at.y + 20, pal(col));
        } else {
            float lh = 11, maxW = 0;
            std::vector<std::string> rows(n);
            std::vector<int> cols(n);
            for (int i = 0; i < n; i++) { rows[i] = label(s_interOpts[i], cols[i]); maxW = std::max(maxW, R::textWidth(rows[i])); }
            lh = 16;
            std::string chooseText = T("Choose");
            maxW = std::max(maxW + 18, Prompt::labelWidth(Prompt::Choose, chooseText));
            float bw = maxW + 10, bh = n * lh + 22;
            float bx = std::floor(at.x + 18), by = std::floor(at.y - bh / 2 + 8);
            R::rect(bx, by, bw, bh, pal(P_DARK, 0.72f));
            R::rectOutline(bx, by, bw, bh, pal(P_PURPLE));
            for (int i = 0; i < n; i++) {
                float ry = by + 3 + i * lh;
                bool sel = i == s_interSel;
                if (sel) {
                    R::rect(bx + 1, ry, bw - 2, lh, pal(P_PURPLE, 0.8f));
                    Prompt::icon(Prompt::Interact, bx + 3, ry);
                }
                R::text(rows[i], bx + 22, ry + 5, sel ? pal(cols[i]) : pal(cols[i], 0.6f));
            }
            Prompt::label(Prompt::Choose, chooseText, bx + 3, by + bh - 19, pal(P_LAVENDER, 0.85f));
        }
    }

    if (s_reviveT > 0) {
        R::rect(at.x - 31, at.y + 38, 62, 6, pal(P_DARK, 0.85f));
        UI::bar(at.x - 30, at.y + 39, 60, 4, s_reviveT / 2.5f, P_YGREEN);
    }
    if (s_downT >= 0 && Local::active()) {
        // Local co-op: the others are still playing on this screen, so no dimming, just
        // the bleed-out bar over this player.
        bool on = std::fmod(G.realTime, 0.8f) < 0.5f;
        R::textCentered(T("DOWN"), at.x, at.y - 34, pal(on ? P_CORAL : P_ORANGE), 1);
        R::rect(at.x - 21, at.y - 24, 42, 5, pal(P_DARK));
        UI::bar(at.x - 20, at.y - 23, 40, 3, s_downT / 30.0f, P_CORAL);
    } else if (s_downT >= 0) {
        R::rect(0, 0, W, H, pal(P_DARK, 0.35f));
        R::textCentered(T("YOU ARE DOWN"), W / 2, H / 2 - 30, pal(P_CORAL), 2);
        R::textCentered(T("A teammate can revive you by holding interact next to you."), W / 2, H / 2 - 10, pal(P_BEIGE));
        R::rect(W / 2 - 51, H / 2 + 2, 102, 6, pal(P_DARK));
        UI::bar(W / 2 - 50, H / 2 + 3, 100, 4, s_downT / 30.0f, P_CORAL);
    }
}

// One player's card in local co-op: their colour and name, health, stamina, the gun in
// their hands with its magazine round by round, grenades and medicine, and whether they
// are down or waiting in the bunker. Drawn in that seat's own context.
static void drawSeatCard(int k, float x, float y, float w) {
    Profile& p = G.prof;
    Player& pl = G.player;
    int col = Local::colorOf(k);
    const float h = 46;
    R::rect(x, y, w, h, pal(P_DARK, 0.85f));
    R::rectOutline(x, y, w, h, pal(col));
    std::string nm = Coop::player(k).name;
    while (nm.size() > 1 && R::textWidth(nm) > w - 72) nm.pop_back();
    R::text(nm, x + 4, y + 3, pal(col));
    std::string tag = "P" + std::to_string(k + 1);
    R::text(tag, x + w - 4 - R::textWidth(tag), y + 3, pal(P_LAVENDER));
    float tagX = x + w - 4 - R::textWidth(tag);
    if (s_localOut) {
        R::text(T("Bled out"), x + 4, y + 16, pal(P_CORAL));
        R::text(T("Waits in the bunker"), x + 4, y + 27, pal(P_LAVENDER));
        return;
    }
    // Health: the pack's small heart bar (channel 40 x 2 at (11, 5)).
    float hpFrac = clampf(p.hp / p.maxHp(), 0, 1);
    float hx = x + 3, hy = y + 12;
    R::rect(hx + 11, hy + 5, 40, 2, pal(P_DARK));
    if (!(hudArt("hp/small/hp_small", hx + 8, hy + 5, (3 + 40 * hpFrac) / 43.0f) && hudArt("hp/small/hp-bar_small", hx, hy)))
        UI::bar(hx, hy + 3, 52, 4, hpFrac, P_CORAL);
    R::text(std::to_string((int)std::ceil(p.hp)), hx + 56, hy + 2, pal(P_CORAL));
    UI::bar(hx + 11, hy + 11, 40, 2, pl.stamina / p.maxStamina(), P_YGREEN);
    if (!p.armor.empty()) UI::bar(hx + 11, hy + 14, 40, 2, p.armor.data / float(itemDef(p.armor.id).param), P_BLUE);
    if (s_downT >= 0) {
        bool on = std::fmod(G.realTime, 0.8f) < 0.5f;
        R::text(T1("DOWN {0}s", std::to_string((int)std::ceil(s_downT))), x + 4, y + 32, pal(on ? P_CORAL : P_ORANGE));
        return;
    }
    // The gun.
    const Item& gun = p.weapons[p.curWeapon];
    if (const WeaponDef* wd = weaponDef(gun.id)) {
        float gx = x + w - 22;
        hudArt("inventory/inventory-cell", gx - 2, y + 11);
        UI::itemIcon(gun.id, gx, y + 13, 16);
        int reserve = countInSlots(p.inv, wd->ammo, p.invCapacity());
        std::string ammo = std::to_string(gun.data) + "/" + std::to_string(reserve);
        R::text(ammo, x + w - 4 - R::textWidth(ammo), y + 36, pal(gun.data == 0 ? P_CORAL : P_YELLOW));
        if (pl.reloadT > 0) {
            float total = wd->reloadTime * p.reloadMul() * tierReload(itemTier(gun));
            UI::bar(x + 4, y + 43, w - 8, 1, clampf(1.0f - pl.reloadT / total, 0, 1), P_YELLOW);
        }
        drawBulletStrip(gun, x + 4, y + 29, w - 12 - R::textWidth(ammo));
    }
    int cap = p.invCapacity();
    std::string kit = "G" + std::to_string(countInSlots(p.inv, IT_GRENADE, cap)) + " H" +
                      std::to_string(countInSlots(p.inv, IT_BANDAGE, cap) + countInSlots(p.inv, IT_MEDKIT, cap));
    R::text(kit, tagX - 6 - R::textWidth(kit), y + 3, pal(P_SAGE));
}

void drawLocalCards(float W, float H) {
    int n = Local::count();
    float left = 92, gap = 4;
    float w = std::min(150.0f, std::floor((W - left - 6 - gap * (n - 1)) / std::max(1, n)));
    float x = left, y = H - 50;
    for (int k = 0; k < Local::MAX_SEATS; k++) {
        if (!Local::used(k)) continue;
        Local::with(k, [&] { drawSeatCard(k, x, y, w); });
        x += w + gap;
    }
    std::string hint = Local::joinHint();
    if (!hint.empty() && Local::uiOwner() < 0) R::text(hint, left, y - 11, pal(P_LAVENDER, 0.75f));
}

void drawHUD() {
    Profile& p = G.prof;
    Player& pl = G.player;
    float W = (float)R::width(), H = (float)R::height();
    // Local co-op with more than one player: each has a card along the bottom instead
    // of the one player's vitals and gun panel (0.12v).
    bool cards = Local::active() && Local::count() > 1;
    if (cards) drawLocalCards(W, H);
    if (!cards) {

    // Vitals (and the contract and team list under them: one column).
    hudFade(HUD_LEFT, 0, 0, 200, s_leftColumnBottom);
    // Health: the pack's heart and bar (0.12v), its channel 40 px long at (13, 4); the
    // plain bar when the art is missing.
    float hpFrac = clampf(p.hp / p.maxHp(), 0, 1);
    bool bleedOn = pl.bleedT > 0 && std::fmod(G.realTime, 0.9f) < 0.6f;
    float barX = 8, barW = 90, textX = 102;
    R::rect(6 + 13, 6 + 4, 40, 4, pal(P_DARK, 0.85f));
    if (hudArt("hp/hp", 6 + 10, 6 + 4, (3 + 40 * hpFrac) / 43.0f) && hudArt("hp/hp-bar", 6, 6)) {
        barX = 6 + 13; barW = 40; textX = 6 + 58;
        if (pl.bleedT > 0) R::rectOutline(6 + 12, 6 + 3, 42, 6, pal(P_CORAL, bleedOn ? 1.0f : 0.3f));
    } else {
        UI::bar(8, 8, 90, 7, hpFrac, P_CORAL);
        if (pl.bleedT > 0) R::rectOutline(7, 7, 92, 9, pal(P_CORAL, bleedOn ? 1.0f : 0.3f));
        R::rectOutline(7, 7, 92, 9, pal(P_DARK));
    }
    if (pl.bleedT > 0) Prompt::label(Prompt::Heal, T("BLEEDING"), textX + 26, 3, pal(P_CORAL, bleedOn ? 1.0f : 0.55f));
    R::textShadow(std::to_string((int)std::ceil(p.hp)), textX, 8, pal(P_CORAL));
    float yy = 21;
    if (!p.armor.empty()) {
        UI::bar(barX, yy, barW, 4, p.armor.data / float(itemDef(p.armor.id).param), P_BLUE);
        R::rectOutline(barX - 1, yy - 1, barW + 2, 6, pal(P_DARK));
        yy += 7;
    }
    UI::bar(barX, yy, barW, 3, pl.stamina / p.maxStamina(), P_YGREEN);
    R::rectOutline(barX - 1, yy - 1, barW + 2, 5, pal(P_DARK));
    if (s_hordeActive || p.baseHp < baseMaxHp()) {
        yy += 6;
        bool hit = s_hordeActive && std::fmod(G.realTime, 0.6f) < 0.3f && p.baseHp < baseMaxHp() * 0.35f;
        UI::bar(barX, yy, barW, 4, p.baseHp / baseMaxHp(), hit ? P_YELLOW : P_ORANGE);
        R::rectOutline(barX - 1, yy - 1, barW + 2, 6, pal(P_DARK));
        R::textShadow(T("BUNKER"), textX, yy - 2, pal(P_ORANGE));
        yy += 2;
    }

    // Keep the accepted contract visible while outside so every kill or recovered
    // item has an obvious result instead of only appearing at the bunker board.
    if (p.mission.active()) {
        float qy = yy + 9;
        bool done = missionDone(p.mission);
        std::string progress = missionProgress(p.mission);
        std::string title = T(missionTitle(p.mission)) + (done ? "  " + T("RETURN TO BASE") : std::string());
        float bw = std::max(154.0f, std::floor(std::max(R::textWidth(progress), R::textWidth(title))) + 10);
        R::rect(7, qy, bw, 25, pal(P_DARK, 0.82f));
        R::rectOutline(7, qy, bw, 25, pal(done ? P_YGREEN : P_PURPLE));
        R::text(title, 11, qy + 4, pal(done ? P_YGREEN : P_YELLOW));
        R::text(progress, 11, qy + 14, pal(P_WHITE));
    }

    // Co-op: everyone else in the game, where they are and how they are doing.
    if (Coop::active()) {
        float ty = yy + (p.mission.active() ? 38 : 10);
        for (int i = 0; i < Coop::MAX_PLAYERS; i++) {
            const Coop::NetPlayer& np = Coop::player(i);
            if (i == mySlot() || !np.used) continue;
            R::rect(7, ty, 96, 11, pal(P_DARK, 0.75f));
            R::rect(8, ty + 2, 6, 7, pal(Coop::colorPal(i)));
            std::string nm = np.name.size() > 12 ? np.name.substr(0, 12) : np.name;
            R::text(nm, 17, ty + 2, pal(Coop::colorPal(i)));
            const char* where = np.downed ? "DOWN" : np.where == Coop::W_RAID ? "OUT" : np.where == Coop::W_BASE ? "BUNKER" : "...";
            if (p.rivals) where = "";   // Rivals: you do not know what the others are doing
            R::text(T(where), 104, ty + 2, pal(np.downed ? P_CORAL : P_LAVENDER));
            if (np.maxHp > 0) UI::bar(17, ty + 9, 60, 1, np.hp / np.maxHp, P_CORAL);
            ty += 13;
        }
    }

    s_leftColumnBottom = yy + (p.mission.active() ? 38 : 10) + (Coop::active() ? 13.0f * (Coop::count() - 1) : 0.0f);
    }   // !cards

    // Clock.
    hudFade(HUD_CLOCK, W / 2 - 80, 0, 160, 50);
    float t = p.timeMin;
    int clockCol = t < 19 * 60 ? P_WHITE : t < 21 * 60 ? P_ORANGE : P_CORAL;
    bool flash = t >= 21 * 60 + 30 && std::fmod(G.realTime, 0.8f) < 0.4f;
    if (nightHeld() && !G.nightFallen) { clockCol = t >= CURFEW_MIN ? P_LAVENDER : clockCol; flash = false; }
    std::string clock = G.nightFallen ? T("NIGHT") : fmtTime(t);
    std::string sub;
    if (G.nightFallen) sub = T("SURVIVE?");
    else {
        int left = (int)(CURFEW_MIN - t);
        std::string until = std::to_string(left / 60) + "h" + (left % 60 < 10 ? "0" : "") + std::to_string(left % 60);
        sub = T2("DAY {0}  dark in {1}", std::to_string(p.day), until);
        if (t >= CURFEW_MIN) sub = s_hordeActive || hordeDueTonight() ? T("NIGHT - HORDE FIRST") : T("QUIET NIGHT");
    }
    float boxW = std::max(92.0f, std::floor(R::textWidth(sub)) + 12.0f);
    float boxX = std::floor(W / 2 - boxW / 2);
    R::rect(boxX, 4, boxW, 30, pal(P_DARK, 0.85f));
    R::rectOutline(boxX, 4, boxW, 30, pal(G.nightFallen ? P_CORAL : P_PURPLE));
    R::sprite(ambientBrightness(t) > 0.6f ? SUN : MOON, {W / 2 - 34, 15});
    R::textShadow(clock, W / 2 - 22, 9, pal(flash ? P_YELLOW : clockCol), 2);
    R::textCentered(sub, W / 2, 25, pal(P_LAVENDER), 1);
    if (s_hordeActive) {
        int left = s_hordePending;
        for (const Enemy& e : G.enemies) if (!e.dead && e.type == EnemyType::Zombie && !e.roamer) left++;
        if (isGuest()) left = s_hordeLeftNet;
        bool blink = std::fmod(G.realTime, 1.0f) < 0.7f;
        R::rect(W / 2 - 60, 36, 120, 12, pal(P_DARK, 0.85f));
        R::textCentered(T2("HORDE {0}: {1} LEFT", std::to_string(s_hordeN), std::to_string(left)), W / 2, 39, pal(blink ? P_CORAL : P_ORANGE));
    } else if (!G.nightFallen && !p.rivals) {
        // Always on the clock, so there is time to get the defenses ready.
        std::string next = T2("Horde {0} in {1}", std::to_string(p.hordeNum), hordeCountdown());
        float nw = std::floor(R::textWidth(next)) + 12;
        R::rect(std::floor(W / 2 - nw / 2), 36, nw, 12, pal(P_DARK, 0.75f));
        R::textCentered(next, W / 2, 39, pal(hordeCountdownColor()));
    }

    // Messages.
    hudFade(HUD_MESSAGES, W - 260, 0, 260, 8 + 10.0f * G.messages.size());
    float my = 8;
    for (auto& m : G.messages) {
        float a = clampf(m.life, 0, 1);
        if (a < 0.3f) continue;
        R::textShadow(m.text, W - 8 - R::textWidth(m.text), my, pal(m.color));
        my += 10;
    }

    // In a car: its dials (0.11v). At the wheel there is no gun in your hands.
    R::setAlpha(1);
    if (s_ride >= 0 && !cards) drawCarHud();
    if (!driving() && !cards) {
    // Weapon panel, the other gun and the grenade/heal row above it.
    // 0.12v: taller, with the magazine drawn round by round under the reload bar.
    hudFade(HUD_WEAPON, W - 152, H - 84, 152, 84);
    Item& w = p.weapons[p.curWeapon];
    float bx = W - 128, by = H - 62, bh = 56;
    R::rect(bx, by, 120, bh, pal(P_DARK, 0.85f));
    R::rectOutline(bx, by, 120, bh, pal(P_PURPLE));
    if (const WeaponDef* wd = weaponDef(w.id)) {
        hudArt("inventory/inventory-cell", bx + 2, by + 2);
        UI::itemIcon(w.id, bx + 4, by + 4, 16);
        bool elite = itemDef(w.id).elite;
        Color tc = tierColor(itemTier(w));
        R::text(T(itemDef(w.id).name), bx + 24, by + 5, tc);
        R::rectOutline(bx, by, 120, bh, tc.withA(0.8f));
        drawBulletStrip(w, bx + 4, by + 38, 112);
        if (elite) R::text(T("ELITE"), bx + 116 - R::textWidth(T("ELITE")), by + 5, pal(P_YELLOW, 0.6f + 0.4f * std::sin(G.realTime * 4.0f)));
        int reserve = countInSlots(p.inv, wd->ammo, p.invCapacity());
        std::string ammo = std::to_string(w.data);
        R::textShadow(ammo, bx + 24, by + 16, pal(w.data == 0 ? P_CORAL : P_YELLOW), 2);
        R::text("/ " + std::to_string(reserve), bx + 28 + R::textWidth(ammo, 2), by + 22, pal(P_BEIGE));
        if (pl.reloadT > 0) {
            float total = wd->reloadTime * p.reloadMul() * tierReload(itemTier(w));
            float prog = clampf(1.0f - pl.reloadT / total, 0, 1);
            bool jam = p.activePhase == Profile::ActivePhase::Failed;
            float rx = bx + 4, ry = by + 32, rw = 112, rh = 3;
            R::rect(rx, ry, rw, rh, pal(P_DARK, 0.9f));
            // The active band: hit reload with the marker inside this and the magazine
            // that comes back hits harder.
            if (p.activePhase == Profile::ActivePhase::Filling) {
                float za = clampf(p.activeZoneA, 0, 1) * rw;
                float zb = clampf(p.activeZoneB, 0, 1) * rw;
                R::rect(rx + za, ry, std::max(1.0f, zb - za), rh, pal(P_YGREEN, 0.85f));
            }
            // A jammed reload runs red until it finally seats the magazine.
            UI::bar(rx, ry, rw * prog, rh, 1.0f, jam ? P_CORAL : P_YELLOW);
            // Marker riding the bar.
            if (p.activePhase == Profile::ActivePhase::Filling) {
                R::rect(rx + rw * prog - 1, ry - 2, 2, rh + 4, pal(jam ? P_CORAL : P_WHITE));
            }
            R::rectOutline(rx - 1, ry - 1, rw + 2, rh + 2, pal(P_PURPLE));
        } else {
            // Result flash / hot-mag tag, drawn in the bar's spot so nothing overlaps.
            if (p.activePhase == Profile::ActivePhase::Perfect && p.activeResultT > 0) {
                R::text(T("PERFECT"), bx + 4, by + 30, pal(P_YGREEN));
            } else if (p.activePhase == Profile::ActivePhase::Failed && p.activeResultT > 0) {
                R::text(T("JAMMED"), bx + 4, by + 30, pal(P_CORAL));
            } else if (p.magBonus > 1.0f) {
                R::text(T("HOT MAG"), bx + 4, by + 30, pal(P_ORANGE));
            }
        }
    } else {
        R::text(T("No weapon"), bx + 8, by + 8, pal(P_CORAL));
    }
    const Item& other = p.weapons[1 - p.curWeapon];
    if (!other.empty()) {
        R::rect(bx - 22, by + 14, 20, 20, pal(P_DARK, 0.85f));
        hudArt("inventory/inventory-cell", bx - 22, by + 14);
        UI::itemIcon(other.id, bx - 20, by + 16, 16, pal(P_LAVENDER));
        Prompt::icon(Prompt::Swap, bx - 20, by - 3, 0.9f);
    }
    int cap = p.invCapacity();
    {
        // Above the weapon box: what you can throw and heal with, on their buttons, and
        // the reload button when the magazine is empty.
        std::string grenades = std::to_string(countInSlots(p.inv, IT_GRENADE, cap));
        std::string meds = std::to_string(countInSlots(p.inv, IT_BANDAGE, cap) + countInSlots(p.inv, IT_MEDKIT, cap));
        float hx = bx, hy = by - 18;
        hx += Prompt::label(Prompt::Grenade, grenades, hx, hy, pal(P_SAGE)) + 8;
        hx += Prompt::label(Prompt::Heal, meds, hx, hy, pal(P_LGREEN)) + 8;
        if (!w.empty() && weaponDef(w.id) && w.data == 0 && G.player.reloadT <= 0)
            Prompt::label(Prompt::Reload, T("Reload"), hx, hy, pal(P_CORAL, 0.6f + 0.4f * std::sin(G.realTime * 6.0f)));
    }
    }   // !driving

    // Minimap.
    Voice::drawHud(10, H - (p.hasMap() ? 96 : 14));
    if (p.hasMap()) {
        hudFade(HUD_MINIMAP, 8, H - 84, 76, 76);
        drawMinimap(10, H - 82, 72);
        R::setAlpha(1);
    }

    // Home marker at the screen edge (Hardcore: only with the home beacon). Up on a
    // city building's floor it points from the building (0.11v).
    Vec2 viewer = G.world.surfacePos(pl.pos);
    Vec2 homeScreen = toScreen(hatchPos() - viewer + pl.pos);
    float homeDist = dist(hatchPos(), viewer) / TILE;
    bool evening = t >= 17 * 60;
    if (p.hasHomeMarker() && localCrypt() < 0 && (homeScreen.x < 0 || homeScreen.y < 0 || homeScreen.x > W || homeScreen.y > H)) {
        Vec2 c(W / 2, H / 2);
        Vec2 d = normalize(homeScreen - c);
        float sx = d.x != 0 ? (W / 2 - 24) / std::fabs(d.x) : 1e9f;
        float sy = d.y != 0 ? (H / 2 - 40) / std::fabs(d.y) : 1e9f;
        Vec2 ep = c + d * std::min(sx, sy);
        bool blink = !evening || std::fmod(G.realTime, 1.0f) < 0.7f;
        if (blink) R::sprite(ARROW, ep, angleOf(d), 1, evening ? pal(P_CORAL) : Color());
        R::sprite(HOME_ICON, ep - d * 12);
        R::textCentered(std::to_string((int)homeDist) + "m", ep.x - d.x * 12, ep.y - d.y * 12 + 9, pal(evening ? P_CORAL : P_WHITE));
    }

    // Your car, when it is off screen and you are not in it (0.11v): a small grey arrow.
    if (Car* mc = myCar(); mc && s_ride < 0 && localCrypt() < 0 && G.world.floorAt(pl.pos) < 0) {
        Vec2 cs = toScreen(mc->pos);
        if (cs.x < 0 || cs.y < 0 || cs.x > W || cs.y > H) {
            Vec2 c(W / 2, H / 2);
            Vec2 d = normalize(cs - c);
            float sx = d.x != 0 ? (W / 2 - 40) / std::fabs(d.x) : 1e9f;
            float sy = d.y != 0 ? (H / 2 - 56) / std::fabs(d.y) : 1e9f;
            Vec2 ep = c + d * std::min(sx, sy);
            R::sprite(ARROW, ep, angleOf(d), 0.55f, mc->wrecked ? pal(P_CORAL, 0.7f) : pal(P_BEIGE, 0.8f));
            std::string tag = (mc->wrecked ? T("Wreck") : T("Car")) + " " + std::to_string((int)(dist(mc->pos, pl.pos) / TILE)) + "m";
            float tw = R::textWidth(tag);
            R::textShadow(tag, std::floor(clampf(ep.x - d.x * 10 - tw / 2, 2, W - tw - 2)), std::floor(ep.y - d.y * 10 + (d.y > 0.5f ? -14 : 4)), pal(P_BEIGE, 0.85f));
        }
    }

    // Dungeon locator: an orange arrow at the nearest catacomb door of the day.
    if (p.hasLocator() && localCrypt() < 0 && !G.world.dungeons.empty()) {
        const Dungeon* best = nullptr;
        for (int i = 0; i < (int)G.world.dungeons.size(); i++) {
            const Dungeon& d = G.world.dungeons[i];
            if (p.cryptDone(i)) continue;   // finished: nothing left to point at
            if (!best || dist(d.door, pl.pos) < dist(best->door, pl.pos)) best = &d;
        }
        Vec2 ds = best ? toScreen(best->door) : Vec2(W / 2, H / 2);
        if (best && (ds.x < 0 || ds.y < 0 || ds.x > W || ds.y > H)) {
            Vec2 c(W / 2, H / 2);
            Vec2 d = normalize(ds - c);
            float sx = d.x != 0 ? (W / 2 - 28) / std::fabs(d.x) : 1e9f;
            float sy = d.y != 0 ? (H / 2 - 46) / std::fabs(d.y) : 1e9f;
            Vec2 ep = c + d * std::min(sx, sy);
            R::sprite(ARROW, ep, angleOf(d), 0.85f, pal(P_ORANGE));
            Vec2 ip = ep - d * 11;
            R::circle(ip, 4, pal(P_DARK, 0.9f));
            R::circle(ip, 3, pal(P_ORANGE));
            std::string tag = T("Catacombs") + " " + std::to_string((int)(dist(best->door, pl.pos) / TILE)) + "m";
            float tw = R::textWidth(tag);
            R::textShadow(tag, std::floor(clampf(ip.x - tw / 2, 2, W - tw - 2)), std::floor(ip.y + (d.y > 0.5f ? -14 : 6)), pal(P_ORANGE));
        }
    }

    // Teammates off screen: a smaller arrow in their colour with their name, so the
    // home marker stays the one that stands out. A downed teammate blinks red.
    // Hardcore: only with the team radio.
    if (Coop::active() && p.hasTeamMarkers() && !p.rivals) {
        for (int i = 0; i < Coop::MAX_PLAYERS; i++) {
            const Coop::NetPlayer& np = Coop::player(i);
            if (i == mySlot() || !np.used || np.where != Coop::W_RAID) continue;
            Vec2 sp = toScreen(np.pos);
            if (sp.x >= 0 && sp.y >= 0 && sp.x <= W && sp.y <= H) continue;
            Vec2 c(W / 2, H / 2);
            Vec2 d = normalize(sp - c);
            float sx = d.x != 0 ? (W / 2 - 34) / std::fabs(d.x) : 1e9f;
            float sy = d.y != 0 ? (H / 2 - 50) / std::fabs(d.y) : 1e9f;
            Vec2 ep = c + d * std::min(sx, sy);
            int col = Coop::colorPal(i);
            bool blinkOn = std::fmod(G.realTime, 0.8f) < 0.5f;
            Color arrowCol = np.downed ? (blinkOn ? pal(P_CORAL) : pal(P_CORAL, 0.35f)) : pal(col, 0.85f);
            R::sprite(ARROW, ep, angleOf(d), 0.6f, arrowCol);
            Vec2 ip = ep - d * 9;
            R::rect(std::floor(ip.x) - 3, std::floor(ip.y) - 3, 6, 6, pal(P_DARK, 0.9f));
            R::rect(std::floor(ip.x) - 2, std::floor(ip.y) - 2, 4, 4, pal(np.downed ? P_CORAL : col));
            float m = dist(np.pos, pl.pos) / TILE;
            std::string tag = np.name + " " + std::to_string((int)m) + "m";
            if (np.downed) tag = np.name + " - " + T("DOWN");
            float tw = R::textWidth(tag);
            float tx = clampf(ip.x - tw / 2, 2, W - tw - 2);
            float ty = ip.y + (d.y > 0.5f ? -14 : 5);
            R::textShadow(tag, std::floor(tx), std::floor(ty), np.downed ? pal(P_CORAL, blinkOn ? 1.0f : 0.6f) : pal(col, 0.8f));
        }
    }

    if (!Local::active()) drawPlayerPrompts(W, H, Vec2(W / 2, H / 2));
    if (s_bigT > 0) {
        float s = 2;
        R::textCentered(s_bigText, W / 2, H * 0.28f, pal(s_bigColor), s);
    }
}

void drawLootPanel() {
    if (G.lootContainer < 0) return;
    Container& c = G.world.containers[G.lootContainer];
    float W = (float)R::width(), H = (float)R::height();
    // Six columns rather than four: the container names ("Contenedor de carga")
    // need more width than twelve slots do, and it matches the inventory beside it.
    float cw = 6 * SLOT + 12, ch = 18 + 2 * SLOT + 22;
    float cx = std::floor(W / 2 - cw - 8), cy = std::floor(H / 2 - 100);
    UI::panel(cx, cy, cw, ch, T(containerName(c.kind)));
    if (!c.searched) {
        R::text(T("Searching..."), cx + 8, cy + 30, pal(P_BEIGE));
        UI::bar(cx + 8, cy + 42, cw - 16, 4, G.searchT / c.searchTime, P_YELLOW);
    } else {
        drawSlotGrid(cx + 6, cy + 18, c.items, 12, 6, InvMode::Loot, true);
        if (containerEmpty(c)) R::text(T("Empty"), cx + 8, cy + 30, pal(P_PURPLE));
        if (UI::button(cx + 6, cy + ch - 18, cw - 12, 13, T("Take all"))) {
            Profile& p = G.prof;
            bool full = false;
            for (int i = 0; i < (int)c.items.size(); i++) {
                if (c.items[i].empty()) continue;
                const ItemDef& d = itemDef(c.items[i].id);
                int lootedId = c.items[i].id;
                bool autoEquip = (d.cat == Cat::Weapon && (p.weapons[0].empty() || p.weapons[1].empty())) ||
                                 (d.cat == Cat::Armor && p.armor.empty()) || (d.cat == Cat::Backpack && p.backpack.empty());
                if (autoEquip && equipFrom(c.items, i)) { missionAddLoot(lootedId); continue; }
                if (moveItem(c.items, i, p.inv, p.invCapacity()) > 0) missionAddLoot(lootedId);
                if (!c.items[i].empty()) full = true;
            }
            sfx(Snd::pickup, 0.6f);
            G.player.act = 3;
            G.player.actT = 0;
            if (full) pushMessage(T("Inventory full"), P_CORAL);
        }
    }
    drawInventoryPanel(std::floor(W / 2 + 8), cy, InvMode::Loot, c.searched ? &c.items : nullptr, 12);
}

// What the catacombs are, the first time you go down: the rules differ for solo,
// co-op and Hardcore, so the text does too.
void drawCryptIntro() {
    float W = (float)R::width(), H = (float)R::height();
    float w = 360, h = 178, x = std::floor(W / 2 - w / 2), y = std::floor(H / 2 - h / 2);
    UI::panel(x, y, w, h, T("THE CATACOMBS"));
    bool coop = Coop::active(), hc = G.prof.hardcore();
    float ty = y + 22;
    ty += UI::textWrap(T("Time stands still down here: the clock only runs while someone is up on the surface."), x + 10, ty, w - 20, pal(P_WHITE)) + 5;
    ty += UI::textWrap(T("Everything hits harder, but chests and urns hold far better loot: epic and rare guns, and now and then an elite one."), x + 10, ty, w - 20, pal(P_YGREEN)) + 5;
    ty += UI::textWrap(T("The halls go on a long way, room after room, with hordes of the dead and armed gangs. Watch the floor for spike traps. The stairs take you back up."), x + 10, ty, w - 20, pal(P_BEIGE)) + 5;
    std::string rule;
    if (!coop && !hc) rule = T("If you die down here, this catacomb is closed to you for the rest of the day.");
    else if (!coop) rule = T("Hardcore: if you die down here you lose everything you carry, and this catacomb is closed to you for the rest of the day.");
    else if (!hc) rule = T("If you go down, your friends can get you back up. If you bleed out, you wake in the bunker and this catacomb is closed to you until tomorrow; the others carry on without you, and your things stay on your body.");
    else rule = T("Hardcore: there is no reviving down here. Go down and you are dead: you wake in the bunker with nothing, this catacomb is closed to you until tomorrow, and the others carry on without you.");
    UI::textWrap(rule, x + 10, ty, w - 20, pal(P_CORAL));
    if (UI::button(x + w / 2 - 50, y + h - 24, 100, 16, T("Understood"), true, P_YGREEN) || Input::keyPressed(GLFW_KEY_ENTER)) {
        G.prof.cryptIntroSeen = true;
        G.panel = Panel::None;
        Input::suppressFireUntilRelease();   // that click was for the button, not the gun
        save_game();
    }
}

void drawMapPanel() {
    float W = (float)R::width(), H = (float)R::height();
    float size = std::floor(std::min(H - 50, W - 40));
    size = std::min(size, (float)G.world.w * 2);
    float x = std::floor(W / 2 - size / 2), y = std::floor(H / 2 - size / 2) + 6;
    UI::panel(x - 6, y - 20, size + 12, size + 26, T("MAP"));
    Prompt::label(Prompt::Map, T("Close"), x + size - Prompt::labelWidth(Prompt::Map, T("Close")) + 4, y - 20, pal(P_WHITE));
    // The surface, or (below) only the catacomb you are in.
    int crypt = localCrypt();
    float rx0 = 0, ry0 = 0, rw = (float)G.world.outW, rh = (float)G.world.outH;
    if (crypt >= 0) {
        const Dungeon& d = G.world.dungeons[crypt];
        rx0 = (float)d.x0; ry0 = (float)d.y0; rw = (float)d.w; rh = (float)d.h;
    }
    R::setTexture(G.mapTex.id);
    R::quad({x, y}, {x + size, y}, {x + size, y + size}, {x, y + size}, rx0 / G.world.w, ry0 / G.world.h,
            (rx0 + rw) / G.world.w, (ry0 + rh) / G.world.h, Color());
    R::setTexture(0);
    float sc = size / rw;
    const Profile& p = G.prof;
    Vec2 origin = Vec2(x, y) - Vec2(rx0, ry0) * sc;
    Vec2 home = hatchPos() / (float)TILE * sc + origin;
    if (p.hasHomeMarker() && crypt < 0) R::sprite(HOME_ICON, home);
    // Catacomb doors on the surface map once you have seen them.
    if (crypt < 0)
        for (int i = 0; i < (int)G.world.dungeons.size(); i++) {
            const Dungeon& d = G.world.dungeons[i];
            if (p.cryptDone(i)) continue;   // finished today: off the map
            int tx = World::toTile(d.door.x), ty = World::toTile(d.door.y);
            if (!G.world.inBounds(tx, ty) || (!G.world.at(tx, ty).explored && !p.hasLocator())) continue;
            Vec2 dp = d.door / (float)TILE * sc + origin;
            R::rect(std::floor(dp.x) - 2, std::floor(dp.y) - 2, 5, 5, pal(P_DARK));
            R::rect(std::floor(dp.x) - 1, std::floor(dp.y) - 1, 3, 3, pal(P_ORANGE));
        }
    for (const Enemy& e : G.enemies) {
        if (e.dead || e.type != EnemyType::Zombie || e.roamer) continue;
        Vec2 zp = e.pos / (float)TILE * sc + origin;
        if (crypt >= 0) continue;
        R::rect(std::floor(zp.x), std::floor(zp.y), 2, 2, pal(P_CORAL));
    }
    if (Coop::active() && p.hasTeamMarkers() && !p.rivals)
        for (int i = 0; i < Coop::MAX_PLAYERS; i++) {
            const Coop::NetPlayer& np = Coop::player(i);
            if (i == mySlot() || !np.used || np.where != Coop::W_RAID) continue;
            Vec2 tp = np.pos / (float)TILE * sc + origin;
            if (!sameArea(np.pos, G.player.pos)) continue;
            R::rect(std::floor(tp.x) - 1, std::floor(tp.y) - 1, 3, 3, pal(Coop::colorPal(i)));
            R::text(np.name, std::floor(tp.x) + 3, std::floor(tp.y) - 3, pal(Coop::colorPal(i)));
        }
    // Your car, and where the mechanic is (0.11v).
    if (crypt < 0) {
        if (const Car* mc = myCar()) {
            Vec2 cp = mc->pos / (float)TILE * sc + origin;
            R::rect(std::floor(cp.x) - 2, std::floor(cp.y) - 2, 5, 4, pal(P_DARK));
            R::rect(std::floor(cp.x) - 1, std::floor(cp.y) - 1, 3, 2, mc->wrecked ? pal(P_CORAL) : carColorSwatch(mc->color));
        }
        if (mechanicOut() && p.mechanicMet) {
            Vec2 mp = mechanicHome() / (float)TILE * sc + origin;
            R::text("M", std::floor(mp.x) - 2, std::floor(mp.y) - 4, pal(P_ORANGE));
        }
    }
    Vec2 pp = G.world.surfacePos(G.player.pos) / (float)TILE * sc + origin;
    bool blink = std::fmod(G.realTime, 0.6f) < 0.4f;
    R::rect(pp.x - 2, pp.y - 2, 5, 5, pal(P_DARK));
    R::rect(pp.x - 1, pp.y - 1, 3, 3, pal(blink ? P_WHITE : P_YELLOW));
    R::text(T("Yellow = containers   Explore to reveal"), x, y + size - 10, pal(P_WHITE));
}

void drawMusicRow(float x, float y, float w) {
    R::text(T("Music"), x, y, pal(P_LAVENDER));
    {
        char buf[16];
        std::snprintf(buf, sizeof buf, "%d%%", (int)std::round(Audio::musicVolume() * 100.0f));
        R::text(buf, x + w - R::textWidth(buf), y, pal(P_WHITE));
    }
    {
        float v = Audio::musicVolume();
        float orig = v;
        if (UI::slider(x, y + 10, w, 8, v)) Audio::setMusicVolume(v);
        else if (v != orig) Audio::setMusicVolume(v);  // drag released: persist final value
    }
    const char* track = Audio::musicTrack();
    if (track && track[0]) {
        std::string now = T1("Now playing: {0}", track);
        if (R::textWidth(now) > w) {
            while (now.size() > 4 && R::textWidth((now.substr(0, now.size() - 4) + "...").c_str()) > w)
                now.erase(now.size() - 4, 1);
            if (R::textWidth(now) > w) now = now.substr(0, now.size() - 4) + "...";
        }
        R::text(now, x, y + 22, pal(P_BEIGE));
    }
}

void drawPausePanel() {
    float W = (float)R::width(), H = (float)R::height();
    if (G.panel == Panel::Pause) {
        float w = 200, h = 178, x = std::floor(W / 2 - w / 2), y = std::floor(H / 2 - h / 2);
        UI::panel(x, y, w, h, T("PAUSED"));
        if (UI::button(x + 10, y + 22, w - 20, 16, T("Resume"))) {
            G.panel = Panel::None;
            Input::suppressFireUntilRelease();
        }
        if (UI::button(x + 10, y + 42, w - 20, 16, T("Controls"))) G.panel = Panel::Controls;
        if (UI::button(x + 10, y + 102, w - 20, 16, T("Options"))) G.panel = Panel::Options;
        if (Local::active()) {
            // Local co-op (0.12v): a joined player drops out from here; player 1 can send
            // everyone else home, or save and quit.
            if (Local::current() != 0) {
                if (UI::button(x + 10, y + 62, w - 20, 16, T("Leave (drop out)"), true, P_CORAL)) {
                    G.panel = Panel::None;
                    Local::leaveSeat(Local::current());
                }
            } else {
                if (UI::button(x + 10, y + 62, w - 20, 16, T("Everyone else out"), true, P_ORANGE)) {
                    G.panel = Panel::None;
                    Local::end();
                }
                if (UI::button(x + 10, y + 82, w - 20, 16, T("Save & quit to menu"))) {
                    G.panel = Panel::None;
                    Local::atHome([] {
                        raid_saveState();
                        if (Local::active()) Local::end();
                        if (G.scene == Scene::Raid) { G.scene = Scene::Menu; menu_init(); }
                    });
                    return;
                }
            }
            R::textCentered(T("Local co-op: time keeps running"), W / 2, y + h - 14, pal(P_LAVENDER), 1, false);
            return;
        }
        if (Coop::active()) {
            // A shared world has no pause and no abandoning: only leaving it.
            if (UI::button(x + 10, y + 62, w - 20, 16, Coop::host() ? T("End session (saves)") : T("Leave the game"), true, P_CORAL)) {
                G.panel = Panel::None;
                if (Coop::host()) raid_saveState();
                Coop::leave("");
                return;
            }
            R::textCentered(T("Co-op: time keeps running"), W / 2, y + h - 14, pal(P_LAVENDER), 1, false);
            return;
        }
        if (UI::button(x + 10, y + 62, w - 20, 16, T("Save & quit to menu"))) {
            // Keeps the raid exactly as it is: Continue drops you back here.
            G.panel = Panel::None;
            raid_saveState();
            if (G.scene == Scene::Raid) {
                G.scene = Scene::Menu;
                menu_init();
            }
            return;
        }
        if (UI::button(x + 10, y + 82, w - 20, 16, T("Abandon raid"), true, P_CORAL)) G.panel = Panel::QuitConfirm;
        {
            const char* track = Audio::musicTrack();
            if (track && track[0]) {
                R::textCentered(T("Now playing:"), W / 2, y + 124, pal(P_LAVENDER), 1, false);
                std::string name = track;
                float maxW = w - 16;
                if (R::textWidth(name) > maxW) {
                    while (name.size() > 1 && R::textWidth(name + "...") > maxW) name.pop_back();
                    name += "...";
                }
                R::textCentered(name, W / 2, y + 135, pal(P_BEIGE), 1, false);
            }
        }
        R::textCentered(T("Time is frozen"), W / 2, y + h - 14, pal(P_LAVENDER), 1, false);
    } else if (G.panel == Panel::QuitConfirm) {
        float w = 220, h = 84, x = std::floor(W / 2 - w / 2), y = std::floor(H / 2 - h / 2);
        UI::panel(x, y, w, h, T("ABANDON RAID?"));
        R::text(T("You will lose everything you carry\nand restart the current day."), x + 10, y + 22, pal(P_BEIGE));
        int yn = UI::yesNo(x + w - 33, y + 4);
        if (UI::button(x + 10, y + 56, 95, 16, T("Abandon"), true, P_CORAL) || yn == 1) {
            G.panel = Panel::None;
            s_deathCause = "You abandoned the raid.";
            finishDeath();
        }
        if (UI::button(x + w - 105, y + 56, 95, 16, T("Cancel")) || yn == 2) G.panel = Panel::Pause;
    }
}

}  // namespace

void drawControlsPanel(float x, float y);

// ---- the view from indoors (0.9v)
// Inside a building you only see the room you are in. Everything outside fades to
// black, except what you can see through a doorway or a hole in the wall when you look
// that way: a cone of rays from you, stopped by walls and closed doors. The hatch
// compound does not count (it is the yard you defend).
namespace {
float s_indoorAmt = 0;             // 0 outside .. 1 fully indoors (eased)
int s_indoorB = -1;                // the building you are in
std::vector<float> s_seen;         // per world tile: how visible it is now (eased)
std::vector<float> s_seenGoal;     // per world tile: this frame's rays
int s_seenW = 0, s_seenH = 0;
uint64_t s_seenWorld = 0;

bool blocksView(const Tile& t) {
    switch (t.solid) {
    case S_WALL_BRICK: case S_WALL_CONCRETE: case S_WALL_WOOD: case S_DOOR: case S_BUNKER: case S_BOUNDARY: return true;
    default: return false;
    }
}

int buildingAt(int tx, int ty) {
    for (size_t i = 0; i < G.world.buildings.size(); i++) {
        const Building& b = G.world.buildings[i];
        if (tx < b.x0 || ty < b.y0 || tx >= b.x0 + b.w || ty >= b.y0 + b.h) continue;
        if (G.world.homeTx >= b.x0 && G.world.homeTy >= b.y0 && G.world.homeTx < b.x0 + b.w && G.world.homeTy < b.y0 + b.h) return -1;
        return (int)i;
    }
    return -1;
}

void updateIndoorView(Vec2 cam, float dt) {
    World& w = G.world;
    if (s_seenW != w.w || s_seenH != w.h || s_seenWorld != w.seed) {
        s_seenW = w.w; s_seenH = w.h; s_seenWorld = w.seed;
        s_seen.assign((size_t)w.w * w.h, 1.0f);
        s_seenGoal.assign((size_t)w.w * w.h, 1.0f);
    }
    bool alive = s_deathT < 0;
    int b = alive ? buildingAt(World::toTile(G.player.pos.x), World::toTile(G.player.pos.y)) : -1;
    if (b >= 0) s_indoorB = b;
    // In a catacomb you see only what you could really see from where you stand: every
    // direction, but stopped by the walls.
    bool crypt = localCrypt() >= 0;
    float want = b >= 0 || crypt ? 1.0f : 0.0f;
    s_indoorAmt = crypt ? 1.0f : s_indoorAmt + (want - s_indoorAmt) * (1.0f - std::exp(-7.0f * dt));
    if (s_indoorAmt < 0.01f) { s_indoorB = -1; return; }
    if (crypt) {
        s_indoorB = -1;
        int tx0 = std::max(0, World::toTile(cam.x) - 2), ty0 = std::max(0, World::toTile(cam.y) - 2);
        int tx1 = std::min(w.w - 1, World::toTile(cam.x + R::viewW()) + 2), ty1 = std::min(w.h - 1, World::toTile(cam.y + R::viewH()) + 2);
        for (int y = ty0; y <= ty1; y++)
            for (int x = tx0; x <= tx1; x++) s_seenGoal[(size_t)y * w.w + x] = 0;
        float reach = std::sqrt((float)(R::viewW() * R::viewW() + R::viewH() * R::viewH())) * 0.55f;
        Vec2 eye = G.player.pos + Vec2(0, -2);
        for (float a = 0; a < 2 * PI; a += 0.006f) {
            Vec2 d = fromAngle(a);
            for (float r = 2; r < reach; r += 3) {
                Vec2 q = eye + d * r;
                int x = World::toTile(q.x), y = World::toTile(q.y);
                if (!w.inBounds(x, y)) break;
                float fade = clampf((reach - r) / 60.0f, 0, 1);
                if (x >= tx0 && y >= ty0 && x <= tx1 && y <= ty1) {
                    float& g = s_seenGoal[(size_t)y * w.w + x];
                    g = std::max(g, fade);
                }
                Tile& t = w.at(x, y);
                // What you have seen is on the map now; nothing else is.
                if (!t.explored) { t.explored = 1; w.updateMapPixel(x, y); w.mapDirty = true; }
                if (t.solid == S_CRYPT_WALL) {
                    // Show the wall you look at, and the face just above it.
                    if (y + 1 < w.h && y + 1 <= ty1 && x >= tx0 && x <= tx1 && y + 1 >= ty0) {
                        float& g2 = s_seenGoal[(size_t)(y + 1) * w.w + x];
                        g2 = std::max(g2, fade);
                    }
                    break;
                }
            }
        }
        // The black already hides everything out of sight; ease the edges.
        float k = 1.0f - std::exp(-14.0f * dt);
        for (int y = ty0; y <= ty1; y++)
            for (int x = tx0; x <= tx1; x++) {
                size_t i = (size_t)y * w.w + x;
                s_seen[i] += (s_seenGoal[i] - s_seen[i]) * k;
            }
        return;
    }

    int tx0 = std::max(0, World::toTile(cam.x) - 2), ty0 = std::max(0, World::toTile(cam.y) - 2);
    int tx1 = std::min(w.w - 1, World::toTile(cam.x + R::viewW()) + 2), ty1 = std::min(w.h - 1, World::toTile(cam.y + R::viewH()) + 2);
    for (int y = ty0; y <= ty1; y++)
        for (int x = tx0; x <= tx1; x++) s_seenGoal[(size_t)y * w.w + x] = 0;
    if (s_indoorB >= 0 && s_indoorB < (int)w.buildings.size()) {
        // The room itself, walls included.
        const Building& bd = w.buildings[s_indoorB];
        for (int y = std::max(ty0, bd.y0); y <= std::min(ty1, bd.y0 + bd.h - 1); y++)
            for (int x = std::max(tx0, bd.x0); x <= std::min(tx1, bd.x0 + bd.w - 1); x++) s_seenGoal[(size_t)y * w.w + x] = 1;
    }
    if (alive) {
        // What the doorways and holes let you see, in the direction you are looking.
        const float half = 0.62f;
        float reach = std::sqrt((float)(R::viewW() * R::viewW() + R::viewH() * R::viewH())) * 0.6f;
        Vec2 eye = G.player.pos;
        for (float a = -half; a <= half; a += 0.008f) {
            float edge = clampf((half - std::fabs(a)) / 0.18f, 0, 1);    // soft sides
            Vec2 d = fromAngle(G.player.angle + a);
            for (float r = 4; r < reach; r += 4) {
                Vec2 q = eye + d * r;
                int x = World::toTile(q.x), y = World::toTile(q.y);
                if (!w.inBounds(x, y)) break;
                float fade = edge * clampf((reach - r) / 90.0f, 0, 1);
                if (x >= tx0 && y >= ty0 && x <= tx1 && y <= ty1) {
                    float& g = s_seenGoal[(size_t)y * w.w + x];
                    g = std::max(g, fade);
                }
                if (blocksView(w.at(x, y))) break;
            }
        }
    }
    float k = 1.0f - std::exp(-9.0f * dt);
    for (int y = ty0; y <= ty1; y++)
        for (int x = tx0; x <= tx1; x++) {
            size_t i = (size_t)y * w.w + x;
            s_seen[i] += (s_seenGoal[i] - s_seen[i]) * k;
        }
}

// ---- looking in from outside (0.10v): the reverse of the indoor view. Standing near a
// building and looking at an open door or a hole, the roof over what you could see
// through it fades, in the same cone, so you can look into the room.
std::vector<float> s_peek, s_peekGoal;
int s_peekW = 0;
uint64_t s_peekWorld = 0;

float roofPeekAt(int tx, int ty) {
    if (s_peek.empty() || tx < 0 || ty < 0 || tx >= s_peekW || ty >= (int)(s_peek.size() / std::max(1, s_peekW))) return 0;
    return s_peek[(size_t)ty * s_peekW + tx];
}

void updateRoofPeek(Vec2 cam, float dt) {
    World& w = G.world;
    g_roofPeek = roofPeekAt;
    if (s_peekW != w.w || s_peekWorld != w.seed || s_peek.size() != (size_t)w.w * w.h) {
        s_peekW = w.w;
        s_peekWorld = w.seed;
        s_peek.assign((size_t)w.w * w.h, 0.0f);
        s_peekGoal.assign((size_t)w.w * w.h, 0.0f);
    }
    int tx0 = std::max(0, World::toTile(cam.x) - 2), ty0 = std::max(0, World::toTile(cam.y) - 2);
    int tx1 = std::min(w.w - 1, World::toTile(cam.x + R::viewW()) + 2), ty1 = std::min(w.h - 1, World::toTile(cam.y + R::viewH()) + 2);
    for (int y = ty0; y <= ty1; y++)
        for (int x = tx0; x <= tx1; x++) s_peekGoal[(size_t)y * w.w + x] = 0;
    // Any roofed building counts here, the hatch compound included.
    auto anyBuilding = [&](int tx, int ty) {
        for (const Building& b : w.buildings)
            if (tx >= b.x0 && ty >= b.y0 && tx < b.x0 + b.w && ty < b.y0 + b.h) return true;
        return false;
    };
    bool outside = s_deathT < 0 && !anyBuilding(World::toTile(G.player.pos.x), World::toTile(G.player.pos.y));
    if (outside) {
        const float half = 0.62f, reach = 190.0f;
        for (float a = -half; a <= half; a += 0.01f) {
            float edge = clampf((half - std::fabs(a)) / 0.18f, 0, 1);
            Vec2 d = fromAngle(G.player.angle + a);
            bool in = false;
            for (float r = 4; r < reach; r += 4) {
                Vec2 q = G.player.pos + d * r;
                int x = World::toTile(q.x), y = World::toTile(q.y);
                if (!w.inBounds(x, y)) break;
                const Tile& t = w.at(x, y);
                if (blocksView(t)) break;                  // a wall or a shut door
                if (anyBuilding(x, y)) in = true;          // through the doorway (or a hole)
                if (in && x >= tx0 && y >= ty0 && x <= tx1 && y <= ty1) {
                    // Strongest close by: this is peeking round a door, not x-ray.
                    float k = edge * clampf((reach - r) / 70.0f, 0, 1);
                    float& g = s_peekGoal[(size_t)y * w.w + x];
                    g = std::max(g, k);
                }
            }
        }
    }
    float k = 1.0f - std::exp(-8.0f * dt);
    for (int y = ty0; y <= ty1; y++)
        for (int x = tx0; x <= tx1; x++) {
            size_t i = (size_t)y * w.w + x;
            s_peek[i] += (s_peekGoal[i] - s_peek[i]) * k;
        }
}

// How dark a world point is under the indoor view (0 = seen, 1 = black).
float indoorDark(Vec2 p) {
    if (s_indoorAmt < 0.01f || s_seen.empty()) return 0;
    int x = World::toTile(p.x), y = World::toTile(p.y);
    if (x < 0 || y < 0 || x >= s_seenW || y >= s_seenH) return s_indoorAmt;
    return s_indoorAmt * (1.0f - s_seen[(size_t)y * s_seenW + x]);
}
bool indoorHidden(Vec2 p) { return indoorDark(p) > 0.6f; }

void drawIndoorView(Vec2 cam) {
    if (s_indoorAmt < 0.01f || s_seen.empty()) return;
    World& w = G.world;
    int tx0 = World::toTile(cam.x) - 1, ty0 = World::toTile(cam.y) - 1;
    int tx1 = World::toTile(cam.x + R::viewW()) + 1, ty1 = World::toTile(cam.y + R::viewH()) + 1;
    // Darkness at a tile corner: the average of the four tiles around it, so the edge
    // of what you can see is a soft gradient instead of a staircase.
    auto seenAt = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= w.w || y >= w.h) return 0.0f;
        return s_seen[(size_t)y * w.w + x];
    };
    auto corner = [&](int x, int y) {
        float v = (seenAt(x - 1, y - 1) + seenAt(x, y - 1) + seenAt(x - 1, y) + seenAt(x, y)) * 0.25f;
        return Color(0, 0, 0, s_indoorAmt * clampf(1.0f - v * 1.15f, 0, 1));
    };
    for (int y = ty0; y <= ty1; y++)
        for (int x = tx0; x <= tx1; x++) {
            Color c00 = corner(x, y), c10 = corner(x + 1, y), c11 = corner(x + 1, y + 1), c01 = corner(x, y + 1);
            if (c00.a + c10.a + c11.a + c01.a < 0.004f) continue;
            float z = R::zoom();
            R::gradRect((x * TILE - std::floor(cam.x)) / z, (y * TILE - std::floor(cam.y)) / z, TILE / z, TILE / z, c00, c10, c11, c01);
        }
}
}  // namespace

// ---- what the place sounds like (0.11v): rain, birds, wind and drips, faded in and out
// by the weather, the hour, the woods around you and whether you are under a roof.
float s_ambForest = 0, s_ambScanT = 0, s_beeT = 5;
void updateAmbience(float dt) {
    float t = G.prof.timeMin;
    if (localCrypt() >= 0) {
        // Underground: water dripping somewhere in the dark, and nothing else.
        Audio::setAmbient(Audio::AMB_DRIP, 0.8f, 0.85f);
        return;
    }
    // How wooded it is round you, looked at now and then.
    s_ambScanT -= dt;
    if (s_ambScanT <= 0) {
        s_ambScanT = 0.5f;
        int trees = 0, cx = World::toTile(G.player.pos.x), cy = World::toTile(G.player.pos.y);
        for (int y = cy - 10; y <= cy + 10; y++)
            for (int x = cx - 10; x <= cx + 10; x++)
                if (G.world.inBounds(x, y) && G.world.at(x, y).solid == S_TREE) trees++;
        s_ambForest = clampf(trees / 30.0f, 0, 1);
    }
    float rain = Atmo::rainAmount();
    // Day: birdsong from a little before sunrise to dusk.
    float day = clampf((t - 5.5f * 60) / 45.0f, 0, 1) * clampf((20.5f * 60 - t) / 60.0f, 0, 1);
    Vec2 sp = G.world.surfacePos(G.player.pos);
    bool city = G.world.cityAt(World::toTile(sp.x), World::toTile(sp.y)) >= 0;
    float inside = std::max(s_indoorAmt, G.world.floorAt(G.player.pos) >= 0 ? 1.0f : 0.0f);
    float muffle = 1.0f - 0.6f * inside;
    float birds = day * (1.0f - 0.85f * rain) * (0.3f + 0.7f * s_ambForest) * (city ? 0.35f : 1.0f);
    if (s_hordeActive || G.nightFallen) birds = 0;
    Audio::setAmbient(Audio::AMB_BIRDS, birds * muffle);
    // Rain: on the roof it is duller and further off.
    Audio::setAmbient(Audio::AMB_RAIN, rain * (inside > 0.5f ? 0.55f : 1.0f), inside > 0.5f ? 0.8f : 1.0f);
    // Wind: a low gusting after dark, and with a storm.
    float wind = (1.0f - day) * 0.55f + clampf((rain - 0.6f) * 2.0f, 0, 1) * 0.6f;
    Audio::setAmbient(Audio::AMB_WIND, wind * muffle);
    // Under a roof in the rain: the gutters run.
    Audio::setAmbient(Audio::AMB_DRIP, rain * inside * 0.6f);
    // Now and then a bee, on a dry day out among the trees and flowers.
    s_beeT -= dt;
    if (s_beeT <= 0) {
        s_beeT = s_rng.range(6, 16);
        if (day > 0.5f && rain < 0.1f && inside < 0.5f && !city && s_ambForest > 0.1f)
            Audio::ambientShot(Audio::AMS_BEE, 0.35f, s_rng.range(-0.9f, 0.9f));
    }
}

void drawRaidPanels();
void drawLocalRaidUi();
void raidLighting(Profile& p, Vec2 cam);

void raid_draw() {
    Profile& p = G.prof;
    updateAmbience(G.frameDt);
    Vec2 cam = G.cam;
    if (G.shake > 0) cam += Vec2(s_rng.range(-1, 1), s_rng.range(-1, 1)) * G.shake;
    float t = G.realTime;

    // ---- world layer
    sceneBegin();
    sceneSetWorld(&G.world);
    if (localCrypt() >= 0) R::setSun(Vec2(), 0);   // no sun below
    else setSunForTime(G.prof.timeMin);
    R::begin(R::WORLD, cam);
    {
        // Feet in the grass (0.12v): everyone standing about on screen.
        std::vector<Vec2> feet;
        Vec2 c0(std::floor(cam.x) - 16, std::floor(cam.y) - 16);
        auto add = [&](Vec2 p) {
            if (p.x > c0.x && p.y > c0.y && p.x < c0.x + R::viewW() + 32 && p.y < c0.y + R::viewH() + 32) feet.push_back(p);
        };
        if (s_ride < 0 && s_deathT < 0) add(G.player.pos + Vec2(0, 6));
        for (const Enemy& e : G.enemies) if (!e.dead) add(e.pos + Vec2(0, 6));
        for (const Hireling* h : s_mercs) if (!h->dead && h->rideCar < 0) add(h->pos + Vec2(0, 6));
        for (const MercView& m : s_mercViews) if (!m.ride) add(m.pos + Vec2(0, 6));
        for (const Target& tg : s_targets) add(tg.pos + Vec2(0, 6));
        setSteppers(feet);
    }
    drawWorldTiles(G.world, cam, t);
    for (const BloodSpeck& s : s_specks) {
        if (!onScreen(s.pos)) continue;
        float a = clampf((SPECK_LIFE - s.t) / 12.0f, 0, 1) * 0.9f;
        R::rect(std::floor(s.pos.x), std::floor(s.pos.y), s.size, s.size > 1 ? s.size - 1 : 1, bloodColor(s.shade, a));
    }
    for (auto& d : G.decals) {
        const Assets::Sprite* a = bloodSheet(0);
        const Assets::Sprite* b = bloodSheet(1);
        if ((d.sprite == BLOOD0 || d.sprite == BLOOD1) && a && b) {
            // A stain made of the splash's own last frames, turned by the decal's angle.
            bool fl = std::fmod(d.angle, 1.0f) < 0.5f;
            const Assets::Sprite* big = d.sprite == BLOOD0 ? a : b;
            R::spriteAt(*big, 1, d.pos, R::Pivot::Center, 1, Color(0.8f, 0.8f, 0.8f, 0.9f), fl);
            R::spriteAt(*(big == a ? b : a), 2, d.pos + Vec2(fl ? -3.0f : 3.0f, 2), R::Pivot::Center, 1, Color(0.8f, 0.8f, 0.8f, 0.85f), !fl);
            continue;
        }
        R::sprite(d.sprite, d.pos, d.angle);
    }
    if (localCrypt() < 0) Atmo::drawGround(G.world, cam);
    drawBloodPools();
    for (const ZombieCorpse& c : s_zCorpses) {
        Art::Piece body = Art::zombieDeath(c.kind, c.left, (int)(c.t * 10.0f), c.fall, c.noAxe);
        if (!body.valid()) continue;
        float a = clampf((30.0f - c.t) / 6.0f, 0, 1);
        R::spriteAt(*body.sprite, body.frame, c.pos + Vec2(0, 7), R::Pivot::Bottom, 1, Color(1, 1, 1, a), body.flipX);
    }
    drawCasings();
    for (const FlyingAxe& a : s_axes) {
        if (a.stage == 0) {
            // Spinning through the air, in an arc.
            float h = std::sin(clampf(a.t / std::max(0.05f, a.flight), 0, 1) * PI) * 10.0f;
            Art::Piece ap = Art::thrownAxe((Art::Dir)a.dir, 0, (int)(a.t * 16.0f));
            if (ap.valid()) R::spriteAt(*ap.sprite, ap.frame, a.pos + Vec2(0, -h), R::Pivot::Center, 1, Color(), ap.flipX);
        } else {
            Art::Piece ap = Art::thrownAxe((Art::Dir)a.dir, a.stage, a.stage == 1 ? (int)(a.t * 20.0f) : 99);
            if (ap.valid()) R::spriteAt(*ap.sprite, ap.frame, a.pos + Vec2(0, 6), R::Pivot::Bottom, 1, Color(), ap.flipX);
        }
    }
    // Sealed while a horde is on or the night has fallen (0.11v): the lid comes down,
    // two steel bars are dropped across it and its lamp turns red.
    bool sealedNow = hatchSealed() && localCrypt() < 0;
    Art::Piece hatchArt = Art::hatch(sealedNow);
    Vec2 hb = hatchPos() + Vec2(0, 8);
    if (hatchArt.valid()) R::spriteAt(*hatchArt.sprite, 0, hb, R::Pivot::Bottom, 1.4f);
    else R::sprite(HATCH, hatchPos());
    if (sealedNow && hatchArt.valid()) {
        float hw = hatchArt.sprite->w * 1.4f, hh = hatchArt.sprite->h * 1.4f;
        for (float fy : {0.3f, 0.72f}) {
            float y = hb.y - hh * fy;
            R::rect(hb.x - hw * 0.5f - 2, y - 1, hw + 4, 3, Color(0.16f, 0.15f, 0.17f));
            R::rect(hb.x - hw * 0.5f - 2, y - 1, hw + 4, 1, Color(0.42f, 0.40f, 0.44f));
            R::rect(hb.x - hw * 0.5f - 3, y - 2, 2, 5, Color(0.1f, 0.1f, 0.11f));
            R::rect(hb.x + hw * 0.5f + 1, y - 2, 2, 5, Color(0.1f, 0.1f, 0.11f));
        }
        float blink = 0.5f + 0.5f * std::sin(G.realTime * 5.0f);
        R::rect(hb.x - 1, hb.y - hh - 3, 2, 2, Color(0.9f, 0.15f + 0.1f * blink, 0.12f, 0.5f + 0.5f * blink));
    }
    drawTileSolids(G.world, cam, t);
    if (!p.rivals) for (const Turret& tu : p.turrets) drawTurret(tu, turretPos(tu), 1.0f);
    drawCars();
    drawMechanic();
    Vec2 cf(std::floor(cam.x), std::floor(cam.y));
    auto onScreen = [&](Vec2 pos) {
        return pos.x > cf.x - 48 && pos.y > cf.y - 64 && pos.x < cf.x + R::viewW() + 48 && pos.y < cf.y + R::viewH() + 48;
    };
    for (auto& e : G.enemies) {
        if (!onScreen(e.pos)) continue;
        float animTime = G.realTime + e.artVariant * 0.13f;
        if (e.type == EnemyType::Shade || e.type == EnemyType::Zombie) {
            drawZombie(e, animTime);
        } else {
            bool moving = lengthSq(e.pos - e.lastPos) > 0.02f;
            bool helmet = e.type == EnemyType::Heavy || e.type == EnemyType::Sniper || e.type == EnemyType::Bandit;
            drawCharacter(ENEMY_DEFS[(int)e.type].sprite, e.pos, e.angle, e.weapon, e.hurtT > 0, moving, animTime,
                          e.reloadT > 0, helmet, e.type == EnemyType::Heavy ? 1.15f : 1.0f, Color(), true, 0,
                          e.flashT > 0 ? 1 : 0, 0.09f - e.flashT);
        }
    }
    if (isGuest()) {
        for (const MercView& m : s_mercViews) {
            if (!onScreen(m.pos) || m.ride) continue;
            const HireTier& ht = hireTier(m.tier);
            drawCharacter(PLAYER, m.pos, m.angle, ht.weapon, m.hurt, lengthSq(m.pos - m.lastPos) > 0.01f,
                          G.realTime + m.tier * 0.31f, m.reload, ht.helmet);
        }
    } else {
        for (const Hireling* hp : s_mercs) {
            const Hireling& h = *hp;
            if (h.dead || !onScreen(h.pos) || h.rideCar >= 0) continue;
            const HireTier& ht = hireTier(h.tier);
            drawCharacter(PLAYER, h.pos, h.angle, ht.weapon, h.hurtT > 0, lengthSq(h.pos - h.lastPos) > 0.01f,
                          G.realTime + h.tier * 0.31f, h.reloadT > 0, ht.helmet, 1, Color(), false, 0,
                          h.meleeT < 0.4f ? 2 : h.flashT > 0 ? 1 : 0, h.meleeT < 0.4f ? h.meleeT : 0.09f - h.flashT);
        }
    }
    drawNetPlayers(Coop::W_RAID);
    if (s_localOut) {
        // Local co-op: player 1 bled out and waits in the bunker.
    } else if (s_downT >= 0) {
        Art::Dir d = Art::dirFromAngle(G.player.angle);
        Art::Piece dead = Art::humanBody(d, Art::Anim::Death, 99, false, false, p.shirt);
        if (dead.valid()) sceneAdd(dead, G.player.pos + Vec2(0, 8));
    } else if (s_deathT < 0) {
        if (s_ride < 0)   // in a car you are inside it, out of sight
            drawCharacter(PLAYER, G.player.pos, G.player.angle, p.weapons[p.curWeapon].id, G.player.hurtT > 0,
                          G.player.moving, G.realTime, G.player.reloadT > 0, false, 1, Color(), false, p.shirt, G.player.act,
                          // Looting: stays bent over the container while it is open.
                          G.panel == Panel::Loot && G.player.act == 3 ? std::min(G.player.actT, 0.3f) : G.player.actT, carryingBat());
    } else {
        Art::Dir d = Art::dirFromAngle(G.player.angle);
        Art::Piece dead = Art::humanBody(d, Art::Anim::Death, (int)((3.0f - s_deathT) * 8.0f), false, false, p.shirt);
        if (dead.valid()) sceneAdd(dead, G.player.pos + Vec2(0, 8), Color(0.9f, 0.75f, 0.75f));
    }
    for (auto& g : G.grenades) sceneAddSprite(GRENADE, g.pos, Color(), g.fuse * 8);
    sceneFlush();
    for (auto& e : G.enemies) {
        if (!onScreen(e.pos)) continue;
        if (e.hp < e.maxHp && e.type != EnemyType::Shade && !immune(e)) UI::bar(e.pos.x - 7, e.pos.y - 16, 14, 2, e.hp / e.maxHp, P_CORAL);
        if (e.state == AIState::Alert && e.type != EnemyType::Shade) R::text("?", e.pos.x - 2, e.pos.y - 24, pal(P_YELLOW));
    }
    drawCarTags();
    drawMechanicTag();
    if (isGuest()) {
        for (const MercView& m : s_mercViews) {
            if (!onScreen(m.pos) || m.ride) continue;
            R::textCentered(m.name, m.pos.x, m.pos.y - 24, pal(m.owner == mySlot() ? (m.guard ? P_BLUE : P_MINT) : Coop::colorPal(m.owner), 0.85f));
            UI::bar(m.pos.x - 7, m.pos.y - 15, 14, 2, m.hp, P_LGREEN);
        }
    } else {
        for (const Hireling* hp : s_mercs) {
            const Hireling& h = *hp;
            if (h.dead || !onScreen(h.pos) || h.rideCar >= 0) continue;
            int col = !Coop::active() || h.owner == mySlot() ? (h.guard ? P_BLUE : P_MINT) : Coop::colorPal(h.owner);
            R::textCentered(h.name, h.pos.x, h.pos.y - 24, pal(col, Coop::active() && h.owner != mySlot() ? 0.85f : 1.0f));
            UI::bar(h.pos.x - 7, h.pos.y - 15, 14, 2, h.hp / h.maxHp(), P_LGREEN);
        }
    }
    drawNetPlayerTags(Coop::W_RAID, cam);
    for (const Turret& tu : p.turrets) {
        Vec2 c = turretPos(tu);
        if (tu.hp <= 0 || !onScreen(c)) continue;
        float mx = turretStats(tu).maxHp;
        if (tu.hp < mx) UI::bar(c.x - 6, c.y - 11, 12, 2, tu.hp / mx, P_LGREEN);
    }
    for (auto& pt : G.particles) {
        if (pt.glow || !onScreen(pt.pos)) continue;
        R::rect(std::floor(pt.pos.x), std::floor(pt.pos.y), pt.size, pt.size, pal(pt.color));
    }
    for (const BloodDrop& d : s_drops) {
        if (!onScreen(d.pos)) continue;
        R::rect(std::floor(d.pos.x), std::floor(d.pos.y), d.size, 1, Color(0, 0, 0, 0.25f));   // its shadow
        R::rect(std::floor(d.pos.x), std::floor(d.pos.y - d.z), d.size, d.size, bloodColor(d.shade));
    }
    for (const BloodFx& bf : s_bloodFx) {
        const Assets::Sprite* sh = bloodSheet(bf.sheet);
        if (!sh || bf.t < 0) continue;
        int fr = std::min(sh->frameCount() - 1, (int)(bf.t / 0.1f));
        R::spriteAt(*sh, fr, bf.pos + Vec2(bf.flip ? -3.0f : 3.0f, -3), R::Pivot::Center, 1, Color(), bf.flip);
    }
    for (auto& ft : G.floatTexts) R::textShadow(ft.text, ft.pos.x - R::textWidth(ft.text) / 2, ft.pos.y, pal(ft.color));
    updateRoofPeek(cam, G.frameDt);
    drawRoofs(G.world, cam, G.player.pos, G.frameDt);
    g_roofPeek = nullptr;          // the bunker and the menu never peek
    if (localCrypt() < 0) Atmo::drawRain(G.world, cam);
    updateIndoorView(cam, G.frameDt);
    { extern bool g_devNoOcc; if (!g_devNoOcc) queueFlashlightOccluders(); }
    R::end();

    // ---- glow layer (unaffected by darkness)
    R::begin(R::GLOW, cam);
    if (p.laserActive() && s_deathT < 0) {
        // The beam stops on the first thing that would stop the flashlight too: walls,
        // closed doors, cars, trees - the same tiles and boxes the light's shadows use.
        Vec2 dir = fromAngle(G.player.angle);
        // Better guns carry a longer beam.
        float reach = laserLength(itemTier(p.weapons[p.curWeapon]));
        Vec2 start = G.player.pos + dir * 9.0f, end = G.player.pos + dir * reach;
        bool hit = false;
        // Roofs still showing (buildings you are not in) hide what is under them, so the
        // beam ends where it goes in under one.
        std::vector<const Building*> roofs;
        for (const Building& b : G.world.buildings) {
            if (b.reveal > 0.5f) continue;
            float bx0 = b.x0 * (float)TILE, by0 = b.y0 * (float)TILE;
            if (dist(G.player.pos, Vec2(bx0 + b.w * TILE * 0.5f, by0 + b.h * TILE * 0.5f)) > reach + (b.w + b.h) * TILE) continue;
            roofs.push_back(&b);
        }
        for (float d = 9.0f; d <= reach; d += 1.0f) {
            Vec2 q = G.player.pos + dir * d;
            int tx = World::toTile(q.x), ty = World::toTile(q.y);
            if (!G.world.inBounds(tx, ty)) { end = q; break; }
            bool underRoof = false;
            for (const Building* b : roofs)   // the roof covers all but the front wall row
                if (tx >= b->x0 && tx < b->x0 + b->w && ty >= b->y0 && ty < b->y0 + b->h - 1) { underRoof = true; break; }
            if (underRoof) { end = q; break; }
            const Tile& tile = G.world.at(tx, ty);
            if (!solidInfo(tile.solid).blocksBullets || tile.solid == S_DOOR_OPEN) continue;
            Vec2 c = World::tileCenter(tx, ty);
            float br = solidInfo(tile.solid).radius;
            float half = br > 0 ? br : TILE * 0.5f;
            if (std::fabs(q.x - c.x) <= half && std::fabs(q.y - c.y) <= half) { end = q; hit = true; break; }
        }
        R::line(start, end, 0.7f, pal(P_CORAL, 0.8f));
        if (hit) R::rect(end.x - 1.5f, end.y - 1.5f, 3, 3, pal(P_CORAL));   // the dot on the wall
        else R::rect(end.x - 1, end.y - 1, 2, 2, pal(P_CORAL));
    }
    for (const Turret& tu : p.turrets) {
        if (tu.beamT <= 0 || indoorHidden(tu.beamEnd)) continue;
        float a = clampf(tu.beamT / 0.14f, 0, 1);
        Vec2 head = turretHead(tu) + fromAngle(tu.angle) * 8.0f;
        R::line(head, tu.beamEnd, 3.0f, pal(P_PINK, 0.35f * a));
        R::line(head, tu.beamEnd, 1.0f, pal(P_WHITE, a));
    }
    for (auto& b : G.bullets) {
        if (indoorHidden(b.pos)) continue;
        Vec2 tail = b.pos - normalize(b.vel) * (b.explosive ? 4.0f : 6.0f);
        if (b.explosive) R::sprite(ROCKET, b.pos, angleOf(b.vel));
        else R::line(tail, b.pos, 1, pal(b.fromPlayer ? P_YELLOW : P_ORANGE));
    }
    for (auto& pt : G.particles) {
        if (!pt.glow || indoorHidden(pt.pos)) continue;
        R::rect(std::floor(pt.pos.x), std::floor(pt.pos.y), pt.size, pt.size, pal(pt.color, clampf(pt.life / pt.maxLife * 2, 0, 1)));
    }
    for (auto& e : G.enemies) {
        if (e.type != EnemyType::Shade || !onScreen(e.pos) || indoorHidden(e.pos)) continue;
        Vec2 f = fromAngle(e.angle), s(-f.y, f.x);
        Vec2 a = e.pos + f * 2.5f + s * 1.2f, b = e.pos + f * 2.5f - s * 1.2f;
        R::rect(std::floor(a.x), std::floor(a.y), 1, 1, pal(P_CORAL));
        R::rect(std::floor(b.x), std::floor(b.y), 1, 1, pal(P_CORAL));
    }
    {
        static const Vec2 FLASH_OFFSET[4] = {{0, 9}, {0, -9}, {9, 2}, {-9, 2}};
        auto flash = [&](Vec2 pos, float angle, float t, int weapon) {
            if (t <= 0 || indoorHidden(pos)) return;
            Art::Dir d = Art::dirFromAngle(angle);
            Art::Piece f = Art::muzzleFlash(d, (int)((0.09f - t) * 33.0f));
            if (f.valid()) R::spriteAt(*f.sprite, f.frame, pos + FLASH_OFFSET[(int)d], R::Pivot::Center, 1);
            (void)weapon;
        };
        flash(G.player.pos, G.player.angle, G.player.flashT, p.weapons[p.curWeapon].id);
        for (auto& e : G.enemies)
            if (e.flashT > 0 && onScreen(e.pos)) flash(e.pos, e.angle, e.flashT, e.weapon);
        for (const Hireling* h : s_mercs)
            if (!h->dead && h->flashT > 0 && onScreen(h->pos)) flash(h->pos, h->angle, h->flashT, hireTier(h->tier).weapon);
        for (const MercView& m : s_mercViews)
            if (m.flash && onScreen(m.pos)) flash(m.pos, m.angle, 0.05f, hireTier(m.tier).weapon);
        if (Coop::active())
            for (int i = 0; i < Coop::MAX_PLAYERS; i++) {
                const Coop::NetPlayer& np = Coop::player(i);
                if (i != mySlot() && np.used && np.where == Coop::W_RAID && np.flashT > 0 && onScreen(np.pos))
                    flash(np.pos, np.angle, np.flashT, np.weapon);
            }
    }
    if (p.timeMin >= 17 * 60 && !G.nightFallen) {
        float pulse = std::fmod(t, 1.5f) / 1.5f;
        R::sprite(RING, hatchPos(), 0, 1 + pulse * 3, pal(P_YELLOW, 1 - pulse));
    }
    R::end();

    // ---- UI layer
    R::begin(R::UI, Vec2());
    drawIndoorView(cam);     // on top of the lit, fogged world, under the HUD
    if (!G.devNoHud) drawHUD();
    if (Local::active()) { drawLocalRaidUi(); UI::endFrame(); R::end(); raidLighting(p, cam); return; }
    drawRaidPanels();
    raidLighting(p, cam);
}

// The panel that is up (the one player's, or in local co-op whoever owns the menu),
// and the aim pointer.
void drawRaidPanels() {
    Profile& p = G.prof;
    switch (G.panel) {
    case Panel::Inventory: drawInventoryPanel(std::floor(R::width() / 2.0f - 70), std::floor(R::height() / 2.0f - 110), InvMode::Raid, nullptr, 0); break;
    case Panel::Loot: drawLootPanel(); break;
    case Panel::Map: drawMapPanel(); break;
    case Panel::CryptIntro: drawCryptIntro(); break;
    case Panel::Mechanic: drawMechanicShop(); break;
    case Panel::MechanicTalk: drawMechanicTalk(); break;
    case Panel::Pause: case Panel::QuitConfirm: drawPausePanel(); break;
    case Panel::Controls: drawControlsPanel(std::floor(R::width() / 2.0f - 110), std::floor(R::height() / 2.0f - 90)); break;
    case Panel::Options: Options::drawPanel((float)R::width(), (float)R::height()); break;
    default: break;
    }
    if (s_deathT >= 0 && !Coop::active()) {
        float W = (float)R::width(), H = (float)R::height();
        R::rect(0, 0, W, H, pal(P_DARK, clampf((3.0f - s_deathT) / 2.0f, 0, 0.75f)));
        R::textCentered(T("YOU DIED"), W / 2, H / 2 - 20, pal(P_CORAL), 3);
        R::textCentered(T(s_deathCause.c_str()), W / 2, H / 2 + 10, pal(P_BEIGE));
    }
    if (G.panel == Panel::None && s_deathT < 0 && s_downT < 0 && glfwGetWindowAttrib(G.window, GLFW_FOCUSED) && !G.devClean) {
        if (Input::usingPad()) {
            // A pointer that circles the player, showing which way the gun faces.
            Vec2 c = toScreen(G.player.pos);
            Vec2 d = fromAngle(G.player.angle);
            for (int i = 0; i < 3; i++) R::rect(std::floor(c.x + d.x * (16 + i * 6)), std::floor(c.y + d.y * (16 + i * 6)), 1, 1, pal(P_WHITE, 0.35f + i * 0.15f));
            R::sprite(ARROW, c + d * 38, G.player.angle, 1, pal(P_YELLOW));
            drawLockOn();
        } else {
            R::sprite(CROSSHAIR, Input::mouse());
        }
    }
    if (G.panel != Panel::None && G.panel != Panel::Map && G.panel != Panel::MechanicTalk) {
        float W = (float)R::width(), H = (float)R::height();
        Prompt::labelCentered(Prompt::Back, T("Close"), W / 2, H - 20, pal(P_LAVENDER));
    }
    (void)p;
    if (Local::active()) return;
    UI::endFrame();
    R::end();
}

// Local co-op (0.12v): every player's pointer in their colour, and the one menu that
// is up, worked by whoever opened it.
void drawLocalRaidUi() {
    float W = (float)R::width(), H = (float)R::height();
    int owner = Local::uiOwner();
    for (int k = 0; k < Local::MAX_SEATS; k++) {
        if (!Local::used(k)) continue;
        Local::with(k, [&] {
            if (s_localOut || G.devClean) return;
            if (G.panel == Panel::None) drawPlayerPrompts(W, H, toScreen(G.player.pos));
            if (s_downT >= 0 || (G.panel != Panel::None && G.panel != Panel::Map)) return;
            Color col = pal(Local::colorOf(k));
            if (Input::usingPad()) {
                Vec2 c = toScreen(G.player.pos);
                Vec2 d = fromAngle(G.player.angle);
                for (int i = 0; i < 3; i++) R::rect(std::floor(c.x + d.x * (16 + i * 6)), std::floor(c.y + d.y * (16 + i * 6)), 1, 1, col.withA(0.35f + i * 0.15f));
                R::sprite(ARROW, c + d * 38, G.player.angle, 1, col);
                drawLockOn();
            } else {
                R::sprite(CROSSHAIR, Input::mouse(), 0, 1, col);
            }
        });
    }
    if (owner >= 0)
        Local::with(owner, [&] {
            std::string who = T1("{0}'s menu", Coop::player(owner).name);
            R::rect(W - 8 - R::textWidth(who) - 8, 4, R::textWidth(who) + 12, 13, pal(P_DARK, 0.85f));
            R::text(who, W - 8 - R::textWidth(who) - 2, 7, pal(Local::colorOf(owner)));
            drawRaidPanels();
        });
    // A map one player has open does not stop the others: it is theirs to close.
    for (int k = 0; k < Local::MAX_SEATS; k++)
        if (Local::used(k) && k != owner) Local::with(k, [&] { if (G.panel == Panel::Map) drawMapPanel(); });
}

// ---- lighting
void raidLighting(Profile& p, Vec2 cam) {
    LightingParams& lp = G.lighting;
    lp.lights.clear();
    // Time of day, the day's mood, the weather and any horde, all in one grade.
    float bright = Atmo::apply(lp, p.timeMin);
    int crypt = localCrypt();
    if (crypt >= 0) {
        // Below ground: no sky, no weather. Torchlight and whatever light you carry.
        lp.ambient = Color(0.30f, 0.27f, 0.34f);
        lp.fog = 0;
        lp.grade = Color(1.0f, 0.95f, 0.9f);
        lp.lift = Color(0.01f, 0.005f, 0.02f);
        bright = 0.1f;
        for (const WorldProp& pr : G.world.props) {
            if (dist(pr.pos, G.player.pos) > 560) continue;
            float flick = 0.9f + 0.1f * std::sin(G.realTime * 11.0f + pr.variant);
            if (pr.kind == PROP_TORCH) lp.lights.push_back({pr.pos + Vec2(0, -6), 118, 0.85f * flick, pal(P_ORANGE)});
            else if (pr.kind == PROP_CANDLE) lp.lights.push_back({pr.pos + Vec2(0, -6), 58, 0.6f * flick, pal(P_YELLOW)});
            else if (pr.kind == PROP_CRYPT_EXIT) lp.lights.push_back({pr.pos + Vec2(0, -20), 60, 0.35f, pal(P_CREAM)});
            else if (pr.kind == PROP_CRYPT_GATE) lp.lights.push_back({pr.pos + Vec2(0, -18), 70, 0.5f * flick, pal(P_ORANGE)});
        }
    }
    if (bright < 0.85f) lp.lights.push_back({G.player.pos, 46 * p.lightMul(), 0.55f, pal(P_CREAM)});
    if (bright < 0.85f && Coop::active())
        for (int i = 0; i < Coop::MAX_PLAYERS; i++) {
            const Coop::NetPlayer& np = Coop::player(i);
            if (i != mySlot() && np.used && np.where == Coop::W_RAID && onScreen(np.pos))
                lp.lights.push_back({np.pos + fromAngle(np.angle) * 20.0f, 70, 0.6f, pal(P_CREAM)});
        }
    if (crypt < 0 && hatchSealed()) {
        // A sealed hatch: its warning lamp pulses red instead of the warm way home.
        float blink = 0.5f + 0.5f * std::sin(G.realTime * 5.0f);
        lp.lights.push_back({hatchPos() + Vec2(0, -8), 60, 0.35f + 0.35f * blink, pal(P_CORAL)});
    } else if (bright < 0.9f && crypt < 0) lp.lights.push_back({hatchPos(), 110, G.nightFallen ? 0.3f : 0.9f, pal(P_YELLOW)});
    // Street lights come on one by one as the sun goes down (each at its own moment,
    // with a stutter as it warms up) and go off again around dawn.
    for (const WorldProp& pr : G.world.props) {
        // The torches on the catacomb arch and the candles at its feet (0.11v).
        if (crypt < 0 && bright < 0.9f && (pr.kind == PROP_CRYPT_DOOR || pr.kind == PROP_CANDLE) && onScreen(pr.pos)) {
            float flick = 0.88f + 0.12f * std::sin(G.realTime * 11.0f + pr.pos.x);
            float k = std::clamp((0.9f - bright) / 0.4f, 0.0f, 1.0f);
            if (pr.kind == PROP_CANDLE) lp.lights.push_back({pr.pos + Vec2(0, -6), 44, 0.5f * flick * k, pal(P_YELLOW)});
            else for (float ox : {-26.0f, 26.0f}) lp.lights.push_back({pr.pos + Vec2(ox, -44), 96, 0.8f * flick * k, pal(P_ORANGE)});
            continue;
        }
        if (pr.kind != PROP_STREETLIGHT || !onScreen(pr.pos)) continue;
        float on = lampOn(p.timeMin, bright, (uint32_t)(pr.pos.x * 7 + pr.pos.y * 13));
        if (on > 0.01f) lp.lights.push_back({pr.pos + Vec2(0, -12), 58, 0.5f * on, pal(P_YELLOW)});
    }
    // Lamps in the building you are in light up the room after dark.
    for (const Building& b : G.world.buildings) {
        if (b.reveal < 0.3f) continue;
        for (int ty = b.y0; ty < b.y0 + b.h; ty++)
            for (int tx = b.x0; tx < b.x0 + b.w; tx++) {
                const Tile& tl = G.world.at(tx, ty);
                if (!tl.furn || tl.furn == FURN_REST || tl.furn > FURN_PIECE_COUNT) continue;
                if (std::strncmp(FURN_PIECES[tl.furn - 1].key, "floorlamp", 9) != 0) continue;
                float on = lampOn(p.timeMin, bright, (uint32_t)(tx * 31 + ty * 17));
                if (on > 0.01f) lp.lights.push_back({World::tileCenter(tx, ty) - Vec2(0, 14), 85, 0.6f * on * b.reveal, pal(P_CREAM)});
            }
    }
    if (bright < 0.8f)
        for (const Turret& tu : p.turrets)
            if (tu.hp > 0 && onScreen(turretPos(tu))) lp.lights.push_back({turretHead(tu), 26, 0.35f, pal(turretDef(tu.type).color)});
    for (auto& f : G.flashes) lp.lights.push_back({f.pos, f.radius, f.intensity * (f.life / f.maxLife), f.color});
    // Headlights on every car with someone at the wheel (0.11v).
    if (bright < 0.85f)
        for (const Car& c : s_cars) {
            if (!c.driven || c.wrecked || !onScreen(c.pos)) continue;
            const CarModel& m = carModel(c.model);
            Vec2 f = fromAngle(c.angle);
            lp.lights.push_back({c.pos + f * (m.halfLen + 26), 58, 0.55f, pal(P_CREAM)});
            lp.lights.push_back({c.pos - f * (m.halfLen + 2), 16, 0.45f, pal(P_CORAL)});
        }
    lp.cone = bright < 0.75f && s_deathT < 0 && s_downT < 0;
    lp.conePos = G.player.pos;
    lp.coneAngle = G.player.angle;
    lp.coneLen = 170 * p.lightMul();
    lp.coneHalfWidth = 0.5f;
    lp.coneIntensity = 0.95f;
    if (Car* rc = carOf(s_ride); rc && driving()) {
        // At the wheel the flashlight is the headlights: longer, a little wider.
        lp.conePos = rc->pos + fromAngle(rc->angle) * carModel(rc->model).halfLen;
        lp.coneAngle = rc->angle;
        lp.coneLen = 250;
        lp.coneHalfWidth = 0.42f;
    }
    lp.coneBlockers.clear();
    if (lp.cone) {
        int r = (int)std::ceil(lp.coneLen / TILE);
        int cx = World::toTile(G.player.pos.x), cy = World::toTile(G.player.pos.y);
        for (int y = cy - r; y <= cy + r && lp.coneBlockers.size() < 96; y++)
            for (int x = cx - r; x <= cx + r && lp.coneBlockers.size() < 96; x++) {
                if (!G.world.inBounds(x, y)) continue;
                const Tile& tile = G.world.at(x, y);
                if (!solidInfo(tile.solid).blocksBullets || tile.solid == S_DOOR_OPEN) continue;
                Vec2 c = World::tileCenter(x, y);
                if (dist(c, G.player.pos) > lp.coneLen + TILE) continue;
                float br = solidInfo(tile.solid).radius;
                float half = br > 0 ? br : TILE * 0.5f;
                lp.coneBlockers.push_back({c.x - half, c.y - half, c.x + half, c.y + half});
            }
    }
    
    G.drawCam = cam;
}


// =================================================================== co-op sync
namespace {
constexpr float NET_RANGE = 520;     // how far from a guest the host bothers describing things
std::vector<uint8_t> s_shadowSolid;  // host: tiles as guests last heard them
std::vector<int16_t> s_shadowHp;
std::vector<uint32_t> s_shadowCont;  // host: container hashes as last sent; guest: as last heard
float s_snapT = 0, s_tileT = 0, s_contT = 0;
int s_lastLoot = -1;                 // guest: the container open last frame

uint32_t containerHash(const Container& c) {
    uint32_t h = 2166136261u;
    auto mix = [&](uint32_t v) { h = (h ^ v) * 16777619u; };
    mix((uint32_t)(c.searched ? 1 : 0) | (c.removed ? 2 : 0));
    mix((uint32_t)(int)(c.pos.x * 4)); mix((uint32_t)(int)(c.pos.y * 4));
    for (const Item& it : c.items) { mix((uint32_t)it.id); mix((uint32_t)it.count); mix((uint32_t)it.data); mix((uint32_t)(uint8_t)it.tier); }
    return h;
}

void writeItems(Net::Writer& w, const std::vector<Item>& items) {
    w.u8((uint8_t)std::min<size_t>(items.size(), 255));
    for (size_t i = 0; i < items.size() && i < 255; i++) { w.i16(items[i].id); w.i16(items[i].count); w.i32(items[i].data); w.u8((uint8_t)items[i].tier); w.u8(items[i].flags); }
}
std::vector<Item> readItems(Net::Reader& r) {
    int n = r.u8();
    std::vector<Item> v(n);
    for (int i = 0; i < n; i++) {
        v[i].id = r.i16();
        v[i].count = r.i16();
        v[i].data = r.i32();
        v[i].tier = (int8_t)r.u8();
        v[i].flags = r.u8();
        if (v[i].id <= IT_NONE || v[i].id >= IT_COUNT || v[i].count <= 0) v[i] = Item();
    }
    return v;
}

void writeContainer(Net::Writer& w, int idx, const Container& c) {
    w.u32((uint32_t)idx);
    w.f32(c.pos.x); w.f32(c.pos.y);
    w.i16((int16_t)c.tx); w.i16((int16_t)c.ty);
    w.u8((uint8_t)c.kind);
    w.u8(c.variant);
    w.u8((uint8_t)((c.searched ? 1 : 0) | (c.removed ? 2 : 0)));
    w.f32(c.searchTime);
    writeItems(w, c.items);
}

void readContainer(Net::Reader& r) {
    int idx = (int)r.u32();
    Container c;
    c.pos.x = r.f32(); c.pos.y = r.f32();
    c.tx = r.i16(); c.ty = r.i16();
    c.kind = r.u8();
    c.variant = r.u8();
    uint8_t f = r.u8();
    c.searched = f & 1;
    c.removed = f & 2;
    c.searchTime = r.f32();
    c.items = readItems(r);
    if (c.items.size() < 12) c.items.resize(12);
    if (r.bad || idx < 0 || idx > 20000) return;
    World& w = G.world;
    if (idx >= (int)w.containers.size()) w.containers.resize(idx + 1);
    // Someone else emptied what I have open: my view follows theirs.
    w.containers[idx] = c;
    if (s_shadowCont.size() < w.containers.size()) s_shadowCont.resize(w.containers.size(), 0);
    s_shadowCont[idx] = containerHash(c);
    if (c.tx >= 0 && w.inBounds(c.tx, c.ty)) w.updateMapPixel(c.tx, c.ty);
}

void netInitShadowsImpl() {
    const World& w = G.world;
    s_shadowSolid.resize(w.tiles.size());
    s_shadowHp.resize(w.tiles.size());
    for (size_t i = 0; i < w.tiles.size(); i++) { s_shadowSolid[i] = w.tiles[i].solid; s_shadowHp[i] = w.tiles[i].hp; }
    s_shadowCont.assign(w.containers.size(), 0);
    for (size_t i = 0; i < w.containers.size(); i++) s_shadowCont[i] = containerHash(w.containers[i]);
}

// Host: everything a guest stepping outside needs that the seed cannot give them.
void sendWorldFull(int slot) {
    if (!s_worldLive) return;
    const World& w = G.world;
    Net::Writer m;
    m.u8(Coop::M_WORLD_FULL);
    m.u16((uint16_t)w.w);
    m.u16((uint16_t)w.h);
    // Tiles as runs: most of the map is the same empty grass over and over.
    size_t i = 0, n = w.tiles.size();
    while (i < n) {
        const Tile& t = w.tiles[i];
        size_t j = i + 1;
        while (j < n && j - i < 65535 && w.tiles[j].solid == t.solid && w.tiles[j].hp == t.hp && w.tiles[j].container == t.container) j++;
        m.u16((uint16_t)(j - i));
        m.u8(t.solid);
        m.i16(t.hp);
        m.i16(t.container);
        i = j;
    }
    m.u32((uint32_t)w.containers.size());
    for (size_t k = 0; k < w.containers.size(); k++) writeContainer(m, (int)k, w.containers[k]);
    Coop::sendReliable(slot, m);
}

void readWorldFull(Net::Reader& r) {
    World& w = G.world;
    int ww = r.u16(), hh = r.u16();
    if (ww != w.w || hh != w.h) return;
    size_t i = 0, n = w.tiles.size();
    while (i < n && !r.bad) {
        int run = r.u16();
        uint8_t solid = r.u8();
        int16_t hp = r.i16(), cont = r.i16();
        for (int k = 0; k < run && i < n; k++, i++) {
            w.tiles[i].solid = solid;
            w.tiles[i].hp = hp;
            w.tiles[i].container = cont;
        }
    }
    uint32_t nc = r.u32();
    w.containers.clear();
    s_shadowCont.clear();
    for (uint32_t k = 0; k < nc && !r.bad; k++) readContainer(r);
    w.rebuildMap();
}

void sendTileChanges() {
    World& w = G.world;
    if (s_shadowSolid.size() != w.tiles.size()) { netInitShadowsImpl(); return; }
    Net::Writer m;
    m.u8(Coop::M_TILES);
    size_t countAt = m.b.size();
    m.u32(0);
    uint32_t count = 0;
    for (size_t i = 0; i < w.tiles.size(); i++) {
        const Tile& t = w.tiles[i];
        if (t.solid == s_shadowSolid[i] && t.hp == s_shadowHp[i]) continue;
        s_shadowSolid[i] = t.solid;
        s_shadowHp[i] = t.hp;
        m.u32((uint32_t)i);
        m.u8(t.solid);
        m.i16(t.hp);
        m.i16(t.container);
        count++;
    }
    if (!count) return;
    std::memcpy(m.b.data() + countAt, &count, 4);
    Coop::broadcast(m, true, Coop::W_RAID);
}

void sendContainerChanges() {
    World& w = G.world;
    if (s_shadowCont.size() < w.containers.size()) s_shadowCont.resize(w.containers.size(), 0);
    Net::Writer m;
    m.u8(Coop::M_CONTAINERS);
    size_t countAt = m.b.size();
    m.u32(0);
    uint32_t count = 0;
    for (size_t i = 0; i < w.containers.size(); i++) {
        uint32_t h = containerHash(w.containers[i]);
        if (h == s_shadowCont[i]) continue;
        s_shadowCont[i] = h;
        writeContainer(m, (int)i, w.containers[i]);
        count++;
    }
    if (!count) return;
    std::memcpy(m.b.data() + countAt, &count, 4);
    Coop::broadcast(m, true, Coop::W_RAID);
}

// A quarter pixel: the bigger map (0.11v) runs past 8192 px, which eighths could not reach.
uint16_t packCoord(float v) { return (uint16_t)clampf(v * 4.0f, 0, 65535); }
float unpackCoord(uint16_t v) { return v / 4.0f; }

// ---- cars on the wire (0.11v)
void writeCar(Net::Writer& w, const Car& c) {
    w.u8((uint8_t)c.owner);
    w.u8((uint8_t)c.model);
    w.u8((uint8_t)c.color);
    w.u16(packCoord(c.pos.x));
    w.u16(packCoord(c.pos.y));
    w.u16((uint16_t)((int)std::lround(c.angle / (2 * PI) * 65536.0f) & 0xFFFF));
    w.i16((int16_t)clampf(c.vel.x * 8.0f, -32000, 32000));
    w.i16((int16_t)clampf(c.vel.y * 8.0f, -32000, 32000));
    w.u16((uint16_t)clampf(c.hp, 0, 65535));
    w.u16((uint16_t)clampf(c.fuel * 100.0f, 0, 65535));
    w.u8((uint8_t)((c.driven ? 1 : 0) | (c.wrecked ? 2 : 0)));
}
// Reads one car and puts it in s_cars (never over my own). `owner` >= 0 overrides the
// sender's word for whose it is (a guest can only speak for their own car).
void readCar(Net::Reader& r, int owner) {
    int o = r.u8();
    int model = std::clamp((int)r.u8(), 0, CAR_MODELS - 1), color = std::clamp((int)r.u8(), 0, CAR_COLORS - 1);
    Vec2 pos{unpackCoord(r.u16()), unpackCoord(r.u16())};
    float angle = r.u16() / 65536.0f * 2 * PI;
    Vec2 vel{r.i16() / 8.0f, r.i16() / 8.0f};
    float hp = (float)r.u16(), fuel = r.u16() / 100.0f;
    uint8_t f = r.u8();
    if (r.bad || (G.scene != Scene::Raid && !(Coop::host() && s_worldLive))) return;
    if (owner >= 0) o = owner;
    if (o < 0 || o >= Coop::MAX_PLAYERS || o == mySlot()) return;
    Car* c = carOf(o);
    bool fresh = !c;
    if (fresh) {
        s_cars.push_back(Car());
        c = &s_cars.back();
        c->owner = o;
    }
    bool wasWrecked = c->wrecked;
    bool swapped = c->model != model;
    c->model = model;
    c->color = color;
    c->netPos = pos;
    c->netAngle = angle;
    c->netT = 0;
    c->vel = vel;
    if (fresh || swapped) { c->pos = pos; c->angle = angle; }
    if (hp < c->hp - 0.5f) c->hurtT = 0.12f;
    c->hp = hp;
    c->fuel = fuel;
    c->driven = (f & 1) != 0;
    c->wrecked = (f & 2) != 0;
    if (c->wrecked && !wasWrecked && !fresh) {
        // Their car went up: the host's world takes the blast; everyone sees it.
        c->pos = pos;
        if (Coop::host()) { s_dmgOwner = -1; explode(pos, 44, 38, false, false); }
        else explodeFx(pos, 44, false);
    }
}
uint8_t packAngle(float a) { return (uint8_t)((int)std::round(a / (2 * PI) * 256.0f) & 255); }
float unpackAngle(uint8_t a) { return a / 256.0f * 2 * PI; }

// Host: what one guest can see - enemies, turrets and mercs around them.
void sendSnapshot(int slot) {
    Vec2 at = Coop::player(slot).target;
    Net::Writer m;
    m.u8(Coop::M_ENEMIES);
    size_t countAt = m.b.size();
    m.u16(0);
    uint16_t n = 0;
    for (const Enemy& e : G.enemies) {
        if (e.dead || n >= 400) continue;
        if (lengthSq(e.pos - at) > NET_RANGE * NET_RANGE && (e.type != EnemyType::Zombie || e.roamer)) continue;
        m.u32(e.netId);
        m.u8((uint8_t)e.type);
        m.u8(e.type == EnemyType::Zombie ? (uint8_t)(e.zkind | (e.roamer ? 0x80 : 0) | (e.noAxe ? 0x40 : 0) | (e.takeT >= 0 ? 0x20 : 0)) : e.artVariant);
        uint8_t f = (e.hurtT > 0 ? 1 : 0) | (e.flashT > 0 ? 2 : 0) | (e.reloadT > 0 ? 4 : 0) |
                    (e.state == AIState::Alert ? 8 : 0) | (e.meleeCd > 0.35f ? 16 : 0);
        m.u8(f);
        m.u16(packCoord(e.pos.x));
        m.u16(packCoord(e.pos.y));
        m.u8(packAngle(e.angle));
        m.u8((uint8_t)clampf(e.hp / std::max(1.0f, e.maxHp) * 255.0f, 0, 255));
        m.u8((uint8_t)e.weapon);
        n++;
    }
    std::memcpy(m.b.data() + countAt, &n, 2);
    const auto& tur = G.prof.turrets;
    m.u8((uint8_t)tur.size());
    for (const Turret& t : tur) {
        m.u8(packAngle(t.angle));
        m.u8((uint8_t)((t.flashT > 0 ? 1 : 0) | (t.beamT > 0 ? 2 : 0)));
        m.u16(packCoord(t.beamEnd.x));
        m.u16(packCoord(t.beamEnd.y));
    }
    uint8_t mc = 0;
    size_t mcAt = m.b.size();
    m.u8(0);
    for (const Hireling* h : s_mercs) {
        if (h->dead || mc >= 24 || lengthSq(h->pos - at) > NET_RANGE * NET_RANGE) continue;
        m.u8((uint8_t)h->owner);
        m.u8((uint8_t)h->tier);
        m.u8((uint8_t)((h->guard ? 1 : 0) | (h->flashT > 0 ? 2 : 0) | (h->reloadT > 0 ? 4 : 0) | (h->hurtT > 0 ? 8 : 0) | (h->rideCar >= 0 ? 16 : 0)));
        m.str(h->name);
        m.u16(packCoord(h->pos.x));
        m.u16(packCoord(h->pos.y));
        m.u8(packAngle(h->angle));
        m.u8((uint8_t)clampf(h->hp / h->maxHp() * 255.0f, 0, 255));
        mc++;
    }
    m.b[mcAt] = mc;
    Coop::sendUnreliable(slot, m);
}

void readSnapshot(Net::Reader& r) {
    int n = r.u16();
    std::vector<Enemy> next;
    next.reserve(n);
    for (int i = 0; i < n && !r.bad; i++) {
        uint32_t id = r.u32();
        EnemyType type = (EnemyType)std::clamp((int)r.u8(), 0, 5);
        uint8_t variant = r.u8();
        uint8_t f = r.u8();
        Vec2 pos{unpackCoord(r.u16()), unpackCoord(r.u16())};
        float angle = unpackAngle(r.u8());
        float hpFrac = r.u8() / 255.0f;
        int weapon = r.u8();
        Enemy e;
        for (const Enemy& old : G.enemies)
            if (old.netId == id) { e = old; break; }
        bool fresh = e.netId != id;
        e.netId = id;
        e.type = type;
        if (type == EnemyType::Zombie) {
            e.zkind = variant & 3; e.artVariant = (uint8_t)(variant & 3); e.roamer = (variant & 0x80) != 0;
            e.noAxe = (variant & 0x40) != 0;
            if ((variant & 0x20) && e.takeT < 0) e.takeT = 0.8f;
            if (!(variant & 0x20)) e.takeT = -1;
        }
        else e.artVariant = variant;
        e.home = pos;
        if (fresh) e.pos = e.lastPos = pos;
        e.angle = angle;
        e.maxHp = 100;
        e.hp = hpFrac * 100;
        e.weapon = weapon;
        if (f & 1) e.hurtT = std::max(e.hurtT, 0.1f);
        if (f & 2) e.flashT = std::max(e.flashT, 0.05f);
        e.reloadT = (f & 4) ? 1.0f : 0.0f;
        e.state = (f & 8) ? AIState::Alert : AIState::Combat;
        if (f & 16) e.meleeCd = std::max(e.meleeCd, 0.5f);
        next.push_back(e);
    }
    if (r.bad) return;
    G.enemies.swap(next);
    int nt = r.u8();
    for (int i = 0; i < nt && !r.bad; i++) {
        float a = unpackAngle(r.u8());
        uint8_t f = r.u8();
        Vec2 be{unpackCoord(r.u16()), unpackCoord(r.u16())};
        if (i >= (int)G.prof.turrets.size()) continue;
        Turret& t = G.prof.turrets[i];
        t.angle = a;
        if (f & 1) t.flashT = 0.08f;
        if (f & 2) { t.beamT = 0.14f; t.beamEnd = be; }
        if ((f & 1) && t.type == TT_FLAME) {
            Vec2 head = turretHead(t), fw = fromAngle(t.angle);
            for (int k = 0; k < 2; k++) {
                Particle pt;
                pt.pos = head + fw * 8.0f;
                pt.vel = fromAngle(t.angle + s_rng.range(-0.35f, 0.35f)) * s_rng.range(60.0f, 110.0f);
                pt.life = pt.maxLife = s_rng.range(0.3f, 0.5f);
                pt.color = s_rng.chance(0.5f) ? P_ORANGE : P_YELLOW;
                pt.glow = true;
                pt.drag = 2.5f;
                G.particles.push_back(pt);
            }
        }
    }
    int mc = r.u8();
    for (MercView& v : s_mercViews) v.seen += 0.0f;
    for (int i = 0; i < mc && !r.bad; i++) {
        int owner = r.u8(), tier = r.u8();
        uint8_t f = r.u8();
        std::string name = r.str();
        Vec2 pos{unpackCoord(r.u16()), unpackCoord(r.u16())};
        float angle = unpackAngle(r.u8());
        float hp = r.u8() / 255.0f;
        if (r.bad) break;
        MercView* v = nullptr;
        for (MercView& m : s_mercViews) if (m.owner == owner && m.name == name) v = &m;
        if (!v) {
            s_mercViews.push_back(MercView{owner, name, tier, false, pos, pos, pos, angle, hp, false, false, false, 0});
            v = &s_mercViews.back();
        }
        v->tier = tier;
        v->guard = f & 1;
        v->flash = f & 2;
        v->reload = f & 4;
        v->hurt = f & 8;
        v->ride = (f & 16) != 0;
        v->target = pos;
        v->angle = angle;
        v->hp = hp;
        v->seen = 0;
        // Keep my own roster's health in step with the fight.
        if (owner == mySlot())
            for (Hireling& h : G.prof.squad) if (h.name == name) h.hp = hp * h.maxHp();
    }
}

void spawnCosmeticShot(const ShotRec& sr) {
    const WeaponDef* wd = weaponDef(sr.weapon);
    int pellets = wd ? wd->pellets : 1;
    float speed = wd ? wd->bulletSpeed : ((sr.flags & SF_EXPLOSIVE) ? 260.0f : 560.0f);
    float range = wd ? wd->range : 400.0f;
    float spread = wd ? wd->spread : 0.03f;
    for (int i = 0; i < pellets; i++) {
        Bullet b;
        b.pos = sr.origin;
        float a = sr.angle + (pellets > 1 ? spread * (s_rng.f() * 2 - 1) : 0);
        b.vel = fromAngle(a) * speed;
        b.rangeLeft = range;
        b.fromPlayer = sr.flags & SF_PLAYER;
        b.turret = sr.flags & SF_TURRET;
        b.explosive = sr.flags & SF_EXPLOSIVE;
        b.weapon = sr.weapon;
        b.owner = sr.owner;
        b.cosmetic = true;
        b.noTiles = true;
        G.bullets.push_back(b);
    }
    if (!(sr.flags & SF_TURRET) && dist(sr.origin, G.player.pos) < 400 && wd)
        sfxAt(wd->sound, sr.origin, G.player.pos, 0.45f, s_rng.range(0.9f, 1.05f));
}

void readShotList(Net::Reader& r, std::vector<ShotRec>& out) {
    int n = r.u16();
    for (int i = 0; i < n && !r.bad; i++) {
        ShotRec sr;
        sr.origin.x = r.f32(); sr.origin.y = r.f32();
        sr.angle = r.f32();
        sr.weapon = r.u8();
        sr.flags = r.u8();
        sr.owner = (int8_t)r.u8();
        if (!r.bad) out.push_back(sr);
    }
}

void writeShotList(Net::Writer& w, const std::vector<const ShotRec*>& shots) {
    w.u16((uint16_t)shots.size());
    for (const ShotRec* sr : shots) {
        w.f32(sr->origin.x); w.f32(sr->origin.y);
        w.f32(sr->angle);
        w.u8(sr->weapon);
        w.u8(sr->flags);
        w.u8((uint8_t)sr->owner);
    }
}
}  // namespace

// The body of the forward-declared helpers from the top of the file.
namespace {
void netEnemyDied(const Enemy& e) {
    if (!Coop::anyGuestOutside()) return;
    Net::Writer w;
    w.u8(Coop::M_ENEMY_DIED);
    w.u32(e.netId);
    w.u8((uint8_t)e.type);
    w.u8((uint8_t)e.zkind);
    w.f32(e.pos.x); w.f32(e.pos.y);
    w.f32(e.angle);
    Coop::broadcast(w, true, Coop::W_RAID);
}

void netExplosion(Vec2 pos, float radius, bool harmless) {
    if (!Coop::anyGuestOutside()) return;
    Net::Writer w;
    w.u8(Coop::M_EXPLOSION);
    w.f32(pos.x); w.f32(pos.y);
    w.f32(radius);
    w.u8(harmless ? 1 : 0);
    Coop::broadcast(w, true, Coop::W_RAID);
}

void netInitShadows() { netInitShadowsImpl(); }

// More players, more raiders, by Coop::enemyScale (1-2 players: none extra, then
// steps at 3, 5, 7 and 8). Their damage stays the same. Topped up when someone
// joins; never taken away when they leave.
void scaleRaiders() {
    if (!Coop::host() || !s_worldLive) return;
    int players = Coop::count();
    if (players <= s_scaledFor) return;
    s_scaledFor = players;
    const auto& spawns = G.world.spawns;
    if (spawns.empty()) return;
    int want = (int)std::round(spawns.size() * (Coop::enemyScale(s_worldKey) - 1.0f));
    Rng r(mix64(s_worldKey ^ (uint64_t)players * 0x51ED));
    for (; s_scaledExtra < want; s_scaledExtra++) {
        const EnemySpawn& sp = spawns[r.next() % spawns.size()];
        Vec2 pos = sp.pos + Vec2{r.range(-20, 20), r.range(-20, 20)};
        if (G.world.collides(pos.x, pos.y, ENEMY_R)) pos = sp.pos;
        // Only near the living: a spawn someone has already cleared stays clear.
        bool cleared = true;
        for (const Enemy& e : G.enemies) if (!e.dead && dist(e.pos, sp.pos) < 200) { cleared = false; break; }
        if (cleared) continue;
        if (sp.type == EnemyType::Zombie) spawnRoamer(pos, roamerKind(r.next()));
        else spawnEnemy(sp.type, pos);
    }
}
}  // namespace

bool raid_worldLive() { return s_worldLive; }
int raid_localCrypt() { return G.scene == Scene::Raid ? localCrypt() : -1; }
// Dev (--crypt[=seen]): straight down into today's first catacomb.
void raid_devCrypt(bool introSeen, bool atDoor, int gate) {
    if (G.world.dungeons.empty()) { std::fprintf(stderr, "[dev] no catacombs today\n"); return; }
    G.prof.cryptIntroSeen = introSeen;
    enterCrypt(0, !atDoor);
    const Dungeon& d = G.world.dungeons[0];
    std::fprintf(stderr, "[dev] gate: %s at %d,%d dir %d\n", d.hasGate() ? "yes" : "NO", d.gateX, d.gateY, d.gateDir);
    if (gate && d.hasGate()) {
        G.player.pos = d.gateFront() + Vec2(0, (gate != 2 ? 14.0f : -58.0f) * d.gateDir);
        G.player.angle = d.gateDir > 0 ? -PI / 2 : PI / 2;
        G.cam = G.player.pos - Vec2(R::viewW() / 2.0f, R::viewH() / 2.0f);
        G.world.reveal(G.player.pos, 12);
        for (Enemy& e : G.enemies) if (dist(e.pos, G.player.pos) < 400) e.dead = true;
        if (gate == 3) raiseGate(0);
    }
    std::fprintf(stderr, "[dev] player %.0f,%.0f exit %.0f,%.0f\n", G.player.pos.x, G.player.pos.y, d.exit.x, d.exit.y);
    std::fprintf(stderr, "[dev] catacombs: %zu, first at %d,%d\n", G.world.dungeons.size(), G.world.dungeons[0].x0, G.world.dungeons[0].y0);
}
bool raid_hordeOn() { return s_hordeActive; }
// Dev (--bloodpool): a pool spreading at your feet, and a finished one beside it.
// Dev (0.11v): put yourself in your car, in a city, upstairs, or at the mechanic's.
void raid_devCars(const std::string& what) {
    auto snap = [] { G.cam = G.player.pos - Vec2(R::viewW() / 2.0f, R::viewH() / 2.0f); G.world.reveal(G.player.pos, 20); };
    if (what == "--atgarage" || what == "--panel=mechanic") {
        G.player.pos = mechanicHome() + Vec2(0, 16);
        if (what == "--panel=mechanic") { s_mech = MECH_IDLE; s_mechPos = mechanicHome(); G.panel = Panel::Mechanic; }
        snap();
    } else if (what == "--driving") {
        if (Car* c = myCar()) { G.player.pos = c->pos; enterCar(mySlot()); snap(); }
    } else if (what == "--atcity" && !G.world.cities.empty()) {
        // The nearest city's first crossing, in the middle of the street.
        const CityZone* best = &G.world.cities[0];
        for (const CityZone& c : G.world.cities)
            if (dist(World::tileCenter(c.x0, c.y0), G.world.homePos) < dist(World::tileCenter(best->x0, best->y0), G.world.homePos)) best = &c;
        Vec2 at = World::tileCenter(best->x0 + 2, best->y0 + 2) + Vec2(24, 24);
        if (Car* c = myCar()) {
            c->pos = at + Vec2(40, 0);
            c->angle = 0;
            if (s_ride >= 0) G.player.pos = c->pos;
            else G.player.pos = at;
        } else G.player.pos = at;
        snap();
    } else if (what == "--atfloor" && !G.world.stairs.empty()) {
        for (int i = 0; i < (int)G.world.stairs.size(); i++)
            if (G.world.stairs[i].up) { if (s_ride >= 0) leaveCar(true); useStairs(i); break; }
    }
}

void raid_devPool() {
    spawnBloodPool(G.player.pos + Vec2(0, 9), 1.0f);
    spawnBloodPool(G.player.pos + Vec2(34, 9), 1.0f);
    if (!s_pools.empty()) s_pools.back().t = 99;
}

void raid_coopTick(float dt) {
    if (!s_worldLive) return;
    s_sim = true;
    updateTime(0);
    worldSim(dt);
    updateBullets(dt);
    updateGrenades(dt);
    s_sim = false;
    G.particles.clear();
    if (!s_hordeActive && absMinutes() >= G.prof.nextHordeAt) launchHorde();
}

std::string raid_coopOffscreenHorde() {
    Profile& p = G.prof;
    uint64_t seed = todaySeed();
    if (!(s_worldLive && s_worldKey == seed)) {
        // Nobody has been out today: stand the world up just for the fight.
        s_rng = Rng(seed ^ 0x51EE9);
        World::zombieSpawns = p.zombieMode();
        World::withCrypts = true;
        G.world.generate(seed, p.day);
        placeTurretsInWorld(G.world);
    s_gateT.clear();
    s_axes.clear();
    s_casings.clear();
        G.enemies.clear();
        G.bullets.clear();
        s_worldLive = false;
    }
    if (p.baseHp < 0 || p.baseHp > baseMaxHp()) p.baseHp = baseMaxHp();
    s_hordeNote.clear();
    s_sim = true;
    buildTargets();
    rebuildMercs();
    launchHorde();
    s_sim = false;
    resolveHordeOffscreen();
    if (!s_worldLive) { G.enemies.clear(); G.particles.clear(); }
    return s_hordeNote;
}

void raid_coopPlayersChanged() {
    scaleRaiders();
    // A car whose owner has left the session goes with them (0.11v).
    s_cars.erase(std::remove_if(s_cars.begin(), s_cars.end(), [](const Car& c) {
        return c.owner != mySlot() && (c.owner < 0 || c.owner >= Coop::MAX_PLAYERS || !Coop::player(c.owner).used);
    }), s_cars.end());
}

void raid_coopEnded() {
    s_worldLive = false;
    s_downT = -1;
    s_mercViews.clear();
    s_shotLog.clear();
    s_hordeActive = false;
    s_hordePending = 0;
    G.enemies.clear();
    G.bullets.clear();
    G.grenades.clear();
}

void raid_hordeState(bool& active, int& n, int& left) {
    active = s_hordeActive;
    n = s_hordeN;
    left = s_hordePending;
    for (const Enemy& e : G.enemies) if (!e.dead && e.type == EnemyType::Zombie && !e.roamer) left++;
}

void raid_setHordeState(bool active, int n, int left, bool nightFallen) {
    if (active && !s_hordeActive && G.scene == Scene::Raid) sfx(Snd::warning, 1.0f, 0.7f);
    s_hordeActive = active;
    s_hordeN = n;
    s_hordeLeftNet = left;
    G.nightFallen = nightFallen;
}

void raid_forceHome() {
    if (G.scene != Scene::Raid) return;
    s_downT = -1;
    G.prof.inRaid = false;
    base_enter(false);
}

bool raid_localDowned(float& bleedLeft) {
    bleedLeft = std::max(0.0f, s_downT);
    return s_downT >= 0 && G.scene == Scene::Raid;
}

void raid_bigText(const std::string& text, int color) { bigText(text, color, 4); }

void raid_netMessage(int slot, uint8_t type, Net::Reader& r) {
    using namespace Coop;
    // A local seat's own messages are read the way a guest reads them (0.12v).
    if (host() && !s_localDeliver) {
        switch (type) {
        case M_WANT_FULL: sendWorldFull(slot); break;
        case M_SHOTS: {
            std::vector<ShotRec> shots;
            readShotList(r, shots);
            for (ShotRec& sr : shots) {
                sr.owner = (int8_t)slot;
                if (sr.flags & SF_EXPLOSIVE) {
                    // A guest's rocket is real here: it hurts what it hits and breaks walls.
                    if (const WeaponDef* wd = weaponDef(sr.weapon)) {
                        s_shotOwner = slot;
                        bool was = s_sim;
                        s_sim = G.scene != Scene::Raid;
                        size_t before = G.bullets.size();
                        spawnBullets(sr.origin, sr.angle, *wd, sr.weapon, 0.3f, 0, true, 1.0f);
                        s_sim = was;
                        s_shotOwner = -1;
                        (void)before;
                    }
                } else {
                    spawnCosmeticShot(sr);
                    // The copy breaks walls here, since the host owns the ground.
                    for (size_t k = G.bullets.size(); k-- > 0;) {
                        Bullet& b = G.bullets[k];
                        if (!b.cosmetic || b.owner != slot) break;
                        b.noTiles = false;
                        if (const WeaponDef* wd = weaponDef(sr.weapon)) { b.damage = wd->damage; b.tileMul = wd->tileDamage; }
                    }
                    s_shotLog.push_back(sr);
                }
                alertEnemies(sr.origin, baseWeapon(sr.weapon) == IT_SNIPER ? 550.0f : 380.0f);
            }
            break;
        }
        case M_HIT: {
            uint32_t id = r.u32();
            float dmg = r.f32();
            Vec2 dir{r.f32(), r.f32()};
            if (r.bad) break;
            dmg = std::min(dmg, 400.0f);
            for (Enemy& e : G.enemies)
                if (e.netId == id && !e.dead) {
                    s_dmgOwner = slot;
                    damageEnemy(e, dmg, dir);
                    s_dmgOwner = -1;
                    break;
                }
            break;
        }
        case M_GRENADE: {
            Grenade g;
            g.pos.x = r.f32(); g.pos.y = r.f32(); g.vel.x = r.f32(); g.vel.y = r.f32(); g.fuse = r.f32();
            r.i32();
            if (r.bad) break;
            g.owner = slot;
            G.grenades.push_back(g);
            Net::Writer w;
            w.u8(M_GRENADE);
            w.f32(g.pos.x); w.f32(g.pos.y); w.f32(g.vel.x); w.f32(g.vel.y); w.f32(g.fuse);
            w.i32(slot);
            for (int i = 1; i < MAX_PLAYERS; i++)
                if (i != slot && player(i).used && player(i).where == W_RAID) sendReliable(i, w);
            break;
        }
        case M_DOOR: {
            Vec2 pos{r.f32(), r.f32()};
            bool open = r.u8() != 0;
            if (r.bad) break;
            int gate = open ? G.world.gateNear(pos, 40) : -1;
            if (gate >= 0) raiseGate(gate);
            else if (open) G.world.openDoorNear(pos, INTERACT_RANGE);
            else G.world.closeDoorNear(pos, INTERACT_RANGE);
            break;
        }
        case M_CONTAINER_SET: {
            int idx = (int)r.u32();
            uint8_t f = r.u8();
            std::vector<Item> items = readItems(r);
            if (r.bad || idx < 0 || idx >= (int)G.world.containers.size()) break;
            Container& c = G.world.containers[idx];
            if (items.size() < 12) items.resize(12);
            c.items = items;
            c.searched = c.searched || (f & 1);
            c.removed = c.removed || (f & 2);
            break;
        }
        case M_DROP: {
            Vec2 pos{r.f32(), r.f32()};
            int kind = r.u8();
            std::vector<Item> items = readItems(r);
            if (r.bad || !s_worldLive) break;
            int id = G.world.addContainer(pos, kind == CK_CORPSE ? CK_CORPSE : CK_BAG, -1, -1, (uint8_t)s_rng.next());
            Container& c = G.world.containers[id];
            c.searched = kind != CK_CORPSE;
            c.searchTime = 0.9f;
            for (const Item& it : items) addToSlots(c.items, it);
            break;
        }
        case M_CAR: readCar(r, slot); break;
        case M_CAR_SMASH: {
            // A guest's car went through these: the ground is the host's to break.
            int n = r.u8();
            for (int k = 0; k < n && !r.bad; k++) {
                int tx = r.u16(), ty = r.u16();
                if (r.bad || !G.world.inBounds(tx, ty)) break;
                int s = G.world.at(tx, ty).solid;
                if (s == S_FENCE || s == S_BUSH || s == S_CRATE || s == S_DOOR || s == S_SANDBAG || s == S_WALL_WOOD) G.world.destroyTile(tx, ty);
            }
            break;
        }
        case M_PVP_HIT: {
            int target = r.u8();
            float dmg = r.f32();
            if (r.bad || !rivalsOn() || target < 0 || target >= Coop::MAX_PLAYERS || target == slot) break;
            hurtSlot(target, dmg, "Shot by another player.", slot);
            break;
        }
        case M_PVP_KILL: {
            int killer = r.u8(), victim = r.u8();
            if (!r.bad && victim == slot) announcePvpKill(killer, victim);
            break;
        }
        case M_REVIVE_REQ: {
            int target = r.u8();
            if (r.bad) break;
            if (target == mySlot()) reviveLocal();
            else {
                Net::Writer w;
                w.u8(M_REVIVE);
                sendReliable(target, w);
            }
            break;
        }
        default: break;
        }
        return;
    }
    // ---- guest
    switch (type) {
    case M_WORLD_FULL: if (G.scene == Scene::Raid) readWorldFull(r); break;
    case M_TILES: {
        uint32_t n = r.u32();
        World& w = G.world;
        for (uint32_t k = 0; k < n && !r.bad; k++) {
            uint32_t i = r.u32();
            uint8_t solid = r.u8();
            int16_t hp = r.i16(), cont = r.i16();
            if (r.bad || i >= w.tiles.size() || G.scene != Scene::Raid) continue;
            Tile& t = w.tiles[i];
            if (t.solid != S_NONE && solid == S_NONE && solidInfo(t.solid).hp > 0) {
                Vec2 c = World::tileCenter((int)(i % w.w), (int)(i / w.w));
                if (onScreen(c)) addParticles(c, 6, solidInfo(t.solid).mapColor, 20, 80, 0.3f, 0.7f, false, 2);
            }
            t.solid = solid;
            t.hp = hp;
            t.container = cont;
            w.updateMapPixel((int)(i % w.w), (int)(i / w.w));
        }
        break;
    }
    case M_CONTAINERS: {
        uint32_t n = r.u32();
        for (uint32_t k = 0; k < n && !r.bad; k++) readContainer(r);
        break;
    }
    case M_ENEMIES: if (G.scene == Scene::Raid) readSnapshot(r); break;
    case M_ENEMY_DIED: {
        uint32_t id = r.u32();
        EnemyType t = (EnemyType)std::clamp((int)r.u8(), 0, 5);
        int zkind = r.u8();
        Vec2 pos{r.f32(), r.f32()};
        float angle = r.f32();
        if (r.bad || G.scene != Scene::Raid) break;
        G.enemies.erase(std::remove_if(G.enemies.begin(), G.enemies.end(), [id](const Enemy& e) { return e.netId == id; }), G.enemies.end());
        sfxAt(Snd::enemy_die, pos, G.player.pos, 0.7f, s_rng.range(0.8f, 1.2f));
        if (t == EnemyType::Shade) { addParticles(pos, 14, P_PURPLE, 10, 50, 0.4f, 0.9f, false, 2); break; }
        addParticles(pos, 14, P_CORAL, 20, 80, 0.3f, 0.8f, false, 1);
        G.decals.push_back({pos, s_rng.chance(0.5f) ? BLOOD0 : BLOOD1, s_rng.range(0, 6.28f)});
        spawnBloodPool(pos);
        if (t == EnemyType::Zombie) {
            if (s_zCorpses.size() > 160) s_zCorpses.erase(s_zCorpses.begin());
            s_zCorpses.push_back({pos, std::clamp(zkind, 0, 2), std::cos(angle) < 0, 0, (int)(s_rng.next() & 1), false});
        }
        break;
    }
    case M_SHOTS: {
        std::vector<ShotRec> shots;
        readShotList(r, shots);
        if (G.scene != Scene::Raid) break;
        for (const ShotRec& sr : shots)
            if (sr.owner != mySlot()) spawnCosmeticShot(sr);
        break;
    }
    case M_GRENADE: {
        Grenade g;
        g.pos.x = r.f32(); g.pos.y = r.f32(); g.vel.x = r.f32(); g.vel.y = r.f32(); g.fuse = r.f32();
        g.owner = r.i32();
        if (r.bad || G.scene != Scene::Raid || g.owner == mySlot()) break;
        g.cosmetic = true;
        G.grenades.push_back(g);
        break;
    }
    case M_EXPLOSION: {
        Vec2 pos{r.f32(), r.f32()};
        float radius = r.f32();
        bool harmless = r.u8() != 0;
        if (!r.bad && G.scene == Scene::Raid) explodeFx(pos, radius, harmless);
        break;
    }
    case M_DAMAGE: {
        float dmg = r.f32();
        std::string cause = r.str();
        int attacker = (int)r.u8() - 1;
        if (r.bad || G.scene != Scene::Raid) break;
        if (attacker >= 0) { s_pvpAttacker = attacker; s_pvpAttackT = 8; }
        damagePlayer(dmg, cause);
        break;
    }
    case M_REWARD: {
        int money = r.i32(), etype = r.i32();
        bool kill = r.u8() != 0;
        if (r.bad) break;
        Profile& p = G.prof;
        p.money += money;
        p.earned += money;
        if (kill) {
            p.kills++;
            G.raidKills++;
            if (etype >= 0) missionAddKill(etype);
        }
        if (money > 0 && G.scene == Scene::Raid)
            G.floatTexts.push_back({G.player.pos + Vec2(0, -18), "+$" + std::to_string(money), P_YGREEN, 0.9f});
        if (!kill && money > 0) pushMessage(T1("Defense bonus: ${0}", std::to_string(money)), P_YGREEN);
        break;
    }
    case M_MERC_DIED: {
        std::string name = r.str();
        if (r.bad) break;
        auto& sq = G.prof.squad;
        sq.erase(std::remove_if(sq.begin(), sq.end(), [&](const Hireling& h) { return h.name == name; }), sq.end());
        bigText(T1("{0} IS DEAD", name), P_CORAL, 3);
        pushMessage(T1("{0} has been killed.", name), P_CORAL);
        save_game();
        break;
    }
    case M_CAR: {
        int n = r.u8();
        for (int k = 0; k < n && !r.bad; k++) readCar(r, -1);
        break;
    }
    case M_CAR_DMG: {
        float dmg = r.f32();
        if (r.bad) break;
        if (G.scene == Scene::Raid) {
            if (Car* c = myCar()) damageCar(*c, dmg);
        } else if (G.prof.activeCar >= 0 && G.prof.cars[G.prof.activeCar].owned) {
            // Parked out there while I am underground: it still gets hit.
            Profile::OwnedCar& oc = G.prof.cars[G.prof.activeCar];
            float hp = oc.hp < 0 ? carModel(G.prof.activeCar).hp : oc.hp;
            oc.hp = std::max(0.0f, hp - dmg);
        }
        break;
    }
    case M_REVIVE: reviveLocal(); break;
    case M_OVERRUN: {
        int lost = G.prof.money / 4;
        G.prof.money -= lost;
        pushMessage(T1("They ransacked it: -${0}", std::to_string(lost)), P_CORAL);
        save_game();
        break;
    }
    default: break;
    }
}

void raid_netSend(float dt) {
    using namespace Coop;
    s_carSendT -= dt;
    if (host()) {
        if (!s_worldLive || !anyGuestOutside()) { s_shotLog.clear(); return; }
        // Every car in the world, to everyone outside (they skip their own).
        if (s_carSendT <= 0 && !s_cars.empty()) {
            s_carSendT = 1.0f / 12.0f;
            Net::Writer w;
            w.u8(M_CAR);
            w.u8((uint8_t)std::min<size_t>(s_cars.size(), 255));
            for (size_t i = 0; i < s_cars.size() && i < 255; i++) writeCar(w, s_cars[i]);
            broadcast(w, false, W_RAID);
        }
        s_snapT -= dt;
        if (s_snapT <= 0) {
            s_snapT = 1.0f / 15.0f;
            for (int i = 1; i < MAX_PLAYERS; i++) {
                const NetPlayer& np = player(i);
                if (!np.used || np.where != W_RAID) continue;
                sendSnapshot(i);
                std::vector<const ShotRec*> near;
                for (const ShotRec& sr : s_shotLog)
                    if (sr.owner != i && lengthSq(sr.origin - np.target) < NET_RANGE * NET_RANGE) near.push_back(&sr);
                if (!near.empty()) {
                    Net::Writer w;
                    w.u8(M_SHOTS);
                    writeShotList(w, near);
                    sendUnreliable(i, w);
                }
            }
            s_shotLog.clear();
        }
        s_tileT -= dt;
        if (s_tileT <= 0) { s_tileT = 0.15f; sendTileChanges(); }
        s_contT -= dt;
        if (s_contT <= 0) { s_contT = 0.25f; sendContainerChanges(); }
        return;
    }
    if (!guest() || G.scene != Scene::Raid) { s_shotLog.clear(); return; }
    // My own car, to the host (who passes it on): often while it moves, now and then parked.
    if (Car* c = myCar(); c && s_carSendT <= 0) {
        s_carSendT = c->driven || c->speed() > 1 ? 1.0f / 15.0f : 0.5f;
        Net::Writer w;
        w.u8(M_CAR);
        writeCar(w, *c);
        toHost(w, false);
    }
    if (!s_shotLog.empty()) {
        std::vector<const ShotRec*> mine;
        for (const ShotRec& sr : s_shotLog) mine.push_back(&sr);
        Net::Writer w;
        w.u8(M_SHOTS);
        writeShotList(w, mine);
        toHost(w, true);
        s_shotLog.clear();
    }
    // Whatever I did to the container I have open (or just closed) goes to the host.
    auto check = [](int idx) {
        if (idx < 0 || idx >= (int)G.world.containers.size()) return;
        if (s_shadowCont.size() < G.world.containers.size()) s_shadowCont.resize(G.world.containers.size(), 0);
        const Container& c = G.world.containers[idx];
        uint32_t h = containerHash(c);
        if (h == s_shadowCont[idx]) return;
        s_shadowCont[idx] = h;
        Net::Writer w;
        w.u8(M_CONTAINER_SET);
        w.u32((uint32_t)idx);
        w.u8((uint8_t)((c.searched ? 1 : 0) | (c.removed ? 2 : 0)));
        writeItems(w, c.items);
        toHost(w, true);
    };
    check(G.lootContainer);
    if (s_lastLoot != G.lootContainer) check(s_lastLoot);
    s_lastLoot = G.lootContainer;
}

// ---- other players' characters
void drawNetPlayers(uint8_t where) {
    if (!Coop::active()) return;
    for (int i = 0; i < Coop::MAX_PLAYERS; i++) {
        const Coop::NetPlayer& np = Coop::player(i);
        if (i == mySlot() || !np.used || np.where != where) continue;
        if (where == Coop::W_RAID && np.ride >= 0) continue;   // inside a car
        int shirt = Coop::shirtOf(i);
        if (np.downed) {
            Art::Piece dead = Art::humanBody(Art::dirFromAngle(np.angle), Art::Anim::Death, 99, false, false, shirt);
            if (dead.valid()) sceneAdd(dead, np.pos + Vec2(0, 8));
            continue;
        }
        bool moving = np.moving || lengthSq(np.pos - np.lastPos) > 0.02f;
        drawCharacter(PLAYER, np.pos, np.angle, where == Coop::W_RAID ? np.weapon : IT_NONE, np.hurtT > 0, moving,
                      G.realTime + i * 0.37f, np.reloading, false, 1, Color(), false, shirt);
    }
}

void drawNetPlayerTags(uint8_t where, Vec2 cam, bool includeSelf) {
    (void)cam;
    if (!Coop::active() || G.prof.rivals) return;   // Rivals: a stranger is a stranger
    for (int i = 0; i < Coop::MAX_PLAYERS; i++) {
        const Coop::NetPlayer& np = Coop::player(i);
        if ((i == mySlot() && !includeSelf) || !np.used || np.where != where) continue;
        if (where == Coop::W_RAID && np.ride >= 0) continue;   // the car carries their name
        int col = Coop::colorPal(i);
        R::textShadow(np.name, std::floor(np.pos.x - R::textWidth(np.name) / 2), np.pos.y - 26, pal(col));
        if (Voice::talking(i)) {   // sound waves beside the name while you can hear them
            float wx = std::floor(np.pos.x + R::textWidth(np.name) / 2) + 2;
            float ph = std::fmod(G.realTime * 3.0f, 1.0f);
            R::text(ph < 0.5f ? ")" : "))", wx, np.pos.y - 26, pal(P_YGREEN));
        }
        if (np.downed) {
            R::textCentered(T("DOWN"), np.pos.x, np.pos.y - 17, pal(P_CORAL));
            UI::bar(np.pos.x - 10, np.pos.y - 9, 20, 2, np.downT / 30.0f, P_CORAL);
        } else if (where == Coop::W_RAID && np.maxHp > 0) {
            UI::bar(np.pos.x - 7, np.pos.y - 16, 14, 2, np.hp / np.maxHp, P_CORAL);
        } else if (np.inBed) {
            R::textCentered("zzz", np.pos.x, np.pos.y - 17, pal(P_LAVENDER));
        }
    }
}

std::string raid_missedHorde() {
    Profile& p = G.prof;
    float endOfDay = (p.day - 1) * 1440.0f + CURFEW_MIN;
    if (p.nextHordeAt > endOfDay) return "";
    // Build today's outside as it stands, with nobody in it but the horde and whoever
    // is posted at the bunker, and let it play out.
    s_rng = Rng(todaySeed() ^ 0x51EE9);
    World::zombieSpawns = p.zombieMode();
    World::withCrypts = true;
    G.world.generate(todaySeed(), p.day);
    placeTurretsInWorld(G.world);
    s_gateT.clear();
    s_axes.clear();
    s_casings.clear();
    G.enemies.clear();
    G.bullets.clear();
    G.nightFallen = false;
    if (p.baseHp < 0 || p.baseHp > baseMaxHp()) p.baseHp = baseMaxHp();
    for (Turret& t : p.turrets) { t.cd = t.retargetT = 0; t.target = -1; }
    G.player.pos = G.world.homePos;
    spawnSquad();
    s_hordeNote.clear();
    launchHorde();
    G.messages.clear();
    s_bigT = 0;
    resolveHordeOffscreen();
    G.enemies.clear();
    G.particles.clear();
    return s_hordeNote;
}

// ---- local co-op seats outside (0.12v) ------------------------------------------------
// A seat's turn swaps its raid state with the globals (seat 0's lives there).
void raid_swapSeat(int seat) {
    RaidSeat& r = s_raidSeats[std::clamp(seat, 0, Local::MAX_SEATS - 1)];
    std::swap(s_spikeCd, r.spikeCd);
    std::swap(s_deathT, r.deathT);
    std::swap(s_deathCause, r.deathCause);
    std::swap(s_downT, r.downT);
    std::swap(s_reviveT, r.reviveT);
    std::swap(s_reviveSlot, r.reviveSlot);
    std::swap(s_ride, r.ride);
    std::swap(s_crashCd, r.crashCd);
    std::swap(s_carMsgT, r.carMsgT);
    std::swap(s_talkT, r.talkT);
    std::swap(s_talkPage, r.talkPage);
    std::swap(s_interOpts, r.interOpts);
    std::swap(s_interSel, r.interSel);
    std::swap(s_interSelKey, r.interSelKey);
    std::swap(s_assistTarget, r.assistTarget);
    std::swap(s_assistNet, r.assistNet);
    std::swap(s_assistT, r.assistT);
    std::swap(s_shopSel, r.shopSel);
    std::swap(s_shopColor, r.shopColor);
    std::swap(s_pvpAttacker, r.pvpAttacker);
    std::swap(s_pvpAttackT, r.pvpAttackT);
    std::swap(s_localOut, r.localOut);
}

// A player joined outside: out of the hatch with the others, beside whoever they joined.
void raid_seatJoin(Vec2 near) {
    seatStepOut(near);
    spawnMyCar();
}

// That player drops out: their car goes back to the mechanic's with them.
void raid_seatLeave() {
    persistMyCar();
    if (s_ride >= 0) leaveCar(false);
    int me = mySlot();
    s_cars.erase(std::remove_if(s_cars.begin(), s_cars.end(), [me](const Car& c) { return c.owner == me; }), s_cars.end());
    closeLoot();
    G.panel = Panel::None;
    resetSeatRaidState();
}

// Bled out: waiting for the others to come home.
bool raid_localOut() { return G.scene == Scene::Raid && s_localOut; }

void raid_localEnded() {
    for (RaidSeat& r : s_raidSeats) r = RaidSeat();
    s_localOut = false;
    R::setZoom(1);
}

float gateOpenness(int tx, int ty) { return gateOpenAt(tx, ty); }
