#pragma once

#include "core/Clip.h"

#include <QImage>
#include <QQuickImageProvider>

// Renders a style pack's preview card through the real text painter, so the picker shows exactly
// what the compositor will draw — accents included. Uses each pack's sampleText. Requested as
// image://textstyle/<presetId>.
class TextStylePreviewImageProvider : public QQuickImageProvider
{
public:
    TextStylePreviewImageProvider();

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};

// One frame of a text clip drawn on the CPU into a card of `size`: the shared piece of every
// text thumbnail provider. `clipTimeUs` picks the animation instant (-1 = the hold pose).
QImage renderTextCard(const drift::Clip &clip, const QString &text, const QSize &size, double renderScale,
                      drift::TimeUs clipTimeUs = -1, int activeWordIndex = -1);

// A preset's animation as a sprite grid (columns × rows of frames), for the In / Out / Loop
// galleries: image://textanim/<slot>/<presetId>?w=104&h=58&frames=24. Style-agnostic, so a
// strip never invalidates.
class TextAnimPreviewImageProvider : public QQuickImageProvider
{
public:
    TextAnimPreviewImageProvider();

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};

// A look rendered with the user's own font and words:
// image://textlook/<lookId>?text=…&font=…&weight=…&italic=0|1&rev=n.
class TextLookPreviewImageProvider : public QQuickImageProvider
{
public:
    TextLookPreviewImageProvider();

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};
