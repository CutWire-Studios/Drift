#pragma once

#include "ClipAnimation.h"
#include "Effect.h"
#include "FadeShape.h"
#include "Keyframe.h"
#include "Mask.h"
#include "MediaAsset.h"
#include "ShapeStyle.h"
#include "SpeedCurve.h"
#include "SubtitleCue.h"
#include "TextStyle.h"
#include "Time.h"

#include <QList>
#include <QString>

namespace drift {

enum class ClipType { Video, Audio, Image, Text, Subtitle, Shape };

QString clipTypeToString(ClipType type);
ClipType clipTypeFromString(const QString &type);

enum class BlendMode { Normal, Multiply, Screen, Overlay, Add, Darken, Lighten };

QString blendModeToString(BlendMode mode);
BlendMode blendModeFromString(const QString &mode);

enum class StabilizeMode { Bake, Keyframes };

QString stabilizeModeToString(StabilizeMode mode);
StabilizeMode stabilizeModeFromString(const QString &mode);

struct Clip
{
    QString id;
    QString assetId;
    // Shared by linked video/audio companions; empty when unlinked.
    QString linkId;
    ClipType type = ClipType::Video;
    // When true, AudioMixer skips this video clip's embedded audio (companion audio track plays it).
    bool suppressEmbeddedAudio = false;
    // 0-based index among the audio streams in `path` (for multi-track video/audio files from OBS, etc.)
    int audioStreamIndex = 0;

    TimeUs timelineStart = 0;
    TimeUs timelineDuration = 0;
    TimeUs srcIn = 0;
    TimeUs srcOut = 0;

    QString name;
    QString textContent;
    TextStyle textStyle; // meaningful when type == Text or Subtitle
    QList<SubtitleCue> subtitleCues; // only meaningful when type == Subtitle
    ShapeStyle shapeStyle; // only meaningful when type == Shape

    QString path;
    QString thumbnailPath;
    QString filmstripPath;

    // Set on image clips added from the emoji picker. `path` points at a rasterised glyph in the
    // app data cache, which another machine will not have — keeping the sequence lets the load
    // re-render it instead of leaving a broken clip.
    QString emoji;

    BlendMode blendMode = BlendMode::Normal;
    double speed = 1.0; // 1.0 = realtime; >1 faster, <1 slower
    // Variable rate across the clip. When set it supersedes `speed` outright rather than
    // multiplying with it, so there is only ever one number describing the rate at a given
    // moment. The source range stays fixed and the timeline duration is what the ramp derives.
    SpeedCurve speedCurve;
    bool reverse = false; // play source range backward; speed still applies as magnitude
    bool flipH = false;
    bool flipV = false;
    Mask mask;

    // Baked face landmarks driving the face warp effects, written once by the detect job and read
    // back per frame at composite time. Like a matte it covers the detected source range, so it is
    // indexed at (sourceUs - faceTrackSrcOffsetUs).
    QString faceTrackPath;
    TimeUs faceTrackSrcOffsetUs = 0;

    // Baked stabilized video written by the two-pass ffmpeg job, plus the settings
    // last used to produce it. `stabilizing` is transient UI state and is not saved.
    // Keyframe mode writes sparse transformX/Y keys instead of a new video.
    QString stabilizePath;
    bool stabilizing = false;
    StabilizeMode stabilizeMode = StabilizeMode::Bake;
    int stabilizeSmoothing = 15;
    bool stabilizeTripod = false;
    int stabilizeAppliedSmoothing = -1;
    bool stabilizeAppliedTripod = false;
    StabilizeMode stabilizeAppliedMode = StabilizeMode::Bake;
    // Layout snapshot taken before keyframe stabilize, so Update/Remove can
    // rebuild or restore without stacking offsets on an already-shifted pose.
    bool stabilizeHasRestPose = false;
    double stabilizeRestX = 0.0;
    double stabilizeRestY = 0.0;
    double stabilizeRestW = 0.0;
    double stabilizeRestH = 0.0;
    double stabilizeRestRot = 0.0;

    // Fades are edge-relative ramps applied multiplicatively on top of opacity
    // (visual clips) or volume (audio). They auto-follow trims and speed changes.
    TimeUs fadeInUs = 0;
    TimeUs fadeOutUs = 0;
    FadeCurve fadeCurve = FadeCurve::Smooth;
    // Used when fadeCurve == Custom; shared by fade-in and fade-out.
    FadeShape fadeShape;

    // CapCut-style body intro/outro (whole-clip opacity/transform motion).
    ClipAnimation animIn;
    ClipAnimation animOut;

    // Layout on the project canvas in pixels: top-left origin, size in px.
    // Empty tracks use defaults (0,0,projectW,projectH) at evaluate time.
    KeyframeTrack<double> opacity;
    KeyframeTrack<double> transformX;
    KeyframeTrack<double> transformY;
    KeyframeTrack<double> transformW;
    KeyframeTrack<double> transformH;
    KeyframeTrack<double> rotation;
    KeyframeTrack<double> volume;
    QList<Effect> effects;
    QList<Effect> audioEffects; // libavfilter chains applied to this clip's audio in the mixer

    TimeUs timelineEnd() const { return timelineStart + timelineDuration; }

    double effectiveSpeed() const { return speed <= 0.0 ? 1.0 : speed; }

    TimeUs sourceSpanUs() const
    {
        return static_cast<TimeUs>(llround(static_cast<double>(timelineDuration) * effectiveSpeed()));
    }

    TimeUs sourceDeltaForTimelineDelta(TimeUs timelineDelta) const
    {
        return static_cast<TimeUs>(llround(static_cast<double>(timelineDelta) * effectiveSpeed()));
    }

    bool hasSpeedCurve() const { return !speedCurve.isEmpty(); }

    TimeUs timelineToSourceUs(TimeUs timelineUs) const
    {
        const TimeUs rel = qBound(TimeUs{0}, timelineUs - timelineStart, timelineDuration);
        const TimeUs offset =
            hasSpeedCurve() ? speedCurve.sourceOffsetForTimelineOffset(rel, srcOut - srcIn)
                            : sourceDeltaForTimelineDelta(rel);
        if (!reverse)
            return srcIn + offset;

        if (srcOut <= srcIn)
            return srcIn;
        const TimeUs range = srcOut - srcIn;
        return srcOut - qMin(offset, range);
    }

    // Inverse of timelineToSourceUs relative to timelineStart: source time → clip-local time.
    TimeUs sourceUsToClipLocalUs(TimeUs sourceUs) const
    {
        const TimeUs range = srcOut - srcIn;
        if (range <= 0)
            return 0;
        TimeUs offset = reverse ? (srcOut - sourceUs) : (sourceUs - srcIn);
        offset = qBound(TimeUs{0}, offset, range);
        if (hasSpeedCurve())
            return speedCurve.timelineOffsetForSourceOffset(offset, range);
        return static_cast<TimeUs>(llround(static_cast<double>(offset) / effectiveSpeed()));
    }

    void syncSrcOutFromSpeed(TimeUs maxSourceUs)
    {
        const TimeUs span = sourceSpanUs();
        srcOut = qMin(srcIn + span, maxSourceUs);
        if (effectiveSpeed() > 0.0) {
            const TimeUs actualSpan = srcOut - srcIn;
            timelineDuration = static_cast<TimeUs>(llround(static_cast<double>(actualSpan) / effectiveSpeed()));
            timelineDuration = qMax(timelineDuration, TimeUs{1});
        }
    }

    // Curved clips invert the scalar relationship: the source range is what the user framed
    // and the ramp decides how long it takes to play.
    void syncDurationFromSpeedCurve()
    {
        if (!hasSpeedCurve())
            return;
        timelineDuration = qMax(speedCurve.retimedDurationUs(srcOut - srcIn), TimeUs{1});
    }

    bool containsTime(TimeUs time) const
    {
        return time >= timelineStart && time < timelineEnd();
    }

    // Fade gain in [0,1] at a timeline time. CapCut Fade In/Out animations own
    // the ramp when set; otherwise edge fadeInUs/Out + clip fadeCurve apply
    // (audio, timeline handles, legacy projects).
    double fadeMultiplier(TimeUs timelineUs) const
    {
        const bool animFadeIn = animIn.kind == ClipAnimKind::Fade && animIn.durationUs > 0;
        const bool animFadeOut = animOut.kind == ClipAnimKind::Fade && animOut.durationUs > 0;
        if (!animFadeIn && !animFadeOut && fadeInUs <= 0 && fadeOutUs <= 0)
            return 1.0;

        const TimeUs rel = qBound(TimeUs{0}, timelineUs - timelineStart, timelineDuration);
        const TimeUs fromEnd = timelineDuration - rel;

        double in = 1.0;
        if (animFadeIn && rel < animIn.durationUs) {
            const double t = static_cast<double>(rel) / static_cast<double>(animIn.durationUs);
            in = shapedProgress(t, animIn.curve, animIn.shape);
        } else if (fadeInUs > 0 && rel < fadeInUs) {
            in = shapedProgress(static_cast<double>(rel) / static_cast<double>(fadeInUs),
                                fadeCurve, fadeShape);
        }

        double out = 1.0;
        if (animFadeOut && fromEnd < animOut.durationUs) {
            const double t = static_cast<double>(fromEnd) / static_cast<double>(animOut.durationUs);
            out = shapedProgress(t, animOut.curve, animOut.shape);
        } else if (fadeOutUs > 0 && fromEnd < fadeOutUs) {
            out = shapedProgress(static_cast<double>(fromEnd) / static_cast<double>(fadeOutUs),
                                 fadeCurve, fadeShape);
        }

        return in * out;
    }
};

} // namespace drift
