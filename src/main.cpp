#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QFontDatabase>
#include <QDir>
#include <QIcon>
#include <QQuickWindow>
#include <QScreen>

#include "appcontroller.h"
#include "emulatorview.h"
#include "gamelistmodel.h"
#include "gamepad.h"
#include "libretrocore.h"

#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <timeapi.h>
#endif

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("Arcade"));

#ifdef Q_OS_WIN
    timeBeginPeriod(1); // temporizador de 1 ms para un ritmo de cuadros estable
#endif

    // Todo vive junto al ejecutable (portátil). ARCADE_DIR permite usar otra carpeta.
    QString base = qEnvironmentVariable("ARCADE_DIR");
    if (base.isEmpty()) base = QCoreApplication::applicationDirPath();
    for (const char *d : { "roms", "media/videos", "media/snaps", "media/marquees", "cores", "dats", "fonts" })
        QDir(base).mkpath(QString::fromLatin1(d));

    // Fuente personalizada opcional (ej. "Press Start 2P") en fonts/
    QString fontFamily = QStringLiteral("Consolas");
    const auto fonts = QDir(base + QStringLiteral("/fonts"))
                           .entryInfoList({ QStringLiteral("*.ttf"), QStringLiteral("*.otf") }, QDir::Files);
    for (const QFileInfo &fi : fonts) {
        const int id = QFontDatabase::addApplicationFont(fi.absoluteFilePath());
        const QStringList fam = QFontDatabase::applicationFontFamilies(id);
        if (!fam.isEmpty()) { fontFamily = fam.first(); break; }
    }

    Gamepad pad;
    LibretroCore core(&pad);
    GameListModel games;
    games.setBaseDir(base);
    games.rescan();
    AppController controller(base, &core, &pad, &games);

    EmulatorView::setCore(&core);
    qmlRegisterType<EmulatorView>("ArcadeNative", 1, 0, "EmulatorView");

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("App"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("Games"), &games);
    engine.rootContext()->setContextProperty(QStringLiteral("Pad"), &pad);
    engine.rootContext()->setContextProperty(QStringLiteral("arcadeFont"), fontFamily);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));

    // La ventana se coloca aquí y no en QML: con monitores de distinta escala (ej. 100 % y 150 %)
    // hay que fijar el monitor y su geometría antes de mostrarla, o sale con el tamaño equivocado.
    auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    if (!win) return 1;
    int shownFullscreen = -1;
    auto applyWindowMode = [&] {
        const bool fs = controller.fullscreen();
        if (int(fs) == shownFullscreen) return;
        shownFullscreen = fs;
        const auto screens = QGuiApplication::screens(); // el primero es el principal
        QScreen *scr = screens.value(controller.screenIndex(), QGuiApplication::primaryScreen());
        win->setScreen(scr);
        if (fs) {
            win->setGeometry(scr->geometry());
            win->showFullScreen();
        } else {
            const QRect avail = scr->availableGeometry();
            const QSize size = QSize(1280, 720).boundedTo(avail.size() * 0.9);
            QRect geo(QPoint(0, 0), size);
            geo.moveCenter(avail.center());
            win->setGeometry(geo);
            win->showNormal();
        }
    };
    QObject::connect(&controller, &AppController::settingsChanged, win, applyWindowMode);
    applyWindowMode();

    const int ret = app.exec();
#ifdef Q_OS_WIN
    timeEndPeriod(1);
#endif
    return ret;
}
