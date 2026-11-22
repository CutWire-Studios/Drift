#pragma once

#include <QObject>
#include <QPointF>

namespace drift::manip {

// Lazy-string stabiliser for a brush: the head trails the cursor on a string of fixed length and
// only moves once the cursor pulls it taut, so small hand tremor never reaches the stroke.
class StrokeSmoother : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QPointF head READ head NOTIFY headChanged)

public:
    // The string length, in the caller's pixels, at full smoothing.
    static constexpr double kMaxStringPx = 80.0;

    explicit StrokeSmoother(QObject *parent = nullptr);

    QPointF head() const { return m_head; }

    Q_INVOKABLE void begin(double x, double y);
    // Pulls the head toward (x, y) and returns it. `smoothing` is 0..1; 0 returns the cursor itself.
    Q_INVOKABLE QPointF follow(double x, double y, double smoothing);

signals:
    void headChanged();

private:
    QPointF m_head;
};

} // namespace drift::manip
