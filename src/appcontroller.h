#pragma once
#include <QObject>
#include <QSettings>
#include <QElapsedTimer>
#include <QVariantList>

class LibretroCore;
class Gamepad;
class GameListModel;

// Puente entre QML y el resto: lanzar/cerrar juegos, ajustes y mensajes.
class AppController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool gameRunning READ gameRunning NOTIFY gameRunningChanged)
    Q_PROPERTY(QString currentTitle READ currentTitle NOTIFY gameRunningChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY pausedChanged)
    Q_PROPERTY(bool confirmingExit READ confirmingExit NOTIFY confirmingExitChanged)
    Q_PROPERTY(bool scanlines READ scanlines WRITE setScanlines NOTIFY settingsChanged)
    Q_PROPERTY(bool smooth READ smooth WRITE setSmooth NOTIFY settingsChanged)
    Q_PROPERTY(bool fullscreen READ fullscreen WRITE setFullscreen NOTIFY settingsChanged)
    Q_PROPERTY(int lastIndex READ lastIndex WRITE setLastIndex NOTIFY settingsChanged)
    Q_PROPERTY(int volume READ volume WRITE setVolume NOTIFY settingsChanged)         // 0..100
    Q_PROPERTY(int aspectMode READ aspectMode WRITE setAspectMode NOTIFY settingsChanged) // ver EmulatorView
    Q_PROPERTY(int crt READ crt WRITE setCrt NOTIFY settingsChanged)       // 0 no, 1 plano, 2 curvo
    Q_PROPERTY(bool bezel READ bezel WRITE setBezel NOTIFY settingsChanged) // marco alrededor del juego
    Q_PROPERTY(QString bezelImage READ bezelImage NOTIFY gameRunningChanged) // imagen del marco del juego actual ("" = el integrado)
    // Al salir de un juego guarda la partida y al volver a abrirlo continúa donde se quedó
    Q_PROPERTY(bool autoResume READ autoResume WRITE setAutoResume NOTIFY settingsChanged)
    // Modo atracción: tras attractSeconds sin tocar nada, el menú recorre juegos solo
    Q_PROPERTY(bool attract READ attract WRITE setAttract NOTIFY settingsChanged)
    Q_PROPERTY(int attractSeconds READ attractSeconds CONSTANT)
    Q_PROPERTY(bool rewind READ rewind WRITE setRewind NOTIFY settingsChanged)   // permite rebobinar
    Q_PROPERTY(bool rewinding READ rewinding NOTIFY rewindingChanged)
    Q_PROPERTY(bool hideBroken READ hideBroken WRITE setHideBroken NOTIFY settingsChanged)
    Q_PROPERTY(bool hideClones READ hideClones WRITE setHideClones NOTIFY settingsChanged)
    Q_PROPERTY(bool fastForward READ fastForward WRITE setFastForward NOTIFY fastForwardChanged)
    Q_PROPERTY(int stateRev READ stateRev NOTIFY statesChanged)     // cambia al guardar una partida
    Q_PROPERTY(int optionsRev READ optionsRev NOTIFY coreOptionsChanged)
    Q_PROPERTY(QString cabinetName READ cabinetName CONSTANT)
    Q_PROPERTY(QString baseDir READ baseDir CONSTANT)
public:
    AppController(const QString &baseDir, LibretroCore *core, Gamepad *pad,
                  GameListModel *games, QObject *parent = nullptr);

    bool gameRunning() const;
    bool paused() const;
    bool confirmingExit() const { return m_confirmExit; }
    QString currentTitle() const { return m_title; }
    QString baseDir() const { return m_base; }
    QString cabinetName() const;

    bool scanlines() const;  void setScanlines(bool v);
    bool smooth() const;     void setSmooth(bool v);
    bool fullscreen() const; void setFullscreen(bool v);
    int screenIndex() const; // video/screen en arcade.ini: 0 = monitor principal, 1, 2… = los demás
    int lastIndex() const;   void setLastIndex(int v);
    int volume() const;      void setVolume(int v);
    int aspectMode() const;  void setAspectMode(int v);
    int crt() const;         void setCrt(int v);
    bool bezel() const;      void setBezel(bool v);
    QString bezelImage() const { return m_bezelImage; }
    bool autoResume() const; void setAutoResume(bool v);
    bool attract() const;    void setAttract(bool v);
    int attractSeconds() const; // ui/attractSeconds en arcade.ini (60 por defecto)
    bool rewind() const;     void setRewind(bool v);
    bool rewinding() const;
    bool hideBroken() const; void setHideBroken(bool v);
    bool hideClones() const; void setHideClones(bool v);
    bool fastForward() const; void setFastForward(bool v);
    int stateRev() const { return m_stateRev; }
    int optionsRev() const { return m_optionsRev; }

    Q_INVOKABLE void launch(int row);
    Q_INVOKABLE void stopGame();
    Q_INVOKABLE void resetGame();
    Q_INVOKABLE void togglePause();
    // Salir del juego pide confirmación: requestExit() congela el juego y muestra el aviso,
    // answerExit(true) sale al menú y answerExit(false) sigue jugando.
    Q_INVOKABLE void requestExit();
    Q_INVOKABLE void answerExit(bool leave);
    Q_INVOKABLE void saveState(int slot = 0);
    Q_INVOKABLE void loadState(int slot = 0);
    // Ranuras de guardado del juego actual: [{ slot, used, image, when }]
    Q_INVOKABLE QVariantList stateSlots() const;
    // Guarda la pantalla en capturas/ (y como preview del juego si no tenía imagen)
    Q_INVOKABLE void takeScreenshot();
    // Opciones del núcleo cargado; stepCoreOption pasa al valor anterior/siguiente y lo guarda en cores/<núcleo>.ini
    Q_INVOKABLE QVariantList coreOptions() const;
    Q_INVOKABLE void stepCoreOption(const QString &key, int direction);
    Q_INVOKABLE void quit();

signals:
    void gameRunningChanged();
    void pausedChanged();
    void confirmingExitChanged();
    void fastForwardChanged();
    void rewindingChanged();
    void statesChanged();
    void coreOptionsChanged();
    void settingsChanged();
    void menuAction(const QString &action);
    void toast(const QString &text);
    void error(const QString &text);

private:
    bool ensureCore(QString coreFile);
    QString statePath(int slot) const;
    QString autoStatePath() const;

    QString m_base;
    LibretroCore *m_core;
    Gamepad *m_pad;
    GameListModel *m_games;
    QSettings m_settings;
    bool m_confirmExit = false;
    bool m_pausedBeforeConfirm = false;
    QString m_title, m_rom, m_coreIni, m_bezelImage;
    QElapsedTimer m_playClock;
    int m_stateRev = 0, m_optionsRev = 0;
};
