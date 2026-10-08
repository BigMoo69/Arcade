# Arcade Multijuegos

Frontend estilo máquina multijuegos (Pandora / NeoGeo) en Qt 6 + QML con FinalBurn Neo integrado
como núcleo libretro. Un solo `Arcade.exe`: menú con previews, mandos Xbox/PlayStation (SDL2),
hasta 4 jugadores, scanlines CRT, guardado de partidas.

## Compilar en Windows
- **Sin instalar nada:** sube esta carpeta a un repositorio de GitHub → pestaña *Actions* →
  descarga el artefacto **Arcade-Windows** (carpeta portable lista, con FBNeo incluido).
- **Local:** instala Qt 6.5+ (MSVC 64-bit + Qt Multimedia) y Visual Studio 2022, edita `QT_DIR`
  en `build_windows.bat` y ejecútalo desde *x64 Native Tools Command Prompt*. Resultado en `dist\`.

## Estructura
| Archivo | Qué hace |
|---|---|
| `qml/Main.qml` | Todo el diseño del menú (colores, layout, textos, opciones) |
| `src/libretrocore.*` | Host libretro: carga `fbneo_libretro.dll`, video, audio, opciones, savestates |
| `src/emulatorview.*` | Dibuja el juego por GPU con aspecto correcto, rotación y scanlines |
| `src/gamepad.*` | SDL2 GameController (Xbox/PS/genéricos) + teclado, navegación del menú |
| `src/gamelistmodel.*` | Escanea `roms/`, títulos (lista integrada + DAT FBNeo), previews en `media/` |
| `resources/names.txt` | Títulos de los juegos típicos de multijuegos |

Instrucciones de uso para el jugador: `LEEME.txt`.
