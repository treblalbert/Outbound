// Proximity voice chat. See voice.h.
#include "voice.h"
#include "audio.h"
#include "coop.h"
#include "game.h"
#include "input.h"
#include "lang.h"
#include "render.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <vector>
#ifndef __EMSCRIPTEN__
#include <AL/al.h>
#include <AL/alc.h>
#endif

namespace Voice {

namespace {

constexpr int RATE = 16000;
constexpr int FRAME = 320;                 // 20 ms
constexpr int FRAME_BYTES = 3 + FRAME / 2; // predictor, step index, 4-bit codes
constexpr int FRAMES_PER_PACKET = 2;       // 40 ms a message
constexpr int POOL = 24;                   // playback buffers per talker

bool s_enabled = true;
int s_mode = PUSH_TO_TALK;
float s_micGain = 1.0f, s_volume = 1.0f, s_sens = 0.5f;
bool s_meter = false;
float s_level = 0;
float s_sendHold = 0;       // open mic keeps sending a moment after you stop
bool s_transmitting = false;

// ---- IMA ADPCM
const int STEPS[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
    107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871,
    5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
    27086, 29794, 32767};
const int INDEX_ADJ[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

struct Adpcm { int pred = 0, index = 0; };

int adpcmStep(Adpcm& st, int code) {
    int step = STEPS[st.index];
    int diff = step >> 3;
    if (code & 4) diff += step;
    if (code & 2) diff += step >> 1;
    if (code & 1) diff += step >> 2;
    st.pred += (code & 8) ? -diff : diff;
    st.pred = std::clamp(st.pred, -32768, 32767);
    st.index = std::clamp(st.index + INDEX_ADJ[code & 7], 0, 88);
    return st.pred;
}

void encodeFrame(const int16_t* in, Adpcm& st, std::vector<uint8_t>& out) {
    out.push_back((uint8_t)(st.pred & 0xFF));
    out.push_back((uint8_t)((st.pred >> 8) & 0xFF));
    out.push_back((uint8_t)st.index);
    for (int i = 0; i < FRAME; i += 2) {
        uint8_t byte = 0;
        for (int k = 0; k < 2; k++) {
            int step = STEPS[st.index];
            int diff = in[i + k] - st.pred;
            int code = 0;
            if (diff < 0) { code = 8; diff = -diff; }
            if (diff >= step) { code |= 4; diff -= step; }
            if (diff >= step >> 1) { code |= 2; diff -= step >> 1; }
            if (diff >= step >> 2) code |= 1;
            adpcmStep(st, code);   // keep the encoder in step with the decoder
            byte |= (uint8_t)(code << (k * 4));
        }
        out.push_back(byte);
    }
}

void decodeFrame(const uint8_t* in, int16_t* out) {
    Adpcm st;
    st.pred = (int16_t)(in[0] | (in[1] << 8));
    st.index = std::clamp((int)in[2], 0, 88);
    for (int i = 0; i < FRAME / 2; i++) {
        uint8_t b = in[3 + i];
        out[i * 2] = (int16_t)adpcmStep(st, b & 15);
        out[i * 2 + 1] = (int16_t)adpcmStep(st, b >> 4);
    }
}

#ifndef __EMSCRIPTEN__
ALCdevice* s_cap = nullptr;
bool s_capTried = false, s_capOk = false, s_capRunning = false;
Adpcm s_enc;
std::vector<uint8_t> s_outFrames;   // encoded frames waiting to be sent
int s_outCount = 0;
uint16_t s_seq = 0;

struct Talker {
    ALuint src = 0;
    std::vector<ALuint> freeBufs;
    int queued = 0;
    float heardT = 99;       // seconds since their voice last arrived
    float audible = 0;       // how loud they were in the last frames (for the badge)
    float gain = 0, pan = 0, lp1 = 0, lp2 = 0;
    bool ready = false;
};
Talker s_talk[Coop::MAX_PLAYERS];

bool capOpen() {
    if (s_cap) return true;
    if (s_capTried && !s_capOk) return false;
    s_capTried = true;
    s_cap = alcCaptureOpenDevice(nullptr, RATE, AL_FORMAT_MONO16, RATE);
    s_capOk = s_cap != nullptr;
    std::fprintf(stderr, s_capOk ? "[voice] microphone ready\n" : "[voice] no microphone\n");
    return s_capOk;
}

void capRun(bool on) {
    if (on && !s_capRunning && capOpen()) { alcCaptureStart(s_cap); s_capRunning = true; }
    if (!on && s_capRunning) {
        alcCaptureStop(s_cap);
        s_capRunning = false;
        s_level = 0;
        // Throw away what was recorded, so the next talk starts fresh.
        ALCint n = 0;
        alcGetIntegerv(s_cap, ALC_CAPTURE_SAMPLES, 1, &n);
        std::vector<int16_t> junk((size_t)std::max(0, n));
        if (n > 0) alcCaptureSamples(s_cap, junk.data(), n);
    }
}

// Where a teammate is heard from, for the local listener: loudness, left/right, how
// much is left of the high end, whether a wall is in the way. False: not heard.
bool listenFor(int slot, float& gain, float& pan, float& cutoff) {
    const Coop::NetPlayer& np = Coop::player(slot);
    const Coop::NetPlayer& me = Coop::player(Coop::localSlot());
    if (!np.used || !me.used || np.where != me.where) return false;
    // The lobby (0.11v): everyone waiting hears everyone, plainly.
    if (np.where == Coop::W_LOBBY && G.scene == Scene::Lobby) { gain = 0.9f; pan = 0; cutoff = 7500; return true; }
    if (np.where != Coop::W_RAID && np.where != Coop::W_BASE) return false;
    if (np.where == Coop::W_BASE && G.prof.rivals) return false;   // Rivals: everyone's bunker is their own
    // At the defense console you are still in the bunker, just looking at a screen:
    // the others down there are heard, without direction (0.11v; it used to be silent).
    if (np.where == Coop::W_BASE && G.scene == Scene::Defense) { gain = 0.7f; pan = 0; cutoff = 6000; return true; }
    bool raid = G.scene == Scene::Raid, base = G.scene == Scene::Base;
    if ((np.where == Coop::W_RAID && !raid) || (np.where == Coop::W_BASE && !base)) return false;
    // Only what is on your screen.
    float W = (float)R::viewW(), H = (float)R::viewH();
    Vec2 c = G.drawCam;
    if (np.pos.x < c.x || np.pos.y < c.y || np.pos.x > c.x + W || np.pos.y > c.y + H) return false;
    float d = dist(np.pos, G.player.pos);
    float maxD = 0.5f * std::sqrt(W * W + H * H);
    float t = clampf(d / maxD, 0, 1);
    gain = 0.15f + 0.85f * (1.0f - t) * (1.0f - t);
    cutoff = 7500.0f - 6000.0f * t;
    const World& w = raid ? G.world : G.baseWorld;
    if (!w.lineOfSight(G.player.pos, np.pos)) {
        gain *= 0.45f;
        cutoff = std::min(cutoff, 600.0f);
    }
    pan = clampf((np.pos.x - G.player.pos.x) / (W * 0.5f), -1, 1) * 0.75f;
    return true;
}

void play(int slot, const int16_t* mono, int n) {
    Talker& tk = s_talk[slot];
    if (!tk.ready) {
        alGenSources(1, &tk.src);
        alSourcei(tk.src, AL_SOURCE_RELATIVE, AL_TRUE);
        alSource3f(tk.src, AL_POSITION, 0, 0, 0);
        tk.freeBufs.resize(POOL);
        alGenBuffers(POOL, tk.freeBufs.data());
        tk.ready = true;
    }
    tk.heardT = 0;
    float gain = 0, pan = 0, cutoff = 1000;
    bool heard = listenFor(slot, gain, pan, cutoff);
    if (!heard) gain = 0;
    gain *= s_volume * 1.6f;
    tk.audible = heard ? gain : 0;
    // More than ~160 ms waiting (a hitch, a burst after a lag spike): drop this piece so
    // the voice stays live instead of trailing behind for good.
    if (tk.freeBufs.empty() || tk.queued >= 4) return;
    // Clearer close by, duller far off or behind a wall: two gentle low-passes.
    float a = 1.0f - std::exp(-2.0f * PI * cutoff / RATE);
    float g0 = tk.gain, g1 = gain, p0 = tk.pan, p1 = pan;
    std::vector<int16_t> st((size_t)n * 2);
    for (int i = 0; i < n; i++) {
        float f = (i + 1) / (float)n;
        float g = g0 + (g1 - g0) * f, pn = p0 + (p1 - p0) * f;
        float x = mono[i] / 32768.0f;
        tk.lp1 += (x - tk.lp1) * a;
        tk.lp2 += (tk.lp1 - tk.lp2) * a;
        float y = tk.lp2 * g;
        float l = y * std::sqrt(0.5f * (1.0f - pn)) * 1.41f, r = y * std::sqrt(0.5f * (1.0f + pn)) * 1.41f;
        st[(size_t)i * 2] = (int16_t)clampf(l * 32767.0f, -32767, 32767);
        st[(size_t)i * 2 + 1] = (int16_t)clampf(r * 32767.0f, -32767, 32767);
    }
    tk.gain = gain;
    tk.pan = pan;
    ALuint b = tk.freeBufs.back();
    tk.freeBufs.pop_back();
    alBufferData(b, AL_FORMAT_STEREO16, st.data(), (ALsizei)(st.size() * 2), RATE);
    alSourceQueueBuffers(tk.src, 1, &b);
    tk.queued++;
    ALint state = 0;
    alGetSourcei(tk.src, AL_SOURCE_STATE, &state);
    if (state != AL_PLAYING && tk.queued >= 3) alSourcePlay(tk.src);   // a little cushion first
}

void flushSend() {
    if (s_outCount == 0) return;
    Net::Writer w;
    w.u8(Coop::M_VOICE);
    w.u8((uint8_t)Coop::localSlot());
    w.u16(s_seq++);
    w.u8((uint8_t)s_outCount);
    w.raw(s_outFrames.data(), s_outFrames.size());
    if (Coop::host()) Coop::broadcast(w, false);
    else Coop::toHost(w, false);
    s_outFrames.clear();
    s_outCount = 0;
}
#endif

}  // namespace

// ---------------------------------------------------------------- settings
bool enabled() { return s_enabled; }
void setEnabled(bool on) { s_enabled = on; }
int mode() { return s_mode; }
void setMode(int m) { s_mode = m == OPEN_MIC ? OPEN_MIC : PUSH_TO_TALK; }
float micGain() { return s_micGain; }
void setMicGain(float g) { s_micGain = clampf(g, 0, 2); }
float volume() { return s_volume; }
void setVolume(float v) { s_volume = clampf(v, 0, 1); }
float sensitivity() { return s_sens; }
void setSensitivity(float s) { s_sens = clampf(s, 0, 1); }

std::string settingsText() {
    std::ostringstream o;
    o << (s_enabled ? 1 : 0) << ' ' << s_mode << ' ' << s_micGain << ' ' << s_volume << ' ' << s_sens;
    return o.str();
}

void settingsFromText(const std::string& text) {
    std::istringstream in(text);
    int on = 1, m = 0;
    float g = 1, v = 1, s = 0.5f;
    if (in >> on >> m >> g >> v >> s) {
        setEnabled(on != 0);
        setMode(m);
        setMicGain(g);
        setVolume(v);
        setSensitivity(s);
    }
}

float micLevel() { return s_level; }
void setMeter(bool on) { s_meter = on; }
bool transmitting() { return s_transmitting; }

#ifdef __EMSCRIPTEN__
void init() {}
void shutdown() {}
void update(float) {}
void onPacket(int, const uint8_t*, size_t) {}
bool micAvailable() { return false; }
bool talking(int) { return false; }
#else

void init() {}

void shutdown() {
    capRun(false);
    if (s_cap) { alcCaptureCloseDevice(s_cap); s_cap = nullptr; }
    for (Talker& tk : s_talk) {
        if (!tk.ready) continue;
        alSourceStop(tk.src);
        ALint q = 0;
        alGetSourcei(tk.src, AL_BUFFERS_QUEUED, &q);
        while (q-- > 0) { ALuint b; alSourceUnqueueBuffers(tk.src, 1, &b); tk.freeBufs.push_back(b); }
        alDeleteSources(1, &tk.src);
        alDeleteBuffers((ALsizei)tk.freeBufs.size(), tk.freeBufs.data());
        tk = Talker();
    }
}

bool micAvailable() { return !s_capTried || s_capOk; }

bool talking(int slot) {
    if (slot < 0 || slot >= Coop::MAX_PLAYERS) return false;
    const Talker& tk = s_talk[slot];
    return tk.heardT < 0.3f && tk.audible > 0.02f;
}

void update(float dt) {
    if (!Audio::ready()) return;
    bool inGame = Coop::active() && (G.scene == Scene::Base || G.scene == Scene::Raid || G.scene == Scene::Defense || G.scene == Scene::Lobby);

    // ---- playback: hand back the buffers that have played
    for (int i = 0; i < Coop::MAX_PLAYERS; i++) {
        Talker& tk = s_talk[i];
        tk.heardT += dt;
        if (!tk.ready) continue;
        ALint done = 0;
        alGetSourcei(tk.src, AL_BUFFERS_PROCESSED, &done);
        while (done-- > 0) {
            ALuint b;
            alSourceUnqueueBuffers(tk.src, 1, &b);
            tk.freeBufs.push_back(b);
            tk.queued--;
        }
        if (!Coop::active() || !s_enabled) { alSourceStop(tk.src); }
    }

    // ---- the microphone
    bool meter = s_meter;
    s_meter = false;          // the options panel asks again every frame it is open
    bool wantMic = s_enabled && (inGame || meter);
    capRun(wantMic);
    s_transmitting = false;
    if (!s_capRunning) return;
    ALCint avail = 0;
    alcGetIntegerv(s_cap, ALC_CAPTURE_SAMPLES, 1, &avail);
    bool ptt = s_mode == PUSH_TO_TALK && Input::down(GLFW_KEY_V) && !Input::capturing();
    float gate = 0.004f + (1.0f - s_sens) * 0.05f;   // open mic: loudness that counts as talking
    int16_t buf[FRAME];
    while (avail >= FRAME) {
        alcCaptureSamples(s_cap, buf, FRAME);
        avail -= FRAME;
        double sum = 0;
        for (int i = 0; i < FRAME; i++) {
            float v = clampf(buf[i] * s_micGain, -32767, 32767);
            buf[i] = (int16_t)v;
            sum += (double)v * v;
        }
        float rms = (float)std::sqrt(sum / FRAME) / 32768.0f;
        s_level = std::max(rms * 4.0f, s_level * 0.85f);
        if (s_mode == OPEN_MIC) {
            if (rms > gate) s_sendHold = 0.4f;
            else s_sendHold -= FRAME / (float)RATE;
        }
        bool send = inGame && (s_mode == PUSH_TO_TALK ? ptt : s_sendHold > 0);
        if (!send) { s_enc = Adpcm(); continue; }
        s_transmitting = true;
        encodeFrame(buf, s_enc, s_outFrames);
        if (++s_outCount >= FRAMES_PER_PACKET) flushSend();
    }
    if (!s_transmitting) flushSend();
    s_transmitting = s_transmitting || (inGame && (ptt || (s_mode == OPEN_MIC && s_sendHold > 0)));
    s_level = std::min(1.0f, s_level);
}

void onPacket(int fromSlot, const uint8_t* data, size_t size) {
    if (size < 5) return;
    Net::Reader r(data + 1, size - 1);
    int slot = r.u8();
    r.u16();
    int n = r.u8();
    if (r.bad || n <= 0 || n > 8 || r.i + (size_t)n * FRAME_BYTES > r.n) return;
    if (Coop::host()) {
        // The host knows who really sent it, and passes it on to everyone else.
        if (fromSlot <= 0) return;
        slot = fromSlot;
        std::vector<uint8_t> copy(data, data + size);
        copy[1] = (uint8_t)slot;
        for (int i = 1; i < Coop::MAX_PLAYERS; i++) {
            if (i == slot || !Coop::player(i).used) continue;
            Net::Writer w;
            w.b = copy;
            Coop::sendUnreliable(i, w);
        }
    }
    if (slot < 0 || slot >= Coop::MAX_PLAYERS || slot == Coop::localSlot()) return;
    if (!s_enabled || !Audio::ready()) return;
    const uint8_t* frames = data + 1 + r.i;
    std::vector<int16_t> pcm((size_t)n * FRAME);
    for (int f = 0; f < n; f++) decodeFrame(frames + (size_t)f * FRAME_BYTES, pcm.data() + (size_t)f * FRAME);
    play(slot, pcm.data(), (int)pcm.size());
}
#endif

void drawHud(float x, float y) {
    if (!Coop::active() || !s_enabled) return;
    if (!s_transmitting) {
        if (s_mode == PUSH_TO_TALK && micAvailable())
            R::text(T("Hold") + " " + Input::keyName(Input::binding(GLFW_KEY_V)) + " " + T("to talk"), x, y, pal(P_PURPLE, 0.8f));
        return;
    }
    bool blink = std::fmod(G.realTime, 0.8f) < 0.55f;
    R::rect(x, y + 1, 3, 5, pal(P_YGREEN));
    R::rect(x - 1, y + 5, 5, 1, pal(P_YGREEN));
    R::rect(x + 1, y + 6, 1, 2, pal(P_YGREEN));
    R::text(T("Talking"), x + 7, y, pal(blink ? P_YGREEN : P_LGREEN));
    float lw = 30 * clampf(s_level, 0, 1);
    R::rect(x + 7 + R::textWidth(T("Talking")) + 4, y + 3, lw, 2, pal(P_YGREEN, 0.8f));
}

}  // namespace Voice
