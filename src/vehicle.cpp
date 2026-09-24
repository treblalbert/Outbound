#include "vehicle.h"
#include "items.h"
#include "world.h"
#include <string>

// Everyday cars, cheapest first. Each one costs more than the last and does more:
// more seats, more health, a bigger tank, more speed and better handling.
//                key          name              price  materials                                                          seats hp   tank top  accel turn  grip mass len   wid   offrd econ
const Assets::Sprite* carSprite(int model, int color);
static CarModel MODELS[CAR_MODELS] = {
    {"micro", "Micro", "A two-seat city car. Slow and thin-skinned, but it runs.", 2000,
     {{IT_NONE, 0}, {IT_NONE, 0}, {IT_NONE, 0}, {IT_NONE, 0}},                                       2, 520, 25, 175, 150, 2.9f, 9.0f, 0.70f, 9.0f, 6.0f, 0.72f, 1.00f},
    {"hatchback", "Hatchback", "Four seats and a little more pull.", 4500,
     {{IT_SCRAP, 8}, {IT_BOLTS, 6}, {IT_NONE, 0}, {IT_NONE, 0}},                                     4, 680, 35, 195, 170, 2.7f, 8.5f, 0.90f, 18.5f, 9.5f, 0.72f, 1.05f},
    {"sedan", "Sedan", "A family car: roomy, steady, quicker on the open road.", 8000,
     {{IT_SCRAP, 12}, {IT_WIRES, 6}, {IT_BATTERY, 2}, {IT_NONE, 0}},                                  4, 820, 45, 220, 185, 2.55f, 8.0f, 1.00f, 19.5f, 8.5f, 0.70f, 1.10f},
    {"wagon", "Station Wagon", "A long sedan with room for five.", 12000,
     {{IT_SCRAP, 14}, {IT_BOLTS, 10}, {IT_TAPE, 4}, {IT_BATTERY, 2}},                                 5, 900, 50, 222, 180, 2.5f, 8.0f, 1.10f, 20.5f, 8.5f, 0.72f, 1.15f},
    {"pickup", "Pickup", "Tough, and happier off the road than any car.", 16500,
     {{IT_SCRAP, 18}, {IT_BOLTS, 12}, {IT_BATTERY, 3}, {IT_FUEL, 2}},                                 5, 1100, 60, 225, 190, 2.45f, 7.5f, 1.35f, 20.0f, 8.5f, 0.86f, 1.25f},
    {"jeep", "Jeep", "Short, quick and sure-footed: the best handling there is.", 21000,
     {{IT_SCRAP, 20}, {IT_WIRES, 8}, {IT_CIRCUIT, 3}, {IT_BATTERY, 3}},                               4, 1150, 55, 235, 215, 2.9f, 9.5f, 1.25f, 14.0f, 8.5f, 0.97f, 1.25f},
    {"minivan", "Minivan", "Seven seats: room for a whole crew.", 26000,
     {{IT_SCRAP, 18}, {IT_WIRES, 10}, {IT_TAPE, 6}, {IT_CIRCUIT, 3}},                                 7, 1100, 65, 228, 185, 2.5f, 8.0f, 1.35f, 19.5f, 8.5f, 0.74f, 1.30f},
    {"suv", "SUV", "Fast, heavy and hard to stop, on the road or off it.", 33000,
     {{IT_SCRAP, 22}, {IT_BOLTS, 12}, {IT_CIRCUIT, 4}, {IT_BATTERY, 4}},                              6, 1350, 75, 250, 220, 2.6f, 8.5f, 1.55f, 18.5f, 9.0f, 0.90f, 1.40f},
    {"van", "Van", "Eight seats behind a steel front.", 40000,
     {{IT_SCRAP, 25}, {IT_WIRES, 14}, {IT_CIRCUIT, 6}, {IT_FUEL, 4}},                                 8, 1550, 85, 238, 190, 2.45f, 8.0f, 1.70f, 17.0f, 8.5f, 0.80f, 1.45f},
    {"boxtruck", "Box Truck", "A moving fortress: nine seats, a huge tank, and nothing stops it.", 50000,
     {{IT_SCRAP, 30}, {IT_BOLTS, 20}, {IT_CIRCUIT, 8}, {IT_BATTERY, 5}},                              9, 2200, 120, 235, 165, 2.25f, 7.5f, 2.40f, 24.5f, 9.0f, 0.80f, 1.70f},
};

// The footprint comes from the art itself (0.11v): the car's length is how wide the
// east-facing frame is, its width how wide the south-facing one is.
static void measure(int m) {
    static bool done[CAR_MODELS] = {};
    if (done[m]) return;
    const Assets::Sprite* s = carSprite(m, 0);
    if (!s) return;
    done[m] = true;
    auto span = [&](int frame) {
        int lo = s->w, hi = -1;
        for (int y = 0; y < s->h; y++)
            for (int x = 0; x < s->w; x++)
                if (s->opaqueAt(frame, x, y)) { lo = std::min(lo, x); hi = std::max(hi, x); }
        return hi >= lo ? hi - lo + 1 : 0;
    };
    int len = span(0), wid = span(12);
    if (len > 4) MODELS[m].halfLen = len * 0.5f - 1.0f;
    if (wid > 4) MODELS[m].halfWid = wid * 0.5f - 1.0f;
}

const CarModel& carModel(int m) {
    m = std::clamp(m, 0, CAR_MODELS - 1);
    measure(m);
    return MODELS[m];
}

static const char* COLOR_NAMES[CAR_COLORS] = {"Black", "Blue", "Brown", "Green", "Magenta", "Red", "White", "Yellow"};
static const char* COLOR_KEYS[CAR_COLORS] = {"black", "blue", "brown", "green", "magenta", "red", "white", "yellow"};
const char* carColorName(int c) { return COLOR_NAMES[std::clamp(c, 0, CAR_COLORS - 1)]; }
Color carColorSwatch(int c) {
    static const Color SW[CAR_COLORS] = {Color(0.16f, 0.16f, 0.2f), Color(0.25f, 0.4f, 0.75f), Color(0.5f, 0.33f, 0.2f), Color(0.3f, 0.55f, 0.3f),
                                         Color(0.72f, 0.25f, 0.6f), Color(0.8f, 0.2f, 0.2f), Color(0.9f, 0.9f, 0.88f), Color(0.92f, 0.78f, 0.2f)};
    return SW[std::clamp(c, 0, CAR_COLORS - 1)];
}

const Assets::Sprite* carSprite(int model, int color) {
    static const Assets::Sprite* cache[CAR_MODELS][CAR_COLORS] = {};
    static bool looked[CAR_MODELS][CAR_COLORS] = {};
    model = std::clamp(model, 0, CAR_MODELS - 1);
    color = std::clamp(color, 0, CAR_COLORS - 1);
    if (!looked[model][color]) {
        looked[model][color] = true;
        cache[model][color] = Assets::find(std::string("vehicles/") + MODELS[model].key + "_" + COLOR_KEYS[color]);
        if (!cache[model][color]) std::fprintf(stderr, "[car] missing art vehicles/%s_%s\n", MODELS[model].key, COLOR_KEYS[color]);
    }
    return cache[model][color];
}

int carFrame(float angle) {
    int f = (int)std::lround(angle / (2 * PI / 48));
    return ((f % 48) + 48) % 48;
}

int carRepairCost(int model, float hpFrac) {
    hpFrac = clampf(hpFrac, 0, 1);
    if (hpFrac >= 0.999f) return 0;
    // A wreck costs a third of the car to rebuild; a scratched one very little.
    float base = MODELS[std::clamp(model, 0, CAR_MODELS - 1)].price * (hpFrac <= 0 ? 0.33f : 0.18f * (1.0f - hpFrac));
    return std::max(60, (int)(std::round(base / 10.0f) * 10));
}

// ---- the footprint ---------------------------------------------------------------
static Vec2 toLocal(const Car& c, Vec2 p) {
    Vec2 d = p - c.pos;
    float ca = std::cos(c.angle), sa = std::sin(c.angle);
    return {d.x * ca + d.y * sa, -d.x * sa + d.y * ca};
}

bool carContains(const Car& c, Vec2 p, float r) {
    const CarModel& m = carModel(c.model);
    Vec2 l = toLocal(c, p);
    return std::fabs(l.x) <= m.halfLen + r && std::fabs(l.y) <= m.halfWid + r;
}

float carOverlap(const Car& c, Vec2 p, float r) {
    const CarModel& m = carModel(c.model);
    Vec2 l = toLocal(c, p);
    float qx = clampf(l.x, -m.halfLen, m.halfLen), qy = clampf(l.y, -m.halfWid, m.halfWid);
    float dx = l.x - qx, dy = l.y - qy;
    float d = std::sqrt(dx * dx + dy * dy);
    if (d >= r) return 0;
    if (d > 0) return r - d;
    // Inside the box: how far to the nearest side.
    return r + std::min(m.halfLen - std::fabs(l.x), m.halfWid - std::fabs(l.y));
}

float carDepth(const Car& c) {
    const CarModel& m = carModel(c.model);
    return std::fabs(std::sin(c.angle)) * m.halfLen + std::fabs(std::cos(c.angle)) * m.halfWid;
}

// Points round the edge of the footprint at (pos, angle), about every 5 px.
static void edgePoints(const CarModel& m, Vec2 pos, float angle, std::vector<Vec2>& out) {
    out.clear();
    Vec2 f = fromAngle(angle), s(-f.y, f.x);
    int nl = std::max(2, (int)std::ceil(m.halfLen * 2 / 5.0f)), nw = std::max(1, (int)std::ceil(m.halfWid * 2 / 5.0f));
    for (int i = 0; i <= nl; i++) {
        float a = -m.halfLen + 2 * m.halfLen * i / nl;
        out.push_back(pos + f * a + s * m.halfWid);
        out.push_back(pos + f * a - s * m.halfWid);
    }
    for (int j = 1; j < nw; j++) {
        float b = -m.halfWid + 2 * m.halfWid * j / nw;
        out.push_back(pos + f * m.halfLen + s * b);
        out.push_back(pos - f * m.halfLen + s * b);
    }
}

// The car being moved right now: the world's hook for moving things (other cars)
// must not count it as its own obstacle.
const Car* g_carMoving = nullptr;

bool carFits(const Car& c, const World& w, Vec2 pos, float angle) {
    static std::vector<Vec2> pts;
    edgePoints(carModel(c.model), pos, angle, pts);
    const Car* prev = g_carMoving;
    g_carMoving = &c;
    bool ok = true;
    for (const Vec2& p : pts)
        if (w.collides(p.x, p.y, 1.0f)) { ok = false; break; }
    g_carMoving = prev;
    return ok;
}

// Light things a car goes straight through at speed.
static bool smashable(int solid, float speed, float mass) {
    switch (solid) {
    case S_FENCE: case S_BUSH: case S_CRATE: case S_DOOR: return speed > 45;
    case S_SANDBAG: return speed > 90 && mass >= 1.3f;
    case S_WALL_WOOD: return speed > 150 && mass >= 1.2f;
    default: return false;
    }
}

void carStep(Car& c, World& w, const CarInput& in, float dt, bool smash, CarStepResult& out) {
    const CarModel& m = carModel(c.model);
    c.hurtT -= dt;
    if (c.wrecked) { c.vel = Vec2(); c.angVel = 0; return; }
    Vec2 f = fromAngle(c.angle), s(-f.y, f.x);
    float vf = dot(c.vel, f), vs = dot(c.vel, s);
    // Off the asphalt it will not go as fast (the jeep hardly notices).
    int tx = World::toTile(c.pos.x), ty = World::toTile(c.pos.y);
    bool paved = false;
    if (w.inBounds(tx, ty)) {
        int g = w.at(tx, ty).ground;
        paved = g == G_ROAD || g == G_BRIDGE || g == G_PAVEMENT || (g >= G_FLOOR_WOOD && g <= G_FLOOR_TILE) || g == G_BASE_FLOOR;
    }
    float hpFrac = c.hp / m.hp;
    float top = m.topSpeed * (paved ? 1.0f : m.offroad) * (hpFrac < 0.25f ? 0.75f : 1.0f);
    // An empty tank still brakes; it just will not pull.
    float throttle = c.fuel > 0 ? in.throttle : (in.throttle < 0 && vf > 5 ? in.throttle : 0.0f);
    const float BRAKE = 420, DRAG = 55;
    if (throttle > 0.05f) {
        if (vf < -5) vf = std::min(0.0f, vf + BRAKE * dt);
        else if (vf < top) vf = std::min(top, vf + m.accel * throttle * dt * (1.0f - 0.55f * vf / top));
        else vf = std::max(top, vf - DRAG * 2 * dt);
    } else if (throttle < -0.05f) {
        if (vf > 5) vf = std::max(0.0f, vf - BRAKE * dt);
        else vf = std::max(-top * 0.4f, vf - m.accel * 0.6f * -throttle * dt);
    } else {
        float d = std::min(std::fabs(vf), DRAG * dt);
        vf -= vf > 0 ? d : -d;
        if (vf > top) vf = std::max(top, vf - DRAG * 2 * dt);
    }
    c.throttle += (std::fabs(throttle) - c.throttle) * std::min(1.0f, dt * 6.0f);
    c.slip = std::fabs(vs) * (paved ? 1.0f : 0.0f);
    float grip = m.grip;
    if (in.handbrake) {
        float d = std::min(std::fabs(vf), 240 * dt);
        vf -= vf > 0 ? d : -d;
        grip = 1.6f;
    }
    if (!paved) grip *= 0.85f;
    vs *= std::exp(-grip * dt);
    // Steering: quick at the wheel, but only as much turning as the speed allows.
    c.steer += (in.steer - c.steer) * std::min(1.0f, dt * 9.0f);
    float turnScale = clampf(std::fabs(vf) / 55.0f, 0, 1) * (1.0f - 0.25f * clampf(std::fabs(vf) / m.topSpeed - 0.6f, 0, 1));
    c.angVel = c.steer * m.turn * turnScale * (vf >= 0 ? 1.0f : -1.0f) * (in.handbrake ? 1.35f : 1.0f);
    // Fuel: burnt by the distance covered.
    if (c.fuel > 0) c.fuel = std::max(0.0f, c.fuel - std::fabs(vf) * dt / 1000.0f * m.economy);

    float newAngle = c.angle + c.angVel * dt;
    Vec2 nf = fromAngle(newAngle), ns(-nf.y, nf.x);
    c.vel = nf * vf + ns * vs;

    // Move in small steps, so a fast car never jumps through a wall or a tree.
    float spd = length(c.vel);
    int n = std::max(1, (int)std::ceil(spd * dt / 4.0f));
    float h = dt / n;
    float angleStep = (newAngle - c.angle) / n;
    bool stuck = !carFits(c, w, c.pos, c.angle);   // already overlapping: let it drive out
    static std::vector<Vec2> pts;
    for (int k = 0; k < n; k++) {
        Vec2 np = c.pos + c.vel * h;
        float na = c.angle + angleStep;
        if (stuck || carFits(c, w, np, na)) { c.pos = np; c.angle = na; continue; }
        // Break through whatever light thing is in the way.
        if (smash) {
            edgePoints(m, np, na, pts);
            bool anyHard = false;
            std::vector<std::pair<int, int>> soft;
            const Car* prev = g_carMoving;
            g_carMoving = &c;
            for (const Vec2& p : pts) {
                if (!w.collides(p.x, p.y, 1.0f)) continue;
                int px = World::toTile(p.x), py = World::toTile(p.y);
                if (w.inBounds(px, py) && smashable(w.at(px, py).solid, spd, m.mass)) soft.push_back({px, py});
                else anyHard = true;
            }
            g_carMoving = prev;
            if (!anyHard && !soft.empty()) {
                for (auto& t : soft) {
                    if (w.at(t.first, t.second).solid == S_NONE) continue;
                    w.destroyTile(t.first, t.second);
                    out.smashed.push_back(t);
                    c.vel *= 1.0f - 0.1f / m.mass;
                }
                if (carFits(c, w, np, na)) { c.pos = np; c.angle = na; continue; }
            }
        }
        // Scrape along it if we can, else stop dead with a small bounce.
        Vec2 before = c.vel;
        if (carFits(c, w, c.pos + Vec2(c.vel.x * h, 0), c.angle)) {
            c.pos.x += c.vel.x * h;
            c.vel.y *= -0.12f;
            c.vel.x *= 0.85f;
        } else if (carFits(c, w, c.pos + Vec2(0, c.vel.y * h), c.angle)) {
            c.pos.y += c.vel.y * h;
            c.vel.x *= -0.12f;
            c.vel.y *= 0.85f;
        } else {
            c.vel = c.vel * -0.18f;
        }
        if (carFits(c, w, c.pos, na)) c.angle = na;
        else c.angVel = 0;
        float lost = length(before - c.vel);
        if (lost > out.impact) { out.impact = lost; out.hitAt = c.pos + normalize(before) * m.halfLen; }
        break;
    }
    // Keep the angle tidy.
    c.angle = std::remainder(c.angle, 2 * PI);
}
