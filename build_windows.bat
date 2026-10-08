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
if exist dist rmdir /s /q dist
mkdir dist
if exist build\Release\Arcade.exe (copy build\Release\Arcade.exe dist\ >nul) else (copy build\Arcade.exe dist\ >nul)
copy third_party\SDL2-%SDL_VER%\lib\x64\SDL2.dll dist\ >nul
"%QT_DIR%\bin\windeployqt.exe" --release --qmldir qml --no-translations dist\Arcade.exe || exit /b 1

for %%d in (roms cores dats fonts system saves media\videos media\snaps media\marquees) do mkdir dist\%%d 2>nul
copy extras\fbneo.ini dist\cores\ >nul
copy LEEME.txt dist\ >nul

REM ---- Emulador FinalBurn Neo (nucleo libretro) ----
echo Descargando FinalBurn Neo...
powershell -Command "Invoke-WebRequest https://buildbot.libretro.com/nightly/windows/x86_64/latest/fbneo_libretro.dll.zip -OutFile build\fbneo.zip"
powershell -Command "Expand-Archive build\fbneo.zip -DestinationPath dist\cores -Force"

echo.
echo ===== LISTO =====
echo Tu arcade esta en: %CD%\dist
echo Copia tus ROMs .zip en dist\roms y abre Arcade.exe
