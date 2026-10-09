#include "emulatorview.h"
#include "libretrocore.h"

#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTransformNode>
#include <QSGTexture>

LibretroCore *EmulatorView::s_core = nullptr;

EmulatorView::EmulatorView(QQuickItem *parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    if (s_core)
        connect(s_core, &LibretroCore::frameReady, this, &QQuickItem::update);
}

void EmulatorView::setScanlines(bool on)
{
    if (on == m_scanlines) return;
    m_scanlines = on;
    emit scanlinesChanged();
    update();
}

void EmulatorView::setSmoothFilter(bool on)
{
    if (on == m_smooth) return;
    m_smooth = on;
    emit smoothChanged();
    update();
}

void EmulatorView::setAspectMode(int m)
{
    if (m == m_aspect) return;
    m_aspect = m;
    emit aspectModeChanged();
    update();
}

// Una fila oscura entre cada línea del juego. Se estira sobre la imagen con
// filtrado lineal, lo que da el típico degradado suave de un monitor CRT.
QImage EmulatorView::scanlineMask(int sourceLines)
{
    QImage img(1, sourceLines * 2, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < img.height(); ++y)
        img.setPixel(0, y, (y & 1) ? qPremultiply(qRgba(0, 0, 0, 120)) : 0u);
    return img;
}

QSGNode *EmulatorView::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
    auto *root = static_cast<QSGTransformNode *>(old);
    const QImage frame = s_core ? s_core->frame() : QImage();

    if (frame.isNull() || width() <= 0 || height() <= 0) {
        delete root;
        m_maskLines = 0;
        return nullptr;
    }

    QSGSimpleTextureNode *game = nullptr, *lines = nullptr;
    if (!root) {
        root = new QSGTransformNode;
        game = new QSGSimpleTextureNode;
        game->setOwnsTexture(true);
        lines = new QSGSimpleTextureNode;
        lines->setOwnsTexture(true);
        root->appendChildNode(game);
        root->appendChildNode(lines);
        m_maskLines = 0;
    } else {
        game  = static_cast<QSGSimpleTextureNode *>(root->firstChild());
        lines = static_cast<QSGSimpleTextureNode *>(game->nextSibling());
    }

    // Textura del cuadro actual
    game->setTexture(window()->createTextureFromImage(frame));
    game->setFiltering(m_smooth ? QSGTexture::Linear : QSGTexture::Nearest);

    // Rectángulo final que respeta la relación de aspecto
    const double ar = s_core->aspectRatio();
    double w = width(), h = width() / ar;
    if (h > height()) { h = height(); w = h * ar; }

    // Con rotación de 90/270 el rectángulo "sin rotar" tiene ancho y alto invertidos
    const int rot = s_core->rotation();
    const bool odd = rot & 1;
    if (m_aspect == 2) {
        w = width(); h = height();
    } else if (m_aspect == 1) {
        // Escala entera: cada línea del juego ocupa un número exacto de píxeles de la pantalla
        const double dpr = window()->effectiveDevicePixelRatio();
        const int srcLines = odd ? frame.width() : frame.height();
        const int k = int(h * dpr / srcLines);
        if (k >= 1) { h = k * srcLines / dpr; w = h * ar; }
    }
    const double rw = odd ? h : w, rh = odd ? w : h;
    const QRectF rect(-rw / 2, -rh / 2, rw, rh);
    game->setRect(rect);

    QTransform t;
    t.translate(width() / 2, height() / 2);
    t.rotate(-90.0 * rot); // libretro rota en sentido antihorario
    root->setMatrix(QMatrix4x4(t));

    // Scanlines (una por cada línea real del juego)
    if (m_maskLines != frame.height()) {
        m_maskLines = frame.height();
        lines->setTexture(window()->createTextureFromImage(scanlineMask(m_maskLines)));
        lines->setFiltering(QSGTexture::Linear);
    }
    lines->setRect(m_scanlines ? rect : QRectF());

    return root;
}
