@echo off
rem Builds Outbound with MSYS2 UCRT64 g++.
rem Requires MSYS2 in C:\msys64 and, from its UCRT64 terminal:
rem   pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-glfw mingw-w64-ucrt-x86_64-openal mingw-w64-ucrt-x86_64-libvorbis
rem (libvorbis is linked statically: no extra DLL)
rem
rem The result in bin\ is the whole game: the exe, its DLLs and every asset it loads
rem (bin\assets, bin\credits.txt). Zip bin\ (or run package.bat) to share it.
rem Double-clicked, it waits for a key at the end so the result can be read;
rem "build.bat nopause" (as package.bat calls it) does not.
setlocal
cd /d "%~dp0"
set NOPAUSE=%1
set MSYSROOT=C:\msys64
set MSYS=%MSYSROOT%\ucrt64\bin
set PATH=%MSYS%;%PATH%

echo Outbound build
echo ==============

rem ---- what the build needs, with what to do when it is missing
if not exist "%MSYSROOT%\" (
  echo.
  echo MSYS2 is not installed in %MSYSROOT%.
  echo Install it from https://www.msys2.org ^(keep the default folder^), then run
  echo this in its "MSYS2 UCRT64" terminal:
  echo   pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-glfw mingw-w64-ucrt-x86_64-openal mingw-w64-ucrt-x86_64-libvorbis
  goto :fail
)
set MISSING=
if not exist "%MSYS%\g++.exe" set MISSING=%MISSING% mingw-w64-ucrt-x86_64-gcc
if not exist "%MSYSROOT%\ucrt64\include\GLFW\glfw3.h" set MISSING=%MISSING% mingw-w64-ucrt-x86_64-glfw
if not exist "%MSYSROOT%\ucrt64\include\AL\al.h" set MISSING=%MISSING% mingw-w64-ucrt-x86_64-openal
if not exist "%MSYSROOT%\ucrt64\lib\libvorbisfile.a" set MISSING=%MISSING% mingw-w64-ucrt-x86_64-libvorbis
if defined MISSING (
  echo.
  echo Some MSYS2 packages are missing. Open the "MSYS2 UCRT64" terminal and run:
  echo   pacman -S%MISSING%
  goto :fail
)
if not exist "external\sdk\redistributable_bin\win64\steam_api64.lib" (
  echo.
  echo The Steamworks SDK is missing: external\sdk\redistributable_bin\win64\steam_api64.lib
  goto :fail
)
if not exist bin mkdir bin

echo [1/3] Compiling (takes a minute or two, nothing is printed meanwhile)...
g++ -std=c++17 -O2 -fwrapv -Wall -Wno-missing-braces -Wno-unused-function -Iexternal -Iexternal\sdk\public ^
  src\*.cpp ^
  -o bin\Outbound.exe external\sdk\redistributable_bin\win64\steam_api64.lib -lglfw3 -lopenal -l:libvorbisfile.a -l:libvorbis.a -l:libogg.a -lwinmm -lgdi32 -luser32 -lshell32 -lws2_32 -mwindows
if errorlevel 1 (
  echo.
  echo The compiler reported errors ^(above^).
  goto :fail
)

echo [2/3] Copying the DLLs...
copy /Y external\sdk\redistributable_bin\win64\steam_api64.dll bin\ >nul
if not exist bin\steam_appid.txt echo 480> bin\steam_appid.txt
for %%D in (glfw3.dll libopenal-1.dll libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll) do copy /Y "%MSYS%\%%D" bin\ >nul

echo [3/3] Copying the assets...
call :assets
if errorlevel 1 (
  echo.
  echo Copying the assets failed.
  goto :fail
)
echo.
echo Build OK: bin\Outbound.exe (with bin\assets)
if /i not "%NOPAUSE%"=="nopause" pause
exit /b 0

:fail
echo.
echo BUILD FAILED
if /i not "%NOPAUSE%"=="nopause" pause
exit /b 1

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
