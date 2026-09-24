#pragma once
#include "core.h"
#include "items.h"
#include "assets.h"
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
void itemIcon(int itemId, float x, float y, float box, Color tint = Color(), int count = 0);

// ---- the art pack's UI skin (0.12v)
const Assets::Sprite* skin(const char* key);   // "ui/<key>", or nullptr
// A frame cut in nine: corners as drawn, edges repeated (or stretched), middle stretched.
void nine(const Assets::Frame& f, float x, float y, float w, float h, int l, int t, int r, int b, Color c = Color(), bool repeatEdges = true);
bool nineSprite(const char* key, float x, float y, float w, float h, int l, int t, int r, int b, Color c = Color(), bool repeatEdges = true);
bool skinSprite(const char* key, float x, float y, int frame = 0, Color c = Color());   // at its own size
void textOutline(const std::string& s, float x, float y, Color c, Color ring);
void subPanel(float x, float y, float w, float h, float scroll = -1);   // the lighter sheet, for a list
// A panel with the pack's scroll bar down its right side, its box `scroll` (0..1) down.
void panelScroll(float x, float y, float w, float h, const std::string& title, float scroll);
bool closeBox(float x, float y);                                        // the inventory's X
bool panelClose();                                                      // that X on the last titled panel
// The main menu's lettered buttons ("play", "load", "save", "settings", "quit").
bool menuButton(float x, float y, float w, float h, const char* art, const std::string& label, bool enabled = true);
int yesNo(float x, float y);                                            // 1 yes, 2 no (the tick and the cross)
// A crafting line: result = inputs (or result < inputs, an upgrade). Returns its width.
float recipe(float x, float y, int result, Color resultTint, const int* ins, const int* counts, int n, bool upgrade);
bool checkbox(float x, float y, bool& on, const std::string& label = "", int labelColor = P_LAVENDER);
// The pack's mouse pointer, drawn by endFrame when the system one is hidden.
void setCursor(bool drawn);

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
