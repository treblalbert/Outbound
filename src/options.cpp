#include "options.h"
#include "audio.h"
#include "game.h"
#include "lang.h"
#include "input.h"
#include "prompt.h"
#include "ui.h"
#include "voice.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <algorithm>
#include <fstream>
#include <string>

void ensureDir(const std::string& path);

namespace Options {

namespace {
int s_mode = DM_WINDOWED;
int s_lastFull = DM_FULLSCREEN;     // what F11 goes back to
float s_sfx = 1.0f;
int s_aimAssist = 1;
bool s_coopZoom = true;
int s_ctrlPage = -1;               // controls: 0 keyboard, 1 controller (-1: whichever is in use)
int s_ctrlPreview = -1;            // which controller's buttons the page shows (-1: the one plugged in)
GLFWwindow* s_window = nullptr;
int s_winX = 100, s_winY = 100, s_winW = 1280, s_winH = 720;   // the window before going full screen

std::string path() { return dataPath("saves/settings.txt"); }

void save() {
    ensureDir(dataPath("saves"));
    {
        std::ofstream f(path());
        if (f) f << "sfx " << s_sfx << "\nambient " << Audio::ambientVolume() << "\ndisplay " << s_mode << "\naimassist " << s_aimAssist
                 << "\ncoopzoom " << (s_coopZoom ? 1 : 0) << "\nbinds " << Input::bindingsText() << "\nvoice " << Voice::settingsText() << "\n";
    }
    persistSaves();
}

void applyMode() {
#ifndef __EMSCRIPTEN__
    GLFWwindow* w = s_window;
    if (!w) return;
    bool wasWindowed = glfwGetWindowMonitor(w) == nullptr && glfwGetWindowAttrib(w, GLFW_DECORATED);
    if (wasWindowed) {
        glfwGetWindowPos(w, &s_winX, &s_winY);
        glfwGetWindowSize(w, &s_winW, &s_winH);
    }
    GLFWmonitor* mon = glfwGetPrimaryMonitor();
    const GLFWvidmode* vm = mon ? glfwGetVideoMode(mon) : nullptr;
    if (s_mode == DM_FULLSCREEN && vm) {
        glfwSetWindowAttrib(w, GLFW_DECORATED, GLFW_TRUE);
        glfwSetWindowMonitor(w, mon, 0, 0, vm->width, vm->height, vm->refreshRate);
    } else if (s_mode == DM_BORDERLESS && vm) {
        // A plain window with no frame covering the whole monitor: alt-tabs instantly.
        int mx = 0, my = 0;
        glfwGetMonitorPos(mon, &mx, &my);
        glfwSetWindowMonitor(w, nullptr, mx, my, vm->width, vm->height, 0);
        glfwSetWindowAttrib(w, GLFW_DECORATED, GLFW_FALSE);
        glfwSetWindowPos(w, mx, my);
        glfwSetWindowSize(w, vm->width, vm->height);
    } else {
        glfwSetWindowAttrib(w, GLFW_DECORATED, GLFW_TRUE);
        glfwSetWindowMonitor(w, nullptr, s_winX, s_winY, s_winW, s_winH, 0);
    }
#endif
}
}  // namespace

void load() {
    std::ifstream f(path());
    std::string key;
    while (f >> key) {
        if (key == "sfx") f >> s_sfx;
        else if (key == "ambient") { float v = 0.7f; f >> v; Audio::setAmbientVolume(v); }
        else if (key == "display") f >> s_mode;
        else if (key == "aimassist") { f >> s_aimAssist; s_aimAssist = std::clamp(s_aimAssist, 0, 2); }
        else if (key == "coopzoom") { int v = 1; f >> v; s_coopZoom = v != 0; }
        else if (key == "binds") { std::string line; std::getline(f, line); Input::bindingsFromText(line); }
        else if (key == "voice") { std::string line; std::getline(f, line); Voice::settingsFromText(line); }
    }
    s_sfx = clampf(s_sfx, 0, 1);
    if (s_mode < 0 || s_mode >= DM_COUNT) s_mode = DM_WINDOWED;
    if (s_mode != DM_WINDOWED) s_lastFull = s_mode;
    Audio::setSfxVolume(s_sfx);
}

void apply(GLFWwindow* w) {
    s_window = w;
    if (s_mode != DM_WINDOWED) applyMode();
}

int displayMode() { return s_mode; }

void setDisplayMode(int mode) {
    if (mode < 0 || mode >= DM_COUNT || mode == s_mode) return;
    s_mode = mode;
    if (mode != DM_WINDOWED) s_lastFull = mode;
    applyMode();
    save();
}

void toggleFullscreen() { setDisplayMode(s_mode == DM_WINDOWED ? s_lastFull : DM_WINDOWED); }

float sfxVolume() { return s_sfx; }
int aimAssist() { return s_aimAssist; }
void setAimAssist(int level) { s_aimAssist = std::clamp(level, 0, 2); save(); }
bool coopZoom() { return s_coopZoom; }
void setCoopZoom(bool on) { s_coopZoom = on; save(); }

void setSfxVolume(float v) {
    s_sfx = clampf(v, 0, 1);
    Audio::setSfxVolume(s_sfx);
    save();
}

void drawPanel(float W, float H) {
    float w = 300, h = 278, x = std::floor(W / 2 - w / 2), y = std::floor(H / 2 - h / 2);
    UI::panel(x, y, w, h, T("OPTIONS"));
    float ly = y + 22;
    Voice::setMeter(true);   // the microphone runs for the level meter while this is open

    // ---- sound
    R::text(T("SOUND"), x + 10, ly, pal(P_YELLOW));
    ly += 12;
    auto volumeRow = [&](const char* name, float value, bool music) {
        R::text(T(name), x + 10, ly, pal(P_LAVENDER));
        char buf[16];
        std::snprintf(buf, sizeof buf, "%d%%", (int)std::round(value * 100.0f));
        R::text(buf, x + w - 10 - R::textWidth(buf), ly, pal(P_WHITE));
        float v = value;
        bool dragging = UI::slider(x + 80, ly + 1, w - 125, 6, v);
        if (dragging || v != value) {
            if (music) Audio::setMusicVolume(v);
            else {
                setSfxVolume(v);
                if (!dragging) Audio::play(Snd::click, 0.8f);
            }
        }
        ly += 16;
    };
    volumeRow("Music", Audio::musicVolume(), true);
    volumeRow("Effects", s_sfx, false);
    {
        // Ambience (0.11v): rain, birds, wind, drips.
        float value = Audio::ambientVolume();
        R::text(T("Ambience"), x + 10, ly, pal(P_LAVENDER));
        char buf[16];
        std::snprintf(buf, sizeof buf, "%d%%", (int)std::round(value * 100.0f));
        R::text(buf, x + w - 10 - R::textWidth(buf), ly, pal(P_WHITE));
        float v = value;
        bool dragging = UI::slider(x + 80, ly + 1, w - 125, 6, v);
        if (dragging || v != value) { Audio::setAmbientVolume(v); if (!dragging) save(); }
        ly += 16;
    }

    // ---- screen
    ly += 6;
    R::text(T("SCREEN"), x + 10, ly, pal(P_YELLOW));
    ly += 12;
#ifndef __EMSCRIPTEN__
    const char* names[DM_COUNT] = {"Windowed", "Full screen", "Borderless"};
    float bw = std::floor((w - 20 - 8) / 3);
    for (int i = 0; i < DM_COUNT; i++) {
        bool on = s_mode == i;
        if (UI::button(x + 10 + i * (bw + 4), ly, bw, 16, T(names[i]), true, on ? P_YELLOW : P_WHITE)) setDisplayMode(i);
        if (on) R::rectOutline(x + 9 + i * (bw + 4), ly - 1, bw + 2, 18, pal(P_YELLOW));
    }
    ly += 22;
    R::text(T("F11: windowed / full screen"), x + 10, ly, pal(P_BEIGE));
    {
        // Local co-op: may the shared camera pull back to keep everyone in view?
        bool z = s_coopZoom;
        std::string zl = T("Co-op zoom");
        if (UI::checkbox(x + w - 22 - R::textWidth(zl), ly, z, zl)) setCoopZoom(z);
    }
#else
    R::text(T("The browser decides the screen mode."), x + 10, ly, pal(P_BEIGE));
#endif

    // ---- voice chat (co-op)
    ly += 16;
    R::text(T("VOICE CHAT (CO-OP)"), x + 10, ly, pal(P_YELLOW));
    ly += 12;
    {
        float bw = std::floor((w - 20 - 8) / 3);
        bool on = Voice::enabled();
        if (UI::checkbox(x + 12, ly + 5, on, on ? T("On") : T("Off"), on ? P_YGREEN : P_CORAL)) { Voice::setEnabled(on); save(); }
        std::string ptt = T("Push to talk") + " (" + Input::keyName(Input::binding(GLFW_KEY_V)) + ")";
        const std::string names[2] = {ptt, T("Open mic")};
        for (int i = 0; i < 2; i++) {
            bool sel = Voice::mode() == i;
            float bx = x + 10 + (i + 1) * (bw + 4);
            if (UI::button(bx, ly, bw, 16, names[i], on, sel ? P_YELLOW : P_WHITE)) { Voice::setMode(i); save(); }
            if (sel && on) R::rectOutline(bx - 1, ly - 1, bw + 2, 18, pal(P_YELLOW));
        }
    }
    ly += 22;
    auto voiceRow = [&](const char* name, float value, float maxV, void (*set)(float)) {
        R::text(T(name), x + 10, ly, pal(P_LAVENDER));
        char buf[16];
        std::snprintf(buf, sizeof buf, "%d%%", (int)std::round(value * 100.0f));
        R::text(buf, x + w - 10 - R::textWidth(buf), ly, pal(P_WHITE));
        float v = value / maxV;
        bool dragging = UI::slider(x + 80, ly + 1, w - 125, 6, v);
        if (dragging || v != value / maxV) { set(v * maxV); if (!dragging) save(); }
        ly += 14;
    };
    voiceRow("Microphone", Voice::micGain(), 2.0f, Voice::setMicGain);
    // What the microphone hears right now; for open mic, the mark is where talking starts.
    {
        float mx = x + 80, mw = w - 125;
        R::rect(mx, ly - 3, mw, 3, pal(P_DARK));
        R::rect(mx, ly - 3, mw * clampf(Voice::micLevel(), 0, 1), 3, pal(Voice::micLevel() > 0.9f ? P_CORAL : P_YGREEN));
        if (Voice::mode() == Voice::OPEN_MIC) {
            float gate = clampf((0.004f + (1.0f - Voice::sensitivity()) * 0.05f) * 4.0f, 0, 1);
            R::rect(mx + mw * gate, ly - 4, 1, 5, pal(P_YELLOW));
        }
        if (!Voice::micAvailable()) R::text(T("No microphone found"), mx, ly + 1, pal(P_CORAL));
        ly += 6;
    }
    voiceRow("Voices", Voice::volume(), 1.0f, Voice::setVolume);
    if (Voice::mode() == Voice::OPEN_MIC) voiceRow("Open mic sensitivity", Voice::sensitivity(), 1.0f, Voice::setSensitivity);
    else { R::text(T("You are only heard by players who can see you."), x + 10, ly, pal(P_BEIGE)); ly += 14; }

    if (UI::button(x + w / 2 - 45, y + h - 24, 90, 16, T("Back"))) {
        Voice::setMeter(false);
        bool inGame = G.scene == Scene::Base || G.scene == Scene::Raid || G.scene == Scene::Defense;
        G.panel = inGame ? Panel::Pause : Panel::None;
    }
}

namespace { int s_rebind = -1; }   // the action waiting for its new key

// The controller page: what every button does, drawn with the buttons of the controller
// that is plugged in (Xbox or PlayStation), and the aim assist setting.
static void drawPadControls(float x, float y, float w) {
    int type = s_ctrlPreview >= 0 ? s_ctrlPreview : Input::padTypeOf(Input::lastPad());
    Prompt::forcePad(type);
    struct Row { Prompt::Action a; const char* what; };
    static const Row PLAY[] = {
        {Prompt::Move, "Move"}, {Prompt::Aim, "Aim (with aim assist)"}, {Prompt::Shoot, "Shoot"}, {Prompt::Sprint, "Sprint"},
        {Prompt::Interact, "Interact / search / get in"}, {Prompt::Choose, "Pick what to interact with"}, {Prompt::Reload, "Reload"},
        {Prompt::Heal, "Quick heal"}, {Prompt::Grenade, "Throw grenade"}, {Prompt::Swap, "Switch weapon"},
        {Prompt::Laser, "Hit (tap) / laser (hold)"}, {Prompt::Inventory, "Inventory"}, {Prompt::Map, "Map"}, {Prompt::Pause, "Pause"},
    };
    static const Row DRIVE[] = {{Prompt::Accelerate, "Accelerate"}, {Prompt::Brake, "Brake / reverse"}, {Prompt::Steer, "Steer"}, {Prompt::Handbrake, "Handbrake"}};
    static const Row MENU[] = {{Prompt::Select, "Select"}, {Prompt::AltSelect, "Other action (drop, equip)"}, {Prompt::Grab, "Pick up an item to move it"},
                               {Prompt::Back, "Back / close"}, {Prompt::PageNext, "Next page"}};
    float colW = (w - 10) / 2;
    auto list = [&](const char* title, const Row* rows, int n, float lx, float& ly) {
        R::text(T(title), lx, ly, pal(P_YELLOW));
        ly += 11;
        for (int i = 0; i < n; i++) {
            float iw = Prompt::icon(rows[i].a, lx, ly);
            R::text(T(rows[i].what), lx + std::max(iw, 16.0f) + 4, ly + 5, pal(P_WHITE));
            ly += 16;
        }
        ly += 4;
    };
    float ly = y, ry = y;
    list("PLAYING", PLAY, (int)(sizeof(PLAY) / sizeof(PLAY[0])), x, ly);
    list("DRIVING", DRIVE, 4, x + colW + 10, ry);
    list("MENUS", MENU, 5, x + colW + 10, ry);
    Prompt::forcePad(-1);
    // Aim assist.
    R::text(T("AIM ASSIST"), x + colW + 10, ry, pal(P_YELLOW));
    ry += 11;
    const char* names[3] = {"Off", "Standard", "Strong"};
    float bw = std::floor((colW - 8) / 3);
    for (int i = 0; i < 3; i++) {
        bool on = s_aimAssist == i;
        if (UI::button(x + colW + 10 + i * (bw + 4), ry, bw, 14, T(names[i]), true, on ? P_YELLOW : P_WHITE)) setAimAssist(i);
        if (on) R::rectOutline(x + colW + 9 + i * (bw + 4), ry - 1, bw + 2, 16, pal(P_YELLOW));
    }
    ry += 22;
    // Which controller's buttons are shown.
    std::string plugged = Input::lastPad() >= 0 ? Input::padName(Input::lastPad()) : T("No controller");
    R::text(T1("Controller: {0}", plugged), x + colW + 10, ry, pal(P_LAVENDER));
    ry += 11;
    if (UI::button(x + colW + 10, ry, colW, 14, type == Input::PAD_PLAYSTATION ? T("Show Xbox buttons") : T("Show PlayStation buttons")))
        s_ctrlPreview = type == Input::PAD_PLAYSTATION ? Input::PAD_XBOX : Input::PAD_PLAYSTATION;
}

void drawControls(float W, float H, void (*back)()) {
    int n = 0;
    const Input::Bindable* list = Input::bindables(n);
    const float rowH = 12;
    if (s_ctrlPage < 0) s_ctrlPage = Input::usingPad() ? 1 : 0;
    float w = s_ctrlPage == 1 ? 420 : 300;
    float h = s_ctrlPage == 1 ? 318 : 44 + n * rowH + 5 * 10 + 48;
    h = std::min(h, H - 8);
    float x = std::floor(W / 2 - w / 2), y = std::floor(H / 2 - h / 2);
    UI::panel(x, y, w, h, T("CONTROLS"));

    // Keyboard | Controller tabs (LB / RB on a controller).
    float tw = std::floor((w - 20 - 4) / 2);
    const char* tabs[2] = {"Keyboard & mouse", "Controller"};
    for (int i = 0; i < 2; i++) {
        bool on = s_ctrlPage == i;
        if (UI::button(x + 10 + i * (tw + 4), y + 18, tw, 14, T(tabs[i]), true, on ? P_YELLOW : P_WHITE) && s_rebind < 0) s_ctrlPage = i;
        if (on) R::rectOutline(x + 9 + i * (tw + 4), y + 17, tw + 2, 16, pal(P_YELLOW));
    }
    if (int pg = Input::padPagePressed(); pg && s_rebind < 0) s_ctrlPage = pg > 0 ? 1 : 0;
    float top = y + 38;

    if (s_ctrlPage == 1) {
        drawPadControls(x + 10, top, w - 20);
        if (UI::button(x + w - 100, y + h - 24, 90, 16, T("Back"))) {
            s_ctrlPage = -1;
            s_ctrlPreview = -1;
            if (back) back();
        }
        return;
    }

    // Waiting for a key: the next one pressed takes the action (Esc keeps the old one).
    if (s_rebind >= 0) {
        int k = Input::keyPressedAny();
        if (k == GLFW_KEY_ESCAPE) { s_rebind = -1; Input::setCapture(false); }
        else if (k >= 0) {
            Input::setBinding(s_rebind, k);
            s_rebind = -1;
            Input::setCapture(false);
            save();
            Audio::play(Snd::click, 0.6f, 1.3f);
        }
    }

    R::text(T("Click a key, then press the new one."), x + 10, top, pal(P_BEIGE));
    float ly = top + 14;
    for (int i = 0; i < n; i++) {
        const Input::Bindable& b = list[i];
        R::text(T(b.label), x + 12, ly + 3, pal(P_WHITE));
        bool waiting = s_rebind == b.action;
        std::string label = waiting ? T("Press a key...") : Input::keyName(Input::binding(b.action));
        if (UI::button(x + w - 112, ly, 100, rowH - 1, label, true, waiting ? P_YELLOW : P_WHITE) && s_rebind < 0) {
            s_rebind = b.action;
            Input::setCapture(true);
        }
        ly += rowH;
    }
    // What cannot be moved.
    ly += 4;
    const char* fixed[][2] = {{"MOUSE", "Aim"}, {"LEFT CLICK", "Shoot"}, {"WHEEL", "Switch weapon / choose"},
                              {"ESC", "Pause / close"}, {"F11", "Fullscreen"}};
    for (auto& f : fixed) {
        R::text(T(f[1]), x + 12, ly, pal(P_LAVENDER));
        R::text(f[0], x + w - 12 - R::textWidth(f[0]), ly, pal(P_PURPLE));
        ly += 10;
    }
    if (UI::button(x + 10, y + h - 24, 120, 16, T("Reset to defaults"))) {
        Input::resetBindings();
        s_rebind = -1;
        Input::setCapture(false);
        save();
    }
    if (UI::button(x + w - 100, y + h - 24, 90, 16, T("Back"))) {
        s_rebind = -1;
        s_ctrlPage = -1;
        Input::setCapture(false);
        if (back) back();
    }
}

}  // namespace Options
