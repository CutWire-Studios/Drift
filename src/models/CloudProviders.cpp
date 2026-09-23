#include "CloudProviders.h"

#include <QEventLoop>
#include <QFile>
#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QSettings>
#include <QThread>
#include <QTimer>
#include <QUrlQuery>
#include <QtConcurrent>

#include <cmath>

namespace {

QString settingsKey(const QString &provider, const char *field)
{
    return QStringLiteral("cloud/%1/%2").arg(provider, QLatin1String(field));
}

QString envKeyName(const QString &provider)
{
    return provider == QLatin1String(CloudProviders::kElevenLabs) ? QStringLiteral("ELEVENLABS_API_KEY")
                                                                  : QStringLiteral("FISH_API_KEY");
}

bool knownProvider(const QString &provider)
{
    return provider == QLatin1String(CloudProviders::kElevenLabs) || provider == QLatin1String(CloudProviders::kFish);
}

QString defaultSetting(const QString &provider, const QString &key)
{
    if (provider == QLatin1String(CloudProviders::kElevenLabs)) {
        if (key == QLatin1String("stt_model"))
            return QStringLiteral("scribe_v2");
        if (key == QLatin1String("tts_model"))
            return QStringLiteral("eleven_multilingual_v2");
    } else if (key == QLatin1String("tts_model")) {
        return QStringLiteral("s2.1-pro");
    }
    return {};
}

} // namespace

CloudProviders::CloudProviders(QObject *parent) : QObject(parent) {}

void CloudProviders::bump()
{
    ++m_revision;
    emit changed();
}

QString CloudProviders::apiKey(const QString &provider) const
{
    if (!knownProvider(provider))
        return {};
    const QString env = qEnvironmentVariable(envKeyName(provider).toUtf8().constData()).trimmed();
    if (!env.isEmpty())
        return env;
    return QSettings().value(settingsKey(provider, "apiKey")).toString().trimmed();
}

bool CloudProviders::configured(const QString &provider) const
{
    return !apiKey(provider).isEmpty();
}

bool CloudProviders::keyFromEnvironment(const QString &provider) const
{
    return knownProvider(provider)
           && !qEnvironmentVariable(envKeyName(provider).toUtf8().constData()).trimmed().isEmpty();
}

QString CloudProviders::maskedKey(const QString &provider) const
{
    const QString key = apiKey(provider);
    if (key.size() < 8)
        return key.isEmpty() ? QString() : QStringLiteral("••••");
    return key.left(3) + QStringLiteral("…") + key.right(4);
}

void CloudProviders::setApiKey(const QString &provider, const QString &key)
{
    if (!knownProvider(provider))
        return;
    QSettings settings;
    if (key.trimmed().isEmpty())
        settings.remove(settingsKey(provider, "apiKey"));
    else
        settings.setValue(settingsKey(provider, "apiKey"), key.trimmed());
    bump();
}

bool CloudProviders::consent(const QString &provider) const
{
    return knownProvider(provider) && QSettings().value(settingsKey(provider, "consent")).toBool();
}

void CloudProviders::setConsent(const QString &provider, bool granted)
{
    if (!knownProvider(provider))
        return;
    QSettings().setValue(settingsKey(provider, "consent"), granted);
    bump();
}

QString CloudProviders::setting(const QString &provider, const QString &key) const
{
    if (!knownProvider(provider))
        return {};
    const QString stored = QSettings().value(QStringLiteral("cloud/%1/%2").arg(provider, key)).toString();
    return stored.isEmpty() ? defaultSetting(provider, key) : stored;
}

void CloudProviders::setSetting(const QString &provider, const QString &key, const QString &value)
{
    if (!knownProvider(provider))
        return;
    QSettings().setValue(QStringLiteral("cloud/%1/%2").arg(provider, key), value.trimmed());
    bump();
}

void CloudProviders::testKey(const QString &provider)
{
    const QString key = apiKey(provider);
    if (key.isEmpty()) {
        emit keyTested(provider, false, tr("No key set"));
        return;
    }
    QPointer<CloudProviders> self(this);
    (void)QtConcurrent::run([self, provider, key]() {
        drift::cloud::CloudError error;
        const bool ok = provider == QLatin1String(kElevenLabs) ? drift::cloud::elevenlabs::checkKey(key, &error)
                                                               : drift::cloud::fish::checkKey(key, &error);
        if (!self)
            return;
        QMetaObject::invokeMethod(self.data(), [self, provider, ok, error]() {
            if (self)
                emit self->keyTested(provider, ok, ok ? tr("Key works") : error.message);
        }, Qt::QueuedConnection);
    });
}

QJsonObject CloudProviders::statusJson() const
{
    QJsonObject out;
    for (const QString provider : {QString::fromLatin1(kElevenLabs), QString::fromLatin1(kFish)}) {
        QJsonObject p{{QStringLiteral("configured"), configured(provider)},
                      {QStringLiteral("key_source"), !configured(provider) ? QStringLiteral("none")
                                                     : keyFromEnvironment(provider) ? QStringLiteral("env")
                                                                                    : QStringLiteral("settings")},
                      {QStringLiteral("consent"), consent(provider)},
                      {QStringLiteral("tts_model"), setting(provider, QStringLiteral("tts_model"))},
                      {QStringLiteral("default_voice"), setting(provider, QStringLiteral("voice"))}};
        if (provider == QLatin1String(kElevenLabs))
            p.insert(QStringLiteral("stt_model"), setting(provider, QStringLiteral("stt_model")));
        out.insert(provider, p);
    }
    return out;
}

namespace drift::cloud {

namespace {

const QString kElevenBase = QStringLiteral("https://api.elevenlabs.io");
const QString kFishBase = QStringLiteral("https://api.fish.audio");

struct Response
{
    int status = 0;
    QByteArray body;
};

QString errorMessageFrom(const QByteArray &body)
{
    const QJsonObject o = QJsonDocument::fromJson(body).object();
    const QJsonValue detail = o.value(QStringLiteral("detail"));
    if (detail.isObject())
        return detail.toObject().value(QStringLiteral("message")).toString();
    if (detail.isString())
        return detail.toString();
    if (detail.isArray() && !detail.toArray().isEmpty())
        return detail.toArray().first().toObject().value(QStringLiteral("msg")).toString();
    const QString message = o.value(QStringLiteral("message")).toString();
    return message.isEmpty() ? QString::fromUtf8(body.left(300)) : message;
}

void classify(const Response &r, CloudError *error)
{
    if (!error || (r.status >= 200 && r.status < 300))
        return;
    const QString message = errorMessageFrom(r.body);
    switch (r.status) {
    case 401:
    case 403:
        *error = {QStringLiteral("unauthorized"), message.isEmpty() ? QStringLiteral("The API key was rejected") : message};
        break;
    case 402:
        *error = {QStringLiteral("quota_exceeded"), message};
        break;
    case 400:
    case 404:
    case 422:
        *error = {QStringLiteral("bad_args"), message};
        break;
    case 429:
        *error = {QStringLiteral("rate_limited"), message};
        break;
    default:
        *error = {QStringLiteral("provider_error"),
                  QStringLiteral("HTTP %1: %2").arg(r.status).arg(message)};
    }
    // ElevenLabs reports quota as 401 with a quota status.
    if (message.contains(QStringLiteral("quota"), Qt::CaseInsensitive))
        error->code = QStringLiteral("quota_exceeded");
}

// One blocking request on the calling (job) thread. `send` issues it on the manager it is given.
Response run(const std::function<QNetworkReply *(QNetworkAccessManager &)> &send, int /*timeoutMs*/,
             const CancelFn &cancel, const ProgressFn &uploadProgress, CloudError *error)
{
    Response out;
    for (int attempt = 0; attempt < 2; ++attempt) {
        QNetworkAccessManager nam;
        QNetworkReply *reply = send(nam);
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        if (uploadProgress) {
            QObject::connect(reply, &QNetworkReply::uploadProgress, &loop, [&](qint64 sent, qint64 total) {
                if (total > 0)
                    uploadProgress(static_cast<double>(sent) / total);
            });
        }
        QTimer poll;
        QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
            if (cancel && cancel())
                reply->abort();
        });
        poll.start(200);
        loop.exec();
        out.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        out.body = reply->readAll();
        const QNetworkReply::NetworkError netError = reply->error();
        const QByteArray retryAfter = reply->rawHeader("Retry-After");
        reply->deleteLater();
        if (cancel && cancel()) {
            if (error)
                *error = {QStringLiteral("cancelled"), QStringLiteral("Cancelled")};
            return out;
        }
        if (out.status == 0 && netError != QNetworkReply::NoError) {
            if (error)
                *error = {QStringLiteral("network"), QStringLiteral("Network error (%1)").arg(int(netError))};
            return out;
        }
        // One polite retry on a rate limit; anything more is the caller's call to make.
        if (out.status == 429 && attempt == 0) {
            const int wait = qBound(1, retryAfter.toInt(), 30);
            for (int i = 0; i < wait * 5 && !(cancel && cancel()); ++i)
                QThread::msleep(200);
            continue;
        }
        break;
    }
    classify(out, error);
    return out;
}

QNetworkRequest elevenRequest(const QString &path, const QString &key, int timeoutMs)
{
    QNetworkRequest req{QUrl(kElevenBase + path)};
    req.setRawHeader("xi-api-key", key.toUtf8());
    req.setTransferTimeout(timeoutMs);
    return req;
}

QNetworkRequest fishRequest(const QString &path, const QString &key, int timeoutMs)
{
    QNetworkRequest req{QUrl(kFishBase + path)};
    req.setRawHeader("Authorization", "Bearer " + key.toUtf8());
    req.setTransferTimeout(timeoutMs);
    return req;
}

QHttpPart textPart(const QString &name, const QString &value)
{
    QHttpPart part;
    part.setHeader(QNetworkRequest::ContentDispositionHeader,
                   QStringLiteral("form-data; name=\"%1\"").arg(name));
    part.setBody(value.toUtf8());
    return part;
}

} // namespace

std::shared_ptr<Transcript> transcriptFromScribe(const QJsonObject &response, const QString &model)
{
    auto t = std::make_shared<Transcript>();
    t->engine = QStringLiteral("elevenlabs:") + model;
    t->language = response.value(QStringLiteral("language_code")).toString();
    t->createdAt = QDateTime::currentDateTimeUtc();
    t->wordTimingsAligned = true;
    QHash<QString, int> speakerIndex;
    for (const QJsonValue &v : response.value(QStringLiteral("words")).toArray()) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("start")).isNull() || o.value(QStringLiteral("end")).isNull())
            continue;
        TranscriptWord w;
        w.text = o.value(QStringLiteral("text")).toString();
        w.startUs = secondsToUs(o.value(QStringLiteral("start")).toDouble());
        w.endUs = std::max(w.startUs, secondsToUs(o.value(QStringLiteral("end")).toDouble()));
        const QString type = o.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("spacing"))
            w.type = TranscriptTokenType::Spacing;
        else if (type == QLatin1String("audio_event"))
            w.type = TranscriptTokenType::AudioEvent;
        else
            w.type = isFillerWord(w.text) ? TranscriptTokenType::Filler : TranscriptTokenType::Word;
        if (o.contains(QStringLiteral("logprob")))
            w.confidence = static_cast<float>(std::exp(o.value(QStringLiteral("logprob")).toDouble()));
        const QString speaker = o.value(QStringLiteral("speaker_id")).toString();
        if (!speaker.isEmpty()) {
            if (!speakerIndex.contains(speaker)) {
                speakerIndex.insert(speaker, speakerIndex.size());
                const int n = speakerIndex.size();
                t->speakers.append({QStringLiteral("S%1").arg(n), QStringLiteral("Speaker %1").arg(n)});
            }
            w.speaker = static_cast<qint16>(speakerIndex.value(speaker));
        }
        t->words.append(w);
    }
    std::stable_sort(t->words.begin(), t->words.end(),
                     [](const TranscriptWord &a, const TranscriptWord &b) { return a.startUs < b.startUs; });
    t->diarized = !speakerIndex.isEmpty();
    return t;
}

namespace elevenlabs {

std::shared_ptr<Transcript> speechToText(const QString &key, const QString &model, const QString &audioPath,
                                         const QString &language, bool diarize, int numSpeakers,
                                         const QStringList &keyterms, const CancelFn &cancel,
                                         const ProgressFn &uploadProgress, CloudError *error)
{
    const Response r = run(
        [&](QNetworkAccessManager &nam) {
            auto *multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);
            multi->append(textPart(QStringLiteral("model_id"), model));
            multi->append(textPart(QStringLiteral("timestamps_granularity"), QStringLiteral("word")));
            multi->append(textPart(QStringLiteral("tag_audio_events"), QStringLiteral("true")));
            multi->append(textPart(QStringLiteral("diarize"), diarize ? QStringLiteral("true") : QStringLiteral("false")));
            if (!language.isEmpty())
                multi->append(textPart(QStringLiteral("language_code"), language));
            if (numSpeakers > 0)
                multi->append(textPart(QStringLiteral("num_speakers"), QString::number(qMin(numSpeakers, 32))));
            for (const QString &term : keyterms)
                multi->append(textPart(QStringLiteral("keyterms"), term));
            QHttpPart filePart;
            filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                               QStringLiteral("form-data; name=\"file\"; filename=\"audio.flac\""));
            filePart.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("audio/flac"));
            auto *file = new QFile(audioPath, multi);
            file->open(QIODevice::ReadOnly);
            filePart.setBodyDevice(file);
            multi->append(filePart);
            QNetworkReply *reply = nam.post(elevenRequest(QStringLiteral("/v1/speech-to-text"), key, 60 * 60 * 1000), multi);
            multi->setParent(reply);
            return reply;
        },
        60 * 60 * 1000, cancel, uploadProgress, error);
    if (error && error->isError())
        return nullptr;
    return transcriptFromScribe(QJsonDocument::fromJson(r.body).object(), model);
}

QByteArray textToSpeech(const QString &key, const QString &voice, const QString &model, const QString &text,
                        const QJsonObject &voiceSettings, const QString &language, const CancelFn &cancel,
                        CloudError *error)
{
    QJsonObject body{{QStringLiteral("text"), text}, {QStringLiteral("model_id"), model}};
    if (!voiceSettings.isEmpty())
        body.insert(QStringLiteral("voice_settings"), voiceSettings);
    if (!language.isEmpty())
        body.insert(QStringLiteral("language_code"), language);
    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    const Response r = run(
        [&](QNetworkAccessManager &nam) {
            QNetworkRequest req = elevenRequest(
                QStringLiteral("/v1/text-to-speech/%1?output_format=mp3_44100_128").arg(voice), key, 5 * 60 * 1000);
            req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
            return nam.post(req, payload);
        },
        5 * 60 * 1000, cancel, {}, error);
    return error && error->isError() ? QByteArray() : r.body;
}

QByteArray soundEffect(const QString &key, const QString &prompt, double durationSeconds,
                       double promptInfluence, bool loop, const CancelFn &cancel, CloudError *error)
{
    QJsonObject body{{QStringLiteral("text"), prompt},
                     {QStringLiteral("prompt_influence"), qBound(0.0, promptInfluence, 1.0)}};
    if (durationSeconds > 0)
        body.insert(QStringLiteral("duration_seconds"), qBound(0.5, durationSeconds, 30.0));
    if (loop)
        body.insert(QStringLiteral("loop"), true);
    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    const Response r = run(
        [&](QNetworkAccessManager &nam) {
            QNetworkRequest req =
                elevenRequest(QStringLiteral("/v1/sound-generation?output_format=mp3_44100_128"), key, 5 * 60 * 1000);
            req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
            return nam.post(req, payload);
        },
        5 * 60 * 1000, cancel, {}, error);
    return error && error->isError() ? QByteArray() : r.body;
}

QJsonObject voices(const QString &key, const QString &search, const QString &pageToken, int limit,
                   CloudError *error)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("page_size"), QString::number(qBound(1, limit, 100)));
    if (!search.isEmpty())
        q.addQueryItem(QStringLiteral("search"), search);
    if (!pageToken.isEmpty())
        q.addQueryItem(QStringLiteral("next_page_token"), pageToken);
    const Response r = run(
        [&](QNetworkAccessManager &nam) {
            return nam.get(elevenRequest(QStringLiteral("/v2/voices?") + q.toString(QUrl::FullyEncoded), key, 30000));
        },
        30000, {}, {}, error);
    if (error && error->isError())
        return {};
    const QJsonObject o = QJsonDocument::fromJson(r.body).object();
    QJsonArray list;
    for (const QJsonValue &v : o.value(QStringLiteral("voices")).toArray()) {
        const QJsonObject voice = v.toObject();
        list.append(QJsonObject{{QStringLiteral("id"), voice.value(QStringLiteral("voice_id"))},
                                {QStringLiteral("name"), voice.value(QStringLiteral("name"))},
                                {QStringLiteral("category"), voice.value(QStringLiteral("category"))},
                                {QStringLiteral("labels"), voice.value(QStringLiteral("labels"))},
                                {QStringLiteral("preview_url"), voice.value(QStringLiteral("preview_url"))}});
    }
    QJsonObject out{{QStringLiteral("voices"), list}};
    if (o.value(QStringLiteral("has_more")).toBool())
        out.insert(QStringLiteral("next"), o.value(QStringLiteral("next_page_token")));
    return out;
}

bool checkKey(const QString &key, CloudError *error)
{
    run([&](QNetworkAccessManager &nam) { return nam.get(elevenRequest(QStringLiteral("/v1/user"), key, 20000)); },
        20000, {}, {}, error);
    return !(error && error->isError());
}

} // namespace elevenlabs

namespace fish {

QByteArray textToSpeech(const QString &key, const QString &model, const QString &voice, const QString &text,
                        double speed, const CancelFn &cancel, CloudError *error)
{
    QJsonObject body{{QStringLiteral("text"), text},
                     {QStringLiteral("format"), QStringLiteral("mp3")},
                     {QStringLiteral("mp3_bitrate"), 128},
                     {QStringLiteral("normalize"), true}};
    if (!voice.isEmpty())
        body.insert(QStringLiteral("reference_id"), voice);
    if (speed > 0 && !qFuzzyCompare(speed, 1.0))
        body.insert(QStringLiteral("prosody"), QJsonObject{{QStringLiteral("speed"), qBound(0.5, speed, 2.0)}});
    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    const Response r = run(
        [&](QNetworkAccessManager &nam) {
            QNetworkRequest req = fishRequest(QStringLiteral("/v1/tts"), key, 5 * 60 * 1000);
            req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
            if (!model.isEmpty())
                req.setRawHeader("model", model.toUtf8());
            return nam.post(req, payload);
        },
        5 * 60 * 1000, cancel, {}, error);
    return error && error->isError() ? QByteArray() : r.body;
}

QJsonObject voices(const QString &key, const QString &search, bool mine, int page, int limit, CloudError *error)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("page_size"), QString::number(qBound(1, limit, 100)));
    q.addQueryItem(QStringLiteral("page_number"), QString::number(qMax(1, page)));
    if (!search.isEmpty())
        q.addQueryItem(QStringLiteral("title"), search);
    if (mine)
        q.addQueryItem(QStringLiteral("self"), QStringLiteral("true"));
    const Response r = run(
        [&](QNetworkAccessManager &nam) {
            return nam.get(fishRequest(QStringLiteral("/model?") + q.toString(QUrl::FullyEncoded), key, 30000));
        },
        30000, {}, {}, error);
    if (error && error->isError())
        return {};
    const QJsonObject o = QJsonDocument::fromJson(r.body).object();
    QJsonArray list;
    for (const QJsonValue &v : o.value(QStringLiteral("items")).toArray()) {
        const QJsonObject voice = v.toObject();
        if (voice.value(QStringLiteral("type")).toString() != QLatin1String("tts"))
            continue;
        list.append(QJsonObject{{QStringLiteral("id"), voice.value(QStringLiteral("_id"))},
                                {QStringLiteral("name"), voice.value(QStringLiteral("title"))},
                                {QStringLiteral("languages"), voice.value(QStringLiteral("languages"))},
                                {QStringLiteral("tags"), voice.value(QStringLiteral("tags"))}});
    }
    QJsonObject out{{QStringLiteral("voices"), list}, {QStringLiteral("total"), o.value(QStringLiteral("total"))}};
    if (o.value(QStringLiteral("has_more")).toBool() || page * limit < o.value(QStringLiteral("total")).toInt())
        out.insert(QStringLiteral("next"), page + 1);
    return out;
}

bool checkKey(const QString &key, CloudError *error)
{
    voices(key, {}, true, 1, 1, error);
    return !(error && error->isError());
}

} // namespace fish

} // namespace drift::cloud
