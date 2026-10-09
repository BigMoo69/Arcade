#pragma once
// Entrada unificada: mandos (Xbox, PlayStation, genéricos vía SDL2 GameController)
// y teclado como jugador 1.

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
    enum Mode { MenuMode, GameMode };
    static constexpr int MaxPlayers = 4;

    // Botones físicos del mando (posiciones tipo Xbox) y acciones del juego que se les pueden asignar.
    // El mapeo solo afecta al juego: el menú siempre usa los botones por defecto.
    enum Phys { PhysA, PhysB, PhysX, PhysY, PhysLB, PhysRB, PhysLT, PhysRT, PhysL3, PhysR3,
                PhysBack, PhysStart, PhysCount };
    enum Action { ActA, ActB, ActC, ActD, ActL, ActR, ActL2, ActR2, ActCoin, ActStart, ActPause, ActionCount };

    Q_INVOKABLE int actionCount() const { return ActionCount; }
    Q_INVOKABLE QString actionName(int action) const;
    Q_INVOKABLE QString bindingName(int action) const;
    // Espera el siguiente botón que se pulse y lo asigna a la acción (intercambia si ya estaba en uso)
    Q_INVOKABLE void startCapture(int action);
    Q_INVOKABLE void cancelCapture();
    Q_INVOKABLE void resetMapping();
    bool capturing() const { return m_captureAction >= 0; }
    int mapRevision() const { return m_mapRevision; }
    QList<int> mapping() const;
    void setMapping(const QList<int> &map);

    explicit Gamepad(QObject *parent = nullptr);
    ~Gamepad() override;

    void setMode(Mode m);
    Mode mode() const { return m_mode; }
    // En pausa el núcleo no corre y no llama a poll(): el mando se lee con el timer del menú
    void setPaused(bool p) { m_paused = p; }

    // Llamado por el núcleo en cada cuadro
    void poll();
    bool retroButton(int port, unsigned retroId) const;

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
    };
    std::array<Pad, MaxPlayers> m_pads{};
    uint16_t m_keyboard = 0;        // bitmask del teclado (jugador 1)

    Mode m_mode = MenuMode;
    bool m_sdlOk = false;
    QTimer m_menuTimer;

    // Auto-repetición en el menú
    QString m_heldAction;
    QElapsedTimer m_heldClock;
    qint64 m_nextRepeat = 0;
    uint16_t m_prevMenuMask = 0;
    bool m_exitLatch = false;
    bool m_pauseLatch = false;

    // Remapeo: m_map[acción] = botón físico
    std::array<int, ActionCount> m_map;
    int m_mapRevision = 0;
    int m_captureAction = -1;
    bool m_captureArmed = false;    // espera a que se suelten todos los botones antes de capturar
    uint16_t m_capturePrev = 0;
    bool m_paused = false;
};
