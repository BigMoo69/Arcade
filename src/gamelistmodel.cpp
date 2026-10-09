#include "gamelistmodel.h"

#include <QDir>
#include <QFile>
#include <QDateTime>
#include <QFileInfo>
#include <QRegularExpression>
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

// Formato: zip|Título|Año|Fabricante|Sistema|Original. Con overwrite=false solo rellena lo que falte
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
        put(m.parent, p.value(5).trimmed().toLower());
        // "(4 Players ver EAC)": se anota aunque names.txt le ponga luego un título corto
        static const QRegularExpression playersRe(QStringLiteral("(\\d)[ -]?players?\\b"),
                                                  QRegularExpression::CaseInsensitiveOption);
        const auto pm = playersRe.match(p.value(1));
        if (pm.hasMatch()) m.players = qMax(m.players, pm.captured(1).toInt());
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

// cores/sistemas.ini: un bloque [id] por sistema con nombre, nucleo, carpeta y extensiones.
// Si no existe se crea con los sistemas más comunes ya definidos (solo falta copiar el núcleo y los ROMs).
void GameListModel::loadSystemDefs()
{
    m_defs.clear();
    const QString file = m_base + QStringLiteral("/cores/sistemas.ini");
    if (!QFileInfo::exists(file)) writeDefaultSystems(file);
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);
    SystemDef cur;
    auto flush = [&] {
        if (!cur.id.isEmpty() && !cur.core.isEmpty() && !cur.exts.isEmpty()) {
            if (cur.folder.isEmpty()) cur.folder = cur.id;
            if (cur.name.isEmpty()) cur.name = cur.id.toUpper();
            m_defs.push_back(cur);
        }
        cur = SystemDef{};
    };
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(u';') || line.startsWith(u'#')) continue;
        if (line.startsWith(u'[') && line.endsWith(u']')) { flush(); cur.id = line.mid(1, line.size() - 2).trimmed().toLower(); continue; }
        const int eq = int(line.indexOf(u'='));
        if (eq <= 0) continue;
        const QString key = line.left(eq).trimmed().toLower();
        const QString val = line.mid(eq + 1).section(u';', 0, 0).trimmed();
        if (key == u"nombre") cur.name = val.toUpper();
        else if (key == u"nucleo") cur.core = val;
        else if (key == u"carpeta") cur.folder = val;
        else if (key == u"extensiones")
            for (const QString &e : val.toLower().split(u',', Qt::SkipEmptyParts)) cur.exts << e.trimmed();
    }
    flush();
    for (const SystemDef &d : m_defs) QDir(m_base).mkpath(QStringLiteral("roms/") + d.folder);
}

void GameListModel::writeDefaultSystems(const QString &file) const
{
    QDir().mkpath(QFileInfo(file).absolutePath());
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    out << "; ===== Sistemas extra de Arcade Multijuegos =====\n"
           "; Los .zip sueltos en roms/ son arcade y los corre FinalBurn Neo (no hace falta definirlos aqui).\n"
           "; Cada bloque de abajo es otro sistema: sus juegos van en roms/<carpeta>/ y los corre el nucleo\n"
           "; libretro indicado, que debe estar en esta carpeta cores/. Si el nucleo no esta, el juego\n"
           "; aparece en la lista pero avisa al intentar abrirlo.\n"
           "; Solo sirven nucleos que dibujan por software (no los que necesitan OpenGL/Vulkan).\n"
           "; Para agregar un sistema copia un bloque y cambia los cuatro valores.\n"
           "\n"
           "[nes]\nnombre = NINTENDO NES\nnucleo = fceumm_libretro.dll\ncarpeta = nes\nextensiones = nes,zip\n\n"
           "[snes]\nnombre = SUPER NINTENDO\nnucleo = snes9x_libretro.dll\ncarpeta = snes\nextensiones = sfc,smc,zip\n\n"
           "[megadrive]\nnombre = MEGA DRIVE\nnucleo = genesis_plus_gx_libretro.dll\ncarpeta = megadrive\nextensiones = md,gen,smd,bin,zip\n\n"
           "[mastersystem]\nnombre = MASTER SYSTEM\nnucleo = genesis_plus_gx_libretro.dll\ncarpeta = mastersystem\nextensiones = sms,zip\n\n"
           "[gb]\nnombre = GAME BOY\nnucleo = gambatte_libretro.dll\ncarpeta = gb\nextensiones = gb,gbc,zip\n\n"
           "[gba]\nnombre = GAME BOY ADVANCE\nnucleo = mgba_libretro.dll\ncarpeta = gba\nextensiones = gba,zip\n\n"
           "[pcengine]\nnombre = PC ENGINE\nnucleo = mednafen_pce_fast_libretro.dll\ncarpeta = pcengine\nextensiones = pce,cue,chd,zip\n\n"
           "[psx]\nnombre = PLAYSTATION\nnucleo = pcsx_rearmed_libretro.dll\ncarpeta = psx\nextensiones = cue,chd,pbp,m3u\n\n"
           "[mame]\nnombre = MAME\nnucleo = mame2003_plus_libretro.dll\ncarpeta = mame\nextensiones = zip\n";
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

void GameListModel::refilter()
{
    beginResetModel();
    applyFilter();
    endResetModel();
    emit filterChanged();
    emit countChanged();
}

void GameListModel::setSystem(const QString &s)
{
    if (s == m_system) return;
    m_system = s;
    refilter();
}

void GameListModel::setSearch(const QString &s)
{
    if (s == m_search) return;
    m_search = s;
    refilter();
}

void GameListModel::setHideBroken(bool v)
{
    if (v == m_hideBroken) return;
    m_hideBroken = v;
    refilter();
}

void GameListModel::setHideClones(bool v)
{
    if (v == m_hideClones) return;
    m_hideClones = v;
    markDuplicates(); // con el estado ✔/✘ de este momento
    refilter();
}

// De cada familia (original + clones) se queda una versión: la mejor por estado (✔, sin probar, ✘)
// y, a igualdad, la original antes que un clon. Las demás quedan marcadas como repetidas.
// Las versiones para 3 o más jugadores ("4 Players") forman su propia familia: se conserva una
// de ellas además de la versión normal, para no perder el modo de 4 jugadores.
void GameListModel::markDuplicates()
{
    auto familyOf = [](const Game &g) {
        return g.core + u'|' + (g.parent.isEmpty() ? g.rom : g.parent)
               + (g.players >= 3 ? u'|' + QString::number(g.players) : QString());
    };
    QHash<QString, int> best; // familia -> índice elegido
    auto rank = [this](const Game &g) { return m_status.value(g.rom, 0) * 2 + (g.parent.isEmpty() ? 1 : 0); };
    for (int i = 0; i < m_all.size(); ++i) {
        const Game &g = m_all.at(i);
        const QString family = familyOf(g);
        const auto it = best.constFind(family);
        if (it == best.cend() || rank(g) > rank(m_all.at(*it))) best.insert(family, i);
    }
    for (int i = 0; i < m_all.size(); ++i) {
        Game &g = m_all[i];
        g.duplicate = best.value(familyOf(g)) != i;
    }
}

void GameListModel::loadUserLists()
{
    m_favs.clear();
    m_stats.clear();
    QFile fav(m_base + QStringLiteral("/roms/favoritos.txt"));
    if (fav.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&fav);
        in.setEncoding(QStringConverter::Utf8);
        while (!in.atEnd()) {
            const QString rom = in.readLine().trimmed().toLower();
            if (!rom.isEmpty() && !rom.startsWith(u'#')) m_favs.insert(rom);
        }
    }
    QFile st(m_base + QStringLiteral("/roms/jugados.txt"));
    if (st.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&st);
        in.setEncoding(QStringConverter::Utf8);
        while (!in.atEnd()) {
            const QStringList p = in.readLine().trimmed().split(u'|');
            if (p.size() < 4 || p.at(0).startsWith(u'#')) continue;
            m_stats.insert(p.at(0).toLower(), Stat{ p.at(1).toInt(), p.at(2).toLongLong(), p.at(3).toLongLong() });
        }
    }
}

bool GameListModel::toggleFavorite(int row)
{
    if (row < 0 || row >= m_view.size()) return false;
    const QString rom = at(row).rom;
    const bool now = !m_favs.contains(rom);
    if (now) m_favs.insert(rom); else m_favs.remove(rom);

    QFile f(m_base + QStringLiteral("/roms/favoritos.txt"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        QTextStream out(&f);
        out.setEncoding(QStringConverter::Utf8);
        QStringList roms(m_favs.cbegin(), m_favs.cend());
        roms.sort();
        out << "# Juegos favoritos, uno por línea\n" << roms.join(u'\n') << '\n';
    }
    if (m_system == favoritesName()) refilter(); // al quitarlo desaparece de esta lista
    else emit dataChanged(index(row), index(row), { FavoriteRole });
    return now;
}

void GameListModel::notePlayed(const QString &rom, qint64 seconds)
{
    Stat &s = m_stats[rom];
    ++s.plays;
    s.secs += seconds;
    s.last = QDateTime::currentSecsSinceEpoch();

    QFile f(m_base + QStringLiteral("/roms/jugados.txt"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        QTextStream out(&f);
        out.setEncoding(QStringConverter::Utf8);
        out << "# rom|veces jugado|segundos|última vez (se actualiza solo)\n";
        QStringList roms = m_stats.keys();
        roms.sort();
        for (const QString &r : roms) {
            const Stat &v = m_stats[r];
            out << r << '|' << v.plays << '|' << v.secs << '|' << v.last << '\n';
        }
    }
    if (m_system == recentsName()) refilter();
    else
        for (int i = 0; i < m_view.size(); ++i)
            if (at(i).rom == rom) emit dataChanged(index(i), index(i), { PlaysRole, PlayTimeRole });
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
    const bool favs = m_system == favoritesName(), recents = m_system == recentsName();
    m_view.clear();
    for (int i = 0; i < m_all.size(); ++i) {
        const Game &g = m_all.at(i);
        if (favs) { if (!m_favs.contains(g.rom)) continue; }
        else if (recents) { if (m_stats.value(g.rom).last == 0) continue; }
        else if (!m_system.isEmpty() && g.system != m_system) continue;
        if (m_hideBroken && m_status.value(g.rom, 0) < 0) continue;
        if (m_hideClones && g.duplicate && !favs && !recents) continue;
        bool ok = true;
        for (const QString &w : words)
            if (!g.key.contains(w)) { ok = false; break; }
        if (ok) m_view.push_back(i);
    }
    if (recents) { // lo último jugado primero, solo los 30 más recientes
        std::sort(m_view.begin(), m_view.end(), [this](int a, int b) {
            return m_stats.value(m_all.at(a).rom).last > m_stats.value(m_all.at(b).rom).last;
        });
        if (m_view.size() > 30) m_view.resize(30);
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
    loadUserLists();

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
        if (it != m_meta.cend()) { g.system = it->system; g.parent = it->parent; g.players = it->players; }
        if (g.system.isEmpty() && rom.contains(u'-'))
            g.system = m_meta.value(rom.section(u'-', 0, 0)).system;
        if (g.system.isEmpty()) g.system = QStringLiteral("OTROS");
        g.key = (g.title + u' ' + g.rom).toLower();
        m_all.push_back(g);
    }

    // Otros sistemas (consolas, MAME…): roms/<carpeta>/ con el núcleo que diga cores/sistemas.ini.
    // El nombre interno lleva la carpeta ("snes/mario") para no chocar con otro sistema; así las
    // capturas van en media/snaps/snes/mario.png y los guardados en saves/snes/.
    loadSystemDefs();
    for (const SystemDef &d : m_defs) {
        QDir dir(m_base + QStringLiteral("/roms/") + d.folder);
        if (!dir.exists()) continue;
        QStringList filters;
        for (const QString &e : d.exts) filters << QStringLiteral("*.") + e;
        for (const QFileInfo &fi : dir.entryInfoList(filters, QDir::Files, QDir::Name)) {
            if (biosSet().contains(fi.completeBaseName().toLower())) continue; // BIOS de MAME, etc.
            Game g;
            g.rom = d.folder.toLower() + u'/' + fi.completeBaseName().toLower();
            g.path = fi.absoluteFilePath();
            g.title = fi.completeBaseName();
            g.system = d.name;
            g.core = d.core;
            const auto it = m_meta.constFind(g.rom); // títulos propios: "snes/mario|Super Mario World|1990|Nintendo"
            if (it != m_meta.cend()) {
                if (!it->title.isEmpty()) g.title = it->title;
                g.year = it->year; g.maker = it->maker;
            }
            g.key = (g.title + u' ' + fi.completeBaseName()).toLower();
            m_all.push_back(g);
        }
    }

    std::sort(m_all.begin(), m_all.end(), [](const Game &a, const Game &b) {
        return QString::compare(a.title, b.title, Qt::CaseInsensitive) < 0;
    });

    m_systems.clear();
    for (const Game &g : m_all)
        if (!m_systems.contains(g.system)) m_systems << g.system;
    m_systems.sort();
    m_systems.prepend(recentsName());
    m_systems.prepend(favoritesName());
    if (!m_systems.contains(m_system)) m_system.clear();
    markDuplicates();
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
    case CoreRole:    return g.core;
    case FavoriteRole: return m_favs.contains(g.rom);
    case PlaysRole:    return m_stats.value(g.rom).plays;
    case PlayTimeRole: return m_stats.value(g.rom).secs;
    case PlayersRole:  return g.players >= 3 ? g.players : 0;
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
        { ImageRole, "image" }, { MarqueeRole, "marquee" }, { StatusRole, "status" }, { SystemRole, "system" }, { CoreRole, "core" },
        { FavoriteRole, "favorite" }, { PlaysRole, "plays" }, { PlayTimeRole, "playTime" }, { PlayersRole, "players" },
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
