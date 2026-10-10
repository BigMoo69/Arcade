#include "libretrocore.h"
#include "gamepad.h"

#include <QAudioSink>
#include <QAudioFormat>
#include <QMediaDevices>
#include <QAudioDevice>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include <QDirIterator>
#include <QProcess>
#include <QTransform>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLFramebufferObject>
#include <QOffscreenSurface>
#include <QSurfaceFormat>
#include <vector>
#include <cstdarg>
#include <cstring>
#include <cstdio>

LibretroCore *LibretroCore::s_self = nullptr;

LibretroCore::LibretroCore(Gamepad *pad, QObject *parent)
    : QObject(parent), m_pad(pad)
{
    s_self = this;
    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.setInterval(1);
    connect(&m_timer, &QTimer::timeout, this, &LibretroCore::tick);
}

LibretroCore::~LibretroCore()
{
    unloadCore();
    if (s_self == this) s_self = nullptr;
}

QString LibretroCore::describe() const
{
    if (!m_coreInited) return QStringLiteral("(sin núcleo)");
    retro_system_info sys{};
    m_api.get_system_info(&sys);
    return QStringLiteral("%1 %2 | extensiones: %3 | %4%5")
        .arg(QString::fromUtf8(sys.library_name ? sys.library_name : "?"),
             QString::fromUtf8(sys.library_version ? sys.library_version : "?"),
             QString::fromUtf8(sys.valid_extensions ? sys.valid_extensions : "-"),
             sys.need_fullpath ? QStringLiteral("lee el archivo él mismo") : QStringLiteral("recibe el ROM en memoria"),
             sys.block_extract ? QStringLiteral(", abre los zip él mismo") : QString());
}

void LibretroCore::unloadCore()
{
    unloadGame();
    if (m_coreInited && m_api.deinit)
        m_api.deinit();
    if (m_lib.isLoaded()) m_lib.unload();
    m_coreInited = false;
    m_api = Api{};
    m_vars.clear();
    m_optDefs.clear();
    m_corePath.clear();
    m_disk = {};
}

template <typename T>
static bool resolveSym(QLibrary &lib, T &out, const char *name)
{
    out = reinterpret_cast<T>(lib.resolve(name));
    return out != nullptr;
}

bool LibretroCore::loadCore(const QString &corePath, const QString &systemDir, const QString &saveDir)
{
    if (m_coreInited && corePath == m_corePath) return true; // ya es el núcleo activo
    unloadCore();                                            // solo hay un núcleo cargado a la vez

    m_systemDir = QDir::toNativeSeparators(systemDir).toUtf8();
    m_saveDir   = QDir::toNativeSeparators(saveDir).toUtf8();

    m_lib.setFileName(corePath);
    if (!m_lib.load()) {
        m_error = QStringLiteral("No se pudo cargar el núcleo:\n%1\n%2").arg(corePath, m_lib.errorString());
        return false;
    }

    bool ok = true;
    ok &= resolveSym(m_lib, m_api.init, "retro_init");
    ok &= resolveSym(m_lib, m_api.deinit, "retro_deinit");
    ok &= resolveSym(m_lib, m_api.api_version, "retro_api_version");
    ok &= resolveSym(m_lib, m_api.get_system_info, "retro_get_system_info");
    ok &= resolveSym(m_lib, m_api.get_system_av_info, "retro_get_system_av_info");
    ok &= resolveSym(m_lib, m_api.set_environment, "retro_set_environment");
    ok &= resolveSym(m_lib, m_api.set_video_refresh, "retro_set_video_refresh");
    ok &= resolveSym(m_lib, m_api.set_audio_sample, "retro_set_audio_sample");
    ok &= resolveSym(m_lib, m_api.set_audio_sample_batch, "retro_set_audio_sample_batch");
    ok &= resolveSym(m_lib, m_api.set_input_poll, "retro_set_input_poll");
    ok &= resolveSym(m_lib, m_api.set_input_state, "retro_set_input_state");
    ok &= resolveSym(m_lib, m_api.set_controller_port_device, "retro_set_controller_port_device");
    ok &= resolveSym(m_lib, m_api.reset, "retro_reset");
    ok &= resolveSym(m_lib, m_api.run, "retro_run");
    ok &= resolveSym(m_lib, m_api.serialize_size, "retro_serialize_size");
    ok &= resolveSym(m_lib, m_api.serialize, "retro_serialize");
    ok &= resolveSym(m_lib, m_api.unserialize, "retro_unserialize");
    ok &= resolveSym(m_lib, m_api.load_game, "retro_load_game");
    ok &= resolveSym(m_lib, m_api.unload_game, "retro_unload_game");
    if (!ok) {
        m_error = QStringLiteral("El archivo no es un núcleo libretro válido: %1").arg(corePath);
        m_lib.unload();
        m_api = Api{};
        return false;
    }
    // Opcionales: memoria de guardado (pilas de cartucho en consolas)
    resolveSym(m_lib, m_api.get_memory_data, "retro_get_memory_data");
    resolveSym(m_lib, m_api.get_memory_size, "retro_get_memory_size");
    m_corePath = corePath;

    m_api.set_environment(&LibretroCore::cbEnvironment);
    m_api.init();
    m_api.set_video_refresh(&LibretroCore::cbVideo);
    m_api.set_audio_sample(&LibretroCore::cbAudioSample);
    m_api.set_audio_sample_batch(&LibretroCore::cbAudioBatch);
    m_api.set_input_poll(&LibretroCore::cbInputPoll);
    m_api.set_input_state(&LibretroCore::cbInputState);
    m_coreInited = true;
    return true;
}

bool LibretroCore::loadGame(const QString &romPath)
{
    if (!m_coreInited) { m_error = QStringLiteral("Núcleo no cargado"); return false; }
    unloadGame();

    retro_system_info sys{};
    m_api.get_system_info(&sys);

    // Los núcleos arcade abren el .zip ellos mismos (block_extract). Los de consola esperan el ROM
    // suelto: si viene en .zip se extrae a una carpeta temporal y se carga el archivo de dentro.
    QString contentPath = romPath;
    if (!sys.block_extract && romPath.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive)) {
        contentPath = extractFromZip(romPath, QString::fromLatin1(sys.valid_extensions ? sys.valid_extensions : ""));
        if (contentPath.isEmpty()) return false; // m_error ya explica por qué
    }
    m_sramPath = QString::fromUtf8(m_saveDir) + u'/' + QFileInfo(romPath).completeBaseName() + QStringLiteral(".srm");

    m_romPathUtf8 = QDir::toNativeSeparators(contentPath).toUtf8();
    retro_game_info info{};
    info.path = m_romPathUtf8.constData();

    if (!sys.need_fullpath) {
        QFile f(contentPath);
        if (!f.open(QIODevice::ReadOnly)) {
            m_error = QStringLiteral("No se pudo abrir %1").arg(contentPath);
            return false;
        }
        m_romData = f.readAll();
        info.data = m_romData.constData();
        info.size = size_t(m_romData.size());
    }

    m_rotation = 0;
    m_pixFmt = RETRO_PIXEL_FORMAT_0RGB1555;
    m_frame = QImage();
    m_hw = {}; // el núcleo lo vuelve a pedir en load_game si dibuja con OpenGL

    m_loadProblems.clear();
    m_driverStarted = false;
    m_isFbneo = QByteArray(sys.library_name ? sys.library_name : "").contains("FinalBurn");
    m_loading = true;
    const bool loaded = m_api.load_game(&info);
    m_loading = false;
    if (!loaded) {
        m_error = m_isFbneo
            ? QStringLiteral("No se pudo iniciar \"%1\".\n\nRevisa que el ROM sea de la versión correcta "
                             "de FinalBurn Neo y que neogeo.zip esté en la carpeta roms si es de NeoGeo.")
                  .arg(QFileInfo(romPath).fileName())
            : QStringLiteral("El emulador %1 no pudo iniciar \"%2\".\n\nRevisa que el archivo sea del sistema correcto "
                             "y, si ese sistema necesita BIOS, que esté en la carpeta system.")
                  .arg(QString::fromUtf8(sys.library_name ? sys.library_name : "?"), QFileInfo(romPath).fileName());
        m_romData.clear();
        return false;
    }
    loadSram();

    for (unsigned p = 0; p < 4; ++p)
        m_api.set_controller_port_device(p, RETRO_DEVICE_JOYPAD);

    m_api.get_system_av_info(&m_av);
    if (m_hw.context_reset && !createGl()) {
        m_api.unload_game();
        m_romData.clear();
        m_error = QStringLiteral("Este emulador dibuja con OpenGL y no se pudo crear el contexto gráfico.\n\n"
                                 "Actualiza el controlador de la tarjeta de video.");
        return false;
    }
    m_gameLoaded = true;

    startAudio(m_av.timing.sample_rate > 0 ? m_av.timing.sample_rate : 48000.0);

    m_clock.restart();
    m_nextFrameMs = 0;
    m_timer.start();
    return true;
}

void LibretroCore::unloadGame()
{
    if (!m_gameLoaded) return;
    m_timer.stop();
    stopAudio();
    saveSram();
    if (m_hwActive) { // el núcleo libera sus recursos gráficos con el contexto todavía activo
        glCurrent();
        if (m_hw.context_destroy) m_hw.context_destroy();
    }
    m_api.unload_game();
    destroyGl();
    m_gameLoaded = false;
    m_fast = false;
    m_rewind.clear();
    if (m_rewinding) { m_rewinding = false; emit rewindingChanged(); }
    if (m_paused) { m_paused = false; emit pausedChanged(); }
    m_romData.clear();
    m_frame = QImage();
    emit gameStopped();
}

// Extrae del .zip el primer archivo con una extensión que el núcleo acepte ("sfc|smc|…").
// Usa tar.exe, que viene con Windows 10/11 y también lee zip (bsdtar en Linux/macOS).
QString LibretroCore::extractFromZip(const QString &zipPath, const QString &validExts)
{
    const QString tmp = QString::fromUtf8(m_saveDir) + QStringLiteral("/tmp");
    QDir(tmp).removeRecursively();
    QDir().mkpath(tmp);
    QProcess tar;
    tar.start(QStringLiteral("tar"), { QStringLiteral("-xf"), QDir::toNativeSeparators(zipPath),
                                       QStringLiteral("-C"), QDir::toNativeSeparators(tmp) });
    if (!tar.waitForFinished(60000) || tar.exitCode() != 0) {
        m_error = QStringLiteral("No se pudo descomprimir %1.\n\nPrueba a descomprimir el ROM a mano y dejar el "
                                 "archivo suelto en la carpeta del sistema.").arg(QFileInfo(zipPath).fileName());
        return {};
    }
    const QStringList exts = validExts.toLower().split(u'|', Qt::SkipEmptyParts);
    QDirIterator it(tmp, QDir::Files, QDirIterator::Subdirectories);
    QString first;
    while (it.hasNext()) {
        const QString f = it.next();
        if (first.isEmpty()) first = f;
        if (exts.contains(QFileInfo(f).suffix().toLower())) return f;
    }
    if (first.isEmpty())
        m_error = QStringLiteral("El archivo %1 está vacío.").arg(QFileInfo(zipPath).fileName());
    return first; // sin extensión reconocida: que el núcleo decida
}

// Memoria de guardado del cartucho (.srm junto a los savestates): se lee al cargar y se escribe al salir
void LibretroCore::loadSram()
{
    if (m_isFbneo || !m_api.get_memory_data || !m_api.get_memory_size) return; // FBNeo guarda su NVRAM solo
    const size_t size = m_api.get_memory_size(RETRO_MEMORY_SAVE_RAM);
    void *mem = m_api.get_memory_data(RETRO_MEMORY_SAVE_RAM);
    QFile f(m_sramPath);
    if (size == 0 || !mem || !f.open(QIODevice::ReadOnly)) return;
    const QByteArray data = f.readAll();
    memcpy(mem, data.constData(), qMin(size, size_t(data.size())));
}

void LibretroCore::saveSram()
{
    if (!m_gameLoaded || m_isFbneo || !m_api.get_memory_data || !m_api.get_memory_size) return;
    const size_t size = m_api.get_memory_size(RETRO_MEMORY_SAVE_RAM);
    const void *mem = m_api.get_memory_data(RETRO_MEMORY_SAVE_RAM);
    if (size == 0 || !mem) return;
    const QByteArray data(static_cast<const char *>(mem), qsizetype(size));
    if (data.count('\0') == data.size() && !QFileInfo::exists(m_sramPath)) return; // nada guardado aún
    QDir().mkpath(QFileInfo(m_sramPath).absolutePath());
    QFile f(m_sramPath);
    if (f.open(QIODevice::WriteOnly)) f.write(data);
}

void LibretroCore::setPaused(bool paused)
{
    if (!m_gameLoaded || paused == m_paused) return;
    m_paused = paused;
    if (m_sink) { if (paused) m_sink->suspend(); else m_sink->resume(); }
    if (!paused) m_nextFrameMs = double(m_clock.nsecsElapsed()) / 1e6; // no recuperes el tiempo en pausa
    emit pausedChanged();
}

void LibretroCore::setFastForward(bool on)
{
    m_fast = on && m_gameLoaded;
    m_nextFrameMs = double(m_clock.nsecsElapsed()) / 1e6;
}

int LibretroCore::diskCount() const
{
    return m_gameLoaded && m_disk.get_num_images ? int(m_disk.get_num_images()) : 0;
}

int LibretroCore::diskIndex() const
{
    return m_gameLoaded && m_disk.get_image_index ? int(m_disk.get_image_index()) : 0;
}

// Como en una consola: abrir la tapa, poner el disco siguiente y cerrarla
bool LibretroCore::nextDisk()
{
    const int n = diskCount();
    if (n < 2 || !m_disk.set_eject_state || !m_disk.set_image_index) return false;
    const unsigned next = unsigned(diskIndex() + 1) % unsigned(n);
    m_disk.set_eject_state(true);
    const bool ok = m_disk.set_image_index(next);
    m_disk.set_eject_state(false);
    m_rewind.clear(); // los estados guardados eran del otro disco
    return ok;
}

void LibretroCore::setRewindEnabled(bool on)
{
    m_rewindOn = on;
    if (!on) m_rewind.clear();
}

// Un cuadro de emulación. Normal: corre y cada 6 cuadros guarda un estado (unos 60 s de historia,
// con tope de 256 MB). Rebobinando: cada 2 cuadros carga el estado anterior y lo dibuja (~3x hacia atrás).
// ---------------------------------------------------------------- OpenGL para núcleos 3D

bool LibretroCore::createGl()
{
    QSurfaceFormat fmt;
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
    fmt.setDepthBufferSize(24);
    fmt.setStencilBufferSize(8);
    if (m_hw.context_type == RETRO_HW_CONTEXT_OPENGL_CORE) {
        fmt.setVersion(int(m_hw.version_major ? m_hw.version_major : 3), int(m_hw.version_major ? m_hw.version_minor : 3));
        fmt.setProfile(QSurfaceFormat::CoreProfile);
    } else { // OpenGL "clásico": perfil de compatibilidad, la versión más alta que dé el controlador
        fmt.setVersion(4, 5);
        fmt.setProfile(QSurfaceFormat::CompatibilityProfile);
    }
    m_gl = new QOpenGLContext;
    m_gl->setFormat(fmt);
    if (!m_gl->create()) { // segundo intento: lo que el sistema ofrezca por defecto
        m_gl->setFormat(QSurfaceFormat());
        if (!m_gl->create()) { destroyGl(); return false; }
    }
    m_glSurface = new QOffscreenSurface;
    m_glSurface->setFormat(m_gl->format());
    m_glSurface->create();
    if (!m_glSurface->isValid() || !m_gl->makeCurrent(m_glSurface)) { destroyGl(); return false; }
    m_hwActive = true;
    if (!ensureFbo(int(qMax(m_av.geometry.max_width, m_av.geometry.base_width)),
                   int(qMax(m_av.geometry.max_height, m_av.geometry.base_height)))) { destroyGl(); return false; }
    m_hw.context_reset();
    return true;
}

// El framebuffer donde dibuja el núcleo; crece si el juego pide una resolución mayor
bool LibretroCore::ensureFbo(int w, int h)
{
    if (!m_hwActive) return false;
    w = qBound(64, w, 8192); h = qBound(64, h, 8192);
    if (m_fbo && m_fbo->width() >= w && m_fbo->height() >= h) return true;
    glCurrent();
    delete m_fbo;
    QOpenGLFramebufferObjectFormat ff;
    ff.setAttachment(m_hw.depth ? QOpenGLFramebufferObject::CombinedDepthStencil : QOpenGLFramebufferObject::NoAttachment);
    m_fbo = new QOpenGLFramebufferObject(w, h, ff);
    return m_fbo->isValid();
}

void LibretroCore::glCurrent()
{
    if (m_hwActive && m_gl && QOpenGLContext::currentContext() != m_gl) m_gl->makeCurrent(m_glSurface);
}

void LibretroCore::destroyGl()
{
    if (m_gl && m_glSurface && m_glSurface->isValid()) m_gl->makeCurrent(m_glSurface);
    delete m_fbo; m_fbo = nullptr;
    if (m_gl) m_gl->doneCurrent();
    delete m_gl; m_gl = nullptr;
    delete m_glSurface; m_glSurface = nullptr;
    m_hwActive = false;
}

// Copia a m_frame lo que el núcleo dibujó. OpenGL entrega las filas de abajo arriba: se invierten
// salvo que el núcleo ya dibuje con el origen arriba (bottom_left_origin = false).
void LibretroCore::readHwFrame(unsigned w, unsigned h)
{
    if (!m_hwActive || !m_fbo) return;
    glCurrent();
    w = qMin(w, unsigned(m_fbo->width()));
    h = qMin(h, unsigned(m_fbo->height()));
    QOpenGLFunctions *f = m_gl->functions();
    f->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo->handle());
    f->glPixelStorei(GL_PACK_ALIGNMENT, 4);
    QImage img(int(w), int(h), QImage::Format_RGBX8888);
    if (m_hw.bottom_left_origin) {
        std::vector<uchar> buf(size_t(w) * h * 4);
        f->glReadPixels(0, 0, int(w), int(h), GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
        for (unsigned y = 0; y < h; ++y)
            memcpy(img.scanLine(int(y)), buf.data() + size_t(h - 1 - y) * w * 4, size_t(w) * 4);
    } else {
        f->glReadPixels(0, 0, int(w), int(h), GL_RGBA, GL_UNSIGNED_BYTE, img.bits());
    }
    m_frame = img;
}

uintptr_t LibretroCore::cbGetFramebuffer()
{
    return s_self && s_self->m_fbo ? uintptr_t(s_self->m_fbo->handle()) : 0;
}

retro_proc_address_t LibretroCore::cbGetProcAddress(const char *sym)
{
    if (!s_self || !s_self->m_gl) return nullptr;
    return reinterpret_cast<retro_proc_address_t>(s_self->m_gl->getProcAddress(sym));
}

void LibretroCore::runFrame()
{
    glCurrent();
    if (m_rewinding) {
        if (++m_rewindCounter % 2 == 0 && !m_rewind.empty()) {
            const QByteArray &st = m_rewind.back();
            m_api.unserialize(st.constData(), size_t(st.size()));
            if (m_rewind.size() > 1) m_rewind.pop_back(); // el más antiguo se queda: ahí se detiene
            m_api.run();
        } else {
            m_pad->poll(); // sin correr el núcleo hay que leer el mando aquí para notar que se soltó
        }
        return;
    }
    m_api.run();
    if (!m_rewindOn || ++m_rewindCounter < 6) return;
    m_rewindCounter = 0;
    const size_t sz = m_api.serialize_size();
    if (sz == 0) return;
    QByteArray st(qsizetype(sz), Qt::Uninitialized);
    if (!m_api.serialize(st.data(), sz)) return;
    m_rewind.push_back(std::move(st));
    const size_t maxStates = qBound<size_t>(20, (256u << 20) / sz, 600);
    while (m_rewind.size() > maxStates) m_rewind.pop_front();
}

void LibretroCore::setVolume(double v)
{
    m_volume = qBound(0.0, v, 1.0);
    if (m_sink) m_sink->setVolume(float(m_volume));
}

QImage LibretroCore::screenshot() const
{
    if (m_frame.isNull()) return {};
    QImage img = m_rotation ? m_frame.transformed(QTransform().rotate(-90.0 * m_rotation)) : m_frame;
    const int w = qRound(img.height() * aspectRatio());
    if (w > 0 && w != img.width())
        img = img.scaled(w, img.height(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    return img;
}

QVariantList LibretroCore::options() const
{
    QVariantList out;
    for (const OptDef &d : m_optDefs) {
        QStringList values;
        for (const QByteArray &v : d.values) values << QString::fromUtf8(v);
        out << QVariantMap{ { QStringLiteral("key"), QString::fromUtf8(d.key) },
                            { QStringLiteral("label"), d.label },
                            { QStringLiteral("value"), QString::fromUtf8(m_vars.value(d.key)) },
                            { QStringLiteral("values"), values } };
    }
    return out;
}

bool LibretroCore::setOption(const QByteArray &key, const QByteArray &value)
{
    if (!m_vars.contains(key)) return false;
    m_vars.insert(key, value);
    m_overrides.insert(key, value);
    m_varsUpdated = true;
    return true;
}

void LibretroCore::reset()
{
    glCurrent();
    if (m_gameLoaded) m_api.reset();
}

bool LibretroCore::saveState(const QString &path)
{
    if (!m_gameLoaded) return false;
    glCurrent();
    const size_t sz = m_api.serialize_size();
    if (sz == 0) return false;
    QByteArray buf(qsizetype(sz), Qt::Uninitialized);
    if (!m_api.serialize(buf.data(), sz)) return false;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    return f.write(buf) == buf.size();
}

bool LibretroCore::loadState(const QString &path)
{
    if (!m_gameLoaded) return false;
    glCurrent();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QByteArray buf = f.readAll();
    return m_api.unserialize(buf.constData(), size_t(buf.size()));
}

double LibretroCore::aspectRatio() const
{
    double ar = m_av.geometry.aspect_ratio;
    if (ar <= 0.0) {
        const unsigned w = m_frame.isNull() ? m_av.geometry.base_width  : unsigned(m_frame.width());
        const unsigned h = m_frame.isNull() ? m_av.geometry.base_height : unsigned(m_frame.height());
        ar = (h > 0) ? double(w) / double(h) : 4.0 / 3.0;
    }
    // Con rotación de 90/270 la imagen final tiene la orientación contraria a la del cuadro
    // (cuadro apaisado → juego vertical). Unos núcleos dan la relación ya girada (FBNeo: 3:4) y
    // otros la del cuadro sin girar: solo se invierte si no corresponde con el resultado.
    if (m_rotation & 1) {
        const bool frameWide = m_frame.isNull() ? m_av.geometry.base_width >= m_av.geometry.base_height
                                                : m_frame.width() >= m_frame.height();
        if ((ar > 1.0) == frameWide) ar = 1.0 / ar;
    }
    return ar;
}

// ---------------------------------------------------------------- ritmo de cuadros

void LibretroCore::tick()
{
    if (!m_gameLoaded || m_paused) return;
    const double fps = m_av.timing.fps > 1.0 ? m_av.timing.fps : 60.0;
    const double frameMs = 1000.0 / fps;
    const double now = double(m_clock.nsecsElapsed()) / 1e6;

    const bool rewind = m_rewindOn && m_pad && m_pad->rewindHeld();
    if (rewind != m_rewinding) { m_rewinding = rewind; m_rewindCounter = 0; emit rewindingChanged(); }
    const bool fast = !m_rewinding && (m_fast || (m_pad && m_pad->fastHeld()));
    m_mute = fast || m_rewinding;

    if (fast) { // hasta ~12 ms de emulación por vuelta; la pantalla se sigue refrescando
        for (int i = 0; i < 8 && m_gameLoaded && double(m_clock.nsecsElapsed()) / 1e6 - now < 12.0; ++i)
            runFrame();
        m_nextFrameMs = double(m_clock.nsecsElapsed()) / 1e6;
        return;
    }
    if (now < m_nextFrameMs) return;

    // Si nos atrasamos mucho (ventana arrastrada, etc.) no intentes recuperar todo.
    if (now - m_nextFrameMs > frameMs * 4)
        m_nextFrameMs = now;

    int frames = 0;
    while (now >= m_nextFrameMs && frames < 2 && m_gameLoaded) {
        runFrame();
        m_nextFrameMs += frameMs;
        ++frames;
    }

}

// ---------------------------------------------------------------- audio

void LibretroCore::startAudio(double sampleRate)
{
    stopAudio();
    QAudioFormat fmt;
    fmt.setSampleRate(int(sampleRate + 0.5));
    fmt.setChannelCount(2);
    fmt.setSampleFormat(QAudioFormat::Int16);

    const QAudioDevice dev = QMediaDevices::defaultAudioOutput();
    if (dev.isNull() || !dev.isFormatSupported(fmt)) {
        qWarning() << "Formato de audio no soportado; el juego correrá sin sonido";
        return;
    }
    m_sink = std::make_unique<QAudioSink>(dev, fmt);
    // ~80 ms de búfer: baja latencia sin cortes.
    m_sink->setBufferSize(fmt.bytesForDuration(80000));
    m_sink->setVolume(float(m_volume));
    m_audioDev = m_sink->start();
}

void LibretroCore::stopAudio()
{
    if (m_sink) m_sink->stop();
    m_audioDev = nullptr;
    m_sink.reset();
    m_audioBuf.clear();
}

// ---------------------------------------------------------------- callbacks

bool LibretroCore::cbEnvironment(unsigned cmd, void *data)
{
    return s_self ? s_self->environment(cmd, data) : false;
}

bool LibretroCore::environment(unsigned cmd, void *data)
{
    switch (cmd) {
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        *static_cast<const char **>(data) = m_systemDir.constData();
        return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *static_cast<const char **>(data) = m_saveDir.constData();
        return true;
    case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY:
        *static_cast<const char **>(data) = m_systemDir.constData();
        return true;

    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
        const auto fmt = *static_cast<const retro_pixel_format *>(data);
        if (fmt == RETRO_PIXEL_FORMAT_0RGB1555 || fmt == RETRO_PIXEL_FORMAT_RGB565
            || fmt == RETRO_PIXEL_FORMAT_XRGB8888) {
            m_pixFmt = fmt;
            return true;
        }
        return false;
    }

    case RETRO_ENVIRONMENT_SET_ROTATION:
        m_rotation = *static_cast<const unsigned *>(data) & 3;
        return true;

    case RETRO_ENVIRONMENT_SET_GEOMETRY: {
        const auto *g = static_cast<const retro_game_geometry *>(data);
        m_av.geometry = *g;
        ensureFbo(int(qMax(g->max_width, g->base_width)), int(qMax(g->max_height, g->base_height)));
        return true;
    }
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: {
        m_av = *static_cast<const retro_system_av_info *>(data);
        ensureFbo(int(qMax(m_av.geometry.max_width, m_av.geometry.base_width)),
                  int(qMax(m_av.geometry.max_height, m_av.geometry.base_height)));
        if (m_gameLoaded) startAudio(m_av.timing.sample_rate);
        return true;
    }

    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        *static_cast<bool *>(data) = true;
        return true;

    // ---- Render por hardware: solo OpenGL de escritorio ----
    case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER:
        if (data) *static_cast<unsigned *>(data) = RETRO_HW_CONTEXT_OPENGL;
        return true;
    case RETRO_ENVIRONMENT_SET_HW_RENDER: {
        auto *cb = static_cast<retro_hw_render_callback *>(data);
        if (!cb) return false;
        if (cb->context_type != RETRO_HW_CONTEXT_OPENGL && cb->context_type != RETRO_HW_CONTEXT_OPENGL_CORE)
            return false; // GLES, Vulkan o Direct3D: el núcleo suele reintentar con OpenGL
        cb->get_current_framebuffer = &LibretroCore::cbGetFramebuffer;
        cb->get_proc_address = &LibretroCore::cbGetProcAddress;
        m_hw = *cb;
        return true;
    }
    case RETRO_ENVIRONMENT_SET_HW_SHARED_CONTEXT:
        return true;

    case RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE:
        if (data) m_disk = *static_cast<const retro_disk_control_callback *>(data);
        return true;

    case RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE:
        if (data) static_cast<retro_rumble_interface *>(data)->set_rumble_state = &LibretroCore::cbRumble;
        return true;

    // Los estados que pedimos son siempre guardados normales (no hay run-ahead ni netplay).
    // FBNeo lo necesita para activar las tablas de récords (hiscore.dat).
    case RETRO_ENVIRONMENT_GET_SAVESTATE_CONTEXT:
        if (data) *static_cast<int *>(data) = RETRO_SAVESTATE_CONTEXT_NORMAL; // con data nulo solo preguntan si existe
        return true;

    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        static_cast<retro_log_callback *>(data)->log = &LibretroCore::cbLog;
        return true;

    // ---- Opciones del núcleo (API "legacy", la más simple) ----
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        *static_cast<unsigned *>(data) = 0;
        return true;

    case RETRO_ENVIRONMENT_SET_VARIABLES: {
        // Formato: { "clave", "Descripción; default|opcion2|opcion3" }
        m_optDefs.clear(); // el núcleo manda siempre la lista completa (FBNeo la repite al cargar el juego)
        for (auto *v = static_cast<const retro_variable *>(data); v && v->key; ++v) {
            const QByteArray key = v->key;
            QByteArray def;
            OptDef d{ key, QString::fromUtf8(key), {} };
            if (v->value) {
                QByteArray s = v->value;
                const int semi = s.indexOf("; ");
                if (semi >= 0) { d.label = QString::fromUtf8(s.left(semi)); s = s.mid(semi + 2); }
                d.values = s.split('|');
                def = d.values.value(0);
            }
            if (d.values.size() > 1) m_optDefs.push_back(d);
            m_vars.insert(key, m_overrides.value(key, def));
        }
        m_varsDirty = true;
        return true;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        auto *v = static_cast<retro_variable *>(data);
        if (!v || !v->key) return false;
        const QByteArray key = v->key;
        if (m_overrides.contains(key))      m_getVarBuf = m_overrides.value(key);
        else if (m_vars.contains(key))      m_getVarBuf = m_vars.value(key);
        else { v->value = nullptr; return false; }
        v->value = m_getVarBuf.constData();
        return true;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *static_cast<bool *>(data) = m_varsUpdated;
        m_varsUpdated = false;
        return true;

    case RETRO_ENVIRONMENT_SET_MESSAGE: {
        const auto *m = static_cast<const retro_message *>(data);
        if (m && m->msg) emit message(QString::fromUtf8(m->msg));
        return true;
    }

    case RETRO_ENVIRONMENT_SHUTDOWN:
        QMetaObject::invokeMethod(this, &LibretroCore::unloadGame, Qt::QueuedConnection);
        return true;

    // Aceptamos sin hacer nada
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
        return true;

    default:
        return false;
    }
}

void LibretroCore::cbVideo(const void *data, unsigned w, unsigned h, size_t pitch)
{
    LibretroCore *self = s_self;
    if (self && data == RETRO_HW_FRAME_BUFFER_VALID && w && h) { // el núcleo dibujó con OpenGL
        self->readHwFrame(w, h);
        emit self->frameReady();
        return;
    }
    if (!self || !data || w == 0 || h == 0) {
        if (self) emit self->frameReady(); // cuadro duplicado: se repinta el anterior
        return;
    }

    QImage::Format fmt;
    switch (self->m_pixFmt) {
    case RETRO_PIXEL_FORMAT_XRGB8888: fmt = QImage::Format_RGB32;  break;
    case RETRO_PIXEL_FORMAT_RGB565:   fmt = QImage::Format_RGB16;  break;
    default:                          fmt = QImage::Format_RGB555; break;
    }
    // Copia profunda: el núcleo reutiliza su búfer.
    QImage view(static_cast<const uchar *>(data), int(w), int(h), qsizetype(pitch), fmt);
    if (fmt == QImage::Format_RGB32)
        self->m_frame = view.copy();
    else
        self->m_frame = view.convertToFormat(QImage::Format_RGB32);
    emit self->frameReady();
}

void LibretroCore::cbAudioSample(int16_t l, int16_t r)
{
    const int16_t s[2] = { l, r };
    cbAudioBatch(s, 1);
}

size_t LibretroCore::cbAudioBatch(const int16_t *data, size_t frames)
{
    LibretroCore *self = s_self;
    if (!self || !self->m_audioDev || !self->m_sink || self->m_mute) return frames;

    const qsizetype bytes = qsizetype(frames * 2 * sizeof(int16_t));
    const qsizetype freeBytes = self->m_sink->bytesFree();
    // Si el búfer está lleno se descarta el excedente para no acumular retraso.
    const qsizetype toWrite = qMin(bytes, freeBytes) & ~qsizetype(3);
    if (toWrite > 0)
        self->m_audioDev->write(reinterpret_cast<const char *>(data), toWrite);
    return frames;
}

void LibretroCore::cbInputPoll()
{
    if (s_self && s_self->m_pad) s_self->m_pad->poll();
}

int16_t LibretroCore::cbInputState(unsigned port, unsigned device, unsigned index, unsigned id)
{
    if (!s_self || !s_self->m_pad) return 0;
    if ((device & RETRO_DEVICE_MASK) == RETRO_DEVICE_ANALOG) {
        if (index > RETRO_DEVICE_INDEX_ANALOG_RIGHT) return 0; // botones analógicos: no hay
        return s_self->m_pad->analog(int(port), index, id);
    }
    if ((device & RETRO_DEVICE_MASK) != RETRO_DEVICE_JOYPAD) return 0;
    return s_self->m_pad->retroButton(int(port), id) ? 1 : 0;
}

bool LibretroCore::cbRumble(unsigned port, enum retro_rumble_effect effect, uint16_t strength)
{
    if (!s_self || !s_self->m_pad) return false;
    s_self->m_pad->setRumble(int(port), effect == RETRO_RUMBLE_STRONG, strength);
    return true;
}

void LibretroCore::cbLog(enum retro_log_level level, const char *fmt, ...)
{
    const bool loading = s_self && s_self->m_loading;
    if (level < RETRO_LOG_WARN && !loading) return;
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    const QString line = QString::fromUtf8(buf).trimmed();
    if (loading && (line.contains(QLatin1String("is required")) || line.contains(QLatin1String("is missing"))
                    || line.contains(QLatin1String("is unknown"), Qt::CaseInsensitive)))
        s_self->m_loadProblems << line;
    if (loading && line.contains(QLatin1String("Driver successfully started")))
        s_self->m_driverStarted = true;
    if (level >= RETRO_LOG_WARN || qEnvironmentVariableIsSet("ARCADE_LOG_ALL"))
        qWarning().noquote() << "[core]" << line;
}
