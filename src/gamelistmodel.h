#pragma once
#include <QAbstractListModel>
#include <QHash>
#include <QVector>

// Lista de juegos: escanea la carpeta roms/ y resuelve títulos y previews.
class GameListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    enum Roles {
        RomRole = Qt::UserRole + 1,
        TitleRole, YearRole, MakerRole, PathRole,
        VideoRole, ImageRole, MarqueeRole,
        StatusRole // 1 = funciona, -1 = no funciona, 0 = sin probar
    };

    explicit GameListModel(QObject *parent = nullptr);

    void setBaseDir(const QString &dir) { m_base = dir; }
    Q_INVOKABLE void rescan();

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return int(m_games.size()); }

    Q_INVOKABLE QVariantMap get(int row) const;
    // Estado de cada ROM, guardado en roms/estado.txt (líneas "rom|ok" o "rom|x")
    void setStatus(const QString &rom, int status);
    // Índice del primer juego de la siguiente/anterior letra (para L/R)
    Q_INVOKABLE int jumpLetter(int current, int direction) const;

signals:
    void countChanged();

private:
    struct Meta { QString title, year, maker; };
    struct Game { QString rom, title, year, maker, path; };

    void loadNamesFile(const QString &file);
    void loadDats();
    void loadStatus();
    void saveStatus() const;
    QString mediaFile(const QString &rom, const QStringList &subdirs, const QStringList &exts) const;

    QString m_base;
    QHash<QString, Meta> m_meta;
    QVector<Game> m_games;
    QHash<QString, int> m_status;
};
