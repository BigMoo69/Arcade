#include "theme.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QUrl>

static const QStringList kImageFilters = { QStringLiteral("*.png"), QStringLiteral("*.jpg"),
                                           QStringLiteral("*.jpeg"), QStringLiteral("*.bmp") };

// Valores por defecto = tema "Arcade clásico". Todo tema parte de aquí y cambia lo que quiera.
static QVariantMap defaults()
{
    return {
        { QStringLiteral("accent"),   QStringLiteral("#ffcc00") }, // barra de selección
        { QStringLiteral("accent2"),  QStringLiteral("#ff3355") }, // números y títulos de panel
        { QStringLiteral("onAccent"), QStringLiteral("#000000") }, // texto sobre la barra de selección
        { QStringLiteral("text"),     QStringLiteral("#e8e8ff") },
        { QStringLiteral("dim"),      QStringLiteral("#7a7aa8") },
        { QStringLiteral("bg1"),      QStringLiteral("#0a0a2e") }, // degradado del fondo (arriba)
        { QStringLiteral("bg2"),      QStringLiteral("#000000") }, // degradado del fondo (abajo)
        { QStringLiteral("header1"),  QStringLiteral("#c00020") }, // encabezado (orillas)
        { QStringLiteral("header2"),  QStringLiteral("#ff3355") }, // encabezado (centro)
        { QStringLiteral("panel"),    QStringLiteral("#80000020") }, // relleno de los paneles (AARRGGBB)
        { QStringLiteral("border"),   QStringLiteral("#3040a0") },
        { QStringLiteral("grid"),     QStringLiteral("#4060ff") },
        { QStringLiteral("showGrid"), true },
        { QStringLiteral("darken"),   0.55 }, // cuánto se oscurece la imagen de fondo (0 = nada, 1 = negro)
    };
}

static QVariantMap with(std::initializer_list<std::pair<const char *, QVariant>> over)
{
    QVariantMap m = defaults();
    for (const auto &p : over) m.insert(QString::fromLatin1(p.first), p.second);
    return m;
}

Theme::Theme(const QString &baseDir, QObject *parent)
    : QObject(parent), m_base(baseDir), m_settings(baseDir + QStringLiteral("/arcade.ini"), QSettings::IniFormat)
{
    QDir(m_base).mkpath(QStringLiteral("themes"));
    QDir(m_base).mkpath(QStringLiteral("fondos"));
    writeExample();
    m_themeId = m_settings.value(QStringLiteral("ui/theme"), QStringLiteral("clasico")).toString();
    m_backgroundChoice = m_settings.value(QStringLiteral("ui/background")).toString();
    scan();
    apply();
}

void Theme::scan()
{
    m_themes = {
        { QStringLiteral("clasico"), QStringLiteral("ARCADE CLÁSICO"), {}, defaults() },
        { QStringLiteral("neon"), QStringLiteral("NEÓN"), {}, with({
              { "accent", "#00f0ff" }, { "accent2", "#ff2bd6" }, { "text", "#f0e8ff" }, { "dim", "#8a78b8" },
              { "bg1", "#12002e" }, { "bg2", "#000000" }, { "header1", "#5a00a0" }, { "header2", "#ff2bd6" },
              { "panel", "#80100030" }, { "border", "#7a30d0" }, { "grid", "#a040ff" } }) },
        { QStringLiteral("fosforo"), QStringLiteral("FÓSFORO VERDE"), {}, with({
              { "accent", "#7dff7d" }, { "accent2", "#20c040" }, { "text", "#c8ffc8" }, { "dim", "#4a9a5a" },
              { "bg1", "#001a08" }, { "bg2", "#000000" }, { "header1", "#004d1a" }, { "header2", "#10a040" },
              { "panel", "#80001a08" }, { "border", "#208040" }, { "grid", "#30ff60" } }) },
        { QStringLiteral("atardecer"), QStringLiteral("ATARDECER"), {}, with({
              { "accent", "#ffb347" }, { "accent2", "#ff5e62" }, { "text", "#fff0e0" }, { "dim", "#b08878" },
              { "bg1", "#2a0a1a" }, { "bg2", "#0a0208" }, { "header1", "#a02040" }, { "header2", "#ff7a45" },
              { "panel", "#80200810" }, { "border", "#b04a3a" }, { "grid", "#ff8050" } }) },
        { QStringLiteral("hielo"), QStringLiteral("HIELO"), {}, with({
              { "accent", "#ffffff" }, { "accent2", "#5ac8fa" }, { "text", "#e6f4ff" }, { "dim", "#7896b4" },
              { "bg1", "#06203a" }, { "bg2", "#01060c" }, { "header1", "#0a4a80" }, { "header2", "#3aa0e0" },
              { "panel", "#80041428" }, { "border", "#2a70b0" }, { "grid", "#60b0ff" } }) },
    };

    // Temas del usuario: themes/<carpeta>/theme.ini con líneas "clave = valor"
    const QDir themes(m_base + QStringLiteral("/themes"));
    for (const QFileInfo &d : themes.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        QFile f(d.absoluteFilePath() + QStringLiteral("/theme.ini"));
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        Entry e{ QStringLiteral("user:") + d.fileName(), d.fileName().toUpper(), d.absoluteFilePath(), defaults() };
        QTextStream in(&f);
        in.setEncoding(QStringConverter::Utf8);
        while (!in.atEnd()) {
            const QString line = in.readLine().trimmed();
            if (line.isEmpty() || line.startsWith(u';') || line.startsWith(u'[') || line.startsWith(QLatin1String("//"))) continue;
            const int eq = int(line.indexOf(u'='));
            if (eq <= 0) continue;
            const QString key = line.left(eq).trimmed();
            const QString val = line.mid(eq + 1).section(u';', 0, 0).trimmed(); // sin la nota del final
            if (key == QLatin1String("name")) e.name = val.toUpper();
            else if (key == QLatin1String("showGrid")) e.values.insert(key, val.compare(QLatin1String("false"), Qt::CaseInsensitive) != 0 && val != QLatin1String("0"));
            else if (key == QLatin1String("darken")) e.values.insert(key, qBound(0.0, val.toDouble(), 1.0));
            else if (key == QLatin1String("background")) e.values.insert(key, val);
            else if (e.values.contains(key) && QColor::isValidColorName(val)) e.values.insert(key, val);
        }
        m_themes.push_back(e);
    }

    m_backgrounds = QDir(m_base + QStringLiteral("/fondos")).entryList(kImageFilters, QDir::Files, QDir::Name);
}

int Theme::currentIndex() const
{
    for (int i = 0; i < m_themes.size(); ++i)
        if (m_themes.at(i).id == m_themeId) return i;
    return 0;
}

QString Theme::name() const { return m_themes.at(currentIndex()).name; }

QString Theme::backgroundName() const
{
    if (m_backgroundChoice == QLatin1String("-")) return QStringLiteral("NINGUNO");
    if (m_backgroundChoice.isEmpty()) return QStringLiteral("EL DEL TEMA");
    return QFileInfo(m_backgroundChoice).completeBaseName().toUpper();
}

void Theme::apply()
{
    const Entry &e = m_themes.at(currentIndex());
    m_colors = e.values;

    QString file;
    if (m_backgroundChoice.isEmpty()) {
        // El del tema: "background = archivo" en su theme.ini, o un background.* en su carpeta
        if (!e.dir.isEmpty()) {
            const QString named = e.values.value(QStringLiteral("background")).toString();
            if (!named.isEmpty() && QFileInfo::exists(e.dir + u'/' + named)) file = e.dir + u'/' + named;
            if (file.isEmpty()) {
                const QStringList found = QDir(e.dir).entryList({ QStringLiteral("background.*") }, QDir::Files);
                if (!found.isEmpty()) file = e.dir + u'/' + found.first();
            }
        }
    } else if (m_backgroundChoice != QLatin1String("-")) {
        const QString p = m_base + QStringLiteral("/fondos/") + m_backgroundChoice;
        if (QFileInfo::exists(p)) file = p;
    }
    m_backgroundUrl = file.isEmpty() ? QString() : QUrl::fromLocalFile(file).toString();
    emit changed();
}

void Theme::nextTheme(int direction)
{
    const int n = int(m_themes.size());
    m_themeId = m_themes.at(((currentIndex() + (direction < 0 ? -1 : 1)) % n + n) % n).id;
    m_settings.setValue(QStringLiteral("ui/theme"), m_themeId);
    apply();
}

void Theme::nextBackground(int direction)
{
    // Orden: el del tema → ninguno → cada imagen de fondos/
    QStringList choices = { QString(), QStringLiteral("-") };
    choices += m_backgrounds;
    const int n = int(choices.size());
    const int cur = qMax(0, int(choices.indexOf(m_backgroundChoice)));
    m_backgroundChoice = choices.at(((cur + (direction < 0 ? -1 : 1)) % n + n) % n);
    m_settings.setValue(QStringLiteral("ui/background"), m_backgroundChoice);
    apply();
}

void Theme::reload()
{
    scan();
    apply();
}

// Deja un tema de ejemplo comentado para que el usuario lo copie y lo edite
void Theme::writeExample() const
{
    const QString dir = m_base + QStringLiteral("/themes/ejemplo");
    if (QFileInfo::exists(dir + QStringLiteral("/theme.ini"))) return;
    QDir().mkpath(dir);
    QFile f(dir + QStringLiteral("/theme.ini"));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    out << "; ===== Tema de ejemplo para Arcade Multijuegos =====\n"
           "; Para crear tu propio tema: copia esta carpeta con otro nombre (por ejemplo themes/mi_tema),\n"
           "; cambia los valores y elige el tema en OPCIONES > TEMA. Las lineas que borres usan el valor clasico.\n"
           "; Colores en formato #RRGGBB (o #AARRGGBB para transparencia). Lineas que empiezan con ; son notas.\n"
           ";\n"
           "; Fondo: pon una imagen llamada background.png / .jpg / .bmp en esta misma carpeta,\n"
           "; o escribe su nombre en \"background\". Tambien puedes dejar imagenes sueltas en la carpeta fondos/\n"
           "; y elegirlas en OPCIONES > FONDO con cualquier tema.\n"
           "\n"
           "name = Ejemplo\n"
           "\n"
           "accent   = #ffcc00   ; barra de seleccion y resaltados\n"
           "accent2  = #ff3355   ; numeros de la lista y titulos de los paneles\n"
           "onAccent = #000000   ; texto encima de la barra de seleccion\n"
           "text     = #e8e8ff   ; texto normal\n"
           "dim      = #7a7aa8   ; texto secundario\n"
           "bg1      = #0a0a2e   ; fondo, parte de arriba\n"
           "bg2      = #000000   ; fondo, parte de abajo\n"
           "header1  = #c00020   ; encabezado, orillas\n"
           "header2  = #ff3355   ; encabezado, centro\n"
           "panel    = #80000020 ; relleno de los paneles (los dos primeros digitos son la transparencia)\n"
           "border   = #3040a0   ; borde de los paneles\n"
           "grid     = #4060ff   ; color de la rejilla del fondo\n"
           "showGrid = true      ; false para quitar la rejilla\n"
           "darken   = 0.55      ; cuanto se oscurece la imagen de fondo: 0 = nada, 1 = negro\n"
           ";background = mi_imagen.png\n";
}
