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
public:
    enum Mode { MenuMode, GameMode };
    static constexpr int MaxPlayers = 4;

    explicit Gamepad(QObject *parent = nullptr);
    ~Gamepad() override;

    void setMode(Mode m);
    Mode mode() const { return m_mode; }

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
    void connectedChanged();

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
        uint16_t buttons = 0;       // bitmask RETRO_DEVICE_ID_JOYPAD_*
        bool guide = false;
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
};
