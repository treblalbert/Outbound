#include "local.h"
#include "assets.h"
#include "audio.h"
#include "coop.h"
#include "game.h"
#include "input.h"
#include "lang.h"
#include "options.h"
#include "render.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace Local {

constexpr int DEV_KBM_LOBBY = Input::DEV_KBM;

namespace {
// A seat's copy of the one-player state. Seat 0's lives in the globals; while another
// seat has its turn, the two are swapped, so this then holds seat 0's (see swapSeat).
struct Seat {
    bool used = false;
    int device = Input::DEV_KBM;
    Profile* prof = nullptr;      // its character (the co-op slot's), for seats 1..3
    Player player;
    Panel panel = Panel::None;
    int lootContainer = -1;
    float searchT = 0;
    int raidKills = 0;
    float raidStartMin = 0;
    RaidSummary summary;
    int traderPage = 0;
    bool inBed = false;
    int grace = 0;                // frames its input is ignored (the button it joined with)
    bool padLost = false;         // its controller was unplugged
};
Seat s_seats[MAX_SEATS];
bool s_on = false;
int s_cur = 0;
int s_depth = 0;                  // inside with(): structural changes wait
std::vector<std::function<void()>> s_deferred;
int s_uiOwner = -1;
// What player 1 was using the frame before anyone joined (their START is a pause).
bool s_p1Pad = false;
int s_p1PadId = -1;

void swapSeat(int k) {
    Seat& st = s_seats[k];
    std::swap(G.prof, *st.prof);
    std::swap(G.player, st.player);
    std::swap(G.panel, st.panel);
    std::swap(G.lootContainer, st.lootContainer);
    std::swap(G.searchT, st.searchT);
    std::swap(G.raidKills, st.raidKills);
    std::swap(G.raidStartMin, st.raidStartMin);
    std::swap(G.summary, st.summary);
    std::swap(G.traderPage, st.traderPage);
    Coop::swapLocalSeat(st.inBed);
    raid_swapSeat(k);
    base_swapSeat(k);
}

// The world belongs to the save: a seat's character sees the save's day, clock, horde
// schedule and defenses whenever it has its turn.
void syncWorld(int k) {
    const Profile& h = *s_seats[k].prof;   // holds the host's while k is in
    Profile& p = G.prof;
    p.day = h.day;
    p.dayRev = h.dayRev;
    p.worldSeed = h.worldSeed;
    p.difficulty = h.difficulty;
    p.gameMode = h.gameMode;
    p.rivals = false;
    p.timeMin = h.timeMin;
    p.nextHordeAt = h.nextHordeAt;
    p.hordeNum = h.hordeNum;
    p.hordesRepelled = h.hordesRepelled;
    p.safeNight = h.safeNight;
    p.baseHp = h.baseHp;
    for (int i = 0; i < TT_COUNT; i++) p.turretUnlocked[i] = h.turretUnlocked[i];
    for (int i = 0; i < DU_COUNT; i++) p.defUp[i] = h.defUp[i];
    p.turrets = h.turrets;
    p.tutorialDone = h.tutorialDone;
    p.mechanicMet = h.mechanicMet;
    p.cryptDoneDay = h.cryptDoneDay;
    p.cryptDoneMask = h.cryptDoneMask;
    if (h.locatorDay == h.day) p.locatorDay = h.day;   // one locator shows everyone the way
    p.missionSalt = p.missionSalt ? p.missionSalt : mix64(0x10CA1ull + (uint64_t)k);
}

void enterSeat(int k) {
    swapSeat(k);
    Coop::setLocalSlot(k);
    syncWorld(k);
}
void leaveSeatCtx(int k) {
    Coop::setLocalSlot(0);
    swapSeat(k);
}

Profile& profOf(int seat) {
    if (seat == s_cur) return G.prof;
    if (seat == 0) return *s_seats[s_cur].prof;
    return *s_seats[seat].prof;
}
Player& playerOf(int seat) {
    if (seat == s_cur) return G.player;
    if (seat == 0) return s_seats[s_cur].player;
    return s_seats[seat].player;
}
Panel& panelOf(int seat) {
    if (seat == s_cur) return G.panel;
    if (seat == 0) return s_seats[s_cur].panel;
    return s_seats[seat].panel;
}

int freeSeat() {
    for (int k = 1; k < MAX_SEATS; k++) if (!s_seats[k].used) return k;
    return -1;
}
bool deviceTaken(int dev) {
    for (int k = 0; k < MAX_SEATS; k++) if (s_seats[k].used && s_seats[k].device == dev) return true;
    return false;
}
std::string deviceName(int dev) {
    if (dev == Input::DEV_KBM) return T("keyboard and mouse");
    return Input::padName(dev);
}

void startSession(int p1Device) {
    Coop::beginLocal();
    s_on = true;
    s_cur = 0;
    s_depth = 0;
    s_uiOwner = -1;
    for (Seat& st : s_seats) st = Seat();
    s_seats[0].used = true;
    s_seats[0].device = p1Device;
    s_seats[0].prof = &G.prof;
    std::fprintf(stderr, "[local] session started, player 1 on %s\n", p1Device == Input::DEV_KBM ? "keyboard" : "a controller");
}

void join(int device) {
    int k = freeSeat();
    if (k < 0) return;
    Profile* prof = Coop::addLocalSeat(k);
    if (!prof) return;
    Seat& st = s_seats[k];
    st = Seat();
    st.used = true;
    st.device = device;
    st.prof = prof;
    st.grace = 3;
    // Standing beside player 1, in the bunker or outside.
    Vec2 at = playerOf(0).pos;
    with(k, [&] {
        if (G.scene == Scene::Raid) raid_seatJoin(at);
        else base_seatJoin(at);
    });
    publishSeats();
    Audio::play(Snd::click, 0.8f, 0.8f);
    pushMessage(T2("{0} joined ({1}).", Coop::player(k).name, deviceName(device)), Coop::colorPal(k));
    std::fprintf(stderr, "[local] seat %d joined (device %d)\n", k, device);
    save_game();
}

void removeSeat(int k) {
    if (k <= 0 || k >= MAX_SEATS || !s_seats[k].used) return;
    std::string name = Coop::player(k).name;
    with(k, [] {
        if (G.scene == Scene::Raid) raid_seatLeave();
        else G.panel = Panel::None;
    });
    Coop::removeLocalSeat(k);
    s_seats[k] = Seat();
    if (s_uiOwner == k) s_uiOwner = -1;
    pushMessage(T1("{0} left the game.", name), P_LAVENDER);
    std::fprintf(stderr, "[local] seat %d left\n", k);
}
}  // namespace

// ---------------------------------------------------------------- queries
bool active() { return s_on && Coop::local(); }
int count() {
    if (!active()) return 1;
    int n = 0;
    for (const Seat& st : s_seats) n += st.used ? 1 : 0;
    return n;
}
bool used(int seat) { return active() && seat >= 0 && seat < MAX_SEATS && s_seats[seat].used; }
int current() { return active() ? s_cur : 0; }
int deviceOf(int seat) { return used(seat) ? s_seats[seat].device : Input::DEV_ALL; }
std::string nameOf(int seat) { return used(seat) ? Coop::player(seat).name : ""; }
int colorOf(int seat) { return Coop::colorPal(seat); }
Profile& hostProfile() { return active() && s_cur != 0 ? *s_seats[s_cur].prof : G.prof; }

// ---------------------------------------------------------------- turns
void with(int seat, const std::function<void()>& f) {
    if (!active() || seat < 0 || seat >= MAX_SEATS || !s_seats[seat].used) { f(); return; }
    int prev = s_cur;
    int prevDev = Input::device();
    if (seat != prev) {
        if (prev != 0) leaveSeatCtx(prev);
        if (seat != 0) enterSeat(seat);
        s_cur = seat;
    }
    Input::setDevice(s_seats[seat].device);
    s_depth++;
    f();
    s_depth--;
    if (seat != prev) {
        if (seat != 0) leaveSeatCtx(seat);
        if (prev != 0) enterSeat(prev);
        s_cur = prev;
    }
    Input::setDevice(prevDev);
}

void forEach(const std::function<void(int seat)>& f) {
    if (!active()) { f(0); return; }
    for (int k = 0; k < MAX_SEATS; k++) {
        if (!s_seats[k].used) continue;
        with(k, [&] { f(k); });
        if (!active()) return;   // the session ended in that turn
    }
}

void atHome(std::function<void()> f) {
    if (!active() || (s_cur == 0 && s_depth == 0)) { f(); return; }
    s_deferred.push_back(std::move(f));
}

void runDeferred() {
    if (s_cur != 0 || s_depth > 0) return;
    for (int guard = 0; guard < 8 && !s_deferred.empty(); guard++) {
        std::vector<std::function<void()>> todo;
        todo.swap(s_deferred);
        for (auto& f : todo) f();
    }
}

int uiOwner() {
    if (!active()) return G.panel != Panel::None ? 0 : -1;
    if (s_uiOwner >= 0 && (!s_seats[s_uiOwner].used || panelOf(s_uiOwner) == Panel::None)) s_uiOwner = -1;
    if (s_uiOwner < 0)
        for (int k = 0; k < MAX_SEATS; k++)
            if (s_seats[k].used && panelOf(k) != Panel::None) { s_uiOwner = k; break; }
    return s_uiOwner;
}

bool otherHasPanel() {
    if (!active()) return false;
    int o = uiOwner();
    return o >= 0 && o != s_cur;
}

bool justJoined() { return active() && s_seats[s_cur].grace > 0; }

// ---------------------------------------------------------------- joining
std::string joinHint() {
    if (Coop::online() || G.prof.rivals) return "";
    if (count() >= MAX_SEATS) return "";
    return T("Another controller: press START to join");
}

void update(float dt) {
    (void)dt;
    if (Coop::online()) { s_on = false; return; }
    if (s_on && !Coop::local()) { s_on = false; for (Seat& st : s_seats) st = Seat(); }
    for (Seat& st : s_seats) if (st.grace > 0) st.grace--;
    bool inGame = G.scene == Scene::Base || G.scene == Scene::Raid;
    // Who player 1 is, until someone joins: whatever they touched last.
    if (!s_on) {
        bool wasPad = s_p1Pad;
        int wasId = s_p1PadId;
        s_p1Pad = Input::usingPad();
        s_p1PadId = Input::lastPad();
        if (!inGame || G.prof.rivals || G.devClean) return;
        // A free controller's START (not the one player 1 is holding), or ENTER when
        // player 1 is on a controller and the keyboard is free.
        for (int j = 0; j < Input::MAX_PADS; j++) {
            if (!Input::padButtonPressed(j, GLFW_GAMEPAD_BUTTON_START)) continue;
            if (wasPad && wasId == j) continue;   // that is player 1 pausing
            startSession(wasPad && wasId >= 0 ? wasId : Input::DEV_KBM);
            join(j);
            return;
        }
        if (wasPad && wasId >= 0 && G.panel == Panel::None && Input::keyPressed(GLFW_KEY_ENTER)) {
            startSession(wasId);
            join(Input::DEV_KBM);
        }
        return;
    }
    // ---- in a session
    // Controllers that come and go.
    for (int k = 0; k < MAX_SEATS; k++) {
        Seat& st = s_seats[k];
        if (!st.used || st.device == Input::DEV_KBM) continue;
        bool on = Input::padConnected(st.device);
        if (!on && !st.padLost) pushMessage(T1("{0}: controller disconnected.", Coop::player(k).name), P_CORAL);
        if (on && st.padLost) pushMessage(T1("{0}: controller back.", Coop::player(k).name), P_YGREEN);
        st.padLost = !on;
    }
    if (!inGame) return;
    // Nobody is in a menu: a free controller can join.
    bool anyPanel = false;
    for (int k = 0; k < MAX_SEATS; k++) if (s_seats[k].used && panelOf(k) != Panel::None) anyPanel = true;
    if (freeSeat() < 0) return;
    for (int j = 0; j < Input::MAX_PADS; j++) {
        if (deviceTaken(j) || !Input::padButtonPressed(j, GLFW_GAMEPAD_BUTTON_START)) continue;
        join(j);
        return;
    }
    if (!anyPanel && !deviceTaken(Input::DEV_KBM) && Input::keyPressed(GLFW_KEY_ENTER)) join(Input::DEV_KBM);
}

void leaveSeat(int seat) {
    atHome([seat] {
        removeSeat(seat);
        bool anyone = false;
        for (int k = 1; k < MAX_SEATS; k++) anyone = anyone || s_seats[k].used;
        if (!anyone) end();
    });
}

void end() {
    if (!s_on) return;
    if (s_cur != 0 || s_depth > 0) { atHome([] { end(); }); return; }
    for (int k = 1; k < MAX_SEATS; k++) if (s_seats[k].used) removeSeat(k);
    raid_localEnded();
    Coop::endLocal();
    s_on = false;
    s_uiOwner = -1;
    for (Seat& st : s_seats) st = Seat();
    Input::setDevice(Input::DEV_ALL);
    pushMessage(T("Back to one player."), P_LAVENDER);
    std::fprintf(stderr, "[local] session over\n");
}

// ---------------------------------------------------------------- the lobby (0.12v)
namespace {
LobbySeat s_lobby[MAX_SEATS];
bool s_lobbyOn = false;
LobbySeat s_plan[MAX_SEATS];
bool s_planOn = false;

int lobbySeatOf(int device) {
    for (int k = 0; k < MAX_SEATS; k++) if (s_lobby[k].used && s_lobby[k].device == device) return k;
    return -1;
}
// Player 1 is the first seat taken; the others fill in behind.
int lobbyFree() {
    for (int k = 0; k < MAX_SEATS; k++) if (!s_lobby[k].used) return k;
    return -1;
}
void lobbyJoin(int device) {
    int k = lobbyFree();
    if (k < 0 || lobbySeatOf(device) >= 0) return;
    s_lobby[k].used = true;
    s_lobby[k].device = device;
    // A shirt nobody else in the lobby has on.
    int shirt = (k * 3) % Assets::SHIRT_COUNT;
    for (int tries = 0; tries < Assets::SHIRT_COUNT; tries++) {
        bool taken = false;
        for (int j = 0; j < MAX_SEATS; j++) if (j != k && s_lobby[j].used && s_lobby[j].shirt == shirt) taken = true;
        if (!taken) break;
        shirt = (shirt + 1) % Assets::SHIRT_COUNT;
    }
    s_lobby[k].shirt = shirt;
    Audio::play(Snd::click, 0.8f, 1.1f);
}
void lobbyLeave(int k) {
    if (k < 0 || !s_lobby[k].used) return;
    s_lobby[k] = LobbySeat();
    // Everyone behind moves up a place, so player 1 is always the first seat.
    for (int j = k; j + 1 < MAX_SEATS; j++) std::swap(s_lobby[j], s_lobby[j + 1]);
    Audio::play(Snd::click, 0.6f, 0.8f);
}
void lobbyShirt(int k, int dir) {
    int n = Assets::SHIRT_COUNT;
    s_lobby[k].shirt = ((s_lobby[k].shirt + dir) % n + n) % n;
    Audio::play(Snd::click, 0.4f, 1.3f);
}
}  // namespace

void lobbyOpen() {
    for (LobbySeat& st : s_lobby) st = LobbySeat();
    s_lobbyOn = true;
}
void lobbyJoinKeyboard() { if (s_lobbyOn) lobbyJoin(DEV_KBM_LOBBY); }
void lobbyClose() { s_lobbyOn = false; for (LobbySeat& st : s_lobby) st = LobbySeat(); }
const LobbySeat& lobbySeat(int k) { static LobbySeat none; return k >= 0 && k < MAX_SEATS ? s_lobby[k] : none; }
int lobbyCount() { int n = 0; for (const LobbySeat& st : s_lobby) n += st.used ? 1 : 0; return n; }
bool lobbyReady() { return lobbyCount() >= 2; }

bool lobbyUpdate() {
    if (!s_lobbyOn) return false;
    bool start = false;
    // Controllers: A or START joins, B leaves, left / right the shirt, START (player 1) starts.
    for (int j = 0; j < Input::MAX_PADS; j++) {
        int k = lobbySeatOf(j);
        if (k >= 0 && !Input::padConnected(j)) { lobbyLeave(k); continue; }
        if (!Input::padConnected(j)) continue;
        if (k < 0) {
            if (Input::padButtonPressed(j, GLFW_GAMEPAD_BUTTON_A) || Input::padButtonPressed(j, GLFW_GAMEPAD_BUTTON_START)) lobbyJoin(j);
            continue;
        }
        if (Input::padButtonPressed(j, GLFW_GAMEPAD_BUTTON_B)) { lobbyLeave(k); continue; }
        if (Input::padButtonPressed(j, GLFW_GAMEPAD_BUTTON_DPAD_LEFT)) lobbyShirt(k, -1);
        if (Input::padButtonPressed(j, GLFW_GAMEPAD_BUTTON_DPAD_RIGHT)) lobbyShirt(k, +1);
        if (k == 0 && Input::padButtonPressed(j, GLFW_GAMEPAD_BUTTON_START) && lobbyReady()) start = true;
    }
    // The keyboard and mouse.
    int kb = lobbySeatOf(DEV_KBM_LOBBY);
    if (kb < 0) {
        if (Input::keyPressed(GLFW_KEY_ENTER) || Input::keyPressed(GLFW_KEY_SPACE)) lobbyJoin(DEV_KBM_LOBBY);
    } else {
        if (Input::keyPressed(GLFW_KEY_BACKSPACE)) lobbyLeave(kb);
        else {
            if (Input::keyPressed(GLFW_KEY_LEFT) || Input::keyPressed(GLFW_KEY_A)) lobbyShirt(kb, -1);
            if (Input::keyPressed(GLFW_KEY_RIGHT) || Input::keyPressed(GLFW_KEY_D)) lobbyShirt(kb, +1);
            if (kb == 0 && Input::keyPressed(GLFW_KEY_ENTER) && lobbyReady()) start = true;
        }
    }
    return start;
}

void lobbyCommit() {
    for (int k = 0; k < MAX_SEATS; k++) s_plan[k] = s_lobby[k];
    s_planOn = lobbyCount() >= 2;
    s_lobbyOn = false;
}
bool planPending() { return s_planOn; }
void cancelPlan() { s_planOn = false; }

void applyPlan() {
    if (!s_planOn) return;
    if (Coop::online() || (G.scene != Scene::Base && G.scene != Scene::Raid)) return;
    s_planOn = false;
    if (s_on) end();
    startSession(s_plan[0].device);
    for (int k = 1; k < MAX_SEATS; k++) {
        if (!s_plan[k].used) continue;
        join(s_plan[k].device);
        // The shirt they picked in the lobby.
        if (s_seats[k].used && s_seats[k].prof) {
            s_seats[k].prof->shirt = s_plan[k].shirt;
            Coop::player(k).shirt = s_plan[k].shirt;
        }
    }
    publishSeats();
}

// ---------------------------------------------------------------- everyone, as others see them
void publishSeats() {
    if (!active()) return;
    forEach([](int k) {
        Coop::NetPlayer& me = Coop::player(k);
        const Profile& p = G.prof;
        me.used = true;
        me.name = p.charName.empty() ? T1("Player {0}", std::to_string(k + 1)) : p.charName;
        me.shirt = std::clamp(p.shirt, 0, Assets::SHIRT_COUNT - 1);
        bool raid = G.scene == Scene::Raid;
        me.where = raid ? (raid_localOut() ? Coop::W_NONE : Coop::W_RAID) : Coop::W_BASE;
        me.lastPos = me.pos;
        me.pos = me.target = G.player.pos;
        me.angle = G.player.angle;
        me.weapon = p.weapons[p.curWeapon].id;
        me.moving = G.player.moving;
        me.reloading = G.player.reloadT > 0;
        me.downed = raid_localDowned(me.downT);
        me.inBed = false;
        me.laser = p.laserActive();
        me.inCrypt = raid && raid_localCrypt() >= 0;
        me.ride = raid ? raid_localRide() : -1;
        me.flashT = G.player.flashT;
        me.hurtT = G.player.hurtT;
        me.hp = p.hp;
        me.maxHp = p.maxHp();
        me.heardT = 0;
    });
}

// ---------------------------------------------------------------- the shared camera
float maxZoom() { return Options::coopZoom() ? R::zoomMax() : 1.0f; }

bool groupBox(Vec2& lo, Vec2& hi, int skipSeat) {
    bool any = false;
    uint8_t want = G.scene == Scene::Raid ? Coop::W_RAID : Coop::W_BASE;
    for (int k = 0; k < MAX_SEATS; k++) {
        if (!s_seats[k].used || k == skipSeat) continue;
        const Coop::NetPlayer& np = Coop::player(k);
        if (np.where != want) continue;
        Vec2 p = k == s_cur ? G.player.pos : np.target;
        if (!any) { lo = hi = p; any = true; }
        lo.x = std::min(lo.x, p.x); lo.y = std::min(lo.y, p.y);
        hi.x = std::max(hi.x, p.x); hi.y = std::max(hi.y, p.y);
    }
    return any;
}

Vec2 leash(Vec2 from, Vec2 to) {
    if (!active() || count() < 2) return to;
    Vec2 lo, hi;
    if (!groupBox(lo, hi, s_cur)) return to;
    // The widest the camera goes, less a margin so nobody is ever half off the edge
    // (and room at the bottom for the player cards).
    float z = maxZoom();
    float maxW = R::width() * z - 56, maxH = R::height() * z - 56 - 44 * z;
    auto axis = [](float f, float t, float l, float h, float maxSpan) {
        float nl = std::min(l, t), nh = std::max(h, t);
        if (nh - nl <= maxSpan) return t;
        // Too far apart: only moving back toward the others is allowed.
        bool away = (t > h && t > f) || (t < l && t < f);
        return away ? f : t;
    };
    return Vec2(axis(from.x, to.x, lo.x, hi.x, maxW), axis(from.y, to.y, lo.y, hi.y, maxH));
}

void devSeats(int n) {
    if (Coop::online() || n < 2) return;
    if (!s_on) startSession(Input::DEV_KBM);
    for (int j = 0; j < n - 1 && freeSeat() >= 0; j++) {
        join(j);
        int k = 0;
        for (int i = 0; i < MAX_SEATS; i++) if (s_seats[i].used && s_seats[i].device == j) k = i;
        Vec2 at = playerOf(0).pos + Vec2(k % 2 ? 90.0f : -90.0f, k >= 2 ? 60.0f : -30.0f);
        with(k, [&] { G.player.pos = at; G.player.angle = (float)k; });
    }
    publishSeats();
}

}  // namespace Local
