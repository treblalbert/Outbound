// Proximity voice chat for co-op, with no outside service: the microphone is read
// through OpenAL, squeezed with IMA ADPCM (16 kHz, about 8 KB/s) and sent over the
// game's own co-op connection (the host passes each player's voice on to the rest).
//
// What you hear depends on where the two of you stand, in the same place (outside, or
// down in the bunker): someone off your screen cannot be heard at all; on screen they
// get louder and clearer the closer they are, sit left or right of you in stereo, and
// sound muffled and quieter with a wall between you.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Voice {

enum Mode { PUSH_TO_TALK, OPEN_MIC };

void init();       // after Audio::init (it shares the OpenAL device)
void shutdown();
void update(float dt);   // every frame: record, send, and play what arrived
// A voice message (the whole message, type byte first) from `fromSlot` (host) or
// from the host (guest, fromSlot -1).
void onPacket(int fromSlot, const uint8_t* data, size_t size);

// ---- settings (kept in saves/settings.txt by the options)
bool enabled();
void setEnabled(bool on);
int mode();
void setMode(int m);
float micGain();        // 0..2
void setMicGain(float g);
float volume();         // 0..1, everyone else's voices
void setVolume(float v);
float sensitivity();    // 0..1, how easily open mic starts sending
void setSensitivity(float s);
std::string settingsText();
void settingsFromText(const std::string& text);

// ---- which microphone (0.12v)
std::vector<std::string> micDevices();   // every input OpenAL sees, refreshed as they come and go
std::string defaultMicName();            // the system's default input
std::string micDevice();                 // the chosen one, "" = follow the system default
void setMicDevice(const std::string& name);

// ---- state for the HUD and the options
bool micAvailable();
bool transmitting();    // you are being heard right now
bool talking(int slot); // that player's voice is coming in (and you can hear it)
float micLevel();       // 0..1, for the options' meter
// While the options are open the microphone runs for the level meter (nothing sent).
void setMeter(bool on);

// A small microphone badge on the HUD while you transmit (screen coordinates).
void drawHud(float x, float y);

}  // namespace Voice
