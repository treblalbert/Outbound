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
#include <algorithm>
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
static FocusRect g_titled{0, 0, 0, 0};   // the last panel drawn with a title this frame

void beginFrame() {
    R::clearTextBox();
    g_hasTooltip = false;
    g_focus.clear();
    g_titled = {0, 0, 0, 0};
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

// ---- the art pack's UI skin (0.12v)
// Panels, buttons, item cells, sliders and check boxes are drawn with the pack's own
// UI sheets (UI/Inventory, UI/Menu, UI/Crafting), cut in nine so they fit any size:
// the corners stay as drawn, the edges repeat and the middle stretches. Without the
// pack the flat colours below are used.
const Assets::Sprite* skin(const char* key) {
    static std::vector<std::pair<std::string, const Assets::Sprite*>> cache;
    for (auto& c : cache)
        if (c.first == key) return c.second;
    const Assets::Sprite* s = Assets::find(std::string("ui/") + key);
    if (s && s->valid()) cache.push_back({key, s});
    return s && s->valid() ? s : nullptr;
}

// Part of a frame: the pixels (px, py, pw, ph) of it.
static Assets::Frame subFrame(const Assets::Frame& f, float px, float py, float pw, float ph) {
    Assets::Frame o;
    float du = (f.u1 - f.u0) / f.w, dv = (f.v1 - f.v0) / f.h;
    o.u0 = f.u0 + du * px;
    o.u1 = f.u0 + du * (px + pw);
    o.v0 = f.v0 + dv * py;
    o.v1 = f.v0 + dv * (py + ph);
    o.w = (int)pw;
    o.h = (int)ph;
    return o;
}

// A run of a frame's pixels laid along a length: repeated at its own size (the last
// copy cut short), or stretched.
static void span(const Assets::Frame& f, float px, float py, float pw, float ph, float x, float y, float w, float h,
                 bool repeatX, bool repeatY, Color c) {
    if (w <= 0 || h <= 0 || pw <= 0 || ph <= 0) return;
    float stepX = repeatX ? pw : w, stepY = repeatY ? ph : h;
    for (float oy = 0; oy < h; oy += stepY)
        for (float ox = 0; ox < w; ox += stepX) {
            float cw = std::min(stepX, w - ox), ch = std::min(stepY, h - oy);
            float sw = repeatX ? cw : pw, sh = repeatY ? ch : ph;
            R::frame(subFrame(f, px, py, sw, sh), x + ox, y + oy, cw, ch, c);
        }
}

void nine(const Assets::Frame& f, float x, float y, float w, float h, int l, int t, int r, int b, Color c, bool repeatEdges) {
    x = std::floor(x); y = std::floor(y); w = std::floor(w); h = std::floor(h);
    float mw = (float)(f.w - l - r), mh = (float)(f.h - t - b);
    float iw = w - l - r, ih = h - t - b;
    if (iw < 0 || ih < 0) { R::frame(f, x, y, w, h, c); return; }
    R::frame(subFrame(f, 0, 0, l, t), x, y, l, t, c);
    R::frame(subFrame(f, f.w - r, 0, r, t), x + w - r, y, r, t, c);
    R::frame(subFrame(f, 0, f.h - b, l, b), x, y + h - b, l, b, c);
    R::frame(subFrame(f, f.w - r, f.h - b, r, b), x + w - r, y + h - b, r, b, c);
    span(f, l, 0, mw, t, x + l, y, iw, t, repeatEdges, false, c);
    span(f, l, f.h - b, mw, b, x + l, y + h - b, iw, b, repeatEdges, false, c);
    span(f, 0, t, l, mh, x, y + t, l, ih, false, repeatEdges, c);
    span(f, f.w - r, t, r, mh, x + w - r, y + t, r, ih, false, repeatEdges, c);
    span(f, l, t, mw, mh, x + l, y + t, iw, ih, false, false, c);
}

bool nineSprite(const char* key, float x, float y, float w, float h, int l, int t, int r, int b, Color c, bool repeatEdges) {
    const Assets::Sprite* s = skin(key);
    if (!s) return false;
    nine(s->frame(0), x, y, w, h, l, t, r, b, c, repeatEdges);
    return true;
}

bool skinSprite(const char* key, float x, float y, int frame, Color c) {
    const Assets::Sprite* s = skin(key);
    if (!s) return false;
    const Assets::Frame& f = s->frame(frame);
    R::frame(f, std::floor(x), std::floor(y), (float)f.w, (float)f.h, c);
    return true;
}

// Text with a one-pixel dark ring, readable on the light buttons.
void textOutline(const std::string& s, float x, float y, Color c, Color ring) {
    for (int oy = -1; oy <= 1; oy++)
        for (int ox = -1; ox <= 1; ox++)
            if (ox || oy) R::text(s, x + ox, y + oy, ring);
    R::text(s, x, y, c);
}

// The pack's inks: the frames' dark line and the buttons' beige.
static Color ink(int r, int g, int b, float a = 1) { return Color(r / 255.0f, g / 255.0f, b / 255.0f, a); }
static Color INK_DARK() { return ink(44, 29, 53); }

void panel(float x, float y, float w, float h, const std::string& title) {
    if (hover(x, y, w, h)) g_overPanelNext = g_overPanel = true;
    R::setTextBox(x, y, x + w, y + h);
    R::rect(x + 2, y + 2, w, h, pal(P_DARK, 0.6f));
    bool skinned = w >= 16 && h >= 16 && nineSprite("inventory/inventory_1", x, y, w, h, 5, 6, 5, 6, Color(), true);
    if (!skinned) {
        R::rect(x, y, w, h, pal(P_DARK));
        R::rectOutline(x, y, w, h, pal(P_PURPLE));
        R::rectOutline(x + 1, y + 1, w - 2, h - 2, pal(P_DARK));
    }
    if (!title.empty()) {
        g_titled = {x, y, w, h};
        if (skinned) {
            // A title plate in the frame's dark ink, underlined with its beige.
            R::rect(x + 5, y + 4, w - 10, 10, ink(71, 64, 89));
            R::rect(x + 5, y + 14, w - 10, 1, ink(199, 187, 167, 0.7f));
            R::textShadow(title, x + 7, y + 6, pal(P_WHITE), 1, INK_DARK());
        } else {
            R::rect(x + 1, y + 1, w - 2, 13, pal(P_PURPLE));
            R::text(title, x + 5, y + 4, pal(P_WHITE));
        }
    }
}

// A panel whose contents scroll or page (UI/Inventory/Inventory_1_Scrollbar): the
// scroll box sits `scroll` (0..1) of the way down its bar.
void panelScroll(float x, float y, float w, float h, const std::string& title, float scroll) {
    panel(x, y, w, h, title);
    if (w < 24 || h < 40 || !nineSprite("inventory/inventory_1_scrollbar", x + w - 12, y, 12, h, 1, 6, 9, 6, Color(), true)) return;
    if (const Assets::Sprite* s = skin("inventory/inventory_scrollbox_1")) {
        const Assets::Frame& f = s->frame(0);
        float top = y + (title.empty() ? 7 : 17), travel = std::max(0.0f, y + h - 7 - f.h - top);
        R::frame(f, std::floor(x + w - 7), std::floor(top + travel * clampf(scroll, 0, 1)), (float)f.w, (float)f.h);
    }
}

// The lighter sheet (the crafting menu's), for a list inside a panel. With `scroll`
// (0..1, or <0 for none) its scroll bar's box sits that far down the bar.
void subPanel(float x, float y, float w, float h, float scroll) {
    if (!nineSprite("crafting/crafting-main-menu", x, y, w, h, 3, 3, 10, 3, Color(), true)) {
        R::rect(x, y, w, h, pal(P_PURPLE, 0.5f));
        R::rectOutline(x, y, w, h, pal(P_PURPLE));
        return;
    }
    if (scroll >= 0)
        if (const Assets::Sprite* s = skin("crafting/crafting_scrollbox")) {
            const Assets::Frame& f = s->frame(0);
            float travel = std::max(0.0f, h - 10 - f.h);
            R::frame(f, std::floor(x + w - 8), std::floor(y + 5 + travel * clampf(scroll, 0, 1)), f.w, f.h);
        }
}

bool panelClose() { return g_titled.w > 0 && closeBox(g_titled.x + g_titled.w - 8, g_titled.y - 2); }

// The close box in a panel's corner (the inventory's X). True when clicked.
bool closeBox(float x, float y) {
    focusable(x - 2, y - 2, 11, 11);
    bool hov = hover(x - 2, y - 2, 11, 11);
    bool down = hov && Input::mouseHeld(0);
    if (!skinSprite(down ? "inventory/inventory_close_pressed" : "inventory/inventory_close_not-pressed", x, y + (down ? 1 : 0), 0,
                    hov ? Color(1.15f, 1.1f, 1.1f) : Color())) {
        R::rect(x, y, 7, 7, pal(hov ? P_CORAL : P_PURPLE));
        R::text("x", x + 1, y - 1, pal(P_WHITE));
    }
    if (hov && (Input::mousePressed(0) || (Input::gamepad() && Input::pressed(GLFW_KEY_E)))) {
        Input::consumeMouse();
        Audio::play(Snd::click, 0.5f);
        return true;
    }
    return false;
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
    float tw = R::textWidth(label);
    float tx = std::floor(x + (w - tw) / 2), ty = std::floor(y + (h - 7) / 2);
    // The pack's beige button (UI/Menu/Main Menu/Blank): pressed in while pointed at.
    const bool tall = h >= 10 && w >= 14;
    if (tall && hov && nineSprite("menu/main menu/blank_pressed", x, y + 2, w, h - 2, 6, 2, 6, 3, Color(), false)) {
        textOutline(label, tx, ty + 1, color == P_WHITE ? pal(P_WHITE) : pal(color), INK_DARK());
    } else if (tall && nineSprite("menu/main menu/blank_not-pressed", x, y, w, h, 6, 2, 6, 5, enabled ? Color() : Color(0.72f, 0.7f, 0.76f), false)) {
        if (!enabled) R::text(label, tx, ty - 1, ink(58, 48, 70, 0.85f));
        else if (color == P_WHITE) R::text(label, tx, ty - 1, INK_DARK());
        else textOutline(label, tx, ty - 1, pal(color), INK_DARK());
    } else {
        int bg = !enabled ? P_DARK : hov ? P_LAVENDER : P_PURPLE;
        R::rect(x, y, w, h, pal(bg));
        R::rectOutline(x, y, w, h, pal(hov ? P_WHITE : P_DARK));
        int tc = !enabled ? P_PURPLE : hov ? P_DARK : color;
        R::text(label, tx, ty, pal(tc));
    }
    if (hov && (Input::mousePressed(0) || (Input::gamepad() && Input::pressed(GLFW_KEY_E)))) {
        Input::consumeMouse();
        Audio::play(Snd::click, 0.5f);
        return true;
    }
    return false;
}

// A crafting line (UI/Crafting): the result, then `=` (or `<` for an upgrade) and what
// goes in, joined by `+`. One or two inputs use the pack's drawn strips (Crafting_1_1,
// _1_2, _2_1, _2_2); more are put together from its cell, plus, equal and arrow.
// Returns the width it took.
float recipe(float x, float y, int result, Color resultTint, const int* ins, const int* counts, int n, bool upgrade) {
    x = std::floor(x); y = std::floor(y);
    std::vector<float> cells;
    char key[40];
    std::snprintf(key, sizeof key, "crafting/crafting_%d_%d", upgrade ? 2 : 1, n);
    const Assets::Sprite* strip = n >= 1 && n <= 2 ? skin(key) : nullptr;
    float width = 0;
    if (strip) {
        const Assets::Frame& f = strip->frame(0);
        R::frame(f, x, y, (float)f.w, (float)f.h);
        float first = upgrade ? 28 : 26;
        cells = {0, first};
        if (n == 2) cells.push_back((float)f.w - 21);
        width = (float)f.w;
    } else {
        const Assets::Sprite* cell = skin("crafting/crafting-cell");
        auto drawCell = [&](float cx) {
            if (cell) R::frame(cell->frame(0), cx, y, 21, 22);
            else { R::rect(cx, y, 21, 21, pal(P_DARK)); R::rectOutline(cx, y, 21, 21, pal(P_BEIGE)); }
            cells.push_back(cx - x);
        };
        float cx = x;
        drawCell(cx);
        cx += 21;
        for (int i = 0; i < n; i++) {
            const char* join = i == 0 ? (upgrade ? "crafting/crafting_arrow" : "crafting/crafting_equal") : "crafting/crafting_plus";
            const Assets::Sprite* js = skin(join);
            float jw = js ? (float)js->frame(0).w : 6;
            if (js) R::frame(js->frame(0), cx, y + std::floor((21 - js->frame(0).h) / 2), jw, (float)js->frame(0).h);
            else R::text(i == 0 ? (upgrade ? "<" : "=") : "+", cx, y + 7, pal(P_BEIGE));
            cx += jw;
            drawCell(cx);
            cx += 21;
        }
        width = cx - x;
    }
    auto icon = [&](int k, int id, int count, Color tint) {
        if (k >= (int)cells.size() || id <= IT_NONE) return;
        float cx = x + cells[k];
        itemIcon(id, cx + 3, y + 3, 15, tint);
        if (count > 1) {
            std::string c = std::to_string(count);
            R::textShadow(c, cx + 20 - R::textWidth(c), y + 14, pal(P_WHITE));
        }
    };
    // The result's cell washed in `resultTint` (a gun's new tier colour, say).
    if (resultTint.a > 0 && !cells.empty()) R::rect(x + cells[0] + 2, y + 2, 17, 17, resultTint.withA(resultTint.a * 0.35f));
    icon(0, result, 1, Color());
    for (int i = 0; i < n; i++) icon(i + 1, ins[i], counts ? counts[i] : 1, Color());
    return width;
}

// The main menu's buttons. The pack also draws lettered ones (Play, Load, Save,
// Settings, Quit), but every button keeps plain text on the same blank one so they all
// match (and translate); `art` is kept for the callers' sake.
bool menuButton(float x, float y, float w, float h, const char* art, const std::string& label, bool enabled) {
    (void)art;
    return button(x, y, w, h, label, enabled);
}

// The small green tick and red cross (UI/Menu/Button_Yes, Button_No). 1 yes, 2 no.
int yesNo(float x, float y) {
    int res = 0;
    for (int i = 0; i < 2; i++) {
        float bx = x + i * 14;
        focusable(bx - 2, y - 2, 14, 13);
        bool hov = hover(bx - 2, y - 2, 14, 13);
        const char* key = i == 0 ? (hov ? "menu/button_yes_pressed" : "menu/button_yes_not-pressed")
                                 : (hov ? "menu/button_no_pressed" : "menu/button_no_not-pressed");
        if (!skinSprite(key, bx, y + (hov ? 1 : 0))) {
            R::rect(bx, y, 9, 9, pal(i == 0 ? P_YGREEN : P_CORAL));
            R::text(i == 0 ? "Y" : "N", bx + 2, y + 1, pal(P_DARK));
        }
        if (hov && (Input::mousePressed(0) || (Input::gamepad() && Input::pressed(GLFW_KEY_E)))) {
            Input::consumeMouse();
            Audio::play(Snd::click, 0.5f);
            res = i + 1;
        }
    }
    return res;
}

// A check box (UI/Menu/Checkmark): the tick draws itself in when it is turned on.
bool checkbox(float x, float y, bool& on, const std::string& label, int labelColor) {
    static std::vector<std::pair<const bool*, float>> ticks;   // when each box was last turned on
    float lw = label.empty() ? 0 : R::textWidth(label) + 5;
    focusable(x - 2, y - 2, 11 + lw, 11);
    bool hov = hover(x - 2, y - 2, 11 + lw, 11);
    bool changed = false;
    if (hov && (Input::mousePressed(0) || (Input::gamepad() && Input::pressed(GLFW_KEY_E)))) {
        Input::consumeMouse();
        Audio::play(Snd::click, 0.5f);
        on = !on;
        changed = true;
    }
    float since = 1e9f, now = (float)glfwGetTime();
    auto it = std::find_if(ticks.begin(), ticks.end(), [&](auto& p) { return p.first == &on; });
    if (changed && on) {
        if (it == ticks.end()) ticks.push_back({&on, now});
        else it->second = now;
    }
    for (auto& p : ticks)
        if (p.first == &on) since = now - p.second;
    if (!skinSprite("menu/checkmark-body", x, y, 0, hov ? Color(1.15f, 1.12f, 1.1f) : Color())) {
        R::rect(x, y, 7, 7, pal(P_DARK));
        R::rectOutline(x, y, 7, 7, pal(hov ? P_WHITE : P_LAVENDER));
    }
    if (on) {
        // "Checkmark-Sheet5" (the tick drawing itself in) and the still "Checkmark"
        // share a name; either way the last frame is the finished tick.
        if (const Assets::Sprite* sheet = skin("menu/checkmark")) {
            int n = sheet->frameCount();
            int fr = std::min(n - 1, (int)(since / 0.05f));
            const Assets::Frame& f = sheet->frame(fr);
            R::frame(f, x, y + 1, (float)f.w, (float)f.h);
        } else R::rect(x + 2, y + 2, 3, 3, pal(P_YGREEN));
    }
    if (!label.empty()) R::text(label, x + 11, y, pal(hov ? P_WHITE : labelColor));
    return changed;
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
    // The pack's inventory cell; the chosen one (UI/Inventory/Inventory-Chosen) when
    // pointed at or picked.
    bool skinned = (hov || highlight) ? nineSprite("inventory/inventory-chosen", x, y, SLOT, SLOT, 3, 3, 3, 3, highlight ? Color(1.1f, 1.05f, 0.8f) : Color(), false)
                                      : nineSprite("inventory/inventory-cell", x, y, SLOT, SLOT, 3, 3, 3, 4, Color(), false);
    if (!skinned) R::rect(x, y, SLOT, SLOT, pal(hov ? P_PURPLE : P_DARK));
    // Guns wear their tier's colour: a faint wash and the frame. Elite (legendary)
    // ones also glint in the corner.
    if (hasTier(it)) {
        Color tc = tierColor(itemTier(it));
        R::rect(x + 2, y + 2, SLOT - 4, SLOT - 4, tc.withA(hov ? 0.28f : 0.18f));
        if (skinned) R::rectOutline(x + 1, y + 1, SLOT - 2, SLOT - 2, highlight ? pal(P_YELLOW) : tc);
        else R::rectOutline(x, y, SLOT, SLOT, highlight ? pal(P_YELLOW) : tc);
    } else if (!skinned) {
        R::rectOutline(x, y, SLOT, SLOT, pal(highlight ? P_YELLOW : (hov ? P_LAVENDER : P_PURPLE)));
    } else if (highlight) {
        R::rectOutline(x + 1, y + 1, SLOT - 2, SLOT - 2, pal(P_YELLOW, 0.8f));
    }
    if (elite) {
        float glint = 0.55f + 0.45f * std::sin((float)(x + y) * 0.1f + (float)glfwGetTime() * 4.0f);
        R::rect(x + 1, y + 1, 3, 1, pal(P_YELLOW, glint));
        R::rect(x + 1, y + 1, 1, 3, pal(P_YELLOW, glint));
    }
    if (!it.empty()) {
        const ItemDef& d = itemDef(it.id);
        itemIcon(it.id, x + 2, y + 2, 16, dim ? pal(P_PURPLE) : Color(), it.count);
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

void itemIcon(int itemId, float x, float y, float box, Color tint, int count) {
    if (const Assets::Sprite* s = Art::itemIcon(itemId, count)) {
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

// The pack's scroll bar (UI/Menu/Scrollbar) as the track, filled up to the value, and
// its scroll box as the knob (the red one, Scrollbar_Scrollbox_1, while held).
static void drawSlider(float x, float y, float w, float h, float value, bool hot, bool held) {
    float kx = std::floor(x + value * (w - 4));
    if (nineSprite("menu/scrollbar", x, y + h / 2 - 3.5f, w, 7, 3, 3, 3, 3, hot ? Color(1.12f, 1.1f, 1.08f) : Color(), true)) {
        float fw = std::floor((w - 4) * value);
        if (fw > 0) R::rect(x + 2, std::floor(y + h / 2 - 3.5f) + 2, fw, 3, pal(P_BLUE, 0.85f));
        if (skinSprite(held ? "menu/scrollbar_scrollbox_1" : "menu/scrollbar_scrollbox", kx, std::floor(y + h / 2 - 4.5f))) return;
    } else {
        R::rect(x, y, w, h, pal(P_DARK));
        R::rect(x, y, std::floor(w * value), h, pal(P_BLUE));
        R::rectOutline(x, y, w, h, pal(hot || held ? P_WHITE : P_PURPLE));
    }
    R::rect(kx, y - 2, 4, h + 4, pal(P_WHITE));
}

bool slider(float x, float y, float w, float h, float& value) {
    value = clampf(value, 0, 1);
    // Only the slider that started a drag follows it; the others stay put.
    bool mine = s_sliderDrag && s_dragX == x && s_dragY == y;
    bool sliderDrag = mine;
    focusable(x - 2, y - 3, w + 4, h + 6);
    bool hot = hover(x - 2, y - 2, w + 4, h + 4);
    // Controller: left/right on the focused slider nudges it instead of moving focus.
    if (!sliderDrag && hover(x - 2, y - 3, w + 4, h + 6) && Input::usingPad()) {
        int n = Input::navDir();
        if (n == 2 || n == 3) {
            value = clampf(value + (n == 3 ? 0.05f : -0.05f), 0, 1);
            Input::consumeNav();
        }
    }
    if (!sliderDrag) {
        if (hot && Input::mousePressed(0)) {
            Input::consumeMouse();
            s_sliderDrag = true;
            s_dragX = x;
            s_dragY = y;
            float v = clampf((Input::mouse().x - x) / w, 0, 1);
            bool ch = (v != value);
            value = v;
            drawSlider(x, y, w, h, value, true, true);
            return ch;
        }
        drawSlider(x, y, w, h, value, hot, false);
        return false;
    }
    // Active drag: use the raw held state so consumeMouse() from prior frames
    // does not end the drag early.
    if (Input::mouseHeld(0)) {
        float v = clampf((Input::mouse().x - x) / w, 0, 1);
        bool ch = (v != value);
        value = v;
        Input::consumeMouse();
        drawSlider(x, y, w, h, value, true, true);
        return ch;
    }
    s_sliderDrag = false;
    drawSlider(x, y, w, h, value, false, false);
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

static bool g_cursor = false;
void setCursor(bool drawn) { g_cursor = drawn; }
void drawCursor();

void endFrame() {
    R::clearTextBox();
    if (!g_hasTooltip) { drawCursor(); return; }
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
    if (!nineSprite("inventory/inventory_1", x - 2, y - 2, w + 14, h + 4, 5, 6, 5, 6, Color(), true)) {
        R::rect(x, y, w + 10, h, pal(P_DARK));
        R::rectOutline(x, y, w + 10, h, pal(P_LAVENDER));
    }
    R::textShadow(g_ttTitle, x + 5, y + 4, g_ttColor, 1, INK_DARK());
    for (size_t i = 0; i < lines.size(); i++) R::text(lines[i], x + 5, y + 14 + i * 9, pal(P_BEIGE));
    drawCursor();
}

void drawCursor() {
    if (!g_cursor) return;
    Vec2 m = Input::mouse();
    if (!skinSprite("menu/cursor", m.x, m.y)) {
        R::rect(m.x, m.y, 1, 6, pal(P_WHITE));
        R::rect(m.x, m.y, 4, 1, pal(P_WHITE));
    }
}

}  // namespace UI
