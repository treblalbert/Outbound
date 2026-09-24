#!/bin/sh
# Builds the browser version into web/ as a WebAssembly bundle ready to zip for
# itch.io. Needs the Emscripten SDK; point EMSDK at it if it is not in C:/emsdk.
#
#   sh build_web.sh
#
# Output: web/index.html, web/outbound.js, web/outbound.wasm, web/outbound.data
# plus web/music/ (streamed at runtime, not bundled into the .data).
set -e

EMSDK="${EMSDK:-/c/emsdk}"
if [ -f "$EMSDK/emsdk_env.sh" ]; then
  # emsdk shells out to python, so it needs one on PATH: the Windows Store stub
  # named python3 will not do. Prefer the copy bundled with the SDK - on Windows it
  # sits at python/<version>/python.exe, elsewhere at python/<version>/bin/python3.
  if [ -z "$EMSDK_PYTHON" ]; then
    for p in "$EMSDK"/python/*_64bit/python.exe "$EMSDK"/python/*_64bit/bin/python3 \
             "$HOME/AppData/Local/Programs/Python/Python312/python.exe" \
             "$(command -v python3 2>/dev/null)" "$(command -v python 2>/dev/null)"; do
      [ -n "$p" ] && [ -e "$p" ] || continue
      EMSDK_PYTHON="$p"
      export EMSDK_PYTHON
      break
    done
  fi
  EMSDK_QUIET=1
  export EMSDK_QUIET
  # shellcheck disable=SC1090
  . "$EMSDK/emsdk_env.sh" >/dev/null 2>&1 || true
fi

command -v em++ >/dev/null 2>&1 || {
  echo "em++ not found. Install the Emscripten SDK and/or set EMSDK=/path/to/emsdk" >&2
  exit 1
}

mkdir -p web

# Music is streamed from web/music/ by the browser rather than packed into the
# bundle, so the first load stays small. Rebuild the manifest from what is there.
if [ -d web/music ]; then
  printf '[' > web/music/tracks.json
  first=1
  for f in web/music/*.mp3 web/music/*.ogg; do
    [ -e "$f" ] || continue
    name=$(basename "$f")
    if [ $first -eq 0 ]; then printf ',' >> web/music/tracks.json; fi
    printf '"%s"' "$name" >> web/music/tracks.json
    first=0
  done
  printf ']' >> web/music/tracks.json
  echo "music: $(ls web/music/*.mp3 web/music/*.ogg 2>/dev/null | wc -l) track(s)"
fi

em++ -std=c++17 -O3 -fwrapv \
  -Wall -Wno-missing-braces -Wno-unused-function \
  -Iexternal \
  src/*.cpp \
  -o web/outbound.js \
  -sUSE_GLFW=3 \
  -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sFULL_ES3 \
  -lopenal -lidbfs.js \
  -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=268435456 -sSTACK_SIZE=1048576 \
  -sEXPORTED_FUNCTIONS=_main,_web_saves_ready \
  -sEXPORTED_RUNTIME_METHODS=ccall,stringToNewUTF8 \
  -sMODULARIZE=0 \
  --preload-file assets/sprites@/assets/sprites \
  --preload-file assets/sounds@/assets/sounds \
  --preload-file credits.txt@/credits.txt

echo
echo "Build OK."
ls -la web/outbound.js web/outbound.wasm web/outbound.data 2>/dev/null || true
echo
echo "To publish on itch.io: zip the contents of web/ (index.html at the zip root)"
echo "and tick \"This file will be played in the browser\"."
