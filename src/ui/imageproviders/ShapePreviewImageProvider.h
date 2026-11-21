#pragma once

#include <QImage>
#include <QQuickImageProvider>

// A catalog shape drawn with its real default style through the shape painter, so the assets
// panel shows what lands on the timeline. Requested as image://shape/<catalogId>; the card is the
// requested size (112² when unspecified) and the shape sits centred at its catalog aspect.
// A "My shapes" entry is image://shape/user:<stem>?r=<revision>, the builtinShapes() `thumbnail`.
class ShapePreviewImageProvider : public QQuickImageProvider
{
public:
    ShapePreviewImageProvider();

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};
