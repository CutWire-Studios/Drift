#pragma once

#include "core/Project.h"
#include "core/Time.h"
#include "engine/audio/AudioEffectRack.h"
#include "engine/audio/ClipAudioRetimer.h"

#include <QHash>
#include <QMutex>
#include <QVector>
#include <functional>
#include <memory>

class AudioMixer;

// Everything the mixer carries between blocks for one clip. Both halves are streaming DSP whose
// state only means anything while playback runs forward, and both are invalidated at the same
// moments, so they live and die together in one entry.
struct ClipAudioState
{
    drift::AudioEffectRack rack;
    drift::ClipAudioRetimer retimer;
    // Composite clips only: the nested timeline, refreshed every block, and the mixer playing it.
    std::shared_ptr<drift::Project> nestedView;
    std::shared_ptr<AudioMixer> nestedMixer;
};

// Mixes active audio clips into interleaved stereo float PCM.
class AudioMixer
{
public:
    void setProject(const drift::Project *project);
    void resetClipAudioState();

    // The master soft clipper is what playback hears, but a meter has to see the mix before it:
    // softClip saturates to exactly 1.0f, so anything measured after it reports 0.0 dBFS no matter
    // how far over the top the mix really is.
    void setMasterClipEnabled(bool enabled) { m_masterClipEnabled = enabled; }
    // For the mixer playing a composite's timeline: `streamSalt` keeps its decode cursors apart
    // from every other instance of the same composite, `depth` bounds nesting.
    void setNesting(quint64 streamSalt, int depth)
    {
        m_streamSalt = streamSalt;
        m_depth = depth;
    }
    bool masterClipEnabled() const { return m_masterClipEnabled; }

    void mix(drift::TimeUs timelineStartUs, int sampleCount, int sampleRate, float *interleavedStereoOut) const;

    // One clip's audio in timeline space, with its source read, reverse and speed (constant or
    // curved) applied exactly as playback does. Exposed so the speed-curve editor's preview
    // player auditions the very same retiming the timeline will produce, rather than growing a
    // second implementation of it. Timeline positions outside the clip come back as silence.
    // Returns interleaved stereo of exactly `outFrames` frames.
    //
    // `retimer` carries the stretcher state across blocks. It must be the same one for every block
    // of a given clip, and callers must not share one between clips.
    //
    // `streamId` is the same thing one level down: the clip's own decode cursor in ClipReaderPool.
    // It must be stable across blocks and unique per caller — two consumers reading one file
    // through a single cursor is what desynced overlapping clips.
    //
    // `source`, when set, replaces the file read: (sourceStartUs, frames, dst) -> frames written.
    using SourceReader = std::function<int(drift::TimeUs, int, float *)>;
    static QVector<float> readClipAudio(const drift::Clip &clip, quint64 streamId,
                                        drift::TimeUs winStartUs, int outFrames, int sampleRate,
                                        drift::ClipAudioRetimer *retimer,
                                        const SourceReader &source = {});


private:
    const drift::Project *m_project = nullptr;
    // mix() runs on the audio thread; resetClipAudioState() is called from the GUI thread on seek,
    // play and pause. The mutex covers the hash itself — callers take a shared_ptr copy out of it
    // and work on the state with the lock released.
    mutable QMutex m_clipAudioMutex;
    mutable QHash<QString, std::shared_ptr<ClipAudioState>> m_clipAudio;
    bool m_masterClipEnabled = true;
    quint64 m_streamSalt = 0;
    int m_depth = 0;
};
