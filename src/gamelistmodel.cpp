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

// Formato: zip|Título|Año|Fabricante|Sistema. Con overwrite=false solo rellena lo que falte
// (así los títulos cortos de names.txt mandan y fbneo.txt aporta el sistema y el resto).
void GameListModel::loadNamesFile(const QString &file, bool overwrite)
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
        Meta &m = m_meta[p.value(0).trimmed().toLower()];
        auto put = [overwrite](QString &dst, const QString &src) {
            if (!src.isEmpty() && (overwrite || dst.isEmpty())) dst = src;
        };
        put(m.title, p.value(1).trimmed());
        put(m.year, p.value(2).trimmed());
        put(m.maker, p.value(3).trimmed());
        put(m.system, p.value(4).trimmed());
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
                if (!name.isEmpty() && m_meta.value(name).title.isEmpty()) {
                    Meta &dst = m_meta[name];
                    dst.title = m.title; dst.year = m.year; dst.maker = m.maker;
                }
            }
        }
    }
}

void GameListModel::loadStatus()
{
    m_status.clear();
    QFile f(m_base + QStringLiteral("/roms/estado.txt"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);
    while (!in.atEnd()) {
        const QStringList p = in.readLine().trimmed().split(u'|');
        if (p.size() < 2 || p.at(0).startsWith(u'#')) continue;
        m_status.insert(p.at(0).trimmed().toLower(), p.at(1).trimmed().toLower() == u"ok" ? 1 : -1);
    }
}

void GameListModel::saveStatus() const
{
    QFile f(m_base + QStringLiteral("/roms/estado.txt"));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) return;
    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    out << "# Estado de cada ROM: ok = funciona, x = no funciona. Se actualiza solo al lanzar un juego.\n";
    QStringList roms = m_status.keys();
    roms.sort();
    for (const QString &rom : roms)
        out << rom << '|' << (m_status.value(rom) > 0 ? "ok" : "x") << '\n';
}

void GameListModel::setStatus(const QString &rom, int status)
{
    if (m_status.value(rom, 0) == status) return;
    m_status.insert(rom, status);
    saveStatus();
    for (int i = 0; i < m_view.size(); ++i)
        if (at(i).rom == rom) emit dataChanged(index(i), index(i), { StatusRole });
}

void GameListModel::setSystem(const QString &s)
{
    if (s == m_system) return;
    m_system = s;
    beginResetModel();
    applyFilter();
    endResetModel();
    emit filterChanged();
    emit countChanged();
}

void GameListModel::setSearch(const QString &s)
{
    if (s == m_search) return;
    m_search = s;
    beginResetModel();
    applyFilter();
    endResetModel();
    emit filterChanged();
    emit countChanged();
}

void GameListModel::cycleSystem(int direction)
{
    if (m_systems.isEmpty()) return;
    const int n = int(m_systems.size()) + 1;                 // posición 0 = todos
    const int cur = m_system.isEmpty() ? 0 : int(m_systems.indexOf(m_system)) + 1;
    const int next = ((cur + (direction < 0 ? -1 : 1)) % n + n) % n;
    setSystem(next == 0 ? QString() : m_systems.at(next - 1));
}

// Recalcula m_view: sistema exacto y todas las palabras de la búsqueda en el título o el nombre del zip
void GameListModel::applyFilter()
{
    const QStringList words = m_search.toLower().split(u' ', Qt::SkipEmptyParts);
    m_view.clear();
    for (int i = 0; i < m_all.size(); ++i) {
        const Game &g = m_all.at(i);
        if (!m_system.isEmpty() && g.system != m_system) continue;
        bool ok = true;
        for (const QString &w : words)
            if (!g.key.contains(w)) { ok = false; break; }
        if (ok) m_view.push_back(i);
    }
}

int GameListModel::sourceRow(int row) const
{
    return row >= 0 && row < m_view.size() ? m_view.at(row) : -1;
}

int GameListModel::rowOfSource(int source) const
{
    return int(m_view.indexOf(source));
}

void GameListModel::rescan()
{
    beginResetModel();
    m_meta.clear();
    m_all.clear();

    loadNamesFile(QStringLiteral(":/resources/names.txt"), true);     // títulos cortos integrados
    loadNamesFile(m_base + QStringLiteral("/roms/names.txt"), true);  // la del usuario sobreescribe
    loadNamesFile(QStringLiteral(":/resources/fbneo.txt"), false);    // todo FBNeo: sistema y lo que falte
    loadDats();
    loadStatus();

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
        // Sistema: el del set; si es un hack con sufijo ("kof2002-5a"), el de su juego base
        if (it != m_meta.cend()) g.system = it->system;
        if (g.system.isEmpty() && rom.contains(u'-'))
            g.system = m_meta.value(rom.section(u'-', 0, 0)).system;
        if (g.system.isEmpty()) g.system = QStringLiteral("OTROS");
        g.key = (g.title + u' ' + g.rom).toLower();
        m_all.push_back(g);
    }

    std::sort(m_all.begin(), m_all.end(), [](const Game &a, const Game &b) {
        return QString::compare(a.title, b.title, Qt::CaseInsensitive) < 0;
    });

    m_systems.clear();
    for (const Game &g : m_all)
        if (!m_systems.contains(g.system)) m_systems << g.system;
    m_systems.sort();
    if (!m_systems.contains(m_system)) m_system.clear();
    applyFilter();

    endResetModel();
    emit systemsChanged();
    emit filterChanged();
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
    return parent.isValid() ? 0 : int(m_view.size());
}

QVariant GameListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_view.size()) return {};
    const Game &g = at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case TitleRole:   return g.title;
    case RomRole:     return g.rom;
    case YearRole:    return g.year;
    case MakerRole:   return g.maker;
    case PathRole:    return g.path;
    case StatusRole:  return m_status.value(g.rom, 0);
    case SystemRole:  return g.system;
    case VideoRole:
        return mediaFile(g.rom, { QStringLiteral("videos/"), QString() },
                         { QStringLiteral(".mp4"), QStringLiteral(".webm"), QStringLiteral(".avi"), QStringLiteral(".mkv") });
    case ImageRole:
        return mediaFile(g.rom, { QStringLiteral("snaps/"), QStringLiteral("titles/"), QString() },
                         { QStringLiteral(".png"), QStringLiteral(".jpg"), QStringLiteral(".jpeg"), QStringLiteral(".bmp") });
    case MarqueeRole:
        return mediaFile(g.rom, { QStringLiteral("marquees/"), QStringLiteral("wheel/") },
                         { QStringLiteral(".png"), QStringLiteral(".jpg"), QStringLiteral(".jpeg"), QStringLiteral(".bmp") });
    }
    return {};
}

QHash<int, QByteArray> GameListModel::roleNames() const
{
    return {
        { RomRole, "rom" }, { TitleRole, "title" }, { YearRole, "year" },
        { MakerRole, "maker" }, { PathRole, "path" }, { VideoRole, "video" },
        { ImageRole, "image" }, { MarqueeRole, "marquee" }, { StatusRole, "status" }, { SystemRole, "system" },
    };
}

QVariantMap GameListModel::get(int row) const
{
    QVariantMap m;
    if (row < 0 || row >= m_view.size()) return m;
    const QModelIndex i = index(row);
    const auto roles = roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        m.insert(QString::fromLatin1(it.value()), data(i, it.key()));
    return m;
}

int GameListModel::jumpLetter(int current, int direction) const
{
    if (m_view.isEmpty()) return 0;
    current = qBound(0, current, int(m_view.size()) - 1);
    auto letter = [this](int i) { return at(i).title.left(1).toUpper(); };
    const QString cur = letter(current);
    if (direction > 0) {
        for (int i = current + 1; i < m_view.size(); ++i)
            if (letter(i) != cur) return i;
        return 0; // vuelve al inicio
    }
    // Hacia atrás: inicio de la letra actual, o de la anterior si ya estamos al inicio
    int i = current;
    while (i > 0 && letter(i - 1) == cur) --i;
    if (i != current) return i;
    if (i == 0) return int(m_view.size()) - 1;
    const QString prev = letter(i - 1);
    int j = i - 1;
    while (j > 0 && letter(j - 1) == prev) --j;
    return j;
}
