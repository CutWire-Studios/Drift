#include "PreviewItem.h"

#include "playback/PlaybackEngine.h"

#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTexture>
#include <QScreen>

PreviewItem::PreviewItem(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    // The item has no window until it enters a scene, so the cadence hook-up cannot happen
    // here — it happens on every window change, including the first.
    connect(this, &QQuickItem::windowChanged, this, &PreviewItem::bindDisplayCadence);
}

void PreviewItem::setPlayback(PlaybackEngine *engine)
{
    if (m_playback == engine)
        return;

    if (m_playback)
        disconnect(m_playback, nullptr, this, nullptr);

    m_playback = engine;
    if (m_playback) {
        connect(m_playback, &PlaybackEngine::currentFrameChanged, this, &PreviewItem::pullFrame);
        pullFrame();
    }
    // The engine may arrive after the window does, so bind from both ends.
    bindDisplayCadence();
    emit playbackChanged();
}

void PreviewItem::bindDisplayCadence()
{
    QQuickWindow *win = window();
    if (m_cadenceWindow == win && m_afterAnimatingConn)
        return;

    disconnect(m_afterAnimatingConn);
    disconnect(m_frameSwappedConn);
    disconnect(m_screenChangedConn);
    m_cadenceWindow = win;

    if (!win || !m_playback) {
        // No display to follow. The engine falls back to its own timer, which is also what
        // headless tests and Android's offscreen paths get.
        if (m_playback)
            m_playback->setDisplayRefreshRate(0.0);
        return;
    }

    // afterAnimating is the one per-frame window signal Qt emits on the GUI thread, and it
    // lands at the right moment: animations have advanced, the scene has not yet been
    // synced, so a frame requested now is the one that should appear at the coming swap.
    m_afterAnimatingConn = connect(win, &QQuickWindow::afterAnimating, m_playback,
                                   &PlaybackEngine::onDisplayTick);
    // frameSwapped comes from the render thread, so it must be queued. It is only used to
    // count real presents — a rate the delivered-frame rate is compared against — which
    // survives the event-loop hop that scheduling would not.
    m_frameSwappedConn = connect(win, &QQuickWindow::frameSwapped, m_playback,
                                 &PlaybackEngine::onFrameSwapped, Qt::QueuedConnection);
    m_screenChangedConn =
        connect(win, &QWindow::screenChanged, this, [this](QScreen *) { reportRefreshRate(); });
    reportRefreshRate();
}

void PreviewItem::reportRefreshRate()
{
    if (!m_playback)
        return;
    const QQuickWindow *win = window();
    const QScreen *screen = win ? win->screen() : nullptr;
    m_playback->setDisplayRefreshRate(screen ? screen->refreshRate() : 0.0);
}

void PreviewItem::pullFrame()
{
    if (!m_playback)
        return;

    // Pull in C++: QML cannot reliably assign QImage between properties, which left
    // the Android preview permanently blank even when the compositor had frames.
    setTextureSize(m_playback->previewTextureSize());
    setTextureId(m_playback->previewTextureId());
    setImage(m_playback->previewImage());
}

void PreviewItem::setTextureId(int id)
{
    if (m_textureId == id)
        return;
    m_textureId = id;
    emit frameChanged();
    update();
}

void PreviewItem::setTextureSize(const QSize &size)
{
    if (m_textureSize == size)
        return;
    m_textureSize = size;
    emit frameChanged();
    update();
}

void PreviewItem::setImage(const QImage &image)
{
    // cacheKey changes whenever pixel data is replaced, even for same size.
    if (m_image.cacheKey() == image.cacheKey())
        return;
    m_image = image;
    if (!m_image.isNull() && m_textureSize.isEmpty())
        m_textureSize = m_image.size();
    emit frameChanged();
    update();
}

QSGNode *PreviewItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    auto *node = static_cast<QSGSimpleTextureNode *>(oldNode);

    const bool hasImage = !m_image.isNull();
    const bool hasNative = m_textureId != 0 && !m_textureSize.isEmpty();
    if ((!hasImage && !hasNative) || !window()) {
        delete node;
        m_boundTextureId = 0;
        m_boundTextureSize = {};
        m_boundImageCacheKey = 0;
        return nullptr;
    }

    if (!node)
        node = new QSGSimpleTextureNode();

    const QSize drawSize = hasImage ? m_image.size() : m_textureSize;
    const bool needNewWrapper = !node->texture()
        || (hasImage && m_boundImageCacheKey != m_image.cacheKey())
        || (!hasImage && (m_boundTextureId != m_textureId || m_boundTextureSize != m_textureSize));

    if (needNewWrapper) {
        QSGTexture *texture = nullptr;
        if (hasImage) {
            // Qt Quick's GLES path needs premultiplied ARGB32. RGBA8888 (and
            // TextureCanUseAtlas) silently produced a blank node on Android even when
            // the compositor handed over a valid QImage. The compositor already reads
            // back in this format, so the convert below normally does not run.
            QImage upload = m_image;
            if (upload.format() != QImage::Format_ARGB32_Premultiplied)
                upload = upload.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            texture = window()->createTextureFromImage(upload);
        } else {
            // Wraps the compositor's framebuffer texture — no copy, no upload. The GL
            // object stays owned by the engine's presentation ring, so the scene graph
            // must not take ownership of it; setOwnsTexture only frees this wrapper.
            //
            // TextureHasAlphaChannel is required, not cosmetic. Without it the wrapper
            // reports no alpha, QSGSimpleTextureNode picks QSGOpaqueTextureMaterial, and
            // on some Android/ANGLE drivers that material draws an externally created
            // texture as solid black. The canvas is opaque (cleared at alpha 1), so
            // declaring the channel only selects the blending material; it does not
            // change the pixels. The QImage branch above does not need it because
            // createTextureFromImage infers alpha from the image format.
            texture = QNativeInterface::QSGOpenGLTexture::fromNative(
                static_cast<GLuint>(m_textureId), window(), m_textureSize,
                QQuickWindow::TextureHasAlphaChannel);
        }
        if (!texture) {
            qWarning("PreviewItem: failed to create scene-graph texture (image=%dx%d id=%d)",
                     drawSize.width(), drawSize.height(), m_textureId);
            delete node;
            m_boundTextureId = 0;
            m_boundTextureSize = {};
            m_boundImageCacheKey = 0;
            return nullptr;
        }
        node->setTexture(texture);
        node->setOwnsTexture(true);
        node->setFiltering(QSGTexture::Linear);
        node->setTextureCoordinatesTransform(QSGSimpleTextureNode::NoTransform);
        m_boundTextureId = hasImage ? 0 : m_textureId;
        m_boundTextureSize = drawSize;
        m_boundImageCacheKey = hasImage ? m_image.cacheKey() : 0;
    }

    // No flip: the compositor promotes every source into its framebuffer such
    // that row 0 holds the image's top row (which is why toImage(false) comes out
    // upright), and the scene graph likewise samples v=0 at the top.

    const QRectF bounds = boundingRect();
    if (drawSize.isEmpty() || bounds.isEmpty()) {
        node->setRect(bounds);
        return node;
    }
    const qreal scale = qMin(bounds.width() / drawSize.width(),
                             bounds.height() / drawSize.height());
    const qreal drawW = drawSize.width() * scale;
    const qreal drawH = drawSize.height() * scale;
    const qreal x = bounds.x() + (bounds.width() - drawW) / 2.0;
    const qreal y = bounds.y() + (bounds.height() - drawH) / 2.0;
    node->setRect(QRectF(x, y, drawW, drawH));

    return node;
}
