#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

#include "core/Time.h"

namespace drift {
class Project;
}

// Named scale targets used by the simple downscale chips. Resolution is derived
// from the project's aspect ratio so export never distorts.
struct ExportScalePreset
{
    QString id;
    QString label;
    // Short-side pixels ("1080p" is 1920×1080 landscape, 1080×1920 portrait). 0 = keep project size.
    int targetHeight = 0;
    int videoBitrateKbps = 12000;
};

// Upper bound for a hand-typed export frame rate; anything above is clamped.
inline constexpr int kMaxExportFps = 480;

// Full encode settings passed from the export dialog.
struct ExportSettings
{
    int targetHeight = 0; // Short-side pixels; 0 = keep project size
    // Output frame rate. 0 = follow the project fps. Rational so NTSC rates
    // (24000/1001, 30000/1001, 60000/1001) round-trip exactly.
    int fpsNum = 0;
    int fpsDen = 1;
    QString videoCodecId = QStringLiteral("h264");
    QString rateControl = QStringLiteral("crf"); // "crf" | "bitrate"
    int crf = 18;
    int videoBitrateKbps = 12000;
    // Peak cap for software H.264/H.265 CRF encodes (VBV), for platforms with an upload
    // ceiling such as Instagram's 25 Mbps. 0 = uncapped.
    int maxVideoBitrateKbps = 0;
    QString videoPreset = QStringLiteral("medium");
    QString audioCodecId = QStringLiteral("aac");
    int audioBitrateKbps = 192;
    bool audioOnly = false;
    bool gifExport = false;
    QString metadataTitle;
    QString metadataArtist;
    QString metadataAlbum;
    QString metadataComment;
    // Optional export slice on the timeline. Both zero = encode the full project.
    drift::TimeUs startUs = 0;
    drift::TimeUs endUs = 0;
};

// WYSIWYG exporter: encodes frames straight from FrameCompositor and audio from
// AudioMixer, so the exported file matches the preview exactly (single compositor).
class Exporter
{
public:
    // Called with progress in [0,1]; return false to cancel the export.
    using ProgressFn = std::function<bool(double)>;

    static const QList<ExportScalePreset> &scalePresets();
    static const ExportScalePreset *scalePresetById(const QString &id);

    // HandBrake-like catalogs; `available` is libav encoder presence, and for hardware
    // entries also a successful device probe. Hardware backends that cannot exist on
    // this OS are omitted from the list; videoCodecById still returns them.
    static QVariantList videoCodecs();
    static QVariantList audioCodecs();
    static QVariantMap videoCodecById(const QString &id);
    static QVariantMap audioCodecById(const QString &id);

    // Preferred container extension for a video+audio pair (mp4 / webm / mkv).
    static QString preferredContainer(const QString &videoCodecId, const QString &audioCodecId);
    // Standalone audio muxer (m4a / mp3 / opus / ac3 / flac).
    static QString preferredAudioOnlyContainer(const QString &audioCodecId);
    static QStringList saveFilters(const QString &container, bool audioOnly = false);
    static QString defaultSuffix(const QString &container, bool audioOnly = false);

    // Downscale chip options for the current project size (no upscale).
    static QVariantList scaleOptions(int projectWidth, int projectHeight);

    // One-click Instagram Reels target: 1080×1920 H.264 + AAC, 23–60 fps, peak bitrate under
    // Instagram's 25 Mbps ceiling. Reels are 9:16 only, so a project of any other shape gets
    // available=false and a `reason` instead of a stretched or letterboxed export.
    static QVariantMap reelsTarget(int projectWidth, int projectHeight, int projectFps);

    // Frame rate choices for the export dialog; the first entry follows `projectFps`.
    static QVariantList frameRateOptions(int projectFps);

    static bool gifAvailable();

    static ExportSettings defaultSettings();
    static ExportSettings settingsFromMap(const QVariantMap &map);

    // Holds the process in the foreground for as long as it exists: an Android foreground service
    // with a progress notification, plus FLAG_KEEP_SCREEN_ON. Without one, a backgrounded process
    // is frozen and then killed, and a multi-minute render or ML pass is gone with nothing to
    // resume from — so any job of that length wants one, not just the encode. Refcounted, so an
    // outer hold spanning the copy into the user's document is not dropped when Exporter::run's own
    // inner hold ends; the outermost hold's `title` is the one the notification shows. Constructing
    // it on desktop does nothing at all.
    class BackgroundHold
    {
    public:
        // `cancellable` puts a Cancel action on the notification. Only the outermost hold's value
        // counts, and only a job that actually polls cancelRequested() should pass true — the
        // action stops nothing on its own.
        explicit BackgroundHold(const QString &title, bool cancellable = false);
        ~BackgroundHold();
        BackgroundHold(const BackgroundHold &) = delete;
        BackgroundHold &operator=(const BackgroundHold &) = delete;

        // Progress in whole percent, clamped to 0..100. Static because the code that knows the
        // percentage is rarely the code holding the outermost hold; repeats are dropped rather
        // than re-posting the same notification once per frame.
        static void setPercent(int percent);

        // True once the notification's Cancel action has been tapped. Cleared when the outermost
        // hold is taken, so it never carries into the next job. Always false off Android.
        static bool cancelRequested();
    };

    // `outputPath` is a filesystem path, or — on Android — the fully encoded content:// URI of a
    // document the save picker created (AndroidUri::filePath of what FileDialogs returned).
    static bool run(const drift::Project &project, const ExportSettings &settings, const QString &outputPath,
                    QString *errorOut, const ProgressFn &onProgress = {});

    // Copies a finished export into the shared Movies (or Music) collection and returns the
    // MediaStore URI it now lives at, so the gallery and the share sheet can see it: a file left
    // in app storage is reachable from neither. Empty with *errorOut set on failure, and always
    // empty on desktop, where an export already lands wherever the user pointed it.
    static QUrl publishToGallery(const QUrl &source, const QString &displayName,
                                 QString *errorOut = nullptr);
};
