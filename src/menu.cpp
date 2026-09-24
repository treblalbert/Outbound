// Main menu, language picker, save slots, how-to-play, credits and controls.
#include "audio.h"
#include "game.h"
#include "coop.h"
#include "assets.h"
#include "art.h"
#include "net.h"
#include "input.h"
#include "lang.h"
#include "local.h"
#include "prompt.h"
#include "options.h"
#include "sprites.h"
#include "ui.h"
#include "voice.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <ctime>

using namespace Sprites;

namespace {
float s_menuClock = 9 * 60;
float s_creditsScroll = 0;
bool s_langPicker = false;     // language panel open over the main menu
bool s_introThenPlay = false;  // the intro was opened by starting a new game
int s_confirmSlot = -1;        // slot waiting on a confirmation
bool s_confirmDelete = false;  // that confirmation is a delete, not an overwrite
int s_setupSlot = -1;          // slot being set up for a new game (difficulty / mode)
int s_setupDiff = DIFF_NORMAL, s_setupMode = MODE_NORMAL, s_setupRivals = 0;
std::string s_charName;        // the character being made (new game) or edited (lobby)
int s_charShirt = 0;
bool s_charFocus = false;      // the name box has the keyboard
bool s_charEdit = false;       // lobby: the character editor is open
bool s_coopPick = false;       // the slot list is picking a save to host co-op with
bool s_localPick = false;      // the slot list is picking a save for the local lobby's players (0.12v)

std::vector<std::string> wrap(const std::string& line, float maxW) {
    std::vector<std::string> out;
    if (R::textWidth(line) <= maxW) { out.push_back(line); return out; }
    std::string cur;
    for (char ch : line) {
        std::string next = cur + ch;
        if (R::textWidth(next) > maxW && !cur.empty()) {
            size_t sp = cur.find_last_of(' ');
            if (sp != std::string::npos && sp > cur.size() / 2) {
                out.push_back(cur.substr(0, sp));
                cur = cur.substr(sp + 1) + ch;
            } else {
                out.push_back(cur);
                cur = std::string(1, ch);
            }
        } else {
            cur = next;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// ---------------------------------------------------------------- flags
// Drawn from rectangles rather than art, so they stay crisp at any scale and do
// not depend on the sprite pack being present.
void drawFlagSpain(float x, float y, float w, float h) {
    Color red(0.67f, 0.11f, 0.16f), yellow(1.0f, 0.77f, 0.0f);
    float band = std::floor(h * 0.25f);
    R::rect(x, y, w, band, red);
    R::rect(x, y + band, w, h - band * 2, yellow);
    R::rect(x, y + h - band, w, band, red);
}

void drawFlagUK(float x, float y, float w, float h) {
    Color blue(0.05f, 0.16f, 0.45f), white(1, 1, 1), red(0.78f, 0.06f, 0.18f);
    R::rect(x, y, w, h, blue);
    Vec2 tl(x, y), tr(x + w, y), bl(x, y + h), br(x + w, y + h);
    R::line(tl, br, 3.0f, white);
    R::line(tr, bl, 3.0f, white);
    R::line(tl, br, 1.2f, red);
    R::line(tr, bl, 1.2f, red);
    float cx = std::floor(x + w / 2), cy = std::floor(y + h / 2);
    R::rect(cx - 2.5f, y, 5, h, white);
    R::rect(x, cy - 2.5f, w, 5, white);
    R::rect(cx - 1.5f, y, 3, h, red);
    R::rect(x, cy - 1.5f, w, 3, red);
}

void drawFlag(int lang, float x, float y, float w, float h) {
    if (lang == LANG_ES) drawFlagSpain(x, y, w, h);
    else drawFlagUK(x, y, w, h);
    R::rectOutline(x, y, w, h, pal(P_DARK));
}

// A language row: flag, native name, and a highlight when it is the active one.
bool languageRow(float x, float y, float w, int lang) {
    const float h = 20;
    bool active = L::get() == lang;
    UI::focusable(x, y, w, h);
    bool hov = UI::hover(x, y, w, h);
    R::rect(x, y, w, h, pal(active ? P_LAVENDER : (hov ? P_PURPLE : P_DARK)));
    R::rectOutline(x, y, w, h, pal(active ? P_YELLOW : P_PURPLE));
    drawFlag(lang, x + 5, y + 4, 18, 12);
    R::text(L::nativeName(lang), x + 30, y + 7, pal(active ? P_DARK : P_WHITE));
    if (hov && (Input::mousePressed(0) || (Input::gamepad() && Input::pressed(GLFW_KEY_E)))) {
        Input::consumeMouse();
        Audio::play(Snd::click, 0.5f);
        return true;
    }
    return false;
}

void drawBackground() {
    Vec2 cam = G.cam;
    sceneBegin();
    sceneSetWorld(&G.menuWorld);
    setSunForTime(s_menuClock);
    R::begin(R::WORLD, cam);
    drawWorldTiles(G.menuWorld, cam, G.realTime);
    drawTileSolids(G.menuWorld, cam, G.realTime);
    sceneFlush();
    drawRoofs(G.menuWorld, cam, Vec2(-1e6f, -1e6f), G.frameDt);
    R::end();
    R::begin(R::GLOW, cam);
    R::end();
    G.lighting.lights.clear();
    G.lighting.ambient = ambientColor(s_menuClock);
    G.lighting.dither = 0.1f;
    G.lighting.cone = false;
    G.lighting.grade = Color(1, 1, 1);
    G.lighting.lift = Color(0, 0, 0);
    G.lighting.fog = 0;
    if (ambientBrightness(s_menuClock) < 0.7f) {
        for (int i = 0; i < 6; i++) {
            Vec2 p = cam + Vec2(R::width() * (0.1f + 0.17f * i), R::height() * (0.3f + 0.4f * ((i * 7) % 3) / 2.0f));
            G.lighting.lights.push_back({p, 90, 0.7f, pal(i % 2 ? P_ORANGE : P_YELLOW)});
        }
    }
    G.drawCam = cam;
}

// ---------------------------------------------------------------- screens
void drawLanguagePanel(float W, float H, bool firstRun) {
    float w = 260, h = firstRun ? 118 : 100;
    float x = std::floor(W / 2 - w / 2), y = std::floor(H / 2 - h / 2);
    UI::panel(x, y, w, h, T("LANGUAGE"));
    R::text(T("Choose your language"), x + 10, y + 20, pal(P_WHITE));
    for (int i = 0; i < LANG_COUNT; i++) {
        if (languageRow(x + 10, y + 34 + i * 24, w - 20, i)) {
            L::set(i);
            L::savePref();
        }
    }
    if (firstRun) {
        R::text(T("Change this any time from the menu."), x + 10, y + h - 34, pal(P_LAVENDER));
    }
    if (UI::button(x + w / 2 - 40, y + h - 22, 80, 16, T("Continue"))) {
        L::savePref();
        s_langPicker = false;
    }
}

void drawIntro(float W, float H) {
    struct Section { const char* title; const char* body; };
    static const Section SECTIONS[] = {
        {"THE BUNKER IS HOME",
         "Sleep to heal and save. Stash what you want to keep, sell the rest at the trader, and spend the money on upgrades at the workbench."},
        {"GO OUT AND LOOT",
         "Each day has its own world. Slip back into the bunker and head out again the same day and you return to the same place, with what you explored, killed and looted still done."},
        {"BE HOME BY 22:00",
         "The hatch seals at 22:00. After that the night horde spawns without end and hunts you down. Dying costs you half of what you carry (all of it on Hardcore)."},
        {"EACH DAY IS WORSE",
         "Sleeping rolls a fresh world and raises the stakes: they grow tougher, more numerous and hit harder, though what they guard is worth more. There is no prize for sleeping early."},
        {"THE WORLD BREAKS",
         "Bullets chew through walls and trees; explosions clear everything in their radius. If a door is a problem, make a new one."},
    };
    const int n = (int)(sizeof(SECTIONS) / sizeof(SECTIONS[0]));
    float w = std::min(W - 32, 440.0f);
    float textW = w - 24;

    // Measure first so the panel always fits its wrapped contents.
    std::vector<std::vector<std::string>> bodies;
    float h = 26;
    for (int i = 0; i < n; i++) {
        bodies.push_back(wrap(T(SECTIONS[i].body), textW));
        h += 11 + bodies[i].size() * 9 + 6;
    }
    h += 26;
    float x = std::floor(W / 2 - w / 2), y = std::floor(H / 2 - h / 2);
    if (y < 4) y = 4;

    UI::panel(x, y, w, h, T("HOW TO PLAY"));
    float ty = y + 20;
    for (int i = 0; i < n; i++) {
        R::text(T(SECTIONS[i].title), x + 12, ty, pal(P_YELLOW));
        ty += 11;
        for (const std::string& line : bodies[i]) {
            R::text(line, x + 12, ty, pal(P_BEIGE));
            ty += 9;
        }
        ty += 6;
    }
    if (UI::button(x + w / 2 - 45, y + h - 22, 90, 16, s_introThenPlay ? T("Start") : T("Back"))) {
        if (s_introThenPlay) {
            s_introThenPlay = false;
            base_enter(false);
        } else {
            G.scene = Scene::Menu;
        }
    }
}

float drawCharacterEditor(float x, float y, float w);

// Co-op: how to start or join a game, then the lobby itself.
void drawLobby(float W, float H) {
    if (!Coop::active()) {
        float w = 330, h = 150, x = std::floor(W / 2 - w / 2), y = std::floor(H / 2 - h / 2);
        UI::panel(x, y, w, h, T("CO-OP"));
        float ty = y + 22;
        std::string why = Coop::leaveReason();
        if (!why.empty()) ty += UI::textWrap(why, x + 10, ty, w - 20, pal(P_CORAL)) + 4;
        bool steam = Net::steamAvailable();
        if (!steam) {
            ty += UI::textWrap(T("Steam is not running. Start Steam to play co-op."), x + 10, ty, w - 20, pal(P_CORAL)) + 4;
        }
        ty += UI::textWrap(T("Up to eight players. The host's save keeps the world and every player's character."), x + 10, ty, w - 20, pal(P_WHITE)) + 8;
        UI::textWrap(T("To join a friend, accept their Steam invite, or right-click them in your friends list and choose Join Game."), x + 10, ty, w - 20, pal(P_BEIGE));
        if (UI::button(x + 10, y + h - 24, 130, 16, T("Host a game"), steam)) {
            s_coopPick = true;
            G.scene = Scene::Slots;
        }
        if (UI::button(x + w - 90, y + h - 24, 80, 16, T("Back"))) G.scene = Scene::Menu;
        return;
    }
    float w = 300, h = 80 + Coop::MAX_PLAYERS * 18, x = std::floor(W / 2 - w / 2), y = std::floor(H / 2 - h / 2);
    UI::panel(x, y, w, h, T("CO-OP LOBBY"));
    Net::LobbyState ls = Net::lobbyState();
    std::string status = ls == Net::LobbyState::Creating ? T("Creating the lobby...")
                       : ls == Net::LobbyState::Joining ? T("Joining...")
                       : Coop::guest() && !Coop::welcomed() ? T("Connecting to the host...")
                       : Coop::host() ? T("Invite your friends, then start.") : T("Waiting for the host to start...");
    R::text(status, x + 10, y + 20, pal(P_BEIGE));
    for (int i = 0; i < Coop::MAX_PLAYERS; i++) {
        const Coop::NetPlayer& np = Coop::player(i);
        float ry = y + 36 + i * 18;
        R::rect(x + 8, ry, w - 16, 16, pal(i % 2 ? P_DARK : P_PURPLE, i % 2 ? 1.0f : 0.35f));
        R::rect(x + 12, ry + 4, 8, 8, pal(Coop::colorPal(i), np.used ? 1.0f : 0.3f));
        if (np.used) {
            std::string nm = np.name.empty() ? Net::nameOf(np.id) : np.name;
            R::text(nm + (i == 0 ? "  " + T("(host)") : "") + (i == Coop::localSlot() ? "  " + T("(you)") : ""), x + 26, ry + 5, pal(Coop::colorPal(i)));
            // Voice in the lobby (0.11v): who is talking right now.
            bool talk = i == Coop::localSlot() ? Voice::transmitting() : Voice::talking(i);
            if (talk) {
                float wx = x + w - 22;
                for (int k = 0; k < 3; k++) {
                    float hgt = 2 + 3 * std::fabs(std::sin(G.realTime * 9.0f + k * 1.3f));
                    R::rect(wx + k * 3, ry + 8 - hgt * 0.5f, 2, hgt, pal(P_YGREEN));
                }
            }
        } else {
            R::text(T("- open -"), x + 26, ry + 5, pal(P_PURPLE));
        }
    }
    float by = y + h - 24;
    if (s_charEdit) {
        float cw = 300, ch = 110, cx = std::floor(W / 2 - cw / 2), cy = std::floor(H / 2 - ch / 2);
        UI::panel(cx, cy, cw, ch, T("YOUR CHARACTER"));
        drawCharacterEditor(cx + 10, cy + 20, cw - 20);
        if (UI::button(cx + 10, cy + ch - 22, 90, 16, T("Save"), true, P_YGREEN)) {
            Coop::setIdentity(s_charName, s_charShirt);
            s_charEdit = s_charFocus = false;
            Input::setCapture(false);
        }
        if (UI::button(cx + cw - 100, cy + ch - 22, 90, 16, T("Cancel"))) { s_charEdit = s_charFocus = false; Input::setCapture(false); }
        return;
    }
    if (UI::button(x + 10, by, 90, 16, T("Invite"), Net::steamAvailable() && !Net::devMode() && ls == Net::LobbyState::In))
        Net::openInviteOverlay();
    if (UI::button(x + 10, by - 20, 90, 16, T("Character"))) {
        s_charEdit = true;
        s_charName = Coop::localName() == Net::myName() ? "" : Coop::localName();
        s_charShirt = Coop::localShirt();
    }
    if (Coop::host() && UI::button(x + w / 2 - 40, by, 80, 16, T("Start"), ls == Net::LobbyState::In, P_YGREEN))
        Coop::startGame();
    if (UI::button(x + w - 90, by, 80, 16, T("Leave"), true, P_CORAL)) Coop::leave("");
    Voice::drawHud(x + 10, y + h + 6);
}

// Name and shirt, with the character standing there wearing it. Returns the height.
float drawCharacterEditor(float x, float y, float w) {
    R::text(T("CHARACTER"), x, y, pal(P_YELLOW));
    float ly = y + 12;
    R::text(T("Name"), x, ly + 4, pal(P_LAVENDER));
    UI::textField(x + 34, ly, 150, 15, s_charName, s_charFocus, 16);
    if (s_charName.empty() && !s_charFocus) R::text(T("(your Steam name)"), x + 38, ly + 4, pal(P_PURPLE));
    ly += 19;
    R::text(T("Shirt"), x, ly + 3, pal(P_LAVENDER));
    for (int i = 0; i < Assets::SHIRT_COUNT; i++) {
        const Assets::ShirtDef& sd = Assets::shirt(i);
        float sx = x + 34 + i * 15;
        bool on = s_charShirt == i;
        UI::focusable(sx, ly, 13, 13);
        bool hov = UI::hover(sx, ly, 13, 13);
        R::rect(sx, ly, 13, 13, pal(on ? P_YELLOW : hov ? P_WHITE : P_DARK));
        R::rect(sx + 2, ly + 2, 9, 9, Color(sd.r / 255.0f, sd.g / 255.0f, sd.b / 255.0f));
        R::rectOutline(sx + 2, ly + 2, 9, 9, pal(P_LAVENDER, 0.5f));
        if (hov) UI::tooltip(T(sd.name), "");
        if (hov && Input::mousePressed(0)) { Input::consumeMouse(); s_charShirt = i; Audio::play(Snd::click, 0.5f); }
    }
    R::text(T(Assets::shirt(s_charShirt).name), x + 34, ly + 16, pal(Assets::shirt(s_charShirt).uiPal));
    // The character, facing you, in the chosen shirt.
    Art::Piece body = Art::humanBody(Art::Dir::Down, Art::Anim::Idle, (int)(G.realTime * 6.0f), false, false, s_charShirt);
    if (body.valid()) R::spriteAt(*body.sprite, body.frame, {x + w - 26, ly + 20}, R::Pivot::Bottom, 2);
    UI::textWrap(T("Your name is what the other players see in co-op."), x, ly + 28, w - 60, pal(P_BEIGE));
    return 12 + 19 + 40;
}

// ---- the local co-op lobby (0.12v) ----------------------------------------------------
// Four places. A controller joins with A or START and leaves with B; the keyboard and
// mouse join with ENTER and leave with BACKSPACE; left and right pick a shirt. Each
// place shows its own device's buttons. Player 1 starts, then picks the save.
// A button's picture from the input prompts; returns its width (0 when missing).
static float glyph(const char* key, float x, float y) {
    const Assets::Sprite* s = Assets::find(std::string("input/") + key);
    if (!s) return 0;
    R::frame(s->frame(0), std::floor(x), std::floor(y), (float)s->w, (float)s->h);
    return (float)s->w;
}
static void centeredLines(const std::string& text, float cx, float y, float maxW, int col) {
    for (const std::string& l : wrap(text, maxW)) { R::textCentered(l, cx, y, pal(col), 1, false); y += 10; }
}

void drawLocalLobby(float W, float H) {
    float w = std::min(W - 12, 580.0f), h = 240, x = std::floor(W / 2 - w / 2), y = std::floor(H / 2 - h / 2);
    UI::panel(x, y, w, h, T("LOCAL CO-OP"));
    centeredLines(T("Up to four players on one screen: controllers, or the keyboard and mouse."), W / 2, y + 18, w - 20, P_BEIGE);
    float cw = std::floor((w - 20 - 3 * 6) / 4), ch = 156, cy = y + 40;
    int lastPad = std::max(0, Input::lastPad());
    for (int k = 0; k < Local::MAX_SEATS; k++) {
        const Local::LobbySeat& st = Local::lobbySeat(k);
        float cx = x + 10 + k * (cw + 6);
        int col = Coop::colorPal(k);
        R::rect(cx, cy, cw, ch, pal(P_DARK, st.used ? 0.9f : 0.55f));
        R::rectOutline(cx, cy, cw, ch, pal(st.used ? col : P_PURPLE));
        R::textCentered(T1("PLAYER {0}", std::to_string(k + 1)), cx + cw / 2, cy + 4, pal(st.used ? col : P_LAVENDER), 1, false);
        if (!st.used) {
            // How to join, in both kinds of buttons.
            bool pads = false;
            for (int j = 0; j < Input::MAX_PADS; j++) pads = pads || Input::padConnected(j);
            float ly = cy + 50;
            if (pads) {
                bool ps = Input::padTypeOf(lastPad) == Input::PAD_PLAYSTATION;
                glyph(ps ? "ps_cross" : "pad_a", cx + cw / 2 - 17, ly);
                glyph(ps ? "ps_options" : "pad_menu", cx + cw / 2 + 1, ly);
                centeredLines(T("Controller: press to join"), cx + cw / 2, ly + 19, cw - 8, P_WHITE);
                ly += 44;
            }
            bool kbFree = true;
            for (int j = 0; j < Local::MAX_SEATS; j++)
                if (Local::lobbySeat(j).used && Local::lobbySeat(j).device == Input::DEV_KBM) kbFree = false;
            if (kbFree) {
                const Assets::Sprite* ek = Assets::find("input/kb_enter");
                glyph("kb_enter", cx + cw / 2 - (ek ? ek->w / 2.0f : 8), ly);
                centeredLines(T("Keyboard: press to join"), cx + cw / 2, ly + 19, cw - 8, P_WHITE);
            }
            if (!pads && !kbFree) centeredLines(T("Plug in a controller to join"), cx + cw / 2, cy + 70, cw - 8, P_LAVENDER);
            continue;
        }
        // The character, turning slowly, in the chosen shirt.
        int dirs[4] = {(int)Art::Dir::Down, (int)Art::Dir::Right, (int)Art::Dir::Up, (int)Art::Dir::Left};
        Art::Dir d = (Art::Dir)dirs[(int)(G.realTime * 0.6f + k) % 4];
        Art::Piece body = Art::humanBody(d, Art::Anim::Idle, (int)(G.realTime * 6.0f), false, false, st.shirt);
        if (body.valid()) R::spriteAt(*body.sprite, body.frame, {cx + cw / 2, cy + 64}, R::Pivot::Bottom, 2);
        const Assets::ShirtDef& sd = Assets::shirt(st.shirt);
        R::textCentered(k == 0 ? T("Your save") : T(sd.name), cx + cw / 2, cy + 70, pal(k == 0 ? P_BEIGE : sd.uiPal), 1, false);
        bool kb = st.device == Input::DEV_KBM;
        std::string dev = kb ? T("Keyboard") : Input::padName(st.device);
        while (dev.size() > 1 && R::textWidth(dev) > cw - 6) dev.pop_back();
        R::textCentered(dev, cx + cw / 2, cy + 82, pal(P_LAVENDER), 1, false);
        // This player's own buttons.
        float ly = cy + 98;
        bool ps = !kb && Input::padTypeOf(st.device) == Input::PAD_PLAYSTATION;
        if (k != 0) {
            float gw = kb ? glyph("kb_left", cx + 6, ly) : glyph("pad_dpad_lr", cx + 6, ly);
            if (kb) gw += glyph("kb_right", cx + 6 + gw, ly);
            R::text(T("Shirt"), cx + 9 + gw, ly + 5, pal(P_WHITE));
            ly += 19;
        }
        float gw = 0;
        if (kb) { R::text("BKSP", cx + 6, ly + 5, pal(P_YELLOW)); gw = R::textWidth("BKSP"); }
        else gw = glyph(ps ? "ps_circle" : "pad_b", cx + 6, ly);
        R::text(T("Leave"), cx + 9 + gw, ly + 5, pal(P_WHITE));
        ly += 19;
        if (k == 0) {
            gw = kb ? glyph("kb_enter", cx + 6, ly) : glyph(ps ? "ps_options" : "pad_menu", cx + 6, ly);
            R::text(T("Start"), cx + 9 + gw, ly + 5, pal(Local::lobbyReady() ? P_YGREEN : P_PURPLE));
            if (!Local::lobbyReady()) centeredLines(T("Needs a second player"), cx + cw / 2, ly + 20, cw - 8, P_PURPLE);
        }
    }
    // Mouse buttons (not controller-focusable: a controller's A and B belong to its seat here).
    auto mouseButton = [&](float bx, float by, float bw, const std::string& label, bool on, int c) {
        bool hov = on && UI::hover(bx, by, bw, 16);
        R::rect(bx, by, bw, 16, pal(hov ? P_PURPLE : P_DARK, on ? 0.95f : 0.5f));
        R::rectOutline(bx, by, bw, 16, pal(on ? c : P_PURPLE));
        R::textCentered(label, bx + bw / 2, by + 5, pal(on ? c : P_PURPLE), 1, false);
        if (hov && Input::mousePressed(0)) { Input::consumeMouse(); Audio::play(Snd::click, 0.6f); return true; }
        return false;
    };
    float by = y + h - 24;
    if (mouseButton(x + 10, by, 90, T("Back"), true, P_WHITE)) { Local::lobbyClose(); G.scene = Scene::Menu; return; }
    bool kbJoined = false;
    for (int j = 0; j < Local::MAX_SEATS; j++)
        if (Local::lobbySeat(j).used && Local::lobbySeat(j).device == Input::DEV_KBM) kbJoined = true;
    if (!kbJoined && mouseButton(x + w / 2 - 70, by, 140, T("Join with keyboard"), Local::lobbyCount() < Local::MAX_SEATS, P_YELLOW)) {
        Local::lobbyJoinKeyboard();   // the same as ENTER
    }
    if (mouseButton(x + w - 100, by, 90, T("Start"), Local::lobbyReady() && Local::lobbySeat(0).device == Input::DEV_KBM, P_YGREEN)) {
        Local::lobbyCommit();
        s_localPick = true;
        G.scene = Scene::Slots;
    }
}

// A new game: how hard, and who is out there. Chosen once, kept by the save.
void drawNewGameSetup(float W, float H) {
    float w = 360, h = s_coopPick ? 340 : 286, x = std::floor(W / 2 - w / 2), y = std::floor(H / 2 - h / 2);
    UI::panel(x, y, w, h, T("NEW GAME"));
    float ly = y + 22;
    ly += drawCharacterEditor(x + 10, ly, w - 20) + 8;
    auto choice = [&](const char* title, int& value, const char* const names[2], const char* const descs[2], const int cols[2]) {
        R::text(T(title), x + 10, ly, pal(P_YELLOW));
        ly += 12;
        float bw = std::floor((w - 24) / 2);
        for (int i = 0; i < 2; i++) {
            bool on = value == i;
            if (UI::button(x + 10 + i * (bw + 4), ly, bw, 16, T(names[i]), true, on ? cols[i] : P_WHITE)) value = i;
            if (on) R::rectOutline(x + 9 + i * (bw + 4), ly - 1, bw + 2, 18, pal(cols[i]));
        }
        ly += 21;
        float th = UI::textWrap(T(descs[value]), x + 12, ly, w - 24, pal(P_BEIGE));
        ly += std::max(th, 30.0f) + 8;
    };
    static const char* const DIFF_NAMES[2] = {"Normal", "Hardcore"};
    static const char* const DIFF_DESC[2] = {
        "Dying costs a random half of what you carry. The minimap, the way home and your friends are always shown.",
        "Dying costs everything you carry. Enemies hit harder and take more to kill, hordes are bigger and you start with less. No minimap, no home marker, no friend markers: buy them back at the trader."};
    static const int DIFF_COLS[2] = {P_YGREEN, P_CORAL};
    static const char* const MODE_NAMES[2] = {"Normal", "Zombies"};
    static const char* const MODE_DESC[2] = {
        "Raiders roam the zone in small groups. Zombie hordes come for the bunker.",
        "No raiders: the zone belongs to the dead. Far more of them, roaming in packs, drawn by noise. Hordes still come for the bunker."};
    static const int MODE_COLS[2] = {P_YGREEN, P_ORANGE};
    choice("DIFFICULTY", s_setupDiff, DIFF_NAMES, DIFF_DESC, DIFF_COLS);
    choice("MODE", s_setupMode, MODE_NAMES, MODE_DESC, MODE_COLS);
    if (s_coopPick) {
        static const char* const COOP_NAMES[2] = {"Together", "Rivals"};
        static const char* const COOP_DESC[2] = {
            "One bunker for everyone, a shared stash, and the hordes fought side by side.",
            "Every player has their own bunker somewhere in the world. Anyone can shoot anyone and nothing shows where the others are: team up, or take their loot. Every kill is announced. No hordes."};
        static const int COOP_COLS[2] = {P_YGREEN, P_CORAL};
        choice("CO-OP", s_setupRivals, COOP_NAMES, COOP_DESC, COOP_COLS);
    }
    if (UI::button(x + 10, y + h - 24, 110, 16, T("Start"), true, P_YGREEN)) {
        int slot = s_setupSlot;
        s_setupSlot = -1;
        G.saveSlot = slot;
        s_charFocus = false;
        Input::setCapture(false);
        new_game(s_setupDiff, s_setupMode, s_charName, s_charShirt, s_coopPick && s_setupRivals == 1);
        Coop::setIdentity(s_charName, s_charShirt);   // and who you are when you join a friend
        if (s_coopPick) { s_coopPick = false; Coop::beginHost(); return; }
        s_localPick = false;
        s_introThenPlay = true;
        G.scene = Scene::Intro;
        return;
    }
    if (UI::button(x + w - 100, y + h - 24, 90, 16, T("Back"))) { s_setupSlot = -1; s_charFocus = false; Input::setCapture(false); }
}

void drawSlots(float W, float H) {
    float w = 330, h = 168;
    float x = std::floor(W / 2 - w / 2), y = std::floor(H / 2 - h / 2);
    UI::panel(x, y, w, h, s_coopPick ? T("HOST CO-OP: PICK A SAVE") : s_localPick ? T("LOCAL CO-OP: PICK A SAVE") : T("SELECT A SLOT"));
    const float btnX = x + w - 170, btnW = 96;      // stacked Continue / New Game
    const float delX = x + w - 68, delW = 58;       // Delete, to their right

    for (int i = 0; i < SAVE_SLOTS; i++) {
        float ry = y + 20 + i * 38;
        SaveInfo info = save_info(i);
        R::rect(x + 6, ry, w - 12, 34, pal(i % 2 ? P_DARK : P_PURPLE, i % 2 ? 1.0f : 0.3f));
        R::rectOutline(x + 6, ry, w - 12, 34, pal(P_PURPLE));
        R::text(T1("Slot {0}", std::to_string(i + 1)), x + 12, ry + 4, pal(P_YELLOW));
        if (info.exists && (info.difficulty != DIFF_NORMAL || info.gameMode != MODE_NORMAL || info.rivals)) {
            std::string tag;
            if (info.difficulty == DIFF_HARDCORE) tag = T("Hardcore");
            if (info.gameMode == MODE_ZOMBIES) tag += (tag.empty() ? "" : " - ") + T("Zombies");
            if (info.rivals) tag += (tag.empty() ? "" : " - ") + T("Rivals");
            R::text(tag, x + 60, ry + 4, pal(info.difficulty == DIFF_HARDCORE ? P_CORAL : P_ORANGE));
        }
        if (info.exists) {
            R::text(T1("Day {0}", std::to_string(info.day)) + "   $" + std::to_string(info.money), x + 12, ry + 15, pal(P_WHITE));
            if (info.inRaid)
                R::text(T1("Outside, {0} (resume)", fmtTime(info.timeMin)), x + 12, ry + 24, pal(P_ORANGE));
            else
                R::text(T2("{0} raids, {1} kills", std::to_string(info.raids), std::to_string(info.kills)), x + 12, ry + 24, pal(P_LAVENDER));
            if (UI::button(btnX, ry + 4, btnW, 13, s_coopPick ? T("Host") : T("Continue"))) {
                if (load_game(i)) {
                    G.scene = Scene::Menu;
                    s_localPick = false;
                    if (s_coopPick) {
                        // Hosting always starts in the bunker; a raid left open in this
                        // save is simply over (what you carried comes home).
                        s_coopPick = false;
                        G.prof.inRaid = false;
                        Coop::beginHost();
                    } else if (G.prof.inRaid) raid_resume();
                    else base_enter(false);
                    return;
                }
            }
            if (UI::button(btnX, ry + 18, btnW, 13, T("New Game"))) {
                s_confirmSlot = i;
                s_confirmDelete = false;
            }
            if (UI::button(delX, ry + 10, delW, 14, T("Delete"), true, P_CORAL)) {
                s_confirmSlot = i;
                s_confirmDelete = true;
            }
        } else {
            R::text(T("Empty slot"), x + 12, ry + 15, pal(P_PURPLE));
            if (UI::button(btnX, ry + 9, btnW, 16, T("New Game"))) {
                s_setupSlot = i;
                s_charName = Coop::localName() == Net::myName() ? "" : Coop::localName();
                s_charShirt = Coop::localShirt();
                return;
            }
        }
    }
    if (s_setupSlot >= 0) { drawNewGameSetup(W, H); return; }
    if (UI::button(x + w / 2 - 40, y + h - 22, 80, 16, T("Back"))) {
        G.scene = s_coopPick ? Scene::Lobby : Scene::Menu;
        if (s_localPick) { Local::cancelPlan(); Local::lobbyOpen(); G.scene = Scene::LocalLobby; }
        s_coopPick = s_localPick = false;
    }

    if (s_confirmSlot >= 0) {
        float cw = 285, ch = 82;
        float cx = std::floor(W / 2 - cw / 2), cy = std::floor(H / 2 - ch / 2);
        UI::panel(cx, cy, cw, ch, s_confirmDelete ? T("DELETE THIS SAVE?") : T("START OVER?"));
        R::text(s_confirmDelete ? T1("Slot {0} will be erased for good.", std::to_string(s_confirmSlot + 1))
                                : T("This will erase the save in this slot."),
                cx + 10, cy + 24, pal(P_BEIGE));
        if (UI::button(cx + 10, cy + ch - 24, 118, 16, s_confirmDelete ? T("Erase") : T("Erase & start"), true, P_CORAL)) {
            int slot = s_confirmSlot;
            s_confirmSlot = -1;
            if (s_confirmDelete) {
                delete_save(slot);
                        } else {
                s_setupSlot = slot;
                s_charName = Coop::localName() == Net::myName() ? "" : Coop::localName();
                s_charShirt = Coop::localShirt();
            }
            return;
        }
        if (UI::button(cx + cw - 85, cy + ch - 24, 75, 16, T("Cancel"))) s_confirmSlot = -1;
    }
}

}  // namespace

// The controls screen (menu and pause menus): the rebindable keys, see Options.
void drawControlsPanel(float x, float y) {
    (void)x; (void)y;
    Options::drawControls((float)R::width(), (float)R::height(), [] {
        if (G.scene == Scene::Controls) G.scene = Scene::Menu;
        else G.panel = Panel::Pause;
    });
}

// Dev helper: jump straight to one of the menu screens for a screenshot.
void menu_devScreen(const std::string& name) {
    if (name == "lang") { G.scene = Scene::Menu; s_langPicker = true; }
    else if (name == "slots") G.scene = Scene::Slots;
    else if (name == "setup") { G.scene = Scene::Slots; s_setupSlot = 0; s_setupDiff = DIFF_HARDCORE; s_setupMode = MODE_ZOMBIES; s_charShirt = 4; s_charName = "Albert"; }
    else if (name == "intro") { s_introThenPlay = false; G.scene = Scene::Intro; }
    else if (name == "credits") G.scene = Scene::Credits;
    else if (name == "controls") G.scene = Scene::Controls;
    else if (name == "coop") G.scene = Scene::Lobby;
    else if (name == "local") { Local::lobbyOpen(); Local::lobbyJoinKeyboard(); G.scene = Scene::LocalLobby; }
}

void menu_init() {
    G.scene = Scene::Menu;
    G.panel = Panel::None;
    World::withCrypts = World::zombieSpawns = false;
    G.menuWorld.generate((uint64_t)time(nullptr) * 2654435761ull, 1);
    G.cam = G.menuWorld.homePos - Vec2(R::width() / 2.0f, R::height() / 2.0f) + Vec2(-300, -200);
    s_menuClock = 8 * 60;
    s_confirmSlot = -1;
    s_introThenPlay = false;
    s_langPicker = !L::chosen();
}

// ---- start-up card: "Made with the Freeman engine", fading in and out over the menu.
namespace { float s_splashT = -1; }
constexpr float SPLASH_IN = 0.7f, SPLASH_HOLD = 1.9f, SPLASH_OUT = 0.7f;

void menu_splash() {
    G.scene = Scene::Splash;
    s_splashT = 0;
}

static void splashUpdate(float dt) {
    s_splashT += dt;
    // Any key, click or button skips straight to the fade-out.
    bool skip = Input::mousePressed(0) || Input::pressed(GLFW_KEY_E) || Input::pressed(GLFW_KEY_ESCAPE) ||
                Input::keyPressed(GLFW_KEY_SPACE) || Input::keyPressed(GLFW_KEY_ENTER);
    if (skip && s_splashT < SPLASH_IN + SPLASH_HOLD) s_splashT = SPLASH_IN + SPLASH_HOLD;
    if (s_splashT >= SPLASH_IN + SPLASH_HOLD + SPLASH_OUT) { s_splashT = -1; G.scene = Scene::Menu; }
}

static void splashDraw(float W, float H) {
    float t = s_splashT;
    float a = t < SPLASH_IN ? t / SPLASH_IN : t < SPLASH_IN + SPLASH_HOLD ? 1.0f : 1.0f - (t - SPLASH_IN - SPLASH_HOLD) / SPLASH_OUT;
    a = clampf(a, 0, 1);
    // The card stays black the whole time; only its words fade, then the black lifts
    // off the menu underneath.
    float black = t < SPLASH_IN + SPLASH_HOLD ? 1.0f : a;
    R::rect(0, 0, W, H, Color(0.04f, 0.04f, 0.06f, black));
    float cy = std::floor(H * 0.42f);
    R::textCentered(T("Made with the"), W / 2, cy - 22, pal(P_LAVENDER, a), 1);
    R::textCentered("FREEMAN ENGINE", W / 2 + 2, cy + 2, pal(P_PURPLE, a), 3, false);
    R::textCentered("FREEMAN ENGINE", W / 2, cy, pal(P_YELLOW, a), 3, false);
    float lw = 150;
    R::rect(std::floor(W / 2 - lw / 2), cy + 28, lw, 1, pal(P_PURPLE, a));
    R::textCentered(T("an engine by Albert Freeman"), W / 2, cy + 36, pal(P_BEIGE, a), 1);
}

// Which of the menu's own dialogs is up, so the controller starts each on its first button.
int menu_navContext() { return (s_langPicker ? 1 : 0) + (s_confirmSlot >= 0 ? 2 : 0) + (s_coopPick ? 4 : 0); }

void menu_update(float dt) {
    if (G.scene == Scene::Splash) { splashUpdate(dt); return; }
    s_menuClock = std::fmod(s_menuClock + dt * 10.0f, 24 * 60.0f);
    Audio::setAmbient(Audio::AMB_BIRDS, 0.35f);   // the title screen's quiet morning (0.11v)
    G.cam += Vec2(9, 4) * dt;
    float maxX = G.menuWorld.w * TILE - R::width() - 64.0f, maxY = G.menuWorld.h * TILE - R::height() - 64.0f;
    if (G.cam.x > maxX || G.cam.y > maxY) G.cam = Vec2(64, 64);
    if (G.scene == Scene::LocalLobby) {
        // The lobby reads every device itself: a controller's B leaves its place rather
        // than backing out of the screen, so only the keyboard's Escape does that.
        if (Local::lobbyUpdate()) {
            Local::lobbyCommit();
            s_localPick = true;
            G.scene = Scene::Slots;
        } else if (Input::keyPressed(GLFW_KEY_ESCAPE)) {
            Local::lobbyClose();
            G.scene = Scene::Menu;
        }
        return;
    }
    if (Input::pressed(GLFW_KEY_ESCAPE)) {
        if (s_setupSlot >= 0) s_setupSlot = -1;
        else if (s_confirmSlot >= 0) s_confirmSlot = -1;
        else if (s_langPicker && L::chosen()) s_langPicker = false;
        else if (G.scene == Scene::Intro && s_introThenPlay) { /* must choose Start */ }
        else if (G.scene == Scene::Lobby && Coop::active()) Coop::leave("");
        else if (G.scene != Scene::Menu) G.scene = Scene::Menu;
        else if (G.panel != Panel::None) G.panel = Panel::None;
    }
    if (G.scene == Scene::Credits) {
        s_creditsScroll = std::max(0.0f, s_creditsScroll - Input::scroll() * 12);
        // Controller: the right stick scrolls the credits.
        if (Input::usingPad()) s_creditsScroll = std::max(0.0f, s_creditsScroll + Input::aimAxis().y * 220.0f * dt);
    }
}

void menu_draw() {
    drawBackground();
    float W = (float)R::width(), H = (float)R::height();
    R::begin(R::UI, Vec2());
    R::rect(0, 0, W, H, pal(P_DARK, 0.35f));

    if (G.scene == Scene::Splash) {
        splashDraw(W, H);
    } else if (G.scene == Scene::Menu) {
        float ty = std::floor(H * 0.16f);
        R::textCentered("OUTBOUND", W / 2 + 2, ty + 2, pal(P_PURPLE), 6, false);
        R::textCentered("OUTBOUND", W / 2, ty, pal(P_YELLOW), 6, false);

        float bw = 140, bh = 18, bx = std::floor(W / 2 - bw / 2), by = std::floor(H * 0.46f);
        if (s_langPicker) {
            drawLanguagePanel(W, H, !L::chosen());
        } else if (G.panel == Panel::None) {
            if (UI::button(bx, by, bw, bh, T("Play"))) { s_coopPick = false; G.scene = Scene::Slots; }
            float cwid = std::floor((bw - 4) / 2);
            if (UI::button(bx, by + 24, cwid, bh, T("Local co-op"))) { Local::lobbyOpen(); G.scene = Scene::LocalLobby; }
            if (UI::button(bx + bw - cwid, by + 24, cwid, bh, T("Online co-op"))) G.scene = Scene::Lobby;
            if (UI::button(bx, by + 48, bw, bh, T("How to play"))) { s_introThenPlay = false; G.scene = Scene::Intro; }
            float hw = std::floor((bw - 4) / 2);
            if (UI::button(bx, by + 72, hw, bh, T("Controls"))) G.scene = Scene::Controls;
            if (UI::button(bx + bw - hw, by + 72, hw, bh, T("Options"))) G.panel = Panel::Options;
            if (UI::button(bx, by + 96, bw, bh, T("Credits"))) { G.scene = Scene::Credits; s_creditsScroll = 0; }
            if (UI::button(bx, by + 120, bw, bh, T("Exit"), true, P_CORAL)) G.quit = true;
            {
                std::string label = T("Check my other game: IncreMiner");
                float iw = std::floor(R::textWidth(label)) + 20;
                float ix = std::floor(W / 2 - iw / 2), iy = by + 150;
                if (UI::button(ix, iy, iw, bh, label, true, P_YELLOW)) openUrl("https://store.steampowered.com/app/5071340/INCREMINER/");
                if (UI::hover(ix, iy, iw, bh)) UI::tooltip("IncreMiner", T("Opens its Steam store page in your browser."));
            }
            if (!Coop::leaveReason().empty()) R::textCentered(Coop::leaveReason(), W / 2, by - 14, pal(P_CORAL));
            // Language shortcut, with the active flag next to it.
            float lx = bx + bw + 12, ly = by;
            drawFlag(L::get(), lx, ly + 3, 18, 12);
            if (UI::button(lx + 22, ly, 60, bh, T("Language"))) s_langPicker = true;
        }
        if (G.panel == Panel::Options) Options::drawPanel(W, H);
        R::text("0.11v  by Albert Freeman", 6, H - 11, pal(P_LAVENDER));
    } else if (G.scene == Scene::Slots) {
        drawSlots(W, H);
    } else if (G.scene == Scene::Lobby) {
        drawLobby(W, H);
    } else if (G.scene == Scene::LocalLobby) {
        drawLocalLobby(W, H);
    } else if (G.scene == Scene::Intro) {
        drawIntro(W, H);
    } else if (G.scene == Scene::Controls) {
        drawControlsPanel(std::floor(W / 2 - 120), std::floor(H / 2 - 101));
    } else if (G.scene == Scene::Credits) {
        float w = std::min(W - 40, 420.0f), h = H - 40;
        float x = std::floor(W / 2 - w / 2), y = 20;
        UI::panel(x, y, w, h, T("CREDITS"));
        // A credit written "Name: https://..." becomes a button that opens the page.
        struct Line { std::string text; int col; std::string url; };
        std::vector<Line> lines;
        auto add = [&](const std::string& s, int col) {
            for (auto& l : wrap(s, w - 24)) lines.push_back({l, col, ""});
        };
        add("OUTBOUND", P_YELLOW);
        add("", P_WHITE);
        add(T("This game was made by Albert Freeman."), P_WHITE);
        add("", P_WHITE);
        add(T("ASSETS USED"), P_YELLOW);
        bool any = false;
        for (auto& c : G.credits) {
            if (c.empty()) { add("", P_WHITE); continue; }
            size_t at = c.find("http");
            if (at != std::string::npos) {
                std::string label = c.substr(0, at);
                while (!label.empty() && (label.back() == ' ' || label.back() == ':' || label.back() == '-')) label.pop_back();
                std::string url = c.substr(at);
                while (!url.empty() && url.back() == ' ') url.pop_back();
                lines.push_back({label.empty() ? url : label, P_BEIGE, url});
            } else {
                add(c, P_BEIGE);
            }
            any = true;
        }
        if (!any) add(T("(credits.txt is empty - placeholder art and sound are generated by the game)"), P_LAVENDER);
        add("", P_WHITE);
        add(T("LIBRARIES"), P_YELLOW);
        add("GLFW, OpenAL Soft, libogg / libvorbis (Xiph.Org), stb_image and stb_easy_font by Sean Barrett", P_BEIGE);

        const float lineH = 10, linkH = 18;
        float total = 0;
        for (const Line& l : lines) total += l.url.empty() ? lineH : linkH;
        float viewH = h - 50;
        float maxScroll = std::max(0.0f, total - viewH);
        s_creditsScroll = std::min(s_creditsScroll, maxScroll);
        float ly = y + 22 - s_creditsScroll;
        for (size_t i = 0; i < lines.size(); i++) {
            const Line& l = lines[i];
            float lh = l.url.empty() ? lineH : linkH;
            bool visible = ly >= y + 18 && ly + lh <= y + 20 + viewH;
            if (visible) {
                if (!l.url.empty()) {
                    std::string label = l.text + "  >";
                    float bw = std::min(w - 24, std::floor(R::textWidth(label)) + 16);
                    if (UI::button(x + 12, ly, bw, 15, label, true, P_YELLOW)) openUrl(l.url);
                    if (UI::hover(x + 12, ly, bw, 15)) UI::tooltip(T("Open in your browser"), l.url);
                } else if (i == 0) {
                    R::textCentered(l.text, W / 2, ly, pal(l.col), 1, false);
                } else {
                    R::text(l.text, x + 12, ly, pal(l.col));
                }
            }
            ly += lh;
        }
        if (maxScroll > 0) R::text(T("scroll"), x + w - 40, y + h - 18, pal(P_PURPLE));
        if (UI::button(x + w / 2 - 40, y + h - 24, 80, 16, T("Back"))) G.scene = Scene::Menu;
    }
    // With a controller, say which buttons do what in here.
    if (Input::usingPad() && G.scene != Scene::LocalLobby) {
        const Prompt::Hint hints[] = {{Prompt::Select, T("Select")}, {Prompt::Back, T("Back")}};
        Prompt::row(hints, 2, W - 6 - Prompt::rowWidth(hints, 2), H - 20, pal(P_LAVENDER));
    }
    UI::endFrame();
    R::end();
}
