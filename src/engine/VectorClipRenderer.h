#pragma once

#include "core/VectorSource.h"
#include "engine/VectorPainter.h"

#include <QImage>
#include <QSize>

#include <memory>

// Renders a Vector clip (Lottie / SVG) for one instant. Parsed documents are cached by
// (document hash, slot overrides) and shared between the two compositor workers, the export
// thread and the thumbnail pool; each document renders under its own mutex because Skottie's
// Animation is not thread-safe. Skia-free header: FrameCompositor includes this without seeing
// Skia, and without DRIFT_WITH_SKIA every call returns nothing.

namespace drift::vec {

struct RenderRequest
{
    VectorSource source;
    // The clip's source time. VectorSource::startOffsetUs is added here, then the result is
    // folded by the source's loop mode against the parsed document's duration.
    TimeUs animUs = 0;
    QSize size; // layer size in device pixels; the document is fitted into it per source.fit
};

// Painter for the GPU path. Null when nothing should be drawn: Hide outside the animation, an
// unparsable document, or an empty size.
std::shared_ptr<const skia::VectorPainter> makePainter(const RenderRequest &request);

// CPU path, any thread. Premultiplied ARGB32 of request.size, null when nothing is drawn.
QImage renderToImage(const RenderRequest &request);

// A still for the bin and the timeline: the frame at `atFraction` of the animation, fitted into
// `size` per the source's fit mode.
QImage renderThumbnail(const VectorSource &source, const QSize &size, double atFraction = 0.35);

// Drop parsed documents (Android background, tests). Painters already built keep theirs.
void clearVectorDocumentCache();

} // namespace drift::vec
