// Outdoor atmosphere: weather that drifts on its own (overcast, rain, storms, fog and
// mixes of them), plus the colour grade laid over each scene - the time of day, a
// mood rolled for each day's world, and a red cast while a horde is on. Everything
// eases between states rather than switching.
#pragma once
#include "core.h"
#include "render.h"

struct World;
#include <string>

namespace Atmo {

// Snap straight to the current weather, for the moment you step outside.
void reset();
// Eases the live weather toward what the clock calls for. `horde` fades the horde
// grade in and out.
void update(float dt, bool horde);
// Fills ambient light, grade, fog and post-processing for an outdoor scene at this
// time of day. Returns the resulting brightness (0 dark .. 1 full day).
float apply(LightingParams& lp, float minutes);
// Indoors: no weather, no grade.
void clearOutdoor(LightingParams& lp);
// Puddles, which gather while it rains and dry out slowly after. Drawn flat on the
// ground, before anything that stands on it.
void drawGround(const World& w, Vec2 cam);
// Rain streaks and drop splashes, drawn in the world layer after the roofs. Nothing
// falls or splashes inside a building.
void drawRain(const World& w, Vec2 cam);
// Walking through a puddle kicks up a splash.
void footstep(const World& w, Vec2 feet, bool moving, float dt);
// How much direct sun gets through the weather right now: 1 clear .. ~0.1 storm.
float sunlight();
// How hard it is raining right now, 0 dry .. 1 a downpour (0.11v: the ambience).
float rainAmount();
// Dev (--atpuddle): the centre of the puddle tile nearest `from`, or `from` if none.
Vec2 nearestPuddle(const World& w, Vec2 from);
// Dev: hold one weather preset (0 clear .. 8 overcast + fog), -1 to release.
void devForce(int preset);

}  // namespace Atmo
