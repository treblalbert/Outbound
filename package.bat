@echo off
rem Builds Outbound and packs everything needed to play it into one zip:
rem   dist\Outbound_0.11v.zip  ->  Outbound\Outbound.exe, the DLLs, assets\, credits.txt
rem Nobody's saves go in: the game makes its own saves\ folder next to the exe.
setlocal
cd /d "%~dp0"
call "%~dp0build.bat"
if errorlevel 1 exit /b 1

set NAME=Outbound_0.11v
if exist dist\Outbound rmdir /s /q dist\Outbound
if exist dist\%NAME%.zip del /q dist\%NAME%.zip
if not exist dist mkdir dist
robocopy bin dist\Outbound /E /XD saves /XF *.log /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 (
  echo PACKAGE FAILED
  exit /b 1
)
powershell -NoProfile -Command "Compress-Archive -Path 'dist\Outbound' -DestinationPath 'dist\%NAME%.zip' -Force"
if errorlevel 1 (
  echo PACKAGE FAILED
  exit /b 1
)
echo Package OK: dist\%NAME%.zip
