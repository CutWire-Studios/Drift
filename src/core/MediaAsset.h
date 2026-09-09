#pragma once

#include "Time.h"

#include <QString>
#include <QStringList>

namespace drift {

enum class MediaKind { Video, Audio, Image, Other };

// Suffixes Drift treats as still images. Lives in core rather than next to the other media lists
// in AssetLibrary because the engine needs it too — FrameCompositor classifies mask media by it,
// and the project importers decide clip types by it — and engine must not include models.
//
// HEIC/HEIF and AVIF have no Qt image plugin in any official kit; they decode through the FFmpeg
// fallback in engine/StillImage.h. Everything else here Qt handles, given qtimageformats.
const QStringList &imageExtensions();
bool isImageSuffix(const QString &path);

QString mediaKindToString(MediaKind kind);
MediaKind mediaKindFromString(const QString &kind);

// Referenced media file with probed metadata.
struct MediaAsset
{
    QString id;
    QString path;
    // Android only: the fully encoded content:// URI the media was imported from, kept alongside
    // the app-storage copy `path` points at. The copy is what FFmpeg opens; this is the only way
    // back to the media once that copy is gone (uninstall, "clear storage", another device).
    // Empty everywhere else. See AppController::rehydrateMissingSources.
    QString sourceUri;
    QString name;
    MediaKind kind = MediaKind::Other;
    TimeUs durationUs = 0;

    int width = 0;
    int height = 0;
    double fps = 0.0;
    int rotationDegrees = 0;

    int sampleRate = 0;
    int channels = 0;
    QString codecName;

    // Explicit audio presence from probe. When false, fall back to channels/sampleRate
    // or a one-shot background probe — never sync MediaProbe from UI getters.
    bool hasAudio = false;
    bool hasAudioKnown = false;

    QString durationLabel;
    QString thumbnailPath;
    QString filmstripPath;

    // Id of a BinFolder in Project::binFolders(). Empty = bin root. Purely an
    // organizational attribute — clips address media through MediaAsset::id, so moving
    // an asset between folders never touches anything on the timeline.
    QString folderId;
};

} // namespace drift
