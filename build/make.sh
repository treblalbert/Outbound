#!/bin/sh
# Incremental dev build (MSYS2 UCRT64). Usage: sh build/make.sh
cd "$(dirname "$0")/.."
export PATH=/c/msys64/ucrt64/bin:$PATH
mkdir -p build/obj bin
fail=0
objs=""
for f in src/*.cpp; do
  n=$(basename "$f" .cpp)
  o=build/obj/$n.o
  objs="$objs $o"
  if [ ! -f "$o" ] || [ "$f" -nt "$o" ] || [ -n "$(find src -name '*.h' -newer "$o")" ]; then
    g++ -std=c++17 -O2 -fwrapv -Wall -Wno-missing-braces -Wno-unused-function -Iexternal -Iexternal/sdk/public -c "$f" -o "$o" || fail=1
  fi
done
[ $fail = 1 ] && { echo BUILD FAILED; exit 1; }
OUT=${OUT:-bin/Outbound.exe}
g++ $objs -o "$OUT" external/sdk/redistributable_bin/win64/steam_api64.lib -lglfw3 -lopenal -l:libvorbisfile.a -l:libvorbis.a -l:libogg.a -lwinmm -lgdi32 -luser32 -lshell32 -lws2_32 || { echo BUILD FAILED; exit 1; }
cp -u external/sdk/redistributable_bin/win64/steam_api64.dll bin/ 2>/dev/null
[ -f bin/steam_appid.txt ] || echo 480 > bin/steam_appid.txt
sh build/copy_assets.sh bin || { echo ASSET COPY FAILED; exit 1; }
echo BUILD OK
