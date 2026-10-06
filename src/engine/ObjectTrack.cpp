#include "engine/ObjectTrack.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QRandomGenerator>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>

namespace drift {
namespace {

constexpr int kFormatVersion = 1;
constexpr int kTrackMaxEdge = 320;
constexpr double kAcceptScore = 0.45;
constexpr double kUpdateScore = 0.62;

struct Gray
{
    int w = 0;
    int h = 0;
    QVector<float> v;
};

struct Templ
{
    int side = 0;
    QVector<float> raw;
    QVector<float> centered;
    double mean = 0;
    double stddev = 0;
    bool ok = false;

    void finalize()
    {
        ok = false;
        centered.clear();
        const int n = raw.size();
        if (n < 9 || side * side != n)
            return;
        double sum = 0;
        double sum2 = 0;
        for (float p : raw) {
            sum += p;
            sum2 += double(p) * p;
        }
        mean = sum / n;
        const double var = sum2 / n - mean * mean;
        stddev = std::sqrt(qMax(0.0, var));
        if (stddev < 1e-4)
            return;
        centered.resize(n);
        for (int i = 0; i < n; ++i)
            centered[i] = float(raw[i] - mean);
        ok = true;
    }
};

QImage fitted(const QImage &frame, int w, int h)
{
    if (frame.isNull())
        return {};
    if (w > 0 && h > 0 && (frame.width() != w || frame.height() != h))
        return frame.scaled(w, h, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    return frame;
}

Gray toGray(const QImage &frame, int w, int h)
{
    Gray g;
    QImage img = fitted(frame, w, h);
    if (img.isNull())
        return g;
    if (w <= 0 || h <= 0) {
        int tw = img.width();
        int th = img.height();
        if (qMax(tw, th) > kTrackMaxEdge) {
            const double s = double(kTrackMaxEdge) / qMax(tw, th);
            tw = qMax(16, int(std::lround(tw * s)));
            th = qMax(16, int(std::lround(th * s)));
            img = img.scaled(tw, th, Qt::IgnoreAspectRatio, Qt::FastTransformation);
        }
    }
    img = img.convertToFormat(QImage::Format_Grayscale8);
    g.w = img.width();
    g.h = img.height();
    if (g.w < 8 || g.h < 8)
        return {};
    g.v.resize(g.w * g.h);
    for (int y = 0; y < g.h; ++y) {
        const uchar *row = img.constScanLine(y);
        float *dst = g.v.data() + y * g.w;
        for (int x = 0; x < g.w; ++x)
            dst[x] = row[x] / 255.f;
    }
    return g;
}

Templ makeTemplate(const Gray &g, int cx, int cy, int side)
{
    Templ t;
    const int half = side / 2;
    if (side < 3 || cx - half < 0 || cy - half < 0 || cx + half >= g.w || cy + half >= g.h)
        return t;
    t.side = side;
    t.raw.resize(side * side);
    int i = 0;
    for (int y = cy - half; y <= cy + half; ++y) {
        const float *row = g.v.constData() + y * g.w;
        for (int x = cx - half; x <= cx + half; ++x)
            t.raw[i++] = row[x];
    }
    t.finalize();
    return t;
}

double scoreTemplate(const Gray &g, const Templ &t, int cx, int cy)
{
    if (!t.ok)
        return -2.0;
    const int half = t.side / 2;
    const int x0 = cx - half;
    const int y0 = cy - half;
    if (x0 < 0 || y0 < 0 || x0 + t.side > g.w || y0 + t.side > g.h)
        return -2.0;

    const int n = t.side * t.side;
    double sum = 0;
    double sum2 = 0;
    for (int y = 0; y < t.side; ++y) {
        const float *row = g.v.constData() + (y0 + y) * g.w + x0;
        for (int x = 0; x < t.side; ++x) {
            const double p = row[x];
            sum += p;
            sum2 += p * p;
        }
    }
    const double mean = sum / n;
    const double var = sum2 / n - mean * mean;
    if (var < 1e-8)
        return 0.0;

    double dot = 0;
    int i = 0;
    for (int y = 0; y < t.side; ++y) {
        const float *row = g.v.constData() + (y0 + y) * g.w + x0;
        for (int x = 0; x < t.side; ++x, ++i)
            dot += double(t.centered[i]) * (row[x] - mean);
    }
    return dot / (n * t.stddev * std::sqrt(var));
}

} // namespace

struct ObjectPatchTracker::Impl
{
    int w = 0;
    int h = 0;
    int side = 0;
    int half = 0;
    Templ original;
    Templ live;
    double nx = 0.5;
    double ny = 0.5;
    double anchorNx = 0.5;
    double anchorNy = 0.5;
    double radius = 0.12;
    int lost = 0;
    bool ready = false;

    int clampX(int cx) const { return qBound(half, cx, w - 1 - half); }
    int clampY(int cy) const { return qBound(half, cy, h - 1 - half); }

    ObjectSample search(const Gray &g)
    {
        ObjectSample held;
        held.x = float(nx);
        held.y = float(ny);
        held.radius = float(radius);
        if (!ready || g.w != w || g.h != h)
            return held;

        int cx = clampX(qRound(nx * (w - 1)));
        int cy = clampY(qRound(ny * (h - 1)));
        int window = qMax(side, 20);
        if (lost > 0)
            window = qMin(window * 2, qMax(w, h) / 2);

        int bestX = cx;
        int bestY = cy;
        double best = -2.0;
        const auto consider = [&](const Templ &t, int x, int y) {
            const double s = scoreTemplate(g, t, x, y);
            if (s > best) {
                best = s;
                bestX = x;
                bestY = y;
            }
        };
        constexpr int step = 3;
        for (int y = cy - window; y <= cy + window; y += step) {
            for (int x = cx - window; x <= cx + window; x += step) {
                consider(original, x, y);
                if (live.ok)
                    consider(live, x, y);
            }
        }
        for (int y = bestY - step; y <= bestY + step; ++y) {
            for (int x = bestX - step; x <= bestX + step; ++x) {
                consider(original, x, y);
                if (live.ok)
                    consider(live, x, y);
            }
        }

        const auto peak = [&](int x, int y) {
            return qMax(scoreTemplate(g, original, x, y),
                        live.ok ? scoreTemplate(g, live, x, y) : -2.0);
        };
        double fx = bestX;
        double fy = bestY;
        if (best >= kAcceptScore) {
            const double c = peak(bestX, bestY);
            const double left = peak(bestX - 1, bestY);
            const double right = peak(bestX + 1, bestY);
            const double dxDen = left - 2.0 * c + right;
            if (std::abs(dxDen) > 1e-6)
                fx += qBound(-0.5, 0.5 * (left - right) / dxDen, 0.5);
            const double up = peak(bestX, bestY - 1);
            const double down = peak(bestX, bestY + 1);
            const double dyDen = up - 2.0 * c + down;
            if (std::abs(dyDen) > 1e-6)
                fy += qBound(-0.5, 0.5 * (up - down) / dyDen, 0.5);
        }

        ObjectSample out;
        out.x = float(fx / double(w - 1));
        out.y = float(fy / double(h - 1));
        out.radius = float(radius);
        out.confidence = float(qBound(0.0, best, 1.0));
        out.valid = best >= kAcceptScore;
        if (out.valid) {
            nx = out.x;
            ny = out.y;
            lost = 0;
            if (best >= kUpdateScore) {
                const Templ fresh = makeTemplate(g, bestX, bestY, side);
                if (fresh.ok && fresh.raw.size() == (live.ok ? live.raw.size() : fresh.raw.size())) {
                    if (!live.ok) {
                        live = fresh;
                    } else {
                        for (int i = 0; i < live.raw.size(); ++i)
                            live.raw[i] = 0.9f * live.raw[i] + 0.1f * fresh.raw[i];
                        live.finalize();
                    }
                }
            }
        } else {
            ++lost;
        }
        return out;
    }
};

namespace {

double layoutValue(const KeyframeTrack<double> &track, TimeUs relative, double fallback)
{
    return track.isEmpty() ? fallback : track.evaluateAt(relative);
}

const Clip *findClip(const QList<Track> &tracks, const QString &id)
{
    if (id.isEmpty())
        return nullptr;
    for (const Track &track : tracks) {
        for (const Clip &clip : track.clips) {
            if (clip.id == id)
                return &clip;
        }
    }
    return nullptr;
}

float lerpF(float a, float b, double t)
{
    return float(a + (b - a) * t);
}

double pointSegmentDistance(double px, double py, double ax, double ay, double bx, double by)
{
    const double dx = bx - ax;
    const double dy = by - ay;
    const double len2 = dx * dx + dy * dy;
    double t = 0;
    if (len2 > 1e-12)
        t = std::clamp(((px - ax) * dx + (py - ay) * dy) / len2, 0.0, 1.0);
    const double qx = ax + t * dx;
    const double qy = ay + t * dy;
    return std::hypot(px - qx, py - qy);
}

void simplifyLocks(const QList<ObjectLockKey> &dense, int a, int b, double epsilon,
                   QVector<char> *keep)
{
    if (b <= a + 1)
        return;
    double worst = 0;
    int at = -1;
    for (int i = a + 1; i < b; ++i) {
        const double d = pointSegmentDistance(dense.at(i).x, dense.at(i).y, dense.at(a).x,
                                              dense.at(a).y, dense.at(b).x, dense.at(b).y);
        if (d > worst) {
            worst = d;
            at = i;
        }
    }
    if (at >= 0 && worst > epsilon) {
        (*keep)[at] = 1;
        simplifyLocks(dense, a, at, epsilon, keep);
        simplifyLocks(dense, at, b, epsilon, keep);
    }
}

struct CacheEntry
{
    QDateTime modified;
    qint64 size = -1;
    std::shared_ptr<const ObjectTrack> track;
};

QMutex g_cacheMutex;
QHash<QString, CacheEntry> g_cache;

} // namespace

ObjectPatchTracker::ObjectPatchTracker()
    : m_impl(std::make_unique<Impl>())
{
}

ObjectPatchTracker::~ObjectPatchTracker() = default;

ObjectSample ObjectPatchTracker::start(const QImage &frame, double x, double y, double radius)
{
    m_impl = std::make_unique<Impl>();
    const Gray g = toGray(frame, 0, 0);
    ObjectSample sample;
    sample.x = float(qBound(0.0, x, 1.0));
    sample.y = float(qBound(0.0, y, 1.0));
    sample.radius = float(radius);
    if (g.w < 16 || g.h < 16)
        return sample;

    m_impl->w = g.w;
    m_impl->h = g.h;
    int side = int(std::lround(qBound(0.02, radius, 0.45) * g.w * 2.0));
    side = qBound(11, side, 61);
    if (side % 2 == 0)
        ++side;
    // The template has to fit. A circle dropped on the edge shrinks until it does.
    side = qMin(side, qMin(g.w, g.h) - 1);
    if (side % 2 == 0)
        --side;
    if (side < 11)
        return sample;

    m_impl->side = side;
    m_impl->half = side / 2;
    m_impl->radius = radius;
    const int cx = m_impl->clampX(qRound(sample.x * (g.w - 1)));
    const int cy = m_impl->clampY(qRound(sample.y * (g.h - 1)));
    m_impl->original = makeTemplate(g, cx, cy, side);
    if (!m_impl->original.ok)
        return sample;

    m_impl->nx = double(cx) / double(g.w - 1);
    m_impl->ny = double(cy) / double(g.h - 1);
    m_impl->anchorNx = m_impl->nx;
    m_impl->anchorNy = m_impl->ny;
    m_impl->ready = true;

    sample.x = float(m_impl->nx);
    sample.y = float(m_impl->ny);
    sample.confidence = 1.f;
    sample.valid = true;
    return sample;
}

ObjectSample ObjectPatchTracker::next(const QImage &frame)
{
    if (!m_impl->ready) {
        ObjectSample held;
        held.radius = float(m_impl->radius);
        return held;
    }
    return m_impl->search(toGray(frame, m_impl->w, m_impl->h));
}

void ObjectPatchTracker::resetToSeed()
{
    m_impl->nx = m_impl->anchorNx;
    m_impl->ny = m_impl->anchorNy;
    m_impl->live = {};
    m_impl->lost = 0;
}

QList<ObjectSample> trackObjectFrames(const QList<QImage> &frames, int seedIndex, double x,
                                      double y, double radius)
{
    QList<ObjectSample> out;
    if (frames.isEmpty())
        return out;
    seedIndex = qBound(0, seedIndex, frames.size() - 1);
    out.resize(frames.size());

    ObjectPatchTracker tracker;
    const ObjectSample seed = tracker.start(frames.at(seedIndex), x, y, radius);
    for (ObjectSample &sample : out) {
        sample = seed;
        sample.valid = false;
        sample.confidence = 0;
    }
    if (!seed.valid)
        return out;

    out[seedIndex] = seed;
    for (int i = seedIndex + 1; i < frames.size(); ++i)
        out[i] = tracker.next(frames.at(i));
    tracker.resetToSeed();
    for (int i = seedIndex - 1; i >= 0; --i)
        out[i] = tracker.next(frames.at(i));
    return out;
}

ObjectSample ObjectTrack::sample(TimeUs relativeUs) const
{
    if (frames.isEmpty() || fps <= 0)
        return {};

    const double exact = double(relativeUs) / (double(kUsPerSecond) / fps);
    const auto nearestValid = [&](int around) {
        for (int d = 1; d < frames.size(); ++d) {
            if (around - d >= 0 && frames.at(around - d).valid)
                return frames.at(around - d);
            if (around + d < frames.size() && frames.at(around + d).valid)
                return frames.at(around + d);
        }
        return ObjectSample{};
    };

    if (exact <= 0.0)
        return frames.first().valid ? frames.first() : nearestValid(0);
    if (exact >= frames.size() - 1) {
        const ObjectSample &last = frames.last();
        return last.valid ? last : nearestValid(frames.size() - 1);
    }

    const int i0 = int(exact);
    const int i1 = i0 + 1;
    const double t = exact - i0;
    const ObjectSample &a = frames.at(i0);
    const ObjectSample &b = frames.at(i1);
    if (a.valid && b.valid) {
        ObjectSample out;
        out.valid = true;
        out.x = lerpF(a.x, b.x, t);
        out.y = lerpF(a.y, b.y, t);
        out.radius = lerpF(a.radius, b.radius, t);
        out.confidence = lerpF(a.confidence, b.confidence, t);
        return out;
    }
    if (a.valid)
        return a;
    if (b.valid)
        return b;
    return nearestValid(i0);
}

void smoothObjectTrack(ObjectTrack *track, int radius)
{
    if (!track || radius <= 0 || track->frames.size() < 2)
        return;
    const QList<ObjectSample> src = track->frames;
    for (int i = 0; i < src.size();) {
        if (!src.at(i).valid) {
            ++i;
            continue;
        }
        int run = i;
        while (run < src.size() && src.at(run).valid)
            ++run;
        for (int j = i; j < run; ++j) {
            const int lo = qMax(i, j - radius);
            const int hi = qMin(run - 1, j + radius);
            double sx = 0, sy = 0, sc = 0;
            int n = 0;
            for (int k = lo; k <= hi; ++k) {
                sx += src.at(k).x;
                sy += src.at(k).y;
                sc += src.at(k).confidence;
                ++n;
            }
            track->frames[j].x = float(sx / n);
            track->frames[j].y = float(sy / n);
            track->frames[j].confidence = float(sc / n);
        }
        i = run;
    }
}

QString objectTrackCacheDir()
{
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (root.isEmpty())
        return {};
    const QString dir = QDir(root).filePath(QStringLiteral("objecttracks"));
    if (!QDir().mkpath(dir))
        return {};
    return dir;
}

QString newObjectTrackPath()
{
    const QString dir = objectTrackCacheDir();
    if (dir.isEmpty())
        return {};
    const QString name = QStringLiteral("object-%1-%2.json")
                             .arg(QDateTime::currentMSecsSinceEpoch())
                             .arg(QRandomGenerator::global()->bounded(100000), 5, 10, QLatin1Char('0'));
    return QDir(dir).filePath(name);
}

bool writeObjectTrack(const QString &path, const ObjectTrack &track, QString *errorOut)
{
    QJsonArray frames;
    for (const ObjectSample &sample : track.frames) {
        frames.append(QJsonArray{double(sample.x), double(sample.y), double(sample.radius),
                                 double(sample.confidence), sample.valid ? 1 : 0});
    }
    const QJsonObject root{
        {QStringLiteral("version"), kFormatVersion},
        {QStringLiteral("fps"), track.fps},
        {QStringLiteral("startSrcUs"), qint64(track.startSrcUs)},
        {QStringLiteral("frames"), frames},
    };

    // A cancelled scan must not leave a file that looks complete. Write aside, then rename.
    const QString partPath = path + QStringLiteral(".part");
    QFile f(partPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorOut)
            *errorOut = QStringLiteral("Could not write %1").arg(partPath);
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    f.close();

    QFile::remove(path);
    if (!QFile::rename(partPath, path)) {
        QFile::remove(partPath);
        if (errorOut)
            *errorOut = QStringLiteral("Could not finalize %1").arg(path);
        return false;
    }
    return true;
}

bool readObjectTrack(const QString &path, ObjectTrack *out, QString *errorOut)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (errorOut)
            *errorOut = QStringLiteral("Could not read %1").arg(path);
        return false;
    }
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorOut)
            *errorOut = QStringLiteral("Object track %1 is not valid JSON").arg(path);
        return false;
    }
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("version")).toInt() != kFormatVersion) {
        if (errorOut)
            *errorOut = QStringLiteral("Object track %1 has an unsupported format version").arg(path);
        return false;
    }
    out->fps = root.value(QStringLiteral("fps")).toInt();
    out->startSrcUs = TimeUs(root.value(QStringLiteral("startSrcUs")).toInteger(0));
    out->frames.clear();
    const QJsonArray frames = root.value(QStringLiteral("frames")).toArray();
    out->frames.reserve(frames.size());
    for (const QJsonValue &value : frames) {
        const QJsonArray row = value.toArray();
        if (row.size() < 5) {
            if (errorOut)
                *errorOut = QStringLiteral("Object track %1 has a malformed entry").arg(path);
            return false;
        }
        ObjectSample sample;
        sample.x = float(row.at(0).toDouble());
        sample.y = float(row.at(1).toDouble());
        sample.radius = float(row.at(2).toDouble());
        sample.confidence = float(row.at(3).toDouble());
        sample.valid = row.at(4).toInt() != 0;
        out->frames.append(sample);
    }
    if (out->fps <= 0 || out->frames.isEmpty()) {
        if (errorOut)
            *errorOut = QStringLiteral("Object track %1 has no frames").arg(path);
        return false;
    }
    return true;
}

std::shared_ptr<const ObjectTrack> loadObjectTrackCached(const QString &path)
{
    if (path.isEmpty())
        return nullptr;
    const QFileInfo info(path);
    if (!info.exists())
        return nullptr;

    QMutexLocker lock(&g_cacheMutex);
    const auto it = g_cache.constFind(path);
    if (it != g_cache.cend() && it->modified == info.lastModified() && it->size == info.size())
        return it->track;

    auto track = std::make_shared<ObjectTrack>();
    QString error;
    if (!readObjectTrack(path, track.get(), &error)) {
        g_cache.insert(path, CacheEntry{info.lastModified(), info.size(), nullptr});
        return nullptr;
    }
    const std::shared_ptr<const ObjectTrack> shared = track;
    g_cache.insert(path, CacheEntry{info.lastModified(), info.size(), shared});
    return shared;
}

bool objectTrackIsStale(const Clip &clip)
{
    if (clip.objectTrackPath.isEmpty())
        return false;
    const TimeUs dt = clip.objectTrackSeedUs - clip.objectTrackRanUs;
    return std::abs(clip.objectTrackX - clip.objectTrackRanX) > 0.004
        || std::abs(clip.objectTrackY - clip.objectTrackRanY) > 0.004
        || std::abs(clip.objectTrackRadius - clip.objectTrackRanRadius) > 0.004
        || dt > 50000 || dt < -50000;
}

bool objectLockIsStale(const Clip &clip)
{
    if (!clip.objectLockApplied)
        return false;
    return std::abs(clip.objectTrackZoom - clip.objectLockZoom) > 0.01
        || std::abs(clip.objectTrackHoldX - clip.objectLockHoldX) > 0.004
        || std::abs(clip.objectTrackHoldY - clip.objectLockHoldY) > 0.004
        || clip.objectTrackPath != clip.objectLockPath;
}

ObjectLockPose objectLockPose(double cx, double cy, const ObjectLockOptions &options)
{
    ObjectLockPose pose;
    if (options.canvasW < 1.0 || options.canvasH < 1.0)
        return pose;
    const double zoom = qMax(1.0, options.zoom);
    pose.w = options.canvasW * zoom;
    pose.h = options.canvasH * zoom;
    // Source (cx, cy) should land on the hold point. Clamp so the enlarged clip still
    // covers the canvas — near the edge the object drifts off the hold rather than
    // revealing empty background.
    const double minX = options.canvasW - pose.w;
    const double minY = options.canvasH - pose.h;
    pose.x = std::clamp(options.holdX * options.canvasW - cx * pose.w, minX, 0.0);
    pose.y = std::clamp(options.holdY * options.canvasH - cy * pose.h, minY, 0.0);
    return pose;
}

QList<ObjectLockKey> planObjectLock(const ObjectTrack &track, const ObjectLockOptions &options)
{
    QList<ObjectLockKey> dense;
    if (track.frames.isEmpty())
        return dense;

    double cx = 0.5;
    double cy = 0.5;
    bool have = false;
    for (int i = 0; i < track.frames.size(); ++i) {
        const ObjectSample &sample = track.frames.at(i);
        if (sample.valid) {
            cx = sample.x;
            cy = sample.y;
            have = true;
        }
        if (!have)
            continue;
        const ObjectLockPose pose = objectLockPose(cx, cy, options);
        dense.append(ObjectLockKey{i, pose.x, pose.y, pose.w, pose.h});
    }
    if (dense.isEmpty())
        return dense;
    if (dense.size() == 1)
        return dense;

    QVector<char> keep(dense.size(), 0);
    keep[0] = 1;
    keep[dense.size() - 1] = 1;
    simplifyLocks(dense, 0, dense.size() - 1, qMax(0.25, options.epsilonPx), &keep);

    QList<ObjectLockKey> sparse;
    for (int i = 0; i < dense.size(); ++i) {
        if (keep.at(i))
            sparse.append(dense.at(i));
    }
    return sparse;
}

bool objectFollowPosition(const QList<Track> &tracks, const Clip &follower, TimeUs timelineUs,
                          double projectW, double projectH, double *xOut, double *yOut)
{
    if (!xOut || !yOut || follower.objectFollowClipId.isEmpty())
        return false;
    const Clip *host = findClip(tracks, follower.objectFollowClipId);
    if (!host || host->objectTrackPath.isEmpty() || !host->containsTime(timelineUs))
        return false;
    const std::shared_ptr<const ObjectTrack> track = loadObjectTrackCached(host->objectTrackPath);
    if (!track || track->isEmpty())
        return false;

    const ObjectSample sample =
        track->sample(host->timelineToSourceUs(timelineUs) - host->objectTrackSrcOffsetUs);
    if (!sample.valid)
        return false;

    const TimeUs hostRel = timelineUs - host->timelineStart;
    const double hx = layoutValue(host->transformX, hostRel, 0.0);
    const double hy = layoutValue(host->transformY, hostRel, 0.0);
    const double hw = layoutValue(host->transformW, hostRel, projectW);
    const double hh = layoutValue(host->transformH, hostRel, projectH);
    const TimeUs followRel = timelineUs - follower.timelineStart;
    const double fw = layoutValue(follower.transformW, followRel, projectW);
    const double fh = layoutValue(follower.transformH, followRel, projectH);

    *xOut = hx + double(sample.x) * hw - fw * 0.5;
    *yOut = hy + double(sample.y) * hh - fh * 0.5;
    return true;
}

} // namespace drift
