#include "TtsController.h"

#include "AppController.h"
#include "JobRegistry.h"
#include "VoiceLibrary.h"
#include "engine/AddonRegistry.h"
#include "engine/AudioFileWriter.h"
#include "engine/OrtRuntime.h"

#include <QDir>
#include <QFile>
#include <QLocale>
#include <QPointer>
#include <QSettings>
#include <QUuid>

#include <memory>

namespace {

QString slugFor(const QString &text)
{
    QString slug;
    for (const QChar c : text.toLower()) {
        if (c.isLetterOrNumber())
            slug.append(c);
        else if (!slug.isEmpty() && !slug.endsWith(QLatin1Char('-')))
            slug.append(QLatin1Char('-'));
        if (slug.size() >= 24)
            break;
    }
    while (slug.endsWith(QLatin1Char('-')))
        slug.chop(1);
    return slug.isEmpty() ? QStringLiteral("speech") : slug;
}

QString speechPath(const QString &text)
{
    return QDir(drift::generatedAudioDir())
        .filePath(QStringLiteral("tts-chatterbox-%1-%2.flac")
                      .arg(slugFor(text), QUuid::createUuid().toString(QUuid::Id128).left(8)));
}

QString jobError(const QJsonObject &job)
{
    const QJsonObject error = job.value(QStringLiteral("error")).toObject();
    return error.value(QStringLiteral("code")).toString() == QLatin1String("cancelled")
               ? QString()
               : error.value(QStringLiteral("message")).toString();
}

struct CueSpeech
{
    QString path;
    QString text;
    drift::TimeUs startUs = 0;
    double cueSeconds = 0.0;
    double speechSeconds = 0.0;
};

} // namespace

TtsController::TtsController(AppController *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
{
    m_poll.setInterval(150);
    connect(&m_poll, &QTimer::timeout, this, &TtsController::pollJob);
    refreshAvailability();
}

void TtsController::refreshAvailability()
{
    const bool now = !drift::resolveChatterboxModelDir().isEmpty() && drift::ort::available();
    if (now == m_available)
        return;
    m_available = now;
    emit availableChanged();
}

QString TtsController::lastVoice() const
{
    return QSettings().value(QStringLiteral("tts/voice"), QStringLiteral("default")).toString();
}

QString TtsController::lastLanguage() const
{
    return QSettings().value(QStringLiteral("tts/language"), QStringLiteral("en")).toString();
}

double TtsController::lastExaggeration() const
{
    return QSettings().value(QStringLiteral("tts/exaggeration"), 0.5).toDouble();
}

void TtsController::remember(const QString &voiceId, const QString &lang, double exaggeration)
{
    QSettings settings;
    settings.setValue(QStringLiteral("tts/voice"), voiceId);
    settings.setValue(QStringLiteral("tts/language"), lang);
    settings.setValue(QStringLiteral("tts/exaggeration"), exaggeration);
    emit lastUsedChanged();
}

QVariantList TtsController::languageOptions() const
{
    QVariantList out;
    for (const QString &code : drift::ChatterboxTts::supportedLanguages()) {
        out.append(QVariantMap{{QStringLiteral("code"), code},
                               {QStringLiteral("label"), QLocale::languageToString(QLocale(code).language())}});
    }
    return out;
}

bool TtsController::ensureLoaded(QString *err)
{
    if (m_loaded.load())
        return true;
    m_loading.store(true);
    const bool ok = m_engine.load(err);
    m_loaded.store(ok);
    m_loading.store(false);
    return ok;
}

void TtsController::setStatus(const QString &status)
{
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged();
}

void TtsController::pollJob()
{
    if (m_shownLoaded != m_loaded.load() || m_shownLoading != m_loading.load()) {
        m_shownLoaded = m_loaded.load();
        m_shownLoading = m_loading.load();
        emit loadStateChanged();
    }
    if (!m_jobId.isEmpty()) {
        const QJsonObject job = m_app->m_jobs->job(m_jobId);
        const double progress = job.value(QStringLiteral("progress")).toDouble();
        if (!qFuzzyCompare(1.0 + progress, 1.0 + m_progress)) {
            m_progress = progress;
            emit progressChanged();
        }
        const QString status = job.value(QStringLiteral("status")).toString();
        if (!status.isEmpty())
            setStatus(status);
    }
    if (m_jobId.isEmpty() && m_loadJobId.isEmpty() && !m_loading.load())
        m_poll.stop();
}

void TtsController::endJob()
{
    m_jobId.clear();
    m_running = false;
    m_progress = 0.0;
    emit runningChanged();
    emit progressChanged();
    pollJob();
}

void TtsController::warmUp()
{
    if (!m_available || m_loaded.load() || m_loading.load() || !m_loadJobId.isEmpty())
        return;
    m_loading.store(true);
    const QPointer<TtsController> self(this);
    m_loadJobId = m_app->m_jobs->start(
        QStringLiteral("tts-load"), QStringLiteral("chatterbox"), JobRegistry::Lane::Model,
        [this](JobContext &ctx) {
            ctx.progress(0.0, QStringLiteral("Loading voice model…"));
            QString err;
            if (!ensureLoaded(&err))
                return ctx.fail(QStringLiteral("internal"), err);
            ctx.succeed({});
        },
        [self](const QJsonObject &job) {
            if (!self)
                return QJsonObject{};
            self->m_loadJobId.clear();
            self->pollJob();
            if (!job.value(QStringLiteral("ok")).toBool())
                emit self->finished(false, jobError(job));
            return QJsonObject{};
        });
    m_poll.start();
    pollJob();
}

void TtsController::cancel()
{
    if (!m_jobId.isEmpty())
        m_app->m_jobs->cancel(m_jobId);
}

void TtsController::generate(const QString &text, const QString &voiceId, const QString &lang,
                             double exaggeration)
{
    const QString script = text.trimmed();
    if (m_running || script.isEmpty())
        return;
    if (!m_available) {
        emit finished(false, tr("Install the voice model first"));
        return;
    }
    remember(voiceId, lang, exaggeration);
    const QString path = speechPath(script);
    const QString voiceName = m_voices->nameFor(voiceId);
    const QJsonObject generator{{QStringLiteral("engine"), QStringLiteral("chatterbox")},
                                {QStringLiteral("voice"), voiceName},
                                {QStringLiteral("lang"), lang},
                                {QStringLiteral("exaggeration"), exaggeration},
                                {QStringLiteral("text"), script}};
    m_running = true;
    m_progress = 0.0;
    emit runningChanged();
    emit progressChanged();
    setStatus(tr("Queued"));
    const QPointer<TtsController> self(this);
    m_jobId = m_app->m_jobs->start(
        QStringLiteral("tts"), QStringLiteral("chatterbox"), JobRegistry::Lane::Model,
        [this, script, voiceId, lang, exaggeration, path](JobContext &ctx) {
            QString err;
            ctx.progress(0.0, QStringLiteral("Loading voice model…"));
            if (!ensureLoaded(&err))
                return ctx.fail(QStringLiteral("internal"), err);
            if (ctx.cancelled())
                return;
            ctx.progress(0.02, QStringLiteral("Preparing voice…"));
            const auto voice = m_voices->conditioning(voiceId, &err);
            if (!voice)
                return ctx.fail(QStringLiteral("internal"), err);
            const std::vector<float> pcm = m_engine.synthesize(
                script, lang, *voice, static_cast<float>(exaggeration),
                [&ctx](double p) {
                    ctx.progress(0.05 + 0.9 * p, QStringLiteral("Generating speech…"));
                    return !ctx.cancelled();
                },
                &err);
            if (ctx.cancelled())
                return;
            if (pcm.empty())
                return ctx.fail(QStringLiteral("internal"), err.isEmpty() ? QStringLiteral("No speech was produced") : err);
            if (!VoiceLibrary::writeFlac24k(path, pcm, &err))
                return ctx.fail(QStringLiteral("internal"), err);
            ctx.succeed({});
        },
        [self, path, generator](const QJsonObject &job) {
            if (!self)
                return QJsonObject{};
            self->endJob();
            if (!job.value(QStringLiteral("ok")).toBool()) {
                QFile::remove(path);
                const QString message = jobError(job);
                self->setStatus(message.isEmpty() ? tr("Cancelled") : message);
                emit self->finished(false, self->m_status);
                return QJsonObject{};
            }
            const AppController::GeneratedAudioImport imported = self->m_app->importGeneratedAudio(
                path, generator, true, self->m_app->playheadSeconds(), -1);
            if (!imported.ok) {
                self->setStatus(imported.error);
                emit self->finished(false, imported.error);
                return QJsonObject{};
            }
            self->setStatus(tr("Speech added to the timeline"));
            emit self->finished(true, self->m_status);
            return QJsonObject{};
        });
    m_poll.start();
}

void TtsController::voiceoverFromSubtitles(int trackIndex, int clipIndex, const QString &voiceId,
                                           const QString &lang, double exaggeration)
{
    if (m_running)
        return;
    if (!m_available) {
        emit finished(false, tr("Install the voice model first"));
        return;
    }
    if (!m_app->isValidClipIndex(trackIndex, clipIndex)
        || m_app->m_project.tracks().at(trackIndex).clips.at(clipIndex).type != drift::ClipType::Subtitle) {
        emit finished(false, tr("Select a subtitle clip first"));
        return;
    }
    const drift::Clip &clip = m_app->m_project.tracks().at(trackIndex).clips.at(clipIndex);
    const drift::TimeUs clipStartUs = clip.timelineStart;
    auto lines = std::make_shared<QList<CueSpeech>>();
    for (const drift::SubtitleCue &cue : clip.subtitleCues) {
        const QString text = cue.text.simplified();
        if (text.isEmpty())
            continue;
        lines->append({speechPath(text), text, cue.startUs, drift::usToSeconds(cue.endUs - cue.startUs), 0.0});
    }
    if (lines->isEmpty()) {
        emit finished(false, tr("This subtitle clip has no text to speak"));
        return;
    }
    remember(voiceId, lang, exaggeration);
    const QString voiceName = m_voices->nameFor(voiceId);
    m_running = true;
    m_progress = 0.0;
    emit runningChanged();
    emit progressChanged();
    setStatus(tr("Queued"));
    const QPointer<TtsController> self(this);
    m_jobId = m_app->m_jobs->start(
        QStringLiteral("tts"), QStringLiteral("chatterbox"), JobRegistry::Lane::Model,
        [this, lines, voiceId, lang, exaggeration](JobContext &ctx) {
            QString err;
            ctx.progress(0.0, QStringLiteral("Loading voice model…"));
            if (!ensureLoaded(&err))
                return ctx.fail(QStringLiteral("internal"), err);
            if (ctx.cancelled())
                return;
            ctx.progress(0.02, QStringLiteral("Preparing voice…"));
            const auto voice = m_voices->conditioning(voiceId, &err);
            if (!voice)
                return ctx.fail(QStringLiteral("internal"), err);
            const int count = static_cast<int>(lines->size());
            for (int i = 0; i < count; ++i) {
                CueSpeech &line = (*lines)[i];
                const std::vector<float> pcm = m_engine.synthesize(
                    line.text, lang, *voice, static_cast<float>(exaggeration),
                    [&ctx, i, count](double p) {
                        ctx.progress(0.05 + 0.95 * (i + p) / count,
                                     QStringLiteral("Speaking line %1 of %2…").arg(i + 1).arg(count));
                        return !ctx.cancelled();
                    },
                    &err);
                if (ctx.cancelled())
                    return;
                if (pcm.empty())
                    return ctx.fail(QStringLiteral("internal"),
                                    err.isEmpty() ? QStringLiteral("No speech was produced") : err);
                if (!VoiceLibrary::writeFlac24k(line.path, pcm, &err))
                    return ctx.fail(QStringLiteral("internal"), err);
                line.speechSeconds = static_cast<double>(pcm.size()) / drift::ChatterboxTts::kSampleRate;
            }
            ctx.succeed({});
        },
        [self, lines, lang, exaggeration, voiceName, clipStartUs](const QJsonObject &job) {
            if (!self)
                return QJsonObject{};
            self->endJob();
            if (!job.value(QStringLiteral("ok")).toBool()) {
                for (const CueSpeech &line : std::as_const(*lines))
                    QFile::remove(line.path);
                const QString message = jobError(job);
                self->setStatus(message.isEmpty() ? tr("Cancelled") : message);
                emit self->finished(false, self->m_status);
                return QJsonObject{};
            }
            QList<AppController::GeneratedAudioPlacement> placements;
            int longer = 0;
            for (const CueSpeech &line : std::as_const(*lines)) {
                const QJsonObject generator{{QStringLiteral("engine"), QStringLiteral("chatterbox")},
                                            {QStringLiteral("voice"), voiceName},
                                            {QStringLiteral("lang"), lang},
                                            {QStringLiteral("exaggeration"), exaggeration},
                                            {QStringLiteral("text"), line.text}};
                if (!self->m_app->importGeneratedAudio(line.path, generator, false, 0.0, -1).ok)
                    continue;
                placements.append({line.path, drift::usToSeconds(clipStartUs + line.startUs)});
                if (line.speechSeconds > line.cueSeconds)
                    ++longer;
            }
            const int placed = self->m_app->placeGeneratedAudioOnNewTracks(placements, tr("Generate voiceover"));
            if (placed == 0) {
                self->setStatus(tr("Could not add the voiceover"));
                emit self->finished(false, self->m_status);
                return QJsonObject{};
            }
            QString message = tr("Generated %n line(s)", nullptr, placed);
            if (longer > 0)
                message += tr("; %n ran longer than their captions", nullptr, longer);
            self->setStatus(message);
            emit self->finished(true, message);
            return QJsonObject{};
        });
    m_poll.start();
}
