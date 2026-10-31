#pragma once

#include <QObject>

// The horizontal time axis a Keyframes or subtitle lane panel shares between its ruler, lanes and
// gestures: seconds map to x as `seconds * pxPerSecond - contentX`.
class KeyframeTimeView : public QObject
{
    Q_OBJECT

    Q_PROPERTY(double pxPerSecond READ pxPerSecond WRITE setPxPerSecond NOTIFY pxPerSecondChanged)
    Q_PROPERTY(double contentX READ contentX WRITE setContentX NOTIFY contentXChanged)
    Q_PROPERTY(double viewportWidth READ viewportWidth WRITE setViewportWidth NOTIFY viewportWidthChanged)
    Q_PROPERTY(double clipStart READ clipStart WRITE setClipStart NOTIFY clipStartChanged)
    Q_PROPERTY(double clipDuration READ clipDuration WRITE setClipDuration NOTIFY clipDurationChanged)
    Q_PROPERTY(double minPxPerSecond READ minPxPerSecond CONSTANT)
    Q_PROPERTY(double maxPxPerSecond READ maxPxPerSecond CONSTANT)

public:
    explicit KeyframeTimeView(QObject *parent = nullptr);

    double pxPerSecond() const { return m_pxPerSecond; }
    double contentX() const { return m_contentX; }
    double viewportWidth() const { return m_viewportWidth; }
    double clipStart() const { return m_clipStart; }
    double clipDuration() const { return m_clipDuration; }
    double minPxPerSecond() const { return 1; }
    double maxPxPerSecond() const { return 5000; }

    void setPxPerSecond(double v);
    void setContentX(double v);
    void setViewportWidth(double v);
    void setClipStart(double v);
    void setClipDuration(double v);

    Q_INVOKABLE double xFor(double seconds) const;
    Q_INVOKABLE double secondsFor(double x) const;
    Q_INVOKABLE void zoomAt(double x, double factor);
    Q_INVOKABLE void fitRange(double s0, double s1);
    Q_INVOKABLE void fitClip();

signals:
    void pxPerSecondChanged();
    void contentXChanged();
    void viewportWidthChanged();
    void clipStartChanged();
    void clipDurationChanged();

private:
    double m_pxPerSecond = 100;
    double m_contentX = 0;
    double m_viewportWidth = 600;
    double m_clipStart = 0;
    double m_clipDuration = 1;
};
