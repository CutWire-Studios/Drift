#include "app/editor/sessions/KeyframeTimeView.h"

#include <algorithm>

KeyframeTimeView::KeyframeTimeView(QObject *parent) : QObject(parent) {}

void KeyframeTimeView::setPxPerSecond(double v)
{
    if (m_pxPerSecond == v)
        return;
    m_pxPerSecond = v;
    emit pxPerSecondChanged();
}

void KeyframeTimeView::setContentX(double v)
{
    if (m_contentX == v)
        return;
    m_contentX = v;
    emit contentXChanged();
}

void KeyframeTimeView::setViewportWidth(double v)
{
    if (m_viewportWidth == v)
        return;
    m_viewportWidth = v;
    emit viewportWidthChanged();
}

void KeyframeTimeView::setClipStart(double v)
{
    if (m_clipStart == v)
        return;
    m_clipStart = v;
    emit clipStartChanged();
}

void KeyframeTimeView::setClipDuration(double v)
{
    if (m_clipDuration == v)
        return;
    m_clipDuration = v;
    emit clipDurationChanged();
}

double KeyframeTimeView::xFor(double seconds) const
{
    return seconds * m_pxPerSecond - m_contentX;
}

double KeyframeTimeView::secondsFor(double x) const
{
    return (x + m_contentX) / m_pxPerSecond;
}

void KeyframeTimeView::zoomAt(double x, double factor)
{
    const double anchor = secondsFor(x);
    setPxPerSecond(std::clamp(m_pxPerSecond * factor, minPxPerSecond(), maxPxPerSecond()));
    setContentX(anchor * m_pxPerSecond - x);
}

void KeyframeTimeView::fitRange(double s0, double s1)
{
    const double span = std::max(0.05, s1 - s0);
    const double padded = span * 1.1;
    setPxPerSecond(std::clamp(std::max(1.0, m_viewportWidth) / padded, minPxPerSecond(), maxPxPerSecond()));
    setContentX((s0 - span * 0.05) * m_pxPerSecond);
}

void KeyframeTimeView::fitClip()
{
    fitRange(m_clipStart, m_clipStart + m_clipDuration);
}
