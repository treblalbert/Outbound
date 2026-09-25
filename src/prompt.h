// On-screen input prompts: the key, mouse button or controller button for an action,
// drawn with Kenney's Input Prompts art. Whatever the player last touched decides
// which set shows (see Input::usingPad), so hints switch as soon as you pick up a pad.
#pragma once
#include "core.h"
#include <string>

namespace Prompt {

enum Action {
    Interact, Inventory, Mission, Map, Pause, Back, Reload, Heal, Grenade, Swap, Sprint, Laser,
    Shoot, Aim, Move, Choose, Select, AltSelect, Fullscreen,
    Accelerate, Brake, Steer, Handbrake,   // 0.11v: driving
    Grab, PagePrev, PageNext, Join,        // 0.12v: moving items with a controller, pages, local co-op
    Melee,                                 // 0.12v: a punch, or the bat
    COUNT
};

// Width of the icon for an action (16, or 33 for a wide key such as Tab).
float iconWidth(Action a);
// Draws just the icon with its top-left at (x, y); returns its width.
float icon(Action a, float x, float y, float alpha = 1);
// Icon, then text beside it, on one 16-pixel line starting at (x, y). Returns the width.
float label(Action a, const std::string& text, float x, float y, Color c, float alpha = 1);
float labelWidth(Action a, const std::string& text);
void labelCentered(Action a, const std::string& text, float cx, float y, Color c, float alpha = 1);

// A row of hints, left to right from (x, y); returns the width used.
struct Hint { Action a; std::string text; };
float row(const Hint* hints, int n, float x, float y, Color c, float gap = 10);
// Draw a controller's buttons (Input::PAD_XBOX / PAD_PLAYSTATION) whatever is in use,
// until forcePad(-1): the controls page.
void forcePad(int type);
float rowWidth(const Hint* hints, int n, float gap = 10);

}  // namespace Prompt
