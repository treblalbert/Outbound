// Transport and lobby for co-op. Two backends behind one interface:
//   * Steam: a friends-only lobby you invite people to through the overlay, and
//     peer-to-peer messages relayed by Steam (ISteamNetworkingMessages).
//   * Dev: plain TCP on localhost, so two copies of the game on one PC can play
//     together without two Steam accounts (--lan-host / --lan-join).
// Game code only sees peers as 64-bit ids and byte messages; coop.cpp gives them
// meaning.
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace Net {

// Starts Steam if it is running (desktop only). Safe to call when it is not: co-op
// through Steam is then simply unavailable. Handles a +connect_lobby launch argument
// (the game was started by accepting an invite).
void init(int argc, char** argv);
void shutdown();
// Pumps Steam callbacks and incoming messages. Call once per frame.
void update();

// Starts Steam if it has not been (online co-op only: see net.cpp).
void ensureSteam();
bool steamAvailable();
uint64_t myId();
std::string myName();
std::string nameOf(uint64_t id);

// ---- lobby
enum class LobbyState { None, Creating, Joining, In, Failed };
LobbyState lobbyState();
bool isHost();                 // we own the lobby (or are the dev TCP host)
uint64_t hostId();
void hostLobby();              // friends-only lobby, 8 players
void joinLobby(uint64_t lobbyId);
void leaveLobby();
void openInviteOverlay();
bool devMode();
std::vector<uint64_t> members(); // everyone in the lobby, host first
void setLobbyStarted();        // late joiners drop straight into the game
bool lobbyStarted();
std::string lastError();

// Dev transport.
bool devHost(int port);
bool devJoin(const char* host, int port);

// ---- messages
// Reliable messages arrive once and in order; unreliable ones may be dropped and
// are for state that is resent many times a second anyway.
void send(uint64_t to, const uint8_t* data, int size, bool reliable);
inline void send(uint64_t to, const std::vector<uint8_t>& d, bool reliable) { send(to, d.data(), (int)d.size(), reliable); }

struct Incoming { uint64_t from; std::vector<uint8_t> data; };
// Messages received since the last call.
std::vector<Incoming>& inbox();

// Lobby membership changes, drained by coop.cpp.
struct Event { enum Kind { Joined, Left, Entered, Failed, InviteAccepted } kind; uint64_t id; };
std::vector<Event>& events();

// ---- byte buffers
struct Writer {
    std::vector<uint8_t> b;
    void u8(uint8_t v) { b.push_back(v); }
    void u16(uint16_t v) { raw(&v, 2); }
    void i16(int16_t v) { raw(&v, 2); }
    void u32(uint32_t v) { raw(&v, 4); }
    void i32(int32_t v) { raw(&v, 4); }
    void u64(uint64_t v) { raw(&v, 8); }
    void f32(float v) { raw(&v, 4); }
    void str(const std::string& s) { u32((uint32_t)s.size()); raw(s.data(), s.size()); }
    void raw(const void* p, size_t n) { const uint8_t* c = (const uint8_t*)p; b.insert(b.end(), c, c + n); }
};

struct Reader {
    const uint8_t* p;
    size_t n, i = 0;
    bool bad = false;
    Reader(const uint8_t* data, size_t size) : p(data), n(size) {}
    bool take(void* out, size_t k) {
        if (i + k > n) { bad = true; std::memset(out, 0, k); return false; }
        std::memcpy(out, p + i, k);
        i += k;
        return true;
    }
    uint8_t u8() { uint8_t v; take(&v, 1); return v; }
    uint16_t u16() { uint16_t v; take(&v, 2); return v; }
    int16_t i16() { int16_t v; take(&v, 2); return v; }
    uint32_t u32() { uint32_t v; take(&v, 4); return v; }
    int32_t i32() { int32_t v; take(&v, 4); return v; }
    uint64_t u64() { uint64_t v; take(&v, 8); return v; }
    float f32() { float v; take(&v, 4); return v; }
    std::string str() {
        uint32_t k = u32();
        if (bad || i + k > n) { bad = true; return std::string(); }
        std::string s((const char*)p + i, k);
        i += k;
        return s;
    }
    bool done() const { return i >= n; }
};

}  // namespace Net
