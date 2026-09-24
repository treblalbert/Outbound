// Procedurally painted placeholder pixel art, restricted to the Vanilla Milkshake palette.
#include "sprites.h"
#include "core.h"
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>

#include "assets.h"

namespace Sprites {

static const char* NAMES[] = {
#define S(n) #n,
    SPRITE_LIST
#undef S
};

static int g_animationFrame = 0;

const char* name(int id) { return (id >= 0 && id < COUNT) ? NAMES[id] : "?"; }
bool setAnimationFrame(int frame) {
    frame = ((frame % 6) + 6) % 6;
    if (frame == g_animationFrame) return false;
    g_animationFrame = frame;
    return true;
}

namespace {

constexpr int T = -1;  // transparent

struct Canvas {
    int px[16][16];
    Canvas() { clear(); }
    void clear() { for (auto& r : px) for (int& p : r) p = T; }
    void set(int x, int y, int p) { if (x >= 0 && y >= 0 && x < 16 && y < 16) px[y][x] = p; }
    int get(int x, int y) const { return (x >= 0 && y >= 0 && x < 16 && y < 16) ? px[y][x] : T; }
    void fill(int x, int y, int w, int h, int p) { for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) set(i, j, p); }
    void outline(int x, int y, int w, int h, int p) {
        for (int i = x; i < x + w; i++) { set(i, y, p); set(i, y + h - 1, p); }
        for (int j = y; j < y + h; j++) { set(x, j, p); set(x + w - 1, j, p); }
    }
    void disc(float cx, float cy, float r, int p) {
        for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            if (dx * dx + dy * dy <= r * r) set(x, y, p);
        }
    }
    void ellipse(float cx, float cy, float rx, float ry, int p) {
        for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) {
            float dx = (x + 0.5f - cx) / rx, dy = (y + 0.5f - cy) / ry;
            if (dx * dx + dy * dy <= 1) set(x, y, p);
        }
    }
    void ring(float cx, float cy, float r0, float r1, int p) {
        for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy, d = dx * dx + dy * dy;
            if (d <= r1 * r1 && d >= r0 * r0) set(x, y, p);
        }
    }
    void lineP(int x0, int y0, int x1, int y1, int p) {
        int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
        for (;;) {
            set(x0, y0, p);
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }
    void speckle(Rng& r, int p, float density, bool onlyOpaque = true) {
        for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++)
            if (r.chance(density) && (!onlyOpaque || px[y][x] != T)) px[y][x] = p;
    }
    // Draw a dark outline around all opaque pixels (into transparent neighbors).
    void autoOutline(int p) {
        Canvas c = *this;
        for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) {
            if (c.px[y][x] != T) continue;
            if (c.get(x - 1, y) != T || c.get(x + 1, y) != T || c.get(x, y - 1) != T || c.get(x, y + 1) != T) px[y][x] = p;
        }
    }
    // Shade the bottom-right edge pixels of opaque regions.
    void edgeShade(int p) {
        Canvas c = *this;
        for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) {
            if (c.px[y][x] == T) continue;
            if (c.get(x + 1, y) == T || c.get(x, y + 1) == T) px[y][x] = p;
        }
    }
};

void paintSprite(int id, Canvas& c) {
    Rng r(0xC0FFEE + id * 7919);
    switch (id) {
    case WHITE: c.fill(0, 0, 16, 16, P_WHITE); break;
    case GRASS0: case GRASS1: case GRASS2:
        c.fill(0, 0, 16, 16, P_LGREEN);
        c.speckle(r, P_SAGE, 0.10f);
        c.speckle(r, P_YGREEN, 0.06f);
        if (id == GRASS1) { c.set(4, 5, P_SAGE); c.set(5, 4, P_SAGE); c.set(11, 11, P_SAGE); c.set(12, 10, P_SAGE); }
        if (id == GRASS2) { c.set(5, 6, P_PINK); c.set(11, 3, P_WHITE); c.set(9, 12, P_YELLOW); }
        break;
    case DIRT:
        c.fill(0, 0, 16, 16, P_TAN);
        c.speckle(r, P_ORANGE, 0.10f);
        c.speckle(r, P_BEIGE, 0.07f);
        c.speckle(r, P_PURPLE, 0.02f);
        break;
    case SAND:
        c.fill(0, 0, 16, 16, P_CREAM);
        c.speckle(r, P_YELLOW, 0.10f);
        c.speckle(r, P_BEIGE, 0.07f);
        break;
    case WATER0: case WATER1: {
        c.fill(0, 0, 16, 16, P_BLUE);
        int o = id == WATER1 ? 4 : 0;
        for (int k = 0; k < 3; k++) {
            int y = 2 + k * 5, x = (k * 6 + o) % 16;
            c.set(x, y, P_LAVENDER); c.set(x + 1, y - 1, P_LAVENDER); c.set(x + 2, y, P_LAVENDER); c.set(x + 3, y - 1, P_LAVENDER);
        }
        c.set((9 + o) % 16, 7, P_WHITE);
        break;
    }
    case ROAD:
        // Proper pixel-art road texture with asphalt feel and markings
        c.fill(0, 0, 16, 16, P_BEIGE);
        // Base asphalt texture - darker with noise
        c.fill(0, 0, 16, 16, P_TAN);
        // Add asphalt speckle/noise
        for (int i = 0; i < 20; i++) {
            int x = r.irange(0, 15);
            int y = r.irange(0, 15);
            int col = r.chance(0.5f) ? P_LAVENDER : P_CREAM;
            c.set(x, y, col);
        }
        // Road edge markings (dashed center line style)
        // Vertical road pattern: dashed line in middle
        c.fill(7, 0, 2, 16, P_CREAM);
        // Add small gaps in the center line for dashed effect
        c.set(7, 3, P_TAN); c.set(8, 3, P_TAN);
        c.set(7, 7, P_TAN); c.set(8, 7, P_TAN);
        c.set(7, 11, P_TAN); c.set(8, 11, P_TAN);
        c.set(7, 15, P_TAN); c.set(8, 15, P_TAN);
        // Road shoulder edges (darker border)
        c.fill(0, 0, 1, 16, P_LAVENDER);
        c.fill(15, 0, 1, 16, P_LAVENDER);
        // Add some cracks and wear
        c.set(3, 5, P_LAVENDER);
        c.set(12, 8, P_LAVENDER);
        c.set(5, 12, P_LAVENDER);
        c.set(10, 2, P_LAVENDER);
        // Corner detail - where road meets grass
        c.set(0, 0, P_ORANGE);
        c.set(15, 0, P_ORANGE);
        c.set(0, 15, P_ORANGE);
        c.set(15, 15, P_ORANGE);
        break;
    case BRIDGE:
        c.fill(0, 0, 16, 16, P_TAN);
        for (int y = 3; y < 16; y += 4) c.fill(0, y, 16, 1, P_ORANGE);
        c.fill(0, 0, 1, 16, P_PURPLE); c.fill(15, 0, 1, 16, P_PURPLE);
        break;
    case FLOOR_WOOD:
        c.fill(0, 0, 16, 16, P_TAN);
        for (int x = 3; x < 16; x += 4) c.fill(x, 0, 1, 16, P_ORANGE);
        c.set(1, 5, P_ORANGE); c.set(9, 12, P_ORANGE); c.set(6, 2, P_BEIGE);
        break;
    case FLOOR_CONCRETE:
        c.fill(0, 0, 16, 16, P_BEIGE);
        c.speckle(r, P_LAVENDER, 0.05f);
        c.fill(15, 0, 1, 16, P_LAVENDER); c.fill(0, 15, 16, 1, P_LAVENDER);
        break;
    case FLOOR_TILE:
        for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++)
            c.set(x, y, ((x / 8) + (y / 8)) % 2 ? P_MINT : P_WHITE);
        c.fill(0, 7, 16, 1, P_BLUE); c.fill(7, 0, 1, 16, P_BLUE);
        break;
    case RUBBLE:
        c.fill(0, 0, 16, 16, P_BEIGE);
        c.speckle(r, P_LAVENDER, 0.2f);
        c.speckle(r, P_PURPLE, 0.06f);
        break;
    case BASE_FLOOR:
        c.fill(0, 0, 16, 16, P_LAVENDER);
        c.fill(0, 15, 16, 1, P_PURPLE); c.fill(15, 0, 1, 16, P_PURPLE);
        c.set(2, 2, P_BLUE); c.set(13, 2, P_BLUE); c.set(2, 13, P_BLUE); c.set(13, 13, P_BLUE);
        break;
    case WALL_BRICK:
        c.fill(0, 0, 16, 16, P_CORAL);
        for (int row = 0; row < 4; row++) {
            int y = row * 4;
            c.fill(0, y + 3, 16, 1, P_PURPLE);
            int off = row % 2 ? 4 : 0;
            for (int x = off; x < 16; x += 8) c.fill(x, y, 1, 3, P_PURPLE);
            for (int x = 0; x < 16; x++) if (c.get(x, y) == P_CORAL) c.set(x, y, P_PINK);
        }
        break;
    case WALL_CONCRETE:
        c.fill(0, 0, 16, 16, P_BEIGE);
        c.speckle(r, P_LAVENDER, 0.08f);
        c.fill(0, 0, 16, 1, P_WHITE); c.fill(0, 0, 1, 16, P_WHITE);
        c.fill(0, 15, 16, 1, P_PURPLE); c.fill(15, 0, 1, 16, P_PURPLE);
        break;
    case WALL_WOOD:
        c.fill(0, 0, 16, 16, P_ORANGE);
        for (int x = 4; x < 16; x += 5) c.fill(x, 0, 1, 16, P_TAN);
        c.fill(0, 15, 16, 1, P_PURPLE);
        c.set(2, 3, P_TAN); c.set(12, 10, P_TAN);
        break;
    case TREE:
        c.disc(8, 8.5f, 7.2f, P_SAGE);
        c.edgeShade(P_PURPLE);
        c.disc(6.5f, 6.5f, 3.6f, P_LGREEN);
        c.set(5, 5, P_YGREEN); c.set(10, 4, P_LGREEN); c.set(11, 9, P_LGREEN);
        c.autoOutline(P_DARK);
        break;
    case BUSH:
        c.disc(6, 9, 4.2f, P_SAGE); c.disc(10, 8, 4.2f, P_SAGE);
        c.edgeShade(P_PURPLE);
        c.set(6, 7, P_LGREEN); c.set(10, 6, P_LGREEN); c.set(9, 7, P_LGREEN);
        c.autoOutline(P_DARK);
        break;
    case ROCK:
        c.ellipse(8, 8.5f, 6.8f, 6.0f, P_LAVENDER);
        c.edgeShade(P_PURPLE);
        c.fill(5, 5, 3, 2, P_WHITE); c.set(10, 6, P_BLUE);
        c.autoOutline(P_DARK);
        break;
    case CRATE:
        c.fill(1, 1, 14, 14, P_ORANGE);
        c.outline(1, 1, 14, 14, P_PURPLE);
        c.lineP(2, 2, 13, 13, P_TAN); c.lineP(13, 2, 2, 13, P_TAN);
        c.outline(2, 2, 12, 12, P_TAN);
        break;
    case FENCE:
        c.fill(0, 6, 16, 1, P_TAN); c.fill(0, 9, 16, 1, P_TAN);
        c.fill(2, 5, 3, 6, P_ORANGE); c.fill(11, 5, 3, 6, P_ORANGE);
        c.autoOutline(P_PURPLE);
        break;
    case SANDBAG:
        for (int row = 0; row < 3; row++)
            for (int k = 0; k < 3; k++) {
                int x = (row % 2 ? -2 : 1) + k * 6, y = 1 + row * 5;
                c.fill(x, y, 6, 4, P_CREAM);
                c.fill(x, y + 3, 6, 1, P_TAN);
            }
        c.fill(0, 0, 16, 1, P_TAN);
        c.outline(0, 0, 16, 16, P_PURPLE);
        break;
    case BOUNDARY:
        c.fill(0, 0, 16, 16, P_PURPLE);
        c.speckle(r, P_DARK, 0.3f);
        c.disc(5, 5, 3.5f, P_SAGE); c.disc(12, 11, 3.5f, P_SAGE);
        c.set(4, 4, P_LGREEN); c.set(11, 10, P_LGREEN);
        break;
    case BUNKER_WALL:
        c.fill(0, 0, 16, 16, P_DARK);
        c.fill(1, 1, 14, 13, P_PURPLE);
        c.fill(1, 1, 14, 1, P_LAVENDER);
        c.set(3, 4, P_DARK); c.set(12, 4, P_DARK); c.set(3, 11, P_DARK); c.set(12, 11, P_DARK);
        break;
    case HATCH:
        for (int i = 0; i < 16; i++) { c.set(i, 0, (i / 2) % 2 ? P_DARK : P_YELLOW); c.set(i, 15, (i / 2) % 2 ? P_YELLOW : P_DARK);
                                        c.set(0, i, (i / 2) % 2 ? P_DARK : P_YELLOW); c.set(15, i, (i / 2) % 2 ? P_YELLOW : P_DARK); }
        c.fill(1, 1, 14, 14, P_PURPLE);
        c.disc(8, 8, 6, P_LAVENDER);
        c.ring(8, 8, 5, 6, P_DARK);
        c.fill(4, 7, 8, 2, P_BEIGE);
        break;
    case STUMP:
        c.disc(8, 8, 4, P_TAN); c.ring(8, 8, 3, 4, P_ORANGE); c.set(8, 8, P_ORANGE);
        c.autoOutline(P_DARK);
        break;
    case CRACK1:
        c.lineP(3, 4, 7, 8, P_DARK); c.lineP(7, 8, 6, 12, P_DARK);
        break;
    case CRACK2:
        c.lineP(3, 4, 7, 8, P_DARK); c.lineP(7, 8, 6, 12, P_DARK); c.lineP(7, 8, 12, 6, P_DARK); c.lineP(12, 6, 14, 2, P_DARK);
        break;
    case CRACK3:
        c.lineP(1, 3, 7, 8, P_DARK); c.lineP(7, 8, 5, 14, P_DARK); c.lineP(7, 8, 12, 6, P_DARK); c.lineP(12, 6, 14, 1, P_DARK);
        c.lineP(7, 8, 14, 12, P_DARK); c.lineP(4, 11, 1, 12, P_DARK); c.speckle(r, P_PURPLE, 0.05f, false);
        break;
    case C_CRATE:
        c.fill(2, 3, 12, 10, P_ORANGE);
        c.fill(2, 7, 12, 2, P_YELLOW);
        c.fill(2, 3, 12, 1, P_CREAM);
        c.edgeShade(P_TAN);
        c.autoOutline(P_DARK);
        break;
    case C_LOCKER:
        c.fill(3, 1, 10, 14, P_BLUE);
        c.fill(8, 1, 1, 14, P_PURPLE);
        for (int y = 3; y < 7; y += 2) { c.fill(4, y, 3, 1, P_LAVENDER); c.fill(10, y, 2, 1, P_LAVENDER); }
        c.set(7, 9, P_YELLOW); c.set(9, 9, P_YELLOW);
        c.autoOutline(P_DARK);
        break;
    case C_CABINET:
        c.fill(2, 2, 12, 12, P_BEIGE);
        c.fill(2, 6, 12, 1, P_TAN); c.fill(2, 10, 12, 1, P_TAN);
        c.fill(7, 4, 2, 1, P_PURPLE); c.fill(7, 8, 2, 1, P_PURPLE); c.fill(7, 12, 2, 1, P_PURPLE);
        c.autoOutline(P_DARK);
        break;
    case C_MILCRATE:
        c.fill(1, 3, 14, 10, P_SAGE);
        c.fill(1, 3, 14, 1, P_LGREEN);
        c.fill(3, 6, 1, 4, P_YGREEN); c.fill(5, 6, 1, 4, P_YGREEN); c.fill(3, 8, 3, 1, P_YGREEN);
        c.fill(12, 5, 2, 6, P_PURPLE);
        c.autoOutline(P_DARK);
        break;
    case C_BAG:
        c.ellipse(8, 9, 6.5f, 4, P_TAN);
        c.edgeShade(P_ORANGE);
        c.lineP(4, 7, 8, 3, P_PURPLE); c.lineP(8, 3, 12, 7, P_PURPLE);
        c.fill(4, 9, 8, 1, P_PURPLE);
        c.autoOutline(P_DARK);
        break;
    case C_CORPSE:
        c.ellipse(9, 9, 5, 4, P_CORAL);
        c.ellipse(7, 8, 5.5f, 2.5f, P_PURPLE);
        c.disc(12.5f, 8, 2.2f, P_BEIGE);
        c.set(2, 6, P_PURPLE); c.set(2, 10, P_PURPLE);
        c.autoOutline(P_DARK);
        break;
    case C_TOOLBOX:
        c.fill(2, 6, 12, 8, P_CORAL);
        c.fill(2, 9, 12, 1, P_PURPLE);
        c.outline(5, 3, 6, 4, P_BEIGE);
        c.autoOutline(P_DARK);
        break;
    case PLAYER: case SCAV: case BANDIT: case HEAVY: case SNIPER: case SHADE: case TRADER: {
        int body = P_BLUE, head = P_TAN, hand = P_CREAM, pack = P_SAGE;
        float ry = 5.5f;
        if (id == SCAV) { body = P_TAN; head = P_BEIGE; pack = P_ORANGE; }
        if (id == BANDIT) { body = P_CORAL; head = P_DARK; pack = P_PURPLE; }
        if (id == HEAVY) { body = P_PURPLE; head = P_LAVENDER; pack = P_DARK; ry = 6.5f; }
        if (id == SNIPER) { body = P_SAGE; head = P_SAGE; pack = P_LGREEN; }
        if (id == SHADE) { body = P_DARK; head = P_DARK; hand = P_DARK; pack = P_PURPLE; }
        if (id == TRADER) { body = P_PINK; head = P_WHITE; pack = P_CORAL; }
        c.fill(2, 5, 3, 6, pack);
        c.ellipse(7.5f, 8, 3.2f, ry, body);
        c.edgeShade(id == SHADE ? P_PURPLE : P_PURPLE);
        c.fill(10, 5, 2, 2, hand);
        c.fill(10, 9, 2, 2, hand);
        c.disc(8, 8, 2.8f, head);
        if (id == HEAVY) c.ring(8, 8, 2, 2.9f, P_BEIGE);
        if (id == SNIPER) c.set(9, 7, P_YGREEN);
        if (id == SHADE) { c.set(10, 7, P_CORAL); c.set(10, 9, P_CORAL); c.set(1, 3, P_DARK); c.set(0, 12, P_DARK); c.set(3, 14, P_DARK); }
        c.autoOutline(P_DARK);
        break;
    }
    case BED:
        c.fill(1, 2, 14, 12, P_TAN);
        c.fill(2, 3, 12, 10, P_BLUE);
        c.fill(2, 3, 4, 10, P_WHITE);
        c.fill(7, 3, 1, 10, P_LAVENDER);
        c.autoOutline(P_DARK);
        break;
    case STASH:
        c.fill(1, 3, 14, 11, P_SAGE);
        c.fill(1, 3, 14, 3, P_LGREEN);
        c.fill(1, 6, 14, 1, P_PURPLE);
        c.fill(7, 7, 2, 3, P_YELLOW);
        c.autoOutline(P_DARK);
        break;
    case TERMINAL:
        c.fill(1, 4, 14, 10, P_PURPLE);
        c.fill(3, 2, 10, 7, P_DARK);
        c.fill(4, 3, 8, 5, P_MINT);
        c.fill(5, 4, 4, 1, P_WHITE); c.fill(5, 6, 5, 1, P_SAGE);
        c.fill(3, 11, 10, 2, P_LAVENDER);
        c.autoOutline(P_DARK);
        break;
    case WORKBENCH:
        c.fill(1, 3, 14, 10, P_TAN);
        c.fill(1, 3, 14, 1, P_CREAM);
        c.fill(3, 5, 5, 2, P_LAVENDER); c.fill(9, 5, 1, 5, P_CORAL); c.fill(8, 5, 3, 1, P_PURPLE);
        c.disc(5, 10, 1.6f, P_YELLOW);
        c.autoOutline(P_DARK);
        break;
    case LAMP:
        c.disc(8, 8, 4, P_ORANGE); c.disc(8, 8, 2.5f, P_YELLOW); c.set(7, 7, P_WHITE);
        c.autoOutline(P_DARK);
        break;
    case TABLE:
        c.fill(1, 3, 14, 10, P_TAN); c.fill(1, 3, 14, 1, P_CREAM); c.edgeShade(P_ORANGE);
        c.disc(5, 7, 1.5f, P_WHITE); c.fill(9, 8, 4, 2, P_BEIGE);
        c.autoOutline(P_DARK);
        break;
    case RUG:
        c.fill(0, 0, 16, 16, P_CORAL); c.outline(1, 1, 14, 14, P_PINK); c.outline(4, 4, 8, 8, P_ORANGE); c.fill(7, 7, 2, 2, P_YELLOW);
        break;
    case PLANT:
        c.disc(8, 8, 3.5f, P_TAN); c.disc(6, 6, 3, P_SAGE); c.disc(10, 7, 3, P_LGREEN); c.disc(8, 10, 2.5f, P_SAGE);
        c.autoOutline(P_DARK);
        break;
    case EXIT_LADDER:
        c.fill(0, 0, 16, 16, P_DARK);
        c.fill(4, 0, 1, 16, P_BEIGE); c.fill(11, 0, 1, 16, P_BEIGE);
        for (int y = 1; y < 16; y += 3) c.fill(4, y, 8, 1, P_LAVENDER);
        c.outline(0, 0, 16, 16, P_YELLOW);
        break;
    case CIRCLE: c.disc(8, 8, 7.8f, P_WHITE); break;
    case RING: c.ring(8, 8, 6.2f, 7.8f, P_WHITE); break;
    case BULLET: c.fill(5, 7, 6, 2, P_YELLOW); c.fill(10, 7, 2, 2, P_WHITE); break;
    case GRENADE: c.disc(8, 8, 3.2f, P_SAGE); c.fill(8, 4, 3, 1, P_BEIGE); c.set(7, 7, P_LGREEN); c.autoOutline(P_DARK); break;
    case ROCKET: c.fill(3, 7, 9, 2, P_LAVENDER); c.fill(12, 7, 2, 2, P_CORAL); c.fill(2, 6, 2, 4, P_PURPLE); c.set(1, 7, P_ORANGE); c.set(1, 8, P_YELLOW); break;
    case PARTICLE: c.fill(7, 7, 2, 2, P_WHITE); break;
    case BLOOD0:
        c.disc(8, 8, 3.5f, P_CORAL); c.disc(4, 5, 1.5f, P_CORAL); c.disc(12, 11, 1.8f, P_CORAL); c.set(13, 4, P_CORAL); c.set(3, 12, P_CORAL);
        break;
    case BLOOD1:
        c.disc(7, 9, 2.5f, P_CORAL); c.disc(10, 7, 2.5f, P_CORAL); c.set(2, 3, P_CORAL); c.set(14, 13, P_CORAL); c.set(12, 3, P_CORAL);
        break;
    case SCORCH:
        c.disc(8, 8, 6.5f, P_PURPLE); c.disc(8, 8, 4.5f, P_DARK); c.speckle(r, T, 0.12f); c.speckle(r, P_DARK, 0.1f);
        break;
    case CROSSHAIR:
        c.fill(7, 1, 2, 4, P_WHITE); c.fill(7, 11, 2, 4, P_WHITE); c.fill(1, 7, 4, 2, P_WHITE); c.fill(11, 7, 4, 2, P_WHITE);
        c.set(7, 7, P_CORAL); c.set(8, 8, P_CORAL); c.set(7, 8, P_CORAL); c.set(8, 7, P_CORAL);
        c.autoOutline(P_DARK);
        break;
    case ARROW:
        for (int x = 3; x < 13; x++) {
            int h = (13 - x) / 2;
            c.fill(x, 8 - h, 1, 2 * h, P_YELLOW);
        }
        c.autoOutline(P_DARK);
        break;
    case HOME_ICON:
        for (int y = 2; y < 8; y++) c.fill(8 - (y - 1), y, 2 * (y - 1), 1, P_CORAL);
        c.fill(3, 8, 10, 6, P_CREAM);
        c.fill(7, 10, 2, 4, P_TAN);
        c.autoOutline(P_DARK);
        break;
    case SUN: c.disc(8, 8, 4, P_YELLOW); c.set(8, 1, P_YELLOW); c.set(8, 14, P_YELLOW); c.set(1, 8, P_YELLOW); c.set(14, 8, P_YELLOW);
        c.set(3, 3, P_ORANGE); c.set(12, 3, P_ORANGE); c.set(3, 12, P_ORANGE); c.set(12, 12, P_ORANGE); break;
    case MOON: c.disc(8, 8, 5, P_LAVENDER); c.disc(10, 6, 4, T); c.set(5, 9, P_WHITE); break;

    // ---- item icons ----
    case I_SCRAP:
        c.fill(3, 5, 7, 5, P_LAVENDER); c.fill(7, 8, 6, 4, P_BEIGE); c.set(4, 6, P_WHITE); c.fill(9, 3, 2, 4, P_LAVENDER);
        c.edgeShade(P_PURPLE); c.autoOutline(P_DARK);
        break;
    case I_WIRES:
        c.ring(8, 8, 3, 5.5f, P_CORAL); c.ring(8, 8, 1, 2.5f, P_BLUE); c.lineP(12, 10, 14, 14, P_BLUE);
        c.autoOutline(P_DARK);
        break;
    case I_BOLTS:
        c.fill(3, 4, 3, 3, P_BEIGE); c.fill(9, 3, 3, 3, P_LAVENDER); c.fill(6, 9, 3, 3, P_BEIGE); c.fill(11, 10, 2, 5, P_LAVENDER);
        c.autoOutline(P_DARK);
        break;
    case I_TAPE: c.ring(8, 8, 2.3f, 6, P_BEIGE); c.ring(8, 8, 2.3f, 3.2f, P_TAN); c.autoOutline(P_DARK); break;
    case I_BATTERY:
        c.fill(5, 4, 6, 10, P_SAGE); c.fill(5, 4, 6, 3, P_YELLOW); c.fill(7, 2, 2, 2, P_BEIGE); c.set(8, 9, P_DARK); c.set(8, 11, P_DARK);
        c.autoOutline(P_DARK);
        break;
    case I_CIRCUIT:
        c.fill(2, 3, 12, 10, P_SAGE); c.fill(4, 5, 3, 3, P_DARK); c.fill(9, 8, 3, 3, P_DARK);
        c.lineP(7, 6, 12, 6, P_YELLOW); c.lineP(3, 10, 8, 10, P_YELLOW); c.set(12, 4, P_YELLOW);
        c.autoOutline(P_DARK);
        break;
    case I_WATCH:
        c.fill(6, 1, 4, 14, P_TAN); c.disc(8, 8, 4, P_YELLOW); c.disc(8, 8, 2.8f, P_WHITE); c.set(8, 7, P_DARK); c.set(9, 8, P_DARK);
        c.autoOutline(P_DARK);
        break;
    case I_GPU:
        c.fill(1, 4, 14, 8, P_PURPLE); c.disc(5, 8, 2.6f, P_LAVENDER); c.disc(11, 8, 2.6f, P_LAVENDER);
        c.set(5, 8, P_DARK); c.set(11, 8, P_DARK); c.fill(2, 12, 8, 1, P_YELLOW);
        c.autoOutline(P_DARK);
        break;
    case I_JEWELRY: c.ring(8, 9, 2.5f, 4.5f, P_YELLOW); c.disc(8, 4, 2.2f, P_PINK); c.set(7, 3, P_WHITE); c.autoOutline(P_DARK); break;
    case I_FOOD: c.fill(4, 3, 8, 11, P_LAVENDER); c.fill(4, 6, 8, 5, P_CORAL); c.fill(5, 7, 3, 2, P_CREAM); c.fill(4, 3, 8, 1, P_WHITE); c.autoOutline(P_DARK); break;
    case I_MEDSUP: c.fill(3, 5, 10, 8, P_WHITE); c.fill(7, 6, 2, 6, P_CORAL); c.fill(5, 8, 6, 2, P_CORAL); c.fill(3, 5, 10, 1, P_BEIGE); c.autoOutline(P_DARK); break;
    case I_FUEL: c.fill(3, 4, 10, 11, P_CORAL); c.fill(4, 2, 5, 2, P_DARK); c.lineP(4, 5, 12, 13, P_PINK); c.fill(11, 2, 2, 2, P_YELLOW); c.autoOutline(P_DARK); break;
    case I_GUNPARTS:
        c.fill(2, 4, 8, 3, P_PURPLE); c.fill(2, 7, 3, 5, P_PURPLE); c.lineP(9, 10, 14, 10, P_LAVENDER);
        for (int x = 9; x < 15; x += 2) c.set(x, 9, P_LAVENDER);
        c.autoOutline(P_DARK);
        break;
    case I_INTEL: c.fill(3, 4, 10, 8, P_DARK); c.fill(4, 5, 8, 6, P_PURPLE); c.set(11, 10, P_MINT); c.fill(5, 6, 4, 1, P_LAVENDER); c.autoOutline(P_WHITE); break;
    case I_BANDAGE: c.disc(7, 8, 4.5f, P_WHITE); c.ring(7, 8, 1, 2, P_BEIGE); c.fill(9, 11, 5, 3, P_WHITE); c.autoOutline(P_DARK); break;
    case I_MEDKIT: c.fill(2, 4, 12, 10, P_CORAL); c.fill(6, 2, 4, 2, P_PURPLE); c.fill(7, 6, 2, 6, P_WHITE); c.fill(5, 8, 6, 2, P_WHITE); c.autoOutline(P_DARK); break;
    case I_GRENADE: c.disc(8, 9, 4.5f, P_SAGE); c.fill(7, 3, 3, 2, P_BEIGE); c.fill(10, 4, 3, 1, P_LAVENDER); c.ring(12, 6, 1, 2, P_YELLOW); c.set(6, 8, P_LGREEN); c.autoOutline(P_DARK); break;
    case I_AMMO_LIGHT: c.fill(2, 5, 12, 8, P_TAN); c.fill(2, 5, 12, 2, P_YELLOW); for (int x = 4; x < 13; x += 3) c.fill(x, 9, 1, 3, P_ORANGE); c.autoOutline(P_DARK); break;
    case I_AMMO_SHELL: for (int k = 0; k < 3; k++) { c.fill(3 + k * 4, 3, 3, 8, P_CORAL); c.fill(3 + k * 4, 11, 3, 3, P_YELLOW); } c.autoOutline(P_DARK); break;
    case I_AMMO_RIFLE: c.fill(2, 5, 12, 8, P_SAGE); c.fill(2, 5, 12, 2, P_YELLOW); c.fill(5, 9, 6, 1, P_LGREEN); c.autoOutline(P_DARK); break;
    case I_AMMO_SNIPER: for (int k = 0; k < 2; k++) { c.fill(4 + k * 5, 5, 3, 9, P_YELLOW); c.fill(4 + k * 5, 2, 3, 3, P_TAN); c.set(5 + k * 5, 1, P_TAN); } c.autoOutline(P_DARK); break;
    case I_ROCKET: c.fill(2, 7, 10, 3, P_SAGE); c.fill(12, 7, 2, 3, P_CORAL); c.set(14, 8, P_CORAL); c.fill(1, 5, 2, 7, P_PURPLE); c.autoOutline(P_DARK); break;
    case I_PISTOL: case I_REVOLVER: case I_SMG: case I_SHOTGUN: case I_CARBINE: case I_RIFLE: case I_SNIPER: {
        int body = P_PURPLE, x0 = 4, bodyLen = 6, barrel = 2, stock = 0, mag = 0, grip = 5, scope = 0;
        if (id == I_PISTOL) { x0 = 4; bodyLen = 6; barrel = 1; grip = 5; }
        if (id == I_REVOLVER) { x0 = 3; bodyLen = 7; barrel = 3; grip = 6; body = P_CORAL; }
        if (id == I_SMG) { x0 = 3; bodyLen = 7; barrel = 2; stock = 2; mag = 4; grip = 5; }
        if (id == I_SHOTGUN) { x0 = 5; bodyLen = 4; barrel = 6; stock = 4; grip = 6; body = P_PURPLE; }
        if (id == I_RIFLE) { x0 = 4; bodyLen = 6; barrel = 4; stock = 3; mag = 3; grip = 6; }
        if (id == I_CARBINE) { x0 = 4; bodyLen = 6; barrel = 3; stock = 2; mag = 2; grip = 6; body = P_SAGE; }
        if (id == I_SNIPER) { x0 = 4; bodyLen = 5; barrel = 6; stock = 3; mag = 2; grip = 6; scope = 1; }
        c.fill(x0, 6, bodyLen, 3, body);
        c.fill(x0, 6, bodyLen, 1, P_LAVENDER);
        c.fill(x0 + bodyLen, 7, barrel, 1, P_LAVENDER);
        if (stock) c.fill(x0 - stock, 7, stock, 3, P_TAN);
        c.fill(grip, 9, 2, 4, (id == I_PISTOL || id == I_REVOLVER) ? P_PURPLE : P_TAN);
        if (mag) c.fill(grip + 3, 9, 2, mag, P_DARK);
        if (id == I_SHOTGUN) c.fill(x0 + bodyLen, 8, 3, 1, P_TAN);
        if (scope) { c.fill(x0 + 1, 4, 4, 2, P_DARK); c.set(x0 + 4, 4, P_BLUE); }
        c.autoOutline(P_DARK);
        break;
    }
    case I_LAUNCHER:
        c.fill(1, 6, 13, 3, P_SAGE); c.fill(1, 6, 13, 1, P_LGREEN); c.fill(14, 5, 1, 5, P_PURPLE);
        c.fill(6, 9, 2, 3, P_TAN); c.fill(3, 4, 3, 2, P_DARK); c.fill(0, 6, 1, 3, P_CORAL);
        c.autoOutline(P_DARK);
        break;
    case I_VEST_LIGHT: case I_VEST_HEAVY: {
        int col = id == I_VEST_LIGHT ? P_SAGE : P_PURPLE;
        c.fill(3, 2, 10, 12, col);
        c.fill(6, 2, 4, 3, T);
        c.fill(3, 2, 1, 5, T); c.fill(12, 2, 1, 5, T);
        if (id == I_VEST_HEAVY) { c.fill(4, 7, 3, 5, P_LAVENDER); c.fill(9, 7, 3, 5, P_LAVENDER); }
        else { c.fill(4, 9, 3, 2, P_LGREEN); c.fill(9, 9, 3, 2, P_LGREEN); }
        c.fill(8, 5, 1, 9, P_DARK);
        c.autoOutline(P_DARK);
        break;
    }
    case I_PACK_SMALL: case I_PACK_LARGE: {
        int col = id == I_PACK_SMALL ? P_TAN : P_SAGE;
        int w = id == I_PACK_SMALL ? 8 : 11, x = 8 - w / 2;
        c.fill(x, 3, w, 11, col);
        c.fill(x, 3, w, 4, id == I_PACK_SMALL ? P_ORANGE : P_LGREEN);
        c.fill(x + 2, 9, w - 4, 3, P_PURPLE);
        c.fill(7, 1, 2, 2, P_PURPLE);
        c.autoOutline(P_DARK);
        break;
    }
    case I_COIN: c.disc(8, 8, 5.5f, P_ORANGE); c.disc(8, 8, 4.2f, P_YELLOW); c.fill(7, 5, 2, 6, P_ORANGE); c.autoOutline(P_DARK); break;
    default:
        c.fill(0, 0, 16, 16, P_PINK);
        c.outline(0, 0, 16, 16, P_DARK);
        break;
    }
}

}  // namespace

static const Assets::Sprite* g_fallback[COUNT];

std::string fallbackKey(int id) {
    std::string n = name(id);
    for (char& ch : n) ch = (char)std::tolower((unsigned char)ch);
    return "gen/" + n;
}

void registerFallbacks() {
    for (int id = 0; id < COUNT; id++) {
        Canvas c;
        paintSprite(id, c);
        uint8_t px[16][16][4];
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++) {
                int p = c.px[y][x];
                if (p == T) { std::memset(px[y][x], 0, 4); continue; }
                uint32_t hex = PALETTE_HEX[p];
                px[y][x][0] = (hex >> 16) & 255;
                px[y][x][1] = (hex >> 8) & 255;
                px[y][x][2] = hex & 255;
                px[y][x][3] = 255;
            }
        Assets::addGenerated(fallbackKey(id), &px[0][0][0]);
    }
}

void resolve() {
    for (int id = 0; id < COUNT; id++) g_fallback[id] = Assets::find(fallbackKey(id));
}

const Assets::Sprite* fallback(int id) {
    return (id >= 0 && id < COUNT) ? g_fallback[id] : nullptr;
}

void uv(int id, float& u0, float& v0, float& u1, float& v1) {
    const Assets::Sprite* s = fallback(id);
    if (!s || !s->valid()) { u0 = v0 = u1 = v1 = 0; return; }
    const Assets::Frame& f = s->frame(0);
    u0 = f.u0; v0 = f.v0; u1 = f.u1; v1 = f.v1;
}

}  // namespace Sprites
