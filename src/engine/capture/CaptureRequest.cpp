#include "engine/capture/CaptureRequest.h"

#include <QCoreApplication>

#include <algorithm>

namespace drift {

QString captureSourceToString(CaptureSource source)
{
    switch (source) {
    case CaptureSource::Window:
        return QStringLiteral("window");
    case CaptureSource::Region:
        return QStringLiteral("region");
    case CaptureSource::Camera:
        return QStringLiteral("camera");
    case CaptureSource::Screen:
        break;
    }
    return QStringLiteral("screen");
}

std::optional<CaptureSource> captureSourceFromString(const QString &text)
{
    if (text == QLatin1String("screen"))
        return CaptureSource::Screen;
    if (text == QLatin1String("window"))
        return CaptureSource::Window;
    if (text == QLatin1String("region"))
        return CaptureSource::Region;
    if (text == QLatin1String("camera"))
        return CaptureSource::Camera;
    return std::nullopt;
}

int clampCaptureFps(int fps)
{
    return std::clamp(fps, 1, 120);
}

std::optional<QRect> normalizeCaptureRegion(const QRect &region, const QSize &screen, int minimum)
{
    if (!region.isValid() || screen.isEmpty())
        return std::nullopt;

    const QRect bounds(QPoint(0, 0), screen);
    QRect clipped = region.intersected(bounds);
    if (clipped.isEmpty())
        return std::nullopt;

    // Round down, never up: growing a rectangle that already touches the screen edge would put it
    // back outside the bounds this just enforced.
    const int width = clipped.width() - (clipped.width() % 2);
    const int height = clipped.height() - (clipped.height() % 2);
    const int floor = std::max(2, minimum - (minimum % 2));
    if (width < floor || height < floor)
        return std::nullopt;

    return QRect(clipped.topLeft(), QSize(width, height));
}

QString captureRequestProblem(const CaptureRequest &request)
{
    if (request.outputPath.isEmpty()) {
        return QCoreApplication::translate("CaptureRequest",
                                           "There is nowhere to save this recording.");
    }

    switch (request.source) {
    case CaptureSource::Window:
        if (request.windowIndex < 0 && request.windowTitle.isEmpty()) {
            return QCoreApplication::translate("CaptureRequest",
                                               "Pick a window to record.");
        }
        break;
    case CaptureSource::Region:
        // The region is validated against its screen by the caller, which is the only side that
        // knows the screen's size; an empty one here means nothing was ever selected.
        if (request.region.isEmpty()) {
            return QCoreApplication::translate("CaptureRequest",
                                               "Drag out the area to record first.");
        }
        break;
    case CaptureSource::Screen:
    case CaptureSource::Camera:
        break;
    }

    return {};
}

} // namespace drift
