// OUTBOUND - a top-down open world extraction shooter.
#include "art.h"
#include "atmosphere.h"
#include "assets.h"
#include "audio.h"
#include "voice.h"
#include "game.h"
#include "coop.h"
#include "net.h"
#include "options.h"
#include "gl.h"
#include "input.h"
#include "lang.h"
#include "local.h"
#include "render.h"
#include "sprites.h"
#include "ui.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <cstdio>
#include <fstream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

static void devBarricades(int argc, char** argv);

std::string g_dataDir;
std::string g_saveDir;

static bool fileExists(const std::string& p) {
    std::ifstream f(p);
    return (bool)f;
}

static bool dirExists(const std::string& p) {
#ifdef _WIN32
    DWORD a = GetFileAttributesA(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
    std::ifstream f(p);
    return (bool)f;
#endif
}

static void findDataDir() {
#ifdef __EMSCRIPTEN__
    g_dataDir = g_saveDir = "/";   // assets are preloaded at the root of MEMFS
    return;
#else
    std::string exeDir;
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    exeDir = std::string(buf, n);
    exeDir = exeDir.substr(0, exeDir.find_last_of("\\/") + 1);
#endif
    // The build copies every asset the game uses next to the exe (bin/assets), so a
    // zip of that folder is the whole game. Running from a source checkout also
    // works: the assets are then found one or two folders up.
    const std::string candidates[] = {exeDir, exeDir + "..\\", exeDir + "..\\..\\", ""};
    g_dataDir.clear();
    for (auto& c : candidates)
        if (dirExists(c + "assets\\sprites")) { g_dataDir = c; break; }
    if (g_dataDir.empty())
        for (auto& c : candidates)
            if (fileExists(c + "credits.txt") || fileExists(c + "assets/README.txt")) { g_dataDir = c; break; }
    if (g_dataDir.empty()) g_dataDir = exeDir;
    // Saves live beside the data, except that a development copy in bin/ keeps using
    // the saves/ folder a level up that it always used.
    g_saveDir = g_dataDir;
    if (!dirExists(g_dataDir + "saves") && dirExists(g_dataDir + "..\\saves") && fileExists(g_dataDir + "..\\credits.txt"))
        g_saveDir = g_dataDir + "..\\";
    std::fprintf(stderr, "[data] assets from %s, saves in %s\n", g_dataDir.c_str(), g_saveDir.c_str());
#endif
}

static void loadCredits() {
    G.credits.clear();
    std::ifstream f(dataPath("credits.txt"));
    std::string line;
    while (std::getline(f, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        G.credits.push_back(line);
    }
    while (!G.credits.empty() && G.credits.back().empty()) G.credits.pop_back();
}

// Developer launch flags for quick testing:
//   --raid               start a fresh game directly outside
//   --time=MINUTES       set the clock (e.g. --time=1310 for 21:50)
//   --shot=FILE@SECONDS  save the game frame as a .bmp at that time (repeatable); quits after the last one
//   --screen=NAME        open a menu screen directly: lang, slots, intro, credits, controls
//   --atbuilding         with --raid, start beside a roofed building
//   --inbuilding         with --raid, start inside the biggest building
//   --atloot             with --raid, start between two containers
//   --day=N              with --base, start on day N (to see how missions scale)
//   --slot=N             with --raid, save into slot N instead of a throwaway game
//   --continue=N         load slot N as the menu's Continue does (resumes a raid)
//   --lan-host           co-op host over localhost TCP (a throwaway game), no Steam needed
//   --lan-join           co-op guest of a --lan-host on this PC
//   --go-out=SECONDS     step outside that many seconds after reaching the bunker
//   --bot                wander in circles and shoot now and then (to watch sync)
//   --lang=en|es         use that language this run (the saved choice is untouched)
//   --promo              with --raid, a well-equipped character for screenshots
//   --elite              with --raid, carry every elite gun (an AK-47 in hand)
//   --bleed              with --raid, start out bleeding
//   --tutstep=N          with --base, open the bunker tour at step N
//   --window=WxH         open the window at that size (e.g. 1920x1080 for store shots)
//   --seed=N             with --raid, a fixed world seed (the same world every run)
//   --at=X,Y             with --raid, start standing on that tile
//   --barricades         with --raid or --defense, a ring of walls and gates round the bunker
//   --local=N            with --raid, N local co-op players (extras on controller slots)
struct DevShot { std::string path; float at; bool done; };
static std::vector<DevShot> g_devShots;
// --record=FILE.mp4@START@SECONDS (trailer capture): the game steps at exactly 1/30 s a
// frame however long a frame takes, and from START it pipes every frame into ffmpeg
// (on the PATH) for SECONDS, then quits.
static std::string g_recPath;
static float g_recStart = 0, g_recLen = 0;
static FILE* g_recPipe = nullptr;
void raid_devCrypt(bool introSeen, bool atDoor, int gate);   // raid.cpp (dev)
static float g_devGoOut = -1;       // --go-out
static int g_argc = 0;
void raid_devCars(const std::string& what);
static char** g_argv = nullptr;
static bool g_devNoGrain = false;   // --nograin
static int g_devMoney = -1;         // --money=N: start with that much (trailer shots)
static int g_devCoopCrypt = 0;      // --coop-crypt=1|2|3: once out, go to the catacomb gate (see raid_devCrypt)
static bool g_devBot = false;       // --bot
static bool g_devBotFar = false;    // --bot-far: walk off in a straight line instead
static float g_devStashOpen = -1;   // --stash-open=SECONDS: open the stash panel then
static float g_devStashPut = -1;    // --stash-put=SECONDS: put everything carried in the shared stash, then close it
static float g_devBaseT = 0;
static float g_devZombiesAt = -1;   // --promo --horde --zombies=N: spawn them then
static int g_devZombieCount = 0;

#ifndef __EMSCRIPTEN__
static void saveFrameBmp(const std::string& path, int w, int h) {
    std::vector<uint8_t> px(w * h * 3);
    glPixelStorei(0x0D05 /*GL_PACK_ALIGNMENT*/, 1);
    glReadPixels(0, 0, w, h, 0x80E0 /*GL_BGR*/, GL_UNSIGNED_BYTE, px.data());
    int rowPad = (4 - (w * 3) % 4) % 4;
    int dataSize = (w * 3 + rowPad) * h;
    std::ofstream f(path, std::ios::binary);
    auto u32 = [&](uint32_t v) { f.write((const char*)&v, 4); };
    auto u16 = [&](uint16_t v) { f.write((const char*)&v, 2); };
    f.write("BM", 2); u32(54 + dataSize); u32(0); u32(54);
    u32(40); u32(w); u32(h); u16(1); u16(24); u32(0); u32(dataSize); u32(2835); u32(2835); u32(0); u32(0);
    const char pad[3] = {0, 0, 0};
    for (int y = 0; y < h; y++) {  // BMP rows are bottom-up, same as glReadPixels
        f.write((const char*)&px[y * w * 3], w * 3);
        f.write(pad, rowPad);
    }
}
#endif

void menu_devScreen(const std::string& name);
int menu_navContext();
void base_devTutorial(int step);
void raid_devBotShoot();
void raid_devZombies(int count);

extern int g_devDifficulty, g_devMode;

static void applyDevArgs(int argc, char** argv) {
    float timeOverride = -1;
    bool raid = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a.rfind("--aim=", 0) == 0) { extern float g_devAim; g_devAim = (float)std::atof(a.c_str() + 6); }
        if (a.rfind("--throttle=", 0) == 0) { extern float g_devThrottle; g_devThrottle = (float)std::atof(a.c_str() + 11); }
        if (a == "--perf") { extern bool g_devPerf; g_devPerf = true; }
        if (a == "--ride") { extern bool g_devRide; g_devRide = true; }
        if (a == "--nocc") { extern bool g_devNoOcc; g_devNoOcc = true; }
        if (a.rfind("--steer=", 0) == 0) { extern float g_devSteer; g_devSteer = (float)std::atof(a.c_str() + 8); }
        if (a == "--hardcore") g_devDifficulty = DIFF_HARDCORE;
        else if (a == "--zmode") g_devMode = MODE_ZOMBIES;
        if (a == "--raid") raid = true;
        else if (a.rfind("--time=", 0) == 0) timeOverride = (float)std::atof(a.c_str() + 7);
        else if (a.rfind("--weather=", 0) == 0) Atmo::devForce(std::atoi(a.c_str() + 10));
        else if (a == "--nohud") G.devNoHud = true;
        else if (a == "--godmode") G.devGod = true;
        else if (a == "--nograin") g_devNoGrain = true;
        else if (a.rfind("--money=", 0) == 0) g_devMoney = std::atoi(a.c_str() + 8);
        else if (a.rfind("--record=", 0) == 0) {
            std::string v = a.substr(9);
            size_t p1 = v.find('@'), p2 = p1 == std::string::npos ? p1 : v.find('@', p1 + 1);
            if (p2 != std::string::npos) {
                g_recPath = v.substr(0, p1);
                g_recStart = (float)std::atof(v.c_str() + p1 + 1);
                g_recLen = (float)std::atof(v.c_str() + p2 + 1);
            }
        }
        else if (a.rfind("--shot=", 0) == 0) {
            size_t at = a.find_last_of('@');
            if (at != std::string::npos) g_devShots.push_back({a.substr(7, at - 7), (float)std::atof(a.c_str() + at + 1), false});
        }
    }
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--checktext") R::setTextCheck(true);
        if (a.rfind("--go-out=", 0) == 0) g_devGoOut = (float)std::atof(a.c_str() + 9);
        if (a.rfind("--coop-crypt=", 0) == 0) g_devCoopCrypt = std::atoi(a.c_str() + 13);
        if (a == "--bot") g_devBot = true;
        if (a == "--bot-far") g_devBot = g_devBotFar = true;
        if (a.rfind("--stash-open=", 0) == 0) g_devStashOpen = (float)std::atof(a.c_str() + 13);
        if (a.rfind("--stash-put=", 0) == 0) g_devStashPut = (float)std::atof(a.c_str() + 12);
    }
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--lan-host") {
            G.devNoSave = true;
            new_game();
            for (int k = 1; k < argc; k++)
                if (std::string(argv[k]) == "--horde") G.prof.nextHordeAt = absMinutes() + 20;
            for (int k = 1; k < argc; k++)
                if (std::string(argv[k]) == "--rivals") { G.prof.rivals = true; G.prof.nextHordeAt = 1e9f; }
            for (int k = 1; k < argc; k++) {
                // 0.11v: --day=N and --car=N for the host too (cars, the bigger map).
                std::string b = argv[k];
                if (b.rfind("--day=", 0) == 0) { G.prof.day = std::atoi(b.c_str() + 6); G.prof.nextHordeAt = absMinutes() + 900; }
                if (b.rfind("--car=", 0) == 0) {
                    int m = std::clamp(std::atoi(b.c_str() + 6), 0, CAR_MODELS - 1);
                    G.prof.cars[m].owned = true; G.prof.activeCar = m; G.prof.mechanicMet = true;
                }
            }
            Coop::beginDevHost(27777);
            Coop::startGame();
            return;
        }
        if (a == "--lan-join") { Coop::beginDevJoin(27777); return; }
    }
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a.rfind("--screen=", 0) == 0) { menu_devScreen(a.substr(9)); return; }
        if (a.rfind("--continue=", 0) == 0) {
            if (load_game(std::atoi(a.c_str() + 11) - 1)) {
                if (G.prof.inRaid) raid_resume();
                else base_enter(false);
            }
            return;
        }
    }
    int devSlot = 0;
    for (int i = 1; i < argc; i++)
        if (std::string(argv[i]).rfind("--slot=", 0) == 0) devSlot = std::atoi(argv[i] + 7);
    bool enemies = false, base = false, inv = false, mission = false, missionActive = false, atBuilding = false, inBuilding = false;
    bool horde = false, defense = false, squad = false, zombies = false, recruit = false;
    int sleepHorde = 0, dayOverride = 0, zombieCount = 9;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--enemies") enemies = true;
        else if (a == "--base") base = true;
        else if (a == "--inv") inv = true;
        else if (a == "--mission") mission = true;
        else if (a == "--mission-active") missionActive = true;
        else if (a == "--atbuilding") atBuilding = true;
        else if (a == "--inbuilding") atBuilding = inBuilding = true;
        else if (a == "--horde") horde = true;
        else if (a == "--defense") defense = true;
        else if (a == "--squad") squad = true;
        else if (a == "--recruit") recruit = true;
        else if (a.rfind("--sleephorde=", 0) == 0) sleepHorde = std::atoi(a.c_str() + 13);
        else if (a == "--zombies") enemies = false, zombies = true;
        else if (a.rfind("--zombies=", 0) == 0) enemies = false, zombies = true, zombieCount = std::atoi(a.c_str() + 10);
        else if (a.rfind("--day=", 0) == 0) dayOverride = std::atoi(a.c_str() + 6);
    }
    if (defense) {
        G.devNoSave = true;
        new_game();
        G.prof.money = 20000;
        devBarricades(argc, argv);
        defense_enter();
        return;
    }
    if (base || raid) G.devNoSave = true;
    if (base) {
        new_game();
        if (missionActive) { G.prof.mission.day = G.prof.day; G.prof.mission.type = 1; G.prof.mission.target = 5; G.prof.mission.reward = 180; }
        if (timeOverride >= 0) G.prof.timeMin = timeOverride;
        if (dayOverride > 0) { G.prof.day = dayOverride; G.prof.nextHordeAt = absMinutes() + 600; G.prof.tutorialDone = true; rollDailyMission(); }
        if (g_devMoney >= 0) G.prof.money = g_devMoney;
        base_enter(false);
        if (recruit) G.panel = Panel::Recruit;
        if (sleepHorde) {
            // Time a horde fought with nobody home, as when you sleep through one.
            G.prof.money = 1000;
            G.prof.hordeNum = sleepHorde;
            G.prof.nextHordeAt = absMinutes();
            double t0 = glfwGetTime();
            std::string note = raid_missedHorde();
            std::fprintf(stderr, "[dev] horde %d offscreen: %.2fs -> %s | money %d baseHp %.0f\n", sleepHorde,
                         glfwGetTime() - t0, note.c_str(), G.prof.money, G.prof.baseHp);
            G.quit = true;
        }
        if (mission) G.panel = Panel::Mission;
        else if (inv) G.panel = Panel::Inventory;
        for (int i = 1; i < argc; i++) {
            std::string a = argv[i];
            if (a.rfind("--tutstep=", 0) == 0) { base_devTutorial(std::atoi(a.c_str() + 10)); continue; }
            if (a.rfind("--panel=", 0) != 0) continue;
            std::string n = a.substr(8);
            const std::pair<const char*, Panel> PANELS[] = {
                {"stash", Panel::Stash}, {"trader", Panel::Trader}, {"workbench", Panel::Workbench}, {"bed", Panel::Bed},
                {"exit", Panel::ExitConfirm}, {"mission", Panel::Mission}, {"recruit", Panel::Recruit}, {"crafter", Panel::Crafter}, {"inv", Panel::Inventory},
                {"pause", Panel::Pause}, {"controls", Panel::Controls}, {"summary", Panel::Summary}, {"tutorial", Panel::Tutorial}, {"options", Panel::Options}};
            for (auto& [name, panel] : PANELS) if (n == name) G.panel = panel;
            if (n == "summary") { G.summary = RaidSummary(); G.summary.died = true; G.summary.cause = "Shot by a hostile."; G.summary.hordeNote = "Horde 3 repelled. Earned $420."; }
        }
        return;
    }
    if (!raid) return;
    new_game();
    for (int i = 1; i < argc; i++)   // --seed=N: the same world every run (world generation work)
        if (std::string(argv[i]).rfind("--seed=", 0) == 0) G.prof.worldSeed = std::strtoull(argv[i] + 7, nullptr, 10);
    if (missionActive) { G.prof.mission.day = G.prof.day; G.prof.mission.type = 1; G.prof.mission.target = 5; G.prof.mission.reward = 180; }
    if (timeOverride >= 0) G.prof.timeMin = timeOverride;
    addToSlots(G.prof.inv, makeItem(IT_GRENADE, 3));
    devBarricades(argc, argv);
    if (squad) {
        for (int t : {1, 3}) {
            Hireling h;
            h.name = t == 1 ? "Dana" : "Rook";
            h.tier = t;
            h.hp = hireTier(t).hp;
            h.guard = t == 3;
            G.prof.squad.push_back(h);
        }
    }
    // --horde-in=N: the next horde is due N game-minutes from the start.
    for (int i = 1; i < argc; i++)
        if (std::string(argv[i]).rfind("--horde-in=", 0) == 0) G.prof.nextHordeAt = absMinutes() + (float)std::atof(argv[i] + 11);
    // A horde right away, with a few extra turrets to watch.
    if (horde) {
        G.prof.nextHordeAt = absMinutes();
        const int extra[4][3] = {{-6, -6, TT_AUTO}, {6, -6, TT_FLAME}, {-7, 2, TT_LASER}, {7, 2, TT_ROCKET}};
        for (auto& e : extra) {
            Turret t;
            t.dx = e[0]; t.dy = e[1]; t.type = e[2];
            t.hp = turretStats(t).maxHp;
            G.prof.turrets.push_back(t);
        }
    }
    G.prof.weapons[1] = makeItem(IT_RIFLE);
    addToSlots(G.prof.inv, makeItem(IT_AMMO_RIFLE, 90));
    for (int i = 1; i < argc; i++)
        if (std::string(argv[i]) == "--promo") {
            // Kitted out for screenshots: a rifle in hand, plates on and a sturdy body.
            G.prof.curWeapon = 1;
            G.prof.up[UP_VITALITY] = 5;
            G.prof.up[UP_TOUGH] = 4;
            G.prof.armor = makeItem(IT_VEST_HEAVY);
            G.prof.backpack = makeItem(IT_PACK_LARGE);
            G.prof.hp = G.prof.maxHp();
            G.prof.laserOwned = G.prof.laserOn = true;
            for (Item& w : G.prof.weapons) if (!w.empty()) w.flags |= ITEMF_LASER;
            G.devClean = true;
        }
    if (dayOverride > 0) { G.prof.day = dayOverride; G.prof.nextHordeAt = absMinutes() + 600; }   // --day=N raid
    if (g_devMoney >= 0) G.prof.money = g_devMoney;
    bool devBleed = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--elite") {
            // Every elite gun, to look at: an AK-47 in hand, the rest in the pockets.
            G.prof.weapons[1] = makeItem(IT_AK47);
            G.prof.curWeapon = 1;
            G.prof.backpack = makeItem(IT_PACK_LARGE);
            for (int id : {IT_M92, IT_LUGER, IT_MAGNUM, IT_MP5, IT_M15, IT_M24}) addToSlots(G.prof.inv, makeItem(id), G.prof.invCapacity());
        }
        if (a == "--bleed") devBleed = true;
        // 0.11v cars: --car=N owns model N (and has met the mechanic), --carcolor=C.
        if (a.rfind("--car=", 0) == 0) {
            int m = std::clamp(std::atoi(a.c_str() + 6), 0, CAR_MODELS - 1);
            G.prof.cars[m].owned = true;
            G.prof.activeCar = m;
            G.prof.mechanicMet = true;
        }
        if (a.rfind("--carcolor=", 0) == 0 && G.prof.activeCar >= 0) G.prof.cars[G.prof.activeCar].color = std::clamp(std::atoi(a.c_str() + 11), 0, CAR_COLORS - 1);
        if (a == "--mechmet") G.prof.mechanicMet = true;
    }
    raid_start();
    if (devBleed) { G.player.bleedT = 60; G.prof.hp = 80; }
    if (atBuilding && !G.world.buildings.empty()) {
        const Building* big = &G.world.buildings[0];
        for (const Building& bb : G.world.buildings)
            if (bb.w * bb.h > big->w * big->h) big = &bb;
        const Building& b = *big;
        G.player.pos = World::tileCenter(b.x0 + b.w / 2, b.y0 + b.h + 3);
        if (inBuilding)   // --inbuilding: on open floor inside it instead
            for (int y = b.y0 + b.h - 2; y > b.y0; y--)
                if (G.world.at(b.x0 + b.w / 2, y).solid == S_NONE) { G.player.pos = World::tileCenter(b.x0 + b.w / 2, y); break; }
        for (int i = 1; i < argc; i++)   // --opendoors: every door of it open
            if (std::string(argv[i]) == "--opendoors")
                for (int d = 0; d < b.doorCount; d++) {
                    G.world.at(b.doorX[d], b.doorY[d]).solid = S_DOOR_OPEN;
                    extern float g_devAim;
                    g_devAim = angleOf(World::tileCenter(b.doorX[d], b.doorY[d]) - G.player.pos);
                }
        G.cam = G.player.pos - Vec2(R::viewW() / 2.0f, R::viewH() / 2.0f);
        G.world.reveal(G.player.pos, 20);
    }
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) != "--atloot") continue;
        // Stand between the closest pair of containers, to see the interaction list.
        const auto& cs = G.world.containers;
        float best = 1e9f;
        for (size_t a = 0; a < cs.size(); a++)
            for (size_t b = a + 1; b < cs.size(); b++) {
                float d = dist(cs[a].pos, cs[b].pos);
                if (d < best && d > 8) { best = d; G.player.pos = (cs[a].pos + cs[b].pos) * 0.5f + Vec2(0, 6); }
            }
        G.cam = G.player.pos - Vec2(R::viewW() / 2.0f, R::viewH() / 2.0f);
        G.world.reveal(G.player.pos, 20);
    }
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--locator") G.prof.locatorDay = G.prof.day;
        // --searching: open the nearest unsearched container, still being searched.
        if (a == "--searching") {
            int best = -1;
            float bd = 1e9f;
            for (size_t k = 0; k < G.world.containers.size(); k++) {
                const Container& c = G.world.containers[k];
                float d = dist(c.pos, G.player.pos);
                if (!c.searched && !c.removed && c.searchTime > 30 * 0 + 0.5f && d < bd) { bd = d; best = (int)k; }
            }
            if (best >= 0) { G.world.containers[best].searchTime = 9999; G.lootContainer = best; G.searchT = 3000; G.panel = Panel::Loot; }
        }
        if (a == "--atpuddle") { G.player.pos = Atmo::nearestPuddle(G.world, G.player.pos) + Vec2(0, -4); G.cam = G.player.pos - Vec2(R::viewW() / 2.0f, R::viewH() / 2.0f); }
        if (a == "--bloodpool") { void raid_devPool(); raid_devPool(); }
        if (a.rfind("--at=", 0) == 0) {   // --at=X,Y: stand on that tile (world generation work)
            int tx = 0, ty = 0;
            if (std::sscanf(a.c_str() + 5, "%d,%d", &tx, &ty) == 2) {
                G.player.pos = World::tileCenter(tx, ty);
                G.cam = G.player.pos - Vec2(R::viewW() / 2.0f, R::viewH() / 2.0f);
                G.world.reveal(G.player.pos, 20);
            }
        }
        if (a.rfind("--local=", 0) == 0) Local::devSeats(std::atoi(a.c_str() + 8));   // --local=N: local co-op test
        if (a == "--athatch") { G.player.pos = G.world.homePos + Vec2(0, 24); G.cam = G.player.pos - Vec2(R::viewW() / 2.0f, R::viewH() / 2.0f); }
        if (a == "--atcompound") { G.player.pos = G.world.homePos + Vec2(0, 76); G.cam = G.player.pos - Vec2(R::viewW() / 2.0f, R::viewH() / 2.0f); }
        // 0.11v: --driving, --atcity, --atfloor, --atgarage, --panel=mechanic.
        if (a == "--driving" || a == "--atcity" || a == "--atfloor" || a == "--atgarage" || a == "--panel=mechanic") { void raid_devCars(const std::string&); raid_devCars(a); }
        if (a == "--crypt") raid_devCrypt(false, false, 0);
        else if (a == "--crypt=seen") raid_devCrypt(true, false, 0);
        else if (a == "--crypt=door") raid_devCrypt(true, true, 0);
        else if (a == "--crypt=gate") raid_devCrypt(true, false, 1);        // in the last room, at the gate
        else if (a == "--crypt=gateback") raid_devCrypt(true, false, 2);    // on the far side of it
        else if (a == "--crypt=gateopen") raid_devCrypt(true, false, 3);    // at the gate, raising it
    }
    if (enemies) {
        void raid_devSpawn(int count);
        raid_devSpawn(6);
    }
    if (zombies) {
        // For promo shots they come in just as the horde starts, under its red light.
        if (G.devClean && horde) g_devZombiesAt = 8.6f, g_devZombieCount = zombieCount;
        else raid_devZombies(zombieCount);
    }
    if (inv) G.panel = Panel::Inventory;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--panel=pause") G.panel = Panel::Pause;
        if (a == "--panel=map") G.panel = Panel::Map;
        if (a == "--panel=quit") G.panel = Panel::QuitConfirm;
    }
    if (devSlot >= 1 && devSlot <= SAVE_SLOTS) {
        G.devNoSave = false;
        G.saveSlot = devSlot - 1;
        raid_saveState();
    }
}

// --barricades: a ring of walls round the bunker with gates across the way out, to
// look at and to throw a horde against.
static void devBarricades(int argc, char** argv) {
    bool on = false;
    for (int i = 1; i < argc; i++) on = on || std::string(argv[i]) == "--barricades";
    if (!on) return;
    G.prof.barricades.clear();
    const int R = 5;
    for (int dy = -R; dy <= R; dy++)
        for (int dx = -R; dx <= R; dx++) {
            if (std::abs(dx) != R && std::abs(dy) != R) continue;
            Barricade b;
            b.dx = dx; b.dy = dy;
            bool lane = dy == R && std::abs(dx) <= 1, side = dx == -R && std::abs(dy) <= 0;
            b.type = lane ? BT_WOOD_GATE : side ? BT_REINF_GATE : dy == -R ? BT_REINF_WALL : BT_WOOD_WALL;
            G.prof.barricades.push_back(b);
        }
}

// One frame of the game. The desktop build spins this in a while loop; the browser
// hands control back between frames, so it calls this from requestAnimationFrame.
namespace {
GLFWwindow* g_window = nullptr;
double g_last = 0;

void frame() {
    GLFWwindow* window = g_window;
    int fbw, fbh;
    glfwPollEvents();
    glfwGetFramebufferSize(window, &fbw, &fbh);
    if (fbw == 0 || fbh == 0) {
#ifndef __EMSCRIPTEN__
        glfwWaitEvents();
        g_last = glfwGetTime();
#endif
        return;
    }
    R::resize(fbw, fbh);
    G.winW = fbw;
    G.winH = fbh;
    double now = glfwGetTime();
    float dt = (float)std::min(now - g_last, 1.0 / 30.0);
    g_last = now;
    if (!g_recPath.empty()) dt = 1.0f / 30.0f;   // recording: game time runs by frames
    G.realTime += dt;
    G.frameDt = dt;

    Input::update(window, R::pixelScale());
    Input::setPadCursor(true);   // the raid turns it off again while you are playing
    UI::beginFrame();

#ifndef __EMSCRIPTEN__
    if (Input::pressed(GLFW_KEY_F11)) Options::toggleFullscreen();
#endif

    Audio::update();
    Audio::ambientTick(dt);
    Net::update();
    Coop::update(dt);
    Voice::update(dt);
    // Local co-op (0.12v): players dropping in and out, and the lobby's players joining
    // once the game it started has loaded.
    if (Local::planPending()) Local::applyPlan();
    Local::update(dt);

    // A controller drives menus and panels by jumping between their widgets (see
    // UI::padNavigate); gameplay keeps the sticks for moving and aiming.
    bool padMenu = G.scene == Scene::Defense || (G.scene == Scene::Base && G.panel != Panel::None) ||
                   (G.scene == Scene::Raid && G.panel != Panel::None && G.panel != Panel::Map) ||
                   (G.scene != Scene::Base && G.scene != Scene::Raid && G.scene != Scene::Defense);
    Input::setPadMenu(padMenu);
    if (G.scene != Scene::Raid) Input::setDpadWalk(true);

    switch (G.scene) {
    case Scene::Menu:
    case Scene::Credits:
    case Scene::Controls:
    case Scene::Intro:
    case Scene::Slots:
    case Scene::Lobby:
    case Scene::Splash:
    case Scene::LocalLobby:
        menu_update(dt);
        if (G.scene == Scene::Base) base_draw();
        else menu_draw();
        break;
    case Scene::Base:
        base_update(dt);
        if (G.scene == Scene::Base) base_draw();
        else if (G.scene == Scene::Raid) raid_draw();
        else menu_draw();
        break;
    case Scene::Raid:
        raid_update(dt);
        if (G.scene == Scene::Raid) raid_draw();
        else base_draw();
        break;
    case Scene::Defense:
        defense_update(dt);
        if (G.scene == Scene::Defense) defense_draw();
        else base_draw();
        break;
    }

#ifndef __EMSCRIPTEN__
    // Dev automation for watching co-op sync with two copies of the game.
    if (G.scene == Scene::Base) {
        g_devBaseT += dt;
        if (G.panel == Panel::Tutorial && (g_devGoOut >= 0 || g_devStashOpen >= 0 || g_devStashPut >= 0)) G.panel = Panel::None;
        if (g_devGoOut >= 0 && g_devBaseT >= g_devGoOut && G.panel == Panel::None) {
            g_devGoOut = -1;
            raid_start();
            if (g_devCoopCrypt) raid_devCrypt(true, false, g_devCoopCrypt);
            for (int k = 1; k < g_argc; k++) {
                std::string b = g_argv[k];
                if (b == "--driving") raid_devCars(b);
            }
        }
        if (g_devStashOpen >= 0 && g_devBaseT >= g_devStashOpen) {
            g_devStashOpen = -1;
            G.panel = Panel::Stash;
            Coop::stashOpen();
        }
        if (g_devStashPut >= 0 && g_devBaseT >= g_devStashPut) {
            if (G.panel != Panel::Stash) { G.panel = Panel::Stash; Coop::stashOpen(); }
            else if (std::vector<Item>* sh = Coop::sharedStash()) {
                for (int i = 0; i < G.prof.invCapacity(); i++)
                    if (!G.prof.inv[i].empty()) moveItem(G.prof.inv, i, *sh, STASH_SLOTS);
                std::fprintf(stderr, "[dev] put everything in the shared stash\n");
                g_devStashPut = -1;
                G.panel = Panel::None;
            }
        }
    }
    if (g_devZombiesAt >= 0 && G.scene == Scene::Raid && G.realTime >= g_devZombiesAt) {
        g_devZombiesAt = -1;
        raid_devZombies(g_devZombieCount);
    }
        if (g_devBot && G.scene == Scene::Raid) {
        float t = G.realTime * 0.6f;
        Vec2 dir = g_devBotFar ? Vec2(0.03f, 1.0f) : Vec2(std::cos(t), std::sin(t));
        G.player.pos = G.world.move(G.player.pos, dir * 60.0f * dt, 5);
        G.player.angle = t + 1.2f;
        G.player.moving = true;
        raid_devBotShoot();
    }
#endif
    UI::padNavigate((int)G.scene * 64 + (int)G.panel + 4096 * menu_navContext());
    // The pack's pointer (UI/Menu/Cursor) stands in for the system's, drawn by the UI
    // next frame; out in the world the crosshair does, and in local co-op every
    // player's own pointer.
    bool hideCursor = G.scene == Scene::Raid && G.panel == Panel::None;
    UI::setCursor(!hideCursor && !Local::active() && glfwGetWindowAttrib(window, GLFW_HOVERED));
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);

    R::present(G.lighting, G.drawCam, fbw, fbh);
#ifndef __EMSCRIPTEN__
    if (!g_recPath.empty() && G.realTime >= g_recStart) {
        if (!g_recPipe) {
            char cmd[1024];
            std::snprintf(cmd, sizeof cmd,
                          "ffmpeg -y -loglevel error -f rawvideo -pix_fmt rgb24 -s %dx%d -r 30 -i - -vf vflip -c:v libx264 -preset medium -crf 14 -pix_fmt yuv420p \"%s\"",
                          fbw, fbh, g_recPath.c_str());
            g_recPipe = _popen(cmd, "wb");
            if (!g_recPipe) { std::fprintf(stderr, "[rec] could not start ffmpeg\n"); g_recPath.clear(); }
        }
        if (g_recPipe) {
            static std::vector<uint8_t> px;
            px.resize((size_t)fbw * fbh * 3);
            glPixelStorei(0x0D05 /*GL_PACK_ALIGNMENT*/, 1);
            glReadPixels(0, 0, fbw, fbh, 0x1907 /*GL_RGB*/, GL_UNSIGNED_BYTE, px.data());
            std::fwrite(px.data(), 1, px.size(), g_recPipe);
            if (G.realTime >= g_recStart + g_recLen) {
                _pclose(g_recPipe);
                g_recPipe = nullptr;
                std::fprintf(stderr, "[rec] wrote %s\n", g_recPath.c_str());
                G.quit = true;
            }
        }
    }
    if (!g_devShots.empty()) {
        bool allDone = true;
        for (auto& s : g_devShots) {
            if (!s.done && G.realTime >= s.at) { saveFrameBmp(s.path, fbw, fbh); s.done = true; }
            allDone = allDone && s.done;
        }
        if (allDone) G.quit = true;
    }
#endif
    glfwSwapBuffers(window);
}
}  // namespace

#ifdef __EMSCRIPTEN__
// The browser's filesystem starts empty and loads asynchronously, so the game waits
// for IndexedDB to be mounted before it looks for saved games.
static bool g_savesReady = false;

extern "C" EMSCRIPTEN_KEEPALIVE void web_saves_ready() {
    g_savesReady = true;
    L::init();
    migrate_saves();
    menu_init();
}

static void webFrame() {
    if (!g_savesReady) return;
    frame();
}
#endif

int main(int argc, char** argv) {
    g_argc = argc;
    g_argv = argv;
    findDataDir();
    if (!glfwInit()) {
        std::fprintf(stderr, "Failed to initialize GLFW\n");
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    for (int i = 1; i < argc; i++)
        if (std::string(argv[i]).rfind("--shot=", 0) == 0) glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
    int winW = 1280, winH = 720;
    for (int i = 1; i < argc; i++)
        if (std::string(argv[i]).rfind("--window=", 0) == 0) std::sscanf(argv[i] + 9, "%dx%d", &winW, &winH);
    GLFWwindow* window = glfwCreateWindow(winW, winH, "Outbound", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "Failed to create an OpenGL 3.3 window\n");
        glfwTerminate();
        return 1;
    }
    G.window = window;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    if (!gl_load()) {
        std::fprintf(stderr, "Failed to load OpenGL functions\n");
        return 1;
    }
    Sprites::registerFallbacks();
    if (!Assets::load()) std::fprintf(stderr, "Asset atlas did not fit\n");
    Sprites::resolve();
    Art::init();
    R::init();
    // Read straight from the command line: the dev flags are applied further down, and
    // the grain has to be off before the first frame is drawn.
    for (int i = 1; i < argc; i++) if (std::string(argv[i]) == "--nograin") g_devNoGrain = true;
    R::setNoGrain(g_devNoGrain);
    int fbw, fbh;
    glfwGetFramebufferSize(window, &fbw, &fbh);
    R::resize(fbw, fbh);
    for (int i = 1; i < argc; i++) if (std::string(argv[i]) == "--mute") Audio::setMuted(true);
    Audio::init();
    Voice::init();
    Options::load();
    Options::apply(window);
    for (int i = 1; i < argc; i++)   // --display=0|1|2: windowed, fullscreen, borderless (saved like the menu's)
        if (std::string(argv[i]).rfind("--display=", 0) == 0) Options::setDisplayMode(std::atoi(argv[i] + 10));
    Input::init(window);
    loadCredits();
#ifndef __EMSCRIPTEN__
    L::init();
    migrate_saves();
    menu_init();
    Net::init(argc, argv);
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--lang=en") L::set(LANG_EN);
        if (a == "--lang=es") L::set(LANG_ES);
    }
    // A plain launch opens on the engine card; test launches (any -- flag) go straight in.
    {
        bool devLaunch = false;
        bool splash = false;
        for (int i = 1; i < argc; i++) {
            if (std::string(argv[i]).rfind("--", 0) == 0) devLaunch = true;
            if (std::string(argv[i]) == "--splash") splash = true;   // show it anyway
        }
        if (!devLaunch || splash) menu_splash();
    }
    applyDevArgs(argc, argv);
#else
    (void)argc; (void)argv;   // web_saves_ready() finishes start-up once IDBFS is in
#endif

    g_window = window;
    g_last = glfwGetTime();

#ifdef __EMSCRIPTEN__
    // Saves live in IndexedDB so they survive a reload. Mounting is asynchronous;
    // web_saves_ready() finishes start-up once the contents are in.
    EM_ASM({
        FS.mkdir('/saves');
        FS.mount(IDBFS, {}, '/saves');
        FS.syncfs(true, function(err) { ccall('web_saves_ready', null, [], []); });
    });
    emscripten_set_main_loop(webFrame, 0, 1);
    return 0;
#else
    while (!glfwWindowShouldClose(window) && !G.quit) frame();

    if (Coop::active()) {
        // The host's save holds the whole session; a guest's character goes to it.
        if (Coop::host() && G.scene == Scene::Raid) raid_saveState();
        Coop::leave("");
    } else if (G.scene == Scene::Base || G.scene == Scene::Defense) save_game();
    else if (G.scene == Scene::Raid) raid_saveState();
    Net::shutdown();
    Voice::shutdown();
    Audio::shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
#endif
}
