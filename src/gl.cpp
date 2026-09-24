#include "gl.h"
#include <cstdio>

#ifdef __EMSCRIPTEN__
bool gl_load() { return true; }   // WebGL 2 entry points are linked, not looked up
#else
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#define X(ret, name, args) ret (GLAPIENTRY *name) args = nullptr;
OGL_FUNCS
#undef X

static void* getProc(const char* name) {
#ifdef _WIN32
    static HMODULE lib = LoadLibraryA("opengl32.dll");
    using WglGetProc = PROC(WINAPI*)(LPCSTR);
    static WglGetProc wglGet = (WglGetProc)(void*)GetProcAddress(lib, "wglGetProcAddress");
    void* p = wglGet ? (void*)wglGet(name) : nullptr;
    intptr_t v = (intptr_t)p;
    if (v == 0 || v == 1 || v == 2 || v == 3 || v == -1) p = (void*)GetProcAddress(lib, name);
    return p;
#else
    return nullptr;
#endif
}

bool gl_load() {
    bool ok = true;
#define X(ret, name, args) \
    name = (ret (GLAPIENTRY *) args)getProc(#name); \
    if (!name) { std::fprintf(stderr, "OpenGL function missing: %s\n", #name); ok = false; }
    OGL_FUNCS
#undef X
    return ok;
}
#endif  // __EMSCRIPTEN__
