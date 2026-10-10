#pragma once
#include <QAbstractListModel>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <QVector>

// Lista de juegos: escanea la carpeta roms/ y resuelve títulos, sistema y previews.
// La lista visible es un filtro (por sistema y por texto) sobre todos los juegos.
class GameListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)   // juegos visibles con el filtro actual
    Q_PROPERTY(int total READ total NOTIFY countChanged)   // todos los juegos
    Q_PROPERTY(QStringList systems READ systems NOTIFY systemsChanged) // sistemas con al menos un juego
    Q_PROPERTY(QString system READ system WRITE setSystem NOTIFY filterChanged) // "" = todos
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY filterChanged)
    Q_PROPERTY(bool hideBroken READ hideBroken WRITE setHideBroken NOTIFY filterChanged) // oculta los marcados con ✘
    Q_PROPERTY(bool hideClones READ hideClones WRITE setHideClones NOTIFY filterChanged)
public:
    enum Roles {
        RomRole = Qt::UserRole + 1,
        TitleRole, YearRole, MakerRole, PathRole,
        VideoRole, ImageRole, MarqueeRole,
        StatusRole, // 1 = funciona, -1 = no funciona, 0 = sin probar
        SystemRole,
        CoreRole,   // archivo del núcleo libretro que lo corre ("" = FinalBurn Neo)
        FavoriteRole,
        PlaysRole,    // veces jugado
        PlayTimeRole, // segundos jugados en total
        PlayersRole,  // 3, 4… si es una versión para varios jugadores; 0 si el título no lo dice
        LastPlayedRole // fecha de la última partida ("" = nunca)
    };

    explicit GameListModel(QObject *parent = nullptr);

    void setBaseDir(const QString &dir) { m_base = dir; }
    Q_INVOKABLE void rescan();

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return int(m_view.size()); }
    int total() const { return int(m_all.size()); }

    QStringList systems() const { return m_systems; }
    QString system() const { return m_system; }
    void setSystem(const QString &s);
    QString search() const { return m_search; }
    void setSearch(const QString &s);
    bool hideBroken() const { return m_hideBroken; }
    void setHideBroken(bool v);
    // Deja una sola versión de cada juego (la original si funciona; si no, un clon que funcione).
    // No se aplica en favoritos ni recientes.
    bool hideClones() const { return m_hideClones; }
    void setHideClones(bool v);
    // Listas especiales que se recorren junto con los sistemas (LT/RT)
    static QString favoritesName() { return QStringLiteral("★ FAVORITOS"); }
    static QString recentsName() { return QStringLiteral("RECIENTES"); }
    // Favoritos en roms/favoritos.txt (un rom por línea); devuelve si quedó marcado
    Q_INVOKABLE bool toggleFavorite(int row);
    // Estadísticas en roms/jugados.txt (rom|veces|segundos|última vez)
    void notePlayed(const QString &rom, qint64 seconds);
    // Pasa al sistema siguiente/anterior: todos → primero → … → último → todos
    Q_INVOKABLE void cycleSystem(int direction);
    // Tarjetas de la pantalla de sistemas: todos, favoritos, recientes y cada sistema (también los
    // definidos en sistemas.ini que aún no tienen juegos). Cada una: { name, id (valor para "system"),
    // count, images (hasta 4 capturas de sus juegos), logo (media/sistemas/<nombre>.png si existe), folder }.
    Q_INVOKABLE QVariantList systemCards() const;
    // Para un juego cuyo rol "core" empieza por '@': programa y argumentos de su sistema
    bool externalCommand(const QString &core, QString *program, QString *args, QStringList *relocate) const;

    Q_INVOKABLE QVariantMap get(int row) const;
    // Fila visible ↔ posición en la lista completa (para conservar la selección al cambiar el filtro)
    Q_INVOKABLE int sourceRow(int row) const;
    Q_INVOKABLE int rowOfSource(int source) const;
    // Estado de cada ROM, guardado en roms/estado.txt (líneas "rom|ok" o "rom|x")
    void setStatus(const QString &rom, int status);
    // Índice del primer juego de la siguiente/anterior letra (para L/R)
    Q_INVOKABLE int jumpLetter(int current, int direction) const;

signals:
    void countChanged();
    void systemsChanged();
    void filterChanged();

private:
    struct Meta { QString title, year, maker, system, parent; int players = 0; }; // players: solo si el título dice "N Players"
    // key = texto en minúsculas para buscar; parent = juego original si este es un clon
    // icon: imagen que trae el propio juego (ICON0.PNG de PS3 / PS Vita); se usa si no hay captura
    struct Game { QString rom, title, year, maker, path, system, key, core, parent, icon; int players = 0; bool duplicate = false; };
    void markDuplicates();
    // Sistema extra definido en cores/sistemas.ini: sus ROMs van en roms/<folder>/ y los corre otro núcleo
    // program/args: emulador aparte (programa externo) en vez de núcleo; entonces core = "@" + id
    // romDir: carpeta de juegos fuera del Arcade (clave "juegos"); vacía = roms/<carpeta>
    // relocate: archivos de configuración del emulador aparte que guardan rutas completas (clave "reubicar")
    // inside: juegos que son carpetas (clave "dentro"): archivo que debe existir dentro de cada una
    struct SystemDef { QString id, name, core, folder, program, args, romDir, inside; QStringList exts, relocate; };
    void loadSystemDefs();
    void writeDefaultSystems(const QString &file) const;
    QVector<SystemDef> m_defs;

    const Game &at(int row) const { return m_all.at(m_view.at(row)); }
    void loadNamesFile(const QString &file, bool overwrite);
    void loadDats();
    void loadStatus();
    void saveStatus() const;
    void applyFilter();
    QString mediaFile(const QString &rom, const QStringList &subdirs, const QStringList &exts) const;

    QString m_base;
    QHash<QString, Meta> m_meta;
    QVector<Game> m_all;
    QVector<int> m_view;
    QStringList m_systems;
    QString m_system, m_search;
    QHash<QString, int> m_status;
    struct Stat { int plays = 0; qint64 secs = 0; qint64 last = 0; };
    QHash<QString, Stat> m_stats;
    QSet<QString> m_favs;
    bool m_hideBroken = false, m_hideClones = false;
    void loadUserLists();
    void refilter();
};
