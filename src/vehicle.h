// Cars (0.11v): the models the mechanic sells, how they drive, and what they look like.
//
// The art is minzinn's "Pixel Vehicles" pack, cut by tools/cut_vehicles.py into one
// strip of 48 turning frames per model and colour ("vehicles/<model>_<colour>"): frame
// 0 faces east, then clockwise in 7.5 degree steps, which is exactly how the game's
// angles run (y grows downwards). The car turns about the middle of its frame.
#pragma once
#include "assets.h"
#include "core.h"
#include <cstdint>
#include <vector>

struct World;

constexpr int CAR_MODELS = 10;
constexpr int CAR_COLORS = 8;
constexpr int CAR_MAX_MATS = 4;
constexpr float FUEL_CAN_LITRES = 12;   // what one Fuel Can puts in the tank
constexpr int FUEL_PRICE = 3;           // the mechanic's price per litre

struct CarMat { int item; int count; };

struct CarModel {
    const char* key;          // sprite key: vehicles/<key>_<colour>
    const char* name;
    const char* blurb;
    int price;
    CarMat mats[CAR_MAX_MATS];   // what the mechanic wants besides money (item 0 = none)
    int seats;                // everyone aboard, the driver too
    float hp;
    float tank;               // litres
    float topSpeed;           // px/s on the asphalt (you walk at 62, sprint at 96)
    float accel;              // px/s^2
    float turn;               // rad/s at full lock and speed
    float grip;               // how fast a slide dies away (1/s)
    float mass;               // 1 = a family car: how hard it hits and how little it cares
    float halfLen, halfWid;   // footprint for collisions (px)
    float offroad;            // top speed off the asphalt, as a share
    float economy;            // litres per 1000 px
};
const CarModel& carModel(int m);
const char* carColorName(int c);
Color carColorSwatch(int c);
const Assets::Sprite* carSprite(int model, int color);
// Which of the 48 frames shows a car facing `angle`.
int carFrame(float angle);
int carRepairCost(int model, float hpFrac);   // money to put a car back to full health

// A car in the world. The one you own is driven by your game; everybody else's is a
// copy kept up to date by the network.
struct Car {
    int owner = 0;            // the player whose car it is (co-op slot, 0 solo)
    int model = 0, color = 0;
    Vec2 pos;                 // middle
    float angle = PI / 2;
    Vec2 vel;
    float angVel = 0;
    float hp = 100, fuel = 0;
    float steer = 0;          // -1..1, eased toward the stick
    float throttle = 0;       // the pedal as last pressed (the engine's sound revs with it)
    float slip = 0;           // how fast it is sliding sideways (px/s): the tyres squeal
    bool driven = false;      // someone at the wheel
    bool wrecked = false;
    float hurtT = 0, smokeT = 0, tyreT = 0, hornT = 0;
    float speed() const { return length(vel); }
    float forwardSpeed() const { return dot(vel, fromAngle(angle)); }
    // Remote copies: where the latest state put it, eased toward.
    Vec2 netPos;
    float netAngle = 0;
    float netT = 0;           // seconds since that state
};

struct CarInput {
    float throttle = 0;       // -1 reverse/brake .. 1 accelerate
    float steer = 0;          // -1 left .. 1 right
    bool handbrake = false;
};

// What a step of driving ran into.
struct CarStepResult {
    float impact = 0;                     // speed lost against something solid (px/s)
    Vec2 hitAt;                           // where
    std::vector<std::pair<int, int>> smashed;   // fences, bushes, crates driven through
};

// Is a point inside the car's footprint (grown by `r`)?
bool carContains(const Car& c, Vec2 p, float r = 0);
// How far a circle at p with radius r sinks into the car (0 = not touching).
float carOverlap(const Car& c, Vec2 p, float r);
// One step of driving. `smash` lets the car break through light things (the host's
// world); otherwise they stop it like anything else.
void carStep(Car& c, World& w, const CarInput& in, float dt, bool smash, CarStepResult& out);
// The car being moved right now (the world's hook for moving things skips it).
extern const Car* g_carMoving;
// Is the car standing clear of everything at this position/angle?
bool carFits(const Car& c, const World& w, Vec2 pos, float angle);
// The car's depth for drawing (how far down the screen its footprint reaches).
float carDepth(const Car& c);
