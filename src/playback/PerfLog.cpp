#include "PerfLog.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHash>
#include <QMutex>
#include <QTimer>
#include <QVector>
#include <algorithm>

#ifdef Q_OS_ANDROID
#include <sys/system_properties.h>
#endif

namespace drift::perf {

namespace {

QMutex &samplesMutex()
{
    static QMutex mutex;
    return mutex;
}

QHash<QByteArray, QVector<double>> &samples()
{
    static QHash<QByteArray, QVector<double>> map;
    return map;
}

double percentile(const QVector<double> &sorted, double p)
{
    const qsizetype index = std::min<qsizetype>(sorted.size() - 1, qsizetype(p * double(sorted.size())));
    return sorted.at(index);
}

void flush()
{
    QHash<QByteArray, QVector<double>> taken;
    {
        QMutexLocker lock(&samplesMutex());
        taken.swap(samples());
    }
    for (auto it = taken.begin(); it != taken.end(); ++it) {
        QVector<double> &v = it.value();
        std::sort(v.begin(), v.end());
        double sum = 0.0;
        int over16 = 0;
        int over33 = 0;
        for (double ms : std::as_const(v)) {
            sum += ms;
            over16 += ms > 16.7 ? 1 : 0;
            over33 += ms > 33.4 ? 1 : 0;
        }
        qWarning("DriftPerf %s n=%lld p50=%.2f p95=%.2f max=%.2f sum=%.1f over16=%d over33=%d",
              it.key().constData(), static_cast<long long>(v.size()), percentile(v, 0.5),
              percentile(v, 0.95), v.constLast(), sum, over16, over33);
    }
}

} // namespace

bool enabled()
{
    static const bool on = [] {
        if (!qEnvironmentVariableIsEmpty("DRIFT_PERF_LOG"))
            return true;
#ifdef Q_OS_ANDROID
        char value[PROP_VALUE_MAX] = {};
        if (__system_property_get("debug.drift.perf", value) > 0)
            return value[0] == '1';
#endif
        return false;
    }();
    return on;
}

void install()
{
    if (!enabled())
        return;
    auto *timer = new QTimer(qApp);
    timer->setInterval(1000);
    QObject::connect(timer, &QTimer::timeout, qApp, &flush);
    timer->start();

    // How late a 16 ms timer fires is how long the GUI thread was busy with something else — the
    // time it could not have synchronised a frame.
    auto *stall = new QTimer(qApp);
    stall->setTimerType(Qt::PreciseTimer);
    stall->setInterval(16);
    auto *since = new QElapsedTimer;
    since->start();
    QObject::connect(stall, &QTimer::timeout, qApp, [since] {
        const double lateMs = double(since->nsecsElapsed()) / 1'000'000.0 - 16.0;
        since->start();
        if (lateMs > 4.0)
            record("gui.stall", lateMs);
    });
    stall->start();
    qWarning("DriftPerf enabled");
}

void record(const char *name, double ms)
{
    QMutexLocker lock(&samplesMutex());
    samples()[QByteArray(name)].append(ms);
}

} // namespace drift::perf
