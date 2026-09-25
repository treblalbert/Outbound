// Online co-op: up to eight players sharing one host's world.
//
// The host is authoritative. Its game runs the whole outside (raiders, hordes,
// turrets, every player's mercenaries, the clock) and its save holds everything,
// including each guest's character. A guest simulates only their own player - so
// moving and shooting feel immediate - and mirrors the rest from the host:
//   guest -> host : own position/state, shots, hits on enemies, loot changes,
//                   doors, grenades, and the character itself whenever it changes
//   host -> guest : who is where, the clock and horde state, enemy/merc/turret
//                   snapshots, tile and container changes, damage, rewards
// Solo play never touches any of this: every hook checks Coop::active() first.
#pragma once
#include "core.h"
#include "net.h"
#include <string>
#include <vector>

struct Profile;
struct Item;

namespace Coop {

constexpr int MAX_PLAYERS = 8;
constexpr int PROTOCOL = 10;   // 0.11v cars: M_CAR*, ride in the player state, 1/4 px coords

enum Where : uint8_t { W_NONE, W_LOBBY, W_BASE, W_RAID };

// Message types. The first byte of every message.
enum Msg : uint8_t {
    M_HELLO = 1,     // guest -> host: protocol, name
    M_WELCOME,       // host -> guest: slot, started, character, world
    M_ROSTER,        // host -> all: who is in which slot
    M_START,         // host -> all: leave the lobby for the bunker
    M_PSTATE,        // guest -> host: my state
    M_PLAYERS,       // host -> guest: everyone's state
    M_PROFILE,       // guest -> host: my character (save-file text)
    M_WORLD,         // host -> all: clock, horde, bunker and turrets
    M_WORLD_FULL,    // host -> guest: every tile and container, on stepping outside
    M_WANT_FULL,     // guest -> host
    M_TILES,         // host -> guest: tiles that changed
    M_CONTAINERS,    // host -> guest: containers that changed or were created
    M_CONTAINER_SET, // guest -> host: I changed this container
    M_ENEMIES,       // host -> guest: enemies, turrets and mercs near you
    M_ENEMY_DIED,    // host -> guest
    M_SHOTS,         // both ways: cosmetic copies of shots fired
    M_GRENADE,       // both ways
    M_EXPLOSION,     // host -> guest
    M_HIT,           // guest -> host: my bullet hit this enemy
    M_DAMAGE,        // host -> guest: you got hurt
    M_REWARD,        // host -> guest: money / a kill credited to you
    M_MERC_DIED,     // host -> guest: one of your mercenaries is dead
    M_DOOR,          // guest -> host: open/close the door near here
    M_DROP,          // guest -> host: put these items on the ground (bag / body)
    M_REVIVE_REQ,    // any -> host: I revived that player
    M_REVIVE,        // host -> guest: you have been revived
    M_NEW_DAY,       // host -> all: everyone slept (or the night ran out)
    M_TEXT,          // host -> all: a headline / message to show
    M_OVERRUN,       // host -> guest: the bunker fell, lose a quarter
    M_BYE,           // either way: leaving / session over
    M_DEF_OP,        // guest -> host: a change at the defense console (see defense_apply)
    M_REFUND,        // host -> guest: that change could not be made, money back
    M_STASH_OPEN,    // guest -> host: let me use the shared stash
    M_STASH,         // host -> guest: yes (and what is in it) / no, someone else has it
    M_STASH_SET,     // guest -> host: the shared stash as I left it
    M_STASH_CLOSE,   // guest -> host: done with the shared stash
    M_VOICE,         // both ways: a few frames of someone talking (the host passes them on)
    M_IDENT,         // guest -> host: my character's name and shirt changed
    M_PVP_HIT,       // guest -> host: my shot hit that player (Rivals)
    M_PVP_KILL,      // victim -> host: that player killed me (Rivals); host tells everyone
    M_CAR,           // both ways: cars as their owners drive them (the host passes them on)
    M_CAR_DMG,       // host -> owner: your car got hurt
    M_CAR_SMASH,     // guest -> host: my car drove through these tiles
    M_MELEE_TILE,    // guest -> host: my melee hit this tile for this much (0.12v)
};

struct NetPlayer {
    bool used = false;
    uint64_t id = 0;
    std::string name;
    int shirt = 0;                // their shirt colour (Assets::shirt)
    uint8_t where = W_NONE;
    Vec2 pos, target;             // drawn at pos, which chases the latest target
    Vec2 lastPos;
    float angle = 0;
    int weapon = 0;
    bool moving = false, reloading = false, downed = false, inBed = false, laser = false;
    bool inCrypt = false;         // outside, but down in a catacomb (time stands still there)
    int ride = -1;                // 0.11v: in a car (the slot of whose car), -1 on foot
    float flashT = 0, hurtT = 0;
    float hp = 100, maxHp = 100;
    float downT = 0;
    float heardT = 0;             // seconds since their last state arrived
    // Host only.
    Profile* prof = nullptr;      // the guest's character, owned by coop.cpp
    std::vector<std::string> deadMercs;   // died this session: never resurrect from a stale profile
};

bool active();
bool host();                      // also true in local co-op: this game runs the world
bool guest();
bool local();                     // local co-op (0.12v): every player on this screen
bool online();                    // a networked session (host or guest)
int localSlot();
int count();                      // players in the session
// How many more enemies (raiders and horde zombies) the group faces, as a multiplier:
// 1-2 players x1, 3-4 +10-15% (rolled from `seed`), 5-6 +10% more, 7 +10% more,
// 8 +5% more. 1.0 when not in co-op.
float enemyScale(uint64_t seed);
NetPlayer& player(int slot);      // slot 0 is the host
int slotOf(uint64_t id);
// Everyone's colour (from their shirt), used for their name tag and map dot.
int colorPal(int slot);
int shirtOf(int slot);

// ---- who you are when you join someone (a guest's character lives in the host's
// save, so the name and shirt you play with are kept here, in saves/identity.txt).
// Starting a new game of your own sets them too. Empty name: your Steam name.
std::string localName();
int localShirt();
void setIdentity(const std::string& name, int shirt);   // saves, and tells the session

// ---- session
// Menu actions. Host: the save slot is already loaded into G.prof.
void beginHost();
void beginDevHost(int port);
void beginDevJoin(int port);
void startGame();                 // host: lobby -> bunker for everyone
void leave(const std::string& reason);
std::string leaveReason();        // shown by the menu after a session ends
bool welcomed();                  // guest: the host has let us in
void update(float dt);            // every frame, before the scene updates

// ---- hooks for the rest of the game
void sendProfile();               // guest: save_game() lands here
void saveGuests();                // host: writes every guest's character
void sendReliable(int slot, const Net::Writer& w);
void sendUnreliable(int slot, const Net::Writer& w);
void toHost(const Net::Writer& w, bool reliable);
void broadcast(const Net::Writer& w, bool reliable, uint8_t onlyWhere = W_NONE);
void setInBed(bool inBed);
bool localInBed();
// ---- the shared stash
// The bunker's big stash belongs to everyone; the host's save holds it. Only one
// player works in it at a time, so two people can never take the same item. Opening
// the stash panel asks for it; closing the panel lets it go.
void stashOpen();
void stashClose();
// The shared stash to draw and edit right now, or nullptr while it is not ours (still
// asking, or someone else is in it). Solo: always the profile's stash.
std::vector<Item>* sharedStash();
std::string stashStatus();        // why sharedStash() is null
void headline(const std::string& text, int color);   // host: show here and on every guest
// Everyone outside at once.
int outsideCount();              // on the surface (not in a catacomb)
bool anyGuestOutside();

// ---- local co-op (0.12v, see local.h): the seats are player slots of a session that
// never touches the network. A message the host would send a seat's player is kept for
// that seat's turn instead.
void beginLocal();
Profile* addLocalSeat(int slot);  // its character (loaded from the save's co-op folder, or new)
void removeLocalSeat(int slot);   // saves its character
void endLocal();
void setLocalSlot(int slot);      // whose turn it is (localSlot)
void swapLocalSeat(bool& inBed);  // a seat's own bits of session state, in and out
std::vector<std::vector<uint8_t>> takeLocalInbox(int slot);

}  // namespace Coop
