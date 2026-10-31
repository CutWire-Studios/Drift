#include "engine/audio/analysis/AudioSync.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace drift {
namespace {

constexpr double kSyncSearchSeconds = 0.25;
constexpr double kOnBeatSeconds = 0.05;
constexpr double kAccentDb = 6.0;
constexpr double kPeakRadius = 0.15;
constexpr double kPeakRiseDb = 3.0;
constexpr double kPeakFallDb = 2.0;
constexpr double kPeakLocalSeconds = 0.3;

double percentile(std::vector<double> v, double pct)
{
    if (v.empty())
        return 0.0;
    std::sort(v.begin(), v.end());
    const double rank = pct / 100.0 * double(v.size() - 1);
    const size_t lo = size_t(rank);
    const size_t hi = std::min(lo + 1, v.size() - 1);
    return v[lo] + (v[hi] - v[lo]) * (rank - double(lo));
}

double round3(double v)
{
    return std::round(v * 1000.0) / 1000.0;
}

using EventPtrs = std::vector<const AudioEvent *>; // sorted by time

const AudioEvent *nearest(double t, const EventPtrs &evs, double radius)
{
    const AudioEvent *best = nullptr;
    for (const AudioEvent *e : evs) {
        const double d = std::abs(e->seconds - t);
        if (d <= radius && (!best || d < std::abs(best->seconds - t)))
            best = e;
    }
    return best;
}

struct LevelPeak
{
    double t = 0.0;
    double riseDb = 0.0;
};

bool levelPeak(const AudioLevelEnvelopes &levels, double t, LevelPeak &out)
{
    bool found = false;
    const QVector<float> *bands[2] = {&levels.full, &levels.high};
    for (const QVector<float> *bp : bands) {
        const QVector<float> &s = *bp;
        const int n = s.size();
        const int c = int(std::lround((t - levels.t0) * 100.0));
        const int radius = int(kPeakRadius * 100);
        const int a = std::max(0, c - radius);
        const int b = std::min(n, c + radius + 1);
        if (b - a < 3)
            continue;
        int i = a;
        for (int k = a; k < b; ++k) {
            if (s[k] > s[i])
                i = k;
        }
        const int w = int(kPeakLocalSeconds * 100);
        float localMax = s[std::max(0, i - w)];
        for (int k = std::max(0, i - w); k < std::min(n, i + w + 1); ++k)
            localMax = std::max(localMax, s[k]);
        if (s[i] < localMax || i < 5)
            continue;
        float before = s[i];
        for (int k = std::max(0, i - 60); k < i; ++k)
            before = std::min(before, s[k]);
        const double rise = double(s[i]) - before;
        const int afterEnd = std::min(n, i + 31);
        float after = s[i];
        for (int k = i; k < afterEnd; ++k)
            after = std::min(after, s[k]);
        if (rise < kPeakRiseDb || afterEnd - i < 5 || double(s[i]) - after < kPeakFallDb)
            continue;
        if (!found || rise > out.riseDb) {
            out = {levels.t0 + i / 100.0, rise};
            found = true;
        }
    }
    return found;
}

// Fraction of random times in [lo, hi] that fall within tol of an event.
double chanceOf(const EventPtrs &evs, double lo, double hi, double tol)
{
    if (evs.empty())
        return 0.0;
    int hits = 0;
    int total = 0;
    size_t k = 0;
    for (double g = lo; g < hi; g += 0.005, ++total) {
        while (k < evs.size() && evs[k]->seconds < g)
            ++k;
        double d = 1e30;
        if (k < evs.size())
            d = std::min(d, std::abs(evs[k]->seconds - g));
        if (k > 0)
            d = std::min(d, std::abs(evs[k - 1]->seconds - g));
        hits += d <= tol;
    }
    return total > 0 ? round3(double(hits) / total) : 0.0;
}

} // namespace

QString syncStatusName(AudioSyncItem::Status status)
{
    switch (status) {
    case AudioSyncItem::Status::Ok: return QStringLiteral("ok");
    case AudioSyncItem::Status::Near: return QStringLiteral("near");
    case AudioSyncItem::Status::Swell: return QStringLiteral("swell");
    case AudioSyncItem::Status::Peak: return QStringLiteral("peak");
    case AudioSyncItem::Status::Off: return QStringLiteral("off");
    case AudioSyncItem::Status::NoSound: break;
    }
    return QStringLiteral("no_sound");
}

AudioSyncResult matchSync(const QList<double> &cues, const QList<AudioEvent> &eventList,
                          const AudioSyncOptions &options)
{
    AudioSyncResult result;
    EventPtrs all;
    for (const AudioEvent &e : eventList)
        all.push_back(&e);
    std::sort(all.begin(), all.end(), [](const AudioEvent *a, const AudioEvent *b) { return a->seconds < b->seconds; });

    EventPtrs swells;
    EventPtrs strong;
    for (const AudioEvent *e : all) {
        if (e->kind == AudioEvent::Kind::Swell)
            swells.push_back(e);
    }
    if (!all.empty()) {
        std::vector<double> prom;
        for (const AudioEvent *e : all)
            prom.push_back(e->prominenceDb);
        const double thr = std::min(6.0, percentile(prom, 40.0));
        for (const AudioEvent *e : all) {
            if (e->prominenceDb >= thr)
                strong.push_back(e);
        }
    }
    EventPtrs accents;
    double accentDb = 0.0;
    if (!all.empty()) {
        std::vector<double> prom;
        for (const AudioEvent *e : all)
            prom.push_back(e->prominenceDb);
        accentDb = std::max(kAccentDb, percentile(prom, 80.0));
        for (const AudioEvent *e : all) {
            if (e->prominenceDb >= accentDb)
                accents.push_back(e);
        }
    }

    const double tol = options.toleranceSeconds;
    const std::vector<double> beats(options.beats.begin(), options.beats.end());
    for (double t : cues) {
        AudioSyncItem it;
        it.seconds = t;
        struct Stage { const EventPtrs *evs; double radius; AudioSyncItem::Status status; };
        const Stage stages[] = {{&all, tol, AudioSyncItem::Status::Ok},
                                {&strong, 0.1, AudioSyncItem::Status::Near},
                                {&all, 0.1, AudioSyncItem::Status::Near},
                                {&swells, 0.3, AudioSyncItem::Status::Swell}};
        const AudioEvent *answer = nullptr;
        for (const Stage &s : stages) {
            answer = nearest(t, *s.evs, s.radius);
            if (answer) {
                it.status = s.status;
                break;
            }
        }
        double answerAt = 0.0;
        double answerProm = 0.0;
        bool answered = answer != nullptr;
        if (answer) {
            answerAt = answer->seconds;
            answerProm = answer->prominenceDb;
        } else if (LevelPeak peak; options.levels && levelPeak(*options.levels, t, peak)) {
            answered = true;
            answerAt = peak.t;
            answerProm = peak.riseDb;
            it.status = AudioSyncItem::Status::Peak;
        } else if (const AudioEvent *off = nearest(t, strong, kSyncSearchSeconds)) {
            answered = true;
            answerAt = off->seconds;
            answerProm = off->prominenceDb;
            it.status = AudioSyncItem::Status::Off;
        }
        if (answered) {
            it.answered = true;
            it.offsetMs = (answerAt - t) * 1000.0;
            it.eventSeconds = answerAt;
            it.eventProminenceDb = answerProm;
        } else {
            it.status = AudioSyncItem::Status::NoSound;
        }
        if (const AudioEvent *a = nearest(t, accents, 0.1)) {
            it.hasAccent = true;
            it.accentOffsetMs = (a->seconds - t) * 1000.0;
        }
        if (!beats.empty()) {
            double best = 1e30;
            for (double b : beats) {
                if (std::abs(t - b) < std::abs(best))
                    best = t - b;
            }
            it.hasBeat = true;
            it.beatOffsetMs = best * 1000.0;
        }
        result.items.append(it);
    }

    AudioSyncStats &st = result.stats;
    st.n = result.items.size();
    st.toleranceMs = tol * 1000.0;
    if (st.n == 0)
        return result;

    std::vector<double> offsets;
    double okCount = 0.0;
    double nearCount = 0.0;
    for (const AudioSyncItem &it : std::as_const(result.items)) {
        if (it.answered)
            offsets.push_back(std::abs(it.offsetMs));
        okCount += it.status == AudioSyncItem::Status::Ok;
        nearCount += it.status == AudioSyncItem::Status::Ok || it.status == AudioSyncItem::Status::Near;
        st.answeredBySwell += it.status == AudioSyncItem::Status::Swell;
        st.answeredByLevelPeak += it.status == AudioSyncItem::Status::Peak;
        if (it.status == AudioSyncItem::Status::NoSound || it.status == AudioSyncItem::Status::Off)
            st.noSound.append(it.seconds);
    }
    st.hitWithinTolerance = round3(okCount / st.n);
    st.hitWithin100 = round3(nearCount / st.n);
    if (!offsets.empty()) {
        st.hasMedianOffset = true;
        st.medianAbsOffsetMs = std::round(percentile(offsets, 50.0) * 10.0) / 10.0;
    }

    double lo = 1e30;
    double hi = -1e30;
    for (const AudioSyncItem &it : std::as_const(result.items)) {
        lo = std::min(lo, it.seconds);
        hi = std::max(hi, it.seconds);
    }
    lo -= 1.0;
    hi += 1.0;
    if (!all.empty()) {
        st.hasChance = true;
        st.chanceTolerance = chanceOf(all, lo, hi, tol);
        st.chance100 = chanceOf(all, lo, hi, 0.1);
    }
    if (!accents.empty()) {
        st.hasAccents = true;
        st.accentMinDb = std::round(accentDb * 10.0) / 10.0;
        int within = 0;
        int within100 = 0;
        for (const AudioSyncItem &it : std::as_const(result.items)) {
            const double ao = it.hasAccent ? std::abs(it.accentOffsetMs) : 1e9;
            within += ao <= tol * 1000.0 + 1e-6;
            within100 += ao <= 100.0;
        }
        st.accentWithinTolerance = round3(double(within) / st.n);
        st.accentWithin100 = round3(double(within100) / st.n);
        st.accentChanceTolerance = chanceOf(accents, lo, hi, tol);
        st.accentChance100 = chanceOf(accents, lo, hi, 0.1);
    }
    if (beats.size() > 1) {
        st.hasBeats = true;
        int on = 0;
        for (const AudioSyncItem &it : std::as_const(result.items))
            on += std::abs(it.beatOffsetMs) <= kOnBeatSeconds * 1000.0;
        st.onBeat = round3(double(on) / st.n);
        std::vector<double> gaps;
        for (size_t i = 1; i < beats.size(); ++i)
            gaps.push_back(beats[i] - beats[i - 1]);
        st.onBeatChance = round3(std::min(1.0, 2.0 * kOnBeatSeconds / percentile(gaps, 50.0)));
    }
    return result;
}

} // namespace drift
