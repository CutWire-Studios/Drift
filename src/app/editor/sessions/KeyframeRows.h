#pragma once

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

// The Keyframes panel's row tree and the maths on it: grouping, per-row key unions, lane dots,
// hit-testing and marquee collection. Pure data in, plain values out, so it needs no project.
namespace KeyframeRows {

constexpr double kTimeEps = 1e-4;

struct Row
{
    QString type; // "group", "prop" or "scale"
    QString group;
    QString label;
    QString prop;
    QString keyProp;
    QString kind;
    QStringList tracks;
    bool animated = false;
    bool enabled = true;
    bool collapsed = false;
    int count = 0;
    QVariantMap entry;
    QVariantList entries;
    QVariantList keys;
    QHash<QString, QVariantList> trackKeys;
    QList<Row> children;

    bool isGroup() const { return type == QLatin1String("group"); }
    QString rowKey() const;
    QVariantMap toMap() const;
};

struct Options
{
    QString scaleLabel;
    bool sizeLinked = false;
    bool animatedOnly = false;
    QSet<QString> collapsed;
    double clipStart = 0;
    std::function<QVariantList(const QString &)> keysFor;
};

struct Axis
{
    double pxPerSecond = 100;
    double contentX = 0;

    double xFor(double seconds) const { return seconds * pxPerSecond - contentX; }
    double secondsFor(double x) const { return (x + contentX) / pxPerSecond; }
};

double secondsOf(const QVariant &key);
QVariantMap makeRef(const QString &prop, double seconds);

QList<Row> build(const QVariantList &entries, const Options &opts);
QVariantList unionKeys(const QList<QVariantList> &lists);

QVariantList refsForRowAt(const Row &row, double seconds, double tolerance = kTimeEps);
QVariantList laneDots(const QList<Row> &rows, int index);
QList<double> allKeyTimes(const QList<Row> &rows);
// -1 when there is no such key.
double prevKeyTime(const QList<double> &times, double t);
double nextKeyTime(const QList<double> &times, double t);

// {row, dot, refs, seconds}; `row` is -1 below the last lane.
QVariantMap hitTest(const QList<Row> &rows, const Axis &axis, double x, double y, double contentY,
                    double rowHeight, double slop);
QVariantList marqueeRefs(const QList<Row> &rows, const Axis &axis, double x0, double x1, double y0,
                         double y1, double contentY, double rowHeight, const QVariantList &base);

} // namespace KeyframeRows
