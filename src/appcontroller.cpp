#include "appcontroller.h"
#include "libretrocore.h"
#include "gamepad.h"
#include "gamelistmodel.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QImage>
#include <QUrl>
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
    connect(m_pad, &Gamepad::exitGameRequested, this, &AppController::requestExit);
    m_core->setVolume(volume() / 100.0);
    m_games->setHideBroken(hideBroken());
    connect(m_core, &LibretroCore::gameStopped, this, [this] {
        // Cuenta como partida si duró al menos 10 s (las pruebas de carga no cuentan)
        if (m_playClock.isValid() && m_playClock.elapsed() >= 10000)
            m_games->notePlayed(m_rom, m_playClock.elapsed() / 1000);
        m_playClock.invalidate();
        emit fastForwardChanged();
        if (m_confirmExit) { m_confirmExit = false; emit confirmingExitChanged(); }
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
void AppController::togglePause()
{
    if (m_confirmExit || !m_core->isRunning()) return;
    if (!m_core->isPaused()) {
        m_core->setPaused(true);
        m_pad->setMode(Gamepad::MenuMode); // la pausa tiene menú: se navega como el resto de menús
    } else {
        m_pad->setMode(Gamepad::GameMode);
        m_core->setPaused(false);
    }
}

void AppController::requestExit()
{
    if (!m_core->isRunning() || m_confirmExit) return;
    m_confirmExit = true;
    m_pausedBeforeConfirm = m_core->isPaused();
    m_core->setPaused(true);
    m_pad->setMode(Gamepad::MenuMode); // el aviso se maneja como un menú (mando y teclado)
    emit confirmingExitChanged();
}

void AppController::answerExit(bool leave)
{
    if (!m_confirmExit) return;
    if (leave) { stopGame(); return; } // gameStopped limpia el aviso
    m_confirmExit = false;
    if (!m_pausedBeforeConfirm) { // si ya estaba en pausa, se queda en el menú de pausa
        m_pad->setMode(Gamepad::GameMode);
        m_core->setPaused(false);
    }
    emit confirmingExitChanged();
}

bool AppController::scanlines() const { return m_settings.value(QStringLiteral("video/scanlines"), true).toBool(); }
void AppController::setScanlines(bool v) { m_settings.setValue(QStringLiteral("video/scanlines"), v); emit settingsChanged(); }
bool AppController::smooth() const { return m_settings.value(QStringLiteral("video/smooth"), false).toBool(); }
void AppController::setSmooth(bool v) { m_settings.setValue(QStringLiteral("video/smooth"), v); emit settingsChanged(); }
bool AppController::fullscreen() const { return m_settings.value(QStringLiteral("video/fullscreen"), true).toBool(); }
void AppController::setFullscreen(bool v) { m_settings.setValue(QStringLiteral("video/fullscreen"), v); emit settingsChanged(); }
int AppController::screenIndex() const { return m_settings.value(QStringLiteral("video/screen"), 0).toInt(); }
int AppController::volume() const { return qBound(0, m_settings.value(QStringLiteral("audio/volume"), 100).toInt(), 100); }
void AppController::setVolume(int v)
{
    v = qBound(0, v, 100);
    if (v == volume()) return;
    m_settings.setValue(QStringLiteral("audio/volume"), v);
    m_core->setVolume(v / 100.0);
    emit settingsChanged();
}
int AppController::aspectMode() const { return qBound(0, m_settings.value(QStringLiteral("video/aspect"), 0).toInt(), 2); }
void AppController::setAspectMode(int v) { m_settings.setValue(QStringLiteral("video/aspect"), (v % 3 + 3) % 3); emit settingsChanged(); }
bool AppController::hideBroken() const { return m_settings.value(QStringLiteral("ui/hideBroken"), false).toBool(); }
void AppController::setHideBroken(bool v)
{
    m_settings.setValue(QStringLiteral("ui/hideBroken"), v);
    m_games->setHideBroken(v);
    emit settingsChanged();
}
bool AppController::fastForward() const { return m_core->fastForward(); }
void AppController::setFastForward(bool v)
{
    if (!m_core->isRunning() || v == m_core->fastForward()) return;
    m_core->setFastForward(v);
    emit fastForwardChanged();
}
int AppController::lastIndex() const { return m_settings.value(QStringLiteral("ui/lastIndex"), 0).toInt(); }
void AppController::setLastIndex(int v)
{
    if (v == lastIndex()) return;
    m_settings.setValue(QStringLiteral("ui/lastIndex"), v);
    emit settingsChanged();
}

// Deja cargado el núcleo que corre ese juego (coreFile vacío = FinalBurn Neo). Solo hay uno en
// memoria: al cambiar de sistema se descarga el anterior.
bool AppController::ensureCore(QString coreFile)
{
    if (coreFile.isEmpty()) coreFile = QString::fromLatin1(kCoreFile);
#ifndef Q_OS_WIN
    // sistemas.ini nombra los núcleos como en Windows; en otros sistemas cambia la extensión
    if (coreFile.endsWith(QLatin1String(".dll"))) coreFile = coreFile.chopped(4) + QFileInfo(QString::fromLatin1(kCoreFile)).suffix().prepend(u'.');
#endif
    const QString corePath = m_base + QStringLiteral("/cores/") + coreFile;
    if (m_core->corePath() == corePath) return true;

    // Opciones del núcleo opcionales: cores/<nombre>.ini (fbneo.ini, snes9x.ini…) con líneas "clave = valor"
    QHash<QByteArray, QByteArray> overrides;
    QString iniName = QFileInfo(coreFile).completeBaseName();
    if (iniName.endsWith(QLatin1String("_libretro"))) iniName.chop(9);
    m_coreIni = m_base + QStringLiteral("/cores/") + iniName + QStringLiteral(".ini");
    QFile ini(m_coreIni);
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
    if (!QFileInfo::exists(corePath)) {
        emit error(QStringLiteral("Falta el emulador de este sistema.\n\nCopia %1 en la carpeta:\n%2")
                       .arg(coreFile, QDir::toNativeSeparators(m_base + QStringLiteral("/cores"))));
        return false;
    }
    if (!m_core->loadCore(corePath, m_base + QStringLiteral("/system"), m_base + QStringLiteral("/saves"))) {
        emit error(m_core->lastError());
        return false;
    }
    return true;
}

void AppController::launch(int row)
{
    const QVariantMap g = m_games->get(row);
    if (g.isEmpty() || !ensureCore(g.value(QStringLiteral("core")).toString())) return;

    m_title = g.value(QStringLiteral("title")).toString();
    m_rom = g.value(QStringLiteral("rom")).toString();
    setLastIndex(m_games->sourceRow(row)); // posición en la lista completa, no en la filtrada

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
    m_playClock.start();
    ++m_stateRev;
    ++m_optionsRev;
    emit statesChanged();
    emit coreOptionsChanged();
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
    const bool ok = m_core->saveState(statePath(slot));
    if (ok) { // miniatura para reconocer la partida al cargarla
        const QImage shot = m_core->screenshot();
        if (!shot.isNull()) shot.save(statePath(slot) + QStringLiteral(".png"));
        ++m_stateRev;
        emit statesChanged();
    }
    emit toast(ok ? QStringLiteral("Partida guardada en la ranura %1").arg(slot + 1)
                  : QStringLiteral("No se pudo guardar"));
}

void AppController::loadState(int slot)
{
    emit toast(m_core->loadState(statePath(slot)) ? QStringLiteral("Partida %1 cargada").arg(slot + 1)
                                                  : QStringLiteral("No hay partida en la ranura %1").arg(slot + 1));
}

QVariantList AppController::stateSlots() const
{
    QVariantList out;
    for (int slot = 0; slot < 6; ++slot) {
        const QFileInfo fi(statePath(slot));
        QVariantMap m{ { QStringLiteral("slot"), slot }, { QStringLiteral("used"), fi.exists() } };
        if (fi.exists()) {
            m.insert(QStringLiteral("when"), fi.lastModified().toString(QStringLiteral("dd/MM/yyyy  HH:mm")));
            const QString png = fi.absoluteFilePath() + QStringLiteral(".png");
            if (QFileInfo::exists(png)) { // el "?…" obliga a recargar la imagen si se sobrescribe la ranura
                QUrl url = QUrl::fromLocalFile(png);
                url.setQuery(QString::number(fi.lastModified().toSecsSinceEpoch()));
                m.insert(QStringLiteral("image"), url.toString());
            }
        }
        out << m;
    }
    return out;
}

void AppController::takeScreenshot()
{
    const QImage shot = m_core->screenshot();
    if (shot.isNull()) { emit toast(QStringLiteral("No hay imagen que capturar")); return; }
    const QString name = QString(m_rom).replace(u'/', u'_');
    const QString file = m_base + QStringLiteral("/capturas/%1-%2.png")
                             .arg(name, QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    QDir().mkpath(QFileInfo(file).absolutePath());
    if (!shot.save(file)) { emit toast(QStringLiteral("No se pudo guardar la captura")); return; }

    // Si el juego no tenía imagen en el menú, esta captura pasa a ser su preview
    bool hasImage = false;
    for (int row = 0; row < m_games->rowCount() && !hasImage; ++row) {
        const QVariantMap g = m_games->get(row);
        if (g.value(QStringLiteral("rom")).toString() == m_rom)
            hasImage = !g.value(QStringLiteral("image")).toString().isEmpty();
    }
    const QString snap = m_base + QStringLiteral("/media/snaps/") + m_rom + QStringLiteral(".png");
    if (!hasImage && !QFileInfo::exists(snap)) {
        QDir().mkpath(QFileInfo(snap).absolutePath());
        shot.save(snap);
        emit toast(QStringLiteral("Captura guardada y puesta como imagen del juego"));
    } else {
        emit toast(QStringLiteral("Captura guardada en la carpeta capturas"));
    }
}

QVariantList AppController::coreOptions() const { return m_core->options(); }

void AppController::stepCoreOption(const QString &key, int direction)
{
    for (const QVariant &v : m_core->options()) {
        const QVariantMap o = v.toMap();
        if (o.value(QStringLiteral("key")).toString() != key) continue;
        const QStringList values = o.value(QStringLiteral("values")).toStringList();
        const int n = int(values.size());
        const int cur = int(values.indexOf(o.value(QStringLiteral("value")).toString()));
        const QString next = values.at(((cur < 0 ? 0 : cur + (direction < 0 ? -1 : 1)) % n + n) % n);
        if (!m_core->setOption(key.toUtf8(), next.toUtf8())) return;

        // Guarda el valor en cores/<núcleo>.ini: cambia la línea de esa clave o la añade al final
        QStringList lines;
        QFile in(m_coreIni);
        if (in.open(QIODevice::ReadOnly | QIODevice::Text))
            lines = QString::fromUtf8(in.readAll()).split(u'\n');
        in.close();
        while (!lines.isEmpty() && lines.last().trimmed().isEmpty()) lines.removeLast();
        const QString entry = key + QStringLiteral(" = ") + next;
        bool found = false;
        for (QString &line : lines) {
            const QString t = line.trimmed();
            if (t.startsWith(u'#') || t.startsWith(u';')) continue;
            if (t.section(u'=', 0, 0).trimmed() == key) { line = entry; found = true; }
        }
        if (!found) lines << entry;
        QFile out(m_coreIni);
        if (out.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
            out.write((lines.join(u'\n') + u'\n').toUtf8());
        ++m_optionsRev;
        emit coreOptionsChanged();
        return;
    }
}

void AppController::quit()
{
    stopGame();
    QCoreApplication::quit();
}
