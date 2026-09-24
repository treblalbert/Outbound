// Math, random numbers and the Vanilla Milkshake palette.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

constexpr float PI = 3.14159265358979f;

struct Vec2 {
    float x = 0, y = 0;
    Vec2() {}
    Vec2(float x_, float y_) : x(x_), y(y_) {}
    Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
    Vec2 operator/(float s) const { return {x / s, y / s}; }
    Vec2& operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(Vec2 o) { x -= o.x; y -= o.y; return *this; }
    Vec2& operator*=(float s) { x *= s; y *= s; return *this; }
};

inline float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline float length(Vec2 v) { return std::sqrt(v.x * v.x + v.y * v.y); }
inline float lengthSq(Vec2 v) { return v.x * v.x + v.y * v.y; }
inline float dist(Vec2 a, Vec2 b) { return length(a - b); }
inline Vec2 normalize(Vec2 v) { float l = length(v); return l > 1e-5f ? v / l : Vec2(0, 0); }
inline Vec2 fromAngle(float a) { return {std::cos(a), std::sin(a)}; }
inline float angleOf(Vec2 v) { return std::atan2(v.y, v.x); }
inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float angleDiff(float a, float b) {
    float d = std::fmod(b - a + PI, 2 * PI);
    if (d < 0) d += 2 * PI;
    return d - PI;
}

struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed = 0x9E3779B97F4A7C15ull) : s(seed ? seed : 1) {}
    uint32_t next() {
        s ^= s << 13; s ^= s >> 7; s ^= s << 17;
        return (uint32_t)(s >> 11);
    }
    float f() { return (next() & 0xFFFFFF) / float(0x1000000); }
    float range(float a, float b) { return a + (b - a) * f(); }
    int irange(int a, int b) { return a + (int)(next() % (uint32_t)(b - a + 1)); }
    bool chance(float p) { return f() < p; }
};

// SplitMix64 finalizer - turns a counter-ish value into a well spread seed.
inline uint64_t mix64(uint64_t z) {
    z += 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

inline uint32_t hash2(int x, int y, uint32_t seed) {
    uint32_t h = seed ^ ((uint32_t)x * 374761393u) ^ ((uint32_t)y * 668265263u);
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

struct Color {
    float r = 1, g = 1, b = 1, a = 1;
    Color() {}
    Color(float r_, float g_, float b_, float a_ = 1) : r(r_), g(g_), b(b_), a(a_) {}
    Color withA(float na) const { return {r, g, b, na}; }
};

// Vanilla Milkshake by Space Sandwich — https://lospec.com/palette-list/vanilla-milkshake
enum Pal : int {
    P_DARK, P_PURPLE, P_BEIGE, P_CORAL, P_LAVENDER, P_BLUE, P_MINT, P_PINK,
    P_SAGE, P_LGREEN, P_YGREEN, P_CREAM, P_TAN, P_ORANGE, P_YELLOW, P_WHITE, P_COUNT
};

constexpr uint32_t PALETTE_HEX[P_COUNT] = {
    0x28282e, 0x6c5671, 0xd9c8bf, 0xf98284, 0xb0a9e4, 0xaccce4, 0xb3e3da, 0xfeaae4,
    0x87a889, 0xb0eb93, 0xe9f59d, 0xffe6c6, 0xdea38b, 0xffc384, 0xfff7a0, 0xfff7e4,
};

inline Color pal(int p, float a = 1) {
    uint32_t h = PALETTE_HEX[p];
    return {((h >> 16) & 255) / 255.0f, ((h >> 8) & 255) / 255.0f, (h & 255) / 255.0f, a};
}

// Makes a directory if it is not already there. Windows and POSIX (which is what
// Emscripten's virtual filesystem looks like) spell this differently.
void ensureDir(const std::string& path);

// Flushes anything just written under saves/ to durable storage. A no-op on the
// desktop, where the write already went to disk; on the web it pushes the virtual
// filesystem into IndexedDB so progress survives a reload.
void persistSaves();

// Folder containing assets/ and credits.txt, and the one holding saves/ (resolved at
// startup; normally the same folder, the one the game was shipped in).
extern std::string g_dataDir;
extern std::string g_saveDir;
inline std::string dataPath(const std::string& rel) {
    return (rel.compare(0, 5, "saves") == 0 ? g_saveDir : g_dataDir) + rel;
}

inline std::string fmtTime(float minutes) {
    int m = (int)minutes;
    char buf[16];
    std::snprintf(buf, sizeof buf, "%02d:%02d", (m / 60) % 24, m % 60);
    return buf;
}
