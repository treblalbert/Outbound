#!/bin/sh
# Copies every asset the game reads at runtime next to the exe, so bin/ on its own is
# the whole game (build.bat does the same with robocopy):
#   assets/sprites/**/*.png (not the Gif preview folders), assets/MoreWeapons/*.png,
#   assets/Furniture/*.png, assets/Input tilemap.png,
#   assets/Darkworld Audio - Survival Effects [Free .ogg]/**/*.ogg (every sound effect),
#   assets/Music/*.mp3|wav|ogg and credits.txt.
cd "$(dirname "$0")/.."
OUT=${1:-bin}
mkdir -p "$OUT/assets"
{
  find assets/sprites -type f -iname '*.png' ! -ipath '*/gif/*'
  find assets/MoreWeapons -maxdepth 1 -type f -iname '*.png'
  find assets/Furniture -maxdepth 1 -type f -iname '*.png'
  echo 'assets/Input tilemap.png'
  find 'assets/Darkworld Audio - Survival Effects [Free .ogg]' -type f -iname '*.ogg'
  find assets/Music -maxdepth 1 -type f \( -iname '*.mp3' -o -iname '*.wav' -o -iname '*.ogg' \)
} | while IFS= read -r f; do
  d="$OUT/$f"
  if [ ! -f "$d" ] || [ "$f" -nt "$d" ]; then
    mkdir -p "$(dirname "$d")"
    cp "$f" "$d"
  fi
done
rm -rf "$OUT/assets/sounds"
cp -u credits.txt "$OUT/"
