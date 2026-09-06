#include "PlaybackStats.h"

#include <QCoreApplication>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace {

constexpr int kEmitIntervalMs = 250;

qint64 nowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

QVariantMap statRow(const QString &label, const QString &value)
{
    QVariantMap row;
    row.insert(QStringLiteral("label"), label);
    row.insert(QStringLiteral("value"), value);
    return row;
}

QString msText(double ms)
{
    if (ms <= 0.0)
        return QStringLiteral("—");
    return QStringLiteral("%1 ms").arg(ms, 0, 'f', 1);
}

QString hzText(double hz)
{
    if (hz <= 0.0)
        return QStringLiteral("—");
    return QStringLiteral("%1 Hz").arg(hz, 0, 'f', 1);
}

} // namespace

void PlaybackStats::Window::add(double value)
{
    m_values[m_next] = value;
    m_next = (m_next + 1) % kWindow;
    if (m_count < kWindow)
        ++m_count;
}

int PlaybackStats::Window::fill(std::array<double, kWindow> &out) const
{
    for (int i = 0; i < m_count; ++i)
        out[i] = m_values[i];
    return m_count;
}

double PlaybackStats::Window::percentile(double fraction) const
{
    if (m_count <= 0)
        return 0.0;
    std::array<double, kWindow> sorted{};
    const int n = fill(sorted);
    std::sort(sorted.begin(), sorted.begin() + n);
    const int index = std::clamp(static_cast<int>(std::lround(fraction * (n - 1))), 0, n - 1);
    return sorted[index];
}

double PlaybackStats::Window::mean() const
{
    if (m_count <= 0)
        return 0.0;
    double total = 0.0;
    for (int i = 0; i < m_count; ++i)
        total += m_values[i];
    return total / m_count;
}

double PlaybackStats::Window::stddev() const
{
    // One sample has no spread, and reporting 0 for it would read as "perfectly even"
    // rather than "nothing measured yet".
    if (m_count < 2)
        return 0.0;
    const double avg = mean();
    double sum = 0.0;
    for (int i = 0; i < m_count; ++i) {
        const double d = m_values[i] - avg;
        sum += d * d;
    }
    return std::sqrt(sum / (m_count - 1));
}

PlaybackStats::PlaybackStats(QObject *parent)
    : QObject(parent)
{
    m_emitTimer.setInterval(kEmitIntervalMs);
    connect(&m_emitTimer, &QTimer::timeout, this, &PlaybackStats::updated);
}

void PlaybackStats::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    if (m_active)
        m_emitTimer.start();
    else
        m_emitTimer.stop();
    emit activeChanged();
    // One immediate signal either way, so a freshly shown overlay paints real numbers
    // instead of waiting out the first interval.
    emit updated();
}

void PlaybackStats::noteComposite(double compositeMs, double decodeWaitMs)
{
    m_composite.add(compositeMs);
    m_decodeWait.add(decodeWaitMs);
}

void PlaybackStats::noteDelivered()
{
    const qint64 now = nowNs();
    if (m_lastDeliveredNs != 0)
        m_deliveredIntervals.add(double(now - m_lastDeliveredNs) / 1'000'000.0);
    m_lastDeliveredNs = now;
}

void PlaybackStats::noteDisplayed()
{
    const qint64 now = nowNs();
    if (m_lastDisplayedNs != 0)
        m_displayedIntervals.add(double(now - m_lastDisplayedNs) / 1'000'000.0);
    m_lastDisplayedNs = now;
}

void PlaybackStats::noteDropped()
{
    ++m_dropped;
}

void PlaybackStats::noteCoalesced()
{
    ++m_coalesced;
}

void PlaybackStats::noteInFlight(int count)
{
    m_inFlightPeak = qMax(m_inFlightPeak, count);
}

void PlaybackStats::setRefreshRate(double hz)
{
    m_refreshRate = hz > 0.0 ? hz : 0.0;
}

void PlaybackStats::setAdaptiveScale(double scale)
{
    m_adaptiveScale = scale;
}

void PlaybackStats::setUploadPath(const QString &path)
{
    m_uploadPath = path;
}

void PlaybackStats::reset()
{
    m_composite.clear();
    m_decodeWait.clear();
    m_deliveredIntervals.clear();
    m_displayedIntervals.clear();
    m_lastDeliveredNs = 0;
    m_lastDisplayedNs = 0;
    m_dropped = 0;
    m_coalesced = 0;
    m_inFlightPeak = 0;
    emit updated();
}

double PlaybackStats::fpsFromIntervals(const Window &intervals)
{
    const double avgMs = intervals.mean();
    if (avgMs <= 0.0)
        return 0.0;
    return 1000.0 / avgMs;
}

double PlaybackStats::deliveredFps() const
{
    return fpsFromIntervals(m_deliveredIntervals);
}

double PlaybackStats::displayedFps() const
{
    return fpsFromIntervals(m_displayedIntervals);
}

double PlaybackStats::jitterMs() const
{
    return m_deliveredIntervals.stddev();
}

double PlaybackStats::compositeMedianMs() const
{
    return m_composite.median();
}

double PlaybackStats::compositeP95Ms() const
{
    return m_composite.percentile(0.95);
}

double PlaybackStats::decodeWaitMedianMs() const
{
    return m_decodeWait.median();
}

QVariantList PlaybackStats::reportRows() const
{
    const auto tr = [](const char *text) {
        return QCoreApplication::translate("PlaybackStats", text);
    };

    QVariantList rows;
    rows.append(statRow(tr("Delivered frames"), hzText(deliveredFps())));
    rows.append(statRow(tr("Displayed frames"), hzText(displayedFps())));
    rows.append(statRow(tr("Display refresh"), hzText(refreshRate())));
    rows.append(statRow(tr("Delivery jitter"), msText(jitterMs())));
    rows.append(statRow(tr("Composite (median)"), msText(compositeMedianMs())));
    rows.append(statRow(tr("Composite (p95)"), msText(compositeP95Ms())));
    rows.append(statRow(tr("Decode wait (median)"), msText(decodeWaitMedianMs())));
    rows.append(statRow(tr("Preview scale"),
                        QStringLiteral("%1%").arg(m_adaptiveScale * 100.0, 0, 'f', 0)));
    rows.append(statRow(tr("Frames dropped"), QString::number(m_dropped)));
    rows.append(statRow(tr("Requests coalesced"), QString::number(m_coalesced)));
    rows.append(statRow(tr("Composites in flight (peak)"), QString::number(m_inFlightPeak)));
    rows.append(statRow(tr("Preview upload"),
                        m_uploadPath.isEmpty() ? QStringLiteral("—") : m_uploadPath));
    rows.append(statRow(tr("Samples"), QString::number(m_composite.count())));
    return rows;
}
