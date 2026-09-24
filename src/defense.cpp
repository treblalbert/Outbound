// Base defense: turret and mercenary tables, their costs, and the defense console's
// edit mode, which looks out over the compound around the hatch so turrets can be
// placed, levelled, repaired and sold. The fighting itself lives in raid.cpp.
#include "art.h"
#include "atmosphere.h"
#include "audio.h"
#include "coop.h"
#include "net.h"
#include "game.h"
#include "input.h"
#include "lang.h"
#include "prompt.h"
#include "sprites.h"
#include "ui.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <ctime>

using namespace Sprites;

// ---------------------------------------------------------------- tables
//                name              desc                                               unlock  wave  build   dmg   rate  range  hp    color
static const TurretDef TURRETS[TT_COUNT] = {
    {"Gun Turret",        "Single shots. Cheap and dependable.",              0,     0,   250,   18,  2.5f, 140,  250, P_TAN},
    {"Auto Turret",       "A machine gun on a swivel. Shreds crowds.",        900,   2,   600,   11,  9.0f, 130,  320, P_BLUE},
    {"Flamethrower",      "Short range cone that burns everything in it.",    2000,  4,   1100,  7,  14.0f,  72,  460, P_ORANGE},
    {"Laser Turret",      "Instant beam that cuts through a whole line.",     4000,  6,   2000,  48,  2.2f, 200,  340, P_PINK},
    {"Rocket Turret",     "Slow, long range rockets with splash damage.",     7000,  9,   3500, 110,  0.6f, 230,  380, P_SAGE},
};

const TurretDef& turretDef(int type) { return TURRETS[std::clamp(type, 0, TT_COUNT - 1)]; }

static const UpgradeDef DEF_UPGRADES[DU_COUNT] = {
    {"Firepower", "+10% turret damage", 10, 450},
    {"Servo Motors", "+7% turret fire rate", 8, 550},
    {"Armor Plating", "+20% turret health", 6, 400},
    {"Fortify Bunker", "+150 bunker health", 8, 450},
    {"Bounty Contracts", "+15% money per zombie", 5, 650},
};

const UpgradeDef& defenseUpgradeDef(int id) { return DEF_UPGRADES[std::clamp(id, 0, DU_COUNT - 1)]; }

int defenseUpgradeCost(int id, int level) {
    return (int)(DEF_UPGRADES[id].baseCost * std::pow(level + 1.0f, 1.5f) / 10.0f) * 10;
}

TurretStats turretStats(const Turret& t) {
    const TurretDef& d = turretDef(t.type);
    const Profile& p = G.prof;
    float l = (float)(std::clamp(t.level, 1, TURRET_MAX_LEVEL) - 1);
    TurretStats s;
    s.damage = d.damage * (1.0f + 0.30f * l) * (1.0f + 0.10f * p.defUp[DU_FIREPOWER]);
    s.rate = d.rate * (1.0f + 0.10f * l) * (1.0f + 0.07f * p.defUp[DU_RATE]);
    s.range = d.range + 8.0f * l;
    s.maxHp = d.hp * (1.0f + 0.25f * l) * (1.0f + 0.20f * p.defUp[DU_PLATING]);
    return s;
}

static int levelCost(int type, int level) {   // level -> level + 1
    return (int)(turretDef(type).buildCost * 0.6f * std::pow((float)level, 1.5f) / 10.0f) * 10;
}

int turretUpgradeCost(const Turret& t) { return levelCost(t.type, t.level); }

int turretRepairCost(const Turret& t) {
    float maxHp = turretStats(t).maxHp;
    float missing = maxHp - std::max(0.0f, t.hp);
    if (missing <= 0.5f) return 0;
    return std::max(10, (int)(missing / maxHp * turretDef(t.type).buildCost * 0.35f / 5.0f) * 5);
}

int turretSellValue(const Turret& t) {
    int spent = turretDef(t.type).buildCost;
    for (int l = 1; l < t.level; l++) spent += levelCost(t.type, l);
    return spent / 2;
}

float baseMaxHp() { return 600.0f + 150.0f * G.prof.defUp[DU_FORTIFY]; }

// Every horde is bigger than the last; the square term keeps late hordes pressing
// even once the flat part stops mattering. The first is already a real wave (33),
// the fifth close to a hundred, the tenth close to two hundred.
int hordeSize(int n) {
    int size = 24 + 9 * n + n * n * 3 / 4;
    return G.prof.hardcore() ? size * 13 / 10 : size;   // Hardcore: 30% more of them
}

float absMinutes() { return (G.prof.day - 1) * 1440.0f + G.prof.timeMin; }

bool hordeDueTonight() {
    const Profile& p = G.prof;
    float tonight = (p.day - 1) * 1440.0f + CURFEW_MIN;
    float morning = p.day * 1440.0f + DAY_START_MIN;
    return p.nextHordeAt >= tonight && p.nextHordeAt < morning;
}

bool hordeBeforeMorning() {
    const Profile& p = G.prof;
    return p.nextHordeAt < p.day * 1440.0f + DAY_START_MIN && absMinutes() < p.nextHordeAt;
}

// Hordes are rare, roughly one every day and a half to two days. They can land at any
// hour: one due in the night has to be fought in the dark before you can sleep.
void rollNextHorde() {
    Profile& p = G.prof;
    Rng r(mix64(p.worldSeed ^ ((uint64_t)p.day << 20) ^ ((uint64_t)p.hordeNum << 40) ^ (uint64_t)(p.timeMin * 7.0f) ^ 0x5EEDull));
    p.nextHordeAt = p.rivals ? 1e9f : absMinutes() + r.range(30.0f, 44.0f) * 60.0f;   // Rivals: no hordes
}

std::string hordeCountdown() {
    int left = (int)std::ceil(G.prof.nextHordeAt - absMinutes());
    if (left <= 0) return T("now");
    int d = left / 1440, h = (left % 1440) / 60, m = left % 60;
    char buf[32];
    if (d > 0) std::snprintf(buf, sizeof buf, "%dd %02dh", d, h);
    else if (h > 0) std::snprintf(buf, sizeof buf, "%dh %02dm", h, m);
    else std::snprintf(buf, sizeof buf, "%d min", m);
    return buf;
}

int hordeCountdownColor() {
    float left = G.prof.nextHordeAt - absMinutes();
    if (left <= 30) return std::fmod(G.realTime, 0.8f) < 0.4f ? P_CORAL : P_YELLOW;
    if (left <= 120) return P_CORAL;
    if (left <= 360) return P_ORANGE;
    return P_LAVENDER;
}

std::string hordeWhen() {
    const Profile& p = G.prof;
    int day = (int)(p.nextHordeAt / 1440.0f) + 1;
    std::string clock = fmtTime(p.nextHordeAt - (day - 1) * 1440.0f);
    return day == p.day ? clock : T1("Day {0}", std::to_string(day)) + " " + clock;
}

void Profile::giveDefaultTurrets() {
    turrets.clear();
    // Either side of the lane to the bunker door, which every horde has to use.
    for (int side : {-3, 3}) {
        Turret t;
        t.dx = side;
        t.dy = 6;
        t.type = TT_GUN;
        turrets.push_back(t);
    }
    for (Turret& t : turrets) t.hp = turretStats(t).maxHp;
}

static const HireTier HIRES[HIRE_TIERS] = {
    //  name        price  day  weapon      hp   dmg   spread  sight  helmet
    {"Drifter",     350,   1,   IT_PISTOL,  90,  0.9f, 0.10f,  190,   false},
    {"Gunhand",     900,   1,   IT_SMG,     130, 1.0f, 0.07f,  210,   false},
    {"Soldier",     1800,  2,   IT_RIFLE,   180, 1.1f, 0.045f, 240,   true},
    {"Veteran",     3400,  4,   IT_CARBINE, 240, 1.35f, 0.025f, 280,  true},
    {"Elite",       6000,  6,   IT_RIFLE,   320, 1.7f, 0.015f, 320,   true},
};

const HireTier& hireTier(int tier) { return HIRES[std::clamp(tier, 0, HIRE_TIERS - 1)]; }
float Hireling::maxHp() const { return hireTier(tier).hp; }

uint64_t todaySeed() {
    Profile& p = G.prof;
    if (p.worldSeed == 0) p.worldSeed = mix64((uint64_t)std::time(nullptr) * 2654435761ull + 1);
    return mix64(p.worldSeed ^ ((uint64_t)p.day * 0x9E3779B97F4A7C15ull) ^
                 ((uint64_t)(uint32_t)p.dayRev * 0xC2B2AE3D27D4EB4Full));
}

void placeTurretsInWorld(World& w) {
    if (G.prof.rivals) return;       // no shared compound to defend in Rivals
    for (const Turret& t : G.prof.turrets) {
        int x = w.homeTx + t.dx, y = w.homeTy + t.dy;
        if (!w.inBounds(x, y)) continue;
        Tile& tile = w.at(x, y);
        tile.worldDeco = 0;
        tile.deco = 0;
        tile.container = -1;
        // A wrecked turret is only scrap on the ground until it is repaired.
        tile.solid = t.hp > 0 ? S_TURRET : S_NONE;
        tile.hp = -1;
        w.updateMapPixel(x, y);
    }
}

// ---------------------------------------------------------------- drawing
// Turrets are not in the art pack, so they are drawn from primitives: a squat armoured
// base, and a head that turns toward its target.
void drawTurret(const Turret& t, Vec2 c, float alpha) {
    const TurretDef& d = turretDef(t.type);
    bool wrecked = t.hp <= 0;
    Color hurt = t.hurtT > 0 ? pal(P_CORAL, alpha) : Color();
    auto col = [&](int p, float a = 1.0f) { return t.hurtT > 0 ? hurt : pal(wrecked ? P_PURPLE : p, a * alpha); };
    R::circle(c + Vec2(0, 4), 7, pal(P_DARK, 0.35f * alpha), 14);
    R::rect(c.x - 6, c.y - 2, 12, 8, pal(P_DARK, alpha));
    R::rect(c.x - 5, c.y - 1, 10, 6, col(P_PURPLE));
    R::rect(c.x - 5, c.y + 3, 10, 1, col(d.color, 0.8f));
    Vec2 head = c + Vec2(0, -2);
    if (wrecked) {
        R::rect(head.x - 3, head.y - 1, 6, 3, pal(P_DARK, alpha));
        R::line(head, head + Vec2(5, 2), 1.5f, pal(P_DARK, alpha));
        return;
    }
    Vec2 f = fromAngle(t.angle);
    float barrel = t.type == TT_FLAME ? 6 : t.type == TT_ROCKET ? 7 : t.type == TT_LASER ? 9 : 8;
    float thick = t.type == TT_ROCKET ? 3.0f : t.type == TT_AUTO ? 1.6f : 2.0f;
    R::line(head, head + f * barrel, thick + 1.2f, pal(P_DARK, alpha));
    R::line(head, head + f * barrel, thick, col(t.type == TT_LASER ? P_LAVENDER : P_BEIGE));
    if (t.type == TT_AUTO) {
        Vec2 s(-f.y, f.x);
        R::line(head + s * 1.5f, head + s * 1.5f + f * (barrel - 1), 1.0f, col(P_BEIGE));
    }
    R::circle(head, 4, pal(P_DARK, alpha), 12);
    R::circle(head, 3, col(d.color), 12);
    R::rect(head.x - 1, head.y - 2, 2, 1, pal(P_WHITE, 0.6f * alpha));
    for (int l = 0; l < t.level; l++) R::rect(c.x - 5 + l * 2, c.y + 5, 1, 1, pal(P_YELLOW, alpha));
}

// ---------------------------------------------------------------- edit mode
namespace {

int s_tab = 0;             // 0 build, 1 research
int s_buildType = -1;      // turret type being placed, -1 = none
int s_selected = -1;       // index into prof.turrets

bool buildableOffset(int dx, int dy) {
    if (std::abs(dx) > BUILD_RADIUS || std::abs(dy) > BUILD_RADIUS) return false;
    if (std::abs(dx) <= 3 && std::abs(dy) <= 3) return false;     // the bunker itself
    if (std::abs(dx) <= 1 && dy >= 4) return false;               // your way out
    return true;
}

int turretAt(int dx, int dy) {
    const auto& ts = G.prof.turrets;
    for (int i = 0; i < (int)ts.size(); i++)
        if (ts[i].dx == dx && ts[i].dy == dy) return i;
    return -1;
}

bool canBuildAt(int dx, int dy) {
    if (!buildableOffset(dx, dy) || turretAt(dx, dy) >= 0) return false;
    const World& w = G.world;
    int x = w.homeTx + dx, y = w.homeTy + dy;
    if (!w.inBounds(x, y)) return false;
    int s = w.at(x, y).solid;
    return s == S_NONE || s == S_BUSH || s == S_ROCK;
}

void ring(Vec2 c, float r, Color col) {
    const int n = 48;
    for (int i = 0; i < n; i++)
        R::line(c + fromAngle(2 * PI * i / n) * r, c + fromAngle(2 * PI * (i + 1) / n) * r, 1, col);
}

Vec2 viewCam() {
    // The compound sits left of centre, clear of the side panel.
    return G.world.homePos - Vec2(std::floor((R::width() - 200) / 2.0f), std::floor(R::height() / 2.0f + 6));
}

std::string money(int v) { return "$" + std::to_string(v); }

void leave() {
    save_game();
    s_buildType = -1;
    s_selected = -1;
    base_enter(false);
}

}  // namespace

// ---------------------------------------------------------------- changes
// Every change to the defenses goes through defense_apply, which touches only the
// shared state (turrets, research, the bunker) and never money: whoever pays does
// that on their own profile. In co-op a guest applies the change locally at once and
// sends it to the host, whose copy is the real one (see Coop::M_DEF_OP).
bool defense_apply(int op, int a, int b, int c) {
    Profile& p = G.prof;
    World& w = G.world;
    switch (op) {
    case DO_BUILD: {
        if (a < 0 || a >= TT_COUNT || !p.turretUnlocked[a] || (int)p.turrets.size() >= MAX_TURRETS || !canBuildAt(b, c)) return false;
        Turret t;
        t.dx = b; t.dy = c; t.type = a; t.level = 1;
        t.hp = turretStats(t).maxHp;
        t.angle = angleOf(Vec2((float)b, (float)c));
        p.turrets.push_back(t);
        placeTurretsInWorld(w);
        return true;
    }
    case DO_UNLOCK:
        if (a < 0 || a >= TT_COUNT || p.turretUnlocked[a]) return false;
        p.turretUnlocked[a] = true;
        return true;
    case DO_REPAIR_ALL:
        for (Turret& t : p.turrets) t.hp = turretStats(t).maxHp;
        placeTurretsInWorld(w);
        return true;
    case DO_RESEARCH: {
        if (a < 0 || a >= DU_COUNT || p.defUp[a] >= defenseUpgradeDef(a).maxLevel) return false;
        float oldBaseMax = baseMaxHp();
        std::vector<float> oldMax;
        for (const Turret& t : p.turrets) oldMax.push_back(turretStats(t).maxHp);
        p.defUp[a]++;
        // Extra health arrives already patched in, not as damage to repair.
        if (a == DU_FORTIFY) p.baseHp += baseMaxHp() - oldBaseMax;
        if (a == DU_PLATING)
            for (size_t k = 0; k < p.turrets.size(); k++)
                if (p.turrets[k].hp > 0) p.turrets[k].hp += turretStats(p.turrets[k]).maxHp - oldMax[k];
        return true;
    }
    case DO_UPGRADE: {
        int i = turretAt(b, c);
        if (i < 0 || p.turrets[i].level >= TURRET_MAX_LEVEL || p.turrets[i].hp <= 0) return false;
        Turret& t = p.turrets[i];
        float before = turretStats(t).maxHp;
        t.level++;
        t.hp += turretStats(t).maxHp - before;
        return true;
    }
    case DO_REPAIR: {
        int i = turretAt(b, c);
        if (i < 0) return false;
        p.turrets[i].hp = turretStats(p.turrets[i]).maxHp;
        placeTurretsInWorld(w);
        return true;
    }
    case DO_SELL: {
        int i = turretAt(b, c);
        if (i < 0) return false;
        int tx = w.homeTx + b, ty = w.homeTy + c;
        if (w.inBounds(tx, ty)) { w.at(tx, ty).solid = S_NONE; w.updateMapPixel(tx, ty); }
        p.turrets.erase(p.turrets.begin() + i);
        if (s_selected >= (int)p.turrets.size()) s_selected = -1;
        return true;
    }
    case DO_REPAIR_BASE:
        p.baseHp = baseMaxHp();
        return true;
    }
    return false;
}

namespace {
// Pays `cost` (negative: gets paid) and makes the change; a guest also tells the host.
bool doOp(int op, int cost, int a = 0, int b = 0, int c = 0) {
    Profile& p = G.prof;
    if (cost > 0 && p.money < cost) { setNotice(T("Not enough money.")); return false; }
    if (!defense_apply(op, a, b, c)) return false;
    p.money -= cost;
    if (Coop::guest()) {
        Net::Writer wr;
        wr.u8(Coop::M_DEF_OP);
        wr.u8((uint8_t)op);
        wr.i32(a); wr.i32(b); wr.i32(c);
        wr.i32(cost);
        Coop::toHost(wr, true);
    }
    save_game();
    return true;
}
}  // namespace

void defense_enter() {
    // In co-op the outside may be live with people in it: edit that one in place.
    if (!raid_worldLive()) {
        World::zombieSpawns = G.prof.zombieMode();
        World::withCrypts = true;
        G.world.generate(todaySeed(), G.prof.day);
        placeTurretsInWorld(G.world);
    }
    G.scene = Scene::Defense;
    G.panel = Panel::None;
    G.cam = viewCam();
    Atmo::reset();
    s_buildType = -1;
    s_selected = -1;
    // Keep turrets pointing somewhere sensible while nothing is attacking.
    for (Turret& t : G.prof.turrets) t.angle = angleOf(Vec2((float)t.dx, (float)t.dy));
}

void defense_update(float dt) {
    G.noticeT -= dt;
    {   // Up on the console, outside: the day's birds, or the wind after dark (0.11v).
        float t = G.prof.timeMin;
        float day = clampf((t - 5.5f * 60) / 45.0f, 0, 1) * clampf((20.5f * 60 - t) / 60.0f, 0, 1);
        Audio::setAmbient(Audio::AMB_BIRDS, day * 0.6f);
        Audio::setAmbient(Audio::AMB_WIND, (1.0f - day) * 0.5f);
    }
    base_hardcoreClock(dt);
    if (base_hordeCall(dt)) { s_buildType = -1; s_selected = -1; return; }
    if (Input::pressed(GLFW_KEY_ESCAPE)) {
        if (s_buildType >= 0 || s_selected >= 0) { s_buildType = -1; s_selected = -1; }
        else { leave(); return; }
    }
    // Co-op: others build and sell too; keep the compound's tiles matching the list.
    if (Coop::active()) {
        World& w = G.world;
        for (int dy = -BUILD_RADIUS; dy <= BUILD_RADIUS; dy++)
            for (int dx = -BUILD_RADIUS; dx <= BUILD_RADIUS; dx++) {
                int x = w.homeTx + dx, y = w.homeTy + dy;
                if (w.inBounds(x, y) && w.at(x, y).solid == S_TURRET && turretAt(dx, dy) < 0) w.at(x, y).solid = S_NONE;
            }
        placeTurretsInWorld(w);
    }
    // Idle sweep so the turrets look alive.
    for (Turret& t : G.prof.turrets) {
        if (t.hp <= 0) continue;
        t.angle += std::sin(G.realTime * 0.6f + t.dx * 1.7f + t.dy) * 0.4f * dt;
    }
    G.cam = viewCam();
    Atmo::update(dt, false);
}

static void panelBuild(float x, float y, float w) {
    Profile& p = G.prof;
    for (int i = 0; i < TT_COUNT; i++) {
        const TurretDef& d = turretDef(i);
        float ry = y + i * 30;
        bool sel = s_buildType == i;
        R::rect(x, ry, w, 28, pal(sel ? P_LAVENDER : (i % 2 ? P_DARK : P_PURPLE), sel ? 0.45f : (i % 2 ? 1.0f : 0.35f)));
        R::rect(x + 3, ry + 3, 4, 22, pal(d.color));
        R::text(T(d.name), x + 11, ry + 3, pal(P_WHITE));
        bool unlocked = p.turretUnlocked[i];
        if (unlocked) {
            R::text(money(d.buildCost), x + 11, ry + 14, pal(p.money >= d.buildCost ? P_YGREEN : P_CORAL));
            if (UI::hover(x, ry, w - 58, 28)) {
                Turret probe; probe.type = i;
                TurretStats s = turretStats(probe);
                char buf[96];
                std::snprintf(buf, sizeof buf, "\n%s %d  %s %.1f/s  %s %d  HP %d", T("DMG").c_str(), (int)s.damage, T("RATE").c_str(), s.rate,
                              T("RANGE").c_str(), (int)s.range, (int)s.maxHp);
                UI::tooltip(T(d.name), T(d.desc) + buf);
            }
            if (UI::button(x + w - 54, ry + 7, 50, 14, sel ? T("Cancel") : T("Place"), true)) {
                s_buildType = sel ? -1 : i;
                s_selected = -1;
            }
        } else if (p.hordesRepelled < d.unlockWave) {
            R::text(T1("Repel {0} hordes", std::to_string(d.unlockWave)), x + 11, ry + 14, pal(P_CORAL));
            if (UI::hover(x, ry, w, 28)) UI::tooltip(T(d.name), T(d.desc));
        } else {
            R::text(T("Research"), x + 11, ry + 14, pal(P_LAVENDER));
            if (UI::hover(x, ry, w - 70, 28)) UI::tooltip(T(d.name), T(d.desc));
            if (UI::button(x + w - 66, ry + 7, 62, 14, money(d.unlockCost), p.money >= d.unlockCost, P_YGREEN)) {
                if (doOp(DO_UNLOCK, d.unlockCost, i)) {
                    Audio::play(Snd::sell, 0.7f, 0.6f);
                    setNotice(T1("{0} unlocked.", T(d.name)));
                }
            }
        }
    }
    float by = y + TT_COUNT * 30 + 4;
    R::text(T2("Turrets {0}/{1}", std::to_string(p.turrets.size()), std::to_string(MAX_TURRETS)), x + 2, by, pal(P_LAVENDER));
    int repairAll = 0;
    for (const Turret& t : p.turrets) repairAll += turretRepairCost(t);
    if (repairAll > 0 && UI::button(x, by + 12, w, 14, T1("Repair all turrets {0}", money(repairAll)), p.money >= repairAll, P_YGREEN)) {
        if (doOp(DO_REPAIR_ALL, repairAll)) Audio::play(Snd::sell, 0.6f, 0.8f);
    }
}

static void panelResearch(float x, float y, float w) {
    Profile& p = G.prof;
    for (int i = 0; i < DU_COUNT; i++) {
        const UpgradeDef& d = defenseUpgradeDef(i);
        float ry = y + i * 30;
        R::rect(x, ry, w, 28, pal(i % 2 ? P_DARK : P_PURPLE, i % 2 ? 1.0f : 0.35f));
        R::text(T(d.name), x + 4, ry + 3, pal(P_WHITE));
        R::text(T(d.desc), x + 4, ry + 14, pal(P_BEIGE));
        for (int l = 0; l < d.maxLevel; l++) R::rect(x + 4 + l * 6, ry + 23, 4, 3, pal(l < p.defUp[i] ? P_YELLOW : P_PURPLE));
        if (p.defUp[i] >= d.maxLevel) {
            R::text(T("MAX"), x + w - 26, ry + 10, pal(P_YELLOW));
            continue;
        }
        int cost = defenseUpgradeCost(i, p.defUp[i]);
        if (UI::button(x + w - 58, ry + 7, 54, 14, money(cost), p.money >= cost, P_YGREEN)) {
            if (doOp(DO_RESEARCH, cost, i)) {
                Audio::play(Snd::sell, 0.7f, 0.6f);
                setNotice(T2("{0} upgraded to level {1}.", T(d.name), std::to_string(p.defUp[i])));
            }
        }
    }
}

static void panelSelected(float x, float y, float w) {
    Profile& p = G.prof;
    if (s_selected < 0 || s_selected >= (int)p.turrets.size()) return;
    Turret& t = p.turrets[s_selected];
    const TurretDef& d = turretDef(t.type);
    TurretStats s = turretStats(t);
    R::rect(x, y, w, 96, pal(P_PURPLE, 0.35f));
    R::rectOutline(x, y, w, 96, pal(d.color));
    R::text(T(d.name) + "  " + T("Lv") + " " + std::to_string(t.level), x + 4, y + 4, pal(P_YELLOW));
    UI::bar(x + 4, y + 15, w - 8, 4, std::max(0.0f, t.hp) / s.maxHp, t.hp > 0 ? P_LGREEN : P_CORAL);
    char buf[96];
    std::snprintf(buf, sizeof buf, "%s %d  %s %.1f/s  %s %d", T("DMG").c_str(), (int)s.damage, T("RATE").c_str(), s.rate, T("RANGE").c_str(), (int)s.range);
    R::text(buf, x + 4, y + 23, pal(P_WHITE));
    if (t.hp <= 0) R::text(T("WRECKED - repair to use it"), x + 4, y + 33, pal(P_CORAL));
    else {
        std::snprintf(buf, sizeof buf, "HP %d/%d", (int)t.hp, (int)s.maxHp);
        R::text(buf, x + 4, y + 33, pal(P_LAVENDER));
    }
    if (t.level < TURRET_MAX_LEVEL) {
        Turret next = t;
        next.level++;
        TurretStats n = turretStats(next);
        std::snprintf(buf, sizeof buf, "%s: %s %d  %s %.1f", T("Next").c_str(), T("DMG").c_str(), (int)n.damage, T("RATE").c_str(), n.rate);
        R::text(buf, x + 4, y + 43, pal(P_BEIGE));
        int cost = turretUpgradeCost(t);
        if (UI::button(x + 4, y + 56, w - 8, 14, T1("Upgrade {0}", money(cost)), p.money >= cost && t.hp > 0, P_YGREEN)) {
            if (doOp(DO_UPGRADE, cost, 0, t.dx, t.dy)) {
                Audio::play(Snd::sell, 0.7f, 0.6f);
                setNotice(T2("{0} upgraded to level {1}.", T(d.name), std::to_string(p.turrets[s_selected].level)));
            }
            return;
        }
    } else {
        R::text(T("MAX LEVEL"), x + 4, y + 46, pal(P_YELLOW));
    }
    int repair = turretRepairCost(t);
    float half = std::floor((w - 12) / 2);
    if (UI::button(x + 4, y + 76, half, 14, repair > 0 ? T1("Repair {0}", money(repair)) : T("Repaired"), repair > 0 && p.money >= repair, P_YGREEN)) {
        if (doOp(DO_REPAIR, repair, 0, t.dx, t.dy)) Audio::play(Snd::sell, 0.6f, 0.8f);
        return;
    }
    int sell = turretSellValue(t);
    if (UI::button(x + 8 + half, y + 76, half, 14, T1("Sell +{0}", money(sell)), true, P_CORAL)) {
        if (doOp(DO_SELL, -sell, 0, t.dx, t.dy)) Audio::play(Snd::sell, 0.6f, 1.2f);
        s_selected = -1;
    }
}

void defense_draw() {
    Profile& p = G.prof;
    float W = (float)R::width(), H = (float)R::height();
    Vec2 cam = G.cam;
    Vec2 camF(std::floor(cam.x), std::floor(cam.y));
    World& w = G.world;
    Vec2 mw = camF + Input::mouse();
    int hx = World::toTile(mw.x) - w.homeTx, hy = World::toTile(mw.y) - w.homeTy;
    float panelX = W - 204;
    bool overUi = Input::mouse().x >= panelX - 4 || Input::mouse().y < 18;

    // ---- world
    sceneBegin();
    sceneSetWorld(&w);
    setSunForTime(p.timeMin);
    R::begin(R::WORLD, cam);
    drawWorldTiles(w, cam, G.realTime);
    Atmo::drawGround(w, cam);
    Art::Piece hatchArt = Art::hatch();
    if (hatchArt.valid()) R::spriteAt(*hatchArt.sprite, 0, w.homePos + Vec2(0, 8), R::Pivot::Bottom, 1.4f);
    else R::sprite(HATCH, w.homePos);
    // The build zone, so it is obvious where turrets may go.
    for (int dy = -BUILD_RADIUS; dy <= BUILD_RADIUS; dy++)
        for (int dx = -BUILD_RADIUS; dx <= BUILD_RADIUS; dx++) {
            if (!buildableOffset(dx, dy)) continue;
            float x = (w.homeTx + dx) * (float)TILE, y = (w.homeTy + dy) * (float)TILE;
            // Controller: the compound's tiles are stops for the D-pad too.
            UI::focusable(x - camF.x, y - camF.y, TILE, TILE);
            R::rectOutline(x, y, TILE, TILE, pal(P_CREAM, s_buildType >= 0 ? 0.22f : 0.08f));
        }
    drawTileSolids(w, cam, G.realTime);
    for (size_t i = 0; i < p.turrets.size(); i++) {
        const Turret& t = p.turrets[i];
        drawTurret(t, World::tileCenter(w.homeTx + t.dx, w.homeTy + t.dy), 1.0f);
    }
    sceneFlush();

    // Placement ghost, selection and range rings.
    if (!overUi && buildableOffset(hx, hy)) {
        Vec2 c = World::tileCenter(w.homeTx + hx, w.homeTy + hy);
        if (s_buildType >= 0) {
            bool ok = canBuildAt(hx, hy);
            R::rect(c.x - 8, c.y - 8, TILE, TILE, pal(ok ? P_LGREEN : P_CORAL, 0.3f));
            Turret ghost; ghost.type = s_buildType; ghost.hp = 1; ghost.angle = -PI / 2;
            if (ok) {
                drawTurret(ghost, c, 0.6f);
                ring(c, turretStats(ghost).range, pal(P_LGREEN, 0.5f));
            }
        } else if (turretAt(hx, hy) >= 0) {
            R::rectOutline(c.x - 8, c.y - 8, TILE, TILE, pal(P_YELLOW, 0.8f));
        }
    }
    if (s_selected >= 0 && s_selected < (int)p.turrets.size()) {
        const Turret& t = p.turrets[s_selected];
        Vec2 c = World::tileCenter(w.homeTx + t.dx, w.homeTy + t.dy);
        R::rectOutline(c.x - 8, c.y - 8, TILE, TILE, pal(P_YELLOW));
        ring(c, turretStats(t).range, pal(P_YELLOW, 0.55f));
    }
    drawRoofs(w, cam, w.homePos, G.frameDt);
    Atmo::drawRain(w, cam);
    R::end();

    R::begin(R::GLOW, cam);
    R::end();

    // ---- UI
    R::begin(R::UI, Vec2());
    R::rect(0, 0, W, 16, pal(P_DARK, 0.9f));
    R::text(T("DEFENSE"), 8, 5, pal(P_LAVENDER));
    R::text(T("DAY") + " " + std::to_string(p.day), 62, 5, pal(P_WHITE));
    R::text(money(p.money), 104, 5, pal(P_YGREEN));
    R::text(T("Bunker"), 170, 5, pal(P_CORAL));
    UI::bar(206, 6, 60, 5, p.baseHp / baseMaxHp(), P_CORAL);
    std::string horde = T2("Horde {0} in {1}", std::to_string(p.hordeNum), hordeCountdown()) + "  (" + std::to_string(hordeSize(p.hordeNum)) + ")";
    R::text(horde, 274, 5, pal(hordeCountdownColor()));

    UI::panel(panelX, 20, 200, H - 24, "");
    if (UI::button(panelX + 4, 24, 94, 14, T("Build"), true, s_tab == 0 ? P_YELLOW : P_WHITE)) s_tab = 0;
    if (UI::button(panelX + 102, 24, 94, 14, T("Research"), true, s_tab == 1 ? P_YELLOW : P_WHITE)) s_tab = 1;
    if (s_tab == 0) panelBuild(panelX + 4, 42, 192);
    else panelResearch(panelX + 4, 42, 192);
    panelSelected(panelX + 4, H - 124, 192);

    // Bunker repairs: sleeping patches it up for free, this is for mid-day.
    int missing = (int)(baseMaxHp() - p.baseHp);
    int baseRepair = missing > 0 ? std::max(10, missing / 2 / 5 * 5) : 0;
    if (s_selected < 0 && baseRepair > 0 &&
        UI::button(panelX + 4, H - 48, 192, 14, T1("Repair bunker {0}", money(baseRepair)), p.money >= baseRepair, P_YGREEN)) {
        if (doOp(DO_REPAIR_BASE, baseRepair)) Audio::play(Snd::sell, 0.6f, 0.8f);
    }
    if (UI::button(panelX + 4, H - 26, 192, 16, T("Back to bunker"))) { leave(); UI::endFrame(); R::end(); return; }

    if (s_buildType >= 0) {
        const Prompt::Hint hints[] = {{Prompt::Select, T("Place on a free tile")}, {Prompt::AltSelect, T("Cancel")}, {Prompt::Back, T("Back to bunker")}};
        Prompt::row(hints, 3, 6, H - 18, pal(P_LAVENDER));
    } else {
        const Prompt::Hint hints[] = {{Prompt::Select, T("Pick a turret to upgrade, repair or sell")}, {Prompt::Back, T("Back to bunker")}};
        Prompt::row(hints, 2, 6, H - 18, pal(P_LAVENDER));
    }
    if (G.noticeT > 0 && !G.notice.empty()) R::textCentered(G.notice, (W - 204) / 2, 24, pal(P_WHITE));
    base_drawHordeCall();
    UI::endFrame();
    R::end();

    // ---- clicks on the compound (after the panel had its chance to eat them)
    if (!overUi && !UI::overPanel()) {
        bool padClick = Input::usingPad() && Input::pressed(GLFW_KEY_E);
        if (Input::mousePressed(1) || Input::padAltPressed()) { s_buildType = -1; s_selected = -1; }
        if (Input::mousePressed(0) || padClick) {
            if (s_buildType >= 0) {
                const TurretDef& d = turretDef(s_buildType);
                if ((int)p.turrets.size() >= MAX_TURRETS) setNotice(T("The compound cannot hold more turrets."));
                else if (p.money < d.buildCost) setNotice(T("Not enough money."));
                else if (canBuildAt(hx, hy) && doOp(DO_BUILD, d.buildCost, s_buildType, hx, hy)) {
                    Audio::play(Snd::door, 0.6f, 1.3f);
                    if (p.money < d.buildCost) s_buildType = -1;
                }
            } else {
                s_selected = turretAt(hx, hy);
            }
        }
    }

    LightingParams& lp = G.lighting;
    lp.lights.clear();
    lp.cone = false;
    lp.coneBlockers.clear();
    // Kept at least a little lit: this is a planning view, not a night out.
    Atmo::apply(lp, p.timeMin);
    lp.ambient = Color(std::max(lp.ambient.r, 0.7f), std::max(lp.ambient.g, 0.7f), std::max(lp.ambient.b, 0.75f));
    lp.saturate = std::max(lp.saturate, 0.85f);
    G.drawCam = cam;
}
