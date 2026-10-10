#pragma once
// Entrada unificada: mandos (Xbox, PlayStation, genéricos vía SDL2 GameController)
// y teclado como jugador 1 y jugador 2.

#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QSet>
#include <array>

struct _SDL_GameController;
typedef struct _SDL_GameController SDL_GameController;

class Gamepad : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int connectedCount READ connectedCount NOTIFY connectedChanged)
    Q_PROPERTY(QString firstPadName READ firstPadName NOTIFY connectedChanged)
    Q_PROPERTY(bool capturing READ capturing NOTIFY capturingChanged)
    Q_PROPERTY(int mapRevision READ mapRevision NOTIFY mappingChanged) // cambia con cada remapeo
public:
    // ExternalMode: hay un emulador aparte en marcha; solo se vigila Select+Start mantenido para cerrarlo
    enum Mode { MenuMode, GameMode, ExternalMode };
    static constexpr int MaxPlayers = 4;

    // Botones físicos del mando (posiciones tipo Xbox) y acciones del juego que se les pueden asignar.
    // El mapeo solo afecta al juego: el menú siempre usa los botones por defecto.
    enum Phys { PhysA, PhysB, PhysX, PhysY, PhysLB, PhysRB, PhysLT, PhysRT, PhysL3, PhysR3,
                PhysBack, PhysStart, PhysCount };
    // Las acciones extra van sin botón hasta que el usuario les asigne uno: turbo (disparo automático
    // mientras se mantiene pulsado) y macros (un botón que pulsa varios a la vez).
    enum Action { ActA, ActB, ActC, ActD, ActL, ActR, ActL2, ActR2, ActCoin, ActStart, ActPause,
                  ActTurboA, ActTurboB, ActTurboC, ActTurboD, ActMacroAB, ActMacroCD, ActMacroABC,
                  ActRewind, ActFast, // se mantienen pulsados: rebobinar y avance rápido
                  ActionCount };

    // Teclas del teclado que se pueden asignar a cada jugador (1 y 2)
    enum KeyAction { KeyUp, KeyDown, KeyLeft, KeyRight, KeyA, KeyB, KeyC, KeyD, KeyL, KeyR,
                     KeyCoin, KeyStart, KeyActionCount };

    Q_INVOKABLE int actionCount() const { return ActionCount; }
    Q_INVOKABLE QString actionName(int action) const;
    Q_INVOKABLE QString bindingName(int action, int player = 0) const;
    // Espera el siguiente botón que se pulse y lo asigna a la acción (intercambia si ya estaba en uso)
    Q_INVOKABLE void startCapture(int action, int player = 0);
    Q_INVOKABLE void cancelCapture();
    Q_INVOKABLE void resetMapping(int player = 0);
    bool capturing() const { return m_captureAction >= 0 || m_keyCaptureAction >= 0; }
    int mapRevision() const { return m_mapRevision; }
    // Cada mando (jugador 0..3) tiene su propio mapeo
    QList<int> mapping(int player = 0) const;
    void setMapping(const QList<int> &map, int player = 0);
    QList<int> keyMapping(int player) const;
    void setKeyMapping(const QList<int> &keys, int player);

    // Pantalla de controles: "dispositivos" 0..3 = mando de cada jugador, 4 y 5 = teclado J1 y J2.
    // Cada uno tiene sus filas (acciones) con el botón o la tecla asignada.
    Q_INVOKABLE int deviceCount() const { return MaxPlayers + 2; }
    Q_INVOKABLE QString deviceName(int dev) const;
    Q_INVOKABLE int rowCount(int dev) const { return dev < MaxPlayers ? int(ActionCount) : int(KeyActionCount); }
    Q_INVOKABLE QString rowName(int dev, int row) const;
    Q_INVOKABLE QString rowBinding(int dev, int row) const;
    Q_INVOKABLE void captureRow(int dev, int row);
    Q_INVOKABLE void resetDevice(int dev);

    explicit Gamepad(QObject *parent = nullptr);
    ~Gamepad() override;

    void setMode(Mode m);
    Mode mode() const { return m_mode; }
    // En pausa el núcleo no corre y no llama a poll(): el mando se lee con el timer del menú
    void setPaused(bool p) { m_paused = p; }

    // Llamado por el núcleo en cada cuadro
    void poll();
    bool retroButton(int port, unsigned retroId) const;
    // Sticks como analógicos (-32768..32767): stick 0 = izquierdo, 1 = derecho; eje 0 = X, 1 = Y.
    // El izquierdo sigue contando además como cruceta para los juegos que no usan analógico.
    qint16 analog(int port, unsigned stick, unsigned axis) const;
    // Botones de "mantener pulsado" (mando, o tecla Retroceso para rebobinar)
    // Vibración que pide el núcleo: motor fuerte o débil del mando de ese jugador (0 = parar)
    void setRumble(int port, bool strongMotor, quint16 strength);
    bool rewindHeld() const;
    bool fastHeld() const;

    int connectedCount() const;
    QString firstPadName() const;

signals:
    // Navegación del menú: "up","down","left","right","accept","back","pageUp","pageDown"
    void menuAction(const QString &action);
    // Combinación de salida dentro del juego (Select+Start o botón Guide/PS)
    void exitGameRequested();
    // Botón de pausa dentro del juego (clic del stick derecho, o tecla P / Pausa)
    void pauseRequested();
    void connectedChanged();
    void capturingChanged();
    void mappingChanged();

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override;

private:
    void pumpEvents();
    void menuTick();
    void openController(int deviceIndex);
    void closeAll();
    void updatePadState();

    struct Pad {
        SDL_GameController *ctrl = nullptr;
        int instanceId = -1;
        uint16_t buttons = 0;       // bitmask RETRO_DEVICE_ID_JOYPAD_* con el mapeo del usuario (juego)
        uint16_t menu = 0;          // igual pero con el mapeo por defecto (menú)
        uint16_t phys = 0;          // bitmask de Phys, sin mapear
        bool guide = false;
        bool pause = false;         // clic del stick derecho
        bool rewind = false, fast = false;
        quint16 rumbleStrong = 0, rumbleWeak = 0;
        std::array<qint16, 4> axes{}; // lx, ly, rx, ry
    };
    std::array<Pad, MaxPlayers> m_pads{};
    std::array<uint16_t, 2> m_keyboard{}; // bitmask del teclado: jugador 1 y jugador 2
    unsigned m_frame = 0;           // cuadros de juego leídos: marca el ritmo del turbo
    bool m_keyRewind = false;       // tecla Retroceso mantenida

    Mode m_mode = MenuMode;
    bool m_sdlOk = false;
    QTimer m_menuTimer;

    // Auto-repetición en el menú
    QString m_heldAction;
    QElapsedTimer m_heldClock;
    qint64 m_nextRepeat = 0;
    uint16_t m_prevMenuMask = 0;
    bool m_exitLatch = false;
    QElapsedTimer m_exitHold;
    bool m_pauseLatch = false;

    // Remapeo: m_maps[jugador][acción] = botón físico
    std::array<std::array<int, ActionCount>, MaxPlayers> m_maps;
    // Teclado: m_keys[jugador][KeyAction] = tecla Qt (0 = sin tecla)
    std::array<std::array<int, KeyActionCount>, 2> m_keys;
    int m_mapRevision = 0;
    int m_captureAction = -1;
    int m_capturePlayer = 0;
    int m_keyCaptureAction = -1;    // esperando una tecla para m_keys[m_keyCapturePlayer][...]
    int m_keyCapturePlayer = 0;
    bool m_captureArmed = false;    // espera a que se suelten todos los botones antes de capturar
    uint16_t m_capturePrev = 0;
    bool m_paused = false;
};
