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
| `src/theme.*` | Temas de color y fondo de pantalla (integrados + `themes/` + `fondos/`) |
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
- **Previews (2026-10-08):** capturas/marquesinas aceptan también `.bmp` y `.jpeg`. El usuario tiene 185
  capturas en `dist/media/snaps/` (venían nombradas con la suma hex de los primeros 1024 bytes del `p1`
  de cada ROM, formato NeoRAGEx; se renombraron a `<rom>.bmp`, originales en `dist/media/snaps_originales/`).
- **Prueba con FBNeo real (2026-10-08):** el núcleo carga, dibuja y la ventana/aspecto/scanlines van bien,
  y **Metal Slug 2 arranca** (pantalla de título vista, CPU ~12 %). Los ROMs del usuario son un set antiguo
  (NeoRAGEx 5.x, archivos `*.rom`): con el `neogeo.zip` nuevo **83 de 196 arrancan** y 113 no (CRC
  distinto, o nombre que FBNeo no conoce como `kof2002-5a`, `svcchaos`).
- **Detección de ROM malo:** `retro_load_game` devuelve true aunque el ROM no sirva (FBNeo pinta su
  pantalla gris de error). `LibretroCore` captura el log durante la carga: líneas "is required" =
  faltan archivos; y con FBNeo exige ver "Driver successfully started" (`loadLooksBad()`). Si falla,
  `AppController::launch` descarga el juego, muestra el error en español y marca el ROM.
- **Marcas ✔/✘ en la lista:** rol `status` del modelo, guardado en `roms/estado.txt` (`rom|ok` / `rom|x`),
  se actualiza solo al lanzar cada juego. Sin entrada = sin probar (sin marca).
- **Pausa:** clic del stick derecho (R3, ya no se envía al juego) o tecla P/Pausa. `LibretroCore::setPaused`
  detiene cuadros y suspende el audio; en pausa `Gamepad::menuTick` sigue llamando a `poll()`.
  Probado con la tecla P (overlay "PAUSA", reanuda bien); R3 con mando real sin probar.
- **Reparación de ROMs (2026-10-08):** 30 juegos se arreglaron reconstruyendo los archivos que FBNeo pide
  a partir de los datos del propio ZIP (sets antiguos: C-ROMs con las mitades intercambiadas, partidos o
  pegados; 3 tomaron archivos de otro ZIP). Al ZIP se le añaden los archivos nuevos sin quitar nada;
  originales en `dist/roms_originales/`. Quedó en 113 ✔ / 83 ✘ tras esa primera ronda.
- **Segunda ronda con el DAT de FBNeo (2026-10-08):** comparando CRC contra el DAT oficial de NeoGeo
  (libretro/FBNeo `dats/`) se recuperaron 20 más → **133 ✔ / 63 ✘**. 14 se reconstruyeron como otro set
  que FBNeo sí conoce (`kof2002`→`kof2k2fd`, `kof2001`→`kof2k1fd`, `kof98`→`kof98h`, `kof95`→`kof95a`,
  `kof97-5a`→`kof97pls`, `bstars`→`bstarsh`, `socbrawl`→`socbrawlh`, `vliner`→`vliner6e`, `tws96`→`twsoc96`,
  `mosyougi`→`moshougi`, `ncolumns`→`columnsn`, `ltorb1`→`ltorb`, `frogfest`→`ngfrog`, `ironclad`→`ironclado`);
  el ZIP viejo pasó a `dist/roms_originales/`, la captura se copió al nombre nuevo y el título va en
  `dist/roms/names.txt`. Otros 6 necesitaban archivos duplicados o rellenos (`diggerma`, `flipshot`,
  `miexchng`, `puzzldpr`, `strhoop`, `samsho3`→`samsho3h`). A los 63 restantes les falta al menos un
  archivo con datos realmente distintos (casi siempre `p1`, `m1`, `s1` o C-ROMs encriptados).
- **Paquete `Downloads\neogeo_202403` (2026-10-08):** romset de archive.org con 117 ZIP en `Juegos\`;
  115 coinciden al 100 % con el DAT de FBNeo. Se instalaron los 26 que el usuario no tenía funcionando
  (19 con ✘ + 7 originales que sustituyen a las conversiones `kof2k2fd`, `kof2k1fd`, `kof98h`, `kof95a`,
  `bstarsh`, `socbrawlh`, `samsho3h`, movidas a `dist/roms_convertidos/`). Con `kof2002` original se pudo
  armar además `kof2002-5d`→`kf2k2mpl`. Estado: **153 ✔ / 43 ✘**. Los 43 son hacks de NeoRAGEx (`*-5a`…,
  `cthd2003`, `svcchaos`, `kof10th`…) y unos pocos originales que el paquete no trae (`s1945p`, `preisle2`,
  `zupapa`, `ganryu`, `nitd`, `rotd`, `pnyaa`, `bangbead`, `jockeygp`, `lans2004`, `neomrdo`, `gururin`,
  `janshin`, `fightfev`). `Imgs NEOGEO.zip` trae 116 PNG 640×480 (captura + caja + logo) con nombre de
  ROM; se instalaron en `dist/media/snaps/` (115 juegos; el `.png` tiene prioridad sobre el `.bmp` viejo). `NEOGEO.zip` (2,2 GB) parece ser lo mismo que `Juegos\`, no se abrió.
- **Paquete `Downloads\tuarcade-cuarentena.part1-3.rar` (2026-10-09):** MameUI32 antiguo con 8.799 ROMs
  (25 GB, RAR con contraseña que dio el usuario). 4.885 nombres existen en FBNeo; se extrajeron los 4.747
  que no tenía funcionando, se verificó cada uno con `--check-rom` (8 procesos en paralelo, 71 min, en una
  base de prueba con enlaces duros y `ARCADE_DIR`) y **pasaron 3.527**; se instalaron junto con 156 zips
  de soporte (padres/BIOS que solos no arrancan, quedan con ✘) y 1.603 capturas. Estado: **3.865 juegos,
  3.680 ✔ / 186 ✘**, `dist/` ≈ 10,8 GB. Ningún CPS-2, CPS-3 ni PGM de ese paquete pasó (sets demasiado
  viejos). De los 14 NeoGeo pendientes entraron 9; siguen mal `fightfev`, `gururin`, `janshin`, `neomrdo`,
  `pnyaa`. No se extrajeron el exe, los CHD ni los 3.914 ROMs que FBNeo no conoce. ~2.060 juegos sin imagen.
- **NeoRAGEx 5.2a** (`Desktop\NeoRAGEx 5.2a`, de donde salieron ROMs y capturas): ignora el parámetro de
  línea de comandos (abre su menú) y cambia la pantalla a 640×480, así que no sirve como emulador de
  respaldo lanzado desde el Arcade. Es de código cerrado; no se puede fusionar con FBNeo.
- **Remapeo de botones:** Opciones → "CONFIGURAR CONTROLES" (`remap` en `Main.qml`). `Gamepad` separa
  botones físicos (`Phys`) de acciones (`Action`: A/B/C/D, L/R/L2/R2, moneda, start, pausa) con `m_map`;
  `startCapture()` asigna el siguiente botón pulsado e intercambia si ya estaba en uso. Se guarda en
  `input/map` de `arcade.ini`. Solo afecta al juego (el menú usa el mapeo por defecto), es el mismo
  para los 4 mandos y no cubre teclado ni direcciones. Pantalla vista; la captura con mando real sin probar.
- **Filtro por sistema y buscador (2026-10-08):** `GameListModel` guarda todos los juegos en `m_all` y
  expone una vista filtrada (`m_view`) por `system` y `search` (todas las palabras, en título o nombre
  del zip). El sistema de cada ROM sale de `resources/fbneo.txt` (rom|título|año|fabricante|sistema,
  8.421 sets generados del DAT "Arcade only" de FBNeo; regenerar con ese DAT si se actualiza el núcleo);
  `names.txt` manda en el título y admite un 5.º campo de sistema. Hacks con sufijo (`kof2002-5a`) heredan
  el sistema del juego base; lo desconocido va a "OTROS". Mando: LT/RT cambia de sistema, X/□ abre el
  buscador con teclado en pantalla (A escribe, B borra, LB/RB recorre resultados). Teclado: Tab/Shift+Tab
  sistema. Hay una barra de búsqueda fija sobre la lista (`searchBar`): en el menú las letras y números
  escriben directo en ella (ya no existen los atajos Z/X/Q/W/1/F), Retroceso borra, Esc limpia y la ✕ o un
  clic en la barra la limpian / abren el teclado en pantalla. El teclado en pantalla (`search`) se despliega
  dentro del panel de la lista, entre la barra y los resultados; el preview de la derecha no se tapa. `lastIndex` guarda la posición en la lista completa.
  Hoy el usuario solo tiene NEO GEO, así que LT/RT avisa "solo hay un sistema".
- **Temas y fondos (2026-10-08):** `src/theme.*` (context prop `Theme`). `Theme.c` es un mapa con
  accent, accent2, onAccent, text, dim, bg1, bg2, header1, header2, panel, border, grid, showGrid, darken;
  `Main.qml` toma de ahí `cAccent`, `cPanel`, etc. 5 temas integrados (clásico, neón, fósforo verde,
  atardecer, hielo) + temas del usuario en `themes/<carpeta>/theme.ini` (líneas `clave = valor`, `;` para
  notas; se crea `themes/ejemplo/` como plantilla; `background.*` en la carpeta = fondo del tema).
  Fondos sueltos en `fondos/` (png/jpg/bmp). Se eligen en Opciones con ◄► ("TEMA", "FONDO": el del tema →
  ninguno → cada imagen) y se guardan en `ui/theme` y `ui/background` de `arcade.ini`. En `dist/fondos/`
  hay 17 fondos copiados de los skins de NeoRAGEx del usuario. `Theme` se crea antes que el motor QML.
- **Mouse en el menú (2026-10-08):** clic selecciona, doble clic (fila o preview) juega, rueda recorre;
  son clicables la etiqueta de sistema (mitad izq./der.), el pie (JUGAR/OPCIONES/BUSCAR/SISTEMA), las
  opciones, el remapeo, las teclas del buscador y los avisos; clic fuera de un panel lo cierra. El cursor
  aparece al mover el mouse y se oculta a los 3 s (en juego solo durante el aviso de salir). La lista ya
  no se arrastra (`interactive: false`).
- **Menú de pausa (2026-10-09):** la pausa (R3 / P) ya no es solo un letrero: `pauseMenu` en `Main.qml`
  ofrece CONTINUAR, GUARDAR PARTIDA, CARGAR PARTIDA, REINICIAR JUEGO y SALIR DEL JUEGO (este sale directo,
  sin el aviso de confirmación). `AppController::togglePause()` pone el mando en modo menú mientras dura
  la pausa; `Gamepad::menuTick` emite `menuAction("pause")` con el botón de pausa para poder cerrarla.
- **Aviso al salir del juego:** Select+Start / Guide / Esc llaman a `AppController::requestExit()`, que
  congela el juego, pone el mando en modo menú y muestra "¿SALIR DEL JUEGO?" (`exitDlg` en `Main.qml`)
  con "NO" marcado por defecto; `answerExit(bool)` sale o reanuda (respeta si ya estaba en pausa).
  Teclado en el aviso: ◄► + Enter, Esc/N = seguir, S = salir.
- **Prueba de UI sin teclado ni pantalla:** (pasos extra: `exit` pide salir del juego, `click:x;y` y
  `dclick:x;y` con coordenadas 0..1 de la ventana) `Arcade.exe --windowed --test-actions "search,down,accept,shot:a.png"`
  con `QT_QPA_PLATFORM=offscreen`, `QT_QUICK_BACKEND=software`, `QT_QPA_FONTDIR=C:\Windows\Fonts` y
  `QT_QPA_PLATFORM_PLUGIN_PATH=F:\Qt\<ver>\msvc2022_64\plugins\platforms`. Usar esto y NO SendKeys /
  capturas de pantalla: el usuario suele estar trabajando en el PC y las teclas simuladas caen en sus ventanas.
- **Herramientas de prueba:** `Arcade.exe --rom <rom>` (arranca directo) y `--check-rom <rom>` (carga sin
  ventana; `[check] OK|FALLA` en stderr con `QT_FORCE_STDERR_LOGGING=1`; `ARCADE_LOG_ALL=1` vuelca todo
  el log del núcleo). Un proceso por ROM: cargar muchos seguidos en el mismo proceso hace caer a FBNeo.
- **Falta verificar** (lo tiene que hacer el usuario): audio sin cortes, mandos reales, juegos verticales.

## Ideas siguientes (si el usuario las pide)
Favoritos / más jugados, sonidos de menú, menú dentro de la pausa, remapeo por jugador y de teclado,
filtros por sistema (NeoGeo / CPS / PGM), shaders CRT más elaborados, modo kiosko al arrancar Windows.
