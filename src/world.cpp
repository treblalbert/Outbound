#include "world.h"
#include "art.h"
#include "sprites.h"
#include <cstring>
#include <queue>

using namespace Sprites;

//                                                                    radius
static const SolidInfo SOLIDS[S_COUNT] = {
    {WHITE, 0, false, false, P_DARK,             0.0f},          // S_NONE
    {TREE, 70, true, false, P_SAGE,              4.5f},          // S_TREE: trunk only
    {BUSH, 12, false, false, P_SAGE,             3.5f},          // S_BUSH
    // Pebbles: eight pixels of rock that used to stop you dead. Now they are only
    // something to look at and to blow away.
    {ROCK, 180, false, false, P_LAVENDER,        0.0f, false},   // S_ROCK
    {WALL_BRICK, 130, true, true, P_CORAL,       0.0f},          // S_WALL_BRICK
    {WALL_CONCRETE, 240, true, true, P_WHITE,    0.0f},          // S_WALL_CONCRETE
    {WALL_WOOD, 55, true, true, P_ORANGE,        0.0f},          // S_WALL_WOOD
    {CRATE, 35, true, true, P_ORANGE,            0.0f},          // S_CRATE
    {FENCE, 25, false, false, P_TAN,             0.0f},          // S_FENCE
    {SANDBAG, 110, true, true, P_CREAM,          0.0f},          // S_SANDBAG
    {C_CRATE, -1, true, false, P_YELLOW,         0.0f},          // S_CONTAINER
    {BUNKER_WALL, -1, true, false, P_PURPLE,     0.0f},          // S_BUNKER
    {BOUNDARY, -1, true, false, P_PURPLE,        0.0f},          // S_BOUNDARY
    {WHITE, -1, true, false, P_PINK,             0.0f},          // S_FURNITURE
    {WHITE, -1, true, false, P_LAVENDER,         0.0f},          // S_CAR
    // A street light: the base of the pole stops you, the thin shaft does not stop
    // bullets, and the pole itself is drawn as a prop.
    {WHITE, -1, false, false, P_TAN,             3.0f},          // S_POLE
    {WHITE, 45, true, true, P_ORANGE,             0.0f},          // S_DOOR
    {WHITE, -1, false, false, P_ORANGE,            0.0f, false},   // S_DOOR_OPEN
    // A base turret: it stops you and the zombies, but its own side shoots over it.
    // Its health lives on the Turret, not the tile, so bullets cannot break it.
    {WHITE, -1, false, false, P_CORAL,             6.0f},          // S_TURRET
    {WHITE, -1, true, false, P_DARK,               0.0f},          // S_CRYPT_WALL
    {WHITE, -1, true, false, P_PURPLE,             0.0f},          // S_CRYPT_PROP: pillars, coffins, the stair arch
    {WHITE, -1, true, false, P_PURPLE,             0.0f},          // S_CRYPT_GATE: the last room's sealed way back (0.11v)
    {WHITE, -1, true, false, P_DARK,               0.0f},          // S_VOID: nothing, round an upper floor (0.11v)
    {WHITE, -1, false, false, P_TAN,               0.0f},          // S_STAIRS: a flight of stairs (0.11v)
    // Barricades (0.12v): waist high, so bullets fly over them, but nothing walks through.
    {WHITE, -1, false, false, P_ORANGE,            0.0f},          // S_BARRICADE
    {WHITE, -1, false, false, P_ORANGE,            0.0f},          // S_GATE
    {WHITE, -1, false, false, P_TAN,               0.0f, false},   // S_GATE_OPEN
    {FENCE, 60, false, false, P_TAN,               0.0f},          // S_FENCE_GATE
    {FENCE, -1, false, false, P_TAN,               0.0f, false},   // S_FENCE_GATE_OPEN
    // A roadblock on a closed bridge (0.13v): waist high, nothing gets through it.
    {WHITE, -1, false, false, P_CORAL,             0.0f},          // S_ROADBLOCK
};

// The island (0.13v): see world.h. The home ground is the old day-one map, 240 tiles
// across, in the middle of the south half; the sea keeps at least 8 tiles between any
// two regions' rectangles, and the coasts are carved inside them.
const Region REGIONS[RG_COUNT] = {
    {280, 300, 240, 240, 1, "Home"},
    {160, 368, 112, 124, CITY_DAY, "West woods"},
    {16, 296, 136, 256, CITY_DAY, "The city"},
    {336, 188, 128, 104, MILITARY_DAY, "North woods"},
    {286, 28, 228, 152, MILITARY_DAY, "Army base"},
    {528, 330, 168, 190, RADIO_DAY, "Radio hill"},
    {296, 548, 208, 148, METRO_DAY, "The suburb"},
};

// What can stand in a building. FP_WALL pieces go against the back wall, FP_FREE ones
// in the open (never blocking a way through), and rugs lie on the floor.
const FurnPiece FURN_PIECES[] = {
    {"bookshelf", 2, FP_WALL}, {"bookshelf_b", 2, FP_WALL}, {"cabinet", 2, FP_WALL}, {"shelf_narrow", 1, FP_WALL},
    {"drawers", 1, FP_WALL}, {"stove", 1, FP_WALL}, {"fridge", 1, FP_WALL}, {"plant_red", 1, FP_WALL},
    {"plant_basket", 1, FP_WALL}, {"plant_grey", 1, FP_WALL}, {"floorlamp_a", 1, FP_WALL}, {"floorlamp_c", 1, FP_WALL},
    {"sofa_grey", 2, FP_FREE}, {"sofa_beige", 2, FP_FREE}, {"armchair_grey", 1, FP_FREE}, {"table_round", 1, FP_FREE},
    {"table_long", 2, FP_FREE}, {"rug_red", 2, FP_RUG}, {"rug_red_b", 2, FP_RUG}, {"rug_round", 2, FP_RUG},
    // 0.12v
    {"tv", 2, FP_WALL}, {"tablelamp", 1, FP_WALL}, {"mirror", 1, FP_WALL},
};
const int FURN_PIECE_COUNT = sizeof(FURN_PIECES) / sizeof(FURN_PIECES[0]);

const SolidInfo& solidInfo(int s) { return SOLIDS[(s >= 0 && s < S_COUNT) ? s : 0]; }

bool (*g_dynamicBlock)(float x, float y, float r) = nullptr;
float (*g_dynamicOverlap)(float x, float y, float r) = nullptr;

static const int GROUND_MAP_COLOR[G_COUNT] = {
    P_LGREEN, P_TAN, P_CREAM, P_BLUE, P_BEIGE, P_TAN, P_TAN, P_BEIGE, P_MINT, P_LAVENDER, P_LAVENDER, P_PURPLE,
    P_CREAM, P_DARK, P_TAN,
};

// ---------------------------------------------------------------- noise
static float valueNoise(float x, float y, uint32_t seed) {
    int xi = (int)std::floor(x), yi = (int)std::floor(y);
    float fx = x - xi, fy = y - yi;
    auto h = [&](int a, int b) { return (hash2(a, b, seed) & 0xFFFF) / 65535.0f; };
    float sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
    float a = lerpf(h(xi, yi), h(xi + 1, yi), sx);
    float b = lerpf(h(xi, yi + 1), h(xi + 1, yi + 1), sx);
    return lerpf(a, b, sy);
}

static float fbm(float x, float y, uint32_t seed) {
    float v = 0, amp = 0.5f, total = 0;
    for (int o = 0; o < 4; o++) {
        v += valueNoise(x, y, seed + o * 1013) * amp;
        total += amp;
        x *= 2.03f; y *= 2.03f; amp *= 0.5f;
    }
    return v / total;
}

// ---------------------------------------------------------------- queries
// Loot gets better further from the bunker. On the bigger map (0.11v) the old map's
// radius still reaches three quarters of the way; the cities add a quarter more.
float World::lootQuality(int tx, int ty) const {
    float d = std::sqrt(float((tx - homeTx) * (tx - homeTx) + (ty - homeTy) * (ty - homeTy)));
    float q = clampf(d / 132.0f, 0, 1);
    // Each region out past the home ground has a floor of its own (0.13v).
    static const float FLOOR[RG_COUNT] = {0.0f, 0.5f, 0.62f, 0.55f, 0.8f, 0.78f, 0.88f};
    int r = regionAtTile(tx, ty);
    if (r >= 0) q = std::max(q * (r == RG_HOME ? 1.0f : 0.8f), FLOOR[r]);
    q = clampf(q + day * 0.022f, 0, 1);
    // However far you walk, the first days' loot stays modest (0.12v): 0.43 on day 1,
    // 0.73 on day 5, the whole range by day 9.
    q = std::min(q, 0.35f + 0.075f * day);
    if (cityAt(tx, ty) >= 0) q = std::min(1.3f, q + 0.25f);
    return q;
}

bool World::blocksMove(int tx, int ty, bool ghost) const {
    if (!inBounds(tx, ty)) return true;
    const Tile& t = at(tx, ty);
    if (ghost) return t.solid == S_BOUNDARY || t.solid == S_BUNKER || t.solid == S_CRYPT_WALL;
    if (t.solid != S_NONE && !solidInfo(t.solid).blocksMove) return t.ground == G_WATER;
    if (t.solid != S_NONE) {
        // Cars are sampled from sprite alpha by collides(), outside the tile query.
        if (t.solid == S_CAR) return false; // handled against sprite alpha in collides()
        return true;
    }
    return t.ground == G_WATER;
}

// Where a round obstacle's collider sits. Trunks and pebbles are mid-tile; a street
// light's pole stands on the bottom edge of its tile, where the art meets the ground.
static Vec2 blockCenter(const World& w, int tx, int ty) {
    Vec2 c = World::tileCenter(tx, ty);
    if (w.inBounds(tx, ty) && w.at(tx, ty).solid == S_POLE) c.y += TILE * 0.5f - 3.0f;
    return c;
}

bool World::blocksBullet(int tx, int ty) const {
    if (!inBounds(tx, ty)) return true;
    return solidInfo(at(tx, ty).solid).blocksBullets;
}

float World::blockRadius(int tx, int ty) const {
    if (!inBounds(tx, ty)) return 0;                     // out of bounds blocks everything
    const Tile& t = at(tx, ty);
    if (t.ground == G_WATER) return 0;                   // water fills its tile
    if (t.solid == S_NONE) return 0;
    return solidInfo(t.solid).radius;
}

// Parked cars and wrecks stop you and bullets with their pixels, not their tiles: the
// tiles only reserve the ground under them, and propAt says which prop is there.
static bool pixelProp(uint8_t kind) { return kind == PROP_CAR || kind == PROP_WRECK || kind == PROP_OBJECT; }
static Art::Piece propArt(const WorldProp& p) { return Art::propArt(p); }

void World::indexProps() {
    propAt.assign(tiles.size(), -1);
    for (size_t i = 0; i < props.size() && i < 32767; i++) {
        const WorldProp& pr = props[i];
        if (!pixelProp(pr.kind)) continue;
        Art::Piece pc = propArt(pr);
        if (!pc.valid()) continue;
        const Assets::Sprite& s = *pc.sprite;
        int x0 = toTile(pr.pos.x - s.w * 0.5f), x1 = toTile(pr.pos.x + s.w * 0.5f - 0.01f);
        int y0 = toTile(pr.pos.y - s.h), y1 = toTile(pr.pos.y - 0.01f);
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++)
                if (inBounds(x, y)) propAt[(size_t)y * w + x] = (int16_t)i;
    }
}

// Calls f(prop) once for each pixel-collided prop on the tiles round (x, y).
template <class F> static void propsNear(const World& W, float x, float y, float r, F f) {
    if (W.propAt.size() != W.tiles.size()) return;
    int x0 = World::toTile(x - r), x1 = World::toTile(x + r), y0 = World::toTile(y - r), y1 = World::toTile(y + r);
    int seen[4] = {-1, -1, -1, -1}, ns = 0;
    for (int ty = y0; ty <= y1; ty++)
        for (int tx = x0; tx <= x1; tx++) {
            if (!W.inBounds(tx, ty)) continue;
            int i = W.propAt[(size_t)ty * W.w + tx];
            if (i < 0) continue;
            bool dup = false;
            for (int k = 0; k < ns; k++) dup = dup || seen[k] == i;
            if (dup) continue;
            if (ns < 4) seen[ns++] = i;
            if (f(W.props[i])) return;
        }
}

bool World::blocksBulletAt(Vec2 p) const {
    int tx = toTile(p.x), ty = toTile(p.y);
    if (!inBounds(tx, ty)) return true;
    const Tile& t = at(tx, ty);
    if (t.solid == S_CAR) {
        bool hit = false;
        propsNear(*this, p.x, p.y, 0, [&](const WorldProp& prop) {
            Art::Piece pc = propArt(prop);
            if (!pc.valid()) return false;
            const Assets::Sprite& s = *pc.sprite;
            int px = (int)std::floor(p.x - (prop.pos.x - s.w * 0.5f));
            int py = (int)std::floor(p.y - (prop.pos.y - s.h));
            if (prop.flipX) px = s.w - 1 - px;
            hit = s.opaqueAt(pc.frame, px, py);
            return hit;
        });
        return hit;
    }
    if (!solidInfo(t.solid).blocksBullets) return false;
    float cr = solidInfo(t.solid).radius;
    if (cr <= 0) return true;
    Vec2 c = blockCenter(*this, tx, ty);
    float dx = p.x - c.x, dy = p.y - c.y;
    return dx * dx + dy * dy < cr * cr;
}

bool World::collides(float x, float y, float r, bool ghost) const {
    if (!ghost) {
        bool hit = false;
        propsNear(*this, x, y, r, [&](const WorldProp& prop) {
            Art::Piece pc = propArt(prop);
            if (!pc.valid()) return false;
            const Assets::Sprite& s = *pc.sprite;
            float left = prop.pos.x - s.w * 0.5f, top = prop.pos.y - s.h;
            if (x + r < left || x - r >= left + s.w || y + r < top || y - r >= top + s.h) return false;
            // Sample the moving circle densely enough for one-pixel fins, bumpers and gaps.
            for (int sy = (int)std::floor(y - r); sy <= (int)std::ceil(y + r); ++sy)
                for (int sx = (int)std::floor(x - r); sx <= (int)std::ceil(x + r); ++sx) {
                    float dx = sx + 0.5f - x, dy = sy + 0.5f - y;
                    if (dx * dx + dy * dy > r * r) continue;
                    int px = (int)std::floor(sx + 0.5f - left), py = (int)std::floor(sy + 0.5f - top);
                    if (prop.flipX) px = s.w - 1 - px;
                    if (s.opaqueAt(pc.frame, px, py)) { hit = true; return true; }
                }
            return false;
        });
        if (hit) return true;
        if (g_dynamicBlock && g_dynamicBlock(x, y, r)) return true;
    }
    int x0 = toTile(x - r), x1 = toTile(x + r), y0 = toTile(y - r), y1 = toTile(y + r);
    for (int ty = y0; ty <= y1; ty++)
        for (int tx = x0; tx <= x1; tx++) {
            if (!blocksMove(tx, ty, ghost)) continue;
            float cr = blockRadius(tx, ty);
            if (cr <= 0) return true;                    // fills its tile
            Vec2 c = blockCenter(*this, tx, ty);
            float dx = x - c.x, dy = y - c.y, rr = cr + r;
            if (dx * dx + dy * dy < rr * rr) return true;
        }
    return false;
}

Vec2 World::move(Vec2 p, Vec2 d, float r, bool ghost) const {
    // Measure overlap, not just yes/no collision. If rounding or a fast prior step
    // left the actor a fraction inside an obstacle, a tangential move with the same
    // overlap is allowed while any move deeper into it is rejected. This preserves
    // wall sliding around round trees and alpha-masked cars without tunnelling.
    auto overlap = [&](float x, float y) {
        float cost = 0.0f;
        if (!ghost) propsNear(*this, x, y, r, [&](const WorldProp& prop) {
            Art::Piece pc = propArt(prop);
            if (!pc.valid()) return false;
            const Assets::Sprite& s = *pc.sprite;
            float left = prop.pos.x - s.w * 0.5f, top = prop.pos.y - s.h;
            if (x + r < left || x - r >= left + s.w || y + r < top || y - r >= top + s.h) return false;
            // Use the sprite bounds only to measure depth. The yes/no test remains
            // alpha-accurate below, while this smooth proxy gives a stable normal
            // for sliding along irregular bumper pixels.
            float qx = clampf(x, left, left + s.w), qy = clampf(y, top, top + s.h);
            float dx = x - qx, dy = y - qy;
            float dd = std::sqrt(dx * dx + dy * dy);
            if (dd < r) cost += r - dd;
            return false;
        });
        if (!ghost && g_dynamicOverlap) cost += g_dynamicOverlap(x, y, r);
        int x0 = toTile(x - r), x1 = toTile(x + r), y0 = toTile(y - r), y1 = toTile(y + r);
        for (int ty = y0; ty <= y1; ty++)
            for (int tx = x0; tx <= x1; tx++) {
                if (!blocksMove(tx, ty, ghost)) continue;
                float cr = blockRadius(tx, ty);
                if (cr > 0) {
                    float pen = cr + r - dist(Vec2(x, y), blockCenter(*this, tx, ty));
                    if (pen > 0) cost += pen;
                    continue;
                }
                float left = tx * (float)TILE, top = ty * (float)TILE;
                float qx = clampf(x, left, left + TILE), qy = clampf(y, top, top + TILE);
                float dx = x - qx, dy = y - qy;
                float dd = std::sqrt(dx * dx + dy * dy);
                if (dd < r) cost += r - dd;
            }
        return cost;
    };
    if (d.x != 0) {
        float nx = p.x + d.x;
        bool beforeHit = collides(p.x, p.y, r, ghost), afterHit = collides(nx, p.y, r, ghost);
        if (!afterHit || (beforeHit && overlap(nx, p.y) <= overlap(p.x, p.y) + 0.001f)) p.x = nx;
    }
    if (d.y != 0) {
        float ny = p.y + d.y;
        bool beforeHit = collides(p.x, p.y, r, ghost), afterHit = collides(p.x, ny, r, ghost);
        if (!afterHit || (beforeHit && overlap(p.x, ny) <= overlap(p.x, p.y) + 0.001f)) p.y = ny;
    }
    return p;
}

void World::furnishBuildings(size_t from) {
    std::vector<uint8_t> nearSpawn(tiles.size(), 0);
    for (const EnemySpawn& sp : spawns) {
        int sx = toTile(sp.pos.x), sy = toTile(sp.pos.y);
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++)
                if (inBounds(sx + dx, sy + dy)) nearSpawn[(sy + dy) * w + sx + dx] = 1;
    }
    auto isDoorTile = [&](int x, int y) { return inBounds(x, y) && (at(x, y).solid == S_DOOR || at(x, y).solid == S_DOOR_OPEN); };
    // A tile furniture may take: open floor, not by a door, a container or a raider.
    auto freeTile = [&](int x, int y, const Building& b) {
        if (x <= b.x0 || y <= b.y0 || x >= b.x0 + b.w - 1 || y >= b.y0 + b.h - 1) return false;
        const Tile& t = at(x, y);
        if (t.solid != S_NONE || t.furn || t.container >= 0 || nearSpawn[y * w + x]) return false;
        for (int dy = -2; dy <= 2; dy++)
            for (int dx = -2; dx <= 2; dx++)
                if (isDoorTile(x + dx, y + dy) || (inBounds(x + dx, y + dy) && at(x + dx, y + dy).solid == S_STAIRS)) return false;
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++)
                if (inBounds(x + dx, y + dy) && at(x + dx, y + dy).solid == S_CONTAINER) return false;
        return true;
    };
    for (size_t bi = from; bi < buildings.size(); bi++) {
        const Building& b = buildings[bi];
        if (b.w < 5 || b.h < 5) continue;
        // Its own dice, from where it stands: the rest of the day's world is untouched.
        Rng r(mix64(((uint64_t)(uint32_t)b.x0 << 32) ^ (uint32_t)b.y0 ^ 0xF0A1C7ull));
        int area = (b.w - 2) * (b.h - 2);
        int wantWall = std::max(1, area / 14), wantFree = area >= 30 ? r.irange(1, 2) : r.irange(0, 1), wantRug = r.irange(0, 1);
        auto place = [&](int piece, int x, int y, bool solid) {
            const FurnPiece& f = FURN_PIECES[piece];
            for (int i = 0; i < f.w; i++) {
                Tile& t = at(x + i, y);
                t.furn = i == 0 ? (uint8_t)(piece + 1) : FURN_REST;
                if (solid) { t.solid = S_FURNITURE; t.hp = -1; }
            }
        };
        auto pick = [&](uint8_t kind) {
            int n = 0;
            for (int i = 0; i < FURN_PIECE_COUNT; i++) n += FURN_PIECES[i].place == kind;
            int k = (int)(r.next() % (uint32_t)n);
            for (int i = 0; i < FURN_PIECE_COUNT; i++)
                if (FURN_PIECES[i].place == kind && k-- == 0) return i;
            return 0;
        };
        // Against the back wall: the row just inside the top wall, where a piece faces
        // the room. Whatever stands there must leave the row in front of it clear.
        for (int tries = 0; tries < 40 && wantWall > 0; tries++) {
            int piece = pick(FP_WALL);
            const FurnPiece& f = FURN_PIECES[piece];
            if (b.x0 + b.w - 1 - f.w < b.x0 + 1) continue;   // too narrow for it
            int x = r.irange(b.x0 + 1, b.x0 + b.w - 1 - f.w), y = b.y0 + 1;
            bool ok = true;
            for (int i = 0; i < f.w && ok; i++) ok = freeTile(x + i, y, b) && at(x + i, y + 1).solid == S_NONE && !at(x + i, y + 1).furn;
            if (!ok) continue;
            place(piece, x, y, true);
            wantWall--;
        }
        // Out in the room: only where every side stays open, so nothing gets walled in.
        for (int tries = 0; tries < 40 && wantFree > 0; tries++) {
            int piece = pick(FP_FREE);
            const FurnPiece& f = FURN_PIECES[piece];
            if (b.x0 + b.w - 2 - f.w < b.x0 + 2 || b.y0 + b.h - 3 < b.y0 + 2) continue;
            int x = r.irange(b.x0 + 2, b.x0 + b.w - 2 - f.w), y = r.irange(b.y0 + 2, b.y0 + b.h - 3);
            bool ok = true;
            for (int yy = y - 1; yy <= y + 1 && ok; yy++)
                for (int xx = x - 1; xx <= x + f.w && ok; xx++) {
                    const Tile& t = at(xx, yy);
                    ok = t.solid == S_NONE && !t.furn && t.container < 0;
                }
            for (int i = 0; i < f.w && ok; i++) ok = freeTile(x + i, y, b);
            if (!ok) continue;
            place(piece, x, y, true);
            wantFree--;
        }
        // A rug on open floor. It lies flat, so it blocks nothing.
        for (int tries = 0; tries < 20 && wantRug > 0; tries++) {
            int piece = pick(FP_RUG);
            const FurnPiece& f = FURN_PIECES[piece];
            if (b.x0 + b.w - 1 - f.w < b.x0 + 1 || b.y0 + b.h - 2 < b.y0 + 2) continue;
            int x = r.irange(b.x0 + 1, b.x0 + b.w - 1 - f.w), y = r.irange(b.y0 + 2, b.y0 + b.h - 2);
            bool ok = true;
            for (int i = 0; i < f.w && ok; i++) { const Tile& t = at(x + i, y); ok = t.solid == S_NONE && !t.furn && t.container < 0; }
            if (!ok) continue;
            place(piece, x, y, false);
            wantRug--;
        }
        // A painting on the back wall now and then (0.12v), over the furniture.
        if (b.w >= 5 && r.chance(0.45f)) {
            int x = r.irange(b.x0 + 1, b.x0 + b.w - 3);
            if (at(x, b.y0).solid != S_NONE && at(x + 1, b.y0).solid != S_NONE && at(x, b.y0).solid != S_DOOR && at(x + 1, b.y0).solid != S_DOOR) {
                WorldProp p;
                p.kind = PROP_WALLDECO;
                p.variant = Art::OB_PAINTING;
                p.frame = Art::objectFrame(r.irange(0, 1), 0);
                p.pos = Vec2((x + 1) * (float)TILE, (b.y0 + 1) * (float)TILE - 1);
                props.push_back(p);
            }
        }
    }
}

bool World::lineOfSight(Vec2 a, Vec2 b) const {
    Vec2 d = b - a;
    float len = length(d);
    int steps = (int)(len / 6.0f) + 1;
    for (int i = 1; i < steps; i++) {
        Vec2 p = a + d * (i / float(steps));
        if (blocksBulletAt(p)) return false;
    }
    return true;
}

bool World::openDoorNear(Vec2 pos, float radius) {
    int cx = toTile(pos.x), cy = toTile(pos.y);
    int r = (int)std::ceil(radius / TILE) + 1;
    int bestX = -1, bestY = -1;
    float best = radius * radius;
    for (int y = cy - r; y <= cy + r; y++)
        for (int x = cx - r; x <= cx + r; x++) {
            if (!inBounds(x, y) || at(x, y).solid != S_DOOR) continue;
            float d = lengthSq(tileCenter(x, y) - pos);
            if (d <= best) { best = d; bestX = x; bestY = y; }
        }
    if (bestX < 0) return false;
    // Generated entrances are two tiles wide. Open the paired leaf as well so a
    // character never catches on an invisible half-door.
    for (int y = bestY - 1; y <= bestY + 1; y++)
        for (int x = bestX - 1; x <= bestX + 1; x++)
            if (inBounds(x, y) && at(x, y).solid == S_DOOR) {
                at(x, y).solid = S_DOOR_OPEN;
                at(x, y).hp = 0;
                updateMapPixel(x, y);
            }
    return true;
}

bool World::gateOpen(int i) const {
    if (i < 0 || i >= (int)dungeons.size() || !dungeons[i].hasGate()) return false;
    const Dungeon& d = dungeons[i];
    return inBounds(d.gateX + 1, d.gateY) && at(d.gateX + 1, d.gateY).solid != S_CRYPT_GATE;
}

void World::openGate(int i) {
    if (i < 0 || i >= (int)dungeons.size() || !dungeons[i].hasGate()) return;
    const Dungeon& d = dungeons[i];
    for (int x = d.gateX + 1; x <= d.gateX + 2; x++) {
        Tile& t = at(x, d.gateY);
        t.solid = S_NONE;
        t.hp = 0;
        updateMapPixel(x, d.gateY);
    }
}

int World::gateNear(Vec2 pos, float radius) const {
    for (int i = 0; i < (int)dungeons.size(); i++) {
        const Dungeon& d = dungeons[i];
        if (!d.hasGate()) continue;
        Vec2 c((d.gateX + 2) * (float)TILE, d.gateY * (float)TILE + TILE * 0.5f);
        if (dist(pos, c) < radius) return i;
    }
    return -1;
}

bool World::closeDoorNear(Vec2 pos, float radius) {
    int cx = toTile(pos.x), cy = toTile(pos.y);
    int r = (int)std::ceil(radius / TILE) + 1;
    int bestX = -1, bestY = -1;
    float best = radius * radius;
    for (int y = cy - r; y <= cy + r; y++)
        for (int x = cx - r; x <= cx + r; x++) {
            if (!inBounds(x, y) || at(x, y).solid != S_DOOR_OPEN) continue;
            float d = lengthSq(tileCenter(x, y) - pos);
            if (d <= best) { best = d; bestX = x; bestY = y; }
        }
    if (bestX < 0) return false;
    for (int y = bestY - 1; y <= bestY + 1; y++)
        for (int x = bestX - 1; x <= bestX + 1; x++)
            if (inBounds(x, y) && at(x, y).solid == S_DOOR_OPEN) {
                at(x, y).solid = S_DOOR;
                at(x, y).hp = (int16_t)solidInfo(S_DOOR).hp;
                updateMapPixel(x, y);
            }
    return true;
}

bool World::damageTile(int tx, int ty, float dmg) {
    if (!inBounds(tx, ty)) return false;
    Tile& t = at(tx, ty);
    if (t.solid == S_NONE || solidInfo(t.solid).hp < 0) return false;
    t.hp = (int16_t)(t.hp - std::max(1.0f, dmg));
    if (t.hp <= 0) {
        destroyTile(tx, ty);
        return true;
    }
    return false;
}

void World::destroyTile(int tx, int ty) {
    if (!inBounds(tx, ty)) return;
    Tile& t = at(tx, ty);
    if (t.solid == S_NONE || solidInfo(t.solid).hp < 0) return;
    // Whatever stood here is simply gone: the tile shows the ground it was built on
    // (a building's floor under its walls, grass or dirt under a crate).
    if (t.solid == S_TREE) t.deco = STUMP;
    t.solid = S_NONE;
    t.hp = 0;
    updateMapPixel(tx, ty);
}

// ---------------------------------------------------------------- flow field
void World::computeFlow(Vec2 target, int size) {
    flowSize = size;
    int cx = toTile(target.x), cy = toTile(target.y);
    flowX0 = cx - size / 2;
    flowY0 = cy - size / 2;
    flow.assign(size * size, -1);
    std::queue<std::pair<int, int>> q;
    auto idx = [&](int x, int y) { return (y - flowY0) * size + (x - flowX0); };
    auto inWin = [&](int x, int y) { return x >= flowX0 && y >= flowY0 && x < flowX0 + size && y < flowY0 + size; };
    if (!inWin(cx, cy)) return;
    flow[idx(cx, cy)] = 0;
    q.push({cx, cy});
    const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
    while (!q.empty()) {
        auto [x, y] = q.front();
        q.pop();
        int d = flow[idx(x, y)];
        for (int k = 0; k < 4; k++) {
            int nx = x + dx[k], ny = y + dy[k];
            if (!inWin(nx, ny) || (blocksMove(nx, ny) && at(nx, ny).solid != S_DOOR)) continue;
            int16_t& f = flow[idx(nx, ny)];
            if (f != -1) continue;
            f = (int16_t)(d + 1);
            q.push({nx, ny});
        }
    }
}

bool World::flowDir(Vec2 pos, Vec2& dir) const {
    if (flowSize == 0) return false;
    int x = toTile(pos.x), y = toTile(pos.y);
    auto get = [&](int tx, int ty) -> int {
        if (tx < flowX0 || ty < flowY0 || tx >= flowX0 + flowSize || ty >= flowY0 + flowSize) return -1;
        return flow[(ty - flowY0) * flowSize + (tx - flowX0)];
    };
    int cur = get(x, y);
    if (cur < 0) return false;
    if (cur == 0) return false;
    int best = cur, bx = x, by = y;
    for (int oy = -1; oy <= 1; oy++)
        for (int ox = -1; ox <= 1; ox++) {
            if (!ox && !oy) continue;
            int v = get(x + ox, y + oy);
            if (v < 0 || v >= best) continue;
            if (ox && oy && (get(x + ox, y) < 0 || get(x, y + oy) < 0)) continue;
            best = v; bx = x + ox; by = y + oy;
        }
    if (bx == x && by == y) return false;
    dir = normalize(tileCenter(bx, by) - pos);
    return true;
}

// ---------------------------------------------------------------- minimap
void World::updateMapPixel(int tx, int ty) {
    if (!inBounds(tx, ty) || mapPixels.empty()) return;
    const Tile& t = at(tx, ty);
    // Pebbles are not in the way, so the map shows the ground under them instead.
    bool obstacle = t.solid != S_NONE && solidInfo(t.solid).blocksMove && t.solid != S_FURNITURE;   // furniture: show the floor
    int p = !t.explored ? P_DARK : (obstacle ? solidInfo(t.solid).mapColor : GROUND_MAP_COLOR[t.ground]);
    if (t.explored && t.container >= 0) p = P_YELLOW;
    uint32_t hex = PALETTE_HEX[p];
    uint8_t* d = &mapPixels[(ty * w + tx) * 4];
    d[0] = (hex >> 16) & 255; d[1] = (hex >> 8) & 255; d[2] = hex & 255; d[3] = 255;
    mapDirty = true;
}

void World::rebuildMap() {
    mapPixels.assign(w * h * 4, 0);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) updateMapPixel(x, y);
}

void World::reveal(Vec2 pos, int radius) {
    int cx = toTile(pos.x), cy = toTile(pos.y);
    for (int y = cy - radius; y <= cy + radius; y++)
        for (int x = cx - radius; x <= cx + radius; x++) {
            if (!inBounds(x, y) || at(x, y).explored) continue;
            if ((x - cx) * (x - cx) + (y - cy) * (y - cy) > radius * radius) continue;
            if (dungeonAtTile(x, y) >= 0) continue;   // the catacombs are only mapped by what you see
            at(x, y).explored = 1;
            updateMapPixel(x, y);
        }
}

int World::addContainer(Vec2 pos, int kind, int tx, int ty, uint8_t variant) {
    Container c;
    c.pos = pos;
    c.kind = kind;
    c.variant = variant;
    c.tx = tx;
    c.ty = ty;
    c.items.assign(12, Item());
    containers.push_back(c);
    int id = (int)containers.size() - 1;
    if (tx >= 0 && inBounds(tx, ty)) {
        Tile& t = at(tx, ty);
        t.solid = S_CONTAINER;
        t.container = (int16_t)id;
        t.hp = -1;
    }
    return id;
}

bool World::placeStreetLight(int x, int y, int variant, bool flip) {
    if (!inBounds(x, y) || at(x, y).solid != S_NONE) return false;
    at(x, y).solid = S_POLE;
    at(x, y).hp = -1;
    at(x, y).worldDeco = 0;
    WorldProp p;
    p.kind = PROP_STREETLIGHT;
    p.variant = (uint8_t)variant;
    p.flipX = flip;
    p.pos = Vec2(x * TILE + TILE * 0.5f, (y + 1) * (float)TILE);
    // The pole is a narrow column at one edge of the side-arm art, and the art is drawn
    // centred on p.pos. Shift it so the pole's foot lands in the middle of the tile that
    // reserves it (and carries its collider).
    Art::Piece art = Art::streetLight(p.variant);
    if (art.valid()) {
        const Assets::Sprite& s = *art.sprite;
        int sum = 0, n = 0;
        for (int px = 0; px < s.w; px++)
            if (s.opaqueAt(art.frame, px, s.h - 1)) { sum += px; n++; }
        if (n > 0) {
            float poleX = sum / (float)n + 0.5f;           // from the art's left edge
            bool flipped = art.flipX != p.flipX;
            float fromCentre = (flipped ? s.w - poleX : poleX) - s.w * 0.5f;
            p.pos.x -= fromCentre;
        }
    }
    props.push_back(p);
    return true;
}

// ---------------------------------------------------------------- generation
namespace {

// Nothing hostile is placed within this many tiles of the bunker, so you always
// get a quiet walk out before anything can reach you.
constexpr int HOME_SAFE_TILES = 46;

struct Poi { int x, y, type; };
enum PoiType { POI_TOWN, POI_WAREHOUSE, POI_MILITARY, POI_FARM };

// A building that gets upper floors, built once the map has grown room for them.
struct FloorPlan { int building; int levels; int sx, sy; };   // sx, sy: its stairs' left top tile

struct Gen {
    World& W;
    Rng& rng;
    int day;
    int roadW = 2;             // road brush: 2 tiles, 3 from CITY_DAY (wider, clearer roads)
    bool cityBuild = false;    // placing a city's own buildings: its paving is not in the way
    uint8_t bkind = BK_CABIN;  // what the next building() is (0.12v)
    std::vector<FloorPlan> floorPlans;

    void setSolid(int x, int y, int s) {
        if (!W.inBounds(x, y)) return;
        Tile& t = W.at(x, y);
        if (t.solid == S_BOUNDARY || t.solid == S_BUNKER || t.solid == S_CONTAINER) return;
        t.solid = (uint8_t)s;
        t.hp = (int16_t)solidInfo(s).hp;
        if (s != S_NONE && t.ground == G_WATER) t.ground = G_DIRT;
    }

    void clearArea(int x0, int y0, int w, int h, int ground) {
        for (int y = y0; y < y0 + h; y++)
            for (int x = x0; x < x0 + w; x++) {
                if (!W.inBounds(x, y)) continue;
                Tile& t = W.at(x, y);
                if (t.solid == S_BOUNDARY) continue;
                t.solid = S_NONE;
                t.deco = 0;
                if (ground >= 0) t.ground = (uint8_t)ground;
                else if (t.ground == G_WATER) t.ground = G_DIRT;
            }
    }

    float qualityAt(int x, int y) { return W.lootQuality(x, y); }

    // Keeps roads and scenery off the bunker compound and the mechanic's yard.
    bool reserved(int tx, int ty) const {
        if (std::abs(tx - W.homeTx) <= 9 && ty - W.homeTy >= -9 && ty - W.homeTy <= 8) return true;   // the compound
        if (day >= CITY_DAY && tx >= W.homeTx + 10 && tx <= W.homeTx + 24 && ty >= W.homeTy - 8 && ty <= W.homeTy + 9) return true;
        return false;
    }

    void fillContainer(int id, float q, LootKind kind, int minItems, int maxItems) {
        Container& c = W.containers[id];
        int n = rng.irange(minItems, maxItems);
        for (int i = 0; i < n; i++) addToSlots(c.items, rollLoot(rng, q, kind));
    }

    void placeContainer(int x, int y, float q, LootKind kind) {
        int ck = CK_CRATE;
        switch (kind) {
        case LootKind::Locker: ck = CK_LOCKER; break;
        case LootKind::Cabinet: ck = CK_CABINET; break;
        case LootKind::Military: ck = CK_MILITARY; break;
        case LootKind::Toolbox: ck = CK_TOOLBOX; break;
        case LootKind::Bag: ck = CK_BAG; break;
        default: ck = CK_CRATE; break;
        }
        int id = W.addContainer(World::tileCenter(x, y), ck, x, y, (uint8_t)rng.next());
        W.at(x, y).hp = -1;
        fillContainer(id, q, kind, kind == LootKind::Military ? 2 : 1, kind == LootKind::Military ? 5 : 4);
    }

    void road(int x0, int y0, int x1, int y1) {
        int x = x0, y = y0;
        bool horizFirst = rng.chance(0.5f);
        // Wider roads (0.11v) run straight: no wandering from side to side.
        bool wobble = roadW <= 2;
        auto paint = [&](int px, int py) {
            for (int oy = 0; oy < roadW; oy++)
                for (int ox = 0; ox < roadW; ox++) {
                    int tx = px + ox, ty = py + oy;
                    if (!W.inBounds(tx, ty)) continue;
                    if (reserved(tx, ty)) continue;           // keep the compound intact
                    if (W.cityAt(tx, ty) >= 0) continue;      // a city has its own streets
                    Tile& t = W.at(tx, ty);
                    if (t.solid == S_BOUNDARY || t.solid == S_BUNKER || t.solid == S_CONTAINER || t.solid == S_ROADBLOCK) continue;
                    t.ground = t.ground == G_WATER || t.ground == G_BRIDGE ? G_BRIDGE : G_ROAD;
                    t.flags &= (uint8_t)~(TF_PLANKS | TF_TRAIL);
                    t.solid = S_NONE;
                    t.deco = 0;
                }
        };
        int jitter = 0;
        for (int pass = 0; pass < 2; pass++) {
            bool horiz = (pass == 0) == horizFirst;
            if (horiz) {
                while (x != x1) { x += x1 > x ? 1 : -1; if (rng.chance(0.08f) && wobble) jitter = clampf((float)jitter + rng.irange(-1, 1), -2, 2); paint(x, y + jitter); }
                y += jitter;
                jitter = 0;
            } else {
                while (y != y1) { y += y1 > y ? 1 : -1; if (rng.chance(0.08f) && wobble) jitter = clampf((float)jitter + rng.irange(-1, 1), -2, 2); paint(x + jitter, y); }
                x += jitter;
                jitter = 0;
            }
        }
    }

    bool areaFree(int x0, int y0, int w, int h) {
        for (int y = y0 - 1; y <= y0 + h; y++)
            for (int x = x0 - 1; x <= x0 + w; x++) {
                if (!W.inBounds(x, y)) return false;
                const Tile& t = W.at(x, y);
                if (t.solid == S_BOUNDARY || t.solid == S_BUNKER || t.solid == S_CONTAINER) return false;
                if (t.ground >= G_FLOOR_WOOD && t.ground <= G_FLOOR_TILE) return false;
                if (t.solid == S_CAR || t.solid == S_STAIRS || t.solid == S_ROADBLOCK) return false;
                if (t.ground == G_WATER || t.ground == G_BRIDGE) return false;
                if (!cityBuild && W.cityAt(x, y) >= 0) return false;
                if (reserved(x, y)) return false;
                int dx = x - W.homeTx, dy = y - W.homeTy;
                if (dx * dx + dy * dy < 16 * 16) return false;
            }
        return true;
    }

    // Returns false when the space was not available.
    bool building(int x0, int y0, int bw, int bh, int wall, int floor, LootKind kind, int minC, int maxC, float ruin, int upper = 0) {
        if (!areaFree(x0, y0, bw, bh)) return false;
        clearArea(x0 - 1, y0 - 1, bw + 2, bh + 2, cityBuild ? G_PAVEMENT : -1);
        uint8_t buildingStyle = (uint8_t)rng.irange(0, 3);
        for (int y = y0; y < y0 + bh; y++)
            for (int x = x0; x < x0 + bw; x++) {
                Tile& tile = W.at(x, y);
                tile.ground = (uint8_t)floor;
                tile.variant = buildingStyle;  // one palette/floor treatment per building
                bool edge = x == x0 || y == y0 || x == x0 + bw - 1 || y == y0 + bh - 1;
                if (edge) setSolid(x, y, wall);
            }
        // Doors (2 tiles wide): one, sometimes two, and never two in the same wall.
        // The first is usually in the front (south) wall, where it can be seen.
        std::vector<std::pair<int, int>> doors;
        std::vector<std::pair<int, int>> outerDoors;   // perimeter only, for the door art
        int sides[4] = {1, 0, 2, 3};                   // 0 north, 1 south, 2 west, 3 east
        for (int i = 3; i > 0; i--) std::swap(sides[i], sides[rng.irange(0, i)]);
        if (rng.chance(0.7f))
            for (int i = 0; i < 4; i++) if (sides[i] == 1) std::swap(sides[0], sides[i]);
        int nDoors = rng.chance(0.3f) && bw >= 6 && bh >= 6 ? 2 : 1;
        for (int i = 0; i < nDoors; i++) {
            int side = sides[i];
            int dx, dy, ox = 0, oy = 0;
            if (side < 2) { dx = rng.irange(x0 + 1, x0 + bw - 3); dy = side == 0 ? y0 : y0 + bh - 1; ox = 1; }
            else { dy = rng.irange(y0 + 1, y0 + bh - 3); dx = side == 2 ? x0 : x0 + bw - 1; oy = 1; }
            // Its style (0.12v, see Art::doorStyle): metal on sheds and army huts, the
            // plain beige ones on some town and city fronts, wood elsewhere.
            int style = bkind == BK_WAREHOUSE || bkind == BK_MILITARY ? 4
                      : (bkind == BK_CITY || bkind == BK_TOWN) && rng.chance(0.4f) ? 3 : rng.irange(0, 1);
            for (int k = 0; k < 2; k++) {
                setSolid(dx + ox * k, dy + oy * k, S_DOOR);
                W.at(dx + ox * k, dy + oy * k).variant = (uint8_t)style;
                doors.push_back({dx + ox * k, dy + oy * k});
                outerDoors.push_back({dx + ox * k, dy + oy * k});
            }
        }
        // Interior partition with a doorway.
        if (bw >= 11 && rng.chance(0.7f)) {
            int px = x0 + bw / 2 + rng.irange(-1, 1);
            int gap = rng.irange(y0 + 1, y0 + bh - 3);
            for (int y = y0 + 1; y < y0 + bh - 1; y++)
                if (y != gap && y != gap + 1) setSolid(px, y, wall);
            setSolid(px, gap, S_DOOR); setSolid(px, gap + 1, S_DOOR);
            W.at(px, gap).variant = W.at(px, gap + 1).variant = 2;   // indoors: the white ones
            doors.push_back({px, gap}); doors.push_back({px, gap + 1});
        } else if (bh >= 11 && rng.chance(0.7f)) {
            int py = y0 + bh / 2 + rng.irange(-1, 1);
            int gap = rng.irange(x0 + 1, x0 + bw - 3);
            for (int x = x0 + 1; x < x0 + bw - 1; x++)
                if (x != gap && x != gap + 1) setSolid(x, py, wall);
            setSolid(gap, py, S_DOOR); setSolid(gap + 1, py, S_DOOR);
            W.at(gap, py).variant = W.at(gap + 1, py).variant = 2;
            doors.push_back({gap, py}); doors.push_back({gap + 1, py});
        }
        // A city building with floors above (0.11v): a flight of stairs against the
        // back wall, two tiles wide, with room in front of it to stand.
        int stairX = -1;
        if (upper > 0 && bw >= 8 && bh >= 7) {
            for (int tries = 0; tries < 20 && stairX < 0; tries++) {
                int sx = rng.irange(x0 + 1, x0 + bw - 3);
                bool ok = true;
                for (int y = y0 + 1; y <= y0 + 3 && ok; y++)
                    for (int x = sx; x <= sx + 1 && ok; x++) ok = W.at(x, y).solid == S_NONE;
                for (auto& d : doors) if (std::abs(d.first - sx) <= 2 && std::abs(d.second - (y0 + 2)) <= 2) ok = false;
                if (ok) stairX = sx;
            }
            if (stairX >= 0)
                for (int y = y0 + 1; y <= y0 + 2; y++)
                    for (int x = stairX; x <= stairX + 1; x++) { W.at(x, y).solid = S_STAIRS; W.at(x, y).hp = -1; }
        }
        // Containers along interior walls, away from doors.
        float q = qualityAt(x0 + bw / 2, y0 + bh / 2);
        int want = rng.irange(minC, maxC), placed = 0;
        for (int attempt = 0; attempt < 60 && placed < want; attempt++) {
            int x = rng.irange(x0 + 1, x0 + bw - 2), y = rng.irange(y0 + 1, y0 + bh - 2);
            if (W.at(x, y).solid != S_NONE) continue;
            bool nearWall = W.at(x - 1, y).solid == wall || W.at(x + 1, y).solid == wall || W.at(x, y - 1).solid == wall || W.at(x, y + 1).solid == wall;
            if (!nearWall) continue;
            bool nearDoor = false;
            for (auto& d : doors) if (std::abs(d.first - x) <= 2 && std::abs(d.second - y) <= 2) nearDoor = true;
            if (stairX >= 0 && x >= stairX - 1 && x <= stairX + 2 && y <= y0 + 4) nearDoor = true;   // the way to the stairs
            if (nearDoor) continue;
            LootKind k = kind;
            if (kind == LootKind::Generic) {
                int r = rng.irange(0, 3);
                k = r == 0 ? LootKind::Cabinet : r == 1 ? LootKind::Locker : r == 2 ? LootKind::Crate : LootKind::Toolbox;
            }
            placeContainer(x, y, q, k);
            placed++;
        }
        // Ruined walls.
        if (ruin > 0) {
            for (int y = y0; y < y0 + bh; y++)
                for (int x = x0; x < x0 + bw; x++)
                    // A gap in a ruined wall shows the building's floor, like a shot-out one.
                    if (W.at(x, y).solid == wall && rng.chance(ruin)) W.at(x, y).solid = S_NONE;
        }
        int standing = 0;
        for (int y = y0; y < y0 + bh; y++)
            for (int x = x0; x < x0 + bw; x++)
                if ((x == x0 || y == y0 || x == x0 + bw - 1 || y == y0 + bh - 1) && W.at(x, y).solid != S_NONE) standing++;
        Building b;
        b.x0 = x0;
        b.y0 = y0;
        b.w = bw;
        b.h = bh;
        b.style = (uint8_t)(buildingStyle & 1);  // roof sheet has two matching styles
        b.sheet = buildingStyle;                  // and the facade sheet it was built from
        b.kind = bkind;
        b.flatRoof = bkind == BK_CITY;
        b.walls = standing;
        for (auto& d : outerDoors) {
            if (b.doorCount >= Building::MAX_DOORS) break;
            b.doorX[b.doorCount] = (int16_t)d.first;
            b.doorY[b.doorCount] = (int16_t)d.second;
            b.doorCount++;
        }
        W.buildings.push_back(b);
        if (stairX >= 0) floorPlans.push_back({(int)W.buildings.size() - 1, upper, stairX, y0 + 1});
        return true;
    }

    void town(int cx, int cy) {
        bkind = BK_TOWN;
        int n = rng.irange(3, 6);
        for (int i = 0; i < n; i++) {
            int bw = rng.irange(7, 12), bh = rng.irange(6, 10);
            int x = cx + rng.irange(-16, 16) - bw / 2, y = cy + rng.irange(-16, 16) - bh / 2;
            // Intact town buildings use the dedicated facade sheets. Exposed brick
            // is reserved for the ruined/abandoned structures it suits.
            float ruin = rng.chance(0.3f) ? 0.12f : 0.0f;
            int wall = ruin > 0.08f ? S_WALL_BRICK : (rng.chance(0.55f) ? S_WALL_WOOD : S_WALL_CONCRETE);
            int floor = rng.chance(0.6f) ? G_FLOOR_WOOD : G_FLOOR_TILE;
            building(x, y, bw, bh, wall, floor, LootKind::Generic, 1, 3, ruin);
        }
        spawnGroup(cx, cy, rng.irange(3, 4), 14);
    }

    void warehouse(int cx, int cy) {
        bkind = BK_WAREHOUSE;
        int bw = rng.irange(14, 20), bh = rng.irange(10, 14);
        building(cx - bw / 2, cy - bh / 2, bw, bh, S_WALL_CONCRETE, G_FLOOR_CONCRETE, LootKind::Crate, 4, 7, 0.03f);
        for (int i = 0; i < 12; i++) {
            int x = cx + rng.irange(-bw, bw), y = cy + rng.irange(-bh, bh);
            if (W.inBounds(x, y) && W.at(x, y).solid == S_NONE && W.at(x, y).ground != G_WATER &&
                !(x >= cx - bw / 2 - 1 && x <= cx + bw / 2 + 1 && y >= cy - bh / 2 - 1 && y <= cy + bh / 2 + 1))
                setSolid(x, y, S_CRATE);
        }
        spawnGroup(cx, cy, rng.irange(3, 5), 12);
    }

    void military(int cx, int cy) {
        bkind = BK_MILITARY;
        int r = rng.irange(9, 12);
        clearArea(cx - r, cy - r, r * 2 + 1, r * 2 + 1, G_DIRT);
        for (int y = cy - r; y <= cy + r; y++)
            for (int x = cx - r; x <= cx + r; x++) {
                bool edge = x == cx - r || x == cx + r || y == cy - r || y == cy + r;
                bool gate = (std::abs(x - cx) <= 1 && (y == cy - r || y == cy + r)) || (std::abs(y - cy) <= 1 && (x == cx - r || x == cx + r));
                if (edge && !gate) setSolid(x, y, S_SANDBAG);
            }
        // Tents / bunkers inside.
        building(cx - 5, cy - 4, 5, 4, S_WALL_WOOD, G_FLOOR_WOOD, LootKind::Military, 1, 2, 0);
        building(cx + 1, cy + 1, 5, 4, S_WALL_WOOD, G_FLOOR_WOOD, LootKind::Military, 1, 2, 0);
        for (int i = 0; i < 4; i++) {
            int x = cx + rng.irange(-r + 2, r - 2), y = cy + rng.irange(-r + 2, r - 2);
            if (W.at(x, y).solid == S_NONE) placeContainer(x, y, qualityAt(cx, cy) + 0.15f, LootKind::Military);
        }
        spawnGroup(cx, cy, rng.irange(4, 6), 8, true);
    }

    void farm(int cx, int cy) {
        bkind = BK_FARM;
        int fw = rng.irange(16, 22), fh = rng.irange(12, 18);
        clearArea(cx - fw / 2, cy - fh / 2, fw, fh, G_DIRT);
        for (int x = cx - fw / 2; x < cx + fw / 2; x++) {
            if (rng.chance(0.85f)) setSolid(x, cy - fh / 2, S_FENCE);
            if (rng.chance(0.85f)) setSolid(x, cy + fh / 2 - 1, S_FENCE);
        }
        for (int y = cy - fh / 2; y < cy + fh / 2; y++) {
            if (rng.chance(0.85f)) setSolid(cx - fw / 2, y, S_FENCE);
            if (rng.chance(0.85f)) setSolid(cx + fw / 2 - 1, y, S_FENCE);
        }
        building(cx - 4, cy - 3, rng.irange(7, 9), rng.irange(6, 7), S_WALL_WOOD, G_FLOOR_WOOD, LootKind::Generic, 1, 3, 0.05f);
        spawnGroup(cx, cy, rng.irange(2, 3), 10);
    }


    // ---- cities (0.11v) ------------------------------------------------------------
    // A wreck from the vehicle pack standing on (cx, cy) (its middle), facing `dir`
    // (0..7, east then clockwise). Its tiles are reserved; its pixels do the blocking.
    bool wreck(int cx, int cy, int dir, uint8_t variant) {
        Art::Piece art = Art::wreck(variant, dir);
        if (!art.valid()) return false;
        const Assets::Sprite& s = *art.sprite;
        Vec2 mid = World::tileCenter(cx, cy);
        Vec2 base(std::floor(mid.x), std::floor(mid.y + s.h * 0.5f));
        int x0 = World::toTile(base.x - s.w * 0.5f), x1 = World::toTile(base.x + s.w * 0.5f - 0.01f);
        int y0 = World::toTile(base.y - s.h), y1 = World::toTile(base.y - 0.01f);
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                if (!W.inBounds(x, y) || reserved(x, y)) return false;
                const Tile& t = W.at(x, y);
                if (t.solid != S_NONE || t.ground == G_WATER || (t.ground >= G_FLOOR_WOOD && t.ground <= G_FLOOR_TILE)) return false;
            }
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) { Tile& t = W.at(x, y); t.solid = S_CAR; t.hp = -1; t.worldDeco = 0; }
        WorldProp prop;
        prop.kind = PROP_WRECK;
        prop.variant = variant;
        prop.frame = (uint8_t)dir;
        prop.pos = base;
        W.props.push_back(prop);
        return true;
    }

    void streetLight(int x, int y, int variant, bool flip) { W.placeStreetLight(x, y, variant, flip); }

    // A grid of streets (four lanes of asphalt) round blocks of paved ground; most
    // blocks hold buildings, some two or three floors high, the rest are car parks,
    // squares or ruins. Gangs hold the buildings and walk the streets.
    void city(int cx0, int cy0, int cw, int ch) {
        const int STREET = 4;
        CityZone zone;
        zone.x0 = cx0; zone.y0 = cy0; zone.w = cw; zone.h = ch;
        zone.street = STREET;
        // Some cities are kept, some the grass has half taken back (0.12v).
        zone.overgrowth = (hash2(cx0, cy0, (uint32_t)W.seed ^ 0x06E7u) & 0xFFFF) / 65535.0f;
        W.cities.push_back(zone);
        bkind = BK_CITY;
        // The outskirts (0.12v): a ragged belt of dead scrub round the city, thinned of
        // trees, so the country does not stop dead at the first kerb.
        for (int y = cy0 - 16; y < cy0 + ch + 16; y++)
            for (int x = cx0 - 16; x < cx0 + cw + 16; x++) {
                if (!W.inBounds(x, y) || (x >= cx0 && y >= cy0 && x < cx0 + cw && y < cy0 + ch)) continue;
                int dx = std::max({cx0 - x, x - (cx0 + cw - 1), 0}), dy = std::max({cy0 - y, y - (cy0 + ch - 1), 0});
                float d = std::sqrt(float(dx * dx + dy * dy));
                float reach = 7 + 9 * fbm(x * 0.11f, y * 0.11f, (uint32_t)W.seed ^ 0xB1EAu);
                if (d > reach) continue;
                Tile& t = W.at(x, y);
                if (t.solid == S_BOUNDARY || reserved(x, y)) continue;
                if (t.ground == G_GRASS || t.ground == G_SAND) t.ground = G_WASTE;
                if ((t.solid == S_TREE || t.solid == S_BUSH) && (hash2(x, y, 0x7E3Du) & 3) != 0) { t.solid = S_NONE; t.hp = 0; }
            }
        // Everything the countryside put here goes: paving over all of it.
        for (int y = cy0; y < cy0 + ch; y++)
            for (int x = cx0; x < cx0 + cw; x++) {
                if (!W.inBounds(x, y)) continue;
                Tile& t = W.at(x, y);
                if (t.solid == S_BOUNDARY) continue;
                t.solid = S_NONE; t.hp = 0; t.deco = 0; t.worldDeco = 0;
                t.ground = G_PAVEMENT;
            }
        // Street lines across and down: the edges, then every block's width.
        std::vector<int> xs, ys;
        for (int x = cx0; x + STREET <= cx0 + cw;) {
            xs.push_back(x);
            int left = cx0 + cw - (x + STREET);
            if (left < 18 + STREET) break;
            int bw = rng.irange(18, 26);
            if (left - STREET - bw < 18 + STREET) bw = left - STREET;   // the last block takes the rest
            x += STREET + bw;
        }
        if (xs.back() + STREET < cx0 + cw) xs.push_back(cx0 + cw - STREET);
        for (int y = cy0; y + STREET <= cy0 + ch;) {
            ys.push_back(y);
            int left = cy0 + ch - (y + STREET);
            if (left < 15 + STREET) break;
            int bh = rng.irange(15, 21);
            if (left - STREET - bh < 15 + STREET) bh = left - STREET;
            y += STREET + bh;
        }
        if (ys.back() + STREET < cy0 + ch) ys.push_back(cy0 + ch - STREET);
        W.cities.back().streetX = xs;
        W.cities.back().streetY = ys;
        auto asphalt = [&](int x, int y) {
            if (!W.inBounds(x, y)) return;
            Tile& t = W.at(x, y);
            if (t.solid == S_BOUNDARY) return;
            t.ground = G_ROAD;
        };
        for (int sx : xs)
            for (int y = cy0; y < cy0 + ch; y++)
                for (int k = 0; k < STREET; k++) asphalt(sx + k, y);
        for (int sy : ys)
            for (int x = cx0; x < cx0 + cw; x++)
                for (int k = 0; k < STREET; k++) asphalt(x, sy + k);
        // The blocks between.
        cityBuild = true;
        for (size_t by = 0; by + 1 < ys.size(); by++)
            for (size_t bx = 0; bx + 1 < xs.size(); bx++) {
                int x0 = xs[bx] + STREET, y0 = ys[by] + STREET;
                int bw = xs[bx + 1] - x0, bh = ys[by + 1] - y0;
                if (bw < 8 || bh < 8) continue;
                cityBlock(x0, y0, bw, bh);
            }
        cityBuild = false;
        // Wrecks along the streets, parked against the kerb or left across a lane.
        int wantWrecks = (cw * ch) / 260;
        for (int i = 0, tries = 0; i < wantWrecks && tries < wantWrecks * 8; tries++) {
            int x, y, dir;
            if (rng.chance(0.5f)) {   // on a street running left-right
                int sy = ys[rng.next() % ys.size()];
                x = rng.irange(cx0 + 2, cx0 + cw - 3);
                y = sy + (rng.chance(0.5f) ? 1 : 2);
                dir = rng.chance(0.5f) ? 0 : 4;
            } else {
                int sx = xs[rng.next() % xs.size()];
                x = sx + (rng.chance(0.5f) ? 1 : 2);
                y = rng.irange(cy0 + 2, cy0 + ch - 3);
                dir = rng.chance(0.5f) ? 2 : 6;
            }
            if (rng.chance(0.2f)) dir = rng.irange(0, 7);
            if (wreck(x, y, dir, (uint8_t)rng.next())) i++;
        }
        // Street lights on the corners.
        for (size_t j = 0; j < ys.size(); j++)
            for (size_t i = 0; i < xs.size(); i++)
                if (rng.chance(0.55f)) streetLight(xs[i] + STREET, ys[j] + STREET, 0, false);
        // Gangs walking the streets: each one patrols a loop round a block or two.
        int blocks = (int)((xs.size() - 1) * (ys.size() - 1));
        int nPatrols = std::max(2, blocks / 4) + day / 6;
        for (int k = 0; k < nPatrols; k++) {
            int bx = rng.irange(0, (int)xs.size() - 2), by = rng.irange(0, (int)ys.size() - 2);
            int bx2 = std::min((int)xs.size() - 1, bx + rng.irange(1, 2)), by2 = std::min((int)ys.size() - 1, by + rng.irange(1, 2));
            Vec2 off(STREET * 0.5f * TILE - 8, STREET * 0.5f * TILE - 8);   // the middle of the street
            PatrolRoute route;
            route.points = {World::tileCenter(xs[bx], ys[by]) + off, World::tileCenter(xs[bx2], ys[by]) + off,
                            World::tileCenter(xs[bx2], ys[by2]) + off, World::tileCenter(xs[bx], ys[by2]) + off};
            if (rng.chance(0.5f)) std::reverse(route.points.begin(), route.points.end());
            std::rotate(route.points.begin(), route.points.begin() + rng.irange(0, 3), route.points.end());
            W.patrols.push_back(route);
            int id = (int)W.patrols.size() - 1;
            int members = rng.irange(3, 5) + day / 8;
            Vec2 at = route.points[0];
            Vec2 back = normalize(route.points[0] - route.points[1]);
            for (int m = 0; m < members; m++) {
                Vec2 pos = at + back * (float)(m * 14) + Vec2(rng.range(-5, 5), rng.range(-5, 5));
                if (W.blocksMove(World::toTile(pos.x), World::toTile(pos.y))) pos = at;
                float r = rng.f();
                EnemyType t = r < 0.18f ? EnemyType::Heavy : r < 0.28f ? EnemyType::Sniper : EnemyType::Bandit;
                W.spawns.push_back({pos, t, false, id});
            }
        }
    }

    void cityBlock(int x0, int y0, int bw, int bh) {
        CityBlock blk;
        blk.x0 = x0; blk.y0 = y0; blk.w = bw; blk.h = bh;
        blk.city = (int)W.cities.size() - 1;
        float r = rng.f();
        // A sidewalk two tiles wide round every block (0.12v), kerbed against the street.
        const int SW = 2;
        int ix = x0 + SW, iy = y0 + SW, iw = bw - 2 * SW, ih = bh - 2 * SW;
        auto inner = [&](int x, int y) { return x >= ix && y >= iy && x < ix + iw && y < iy + ih; };
        // Grass on the block: a kerbed lawn, or wild grass that took the paving back.
        auto green = [&](int x, int y, bool kerb) {
            Tile& t = W.at(x, y);
            if (t.solid != S_NONE && t.solid != S_TREE && t.solid != S_BUSH) return;
            t.ground = G_GRASS;
            t.flags = kerb ? (uint8_t)(t.flags | TF_KERB) : (uint8_t)(t.flags & ~TF_KERB);
        };
        if (r < 0.56f) {
            blk.kind = BLK_BUILDINGS;
            size_t firstB = W.buildings.size();
            // Buildings: one big one, or two or three side by side.
            int n = iw >= 30 ? rng.irange(1, 3) : iw >= 18 ? rng.irange(1, 2) : 1;
            int slot = iw / n;
            for (int i = 0; i < n; i++) {
                int w = std::min(slot - 2, rng.irange(9, 16));
                int h = std::min(ih - 2, rng.irange(8, 13));
                if (w < 7 || h < 7) continue;
                int x = ix + 1 + i * slot + rng.irange(0, std::max(0, slot - w - 2));
                int y = iy + 1 + rng.irange(0, std::max(0, ih - h - 2));
                int upper = w >= 9 && h >= 8 ? (rng.chance(0.55f) ? rng.irange(1, 2) : 0) : 0;
                float ruin = rng.chance(0.25f) ? 0.07f : 0.0f;
                int wall = ruin > 0 ? S_WALL_BRICK : (rng.chance(0.7f) ? S_WALL_CONCRETE : S_WALL_WOOD);
                int floor = rng.chance(0.5f) ? G_FLOOR_CONCRETE : (rng.chance(0.5f) ? G_FLOOR_TILE : G_FLOOR_WOOD);
                LootKind k = rng.chance(0.2f) ? LootKind::Military : rng.chance(0.3f) ? LootKind::Locker : LootKind::Generic;
                if (building(x, y, w, h, wall, floor, k, 2, 4, ruin, upper))
                    spawnGroup(x + w / 2, y + h / 2, rng.irange(2, 4), std::max(3, std::min(w, h) / 2 - 1), rng.chance(0.3f));
            }
            // What is left of the block round them: lawns, wild grass, or paving. A
            // paved apron stays round each building and a path runs from every door
            // out to the sidewalk.
            float yr = rng.f();
            blk.yard = yr < 0.45f ? 1 : yr < 0.75f ? 2 : 0;
            if (blk.yard) {
                auto apron = [&](int x, int y) {
                    for (size_t bi = firstB; bi < W.buildings.size(); bi++) {
                        const Building& b = W.buildings[bi];
                        if (x >= b.x0 - 1 && y >= b.y0 - 1 && x <= b.x0 + b.w && y <= b.y0 + b.h) return true;
                    }
                    return false;
                };
                for (int y = iy; y < iy + ih; y++)
                    for (int x = ix; x < ix + iw; x++)
                        if (!apron(x, y) && W.at(x, y).ground == G_PAVEMENT) green(x, y, blk.yard == 1);
                for (size_t bi = firstB; bi < W.buildings.size(); bi++) {
                    const Building& b = W.buildings[bi];
                    for (int d = 0; d < b.doorCount; d++) {
                        int dx = b.doorX[d], dy = b.doorY[d];
                        int sx = dx == b.x0 ? -1 : dx == b.x0 + b.w - 1 ? 1 : 0;
                        int sy = dy == b.y0 ? -1 : dy == b.y0 + b.h - 1 ? 1 : 0;
                        if (sx && sy) sy = 0;
                        for (int x = dx + sx, y = dy + sy; inner(x, y); x += sx, y += sy) {
                            Tile& t = W.at(x, y);
                            if (t.ground != G_GRASS) { if (t.ground == G_PAVEMENT) continue; break; }
                            t.ground = G_PAVEMENT;
                            t.flags &= (uint8_t)~TF_KERB;
                            if (t.solid == S_TREE || t.solid == S_BUSH) { t.solid = S_NONE; t.hp = 0; }
                        }
                    }
                }
            }
        } else if (r < 0.68f) {
            blk.kind = BLK_PARKING;
            // A car park: asphalt with the dead cars still in their bays.
            for (int y = iy; y < iy + ih; y++)
                for (int x = ix; x < ix + iw; x++) W.at(x, y).ground = G_ROAD;
            for (int y = iy + 2; y + 2 < iy + ih; y += 5)
                for (int x = ix + 2; x + 2 < ix + iw; x += 3)
                    if (rng.chance(0.45f)) wreck(x, y, rng.chance(0.5f) ? 2 : 6, (uint8_t)rng.next());
            for (int i = 0; i < 3; i++) {
                int x = rng.irange(ix + 1, ix + iw - 2), y = rng.irange(iy + 1, iy + ih - 2);
                if (W.at(x, y).solid == S_NONE) placeContainer(x, y, qualityAt(x, y), rng.chance(0.4f) ? LootKind::Toolbox : LootKind::Bag);
            }
            spawnGroup(ix + iw / 2, iy + ih / 2, rng.irange(1, 3), std::min(iw, ih) / 2, false, false);
        } else if (r < 0.82f) {
            blk.kind = BLK_PARK;
            // A park: lawns (kept or run wild) crossed by paved paths, with trees and
            // shrubs; the dressing adds its benches and lamps.
            bool kept = rng.chance(0.65f);
            blk.yard = kept ? 1 : 2;
            int px = ix + iw / 2 - 1 + rng.irange(-2, 2), py = iy + ih / 2 - 1 + rng.irange(-2, 2);
            for (int y = iy; y < iy + ih; y++)
                for (int x = ix; x < ix + iw; x++) {
                    bool path = (x >= px && x <= px + 1) || (y >= py && y <= py + 1);
                    if (!path) green(x, y, kept);
                }
            // A kept park is often railed in wrought iron (0.12v), open where the paths run out.
            if (kept && rng.chance(0.7f))
                for (int y = iy; y < iy + ih; y++)
                    for (int x = ix; x < ix + iw; x++) {
                        bool edge = x == ix || y == iy || x == ix + iw - 1 || y == iy + ih - 1;
                        bool path = (x >= px && x <= px + 1) || (y >= py && y <= py + 1);
                        if (!edge || path || W.at(x, y).solid != S_NONE) continue;
                        setSolid(x, y, S_FENCE);
                        W.at(x, y).flags |= TF_IRON;
                    }
            int want = iw * ih / (kept ? 16 : 9);
            for (int i = 0; i < want; i++) {
                int x = rng.irange(ix, ix + iw - 1), y = rng.irange(iy, iy + ih - 1);
                const Tile& t = W.at(x, y);
                if (t.ground != G_GRASS || t.solid != S_NONE) continue;
                // Keep a tile of lawn clear along the paths, for the benches.
                if (std::abs(x - px) <= 2 || std::abs(x - px - 1) <= 2 || std::abs(y - py) <= 2 || std::abs(y - py - 1) <= 2) continue;
                setSolid(x, y, rng.chance(0.7f) ? S_TREE : S_BUSH);
            }
        } else if (r < 0.92f) {
            blk.kind = BLK_RUIN;
            // What is left of a block: rubble, broken walls, crates and a gang's camp,
            // with the scrub coming through.
            for (int y = iy; y < iy + ih; y++)
                for (int x = ix; x < ix + iw; x++) {
                    float n = fbm(x * 0.21f, y * 0.21f, (uint32_t)W.seed ^ 0x5C4Bu);
                    if (n > 0.58f) W.at(x, y).ground = G_WASTE;
                    else if (rng.chance(0.6f)) W.at(x, y).ground = G_RUBBLE;
                }
            for (int i = 0; i < 4; i++) {
                int x = rng.irange(ix + 1, ix + iw - 4), y = rng.irange(iy + 1, iy + ih - 2);
                int len = rng.irange(2, 5);
                bool horiz = rng.chance(0.5f);
                for (int k = 0; k < len; k++) {
                    int tx = horiz ? x + k : x, ty = horiz ? y : y + k;
                    if (tx < ix + iw && ty < iy + ih && W.at(tx, ty).solid == S_NONE) setSolid(tx, ty, S_WALL_BRICK);
                }
            }
            for (int i = 0; i < 6; i++) {
                int x = rng.irange(ix, ix + iw - 1), y = rng.irange(iy, iy + ih - 1);
                if (W.at(x, y).solid == S_NONE) setSolid(x, y, rng.chance(0.5f) ? S_CRATE : S_SANDBAG);
            }
            for (int i = 0; i < 2; i++) {
                int x = rng.irange(ix + 1, ix + iw - 2), y = rng.irange(iy + 1, iy + ih - 2);
                if (W.at(x, y).solid == S_NONE) placeContainer(x, y, qualityAt(x, y) + 0.1f, LootKind::Military);
            }
            spawnGroup(ix + iw / 2, iy + ih / 2, rng.irange(3, 5), std::min(iw, ih) / 2, true);
        } else {
            blk.kind = BLK_DEPOT;
            // A depot: a fenced yard of bare earth and asphalt where the shipping
            // containers were left (the dressing stacks them), a gate on one side.
            for (int y = iy; y < iy + ih; y++)
                for (int x = ix; x < ix + iw; x++)
                    W.at(x, y).ground = fbm(x * 0.17f, y * 0.17f, (uint32_t)W.seed ^ 0xDE90u) > 0.55f ? G_DIRT : G_ROAD;
            int gateSide = rng.irange(0, 3), gate = gateSide < 2 ? ix + iw / 2 - 1 : iy + ih / 2 - 1;
            for (int y = iy; y < iy + ih; y++)
                for (int x = ix; x < ix + iw; x++) {
                    int side = y == iy ? 0 : y == iy + ih - 1 ? 1 : x == ix ? 2 : x == ix + iw - 1 ? 3 : -1;
                    if (side < 0) continue;
                    int along = side < 2 ? x : y;
                    // The way in (0.12v): wire gates when it is in a side that runs across.
                    if (side == gateSide && along >= gate && along <= gate + 2) {
                        if (side < 2) { setSolid(x, y, S_FENCE_GATE); W.at(x, y).variant = along == gate + 1 ? 1 : 0; }
                        continue;
                    }
                    setSolid(x, y, S_FENCE);
                }
            for (int i = 0; i < 3; i++) {
                int x = rng.irange(ix + 2, ix + iw - 3), y = rng.irange(iy + 2, iy + ih - 3);
                if (W.at(x, y).solid == S_NONE) placeContainer(x, y, qualityAt(x, y) + 0.05f, rng.chance(0.5f) ? LootKind::Crate : LootKind::Toolbox);
            }
            spawnGroup(ix + iw / 2, iy + ih / 2, rng.irange(2, 4), std::min(iw, ih) / 2 - 1);
        }
        W.blocks.push_back(blk);
    }

    // The mechanic's yard, just east of the bunker compound: a paved lot, his open
    // workshop, and a bay out front where the car he sells you waits.
    void garage() {
        int hx = W.homeTx, hy = W.homeTy;
        for (int y = hy - 8; y <= hy + 9; y++)
            for (int x = hx + 10; x <= hx + 24; x++) {
                if (!W.inBounds(x, y)) continue;
                Tile& t = W.at(x, y);
                if (t.solid == S_BOUNDARY) continue;
                if (t.solid == S_CONTAINER) continue;   // (never: the yard is kept clear)
                t.solid = S_NONE; t.hp = 0; t.deco = 0; t.worldDeco = 0;
                t.ground = G_PAVEMENT;
            }
        // The workshop: three walls and a roof, open to the south.
        int x0 = hx + 13, y0 = hy - 7, w = 9, h = 6;
        uint8_t style = (uint8_t)rng.irange(0, 3);
        for (int y = y0; y < y0 + h; y++)
            for (int x = x0; x < x0 + w; x++) {
                Tile& t = W.at(x, y);
                t.ground = G_FLOOR_CONCRETE;
                t.variant = style;
                bool edge = x == x0 || y == y0 || x == x0 + w - 1 || (y == y0 + h - 1 && (x < x0 + 2 || x > x0 + w - 3));
                if (edge) { t.solid = S_WALL_CONCRETE; t.hp = -1; }
            }
        Building b;
        b.x0 = x0; b.y0 = y0; b.w = w; b.h = h;
        b.style = (uint8_t)(style & 1);
        b.sheet = style;
        b.kind = BK_GARAGE;
        for (int x = x0 + 2; x <= x0 + w - 3 && b.doorCount < Building::MAX_DOORS; x++) {
            b.doorX[b.doorCount] = (int16_t)x;
            b.doorY[b.doorCount] = (int16_t)(y0 + h - 1);
            b.doorCount++;
        }
        for (int y = y0; y < y0 + h; y++)
            for (int x = x0; x < x0 + w; x++)
                if ((x == x0 || y == y0 || x == x0 + w - 1 || y == y0 + h - 1) && W.at(x, y).solid != S_NONE) b.walls++;
        W.buildings.push_back(b);
        // Oil drums along its back wall.
        for (int x : {x0 + 1, x0 + 2, x0 + w - 2}) {
            Tile& t = W.at(x, y0 + 1);
            t.solid = S_CRATE; t.hp = -1; t.variant = (uint8_t)rng.next();
        }
        W.props.push_back({Vec2((x0 + w * 0.5f) * TILE, (float)(y0 + h) * TILE - 2), PROP_GARAGE, 0, false});
        // A lamp by the bay.
        streetLight(hx + 23, hy + 2, 0, true);
    }

    EnemyType pickType(int x, int y, bool military) {
        float q = qualityAt(x, y);
        float r = rng.f();
        if (military) {
            if (r < 0.15f + q * 0.1f) return EnemyType::Sniper;
            if (r < 0.40f + q * 0.1f) return EnemyType::Heavy;
            return EnemyType::Bandit;
        }
        if (r < 0.05f + q * 0.1f) return EnemyType::Heavy;
        if (r < 0.08f + q * 0.15f) return EnemyType::Sniper;
        if (r < 0.35f + q * 0.35f) return EnemyType::Bandit;
        return EnemyType::Scav;
    }

    // ---- the island (0.13v) ----------------------------------------------------------
    bool water(int x, int y) const { return W.inBounds(x, y) && W.at(x, y).ground == G_WATER; }
    bool nearWater(int cx, int cy, int r) const {
        for (int y = cy - r; y <= cy + r; y++)
            for (int x = cx - r; x <= cx + r; x++)
                if (!W.inBounds(x, y) || W.at(x, y).ground == G_WATER) return true;
        return false;
    }
    // Where a tile may be dug out for a river: not the compound, not near the hatch.
    bool keepDry(int x, int y) const {
        int dx = x - W.homeTx, dy = y - W.homeTy;
        return reserved(x, y) || dx * dx + dy * dy < 30 * 30 || W.at(x, y).solid == S_BOUNDARY;
    }

    // A river: it wanders from (x, y) on `dir` until it runs into the sea (or `len` steps),
    // `width` tiles across give or take. Returns the points it passed, for the bridges.
    std::vector<Vec2> river(float x, float y, float dir, int len, float width, uint32_t salt) {
        std::vector<Vec2> path;
        float heading = dir;
        bool leftLand = false;
        for (int i = 0; i < len; i++) {
            // Bends from noise along its length, pulled back towards its general heading.
            float bend = (fbm(i * 0.035f, 0.5f, salt) - 0.5f) * 2.2f;
            heading += (dir + bend - heading) * 0.08f;
            x += std::cos(heading);
            y += std::sin(heading);
            int tx = (int)x, ty = (int)y;
            if (!W.inBounds(tx, ty)) break;
            bool sea = W.at(tx, ty).ground == G_WATER && regionAtTile(tx, ty) < 0;
            if (!sea) leftLand = true;
            else if (leftLand) break;   // out into the sea again
            path.push_back(Vec2(x, y));
            float r = width * 0.5f * (0.8f + 0.5f * fbm(i * 0.08f, 3.5f, salt ^ 0x51u));
            int ir = (int)std::ceil(r);
            for (int oy = -ir; oy <= ir; oy++)
                for (int ox = -ir; ox <= ir; ox++) {
                    if (ox * ox + oy * oy > r * r + 0.3f) continue;
                    int px = tx + ox, py = ty + oy;
                    if (!W.inBounds(px, py) || keepDry(px, py)) continue;
                    Tile& t = W.at(px, py);
                    t.ground = G_WATER;
                    t.solid = S_NONE;
                    t.hp = 0;
                    t.worldDeco = 0;
                }
        }
        return path;
    }
    // Wooden footbridges over a river every so often, square across its flow.
    void footbridges(const std::vector<Vec2>& path, int every) {
        for (size_t i = (size_t)every / 2; i + 3 < path.size(); i += (size_t)every) {
            Vec2 d = path[i + 3] - path[i >= 3 ? i - 3 : 0];
            bool flowsAcross = std::fabs(d.x) > std::fabs(d.y);   // east-west: the bridge runs north-south
            int cx = (int)path[i].x, cy = (int)path[i].y;
            int ax = flowsAcross ? 0 : 1, ay = flowsAcross ? 1 : 0;
            int a = 0, b = 0;
            while (a < 12 && water(cx - ax * (a + 1), cy - ay * (a + 1))) a++;
            while (b < 12 && water(cx + ax * (b + 1), cy + ay * (b + 1))) b++;
            if (a >= 12 || b >= 12) continue;
            if (!W.inBounds(cx - ax * (a + 1), cy - ay * (a + 1)) || !W.inBounds(cx + ax * (b + 1), cy + ay * (b + 1))) continue;
            for (int k = -a; k <= b; k++)
                for (int s = 0; s < 2; s++) {
                    int x = cx + ax * k + ay * s, y = cy + ay * k + ax * s;
                    if (!water(x, y)) continue;
                    Tile& t = W.at(x, y);
                    t.ground = G_BRIDGE;
                    t.flags |= TF_PLANKS;
                }
        }
    }

    // A closed bridge: a roadblock across it, two tiles thick, where the water is widest.
    void roadblock(int x0, int y0, int x1, int y1, int openDay) {
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                if (!W.inBounds(x, y) || W.at(x, y).ground != G_BRIDGE) continue;
                Tile& t = W.at(x, y);
                t.solid = S_ROADBLOCK;
                t.hp = -1;
                t.variant = (uint8_t)openDay;
            }
    }

    // A forest trail (0.13v): a winding strip of worn earth from (x, y) into the trees,
    // `len` steps long. Returns where it ended.
    std::pair<int, int> trail(float x, float y, float dir, int len, int wide, const Region& in) {
        float heading = dir;
        for (int i = 0; i < len; i++) {
            heading += rng.range(-0.28f, 0.28f);
            heading += (dir - heading) * 0.05f;
            float nx = x + std::cos(heading), ny = y + std::sin(heading);
            int tx = (int)nx, ty = (int)ny;
            if (!in.contains(tx, ty) || !in.contains(tx + 3, ty + 3) || !in.contains(tx - 3, ty - 3) || water(tx, ty)) {
                heading += PI * 0.5f;   // turn off the coast
                continue;
            }
            x = nx; y = ny;
            for (int oy = 0; oy < wide; oy++)
                for (int ox = 0; ox < wide; ox++) {
                    int px = tx + ox, py = ty + oy;
                    if (!W.inBounds(px, py)) continue;
                    Tile& t = W.at(px, py);
                    if (t.ground == G_ROAD || t.ground == G_BRIDGE || t.ground == G_WATER) continue;
                    if (t.solid == S_TREE || t.solid == S_BUSH) { t.solid = S_NONE; t.hp = 0; }
                    if (t.solid != S_NONE) continue;
                    t.ground = G_DIRT;
                    t.worldDeco = 0;
                    t.flags |= TF_TRAIL;
                }
        }
        return {(int)x, (int)y};
    }
    // A clearing in the trees, round (cx, cy), with something in it: a cabin, a camp, a
    // hunter's hide, or nothing but the dead that wander there.
    void clearing(int cx, int cy, int r) {
        for (int y = cy - r; y <= cy + r; y++)
            for (int x = cx - r; x <= cx + r; x++) {
                if (!W.inBounds(x, y)) continue;
                float d = std::sqrt(float((x - cx) * (x - cx) + (y - cy) * (y - cy)));
                if (d > r - rng.f() * 2.0f) continue;
                Tile& t = W.at(x, y);
                if (t.ground == G_WATER || t.ground == G_ROAD || t.ground == G_BRIDGE) continue;
                if (t.solid == S_TREE || t.solid == S_BUSH) { t.solid = S_NONE; t.hp = 0; }
                if (t.ground == G_DIRT && !(t.flags & TF_TRAIL) && rng.chance(0.5f)) t.ground = G_GRASS;
            }
        float what = rng.f();
        bkind = BK_CABIN;
        if (what < 0.45f) {
            int bw = rng.irange(5, 8), bh = rng.irange(5, 7);
            float ruin = rng.chance(0.4f) ? 0.15f : 0;
            building(cx - bw / 2, cy - bh / 2, bw, bh, ruin > 0 ? S_WALL_BRICK : S_WALL_WOOD, G_FLOOR_WOOD, LootKind::Generic, 1, 3, ruin);
        } else if (what < 0.75f) {
            // A camp someone left in a hurry: their bags and a crate or two.
            for (int i = 0; i < rng.irange(2, 4); i++) {
                int x = cx + rng.irange(-r / 2, r / 2), y = cy + rng.irange(-r / 2, r / 2);
                if (!W.inBounds(x, y) || W.blocksMove(x, y) || W.at(x, y).container >= 0) continue;
                if (rng.chance(0.6f)) {
                    int id = W.addContainer(World::tileCenter(x, y), CK_BAG, -1, -1, (uint8_t)rng.next());
                    fillContainer(id, qualityAt(x, y), LootKind::Bag, 1, 3);
                } else {
                    placeContainer(x, y, qualityAt(x, y), LootKind::Crate);
                }
            }
        } else if (what < 0.88f) {
            bkind = BK_CABIN;
            building(cx - 2, cy - 2, 5, 4, S_WALL_WOOD, G_FLOOR_WOOD, LootKind::Toolbox, 1, 1, 0.1f);
        }
        // The dead in the woods (they are raiders' posts no longer: zombies roam here).
        int n = rng.irange(2, 4) + day / 4;
        for (int i = 0; i < n; i++)
            for (int attempt = 0; attempt < 10; attempt++) {
                int x = cx + rng.irange(-r - 4, r + 4), y = cy + rng.irange(-r - 4, r + 4);
                if (!W.inBounds(x, y) || W.blocksMove(x, y)) continue;
                W.spawns.push_back({World::tileCenter(x, y), EnemyType::Zombie});
                break;
            }
    }
    // A forest region (0.13v): thick trees (the terrain pass planted them), trails off the
    // highway that runs through it, and clearings at their ends.
    void forest(const Region& R, bool westEast) {
        // Where the highway runs through: find its tiles.
        std::vector<std::pair<int, int>> hwy;
        for (int y = R.y0; y < R.y0 + R.h; y++)
            for (int x = R.x0; x < R.x0 + R.w; x++)
                if (W.at(x, y).ground == G_ROAD) hwy.push_back({x, y});
        int trails = rng.irange(5, 7);
        std::vector<std::pair<int, int>> ends;
        for (int i = 0; i < trails && !hwy.empty(); i++) {
            auto [hx, hy] = hwy[rng.next() % hwy.size()];
            // Off one side of the road or the other, square to it, then winding.
            bool side = rng.chance(0.5f);
            float dir = westEast ? (side ? PI * 0.5f : -PI * 0.5f) : (side ? 0.0f : PI);
            // Step off the asphalt first.
            int sx = hx, sy = hy;
            for (int k = 0; k < 6 && W.inBounds(sx, sy) && W.at(sx, sy).ground == G_ROAD; k++) {
                sx += (int)std::lround(std::cos(dir));
                sy += (int)std::lround(std::sin(dir));
            }
            auto end = trail((float)sx, (float)sy, dir + rng.range(-0.4f, 0.4f), rng.irange(28, 60), rng.chance(0.4f) ? 2 : 1, R);
            // A fork now and then, on towards somewhere else.
            if (rng.chance(0.5f)) {
                auto fork = trail((float)end.first, (float)end.second, dir + rng.range(-1.4f, 1.4f), rng.irange(16, 34), 1, R);
                ends.push_back(fork);
            }
            ends.push_back(end);
        }
        for (auto [x, y] : ends) clearing(x, y, rng.irange(5, 8));
        // A few loners wandering between the trees, and a bag dropped here and there.
        for (int i = 0; i < 6 + day / 2; i++) {
            int x = rng.irange(R.x0 + 6, R.x0 + R.w - 7), y = rng.irange(R.y0 + 6, R.y0 + R.h - 7);
            if (W.blocksMove(x, y)) continue;
            W.spawns.push_back({World::tileCenter(x, y), EnemyType::Zombie});
        }
        for (int i = 0; i < 8; i++) {
            int x = rng.irange(R.x0 + 6, R.x0 + R.w - 7), y = rng.irange(R.y0 + 6, R.y0 + R.h - 7);
            if (W.blocksMove(x, y) || W.at(x, y).ground == G_ROAD) continue;
            int id = W.addContainer(World::tileCenter(x, y), CK_BAG, -1, -1, (uint8_t)rng.next());
            fillContainer(id, qualityAt(x, y), LootKind::Bag, 1, 3);
        }
    }

    // ---- the army base (0.13v, day 7): a wire fence round barracks, the HQ, a motor
    // pool and a supply depot, towers of sandbags at the corners, soldiers on patrol.
    void armyBase(const Region& R, int gateX) {
        const int bw = 124, bh = 78;
        int x0 = R.cx() - bw / 2, y0 = R.cy() - bh / 2 + 4;
        for (int y = y0 - 2; y < y0 + bh + 2; y++)
            for (int x = x0 - 2; x < x0 + bw + 2; x++) {
                Tile& t = W.at(x, y);
                if (t.ground == G_ROAD) continue;
                t.solid = S_NONE; t.hp = 0; t.worldDeco = 0; t.deco = 0; t.flags = 0;
                t.ground = G_DIRT;
            }
        // The fence, with the gate where the highway comes in.
        for (int x = x0; x < x0 + bw; x++)
            for (int y : {y0, y0 + bh - 1}) {
                if (y == y0 + bh - 1 && x >= gateX - 1 && x <= gateX + 4) continue;
                setSolid(x, y, S_FENCE);
            }
        for (int y = y0; y < y0 + bh; y++) { setSolid(x0, y, S_FENCE); setSolid(x0 + bw - 1, y, S_FENCE); }
        // Sandbag towers at the corners, a sniper in each.
        for (int cx : {x0 + 3, x0 + bw - 6})
            for (int cy : {y0 + 3, y0 + bh - 6}) {
                for (int k = 0; k < 4; k++) {
                    setSolid(cx + k, cy, S_SANDBAG); setSolid(cx + k, cy + 3, S_SANDBAG);
                    setSolid(cx, cy + k, S_SANDBAG); setSolid(cx + 3, cy + k, S_SANDBAG);
                }
                setSolid(cx + 1, cy + 3, S_NONE); setSolid(cx + 2, cy + 3, S_NONE);
                W.spawns.push_back({World::tileCenter(cx + 1, cy + 1), EnemyType::Sniper});
            }
        // The parade ground in the middle, paved, and the helipad beside it.
        auto pave = [&](int px, int py, int pw, int ph) {
            for (int y = py; y < py + ph; y++)
                for (int x = px; x < px + pw; x++) if (W.at(x, y).solid == S_NONE) W.at(x, y).ground = G_PAVEMENT;
        };
        pave(x0 + 44, y0 + 30, 36, 18);
        pave(x0 + 10, y0 + 32, 14, 14);
        // Barracks along the back.
        bkind = BK_MILITARY;
        for (int i = 0; i < 4; i++)
            building(x0 + 12 + i * 26, y0 + 9, 18, 9, S_WALL_CONCRETE, G_FLOOR_CONCRETE, LootKind::Military, 2, 4, 0.03f);
        // The HQ, two floors, and the armoury.
        building(x0 + 88, y0 + 30, 22, 13, S_WALL_CONCRETE, G_FLOOR_TILE, LootKind::Military, 3, 5, 0, 1);
        building(x0 + 26, y0 + bh - 20, 14, 9, S_WALL_CONCRETE, G_FLOOR_CONCRETE, LootKind::Military, 4, 6, 0);
        // The motor pool: wrecks in rows on the pavement.
        pave(x0 + 80, y0 + bh - 22, 34, 16);
        for (int i = 0; i < 5; i++) wreck(x0 + 84 + i * 6, y0 + bh - 16, 2, (uint8_t)rng.next());
        // The supply depot: stacked crates and the army's own boxes.
        for (int i = 0; i < 40; i++) {
            int x = x0 + 48 + rng.irange(0, 26), y = y0 + bh - 20 + rng.irange(0, 12);
            if (W.at(x, y).solid == S_NONE && W.at(x, y).ground != G_ROAD) setSolid(x, y, S_CRATE);
        }
        for (int i = 0; i < 6; i++) {
            int x = x0 + 48 + rng.irange(0, 26), y = y0 + bh - 20 + rng.irange(0, 12);
            if (W.at(x, y).solid == S_NONE && W.at(x, y).ground != G_ROAD) placeContainer(x, y, qualityAt(x, y) + 0.2f, LootKind::Military);
        }
        // Who holds it: squads about the place and a patrol round the inside of the fence.
        spawnGroup(x0 + 60, y0 + 38, 4, 12, true);
        spawnGroup(x0 + 30, y0 + 20, 3, 10, true);
        spawnGroup(x0 + 95, y0 + 20, 3, 10, true);
        spawnGroup(x0 + 60, y0 + bh - 14, 3, 10, true);
        PatrolRoute route;
        int in = 7;
        route.points = {World::tileCenter(x0 + in, y0 + in), World::tileCenter(x0 + bw - in, y0 + in),
                        World::tileCenter(x0 + bw - in, y0 + bh - in), World::tileCenter(x0 + in, y0 + bh - in)};
        W.patrols.push_back(route);
        int id = (int)W.patrols.size() - 1;
        for (int m = 0; m < 4 + day / 8; m++)
            W.spawns.push_back({route.points[0] + Vec2((float)(m * 14), 0), m == 0 ? EnemyType::Heavy : EnemyType::Bandit, false, id});
        // Outposts out on the rest of the land.
        for (int i = 0, tries = 0; i < 3 && tries < 60; tries++) {
            int cx = rng.irange(R.x0 + 26, R.x0 + R.w - 27), cy = rng.irange(R.y0 + 26, R.y0 + R.h - 27);
            if (cx > x0 - 16 && cx < x0 + bw + 16 && cy > y0 - 16 && cy < y0 + bh + 16) continue;
            if (nearWater(cx, cy, 13)) continue;
            military(cx, cy);
            i++;
        }
    }

    // ---- the radio hill (0.13v, day 12): a dead radio tower in its fenced yard (mend it
    // and a supply drop comes down every day after), and the survivors' camp.
    void radioHill(const Region& R) {
        // Somewhere dry in the north half for the tower, the south half for the camp.
        auto dryspot = [&](int ya, int yb, int r) {
            for (int tries = 0; tries < 200; tries++) {
                int cx = rng.irange(R.x0 + 34, R.x0 + R.w - 35), cy = rng.irange(ya, yb);
                if (!nearWater(cx, cy, r) && W.at(cx, cy).ground != G_ROAD) return std::make_pair(cx, cy);
            }
            return std::make_pair(R.cx() + 30, (ya + yb) / 2);
        };
        auto [tx, ty] = dryspot(R.y0 + 34, R.cy() - 20, 16);
        clearArea(tx - 12, ty - 10, 25, 21, G_DIRT);
        for (int x = tx - 11; x <= tx + 11; x++) { setSolid(x, ty - 9, S_FENCE); if (std::abs(x - tx) > 2) setSolid(x, ty + 9, S_FENCE); }
        for (int y = ty - 9; y <= ty + 9; y++) { setSolid(tx - 11, y, S_FENCE); setSolid(tx + 11, y, S_FENCE); }
        // The tower's feet (drawn by the raid), and its console in front.
        for (int y = ty - 3; y <= ty - 2; y++)
            for (int x = tx - 1; x <= tx; x++) { Tile& t = W.at(x, y); t.solid = S_FURNITURE; t.hp = -1; }
        W.radioPos = World::tileCenter(tx, ty) + Vec2(-8, 2);
        bkind = BK_MILITARY;
        building(tx + 3, ty - 7, 7, 6, S_WALL_CONCRETE, G_FLOOR_CONCRETE, LootKind::Toolbox, 1, 2, 0);
        spawnGroup(tx, ty + 14, 3, 10, true);
        g_road(tx, ty + 9, R.cx(), W.homeTy + 12);

        // The camp: a ring of sandbags, tents, and two who will deal with you.
        auto [cx, cy] = dryspot(R.cy() + 24, R.y0 + R.h - 34, 18);
        clearArea(cx - 14, cy - 11, 29, 23, G_DIRT);
        for (int x = cx - 13; x <= cx + 13; x++) { setSolid(x, cy - 10, S_SANDBAG); if (std::abs(x - cx) > 2) setSolid(x, cy + 10, S_SANDBAG); }
        for (int y = cy - 10; y <= cy + 10; y++) { setSolid(cx - 13, y, S_SANDBAG); setSolid(cx + 13, y, S_SANDBAG); }
        bkind = BK_CABIN;
        building(cx - 11, cy - 8, 6, 5, S_WALL_WOOD, G_FLOOR_WOOD, LootKind::Generic, 0, 1, 0);
        building(cx + 5, cy - 8, 6, 5, S_WALL_WOOD, G_FLOOR_WOOD, LootKind::Generic, 0, 1, 0);
        W.medicPos = World::tileCenter(cx - 5, cy + 3);
        W.buyerPos = World::tileCenter(cx + 5, cy + 3);
        g_road(cx, cy + 10, R.cx(), W.homeTy + 12);
        // Farms round about, and those who pick them over.
        for (int i = 0, tries = 0; i < 2 && tries < 60; tries++) {
            int fx = rng.irange(R.x0 + 26, R.x0 + R.w - 27), fy = rng.irange(R.y0 + 26, R.y0 + R.h - 27);
            if (nearWater(fx, fy, 13) || std::abs(fx - tx) + std::abs(fy - ty) < 40 || std::abs(fx - cx) + std::abs(fy - cy) < 40) continue;
            farm(fx, fy);
            i++;
        }
    }
    // A plain two-wide road for a region's own tracks (the Gen's width may be the highway's).
    void g_road(int x0, int y0, int x1, int y1) {
        int keep = roadW;
        roadW = 2;
        road(x0, y0, x1, y1);
        roadW = keep;
    }

    // ---- the suburb (0.13v, day 15): streets of houses off the highway, most of them
    // wrecked; the metro goes down somewhere among them (generateBelow).
    void suburb(const Region& R, int highwayX) {
        bkind = BK_TOWN;
        for (int sy = R.y0 + 30; sy < R.y0 + R.h - 24; sy += 30) {
            int x0 = R.x0 + 24, x1 = R.x0 + R.w - 24;
            g_road(x0, sy, x1, sy);
            for (int x = x0 + 2; x < x1 - 10; x += rng.irange(12, 16)) {
                if (std::abs(x - highwayX) < 12) continue;
                for (int side = 0; side < 2; side++) {
                    int bw = rng.irange(7, 10), bh = rng.irange(6, 8);
                    int by = side == 0 ? sy - bh - 2 : sy + 4;
                    float ruin = rng.chance(0.6f) ? 0.18f : 0.04f;
                    int wall = ruin > 0.1f ? S_WALL_BRICK : (rng.chance(0.5f) ? S_WALL_WOOD : S_WALL_CONCRETE);
                    if (!nearWater(x + bw / 2, by + bh / 2, 6))
                        building(x, by, bw, bh, wall, rng.chance(0.6f) ? G_FLOOR_WOOD : G_FLOOR_TILE, LootKind::Generic, 1, 3, ruin);
                }
            }
            spawnGroup(R.cx() + rng.irange(-60, 60), sy, rng.irange(3, 4), 14);
        }
        for (int i = 0; i < 18 + day; i++) {
            int x = rng.irange(R.x0 + 10, R.x0 + R.w - 11), y = rng.irange(R.y0 + 10, R.y0 + R.h - 11);
            if (!W.blocksMove(x, y)) W.spawns.push_back({World::tileCenter(x, y), EnemyType::Zombie});
        }
    }

    // Today's supply drop (0.13v): a crate of army kit on open ground somewhere on the
    // open land well away from home, with the ones who saw it come down on their way.
    void supplyDrop() {
        for (int tries = 0; tries < 400; tries++) {
            int r = rng.irange(0, RG_COUNT - 1);
            const Region& R = REGIONS[r];
            if (R.day > day || r == RG_CITY_W) continue;
            int x = rng.irange(R.x0 + 14, R.x0 + R.w - 15), y = rng.irange(R.y0 + 14, R.y0 + R.h - 15);
            int dx = x - W.homeTx, dy = y - W.homeTy;
            if (dx * dx + dy * dy < 70 * 70) continue;
            bool ok = true;
            for (int yy = y - 2; yy <= y + 2 && ok; yy++)
                for (int xx = x - 2; xx <= x + 2 && ok; xx++) {
                    const Tile& t = W.at(xx, yy);
                    if (t.solid != S_NONE || t.ground == G_WATER || t.ground == G_BRIDGE || (t.ground >= G_FLOOR_WOOD && t.ground <= G_FLOOR_TILE)) ok = false;
                }
            if (!ok) continue;
            int id = W.addContainer(World::tileCenter(x, y), CK_MILITARY, x, y, (uint8_t)rng.next());
            W.containers[id].searchTime = 2.0f;
            fillContainer(id, 1.15f, LootKind::Military, 5, 8);
            W.dropPos = World::tileCenter(x, y);
            spawnGroup(x, y, 3, 9, true, false);
            return;
        }
    }

    void spawnGroup(int cx, int cy, int count, int radius, bool military = false, bool addDay = true) {
        if (addDay) count += day / 3;
        for (int i = 0; i < count; i++) {
            for (int attempt = 0; attempt < 20; attempt++) {
                int x = cx + rng.irange(-radius, radius), y = cy + rng.irange(-radius, radius);
                if (W.blocksMove(x, y)) continue;
                int dx = x - W.homeTx, dy = y - W.homeTy;
                if (dx * dx + dy * dy < HOME_SAFE_TILES * HOME_SAFE_TILES) continue;
                W.spawns.push_back({World::tileCenter(x, y), pickType(x, y, military)});
                break;
            }
        }
    }
};

}  // namespace

bool World::zombieSpawns = false;
bool World::withCrypts = false;
bool World::airdrops = false;

// ---------------------------------------------------------------- the catacombs
// Built after the outside: the map grows to 320x320 and today's catacombs (three of
// the seven 80x80 blocks beside the outside map) are carved into the new rock. Each is
// a long chain of rooms from the stairs to a treasure room, with a horde of the dead
// or an armed gang in most rooms, chests and urns worth more than anything outside,
// torches on the walls and spikes in the halls.
namespace {
constexpr int CRYPT_BLOCK = 80;
struct Room { int x, y, w, h; int cx() const { return x + w / 2; } int cy() const { return y + h / 2; } };

void cryptLoot(World& W, Rng& rng, int id, int n, bool rich) {
    Container& c = W.containers[id];
    for (int i = 0; i < n; i++) {
        Item it = rollLoot(rng, 1.0f, rng.chance(0.5f) ? LootKind::Military : LootKind::Locker);
        if (hasTier(it) && !itemDef(it.id).elite) {
            // Down here guns come in better: epic and rare far more often, and now and
            // then an elite one.
            if (rich && eliteOf(it.id) != IT_NONE && rng.chance(0.18f * eliteGate())) it = makeItem(eliteOf(it.id));
            else it.tier = (int8_t)rollWeaponTier(rng, rich ? 2.0f : 1.6f);
        }
        addToSlots(c.items, it);
    }
}

void buildCrypt(World& W, Rng& rng, int idx, int bx, int by, int day, bool metro = false) {
    Dungeon d;
    d.metro = metro;
    d.x0 = bx; d.y0 = by; d.w = CRYPT_BLOCK; d.h = CRYPT_BLOCK;
    auto carve = [&](int x, int y) {
        if (x <= bx || y <= by || x >= bx + CRYPT_BLOCK - 1 || y >= by + CRYPT_BLOCK - 1) return;
        Tile& t = W.at(x, y);
        t.ground = G_CRYPT;
        t.solid = S_NONE;
        t.hp = 0;
        t.variant = (uint8_t)rng.next();
    };
    // ---- rooms. A layout whose last room has no way back to the first is thrown
    // away and drawn again (rarely needed more than once).
    std::vector<Room> path;
    std::vector<Vec2> spikes;
    float tier = cryptTier(day);
    for (int layout = 0; layout < 8; layout++) {
        if (layout > 0)
            for (int y = by; y < by + CRYPT_BLOCK; y++)
                for (int x = bx; x < bx + CRYPT_BLOCK; x++) { Tile& t = W.at(x, y); t = Tile(); t.ground = G_CRYPT; t.solid = S_CRYPT_WALL; t.hp = -1; }
        spikes.clear();
        std::vector<Room> rooms;
        // Longer on later days: about 10 rooms on day 1, 16 by day 20.
        size_t wantRooms = (size_t)(10 + tier * 6);
        for (int attempt = 0; attempt < 400 && rooms.size() < wantRooms; attempt++) {
            Room r{0, 0, rng.irange(7, 13), rng.irange(6, 10)};
            // Rooms keep off the block's edge: the rock ring left there is where the way
            // back from the last room can always run (0.11v).
            r.x = rng.irange(bx + 6, bx + CRYPT_BLOCK - 7 - r.w);
            r.y = rng.irange(by + 7, by + CRYPT_BLOCK - 7 - r.h);   // room for the stair arch above the first
            bool clash = false;
            for (const Room& o : rooms)
                if (r.x < o.x + o.w + 3 && o.x < r.x + r.w + 3 && r.y < o.y + o.h + 3 && o.y < r.y + r.h + 3) { clash = true; break; }
            if (!clash) rooms.push_back(r);
        }
        if (rooms.size() < 4) { if (layout == 7) return; continue; }
        // A long way through: from the first room, always on to the nearest one not yet
        // visited, so the rooms string out into one winding path.
        path.assign(1, rooms[0]);
        std::vector<bool> used(rooms.size(), false);
        used[0] = true;
        for (size_t k = 1; k < rooms.size(); k++) {
            int best = -1, bd = 1 << 30;
            for (size_t j = 0; j < rooms.size(); j++) {
                if (used[j]) continue;
                int dx = rooms[j].cx() - path.back().cx(), dy = rooms[j].cy() - path.back().cy();
                if (dx * dx + dy * dy < bd) { bd = dx * dx + dy * dy; best = (int)j; }
            }
            used[best] = true;
            path.push_back(rooms[best]);
        }
        for (const Room& r : path)
            for (int y = r.y; y < r.y + r.h; y++)
                for (int x = r.x; x < r.x + r.w; x++) carve(x, y);
        for (size_t k = 1; k < path.size(); k++) {
            int x = path[k - 1].cx(), y = path[k - 1].cy(), tx = path[k].cx(), ty = path[k].cy();
            bool horizFirst = rng.chance(0.5f);
            auto dig = [&](int px, int py) { for (int oy = -1; oy <= 1; oy++) for (int ox = -1; ox <= 1; ox++) carve(px + ox, py + oy); };
            int steps = 0, spikeAt = rng.chance(0.4f) ? rng.irange(4, 12) : -1;
            auto walk = [&](bool horiz) {
                while (horiz ? x != tx : y != ty) {
                    if (horiz) x += x < tx ? 1 : -1; else y += y < ty ? 1 : -1;
                    dig(x, y);
                    if (++steps == spikeAt) spikes.push_back(World::tileCenter(x, y));
                }
            };
            walk(horizFirst);
            walk(!horizFirst);
        }
        // ---- the way back (0.11v): a hall of its own from the last room to the first,
        // touching nothing else on the way (so it skips no rooms), sealed at the last room
        // by a portcullis that only opens from inside.
        const Room& start = path.front();
        int sx = start.cx();
        {
            const Room& last = path.back();
            auto inRoom = [](const Room& r, int x, int y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; };
            const int N = CRYPT_BLOCK;
            // Where the hall's middle may run: a wall between it and everything carved so
            // far except the first room, and never through the stair arch above it.
            std::vector<uint8_t> ok((size_t)N * N, 0);
            for (int cy = by + 2; cy <= by + N - 3; cy++)
                for (int cx = bx + 2; cx <= bx + N - 3; cx++) {
                    if (cy < start.y && cy >= start.y - 6 && std::abs(cx - sx) <= 4) continue;
                    bool good = true;
                    for (int y = cy - 2; y <= cy + 2 && good; y++)
                        for (int x = cx - 2; x <= cx + 2 && good; x++)
                            if (W.at(x, y).solid != S_CRYPT_WALL && !inRoom(start, x, y)) good = false;
                    ok[(size_t)(cy - by) * N + (cx - bx)] = good;
                }
            auto goal = [&](int cx, int cy) {
                return cx + 1 >= start.x && cx - 1 < start.x + start.w && cy + 1 >= start.y && cy - 1 < start.y + start.h;
            };
            auto solidRock = [&](int x0, int y0, int x1, int y1) {
                for (int y = y0; y <= y1; y++)
                    for (int x = x0; x <= x1; x++)
                        if (x <= bx || y <= by || x >= bx + N - 1 || y >= by + N - 1 || W.at(x, y).solid != S_CRYPT_WALL) return false;
                return true;
            };
            std::vector<int> best;
            int bestGx = -1, bestDir = 0;
            for (int dir : {1, -1}) {
                int gy = dir > 0 ? last.y - 1 : last.y + last.h;
                for (int gx = last.x; gx + 3 < last.x + last.w && best.empty(); gx++) {
                    if (!solidRock(gx - 1, dir > 0 ? gy - 3 : gy, gx + 4, dir > 0 ? gy : gy + 3)) continue;
                    // Breadth-first from just past the gate's stub to any spot opening
                    // onto the first room.
                    int sx0 = gx + 1, sy0 = gy - 3 * dir;
                    if (sx0 - bx < 0 || sy0 - by < 0 || sx0 - bx >= N || sy0 - by >= N || !ok[(size_t)(sy0 - by) * N + (sx0 - bx)]) continue;
                    std::vector<int> from((size_t)N * N, -1);
                    std::vector<int> q{(sy0 - by) * N + (sx0 - bx)};
                    from[q[0]] = q[0];
                    int found = -1;
                    for (size_t h = 0; h < q.size() && found < 0; h++) {
                        int cur = q[h], cx = cur % N + bx, cy = cur / N + by;
                        if (goal(cx, cy)) { found = cur; break; }
                        const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
                        for (int k = 0; k < 4; k++) {
                            int nx = cx + dx[k] - bx, ny = cy + dy[k] - by;
                            if (nx < 0 || ny < 0 || nx >= N || ny >= N) continue;
                            int ni = ny * N + nx;
                            if (from[ni] >= 0 || !ok[ni]) continue;
                            from[ni] = cur;
                            q.push_back(ni);
                        }
                    }
                    if (found < 0) continue;
                    for (int c = found;; c = from[c]) { best.push_back(c); if (from[c] == c) break; }
                    bestGx = gx; bestDir = dir;
                }
                if (!best.empty()) break;
            }
            if (!best.empty()) {
                int gx = bestGx, dir = bestDir, gy = dir > 0 ? last.y - 1 : last.y + last.h;
                for (int c : best)
                    for (int oy = -1; oy <= 1; oy++)
                        for (int ox = -1; ox <= 1; ox++) carve(c % N + bx + ox, c / N + by + oy);
                for (int s = 1; s <= 2; s++)
                    for (int x = gx; x < gx + 4; x++) carve(x, gy - s * dir);
                for (int x = gx; x < gx + 4; x++) {
                    Tile& t = W.at(x, gy);
                    t.ground = G_CRYPT;
                    t.solid = (x == gx || x == gx + 3) ? S_CRYPT_PROP : S_CRYPT_GATE;
                    t.hp = -1;
                    t.variant = (uint8_t)rng.next();
                }
                d.gateX = gx; d.gateY = gy; d.gateDir = dir;
                W.props.push_back({Vec2((gx + 2) * (float)TILE, (gy + 1) * (float)TILE), PROP_CRYPT_GATE, (uint8_t)idx, false});
            }
        }
        if (d.hasGate()) break;
    }
    const Room& start = path.front();
    int sx = start.cx();
    // ---- the way in and out: a stair arch in the first room's top wall
    d.exit = Vec2(sx * (float)TILE + TILE * 0.5f, start.y * (float)TILE + TILE * 0.5f);
    d.arrive = d.exit + Vec2(0, 26);
    W.props.push_back({Vec2(sx * (float)TILE + TILE * 0.5f, start.y * (float)TILE), PROP_CRYPT_EXIT, (uint8_t)idx, false});
    // ---- dressing and trouble, room by room
    float depth = 0;
    // Nothing stands in front of the way back's gate.
    auto byGate = [&](int x, int y) {
        return d.hasGate() && x >= d.gateX - 1 && x <= d.gateX + 4 && std::abs(y - (d.gateY + d.gateDir)) <= 2;
    };
    for (size_t k = 0; k < path.size(); k++) {
        const Room& r = path[k];
        depth = k / (float)(path.size() - 1);
        bool last = k + 1 == path.size();
        // Torches along the top wall, candles in the corners.
        for (int x = r.x + 1; x < r.x + r.w - 1; x += 4)
            if (W.at(x, r.y - 1).solid == S_CRYPT_WALL && !(k == 0 && std::abs(x - sx) <= 2))
                W.props.push_back({Vec2(x * (float)TILE + TILE * 0.5f, r.y * (float)TILE - 2), PROP_TORCH, (uint8_t)rng.next(), false});
        if (rng.chance(0.6f))
            W.props.push_back({World::tileCenter(r.x, r.y + r.h - 1) + Vec2(0, 7), PROP_CANDLE, (uint8_t)rng.next(), false});
        if (rng.chance(0.6f))
            W.props.push_back({World::tileCenter(r.x + r.w - 1, r.y + r.h - 1) + Vec2(0, 7), PROP_CANDLE, (uint8_t)rng.next(), false});
        // Pillars in the big halls.
        if (r.w >= 10 && r.h >= 8 && k > 0)
            for (int px : {r.x + 2, r.x + r.w - 3})
                for (int py : {r.y + 2, r.y + r.h - 3}) {
                    Tile& t = W.at(px, py);
                    if (t.solid != S_NONE || byGate(px, py)) continue;
                    t.solid = S_CRYPT_PROP;
                    t.hp = -1;
                    W.props.push_back({World::tileCenter(px, py) + Vec2(0, 8), PROP_PILLAR, (uint8_t)rng.next(), false});
                }
        // A coffin stood against the back wall now and then.
        if (k > 0 && rng.chance(0.45f)) {
            int cx = rng.irange(r.x + 1, r.x + r.w - 3);
            if (W.at(cx, r.y).solid == S_NONE && W.at(cx + 1, r.y).solid == S_NONE &&
                W.at(cx, r.y - 1).solid == S_CRYPT_WALL && W.at(cx + 1, r.y - 1).solid == S_CRYPT_WALL &&
                W.at(cx, r.y + 1).solid == S_NONE && W.at(cx + 1, r.y + 1).solid == S_NONE) {
                W.at(cx, r.y).solid = W.at(cx + 1, r.y).solid = S_CRYPT_PROP;
                W.at(cx, r.y).hp = W.at(cx + 1, r.y).hp = -1;
                W.props.push_back({Vec2((cx + 1) * (float)TILE, (r.y + 1) * (float)TILE), PROP_COFFIN, (uint8_t)rng.next(), false});
            }
        }
        // Urns (a little loot) and chests (a lot).
        auto freeSpot = [&](int& fx, int& fy) {
            for (int a = 0; a < 30; a++) {
                fx = rng.irange(r.x + 1, r.x + r.w - 2);
                fy = rng.irange(r.y + 1, r.y + r.h - 2);
                if (W.at(fx, fy).solid == S_NONE && !byGate(fx, fy) && std::abs(fx - r.cx()) + std::abs(fy - r.cy()) > 1) return true;
            }
            return false;
        };
        int urns = rng.irange(1, 3), chests = last ? 3 : (k > 0 && rng.chance(0.3f + depth * 0.3f) ? 1 : 0);
        for (int i = 0; i < urns; i++) {
            int fx, fy;
            if (!freeSpot(fx, fy)) continue;
            int id = W.addContainer(World::tileCenter(fx, fy), CK_URN, fx, fy, (uint8_t)rng.next());
            W.containers[id].searchTime = 0.5f;
            cryptLoot(W, rng, id, rng.irange(0, 2), false);
        }
        for (int i = 0; i < chests; i++) {
            int fx, fy;
            if (!freeSpot(fx, fy)) continue;
            int id = W.addContainer(World::tileCenter(fx, fy), CK_CHEST, fx, fy, (uint8_t)rng.next());
            W.containers[id].searchTime = 1.2f;
            cryptLoot(W, rng, id, rng.irange(2, 4) + (last ? 2 : 0), true);
        }
        // Who is waiting. Nothing in the first room; more the deeper you go, and the
        // last room has both.
        if (k == 0) continue;
        // Early on some rooms are empty, a breather between fights; by day 20 none are.
        if (!last && rng.chance(0.35f * (1.0f - tier))) continue;
        int kind = last ? 2 : (int)(rng.next() % 2);
        auto spot = [&]() {
            for (int a = 0; a < 20; a++) {
                int fx = rng.irange(r.x + 1, r.x + r.w - 2), fy = rng.irange(r.y + 1, r.y + r.h - 2);
                if (W.at(fx, fy).solid == S_NONE && !byGate(fx, fy)) return World::tileCenter(fx, fy);
            }
            return World::tileCenter(r.cx(), r.cy());
        };
        if (metro && last) W.spawns.push_back({World::tileCenter(r.cx(), r.cy()), EnemyType::Zombie, true, -1, true});
        if (kind != 1) {   // a horde of the dead
            int n = rng.irange(3, 5) + (int)(tier * 5) + (int)(depth * (2 + tier * 5));
            for (int i = 0; i < n; i++) W.spawns.push_back({spot(), EnemyType::Zombie, true});
        }
        if (kind != 0) {   // an armed gang
            int n = rng.irange(2, 3) + (int)(tier * 3) + (int)(depth * (1 + tier * 3));
            for (int i = 0; i < n; i++) {
                float t = rng.f();
                // Heavies and snipers are rare early, common later and deeper in.
                float hv = 0.08f + 0.2f * tier + depth * 0.15f, sn = hv + 0.08f + 0.15f * tier + depth * 0.1f;
                EnemyType ty = t < hv ? EnemyType::Heavy : t < sn ? EnemyType::Sniper : t < 0.35f + tier * 0.4f ? EnemyType::Bandit : EnemyType::Scav;
                W.spawns.push_back({spot(), ty, true});
            }
        }
    }
    for (Vec2 p : spikes) W.props.push_back({p, PROP_SPIKES, (uint8_t)rng.next(), false});
    W.dungeons.push_back(d);
}
}  // namespace

// ---- upper floors (0.11v) ------------------------------------------------------------
// Each floor above a city building is built in the blocks beside the outside map, the
// same size and shape as the building, walled in, with stairs where the flight from
// below comes up and, if there is one more floor, another flight further along. What
// is round a floor is nothing at all: black void.
namespace {
struct FloorPacker {
    std::vector<std::pair<int, int>> blocks;   // free 80x80 blocks, used in order
    size_t cur = 0;
    int x = 0, y = 0, shelf = 0;
    bool started = false;
    // A place for a w x h floor with void round it; false when there is no room left.
    bool place(int w, int h, int& ox, int& oy) {
        const int M = 3;
        for (;;) {
            if (cur >= blocks.size()) return false;
            if (!started) { x = y = M; shelf = 0; started = true; }
            if (x + w + M > CRYPT_BLOCK) { x = M; y += shelf + M; shelf = 0; }
            if (y + h + M > CRYPT_BLOCK) { cur++; started = false; continue; }
            ox = blocks[cur].first + x;
            oy = blocks[cur].second + y;
            x += w + M;
            shelf = std::max(shelf, h);
            return true;
        }
    }
};

// The front of a flight of stairs whose left top tile is (sx, sy): where you stand.
Vec2 stairFront(int sx, int sy) { return Vec2((sx + 1) * (float)TILE, (sy + 2) * (float)TILE + 8); }

void putStairs(World& W, int sx, int sy, int stairway) {
    for (int y = sy; y <= sy + 1; y++)
        for (int x = sx; x <= sx + 1; x++) {
            Tile& t = W.at(x, y);
            t.solid = S_STAIRS; t.hp = -1; t.furn = 0; t.container = -1;
        }
    W.props.push_back({Vec2((sx + 1) * (float)TILE, (sy + 2) * (float)TILE), PROP_STAIRS, (uint8_t)stairway, false});
}

void buildFloors(World& W, Rng& rng, const std::vector<FloorPlan>& plans, int day, FloorPacker& pack) {
    for (const FloorPlan& plan : plans) {
        if (plan.building < 0 || plan.building >= (int)W.buildings.size()) continue;
        Building below = W.buildings[plan.building];
        int rsx = plan.sx - below.x0, rsy = plan.sy - below.y0;
        // The stairs up from the ground floor.
        int lowX = plan.sx, lowY = plan.sy;
        int wallType = W.at(below.x0, below.y0).solid;
        if (wallType != S_WALL_BRICK && wallType != S_WALL_WOOD && wallType != S_WALL_CONCRETE) wallType = S_WALL_CONCRETE;
        uint8_t groundType = W.at(below.x0 + below.w / 2, below.y0 + below.h - 2).ground;
        if (groundType < G_FLOOR_WOOD || groundType > G_FLOOR_TILE) groundType = G_FLOOR_CONCRETE;
        uint8_t style = W.at(below.x0 + 1, below.y0 + below.h - 2).variant;
        for (int level = 1; level <= plan.levels; level++) {
            int fx, fy;
            if (!pack.place(below.w, below.h, fx, fy)) return;   // out of room: the building stays lower
            Floor fl;
            fl.x0 = fx; fl.y0 = fy; fl.w = below.w; fl.h = below.h;
            fl.bx = below.x0; fl.by = below.y0; fl.level = level;
            W.floors.push_back(fl);
            int floorIdx = (int)W.floors.size() - 1;
            for (int y = fy; y < fy + below.h; y++)
                for (int x = fx; x < fx + below.w; x++) {
                    Tile& t = W.at(x, y);
                    t = Tile();
                    t.ground = groundType;
                    t.variant = style;
                    bool edge = x == fx || y == fy || x == fx + below.w - 1 || y == fy + below.h - 1;
                    if (edge) { t.solid = (uint8_t)wallType; t.hp = (int16_t)solidInfo(wallType).hp; }
                }
            // The flight coming up from below, and its twin going back down.
            int upX = fx + rsx, upY = fy + rsy;
            W.stairs.push_back({stairFront(lowX, lowY), stairFront(upX, upY), true, floorIdx});
            putStairs(W, lowX, lowY, (int)W.stairs.size() - 1);
            W.stairs.push_back({stairFront(upX, upY), stairFront(lowX, lowY), false, floorIdx});
            putStairs(W, upX, upY, (int)W.stairs.size() - 1);
            // One more floor: its flight goes somewhere else along the back wall.
            int nextX = -1;
            if (level < plan.levels) {
                for (int tries = 0; tries < 30 && nextX < 0; tries++) {
                    int sx = rng.irange(fx + 1, fx + below.w - 3);
                    if (sx >= upX - 3 && sx <= upX + 3) continue;
                    bool ok = true;
                    for (int y = fy + 1; y <= fy + 3 && ok; y++)
                        for (int x = sx; x <= sx + 1 && ok; x++) ok = W.at(x, y).solid == S_NONE;
                    if (ok) nextX = sx;
                }
                // Taken now, so nothing else is put there; its stairs are drawn with the next floor.
                if (nextX >= 0)
                    for (int y = fy + 1; y <= fy + 2; y++)
                        for (int x = nextX; x <= nextX + 1; x++) { W.at(x, y).solid = S_STAIRS; W.at(x, y).hp = -1; }
            }
            // A dividing wall with a doorway, on the bigger floors.
            if (below.w >= 12 && rng.chance(0.6f)) {
                int px = fx + below.w / 2 + rng.irange(-1, 1);
                bool clear = px < upX - 1 || px > upX + 2;
                if (nextX >= 0) clear = clear && (px < nextX - 1 || px > nextX + 2);
                if (clear) {
                    int gap = rng.irange(fy + 2, fy + below.h - 3);
                    for (int y = fy + 1; y < fy + below.h - 1; y++) {
                        Tile& t = W.at(px, y);
                        if (y == gap || y == gap + 1) { t.solid = S_DOOR; t.hp = (int16_t)solidInfo(S_DOOR).hp; }
                        else if (t.solid == S_NONE) { t.solid = (uint8_t)wallType; t.hp = (int16_t)solidInfo(wallType).hp; }
                    }
                }
            }
            // Loot: the higher, the better.
            float q = W.lootQuality(below.x0, below.y0) + 0.12f * level;
            int want = rng.irange(2, 4) + level;
            for (int attempt = 0, placed = 0; attempt < 80 && placed < want; attempt++) {
                int x = rng.irange(fx + 1, fx + below.w - 2), y = rng.irange(fy + 1, fy + below.h - 2);
                if (W.at(x, y).solid != S_NONE) continue;
                bool nearWall = false;
                for (int k = 0; k < 4; k++) {
                    static const int DX[4] = {1, -1, 0, 0}, DY[4] = {0, 0, 1, -1};
                    int s = W.at(x + DX[k], y + DY[k]).solid;
                    nearWall = nearWall || s == wallType;
                }
                if (!nearWall) continue;
                if (dist(World::tileCenter(x, y), stairFront(upX, upY)) < 40) continue;
                if (nextX >= 0 && dist(World::tileCenter(x, y), stairFront(nextX, fy + 1)) < 40) continue;
                LootKind k = rng.chance(0.35f) ? LootKind::Military : rng.chance(0.5f) ? LootKind::Locker : LootKind::Crate;
                int ck = k == LootKind::Military ? CK_MILITARY : k == LootKind::Locker ? CK_LOCKER : CK_CRATE;
                int id = W.addContainer(World::tileCenter(x, y), ck, x, y, (uint8_t)rng.next());
                Container& c = W.containers[id];
                int n = rng.irange(2, 4);
                for (int i = 0; i < n; i++) addToSlots(c.items, rollLoot(rng, q, k));
                placed++;
            }
            // Whoever holds it.
            int gang = 2 + level + day / 5;
            for (int i = 0; i < gang; i++)
                for (int attempt = 0; attempt < 20; attempt++) {
                    int x = rng.irange(fx + 1, fx + below.w - 2), y = rng.irange(fy + 1, fy + below.h - 2);
                    if (W.blocksMove(x, y)) continue;
                    if (dist(World::tileCenter(x, y), stairFront(upX, upY)) < 48) continue;   // not on top of you as you come up
                    float r = rng.f();
                    EnemyType t = r < 0.2f ? EnemyType::Heavy : r < 0.32f ? EnemyType::Sniper : EnemyType::Bandit;
                    W.spawns.push_back({World::tileCenter(x, y), t});
                    break;
                }
            Building b;
            b.x0 = fx; b.y0 = fy; b.w = below.w; b.h = below.h;
            b.style = below.style;
            b.sheet = below.sheet;
            b.kind = BK_FLOOR;
            b.walls = 2 * (below.w + below.h) - 4;
            W.buildings.push_back(b);
            if (nextX < 0) break;
            lowX = nextX;
            lowY = fy + 1;
            rsx = nextX - fx;
            rsy = 1;
        }
    }
}
}  // namespace

// Grows the map (rock for the catacombs, void for upper floors) and builds what lives
// out there: the day's upper floors and its catacomb, with its door out in the world.
static void generateBelow(World& W, uint64_t seed, int day, const std::vector<FloorPlan>& plans, bool crypts) {
    Rng rng(mix64(seed ^ 0xC7A7C0B5ull));
    int ow = W.w, oh = W.h;
    int nw = ow + CRYPT_BLOCK, nh = oh + CRYPT_BLOCK;
    std::vector<Tile> tiles((size_t)nw * nh);
    for (Tile& t : tiles) { t.ground = G_CRYPT; t.solid = S_CRYPT_WALL; t.hp = -1; }
    for (int y = 0; y < oh; y++)
        for (int x = 0; x < ow; x++) tiles[(size_t)y * nw + x] = W.tiles[(size_t)y * ow + x];
    W.tiles.swap(tiles);
    W.w = nw;
    W.h = nh;
    // The blocks beside the outside map: down the right, and along the bottom.
    std::vector<std::pair<int, int>> blocks;
    for (int by = 0; by + CRYPT_BLOCK <= oh; by += CRYPT_BLOCK) blocks.push_back({ow, by});
    for (int bx = 0; bx + CRYPT_BLOCK <= nw; bx += CRYPT_BLOCK) blocks.push_back({bx, oh});
    // Upper floors take blocks from the bottom-left corner on, as many as they need.
    if (!plans.empty()) {
        int area = 0;
        for (const FloorPlan& fp : plans)
            if (fp.building >= 0 && fp.building < (int)W.buildings.size())
                area += (W.buildings[fp.building].w + 3) * (W.buildings[fp.building].h + 3) * fp.levels;
        int want = std::min((int)blocks.size() - 2, area / (CRYPT_BLOCK * CRYPT_BLOCK * 6 / 10) + 1);
        FloorPacker pack;
        for (int i = 0; i < want; i++) {
            auto b = blocks.back();   // bottom row, from the right end
            blocks.pop_back();
            pack.blocks.push_back(b);
            for (int y = b.second; y < b.second + CRYPT_BLOCK; y++)
                for (int x = b.first; x < b.first + CRYPT_BLOCK; x++) {
                    Tile& t = W.at(x, y);
                    t = Tile();
                    t.ground = G_VOID; t.solid = S_VOID; t.hp = -1;
                }
        }
        Rng frng(mix64(seed ^ 0xF100A5ull));
        size_t firstNew = W.buildings.size();
        buildFloors(W, frng, plans, day, pack);
        W.furnishBuildings(firstNew);
    }
    if (!crypts) return;
    for (size_t i = blocks.size(); i > 1; i--) std::swap(blocks[i - 1], blocks[(size_t)rng.irange(0, (int)i - 1)]);
    // One catacomb a day (0.11v): the first block that takes one and has room for its door.
    // From day 15 (0.13v) a second one: the metro under the suburb, harder, its brute
    // waiting at the end.
    int count = day >= METRO_DAY && REGIONS[RG_SOUTH].day <= day ? 2 : 1;
    for (int i = 0; (int)W.dungeons.size() < count && i < (int)blocks.size(); i++) {
        size_t before = W.dungeons.size();
        bool metro = before == 1;
        buildCrypt(W, rng, (int)before, blocks[i].first, blocks[i].second, metro ? day + 8 : day, metro);
        if (W.dungeons.size() == before) continue;
        // Its door: a stair arch on open ground, well away from the bunker.
        Dungeon& d = W.dungeons.back();
        for (int attempt = 0; attempt < 600; attempt++) {
            // Somewhere on today's open land (0.13v).
            int tx = rng.irange(12, ow - 13), ty = rng.irange(12, oh - 13);
            {
                int total = 0;
                for (const Region& R : REGIONS) if (R.day <= day) total += R.w * R.h;
                int pick = rng.irange(0, std::max(0, total - 1));
                for (const Region& R : REGIONS) {
                    if (R.day > day) continue;
                    if (pick < R.w * R.h) { tx = R.x0 + pick % R.w; ty = R.y0 + pick / R.w; break; }
                    pick -= R.w * R.h;
                }
            }
            if (metro) {
                const Region& S = REGIONS[RG_SOUTH];
                tx = rng.irange(S.x0 + 16, S.x0 + S.w - 17);
                ty = rng.irange(S.y0 + 16, S.y0 + S.h - 17);
            }
            if (tx < 12 || ty < 12 || tx > ow - 13 || ty > oh - 13 || !W.unlockedAt(tx, ty)) continue;
            int dx = tx - W.homeTx, dy = ty - W.homeTy;
            if (dx * dx + dy * dy < 40 * 40) continue;
            bool ok = true;
            for (int y = ty - 5; y <= ty + 4 && ok; y++)
                for (int x = tx - 5; x <= tx + 4 && ok; x++) {
                    const Tile& t = W.at(x, y);
                    if (t.ground == G_WATER || t.ground == G_ROAD || t.ground == G_BRIDGE || (t.ground >= G_FLOOR_WOOD && t.ground != G_WASTE) ||
                        t.solid == S_CONTAINER || t.solid == S_BUNKER || t.solid == S_BOUNDARY || t.solid == S_CAR ||
                        t.solid == S_POLE || t.solid == S_DOOR || t.solid == S_WALL_BRICK || t.solid == S_WALL_CONCRETE ||
                        t.solid == S_WALL_WOOD)
                        ok = false;
                }
            bool nearDoor = false;
            for (const Dungeon& o : W.dungeons)
                if (&o != &d && o.door.x > 0 && dist(o.door, World::tileCenter(tx, ty)) < 30 * TILE) nearDoor = true;
            if (!ok || nearDoor) continue;
            // Clear the ground round it; the arch itself is solid, you stand in front.
            // The paving, the two old pillars flanking the way down, the torches on
            // the arch and the candles at its feet make it a place (0.11v).
            for (int y = ty - 5; y <= ty + 4; y++)
                for (int x = tx - 5; x <= tx + 4; x++) {
                    Tile& t = W.at(x, y);
                    t.solid = S_NONE; t.worldDeco = 0; t.deco = 0;
                    // Trodden earth in an oval round it.
                    float ex = (x - tx + 0.5f) / 4.6f, ey = (y - ty - 1.0f) / 3.6f;
                    if (ex * ex + ey * ey < 1.0f && (t.ground == G_GRASS || t.ground == G_SAND || t.ground == G_WASTE)) t.ground = G_DIRT;
                }
            for (int y = ty - 2; y <= ty; y++)
                for (int x = tx - 2; x <= tx + 1; x++) { Tile& t = W.at(x, y); t.solid = S_CRYPT_PROP; t.hp = -1; }
            W.props.push_back({Vec2(tx * (float)TILE, (ty + 1) * (float)TILE), PROP_CRYPT_DOOR, (uint8_t)before, false});
            for (int px : {tx - 4, tx + 3}) {
                W.at(px, ty + 2).solid = S_CRYPT_PROP;
                W.at(px, ty + 2).hp = -1;
                W.props.push_back({World::tileCenter(px, ty + 2) + Vec2(0, 8), PROP_PILLAR, (uint8_t)rng.next(), false});
            }
            for (float ox : {-40.0f, 38.0f})
                W.props.push_back({Vec2(tx * (float)TILE + ox, (ty + 1) * (float)TILE + 3), PROP_CANDLE, (uint8_t)(rng.next() | 4), false});
            d.door = Vec2(tx * (float)TILE, (ty + 1) * (float)TILE + 10);
            break;
        }
        if (d.door.x <= 0) W.dungeons.pop_back();   // nowhere to put its door today
    }
    int below = 0;
    for (const EnemySpawn& sp : W.spawns) below += sp.crypt ? 1 : 0;
    std::fprintf(stderr, "[crypt] day %d (tier %.2f): %zu catacombs, %d below\n", day, cryptTier(day), W.dungeons.size(), below);
}



void World::generate(uint64_t seedIn, int day) {
    seed = seedIn;
    this->day = day;
    setLootDay(day);
    Rng rng(seed);
    // The island (0.13v): the same map every day, the regions round the home ground
    // opening as the days go by (see REGIONS).
    w = h = WORLD_SIZE;
    outW = w;
    outH = h;
    bool big = day >= CITY_DAY;
    tiles.assign(w * h, Tile());
    containers.clear();
    props.clear();
    spawns.clear();
    buildings.clear();
    cities.clear();
    blocks.clear();
    roofProps.clear();
    floors.clear();
    stairs.clear();
    patrols.clear();
    radioPos = medicPos = buyerPos = dropPos = Vec2();
    const Region& HOME = REGIONS[RG_HOME];
    homeTx = HOME.cx();
    homeTy = HOME.cy();
    homePos = tileCenter(homeTx, homeTy);
    Gen g{*this, rng, day};
    if (big) g.roadW = 3;

    uint32_t s1 = rng.next(), s2 = rng.next(), s3 = rng.next();
    float waterLevel = rng.range(0.30f, 0.38f);
    float forestiness = rng.range(0.50f, 0.62f);
    float desert = rng.range(0.18f, 0.34f);
    uint32_t coastSalt = (uint32_t)mix64(seed ^ 0xC0A57ull);

    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            Tile& t = at(x, y);
            t.variant = (uint8_t)(hash2(x, y, s3) & 255);
            // The sea round every region: its rectangle with the corners rounded off, and
            // the coast wandering in from that by 4 to 26 tiles.
            int r = regionAtTile(x, y);
            if (r >= 0) {
                const Region& R = REGIONS[r];
                float ex = (float)std::min(x - R.x0, R.x0 + R.w - 1 - x), ey = (float)std::min(y - R.y0, R.y0 + R.h - 1 - y);
                const float cr = 34;
                float edge = ex < cr && ey < cr ? cr - std::sqrt((cr - ex) * (cr - ex) + (cr - ey) * (cr - ey)) : std::min(ex, ey);
                if (edge < 30) {
                    float inset = 3.0f + fbm(x * 0.02f, y * 0.02f, coastSalt) * 20.0f + (fbm(x * 0.1f, y * 0.1f, coastSalt ^ 7u) - 0.5f) * 6.0f;
                    if (edge < inset) r = -1;
                }
            }
            if (r < 0) { t.ground = G_WATER; continue; }
            bool woods = r == RG_FOREST_W || r == RG_FOREST_N;
            float e = fbm(x * 0.03f, y * 0.03f, s1);
            float m = fbm(x * 0.045f, y * 0.045f, s2);
            float dh = std::sqrt(float((x - homeTx) * (x - homeTx) + (y - homeTy) * (y - homeTy)));
            if (dh < 16) e = std::max(e, 0.6f);
            // Grass is the default ground; bare earth and the drier scrub are the
            // minority, so the world does not read as one brown mass.
            // Dead scrub (0.12v) in patches of its own, off the home ground.
            float dead = fbm(x * 0.022f, y * 0.022f, s1 ^ 0xDEADu);
            // Ponds (0.13v) in the deepest hollows, away from the hatch.
            if (e < waterLevel * 0.40f && dh > 40 && !woods && r != RG_CITY_W) { t.ground = G_WATER; continue; }
            if (e < waterLevel * 0.55f) t.ground = G_DIRT;    // worn hollows
            else if (dead > 0.64f && dh > 30 && !woods) t.ground = G_WASTE;
            else if (m < desert && !woods) t.ground = G_SAND; // drier scrub
            else t.ground = G_GRASS;

            if (t.ground == G_GRASS && rng.chance(0.03f)) t.worldDeco = Art::decoCode(Art::DK_TUFT, rng.irange(1, 255));
            else if (t.ground == G_DIRT && rng.chance(0.05f)) {
                int k = rng.irange(1, 255);
                t.worldDeco = Art::decoCode(k % 3 ? Art::DK_PEBBLE : Art::DK_FOREST, k / 3);
            }
            if (woods) {
                // The woods (0.13v): trees close together, thinning in glades.
                float thick = 0.30f + (m - 0.5f) * 0.5f;
                if (t.ground != G_SAND && rng.chance(thick)) g.setSolid(x, y, S_TREE);
                else if (rng.chance(0.05f)) g.setSolid(x, y, S_BUSH);
                continue;
            }
            // The scrub keeps a few dead trees.
            if (t.ground == G_WASTE && hash2(x, y, s2 ^ 0x77u) % 90 == 0) g.setSolid(x, y, S_TREE);
            if (t.ground == G_GRASS && m > forestiness && rng.chance((m - forestiness) * 2.2f)) g.setSolid(x, y, S_TREE);
            else if (t.ground == G_GRASS && rng.chance(0.012f)) g.setSolid(x, y, S_TREE);
            else if (t.ground != G_SAND && rng.chance(0.01f)) g.setSolid(x, y, S_BUSH);
        }

    // Islets out in the sea, too far to reach: trees on a rock.
    {
        Rng ir(mix64(seed ^ 0x15137ull));
        for (int i = 0, tries = 0; i < 14 && tries < 400; tries++) {
            int cx = ir.irange(12, w - 13), cy = ir.irange(12, h - 13), rad = ir.irange(3, 8);
            bool clear = true;
            for (int k = 0; k < RG_COUNT && clear; k++) {
                const Region& R = REGIONS[k];
                if (cx > R.x0 - rad - 12 && cx < R.x0 + R.w + rad + 12 && cy > R.y0 - rad - 12 && cy < R.y0 + R.h + rad + 12) clear = false;
            }
            if (!clear) continue;
            i++;
            for (int y = cy - rad - 2; y <= cy + rad + 2; y++)
                for (int x = cx - rad - 2; x <= cx + rad + 2; x++) {
                    float d = std::sqrt(float((x - cx) * (x - cx) + (y - cy) * (y - cy)));
                    if (d > rad * (0.75f + 0.5f * fbm(x * 0.3f, y * 0.3f, coastSalt ^ 0x1u))) continue;
                    Tile& t = at(x, y);
                    t.ground = d > rad - 2 ? G_DIRT : G_GRASS;
                    if (ir.chance(0.45f)) g.setSolid(x, y, S_TREE);
                }
        }
    }

    // Home compound.
    g.clearArea(homeTx - 9, homeTy - 9, 19, 19, G_DIRT);
    for (int y = homeTy - 3; y <= homeTy + 3; y++)
        for (int x = homeTx - 3; x <= homeTx + 3; x++) at(x, y).ground = G_BASE_FLOOR;
    for (int i = -3; i <= 3; i++) {
        at(homeTx + i, homeTy - 3).solid = S_BUNKER;
        at(homeTx - 3, homeTy + i).solid = S_BUNKER;
        at(homeTx + 3, homeTy + i).solid = S_BUNKER;
        if (std::abs(i) >= 2) at(homeTx + i, homeTy + 3).solid = S_BUNKER;
    }
    for (int i = -9; i <= 9; i++) {
        bool gate = std::abs(i) <= 1;
        if (!gate) {
            g.setSolid(homeTx + i, homeTy - 9, S_FENCE);
            g.setSolid(homeTx + i, homeTy + 9, S_FENCE);
            g.setSolid(homeTx - 9, homeTy + i, S_FENCE);
            g.setSolid(homeTx + 9, homeTy + i, S_FENCE);
        } else {
            // The back of the compound has wire gates (0.12v): they open for you and
            // your people, and the dead have to tear them down. The middle one is locked.
            g.setSolid(homeTx + i, homeTy - 9, S_FENCE_GATE);
            at(homeTx + i, homeTy - 9).variant = i == 0 ? 1 : 0;
        }
    }

    // Rivers (0.13v): one across the home ground, one through each wood and one in the
    // east, with wooden footbridges over them now and then (the highways get bridges
    // of their own).
    {
        Rng rr(mix64(seed ^ 0x61FE5ull));
        auto run = [&](float x, float y, float dir, float width) {
            std::vector<Vec2> path = g.river(x, y, dir, 900, width, rr.next());
            g.footbridges(path, 44);
        };
        const Region& FW = REGIONS[RG_FOREST_W];
        const Region& FN = REGIONS[RG_FOREST_N];
        const Region& EA = REGIONS[RG_EAST];
        run((float)(HOME.x0 + rr.irange(36, 80)), (float)HOME.y0, PI * 0.5f + rr.range(0.25f, 0.5f), rr.range(3.5f, 5.0f));
        run((float)(FW.x0 + rr.irange(30, FW.w - 30)), (float)FW.y0, PI * 0.5f + rr.range(-0.3f, 0.3f), rr.range(3.0f, 4.5f));
        run((float)FN.x0, (float)(FN.y0 + rr.irange(24, FN.h - 24)), rr.range(-0.3f, 0.3f), rr.range(3.0f, 4.0f));
        run((float)(EA.x0 + rr.irange(40, EA.w - 40)), (float)(EA.y0 + EA.h), -PI * 0.5f + rr.range(-0.35f, 0.35f), rr.range(4.0f, 6.0f));
    }
    // Muddy banks: the land along the water is bare earth.
    {
        std::vector<uint8_t> bank(tiles.size(), 0);
        for (int y = 1; y < h - 1; y++)
            for (int x = 1; x < w - 1; x++) {
                if (at(x, y).ground == G_WATER) continue;
                for (int k = 0; k < 8; k++) {
                    static const int DX[8] = {1, -1, 0, 0, 1, 1, -1, -1}, DY[8] = {0, 0, 1, -1, 1, -1, 1, -1};
                    if (at(x + DX[k], y + DY[k]).ground == G_WATER) { bank[y * w + x] = 1; break; }
                }
            }
        for (size_t i = 0; i < tiles.size(); i++)
            if (bank[i] && (tiles[i].ground == G_GRASS || tiles[i].ground == G_SAND || tiles[i].ground == G_WASTE)) {
                tiles[i].ground = G_DIRT;
                if (tiles[i].solid == S_TREE && (hash2((int)(i % w), (int)(i / w), s2) & 1)) { tiles[i].solid = S_NONE; tiles[i].hp = 0; }
            }
    }

    // The city (0.11v; 0.13v: always to the west, across the woods): too far to walk to
    // and back in a day. You need a car.
    {
        const Region& C = REGIONS[RG_CITY_W];
        Rng cr(mix64(seed ^ 0xC17A11E5ull));
        Gen cg{*this, cr, day};
        cg.roadW = g.roadW;
        cg.city(C.x0 + 17, C.y0 + 20, C.w - 30, C.h - 40);
        g.floorPlans.insert(g.floorPlans.end(), cg.floorPlans.begin(), cg.floorPlans.end());
    }
    auto nearCity = [&](int x, int y, int margin) {
        for (const CityZone& c : cities)
            if (x >= c.x0 - margin && y >= c.y0 - margin && x < c.x0 + c.w + margin && y < c.y0 + c.h + margin) return true;
        return false;
    };

    // Points of interest on the home ground, as the old map had them.
    std::vector<Poi> pois;
    int wantPois = rng.irange(9, 12);
    for (int attempt = 0; attempt < 600 && (int)pois.size() < wantPois; attempt++) {
        int x = rng.irange(HOME.x0 + 22, HOME.x0 + HOME.w - 23), y = rng.irange(HOME.y0 + 22, HOME.y0 + HOME.h - 23);
        int dx = x - homeTx, dy = y - homeTy;
        if (dx * dx + dy * dy < 50 * 50) continue;
        if (nearCity(x, y, 26) || g.nearWater(x, y, 13)) continue;
        bool ok = true;
        for (auto& p : pois) if ((p.x - x) * (p.x - x) + (p.y - y) * (p.y - y) < 38 * 38) ok = false;
        if (!ok) continue;
        float q = g.qualityAt(x, y);
        int type;
        float r = rng.f();
        if (r < 0.40f) type = POI_TOWN;
        else if (r < 0.62f) type = POI_FARM;
        else if (r < 0.82f) type = POI_WAREHOUSE;
        else type = POI_MILITARY;
        if (type == POI_MILITARY && q < 0.3f) type = POI_WAREHOUSE;
        pois.push_back({x, y, type});
    }

    // Roads: home to the closest POIs, and each POI to its nearest neighbour.
    std::vector<int> order(pois.size());
    for (size_t i = 0; i < pois.size(); i++) order[i] = (int)i;
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        auto d = [&](int i) { return (pois[i].x - homeTx) * (pois[i].x - homeTx) + (pois[i].y - homeTy) * (pois[i].y - homeTy); };
        return d(a) < d(b);
    });
    for (int i = 0; i < std::min<int>(3, (int)order.size()); i++) g.road(homeTx, homeTy + 9, pois[order[i]].x, pois[order[i]].y);
    for (size_t i = 0; i < pois.size(); i++) {
        int best = -1, bd = 1 << 30;
        for (size_t j = 0; j < pois.size(); j++) {
            if (i == j) continue;
            int d = (pois[i].x - pois[j].x) * (pois[i].x - pois[j].x) + (pois[i].y - pois[j].y) * (pois[i].y - pois[j].y);
            if (d < bd) { bd = d; best = (int)j; }
        }
        if (best >= 0 && rng.chance(0.8f)) g.road(pois[i].x, pois[i].y, pois[best].x, pois[best].y);
    }
    // Highways (0.11v; 0.13v: the island's crossroads): four lanes west to the city and
    // east to the radio hill below the bunker, north to the army base and south to the
    // suburb beside it, over the water on bridges. A bridge whose far side has not
    // opened yet is closed by a roadblock halfway over.
    {
        Rng hr(mix64(seed ^ 0x416E3A7ull));
        Gen hw{*this, hr, day};
        hw.roadW = 4;
        const int hy = homeTy + 12, vx = homeTx - 16;
        const Region &C = REGIONS[RG_CITY_W], &M = REGIONS[RG_MILITARY], &E = REGIONS[RG_EAST], &S = REGIONS[RG_SOUTH];
        hw.road(vx, hy, C.x0 + C.w - 14, hy);
        hw.road(vx, hy, E.x0 + E.w / 2, hy);
        hw.road(vx, hy, vx, M.y0 + M.h / 2);
        hw.road(vx, hy, vx, S.y0 + S.h / 2);
        auto gapX = [&](int leftRegion, int rightRegion) { const Region& L = REGIONS[leftRegion]; return (L.x0 + L.w + REGIONS[rightRegion].x0) / 2; };
        auto gapY = [&](int upRegion, int downRegion) { const Region& U = REGIONS[upRegion]; return (U.y0 + U.h + REGIONS[downRegion].y0) / 2; };
        int x;
        x = gapX(RG_CITY_W, RG_FOREST_W); if (day < REGIONS[RG_CITY_W].day) hw.roadblock(x - 1, hy, x, hy + 3, REGIONS[RG_CITY_W].day);
        x = gapX(RG_FOREST_W, RG_HOME); if (day < REGIONS[RG_FOREST_W].day) hw.roadblock(x - 1, hy, x, hy + 3, REGIONS[RG_FOREST_W].day);
        x = gapX(RG_HOME, RG_EAST); if (day < REGIONS[RG_EAST].day) hw.roadblock(x - 1, hy, x, hy + 3, REGIONS[RG_EAST].day);
        int y;
        y = gapY(RG_MILITARY, RG_FOREST_N); if (day < REGIONS[RG_MILITARY].day) hw.roadblock(vx, y - 1, vx + 3, y, REGIONS[RG_MILITARY].day);
        y = gapY(RG_FOREST_N, RG_HOME); if (day < REGIONS[RG_FOREST_N].day) hw.roadblock(vx, y - 1, vx + 3, y, REGIONS[RG_FOREST_N].day);
        y = gapY(RG_HOME, RG_SOUTH); if (day < REGIONS[RG_SOUTH].day) hw.roadblock(vx, y - 1, vx + 3, y, REGIONS[RG_SOUTH].day);
    }
    // Re-clear the compound gate area after roads.
    for (int y = homeTy + 4; y <= homeTy + 8; y++)
        for (int x = homeTx - 1; x <= homeTx + 1; x++) if (at(x, y).solid != S_BUNKER) at(x, y).solid = S_NONE;

    // A worn dirt shoulder either side of every road, so asphalt never butts
    // straight into grass; the grass/earth corner art then draws that seam for us.
    {
        std::vector<uint8_t> shoulder(tiles.size(), 0);
        for (int y = 1; y < h - 1; y++)
            for (int x = 1; x < w - 1; x++) {
                if (at(x, y).ground != G_ROAD || cityAt(x, y) >= 0) continue;   // a city's streets have kerbs
                for (int oy = -1; oy <= 1; oy++)
                    for (int ox = -1; ox <= 1; ox++)
                        if ((at(x + ox, y + oy).ground == G_GRASS || at(x + ox, y + oy).ground == G_WASTE) &&
                            !(at(x + ox, y + oy).flags & TF_KERB) && cityAt(x + ox, y + oy) < 0)
                            shoulder[(y + oy) * w + (x + ox)] = 1;
            }
        for (size_t i = 0; i < tiles.size(); i++)
            if (shoulder[i]) tiles[i].ground = G_DIRT;
    }

    for (auto& p : pois) {
        switch (p.type) {
        case POI_TOWN: g.town(p.x, p.y); break;
        case POI_WAREHOUSE: g.warehouse(p.x, p.y); break;
        case POI_MILITARY: g.military(p.x, p.y); break;
        default: g.farm(p.x, p.y); break;
        }
    }

    // Scattered cabins and sheds.
    g.bkind = BK_CABIN;
    for (int i = 0; i < 22; i++) {
        int bw = rng.irange(5, 8), bh = rng.irange(5, 7);
        int x = rng.irange(HOME.x0 + 8, HOME.x0 + HOME.w - 16), y = rng.irange(HOME.y0 + 8, HOME.y0 + HOME.h - 16);
        float ruin = rng.chance(0.4f) ? 0.15f : 0;
        int wall = ruin > 0 ? S_WALL_BRICK : (rng.chance(0.55f) ? S_WALL_WOOD : S_WALL_CONCRETE);
        if (g.building(x, y, bw, bh, wall, rng.chance(0.5f) ? G_FLOOR_WOOD : G_FLOOR_CONCRETE, LootKind::Generic, 1, 2, ruin))
            if (rng.chance(0.5f)) g.spawnGroup(x + bw / 2, y + bh / 2, 1, 5);
    }
    // The woods (0.13v): trails off the highway to clearings in the trees.
    {
        Rng fr(mix64(seed ^ 0xF02E57ull));
        Gen fg{*this, fr, day};
        fg.forest(REGIONS[RG_FOREST_W], true);
        fg.forest(REGIONS[RG_FOREST_N], false);
    }
    // The land further out (0.13v): the army base, the radio hill and the suburb. Each on
    // dice of its own, so what one of them holds never moves another.
    {
        Rng ar(mix64(seed ^ 0xA7B45Eull));
        Gen ag{*this, ar, day};
        ag.armyBase(REGIONS[RG_MILITARY], homeTx - 16);
        Rng er(mix64(seed ^ 0xEA57ull));
        Gen eg{*this, er, day};
        eg.radioHill(REGIONS[RG_EAST]);
        Rng sr(mix64(seed ^ 0x50B0ull));
        Gen sg{*this, sr, day};
        sg.suburb(REGIONS[RG_SOUTH], homeTx - 16);
        for (Gen* x : {&ag, &eg, &sg}) g.floorPlans.insert(g.floorPlans.end(), x->floorPlans.begin(), x->floorPlans.end());
        if (airdrops && day >= RADIO_DAY) {
            Rng dr(mix64(seed ^ 0xD209ull));
            Gen dg{*this, dr, day};
            dg.supplyDrop();
        }
    }

    // Abandoned cars and street lights along the roads.
    {
        std::vector<std::pair<int, int>> roadTiles;
        for (int y = 4; y < h - 4; y++)
            for (int x = 4; x < w - 4; x++)
                if (at(x, y).ground == G_ROAD && at(x, y).solid == S_NONE) roadTiles.push_back({x, y});
        // From day 5 the roads are kept clearer: fewer wrecks, only at the kerb, and
        // none on the city streets (those have wrecks of their own).
        int wantCars = big ? std::min<int>(80, (int)roadTiles.size() / 140) : std::min<int>(46, (int)roadTiles.size() / 45);
        for (int i = 0; i < wantCars && !roadTiles.empty(); i++) {
            auto [cx, cy] = roadTiles[rng.next() % roadTiles.size()];
            uint8_t variant = (uint8_t)rng.next();
            if (big) {
                bool kerb = at(cx - 1, cy).ground != G_ROAD || at(cx + 1, cy).ground != G_ROAD;
                if (cityAt(cx, cy) >= 0 || !kerb) continue;
            }
            Art::Piece art = Art::car(variant);
            int cw = art.valid() ? (art.sprite->w + TILE - 1) / TILE : 2;
            int chh = art.valid() ? (art.sprite->h + TILE - 1) / TILE : 3;
            bool free = true;
            for (int y = cy - chh + 1; y <= cy && free; y++)
                for (int x = cx; x < cx + cw && free; x++) {
                    if (!inBounds(x, y) || at(x, y).solid != S_NONE || blocksMove(x, y)) free = false;
                    int dx = x - homeTx, dy = y - homeTy;
                    if (dx * dx + dy * dy < 14 * 14) free = false;
                }
            if (!free) continue;
            
            WorldProp prop;
            prop.kind = PROP_CAR;
            prop.variant = variant;
            prop.pos = Vec2(cx * TILE + cw * TILE * 0.5f, (cy + 1) * (float)TILE);
            prop.flipX = false;
            
            // Tiles reserve the placement; movement and bullets sample the sprite's
            // retained alpha mask, so transparent corners never act like a box.
            for (int y = cy - chh + 1; y <= cy; y++)
                for (int x = cx; x < cx + cw; x++) { at(x, y).solid = S_CAR; at(x, y).hp = -1; }
            props.push_back(prop);
        }
        // Street lights stand on the verge, never on the asphalt: walk out of the
        // chosen road tile until the road ends and stand the pole on the first clear
        // tile past the edge, with the arm leaning back over the road it lights.
        static const int LDX[4] = {0, 0, 1, -1}, LDY[4] = {-1, 1, 0, 0};
        for (int i = 0; i < (big ? 96 : 60) && !roadTiles.empty(); i++) {
            auto [cx, cy] = roadTiles[rng.next() % roadTiles.size()];
            int first = (int)(rng.next() % 4);
            for (int k = 0; k < 4; k++) {
                int dir = (first + k) % 4;
                int x = cx, y = cy, steps = 0;
                while (steps < 3 && inBounds(x + LDX[dir], y + LDY[dir]) &&
                       at(x + LDX[dir], y + LDY[dir]).ground == G_ROAD) {
                    x += LDX[dir]; y += LDY[dir]; steps++;
                }
                int vx = x + LDX[dir], vy = y + LDY[dir];
                if (!inBounds(vx, vy)) continue;
                const Tile& verge = at(vx, vy);
                if (verge.solid != S_NONE || verge.deco || blocksMove(vx, vy)) continue;   // clear ground only
                if (verge.ground != G_GRASS && verge.ground != G_DIRT && verge.ground != G_SAND && verge.ground != G_WASTE) continue;
                int hx = vx - homeTx, hy = vy - homeTy;
                if (hx * hx + hy * hy < 14 * 14) continue;
                // Variants: 0 arm to the right, 1 arm up, 2 arm down. The arm has to
                // point back the way we came, since that is where the road is.
                placeStreetLight(vx, vy, dir == 0 ? 2 : (dir == 1 ? 1 : 0), dir == 2);
                break;
            }
        }
    }

    // Loose bags lying around.
    for (int i = 0; i < 26; i++) {
        int x = rng.irange(HOME.x0 + 5, HOME.x0 + HOME.w - 6), y = rng.irange(HOME.y0 + 5, HOME.y0 + HOME.h - 6);
        int dx = x - homeTx, dy = y - homeTy;
        if (blocksMove(x, y) || dx * dx + dy * dy < 20 * 20 || g.reserved(x, y)) continue;
        int id = addContainer(tileCenter(x, y), CK_BAG, -1, -1, (uint8_t)rng.next());
        g.fillContainer(id, g.qualityAt(x, y), LootKind::Bag, 1, 3);
    }

    // Wandering enemies.
    int wanderers = 9 + day;
    for (int i = 0; i < wanderers; i++) {
        int x = rng.irange(HOME.x0 + 6, HOME.x0 + HOME.w - 7), y = rng.irange(HOME.y0 + 6, HOME.y0 + HOME.h - 7);
        g.spawnGroup(x, y, 1, 3);
    }

    furnishBuildings();
    // The mechanic's yard (0.11v), after the furniture: his workshop has its own.
    if (day >= CITY_DAY) {
        Rng gr(mix64(seed ^ 0x6A2A6Eull));
        Gen gg{*this, gr, day};
        gg.garage();
    }

    // The bunker over the hatch is roofed like any other building, and the roof fades
    // as you walk in. Its three-tile opening faces south. Added last so nothing above
    // treats it as a place for loot or raiders.
    {
        Building b;
        b.x0 = homeTx - 3;
        b.y0 = homeTy - 3;
        b.w = b.h = 7;
        b.style = 0;
        b.kind = BK_BUNKER;
        for (int y = b.y0; y < b.y0 + b.h; y++)
            for (int x = b.x0; x < b.x0 + b.w; x++)
                if ((x == b.x0 || y == b.y0 || x == b.x0 + b.w - 1 || y == b.y0 + b.h - 1) && at(x, y).solid != S_NONE) b.walls++;
        for (int i = -1; i <= 1; i++) {
            b.doorX[b.doorCount] = (int16_t)(homeTx + i);
            b.doorY[b.doorCount] = (int16_t)(homeTy + 3);
            b.doorCount++;
        }
        buildings.push_back(b);
    }

    outW = w;
    outH = h;
    dungeons.clear();
    bool crypts = withCrypts && day >= CRYPT_DAY;
    if (crypts || !g.floorPlans.empty()) generateBelow(*this, seed, day, g.floorPlans, crypts);

    if (zombieSpawns) {
        // Zombies mode. Its own dice, so the rest of the layout matches a normal day.
        Rng zr(mix64(seed ^ 0x2B1E5D0Dull));
        std::vector<EnemySpawn> packs;
        auto pack = [&](Vec2 c, int n) {
            for (int i = 0; i < n; i++)
                for (int attempt = 0; attempt < 12; attempt++) {
                    int tx = toTile(c.x + zr.range(-40, 40)), ty = toTile(c.y + zr.range(-40, 40));
                    if (!inBounds(tx, ty) || blocksMove(tx, ty) || at(tx, ty).ground == G_WATER) continue;
                    int dx = tx - homeTx, dy = ty - homeTy;
                    if (dx * dx + dy * dy < HOME_SAFE_TILES * HOME_SAFE_TILES) continue;
                    packs.push_back({tileCenter(tx, ty) + Vec2(zr.range(-4, 4), zr.range(-4, 4)), EnemyType::Zombie});
                    break;
                }
        };
        for (const EnemySpawn& s : spawns) {
            if (s.crypt) {
                // Down in the catacombs a gang becomes a few more of the dead, where it stood.
                int n = s.type == EnemyType::Zombie ? 1 : zr.irange(2, 3);
                for (int i = 0; i < n; i++) packs.push_back({s.pos + Vec2(zr.range(-6, 6), zr.range(-6, 6)), EnemyType::Zombie, true, -1, s.boss && i == 0});
                continue;
            }
            pack(s.pos, zr.irange(2, 4) + day / 5);
        }
        // More packs out on the open ground, away from the houses.
        int extra = 12 + day;
        for (int k = 0, tries = 0; k < extra && tries < 4000; tries++) {
            int tx = zr.irange(8, outW - 9), ty = zr.irange(8, outH - 9);
            int dx = tx - homeTx, dy = ty - homeTy;
            if (!unlockedAt(tx, ty) || blocksMove(tx, ty) || at(tx, ty).ground == G_WATER || dx * dx + dy * dy < (HOME_SAFE_TILES + 6) * (HOME_SAFE_TILES + 6)) continue;
            pack(tileCenter(tx, ty), zr.irange(4, 7));
            k++;
        }
        spawns = std::move(packs);
    }

    // Nothing waits on the land that has not opened yet (0.13v): it is only scenery. Nor
    // in the survivors' camp.
    spawns.erase(std::remove_if(spawns.begin(), spawns.end(), [&](const EnemySpawn& sp) {
        if (sp.crypt) return false;
        if (medicPos.x > 0 && dist(sp.pos, (medicPos + buyerPos) * 0.5f) < 22 * TILE) return true;
        Vec2 p = surfacePos(sp.pos);
        int tx = toTile(p.x), ty = toTile(p.y);
        if (tx >= outW || ty >= outH) return false;
        return !unlockedAt(tx, ty);
    }), spawns.end());

    // The sea is on every map (0.13v): the coasts show the shape of the island from the
    // start, the land itself is revealed as you go.
    for (int y = 0; y < outH; y++)
        for (int x = 0; x < outW; x++)
            if (at(x, y).ground == G_WATER) at(x, y).explored = 1;

    dressWorld(*this, seed);
    indexProps();
    rebuildMap();
    if (const char* dump = std::getenv("OUTBOUND_MAPDUMP")) {
        // Dev: the whole outside as a picture, everything seen (binary PPM).
        if (FILE* f = std::fopen(dump, "wb")) {
            std::fprintf(f, "P6 %d %d 255\n", outW, outH);
            for (int y = 0; y < outH; y++)
                for (int x = 0; x < outW; x++) {
                    const Tile& t = at(x, y);
                    bool obstacle = t.solid != S_NONE && solidInfo(t.solid).blocksMove && t.solid != S_FURNITURE;
                    int pc = obstacle ? solidInfo(t.solid).mapColor : GROUND_MAP_COLOR[t.ground];
                    if (t.solid == S_ROADBLOCK) pc = P_CORAL;
                    uint32_t hex = PALETTE_HEX[pc];
                    unsigned char px[3] = {(unsigned char)(hex >> 16), (unsigned char)(hex >> 8), (unsigned char)hex};
                    std::fwrite(px, 1, 3, f);
                }
            std::fclose(f);
        }
    }
    if (std::getenv("OUTBOUND_DRESS_LOG") || std::getenv("OUTBOUND_MAPDUMP")) {
        std::fprintf(stderr, "[world] radio %d,%d  camp %d,%d  drop %d,%d\n", toTile(radioPos.x), toTile(radioPos.y), toTile(medicPos.x), toTile(medicPos.y),
                     toTile(dropPos.x), toTile(dropPos.y));
        for (const Dungeon& d : dungeons) std::fprintf(stderr, "[world] %s door %d,%d\n", d.metro ? "metro" : "crypt", toTile(d.door.x), toTile(d.door.y));
    }
    std::fprintf(stderr, "[world] day %d: %dx%d, %zu cities, %zu floors, %zu spawns, %zu containers, %zu props\n", day, outW, outH,
                 cities.size(), floors.size(), spawns.size(), containers.size(), props.size());
}

void World::generateBase() {
    // One room, sized to what is actually in it. See STATIONS in base.cpp.
    w = 18;
    h = 10;
    outW = w;
    outH = h;
    dungeons.clear();
    tiles.assign(w * h, Tile());
    containers.clear();
    props.clear();
    spawns.clear();
    buildings.clear();
    cities.clear();
    blocks.clear();
    roofProps.clear();
    floors.clear();
    stairs.clear();
    patrols.clear();
    propAt.assign(tiles.size(), -1);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            Tile& t = at(x, y);
            t.ground = G_BASE_FLOOR;
            t.explored = 1;
            if (x == 0 || y == 0 || x == w - 1 || y == h - 1) { t.solid = S_BUNKER; t.hp = -1; }
        }
    homeTx = 3;
    homeTy = h - 2;
    homePos = tileCenter(homeTx, homeTy);
    rebuildMap();
}
