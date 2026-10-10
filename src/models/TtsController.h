#pragma once

#include "engine/ChatterboxTts.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

#include <atomic>

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

    // [{code, label}] for the languages the model supports.
    Q_INVOKABLE QVariantList languageOptions() const;
    Q_INVOKABLE void warmUp();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void generate(const QString &text, const QString &voiceId, const QString &lang,
                              double exaggeration);
    // The subtitle clip at (trackIndex, clipIndex): one voiced clip per caption, on new audio tracks.
    Q_INVOKABLE void voiceoverFromSubtitles(int trackIndex, int clipIndex, const QString &voiceId,
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

private:
    void remember(const QString &voiceId, const QString &lang, double exaggeration);
    void pollJob();
    void endJob();
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
    QTimer m_poll;
    bool m_shownLoaded = false;
    bool m_shownLoading = false;
};
