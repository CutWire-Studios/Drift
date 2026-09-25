#pragma once

#include "core/Transcript.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include <functional>
#include <memory>

// ElevenLabs and Fish Audio accounts: API keys, the user's one-time consent to send audio/text to
// each, and default models and voices. Keys live in plain QSettings (the Settings page says so);
// ELEVENLABS_API_KEY / FISH_API_KEY in the environment take precedence, which is what headless
// runs use. Keys are never reported over MCP.
class CloudProviders : public QObject
{
    Q_OBJECT
    // Bumped on every change, so QML bindings over the getters below re-evaluate.
    Q_PROPERTY(int revision READ revision NOTIFY changed)
public:
    static constexpr const char *kElevenLabs = "elevenlabs";
    static constexpr const char *kFish = "fish";

    explicit CloudProviders(QObject *parent = nullptr);

    int revision() const { return m_revision; }

    Q_INVOKABLE bool configured(const QString &provider) const;
    Q_INVOKABLE bool keyFromEnvironment(const QString &provider) const;
    Q_INVOKABLE QString maskedKey(const QString &provider) const;
    Q_INVOKABLE void setApiKey(const QString &provider, const QString &key);
    Q_INVOKABLE bool consent(const QString &provider) const;
    Q_INVOKABLE void setConsent(const QString &provider, bool granted);
    // "scribe_v2" / "eleven_multilingual_v2" for ElevenLabs STT/TTS, "s2.1-pro" … for Fish.
    Q_INVOKABLE QString setting(const QString &provider, const QString &key) const;
    Q_INVOKABLE void setSetting(const QString &provider, const QString &key, const QString &value);
    // Checks the key against the provider; answers with keyTested.
    Q_INVOKABLE void testKey(const QString &provider);

    QString apiKey(const QString &provider) const;
    QJsonObject statusJson() const;

signals:
    void changed();
    void keyTested(const QString &provider, bool ok, const QString &message);

private:
    void bump();
    int m_revision = 0;
};

namespace drift::cloud {

// A failed call: `code` is one of unauthorized, quota_exceeded, bad_args, rate_limited,
// provider_error, network, cancelled.
struct CloudError
{
    QString code;
    QString message;
    bool isError() const { return !code.isEmpty(); }
};

using CancelFn = std::function<bool()>;
using ProgressFn = std::function<void(double)>;

// All blocking: call them from a job thread, never the GUI thread.
namespace elevenlabs {
std::shared_ptr<Transcript> speechToText(const QString &key, const QString &model, const QString &audioPath,
                                         const QString &language, bool diarize, int numSpeakers,
                                         const QStringList &keyterms, const CancelFn &cancel,
                                         const ProgressFn &uploadProgress, CloudError *error);
QByteArray textToSpeech(const QString &key, const QString &voice, const QString &model, const QString &text,
                        const QJsonObject &voiceSettings, const QString &language, const CancelFn &cancel,
                        CloudError *error);
QByteArray soundEffect(const QString &key, const QString &prompt, double durationSeconds,
                       double promptInfluence, bool loop, const CancelFn &cancel, CloudError *error);
QJsonObject voices(const QString &key, const QString &search, const QString &pageToken, int limit,
                   CloudError *error);
bool checkKey(const QString &key, CloudError *error);
} // namespace elevenlabs

namespace fish {
QByteArray textToSpeech(const QString &key, const QString &model, const QString &voice, const QString &text,
                        double speed, const CancelFn &cancel, CloudError *error);
QJsonObject voices(const QString &key, const QString &search, bool mine, int page, int limit, CloudError *error);
bool checkKey(const QString &key, CloudError *error);
} // namespace fish

// Scribe's response as a Transcript (source time = the uploaded audio's time).
std::shared_ptr<Transcript> transcriptFromScribe(const QJsonObject &response, const QString &model);

} // namespace drift::cloud
