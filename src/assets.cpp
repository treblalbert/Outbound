#include "assets.h"
#include "core.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_THREAD_LOCALS
#include "../external/stb_image.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

namespace Assets {

namespace {

struct Raw {
    std::string key;
    int w = 0, h = 0;
    int frameCount = 1;
    bool tileset = false;
    std::vector<uint8_t> px;
};

std::vector<Raw> g_raw;
std::map<std::string, Sprite> g_sprites;
std::vector<TileRef> g_fills[(int)Fill::COUNT];
GLuint g_texture = 0;
int g_atlasSize = 0;

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// "Character_down_idle-Sheet6" -> key without suffix, frames = 6
void parseSheet(std::string& name, int& frames) {
    std::string low = lower(name);
    size_t p = low.rfind("sheet");
    if (p == std::string::npos) return;
    size_t digits = p + 5;
    if (digits >= low.size() || !std::isdigit((unsigned char)low[digits])) return;
    int n = std::atoi(low.c_str() + digits);
    for (size_t i = digits; i < low.size(); i++)
        if (!std::isdigit((unsigned char)low[i])) return;  // trailing junk: not a sheet
    if (n < 2 || n > 64) return;
    size_t cut = p;
    while (cut > 0 && (name[cut - 1] == '-' || name[cut - 1] == '_' || name[cut - 1] == ' ')) cut--;
    name = name.substr(0, cut);
    frames = n;
}

}  // namespace

const ShirtDef& shirt(int i) {
    static const ShirtDef SHIRTS[SHIRT_COUNT] = {
        {"Green", 67, 115, 91, P_YGREEN},   {"Blue", 58, 86, 150, P_BLUE},     {"Yellow", 184, 156, 60, P_YELLOW},
        {"Orange", 184, 102, 50, P_ORANGE}, {"Purple", 108, 70, 140, P_LAVENDER}, {"Pink", 178, 92, 134, P_PINK},
        {"Teal", 46, 124, 130, P_MINT},     {"White", 206, 202, 196, P_WHITE},  {"Black", 42, 40, 50, P_PURPLE},
        {"Brown", 112, 78, 54, P_TAN},
    };
    return SHIRTS[i >= 0 && i < SHIRT_COUNT ? i : 0];
}

namespace {

void addRaw(const std::string& relPath, const std::vector<uint8_t>& fileBytes) {
    int w, h, comp;
    unsigned char* img = stbi_load_from_memory(fileBytes.data(), (int)fileBytes.size(), &w, &h, &comp, 4);
    if (!img) return;
    Raw r;
    r.w = w;
    r.h = h;
    r.px.assign(img, img + (size_t)w * h * 4);
    stbi_image_free(img);

    std::string key = lower(relPath);
    for (char& c : key) if (c == '\\') c = '/';
    if (key.size() > 4 && key.compare(key.size() - 4, 4, ".png") == 0) key.resize(key.size() - 4);
    int frames = 1;
    parseSheet(key, frames);
    r.tileset = key.find("tileset") != std::string::npos;
    if (r.tileset) frames = 1;
    r.frameCount = frames;
    r.key = key;
    // Raiders wear the player's art; give them a copy with the green shirt dyed red,
    // under "character/main_enemy/...", so they read as enemies at a glance.
    if (key.compare(0, 15, "character/main/") == 0) {
        Raw e = r;
        e.key = "character/main_enemy/" + key.substr(15);
        for (size_t i = 0; i + 3 < e.px.size(); i += 4) {
            int cr = e.px[i], cg = e.px[i + 1], cb = e.px[i + 2];
            if (e.px[i + 3] == 0 || !(cg > cr + 15 && cg > cb + 5)) continue;
            e.px[i] = (uint8_t)std::min(255, cg * 135 / 100 + 10);
            e.px[i + 1] = (uint8_t)(cr * 55 / 100);
            e.px[i + 2] = (uint8_t)(cb * 60 / 100);
        }
        g_raw.push_back(std::move(e));
        // And one copy per shirt colour the player can pick.
        const ShirtDef& base = shirt(0);
        for (int s = 1; s < SHIRT_COUNT; s++) {
            const ShirtDef& sd = shirt(s);
            Raw c = r;
            c.key = "character/main_c" + std::to_string(s) + "/" + key.substr(15);
            for (size_t i = 0; i + 3 < c.px.size(); i += 4) {
                if (c.px[i + 3] == 0 || c.px[i] != base.r || c.px[i + 1] != base.g || c.px[i + 2] != base.b) continue;
                c.px[i] = sd.r;
                c.px[i + 1] = sd.g;
                c.px[i + 2] = sd.b;
            }
            g_raw.push_back(std::move(c));
        }
    }
    g_raw.push_back(std::move(r));
}

void scanDir(const std::string& base, const std::string& rel) {
#ifndef _WIN32
    // POSIX / Emscripten MEMFS.
    DIR* d = opendir((base + rel).c_str());
    if (!d) return;
    while (struct dirent* e = readdir(d)) {
        std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        std::string full = base + rel + name;
        struct stat st;
        if (stat(full.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            if (lower(name) == "gif") continue;   // animated previews, not frames
            scanDir(base, rel + name + "/");
            continue;
        }
        std::string low = lower(name);
        if (low.size() < 4 || low.compare(low.size() - 4, 4, ".png") != 0) continue;
        std::ifstream f(full, std::ios::binary);
        if (!f) continue;
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (!bytes.empty()) addRaw(rel + name, bytes);
    }
    closedir(d);
#else
    std::string pattern = base + rel + "*";
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::string name = fd.cFileName;
        if (name == "." || name == "..") continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (lower(name) == "gif") continue;  // animated previews, not frames
            scanDir(base, rel + name + "\\");
            continue;
        }
        std::string low = lower(name);
        if (low.size() < 4 || low.compare(low.size() - 4, 4, ".png") != 0) continue;
        std::ifstream f(base + rel + name, std::ios::binary);
        if (!f) continue;
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (!bytes.empty()) addRaw(rel + name, bytes);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#endif
}


// ---- sprites cut out of packed sheets ------------------------------------------
// Some packs ship as one big sheet rather than one file per sprite. These tables name
// the pieces the game uses. Furniture is drawn at twice the world's pixel size, so it
// is halved on the way in (keeping crisp edges: a 2x2 block is opaque when most of it
// is, with the average colour of its opaque pixels).
struct SliceDef { const char* key; int x, y, w, h; };

// Kenney's Input Prompts (pixel, 16x16 tiles on a 17-pixel grid). Wide keys span two.
#define IN1(k, c, r) {k, (c) * 17, (r) * 17, 16, 16}
#define IN2(k, c, r) {k, (c) * 17, (r) * 17, 33, 16}
const SliceDef INPUT_SLICES[] = {
    IN1("kb_esc", 17, 0), IN1("kb_f1", 18, 0), IN1("kb_f2", 19, 0), IN1("kb_f3", 20, 0), IN1("kb_f4", 21, 0),
    IN1("kb_f5", 22, 0), IN1("kb_f6", 23, 0), IN1("kb_f7", 24, 0), IN1("kb_f8", 25, 0), IN1("kb_f9", 26, 0),
    IN1("kb_f10", 27, 0), IN1("kb_f11", 28, 0), IN1("kb_f12", 29, 0), IN1("kb_1", 17, 1), IN1("kb_2", 18, 1),
    IN1("kb_3", 19, 1), IN1("kb_4", 20, 1), IN1("kb_5", 21, 1), IN1("kb_6", 22, 1), IN1("kb_7", 23, 1),
    IN1("kb_8", 24, 1), IN1("kb_9", 25, 1), IN1("kb_0", 26, 1), IN1("kb_q", 17, 2), IN1("kb_w", 18, 2),
    IN1("kb_e", 19, 2), IN1("kb_r", 20, 2), IN1("kb_t", 21, 2), IN1("kb_y", 22, 2), IN1("kb_u", 23, 2),
    IN1("kb_i", 24, 2), IN1("kb_o", 25, 2), IN1("kb_p", 26, 2), IN1("kb_a", 18, 3), IN1("kb_s", 19, 3),
    IN1("kb_d", 20, 3), IN1("kb_f", 21, 3), IN1("kb_g", 22, 3), IN1("kb_h", 23, 3), IN1("kb_j", 24, 3),
    IN1("kb_k", 25, 3), IN1("kb_l", 26, 3), IN1("kb_z", 19, 4), IN1("kb_x", 20, 4), IN1("kb_c", 21, 4),
    IN1("kb_v", 22, 4), IN1("kb_b", 23, 4), IN1("kb_n", 24, 4), IN1("kb_m", 25, 4), IN1("kb_space", 17, 4),
    IN1("kb_up", 30, 4), IN1("kb_right", 31, 4), IN1("kb_down", 32, 4), IN1("kb_left", 33, 4), IN2("kb_alt", 17, 5),
    IN2("kb_tab", 19, 5), IN2("kb_delete", 21, 5), IN2("kb_end", 23, 5), IN2("kb_ctrl", 17, 6), IN2("kb_caps", 19, 6),
    IN2("kb_home", 21, 6), IN2("kb_shift", 17, 7), IN2("kb_insert", 19, 7), IN2("kb_enter", 32, 3),
    IN1("mouse", 8, 2), IN1("mouse_l", 9, 2), IN1("mouse_r", 10, 2), IN1("mouse_wheel", 13, 2),
    IN1("pad_a", 4, 0), IN1("pad_b", 5, 0), IN1("pad_x", 6, 0), IN1("pad_y", 7, 0),
    IN1("pad_dpad", 0, 1), IN1("pad_dpad_ud", 6, 1), IN1("pad_dpad_lr", 5, 1),
    IN1("pad_ls", 15, 6), IN1("pad_ls_click", 16, 6), IN1("pad_rs", 15, 8), IN1("pad_rs_click", 16, 8),
    IN1("pad_lt", 7, 16), IN1("pad_rt", 8, 16), IN1("pad_lb", 9, 16), IN1("pad_rb", 10, 16),
    IN1("pad_view", 4, 18), IN1("pad_menu", 5, 18),
    // PlayStation (0.12v): shoulders, and Options / Share (Create). The face buttons are
    // put together below: the sheet draws each symbol in two halves on two tiles.
    IN1("ps_l1", 19, 18), IN1("ps_r1", 20, 18), IN1("ps_l2", 17, 18), IN1("ps_r2", 18, 18),
    IN1("ps_options", 20, 22), IN1("ps_share", 19, 22),
};
#undef IN1
#undef IN2

// BitGlow's living room & kitchen interiors.
const SliceDef FURN_DOORS[] = {
    {"door_brown", 113, 16, 30, 48}, {"door_brown_open", 161, 16, 30, 51},
    {"door_dark", 113, 96, 30, 48}, {"door_dark_open", 161, 96, 30, 51},
    {"door_white", 113, 176, 30, 48}, {"door_white_open", 161, 176, 30, 51},
    // Empty frames: a doorway standing open, the room behind showing through.
    {"door_brown_hole", 65, 16, 30, 48}, {"door_dark_hole", 65, 96, 30, 48},
    // A straight flight seen from above (0.11v): with its runner, and plain.
    {"stairs_runner", 272, 16, 31, 67}, {"stairs_plain", 304, 16, 31, 67},
};
const SliceDef FURN_DECOR[] = {
    {"floorlamp_a", 16, 16, 16, 46}, {"floorlamp_b", 48, 15, 16, 47}, {"floorlamp_c", 80, 16, 16, 46},
    {"tablelamp", 120, 22, 16, 25}, {"painting_sunset", 112, 64, 32, 16}, {"painting_hills", 112, 96, 32, 16},
    {"mirror", 160, 64, 16, 46}, {"plant_red", 16, 81, 15, 30}, {"plant_basket", 48, 80, 16, 31},
    {"plant_grey", 80, 80, 16, 31}, {"clock", 48, 124, 16, 16},
};
const SliceDef FURN_LIVING[] = {
    {"sofa_beige", 16, 32, 48, 32}, {"sofa_grey", 16, 96, 48, 32}, {"armchair_grey", 240, 96, 32, 32},
    {"table_long", 16, 160, 48, 32}, {"table_round", 224, 208, 32, 32}, {"rug_red", 16, 336, 64, 32},
    {"rug_red_b", 144, 336, 64, 32}, {"rug_round", 416, 320, 48, 32}, {"tv", 358, 342, 36, 20},
};
const SliceDef FURN_CABINETS[] = {
    {"bookshelf", 16, 80, 48, 48}, {"bookshelf_b", 16, 16, 48, 48}, {"cabinet", 208, 19, 48, 45},
    {"shelf_narrow", 464, 30, 16, 34},
};
const SliceDef FURN_KITCHEN[] = {
    {"fridge", 356, 24, 24, 56}, {"stove", 192, 80, 32, 42}, {"drawers", 144, 80, 32, 42},
};

bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return !out.empty();
}

void addSlices(const std::string& fileRel, const std::string& prefix, const SliceDef* defs, int n, bool halve) {
    std::vector<uint8_t> bytes;
    if (!readFile(dataPath(fileRel), bytes)) { std::fprintf(stderr, "[assets] missing %s\n", fileRel.c_str()); return; }
    int w, h, comp;
    unsigned char* img = stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &comp, 4);
    if (!img) return;
    for (int i = 0; i < n; i++) {
        const SliceDef& d = defs[i];
        if (d.x < 0 || d.y < 0 || d.x + d.w > w || d.y + d.h > h) continue;
        Raw r;
        r.key = prefix + d.key;
        if (!halve) {
            r.w = d.w; r.h = d.h;
            r.px.resize((size_t)r.w * r.h * 4);
            for (int y = 0; y < r.h; y++)
                std::memcpy(&r.px[(size_t)y * r.w * 4], img + ((size_t)(d.y + y) * w + d.x) * 4, (size_t)r.w * 4);
        } else {
            r.w = (d.w + 1) / 2; r.h = (d.h + 1) / 2;
            r.px.assign((size_t)r.w * r.h * 4, 0);
            for (int y = 0; y < r.h; y++)
                for (int x = 0; x < r.w; x++) {
                    int sum[3] = {0, 0, 0}, opaque = 0;
                    for (int oy = 0; oy < 2; oy++)
                        for (int ox = 0; ox < 2; ox++) {
                            int sx = d.x + x * 2 + ox, sy = d.y + y * 2 + oy;
                            if (sx >= d.x + d.w || sy >= d.y + d.h) continue;
                            const unsigned char* p = img + ((size_t)sy * w + sx) * 4;
                            if (p[3] < 128) continue;
                            opaque++;
                            for (int c = 0; c < 3; c++) sum[c] += p[c];
                        }
                    if (opaque < 2) continue;
                    uint8_t* o = &r.px[((size_t)y * r.w + x) * 4];
                    for (int c = 0; c < 3; c++) o[c] = (uint8_t)(sum[c] / opaque);
                    o[3] = 255;
                }
        }
        g_raw.push_back(std::move(r));
    }
    stbi_image_free(img);
}

// The PlayStation face buttons: the sheet keeps the button with the left half of its
// symbol on one tile and the right half on the next, to be laid over each other.
void addPlayStationFaces() {
    std::vector<uint8_t> bytes;
    if (!readFile(dataPath("assets/Input tilemap.png"), bytes)) return;
    int w, h, comp;
    unsigned char* img = stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &comp, 4);
    if (!img) return;
    struct Face { const char* key; int col; };
    const Face faces[] = {{"input/ps_triangle", 17}, {"input/ps_circle", 19}, {"input/ps_square", 21}, {"input/ps_cross", 23}};
    for (const Face& f : faces) {
        int x0 = f.col * 17, y0 = 16 * 17, x1 = (f.col + 1) * 17;
        if (x1 + 16 > w || y0 + 16 > h) continue;
        Raw r;
        r.key = f.key;
        r.w = r.h = 16;
        r.px.resize(16 * 16 * 4);
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++) {
                const unsigned char* a = img + ((size_t)(y0 + y) * w + x0 + x) * 4;
                const unsigned char* b = img + ((size_t)(y0 + y) * w + x1 + x) * 4;
                uint8_t* o = &r.px[((size_t)y * 16 + x) * 4];
                const unsigned char* src = b[3] > 127 ? b : a;   // the second half goes on top
                for (int c = 0; c < 4; c++) o[c] = src[c];
            }
        g_raw.push_back(std::move(r));
    }
    stbi_image_free(img);
}

void addPackSheets() {
    addSlices("assets/Input tilemap.png", "input/", INPUT_SLICES, sizeof(INPUT_SLICES) / sizeof(INPUT_SLICES[0]), false);
    addPlayStationFaces();
    addSlices("assets/Furniture/doorswindowsstairs_LRK.png", "furniture/", FURN_DOORS, sizeof(FURN_DOORS) / sizeof(FURN_DOORS[0]), true);
    addSlices("assets/Furniture/decorations_LRK.png", "furniture/", FURN_DECOR, sizeof(FURN_DECOR) / sizeof(FURN_DECOR[0]), true);
    addSlices("assets/Furniture/livingroom_LRK.png", "furniture/", FURN_LIVING, sizeof(FURN_LIVING) / sizeof(FURN_LIVING[0]), true);
    addSlices("assets/Furniture/cabinets_LRK.png", "furniture/", FURN_CABINETS, sizeof(FURN_CABINETS) / sizeof(FURN_CABINETS[0]), true);
    addSlices("assets/Furniture/kitchen_LRK.png", "furniture/", FURN_KITCHEN, sizeof(FURN_KITCHEN) / sizeof(FURN_KITCHEN[0]), true);
}

// ---- tileset classification -------------------------------------------------
struct TileStats {
    bool opaque = false;
    bool seamless = false;
    float variance = 0;
    float r = 0, g = 0, b = 0;
};

TileStats analyzeTile(const Raw& raw, int tx, int ty, int size) {
    TileStats s;
    auto at = [&](int x, int y, int c) { return raw.px[(((size_t)(ty * size + y) * raw.w) + (tx * size + x)) * 4 + c]; };
    double sr = 0, sg = 0, sb = 0;
    bool opaque = true;
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            if (at(x, y, 3) < 250) opaque = false;
            sr += at(x, y, 0);
            sg += at(x, y, 1);
            sb += at(x, y, 2);
        }
    int n = size * size;
    s.opaque = opaque;
    s.r = (float)(sr / n);
    s.g = (float)(sg / n);
    s.b = (float)(sb / n);
    double dev = 0;
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++)
            dev += std::fabs(at(x, y, 0) - s.r) + std::fabs(at(x, y, 1) - s.g) + std::fabs(at(x, y, 2) - s.b);
    s.variance = (float)(dev / (n * 3));
    // Does it tile against itself?
    double edge = 0;
    for (int i = 0; i < size; i++)
        for (int c = 0; c < 3; c++) {
            edge += std::fabs((double)at(0, i, c) - at(size - 1, i, c));
            edge += std::fabs((double)at(i, 0, c) - at(i, size - 1, c));
        }
    s.seamless = edge / (size * 6) < 42.0;
    return s;
}

Fill classifyFill(const std::string& key, const TileStats& t) {
    if (key.find("buildings_") != std::string::npos) return Fill::Interior;
    if (key.find("brick-wall") != std::string::npos) return Fill::Brick;
    if (key.find("garbage") != std::string::npos) return Fill::Garbage;
    if (key.find("roof") != std::string::npos) return Fill::Roof;
    float mx = std::max(t.r, std::max(t.g, t.b)), mn = std::min(t.r, std::min(t.g, t.b));
    float sat = mx - mn;
    if (t.g > t.r + 6 && t.g > t.b + 6) return Fill::Grass;
    if (sat <= 16) return Fill::Asphalt;
    if (t.r >= t.g && t.g >= t.b) return Fill::Soil;
    return Fill::Stone;
}


}  // namespace

void addGenerated(const std::string& key, const uint8_t* rgba) {
    Raw r;
    r.key = key;
    r.w = r.h = 16;
    r.px.assign(rgba, rgba + 16 * 16 * 4);
    g_raw.push_back(std::move(r));
}

bool load() {
    std::fprintf(stderr, "[assets] scanning %s\n", dataPath("assets/sprites/").c_str());
    std::fflush(stderr);
    scanDir(dataPath("assets/sprites/"), "");
    // The elite guns' art lives in its own pack folder: keys "moreweapons/<name>".
#ifdef _WIN32
    scanDir(dataPath("assets/"), "MoreWeapons\\");
#else
    scanDir(dataPath("assets/"), "MoreWeapons/");
#endif
    addPackSheets();
    std::fprintf(stderr, "[assets] %zu images\n", g_raw.size());

    std::fflush(stderr);
    // Shelf-pack the images, tallest first.
    std::vector<int> order(g_raw.size());
    for (size_t i = 0; i < order.size(); i++) order[i] = (int)i;
    std::sort(order.begin(), order.end(), [](int a, int b) { return g_raw[a].h > g_raw[b].h; });

    const int pad = 1;
    int size = 1024;
    std::vector<int> px_, py_;
    for (;; size *= 2) {
        px_.assign(g_raw.size(), 0);
        py_.assign(g_raw.size(), 0);
        int x = pad, y = pad, shelf = 0;
        bool ok = true;
        for (int idx : order) {
            const Raw& r = g_raw[idx];
            if (r.w + 2 * pad > size) { ok = false; break; }
            if (x + r.w + pad > size) {
                x = pad;
                y += shelf + pad;
                shelf = 0;
            }
            if (y + r.h + pad > size) { ok = false; break; }
            px_[idx] = x;
            py_[idx] = y;
            x += r.w + pad;
            shelf = std::max(shelf, r.h);
        }
        if (ok) break;
        if (size >= 8192) return false;
    }
    g_atlasSize = size;
    std::fprintf(stderr, "[assets] packed into %d\n", size);
    std::fflush(stderr);

    std::vector<uint8_t> atlas((size_t)size * size * 4, 0);
    for (size_t i = 0; i < g_raw.size(); i++) {
        const Raw& r = g_raw[i];
        for (int y = 0; y < r.h; y++)
            std::memcpy(&atlas[(((size_t)(py_[i] + y) * size) + px_[i]) * 4], &r.px[(size_t)y * r.w * 4], (size_t)r.w * 4);
    }

    std::fprintf(stderr, "[assets] blitted\n");
    std::fflush(stderr);
    // Build sprite records.
    for (size_t i = 0; i < g_raw.size(); i++) {
        Raw& r = g_raw[i];
        Sprite s;
        s.key = r.key;
        s.alpha.resize((size_t)r.w * r.h);
        for (size_t a = 0; a < s.alpha.size(); ++a) s.alpha[a] = r.px[a * 4 + 3];
        float inv = 1.0f / size;
        auto pushFrame = [&](int ox, int oy, int w, int h) {
            Frame f;
            f.u0 = (px_[i] + ox) * inv;
            f.v0 = (py_[i] + oy) * inv;
            f.u1 = (px_[i] + ox + w) * inv;
            f.v1 = (py_[i] + oy + h) * inv;
            f.w = w;
            f.h = h;
            s.frames.push_back(f);
        };
        if (r.tileset) {
            const int T = 16;
            s.cols = std::max(1, r.w / T);
            s.rows = std::max(1, r.h / T);
            s.w = s.h = T;
            for (int ty = 0; ty < s.rows; ty++)
                for (int tx = 0; tx < s.cols; tx++) pushFrame(tx * T, ty * T, T, T);
            // std::map nodes are stable, so the sprite can be referenced once inserted.
            std::string key = s.key;
            Sprite* stored = &(g_sprites[key] = std::move(s));
            for (int ty = 0; ty < stored->rows; ty++)
                for (int tx = 0; tx < stored->cols; tx++) {
                    TileStats st = analyzeTile(r, tx, ty, T);
                    if (!st.opaque || !st.seamless || st.variance > 70) continue;
                    if (st.r + st.g + st.b < 60) continue;  // near-black
                    TileRef ref;
                    ref.sprite = stored;
                    ref.frame = ty * stored->cols + tx;
                    ref.variance = st.variance;
                    g_fills[(int)classifyFill(r.key, st)].push_back(ref);
                }
            r.px.clear();
            r.px.shrink_to_fit();
            continue;
        } else {
            int n = std::max(1, r.frameCount);
            int fw = r.w / n;
            s.w = fw;
            s.h = r.h;
            s.cols = n;
            for (int k = 0; k < n; k++) pushFrame(k * fw, 0, fw, r.h);
        }
        g_sprites[s.key] = std::move(s);
        r.px.clear();
        r.px.shrink_to_fit();
    }

    std::fprintf(stderr, "[assets] sprites built\n");
    std::fflush(stderr);
    // Flattest tiles first so the main ground fills look clean.
    for (int c = 0; c < (int)Fill::COUNT; c++)
        std::sort(g_fills[c].begin(), g_fills[c].end(), [](const TileRef& a, const TileRef& b) { return a.variance < b.variance; });

    glGenTextures(1, &g_texture);
    glBindTexture(GL_TEXTURE_2D, g_texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, atlas.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    std::fprintf(stderr, "[assets] atlas %dx%d, %zu sprites\n", size, size, g_sprites.size());
    for (int c = 0; c < (int)Fill::COUNT; c++)
        std::fprintf(stderr, "[assets] fill %d: %zu tiles\n", c, g_fills[c].size());
    return true;
}

GLuint atlasTexture() { return g_texture; }
int atlasSize() { return g_atlasSize; }
int spriteCount() { return (int)g_sprites.size(); }

const Sprite* find(const std::string& key) {
    auto it = g_sprites.find(key);
    return it == g_sprites.end() ? nullptr : &it->second;
}

const Sprite& get(const std::string& key) {
    static const Sprite empty;
    const Sprite* s = find(key);
    return s ? *s : empty;
}

const Sprite* search(const std::vector<std::string>& parts) {
    for (auto& kv : g_sprites) {
        bool all = true;
        for (auto& p : parts)
            if (kv.first.find(p) == std::string::npos) { all = false; break; }
        if (all) return &kv.second;
    }
    return nullptr;
}

std::vector<const Sprite*> keysWithPrefix(const std::string& prefix) {
    std::vector<const Sprite*> v;
    for (auto it = g_sprites.lower_bound(prefix); it != g_sprites.end() && it->first.compare(0, prefix.size(), prefix) == 0; ++it)
        v.push_back(&it->second);
    return v;
}

const std::vector<TileRef>& fills(Fill f) { return g_fills[(int)f]; }

}  // namespace Assets
