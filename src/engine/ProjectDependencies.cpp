#include "ProjectDependencies.h"

#include "AddonRegistry.h"
#include "AudioEffectCatalog.h"
#include "AudioFileWriter.h"
#include "EffectCatalog.h"
#include "FontCatalog.h"
#include "TransitionCatalog.h"
#include "core/Project.h"

#include <QDir>
#include <QSet>
#include <QStandardPaths>

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

} // namespace

QList<MediaEntry> collectMedia(const Project &project, bool embedSource)
{
    const QString denoiseDir = denoiseCacheDir();
    const QString emojiDir = emojiCacheDir();

    QList<MediaEntry> media;
    QSet<QString> seen;

    const auto append = [&](const QString &path, MediaRole role, bool embedded) {
        if (path.isEmpty() || seen.contains(path))
            return;
        seen.insert(path);
        MediaEntry entry;
        entry.originalPath = path;
        entry.role = role;
        entry.embedded = embedded;
        media.append(entry);
    };

    for (const QString &id : project.assetOrder()) {
        const MediaAsset *asset = project.asset(id);
        if (!asset || isUnder(asset->path, emojiDir))
            continue;
        // Denoised audio is an ordinary asset, but its file lives in a cache directory that gets
        // swept — it is a post-process result, so it travels with the project either way.
        append(asset->path, MediaRole::Source, embedSource || isUnder(asset->path, denoiseDir));
    }

    for (const Track &track : project.tracks()) {
        for (const Clip &clip : track.clips) {
            if (!clip.emoji.isEmpty() || isUnder(clip.path, emojiDir))
                continue;
            // Clips carry their own copy of the asset path; a text or shape clip has none.
            append(clip.path, MediaRole::Source,
                   embedSource || isUnder(clip.path, denoiseDir));
        }
    }

    for (const Track &track : project.tracks()) {
        for (const Mask &mask : track.masks)
            append(mask.mediaPath, MediaRole::Matte, true);
        for (const Clip &clip : track.clips)
            append(clip.faceTrackPath, MediaRole::FaceTrack, true);
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

    for (const Track &track : project.tracks()) {
        for (const Clip &clip : track.clips) {
            for (const Effect &effect : clip.effects) {
                if (const EffectPresetEntry *def = effectDefForId(effect.catalogId))
                    addForPath(def->gpu.packageDir, QStringLiteral("effects"));
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

    return addons;
}

} // namespace drift::bundle
