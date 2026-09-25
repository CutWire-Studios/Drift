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
    // Composite clips only: the nested timeline and the mixer playing it. Rebuilt when the
    // project snapshot changes; without a snapshot (the offline mixers) on every block.
    std::shared_ptr<drift::Project> nestedView;
    std::shared_ptr<AudioMixer> nestedMixer;
    quint64 nestedSerial = 0;
    // The rack's specs, reused while the snapshot and the live lane adjustments stay the same.
    // Effect parameters are not keyframed, so within one snapshot the chain decides them alone.
    QVector<drift::AudioEffectSpec> effectSpecs;
    quint64 effectSpecsSerial = 0;
    size_t effectSpecsKey = 0;
    // Scratch for the block and its preroll, so a playing clip does not allocate per block.
    QVector<float> chunk;
    QVector<float> preroll;
};

// Mixes active audio clips into interleaved stereo float PCM.
class AudioMixer
{
public:
    void setProject(const drift::Project *project);
    // Playback's immutable copy of the project, swapped from the GUI thread on each edit. While
    // one is set, mix() reads it instead of the project passed to setProject(), which the GUI
    // thread keeps editing underneath the audio thread. `serial` changes with every new copy.
    void setSnapshot(std::shared_ptr<const drift::Project> snapshot, quint64 serial);
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
    //
    // `audibleStartUs` / `audibleEndUs` widen the clip past its edges into the media beyond its
    // trim (a transition handle); -1 keeps the clip's own bounds.
    using SourceReader = std::function<int(drift::TimeUs, int, float *)>;
    static QVector<float> readClipAudio(const drift::Clip &clip, quint64 streamId,
                                        drift::TimeUs winStartUs, int outFrames, int sampleRate,
                                        drift::ClipAudioRetimer *retimer,
                                        const SourceReader &source = {},
                                        drift::TimeUs audibleStartUs = -1, drift::TimeUs audibleEndUs = -1);
    // The same, into `out`, which is resized to `outFrames` and keeps its capacity across calls.
    static void readClipAudio(QVector<float> &out, const drift::Clip &clip, quint64 streamId,
                              drift::TimeUs winStartUs, int outFrames, int sampleRate,
                              drift::ClipAudioRetimer *retimer, const SourceReader &source = {},
                              drift::TimeUs audibleStartUs = -1, drift::TimeUs audibleEndUs = -1);


    void setMasterVolume(double volume) { m_masterVolume = volume; }
    double masterVolume() const { return m_masterVolume; }
    void setMasterMuted(bool muted) { m_masterMuted = muted; }
    bool masterMuted() const { return m_masterMuted; }

    QPair<float, float> trackLevels(int trackIndex) const;
    QPair<float, float> masterLevels() const;
    // Loudest block per channel since the previous call, master first then one pair per
    // requested track. A meter polling slower than the mixer's block rate would otherwise
    // see whichever block happened to be last and miss the transients between polls.
    QList<float> takeMeterPeaks(const QList<int> &trackIndexes) const;

    // The audio-effect adjustments a snapshot holds, with only the time test left to do per
    // block: per parent track its lanes' clips, and the standalone ones that form the master bus.
    // Only adjustments that carry effects are listed.
    struct AudioAdjustments
    {
        quint64 serial = 0;
        QHash<int, QList<const drift::Clip *>> lanes;
        QList<const drift::Clip *> masterBus;
    };

private:
    const drift::Project *m_project = nullptr;
    // Built once per snapshot serial and read only by mix(), which for a snapshot-driven mixer
    // runs on the audio thread alone. The pointers are into the snapshot with that serial.
    mutable AudioAdjustments m_adjustments;
    mutable QMutex m_snapshotMutex;
    std::shared_ptr<const drift::Project> m_snapshot;
    quint64 m_snapshotSerial = 0;
    // mix() runs on the audio thread; resetClipAudioState() is called from the GUI thread on seek,
    // play and pause. The mutex covers the hash itself — callers take a shared_ptr copy out of it
    // and work on the state with the lock released.
    mutable QMutex m_clipAudioMutex;
    mutable QHash<QString, std::shared_ptr<ClipAudioState>> m_clipAudio;
    bool m_masterClipEnabled = true;
    double m_masterVolume = 1.0;
    bool m_masterMuted = false;
    mutable QMutex m_levelsMutex;
    mutable QHash<int, QPair<float, float>> m_trackLevels;
    mutable QPair<float, float> m_masterLevels{0.0f, 0.0f};
    mutable QHash<int, QPair<float, float>> m_trackMeterPeaks;
    mutable QPair<float, float> m_masterMeterPeak{0.0f, 0.0f};
    quint64 m_streamSalt = 0;
    int m_depth = 0;
};
