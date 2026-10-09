#include "gamepad.h"
#include "libretro/libretro.h"

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <QKeyEvent>
#include <QKeySequence>
#include <QGuiApplication>
#include <QDebug>

static constexpr int kStickThreshold = 16000;
static constexpr int kTriggerThreshold = 12000;
static constexpr qint64 kRepeatDelay = 380; // ms antes de repetir
static constexpr qint64 kRepeatRate  = 70;  // ms entre repeticiones

static inline uint16_t bit(unsigned id) { return uint16_t(1u << id); }

// Botones RetroPad de cada acción (ActPause no llega al núcleo). NeoGeo en FBNeo: B=A, A=B, Y=C, X=D.
static constexpr uint16_t kBtnA = 1u << RETRO_DEVICE_ID_JOYPAD_B, kBtnB = 1u << RETRO_DEVICE_ID_JOYPAD_A,
                          kBtnC = 1u << RETRO_DEVICE_ID_JOYPAD_Y, kBtnD = 1u << RETRO_DEVICE_ID_JOYPAD_X;
static constexpr uint16_t kActionMask[Gamepad::ActionCount] = {
    kBtnA, kBtnB, kBtnC, kBtnD,
    1u << RETRO_DEVICE_ID_JOYPAD_L, 1u << RETRO_DEVICE_ID_JOYPAD_R,
    1u << RETRO_DEVICE_ID_JOYPAD_L2, 1u << RETRO_DEVICE_ID_JOYPAD_R2,
    1u << RETRO_DEVICE_ID_JOYPAD_SELECT, 1u << RETRO_DEVICE_ID_JOYPAD_START, 0,
    kBtnA, kBtnB, kBtnC, kBtnD,                      // turbo
    kBtnA | kBtnB, kBtnC | kBtnD, kBtnA | kBtnB | kBtnC, // macros
    0, 0,                                            // rebobinar y avance rápido no llegan al núcleo
};
static constexpr std::array<int, Gamepad::ActionCount> kDefaultMap = {
    Gamepad::PhysA, Gamepad::PhysB, Gamepad::PhysX, Gamepad::PhysY,
    Gamepad::PhysLB, Gamepad::PhysRB, Gamepad::PhysLT, Gamepad::PhysRT,
    Gamepad::PhysBack, Gamepad::PhysStart, Gamepad::PhysR3,
    -1, -1, -1, -1, -1, -1, -1, -1, -1,
};

// turboOn: fase del disparo automático (los botones turbo solo cuentan cuando es true)
static uint16_t applyMap(uint16_t phys, const std::array<int, Gamepad::ActionCount> &map, bool *pause, bool turboOn)
{
    uint16_t m = 0;
    for (int a = 0; a < Gamepad::ActionCount; ++a) {
        if (map[size_t(a)] < 0 || !(phys & (1u << map[size_t(a)]))) continue;
        if (a == Gamepad::ActPause) { if (pause) *pause = true; continue; }
        if (a >= Gamepad::ActTurboA && a <= Gamepad::ActTurboD && !turboOn) continue;
        m |= kActionMask[a];
    }
    return m;
}

// Teclas por defecto. RETRO de cada KeyAction en kKeyRetro.
static constexpr std::array<std::array<int, Gamepad::KeyActionCount>, 2> kDefaultKeys = { {
    { Qt::Key_Up, Qt::Key_Down, Qt::Key_Left, Qt::Key_Right, Qt::Key_Z, Qt::Key_X, Qt::Key_A, Qt::Key_S,
      Qt::Key_Q, Qt::Key_W, Qt::Key_5, Qt::Key_1 },
    { Qt::Key_I, Qt::Key_K, Qt::Key_J, Qt::Key_L, Qt::Key_G, Qt::Key_H, Qt::Key_T, Qt::Key_Y,
      0, 0, Qt::Key_6, Qt::Key_2 },
} };
static constexpr unsigned kKeyRetro[Gamepad::KeyActionCount] = {
    RETRO_DEVICE_ID_JOYPAD_UP, RETRO_DEVICE_ID_JOYPAD_DOWN, RETRO_DEVICE_ID_JOYPAD_LEFT, RETRO_DEVICE_ID_JOYPAD_RIGHT,
    RETRO_DEVICE_ID_JOYPAD_B, RETRO_DEVICE_ID_JOYPAD_A, RETRO_DEVICE_ID_JOYPAD_Y, RETRO_DEVICE_ID_JOYPAD_X,
    RETRO_DEVICE_ID_JOYPAD_L, RETRO_DEVICE_ID_JOYPAD_R, RETRO_DEVICE_ID_JOYPAD_SELECT, RETRO_DEVICE_ID_JOYPAD_START,
};

QString Gamepad::deviceName(int dev) const
{
    if (dev < MaxPlayers) return QStringLiteral("MANDO DEL JUGADOR %1").arg(dev + 1);
    return QStringLiteral("TECLADO DEL JUGADOR %1").arg(dev - MaxPlayers + 1);
}

QString Gamepad::rowName(int dev, int row) const
{
    if (dev < MaxPlayers) return actionName(row);
    static const char *names[KeyActionCount] = {
        "ARRIBA", "ABAJO", "IZQUIERDA", "DERECHA", "BOTÓN A", "BOTÓN B", "BOTÓN C", "BOTÓN D",
        "BOTÓN L", "BOTÓN R", "MONEDA", "START",
    };
    return row >= 0 && row < KeyActionCount ? QString::fromUtf8(names[row]) : QString();
}

QString Gamepad::rowBinding(int dev, int row) const
{
    if (dev < MaxPlayers) return bindingName(row, dev);
    if (dev >= deviceCount() || row < 0 || row >= KeyActionCount) return {};
    const int key = m_keys[size_t(dev - MaxPlayers)][size_t(row)];
    switch (key) {
    case 0:              return QStringLiteral("—");
    case Qt::Key_Up:     return QStringLiteral("FLECHA ARRIBA");
    case Qt::Key_Down:   return QStringLiteral("FLECHA ABAJO");
    case Qt::Key_Left:   return QStringLiteral("FLECHA IZQUIERDA");
    case Qt::Key_Right:  return QStringLiteral("FLECHA DERECHA");
    case Qt::Key_Space:  return QStringLiteral("ESPACIO");
    case Qt::Key_Return: return QStringLiteral("ENTER");
    case Qt::Key_Enter:  return QStringLiteral("ENTER (NUM.)");
    }
    return QKeySequence(key).toString().toUpper();
}

void Gamepad::captureRow(int dev, int row)
{
    if (dev < MaxPlayers) { startCapture(row, dev); return; }
    if (dev >= deviceCount() || row < 0 || row >= KeyActionCount) return;
    m_captureAction = -1;
    m_keyCapturePlayer = dev - MaxPlayers;
    m_keyCaptureAction = row;
    emit capturingChanged();
}

void Gamepad::resetDevice(int dev)
{
    if (dev < MaxPlayers) { resetMapping(dev); return; }
    if (dev >= deviceCount()) return;
    m_keys[size_t(dev - MaxPlayers)] = kDefaultKeys[size_t(dev - MaxPlayers)];
    // Una tecla no puede quedar en los dos jugadores a la vez
    auto &other = m_keys[size_t(1 - (dev - MaxPlayers))];
    for (int &k : other)
        for (int mine : m_keys[size_t(dev - MaxPlayers)])
            if (k != 0 && k == mine) k = 0;
    ++m_mapRevision;
    emit mappingChanged();
}

QList<int> Gamepad::keyMapping(int player) const
{
    const auto &k = m_keys[size_t(qBound(0, player, 1))];
    return QList<int>(k.begin(), k.end());
}

void Gamepad::setKeyMapping(const QList<int> &keys, int player)
{
    if (keys.size() != KeyActionCount || player < 0 || player > 1) return;
    std::copy(keys.begin(), keys.end(), m_keys[size_t(player)].begin());
    ++m_mapRevision;
    emit mappingChanged();
}

QString Gamepad::actionName(int action) const
{
    static const char *names[ActionCount] = {
        "BOTÓN A", "BOTÓN B", "BOTÓN C", "BOTÓN D", "BOTÓN L", "BOTÓN R", "BOTÓN L2", "BOTÓN R2",
        "MONEDA", "START", "PAUSA",
        "TURBO A", "TURBO B", "TURBO C", "TURBO D", "MACRO A+B", "MACRO C+D", "MACRO A+B+C",
        "REBOBINAR (MANTENER)", "AVANCE RÁPIDO (MANTENER)",
    };
    return action >= 0 && action < ActionCount ? QString::fromUtf8(names[action]) : QString();
}

QString Gamepad::bindingName(int action, int player) const
{
    static const char *names[PhysCount] = {
        "A / ✕", "B / ○", "X / □", "Y / △", "LB / L1", "RB / R1", "LT / L2", "RT / R2",
        "STICK IZQ. (L3)", "STICK DER. (R3)", "BACK / SHARE", "START / OPTIONS",
    };
    if (action < 0 || action >= ActionCount || player < 0 || player >= MaxPlayers) return {};
    const int p = m_maps[size_t(player)][size_t(action)];
    return p >= 0 && p < PhysCount ? QString::fromUtf8(names[p]) : QStringLiteral("—");
}

QList<int> Gamepad::mapping(int player) const
{
    const auto &m = m_maps[size_t(qBound(0, player, MaxPlayers - 1))];
    return QList<int>(m.begin(), m.end());
}

void Gamepad::setMapping(const QList<int> &map, int player)
{
    if (player < 0 || player >= MaxPlayers) return;
    auto &m_map = m_maps[size_t(player)];
    // Un mapeo guardado por una versión anterior trae menos acciones: las nuevas quedan sin botón
    if (map.size() < ActPause + 1 || map.size() > ActionCount) return;
    for (int v : map)
        if (v < -1 || v >= PhysCount) return;
    m_map = kDefaultMap;
    std::copy(map.begin(), map.end(), m_map.begin());
    ++m_mapRevision;
    emit mappingChanged();
}

void Gamepad::resetMapping(int player)
{
    if (player < 0 || player >= MaxPlayers) return;
    m_maps[size_t(player)] = kDefaultMap;
    ++m_mapRevision;
    emit mappingChanged();
}

void Gamepad::startCapture(int action, int player)
{
    if (action < 0 || action >= ActionCount || player < 0 || player >= MaxPlayers) return;
    m_keyCaptureAction = -1;
    m_capturePlayer = player;
    m_captureAction = action;
    m_captureArmed = false;
    m_capturePrev = 0;
    emit capturingChanged();
}

void Gamepad::cancelCapture()
{
    if (!capturing()) return;
    m_captureAction = -1;
    m_keyCaptureAction = -1;
    emit capturingChanged();
}

Gamepad::Gamepad(QObject *parent) : QObject(parent), m_keys(kDefaultKeys)
{
    m_maps.fill(kDefaultMap);
    SDL_SetMainReady();
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    // Habilita los drivers HIDAPI (DualShock 4, DualSense, Switch Pro, etc.)
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5, "1");

    if (SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK | SDL_INIT_EVENTS) == 0) {
        m_sdlOk = true;
        // Base de datos extra de mandos opcional junto al exe
        SDL_GameControllerAddMappingsFromFile("gamecontrollerdb.txt");
        for (int i = 0; i < SDL_NumJoysticks(); ++i)
            openController(i);
    } else {
        qWarning() << "SDL_Init falló:" << SDL_GetError();
    }

    qApp->installEventFilter(this);

    m_heldClock.start();
    m_menuTimer.setInterval(10);
    connect(&m_menuTimer, &QTimer::timeout, this, &Gamepad::menuTick);
    m_menuTimer.start();
}

Gamepad::~Gamepad()
{
    closeAll();
    if (m_sdlOk) SDL_Quit();
}

void Gamepad::setMode(Mode m)
{
    m_mode = m;
    m_keyboard = {};
    m_keyRewind = false;
    m_heldAction.clear();
    // Evita que el botón usado para lanzar/salir se "arrastre" al otro modo
    updatePadState();
    uint16_t mask = 0;
    for (const Pad &p : m_pads) mask |= p.menu;
    m_prevMenuMask = mask;
    m_exitLatch = true;
    m_pauseLatch = true;
    m_paused = false;
}

void Gamepad::openController(int deviceIndex)
{
    if (!SDL_IsGameController(deviceIndex)) return;
    for (Pad &p : m_pads) {
        if (p.ctrl) continue;
        p.ctrl = SDL_GameControllerOpen(deviceIndex);
        if (!p.ctrl) return;
        p.instanceId = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(p.ctrl));
        qInfo() << "Mando conectado:" << SDL_GameControllerName(p.ctrl);
        emit connectedChanged();
        return;
    }
}

void Gamepad::closeAll()
{
    for (Pad &p : m_pads) {
        if (p.ctrl) SDL_GameControllerClose(p.ctrl);
        p = Pad{};
    }
}

void Gamepad::pumpEvents()
{
    if (!m_sdlOk) return;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_CONTROLLERDEVICEADDED) {
            // Ignora si ya está abierto (SDL avisa también de los presentes al iniciar)
            const SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(e.cdevice.which);
            bool known = false;
            for (const Pad &p : m_pads) known |= (p.instanceId == id);
            if (!known) openController(e.cdevice.which);
        } else if (e.type == SDL_CONTROLLERDEVICEREMOVED) {
            for (Pad &p : m_pads) {
                if (p.ctrl && p.instanceId == e.cdevice.which) {
                    SDL_GameControllerClose(p.ctrl);
                    p = Pad{};
                    emit connectedChanged();
                }
            }
        }
    }
    SDL_GameControllerUpdate();
}

void Gamepad::updatePadState()
{
    for (Pad &p : m_pads) {
        if (!p.ctrl) { p.buttons = p.menu = p.phys = 0; p.guide = p.pause = p.rewind = p.fast = false; continue; }
        SDL_GameController *c = p.ctrl;
        auto btn = [c](SDL_GameControllerButton b) { return SDL_GameControllerGetButton(c, b) != 0; };
        auto ax  = [c](SDL_GameControllerAxis a)   { return int(SDL_GameControllerGetAxis(c, a)); };

        // Botones físicos (SDL usa posiciones tipo Xbox); el mapeo a RetroPad se aplica después.
        uint16_t ph = 0;
        if (btn(SDL_CONTROLLER_BUTTON_A)) ph |= 1u << PhysA; // abajo   (Xbox A / PS ✕)
        if (btn(SDL_CONTROLLER_BUTTON_B)) ph |= 1u << PhysB; // derecha (Xbox B / PS ○)
        if (btn(SDL_CONTROLLER_BUTTON_X)) ph |= 1u << PhysX; // izq.    (Xbox X / PS □)
        if (btn(SDL_CONTROLLER_BUTTON_Y)) ph |= 1u << PhysY; // arriba  (Xbox Y / PS △)
        if (btn(SDL_CONTROLLER_BUTTON_LEFTSHOULDER))  ph |= 1u << PhysLB;
        if (btn(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) ph |= 1u << PhysRB;
        if (ax(SDL_CONTROLLER_AXIS_TRIGGERLEFT)  > kTriggerThreshold) ph |= 1u << PhysLT;
        if (ax(SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > kTriggerThreshold) ph |= 1u << PhysRT;
        if (btn(SDL_CONTROLLER_BUTTON_LEFTSTICK))  ph |= 1u << PhysL3;
        if (btn(SDL_CONTROLLER_BUTTON_RIGHTSTICK)) ph |= 1u << PhysR3;
        if (btn(SDL_CONTROLLER_BUTTON_BACK))  ph |= 1u << PhysBack;
        if (btn(SDL_CONTROLLER_BUTTON_START)) ph |= 1u << PhysStart;

        uint16_t m = 0; // direcciones: no se remapean
        const int lx = ax(SDL_CONTROLLER_AXIS_LEFTX), ly = ax(SDL_CONTROLLER_AXIS_LEFTY);
        if (btn(SDL_CONTROLLER_BUTTON_DPAD_UP)    || ly < -kStickThreshold) m |= bit(RETRO_DEVICE_ID_JOYPAD_UP);
        if (btn(SDL_CONTROLLER_BUTTON_DPAD_DOWN)  || ly >  kStickThreshold) m |= bit(RETRO_DEVICE_ID_JOYPAD_DOWN);
        if (btn(SDL_CONTROLLER_BUTTON_DPAD_LEFT)  || lx < -kStickThreshold) m |= bit(RETRO_DEVICE_ID_JOYPAD_LEFT);
        if (btn(SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || lx >  kStickThreshold) m |= bit(RETRO_DEVICE_ID_JOYPAD_RIGHT);

        bool pause = false;
        p.phys = ph;
        p.menu = m | applyMap(ph, kDefaultMap, nullptr, true);
        p.buttons = m | applyMap(ph, m_maps[size_t(&p - m_pads.data())], &pause,
                                 (m_frame / 3) % 2 == 0); // turbo: ~10 disparos por segundo
        p.pause = pause; // el botón de pausa no llega al juego
        const auto &map = m_maps[size_t(&p - m_pads.data())];
        auto held = [&](Action a) { return map[size_t(a)] >= 0 && (ph & (1u << map[size_t(a)])); };
        p.rewind = held(ActRewind);
        p.fast = held(ActFast);
        p.guide = btn(SDL_CONTROLLER_BUTTON_GUIDE); // botón Xbox / PS
    }
}

void Gamepad::poll()
{
    ++m_frame;
    pumpEvents();
    updatePadState();

    // Salir del juego: Select+Start (View+Menu / Share+Options) o botón Guide/PS
    bool exitCombo = false;
    for (const Pad &p : m_pads) {
        const uint16_t combo = bit(RETRO_DEVICE_ID_JOYPAD_SELECT) | bit(RETRO_DEVICE_ID_JOYPAD_START);
        if ((p.buttons & combo) == combo || p.guide) exitCombo = true;
    }
    if (exitCombo && !m_exitLatch && m_mode == GameMode) {
        m_exitLatch = true;
        emit exitGameRequested();
    } else if (!exitCombo) {
        m_exitLatch = false;
    }

    bool pauseBtn = false;
    for (const Pad &p : m_pads) pauseBtn |= p.pause;
    if (pauseBtn && !m_pauseLatch && m_mode == GameMode) {
        m_pauseLatch = true;
        emit pauseRequested();
    } else if (!pauseBtn) {
        m_pauseLatch = false;
    }
}

bool Gamepad::retroButton(int port, unsigned retroId) const
{
    if (port < 0 || port >= MaxPlayers || retroId > 15) return false;
    uint16_t mask = m_pads[size_t(port)].buttons;
    if (port < 2) mask |= m_keyboard[size_t(port)];
    return (mask >> retroId) & 1;
}

bool Gamepad::rewindHeld() const
{
    bool on = m_keyRewind;
    for (const Pad &p : m_pads) on |= p.rewind;
    return on && m_mode == GameMode;
}

bool Gamepad::fastHeld() const
{
    bool on = false;
    for (const Pad &p : m_pads) on |= p.fast;
    return on && m_mode == GameMode;
}

int Gamepad::connectedCount() const
{
    int n = 0;
    for (const Pad &p : m_pads) n += p.ctrl ? 1 : 0;
    return n;
}

QString Gamepad::firstPadName() const
{
    for (const Pad &p : m_pads)
        if (p.ctrl) return QString::fromUtf8(SDL_GameControllerName(p.ctrl));
    return {};
}

void Gamepad::menuTick()
{
    if (m_mode != MenuMode) {
        if (m_paused) poll(); // para poder quitar la pausa o salir
        return;
    }
    pumpEvents();
    updatePadState();

    uint16_t mask = 0;
    for (const Pad &p : m_pads) mask |= p.menu;
    // Remapeo: el siguiente botón que se pulse se asigna a la acción elegida
    if (m_captureAction >= 0) {
        uint16_t ph = 0;
        for (const Pad &p : m_pads) ph |= p.phys;
        if (!m_captureArmed) {
            m_captureArmed = (ph == 0);
        } else if (const uint16_t fresh = ph & ~m_capturePrev) {
            int phys = 0;
            while (!(fresh & (1u << phys))) ++phys;
            auto &m_map = m_maps[size_t(m_capturePlayer)];
            for (int a = 0; a < ActionCount; ++a)
                if (m_map[size_t(a)] == phys) m_map[size_t(a)] = m_map[size_t(m_captureAction)]; // intercambia
            m_map[size_t(m_captureAction)] = phys;
            m_captureAction = -1;
            ++m_mapRevision;
            emit mappingChanged();
            emit capturingChanged();
        }
        m_capturePrev = ph;
        m_prevMenuMask = mask; // que el botón capturado no cuente como "aceptar"
        m_heldAction.clear();
        return;
    }

    // El botón de pausa también cuenta en modo menú: sirve para cerrar el menú de pausa
    bool pauseBtn = false;
    for (const Pad &p : m_pads) pauseBtn |= p.pause;
    if (pauseBtn && !m_pauseLatch) {
        m_pauseLatch = true;
        emit menuAction(QStringLiteral("pause"));
    } else if (!pauseBtn) {
        m_pauseLatch = false;
    }

    const uint16_t pressed = mask & ~m_prevMenuMask;
    m_prevMenuMask = mask;

    // Botones de un solo disparo
    if (pressed & (bit(RETRO_DEVICE_ID_JOYPAD_B) | bit(RETRO_DEVICE_ID_JOYPAD_START)))
        emit menuAction(QStringLiteral("accept"));
    if (pressed & bit(RETRO_DEVICE_ID_JOYPAD_A))
        emit menuAction(QStringLiteral("back"));
    if (pressed & bit(RETRO_DEVICE_ID_JOYPAD_Y))        // Xbox X / PS □
        emit menuAction(QStringLiteral("search"));
    if (pressed & bit(RETRO_DEVICE_ID_JOYPAD_X))        // Xbox Y / PS △
        emit menuAction(QStringLiteral("favorite"));
    if (pressed & bit(RETRO_DEVICE_ID_JOYPAD_L2))       // gatillos: cambiar de sistema
        emit menuAction(QStringLiteral("systemPrev"));
    if (pressed & bit(RETRO_DEVICE_ID_JOYPAD_R2))
        emit menuAction(QStringLiteral("systemNext"));

    // Direcciones y saltos con auto-repetición
    QString dir;
    if      (mask & bit(RETRO_DEVICE_ID_JOYPAD_UP))    dir = QStringLiteral("up");
    else if (mask & bit(RETRO_DEVICE_ID_JOYPAD_DOWN))  dir = QStringLiteral("down");
    else if (mask & bit(RETRO_DEVICE_ID_JOYPAD_LEFT))  dir = QStringLiteral("left");
    else if (mask & bit(RETRO_DEVICE_ID_JOYPAD_RIGHT)) dir = QStringLiteral("right");
    else if (mask & bit(RETRO_DEVICE_ID_JOYPAD_L))     dir = QStringLiteral("pageUp");
    else if (mask & bit(RETRO_DEVICE_ID_JOYPAD_R))     dir = QStringLiteral("pageDown");

    const qint64 now = m_heldClock.elapsed();
    if (dir.isEmpty()) {
        m_heldAction.clear();
    } else if (dir != m_heldAction) {
        m_heldAction = dir;
        m_nextRepeat = now + kRepeatDelay;
        emit menuAction(dir);
    } else if (now >= m_nextRepeat) {
        m_nextRepeat = now + kRepeatRate;
        emit menuAction(dir);
    }
}

bool Gamepad::eventFilter(QObject *obj, QEvent *ev)
{
    const bool isKey = ev->type() == QEvent::KeyPress || ev->type() == QEvent::KeyRelease;

    // Remapeo del teclado: la siguiente tecla que se pulse se asigna a la acción elegida
    if (m_keyCaptureAction >= 0 && isKey) {
        auto *ke = static_cast<QKeyEvent *>(ev);
        const int key = ke->key();
        if (ev->type() == QEvent::KeyRelease || ke->isAutoRepeat()) return true;
        const bool reserved = key == Qt::Key_Escape || key == Qt::Key_P || key == Qt::Key_Pause
                              || (key >= Qt::Key_F1 && key <= Qt::Key_F12) || key == Qt::Key_unknown
                              || key == Qt::Key_Shift || key == Qt::Key_Control || key == Qt::Key_Alt || key == Qt::Key_Meta;
        if (!reserved) {
            int &slot = m_keys[size_t(m_keyCapturePlayer)][size_t(m_keyCaptureAction)];
            for (auto &player : m_keys) // si la tecla ya se usaba, esa acción recibe la tecla anterior
                for (int &k : player)
                    if (k == key) k = slot;
            slot = key;
            ++m_mapRevision;
            emit mappingChanged();
        }
        if (!reserved || key == Qt::Key_Escape) {
            m_keyCaptureAction = -1;
            emit capturingChanged();
        }
        return true;
    }

    if (m_mode != GameMode || !isKey)
        return QObject::eventFilter(obj, ev);

    auto *ke = static_cast<QKeyEvent *>(ev);
    if (ke->isAutoRepeat()) return true;
    const bool down = ev->type() == QEvent::KeyPress;
    const int key = ke->key();

    switch (key) {
    case Qt::Key_Escape:
        if (down) emit exitGameRequested();
        return true;
    case Qt::Key_P:
    case Qt::Key_Pause:
        if (down) emit pauseRequested();
        return true;
    case Qt::Key_Backspace: // mantener = rebobinar (si está activado en Opciones)
        m_keyRewind = down;
        return true;
    }

    // Teclas asignadas por el usuario (o las de fábrica) para los jugadores 1 y 2
    bool used = false;
    for (size_t player = 0; player < m_keys.size(); ++player) {
        for (int a = 0; a < KeyActionCount; ++a) {
            if (m_keys[player][size_t(a)] != key) continue;
            if (down) m_keyboard[player] |= bit(kKeyRetro[a]);
            else      m_keyboard[player] &= uint16_t(~bit(kKeyRetro[a]));
            used = true;
        }
    }
    if (used) return true;

    // Atajos fijos del jugador 1, si el usuario no les dio otro uso: Enter = start, Espacio = moneda
    unsigned id = 0xFFFF;
    if (key == Qt::Key_Return || key == Qt::Key_Enter) id = RETRO_DEVICE_ID_JOYPAD_START;
    else if (key == Qt::Key_Space) id = RETRO_DEVICE_ID_JOYPAD_SELECT;
    else return QObject::eventFilter(obj, ev); // F2/F5/F7 etc. los maneja la app
    if (down) m_keyboard[0] |= bit(id);
    else      m_keyboard[0] &= uint16_t(~bit(id));
    return true;
}
