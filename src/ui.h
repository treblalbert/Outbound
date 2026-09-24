#pragma once
#include "core.h"
#include "items.h"
#include <string>

constexpr float SLOT = 20;

namespace UI {
void beginFrame();
void endFrame();                      // draws deferred tooltip
bool hover(float x, float y, float w, float h);
bool overPanel();                     // true if the mouse is over any panel drawn this frame
void panel(float x, float y, float w, float h, const std::string& title = "");
// Draws text broken into lines that fit maxW; returns the height it took.
float textWrap(const std::string& s, float x, float y, float maxW, Color c, float lineH = 10);
bool button(float x, float y, float w, float h, const std::string& label, bool enabled = true, int color = P_WHITE);
// Returns 0 none, 1 left click, 2 right click.
int itemSlot(float x, float y, const Item& it, bool highlight = false, bool dim = false);
void tooltip(const std::string& title, const std::string& body, int titleColor = P_YELLOW);
void tooltip(const std::string& title, const std::string& body, Color titleColor);
void itemTooltip(const Item& it, const std::string& extra = "");
void bar(float x, float y, float w, float h, float frac, int fg, int bg = P_DARK);
bool slider(float x, float y, float w, float h, float& value);  // draggable 0..1 slider, returns true while changed
// A one-line text box. Click it to type (it holds the keyboard until you click
// elsewhere or press Enter); `focused` is the caller's. Returns true when it changed.
bool textField(float x, float y, float w, float h, std::string& text, bool& focused, int maxLen);
// Draws an item icon centred in a box, preferring the art pack over the built-in art.
void itemIcon(int itemId, float x, float y, float box, Color tint = Color());

// ---- controller navigation
// Buttons, item slots and sliders register themselves as they are drawn; anything
// else clickable can call focusable(). After the frame is drawn, padNavigate moves
// the controller's cursor to the nearest widget in the direction pressed, so A
// clicks it. A new screen (context) starts on its first widget.
void focusable(float x, float y, float w, float h);
unsigned frameNo();                   // counts UI frames (beginFrame)
void padNavigate(int context);
// Gold marker for the special "elite" guns: frame, name colour.
int eliteColor();
}
