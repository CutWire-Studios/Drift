#include <QtTest>

#include <QColor>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>

#include "core/ClipAnimation.h"
#include "core/Keyframe.h"
#include "core/Project.h"
#include "core/ShapePath.h"
#include "core/SrtIO.h"
#include "core/SubtitleCue.h"
#include "core/TimelineOps.h"
#include "core/Transition.h"

#include <cmath>

class CoreTest : public QObject
{
    Q_OBJECT

private slots:
    void timeConversion();
    void keyframeHoldInterpolation();
    void keyframeLinearInterpolation();
    void keyframeEaseInterpolation();
    void keyframeBezierTangents();
    void disabledKeyframesFreezeAtFirstKey();
    void legacyTrackInterpolationMigratesLosslessly();
    void keyframeNearestQuery();
    void projectSerializationRoundTrip();
    void projectMetadataRoundTrip();
    void effectColorParamSurvivesRoundTrip();
    void clipTransformSerialization();
    void legacyFractionalTransformMigration();
    void volumeKeyframeSerialization();
    void projectLoadsLegacyV1Format();
    void projectRejectsUnreadableDocuments();
    void trackAllowsClipTypes();
    void subtitleCueSerialization();
    void subtitleCueLookup();
    void subtitleCuePacking();
    void srtRoundTrip();
    void srtParseEdgeCases();
    void insertTrackAtTopAllowsDuplicateTypes();
    void multiTrackSerializationRoundTrip();
    void textStyleAndBlendModeSerialization();
    void legacyBoldMigratesToFontWeight();
    void textPresetsAreWellFormed();
    void karaokeWordIndexTracksTheCue();
    void shapeStyleSerialization();
    void legacyShapeStyleLoadsWithDefaults();
    void shapeCatalogPathsFitBounds();
    void effectCatalogIdSerialization();
    void effectParamKeyframeSerialization();
    void detachedCopyIsolatesKeyframesFromLiveMutations();
    void effectTemplateStackSerialization();
    void audioEffectSerialization();
    void rgbSplitEffectParametersSerialization();
    void blockGlitchEffectParametersSerialization();
    void clipSpeedSourceMapping();
    void speedCurveMatchesConstantSpeed();
    void speedCurveRampRetimesDuration();
    void speedCurveMappingIsMonotonic();
    void speedCurveSubRangePreservesShape();
    void speedCurveSerialization();
    void clipReverseAndFlipSerialization();
    void clipSplitMergeRoundTrip();
    void clipLinkFieldsSerialization();
    void maskAndTransitionSerialization();
    void matteMaskSerialization();
    void legacySingleMaskLoadsAsStack();
    void legacyEmptyMaskLoadsAsNoStack();
    void faceTrackSerialization();
    void emojiClipSerialization();
    void allTransitionKindsRoundTrip();
    void transitionParametersRoundTrip();
    void legacyTransitionJsonStillLoads();
    void transitionAudioCurves();
    void physicalOverlapTransitionWindow();
    void clampClipStartNoOverlapPushesPastBlockers();
    void clampTrimEdgesIgnoreExistingOverlaps();
    void backgroundSerialization();
    void fadeSerializationAndMultiplier();
    void clipAnimationSerializationAndSample();
    void rebaseClipLayoutFreezesImplicitSize();
    void rebaseClipLayoutShiftsKeyframedPosition();
};

void CoreTest::timeConversion()
{
    QCOMPARE(drift::secondsToUs(1.0), drift::TimeUs{1'000'000});
    QCOMPARE(drift::usToSeconds(2'500'000), 2.5);
    QCOMPARE(drift::frameDurationUs(30), drift::TimeUs{33'333});
}

void CoreTest::keyframeHoldInterpolation()
{
    drift::KeyframeTrack<double> track;
    track.setKeyframe(0, 0.0);
    track.setKeyframe(drift::secondsToUs(2.0), 1.0);
    // Hold is a property of the key you are leaving, not of the whole track.
    track.setEasing(0, drift::Interpolation::Hold);
    QCOMPARE(track.evaluateAt(drift::secondsToUs(1.5)), 0.0);
    QCOMPARE(track.evaluateAt(drift::secondsToUs(2.0)), 1.0);
}

void CoreTest::keyframeLinearInterpolation()
{
    drift::KeyframeTrack<double> track;
    track.setKeyframe(0, 0.0);
    track.setKeyframe(drift::secondsToUs(2.0), 1.0);
    QCOMPARE(track.evaluateAt(drift::secondsToUs(1.0)), 0.5);
}

void CoreTest::disabledKeyframesFreezeAtFirstKey()
{
    drift::KeyframeTrack<double> track;
    track.setKeyframe(0, 0.0);
    track.setKeyframe(drift::secondsToUs(2.0), 1.0);
    QVERIFY(track.enabled());

    track.setEnabled(false);
    // Every sample reads as the first key while the animation is parked...
    QCOMPARE(track.evaluateAt(drift::secondsToUs(1.0)), 0.0);
    QCOMPARE(track.evaluateAt(drift::secondsToUs(2.0)), 0.0);
    // ...and the keys themselves are untouched, so switching back on restores the curve exactly.
    QCOMPARE(track.keyframes().size(), 2);
    track.setEnabled(true);
    QCOMPARE(track.evaluateAt(drift::secondsToUs(1.0)), 0.5);

    // The switch survives a save/load round trip.
    drift::Project project;
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Video});
    drift::Clip clip;
    clip.id = QStringLiteral("clip-kf");
    clip.type = drift::ClipType::Text;
    clip.timelineDuration = drift::secondsToUs(3.0);
    clip.opacity = track;
    clip.opacity.setEnabled(false);
    project.tracks()[0].clips.append(clip);

    QString error;
    const drift::Project loaded = drift::Project::fromJson(project.toJson(), &error);
    QVERIFY(error.isEmpty());
    const drift::KeyframeTrack<double> &loadedTrack = loaded.tracks().at(0).clips.at(0).opacity;
    QVERIFY(!loadedTrack.enabled());
    QCOMPARE(loadedTrack.keyframes().size(), 2);
    // A project written before the switch existed loads with its animations on.
    QVERIFY(loaded.tracks().at(0).clips.at(0).transformX.enabled());
}

void CoreTest::keyframeBezierTangents()
{
    drift::KeyframeTrack<double> track;
    track.setKeyframe(0, 0.0);
    track.setKeyframe(drift::secondsToUs(1.0), 10.0);

    // Sharp attack, long settle: the out-handle of the first key held flat and far to the
    // right pushes the curve above the straight line for most of the segment.
    drift::Keyframe<double> *first = track.keyframeRef(0);
    QVERIFY(first != nullptr);
    first->outDx = drift::secondsToUs(0.8);
    first->outDy = 9.0;
    QVERIFY(track.evaluateAt(drift::secondsToUs(0.25)) > 2.5);

    // The endpoints stay pinned no matter what the handles do.
    QCOMPARE(track.evaluateAt(0), 0.0);
    QCOMPARE(track.evaluateAt(drift::secondsToUs(1.0)), 10.0);

    // A handle reaching past the segment must not fold the curve back on itself: time still
    // maps to exactly one value, so the result stays monotonic in a monotonic segment.
    first->outDx = drift::secondsToUs(5.0);
    first->outDy = 0.0;
    double prevValue = -1.0;
    for (int i = 0; i <= 20; ++i) {
        const double v = track.evaluateAt(drift::secondsToUs(i / 20.0));
        QVERIFY2(v >= prevValue - 1e-9, qPrintable(QStringLiteral("folded at %1").arg(i)));
        prevValue = v;
    }

    // Custom tangents match no preset, which is what leaves the chips unlit.
    QVERIFY(track.hasCustomTangents(0));
    track.setEasing(0, drift::Interpolation::Linear);
    QVERIFY(!track.hasCustomTangents(0));
}

void CoreTest::legacyTrackInterpolationMigratesLosslessly()
{
    // A project written before keyframes had tangents: one mode for the whole track.
    const auto legacyJson = [](const QString &mode) {
        return QJsonObject{
            {QStringLiteral("interpolation"), mode},
            {QStringLiteral("keyframes"),
             QJsonArray{
                 QJsonObject{{QStringLiteral("timeUs"), 0.0}, {QStringLiteral("value"), 0.0}},
                 QJsonObject{{QStringLiteral("timeUs"), 1'000'000.0},
                             {QStringLiteral("value"), 10.0}},
             }},
        };
    };

    // Build a real project, serialize it, then rewrite the keyframe block into the legacy
    // shape — so the loader is exercised exactly as it would be on an old file.
    drift::Project project;
    project.tracks().append(drift::Track{});
    drift::Clip clip;
    clip.id = QStringLiteral("c1");
    clip.timelineDuration = drift::secondsToUs(2.0);
    drift::Effect effect;
    effect.catalogId = QStringLiteral("adjust.contrast");
    drift::KeyframeTrack<double> seed;
    seed.setKeyframe(0, 0.0);
    seed.setKeyframe(drift::secondsToUs(1.0), 10.0);
    effect.paramKeyframes.insert(QStringLiteral("contrast"), seed);
    clip.effects.append(effect);
    project.tracks()[0].clips.append(clip);
    const QJsonObject baseJson = project.toJson();

    struct Case { const char *mode; double at0_25; };
    const Case cases[] = {
        {"linear", 2.5},     // straight line
        {"ease", 1.5625},    // smoothstep(0.25) * 10
        {"hold", 0.0},       // steps at the next key
    };

    for (const Case &c : cases) {
        QJsonObject projectJson = baseJson;
        QJsonArray tracks = projectJson.value(QStringLiteral("tracks")).toArray();
        QJsonObject trackJson = tracks[0].toObject();
        QJsonArray clips = trackJson.value(QStringLiteral("clips")).toArray();
        QJsonObject clipJson = clips[0].toObject();
        QJsonArray effects = clipJson.value(QStringLiteral("effects")).toArray();
        QJsonObject effectJson = effects[0].toObject();
        QJsonObject params;
        params.insert(QStringLiteral("contrast"), legacyJson(QString::fromLatin1(c.mode)));
        effectJson.insert(QStringLiteral("paramKeyframes"), params);
        effects[0] = effectJson;
        clipJson.insert(QStringLiteral("effects"), effects);
        clips[0] = clipJson;
        trackJson.insert(QStringLiteral("clips"), clips);
        tracks[0] = trackJson;
        projectJson.insert(QStringLiteral("tracks"), tracks);

        QString error;
        const drift::Project loaded = drift::Project::fromJson(projectJson, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));

        // The loader may materialise default tracks, so find the clip rather than index into it.
        const drift::Clip *found = nullptr;
        for (const drift::Track &t : loaded.tracks()) {
            for (const drift::Clip &cl : t.clips) {
                if (!cl.effects.isEmpty())
                    found = &cl;
            }
        }
        QVERIFY(found != nullptr);

        const drift::KeyframeTrack<double> &kt =
            found->effects[0].paramKeyframes.value(QStringLiteral("contrast"));
        QCOMPARE(kt.keyframes().size(), 2);
        const double got = kt.evaluateAt(drift::secondsToUs(0.25));
        QVERIFY2(std::abs(got - c.at0_25) < 0.01,
                 qPrintable(QStringLiteral("%1: got %2, expected %3")
                                .arg(QString::fromLatin1(c.mode)).arg(got).arg(c.at0_25)));
    }
}

void CoreTest::keyframeEaseInterpolation()
{
    drift::KeyframeTrack<double> track;
    track.setKeyframe(0, 0.0);
    track.setKeyframe(drift::secondsToUs(1.0), 10.0);
    track.setEasing(0, drift::Interpolation::Ease);
    track.setEasing(drift::secondsToUs(1.0), drift::Interpolation::Ease);

    // The Ease preset is flat tangents a third of the way to each neighbour, which is exactly
    // the smoothstep the old track-wide mode produced: t*t*(3-2t) at t=0.25 is 0.15625.
    const double eased = track.evaluateAt(drift::secondsToUs(0.25));
    QVERIFY2(std::abs(eased - 1.5625) < 0.01,
             qPrintable(QStringLiteral("eased %1, expected 1.5625").arg(eased)));
    QCOMPARE(drift::interpolationToString(drift::Interpolation::Ease), QStringLiteral("ease"));
    QCOMPARE(drift::interpolationFromString(QStringLiteral("ease")), drift::Interpolation::Ease);

    // Zero-length handles are a straight line, because x and y then share blend weights.
    drift::KeyframeTrack<double> linear;
    linear.setKeyframe(0, 0.0);
    linear.setKeyframe(drift::secondsToUs(1.0), 10.0);
    QCOMPARE(linear.evaluateAt(drift::secondsToUs(0.25)), 2.5);
}

void CoreTest::keyframeNearestQuery()
{
    drift::KeyframeTrack<double> track;
    track.setKeyframe(drift::secondsToUs(1.0), 5.0);
    QCOMPARE(track.nearestKeyframe(drift::secondsToUs(1.01), drift::secondsToUs(0.05)),
             drift::secondsToUs(1.0));
    QCOMPARE(track.nearestKeyframe(drift::secondsToUs(2.0), drift::secondsToUs(0.05)), drift::TimeUs{-1});
}

void CoreTest::projectMetadataRoundTrip()
{
    drift::Project project;
    const QString id = project.id();
    QVERIFY(!id.isEmpty());

    project.setName(QStringLiteral("Documentary"));
    project.setAuthor(QStringLiteral("Ada"));
    project.setDescription(QStringLiteral("Rough cut"));
    const QDateTime created(QDate(2026, 3, 4), QTime(5, 6, 7), QTimeZone::UTC);
    project.setCreatedAt(created);
    project.setModifiedAt(created.addDays(2));

    QString error;
    const drift::Project loaded = drift::Project::fromJson(project.toJson(), &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(loaded.id(), id);
    QCOMPARE(loaded.name(), QStringLiteral("Documentary"));
    QCOMPARE(loaded.author(), QStringLiteral("Ada"));
    QCOMPARE(loaded.description(), QStringLiteral("Rough cut"));
    QCOMPARE(loaded.createdAt(), created);
    QCOMPARE(loaded.modifiedAt(), created.addDays(2));

    // An empty timeline routes through resetToDefaultTimeline() during the load, which mints a
    // fresh id — the saved one has to survive that.
    QVERIFY(loaded.tracks().size() > 0);
    QCOMPARE(drift::Project::fromJson(loaded.toJson(), &error).id(), id);

    // Two fresh projects are distinct documents, not the same one.
    QVERIFY(drift::Project().id() != drift::Project().id());
}

void CoreTest::projectSerializationRoundTrip()
{
    drift::Project project;
    project.setName(QStringLiteral("Test Project"));
    project.setFps(24);
    project.setResolution(1280, 720);

    drift::MediaAsset asset;
    asset.name = QStringLiteral("clip.mp4");
    asset.kind = drift::MediaKind::Video;
    asset.path = QStringLiteral("/tmp/clip.mp4");
    asset.durationUs = drift::secondsToUs(10.0);
    const QString assetId = project.addAsset(asset);

    drift::Clip clip;
    clip.id = QStringLiteral("clip-1");
    clip.assetId = assetId;
    clip.type = drift::ClipType::Video;
    clip.name = asset.name;
    clip.path = asset.path;
    clip.timelineStart = drift::secondsToUs(1.0);
    clip.timelineDuration = drift::secondsToUs(5.0);
    clip.srcIn = 0;
    clip.srcOut = drift::secondsToUs(5.0);
    project.tracks()[0].clips.append(clip);

    project.bookmarks().append({.timeUs = drift::secondsToUs(3.0), .label = QStringLiteral("Mark")});
    project.setWorkAreaInUs(drift::secondsToUs(1.0));
    project.setWorkAreaOutUs(drift::secondsToUs(4.0));

    const QJsonObject json = project.toJson();
    QString error;
    const drift::Project loaded = drift::Project::fromJson(json, &error);

    QVERIFY(error.isEmpty());
    QCOMPARE(loaded.name(), project.name());
    QCOMPARE(loaded.fps(), 24);
    QCOMPARE(loaded.width(), 1280);
    QCOMPARE(loaded.tracks().size(), 1);
    QCOMPARE(loaded.tracks()[0].clips.size(), 1);
    QCOMPARE(loaded.tracks()[0].clips[0].timelineStart, clip.timelineStart);
    QCOMPARE(loaded.bookmarks().size(), 1);
    QCOMPARE(loaded.bookmarks()[0].label, QStringLiteral("Mark"));
    QVERIFY(loaded.hasWorkArea());
    QCOMPARE(loaded.workAreaInUs(), drift::secondsToUs(1.0));
    QCOMPARE(loaded.workAreaOutUs(), drift::secondsToUs(4.0));
}

// A colour parameter is stored as a "#rrggbb" string rather than a number, so it has to survive the
// project file as one. Effect params round-trip through QVariant, and a silent coercion to double
// here would reach the shader as black.
void CoreTest::effectColorParamSurvivesRoundTrip()
{
    drift::Project project;

    drift::Clip clip;
    clip.id = QStringLiteral("clip-1");
    clip.type = drift::ClipType::Video;
    clip.timelineDuration = drift::secondsToUs(5.0);

    drift::Effect effect;
    effect.catalogId = QStringLiteral("face_lipstick");
    effect.parameters.insert(QStringLiteral("shade"), QStringLiteral("#b03048"));
    effect.parameters.insert(QStringLiteral("opacity"), 0.8);
    effect.parameters.insert(QStringLiteral("coverInner"), true);
    clip.effects.append(effect);
    project.tracks()[0].clips.append(clip);

    QString error;
    const drift::Project loaded = drift::Project::fromJson(project.toJson(), &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(loaded.tracks()[0].clips.size(), 1);
    const drift::Effect &out = loaded.tracks()[0].clips[0].effects.at(0);

    const QVariant shade = out.parameters.value(QStringLiteral("shade"));
    QCOMPARE(shade.typeId(), QMetaType::QString);
    QCOMPARE(shade.toString(), QStringLiteral("#b03048"));
    QCOMPARE(out.parameters.value(QStringLiteral("opacity")).toDouble(), 0.8);
    QCOMPARE(out.parameters.value(QStringLiteral("coverInner")).toBool(), true);
}

void CoreTest::clipTransformSerialization()
{
    drift::Project project;
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Text});

    drift::Clip clip;
    clip.id = QStringLiteral("clip-transform");
    clip.type = drift::ClipType::Text;
    clip.name = QStringLiteral("Title");
    clip.textContent = QStringLiteral("Hello");
    clip.timelineStart = 0;
    clip.timelineDuration = drift::secondsToUs(3.0);
    clip.transformX.setKeyframe(0, 100.0);
    clip.transformY.setKeyframe(0, 200.0);
    clip.transformW.setKeyframe(0, 640.0);
    clip.transformH.setKeyframe(0, 360.0);
    clip.rotation.setKeyframe(0, 45.0);
    project.tracks()[0].clips.append(clip);

    const QJsonObject json = project.toJson();
    QString error;
    const drift::Project loaded = drift::Project::fromJson(json, &error);

    QVERIFY(error.isEmpty());
    const drift::Clip &loadedClip = loaded.tracks()[0].clips[0];
    QCOMPARE(loadedClip.transformX.evaluateAt(0), 100.0);
    QCOMPARE(loadedClip.transformY.evaluateAt(0), 200.0);
    QCOMPARE(loadedClip.transformW.evaluateAt(0), 640.0);
    QCOMPARE(loadedClip.transformH.evaluateAt(0), 360.0);
    QCOMPARE(loadedClip.rotation.evaluateAt(0), 45.0);
}

void CoreTest::legacyFractionalTransformMigration()
{
    // Old projects stored center-normalized posX/posY + scale; load them as
    // top-left pixel layout on the project canvas.
    auto kf = [](double value) {
        return QJsonObject{
            {QStringLiteral("interpolation"), QStringLiteral("linear")},
            {QStringLiteral("keyframes"),
             QJsonArray{QJsonObject{{QStringLiteral("timeUs"), 0.0},
                                    {QStringLiteral("value"), value}}}},
        };
    };
    const QJsonObject root{
        {QStringLiteral("version"), 2},
        {QStringLiteral("projectName"), QStringLiteral("LegacyTransform")},
        {QStringLiteral("fps"), 30},
        {QStringLiteral("width"), 1920},
        {QStringLiteral("height"), 1080},
        {QStringLiteral("tracks"),
         QJsonArray{
             QJsonObject{
                 {QStringLiteral("type"), QStringLiteral("video")},
                 {QStringLiteral("clips"),
                  QJsonArray{
                      QJsonObject{
                          {QStringLiteral("id"), QStringLiteral("legacy-clip")},
                          {QStringLiteral("type"), QStringLiteral("video")},
                          {QStringLiteral("name"), QStringLiteral("v")},
                          {QStringLiteral("timelineStartUs"), 0},
                          {QStringLiteral("timelineDurationUs"), 1000000},
                          {QStringLiteral("srcInUs"), 0},
                          {QStringLiteral("srcOutUs"), 1000000},
                          {QStringLiteral("posX"), kf(0.5)},
                          {QStringLiteral("posY"), kf(0.5)},
                          {QStringLiteral("scale"), kf(1.0)},
                      },
                  }},
             },
         }},
    };

    QString error;
    const drift::Project loaded = drift::Project::fromJson(root, &error);
    QVERIFY(error.isEmpty());
    QVERIFY(!loaded.tracks().isEmpty());
    QVERIFY(!loaded.tracks()[0].clips.isEmpty());
    const drift::Clip &clip = loaded.tracks()[0].clips[0];
    QCOMPARE(clip.transformW.evaluateAt(0), 1920.0);
    QCOMPARE(clip.transformH.evaluateAt(0), 1080.0);
    QCOMPARE(clip.transformX.evaluateAt(0), 0.0);
    QCOMPARE(clip.transformY.evaluateAt(0), 0.0);
}

void CoreTest::volumeKeyframeSerialization()
{
    drift::Project project;
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Audio});

    drift::Clip clip;
    clip.id = QStringLiteral("clip-volume");
    clip.type = drift::ClipType::Audio;
    clip.name = QStringLiteral("Audio");
    clip.timelineStart = 0;
    clip.timelineDuration = drift::secondsToUs(4.0);
    clip.volume.setKeyframe(0, 1.0);
    clip.volume.setKeyframe(drift::secondsToUs(2.0), 0.5);
    project.tracks()[0].clips.append(clip);

    const QJsonObject json = project.toJson();
    QString error;
    const drift::Project loaded = drift::Project::fromJson(json, &error);

    QVERIFY(error.isEmpty());
    const drift::Clip &loadedClip = loaded.tracks()[0].clips[0];
    QCOMPARE(loadedClip.volume.evaluateAt(0), 1.0);
    QCOMPARE(loadedClip.volume.evaluateAt(drift::secondsToUs(2.0)), 0.5);
    QCOMPARE(loadedClip.volume.evaluateAt(drift::secondsToUs(1.0)), 0.75);
}

void CoreTest::projectLoadsLegacyV1Format()
{
    const QJsonObject root{
        {QStringLiteral("version"), 1},
        {QStringLiteral("projectName"), QStringLiteral("Legacy")},
        {QStringLiteral("assets"),
         QJsonArray{
             QJsonObject{
                 {QStringLiteral("name"), QStringLiteral("a.mp4")},
                 {QStringLiteral("kind"), QStringLiteral("video")},
                 {QStringLiteral("durationSeconds"), 12.0},
                 {QStringLiteral("path"), QStringLiteral("/tmp/a.mp4")},
             },
         }},
        {QStringLiteral("tracks"),
         QJsonArray{
             QJsonObject{
                 {QStringLiteral("type"), QStringLiteral("video")},
                 {QStringLiteral("clips"),
                  QJsonArray{
                      QJsonObject{
                          {QStringLiteral("name"), QStringLiteral("a.mp4")},
                          {QStringLiteral("kind"), QStringLiteral("video")},
                          {QStringLiteral("path"), QStringLiteral("/tmp/a.mp4")},
                          {QStringLiteral("start"), 1.0},
                          {QStringLiteral("duration"), 4.0},
                          {QStringLiteral("inPoint"), 0.5},
                          {QStringLiteral("outPoint"), 4.5},
                          {QStringLiteral("assetIndex"), 0},
                      },
                  }},
             },
             QJsonObject{{QStringLiteral("type"), QStringLiteral("text")}},
             QJsonObject{{QStringLiteral("type"), QStringLiteral("audio")}},
         }},
    };

    QString error;
    const drift::Project project = drift::Project::fromJson(root, &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(project.name(), QStringLiteral("Legacy"));
    QCOMPARE(project.tracks()[0].clips.size(), 1);
    QCOMPARE(project.tracks()[0].clips[0].timelineStart, drift::secondsToUs(1.0));
    QCOMPARE(project.tracks()[0].clips[0].srcIn, drift::secondsToUs(0.5));
    QVERIFY(!project.tracks()[0].clips[0].assetId.isEmpty());
}

// fromJson used to have no failure path at all: every field fell back to a default and the
// errorOut param was only ever cleared. Both of these loaded as a plausible-looking project.
void CoreTest::projectRejectsUnreadableDocuments()
{
    // A document from a future format. The .drift container revision is bumped separately, so
    // ProjectBundle's own gate does not catch this.
    {
        drift::Project project;
        QJsonObject json = project.toJson();
        json[QStringLiteral("version")] = drift::Project::kCurrentVersion + 1;

        QString error;
        drift::Project::fromJson(json, &error);
        QVERIFY(!error.isEmpty());
        QVERIFY(error.contains(QStringLiteral("newer version")));
    }

    // Not a project document. Anything without a tracks array used to come back as an empty
    // project named "Untitled Project", which could then be saved back over the original.
    {
        QString error;
        drift::Project::fromJson(QJsonObject{}, &error);
        QVERIFY(!error.isEmpty());

        error.clear();
        drift::Project::fromJson(QJsonObject{{QStringLiteral("hello"), QStringLiteral("world")}},
                                 &error);
        QVERIFY(!error.isEmpty());
    }

    // The current version, and an empty timeline, both still load.
    {
        drift::Project project;
        project.tracks().clear();
        QString error;
        const drift::Project loaded = drift::Project::fromJson(project.toJson(), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(loaded.tracks().size(), 1); // empty timeline falls back to one video track
    }
}

void CoreTest::trackAllowsClipTypes()
{
    drift::Track videoTrack{.type = drift::TrackType::Video};
    QVERIFY(videoTrack.allowsClipType(drift::ClipType::Video));
    QVERIFY(!videoTrack.allowsClipType(drift::ClipType::Image));
    QVERIFY(!videoTrack.allowsClipType(drift::ClipType::Audio));

    drift::Track audioTrack{.type = drift::TrackType::Audio};
    QVERIFY(audioTrack.allowsClipType(drift::ClipType::Audio));
    QVERIFY(!audioTrack.allowsClipType(drift::ClipType::Video));

    drift::Track shapeTrack{.type = drift::TrackType::Shape};
    QVERIFY(shapeTrack.allowsClipType(drift::ClipType::Image));
    QVERIFY(shapeTrack.allowsClipType(drift::ClipType::Shape));
    QVERIFY(!shapeTrack.allowsClipType(drift::ClipType::Video));

    drift::Track subtitleTrack{.type = drift::TrackType::Subtitle};
    QVERIFY(subtitleTrack.allowsClipType(drift::ClipType::Subtitle));
    QVERIFY(!subtitleTrack.allowsClipType(drift::ClipType::Text));
}

void CoreTest::subtitleCueSerialization()
{
    drift::Project project;
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Subtitle});

    drift::Clip clip;
    clip.id = QStringLiteral("clip-subtitle");
    clip.type = drift::ClipType::Subtitle;
    clip.name = QStringLiteral("Subtitles (2)");
    clip.timelineStart = 0;
    clip.timelineDuration = drift::secondsToUs(30.0);
    clip.subtitleCues = {
        {drift::secondsToUs(1.0), drift::secondsToUs(4.0), QStringLiteral("Hello")},
        {drift::secondsToUs(5.0), drift::secondsToUs(8.0), QStringLiteral("World")},
    };
    project.tracks()[0].clips.append(clip);

    const QJsonObject json = project.toJson();
    QString error;
    const drift::Project loaded = drift::Project::fromJson(json, &error);

    QVERIFY(error.isEmpty());
    const drift::Clip &loadedClip = loaded.tracks()[0].clips[0];
    QCOMPARE(loadedClip.type, drift::ClipType::Subtitle);
    QCOMPARE(loadedClip.subtitleCues.size(), 2);
    QCOMPARE(loadedClip.subtitleCues[0].text, QStringLiteral("Hello"));
    QCOMPARE(loadedClip.subtitleCues[1].startUs, drift::secondsToUs(5.0));
}

void CoreTest::subtitleCueLookup()
{
    QList<drift::SubtitleCue> cues;
    cues.append({drift::secondsToUs(1.0), drift::secondsToUs(3.0), QStringLiteral("A")});
    cues.append({drift::secondsToUs(4.0), drift::secondsToUs(6.0), QStringLiteral("B")});

    const drift::SubtitleCue *active =
        drift::activeSubtitleCueAt(cues, drift::secondsToUs(2.5));
    QVERIFY(active);
    QCOMPARE(active->text, QStringLiteral("A"));
    QVERIFY(!drift::activeSubtitleCueAt(cues, drift::secondsToUs(3.5)));
    QCOMPARE(drift::subtitleCueIndexAt(cues, drift::secondsToUs(5.0)), 1);
}

void CoreTest::subtitleCuePacking()
{
    // One long Whisper segment should become several ~42-char single-line cues.
    QList<drift::SubtitleCue> input;
    input.append({drift::secondsToUs(0.0), drift::secondsToUs(10.0),
                  QStringLiteral("Hello everyone welcome to the show today we will talk about "
                                 "video editing and automatic subtitles.")});

    const QList<drift::SubtitleCue> packed = drift::packSubtitleCues(input, 42, 1);
    QVERIFY(packed.size() >= 2);
    for (const drift::SubtitleCue &cue : packed) {
        // A single oversize token may exceed the width; otherwise stay within 42.
        QVERIFY(cue.text.size() <= 42 || !cue.text.contains(QLatin1Char(' ')));
        QVERIFY(cue.endUs > cue.startUs);
        QVERIFY(!cue.text.contains(QLatin1Char('\n')));
    }
    QCOMPARE(packed.first().startUs, drift::secondsToUs(0.0));
    QCOMPARE(packed.last().endUs, drift::secondsToUs(10.0));

    // Short cues under the limit stay as a single cue.
    QList<drift::SubtitleCue> shortInput;
    shortInput.append(
        {drift::secondsToUs(1.0), drift::secondsToUs(2.0), QStringLiteral("Hi there")});
    const QList<drift::SubtitleCue> shortPacked = drift::packSubtitleCues(shortInput, 42, 1);
    QCOMPARE(shortPacked.size(), 1);
    QCOMPARE(shortPacked.first().text, QStringLiteral("Hi there"));

    // A word cap splits further than the width alone would, and keeps the segment's outer edges.
    const QList<drift::SubtitleCue> capped = drift::packSubtitleCues(input, 42, 1, 2);
    QVERIFY(capped.size() > packed.size());
    for (const drift::SubtitleCue &cue : capped) {
        QCOMPARE(cue.text.split(QLatin1Char(' '), Qt::SkipEmptyParts).size() <= 2, true);
        QVERIFY(cue.endUs > cue.startUs);
    }
    QCOMPARE(capped.first().startUs, drift::secondsToUs(0.0));
    QCOMPARE(capped.last().endUs, drift::secondsToUs(10.0));

    // A cap of 0 is the recommended packing, unchanged.
    const QList<drift::SubtitleCue> uncapped = drift::packSubtitleCues(input, 42, 1, 0);
    QCOMPARE(uncapped.size(), packed.size());
}

void CoreTest::srtRoundTrip()
{
    QList<drift::SubtitleCue> cues;
    cues.append({drift::secondsToUs(1.0), drift::secondsToUs(4.0), QStringLiteral("Hello")});
    cues.append({drift::secondsToUs(65.5), drift::secondsToUs(70.25),
                 QStringLiteral("Line one\nLine two")});

    const QString srt = drift::writeSrt(cues);
    QVERIFY(srt.contains(QStringLiteral("00:00:01,000 --> 00:00:04,000")));
    QVERIFY(srt.contains(QStringLiteral("00:01:05,500 --> 00:01:10,250")));
    QVERIFY(srt.contains(QStringLiteral("Line one\nLine two")));

    QList<drift::SubtitleCue> loaded;
    QString error;
    QVERIFY(drift::parseSrt(srt, &loaded, &error));
    QVERIFY(error.isEmpty());
    QCOMPARE(loaded.size(), 2);
    QCOMPARE(loaded[0].text, QStringLiteral("Hello"));
    QCOMPARE(loaded[0].startUs, drift::secondsToUs(1.0));
    QCOMPARE(loaded[0].endUs, drift::secondsToUs(4.0));
    QCOMPARE(loaded[1].text, QStringLiteral("Line one\nLine two"));
    QCOMPARE(loaded[1].startUs, drift::secondsToUs(65.5));
    QCOMPARE(loaded[1].endUs, drift::secondsToUs(70.25));
}

void CoreTest::srtParseEdgeCases()
{
    // Dot milliseconds (common non-strict variant) and UTF-8 BOM.
    const QString srt = QStringLiteral("\uFEFF1\n00:00:00.500 --> 00:00:02.000\nCafé\n");
    QList<drift::SubtitleCue> cues;
    QString error;
    QVERIFY(drift::parseSrt(srt, &cues, &error));
    QCOMPARE(cues.size(), 1);
    QCOMPARE(cues[0].text, QStringLiteral("Café"));
    QCOMPARE(cues[0].startUs, drift::secondsToUs(0.5));
    QCOMPARE(cues[0].endUs, drift::secondsToUs(2.0));

    QVERIFY(!drift::parseSrt(QString(), &cues, &error));
    QVERIFY(!error.isEmpty());
}

void CoreTest::insertTrackAtTopAllowsDuplicateTypes()
{
    drift::Project project;
    QCOMPARE(project.tracks().size(), 1);
    QCOMPARE(project.tracks()[0].type, drift::TrackType::Video);

    const int first = drift::insertTrackAtTopForClipType(project, drift::ClipType::Video);
    QCOMPARE(first, 0);
    QCOMPARE(project.tracks().size(), 2);
    QCOMPARE(project.tracks()[0].type, drift::TrackType::Video);
    QCOMPARE(project.tracks()[1].type, drift::TrackType::Video);

    const int second = drift::insertTrackAtTopForClipType(project, drift::ClipType::Audio);
    QCOMPARE(second, 0);
    QCOMPARE(project.tracks().size(), 3);
    QCOMPARE(project.tracks()[0].type, drift::TrackType::Audio);
}

void CoreTest::multiTrackSerializationRoundTrip()
{
    drift::Project project;
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Video, .muted = true});
    project.tracks().append(drift::Track{.type = drift::TrackType::Audio, .showWaveform = true});
    project.tracks().append(drift::Track{.type = drift::TrackType::Video, .hidden = true});
    project.tracks().append(drift::Track{.type = drift::TrackType::Text});

    drift::Clip clip;
    clip.id = QStringLiteral("clip-v2");
    clip.type = drift::ClipType::Video;
    clip.timelineStart = drift::secondsToUs(1.0);
    clip.timelineDuration = drift::secondsToUs(2.0);
    project.tracks()[2].clips.append(clip);

    const QJsonObject json = project.toJson();
    QString error;
    const drift::Project loaded = drift::Project::fromJson(json, &error);

    QVERIFY(error.isEmpty());
    QCOMPARE(loaded.tracks().size(), 4);
    QCOMPARE(loaded.tracks()[0].type, drift::TrackType::Video);
    QVERIFY(loaded.tracks()[0].muted);
    QCOMPARE(loaded.tracks()[1].type, drift::TrackType::Audio);
    QVERIFY(loaded.tracks()[1].showWaveform);
    QCOMPARE(loaded.tracks()[2].type, drift::TrackType::Video);
    QVERIFY(loaded.tracks()[2].hidden);
    QCOMPARE(loaded.tracks()[2].clips.size(), 1);
    QCOMPARE(loaded.tracks()[2].clips[0].id, QStringLiteral("clip-v2"));
    QCOMPARE(loaded.tracks()[3].type, drift::TrackType::Text);
}

void CoreTest::textStyleAndBlendModeSerialization()
{
    drift::Project project;
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Text});

    drift::Clip clip;
    clip.id = QStringLiteral("clip-textstyle");
    clip.type = drift::ClipType::Text;
    clip.name = QStringLiteral("Title");
    clip.textContent = QStringLiteral("Hello");
    clip.timelineStart = 0;
    clip.timelineDuration = drift::secondsToUs(3.0);
    clip.blendMode = drift::BlendMode::Multiply;
    clip.textStyle.fontFamily = QStringLiteral("Courier New");
    clip.textStyle.pixelSize = 88;
    clip.textStyle.fontWeight = 300;
    clip.textStyle.italic = true;
    clip.textStyle.color = QColor(10, 20, 30, 200);
    clip.textStyle.align = drift::TextAlign::Right;
    clip.textStyle.valign = drift::TextVAlign::Bottom;
    clip.textStyle.wordWrap = false;
    clip.textStyle.lineHeight = 1.6;
    clip.textStyle.letterSpacing = 3.5;
    clip.textStyle.outlineEnabled = true;
    clip.textStyle.outlineWidth = 2.5;
    clip.textStyle.outlineColor = QColor(255, 0, 0);
    clip.textStyle.shadowEnabled = true;
    clip.textStyle.shadowOffsetX = -3.0;
    clip.textStyle.shadowOffsetY = 7.0;
    clip.textStyle.shadowBlur = 11.0;
    clip.textStyle.shadowOpacity = 0.42;
    clip.textStyle.shadowColor = QColor(0, 128, 255);
    clip.textStyle.glowEnabled = true;
    clip.textStyle.glowColor = QColor(0, 255, 128);
    clip.textStyle.glowRadius = 21.0;
    clip.textStyle.glowOpacity = 0.55;
    clip.textStyle.boxEnabled = true;
    clip.textStyle.boxColor = QColor(0, 0, 0, 100);
    clip.textStyle.boxPadding = 12.0;
    clip.textStyle.boxRadius = 5.0;
    clip.textStyle.packId = QStringLiteral("hormozi");
    clip.textStyle.wordHighlight = {true, QColor(12, 34, 56), 9.0, 3.0};
    clip.textStyle.underlineEnabled = true;
    clip.textStyle.underlineColor = QColor(200, 100, 50);
    clip.textStyle.underlineWidth = 7.5;
    clip.textStyle.underlineOffset = 2.5;
    clip.textStyle.accent.rule = drift::WordAccentRule::EveryNth;
    clip.textStyle.accent.n = 3;
    clip.textStyle.accent.phase = 1;
    clip.textStyle.accent.colorEnabled = true;
    clip.textStyle.accent.color = QColor(9, 8, 7);
    clip.textStyle.accent.sizeScale = 1.4;
    clip.textStyle.accent.outlineEnabled = true;
    clip.textStyle.accent.outlineWidth = 4.5;
    clip.textStyle.accent.outlineColor = QColor(1, 2, 3);
    clip.textStyle.accent.highlight = {true, QColor(60, 70, 80), 11.0, 6.0};
    clip.textStyle.animIn = {drift::TextAnimKind::Pop, drift::secondsToUs(0.3), drift::TextEase::Back};
    clip.textStyle.animOut = {drift::TextAnimKind::SlideDown, drift::secondsToUs(0.25),
                              drift::TextEase::EaseInOut};
    project.tracks()[0].clips.append(clip);

    const QJsonObject json = project.toJson();
    QString error;
    const drift::Project loaded = drift::Project::fromJson(json, &error);

    QVERIFY(error.isEmpty());
    const drift::Clip &loadedClip = loaded.tracks()[0].clips[0];
    const drift::TextStyle &s = loadedClip.textStyle;
    QCOMPARE(loadedClip.blendMode, drift::BlendMode::Multiply);
    QCOMPARE(s.fontFamily, QStringLiteral("Courier New"));
    QCOMPARE(s.pixelSize, 88);
    QCOMPARE(s.fontWeight, 300);
    QCOMPARE(s.italic, true);
    QCOMPARE(s.color, QColor(10, 20, 30, 200));
    QCOMPARE(s.align, drift::TextAlign::Right);
    QCOMPARE(s.valign, drift::TextVAlign::Bottom);
    QCOMPARE(s.wordWrap, false);
    QCOMPARE(s.lineHeight, 1.6);
    QCOMPARE(s.letterSpacing, 3.5);
    QCOMPARE(s.outlineEnabled, true);
    QCOMPARE(s.outlineWidth, 2.5);
    QCOMPARE(s.outlineColor, QColor(255, 0, 0));
    QCOMPARE(s.shadowEnabled, true);
    QCOMPARE(s.shadowOffsetX, -3.0);
    QCOMPARE(s.shadowOffsetY, 7.0);
    QCOMPARE(s.shadowBlur, 11.0);
    QCOMPARE(s.shadowOpacity, 0.42);
    QCOMPARE(s.shadowColor, QColor(0, 128, 255));
    QCOMPARE(s.glowEnabled, true);
    QCOMPARE(s.glowColor, QColor(0, 255, 128));
    QCOMPARE(s.glowRadius, 21.0);
    QCOMPARE(s.glowOpacity, 0.55);
    QCOMPARE(s.boxEnabled, true);
    QCOMPARE(s.boxColor, QColor(0, 0, 0, 100));
    QCOMPARE(s.boxPadding, 12.0);
    QCOMPARE(s.boxRadius, 5.0);
    QCOMPARE(s.packId, QStringLiteral("hormozi"));
    QCOMPARE(s.wordHighlight.enabled, true);
    QCOMPARE(s.wordHighlight.color, QColor(12, 34, 56));
    QCOMPARE(s.wordHighlight.padding, 9.0);
    QCOMPARE(s.wordHighlight.radius, 3.0);
    QCOMPARE(s.underlineEnabled, true);
    QCOMPARE(s.underlineColor, QColor(200, 100, 50));
    QCOMPARE(s.underlineWidth, 7.5);
    QCOMPARE(s.underlineOffset, 2.5);
    QCOMPARE(s.accent.rule, drift::WordAccentRule::EveryNth);
    QCOMPARE(s.accent.n, 3);
    QCOMPARE(s.accent.phase, 1);
    QCOMPARE(s.accent.colorEnabled, true);
    QCOMPARE(s.accent.color, QColor(9, 8, 7));
    QCOMPARE(s.accent.sizeScale, 1.4);
    QCOMPARE(s.accent.outlineEnabled, true);
    QCOMPARE(s.accent.outlineWidth, 4.5);
    QCOMPARE(s.accent.outlineColor, QColor(1, 2, 3));
    QCOMPARE(s.accent.highlight.enabled, true);
    QCOMPARE(s.accent.highlight.color, QColor(60, 70, 80));
    QCOMPARE(s.accent.highlight.padding, 11.0);
    QCOMPARE(s.accent.highlight.radius, 6.0);
    QCOMPARE(s.animIn.kind, drift::TextAnimKind::Pop);
    QCOMPARE(s.animIn.durationUs, drift::secondsToUs(0.3));
    QCOMPARE(s.animIn.ease, drift::TextEase::Back);
    QCOMPARE(s.animOut.kind, drift::TextAnimKind::SlideDown);
    QCOMPARE(s.animOut.durationUs, drift::secondsToUs(0.25));
    QCOMPARE(s.animOut.ease, drift::TextEase::EaseInOut);
}

void CoreTest::legacyBoldMigratesToFontWeight()
{
    // Projects written before the weight ladder carried a bold flag instead.
    const auto weightForLegacy = [](const QJsonObject &textStyle) {
        QJsonObject clip{
            {QStringLiteral("id"), QStringLiteral("c1")},
            {QStringLiteral("type"), QStringLiteral("text")},
            {QStringLiteral("textContent"), QStringLiteral("Hi")},
            {QStringLiteral("timelineStart"), 0},
            {QStringLiteral("timelineDuration"), 1000000},
            {QStringLiteral("textStyle"), textStyle},
        };
        QJsonObject track{
            {QStringLiteral("type"), QStringLiteral("text")},
            {QStringLiteral("clips"), QJsonArray{clip}},
        };
        QJsonObject project{
            {QStringLiteral("version"), 2},
            {QStringLiteral("width"), 1920},
            {QStringLiteral("height"), 1080},
            {QStringLiteral("fps"), 30},
            {QStringLiteral("tracks"), QJsonArray{track}},
        };
        QString error;
        const drift::Project loaded = drift::Project::fromJson(project, &error);
        return loaded.tracks().at(0).clips.at(0).textStyle.fontWeight;
    };

    QCOMPARE(weightForLegacy({{QStringLiteral("bold"), true}}), 700);
    QCOMPARE(weightForLegacy({{QStringLiteral("bold"), false}}), 400);
    // A style object with neither key keeps the struct default.
    QCOMPARE(weightForLegacy({{QStringLiteral("pixelSize"), 40}}), 700);
    // A new-format style wins over any stale bold flag.
    QCOMPARE(weightForLegacy({{QStringLiteral("bold"), false}, {QStringLiteral("fontWeight"), 900}}), 900);

    // Pre-outlineEnabled projects treated any positive width as on.
    {
        QJsonObject clip{
            {QStringLiteral("id"), QStringLiteral("c1")},
            {QStringLiteral("type"), QStringLiteral("text")},
            {QStringLiteral("textContent"), QStringLiteral("Hi")},
            {QStringLiteral("timelineStart"), 0},
            {QStringLiteral("timelineDuration"), 1000000},
            {QStringLiteral("textStyle"), QJsonObject{{QStringLiteral("outlineWidth"), 3.0}}},
        };
        QJsonObject track{
            {QStringLiteral("type"), QStringLiteral("text")},
            {QStringLiteral("clips"), QJsonArray{clip}},
        };
        QJsonObject project{
            {QStringLiteral("version"), 2},
            {QStringLiteral("width"), 1920},
            {QStringLiteral("height"), 1080},
            {QStringLiteral("fps"), 30},
            {QStringLiteral("tracks"), QJsonArray{track}},
        };
        QString err;
        const drift::Project loaded = drift::Project::fromJson(project, &err);
        QVERIFY(err.isEmpty());
        QCOMPARE(loaded.tracks().at(0).clips.at(0).textStyle.outlineEnabled, true);
        QCOMPARE(loaded.tracks().at(0).clips.at(0).textStyle.outlineWidth, 3.0);
    }
}

void CoreTest::textPresetsAreWellFormed()
{
    const QList<drift::TextPreset> &presets = drift::textPresets();
    QVERIFY(!presets.isEmpty());

    QSet<QString> ids;
    for (const drift::TextPreset &preset : presets) {
        QVERIFY(!preset.id.isEmpty());
        QVERIFY(!preset.label.isEmpty());
        QVERIFY(!preset.sampleText.isEmpty());
        QVERIFY(!ids.contains(preset.id));
        ids.insert(preset.id);
        QVERIFY(preset.style.pixelSize > 0);
        QVERIFY(!preset.style.fontFamily.isEmpty());
        QVERIFY(preset.style.fontWeight >= 100 && preset.style.fontWeight <= 900);
        QCOMPARE(drift::textStyleForPresetId(preset.id)->fontFamily, preset.style.fontFamily);
        QCOMPARE(drift::textPresetForId(preset.id)->label, preset.label);

        // A pack's accent has to be usable: a stride that advances, a size that renders, and an
        // override that actually changes something when a rule picks words out.
        const drift::WordAccent &accent = preset.style.accent;
        QVERIFY(accent.n >= 1);
        QVERIFY(accent.phase >= 0);
        QVERIFY(accent.sizeScale > 0.0);
        if (accent.rule != drift::WordAccentRule::None) {
            QVERIFY(accent.colorEnabled || accent.outlineEnabled || accent.highlight.enabled
                    || !qFuzzyCompare(accent.sizeScale, 1.0));
        }
        if (accent.colorEnabled)
            QVERIFY(accent.color.isValid());
        if (accent.highlight.enabled)
            QVERIFY(accent.highlight.color.isValid());
    }
    QVERIFY(drift::textStyleForPresetId(QStringLiteral("nope")) == nullptr);
}

void CoreTest::karaokeWordIndexTracksTheCue()
{
    const QString text = QStringLiteral("Number of thumbnails that");
    const drift::TimeUs start = drift::secondsToUs(2.0);
    const drift::TimeUs end = drift::secondsToUs(4.0);

    // Before the window there is no spoken word at all.
    QCOMPARE(drift::activeWordIndexAt(text, start, end, drift::secondsToUs(1.0)), -1);
    QCOMPARE(drift::activeWordIndexAt(text, start, start, drift::secondsToUs(2.5)), -1);

    // Inside it the index only ever advances, starts at the first word and ends on the last.
    QCOMPARE(drift::activeWordIndexAt(text, start, end, start), 0);
    int previous = 0;
    for (int step = 1; step <= 20; ++step) {
        const drift::TimeUs at = start + (end - start) * step / 21;
        const int index = drift::activeWordIndexAt(text, start, end, at);
        QVERIFY(index >= previous);
        QVERIFY(index < 4);
        previous = index;
    }
    QCOMPARE(drift::activeWordIndexAt(text, start, end, end - 1), 3);
    // Past the end (rounding at a cue boundary) keeps the last word lit rather than blanking it.
    QCOMPARE(drift::activeWordIndexAt(text, start, end, end), 3);
}

void CoreTest::shapeStyleSerialization()
{
    drift::Project project;
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Shape});

    drift::Clip clip;
    clip.id = QStringLiteral("clip-shape");
    clip.type = drift::ClipType::Shape;
    clip.name = QStringLiteral("Hexagon");
    clip.timelineStart = 0;
    clip.timelineDuration = drift::kImageClipDurationUs;
    clip.shapeStyle.kind = drift::ShapeKind::Hexagon;
    clip.shapeStyle.fillKind = drift::ShapeFillKind::LinearGradient;
    clip.shapeStyle.fill = QColor(10, 20, 30, 200);
    clip.shapeStyle.fillSecondary = QColor(40, 50, 60, 128);
    clip.shapeStyle.gradientAngle = 35.0;
    clip.shapeStyle.stroke = QColor(255, 255, 255);
    clip.shapeStyle.strokeWidth = 6.0;
    clip.shapeStyle.strokeStyle = drift::ShapeStrokeStyle::DashDot;
    clip.shapeStyle.cornerRadius = 18.0;
    clip.shapeStyle.points = 9;
    clip.shapeStyle.innerRatio = 0.33;
    clip.shapeStyle.headSize = 0.55;
    clip.shapeStyle.thickness = 0.22;
    clip.shapeStyle.tailX = 0.7;
    clip.shapeStyle.tailSize = 0.15;
    clip.transformX.setKeyframe(0, 100.0);
    clip.transformY.setKeyframe(0, 200.0);
    project.tracks()[0].clips.append(clip);

    const QJsonObject json = project.toJson();
    QString error;
    const drift::Project loaded = drift::Project::fromJson(json, &error);

    QVERIFY(error.isEmpty());
    const drift::Clip &loadedClip = loaded.tracks()[0].clips[0];
    QCOMPARE(loadedClip.type, drift::ClipType::Shape);
    QCOMPARE(loadedClip.shapeStyle.kind, drift::ShapeKind::Hexagon);
    QCOMPARE(loadedClip.shapeStyle.fillKind, drift::ShapeFillKind::LinearGradient);
    QCOMPARE(loadedClip.shapeStyle.fill, QColor(10, 20, 30, 200));
    QCOMPARE(loadedClip.shapeStyle.fillSecondary, QColor(40, 50, 60, 128));
    QCOMPARE(loadedClip.shapeStyle.gradientAngle, 35.0);
    QCOMPARE(loadedClip.shapeStyle.stroke, QColor(255, 255, 255));
    QCOMPARE(loadedClip.shapeStyle.strokeWidth, 6.0);
    QCOMPARE(loadedClip.shapeStyle.strokeStyle, drift::ShapeStrokeStyle::DashDot);
    QCOMPARE(loadedClip.shapeStyle.cornerRadius, 18.0);
    QCOMPARE(loadedClip.shapeStyle.points, 9);
    QCOMPARE(loadedClip.shapeStyle.innerRatio, 0.33);
    QCOMPARE(loadedClip.shapeStyle.headSize, 0.55);
    QCOMPARE(loadedClip.shapeStyle.thickness, 0.22);
    QCOMPARE(loadedClip.shapeStyle.tailX, 0.7);
    QCOMPARE(loadedClip.shapeStyle.tailSize, 0.15);
    QCOMPARE(loadedClip.transformX.evaluateAt(0), 100.0);
    QCOMPARE(loadedClip.transformY.evaluateAt(0), 200.0);
}

// A project saved before shapes gained gradients, dash styles and geometry knobs carries only the
// original four keys, and must still load with the new fields at their defaults.
void CoreTest::legacyShapeStyleLoadsWithDefaults()
{
    const QJsonObject json{
        {QStringLiteral("version"), drift::Project::kCurrentVersion},
        {QStringLiteral("tracks"),
         QJsonArray{QJsonObject{
             {QStringLiteral("type"), QStringLiteral("shape")},
             {QStringLiteral("clips"),
              QJsonArray{QJsonObject{
                  {QStringLiteral("id"), QStringLiteral("legacy-shape")},
                  {QStringLiteral("type"), QStringLiteral("shape")},
                  {QStringLiteral("timelineDurationUs"), qint64(drift::kImageClipDurationUs)},
                  {QStringLiteral("shapeStyle"),
                   QJsonObject{{QStringLiteral("kind"), QStringLiteral("pentagon")},
                               {QStringLiteral("fill"), QStringLiteral("#ffa060ff")},
                               {QStringLiteral("stroke"), QStringLiteral("#ffffffff")},
                               {QStringLiteral("strokeWidth"), 4.0}}}}}}}}}};

    QString error;
    const drift::Project loaded = drift::Project::fromJson(json, &error);
    QVERIFY(error.isEmpty());

    const drift::ShapeStyle &style = loaded.tracks()[0].clips[0].shapeStyle;
    const drift::ShapeStyle defaults;
    QCOMPARE(style.kind, drift::ShapeKind::Pentagon);
    QCOMPARE(style.fill, QColor(160, 96, 255));
    QCOMPARE(style.fillKind, defaults.fillKind);
    QCOMPARE(style.fillSecondary, defaults.fillSecondary);
    QCOMPARE(style.gradientAngle, defaults.gradientAngle);
    QCOMPARE(style.strokeStyle, defaults.strokeStyle);
    QCOMPARE(style.cornerRadius, defaults.cornerRadius);
    QCOMPARE(style.points, defaults.points);
    QCOMPARE(style.innerRatio, defaults.innerRatio);
}

// Guards ~28 hand-written path formulas: a typo shows up as an empty path or one that escapes the
// layout rect and gets clipped out of the layer.
void CoreTest::shapeCatalogPathsFitBounds()
{
    const QRectF bounds(0, 0, 200, 120);
    QVERIFY(!drift::shapeCatalog().isEmpty());

    for (const drift::ShapeCatalogEntry &entry : drift::shapeCatalog()) {
        const QPainterPath path = drift::shapePath(entry.style, bounds);
        QVERIFY2(!path.isEmpty(), qPrintable(entry.id));
        QVERIFY2(entry.aspect > 0.0, qPrintable(entry.id));

        // Cubic control points can bow a hair outside the hull, so allow a small tolerance.
        const QRectF box = path.boundingRect();
        QVERIFY2(bounds.adjusted(-1, -1, 1, 1).contains(box), qPrintable(entry.id));
        QVERIFY2(box.width() > bounds.width() * 0.3, qPrintable(entry.id));
        QVERIFY2(box.height() > bounds.height() * 0.3, qPrintable(entry.id));

        QVERIFY2(!drift::shapeSvgPath(entry.style, bounds).isEmpty(), qPrintable(entry.id));
        // Ids are what QML and the drag mime data carry, so every one must resolve.
        QVERIFY2(drift::shapeCatalogEntry(entry.id) != nullptr, qPrintable(entry.id));
    }
}

void CoreTest::effectCatalogIdSerialization()
{
    drift::Project project;
    drift::Clip clip;
    clip.id = QStringLiteral("clip-effects");
    clip.type = drift::ClipType::Video;
    clip.name = QStringLiteral("Video");
    clip.timelineStart = 0;
    clip.timelineDuration = drift::secondsToUs(2.0);

    drift::Effect effect;
    effect.name = QStringLiteral("eq");
    effect.catalogId = QStringLiteral("adjust.contrast");
    effect.parameters.insert(QStringLiteral("contrast"), 1.4);
    clip.effects.append(effect);
    project.tracks()[0].clips.append(clip);

    const QJsonObject json = project.toJson();
    QString error;
    const drift::Project loaded = drift::Project::fromJson(json, &error);

    QVERIFY(error.isEmpty());
    const drift::Clip &loadedClip = loaded.tracks()[0].clips[0];
    QCOMPARE(loadedClip.effects.size(), 1);
    QCOMPARE(loadedClip.effects[0].catalogId, QStringLiteral("adjust.contrast"));
    QCOMPARE(loadedClip.effects[0].parameters.value(QStringLiteral("contrast")).toDouble(), 1.4);
    // Projects written before animated params existed carry no paramKeyframes at all.
    QVERIFY(loadedClip.effects[0].paramKeyframes.isEmpty());
}

// An animated effect parameter is only worth anything if it survives a save/load, and the static
// value has to come back alongside it — that is what an un-keyed frame falls back to.
void CoreTest::effectParamKeyframeSerialization()
{
    drift::Project project;
    drift::Clip clip;
    clip.id = QStringLiteral("clip-animated-effect");
    clip.type = drift::ClipType::Video;
    clip.timelineStart = 0;
    clip.timelineDuration = drift::secondsToUs(4.0);

    drift::Effect effect;
    effect.catalogId = QStringLiteral("adjust.contrast");
    effect.parameters.insert(QStringLiteral("contrast"), 1.4);
    drift::KeyframeTrack<double> track;
    track.setKeyframe(0, 0.5);
    track.setKeyframe(drift::secondsToUs(2.0), 2.5);
    track.setEasing(0, drift::Interpolation::Ease);
    track.setEasing(drift::secondsToUs(2.0), drift::Interpolation::Ease);
    effect.paramKeyframes.insert(QStringLiteral("contrast"), track);
    clip.effects.append(effect);
    project.tracks()[0].clips.append(clip);

    const QJsonObject json = project.toJson();
    QString error;
    const drift::Project loaded = drift::Project::fromJson(json, &error);

    QVERIFY(error.isEmpty());
    const drift::Effect &loadedEffect = loaded.tracks()[0].clips[0].effects[0];
    QCOMPARE(loadedEffect.parameters.value(QStringLiteral("contrast")).toDouble(), 1.4);
    const drift::KeyframeTrack<double> &loadedTrack =
        loadedEffect.paramKeyframes.value(QStringLiteral("contrast"));
    QCOMPARE(loadedTrack.keyframes().size(), 2);
    QVERIFY(loadedTrack.easingAt(0) == drift::Interpolation::Ease);
    QVERIFY(loadedTrack.easingAt(drift::secondsToUs(2.0)) == drift::Interpolation::Ease);

    // valueAt is what the compositor reads: the track wins where it has keys, and an unkeyed
    // param falls back to the static value.
    QCOMPARE(loadedEffect.valueAt(QStringLiteral("contrast"), 0).toDouble(), 0.5);
    QCOMPARE(loadedEffect.valueAt(QStringLiteral("contrast"), drift::secondsToUs(2.0)).toDouble(), 2.5);
    const double mid = loadedEffect.valueAt(QStringLiteral("contrast"), drift::secondsToUs(1.0)).toDouble();
    QVERIFY(mid > 0.5 && mid < 2.5);
    QCOMPARE(loadedEffect.valueAt(QStringLiteral("nosuch"), 0).isValid(), false);

    // resolvedAt bakes the animated params down; everything else is carried through untouched.
    const drift::Effect resolved = loadedEffect.resolvedAt(drift::secondsToUs(2.0));
    QCOMPARE(resolved.parameters.value(QStringLiteral("contrast")).toDouble(), 2.5);
    QCOMPARE(resolved.catalogId, QStringLiteral("adjust.contrast"));
}

// Plain Project copy shares QMap payloads with the source. Mutating keyframes on the live
// project must not touch a compositor snapshot (and must not race a concurrent reader).
void CoreTest::detachedCopyIsolatesKeyframesFromLiveMutations()
{
    drift::Project live;
    drift::Clip clip;
    clip.id = QStringLiteral("clip-detach");
    clip.type = drift::ClipType::Video;
    clip.timelineStart = 0;
    clip.timelineDuration = drift::secondsToUs(2.0);
    clip.opacity.setKeyframe(0, 1.0);

    drift::Effect effect;
    effect.catalogId = QStringLiteral("adjust.contrast");
    effect.parameters.insert(QStringLiteral("contrast"), 1.0);
    drift::KeyframeTrack<double> track;
    track.setKeyframe(0, 0.5);
    effect.paramKeyframes.insert(QStringLiteral("contrast"), track);
    clip.effects.append(effect);
    live.tracks()[0].clips.append(clip);

    const drift::Project snapshot = live.detachedCopy();
    QCOMPARE(snapshot.tracks().at(0).clips.at(0).opacity.evaluateAt(0), 1.0);
    QCOMPARE(snapshot.tracks().at(0).clips.at(0).effects.at(0).valueAt(QStringLiteral("contrast"), 0).toDouble(),
             0.5);

    // Mutate every COW container the compositor reads — list structure and nested maps.
    live.tracks()[0].clips[0].opacity.setKeyframe(0, 0.25);
    live.tracks()[0].clips[0].opacity.setKeyframe(drift::secondsToUs(1.0), 0.0);
    live.tracks()[0].clips[0].effects[0].paramKeyframes[QStringLiteral("contrast")].setKeyframe(
        0, 2.0);
    live.tracks()[0].clips[0].effects[0].parameters.insert(QStringLiteral("contrast"), 2.0);
    drift::Clip extra;
    extra.id = QStringLiteral("clip-extra");
    extra.type = drift::ClipType::Video;
    live.tracks()[0].clips.append(extra);

    QCOMPARE(snapshot.tracks().size(), 1);
    QCOMPARE(snapshot.tracks().at(0).clips.size(), 1);
    QCOMPARE(snapshot.tracks().at(0).clips.at(0).opacity.evaluateAt(0), 1.0);
    QCOMPARE(snapshot.tracks().at(0).clips.at(0).opacity.keyframes().size(), 1);
    QCOMPARE(snapshot.tracks().at(0).clips.at(0).effects.at(0).valueAt(QStringLiteral("contrast"), 0).toDouble(),
             0.5);
    QCOMPARE(snapshot.tracks().at(0).clips.at(0).effects.at(0).parameters.value(QStringLiteral("contrast")).toDouble(),
             1.0);

    const drift::Effect resolved =
        snapshot.tracks().at(0).clips.at(0).effects.at(0).resolvedAt(0);
    QCOMPARE(resolved.parameters.value(QStringLiteral("contrast")).toDouble(), 0.5);
}

// A beat-synced template applies several effects and per-param keyframes in one edit; all of
// that has to survive save/load so scrubbing after reopen matches what preview showed.
void CoreTest::effectTemplateStackSerialization()
{
    drift::Project project;
    drift::Clip clip;
    clip.id = QStringLiteral("clip-template");
    clip.type = drift::ClipType::Video;
    clip.timelineStart = 0;
    clip.timelineDuration = drift::secondsToUs(4.0);

    drift::Effect shake;
    shake.catalogId = QStringLiteral("beat_shake");
    shake.parameters.insert(QStringLiteral("amount"), 0.0);
    drift::KeyframeTrack<double> amountTrack;
    amountTrack.setKeyframe(0, 0.7);
    amountTrack.setKeyframe(drift::secondsToUs(0.18), 0.0);
    amountTrack.setKeyframe(drift::secondsToUs(1.0), 0.65);
    amountTrack.setKeyframe(drift::secondsToUs(1.09), 0.0);
    shake.paramKeyframes.insert(QStringLiteral("amount"), amountTrack);
    clip.effects.append(shake);

    drift::Effect strobe;
    strobe.catalogId = QStringLiteral("strobe_flash");
    strobe.parameters.insert(QStringLiteral("flash"), 0.0);
    drift::KeyframeTrack<double> flashTrack;
    flashTrack.setKeyframe(0, 0.5);
    flashTrack.setKeyframe(drift::secondsToUs(0.09), 0.0);
    strobe.paramKeyframes.insert(QStringLiteral("flash"), flashTrack);
    clip.effects.append(strobe);

    project.tracks()[0].clips.append(clip);

    const QJsonObject json = project.toJson();
    QString error;
    const drift::Project loaded = drift::Project::fromJson(json, &error);

    QVERIFY(error.isEmpty());
    const drift::Clip &loadedClip = loaded.tracks()[0].clips[0];
    QCOMPARE(loadedClip.effects.size(), 2);
    QCOMPARE(loadedClip.effects[0].catalogId, QStringLiteral("beat_shake"));
    QCOMPARE(loadedClip.effects[1].catalogId, QStringLiteral("strobe_flash"));

    const drift::KeyframeTrack<double> &loadedAmount =
        loadedClip.effects[0].paramKeyframes.value(QStringLiteral("amount"));
    QCOMPARE(loadedAmount.keyframes().size(), 4);
    QCOMPARE(loadedAmount.keyframes().value(0).value, 0.7);
    QCOMPARE(loadedAmount.keyframes().value(drift::secondsToUs(1.0)).value, 0.65);

    const drift::KeyframeTrack<double> &loadedFlash =
        loadedClip.effects[1].paramKeyframes.value(QStringLiteral("flash"));
    QCOMPARE(loadedFlash.keyframes().size(), 2);
    QCOMPARE(loadedFlash.keyframes().value(0).value, 0.5);
    QCOMPARE(loadedClip.effects[1].valueAt(QStringLiteral("flash"), 0).toDouble(), 0.5);
}

// Audio effects live in a separate list from video effects on the clip and must survive a project
// round-trip independently — a regression here silently drops a clip's sound design on reload.
void CoreTest::audioEffectSerialization()
{
    drift::Project project;
    drift::Clip clip;
    clip.id = QStringLiteral("clip-audio-fx");
    clip.type = drift::ClipType::Audio;
    clip.name = QStringLiteral("Audio");
    clip.timelineStart = 0;
    clip.timelineDuration = drift::secondsToUs(2.0);

    drift::Effect telephone;
    telephone.name = QStringLiteral("Telephone");
    telephone.catalogId = QStringLiteral("transmission.telephone");
    clip.audioEffects.append(telephone);

    drift::Effect bitcrush;
    bitcrush.name = QStringLiteral("Bitcrush");
    bitcrush.catalogId = QStringLiteral("texture.bitcrush");
    bitcrush.parameters.insert(QStringLiteral("bits"), 6.0);
    bitcrush.parameters.insert(QStringLiteral("mix"), 0.7);
    clip.audioEffects.append(bitcrush);

    // A video effect on the same clip must not bleed into the audio list and vice versa.
    drift::Effect contrast;
    contrast.catalogId = QStringLiteral("adjust.contrast");
    clip.effects.append(contrast);

    project.tracks()[0].clips.append(clip);

    const QJsonObject json = project.toJson();
    QString error;
    const drift::Project loaded = drift::Project::fromJson(json, &error);

    QVERIFY(error.isEmpty());
    const drift::Clip &loadedClip = loaded.tracks()[0].clips[0];
    QCOMPARE(loadedClip.audioEffects.size(), 2);
    QCOMPARE(loadedClip.audioEffects[0].catalogId, QStringLiteral("transmission.telephone"));
    QCOMPARE(loadedClip.audioEffects[1].catalogId, QStringLiteral("texture.bitcrush"));
    QCOMPARE(loadedClip.audioEffects[1].parameters.value(QStringLiteral("bits")).toDouble(), 6.0);
    QCOMPARE(loadedClip.audioEffects[1].parameters.value(QStringLiteral("mix")).toDouble(), 0.7);
    QCOMPARE(loadedClip.effects.size(), 1);
    QCOMPARE(loadedClip.effects[0].catalogId, QStringLiteral("adjust.contrast"));
}

void CoreTest::rgbSplitEffectParametersSerialization()
{
    drift::Project project;
    drift::Clip clip;
    clip.id = QStringLiteral("clip-rgb-split");
    clip.type = drift::ClipType::Video;
    clip.name = QStringLiteral("Video");
    clip.timelineStart = 0;
    clip.timelineDuration = drift::secondsToUs(2.0);

    drift::Effect effect;
    effect.name = QStringLiteral("rgb_split");
    effect.catalogId = QStringLiteral("rgb_split");
    effect.parameters.insert(QStringLiteral("amount"), 12.0);
    effect.parameters.insert(QStringLiteral("angle"), 45.0);
    effect.parameters.insert(QStringLiteral("animated"), true);
    effect.parameters.insert(QStringLiteral("speed"), 2.5);
    clip.effects.append(effect);
    project.tracks()[0].clips.append(clip);

    const QJsonObject json = project.toJson();
    QString error;
    const drift::Project loaded = drift::Project::fromJson(json, &error);

    QVERIFY(error.isEmpty());
    const drift::Clip &loadedClip = loaded.tracks()[0].clips[0];
    QCOMPARE(loadedClip.effects.size(), 1);
    QCOMPARE(loadedClip.effects[0].catalogId, QStringLiteral("rgb_split"));
    const QMap<QString, QVariant> &params = loadedClip.effects[0].parameters;
    QCOMPARE(params.value(QStringLiteral("amount")).toDouble(), 12.0);
    QCOMPARE(params.value(QStringLiteral("angle")).toDouble(), 45.0);
    QCOMPARE(params.value(QStringLiteral("animated")).toBool(), true);
    QCOMPARE(params.value(QStringLiteral("speed")).toDouble(), 2.5);
}

void CoreTest::blockGlitchEffectParametersSerialization()
{
    drift::Project project;
    drift::Clip clip;
    clip.id = QStringLiteral("clip-block-glitch");
    clip.type = drift::ClipType::Video;
    clip.name = QStringLiteral("Video");
    clip.timelineStart = 0;
    clip.timelineDuration = drift::secondsToUs(2.0);

    drift::Effect effect;
    effect.name = QStringLiteral("block_glitch");
    effect.catalogId = QStringLiteral("block_glitch");
    effect.parameters.insert(QStringLiteral("intensity"), 0.5);
    effect.parameters.insert(QStringLiteral("blockSize"), 48.0);
    effect.parameters.insert(QStringLiteral("shiftAmount"), 36.0);
    effect.parameters.insert(QStringLiteral("frequency"), 0.4);
    effect.parameters.insert(QStringLiteral("seed"), 7.0);
    clip.effects.append(effect);
    project.tracks()[0].clips.append(clip);

    const QJsonObject json = project.toJson();
    QString error;
    const drift::Project loaded = drift::Project::fromJson(json, &error);

    QVERIFY(error.isEmpty());
    const drift::Clip &loadedClip = loaded.tracks()[0].clips[0];
    QCOMPARE(loadedClip.effects.size(), 1);
    QCOMPARE(loadedClip.effects[0].catalogId, QStringLiteral("block_glitch"));
    const QMap<QString, QVariant> &params = loadedClip.effects[0].parameters;
    QCOMPARE(params.value(QStringLiteral("intensity")).toDouble(), 0.5);
    QCOMPARE(params.value(QStringLiteral("blockSize")).toDouble(), 48.0);
    QCOMPARE(params.value(QStringLiteral("shiftAmount")).toDouble(), 36.0);
    QCOMPARE(params.value(QStringLiteral("frequency")).toDouble(), 0.4);
    QCOMPARE(params.value(QStringLiteral("seed")).toDouble(), 7.0);
}

void CoreTest::clipSpeedSourceMapping()
{
    drift::Clip clip;
    clip.timelineStart = drift::secondsToUs(1.0);
    clip.timelineDuration = drift::secondsToUs(4.0);
    clip.srcIn = drift::secondsToUs(2.0);
    clip.speed = 2.0;
    clip.srcOut = clip.srcIn + clip.sourceSpanUs();

    QCOMPARE(clip.sourceSpanUs(), drift::secondsToUs(8.0));
    QCOMPARE(clip.timelineToSourceUs(drift::secondsToUs(3.0)), drift::secondsToUs(6.0));

    clip.syncSrcOutFromSpeed(drift::secondsToUs(20.0));
    QCOMPARE(clip.srcOut, clip.srcIn + drift::secondsToUs(8.0));

    clip.reverse = true;
    // At timeline start → near srcOut; at +1s timeline with speed 2 → srcOut - 2s
    QCOMPARE(clip.timelineToSourceUs(drift::secondsToUs(1.0)), clip.srcOut);
    QCOMPARE(clip.timelineToSourceUs(drift::secondsToUs(2.0)), clip.srcOut - drift::secondsToUs(2.0));
}

namespace {

// A ramp with no handles is linear in speed across pos, which keeps the expected values above
// closed-form rather than something only the implementation can produce.
drift::SpeedCurve linearRamp(double from, double to)
{
    drift::SpeedPoint start;
    start.pos = 0.0;
    start.speed = from;
    start.corner = true;
    drift::SpeedPoint end;
    end.pos = 1.0;
    end.speed = to;
    end.corner = true;

    drift::SpeedCurve curve;
    curve.setPoints({start, end});
    return curve;
}

drift::Clip curvedClip(const drift::SpeedCurve &curve, double srcInSec, double srcOutSec)
{
    drift::Clip clip;
    clip.timelineStart = drift::secondsToUs(1.0);
    clip.srcIn = drift::secondsToUs(srcInSec);
    clip.srcOut = drift::secondsToUs(srcOutSec);
    clip.speedCurve = curve;
    clip.syncDurationFromSpeedCurve();
    return clip;
}

} // namespace

void CoreTest::speedCurveMatchesConstantSpeed()
{
    drift::Clip scalar;
    scalar.timelineStart = drift::secondsToUs(1.0);
    scalar.srcIn = drift::secondsToUs(2.0);
    scalar.srcOut = scalar.srcIn + drift::secondsToUs(8.0);
    scalar.speed = 2.0;
    scalar.timelineDuration = drift::secondsToUs(4.0);

    const drift::Clip curved = curvedClip(drift::SpeedCurve::flat(2.0), 2.0, 10.0);

    QCOMPARE(curved.timelineDuration, scalar.timelineDuration);
    for (int i = 0; i <= 20; ++i) {
        const drift::TimeUs at = scalar.timelineStart + (scalar.timelineDuration * i) / 20;
        QVERIFY(qAbs(curved.timelineToSourceUs(at) - scalar.timelineToSourceUs(at)) <= 2);
    }
}

void CoreTest::speedCurveRampRetimesDuration()
{
    // 1× ramping to 4× over a 10s source: ∫dp/(1+3p) = ln(4)/3.
    const drift::Clip clip = curvedClip(linearRamp(1.0, 4.0), 0.0, 10.0);

    const double expectedSeconds = 10.0 * std::log(4.0) / 3.0;
    QVERIFY(qAbs(drift::usToSeconds(clip.timelineDuration) - expectedSeconds) < 0.001);

    // And the inverse: p = (exp(3t/span) - 1) / 3.
    for (int i = 1; i < 10; ++i) {
        const double t = expectedSeconds * i / 10.0;
        const double expectedPos = (std::exp(3.0 * t / 10.0) - 1.0) / 3.0;
        const drift::TimeUs at = clip.timelineStart + drift::secondsToUs(t);
        const double actual = drift::usToSeconds(clip.timelineToSourceUs(at) - clip.srcIn) / 10.0;
        QVERIFY(qAbs(actual - expectedPos) < 0.002);
    }
}

void CoreTest::speedCurveMappingIsMonotonic()
{
    drift::SpeedPoint a;
    a.pos = 0.0;
    a.speed = 4.0;
    drift::SpeedPoint b;
    b.pos = 0.4;
    b.speed = 0.2;
    drift::SpeedPoint c;
    c.pos = 1.0;
    c.speed = 8.0;
    // Give the dip real tangents, so the flattening and not just the corner case is exercised.
    b.inDx = -0.1;
    b.outDx = 0.15;

    drift::SpeedCurve curve;
    curve.setPoints({a, b, c});
    const drift::Clip clip = curvedClip(curve, 0.0, 12.0);

    QVERIFY(clip.timelineDuration > 0);
    drift::TimeUs previous = -1;
    for (int i = 0; i <= 500; ++i) {
        const drift::TimeUs at = clip.timelineStart + (clip.timelineDuration * i) / 500;
        const drift::TimeUs source = clip.timelineToSourceUs(at);
        QVERIFY(source >= previous);
        QVERIFY(source >= clip.srcIn && source <= clip.srcOut);
        previous = source;
    }
}

void CoreTest::speedCurveSubRangePreservesShape()
{
    const drift::SpeedCurve curve = linearRamp(1.0, 4.0);
    const drift::SpeedCurve head = curve.subRange(0.0, 0.5);
    const drift::SpeedCurve tail = curve.subRange(0.5, 1.0);

    for (int i = 0; i <= 10; ++i) {
        const double f = i / 10.0;
        QVERIFY(qAbs(head.speedAt(f) - curve.speedAt(f * 0.5)) < 0.01);
        QVERIFY(qAbs(tail.speedAt(f) - curve.speedAt(0.5 + f * 0.5)) < 0.01);
    }
}

void CoreTest::speedCurveSerialization()
{
    drift::SpeedPoint mid;
    mid.pos = 0.5;
    mid.speed = 0.25;
    mid.inDx = -0.2;
    mid.inDy = 0.1;
    mid.outDx = 0.3;
    mid.outDy = -0.05;
    mid.corner = true;

    drift::SpeedPoint start;
    start.pos = 0.0;
    start.speed = 1.0;
    drift::SpeedPoint end;
    end.pos = 1.0;
    end.speed = 2.0;

    drift::SpeedCurve curve;
    curve.setPoints({start, mid, end});

    drift::Project project;
    drift::Clip clip;
    clip.id = QStringLiteral("clip-curve");
    clip.type = drift::ClipType::Video;
    clip.srcIn = 0;
    clip.srcOut = drift::secondsToUs(6.0);
    clip.speedCurve = curve;
    clip.syncDurationFromSpeedCurve();
    project.tracks()[0].clips.append(clip);

    QString error;
    const drift::Project loaded = drift::Project::fromJson(project.toJson(), &error);
    QVERIFY(error.isEmpty());

    const drift::Clip &out = loaded.tracks()[0].clips[0];
    QVERIFY(out.hasSpeedCurve());
    QCOMPARE(out.speedCurve.points().size(), 3);
    const drift::SpeedPoint &loadedMid = out.speedCurve.points().at(1);
    QCOMPARE(loadedMid.pos, 0.5);
    QCOMPARE(loadedMid.speed, 0.25);
    QCOMPARE(loadedMid.inDx, -0.2);
    QCOMPARE(loadedMid.outDx, 0.3);
    QCOMPARE(loadedMid.corner, true);
    QCOMPARE(out.speedCurve.retimedDurationUs(out.srcOut - out.srcIn), clip.timelineDuration);
}

void CoreTest::clipReverseAndFlipSerialization()
{
    drift::Project project;
    drift::Clip clip;
    clip.id = QStringLiteral("clip-flip");
    clip.type = drift::ClipType::Video;
    clip.timelineStart = 0;
    clip.timelineDuration = drift::secondsToUs(2.0);
    clip.srcIn = drift::secondsToUs(1.0);
    clip.srcOut = drift::secondsToUs(3.0);
    clip.reverse = true;
    clip.flipH = true;
    clip.flipV = true;
    clip.speed = 1.5;
    project.tracks()[0].clips.append(clip);

    const QJsonObject json = project.toJson();
    QString error;
    const drift::Project loaded = drift::Project::fromJson(json, &error);
    QVERIFY(error.isEmpty());
    const drift::Clip &out = loaded.tracks()[0].clips[0];
    QCOMPARE(out.reverse, true);
    QCOMPARE(out.flipH, true);
    QCOMPARE(out.flipV, true);
    QCOMPARE(out.speed, 1.5);
}

void CoreTest::clipSplitMergeRoundTrip()
{
    drift::Clip head;
    head.id = QStringLiteral("head");
    head.type = drift::ClipType::Video;
    head.assetId = QStringLiteral("asset-a");
    head.path = QStringLiteral("/tmp/a.mp4");
    head.timelineStart = 0;
    head.timelineDuration = drift::secondsToUs(4.0);
    head.srcIn = drift::secondsToUs(1.0);
    head.srcOut = drift::secondsToUs(5.0);
    head.speed = 1.0;

    drift::Clip tail;
    QVERIFY(drift::splitClipAtOffset(head, tail, drift::secondsToUs(2.0)));
    tail.id = QStringLiteral("tail");
    QCOMPARE(head.timelineDuration, drift::secondsToUs(2.0));
    QCOMPARE(tail.timelineStart, drift::secondsToUs(2.0));
    QCOMPARE(head.srcOut, drift::secondsToUs(3.0));
    QCOMPARE(tail.srcIn, drift::secondsToUs(3.0));
    QVERIFY(drift::clipsCanMerge(head, tail));

    const drift::Clip merged = drift::mergeClips(head, tail);
    QCOMPARE(merged.timelineDuration, drift::secondsToUs(4.0));
    QCOMPARE(merged.srcIn, drift::secondsToUs(1.0));
    QCOMPARE(merged.srcOut, drift::secondsToUs(5.0));

    // Reverse split: earlier half maps to higher source.
    drift::Clip rev;
    rev.id = QStringLiteral("rev");
    rev.type = drift::ClipType::Video;
    rev.assetId = QStringLiteral("asset-a");
    rev.path = QStringLiteral("/tmp/a.mp4");
    rev.timelineStart = 0;
    rev.timelineDuration = drift::secondsToUs(4.0);
    rev.srcIn = drift::secondsToUs(1.0);
    rev.srcOut = drift::secondsToUs(5.0);
    rev.reverse = true;
    drift::Clip revTail;
    QVERIFY(drift::splitClipAtOffset(rev, revTail, drift::secondsToUs(2.0)));
    revTail.id = QStringLiteral("rev-tail");
    QCOMPARE(rev.srcIn, drift::secondsToUs(3.0));
    QCOMPARE(rev.srcOut, drift::secondsToUs(5.0));
    QCOMPARE(revTail.srcIn, drift::secondsToUs(1.0));
    QCOMPARE(revTail.srcOut, drift::secondsToUs(3.0));
    QVERIFY(drift::clipsCanMerge(rev, revTail));
}

void CoreTest::clipLinkFieldsSerialization()
{
    drift::Project project;
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Video});

    drift::Clip clip;
    clip.id = QStringLiteral("clip-v");
    clip.linkId = QStringLiteral("link-abc");
    clip.suppressEmbeddedAudio = true;
    clip.type = drift::ClipType::Video;
    clip.timelineStart = 0;
    clip.timelineDuration = drift::secondsToUs(2.0);
    project.tracks()[0].clips.append(clip);

    const QJsonObject json = project.toJson();
    QString error;
    const drift::Project loaded = drift::Project::fromJson(json, &error);
    QVERIFY(error.isEmpty());
    const drift::Clip &out = loaded.tracks()[0].clips[0];
    QCOMPARE(out.linkId, QStringLiteral("link-abc"));
    QCOMPARE(out.suppressEmbeddedAudio, true);
}

void CoreTest::maskAndTransitionSerialization()
{
    drift::Project project;
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Video});

    drift::Clip clipA;
    clipA.id = QStringLiteral("clip-a");
    clipA.type = drift::ClipType::Video;
    clipA.timelineStart = 0;
    clipA.timelineDuration = drift::secondsToUs(2.0);
    clipA.speed = 2.0;
    drift::Mask ellipse;
    ellipse.shape = drift::MaskShape::Ellipse;
    ellipse.w = 0.5;
    ellipse.feather = 4.0;
    drift::Mask hole;
    hole.shape = drift::MaskShape::Star;
    hole.op = drift::MaskOp::Subtract;
    hole.name = QStringLiteral("Punch");
    hole.enabled = false;
    clipA.masks = {ellipse, hole};

    drift::Clip clipB;
    clipB.id = QStringLiteral("clip-b");
    clipB.type = drift::ClipType::Video;
    clipB.timelineStart = drift::secondsToUs(2.0);
    clipB.timelineDuration = drift::secondsToUs(2.0);

    project.tracks()[0].clips.append(clipA);
    project.tracks()[0].clips.append(clipB);

    drift::Transition transition;
    transition.id = QStringLiteral("tr-1");
    transition.fromClipId = clipA.id;
    transition.toClipId = clipB.id;
    transition.kindId = QStringLiteral("dip");
    transition.durationUs = drift::secondsToUs(0.5);
    project.tracks()[0].transitions.append(transition);

    const QJsonObject json = project.toJson();
    QString error;
    const drift::Project loaded = drift::Project::fromJson(json, &error);

    QVERIFY(error.isEmpty());
    QCOMPARE(loaded.tracks()[0].clips[0].speed, 2.0);
    const QList<drift::Mask> &roundTripped = loaded.tracks()[0].clips[0].masks;
    QCOMPARE(roundTripped.size(), 2);
    QCOMPARE(roundTripped.at(0).shape, drift::MaskShape::Ellipse);
    QCOMPARE(roundTripped.at(0).op, drift::MaskOp::Add);
    QCOMPARE(roundTripped.at(1).shape, drift::MaskShape::Star);
    QCOMPARE(roundTripped.at(1).op, drift::MaskOp::Subtract);
    QCOMPARE(roundTripped.at(1).name, QStringLiteral("Punch"));
    QCOMPARE(roundTripped.at(1).enabled, false);
    QCOMPARE(loaded.tracks()[0].transitions.size(), 1);
    QCOMPARE(loaded.tracks()[0].transitions[0].kindId, QStringLiteral("dip"));
    QCOMPARE(loaded.tracks()[0].transitions[0].fromClipId, QStringLiteral("clip-a"));
}

// A segmentation result is only as durable as its matte reference: losing the path or the source
// offset on reload would silently slide the mask off the subject.
void CoreTest::matteMaskSerialization()
{
    drift::Project project;
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Video});

    drift::Clip clip;
    clip.id = QStringLiteral("clip-matte");
    clip.type = drift::ClipType::Video;
    clip.timelineStart = 0;
    clip.timelineDuration = drift::secondsToUs(3.0);
    drift::Mask matte;
    matte.shape = drift::MaskShape::Matte;
    matte.mattePath = QStringLiteral("/tmp/mattes/abc.mkv");
    matte.matteSrcOffsetUs = drift::secondsToUs(1.5);
    matte.invert = true;
    clip.masks = {matte};
    project.tracks()[0].clips.append(clip);

    const QJsonObject json = project.toJson();
    QString error;
    const drift::Project loaded = drift::Project::fromJson(json, &error);

    QVERIFY(error.isEmpty());
    QCOMPARE(loaded.tracks()[0].clips[0].masks.size(), 1);
    const drift::Mask &mask = loaded.tracks()[0].clips[0].masks.at(0);
    QCOMPARE(mask.shape, drift::MaskShape::Matte);
    QCOMPARE(mask.mattePath, QStringLiteral("/tmp/mattes/abc.mkv"));
    QCOMPARE(mask.matteSrcOffsetUs, drift::secondsToUs(1.5));
    QCOMPARE(mask.invert, true);
}

// Every project saved before the mask stack existed carries a single "mask" object. That
// fallback is the only migration path there is, so it has to keep working indefinitely.
void CoreTest::legacySingleMaskLoadsAsStack()
{
    drift::Project project;
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Video});

    drift::Clip clip;
    clip.id = QStringLiteral("clip-legacy");
    clip.type = drift::ClipType::Video;
    clip.timelineDuration = drift::secondsToUs(2.0);
    project.tracks()[0].clips.append(clip);

    // Rewrite the document the way the old serializer did: a "mask" object, no "masks" array.
    QJsonObject json = project.toJson();
    QJsonArray tracks = json.value(QStringLiteral("tracks")).toArray();
    QJsonObject track = tracks.at(0).toObject();
    QJsonArray clips = track.value(QStringLiteral("clips")).toArray();
    QJsonObject clipJson = clips.at(0).toObject();
    clipJson.remove(QStringLiteral("masks"));
    clipJson.insert(QStringLiteral("mask"),
                    QJsonObject{{QStringLiteral("shape"), QStringLiteral("ellipse")},
                                {QStringLiteral("x"), 0.25},
                                {QStringLiteral("feather"), 6.0},
                                {QStringLiteral("invert"), true}});
    clips.replace(0, clipJson);
    track.insert(QStringLiteral("clips"), clips);
    tracks.replace(0, track);
    json.insert(QStringLiteral("tracks"), tracks);

    QString error;
    const drift::Project loaded = drift::Project::fromJson(json, &error);
    QVERIFY(error.isEmpty());

    const QList<drift::Mask> &masks = loaded.tracks().at(0).clips.at(0).masks;
    QCOMPARE(masks.size(), 1);
    QCOMPARE(masks.at(0).shape, drift::MaskShape::Ellipse);
    QCOMPARE(masks.at(0).x, 0.25);
    QCOMPARE(masks.at(0).feather, 6.0);
    QCOMPARE(masks.at(0).invert, true);
    // Defaults the old format never stored.
    QCOMPARE(masks.at(0).op, drift::MaskOp::Add);
    QCOMPARE(masks.at(0).enabled, true);
}

// "none" was how the old format spelled "unmasked". Carrying those over would give every
// pre-existing clip a dead row in the stack list.
void CoreTest::legacyEmptyMaskLoadsAsNoStack()
{
    drift::Project project;
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Video});

    drift::Clip clip;
    clip.id = QStringLiteral("clip-unmasked");
    clip.type = drift::ClipType::Video;
    clip.timelineDuration = drift::secondsToUs(2.0);
    project.tracks()[0].clips.append(clip);

    QJsonObject json = project.toJson();
    QJsonArray tracks = json.value(QStringLiteral("tracks")).toArray();
    QJsonObject track = tracks.at(0).toObject();
    QJsonArray clips = track.value(QStringLiteral("clips")).toArray();
    QJsonObject clipJson = clips.at(0).toObject();
    clipJson.remove(QStringLiteral("masks"));
    clipJson.insert(QStringLiteral("mask"),
                    QJsonObject{{QStringLiteral("shape"), QStringLiteral("none")}});
    clips.replace(0, clipJson);
    track.insert(QStringLiteral("clips"), clips);
    tracks.replace(0, track);
    json.insert(QStringLiteral("tracks"), tracks);

    QString error;
    const drift::Project loaded = drift::Project::fromJson(json, &error);
    QVERIFY(error.isEmpty());
    QVERIFY(loaded.tracks().at(0).clips.at(0).masks.isEmpty());
}

void CoreTest::faceTrackSerialization()
{
    drift::Project project;
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Video});

    drift::Clip clip;
    clip.id = QStringLiteral("clip-face");
    clip.type = drift::ClipType::Video;
    clip.timelineStart = 0;
    clip.timelineDuration = drift::secondsToUs(3.0);
    clip.faceTrackPath = QStringLiteral("/tmp/facetracks/abc.json");
    clip.faceTrackSrcOffsetUs = drift::secondsToUs(2.25);
    project.tracks()[0].clips.append(clip);

    const QJsonObject json = project.toJson();
    QString error;
    const drift::Project loaded = drift::Project::fromJson(json, &error);

    QVERIFY(error.isEmpty());
    const drift::Clip &out = loaded.tracks()[0].clips[0];
    QCOMPARE(out.faceTrackPath, QStringLiteral("/tmp/facetracks/abc.json"));
    QCOMPARE(out.faceTrackSrcOffsetUs, drift::secondsToUs(2.25));

    // A project written before face tracking existed carries neither key, and must still load with
    // the clip simply having no track rather than failing.
    QJsonObject legacy = json;
    QJsonArray legacyTracks = legacy.value(QStringLiteral("tracks")).toArray();
    QJsonObject legacyTrack = legacyTracks.at(0).toObject();
    QJsonArray legacyClips = legacyTrack.value(QStringLiteral("clips")).toArray();
    QJsonObject legacyClip = legacyClips.at(0).toObject();
    legacyClip.remove(QStringLiteral("faceTrackPath"));
    legacyClip.remove(QStringLiteral("faceTrackSrcOffsetUs"));
    legacyClips.replace(0, legacyClip);
    legacyTrack.insert(QStringLiteral("clips"), legacyClips);
    legacyTracks.replace(0, legacyTrack);
    legacy.insert(QStringLiteral("tracks"), legacyTracks);

    const drift::Project old = drift::Project::fromJson(legacy, &error);
    QVERIFY(error.isEmpty());
    QVERIFY(old.tracks()[0].clips[0].faceTrackPath.isEmpty());
    QCOMPARE(old.tracks()[0].clips[0].faceTrackSrcOffsetUs, drift::TimeUs(0));
}

void CoreTest::emojiClipSerialization()
{
    drift::Project project;
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Video});

    drift::Clip clip;
    clip.id = QStringLiteral("clip-emoji");
    clip.type = drift::ClipType::Image;
    clip.timelineStart = 0;
    clip.timelineDuration = drift::secondsToUs(3.0);
    clip.path = QStringLiteral("/tmp/emoji/1f600.png");
    clip.emoji = QStringLiteral("\U0001F600");
    project.tracks()[0].clips.append(clip);

    const QJsonObject json = project.toJson();
    QString error;
    const drift::Project loaded = drift::Project::fromJson(json, &error);

    QVERIFY(error.isEmpty());
    // The sequence is what survives a move between machines; the cached raster path does not.
    QCOMPARE(loaded.tracks()[0].clips[0].emoji, QStringLiteral("\U0001F600"));

    // A sticker or any other image clip written before the picker existed has no key at all.
    QJsonObject legacy = json;
    QJsonArray legacyTracks = legacy.value(QStringLiteral("tracks")).toArray();
    QJsonObject legacyTrack = legacyTracks.at(0).toObject();
    QJsonArray legacyClips = legacyTrack.value(QStringLiteral("clips")).toArray();
    QJsonObject legacyClip = legacyClips.at(0).toObject();
    legacyClip.remove(QStringLiteral("emoji"));
    legacyClips.replace(0, legacyClip);
    legacyTrack.insert(QStringLiteral("clips"), legacyClips);
    legacyTracks.replace(0, legacyTrack);
    legacy.insert(QStringLiteral("tracks"), legacyTracks);

    const drift::Project old = drift::Project::fromJson(legacy, &error);
    QVERIFY(error.isEmpty());
    QVERIFY(old.tracks()[0].clips[0].emoji.isEmpty());
}

// The pre-shader enum serialized exactly these strings, so a project written by an older build
// must still resolve to the right transition package.
static drift::Project projectWithTransition(const QString &kindId,
                                            const QMap<QString, QVariant> &params = {})
{
    drift::Project project;
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Video});

    drift::Clip clipA;
    clipA.id = QStringLiteral("a");
    clipA.type = drift::ClipType::Video;
    clipA.timelineStart = 0;
    clipA.timelineDuration = drift::secondsToUs(1.0);

    drift::Clip clipB;
    clipB.id = QStringLiteral("b");
    clipB.type = drift::ClipType::Video;
    clipB.timelineStart = drift::secondsToUs(1.0);
    clipB.timelineDuration = drift::secondsToUs(1.0);

    project.tracks()[0].clips.append(clipA);
    project.tracks()[0].clips.append(clipB);

    drift::Transition transition;
    transition.id = QStringLiteral("tr");
    transition.fromClipId = clipA.id;
    transition.toClipId = clipB.id;
    transition.kindId = kindId;
    transition.parameters = params;
    project.tracks()[0].transitions.append(transition);
    return project;
}

void CoreTest::allTransitionKindsRoundTrip()
{
    const QStringList kinds = {
        QStringLiteral("crossfade"),  QStringLiteral("dip"),        QStringLiteral("dip_white"),
        QStringLiteral("wipe_left"),  QStringLiteral("wipe_right"), QStringLiteral("wipe_up"),
        QStringLiteral("wipe_down"),  QStringLiteral("push_left"),  QStringLiteral("zoom_in"),
    };

    for (const QString &kind : kinds) {
        const drift::Project project = projectWithTransition(kind);
        QString error;
        const drift::Project loaded = drift::Project::fromJson(project.toJson(), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(loaded.tracks()[0].transitions[0].kindId, kind);
    }
}

void CoreTest::transitionParametersRoundTrip()
{
    QMap<QString, QVariant> params;
    params.insert(QStringLiteral("softness"), 0.25);
    params.insert(QStringLiteral("invert"), true);

    const drift::Project project = projectWithTransition(QStringLiteral("luma_fade"), params);
    QString error;
    const drift::Project loaded = drift::Project::fromJson(project.toJson(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    const drift::Transition &t = loaded.tracks()[0].transitions[0];
    QCOMPARE(t.kindId, QStringLiteral("luma_fade"));
    QCOMPARE(t.parameters.value(QStringLiteral("softness")).toDouble(), 0.25);
    QCOMPARE(t.parameters.value(QStringLiteral("invert")).toBool(), true);
}

// A project file written before transitions became packages has no "parameters" key at all.
void CoreTest::legacyTransitionJsonStillLoads()
{
    QJsonObject legacy = projectWithTransition(QStringLiteral("wipe_up")).toJson();
    QJsonArray tracks = legacy.value(QStringLiteral("tracks")).toArray();
    QJsonObject track = tracks.at(0).toObject();
    QJsonArray transitions = track.value(QStringLiteral("transitions")).toArray();
    QJsonObject t = transitions.at(0).toObject();
    t.remove(QStringLiteral("parameters"));
    transitions.replace(0, t);
    track.insert(QStringLiteral("transitions"), transitions);
    tracks.replace(0, track);
    legacy.insert(QStringLiteral("tracks"), tracks);

    QString error;
    const drift::Project loaded = drift::Project::fromJson(legacy, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(loaded.tracks()[0].transitions[0].kindId, QStringLiteral("wipe_up"));
    QVERIFY(loaded.tracks()[0].transitions[0].parameters.isEmpty());
}

void CoreTest::transitionAudioCurves()
{
    // crossfade: linear, sums to 1 at every point.
    const auto mid = drift::transitionAudioGains(QStringLiteral("crossfade"), 0.5);
    QCOMPARE(mid.outgoing, 0.5);
    QCOMPARE(mid.incoming, 0.5);

    // dip: silent at the midpoint, matching the visual dip through black.
    const auto dip = drift::transitionAudioGains(QStringLiteral("dip"), 0.5);
    QCOMPARE(dip.outgoing, 0.0);
    QCOMPARE(dip.incoming, 0.0);
    QCOMPARE(drift::transitionAudioGains(QStringLiteral("dip"), 0.0).outgoing, 1.0);
    QCOMPARE(drift::transitionAudioGains(QStringLiteral("dip"), 1.0).incoming, 1.0);

    // hold: no ducking at all.
    const auto hold = drift::transitionAudioGains(QStringLiteral("hold"), 0.5);
    QCOMPARE(hold.outgoing, 1.0);
    QCOMPARE(hold.incoming, 1.0);
}

void CoreTest::physicalOverlapTransitionWindow()
{
    drift::Track track;
    track.type = drift::TrackType::Video;

    drift::Clip clipA;
    clipA.id = QStringLiteral("a");
    clipA.timelineStart = 0;
    clipA.timelineDuration = drift::secondsToUs(2.0);

    drift::Clip clipB;
    clipB.id = QStringLiteral("b");
    clipB.timelineStart = drift::secondsToUs(1.5);
    clipB.timelineDuration = drift::secondsToUs(2.0);

    track.clips.append(clipA);
    track.clips.append(clipB);

    QVERIFY(drift::clipsPhysicallyOverlap(clipA, clipB));
    QCOMPARE(drift::physicalOverlapDurationUs(clipA, clipB), drift::secondsToUs(0.5));

    drift::Transition transition;
    transition.fromClipId = clipA.id;
    transition.toClipId = clipB.id;
    transition.durationUs = drift::secondsToUs(1.0); // ignored when overlapping

    drift::TimeUs startUs = 0;
    drift::TimeUs endUs = 0;
    QVERIFY(drift::transitionWindow(track, transition, startUs, endUs));
    QCOMPARE(startUs, drift::secondsToUs(1.5));
    QCOMPARE(endUs, drift::secondsToUs(2.0));
}

void CoreTest::clampClipStartNoOverlapPushesPastBlockers()
{
    drift::Track track;
    track.type = drift::TrackType::Video;

    drift::Clip blocker;
    blocker.id = QStringLiteral("blocker");
    blocker.timelineStart = drift::secondsToUs(1.0);
    blocker.timelineDuration = drift::secondsToUs(2.0);
    track.clips.append(blocker);

    drift::Clip moving;
    moving.id = QStringLiteral("moving");
    moving.timelineDuration = drift::secondsToUs(1.0);

    const QSet<QString> exclude{moving.id};
    // Dropping into the blocker should land just after it.
    QCOMPARE(drift::clampClipStartNoOverlap(track, exclude, drift::secondsToUs(1.5),
                                            moving.timelineDuration),
             drift::secondsToUs(3.0));
    // Abutting the blocker is allowed.
    QCOMPARE(drift::clampClipStartNoOverlap(track, exclude, drift::secondsToUs(3.0),
                                            moving.timelineDuration),
             drift::secondsToUs(3.0));
    // Clear space before the blocker stays put.
    QCOMPARE(drift::clampClipStartNoOverlap(track, exclude, 0, moving.timelineDuration), 0);
}

void CoreTest::clampTrimEdgesIgnoreExistingOverlaps()
{
    drift::Track track;
    track.type = drift::TrackType::Video;

    drift::Clip left;
    left.id = QStringLiteral("left");
    left.timelineStart = 0;
    left.timelineDuration = drift::secondsToUs(2.0);

    drift::Clip mid;
    mid.id = QStringLiteral("mid");
    mid.timelineStart = drift::secondsToUs(1.0); // already overlaps left
    mid.timelineDuration = drift::secondsToUs(2.0);

    drift::Clip right;
    right.id = QStringLiteral("right");
    right.timelineStart = drift::secondsToUs(4.0);
    right.timelineDuration = drift::secondsToUs(1.0);

    track.clips.append(left);
    track.clips.append(mid);
    track.clips.append(right);

    const QSet<QString> excludeMid{mid.id};
    // Extending mid left must not jump past the already-overlapping left clip.
    QCOMPARE(drift::clampClipStartAgainstLeftNeighbors(track, excludeMid, mid.timelineStart,
                                                       drift::secondsToUs(0.5)),
             drift::secondsToUs(0.5));
    // Extending mid right stops at the abutting/gapped right neighbor.
    QCOMPARE(drift::clampClipEndNoOverlap(track, excludeMid, mid.timelineEnd(),
                                          drift::secondsToUs(4.5)),
             drift::secondsToUs(4.0));
}

void CoreTest::backgroundSerialization()
{
    // Default background is opaque black / Color and must survive a round-trip.
    {
        drift::Project project;
        const drift::Project loaded = drift::Project::fromJson(project.toJson());
        QCOMPARE(loaded.background().kind, drift::BackgroundKind::Color);
        QCOMPARE(loaded.background().color, QColor(Qt::black));
    }

    // Non-default (blur + color + strength) round-trips.
    {
        drift::Project project;
        drift::Background bg;
        bg.kind = drift::BackgroundKind::Blur;
        bg.color = QColor(QStringLiteral("#ff2563eb"));
        bg.blurStrength = 42.0;
        project.setBackground(bg);

        QString error;
        const drift::Project loaded = drift::Project::fromJson(project.toJson(), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(loaded.background().kind, drift::BackgroundKind::Blur);
        QCOMPARE(loaded.background().color, QColor(QStringLiteral("#ff2563eb")));
        QCOMPARE(loaded.background().blurStrength, 42.0);
    }

    // Projects saved before this field default to solid black.
    {
        const QJsonObject root{
            {QStringLiteral("version"), 3},
            {QStringLiteral("projectName"), QStringLiteral("NoBackground")},
            {QStringLiteral("fps"), 30},
            {QStringLiteral("width"), 1920},
            {QStringLiteral("height"), 1080},
            {QStringLiteral("tracks"), QJsonArray{}},
        };
        QString error;
        const drift::Project loaded = drift::Project::fromJson(root, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(loaded.background().kind, drift::BackgroundKind::Color);
        QCOMPARE(loaded.background().color, QColor(Qt::black));
    }
}

void CoreTest::fadeSerializationAndMultiplier()
{
    drift::Project project;
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Video});

    drift::Clip clip;
    clip.id = QStringLiteral("fade-clip");
    clip.type = drift::ClipType::Video;
    clip.timelineStart = drift::secondsToUs(1.0);
    clip.timelineDuration = drift::secondsToUs(4.0);
    clip.fadeInUs = drift::secondsToUs(1.0);
    clip.fadeOutUs = drift::secondsToUs(2.0);
    clip.fadeCurve = drift::FadeCurve::Linear;
    project.tracks()[0].clips.append(clip);

    QString error;
    const drift::Project loaded = drift::Project::fromJson(project.toJson(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    const drift::Clip &c = loaded.tracks()[0].clips[0];
    QCOMPARE(c.fadeInUs, drift::secondsToUs(1.0));
    QCOMPARE(c.fadeOutUs, drift::secondsToUs(2.0));
    QCOMPARE(c.fadeCurve, drift::FadeCurve::Linear);

    // Linear ramp: at the very edges gain is 0, at the fade midpoints 0.5, and
    // fully present between the fades.
    QCOMPARE(c.fadeMultiplier(c.timelineStart), 0.0);
    QVERIFY(qAbs(c.fadeMultiplier(c.timelineStart + drift::secondsToUs(0.5)) - 0.5) < 1e-6);
    QVERIFY(qAbs(c.fadeMultiplier(c.timelineStart + drift::secondsToUs(1.5)) - 1.0) < 1e-6);
    QVERIFY(qAbs(c.fadeMultiplier(c.timelineEnd() - drift::secondsToUs(1.0)) - 0.5) < 1e-6);

    // Presets must diverge early in the fade so Smooth / Natural are audible and visible.
    // (Smoothstep equals Linear at t=0.5, so sample at quarter-fade.)
    drift::Clip smooth = c;
    smooth.fadeCurve = drift::FadeCurve::Smooth;
    drift::Clip natural = c;
    natural.fadeCurve = drift::FadeCurve::EqualPower;
    const drift::TimeUs earlyIn = c.timelineStart + drift::secondsToUs(0.25);
    const double linearEarly = c.fadeMultiplier(earlyIn);
    const double smoothEarly = smooth.fadeMultiplier(earlyIn);
    const double naturalEarly = natural.fadeMultiplier(earlyIn);
    QVERIFY(smoothEarly < linearEarly - 0.05);
    QVERIFY(naturalEarly > linearEarly + 0.05);

    // Custom shape round-trips and drives the multiplier.
    drift::Clip custom = c;
    custom.fadeCurve = drift::FadeCurve::Custom;
    custom.fadeShape.setPoints({QPointF(0.0, 0.0), QPointF(0.5, 0.25), QPointF(1.0, 1.0)});
    project.tracks()[0].clips[0] = custom;
    const drift::Project customLoaded = drift::Project::fromJson(project.toJson(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    const drift::Clip &cc = customLoaded.tracks()[0].clips[0];
    QCOMPARE(cc.fadeCurve, drift::FadeCurve::Custom);
    QVERIFY(!cc.fadeShape.isEmpty());
    const drift::TimeUs midIn = c.timelineStart + drift::secondsToUs(0.5);
    QVERIFY(qAbs(cc.fadeMultiplier(midIn) - 0.25) < 1e-6);

    // A clip with no fades is always fully present.
    drift::Clip plain;
    plain.timelineStart = 0;
    plain.timelineDuration = drift::secondsToUs(2.0);
    QCOMPARE(plain.fadeMultiplier(drift::secondsToUs(1.0)), 1.0);
}

void CoreTest::clipAnimationSerializationAndSample()
{
    drift::Project project;
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Video});

    drift::Clip clip;
    clip.id = QStringLiteral("anim-clip");
    clip.type = drift::ClipType::Shape;
    clip.timelineStart = 0;
    clip.timelineDuration = drift::secondsToUs(2.0);
    clip.animIn = {drift::ClipAnimKind::Fade, drift::secondsToUs(1.0), drift::ClipAnimEase::Linear,
                   drift::FadeCurve::Linear};
    clip.animOut = {drift::ClipAnimKind::ZoomIn, drift::secondsToUs(0.5), drift::ClipAnimEase::EaseOut,
                    drift::FadeCurve::EqualPower};
    project.tracks()[0].clips.append(clip);

    QString error;
    const drift::Project loaded = drift::Project::fromJson(project.toJson(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    const drift::Clip &c = loaded.tracks()[0].clips[0];
    QCOMPARE(c.animIn.kind, drift::ClipAnimKind::Fade);
    QCOMPARE(c.animIn.durationUs, drift::secondsToUs(1.0));
    QCOMPARE(c.animIn.curve, drift::FadeCurve::Linear);
    QCOMPARE(c.animOut.kind, drift::ClipAnimKind::ZoomIn);
    QCOMPARE(c.animOut.curve, drift::FadeCurve::EqualPower);

    // Fade kind owns opacity via fadeMultiplier (not body-anim sample).
    QVERIFY(qAbs(c.fadeMultiplier(drift::secondsToUs(0.5)) - 0.5) < 1e-6);
    const drift::ClipAnimSample midIn =
        drift::evaluateClipAnimation(c.timelineStart, c.timelineDuration, c.animIn, {},
                                     drift::secondsToUs(0.5), 100.0, 100.0);
    QVERIFY(qAbs(midIn.opacity - 1.0) < 1e-6);

    drift::Clip zoom;
    zoom.timelineStart = 0;
    zoom.timelineDuration = drift::secondsToUs(2.0);
    zoom.animIn = {drift::ClipAnimKind::ZoomIn, drift::secondsToUs(1.0), drift::ClipAnimEase::Linear,
                   drift::FadeCurve::Linear};
    const drift::ClipAnimSample zoomMid =
        drift::evaluateClipAnimation(zoom.timelineStart, zoom.timelineDuration, zoom.animIn, {},
                                     drift::secondsToUs(0.5), 100.0, 100.0);
    QVERIFY(qAbs(zoomMid.scale - 0.8) < 1e-6); // 0.6 + 0.4 * 0.5
    QVERIFY(zoomMid.scale < 1.0);

    // Smooth style bends motion progress vs linear at quarter-time.
    drift::Clip smoothZoom = zoom;
    smoothZoom.animIn.curve = drift::FadeCurve::Smooth;
    const drift::ClipAnimSample smoothMid =
        drift::evaluateClipAnimation(smoothZoom.timelineStart, smoothZoom.timelineDuration,
                                     smoothZoom.animIn, {}, drift::secondsToUs(0.25), 100.0, 100.0);
    const drift::ClipAnimSample linearQuarter =
        drift::evaluateClipAnimation(zoom.timelineStart, zoom.timelineDuration, zoom.animIn, {},
                                     drift::secondsToUs(0.25), 100.0, 100.0);
    QVERIFY(smoothMid.scale < linearQuarter.scale - 0.01);
}

// A canvas resize must not move or rescale anything: clips that relied on the
// implicit full-canvas size get that size frozen, so they overflow the smaller
// frame instead of shrinking with it.
void CoreTest::rebaseClipLayoutFreezesImplicitSize()
{
    drift::Project project;
    project.setResolution(1920, 1080);

    drift::Track track;
    track.type = drift::TrackType::Video;

    drift::Clip implicitSize; // no transform keyframes at all
    implicitSize.type = drift::ClipType::Video;
    track.clips.append(implicitSize);

    drift::Clip explicitSize;
    explicitSize.type = drift::ClipType::Image;
    explicitSize.transformW.setKeyframe(0, 640.0);
    explicitSize.transformH.setKeyframe(0, 360.0);
    explicitSize.transformX.setKeyframe(0, 100.0);
    explicitSize.transformY.setKeyframe(0, 50.0);
    track.clips.append(explicitSize);

    drift::Clip audio; // audio carries no layout; must be left alone
    audio.type = drift::ClipType::Audio;
    track.clips.append(audio);

    project.tracks().clear(); // drop the default timeline; this test owns the document
    project.tracks().append(track);

    // Crop to a 1520x1080 window starting 400px in from the left.
    drift::rebaseClipLayout(project, 1920, 1080, 400.0, 0.0);
    project.setResolution(1520, 1080);

    const drift::Track &out = project.tracks().at(0);

    // The implicit clip keeps its original 1920x1080 footprint and is pushed
    // left by the crop origin, so it now overflows both sides of the frame.
    QCOMPARE(out.clips.at(0).transformW.evaluateAt(0), 1920.0);
    QCOMPARE(out.clips.at(0).transformH.evaluateAt(0), 1080.0);
    QCOMPARE(out.clips.at(0).transformX.evaluateAt(0), -400.0);
    QCOMPARE(out.clips.at(0).transformY.evaluateAt(0), 0.0);

    // Explicit sizes are untouched; only the position shifts.
    QCOMPARE(out.clips.at(1).transformW.evaluateAt(0), 640.0);
    QCOMPARE(out.clips.at(1).transformH.evaluateAt(0), 360.0);
    QCOMPARE(out.clips.at(1).transformX.evaluateAt(0), -300.0);
    QCOMPARE(out.clips.at(1).transformY.evaluateAt(0), 50.0);

    QVERIFY(out.clips.at(2).transformW.isEmpty());
    QVERIFY(out.clips.at(2).transformX.isEmpty());
}

// Animated positions must shift wholesale, so the motion path is preserved
// relative to the content rather than being flattened to one value.
void CoreTest::rebaseClipLayoutShiftsKeyframedPosition()
{
    drift::Project project;
    project.setResolution(1920, 1080);

    drift::Track track;
    track.type = drift::TrackType::Video;

    drift::Clip clip;
    clip.type = drift::ClipType::Video;
    clip.transformX.setKeyframe(0, 0.0);
    clip.transformX.setKeyframe(drift::secondsToUs(2.0), 800.0);
    clip.transformY.setKeyframe(0, 200.0);
    track.clips.append(clip);

    project.tracks().clear(); // drop the default timeline; this test owns the document
    project.tracks().append(track);

    drift::rebaseClipLayout(project, 1920, 1080, 120.0, 60.0);

    const drift::Clip &out = project.tracks().at(0).clips.at(0);
    QCOMPARE(out.transformX.keyframes().size(), 2);
    QCOMPARE(out.transformX.evaluateAt(0), -120.0);
    QCOMPARE(out.transformX.evaluateAt(drift::secondsToUs(2.0)), 680.0);
    QCOMPARE(out.transformY.evaluateAt(0), 140.0);
}

QTEST_MAIN(CoreTest)
#include "tst_core.moc"
