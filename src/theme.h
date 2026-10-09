#pragma once
#include <QObject>
#include <QSettings>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

// Apariencia del menú: temas de color y fondo de pantalla.
//  - Temas integrados (definidos en theme.cpp) + temas del usuario en themes/<carpeta>/theme.ini
//  - Fondos: cualquier imagen en fondos/ (png, jpg, bmp); un tema puede traer el suyo
// La elección se guarda en arcade.ini (ui/theme, ui/background).
class Theme : public QObject
{
    Q_OBJECT
    // Colores y ajustes del tema activo: accent, accent2, text, dim, bg1, bg2, header1, header2,
    // panel, border, grid, onAccent, showGrid (bool), darken (0..1)
    Q_PROPERTY(QVariantMap c READ colors NOTIFY changed)
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(QString background READ background NOTIFY changed)         // URL de la imagen, o ""
    Q_PROPERTY(QString backgroundName READ backgroundName NOTIFY changed) // texto para el menú
public:
    Theme(const QString &baseDir, QObject *parent = nullptr);

    QVariantMap colors() const { return m_colors; }
    QString name() const;
    QString background() const { return m_backgroundUrl; }
    QString backgroundName() const;

    Q_INVOKABLE void nextTheme(int direction = 1);
    Q_INVOKABLE void nextBackground(int direction = 1);
    Q_INVOKABLE void reload(); // vuelve a leer themes/ y fondos/ (tras copiar archivos nuevos)

signals:
    void changed();

private:
    struct Entry { QString id, name, dir; QVariantMap values; }; // dir vacío = integrado

    void scan();
    void apply();
    void writeExample() const;
    int currentIndex() const;

    QString m_base;
    QSettings m_settings;
    QVector<Entry> m_themes;
    QStringList m_backgrounds;   // nombres de archivo en fondos/
    QString m_themeId;           // id del tema elegido
    QString m_backgroundChoice;  // "" = el del tema, "-" = ninguno, o nombre de archivo en fondos/
    QVariantMap m_colors;
    QString m_backgroundUrl;
};
