#pragma once

#include "core/Time.h"
#include "core/Transcript.h"

#include <QList>
#include <QString>

#include <functional>
#include <memory>
#include <vector>

namespace drift {

struct DiarizeParams
{
    int numSpeakers = -1;    // known count; otherwise clusters are cut at `threshold`
    float threshold = 0.5f;  // cosine distance; smaller finds more speakers
    float minDurationOn = 0.3f;
    float minDurationOff = 0.5f;
};

struct DiarizeSegment
{
    TimeUs startUs = 0;
    TimeUs endUs = 0;
    int speaker = 0; // 0-based, numbered in order of first appearance
};

// Offline speaker diarization: pyannote segmentation-3.0 finds who is active in each 10 s
// window, a speaker-embedding model (3D-Speaker / WeSpeaker export) fingerprints each local
// speaker, and complete-linkage clustering matches them across windows. A port of sherpa-onnx's
// OfflineSpeakerDiarizationPyannoteImpl onto Drift's own ONNX Runtime. Synchronous.
class SpeakerDiarizer
{
public:
    static SpeakerDiarizer &instance();
    static bool modelPresent();

    bool available();
    QString lastError() const;
    void unload();

    // pcm: 16 kHz mono. Segments are relative to pcm[0]. `progress` returns false to cancel.
    QList<DiarizeSegment> diarize(const std::vector<float> &pcm, const DiarizeParams &params,
                                  const std::function<bool(double)> &progress, bool *cancelled);

    SpeakerDiarizer(const SpeakerDiarizer &) = delete;
    SpeakerDiarizer &operator=(const SpeakerDiarizer &) = delete;

private:
    SpeakerDiarizer();
    ~SpeakerDiarizer();
    struct Impl;
    std::unique_ptr<Impl> d;
};

// Complete-linkage agglomerative clustering on cosine distance (rows are L2-normalised here).
// numClusters > 0 cuts to that many; otherwise merges stop above `threshold`.
std::vector<int> clusterEmbeddings(std::vector<float> embeddings, int rows, int dim, int numClusters,
                                   float threshold);

// Gives each speech token of `transcript` the speaker overlapping it most; segments are in the
// transcript's own time base.
void assignSpeakers(Transcript &transcript, const QList<DiarizeSegment> &segments);

} // namespace drift
