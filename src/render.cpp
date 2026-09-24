#include "render.h"
#include "assets.h"
#include "sprites.h"
#include <cstdio>
#include <cstring>

#include "../external/stb_easy_font.h"

namespace R {

struct Vertex { float x, y, u, v, r, g, b, a; };

struct Target { GLuint fbo = 0; Texture tex; };

static GLuint g_spriteProg = 0, g_compProg = 0, g_postProg = 0;
static GLuint g_vao = 0, g_vbo = 0, g_emptyVao = 0;
static Texture g_atlas;
// 0 world, 1 glow, 2 UI, 3 the lit composite that the post pass runs over, 4 the sun
// shadow mask, 5 the ambient occlusion mask, 6 where the water lies, 7 what it reflects.
static const int TARGET_POST = 3, TARGET_SHADOW = 4, TARGET_AO = 5, TARGET_WATER = 6, TARGET_REFL = 7, TARGET_OCC = 8;
static Target g_targets[9];
static GLuint g_waterProg = 0;
static GLint u_wMask = -1, u_wRefl = -1, u_wRes = -1, u_wTime = -1, u_wSky = -1, u_wCam = -1, u_wViewH = -1;
static std::vector<struct Vertex> g_waterVerts, g_reflVerts;
static GLuint g_aoProg = 0;
static GLint u_aoMask = -1, u_aoRes = -1, u_aoStrength = -1;
static std::vector<struct Vertex> g_aoVerts;
static std::vector<struct Vertex> g_occVerts;   // what stops the flashlight (0.11v)
static bool g_occReady = false;
static Vec2 g_layerCam;          // camera of the layer being drawn
static GLuint g_layerFbo = 0;    // and its framebuffer
static GLint u_sil = -1;
static Vec2 g_sunDir(0.5f, 0.6f);
static float g_sunStrength = 0;
static std::vector<struct Vertex> g_shadowVerts;
static float g_postFrame = 0;   // drives the grain, so it crawls instead of sticking
static bool g_noGrain = false;  // --nograin: film grain off (trailer and store shots)
static std::vector<Vertex> g_verts;
static GLuint g_curTex = 0;
static int g_iw = 640, g_ih = 360, g_scale = 2;
// The world can be drawn zoomed out (0.12v, local co-op): it goes into a bigger view,
// g_vw x g_vh, which the post pass scales down onto the window; the UI stays g_iw x
// g_ih. Every target is allocated at the largest view (g_tw x g_th) and drawn into its
// lower-left corner, so zooming never reallocates anything.
constexpr float ZOOM_MAX = 1.8f;
static int g_tw = 640, g_th = 360;
static float g_zoom = 1.0f;
static int g_vw = 640, g_vh = 360;
static int g_lw = 640, g_lh = 360;   // the layer being drawn: its view size
static GLint u_cam, u_view, u_tex;
static size_t g_vboCap = 0;

static const char* SPRITE_VS = R"(
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
layout(location=2) in vec4 aCol;
uniform vec2 uCam;
uniform vec2 uView;
out vec2 vUV;
out vec4 vCol;
void main(){
    vec2 p = (aPos - uCam) / uView;
    gl_Position = vec4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
    vUV = aUV; vCol = aCol;
})";

static const char* SPRITE_FS = R"(
in vec2 vUV;
in vec4 vCol;
uniform sampler2D uTex;
uniform float uSil;     // 1: draw only the shape, as coverage in alpha (shadow mask)
out vec4 fragColor;
void main(){
    vec4 c = texture(uTex, vUV) * vCol;
    if (c.a < 0.01) discard;
    if (uSil > 0.5) { fragColor = vec4(0.0, 0.0, 0.0, c.a); return; }
    fragColor = c;
})";

static const char* COMP_VS = R"(
out vec2 vUV;
void main(){
    vec2 p = vec2((gl_VertexID == 1) ? 3.0 : -1.0, (gl_VertexID == 2) ? 3.0 : -1.0);
    vUV = p * 0.5 + 0.5;
    gl_Position = vec4(p, 0.0, 1.0);
})";

static const char* COMP_FS = R"(
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uWorld;
uniform sampler2D uGlow;
uniform sampler2D uUI;
uniform vec2 uRes;
uniform vec3 uAmbient;
uniform float uSaturate;
uniform int uNumLights;
uniform vec4 uL[48];
uniform vec3 uLC[48];
uniform vec4 uCone;
uniform vec2 uConeParams;
uniform int uNumBlockers;
uniform vec4 uBlockers[96];
uniform sampler2D uOcc;   // the real shapes that stop the flashlight (0.11v)
uniform int uUseOcc;
uniform vec3 uGrade;
uniform vec3 uLift;
uniform vec4 uFog;      // rgb colour, a density
uniform vec4 uFogP;     // xy camera, z time, w wind
uniform vec2 uUVScale;  // the view's share of the targets (zoom, 0.12v)

float fogHash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123); }
float fogNoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = fogHash(i), b = fogHash(i + vec2(1.0, 0.0));
    float c = fogHash(i + vec2(0.0, 1.0)), d = fogHash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}
float fogFbm(vec2 p) {
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 4; i++) { v += fogNoise(p) * a; p *= 2.03; a *= 0.5; }
    return v;
}

bool segmentHitsBox(vec2 a, vec2 b, vec4 box) {
    vec2 d = b - a;
    vec2 safeD = vec2(abs(d.x) < 0.0001 ? 0.0001 : d.x,
                      abs(d.y) < 0.0001 ? 0.0001 : d.y);
    vec2 t0 = (box.xy - a) / safeD;
    vec2 t1 = (box.zw - a) / safeD;
    vec2 lo = min(t0, t1), hi = max(t0, t1);
    float enter = max(lo.x, lo.y), leave = min(hi.x, hi.y);
    return leave >= max(enter, 0.0) && enter < 0.985;
}

void main(){
    vec2 px = vec2(vUV.x * uRes.x, (1.0 - vUV.y) * uRes.y);
    vec4 worldSample = texture(uWorld, vUV * uUVScale);
    vec3 albedo = worldSample.rgb;
    vec3 light = uAmbient;
    for (int i = 0; i < uNumLights; i++) {
        float d = length(px - uL[i].xy);
        float f = clamp(1.0 - d / uL[i].z, 0.0, 1.0);
        light += uLC[i] * (f * f * uL[i].w);
    }
    if (uCone.w > 0.0) {
        vec2 dv = px - uCone.xy;
        float d = length(dv);
        if (d > 0.5) {
            float a = atan(dv.y, dv.x);
            float da = abs(mod(a - uCone.z + 3.14159265, 6.2831853) - 3.14159265);
            float fa = clamp(1.0 - da / uConeParams.y, 0.0, 1.0);
            float fd = clamp(1.0 - d / uConeParams.x, 0.0, 1.0);
            float visible = 1.0;
            if (uUseOcc == 1) {
                // Walk the beam from the lamp to this pixel over the occluder mask, a pixel
                // at a time: anything opaque on the way throws its own exact shadow. The
                // last pixel or two are left out, so what is hit is lit on its near face.
                if (fa > 0.0 && fd > 0.0) {
                    vec2 dir = dv / d;
                    for (float t = 3.0; t < 400.0; t += 1.0) {
                        if (t > d - 1.5) break;
                        vec2 q = uCone.xy + dir * t;
                        if (texture(uOcc, vec2(q.x / uRes.x, 1.0 - q.y / uRes.y) * uUVScale).a > 0.5) { visible = 0.0; break; }
                    }
                }
            } else {
                for (int i = 0; i < 96; i++) {
                    if (i >= uNumBlockers) break;
                    if (segmentHitsBox(uCone.xy, px, uBlockers[i])) { visible = 0.0; break; }
                }
            }
            light += vec3(1.0, 0.96, 0.85) * (sqrt(fa) * fd * uCone.w * visible);
        }
    }
    // Keep a little colour in deep shadow instead of crushing to black.
    vec3 lit = albedo * min(light, vec3(1.35));
    float lum = dot(lit, vec3(0.299, 0.587, 0.114));
    vec3 c = mix(vec3(lum), lit, uSaturate);
    // Fog: banks of world-anchored noise that drift with the wind. It takes the
    // light where it lies, so a lamp or a torch beam glows through it at night.
    if (uFog.a > 0.0) {
        vec2 wp = floor((px + uFogP.xy) / 2.0) * 2.0;
        float t = uFogP.z * uFogP.w;
        float n = fogFbm(wp * 0.009 + vec2(t * 0.020, t * 0.006));
        float n2 = fogFbm(wp * 0.025 - vec2(t * 0.012, t * 0.009));
        float dens = uFog.a * clamp(0.25 + n * 1.1 + n2 * 0.35 - 0.3, 0.0, 1.0);
        dens = floor(dens * 10.0 + 0.5) / 10.0;
        c = mix(c, uFog.rgb * clamp(light, vec3(0.0), vec3(1.2)), dens);
    }
    float glum = dot(c, vec3(0.299, 0.587, 0.114));
    c = c * uGrade + uLift * (1.0 - glum);
    vec4 g = texture(uGlow, vUV * uUVScale);
    c = g.rgb + c * (1.0 - g.a);
    fragColor = vec4(c, 1.0);
})";

// Ambient occlusion (0.11v): the mask holds the real pixels of whatever stands on the
// ground (walls, and the bottom rows of every standing sprite). Blurred, it darkens
// the ground just outside those pixels, never under them, so it follows every shape
// instead of drawing boxes around sprites with see-through parts.
static const char* AO_FS = R"(
out vec4 fragColor;
uniform sampler2D uMask;
uniform vec2 uRes;
uniform float uStrength;
void main(){
    vec2 t = 1.0 / uRes;
    vec2 uv = gl_FragCoord.xy / uRes;
    float self = texture(uMask, uv).a;
    if (self > 0.5) discard;
    float s = 0.0, wsum = 0.0;
    for (int y = -5; y <= 5; y++)
        for (int x = -5; x <= 5; x++) {
            float d2 = float(x * x + y * y);
            if (d2 > 30.0) continue;
            float w = exp(-d2 / 9.0);
            s += texture(uMask, uv + vec2(float(x), float(y)) * t).a * w;
            wsum += w;
        }
    float ao = smoothstep(0.0, 0.62, s / wsum);
    fragColor = vec4(0.0, 0.0, 0.0, ao * uStrength * (1.0 - self));
})";

// Reflections (0.11v): puddles and blood pools mirror what stands over them. The mask
// is where water lies (with how strongly it shows); the reflection target holds every
// standing sprite flipped upside down about its feet. Laid on the ground with a slow
// ripple (whole pixels, so the art stays crisp) and a touch of sky.
static const char* WATER_FS = R"(
out vec4 fragColor;
uniform sampler2D uMask;
uniform sampler2D uRefl;
uniform vec2 uRes;
uniform vec2 uCam;
uniform float uTime;
uniform vec3 uSky;
uniform float uViewH;
void main(){
    vec2 uv = gl_FragCoord.xy / uRes;
    vec4 mk = texture(uMask, uv);
    float m = mk.a;
    if (m < 0.01) discard;
    float wy = uViewH - gl_FragCoord.y + uCam.y;
    float off = floor(sin(wy * 0.9 + uTime * 2.2) * 1.1 + 0.5);
    vec4 r = texture(uRefl, uv + vec2(off / uRes.x, 0.0));
    vec3 refl = r.rgb / max(r.a, 0.001);
    // mk.rgb carries the water's own tint (blood is red), mixed into what it shows.
    vec3 c = mix(uSky, refl * 0.8, r.a) * mix(vec3(1.0), mk.rgb, 0.55);
    fragColor = vec4(c, m * (0.14 + 0.46 * r.a));
})";

// The post pass runs over the finished frame: the lit composite in .rgb, with the
// glow layer alongside it for bloom.
static const char* POST_FS = R"(
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uImage;
uniform sampler2D uGlow;
uniform sampler2D uUI;
uniform vec2 uShift;    // the camera's part of a pixel, in UV: the world slides by it, the UI does not
uniform vec2 uRes;      // the world view's resolution, so effects land on whole pixels
uniform vec2 uUVScale;  // the world view's share of its targets
uniform vec2 uUIScale;  // the UI's share of its target
uniform vec2 uTexSize;  // the targets' size in texels
uniform float uSharp;   // zoomed out: window pixels per world pixel (sharp scaling); 0 = plain pixels
uniform vec4 uFx;       // x vignette, y aberration, z unused, w grain
uniform vec3 uFx2;      // x bloom, y contrast, z saturation
uniform float uTime;

float hash21(vec2 p){
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

// Hash without sine (Dave Hoskins): no diagonal banding, and the third coordinate is
// the grain frame, so every frame is a fresh, unrelated pattern instead of the last
// one slid across the screen.
float hash13(vec3 p3){
    p3 = fract(p3 * 0.1031);
    p3 += dot(p3, p3.zyx + 31.32);
    return fract((p3.x + p3.y) * p3.z);
}

// A texel of the lit world. At 1:1 the nearest one, exactly as before. Zoomed out the
// world is scaled by a fraction, so each texel is blended only across the one window
// pixel on its edge: crisp pixels that do not shimmer as the camera moves.
vec3 img(vec2 viewUV){
    vec2 t = viewUV * uUVScale * uTexSize;
    if (uSharp <= 0.0) return texture(uImage, (floor(t) + 0.5) / uTexSize).rgb;
    vec2 tc = t - 0.5;
    vec2 i = floor(tc);
    vec2 f = clamp((tc - i - 0.5) * uSharp + 0.5, 0.0, 1.0);
    return texture(uImage, (i + 0.5 + f) / uTexSize).rgb;
}

void main(){
    // The world was drawn at the camera rounded down to a whole low-res pixel; slide it
    // by what was left over, one window pixel at a time, so slow camera moves glide
    // instead of hopping a whole low-res pixel at once.
    vec2 uv = vUV + uShift;
    vec2 px = uv * uRes;
    vec2 d = uv - 0.5;
    float r2 = dot(d, d);

    // Chromatic aberration: the split grows toward the edges, so the middle of the
    // screen - where you are looking - stays clean.
    vec2 off = (d * (0.3 + r2 * 4.0) * uFx.y) / uRes;
    vec3 c;
    c.r = img(uv + off).r;
    c.g = img(uv).g;
    c.b = img(uv - off).b;

    // Bloom: the glow layer already holds the bright things on their own, so a few
    // taps around it are enough to make them bleed.
    if (uFx2.x > 0.0) {
        vec2 t = 1.6 / uRes;
        vec3 b = texture(uGlow, uv * uUVScale).rgb;
        b += texture(uGlow, (uv + vec2(t.x, 0.0)) * uUVScale).rgb;
        b += texture(uGlow, (uv - vec2(t.x, 0.0)) * uUVScale).rgb;
        b += texture(uGlow, (uv + vec2(0.0, t.y)) * uUVScale).rgb;
        b += texture(uGlow, (uv - vec2(0.0, t.y)) * uUVScale).rgb;
        c += b * (uFx2.x * 0.2);
    }

    // Grade: a touch of contrast and colour over the palette.
    float lum = dot(c, vec3(0.299, 0.587, 0.114));
    c = mix(vec3(lum), c, uFx2.z);
    c = (c - 0.5) * (1.0 + uFx2.y) + 0.5;

    // Vignette, kept well off centre so panels and HUD text stay readable.
    c *= 1.0 - uFx.x * smoothstep(0.18, 0.85, r2);

    // The UI goes on top, where it was drawn: it never slides with the camera.
    vec4 u = texture(uUI, vUV * uUIScale);
    c = u.rgb + c * (1.0 - u.a);

    // Film grain: per screen pixel, soft (two samples average toward a bell curve),
    // monochrome, and strongest in the midtones like real film - not in the blacks or
    // the highlights.
    if (uFx.w > 0.0) {
        vec2 sp = floor(gl_FragCoord.xy);
        float t = mod(uTime, 997.0);
        float g = hash13(vec3(sp, t)) + hash13(vec3(sp + 57.0, t + 13.0)) - 1.0;
        float lum2 = dot(c, vec3(0.299, 0.587, 0.114));
        float body = 0.35 + 2.6 * lum2 * (1.0 - lum2);
        c += g * uFx.w * body;
    }
    fragColor = vec4(clamp(c, 0.0, 1.0), 1.0);
})";

// The shader bodies below are shared; only the version line and the precision
// qualifiers differ between desktop GL 3.3 and the WebGL 2 (GL ES 3.0) build.
#ifdef __EMSCRIPTEN__
static const char* GLSL_VS_HEADER = "#version 300 es\nprecision highp float;\n";
static const char* GLSL_FS_HEADER = "#version 300 es\nprecision highp float;\nprecision highp sampler2D;\n";
#else
static const char* GLSL_VS_HEADER = "#version 330 core\n";
static const char* GLSL_FS_HEADER = "#version 330 core\n";
#endif

static GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    const char* parts[2] = {type == GL_VERTEX_SHADER ? GLSL_VS_HEADER : GLSL_FS_HEADER, src};
    glShaderSource(s, 2, parts, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(s, sizeof log, nullptr, log);
        std::fprintf(stderr, "Shader compile error:\n%s\n", log);
    }
    return s;
}

static GLuint link(const char* vs, const char* fs) {
    GLuint p = glCreateProgram();
    GLuint a = compile(GL_VERTEX_SHADER, vs), b = compile(GL_FRAGMENT_SHADER, fs);
    glAttachShader(p, a);
    glAttachShader(p, b);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(p, sizeof log, nullptr, log);
        std::fprintf(stderr, "Shader link error:\n%s\n", log);
    }
    glDeleteShader(a);
    glDeleteShader(b);
    return p;
}

Texture createTexture(int w, int h, const uint8_t* rgba) {
    Texture t;
    t.w = w; t.h = h;
    glGenTextures(1, &t.id);
    glBindTexture(GL_TEXTURE_2D, t.id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

void updateTexture(Texture& t, const uint8_t* rgba) {
    glBindTexture(GL_TEXTURE_2D, t.id);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, t.w, t.h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

bool init() {
    g_atlas.id = Assets::atlasTexture();
    g_atlas.w = g_atlas.h = Assets::atlasSize();

    g_spriteProg = link(SPRITE_VS, SPRITE_FS);
    g_compProg = link(COMP_VS, COMP_FS);
    g_postProg = link(COMP_VS, POST_FS);
    g_aoProg = link(COMP_VS, AO_FS);
    g_waterProg = link(COMP_VS, WATER_FS);
    u_wMask = glGetUniformLocation(g_waterProg, "uMask");
    u_wRefl = glGetUniformLocation(g_waterProg, "uRefl");
    u_wRes = glGetUniformLocation(g_waterProg, "uRes");
    u_wCam = glGetUniformLocation(g_waterProg, "uCam");
    u_wTime = glGetUniformLocation(g_waterProg, "uTime");
    u_wSky = glGetUniformLocation(g_waterProg, "uSky");
    u_wViewH = glGetUniformLocation(g_waterProg, "uViewH");
    u_aoMask = glGetUniformLocation(g_aoProg, "uMask");
    u_aoRes = glGetUniformLocation(g_aoProg, "uRes");
    u_aoStrength = glGetUniformLocation(g_aoProg, "uStrength");
    u_cam = glGetUniformLocation(g_spriteProg, "uCam");
    u_view = glGetUniformLocation(g_spriteProg, "uView");
    u_tex = glGetUniformLocation(g_spriteProg, "uTex");
    u_sil = glGetUniformLocation(g_spriteProg, "uSil");

    glGenVertexArrays(1, &g_vao);
    glBindVertexArray(g_vao);
    glGenBuffers(1, &g_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    g_vboCap = 65536;
    glBufferData(GL_ARRAY_BUFFER, g_vboCap * sizeof(Vertex), nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(2 * sizeof(float)));
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(4 * sizeof(float)));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glGenVertexArrays(1, &g_emptyVao);
    g_verts.reserve(65536);
    return true;
}

void resize(int winW, int winH) {
    if (winW <= 0 || winH <= 0) return;
    int s = (int)std::floor(winH / 340.0f + 0.25f);
    s = std::max(1, s);
    int iw = (winW + s - 1) / s, ih = (winH + s - 1) / s;
    if (iw == g_iw && ih == g_ih && s == g_scale && g_targets[0].fbo) return;
    g_iw = iw; g_ih = ih; g_scale = s;
    g_tw = (int)std::ceil(iw * ZOOM_MAX);
    g_th = (int)std::ceil(ih * ZOOM_MAX);
    setZoom(g_zoom);
    for (int i = 0; i < (int)(sizeof(g_targets) / sizeof(g_targets[0])); i++) {
        Target& t = g_targets[i];
        if (!t.fbo) glGenFramebuffers(1, &t.fbo);
        if (t.tex.id) glDeleteTextures(1, &t.tex.id);
        t.tex = createTexture(g_tw, g_th, nullptr);
        if (i == TARGET_POST) {
            // The post pass filters it itself (see img() in POST_FS).
            glBindTexture(GL_TEXTURE_2D, t.tex.id);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.tex.id, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            std::fprintf(stderr, "Framebuffer incomplete\n");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

int width() { return g_iw; }
int height() { return g_ih; }
int pixelScale() { return g_scale; }
void setZoom(float z) {
    g_zoom = clampf(z, 1.0f, ZOOM_MAX);
    g_vw = std::min(g_tw, (int)std::lround(g_iw * g_zoom));
    g_vh = std::min(g_th, (int)std::lround(g_ih * g_zoom));
    if (g_zoom <= 1.0001f) { g_vw = g_iw; g_vh = g_ih; }
}
float zoom() { return g_zoom; }
float zoomMax() { return ZOOM_MAX; }
int viewW() { return g_vw; }
int viewH() { return g_vh; }

void flush() {
    if (g_verts.empty()) return;
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    if (g_verts.size() > g_vboCap) {
        g_vboCap = g_verts.size() * 2;
        glBufferData(GL_ARRAY_BUFFER, g_vboCap * sizeof(Vertex), nullptr, GL_DYNAMIC_DRAW);
    }
    glBufferSubData(GL_ARRAY_BUFFER, 0, g_verts.size() * sizeof(Vertex), g_verts.data());
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_curTex ? g_curTex : g_atlas.id);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)g_verts.size());
    g_verts.clear();
}

void begin(Layer layer, Vec2 cam) {
    Target& t = g_targets[layer];
    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    g_lw = layer == UI ? g_iw : g_vw;
    g_lh = layer == UI ? g_ih : g_vh;
    glViewport(0, 0, g_lw, g_lh);
    if (layer == WORLD) glClearColor(pal(P_DARK).r, pal(P_DARK).g, pal(P_DARK).b, 1);
    else glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(g_spriteProg);
    glUniform2f(u_cam, std::floor(cam.x), std::floor(cam.y));
    glUniform2f(u_view, (float)g_lw, (float)g_lh);
    glUniform1i(u_tex, 0);
    glUniform1f(u_sil, 0.0f);
    g_curTex = 0;
    g_layerCam = Vec2(std::floor(cam.x), std::floor(cam.y));
    g_layerFbo = t.fbo;
}

void end() {
    flush();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void setTexture(GLuint tex) {
    if (tex == g_curTex) return;
    flush();
    g_curTex = tex;
}

static float g_alphaMul = 1.0f;
void setAlpha(float a) { g_alphaMul = clampf(a, 0, 1); }
float alpha() { return g_alphaMul; }

void quad(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, float u0, float v0, float u1, float v1, Color c) {
    c.a *= g_alphaMul;
    Vertex a{p0.x, p0.y, u0, v0, c.r, c.g, c.b, c.a};
    Vertex b{p1.x, p1.y, u1, v0, c.r, c.g, c.b, c.a};
    Vertex d{p2.x, p2.y, u1, v1, c.r, c.g, c.b, c.a};
    Vertex e{p3.x, p3.y, u0, v1, c.r, c.g, c.b, c.a};
    g_verts.push_back(a); g_verts.push_back(b); g_verts.push_back(d);
    g_verts.push_back(a); g_verts.push_back(d); g_verts.push_back(e);
}

static void whiteUV(float& u, float& v) {
    float u0, v0, u1, v1;
    Sprites::uv(Sprites::WHITE, u0, v0, u1, v1);
    u = (u0 + u1) * 0.5f;
    v = (v0 + v1) * 0.5f;
}

void rect(float x, float y, float w, float h, Color c) {
    if (g_curTex) setTexture(0);
    float u, v;
    whiteUV(u, v);
    quad({x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}, u, v, u, v, c);
}

void gradRect(float x, float y, float w, float h, Color c00, Color c10, Color c11, Color c01) {
    if (g_curTex) setTexture(0);
    float u, v;
    whiteUV(u, v);
    Color cs[4] = {c00, c10, c11, c01};
    for (Color& c : cs) c.a *= g_alphaMul;
    Vertex a{x, y, u, v, cs[0].r, cs[0].g, cs[0].b, cs[0].a};
    Vertex b{x + w, y, u, v, cs[1].r, cs[1].g, cs[1].b, cs[1].a};
    Vertex d{x + w, y + h, u, v, cs[2].r, cs[2].g, cs[2].b, cs[2].a};
    Vertex e{x, y + h, u, v, cs[3].r, cs[3].g, cs[3].b, cs[3].a};
    g_verts.push_back(a); g_verts.push_back(b); g_verts.push_back(d);
    g_verts.push_back(a); g_verts.push_back(d); g_verts.push_back(e);
}

void rectOutline(float x, float y, float w, float h, Color c, float t) {
    rect(x, y, w, t, c);
    rect(x, y + h - t, w, t, c);
    rect(x, y + t, t, h - 2 * t, c);
    rect(x + w - t, y + t, t, h - 2 * t, c);
}

void sprite(int id, Vec2 center, float angle, float scale, Color c, bool flipY, bool flipX) {
    if (g_curTex) setTexture(0);
    float u0, v0, u1, v1;
    Sprites::uv(id, u0, v0, u1, v1);
    if (flipY) std::swap(v0, v1);
    if (flipX) std::swap(u0, u1);
    float h = Sprites::SIZE * 0.5f * scale;
    if (angle == 0) {
        quad(center + Vec2(-h, -h), center + Vec2(h, -h), center + Vec2(h, h), center + Vec2(-h, h), u0, v0, u1, v1, c);
        return;
    }
    float cs = std::cos(angle), sn = std::sin(angle);
    auto rot = [&](float x, float y) { return center + Vec2(x * cs - y * sn, x * sn + y * cs); };
    quad(rot(-h, -h), rot(h, -h), rot(h, h), rot(-h, h), u0, v0, u1, v1, c);
}

void spriteRect(int id, float x, float y, float w, float h, Color c) {
    if (g_curTex) setTexture(0);
    float u0, v0, u1, v1;
    Sprites::uv(id, u0, v0, u1, v1);
    quad({x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}, u0, v0, u1, v1, c);
}

// ---- asset sprites --------------------------------------------------------
void frame(const Assets::Frame& f, float x, float y, float w, float h, Color c, bool flipX, float angle) {
    if (f.w <= 0) return;
    if (g_curTex) setTexture(0);
    float u0 = f.u0, u1 = f.u1;
    if (flipX) std::swap(u0, u1);
    if (angle == 0) {
        quad({x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}, u0, f.v0, u1, f.v1, c);
        return;
    }
    Vec2 center(x + w * 0.5f, y + h * 0.5f);
    float cs = std::cos(angle), sn = std::sin(angle);
    auto rot = [&](float dx, float dy) { return center + Vec2(dx * cs - dy * sn, dx * sn + dy * cs); };
    float hw = w * 0.5f, hh = h * 0.5f;
    quad(rot(-hw, -hh), rot(hw, -hh), rot(hw, hh), rot(-hw, hh), u0, f.v0, u1, f.v1, c);
}

void spriteAt(const Assets::Sprite& s, int frameIndex, Vec2 pos, Pivot pivot, float scale, Color c, bool flipX, float angle) {
    if (!s.valid()) return;
    const Assets::Frame& f = s.frame(frameIndex);
    float w = f.w * scale, h = f.h * scale;
    float x = std::floor(pos.x - w * 0.5f);
    float y = pivot == Pivot::Center ? std::floor(pos.y - h * 0.5f) : std::floor(pos.y - h + (pivot == Pivot::Bottom ? 0.0f : 0.0f));
    frame(f, x, y, w, h, c, flipX, angle);
}

void spriteSway(const Assets::Sprite& s, int frameIndex, Vec2 pos, float sway, float scale, Color c, bool flipX) {
    if (!s.valid()) return;
    const Assets::Frame& f = s.frame(frameIndex);
    if (f.w <= 0 || f.h <= 0) return;
    if (g_curTex) setTexture(0);
    float w = f.w * scale, h = f.h * scale;
    float x = std::floor(pos.x - w * 0.5f), y = std::floor(pos.y - h);
    float u0 = f.u0, u1 = f.u1;
    if (flipX) std::swap(u0, u1);
    // The lower 40% (trunk, base) stays put; above that the lean grows with the
    // square of the height, so the crown moves most and the bend looks springy.
    auto offsetFor = [&](int row) {
        float t = 1.0f - (row + 0.5f) / f.h;              // 0 at the bottom row, 1 at the top
        float k = std::max(0.0f, (t - 0.4f) / 0.6f);
        return std::round(sway * k * k * scale);
    };
    int start = 0;
    float cur = offsetFor(0);
    for (int row = 1; row <= f.h; row++) {
        float off = row < f.h ? offsetFor(row) : cur + 1e9f;
        if (off == cur) continue;
        // Rows [start, row) share one offset: draw them as one strip.
        float va = f.v0 + (f.v1 - f.v0) * start / f.h, vb = f.v0 + (f.v1 - f.v0) * row / f.h;
        float ya = y + start * scale, yb = y + row * scale;
        quad({x + cur, ya}, {x + cur + w, ya}, {x + cur + w, yb}, {x + cur, yb}, u0, va, u1, vb, c);
        start = row;
        cur = off;
    }
}

void tileAt(const Assets::TileRef& t, float x, float y, float size, Color c) {
    if (!t.valid()) return;
    frame(t.sprite->frame(t.frame), x, y, size, size, c, false, 0);
}

void line(Vec2 a, Vec2 b, float thick, Color c) {
    if (g_curTex) setTexture(0);
    Vec2 d = normalize(b - a);
    Vec2 n(-d.y * thick * 0.5f, d.x * thick * 0.5f);
    float u, v;
    whiteUV(u, v);
    quad(a + n, b + n, b - n, a - n, u, v, u, v, c);
}

void circle(Vec2 center, float r, Color c, int segments) {
    c.a *= g_alphaMul;
    if (g_curTex) setTexture(0);
    float u, v;
    whiteUV(u, v);
    for (int i = 0; i < segments; i++) {
        float a0 = 2 * PI * i / segments, a1 = 2 * PI * (i + 1) / segments;
        Vec2 p0 = center + fromAngle(a0) * r, p1 = center + fromAngle(a1) * r;
        g_verts.push_back({center.x, center.y, u, v, c.r, c.g, c.b, c.a});
        g_verts.push_back({p0.x, p0.y, u, v, c.r, c.g, c.b, c.a});
        g_verts.push_back({p1.x, p1.y, u, v, c.r, c.g, c.b, c.a});
    }
}

void image(const Texture& t, float x, float y, float w, float h, Color c) {
    setTexture(t.id);
    quad({x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}, 0, 0, 1, 1, c);
    setTexture(0);
}

static std::vector<char> g_fontBuf;

// stb_easy_font only knows ASCII 32..126 and indexes its table without a bounds
// check, so anything else has to be folded away before it gets there. Accented
// Latin-1 letters (which the Spanish text uses) drop to their base letter.
static bool needsFold(const std::string& s) {
    for (char c : s)
        if ((unsigned char)c >= 127) return true;
    return false;
}

static std::string asciiFold(const std::string& in) {
    static const char* LATIN1 =
        "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYPs"    // U+00C0..U+00DF
        "aaaaaaaceeeeiiiidnooooo/ouuuuypy";   // U+00E0..U+00FF
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); i++) {
        unsigned char ch = (unsigned char)in[i];
        if (ch == '\n' || (ch >= 32 && ch < 127)) { out += (char)ch; continue; }
        unsigned cp = 0;
        if ((ch & 0xE0) == 0xC0 && i + 1 < in.size()) {          // 2 byte sequence
            cp = ((ch & 0x1Fu) << 6) | ((unsigned char)in[i + 1] & 0x3Fu);
            i += 1;
        } else if ((ch & 0xF0) == 0xE0) {
            i += 2;
        } else if ((ch & 0xF8) == 0xF0) {
            i += 3;
        }
        if (cp >= 0xC0 && cp <= 0xFF) out += LATIN1[cp - 0xC0];
        else if (cp >= 32 && cp < 127) out += (char)cp;
        else if (cp == 0xA1 || cp == 0xBF) continue;             // inverted ! and ?
        else if (cp) out += '?';
    }
    return out;
}

static bool g_textCheck = false, g_textBoxOn = false;
static float g_tb[4];
void setTextCheck(bool enabled) { g_textCheck = enabled; }
void setTextBox(float x0, float y0, float x1, float y1) { g_tb[0] = x0; g_tb[1] = y0; g_tb[2] = x1; g_tb[3] = y1; g_textBoxOn = true; }
void clearTextBox() { g_textBoxOn = false; }

void text(const std::string& raw, float x, float y, Color c, float scale) {
    if (raw.empty()) return;
    if (g_textCheck && g_textBoxOn && x >= g_tb[0] && x < g_tb[2] && y >= g_tb[1] && y < g_tb[3]) {
        float w = textWidth(raw, scale);
        if (x + w > g_tb[2] - 2) std::fprintf(stderr, "[textcheck] %.0fpx over: %s\n", x + w - (g_tb[2] - 2), raw.c_str());
    }
    const bool fold = needsFold(raw);
    std::string folded;
    if (fold) folded = asciiFold(raw);
    const std::string& s = fold ? folded : raw;
    if (g_curTex) setTexture(0);
    size_t need = s.size() * 300 + 256;
    if (g_fontBuf.size() < need) g_fontBuf.resize(need);
    std::string tmp = s;
    int quads = stb_easy_font_print(0, 0, &tmp[0], nullptr, g_fontBuf.data(), (int)g_fontBuf.size());
    float u, v;
    whiteUV(u, v);
    x = std::floor(x);
    y = std::floor(y);
    for (int i = 0; i < quads; i++) {
        // each quad has 4 vertices of 16 bytes: x,y,z (float) + rgba (bytes)
        float* v0p = (float*)(g_fontBuf.data() + i * 4 * 16);
        float* v2 = (float*)(g_fontBuf.data() + i * 4 * 16 + 2 * 16);
        float rx0 = x + v0p[0] * scale, ry0 = y + v0p[1] * scale;
        float rx1 = x + v2[0] * scale, ry1 = y + v2[1] * scale;
        quad({rx0, ry0}, {rx1, ry0}, {rx1, ry1}, {rx0, ry1}, u, v, u, v, c);
    }
}

// ---- sun shadows -----------------------------------------------------------------
void setNoGrain(bool on) { g_noGrain = on; }

void setSun(Vec2 dir, float strength) { g_sunDir = dir; g_sunStrength = clampf(strength, 0, 1); }
float sunStrength() { return g_sunStrength; }
Vec2 sunDir() { return g_sunDir; }

static void shadowQuad(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, float u0, float v0, float u1, float v1, float aTop, float aBase) {
    // p0,p1 the far (top) edge, p2,p3 the base; the far end fades a little (penumbra).
    Vertex a{p0.x, p0.y, u0, v0, 0, 0, 0, aTop};
    Vertex b{p1.x, p1.y, u1, v0, 0, 0, 0, aTop};
    Vertex d{p2.x, p2.y, u1, v1, 0, 0, 0, aBase};
    Vertex e{p3.x, p3.y, u0, v1, 0, 0, 0, aBase};
    g_shadowVerts.push_back(a); g_shadowVerts.push_back(b); g_shadowVerts.push_back(d);
    g_shadowVerts.push_back(a); g_shadowVerts.push_back(d); g_shadowVerts.push_back(e);
}

void shadowSprite(const Assets::Frame& f, Vec2 base, float w, float h, bool flipX) {
    if (g_sunStrength <= 0.001f) return;
    Vec2 reach = g_sunDir * h;
    Vec2 bl(base.x - w * 0.5f, base.y), br(base.x + w * 0.5f, base.y);
    float u0 = flipX ? f.u1 : f.u0, u1 = flipX ? f.u0 : f.u1;
    shadowQuad(bl + reach, br + reach, br, bl, u0, f.v0, u1, f.v1, 0.55f, 1.0f);
}

void shadowBox(float x0, float y0, float x1, float y1, float height) {
    if (g_sunStrength <= 0.001f) return;
    float u0, v0, u1, v1;
    Sprites::uv(Sprites::WHITE, u0, v0, u1, v1);
    float u = (u0 + u1) * 0.5f, v = (v0 + v1) * 0.5f;
    Vec2 reach = g_sunDir * height;
    // The footprint swept along the sun: copies close enough together to join up.
    int steps = std::max(2, (int)std::ceil(length(reach) / 6.0f));
    for (int k = 1; k <= steps; k++) {
        float t = k / (float)steps;
        Vec2 o = reach * t;
        float a = 1.0f - 0.45f * t;
        shadowQuad({x0 + o.x, y0 + o.y}, {x1 + o.x, y0 + o.y}, {x1 + o.x, y1 + o.y}, {x0 + o.x, y1 + o.y}, u, v, u, v, a, a);
    }
}

void flushShadows() {
    if (g_shadowVerts.empty()) return;
    if (g_sunStrength <= 0.001f) { g_shadowVerts.clear(); return; }
    flush();
    GLuint prevFbo = g_layerFbo;
    GLuint prevTex = g_curTex;
    // 1: the mask. Coverage only, and where shadows overlap the deeper one wins.
    glBindFramebuffer(GL_FRAMEBUFFER, g_targets[TARGET_SHADOW].fbo);
    glViewport(0, 0, g_lw, g_lh);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glBlendEquation(GL_MAX);
    glBlendFunc(GL_ONE, GL_ONE);
    glUniform1f(u_sil, 1.0f);
    g_verts.swap(g_shadowVerts);
    g_curTex = 0;
    flush();
    g_shadowVerts.clear();
    glUniform1f(u_sil, 0.0f);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    // 2: back on the world layer, darken by the mask - a few offset copies of it,
    // which softens every edge by a pixel or two.
    glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
    glViewport(0, 0, g_lw, g_lh);
    setTexture(g_targets[TARGET_SHADOW].tex.id);
    static const float OFF[9][2] = {{0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {-1, -1}, {1, -1}, {-1, 1}};
    float a = g_sunStrength / 9.0f * 1.35f;
    float uR = (float)g_lw / g_tw, vT = (float)g_lh / g_th;
    for (auto& o : OFF) {
        float x = g_layerCam.x + o[0], y = g_layerCam.y + o[1];
        // Render targets are stored upside down relative to the sprite space, and the
        // view fills only their lower-left corner.
        quad({x, y}, {x + g_lw, y}, {x + g_lw, y + g_lh}, {x, y + g_lh}, 0, vT, uR, 0, Color(0, 0, 0, a));
    }
    flush();
    setTexture(prevTex);
}

// ---- reflections ---------------------------------------------------------------------
static void pushQuad(std::vector<Vertex>& v, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, Color c) {
    Vertex a{x0, y0, u0, v0, c.r, c.g, c.b, c.a}, b{x1, y0, u1, v0, c.r, c.g, c.b, c.a};
    Vertex d{x1, y1, u1, v1, c.r, c.g, c.b, c.a}, e{x0, y1, u0, v1, c.r, c.g, c.b, c.a};
    v.push_back(a); v.push_back(b); v.push_back(d);
    v.push_back(a); v.push_back(d); v.push_back(e);
}

bool waterQueued() { return !g_waterVerts.empty(); }

void waterFrame(const Assets::Frame& f, float x, float y, float w, float h, Color tint) {
    if (f.w <= 0) return;
    pushQuad(g_waterVerts, x, y, x + w, y + h, f.u0, f.v0, f.u1, f.v1, tint);
}

void waterRect(float x, float y, float w, float h, Color tint) {
    float u0, v0, u1, v1;
    Sprites::uv(Sprites::WHITE, u0, v0, u1, v1);
    float u = (u0 + u1) * 0.5f, v = (v0 + v1) * 0.5f;
    pushQuad(g_waterVerts, x, y, x + w, y + h, u, v, u, v, tint);
}

void reflectSprite(const Assets::Frame& f, Vec2 base, float w, float h, bool flipX, Color tint) {
    if (f.w <= 0 || g_waterVerts.empty()) return;
    // Upside down below the feet: the bottom row of the art touches the baseline.
    float x = std::floor(base.x - w * 0.5f), y = std::floor(base.y - h) + h;
    float u0 = flipX ? f.u1 : f.u0, u1 = flipX ? f.u0 : f.u1;
    pushQuad(g_reflVerts, x, y, x + w, y + h, u0, f.v1, u1, f.v0, tint);
}

void flushWater(float time, Color sky) {
    if (g_waterVerts.empty() || !g_waterProg) { g_waterVerts.clear(); g_reflVerts.clear(); return; }
    flush();
    GLuint prevFbo = g_layerFbo, prevTex = g_curTex;
    g_curTex = 0;
    // 1: where the water lies. Colour = its tint, alpha = how much it shows.
    glBindFramebuffer(GL_FRAMEBUFFER, g_targets[TARGET_WATER].fbo);
    glViewport(0, 0, g_lw, g_lh);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glBlendFuncSeparate(GL_ONE, 0 /* GL_ZERO */, GL_ONE, 0);   // last writer wins: no dark fringes
    g_verts.swap(g_waterVerts);
    flush();
    g_waterVerts.clear();
    // 2: what stands over it, flipped.
    glBindFramebuffer(GL_FRAMEBUFFER, g_targets[TARGET_REFL].fbo);
    glClear(GL_COLOR_BUFFER_BIT);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    g_verts.swap(g_reflVerts);
    flush();
    g_reflVerts.clear();
    // 3: onto the ground.
    glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
    glViewport(0, 0, g_lw, g_lh);
    glUseProgram(g_waterProg);
    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, g_targets[TARGET_REFL].tex.id);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_targets[TARGET_WATER].tex.id);
    glUniform1i(u_wMask, 0);
    glUniform1i(u_wRefl, 1);
    glUniform2f(u_wRes, (float)g_tw, (float)g_th);
    glUniform1f(u_wViewH, (float)g_lh);
    glUniform2f(u_wCam, g_layerCam.x, g_layerCam.y);
    glUniform1f(u_wTime, time);
    glUniform3f(u_wSky, sky.r, sky.g, sky.b);
    glBindVertexArray(g_emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glUseProgram(g_spriteProg);
    g_curTex = prevTex;
}

// ---- ambient occlusion -------------------------------------------------------------
void aoRect(float x, float y, float w, float h) {
    float u0, v0, u1, v1;
    Sprites::uv(Sprites::WHITE, u0, v0, u1, v1);
    float u = (u0 + u1) * 0.5f, v = (v0 + v1) * 0.5f;
    Vertex a{x, y, u, v, 0, 0, 0, 1}, b{x + w, y, u, v, 0, 0, 0, 1}, d{x + w, y + h, u, v, 0, 0, 0, 1}, e{x, y + h, u, v, 0, 0, 0, 1};
    g_aoVerts.push_back(a); g_aoVerts.push_back(b); g_aoVerts.push_back(d);
    g_aoVerts.push_back(a); g_aoVerts.push_back(d); g_aoVerts.push_back(e);
}

void aoSprite(const Assets::Frame& f, Vec2 base, float w, float h, bool flipX, int rows) {
    if (f.w <= 0 || f.h <= 0) return;
    rows = std::min(rows, f.h);
    float scale = h / f.h;
    // Placed exactly as spriteAt places a bottom-anchored sprite.
    float x = std::floor(base.x - w * 0.5f), yb = std::floor(base.y - h) + h, yt = yb - rows * scale;
    float u0 = flipX ? f.u1 : f.u0, u1 = flipX ? f.u0 : f.u1;
    float vt = f.v1 - (f.v1 - f.v0) * rows / f.h;
    Vertex a{x, yt, u0, vt, 1, 1, 1, 1}, b{x + w, yt, u1, vt, 1, 1, 1, 1}, d{x + w, yb, u1, f.v1, 1, 1, 1, 1}, e{x, yb, u0, f.v1, 1, 1, 1, 1};
    g_aoVerts.push_back(a); g_aoVerts.push_back(b); g_aoVerts.push_back(d);
    g_aoVerts.push_back(a); g_aoVerts.push_back(d); g_aoVerts.push_back(e);
}

// ---- the flashlight's occluders (0.11v): the true shapes of what stands in the beam.
static void occQuad(float x, float y, float w, float h, float u0, float v0, float u1, float v1) {
    Vertex a{x, y, u0, v0, 1, 1, 1, 1}, b{x + w, y, u1, v0, 1, 1, 1, 1}, d{x + w, y + h, u1, v1, 1, 1, 1, 1}, e{x, y + h, u0, v1, 1, 1, 1, 1};
    g_occVerts.push_back(a); g_occVerts.push_back(b); g_occVerts.push_back(d);
    g_occVerts.push_back(a); g_occVerts.push_back(d); g_occVerts.push_back(e);
}
void occRect(float x, float y, float w, float h) {
    float u0, v0, u1, v1;
    Sprites::uv(Sprites::WHITE, u0, v0, u1, v1);
    float u = (u0 + u1) * 0.5f, v = (v0 + v1) * 0.5f;
    occQuad(x, y, w, h, u, v, u, v);
}
void occDisc(Vec2 c, float r) {
    // Row by row, so a trunk or a pole is round in the mask too.
    for (float dy = -r; dy < r; dy += 1.0f) {
        float yy = dy + 0.5f;
        float half = std::sqrt(std::max(0.0f, r * r - yy * yy));
        if (half < 0.5f) continue;
        occRect(std::floor(c.x - half), std::floor(c.y + dy), std::ceil(half * 2), 1);
    }
}
void occSprite(const Assets::Frame& f, Vec2 base, float w, float h, bool flipX) {
    if (f.w <= 0 || f.h <= 0) return;
    float x = std::floor(base.x - w * 0.5f), y = std::floor(base.y - h);
    occQuad(x, y, w, h, flipX ? f.u1 : f.u0, f.v0, flipX ? f.u0 : f.u1, f.v1);
}
void flushOcc() {
    flush();
    GLuint prevFbo = g_layerFbo, prevTex = g_curTex;
    glBindFramebuffer(GL_FRAMEBUFFER, g_targets[TARGET_OCC].fbo);
    glViewport(0, 0, g_lw, g_lh);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glBlendEquation(GL_MAX);
    glBlendFunc(GL_ONE, GL_ONE);
    glUniform1f(u_sil, 1.0f);
    g_verts.swap(g_occVerts);
    g_curTex = 0;
    flush();
    g_occVerts.clear();
    glUniform1f(u_sil, 0.0f);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
    glViewport(0, 0, g_lw, g_lh);
    g_curTex = prevTex;
    g_occReady = true;
}

void flushAO(float strength) {
    if (g_aoVerts.empty()) return;
    if (strength <= 0.001f || !g_aoProg) { g_aoVerts.clear(); return; }
    flush();
    GLuint prevFbo = g_layerFbo, prevTex = g_curTex;
    // 1: the mask, coverage only.
    glBindFramebuffer(GL_FRAMEBUFFER, g_targets[TARGET_AO].fbo);
    glViewport(0, 0, g_lw, g_lh);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glBlendEquation(GL_MAX);
    glBlendFunc(GL_ONE, GL_ONE);
    glUniform1f(u_sil, 1.0f);
    g_verts.swap(g_aoVerts);
    g_curTex = 0;
    flush();
    g_aoVerts.clear();
    glUniform1f(u_sil, 0.0f);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    // 2: blurred onto the world layer, outside the shapes only.
    glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
    glViewport(0, 0, g_lw, g_lh);
    glUseProgram(g_aoProg);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_targets[TARGET_AO].tex.id);
    glUniform1i(u_aoMask, 0);
    glUniform2f(u_aoRes, (float)g_tw, (float)g_th);
    glUniform1f(u_aoStrength, strength);
    glBindVertexArray(g_emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glUseProgram(g_spriteProg);
    g_curTex = prevTex;
}

void textShadow(const std::string& s, float x, float y, Color c, float scale, Color shadow) {
    text(s, x + scale, y + scale, shadow, scale);
    text(s, x, y, c, scale);
}

void textCentered(const std::string& s, float cx, float y, Color c, float scale, bool shadow) {
    float w = textWidth(s, scale);
    if (shadow) textShadow(s, std::floor(cx - w / 2), y, c, scale);
    else text(s, std::floor(cx - w / 2), y, c, scale);
}

float textWidth(const std::string& raw, float scale) {
    std::string tmp = needsFold(raw) ? asciiFold(raw) : raw;
    if (tmp.empty()) return 0;
    return stb_easy_font_width(&tmp[0]) * scale;
}

float textHeight(float scale) { return 8 * scale; }

void present(const LightingParams& lp, Vec2 cam, int winW, int winH) {
    flush();
    const PostFx& post = lp.post;

    // Pass 1: light the world, glow and UI layers together, into the target the post
    // pass reads back, rather than straight to the window.
    glBindFramebuffer(GL_FRAMEBUFFER, g_targets[TARGET_POST].fbo);
    glViewport(0, 0, g_vw, g_vh);
    glClearColor(0, 0, 0, 1);
    glDisable(GL_BLEND);
    glUseProgram(g_compProg);
    GLuint p = g_compProg;
    for (int i = 0; i < 3; i++) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, g_targets[i].tex.id);
    }
    glUniform1i(glGetUniformLocation(p, "uWorld"), 0);
    glUniform1i(glGetUniformLocation(p, "uGlow"), 1);
    glUniform1i(glGetUniformLocation(p, "uUI"), 2);
    glUniform2f(glGetUniformLocation(p, "uRes"), (float)g_vw, (float)g_vh);
    glUniform2f(glGetUniformLocation(p, "uUVScale"), (float)g_vw / g_tw, (float)g_vh / g_th);
    glUniform3f(glGetUniformLocation(p, "uAmbient"), lp.ambient.r, lp.ambient.g, lp.ambient.b);
    glUniform1f(glGetUniformLocation(p, "uSaturate"), lp.saturate);
    glUniform3f(glGetUniformLocation(p, "uGrade"), lp.grade.r, lp.grade.g, lp.grade.b);
    glUniform3f(glGetUniformLocation(p, "uLift"), lp.lift.r, lp.lift.g, lp.lift.b);
    glUniform4f(glGetUniformLocation(p, "uFog"), lp.fogColor.r, lp.fogColor.g, lp.fogColor.b, lp.fog);
    glUniform4f(glGetUniformLocation(p, "uFogP"), std::floor(cam.x), std::floor(cam.y), lp.fogTime, lp.fogWind);

    Vec2 camF(std::floor(cam.x), std::floor(cam.y));
    float L[48 * 4], LC[48 * 3];
    int n = 0;
    for (const auto& l : lp.lights) {
        if (n >= 48) break;
        Vec2 s = l.pos - camF;
        if (s.x < -l.radius || s.y < -l.radius || s.x > g_vw + l.radius || s.y > g_vh + l.radius) continue;
        L[n * 4] = s.x; L[n * 4 + 1] = s.y; L[n * 4 + 2] = l.radius; L[n * 4 + 3] = l.intensity;
        LC[n * 3] = l.color.r; LC[n * 3 + 1] = l.color.g; LC[n * 3 + 2] = l.color.b;
        n++;
    }
    glUniform1i(glGetUniformLocation(p, "uNumLights"), n);
    if (n > 0) {
        glUniform4fv(glGetUniformLocation(p, "uL"), n, L);
        glUniform3fv(glGetUniformLocation(p, "uLC"), n, LC);
    }
    Vec2 cp = lp.conePos - camF;
    glUniform4f(glGetUniformLocation(p, "uCone"), cp.x, cp.y, lp.coneAngle, lp.cone ? lp.coneIntensity : 0.0f);
    glUniform2f(glGetUniformLocation(p, "uConeParams"), lp.coneLen, lp.coneHalfWidth);
    float blockers[96 * 4];
    int nb = std::min<int>(96, (int)lp.coneBlockers.size());
    for (int i = 0; i < nb; i++) {
        const ShadowRect& b = lp.coneBlockers[i];
        blockers[i * 4] = b.x0 - camF.x; blockers[i * 4 + 1] = b.y0 - camF.y;
        blockers[i * 4 + 2] = b.x1 - camF.x; blockers[i * 4 + 3] = b.y1 - camF.y;
    }
    glUniform1i(glGetUniformLocation(p, "uNumBlockers"), nb);
    if (nb > 0) glUniform4fv(glGetUniformLocation(p, "uBlockers"), nb, blockers);
    glActiveTexture(GL_TEXTURE0 + 3);
    glBindTexture(GL_TEXTURE_2D, g_targets[TARGET_OCC].tex.id);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(glGetUniformLocation(p, "uOcc"), 3);
    glUniform1i(glGetUniformLocation(p, "uUseOcc"), g_occReady ? 1 : 0);
    g_occReady = false;   // a scene that wants it again builds it again

    glBindVertexArray(g_emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // Pass 2: post-process the frame onto the window. With every weight at zero this
    // is a plain copy, which is what `post.enabled = false` asks for.
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, winH - g_ih * g_scale, g_iw * g_scale, g_ih * g_scale);
    glUseProgram(g_postProg);
    GLuint q = g_postProg;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_targets[TARGET_POST].tex.id);
    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, g_targets[GLOW].tex.id);
    glActiveTexture(GL_TEXTURE0 + 2);
    glBindTexture(GL_TEXTURE_2D, g_targets[UI].tex.id);
    glUniform1i(glGetUniformLocation(q, "uImage"), 0);
    glUniform1i(glGetUniformLocation(q, "uGlow"), 1);
    glUniform1i(glGetUniformLocation(q, "uUI"), 2);
    // World y runs down the screen, texture v up: hence the flipped y.
    Vec2 frac = cam - Vec2(std::floor(cam.x), std::floor(cam.y));
    glUniform2f(glGetUniformLocation(q, "uShift"), frac.x / g_vw, -frac.y / g_vh);
    glUniform2f(glGetUniformLocation(q, "uRes"), (float)g_vw, (float)g_vh);
    glUniform2f(glGetUniformLocation(q, "uUVScale"), (float)g_vw / g_tw, (float)g_vh / g_th);
    glUniform2f(glGetUniformLocation(q, "uUIScale"), (float)g_iw / g_tw, (float)g_ih / g_th);
    glUniform2f(glGetUniformLocation(q, "uTexSize"), (float)g_tw, (float)g_th);
    glUniform1f(glGetUniformLocation(q, "uSharp"), g_zoom > 1.001f ? (float)g_iw * g_scale / g_vw : 0.0f);
    float on = post.enabled ? 1.0f : 0.0f;
    glUniform4f(glGetUniformLocation(q, "uFx"), post.vignette * on, post.aberration * on,
                0.0f, g_noGrain ? 0.0f : post.grain * on);
    glUniform3f(glGetUniformLocation(q, "uFx2"), post.bloom * on, post.contrast * on,
                lerpf(1.0f, post.saturation, on));
    // Two frames per grain step: 60 fps noise reads as interference, not film.
    glUniform1f(glGetUniformLocation(q, "uTime"), std::floor(g_postFrame * 0.5f));
    glBindVertexArray(g_emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glActiveTexture(GL_TEXTURE0);
    g_postFrame += 1.0f;
}

}  // namespace R
