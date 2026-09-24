#include "prompt.h"
#include "assets.h"
#include "input.h"
#include "render.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <cctype>

namespace Prompt {

namespace {
// What shows for each action: on the keyboard the key it is bound to (by its default,
// see Input::binding) or a mouse sprite, and the controller button.
struct Glyph { int key; const char* mouse; const char* mouseText; const char* pad; const char* padText; const char* ps; const char* psText; };
const Glyph GLYPHS[COUNT] = {
    {GLFW_KEY_E, nullptr, nullptr, "pad_a", "A", "ps_cross", "CROSS"},                  // Interact
    {GLFW_KEY_TAB, nullptr, nullptr, "pad_view", "VIEW", "ps_share", "SHARE"},          // Inventory
    {GLFW_KEY_M, nullptr, nullptr, "pad_b", "B", "ps_circle", "CIRCLE"},                // Mission
    {GLFW_KEY_M, nullptr, nullptr, "pad_b", "B", "ps_circle", "CIRCLE"},                // Map
    {GLFW_KEY_ESCAPE, nullptr, nullptr, "pad_menu", "MENU", "ps_options", "OPTIONS"},   // Pause
    {GLFW_KEY_ESCAPE, nullptr, nullptr, "pad_b", "B", "ps_circle", "CIRCLE"},           // Back
    {GLFW_KEY_R, nullptr, nullptr, "pad_x", "X", "ps_square", "SQUARE"},                // Reload
    {GLFW_KEY_H, nullptr, nullptr, "pad_y", "Y", "ps_triangle", "TRIANGLE"},            // Heal
    {GLFW_KEY_G, nullptr, nullptr, "pad_lb", "LB", "ps_l1", "L1"},                      // Grenade
    {GLFW_KEY_Q, nullptr, nullptr, "pad_rb", "RB", "ps_r1", "R1"},                      // Swap
    {GLFW_KEY_LEFT_SHIFT, nullptr, nullptr, "pad_ls_click", "LS", "pad_ls_click", "L3"},// Sprint
    {GLFW_KEY_L, nullptr, nullptr, "pad_rs_click", "RS", "pad_rs_click", "R3"},         // Laser
    {-1, "mouse_l", "LMB", "pad_rt", "RT", "ps_r2", "R2"},                              // Shoot
    {-1, "mouse", "MOUSE", "pad_rs", "RS", "pad_rs", "R"},                              // Aim
    {GLFW_KEY_W, nullptr, nullptr, "pad_ls", "LS", "pad_ls", "L"},                      // Move
    {-1, "mouse_wheel", "WHEEL", "pad_dpad_ud", "DPAD", "pad_dpad_ud", "DPAD"},         // Choose
    {-1, "mouse_l", "LMB", "pad_a", "A", "ps_cross", "CROSS"},                          // Select
    {-1, "mouse_r", "RMB", "pad_x", "X", "ps_square", "SQUARE"},                        // AltSelect
    {GLFW_KEY_F11, nullptr, nullptr, "kb_f11", "F11", "kb_f11", "F11"},                 // Fullscreen
    {GLFW_KEY_W, nullptr, nullptr, "pad_rt", "RT", "ps_r2", "R2"},                      // Accelerate
    {GLFW_KEY_S, nullptr, nullptr, "pad_lt", "LT", "ps_l2", "L2"},                      // Brake / reverse
    {GLFW_KEY_A, nullptr, nullptr, "pad_ls", "LS", "pad_ls", "L"},                      // Steer
    {GLFW_KEY_SPACE, nullptr, nullptr, "pad_x", "X", "ps_square", "SQUARE"},            // Handbrake
    {-1, "mouse_l", "DRAG", "pad_y", "Y", "ps_triangle", "TRIANGLE"},                   // Grab (move an item)
    {-1, nullptr, "", "pad_lb", "LB", "ps_l1", "L1"},                                   // PagePrev
    {-1, nullptr, "", "pad_rb", "RB", "ps_r1", "R1"},                                   // PageNext
    {GLFW_KEY_ENTER, nullptr, nullptr, "pad_menu", "MENU", "ps_options", "OPTIONS"},    // Join (local co-op)
};

int g_forcePad = -1;   // the controls page shows a controller's buttons whatever is in use

bool showPad() { return g_forcePad >= 0 || Input::usingPad(); }
bool showPS() { return (g_forcePad >= 0 ? g_forcePad : Input::padType()) == Input::PAD_PLAYSTATION; }

// "input/kb_shift" for SHIFT and so on: the sheet names keys the way keyName does.
std::string keySprite(int key) {
    std::string n = Input::keyName(key);
    for (char& c : n) c = c == ' ' ? '_' : (char)std::tolower((unsigned char)c);
    return "input/kb_" + n;
}

const Assets::Sprite* sprite(Action a) {
    if (a < 0 || a >= COUNT) return nullptr;
    const Glyph& g = GLYPHS[a];
    if (showPad()) return Assets::find(std::string("input/") + (showPS() ? g.ps : g.pad));
    if (g.key >= 0) return Assets::find(keySprite(Input::binding(g.key)));
    if (!g.mouse) return nullptr;
    return Assets::find(std::string("input/") + g.mouse);
}

std::string fallbackText(Action a) {
    const Glyph& g = GLYPHS[a];
    if (showPad()) return std::string("[") + (showPS() ? g.psText : g.padText) + "]";
    if (g.key >= 0) return "[" + Input::keyName(Input::binding(g.key)) + "]";
    return std::string("[") + g.mouseText + "]";
}
}  // namespace

void forcePad(int type) { g_forcePad = type; }

float iconWidth(Action a) {
    if (const Assets::Sprite* s = sprite(a)) return (float)s->w;
    return R::textWidth(fallbackText(a));
}

float icon(Action a, float x, float y, float alpha) {
    x = std::floor(x); y = std::floor(y);
    if (const Assets::Sprite* s = sprite(a)) {
        R::frame(s->frame(0), x, y, (float)s->w, (float)s->h, Color(1, 1, 1, alpha));
        return (float)s->w;
    }
    std::string t = fallbackText(a);
    R::text(t, x, y + 5, pal(P_YELLOW, alpha));
    return R::textWidth(t);
}

float label(Action a, const std::string& text, float x, float y, Color c, float alpha) {
    float w = icon(a, x, y, alpha);
    if (text.empty()) return w;
    R::textShadow(text, std::floor(x + w + 2), std::floor(y + 5), c.withA(c.a * alpha));
    return w + 2 + R::textWidth(text);
}

float labelWidth(Action a, const std::string& text) {
    return iconWidth(a) + (text.empty() ? 0 : 2 + R::textWidth(text));
}

void labelCentered(Action a, const std::string& text, float cx, float y, Color c, float alpha) {
    label(a, text, cx - labelWidth(a, text) / 2, y, c, alpha);
}

float row(const Hint* hints, int n, float x, float y, Color c, float gap) {
    float start = x;
    for (int i = 0; i < n; i++) x += label(hints[i].a, hints[i].text, x, y, c) + gap;
    return n > 0 ? x - start - gap : 0;
}

float rowWidth(const Hint* hints, int n, float gap) {
    float w = 0;
    for (int i = 0; i < n; i++) w += labelWidth(hints[i].a, hints[i].text) + (i ? gap : 0);
    return w;
}

}  // namespace Prompt
