#pragma once
#include <QAbstractListModel>
#include <QHash>
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
public:
    enum Roles {
        RomRole = Qt::UserRole + 1,
        TitleRole, YearRole, MakerRole, PathRole,
        VideoRole, ImageRole, MarqueeRole,
        StatusRole, // 1 = funciona, -1 = no funciona, 0 = sin probar
        SystemRole
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
    // Pasa al sistema siguiente/anterior: todos → primero → … → último → todos
    Q_INVOKABLE void cycleSystem(int direction);

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
    struct Meta { QString title, year, maker, system; };
    struct Game { QString rom, title, year, maker, path, system, key; }; // key = texto en minúsculas para buscar

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
};
