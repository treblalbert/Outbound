#include "atmosphere.h"
#include "audio.h"
#include "assets.h"
#include "game.h"
#include "world.h"
#include <unordered_map>

namespace Atmo {

namespace {

struct Weather { float overcast = 0, rain = 0, fog = 0; };

struct Preset { const char* name; Weather w; int weight; bool wet; };   // name: for reading the table
const Preset PRESETS[] = {
    {"Clear", {0.0f, 0.0f, 0.0f}, 30, false},
    {"Hazy", {0.25f, 0.0f, 0.22f}, 10, false},
    {"Overcast", {0.8f, 0.0f, 0.0f}, 14, false},
    {"Drizzle", {0.7f, 0.35f, 0.0f}, 10, true},
    {"Rain", {0.9f, 0.7f, 0.0f}, 8, true},
    {"Storm", {1.0f, 1.0f, 0.1f}, 4, true},
    {"Fog", {0.4f, 0.0f, 0.75f}, 8, false},
    {"Rain + fog", {0.85f, 0.5f, 0.55f}, 6, true},
    {"Overcast + fog", {0.75f, 0.0f, 0.45f}, 6, false},
};
constexpr int PRESET_COUNT = sizeof(PRESETS) / sizeof(PRESETS[0]);
constexpr float SLOT_MIN = 150;        // game minutes each weather holds
constexpr float BLEND_FROM = 0.7f;     // the last 30% of a slot eases into the next

struct Mood { Color tint; Color lift; float sat, contrast; };

Weather s_cur;
float s_horde = 0;
int s_forced = -1;         // dev: pin one preset
float s_wet = 0;           // how much water is lying around: rises in rain, dries slowly
float s_stepT = 0;
struct Splash { Vec2 pos; float t; bool small = false; };
std::vector<Splash> s_splashes;   // footstep splashes in puddles

const char* WATER = "objects/nature/flowers_mashrooms_other-nature-stuff/puddles-and-water-anim/";

const Assets::Sprite* water(const std::string& name) {
    return Assets::find(std::string(WATER) + name);
}

// Puddle art per ground: on grass the rim takes the grass's own colour (the pack
// draws each puddle in plain, green, dark green and bleak yellow grass), mud for bare
// earth, dry ground for the rest.
const Assets::Sprite* puddleArt(int ground, int variant, int tone) {
    static const Assets::Sprite* cache[6][7];
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        const char* kinds[6] = {"puddle_on-grass_", "puddle_on-mud_", "puddle_on-dry-ground_", "puddle_on-grass_#_grass_green",
                                "puddle_on-grass_#_grass_dark-green", "puddle_on-grass_#_grass_bleak-yellow"};
        for (int k = 0; k < 6; k++)
            for (int v = 0; v < 7; v++) {
                std::string n = kinds[k];
                size_t at = n.find('#');
                if (at == std::string::npos) n += std::to_string(v + 1);
                else n.replace(at, 1, std::to_string(v + 1));
                cache[k][v] = water(n);
            }
    }
    int k = ground == G_DIRT ? 1 : ground == G_GRASS ? 0 : 2;
    if (ground == G_GRASS) {
        // The rim matches the grass: its palette zone, or a mix where it follows the ground.
        if (tone == TONE_DARK) k = 4;
        else if (tone == TONE_BLEAK || tone == TONE_ORANGE || tone == TONE_YELLOW || tone == TONE_RED) k = 5;
        else if (tone == TONE_GREEN || (variant & 8)) k = 3;
    }
    const Assets::Sprite* s = cache[k][variant % 7];
    return s ? s : cache[ground == G_GRASS ? 0 : k][variant % 7];
}

// True when the tile lies in a building's footprint: dry under its roof.
bool underRoof(const World& w, int tx, int ty) {
    for (const Building& b : w.buildings)
        if (tx >= b.x0 && ty >= b.y0 && tx < b.x0 + b.w && ty < b.y0 + b.h) return true;
    return false;
}

// Which tiles hold a puddle, and how wet it has to be before it shows. Only open,
// outdoor ground: never a floor, never under a roof.
bool puddleAt(const World& w, int tx, int ty, float& threshold, int& variant) {
    if (!w.inBounds(tx, ty)) return false;
    const Tile& t = w.at(tx, ty);
    if (t.solid != S_NONE) return false;
    if (t.ground != G_GRASS && t.ground != G_DIRT && t.ground != G_SAND && t.ground != G_WASTE && t.ground != G_ROAD &&
        t.ground != G_PAVEMENT) return false;
    uint32_t h = hash2(tx, ty, (uint32_t)w.seed ^ 0x9D11u);
    if ((h & 0xFFFF) > 0xFFFF * 0.05f) return false;
    if (underRoof(w, tx, ty)) return false;
    threshold = 0.15f + 0.6f * ((h >> 16) & 0xFF) / 255.0f;
    variant = (int)((h >> 24) % 7) | (int)((h >> 20) & 8);
    return true;
}
float s_flash = 0, s_boltT = 6;
int s_flicker = 0;

float hashf(uint64_t a, uint64_t b) { return (mix64(a * 0x9E3779B97F4A7C15ull ^ b) >> 40) / float(1 << 24); }

// Some days are wetter than others; rain presets are weighted up or down for the day.
int presetFor(int64_t slot) {
    const Profile& p = G.prof;
    int day = (int)(slot * SLOT_MIN / 1440.0f) + 1;
    float wetness = 0.3f + 1.7f * hashf(p.worldSeed ^ 0xC11A7E, (uint64_t)day);
    float total = 0;
    for (const Preset& pr : PRESETS) total += pr.weight * (pr.wet ? wetness : 1.0f);
    float r = hashf(p.worldSeed ^ 0x3EA7, (uint64_t)slot) * total;
    for (int i = 0; i < PRESET_COUNT; i++) {
        r -= PRESETS[i].weight * (PRESETS[i].wet ? wetness : 1.0f);
        if (r <= 0) return i;
    }
    return 0;
}

Weather lerpW(const Weather& a, const Weather& b, float t) {
    return {lerpf(a.overcast, b.overcast, t), lerpf(a.rain, b.rain, t), lerpf(a.fog, b.fog, t)};
}

// What the sky should be doing at this moment on the absolute clock.
Weather target() {
    if (s_forced >= 0) return PRESETS[s_forced].w;
    float at = absMinutes();
    int64_t slot = (int64_t)std::floor(at / SLOT_MIN);
    float f = at / SLOT_MIN - (float)slot;
    float t = clampf((f - BLEND_FROM) / (1.0f - BLEND_FROM), 0, 1);
    t = t * t * (3 - 2 * t);
    Weather w = lerpW(PRESETS[presetFor(slot)].w, PRESETS[presetFor(slot + 1)].w, t);
    // Morning mist burns off by mid-morning.
    float tod = G.prof.timeMin;
    float mist = clampf(1.0f - (tod - 6.5f * 60) / 150.0f, 0, 1) * clampf((tod - 4 * 60) / 60.0f, 0, 1);
    w.fog = std::max(w.fog, mist * 0.35f);
    return w;
}

// One mood per day's world, so a new map also feels like a new place.
Mood dayMood() {
    static const Mood MOODS[] = {
        {{1.00f, 1.00f, 1.00f}, {0, 0, 0}, 1.00f, 0.00f},          // plain
        {{1.06f, 1.00f, 0.88f}, {0.02f, 0.01f, 0}, 0.90f, 0.03f},   // warm and dusty
        {{0.92f, 0.98f, 1.08f}, {0, 0.01f, 0.03f}, 0.90f, 0.02f},   // cold
        {{0.96f, 1.05f, 0.90f}, {0, 0.02f, 0}, 0.85f, 0.04f},       // sickly green
        {{1.02f, 1.02f, 1.00f}, {0, 0, 0}, 1.15f, 0.05f},           // vivid
        {{1.00f, 0.98f, 0.96f}, {0.03f, 0.03f, 0.035f}, 0.75f, -0.03f},  // faded
    };
    const int n = sizeof(MOODS) / sizeof(MOODS[0]);
    uint64_t seed = todaySeed();
    Mood m = MOODS[mix64(seed ^ 0x600D) % n];
    // A little jitter on top, so two "warm" days are not identical.
    m.tint.r *= 0.97f + 0.06f * hashf(seed, 1);
    m.tint.g *= 0.97f + 0.06f * hashf(seed, 2);
    m.tint.b *= 0.97f + 0.06f * hashf(seed, 3);
    return m;
}

struct TodKey { float h; Color tint, lift; };
void timeGrade(float minutes, Color& tint, Color& lift) {
    static const TodKey KEYS[] = {
        {0.0f, {0.85f, 0.93f, 1.12f}, {0, 0.005f, 0.04f}},
        {5.0f, {0.85f, 0.93f, 1.12f}, {0, 0.005f, 0.04f}},
        {6.5f, {1.06f, 0.96f, 1.00f}, {0.03f, 0.01f, 0.03f}},     // dawn
        {8.5f, {1.00f, 1.00f, 1.00f}, {0, 0, 0}},
        {16.5f, {1.00f, 1.00f, 1.00f}, {0, 0, 0}},
        {19.0f, {1.12f, 1.00f, 0.84f}, {0.035f, 0.015f, 0}},     // golden hour
        {21.0f, {1.00f, 0.90f, 0.95f}, {0.02f, 0, 0.03f}},        // dusk
        {22.5f, {0.85f, 0.93f, 1.12f}, {0, 0.005f, 0.04f}},       // night
        {24.0f, {0.85f, 0.93f, 1.12f}, {0, 0.005f, 0.04f}},
    };
    float h = std::fmod(minutes / 60.0f, 24.0f);
    for (size_t i = 0; i + 1 < sizeof(KEYS) / sizeof(KEYS[0]); i++) {
        if (h < KEYS[i].h || h > KEYS[i + 1].h) continue;
        float t = (h - KEYS[i].h) / (KEYS[i + 1].h - KEYS[i].h);
        tint = {lerpf(KEYS[i].tint.r, KEYS[i + 1].tint.r, t), lerpf(KEYS[i].tint.g, KEYS[i + 1].tint.g, t), lerpf(KEYS[i].tint.b, KEYS[i + 1].tint.b, t)};
        lift = {lerpf(KEYS[i].lift.r, KEYS[i + 1].lift.r, t), lerpf(KEYS[i].lift.g, KEYS[i + 1].lift.g, t), lerpf(KEYS[i].lift.b, KEYS[i + 1].lift.b, t)};
        return;
    }
    tint = {1, 1, 1};
    lift = {0, 0, 0};
}

Color mul(Color a, Color b) { return {a.r * b.r, a.g * b.g, a.b * b.b}; }
Color mixC(Color a, Color b, float t) { return {lerpf(a.r, b.r, t), lerpf(a.g, b.g, t), lerpf(a.b, b.b, t)}; }

}  // namespace

void devForce(int preset) { s_forced = preset >= 0 && preset < PRESET_COUNT ? preset : -1; }

void reset() {
    s_cur = target();
    s_horde = 0;
    // Stepping out into rain that has been falling a while, the ground is already wet.
    s_wet = s_cur.rain;
    s_splashes.clear();
    s_flash = 0;
    s_boltT = 6;
}

void update(float dt, bool horde) {
    // The clock already blends weather slowly; this catches any leftover jump.
    Weather t = target();
    float k = 1.0f - std::exp(-0.6f * dt);
    s_cur = lerpW(s_cur, t, k);
    s_horde += ((horde ? 1.0f : 0.0f) - s_horde) * (1.0f - std::exp(-0.8f * dt));
    if (s_cur.rain > s_wet) s_wet = std::min(s_cur.rain, s_wet + dt * 0.04f);
    else s_wet = std::max(s_cur.rain, s_wet - dt * 0.006f);
    for (Splash& sp : s_splashes) sp.t += dt;
    s_splashes.erase(std::remove_if(s_splashes.begin(), s_splashes.end(), [](const Splash& sp) { return sp.t > 0.4f; }), s_splashes.end());

    // Storms throw lightning: a double flicker of white, then thunder.
    s_flash = std::max(0.0f, s_flash - dt * 5.0f);
    if (s_flicker > 0 && s_flash < 0.35f) { s_flash = 0.8f; s_flicker--; }
    if (s_cur.rain > 0.75f) {
        s_boltT -= dt;
        if (s_boltT <= 0) {
            s_boltT = (7.0f + 18.0f * hashf((uint64_t)(G.realTime * 1000), 7)) / s_cur.rain;
            s_flash = 1.0f;
            s_flicker = 1;
            Audio::play(Snd::explosion, 0.35f, 0.3f);
        }
    }
}

float rainAmount() { return s_cur.rain; }
float sunlight() { return clampf(1.0f - 0.72f * s_cur.overcast - 0.25f * s_cur.rain - 0.35f * s_cur.fog, 0.08f, 1.0f); }

float apply(LightingParams& lp, float minutes) {
    Mood mood = dayMood();
    Color todTint, todLift;
    timeGrade(minutes, todTint, todLift);
    float oc = s_cur.overcast, rn = s_cur.rain, fg = s_cur.fog;

    // Cloud and rain take light out of the day; lightning puts it back for an instant.
    Color amb = ambientColor(minutes);
    // Tuned so plain rain stays daylight; only a full storm is dark enough for the torch.
    float dim = 1.0f - 0.20f * oc - 0.06f * rn;
    amb = {amb.r * dim, amb.g * dim, amb.b * dim * (1.0f + 0.05f * oc)};
    amb = {amb.r + s_flash * 0.9f, amb.g + s_flash * 0.9f, amb.b + s_flash * 1.0f};
    lp.ambient = amb;
    float bright = clampf((amb.r + amb.g + amb.b) / 3.0f, 0, 1);
    lp.saturate = clampf(0.45f + bright * 0.7f, 0.45f, 1.0f);

    Color grade = mul(mood.tint, todTint);
    grade = mul(grade, mixC(Color(1, 1, 1), Color(0.94f, 0.97f, 1.04f), oc));
    grade = mul(grade, mixC(Color(1, 1, 1), Color(0.95f, 0.98f, 1.03f), rn));
    grade = mul(grade, mixC(Color(1, 1, 1), Color(1.15f, 0.86f, 0.84f), s_horde));
    lp.grade = grade;
    lp.lift = {mood.lift.r + todLift.r + 0.045f * s_horde, mood.lift.g + todLift.g, mood.lift.b + todLift.b + 0.01f * s_horde};

    lp.fog = clampf(fg * 0.6f, 0, 0.6f);
    lp.fogColor = mixC(Color(0.80f, 0.82f, 0.86f), Color(0.62f, 0.64f, 0.72f), oc);
    lp.fogTime = G.realTime;
    lp.fogWind = 1.0f + rn * 1.5f;

    // Post: eased by brightness instead of stepping at fixed thresholds.
    float day = clampf((bright - 0.4f) / 0.5f, 0, 1);
    PostFx& post = lp.post;
    post.enabled = true;
    post.vignette = lerpf(0.6f, 0.45f, day) + 0.18f * s_horde + 0.05f * oc;
    post.grain = lerpf(0.12f, 0.06f, day) + 0.03f * rn;
    post.bloom = lerpf(0.8f, 0.55f, day);
    post.saturation = lerpf(0.8f, 1.06f, day) * mood.sat * (1.0f - 0.2f * oc) * (1.0f - 0.25f * s_horde);
    post.contrast = lerpf(0.10f, 0.06f, day) + mood.contrast + 0.08f * s_horde + 0.03f * rn;
    post.aberration = 0.8f + 0.5f * s_horde;
    return bright;
}

void clearOutdoor(LightingParams& lp) {
    lp.grade = Color(1, 1, 1);
    lp.lift = Color(0, 0, 0);
    lp.fog = 0;
}

void drawGround(const World& w, Vec2 cam) {
    if (s_wet < 0.02f && s_splashes.empty()) return;
    float W = (float)R::viewW(), H = (float)R::viewH();
    int x0 = World::toTile(cam.x) - 2, y0 = World::toTile(cam.y) - 2;
    int x1 = World::toTile(cam.x + W) + 2, y1 = World::toTile(cam.y + H) + 2;
    const Assets::Sprite* ripple = water("animations/puddle-splash_3_normal");
    float t = G.realTime;
    for (int ty = y0; ty <= y1; ty++)
        for (int tx = x0; tx <= x1; tx++) {
            float th;
            int v;
            if (!puddleAt(w, tx, ty, th, v)) continue;
            float a = clampf((s_wet - th) * 4.0f, 0, 1);
            if (a <= 0) continue;
            const Assets::Sprite* art = puddleArt(w.at(tx, ty).ground, v, w.at(tx, ty).tone);
            if (!art) continue;
            uint32_t h = hash2(tx, ty, 0x51u);
            Vec2 c = World::tileCenter(tx, ty) + Vec2((float)(h % 7) - 3.0f, (float)((h >> 4) % 5) - 2.0f);
            R::spriteAt(*art, 0, c, R::Pivot::Center, 1, Color(1, 1, 1, a * 0.9f));
            // And it reflects what stands over it (0.11v), placed as spriteAt placed it.
            const Assets::Frame& pf = art->frame(0);
            R::waterFrame(pf, std::floor(c.x - pf.w * 0.5f), std::floor(c.y - pf.h * 0.5f), (float)pf.w, (float)pf.h, Color(0.85f, 0.92f, 1.0f, a));
            // Drops keep landing in it while it rains.
            if (ripple && s_cur.rain > 0.1f) {
                float phase = t * (1.2f + s_cur.rain * 2.0f) + (h & 255) / 255.0f * 7.0f;
                float f = phase - std::floor(phase);
                if (f < 0.6f) {
                    Vec2 off((float)((h >> 8) % 9) - 4.0f, (float)((h >> 12) % 3) - 1.0f);
                    int fr = std::min(ripple->frameCount() - 1, (int)(f / 0.6f * ripple->frameCount()));
                    R::spriteAt(*ripple, fr, c + off, R::Pivot::Center, 1, Color(1, 1, 1, a * 0.8f));
                }
            }
        }
    // Your own steps squash the water out; everyone else's (the dead, your mercs)
    // leave the small splash.
    const Assets::Sprite* kick = water("animations/puddle-splash_2_squished");
    const Assets::Sprite* flick = water("animations/puddle-splash_1_small");
    for (const Splash& sp : s_splashes) {
        const Assets::Sprite* a = sp.small && flick ? flick : kick;
        if (!a) continue;
        int fr = std::min(a->frameCount() - 1, (int)(sp.t / 0.4f * a->frameCount()));
        R::spriteAt(*a, fr, sp.pos, R::Pivot::Center, 1, Color(1, 1, 1, 0.9f));
    }
}

Vec2 nearestPuddle(const World& w, Vec2 from) {
    int fx = World::toTile(from.x), fy = World::toTile(from.y);
    for (int r = 0; r < 60; r++)
        for (int y = fy - r; y <= fy + r; y++)
            for (int x = fx - r; x <= fx + r; x++) {
                if (std::abs(x - fx) != r && std::abs(y - fy) != r) continue;
                float th;
                int v;
                if (puddleAt(w, x, y, th, v)) return World::tileCenter(x, y);
            }
    return from;
}

void footstep(const World& w, Vec2 feet, bool moving, float dt) {
    s_stepT -= dt;
    if (!moving || s_stepT > 0 || s_wet < 0.05f) return;
    float th;
    int v;
    int tx = World::toTile(feet.x), ty = World::toTile(feet.y);
    if (!puddleAt(w, tx, ty, th, v) || s_wet < th + 0.05f) return;
    if (dist(feet, World::tileCenter(tx, ty)) > 10) return;
    s_stepT = 0.28f;
    s_splashes.push_back({feet, 0});
    Audio::play(Snd::splash, 0.8f, 0.9f + 0.2f * hashf((uint64_t)(feet.x * 13), (uint64_t)(feet.y * 7)));
}

// Someone else walking through a puddle (0.12v): a small splash, now and then.
void otherStep(const World& w, Vec2 feet, uint32_t who) {
    if (s_wet < 0.05f || s_splashes.size() > 48) return;
    float th;
    int v;
    int tx = World::toTile(feet.x), ty = World::toTile(feet.y);
    if (!puddleAt(w, tx, ty, th, v) || s_wet < th + 0.05f) return;
    if (dist(feet, World::tileCenter(tx, ty)) > 10) return;
    // About three a second each, spread over time by who it is.
    float slot = G.realTime * 3.0f + (who % 97) * 0.173f;
    static std::unordered_map<uint32_t, int> last;
    int n = (int)slot;
    if (last[who] == n) return;
    last[who] = n;
    s_splashes.push_back({feet, 0, true});
}

void drawRain(const World& w, Vec2 cam) {
    float rain = s_cur.rain;
    if (rain < 0.02f) return;
    float W = (float)R::viewW(), H = (float)R::viewH();
    Vec2 camF(std::floor(cam.x), std::floor(cam.y));
    float t = G.realTime;

    // Inside a building (its roof faded away) the rain stops at the walls. Over a
    // roof that is still drawn it falls as normal.
    struct Dry { float x0, y0, x1, y1, keep; };
    std::vector<Dry> dry;
    for (const Building& b : w.buildings) {
        float bx0 = b.x0 * (float)TILE, by0 = b.y0 * (float)TILE;
        float bx1 = bx0 + b.w * TILE, by1 = by0 + b.h * TILE;
        if (bx1 < camF.x - 90 || by1 < camF.y - 90 || bx0 > camF.x + W + 90 || by0 > camF.y + H + 90) continue;
        dry.push_back({bx0, by0, bx1, by1, 1.0f - b.reveal});
    }
    auto shelter = [&](Vec2 p, bool anyRoof) {
        float keep = 1.0f;
        for (const Dry& d : dry)
            if (p.x >= d.x0 && p.y >= d.y0 && p.x < d.x1 && p.y < d.y1) keep = anyRoof ? 0.0f : std::min(keep, d.keep);
        return keep;
    };

    const float padX = 80, padY = 80;
    float boxW = W + padX * 2, boxH = H + padY * 2;
    Vec2 dir = normalize(Vec2(0.22f + rain * 0.15f, 1.0f));
    static const char* const DROPS[4] = {"rain-drop_2_long_blue", "rain-drop_2_long_white", "rain-drop_1_blue", "rain-drop_1_white"};
    static const Assets::Sprite* dropArt[4] = {};
    static bool dropsLooked = false;
    if (!dropsLooked) {
        dropsLooked = true;
        for (int k = 0; k < 4; k++) dropArt[k] = Assets::find(std::string("objects/nature/flowers_mashrooms_other-nature-stuff/") + DROPS[k]);
    }
    int n = (int)(rain * 420);
    for (int i = 0; i < n; i++) {
        float speed = 240.0f + 120.0f * hashf(i, 3);
        float sx = hashf(i, 1) * boxW + dir.x * speed * t;
        float sy = hashf(i, 2) * boxH + dir.y * speed * t;
        // Anchored to the world, wrapped around the view: the rain does not slide
        // with the camera.
        float x = std::fmod(sx - camF.x, boxW); if (x < 0) x += boxW;
        float y = std::fmod(sy - camF.y, boxH); if (y < 0) y += boxH;
        Vec2 p(camF.x + x - padX, camF.y + y - padY);
        float keep = shelter(p, false);
        if (keep <= 0.02f) continue;
        // The pack's drops: mostly the long streak, some short, blue and white.
        float a = (0.35f + 0.3f * rain) * keep;
        const Assets::Sprite* drop = dropArt[(hashf(i, 5) < 0.7f ? 0 : 2) + (hashf(i, 6) < 0.6f ? 0 : 1)];
        if (drop) {
            const Assets::Frame& f = drop->frame(0);
            R::frame(f, std::floor(p.x), std::floor(p.y - f.h), (float)f.w, (float)f.h, Color(1, 1, 1, a), false, -std::atan2(dir.x, dir.y));
        } else {
            float len = 4.0f + 4.0f * hashf(i, 4) + rain * 2.0f;
            R::line(p - dir * len, p, 1, pal(P_BLUE, a * 0.8f));
        }
    }

    // Where drops hit open ground they splash: the pack's own splash animations.
    const Assets::Sprite* small = water("animations/rain-drop-splash_1_small");
    const Assets::Sprite* big = water("animations/rain-drop-splash_2_big");
    // Splashes belong to the ground, not the screen: the world is cut into cells and
    // each cell lands its own drops at spots rolled from the cell and the drop's cycle,
    // so they stay put as the camera moves.
    const int CELL = 32;
    float chance = std::min(1.0f, rain * 0.36f);
    int cx0 = (int)std::floor(camF.x / CELL) - 1, cy0 = (int)std::floor(camF.y / CELL) - 1;
    int cx1 = (int)std::floor((camF.x + W) / CELL) + 1, cy1 = (int)std::floor((camF.y + H) / CELL) + 1;
    for (int cy = cy0; cy <= cy1; cy++)
        for (int cx = cx0; cx <= cx1; cx++) {
            uint64_t cell = (uint64_t)(uint32_t)cx * 73856093ull ^ (uint64_t)(uint32_t)cy * 19349663ull;
            float phase = t * 2.2f + hashf(cell, 9);
            uint64_t cycle = (uint64_t)std::floor(phase);
            if (hashf(cell * 131 + cycle, 13) >= chance) continue;
            float f = phase - (float)cycle;
            Vec2 p(cx * (float)CELL + hashf(cell * 131 + cycle, 11) * CELL, cy * (float)CELL + hashf(cell * 131 + cycle, 12) * CELL);
            if (shelter(p, true) <= 0) continue;
            int tx = World::toTile(p.x), ty = World::toTile(p.y);
            if (!w.inBounds(tx, ty) || w.at(tx, ty).ground == G_WATER || w.at(tx, ty).solid != S_NONE) continue;
            const Assets::Sprite* art = (hashf(cell, cycle) < 0.3f + rain * 0.3f) ? big : small;
            if (!art) {
                R::rect(std::floor(p.x), std::floor(p.y), 1, 1, pal(P_BLUE, (1.0f - f) * 0.5f));
                continue;
            }
            int fr = std::min(art->frameCount() - 1, (int)(f * art->frameCount()));
            R::spriteAt(*art, fr, p, R::Pivot::Bottom, 1, Color(1, 1, 1, 0.75f));
        }
}

}  // namespace Atmo
