#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QFontDatabase>
#include <QDir>
#include <QIcon>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>

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

    // "Arcade.exe --check-rom mslug" prueba cargar ese juego sin abrir la ventana y escribe el
    // resultado en stderr: "[check] OK <rom>" o "[check] FALLA <rom>". Antes salen las líneas
    // "[core] ... is required" con los archivos que FBNeo echa en falta (aunque diga OK).
    const int checkArg = QCoreApplication::arguments().indexOf(QStringLiteral("--check-rom"));
    if (checkArg >= 0) {
        const QString only = QCoreApplication::arguments().value(checkArg + 1).toLower();
        const int last = controller.lastIndex();
        bool failed = false;
        QObject::connect(&controller, &AppController::error, &app, [&failed](const QString &) { failed = true; });
        for (int row = 0; row < games.rowCount(); ++row) {
            if (games.get(row).value(QStringLiteral("rom")).toString().toLower() != only) continue;
            failed = false;
            controller.launch(row);
            qWarning().noquote() << (failed ? "[check] FALLA" : "[check] OK")
                                 << games.get(row).value(QStringLiteral("rom")).toString();
            controller.stopGame();
        }
        controller.setLastIndex(last);
        return 0;
    }

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

    // "Arcade.exe --rom mslug" arranca directo en ese juego (útil para pruebas y accesos directos)
    const QStringList args = QCoreApplication::arguments();
    const int romArg = args.indexOf(QStringLiteral("--rom"));
    if (romArg >= 0 && romArg + 1 < args.size()) {
        const QString rom = args.at(romArg + 1).toLower();
        QObject::connect(&controller, &AppController::error, &app,
                         [](const QString &msg) { qWarning().noquote() << "[error]" << msg; });
        QTimer::singleShot(500, &app, [&, rom] {
            for (int row = 0; row < games.rowCount(); ++row) {
                if (games.get(row).value(QStringLiteral("rom")).toString().toLower() == rom) {
                    controller.launch(row);
                    return;
                }
            }
            qWarning().noquote() << "[error] ROM no encontrado:" << rom;
        });
    }

    const int ret = app.exec();
#ifdef Q_OS_WIN
    timeEndPeriod(1);
#endif
    return ret;
}
