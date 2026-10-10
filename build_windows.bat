@echo off
REM ==========================================================================
REM  Compila Arcade.exe en Windows y arma la carpeta lista para usar (dist\)
REM
REM  Requisitos (una sola vez):
REM   1. Qt 6.5 o superior para MSVC 2019/2022 64-bit con el modulo "Qt Multimedia"
REM      (instalador en qt.io; marca "Additional Libraries > Qt Multimedia")
REM   2. Visual Studio 2022 o 2026 (Community o Build Tools) con "Desarrollo de escritorio con C++"
REM   3. CMake (viene con Visual Studio) y conexion a internet (descarga SDL2 y FBNeo)
REM
REM  Ejecutalo desde "x64 Native Tools Command Prompt" de tu Visual Studio.
REM  Ajusta QT_DIR a tu instalacion:
REM ==========================================================================
if "%QT_DIR%"=="" set QT_DIR=C:\Qt\6.8.3\msvc2022_64
set SDL_VER=2.30.9

if not exist "%QT_DIR%\bin\windeployqt.exe" (
    echo No encuentro Qt en %QT_DIR%. Edita QT_DIR en este archivo.
    exit /b 1
)

REM ---- SDL2 (mandos Xbox / PlayStation) ----
if not exist third_party\SDL2-%SDL_VER% (
    echo Descargando SDL2 %SDL_VER%...
    mkdir third_party 2>nul
    powershell -Command "Invoke-WebRequest https://github.com/libsdl-org/SDL/releases/download/release-%SDL_VER%/SDL2-devel-%SDL_VER%-VC.zip -OutFile third_party\sdl2.zip"
    powershell -Command "Expand-Archive third_party\sdl2.zip -DestinationPath third_party -Force"
)

REM ---- Compilar ----
REM Sin -G: CMake usa el Visual Studio mas nuevo instalado (2022, 2026...)
cmake -S . -B build -A x64 ^
      -DCMAKE_PREFIX_PATH="%QT_DIR%" ^
      -DSDL2_DIR="%CD%\third_party\SDL2-%SDL_VER%\cmake" || exit /b 1
cmake --build build --config Release || exit /b 1

REM ---- Armar dist\ ----
REM dist\ NO se borra: ahi viven tus ROMs, capturas, partidas y ajustes. Solo se actualiza el programa.
if not exist dist mkdir dist
if exist build\Release\Arcade.exe (copy build\Release\Arcade.exe dist\ >nul) else (copy build\Arcade.exe dist\ >nul)
copy third_party\SDL2-%SDL_VER%\lib\x64\SDL2.dll dist\ >nul
"%QT_DIR%\bin\windeployqt.exe" --release --qmldir qml --no-translations dist\Arcade.exe || exit /b 1

for %%d in (roms cores dats fonts system saves media\videos media\snaps media\marquees) do mkdir dist\%%d 2>nul
if not exist dist\cores\fbneo.ini copy extras\fbneo.ini dist\cores\ >nul
copy LEEME.txt dist\ >nul

REM ---- Emuladores (nucleos libretro) ----
REM fbneo = arcade (los .zip sueltos en roms\). Los demas son los sistemas de cores\sistemas.ini.
REM Solo se descargan los que falten: un nucleo ya instalado no se reemplaza, porque una version
REM nueva puede exigir ROMs distintos. Para actualizar uno, borra su .dll de dist\cores y vuelve a ejecutar.
set CORES=fbneo snes9x genesis_plus_gx fceumm gambatte mgba mednafen_pce_fast mame2003_plus pcsx_rearmed flycast mupen64plus_next ^
 picodrive mednafen_saturn mednafen_vb pokemini gw mednafen_supergrafx mednafen_pcfx mednafen_ngp neocd mednafen_wswan handy stella a5200 prosystem virtualjaguar opera gearcoleco freeintv vecx o2em freechaf potator same_cdi melondsds ppsspp dolphin pcsx2 azahar supermodel
for %%c in (%CORES%) do (
    if not exist dist\cores\%%c_libretro.dll (
        echo Descargando nucleo %%c...
        powershell -Command "$ProgressPreference='SilentlyContinue'; try { Invoke-WebRequest https://buildbot.libretro.com/nightly/windows/x86_64/latest/%%c_libretro.dll.zip -OutFile build\%%c.zip; Expand-Archive build\%%c.zip -DestinationPath dist\cores -Force } catch { Write-Host '  no se pudo descargar %%c: se puede copiar a mano en dist\cores' }"
    )
)

echo.
echo ===== LISTO =====
echo Tu arcade esta en: %CD%\dist
echo Copia tus ROMs .zip en dist\roms y abre Arcade.exe
