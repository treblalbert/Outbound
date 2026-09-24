#pragma once
#include "core.h"
#include "assets.h"
#include "gl.h"
#include <string>
#include <vector>

struct Texture { GLuint id = 0; int w = 0, h = 0; };

struct LightSrc {
    Vec2 pos;          // world coordinates
    float radius;
    float intensity;
    Color color;
};

struct ShadowRect { float x0, y0, x1, y1; };

// Screen-space effects applied to the finished frame, after the world, glow and UI
// layers have been lit and composited. Every weight is a 0 (off) .. 1 (full)
// strength; `enabled` turns the whole pass into a plain copy.
struct PostFx {
    bool enabled = true;
    float vignette = 0.45f;     // darkens the corners
    float aberration = 0.8f;    // RGB split that grows toward the edges
    float grain = 0.06f;        // animated film grain
    float bloom = 0.55f;        // glow from the lit things, bled outward
    float contrast = 0.06f;     // grade: extra punch
    float saturation = 1.06f;   // grade: 1 = untouched
};

struct LightingParams {
    Color ambient{1, 1, 1};
    float dither = 0.0f;        // unused, kept so existing call sites compile
    float saturate = 1.0f;      // 0 = greyscale, 1 = full colour
    PostFx post;
    std::vector<LightSrc> lights;
    bool cone = false;
    Vec2 conePos;
    float coneAngle = 0, coneLen = 160, coneHalfWidth = 0.45f, coneIntensity = 0.9f;
    // More flashlights (0.12v, local co-op): every player's own. Up to three besides the first.
    struct Cone { Vec2 pos; float angle, len, halfWidth, intensity; };
    std::vector<Cone> extraCones;
    std::vector<ShadowRect> coneBlockers;
    // Colour grade on the lit world (not the HUD): multiply, then lift the shadows.
    Color grade{1, 1, 1};
    Color lift{0, 0, 0};
    // Procedural fog over the world, lit by the same lights. 0 = none.
    float fog = 0;
    Color fogColor{0.8f, 0.82f, 0.86f};
    float fogTime = 0;         // seconds, drifts the fog
    float fogWind = 1;         // how fast it drifts
};

namespace R {
enum Layer { WORLD, GLOW, UI };

bool init();
void resize(int winW, int winH);
int width();                 // the UI's size in low-res pixels (and the world's at zoom 1)
int height();
int pixelScale();
// ---- zoom (0.12v, local co-op): the world drawn smaller, to fit more of it on screen.
// 1 is normal; above it the world layers cover viewW() x viewH() world pixels while the
// UI stays width() x height(). Set before the world layers are begun.
void setZoom(float z);
float zoom();
float zoomMax();
int viewW();
int viewH();

Texture createTexture(int w, int h, const uint8_t* rgba);
void updateTexture(Texture& t, const uint8_t* rgba);

void begin(Layer layer, Vec2 cam);
void end();
void flush();
void setTexture(GLuint tex);   // 0 = atlas

// Multiplies the alpha of everything drawn from now on (1 = normal). The HUD uses it
// to fade pieces the crosshair passes over.
void setAlpha(float a);

// ---- sun shadows
// Everything standing in the sun throws a soft shadow away from it. The scene hands
// over its casters while it draws; flushShadows() then lays the shadows (all at once,
// so overlaps never double up) onto what has been drawn so far - the ground and the
// walls - before the standing things and the roofs go on top.
// `dir` is how far a shadow reaches per pixel of height; `strength` 0 turns them off.
void setSun(Vec2 dir, float strength);
// --nograin: turns the film grain off, for trailer footage and store screenshots.
void setNoGrain(bool on);
float sunStrength();
Vec2 sunDir();
// A standing sprite: bottom-centre `base`, drawn `w` x `h` (its frame is squashed onto
// the ground along the sun).
void shadowSprite(const Assets::Frame& f, Vec2 base, float w, float h, bool flipX);
// A box standing on the ground (a wall, a whole building): its footprint swept along
// the sun by `height` pixels.
void shadowBox(float x0, float y0, float x1, float y1, float height);
void flushShadows();
// ---- ambient occlusion (0.11v): queue what stands on the ground - a solid rect (a wall
// tile) or the bottom `rows` pixel rows of a standing sprite, placed as spriteAt places
// it - then flushAO() darkens the ground softly just outside those pixels.
void aoRect(float x, float y, float w, float h);
void aoSprite(const Assets::Frame& f, Vec2 base, float w, float h, bool flipX, int rows);
void flushAO(float strength);
// ---- the flashlight's occluders (0.11v): queue the real shapes that stop the beam (a
// whole tile, a round trunk, a sprite's pixels placed as spriteAt places it), then
// flushOcc() inside the world layer. The light pass walks each beam over that mask, so
// every object throws a shadow of its own shape. Without a flushOcc() this frame the
// old boxes (LightingParams::coneBlockers) are used.
void occRect(float x, float y, float w, float h);
void occDisc(Vec2 c, float r);
void occSprite(const Assets::Frame& f, Vec2 base, float w, float h, bool flipX);
void flushOcc();
// ---- reflections (0.11v): queue where water lies (a puddle sprite's frame, or a
// rect of a blood pool, tinted with the water's colour and alpha), then the standing
// sprites, mirrored below their feet; flushWater() lays the reflections on the water.
void waterFrame(const Assets::Frame& f, float x, float y, float w, float h, Color tint);
void waterRect(float x, float y, float w, float h, Color tint);
bool waterQueued();
void reflectSprite(const Assets::Frame& f, Vec2 base, float w, float h, bool flipX, Color tint);
void flushWater(float time, Color sky);
float alpha();
void quad(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, float u0, float v0, float u1, float v1, Color c);
void rect(float x, float y, float w, float h, Color c);
// A rectangle with its own colour at each corner (top-left, top-right, bottom-right,
// bottom-left), blended across.
void gradRect(float x, float y, float w, float h, Color c00, Color c10, Color c11, Color c01);
void rectOutline(float x, float y, float w, float h, Color c, float t = 1);
// Pivot for asset sprites: Center for characters/effects, Bottom for props standing on the ground.
enum class Pivot { Center, Bottom };
void frame(const Assets::Frame& f, float x, float y, float w, float h, Color c = Color(), bool flipX = false, float angle = 0);
void spriteAt(const Assets::Sprite& s, int frameIndex, Vec2 pos, Pivot pivot = Pivot::Center, float scale = 1, Color c = Color(), bool flipX = false, float angle = 0);
// A bottom-anchored sprite bent by the wind: each source row shifts sideways by a
// whole number of pixels, `sway` at the top row easing to none below the canopy.
void spriteSway(const Assets::Sprite& s, int frameIndex, Vec2 pos, float sway, float scale = 1, Color c = Color(), bool flipX = false);
void tileAt(const Assets::TileRef& t, float x, float y, float size, Color c = Color());
void sprite(int id, Vec2 center, float angle = 0, float scale = 1, Color c = Color(), bool flipY = false, bool flipX = false);
void spriteRect(int id, float x, float y, float w, float h, Color c = Color());
void line(Vec2 a, Vec2 b, float thick, Color c);
void circle(Vec2 center, float r, Color c, int segments = 20);
void image(const Texture& t, float x, float y, float w, float h, Color c = Color());

void text(const std::string& s, float x, float y, Color c, float scale = 1);
// Dev (--checktext): report any text that starts inside this box but runs past it.
void setTextCheck(bool enabled);
void setTextBox(float x0, float y0, float x1, float y1);
void clearTextBox();
void textShadow(const std::string& s, float x, float y, Color c, float scale = 1, Color shadow = pal(P_DARK));
void textCentered(const std::string& s, float cx, float y, Color c, float scale = 1, bool shadow = true);
float textWidth(const std::string& s, float scale = 1);
float textHeight(float scale = 1);

void present(const LightingParams& lp, Vec2 cam, int winW, int winH);
}
