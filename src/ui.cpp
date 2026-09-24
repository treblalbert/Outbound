#include "ui.h"
#include "art.h"
#include "audio.h"
#include "input.h"
#include "lang.h"
#include "render.h"
#include "sprites.h"
#include "prompt.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <sstream>
#include <vector>

namespace UI {

static bool g_hasTooltip = false;
static std::string g_ttTitle, g_ttBody;
static Color g_ttColor = pal(P_YELLOW);
static bool g_overPanel = false, g_overPanelNext = false;
struct FocusRect { float x, y, w, h; };
static std::vector<FocusRect> g_focus;      // widgets drawn this frame
static int g_navContext = -1;
static unsigned g_frame = 0;
// The on-screen keyboard (0.12v) is modal: while it is up only its keys take focus.
static bool g_oskUp = false, g_oskUpNext = false;
static FocusRect g_oskRect{0, 0, 0, 0};

void beginFrame() {
    R::clearTextBox();
    g_hasTooltip = false;
    g_focus.clear();
    g_frame++;
    g_oskUp = g_oskUpNext;
    g_oskUpNext = false;
    g_overPanel = g_overPanelNext;
    g_overPanelNext = false;
}

bool hover(float x, float y, float w, float h) {
    Vec2 m = Input::mouse();
    return m.x >= x && m.y >= y && m.x < x + w && m.y < y + h;
}

bool overPanel() { return g_overPanel; }

void panel(float x, float y, float w, float h, const std::string& title) {
    if (hover(x, y, w, h)) g_overPanelNext = g_overPanel = true;
    R::setTextBox(x, y, x + w, y + h);
    R::rect(x + 2, y + 2, w, h, pal(P_DARK, 0.6f));
    R::rect(x, y, w, h, pal(P_DARK));
    R::rectOutline(x, y, w, h, pal(P_PURPLE));
    R::rectOutline(x + 1, y + 1, w - 2, h - 2, pal(P_DARK));
    if (!title.empty()) {
        R::rect(x + 1, y + 1, w - 2, 13, pal(P_PURPLE));
        R::text(title, x + 5, y + 4, pal(P_WHITE));
    }
}

void focusable(float x, float y, float w, float h) {
    if (g_oskUp) {
        const FocusRect& o = g_oskRect;
        if (x < o.x || y < o.y || x + w > o.x + o.w || y + h > o.y + o.h) return;
    }
    g_focus.push_back({x, y, w, h});
}
unsigned frameNo() { return g_frame; }

int eliteColor() { return P_ORANGE; }

void padNavigate(int context) {
    if (!Input::gamepad() || !Input::padMenu() || g_focus.empty()) { g_navContext = -1; return; }
    Vec2 m = Input::mouse();
    auto center = [](const FocusRect& r) { return Vec2(r.x + r.w / 2, r.y + r.h / 2); };
    // A new screen or panel: start on its first widget (if the pad is in use).
    if (context != g_navContext) {
        g_navContext = context;
        if (Input::usingPad()) Input::warpPadCursor(center(g_focus[0]));
        return;
    }
    int dir = Input::navDir();
    if (dir < 0) return;
    // Where we are: the smallest widget under the cursor, or the cursor itself.
    int cur = -1;
    for (int i = 0; i < (int)g_focus.size(); i++) {
        const FocusRect& r = g_focus[i];
        if (m.x >= r.x && m.y >= r.y && m.x < r.x + r.w && m.y < r.y + r.h &&
            (cur < 0 || r.w * r.h < g_focus[cur].w * g_focus[cur].h))
            cur = i;
    }
    Vec2 from = cur >= 0 ? center(g_focus[cur]) : m;
    const Vec2 DIRS[4] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
    Vec2 d = DIRS[dir];
    int best = -1;
    float bestScore = 1e9f;
    for (int i = 0; i < (int)g_focus.size(); i++) {
        if (i == cur) continue;
        Vec2 v = center(g_focus[i]) - from;
        float along = dot(v, d);
        if (along < 3) continue;
        float across = std::fabs(v.x * d.y - v.y * d.x);
        float score = along + across * 2.5f;
        if (score < bestScore) { bestScore = score; best = i; }
    }
    if (best < 0 && cur < 0) best = 0;   // lost somewhere: back onto the first widget
    if (best >= 0) {
        Input::warpPadCursor(center(g_focus[best]));
        Audio::play(Snd::click, 0.25f, 1.4f);
    }
}

bool button(float x, float y, float w, float h, const std::string& label, bool enabled, int color) {
    if (enabled) focusable(x, y, w, h);
    bool hov = enabled && hover(x, y, w, h);
    int bg = !enabled ? P_DARK : hov ? P_LAVENDER : P_PURPLE;
    R::rect(x, y, w, h, pal(bg));
    R::rectOutline(x, y, w, h, pal(hov ? P_WHITE : P_DARK));
    int tc = !enabled ? P_PURPLE : hov ? P_DARK : color;
    float tw = R::textWidth(label);
    R::text(label, std::floor(x + (w - tw) / 2), std::floor(y + (h - 7) / 2), pal(tc));
    if (hov && (Input::mousePressed(0) || (Input::gamepad() && Input::pressed(GLFW_KEY_E)))) {
        Input::consumeMouse();
        Audio::play(Snd::click, 0.5f);
        return true;
    }
    return false;
}

// ---- the on-screen keyboard: typing a name with a controller (0.12v)
static std::string* g_oskText = nullptr;
static bool* g_oskFocus = nullptr;
static unsigned g_oskFrame = 0;
static bool g_oskCaps = true;

// A: type the key under the cursor. X: delete. Y: space. MENU / B / DONE: finished.
// The keyboard holds the input (capture) so nothing behind it reacts meanwhile.
static bool drawKeyboard(float fx, float fy, float fw, float fh, std::string& text, bool& focused, int maxLen) {
    static const char* ROWS[4] = {"1234567890", "QWERTYUIOP", "ASDFGHJKL-", "ZXCVBNM_.'"};
    const float key = 16, gap = 2;
    float kw = 10 * (key + gap) + 8, kh = 4 * (key + gap) + key + 26;
    float W = (float)R::width(), H = (float)R::height();
    float x = std::floor(clampf(fx + fw / 2 - kw / 2, 4, W - kw - 4));
    float y = fy + fh + 4;
    if (y + kh > H - 4) y = std::floor(fy - kh - 4);
    g_oskRect = {x, y, kw, kh};
    g_oskUpNext = true;
    panel(x, y, kw, kh, "");
    bool changed = false;
    bool pressA = Input::padButtonPressed(Input::device() >= 0 && Input::device() < Input::MAX_PADS ? Input::device() : Input::lastPad(), GLFW_GAMEPAD_BUTTON_A);
    auto padPressed = [](int b) {
        int pad = Input::device() >= 0 && Input::device() < Input::MAX_PADS ? Input::device() : Input::lastPad();
        return Input::padButtonPressed(pad, b);
    };
    auto keyBtn = [&](float kx, float ky, float w2, const std::string& label) {
        focusable(kx, ky, w2, key);
        bool hov = hover(kx, ky, w2, key);
        R::rect(kx, ky, w2, key, pal(hov ? P_LAVENDER : P_PURPLE));
        R::rectOutline(kx, ky, w2, key, pal(hov ? P_WHITE : P_DARK));
        R::textCentered(label, kx + w2 / 2, ky + 5, pal(hov ? P_DARK : P_WHITE), 1, false);
        return hov && (pressA || Input::mousePressed(0));
    };
    for (int r = 0; r < 4; r++)
        for (int c = 0; ROWS[r][c]; c++) {
            char ch = ROWS[r][c];
            if (!g_oskCaps && ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
            if (keyBtn(x + 4 + c * (key + gap), y + 4 + r * (key + gap), key, std::string(1, ch)) && (int)text.size() < maxLen) {
                text += ch;
                changed = true;
                Audio::play(Snd::click, 0.3f, 1.5f);
            }
        }
    float by = y + 4 + 4 * (key + gap);
    float bw = std::floor((kw - 8 - 3 * gap) / 4);
    bool done = false;
    if (keyBtn(x + 4, by, bw, g_oskCaps ? "abc" : "ABC")) g_oskCaps = !g_oskCaps;
    if (keyBtn(x + 4 + (bw + gap), by, bw, T("Space")) && (int)text.size() < maxLen) { text += ' '; changed = true; }
    if (keyBtn(x + 4 + 2 * (bw + gap), by, bw, T("Delete")) && !text.empty()) { text.pop_back(); changed = true; }
    if (keyBtn(x + 4 + 3 * (bw + gap), by, bw, T("Done"))) done = true;
    if (padPressed(GLFW_GAMEPAD_BUTTON_X) && !text.empty()) { text.pop_back(); changed = true; }
    if (padPressed(GLFW_GAMEPAD_BUTTON_Y) && (int)text.size() < maxLen) { text += ' '; changed = true; }
    if (padPressed(GLFW_GAMEPAD_BUTTON_START) || padPressed(GLFW_GAMEPAD_BUTTON_B)) done = true;
    const Prompt::Hint hints[] = {{Prompt::AltSelect, T("Delete")}, {Prompt::Grab, T("Space")}, {Prompt::Pause, T("Done")}};
    Prompt::row(hints, 3, x + 4, y + kh - 18, pal(P_LAVENDER), 8);
    if (done) {
        focused = false;
        g_oskText = nullptr;
        g_oskFocus = nullptr;
        Input::setCapture(false);
        Input::suppressFireUntilRelease();
        Audio::play(Snd::click, 0.5f);
    }
    return changed;
}

bool textField(float x, float y, float w, float h, std::string& text, bool& focused, int maxLen) {
    focusable(x, y, w, h);
    bool hov = hover(x, y, w, h);
    // With a controller, A on the box brings up the keyboard.
    if (!focused && hov && Input::usingPad() && Input::pressed(GLFW_KEY_E)) {
        focused = true;
        g_oskText = &text;
        g_oskFocus = &focused;
        g_oskFrame = g_frame;
        Input::setCapture(true);
        Input::consumeMouse();
        Audio::play(Snd::click, 0.5f);
    }
    bool osk = focused && g_oskText == &text;
    if (osk && g_oskFrame == g_frame) {
        // Opened this frame: its first key press comes next frame.
    } else if (osk) {
        R::rect(x, y, w, h, pal(P_DARK));
        R::rectOutline(x, y, w, h, pal(P_YELLOW));
        float ty = std::floor(y + (h - 7) / 2);
        R::text(text, x + 4, ty, pal(P_WHITE));
        if (std::fmod((float)glfwGetTime(), 1.0f) < 0.6f) R::rect(x + 5 + R::textWidth(text), ty, 1, 7, pal(P_YELLOW));
        return drawKeyboard(x, y, w, h, text, focused, maxLen);
    }
    if (Input::mousePressed(0)) {
        bool was = focused;
        focused = hov;
        if (hov) Input::consumeMouse();
        if (was != focused) Input::setCapture(focused);
    }
    bool changed = false;
    if (focused) {
        for (char c : Input::typed())
            if ((int)text.size() < maxLen) { text += c; changed = true; }
        for (int i = Input::backspaces(); i > 0 && !text.empty(); i--) { text.pop_back(); changed = true; }
        if (Input::keyPressedAny() == GLFW_KEY_ENTER || Input::keyPressedAny() == GLFW_KEY_ESCAPE) {
            focused = false;
            Input::setCapture(false);
        }
    }
    R::rect(x, y, w, h, pal(P_DARK));
    R::rectOutline(x, y, w, h, pal(focused ? P_YELLOW : hov ? P_LAVENDER : P_PURPLE));
    float ty = std::floor(y + (h - 7) / 2);
    R::text(text, x + 4, ty, pal(P_WHITE));
    if (focused && std::fmod((float)glfwGetTime(), 1.0f) < 0.6f) R::rect(x + 5 + R::textWidth(text), ty, 1, 7, pal(P_YELLOW));
    return changed;
}

int itemSlot(float x, float y, const Item& it, bool highlight, bool dim) {
    focusable(x, y, SLOT, SLOT);
    bool hov = hover(x, y, SLOT, SLOT);
    bool elite = !it.empty() && itemDef(it.id).elite;
    R::rect(x, y, SLOT, SLOT, pal(hov ? P_PURPLE : P_DARK));
    // Guns wear their tier's colour: a faint wash and the frame. Elite (legendary)
    // ones also glint in the corner.
    if (hasTier(it)) {
        Color tc = tierColor(itemTier(it));
        R::rect(x + 1, y + 1, SLOT - 2, SLOT - 2, tc.withA(hov ? 0.28f : 0.18f));
        R::rectOutline(x, y, SLOT, SLOT, highlight ? pal(P_YELLOW) : tc);
    } else {
        R::rectOutline(x, y, SLOT, SLOT, pal(highlight ? P_YELLOW : (hov ? P_LAVENDER : P_PURPLE)));
    }
    if (elite) {
        float glint = 0.55f + 0.45f * std::sin((float)(x + y) * 0.1f + (float)glfwGetTime() * 4.0f);
        R::rect(x + 1, y + 1, 3, 1, pal(P_YELLOW, glint));
        R::rect(x + 1, y + 1, 1, 3, pal(P_YELLOW, glint));
    }
    if (!it.empty()) {
        const ItemDef& d = itemDef(it.id);
        itemIcon(it.id, x + 2, y + 2, 16, dim ? pal(P_PURPLE) : Color());
        if (it.count > 1) {
            std::string c = std::to_string(it.count);
            R::textShadow(c, x + SLOT - 1 - R::textWidth(c), y + SLOT - 8, pal(P_WHITE));
        }
        if (d.cat == Cat::Armor && d.param > 0) UI::bar(x + 2, y + SLOT - 3, SLOT - 4, 2, it.data / float(d.param), P_BLUE);
        if (hov) itemTooltip(it);
    }
    if (hov) {
        if (Input::mousePressed(0) || (Input::gamepad() && Input::pressed(GLFW_KEY_E))) { Input::consumeMouse(); return 1; }
        if (Input::mousePressed(1) || Input::padAltPressed()) { Input::consumeMouse(); return 2; }
    }
    return 0;
}

void tooltip(const std::string& title, const std::string& body, int titleColor) {
    g_hasTooltip = true;
    g_ttTitle = title;
    g_ttBody = body;
    g_ttColor = pal(titleColor);
}

void tooltip(const std::string& title, const std::string& body, Color titleColor) {
    g_hasTooltip = true;
    g_ttTitle = title;
    g_ttBody = body;
    g_ttColor = titleColor;
}

static bool w_mods(const Item& it) { return weaponDef(it.id) && (it.flags & (ITEMF_LASER | ITEMF_MAG_EXT | ITEMF_MAG_DRUM)); }

void itemTooltip(const Item& it, const std::string& extra) {
    if (it.empty()) return;
    const ItemDef& d = itemDef(it.id);
    std::string body = T(d.desc);
    if (const WeaponDef* w = weaponDef(it.id)) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "%.1f/s", w->fireRate);
        std::string pellets = w->pellets > 1 ? "x" + std::to_string(w->pellets) : "";
        body += "\n" + T("DMG") + " " + std::to_string((int)std::round(w->damage * tierDamage(itemTier(it)))) + pellets + "  " + T("RATE") + " " + buf;
        body += "\n" + T("MAG") + " " + std::to_string(it.data) + "/" + std::to_string(magSizeOf(it)) + "  " +
                T1("Ammo: {0}", T(itemDef(w->ammo).name));
    }
    if (w_mods(it)) {
        std::string m;
        if (it.flags & ITEMF_LASER) m += T("Laser pointer");
        if (it.flags & (ITEMF_MAG_EXT | ITEMF_MAG_DRUM)) m += (m.empty() ? "" : ", ") + T((it.flags & ITEMF_MAG_DRUM) ? "Drum magazine" : "Extended magazine");
        body += "\n" + T1("Fitted: {0}", m);
    }
    if (d.cat == Cat::Armor) body += "\n" + T2("Durability {0}/{1}", std::to_string(it.data), std::to_string(d.param));
    if (d.cat == Cat::Armor && it.data < d.param) body += "\n" + T("Drag onto another of the same vest to combine them.");
    body += "\n" + T1("Value ${0}", std::to_string(itemValue(it)));
    if (d.elite) {
        body = T("ELITE TIER - found only out in the world") + "\n" + body;
        if (int base = baseWeapon(it.id); base != it.id) body += "\n" + T1("A better {0}.", T(itemDef(base).name));
    }
    if (hasTier(it)) body = T(tierName(itemTier(it))) + "\n" + body;
    if (!extra.empty()) body += "\n" + extra;
    const int rarityCol[4] = {P_WHITE, P_LGREEN, P_BLUE, P_PINK};
    std::string title = d.elite ? "* " + itemLabel(it) + " *" : itemLabel(it);
    if (hasTier(it)) tooltip(title, body, tierColor(itemTier(it)));
    else tooltip(title, body, rarityCol[d.rarity]);
}

void itemIcon(int itemId, float x, float y, float box, Color tint) {
    if (const Assets::Sprite* s = Art::itemIcon(itemId)) {
        const Assets::Frame& f = s->frame(0);
        float sc = std::min(1.0f, std::min(box / f.w, box / f.h));
        R::frame(f, std::floor(x + (box - f.w * sc) * 0.5f), std::floor(y + (box - f.h * sc) * 0.5f), f.w * sc, f.h * sc, tint);
        return;
    }
    R::spriteRect(itemDef(itemId).sprite, x + (box - 16) * 0.5f, y + (box - 16) * 0.5f, 16, 16, tint);
}

void bar(float x, float y, float w, float h, float frac, int fg, int bg) {
    frac = clampf(frac, 0, 1);
    R::rect(x, y, w, h, pal(bg));
    R::rect(x, y, std::floor(w * frac), h, pal(fg));
}

static bool s_sliderDrag = false;
static float s_dragX = 0, s_dragY = 0;   // which slider the drag belongs to (its position)

bool slider(float x, float y, float w, float h, float& value) {
    value = clampf(value, 0, 1);
    // Only the slider that started a drag follows it; the others stay put.
    bool mine = s_sliderDrag && s_dragX == x && s_dragY == y;
    bool sliderDrag = mine;
    focusable(x - 2, y - 3, w + 4, h + 6);
    // Controller: left/right on the focused slider nudges it instead of moving focus.
    if (!sliderDrag && hover(x - 2, y - 3, w + 4, h + 6) && Input::usingPad()) {
        int n = Input::navDir();
        if (n == 2 || n == 3) {
            value = clampf(value + (n == 3 ? 0.05f : -0.05f), 0, 1);
            Input::consumeNav();
        }
    }
    if (!sliderDrag) {
        if (hover(x - 2, y - 2, w + 4, h + 4) && Input::mousePressed(0)) {
            Input::consumeMouse();
            s_sliderDrag = true;
            s_dragX = x;
            s_dragY = y;
            float v = clampf((Input::mouse().x - x) / w, 0, 1);
            bool ch = (v != value);
            value = v;
            R::rect(x, y, w, h, pal(P_DARK));
            R::rect(x, y, std::floor(w * value), h, pal(P_BLUE));
            R::rectOutline(x, y, w, h, pal(P_WHITE));
            float kx = x + value * (w - 4);
            R::rect(kx, y - 2, 4, h + 4, pal(P_WHITE));
            return ch;
        }
        R::rect(x, y, w, h, pal(P_DARK));
        R::rect(x, y, std::floor(w * value), h, pal(P_BLUE));
        R::rectOutline(x, y, w, h, pal(hover(x - 2, y - 2, w + 4, h + 4) ? P_WHITE : P_PURPLE));
        float kx = x + value * (w - 4);
        R::rect(kx, y - 2, 4, h + 4, pal(P_WHITE));
        return false;
    }
    // Active drag: use the raw held state so consumeMouse() from prior frames
    // does not end the drag early.
    if (Input::mouseHeld(0)) {
        float v = clampf((Input::mouse().x - x) / w, 0, 1);
        bool ch = (v != value);
        value = v;
        Input::consumeMouse();
        R::rect(x, y, w, h, pal(P_DARK));
        R::rect(x, y, std::floor(w * value), h, pal(P_BLUE));
        R::rectOutline(x, y, w, h, pal(P_WHITE));
        float kx = x + value * (w - 4);
        R::rect(kx, y - 2, 4, h + 4, pal(P_WHITE));
        return ch;
    }
    s_sliderDrag = false;
    R::rect(x, y, w, h, pal(P_DARK));
    R::rect(x, y, std::floor(w * value), h, pal(P_BLUE));
    R::rectOutline(x, y, w, h, pal(P_PURPLE));
    float kx = x + value * (w - 4);
    R::rect(kx, y - 2, 4, h + 4, pal(P_WHITE));
    return true;  // released: caller persists the final value
}

float textWrap(const std::string& s, float x, float y, float maxW, Color c, float lineH) {
    std::vector<std::string> lines;
    std::string cur, word;
    auto flushWord = [&]() {
        if (word.empty()) return;
        std::string next = cur.empty() ? word : cur + " " + word;
        if (!cur.empty() && R::textWidth(next) > maxW) { lines.push_back(cur); cur = word; }
        else cur = next;
        word.clear();
    };
    for (char ch : s) {
        if (ch == ' ') flushWord();
        else if (ch == '\n') { flushWord(); lines.push_back(cur); cur.clear(); }
        else word += ch;
    }
    flushWord();
    if (!cur.empty()) lines.push_back(cur);
    for (size_t i = 0; i < lines.size(); i++) R::text(lines[i], x, y + i * lineH, c);
    return lines.size() * lineH;
}

void endFrame() {
    R::clearTextBox();
    if (!g_hasTooltip) return;
    std::vector<std::string> lines;
    std::stringstream ss(g_ttBody);
    std::string line;
    float w = R::textWidth(g_ttTitle);
    while (std::getline(ss, line)) {
        lines.push_back(line);
        w = std::max(w, R::textWidth(line));
    }
    float h = 14 + lines.size() * 9;
    Vec2 m = Input::mouse();
    float x = m.x + 10, y = m.y + 10;
    if (x + w + 10 > R::width()) x = m.x - w - 14;
    if (y + h > R::height()) y = R::height() - h - 2;
    R::rect(x, y, w + 10, h, pal(P_DARK));
    R::rectOutline(x, y, w + 10, h, pal(P_LAVENDER));
    R::text(g_ttTitle, x + 5, y + 4, g_ttColor);
    for (size_t i = 0; i < lines.size(); i++) R::text(lines[i], x + 5, y + 14 + i * 9, pal(P_BEIGE));
}

}  // namespace UI
