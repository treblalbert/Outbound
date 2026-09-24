#pragma once
#include "core.h"

// Every sound effect comes from the Darkworld Audio "Survival Effects" pack
// (assets/Darkworld Audio - Survival Effects [Free .ogg]); see the table in audio.cpp
// for which clips each one uses. Most have several clips and a random one plays.
#define SOUND_LIST \
    S(pistol) S(smg) S(shotgun) S(rifle) S(sniper) S(launcher) S(explosion) S(empty) S(reload) S(reload_end) \
    S(hit) S(hurt) S(pickup) S(click) S(tile_hit) S(tile_break) S(enemy_die) S(warning) S(night) \
    S(shade) S(toss) S(heal) S(sell) S(door) S(search) S(melee) S(step) S(step_in) S(splash) \
    S(car_crash) S(car_door) S(car_hit) S(car_glass) S(tyres) S(fuel)

namespace Snd {
#define S(n) n,
enum Id : int { SOUND_LIST COUNT };
#undef S
}

namespace Audio {
bool init();
// Dev (--mute): no music and no sound effects at all, for silent test runs.
void setMuted(bool m);
void shutdown();
bool ready();    // OpenAL is up (voice chat shares its device)
void update();   // polls background music, advances to the next random track
void play(int snd, float volume = 1, float pitch = 1);
// Volume of every sound effect (0..1), set from the options.
void setSfxVolume(float v);
float sfxVolume();
// Plays with distance attenuation relative to the listener.
void playAt(int snd, Vec2 pos, Vec2 listener, float volume = 1, float pitch = 1);
// ---- ambience (0.11v): looping beds under everything, from the same pack, with their
// own volume. A scene says every frame how much of each it wants (0..1); they fade in
// and out on their own, and fade away when nobody asks for them.
enum Ambient { AMB_BIRDS, AMB_RAIN, AMB_DRIP, AMB_WIND, AMB_COUNT };
void setAmbient(int bed, float level, float pitch = 1);
void ambientTick(float dt);
// A short sound of the place (a bee going by), at the ambience volume. pan -1..1.
enum AmbientShot { AMS_BEE, AMS_COUNT };
void ambientShot(int shot, float volume, float pan);
float ambientVolume();
void setAmbientVolume(float v);
// ---- looping effect voices (0.11v: cars). A car's engine and its tyres squealing are
// loops cut from the pack's own clips: the engine is the bee's buzz slowed far down into
// a motor's drone, the squeal the crowbar's scrape. A caller sets a voice every frame it
// wants it (gain 0..1, pitch, pan -1..1); voices nobody sets fade out. Gain follows the
// effects volume.
enum LoopClip { LOOP_ENGINE, LOOP_SCREECH, LOOP_COUNT };
constexpr int LOOP_VOICES = 6;
void setLoop(int voice, int clip, float gain, float pitch, float pan);
// Background music: random shuffle of assets/Music/*, 0..1, default 0.25, saved.
float musicVolume();
void setMusicVolume(float v);
const char* musicTrack();  // short display name of the current track ("" if none)
void nextTrack();
}
