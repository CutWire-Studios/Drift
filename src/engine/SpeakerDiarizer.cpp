#include "SpeakerDiarizer.h"

#include "GpuPackageParse.h"
#include "KaldiFbank.h"
#include "OrtSupport.h"
#include "SpeechAudio.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

namespace drift {

namespace {

constexpr const char *kSegmentationFile = "segmentation.onnx";
constexpr const char *kEmbeddingFile = "embedding.onnx";
// Beyond this many (window, speaker) embeddings the O(n²) distance matrix gets large; the rest
// are assigned to the nearest cluster centre instead of clustered.
constexpr int kMaxClusteredEmbeddings = 3000;

QString resolveModelDir()
{
    const QStringList roots = GpuPackageParse::defaultSearchPaths(
        QStringLiteral("DRIFT_DIARIZE_MODEL_DIR"), QStringLiteral("models/diarization"),
        QStringLiteral("diarize-model"));
    for (const QString &root : roots) {
        const QDir dir(root);
        if (QFile::exists(dir.filePath(QLatin1String(kSegmentationFile)))
            && QFile::exists(dir.filePath(QLatin1String(kEmbeddingFile))))
            return root;
    }
    return {};
}

int metaInt(Ort::Session &session, const char *key, int fallback)
{
    Ort::AllocatorWithDefaultOptions alloc;
    Ort::ModelMetadata meta = session.GetModelMetadata();
    Ort::AllocatedStringPtr value = meta.LookupCustomMetadataMapAllocated(key, alloc);
    if (!value)
        return fallback;
    bool ok = false;
    const int v = QString::fromUtf8(value.get()).toInt(&ok);
    return ok ? v : fallback;
}

QString metaString(Ort::Session &session, const char *key)
{
    Ort::AllocatorWithDefaultOptions alloc;
    Ort::ModelMetadata meta = session.GetModelMetadata();
    Ort::AllocatedStringPtr value = meta.LookupCustomMetadataMapAllocated(key, alloc);
    return value ? QString::fromUtf8(value.get()) : QString();
}

using Labels = std::vector<std::vector<int>>; // frames x speakers, 0/1

} // namespace

std::vector<int> clusterEmbeddings(std::vector<float> e, int rows, int dim, int numClusters, float threshold)
{
    if (rows <= 0)
        return {};
    if (rows == 1)
        return {0};
    for (int i = 0; i < rows; ++i) {
        float *v = e.data() + static_cast<size_t>(i) * dim;
        double n = 0.0;
        for (int k = 0; k < dim; ++k)
            n += double(v[k]) * v[k];
        const float inv = n > 0 ? static_cast<float>(1.0 / std::sqrt(n)) : 0.0f;
        for (int k = 0; k < dim; ++k)
            v[k] *= inv;
    }
    auto distance = [&](int a, int b) {
        const float *x = e.data() + static_cast<size_t>(a) * dim;
        const float *y = e.data() + static_cast<size_t>(b) * dim;
        double dot = 0.0;
        for (int k = 0; k < dim; ++k)
            dot += double(x[k]) * y[k];
        return static_cast<float>(std::max(0.0, 1.0 - dot));
    };

    const int m = std::min(rows, kMaxClusteredEmbeddings);
    // Evenly spaced subset when there are too many to cluster directly.
    std::vector<int> pick(m);
    for (int i = 0; i < m; ++i)
        pick[i] = static_cast<int>(static_cast<int64_t>(i) * rows / m);

    std::vector<float> dist(static_cast<size_t>(m) * m, 0.0f);
    for (int i = 0; i < m; ++i)
        for (int j = i + 1; j < m; ++j)
            dist[static_cast<size_t>(i) * m + j] = dist[static_cast<size_t>(j) * m + i] = distance(pick[i], pick[j]);

    // Nearest-neighbour chain: O(m²) complete linkage (Lance–Williams: d(k, i∪j) = max).
    struct Merge
    {
        int a, b;
        float height;
    };
    std::vector<Merge> merges;
    std::vector<char> active(m, 1);
    std::vector<int> chain;
    chain.reserve(m);
    int remaining = m;
    while (remaining > 1) {
        if (chain.empty()) {
            for (int i = 0; i < m; ++i) {
                if (active[i]) {
                    chain.push_back(i);
                    break;
                }
            }
        }
        const int top = chain.back();
        const int prev = chain.size() > 1 ? chain[chain.size() - 2] : -1;
        int best = -1;
        float bestD = std::numeric_limits<float>::infinity();
        for (int j = 0; j < m; ++j) {
            if (j == top || !active[j])
                continue;
            const float dj = dist[static_cast<size_t>(top) * m + j];
            // Ties go to the previous chain element so the chain terminates.
            if (dj < bestD || (dj == bestD && j == prev)) {
                bestD = dj;
                best = j;
            }
        }
        if (best == prev) {
            chain.pop_back();
            chain.pop_back();
            const int keep = std::min(top, prev);
            const int gone = std::max(top, prev);
            merges.push_back({keep, gone, bestD});
            for (int k = 0; k < m; ++k) {
                if (!active[k] || k == keep || k == gone)
                    continue;
                const float v = std::max(dist[static_cast<size_t>(keep) * m + k], dist[static_cast<size_t>(gone) * m + k]);
                dist[static_cast<size_t>(keep) * m + k] = dist[static_cast<size_t>(k) * m + keep] = v;
            }
            active[gone] = 0;
            --remaining;
        } else {
            chain.push_back(best);
        }
    }

    // Complete linkage is monotone, so the clusters at a cut are exactly the merges below it.
    std::sort(merges.begin(), merges.end(), [](const Merge &a, const Merge &b) { return a.height < b.height; });
    std::vector<int> parent(m);
    std::iota(parent.begin(), parent.end(), 0);
    auto find = [&](int x) {
        while (parent[x] != x)
            x = parent[x] = parent[parent[x]];
        return x;
    };
    const int mergesToApply = numClusters > 0 ? std::max(0, m - numClusters) : static_cast<int>(merges.size());
    for (int i = 0; i < static_cast<int>(merges.size()) && i < mergesToApply; ++i) {
        if (numClusters <= 0 && merges[i].height > threshold)
            break;
        parent[find(merges[i].a)] = find(merges[i].b);
    }
    std::vector<int> subsetLabel(m, -1);
    QHash<int, int> rootToLabel;
    for (int i = 0; i < m; ++i) {
        const int r = find(i);
        if (!rootToLabel.contains(r))
            rootToLabel.insert(r, rootToLabel.size());
        subsetLabel[i] = rootToLabel.value(r);
    }
    if (m == rows)
        return subsetLabel;

    // Everything outside the subset joins the nearest cluster centre.
    const int k = rootToLabel.size();
    std::vector<float> centre(static_cast<size_t>(k) * dim, 0.0f);
    for (int i = 0; i < m; ++i)
        for (int c = 0; c < dim; ++c)
            centre[static_cast<size_t>(subsetLabel[i]) * dim + c] += e[static_cast<size_t>(pick[i]) * dim + c];
    std::vector<int> labels(rows, 0);
    for (int i = 0; i < rows; ++i) {
        float best = -2.0f;
        for (int c = 0; c < k; ++c) {
            const float *cv = centre.data() + static_cast<size_t>(c) * dim;
            const float *v = e.data() + static_cast<size_t>(i) * dim;
            double dot = 0.0, n = 0.0;
            for (int q = 0; q < dim; ++q) {
                dot += double(cv[q]) * v[q];
                n += double(cv[q]) * cv[q];
            }
            const float cosine = n > 0 ? static_cast<float>(dot / std::sqrt(n)) : -1.0f;
            if (cosine > best) {
                best = cosine;
                labels[i] = c;
            }
        }
    }
    return labels;
}

void assignSpeakers(Transcript &transcript, const QList<DiarizeSegment> &segments)
{
    int maxSpeaker = -1;
    for (TranscriptWord &w : transcript.words) {
        if (!isSpeechToken(w))
            continue;
        TimeUs bestOverlap = 0;
        int best = -1;
        for (const DiarizeSegment &s : segments) {
            if (s.endUs <= w.startUs || s.startUs >= w.endUs)
                continue;
            const TimeUs overlap = std::min(s.endUs, w.endUs) - std::max(s.startUs, w.startUs);
            if (overlap > bestOverlap) {
                bestOverlap = overlap;
                best = s.speaker;
            }
        }
        // A word in a gap between turns belongs to whoever spoke nearest to it.
        if (best < 0 && !segments.isEmpty()) {
            TimeUs nearest = std::numeric_limits<TimeUs>::max();
            for (const DiarizeSegment &s : segments) {
                const TimeUs gap = s.endUs <= w.startUs ? w.startUs - s.endUs : s.startUs - w.endUs;
                if (gap >= 0 && gap < nearest) {
                    nearest = gap;
                    best = s.speaker;
                }
            }
        }
        w.speaker = static_cast<qint16>(best);
    }
    // Number speakers by who talks first in the transcript; a turn with no words in it (a cough,
    // a laugh) mustn't leave a gap in the labels.
    QHash<int, int> renumber;
    for (TranscriptWord &w : transcript.words) {
        if (w.speaker < 0)
            continue;
        if (!renumber.contains(w.speaker))
            renumber.insert(w.speaker, renumber.size());
        w.speaker = static_cast<qint16>(renumber.value(w.speaker));
        maxSpeaker = std::max<int>(maxSpeaker, w.speaker);
    }
    transcript.speakers.clear();
    for (int i = 0; i <= maxSpeaker; ++i)
        transcript.speakers.append({QStringLiteral("S%1").arg(i + 1), QStringLiteral("Speaker %1").arg(i + 1)});
    transcript.diarized = maxSpeaker >= 0;
}

struct SpeakerDiarizer::Impl
{
    QMutex mutex;
    bool loaded = false;
    QString error;
    std::unique_ptr<Ort::Session> segmentation;
    std::unique_ptr<Ort::Session> embedding;
    std::string segIn, segOut, embIn, embOut;
    int windowSize = 160000;
    int windowShift = 16000;
    int rfSize = 991;
    int rfShift = 270;
    int numSpeakers = 3;
    int numClasses = 7;
    int powersetMax = 2;
    bool embNormalizeSamples = true;
    bool embGlobalMean = false;
    std::vector<std::vector<int>> powerset; // classes x speakers
    KaldiFbank fbank;

    bool ensureLoaded();
    std::vector<float> segmentChunk(const float *p);
};

bool SpeakerDiarizer::Impl::ensureLoaded()
{
    if (loaded)
        return true;
    if (!ort::ensureLoaded(&error))
        return false;
    const QString dir = resolveModelDir();
    if (dir.isEmpty()) {
        error = QStringLiteral("Speaker labelling model not found. Install it from the Addon Manager, "
                               "place it in models/diarization, or set DRIFT_DIARIZE_MODEL_DIR.");
        return false;
    }
    const auto segPath = ort::ortPath(QDir(dir).filePath(QLatin1String(kSegmentationFile)));
    const auto embPath = ort::ortPath(QDir(dir).filePath(QLatin1String(kEmbeddingFile)));
    if (!ort::buildSessions(ort::env(), "diarize", false, &error, [&](Ort::SessionOptions &opts) {
            segmentation = std::make_unique<Ort::Session>(ort::env(), segPath.c_str(), opts);
            embedding = std::make_unique<Ort::Session>(ort::env(), embPath.c_str(), opts);
        }))
        return false;
    try {
        windowSize = metaInt(*segmentation, "window_size", 160000);
        rfSize = metaInt(*segmentation, "receptive_field_size", 991);
        rfShift = metaInt(*segmentation, "receptive_field_shift", 270);
        numSpeakers = metaInt(*segmentation, "num_speakers", 3);
        numClasses = metaInt(*segmentation, "num_classes", 7);
        powersetMax = metaInt(*segmentation, "powerset_max_classes", 2);
        embNormalizeSamples = metaInt(*embedding, "normalize_samples", 1) != 0;
        embGlobalMean = metaString(*embedding, "feature_normalize_type") == QLatin1String("global-mean");
    } catch (const Ort::Exception &e) {
        error = QString::fromUtf8(e.what());
        return false;
    }
    windowShift = std::max(1, static_cast<int>(0.1 * windowSize));
    segIn = ort::sessionNames(*segmentation, true).front();
    segOut = ort::sessionNames(*segmentation, false).front();
    embIn = ort::sessionNames(*embedding, true).front();
    embOut = ort::sessionNames(*embedding, false).front();

    // pyannote's powerset: class 0 = nobody, then each speaker alone, then each pair.
    powerset.assign(numClasses, std::vector<int>(numSpeakers, 0));
    int k = 1;
    for (int j = 0; j < numSpeakers && k < numClasses; ++j, ++k)
        powerset[k][j] = 1;
    if (powersetMax >= 2) {
        for (int j = 0; j < numSpeakers; ++j)
            for (int m = j + 1; m < numSpeakers && k < numClasses; ++m, ++k)
                powerset[k][j] = powerset[k][m] = 1;
    }
    loaded = true;
    return true;
}

std::vector<float> SpeakerDiarizer::Impl::segmentChunk(const float *p)
{
    const std::array<int64_t, 3> shape{1, 1, windowSize};
    Ort::Value x = Ort::Value::CreateTensor<float>(ort::cpuMemory(), const_cast<float *>(p),
                                                   static_cast<size_t>(windowSize), shape.data(), shape.size());
    const char *in = segIn.c_str();
    const char *out = segOut.c_str();
    auto result = segmentation->Run(Ort::RunOptions{nullptr}, &in, &x, 1, &out, 1);
    const auto s = result[0].GetTensorTypeAndShapeInfo().GetShape();
    const float *data = result[0].GetTensorData<float>();
    return std::vector<float>(data, data + s[1] * s[2]);
}

SpeakerDiarizer::SpeakerDiarizer() : d(std::make_unique<Impl>()) {}
SpeakerDiarizer::~SpeakerDiarizer() = default;

SpeakerDiarizer &SpeakerDiarizer::instance()
{
    static SpeakerDiarizer inst;
    return inst;
}

bool SpeakerDiarizer::modelPresent()
{
    return !resolveModelDir().isEmpty();
}

bool SpeakerDiarizer::available()
{
    QMutexLocker lock(&d->mutex);
    return d->ensureLoaded();
}

QString SpeakerDiarizer::lastError() const
{
    return d->error;
}

void SpeakerDiarizer::unload()
{
    QMutexLocker lock(&d->mutex);
    d->segmentation.reset();
    d->embedding.reset();
    d->loaded = false;
}

QList<DiarizeSegment> SpeakerDiarizer::diarize(const std::vector<float> &pcm, const DiarizeParams &params,
                                               const std::function<bool(double)> &progress, bool *cancelled)
{
    if (cancelled)
        *cancelled = false;
    QMutexLocker lock(&d->mutex);
    if (!d->ensureLoaded() || pcm.empty())
        return {};
    auto report = [&](double f) {
        if (progress && !progress(f)) {
            if (cancelled)
                *cancelled = true;
            return false;
        }
        return true;
    };
    const int ws = d->windowSize;
    const int sh = d->windowShift;
    const int n = static_cast<int>(pcm.size());

    // 1. Segmentation: who is active per frame in each 10 s window.
    std::vector<Labels> labels;
    int numChunks = 0;
    bool hasLast = false;
    if (n <= ws) {
        numChunks = 1;
    } else {
        numChunks = (n - ws) / sh + 1;
        hasLast = ((n - ws) % sh) > 0;
    }
    const int totalChunks = numChunks + (hasLast ? 1 : 0);
    std::vector<float> buf(ws);
    try {
        for (int c = 0; c < totalChunks; ++c) {
            const float *p;
            if (n <= ws || c == numChunks) {
                std::fill(buf.begin(), buf.end(), 0.0f);
                const int from = n <= ws ? 0 : c * sh;
                std::copy(pcm.begin() + from, pcm.end(), buf.begin());
                p = buf.data();
            } else {
                p = pcm.data() + static_cast<size_t>(c) * sh;
            }
            const std::vector<float> logits = d->segmentChunk(p);
            const int frames = static_cast<int>(logits.size()) / d->numClasses;
            Labels l(frames, std::vector<int>(d->numSpeakers, 0));
            for (int f = 0; f < frames; ++f) {
                const float *row = logits.data() + static_cast<size_t>(f) * d->numClasses;
                const int cls = static_cast<int>(std::max_element(row, row + d->numClasses) - row);
                l[f] = d->powerset[cls];
            }
            labels.push_back(std::move(l));
            if (!report(0.5 * (c + 1) / totalChunks))
                return {};
        }
    } catch (const Ort::Exception &e) {
        d->error = QString::fromUtf8(e.what());
        qWarning() << "[diarize] segmentation failed:" << d->error;
        return {};
    }
    const int chunkFrames = static_cast<int>(labels.front().size());
    const int C = static_cast<int>(labels.size());
    const int gridFrames = (ws + (C - 1) * sh) / d->rfShift + 1;
    auto chunkStartFrame = [&](int c) { return static_cast<int>(static_cast<float>(c) * sh / d->rfShift + 0.5f); };

    // 2. How many people speak at each frame, averaged over overlapping windows.
    std::vector<int> speakersPerFrame(gridFrames, 0);
    {
        std::vector<float> count(gridFrames, 0.0f), weight(gridFrames, 0.0f);
        for (int c = 0; c < C; ++c) {
            const int s0 = chunkStartFrame(c);
            for (int f = 0; f < chunkFrames && s0 + f < gridFrames; ++f) {
                int active = 0;
                for (int v : labels[c][f])
                    active += v;
                count[s0 + f] += active;
                weight[s0 + f] += 1.0f;
            }
        }
        for (int f = 0; f < gridFrames; ++f)
            speakersPerFrame[f] = static_cast<int>(count[f] / (weight[f] + 1e-12f) + 0.5f);
    }
    if (*std::max_element(speakersPerFrame.begin(), speakersPerFrame.end()) == 0)
        return {};

    // A single window needs no matching across windows: its local speakers are the speakers
    // (sherpa's one-chunk case).
    if (C == 1) {
        QList<DiarizeSegment> out;
        const int last = std::min(chunkFrames - 1, n / d->rfShift);
        const double scale = static_cast<double>(d->rfShift) / kSpeechSampleRate;
        const double offset = 0.5 * d->rfSize / kSpeechSampleRate;
        for (int spk = 0; spk < d->numSpeakers; ++spk) {
            QList<QPair<double, double>> segs;
            int start = -1;
            for (int f = 0; f <= last + 1; ++f) {
                const bool on = f <= last && labels[0][f][spk];
                if (on && start < 0)
                    start = f;
                else if (!on && start >= 0) {
                    segs.append({start * scale + offset, std::min(f, last) * scale + offset});
                    start = -1;
                }
            }
            for (int i = 0; i + 1 < segs.size();) {
                if (segs[i + 1].first - segs[i].second < params.minDurationOff) {
                    segs[i].second = segs[i + 1].second;
                    segs.removeAt(i + 1);
                } else {
                    ++i;
                }
            }
            for (const auto &s : std::as_const(segs))
                if (s.second - s.first > params.minDurationOn)
                    out.append({secondsToUs(s.first), secondsToUs(s.second), spk});
        }
        std::sort(out.begin(), out.end(), [](const DiarizeSegment &a, const DiarizeSegment &b) { return a.startUs < b.startUs; });
        QHash<int, int> renumber;
        for (DiarizeSegment &s : out) {
            if (!renumber.contains(s.speaker))
                renumber.insert(s.speaker, renumber.size());
            s.speaker = renumber.value(s.speaker);
        }
        report(1.0);
        return out;
    }

    // 3. One embedding per (window, local speaker), from the frames where they speak alone.
    struct ChunkSpeaker
    {
        int chunk;
        int speaker;
        std::vector<std::pair<int, int>> samples;
    };
    std::vector<ChunkSpeaker> pairs;
    for (int c = 0; c < C; ++c) {
        const int offset = c * sh;
        for (int s = 0; s < d->numSpeakers; ++s) {
            int solo = 0;
            for (int f = 0; f < chunkFrames; ++f) {
                int active = 0;
                for (int v : labels[c][f])
                    active += v;
                solo += (active < 2 && labels[c][f][s]) ? 1 : 0;
            }
            if (solo < 10)
                continue;
            ChunkSpeaker cs{c, s, {}};
            int start = -1;
            for (int f = 0; f <= chunkFrames; ++f) {
                bool on = false;
                if (f < chunkFrames) {
                    int active = 0;
                    for (int v : labels[c][f])
                        active += v;
                    on = active < 2 && labels[c][f][s];
                }
                if (on && start < 0) {
                    start = f;
                } else if (!on && start >= 0) {
                    const int endFrame = f < chunkFrames ? f : chunkFrames - 1;
                    cs.samples.push_back({static_cast<int>(static_cast<float>(start) / chunkFrames * ws) + offset,
                                          static_cast<int>(static_cast<float>(endFrame) / chunkFrames * ws) + offset});
                    start = -1;
                }
            }
            pairs.push_back(std::move(cs));
        }
    }

    int dim = 0;
    std::vector<float> embeddings;
    std::vector<int> valid;
    try {
        for (size_t i = 0; i < pairs.size(); ++i) {
            std::vector<float> audio;
            for (const auto &[a, b] : pairs[i].samples) {
                const int end = std::min(b, n);
                if (end > a)
                    audio.insert(audio.end(), pcm.begin() + a, pcm.begin() + end);
            }
            std::vector<float> feats = d->fbank.compute(audio.data(), audio.size(), !d->embNormalizeSamples);
            const int frames = static_cast<int>(feats.size() / KaldiFbank::kBins);
            if (frames <= 0)
                continue;
            if (d->embGlobalMean) {
                for (int b = 0; b < KaldiFbank::kBins; ++b) {
                    double mean = 0.0;
                    for (int f = 0; f < frames; ++f)
                        mean += feats[static_cast<size_t>(f) * KaldiFbank::kBins + b];
                    mean /= frames;
                    for (int f = 0; f < frames; ++f)
                        feats[static_cast<size_t>(f) * KaldiFbank::kBins + b] -= static_cast<float>(mean);
                }
            }
            const std::array<int64_t, 3> shape{1, frames, KaldiFbank::kBins};
            Ort::Value x = Ort::Value::CreateTensor<float>(ort::cpuMemory(), feats.data(), feats.size(),
                                                           shape.data(), shape.size());
            const char *in = d->embIn.c_str();
            const char *out = d->embOut.c_str();
            auto result = d->embedding->Run(Ort::RunOptions{nullptr}, &in, &x, 1, &out, 1);
            const auto s = result[0].GetTensorTypeAndShapeInfo().GetShape();
            const int thisDim = static_cast<int>(s.back());
            const float *v = result[0].GetTensorData<float>();
            if (std::any_of(v, v + thisDim, [](float f) { return std::isnan(f); }))
                continue;
            dim = thisDim;
            embeddings.insert(embeddings.end(), v, v + thisDim);
            valid.push_back(static_cast<int>(i));
            if (!report(0.5 + 0.45 * (i + 1) / pairs.size()))
                return {};
        }
    } catch (const Ort::Exception &e) {
        d->error = QString::fromUtf8(e.what());
        qWarning() << "[diarize] embedding failed:" << d->error;
        return {};
    }
    if (valid.empty())
        return {};

    // 4. Cluster, then put each window's local speakers onto the global ones.
    const std::vector<int> cluster =
        clusterEmbeddings(embeddings, static_cast<int>(valid.size()), dim, params.numSpeakers, params.threshold);
    const int numClusters = *std::max_element(cluster.begin(), cluster.end()) + 1;
    std::vector<std::vector<int>> localToGlobal(C, std::vector<int>(d->numSpeakers, -1));
    for (size_t k = 0; k < valid.size(); ++k)
        localToGlobal[pairs[valid[k]].chunk][pairs[valid[k]].speaker] = cluster[k];

    std::vector<std::vector<int>> speakerCount(gridFrames, std::vector<int>(numClusters, 0));
    for (int c = 0; c < C; ++c) {
        const int s0 = chunkStartFrame(c);
        for (int f = 0; f < chunkFrames && s0 + f < gridFrames; ++f)
            for (int s = 0; s < d->numSpeakers; ++s)
                if (labels[c][f][s] && localToGlobal[c][s] >= 0)
                    speakerCount[s0 + f][localToGlobal[c][s]] += 1;
    }
    int lastFrame = gridFrames - 1;
    if (hasLast)
        lastFrame = std::min(lastFrame, n / d->rfShift);
    else if (C == 1 && n < ws)
        lastFrame = std::min(lastFrame, n / d->rfShift);

    // 5. Each frame keeps its speakers_per_frame most-present clusters.
    std::vector<std::vector<int>> finalLabels(lastFrame + 1, std::vector<int>(numClusters, 0));
    for (int f = 0; f <= lastFrame; ++f) {
        const int k = std::min(speakersPerFrame[f], numClusters);
        if (k == 0)
            continue;
        std::vector<int> order(numClusters);
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(order.begin(), order.end(),
                         [&](int a, int b) { return speakerCount[f][a] > speakerCount[f][b]; });
        for (int i = 0; i < k; ++i)
            finalLabels[f][order[i]] = 1;
    }

    // 6. Frames to time, merge short gaps, drop blips.
    const double scale = static_cast<double>(d->rfShift) / kSpeechSampleRate;
    const double offset = 0.5 * d->rfSize / kSpeechSampleRate;
    QList<DiarizeSegment> out;
    for (int spk = 0; spk < numClusters; ++spk) {
        QList<QPair<double, double>> segs;
        int start = finalLabels[0][spk] ? 0 : -1;
        for (int f = 1; f <= lastFrame; ++f) {
            if (start >= 0 && !finalLabels[f][spk]) {
                segs.append({start * scale + offset, f * scale + offset});
                start = -1;
            } else if (start < 0 && finalLabels[f][spk]) {
                start = f;
            }
        }
        if (start >= 0)
            segs.append({start * scale + offset, lastFrame * scale + offset});
        for (int i = 0; i + 1 < segs.size();) {
            if (segs[i + 1].first - segs[i].second < params.minDurationOff) {
                segs[i].second = segs[i + 1].second;
                segs.removeAt(i + 1);
            } else {
                ++i;
            }
        }
        for (const auto &s : std::as_const(segs)) {
            if (s.second - s.first > params.minDurationOn)
                out.append({secondsToUs(s.first), secondsToUs(s.second), spk});
        }
    }
    std::sort(out.begin(), out.end(), [](const DiarizeSegment &a, const DiarizeSegment &b) { return a.startUs < b.startUs; });
    // Number speakers by first appearance.
    QHash<int, int> renumber;
    for (DiarizeSegment &s : out) {
        if (!renumber.contains(s.speaker))
            renumber.insert(s.speaker, renumber.size());
        s.speaker = renumber.value(s.speaker);
    }
    report(1.0);
    return out;
}

} // namespace drift
