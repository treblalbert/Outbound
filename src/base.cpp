// The bunker: sleep, stash, trade, upgrade and head out.
#include "art.h"
#include "atmosphere.h"
#include "assets.h"
#include "audio.h"
#include "game.h"
#include "coop.h"
#include "input.h"
#include "lang.h"
#include "local.h"
#include "prompt.h"
#include "options.h"
#include "sprites.h"
#include "ui.h"
#include "voice.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

using namespace Sprites;

// Panels drawn by base_draw but defined at the bottom of this file. These are plain
// file-local forwards; the public entry points (base_enter/base_update/base_draw,
// giveItem, the mission helpers) keep external linkage via game.h.
static void panelSummary(float W, float H);
static void panelBed(float W, float H);
static void soloNewDay(bool nightRanOut);
static void panelExit(float W, float H);
static void panelStash(float W, float H);
static void panelTrader(float W, float H);
static void panelWorkbench(float W, float H);
static void panelMission(float W, float H);
static void panelPause(float W, float H);
static void panelRecruit(float W, float H);
static void panelCrafter(float W, float H);
static void panelTutorial(float W, float H);
void drawControlsPanel(float x, float y);   // menu.cpp
static void drawStationArt(int tx, int ty, int sprite, const char* artKey = nullptr);
static void drawBasePanel(float W, float H);
static void baseLighting(Vec2 cam);
static void stationPrompt(int st, Vec2 cam);
static void drawLocalBaseUi(float W, float H, Vec2 cam);
static void baseSeatEnter(bool fromRaid, Vec2 at);
static void swapCrafterSeat(int& sel, int& page);
static void swapMissionSeat(int& lastDone);
static void baseSeatTick(float dt, int seat);
static void baseCamera(float dt);

namespace {

struct Station {
    int tx, ty;
    int sprite;
    Panel panel;
    const char* label;
    bool solid;
    const char* art = nullptr;   // pack sprite overriding the sprite's usual art
};

// Stations with no panel: the defense console takes you to its own scene.
constexpr int RECRUITER = -1;
constexpr int CRAFTER = -2;      // 0.11v: the gunsmith, drawn as a person at a bench

const Station STATIONS[] = {
    {2, 1, BED, Panel::Bed, "[E] Sleep", true},
    {3, 8, EXIT_LADDER, Panel::ExitConfirm, "[E] Head outside", false},
    {7, 1, STASH, Panel::Stash, "[E] Open stash", true},
    {9, 1, TERMINAL, Panel::Mission, "[E] Mission board", true},
    {12, 1, WORKBENCH, Panel::Workbench, "[E] Workbench (upgrades)", true},
    {12, 8, TERMINAL, Panel::Trader, "[E] Trade", true},
    {16, 6, TERMINAL, Panel::None, "[E] Defense console", true, "objects/buildings/antenna_1"},
    {7, 8, RECRUITER, Panel::Recruit, "[E] Hire mercenaries", true},
    {14, 8, CRAFTER, Panel::Crafter, "[E] Crafter (weapon upgrades)", true},
};

struct Deco { int tx, ty, sprite; bool solid; };
const Deco DECOS[] = {
    {11, 8, TRADER, true},
    {13, 8, WORKBENCH, true},    // the crafter's bench
};

// The bunker's furniture (BitGlow's interior pack). Nothing here can be used: it is
// only there to make the place feel lived in. Standing pieces sit on the bottom of
// their tiles and block `w` tiles from `tx`; rugs lie on the floor; wall pieces hang on
// the top wall. Lamps glow.
enum FurnKind { FK_STAND, FK_RUG, FK_WALL };
struct Furn { const char* key; int tx, ty, w; FurnKind kind; bool light; };
const Furn FURNITURE[] = {
    // along the top wall
    {"shelf_narrow", 3, 1, 1, FK_STAND, false},
    {"bookshelf", 4, 1, 2, FK_STAND, false},
    {"floorlamp_a", 1, 1, 1, FK_STAND, true},
    {"floorlamp_b", 11, 1, 1, FK_STAND, true},
    {"plant_basket", 13, 1, 1, FK_STAND, false},
    {"drawers", 14, 1, 1, FK_STAND, false},
    {"stove", 15, 1, 1, FK_STAND, false},
    {"fridge", 16, 1, 1, FK_STAND, false},
    // on the wall itself
    {"painting_hills", 8, 0, 1, FK_WALL, false},
    {"clock", 10, 0, 1, FK_WALL, false},
    // the sides and the bottom row
    {"plant_grey", 1, 4, 1, FK_STAND, false},
    {"armchair_grey", 16, 3, 1, FK_STAND, false},
    {"plant_basket", 1, 6, 1, FK_STAND, false},
    {"floorlamp_c", 15, 8, 1, FK_STAND, true},
    {"plant_grey", 16, 8, 1, FK_STAND, false},
    {"plant_red", 9, 8, 1, FK_STAND, false},
    // the middle of the room: a rug for the crew to stand on, a sofa facing it
    {"rug_red", 9, 5, 2, FK_RUG, false},
    {"rug_round", 5, 5, 1, FK_RUG, false},
    {"sofa_grey", 9, 6, 2, FK_STAND, false},
    {"table_round", 12, 6, 1, FK_STAND, false},
};

const Assets::Sprite* furnSprite(const char* key) { return Assets::find(std::string("furniture/") + key); }
// Bottom-centre of a standing piece (or centre of a rug / wall piece), in pixels.
Vec2 furnAnchor(const Furn& f) {
    float cx = (f.tx + f.w * 0.5f) * TILE;
    if (f.kind == FK_STAND) return {cx, (f.ty + 1.0f) * TILE};
    if (f.kind == FK_WALL) return {cx, f.ty * TILE + TILE * 0.55f};
    return {cx, (f.ty + 0.5f) * TILE};
}

struct ShopEntry { int id; int count; int price; int minDay; };
const ShopEntry SHOP[] = {
    {IT_BANDAGE, 1, 30, 1},      {IT_MEDKIT, 1, 120, 1},      {IT_GRENADE, 1, 110, 1},     {IT_AMMO_LIGHT, 30, 45, 1},
    {IT_AMMO_SHELL, 12, 45, 1},  {IT_AMMO_RIFLE, 30, 90, 1},  {IT_AMMO_SNIPER, 10, 100, 2}, {IT_ROCKET, 1, 220, 4},
    {IT_PISTOL, 1, 200, 1},      {IT_REVOLVER, 1, 680, 2},    {IT_SMG, 1, 560, 1},         {IT_SHOTGUN, 1, 520, 1},
    {IT_CARBINE, 1, 920, 3},
    {IT_SNIPER, 1, 1600, 3},     {IT_LAUNCHER, 1, 2600, 5},   {IT_VEST_LIGHT, 1, 380, 1},  {IT_VEST_HEAVY, 1, 1100, 3},
    {IT_PACK_SMALL, 1, 240, 1},  {IT_PACK_LARGE, 1, 720, 2},
};
constexpr int SHOP_COUNT = sizeof(SHOP) / sizeof(SHOP[0]);
constexpr int SHOP_PER_PAGE = 9;

float s_sleepFade = 0;
float s_callT = -1;           // counting down to being sent up to meet a horde
int s_tutStep = 0;
bool s_stashToPrivate = false; // where a click in the inventory sends things
// Local co-op (0.12v): each seat's own choices in the bunker's panels.
struct BaseSeat { bool stashToPrivate = false; int craftSel = 0, craftPage = 0, lastDone = -1; };
BaseSeat s_baseSeats[Local::MAX_SEATS];

// The stashes the game may use on its own (overflow from the trader, contract goods,
// the crafter's gun parts). In co-op the big stash belongs to everyone, so only your
// private one; alone, both.
struct StashRef { std::vector<Item>* v; int slots; };
std::vector<StashRef> ownStashes() {
    Profile& p = G.prof;
    if (Coop::active()) return {{&p.privStash, PRIVATE_STASH_SLOTS}};
    return {{&p.stash, STASH_SLOTS}, {&p.privStash, PRIVATE_STASH_SLOTS}};
}

int nearestStation(Vec2 at) {
    float best = 22;
    int idx = -1;
    for (int i = 0; i < (int)(sizeof(STATIONS) / sizeof(STATIONS[0])); i++) {
        float d = dist(World::tileCenter(STATIONS[i].tx, STATIONS[i].ty), at);
        if (d < best) { best = d; idx = i; }
    }
    return idx;
}

const char* stationArtKey(int sprite) {
    switch (sprite) {
    case STASH: return "objects/container/container_3_gray_horizontal";
    case TERMINAL: return "objects/vending-machine_blue";
    case WORKBENCH: return "objects/metal-plates";
    case EXIT_LADDER: return "objects/buildings/hatch_1_open";
    case BED: return "objects/bench_1_down";
    case TABLE: return "objects/pallet_1";
    case CRATE: return "objects/barrel_red_1";
    case C_BAG: return "objects/trash-bag_1";
    case PLANT: return "objects/nature/green/bush_1_green";
    case TRADER: return nullptr;
    }
    return nullptr;
}

void buildBase() {
    World& w = G.baseWorld;
    w.generateBase();
    auto place = [&](int tx, int ty, int sprite, bool solid) {
        Tile& t = w.at(tx, ty);
        const char* key = stationArtKey(sprite);
        bool hasArt = key && Assets::find(key);
        t.deco = hasArt ? 0 : (uint8_t)sprite;
        if (solid) { t.solid = S_FURNITURE; t.hp = -1; }
    };
    for (auto& s : STATIONS) place(s.tx, s.ty, s.sprite < 0 ? TABLE : s.sprite, s.solid);
    for (auto& d : DECOS) place(d.tx, d.ty, d.sprite, d.solid);
    for (const Furn& f : FURNITURE) {
        if (f.kind != FK_STAND || !furnSprite(f.key)) continue;
        for (int i = 0; i < f.w; i++) {
            Tile& t = w.at(f.tx + i, f.ty);
            if (t.solid != S_NONE) continue;
            t.solid = S_FURNITURE;
            t.hp = -1;
            t.deco = 0;
        }
    }
}

}  // namespace

static void drawStationArt(int tx, int ty, int sprite, const char* artKey) {
    Vec2 stand = World::tileCenter(tx, ty) + Vec2(0, TILE * 0.5f);
    if (sprite == CRAFTER) {
        // The crafter: a gunsmith in a brown work shirt, turning a pistol over in his
        // hands by his bench.
        int fr = (int)(G.realTime * 3.0f);
        Art::Piece body = Art::humanBody(Art::Dir::Left, Art::Anim::Idle, fr, true, false, 9);
        if (body.valid()) {
            sceneAdd(body, stand);
            Art::Piece gun = Art::humanGun(Art::Dir::Left, Art::Anim::Idle, fr, IT_PISTOL);
            if (gun.valid()) sceneAddCentered(gun, stand + Vec2(0, -3), Color(), 1, 0.02f);
            return;
        }
        sprite = TRADER;
    }
    if (sprite == RECRUITER) {
        // The recruiter: a veteran in a helmet, leaning on a rifle.
        Art::Piece body = Art::humanBody(Art::Dir::Down, Art::Anim::Idle, (int)(G.realTime * 4.0f), true);
        if (body.valid()) {
            Color tint(0.85f, 0.95f, 0.85f);
            sceneAdd(body, stand, tint);
            Art::Piece gun = Art::humanGun(Art::Dir::Down, Art::Anim::Idle, (int)(G.realTime * 4.0f), IT_RIFLE);
            if (gun.valid()) sceneAddCentered(gun, stand + Vec2(0, -3), tint, 1, 0.02f);
            Art::Piece h = Art::helmet(Art::Dir::Down, (int)(G.realTime * 4.0f));
            if (h.valid()) sceneAdd(h, stand + Vec2(0, -9), tint, 1, 0.03f);
            return;
        }
        sprite = TRADER;
    }
    if (artKey) {
        if (const Assets::Sprite* a = Assets::find(artKey)) { sceneAdd(Art::Piece{a, 0, false, 1.0f}, stand); return; }
    }
    if (sprite == TRADER) {
        Art::Piece body = Art::humanBody(Art::Dir::Down, Art::Anim::Idle, (int)(G.realTime * 5.0f), false);
        if (body.valid()) { sceneAdd(body, stand, Color(0.95f, 0.9f, 1.0f)); return; }
    }
    const char* key = stationArtKey(sprite);
    const Assets::Sprite* art = key ? Assets::find(key) : nullptr;
    Vec2 base = World::tileCenter(tx, ty) + Vec2(0, TILE * 0.5f);
    if (art && art->valid()) sceneAdd(Art::Piece{art, 0, false, 1.0f}, base);
    else R::spriteRect(sprite, tx * (float)TILE, ty * (float)TILE, TILE, TILE);
}

// ---- the trader's visit (0.11v): on the morning of day 3 he comes over from his counter
// with your first dungeon locator, free, and a word of warning. You hear him out.
namespace {
enum TraderVisit { TV_HOME, TV_COMING, TV_TALKING, TV_GOING };
int s_tv = TV_HOME;
Vec2 s_tvPos, s_tvLast;
float s_tvAngle = PI / 2;
int s_tvPage = 0;
float s_tvT = 0, s_tvStuck = 0, s_tvSide = 0;
Vec2 s_tvSideDir;
const char* TRADER_TALK[] = {
    "Morning. Don't get up on my account - ah, you're up. Good. Listen.",
    "Word is the old catacombs under the fields have opened up. Stairs going down, right out in the open. Nobody who got out of one came back poor.",
    "Here: a dungeon locator. It points you to the nearest way down and marks every entrance on your map. This first one's on me.",
    "It only works for today. When you want another, I sell them at the counter.",
    "But don't go down there as you are. It's dark, the dead fill every room, and the gangs below are better armed than anything up here.",
    "Get ready first: armour, a proper gun, medkits, plenty of ammo. Maybe some hired help. Then find one, clear it to the last room, and come back rich.",
};
constexpr int TRADER_PAGES = sizeof(TRADER_TALK) / sizeof(TRADER_TALK[0]);
Vec2 traderHome() { return World::tileCenter(11, 8); }

void resetTraderVisit() {
    s_tv = TV_HOME;
    s_tvPos = s_tvLast = traderHome();
    s_tvAngle = PI / 2;
}

void updateTraderVisit(float dt) {
    Profile& p = G.prof;
    s_tvLast = s_tvPos;
    auto walk = [&](Vec2 to, float speed) {
        Vec2 d = to - s_tvPos;
        if (length(d) < 2) return;
        Vec2 dir = normalize(d);
        if (s_tvSide > 0) { s_tvSide -= dt; dir = normalize(dir + s_tvSideDir * 1.5f); }
        s_tvAngle = angleOf(dir);
        Vec2 before = s_tvPos;
        s_tvPos = G.baseWorld.move(s_tvPos, dir * std::min(speed * dt, length(d)), 5);
        if (dist(before, s_tvPos) < speed * dt * 0.25f) {
            if ((s_tvStuck += dt) > 0.35f) { s_tvStuck = 0; s_tvSide = 0.6f; s_tvSideDir = Vec2(-dir.y, dir.x) * (std::fmod(G.realTime, 2.0f) < 1 ? 1.0f : -1.0f); }
        } else s_tvStuck = 0;
    };
    switch (s_tv) {
    case TV_HOME:
        // The first morning the catacombs are open, once you are up and about.
        if (p.day >= CRYPT_DAY && !p.locatorGift && p.tutorialDone && G.panel == Panel::None && s_sleepFade <= 0 && s_callT < 0) {
            s_tv = TV_COMING;
            s_tvPos = s_tvLast = traderHome() + Vec2(0, -TILE);   // out from behind his counter
        } else {
            s_tvPos = traderHome();
            s_tvAngle = dist(G.player.pos, s_tvPos) < 80 ? angleOf(G.player.pos - s_tvPos) : PI / 2;
        }
        break;
    case TV_COMING:
        if (dist(s_tvPos, G.player.pos) > 22) walk(G.player.pos, 60);
        else if (G.panel == Panel::None || G.panel == Panel::Inventory) {
            s_tv = TV_TALKING;
            s_tvPage = 0;
            s_tvT = 0;
            G.panel = Panel::TraderTalk;
            Audio::play(Snd::click, 0.6f, 0.8f);
        }
        break;
    case TV_TALKING:
        s_tvAngle = angleOf(G.player.pos - s_tvPos);
        if (G.panel != Panel::TraderTalk) G.panel = Panel::TraderTalk;   // hear him out
        s_tvT += dt;
        if (s_tvT > 0.35f && (Input::pressed(GLFW_KEY_E) || Input::keyPressed(GLFW_KEY_SPACE) || Input::keyPressed(GLFW_KEY_ENTER) || Input::mousePressed(0))) {
            Input::consumeMouse();
            Audio::play(Snd::click, 0.5f, 1.1f);
            s_tvT = 0;
            if (++s_tvPage >= TRADER_PAGES) {
                G.panel = Panel::None;
                p.locatorGift = true;
                p.locatorDay = p.day;
                s_tv = TV_GOING;
                Audio::play(Snd::sell, 0.7f, 0.9f);
                setNotice(T("Dungeon locator: works today. Outside, an orange arrow points to the nearest catacomb."));
                save_game();
            }
        }
        break;
    case TV_GOING:
        walk(traderHome() + Vec2(0, -TILE), 55);
        if (dist(s_tvPos, traderHome() + Vec2(0, -TILE)) < 3) s_tv = TV_HOME;
        break;
    }
}

void drawTraderVisitor() {
    bool moving = lengthSq(s_tvPos - s_tvLast) > 0.01f;
    Art::Piece body = Art::humanBody(Art::dirFromAngle(s_tvAngle), moving ? Art::Anim::Run : Art::Anim::Idle,
                                     (int)(G.realTime * (moving ? 10.0f : 5.0f)), false);
    if (body.valid()) sceneAdd(body, s_tvPos + Vec2(0, 8), Color(0.95f, 0.9f, 1.0f));
}

void drawTraderTalk(float W, float H) {
    float w = std::min(460.0f, W - 40), h = 92;
    float x = std::floor(W / 2 - w / 2), y = std::floor(H - h - 30);
    UI::panel(x, y, w, h, T("THE TRADER"));
    int page = std::clamp(s_tvPage, 0, TRADER_PAGES - 1);
    UI::textWrap(T(TRADER_TALK[page]), x + 10, y + 22, w - 20, pal(P_CREAM), 11);
    R::text(std::to_string(page + 1) + "/" + std::to_string(TRADER_PAGES), x + 8, y + h - 14, pal(P_LAVENDER));
    Prompt::label(Prompt::Interact, page + 1 < TRADER_PAGES ? T("Continue") : T("Thanks"), x + w - 110, y + h - 19, pal(P_YELLOW));
    if (page == 2) {
        // The locator itself, handed over.
        Vec2 ic(x + w - 30, y + 30);
        R::circle(ic, 8, pal(P_DARK));
        R::circle(ic, 7, pal(P_ORANGE, 0.35f));
        R::circle(ic, 2, pal(P_ORANGE));
    }
}
}  // namespace

void base_draw() {
    Profile& p = G.prof;
    float W = (float)R::width(), H = (float)R::height();
    Vec2 cam = G.cam;

    sceneBegin();
    sceneSetWorld(nullptr);      // underground: no sun
    R::begin(R::WORLD, cam);
    drawWorldTiles(G.baseWorld, cam, G.realTime);
    for (const Furn& f : FURNITURE)
        if (f.kind == FK_RUG)
            if (const Assets::Sprite* s = furnSprite(f.key)) R::spriteAt(*s, 0, furnAnchor(f), R::Pivot::Center);
    drawTileSolids(G.baseWorld, cam, G.realTime);
    for (const Furn& f : FURNITURE)
        if (f.kind == FK_WALL)
            if (const Assets::Sprite* s = furnSprite(f.key)) R::spriteAt(*s, 0, furnAnchor(f), R::Pivot::Center);
    for (auto& st : STATIONS) drawStationArt(st.tx, st.ty, st.sprite, st.art);
    // Your hired guns hang around the bunker between runs.
    for (size_t i = 0; i < G.prof.squad.size(); i++) {
        static const Vec2 SPOTS[SQUAD_MAX] = {{5.5f, 4.5f}, {9.5f, 4.5f}, {13.5f, 4.5f}};
        const Hireling& h = G.prof.squad[i];
        Vec2 at = SPOTS[i] * (float)TILE;
        Art::Piece body = Art::humanBody(Art::Dir::Down, Art::Anim::Idle, (int)(G.realTime * 5.0f + i * 2), true);
        if (!body.valid()) continue;
        sceneAdd(body, at + Vec2(0, 8));
        Art::Piece gun = Art::humanGun(Art::Dir::Down, Art::Anim::Idle, (int)(G.realTime * 5.0f + i * 2), hireTier(h.tier).weapon);
        if (gun.valid()) sceneAddCentered(gun, at + Vec2(0, 5), Color(), 1, 0.02f);
        if (hireTier(h.tier).helmet) {
            Art::Piece hm = Art::helmet(Art::Dir::Down, (int)(G.realTime * 5.0f + i * 2));
            if (hm.valid()) sceneAdd(hm, at + Vec2(0, -1), Color(), 1, 0.03f);
        }
    }
    for (auto& d : DECOS)
        if (d.sprite != TRADER) drawStationArt(d.tx, d.ty, d.sprite);
    drawTraderVisitor();   // the trader, at his counter or walking over (0.11v)
    for (const Furn& f : FURNITURE)
        if (f.kind == FK_STAND)
            if (const Assets::Sprite* s = furnSprite(f.key)) sceneAdd(Art::Piece{s, 0, false, 1.0f}, furnAnchor(f));
    {
        Art::Dir dir = Art::dirFromAngle(G.player.angle);
        Art::Piece body = Art::humanBody(dir, G.player.moving ? Art::Anim::Run : Art::Anim::Idle,
                                         (int)(G.realTime * (G.player.moving ? 12.0f : 6.0f)), false, false, G.prof.shirt);
        if (body.valid()) sceneAdd(body, G.player.pos + Vec2(0, 8));
        else sceneAddSprite(PLAYER, G.player.pos);
    }
    if (!G.prof.rivals) drawNetPlayers(Coop::W_BASE);   // Rivals: everyone has their own bunker
    sceneFlush();
    if (!G.prof.rivals) drawNetPlayerTags(Coop::W_BASE, cam);
    if (Local::active()) drawNetPlayerTags(Coop::W_BASE, cam, true);   // your own name too: who is who
    for (size_t i = 0; i < G.prof.squad.size() && i < SQUAD_MAX; i++) {
        static const Vec2 SPOTS[SQUAD_MAX] = {{5.5f, 4.5f}, {9.5f, 4.5f}, {13.5f, 4.5f}};
        R::textCentered(G.prof.squad[i].name, SPOTS[i].x * TILE, SPOTS[i].y * TILE - 16, pal(G.prof.squad[i].guard ? P_BLUE : P_MINT));
    }
    R::end();

    R::begin(R::GLOW, cam);
    R::end();

    R::begin(R::UI, Vec2());
    if (Local::active()) { drawLocalBaseUi(W, H, cam); UI::endFrame(); R::end(); baseLighting(cam); return; }
    R::rect(0, 0, W, 16, pal(P_DARK, 0.9f));
    R::text(T("DAY") + " " + std::to_string(p.day), 8, 5, pal(P_WHITE));
    R::text(fmtTime(p.timeMin), 56, 5, pal(p.timeMin >= LATEST_DEPART_MIN ? P_CORAL : P_YELLOW));
    R::text("$" + std::to_string(p.money), 100, 5, pal(P_YGREEN));
    char buf[64];
    std::snprintf(buf, sizeof buf, "HP %d/%d", (int)p.hp, (int)p.maxHp());
    R::text(buf, 160, 5, pal(P_CORAL));
    R::text(T("Bunker"), 236, 5, pal(P_ORANGE));
    UI::bar(272, 6, 44, 5, p.baseHp / baseMaxHp(), P_ORANGE);
    std::string next = p.rivals ? T("Rivals: trust nobody out there") : T2("Horde {0} in {1}", std::to_string(p.hordeNum), hordeCountdown()) + "  (~" + hordeWhen() + ")";
    R::text(next, 324, 5, pal(hordeCountdownColor()));
    std::string right = T("THE BUNKER");
    R::text(right, W - 8 - R::textWidth(right), 5, pal(P_LAVENDER));
    if (G.panel == Panel::TraderTalk) {
    } else if (G.panel == Panel::None) {
        const Prompt::Hint hints[] = {{Prompt::Move, T("Move")}, {Prompt::Inventory, T("Inventory")}, {Prompt::Mission, T("Mission")},
                                      {Prompt::Interact, T("Interact")}, {Prompt::Pause, T("Menu")}};
        Prompt::row(hints, 5, 6, H - 18, pal(P_LAVENDER));
    } else {
        Prompt::label(Prompt::Back, T("Close"), W - 6 - Prompt::labelWidth(Prompt::Back, T("Close")), H - 18, pal(P_LAVENDER));
    }

    int st = nearestStation(G.player.pos);
    if (G.panel == Panel::None && st >= 0) stationPrompt(st, cam);
    if (G.noticeT > 0 && !G.notice.empty()) {
        R::rect(W / 2 - R::textWidth(G.notice) / 2 - 6, H - 34, R::textWidth(G.notice) + 12, 14, pal(P_DARK, 0.9f));
        R::textCentered(G.notice, W / 2, H - 30, pal(P_WHITE), 1, false);
    }

    drawBasePanel(W, H);
    if (s_sleepFade > 0) R::rect(0, 0, W, H, pal(P_DARK, clampf(s_sleepFade, 0, 1)));
    if (G.panel == Panel::None) Voice::drawHud(10, H - 14);
    base_drawHordeCall();
    UI::endFrame();
    R::end();
    baseLighting(cam);
}

static void drawBasePanel(float W, float H) {
    switch (G.panel) {
    case Panel::Summary: panelSummary(W, H); break;
    case Panel::Bed: panelBed(W, H); break;
    case Panel::ExitConfirm: panelExit(W, H); break;
    case Panel::Stash: panelStash(W, H); break;
    case Panel::Trader: panelTrader(W, H); break;
    case Panel::Workbench: panelWorkbench(W, H); break;
    case Panel::Inventory: drawInventoryPanel(std::floor(W / 2 - 70), std::floor(H / 2 - 110), InvMode::Base, nullptr, 0); break;
    case Panel::Pause: panelPause(W, H); break;
    case Panel::Controls: drawControlsPanel(std::floor(W / 2 - 110), std::floor(H / 2 - 90)); break;
    case Panel::Mission: panelMission(W, H); break;
    case Panel::Recruit: panelRecruit(W, H); break;
    case Panel::Crafter: panelCrafter(W, H); break;
    case Panel::Tutorial: panelTutorial(W, H); break;
    case Panel::TraderTalk: drawTraderTalk(W, H); break;
    case Panel::Options: Options::drawPanel(W, H); break;
    default: break;
    }
}

static void baseLighting(Vec2 cam) {
    LightingParams& lp = G.lighting;
    lp.lights.clear();
    lp.ambient = Color(0.62f, 0.58f, 0.72f);
    Atmo::clearOutdoor(lp);
    lp.dither = 0.08f;
    lp.cone = false;
    lp.extraCones.clear();
    for (const Furn& f : FURNITURE)
        if (f.light) lp.lights.push_back({furnAnchor(f) - Vec2(0, 20), 95, 0.6f, pal(P_CREAM)});
    lp.lights.push_back({G.player.pos, 50, 0.3f, pal(P_WHITE)});
    if (Local::active())
        for (int k = 1; k < Local::MAX_SEATS; k++)
            if (Local::used(k)) lp.lights.push_back({Coop::player(k).pos, 50, 0.3f, pal(P_WHITE)});
    G.drawCam = cam;
}

// "Sleep", "Open stash"...: the button to press and what it does, over the station.
static void stationPrompt(int st, Vec2 cam) {
    Vec2 sp = World::tileCenter(STATIONS[st].tx, STATIONS[st].ty) - Vec2(std::floor(cam.x), std::floor(cam.y));
    // The labels were written as "[E] Sleep"; the button is drawn now instead.
    std::string text = T(STATIONS[st].label);
    size_t cut = text.find("] ");
    if (text[0] == '[' && cut != std::string::npos) text = text.substr(cut + 2);
    Prompt::labelCentered(Prompt::Interact, text, sp.x, sp.y - 26, pal(P_YELLOW));
}

// ---- the bunker with local co-op (0.12v): everyone's money and health along the top,
// each player's station prompt over their station (in their own buttons), and the one
// panel that is open, worked by whoever opened it.
static void drawLocalBaseUi(float W, float H, Vec2 cam) {
    Profile& host = G.prof;
    R::rect(0, 0, W, 16, pal(P_DARK, 0.9f));
    R::text(T("DAY") + " " + std::to_string(host.day), 8, 5, pal(P_WHITE));
    R::text(fmtTime(host.timeMin), 56, 5, pal(host.timeMin >= LATEST_DEPART_MIN ? P_CORAL : P_YELLOW));
    R::text(T("Bunker"), 100, 5, pal(P_ORANGE));
    UI::bar(136, 6, 44, 5, host.baseHp / baseMaxHp(), P_ORANGE);
    std::string next = T2("Horde {0} in {1}", std::to_string(host.hordeNum), hordeCountdown());
    R::text(next, 188, 5, pal(hordeCountdownColor()));
    // One chip per player: colour, name, money and health.
    float cx = 6, cy = 18;
    for (int k = 0; k < Local::MAX_SEATS; k++) {
        if (!Local::used(k)) continue;
        Local::with(k, [&] {
            const Profile& p = G.prof;
            std::string nm = Coop::player(k).name;
            if (nm.size() > 10) nm = nm.substr(0, 10);
            std::string money = "$" + std::to_string(p.money);
            float cw = 20 + R::textWidth(nm) + 6 + R::textWidth(money) + 40;
            R::rect(cx, cy, cw, 14, pal(P_DARK, 0.8f));
            R::rect(cx + 2, cy + 3, 8, 8, pal(Local::colorOf(k)));
            R::text(nm, cx + 13, cy + 4, pal(Local::colorOf(k)));
            float mx = cx + 13 + R::textWidth(nm) + 6;
            R::text(money, mx, cy + 4, pal(P_YGREEN));
            UI::bar(mx + R::textWidth(money) + 5, cy + 5, 30, 4, p.hp / p.maxHp(), P_CORAL);
            cx += cw + 4;
        });
    }
    // Each player's station prompt, in the buttons of what they are holding.
    int owner = Local::uiOwner();
    for (int k = 0; k < Local::MAX_SEATS; k++) {
        if (!Local::used(k) || owner >= 0) continue;
        Local::with(k, [&] {
            int st = nearestStation(G.player.pos);
            if (G.panel == Panel::None && st >= 0) stationPrompt(st, cam);
        });
    }
    if (G.noticeT > 0 && !G.notice.empty()) {
        R::rect(W / 2 - R::textWidth(G.notice) / 2 - 6, H - 34, R::textWidth(G.notice) + 12, 14, pal(P_DARK, 0.9f));
        R::textCentered(G.notice, W / 2, H - 30, pal(P_WHITE), 1, false);
    }
    if (owner >= 0) {
        Local::with(owner, [&] {
            // Whose menu this is.
            std::string who = T1("{0}'s menu", Coop::player(owner).name);
            R::rect(W - 8 - R::textWidth(who) - 8, H - 16, R::textWidth(who) + 12, 13, pal(P_DARK, 0.85f));
            R::text(who, W - 8 - R::textWidth(who) - 2, H - 13, pal(Local::colorOf(owner)));
            drawBasePanel(W, H);
        });
    } else {
        std::string hint = Local::joinHint();
        if (!hint.empty()) R::text(hint, 6, H - 12, pal(P_LAVENDER, 0.8f));
    }
    if (s_sleepFade > 0) R::rect(0, 0, W, H, pal(P_DARK, clampf(s_sleepFade, 0, 1)));
    base_drawHordeCall();
}

void panelBed(float W, float H) {
    Profile& p = G.prof;
    float hordeClock = p.nextHordeAt - (p.day - 1) * 1440.0f;   // on today's clock (may pass 24:00)
    if (Coop::online()) {
        // Co-op: the night only passes when every player is lying down.
        bool early = hordeBeforeMorning();
        float w = 260, h = 70 + Coop::MAX_PLAYERS * 11 + (early ? 12 : 0), x = std::floor(W / 2 - w / 2), y = std::floor(H / 2 - h / 2);
        UI::panel(x, y, w, h, T("BED"));
        R::text(T("Everyone has to be in bed to sleep."), x + 10, y + 20, pal(P_WHITE));
        if (early) {
            R::text(T1("Horde at {0}: it will wake you all to fight.", fmtTime(hordeClock)), x + 10, y + h - 50, pal(P_CORAL));
        }
        int in = 0, total = 0;
        for (int i = 0; i < Coop::MAX_PLAYERS; i++) {
            const Coop::NetPlayer& np = Coop::player(i);
            if (!np.used) continue;
            bool bed = i == Coop::localSlot() ? Coop::localInBed() : np.inBed;
            total++;
            in += bed ? 1 : 0;
            float ry = y + 32 + (total - 1) * 11;
            R::rect(x + 10, ry + 1, 6, 6, pal(Coop::colorPal(i)));
            R::text(np.name, x + 20, ry, pal(Coop::colorPal(i)));
            const char* st = bed ? "in bed" : np.where == Coop::W_RAID && !p.rivals ? "outside" : "awake";
            R::text(T(st), x + w - 70, ry, pal(bed ? P_YGREEN : P_LAVENDER));
        }
        R::text(T2("{0}/{1} in bed", std::to_string(in), std::to_string(total)), x + 10, y + h - 38, pal(P_BEIGE));
        if (UI::button(x + 10, y + h - 24, 110, 16, Coop::localInBed() ? T("Get up") : T("Lie down"))) Coop::setInBed(!Coop::localInBed());
        if (UI::button(x + w - 105, y + h - 24, 95, 16, T("Close")) || UI::panelClose()) { Coop::setInBed(false); G.panel = Panel::None; }
        return;
    }
    if (hordeBeforeMorning()) {
        // A horde lands before morning. Nobody sleeps through that: rest until they
        // come, then go up and meet them. The bed is yours once they are beaten.
        float w = 268, h = 108, x = std::floor(W / 2 - w / 2), y = std::floor(H / 2 - h / 2);
        UI::panel(x, y, w, h, T("BED"));
        R::text(T2("Horde {0} is coming at {1}.", std::to_string(p.hordeNum), fmtTime(hordeClock)), x + 10, y + 22, pal(P_CORAL));
        UI::textWrap(T("You cannot sleep through it. Rest until they come, then go out and hold the bunker."), x + 10, y + 36, w - 20, pal(P_WHITE));
        R::text(T2("It is {0} on day {1}.", fmtTime(p.timeMin), std::to_string(p.day)), x + 10, y + 62, pal(P_LAVENDER));
        if (UI::button(x + 10, y + h - 24, 130, 16, T("Rest until they come"))) {
            G.panel = Panel::None;
            // Local co-op: the whole room rests (the clock is the save's).
            Local::atHome([hordeClock] {
                Profile& hp = G.prof;
                hp.timeMin = std::max(hp.timeMin, hordeClock);
                Local::forEach([](int) { G.prof.hp = G.prof.maxHp(); G.panel = Panel::None; });
                s_sleepFade = 1.5f;
                save_game();
            });
            return;
        }
        if (UI::button(x + w - 105, y + h - 24, 95, 16, T("Cancel"))) G.panel = Panel::None;
        return;
    }
    float w = 244, h = 96, x = std::floor(W / 2 - w / 2), y = std::floor(H / 2 - h / 2);
    UI::panel(x, y, w, h, T("BED"));
    R::text(T("Sleep until 06:00 tomorrow?"), x + 10, y + 22, pal(P_WHITE));
    R::text(T("Restores health and saves the game."), x + 10, y + 34, pal(P_BEIGE));
    R::text(T2("It is {0} on day {1}.", fmtTime(p.timeMin), std::to_string(p.day)), x + 10, y + 46, pal(P_LAVENDER));
    if (UI::button(x + 10, y + h - 24, 95, 16, T("Sleep"))) {
        G.panel = Panel::None;
        Local::atHome([] { soloNewDay(false); });
    }
    if (UI::button(x + w - 105, y + h - 24, 95, 16, T("Cancel"))) G.panel = Panel::None;
}

// Solo: the day turns over, by sleeping or (Hardcore, whose clock never stops) because
// the night ran out while you were down here awake.
void soloNewDay(bool nightRanOut) {
    Profile& p = G.prof;
    {
        // The rest of today passes while you sleep. A horde due before dark comes
        // anyway, and the bunker has to hold without you.
        std::string missed = raid_missedHorde();
        p.dayMem.clear();
        p.day++;              // a new day starts on its first layout again
        p.dayRev = 0;
        rollDailyMission();
        p.timeMin = DAY_START_MIN;
        p.hp = p.maxHp();
        // A night's work: the bunker is patched up, standing turrets get half their
        // plating back, and your people are back on their feet. Wrecks stay wrecked.
        p.baseHp = baseMaxHp();
        for (Turret& t : p.turrets) {
            float mx = turretStats(t).maxHp;
            if (t.hp > 0) t.hp = std::min(mx, t.hp + mx * 0.5f);
        }
        for (Hireling& h : p.squad) if (!h.dead) h.hp = h.maxHp();
        if (p.nextHordeAt < absMinutes()) rollNextHorde();
        // Local co-op: everyone slept. Their own contracts, health and hired guns.
        if (Local::active())
            for (int k = 1; k < Local::MAX_SEATS; k++)
                if (Local::used(k))
                    Local::with(k, [] {
                        Profile& sp = G.prof;
                        sp.dayMem.clear();
                        rollDailyMission();
                        sp.hp = sp.maxHp();
                        for (Hireling& h : sp.squad) if (!h.dead) h.hp = h.maxHp();
                        G.panel = Panel::Mission;
                    });
        save_game();
        s_sleepFade = 1.5f;
        G.panel = Panel::Mission;
        if (!missed.empty()) {
            G.summary = RaidSummary();
            G.summary.dayAfter = p.day;
            G.summary.hordeNote = missed;
            G.summary.slept = true;
            G.panel = Panel::Summary;
        }
        setNotice(T1("Day {0}: choose a new contract.", std::to_string(p.day)));
        if (nightRanOut) pushMessage(T("You never slept, and the night ran out. A new day begins."), P_LAVENDER);
        Audio::play(Snd::heal, 0.6f, 0.7f);
    }
}

// Hardcore: time never stops, not even down here. Solo only (in co-op the host's
// clock runs it, see Coop::update). Called by the bunker and the defense console.
void base_hardcoreClock(float dt) {
    Profile& p = G.prof;
    if (!p.hardcore() || Coop::online()) return;
    // The pause menu really pauses (solo); everything else lets the clock run.
    if (G.panel == Panel::Pause || G.panel == Panel::Options || G.panel == Panel::Controls || G.panel == Panel::QuitConfirm) return;
    p.timeMin += GAME_MINUTES_PER_SEC * dt;
    if (p.timeMin >= 1440 + DAY_START_MIN) {
        if (G.scene == Scene::Defense) base_enter(false);
        soloNewDay(true);
    }
}

// Co-op host: everybody is in bed (or the night ran out). The day turns over for the
// whole session; coop.cpp tells the guests.
std::string base_coopSleep(bool nightRanOut) {
    Profile& p = G.prof;
    p.dayMem.clear();
    p.day++;
    p.dayRev = 0;
    rollDailyMission();
    p.timeMin = DAY_START_MIN;
    p.hp = p.maxHp();
    p.baseHp = baseMaxHp();
    for (Turret& t : p.turrets) {
        float mx = turretStats(t).maxHp;
        if (t.hp > 0) t.hp = std::min(mx, t.hp + mx * 0.5f);
    }
    for (Hireling& h : p.squad) if (!h.dead) h.hp = h.maxHp();
    for (int i = 1; i < Coop::MAX_PLAYERS; i++)
        if (Coop::player(i).used && Coop::player(i).prof)
            for (Hireling& h : Coop::player(i).prof->squad) if (!h.dead) h.hp = h.maxHp();
    if (p.nextHordeAt < absMinutes()) rollNextHorde();
    raid_coopEnded();          // today's outside is gone; tomorrow's is built on the first step out
    G.nightFallen = false;     // and the new morning is light for everyone (sent with the world)
    Coop::setInBed(false);
    if (G.scene == Scene::Raid) raid_forceHome();
    save_game();
    std::string note = nightRanOut ? T("Nobody slept, and the night ran out. A new day begins.") : "";
    base_coopWoke(note, nightRanOut);
    return note;
}

void base_coopWoke(const std::string& note, bool nightRanOut) {
    (void)nightRanOut;
    s_sleepFade = 1.5f;
    G.panel = Panel::Mission;
    if (!note.empty()) pushMessage(note, P_LAVENDER);
    setNotice(T1("Day {0}: choose a new contract.", std::to_string(G.prof.day)));
    Audio::play(Snd::heal, 0.6f, 0.7f);
}

// Groups a stash by kind, most valuable first, merging split stacks.
static void sortStash(std::vector<Item>& stash, int slots) {
        std::vector<Item> items;
        for (auto& it : stash) if (!it.empty()) items.push_back(it);
        std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
            const ItemDef& da = itemDef(a.id);
            const ItemDef& db = itemDef(b.id);
            auto catOrder = [](Cat c) -> int {
                switch (c) {
                case Cat::Weapon: return 0;
                case Cat::Ammo: return 1;
                case Cat::Medical: return 2;
                case Cat::Throwable: return 3;
                case Cat::Armor: return 4;
                case Cat::Backpack: return 5;
                case Cat::Valuable: return 6;
                default: return 7;
                }
            };
            int ca = catOrder(da.cat), cb = catOrder(db.cat);
            if (ca != cb) return ca < cb;
            if (da.rarity != db.rarity) return da.rarity > db.rarity;
            if (da.value != db.value) return da.value > db.value;
            return a.id < b.id;
        });
        stash.assign(slots, Item());
        for (const Item& it : items) addToSlots(stash, it, slots);
}

// The big stash is shared by every player in co-op (one of them at a time works in
// it); the small one under it is yours alone.
void panelStash(float W, float H) {
    Profile& p = G.prof;
    const int cols = 12, rows = 5, prows = (PRIVATE_STASH_SLOTS + cols - 1) / cols;
    float sw = cols * SLOT + 12, sh = 18 + rows * SLOT + 14 + prows * SLOT + 44;
    float invW = 6 * SLOT + 12;
    float total = sw + 8 + invW;
    float x = std::floor(W / 2 - total / 2), y = std::floor(H / 2 - 124);
    std::vector<Item>* shared = Coop::sharedStash();
    UI::panel(x, y, sw, sh, Coop::online() || Local::active() ? T("SHARED STASH") : T("STASH"));
    if (shared) {
        drawSlotGrid(x + 6, y + 18, *shared, STASH_SLOTS, cols, InvMode::Stash, true);
    } else {
        R::rect(x + 6, y + 18, cols * SLOT, rows * SLOT, pal(P_DARK, 0.6f));
        UI::textWrap(Coop::stashStatus(), x + 16, y + 18 + rows * SLOT / 2 - 5, cols * SLOT - 20, pal(P_LAVENDER));
    }
    float py = y + 18 + rows * SLOT + 14;
    R::text(T("PRIVATE STASH (only yours)"), x + 6, py - 11, pal(P_YELLOW));
    drawSlotGrid(x + 6, py, p.privStash, PRIVATE_STASH_SLOTS, cols, InvMode::Stash, true);

    // Where a click on something you carry puts it.
    if (!shared) s_stashToPrivate = true;
    std::vector<Item>* target = s_stashToPrivate ? &p.privStash : shared;
    int targetSlots = s_stashToPrivate ? PRIVATE_STASH_SLOTS : STASH_SLOTS;
    float by = y + sh - 38;
    R::text(T("Click stores in:"), x + 6, by + 3, pal(P_LAVENDER));
    float tw = R::textWidth(T("Click stores in:")) + 10;
    if (UI::button(x + 6 + tw, by, 60, 14, Coop::active() ? T("Shared") : T("Stash"), shared != nullptr, !s_stashToPrivate ? P_YELLOW : P_WHITE))
        s_stashToPrivate = false;
    if (UI::button(x + 70 + tw, by, 60, 14, T("Private"), true, s_stashToPrivate ? P_YELLOW : P_WHITE)) s_stashToPrivate = true;
    if (UI::button(x + 66, y + sh - 22, 116, 12, T("Store valuables"), target != nullptr)) {
        int cap = p.invCapacity();
        for (int i = 0; i < cap; i++)
            if (!p.inv[i].empty() && itemDef(p.inv[i].id).cat == Cat::Valuable) moveItem(p.inv, i, *target, targetSlots);
        Audio::play(Snd::pickup, 0.5f);
    }
    float sortW = 56, sortX = x + 6, sortY = y + sh - 22;
    if (UI::hover(sortX, sortY, sortW, 12)) UI::tooltip(T("Sort"), T("Sort the stash by kind and value"));
    if (UI::button(sortX, sortY, sortW, 12, T("Sort"))) {
        if (shared) sortStash(*shared, STASH_SLOTS);
        sortStash(p.privStash, PRIVATE_STASH_SLOTS);
        Audio::play(Snd::click, 0.5f, 1.2f);
        setNotice(T("Stash sorted."));
    }
    if (UI::button(x + sw - 66, y + sh - 22, 60, 12, T("Close")) || UI::panelClose()) G.panel = Panel::None;
    drawInventoryPanel(x + sw + 8, y, InvMode::Stash, target, targetSlots);
}

// ------------------------------------------------------------------- missions
// One mission a day, rerolled whenever the profile's day moves on. The generator is
// seeded from worldSeed ^ day so every load of the same save sees the same mission.
void rollDailyMission() {
    Profile& p = G.prof;
    DayMission m;
    m.day = p.day;
    p.mission = m;
}

// The trader's shelf price for an item on a given day, or 0 when it is not sold.
// A contract never pays more than buying its items would cost, or it would be free money.
static int shopPrice(int id, int day) {
    for (const ShopEntry& e : SHOP)
        if (e.id == id && e.minDay <= day) return e.price / std::max(1, e.count);
    return 0;
}

// Bounty targets: plural name, pay per head, first day offered.
struct BountyKind { int enemy; const char* name; int pay; int minDay; float base, perDay; };
static const BountyKind BOUNTIES[] = {
    {-1, "hostiles", 34, 1, 6, 1.2f},
    {(int)EnemyType::Scav, "Scavengers", 30, 1, 5, 0.9f},
    {(int)EnemyType::Bandit, "Bandits", 48, 2, 3, 0.6f},
    {(int)EnemyType::Heavy, "Heavies", 95, 3, 2, 0.35f},
    {(int)EnemyType::Sniper, "Snipers", 120, 4, 1, 0.25f},
};

static const char* bountyName(int enemy) {
    if (enemy == (int)EnemyType::Zombie) return "zombies";
    for (const BountyKind& b : BOUNTIES) if (b.enemy == enemy) return b.name;
    return "hostiles";
}

// Picks from ids whose rarity the day allows, leaning toward the rarer ones.
static int pickItem(Rng& rng, std::initializer_list<int> ids, int maxRarity, int avoid = IT_NONE) {
    std::vector<int> ok;
    for (int id : ids)
        if (itemDef(id).rarity <= maxRarity && id != avoid)
            for (int k = 0; k <= itemDef(id).rarity; k++) ok.push_back(id);
    if (ok.empty()) return IT_NONE;
    return ok[rng.next() % ok.size()];
}

static DayMission missionOffer(int slot) {
    Profile& p = G.prof;
    DayMission m;
    m.day = p.day;
    uint64_t dailySeed = mix64(p.worldSeed ^ p.missionSalt ^ ((uint64_t)p.day << 32) ^ 0xD1B54A32D192ED03ull);
    // Which three kinds of contract are on the board today. A horde due before
    // tomorrow morning adds the zombie cull to the pool.
    // Zombies mode has no raiders to put a bounty on: the dead are the job.
    std::vector<int> kinds = {MT_TECH, p.zombieMode() ? MT_CULL : MT_BOUNTY, MT_WEAPON, MT_SUPPLY, MT_GEAR};
    if (!p.zombieMode() && p.nextHordeAt < p.day * 1440.0f + DAY_START_MIN) kinds.push_back(MT_CULL);
    Rng orderRng(dailySeed);
    for (int i = (int)kinds.size() - 1; i > 0; --i) std::swap(kinds[i], kinds[orderRng.irange(0, i)]);
    m.type = kinds[std::clamp(slot, 0, 2)];
    Rng rng(mix64(dailySeed ^ (uint64_t)(slot + 17) * 0x9E3779B97F4A7C15ull));

    int d = p.day, tier = std::min(20, d - 1);
    int maxR = d <= 2 ? 1 : d <= 5 ? 2 : 3;
    auto countFor = [&](int id) {
        int r = itemDef(id).rarity;
        int n = r >= 3 ? 1 + tier / 9 : r == 2 ? 1 + tier / 5 : r == 1 ? 2 + tier / 3 : 3 + tier * 2 / 3;
        return std::max(1, n + rng.irange(0, r >= 2 ? 0 : 1));
    };

    switch (m.type) {
    case MT_TECH:
        m.itemId = pickItem(rng, {IT_CIRCUIT, IT_BATTERY, IT_WATCH, IT_JEWELRY, IT_GPU, IT_INTEL}, maxR);
        m.target = countFor(m.itemId);
        if (d >= 4 && rng.chance(0.6f)) {
            m.itemId2 = pickItem(rng, {IT_CIRCUIT, IT_BATTERY, IT_WATCH, IT_JEWELRY}, std::min(maxR, 2), m.itemId);
            m.target2 = m.itemId2 ? std::max(1, countFor(m.itemId2) - 1) : 0;
        }
        break;
    case MT_WEAPON: {
        std::vector<int> pool = {IT_SMG, IT_SHOTGUN};
        if (d >= 2) pool.push_back(IT_RIFLE);
        if (d >= 4) pool.push_back(IT_SNIPER);
        if (d >= 7) pool.push_back(IT_LAUNCHER);
        m.itemId = pool[rng.next() % pool.size()];
        m.target = itemDef(m.itemId).rarity <= 1 ? 1 + tier / 5 : 1 + tier / 12;
        if (d >= 5 && rng.chance(0.4f)) {
            int second = pool[rng.next() % pool.size()];
            if (second != m.itemId) { m.itemId2 = second; m.target2 = 1; }
        }
        break;
    }
    case MT_SUPPLY:
        m.itemId = pickItem(rng, {IT_WIRES, IT_TAPE, IT_FOOD, IT_BATTERY, IT_MEDSUP, IT_FUEL, IT_GUNPARTS}, maxR);
        m.target = countFor(m.itemId);
        if (d >= 3) {
            m.itemId2 = pickItem(rng, {IT_WIRES, IT_TAPE, IT_BOLTS, IT_MEDSUP, IT_FUEL, IT_GUNPARTS}, maxR, m.itemId);
            m.target2 = m.itemId2 ? countFor(m.itemId2) : 0;
        }
        break;
    case MT_GEAR: {
        std::vector<int> pool = {IT_VEST_LIGHT, IT_PACK_SMALL, IT_MEDKIT};
        if (d >= 3) { pool.push_back(IT_VEST_HEAVY); pool.push_back(IT_PACK_LARGE); }
        m.itemId = pool[rng.next() % pool.size()];
        m.target = m.itemId == IT_MEDKIT ? 2 + tier / 3 : 1 + (d >= 8 && itemDef(m.itemId).rarity <= 1 ? 1 : 0);
        break;
    }
    case MT_BOUNTY: {
        std::vector<const BountyKind*> pool;
        for (const BountyKind& b : BOUNTIES) if (b.minDay <= d) pool.push_back(&b);
        const BountyKind& b = *pool[rng.next() % pool.size()];
        m.enemy = b.enemy;
        m.target = std::max(1, (int)(b.base + b.perDay * tier) + rng.irange(0, 1));
        m.reward = (int)(b.pay * m.target * (1.0f + 0.07f * tier)) + 40 + 20 * tier;
        break;
    }
    case MT_CULL:
        m.enemy = (int)EnemyType::Zombie;
        m.target = 10 + hordeSize(p.hordeNum) / 3;
        m.reward = (int)(18 * m.target * (1.0f + 0.05f * tier)) + 60 + 20 * tier;
        break;
    }
    if (m.deliver()) {
        // Pays well over what the goods would sell for, more so as the days get
        // harder, but never above what the trader would charge for them.
        float mult = std::min(2.6f, 1.7f + 0.06f * tier);
        int value = itemDef(m.itemId).value * m.target + (m.itemId2 ? itemDef(m.itemId2).value * m.target2 : 0);
        float reward = value * mult + 50 + 25 * tier;
        int cap = 0;
        bool capped = false;
        for (int k = 0; k < 2; k++) {
            int id = k ? m.itemId2 : m.itemId, n = k ? m.target2 : m.target;
            if (!id) continue;
            int price = shopPrice(id, d);
            if (price) { capped = true; cap += (int)(price * n * 0.85f); }
            else cap += (int)(itemDef(id).value * n * mult) + 50 + 25 * tier;
        }
        if (capped) reward = std::min(reward, (float)cap);
        m.reward = std::max((int)(value * 1.25f), (int)reward);
    }
    m.reward = (m.reward + 4) / 5 * 5;
    return m;
}

int missionHave(int itemId) {
    if (itemId <= IT_NONE) return 0;
    const Profile& p = G.prof;
    int n = countInSlots(p.inv, itemId, p.invCapacity());
    for (const StashRef& s : ownStashes()) n += countInSlots(*s.v, itemId, s.slots);
    for (const Item& w : p.weapons) if (w.id == itemId) n += w.count;
    if (p.armor.id == itemId) n++;
    return n;
}

bool missionDone(const DayMission& m) {
    if (!m.active()) return false;
    if (!m.deliver()) return m.collected >= m.target;
    return missionHave(m.itemId) >= m.target && (!m.itemId2 || missionHave(m.itemId2) >= m.target2);
}

std::string missionProgress(const DayMission& m) {
    if (!m.deliver())
        return T(bountyName(m.enemy)) + " " + std::to_string(std::min(m.collected, m.target)) + "/" + std::to_string(m.target);
    std::string s = T(itemDef(m.itemId).name) + " " + std::to_string(std::min(missionHave(m.itemId), m.target)) + "/" + std::to_string(m.target);
    if (m.itemId2)
        s += "  " + T(itemDef(m.itemId2).name) + " " + std::to_string(std::min(missionHave(m.itemId2), m.target2)) + "/" + std::to_string(m.target2);
    return s;
}

// What the contract asks for, in one line: "Bring 2 Battery + 1 Fuel Can".
static std::string missionAsk(const DayMission& m) {
    if (!m.deliver()) return T2("Kill {0} {1}", std::to_string(m.target), T(bountyName(m.enemy)));
    std::string s = T("Bring") + " " + std::to_string(m.target) + " " + T(itemDef(m.itemId).name);
    if (m.itemId2) s += " + " + std::to_string(m.target2) + " " + T(itemDef(m.itemId2).name);
    return s;
}

// Takes the contract's goods: the stash first, then your pockets, then your hands.
static void takeForMission(int id, int n) {
    Profile& p = G.prof;
    for (const StashRef& s : ownStashes()) if (n > 0) n -= takeFromSlots(*s.v, id, n, s.slots);
    if (n > 0) n -= takeFromSlots(p.inv, id, n, p.invCapacity());
    for (int k = 0; k < 2 && n > 0; k++) {
        int w = k == 0 ? 1 - p.curWeapon : p.curWeapon;
        if (p.weapons[w].id == id) { p.weapons[w] = Item(); n--; }
    }
    if (n > 0 && p.armor.id == id) { p.armor = Item(); n--; }
}

static int s_lastDone = -1;   // so "mission complete" is only announced once
static void swapMissionSeat(int& lastDone) { std::swap(s_lastDone, lastDone); }

void missionAddLoot(int itemId) {
    // (Every looted item passes through here, which makes it the place to celebrate a
    // rare find.)
    if (itemDef(itemId).elite) {
        pushMessage(T1("ELITE GUN FOUND: {0}!", T(itemDef(itemId).name)), UI::eliteColor());
        Audio::play(Snd::sell, 0.8f, 1.25f);
    }
    DayMission& m = G.prof.mission;
    if (!m.active() || !m.deliver()) return;
    if (itemId != m.itemId && itemId != m.itemId2) return;
    bool done = missionDone(m);
    if (done && s_lastDone != m.day) {
        s_lastDone = m.day;
        setNotice(T("Mission complete - hand it in at the terminal."));
        pushMessage(T("Mission goods collected! Hand them in at the bunker."), P_YGREEN);
    } else if (!done) {
        pushMessage(T("Mission") + std::string(": ") + missionProgress(m), P_LAVENDER);
    }
}

void missionAddKill(int enemyType) {
    DayMission& m = G.prof.mission;
    if (!m.active() || m.deliver()) return;
    bool zombie = enemyType == (int)EnemyType::Zombie;
    if (m.type == MT_CULL ? !zombie : (zombie || (m.enemy >= 0 && m.enemy != enemyType))) return;
    int before = m.collected;
    m.collected = std::min(m.target, m.collected + 1);
    if (m.collected > before && m.collected < m.target)
        pushMessage(T("Mission") + std::string(": ") + missionProgress(m), P_LAVENDER);
    if (before < m.target && m.collected >= m.target) {
        setNotice(T("Mission complete - claim it at the terminal."));
        pushMessage(T("Mission complete!"), P_YGREEN);
    }
}

const char* missionTitle(const DayMission& m) {
    switch (m.type) {
    case MT_TECH: return "Tech salvage";
    case MT_BOUNTY: return m.enemy < 0 ? "Clear the area" : "Bounty";
    case MT_WEAPON: return "Weapon request";
    case MT_SUPPLY: return "Supply run";
    case MT_GEAR: return "Gear request";
    default: return "Horde cull";
    }
}

static const char* missionDesc(const DayMission& m) {
    switch (m.type) {
    case MT_TECH: return "A buyer wants these exact electronics.";
    case MT_BOUNTY: return m.enemy < 0 ? "Thin out the raiders in the zone." : "Hunt down this kind of raider.";
    case MT_WEAPON: return "Someone needs this exact gun.";
    case MT_SUPPLY: return "The settlements are short of these.";
    case MT_GEAR: return "A crew heading out needs this kit.";
    default: return "Kill zombies when the next horde hits.";
    }
}

// Panels ---------------------------------------------------------------------
void panelSummary(float W, float H) {
    Profile& p = G.prof;
    const RaidSummary& s = G.summary;
    bool note = !s.hordeNote.empty();
    if (s.slept) {
        float w = 360, h = 84, x = std::floor(W / 2 - w / 2), y = std::floor(H / 2 - h / 2);
        UI::panel(x, y, w, h, T("WHILE YOU SLEPT"));
        R::text(s.hordeNote, x + 10, y + 24, pal(P_WHITE));
        R::text(T1("Money ${0}", std::to_string(p.money)), x + 10, y + 40, pal(P_YGREEN));
        if (UI::button(x + w / 2 - 50, y + h - 24, 100, 16, T("Continue"))) G.panel = Panel::Mission;
        return;
    }
    float w = note ? 360 : 300, h = note ? 184 : 158, x = std::floor(W / 2 - w / 2), y = std::floor(H / 2 - h / 2);
    UI::panel(x, y, w, h, s.died ? T("YOU DIED") : T("BACK HOME"));
    if (note) {
        R::text(T("While you were gone:"), x + 10, y + 116, pal(P_ORANGE));
        R::text(s.hordeNote, x + 10, y + 128, pal(P_WHITE));
    }
    if (s.died) R::text(T(s.cause.c_str()), x + 10, y + 22, pal(P_CORAL));
    if (s.died && s.carried > 0)
        R::text(T2("Lost {0} of the {1} things you carried.", std::to_string(s.lost), std::to_string(s.carried)), x + 10, y + 31, pal(P_ORANGE));
    else R::text(T("You made it back to the bunker."), x + 10, y + 22, pal(P_LGREEN));
    R::text(T1("Kills this raid: {0}", std::to_string(s.kills)), x + 10, y + 40, pal(P_WHITE));
    R::text(T1("Carried out: ${0}", std::to_string(s.value)), x + 10, y + 54, pal(P_YGREEN));
    R::text(T1("Time outside: {0}", fmtTime((int)s.minutes)), x + 10, y + 68, pal(P_LAVENDER));
    R::text(T1("Day {0} begins.", std::to_string(s.dayAfter)), x + 10, y + 86, pal(P_YELLOW));
    R::text(T1("Money ${0}", std::to_string(p.money)), x + 10, y + 100, pal(P_YGREEN));
    if (UI::button(x + w / 2 - 50, y + h - 24, 100, 16, T("Continue"))) G.panel = Panel::None;
}

void panelExit(float W, float H) {
    Profile& p = G.prof;
    float w = 290, h = 116, x = std::floor(W / 2 - w / 2), y = std::floor(H / 2 - h / 2);
    UI::panel(x, y, w, h, T("HEAD OUTSIDE"));
    bool nightHorde = hordeDueTonight() || (p.timeMin >= CURFEW_MIN && p.safeNight == p.day);
    bool tooLate = p.timeMin >= LATEST_DEPART_MIN && !nightHorde;
    if (nightHorde && p.timeMin >= LATEST_DEPART_MIN) {
        if (hordeDueTonight())
            R::text(T1("A horde is due tonight at {0}.", fmtTime(p.nextHordeAt - (p.day - 1) * 1440.0f)), x + 10, y + 22, pal(P_ORANGE));
        else
            R::text(T("Tonight's horde is beaten. The night is quiet."), x + 10, y + 22, pal(P_YGREEN));
        R::text(T1("Current time: {0}", fmtTime((int)p.timeMin)), x + 10, y + 34, pal(P_LAVENDER));
        R::text(T("The dead can still die until it is over."), x + 10, y + 46, pal(P_YELLOW));
    } else if (tooLate) {
        R::text(T1("It is {0}. Night is coming.", fmtTime((int)p.timeMin)), x + 10, y + 22, pal(P_CORAL));
        UI::textWrap(T("Going out now is suicide. Sleep in the bed first."), x + 10, y + 36, w - 20, pal(P_BEIGE));
    } else {
        R::text(T("The zone out there is waiting."), x + 10, y + 22, pal(P_WHITE));
        R::text(T1("Current time: {0}", fmtTime((int)p.timeMin)), x + 10, y + 34, pal(P_LAVENDER));
        R::text(T("Be back at the hatch before 22:00."), x + 10, y + 46, pal(P_YELLOW));
        if (p.weapons[0].empty() && p.weapons[1].empty())
            R::text(T("Warning: you have no weapon!"), x + 10, y + 62, pal(P_CORAL));
        else if (p.hp < p.maxHp() * 0.5f)
            R::text(T("Warning: you are badly hurt."), x + 10, y + 62, pal(P_CORAL));
    }
    int yn = UI::yesNo(x + w - 33, y + 4);
    if (UI::button(x + 10, y + h - 24, 110, 16, T("Go outside"), !tooLate) || (yn == 1 && !tooLate)) {
        G.panel = Panel::None;
        raid_start();
        return;
    }
    if (UI::button(x + w - 120, y + h - 24, 110, 16, T("Stay")) || yn == 2) G.panel = Panel::None;
}

// Looted goods go to the pockets first, then overflow into the stash, so the trader
// can always hand you a purchase. Returns false only when both are full.
bool giveItem(const Item& it) {
    Profile& p = G.prof;
    int left = addToSlots(p.inv, it, p.invCapacity());
    if (left > 0) {
        Item rest = it;
        for (const StashRef& s : ownStashes()) {
            if (left <= 0) break;
            rest.count = (int16_t)left;
            left = addToSlots(*s.v, rest, s.slots);
        }
        if (left > 0) return false;
        setNotice(T("Inventory full - sent to stash."));
    }
    return true;
}

void panelTrader(float W, float H) {
    Profile& p = G.prof;
    float lw = 252, lh = 18 + SHOP_PER_PAGE * 20 + 44;
    float invW = 6 * SLOT + 12;
    float x = std::floor(W / 2 - (lw + 8 + invW) / 2), y = std::floor(H / 2 - 124);
    UI::panel(x, y, lw, lh, T("TRADER - BUY"));
    std::string money = "$" + std::to_string(p.money);
    R::text(money, x + lw - 6 - R::textWidth(money), y + 4, pal(P_YGREEN));

    // Hardcore: the map, the home beacon and the team radio are for sale until bought,
    // at the top of the list (as -1 - HardcoreUnlock).
    std::vector<int> visible;
    if (!p.hasLocator() && p.day >= CRYPT_DAY) visible.push_back(-100);   // today's dungeon locator
    if (p.hardcore())
        for (int i = 0; i < HC_COUNT; i++)
            if (!p.hcUnlock[i] && (i != HC_TEAM || Coop::active())) visible.push_back(-1 - i);
    for (int i = 0; i < SHOP_COUNT; i++)
        if (p.day >= SHOP[i].minDay) visible.push_back(i);
    int pages = std::max(1, ((int)visible.size() + SHOP_PER_PAGE - 1) / SHOP_PER_PAGE);
    G.traderPage = std::clamp(G.traderPage, 0, pages - 1);
    for (int row = 0; row < SHOP_PER_PAGE; row++) {
        int vi = G.traderPage * SHOP_PER_PAGE + row;
        if (vi >= (int)visible.size()) break;
        float ry = y + 18 + row * 20;
        if (visible[vi] == -100) {
            int price = p.locatorPrice();
            UI::itemSlot(x + 6, ry, Item());
            Vec2 ic(x + 6 + SLOT / 2.0f, ry + SLOT / 2.0f);
            R::circle(ic, 6, pal(P_DARK));
            R::circle(ic, 5, pal(P_ORANGE, 0.35f));
            R::sprite(ARROW, ic, -PI / 3, 0.7f, pal(P_ORANGE));
            R::text(T("Dungeon locator"), x + 30, ry + 3, pal(P_ORANGE));
            R::text("$" + std::to_string(price) + "  " + T("today only"), x + 30, ry + 11, pal(p.money >= price ? P_YGREEN : P_CORAL));
            if (UI::hover(x + 6, ry, lw - 72, 18))
                UI::tooltip(T("Dungeon locator"), T("For today: an arrow at the edge of your screen points to the nearest catacomb, and the map shows where every one of them is."), P_ORANGE);
            if (UI::button(x + lw - 62, ry + 3, 56, 14, T("Buy"), p.money >= price)) {
                p.money -= price;
                p.locatorDay = p.day;
                Audio::play(Snd::sell, 0.7f, 0.9f);
                setNotice(T("Dungeon locator bought: it points to today's catacombs."));
                save_game();
            }
            continue;
        }
        if (visible[vi] < 0) {
            int id = -1 - visible[vi];
            const HardcoreUnlockDef& hd = hardcoreUnlockDef(id);
            UI::itemSlot(x + 6, ry, Item());
            Vec2 ic(x + 6 + SLOT / 2.0f, ry + SLOT / 2.0f);
            if (id == HC_MAP) {
                R::rect(ic.x - 6, ic.y - 5, 12, 10, pal(P_BEIGE));
                R::rectOutline(ic.x - 6, ic.y - 5, 12, 10, pal(P_TAN));
                R::line(ic + Vec2(-4, 2), ic + Vec2(3, -3), 1, pal(P_CORAL));
            } else if (id == HC_HOME) {
                R::sprite(HOME_ICON, ic);
            } else {
                R::sprite(ARROW, ic, -PI / 4, 0.8f, pal(P_MINT));
            }
            R::text(T(hd.name), x + 30, ry + 3, pal(P_CORAL));
            R::text("$" + std::to_string(hd.cost) + "  " + T("Hardcore"), x + 30, ry + 11, pal(p.money >= hd.cost ? P_YGREEN : P_CORAL));
            if (UI::hover(x + 6, ry, lw - 72, 18)) UI::tooltip(T(hd.name), T(hd.desc), P_CORAL);
            if (UI::button(x + lw - 62, ry + 3, 56, 14, T("Buy"), p.money >= hd.cost)) {
                p.money -= hd.cost;
                p.hcUnlock[id] = true;
                Audio::play(Snd::sell, 0.7f, 0.9f);
                setNotice(T1("Bought {0}.", T(hd.name)));
                save_game();
            }
            continue;
        }
        const ShopEntry& e = SHOP[visible[vi]];
        Item preview = makeItem(e.id, e.count);
        UI::itemSlot(x + 6, ry, preview);
        std::string name = T(itemDef(e.id).name);
        if (e.count > 1) name += " x" + std::to_string(e.count);
        R::text(name, x + 30, ry + 3, pal(P_WHITE));
        R::text("$" + std::to_string(e.price), x + 30, ry + 11, pal(p.money >= e.price ? P_YGREEN : P_CORAL));
        if (UI::button(x + lw - 62, ry + 3, 56, 14, T("Buy"), p.money >= e.price)) {
            if (giveItem(preview)) {
                p.money -= e.price;
                Audio::play(Snd::sell, 0.5f, 0.8f);
                setNotice(T1("Bought {0}.", itemLabel(preview)));
            } else {
                setNotice(T("No space in inventory or stash."));
            }
        }
    }
    float by = y + lh - 42;
    if (UI::button(x + 6, by, 20, 14, "<", G.traderPage > 0)) G.traderPage--;
    R::textCentered(std::to_string(G.traderPage + 1) + "/" + std::to_string(pages), x + 42, by + 4, pal(P_LAVENDER), 1, false);
    if (UI::button(x + 58, by, 20, 14, ">", G.traderPage < pages - 1)) G.traderPage++;
    // A controller turns the pages with its shoulder buttons.
    if (int pg = Input::padPagePressed()) {
        int np = std::clamp(G.traderPage + pg, 0, pages - 1);
        if (np != G.traderPage) { G.traderPage = np; Audio::play(Snd::click, 0.4f, 1.2f); }
    }
    if (Input::usingPad() && pages > 1) {
        Prompt::icon(Prompt::PagePrev, x + 82, by - 1);
        Prompt::icon(Prompt::PageNext, x + 100, by - 1);
    }

    // Only what you carry: the stash is what you are keeping (0.11v). Gun parts are
    // never sold this way either, they are what the crafter works with.
    auto sellable = [](const Item& it) { return !it.empty() && itemDef(it.id).cat == Cat::Valuable && it.id != IT_GUNPARTS; };
    int cap = p.invCapacity();
    // What today's contract still needs stays in your pockets (0.11v): as many as it asks
    // for, less what is already in the stash or your hands.
    std::vector<int> keep(IT_COUNT, 0);
    const DayMission& cm = p.mission;
    if (cm.active() && cm.deliver()) {
        auto hold = [&](int id, int target) {
            if (id <= IT_NONE || id >= IT_COUNT || target <= 0) return;
            int pocket = countInSlots(p.inv, id, cap);
            int elsewhere = missionHave(id) - pocket;
            keep[id] += std::clamp(target - elsewhere, 0, pocket);
        };
        hold(cm.itemId, cm.target);
        hold(cm.itemId2, cm.target2);
    }
    // Goes through the pockets once: what it would sell for, and (apply) sells it.
    auto sellPockets = [&](bool apply) {
        std::vector<int> left = keep;
        int total = 0;
        for (int i = 0; i < cap; i++) {
            Item& it = p.inv[i];
            if (!sellable(it)) continue;
            int held = std::min<int>(left[it.id], it.count);
            left[it.id] -= held;
            Item sold = it;
            sold.count = (int16_t)(it.count - held);
            if (sold.count <= 0) continue;
            total += itemValue(sold);
            if (apply) {
                if (held > 0) it.count = (int16_t)held;
                else it = Item();
            }
        }
        return total;
    };
    int valuables = sellPockets(false);
    bool keeping = false;
    for (int k : keep) keeping = keeping || k > 0;
    if (UI::hover(x + 6, by + 20, lw - 60, 16))
        UI::tooltip(T("Sell all valuables"), keeping ? T("Sells the valuables in your pockets, except what your contract still needs. Your stash and your gun parts are left alone.")
                                                     : T("Sells the valuables in your pockets. Your stash and your gun parts are left alone."));
    if (UI::button(x + 6, by + 20, lw - 60, 16, T1("Sell all valuables (${0})", std::to_string(valuables)), valuables > 0, P_YGREEN)) {
        sellPockets(true);
        p.money += valuables;
        p.earned += valuables;
        Audio::play(Snd::sell, 0.8f);
        setNotice(keeping ? T1("Sold valuables for ${0}. Kept what your contract needs.", std::to_string(valuables))
                          : T1("Sold valuables for ${0}.", std::to_string(valuables)));
    }
    if (UI::button(x + lw - 62, by + 20, 56, 16, T("Close")) || UI::panelClose()) G.panel = Panel::None;
    drawInventoryPanel(x + lw + 8, y, InvMode::Trader, nullptr, 0);
}

void panelWorkbench(float W, float H) {
    Profile& p = G.prof;
    float w = 300, h = 18 + UP_COUNT * 26 + 26;
    float x = std::floor(W / 2 - w / 2), y = std::floor(H / 2 - h / 2);
    UI::panel(x, y, w, h, T("WORKBENCH - UPGRADES"));
    std::string money = "$" + std::to_string(p.money);
    R::text(money, x + w - 6 - R::textWidth(money), y + 4, pal(P_YGREEN));
    for (int i = 0; i < UP_COUNT; i++) {
        const UpgradeDef& d = upgradeDef(i);
        float ry = y + 18 + i * 26;
        R::rect(x + 4, ry, w - 8, 24, pal(i % 2 ? P_DARK : P_PURPLE, i % 2 ? 1.0f : 0.35f));
        R::text(T(d.name), x + 8, ry + 3, pal(P_WHITE));
        R::text(T(d.desc), x + 8, ry + 13, pal(P_BEIGE));
        for (int l = 0; l < d.maxLevel; l++) {
            float px = x + 100 + l * 7;
            R::rect(px, ry + 4, 5, 5, pal(l < p.up[i] ? P_YELLOW : P_PURPLE));
        }
        if (p.up[i] >= d.maxLevel) {
            R::text(T("MAX"), x + w - 44, ry + 8, pal(P_YELLOW));
        } else {
            int cost = upgradeCost(i, p.up[i]);
            bool available = i != UP_LASER || p.laserOwned;
            if (UI::button(x + w - 76, ry + 4, 70, 16, available ? "$" + std::to_string(cost) : T("LOCKED"), available && p.money >= cost, P_YGREEN)) {
                p.money -= cost;
                p.up[i]++;
                if (i == UP_VITALITY) p.hp += 20;
                Audio::play(Snd::sell, 0.7f, 0.6f);
                setNotice(T2("{0} upgraded to level {1}.", T(d.name), std::to_string(p.up[i])));
                save_game();
            }
        }
    }
    if (UI::button(x + w / 2 - 40, y + h - 22, 80, 16, T("Close")) || UI::panelClose()) G.panel = Panel::None;
}

// Draws the goods a contract asks for as icons, each with how many you have.
static void drawMissionGoods(const DayMission& m, float x, float y, bool showHave) {
    for (int k = 0; k < 2; k++) {
        int id = k ? m.itemId2 : m.itemId, n = k ? m.target2 : m.target;
        if (!id) continue;
        UI::itemIcon(id, x, y, 16);
        std::string s = (showHave ? std::to_string(std::min(missionHave(id), n)) + "/" : std::string("x")) + std::to_string(n);
        bool ok = missionHave(id) >= n;
        R::text(s, x + 18, y + 5, pal(showHave ? (ok ? P_YGREEN : P_LAVENDER) : P_WHITE));
        x += 26 + R::textWidth(s);
    }
}

void panelMission(float W, float H) {
    Profile& p = G.prof;
    float w = 360, h = 190, x = std::floor(W / 2 - w / 2), y = std::floor(H / 2 - h / 2);
    UI::panel(x, y, w, h, T("MISSIONS"));
    DayMission& m = p.mission;
    if (!m.active() && !m.claimed) {
        R::text(T1("Choose one contract for day {0}.", std::to_string(p.day)), x + 10, y + 21, pal(P_BEIGE));
        for (int i = 0; i < 3; ++i) {
            DayMission o = missionOffer(i);
            float ry = y + 34 + i * 42;
            if (UI::button(x + 10, ry, w - 20, 38, "")) { p.mission = o; save_game(); }
            bool hov = UI::hover(x + 10, ry, w - 20, 38);
            R::text(T(missionTitle(o)), x + 16, ry + 4, pal(hov ? P_DARK : P_YELLOW));
            std::string pay = "$" + std::to_string(o.reward);
            R::text(pay, x + w - 16 - R::textWidth(pay), ry + 4, pal(hov ? P_DARK : P_YGREEN));
            R::text(missionAsk(o), x + 16, ry + 14, pal(hov ? P_DARK : P_WHITE));
            if (o.deliver()) drawMissionGoods(o, x + 16, ry + 22, false);
            else R::text(T(missionDesc(o)), x + 16, ry + 26, pal(hov ? P_DARK : P_LAVENDER));
        }
    } else if (m.claimed) {
        // Done and paid (0.11v): a finished card, not the counters of a contract that is
        // over (they read 0/2 once the goods were handed over).
        R::text(T("CONTRACT COMPLETE"), x + 10, y + 22, pal(P_YGREEN));
        std::string pay = "+$" + std::to_string(m.reward);
        R::text(pay, x + w - 10 - R::textWidth(pay), y + 22, pal(P_YGREEN));
        R::text(T(missionTitle(m)), x + 10, y + 40, pal(P_LAVENDER));
        R::text((m.deliver() ? T("Delivered") : T("Done")) + ": " + missionAsk(m).substr(m.deliver() ? T("Bring").size() + 1 : 0), x + 10, y + 54, pal(P_BEIGE));
        R::text(T("Contract complete. Sleep for new offers."), x + 10, y + 108, pal(P_YGREEN));
    } else {
        bool done = missionDone(m);
        R::text(T(missionTitle(m)), x + 10, y + 22, pal(P_YELLOW));
        std::string pay = "$" + std::to_string(m.reward);
        R::text(pay, x + w - 10 - R::textWidth(pay), y + 22, pal(P_YGREEN));
        R::text(T(missionDesc(m)), x + 10, y + 36, pal(P_BEIGE));
        R::text(missionAsk(m), x + 10, y + 50, pal(P_WHITE));
        if (m.deliver()) {
            drawMissionGoods(m, x + 10, y + 62, true);
            if (!m.claimed)
                R::text(T("Counts what is in your stash, pockets and hands."), x + 10, y + 84, pal(P_LAVENDER));
        } else {
            R::text(T("Progress") + std::string(": ") + missionProgress(m), x + 10, y + 66, pal(done ? P_YGREEN : P_LAVENDER));
        }
        if (!m.claimed) {
            const char* label = m.deliver() ? "Hand over" : "Claim reward";
            if (UI::button(x + 10, y + 102, 130, 18, T(label), done, P_YGREEN)) {
                if (m.deliver()) {
                    takeForMission(m.itemId, m.target);
                    if (m.itemId2) takeForMission(m.itemId2, m.target2);
                }
                p.money += m.reward; p.earned += m.reward; p.missionsCompleted++; m.claimed = true;
                if (p.missionsCompleted >= 3 && !p.laserUnlocked) p.laserUnlocked = true;   // before 0.11v: opened the trader's laser
                else setNotice(T1("Contract paid: ${0}", std::to_string(m.reward)));
                save_game(); Audio::play(Snd::sell, 0.8f);
            }
            if (!done) R::text(m.deliver() ? T("Bring the goods back to hand them over.") : T("Not finished yet."), x + 148, y + 107, pal(P_LAVENDER));
        } else {
            R::text(T("Contract complete. Sleep for new offers."), x + 10, y + 108, pal(P_YGREEN));
        }
    }
    if (UI::button(x + w / 2 - 40, y + h - 22, 80, 16, T("Close")) || UI::panelClose()) G.panel = Panel::None;
}

void panelPause(float W, float H) {
    float w = 200, h = 132, x = std::floor(W / 2 - w / 2), y = std::floor(H / 2 - h / 2);
    UI::panel(x, y, w, h, T("PAUSED"));
    if (UI::button(x + 10, y + 22, w - 20, 16, T("Resume"))) G.panel = Panel::None;
    if (UI::button(x + 10, y + 42, w - 20, 16, T("Controls"))) G.panel = Panel::Controls;
    if (UI::button(x + 10, y + 62, w - 20, 16, T("Options"))) G.panel = Panel::Options;
    if (Coop::online()) {
        if (UI::button(x + 10, y + h - 22, w - 20, 16, Coop::host() ? T("End session (saves)") : T("Leave the game"), true, P_CORAL)) {
            G.panel = Panel::None;
            Coop::leave("");
        }
        return;
    }
    // Local co-op: a joined player can drop out from their own pause menu.
    if (Local::active() && Local::current() != 0) {
        if (UI::button(x + 10, y + h - 22, w - 20, 16, T("Leave (drop out)"), true, P_CORAL)) {
            G.panel = Panel::None;
            Local::leaveSeat(Local::current());
        }
        return;
    }
    if (Local::active() && UI::button(x + 10, y + 82, w - 20, 16, T("Everyone else out"), true, P_ORANGE)) {
        G.panel = Panel::None;
        Local::end();
    }
    if (UI::button(x + 10, y + h - 22, w - 20, 16, T("Quit to menu"))) {
        G.panel = Panel::None;
        Local::atHome([] {
            if (Local::active()) Local::end();
            save_game();
            G.scene = Scene::Menu;
            menu_init();
        });
    }
}


// ------------------------------------------------------------------- entry
// Coming home from a raid (or starting fresh): stand the player at the hatch and
// hand them the day's mission. Sleeping is what rerolls it, so on the way in we only
// roll when the profile has no mission for the current day yet.
void base_enter(bool fromRaid) {
    // A co-op host's outside keeps running for whoever is still out there.
    bool keepWorld = Coop::host() && raid_worldLive() && !Coop::local();
    G.scene = Scene::Base;
    if (!keepWorld) {
        G.enemies.clear();
        G.bullets.clear();
        G.grenades.clear();
    }
    G.particles.clear();
    G.decals.clear();
    G.floatTexts.clear();
    G.flashes.clear();
    G.shake = 0;
    if (!Coop::active()) G.nightFallen = false;
    G.warnStage = 0;
    buildBase();
    resetTraderVisit();
    R::setZoom(1);
    baseSeatEnter(fromRaid, World::tileCenter(3, 7));
    G.cam = G.player.pos - Vec2(R::viewW() / 2.0f, R::viewH() / 2.0f);
    // Local co-op: everyone else comes down the ladder too.
    if (Local::active())
        for (int k = 1; k < Local::MAX_SEATS; k++)
            if (Local::used(k)) Local::with(k, [&] { baseSeatEnter(fromRaid, World::tileCenter(3, 7) + Vec2(k % 2 ? 14.0f : -14.0f, k >= 2 ? -12.0f : 0.0f)); });
    save_game();
}

// One player's part of coming into the bunker: stood at the ladder, the summary of
// the trip, today's contract, and their people patched up.
static void baseSeatEnter(bool fromRaid, Vec2 at) {
    Profile& p = G.prof;
    // Anyone who fell outside is off the roster before the bunker is drawn.
    p.squad.erase(std::remove_if(p.squad.begin(), p.squad.end(), [](const Hireling& h) { return h.dead || h.hp <= 0; }), p.squad.end());
    G.panel = (fromRaid && (G.summary.died || !G.summary.hordeNote.empty())) ? Panel::Summary : Panel::None;
#ifdef DEV_MISSION_PANEL_DEMO
    G.panel = Panel::Mission;
#endif
    G.lootContainer = -1;
    G.searchT = 0;
    p.inRaid = false;
    G.player = Player();
    G.player.stamina = p.maxStamina();
    // Stand on clear floor above the hatch. Spawning *on* the hatch tile put the
    // player inside the bottom bunker wall, where every direction collided and the
    // player could not move at all.
    G.player.pos = G.baseWorld.collides(at.x, at.y, 5) ? World::tileCenter(3, 7) : at;
    if (p.mission.day != p.day) rollDailyMission();
    if (p.hp <= 0) p.hp = p.maxHp();
    for (Hireling& h : p.squad) if (!h.dead) h.hp = h.maxHp();
}

// Local co-op: a player joined while everyone is in the bunker.
void base_seatJoin(Vec2 at) {
    for (int i = 0; i < 12; i++) {
        Vec2 c = at + fromAngle(i * 0.52f) * 16.0f;
        if (!G.baseWorld.collides(c.x, c.y, 5)) { at = c; break; }
    }
    baseSeatEnter(false, at);
}

void base_swapSeat(int seat) {
    BaseSeat& b = s_baseSeats[std::clamp(seat, 0, Local::MAX_SEATS - 1)];
    std::swap(s_stashToPrivate, b.stashToPrivate);
    swapCrafterSeat(b.craftSel, b.craftPage);
    swapMissionSeat(b.lastDone);
}

void base_update(float dt) {
    Profile& p = G.prof;
    G.realTime += dt;
    G.noticeT -= dt;
    s_sleepFade -= dt;
    for (auto& m : G.messages) m.life -= dt;
    while (!G.messages.empty() && G.messages.front().life <= 0) G.messages.erase(G.messages.begin());

    base_hardcoreClock(dt);
    if (base_hordeCall(dt)) return;
    Audio::setAmbient(Audio::AMB_DRIP, 0.18f, 0.7f);   // the bunker's pipes (0.11v)
    if (Local::active()) {
        // Local co-op: the trader comes to player 1 (it is their save); then everyone
        // has their turn, in their own controls.
        Local::with(0, [&] { updateTraderVisit(dt); });
        if (G.panel != Panel::TraderTalk)
            Local::forEach([&](int k) {
                int owner = Local::uiOwner();
                Input::setPadMenu(G.panel != Panel::None);
                if (Local::justJoined()) return;
                Panel before = G.panel;
                baseSeatTick(dt, k);
                // One menu at a time: someone else was already in one.
                if (owner >= 0 && owner != k && before == Panel::None && G.panel != Panel::None) {
                    if (G.panel == Panel::Stash) Coop::stashClose();
                    G.panel = Panel::None;
                    setNotice(T1("{0} is using a menu. Wait a moment.", Coop::player(owner).name));
                }
            });
        Local::publishSeats();
        Local::runDeferred();
        if (G.scene != Scene::Base) return;
        baseCamera(dt);
        return;
    }
    updateTraderVisit(dt);
    if (G.panel == Panel::TraderTalk) return;   // nothing else while he talks
    baseSeatTick(dt, 0);
    baseCamera(dt);
    (void)p;
}

// One player's turn in the bunker: menus, walking about and the stations.
static void baseSeatTick(float dt, int seat) {
    Profile& p = G.prof;
    G.player.healCd -= dt;
    if (G.player.flashT > 0) G.player.flashT -= dt;
    // The first time in the bunker: a short tour of what everything is for.
    if (seat == 0 && !p.tutorialDone && G.panel == Panel::None && s_callT < 0) { G.panel = Panel::Tutorial; s_tutStep = 0; }
    if (Input::pressed(GLFW_KEY_ESCAPE)) {
        if (G.panel == Panel::Tutorial) { p.tutorialDone = true; G.panel = Panel::None; save_game(); }
        else if (G.panel == Panel::Summary) { G.panel = Panel::None; save_game(); }
        else if (G.panel == Panel::Controls || G.panel == Panel::Options) G.panel = Panel::Pause;
        else if (G.panel != Panel::None) G.panel = Panel::None;
        else G.panel = Panel::Pause;
    }
    if (Input::pressed(GLFW_KEY_TAB) && (G.panel == Panel::None || G.panel == Panel::Inventory))
        G.panel = G.panel == Panel::Inventory ? Panel::None : Panel::Inventory;

    int st = nearestStation(G.player.pos);
    bool openedMissionThisFrame = false;
    if (G.panel != Panel::Bed) Coop::setInBed(false);
    if (G.panel == Panel::None) {
        Vec2 in;
        if (Input::down(GLFW_KEY_W)) in.y -= 1;
        if (Input::down(GLFW_KEY_S)) in.y += 1;
        if (Input::down(GLFW_KEY_A)) in.x -= 1;
        if (Input::down(GLFW_KEY_D)) in.x += 1;
        Vec2 stick = Input::moveAxis();
        if (lengthSq(stick) > lengthSq(in)) in = stick;
        if (G.player.stamina <= 0.01f) G.player.exhausted = true;
        if (G.player.exhausted && G.player.stamina >= p.maxStamina() * 0.25f) G.player.exhausted = false;
        bool sprint = lengthSq(in) > 0 && Input::down(GLFW_KEY_LEFT_SHIFT) && !G.player.exhausted;
        float speed = 70 * p.moveMul() * (sprint ? 1.4f : 1.0f);
        if (sprint) G.player.stamina = std::max(0.0f, G.player.stamina - 26 * dt);
        else G.player.stamina = std::min(p.maxStamina(), G.player.stamina + (16 + p.up[UP_ENDURANCE] * 4) * dt);
        G.player.moving = lengthSq(in) > 0;
        if (G.player.moving) G.player.pos = G.baseWorld.move(G.player.pos, normalize(in) * speed * dt, 5);
        if (G.player.moving) {
            G.player.stepT -= dt;
            if (G.player.stepT <= 0) { G.player.stepT = 0.36f; Audio::play(Snd::step_in, 0.7f, 0.92f + 0.16f * (float)std::fmod(G.realTime * 7.3, 1.0)); }
        } else {
            G.player.stepT = 0.08f;
        }
        Vec2 camF(std::floor(G.cam.x), std::floor(G.cam.y));
        Vec2 aim = Input::aimAxis();
        if (lengthSq(aim) > 0) G.player.angle = angleOf(aim);
        else if (Input::usingPad()) { if (G.player.moving) G.player.angle = angleOf(in); }
        else G.player.angle = angleOf(camF + Input::mouse() - G.player.pos);
        if (Input::pressed(GLFW_KEY_E) && st >= 0 && STATIONS[st].panel == Panel::None) {
            if (p.rivals) {
                setNotice(T("Rivals: no hordes come for your bunker, and there is nothing to defend here."));
            } else {
                Audio::play(Snd::door, 0.6f, 1.2f);
                defense_enter();
                return;
            }
        }
        if (Input::pressed(GLFW_KEY_E) && st >= 0) {
            G.panel = STATIONS[st].panel;
            if (G.panel == Panel::Stash) Coop::stashOpen();
            openedMissionThisFrame = G.panel == Panel::Mission;
            Audio::play(Snd::click, 0.5f);
        }
        if (Input::pressed(GLFW_KEY_M)) {
            G.panel = Panel::Mission;
            openedMissionThisFrame = true;
            Audio::play(Snd::click, 0.5f);
        }
    } else if (Input::keyPressed(GLFW_KEY_E) && G.panel != Panel::Summary && G.panel != Panel::Tutorial) {
        G.panel = Panel::None;
    }
    if (!openedMissionThisFrame && G.panel == Panel::Mission &&
        (Input::pressed(GLFW_KEY_M) || Input::keyPressed(GLFW_KEY_E)))
        G.panel = Panel::None;

}

// The bunker's camera: the room, leaning a little toward the player (or, in local
// co-op, toward the middle of everyone).
static void baseCamera(float dt) {
    Vec2 focus = G.player.pos;
    Vec2 lo, hi;
    if (Local::active() && Local::groupBox(lo, hi)) focus = (lo + hi) * 0.5f;
    Vec2 screen(R::width() / 2.0f, R::height() / 2.0f);
    Vec2 center(G.baseWorld.w * TILE / 2.0f, G.baseWorld.h * TILE / 2.0f);
    Vec2 target = (focus * 0.35f + center * 0.65f) - screen;
    if (R::width() >= G.baseWorld.w * TILE) target.x = center.x - screen.x;
    if (R::height() >= G.baseWorld.h * TILE + 40) target.y = center.y - screen.y;
    G.cam = G.cam + (target - G.cam) * (1.0f - std::exp(-8.0f * dt));
}

// ------------------------------------------------------------------- recruiter
// Mercenaries, paid once. Money buys better guns, more health and steadier aim, and a
// dead hireling is gone for good.
static const char* HIRE_NAMES[] = {
    "Marco", "Dana", "Ivo", "Kasia", "Rook", "Tess", "Bram", "Nadia", "Oskar", "Lena", "Viktor", "Ines",
    "Jonah", "Mira", "Teo", "Sasha", "Pike", "Yara", "Emil", "Rhea", "Cole", "Zofia", "Hugo", "Alba",
};

static std::string nextHireName() {
    Profile& p = G.prof;
    const int n = (int)(sizeof(HIRE_NAMES) / sizeof(HIRE_NAMES[0]));
    Rng r(mix64(p.worldSeed ^ p.missionSalt ^ 0xB1DE ^ (uint64_t)p.hires));
    for (int tries = 0; tries < 40; tries++) {
        std::string name = HIRE_NAMES[r.next() % n];
        bool taken = false;
        for (const Hireling& h : p.squad) taken = taken || h.name == name;
        if (!taken) return name;
    }
    return std::string(HIRE_NAMES[p.hires % n]) + std::to_string(p.hires);
}

// ---- the crafter (0.11v): work on one gun at a time. Raising a tier is expensive in
// money and gun parts on purpose, so a good gun found outside is still worth finding;
// the best tier, legendary, is still only the elite guns'.
namespace {
int s_craftSel = 0;
int s_craftPage = 0;
}  // namespace
static void swapCrafterSeat(int& sel, int& page) { std::swap(s_craftSel, sel); std::swap(s_craftPage, page); }
namespace {

struct GunRef { Item* it; const char* where; };
std::vector<GunRef> craftableGuns() {
    Profile& p = G.prof;
    std::vector<GunRef> v;
    for (Item& w : p.weapons) if (weaponDef(w.id) && !w.empty()) v.push_back({&w, "In hand"});
    int cap = p.invCapacity();
    for (int i = 0; i < cap && i < (int)p.inv.size(); i++) if (weaponDef(p.inv[i].id) && !p.inv[i].empty()) v.push_back({&p.inv[i], "Pockets"});
    for (const StashRef& s : ownStashes())
        for (int i = 0; i < s.slots && i < (int)s.v->size(); i++) {
            Item& it = (*s.v)[i];
            if (weaponDef(it.id) && !it.empty()) v.push_back({&it, "Stash"});
        }
    return v;
}

// What the gun is worth to work on: its shop price (the gun it is a better version
// of, for an elite), or a guess from its value.
int craftBase(const Item& it) {
    int base = baseWeapon(it.id);
    for (const ShopEntry& e : SHOP) if (e.id == base) return e.price;
    return itemDef(base).value * 4;
}
struct CraftCost { int money, parts; };
CraftCost tierCost(const Item& it) {
    float b = (float)craftBase(it);
    switch (itemTier(it)) {
    case TIER_COMMON: return {std::max(250, (int)(b * 0.5f)), 1};
    case TIER_UNCOMMON: return {std::max(900, (int)(b * 1.5f)), 3};
    case TIER_RARE: return {std::max(2400, (int)(b * 3.0f)), 6};
    default: return {0, 0};
    }
}
bool canTierUp(const Item& it) { return !itemDef(it.id).elite && itemTier(it) < TIER_EPIC; }
int laserCost(const Item& it) { return std::max(450, (int)(craftBase(it) * 0.3f)); }
int extCost(const Item& it) { return std::max(350, (int)(craftBase(it) * 0.4f)); }
int drumCost(const Item& it) { return std::max(1100, (int)(craftBase(it) * 1.0f)); }
// A launcher's single round has nowhere to go.
bool magUpgradable(const Item& it) { const WeaponDef* w = weaponDef(it.id); return w && !w->explosive && w->magSize >= 4; }

int partsHave() { return missionHave(IT_GUNPARTS); }
void takeParts(int n) {
    Profile& p = G.prof;
    n -= takeFromSlots(p.inv, IT_GUNPARTS, n, p.invCapacity());
    for (const StashRef& s : ownStashes()) if (n > 0) n -= takeFromSlots(*s.v, IT_GUNPARTS, n, s.slots);
}
}  // namespace

static void panelCrafter(float W, float H) {
    Profile& p = G.prof;
    float w = 450, h = 250;
    float x = std::floor(W / 2 - w / 2), y = std::floor(H / 2 - h / 2);
    UI::panel(x, y, w, h, T("CRAFTER - WEAPON UPGRADES"));
    std::string cash = "$" + std::to_string(p.money);
    R::text(cash, x + w - 6 - R::textWidth(cash), y + 4, pal(P_YGREEN));
    int parts = partsHave();
    UI::itemIcon(IT_GUNPARTS, x + w - 70 - R::textWidth(cash), y + 1, 12);
    R::text("x" + std::to_string(parts), x + w - 56 - R::textWidth(cash), y + 4, pal(P_BEIGE));

    std::vector<GunRef> guns = craftableGuns();
    // ---- left: your guns
    const int PER = 8;
    float lx = x + 6, ly = y + 18, lw = 170;
    int pages = std::max(1, ((int)guns.size() + PER - 1) / PER);
    s_craftPage = std::clamp(s_craftPage, 0, pages - 1);
    // The crafting sheet behind the list, its scroll box showing the page.
    UI::subPanel(lx - 3, ly - 3, lw + 14, PER * 24 + 4, pages > 1 ? s_craftPage / float(pages - 1) : -1);
    s_craftSel = std::clamp(s_craftSel, 0, std::max(0, (int)guns.size() - 1));
    if (guns.empty()) R::text(T("No guns here. Bring one in your hands, pockets or stash."), lx, ly + 4, pal(P_LAVENDER));
    for (int k = 0; k < PER; k++) {
        int i = s_craftPage * PER + k;
        if (i >= (int)guns.size()) break;
        const Item& it = *guns[i].it;
        float ry = ly + k * 24;
        bool sel = i == s_craftSel;
        R::rect(lx, ry, lw, 22, pal(sel ? P_PURPLE : P_DARK, sel ? 0.8f : 0.45f));
        if (sel) R::rectOutline(lx, ry, lw, 22, tierColor(itemTier(it)));
        if (!UI::skinSprite("crafting/crafting-cell", lx, ry)) R::rectOutline(lx, ry, 21, 22, pal(P_PURPLE));
        UI::itemIcon(it.id, lx + 3, ry + 3, 15);
        R::text(itemLabel(it), lx + 22, ry + 3, tierColor(itemTier(it)));
        R::text(T(guns[i].where), lx + 22, ry + 12, pal(P_LAVENDER));
        if (UI::button(lx + lw - 30, ry + 4, 26, 14, sel ? ">" : T("Pick"), !sel)) s_craftSel = i;
        if (UI::hover(lx, ry, lw - 32, 22)) UI::itemTooltip(it);
    }
    float by = y + h - 22;
    if (pages > 1) {
        if (UI::button(lx, by, 20, 14, "<", s_craftPage > 0)) s_craftPage--;
        R::textCentered(std::to_string(s_craftPage + 1) + "/" + std::to_string(pages), lx + 36, by + 4, pal(P_LAVENDER), 1, false);
        if (UI::button(lx + 52, by, 20, 14, ">", s_craftPage < pages - 1)) s_craftPage++;
    }

    // ---- right: what can be done to the one picked
    float rx = x + 184, rw = w - 190, ry = y + 18;
    R::text(T("Gun parts are found out in the world."), rx, y + h - 38, pal(P_LAVENDER));
    if (!guns.empty()) {
        Item& it = *guns[s_craftSel].it;
        const WeaponDef* wd = weaponDef(it.id);
        UI::itemIcon(it.id, rx, ry, 24);
        R::text(itemLabel(it), rx + 28, ry + 2, tierColor(itemTier(it)));
        R::text(T(tierName(itemTier(it))) + "   " + T("MAG") + " " + std::to_string(magSizeOf(it)), rx + 28, ry + 13, pal(P_BEIGE));
        ry += 30;
        auto row = [&](const std::string& name, const std::string& desc, const std::string& state, CraftCost c, bool possible, int color, int toTier = -1) {
            R::rect(rx, ry, rw, state.empty() ? 46 : 26, pal(P_PURPLE, 0.25f));
            R::text(name, rx + 4, ry + 3, pal(color));
            R::text(desc, rx + 4, ry + 13, pal(P_BEIGE));
            bool clicked = false;
            if (!state.empty()) R::text(state, rx + rw - 6 - R::textWidth(state), ry + 3, pal(P_YGREEN));
            else {
                std::string price = "$" + std::to_string(c.money) + (c.parts ? "  +" + std::to_string(c.parts) + " " + T("gun parts") : "");
                bool afford = p.money >= c.money && parts >= c.parts;
                // The gun it becomes, from the gun (and the parts it takes).
                int ins[2] = {it.id, IT_GUNPARTS}, counts[2] = {1, c.parts};
                float sw = UI::recipe(rx + 3, ry + 23, it.id, tierColor(toTier >= 0 ? toTier : itemTier(it)), ins, counts, c.parts ? 2 : 1, true);
                R::text(price, rx + sw + 10, ry + 31, pal(afford ? P_YGREEN : P_CORAL));
                clicked = UI::button(rx + rw - 62, ry + 2, 58, 14, T("Upgrade"), possible && afford, P_YGREEN);
                if (clicked) {
                    p.money -= c.money;
                    if (c.parts) takeParts(c.parts);
                    Audio::play(Snd::reload_end, 0.8f, 0.8f);
                    Audio::play(Snd::sell, 0.5f, 0.7f);
                }
            }
            ry += state.empty() ? 50 : 30;
            return clicked;
        };
        // Tier.
        if (canTierUp(it)) {
            int next = itemTier(it) + 1;
            if (row(T1("Rework to {0}", T(tierName(next))), T("More damage, better aim, faster reload."), "", tierCost(it), true, P_YELLOW, next)) {
                it.tier = (int8_t)next;
                setNotice(T2("{0} is now {1}.", T(itemDef(it.id).name), T(tierName(next))));
                save_game();
            }
        } else {
            row(T("Tier"), itemDef(it.id).elite ? T("An elite gun: already the best.") : T("Epic is as far as a crafter can take it."),
                T("MAX"), {0, 0}, false, P_YELLOW);
        }
        // Laser.
        if (row(T("Laser pointer"), T("Tighter spread. L turns it on and off."), (it.flags & ITEMF_LASER) ? T("Fitted") : "",
                {laserCost(it), 0}, true, P_CORAL)) {
            it.flags |= ITEMF_LASER;
            it.flags &= ~ITEMF_LASER_OFF;
            p.laserOwned = true;
            setNotice(T1("Laser pointer fitted to the {0}.", T(itemDef(it.id).name)));
            save_game();
        }
        // Magazine: extended once, then a drum.
        if (!magUpgradable(it)) {
            row(T("Magazine"), T("Nothing to enlarge on this one."), "-", {0, 0}, false, P_BLUE);
        } else if (!(it.flags & ITEMF_MAG_EXT)) {
            if (row(T("Extended magazine"), T1("Holds half again as many: {0} rounds.", std::to_string((wd->magSize * 3 + 1) / 2)), "", {extCost(it), 1}, true, P_BLUE)) {
                it.flags |= ITEMF_MAG_EXT;
                setNotice(T1("Extended magazine fitted to the {0}.", T(itemDef(it.id).name)));
                save_game();
            }
        } else {
            if (row(T("Drum magazine"), T1("Twice the rounds: {0}.", std::to_string(wd->magSize * 2)), (it.flags & ITEMF_MAG_DRUM) ? T("Fitted") : "",
                    {drumCost(it), 3}, true, P_BLUE)) {
                it.flags |= ITEMF_MAG_DRUM;
                setNotice(T1("Drum magazine fitted to the {0}.", T(itemDef(it.id).name)));
                save_game();
            }
        }
    }
    if (UI::button(x + w / 2 - 40, by, 80, 16, T("Close")) || UI::panelClose()) G.panel = Panel::None;
}

void panelRecruit(float W, float H) {
    Profile& p = G.prof;
    float w = 360, h = 18 + SQUAD_MAX * 26 + 16 + HIRE_TIERS * 26 + 26;
    float x = std::floor(W / 2 - w / 2), y = std::floor(H / 2 - h / 2);
    UI::panel(x, y, w, h, T("MERCENARIES"));
    std::string cash = "$" + std::to_string(p.money);
    R::text(cash, x + w - 6 - R::textWidth(cash), y + 4, pal(P_YGREEN));

    // ---- your squad
    for (int i = 0; i < SQUAD_MAX; i++) {
        float ry = y + 18 + i * 26;
        R::rect(x + 4, ry, w - 8, 24, pal(i % 2 ? P_DARK : P_PURPLE, i % 2 ? 1.0f : 0.35f));
        if (i >= (int)p.squad.size()) {
            R::text(T("- empty slot -"), x + 10, ry + 8, pal(P_PURPLE));
            continue;
        }
        Hireling& m = p.squad[i];
        const HireTier& ht = hireTier(m.tier);
        UI::itemIcon(ht.weapon, x + 6, ry + 4, 16);
        R::text(m.name + "  (" + T(ht.name) + ")", x + 26, ry + 3, pal(P_WHITE));
        UI::bar(x + 26, ry + 14, 60, 3, m.hp / m.maxHp(), P_LGREEN);
        R::text(T1("{0} kills", std::to_string(m.kills)), x + 92, ry + 12, pal(P_LAVENDER));
        if (UI::button(x + w - 150, ry + 5, 80, 14, m.guard ? T("Guarding base") : T("Following you"), true, m.guard ? P_BLUE : P_MINT)) {
            m.guard = !m.guard;
            save_game();
        }
        if (UI::hover(x + w - 150, ry + 5, 80, 14))
            UI::tooltip(T("Orders"), T("Following: comes outside with you.\nGuarding: holds the compound against hordes."));
        if (UI::button(x + w - 64, ry + 5, 58, 14, T("Dismiss"), true, P_CORAL)) {
            setNotice(T1("{0} packed up and left.", m.name));
            p.squad.erase(p.squad.begin() + i);
            save_game();
            break;
        }
    }

    // ---- who is for hire
    float oy = y + 18 + SQUAD_MAX * 26 + 4;
    R::text(T("For hire - paid once, dead is dead"), x + 6, oy, pal(P_BEIGE));
    bool full = (int)p.squad.size() >= SQUAD_MAX;
    for (int t = 0; t < HIRE_TIERS; t++) {
        const HireTier& ht = hireTier(t);
        float ry = oy + 12 + t * 26;
        R::rect(x + 4, ry, w - 8, 24, pal(t % 2 ? P_DARK : P_PURPLE, t % 2 ? 1.0f : 0.35f));
        UI::itemIcon(ht.weapon, x + 6, ry + 4, 16);
        R::text(T(ht.name), x + 26, ry + 3, pal(P_YELLOW));
        char buf[96];
        std::snprintf(buf, sizeof buf, "%s  HP %d  %s", T(itemDef(ht.weapon).name).c_str(), (int)ht.hp,
                      ht.helmet ? T("helmet").c_str() : "");
        R::text(buf, x + 26, ry + 13, pal(P_BEIGE));
        bool locked = p.day < ht.minDay;
        std::string label = locked ? T1("Day {0}", std::to_string(ht.minDay)) : "$" + std::to_string(ht.price);
        if (UI::button(x + w - 64, ry + 5, 58, 14, label, !locked && !full && p.money >= ht.price, P_YGREEN)) {
            Hireling m;
            m.name = nextHireName();
            m.tier = t;
            m.hp = ht.hp;
            p.money -= ht.price;
            p.hires++;
            p.squad.push_back(m);
            Audio::play(Snd::sell, 0.7f, 0.9f);
            setNotice(T2("{0} the {1} joins you.", m.name, T(ht.name)));
            save_game();
        }
    }
    if (UI::button(x + w / 2 - 40, y + h - 20, 80, 16, T("Close")) || UI::panelClose()) G.panel = Panel::None;
}

// ------------------------------------------------------------------- hordes
// A horde that is due (or already out there) is everybody's fight: whoever is down
// here gets a few seconds' warning and is sent up the ladder. There is no sleeping
// through one and no hiding from one.
bool base_hordeCall(float dt) {
    bool due = raid_hordeOn() || absMinutes() >= G.prof.nextHordeAt;
    if (!due) { s_callT = -1; return false; }
    if (s_callT < 0) {
        s_callT = 4.0f;
        Audio::play(Snd::warning, 1.0f, 0.7f);
        pushMessage(T("The horde is here! Everyone to the surface!"), P_CORAL);
    }
    s_callT -= dt;
    if (s_callT > 0) return false;
    s_callT = -1;
    std::fprintf(stderr, "[horde] everyone up: sent outside at %s\n", fmtTime(G.prof.timeMin).c_str());
    Coop::setInBed(false);
    G.panel = Panel::None;
    raid_start();
    return true;
}

void base_drawHordeCall() {
    if (s_callT < 0) return;
    float W = (float)R::width(), H = (float)R::height();
    bool blink = std::fmod(G.realTime, 0.5f) < 0.3f;
    R::rect(0, H * 0.22f - 6, W, 40, pal(P_DARK, 0.8f));
    R::textCentered(T("THE HORDE IS HERE"), W / 2, H * 0.22f, pal(blink ? P_CORAL : P_ORANGE), 2);
    R::textCentered(T1("Heading outside in {0}...", std::to_string((int)std::ceil(std::max(0.0f, s_callT)))), W / 2, H * 0.22f + 22,
                    pal(P_WHITE));
}

// ------------------------------------------------------------------- tutorial
// A tour of the bunker the first time you are in it: one box per station, with an
// arrow from the box to the thing it describes.
namespace {
struct TutStep { int station; const char* title; const char* text; };
const TutStep TUTORIAL[] = {
    {-1, "WELCOME TO THE BUNKER", "This is home. Here is what everything down here is for. Walk up to a station and press E to use it."},
    {0, "BED", "Sleep to end the day: it heals you and saves the game. In co-op everyone has to lie down. A horde due before morning wakes you up to fight it."},
    {1, "EXIT HATCH", "Climb out to explore and loot. Every day is a new world. Get back before 22:00: at night the dead cannot be killed."},
    {2, "STASH", "Keep your loot safe here. In co-op the big stash is shared by every player, and the small private one is only yours."},
    {3, "MISSION BOARD", "Pick a contract each day. Bring what it asks for or make the kills, then hand it in here for the reward."},
    {4, "WORKBENCH", "Spend money on permanent upgrades: health, stamina, pockets, speed, aim and more."},
    {5, "TRADER", "Sell what you found. Buy weapons, ammo, armor, backpacks and medicine."},
    {6, "DEFENSE CONSOLE", "Build, upgrade and repair turrets around the hatch, and research better defenses. Every player can use it."},
    {7, "MERCENARIES", "Hire mercenaries to fight beside you or guard the bunker. A dead mercenary is gone for good."},
    {8, "CRAFTER", "Improve one gun at a time: raise it a tier, fit a laser pointer, or give it a bigger magazine. Good work costs money and gun parts."},
    {-1, "HORDES", "Every day or two a horde of zombies attacks the bunker. When it comes, everyone is sent outside to fight it. Good luck out there."},
};
constexpr int TUT_STEPS = sizeof(TUTORIAL) / sizeof(TUTORIAL[0]);
}  // namespace

void base_devTutorial(int step) {
    if (step < 0) { G.prof.tutorialDone = true; G.panel = Panel::None; return; }   // skip it
    G.panel = Panel::Tutorial;
    s_tutStep = step;
}

void panelTutorial(float W, float H) {
    Profile& p = G.prof;
    s_tutStep = std::clamp(s_tutStep, 0, TUT_STEPS - 1);
    const TutStep& st = TUTORIAL[s_tutStep];
    Vec2 camF(std::floor(G.cam.x), std::floor(G.cam.y));
    bool pointing = st.station >= 0;
    Vec2 target;
    if (pointing) target = World::tileCenter(STATIONS[st.station].tx, STATIONS[st.station].ty) - camF;

    float w = 230, x, y;
    std::string text = T(st.text);
    // Measure the text by wrapping it off screen first.
    float textH = UI::textWrap(text, -10000, -10000, w - 20, pal(P_WHITE));
    float h = 40 + textH + 22;
    if (pointing) {
        // Put the box on the other half of the screen from what it points at.
        x = clampf(target.x - w / 2, 8, W - w - 8);
        y = target.y > H / 2 ? target.y - h - 46 : target.y + 40;
        y = clampf(y, 20, H - h - 16);
    } else {
        x = std::floor(W / 2 - w / 2);
        y = std::floor(H / 2 - h / 2);
    }
    x = std::floor(x); y = std::floor(y);

    if (pointing) {
        // Arrow from the nearest edge of the box to the station, and a pulsing frame on it.
        Vec2 from(clampf(target.x, x + 6, x + w - 6), target.y < y ? y : y + h);
        Vec2 d = normalize(target - from);
        Vec2 tip = target - d * 12;
        R::line(from, tip, 2.0f, pal(P_DARK, 0.9f));
        R::line(from, tip, 1.0f, pal(P_YELLOW));
        R::sprite(ARROW, tip, angleOf(d), 1, pal(P_YELLOW));
        float pulse = 0.5f + 0.5f * std::sin(G.realTime * 6.0f);
        R::rectOutline(target.x - 10 - pulse * 2, target.y - 12 - pulse * 2, 20 + pulse * 4, 22 + pulse * 4, pal(P_YELLOW, 0.8f));
    }
    UI::panel(x, y, w, h, T(st.title));
    UI::textWrap(text, x + 10, y + 22, w - 20, pal(P_WHITE));
    R::text(std::to_string(s_tutStep + 1) + "/" + std::to_string(TUT_STEPS), x + 10, y + h - 18, pal(P_LAVENDER));
    bool last = s_tutStep == TUT_STEPS - 1;
    if (s_tutStep > 0 && UI::button(x + w - 170, y + h - 22, 50, 16, T("Back"))) s_tutStep--;
    if (UI::button(x + w - 114, y + h - 22, 50, 16, last ? T("Done") : T("Next"), true, P_YGREEN)) {
        if (last) { p.tutorialDone = true; G.panel = Panel::None; save_game(); }
        else s_tutStep++;
    }
    if (!last && UI::button(x + w - 58, y + h - 22, 50, 16, T("Skip"))) { p.tutorialDone = true; G.panel = Panel::None; save_game(); }
}
