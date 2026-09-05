#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantMap>

#include <array>

// Rolling playback counters, sampled as frames are composited and presented.
//
// These exist because "the preview stutters" has at least four unrelated causes in this
// pipeline, and a frame rate alone cannot tell them apart. The two that matter most are
// jitterMs (spread of the delivery interval) and deliveredFps against displayedFps: a
// throughput deficit shows up as both rates falling together, while a cadence problem shows
// up as a healthy delivered rate that the display never gets to show evenly.
//
// Everything is written from the GUI thread — composites report in on completion, presents
// come from the render loop's frameSwapped — so no locking is needed. Nothing here
// allocates on the hot path; the change signal is coalesced onto a timer so QML bindings do
// not re-evaluate per frame.
class PlaybackStats : public QObject
{
    Q_OBJECT

    Q_PROPERTY(double deliveredFps READ deliveredFps NOTIFY updated)
    Q_PROPERTY(double displayedFps READ displayedFps NOTIFY updated)
    Q_PROPERTY(double refreshRate READ refreshRate NOTIFY updated)
    Q_PROPERTY(double jitterMs READ jitterMs NOTIFY updated)
    Q_PROPERTY(double compositeMedianMs READ compositeMedianMs NOTIFY updated)
    Q_PROPERTY(double compositeP95Ms READ compositeP95Ms NOTIFY updated)
    Q_PROPERTY(double decodeWaitMedianMs READ decodeWaitMedianMs NOTIFY updated)
    Q_PROPERTY(double adaptiveScale READ adaptiveScale NOTIFY updated)
    Q_PROPERTY(int droppedFrames READ droppedFrames NOTIFY updated)
    Q_PROPERTY(int coalescedRequests READ coalescedRequests NOTIFY updated)
    Q_PROPERTY(int inFlightPeak READ inFlightPeak NOTIFY updated)
    Q_PROPERTY(QString uploadPath READ uploadPath NOTIFY updated)
    Q_PROPERTY(bool active READ isActive WRITE setActive NOTIFY activeChanged)

public:
    explicit PlaybackStats(QObject *parent = nullptr);

    // Samples kept per metric. At 60 fps this is a four second window — long enough for a
    // p95 to mean something, short enough that a fixed condition shows up promptly.
    static constexpr int kWindow = 240;

    // --- write side, all GUI thread ---

    // One completed composite. compositeMs is wall time from dispatch to delivery;
    // decodeWaitMs is how much of that was spent blocked on a decoder.
    void noteComposite(double compositeMs, double decodeWaitMs);
    // A composited frame actually handed to the scene graph.
    void noteDelivered();
    // A composited frame thrown away instead of shown.
    void noteDropped();
    // A composite request folded into one already pending.
    void noteCoalesced();
    // One display buffer swap.
    void noteDisplayed();
    void noteInFlight(int count);

    void setRefreshRate(double hz);
    void setAdaptiveScale(double scale);
    void setUploadPath(const QString &path);

    // Clears the windows. Called when playback starts so the previous run's numbers, and the
    // decoder-opening composites at the head of this one, do not colour the current window.
    void reset();

    // --- read side ---

    double deliveredFps() const;
    double displayedFps() const;
    double refreshRate() const { return m_refreshRate; }
    double jitterMs() const;
    double compositeMedianMs() const;
    double compositeP95Ms() const;
    double decodeWaitMedianMs() const;
    double adaptiveScale() const { return m_adaptiveScale; }
    int droppedFrames() const { return m_dropped; }
    int coalescedRequests() const { return m_coalesced; }
    int inFlightPeak() const { return m_inFlightPeak; }
    QString uploadPath() const { return m_uploadPath; }

    // Flat {label, value} rows for the diagnostics report, matching DebugReport's shape.
    QVariantList reportRows() const;

    // While inactive the counters still accumulate — they are nearly free — but the change
    // signal is not emitted, so no binding re-evaluates. The overlay turns this on.
    bool isActive() const { return m_active; }
    void setActive(bool active);

signals:
    void updated();
    void activeChanged();

private:
    // Fixed-size sample window. Overwrites oldest once full; no allocation after construction.
    class Window
    {
    public:
        void add(double value);
        void clear() { m_count = 0; m_next = 0; }
        int count() const { return m_count; }
        // Both sort a stack copy of the live samples, so they are safe to call at UI rates
        // but not per frame.
        double median() const { return percentile(0.5); }
        double percentile(double fraction) const;
        double stddev() const;
        double mean() const;

    private:
        int fill(std::array<double, kWindow> &out) const;
        std::array<double, kWindow> m_values{};
        int m_count = 0;
        int m_next = 0;
    };

    static double fpsFromIntervals(const Window &intervals);

    Window m_composite;
    Window m_decodeWait;
    Window m_deliveredIntervals;
    Window m_displayedIntervals;

    qint64 m_lastDeliveredNs = 0;
    qint64 m_lastDisplayedNs = 0;

    double m_refreshRate = 0.0;
    double m_adaptiveScale = 1.0;
    QString m_uploadPath;
    int m_dropped = 0;
    int m_coalesced = 0;
    int m_inFlightPeak = 0;
    bool m_active = false;

    // Coalesces `updated` to a UI-friendly rate: the underlying counters move at the frame
    // rate, and re-evaluating overlay bindings that often would itself cost frames.
    QTimer m_emitTimer;
};
