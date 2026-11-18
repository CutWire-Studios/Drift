#pragma once

#include "core/project/Project.h"
#include "engine/audio/analysis/AudioDucking.h"

#include <QAtomicInt>
#include <QList>
#include <QObject>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

class AppController;

// Silence removal and audio-based clip syncing, reached from QML as EditorState.audioAlign.
// Both decode the clips' audio off the GUI thread, then land one undo step back on it.
class AudioAlignController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
    // What a ducking pass was asked for. detect is "auto", "level" or "voice".
    struct DuckRequest
    {
        int overTrack = -1;      // -1 = every other track with audio
        QStringList overClips;   // when set, only these clips are the speech
        drift::DuckSettings settings;
        double sensitivity = 0.5;
        QString detect = QStringLiteral("auto");
    };
    // Everything the worker needs, detached from the live project: a snapshot in which only the
    // speech sources are audible.
    struct DuckJob
    {
        QString error;
        QString musicId;
        drift::Project snapshot;
        drift::TimeUs startUs = 0;
        drift::TimeUs durationUs = 0;
        drift::DuckSettings settings;
        double sensitivity = 0.5;
        QString detect;
        QList<drift::DuckSpan> owned;
    };
    struct DuckDetection
    {
        bool ok = false;
        bool cancelled = false;
        QString method; // "voice" or "level"
        QString error;
        QList<drift::DuckSpan> dips;
    };
    static DuckJob makeDuckJob(const drift::Project &project, int musicTrack, int musicClip,
                               const DuckRequest &request);
    // Blocking; run it off the GUI thread. `progress` takes 0-1 and returns false to cancel.
    static DuckDetection detectDuckSpeech(const DuckJob &job,
                                          const std::function<bool(double)> &progress);

    explicit AudioAlignController(AppController *app, QObject *parent = nullptr);

    bool running() const { return m_running; }
    double progress() const { return m_progress; }
    QString status() const { return m_status; }

    // True for a clip that has sound of its own, or a separated video whose audio partner does.
    Q_INVOKABLE bool canRemoveSilence(int trackIndex, int clipIndex) const;
    // Cuts the quiet stretches out of the clip (and its linked partners) and closes the gaps
    // within its span. RMS 0.02, at least 0.5 s quiet, 0.08 s of sound kept either side.
    Q_INVOKABLE void removeSilence(int trackIndex, int clipIndex);

    // True when the selection holds exactly two audio-bearing items (a linked video and its audio
    // count as one).
    Q_INVOKABLE bool canSyncByAudio() const;
    // The first selected item is the anchor and stays put; the other, with its linked partners,
    // moves so its sound lines up with the anchor's.
    Q_INVOKABLE void syncByAudio();

    // True when the clip has sound of its own to lower (or a separated video whose audio does).
    Q_INVOKABLE bool canDuck(int track, int clip) const;
    // True when the Silero voice activity model is installed.
    Q_INVOKABLE bool voiceDetectionAvailable() const;
    // The other tracks with enabled audio under the clip, as {track, name}.
    Q_INVOKABLE QVariantList duckSources(int musicTrack, int musicClip) const;
    // Lowers the clip under speech on overTrack (-1 = every other track) by writing volume keys,
    // replacing a previous pass. settings: reduceDb, attack, release, hold (seconds), sensitivity
    // (0-1), detect ("auto", "level", "voice"). One undo step. Always ends with duckFinished.
    Q_INVOKABLE void duck(int musicTrack, int musicClip, int overTrack, const QVariantMap &settings);

    Q_INVOKABLE void cancel();

signals:
    // ok, keys, dips, method ("voice" or "level"), detail (a message when there is something to say).
    void duckFinished(const QVariantMap &result);
    void runningChanged();
    void progressChanged();
    void statusChanged();

private:
    struct SyncUnit;
    bool begin(const QString &message);
    void end();
    void reportProgress(double fraction, const QString &status);
    void applySilence(const QString &clipId, qint64 startUs, qint64 durationUs,
                      const QList<QPair<double, double>> &silences);
    void applyDuck(const QString &musicId, qint64 startUs, qint64 durationUs,
                   const DuckJob &job, const DuckDetection &detection);
    void finishDuck(bool ok, int keys, int dips, const QString &method, const QString &detail);
    void applySync(const QString &anchorId, const QString &otherId, qint64 anchorStartUs,
                   qint64 otherStartUs, double lagSeconds);

    AppController *m_app = nullptr;
    bool m_running = false;
    double m_progress = 0.0;
    QString m_status;
    QAtomicInt m_cancel = 0;
};
