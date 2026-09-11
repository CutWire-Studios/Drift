#pragma once

#include "core/Time.h"

#include <QMap>
#include <QString>
#include <QVector>

#include <memory>

namespace drift {

// One effect instance reduced to what the DSP needs: which processor to build, how much warm-up it
// wants, and its resolved parameter values. Built by audioEffectSpecsFor() in AudioEffectCatalog,
// which is what keeps this library independent of the catalog and its package loader.
struct AudioEffectSpec
{
    QString processorId;
    int prerollMs = 0;
    QMap<QString, float> parameters;
};

// A clip's audio effect chain. Replaces the libavfilter graph that used to live in
// AudioEffectChain::Stream.
//
// Two things differ from that design and both are deliberate:
//
//  - process() is in place and strictly 1:1. The old feed/drain split existed because avfilter is
//    push/pull, and drain() zero-filled whatever the graph had not produced yet — silent dropouts
//    and a permanent offset for any latent filter.
//  - configure() rebuilds only when the *set* of effects changes. Parameter values are pushed into
//    live stages, which smooth them. The old graph rebuilt on every value change, which is what
//    made dragging a slider click.
//
// Not thread-safe. AudioMixer owns one per clip and touches it only from the mixing thread.
class AudioEffectRack
{
public:
    AudioEffectRack();
    ~AudioEffectRack();
    AudioEffectRack(AudioEffectRack &&other) noexcept;
    AudioEffectRack &operator=(AudioEffectRack &&other) noexcept;
    AudioEffectRack(const AudioEffectRack &) = delete;
    AudioEffectRack &operator=(const AudioEffectRack &) = delete;

    // Returns true when there is something to process.
    // `rebuilt`, when given, reports that the *set* of effects changed and the chain was torn
    // down and re-prepared. That is a discontinuity as real as a seek — the new stages have no
    // history — so a caller that primes after a seek must prime here too, or a tail that appears
    // part-way through a clip opens cold.
    bool configure(const QVector<AudioEffectSpec> &specs, int sampleRate, bool *rebuilt = nullptr);

    // Frames of clip audio from *before* the block the caller should push through warmUp() after a
    // reset, so latent stages line up and stateful tails start warm instead of cold.
    int primeFrames() const;

    // Process and discard: fills stage state without emitting anything.
    void warmUp(const float *interleavedStereo, int frames);

    void process(float *interleavedStereo, int frames);

    void reset();

    drift::TimeUs lastTimelineEndUs() const;
    void setLastTimelineEndUs(drift::TimeUs us);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace drift
