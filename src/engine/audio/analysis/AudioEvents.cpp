#include "engine/audio/analysis/AudioEvents.h"

#include "engine/audio/analysis/RealFft.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace drift {
namespace {

constexpr int kNFft = 1024;
constexpr int kHop = 240; // 5 ms at 48 kHz
constexpr int kBands = 96;
constexpr double kMelMinHz = 30.0;
constexpr double kMelMaxHz = 16000.0;
constexpr int kMedianWindow = 401; // 2 s of 5 ms frames
constexpr double kBedFloor = 1e-4; // beds under -80 dBFS count as -80
constexpr double kSwellRiseDb = 8.0;
// An event whose first stretch peaks under this is a boundary artefact of the detector (the
// band filters ringing at a gap), not sound anyone could hear.
constexpr double kMinEventPeakDbfs = -80.0;

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

double median(std::vector<double> v)
{
    return percentile(std::move(v), 50.0);
}

struct Section
{
    double b0, b1, b2, a1, a2, z1 = 0.0, z2 = 0.0;

    double process(double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

// One RBJ biquad; two of them with these Qs make a 4th-order Butterworth.
Section butterSection(double freq, double q, bool highPass)
{
    const double w0 = 2.0 * M_PI * freq / AudioEvents::kSampleRate;
    const double cosw = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    const double sign = highPass ? 1.0 : -1.0;
    const double b0 = (1.0 + sign * cosw) / 2.0 / a0;
    return {b0, sign * -(1.0 + sign * cosw) / a0, b0, -2.0 * cosw / a0, (1.0 - alpha) / a0};
}

std::vector<double> butter4(const std::vector<double> &x, double freq, bool highPass)
{
    Section s1 = butterSection(freq, 0.5411961, highPass);
    Section s2 = butterSection(freq, 1.3065630, highPass);
    std::vector<double> y(x.size());
    for (size_t i = 0; i < x.size(); ++i)
        y[i] = s2.process(s1.process(x[i]));
    return y;
}

// The signal with a reflected 100 ms lead-in, so a filter starts without a transient.
std::vector<double> reflectedLead(const float *m, qint64 n, int &pad)
{
    pad = int(std::min<qint64>(n - 1, AudioEvents::kSampleRate / 10));
    std::vector<double> out(static_cast<size_t>(n + pad));
    for (int i = 0; i < pad; ++i)
        out[size_t(i)] = m[pad - i];
    for (qint64 i = 0; i < n; ++i)
        out[size_t(pad + i)] = m[i];
    return out;
}

// BS.1770 K-weighting at 48 kHz.
struct KWeight
{
    Section shelf{1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, 0.73248077421585};
    Section high{1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621};
    double process(double x) { return high.process(shelf.process(x)); }
};

std::vector<float> medianFilter(const std::vector<float> &v, int window)
{
    const int n = int(v.size());
    const int half = window / 2;
    std::vector<float> out(static_cast<size_t>(n));
    if (n == 0)
        return out;
    const auto at = [&](int i) { return v[size_t(std::clamp(i, 0, n - 1))]; };
    std::vector<float> sorted;
    sorted.reserve(size_t(window));
    for (int k = -half; k <= half; ++k)
        sorted.push_back(at(k));
    std::sort(sorted.begin(), sorted.end());
    for (int i = 0; i < n; ++i) {
        out[size_t(i)] = sorted[size_t(half)];
        const float leaving = at(i - half);
        const float entering = at(i + half + 1);
        sorted.erase(std::lower_bound(sorted.begin(), sorted.end(), leaving));
        sorted.insert(std::lower_bound(sorted.begin(), sorted.end(), entering), entering);
    }
    return out;
}

struct MelBank
{
    std::vector<int> first;
    std::vector<std::vector<float>> weights;
};

MelBank makeMelBank()
{
    const auto mel = [](double f) { return 2595.0 * std::log10(1.0 + f / 700.0); };
    std::vector<double> edges(kBands + 2);
    for (int i = 0; i < kBands + 2; ++i) {
        const double m = mel(kMelMinHz) + (mel(kMelMaxHz) - mel(kMelMinHz)) * i / (kBands + 1);
        edges[size_t(i)] = 700.0 * (std::pow(10.0, m / 2595.0) - 1.0);
    }
    MelBank bank;
    const double binHz = double(AudioEvents::kSampleRate) / kNFft;
    for (int b = 0; b < kBands; ++b) {
        const double lo = edges[size_t(b)], c = edges[size_t(b) + 1], hi = edges[size_t(b) + 2];
        int first = -1;
        std::vector<float> w;
        for (int k = 0; k <= kNFft / 2; ++k) {
            const double f = k * binHz;
            const double v = std::max(0.0, std::min((f - lo) / (c - lo), (hi - f) / (hi - c)));
            if (v > 0.0) {
                if (first < 0)
                    first = k;
                w.push_back(float(v));
            } else if (first >= 0) {
                break;
            }
        }
        bank.first.push_back(std::max(first, 0));
        bank.weights.push_back(std::move(w));
    }
    return bank;
}

// Superflux onset strength at 5 ms frames: mean positive log-mel increase over the previous
// frame pair, the reference taken as the maximum across neighbouring bands.
std::vector<float> onsetCurve(const float *m, qint64 n, bool &ok)
{
    ok = false;
    const int frames = int(1 + n / kHop);
    RealFft fft(kNFft);
    if (!fft.ok())
        return {};
    ok = true;
    const MelBank bank = makeMelBank();
    std::vector<float> hann(kNFft);
    for (int i = 0; i < kNFft; ++i)
        hann[size_t(i)] = float(0.5 - 0.5 * std::cos(2.0 * M_PI * i / (kNFft - 1)));

    const bool reflect = n > kNFft;
    const auto sample = [&](qint64 i) -> float {
        if (!reflect)
            return i >= 0 && i < n ? m[i] : 0.0f;
        if (i < 0)
            i = -i;
        if (i >= n)
            i = 2 * (n - 1) - i;
        return m[std::clamp<qint64>(i, 0, n - 1)];
    };

    std::vector<float> logMel(static_cast<size_t>(frames) * kBands);
    for (int t = 0; t < frames; ++t) {
        const qint64 start = qint64(t) * kHop - kNFft / 2;
        for (int i = 0; i < kNFft; ++i)
            fft.in()[i] = sample(start + i) * hann[size_t(i)];
        fft.run();
        const float *c = fft.out();
        for (int b = 0; b < kBands; ++b) {
            double acc = 0.0;
            const std::vector<float> &w = bank.weights[size_t(b)];
            for (size_t k = 0; k < w.size(); ++k) {
                const int bin = bank.first[size_t(b)] + int(k);
                acc += double(w[k]) * (double(c[2 * bin]) * c[2 * bin] + double(c[2 * bin + 1]) * c[2 * bin + 1]);
            }
            logMel[size_t(t) * kBands + size_t(b)] = float(10.0 * std::log10(acc + 1e-12));
        }
    }
    float top = -1e30f;
    for (float v : logMel)
        top = std::max(top, v);
    const float floorDb = top - 80.0f;
    for (float &v : logMel)
        v = std::max(v, floorDb);

    std::vector<float> odf(static_cast<size_t>(frames), 0.0f);
    std::vector<float> ref(kBands);
    for (int t = 2; t < frames; ++t) {
        const float *old = &logMel[size_t(t - 2) * kBands];
        for (int b = 0; b < kBands; ++b)
            ref[size_t(b)] = std::max({old[std::max(b - 1, 0)], old[b], old[std::min(b + 1, kBands - 1)]});
        const float *cur = &logMel[size_t(t) * kBands];
        double sum = 0.0;
        for (int b = 0; b < kBands; ++b)
            sum += std::max(0.0f, cur[b] - ref[size_t(b)]);
        odf[size_t(t)] = float(sum / kBands);
    }
    return odf;
}

std::vector<float> robustZ(const std::vector<float> &v)
{
    const std::vector<float> med = medianFilter(v, kMedianWindow);
    std::vector<float> dev(v.size());
    for (size_t i = 0; i < v.size(); ++i)
        dev[i] = std::abs(v[i] - med[i]);
    const std::vector<float> mad = medianFilter(dev, kMedianWindow);
    std::vector<double> pos;
    for (float x : v) {
        if (x > 0.0f)
            pos.push_back(x);
    }
    const double floorV = pos.empty() ? 1e-6 : 0.02 * percentile(std::move(pos), 99.0);
    std::vector<float> z(v.size());
    for (size_t i = 0; i < v.size(); ++i)
        z[i] = float((v[i] - med[i]) / (mad[i] * 1.4826 + floorV + 1e-9));
    return z;
}

struct BandEnvelopes
{
    std::vector<double> full, low, high; // 1 ms max-abs
    const std::vector<double> *byName(int i) const { return i == 0 ? &full : (i == 1 ? &low : &high); }
};

std::vector<double> maxAbsMs(const std::vector<double> &y, size_t offset, size_t n)
{
    constexpr int w = AudioEvents::kSampleRate / 1000;
    std::vector<double> env(n / w);
    for (size_t b = 0; b < env.size(); ++b) {
        double peak = 0.0;
        for (int i = 0; i < w; ++i)
            peak = std::max(peak, std::abs(y[offset + b * w + size_t(i)]));
        env[b] = peak + 1e-7;
    }
    return env;
}

BandEnvelopes bandEnvelopes(const float *m, qint64 n)
{
    BandEnvelopes e;
    std::vector<double> mono(m, m + n);
    e.full = maxAbsMs(mono, 0, size_t(n));
    int pad = 0;
    const std::vector<double> lead = reflectedLead(m, n, pad);
    e.low = maxAbsMs(butter4(lead, 200.0, false), size_t(pad), size_t(n));
    e.high = maxAbsMs(butter4(lead, 2000.0, true), size_t(pad), size_t(n));
    return e;
}

const char *kBandNames[3] = {"full", "low", "high"};

struct Attack
{
    double ratio;
    int bestBand;
    int j;
    int ip;
    double bed;
    int timingBand;
};

bool findAttack(const BandEnvelopes &envs, double tc, Attack &out)
{
    const int ic = int(std::lround(tc * 1000.0));
    struct Res { double ratio; int band; int j; int ip; double bed; };
    std::vector<Res> res;
    for (int band = 0; band < 3; ++band) {
        const std::vector<double> &e = *envs.byName(band);
        const int n = int(e.size());
        const int a = std::max(0, ic - 15);
        const int b = std::min(n, ic + 50);
        if (b - a < 5)
            continue;
        int ip = a;
        for (int i = a; i < b; ++i) {
            if (e[size_t(i)] > e[size_t(ip)])
                ip = i;
        }
        std::vector<double> pre;
        for (int i = std::max(0, ic - 160); i < std::max(0, ic - 40) && i < n; ++i)
            pre.push_back(e[size_t(i)]);
        if (pre.size() < 20) {
            pre.clear();
            for (int i = 0; i < std::min(n, ic + 250); ++i)
                pre.push_back(e[size_t(i)]);
        }
        const double bed = std::max(median(pre), kBedFloor);
        const double thr = bed + 0.1 * (e[size_t(ip)] - bed);
        int j = ip;
        const int lo = std::max(0, ic - 40);
        while (j > lo && e[size_t(j - 1)] >= thr)
            --j;
        res.push_back({e[size_t(ip)] / bed, band, j, ip, bed});
    }
    if (res.empty())
        return false;
    // Ties on the ratio fall to the later band name, as comparing (ratio, name, ...) does.
    const auto better = [](const Res &a, const Res &b) {
        if (a.ratio != b.ratio)
            return a.ratio > b.ratio;
        return std::string(kBandNames[a.band]) > std::string(kBandNames[b.band]);
    };
    const Res best = *std::min_element(res.begin(), res.end(), better);
    Res timing = best;
    for (const Res &r : res) {
        if (r.band == 0 && r.ratio >= 4.0)
            timing = r;
    }
    out = {best.ratio, best.band, timing.j, timing.ip, timing.bed, timing.band};
    return true;
}

int lengthMs(const std::vector<double> &e, int i0, int ip, double bed)
{
    const double floorV = std::max(e[size_t(ip)] * 0.1, bed * 1.41);
    const int end = std::min(int(e.size()), ip + 2000);
    int run = 0;
    int k = end - ip;
    for (int i = ip; i < end; ++i) {
        run = e[size_t(i)] < floorV ? run + 1 : 0;
        if (run == 30) {
            k = i - 29 - ip;
            break;
        }
    }
    return std::min(ip + k - i0, 2000);
}

std::vector<float> uniformFilter9(const std::vector<float> &v)
{
    const int n = int(v.size());
    std::vector<float> out(static_cast<size_t>(n));
    const auto at = [&](int i) {
        if (i < 0)
            i = -i - 1;
        if (i >= n)
            i = 2 * n - i - 1;
        return v[size_t(std::clamp(i, 0, n - 1))];
    };
    for (int i = 0; i < n; ++i) {
        double sum = 0.0;
        for (int k = -4; k <= 4; ++k)
            sum += at(i + k);
        out[size_t(i)] = float(sum / 9.0);
    }
    return out;
}

AudioLevelEnvelopes levelEnvelopes(const float *m, qint64 n)
{
    AudioLevelEnvelopes out;
    const qint64 frames = n / 480;
    if (frames < 3)
        return out;
    int pad = 0;
    const std::vector<double> hi = butter4(reflectedLead(m, n, pad), 2000.0, true);
    const auto levels = [&](auto sampleAt) {
        std::vector<float> r(static_cast<size_t>(frames));
        for (qint64 f = 0; f < frames; ++f) {
            double sum = 0.0;
            for (int i = 0; i < 480; ++i) {
                const double v = sampleAt(f * 480 + i);
                sum += v * v;
            }
            r[size_t(f)] = float(std::max(20.0 * std::log10(std::sqrt(sum / 480.0) + 1e-9), -80.0));
        }
        return uniformFilter9(r);
    };
    const std::vector<float> full = levels([&](qint64 i) { return double(m[i]); });
    const std::vector<float> high = levels([&](qint64 i) { return hi[size_t(pad + i)]; });
    out.full = QVector<float>(full.begin(), full.end());
    out.high = QVector<float>(high.begin(), high.end());
    return out;
}

bool isLocalMax(const QVector<float> &s, int i, int half)
{
    const int n = s.size();
    for (int k = std::max(0, i - half); k <= std::min(n - 1, i + half); ++k) {
        if (s[k] > s[i])
            return false;
    }
    return true;
}

QList<AudioEvent> findSwells(const AudioLevelEnvelopes &levels, const QList<AudioEvent> &hits)
{
    QList<AudioEvent> out;
    if (levels.full.size() < 60)
        return out;
    const QVector<float> *bands[2] = {&levels.full, &levels.high};
    const char *names[2] = {"full", "high"};
    for (int bi = 0; bi < 2; ++bi) {
        const QVector<float> &s = *bands[bi];
        const int n = s.size();
        for (int i = 0; i < n; ++i) {
            if (s[i] <= -70.0f || !isLocalMax(s, i, 15))
                continue;
            const int a = std::max(0, i - 200);
            if (i - a < 25)
                continue;
            int j = a;
            for (int k = a; k < i - 15; ++k) {
                if (s[k] < s[j])
                    j = k;
            }
            const double rise = double(s[i]) - s[j];
            const double dur = (i - j) / 100.0;
            const int afterEnd = std::min(n, i + 100);
            if (rise < kSwellRiseDb || dur < 0.25 || afterEnd - i < 10)
                continue;
            float afterMin = s[i];
            for (int k = i; k < afterEnd; ++k)
                afterMin = std::min(afterMin, s[k]);
            if (double(s[i]) - afterMin < 4.0)
                continue;
            int rising = 0;
            for (int k = j; k < i; ++k)
                rising += s[k + 1] > s[k];
            if (double(rising) / double(i - j) < 0.6)
                continue;
            const double t = i / 100.0;
            bool nearHit = false;
            for (const AudioEvent &h : hits)
                nearHit = nearHit || std::abs((h.seconds - levels.t0) - (t - 0.03)) < 0.05;
            if (nearHit)
                continue;
            bool dup = false;
            for (const AudioEvent &o : out)
                dup = dup || std::abs((o.seconds - levels.t0) - t) < 0.15;
            if (dup)
                continue;
            int fall = 1000;
            for (int k = i; k < afterEnd; ++k) {
                if (s[k] < s[i] - 10.0f) {
                    fall = (k - i) * 10;
                    break;
                }
            }
            AudioEvent e;
            e.seconds = levels.t0 + t;
            e.kind = AudioEvent::Kind::Swell;
            e.band = QString::fromLatin1(names[bi]);
            e.contrastDb = rise;
            e.prominenceDb = rise;
            e.bedDb = s[j];
            e.lengthMs = fall;
            out.append(e);
        }
    }
    return out;
}

} // namespace

AudioEventsResult AudioEvents::analyze(const float *mono, qint64 frameCount, int sampleRate,
                                       double startSeconds, const AudioEventsOptions &options)
{
    AudioEventsResult result;
    result.levels.t0 = startSeconds;
    if (!mono || sampleRate != kSampleRate || frameCount < qint64(kMinAnalysisSec * kSampleRate))
        return result;
    const qint64 n = frameCount;

    bool fftOk = false;
    const std::vector<float> odf = onsetCurve(mono, n, fftOk);
    if (!fftOk)
        return result;
    const std::vector<float> z = robustZ(odf);
    const BandEnvelopes envs = bandEnvelopes(mono, n);

    QList<AudioEvent> hits;
    const int frames = int(odf.size());
    for (int i = 0; i < frames; ++i) {
        if (odf[size_t(i)] <= 0.0f || z[size_t(i)] < options.zThreshold)
            continue;
        bool peak = true;
        for (int k = std::max(0, i - 5); k <= std::min(frames - 1, i + 5) && peak; ++k)
            peak = odf[size_t(k)] <= odf[size_t(i)];
        if (!peak)
            continue;
        Attack att;
        if (!findAttack(envs, double(i) * kHop / kSampleRate, att))
            continue;
        const double contrast = 20.0 * std::log10(att.ratio);
        if (contrast < options.minContrastDb)
            continue;
        AudioEvent e;
        e.seconds = att.j / 1000.0;
        e.band = QString::fromLatin1(kBandNames[att.bestBand]);
        e.contrastDb = std::min(contrast, 60.0);
        e.z = std::min<double>(z[size_t(i)], 99.0);
        e.lengthMs = lengthMs(*envs.byName(att.timingBand), att.j, att.ip, att.bed);
        hits.append(e);
    }
    std::sort(hits.begin(), hits.end(),
              [](const AudioEvent &a, const AudioEvent &b) { return a.seconds < b.seconds; });
    QList<AudioEvent> merged;
    for (const AudioEvent &e : std::as_const(hits)) {
        if (!merged.isEmpty() && e.seconds - merged.last().seconds < options.minGapSeconds) {
            if (e.contrastDb > merged.last().contrastDb)
                merged.last() = e;
            continue;
        }
        merged.append(e);
    }

    bool startsFile = false;
    const std::vector<double> &full = envs.full;
    if (options.leadIn && full.size() > 5) {
        const size_t head = std::min<size_t>(50, full.size());
        double hmax = 0.0;
        size_t ip = 0;
        for (size_t i = 0; i < head; ++i) {
            if (full[i] > hmax) {
                hmax = full[i];
                ip = i;
            }
        }
        const bool early = !merged.isEmpty() && merged.first().seconds < 0.05;
        if (hmax >= 1e-3 && !early) {
            size_t j = 0;
            while (j < ip && full[j] < 0.1 * full[ip])
                ++j;
            AudioEvent e;
            e.seconds = double(j) / 1000.0;
            e.band = QStringLiteral("full");
            e.contrastDb = 60.0;
            e.lengthMs = lengthMs(full, int(j), int(ip), kBedFloor);
            merged.prepend(e);
            startsFile = true;
        }
    }

    // K-weighted 10 ms power in dB, one value per millisecond of window start. Dual-mono, so the
    // two channels of BS.1770 add up to twice the mono power.
    const size_t blocks = size_t((n + 47) / 48);
    std::vector<double> blockPower(blocks, 0.0);
    {
        KWeight kw;
        for (qint64 i = 0; i < n; ++i) {
            const double y = kw.process(mono[i]);
            blockPower[size_t(i / 48)] += 2.0 * y * y;
        }
    }
    const size_t kwCount = size_t(std::max<qint64>(1, (std::max<qint64>(1, n - 480) + 47) / 48));
    std::vector<double> kw(kwCount);
    for (size_t i = 0; i < kwCount; ++i) {
        double sum = 0.0;
        for (size_t k = i; k < std::min(blocks, i + 10); ++k)
            sum += blockPower[k];
        kw[i] = 10.0 * std::log10(sum / 480.0 + 1e-12);
    }
    for (size_t idx = 0; idx < size_t(merged.size()); ++idx) {
        AudioEvent &e = merged[qsizetype(idx)];
        const int j = int(std::lround(e.seconds * 1000.0));
        const bool first = startsFile && idx == 0;
        double bed = -80.0;
        if (!first) {
            std::vector<double> pre;
            for (int k = std::max(0, j - 300); k < std::max(0, j - 20) && k < int(kw.size()); ++k)
                pre.push_back(kw[size_t(k)]);
            if (pre.size() >= 20) {
                bed = median(pre);
            } else {
                std::vector<double> head(kw.begin(), kw.begin() + std::min<size_t>(kw.size(), size_t(j) + 2000));
                bed = percentile(std::move(head), 10.0);
            }
            bed = std::max(bed, -80.0);
        }
        double prominence = 0.0;
        if (j < int(kw.size())) {
            double hi = -1e30;
            for (int k = j; k < std::min(int(kw.size()), j + 80); ++k)
                hi = std::max(hi, kw[size_t(k)]);
            prominence = std::max(0.0, hi - bed);
        }
        e.prominenceDb = prominence;
        e.bedDb = bed;
    }

    result.levels = levelEnvelopes(mono, n);
    result.levels.t0 = 0.0;
    QList<AudioEvent> swells = findSwells(result.levels, merged);
    merged += swells;
    std::sort(merged.begin(), merged.end(),
              [](const AudioEvent &a, const AudioEvent &b) { return a.seconds < b.seconds; });

    QList<AudioEvent> kept;
    for (AudioEvent e : std::as_const(merged)) {
        const qint64 a = qint64(e.seconds * kSampleRate);
        if (a >= n)
            continue;
        const qint64 b = std::min<qint64>(
            n, std::max(a + 1, a + qint64(std::max(0.05, std::min(e.lengthMs / 1000.0, 0.3)) * kSampleRate)));
        float peak = 0.0f;
        for (qint64 i = a; i < b; ++i)
            peak = std::max(peak, std::abs(mono[i]));
        e.levelDbfs = peak > 0.0f ? 20.0 * std::log10(double(peak)) : -120.0;
        if (e.levelDbfs < kMinEventPeakDbfs)
            continue;
        e.strength = float(std::clamp(e.prominenceDb / 30.0, 0.0, 1.0));
        e.seconds += startSeconds;
        kept.append(e);
    }
    result.events = kept;
    result.levels.t0 = startSeconds;
    return result;
}

} // namespace drift
