#include "emulatorview.h"
#include "libretrocore.h"

#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTransformNode>
#include <QSGTexture>
#include <QSGGeometryNode>
#include <QSGMaterial>
#include <QSGMaterialShader>
#include <QSGRendererInterface>
#include <cstring>

// ---------------------------------------------------------------- efecto CRT (shaders/crt.*)
namespace {

class CrtMaterial : public QSGMaterial
{
public:
    ~CrtMaterial() override { delete texture; }
    QSGMaterialType *type() const override { static QSGMaterialType t; return &t; }
    QSGMaterialShader *createShader(QSGRendererInterface::RenderMode) const override;
    int compare(const QSGMaterial *other) const override { return this == other ? 0 : (this < other ? -1 : 1); }

    QSGTexture *texture = nullptr;
    float curvature = 0, scanline = 0, mask = 0;
    QSizeF sourceSize, outputSize;
};

class CrtShader : public QSGMaterialShader
{
public:
    CrtShader()
    {
        setShaderFileName(VertexStage, QStringLiteral(":/shaders/crt.vert.qsb"));
        setShaderFileName(FragmentStage, QStringLiteral(":/shaders/crt.frag.qsb"));
    }
    // Bloque std140: mat4 (0), opacidad (64), curvatura (68), líneas (72), máscara (76), vec2 (80), vec2 (88)
    bool updateUniformData(RenderState &state, QSGMaterial *newMaterial, QSGMaterial *) override
    {
        auto *m = static_cast<CrtMaterial *>(newMaterial);
        char *buf = state.uniformData()->data();
        const QMatrix4x4 mvp = state.combinedMatrix();
        memcpy(buf, mvp.constData(), 64);
        const float f[4] = { state.opacity(), m->curvature, m->scanline, m->mask };
        memcpy(buf + 64, f, 16);
        const float sz[4] = { float(m->sourceSize.width()), float(m->sourceSize.height()),
                              float(m->outputSize.width()), float(m->outputSize.height()) };
        memcpy(buf + 80, sz, 16);
        return true;
    }
    void updateSampledImage(RenderState &state, int, QSGTexture **texture, QSGMaterial *newMaterial, QSGMaterial *) override
    {
        auto *m = static_cast<CrtMaterial *>(newMaterial);
        m->texture->commitTextureOperations(state.rhi(), state.resourceUpdateBatch());
        *texture = m->texture;
    }
};

QSGMaterialShader *CrtMaterial::createShader(QSGRendererInterface::RenderMode) const { return new CrtShader; }

class CrtNode : public QSGGeometryNode
{
public:
    CrtNode() : m_geometry(QSGGeometry::defaultAttributes_TexturedPoint2D(), 4)
    {
        setGeometry(&m_geometry);
        setMaterial(&material);
    }
    void setRect(const QRectF &r)
    {
        QSGGeometry::updateTexturedRectGeometry(&m_geometry, r, QRectF(0, 0, 1, 1));
        markDirty(DirtyGeometry);
    }
    CrtMaterial material;
private:
    QSGGeometry m_geometry;
};

} // namespace

LibretroCore *EmulatorView::s_core = nullptr;

EmulatorView::EmulatorView(QQuickItem *parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    if (s_core)
        connect(s_core, &LibretroCore::frameReady, this, [this] { updateContentRect(); update(); });
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
    updateContentRect();
    update();
}

void EmulatorView::setCrt(int m)
{
    if (m == m_crt) return;
    m_crt = m;
    emit crtChanged();
    update();
}

void EmulatorView::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    updateContentRect();
}

// Tamaño en pantalla de la imagen del juego (ya girada), según el modo de imagen
QSizeF EmulatorView::fittedSize(const QImage &frame) const
{
    const double ar = s_core->aspectRatio();
    double w = width(), h = width() / ar;
    if (h > height()) { h = height(); w = h * ar; }
    if (m_aspect == 2) {
        w = width(); h = height();
    } else if (m_aspect == 1 && window()) {
        // Escala entera: cada línea del juego ocupa un número exacto de píxeles de la pantalla
        const double dpr = window()->effectiveDevicePixelRatio();
        const int srcLines = (s_core->rotation() & 1) ? frame.width() : frame.height();
        const int k = int(h * dpr / srcLines);
        if (k >= 1) { h = k * srcLines / dpr; w = h * ar; }
    }
    return { w, h };
}

void EmulatorView::updateContentRect()
{
    QRectF r;
    const QImage frame = s_core ? s_core->frame() : QImage();
    if (!frame.isNull() && width() > 0 && height() > 0) {
        const QSizeF s = fittedSize(frame);
        r = QRectF((width() - s.width()) / 2, (height() - s.height()) / 2, s.width(), s.height());
    }
    if (r == m_content) return;
    m_content = r;
    emit contentRectChanged();
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

    // El efecto CRT necesita shaders: con el dibujado por software se usan las scanlines simples
    const bool crt = m_crt > 0
        && window()->rendererInterface()->graphicsApi() != QSGRendererInterface::Software;
    if (root && crt != m_nodeIsCrt) { delete root; root = nullptr; } // cambia el tipo de nodo: se rehace

    QSGSimpleTextureNode *game = nullptr, *lines = nullptr;
    CrtNode *tube = nullptr;
    if (!root) {
        root = new QSGTransformNode;
        if (crt) {
            auto *n = new CrtNode;
            n->setFlag(QSGNode::OwnedByParent);
            root->appendChildNode(n);
        } else {
            game = new QSGSimpleTextureNode;
            game->setOwnsTexture(true);
            root->appendChildNode(game);
        }
        lines = new QSGSimpleTextureNode;
        lines->setOwnsTexture(true);
        root->appendChildNode(lines);
        m_maskLines = 0;
        m_nodeIsCrt = crt;
    }
    if (crt) tube = static_cast<CrtNode *>(root->firstChild());
    else     game = static_cast<QSGSimpleTextureNode *>(root->firstChild());
    lines = static_cast<QSGSimpleTextureNode *>(root->firstChild()->nextSibling());

    // Con rotación de 90/270 el rectángulo "sin rotar" tiene ancho y alto invertidos
    const QSizeF fit = fittedSize(frame);
    const double w = fit.width(), h = fit.height();
    const int rot = s_core->rotation();
    const bool odd = rot & 1;
    const double rw = odd ? h : w, rh = odd ? w : h;
    const QRectF rect(-rw / 2, -rh / 2, rw, rh);

    // Textura del cuadro actual
    QSGTexture *tex = window()->createTextureFromImage(frame);
    if (crt) {
        CrtMaterial &m = tube->material;
        delete m.texture;
        m.texture = tex;
        tex->setFiltering(QSGTexture::Linear);
        m.curvature = m_crt >= 2 ? 1.0f : 0.0f;
        m.scanline = 0.85f;
        m.mask = 0.22f;
        m.sourceSize = frame.size();
        m.outputSize = QSizeF(rw, rh) * window()->effectiveDevicePixelRatio();
        tube->setRect(rect);
        tube->markDirty(QSGNode::DirtyMaterial);
    } else {
        game->setTexture(tex);
        game->setFiltering(m_smooth ? QSGTexture::Linear : QSGTexture::Nearest);
        game->setRect(rect);
    }

    QTransform t;
    t.translate(width() / 2, height() / 2);
    t.rotate(-90.0 * rot); // libretro rota en sentido antihorario
    root->setMatrix(QMatrix4x4(t));

    // Scanlines simples (una por cada línea real del juego); el efecto CRT trae las suyas
    if (m_maskLines != frame.height()) {
        m_maskLines = frame.height();
        lines->setTexture(window()->createTextureFromImage(scanlineMask(m_maskLines)));
        lines->setFiltering(QSGTexture::Linear);
    }
    lines->setRect(m_scanlines && !crt ? rect : QRectF());

    return root;
}
