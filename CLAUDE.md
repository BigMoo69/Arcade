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
| `build_windows.bat` | Descarga SDL2 2.30.9 VC, compila (sin `-G`, usa el VS más nuevo), `windeployqt`, actualiza `dist/` sin borrarla y baja del buildbot de libretro los núcleos que falten |
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
- **Capturas de libretro-thumbnails (2026-10-09):** se bajaron 2.050 PNG (171 MB) del repositorio
  `libretro-thumbnails/FBNeo_-_Arcade_Games` (carpeta `Named_Snaps`; el archivo se llama como el título
  de `resources/fbneo.txt` cambiando ``&*/:`<>?\|"`` por `_`; los clones son enlaces simbólicos) a
  `dist/media/snaps/<rom>.png`, sin sobrescribir nada. Solo quedan 14 zips sin imagen (`apb1`–`apb6`,
  `apbf`, `apbg`, `blasterkit`, `bnstars1`, `rf2`, `robotronyo`, `spbactnj` y el BIOS `skns`).
- **ROMs de MAME del paquete tuarcade (2026-10-09):** se extrajeron los 3.914 zips que FBNeo no conoce a
  `dist/_mame_nuevos/` y se verificaron con MAME 2003-Plus (`--check-rom mame/<rom>` con
  `ARCADE_LOG_ALL=1`, 8 en paralelo, 12 min) junto con los 186 que FBNeo marca ✘: 2.851 no existen en
  MAME 0.78 ("Matched game driver" ausente), 166 fallan, 6 tumban el núcleo, 24 cargan con avisos
  "WRONG CHECKSUM/LENGTH" y **1.053 pasan limpios** (940 nuevos + 113 de los ✘ de FBNeo). Esos 1.053
  están en `dist/roms/mame/` (2,2 GB) con `mame/<rom>|ok` en `estado.txt` y 684 capturas en
  `media/snaps/mame/`. Los "padres" que necesitan los clones salen del propio log (`gamename:<padre>`):
  89 zips de soporte, 60 de ellos enlaces duros a zips de `dist/roms/`; se listan en
  `roms/mame/soporte.txt`, que `GameListModel` usa para no mostrarlos como juegos (vale para cualquier
  carpeta de sistema). Total en la lista: 4.918. Títulos, año y fabricante: de `metadata/mame2003-plus.xml` (libretro/mame2003-plus-libretro, 22 MB)
  a `dist/roms/names.txt` como `mame/<rom>|Título|Año|Fabricante||mame/<padre>` (el 6.º campo, 255
  clones, hace que "ocultar versiones repetidas" también los agrupe). En `dist/_mame_nuevos/` quedan 2.945 zips sin instalar (12,4 GB).
  `--check-rom` ya no llama a `get()` por fila (tardaba ~30 s con miles de juegos; ahora 0,5 s).
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
- **Varios núcleos (2026-10-09):** los `.zip` sueltos en `roms/` siguen siendo de FBNeo; cada sistema
  extra se define en `cores/sistemas.ini` (bloques `[id]` con `nombre`, `nucleo`, `carpeta`, `extensiones`;
  se crea con NES, SNES, Mega Drive, Master System, GB, GBA, PC Engine, PSX y MAME 2003-Plus) y sus ROMs van
  en `roms/<carpeta>/`. El nombre interno lleva la carpeta (`snes/mario`): capturas en
  `media/snaps/snes/mario.png`, guardados en `saves/snes/`. `GameListModel` añade el rol `core`;
  `AppController::ensureCore(coreFile)` carga el núcleo del juego y `LibretroCore` descarga el anterior
  (solo uno en memoria). Opciones por núcleo en `cores/<nombre>.ini`. Nuevo en el host: SRAM (`.srm` en
  `saves/`), ROMs de consola en `.zip` (se extraen con `tar` a `saves/tmp/` cuando el núcleo no pone
  `block_extract`). Solo núcleos con render por software (no hay contexto OpenGL). Probado el cambio
  FBNeo → segunda copia de FBNeo → FBNeo y el aviso de núcleo ausente. En `dist/cores/` hay 7 núcleos más
  bajados del buildbot de libretro (nightly 2026-10-08): `snes9x`, `genesis_plus_gx`, `fceumm`, `gambatte`,
  `mgba`, `mame2003_plus`, `pcsx_rearmed`; los 8 cargan (`Arcade.exe --core-info` los lista). MAME 2003-Plus
  probado con ROMs arcade en `roms/mame/` (galaga, dkong, sf2, tmnt, bublbobl arrancan y dibuja bien;
  `pacman` falla porque en MAME es clon de `puckman`). **Los núcleos de consola no se han probado con
  ningún juego** (el usuario no tiene ROMs de consola todavía). **PC Engine (2026-10-09):** se añadió el
  núcleo `mednafen_pce_fast` (Beetle PCE Fast, carga bien con `--core-info`; ya son 9); el sistema
  `[pcengine]` acepta `pce,cue,chd,zip`, ROMs en `roms/pcengine/`. Los juegos de CD necesitan el BIOS
  `syscard3.pce` en `system/` (lo pone el usuario). Sin probar con juegos. `build_windows.bat` y el workflow de GitHub bajan los 9
  núcleos; el .bat **ya no borra `dist/`** (antes hacía `rmdir`) y solo descarga los núcleos que falten,
  para no cambiar de versión un núcleo cuyos ROMs ya se verificaron. Ojo al lanzar el .bat desde las
  herramientas de esta sesión: `cmd` tiene `NoDefaultCurrentDirectoryInExePath=1`, hay que usar `.\build_windows.bat`.
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
- **Funciones estilo RetroArch, tanda 1 (2026-10-09):** probadas con `--test-actions` en Metal Slug 2.
  - *Favoritos y recientes:* Y/△, F2 o Insert marcan favorito (★ en la lista, `roms/favoritos.txt`);
    `roms/jugados.txt` guarda veces/segundos/última vez (solo partidas de ≥10 s) y el preview lo muestra.
    "★ FAVORITOS" y "RECIENTES" (30 últimos) son listas más del ciclo LT/RT (`GameListModel::favoritesName()`).
  - *Ocultar juegos con ✘:* Opciones (`ui/hideBroken`, `GameListModel::hideBroken`).
  - *Pausa:* 6 ranuras de guardado con miniatura y fecha (`saves/<rom>.state<N>` + `.png`,
    `AppController::stateSlots()`), volumen (`audio/volume`, 0–100), imagen (`video/aspect`: original /
    píxeles exactos / estirada, `EmulatorView::aspectMode`), avance rápido (sin sonido), captura de pantalla
    (`capturas/`; si el juego no tenía imagen pasa a ser su preview) y **opciones del emulador**: lista lo que
    el núcleo declara por `SET_VARIABLES` y cada cambio se guarda en `cores/<núcleo>.ini`.
  - *Teclas en juego:* F4 avance rápido, F5/F7 ranura 1, F8 imagen, F9/F10 volumen, F12 captura.
  - Ojo en QML: no llamar `list` a una propiedad (choca con el id de la lista de juegos).
  - *Ocultar versiones repetidas:* Opciones (`ui/hideClones`, apagada por defecto). `resources/fbneo.txt`
    lleva un 6.º campo con el juego original de cada clon (`cloneof` del DAT; 5.806 clones);
    `GameListModel::markDuplicates()` deja una versión por familia: mejor estado (✔ > sin probar > ✘) y,
    a igualdad, la original. No se aplica en favoritos ni recientes. Las versiones cuyo título de FBNeo
    dice "3/4/6 Players" (`Meta::players`, rol `players`) son familia aparte: queda una de 4 jugadores
    además de la normal, y el preview pone "4 JUGADORES". Con los ROMs del usuario: 3.865 → 1.932.
  - *Turbo, macros y teclado J2 (2026-10-09):* `Gamepad::Action` tiene 7 acciones más, sin botón por
    defecto, que se asignan en CONFIGURAR CONTROLES: TURBO A–D (solo cuenta en la fase activa de
    `m_frame / 3`, ~10 disparos/s) y MACRO A+B, C+D, A+B+C (`kActionMask` con varios bits). `input/map`
    guardado con 11 valores sigue valiendo (las nuevas quedan en -1). Teclado J2: I/J/K/L, G H T Y,
    2 start, 6 moneda (`m_keyboard[2]`). J2 probado con `hold:`/`release:` (pasos nuevos de
    `--test-actions`); **turbo y macros sin probar: hace falta un mando real**.
  - *Efecto CRT (2026-10-09):* `shaders/crt.vert|frag` compilados con `qt_add_shaders` (Qt ShaderTools;
    el workflow instala `qtshadertools`). `EmulatorView::crt` (0 no, 1 plano, 2 curvo; `video/crt`) usa
    un `QSGGeometryNode` con material propio (`CrtMaterial`/`CrtShader` en `emulatorview.cpp`): curvatura,
    haz por línea según brillo, máscara RGB y viñeta. Con el backend software (pruebas offscreen) cae a
    las scanlines simples. Parámetros fijos en `updatePaintNode` (scanline 0.85, mask 0.22).
  - *Marcos:* `video/bezel`. Imagen `media/bezels/<rom>.png` → `default-vertical.png` → `default.png`
    (estirada a toda la ventana, hueco transparente); sin imagen, `plainBezel` en QML rellena las franjas
    con los colores del tema usando `EmulatorView::contentRect`.
  - *Juegos verticales arreglados:* `LibretroCore::aspectRatio()` invertía la relación de FBNeo (que ya
    la da girada, 3:4) y se veían apaisados; ahora solo invierte si no cuadra con el cuadro girado.
  - *Continuar donde lo dejé:* `game/autoResume`; `stopGame()` guarda `saves/<rom>.auto` (partidas ≥10 s)
    y `launch()` lo carga a los 250 ms. *Modo atracción:* `ui/attract` + `ui/attractSeconds` (60);
    `attractOn` en `Main.qml` salta a un juego al azar cada 7 s hasta que haya cualquier entrada.
  - *Pruebas con GPU:* `--test-hidden` (ventana real fuera de pantalla, sin foco) + `--test-actions`;
    las pruebas automáticas van sin sonido. Usar una base aparte con `ARCADE_DIR` (ROMs por enlace duro).
  - *Mapeo por jugador y de teclado (2026-10-09):* `Gamepad::m_maps[4]` (un mapeo por mando;
    `input/map`, `map2`–`map4`) y `m_keys[2][KeyActionCount]` (teclas de J1 y J2; `input/keys1|2`, se
    capturan en `eventFilter`; Esc, P, F1–F12 reservadas; Enter/Espacio siguen de start/moneda de J1 si
    no se reasignan). La pantalla CONTROLES elige "dispositivo" con ◄► (mando J1–J4, teclado J1–J2) vía
    `Pad.deviceName/rowCount/rowName/rowBinding/captureRow/resetDevice`. La captura de mando escucha
    cualquier mando conectado. **No hay mapeo por juego.**
  - *Rebobinar:* `game/rewind` (apagado por defecto). `LibretroCore::runFrame()` guarda un estado cada
    6 cuadros (hasta 600 o 256 MB) y, con Retroceso o el botón asignado a "REBOBINAR (MANTENER)"
    (`Gamepad::rewindHeld()`), carga uno cada 2 cuadros (~3x hacia atrás, sin sonido). También hay acción
    "AVANCE RÁPIDO (MANTENER)". Probado con teclado en Metal Slug 2.
  - *Trucos:* se bajó el paquete `finalburnneo/FBNeo-cheats` (3.415 `.ini`, 31 MB) a
    `dist/system/fbneo/cheats/`. FBNeo los publica como opciones "[Cheat][rom.ini] Nombre"; el panel
    `coreOpts` de la pausa las separa: "TRUCOS" muestra solo esas (sin el prefijo) y "OPCIONES DEL
    EMULADOR" el resto. `stepCoreOption` no guarda los trucos en el `.ini`: duran hasta cerrar el Arcade.
    1.919 de los zips del usuario tienen archivo propio. Solo FBNeo (otros núcleos usan `retro_cheat_set`,
    sin implementar).
  - *Opciones por secciones (2026-10-09):* `options.sections` en `Main.qml` (IMAGEN, SONIDO, JUEGO,
    LISTA DE JUEGOS, APARIENCIA DEL MENÚ); cada opción nueva va en su sección, no en el menú principal.
  - *Sonidos del menú:* `sounds/mover.wav`, `aceptar.wav`, `volver.wav` (`AppController::ensureSounds()`
    los genera si faltan; el usuario puede sustituirlos) y música opcional `sounds/musica.mp3|ogg|wav`.
    `audio/menuSounds`, `audio/menuMusic`. Con `--test-actions`/`--check-rom` no suenan (`m_quiet`).
    **Nadie los ha oído todavía.**
  - *Vibración:* `GET_RUMBLE_INTERFACE` → `Gamepad::setRumble` (SDL). Sin probar con mando.
  - *Récords:* el host ya responde `GET_SAVESTATE_CONTEXT` (FBNeo lo exige para hiscore). Ojo: los
    núcleos llaman a `environment` con `data` nulo para preguntar si algo existe; hay que comprobarlo
    (sin eso FBNeo tumbaba el programa al cargar). `hiscore.dat` (328 KB, de `libretro/FBNeo`
    `metadata/`; el repo `finalburnneo/FBNeo` no lo trae) está en `dist/system/fbneo/`; los récords
    se guardan en `saves/fbneo/<rom>.hi` (probado con 1942). NeoGeo usa su propia memoria (`.fs`).
  - *Ajustes por juego (2026-10-09):* pausa → AJUSTES → "SOLO PARA ESTE JUEGO". Con `m_gameScope`,
    `AppController::videoValue/setVideoValue` y `loadMaps/saveMaps` leen y escriben imagen (crt, aspect,
    scanlines, smooth, bezel) y controles (mapas de mando y teclas) en `juegos.ini`, grupo = rom con `/`
    cambiado por `|` y clave `propio=true`; al salir del juego vuelven los generales. El volumen y las
    opciones del emulador siguen siendo generales. La pausa tiene dos niveles (`pauseMenu.settings`) y
    CONTROLES también se abre desde ahí (`remap` visible en pausa).
  - *Sticks analógicos:* `Gamepad::analog()` responde a `RETRO_DEVICE_ANALOG` (zona muerta 3500); el
    izquierdo sigue contando como cruceta. *Cambio de disco:* `SET_DISK_CONTROL_INTERFACE` →
    `LibretroCore::nextDisk()`; la pausa muestra "CAMBIAR DE DISCO" si hay más de uno. **Ninguno de los
    dos se ha probado** (hace falta mando y un juego de PlayStation de varios discos).
  - *Núcleos con OpenGL (2026-10-09):* `SET_HW_RENDER` (solo `OPENGL` y `OPENGL_CORE`; GLES/Vulkan se
    rechazan). `LibretroCore::createGl()` crea un `QOpenGLContext` + `QOffscreenSurface` + FBO propios
    tras `load_game`; cada cuadro (`RETRO_HW_FRAME_BUFFER_VALID`) se lee con `glReadPixels` a `m_frame`
    (`readHwFrame`, invierte filas si `bottom_left_origin`), así el resto (CRT, capturas, miniaturas)
    no cambia. `glCurrent()` antes de run/reset/serialize; `context_destroy` antes de `unload_game`.
    Probado con `tests/testgl_core.cpp` (núcleo de prueba, objetivo CMake `testgl_libretro`, no se
    compila por defecto) usando `--test-hidden`: la plataforma offscreen de Qt no tiene OpenGL.
    Núcleos añadidos: `flycast` (sistemas `[dreamcast]` y `[naomi]`) y `mupen64plus_next` (`[n64]`);
    los 11 cargan con `--core-info`. **Ningún juego real probado**: faltan ROMs y BIOS (Flycast los busca
    en `system/dc/`). Leer cada cuadro de vuelta cuesta rendimiento a resoluciones altas.
  - *Pantalla de sistemas (2026-10-09), estilo Pegasus:* el Arcade arranca en `home` (`win.homeOpen`),
    una fila de tarjetas: TODOS, FAVORITOS, RECIENTES y cada sistema, **también los de `sistemas.ini`
    sin juegos** ("SIN JUEGOS AÚN" + carpeta donde copiarlos). `GameListModel::systemCards()` da nombre,
    id, cuenta y hasta 4 capturas para el mosaico; si existe `media/sistemas/<nombre en minúsculas, solo
    letras y números>.png` se usa como imagen (p. ej. `neogeo.png`, `cps1.png`). Ⓐ entra a la lista
    (`enterSystem`), Ⓑ en la lista vuelve a sistemas (`goHome`) y Ⓑ en sistemas abre Opciones (también
    F1 o el pie). Escribir en la pantalla de sistemas busca en todos. `ui/lastSystem` recuerda la lista.
    En las pruebas `--test-actions` hay que dar `accept` para entrar a la lista antes de navegar juegos
    (`launch:` funciona igual desde cualquier pantalla).
  - *Vista de tabla, estilo Cemu (2026-10-09):* Opciones → LISTA DE JUEGOS → "VISTA" (`ui/listView`:
    0 lista y preview, 1 tabla; `win.tableView`). La tabla ocupa todo el ancho (sin preview ni video):
    miniatura, juego, sistema, año, fabricante, has jugado, última vez (rol nuevo `lastPlayed`) y ★/✔.
    Las columnas (`listPanel.cThumb`… con `cs`) se encogen en ventanas estrechas. No ordena por columna.
    Probada offscreen con 15 juegos; no con la lista completa de 4.918.
  - *Sistemas en cuadrícula (2026-10-09):* `home` ya no es una fila sino un `GridView` (`cardGrid`) de
    5 columnas que se recorre en las cuatro direcciones (LB/RB salta una página; ◄► dan la vuelta, ▲▼ no).
    La tarjeta elegida "salta": escala 1.16 con rebote, borde blanco y resplandor que late; las demás
    van atenuadas. `systemCards()` pone los sistemas con juegos antes que los vacíos. Probada offscreen
    con 15 tarjetas (ventana cuadrada, cabían todas): **el desplazamiento con más hileras de las que
    caben y la animación no se han visto**.
  - *27 sistemas más (2026-10-09):* Game Gear, SG-1000, Sega CD (`genesis_plus_gx`), 32X (`picodrive`),
    Saturn (`mednafen_saturn`), FDS (`fceumm`), Virtual Boy, Pokémon Mini, Game & Watch, SuperGrafx,
    PC-FX, Neo Geo Pocket, Neo Geo CD, WonderSwan, Lynx, Atari 2600/5200/7800/Jaguar, 3DO, ColecoVision,
    Intellivision, Vectrex, Odyssey 2, Channel F, Supervision y CD-i. 23 núcleos nuevos (65,7 MB, nightly
    del buildbot); **los 34 cargan con `--core-info`, ninguno probado con juegos**. Están en el texto por
    defecto de `sistemas.ini`, en `build_windows.bat` y en el workflow; al `sistemas.ini` del usuario se
    le añadieron los bloques (uno ya existente no se regenera solo). Total: 39 sistemas definidos.
    **Siguen faltando:** DS y 3DS (hace falta puntero táctil en el host), PSP/GameCube/Wii/PS2/Model 3
    (núcleos con OpenGL: `ppsspp`, `dolphin`, `pcsx2`, `supermodel`) y, como programa externo, Xbox,
    360, Vita, PS3 y Wii U.
  - *Fotos de consolas (2026-10-09):* 37 imágenes de Wikimedia Commons (fotos de Evan Amos, 640 px,
    13 MB; dominio público/CC0 o CC BY-SA 3.0, ver `CREDITOS.txt`) en `dist/media/sistemas/<slug>.png|jpg`
    (no van en git). Las tarjetas con foto usan fondo claro. Sin foto: Pokémon Mini y los sistemas
    arcade (mosaico o iniciales). El desplazamiento de la cuadrícula ya se vio en capturas (45 tarjetas).
  - *Puntero: táctil, pistola y mouse (2026-10-09):* `EmulatorView` acepta el mouse (`pointerMoved`,
    eventos de botón) y pasa la posición a 0..1 sobre la imagen (deshace la rotación) →
    `LibretroCore::setPointer/setPointerButtons`; `pointerState()` responde `RETRO_DEVICE_POINTER`,
    `LIGHTGUN` (izq. = gatillo, der. = recargar) y `MOUSE` (relativo), solo en el puerto 0. El host
    declara `GET_INPUT_DEVICE_CAPABILITIES`. Si el juego pide táctil o pistola (`App.pointerUsed`) el
    cursor es una mira y no se oculta. Pasos de prueba `mdown:x;y` / `mup:x;y`. Probado con el núcleo
    de prueba (dibuja un cuadro donde se toca) en `--test-hidden`. Sistema `[nds]` (núcleo `melondsds`,
    `roms/nds/`) definido; el núcleo (5,1 MB) carga con `--core-info` (35 núcleos); **ningún juego probado**; tampoco
    juegos de pistola de FBNeo/MAME. No hay cursor movido con el stick (melonDS trae el suyo:
    stick derecho + R2).
  - *PSP, GameCube, Wii, PS2, 3DS y Model 3 (2026-10-09):* núcleos `ppsspp`, `dolphin`, `pcsx2` (LRPS2),
    `azahar` y `supermodel` (80 MB) + recursos del buildbot (`assets/system/PPSSPP.zip` y `Dolphin.zip`)
    en `dist/system/PPSSPP/` y `dist/system/dolphin-emu/` (el .bat no los baja). 40 núcleos cargan con
    `--core-info`; 46 sistemas definidos. Prueba con archivos falsos (`--check-rom`, plataforma normal
    de Qt, base aparte): PPSSPP "Using OpenGL backend", Dolphin "SetHWRender - using OpenGL 3.0",
    Supermodel "OpenGL 4.1"; PS2 se detiene antes por falta de BIOS (`system/pcsx2/bios/`) y Azahar al
    leer el archivo. **Ningún juego real probado.** Ojo: PPSSPP da `[check] OK` aunque el archivo sea
    basura (carga en segundo plano), y Dolphin dejó el proceso colgado al fallar la carga (hubo que
    matarlo): hay que mirarlo con un juego de verdad.
  - *Barra de búsqueda en la pantalla de sistemas (2026-10-09):* `homeSearch` sobre la cuadrícula; clic
    o Ⓧ/□ = `win.act("search")` (pasa a TODOS con el teclado en pantalla) y escribir con el teclado
    busca directo, como antes. Probada offscreen (clic y `type:`).
    **Cambiado después:** la búsqueda ya no sale de la pantalla de sistemas. `win.homeSearching`
    (inicio + texto o teclado abierto) oculta la cuadrícula y muestra `homeResults` (miniatura, título,
    sistema, año) sobre todos los juegos; Ⓐ/Enter juega desde ahí y Ⓑ/Esc limpia y vuelve a los sistemas
    de un solo paso. El teclado en pantalla (`search`) cambia de padre (`home` / `listPanel`) con
    posición explícita en vez de anclas. `goHome()` limpia la búsqueda.
  - *Emuladores aparte / programas externos (2026-10-10):* en `sistemas.ini` un bloque puede llevar
    `programa = <exe>` (+ `argumentos`, con `{rom}`; si falta, el juego va al final) en vez de `nucleo`.
    `GameListModel` marca esos juegos con `core = "@<id>"` (`externalCommand()`); `AppController::launch`
    los desvía a `launchExternal()` (QProcess, carpeta de trabajo = la del exe). Mientras corre:
    `App.externalRunning`, el mando pasa a `Gamepad::ExternalMode` (solo vigila Select+Start/Guide
    mantenido 1,5 s → `requestExit()` hace `terminate()` y `kill()` a los 2,5 s), el menú no responde
    (overlay "JUGANDO EN …") y `main.cpp` minimiza la ventana y la restaura al acabar. Al cerrar:
    `notePlayed` + ✔ si duró ≥10 s; error si no existe el exe, no arranca o se cierra en <3 s.
    Probado offscreen con `cmd.exe`/`ping` como emulador falso (`ext_test.ps1`): arranque, vuelta al
    menú, los tres errores y el cierre desde el Arcade. **Sin probar:** un emulador real, minimizar y
    restaurar la ventana (se omite en offscreen) y el cierre con el mando. `kill()` solo mata el proceso
    lanzado, no sus hijos. El `sistemas.ini` del usuario tiene ejemplos comentados (Wii U, PS3, Xbox).
    Clave `juegos = <ruta>` (`SystemDef::romDir`): carpeta de juegos fuera de `roms/` (vale también
    para sistemas con núcleo); `carpeta` sigue dando el prefijo del nombre interno (`xbox/<juego>`).
    El usuario tiene xemu y Xenia en `Desktop\Emuladores\Xbox|Xbox 360` con los ISO en `Juegos\`
    (10 de Xbox, 6 de 360, ~90 GB): bloques `[xbox]` y `[xbox360]` activos en su `sistemas.ini`; la
    lista muestra los 16. **No se ha lanzado ninguno** (abriría el emulador en su pantalla): los
    argumentos (`-full-screen -dvd_path`, `--fullscreen=true`) están sin comprobar.
  - **Pendiente de la lista de RetroArch:** run-ahead, trucos en consolas, pistola/
    mouse, más shaders, cámara lenta, grabación de partidas.
  - **Al probar en `dist/`:** respaldar y restaurar `arcade.ini`, `roms/estado.txt`, `roms/jugados.txt`
    y `roms/favoritos.txt` (no borrarlos: el usuario usa el Arcade entre prueba y prueba), o usar una
    base aparte con `ARCADE_DIR`.
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
