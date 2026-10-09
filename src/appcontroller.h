#pragma once
#include <QObject>
#include <QSettings>

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
    Q_INVOKABLE void quit();

signals:
    void gameRunningChanged();
    void pausedChanged();
    void confirmingExitChanged();
    void settingsChanged();
    void menuAction(const QString &action);
    void toast(const QString &text);
    void error(const QString &text);

private:
    bool ensureCore();
    QString statePath(int slot) const;

    QString m_base;
    LibretroCore *m_core;
    Gamepad *m_pad;
    GameListModel *m_games;
    QSettings m_settings;
    bool m_coreLoaded = false;
    bool m_confirmExit = false;
    bool m_pausedBeforeConfirm = false;
    QString m_title, m_rom;
};
