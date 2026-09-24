@echo off
rem Builds Outbound with MSYS2 UCRT64 g++.
rem Requires the packages: mingw-w64-ucrt-x86_64-gcc, mingw-w64-ucrt-x86_64-glfw, mingw-w64-ucrt-x86_64-openal,
rem mingw-w64-ucrt-x86_64-libvorbis (linked statically: no extra DLL)
rem
rem The result in bin\ is the whole game: the exe, its DLLs and every asset it loads
rem (bin\assets, bin\credits.txt). Zip bin\ (or run package.bat) to share it.
setlocal
cd /d "%~dp0"
set MSYS=C:\msys64\ucrt64\bin
set PATH=%MSYS%;%PATH%
if not exist bin mkdir bin

g++ -std=c++17 -O2 -fwrapv -Wall -Wno-missing-braces -Wno-unused-function -Iexternal -Iexternal\sdk\public ^
  src\*.cpp ^
  -o bin\Outbound.exe external\sdk\redistributable_bin\win64\steam_api64.lib -lglfw3 -lopenal -l:libvorbisfile.a -l:libvorbis.a -l:libogg.a -lwinmm -lgdi32 -luser32 -lshell32 -lws2_32 -mwindows
if errorlevel 1 (
  echo BUILD FAILED
  exit /b 1
)

copy /Y external\sdk\redistributable_bin\win64\steam_api64.dll bin\ >nul
if not exist bin\steam_appid.txt echo 480> bin\steam_appid.txt
for %%D in (glfw3.dll libopenal-1.dll libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll) do copy /Y %MSYS%\%%D bin\ >nul

call :assets
if errorlevel 1 (
  echo ASSET COPY FAILED
  exit /b 1
)
echo Build OK: bin\Outbound.exe (with bin\assets)
exit /b 0

rem ---------------------------------------------------------------------------
rem Copies exactly what the game reads at runtime:
rem   assets\sprites\**\*.png   every sprite (the animated-preview Gif folders are skipped)
rem   assets\MoreWeapons\*.png  the elite guns
rem   assets\Furniture\*.png, assets\Input tilemap.png  interiors and button prompts
rem   assets\Darkworld Audio - Survival Effects [Free .ogg]\**\*.ogg   every sound effect
rem   assets\Music\*            the music playlist (mp3 / wav / ogg)
rem   credits.txt               the credits screen
rem robocopy only copies what changed, so rebuilding stays quick. Its exit codes
rem below 8 all mean success.
:assets
robocopy assets\sprites bin\assets\sprites *.png /S /XD Gif gif /PURGE /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 exit /b 1
robocopy assets\MoreWeapons bin\assets\MoreWeapons *.png /PURGE /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 exit /b 1
robocopy assets\Furniture bin\assets\Furniture *.png /PURGE /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 exit /b 1
copy /Y "assets\Input tilemap.png" bin\assets\ >nul
robocopy "assets\Darkworld Audio - Survival Effects [Free .ogg]" "bin\assets\Darkworld Audio - Survival Effects [Free .ogg]" *.ogg /S /PURGE /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 exit /b 1
if exist bin\assets\sounds rmdir /s /q bin\assets\sounds
robocopy assets\Music bin\assets\Music *.mp3 *.wav *.ogg /PURGE /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 exit /b 1
copy /Y credits.txt bin\ >nul
exit /b 0
