#pragma once

#include "engine/ml/speech/tts/ChatterboxTts.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

#include <atomic>
#include <functional>

class AppController;
class VoiceLibrary;

// Local Chatterbox text-to-speech, reached from QML as EditorState.tts. Owns the one shared engine:
// loading it takes about a minute, so it is loaded once on first use (warmUp() starts that early)
// and kept. Every job that touches it runs on JobRegistry's single model lane, which is what keeps
// two of them from using the engine at once.
class TtsController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(bool modelLoaded READ modelLoaded NOTIFY loadStateChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadStateChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString lastVoice READ lastVoice NOTIFY lastUsedChanged)
    Q_PROPERTY(QString lastLanguage READ lastLanguage NOTIFY lastUsedChanged)
    Q_PROPERTY(double lastExaggeration READ lastExaggeration NOTIFY lastUsedChanged)
    Q_PROPERTY(bool sfxRunning READ sfxRunning NOTIFY sfxChanged)
    Q_PROPERTY(double sfxProgress READ sfxProgress NOTIFY sfxChanged)
    Q_PROPERTY(QString sfxStatus READ sfxStatus NOTIFY sfxChanged)

public:
    TtsController(AppController *app, QObject *parent = nullptr);

    void setVoices(VoiceLibrary *voices) { m_voices = voices; }

    bool available() const { return m_available; }
    bool modelLoaded() const { return m_loaded.load(); }
    bool loading() const { return m_loading.load(); }
    bool running() const { return m_running; }
    double progress() const { return m_progress; }
    QString status() const { return m_status; }
    QString lastVoice() const;
    QString lastLanguage() const;
    double lastExaggeration() const;

    bool sfxRunning() const { return !m_sfxJobId.isEmpty(); }
    double sfxProgress() const { return m_sfxProgress; }
    QString sfxStatus() const { return m_sfxStatus; }

    // generated-audio/<kind>-<provider>-<slug of text>-<id>.mp3, for the cloud generators.
    static QString generatedAudioPath(const QString &kind, const QString &provider, const QString &text);
    // The ElevenLabs sound-effect job, which the caller has already checked is allowed: duration <= 0
    // lets the model pick. When it succeeds, `imported` gets the file and the asset's generator
    // record, and returns what to merge into the job's result. Returns the job id.
    QString startSfxJob(const QString &prompt, double duration, double influence, bool loop,
                        std::function<QJsonObject(const QString &path, const QJsonObject &generator)> imported);
    // From the sound-effect dialog: one at a time, imported to the bin and with `place` at the
    // playhead. Answers with sfxFinished. Returns whether a job started.
    Q_INVOKABLE bool generateSfx(const QString &prompt, double duration, double influence, bool loop, bool place);
    Q_INVOKABLE void cancelSfx();

    // [{code, label}] for the languages the model supports.
    Q_INVOKABLE QVariantList languageOptions() const;
    Q_INVOKABLE void warmUp();
    Q_INVOKABLE void cancel();
    // Returns the job id, or empty when nothing was started. atSeconds < 0 means the playhead and
    // track < 0 the first audio lane with room.
    Q_INVOKABLE QString generate(const QString &text, const QString &voiceId, const QString &lang,
                                 double exaggeration, bool place = true, double atSeconds = -1.0,
                                 int track = -1);
    // The subtitle clip at (trackIndex, clipIndex): one voiced clip per caption, on new audio tracks.
    Q_INVOKABLE QString voiceoverFromSubtitles(int trackIndex, int clipIndex, const QString &voiceId,
                                               const QString &lang, double exaggeration);

    // Model-lane workers only: loads on first use, then reuses the session.
    bool ensureLoaded(QString *err);
    drift::ChatterboxTts &engine() { return m_engine; }

public slots:
    void refreshAvailability();

signals:
    void availableChanged();
    void loadStateChanged();
    void runningChanged();
    void progressChanged();
    void statusChanged();
    void lastUsedChanged();
    void finished(bool ok, const QString &message);
    void sfxChanged();
    void sfxFinished(bool ok, const QString &message);

private:
    void remember(const QString &voiceId, const QString &lang, double exaggeration);
    void pollJob();
    void endJob();
    void clearStaleLoading();
    void setStatus(const QString &status);

    AppController *m_app = nullptr;
    VoiceLibrary *m_voices = nullptr;
    drift::ChatterboxTts m_engine;
    bool m_available = false;
    std::atomic<bool> m_loaded{false};
    std::atomic<bool> m_loading{false};
    bool m_running = false;
    double m_progress = 0.0;
    QString m_status;
    QString m_jobId;
    QString m_loadJobId;
    QString m_sfxJobId;
    double m_sfxProgress = 0.0;
    QString m_sfxStatus;
    QTimer m_sfxPoll;
    QTimer m_poll;
    bool m_shownLoaded = false;
    bool m_shownLoading = false;
};
