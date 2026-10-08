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
#include <cstdarg>
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
    unloadGame();
    if (m_coreInited && m_api.deinit)
        m_api.deinit();
    m_lib.unload();
    if (s_self == this) s_self = nullptr;
}

template <typename T>
static bool resolveSym(QLibrary &lib, T &out, const char *name)
{
    out = reinterpret_cast<T>(lib.resolve(name));
    return out != nullptr;
}

bool LibretroCore::loadCore(const QString &corePath, const QString &systemDir, const QString &saveDir)
{
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
        return false;
    }

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

    m_romPathUtf8 = QDir::toNativeSeparators(romPath).toUtf8();
    retro_game_info info{};
    info.path = m_romPathUtf8.constData();

    if (!sys.need_fullpath) {
        QFile f(romPath);
        if (!f.open(QIODevice::ReadOnly)) {
            m_error = QStringLiteral("No se pudo abrir %1").arg(romPath);
            return false;
        }
        m_romData = f.readAll();
        info.data = m_romData.constData();
        info.size = size_t(m_romData.size());
    }

    m_rotation = 0;
    m_pixFmt = RETRO_PIXEL_FORMAT_0RGB1555;
    m_frame = QImage();

    if (!m_api.load_game(&info)) {
        m_error = QStringLiteral("No se pudo iniciar \"%1\".\n\nRevisa que el ROM sea de la versión correcta "
                                 "de FinalBurn Neo y que neogeo.zip esté en la carpeta roms si es de NeoGeo.")
                      .arg(QFileInfo(romPath).fileName());
        m_romData.clear();
        return false;
    }

    for (unsigned p = 0; p < 4; ++p)
        m_api.set_controller_port_device(p, RETRO_DEVICE_JOYPAD);

    m_api.get_system_av_info(&m_av);
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
    m_api.unload_game();
    m_gameLoaded = false;
    m_romData.clear();
    m_frame = QImage();
    emit gameStopped();
}

void LibretroCore::reset()
{
    if (m_gameLoaded) m_api.reset();
}

bool LibretroCore::saveState(const QString &path)
{
    if (!m_gameLoaded) return false;
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
    // Igual que RetroArch: con rotación de 90/270 se invierte la relación.
    if (m_rotation & 1) ar = 1.0 / ar;
    return ar;
}

// ---------------------------------------------------------------- ritmo de cuadros

void LibretroCore::tick()
{
    if (!m_gameLoaded) return;
    const double fps = m_av.timing.fps > 1.0 ? m_av.timing.fps : 60.0;
    const double frameMs = 1000.0 / fps;
    const double now = double(m_clock.nsecsElapsed()) / 1e6;

    if (now < m_nextFrameMs) return;

    // Si nos atrasamos mucho (ventana arrastrada, etc.) no intentes recuperar todo.
    if (now - m_nextFrameMs > frameMs * 4)
        m_nextFrameMs = now;

    int frames = 0;
    while (now >= m_nextFrameMs && frames < 2 && m_gameLoaded) {
        m_api.run();
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
        return true;
    }
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: {
        m_av = *static_cast<const retro_system_av_info *>(data);
        if (m_gameLoaded) startAudio(m_av.timing.sample_rate);
        return true;
    }

    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        *static_cast<bool *>(data) = true;
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
        for (auto *v = static_cast<const retro_variable *>(data); v && v->key; ++v) {
            const QByteArray key = v->key;
            QByteArray def;
            if (v->value) {
                QByteArray s = v->value;
                const int semi = s.indexOf("; ");
                if (semi >= 0) s = s.mid(semi + 2);
                const int bar = s.indexOf('|');
                def = bar >= 0 ? s.left(bar) : s;
            }
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
        *static_cast<bool *>(data) = false;
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
    if (!self || !self->m_audioDev || !self->m_sink) return frames;

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
    Q_UNUSED(index);
    if (!s_self || !s_self->m_pad) return 0;
    if ((device & RETRO_DEVICE_MASK) != RETRO_DEVICE_JOYPAD) return 0;
    return s_self->m_pad->retroButton(int(port), id) ? 1 : 0;
}

void LibretroCore::cbLog(enum retro_log_level level, const char *fmt, ...)
{
    if (level < RETRO_LOG_WARN) return;
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    qWarning().noquote() << "[core]" << QString::fromUtf8(buf).trimmed();
}
