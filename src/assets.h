// Loads every PNG under assets/sprites into a single texture atlas.
//
//  * "<name>-Sheet6.png"     -> one sprite with 6 animation frames (split horizontally)
//  * "<name>_TileSet.png"    -> one sprite split into a grid of 16x16 tiles
//  * anything else           -> a single frame sprite
//
// Sprites are looked up by their path relative to assets/sprites, lowercased, with
// '/' separators, without the extension and without the "-SheetN" suffix. e.g.
//   assets/sprites/Objects/Nature/Green/Tree_5_Big_Green.png
//     -> "objects/nature/green/tree_5_big_green"
#pragma once
#include "gl.h"
#include <cstdint>
#include <string>
#include <vector>

namespace Assets {

struct Frame {
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    int w = 0, h = 0;
    // The pixels (px, py, pw, ph) of this frame, as a frame of their own.
    Frame sub(float px, float py, float pw, float ph) const {
        Frame o;
        float du = (u1 - u0) / w, dv = (v1 - v0) / h;
        o.u0 = u0 + du * px; o.u1 = u0 + du * (px + pw);
        o.v0 = v0 + dv * py; o.v1 = v0 + dv * (py + ph);
        o.w = (int)pw; o.h = (int)ph;
        return o;
    }
};

struct Sprite {
    std::string key;
    std::vector<Frame> frames;
    int w = 0, h = 0;            // frame size
    int cols = 1, rows = 1;      // tile grid (tilesets only)
    std::vector<uint8_t> alpha;   // source alpha, retained for pixel collision
    bool valid() const { return !frames.empty(); }
    int frameCount() const { return (int)frames.size(); }
    const Frame& frame(int i) const {
        static const Frame none;
        if (frames.empty()) return none;
        int n = (int)frames.size();
        return frames[((i % n) + n) % n];
    }
    const Frame& tile(int col, int row) const { return frame(row * cols + col); }
    bool opaqueAt(int frameIndex, int x, int y, uint8_t threshold = 32) const {
        if (x < 0 || y < 0 || x >= w || y >= h || alpha.empty()) return false;
        int n = std::max(1, (int)frames.size()); frameIndex = ((frameIndex % n) + n) % n;
        int col = frameIndex % std::max(1, cols), row = frameIndex / std::max(1, cols);
        int sw = w * std::max(1, cols);
        int sx = col * w + x, sy = row * h + y;
        size_t i = (size_t)sy * sw + sx;
        return i < alpha.size() && alpha[i] >= threshold;
    }
};

// One 16x16 seamless tile taken from a tileset, usable as a ground fill.
struct TileRef {
    const Sprite* sprite = nullptr;
    int frame = 0;
    float variance = 0;
    bool valid() const { return sprite != nullptr; }
};

enum class Fill { Grass, Asphalt, Soil, Stone, Interior, Brick, Garbage, Roof, COUNT };

bool load();                     // scans the folder and builds the atlas
GLuint atlasTexture();
int atlasSize();
int spriteCount();

// Registers a procedurally generated 16x16 sprite (fallback art) before load().
void addGenerated(const std::string& key, const uint8_t* rgba16x16);

const Sprite* find(const std::string& key);           // nullptr when missing
const Sprite& get(const std::string& key);            // empty sprite when missing
// First sprite whose key contains all the given substrings (cheap fuzzy lookup).
const Sprite* search(const std::vector<std::string>& parts);
// Every sprite whose key starts with `prefix`, in key order.
std::vector<const Sprite*> keysWithPrefix(const std::string& prefix);

const std::vector<TileRef>& fills(Fill f);            // sorted, flattest first

// ---- shirts: the character's chosen colour (0.10v). The pack's shirt is one colour,
// so each choice is a copy of the player art with that colour swapped, loaded as
// "character/main_c<n>/..." (0 is the pack's own green, under "character/main/").
// Red is left out: that is what the raiders wear.
constexpr int SHIRT_COUNT = 10;
struct ShirtDef { const char* name; uint8_t r, g, b; int uiPal; };
const ShirtDef& shirt(int i);

}  // namespace Assets
