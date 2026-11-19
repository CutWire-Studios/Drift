#include "app/platform/PlatformDrag.h"

#include <QDrag>
#include <QGuiApplication>
#include <QMimeData>
#include <QPointer>

namespace drift {

PlatformDrag::PlatformDrag(QObject *parent)
    : QObject(parent)
{
}

void PlatformDrag::start(const QVariantMap &mimeData, QPointF hotSpot)
{
    if (m_active)
        return;
    m_active = true;
    QMetaObject::invokeMethod(this, [this, mimeData, hotSpot] { exec(mimeData, hotSpot); },
                              Qt::QueuedConnection);
}

void PlatformDrag::exec(const QVariantMap &mimeData, QPointF hotSpot)
{
    // The release can be delivered before this queued call; xcb and Wayland would then hold the
    // drag until the next click.
    if (QGuiApplication::mouseButtons() & Qt::LeftButton) {
        auto *mime = new QMimeData;
        for (auto it = mimeData.cbegin(); it != mimeData.cend(); ++it) {
            if (it.key() == QLatin1String("text/plain"))
                mime->setText(it.value().toString());
            else
                mime->setData(it.key(), it.value().toString().toUtf8());
        }
        auto *drag = new QDrag(this);
        drag->setMimeData(mime);
        drag->setHotSpot(hotSpot.toPoint());
        const QPointer<QDrag> guard(drag);
        drag->exec(Qt::CopyAction);
        // Some platforms delete the drag themselves; deleteLater on one already queued is harmless.
        if (guard)
            guard->deleteLater();
    }
    m_active = false;
}

} // namespace drift
