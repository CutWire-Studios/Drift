#include "engine/diagnostics/sheets/AudioSheet.h"

#include "engine/audio/analysis/RealFft.h"

#include <QFont>
#include <QHash>
#include <QPair>
#include <QSet>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace drift::audiosheet {
namespace {

constexpr double kDpi = 100.0;
constexpr int kSpecFft = 4096;
constexpr int kLabelRows = 4;
constexpr double kLoudFloor = -60.0;

const QColor kCutColor(0x00, 0xa6, 0xd6);

double ptToPx(double pt)
{
    return pt * kDpi / 72.0;
}

QColor kindColor(const QString &kind)
{
    static const QHash<QString, QColor> colors = {
        {QStringLiteral("HIT"), QColor(0xd6, 0x27, 0x28)},   {QStringLiteral("SWELL"), QColor(0x1f, 0x77, 0xb4)},
        {QStringLiteral("WHOOSH"), QColor(0x1f, 0x77, 0xb4)}, {QStringLiteral("RISER"), QColor(0x17, 0xbe, 0xcf)},
        {QStringLiteral("KEY"), QColor(0x2c, 0xa0, 0x2c)},    {QStringLiteral("UI"), QColor(0x8c, 0x56, 0x4b)},
        {QStringLiteral("STEP"), QColor(0xbc, 0xbd, 0x22)},   {QStringLiteral("FOLEY"), QColor(0xe3, 0x77, 0xc2)},
        {QStringLiteral("NOTE"), QColor(0x94, 0x67, 0xbd)},   {QStringLiteral("DRUM"), QColor(0xff, 0x7f, 0x0e)},
        {QStringLiteral("VOICE"), QColor(0x0b, 0x6e, 0x4f)},  {QStringLiteral("SPEECH"), QColor(0x0b, 0x6e, 0x4f)},
        {QStringLiteral("MUSIC"), QColor(0x94, 0x67, 0xbd)},
    };
    return colors.value(kind.toUpper(), QColor(0x7f, 0x7f, 0x7f));
}

QColor statusColor(AudioSyncItem::Status s)
{
    switch (s) {
    case AudioSyncItem::Status::Ok: return QColor(0x2c, 0xa0, 0x2c);
    case AudioSyncItem::Status::Near: return QColor(0xff, 0x9f, 0x1c);
    case AudioSyncItem::Status::Swell: return QColor(0x1f, 0x77, 0xb4);
    case AudioSyncItem::Status::Peak: return QColor(0x94, 0x67, 0xbd);
    case AudioSyncItem::Status::Off:
    case AudioSyncItem::Status::NoSound: break;
    }
    return QColor(0xd6, 0x27, 0x28);
}

QString minSec(double t)
{
    const int m = int(t / 60.0);
    return QStringLiteral("%1:%2").arg(m).arg(int(std::lround(t - 60.0 * m)), 2, 10, QLatin1Char('0'));
}

QString tickLabel(double t, double step)
{
    const int m = int(t / 60.0);
    const double s = t - 60.0 * m;
    if (step >= 1.0)
        return QStringLiteral("%1:%2").arg(m).arg(int(std::lround(s)), 2, 10, QLatin1Char('0'));
    const int nd = std::abs(step * 10.0 - std::round(step * 10.0)) < 1e-9 ? 1 : 2;
    return QStringLiteral("%1:%2").arg(m).arg(s, 3 + nd, 'f', nd, QLatin1Char('0'));
}

QFont fontOf(double pt, bool bold = false, bool mono = false)
{
    QFont f;
    if (mono)
        f.setStyleHint(QFont::Monospace);
    if (mono)
        f.setFamily(QStringLiteral("monospace"));
    f.setPixelSize(std::max(6, int(std::lround(ptToPx(pt)))));
    f.setBold(bold);
    return f;
}

double percentileOf(std::vector<float> v, double pct)
{
    if (v.empty())
        return 0.0;
    const size_t k = std::min(v.size() - 1, size_t(pct / 100.0 * double(v.size() - 1) + 0.5));
    std::nth_element(v.begin(), v.begin() + std::ptrdiff_t(k), v.end());
    return v[k];
}

struct Lane
{
    int top = 0;
    int height = 0;
    QRectF rect; // the drawable box, 6 px shorter than the slot
};

} // namespace

QRgb magma(float v)
{
    static const float stops[9][3] = {{0, 0, 4},       {29, 17, 71},   {81, 18, 124},
                                      {126, 36, 130},  {183, 55, 121}, {233, 84, 98},
                                      {252, 137, 97},  {254, 201, 141}, {252, 253, 191}};
    v = std::clamp(v, 0.0f, 1.0f) * 8.0f;
    const int i = std::min(int(v), 7);
    const float f = v - float(i);
    const auto mix = [&](int c) { return int(std::lround(stops[i][c] + (stops[i + 1][c] - stops[i][c]) * f)); };
    return qRgb(mix(0), mix(1), mix(2));
}

double tickStep(double span, int maxTicks)
{
    span = std::max(span, 1e-3);
    for (double s : {0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0, 300.0}) {
        if (span / s <= maxTicks)
            return s;
    }
    return 600.0;
}

Spectrogram logSpectrogram(const float *mono, qint64 frames, int sampleRate, int columns, int rows,
                           double minHz, double maxHz)
{
    Spectrogram out;
    RealFft fft(kSpecFft);
    if (!mono || frames <= 0 || columns <= 0 || rows <= 0 || !fft.ok())
        return out;
    const int hop = std::max<qint64>(64, frames / columns);
    const qint64 padded = frames + kSpecFft;
    const int count = int(std::max<qint64>(1, 1 + (padded - kSpecFft) / hop));

    std::vector<double> edges(static_cast<size_t>(rows) + 2);
    for (int i = 0; i < rows + 2; ++i)
        edges[size_t(i)] = minHz * std::pow(maxHz / minHz, double(i) / (rows + 1));
    const double binHz = double(sampleRate) / kSpecFft;
    struct Band { int first; std::vector<float> w; };
    std::vector<Band> bands(static_cast<size_t>(rows));
    out.centreHz.resize(rows);
    for (int r = 0; r < rows; ++r) {
        const double lo = edges[size_t(r)], c = edges[size_t(r) + 1], hi = edges[size_t(r) + 2];
        out.centreHz[r] = c;
        Band b{-1, {}};
        for (int k = 0; k <= kSpecFft / 2; ++k) {
            const double f = k * binHz;
            const double v = std::max(0.0, std::min((f - lo) / (c - lo), (hi - f) / (hi - c)));
            if (v > 0.0) {
                if (b.first < 0)
                    b.first = k;
                b.w.push_back(float(v));
            } else if (b.first >= 0) {
                break;
            }
        }
        if (b.first < 0) {
            b.first = std::clamp(int(std::lround(c / binHz)), 0, kSpecFft / 2);
            b.w = {1.0f};
        }
        float sum = 0.0f;
        for (float w : b.w)
            sum += w;
        for (float &w : b.w)
            w /= sum;
        bands[size_t(r)] = std::move(b);
    }

    std::vector<float> hann(kSpecFft);
    for (int i = 0; i < kSpecFft; ++i)
        hann[size_t(i)] = float(0.5 - 0.5 * std::cos(2.0 * M_PI * i / (kSpecFft - 1)));

    out.rows = rows;
    out.columns = count;
    out.db.resize(qsizetype(rows) * count);
    for (int t = 0; t < count; ++t) {
        const qint64 start = qint64(t) * hop - kSpecFft / 2;
        for (int i = 0; i < kSpecFft; ++i) {
            const qint64 s = start + i;
            fft.in()[i] = s >= 0 && s < frames ? mono[s] * hann[size_t(i)] : 0.0f;
        }
        fft.run();
        const float *c = fft.out();
        for (int r = 0; r < rows; ++r) {
            const Band &b = bands[size_t(r)];
            double acc = 0.0;
            for (size_t k = 0; k < b.w.size(); ++k) {
                const int bin = b.first + int(k);
                acc += double(b.w[k]) * (double(c[2 * bin]) * c[2 * bin] + double(c[2 * bin + 1]) * c[2 * bin + 1]);
            }
            out.db[qsizetype(t) * rows + r] = float(10.0 * std::log10(acc + 1e-14));
        }
    }
    return out;
}

QList<PlacedLabel> placeLabels(const QList<EventLabel> &labels, const QList<int> &widthsPx,
                               double start, double end, double plotPx, int rows)
{
    QList<int> order;
    for (int i = 0; i < labels.size(); ++i)
        order.append(i);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        if (labels[a].priority != labels[b].priority)
            return labels[a].priority < labels[b].priority;
        return labels[a].rank > labels[b].rank;
    });
    const double pxPerSec = plotPx / std::max(end - start, 1e-6);
    std::vector<std::vector<std::pair<double, double>>> used(static_cast<size_t>(std::max(rows, 0)));
    QList<PlacedLabel> out;
    for (int i : order) {
        const double w = (widthsPx.value(i) + 16) / pxPerSec;
        const double t = labels[i].seconds;
        double a = t - 0.15 * w;
        double b = t + 0.85 * w;
        if (b > end) {
            a = end - w;
            b = end;
        }
        a = std::max(a, start);
        for (int r = 0; r < rows; ++r) {
            bool free = true;
            for (const auto &u : used[size_t(r)])
                free = free && (b <= u.first || a >= u.second);
            if (free) {
                used[size_t(r)].push_back({a, b});
                out.append({r, t, a, i});
                break;
            }
        }
    }
    return out;
}

QList<EventLabel> defaultEventLabels(const Input &in)
{
    QList<EventLabel> labels;
    QList<double> cueTimes;
    for (const AudioSyncItem &it : in.sync.items) {
        if (it.answered)
            cueTimes.append(it.eventSeconds);
    }
    for (const AudioEvent &e : in.events) {
        EventLabel el;
        el.seconds = e.seconds;
        el.kind = e.kind == AudioEvent::Kind::Swell ? QStringLiteral("SWELL") : QStringLiteral("HIT");
        el.text = e.kind == AudioEvent::Kind::Swell
                      ? QStringLiteral("SWELL")
                      : QStringLiteral("HIT %1").arg(int(std::lround(e.levelDbfs)));
        el.weak = e.prominenceDb < 6.0;
        bool answers = false;
        for (double c : cueTimes)
            answers = answers || std::abs(c - e.seconds) < 1e-6;
        el.priority = answers ? 0 : 1;
        el.rank = e.prominenceDb;
        labels.append(el);
    }
    return labels;
}

QImage render(const Input &in, const Options &opt)
{
    const int width = std::clamp(opt.width, kMinWidth, kMaxWidth);
    const double t0 = in.startSeconds;
    const double dur = std::max(in.durationSeconds, 1e-3);
    const double t1 = t0 + dur;
    const double fs = std::max(11.0, width / 125.0);
    const int left = std::max(92, int(6.4 * fs) + 8);
    const int right = 64;
    const double plotW = width - left - right;
    const auto xOf = [&](double t) { return left + (t - t0) / dur * plotW; };

    const bool hasFrames = !in.frames.isEmpty();
    const bool hasSections = !in.sections.isEmpty();
    const bool hasPresence = !in.presence.isEmpty();

    struct Line { QString text; double pt; bool bold; QColor color; };
    QList<Line> lines;
    lines.append({in.title + QStringLiteral("   %1-%2").arg(minSec(t0), minSec(t1)), fs * 1.25, true, Qt::black});
    const LoudnessReport &lo = in.loudness;
    QStringList sub;
    if (lo.hasIntegrated) {
        QString s = QStringLiteral("%1 LUFS").arg(lo.integratedLufs, 0, 'f', 1);
        if (lo.hasTruePeak)
            s += QStringLiteral("   TP %1 dBTP").arg(lo.truePeakDb, 0, 'f', 1);
        if (lo.hasLra)
            s += QStringLiteral("   LRA %1 LU").arg(lo.lraLu, 0, 'f', 1);
        sub << s;
    } else if (!lo.hasTruePeak) {
        sub << QStringLiteral("digital silence");
    }
    if (in.bpm > 0.0)
        sub << QStringLiteral("~%1 BPM").arg(in.bpm, 0, 'f', 0);
    sub << QStringLiteral("%1 events").arg(in.events.size());
    lines.append({sub.join(QStringLiteral("    ")), fs * 0.95, false, QColor(0x33, 0x33, 0x33)});
    if (in.hasSync && in.sync.stats.n > 0) {
        const AudioSyncStats &st = in.sync.stats;
        QString s = QStringLiteral("sync %1 cuts: %2% onset within %3 ms").arg(st.n).arg(100.0 * st.hitWithinTolerance, 0, 'f', 0).arg(st.toleranceMs, 0, 'f', 0);
        if (st.hasChance)
            s += QStringLiteral(" (chance %1%)").arg(100.0 * st.chanceTolerance, 0, 'f', 0);
        if (st.hasAccents) {
            s += QStringLiteral(", %1% on a strong accent (chance %2%)")
                     .arg(100.0 * st.accentWithinTolerance, 0, 'f', 0)
                     .arg(100.0 * st.accentChanceTolerance, 0, 'f', 0);
        }
        if (st.answeredByLevelPeak > 0)
            s += QStringLiteral(", %1 only a level peak").arg(st.answeredByLevelPeak);
        s += QStringLiteral(", %1 without a hit").arg(st.noSound.size());
        lines.append({s, fs * 0.95, false, QColor(0x33, 0x33, 0x33)});
        lines.append({QStringLiteral("markers (diamond = cut): green = an onset within %1 ms, amber = within 100 ms, blue = a swell peak, "
                                     "violet = only a level peak (soft whoosh?), red = no hit.  Dashed lines = picture cuts.")
                          .arg(st.toleranceMs, 0, 'f', 0),
                      fs * 0.72, false, QColor(0x55, 0x55, 0x55)});
    }
    for (const QString &n : in.notes)
        lines.append({n, fs * 0.72, false, QColor(0x55, 0x55, 0x55)});

    double headH = 22.0;
    for (const Line &l : lines)
        headH += ptToPx(l.pt * 1.45);

    struct Slot { QString name; int h; };
    QList<Slot> laneSlots;
    if (hasFrames)
        laneSlots.append({QStringLiteral("frames"), 155});
    if (hasSections)
        laneSlots.append({QStringLiteral("sections"), 60});
    if (hasPresence)
        laneSlots.append({QStringLiteral("presence"), 40});
    laneSlots.append({QStringLiteral("labels"), 155});
    laneSlots.append({QStringLiteral("spec"), 360});
    laneSlots.append({QStringLiteral("wave"), 95});
    laneSlots.append({QStringLiteral("loud"), 175});
    laneSlots.append({QStringLiteral("beats"), 75});
    int total = int(std::lround(headH)) + 55;
    for (const Slot &s : laneSlots)
        total += s.h;

    QImage img(width, total, QImage::Format_RGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    QHash<QString, Lane> lanes;
    {
        int y = int(std::lround(headH));
        for (const Slot &s : laneSlots) {
            Lane l;
            l.top = y;
            l.height = s.h;
            l.rect = QRectF(left, y, plotW, s.h - 6);
            lanes.insert(s.name, l);
            y += s.h;
        }
    }

    const auto text = [&](const QRectF &r, int flags, const QString &s, const QFont &f, const QColor &c) {
        p.setFont(f);
        p.setPen(c);
        p.drawText(r, flags, s);
    };

    double yy = 12.0;
    for (const Line &l : lines) {
        const double h = ptToPx(l.pt * 1.45);
        text(QRectF(left, yy, width - left - 8, h), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, l.text,
             fontOf(l.pt, l.bold), l.color);
        yy += h;
    }

    const double step = tickStep(dur);
    std::vector<double> ticks;
    for (double t = std::ceil(t0 / step) * step; t <= t1 + 1e-9; t += step)
        ticks.push_back(t);

    const auto frameLane = [&](const Lane &l) {
        p.setPen(QPen(QColor(0x99, 0x99, 0x99), 0.8));
        p.setBrush(Qt::NoBrush);
        p.drawRect(l.rect);
    };
    const auto laneTitle = [&](const Lane &l, const QString &s, bool inside) {
        const QFont f = fontOf(fs * (inside ? 0.78 : 0.82));
        if (inside) {
            const QFontMetrics fm(f);
            const QRectF r(l.rect.left() + 3, l.rect.top() + 3, fm.horizontalAdvance(s) + 10, fm.height() + 4);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255, 205));
            p.drawRoundedRect(r, 3, 3);
            text(r, Qt::AlignCenter, s, f, QColor(0x22, 0x22, 0x22));
        } else {
            text(QRectF(2, l.rect.top(), left - 8, l.rect.height()), Qt::AlignRight | Qt::AlignVCenter, s, f,
                 QColor(0x33, 0x33, 0x33));
        }
    };

    // Frames.
    if (hasFrames) {
        const Lane &l = lanes[QStringLiteral("frames")];
        p.save();
        p.setClipRect(l.rect);
        QList<FrameAt> shown;
        for (const FrameAt &f : std::as_const(in.frames)) {
            if (!f.image.isNull() && f.seconds >= t0 && f.seconds <= t1)
                shown.append(f);
        }
        std::sort(shown.begin(), shown.end(), [](const FrameAt &a, const FrameAt &b) { return a.seconds < b.seconds; });
        const qsizetype n = shown.size();
        constexpr double gap = 4.0;
        const double maxH = l.rect.height() - 12 - 12;
        const double aspect = n ? double(shown.first().image.width()) / std::max(1, shown.first().image.height()) : 1.0;
        const double tw = n ? std::min(maxH * aspect, (l.rect.width() - gap * double(n - 1)) / double(n)) : 0.0;
        const double th = tw / aspect;
        // Tiles sit at their own time when they can, else are pushed apart; the tick under each
        // still points at the true time.
        std::vector<double> xs(static_cast<size_t>(n));
        for (qsizetype i = 0; i < n; ++i) {
            const double want = std::clamp(xOf(shown[i].seconds), l.rect.left(), l.rect.right() - tw);
            xs[size_t(i)] = i ? std::max(want, xs[size_t(i - 1)] + tw + gap) : want;
        }
        for (qsizetype i = n - 1; i >= 0; --i) {
            const double limit = i == n - 1 ? l.rect.right() - tw : xs[size_t(i + 1)] - tw - gap;
            xs[size_t(i)] = std::min(xs[size_t(i)], limit);
        }
        for (qsizetype i = 0; i < n; ++i) {
            const QRectF r(xs[size_t(i)], l.rect.top() + 5, tw, th);
            p.drawImage(r, shown[i].image);
            p.setPen(QPen(QColor(0x33, 0x33, 0x33), 1.5));
            const double tx = xOf(shown[i].seconds);
            p.drawLine(QPointF(std::clamp(tx, r.left(), r.right()), r.bottom() + 1), QPointF(tx, l.rect.bottom()));
        }
        p.restore();
        frameLane(l);
        laneTitle(l, QStringLiteral("frames"), false);
    }

    // Sections.
    if (hasSections) {
        const Lane &l = lanes[QStringLiteral("sections")];
        int k = 0;
        const QFont f = fontOf(fs * 0.9);
        const QFontMetrics fm(f);
        for (const Section &s : in.sections) {
            const double a = xOf(std::max(s.start, t0));
            const double b = xOf(std::min(s.end, t1));
            if (b <= a)
                continue;
            const QRectF r(a, l.rect.top() + 3, b - a, l.rect.height() - 6);
            p.setPen(QPen(QColor(0x88, 0x99, 0xaa), 0.8));
            p.setBrush((k++ % 2) ? QColor(0xf4, 0xea, 0xd9) : QColor(0xe8, 0xee, 0xf7));
            p.drawRect(r);
            const QString label = fm.elidedText(s.label, Qt::ElideRight, int(r.width()) - 8);
            if (r.width() > 30)
                text(r.adjusted(4, 0, -2, 0), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, label, f, Qt::black);
        }
        frameLane(l);
        laneTitle(l, QStringLiteral("sections"), false);
    }

    // Speech / music presence.
    if (hasPresence) {
        const Lane &l = lanes[QStringLiteral("presence")];
        const QFont f = fontOf(fs * 0.8);
        for (const PresenceSpan &s : in.presence) {
            const double a = xOf(std::max(s.start, t0));
            const double b = xOf(std::min(s.end, t1));
            if (b <= a)
                continue;
            const QRectF r(a, l.rect.top() + 3, b - a, l.rect.height() - 6);
            QColor c = kindColor(s.kind);
            c.setAlpha(150);
            p.fillRect(r, c);
            if (r.width() > 40)
                text(r, Qt::AlignCenter | Qt::TextSingleLine, s.kind, f, Qt::white);
        }
        frameLane(l);
        laneTitle(l, QStringLiteral("presence"), false);
    }

    // Event labels.
    QList<PlacedLabel> placed;
    const QList<EventLabel> labels = in.labels.isEmpty() ? defaultEventLabels(in) : in.labels;
    {
        const Lane &l = lanes[QStringLiteral("labels")];
        const double lf = fs * 0.85;
        const QFont f = fontOf(lf, false, true);
        const QFontMetrics fm(f);
        QList<int> widths;
        for (const EventLabel &el : std::as_const(labels))
            widths.append(fm.horizontalAdvance(el.text));
        placed = placeLabels(labels, widths, t0, t1, plotW, kLabelRows);
        const double rowH = l.rect.height() / kLabelRows;
        for (const PlacedLabel &pl : std::as_const(placed)) {
            const EventLabel &el = labels[pl.index];
            QColor c = kindColor(el.kind);
            if (el.weak)
                c.setAlpha(140);
            const double cy = l.rect.top() + (pl.row + 0.5) * rowH;
            p.setPen(QPen(c, 1.0));
            p.drawLine(QPointF(xOf(pl.seconds), cy + rowH * 0.5 - 2), QPointF(xOf(pl.seconds), l.rect.bottom()));
            const double w = widths[pl.index] + 8;
            const QRectF r(xOf(pl.left), cy - fm.height() / 2.0 - 1, w, fm.height() + 2);
            p.fillRect(r, c);
            text(r, Qt::AlignCenter | Qt::TextSingleLine, el.text, f, Qt::white);
        }
        laneTitle(l, QStringLiteral("events"), false);

        QStringList kinds;
        for (const PlacedLabel &pl : std::as_const(placed)) {
            if (!kinds.contains(labels[pl.index].kind))
                kinds << labels[pl.index].kind;
        }
        std::sort(kinds.begin(), kinds.end(), std::greater<QString>());
        const QFont kf = fontOf(fs * 0.85, false, true);
        const QFontMetrics kfm(kf);
        double xk = width - right;
        for (const QString &k : std::as_const(kinds)) {
            const double w = kfm.horizontalAdvance(k) + 12;
            const QRectF r(xk - w, 12, w, kfm.height() + 6);
            p.setPen(Qt::NoPen);
            p.setBrush(kindColor(k));
            p.drawRoundedRect(r, 4, 4);
            text(r, Qt::AlignCenter, k, kf, Qt::white);
            xk -= w + 8;
        }
    }

    // Spectrogram.
    {
        const Lane &l = lanes[QStringLiteral("spec")];
        const int cols = std::max(1, int(plotW));
        const Spectrogram spec = logSpectrogram(in.mono, in.monoFrames, in.sampleRate, cols);
        if (spec.columns > 0) {
            std::vector<float> all(spec.db.begin(), spec.db.end());
            const double top = std::max(percentileOf(std::move(all), 99.7), -40.0);
            const double lowDb = top - 75.0;
            QImage si(spec.columns, spec.rows, QImage::Format_RGB32);
            for (int c = 0; c < spec.columns; ++c) {
                for (int r = 0; r < spec.rows; ++r)
                    si.setPixel(c, spec.rows - 1 - r, magma(float((spec.at(c, r) - lowDb) / 75.0)));
            }
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            p.drawImage(l.rect, si);
        } else {
            p.fillRect(l.rect, Qt::black);
        }
        const QFont tf = fontOf(fs * 0.8);
        const QPair<double, const char *> yt[] = {{50, "50"}, {100, "100"}, {200, "200"}, {500, "500"},
                                                  {1000, "1k"}, {2000, "2k"}, {5000, "5k"}, {10000, "10k"}};
        for (const auto &t : yt) {
            const double frac = std::log(t.first / 30.0) / std::log(16000.0 / 30.0);
            const double y = l.rect.bottom() - frac * l.rect.height();
            p.setPen(QPen(QColor(0x99, 0x99, 0x99), 1.0));
            p.drawLine(QPointF(l.rect.left() - 3, y), QPointF(l.rect.left(), y));
            text(QRectF(0, y - 10, left - 6, 20), Qt::AlignRight | Qt::AlignVCenter, QString::fromLatin1(t.second), tf,
                 QColor(0x33, 0x33, 0x33));
        }
        QSet<qint64> labelled;
        for (const PlacedLabel &pl : std::as_const(placed))
            labelled.insert(qint64(std::lround(labels[pl.index].seconds * 1000.0)));
        for (const AudioEvent &e : in.events) {
            const bool lab = labelled.contains(qint64(std::lround(e.seconds * 1000.0)));
            const double h = l.rect.height() * (lab ? 0.07 : 0.035);
            QColor c(255, 255, 255, lab ? 230 : 140);
            p.setPen(QPen(c, lab ? 1.4 : 0.9));
            p.drawLine(QPointF(xOf(e.seconds), l.rect.top()), QPointF(xOf(e.seconds), l.rect.top() + h));
        }
        frameLane(l);
        laneTitle(l, QStringLiteral("spectrogram (Hz, log)"), true);
    }

    // Waveform.
    {
        const Lane &l = lanes[QStringLiteral("wave")];
        const qint64 n = in.monoFrames;
        double pk = 0.0;
        double sc = 1e-9;
        if (in.mono && n > 0) {
            for (qint64 i = 0; i < n; ++i)
                pk = std::max(pk, double(std::abs(in.mono[i])));
            const qint64 stride = std::max<qint64>(1, n / 2'000'000);
            std::vector<float> a;
            for (qint64 i = 0; i < n; i += stride)
                a.push_back(std::abs(in.mono[i]));
            sc = std::max(percentileOf(std::move(a), 99.9), 1e-9);
            const int cols = std::max(1, int(plotW));
            const double mid = l.rect.center().y();
            const double half = l.rect.height() / 2.0 / 1.08;
            p.setPen(QPen(QColor(0x6b, 0x7a, 0x90), 1.0));
            for (int c = 0; c < cols; ++c) {
                const qint64 a0 = n * c / cols;
                const qint64 a1 = std::max(a0 + 1, n * (c + 1) / cols);
                float lo = 0.0f;
                float hi = 0.0f;
                for (qint64 i = a0; i < a1 && i < n; ++i) {
                    lo = std::min(lo, in.mono[i]);
                    hi = std::max(hi, in.mono[i]);
                }
                const double yl = mid - std::clamp(double(hi) / sc, -1.0, 1.0) * half;
                const double yh = mid - std::clamp(double(lo) / sc, -1.0, 1.0) * half;
                p.drawLine(QPointF(l.rect.left() + c + 0.5, yl), QPointF(l.rect.left() + c + 0.5, std::max(yh, yl + 1.0)));
            }
        }
        frameLane(l);
        const auto db = [](double v) { return 20.0 * std::log10(std::max(v, 1e-9)); };
        laneTitle(l, pk > 0.0 ? QStringLiteral("waveform (full height = %1 dBFS, peak %2 dBFS)").arg(db(sc), 0, 'f', 0).arg(db(pk), 0, 'f', 1)
                              : QStringLiteral("waveform (digital silence)"), true);
    }

    // Loudness.
    {
        const Lane &l = lanes[QStringLiteral("loud")];
        const auto yOf = [&](double v) { return l.rect.bottom() - (std::max(v, kLoudFloor) - kLoudFloor) / -kLoudFloor * l.rect.height(); };
        p.save();
        p.setClipRect(l.rect.adjusted(-1, -1, 1, 1));
        for (const LoudnessGap &g : lo.gaps) {
            const double a = xOf(std::max(t0 + g.start, t0));
            const double b = xOf(std::min(t0 + g.end, t1));
            p.fillRect(QRectF(a, l.rect.top(), b - a, l.rect.height()), QColor(0xff, 0xdd, 0x57, 115));
        }
        const auto curve = [&](const LoudnessCurve &c, const QColor &col, double lwPt) {
            if (c.lufs.isEmpty())
                return;
            QPainterPath path;
            for (int i = 0; i < c.lufs.size(); ++i) {
                const QPointF pt(xOf(t0 + c.t0 + c.dt * i), yOf(c.lufs[i]));
                i == 0 ? path.moveTo(pt) : path.lineTo(pt);
            }
            p.setPen(QPen(col, ptToPx(lwPt), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);
        };
        curve(lo.momentary, QColor(0x3d, 0x6f, 0xb6), 1.0);
        curve(lo.shortTerm, QColor(0x0b, 0x2e, 0x66), 2.4);
        if (lo.hasIntegrated && lo.integratedLufs > kLoudFloor) {
            QPen pen(QColor(0xd1, 0x49, 0x5b), ptToPx(1.2));
            pen.setStyle(Qt::DashLine);
            p.setPen(pen);
            p.drawLine(QPointF(l.rect.left(), yOf(lo.integratedLufs)), QPointF(l.rect.right(), yOf(lo.integratedLufs)));
        }
        p.setPen(QPen(Qt::red, 2.5));
        for (const ClippingRun &c : lo.clippingRuns) {
            const double x = xOf(t0 + c.start);
            p.drawLine(QPointF(x, yOf(-3)), QPointF(x, yOf(0)));
        }
        p.restore();
        frameLane(l);
        const QFont tf = fontOf(fs * 0.8);
        for (int v : {-50, -40, -30, -20, -10}) {
            p.setPen(QPen(QColor(0x99, 0x99, 0x99), 1.0));
            p.drawLine(QPointF(l.rect.left() - 3, yOf(v)), QPointF(l.rect.left(), yOf(v)));
            text(QRectF(0, yOf(v) - 10, left - 6, 20), Qt::AlignRight | Qt::AlignVCenter, QString::number(v), tf,
                 QColor(0x33, 0x33, 0x33));
        }
        if (lo.hasIntegrated && lo.integratedLufs > kLoudFloor) {
            text(QRectF(l.rect.right() + 4, yOf(lo.integratedLufs) - 10, right - 4, 20), Qt::AlignLeft | Qt::AlignVCenter,
                 QString::number(lo.integratedLufs, 'f', 1), tf, QColor(0xd1, 0x49, 0x5b));
        }
        const QFont lf = fontOf(fs * 0.78);
        double lx = l.rect.left() + 8;
        const double ly = l.rect.bottom() - 12;
        struct Key { QString name; QColor c; double w; };
        for (const Key &k : {Key{QStringLiteral("momentary"), QColor(0x3d, 0x6f, 0xb6), 1.0},
                             Key{QStringLiteral("short-term"), QColor(0x0b, 0x2e, 0x66), 2.4}}) {
            p.setPen(QPen(k.c, ptToPx(k.w)));
            p.drawLine(QPointF(lx, ly), QPointF(lx + 24, ly));
            const QFontMetrics fm(lf);
            text(QRectF(lx + 30, ly - 10, fm.horizontalAdvance(k.name) + 6, 20), Qt::AlignLeft | Qt::AlignVCenter, k.name,
                 lf, QColor(0x22, 0x22, 0x22));
            lx += 30 + fm.horizontalAdvance(k.name) + 22;
        }
        laneTitle(l, QStringLiteral("loudness (LUFS)"), true);
    }

    // Beats and sync.
    {
        const Lane &l = lanes[QStringLiteral("beats")];
        p.save();
        p.setClipRect(l.rect.adjusted(-1, -1, 1, 1));
        for (double b : in.beats) {
            if (b < t0 || b > t1)
                continue;
            bool down = false;
            for (double d : in.downbeats)
                down = down || std::abs(d - b) < 1e-6;
            p.setPen(QPen(down ? QColor(0x22, 0x22, 0x22) : QColor(0x66, 0x66, 0x66), 1.0));
            p.drawLine(QPointF(xOf(b), l.rect.bottom()), QPointF(xOf(b), l.rect.bottom() - l.rect.height() * (down ? 0.45 : 0.28)));
        }
        const double ms = ptToPx(fs * 0.45);
        for (const AudioSyncItem &it : in.sync.items) {
            const QPointF c(xOf(it.seconds), l.rect.bottom() - l.rect.height() * 0.6);
            QPolygonF d;
            d << QPointF(c.x(), c.y() - ms * 0.75) << QPointF(c.x() + ms * 0.75, c.y()) << QPointF(c.x(), c.y() + ms * 0.75)
              << QPointF(c.x() - ms * 0.75, c.y());
            p.setPen(Qt::NoPen);
            p.setBrush(statusColor(it.status));
            p.drawPolygon(d);
        }
        p.restore();
        frameLane(l);
        laneTitle(l, QStringLiteral("sync /\nbeats"), false);
    }

    // Time axis: ticks under every lane, labels under the last.
    const QFont af = fontOf(fs * 0.85);
    for (const Slot &s : laneSlots) {
        const Lane &l = lanes[s.name];
        p.setPen(QPen(QColor(0x99, 0x99, 0x99), 1.0));
        for (double t : ticks)
            p.drawLine(QPointF(xOf(t), l.rect.bottom()), QPointF(xOf(t), l.rect.bottom() + 3));
    }
    const Lane &last = lanes[QStringLiteral("beats")];
    for (double t : ticks) {
        text(QRectF(xOf(t) - 40, last.rect.bottom() + 4, 80, 22), Qt::AlignHCenter | Qt::AlignTop, tickLabel(t, step), af,
             QColor(0x22, 0x22, 0x22));
    }
    text(QRectF(left, last.rect.bottom() + 26, plotW, 22), Qt::AlignHCenter | Qt::AlignTop,
         QStringLiteral("time (m:ss)"), af, QColor(0x22, 0x22, 0x22));

    // Cuts, dashed through every lane but the label lane.
    {
        QPen pen(kCutColor, 1.5);
        pen.setDashPattern({4, 3});
        for (double c : in.cuts) {
            if (c < t0 || c > t1)
                continue;
            p.setPen(pen);
            for (const Slot &s : laneSlots) {
                if (s.name == QLatin1String("labels"))
                    continue;
                const Lane &l = lanes[s.name];
                p.drawLine(QPointF(xOf(c), l.rect.top()), QPointF(xOf(c), l.rect.bottom()));
            }
        }
    }
    p.end();
    return img;
}

} // namespace drift::audiosheet
