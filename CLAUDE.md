# Arcade Multijuegos — contexto para Claude Code

Frontend estilo máquina multijuegos arcade de principios de los 2000 (Pandora Box / NeoGeo):
un solo `Arcade.exe` que muestra la lista de juegos con previews y corre los ROMs con
**FinalBurn Neo** cargado como núcleo libretro (`cores/fbneo_libretro.dll`).
El usuario habla español; responde en español, conciso y directo.

## Stack
- C++17, Qt 6 (Quick + Multimedia), QML para toda la UI
- SDL2 (GameController) para mandos Xbox / PlayStation / genéricos, hasta 4 jugadores
- Host libretro propio (no RetroArch). Sin ROMs incluidos: el usuario pone los suyos en `roms/`.

## Arquitectura
| Archivo | Responsabilidad |
|---|---|
| `src/main.cpp` | Arranque, coloca la ventana (monitor + pantalla completa/ventana), carpeta base = carpeta del exe (o env `ARCADE_DIR`), registra `EmulatorView`, context props `App`, `Games`, `Pad`, `arcadeFont` |
| `src/libretrocore.*` | Carga la DLL con QLibrary, callbacks estáticos (singleton `s_self`), environment (pixel format, rotación, SET_VARIABLES legacy + overrides de `cores/fbneo.ini`), audio por QAudioSink push (~80 ms, descarta excedente), ritmo de cuadros con QTimer 1 ms + acumulador según `av_info.timing.fps`, savestates |
| `src/emulatorview.*` | QQuickItem con `updatePaintNode`: textura escalada por GPU, aspecto correcto, rotación libretro (antihoraria, invierte aspecto si es impar como RetroArch), máscara de scanlines estirada |
| `src/gamepad.*` | SDL2 sin ventana (`SDL_MAIN_HANDLED`), mapeo SDL→RetroPad (A→B, B→A, X→Y, Y→X), stick = cruceta, gatillos = L2/R2, teclado como J1 vía eventFilter en modo juego, señales `menuAction` con auto-repetición, salida con Select+Start o Guide |
| `src/gamelistmodel.*` | Escanea `roms/*.zip|*.7z`, oculta BIOS (`neogeo`, `pgm`…), títulos: `resources/names.txt` integrado → `roms/names.txt` del usuario → DATs XML en `dats/`. Previews en `media/videos|snaps|marquees/<rom>.*` |
| `src/appcontroller.*` | Puente QML: launch/stop/reset/save/load, ajustes en `arcade.ini` (QSettings) |
| `qml/Main.qml` | Toda la UI: lista numerada, preview con video (retraso 350 ms), opciones, errores, toasts, teclas F1–F11 |
| `build_windows.bat` | Descarga SDL2 2.30.9 VC, compila (sin `-G`, usa el VS más nuevo), `windeployqt`, arma `dist/` y baja FBNeo del buildbot de libretro |
| `.github/workflows/windows.yml` | Mismo build en GitHub Actions → artefacto `Arcade-Windows` |

## Entorno del usuario (Windows)
- Proyecto: `C:\Users\xXx_MooGaming_xXx\Desktop\ArcadeMulti\ArcadeMulti`
- Qt en `F:\Qt\<versión>\msvc2022_64` (con Qt Multimedia)
- Visual Studio **2026** (carpeta `F:\Microsoft Visual Studio\18\Community`, toolset v180), CMake 4.3
- Compilar desde "x64 Native Tools Command Prompt":
  ```bat
  cd /d C:\Users\xXx_MooGaming_xXx\Desktop\ArcadeMulti\ArcadeMulti
  rmdir /s /q build
  for /d %v in (F:\Qt\6.*) do set QT_DIR=%v\msvc2022_64
  build_windows.bat
  ```

## Estado
- Compila limpio en Linux (Qt 6.4, GCC). Probado con un núcleo libretro falso: menú, lanzar juego,
  input, scanlines, savestate, volver al menú y opciones funcionan.
- **Compilado en Windows (VS 2026) el 2026-10-08:** `dist/` contiene `Arcade.exe`, DLLs de Qt,
  `SDL2.dll` y `cores/fbneo_libretro.dll`. (El primer intento falló porque el script forzaba
  `-G "Visual Studio 17 2022"` → MSB8020; se quitó el `-G`.) `dist/roms/` aún vacío.
- **Arranque probado en Windows el 2026-10-08:** `dist\Arcade.exe` abre el menú ("ARCADE MULTIJUEGOS"),
  detecta el mando (`Xbox One Controller`), Qt Multimedia carga FFmpeg y cierra limpio. Sin errores en stderr.
- **Multi-monitor (2026-10-08):** el usuario tiene principal 2560×1440 al 100 % y secundario 4K al 150 %.
  La ventana salía en el secundario y más grande que la pantalla. Ahora `main.cpp` fija monitor y
  geometría antes de mostrarla (QML ya no pone `visibility`); monitor elegible con `video/screen`
  en `arcade.ini` (0 = principal). Verificado en ambos monitores, pantalla completa y ventana.
- **No probado aún** con FinalBurn Neo real + ROM real (verificar: arranque NeoGeo con `neogeo.zip`,
  juegos verticales rotados como `1944`, audio sin cortes, mandos PS/Xbox reales).

## Ideas siguientes (si el usuario las pide)
Favoritos / más jugados, sonidos de menú, pausa en juego con menú, remapeo de botones,
filtros por sistema (NeoGeo / CPS / PGM), shaders CRT más elaborados, modo kiosko al arrancar Windows.
