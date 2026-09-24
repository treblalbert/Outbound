// The dressing pass (0.12v). World::generate lays out the day's ground, roads, buildings,
// loot and raiders; this runs after all of it, on dice of its own, and makes the place
// look lived in and left: the vegetation grows in stands of one colouring, the ground
// is strewn with tufts, flowers, litter and stones, the cities get their road paint,
// street furniture, rooftops and shop fronts, the yards their clutter, and the grass
// creeps back over the paving. None of it moves anything the day already placed.
#include "art.h"
#include "world.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

using namespace Art;   // ObjectKind, DecoKind, Overlay

float vnoise(float x, float y, uint32_t seed) {
    int xi = (int)std::floor(x), yi = (int)std::floor(y);
    float fx = x - xi, fy = y - yi;
    auto h = [&](int a, int b) { return (hash2(a, b, seed) & 0xFFFF) / 65535.0f; };
    float sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
    return lerpf(lerpf(h(xi, yi), h(xi + 1, yi), sx), lerpf(h(xi, yi + 1), h(xi + 1, yi + 1), sx), sy);
}

float fbm(float x, float y, uint32_t seed) {
    float v = 0, amp = 0.5f, total = 0;
    for (int o = 0; o < 3; o++) {
        v += vnoise(x, y, seed + o * 7919) * amp;
        total += amp;
        x *= 2.07f; y *= 2.07f; amp *= 0.5f;
    }
    return v / total;
}

bool isVeg(int g) { return g == G_GRASS || g == G_SAND || g == G_WASTE; }
bool isFloor(int g) { return (g >= G_FLOOR_WOOD && g <= G_FLOOR_TILE) || g == G_BASE_FLOOR; }

// Garbage_TileSet: which frames draw which part of a heap.
const int GB_TL[2] = {0, 5}, GB_TR[2] = {7, 4}, GB_BL[2] = {24, 21}, GB_BR[2] = {23, 28};
const int GB_TOP[4] = {1, 2, 3, 6}, GB_LEFT[3] = {8, 13, 16}, GB_RIGHT[3] = {12, 15, 20}, GB_BOTTOM[4] = {22, 25, 26, 27};
const int GB_MID[10] = {9, 11, 14, 9, 11, 14, 10, 17, 18, 19};

struct Dresser {
    World& W;
    Rng rng;
    uint32_t salt;
    int ow, oh;
    std::vector<uint8_t> busy;     // must stay clear: raiders' spots, doorways, stairs, loot

    Dresser(World& w, uint64_t seed)
        : W(w), rng(mix64(seed ^ 0xD4E55ull)), salt((uint32_t)mix64(seed ^ 0x5A17ull)), ow(w.outW), oh(w.outH) {}

    bool in(int x, int y) const { return x >= 3 && y >= 3 && x < ow - 3 && y < oh - 3; }
    // The bunker compound and the mechanic's yard keep their own look.
    bool compound(int x, int y) const {
        if (std::abs(x - W.homeTx) <= 11 && y - W.homeTy >= -11 && y - W.homeTy <= 10) return true;
        if (ow > 240 && x >= W.homeTx + 9 && x <= W.homeTx + 25 && y >= W.homeTy - 9 && y <= W.homeTy + 10) return true;
        return false;
    }
    bool isBusy(int x, int y) const { return !W.inBounds(x, y) || busy[(size_t)y * W.w + x]; }
    void markBusy(int x, int y, int r) {
        for (int yy = y - r; yy <= y + r; yy++)
            for (int xx = x - r; xx <= x + r; xx++)
                if (W.inBounds(xx, yy)) busy[(size_t)yy * W.w + xx] = 1;
    }
    float chanceAt(int x, int y, uint32_t k) const { return (hash2(x, y, salt ^ k) & 0xFFFF) / 65535.0f; }

    // Which overgrown art (1 green, 2 dark, 3 bleak) suits a tile's colouring.
    int grownFor(int x, int y) const {
        const Tile& t = W.at(x, y);
        switch (t.tone) {
        case TONE_GREEN: return 1;
        case TONE_DARK: return 2;
        case TONE_AUTO: return t.ground == G_SAND ? 2 : t.ground == G_WASTE ? 3 : 1;
        default: return 3;
        }
    }

    void buildBusy() {
        busy.assign(W.tiles.size(), 0);
        for (const EnemySpawn& s : W.spawns) markBusy(World::toTile(s.pos.x), World::toTile(s.pos.y), 1);
        for (const Container& c : W.containers)
            if (!c.removed) markBusy(World::toTile(c.pos.x), World::toTile(c.pos.y), 1);
        for (int y = 0; y < oh; y++)
            for (int x = 0; x < ow; x++) {
                int s = W.at(x, y).solid;
                if (s == S_DOOR || s == S_DOOR_OPEN || s == S_STAIRS) markBusy(x, y, 2);
                if (s == S_CRYPT_PROP) markBusy(x, y, 2);
            }
        for (const Dungeon& d : W.dungeons) markBusy(World::toTile(d.door.x), World::toTile(d.door.y), 3);
        for (const PatrolRoute& r : W.patrols)
            for (Vec2 p : r.points) markBusy(World::toTile(p.x), World::toTile(p.y), 2);
    }

    // ---- placing things ----------------------------------------------------------
    // A standing object with its bottom middle at `base`. Its tiles are reserved (as a
    // car's are) and it blocks with its pixels. `ground` limits what it may stand on.
    enum { ON_ANY, ON_PAVED, ON_OPEN };
    bool object(int kind, int pick, int grown, Vec2 base, bool flip = false, int on = ON_ANY) {
        Art::Piece art = Art::object(kind, pick, grown);
        if (!art.valid()) return false;
        const Assets::Sprite& s = *art.sprite;
        base = Vec2(std::floor(base.x), std::floor(base.y));
        int x0 = World::toTile(base.x - s.w * 0.5f), x1 = World::toTile(base.x + s.w * 0.5f - 0.01f);
        int y0 = World::toTile(base.y - s.h), y1 = World::toTile(base.y - 0.01f);
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                if (!in(x, y) || compound(x, y) || isBusy(x, y)) return false;
                const Tile& t = W.at(x, y);
                if (t.solid != S_NONE || t.container >= 0 || t.ground == G_WATER || t.ground == G_BRIDGE || isFloor(t.ground) ||
                    t.ground == G_VOID || t.ground == G_CRYPT)
                    return false;
                bool paved = t.ground == G_PAVEMENT || t.ground == G_ROAD || t.ground == G_RUBBLE;
                if (on == ON_PAVED && !paved) return false;
                if (on == ON_OPEN && t.ground == G_ROAD) return false;
            }
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                Tile& t = W.at(x, y);
                t.solid = S_CAR;
                t.hp = -1;
                t.worldDeco = 0;
            }
        WorldProp p;
        p.kind = PROP_OBJECT;
        p.variant = (uint8_t)kind;
        p.frame = Art::objectFrame(pick, grown);
        p.flipX = flip;
        p.pos = base;
        W.props.push_back(p);
        return true;
    }
    // The same, standing on a tile (its foot on the tile's bottom edge, a little up).
    bool objectAt(int kind, int pick, int grown, int tx, int ty, bool flip = false, int on = ON_ANY) {
        return object(kind, pick, grown, Vec2(tx * (float)TILE + TILE * 0.5f, (ty + 1) * (float)TILE - 2), flip, on);
    }
    // Gives a parked car another look (overgrown with `tone`'s grass). Its art can come
    // out a different size, so its reserved tiles are redone; it keeps the old look
    // when the new one would not fit.
    void retoneCar(WorldProp& p, int tone) {
        auto rect = [&](const WorldProp& q, int& x0, int& y0, int& x1, int& y1) {
            Art::Piece a = Art::propArt(q);
            if (!a.valid()) return false;
            const Assets::Sprite& s = *a.sprite;
            x0 = World::toTile(q.pos.x - s.w * 0.5f); x1 = World::toTile(q.pos.x + s.w * 0.5f - 0.01f);
            y0 = World::toTile(q.pos.y - s.h); y1 = World::toTile(q.pos.y - 0.01f);
            return true;
        };
        WorldProp q = p;
        q.frame = (uint8_t)tone;
        int ox0, oy0, ox1, oy1, nx0, ny0, nx1, ny1;
        if (!rect(p, ox0, oy0, ox1, oy1) || !rect(q, nx0, ny0, nx1, ny1)) return;
        auto inOld = [&](int x, int y) { return x >= ox0 && x <= ox1 && y >= oy0 && y <= oy1; };
        for (int y = ny0; y <= ny1; y++)
            for (int x = nx0; x <= nx1; x++) {
                if (inOld(x, y)) continue;
                if (!in(x, y) || compound(x, y) || isBusy(x, y) || W.at(x, y).solid != S_NONE) return;
            }
        for (int y = oy0; y <= oy1; y++)
            for (int x = ox0; x <= ox1; x++)
                if (W.inBounds(x, y) && W.at(x, y).solid == S_CAR) { W.at(x, y).solid = S_NONE; W.at(x, y).hp = 0; }
        for (int y = ny0; y <= ny1; y++)
            for (int x = nx0; x <= nx1; x++) { Tile& t = W.at(x, y); t.solid = S_CAR; t.hp = -1; t.worldDeco = 0; }
        p.frame = q.frame;
    }
    int pickOf(int kind) { int n = std::max(1, Art::objectChoices(kind)); return (int)(rng.next() % (uint32_t)n); }

    // Flat detail on a free tile.
    bool deco(int x, int y, int kind, int which) {
        if (!in(x, y) || compound(x, y)) return false;
        Tile& t = W.at(x, y);
        if (t.solid != S_NONE || t.worldDeco || t.deco || t.furn || t.ground == G_WATER || isFloor(t.ground)) return false;
        t.worldDeco = Art::decoCode(kind, which);
        return true;
    }

    // A heap of garbage, roughly w x h tiles round (cx, cy), laid flat over the ground.
    void heap(int cx, int cy, int w, int h) {
        std::vector<uint8_t> m((size_t)w * h, 0);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                float ex = (x + 0.5f - w * 0.5f) / (w * 0.5f), ey = (y + 0.5f - h * 0.5f) / (h * 0.5f);
                int tx = cx - w / 2 + x, ty = cy - h / 2 + y;
                if (ex * ex + ey * ey > 1.15f + rng.range(-0.25f, 0.2f) || !in(tx, ty) || compound(tx, ty)) continue;
                const Tile& t = W.at(tx, ty);
                if (t.ground == G_WATER || isFloor(t.ground) || t.overlay || (t.flags & TF_KERB)) continue;
                if (t.solid != S_NONE && t.solid != S_CAR && t.solid != S_CRATE) continue;
                m[(size_t)y * w + x] = 1;
            }
        auto at = [&](int x, int y) { return x >= 0 && y >= 0 && x < w && y < h && m[(size_t)y * w + x]; };
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                if (!at(x, y)) continue;
                bool n = at(x, y - 1), s = at(x, y + 1), wv = at(x - 1, y), e = at(x + 1, y);
                int k = rng.irange(0, 9), f;
                if (!n && !wv) f = GB_TL[k & 1];
                else if (!n && !e) f = GB_TR[k & 1];
                else if (!s && !wv) f = GB_BL[k & 1];
                else if (!s && !e) f = GB_BR[k & 1];
                else if (!n) f = GB_TOP[k % 4];
                else if (!wv) f = GB_LEFT[k % 3];
                else if (!e) f = GB_RIGHT[k % 3];
                else if (!s) f = GB_BOTTOM[k % 4];
                else f = GB_MID[k];
                Tile& t = W.at(cx - w / 2 + x, cy - h / 2 + y);
                t.overlay = (uint8_t)(Art::OV_GARBAGE + f);
                t.worldDeco = 0;
            }
    }

    // ---- 1. colour: stands of one tree colouring --------------------------------
    void tones() {
        for (int y = 0; y < oh; y++)
            for (int x = 0; x < ow; x++) {
                Tile& t = W.at(x, y);
                float stand = fbm(x * 0.035f, y * 0.035f, salt ^ 0x51A1u);
                float autumn = fbm(x * 0.045f, y * 0.045f, salt ^ 0xA07Eu);
                float hue = vnoise(x * 0.02f, y * 0.02f, salt ^ 0x40E5u);
                uint8_t tone = t.ground == G_SAND ? TONE_DARK : t.ground == G_WASTE ? TONE_BLEAK : TONE_GREEN;
                if (tone == TONE_GREEN && stand > 0.62f) tone = TONE_DARK;          // the darker woods
                if (tone == TONE_DARK && stand < 0.34f) tone = TONE_GREEN;
                // A stand is mostly one colouring, never all of it: a quarter of its trees
                // take the neighbouring green, so a wood keeps some depth.
                uint32_t hh = hash2(x, y, salt ^ 0x3171u);
                if ((hh & 3) == 0 && (tone == TONE_GREEN || tone == TONE_DARK)) tone = tone == TONE_GREEN ? TONE_DARK : TONE_GREEN;
                else if ((hh >> 8) % 14 == 0 && tone == TONE_GREEN) tone = TONE_BLEAK;   // a dying tree here and there
                // Autumn: whole copses turned orange, yellow or red together.
                if (t.ground != G_WASTE && autumn > 0.66f && ((hh >> 4) % 5) != 0)
                    tone = hue < 0.4f ? TONE_ORANGE : hue < 0.7f ? TONE_YELLOW : TONE_RED;
                if (t.ground == G_WASTE && autumn > 0.76f) tone = TONE_ORANGE;
                t.tone = tone;
            }
    }

    // ---- 2. the countryside's detail ----------------------------------------------
    int treesNear(int x, int y) {
        int n = 0;
        for (int yy = y - 1; yy <= y + 1; yy++)
            for (int xx = x - 1; xx <= x + 1; xx++)
                if (W.inBounds(xx, yy) && W.at(xx, yy).solid == S_TREE) n++;
        return n;
    }

    void nature() {
        for (int y = 3; y < oh - 3; y++)
            for (int x = 3; x < ow - 3; x++) {
                if (compound(x, y) || W.cityAt(x, y) >= 0) continue;
                Tile& t = W.at(x, y);
                if (t.solid != S_NONE || t.overlay || isFloor(t.ground)) continue;
                int g = t.ground;
                if (!isVeg(g) && g != G_DIRT) continue;
                int trees = treesNear(x, y);
                float clump = fbm(x * 0.14f, y * 0.14f, salt ^ 0xC1u);
                float meadow = fbm(x * 0.07f, y * 0.07f, salt ^ 0xF10u);
                float r = rng.f();
                // Big things first: fallen trunks, stumps and boulders in the woods, a
                // boulder or two in the open.
                if (trees >= 2 && r < 0.006f) {
                    int kind = r < 0.0025f ? OB_TRUNK : r < 0.0045f ? OB_STUMP : OB_BOULDER;
                    if (objectAt(kind, pickOf(kind), grownFor(x, y), x, y, rng.chance(0.5f), ON_OPEN)) continue;
                }
                if (trees == 0 && g != G_DIRT && r > 0.9985f) {
                    if (objectAt(OB_BOULDER, pickOf(OB_BOULDER), grownFor(x, y), x, y, false, ON_OPEN)) continue;
                }
                if (t.worldDeco) continue;
                float p = rng.f();
                if (g == G_DIRT) {
                    if (p < 0.03f) deco(x, y, Art::DK_PEBBLE, rng.irange(0, 31));
                    else if (p < 0.05f) deco(x, y, Art::DK_FOREST, rng.irange(0, 31));
                    else if (p < 0.08f && trees == 0) deco(x, y, Art::DK_TUFT, rng.irange(0, 31));
                    continue;
                }
                if (g == G_WASTE) {
                    if (p < 0.05f + 0.1f * std::max(0.0f, clump - 0.5f)) deco(x, y, Art::DK_TUFT, rng.irange(0, 31));
                    else if (p < 0.08f) deco(x, y, Art::DK_PEBBLE, rng.irange(0, 31));
                    else if (p < 0.09f) deco(x, y, Art::DK_JUNK, rng.irange(0, 31));
                    else if (p < 0.0915f) objectAt(rng.chance(0.5f) ? OB_TIRES : OB_BARREL, rng.irange(0, 7), 3, x, y, false, ON_OPEN);
                    continue;
                }
                // Grass: tufts in clumps, flowers in meadows, litter under trees.
                float tuft = 0.03f + 0.3f * std::max(0.0f, clump - 0.52f);
                if (trees > 0 && p < 0.07f) deco(x, y, Art::DK_FOREST, rng.irange(0, 31));
                else if (meadow > 0.63f && p < 0.16f) deco(x, y, Art::DK_FLOWER, (int)(meadow * 97) + rng.irange(0, 3));
                else if (p < tuft) deco(x, y, Art::DK_TUFT, rng.irange(0, 31));
                else if (p > 0.996f) deco(x, y, Art::DK_PEBBLE, rng.irange(0, 31));
            }
    }

    // ---- 3. roads out in the country ------------------------------------------------
    void countryRoads() {
        for (int y = 3; y < oh - 3; y++)
            for (int x = 3; x < ow - 3; x++) {
                if (compound(x, y) || W.cityAt(x, y) >= 0) continue;
                Tile& t = W.at(x, y);
                if (t.ground != G_ROAD || t.solid != S_NONE || t.worldDeco || t.overlay) continue;
                bool edge = W.at(x - 1, y).ground != G_ROAD || W.at(x + 1, y).ground != G_ROAD ||
                            W.at(x, y - 1).ground != G_ROAD || W.at(x, y + 1).ground != G_ROAD;
                float p = rng.f();
                if (edge && p < 0.06f) deco(x, y, Art::DK_TUFT, rng.irange(0, 31));   // weeds in the cracks
                else if (p < 0.004f) deco(x, y, Art::DK_JUNK, rng.irange(0, 31));
            }
        // Cars left out in the country are overgrown with whatever grows there, and
        // some have a cone or a tyre left by them.
        size_t n = W.props.size();
        for (size_t i = 0; i < n; i++) {
            WorldProp& p = W.props[i];
            if (p.kind != PROP_CAR) continue;
            int tx = World::toTile(p.pos.x), ty = World::toTile(p.pos.y - 1);
            if (!W.inBounds(tx, ty) || W.cityAt(tx, ty) >= 0) continue;
            if (rng.chance(0.55f)) retoneCar(p, grownFor(tx, ty));
            if (rng.chance(0.3f)) {
                int side = rng.chance(0.5f) ? -2 : 2;
                objectAt(rng.chance(0.5f) ? OB_CONE : OB_TIRES, rng.irange(0, 7), p.frame ? p.frame : 0, tx + side, ty + rng.irange(-1, 1));
            }
        }
    }

    // ---- 4. the cities --------------------------------------------------------------
    void crosswalks(const CityZone& c) {
        int S = c.street;
        for (int sy : c.streetY)
            for (int sx : c.streetX) {
                // The four arms of the crossing, one tile out from its middle square.
                if (chanceAt(sx, sy, 1) < 0.85f)
                    for (int k = 0; k < S; k++) paint(sx - 1, sy + k, Art::OV_CROSS_EW + 1);
                if (chanceAt(sx, sy, 2) < 0.85f)
                    for (int k = 0; k < S; k++) paint(sx + S, sy + k, Art::OV_CROSS_EW + 1);
                if (chanceAt(sx, sy, 3) < 0.85f)
                    for (int k = 0; k < S; k++) paint(sx + k, sy - 1, Art::OV_CROSS_NS + 1);
                if (chanceAt(sx, sy, 4) < 0.85f)
                    for (int k = 0; k < S; k++) paint(sx + k, sy + S, Art::OV_CROSS_NS + 1);
            }
    }
    void paint(int x, int y, int ov) {
        if (!in(x, y)) return;
        Tile& t = W.at(x, y);
        if (t.ground != G_ROAD || t.overlay) return;
        t.overlay = (uint8_t)ov;
        t.worldDeco = 0;
    }

    // The sidewalk round a block: lamps at the kerb, benches, bins, hydrants and signs.
    void sidewalks(const CityBlock& b, const CityZone& c) {
        float og = c.overgrowth;
        auto grown = [&](int x, int y) { return rng.chance(og * 0.8f) ? grownFor(x, y) : 0; };
        // side 0 top, 1 bottom, 2 left, 3 right. kerb row / inner row of each.
        for (int side = 0; side < 4; side++) {
            bool horiz = side < 2;
            int len = horiz ? b.w : b.h;
            // Lamps down one side of each street only (the top and left of a block), so
            // a street is lit without a double row of poles.
            int lampEvery = rng.irange(9, 12), lampAt = (side == 0 || side == 2) ? rng.irange(2, 5) : 1 << 20;
            int next = rng.irange(3, 6);
            for (int i = 2; i < len - 2; i++) {
                int kx, ky, ix, iy;   // kerb tile and inner sidewalk tile at this step
                if (side == 0) { kx = b.x0 + i; ky = b.y0; ix = kx; iy = b.y0 + 1; }
                else if (side == 1) { kx = b.x0 + i; ky = b.y0 + b.h - 1; ix = kx; iy = ky - 1; }
                else if (side == 2) { kx = b.x0; ky = b.y0 + i; ix = kx + 1; iy = ky; }
                else { kx = b.x0 + b.w - 1; ky = b.y0 + i; ix = kx - 1; iy = ky; }
                if (i == lampAt) {
                    lampAt += lampEvery;
                    if (!isBusy(kx, ky) && W.at(kx, ky).ground == G_PAVEMENT) {
                        int variant = side == 0 ? 1 : side == 1 ? 2 : 0;
                        if (W.placeStreetLight(kx, ky, variant, side == 2)) continue;
                    }
                }
                if (i < next) continue;
                next = i + rng.irange(4, 9);
                float r = rng.f();
                int gx = grown(ix, iy);
                if (r < 0.34f) {
                    // A bench facing the street, a bin beside it.
                    int kind = side == 0 ? OB_BENCH_UP : side == 1 ? OB_BENCH_DOWN : OB_BENCH_SIDE;
                    if (objectAt(kind, 0, gx, ix, iy, side == 3, ON_PAVED) && rng.chance(0.6f)) {
                        int bx = horiz ? ix + 2 : ix, by = horiz ? iy : iy + 2;
                        objectAt(OB_TRASH_CAN, pickOf(OB_TRASH_CAN), 0, bx, by, false, ON_PAVED);
                    }
                } else if (r < 0.5f) objectAt(OB_HYDRANT, pickOf(OB_HYDRANT), 0, kx, ky, false, ON_PAVED);
                else if (r < 0.66f) objectAt(OB_TRASH_CAN, pickOf(OB_TRASH_CAN), 0, ix, iy, false, ON_PAVED);
                else if (r < 0.74f) objectAt(OB_VENDING, pickOf(OB_VENDING), gx, ix, iy, false, ON_PAVED);
                else if (r < 0.8f) objectAt(OB_CART, 0, 0, ix, iy, rng.chance(0.5f), ON_PAVED);
                else if (r < 0.86f) objectAt(OB_CONE, 0, 0, kx, ky, false, ON_PAVED);
                else deco(ix, iy, Art::DK_JUNK, rng.irange(0, 31));
            }
        }
        // Stop signs on some of the block's corners, turned to the street they face.
        struct Corner { int x, y, kind; bool flip; };
        Corner corners[4] = {{b.x0, b.y0, OB_STOP_UP, false}, {b.x0 + b.w - 1, b.y0, OB_STOP_SIDE, false},
                             {b.x0, b.y0 + b.h - 1, OB_STOP_SIDE, true}, {b.x0 + b.w - 1, b.y0 + b.h - 1, OB_STOP_DOWN, false}};
        for (const Corner& k : corners)
            if (rng.chance(0.3f)) objectAt(k.kind, 0, grown(k.x, k.y), k.x, k.y, k.flip, ON_PAVED);
    }

    void block(const CityBlock& b, const CityZone& c) {
        int ix = b.x0 + 2, iy = b.y0 + 2, iw = b.w - 4, ih = b.h - 4;
        float og = c.overgrowth;
        auto freeRandom = [&](int& x, int& y) {
            for (int a = 0; a < 20; a++) {
                x = rng.irange(ix, ix + iw - 1);
                y = rng.irange(iy, iy + ih - 1);
                const Tile& t = W.at(x, y);
                if (t.solid == S_NONE && !isFloor(t.ground)) return true;
            }
            return false;
        };
        switch (b.kind) {
        case BLK_BUILDINGS: {
            int x, y;
            if (b.yard == 0) {
                // Paved yards: dumpsters and pallets out back, a heap of rubbish.
                for (int i = 0, n = rng.irange(1, 3); i < n; i++)
                    if (freeRandom(x, y)) objectAt(rng.chance(0.6f) ? OB_DUMPSTER : OB_PALLET, rng.irange(0, 7), 0, x, y);
                if (rng.chance(0.55f) && freeRandom(x, y)) heap(x, y, rng.irange(2, 4), rng.irange(2, 3));
            } else {
                // Lawns get a tree or two and a hedge, wild yards more of everything.
                int n = b.yard == 1 ? rng.irange(1, 3) : rng.irange(3, 6);
                for (int i = 0; i < n; i++)
                    if (freeRandom(x, y) && W.at(x, y).ground == G_GRASS && !isBusy(x, y)) {
                        Tile& t = W.at(x, y);
                        t.solid = rng.chance(0.6f) ? S_TREE : S_BUSH;
                        t.hp = (int16_t)solidInfo(t.solid).hp;
                    }
                for (int i = 0; i < iw * ih / 6; i++)
                    if (freeRandom(x, y) && W.at(x, y).ground == G_GRASS)
                        deco(x, y, b.yard == 1 && rng.chance(0.4f) ? Art::DK_FLOWER : Art::DK_TUFT, rng.irange(0, 31));
                if (b.yard == 2 && rng.chance(0.5f) && freeRandom(x, y)) objectAt(OB_TIRES, pickOf(OB_TIRES), grownFor(x, y), x, y);
            }
            break;
        }
        case BLK_PARKING: {
            // Bay markings over the whole lot, a cone or two.
            for (int y = iy + 1; y + 3 <= iy + ih - 1; y += 3)
                for (int x = ix + 1; x + 4 <= ix + iw - 1; x += 4)
                    for (int k = 0; k < 12; k++) {
                        Tile& t = W.at(x + k % 4, y + k / 4);
                        if (t.ground == G_ROAD && !t.overlay) t.overlay = (uint8_t)(Art::OV_PARKING + k);
                    }
            int x, y;
            for (int i = 0, n = rng.irange(1, 4); i < n; i++)
                if (freeRandom(x, y)) objectAt(rng.chance(0.7f) ? OB_CONE : OB_CART, 0, 0, x, y, rng.chance(0.5f));
            break;
        }
        case BLK_PARK: {
            // Benches and lamps along the paths, flowers in the beds.
            for (int y = iy; y < iy + ih; y++)
                for (int x = ix; x < ix + iw; x++) {
                    const Tile& t = W.at(x, y);
                    if (t.ground != G_PAVEMENT) continue;
                    bool pathEW = W.at(x, y - 1).ground == G_PAVEMENT || W.at(x, y + 1).ground == G_PAVEMENT;
                    bool lawnN = W.at(x, y - 1).ground == G_GRASS, lawnS = W.at(x, y + 1).ground == G_GRASS;
                    bool lawnW = W.at(x - 1, y).ground == G_GRASS, lawnE = W.at(x + 1, y).ground == G_GRASS;
                    float r = rng.f();
                    if (r < 0.07f && lawnN && !lawnS) objectAt(OB_BENCH_DOWN, 0, rng.chance(og) ? grownFor(x, y) : 0, x, y, false, ON_PAVED);
                    else if (r < 0.07f && lawnW && !lawnE && !pathEW) objectAt(OB_BENCH_SIDE, 0, rng.chance(og) ? grownFor(x, y) : 0, x, y, false, ON_PAVED);
                    else if (r < 0.1f && (lawnN || lawnW)) W.placeStreetLight(x, y, 1, false);
                    else if (r < 0.115f) objectAt(OB_TRASH_CAN, pickOf(OB_TRASH_CAN), 0, x, y, false, ON_PAVED);
                }
            for (int i = 0; i < iw * ih / 5; i++) {
                int x = rng.irange(ix, ix + iw - 1), y = rng.irange(iy, iy + ih - 1);
                if (W.at(x, y).ground == G_GRASS) deco(x, y, b.yard == 1 && rng.chance(0.5f) ? Art::DK_FLOWER : Art::DK_TUFT, rng.irange(0, 31));
            }
            break;
        }
        case BLK_RUIN: {
            int x, y;
            for (int i = 0, n = rng.irange(2, 4); i < n; i++)
                if (freeRandom(x, y)) heap(x, y, rng.irange(2, 5), rng.irange(2, 4));
            for (int i = 0, n = rng.irange(3, 6); i < n; i++)
                if (freeRandom(x, y)) {
                    static const int JUNK[] = {OB_BARREL, OB_TIRES, OB_FRIDGE, OB_WASHER, OB_JUNK, OB_PALLET, OB_CART};
                    int kind = JUNK[rng.irange(0, 6)];
                    objectAt(kind, pickOf(kind), rng.chance(og) ? grownFor(x, y) : 0, x, y, rng.chance(0.5f));
                }
            for (int i = 0; i < iw * ih / 7; i++)
                if (freeRandom(x, y)) deco(x, y, rng.chance(0.6f) ? Art::DK_JUNK : Art::DK_TUFT, rng.irange(0, 31));
            break;
        }
        case BLK_DEPOT: {
            // Rows of shipping containers with room to walk between, pallets and drums.
            bool vertical = rng.chance(0.5f);
            int stepX = vertical ? 3 : 4, stepY = vertical ? 4 : 3;
            for (int y = iy + 3; y + 2 < iy + ih - 1; y += stepY)
                for (int x = ix + 2; x + 2 < ix + iw - 1; x += stepX) {
                    if (rng.chance(0.3f)) continue;
                    int kind = vertical ? OB_CONTAINER_V : OB_CONTAINER_H;
                    object(kind, pickOf(kind), rng.chance(0.25f + og * 0.5f) ? grownFor(x, y) : 0,
                           Vec2((x + 1) * (float)TILE, (y + 1) * (float)TILE - 1));
                }
            int x, y;
            for (int i = 0, n = rng.irange(3, 6); i < n; i++)
                if (freeRandom(x, y)) objectAt(rng.chance(0.5f) ? OB_PALLET : OB_BARREL, rng.irange(0, 7), 0, x, y);
            break;
        }
        }
    }

    void cities() {
        for (size_t ci = 0; ci < W.cities.size(); ci++) {
            const CityZone& c = W.cities[ci];
            float og = c.overgrowth;
            crosswalks(c);
            // The more overgrown cities have lost some of their paving to the grass.
            if (og > 0.45f)
                for (int y = c.y0; y < c.y0 + c.h; y++)
                    for (int x = c.x0; x < c.x0 + c.w; x++) {
                        Tile& t = W.at(x, y);
                        if (t.ground != G_PAVEMENT || t.solid != S_NONE || isBusy(x, y)) continue;
                        float n = fbm(x * 0.16f, y * 0.16f, salt ^ 0x0E6Au);
                        if (n > 0.86f - (og - 0.45f) * 0.35f) t.ground = G_GRASS;
                    }
            for (const CityBlock& b : W.blocks) {
                if (b.city != (int)ci) continue;
                sidewalks(b, c);
                block(b, c);
            }
            // Street litter, manholes, weeds through the cracks.
            for (int y = c.y0; y < c.y0 + c.h; y++)
                for (int x = c.x0; x < c.x0 + c.w; x++) {
                    const Tile& t = W.at(x, y);
                    if (t.solid != S_NONE || t.worldDeco || t.overlay) continue;
                    float p = rng.f();
                    if (t.ground == G_ROAD) {
                        if (p < 0.006f) deco(x, y, Art::DK_POSTER, 2);                    // a manhole
                        else if (p < 0.016f) deco(x, y, Art::DK_JUNK, rng.irange(0, 31));
                        else if (p < 0.016f + 0.03f * og) deco(x, y, Art::DK_TUFT, rng.irange(0, 31));
                    } else if (t.ground == G_PAVEMENT) {
                        if (p < 0.012f) deco(x, y, Art::DK_JUNK, rng.irange(0, 31));
                        else if (p < 0.016f) deco(x, y, Art::DK_POSTER, rng.irange(0, 1));
                        else if (p < 0.02f + 0.08f * og) deco(x, y, Art::DK_TUFT, rng.irange(0, 31));
                    }
                }
            // Wrecks on the streets, as cars out in the country: some gone green.
            for (WorldProp& p : W.props)
                if (p.kind == PROP_CAR && c.contains(World::toTile(p.pos.x), World::toTile(p.pos.y - 1)) && rng.chance(og))
                    retoneCar(p, grownFor(World::toTile(p.pos.x), World::toTile(p.pos.y - 1)));
        }
    }

    // ---- 5. the rest of the buildings: their yards, fronts and roofs ---------------
    void around(const Building& b) {
        int x, y;
        auto near = [&](int r, int& ox, int& oy) {
            for (int a = 0; a < 16; a++) {
                ox = rng.irange(b.x0 - r, b.x0 + b.w - 1 + r);
                oy = rng.irange(b.y0 - r, b.y0 + b.h - 1 + r);
                if (ox >= b.x0 - 1 && ox <= b.x0 + b.w && oy >= b.y0 - 1 && oy <= b.y0 + b.h) continue;   // off its walls
                if (W.inBounds(ox, oy) && W.at(ox, oy).solid == S_NONE) return true;
            }
            return false;
        };
        switch (b.kind) {
        case BK_TOWN:
            for (int i = 0, n = rng.irange(1, 3); i < n; i++)
                if (near(2, x, y)) {
                    static const int K[] = {OB_TRASH_CAN, OB_DUMPSTER, OB_TIRES, OB_BARREL, OB_BENCH_DOWN, OB_CART};
                    int k = K[rng.irange(0, 5)];
                    objectAt(k, pickOf(k), rng.chance(0.4f) ? grownFor(x, y) : 0, x, y, rng.chance(0.5f), ON_OPEN);
                }
            if (rng.chance(0.4f) && near(3, x, y)) heap(x, y, 2, 2);
            break;
        case BK_FARM:
            // A tractor in the yard, tyres and drums, and the field planted in rows.
            if (near(3, x, y)) objectAt(OB_TRACTOR, pickOf(OB_TRACTOR), rng.chance(0.6f) ? grownFor(x, y) : 0, x, y, rng.chance(0.5f), ON_OPEN);
            for (int i = 0, n = rng.irange(2, 4); i < n; i++)
                if (near(4, x, y)) objectAt(rng.chance(0.5f) ? OB_TIRES : OB_BARREL, rng.irange(0, 7), grownFor(x, y), x, y, false, ON_OPEN);
            for (int yy = b.y0 - 8; yy <= b.y0 + b.h + 8; yy += 2)
                for (int xx = b.x0 - 10; xx <= b.x0 + b.w + 10; xx++)
                    if (in(xx, yy) && W.at(xx, yy).ground == G_DIRT && rng.chance(0.55f)) {
                        Tile& t = W.at(xx, yy);
                        if (t.solid == S_NONE && !t.overlay) { t.worldDeco = Art::decoCode(Art::DK_TUFT, (xx * 3 + yy) & 31); t.tone = TONE_GREEN; }
                    }
            break;
        case BK_WAREHOUSE: {
            // A paved apron all round, shipping containers and pallets on it.
            for (int yy = b.y0 - 3; yy <= b.y0 + b.h + 2; yy++)
                for (int xx = b.x0 - 3; xx <= b.x0 + b.w + 2; xx++) {
                    if (!in(xx, yy) || compound(xx, yy)) continue;
                    Tile& t = W.at(xx, yy);
                    if (isVeg(t.ground) || t.ground == G_DIRT) { t.ground = G_PAVEMENT; t.worldDeco = 0; }
                }
            for (int i = 0, n = rng.irange(2, 4); i < n; i++)
                if (near(5, x, y)) {
                    int kind = rng.chance(0.5f) ? OB_CONTAINER_V : OB_CONTAINER_H;
                    objectAt(kind, pickOf(kind), rng.chance(0.5f) ? grownFor(x, y) : 0, x, y);
                }
            for (int i = 0, n = rng.irange(2, 5); i < n; i++)
                if (near(3, x, y)) objectAt(rng.chance(0.5f) ? OB_PALLET : OB_BARREL, rng.irange(0, 7), 0, x, y);
            if (near(4, x, y)) heap(x, y, rng.irange(2, 4), 2);
            break;
        }
        case BK_MILITARY:
            for (int i = 0, n = rng.irange(1, 2); i < n; i++)
                if (near(3, x, y)) objectAt(OB_CONTAINER_V, 2, 0, x, y);   // army green
            for (int i = 0, n = rng.irange(1, 3); i < n; i++)
                if (near(2, x, y)) objectAt(rng.chance(0.6f) ? OB_BARREL : OB_TIRES, rng.irange(0, 7), 0, x, y);
            break;
        case BK_CABIN:
            if (rng.chance(0.5f) && near(2, x, y)) objectAt(rng.chance(0.5f) ? OB_TRUNK : OB_STUMP, rng.irange(0, 3), grownFor(x, y), x, y, rng.chance(0.5f), ON_OPEN);
            if (rng.chance(0.4f) && near(2, x, y)) objectAt(OB_BARREL, rng.irange(0, 7), 0, x, y, false, ON_OPEN);
            break;
        default: break;
        }
    }

    // Windows, posters and graffiti along a building's front wall, an awning over a
    // city shop's door.
    void front(size_t bi) {
        const Building& b = W.buildings[bi];
        int y = b.y0 + b.h - 1;
        bool city = b.kind == BK_CITY, shop = city && rng.chance(0.45f);
        int wallSolid = W.at(b.x0, y).solid;
        bool brick = wallSolid == S_WALL_BRICK;
        int pattern = rng.irange(0, 2);          // every tile, every other, pairs
        if (b.kind == BK_WAREHOUSE || b.kind == BK_MILITARY) pattern = 3;   // sheds: a window here and there
        int windowPick = rng.irange(0, 7);        // one style per building
        for (int x = b.x0 + 1; x < b.x0 + b.w - 1; x++) {
            const Tile& t = W.at(x, y);
            if (t.solid != S_WALL_CONCRETE && t.solid != S_WALL_WOOD && t.solid != S_WALL_BRICK) continue;
            bool byDoor = b.isDoor(x - 1, y) || b.isDoor(x + 1, y);
            int i = x - b.x0;
            bool slot = pattern == 0 ? true : pattern == 1 ? (i % 2 == 1) : pattern == 2 ? (i % 3 != 0) : (i % 4 == 2);
            int kind = OB_NONE, pick = 0;
            float r = rng.f();
            if (shop && byDoor) { kind = OB_SHOPFRONT; pick = (b.sheet << 3) | (4 + (i & 3)); }
            else if (slot) {
                kind = brick || r < 0.15f ? (rng.chance(0.5f) ? OB_WINDOW_BROKEN : OB_WINDOW_BOARDED) : OB_WINDOW;
                pick = kind == OB_WINDOW ? windowPick : rng.irange(0, 7);
            } else if (r < 0.2f) { kind = OB_POSTER; pick = rng.irange(0, 1); }
            else if (r < 0.32f && city) { kind = OB_GRAFFITI; pick = (b.sheet << 3) | rng.irange(0, 5); }
            if (kind != OB_NONE) wallDeco(x, y, kind, pick, rng.chance(0.5f) && kind == OB_POSTER);
            if (rng.chance(city ? 0.12f : 0.2f)) wallDeco(x, y, OB_IVY, rng.irange(0, 12), false);
        }
        // A downspout down a front corner or two, rusty on the older places. The ones
        // that spill out onto the ground run with rainwater when it rains.
        if (b.kind != BK_MILITARY && b.w >= 4)
            for (int side = 0; side < 2; side++) {
                if (!rng.chance(city ? 0.6f : 0.4f)) continue;
                int x = side ? b.x0 + b.w - 1 : b.x0;
                int s = W.at(x, y).solid;
                if (s != S_WALL_CONCRETE && s != S_WALL_WOOD && s != S_WALL_BRICK) continue;
                int pick = (brick || rng.chance(0.3f) ? 2 : 0) + (rng.chance(0.65f) ? 1 : 0);
                WorldProp p;
                p.kind = PROP_WALLDECO;
                p.variant = OB_DOWNSPOUT;
                p.frame = Art::objectFrame(pick, 0);
                p.pos = Vec2(x * (float)TILE + (side ? 12.0f : 4.0f), (y + 1) * (float)TILE);
                W.props.push_back(p);
            }
        if (shop)
            for (int d = 0; d < b.doorCount; d++) {
                if (b.doorY[d] != y || b.isDoor(b.doorX[d] - 1, y)) continue;   // the left leaf of a front door
                WorldProp p;
                p.kind = PROP_OBJECT;
                p.variant = OB_AWNING;
                p.frame = Art::objectFrame((rng.irange(0, 1) << 3) | 1, 0);   // three tiles wide
                p.pos = Vec2((b.doorX[d] + 1) * (float)TILE, y * (float)TILE + 5);
                addRoofProp(bi, p);
            }
    }
    void wallDeco(int x, int y, int kind, int pick, bool flip) {
        WorldProp p;
        p.kind = PROP_WALLDECO;
        p.variant = (uint8_t)kind;
        p.frame = Art::objectFrame(pick, 0);
        p.flipX = flip;
        p.pos = Vec2(x * (float)TILE + TILE * 0.5f, (y + 1) * (float)TILE);
        W.props.push_back(p);
    }

    // Rooftop gear. Roof props are kept in one run per building, in building order.
    std::vector<std::vector<WorldProp>> roofs;
    void addRoofProp(size_t bi, const WorldProp& p) { roofs[bi].push_back(p); }
    void roof(size_t bi) {
        const Building& b = W.buildings[bi];
        if (!b.flatRoof || b.w < 5 || b.h < 5) return;
        std::vector<std::pair<int, int>> used;
        auto spot = [&](int& x, int& y) {
            for (int a = 0; a < 20; a++) {
                x = rng.irange(b.x0 + 1, b.x0 + b.w - 2);
                y = rng.irange(b.y0 + 1, b.y0 + b.h - 3);
                bool clash = false;
                for (auto& u : used) clash = clash || (std::abs(u.first - x) <= 1 && std::abs(u.second - y) <= 1);
                if (!clash) { used.push_back({x, y}); return true; }
            }
            return false;
        };
        auto put = [&](int kind, int pick, int grown) {
            int x, y;
            if (!spot(x, y)) return;
            WorldProp p;
            p.kind = PROP_OBJECT;
            p.variant = (uint8_t)kind;
            p.frame = Art::objectFrame(pick, grown);
            p.flipX = rng.chance(0.5f);
            p.pos = Vec2(x * (float)TILE + TILE * 0.5f + rng.irange(-3, 3), (y + 1) * (float)TILE);
            addRoofProp(bi, p);
        };
        int area = (b.w - 2) * (b.h - 3);
        int og = rng.chance(0.35f) ? grownFor(b.x0, b.y0) : 0;
        for (int i = 0, n = 1 + area / 40; i < n; i++) put(OB_HVAC, 0, og);
        for (int i = 0, n = rng.irange(1, 2 + area / 30); i < n; i++) put(OB_VENT, rng.irange(0, 3), 0);
        if (rng.chance(0.5f)) put(OB_ANTENNA, rng.irange(0, 1), 0);
        if (rng.chance(0.4f)) put(OB_DUCT, rng.irange(0, 2), 0);
        if (rng.chance(0.2f)) put(OB_ROOF_HOLE, rng.irange(0, 1), 0);
    }

    void buildings() {
        roofs.assign(W.buildings.size(), {});
        for (size_t bi = 0; bi < W.buildings.size(); bi++) {
            const Building& b = W.buildings[bi];
            if (b.kind == BK_FLOOR || b.kind == BK_BUNKER || b.kind == BK_GARAGE) continue;
            if (b.x0 >= ow || b.y0 >= oh) continue;
            around(b);
            front(bi);
            roof(bi);
        }
        W.roofProps.clear();
        for (size_t bi = 0; bi < W.buildings.size(); bi++) {
            W.buildings[bi].roofProp0 = (int)W.roofProps.size();
            W.buildings[bi].roofPropN = (int)roofs[bi].size();
            for (const WorldProp& p : roofs[bi]) W.roofProps.push_back(p);
        }
    }

    // ---- 6. grass creeping over the paving -------------------------------------------
    // Grass_On-Top_TileSet: an edge ring (N+W 0, N 1, N+E 2, W 8, E 10, S+W 16, S 17,
    // S+E 18) and inner corners (SE 3, SW 4, NE 11, NW 12), then a few loose tufts.
    void creep() {
        auto wild = [&](int x, int y) {
            if (!W.inBounds(x, y)) return false;
            const Tile& t = W.at(x, y);
            return (t.ground == G_GRASS || t.ground == G_SAND) && !(t.flags & TF_KERB);
        };
        for (int y = 3; y < oh - 3; y++)
            for (int x = 3; x < ow - 3; x++) {
                Tile& t = W.at(x, y);
                if (t.overlay || compound(x, y)) continue;
                if (t.ground != G_PAVEMENT && t.ground != G_RUBBLE && !(t.ground == G_ROAD && W.cityAt(x, y) >= 0)) continue;
                bool n = wild(x, y - 1), s = wild(x, y + 1), w = wild(x - 1, y), e = wild(x + 1, y);
                if (n && s) s = false;
                if (w && e) e = false;
                int f = -1;
                if (n && w) f = 0;
                else if (n && e) f = 2;
                else if (s && w) f = 16;
                else if (s && e) f = 18;
                else if (n) f = 1;
                else if (s) f = 17;
                else if (w) f = 8;
                else if (e) f = 10;
                else if (wild(x - 1, y - 1)) f = 12;
                else if (wild(x + 1, y - 1)) f = 11;
                else if (wild(x - 1, y + 1)) f = 4;
                else if (wild(x + 1, y + 1)) f = 3;
                if (f < 0) {
                    // Loose tufts out on the paving of the overgrown cities.
                    int c = W.cityAt(x, y);
                    float og = c >= 0 ? W.cities[c].overgrowth : 0.2f;
                    if (t.ground != G_ROAD && chanceAt(x, y, 9) < 0.02f + 0.06f * og) {
                        static const int LOOSE[4] = {19, 20, 35, 36};
                        f = LOOSE[hash2(x, y, salt) & 3];
                    }
                }
                if (f >= 0) t.overlay = (uint8_t)(Art::OV_GRASSTOP + f);
            }
    }
};

}  // namespace

void dressWorld(World& w, uint64_t seed) {
    if (w.outW <= 0 || w.outH <= 0) return;
    Dresser d(w, seed);
    d.buildBusy();
    d.tones();
    d.cities();
    d.buildings();
    d.nature();
    d.countryRoads();
    d.creep();
    // OUTBOUND_DRESS_LOG=1: list the city blocks (tile coordinates), for looking at them.
    if (std::getenv("OUTBOUND_DRESS_LOG"))
        for (const CityBlock& b : w.blocks)
            std::fprintf(stderr, "[dress] city %d block kind %d yard %d at %d,%d size %dx%d\n", b.city, b.kind, b.yard, b.x0, b.y0, b.w, b.h);
    if (std::getenv("OUTBOUND_DRESS_LOG"))
        for (const Building& b : w.buildings)
            if (b.kind != BK_CITY && b.kind != BK_FLOOR)
                std::fprintf(stderr, "[dress] building kind %d at %d,%d size %dx%d\n", b.kind, b.x0, b.y0, b.w, b.h);
}
