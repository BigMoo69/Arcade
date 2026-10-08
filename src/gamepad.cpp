#include "gamepad.h"
#include "libretro/libretro.h"

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <QKeyEvent>
#include <QGuiApplication>
#include <QDebug>

static constexpr int kStickThreshold = 16000;
static constexpr int kTriggerThreshold = 12000;
static constexpr qint64 kRepeatDelay = 380; // ms antes de repetir
static constexpr qint64 kRepeatRate  = 70;  // ms entre repeticiones

static inline uint16_t bit(unsigned id) { return uint16_t(1u << id); }

Gamepad::Gamepad(QObject *parent) : QObject(parent)
{
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
    m_keyboard = 0;
    m_heldAction.clear();
    // Evita que el botón usado para lanzar/salir se "arrastre" al otro modo
    updatePadState();
    uint16_t mask = 0;
    for (const Pad &p : m_pads) mask |= p.buttons;
    m_prevMenuMask = mask;
    m_exitLatch = true;
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
        if (!p.ctrl) { p.buttons = 0; p.guide = false; continue; }
        SDL_GameController *c = p.ctrl;
        auto btn = [c](SDL_GameControllerButton b) { return SDL_GameControllerGetButton(c, b) != 0; };
        auto ax  = [c](SDL_GameControllerAxis a)   { return int(SDL_GameControllerGetAxis(c, a)); };

        uint16_t m = 0;
        // SDL usa posiciones físicas tipo Xbox; RetroPad usa posiciones tipo SNES.
        if (btn(SDL_CONTROLLER_BUTTON_A)) m |= bit(RETRO_DEVICE_ID_JOYPAD_B); // abajo  (Xbox A / PS ✕)
        if (btn(SDL_CONTROLLER_BUTTON_B)) m |= bit(RETRO_DEVICE_ID_JOYPAD_A); // derecha(Xbox B / PS ○)
        if (btn(SDL_CONTROLLER_BUTTON_X)) m |= bit(RETRO_DEVICE_ID_JOYPAD_Y); // izq.   (Xbox X / PS □)
        if (btn(SDL_CONTROLLER_BUTTON_Y)) m |= bit(RETRO_DEVICE_ID_JOYPAD_X); // arriba (Xbox Y / PS △)
        if (btn(SDL_CONTROLLER_BUTTON_LEFTSHOULDER))  m |= bit(RETRO_DEVICE_ID_JOYPAD_L);
        if (btn(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) m |= bit(RETRO_DEVICE_ID_JOYPAD_R);
        if (ax(SDL_CONTROLLER_AXIS_TRIGGERLEFT)  > kTriggerThreshold) m |= bit(RETRO_DEVICE_ID_JOYPAD_L2);
        if (ax(SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > kTriggerThreshold) m |= bit(RETRO_DEVICE_ID_JOYPAD_R2);
        if (btn(SDL_CONTROLLER_BUTTON_LEFTSTICK))  m |= bit(RETRO_DEVICE_ID_JOYPAD_L3);
        if (btn(SDL_CONTROLLER_BUTTON_RIGHTSTICK)) m |= bit(RETRO_DEVICE_ID_JOYPAD_R3);
        if (btn(SDL_CONTROLLER_BUTTON_BACK))  m |= bit(RETRO_DEVICE_ID_JOYPAD_SELECT); // moneda
        if (btn(SDL_CONTROLLER_BUTTON_START)) m |= bit(RETRO_DEVICE_ID_JOYPAD_START);

        const int lx = ax(SDL_CONTROLLER_AXIS_LEFTX), ly = ax(SDL_CONTROLLER_AXIS_LEFTY);
        if (btn(SDL_CONTROLLER_BUTTON_DPAD_UP)    || ly < -kStickThreshold) m |= bit(RETRO_DEVICE_ID_JOYPAD_UP);
        if (btn(SDL_CONTROLLER_BUTTON_DPAD_DOWN)  || ly >  kStickThreshold) m |= bit(RETRO_DEVICE_ID_JOYPAD_DOWN);
        if (btn(SDL_CONTROLLER_BUTTON_DPAD_LEFT)  || lx < -kStickThreshold) m |= bit(RETRO_DEVICE_ID_JOYPAD_LEFT);
        if (btn(SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || lx >  kStickThreshold) m |= bit(RETRO_DEVICE_ID_JOYPAD_RIGHT);

        p.buttons = m;
        p.guide = btn(SDL_CONTROLLER_BUTTON_GUIDE); // botón Xbox / PS
    }
}

void Gamepad::poll()
{
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
}

bool Gamepad::retroButton(int port, unsigned retroId) const
{
    if (port < 0 || port >= MaxPlayers || retroId > 15) return false;
    uint16_t mask = m_pads[size_t(port)].buttons;
    if (port == 0) mask |= m_keyboard;
    return (mask >> retroId) & 1;
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
    if (m_mode != MenuMode) return;
    pumpEvents();
    updatePadState();

    uint16_t mask = 0;
    for (const Pad &p : m_pads) mask |= p.buttons;
    const uint16_t pressed = mask & ~m_prevMenuMask;
    m_prevMenuMask = mask;

    // Botones de un solo disparo
    if (pressed & (bit(RETRO_DEVICE_ID_JOYPAD_B) | bit(RETRO_DEVICE_ID_JOYPAD_START)))
        emit menuAction(QStringLiteral("accept"));
    if (pressed & bit(RETRO_DEVICE_ID_JOYPAD_A))
        emit menuAction(QStringLiteral("back"));

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
    if (m_mode != GameMode || (ev->type() != QEvent::KeyPress && ev->type() != QEvent::KeyRelease))
        return QObject::eventFilter(obj, ev);

    auto *ke = static_cast<QKeyEvent *>(ev);
    if (ke->isAutoRepeat()) return true;
    const bool down = ev->type() == QEvent::KeyPress;

    unsigned id = 0xFFFF;
    switch (ke->key()) {
    case Qt::Key_Up:     id = RETRO_DEVICE_ID_JOYPAD_UP; break;
    case Qt::Key_Down:   id = RETRO_DEVICE_ID_JOYPAD_DOWN; break;
    case Qt::Key_Left:   id = RETRO_DEVICE_ID_JOYPAD_LEFT; break;
    case Qt::Key_Right:  id = RETRO_DEVICE_ID_JOYPAD_RIGHT; break;
    case Qt::Key_Z:      id = RETRO_DEVICE_ID_JOYPAD_B; break; // NeoGeo A
    case Qt::Key_X:      id = RETRO_DEVICE_ID_JOYPAD_A; break; // NeoGeo B
    case Qt::Key_A:      id = RETRO_DEVICE_ID_JOYPAD_Y; break; // NeoGeo C
    case Qt::Key_S:      id = RETRO_DEVICE_ID_JOYPAD_X; break; // NeoGeo D
    case Qt::Key_Q:      id = RETRO_DEVICE_ID_JOYPAD_L; break;
    case Qt::Key_W:      id = RETRO_DEVICE_ID_JOYPAD_R; break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_1:      id = RETRO_DEVICE_ID_JOYPAD_START; break;
    case Qt::Key_5:
    case Qt::Key_Space:  id = RETRO_DEVICE_ID_JOYPAD_SELECT; break; // moneda
    case Qt::Key_Escape:
        if (down) emit exitGameRequested();
        return true;
    default:
        return QObject::eventFilter(obj, ev); // F2/F5/F7 etc. los maneja la app
    }
    if (down) m_keyboard |= bit(id);
    else      m_keyboard &= uint16_t(~bit(id));
    return true;
}
