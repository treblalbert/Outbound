#include "net.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>

#if !defined(__EMSCRIPTEN__)
#define OUTBOUND_STEAM 1
#include "steam/steam_api.h"
#include "steam/isteamnetworkingmessages.h"
#include "steam/isteamnetworkingutils.h"
#include "steam/steam_api_flat.h"
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif
#endif

namespace Net {

namespace {
std::vector<Incoming> s_inbox;
std::vector<Event> s_events;
LobbyState s_state = LobbyState::None;
std::string s_error;
bool s_steam = false;
bool s_dev = false;
bool s_devHost = false;
bool s_started = false;
uint64_t s_devMyId = 0;
std::vector<uint64_t> s_devMembers;

// Channel 0 carries everything; reliability is per message.
constexpr int CHANNEL = 0;
constexpr int MAX_MEMBERS = 8;

#ifdef OUTBOUND_STEAM
// Steam interface methods that return a CSteamID use MSVC's convention for returning
// structs, which MinGW gets wrong; the flat C API returns plain 64-bit ids instead.
uint64_t steamSelf() { return SteamAPI_ISteamUser_GetSteamID(SteamUser()); }
uint64_t lobbyOwner(CSteamID lobby) { return SteamAPI_ISteamMatchmaking_GetLobbyOwner(SteamMatchmaking(), lobby.ConvertToUint64()); }
uint64_t lobbyMember(CSteamID lobby, int i) { return SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex(SteamMatchmaking(), lobby.ConvertToUint64(), i); }
CSteamID s_lobby;
uint64_t s_pendingJoin = 0;

// ---------------------------------------------------------------- dev TCP
// Frames are [u32 length][u8 reliable][payload]. Localhost does not drop, so the
// reliable flag is only carried for symmetry.
struct Conn {
    SOCKET s = INVALID_SOCKET;
    uint64_t id = 0;
    std::vector<uint8_t> in;
};
SOCKET s_listen = INVALID_SOCKET;
std::vector<Conn> s_conns;
bool s_wsa = false;

bool wsaUp() {
    if (s_wsa) return true;
    WSADATA d;
    s_wsa = WSAStartup(MAKEWORD(2, 2), &d) == 0;
    return s_wsa;
}

void setNonBlocking(SOCKET s) {
    u_long on = 1;
    ioctlsocket(s, FIONBIO, &on);
    int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof one);
}

void devSendRaw(Conn& c, const uint8_t* data, int size, bool reliable) {
    std::vector<uint8_t> f(5 + size);
    uint32_t len = (uint32_t)size + 1;
    std::memcpy(f.data(), &len, 4);
    f[4] = reliable ? 1 : 0;
    if (size) std::memcpy(f.data() + 5, data, size);
    size_t off = 0;
    while (off < f.size()) {
        int r = ::send(c.s, (const char*)f.data() + off, (int)(f.size() - off), 0);
        if (r > 0) { off += r; continue; }
        if (WSAGetLastError() == WSAEWOULDBLOCK) { Sleep(0); continue; }
        break;
    }
}

void devPoll() {
    if (s_devHost && s_listen != INVALID_SOCKET) {
        for (;;) {
            SOCKET a = accept(s_listen, nullptr, nullptr);
            if (a == INVALID_SOCKET) break;
            setNonBlocking(a);
            Conn c;
            c.s = a;
            // Ids are handed out by the host; the first message tells the guest its own.
            static uint64_t next = 1001;
            c.id = next++;
            s_conns.push_back(c);
            s_devMembers.push_back(c.id);
            uint8_t hello[9] = {0xFE};
            std::memcpy(hello + 1, &c.id, 8);
            devSendRaw(s_conns.back(), hello, 9, true);
            s_events.push_back({Event::Joined, c.id});
        }
    }
    for (size_t k = 0; k < s_conns.size(); k++) {
        Conn& c = s_conns[k];
        char buf[65536];
        bool closed = false;
        for (;;) {
            int r = recv(c.s, buf, sizeof buf, 0);
            if (r > 0) { c.in.insert(c.in.end(), buf, buf + r); continue; }
            if (r == 0 || WSAGetLastError() != WSAEWOULDBLOCK) closed = true;
            break;
        }
        size_t off = 0;
        while (c.in.size() - off >= 4) {
            uint32_t len;
            std::memcpy(&len, c.in.data() + off, 4);
            if (c.in.size() - off < 4 + len) break;
            const uint8_t* body = c.in.data() + off + 5;
            int bodyLen = (int)len - 1;
            if (!s_devHost && bodyLen == 9 && body[0] == 0xFE) {
                std::memcpy(&s_devMyId, body + 1, 8);
                s_devMembers = {1, s_devMyId};
                s_state = LobbyState::In;
                s_events.push_back({Event::Entered, s_devMyId});
            } else {
                s_inbox.push_back({c.id, std::vector<uint8_t>(body, body + bodyLen)});
            }
            off += 4 + len;
        }
        c.in.erase(c.in.begin(), c.in.begin() + off);
        if (closed) {
            closesocket(c.s);
            uint64_t id = c.id;
            s_conns.erase(s_conns.begin() + k);
            k--;
            s_devMembers.erase(std::remove(s_devMembers.begin(), s_devMembers.end(), id), s_devMembers.end());
            s_events.push_back({Event::Left, id});
            if (!s_devHost) s_state = LobbyState::None;
        }
    }
}

// ---------------------------------------------------------------- Steam callbacks
struct Callbacks {
    STEAM_CALLBACK(Callbacks, onJoinRequested, GameLobbyJoinRequested_t);
    STEAM_CALLBACK(Callbacks, onPresenceJoin, GameRichPresenceJoinRequested_t);
    STEAM_CALLBACK(Callbacks, onLobbyEnter, LobbyEnter_t);
    STEAM_CALLBACK(Callbacks, onChatUpdate, LobbyChatUpdate_t);
    STEAM_CALLBACK(Callbacks, onSessionRequest, SteamNetworkingMessagesSessionRequest_t);
    STEAM_CALLBACK(Callbacks, onSessionFailed, SteamNetworkingMessagesSessionFailed_t);
    void onLobbyCreated(LobbyCreated_t* r, bool ioFailure);
    CCallResult<Callbacks, LobbyCreated_t> created;
};
Callbacks* s_cb = nullptr;

// Lets friends use "Join Game" from their friends list: Steam launches the game (or
// hands a running one) +connect_lobby <id>.
void advertise() {
    std::string connect = "+connect_lobby " + std::to_string((unsigned long long)s_lobby.ConvertToUint64());
    SteamFriends()->SetRichPresence("connect", connect.c_str());
    SteamFriends()->SetRichPresence("status", "Playing co-op");
}

void Callbacks::onJoinRequested(GameLobbyJoinRequested_t* r) {
    s_events.push_back({Event::InviteAccepted, r->m_steamIDLobby.ConvertToUint64()});
    joinLobby(r->m_steamIDLobby.ConvertToUint64());
}

void Callbacks::onPresenceJoin(GameRichPresenceJoinRequested_t* r) {
    std::string c = r->m_rgchConnect;
    size_t at = c.find("+connect_lobby ");
    if (at == std::string::npos) return;
    uint64_t id = std::strtoull(c.c_str() + at + 15, nullptr, 10);
    s_events.push_back({Event::InviteAccepted, id});
    joinLobby(id);
}

void Callbacks::onLobbyEnter(LobbyEnter_t* r) {
    if (r->m_EChatRoomEnterResponse != k_EChatRoomEnterResponseSuccess) {
        s_state = LobbyState::Failed;
        s_error = "Could not join that lobby (it may be full or closed).";
        s_events.push_back({Event::Failed, 0});
        return;
    }
    s_lobby = CSteamID(r->m_ulSteamIDLobby);
    s_state = LobbyState::In;
    advertise();
    const char* started = SteamMatchmaking()->GetLobbyData(s_lobby, "started");
    s_started = started && started[0] == '1';
    s_events.push_back({Event::Entered, myId()});
}

void Callbacks::onChatUpdate(LobbyChatUpdate_t* r) {
    if (CSteamID(r->m_ulSteamIDLobby) != s_lobby) return;
    uint64_t who = r->m_ulSteamIDUserChanged;
    if (r->m_rgfChatMemberStateChange & k_EChatMemberStateChangeEntered) s_events.push_back({Event::Joined, who});
    else s_events.push_back({Event::Left, who});
}

void Callbacks::onSessionRequest(SteamNetworkingMessagesSessionRequest_t* r) {
    // Only talk to people who are in our lobby.
    uint64_t who = r->m_identityRemote.GetSteamID64();
    std::vector<uint64_t> m = members();
    if (std::find(m.begin(), m.end(), who) != m.end())
        SteamNetworkingMessages()->AcceptSessionWithUser(r->m_identityRemote);
}

void Callbacks::onSessionFailed(SteamNetworkingMessagesSessionFailed_t* r) {
    std::fprintf(stderr, "[net] session failed: %s\n", r->m_info.m_szEndDebug);
}

void Callbacks::onLobbyCreated(LobbyCreated_t* r, bool ioFailure) {
    if (ioFailure || r->m_eResult != k_EResultOK) {
        s_state = LobbyState::Failed;
        s_error = "Steam could not create a lobby.";
        s_events.push_back({Event::Failed, 0});
        return;
    }
    s_lobby = CSteamID(r->m_ulSteamIDLobby);
    s_state = LobbyState::In;
    SteamMatchmaking()->SetLobbyData(s_lobby, "game", "outbound");
    SteamMatchmaking()->SetLobbyData(s_lobby, "started", "0");
    s_started = false;
    advertise();
    s_events.push_back({Event::Entered, myId()});
}
#endif
}  // namespace

// Steam is started only when online co-op needs it (0.12v). Once the game tells Steam
// it is running (as app 480, Spacewar, until it has an id of its own), Steam applies
// that app's Steam Input setup, which takes the controllers over for its own input
// API: the game stopped seeing an Xbox pad at all. So the menus and local play never
// start Steam; opening Online co-op (or a Steam invite on the command line) does.
static bool s_steamTried = false;
static void startSteam() {
#ifdef OUTBOUND_STEAM
    if (s_steamTried) return;
    s_steamTried = true;
    s_steam = SteamAPI_Init();
    std::fprintf(stderr, "[net] steam %s\n", s_steam ? "ready" : "unavailable");
    if (!s_steam) return;
    s_cb = new Callbacks();
    SteamNetworkingUtils()->InitRelayNetworkAccess();
#endif
}

void init(int argc, char** argv) {
#ifdef OUTBOUND_STEAM
    for (int i = 1; i + 1 < argc; i++)
        if (std::string(argv[i]) == "+connect_lobby") s_pendingJoin = std::strtoull(argv[i + 1], nullptr, 10);
    if (s_pendingJoin) startSteam();   // launched from a Steam invite
#else
    (void)argc; (void)argv;
#endif
}

void ensureSteam() { startSteam(); }

void shutdown() {
#ifdef OUTBOUND_STEAM
    leaveLobby();
    if (s_steam) { delete s_cb; s_cb = nullptr; SteamAPI_Shutdown(); }
    if (s_wsa) WSACleanup();
#endif
}

void update() {
#ifdef OUTBOUND_STEAM
    if (s_dev) { devPoll(); return; }
    if (!s_steam) return;
    SteamAPI_RunCallbacks();
    if (s_pendingJoin) { uint64_t id = s_pendingJoin; s_pendingJoin = 0; joinLobby(id); }
    SteamNetworkingMessage_t* msgs[64];
    for (;;) {
        int n = SteamNetworkingMessages()->ReceiveMessagesOnChannel(CHANNEL, msgs, 64);
        if (n <= 0) break;
        for (int i = 0; i < n; i++) {
            const uint8_t* d = (const uint8_t*)msgs[i]->m_pData;
            s_inbox.push_back({msgs[i]->m_identityPeer.GetSteamID64(), std::vector<uint8_t>(d, d + msgs[i]->m_cbSize)});
            msgs[i]->Release();
        }
        if (n < 64) break;
    }
#endif
}

bool steamAvailable() { return s_steam; }
bool devMode() { return s_dev; }

uint64_t myId() {
    if (s_dev) return s_devHost ? 1 : s_devMyId;
#ifdef OUTBOUND_STEAM
    if (s_steam) return steamSelf();
#endif
    return 1;
}

std::string myName() {
#ifdef OUTBOUND_STEAM
    if (s_steam && !s_dev) return SteamFriends()->GetPersonaName();
#endif
    return s_devHost || !s_dev ? "Host" : "Guest " + std::to_string(s_devMyId - 1000);
}

std::string nameOf(uint64_t id) {
    if (id == myId()) return myName();
#ifdef OUTBOUND_STEAM
    if (s_steam && !s_dev) return SteamFriends()->GetFriendPersonaName(CSteamID(id));
#endif
    return id == 1 ? "Host" : "Guest " + std::to_string((long long)id - 1000);
}

LobbyState lobbyState() { return s_state; }
std::string lastError() { return s_error; }

bool isHost() {
    if (s_dev) return s_devHost;
#ifdef OUTBOUND_STEAM
    if (s_steam && s_state == LobbyState::In) return lobbyOwner(s_lobby) == steamSelf();
#endif
    return false;
}

uint64_t hostId() {
    if (s_dev) return 1;
#ifdef OUTBOUND_STEAM
    if (s_steam && s_state == LobbyState::In) return lobbyOwner(s_lobby);
#endif
    return 0;
}

void hostLobby() {
#ifdef OUTBOUND_STEAM
    startSteam();
    if (!s_steam) { s_state = LobbyState::Failed; s_error = "Steam is not running."; return; }
    s_state = LobbyState::Creating;
    SteamAPICall_t call = SteamMatchmaking()->CreateLobby(k_ELobbyTypeFriendsOnly, MAX_MEMBERS);
    s_cb->created.Set(call, s_cb, &Callbacks::onLobbyCreated);
#endif
}

void joinLobby(uint64_t lobbyId) {
#ifdef OUTBOUND_STEAM
    startSteam();
    if (!s_steam || !lobbyId) return;
    if (s_state == LobbyState::In) leaveLobby();
    s_state = LobbyState::Joining;
    SteamMatchmaking()->JoinLobby(CSteamID(lobbyId));
#else
    (void)lobbyId;
#endif
}

void leaveLobby() {
#ifdef OUTBOUND_STEAM
    if (s_dev) {
        for (Conn& c : s_conns) closesocket(c.s);
        s_conns.clear();
        if (s_listen != INVALID_SOCKET) closesocket(s_listen);
        s_listen = INVALID_SOCKET;
        s_devMembers.clear();
        s_dev = false;
        s_state = LobbyState::None;
        return;
    }
    if (s_steam && s_state == LobbyState::In) {
        for (uint64_t id : members()) {
            if (id == myId()) continue;
            SteamNetworkingIdentity ident;
            ident.SetSteamID64(id);
            SteamNetworkingMessages()->CloseSessionWithUser(ident);
        }
        SteamMatchmaking()->LeaveLobby(s_lobby);
        SteamFriends()->ClearRichPresence();
    }
    s_lobby = CSteamID();
#endif
    s_state = LobbyState::None;
    s_started = false;
}

void openInviteOverlay() {
#ifdef OUTBOUND_STEAM
    if (s_steam && s_state == LobbyState::In && !s_dev) SteamFriends()->ActivateGameOverlayInviteDialog(s_lobby);
#endif
}

std::vector<uint64_t> members() {
    if (s_dev) return s_devHost ? ([] { std::vector<uint64_t> v{1}; v.insert(v.end(), s_devMembers.begin(), s_devMembers.end()); return v; })() : s_devMembers;
    std::vector<uint64_t> out;
#ifdef OUTBOUND_STEAM
    if (!s_steam || s_state != LobbyState::In) return out;
    uint64_t owner = lobbyOwner(s_lobby);
    out.push_back(owner);
    int n = SteamMatchmaking()->GetNumLobbyMembers(s_lobby);
    for (int i = 0; i < n; i++) {
        uint64_t m = lobbyMember(s_lobby, i);
        if (m != owner) out.push_back(m);
    }
#endif
    return out;
}

void setLobbyStarted() {
    s_started = true;
#ifdef OUTBOUND_STEAM
    if (s_steam && !s_dev && s_state == LobbyState::In) SteamMatchmaking()->SetLobbyData(s_lobby, "started", "1");
#endif
}

bool lobbyStarted() { return s_started; }

bool devHost(int port) {
#ifdef OUTBOUND_STEAM
    if (!wsaUp()) return false;
    s_listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons((u_short)port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int one = 1;
    setsockopt(s_listen, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof one);
    if (bind(s_listen, (sockaddr*)&a, sizeof a) != 0 || listen(s_listen, 8) != 0) {
        closesocket(s_listen);
        s_listen = INVALID_SOCKET;
        s_error = "Could not open the dev port.";
        return false;
    }
    u_long on = 1;
    ioctlsocket(s_listen, FIONBIO, &on);
    s_dev = true;
    s_devHost = true;
    s_state = LobbyState::In;
    s_events.push_back({Event::Entered, 1});
    return true;
#else
    (void)port;
    return false;
#endif
}

bool devJoin(const char* host, int port) {
#ifdef OUTBOUND_STEAM
    if (!wsaUp()) return false;
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons((u_short)port);
    inet_pton(AF_INET, host, &a.sin_addr);
    for (int tries = 0; tries < 50; tries++) {
        if (connect(s, (sockaddr*)&a, sizeof a) == 0) {
            setNonBlocking(s);
            Conn c;
            c.s = s;
            c.id = 1;            // the host
            s_conns.push_back(c);
            s_dev = true;
            s_devHost = false;
            s_state = LobbyState::Joining;
            return true;
        }
        Sleep(100);
    }
    closesocket(s);
    s_error = "Could not reach the dev host.";
    return false;
#else
    (void)host; (void)port;
    return false;
#endif
}

void send(uint64_t to, const uint8_t* data, int size, bool reliable) {
#ifdef OUTBOUND_STEAM
    if (s_dev) {
        for (Conn& c : s_conns)
            if (c.id == to) devSendRaw(c, data, size, reliable);
        return;
    }
    if (!s_steam || to == 0 || to == myId()) return;
    SteamNetworkingIdentity ident;
    ident.SetSteamID64(to);
    int flags = reliable ? k_nSteamNetworkingSend_Reliable : (k_nSteamNetworkingSend_Unreliable | k_nSteamNetworkingSend_NoNagle);
    flags |= k_nSteamNetworkingSend_AutoRestartBrokenSession;
    SteamNetworkingMessages()->SendMessageToUser(ident, data, (uint32_t)size, flags, CHANNEL);
#else
    (void)to; (void)data; (void)size; (void)reliable;
#endif
}

std::vector<Incoming>& inbox() { return s_inbox; }
std::vector<Event>& events() { return s_events; }

}  // namespace Net
