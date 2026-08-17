#include "AudioEffectCatalog.h"

#include "GpuPackageParse.h"
#include "engine/audio/AudioEffectFactory.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSet>

#include <algorithm>
#include <optional>

namespace {

QList<AudioEffectEntry> g_catalog;
bool g_loaded = false;

// User-facing labels for the known category slugs; unknown slugs are title-cased as a fallback.
QString labelForCategory(const QString &slug)
{
    if (slug == QLatin1String("voice"))
        return QCoreApplication::translate("AudioEffectCatalog", "Voice");
    if (slug == QLatin1String("transmission"))
        return QCoreApplication::translate("AudioEffectCatalog", "Transmission");
    if (slug == QLatin1String("texture"))
        return QCoreApplication::translate("AudioEffectCatalog", "Texture");
    if (slug == QLatin1String("space"))
        return QCoreApplication::translate("AudioEffectCatalog", "Space");
    if (slug.isEmpty())
        return QCoreApplication::translate("AudioEffectCatalog", "Other");
    QString label = slug;
    label[0] = label[0].toUpper();
    return label;
}

std::optional<AudioEffectEntry> loadManifest(const QString &packageDir, QString *errorOut)
{
    const QString jsonPath = QDir(packageDir).filePath(QStringLiteral("audio-effect.json"));
    QString readError;
    const QString jsonText = GpuPackageParse::readTextFile(jsonPath, &readError);
    if (jsonText.isEmpty()) {
        if (errorOut)
            *errorOut = readError.isEmpty() ? QStringLiteral("empty audio-effect.json") : readError;
        return std::nullopt;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonText.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorOut)
            *errorOut = QStringLiteral("invalid JSON: %1").arg(parseError.errorString());
        return std::nullopt;
    }

    const QJsonObject root = doc.object();
    const QString backend = root.value(QStringLiteral("backend")).toString();
    if (backend != QLatin1String("juce")) {
        if (errorOut) {
            if (backend.isEmpty())
                *errorOut = QStringLiteral("audio-effect.json missing backend");
            else if (backend == QLatin1String("avfilter"))
                *errorOut = QStringLiteral(
                    "backend 'avfilter' is no longer supported; audio effects are JUCE processors");
            else
                *errorOut = QStringLiteral("unsupported backend '%1'").arg(backend);
        }
        return std::nullopt;
    }

    AudioEffectEntry entry;
    entry.id = root.value(QStringLiteral("id")).toString();
    if (entry.id.isEmpty()) {
        if (errorOut)
            *errorOut = QStringLiteral("audio-effect.json missing id");
        return std::nullopt;
    }
    entry.displayName = root.value(QStringLiteral("displayName")).toString();
    if (entry.displayName.isEmpty())
        entry.displayName = entry.id;
    const QString categoryRaw = root.value(QStringLiteral("category")).toString();
    entry.category = GpuPackageParse::slugifyCategory(categoryRaw.isEmpty() ? QStringLiteral("space")
                                                                            : categoryRaw);
    entry.order = root.value(QStringLiteral("order")).toInt(0);
    entry.prerollMs = root.value(QStringLiteral("prerollMs")).toInt(0);

    entry.processorId = root.value(QStringLiteral("processor")).toString();
    if (entry.processorId.isEmpty()) {
        if (errorOut)
            *errorOut = QStringLiteral("audio-effect.json missing processor");
        return std::nullopt;
    }
    // Reject at load rather than at playback: a manifest naming a processor nobody implements would
    // otherwise show up in the browser and then do nothing.
    if (!drift::audiofx::hasProcessor(entry.processorId)) {
        if (errorOut)
            *errorOut = QStringLiteral("unknown processor '%1'").arg(entry.processorId);
        return std::nullopt;
    }

    // Parameters share the GPU effect param schema (identifier/displayName/min/max/default); reuse
    // the same parser so both catalogs accept identical JSON. GPU-specific rules are off here.
    QString paramError;
    if (!GpuPackageParse::parseParameters(root.value(QStringLiteral("parameters")).toArray(),
                                          &entry.parameters, /*gpu=*/false, &paramError)) {
        if (errorOut)
            *errorOut = paramError;
        return std::nullopt;
    }

    entry.icon = root.value(QStringLiteral("icon")).toString();
    // Optional thumbnail: explicit relative/absolute path, else thumbnail.png in the package.
    QString thumbRel = root.value(QStringLiteral("thumbnail")).toString();
    if (thumbRel.isEmpty())
        thumbRel = QStringLiteral("thumbnail.png");
    entry.thumbnailPath = GpuPackageParse::resolvePackageAsset(packageDir, thumbRel);

    entry.packageDir = packageDir;
    return entry;
}

QList<AudioEffectEntry> scanDirectories(const QStringList &rootDirs)
{
    QList<AudioEffectEntry> loaded;
    QSet<QString> seenIds;

    for (const QString &root : rootDirs) {
        if (root.isEmpty())
            continue;
        QDir dir(root);
        if (!dir.exists())
            continue;

        const QFileInfoList subdirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &info : subdirs) {
            const QString packageDir = info.absoluteFilePath();
            if (!QFileInfo::exists(QDir(packageDir).filePath(QStringLiteral("audio-effect.json"))))
                continue;

            QString error;
            const std::optional<AudioEffectEntry> entry = loadManifest(packageDir, &error);
            if (!entry) {
                qWarning("AudioEffectCatalog: skip %s: %s", qPrintable(packageDir),
                         qPrintable(error.isEmpty() ? QStringLiteral("invalid package") : error));
                continue;
            }
            // Higher-priority roots (installed addons, DRIFT_*_DIR) intentionally supersede the
            // bundled <appDir>/audio-effects copy — expected, so silent.
            if (seenIds.contains(entry->id))
                continue;
            seenIds.insert(entry->id);
            loaded.append(*entry);
        }
    }

    std::sort(loaded.begin(), loaded.end(), [](const AudioEffectEntry &a, const AudioEffectEntry &b) {
        if (a.order != b.order)
            return a.order < b.order;
        return a.id < b.id;
    });
    return loaded;
}

void ensureLoaded()
{
    if (!g_loaded)
        reloadAudioEffectCatalog();
}

} // namespace

QStringList defaultAudioEffectSearchPaths()
{
    return GpuPackageParse::defaultSearchPaths(QStringLiteral("DRIFT_AUDIO_EFFECTS_DIR"),
                                               QStringLiteral("audio-effects"),
                                               QStringLiteral("audio-effects"));
}

void reloadAudioEffectCatalog(const QStringList &packageRoots)
{
    const QStringList roots = packageRoots.isEmpty() ? defaultAudioEffectSearchPaths() : packageRoots;
    g_catalog = scanDirectories(roots);
    g_loaded = true;
}

const QList<AudioEffectEntry> &audioEffectCatalog()
{
    ensureLoaded();
    return g_catalog;
}

const AudioEffectEntry *audioEffectDefForId(const QString &id)
{
    ensureLoaded();
    for (const AudioEffectEntry &entry : g_catalog) {
        if (entry.id == id)
            return &entry;
    }
    return nullptr;
}

QMap<QString, QVariant> resolvedAudioEffectParameters(const drift::Effect &effect,
                                                      const AudioEffectEntry &def)
{
    QMap<QString, QVariant> values;
    for (const drift::EffectParamSpec &spec : def.parameters) {
        const auto it = effect.parameters.constFind(spec.key);
        values.insert(spec.key, it != effect.parameters.constEnd() ? it.value()
                                                                    : QVariant(spec.defaultValue));
    }
    return values;
}

QVector<drift::AudioEffectSpec> audioEffectSpecsFor(const QList<drift::Effect> &effects)
{
    QVector<drift::AudioEffectSpec> specs;
    specs.reserve(effects.size());

    for (const drift::Effect &effect : effects) {
        if (!effect.enabled)
            continue;
        const AudioEffectEntry *def = audioEffectDefForId(effect.catalogId);
        if (!def)
            continue; // addon not installed — pass the audio through untouched

        drift::AudioEffectSpec spec;
        spec.processorId = def->processorId;
        spec.prerollMs = def->prerollMs;

        const QMap<QString, QVariant> values = resolvedAudioEffectParameters(effect, *def);
        for (auto it = values.constBegin(); it != values.constEnd(); ++it)
            spec.parameters.insert(it.key(), static_cast<float>(it.value().toDouble()));

        specs.append(spec);
    }

    return specs;
}

QStringList audioEffectPresetIds()
{
    ensureLoaded();
    QStringList ids;
    ids.reserve(g_catalog.size());
    for (const AudioEffectEntry &entry : g_catalog)
        ids.append(entry.id);
    return ids;
}

QList<QPair<QString, QString>> audioEffectCategories()
{
    ensureLoaded();
    QList<QPair<QString, QString>> categories;
    QSet<QString> seen;
    for (const AudioEffectEntry &entry : g_catalog) {
        if (seen.contains(entry.category))
            continue;
        seen.insert(entry.category);
        categories.append({entry.category, labelForCategory(entry.category)});
    }
    return categories;
}

QString audioEffectCategoryLabel(const QString &categoryId)
{
    return labelForCategory(categoryId);
}
