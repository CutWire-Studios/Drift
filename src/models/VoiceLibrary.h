#pragma once

#include "engine/AudioRecorder.h"
#include "engine/ChatterboxTts.h"

#include <QAbstractListModel>
#include <QDateTime>
#include <QString>
#include <QUrl>

#include <optional>
#include <vector>

class AppController;
class TtsController;

// Cloned voices for Chatterbox: a built-in "Default" plus one folder per voice under
// <AppDataLocation>/voices/<uuid>/ holding voice.json, reference.flac (24 kHz mono) and
// conditioning.bin. The conditioning is a cache of the speech encoder's output; the reference is
// what is authoritative, so a model update re-encodes it. Reached from QML as EditorState.voices.
class VoiceLibrary : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    Q_PROPERTY(double recordingSeconds READ recordingSeconds NOTIFY recordingSecondsChanged)
    Q_PROPERTY(double level READ level NOTIFY levelChanged)
    Q_PROPERTY(bool encoding READ encoding NOTIFY encodingChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        BuiltInRole,
        SourceRole,
        CreatedRole,
    };

    static constexpr double kMinReferenceSeconds = 3.0;
    static constexpr double kMaxReferenceSeconds = 20.0;

    VoiceLibrary(AppController *app, TtsController *tts, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool recording() const { return m_recorder.isRecording(); }
    double recordingSeconds() const { return m_recorder.recordedSeconds(); }
    double level() const { return m_recorder.audioLevel(); }
    bool encoding() const { return m_encoding; }

    // [inSeconds, outSeconds) of any audio or video file; a longer range keeps its first 20 s.
    Q_INVOKABLE void addFromMedia(const QString &path, double inSeconds, double outSeconds,
                                  const QString &name);
    Q_INVOKABLE void addFromFile(const QString &path, const QString &name);
    Q_INVOKABLE void rename(const QString &id, const QString &name);
    Q_INVOKABLE void remove(const QString &id);
    // Where the reference recording can be played from; empty when the voice has none.
    Q_INVOKABLE QUrl referenceUrl(const QString &id) const;
    Q_INVOKABLE QString nameFor(const QString &id) const;
    Q_INVOKABLE int indexOfId(const QString &id) const;

    Q_INVOKABLE void startRecording();
    Q_INVOKABLE void stopRecordingAndAdd(const QString &name);
    Q_INVOKABLE void cancelRecording();

    // Blocking: loads the model if needed and re-encodes a stale cache. Worker threads only.
    std::optional<drift::VoiceConditioning> conditioning(const QString &id, QString *err);

    static bool writeFlac24k(const QString &path, const std::vector<float> &pcm, QString *err);

signals:
    void recordingChanged();
    void recordingSecondsChanged();
    void levelChanged();
    void encodingChanged();
    void voiceAdded(const QString &id);
    void error(const QString &message);

private:
    struct Entry
    {
        QString id;
        QString name;
        QString source;
        QDateTime created;
        bool builtIn = false;
    };

    void addVoice(const QString &path, double inSeconds, double outSeconds, const QString &name,
                  const QString &source, bool removeSource);
    void setEncoding(bool encoding);

    AppController *m_app = nullptr;
    TtsController *m_tts = nullptr;
    drift::AudioRecorder m_recorder;
    QList<Entry> m_entries;
    bool m_encoding = false;
    QString m_recordingPath;
};
