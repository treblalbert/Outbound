// Player settings: sound volumes and how the window is shown. Kept in
// saves/settings.txt (the music volume keeps its own older file, see audio.cpp).
#pragma once

struct GLFWwindow;

namespace Options {

enum DisplayMode { DM_WINDOWED, DM_FULLSCREEN, DM_BORDERLESS, DM_COUNT };

void load();                          // before the window is shown
void apply(GLFWwindow* w);            // puts the window in the saved display mode
int displayMode();
void setDisplayMode(int mode);
void toggleFullscreen();              // F11: windowed <-> the last full-screen mode
float sfxVolume();
void setSfxVolume(float v);

// The OPTIONS panel, shared by the main menu and both pause menus. `onClose` is where
// its Back button (and Escape) goes.
void drawPanel(float W, float H);
// CONTROLS: every rebindable key, click one and press the new key. Shared by the menu
// and the pause menus; `back` runs when it is closed.
void drawControls(float W, float H, void (*back)());
// Aim assist for controllers (0.12v): 0 off, 1 standard, 2 strong.
int aimAssist();
void setAimAssist(int level);
// Local co-op (0.12v): pull the camera out when the players spread apart (on by default).
bool coopZoom();
void setCoopZoom(bool on);

}  // namespace Options
