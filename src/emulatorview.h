#pragma once
#include <QQuickItem>
#include <QImage>

class LibretroCore;

// Muestra la imagen del juego escalada por GPU, con la relación de aspecto
// correcta, rotación para juegos verticales y scanlines opcionales estilo CRT.
class EmulatorView : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(bool scanlines READ scanlines WRITE setScanlines NOTIFY scanlinesChanged)
    Q_PROPERTY(bool smooth READ smoothFilter WRITE setSmoothFilter NOTIFY smoothChanged)
    // 0 = proporción original, 1 = escala entera (píxeles exactos), 2 = estirar a toda la pantalla
    Q_PROPERTY(int aspectMode READ aspectMode WRITE setAspectMode NOTIFY aspectModeChanged)
public:
    explicit EmulatorView(QQuickItem *parent = nullptr);

    static void setCore(LibretroCore *core) { s_core = core; }

    bool scanlines() const { return m_scanlines; }
    void setScanlines(bool on);
    bool smoothFilter() const { return m_smooth; }
    void setSmoothFilter(bool on);
    int aspectMode() const { return m_aspect; }
    void setAspectMode(int m);

signals:
    void scanlinesChanged();
    void smoothChanged();
    void aspectModeChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;

private:
    static QImage scanlineMask(int sourceLines);

    static LibretroCore *s_core;
    bool m_scanlines = true;
    bool m_smooth = false;
    int m_aspect = 0;
    int m_maskLines = 0;
};
