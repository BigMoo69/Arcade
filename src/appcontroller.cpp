#include "appcontroller.h"
#include "libretrocore.h"
#include "gamepad.h"
#include "gamelistmodel.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QDateTime>
#include <cmath>
#include <QImage>
#include <QUrl>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QTextStream>
#include <QTimer>

#ifdef Q_OS_WIN
static const char *kCoreFile = "fbneo_libretro.dll";
#elif defined(Q_OS_MACOS)
static const char *kCoreFile = "fbneo_libretro.dylib";
#else
static const char *kCoreFile = "fbneo_libretro.so";
#endif

// Escribe un .wav corto (22 kHz, mono, 16 bits) con una serie de notas de onda cuadrada que se
// apagan: los "blips" del menú. Solo se crean si faltan, así el usuario puede poner los suyos.
// Con soft=true la onda es senoidal, más baja de volumen y con entrada y salida graduales: un toque
// discreto para lo que suena a cada rato (moverse por la lista).
static void writeBlip(const QString &file, std::initializer_list<std::pair<double, int>> notes, bool soft = false)
{
    if (QFileInfo::exists(file)) return;
    const int rate = 22050;
    QByteArray pcm;
    QDataStream s(&pcm, QIODevice::WriteOnly);
    s.setByteOrder(QDataStream::LittleEndian);
    for (const auto &[freq, ms] : notes) {
        const int n = rate * ms / 1000;
        for (int i = 0; i < n; ++i) {
            const double phase = std::fmod(i * freq / rate, 1.0);
            const double fade = 1.0 - double(i) / n;
            if (soft) {
                const double attack = qMin(1.0, i / (rate * 0.006)); // 6 ms de entrada: sin chasquido
                s << qint16(std::sin(phase * 6.283185307) * 1800 * attack * fade * fade);
            } else {
                s << qint16((phase < 0.5 ? 1 : -1) * 5000 * fade);
            }
        }
    }
    QByteArray wav;
    QDataStream h(&wav, QIODevice::WriteOnly);
    h.setByteOrder(QDataStream::LittleEndian);
    h.writeRawData("RIFF", 4); h << quint32(36 + pcm.size());
    h.writeRawData("WAVEfmt ", 8); h << quint32(16) << quint16(1) << quint16(1) << quint32(rate)
                                      << quint32(rate * 2) << quint16(2) << quint16(16);
    h.writeRawData("data", 4); h << quint32(pcm.size());
    QDir().mkpath(QFileInfo(file).absolutePath());
    QFile f(file);
    if (f.open(QIODevice::WriteOnly)) { f.write(wav); f.write(pcm); }
}

void AppController::ensureSounds() const
{
    const QString dir = m_base + QStringLiteral("/sounds/");
    // El primer mover.wav que se generaba (1.278 bytes, agudo y de onda cuadrada) resultó molesto:
    // si sigue ahí se sustituye por el suave. Un archivo puesto por el usuario no se toca.
    const QString move = dir + QStringLiteral("mover.wav");
    if (QFileInfo(move).size() == 1278) QFile::remove(move);
    writeBlip(move, { { 392, 55 } }, true);
    writeBlip(dir + QStringLiteral("aceptar.wav"), { { 660, 45 }, { 990, 45 }, { 1320, 70 } });
    writeBlip(dir + QStringLiteral("volver.wav"), { { 520, 45 }, { 390, 70 } });
}

QString AppController::soundUrl(const QString &name) const
{
    const QString f = m_base + QStringLiteral("/sounds/") + name + QStringLiteral(".wav");
    return QFileInfo::exists(f) ? QUrl::fromLocalFile(f).toString() : QString();
}

QString AppController::musicUrl() const
{
    for (const char *ext : { ".mp3", ".ogg", ".wav", ".flac", ".m4a" }) {
        const QString f = m_base + QStringLiteral("/sounds/musica") + QLatin1String(ext);
        if (QFileInfo::exists(f)) return QUrl::fromLocalFile(f).toString();
    }
    return {};
}

AppController::AppController(const QString &baseDir, LibretroCore *core, Gamepad *pad,
                             GameListModel *games, QObject *parent)
    : QObject(parent), m_base(baseDir), m_core(core), m_pad(pad), m_games(games),
      m_settings(baseDir + QStringLiteral("/arcade.ini"), QSettings::IniFormat),
      m_gameCfg(baseDir + QStringLiteral("/juegos.ini"), QSettings::IniFormat)
{
    connect(m_pad, &Gamepad::menuAction, this, &AppController::menuAction);
    connect(m_pad, &Gamepad::exitGameRequested, this, &AppController::requestExit);
    m_core->setVolume(volume() / 100.0);
    m_quiet = QCoreApplication::arguments().contains(QStringLiteral("--test-actions"))
              || QCoreApplication::arguments().contains(QStringLiteral("--check-rom"));
    if (!m_quiet) ensureSounds();
    m_games->setHideBroken(hideBroken());
    m_core->setRewindEnabled(rewind());
    connect(m_core, &LibretroCore::rewindingChanged, this, &AppController::rewindingChanged);
    connect(m_core, &LibretroCore::pointerUsedChanged, this, &AppController::pointerUsedChanged);
    m_games->setHideClones(hideClones());
    connect(m_core, &LibretroCore::gameStopped, this, [this] {
        if (m_gameScope) { // vuelve a los ajustes y controles generales
            m_gameScope = false;
            loadMaps();
            emit settingsChanged();
        }
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
    // (input/map = jugador 1, map2..map4 los demás; input/keys1 y keys2 = teclas de J1 y J2)
    loadMaps();
    connect(m_pad, &Gamepad::mappingChanged, this, [this] { if (!m_applyingMaps) saveMaps(); });
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
    if (m_ext) { // emulador aparte: se le pide cerrar y, si no hace caso, se le obliga
        m_extStopping = true;
        m_ext->terminate();
        QProcess *p = m_ext;
        QTimer::singleShot(2500, this, [this, p] { if (m_ext == p && p->state() != QProcess::NotRunning) p->kill(); });
        return;
    }
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

// ---- Ajustes por juego -------------------------------------------------------------------------
// Con m_gameScope los ajustes de imagen y los controles se leen y escriben en el grupo del juego
// dentro de juegos.ini ("mame/40love" se guarda como "mame|40love"); sin él, en arcade.ini.
QString AppController::gameKey(const QString &key) const
{
    return QString(m_rom).replace(u'/', u'|') + u'/' + QString(key).replace(u'/', u'_');
}

QVariant AppController::videoValue(const QString &key, const QVariant &def) const
{
    if (m_gameScope) {
        const QVariant v = m_gameCfg.value(gameKey(key));
        if (v.isValid()) return v;
    }
    return m_settings.value(key, def);
}

void AppController::setVideoValue(const QString &key, const QVariant &v)
{
    if (m_gameScope) m_gameCfg.setValue(gameKey(key), v);
    else             m_settings.setValue(key, v);
    emit settingsChanged();
}

static QString mapKeyName(int player) { return player == 0 ? QStringLiteral("input/map") : QStringLiteral("input/map%1").arg(player + 1); }

void AppController::loadMaps()
{
    auto read = [this](const QString &key) {
        QList<int> out;
        const QString text = videoValue(key, QString()).toString();
        for (const QString &v : text.split(u',', Qt::SkipEmptyParts)) out << v.toInt();
        return out;
    };
    m_applyingMaps = true; // que cargar no dispare un guardado
    for (int p = 0; p < Gamepad::MaxPlayers; ++p) {
        m_pad->resetMapping(p);
        m_pad->setMapping(read(mapKeyName(p)), p); // se ignora si no es válido
    }
    for (int p = 0; p < 2; ++p) {
        m_pad->resetDevice(Gamepad::MaxPlayers + p);
        m_pad->setKeyMapping(read(QStringLiteral("input/keys%1").arg(p + 1)), p);
    }
    m_applyingMaps = false;
}

void AppController::saveMaps()
{
    auto write = [this](const QString &key, const QList<int> &list) {
        QStringList out;
        for (int v : list) out << QString::number(v);
        if (m_gameScope) m_gameCfg.setValue(gameKey(key), out.join(u','));
        else             m_settings.setValue(key, out.join(u','));
    };
    for (int p = 0; p < Gamepad::MaxPlayers; ++p) write(mapKeyName(p), m_pad->mapping(p));
    for (int p = 0; p < 2; ++p) write(QStringLiteral("input/keys%1").arg(p + 1), m_pad->keyMapping(p));
}

void AppController::setGameConfig(bool v)
{
    if (!m_core->isRunning() || v == m_gameScope) return;
    if (v) {
        // Parte de lo que hay ahora mismo: se copia al grupo del juego y desde aquí cambia aparte
        const int crtNow = crt(), aspectNow = aspectMode();
        const bool scan = scanlines(), smoothNow = smooth(), bezelNow = bezel();
        m_gameScope = true;
        m_gameCfg.setValue(gameKey(QStringLiteral("propio")), true);
        m_gameCfg.setValue(gameKey(QStringLiteral("video/crt")), crtNow);
        m_gameCfg.setValue(gameKey(QStringLiteral("video/aspect")), aspectNow);
        m_gameCfg.setValue(gameKey(QStringLiteral("video/scanlines")), scan);
        m_gameCfg.setValue(gameKey(QStringLiteral("video/smooth")), smoothNow);
        m_gameCfg.setValue(gameKey(QStringLiteral("video/bezel")), bezelNow);
        saveMaps();
    } else {
        m_gameCfg.remove(QString(m_rom).replace(u'/', u'|'));
        m_gameScope = false;
        loadMaps();
    }
    emit settingsChanged();
}

bool AppController::scanlines() const { return videoValue(QStringLiteral("video/scanlines"), true).toBool(); }
void AppController::setScanlines(bool v) { setVideoValue(QStringLiteral("video/scanlines"), v); }
bool AppController::smooth() const { return videoValue(QStringLiteral("video/smooth"), false).toBool(); }
void AppController::setSmooth(bool v) { setVideoValue(QStringLiteral("video/smooth"), v); }
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
int AppController::aspectMode() const { return qBound(0, videoValue(QStringLiteral("video/aspect"), 0).toInt(), 2); }
void AppController::setAspectMode(int v) { setVideoValue(QStringLiteral("video/aspect"), (v % 3 + 3) % 3); }
int AppController::crt() const { return qBound(0, videoValue(QStringLiteral("video/crt"), 0).toInt(), 2); }
void AppController::setCrt(int v) { setVideoValue(QStringLiteral("video/crt"), (v % 3 + 3) % 3); }
bool AppController::bezel() const { return videoValue(QStringLiteral("video/bezel"), false).toBool(); }
void AppController::setBezel(bool v) { setVideoValue(QStringLiteral("video/bezel"), v); }
bool AppController::autoResume() const { return m_settings.value(QStringLiteral("game/autoResume"), false).toBool(); }
void AppController::setAutoResume(bool v) { m_settings.setValue(QStringLiteral("game/autoResume"), v); emit settingsChanged(); }
bool AppController::attract() const { return m_settings.value(QStringLiteral("ui/attract"), false).toBool(); }
void AppController::setAttract(bool v) { m_settings.setValue(QStringLiteral("ui/attract"), v); emit settingsChanged(); }
int AppController::attractSeconds() const { return qMax(1, m_settings.value(QStringLiteral("ui/attractSeconds"), 60).toInt()); }
bool AppController::rewind() const { return m_settings.value(QStringLiteral("game/rewind"), false).toBool(); }
void AppController::setRewind(bool v)
{
    m_settings.setValue(QStringLiteral("game/rewind"), v);
    m_core->setRewindEnabled(v);
    emit settingsChanged();
}
bool AppController::rewinding() const { return m_core->rewinding(); }
bool AppController::menuSounds() const { return !m_quiet && m_settings.value(QStringLiteral("audio/menuSounds"), true).toBool(); }
void AppController::setMenuSounds(bool v) { m_settings.setValue(QStringLiteral("audio/menuSounds"), v); emit settingsChanged(); }
bool AppController::menuMusic() const { return !m_quiet && m_settings.value(QStringLiteral("audio/menuMusic"), true).toBool(); }
void AppController::setMenuMusic(bool v) { m_settings.setValue(QStringLiteral("audio/menuMusic"), v); emit settingsChanged(); }
bool AppController::pointerUsed() const { return m_core->pointerUsed(); }
int AppController::diskCount() const { return m_core->diskCount(); }
void AppController::nextDisk()
{
    emit toast(m_core->nextDisk() ? QStringLiteral("DISCO %1 DE %2").arg(m_core->diskIndex() + 1).arg(m_core->diskCount())
                                  : QStringLiteral("No se pudo cambiar de disco"));
}
bool AppController::hideBroken() const { return m_settings.value(QStringLiteral("ui/hideBroken"), false).toBool(); }
void AppController::setHideBroken(bool v)
{
    m_settings.setValue(QStringLiteral("ui/hideBroken"), v);
    m_games->setHideBroken(v);
    emit settingsChanged();
}
int AppController::listView() const { return qBound(0, m_settings.value(QStringLiteral("ui/listView"), 0).toInt(), 1); }
void AppController::setListView(int v)
{
    m_settings.setValue(QStringLiteral("ui/listView"), ((v % 2) + 2) % 2);
    emit settingsChanged();
}
bool AppController::hideClones() const { return m_settings.value(QStringLiteral("ui/hideClones"), false).toBool(); }
void AppController::setHideClones(bool v)
{
    m_settings.setValue(QStringLiteral("ui/hideClones"), v);
    m_games->setHideClones(v);
    emit settingsChanged();
}
bool AppController::fastForward() const { return m_core->fastForward(); }
void AppController::setFastForward(bool v)
{
    if (!m_core->isRunning() || v == m_core->fastForward()) return;
    m_core->setFastForward(v);
    emit fastForwardChanged();
}
QString AppController::lastSystem() const { return m_settings.value(QStringLiteral("ui/lastSystem")).toString(); }
void AppController::setLastSystem(const QString &v)
{
    if (v == lastSystem()) return;
    m_settings.setValue(QStringLiteral("ui/lastSystem"), v);
    emit settingsChanged();
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
    if (g.isEmpty() || m_ext) return;
    if (g.value(QStringLiteral("core")).toString().startsWith(u'@')) { launchExternal(g); return; }
    if (!ensureCore(g.value(QStringLiteral("core")).toString())) return;

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

    // ¿Este juego tiene ajustes propios? Entonces rigen su imagen y sus controles
    m_gameScope = m_gameCfg.value(gameKey(QStringLiteral("propio")), false).toBool();
    if (m_gameScope) loadMaps();
    emit settingsChanged();

    // Marco: media/bezels/<rom>.png; si no hay, default-vertical.png (juegos verticales) o default.png.
    // Sin ninguna imagen la interfaz dibuja un marco sencillo con los colores del tema.
    m_bezelImage.clear();
    QStringList names{ m_rom };
    if (m_core->rotation() & 1) names << QStringLiteral("default-vertical");
    names << QStringLiteral("default");
    for (const QString &n : names) {
        for (const char *ext : { ".png", ".jpg" }) {
            const QString f = m_base + QStringLiteral("/media/bezels/") + n + QLatin1String(ext);
            if (m_bezelImage.isEmpty() && QFileInfo::exists(f)) m_bezelImage = QUrl::fromLocalFile(f).toString();
        }
    }
    m_playClock.start();

    // Continuar donde se dejó: se carga tras unos cuadros, cuando el núcleo ya terminó de arrancar
    if (autoResume() && QFileInfo::exists(autoStatePath())) {
        const QString rom = m_rom;
        QTimer::singleShot(250, this, [this, rom] {
            if (m_core->isRunning() && m_rom == rom && m_core->loadState(autoStatePath()))
                emit toast(QStringLiteral("Continúas donde lo dejaste · REINICIAR JUEGO en la pausa empieza de cero"));
        });
    }
    ++m_stateRev;
    ++m_optionsRev;
    emit statesChanged();
    emit coreOptionsChanged();
    emit gameRunningChanged();
}

// Emuladores que guardan rutas completas en su configuración (xemu.toml: BIOS, disco duro...).
// <archivo>.origen recuerda para qué carpeta se escribieron; si el emulador ya no está ahí (se copió
// el Arcade a otra carpeta o a otro PC) se cambian esas rutas por la carpeta actual.
static void relocateConfig(const QString &dir, const QString &name)
{
    const QString file = dir + u'/' + name, now = QDir::toNativeSeparators(dir);
    if (!QFileInfo::exists(file)) return;
    QFile mark(file + QStringLiteral(".origen"));
    QString old;
    if (mark.open(QIODevice::ReadOnly)) { old = QString::fromUtf8(mark.readAll()).trimmed(); mark.close(); }
    if (old.compare(now, Qt::CaseInsensitive) == 0) return;
    QFile f(file);
    if (!old.isEmpty() && f.open(QIODevice::ReadOnly)) {
        const QString before = QString::fromUtf8(f.readAll());
        f.close();
        // La misma ruta puede estar escrita con \, con \\ (JSON) o con /
        auto forms = [](const QString &p) {
            return QStringList{ p, QString(p).replace(u'\\', QLatin1String("\\\\")), QString(p).replace(u'\\', u'/') };
        };
        const QStringList from = forms(old), to = forms(now);
        QString text = before;
        for (int i = 0; i < from.size(); ++i) text.replace(from.at(i), to.at(i), Qt::CaseInsensitive);
        if (text != before && f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { f.write(text.toUtf8()); f.close(); }
    }
    if (mark.open(QIODevice::WriteOnly | QIODevice::Truncate)) mark.write(now.toUtf8());
}

// Sistemas con "programa" en sistemas.ini: el juego lo corre un emulador aparte. El Arcade espera
// a que se cierre (o lo cierra con Select+Start mantenido) y vuelve al menú.
void AppController::launchExternal(const QVariantMap &g)
{
    QString program, args;
    QStringList relocate;
    if (!m_games->externalCommand(g.value(QStringLiteral("core")).toString(), &program, &args, &relocate)) return;
    if (QDir::isRelativePath(program)) program = m_base + u'/' + program;
    if (!QFileInfo(program).isFile()) {
        emit error(QStringLiteral("No encuentro el emulador de este sistema:\n%1\n\nRevisa la línea \"programa\" "
                                  "de su bloque en cores\\sistemas.ini.").arg(QDir::toNativeSeparators(program)));
        return;
    }
    for (const QString &r : relocate) relocateConfig(QFileInfo(program).absolutePath(), r);
    const QString rom = QDir::toNativeSeparators(g.value(QStringLiteral("path")).toString());
    QStringList list = QProcess::splitCommand(args);
    // {nombre}: el del archivo sin extensión o, si el juego es una carpeta, el de esa carpeta
    // (con sus mayúsculas: el nombre interno del juego va todo en minúsculas)
    const QString id = g.value(QStringLiteral("rom")).toString().section(u'/', -1);
    QString name = QFileInfo(g.value(QStringLiteral("path")).toString()).completeBaseName();
    for (const QString &part : g.value(QStringLiteral("path")).toString().split(u'/'))
        if (part.compare(id, Qt::CaseInsensitive) == 0) name = part;
    bool used = false;
    for (QString &a : list) {
        used |= a.contains(QLatin1String("{rom}"));
        a.replace(QLatin1String("{rom}"), rom);
        a.replace(QLatin1String("{nombre}"), name);
    }
    if (!used) list << rom; // sin {rom}: el juego va al final

    m_title = g.value(QStringLiteral("title")).toString();
    m_rom = g.value(QStringLiteral("rom")).toString();
    for (int i = 0; i < m_games->rowCount(); ++i) // posición en la lista completa, para volver al mismo juego
        if (m_games->data(m_games->index(i), GameListModel::RomRole).toString() == m_rom) { setLastIndex(m_games->sourceRow(i)); break; }
    m_extName = QFileInfo(program).completeBaseName().toUpper();
    m_ext = new QProcess(this);
    m_ext->setWorkingDirectory(QFileInfo(program).absolutePath());
    m_ext->setProgram(program);
    m_ext->setArguments(list);
    connect(m_ext, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            endExternal(QStringLiteral("No se pudo abrir el emulador %1.\n\nComprueba que el programa funciona por su cuenta.").arg(m_extName));
    });
    connect(m_ext, &QProcess::finished, this, [this] { endExternal(QString()); });
    m_extStopping = false;
    m_playClock.start();
    m_pad->setMode(Gamepad::ExternalMode);
    emit externalRunningChanged();
    m_ext->start();
}

void AppController::endExternal(const QString &problem)
{
    if (!m_ext) return;
    m_ext->disconnect(this);
    m_ext->deleteLater();
    m_ext = nullptr;
    const qint64 secs = m_playClock.elapsed() / 1000;
    m_playClock.invalidate();
    m_pad->setMode(Gamepad::MenuMode);
    if (problem.isEmpty() && secs >= 10) { // igual que en los juegos normales: las pruebas cortas no cuentan
        m_games->notePlayed(m_rom, secs);
        m_games->setStatus(m_rom, 1);
    }
    emit externalRunningChanged();
    if (!problem.isEmpty()) emit error(problem);
    else if (secs < 3 && !m_extStopping)
        emit error(QStringLiteral("%1 se cerró nada más abrirse.\n\nRevisa el juego y la línea \"argumentos\" "
                                  "de su sistema en cores\\sistemas.ini.").arg(m_extName));
}

void AppController::stopGame()
{
    if (!m_core->isRunning()) return;
    // Guardado automático al salir (no en partidas de menos de 10 s, que suelen ser pruebas)
    if (autoResume() && m_playClock.isValid() && m_playClock.elapsed() >= 10000)
        m_core->saveState(autoStatePath());
    m_core->unloadGame(); // emite gameStopped
}

void AppController::resetGame()
{
    m_core->reset();
    emit toast(QStringLiteral("Reinicio"));
}

QString AppController::autoStatePath() const
{
    return m_base + QStringLiteral("/saves/%1.auto").arg(m_rom);
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
        // Los trucos valen solo hasta cerrar el Arcade: no se guardan en el .ini
        if (o.value(QStringLiteral("label")).toString().startsWith(QLatin1String("[Cheat]"))) {
            ++m_optionsRev;
            emit coreOptionsChanged();
            return;
        }

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
