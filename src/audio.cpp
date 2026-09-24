// OpenAL audio: sound effects from the Darkworld Audio pack (Ogg Vorbis), music via MCI.
#include "audio.h"
#include "render.h"
#include <AL/al.h>
#include <AL/alc.h>
#include <vorbis/vorbisfile.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iterator>
#include <random>
#include <vector>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <cstdlib>

// On the web the music is streamed by the browser from music/ rather than packed
// into the build, so an <audio> element owns the playlist and C++ just steers it.
// Browsers refuse to start audio before the player interacts, so the first click or
// key press kicks it off.
EM_JS(void, web_music_init, (float vol), {
    if (Module.__music) return;
    var m = {el: new Audio(), list: [], idx: -1, vol: vol, started: false, name: ""};
    Module.__music = m;
    m.el.volume = vol;
    m.el.addEventListener('ended', function() { Module.__musicNext(); });
    Module.__musicNext = function() {
        if (!m.list.length) return;
        var n = m.idx;
        if (m.list.length === 1) n = 0;
        else while (n === m.idx) n = Math.floor(Math.random() * m.list.length);
        m.idx = n;
        var f = m.list[n];
        m.name = f.replace(/\.[^.]+$/, '');
        m.el.src = 'music/' + encodeURIComponent(f);
        var pr = m.el.play();
        if (pr && pr.catch) pr.catch(function() {});
    };
    var begin = function() {
        if (m.started || !m.list.length) return;
        m.started = true;
        Module.__musicNext();
    };
    ['pointerdown', 'keydown'].forEach(function(e) {
        window.addEventListener(e, begin, {once: false});
    });
    fetch('music/tracks.json').then(function(r) { return r.json(); }).then(function(j) {
        m.list = j || [];
        begin();
    }).catch(function() {});
});

EM_JS(void, web_music_next, (), {
    if (Module.__musicNext) Module.__musicNext();
});

EM_JS(void, web_music_volume, (float v), {
    if (Module.__music) { Module.__music.vol = v; Module.__music.el.volume = v; }
});

EM_JS(char*, web_music_name, (), {
    var n = (Module.__music && Module.__music.started) ? Module.__music.name : "";
    return stringToNewUTF8(n);
});
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <direct.h>
#endif

namespace Audio {

static ALCdevice* g_device = nullptr;
static ALCcontext* g_context = nullptr;
static std::vector<ALuint> g_sources;
static bool g_ok = false;


// ---- sound effects: clips from the Darkworld Audio "Survival Effects" pack
// Each sound lists its clips ('|' separated, relative to the pack folder); one is
// picked at random every time it plays. `pitch` and `gain` shape the clip into what
// the game needs (a pistol shot pitched down with its reverb tail is the shotgun),
// and `maxLen` cuts long clips (with a short fade) to the part that is used.
static const char* PACK_DIR = "assets/Darkworld Audio - Survival Effects [Free .ogg]/";

struct SoundDef { const char* clips; float pitch, gain, maxLen; };

static const SoundDef SOUND_DEFS[Snd::COUNT] = {
    /* pistol     */ {"Combat/DesignedGunshot_Pistol1|Combat/DesignedGunshot_Pistol2|Combat/DesignedGunshot_Pistol3|Combat/DesignedGunshot_Pistol4", 1.0f, 0.9f, 0},
    /* smg        */ {"Combat/DesignedGunshot_Pistol1|Combat/DesignedGunshot_Pistol2|Combat/DesignedGunshot_Pistol3|Combat/DesignedGunshot_Pistol4", 1.22f, 0.7f, 0.35f},
    /* shotgun    */ {"Combat/DesignedGunshot_Pistol1_Reverb|Combat/DesignedGunshot_Pistol2_Reverb|Combat/DesignedGunshot_Pistol3_Reverb|Combat/DesignedGunshot_Pistol4_Reverb", 0.7f, 1.0f, 0},
    /* rifle      */ {"Combat/DesignedGunshot_Pistol1_Reverb|Combat/DesignedGunshot_Pistol2_Reverb|Combat/DesignedGunshot_Pistol3_Reverb|Combat/DesignedGunshot_Pistol4_Reverb", 0.9f, 0.9f, 0},
    /* sniper     */ {"Combat/DesignedGunshot_Pistol1_Reverb|Combat/DesignedGunshot_Pistol2_Reverb|Combat/DesignedGunshot_Pistol3_Reverb|Combat/DesignedGunshot_Pistol4_Reverb", 0.58f, 1.0f, 0},
    /* launcher   */ {"Combat/DesignedGunshot_Pistol1_Reverb|Combat/DesignedGunshot_Pistol3_Reverb", 0.45f, 1.0f, 0},
    /* explosion  */ {"Destruction/DesignedCarCrash1|Destruction/DesignedCarCrash2", 0.62f, 1.0f, 0},
    /* empty      */ {"Environment/SwitchButton1_Off|Environment/SwitchButton3_Off", 1.3f, 0.8f, 0},
    /* reload     */ {"Equipment/DesignedGunSoundReload1|Equipment/DesignedGunSoundReload2|Equipment/DesignedGunSoundReload3|Equipment/DesignedGunSoundReload4", 1.0f, 0.9f, 0},
    /* reload_end */ {"Equipment/DesignedGunSoundHandling1|Equipment/DesignedGunSoundHandling2|Equipment/DesignedGunSoundHandling3", 1.0f, 0.9f, 0},
    /* hit        */ {"Combat/DesignedPunch1|Combat/DesignedPunch2|Combat/DesignedPunch3|Combat/DesignedPunch4", 1.25f, 0.7f, 0},
    /* hurt       */ {"Human/HumanInjured1|Human/HumanInjured2|Human/HumanInjured3|Human/HumanInjured5", 1.0f, 0.9f, 0},
    /* pickup     */ {"Equipment/PlasticBagHandling2|Equipment/PlasticBagHandling3|Equipment/LargeBagHandling1|Equipment/LargeBagHandling3", 1.0f, 0.7f, 0.5f},
    /* click      */ {"Environment/SwitchButton1_On|Environment/SwitchButton1_Off", 1.1f, 0.6f, 0},
    /* tile_hit   */ {"Tools/DesignedAxe1|Tools/DesignedAxe2|Tools/DesignedAxe4|Tools/Hammer2|Tools/Hammer3", 1.0f, 0.55f, 0},
    /* tile_break */ {"Destruction/WoodSnap1|Destruction/WoodSnap2|Destruction/WoodSnap3|Destruction/WoodSnap4|Environment/Rockfall1|Environment/Rockfall2", 1.0f, 0.9f, 1.0f},
    /* enemy_die  */ {"Human/HumanBreathingOut1|Human/HumanBreathingOut2|Human/HumanBreathingOut3|Human/HumanInjured4", 0.8f, 0.9f, 0.9f},
    /* warning    */ {"Environment/GateWoodChain1|Environment/GateWoodChain2|Environment/GateWoodChain3", 0.7f, 1.0f, 0},
    /* night      */ {"Environment/BirdsCrowsDistantAmbienceLoop", 0.85f, 1.0f, 4.5f},
    /* shade      */ {"Human/HumanBreathingOut1|Human/HumanBreathingOut2|Human/HumanBreathingOut3", 0.55f, 0.8f, 0},
    /* toss       */ {"Clothing/ClothesSyntheticfabric3|Clothing/ClothesRubberMovement2", 1.2f, 0.8f, 0.4f},
    /* heal       */ {"Medicine/Bandage1|Medicine/BlisterPack1|Medicine/PillsBox1|Medicine/PillsBox2", 1.0f, 0.9f, 0},
    /* sell       */ {"Equipment/PaperDocument1|Equipment/PaperDocument2|Equipment/PaperDocument3", 1.0f, 0.9f, 0},
    /* door       */ {"Environment/OldDoorOpen|Environment/OldDoorClose", 1.0f, 0.8f, 1.0f},
    /* search     */ {"Destruction/CardboardBoxRip1|Environment/MetalCabinet1|Equipment/LargeBagZip2|Equipment/PlasticBox1", 1.0f, 0.8f, 0},
    /* melee      */ {"Combat/DesignedPunch1|Combat/DesignedPunch2|Combat/DesignedPunch3|Combat/DesignedPunch4", 0.85f, 0.9f, 0},
    /* step       */ {"Footsteps/FootstepsStoneDirt2|Footsteps/FootstepsStoneDirt3|Footsteps/FootstepsDryBeachTwigs1|Footsteps/FootstepsDryBeachTwigs3", 1.0f, 0.35f, 0.35f},
    /* step_in    */ {"Footsteps/FootstepsConcrete1|Footsteps/FootstepsConcrete2|Footsteps/FootstepsConcrete3|Footsteps/FootstepsConcrete4", 1.0f, 0.4f, 0},
    /* splash     */ {"Environment/WaterSplash1|Environment/WaterSplash2", 1.2f, 0.45f, 0.5f},
    // Cars (0.11v): the crash clips (also the explosions'), a metal clunk for the doors,
    // body knocks and glass for bullets, gravel under the tyres, a pour of fuel.
    /* car_crash  */ {"Destruction/DesignedCarCrash1|Destruction/DesignedCarCrash2", 1.05f, 0.8f, 0},
    /* car_door   */ {"Environment/MetalCabinet1", 1.25f, 0.7f, 0.6f},
    /* car_hit    */ {"Tools/Hammer2|Tools/Hammer3|Tools/DesignedPickaxe2|Tools/DesignedPickaxe3", 0.8f, 0.6f, 0.5f},
    /* car_glass  */ {"Destruction/LargeGlassMirrorCrunch1|Destruction/LargeGlassMirrorCrunch2", 1.2f, 0.45f, 0.4f},
    /* tyres      */ {"Environment/Gravelfall1|Environment/Gravelfall2|Environment/Gravelfall3", 1.3f, 0.35f, 0.45f},
    /* fuel       */ {"Equipment/LighterFluid1|Equipment/LighterFluid2|Equipment/LighterFluid3", 0.75f, 0.9f, 0},
};

static std::vector<ALuint> g_clips[Snd::COUNT];
static Rng g_pick{0x5EED5u};

// Decodes an Ogg Vorbis file (read into memory, so any path the OS accepts works)
// into mono 16-bit samples.
struct MemFile { const std::vector<char>* d; size_t pos; };
static size_t memRead(void* ptr, size_t size, size_t n, void* src) {
    MemFile* m = (MemFile*)src;
    size_t want = size * n, left = m->d->size() - m->pos;
    size_t k = std::min(want, left);
    std::memcpy(ptr, m->d->data() + m->pos, k);
    m->pos += k;
    return size ? k / size : 0;
}
static int memSeek(void* src, ogg_int64_t off, int whence) {
    MemFile* m = (MemFile*)src;
    ogg_int64_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? (ogg_int64_t)m->pos : (ogg_int64_t)m->d->size();
    ogg_int64_t p = base + off;
    if (p < 0 || p > (ogg_int64_t)m->d->size()) return -1;
    m->pos = (size_t)p;
    return 0;
}
static long memTell(void* src) { return (long)((MemFile*)src)->pos; }

static bool decodeOgg(const std::string& path, std::vector<int16_t>& out, int& rate) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<char> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    MemFile mf{&d, 0};
    ov_callbacks cb{memRead, memSeek, nullptr, memTell};
    OggVorbis_File vf;
    if (ov_open_callbacks(&mf, &vf, nullptr, 0, cb) != 0) return false;
    vorbis_info* vi = ov_info(&vf, -1);
    int ch = vi ? vi->channels : 1;
    rate = vi ? (int)vi->rate : 44100;
    out.clear();
    char buf[8192];
    int section = 0;
    for (;;) {
        long n = ov_read(&vf, buf, sizeof buf, 0, 2, 1, &section);
        if (n <= 0) break;
        const int16_t* s = (const int16_t*)buf;
        long frames = n / (2 * ch);
        for (long i = 0; i < frames; i++) {
            int acc = 0;
            for (int c = 0; c < ch; c++) acc += s[i * ch + c];
            out.push_back((int16_t)(acc / ch));
        }
    }
    ov_clear(&vf);
    return !out.empty();
}

static void loadSounds() {
    for (int i = 0; i < Snd::COUNT; i++) {
        const SoundDef& def = SOUND_DEFS[i];
        std::string list = def.clips;
        size_t at = 0;
        while (at <= list.size()) {
            size_t bar = list.find('|', at);
            std::string name = list.substr(at, bar == std::string::npos ? std::string::npos : bar - at);
            at = bar == std::string::npos ? list.size() + 1 : bar + 1;
            if (name.empty()) continue;
            std::vector<int16_t> pcm;
            int rate = 44100;
            if (!decodeOgg(dataPath(std::string(PACK_DIR) + name + ".ogg"), pcm, rate)) {
                std::fprintf(stderr, "[audio] missing %s.ogg\n", name.c_str());
                continue;
            }
            if (def.maxLen > 0 && pcm.size() > (size_t)(def.maxLen * rate)) {
                pcm.resize((size_t)(def.maxLen * rate));
                size_t fade = std::min(pcm.size(), (size_t)(rate / 12));
                for (size_t k = 0; k < fade; k++) {
                    size_t idx = pcm.size() - fade + k;
                    pcm[idx] = (int16_t)(pcm[idx] * (1.0f - (k + 1) / (float)fade));
                }
            }
            ALuint b = 0;
            alGenBuffers(1, &b);
            alBufferData(b, AL_FORMAT_MONO16, pcm.data(), (ALsizei)(pcm.size() * 2), rate);
            if (alGetError() == AL_NO_ERROR) g_clips[i].push_back(b);
            else alDeleteBuffers(1, &b);
        }
    }
}

// Background music: random shuffle of assets/Music/* via Windows MCI (mp3).
// MCI is used instead of OpenAL because mp3 needs a decoder; the OS handles it.
static float g_musicVol = 0.25f;
static bool g_muted = false;
void setMuted(bool m) { g_muted = m; }
static std::vector<std::string> g_tracks;
static std::string g_trackName;
static int g_trackIdx = -1;
static bool g_musicOpen = false;
static Rng g_musicRng{(uint64_t)std::time(nullptr) + 0x9e3779b9u};

static std::string musicPrefPath() { return dataPath("saves/music_volume.txt"); }

static void musicLoadPref() {
    std::ifstream f(musicPrefPath());
    float v = 0.25f;
    if (f >> v) g_musicVol = clampf(v, 0, 1);
}

static void musicSavePref() {
    ensureDir(dataPath("saves"));
#ifdef _WIN32
#endif
    {
        std::ofstream f(musicPrefPath());
        if (f) f << g_musicVol << "\n";
    }
    persistSaves();
}

static std::string baseName(const std::string& p) {
    size_t s = p.find_last_of("/\\");
    std::string b = s == std::string::npos ? p : p.substr(s + 1);
    size_t d = b.find_last_of('.');
    if (d != std::string::npos) b = b.substr(0, d);
    return b;
}

static void musicScan() {
    g_tracks.clear();
    for (const std::string& dir : {dataPath("assets/Music/"), dataPath("assets/Music\\"),
                                   dataPath("assets/music/"), dataPath("assets/music\\")}) {
#ifdef _WIN32
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA((dir + "*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::string n = fd.cFileName;
            std::string low = n;
            for (char& c : low) c = (char)std::tolower((unsigned char)c);
            bool ok = low.size() > 4 && (low.compare(low.size() - 4, 4, ".mp3") == 0 ||
                                         low.compare(low.size() - 4, 4, ".wav") == 0 ||
                                         low.compare(low.size() - 4, 4, ".ogg") == 0);
            if (ok) g_tracks.push_back(dir + n);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
#endif
        if (!g_tracks.empty()) break;
    }
    std::sort(g_tracks.begin(), g_tracks.end());
    // Show a random order first so launches differ.
    for (size_t i = g_tracks.size(); i > 1; i--) {
        size_t j = (size_t)g_musicRng.irange(0, (int)i - 1);
        std::swap(g_tracks[i - 1], g_tracks[j]);
    }
    std::fprintf(stderr, "[music] %zu track(s) in assets/Music\n", g_tracks.size());
}

#ifdef _WIN32
static void musicClose() {
    if (g_musicOpen) { mciSendStringA("close outbound_music", nullptr, 0, nullptr); g_musicOpen = false; }
}

static void musicPlayCurrent() {
    musicClose();
    if (g_muted) return;
    if (g_trackIdx < 0 || g_trackIdx >= (int)g_tracks.size()) return;
    const std::string& path = g_tracks[(size_t)g_trackIdx];
    char cmd[1024];
    std::snprintf(cmd, sizeof cmd, "open \"%s\" alias outbound_music", path.c_str());
    if (mciSendStringA(cmd, nullptr, 0, nullptr) != 0) {
        std::fprintf(stderr, "[music] cannot open %s\n", path.c_str());
        return;
    }
    g_musicOpen = true;
    g_trackName = baseName(path);
    int vol = (int)(g_musicVol * 1000.0f);  // MCI volume is 0..1000
    std::snprintf(cmd, sizeof cmd, "setaudio outbound_music volume to %d", vol);
    mciSendStringA(cmd, nullptr, 0, nullptr);
    mciSendStringA("play outbound_music", nullptr, 0, nullptr);
    std::fprintf(stderr, "[music] playing: %s\n", g_trackName.c_str());
}

static bool musicStopped() {
    if (!g_musicOpen) return true;
    char status[64] = {0};
    if (mciSendStringA("status outbound_music mode", status, sizeof status, nullptr) != 0) return true;
    return std::strncmp(status, "stop", 4) == 0;  // "stopped"
}
#endif

float musicVolume() { return g_musicVol; }

void setMusicVolume(float v) {
    g_musicVol = clampf(v, 0, 1);
#ifdef __EMSCRIPTEN__
    web_music_volume(g_musicVol);
#endif
#ifdef _WIN32
    if (g_musicOpen) {
        char cmd[128];
        std::snprintf(cmd, sizeof cmd, "setaudio outbound_music volume to %d", (int)(g_musicVol * 1000.0f));
        mciSendStringA(cmd, nullptr, 0, nullptr);
    }
#endif
    musicSavePref();
}

const char* musicTrack() {
#ifdef __EMSCRIPTEN__
    static std::string name;
    char* p = web_music_name();
    name = p ? p : "";
    std::free(p);
    return name.c_str();
#else
    return g_trackName.c_str();
#endif
}

void nextTrack() {
#ifdef __EMSCRIPTEN__
    web_music_next();
    return;
#endif
    if (g_tracks.empty()) return;
    if (g_tracks.size() == 1) g_trackIdx = 0;
    else {
        int n = g_trackIdx;
        while (n == g_trackIdx) n = g_musicRng.irange(0, (int)g_tracks.size() - 1);
        g_trackIdx = n;
    }
#ifdef _WIN32
    musicPlayCurrent();
#endif
}

void update() {
#ifdef __EMSCRIPTEN__
    return;                       // the <audio> element advances its own playlist
#endif
    if (g_tracks.empty()) return;
#ifdef _WIN32
    if (g_trackIdx < 0 || musicStopped()) {
        // Pick a random next track (avoids immediate repeats).
        if (g_tracks.size() == 1) g_trackIdx = 0;
        else {
            int n = g_trackIdx;
            while (n == g_trackIdx) n = g_musicRng.irange(0, (int)g_tracks.size() - 1);
            g_trackIdx = n;
        }
        musicPlayCurrent();
    }
#endif
}

static float g_sfxVol = 1.0f;

// ---- ambience (0.11v)
struct AmbDef { const char* clip; float gain, pitch; };
static const AmbDef AMB_DEFS[AMB_COUNT] = {
    {"Environment/BirdsCrowsDistantAmbienceLoop", 0.55f, 1.0f},   // birds: the countryside by day
    {"Environment/FastWaterfallStreamLoop", 0.5f, 1.25f},          // rain: a steady wash of water
    {"Environment/OverflowingWaterPipeLoop", 0.45f, 1.0f},         // drips: the catacombs, gutters, the bunker's pipes
    {"Environment/CoastalWavesOnRocksLoop", 0.3f, 0.55f},          // wind: surf slowed right down, gusting in the dark
};
static const char* AMB_SHOTS[AMS_COUNT] = {"Environment/BeeBuzz"};
static ALuint g_ambBuf[AMB_COUNT] = {}, g_ambSrc[AMB_COUNT] = {}, g_shotBuf[AMS_COUNT] = {}, g_shotSrc = 0;
static float g_ambCur[AMB_COUNT] = {}, g_ambTarget[AMB_COUNT] = {}, g_ambPitch[AMB_COUNT] = {1, 1, 1, 1};
static float g_ambAsked[AMB_COUNT] = {};   // seconds since a scene last asked for each
static float g_ambVol = 0.7f;
static bool g_ambLoaded = false;

static void loadAmbience() {
    for (int i = 0; i < AMB_COUNT + AMS_COUNT; i++) {
        const char* name = i < AMB_COUNT ? AMB_DEFS[i].clip : AMB_SHOTS[i - AMB_COUNT];
        std::vector<int16_t> pcm;
        int rate = 44100;
        if (!decodeOgg(dataPath(std::string(PACK_DIR) + name + ".ogg"), pcm, rate)) {
            std::fprintf(stderr, "[audio] missing %s.ogg\n", name);
            continue;
        }
        ALuint b = 0;
        alGenBuffers(1, &b);
        alBufferData(b, AL_FORMAT_MONO16, pcm.data(), (ALsizei)(pcm.size() * 2), rate);
        if (i < AMB_COUNT) g_ambBuf[i] = b; else g_shotBuf[i - AMB_COUNT] = b;
    }
    alGenSources(AMB_COUNT, g_ambSrc);
    alGenSources(1, &g_shotSrc);
    for (int i = 0; i < AMB_COUNT; i++) {
        alSourcei(g_ambSrc[i], AL_SOURCE_RELATIVE, AL_TRUE);
        alSource3f(g_ambSrc[i], AL_POSITION, 0, 0, -1);
        alSourcei(g_ambSrc[i], AL_LOOPING, AL_TRUE);
        if (g_ambBuf[i]) alSourcei(g_ambSrc[i], AL_BUFFER, (ALint)g_ambBuf[i]);
        alSourcef(g_ambSrc[i], AL_GAIN, 0);
    }
    alSourcei(g_shotSrc, AL_SOURCE_RELATIVE, AL_TRUE);
    g_ambLoaded = true;
}

void setAmbient(int bed, float level, float pitch) {
    if (bed < 0 || bed >= AMB_COUNT) return;
    g_ambTarget[bed] = clampf(level, 0, 1);
    g_ambPitch[bed] = pitch;
    g_ambAsked[bed] = 0;
}

static void loopTick(float dt);
void ambientTick(float dt) {
    if (!g_ok) return;
    loopTick(dt);
    if (!g_ambLoaded) return;
    for (int i = 0; i < AMB_COUNT; i++) {
        g_ambAsked[i] += dt;
        if (g_ambAsked[i] > 0.3f) g_ambTarget[i] = 0;   // nobody wants it any more
        // Slow fades: weather and places change gently.
        float rate = g_ambTarget[i] > g_ambCur[i] ? 0.6f : 0.9f;
        g_ambCur[i] += (g_ambTarget[i] - g_ambCur[i]) * std::min(1.0f, dt * rate);
        float gain = g_muted ? 0.0f : g_ambCur[i] * g_ambVol * AMB_DEFS[i].gain;
        ALint state;
        alGetSourcei(g_ambSrc[i], AL_SOURCE_STATE, &state);
        if (gain > 0.002f && g_ambBuf[i]) {
            alSourcef(g_ambSrc[i], AL_GAIN, gain);
            alSourcef(g_ambSrc[i], AL_PITCH, AMB_DEFS[i].pitch * g_ambPitch[i]);
            if (state != AL_PLAYING) alSourcePlay(g_ambSrc[i]);
        } else if (state == AL_PLAYING) {
            alSourcePause(g_ambSrc[i]);   // paused, so it picks up where it left off
        }
    }
}

void ambientShot(int shot, float volume, float pan) {
    if (!g_ok || !g_ambLoaded || g_muted || shot < 0 || shot >= AMS_COUNT || !g_shotBuf[shot]) return;
    float gain = volume * g_ambVol;
    if (gain < 0.01f) return;
    ALint state;
    alGetSourcei(g_shotSrc, AL_SOURCE_STATE, &state);
    if (state == AL_PLAYING) return;
    alSourcei(g_shotSrc, AL_BUFFER, (ALint)g_shotBuf[shot]);
    alSourcef(g_shotSrc, AL_GAIN, clampf(gain, 0, 1));
    alSourcef(g_shotSrc, AL_PITCH, 0.9f + 0.2f * g_pick.f());
    pan = clampf(pan, -1, 1);
    alSource3f(g_shotSrc, AL_POSITION, pan, 0, -std::sqrt(std::max(0.0f, 1.0f - pan * pan)));
    alSourcePlay(g_shotSrc);
}

// ---- looping voices (0.11v): seamless loops cut out of one-shot clips.
struct LoopDef { const char* clip; float from, to, fade, gain; };
static const LoopDef LOOP_DEFS[LOOP_COUNT] = {
    {"Environment/BeeBuzz", 0.35f, 1.15f, 0.2f, 0.9f},        // engine: the steady middle of the buzz
    {"Tools/CrowbarDrag1", 0.2f, 1.0f, 0.15f, 0.5f},          // screech: the scrape
};
static ALuint g_loopBuf[LOOP_COUNT] = {}, g_loopSrc[LOOP_VOICES] = {};
static int g_loopClip[LOOP_VOICES];
static float g_loopCur[LOOP_VOICES] = {}, g_loopTarget[LOOP_VOICES] = {}, g_loopAsked[LOOP_VOICES] = {};
static bool g_loopLoaded = false;

static void loadLoops() {
    for (int i = 0; i < LOOP_COUNT; i++) {
        const LoopDef& d = LOOP_DEFS[i];
        std::vector<int16_t> pcm;
        int rate = 44100;
        if (!decodeOgg(dataPath(std::string(PACK_DIR) + d.clip + ".ogg"), pcm, rate)) continue;
        size_t a = std::min(pcm.size(), (size_t)(d.from * rate)), b = std::min(pcm.size(), (size_t)(d.to * rate));
        size_t fade = (size_t)(d.fade * rate);
        if (b <= a + 2 * fade) continue;
        // The tail is crossfaded into the head, so the join is not heard.
        std::vector<int16_t> loop(pcm.begin() + a, pcm.begin() + (b - fade));
        for (size_t k = 0; k < fade; k++) {
            float t = (k + 0.5f) / fade;
            float v = loop[k] * t + pcm[b - fade + k] * (1.0f - t);
            loop[k] = (int16_t)clampf(v, -32768, 32767);
        }
        alGenBuffers(1, &g_loopBuf[i]);
        alBufferData(g_loopBuf[i], AL_FORMAT_MONO16, loop.data(), (ALsizei)(loop.size() * 2), rate);
    }
    alGenSources(LOOP_VOICES, g_loopSrc);
    for (int v = 0; v < LOOP_VOICES; v++) {
        alSourcei(g_loopSrc[v], AL_SOURCE_RELATIVE, AL_TRUE);
        alSourcei(g_loopSrc[v], AL_LOOPING, AL_TRUE);
        alSourcef(g_loopSrc[v], AL_GAIN, 0);
        g_loopClip[v] = -1;
    }
    g_loopLoaded = true;
}

void setLoop(int voice, int clip, float gain, float pitch, float pan) {
    if (!g_loopLoaded || voice < 0 || voice >= LOOP_VOICES || clip < 0 || clip >= LOOP_COUNT || !g_loopBuf[clip]) return;
    ALuint src = g_loopSrc[voice];
    if (g_loopClip[voice] != clip) {
        alSourceStop(src);
        alSourcei(src, AL_BUFFER, (ALint)g_loopBuf[clip]);
        g_loopClip[voice] = clip;
        g_loopCur[voice] = 0;
    }
    g_loopTarget[voice] = clampf(gain, 0, 1) * LOOP_DEFS[clip].gain;
    g_loopAsked[voice] = 0;
    alSourcef(src, AL_PITCH, clampf(pitch, 0.05f, 4.0f));
    pan = clampf(pan, -1, 1);
    alSource3f(src, AL_POSITION, pan, 0, -std::sqrt(std::max(0.0f, 1.0f - pan * pan)));
}

static void loopTick(float dt) {
    if (!g_loopLoaded) return;
    for (int v = 0; v < LOOP_VOICES; v++) {
        g_loopAsked[v] += dt;
        if (g_loopAsked[v] > 0.15f) g_loopTarget[v] = 0;
        g_loopCur[v] += (g_loopTarget[v] - g_loopCur[v]) * std::min(1.0f, dt * 10.0f);
        float gain = g_muted ? 0.0f : g_loopCur[v] * g_sfxVol;
        ALint state;
        alGetSourcei(g_loopSrc[v], AL_SOURCE_STATE, &state);
        if (gain > 0.002f && g_loopClip[v] >= 0) {
            alSourcef(g_loopSrc[v], AL_GAIN, gain);
            if (state != AL_PLAYING) alSourcePlay(g_loopSrc[v]);
        } else if (state == AL_PLAYING) {
            alSourcePause(g_loopSrc[v]);
        }
    }
}

float ambientVolume() { return g_ambVol; }
void setAmbientVolume(float v) { g_ambVol = clampf(v, 0, 1); }

bool init() {
    musicLoadPref();
#ifdef __EMSCRIPTEN__
    web_music_init(g_musicVol);
#endif
    musicScan();
#ifdef _WIN32
    if (!g_tracks.empty()) {
        g_trackIdx = g_musicRng.irange(0, (int)g_tracks.size() - 1);
        musicPlayCurrent();
    }
#endif
    g_device = alcOpenDevice(nullptr);
    if (!g_device) { std::fprintf(stderr, "Audio: no device\n"); return false; }
    g_context = alcCreateContext(g_device, nullptr);
    if (!g_context || !alcMakeContextCurrent(g_context)) { std::fprintf(stderr, "Audio: no context\n"); return false; }
    loadSounds();
    g_sources.resize(32);
    alGenSources((ALsizei)g_sources.size(), g_sources.data());
    // Direction only: every sound sits around the listener (relative, unit distance)
    // and distance is handled by playAt's own falloff, so nothing gets quieter here.
    alDistanceModel(AL_NONE);
    for (ALuint src : g_sources) alSourcei(src, AL_SOURCE_RELATIVE, AL_TRUE);
    g_ok = true;
    loadAmbience();
    loadLoops();
    return true;
}

void shutdown() {
#ifdef _WIN32
    musicClose();
#endif
    if (!g_ok) return;
    alDeleteSources((ALsizei)g_sources.size(), g_sources.data());
    if (g_loopLoaded) {
        alDeleteSources(LOOP_VOICES, g_loopSrc);
        for (ALuint b : g_loopBuf) if (b) alDeleteBuffers(1, &b);
    }
    if (g_ambLoaded) {
        alDeleteSources(AMB_COUNT, g_ambSrc);
        alDeleteSources(1, &g_shotSrc);
        for (ALuint b : g_ambBuf) if (b) alDeleteBuffers(1, &b);
        for (ALuint b : g_shotBuf) if (b) alDeleteBuffers(1, &b);
    }
    for (auto& v : g_clips) { if (!v.empty()) alDeleteBuffers((ALsizei)v.size(), v.data()); v.clear(); }
    alcMakeContextCurrent(nullptr);
    alcDestroyContext(g_context);
    alcCloseDevice(g_device);
    g_ok = false;
}


bool ready() { return g_ok; }
void setSfxVolume(float v) { g_sfxVol = clampf(v, 0, 1); }
float sfxVolume() { return g_sfxVol; }

// `pan` -1 (hard left) .. 1 (hard right), 0 in front of you.
static void playPanned(int snd, float volume, float pitch, float pan) {
    if (g_muted) return;
    if (!g_ok || snd < 0 || snd >= Snd::COUNT || g_clips[snd].empty()) return;
    const SoundDef& def = SOUND_DEFS[snd];
    volume *= g_sfxVol * def.gain;
    pitch *= def.pitch;
    if (volume <= 0.01f) return;
    const std::vector<ALuint>& clips = g_clips[snd];
    ALuint buffer = clips[clips.size() == 1 ? 0 : (size_t)g_pick.irange(0, (int)clips.size() - 1)];
    for (ALuint src : g_sources) {
        ALint state;
        alGetSourcei(src, AL_SOURCE_STATE, &state);
        if (state == AL_PLAYING) continue;
        alSourcei(src, AL_BUFFER, (ALint)buffer);
        alSourcef(src, AL_GAIN, clampf(volume, 0, 1));
        alSourcef(src, AL_PITCH, pitch);
        pan = clampf(pan, -1, 1);
        alSource3f(src, AL_POSITION, pan, 0, -std::sqrt(std::max(0.0f, 1.0f - pan * pan)));
        alSourcePlay(src);
        return;
    }
}

void play(int snd, float volume, float pitch) { playPanned(snd, volume, pitch, 0); }

void playAt(int snd, Vec2 pos, Vec2 listener, float volume, float pitch) {
    float d = dist(pos, listener);
    float att = clampf(1.0f - d / 700.0f, 0, 1);
    // Left or right of you by where it is on screen: fully to one side at the screen's
    // edge (and beyond), centred when it is on top of you.
    float half = std::max(120.0f, R::viewW() * 0.5f);
    float pan = clampf((pos.x - listener.x) / half, -1, 1) * 0.9f;
    playPanned(snd, volume * att * att, pitch, pan);
}

}  // namespace Audio
