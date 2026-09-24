#pragma once
#include "core.h"
#include <string>

struct GLFWwindow;

namespace Input {
void init(GLFWwindow* w);
void update(GLFWwindow* w, int pixelScale);
bool down(int key);
bool pressed(int key);
bool keyPressed(int key);      // physical keyboard only (useful when pad A selects UI)
bool mouseDown(int button);
bool mouseHeld(int button);   // raw held state, unaffected by consumeMouse (for drags)
bool mousePressed(int button);
bool mouseReleased(int button);
void consumeMouse();          // swallow this frame's clicks (UI handled them)
Vec2 mouse();                 // in internal (pixel-art) coordinates
float scroll();
// Standard GLFW gamepad support. The left stick/D-pad feeds movement keys, the
// right stick aims, and the usual face/shoulder buttons feed gameplay actions.
bool gamepad();
Vec2 moveAxis();
Vec2 aimAxis();
// How far a controller trigger is pulled, 0..1 (0 with no controller): driving.
float padTrigger(bool right);
void suppressFireUntilRelease();
// True while the controller is what the player last touched (a stick, trigger or
// button), false again once the mouse moves or clicks. Gameplay aims by direction
// then, and draws a pointer around the player instead of a free cursor.
bool usingPad();
// The right stick drives an on-screen cursor only while this is on (menus, panels).
// During play it is off, so the stick only aims. Set every frame; default on.
void setPadCursor(bool on);

// ---- menus with a controller
// Set every frame before the scene updates: true while a menu or panel is up. Then
// B means "back" (Escape) instead of its gameplay action, and X is the alternative
// action on a slot (what a right click does) instead of reload.
void setPadMenu(bool on);
bool padMenu();
// A direction pressed on the D-pad or left stick this frame, with key-style repeat
// while held: 0 up, 1 down, 2 left, 3 right, -1 none. consumeNav() takes it (a
// slider uses left/right itself).
int navDir();
void consumeNav();
bool padAltPressed();           // X, while a menu is up
bool padGrabPressed();          // Y, while a menu is up: pick an item up to move it
// LB / RB while a menu is up: the previous / next page or tab (-1, +1, 0 none).
int padPagePressed();
// Puts the controller's cursor on a point (menu navigation jumps it between widgets).
void warpPadCursor(Vec2 p);
bool padCursorMoved();          // the right stick moved the cursor this frame
// Raw D-pad presses (this frame): 0 up, 1 down, 2 left, 3 right.
bool dpadPressed(int dir);
// While off, the D-pad does not walk (the raid uses up/down to pick between things in
// reach instead). Persistent; the raid sets it every frame, other scenes turn it on.
void setDpadWalk(bool on);

// ---- which device answers (local co-op, 0.12v)
// Everything above reads the current device. DEV_ALL (the default, and all of single
// player) merges the keyboard, the mouse and whichever controller was touched last.
// A local co-op seat reads only its own: DEV_KBM, or one controller (0..15). Setters
// (setPadCursor, setPadMenu, setDpadWalk, suppressFireUntilRelease) apply to the
// current device; under DEV_ALL they apply to every device.
constexpr int DEV_ALL = -1;
constexpr int DEV_KBM = 100;
constexpr int MAX_PADS = 16;
void setDevice(int dev);
int device();
// The controller that was touched last (any device), -1 none.
int lastPad();
bool padConnected(int pad);
// A button (GLFW_GAMEPAD_BUTTON_*) going down this frame on that controller, whatever
// the current device is (joining a local game).
bool padButtonPressed(int pad, int button);
// Any key or mouse button went down this frame (raw, whatever the device).
bool kbmPressedAny();
// What kind of controller: its button faces and names follow it (prompts, 0.12v).
enum PadType { PAD_XBOX, PAD_PLAYSTATION };
int padType();                  // the current device's controller (the last one touched under DEV_ALL)
int padTypeOf(int pad);
std::string padName(int pad);   // "Xbox controller", "DualSense", ...

// ---- key bindings
// The game asks for keys by their default (GLFW_KEY_E means "interact"); a binding
// maps that to whatever key the player chose. Mouse and controller are not remapped.
int binding(int action);                  // the key an action is on now
void setBinding(int action, int key);     // swaps with any action already on that key
void resetBindings();
// The keys the player can rebind, in the order the controls panel lists them.
struct Bindable { int action; const char* label; };
const Bindable* bindables(int& count);
int keyPressedAny();                      // a key pressed this frame (raw), or -1
// While on, the keyboard answers nothing to the game (the controls panel is waiting for
// the key to bind, and that key must not also do its old job).
void setCapture(bool on);
bool capturing();
std::string keyName(int key);
// Text typed this frame (printable ASCII) and how many times backspace was pressed
// (with key repeat), for text fields.
std::string typed();
int backspaces();             // "E", "SHIFT", "F5", ...
// Bindings as "action key" pairs, for the settings file.
std::string bindingsText();
void bindingsFromText(const std::string& text);
}
