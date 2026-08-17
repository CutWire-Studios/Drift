#include "EffectTemplateCatalog.h"

#include "EffectCatalog.h"
#include "GpuPackageParse.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <optional>

namespace {

QList<EffectTemplateEntry> g_catalog;
bool g_loaded = false;

QString labelForCategory(const QString &slug)
{
    if (slug == QLatin1String("rhythm") || slug == QLatin1String("hype"))
        return QCoreApplication::translate("EffectTemplateCatalog", "Hype");
    if (slug == QLatin1String("look") || slug == QLatin1String("dreamy"))
        return QCoreApplication::translate("EffectTemplateCatalog", "Dreamy");
    if (slug == QLatin1String("cinematic"))
        return QCoreApplication::translate("EffectTemplateCatalog", "Cinematic");
    if (slug == QLatin1String("clone"))
        return QCoreApplication::translate("EffectTemplateCatalog", "Clone");
    if (slug == QLatin1String("anime"))
        return QCoreApplication::translate("EffectTemplateCatalog", "Anime");
    if (slug == QLatin1String("retro"))
        return QCoreApplication::translate("EffectTemplateCatalog", "Retro");
    if (slug == QLatin1String("chaos"))
        return QCoreApplication::translate("EffectTemplateCatalog", "Chaos");
    if (slug == QLatin1String("drama"))
        return QCoreApplication::translate("EffectTemplateCatalog", "Drama");
    if (slug == QLatin1String("transition"))
        return QCoreApplication::translate("EffectTemplateCatalog", "Transition");
    if (slug.isEmpty())
        return QCoreApplication::translate("EffectTemplateCatalog", "Other");
    QString label = slug;
    label[0] = label[0].toUpper();
    return label;
}

std::optional<EffectTemplatePulse> parsePulse(const QJsonObject &obj)
{
    if (obj.isEmpty())
        return std::nullopt;

    EffectTemplatePulse pulse;
    pulse.param = obj.value(QStringLiteral("param")).toString();
    if (pulse.param.isEmpty())
        return std::nullopt;
    pulse.rest = obj.value(QStringLiteral("rest")).toDouble(0.0);
    pulse.peak = obj.value(QStringLiteral("peak")).toDouble(1.0);
    pulse.decayMs = obj.value(QStringLiteral("decayMs")).toInt(100);
    pulse.valid = true;
    return pulse;
}

std::optional<EffectTemplateSpeedPulse> parseSpeedPulse(const QJsonObject &obj)
{
    if (obj.isEmpty())
        return std::nullopt;

    EffectTemplateSpeedPulse pulse;
    pulse.rest = obj.value(QStringLiteral("rest")).toDouble(1.0);
    pulse.peak = obj.value(QStringLiteral("peak")).toDouble(0.4);
    pulse.decayMs = obj.value(QStringLiteral("decayMs")).toInt(600);
    pulse.valid = true;
    return pulse;
}

bool parseLayerObject(const QJsonObject &layerObj, EffectTemplateLayer *layerOut)
{
    const QString effectId = layerObj.value(QStringLiteral("effectId")).toString();
    if (effectId.isEmpty() || !effectDefForId(effectId))
        return false;

    layerOut->effectId = effectId;
    const QJsonObject params = layerObj.value(QStringLiteral("params")).toObject();
    for (auto it = params.constBegin(); it != params.constEnd(); ++it)
        layerOut->params.insert(it.key(), it.value().toVariant());

    if (const std::optional<EffectTemplatePulse> pulse =
            parsePulse(layerObj.value(QStringLiteral("pulse")).toObject())) {
        layerOut->pulse = *pulse;
    }
    return true;
}

std::optional<EffectTemplateTrack> parseTrack(const QJsonObject &trackObj, const QString &packageDir)
{
    const QString role = trackObj.value(QStringLiteral("role")).toString();
    if (role.isEmpty())
        return std::nullopt;

    EffectTemplateTrack track;
    track.role = role;
    track.opacity = trackObj.value(QStringLiteral("opacity")).toDouble(1.0);

    const QJsonArray layers = trackObj.value(QStringLiteral("layers")).toArray();
    for (const QJsonValue &layerVal : layers) {
        if (!layerVal.isObject())
            continue;
        EffectTemplateLayer layer;
        if (!parseLayerObject(layerVal.toObject(), &layer))
            qWarning("EffectTemplateCatalog: skip layer in %s track '%s' (unknown effect)",
                     qPrintable(packageDir), qPrintable(role));
        else
            track.layers.append(layer);
    }

    if (const std::optional<EffectTemplateSpeedPulse> speedPulse =
            parseSpeedPulse(trackObj.value(QStringLiteral("speedPulse")).toObject())) {
        track.speedPulse = *speedPulse;
    }

    return track;
}

std::optional<EffectTemplateEntry> loadManifest(const QString &packageDir, QString *errorOut)
{
    const QString jsonPath = QDir(packageDir).filePath(QStringLiteral("template.json"));
    QString readError;
    const QString jsonText = GpuPackageParse::readTextFile(jsonPath, &readError);
    if (jsonText.isEmpty()) {
        if (errorOut)
            *errorOut = readError.isEmpty() ? QStringLiteral("empty template.json") : readError;
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
    EffectTemplateEntry entry;
    entry.id = root.value(QStringLiteral("id")).toString();
    if (entry.id.isEmpty()) {
        if (errorOut)
            *errorOut = QStringLiteral("template.json missing id");
        return std::nullopt;
    }
    entry.displayName = root.value(QStringLiteral("displayName")).toString();
    if (entry.displayName.isEmpty())
        entry.displayName = entry.id;
    const QString categoryRaw = root.value(QStringLiteral("category")).toString();
    entry.category = GpuPackageParse::slugifyCategory(categoryRaw.isEmpty() ? QStringLiteral("rhythm")
                                                                              : categoryRaw);
    entry.order = root.value(QStringLiteral("order")).toInt(0);
    entry.sync = root.value(QStringLiteral("sync")).toString(QStringLiteral("onset"));
    entry.requiresSegmentation =
        root.value(QStringLiteral("requiresSegmentation")).toBool(false);

    const QJsonObject clonesObj = root.value(QStringLiteral("clones")).toObject();
    if (!clonesObj.isEmpty()) {
        entry.clones.count = clonesObj.value(QStringLiteral("count")).toInt(0);
        for (const QJsonValue &v : clonesObj.value(QStringLiteral("opacity")).toArray())
            entry.clones.opacities.append(v.toDouble());
        for (const QJsonValue &v : clonesObj.value(QStringLiteral("scale")).toArray())
            entry.clones.scales.append(v.toDouble());
    }

    if (const std::optional<EffectTemplateSpeedPulse> speedPulse =
            parseSpeedPulse(root.value(QStringLiteral("speedPulse")).toObject())) {
        entry.speedPulse = *speedPulse;
    }

    const QJsonArray tracks = root.value(QStringLiteral("tracks")).toArray();
    if (!tracks.isEmpty()) {
        for (const QJsonValue &trackVal : tracks) {
            if (!trackVal.isObject())
                continue;
            const std::optional<EffectTemplateTrack> track =
                parseTrack(trackVal.toObject(), packageDir);
            if (!track) {
                qWarning("EffectTemplateCatalog: skip invalid track in %s",
                         qPrintable(packageDir));
                continue;
            }
            entry.tracks.append(*track);
        }
    } else {
        const QJsonArray layers = root.value(QStringLiteral("layers")).toArray();
        for (const QJsonValue &layerVal : layers) {
            if (!layerVal.isObject())
                continue;
            EffectTemplateLayer layer;
            if (!parseLayerObject(layerVal.toObject(), &layer)) {
                qWarning("EffectTemplateCatalog: skip layer in %s (unknown effect)",
                         qPrintable(packageDir));
                continue;
            }
            entry.layers.append(layer);
        }
    }

    const bool hasLayers = !entry.layers.isEmpty();
    const bool hasTracks = !entry.tracks.isEmpty();
    if (!hasLayers && !hasTracks) {
        if (errorOut)
            *errorOut = QStringLiteral("no valid layers remain after validation");
        return std::nullopt;
    }

    entry.packageDir = packageDir;
    entry.thumbnailPath =
        GpuPackageParse::resolvePackageAsset(packageDir, QStringLiteral("thumbnail.png"));
    return entry;
}

QList<EffectTemplateEntry> scanDirectories(const QStringList &rootDirs)
{
    QList<EffectTemplateEntry> loaded;
    QSet<QString> seenIds;

    for (const QString &root : rootDirs) {
        if (root.isEmpty())
            continue;
        QDir dir(root);
        if (!dir.exists())
            continue;

        const QFileInfoList subdirs =
            dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &info : subdirs) {
            const QString packageDir = info.absoluteFilePath();
            if (!QFileInfo::exists(QDir(packageDir).filePath(QStringLiteral("template.json"))))
                continue;

            QString error;
            const std::optional<EffectTemplateEntry> entry = loadManifest(packageDir, &error);
            if (!entry) {
                qWarning("EffectTemplateCatalog: skip %s: %s", qPrintable(packageDir),
                         qPrintable(error.isEmpty() ? QStringLiteral("invalid package") : error));
                continue;
            }
            // Installed addons supersede the bundled <appDir> copy — expected, so silent.
            if (seenIds.contains(entry->id))
                continue;
            seenIds.insert(entry->id);
            loaded.append(*entry);
        }
    }

    std::sort(loaded.begin(), loaded.end(),
              [](const EffectTemplateEntry &a, const EffectTemplateEntry &b) {
                  if (a.order != b.order)
                      return a.order < b.order;
                  return a.id < b.id;
              });
    return loaded;
}

void ensureLoaded()
{
    if (!g_loaded)
        reloadEffectTemplateCatalog();
}

} // namespace

QStringList defaultEffectTemplateSearchPaths()
{
    return GpuPackageParse::defaultSearchPaths(QStringLiteral("DRIFT_TEMPLATES_DIR"),
                                               QStringLiteral("effect-templates"),
                                               QStringLiteral("effect-templates"));
}

void reloadEffectTemplateCatalog(const QStringList &packageRoots)
{
    const QStringList roots =
        packageRoots.isEmpty() ? defaultEffectTemplateSearchPaths() : packageRoots;
    g_catalog = scanDirectories(roots);
    g_loaded = true;
}

const QList<EffectTemplateEntry> &effectTemplateCatalog()
{
    ensureLoaded();
    return g_catalog;
}

const EffectTemplateEntry *effectTemplateForId(const QString &id)
{
    ensureLoaded();
    for (const EffectTemplateEntry &entry : g_catalog) {
        if (entry.id == id)
            return &entry;
    }
    return nullptr;
}

QList<QPair<QString, QString>> effectTemplateCategories()
{
    ensureLoaded();
    QList<QPair<QString, QString>> categories;
    QSet<QString> seen;
    for (const EffectTemplateEntry &entry : g_catalog) {
        if (seen.contains(entry.category))
            continue;
        seen.insert(entry.category);
        categories.append({entry.category, labelForCategory(entry.category)});
    }
    return categories;
}

QString effectTemplateCategoryLabel(const QString &categoryId)
{
    return labelForCategory(categoryId);
}
