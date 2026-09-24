#include "input.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace Input {

static bool g_keys[GLFW_KEY_LAST + 1], g_prevKeys[GLFW_KEY_LAST + 1];
static bool g_mouse[8], g_prevMouse[8];
static Vec2 g_mousePos;          // what DEV_ALL answers (the real mouse, or the pad's cursor)
static Vec2 g_rawMouse;          // the real mouse, always
static float g_scrollAccum = 0, g_scroll = 0;
static bool g_usingPad = false;  // DEV_ALL: the controller has the game right now
static Vec2 g_padCursor;         // DEV_ALL's controller cursor
static Vec2 g_lastRawMouse(-1, -1);
static int g_bind[GLFW_KEY_LAST + 1];
static bool g_capture = false;
static bool g_bindInit = false;
static bool g_kbmHit = false;
static int g_viewW = 640, g_viewH = 360;

// Every controller, read every frame (0.12v: local co-op gives each its own player).
struct Pad {
    GLFWgamepadstate cur{}, prev{};
    bool on = false;
    int type = PAD_XBOX;
    std::string name;
    Vec2 cursor;                // the cursor this controller drives as a local co-op seat
    bool cursorSet = false;
    int nav = -1, navHeld = -1;
    double navRepeatAt = 0;
    bool cursorMoved = false;   // its right stick moved a cursor this frame
    bool touched = false;       // really used this frame (not a drifting stick)
};
static Pad g_pads[MAX_PADS];
static int g_lastPad = -1;

// Per-device switches the scenes set (index: 0..15 a controller, 16 DEV_ALL, 17 the keyboard).
struct Cfg { bool cursorOn = true, menu = false, dpadWalk = true, suppressFire = false, consumed = false; };
static Cfg g_cfg[MAX_PADS + 2];
static int g_dev = DEV_ALL;
constexpr int CFG_ALL = MAX_PADS, CFG_KBM = MAX_PADS + 1;

static int cfgIndex() { return g_dev == DEV_ALL ? CFG_ALL : g_dev == DEV_KBM ? CFG_KBM : std::clamp(g_dev, 0, MAX_PADS - 1); }
static Cfg& cfg() { return g_cfg[cfgIndex()]; }
template <class F> static void setCfg(F f) {
    if (g_dev == DEV_ALL) { for (Cfg& c : g_cfg) f(c); }
    else f(cfg());
}
static bool kbmOn() { return g_dev == DEV_ALL || g_dev == DEV_KBM; }
// The controller the current device reads, or nullptr.
static Pad* activePad() {
    if (g_dev == DEV_KBM) return nullptr;
    if (g_dev >= 0 && g_dev < MAX_PADS) return g_pads[g_dev].on ? &g_pads[g_dev] : nullptr;
    if (g_lastPad >= 0 && g_pads[g_lastPad].on) return &g_pads[g_lastPad];
    for (Pad& p : g_pads) if (p.on) return &p;
    return nullptr;
}

static void initBinds() {
    if (g_bindInit) return;
    for (int k = 0; k <= GLFW_KEY_LAST; k++) g_bind[k] = k;
    g_bindInit = true;
}
static int phys(int key) {
    initBinds();
    return key >= 0 && key <= GLFW_KEY_LAST ? g_bind[key] : key;
}

static void scrollCb(GLFWwindow*, double, double y) { g_scrollAccum += (float)y; }

static std::string g_typedAccum, g_typed;
static int g_backAccum = 0, g_back = 0;
static void charCb(GLFWwindow*, unsigned int cp) {
    if (cp >= 32 && cp < 127) g_typedAccum += (char)cp;
}
static void keyCb(GLFWwindow*, int key, int, int action, int) {
    if (key == GLFW_KEY_BACKSPACE && (action == GLFW_PRESS || action == GLFW_REPEAT)) g_backAccum++;
}

void init(GLFWwindow* w) {
    glfwSetScrollCallback(w, scrollCb);
    glfwSetCharCallback(w, charCb);
    glfwSetKeyCallback(w, keyCb);
}

std::string typed() { return kbmOn() ? g_typed : std::string(); }
int backspaces() { return kbmOn() ? g_back : 0; }

// PlayStation pads by the name the driver gives them, or Sony's USB vendor id (054c)
// in the SDL-style GUID GLFW reports (bytes 4-5, little endian).
static int detectType(int j, std::string& name) {
    const char* gn = glfwGetGamepadName(j);
    const char* jn = glfwGetJoystickName(j);
    name = gn ? gn : jn ? jn : "";
    std::string all = name + " " + (jn ? jn : "");
    for (char& c : all) c = (char)std::tolower((unsigned char)c);
    const char* ps[] = {"ps3", "ps4", "ps5", "playstation", "dualshock", "dualsense", "sony", "wireless controller"};
    for (const char* k : ps) if (all.find(k) != std::string::npos) return PAD_PLAYSTATION;
    if (const char* guid = glfwGetJoystickGUID(j)) {
        std::string g = guid;
        if (g.size() >= 12) {
            std::string vendor = g.substr(10, 2) + g.substr(8, 2);
            for (char& c : vendor) c = (char)std::tolower((unsigned char)c);
            if (vendor == "054c") return PAD_PLAYSTATION;
        }
    }
    return PAD_XBOX;
}

void update(GLFWwindow* w, int pixelScale) {
    std::memcpy(g_prevKeys, g_keys, sizeof g_keys);
    std::memcpy(g_prevMouse, g_mouse, sizeof g_mouse);
    for (int k = GLFW_KEY_SPACE; k <= GLFW_KEY_LAST; k++) g_keys[k] = glfwGetKey(w, k) == GLFW_PRESS;
    static int frames = 0;
    bool focused = glfwGetWindowAttrib(w, GLFW_FOCUSED) && ++frames > 15;  // ignore the click that focused the window
    if (!glfwGetWindowAttrib(w, GLFW_FOCUSED)) frames = 0;
    for (int b = 0; b < 8; b++) g_mouse[b] = focused && glfwGetMouseButton(w, b) == GLFW_PRESS;
    double mx, my;
    glfwGetCursorPos(w, &mx, &my);
    g_mousePos = g_rawMouse = Vec2((float)mx / pixelScale, (float)my / pixelScale);
    int ww = 0, wh = 0;
    glfwGetWindowSize(w, &ww, &wh);
    g_viewW = std::max(1, ww / std::max(1, pixelScale));
    g_viewH = std::max(1, wh / std::max(1, pixelScale));
    // The real mouse moving (or clicking) hands control back from the pad.
    Vec2 raw((float)mx, (float)my);
    bool mouseMoved = g_lastRawMouse.x >= 0 && lengthSq(raw - g_lastRawMouse) > 4.0f;
    g_lastRawMouse = raw;
    // So does any key being pressed (0.11v): you can put the controller down and pick
    // up the keyboard mid-game without touching the mouse.
    bool keyHit = false;
    for (int k = GLFW_KEY_SPACE; k <= GLFW_KEY_LAST && !keyHit; k++) keyHit = g_keys[k] && !g_prevKeys[k];
    bool clicked = (g_mouse[0] && !g_prevMouse[0]) || (g_mouse[1] && !g_prevMouse[1]);
    g_kbmHit = keyHit || clicked;
    if (mouseMoved || clicked || keyHit) g_usingPad = false;
    g_scroll = g_scrollAccum;
    g_scrollAccum = 0;
    g_typed.swap(g_typedAccum);
    g_typedAccum.clear();
    g_back = g_backAccum;
    g_backAccum = 0;
    for (Cfg& c : g_cfg) c.consumed = false;

    double now = glfwGetTime();
    bool anyTouched = false;
    for (int j = 0; j < MAX_PADS; j++) {
        Pad& p = g_pads[j];
        p.prev = p.cur;
        bool was = p.on;
        p.on = glfwJoystickIsGamepad(GLFW_JOYSTICK_1 + j) && glfwGetGamepadState(GLFW_JOYSTICK_1 + j, &p.cur);
        if (!p.on) { std::memset(&p.cur, 0, sizeof p.cur); p.prev = p.cur; p.touched = false; p.nav = -1; p.navHeld = -1; continue; }
        if (!was) { p.type = detectType(GLFW_JOYSTICK_1 + j, p.name); p.prev = p.cur; }
        p.cursorMoved = false;
        // Menu navigation: D-pad or a clear push of the left stick, repeating while held.
        int held = -1;
        float lx = p.cur.axes[GLFW_GAMEPAD_AXIS_LEFT_X], ly = p.cur.axes[GLFW_GAMEPAD_AXIS_LEFT_Y];
        if (p.cur.buttons[GLFW_GAMEPAD_BUTTON_DPAD_UP] == GLFW_PRESS || (ly < -0.6f && std::fabs(ly) > std::fabs(lx))) held = 0;
        else if (p.cur.buttons[GLFW_GAMEPAD_BUTTON_DPAD_DOWN] == GLFW_PRESS || (ly > 0.6f && std::fabs(ly) > std::fabs(lx))) held = 1;
        else if (p.cur.buttons[GLFW_GAMEPAD_BUTTON_DPAD_LEFT] == GLFW_PRESS || lx < -0.6f) held = 2;
        else if (p.cur.buttons[GLFW_GAMEPAD_BUTTON_DPAD_RIGHT] == GLFW_PRESS || lx > 0.6f) held = 3;
        p.nav = -1;
        if (held >= 0 && held != p.navHeld) { p.nav = held; p.navRepeatAt = now + 0.38; }
        else if (held >= 0 && now >= p.navRepeatAt) { p.nav = held; p.navRepeatAt = now + 0.11; }
        p.navHeld = held;
        // The controller takes over when it is actually used: a button or trigger going
        // down, or a stick pushed well out or moving. A stick that drifts, or a button
        // held from before, cannot keep grabbing control back from the mouse (0.11v).
        Vec2 rs(p.cur.axes[GLFW_GAMEPAD_AXIS_RIGHT_X], p.cur.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y]);
        Vec2 ls(lx, ly);
        Vec2 prs(p.prev.axes[GLFW_GAMEPAD_AXIS_RIGHT_X], p.prev.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y]);
        Vec2 pls(p.prev.axes[GLFW_GAMEPAD_AXIS_LEFT_X], p.prev.axes[GLFW_GAMEPAD_AXIS_LEFT_Y]);
        auto stickUsed = [](Vec2 n, Vec2 before) {
            return lengthSq(n) > 0.36f || (lengthSq(n) > 0.09f && lengthSq(n - before) > 0.02f);
        };
        auto triggerDown = [&](int a) { return p.cur.axes[a] > 0.35f && p.prev.axes[a] <= 0.35f; };
        bool touched = stickUsed(rs, prs) || stickUsed(ls, pls) ||
                       triggerDown(GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER) || triggerDown(GLFW_GAMEPAD_AXIS_LEFT_TRIGGER);
        for (int b = 0; b <= GLFW_GAMEPAD_BUTTON_LAST && !touched; b++)
            touched = p.cur.buttons[b] == GLFW_PRESS && p.prev.buttons[b] != GLFW_PRESS;
        // A trigger held down keeps the controller in charge while firing.
        if (g_usingPad && j == g_lastPad && p.cur.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] > 0.35f) touched = true;
        p.touched = touched;
        if (touched) { g_lastPad = j; anyTouched = true; }
        // Its own cursor, as a local co-op seat's pointer in menus.
        const Cfg& pc = g_cfg[j];
        if (!p.cursorSet) { p.cursor = Vec2(g_viewW * 0.5f, g_viewH * 0.5f); p.cursorSet = true; }
        if (pc.cursorOn && lengthSq(rs) > 0.12f) {
            p.cursor.x = clampf(p.cursor.x + rs.x * 5.0f, 0, g_viewW - 1.0f);
            p.cursor.y = clampf(p.cursor.y + rs.y * 5.0f, 0, g_viewH - 1.0f);
            p.cursorMoved = true;
        }
    }
    if (anyTouched) g_usingPad = true;
    if (g_lastPad >= 0 && !g_pads[g_lastPad].on) g_lastPad = -1;

    // DEV_ALL's cursor: the mouse, or the last controller's right stick in menus.
    Pad* ap = nullptr;
    if (g_lastPad >= 0) ap = &g_pads[g_lastPad];
    else for (Pad& p : g_pads) if (p.on) { ap = &p; break; }
    if (ap) {
        Vec2 rs(ap->cur.axes[GLFW_GAMEPAD_AXIS_RIGHT_X], ap->cur.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y]);
        if (!g_cfg[CFG_ALL].cursorOn) {
            // Gameplay: the stick aims; the cursor stays where the UI will want it.
            if (g_usingPad) g_mousePos = g_padCursor;
        } else if (lengthSq(rs) > 0.12f) {
            if (g_padCursor.x == 0 && g_padCursor.y == 0) g_padCursor = g_mousePos;
            g_padCursor.x = clampf(g_padCursor.x + rs.x * 5.0f, 0, g_viewW - 1.0f);
            g_padCursor.y = clampf(g_padCursor.y + rs.y * 5.0f, 0, g_viewH - 1.0f);
            g_mousePos = g_padCursor;
            ap->cursorMoved = true;
        } else if (g_usingPad && (g_padCursor.x != 0 || g_padCursor.y != 0)) {
            g_mousePos = g_padCursor;
        } else g_padCursor = g_mousePos;
    } else g_usingPad = false;
    for (int i = 0; i < MAX_PADS + 2; i++) {
        Cfg& c = g_cfg[i];
        if (!c.suppressFire) continue;
        bool mouseUp = !g_mouse[0];
        bool trigUp = true;
        if (i < MAX_PADS) { mouseUp = true; trigUp = !g_pads[i].on || g_pads[i].cur.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] < 0.35f; }
        else if (i == CFG_KBM) trigUp = true;
        else trigUp = !ap || ap->cur.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] < 0.35f;
        if (mouseUp && trigUp) c.suppressFire = false;
    }
}

static bool padButton(const Pad* p, int b) { return p && p->cur.buttons[b] == GLFW_PRESS; }
static bool padForKey(const Pad* p, const GLFWgamepadstate& s, int key) {
    if (!p) return false;
    const Cfg& c = cfg();
    auto btn = [&](int b) { return s.buttons[b] == GLFW_PRESS; };
    switch (key) {
    // In a menu the left stick and D-pad move between buttons, not the player.
    case GLFW_KEY_W: case GLFW_KEY_S: case GLFW_KEY_A: case GLFW_KEY_D:
        if (c.menu) return false;
        break;
    default: break;
    }
    switch (key) {
    case GLFW_KEY_W: return s.axes[GLFW_GAMEPAD_AXIS_LEFT_Y] < -0.35f || (c.dpadWalk && btn(GLFW_GAMEPAD_BUTTON_DPAD_UP));
    case GLFW_KEY_S: return s.axes[GLFW_GAMEPAD_AXIS_LEFT_Y] >  0.35f || (c.dpadWalk && btn(GLFW_GAMEPAD_BUTTON_DPAD_DOWN));
    case GLFW_KEY_A: return s.axes[GLFW_GAMEPAD_AXIS_LEFT_X] < -0.35f || (c.dpadWalk && btn(GLFW_GAMEPAD_BUTTON_DPAD_LEFT));
    case GLFW_KEY_D: return s.axes[GLFW_GAMEPAD_AXIS_LEFT_X] >  0.35f || (c.dpadWalk && btn(GLFW_GAMEPAD_BUTTON_DPAD_RIGHT));
    case GLFW_KEY_LEFT_SHIFT: return btn(GLFW_GAMEPAD_BUTTON_LEFT_THUMB);
    case GLFW_KEY_E: return btn(GLFW_GAMEPAD_BUTTON_A);
    case GLFW_KEY_R: return !c.menu && btn(GLFW_GAMEPAD_BUTTON_X);
    case GLFW_KEY_G: return !c.menu && btn(GLFW_GAMEPAD_BUTTON_LEFT_BUMPER);
    case GLFW_KEY_H: return !c.menu && btn(GLFW_GAMEPAD_BUTTON_Y);
    case GLFW_KEY_Q: return !c.menu && btn(GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER);
    case GLFW_KEY_TAB: return btn(GLFW_GAMEPAD_BUTTON_BACK);
    case GLFW_KEY_M: return !c.menu && btn(GLFW_GAMEPAD_BUTTON_B);
    case GLFW_KEY_L: return btn(GLFW_GAMEPAD_BUTTON_RIGHT_THUMB);
    case GLFW_KEY_SPACE: return !c.menu && btn(GLFW_GAMEPAD_BUTTON_X);   // the handbrake
    case GLFW_KEY_ESCAPE: return btn(GLFW_GAMEPAD_BUTTON_START) || (c.menu && btn(GLFW_GAMEPAD_BUTTON_B));
    default: return false;
    }
}
bool down(int key) {
    const Pad* p = activePad();
    if (g_capture) return p && padForKey(p, p->cur, key);
    int k = phys(key);
    return (kbmOn() && k >= 0 && k <= GLFW_KEY_LAST && g_keys[k]) || (p && padForKey(p, p->cur, key));
}
bool pressed(int key) {
    if (g_capture) return false;
    const Pad* p = activePad();
    int k = phys(key);
    return (kbmOn() && k >= 0 && k <= GLFW_KEY_LAST && g_keys[k] && !g_prevKeys[k]) ||
           (p && padForKey(p, p->cur, key) && !padForKey(p, p->prev, key));
}
bool keyPressed(int key) {
    if (g_capture || !kbmOn()) return false;
    int k = phys(key);
    return k >= 0 && k <= GLFW_KEY_LAST && g_keys[k] && !g_prevKeys[k];
}

// ---- devices
void setDevice(int dev) { g_dev = (dev == DEV_ALL || dev == DEV_KBM || (dev >= 0 && dev < MAX_PADS)) ? dev : DEV_ALL; }
int device() { return g_dev; }
int lastPad() { return g_lastPad; }
bool padConnected(int pad) { return pad >= 0 && pad < MAX_PADS && g_pads[pad].on; }
bool padButtonPressed(int pad, int button) {
    if (!padConnected(pad) || button < 0 || button > GLFW_GAMEPAD_BUTTON_LAST) return false;
    const Pad& p = g_pads[pad];
    return p.cur.buttons[button] == GLFW_PRESS && p.prev.buttons[button] != GLFW_PRESS;
}
bool kbmPressedAny() { return g_kbmHit; }
int padTypeOf(int pad) { return padConnected(pad) ? g_pads[pad].type : PAD_XBOX; }
int padType() {
    const Pad* p = activePad();
    return p ? p->type : PAD_XBOX;
}
std::string padName(int pad) {
    if (!padConnected(pad)) return "";
    const Pad& p = g_pads[pad];
    if (p.type == PAD_PLAYSTATION) {
        std::string n = p.name;
        for (char& c : n) c = (char)std::tolower((unsigned char)c);
        if (n.find("dualsense") != std::string::npos || n.find("ps5") != std::string::npos) return "DualSense";
        return "DualShock";
    }
    return "Xbox controller";
}

// ---- bindings
static const Bindable BINDABLES[] = {
    {GLFW_KEY_W, "Move up"}, {GLFW_KEY_S, "Move down"}, {GLFW_KEY_A, "Move left"}, {GLFW_KEY_D, "Move right"},
    {GLFW_KEY_LEFT_SHIFT, "Sprint"}, {GLFW_KEY_E, "Interact / search"}, {GLFW_KEY_R, "Reload"},
    {GLFW_KEY_1, "Weapon 1"}, {GLFW_KEY_2, "Weapon 2"}, {GLFW_KEY_Q, "Switch weapon"},
    {GLFW_KEY_G, "Throw grenade"}, {GLFW_KEY_H, "Quick heal"}, {GLFW_KEY_TAB, "Inventory"},
    {GLFW_KEY_M, "Map"}, {GLFW_KEY_L, "Toggle laser"}, {GLFW_KEY_V, "Push to talk"},
    {GLFW_KEY_SPACE, "Handbrake"},
};
const Bindable* bindables(int& count) { count = (int)(sizeof(BINDABLES) / sizeof(BINDABLES[0])); return BINDABLES; }

int binding(int action) { return phys(action); }

void setBinding(int action, int key) {
    initBinds();
    if (action < 0 || action > GLFW_KEY_LAST || key < 0 || key > GLFW_KEY_LAST) return;
    // Whatever action had that key takes this one's old key, so nothing is left unbound.
    int old = g_bind[action];
    for (const Bindable& b : BINDABLES)
        if (b.action != action && g_bind[b.action] == key) g_bind[b.action] = old;
    g_bind[action] = key;
}

void resetBindings() { g_bindInit = false; initBinds(); }

void setCapture(bool on) { g_capture = on; }
bool capturing() { return g_capture; }

int keyPressedAny() {
    if (!kbmOn()) return -1;
    for (int k = GLFW_KEY_SPACE; k <= GLFW_KEY_LAST; k++)
        if (g_keys[k] && !g_prevKeys[k]) return k;
    return -1;
}

std::string keyName(int key) {
    switch (key) {
    case GLFW_KEY_SPACE: return "SPACE";
    case GLFW_KEY_LEFT_SHIFT: return "SHIFT";
    case GLFW_KEY_RIGHT_SHIFT: return "R-SHIFT";
    case GLFW_KEY_LEFT_CONTROL: return "CTRL";
    case GLFW_KEY_RIGHT_CONTROL: return "R-CTRL";
    case GLFW_KEY_LEFT_ALT: return "ALT";
    case GLFW_KEY_RIGHT_ALT: return "ALT GR";
    case GLFW_KEY_TAB: return "TAB";
    case GLFW_KEY_ENTER: return "ENTER";
    case GLFW_KEY_BACKSPACE: return "BACKSPACE";
    case GLFW_KEY_ESCAPE: return "ESC";
    case GLFW_KEY_CAPS_LOCK: return "CAPS";
    case GLFW_KEY_UP: return "UP";
    case GLFW_KEY_DOWN: return "DOWN";
    case GLFW_KEY_LEFT: return "LEFT";
    case GLFW_KEY_RIGHT: return "RIGHT";
    case GLFW_KEY_INSERT: return "INSERT";
    case GLFW_KEY_DELETE: return "DELETE";
    case GLFW_KEY_HOME: return "HOME";
    case GLFW_KEY_END: return "END";
    case GLFW_KEY_PAGE_UP: return "PG UP";
    case GLFW_KEY_PAGE_DOWN: return "PG DOWN";
    }
    if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F25) return "F" + std::to_string(key - GLFW_KEY_F1 + 1);
    if (key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_9) return "NUM " + std::to_string(key - GLFW_KEY_KP_0);
    if ((key >= GLFW_KEY_A && key <= GLFW_KEY_Z) || (key >= GLFW_KEY_0 && key <= GLFW_KEY_9)) return std::string(1, (char)key);
    if (const char* n = glfwGetKeyName(key, 0)) {
        std::string s = n;
        for (char& c : s) c = (char)std::toupper((unsigned char)c);
        return s;
    }
    return "KEY " + std::to_string(key);
}

std::string bindingsText() {
    initBinds();
    std::string out;
    for (const Bindable& b : BINDABLES)
        if (g_bind[b.action] != b.action) out += std::to_string(b.action) + " " + std::to_string(g_bind[b.action]) + " ";
    return out;
}

void bindingsFromText(const std::string& text) {
    resetBindings();
    size_t i = 0;
    while (i < text.size()) {
        char* end = nullptr;
        long a = std::strtol(text.c_str() + i, &end, 10);
        if (end == text.c_str() + i) break;
        i = end - text.c_str();
        long k = std::strtol(text.c_str() + i, &end, 10);
        if (end == text.c_str() + i) break;
        i = end - text.c_str();
        for (const Bindable& b : BINDABLES)
            if (b.action == a && k >= GLFW_KEY_SPACE && k <= GLFW_KEY_LAST) g_bind[a] = (int)k;
    }
}

// ---- mouse, or the controller's trigger and cursor
static bool triggerHeld(const Pad* p, bool prev = false) {
    if (!p) return false;
    const GLFWgamepadstate& s = prev ? p->prev : p->cur;
    return s.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] > 0.35f;
}
bool mouseDown(int b) {
    const Cfg& c = cfg();
    if (c.consumed || c.suppressFire) return false;
    return (kbmOn() && g_mouse[b]) || (b == 0 && triggerHeld(activePad()));
}
bool mouseHeld(int b) { return kbmOn() && b >= 0 && b < 8 && g_mouse[b]; }
bool mousePressed(int b) {
    const Cfg& c = cfg();
    if (c.consumed || c.suppressFire) return false;
    const Pad* p = activePad();
    bool trigger = b == 0 && triggerHeld(p) && !triggerHeld(p, true);
    return (kbmOn() && g_mouse[b] && !g_prevMouse[b]) || trigger;
}
bool mouseReleased(int b) { return kbmOn() && !cfg().consumed && !g_mouse[b] && g_prevMouse[b]; }
void consumeMouse() { setCfg([](Cfg& c) { c.consumed = true; }); }
Vec2 mouse() {
    if (g_dev == DEV_KBM) return g_rawMouse;
    if (g_dev >= 0 && g_dev < MAX_PADS) return g_pads[g_dev].cursor;
    return g_mousePos;
}
float scroll() { return kbmOn() ? g_scroll : 0.0f; }
bool gamepad() { return activePad() != nullptr; }
bool usingPad() {
    if (g_dev == DEV_KBM) return false;
    if (g_dev >= 0) return padConnected(g_dev);
    return activePad() && g_usingPad;
}
Vec2 moveAxis() {
    const Pad* p = activePad();
    if (!p || cfg().menu || !usingPad()) return {};
    Vec2 v(p->cur.axes[GLFW_GAMEPAD_AXIS_LEFT_X], p->cur.axes[GLFW_GAMEPAD_AXIS_LEFT_Y]);
    return lengthSq(v) < 0.12f ? Vec2() : v;
}
Vec2 aimAxis() {
    // Only while the controller is in charge: a drifting stick must not turn the gun
    // away from the mouse.
    const Pad* p = activePad();
    if (!p || !usingPad()) return {};
    Vec2 v(p->cur.axes[GLFW_GAMEPAD_AXIS_RIGHT_X], p->cur.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y]);
    return lengthSq(v) < 0.16f ? Vec2() : v;
}
float padTrigger(bool right) {
    const Pad* p = activePad();
    if (!p || !usingPad()) return 0;
    float v = p->cur.axes[right ? GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER : GLFW_GAMEPAD_AXIS_LEFT_TRIGGER];   // -1 at rest .. 1
    return clampf(((v + 1.0f) * 0.5f - 0.08f) / 0.92f, 0, 1);
}
void suppressFireUntilRelease() { setCfg([](Cfg& c) { c.suppressFire = true; }); }
void setPadCursor(bool on) { setCfg([on](Cfg& c) { c.cursorOn = on; }); }
void setPadMenu(bool on) { setCfg([on](Cfg& c) { c.menu = on; }); }
bool padMenu() { return cfg().menu; }
int navDir() {
    Pad* p = activePad();
    return p && cfg().menu ? p->nav : -1;
}
void consumeNav() { if (Pad* p = activePad()) p->nav = -1; }
static bool menuButton(int b) {
    const Pad* p = activePad();
    return p && cfg().menu && p->cur.buttons[b] == GLFW_PRESS && p->prev.buttons[b] != GLFW_PRESS;
}
bool padAltPressed() { return menuButton(GLFW_GAMEPAD_BUTTON_X); }
bool padGrabPressed() { return menuButton(GLFW_GAMEPAD_BUTTON_Y); }
int padPagePressed() {
    if (menuButton(GLFW_GAMEPAD_BUTTON_LEFT_BUMPER)) return -1;
    if (menuButton(GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER)) return 1;
    return 0;
}
void warpPadCursor(Vec2 p) {
    if (g_dev >= 0 && g_dev < MAX_PADS) { g_pads[g_dev].cursor = p; g_pads[g_dev].cursorSet = true; return; }
    g_padCursor = p;
    g_mousePos = p;
    g_usingPad = true;
}
bool padCursorMoved() {
    const Pad* p = activePad();
    return p && p->cursorMoved;
}
bool dpadPressed(int dir) {
    static const int B[4] = {GLFW_GAMEPAD_BUTTON_DPAD_UP, GLFW_GAMEPAD_BUTTON_DPAD_DOWN, GLFW_GAMEPAD_BUTTON_DPAD_LEFT, GLFW_GAMEPAD_BUTTON_DPAD_RIGHT};
    const Pad* p = activePad();
    if (!p || dir < 0 || dir > 3) return false;
    return p->cur.buttons[B[dir]] == GLFW_PRESS && p->prev.buttons[B[dir]] != GLFW_PRESS;
}
void setDpadWalk(bool on) { setCfg([on](Cfg& c) { c.dpadWalk = on; }); }

}  // namespace Input
