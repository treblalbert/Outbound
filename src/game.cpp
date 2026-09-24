#include "game.h"
#include "art.h"
#include "assets.h"
#include "sprites.h"
#include "coop.h"
#include "atmosphere.h"
#include <cstdio>
#include <ctime>
#include <fstream>
#include <map>
#include <string>
#include <unordered_map>
#include <sstream>
#ifdef _WIN32
#include <direct.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

void ensureDir(const std::string& path) {
#ifdef _WIN32
    _mkdir(path.c_str());
#else
    mkdir(path.c_str(), 0755);
#endif
}

void openUrl(const std::string& url) {
    // Only ever hand the shell a web address, never a path or command.
    if (url.rfind("https://", 0) != 0 && url.rfind("http://", 0) != 0) return;
#ifdef __EMSCRIPTEN__
    std::string js = "window.open('" + url + "', '_blank');";
    emscripten_run_script(js.c_str());
#elif defined(_WIN32)
    ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
    std::string cmd = "xdg-open '" + url + "' &";
    (void)std::system(cmd.c_str());
#endif
}

void persistSaves() {
#ifdef __EMSCRIPTEN__
    EM_ASM({ if (typeof FS !== 'undefined') FS.syncfs(false, function(e) {}); });
#endif
}

using namespace Sprites;

Game G;

static const UpgradeDef UPGRADES[UP_COUNT] = {
    {"Vitality", "+20 max health", 5, 250},
    {"Endurance", "+25 stamina, faster recovery", 4, 180},
    // Eight levels of +2, on top of whatever a backpack gives: pockets are the way
    // past what a pack alone can hold.
    {"Deep Pockets", "+2 inventory slots, on top of any backpack", 8, 220},
    {"Agility", "+6% movement speed", 3, 400},
    {"Steady Hands", "-12% spread, -10% reload time", 3, 350},
    {"Night Eyes", "+25% flashlight range", 3, 300},
    {"Toughness", "-7% damage taken", 4, 450},
    {"Laser Tuning", "Tighter laser spread", 3, 500},
};

const UpgradeDef& upgradeDef(int id) { return UPGRADES[id]; }

int upgradeCost(int id, int level) {
    return (int)(UPGRADES[id].baseCost * std::pow(level + 1.0f, 1.6f) / 10.0f) * 10;
}

int Profile::invCapacity() const {
    // Pockets stack on top of the backpack instead of competing with it, so a maxed
    // Deep Pockets buys slots a pack cannot.
    int c = BASE_INV_SLOTS + up[UP_POCKETS] * 2;
    if (!backpack.empty()) c += itemDef(backpack.id).param;
    return std::min(c, GAMEPLAY_INV_MAX);
}

void grantStarterKit() {
    Profile& p = G.prof;
    p.weapons[0] = makeItem(IT_PISTOL, 1);          // makeItem fills the magazine
    p.weapons[1] = p.armor = p.backpack = Item();
    p.curWeapon = 0;
    p.inv.assign(INV_MAX_SLOTS, Item());
    addToSlots(p.inv, makeItem(IT_AMMO_LIGHT, 60), p.invCapacity());
}

const char* difficultyName(int d) { return d == DIFF_HARDCORE ? "Hardcore" : "Normal"; }
const char* gameModeName(int m) { return m == MODE_ZOMBIES ? "Zombies" : "Normal"; }

const HardcoreUnlockDef& hardcoreUnlockDef(int id) {
    static const HardcoreUnlockDef DEFS[HC_COUNT] = {
        {"Map & compass", "Brings back the minimap and the map (M).", 900},
        {"Home beacon", "Points the way back to the hatch, with its distance.", 1200},
        {"Team radio", "Shows where your friends are, on the edge of the screen and on the map.", 700},
    };
    return DEFS[std::clamp(id, 0, HC_COUNT - 1)];
}

std::vector<Item> applyDeathLoss() {
    Profile& p = G.prof;
    std::vector<Item*> held;
    int cap = p.invCapacity();
    for (int i = 0; i < cap; i++) if (!p.inv[i].empty()) held.push_back(&p.inv[i]);
    for (Item* it : {&p.weapons[0], &p.weapons[1], &p.armor, &p.backpack}) if (!it->empty()) held.push_back(it);
    G.summary.carried = (int)held.size();
    // Which half goes is down to luck: every stack, gun, vest and pack counts as one.
    Rng r(mix64((uint64_t)std::time(nullptr) ^ p.worldSeed ^ ((uint64_t)p.deaths << 24) ^ (uint64_t)(p.timeMin * 13)));
    for (size_t i = held.size(); i > 1; i--) std::swap(held[i - 1], held[(size_t)r.irange(0, (int)i - 1)]);
    size_t loseN = p.hardcore() ? held.size() : (held.size() + 1) / 2;
    std::vector<Item> lost;
    for (size_t k = 0; k < loseN && k < held.size(); k++) { lost.push_back(*held[k]); *held[k] = Item(); }
    // Pockets close up; a lost backpack takes its room with it, and whatever no longer
    // fits is lost too.
    std::vector<Item> kept;
    for (int i = 0; i < INV_MAX_SLOTS; i++) if (!p.inv[i].empty()) kept.push_back(p.inv[i]);
    p.inv.assign(INV_MAX_SLOTS, Item());
    int newCap = p.invCapacity();
    for (size_t i = 0; i < kept.size(); i++) {
        if ((int)i < newCap) p.inv[i] = kept[i];
        else lost.push_back(kept[i]);
    }
    G.summary.lost = (int)lost.size();
    // Never wake up unarmed.
    if (p.weapons[0].empty() && p.weapons[1].empty()) {
        p.weapons[0] = makeItem(IT_PISTOL, 1);
        addToSlots(p.inv, makeItem(IT_AMMO_LIGHT, p.hardcore() ? 24 : 60), p.invCapacity());
    }
    if (p.weapons[p.curWeapon].empty()) p.curWeapon = p.weapons[0].empty() ? 1 : 0;
    return lost;
}

void pushMessage(const std::string& text, int color) {
    if (!G.messages.empty() && G.messages.back().text == text) {
        G.messages.back().life = 3.5f;
        return;
    }
    G.messages.push_back({text, color, 3.5f});
    if (G.messages.size() > 6) G.messages.erase(G.messages.begin());
}

void setNotice(const std::string& text) {
    G.notice = text;
    G.noticeT = 3.0f;
}

// ---------------------------------------------------------------- lighting
Color ambientColor(float minutes) {
    struct Key { float h; float r, g, b; };
    static const Key keys[] = {
        {0.0f, 0.30f, 0.30f, 0.46f},  {4.0f, 0.30f, 0.30f, 0.46f},  {5.5f, 0.85f, 0.78f, 0.85f},
        {6.5f, 1.0f, 1.0f, 1.0f},     {18.0f, 1.0f, 1.0f, 1.0f},    {20.0f, 1.0f, 0.82f, 0.70f},
        {21.5f, 0.52f, 0.48f, 0.62f}, {22.0f, 0.32f, 0.32f, 0.50f}, {24.0f, 0.30f, 0.30f, 0.46f},
    };
    float h = std::fmod(minutes / 60.0f, 24.0f);
    for (size_t i = 0; i + 1 < sizeof(keys) / sizeof(keys[0]); i++) {
        if (h >= keys[i].h && h <= keys[i + 1].h) {
            float t = (h - keys[i].h) / (keys[i + 1].h - keys[i].h);
            return {lerpf(keys[i].r, keys[i + 1].r, t), lerpf(keys[i].g, keys[i + 1].g, t), lerpf(keys[i].b, keys[i + 1].b, t)};
        }
    }
    return {1, 1, 1};
}

float ambientBrightness(float minutes) {
    Color c = ambientColor(minutes);
    return (c.r + c.g + c.b) / 3.0f;
}

// ---------------------------------------------------------------- world drawing
// Sprites standing on the ground, drawn back-to-front.
namespace {
struct SceneSprite {
    float sortY;
    const Assets::Sprite* sprite;
    int frame;
    Vec2 pos;          // baseline (bottom centre) for pack art, centre for fallback art
    Color tint;
    float scale;
    bool flipX;
    bool centered;
    float angle;
    float sway;
    bool noReflect = false;   // lying on the ground (bodies, bags): nothing to mirror
    float lift = 0;           // empty rows under the art (a car's frame): where it really meets the ground
};
// The frame as it stands on the ground: without the empty rows under it.
Assets::Frame grounded(const SceneSprite& s, Vec2& base) {
    Assets::Frame f = s.sprite->frame(s.frame);
    base = s.pos;
    if (s.lift <= 0 || s.lift >= f.h) return f;
    float keep = (f.h - s.lift) / (float)f.h;
    f.v1 = f.v0 + (f.v1 - f.v0) * keep;
    f.h = (int)(f.h - s.lift);
    base.y -= s.lift * s.scale;
    return f;
}
std::vector<SceneSprite> s_scene;
}  // namespace

void sceneBegin() { s_scene.clear(); }
void sceneNoReflect() { if (!s_scene.empty()) s_scene.back().noReflect = true; }
void sceneLift(float rows) { if (!s_scene.empty()) s_scene.back().lift = rows; }
// How many empty rows a frame has at its bottom (cached per sprite and frame).
float spriteEmptyRowsBelow(const Assets::Sprite* s, int frame) {
    static std::map<std::pair<const Assets::Sprite*, int>, float> cache;
    auto key = std::make_pair(s, frame);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    int rows = 0;
    for (int y = s->h - 1; y >= 0; y--) {
        bool any = false;
        for (int x = 0; x < s->w && !any; x++) any = s->opaqueAt(frame, x, y);
        if (any) break;
        rows++;
    }
    return cache[key] = (float)rows;
}

// ---------------------------------------------------------------- the sun
static const World* s_sunWorld = nullptr;
void sceneSetWorld(const World* w) {
    s_sunWorld = w;
    if (!w) R::setSun(Vec2(0, 1), 0);
}

// The sun crosses the sky from 06:00 to 20:00: low in the east in the morning (long
// shadows to the west), high at noon (short ones), low in the west at dusk. Shadows
// always lean a little toward the camera so they read on a top-down map. Cloud thins
// them out; there are none at night.
void setSunForTime(float minutes) {
    float h = std::fmod(minutes / 60.0f, 24.0f);
    float t = (h - 6.0f) / 14.0f;
    if (t <= 0 || t >= 1) { R::setSun(Vec2(0, 1), 0); return; }
    float elev = std::sin(t * PI);
    float len = clampf(0.38f / std::max(elev, 0.2f), 0.38f, 1.9f);
    Vec2 dir(-std::cos(t * PI), 0.55f + 0.25f * (1.0f - elev));
    dir = normalize(dir) * len;
    float fade = clampf(elev / 0.18f, 0, 1);            // dawn and dusk: fading in and out
    R::setSun(dir, 0.34f * fade * Atmo::sunlight());
}

float lampOn(float minutes, float bright, uint32_t seed) {
    float h = std::fmod(minutes / 60.0f, 24.0f);
    uint32_t r = hash2((int)seed, 17, 0x1A3Bu);
    float onAt = 18.6f + (r % 1000) / 1000.0f * 1.2f;          // 18:36 .. 19:48
    float offAt = 5.5f + ((r / 1000) % 1000) / 1000.0f * 1.0f; // 05:30 .. 06:30
    float since;                                               // hours since it came on
    if (h >= onAt) since = h - onAt;
    else if (h < offAt) since = h + 24.0f - onAt;
    else if (bright < 0.45f) since = 1.0f;                     // a storm dark enough: on
    else return 0;
    // Warming up: a few seconds of stutter before it holds steady.
    if (since < 0.12f) {
        float f = std::sin(G.realTime * 37.0f + seed) * std::sin(G.realTime * 13.0f + seed * 0.7f);
        return clampf(since / 0.12f * 0.6f + (f > 0.2f ? 0.4f : 0.0f), 0, 1);
    }
    return 1;
}

// Under an intact roof there is no sun to cast a shadow from.
static bool underSunlessRoof(Vec2 p) {
    if (!s_sunWorld) return true;
    int tx = World::toTile(p.x), ty = World::toTile(p.y - 1);
    for (const Building& b : s_sunWorld->buildings)
        if (tx >= b.x0 && tx < b.x0 + b.w && ty >= b.y0 && ty < b.y0 + b.h - 1) return true;
    return false;
}

void sceneAdd(const Art::Piece& piece, Vec2 baseline, Color tint, float scale, float yBias) {
    if (!piece.valid()) return;
    s_scene.push_back({baseline.y + yBias, piece.sprite, piece.frame, baseline, tint, scale * piece.scale, piece.flipX, false, 0.0f, piece.sway});
}

void sceneAddCentered(const Art::Piece& piece, Vec2 center, Color tint, float scale, float yBias) {
    if (!piece.valid()) return;
    s_scene.push_back({center.y + yBias, piece.sprite, piece.frame, center, tint, scale * piece.scale, piece.flipX, true, 0.0f, 0.0f});
}

void sceneAddSprite(int fallbackSpriteId, Vec2 center, Color tint, float angle, float scale) {
    const Assets::Sprite* s = Sprites::fallback(fallbackSpriteId);
    if (!s || !s->valid()) return;
    s_scene.push_back({center.y, s, 0, center, tint, scale, false, true, angle, 0.0f});
}

void sceneFlush() {
    std::stable_sort(s_scene.begin(), s_scene.end(), [](const SceneSprite& a, const SceneSprite& b) { return a.sortY < b.sortY; });
    // Reflections in any puddle or pool queued this frame, before anything stands on it.
    if (R::waterQueued()) {
        for (const SceneSprite& s : s_scene) {
            if (s.centered || !s.sprite || s.noReflect) continue;
            Vec2 base;
            Assets::Frame f = grounded(s, base);
            R::reflectSprite(f, base, f.w * s.scale, f.h * s.scale, s.flipX, s.tint);
        }
    }
    R::flushWater(G.realTime, Color(0.72f, 0.8f, 0.9f));
    // Ambient occlusion next: the bottom few pixel rows of everything standing, with
    // the walls queued by drawWorldTiles.
    for (const SceneSprite& s : s_scene) {
        if (s.centered || !s.sprite) continue;
        Vec2 base;
        Assets::Frame f = grounded(s, base);
        R::aoSprite(f, base, f.w * s.scale, f.h * s.scale, s.flipX, 5);
    }
    R::flushAO(0.3f);
    // Shadows first, on the ground and walls drawn so far; then everything standing
    // goes on top of them.
    if (s_sunWorld && R::sunStrength() > 0.001f) {
        for (const SceneSprite& s : s_scene) {
            if (s.centered || !s.sprite) continue;
            if (underSunlessRoof(s.pos)) continue;
            Vec2 base;
            Assets::Frame f = grounded(s, base);
            R::shadowSprite(f, base, f.w * s.scale, f.h * s.scale, s.flipX);
        }
    }
    R::flushShadows();
    for (const SceneSprite& s : s_scene)
        if (s.sway != 0 && !s.centered) R::spriteSway(*s.sprite, s.frame, s.pos, s.sway, s.scale, s.tint, s.flipX);
        else R::spriteAt(*s.sprite, s.frame, s.pos, s.centered ? R::Pivot::Center : R::Pivot::Bottom, s.scale, s.tint, s.flipX, s.angle);
    s_scene.clear();
}

static void viewRange(World& w, Vec2 cam, int& x0, int& y0, int& x1, int& y1, int marginBelow = 0) {
    x0 = std::max(0, (int)std::floor(cam.x / TILE) - 2);
    y0 = std::max(0, (int)std::floor(cam.y / TILE) - 2);
    x1 = std::min(w.w - 1, (int)std::floor((cam.x + R::viewW()) / TILE) + 2);
    y1 = std::min(w.h - 1, (int)std::floor((cam.y + R::viewH()) / TILE) + 2 + marginBelow);
}

// ---- the catacombs' art (Szadi Art's Rogue Fantasy Catacombs, cut by
// tools/cut_catacombs.py into "catacombs/...")
namespace {
struct CryptArt {
    bool loaded = false;
    const Assets::Sprite* floor[14] = {};
    const Assets::Sprite* face[6] = {};
    const Assets::Sprite *faceTop = nullptr, *cap = nullptr, *rimN = nullptr, *rimS = nullptr, *rimW = nullptr, *rimE = nullptr;
    const Assets::Sprite *stairsOut = nullptr, *gateFrame = nullptr, *gateBars = nullptr;
    const Assets::Sprite *stairs = nullptr, *torch = nullptr, *candleA = nullptr, *candleB = nullptr, *spikes = nullptr;
    const Assets::Sprite* pillar[5] = {};
    const Assets::Sprite* coffin[2] = {};
    const Assets::Sprite* candles[4] = {};
};
CryptArt s_crypt;
const CryptArt& cryptArt() {
    CryptArt& a = s_crypt;
    if (a.loaded) return a;
    a.loaded = true;
    auto f = [](const std::string& k) { return Assets::find("catacombs/" + k); };
    for (int i = 0; i < 14; i++) a.floor[i] = f("floor_" + std::to_string(i));
    for (int i = 0; i < 6; i++) a.face[i] = f("wall_face_" + std::to_string(i));
    a.faceTop = f("wall_face_top"); a.cap = f("wall_cap");
    a.rimN = f("rim_n"); a.rimS = f("rim_s"); a.rimW = f("rim_w"); a.rimE = f("rim_e");
    a.stairsOut = f("stairs_out"); a.gateFrame = f("gate_frame"); a.gateBars = f("gate_bars");
    a.stairs = f("stairs"); a.torch = f("torch"); a.candleA = f("candle_a"); a.candleB = f("candle_b"); a.spikes = f("spikes");
    for (int i = 0; i < 5; i++) a.pillar[i] = f("pillar_" + std::to_string(i));
    for (int i = 0; i < 2; i++) a.coffin[i] = f("coffin_" + std::to_string(i));
    for (int i = 0; i < 4; i++) a.candles[i] = f("candles_" + std::to_string(i));
    if (!a.cap || !a.floor[0]) std::fprintf(stderr, "[art] catacomb tiles missing (assets/sprites/Catacombs)\n");
    return a;
}
const Assets::Sprite* cryptFloor(uint8_t v) {
    const CryptArt& a = cryptArt();
    // Mostly plain slabs, some paving, a few cracked stones.
    int r = v % 20;
    int i = r < 10 ? r % 6 : r < 16 ? 6 + r % 4 : 10 + r % 4;
    return a.floor[i];
}
}  // namespace

// Spikes stick up for a moment every couple of seconds; each trap has its own beat.
float spikeCycle(const WorldProp& p, float t) {
    return std::fmod(t + (p.variant % 16) * 0.13f, 2.4f);
}
bool spikesUp(const WorldProp& p, float t) {
    float c = spikeCycle(p, t);
    return c > 1.6f && c < 2.2f;
}

// Ambient occlusion (0.11v): the walls (solid, whole tiles) go into the occlusion mask
// here; the standing sprites add the real pixels of their bases in sceneFlush, which
// lays the soft darkening on the ground before anything stands on it.
static bool aoWall(const World& w, int x, int y) {
    if (!w.inBounds(x, y)) return false;
    switch (w.at(x, y).solid) {
    case S_WALL_BRICK: case S_WALL_CONCRETE: case S_WALL_WOOD: case S_DOOR: case S_BUNKER: case S_BOUNDARY: case S_CRYPT_WALL:
        return true;
    default: return false;
    }
}

static void drawAmbientOcclusion(const World& w, int x0, int y0, int x1, int y1) {
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            if (!aoWall(w, x, y)) continue;
            // Only walls with open ground beside them matter; runs of them are merged.
            bool open = false;
            for (int k = 0; k < 8 && !open; k++) {
                static const int DX[8] = {1, -1, 0, 0, 1, 1, -1, -1}, DY[8] = {0, 0, 1, -1, 1, -1, 1, -1};
                open = w.inBounds(x + DX[k], y + DY[k]) && !aoWall(w, x + DX[k], y + DY[k]);
            }
            if (!open) continue;
            int run = 1;
            while (x + run <= x1 && aoWall(w, x + run, y)) run++;
            R::aoRect((float)x * TILE, (float)y * TILE, (float)run * TILE, TILE);
            x += run - 1;
        }
}

void drawWorldTiles(World& w, Vec2 cam, float timeSec) {
    int x0, y0, x1, y1;
    viewRange(w, cam, x0, y0, x1, y1);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            const Tile& t = w.at(x, y);
            float px = (float)x * TILE, py = (float)y * TILE;
            if (t.ground == G_VOID) { R::rect(px, py, TILE, TILE, Color(0.02f, 0.02f, 0.03f)); continue; }
            if (t.ground == G_CRYPT) {
                if (t.solid == S_CRYPT_WALL) continue;   // the wall draws all of itself
                if (const Assets::Sprite* s = cryptFloor(t.variant)) R::frame(s->frame(0), px, py, TILE, TILE);
                else R::rect(px, py, TILE, TILE, pal(P_DARK));
                continue;
            }
            Assets::TileRef ref = Art::groundTile(w, x, y, timeSec);
            if (ref.valid()) R::tileAt(ref, px, py, TILE);
            else {
                int spr = t.ground == G_GRASS ? GRASS0 : t.ground == G_DIRT ? DIRT : t.ground == G_ROAD ? ROAD : RUBBLE;
                R::spriteRect(spr, px, py, TILE, TILE);
            }
            if (t.ground == G_ROAD) {
                auto road = [&](int tx, int ty) { return w.inBounds(tx, ty) && (w.at(tx, ty).ground == G_ROAD || w.at(tx, ty).ground == G_BRIDGE); };
                bool l = road(x - 1, y), rr = road(x + 1, y), u = road(x, y - 1), d = road(x, y + 1);
                // The pack's shoulder tiles cover roads two or more tiles wide; a strip
                // one tile wide (or a stub end) gets a thin drawn edge instead.
                if (!Art::roadEdgeTile(w, x, y) && !(l && rr && u && d)) {
                    Color edge = pal(P_DARK, 0.38f);
                    if (!l) R::rect(px, py, 1, TILE, edge);
                    if (!rr) R::rect(px + TILE - 1, py, 1, TILE, edge);
                    if (!u) R::rect(px, py, TILE, 1, edge);
                    if (!d) R::rect(px, py + TILE - 1, TILE, 1, edge);
                }
                // A dashed centre line down the middle of the road, however wide it is:
                // measure the road across (both ways) and paint where its middle falls.
                auto run = [&](int dx, int dy) {
                    int n = 0;
                    while (n < 12 && road(x + dx * (n + 1), y + dy * (n + 1))) n++;
                    return n;
                };
                int up = run(0, -1), dn = run(0, 1), lf = run(-1, 0), rt = run(1, 0);
                int across = up + dn + 1, along = lf + rt + 1;      // for a road running left-right
                Color paint(0.80f, 0.78f, 0.78f, 0.85f);
                if (along > across + 2 && across >= 2 && across <= 10 && (x & 1) == 0) {
                    if (across % 2 == 0 && up == across / 2) R::rect(px + 3, py - 1, 10, 2, paint);
                    else if (across % 2 == 1 && up == across / 2) R::rect(px + 3, py + 7, 10, 2, paint);
                } else if (across > along + 2 && along >= 2 && along <= 10 && (y & 1) == 0) {
                    if (along % 2 == 0 && lf == along / 2) R::rect(px - 1, py + 3, 2, 10, paint);
                    else if (along % 2 == 1 && lf == along / 2) R::rect(px + 7, py + 3, 2, 10, paint);
                }
            }
            // Road paint, garbage, grass creeping over the paving (0.12v).
            if (t.overlay) {
                Assets::TileRef ov = Art::overlayTile(t.overlay);
                if (ov.valid()) R::tileAt(ov, px, py, TILE);
            }
        }
    // Flat scenery details (grass tufts, litter, flowers) sit on top of the ground,
    // and rugs on the floors of buildings.
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            const Tile& t = w.at(x, y);
            if (t.solid != S_NONE) continue;
            if (t.furn && t.furn != FURN_REST && t.furn <= FURN_PIECE_COUNT) {
                const FurnPiece& f = FURN_PIECES[t.furn - 1];
                if (const Assets::Sprite* s = Assets::find(std::string("furniture/") + f.key))
                    R::spriteAt(*s, 0, {x * TILE + f.w * TILE * 0.5f, y * TILE + TILE * 0.5f}, R::Pivot::Center);
                continue;
            }
            if (t.worldDeco) {
                Art::Piece p = Art::groundDeco(t.worldDeco, t.tone);
                if (p.valid()) {
                    float px = x * TILE + TILE * 0.5f, py = (y + 1) * (float)TILE;
                    R::spriteAt(*p.sprite, p.frame, {px, py}, R::Pivot::Bottom, 1, Color());
                    continue;
                }
            }
            if (t.deco) {
                Art::Piece stump = t.deco == STUMP ? Art::stump() : Art::Piece();
                if (stump.valid()) R::spriteAt(*stump.sprite, 0, {x * TILE + TILE * 0.5f, (y + 1) * (float)TILE}, R::Pivot::Bottom);
                else R::spriteRect(t.deco, (float)x * TILE, (float)y * TILE, TILE, TILE);
            }
        }
    drawAmbientOcclusion(w, x0, y0, x1, y1);
}

// Walls are flat tiles; everything else is queued into the depth-sorted pass.
// ---- barricades (0.12v): Objects/Buildable, wooden or reinforced, joined up with
// their neighbours: straight runs, the four corners, the T, and gates that swing.
static const Assets::Sprite* buildablePart(bool reinforced, const char* part) {
    static std::unordered_map<std::string, const Assets::Sprite*> cache;
    std::string key = reinforced ? std::string("objects/buildable/reinforced/reinforced_wooden-wall_") + part
                                 : std::string("objects/buildable/wooden/wooden-wall_") + part;
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    const Assets::Sprite* sp = Assets::find(key);
    cache[key] = sp && sp->valid() ? sp : nullptr;
    return cache[key];
}
static const Assets::Sprite* buildableSwing(bool reinforced, bool opensHorizontal) {
    static const Assets::Sprite* s[2][2] = {};
    static bool looked = false;
    if (!looked) {
        looked = true;
        s[0][1] = Assets::find("objects/buildable/wooden/animations/wooden-wall_gates-openingh_closingv");
        s[0][0] = Assets::find("objects/buildable/wooden/animations/wooden-wall_gates-closingh_openingv");
        s[1][1] = Assets::find("objects/buildable/reinforced/animation/reinforced-wooden-wall_gates-openingh_closingv");
        s[1][0] = Assets::find("objects/buildable/reinforced/animation/reinforced-wooden-wall_gates-closingh_openingv");
    }
    return s[reinforced][opensHorizontal];
}

static bool isBarricade(const World& w, int x, int y) {
    if (!w.inBounds(x, y)) return false;
    int s = w.at(x, y).solid;
    return s == S_BARRICADE || s == S_GATE || s == S_GATE_OPEN;
}

static void drawBarricadeTile(const World& w, int x, int y, const Tile& t, Color tint) {
    bool reinf = barricadeDef(t.variant).reinforced;
    // The barricade's own health shows as the others' do: darker as it is broken up.
    for (const Barricade& b : G.prof.barricades)
        if (w.homeTx + b.dx == x && w.homeTy + b.dy == y) {
            float f = clampf(b.hp / std::max(1.0f, barricadeMaxHp(b)), 0.35f, 1.0f);
            tint = b.hurtT > 0 ? pal(P_CORAL) : Color(f * 0.4f + 0.6f, f * 0.55f + 0.45f, f * 0.55f + 0.45f);
            break;
        }
    int mask = (isBarricade(w, x - 1, y) ? 1 : 0) | (isBarricade(w, x + 1, y) ? 2 : 0) | (isBarricade(w, x, y - 1) ? 4 : 0) | (isBarricade(w, x, y + 1) ? 8 : 0);
    float px = (float)x * TILE, py = (float)y * TILE;
    Vec2 base(px + TILE * 0.5f, py + TILE);
    auto put = [&](const Assets::Sprite* sp, float cx, int frame = 0) {
        if (sp) sceneAdd(Art::Piece{sp, frame, false, 1.0f}, Vec2(cx, base.y), tint);
    };
    bool vertical = (mask & 12) && !(mask & 3);
    if (t.solid != S_BARRICADE) {
        float open = gateOpenness(x, y);
        if (vertical) {
            if (open > 0.02f) put(buildableSwing(reinf, false), base.x, std::min(6, (int)(open * 7)));
            else put(buildablePart(reinf, "gate_vertical"), base.x);
            // The posts it hangs between.
            if (const Assets::Sprite* post = buildablePart(reinf, "vertical_for-gate"))
                sceneAdd(Art::Piece{post, 0, false, 1.0f}, Vec2(base.x, py + post->frame(0).h - 2), tint);
        } else {
            if (open > 0.02f) put(buildableSwing(reinf, true), base.x, std::min(6, (int)(open * 7)));
            else put(buildablePart(reinf, "gate_horizontal"), base.x);
        }
        return;
    }
    switch (mask & 15) {
    case 2 | 8: put(buildablePart(reinf, "left-side_right&down-connect"), px + 10); break;
    case 1 | 8: put(buildablePart(reinf, "right-side_left&down-connect"), px + 6); break;
    case 2 | 4: put(buildablePart(reinf, "left-side_right&up-connect"), px + 10); break;
    case 1 | 4: put(buildablePart(reinf, "right-side_left&up-connect"), px + 6); break;
    case 1 | 2 | 8: put(buildablePart(reinf, "middle_right&left&down-connect"), base.x); break;
    case 4: case 8: case 4 | 8: put(buildablePart(reinf, "vertical"), base.x); break;
    case 0: case 1: case 2: case 1 | 2: put(buildablePart(reinf, "horizontal"), base.x); break;
    default:
        // Where more runs meet than the pack drew: the post and the planks crossing it.
        put(buildablePart(reinf, "vertical"), base.x);
        put(buildablePart(reinf, "horizontal"), base.x);
        break;
    }
}

// The compound's wire gates (Tiles/Wire-Fence): flat like the fence, swinging open
// with the pack's opening and closing frames, locked or not.
static void drawFenceGate(int x, int y, const Tile& t, Color tint) {
    static std::unordered_map<int, float> last;   // was it opening or closing
    bool lock = (t.variant & 1) != 0;
    float px = (float)x * TILE, py = (float)y * TILE;
    float open = gateOpenness(x, y);
    int key = y * 4096 + x;
    float before = last.count(key) ? last[key] : open;
    last[key] = open;
    if (open <= 0.02f) {
        if (const Assets::Sprite* sp = Assets::find(lock ? "tiles/wire-fence/wire-fence_gate_lock" : "tiles/wire-fence/wire-fence_gate"))
            R::frame(sp->frame(0), px, py, TILE, TILE, tint);
        return;
    }
    bool closing = open < before;
    std::string k = std::string("tiles/wire-fence/wire-fence_") + (closing ? "closing" : "opening") + (lock ? "" : "_no-lock");
    const Assets::Sprite* sp = Assets::find(k);
    if (!sp) return;
    int n = sp->frameCount();
    int fr = closing ? std::min(n - 1, (int)((1.0f - open) * n)) : std::min(n - 1, (int)(open * n));
    const Assets::Frame& f = sp->frame(fr);
    R::frame(f, std::floor(px + (TILE - f.w) * 0.5f), py + TILE - f.h, (float)f.w, (float)f.h, tint);
}

// A downspout (Tiles/Gutter-And-Downspout, 0.12v): one of the sheet's four drop pipes,
// grey or rusty, straight into the ground or bent out over it; `base` is the bottom of
// the wall it runs down. In the rain the bent ones pour (Downspout_Rainwater).
static void drawDownspout(Vec2 base, int pick) {
    static const Assets::Sprite* sheet = Assets::find("tiles/gutter-and-downspout");
    static const Assets::Sprite* pour = Assets::find("objects/nature/flowers_mashrooms_other-nature-stuff/puddles-and-water-anim/animations/downspout_rainwater");
    if (!sheet) return;
    int col = pick & 3;
    bool bent = (col & 1) != 0;
    // Straight ones: rows 0-1 end in a foot at y 24. Bent ones: the elbow's mouth at y 40.
    float top = bent ? 8.0f : 0.0f, bottom = bent ? 41.0f : 25.0f;
    Assets::Frame f = sheet->frame(0).sub(col * 16.0f, top, 16, bottom - top);
    // The foot of a straight one stands at the wall's bottom; a bent one's mouth sticks
    // out a little onto the ground in front.
    float x = std::floor(base.x - 8), y = std::floor(base.y + (bent ? 7.0f : 1.0f) - f.h);
    R::frame(f, x, y, (float)f.w, (float)f.h);
    float rain = Atmo::rainAmount();
    if (bent && pour && rain > 0.15f) {
        int fr = (int)(G.realTime * 10.0f + base.x * 0.13f) % pour->frameCount();
        const Assets::Frame& pf = pour->frame(fr);
        R::frame(pf, std::floor(base.x - pf.w * 0.5f), y + f.h - 3, (float)pf.w, (float)pf.h, Color(1, 1, 1, clampf(rain * 1.5f, 0, 1)));
    }
}

void drawTileSolids(World& w, Vec2 cam, float timeSec) {
    int x0, y0, x1, y1;
    viewRange(w, cam, x0, y0, x1, y1, 5);
    // Buildings throw their whole shape while enough of their walls stand to hold the
    // roof up; once it has fallen in, only the walls left cast anything.
    bool sun = s_sunWorld == &w && R::sunStrength() > 0.001f;
    if (sun) {
        for (const Building& b : w.buildings) {
            if (b.x0 > x1 + 4 || b.y0 > y1 + 4 || b.x0 + b.w < x0 - 6 || b.y0 + b.h < y0 - 6) continue;
            int standing = 0;
            for (int ty = b.y0; ty < b.y0 + b.h; ty++)
                for (int tx = b.x0; tx < b.x0 + b.w; tx++)
                    if ((tx == b.x0 || ty == b.y0 || tx == b.x0 + b.w - 1 || ty == b.y0 + b.h - 1) && w.inBounds(tx, ty) &&
                        w.at(tx, ty).solid != S_NONE) standing++;
            if (b.walls > 0 && standing * 5 < b.walls * 2) continue;
            R::shadowBox(b.x0 * (float)TILE, b.y0 * (float)TILE, (b.x0 + b.w) * (float)TILE, (b.y0 + b.h) * (float)TILE, 26);
        }
    }
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            const Tile& t = w.at(x, y);
            // Cars and street lights are props: several tiles of art drawn from one
            // anchor, so their tiles do not draw themselves.
            if (t.solid == S_NONE || t.solid == S_CAR || t.solid == S_POLE || t.solid == S_TURRET || t.solid == S_CRYPT_PROP || t.solid == S_CRYPT_GATE ||
                t.solid == S_VOID || t.solid == S_STAIRS) continue;
            float px = (float)x * TILE, py = (float)y * TILE;
            if (t.solid == S_CRYPT_WALL) {
                // Seen from above: a wall with open floor to its south shows its brick
                // face; the rest is the black top of the rock, rimmed in stone where it
                // meets a room.
                const CryptArt& a = cryptArt();
                auto rock = [&](int tx, int ty) { return !w.inBounds(tx, ty) || w.at(tx, ty).solid == S_CRYPT_WALL; };
                if (!rock(x, y + 1)) {
                    const Assets::Sprite* f = rock(x, y - 1) && !rock(x, y + 1) && a.face[(x * 7 + y * 3) % 6] ? a.face[(x * 7 + y * 3) % 6] : a.faceTop;
                    if (f) R::frame(f->frame(0), px, py, TILE, TILE);
                    if (!rock(x, y - 1) && a.rimN) R::frame(a.rimN->frame(0), px, py, TILE, TILE);
                    continue;
                }
                if (a.cap) R::frame(a.cap->frame(0), px, py, TILE, TILE);
                else R::rect(px, py, TILE, TILE, Color(0.03f, 0.03f, 0.04f));
                bool faceBelow = !rock(x, y + 2) && rock(x, y + 1);
                if (faceBelow && a.rimS) R::frame(a.rimS->frame(0), px, py, TILE, TILE);
                if (!rock(x, y - 1) && a.rimN) R::frame(a.rimN->frame(0), px, py, TILE, TILE);
                if (!rock(x - 1, y) && a.rimW) R::frame(a.rimW->frame(0), px, py, TILE, TILE);
                if (!rock(x + 1, y) && a.rimE) R::frame(a.rimE->frame(0), px, py, TILE, TILE);
                continue;
            }
            Vec2 base(px + TILE * 0.5f, py + TILE);
            const SolidInfo& si = solidInfo(t.solid);
            if (sun) {
                // Each wall, fence and barricade throws its own, so a wall blown away
                // takes its shadow with it.
                float hgt = 0;
                switch (t.solid) {
                case S_WALL_BRICK: case S_WALL_CONCRETE: case S_WALL_WOOD: case S_DOOR: hgt = 20; break;
                case S_BUNKER: hgt = 16; break;
                case S_CRATE: case S_SANDBAG: hgt = 10; break;
                case S_FENCE: case S_FENCE_GATE: hgt = 7; break;
                case S_BARRICADE: case S_GATE: hgt = 12; break;
                default: break;
                }
                if (hgt > 0) R::shadowBox(px, py, px + TILE, py + TILE, hgt);
            }
            Color tint;
            if (si.hp > 0 && t.hp < si.hp) {
                float f = clampf(t.hp / (float)si.hp, 0.25f, 1.0f);
                tint = Color(f, f * 0.85f + 0.15f, f * 0.85f + 0.15f);
            }
            switch (t.solid) {
            case S_CONTAINER: {
                if (t.container < 0 || t.container >= (int)w.containers.size()) break;
                const Container& c = w.containers[t.container];
                bool empty = c.searched && std::all_of(c.items.begin(), c.items.end(), [](const Item& i) { return i.empty(); });
                Art::Piece p = Art::containerArt(c.kind, c.variant);
                if (c.kind == CK_CHEST || c.kind == CK_URN) {
                    std::string key = std::string("catacombs/") + (c.kind == CK_CHEST ? (c.searched ? "chest_open_" : "chest_") : (c.searched ? "urn_broken_" : "urn_")) +
                                      std::to_string(c.variant % (c.kind == CK_CHEST ? 4 : 6));
                    if (const Assets::Sprite* s = Assets::find(key)) p = Art::Piece{s, 0, false, 1.0f};
                }
                Color ct = empty ? Color(0.65f, 0.65f, 0.7f) : Color();
                if (p.valid()) sceneAdd(p, base, ct);
                else sceneAddSprite(C_CRATE, {base.x, base.y - TILE * 0.5f}, ct);
                break;
            }
            case S_TREE: {
                Art::Piece p = Art::tree(t.variant, timeSec, t.tone);
                if (p.valid()) sceneAdd(p, base + Vec2(0, 3), tint);
                else sceneAddSprite(TREE, {base.x, base.y - TILE * 0.5f}, tint);
                break;
            }
            case S_BUSH: {
                Art::Piece p = Art::bush(t.variant, timeSec, t.tone);
                if (p.valid()) sceneAdd(p, base + Vec2(0, 2), tint);
                else sceneAddSprite(BUSH, {base.x, base.y - TILE * 0.5f}, tint);
                break;
            }
            case S_ROCK: {
                Art::Piece p = Art::rock(t.variant);
                if (p.valid()) sceneAdd(p, base + Vec2(0, 2), tint, 1.6f);
                else sceneAddSprite(ROCK, {base.x, base.y - TILE * 0.5f}, tint);
                break;
            }
            case S_CRATE:
            case S_SANDBAG: {
                Art::Piece p = Art::barrel(t.solid == S_CRATE ? t.variant : (uint8_t)(t.variant + 3));
                if (p.valid()) sceneAdd(p, base + Vec2(0, 2), tint);
                else sceneAddSprite(t.solid == S_CRATE ? CRATE : SANDBAG, {base.x, base.y - TILE * 0.5f}, tint);
                break;
            }
            case S_BOUNDARY: {
                Art::Piece p = Art::tree((uint8_t)(t.variant + x + y), timeSec, t.tone);
                Color dark(0.45f, 0.45f, 0.55f);
                if (p.valid()) sceneAdd(p, base + Vec2(0, 3), dark);
                else R::spriteRect(BOUNDARY, px, py, TILE, TILE);
                break;
            }
            case S_FURNITURE:
                if (t.furn && t.furn != FURN_REST && t.furn <= FURN_PIECE_COUNT) {
                    const FurnPiece& f = FURN_PIECES[t.furn - 1];
                    if (const Assets::Sprite* s = Assets::find(std::string("furniture/") + f.key))
                        sceneAdd(Art::Piece{s, 0, false, 1.0f}, Vec2(px + f.w * TILE * 0.5f, py + TILE), tint);
                } else if (t.deco) R::spriteRect(t.deco, px, py, TILE, TILE);
                break;
            case S_DOOR:
            case S_DOOR_OPEN: {
                // Which wall the door is in. Front (south) and back (north) doors face
                // the camera; side doors are seen edge-on. Everything but the front
                // is drawn here, before the roof, so the roof hides it until you are
                // inside - the chevron on the roof still marks the way in.
                enum { FACE_FRONT, FACE_BACK, FACE_SIDE } face = FACE_FRONT;
                auto doorAt = [&](int dx, int dy) {
                    return w.inBounds(dx, dy) && (w.at(dx, dy).solid == S_DOOR || w.at(dx, dy).solid == S_DOOR_OPEN);
                };
                bool onEdge = false;
                for (const Building& b : w.buildings) {
                    if (!b.isDoor(x, y)) continue;
                    onEdge = true;
                    if (y == b.y0 + b.h - 1) face = FACE_FRONT;
                    else if (y == b.y0) face = FACE_BACK;
                    else face = FACE_SIDE;
                    break;
                }
                // A doorway in an inner wall: stacked door tiles mean the wall runs
                // up and down, so it is seen edge-on.
                if (!onEdge && (doorAt(x, y - 1) || doorAt(x, y + 1))) face = FACE_SIDE;
                bool open = t.solid == S_DOOR_OPEN, alt = (t.variant & 1) != 0;
                Art::Piece p = Art::door((uint8_t)(open ? (alt ? 3 : 1) : (alt ? 2 : 0)));
                if (face == FACE_SIDE && p.valid()) {
                    // One edge-on leaf per doorway, standing on its lower tile.
                    if (doorAt(x, y + 1)) break;
                    const Assets::Frame& f = p.sprite->frame(p.frame);
                    float dw = open ? 4.0f : 6.0f, dh = (float)f.h;
                    R::frame(f, std::floor(px + (TILE - dw) * 0.5f), py + TILE - dh, dw, dh, tint);
                    break;
                }
                // A back door hangs down into the room, so all of it stays under the roof.
                Vec2 at = face == FACE_BACK ? base + Vec2(0, 8) : base;
                if (p.valid()) sceneAdd(p, at, tint);
                else R::spriteRect(WALL_WOOD, px, py, TILE, TILE, tint);
                break;
            }
            case S_BARRICADE:
            case S_GATE:
            case S_GATE_OPEN:
                drawBarricadeTile(w, x, y, t, tint);
                break;
            case S_FENCE_GATE:
            case S_FENCE_GATE_OPEN:
                drawFenceGate(x, y, t, tint);
                break;
            default: {
                // A fence runs on into its gates.
                auto same = [&](int sx, int sy) {
                    int o = w.at(sx, sy).solid;
                    return o == t.solid || (t.solid == S_FENCE && (o == S_FENCE_GATE || o == S_FENCE_GATE_OPEN));
                };
                int mask = 0;
                if (x > 0 && same(x - 1, y)) mask |= 1;
                if (x + 1 < w.w && same(x + 1, y)) mask |= 2;
                if (y > 0 && same(x, y - 1)) mask |= 4;
                if (y + 1 < w.h && same(x, y + 1)) mask |= 8;
                if (t.solid == S_WALL_WOOD || t.solid == S_WALL_CONCRETE) {
                    for (const Building& b : w.buildings) {
                        if (x < b.x0 || y < b.y0 || x >= b.x0 + b.w || y >= b.y0 + b.h) continue;
                        if (x == b.x0) mask |= 16;
                        if (x == b.x0 + b.w - 1) mask |= 32;
                        if (y == b.y0) mask |= 64;
                        if (y == b.y0 + b.h - 1) mask |= 128;
                        break;
                    }
                }
                Art::Piece p = Art::wallTile(t.solid, t.variant, mask);
                if (p.valid()) R::frame(p.sprite->frame(p.frame), px, py, TILE, TILE, tint);
                else R::spriteRect(si.sprite, px, py, TILE, TILE, tint);
                break;
            }
            }
        }

    // Cars, street lights and loose containers.
    Vec2 camF(std::floor(cam.x), std::floor(cam.y));
    float vw = (float)R::viewW(), vh = (float)R::viewH();
    for (const WorldProp& p : w.props) {
        if (p.pos.x < camF.x - 64 || p.pos.y < camF.y - 96 || p.pos.x > camF.x + vw + 64 || p.pos.y > camF.y + vh + 64) continue;
        if (p.kind == PROP_WRECK) {
            Art::Piece wp = Art::wreck(p.variant, p.frame);
            sceneAdd(wp, p.pos);
            if (wp.valid()) sceneLift(spriteEmptyRowsBelow(wp.sprite, wp.frame));
            continue;
        }
        if (p.kind == PROP_OBJECT) {
            Art::Piece op = Art::propArt(p);
            sceneAdd(op, p.pos);
            if (op.valid()) sceneLift(spriteEmptyRowsBelow(op.sprite, op.frame));
            continue;
        }
        if (p.kind == PROP_WALLDECO) {
            // Hangs on a front wall tile; shot away with it.
            int tx = World::toTile(p.pos.x), ty = World::toTile(p.pos.y - 1);
            if (!w.inBounds(tx, ty) || w.at(tx, ty).solid == S_NONE) continue;
            if (p.variant == Art::OB_DOWNSPOUT) { drawDownspout(p.pos, p.frame & 63); continue; }
            Art::Piece dp = Art::object(p.variant, p.frame & 63, p.frame >> 6);
            if (dp.valid()) R::spriteAt(*dp.sprite, dp.frame, p.pos, R::Pivot::Bottom, 1, Color(), dp.flipX != p.flipX);
            continue;
        }
        if (p.kind == PROP_STAIRS) {
            // A flight of stairs lies flat on the floor: two strips, runner and plain.
            // The way down is drawn darker, a stairwell going under the floor.
            static const Assets::Sprite* a = Assets::find("furniture/stairs_runner");
            static const Assets::Sprite* b = Assets::find("furniture/stairs_plain");
            bool down = p.variant < w.stairs.size() && !w.stairs[p.variant].up;
            Color tint = down ? Color(0.62f, 0.6f, 0.66f) : Color();
            if (a) R::spriteAt(*a, 0, p.pos + Vec2(-8, 0), R::Pivot::Bottom, 1, tint);
            if (b) R::spriteAt(*b, 0, p.pos + Vec2(8, 0), R::Pivot::Bottom, 1, tint);
            if (!a || !b) R::rect(p.pos.x - 16, p.pos.y - 32, 32, 32, down ? pal(P_DARK) : pal(P_TAN));
            continue;
        }
        if (p.kind == PROP_GARAGE) {
            // The mechanic's sign over his workshop door.
            const char* sign = "GARAGE";
            float tw = (float)R::textWidth(sign);
            R::rect(std::floor(p.pos.x - tw / 2 - 4), p.pos.y - 11, tw + 8, 11, Color(0.12f, 0.1f, 0.12f));
            R::rectOutline(std::floor(p.pos.x - tw / 2 - 4), p.pos.y - 11, tw + 8, 11, Color(0.75f, 0.55f, 0.2f));
            R::text(sign, std::floor(p.pos.x - tw / 2), p.pos.y - 9, Color(0.95f, 0.75f, 0.3f));
            continue;
        }
        if (p.kind >= PROP_CRYPT_DOOR) {
            const CryptArt& a = cryptArt();
            int fr = (int)(timeSec * 8.0f) + p.variant;
            switch (p.kind) {
            case PROP_CRYPT_DOOR: {
                // A short walk of old slabs up to the steps (the trodden earth round it
                // is the tiles' own dirt), then the arch with a torch burning either side.
                int tx = World::toTile(p.pos.x), ty = World::toTile(p.pos.y) - 1;
                for (int y = ty + 1; y <= ty + 3; y++)
                    for (int x = tx - 1; x <= tx; x++) {
                        uint32_t hsh = (uint32_t)(x * 73856093) ^ (uint32_t)(y * 19349663);
                        hsh ^= hsh >> 13; hsh *= 0x5bd1e995u; hsh ^= hsh >> 15;
                        if (y == ty + 3 && hsh % 2) continue;
                        if (const Assets::Sprite* s = cryptFloor((uint8_t)(hsh >> 8)))
                            R::frame(s->frame(0), x * (float)TILE, y * (float)TILE, TILE, TILE, y == ty + 3 ? Color(1, 1, 1, 0.75f) : Color());
                    }
                const Assets::Sprite* arch = a.stairsOut ? a.stairsOut : a.stairs;
                if (arch) sceneAdd(Art::Piece{arch, 0, false, 1.0f}, p.pos);
                if (a.torch)
                    for (float ox : {-26.0f, 26.0f}) sceneAdd(Art::Piece{a.torch, fr + (ox > 0 ? 2 : 0), false, 1.0f}, p.pos + Vec2(ox, -38), Color(), 1, 38.5f);
                break;
            }
            case PROP_CRYPT_GATE: {
                // The portcullis: its bars rise into the lintel once it is opened.
                if (p.variant >= w.dungeons.size()) break;
                Dungeon& d = w.dungeons[p.variant];
                bool open = w.gateOpen(p.variant);
                d.gateAnim = open ? std::min(1.0f, d.gateAnim + G.frameDt * 0.8f) : 0.0f;
                if (a.gateBars && d.gateAnim < 1) {
                    const Assets::Frame& bf = a.gateBars->frame(0);
                    float lift = d.gateAnim * d.gateAnim * (float)bf.h;
                    float x0 = p.pos.x - bf.w * 0.5f, yTop = p.pos.y - bf.h;
                    if (lift <= 0) sceneAdd(Art::Piece{a.gateBars, 0, false, 1.0f}, p.pos, Color(), 1, -0.5f);
                    else {
                        // Clipped at the lintel: only the part still below it shows.
                        Assets::Frame part = bf;
                        part.v0 = bf.v0 + (bf.v1 - bf.v0) * (lift / bf.h);
                        part.h = (int)(bf.h - lift);
                        if (part.h > 0) R::frame(part, x0, yTop, (float)bf.w, bf.h - lift);
                    }
                }
                if (a.gateFrame) sceneAdd(Art::Piece{a.gateFrame, 0, false, 1.0f}, p.pos);
                break;
            }
            // Flat on the wall and the floor: the arch out, torches, candles, spikes.
            case PROP_CRYPT_EXIT: if (a.stairs) R::spriteAt(*a.stairs, 0, p.pos, R::Pivot::Bottom); break;
            case PROP_TORCH: if (a.torch) R::spriteAt(*a.torch, fr, p.pos, R::Pivot::Bottom); break;
            case PROP_CANDLE: {
                const Assets::Sprite* s = (p.variant & 1) ? a.candleA : a.candleB;
                if (p.variant & 4) s = a.candles[(p.variant >> 3) & 3];
                if (s) R::spriteAt(*s, fr, p.pos, R::Pivot::Bottom);
                break;
            }
            case PROP_SPIKES:
                if (a.spikes) {
                    float c = spikeCycle(p, timeSec);
                    int f = c < 1.4f ? 0 : c < 1.6f ? 1 + (int)((c - 1.4f) / 0.07f) : c < 2.2f ? a.spikes->frameCount() - 1 : std::max(0, a.spikes->frameCount() - 1 - (int)((c - 2.2f) / 0.05f));
                    R::spriteAt(*a.spikes, std::clamp(f, 0, a.spikes->frameCount() - 1), p.pos, R::Pivot::Center);
                }
                break;
            case PROP_PILLAR: {
                const Assets::Sprite* s = a.pillar[3 + (p.variant & 1)];
                if (s) sceneAdd(Art::Piece{s, 0, false, 1.0f}, p.pos);
                break;
            }
            case PROP_COFFIN:
                if (const Assets::Sprite* s = a.coffin[p.variant & 1]) sceneAdd(Art::Piece{s, 0, false, 1.0f}, p.pos);
                break;
            default: break;
            }
            continue;
        }
        Art::Piece art = p.kind == PROP_CAR ? Art::car(p.variant, p.frame) : Art::streetLight(p.variant);
        art.flipX = art.flipX != p.flipX;   // which way round the art pack drew it
        sceneAdd(art, p.pos);
    }
    for (const Container& c : w.containers) {
        if (c.removed || c.tx >= 0) continue;
        if (c.pos.x < camF.x - 48 || c.pos.y < camF.y - 64 || c.pos.x > camF.x + vw + 48 || c.pos.y > camF.y + vh + 48) continue;
        bool empty = c.searched && std::all_of(c.items.begin(), c.items.end(), [](const Item& i) { return i.empty(); });
        Color ct = empty ? Color(0.65f, 0.65f, 0.7f) : Color();
        Art::Piece art = c.kind == CK_CORPSE ? Art::corpse(c.variant) : Art::containerArt(c.kind, c.variant);
        if (art.valid()) {
            sceneAdd(art, c.pos + Vec2(0, 4), ct);
            sceneNoReflect();
            // A helmet that rolled off as they fell (Character/Helmet death).
            if (c.kind == CK_CORPSE && (c.variant & 0x40)) {
                Art::Piece hm = Art::helmetFall((c.variant & 1) != 0, 99);
                if (hm.valid()) { sceneAdd(hm, c.pos + Vec2(0, 4), ct, 1, 0.01f); sceneNoReflect(); }
            }
        }
        else sceneAddSprite(c.kind == CK_CORPSE ? C_CORPSE : C_BAG, c.pos, ct);
    }
}

namespace {

// A way in that the roof is hiding, marked with two short strokes aimed the way you
// would walk through it. Deliberately plain: it is a hint, not scenery.
void drawEntranceMark(Vec2 c, int dir, float alpha) {
    Vec2 apex, a, b;
    switch (dir) {
    case 0:  apex = c + Vec2(0, -4); a = c + Vec2(-5, 2);  b = c + Vec2(5, 2);  break;   // up
    case 1:  apex = c + Vec2(-4, 0); a = c + Vec2(2, -5);  b = c + Vec2(2, 5);  break;   // left
    default: apex = c + Vec2(4, 0);  a = c + Vec2(-2, -5); b = c + Vec2(-2, 5); break;   // right
    }
    // A pale halo first: the roofs are dark, and a black mark on its own vanishes
    // into them.
    Color halo = pal(P_CREAM, alpha * 0.7f);
    R::line(apex, a, 3.5f, halo);
    R::line(apex, b, 3.5f, halo);
    Color ink = pal(P_DARK, alpha);
    R::line(apex, a, 1.6f, ink);
    R::line(apex, b, 1.6f, ink);
}

}  // namespace

float (*g_roofPeek)(int tx, int ty) = nullptr;

void drawRoofs(World& w, Vec2 cam, Vec2 viewer, float dt) {
    if (w.buildings.empty()) return;
    int vx = World::toTile(viewer.x), vy = World::toTile(viewer.y);
    float camX = std::floor(cam.x), camY = std::floor(cam.y);
    float vw = (float)R::viewW(), vh = (float)R::viewH();

    std::vector<uint8_t> roofDrawn(w.buildings.size(), 0);
    std::vector<uint8_t> standingWalls(w.buildings.size(), 0);
    std::vector<uint8_t> inView(w.buildings.size(), 0);

    for (size_t bi = 0; bi < w.buildings.size(); bi++) {
        Building& b = w.buildings[bi];
        bool inside = vx >= b.x0 && vx < b.x0 + b.w && vy >= b.y0 && vy < b.y0 + b.h;
        float target = inside ? 1.0f : 0.0f;
        b.reveal += (target - b.reveal) * (1.0f - std::exp(-12.0f * dt));

        float px0 = b.x0 * (float)TILE, py0 = b.y0 * (float)TILE;
        if (px0 + b.w * TILE < camX || py0 + b.h * TILE < camY || px0 > camX + vw || py0 > camY + vh) continue;
        inView[bi] = 1;

        // Once enough of the walls have been shot away the roof is gone with them.
        bool intact = true;
        if (b.walls > 0) {
            int standing = 0;
            for (int ty = b.y0; ty < b.y0 + b.h; ty++)
                for (int tx = b.x0; tx < b.x0 + b.w; tx++)
                    if ((tx == b.x0 || ty == b.y0 || tx == b.x0 + b.w - 1 || ty == b.y0 + b.h - 1) &&
                        w.inBounds(tx, ty) && w.at(tx, ty).solid != S_NONE) standing++;
            intact = standing * 5 >= b.walls * 2;
        }
        standingWalls[bi] = intact ? 1 : 0;
        if (!intact) continue;
        if (b.reveal > 0.98f) continue;

        int rh = b.h - 1;          // the bottom wall row stays uncovered
        if (rh < 1) continue;
        roofDrawn[bi] = 1;
        Color tint(1, 1, 1, 1.0f - b.reveal);
        if (b.flatRoof) {
            // A city block's flat concrete roof (0.12v), parapet round the edge, with
            // whatever stands up there.
            for (int ry = 0; ry < rh; ry++)
                for (int rx = 0; rx < b.w; rx++) {
                    int col = rx == 0 ? 0 : rx == b.w - 1 ? 2 : 1, row = ry == 0 ? 0 : ry == rh - 1 ? 2 : 1;
                    Assets::TileRef ref = Art::flatRoofTile(b.sheet, col, row, (uint8_t)hash2(b.x0 + rx, b.y0 + ry, 0x0F1A7u));
                    Color tc = tint;
                    if (g_roofPeek) tc.a *= 1.0f - g_roofPeek(b.x0 + rx, b.y0 + ry);
                    if (ref.valid() && tc.a > 0.01f) R::tileAt(ref, px0 + rx * TILE, py0 + ry * TILE, TILE, tc);
                }
            for (int i = b.roofProp0; i < b.roofProp0 + b.roofPropN && i < (int)w.roofProps.size(); i++) {
                const WorldProp& rp = w.roofProps[i];
                Art::Piece op = Art::object(rp.variant, rp.frame & 63, rp.frame >> 6);
                Color tc = tint;
                if (g_roofPeek) tc.a *= 1.0f - g_roofPeek(World::toTile(rp.pos.x), World::toTile(rp.pos.y - 1));
                if (op.valid() && tc.a > 0.01f) R::spriteAt(*op.sprite, op.frame, rp.pos, R::Pivot::Bottom, 1, tc, op.flipX != rp.flipX);
            }
            continue;
        }
        for (int ry = 0; ry < rh; ry++) {
            int row = rh == 1 ? 2
                    : ry == 0 ? 0
                    : ry == rh - 1 ? 4
                    : ry == rh / 2 ? 2
                    : (ry < rh / 2 ? 1 : 3);
            for (int rx = 0; rx < b.w; rx++) {
                int col = b.w <= 1 ? 1 : rx == 0 ? 0 : rx == b.w - 1 ? 2 : 1;
                Assets::TileRef ref = Art::roofTile(b.style, col, row);
                Color tc = tint;
                if (g_roofPeek) tc.a *= 1.0f - g_roofPeek(b.x0 + rx, b.y0 + ry);
                if (ref.valid() && tc.a > 0.01f) R::tileAt(ref, px0 + rx * TILE, py0 + ry * TILE, TILE, tc);
            }
        }
    }

    // Entrances, on top of the roofs. A door in the south wall faces the camera and
    // is never covered, so it gets the real door art. Every other opening - a door
    // in a side or back wall, or a hole blown through one - is only findable while
    // the roof is in the way, so it gets a mark instead.
    for (size_t bi = 0; bi < w.buildings.size(); bi++) {
        if (!inView[bi]) continue;
        const Building& b = w.buildings[bi];
        float alpha = 1.0f - b.reveal;
        for (int ty = b.y0; ty < b.y0 + b.h; ty++) {
            for (int tx = b.x0; tx < b.x0 + b.w; tx++) {
                bool edge = tx == b.x0 || ty == b.y0 || tx == b.x0 + b.w - 1 || ty == b.y0 + b.h - 1;
                if (!edge || !w.inBounds(tx, ty)) continue;
                int solid = w.at(tx, ty).solid;
                bool entrance = b.isDoor(tx, ty) && (solid == S_DOOR || solid == S_DOOR_OPEN);
                if (solid != S_NONE && !entrance) continue;

                if (ty == b.y0 + b.h - 1) {                     // south face, in plain sight
                    if (!standingWalls[bi] || !b.isDoor(tx, ty)) continue;
                    continue;
                }
                if (!roofDrawn[bi]) continue;                   // nothing is being hidden
                int dir = ty == b.y0 ? 0 : (tx == b.x0 ? 1 : 2);
                Vec2 mark = World::tileCenter(tx, ty);
                if (b.isDoor(tx, ty)) {
                    // A generated doorway is two tiles wide for collision. Treat it
                    // as one entrance marker, centered between the two leaves.
                    if (ty == b.y0) {
                        if (b.isDoor(tx - 1, ty)) continue;
                        if (b.isDoor(tx + 1, ty)) mark.x += TILE * 0.5f;
                    } else {
                        if (b.isDoor(tx, ty - 1)) continue;
                        if (b.isDoor(tx, ty + 1)) mark.y += TILE * 0.5f;
                    }
                }
                drawEntranceMark(mark, dir, alpha);
            }
        }
    }
}

// ---------------------------------------------------------------- save / load
static std::string savePath(int slot) {
    return dataPath("saves/outbound" + std::to_string(std::clamp(slot, 0, SAVE_SLOTS - 1) + 1) + ".sav");
}

bool save_exists(int slot) {
    std::ifstream f(savePath(slot));
    return (bool)f;
}

bool any_save_exists() {
    for (int i = 0; i < SAVE_SLOTS; i++)
        if (save_exists(i)) return true;
    return false;
}

bool delete_save(int slot) {
    bool ok = std::remove(savePath(slot).c_str()) == 0;
    persistSaves();
    return ok;
}

// Builds before save slots kept one file; adopt it as slot 1 rather than losing it.
void migrate_saves() {
    std::string legacy = dataPath("saves/outbound.sav");
    std::ifstream f(legacy);
    if (!f) return;
    f.close();
    if (save_exists(0)) return;
    std::rename(legacy.c_str(), savePath(0).c_str());
}

// Reads only the header fields, so the menu can label a slot without loading it.
SaveInfo save_info(int slot) {
    SaveInfo info;
    std::ifstream in(savePath(slot));
    if (!in) return info;
    std::string magic;
    int version = 0;
    in >> magic >> version;
    if (magic != "OUTBOUND_SAVE") return info;
    info.exists = true;
    std::string key;
    while (in >> key) {
        if (key == "day") in >> info.day;
        else if (key == "money") in >> info.money;
        else if (key == "time") in >> info.timeMin;
        else if (key == "gamemode") in >> info.difficulty >> info.gameMode;
        else if (key == "rivals") { int v = 0; in >> v; info.rivals = v != 0; }
        else if (key == "stats") {
            int deaths = 0;
            in >> info.raids >> info.extractions >> deaths >> info.kills;
        } else if (key == "inraid") {
            int v = 0;
            in >> v;
            info.inRaid = v != 0;
            break;
        }
    }
    return info;
}

// "id count data", plus " ~tier" for a gun that is not the ordinary tier. No save key
// starts with '~', so an older save (without it) still reads.
static void writeItem(std::ostream& o, const Item& it) {
    o << it.id << ' ' << it.count << ' ' << it.data;
    if (it.tier != 0) o << " ~" << (int)it.tier;
    if (it.flags != 0) o << " ^" << (int)it.flags;
    o << '\n';
}

// The day's explored/dead flags are bulky, so they go out as hex rather than as a
// few thousand decimal numbers.
static void writeBytes(std::ostream& o, const std::vector<uint8_t>& v) {
    static const char* HEX = "0123456789abcdef";
    o << v.size();
    if (!v.empty()) {
        o << ' ';
        for (uint8_t b : v) {
            o << HEX[b >> 4];
            o << HEX[b & 15];
        }
    }
    o << '\n';
}

static int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

static void readBytes(std::istream& i, std::vector<uint8_t>& v) {
    size_t n = 0;
    i >> n;
    v.assign(n, 0);
    if (!n) return;
    std::string hex;
    i >> hex;
    for (size_t k = 0; k < n && k * 2 + 1 < hex.size(); k++)
        v[k] = (uint8_t)((hexVal(hex[k * 2]) << 4) | hexVal(hex[k * 2 + 1]));
}
static Item readItem(std::istream& i) {
    Item it;
    int id = 0, count = 0, data = 0;
    i >> id >> count >> data;
    int tier = 0, flags = 0;
    for (;;) {
        i >> std::ws;
        if (i.peek() == '~') { i.get(); i >> tier; }
        else if (i.peek() == '^') { i.get(); i >> flags; }
        else break;
    }
    if (id <= IT_NONE || id >= IT_COUNT || count <= 0) return Item();
    it.id = (int16_t)id;
    it.count = (int16_t)count;
    it.data = data;
    it.tier = (int8_t)std::clamp(tier, -1, 3);
    it.flags = (uint8_t)flags;
    return it;
}

// The whole profile as text: the save file format, and also what a co-op guest's
// character travels as between the guest and the host who keeps it.
static void writeProfile(std::ostream& o, const Profile& p) {
    o << "OUTBOUND_SAVE 4\n";
    o << "missionsalt " << p.missionSalt << "\n";
    o << "day " << p.day << "\n";
    o << "worldseed " << p.worldSeed << "\n";
    o << "character " << p.shirt << ' ' << p.charName << "\n";
    o << "cryptdone " << p.cryptDoneDay << ' ' << p.cryptDoneMask << "\n";
    o << "crypt " << (p.cryptIntroSeen ? 1 : 0) << ' ' << p.cryptBanDay << ' ' << p.cryptBanMask << ' ' << p.locatorDay << "\n";
    o << "rivals " << (p.rivals ? 1 : 0) << "\n";
    o << "gamemode " << p.difficulty << ' ' << p.gameMode;
    for (int i = 0; i < HC_COUNT; i++) o << ' ' << (p.hcUnlock[i] ? 1 : 0);
    o << "\n";
    o << "dayrev " << p.dayRev << "\n";
    o << "time " << p.timeMin << "\n";
    o << "money " << p.money << "\n";
    o << "hp " << p.hp << "\n";
    o << "stats " << p.raids << ' ' << p.extractions << ' ' << p.deaths << ' ' << p.kills << ' ' << p.earned << "\n";
    o << "mission " << p.mission.day << ' ' << p.mission.type << ' ' << p.mission.target << ' '
      << p.mission.reward << ' ' << p.mission.collected << ' ' << (p.mission.claimed ? 1 : 0) << ' ' << p.mission.itemId << "\n";
    o << "mission2 " << p.mission.itemId2 << ' ' << p.mission.target2 << ' ' << p.mission.enemy << "\n";
    o << "crafter 1\n";   // 0.11v: lasers live on the guns
    o << "carhp 2\n";
    o << "locgift " << (p.locatorGift ? 1 : 0) << "\n";     // 0.11v: cars are much tougher (older cars get repaired on load)
    o << "cars " << p.activeCar << ' ' << (p.mechanicMet ? 1 : 0) << ' ' << p.carDay << ' ' << p.carRev << ' '
      << p.carPos.x << ' ' << p.carPos.y << ' ' << p.carAngle << "\n";
    for (int i = 0; i < CAR_MODELS; i++)
        if (p.cars[i].owned) o << "car " << i << ' ' << p.cars[i].color << ' ' << p.cars[i].hp << ' ' << p.cars[i].fuel << "\n";
    o << "missionstats " << p.missionsCompleted << ' ' << (p.laserUnlocked ? 1 : 0) << ' '
      << (p.laserOwned ? 1 : 0) << ' ' << (p.laserOn ? 1 : 0) << "\n";
    o << "inraid " << (p.inRaid ? 1 : 0) << "\n";
    if (p.inRaid && p.resume.valid) {
        const RaidResume& r = p.resume;
        o << "raidstate " << r.pos.x << ' ' << r.pos.y << ' ' << r.angle << ' ' << r.startMin << ' ' << r.kills << ' '
          << r.hordeN << ' ' << r.hordeLeft << ' ' << r.foes.size() << "\n";
        for (const RaidResume::Foe& f : r.foes) o << f.idx << ' ' << f.pos.x << ' ' << f.pos.y << ' ' << f.hp << "\n";
    }
    o << "upgrades";
    for (int i = 0; i < UP_COUNT; i++) o << ' ' << p.up[i];
    o << "\ncur " << p.curWeapon << "\n";
    o << "weapon0 "; writeItem(o, p.weapons[0]);
    o << "weapon1 "; writeItem(o, p.weapons[1]);
    o << "armor "; writeItem(o, p.armor);
    o << "backpack "; writeItem(o, p.backpack);
    o << "inv " << p.inv.size() << "\n";
    for (auto& it : p.inv) writeItem(o, it);
    o << "stash " << p.stash.size() << "\n";
    for (auto& it : p.stash) writeItem(o, it);
    o << "pstash " << p.privStash.size() << "\n";
    for (auto& it : p.privStash) writeItem(o, it);

    // Base defense and the people you pay.
    o << "defense " << p.hordeNum << ' ' << p.hordesRepelled << ' ' << p.hordeKills << ' '
      << p.nextHordeAt << ' ' << p.baseHp << ' ' << p.hires << "\n";
    o << "nightsafe " << p.safeNight << "\n";
    o << "tutorial " << (p.tutorialDone ? 1 : 0) << "\n";
    o << "defunlock";
    for (int i = 0; i < TT_COUNT; i++) o << ' ' << (p.turretUnlocked[i] ? 1 : 0);
    o << "\ndefup";
    for (int i = 0; i < DU_COUNT; i++) o << ' ' << p.defUp[i];
    o << "\nturrets " << p.turrets.size() << "\n";
    for (const Turret& t : p.turrets) o << t.dx << ' ' << t.dy << ' ' << t.type << ' ' << t.level << ' ' << t.hp << "\n";
    o << "barricades " << p.barricades.size() << "\n";
    for (const Barricade& b : p.barricades) o << b.dx << ' ' << b.dy << ' ' << b.type << ' ' << b.hp << "\n";
    // The dead are never written: dead is dead, even if they were still on the roster.
    int alive = 0;
    for (const Hireling& h : p.squad) alive += !h.dead && h.hp > 0;
    o << "squad " << alive << "\n";
    for (const Hireling& h : p.squad)
        if (!h.dead && h.hp > 0) o << h.name << ' ' << h.tier << ' ' << h.hp << ' ' << h.kills << ' ' << (h.guard ? 1 : 0) << "\n";

    // What you already did outside today, so a second trip on the same day picks up
    // where the first left off.
    const DayMemory& m = p.dayMem;
    o << "daymem " << m.day << "\n";
    if (m.day) {
        o << "dm_explored "; writeBytes(o, m.explored);
        o << "dm_dead "; writeBytes(o, m.spawnDead);
        o << "dm_opened " << m.openedIdx.size() << "\n";
        for (size_t i = 0; i < m.openedIdx.size(); i++) {
            o << m.openedIdx[i] << ' ' << m.openedItems[i].size() << "\n";
            for (const Item& it : m.openedItems[i]) writeItem(o, it);
        }
        o << "dm_dropped " << m.dropped.size() << "\n";
        for (const Container& c : m.dropped) {
            o << c.pos.x << ' ' << c.pos.y << ' ' << c.tx << ' ' << c.ty << ' ' << c.kind << ' '
              << (int)c.variant << ' ' << (c.searched ? 1 : 0) << ' ' << c.searchTime << ' '
              << c.items.size() << "\n";
            for (const Item& it : c.items) writeItem(o, it);
        }
    }
}

std::string profileToText(const Profile& p) {
    std::ostringstream o;
    writeProfile(o, p);
    return o.str();
}

bool save_game() {
    // A co-op guest's character lives in the host's save: "saving" sends it there.
    if (Coop::guest()) { Coop::sendProfile(); return true; }
    if (G.devNoSave || G.coopNoSave) return false;   // --raid / --base start a throwaway game
    ensureDir(dataPath("saves"));
    std::ofstream o(savePath(G.saveSlot));
    if (!o) return false;
    writeProfile(o, G.prof);
    o.close();
    if (Coop::host()) Coop::saveGuests();
    persistSaves();
    return true;
}

std::string coopGuestDir() {
    return dataPath("saves/outbound" + std::to_string(std::clamp(G.saveSlot, 0, SAVE_SLOTS - 1) + 1) + "_coop");
}

static bool readProfile(std::istream& in, Profile& p, bool& hadTurrets) {
    std::string magic;
    int version = 0;
    in >> magic >> version;
    if (magic != "OUTBOUND_SAVE") return false;
    p = Profile();
    hadTurrets = false;
    int tutorial = -1;
    bool crafterSave = false;   // written by 0.11v and later
    bool carHp2 = false;        // 0.11v: cars have far more health
    std::string key;
    while (in >> key) {
        if (key == "day") in >> p.day;
        else if (key == "missionsalt") in >> p.missionSalt;
        else if (key == "defense") in >> p.hordeNum >> p.hordesRepelled >> p.hordeKills >> p.nextHordeAt >> p.baseHp >> p.hires;
        else if (key == "nightsafe") in >> p.safeNight;
        else if (key == "tutorial") in >> tutorial;
        else if (key == "defunlock") for (int i = 0; i < TT_COUNT; i++) { int u = 0; in >> u; p.turretUnlocked[i] = u != 0; }
        else if (key == "defup") for (int i = 0; i < DU_COUNT; i++) in >> p.defUp[i];
        else if (key == "turrets") {
            size_t n = 0;
            in >> n;
            hadTurrets = true;
            p.turrets.clear();
            for (size_t i = 0; i < n; i++) {
                Turret t;
                in >> t.dx >> t.dy >> t.type >> t.level >> t.hp;
                t.type = std::clamp(t.type, 0, TT_COUNT - 1);
                t.level = std::clamp(t.level, 1, TURRET_MAX_LEVEL);
                if ((int)p.turrets.size() < MAX_TURRETS) p.turrets.push_back(t);
            }
        }
        else if (key == "barricades") {
            size_t n = 0;
            in >> n;
            p.barricades.clear();
            for (size_t i = 0; i < n; i++) {
                Barricade b;
                in >> b.dx >> b.dy >> b.type >> b.hp;
                b.type = std::clamp(b.type, 0, BT_COUNT - 1);
                if ((int)p.barricades.size() < MAX_BARRICADES) p.barricades.push_back(b);
            }
        }
        else if (key == "squad") {
            size_t n = 0;
            in >> n;
            p.squad.clear();
            for (size_t i = 0; i < n; i++) {
                Hireling h;
                int guard = 0;
                in >> h.name >> h.tier >> h.hp >> h.kills >> guard;
                h.tier = std::clamp(h.tier, 0, HIRE_TIERS - 1);
                h.guard = guard != 0;
                if ((int)p.squad.size() < SQUAD_MAX && h.hp > 0) p.squad.push_back(h);
            }
        }
        else if (key == "worldseed") in >> p.worldSeed;
        else if (key == "rivals") { int v = 0; in >> v; p.rivals = v != 0; }
        else if (key == "cryptdone") in >> p.cryptDoneDay >> p.cryptDoneMask;
        else if (key == "cars") {
            int met = 0;
            in >> p.activeCar >> met >> p.carDay >> p.carRev >> p.carPos.x >> p.carPos.y >> p.carAngle;
            p.mechanicMet = met != 0;
            p.activeCar = std::clamp(p.activeCar, -1, CAR_MODELS - 1);
        }
        else if (key == "car") {
            int i = -1;
            Profile::OwnedCar c;
            in >> i >> c.color >> c.hp >> c.fuel;
            if (i >= 0 && i < CAR_MODELS) {
                c.owned = true;
                c.color = std::clamp(c.color, 0, CAR_COLORS - 1);
                p.cars[i] = c;
            }
        }
        else if (key == "crypt") {
            int seen = 0;
            in >> seen >> p.cryptBanDay >> p.cryptBanMask;
            p.cryptIntroSeen = seen != 0;
            if (in.peek() == ' ') in >> p.locatorDay;
        }
        else if (key == "character") {
            in >> p.shirt;
            std::string rest;
            std::getline(in, rest);
            p.charName = cleanCharName(rest);
            p.shirt = std::clamp(p.shirt, 0, Assets::SHIRT_COUNT - 1);
        }
        else if (key == "gamemode") {
            in >> p.difficulty >> p.gameMode;
            for (int i = 0; i < HC_COUNT; i++) { int u = 0; in >> u; p.hcUnlock[i] = u != 0; }
            p.difficulty = std::clamp(p.difficulty, 0, DIFF_COUNT - 1);
            p.gameMode = std::clamp(p.gameMode, 0, MODE_COUNT - 1);
        }
        else if (key == "dayrev") in >> p.dayRev;
        else if (key == "time") in >> p.timeMin;
        else if (key == "money") in >> p.money;
        else if (key == "hp") in >> p.hp;
        else if (key == "stats") in >> p.raids >> p.extractions >> p.deaths >> p.kills >> p.earned;
        else if (key == "mission") {
            int claimed = 0;
            in >> p.mission.day >> p.mission.type >> p.mission.target >> p.mission.reward
               >> p.mission.collected >> claimed;
            if (version >= 3) in >> p.mission.itemId;
            p.mission.claimed = claimed != 0;
        }
        else if (key == "mission2") in >> p.mission.itemId2 >> p.mission.target2 >> p.mission.enemy;
        else if (key == "crafter") { int v; in >> v; crafterSave = v != 0; }
        else if (key == "carhp") { int v; in >> v; carHp2 = v >= 2; }
        else if (key == "locgift") { int v; in >> v; p.locatorGift = v != 0; }
        else if (key == "missionstats") { int u, o, on; in >> p.missionsCompleted >> u >> o >> on; p.laserUnlocked = u; p.laserOwned = o; p.laserOn = on; }
        else if (key == "inraid") { int v; in >> v; p.inRaid = v != 0; }
        else if (key == "raidstate") {
            RaidResume& r = p.resume;
            size_t n = 0;
            in >> r.pos.x >> r.pos.y >> r.angle >> r.startMin >> r.kills >> r.hordeN >> r.hordeLeft >> n;
            r.foes.clear();
            for (size_t i = 0; i < n && in; i++) {
                RaidResume::Foe f;
                in >> f.idx >> f.pos.x >> f.pos.y >> f.hp;
                r.foes.push_back(f);
            }
            r.valid = (bool)in;
        }
        else if (key == "upgrades") for (int i = 0; i < (version >= 3 ? UP_COUNT : UP_COUNT - 1); i++) in >> p.up[i];
        else if (key == "cur") in >> p.curWeapon;
        else if (key == "weapon0") p.weapons[0] = readItem(in);
        else if (key == "weapon1") p.weapons[1] = readItem(in);
        else if (key == "armor") p.armor = readItem(in);
        else if (key == "backpack") p.backpack = readItem(in);
        else if (key == "daymem") in >> p.dayMem.day;
        else if (key == "dm_explored") readBytes(in, p.dayMem.explored);
        else if (key == "dm_dead") readBytes(in, p.dayMem.spawnDead);
        else if (key == "dm_opened") {
            size_t n = 0;
            in >> n;
            p.dayMem.openedIdx.clear();
            p.dayMem.openedItems.clear();
            for (size_t i = 0; i < n; i++) {
                int idx = 0;
                size_t slots = 0;
                in >> idx >> slots;
                std::vector<Item> items;
                items.reserve(slots);
                for (size_t k = 0; k < slots; k++) items.push_back(readItem(in));
                p.dayMem.openedIdx.push_back(idx);
                p.dayMem.openedItems.push_back(std::move(items));
            }
        }
        else if (key == "dm_dropped") {
            size_t n = 0;
            in >> n;
            p.dayMem.dropped.clear();
            for (size_t i = 0; i < n; i++) {
                Container c;
                int variant = 0, searched = 0;
                size_t slots = 0;
                in >> c.pos.x >> c.pos.y >> c.tx >> c.ty >> c.kind >> variant >> searched >> c.searchTime >> slots;
                c.variant = (uint8_t)variant;
                c.searched = searched != 0;
                c.items.clear();
                c.items.reserve(slots);
                for (size_t k = 0; k < slots; k++) c.items.push_back(readItem(in));
                p.dayMem.dropped.push_back(std::move(c));
            }
        }
        else if (key == "inv" || key == "stash" || key == "pstash") {
            size_t n;
            in >> n;
            std::vector<Item>& v = key == "inv" ? p.inv : key == "stash" ? p.stash : p.privStash;
            for (size_t i = 0; i < n; i++) {
                Item it = readItem(in);
                if (i < v.size()) v[i] = it;
            }
        }
    }
    // Before 0.7v one switch turned the laser off for every gun: carry it over onto the
    // guns in hand, then retire it (saves now keep it on, so this only runs once).
    if (p.laserOwned && !p.laserOn)
        for (Item& w : p.weapons) if (!w.empty()) w.flags |= ITEMF_LASER_OFF;
    p.laserOn = true;
    // Before 0.11v the laser was bought once for every gun: whoever had it keeps one on
    // each gun they own, and from now on it is fitted gun by gun at the crafter.
    if (!crafterSave && p.laserOwned) {
        auto fit = [](Item& it) { if (weaponDef(it.id)) it.flags |= ITEMF_LASER; };
        for (Item& w : p.weapons) fit(w);
        for (Item& it : p.inv) fit(it);
        for (Item& it : p.stash) fit(it);
        for (Item& it : p.privStash) fit(it);
    }
    // Cars saved before they got tougher (0.11v): patched up and filled, on the house.
    if (!carHp2)
        for (Profile::OwnedCar& c : p.cars)
            if (c.owned) c.hp = -1;
    // Saves from before the bunker tour: only show it to someone who has never been out.
    p.tutorialDone = tutorial >= 0 ? tutorial != 0 : p.raids > 0;
    for (int i = 0; i < UP_COUNT; i++) p.up[i] = std::clamp(p.up[i], 0, UPGRADES[i].maxLevel);
    p.curWeapon = std::clamp(p.curWeapon, 0, 1);
    p.turretUnlocked[TT_GUN] = true;
    for (int i = 0; i < DU_COUNT; i++) p.defUp[i] = std::clamp(p.defUp[i], 0, defenseUpgradeDef(i).maxLevel);
    // A contract from before missions named their goods ("find any weapon") has
    // nothing to hand over: put the day's board back up instead.
    if (p.mission.deliver() && p.mission.itemId <= IT_NONE && !p.mission.claimed) {
        int day = p.mission.day;
        p.mission = DayMission();
        p.mission.day = day;
    }
    return true;
}

bool profileFromText(const std::string& text, Profile& out) {
    std::istringstream in(text);
    bool hadTurrets = false;
    return readProfile(in, out, hadTurrets);
}

bool load_game(int slot) {
    G.saveSlot = std::clamp(slot, 0, SAVE_SLOTS - 1);
    std::ifstream in(savePath(G.saveSlot));
    if (!in) return false;
    Profile p;
    bool hadTurrets = false;
    if (!readProfile(in, p, hadTurrets)) return false;
    G.prof = p;
    G.coopNoSave = false;
    // Saves from before the defenses start with the same two turrets a new game has.
    if (!hadTurrets) {
        G.prof.giveDefaultTurrets();
        rollNextHorde();
    }
    for (Turret& t : G.prof.turrets) {
        float mx = turretStats(t).maxHp;
        if (t.hp < 0 || t.hp > mx) t.hp = mx;
    }
    if (G.prof.baseHp < 0 || G.prof.baseHp > baseMaxHp()) G.prof.baseHp = baseMaxHp();
    if (G.prof.inRaid && G.prof.resume.valid) {
        // Closed mid-raid: Continue picks the raid back up (see raid_resume).
    } else if (G.prof.inRaid) {
        // An old save without the raid's state. Quitting during a raid counts as not making it home. The same day restarts,
        // but on a fresh layout: quitting out is not a way to get the world you were
        // already standing in back.
        G.prof.inRaid = false;
        G.prof.dayRev++;
        G.prof.dayMem.clear();
        G.summary = RaidSummary();
        applyDeathLoss();
        G.prof.deaths++;
        G.prof.timeMin = DAY_START_MIN;
        rollNextHorde();
        G.prof.hp = G.prof.maxHp();
        G.summary.died = true;
        G.summary.cause = "You abandoned the raid and never made it home.";
        G.summary.dayAfter = G.prof.day;
        G.panel = Panel::Summary;
        save_game();
    }
    return true;
}

int g_devDifficulty = -1, g_devMode = -1;   // --hardcore / --zmode: every new game uses them

std::string cleanCharName(const std::string& s) {
    std::string out;
    for (char c : s)
        if (c >= 32 && c < 127 && (int)out.size() < 16) out += c;
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

void new_game(int difficulty, int mode, const std::string& name, int shirt, bool rivals) {
    if (g_devDifficulty >= 0) difficulty = g_devDifficulty;
    if (g_devMode >= 0) mode = g_devMode;
    G.coopNoSave = false;
    G.prof = Profile();
    G.prof.difficulty = std::clamp(difficulty, 0, DIFF_COUNT - 1);
    G.prof.gameMode = std::clamp(mode, 0, MODE_COUNT - 1);
    G.prof.charName = cleanCharName(name);
    G.prof.rivals = rivals;
    G.prof.shirt = std::clamp(shirt, 0, Assets::SHIRT_COUNT - 1);
    G.prof.worldSeed = mix64((uint64_t)std::time(nullptr) * 2654435761ull + (uint64_t)G.saveSlot * 7919ull + 1);
    G.prof.weapons[0] = makeItem(IT_PISTOL);
    if (G.prof.hardcore()) {
        // Hardcore starts leaner: less money, one bandage, nothing waiting in the stash.
        G.prof.money = 100;
        addToSlots(G.prof.inv, makeItem(IT_AMMO_LIGHT, 36));
        addToSlots(G.prof.inv, makeItem(IT_BANDAGE, 1));
    } else {
        addToSlots(G.prof.inv, makeItem(IT_AMMO_LIGHT, 48));
        addToSlots(G.prof.inv, makeItem(IT_BANDAGE, 3));
        addToSlots(G.prof.stash, makeItem(IT_AMMO_LIGHT, 36));
        addToSlots(G.prof.stash, makeItem(IT_GRENADE, 1));
    }
    G.prof.hp = G.prof.maxHp();
    G.prof.giveDefaultTurrets();
    G.prof.baseHp = baseMaxHp();
    // The first horde gives you a day to find your feet: late morning on day 2.
    G.prof.nextHordeAt = rivals ? 1e9f : 1440 + 11 * 60;   // Rivals: no hordes
    rollDailyMission();
    save_game();
}
