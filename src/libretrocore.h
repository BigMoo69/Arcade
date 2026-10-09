#pragma once
// Host libretro mínimo: carga fbneo_libretro.dll y ejecuta un juego.
// Todo corre en el hilo de la GUI (FBNeo es 100% software, sobra rendimiento).

#include <QObject>
#include <QImage>
#include <QLibrary>
#include <QHash>
#include <QStringList>
#include <QVariantList>
#include <QVector>
#include <QMutex>
#include <QElapsedTimer>
#include <QTimer>
#include <memory>
#include <deque>

#include "libretro/libretro.h"

class QAudioSink;
class QIODevice;
class Gamepad;

class LibretroCore : public QObject
{
    Q_OBJECT
public:
    explicit LibretroCore(Gamepad *pad, QObject *parent = nullptr);
    ~LibretroCore() override;

    // Ruta de la DLL del núcleo, directorio de sistema (BIOS) y de guardado.
    // Solo hay un núcleo cargado a la vez: pedir otro descarga el anterior.
    bool loadCore(const QString &corePath, const QString &systemDir, const QString &saveDir);
    void unloadCore();
    QString corePath() const { return m_corePath; }
    QString describe() const; // nombre, versión y extensiones del núcleo cargado (para diagnóstico)
    bool loadGame(const QString &romPath);
    void unloadGame();
    bool isRunning() const { return m_gameLoaded; }

    void setPaused(bool paused);
    bool isPaused() const { return m_paused; }

    // Avance rápido: corre tantos cuadros como dé tiempo, sin sonido
    void setFastForward(bool on);
    bool fastForward() const { return m_fast; }
    void setVolume(double v); // 0..1

    // Rebobinar: guarda un estado cada pocos cuadros y, mientras el mando o el teclado mantengan
    // el botón de rebobinar, los va cargando hacia atrás. Gasta memoria: por eso se puede apagar.
    void setRewindEnabled(bool on);
    bool rewinding() const { return m_rewinding; }

    // Cuadro actual ya girado y con la proporción de pantalla correcta (capturas y miniaturas)
    QImage screenshot() const;

    // Opciones que el núcleo declara (dificultad, región, DIP switches…): lista de
    // { key, label, value, values[] }. setOption la cambia en caliente; devuelve false si no existe.
    QVariantList options() const;
    bool setOption(const QByteArray &key, const QByteArray &value);

    // Lo que el núcleo echó en falta al cargar el último juego (vacío = todo bien).
    // FBNeo devuelve "cargado" aunque falten ROMs y muestra su propia pantalla de error.
    QStringList loadProblems() const { return m_loadProblems; }
    // true si el último juego no arrancó de verdad (faltan archivos o FBNeo no confirmó el arranque)
    bool loadLooksBad() const { return !m_loadProblems.isEmpty() || (m_isFbneo && !m_driverStarted); }

    bool saveState(const QString &path);
    bool loadState(const QString &path);
    void reset();

    QString lastError() const { return m_error; }

    // Datos de video para EmulatorView
    QImage frame() const { return m_frame; }
    double aspectRatio() const;          // relación final (ya considera la rotación)
    int rotation() const { return m_rotation; } // 0..3 (múltiplos de 90° antihorario)

    // Overrides de opciones del núcleo (cores/fbneo.ini)
    void setOptionOverrides(const QHash<QByteArray, QByteArray> &o) { m_overrides = o; }

signals:
    void frameReady();
    void gameStopped();
    void pausedChanged();
    void rewindingChanged();
    void message(const QString &text);

private:
    // Callbacks estáticos de libretro (redirigen a la instancia activa)
    static bool    cbEnvironment(unsigned cmd, void *data);
    static void    cbVideo(const void *data, unsigned w, unsigned h, size_t pitch);
    static void    cbAudioSample(int16_t l, int16_t r);
    static size_t  cbAudioBatch(const int16_t *data, size_t frames);
    static void    cbInputPoll();
    static int16_t cbInputState(unsigned port, unsigned device, unsigned index, unsigned id);
    static void    cbLog(enum retro_log_level level, const char *fmt, ...);
    static bool    cbRumble(unsigned port, enum retro_rumble_effect effect, uint16_t strength);

    bool environment(unsigned cmd, void *data);
    void tick();
    QString extractFromZip(const QString &zipPath, const QString &validExts);
    void loadSram();
    void saveSram();
    void startAudio(double sampleRate);
    void stopAudio();

    // Funciones exportadas por el núcleo
    struct Api {
        void (*init)();
        void (*deinit)();
        unsigned (*api_version)();
        void (*get_system_info)(retro_system_info *);
        void (*get_system_av_info)(retro_system_av_info *);
        void (*set_environment)(retro_environment_t);
        void (*set_video_refresh)(retro_video_refresh_t);
        void (*set_audio_sample)(retro_audio_sample_t);
        void (*set_audio_sample_batch)(retro_audio_sample_batch_t);
        void (*set_input_poll)(retro_input_poll_t);
        void (*set_input_state)(retro_input_state_t);
        void (*set_controller_port_device)(unsigned, unsigned);
        void (*reset)();
        void (*run)();
        size_t (*serialize_size)();
        bool (*serialize)(void *, size_t);
        bool (*unserialize)(const void *, size_t);
        bool (*load_game)(const retro_game_info *);
        void (*unload_game)();
        void *(*get_memory_data)(unsigned);  // opcional
        size_t (*get_memory_size)(unsigned); // opcional
    } m_api{};

    static LibretroCore *s_self;

    Gamepad *m_pad;
    QLibrary m_lib;
    bool m_coreInited = false;
    bool m_gameLoaded = false;
    bool m_paused = false;
    bool m_loading = false;
    bool m_isFbneo = false;
    bool m_driverStarted = false;
    QStringList m_loadProblems;
    QString m_error;

    QString m_corePath, m_sramPath;
    QByteArray m_systemDir, m_saveDir;
    QByteArray m_romData;   // si el núcleo no pide ruta completa
    QByteArray m_romPathUtf8;

    retro_pixel_format m_pixFmt = RETRO_PIXEL_FORMAT_0RGB1555;
    retro_system_av_info m_av{};
    unsigned m_rotation = 0;
    QImage m_frame;

    // Opciones de núcleo: clave -> valor actual
    QHash<QByteArray, QByteArray> m_vars, m_overrides;
    struct OptDef { QByteArray key; QString label; QList<QByteArray> values; };
    QVector<OptDef> m_optDefs;
    bool m_varsDirty = false;
    bool m_varsUpdated = false; // hay un cambio que el núcleo aún no ha leído
    bool m_fast = false;
    bool m_mute = false;            // avance rápido o rebobinado: sin sonido
    bool m_rewindOn = false, m_rewinding = false;
    std::deque<QByteArray> m_rewind; // estados guardados, el más reciente al final
    int m_rewindCounter = 0;
    void runFrame();
    double m_volume = 1.0;
    QByteArray m_getVarBuf;

    // Audio
    std::unique_ptr<QAudioSink> m_sink;
    QIODevice *m_audioDev = nullptr;
    QByteArray m_audioBuf;

    // Ritmo de cuadros
    QTimer m_timer;
    QElapsedTimer m_clock;
    double m_nextFrameMs = 0;
};
