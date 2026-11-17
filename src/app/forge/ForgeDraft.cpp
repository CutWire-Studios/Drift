#include "app/forge/ForgeDraft.h"

#include "engine/gpu/GlRuntime.h"
#include "engine/packages/animations/AnimationCatalog.h"
#include "engine/packages/animations/AnimationPackageLoader.h"
#include "engine/packages/audio/AudioEffectCatalog.h"
#include "engine/packages/effects/EffectCatalog.h"
#include "engine/packages/effects/EffectPackageLoader.h"
#include "engine/packages/transitions/TransitionCatalog.h"
#include "engine/packages/transitions/TransitionPackageLoader.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>

namespace drift::forge {

namespace {

enum class Kind { Effect, Transition, Animation, Audio, Unknown };

Kind kindOf(const QString &name)
{
    if (name == QLatin1String("effect"))
        return Kind::Effect;
    if (name == QLatin1String("transition"))
        return Kind::Transition;
    if (name == QLatin1String("animation"))
        return Kind::Animation;
    if (name == QLatin1String("audio_effect"))
        return Kind::Audio;
    return Kind::Unknown;
}

QString manifestName(Kind kind)
{
    switch (kind) {
    case Kind::Effect: return QStringLiteral("effect.json");
    case Kind::Transition: return QStringLiteral("transition.json");
    case Kind::Animation: return QStringLiteral("animation.json");
    default: return QStringLiteral("audio-effect.json");
    }
}

bool writeFile(const QString &path, const QByteArray &bytes, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(bytes) != bytes.size()) {
        *error = QStringLiteral("cannot write %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

template <typename Entry>
bool checkGpu(const Entry &entry, QString *error)
{
    if (!error->isEmpty())
        return false;
    if (!entry.gpu.valid) {
        *error = entry.gpu.errorMessage.isEmpty() ? QStringLiteral("invalid pipeline") : entry.gpu.errorMessage;
        return false;
    }
    return true;
}

// Parses `dir`; when `publish` is set also installs the result into the kind's draft slot.
// `gpu` receives the pipeline to test-compile (left invalid for audio).
bool load(Kind kind, const QString &dir, bool publish, drift::GpuEffectDefinition *gpu, QString *error)
{
    switch (kind) {
    case Kind::Effect: {
        EffectPresetEntry entry = EffectPackageLoader::loadPackage(dir, error);
        if (!checkGpu(entry, error))
            return false;
        *gpu = entry.gpu;
        if (publish)
            setDraftEffect(std::move(entry));
        return true;
    }
    case Kind::Transition: {
        TransitionPresetEntry entry = TransitionPackageLoader::loadPackage(dir, error);
        if (!checkGpu(entry, error))
            return false;
        *gpu = entry.gpu;
        if (publish)
            setDraftTransition(std::move(entry));
        return true;
    }
    case Kind::Animation: {
        AnimationPresetEntry entry = AnimationPackageLoader::loadPackage(dir, error);
        if (!checkGpu(entry, error))
            return false;
        *gpu = entry.gpu;
        if (publish)
            setDraftAnimation(std::move(entry));
        return true;
    }
    default: {
        std::optional<AudioEffectEntry> entry = loadAudioEffectPackage(dir, error);
        if (!entry) {
            if (error->isEmpty())
                *error = QStringLiteral("invalid audio effect package");
            return false;
        }
        if (publish)
            setDraftAudio(std::move(entry));
        return true;
    }
    }
}

} // namespace

QString draftDir(const QString &kind)
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .filePath(QStringLiteral("forge-drafts/%1").arg(kind));
}

DraftResult stageDraft(const QString &kindName, const QJsonObject &manifest, const QMap<QString, QByteArray> &files)
{
    DraftResult result;
    const Kind kind = kindOf(kindName);
    if (kind == Kind::Unknown) {
        result.error = QStringLiteral("unknown package kind \"%1\"").arg(kindName);
        return result;
    }
    for (auto it = files.cbegin(); it != files.cend(); ++it) {
        if (it.key().isEmpty() || it.key().contains(QLatin1Char('/')) || it.key().contains(QLatin1Char('\\'))
            || it.key().startsWith(QLatin1Char('.'))) {
            result.error = QStringLiteral("\"%1\" must be a bare file name").arg(it.key());
            return result;
        }
    }

    const QString finalDir = draftDir(kindName);
    const QString stagingDir = finalDir + QStringLiteral(".staging");
    QDir(stagingDir).removeRecursively();
    if (!QDir().mkpath(stagingDir)) {
        result.error = QStringLiteral("cannot create %1").arg(stagingDir);
        return result;
    }
    if (!writeFile(QDir(stagingDir).filePath(manifestName(kind)), QJsonDocument(manifest).toJson(QJsonDocument::Indented),
                   &result.error))
        return result;
    for (auto it = files.cbegin(); it != files.cend(); ++it) {
        if (!writeFile(QDir(stagingDir).filePath(it.key()), it.value(), &result.error))
            return result;
    }

    drift::GpuEffectDefinition gpu;
    if (!load(kind, stagingDir, false, &gpu, &result.error)) {
        QDir(stagingDir).removeRecursively();
        return result;
    }
    if (kind != Kind::Audio) {
        QStringList compileErrors;
        if (!drift::gl::runtime().validateProgram(gpu, &compileErrors)) {
            result.shaderSkipped = true;
        } else if (!compileErrors.isEmpty()) {
            result.shaderLog = compileErrors;
            result.error = compileErrors.join(QLatin1Char('\n'));
            QDir(stagingDir).removeRecursively();
            return result;
        }
    }

    QDir(finalDir).removeRecursively();
    if (!QDir().rename(stagingDir, finalDir)) {
        result.error = QStringLiteral("cannot move %1 to %2").arg(stagingDir, finalDir);
        return result;
    }
    if (!load(kind, finalDir, true, &gpu, &result.error))
        return result;
    result.ok = true;
    return result;
}

void clearDraft(const QString &kindName)
{
    switch (kindOf(kindName)) {
    case Kind::Effect: setDraftEffect(std::nullopt); break;
    case Kind::Transition: setDraftTransition(std::nullopt); break;
    case Kind::Animation: setDraftAnimation(std::nullopt); break;
    case Kind::Audio: setDraftAudio(std::nullopt); break;
    default: return;
    }
    QDir(draftDir(kindName)).removeRecursively();
}

} // namespace drift::forge
