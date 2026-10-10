#include "VoiceLibrary.h"

#include "AppController.h"
#include "JobRegistry.h"
#include "TtsController.h"
#include "engine/AddonRegistry.h"
#include "engine/AudioFileWriter.h"
#include "engine/MediaProbe.h"
#include "engine/SpeechAudio.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>

namespace {

const QString kDefaultId = QStringLiteral("default");
const QString kModelId = QStringLiteral("tts.chatterbox-multilingual");

QString voicesDir()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .filePath(QStringLiteral("voices"));
}

// The built-in voice's reference lives in the addon; only its encoded cache is kept here.
QString voiceDir(const QString &id)
{
    if (id.isEmpty() || id.contains(QLatin1Char('/')) || id.contains(QLatin1Char('\\')) || id.startsWith(QLatin1Char('.')))
        return {};
    return QDir(voicesDir()).filePath(id == kDefaultId ? QStringLiteral("_default") : id);
}

QString modelVersion()
{
    const drift::addon::InstalledAddon *addon = drift::addon::installedAddon(kModelId);
    return addon ? addon->version : QString();
}

QJsonObject readMeta(const QString &dir)
{
    QFile file(QDir(dir).filePath(QStringLiteral("voice.json")));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

bool writeMeta(const QString &dir, const QJsonObject &meta)
{
    QFile file(QDir(dir).filePath(QStringLiteral("voice.json")));
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
           && file.write(QJsonDocument(meta).toJson(QJsonDocument::Indented)) >= 0;
}

bool writeBytes(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
}

constexpr drift::TimeUs kMaxReferenceUs = 20 * drift::kUsPerSecond;

} // namespace

VoiceLibrary::VoiceLibrary(AppController *app, TtsController *tts, QObject *parent)
    : QAbstractListModel(parent)
    , m_app(app)
    , m_tts(tts)
    , m_recorder(this)
{
    connect(&m_recorder, &drift::AudioRecorder::recordingStateChanged, this, &VoiceLibrary::recordingChanged);
    connect(&m_recorder, &drift::AudioRecorder::recordedSecondsChanged, this, &VoiceLibrary::recordingSecondsChanged);
    connect(&m_recorder, &drift::AudioRecorder::audioLevelChanged, this, &VoiceLibrary::levelChanged);
    connect(&m_recorder, &drift::AudioRecorder::recordingError, this, &VoiceLibrary::error);

    m_entries.append({kDefaultId, tr("Default"), QStringLiteral("builtin"), {}, true});
    QList<Entry> saved;
    const QDir dir(voicesDir());
    for (const QString &name : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (name.startsWith(QLatin1Char('_')))
            continue;
        const QJsonObject meta = readMeta(dir.filePath(name));
        if (meta.value(QStringLiteral("name")).toString().isEmpty()
            || !QFile::exists(dir.filePath(name + QStringLiteral("/reference.flac"))))
            continue;
        saved.append({name, meta.value(QStringLiteral("name")).toString(),
                      meta.value(QStringLiteral("source")).toString(),
                      QDateTime::fromString(meta.value(QStringLiteral("created")).toString(), Qt::ISODate), false});
    }
    std::sort(saved.begin(), saved.end(), [](const Entry &a, const Entry &b) { return a.created < b.created; });
    m_entries.append(saved);
}

int VoiceLibrary::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant VoiceLibrary::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size())
        return {};
    const Entry &e = m_entries.at(index.row());
    switch (role) {
    case IdRole:
        return e.id;
    case NameRole:
    case Qt::DisplayRole:
        return e.name;
    case BuiltInRole:
        return e.builtIn;
    case SourceRole:
        return e.source;
    case CreatedRole:
        return e.created;
    }
    return {};
}

QHash<int, QByteArray> VoiceLibrary::roleNames() const
{
    return {{IdRole, "id"}, {NameRole, "name"}, {BuiltInRole, "builtIn"}, {SourceRole, "source"},
            {CreatedRole, "created"}};
}

int VoiceLibrary::indexOfId(const QString &id) const
{
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries.at(i).id == id)
            return i;
    return -1;
}

QString VoiceLibrary::nameFor(const QString &id) const
{
    const int i = indexOfId(id);
    return i < 0 ? QString() : m_entries.at(i).name;
}

QUrl VoiceLibrary::referenceUrl(const QString &id) const
{
    if (indexOfId(id) < 0)
        return {};
    if (id == kDefaultId) {
        const QString dir = drift::resolveChatterboxModelDir();
        return dir.isEmpty() ? QUrl() : QUrl::fromLocalFile(QDir(dir).filePath(QStringLiteral("default_voice.wav")));
    }
    return QUrl::fromLocalFile(QDir(voiceDir(id)).filePath(QStringLiteral("reference.flac")));
}

void VoiceLibrary::setEncoding(bool encoding)
{
    if (m_encoding == encoding)
        return;
    m_encoding = encoding;
    emit encodingChanged();
}

bool VoiceLibrary::writeFlac24k(const QString &path, const std::vector<float> &pcm, QString *err)
{
    drift::AudioFileWriter writer;
    if (!writer.open(path, drift::ChatterboxTts::kSampleRate, 1, err))
        return false;
    if (!writer.writeFrames(pcm.data(), static_cast<int>(pcm.size()), err)) {
        writer.abort();
        return false;
    }
    return writer.finish(err);
}

void VoiceLibrary::addFromMedia(const QString &path, double inSeconds, double outSeconds, const QString &name)
{
    addVoice(path, inSeconds, outSeconds, name, QStringLiteral("clip"), false);
}

void VoiceLibrary::addFromFile(const QString &path, const QString &name)
{
    addVoice(path, 0.0, -1.0, name, QStringLiteral("file"), false);
}

void VoiceLibrary::addVoice(const QString &mediaPath, double inSeconds, double outSeconds, const QString &name,
                            const QString &source, bool removeSource)
{
    struct Outcome
    {
        QString error;
        QDateTime created;
    };
    const QUrl url(mediaPath);
    const QString path = url.isLocalFile() ? url.toLocalFile() : mediaPath;
    const QString trimmed = name.trimmed();
    if (m_encoding) {
        emit error(tr("Another voice is still being prepared"));
        return;
    }
    if (trimmed.isEmpty() || path.isEmpty()) {
        emit error(tr("A voice needs a name and a recording"));
        return;
    }
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QDateTime created = QDateTime::currentDateTimeUtc();
    auto outcome = std::make_shared<Outcome>();
    setEncoding(true);
    const QPointer<VoiceLibrary> self(this);
    TtsController *tts = m_tts;
    m_app->m_jobs->start(
        QStringLiteral("voice-encode"), QStringLiteral("chatterbox"), JobRegistry::Lane::Model,
        [=](JobContext &ctx) {
            ctx.progress(0.05, QStringLiteral("Reading the recording…"));
            drift::TimeUs inUs = drift::secondsToUs(qMax(0.0, inSeconds));
            drift::TimeUs outUs = outSeconds < 0 ? MediaProbe::probe(path).durationUs
                                                  : drift::secondsToUs(outSeconds);
            outUs = qMin(outUs, inUs + kMaxReferenceUs);
            const std::vector<float> pcm = drift::readMono(path, inUs, outUs, drift::ChatterboxTts::kSampleRate);
            if (removeSource)
                QFile::remove(path);
            if (pcm.size() < static_cast<size_t>(kMinReferenceSeconds * drift::ChatterboxTts::kSampleRate)) {
                outcome->error = QObject::tr("The voice sample must be at least %1 seconds of speech")
                                     .arg(static_cast<int>(kMinReferenceSeconds));
                return ctx.fail(QStringLiteral("bad_args"), outcome->error);
            }
            if (ctx.cancelled())
                return;
            const QString dir = voiceDir(id);
            QString err;
            QDir().mkpath(dir);
            ctx.progress(0.2, QStringLiteral("Loading voice model…"));
            if (!tts->ensureLoaded(&err)) {
                outcome->error = err;
                return ctx.fail(QStringLiteral("internal"), err);
            }
            ctx.progress(0.7, QStringLiteral("Learning the voice…"));
            const auto conditioning = tts->engine().encodeVoice(pcm, &err);
            if (!conditioning) {
                outcome->error = err;
                return ctx.fail(QStringLiteral("internal"), err);
            }
            if (!writeFlac24k(QDir(dir).filePath(QStringLiteral("reference.flac")), pcm, &err)
                || !writeBytes(QDir(dir).filePath(QStringLiteral("conditioning.bin")), conditioning->serialize())
                || !writeMeta(dir, {{QStringLiteral("name"), name.trimmed()},
                                    {QStringLiteral("created"), created.toString(Qt::ISODate)},
                                    {QStringLiteral("source"), source},
                                    {QStringLiteral("modelId"), kModelId},
                                    {QStringLiteral("modelVersion"), modelVersion()}})) {
                outcome->error = err.isEmpty() ? QObject::tr("Could not save the voice") : err;
                return ctx.fail(QStringLiteral("internal"), outcome->error);
            }
            ctx.succeed({});
        },
        [self, id, trimmed, source, created, outcome](const QJsonObject &job) {
            if (!self)
                return QJsonObject{};
            self->setEncoding(false);
            if (!job.value(QStringLiteral("ok")).toBool()) {
                QDir(voiceDir(id)).removeRecursively();
                if (job.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString()
                    != QLatin1String("cancelled"))
                    emit self->error(outcome->error.isEmpty() ? VoiceLibrary::tr("Could not prepare the voice") : outcome->error);
                return QJsonObject{};
            }
            const int row = self->m_entries.size();
            self->beginInsertRows({}, row, row);
            self->m_entries.append({id, trimmed, source, created, false});
            self->endInsertRows();
            emit self->voiceAdded(id);
            return QJsonObject{};
        });
}

void VoiceLibrary::rename(const QString &id, const QString &name)
{
    const int row = indexOfId(id);
    const QString trimmed = name.trimmed();
    if (row < 0 || m_entries.at(row).builtIn || trimmed.isEmpty())
        return;
    const QString dir = voiceDir(id);
    QJsonObject meta = readMeta(dir);
    meta.insert(QStringLiteral("name"), trimmed);
    writeMeta(dir, meta);
    m_entries[row].name = trimmed;
    emit dataChanged(index(row), index(row), {NameRole, Qt::DisplayRole});
}

void VoiceLibrary::remove(const QString &id)
{
    const int row = indexOfId(id);
    if (row < 0 || m_entries.at(row).builtIn)
        return;
    QDir(voiceDir(id)).removeRecursively();
    beginRemoveRows({}, row, row);
    m_entries.removeAt(row);
    endRemoveRows();
}

void VoiceLibrary::startRecording()
{
    if (m_recorder.isRecording() || m_app->m_audioRecorder.isRecording()) {
        emit error(tr("A recording is already in progress"));
        return;
    }
    const QString deviceName = m_app->m_audioRecorder.currentDeviceName();
    for (const QVariant &d : m_app->m_audioRecorder.availableDevices()) {
        const QVariantMap device = d.toMap();
        if (device.value(QStringLiteral("name")).toString() == deviceName) {
            m_recorder.selectDevice(device.value(QStringLiteral("id")).toString());
            break;
        }
    }
    m_recorder.setGain(m_app->m_audioRecorder.gain());
    m_recordingPath = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                          .filePath(QStringLiteral("drift-voice-%1.flac")
                                        .arg(QUuid::createUuid().toString(QUuid::Id128).left(8)));
    QString err;
    if (!m_recorder.startRecording(-1, m_recordingPath, &err))
        emit error(err.isEmpty() ? tr("Failed to start audio recording") : err);
}

void VoiceLibrary::stopRecordingAndAdd(const QString &name)
{
    if (!m_recorder.isRecording())
        return;
    drift::TimeUs durationUs = 0;
    const QString path = m_recorder.stopRecording(&durationUs);
    if (path.isEmpty())
        return;
    if (durationUs < drift::secondsToUs(kMinReferenceSeconds)) {
        QFile::remove(path);
        emit error(tr("The recording is too short: record at least %1 seconds").arg(static_cast<int>(kMinReferenceSeconds)));
        return;
    }
    addVoice(path, 0.0, -1.0, name, QStringLiteral("mic"), true);
}

void VoiceLibrary::cancelRecording()
{
    if (m_recorder.isRecording())
        m_recorder.cancelRecording();
}

std::optional<drift::VoiceConditioning> VoiceLibrary::conditioning(const QString &id, QString *err)
{
    const QString voiceId = id.isEmpty() ? kDefaultId : id;
    const QString dir = voiceDir(voiceId);
    QString reference;
    if (dir.isEmpty()) {
        *err = QStringLiteral("Unknown voice");
        return std::nullopt;
    }
    if (voiceId == kDefaultId) {
        const QString modelDir = drift::resolveChatterboxModelDir();
        if (!modelDir.isEmpty())
            reference = QDir(modelDir).filePath(QStringLiteral("default_voice.wav"));
        QDir().mkpath(dir);
    } else {
        reference = QDir(dir).filePath(QStringLiteral("reference.flac"));
    }
    if (reference.isEmpty() || !QFile::exists(reference)) {
        *err = QStringLiteral("The reference recording for this voice is missing");
        return std::nullopt;
    }

    const QString version = modelVersion();
    QJsonObject meta = readMeta(dir);
    const QString cachePath = QDir(dir).filePath(QStringLiteral("conditioning.bin"));
    if (meta.value(QStringLiteral("modelVersion")).toString() == version && QFile::exists(cachePath)) {
        QFile file(cachePath);
        if (file.open(QIODevice::ReadOnly)) {
            if (auto cached = drift::VoiceConditioning::deserialize(file.readAll()))
                return cached;
        }
    }

    if (!m_tts->ensureLoaded(err))
        return std::nullopt;
    const std::vector<float> pcm = drift::readMono(reference, 0, kMaxReferenceUs, drift::ChatterboxTts::kSampleRate);
    if (pcm.empty()) {
        *err = QStringLiteral("Could not read the reference recording");
        return std::nullopt;
    }
    auto fresh = m_tts->engine().encodeVoice(pcm, err);
    if (!fresh)
        return std::nullopt;
    if (writeBytes(cachePath, fresh->serialize())) {
        meta.insert(QStringLiteral("modelId"), kModelId);
        meta.insert(QStringLiteral("modelVersion"), version);
        writeMeta(dir, meta);
    }
    return fresh;
}
