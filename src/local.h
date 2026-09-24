// Local co-op (0.12v): up to four players on one screen, each on their own controller
// (or the keyboard and mouse), in the same world.
//
// It rides on the online co-op machinery: a local session is a co-op session in which
// this game is the host and every seat is a player slot (seat N = slot N). Kills,
// money, damage, reviving and mercenaries all go through the co-op code, with the
// messages a guest would get handed straight to that seat instead of over the wire.
//
// The rest of the game is written for one player: G.prof, G.player, G.panel and the
// raid's per-player state. So each seat's copy of that lives here, and a seat's turn
// swaps it into the globals (with(seat, ...)), with that seat's controls as the input.
// Seat 0 is the save's own character and lives in the globals ("home"); the world
// itself (enemies, the clock, the horde) is simulated once, at home.
//
// Everyone shares the screen, so the group moves together: going out, coming home,
// the stairs and the catacombs take everybody. The camera follows all of them and
// pulls back when they spread out; nobody can walk off the edge of it.
#pragma once
#include "core.h"
#include <functional>
#include <string>

struct Profile;

namespace Local {

constexpr int MAX_SEATS = 4;

bool active();                     // a local session is on (Coop::local())
int count();                       // seats playing (1 when off)
bool used(int seat);
int current();                     // whose state is in the globals now (0 at home)
int deviceOf(int seat);            // Input::DEV_KBM or a controller
std::string nameOf(int seat);
int colorOf(int seat);             // palette colour (from their shirt)

// Runs f with that seat's character, player, panel and controls in the globals. Safe
// to nest: whatever was current before is put back afterwards.
void with(int seat, const std::function<void()>& f);
// Each seat in turn, in its own context (seat 0 at home with its controls).
void forEach(const std::function<void(int seat)>& f);
// The save's own character, wherever it is right now.
Profile& hostProfile();

// Group actions one player sets off (going out, coming home, the stairs, sleeping)
// have to run at home, once, for everybody: they are queued and run after the turn.
void atHome(std::function<void()> f);
void runDeferred();

// Every seat's player written into the co-op player list (where they are, how they
// are): what the world, the other seats and the drawing go by. After each round of turns.
void publishSeats();
// The current seat joined a moment ago: its input waits (the button it joined with).
bool justJoined();

// One player at a time works in a menu: the one whose panel is on screen.
int uiOwner();                     // -1 nobody
bool otherHasPanel();              // a seat other than the current one has a panel up

// Joining (START on a free controller, or ENTER on the keyboard while everyone else
// is on controllers) and leaving. Called by main every frame, at home.
void update(float dt);
void leaveSeat(int seat);          // from that seat's pause menu (deferred)
void end();                        // back to single player (deferred when inside a turn)

// ---- the shared camera (raid)
// Where everyone standing is: a seat that bled out waits for the others and does not
// count. Returns false if nobody is.
bool groupBox(Vec2& lo, Vec2& hi, int skipSeat = -1);
// Walking or driving from `from` to `to`: keeps everyone within the widest view the
// camera can show. Only the axes that would spread the group too far are held back.
Vec2 leash(Vec2 from, Vec2 to);
float maxZoom();                   // 1 when the zoom-out option is off

// ---- UI helpers
// "P2" style tags, colours and the hint for joining (drawn by the bunker and pause).
std::string joinHint();

}  // namespace Local
