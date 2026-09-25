#include "TimelineViewState.h"

TimelineViewState::TimelineViewState(QObject *parent)
    : QObject(parent)
{
}

void TimelineViewState::setViewX(double x)
{
    if (qFuzzyCompare(m_viewX, x))
        return;
    m_viewX = x;
    emit viewChanged();
}

void TimelineViewState::setViewW(double w)
{
    if (qFuzzyCompare(m_viewW, w))
        return;
    m_viewW = w;
    emit viewChanged();
}

void TimelineViewState::setPxPerSecond(double pps)
{
    if (pps <= 0.0 || qFuzzyCompare(m_pxPerSecond, pps))
        return;
    m_pxPerSecond = pps;
    emit viewChanged();
}

void TimelineViewState::setTouchMode(bool touch)
{
    if (m_touchMode == touch)
        return;
    m_touchMode = touch;
    emit viewChanged();
}

void TimelineViewState::setStyle(const QVariantMap &style)
{
    if (m_styleMap == style)
        return;
    m_styleMap = style;

    const auto color = [&style](const char *key, QColor &out) {
        const QVariant v = style.value(QLatin1String(key));
        if (v.isValid())
            out = v.value<QColor>();
    };
    const auto real = [&style](const char *key, double &out) {
        const QVariant v = style.value(QLatin1String(key));
        if (v.isValid())
            out = v.toDouble();
    };
    const auto text = [&style](const char *key, QString &out) {
        const QVariant v = style.value(QLatin1String(key));
        if (v.isValid())
            out = v.toString();
    };

    color("clipVideo", m_style.clipVideo);
    color("clipAudio", m_style.clipAudio);
    color("clipText", m_style.clipText);
    color("clipSubtitle", m_style.clipSubtitle);
    color("clipGraphic", m_style.clipGraphic);
    color("clipEffect", m_style.clipEffect);
    color("clipComposite", m_style.clipComposite);
    color("adjustmentVideo", m_style.adjustmentVideo);
    color("adjustmentAudio", m_style.adjustmentAudio);
    color("adjustmentMask", m_style.adjustmentMask);
    color("primary", m_style.primary);
    color("scrim", m_style.scrim);
    color("proxyBand", m_style.proxyBand);
    color("onMedia", m_style.onMedia);
    color("waveform", m_style.waveform);
    color("mutedForeground", m_style.mutedForeground);
    color("panelBorder", m_style.panelBorder);
    color("proxyPill", m_style.proxyPill);
    color("proxyPillForeground", m_style.proxyPillForeground);
    color("editFriendlyPill", m_style.editFriendlyPill);
    color("editFriendlyPillForeground", m_style.editFriendlyPillForeground);
    color("warning", m_style.warning);
    text("fontFamily", m_style.fontFamily);
    real("fontSizeTiny", m_style.fontSizeTiny);
    real("fontSizeXs", m_style.fontSizeXs);
    real("radiusSm", m_style.radiusSm);
    real("radiusXs", m_style.radiusXs);
    real("ringWidth", m_style.ringWidth);
    real("borderWidthFocus", m_style.borderWidthFocus);
    real("headerBandHeight", m_style.headerBandHeight);
    real("clipMinWidth", m_style.clipMinWidth);
    real("iconSizeSm", m_style.iconSizeSm);
    real("spacingLg", m_style.spacingLg);
    real("spacingMd", m_style.spacingMd);
    real("edgeMarginDesktop", m_style.edgeMarginDesktop);
    real("edgeMarginTouch", m_style.edgeMarginTouch);
    real("trimHotspotExtraDesktop", m_style.trimHotspotExtraDesktop);
    real("trimHotspotExtraTouch", m_style.trimHotspotExtraTouch);
    text("proxyLabel", m_style.proxyLabel);
    text("proxyTooltip", m_style.proxyTooltip);
    text("editFriendlyLabel", m_style.editFriendlyLabel);
    text("editFriendlyTooltip", m_style.editFriendlyTooltip);
    text("vfrTooltip", m_style.vfrTooltip);
    emit styleChanged();
}
