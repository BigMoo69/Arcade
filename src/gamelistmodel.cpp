#include "gamelistmodel.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTextStream>
#include <QUrl>
#include <QXmlStreamReader>
#include <algorithm>

// Archivos que son BIOS o dependencias, no juegos
static const QSet<QString> &biosSet()
{
    static const QSet<QString> s = {
        QStringLiteral("neogeo"), QStringLiteral("pgm"), QStringLiteral("decocass"),
        QStringLiteral("isgsm"), QStringLiteral("nmk004"), QStringLiteral("ym2608"),
        QStringLiteral("skns"), QStringLiteral("qsound"), QStringLiteral("cchip"),
        QStringLiteral("namcoc69"), QStringLiteral("namcoc70"), QStringLiteral("namcoc75"),
        QStringLiteral("bubsys"), QStringLiteral("midssio"), QStringLiteral("coleco"),
        QStringLiteral("spectrum"), QStringLiteral("msx"), QStringLiteral("hiscore"),
    };
    return s;
}

GameListModel::GameListModel(QObject *parent) : QAbstractListModel(parent) {}

void GameListModel::loadNamesFile(const QString &file)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(u'#')) continue;
        const QStringList p = line.split(u'|');
        if (p.size() < 2) continue;
        Meta m;
        m.title = p.value(1).trimmed();
        m.year  = p.value(2).trimmed();
        m.maker = p.value(3).trimmed();
        m_meta.insert(p.value(0).trimmed().toLower(), m);
    }
}

// Lee DATs XML (FinalBurn Neo o MAME -listxml) de la carpeta dats/
void GameListModel::loadDats()
{
    const QDir dir(m_base + QStringLiteral("/dats"));
    const auto files = dir.entryInfoList({ QStringLiteral("*.dat"), QStringLiteral("*.xml") }, QDir::Files);
    for (const QFileInfo &fi : files) {
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly)) continue;
        QXmlStreamReader x(&f);
        QString name; Meta m; bool inGame = false;
        while (!x.atEnd()) {
            x.readNext();
            if (x.isStartElement()) {
                const auto tag = x.name();
                if (tag == u"game" || tag == u"machine") {
                    inGame = true;
                    name = x.attributes().value(QLatin1String("name")).toString().toLower();
                    m = Meta{};
                } else if (inGame && tag == u"description") {
                    m.title = x.readElementText();
                } else if (inGame && tag == u"year") {
                    m.year = x.readElementText();
                } else if (inGame && tag == u"manufacturer") {
                    m.maker = x.readElementText();
                }
            } else if (x.isEndElement() && (x.name() == u"game" || x.name() == u"machine")) {
                inGame = false;
                // La lista integrada / la del usuario tiene prioridad (nombres más cortos)
                if (!name.isEmpty() && !m_meta.contains(name)) m_meta.insert(name, m);
            }
        }
    }
}

void GameListModel::rescan()
{
    beginResetModel();
    m_meta.clear();
    m_games.clear();

    loadNamesFile(QStringLiteral(":/resources/names.txt"));     // lista integrada
    loadNamesFile(m_base + QStringLiteral("/roms/names.txt"));  // la del usuario sobreescribe
    loadDats();

    const QDir roms(m_base + QStringLiteral("/roms"));
    const auto files = roms.entryInfoList({ QStringLiteral("*.zip"), QStringLiteral("*.7z") },
                                          QDir::Files, QDir::Name);
    for (const QFileInfo &fi : files) {
        const QString rom = fi.completeBaseName().toLower();
        if (biosSet().contains(rom)) continue;
        Game g;
        g.rom = rom;
        g.path = fi.absoluteFilePath();
        const auto it = m_meta.constFind(rom);
        if (it != m_meta.cend() && !it->title.isEmpty()) {
            g.title = it->title;
            g.year = it->year;
            g.maker = it->maker;
        } else {
            g.title = fi.completeBaseName().toUpper();
        }
        m_games.push_back(g);
    }

    std::sort(m_games.begin(), m_games.end(), [](const Game &a, const Game &b) {
        return QString::compare(a.title, b.title, Qt::CaseInsensitive) < 0;
    });

    endResetModel();
    emit countChanged();
}

QString GameListModel::mediaFile(const QString &rom, const QStringList &subdirs, const QStringList &exts) const
{
    for (const QString &sub : subdirs) {
        for (const QString &ext : exts) {
            const QString p = m_base + QStringLiteral("/media/") + sub + rom + ext;
            if (QFileInfo::exists(p)) return QUrl::fromLocalFile(p).toString();
        }
    }
    return {};
}

int GameListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_games.size());
}

QVariant GameListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_games.size()) return {};
    const Game &g = m_games.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case TitleRole:   return g.title;
    case RomRole:     return g.rom;
    case YearRole:    return g.year;
    case MakerRole:   return g.maker;
    case PathRole:    return g.path;
    case VideoRole:
        return mediaFile(g.rom, { QStringLiteral("videos/"), QString() },
                         { QStringLiteral(".mp4"), QStringLiteral(".webm"), QStringLiteral(".avi"), QStringLiteral(".mkv") });
    case ImageRole:
        return mediaFile(g.rom, { QStringLiteral("snaps/"), QStringLiteral("titles/"), QString() },
                         { QStringLiteral(".png"), QStringLiteral(".jpg") });
    case MarqueeRole:
        return mediaFile(g.rom, { QStringLiteral("marquees/"), QStringLiteral("wheel/") },
                         { QStringLiteral(".png"), QStringLiteral(".jpg") });
    }
    return {};
}

QHash<int, QByteArray> GameListModel::roleNames() const
{
    return {
        { RomRole, "rom" }, { TitleRole, "title" }, { YearRole, "year" },
        { MakerRole, "maker" }, { PathRole, "path" }, { VideoRole, "video" },
        { ImageRole, "image" }, { MarqueeRole, "marquee" },
    };
}

QVariantMap GameListModel::get(int row) const
{
    QVariantMap m;
    if (row < 0 || row >= m_games.size()) return m;
    const QModelIndex i = index(row);
    const auto roles = roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        m.insert(QString::fromLatin1(it.value()), data(i, it.key()));
    return m;
}

int GameListModel::jumpLetter(int current, int direction) const
{
    if (m_games.isEmpty()) return 0;
    current = qBound(0, current, int(m_games.size()) - 1);
    auto letter = [this](int i) { return m_games.at(i).title.left(1).toUpper(); };
    const QString cur = letter(current);
    if (direction > 0) {
        for (int i = current + 1; i < m_games.size(); ++i)
            if (letter(i) != cur) return i;
        return 0; // vuelve al inicio
    }
    // Hacia atrás: inicio de la letra actual, o de la anterior si ya estamos al inicio
    int i = current;
    while (i > 0 && letter(i - 1) == cur) --i;
    if (i != current) return i;
    if (i == 0) return int(m_games.size()) - 1;
    const QString prev = letter(i - 1);
    int j = i - 1;
    while (j > 0 && letter(j - 1) == prev) --j;
    return j;
}
