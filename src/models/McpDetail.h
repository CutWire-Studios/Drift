#pragma once

#include "engine/MediaWaveform.h"
#include "mcp/McpJson.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QtConcurrent>

#include <cmath>
#include <memory>

#include "core/Project.h"
#include "engine/AudioMixer.h"
#include "engine/AudioOnsets.h"
#include "engine/SceneDetect.h"

#include <QVariant>
#include <QVector>

// Helpers shared by McpController and AppController. They used to live in an
// anonymous namespace in AppController.cpp.
namespace drift::mcpdetail {

inline double round3(double v)
{
    return std::round(v * 1000.0) / 1000.0;
}

inline double round2(double v)
{
    return std::round(v * 100.0) / 100.0;
}

// Max-reduce raw peaks into `buckets`. Deliberately not reduceDensePeaks: that one applies a
// dB curve and a 0.05 visibility floor, both of which are drawing decisions. An agent asking
// whether a stretch is silent has to be able to get 0 back.
inline QVector<float> reduceRawPeaks(const QVector<float> &src, int buckets)
{
    if (src.isEmpty() || buckets <= 0)
        return {};
    if (src.size() <= buckets)
        return src;

    QVector<float> out(buckets, 0.0f);
    for (int b = 0; b < buckets; ++b) {
        int i0 = static_cast<int>((static_cast<qint64>(b) * src.size()) / buckets);
        int i1 = static_cast<int>((static_cast<qint64>(b + 1) * src.size()) / buckets);
        if (i1 <= i0)
            i1 = qMin(src.size(), i0 + 1);
        float peak = 0.0f;
        for (int i = i0; i < i1; ++i)
            peak = qMax(peak, src[i]);
        out[b] = peak;
    }
    return out;
}

// Decodes off the GUI thread and waits, so the reply carries data on the first call. Same
// nested-event-loop shape captureFrame uses.
inline QVector<float> blockingSourcePeaks(const QString &path, double startSeconds, double durSeconds,
                                          int buckets)
{
    if (path.isEmpty() || durSeconds <= 0.0)
        return {};

    // Oversample a little before reducing so a bucket boundary cannot land on the only loud
    // sample in its span, then clamp to the rate the block cache uses.
    const int pps = qBound(1, static_cast<int>(std::ceil(buckets / durSeconds)) * 4, 100);

    auto raw = std::make_shared<QVector<float>>();
    QEventLoop loop;
    (void)QtConcurrent::run([path, startSeconds, durSeconds, pps, raw, &loop]() {
        *raw = MediaWaveform::peaksForRange(path, startSeconds, startSeconds + durSeconds, pps);
        QMetaObject::invokeMethod(&loop, &QEventLoop::quit, Qt::QueuedConnection);
    });
    loop.exec();

    return reduceRawPeaks(*raw, buckets);
}

inline QJsonObject imageResult(const QJsonObject &meta, const QByteArray &bytes, const QString &mime)
{
    using namespace drift::mcp;
    QJsonArray content;
    content.append(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("text")},
        {QStringLiteral("text"),
         QString::fromUtf8(QJsonDocument(compactJson(meta)).toJson(QJsonDocument::Compact))},
    });
    content.append(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("image")},
        {QStringLiteral("mimeType"), mime},
        {QStringLiteral("data"), QString::fromLatin1(bytes.toBase64())},
    });
    return {{QStringLiteral("content"), content}, {QStringLiteral("isError"), false}};
}


inline bool findClipById(const drift::Project &project, const QString &clipId, int *trackOut, int *clipOut)
{
    for (int t = 0; t < project.tracks().size(); ++t) {
        const QList<drift::Clip> &clips = project.tracks().at(t).clips;
        for (int c = 0; c < clips.size(); ++c) {
            if (clips.at(c).id == clipId) {
                if (trackOut)
                    *trackOut = t;
                if (clipOut)
                    *clipOut = c;
                return true;
            }
        }
    }
    return false;
}

inline QVariantList sceneRowsFromAnalysis(const drift::SceneAnalysis &analysis)
{
    QVariantList rows;
    rows.reserve(analysis.scenes.size());
    for (int i = 0; i < analysis.scenes.size(); ++i) {
        const drift::Scene &scene = analysis.scenes.at(i);
        rows.append(QVariantMap{
            {QStringLiteral("index"), i},
            {QStringLiteral("sourceStart"), drift::usToSeconds(scene.sourceIn)},
            {QStringLiteral("sourceEnd"), drift::usToSeconds(scene.sourceOut)},
            {QStringLiteral("duration"), drift::usToSeconds(scene.duration())},
            {QStringLiteral("thumbnailSeconds"), drift::usToSeconds(scene.thumbnailUs)},
            {QStringLiteral("motion"), scene.motion},
            {QStringLiteral("loudness"), scene.loudness},
            {QStringLiteral("objects"), scene.objects},
            {QStringLiteral("score"), scene.score},
            {QStringLiteral("labels"), scene.labels},
        });
    }
    return rows;
}

// 22050 rather than the 8000 used for voice peaks: hats and cymbals, the sharpest onset cues
// in music, live above 4 kHz.
inline constexpr int kBeatAnalysisRate = 22050;

// Mixes [startUs, +durUs) down to mono at kBeatAnalysisRate and runs onset/tempo detection.
//
// `snap` must be a copy the caller owns: this runs off the GUI thread and the mixer would
// otherwise race the live project.
//
// The mix is pulled a window at a time rather than allocated whole. AudioOnsets needs the mono
// buffer contiguous, so that one stays, but holding the interleaved stereo alongside it doubled
// the peak for no reason — at the ten-minute ceiling MCP allows that is 106 MB of scratch to
// produce 53 MB of mono.
inline AudioBeatAnalysis runBeatAnalysis(const drift::Project &snap, drift::TimeUs startUs,
                                  drift::TimeUs durUs, double startSeconds)
{
    const int frames =
        static_cast<int>((static_cast<double>(durUs) / 1'000'000.0) * kBeatAnalysisRate);
    if (frames <= 0)
        return {};

    AudioMixer mixer;
    mixer.setProject(&snap);

    constexpr int kWindowFrames = 1 << 18;
    QVector<float> mono(frames, 0.0f);
    QVector<float> window(static_cast<qsizetype>(kWindowFrames) * 2);
    for (int done = 0; done < frames;) {
        const int want = qMin(kWindowFrames, frames - done);
        const drift::TimeUs at =
            startUs + static_cast<drift::TimeUs>(done) * drift::kUsPerSecond / kBeatAnalysisRate;
        mixer.mix(at, want, kBeatAnalysisRate, window.data());
        for (int i = 0; i < want; ++i)
            mono[done + i] = 0.5f * (window[i * 2] + window[i * 2 + 1]);
        done += want;
    }

    return AudioOnsets::analyze(mono.constData(), frames, kBeatAnalysisRate, startSeconds);
}

} // namespace drift::mcpdetail
