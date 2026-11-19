#pragma once

#include <QObject>
#include <QPointF>
#include <QVariantMap>

// Starts the desktop asset drags in place of Drag.Automatic. QQuickDragAttached runs QDrag::exec
// on the dragged item's attached object, parents the QDrag to it, and touches both once exec
// returns; a drop that rebuilds the browser model destroys that delegate inside exec, and the
// return lands on freed memory. Here the QDrag belongs to this long-lived object, and exec is
// queued so it does not run inside the delegate's DragHandler signal either.
namespace drift {

class PlatformDrag : public QObject
{
    Q_OBJECT

public:
    explicit PlatformDrag(QObject *parent = nullptr);

    // mimeData maps a mime type to its string value, as Drag.mimeData did. Copy action only.
    Q_INVOKABLE void start(const QVariantMap &mimeData, QPointF hotSpot);

private:
    void exec(const QVariantMap &mimeData, QPointF hotSpot);

    bool m_active = false;
};

} // namespace drift
