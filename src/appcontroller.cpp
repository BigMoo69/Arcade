#include "appcontroller.h"
#include "libretrocore.h"
#include "gamepad.h"
#include "gamelistmodel.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>

#ifdef Q_OS_WIN
static const char *kCoreFile = "fbneo_libretro.dll";
#elif defined(Q_OS_MACOS)
static const char *kCoreFile = "fbneo_libretro.dylib";
#else
static const char *kCoreFile = "fbneo_libretro.so";
#endif

AppController::AppController(const QString &baseDir, LibretroCore *core, Gamepad *pad,
                             GameListModel *games, QObject *parent)
    : QObject(parent), m_base(baseDir), m_core(core), m_pad(pad), m_games(games),
      m_settings(baseDir + QStringLiteral("/arcade.ini"), QSettings::IniFormat)
{
    connect(m_pad, &Gamepad::menuAction, this, &AppController::menuAction);
    connect(m_pad, &Gamepad::exitGameRequested, this, &AppController::stopGame);
    connect(m_core, &LibretroCore::gameStopped, this, [this] {
        m_pad->setMode(Gamepad::MenuMode);
        emit gameRunningChanged();
    });
    connect(m_core, &LibretroCore::message, this, &AppController::toast);
    connect(m_pad, &Gamepad::pauseRequested, this, &AppController::togglePause);

    // Mapeo de botones: input/map en arcade.ini, un número de botón físico por acción
    QList<int> map;
    for (const QString &v : m_settings.value(QStringLiteral("input/map")).toString().split(u',', Qt::SkipEmptyParts))
        map << v.toInt();
    m_pad->setMapping(map); // se ignora si no es válido
    connect(m_pad, &Gamepad::mappingChanged, this, [this] {
        QStringList out;
        for (int v : m_pad->mapping()) out << QString::number(v);
        m_settings.setValue(QStringLiteral("input/map"), out.join(u','));
    });
    connect(m_core, &LibretroCore::pausedChanged, this, [this] {
        m_pad->setPaused(m_core->isPaused());
        emit pausedChanged();
    });
}

QString AppController::cabinetName() const
{
    return m_settings.value(QStringLiteral("ui/title"), QStringLiteral("ARCADE MULTIJUEGOS")).toString();
}

bool AppController::gameRunning() const { return m_core->isRunning(); }
bool AppController::paused() const { return m_core->isPaused(); }
void AppController::togglePause() { m_core->setPaused(!m_core->isPaused()); }

bool AppController::scanlines() const { return m_settings.value(QStringLiteral("video/scanlines"), true).toBool(); }
void AppController::setScanlines(bool v) { m_settings.setValue(QStringLiteral("video/scanlines"), v); emit settingsChanged(); }
bool AppController::smooth() const { return m_settings.value(QStringLiteral("video/smooth"), false).toBool(); }
void AppController::setSmooth(bool v) { m_settings.setValue(QStringLiteral("video/smooth"), v); emit settingsChanged(); }
bool AppController::fullscreen() const { return m_settings.value(QStringLiteral("video/fullscreen"), true).toBool(); }
void AppController::setFullscreen(bool v) { m_settings.setValue(QStringLiteral("video/fullscreen"), v); emit settingsChanged(); }
int AppController::screenIndex() const { return m_settings.value(QStringLiteral("video/screen"), 0).toInt(); }
int AppController::lastIndex() const { return m_settings.value(QStringLiteral("ui/lastIndex"), 0).toInt(); }
void AppController::setLastIndex(int v)
{
    if (v == lastIndex()) return;
    m_settings.setValue(QStringLiteral("ui/lastIndex"), v);
    emit settingsChanged();
}

bool AppController::ensureCore()
{
    if (m_coreLoaded) return true;

    // Opciones del núcleo opcionales: cores/fbneo.ini con líneas "clave = valor"
    QHash<QByteArray, QByteArray> overrides;
    QFile ini(m_base + QStringLiteral("/cores/fbneo.ini"));
    if (ini.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!ini.atEnd()) {
            const QByteArray line = ini.readLine().trimmed();
            if (line.isEmpty() || line.startsWith('#') || line.startsWith(';')) continue;
            const int eq = line.indexOf('=');
            if (eq <= 0) continue;
            QByteArray val = line.mid(eq + 1).trimmed();
            if (val.size() >= 2 && val.startsWith('"') && val.endsWith('"')) val = val.mid(1, val.size() - 2);
            overrides.insert(line.left(eq).trimmed(), val);
        }
    }
    m_core->setOptionOverrides(overrides);

    QDir().mkpath(m_base + QStringLiteral("/saves"));
    QDir().mkpath(m_base + QStringLiteral("/system"));
    const QString corePath = m_base + QStringLiteral("/cores/") + QString::fromLatin1(kCoreFile);
    if (!QFileInfo::exists(corePath)) {
        emit error(QStringLiteral("Falta el emulador.\n\nCopia %1 en la carpeta:\n%2")
                       .arg(QString::fromLatin1(kCoreFile), QDir::toNativeSeparators(m_base + QStringLiteral("/cores"))));
        return false;
    }
    if (!m_core->loadCore(corePath, m_base + QStringLiteral("/system"), m_base + QStringLiteral("/saves"))) {
        emit error(m_core->lastError());
        return false;
    }
    m_coreLoaded = true;
    return true;
}

void AppController::launch(int row)
{
    const QVariantMap g = m_games->get(row);
    if (g.isEmpty() || !ensureCore()) return;

    m_title = g.value(QStringLiteral("title")).toString();
    m_rom = g.value(QStringLiteral("rom")).toString();
    setLastIndex(row);

    if (!m_core->loadGame(g.value(QStringLiteral("path")).toString())) {
        emit error(m_core->lastError());
        return;
    }

    // FBNeo dice "cargado" aunque el ROM no le sirva y enseña su pantalla gris de error:
    // mejor avisar aquí, marcar el juego con X y volver al menú.
    const QStringList problems = m_core->loadProblems();
    if (m_core->loadLooksBad()) {
        m_core->unloadGame();
        m_games->setStatus(m_rom, -1);
        QStringList files;
        static const QRegularExpression re(QStringLiteral("with name (\\S+)"));
        for (const QString &p : problems) {
            const auto mt = re.match(p);
            if (mt.hasMatch()) files << mt.captured(1);
        }
        emit error(files.isEmpty()
                       ? QStringLiteral("\"%1\" no es un juego que FinalBurn Neo reconozca.\n\nEl archivo %2.zip tiene un "
                                        "nombre o versión que el emulador no conoce.").arg(m_title, m_rom)
                       : QStringLiteral("\"%1\" no es compatible con esta versión de FinalBurn Neo.\n\nFaltan %2 archivo(s) "
                                        "en %3.zip:\n%4")
                             .arg(m_title).arg(files.size()).arg(m_rom, files.mid(0, 8).join(QStringLiteral("  "))));
        return;
    }
    m_games->setStatus(m_rom, 1);
    m_pad->setMode(Gamepad::GameMode);
    emit gameRunningChanged();
}

void AppController::stopGame()
{
    if (m_core->isRunning()) m_core->unloadGame(); // emite gameStopped
}

void AppController::resetGame()
{
    m_core->reset();
    emit toast(QStringLiteral("Reinicio"));
}

QString AppController::statePath(int slot) const
{
    return m_base + QStringLiteral("/saves/%1.state%2").arg(m_rom).arg(slot);
}

void AppController::saveState(int slot)
{
    emit toast(m_core->saveState(statePath(slot)) ? QStringLiteral("Partida guardada")
                                                  : QStringLiteral("No se pudo guardar"));
}

void AppController::loadState(int slot)
{
    emit toast(m_core->loadState(statePath(slot)) ? QStringLiteral("Partida cargada")
                                                  : QStringLiteral("No hay partida guardada"));
}

void AppController::quit()
{
    stopGame();
    QCoreApplication::quit();
}
