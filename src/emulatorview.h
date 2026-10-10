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
    // Efecto de monitor de tubo por shader: 0 = no, 1 = plano (líneas + máscara), 2 = curvo.
    // Con el efecto activo no se dibujan las scanlines simples.
    Q_PROPERTY(int crt READ crt WRITE setCrt NOTIFY crtChanged)
    // Rectángulo que ocupa la imagen del juego dentro del item (para dibujar marcos alrededor)
    Q_PROPERTY(QRectF contentRect READ contentRect NOTIFY contentRectChanged)
public:
    explicit EmulatorView(QQuickItem *parent = nullptr);

    static void setCore(LibretroCore *core) { s_core = core; }

    bool scanlines() const { return m_scanlines; }
    void setScanlines(bool on);
    bool smoothFilter() const { return m_smooth; }
    void setSmoothFilter(bool on);
    int aspectMode() const { return m_aspect; }
    void setAspectMode(int m);
    int crt() const { return m_crt; }
    void setCrt(int m);
    QRectF contentRect() const { return m_content; }
    // Posición del mouse sobre el item: se envía al juego como puntero (táctil / pistola / mouse)
    Q_INVOKABLE void pointerMoved(qreal x, qreal y);

signals:
    void scanlinesChanged();
    void smoothChanged();
    void aspectModeChanged();
    void crtChanged();
    void contentRectChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;

private:
    static QImage scanlineMask(int sourceLines);
    QSizeF fittedSize(const QImage &frame) const; // tamaño en pantalla de la imagen ya girada
    void updateContentRect();

    static LibretroCore *s_core;
    bool m_scanlines = true;
    bool m_smooth = false;
    int m_aspect = 0;
    int m_crt = 0;
    bool m_nodeIsCrt = false;
    QRectF m_content;
    int m_maskLines = 0;
};
