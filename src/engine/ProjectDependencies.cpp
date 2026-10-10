#include "ProjectDependencies.h"

#include "AddonRegistry.h"
#include "AudioEffectCatalog.h"
#include "AudioFileWriter.h"
#include "EffectCatalog.h"
#include "FontCatalog.h"
#include "MediaEditor.h"
#include "TransitionCatalog.h"
#include "core/Project.h"

#include <algorithm>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QXmlStreamReader>

namespace drift::bundle {
namespace {

// Emoji clips point at a raster under <AppData>/emoji that EmojiCatalog re-renders from the glyph
// sequence on load, so bundling it would ship a file the loader immediately replaces.
QString emojiCacheDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return base.isEmpty() ? QString() : QDir(base).filePath(QStringLiteral("emoji"));
}

bool isUnder(const QString &path, const QString &dir)
{
    if (path.isEmpty() || dir.isEmpty())
        return false;
    const QString root = QDir::cleanPath(dir);
    return QDir::cleanPath(path).startsWith(root + QLatin1Char('/'));
}

// A reference the renderer resolves against the document's directory (SkiaVectorResources), as an
// absolute path, or empty when it is inline, remote, or would climb out of that directory.
QString documentRelativeFile(const QString &documentDir, const QString &dir, const QString &name)
{
    if (name.isEmpty() || name.startsWith(QLatin1String("data:")) || name.contains(QLatin1String("://"))
        || dir.contains(QLatin1String("://")))
        return {};
    QString rel = QDir::cleanPath(dir + QLatin1Char('/') + name);
    while (rel.startsWith(QLatin1Char('/')))
        rel.remove(0, 1);
    if (rel.isEmpty() || rel.startsWith(QLatin1String("..")) || QFileInfo(rel).isAbsolute())
        return {};
    const QString path = QDir(documentDir).filePath(rel);
    return QFileInfo(path).isFile() ? path : QString();
}

// The files a Lottie or SVG document loads from beside itself: Lottie image assets ("u" + "p")
// and font files ("fPath"), SVG <image> hrefs. A .lottie import unpacks its images next to the
// JSON exactly this way.
QStringList documentResources(const QString &documentPath)
{
    const QString suffix = QFileInfo(documentPath).suffix().toLower();
    if (suffix != QLatin1String("json") && suffix != QLatin1String("svg"))
        return {};
    QFile file(documentPath);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QString dir = QFileInfo(documentPath).absolutePath();
    QStringList out;

    if (suffix == QLatin1String("json")) {
        const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        for (const QJsonValue &value : root.value(QStringLiteral("assets")).toArray()) {
            const QJsonObject asset = value.toObject();
            if (asset.value(QStringLiteral("e")).toInt() == 1)
                continue;
            out.append(documentRelativeFile(dir, asset.value(QStringLiteral("u")).toString(),
                                            asset.value(QStringLiteral("p")).toString()));
        }
        const QJsonArray fonts =
            root.value(QStringLiteral("fonts")).toObject().value(QStringLiteral("list")).toArray();
        for (const QJsonValue &value : fonts)
            out.append(documentRelativeFile(dir, QString(),
                                            value.toObject().value(QStringLiteral("fPath")).toString()));
    } else {
        QXmlStreamReader xml(&file);
        while (!xml.atEnd()) {
            if (xml.readNext() != QXmlStreamReader::StartElement || xml.name() != QLatin1String("image"))
                continue;
            for (const QXmlStreamAttribute &attribute : xml.attributes()) {
                if (attribute.name() == QLatin1String("href"))
                    out.append(documentRelativeFile(dir, QString(), attribute.value().toString()));
            }
        }
    }
    out.removeAll(QString());
    return out;
}

void addAddon(const addon::InstalledAddon *installed, const QString &kind,
              QList<AddonRef> *out, QHash<QString, int> *seen)
{
    if (!installed)
        return;

    const auto existing = seen->constFind(installed->id);
    if (existing != seen->constEnd()) {
        AddonRef &ref = (*out)[existing.value()];
        if (!kind.isEmpty() && !ref.kinds.contains(kind))
            ref.kinds.append(kind);
        return;
    }

    AddonRef ref;
    ref.id = installed->id;
    ref.version = installed->version;
    ref.name = installed->name;
    if (kind.isEmpty()) {
        // Path-derived hits know the addon but not which of its kinds they came from.
        for (const addon::InstalledProvide &provide : installed->provides) {
            if (!ref.kinds.contains(provide.kind))
                ref.kinds.append(provide.kind);
        }
    } else {
        ref.kinds.append(kind);
    }
    seen->insert(ref.id, out->size());
    out->append(ref);
}

QByteArray fileSha256(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&file);
    return hash.result();
}

bool sameBytes(const QString &a, const QString &b)
{
    return QFileInfo(a).size() == QFileInfo(b).size() && fileSha256(a) == fileSha256(b);
}

// "clip.mp4", "clip (2).mp4", "clip (3).mp4", ...
QString numberedName(const QString &name, int n)
{
    if (n < 2)
        return name;
    const QFileInfo info(name);
    const QString stem = QStringLiteral("%1 (%2)").arg(info.completeBaseName()).arg(n);
    return info.suffix().isEmpty() ? stem : stem + QLatin1Char('.') + info.suffix();
}

} // namespace

QList<MediaEntry> collectMedia(const Project &project, bool embedSource)
{
    const QString denoiseDir = denoiseCacheDir();
    // Stabilized renders made before they were written as project media; on load they become
    // bin assets that still live in this cache directory.
    const QString stabilizationDir = stabilizationCacheDir();
    const QString emojiDir = emojiCacheDir();

    QList<MediaEntry> media;
    QSet<QString> seen;

    const auto appendEntry = [&](const QString &path, const QString &resourceOf, MediaRole role,
                                 bool embedded) {
        const QString key = resourceOf + QLatin1Char('\n') + path;
        if (path.isEmpty() || seen.contains(key))
            return false;
        seen.insert(key);
        MediaEntry entry;
        entry.originalPath = path;
        entry.resourceOf = resourceOf;
        entry.role = role;
        entry.embedded = embedded;
        media.append(entry);
        return true;
    };
    const auto append = [&](const QString &path, MediaRole role, bool embedded) {
        if (!appendEntry(path, QString(), role, embedded) || role != MediaRole::Source)
            return;
        for (const QString &resource : documentResources(path))
            appendEntry(resource, path, MediaRole::Source, embedded);
    };
    const auto appendTextures = [&](const QList<TextShadingLayer> &layers) {
        for (const TextShadingLayer &layer : layers)
            append(layer.paint.texture.path, MediaRole::Source, embedSource);
    };

    for (const QString &id : project.assetOrder()) {
        const MediaAsset *asset = project.asset(id);
        if (!asset || isUnder(asset->path, emojiDir))
            continue;
        // Denoised audio is an ordinary asset, but its file lives in a cache directory that gets
        // swept — it is a post-process result, so it travels with the project either way. Same
        // for a stabilized render from before those were project media.
        append(asset->path, MediaRole::Source,
               embedSource || isUnder(asset->path, denoiseDir)
                   || isUnder(asset->path, stabilizationDir));
    }

    QList<Track> allTracks;
    project.forEachTrackList([&](const QList<Track> &tracks) { allTracks.append(tracks); });

    for (const Track &track : allTracks) {
        for (const Clip &clip : track.clips) {
            if (!clip.emoji.isEmpty() || isUnder(clip.path, emojiDir))
                continue;
            // Clips carry their own copy of the asset path; a text or shape clip has none.
            append(clip.path, MediaRole::Source,
                   embedSource || isUnder(clip.path, denoiseDir)
                       || isUnder(clip.path, stabilizationDir));
        }
    }

    for (const Track &track : allTracks) {
        for (const Clip &clip : track.clips) {
            // Masks live on adjustment clips, which this flat walk already covers.
            append(clip.mask.mediaPath, MediaRole::Matte, true);
            append(clip.mask.mediaFgrPath, MediaRole::Matte, true);
            append(clip.faceTrackPath, MediaRole::FaceTrack, true);
            append(clip.depthPath, MediaRole::Depth, true);
            for (const VectorSlotValue &slot : clip.vector.slotValues) {
                if (slot.type == VectorSlotValue::Type::Image)
                    append(slot.image, MediaRole::Source, embedSource);
            }
            appendTextures(clip.textStyle.layers);
            appendTextures(clip.shapeStyle.layers);
            for (const Effect &effect : clip.effects) {
                const EffectPresetEntry *def = effectDefForId(effect.catalogId);
                if (!def)
                    continue;
                for (const drift::EffectParamSpec &spec : def->meta.parameters) {
                    if (!spec.isFilePath())
                        continue;
                    // Read from effect.parameters directly — keyframes never apply to file params.
                    const QString modelPath = effect.parameters.value(spec.key).toString();
                    if (modelPath.isEmpty())
                        continue;
                    // Always embed: a model is small, and relying on the addon ref means opening
                    // on a machine without the pack silently renders a bare head.
                    append(modelPath, MediaRole::Model3d, true);
                }
            }
        }
    }

    return media;
}

QList<AddonRef> collectAddons(const Project &project)
{
    QList<AddonRef> addons;
    QHash<QString, int> seen;

    const auto addForPath = [&](const QString &path, const QString &kind) {
        addAddon(addon::addonForPath(path), kind, &addons, &seen);
    };

    bool usesEmoji = false;
    bool usesPdf = false;

    QList<Track> allTracks;
    project.forEachTrackList([&](const QList<Track> &tracks) { allTracks.append(tracks); });
    for (const Track &track : allTracks) {
        for (const Clip &clip : track.clips) {
            for (const Effect &effect : clip.effects) {
                if (const EffectPresetEntry *def = effectDefForId(effect.catalogId)) {
                    addForPath(def->gpu.packageDir, QStringLiteral("effects"));
                    for (const drift::EffectParamSpec &spec : def->meta.parameters) {
                        if (!spec.isFilePath())
                            continue;
                        const QString modelPath = effect.parameters.value(spec.key).toString();
                        if (!modelPath.isEmpty())
                            addForPath(modelPath, QStringLiteral("face-props"));
                    }
                }
            }
            for (const Effect &effect : clip.audioEffects) {
                if (const AudioEffectEntry *def = audioEffectDefForId(effect.catalogId))
                    addForPath(def->packageDir, QStringLiteral("audio-effects"));
            }
            if (clip.type == ClipType::Text || clip.type == ClipType::Subtitle) {
                if (const FontFamilyEntry *font = fontFamilyForName(clip.textStyle.fontFamily))
                    addForPath(font->packageDir, QStringLiteral("fonts"));
            }
            // Stickers are image clips whose file lives inside the pack that provided them; user
            // media resolves to no addon at all, so this is a cheap miss for ordinary clips.
            addForPath(clip.path, QString());
            usesEmoji = usesEmoji || !clip.emoji.isEmpty();
            usesPdf = usesPdf || clip.type == ClipType::Pdf;
        }
        for (const Transition &transition : track.transitions) {
            if (const TransitionPresetEntry *def = transitionDefForId(transition.kindId))
                addForPath(def->gpu.packageDir, QStringLiteral("transitions"));
        }
    }

    // The raster is regenerated from the glyph sequence on load, which needs the font back.
    if (usesEmoji) {
        for (const QString &root : addon::addonRootsForKind(QStringLiteral("emoji-font")))
            addForPath(root, QStringLiteral("emoji-font"));
    }

    // The library is not a path any clip points into, so find it by the kind it provides.
    if (usesPdf) {
        for (const addon::InstalledAddon &installed : addon::installedAddons()) {
            const bool provides = std::any_of(
                installed.provides.cbegin(), installed.provides.cend(),
                [](const addon::InstalledProvide &p) { return p.kind == QLatin1String("pdfium"); });
            if (provides) {
                addAddon(&installed, QStringLiteral("pdfium"), &addons, &seen);
                break;
            }
        }
    }

    return addons;
}

bool collectToFolder(const QList<MediaEntry> &media, const QHash<QString, QString> &subfolders,
                     const QString &destDir, bool move, const ProgressFn &progress,
                     QHash<QString, QString> *pathRemap, int *undeletedOriginals, QString *error)
{
    const QString root = QDir::cleanPath(destDir);
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    const auto collectable = [&root](const MediaEntry &entry) {
        return entry.role != MediaRole::Model3d && QFileInfo(entry.originalPath).isFile()
               && !isUnder(entry.originalPath, root) && !addon::addonForPath(entry.originalPath);
    };

    QHash<QString, QStringList> resourcesOf;
    QList<MediaEntry> files;
    for (const MediaEntry &entry : media) {
        if (!entry.resourceOf.isEmpty())
            resourcesOf[entry.resourceOf].append(entry.originalPath);
        else if (collectable(entry))
            files.append(entry);
    }

    qint64 total = 0;
    for (const MediaEntry &entry : files) {
        total += QFileInfo(entry.originalPath).size();
        for (const QString &resource : resourcesOf.value(entry.originalPath))
            total += QFileInfo(resource).size();
    }

    // Where each file this run has placed now lives. A move renames the original away, so a file
    // needed a second time (an image both in the bin and inside a Lottie) is read from here.
    QHash<QString, QString> placed;
    QList<QPair<QString, QString>> renamed;
    QStringList created;
    QSet<QString> originalsToDelete;
    QHash<QString, QString> remap;
    qint64 done = 0;

    const auto current = [&placed](const QString &source) { return placed.value(source, source); };

    const auto rollback = [&]() {
        for (auto it = renamed.crbegin(); it != renamed.crend(); ++it)
            QFile::rename(it->second, it->first);
        for (const QString &path : created)
            QFile::remove(path);
    };

    const auto copyFile = [&](const QString &from, const QString &to) {
        QFile in(from);
        if (!in.open(QIODevice::ReadOnly))
            return fail(QCoreApplication::translate("ProjectBundle", "Couldn’t read %1")
                            .arg(QDir::toNativeSeparators(from)));
        QSaveFile out(to);
        if (!out.open(QIODevice::WriteOnly))
            return fail(QCoreApplication::translate("ProjectBundle", "Couldn’t write %1")
                            .arg(QDir::toNativeSeparators(to)));
        QByteArray buffer(1 << 20, Qt::Uninitialized);
        while (!in.atEnd()) {
            const qint64 read = in.read(buffer.data(), buffer.size());
            if (read < 0 || out.write(buffer.constData(), read) != read)
                return fail(QCoreApplication::translate("ProjectBundle", "Couldn’t write %1")
                                .arg(QDir::toNativeSeparators(to)));
            done += read;
            if (progress && !progress(done, total))
                return fail(QCoreApplication::translate("ProjectBundle", "Cancelled"));
        }
        if (!out.commit())
            return fail(QCoreApplication::translate("ProjectBundle", "Couldn’t write %1")
                            .arg(QDir::toNativeSeparators(to)));
        created.append(to);
        return true;
    };

    const auto transfer = [&](const QString &source, const QString &target) {
        const qint64 size = QFileInfo(current(source)).size();
        if (QFileInfo::exists(target)) {
            // Chosen only when the bytes match: the file is already here.
            done += size;
        } else {
            if (!QDir().mkpath(QFileInfo(target).absolutePath()))
                return fail(QCoreApplication::translate("ProjectBundle", "Couldn’t create %1")
                                .arg(QDir::toNativeSeparators(QFileInfo(target).absolutePath())));
            if (move && !placed.contains(source) && QFile::rename(source, target)) {
                renamed.append({source, target});
                done += size;
            } else if (!copyFile(current(source), target)) {
                return false;
            }
        }
        if (move && !placed.contains(source))
            originalsToDelete.insert(source);
        placed.insert(source, placed.value(source, target));
        return !progress || progress(done, total)
               || fail(QCoreApplication::translate("ProjectBundle", "Cancelled"));
    };

    for (const MediaEntry &entry : files) {
        const QString source = entry.originalPath;
        const QString folder = QDir(root).filePath(subfolders.value(source, QStringLiteral("Other")));
        const QString name = QFileInfo(source).fileName();
        const QStringList resources = resourcesOf.value(source);

        // Pairs of (source, target) that must land together; target names are picked so that
        // every one is either free or already holds the same bytes.
        QList<QPair<QString, QString>> pairs;
        for (int n = 1;; ++n) {
            pairs.clear();
            if (resources.isEmpty()) {
                pairs.append({source, QDir(folder).filePath(numberedName(name, n))});
            } else {
                const QDir own(QDir(folder).filePath(
                    numberedName(QFileInfo(source).completeBaseName(), n)));
                const QDir documentDir = QFileInfo(source).absoluteDir();
                pairs.append({source, own.filePath(name)});
                for (const QString &resource : resources)
                    pairs.append({resource, own.filePath(documentDir.relativeFilePath(resource))});
            }
            const bool fits = std::all_of(pairs.cbegin(), pairs.cend(), [&](const auto &pair) {
                return !QFileInfo::exists(pair.second) || sameBytes(current(pair.first), pair.second);
            });
            if (fits)
                break;
        }

        for (const auto &pair : pairs) {
            if (!transfer(pair.first, pair.second)) {
                rollback();
                return false;
            }
        }
        remap.insert(source, pairs.first().second);
    }

    int undeleted = 0;
    for (const QString &original : originalsToDelete) {
        if (QFileInfo::exists(original) && !QFile::remove(original))
            ++undeleted;
    }
    if (undeletedOriginals)
        *undeletedOriginals = undeleted;
    if (pathRemap)
        *pathRemap = remap;
    return true;
}

} // namespace drift::bundle
