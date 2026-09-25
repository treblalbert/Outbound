#include "coop.h"
#include "assets.h"
#include "voice.h"
#include "audio.h"
#include "game.h"
#include "lang.h"
#include "local.h"
#include <fstream>
#include <map>
#include <memory>
#include <sstream>

void ensureDir(const std::string& path);   // game.cpp

namespace Coop {

namespace {
enum class Mode { Off, Host, Guest, Local };
Mode s_mode = Mode::Off;
NetPlayer s_players[MAX_PLAYERS];
int s_local = 0;
bool s_welcomed = false;
std::string s_leaveReason;
float s_helloT = 0, s_stateT = 0, s_worldT = 0, s_profileT = 0;
std::string s_lastProfileSent;
bool s_localInBed = false;
// The shared stash. Host: which slot is working in it (-1 nobody). Guest: whether
// the host gave it to us, a copy of it, and what we last told the host.
int s_stashLock = -1;
enum class StashAsk { None, Waiting, Granted, Denied };
StashAsk s_stashAsk = StashAsk::None;
std::vector<Item> s_stashMirror, s_stashSent;
std::string s_stashHolder;
float s_stashAskT = 0;
// Host: every guest character met this session, by Steam id. Kept after they
// leave so rejoining picks up where they were.
std::map<uint64_t, std::unique_ptr<Profile>> s_guestProfiles;
// Local co-op (0.12v): what the host would have sent a seat, waiting for its turn.
std::vector<std::vector<uint8_t>> s_localInbox[MAX_PLAYERS];
constexpr uint64_t LOCAL_ID = 0x10CA1000ull;   // local seats' ids: LOCAL_ID + seat

// The messages a local seat needs: what happened to that player. Everything about the
// world is shared already (it is all one game).
bool personalMsg(uint8_t t) {
    return t == M_DAMAGE || t == M_REWARD || t == M_MERC_DIED || t == M_CAR_DMG || t == M_REVIVE || t == M_OVERRUN;
}

std::string guestPath(uint64_t id) {
    // A local seat's character: saves/outboundN_coop/local2.sav and so on.
    if (id > LOCAL_ID && id < LOCAL_ID + MAX_PLAYERS) return coopGuestDir() + "/local" + std::to_string((int)(id - LOCAL_ID) + 1) + ".sav";
    return coopGuestDir() + "/" + std::to_string((unsigned long long)id) + ".sav";
}

// A new guest starts the way a new game does: a pistol, some rounds and bandages.
std::unique_ptr<Profile> freshGuest(uint64_t id) {
    auto p = std::make_unique<Profile>();
    p->missionSalt = mix64(id ^ 0xC0FFEEull);
    p->weapons[0] = makeItem(IT_PISTOL);
    addToSlots(p->inv, makeItem(IT_AMMO_LIGHT, 48));
    addToSlots(p->inv, makeItem(IT_BANDAGE, 3));
    addToSlots(p->privStash, makeItem(IT_AMMO_LIGHT, 36), PRIVATE_STASH_SLOTS);
    addToSlots(p->privStash, makeItem(IT_GRENADE, 1), PRIVATE_STASH_SLOTS);
    p->hp = p->maxHp();
    return p;
}

Profile* guestProfile(uint64_t id) {
    auto it = s_guestProfiles.find(id);
    if (it != s_guestProfiles.end()) return it->second.get();
    std::unique_ptr<Profile> p;
    std::ifstream f(guestPath(id));
    if (f) {
        std::stringstream ss;
        ss << f.rdbuf();
        p = std::make_unique<Profile>();
        if (!profileFromText(ss.str(), *p)) p.reset();
    }
    if (!p) p = freshGuest(id);
    if (!p->missionSalt) p->missionSalt = mix64(id ^ 0xC0FFEEull);
    // Guests used to have a stash of their own. Since the big stash became the shared
    // one, what they kept there moves to their private stash, and whatever does not
    // fit goes into the shared one (when nobody is working in it).
    for (Item& it : p->stash) {
        if (it.empty()) continue;
        int left = addToSlots(p->privStash, it, PRIVATE_STASH_SLOTS);
        if (left > 0 && s_stashLock < 0) {
            Item rest = it;
            rest.count = (int16_t)left;
            left = addToSlots(G.prof.stash, rest, STASH_SLOTS);
        }
        if (left <= 0) it = Item();
        else it.count = (int16_t)left;
    }
    Profile* raw = p.get();
    s_guestProfiles[id] = std::move(p);
    return raw;
}

// ---- world state: the clock, the horde, the bunker and its turrets
void writeWorld(Net::Writer& w) {
    const Profile& p = G.prof;
    bool hActive;
    int hN, hLeft;
    raid_hordeState(hActive, hN, hLeft);
    w.u32((uint32_t)p.day);
    w.i32(p.dayRev);
    w.u64(p.worldSeed);
    w.u8((uint8_t)p.difficulty);
    w.u8((uint8_t)p.gameMode);
    w.u8(p.rivals ? 1 : 0);
    w.f32(p.timeMin);
    w.u8(G.nightFallen ? 1 : 0);
    w.u8(hActive ? 1 : 0);
    w.i32(hN);
    w.i32(hLeft);
    w.f32(p.nextHordeAt);
    w.i32(p.hordeNum);
    w.i32(p.hordesRepelled);
    w.i32(p.safeNight);
    w.f32(p.baseHp);
    for (int i = 0; i < TT_COUNT; i++) w.u8(p.turretUnlocked[i] ? 1 : 0);
    for (int i = 0; i < DU_COUNT; i++) w.u8((uint8_t)p.defUp[i]);
    w.u8((uint8_t)p.turrets.size());
    for (const Turret& t : p.turrets) {
        w.i16((int16_t)t.dx);
        w.i16((int16_t)t.dy);
        w.u8((uint8_t)t.type);
        w.u8((uint8_t)t.level);
        w.f32(t.hp);
    }
    w.u8((uint8_t)p.barricades.size());
    for (const Barricade& b : p.barricades) {
        w.i16((int16_t)b.dx);
        w.i16((int16_t)b.dy);
        w.u8((uint8_t)b.type);
        w.f32(b.hp);
    }
}

void readWorld(Net::Reader& r) {
    Profile& p = G.prof;
    int day = (int)r.u32();
    int rev = r.i32();
    uint64_t seed = r.u64();
    int difficulty = std::clamp((int)r.u8(), 0, DIFF_COUNT - 1);
    int gameMode = std::clamp((int)r.u8(), 0, MODE_COUNT - 1);
    bool rivals = r.u8() != 0;
    float time = r.f32();
    bool night = r.u8() != 0;
    bool hActive = r.u8() != 0;
    int hN = r.i32(), hLeft = r.i32();
    float nextAt = r.f32();
    int hordeNum = r.i32(), repelled = r.i32(), safeNight = r.i32();
    float baseHp = r.f32();
    bool unlocked[TT_COUNT];
    for (int i = 0; i < TT_COUNT; i++) unlocked[i] = r.u8() != 0;
    int defUp[DU_COUNT];
    for (int i = 0; i < DU_COUNT; i++) defUp[i] = r.u8();
    int n = r.u8();
    std::vector<Turret> turrets;
    for (int i = 0; i < n; i++) {
        Turret t;
        t.dx = r.i16();
        t.dy = r.i16();
        t.type = std::clamp((int)r.u8(), 0, TT_COUNT - 1);
        t.level = std::clamp((int)r.u8(), 1, TURRET_MAX_LEVEL);
        t.hp = r.f32();
        turrets.push_back(t);
    }
    std::vector<Barricade> barricades;
    int nb = r.u8();
    for (int i = 0; i < nb; i++) {
        Barricade b;
        b.dx = r.i16();
        b.dy = r.i16();
        b.type = std::clamp((int)r.u8(), 0, BT_COUNT - 1);
        b.hp = r.f32();
        barricades.push_back(b);
    }
    if (r.bad) return;
    bool newWorld = day != p.day || rev != p.dayRev || seed != p.worldSeed;
    p.day = day;
    p.dayRev = rev;
    p.worldSeed = seed;
    // The host's save decides how everyone plays.
    p.difficulty = difficulty;
    p.gameMode = gameMode;
    p.rivals = rivals;
    p.timeMin = time;
    p.nextHordeAt = nextAt;
    p.hordeNum = hordeNum;
    p.hordesRepelled = repelled;
    p.safeNight = safeNight;
    p.baseHp = baseHp;
    for (int i = 0; i < TT_COUNT; i++) p.turretUnlocked[i] = unlocked[i];
    for (int i = 0; i < DU_COUNT; i++) p.defUp[i] = defUp[i];
    // Keep the aim of turrets we already know about; the snapshot moves them.
    if (turrets.size() == p.turrets.size()) {
        for (size_t i = 0; i < turrets.size(); i++) {
            Turret& t = p.turrets[i];
            bool moved = t.dx != turrets[i].dx || t.dy != turrets[i].dy;
            t.dx = turrets[i].dx; t.dy = turrets[i].dy; t.type = turrets[i].type; t.level = turrets[i].level; t.hp = turrets[i].hp;
            if (moved) t.angle = turrets[i].angle;
        }
    } else {
        p.turrets = turrets;
    }
    // Barricades: put the host's list into the world when it changed.
    bool barrChanged = barricades.size() != p.barricades.size();
    for (size_t i = 0; !barrChanged && i < barricades.size(); i++) {
        const Barricade &a = barricades[i], &b = p.barricades[i];
        barrChanged = a.dx != b.dx || a.dy != b.dy || a.type != b.type || (a.hp > 0) != (b.hp > 0);
    }
    for (size_t i = 0; i < barricades.size() && i < p.barricades.size(); i++) barricades[i].hurtT = p.barricades[i].hurtT;
    p.barricades = barricades;
    if (barrChanged && !newWorld && G.world.w > 0) placeTurretsInWorld(G.world);
    raid_setHordeState(hActive, hN, hLeft, night);
    if (newWorld && G.scene == Scene::Raid) raid_forceHome();
}

void writeState(Net::Writer& w, int slot) {
    const NetPlayer& pl = s_players[slot];
    w.u8((uint8_t)slot);
    w.u8(pl.where);
    w.f32(pl.target.x);
    w.f32(pl.target.y);
    w.f32(pl.angle);
    w.u8((uint8_t)pl.weapon);
    uint8_t f = (pl.moving ? 1 : 0) | (pl.reloading ? 2 : 0) | (pl.downed ? 4 : 0) | (pl.inBed ? 8 : 0) | (pl.laser ? 16 : 0) |
                (pl.flashT > 0 ? 32 : 0) | (pl.hurtT > 0 ? 64 : 0) | (pl.inCrypt ? 128 : 0);
    w.u8(f);
    w.f32(pl.hp);
    w.f32(pl.maxHp);
    w.f32(pl.downT);
    w.u8((uint8_t)(pl.ride + 1));
}

void readState(Net::Reader& r, int expectSlot) {
    int slot = r.u8();
    uint8_t where = r.u8();
    Vec2 pos{r.f32(), r.f32()};
    float angle = r.f32();
    int weapon = r.u8();
    uint8_t f = r.u8();
    float hp = r.f32(), maxHp = r.f32(), downT = r.f32();
    int ride = (int)r.u8() - 1;
    if (r.bad || slot < 0 || slot >= MAX_PLAYERS || slot == s_local) return;
    if (expectSlot >= 0 && slot != expectSlot) return;
    NetPlayer& pl = s_players[slot];
    if (!pl.used) return;
    bool jumped = pl.where != where || dist(pl.target, pos) > 96;
    pl.where = where;
    pl.target = pos;
    if (jumped) pl.pos = pl.lastPos = pos;
    pl.angle = angle;
    pl.weapon = weapon;
    pl.moving = f & 1;
    pl.reloading = f & 2;
    pl.downed = f & 4;
    pl.inBed = f & 8;
    pl.laser = f & 16;
    pl.inCrypt = (f & 128) != 0;
    if (f & 32) pl.flashT = 0.09f;
    if (f & 64) pl.hurtT = 0.2f;
    pl.hp = hp;
    pl.maxHp = maxHp;
    pl.downT = downT;
    pl.ride = ride;
    pl.heardT = 0;
}

void sendRoster() {
    Net::Writer w;
    w.u8(M_ROSTER);
    for (int i = 0; i < MAX_PLAYERS; i++) {
        w.u8(s_players[i].used ? 1 : 0);
        w.u64(s_players[i].id);
        w.str(s_players[i].name);
        w.u8((uint8_t)s_players[i].shirt);
    }
    broadcast(w, true);
}

void sendWelcome(int slot) {
    NetPlayer& pl = s_players[slot];
    Net::Writer w;
    w.u8(M_WELCOME);
    w.u8((uint8_t)slot);
    w.u8(Net::lobbyStarted() ? 1 : 0);
    w.str(profileToText(*pl.prof));
    writeWorld(w);
    sendReliable(slot, w);
}

void onHello(uint64_t from, Net::Reader& r) {
    int proto = (int)r.u32();
    std::string name = cleanCharName(r.str());
    int shirt = std::clamp((int)r.u8(), 0, Assets::SHIRT_COUNT - 1);
    if (r.bad) return;
    if (proto != PROTOCOL) {
        Net::Writer w;
        w.u8(M_BYE);
        w.str("Your game is a different version than the host's.");
        Net::send(from, w.b, true);
        return;
    }
    int slot = slotOf(from);
    if (slot < 0) {
        for (int i = 1; i < MAX_PLAYERS && slot < 0; i++)
            if (!s_players[i].used) slot = i;
        if (slot < 0) {
            Net::Writer w;
            w.u8(M_BYE);
            w.str("The game is full.");
            Net::send(from, w.b, true);
            return;
        }
        NetPlayer& pl = s_players[slot];
        pl = NetPlayer();
        pl.used = true;
        pl.id = from;
        pl.name = name.empty() ? Net::nameOf(from) : name;
        pl.where = W_LOBBY;
        pl.prof = guestProfile(from);
        pl.shirt = shirt;
        // The character in the host's save wears what its player chose last.
        pl.prof->charName = name;
        pl.prof->shirt = shirt;
        pushMessage(T1("{0} joined the game.", pl.name), colorPal(slot));
        raid_coopPlayersChanged();
    }
    sendWelcome(slot);
    sendRoster();
}

void writeStashItems(Net::Writer& w, const std::vector<Item>& v) {
    w.u8((uint8_t)v.size());
    for (const Item& it : v) { w.i16(it.id); w.i16(it.count); w.i32(it.data); w.u8((uint8_t)it.tier); w.u8(it.flags); }
}

std::vector<Item> readStashItems(Net::Reader& r) {
    int n = r.u8();
    std::vector<Item> v;
    for (int i = 0; i < n && !r.bad; i++) {
        Item it;
        it.id = r.i16();
        it.count = r.i16();
        it.data = r.i32();
        it.tier = (int8_t)r.u8();
        it.flags = r.u8();
        if (it.id <= IT_NONE || it.id >= IT_COUNT || it.count <= 0) it = Item();
        v.push_back(it);
    }
    v.resize(STASH_SLOTS);
    return v;
}

// Host: a guest asks for the shared stash. It is theirs unless someone else is in it.
void onStashOpen(int slot) {
    Net::Writer w;
    w.u8(M_STASH);
    if (s_stashLock < 0 || s_stashLock == slot) {
        s_stashLock = slot;
        w.u8(1);
        writeStashItems(w, G.prof.stash);
    } else {
        w.u8(0);
        w.str(s_players[s_stashLock].name);
    }
    sendReliable(slot, w);
}

void onStashSet(int slot, Net::Reader& r) {
    std::vector<Item> v = readStashItems(r);
    if (r.bad || s_stashLock != slot) return;
    G.prof.stash = v;
}

void onDefenseOp(int slot, Net::Reader& r) {
    int op = r.u8();
    int a = r.i32(), b = r.i32(), c = r.i32(), cost = r.i32();
    if (r.bad) return;
    if (defense_apply(op, a, b, c)) {
        save_game();
        return;
    }
    // Someone got there first (the tile was taken, the research already done...).
    Net::Writer w;
    w.u8(M_REFUND);
    w.i32(cost);
    sendReliable(slot, w);
}

void removePlayer(uint64_t id, const std::string& why) {
    int slot = slotOf(id);
    if (slot <= 0) return;
    if (s_stashLock == slot) s_stashLock = -1;
    NetPlayer& pl = s_players[slot];
    if (pl.prof && !G.devNoSave) {
        ensureDir(coopGuestDir());
        std::ofstream f(guestPath(id));
        f << profileToText(*pl.prof);
    }
    pushMessage(T1("{0} left the game.", pl.name) + (why.empty() ? "" : " (" + why + ")"), P_LAVENDER);
    pl = NetPlayer();
    raid_coopPlayersChanged();
    sendRoster();
}

// Host: a guest's character arrived. Their inventory, money and so on are theirs to
// change; whether a mercenary is alive and how hurt it is belongs to the host, since
// that is where they fight.
void onProfile(int slot, Net::Reader& r) {
    std::string text = r.str();
    if (r.bad || slot <= 0 || !s_players[slot].prof) return;
    Profile incoming;
    if (!profileFromText(text, incoming)) return;
    NetPlayer& pl = s_players[slot];
    Profile& cur = *pl.prof;
    std::vector<Hireling> merged;
    for (Hireling h : incoming.squad) {
        if (std::find(pl.deadMercs.begin(), pl.deadMercs.end(), h.name) != pl.deadMercs.end()) continue;
        for (const Hireling& old : cur.squad)
            if (old.name == h.name) {
                bool guard = h.guard;
                h = old;               // keep position, health, everything runtime
                h.guard = guard;       // but orders come from the owner
            }
        merged.push_back(h);
    }
    cur = incoming;
    cur.squad = merged;
}

void applyNewDay(Net::Reader& r) {
    int day = (int)r.u32();
    bool force = r.u8() != 0;
    std::string note = r.str();
    if (r.bad) return;
    Profile& p = G.prof;
    p.day = day;
    p.dayRev = 0;
    p.dayMem.clear();
    p.timeMin = DAY_START_MIN;
    p.hp = p.maxHp();
    for (Hireling& h : p.squad) if (!h.dead) h.hp = h.maxHp();
    rollDailyMission();
    s_localInBed = false;
    if (G.scene == Scene::Raid) raid_forceHome();
    if (G.scene == Scene::Base) base_coopWoke(note, force);
    sendProfile();
}
}  // namespace

// ---------------------------------------------------------------- queries
bool active() { return s_mode != Mode::Off; }
bool host() { return s_mode == Mode::Host || s_mode == Mode::Local; }
bool guest() { return s_mode == Mode::Guest; }
bool local() { return s_mode == Mode::Local; }
bool online() { return s_mode == Mode::Host || s_mode == Mode::Guest; }
int localSlot() { return s_local; }
NetPlayer& player(int slot) { return s_players[std::clamp(slot, 0, MAX_PLAYERS - 1)]; }
int shirtOf(int slot) { return std::clamp(s_players[std::clamp(slot, 0, MAX_PLAYERS - 1)].shirt, 0, Assets::SHIRT_COUNT - 1); }
int colorPal(int slot) { return Assets::shirt(shirtOf(slot)).uiPal; }
bool welcomed() { return s_welcomed; }
std::string leaveReason() { return s_leaveReason; }
bool localInBed() { return s_localInBed; }
void setInBed(bool b) { s_localInBed = b; }

int count() {
    if (!active()) return 1;
    int n = 0;
    for (const NetPlayer& p : s_players) n += p.used ? 1 : 0;
    return std::max(1, n);
}

float enemyScale(uint64_t seed) {
    int n = count();
    float m = 1.0f;
    if (n >= 3) m += 0.10f + 0.05f * ((mix64(seed ^ 0x5CA1Eull) & 0xFFFF) / 65535.0f);
    if (n >= 5) m += 0.10f;
    if (n >= 7) m += 0.10f;
    if (n >= 8) m += 0.05f;
    return m;
}

int slotOf(uint64_t id) {
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (s_players[i].used && s_players[i].id == id) return i;
    return -1;
}

int outsideCount() {
    int n = 0;
    for (const NetPlayer& p : s_players) n += p.used && p.where == W_RAID && !p.inCrypt ? 1 : 0;
    return n;
}

bool anyGuestOutside() {
    for (int i = 1; i < MAX_PLAYERS; i++)
        if (s_players[i].used && s_players[i].where == W_RAID) return true;
    return false;
}

// ---------------------------------------------------------------- sending
void sendReliable(int slot, const Net::Writer& w) {
    if (slot < 0 || slot >= MAX_PLAYERS || !s_players[slot].used || slot == s_local) return;
    if (local()) {
        if (!w.b.empty() && personalMsg(w.b[0])) s_localInbox[slot].push_back(w.b);
        return;
    }
    Net::send(s_players[slot].id, w.b, true);
}
void sendUnreliable(int slot, const Net::Writer& w) {
    if (local()) { sendReliable(slot, w); return; }
    if (slot >= 0 && slot < MAX_PLAYERS && s_players[slot].used && slot != s_local) Net::send(s_players[slot].id, w.b, false);
}
void toHost(const Net::Writer& w, bool reliable) {
    if (guest()) Net::send(Net::hostId(), w.b, reliable);
}
void broadcast(const Net::Writer& w, bool reliable, uint8_t onlyWhere) {
    if (!host() || local()) return;   // local co-op: everybody is watching this screen already
    for (int i = 1; i < MAX_PLAYERS; i++) {
        if (!s_players[i].used) continue;
        if (onlyWhere != W_NONE && s_players[i].where != onlyWhere) continue;
        Net::send(s_players[i].id, w.b, reliable);
    }
}

void headline(const std::string& text, int color) {
    raid_bigText(text, color);
    pushMessage(text, color);
    if (!host()) return;
    Net::Writer w;
    w.u8(M_TEXT);
    w.str(text);
    w.u8((uint8_t)color);
    broadcast(w, true);
}

void sendProfile() {
    if (!guest() || !s_welcomed) return;
    std::string text = profileToText(G.prof);
    if (text == s_lastProfileSent) return;
    s_lastProfileSent = text;
    Net::Writer w;
    w.u8(M_PROFILE);
    w.str(text);
    toHost(w, true);
}

void saveGuests() {
    if (!host() || G.devNoSave || G.coopNoSave) return;
    ensureDir(coopGuestDir());
    for (int i = 1; i < MAX_PLAYERS; i++) {
        const NetPlayer& pl = s_players[i];
        if (!pl.used || !pl.prof) continue;
        std::ofstream f(guestPath(pl.id));
        f << profileToText(*pl.prof);
    }
}

// ---------------------------------------------------------------- shared stash
// Guest: sends the shared stash to the host if it changed since the last time.
static void flushStash() {
    if (s_stashAsk != StashAsk::Granted) return;
    bool changed = s_stashMirror.size() != s_stashSent.size();
    for (size_t i = 0; !changed && i < s_stashMirror.size(); i++) {
        const Item &a = s_stashMirror[i], &b = s_stashSent[i];
        changed = a.id != b.id || a.count != b.count || a.data != b.data;
    }
    if (!changed) return;
    Net::Writer w;
    w.u8(M_STASH_SET);
    writeStashItems(w, s_stashMirror);
    toHost(w, true);
    s_stashSent = s_stashMirror;
    sendProfile();   // what we took is in our pockets now: tell the host too
}

void stashOpen() {
    if (G.prof.rivals || local()) return;
    if (host()) {
        if (s_stashLock < 0) s_stashLock = 0;
        return;
    }
    if (!guest()) return;
    s_stashAsk = StashAsk::Waiting;
    Net::Writer w;
    w.u8(M_STASH_OPEN);
    toHost(w, true);
}

void stashClose() {
    if (local()) return;
    if (host()) {
        if (s_stashLock == 0) s_stashLock = -1;
        return;
    }
    if (!guest() || s_stashAsk == StashAsk::None) return;
    flushStash();
    Net::Writer w;
    w.u8(M_STASH_CLOSE);
    toHost(w, true);
    s_stashAsk = StashAsk::None;
    s_stashMirror.clear();
    s_stashSent.clear();
    sendProfile();
}

std::vector<Item>* sharedStash() {
    if (!active() || G.prof.rivals) return &G.prof.stash;   // Rivals: your stash is yours
    if (local()) return &Local::hostProfile().stash;         // one menu at a time: no lock needed
    if (host()) return s_stashLock == 0 ? &G.prof.stash : nullptr;
    return s_stashAsk == StashAsk::Granted ? &s_stashMirror : nullptr;
}

std::string stashStatus() {
    if (local()) return "";
    if (host()) {
        if (s_stashLock > 0) return T1("{0} is using the shared stash.", s_players[s_stashLock].name);
        return "";
    }
    if (s_stashAsk == StashAsk::Denied) return T1("{0} is using the shared stash.", s_stashHolder);
    return T("Opening the shared stash...");
}

// ---------------------------------------------------------------- session
static void resetPlayers() {
    for (NetPlayer& p : s_players) p = NetPlayer();
    s_welcomed = false;
    s_localInBed = false;
    s_lastProfileSent.clear();
    s_stashLock = -1;
    s_stashAsk = StashAsk::None;
    s_stashMirror.clear();
    s_stashSent.clear();
}

// ---------------------------------------------------------------- identity
static std::string s_idName;
static int s_idShirt = 0;
static bool s_idLoaded = false;

static void loadIdentity() {
    if (s_idLoaded) return;
    s_idLoaded = true;
    std::ifstream f(dataPath("saves/identity.txt"));
    std::string rest;
    if (f >> s_idShirt) { std::getline(f, rest); s_idName = cleanCharName(rest); }
    s_idShirt = std::clamp(s_idShirt, 0, Assets::SHIRT_COUNT - 1);
}

std::string localName() {
    if (host()) return G.prof.charName.empty() ? Net::myName() : G.prof.charName;
    loadIdentity();
    return s_idName.empty() ? Net::myName() : s_idName;
}
int localShirt() {
    if (host()) return G.prof.shirt;
    loadIdentity();
    return s_idShirt;
}


void setIdentity(const std::string& name, int shirt) {
    loadIdentity();
    s_idName = cleanCharName(name);
    s_idShirt = std::clamp(shirt, 0, Assets::SHIRT_COUNT - 1);
    ensureDir(dataPath("saves"));
    {
        std::ofstream f(dataPath("saves/identity.txt"));
        if (f) f << s_idShirt << ' ' << s_idName << "\n";
    }
    persistSaves();
    if (host()) {
        // The host's own character is the one in the save.
        G.prof.charName = s_idName;
        G.prof.shirt = s_idShirt;
        s_players[0].name = localName();
        s_players[0].shirt = s_idShirt;
        sendRoster();
    } else if (guest() && s_welcomed) {
        G.prof.charName = s_idName;
        G.prof.shirt = s_idShirt;
        Net::Writer w;
        w.u8(M_IDENT);
        w.str(localName());
        w.u8((uint8_t)s_idShirt);
        toHost(w, true);
        sendProfile();
    }
}

static void takeHostSeat() {
    resetPlayers();
    s_mode = Mode::Host;
    s_local = 0;
    NetPlayer& me = s_players[0];
    me.used = true;
    me.id = Net::myId();
    me.prof = &G.prof;
    me.name = localName();
    me.shirt = G.prof.shirt;
    me.where = W_LOBBY;
    s_guestProfiles.clear();
    s_leaveReason.clear();
}

void beginHost() {
    takeHostSeat();
    Net::hostLobby();
    G.scene = Scene::Lobby;
}

void beginDevHost(int port) {
    if (!Net::devHost(port)) { s_leaveReason = Net::lastError(); return; }
    takeHostSeat();
    G.scene = Scene::Lobby;
}

static void beginGuest() {
    resetPlayers();
    s_mode = Mode::Guest;
    s_local = -1;
    s_helloT = 0;
    s_leaveReason.clear();
    G.scene = Scene::Lobby;
}

void beginDevJoin(int port) {
    if (!Net::devJoin("127.0.0.1", port)) { s_leaveReason = Net::lastError(); return; }
    beginGuest();
}

void startGame() {
    if (!host()) return;
    Net::setLobbyStarted();
    Net::Writer w;
    w.u8(M_START);
    broadcast(w, true);
    base_enter(false);
}

void leave(const std::string& reason) {
    if (s_mode == Mode::Off) { s_leaveReason = reason; return; }
    if (local()) { Local::end(); return; }
    Net::Writer w;
    w.u8(M_BYE);
    w.str(host() ? "The host ended the session." : "");
    if (host()) {
        save_game();          // the host's save holds everyone
        broadcast(w, true);
    } else {
        sendProfile();
        toHost(w, true);
        // The guest's character belongs to the host's save: nothing here writes it.
        G.coopNoSave = true;
    }
    Net::update();
    Net::leaveLobby();
    bool wasHost = host();
    s_mode = Mode::Off;
    resetPlayers();
    s_local = 0;
    s_leaveReason = reason;
    raid_coopEnded();
    if (wasHost) G.coopNoSave = false;
    G.panel = Panel::None;
    G.scene = Scene::Menu;
    menu_init();
}

// ---------------------------------------------------------------- per frame
void update(float dt) {
    // Local co-op has no wire: seats get their messages on their turn (takeLocalInbox),
    // and the group goes everywhere together, so none of the session upkeep applies.
    if (local()) { Net::events().clear(); Net::inbox().clear(); return; }
    // Invites accepted while not in a session: joining puts us in the guest seat.
    for (const Net::Event& e : Net::events()) {
        if (s_mode == Mode::Off) {
            if (e.kind == Net::Event::InviteAccepted && G.scene == Scene::Base) save_game();
            if (e.kind == Net::Event::Entered && !Net::isHost()) beginGuest();
            if (e.kind == Net::Event::Failed) s_leaveReason = Net::lastError();
            continue;
        }
        if (host() && e.kind == Net::Event::Left) removePlayer(e.id, "");
        if (guest() && e.kind == Net::Event::Left && e.id == Net::hostId()) { Net::events().clear(); leave(T("The host left the game.")); return; }
        if (guest() && e.kind == Net::Event::Entered) s_helloT = 0;
        if (e.kind == Net::Event::Failed) { Net::events().clear(); leave(Net::lastError()); return; }
    }
    Net::events().clear();
    if (s_mode == Mode::Off) { Net::inbox().clear(); return; }

    // ---- messages
    std::vector<Net::Incoming> inbox;
    inbox.swap(Net::inbox());
    for (const Net::Incoming& m : inbox) {
        if (m.data.empty()) continue;
        uint8_t type = m.data[0];
        Net::Reader r(m.data.data() + 1, m.data.size() - 1);
        if (host()) {
            if (type == M_HELLO) { onHello(m.from, r); continue; }
            int slot = slotOf(m.from);
            if (slot <= 0) continue;
            switch (type) {
            case M_PSTATE: readState(r, slot); break;
            case M_PROFILE: onProfile(slot, r); break;
            case M_BYE: removePlayer(m.from, ""); break;
            case M_STASH_OPEN: onStashOpen(slot); break;
            case M_STASH_SET: onStashSet(slot, r); break;
            case M_STASH_CLOSE: if (s_stashLock == slot) s_stashLock = -1; break;
            case M_DEF_OP: onDefenseOp(slot, r); break;
            case M_VOICE: Voice::onPacket(slot, m.data.data(), m.data.size()); break;
            case M_IDENT: {
                std::string nm = cleanCharName(r.str());
                int sh = std::clamp((int)r.u8(), 0, Assets::SHIRT_COUNT - 1);
                if (r.bad) break;
                NetPlayer& pl = s_players[slot];
                pl.name = nm.empty() ? Net::nameOf(pl.id) : nm;
                pl.shirt = sh;
                if (pl.prof) { pl.prof->charName = nm; pl.prof->shirt = sh; }
                sendRoster();
                break;
            }
            default: raid_netMessage(slot, type, r); break;
            }
        } else {
            if (m.from != Net::hostId() && !Net::devMode()) continue;
            switch (type) {
            case M_WELCOME: {
                int slot = r.u8();
                bool started = r.u8() != 0;
                std::string text = r.str();
                Profile p;
                if (r.bad || !profileFromText(text, p)) break;
                G.prof = p;
                readWorld(r);
                s_local = slot;
                s_welcomed = true;
                s_lastProfileSent = text;
                if (started && G.scene == Scene::Lobby) base_enter(false);
                break;
            }
            case M_ROSTER:
                for (int i = 0; i < MAX_PLAYERS; i++) {
                    bool used = r.u8() != 0;
                    uint64_t id = r.u64();
                    std::string name = r.str();
                    int shirt = r.u8();
                    if (r.bad) break;
                    NetPlayer& pl = s_players[i];
                    if (!used) { pl = NetPlayer(); continue; }
                    if (!pl.used || pl.id != id) { pl = NetPlayer(); pl.used = true; pl.id = id; }
                    pl.name = name;
                    pl.shirt = std::clamp(shirt, 0, Assets::SHIRT_COUNT - 1);
                }
                break;
            case M_START:
                if (G.scene == Scene::Lobby && s_welcomed) base_enter(false);
                break;
            case M_PLAYERS: {
                int n = r.u8();
                for (int i = 0; i < n && !r.bad; i++) readState(r, -1);
                break;
            }
            case M_WORLD: readWorld(r); break;
            case M_VOICE: Voice::onPacket(-1, m.data.data(), m.data.size()); break;
            case M_NEW_DAY: applyNewDay(r); break;
            case M_STASH: {
                bool granted = r.u8() != 0;
                if (granted) {
                    std::vector<Item> v = readStashItems(r);
                    if (r.bad || s_stashAsk != StashAsk::Waiting) break;
                    s_stashMirror = v;
                    s_stashSent = v;
                    s_stashAsk = StashAsk::Granted;
                } else {
                    std::string who = r.str();
                    if (s_stashAsk != StashAsk::Waiting) break;
                    s_stashHolder = who;
                    s_stashAsk = StashAsk::Denied;
                    s_stashAskT = 2.0f;        // ask again in a moment
                }
                break;
            }
            case M_REFUND: {
                int cost = r.i32();
                if (r.bad) break;
                G.prof.money += cost;
                setNotice(T("Someone else changed that first. Money returned."));
                break;
            }
            case M_TEXT: {
                std::string t = r.str();
                int col = r.u8();
                if (!r.bad) { raid_bigText(t, col); pushMessage(t, col); }
                break;
            }
            case M_BYE: {
                std::string why = r.str();
                leave(why.empty() ? T("The host ended the session.") : why);
                return;
            }
            default: raid_netMessage(0, type, r); break;
            }
        }
    }
    if (s_mode == Mode::Off) return;

    // ---- the local player, as everyone else will see it
    if (s_local >= 0) {
        NetPlayer& me = s_players[s_local];
        me.used = true;
        me.id = Net::myId();
        if (host()) { me.name = localName(); me.shirt = G.prof.shirt; }
        else if (me.name.empty()) me.name = localName();
        me.where = G.scene == Scene::Raid ? W_RAID : (G.scene == Scene::Base || G.scene == Scene::Defense) ? W_BASE : W_LOBBY;
        me.pos = me.target = G.player.pos;
        me.angle = G.player.angle;
        const Profile& p = G.prof;
        me.weapon = p.weapons[p.curWeapon].id;
        me.moving = G.player.moving;
        me.reloading = G.player.reloadT > 0;
        me.downed = raid_localDowned(me.downT);
        me.inBed = s_localInBed && me.where == W_BASE;
        me.laser = p.laserActive();
        me.inCrypt = me.where == W_RAID && raid_localCrypt() >= 0;
        me.ride = me.where == W_RAID ? raid_localRide() : -1;
        me.flashT = G.player.flashT;
        me.hurtT = G.player.hurtT;
        me.hp = p.hp;
        me.maxHp = p.maxHp();
    }
    for (int i = 0; i < MAX_PLAYERS; i++) {
        NetPlayer& pl = s_players[i];
        if (!pl.used || i == s_local) continue;
        pl.heardT += dt;
        pl.lastPos = pl.pos;
        pl.pos = pl.pos + (pl.target - pl.pos) * std::min(1.0f, dt * 14.0f);
        pl.flashT -= dt;
        pl.hurtT -= dt;
    }

    // ---- periodic sends
    s_stateT -= dt;
    if (s_stateT <= 0) {
        s_stateT = 0.05f;
        if (guest() && s_welcomed) {
            Net::Writer w;
            w.u8(M_PSTATE);
            writeState(w, s_local);
            toHost(w, false);
        } else if (host()) {
            Net::Writer w;
            w.u8(M_PLAYERS);
            w.u8((uint8_t)count());
            for (int i = 0; i < MAX_PLAYERS; i++)
                if (s_players[i].used) writeState(w, i);
            broadcast(w, false);
        }
    }
    if (guest() && !s_welcomed && Net::lobbyState() == Net::LobbyState::In) {
        s_helloT -= dt;
        if (s_helloT <= 0) {
            s_helloT = 1.5f;
            Net::Writer w;
            w.u8(M_HELLO);
            w.u32(PROTOCOL);
            w.str(localName());
            w.u8((uint8_t)localShirt());
            toHost(w, true);
        }
    }
    if (host()) {
        s_worldT -= dt;
        if (s_worldT <= 0) {
            s_worldT = 0.25f;
            Net::Writer w;
            w.u8(M_WORLD);
            writeWorld(w);
            broadcast(w, false);
        }
    }
    if (guest()) {
        s_profileT -= dt;
        if (s_profileT <= 0) { s_profileT = 8; sendProfile(); }
    }

    bool inGame = G.scene == Scene::Base || G.scene == Scene::Raid || G.scene == Scene::Defense;
    if (!inGame) return;

    // ---- the clock runs while anyone is outside; with everybody in the bunker it
    // stands still, as it does in single player
    // Hardcore: it never stands still, bunker or not.
    if (G.scene != Scene::Raid && (outsideCount() > 0 || G.prof.hardcore())) G.prof.timeMin += GAME_MINUTES_PER_SEC * dt;

    // ---- the shared stash
    if (host()) {
        if (s_stashLock == 0 && (G.panel != Panel::Stash || G.scene != Scene::Base)) s_stashLock = -1;
        if (s_stashLock > 0) {
            const NetPlayer& h = s_players[s_stashLock];
            if (!h.used || h.where != W_BASE) s_stashLock = -1;
        }
    } else if (s_stashAsk != StashAsk::None) {
        if (G.panel != Panel::Stash || G.scene != Scene::Base) stashClose();
        else if (s_stashAsk == StashAsk::Granted) flushStash();
        else if (s_stashAsk == StashAsk::Denied) {
            s_stashAskT -= dt;
            if (s_stashAskT <= 0) stashOpen();
        }
    }

    if (host()) {
        // Someone is out there while we are not: the world still has to run for them.
        if (G.scene != Scene::Raid && anyGuestOutside()) raid_coopTick(dt);
        // A horde due (or on) sends everyone in the bunker up the ladder: see
        // base_hordeCall. Nobody sleeps through one either: with everybody in bed and a
        // horde due before morning, the night passes only until they come.
        bool allInBed = true;
        for (const NetPlayer& pl : s_players)
            if (pl.used && !(pl.where == W_BASE && pl.inBed)) allInBed = false;
        bool nightOver = G.prof.timeMin >= 1440 + DAY_START_MIN;
        bool hordeActive;
        int hordeN, hordeLeft;
        raid_hordeState(hordeActive, hordeN, hordeLeft);
        bool hordeOn = hordeActive || absMinutes() >= G.prof.nextHordeAt;
        if (!hordeOn && allInBed && hordeBeforeMorning()) {
            G.prof.timeMin = std::max(G.prof.timeMin, G.prof.nextHordeAt - (G.prof.day - 1) * 1440.0f);
            for (NetPlayer& pl : s_players) pl.inBed = false;
            s_localInBed = false;
            headline(T("THE HORDE WAKES YOU - EVERYONE OUTSIDE!"), P_CORAL);
            Net::Writer ww;
            ww.u8(M_WORLD);
            writeWorld(ww);
            broadcast(ww, true);
        } else if (!hordeOn && (allInBed || nightOver)) {
            std::string note = base_coopSleep(nightOver && !allInBed);
            Net::Writer w;
            w.u8(M_NEW_DAY);
            w.u32((uint32_t)G.prof.day);
            w.u8(nightOver && !allInBed ? 1 : 0);
            w.str(note);
            broadcast(w, true);
            Net::Writer ww;
            ww.u8(M_WORLD);
            writeWorld(ww);
            broadcast(ww, true);
        }
    }
    raid_netSend(dt);
}

// ---------------------------------------------------------------- local co-op (0.12v)
void beginLocal() {
    resetPlayers();
    s_mode = Mode::Local;
    s_local = 0;
    s_guestProfiles.clear();
    s_leaveReason.clear();
    for (auto& box : s_localInbox) box.clear();
    NetPlayer& me = s_players[0];
    me.used = true;
    me.id = LOCAL_ID;
    me.prof = &G.prof;
    me.name = G.prof.charName.empty() ? T1("Player {0}", "1") : G.prof.charName;
    me.shirt = G.prof.shirt;
    me.where = G.scene == Scene::Raid ? W_RAID : W_BASE;
}

Profile* addLocalSeat(int slot) {
    if (!local() || slot <= 0 || slot >= MAX_PLAYERS) return nullptr;
    NetPlayer& pl = s_players[slot];
    pl = NetPlayer();
    pl.used = true;
    pl.id = LOCAL_ID + (uint64_t)slot;
    pl.prof = guestProfile(pl.id);
    if (pl.prof->charName.empty()) {
        // A new character: a shirt nobody else in the room is wearing.
        pl.prof->charName = T1("Player {0}", std::to_string(slot + 1));
        int shirt = (slot * 3) % Assets::SHIRT_COUNT;
        for (int tries = 0; tries < Assets::SHIRT_COUNT; tries++) {
            bool taken = false;
            for (int i = 0; i < MAX_PLAYERS; i++)
                if (i != slot && s_players[i].used && s_players[i].shirt == shirt) taken = true;
            if (!taken) break;
            shirt = (shirt + 1) % Assets::SHIRT_COUNT;
        }
        pl.prof->shirt = shirt;
    }
    pl.name = pl.prof->charName;
    pl.shirt = std::clamp(pl.prof->shirt, 0, Assets::SHIRT_COUNT - 1);
    pl.where = G.scene == Scene::Raid ? W_RAID : W_BASE;
    raid_coopPlayersChanged();
    return pl.prof;
}

void removeLocalSeat(int slot) {
    if (!local() || slot <= 0 || slot >= MAX_PLAYERS || !s_players[slot].used) return;
    NetPlayer& pl = s_players[slot];
    if (pl.prof && !G.devNoSave && !G.coopNoSave) {
        ensureDir(coopGuestDir());
        std::ofstream f(guestPath(pl.id));
        f << profileToText(*pl.prof);
    }
    pl = NetPlayer();
    s_localInbox[slot].clear();
    raid_coopPlayersChanged();
}

void endLocal() {
    if (!local()) return;
    saveGuests();
    s_mode = Mode::Off;
    resetPlayers();
    s_local = 0;
    for (auto& box : s_localInbox) box.clear();
    s_guestProfiles.clear();
}

void setLocalSlot(int slot) { s_local = std::clamp(slot, 0, MAX_PLAYERS - 1); }
void swapLocalSeat(bool& inBed) { std::swap(s_localInBed, inBed); }

std::vector<std::vector<uint8_t>> takeLocalInbox(int slot) {
    std::vector<std::vector<uint8_t>> out;
    if (slot >= 0 && slot < MAX_PLAYERS) out.swap(s_localInbox[slot]);
    return out;
}

}  // namespace Coop
