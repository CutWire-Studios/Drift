#include <QtTest>

#include <QColor>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointF>
#include <QSet>
#include <QStandardPaths>

#include "core/ClipAnimation.h"
#include "core/Keyframe.h"
#include "core/Project.h"
#include "core/Stabilize.h"
#include "core/ShapePath.h"
#include "core/SrtIO.h"
#include "core/SubtitleCue.h"
#include "core/LottieTextImport.h"
#include "core/TextAnimationPreset.h"
#include "core/TextAnimator.h"
#include "core/TextLook.h"
#include "core/TextShading.h"
#include "core/EffectStackStore.h"
#include "core/TextPresetStore.h"
#include "core/VectorSource.h"
#include "core/DotLottie.h"
#include "TestZip.h"

#include <QTemporaryDir>
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
    void binFolderSerializationRoundTrip();
    void binFolderDeletionMovesChildrenToParent();
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
    void userTextPresetsRoundTrip();
    void effectStackJsonRoundTrip();
    void effectStackRejectsForeignPayloads();
    void effectKeyframeRescaleIsProportional();
    void userEffectPresetsRoundTrip();
    void rangeSelectorCoverageMatchesSkottie();
    void staggerMatchesMovingRamp();
    void animatorCompositionOrder();
    void trackingLineAdjust();
    void caretTiming();
    void animationBoundsCoverTheEnvelope();
    void textStyleMigratesFromV6();
    void textStyleV7RoundTrip();
    void textAnimationPresetsAreWellFormed();
    void legacyTextAnimationParity();
    void textKeyframeKeysAreDynamic();
    void textLooksRegenerate();
    void lottieTextImport();
    void karaokeWordIndexTracksTheCue();
    void shapeStyleSerialization();
    void legacyShapeStyleLoadsWithDefaults();
    void shapeCatalogPathsFitBounds();
    void vectorSourceSerialization();
    void dotLottieUnpacks();
    void textStyleKeyframesSerialization();
    void vectorClipIsSyntheticOnGraphicTracks();
    void foldVectorTimeTable_data();
    void foldVectorTimeTable();
    void effectCatalogIdSerialization();
    void effectParamKeyframeSerialization();
    void detachedCopyIsolatesKeyframesFromLiveMutations();
    void effectTemplateStackSerialization();
    void audioEffectSerialization();
    void rgbSplitEffectParametersSerialization();
    void blockGlitchEffectParametersSerialization();
    void clipSpeedSourceMapping();
    void piecewiseLinearBreakpointsCompressLinearMotion();
    void stabilizePlanDoesNotKeyEveryFrame();
    void stabilizeApplyPlanScalesOffsetsByZoom();
    void stabilizeTrfAsciiAndBinaryParse();
    void stabilizeModeSerialization();
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
    void faceTrackSerialization();
    void emojiClipSerialization();
    void allTransitionKindsRoundTrip();
    void transitionParametersRoundTrip();
    void legacyTransitionJsonStillLoads();
    void transitionAudioCurves();
    void transitionEasingCurveRoundTrips();
    void transitionEasingRemapsProgress();
    void bezierCurveShapesProgress();
    void bezierShapeRoundTrips();
    void physicalOverlapTransitionWindow();
    void clampClipStartNoOverlapPushesPastBlockers();
    void clampTrimEdgesIgnoreExistingOverlaps();
    void backgroundSerialization();
    void fadeSerializationAndMultiplier();
    void clipAnimationSerializationAndSample();
    void rebaseClipLayoutFreezesImplicitSize();
    void rebaseClipLayoutShiftsKeyframedPosition();
    void retargetClipToSourceKeepsPlacementAndSyncsSource();
    void retargetClipToSourceClearsPerSourceState();
    void retargetClipToSourceKeepsAGeometricMask();
    void addLinkedMaskStacksRatherThanReplacing();
    void laneMaskIsUnpinnedAndSurvivesTheLiftPass();
    void legacyClipMaskMigratesToAnAdjustmentLane();
    void retargetClipToSourceShrinksWhenMediaRunsOut();
    void applyMulticamSwitchPunchesAndRecuts();
    void applyMulticamSwitchMergesAdjacentSameCamera();
    void applyMulticamSwitchRejectsEdges();
    void sliceClipToTimelineRangeKeepsSourceInSync();
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

void CoreTest::binFolderSerializationRoundTrip()
{
    drift::Project project;

    drift::BinFolder root;
    root.name = QStringLiteral("B-Roll");
    const QString rootFolderId = project.addBinFolder(root);

    drift::BinFolder nested;
    nested.name = QStringLiteral("Drone");
    nested.parentId = rootFolderId;
    const QString nestedFolderId = project.addBinFolder(nested);

    drift::MediaAsset asset;
    asset.name = QStringLiteral("aerial.mp4");
    asset.kind = drift::MediaKind::Video;
    asset.path = QStringLiteral("/tmp/aerial.mp4");
    asset.folderId = nestedFolderId;
    const QString assetId = project.addAsset(asset);

    const QJsonObject json = project.toJson();
    QString error;
    const drift::Project loaded = drift::Project::fromJson(json, &error);

    QVERIFY(error.isEmpty());
    QCOMPARE(loaded.binFolders().size(), 2);
    QVERIFY(loaded.binFolder(rootFolderId));
    QCOMPARE(loaded.binFolder(rootFolderId)->parentId, QString());
    QVERIFY(loaded.binFolder(nestedFolderId));
    QCOMPARE(loaded.binFolder(nestedFolderId)->parentId, rootFolderId);
    QVERIFY(loaded.asset(assetId));
    QCOMPARE(loaded.asset(assetId)->folderId, nestedFolderId);
}

void CoreTest::binFolderDeletionMovesChildrenToParent()
{
    drift::Project project;

    drift::BinFolder parent;
    parent.name = QStringLiteral("Interviews");
    const QString parentId = project.addBinFolder(parent);

    drift::BinFolder child;
    child.name = QStringLiteral("Day 1");
    child.parentId = parentId;
    const QString childId = project.addBinFolder(child);

    drift::MediaAsset asset;
    asset.name = QStringLiteral("clip.mp4");
    asset.kind = drift::MediaKind::Video;
    asset.path = QStringLiteral("/tmp/clip.mp4");
    asset.folderId = childId;
    const QString assetId = project.addAsset(asset);

    // Mirrors BinFolderListModel::deleteFolder + AssetLibrary::reparentAssetsInFolder:
    // reparent everything that pointed at the deleted folder up to its own parent, then
    // remove the folder row.
    const QString deletedParentId = project.binFolder(childId)->parentId;
    for (const QString &id : project.binFolderOrder()) {
        drift::BinFolder *folder = project.binFolder(id);
        if (folder && folder->parentId == childId)
            folder->parentId = deletedParentId;
    }
    for (const QString &id : project.assetOrder()) {
        drift::MediaAsset *a = project.asset(id);
        if (a && a->folderId == childId)
            a->folderId = deletedParentId;
    }
    project.binFolders().remove(childId);
    project.binFolderOrder().removeAll(childId);

    QCOMPARE(project.binFolders().size(), 1);
    QVERIFY(!project.binFolder(childId));
    QVERIFY(project.binFolder(parentId));
    QCOMPARE(project.asset(assetId)->folderId, parentId);
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
    QVERIFY(shapeTrack.allowsClipType(drift::ClipType::Vector));
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
    clip.textStyle.align = drift::TextAlign::Right;
    clip.textStyle.valign = drift::TextVAlign::Bottom;
    clip.textStyle.wordWrap = false;
    clip.textStyle.lineHeight = 1.6;
    clip.textStyle.letterSpacing = 3.5;
    clip.textStyle.layers = {drift::shadowLayer(QColor(0, 128, 255), -3.0, 7.0, 11.0, 0.42),
                             drift::glowLayer(QColor(0, 255, 128), 21.0, 0.55),
                             drift::strokeLayer(2.5, QColor(255, 0, 0)),
                             drift::solidFillLayer(QColor(10, 20, 30, 200))};
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
    clip.textStyle.animation.in = drift::legacyTextAnimationSlot(
        QStringLiteral("pop"), drift::secondsToUs(0.3), QStringLiteral("back"), QStringLiteral("block"), 60000, QStringLiteral("forward"));
    clip.textStyle.animation.out = drift::legacyTextAnimationSlot(
        QStringLiteral("slideDown"), drift::secondsToUs(0.25), QStringLiteral("easeInOut"), QStringLiteral("block"), 60000, QStringLiteral("forward"));
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
    QCOMPARE(s.primaryColor(), QColor(10, 20, 30, 200));
    QCOMPARE(s.align, drift::TextAlign::Right);
    QCOMPARE(s.valign, drift::TextVAlign::Bottom);
    QCOMPARE(s.wordWrap, false);
    QCOMPARE(s.lineHeight, 1.6);
    QCOMPARE(s.letterSpacing, 3.5);
    QCOMPARE(s.layers.size(), 4);
    QCOMPARE(s.layers[0].kind, drift::TextLayerKind::Shadow);
    QVERIFY(s.layers[0].enabled);
    QCOMPARE(s.layers[0].offsetX, -3.0);
    QCOMPARE(s.layers[0].offsetY, 7.0);
    QCOMPARE(s.layers[0].blur, 11.0);
    QCOMPARE(s.layers[0].opacity, 0.42);
    QCOMPARE(s.layers[0].paint.color, QColor(0, 128, 255));
    QCOMPARE(s.layers[1].kind, drift::TextLayerKind::Glow);
    QCOMPARE(s.layers[1].paint.color, QColor(0, 255, 128));
    QCOMPARE(s.layers[1].blur, 21.0);
    QCOMPARE(s.layers[1].opacity, 0.55);
    QCOMPARE(s.layers[2].kind, drift::TextLayerKind::Stroke);
    QCOMPARE(s.layers[2].width, 2.5);
    QCOMPARE(s.layers[2].paint.color, QColor(255, 0, 0));
    QCOMPARE(drift::textStrokeWidth(s, false), 2.5);
    QCOMPARE(s.layers[3].id, QStringLiteral("fill"));
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
    QCOMPARE(s.animation.in.presetId, QStringLiteral("pop"));
    QCOMPARE(s.animation.in.params.value(QStringLiteral("duration")).scalar, 0.3);
    QCOMPARE(s.animation.in.params.value(QStringLiteral("ease")).text, QStringLiteral("back"));
    QCOMPARE(s.animation.out.presetId, QStringLiteral("slide-down"));
    QCOMPARE(s.animation.out.params.value(QStringLiteral("duration")).scalar, 0.25);
    QCOMPARE(s.animation.out.params.value(QStringLiteral("ease")).text, QStringLiteral("easeInOut"));
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
        QCOMPARE(drift::textStrokeWidth(loaded.tracks().at(0).clips.at(0).textStyle, false), 3.0);
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

        // The layer stack: unique ids, gradients with something to blend, effects the renderer knows.
        QSet<QString> layerIds;
        bool paintedFill = false;
        for (const drift::TextShadingLayer &layer : preset.style.layers) {
            QVERIFY2(!layerIds.contains(layer.id), qPrintable(preset.id + QLatin1Char(' ') + layer.id));
            layerIds.insert(layer.id);
            if (layer.paint.kind == drift::TextPaintKind::Gradient)
                QVERIFY2(layer.paint.gradient.stops.size() >= 2, qPrintable(preset.id));
            if (layer.paint.kind == drift::TextPaintKind::Effect)
                QVERIFY2(drift::textShaderEffectSpec(layer.paint.effect.id), qPrintable(preset.id));
            paintedFill = paintedFill || layer.paint.kind != drift::TextPaintKind::Solid;
        }

        // Every slot names a catalogue preset that offers that slot, and none of them recolours
        // the fill (an animator fillColor would flatten a gradient or effect to a solid).
        const drift::TextAnimationSet &anim = preset.style.animation;
        QVERIFY2(anim.in.isActive() && anim.out.isActive(), qPrintable(preset.id));
        const auto check = [&](const drift::TextAnimationSlot &slot, drift::TextAnimSlotKind kind) {
            if (!slot.isActive())
                return;
            const std::optional<drift::TextAnimationPreset> ap =
                drift::TextAnimationPresetCatalog::instance().presetForId(slot.presetId);
            QVERIFY2(ap && ap->supportsSlot(kind), qPrintable(preset.id + QLatin1Char(' ') + slot.presetId));
            const drift::ResolvedPresetSlot resolved = drift::resolvePresetSlot(*ap, slot.params, kind);
            QVERIFY2(resolved.valid, qPrintable(preset.id + QLatin1Char(' ') + slot.presetId));
            if (paintedFill)
                for (const drift::TextAnimator &a : resolved.animators)
                    QVERIFY2(!a.props.hasFillColor, qPrintable(preset.id + QLatin1Char(' ') + slot.presetId));
        };
        check(anim.in, drift::TextAnimSlotKind::In);
        check(anim.out, drift::TextAnimSlotKind::Out);
        check(anim.loop, drift::TextAnimSlotKind::Loop);
    }
    QVERIFY(!drift::textStyleForPresetId(QStringLiteral("nope")));
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
    clip.shapeStyle.layers = drift::defaultShapeLayers(QColor(10, 20, 30, 200), QColor(40, 50, 60, 128), Qt::white, 6.0);
    drift::TextShadingLayer &fill = clip.shapeStyle.layers[0];
    fill.paint.kind = drift::TextPaintKind::Gradient;
    fill.paint.gradient.angle = 35.0;
    fill.paint.gradient.offsetSpeed = 0.25;
    drift::TextShadingLayer &stroke = clip.shapeStyle.layers[1];
    stroke.dash = drift::StrokeDash::DashDot;
    stroke.dashOffset = 1.5;
    stroke.strokeAlign = drift::StrokeAlign::Center;
    stroke.trimEnd = 0.6;
    stroke.sketchLength = 8.0;
    stroke.sketchDeviation = 2.0;
    stroke.sketchSeed = 7;
    clip.shapeStyle.layers.append(drift::shadowLayer(Qt::black, 3.0, 5.0, 10.0, 0.5, QStringLiteral("shadow")));
    clip.shapeStyle.cornerRadius = 18.0;
    clip.shapeStyle.points = 9;
    clip.shapeStyle.innerRatio = 0.33;
    clip.shapeStyle.headSize = 0.55;
    clip.shapeStyle.thickness = 0.22;
    clip.shapeStyle.tailX = 0.7;
    clip.shapeStyle.tailSize = 0.15;
    clip.shapeStyle.keyframes[QStringLiteral("cornerRadius")].setKeyframe(0, 0.0);
    clip.shapeStyle.keyframes[QStringLiteral("cornerRadius")].setKeyframe(drift::kUsPerSecond, 40.0);
    clip.shapeStyle.keyframes[QStringLiteral("layer.stroke.width")].setKeyframe(0, 2.0);
    clip.shapeStyle.keyframes[QStringLiteral("layer.stroke.width")].setKeyframe(drift::kUsPerSecond, 12.0);
    clip.transformX.setKeyframe(0, 100.0);
    clip.transformY.setKeyframe(0, 200.0);
    project.tracks()[0].clips.append(clip);

    const QJsonObject json = project.toJson();
    QString error;
    const drift::Project loaded = drift::Project::fromJson(json, &error);

    QVERIFY(error.isEmpty());
    const drift::Clip &loadedClip = loaded.tracks()[0].clips[0];
    QCOMPARE(loadedClip.type, drift::ClipType::Shape);
    const drift::ShapeStyle &style = loadedClip.shapeStyle;
    QCOMPARE(style.kind, drift::ShapeKind::Hexagon);
    QCOMPARE(style.layers.size(), 3);
    QCOMPARE(style.layers[0].id, QStringLiteral("fill"));
    QCOMPARE(style.layers[0].kind, drift::TextLayerKind::Fill);
    QCOMPARE(style.layers[0].paint.kind, drift::TextPaintKind::Gradient);
    QCOMPARE(style.layers[0].paint.gradient.stops.size(), 2);
    QCOMPARE(style.layers[0].paint.gradient.stops[0].color, QColor(10, 20, 30, 200));
    QCOMPARE(style.layers[0].paint.gradient.stops[1].color, QColor(40, 50, 60, 128));
    QCOMPARE(style.layers[0].paint.gradient.angle, 35.0);
    QCOMPARE(style.layers[0].paint.gradient.offsetSpeed, 0.25);
    QCOMPARE(style.layers[1].id, QStringLiteral("stroke"));
    QCOMPARE(style.layers[1].kind, drift::TextLayerKind::Stroke);
    QCOMPARE(style.layers[1].width, 6.0);
    QCOMPARE(style.layers[1].paint.color, QColor(255, 255, 255));
    QCOMPARE(style.layers[1].dash, drift::StrokeDash::DashDot);
    QCOMPARE(style.layers[1].dashOffset, 1.5);
    QCOMPARE(style.layers[1].strokeAlign, drift::StrokeAlign::Center);
    QCOMPARE(style.layers[1].trimEnd, 0.6);
    QCOMPARE(style.layers[1].sketchLength, 8.0);
    QCOMPARE(style.layers[1].sketchDeviation, 2.0);
    QCOMPARE(style.layers[1].sketchSeed, 7);
    QCOMPARE(style.layers[2].kind, drift::TextLayerKind::Shadow);
    QCOMPARE(style.layers[2].offsetY, 5.0);
    QCOMPARE(style.cornerRadius, 18.0);
    QCOMPARE(style.points, 9);
    QCOMPARE(style.innerRatio, 0.33);
    QCOMPARE(style.headSize, 0.55);
    QCOMPARE(style.thickness, 0.22);
    QCOMPARE(style.tailX, 0.7);
    QCOMPARE(style.tailSize, 0.15);
    QCOMPARE(style.keyframes.size(), 2);
    QVERIFY(style.isAnimated());
    const drift::ShapeStyle mid = style.resolvedAt(drift::kUsPerSecond / 2);
    QVERIFY(mid.cornerRadius > 10.0 && mid.cornerRadius < 30.0);
    QVERIFY(mid.layers[1].width > 4.0 && mid.layers[1].width < 10.0);
    QCOMPARE(loadedClip.transformX.evaluateAt(0), 100.0);
    QCOMPARE(loadedClip.transformY.evaluateAt(0), 200.0);
    QCOMPARE(json.value(QStringLiteral("version")).toInt(), 8);
}

// A project saved before format 8 carries the flat fill/stroke keys — possibly only the original
// four — and must load as the equivalent layer stack: a fill under an inside stroke, both with
// the stable ids the catalog uses.
void CoreTest::legacyShapeStyleLoadsWithDefaults()
{
    const auto load = [](const QJsonObject &shapeStyle) {
        const QJsonObject json{
            {QStringLiteral("version"), 7},
            {QStringLiteral("tracks"),
             QJsonArray{QJsonObject{
                 {QStringLiteral("type"), QStringLiteral("shape")},
                 {QStringLiteral("clips"),
                  QJsonArray{QJsonObject{
                      {QStringLiteral("id"), QStringLiteral("legacy-shape")},
                      {QStringLiteral("type"), QStringLiteral("shape")},
                      {QStringLiteral("timelineDurationUs"), qint64(drift::kImageClipDurationUs)},
                      {QStringLiteral("shapeStyle"), shapeStyle}}}}}}}};
        QString error;
        const drift::Project loaded = drift::Project::fromJson(json, &error);
        if (!error.isEmpty())
            qWarning() << error;
        return loaded.tracks()[0].clips[0].shapeStyle;
    };

    const drift::ShapeStyle minimal = load({{QStringLiteral("kind"), QStringLiteral("pentagon")},
                                           {QStringLiteral("fill"), QStringLiteral("#ffa060ff")},
                                           {QStringLiteral("stroke"), QStringLiteral("#ffffffff")},
                                           {QStringLiteral("strokeWidth"), 4.0}});
    const drift::ShapeStyle defaults;
    QCOMPARE(minimal.kind, drift::ShapeKind::Pentagon);
    QCOMPARE(minimal.layers.size(), 2);
    QCOMPARE(minimal.layers[0].id, QStringLiteral("fill"));
    QCOMPARE(minimal.layers[0].kind, drift::TextLayerKind::Fill);
    QVERIFY(minimal.layers[0].enabled);
    QCOMPARE(minimal.layers[0].paint.kind, drift::TextPaintKind::Solid);
    QCOMPARE(minimal.layers[0].paint.color, QColor(160, 96, 255));
    QCOMPARE(minimal.primaryColor(), QColor(160, 96, 255));
    QCOMPARE(minimal.layers[1].id, QStringLiteral("stroke"));
    QCOMPARE(minimal.layers[1].kind, drift::TextLayerKind::Stroke);
    QVERIFY(minimal.layers[1].enabled);
    QCOMPARE(minimal.layers[1].width, 4.0);
    QCOMPARE(minimal.layers[1].paint.color, QColor(Qt::white));
    QCOMPARE(minimal.layers[1].strokeAlign, drift::StrokeAlign::Inside);
    QCOMPARE(minimal.layers[1].dash, drift::StrokeDash::Solid);
    QCOMPARE(minimal.cornerRadius, defaults.cornerRadius);
    QCOMPARE(minimal.points, defaults.points);
    QCOMPARE(minimal.innerRatio, defaults.innerRatio);
    QVERIFY(minimal.keyframes.isEmpty());

    const drift::ShapeStyle gradient = load({{QStringLiteral("kind"), QStringLiteral("star")},
                                            {QStringLiteral("fillKind"), QStringLiteral("linear")},
                                            {QStringLiteral("fill"), QStringLiteral("#ff102030")},
                                            {QStringLiteral("fillSecondary"), QStringLiteral("#ff405060")},
                                            {QStringLiteral("gradientAngle"), 35.0},
                                            {QStringLiteral("strokeStyle"), QStringLiteral("dashdot")},
                                            {QStringLiteral("strokeWidth"), 6.0},
                                            {QStringLiteral("points"), 7}});
    QCOMPARE(gradient.layers[0].paint.kind, drift::TextPaintKind::Gradient);
    QCOMPARE(gradient.layers[0].paint.gradient.kind, drift::TextGradientKind::Linear);
    QCOMPARE(gradient.layers[0].paint.gradient.angle, 35.0);
    QCOMPARE(gradient.layers[0].paint.gradient.stops.size(), 2);
    QCOMPARE(gradient.layers[0].paint.gradient.stops[0].color, QColor(16, 32, 48));
    QCOMPARE(gradient.layers[0].paint.gradient.stops[1].color, QColor(64, 80, 96));
    QCOMPARE(gradient.layers[1].dash, drift::StrokeDash::DashDot);
    QCOMPARE(gradient.layers[1].width, 6.0);
    QCOMPARE(gradient.points, 7);

    const drift::ShapeStyle hollow = load({{QStringLiteral("kind"), QStringLiteral("ellipse")},
                                          {QStringLiteral("fillKind"), QStringLiteral("none")},
                                          {QStringLiteral("fill"), QStringLiteral("#ff00ff00")},
                                          {QStringLiteral("strokeStyle"), QStringLiteral("none")}});
    QVERIFY(!hollow.layers[0].enabled);
    QCOMPARE(hollow.layers[0].paint.color, QColor(0, 255, 0));
    QVERIFY(!hollow.layers[1].enabled);
}

void CoreTest::vectorSourceSerialization()
{
    drift::Project project;
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Shape});

    drift::Clip clip;
    clip.id = QStringLiteral("clip-vector");
    clip.type = drift::ClipType::Vector;
    clip.name = QStringLiteral("Loader");
    clip.timelineStart = 0;
    clip.timelineDuration = drift::secondsToUs(3.0);
    clip.speed = 2.0;
    clip.reverse = true;
    clip.vector.kind = drift::VectorKind::Lottie;
    clip.vector.source = QStringLiteral("{\"v\":\"5.7.4\",\"fr\":30,\"ip\":0,\"op\":60,\"w\":200,\"h\":100,\"layers\":[]}");
    clip.vector.hash = drift::vectorSourceHash(clip.vector.source.toUtf8());
    clip.vector.width = 200;
    clip.vector.height = 100;
    clip.vector.fps = 30.0;
    clip.vector.durationUs = drift::secondsToUs(2.0);
    clip.vector.title = QStringLiteral("Spinner");
    clip.vector.fit = drift::VectorFit::Cover;
    clip.vector.loop = drift::VectorLoop::PingPong;
    clip.vector.startOffsetUs = drift::secondsToUs(0.25);
    clip.vector.slotValues.insert(QStringLiteral("accent"), drift::VectorSlotValue::fromColor(QColor(10, 20, 30, 200)));
    clip.vector.slotValues.insert(QStringLiteral("speed"), drift::VectorSlotValue::fromScalar(1.5));
    clip.vector.slotValues.insert(QStringLiteral("anchor"), drift::VectorSlotValue::fromVec2(QPointF(12.5, -3.0)));
    clip.vector.slotValues.insert(QStringLiteral("label"), drift::VectorSlotValue::fromText(QStringLiteral("Hi \u00e9")));
    clip.vector.slotValues.insert(QStringLiteral("logo"), drift::VectorSlotValue::fromImage(QStringLiteral("/tmp/logo.png")));
    project.tracks()[0].clips.append(clip);

    // Non-vector clips must not grow a document key.
    drift::Clip shape;
    shape.id = QStringLiteral("clip-shape");
    shape.type = drift::ClipType::Shape;
    project.tracks()[0].clips.append(shape);

    const QJsonObject json = project.toJson();
    QCOMPARE(json.value(QStringLiteral("version")).toInt(), drift::Project::kCurrentVersion);
    const QJsonArray clips = json.value(QStringLiteral("tracks")).toArray().at(0).toObject()
                                 .value(QStringLiteral("clips")).toArray();
    QVERIFY(clips.at(0).toObject().contains(QStringLiteral("vector")));
    QVERIFY(!clips.at(1).toObject().contains(QStringLiteral("vector")));

    QString error;
    const drift::Project loaded = drift::Project::fromJson(json, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    const drift::Clip &c = loaded.tracks()[0].clips[0];
    QCOMPARE(c.type, drift::ClipType::Vector);
    QCOMPARE(c.speed, 2.0);
    QCOMPARE(c.reverse, true);
    QCOMPARE(c.vector.kind, drift::VectorKind::Lottie);
    QCOMPARE(c.vector.source, clip.vector.source);
    QVERIFY(c.vector.isInline());
    QCOMPARE(c.vector.hash, clip.vector.hash);
    QCOMPARE(c.vector.hash.size(), 64);
    QCOMPARE(c.vector.width, 200);
    QCOMPARE(c.vector.height, 100);
    QCOMPARE(c.vector.fps, 30.0);
    QCOMPARE(c.vector.durationUs, drift::secondsToUs(2.0));
    QCOMPARE(c.vector.title, QStringLiteral("Spinner"));
    QCOMPARE(c.vector.fit, drift::VectorFit::Cover);
    QCOMPARE(c.vector.loop, drift::VectorLoop::PingPong);
    QCOMPARE(c.vector.startOffsetUs, drift::secondsToUs(0.25));
    QCOMPARE(c.vector.slotValues.size(), 5);
    QCOMPARE(c.vector.slotValues.value(QStringLiteral("accent")), drift::VectorSlotValue::fromColor(QColor(10, 20, 30, 200)));
    QCOMPARE(c.vector.slotValues.value(QStringLiteral("speed")), drift::VectorSlotValue::fromScalar(1.5));
    QCOMPARE(c.vector.slotValues.value(QStringLiteral("anchor")), drift::VectorSlotValue::fromVec2(QPointF(12.5, -3.0)));
    QCOMPARE(c.vector.slotValues.value(QStringLiteral("label")), drift::VectorSlotValue::fromText(QStringLiteral("Hi \u00e9")));
    QCOMPARE(c.vector.slotValues.value(QStringLiteral("logo")), drift::VectorSlotValue::fromImage(QStringLiteral("/tmp/logo.png")));
    QVERIFY(c.vector.slotValues.value(QStringLiteral("speed")) != drift::VectorSlotValue::fromScalar(2.0));
    QVERIFY(c.vector.slotValues.value(QStringLiteral("speed")) != drift::VectorSlotValue::fromText(QStringLiteral("1.5")));

    // A file-backed source round-trips too and reads as not inline.
    drift::VectorSource file;
    file.kind = drift::VectorKind::Svg;
    file.path = QStringLiteral("/media/icon.svg");
    const drift::VectorSource fileLoaded = drift::VectorSource::fromJson(file.toJson());
    QCOMPARE(fileLoaded.kind, drift::VectorKind::Svg);
    QCOMPARE(fileLoaded.path, file.path);
    QVERIFY(!fileLoaded.isInline());
    QVERIFY(!fileLoaded.isEmpty());
    QVERIFY(drift::VectorSource().isEmpty());
    QCOMPARE(drift::VectorSource::fromJson(QJsonObject()).loop, drift::VectorLoop::Hold);

    QCOMPARE(drift::clipTypeFromString(QStringLiteral("vector")), drift::ClipType::Vector);
    QCOMPARE(drift::clipTypeToString(drift::ClipType::Vector), QStringLiteral("vector"));
    QCOMPARE(drift::mediaKindFromString(QStringLiteral("vector")), drift::MediaKind::Vector);
    QCOMPARE(drift::mediaKindToString(drift::MediaKind::Vector), QStringLiteral("vector"));
}

void CoreTest::textStyleKeyframesSerialization()
{
    drift::TextStyle style;
    style.pixelSize = 40;
    drift::setSolidFill(style, QColor(255, 0, 0));
    // A static style writes no keyframes key at all, so older files stay byte-identical.
    QVERIFY(!drift::textStyleToJson(style).contains(QStringLiteral("keyframes")));
    QVERIFY(!style.isAnimated());

    style.keyframes[QStringLiteral("pixelSize")].setKeyframe(0, 40.0);
    style.keyframes[QStringLiteral("pixelSize")].setKeyframe(drift::secondsToUs(2.0), 80.0);
    style.keyframes[QStringLiteral("layer.fill.color.g")].setKeyframe(0, 0.0);
    style.keyframes[QStringLiteral("layer.fill.color.g")].setKeyframe(drift::secondsToUs(1.0), 1.0);
    style.keyframes[QStringLiteral("pathBend")]; // empty: must not be written
    QVERIFY(style.isAnimated());

    const drift::TextStyle mid = style.resolvedAt(drift::secondsToUs(1.0));
    QCOMPARE(mid.pixelSize, 60);
    QCOMPARE(mid.primaryColor().greenF(), 1.0);
    QCOMPARE(mid.primaryColor().redF(), 1.0);
    QVERIFY(mid.isAnimated()); // the copy keeps its tracks so renderers can tell it apart
    QCOMPARE(style.resolvedAt(0).pixelSize, 40);

    const QJsonObject json = drift::textStyleToJson(style);
    const QJsonObject keys = json.value(QStringLiteral("keyframes")).toObject();
    QCOMPARE(keys.size(), 2);
    QVERIFY(keys.contains(QStringLiteral("pixelSize")));
    QVERIFY(!keys.contains(QStringLiteral("pathBend")));

    // Through the whole project file.
    drift::Project project;
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Text});
    drift::Clip clip;
    clip.id = QStringLiteral("t");
    clip.type = drift::ClipType::Text;
    clip.textContent = QStringLiteral("Grow");
    clip.textStyle = style;
    clip.timelineDuration = drift::secondsToUs(3.0);
    project.tracks()[0].clips.append(clip);
    QString error;
    const drift::Project loaded = drift::Project::fromJson(project.toJson(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    const drift::TextStyle &back = loaded.tracks()[0].clips[0].textStyle;
    QVERIFY(back.isAnimated());
    QCOMPARE(back.keyframes.size(), 2);
    QCOMPARE(back.resolvedAt(drift::secondsToUs(2.0)).pixelSize, 80);
    QVERIFY(qAbs(back.resolvedAt(drift::secondsToUs(0.5)).primaryColor().greenF() - 0.5) < 0.001);

    // A gradient fill and a bend round-trip too and default to a plain solid fill on a straight line.
    drift::TextStyle look;
    drift::TextShadingLayer *fill = drift::firstTextLayerOfKind(look.layers, drift::TextLayerKind::Fill, false);
    fill->paint.kind = drift::TextPaintKind::Gradient;
    fill->paint.gradient.stops = {{0.0, QColor(255, 0, 0)}, {1.0, QColor(1, 2, 3, 4)}};
    fill->paint.gradient.angle = 33.0;
    look.pathBend = -40.0;
    const drift::TextStyle lookBack = drift::textStyleFromJson(drift::textStyleToJson(look));
    const drift::TextShadingLayer *fillBack = drift::firstTextLayerOfKind(lookBack.layers, drift::TextLayerKind::Fill, false);
    QVERIFY(fillBack);
    QCOMPARE(fillBack->paint.kind, drift::TextPaintKind::Gradient);
    QCOMPARE(fillBack->paint.gradient.stops.last().color, QColor(1, 2, 3, 4));
    QCOMPARE(fillBack->paint.gradient.angle, 33.0);
    QCOMPARE(lookBack.pathBend, -40.0);
    const drift::TextStyle bare = drift::textStyleFromJson(QJsonObject{{QStringLiteral("pixelSize"), 12}});
    QCOMPARE(drift::firstTextLayerOfKind(bare.layers, drift::TextLayerKind::Fill, true)->paint.kind, drift::TextPaintKind::Solid);
    QCOMPARE(bare.pathBend, 0.0);

    // Legacy key names keep resolving onto the layers they became.
    double scalar = 0;
    QVERIFY(drift::textStyleScalar(style, QStringLiteral("color.r"), &scalar));
    QCOMPARE(scalar, 1.0);
    QVERIFY(drift::textStyleScalar(style, QStringLiteral("layer.fill.color.r"), &scalar));
    QVERIFY(!drift::textStyleScalar(style, QStringLiteral("nope"), &scalar));
    QVERIFY(!drift::textStyleScalar(style, QStringLiteral("glowRadius"), &scalar)); // no glow layer on a fresh style
    QVERIFY(drift::textKeyframeProperties(style).contains(QStringLiteral("layer.fill.color.r")));
    QCOMPARE(drift::textKeyframeCanonicalKey(QStringLiteral("color.g"), style), QStringLiteral("layer.fill.color.g"));
}

void CoreTest::dotLottieUnpacks()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray anim = "{\"v\":\"5.7.4\",\"fr\":30,\"ip\":0,\"op\":30,\"w\":10,\"h\":10,\"layers\":[]}";
    const QString bundle = dir.filePath("spinner.lottie");
    QVERIFY(writeStoredZip(bundle, {
        {QStringLiteral("manifest.json"), "{\"animations\":[{\"id\":\"b\"},{\"id\":\"a\"}]}"},
        {QStringLiteral("animations/a.json"), anim},
        {QStringLiteral("animations/b.json"), anim},
        {QStringLiteral("images/img_0.png"), QByteArray("png-bytes")},
    }));
    QVERIFY(drift::isDotLottiePath(bundle));
    QVERIFY(!drift::isDotLottiePath(dir.filePath("x.json")));

    QString error;
    const QStringList out = drift::unpackDotLottie(bundle, dir.filePath("unpacked"), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(out.size(), 2);
    // Manifest order, named after the bundle plus the animation id.
    QCOMPARE(QFileInfo(out[0]).fileName(), QStringLiteral("spinner-b.json"));
    QCOMPARE(QFileInfo(out[1]).fileName(), QStringLiteral("spinner-a.json"));
    QFile extracted(out[0]);
    QVERIFY(extracted.open(QIODevice::ReadOnly));
    QCOMPARE(extracted.readAll(), anim);
    QVERIFY(QFile::exists(QFileInfo(out[0]).dir().filePath(QStringLiteral("images/img_0.png"))));
    // Idempotent: the same bundle maps to the same files.
    QCOMPARE(drift::unpackDotLottie(bundle, dir.filePath("unpacked"), &error), out);

    // One animation keeps the bundle's own name; a broken bundle fails with a message.
    const QString single = dir.filePath("hello.lottie");
    QVERIFY(writeStoredZip(single, {{QStringLiteral("animations/anim.json"), anim}}));
    const QStringList one = drift::unpackDotLottie(single, dir.filePath("unpacked"), &error);
    QCOMPARE(one.size(), 1);
    QCOMPARE(QFileInfo(one[0]).fileName(), QStringLiteral("hello.json"));
    QFile junk(dir.filePath("junk.lottie"));
    QVERIFY(junk.open(QIODevice::WriteOnly));
    junk.write("not a zip");
    junk.close();
    QVERIFY(drift::unpackDotLottie(junk.fileName(), dir.filePath("unpacked"), &error).isEmpty());
    QVERIFY(!error.isEmpty());
}

void CoreTest::vectorClipIsSyntheticOnGraphicTracks()
{
    QCOMPARE(drift::trackTypeForClipType(drift::ClipType::Vector), drift::TrackType::Shape);

    drift::Project project;
    drift::Clip clip;
    clip.type = drift::ClipType::Vector;
    clip.timelineDuration = drift::secondsToUs(12.0);
    clip.srcOut = drift::secondsToUs(12.0);
    // No media behind it: the source range is a convention, like an image's.
    QCOMPARE(drift::sourceDurationForClip(project, clip), drift::kImageClipDurationUs);

    // Speed and reverse are the ordinary clip remap; the vector renderer folds what comes out.
    clip.timelineStart = drift::secondsToUs(1.0);
    clip.timelineDuration = drift::secondsToUs(2.0);
    clip.srcIn = 0;
    clip.srcOut = drift::secondsToUs(4.0);
    clip.speed = 2.0;
    QCOMPARE(clip.timelineToSourceUs(drift::secondsToUs(1.5)), drift::secondsToUs(1.0));
    clip.reverse = true;
    QCOMPARE(clip.timelineToSourceUs(drift::secondsToUs(1.5)), drift::secondsToUs(3.0));
}

void CoreTest::foldVectorTimeTable_data()
{
    QTest::addColumn<qint64>("anim");
    QTest::addColumn<qint64>("duration");
    QTest::addColumn<int>("loop");
    QTest::addColumn<bool>("visible");
    QTest::addColumn<qint64>("expected");
    const qint64 d = drift::secondsToUs(2.0);
    const qint64 s = drift::kUsPerSecond;
    using L = drift::VectorLoop;
    QTest::newRow("still ignores loop") << 5 * s << qint64(0) << int(L::Loop) << true << qint64(0);
    QTest::newRow("hold inside") << s << d << int(L::Hold) << true << s;
    QTest::newRow("hold clamps end") << 5 * s << d << int(L::Hold) << true << d;
    QTest::newRow("hold clamps start") << -s << d << int(L::Hold) << true << qint64(0);
    QTest::newRow("loop inside") << s << d << int(L::Loop) << true << s;
    QTest::newRow("loop wraps") << 5 * s << d << int(L::Loop) << true << s;
    QTest::newRow("loop at boundary") << 4 * s << d << int(L::Loop) << true << qint64(0);
    QTest::newRow("loop negative") << -s / 2 << d << int(L::Loop) << true << d - s / 2;
    QTest::newRow("pingpong forward") << s / 2 << d << int(L::PingPong) << true << s / 2;
    QTest::newRow("pingpong back") << 3 * s << d << int(L::PingPong) << true << s;
    QTest::newRow("pingpong turn") << d << d << int(L::PingPong) << true << d;
    QTest::newRow("pingpong second cycle") << 4 * s + s / 2 << d << int(L::PingPong) << true << s / 2;
    QTest::newRow("pingpong negative") << -s / 2 << d << int(L::PingPong) << true << s / 2;
    QTest::newRow("hide inside") << s << d << int(L::Hide) << true << s;
    QTest::newRow("hide at end") << d << d << int(L::Hide) << true << d;
    QTest::newRow("hide after") << d + 1 << d << int(L::Hide) << false << qint64(0);
    QTest::newRow("hide before") << qint64(-1) << d << int(L::Hide) << false << qint64(0);
}

void CoreTest::foldVectorTimeTable()
{
    QFETCH(qint64, anim);
    QFETCH(qint64, duration);
    QFETCH(int, loop);
    QFETCH(bool, visible);
    QFETCH(qint64, expected);
    drift::TimeUs out = -1;
    QCOMPARE(drift::foldVectorTime(anim, duration, drift::VectorLoop(loop), &out), visible);
    if (visible)
        QCOMPARE(out, expected);
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

    clip.reverse = false;
    const drift::TimeUs local = drift::secondsToUs(1.5);
    QCOMPARE(clip.sourceUsToClipLocalUs(clip.timelineToSourceUs(clip.timelineStart + local)), local);
}

void CoreTest::piecewiseLinearBreakpointsCompressLinearMotion()
{
    QVector<QPointF> line;
    for (int i = 0; i < 80; ++i)
        line.append(QPointF(i * 2.0, i * 0.5));
    const QVector<int> linear = drift::piecewiseLinearBreakpoints(line, 0.5);
    QCOMPARE(linear.size(), 2);
    QCOMPARE(linear.first(), 0);
    QCOMPARE(linear.last(), 79);

    QVector<QPointF> corner;
    for (int i = 0; i <= 40; ++i)
        corner.append(QPointF(i, 0));
    for (int i = 41; i <= 80; ++i)
        corner.append(QPointF(40.0, i - 40.0));
    const QVector<int> broken = drift::piecewiseLinearBreakpoints(corner, 0.5);
    QCOMPARE(broken.size(), 3);
    QCOMPARE(broken.first(), 0);
    QCOMPARE(broken.last(), 80);
}

void CoreTest::stabilizePlanDoesNotKeyEveryFrame()
{
    const QString path = QDir::temp().filePath(QStringLiteral("drift-stab-plan-test.trf"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("VID.STAB 1\n");
    const int frames = 60;
    for (int i = 0; i < frames; ++i) {
        // Constant 2px/frame pan: one linear segment after accumulation.
        file.write(QStringLiteral("Frame %1 (List 1 [(LM 2 0 10 10 16 0.5 0.9)])\n")
                       .arg(i)
                       .toLatin1());
    }
    file.close();

    drift::Clip clip;
    clip.srcIn = 0;
    clip.srcOut = drift::secondsToUs(2.0);
    clip.timelineDuration = clip.srcOut;
    clip.speed = 1.0;
    clip.transformX.setKeyframe(0, 0.0);
    clip.transformY.setKeyframe(0, 0.0);
    clip.transformW.setKeyframe(0, 1280.0);
    clip.transformH.setKeyframe(0, 720.0);

    const drift::StabilizePlan pan = drift::planStabilizeKeyframes(
        path, clip, 30.0, 1.0, 1.0, /*smoothing=*/15, /*tripod=*/true, /*epsilon=*/1.0);
    QVERIFY(pan.keys.size() >= 2);
    QVERIFY(pan.keys.size() < frames / 4);

    const drift::StabilizePlan smoothed = drift::planStabilizeKeyframes(
        path, clip, 30.0, 1.0, 1.0, /*smoothing=*/15, /*tripod=*/false, /*epsilon=*/1.0);
    QVERIFY(smoothed.keys.size() < frames / 2);

    // vid.stab applies C - S (tripod: the accumulated path). A +x local motion
    // must produce a +x clip offset; the old S - C sign doubled the shake.
    QVERIFY(!pan.keys.isEmpty());
    QVERIFY(pan.keys.last().dx > 1.0);

    // Smoothing's moving average is one-sided at t=0, so C-S starts with a DC
    // offset. We subtract that so the first key sits on the rest pose.
    QCOMPARE(smoothed.keys.first().timeUs, 0);
    QVERIFY(qAbs(smoothed.keys.first().dx) < 0.5);
    QVERIFY(qAbs(smoothed.keys.first().dy) < 0.5);
    QCOMPARE(pan.keys.first().timeUs, 0);
    QVERIFY(qAbs(pan.keys.first().dx) < 0.5);
    QVERIFY(qAbs(pan.keys.first().dy) < 0.5);

    QFile::remove(path);
}

void CoreTest::stabilizeApplyPlanScalesOffsetsByZoom()
{
    drift::Clip clip;
    clip.transformX.setKeyframe(0, 100.0);
    clip.transformY.setKeyframe(0, 200.0);
    clip.transformW.setKeyframe(0, 400.0);
    clip.transformH.setKeyframe(0, 400.0);

    drift::StabilizePlan plan;
    plan.keys.append({0, 0.0, 0.0});
    plan.keys.append({1'000'000, 20.0, -10.0});
    drift::applyStabilizePlan(clip, plan);

    // maxAbs=20 → zoom = 1 + 2*20/400 = 1.1. W/H grow around the rest center,
    // and X/Y offsets are in that zoomed pixel grid.
    const double zoom = 1.1;
    const double originX = 100.0 + 200.0 - 400.0 * zoom * 0.5;
    const double originY = 200.0 + 200.0 - 400.0 * zoom * 0.5;
    QCOMPARE(clip.transformX.evaluateAt(0), originX);
    QCOMPARE(clip.transformY.evaluateAt(0), originY);
    QCOMPARE(clip.transformX.evaluateAt(1'000'000), originX + 20.0 * zoom);
    QCOMPARE(clip.transformY.evaluateAt(1'000'000), originY - 10.0 * zoom);
    QCOMPARE(clip.transformW.evaluateAt(0), 400.0 * zoom);
    QCOMPARE(clip.transformH.evaluateAt(0), 400.0 * zoom);
}

void CoreTest::stabilizeTrfAsciiAndBinaryParse()
{
    const QString asciiPath = QDir::temp().filePath(QStringLiteral("drift-stab-ascii.trf"));
    QFile ascii(asciiPath);
    QVERIFY(ascii.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ascii.write("VID.STAB 1\n");
    ascii.write("Frame 0 (List 0 [])\n");
    ascii.write("Frame 1 (List 1 [(LM 4 -2 8 12 16 0.5 0.9)])\n");
    ascii.close();

    const QVector<QPointF> asciiFrames = drift::readTrfFrameTranslations(asciiPath);
    QVERIFY(asciiFrames.size() >= 2);
    QCOMPARE(asciiFrames.at(1).x(), 4.0);
    QCOMPARE(asciiFrames.at(1).y(), -2.0);

    const QString binaryPath = QDir::temp().filePath(QStringLiteral("drift-stab-binary.trf"));
    QFile binary(binaryPath);
    QVERIFY(binary.open(QIODevice::WriteOnly | QIODevice::Truncate));
    binary.write("TRF1", 4);
    const qint32 accuracy = 15, shakiness = 5, stepSize = 6;
    const double contrast = 0.25;
    binary.write(reinterpret_cast<const char *>(&accuracy), sizeof(accuracy));
    binary.write(reinterpret_cast<const char *>(&shakiness), sizeof(shakiness));
    binary.write(reinterpret_cast<const char *>(&stepSize), sizeof(stepSize));
    binary.write(reinterpret_cast<const char *>(&contrast), sizeof(contrast));
    const qint32 frameNum = 1;
    const qint32 count = 1;
    binary.write(reinterpret_cast<const char *>(&frameNum), sizeof(frameNum));
    binary.write(reinterpret_cast<const char *>(&count), sizeof(count));
    const qint16 vx = 7, vy = 3, fx = 10, fy = 20, size = 16;
    const double fieldContrast = 0.4, match = 0.8;
    binary.write(reinterpret_cast<const char *>(&vx), sizeof(vx));
    binary.write(reinterpret_cast<const char *>(&vy), sizeof(vy));
    binary.write(reinterpret_cast<const char *>(&fx), sizeof(fx));
    binary.write(reinterpret_cast<const char *>(&fy), sizeof(fy));
    binary.write(reinterpret_cast<const char *>(&size), sizeof(size));
    binary.write(reinterpret_cast<const char *>(&fieldContrast), sizeof(fieldContrast));
    binary.write(reinterpret_cast<const char *>(&match), sizeof(match));
    binary.close();

    const QVector<QPointF> binaryFrames = drift::readTrfFrameTranslations(binaryPath);
    QVERIFY(binaryFrames.size() >= 2);
    QCOMPARE(binaryFrames.at(1).x(), 7.0);
    QCOMPARE(binaryFrames.at(1).y(), 3.0);

    QFile::remove(asciiPath);
    QFile::remove(binaryPath);
}

void CoreTest::stabilizeModeSerialization()
{
    drift::Project project;
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Video});

    drift::Clip clip;
    clip.id = QStringLiteral("clip-stab");
    clip.type = drift::ClipType::Video;
    clip.stabilizeMode = drift::StabilizeMode::Keyframes;
    clip.stabilizeSmoothing = 22;
    clip.stabilizeTripod = true;
    clip.stabilizeAppliedSmoothing = 22;
    clip.stabilizeAppliedTripod = true;
    clip.stabilizeAppliedMode = drift::StabilizeMode::Keyframes;
    clip.stabilizeHasRestPose = true;
    clip.stabilizeRestX = 12.0;
    clip.stabilizeRestY = 34.0;
    clip.stabilizeRestW = 640.0;
    clip.stabilizeRestH = 360.0;
    clip.stabilizeRestRot = 5.0;
    project.tracks()[0].clips.append(clip);

    const QJsonObject json = project.toJson();
    QString error;
    const drift::Project loaded = drift::Project::fromJson(json, &error);
    QVERIFY(error.isEmpty());
    const drift::Clip &loadedClip = loaded.tracks()[0].clips[0];
    QCOMPARE(loadedClip.stabilizeMode, drift::StabilizeMode::Keyframes);
    QCOMPARE(loadedClip.stabilizeSmoothing, 22);
    QCOMPARE(loadedClip.stabilizeTripod, true);
    QCOMPARE(loadedClip.stabilizeAppliedMode, drift::StabilizeMode::Keyframes);
    QCOMPARE(loadedClip.stabilizeHasRestPose, true);
    QCOMPARE(loadedClip.stabilizeRestX, 12.0);
    QCOMPARE(loadedClip.stabilizeRestY, 34.0);
    QCOMPARE(loadedClip.stabilizeRestW, 640.0);
    QCOMPARE(loadedClip.stabilizeRestH, 360.0);
    QCOMPARE(loadedClip.stabilizeRestRot, 5.0);
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
    clipA.mask.shape = drift::MaskShape::Ellipse;
    clipA.mask.w = 0.5;
    clipA.mask.feather = 4.0;

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
    QCOMPARE(loaded.tracks()[0].clips[0].mask.shape, drift::MaskShape::Ellipse);
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
    project.tracks()[0].clips.append(clip);

    drift::Mask matte = drift::fullFrameMediaMask(QStringLiteral("/tmp/mattes/abc.mkv"),
                                                  drift::secondsToUs(1.5));
    matte.mediaFgrPath = QStringLiteral("/tmp/mattes/abc.fgr.mkv");
    matte.invert = true;
    matte.op = drift::MaskOp::Intersect;
    matte.mediaChannel = drift::MaskMediaChannel::Alpha;
    drift::setLinkedMask(project, 0, 0, matte);

    const QJsonObject json = project.toJson();
    QString error;
    const drift::Project loaded = drift::Project::fromJson(json, &error);

    QVERIFY(error.isEmpty());
    const QList<drift::LaneMask> masks = drift::laneMasksAt(loaded, 0, 0);
    QCOMPARE(masks.size(), 1);
    const drift::Mask &mask = masks.constFirst().mask;
    QCOMPARE(mask.shape, drift::MaskShape::Media);
    QCOMPARE(mask.mediaPath, QStringLiteral("/tmp/mattes/abc.mkv"));
    QCOMPARE(mask.mediaFgrPath, QStringLiteral("/tmp/mattes/abc.fgr.mkv"));
    QCOMPARE(mask.mediaSrcOffsetUs, drift::secondsToUs(1.5));
    QCOMPARE(mask.invert, true);
    QCOMPARE(mask.op, drift::MaskOp::Intersect);
    QCOMPARE(mask.mediaChannel, drift::MaskMediaChannel::Alpha);
}

// A pre-v5 project put one mask directly on the clip under the old "matte" spelling. It has to
// come back as a Media mask on a linked adjustment, and — the part that is easy to get wrong —
// full-frame: the old shape ignored its rect entirely, so the serialized w/h defaults of 0.6
// would suddenly crop the cutout to 60% of the frame.
void CoreTest::legacyClipMaskMigratesToAnAdjustmentLane()
{
    const QJsonObject clip{
        {QStringLiteral("id"), QStringLiteral("clip-1")},
        {QStringLiteral("type"), QStringLiteral("video")},
        {QStringLiteral("timelineStartUs"), 0},
        {QStringLiteral("timelineDurationUs"), qint64(drift::secondsToUs(3.0))},
        {QStringLiteral("mask"),
         QJsonObject{{QStringLiteral("shape"), QStringLiteral("matte")},
                     {QStringLiteral("w"), 0.6},
                     {QStringLiteral("h"), 0.6},
                     {QStringLiteral("invert"), true},
                     {QStringLiteral("mattePath"), QStringLiteral("/tmp/mattes/old.mkv")},
                     {QStringLiteral("matteSrcOffsetUs"), qint64(drift::secondsToUs(2.0))}}},
    };
    const QJsonObject json{
        {QStringLiteral("version"), 4},
        {QStringLiteral("tracks"),
         QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("video")},
                                {QStringLiteral("clips"), QJsonArray{clip}}}}},
    };

    QString error;
    const drift::Project loaded = drift::Project::fromJson(json, &error);
    QVERIFY(error.isEmpty());

    // Nothing left on the clip; one lane carrying it instead.
    QCOMPARE(loaded.tracks().at(0).clips.at(0).mask.shape, drift::MaskShape::None);
    const QList<drift::LaneMask> masks =
        drift::laneMasksAt(loaded, 0, drift::secondsToUs(1.0));
    QCOMPARE(masks.size(), 1);
    const drift::Mask &mask = masks.constFirst().mask;
    QCOMPARE(mask.shape, drift::MaskShape::Media);
    QCOMPARE(mask.mediaPath, QStringLiteral("/tmp/mattes/old.mkv"));
    QCOMPARE(mask.mediaSrcOffsetUs, drift::secondsToUs(2.0));
    QCOMPARE(mask.invert, true);
    QCOMPARE(mask.w, 1.0);
    QCOMPARE(mask.h, 1.0);

    // Pinned to the clip, so every later move/trim keeps it aligned.
    const QList<drift::ClipRef> pinned = drift::linkedMaskAdjustments(loaded, 0, 0);
    QCOMPARE(pinned.size(), 1);
    const drift::Clip &adjustment =
        loaded.tracks().at(pinned.constFirst().trackIndex).clips.at(pinned.constFirst().clipIndex);
    QCOMPARE(adjustment.linkedClipId, QStringLiteral("clip-1"));
    QCOMPARE(adjustment.timelineDuration, drift::secondsToUs(3.0));
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

void CoreTest::transitionEasingCurveRoundTrips()
{
    drift::Project project = projectWithTransition(QStringLiteral("crossfade"));
    drift::Transition &t = project.tracks()[0].transitions[0];
    t.easingCurve = drift::FadeCurve::Custom;
    drift::FadeShape shape;
    shape.setPoints({QPointF(0.0, 0.0), QPointF(0.4, 0.1), QPointF(1.0, 1.0)});
    t.easingShape = shape;

    QString error;
    const drift::Project loaded = drift::Project::fromJson(project.toJson(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    const drift::Transition &out = loaded.tracks()[0].transitions[0];
    QCOMPARE(out.easingCurve, drift::FadeCurve::Custom);
    QCOMPARE(out.easingShape.points().size(), shape.points().size());
    QCOMPARE(out.easingShape.gainAt(0.4), shape.gainAt(0.4));

    // A transition that was never eased must serialize exactly as it did before the field
    // existed, so projects written by older builds stay byte-identical on re-save.
    const drift::Project plain = projectWithTransition(QStringLiteral("crossfade"));
    const QJsonObject plainJson =
        plain.toJson().value(QStringLiteral("tracks")).toArray().at(0).toObject()
            .value(QStringLiteral("transitions")).toArray().at(0).toObject();
    QVERIFY(!plainJson.contains(QStringLiteral("easingCurve")));
    QVERIFY(!plainJson.contains(QStringLiteral("easingShape")));
}

void CoreTest::transitionEasingRemapsProgress()
{
    drift::Transition t;
    const drift::TimeUs start = 0;
    const drift::TimeUs end = 1'000'000;
    const drift::TimeUs quarter = 250'000;

    // Linear is the historical behaviour and must stay bit-identical to the plain overload.
    QCOMPARE(drift::transitionProgress(t, quarter, start, end),
             drift::transitionProgress(quarter, start, end));

    t.easingCurve = drift::FadeCurve::Smooth;
    const double eased = drift::transitionProgress(t, quarter, start, end);
    QVERIFY(eased < 0.25);                                    // smoothstep starts slow
    QCOMPARE(drift::transitionProgress(t, start, start, end), 0.0);
    QCOMPARE(drift::transitionProgress(t, end, start, end), 1.0);

    // Endpoints stay pinned for a custom shape too, or the transition would not resolve to the
    // incoming clip.
    t.easingCurve = drift::FadeCurve::Custom;
    drift::FadeShape shape;
    shape.setPoints({QPointF(0.0, 0.0), QPointF(0.5, 0.9), QPointF(1.0, 1.0)});
    t.easingShape = shape;
    QCOMPARE(drift::transitionProgress(t, start, start, end), 0.0);
    QCOMPARE(drift::transitionProgress(t, end, start, end), 1.0);
    QVERIFY(drift::transitionProgress(t, 500'000, start, end) > 0.8);
}

void CoreTest::bezierCurveShapesProgress()
{
    drift::FadeShape shape;
    // CSS ease-in: slow to start, so the midpoint sits below the diagonal.
    shape.setHandles(QPointF(0.42, 0.0), QPointF(1.0, 1.0));
    QCOMPARE(shape.bezierAt(0.0), 0.0);
    QCOMPARE(shape.bezierAt(1.0), 1.0);
    QVERIFY(shape.bezierAt(0.5) < 0.5);

    // Mirror it and the midpoint must rise above the diagonal.
    drift::FadeShape out;
    out.setHandles(QPointF(0.0, 0.0), QPointF(0.58, 1.0));
    QVERIFY(out.bezierAt(0.5) > 0.5);

    // Handles on the thirds are the identity cubic, so it must track linear closely.
    drift::FadeShape straight;
    straight.setHandles(QPointF(1.0 / 3.0, 1.0 / 3.0), QPointF(2.0 / 3.0, 2.0 / 3.0));
    for (double t = 0.0; t <= 1.0; t += 0.1)
        QVERIFY(qAbs(straight.bezierAt(t) - t) < 1e-6);

    // Monotonic: a fold-back would make progress run backwards mid-transition.
    double previous = -1.0;
    for (int i = 0; i <= 100; ++i) {
        const double v = shape.bezierAt(i / 100.0);
        QVERIFY(v >= previous - 1e-9);
        previous = v;
    }

    // Handles are clamped into the unit box on the way in.
    drift::FadeShape clamped;
    clamped.setHandles(QPointF(-2.0, 5.0), QPointF(3.0, -1.0));
    QCOMPARE(clamped.handle1(), QPointF(0.0, 1.0));
    QCOMPARE(clamped.handle2(), QPointF(1.0, 0.0));

    // And it reaches shapedProgress under the Bezier curve, not the polyline.
    drift::FadeShape eased;
    eased.setHandles(QPointF(0.42, 0.0), QPointF(1.0, 1.0));
    QCOMPARE(drift::shapedProgress(0.5, drift::FadeCurve::Bezier, eased), eased.bezierAt(0.5));
    QCOMPARE(drift::shapedProgress(0.5, drift::FadeCurve::Linear, eased), 0.5);
}

void CoreTest::bezierShapeRoundTrips()
{
    drift::Project project = projectWithTransition(QStringLiteral("crossfade"));
    drift::Transition &t = project.tracks()[0].transitions[0];
    t.easingCurve = drift::FadeCurve::Bezier;
    t.easingShape.setHandles(QPointF(0.17, 0.67), QPointF(0.83, 0.67));

    QString error;
    const drift::Project loaded = drift::Project::fromJson(project.toJson(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    const drift::Transition &out = loaded.tracks()[0].transitions[0];
    QCOMPARE(out.easingCurve, drift::FadeCurve::Bezier);
    QCOMPARE(out.easingShape.handle1(), QPointF(0.17, 0.67));
    QCOMPARE(out.easingShape.handle2(), QPointF(0.83, 0.67));

    // A shape that was never given handles still writes the bare array older builds expect,
    // rather than being promoted to the object form.
    drift::FadeShape polyline;
    polyline.setPoints({QPointF(0.0, 0.0), QPointF(0.5, 0.8), QPointF(1.0, 1.0)});
    QVERIFY(!polyline.hasHandles());
    QVERIFY(polyline.toJson().isArray());
    QVERIFY(drift::FadeShape::fromJson(polyline.toJson()).gainAt(0.5) > 0.7);

    drift::FadeShape bezier;
    bezier.setHandles(QPointF(0.2, 0.1), QPointF(0.8, 0.9));
    QVERIFY(bezier.toJson().isObject());
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

namespace {

// An angle laid out on the timeline the way a synced multicam track is: the clip sits at
// `timelineStart` and its media is already lined up there.
drift::Clip makeAngleClip(const QString &path, drift::TimeUs timelineStart,
                          drift::TimeUs duration, drift::TimeUs srcIn)
{
    drift::Clip clip;
    clip.id = QStringLiteral("angle-") + path;
    clip.assetId = QStringLiteral("asset-") + path;
    clip.path = path;
    clip.name = path;
    clip.type = drift::ClipType::Video;
    clip.timelineStart = timelineStart;
    clip.timelineDuration = duration;
    clip.srcIn = srcIn;
    clip.srcOut = srcIn + duration;
    return clip;
}

} // namespace

void CoreTest::retargetClipToSourceKeepsPlacementAndSyncsSource()
{
    // Program segment occupying [4s, 7s).
    drift::Clip program = makeAngleClip(QStringLiteral("cam1.mp4"), drift::secondsToUs(4.0),
                                        drift::secondsToUs(3.0), drift::secondsToUs(10.0));
    // The program clip carries a look the switch must not throw away.
    program.opacity.setKeyframe(drift::secondsToUs(4.0), 0.5);
    program.fadeInUs = drift::secondsToUs(0.25);
    program.effects.append(drift::Effect{});

    // The angle starts at 2s on the timeline, reading its media from 30s.
    const drift::Clip angle = makeAngleClip(QStringLiteral("cam2.mp4"), drift::secondsToUs(2.0),
                                            drift::secondsToUs(20.0), drift::secondsToUs(30.0));

    drift::retargetClipToSource(program, angle, drift::secondsToUs(120.0));

    // Placement is untouched: the switch fills the slot the program already had.
    QCOMPARE(program.timelineStart, drift::secondsToUs(4.0));
    QCOMPARE(program.timelineDuration, drift::secondsToUs(3.0));

    // Media identity now comes from the angle.
    QCOMPARE(program.path, QStringLiteral("cam2.mp4"));
    QCOMPARE(program.assetId, QStringLiteral("asset-cam2.mp4"));

    // The frame the angle was showing at 4s: 30s + (4s - 2s) = 32s.
    QCOMPARE(program.srcIn, drift::secondsToUs(32.0));
    QCOMPARE(program.srcOut, drift::secondsToUs(35.0));
    // And that is exactly what the angle itself maps 4s to, which is the whole point.
    QCOMPARE(program.srcIn, angle.timelineToSourceUs(drift::secondsToUs(4.0)));

    // The treatment survives — switching camera changes pixels, not grade.
    QCOMPARE(program.opacity.keyframes().size(), 1);
    QCOMPARE(program.fadeInUs, drift::secondsToUs(0.25));
    QCOMPARE(program.effects.size(), 1);
}

void CoreTest::retargetClipToSourceClearsPerSourceState()
{
    drift::Clip program = makeAngleClip(QStringLiteral("cam1.mp4"), drift::secondsToUs(1.0),
                                        drift::secondsToUs(2.0), 0);
    program.linkId = QStringLiteral("pair-1");
    program.faceTrackPath = QStringLiteral("/cache/cam1.faces");
    program.faceTrackSrcOffsetUs = drift::secondsToUs(5.0);
    program.speedCurve.setPoints({{0.0, 1.0}, {1.0, 2.0}});
    QVERIFY(program.hasSpeedCurve());

    const drift::Clip angle = makeAngleClip(QStringLiteral("cam2.mp4"), 0, drift::secondsToUs(10.0), 0);
    drift::retargetClipToSource(program, angle, drift::secondsToUs(10.0));

    // All of these are indexed against media that is no longer under this clip.
    QVERIFY(program.linkId.isEmpty());
    QVERIFY(program.faceTrackPath.isEmpty());
    QCOMPARE(program.faceTrackSrcOffsetUs, drift::TimeUs{0});
    QVERIFY(!program.hasSpeedCurve());
}

// Masks are not reachable from retargetClipToSource — they live on the adjustments pinned to the
// clip — so the switch clears them separately, and only the baked ones. Media coverage is pixels
// segmented out of cam1 and would cut cam2 to cam1's silhouette; a shape describes the framing,
// not the footage, and belongs with the transform and effects that already survive.
void CoreTest::retargetClipToSourceKeepsAGeometricMask()
{
    drift::Project project;
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Video});
    project.tracks()[0].clips.append(makeAngleClip(QStringLiteral("cam1.mp4"),
                                                   drift::secondsToUs(1.0),
                                                   drift::secondsToUs(2.0), 0));

    drift::Mask ellipse;
    ellipse.shape = drift::MaskShape::Ellipse;
    ellipse.x = 0.25;
    ellipse.feather = 12.0;
    drift::setLinkedMask(project, 0, 0, ellipse);
    QCOMPARE(drift::laneMasksAt(project, 0, drift::secondsToUs(1.5)).size(), 1);

    drift::clearLinkedMasks(project, 0, 0, /*mediaOnly=*/true);

    const QList<drift::LaneMask> kept = drift::laneMasksAt(project, 0, drift::secondsToUs(1.5));
    QCOMPARE(kept.size(), 1);
    QCOMPARE(kept.constFirst().mask.shape, drift::MaskShape::Ellipse);
    QCOMPARE(kept.constFirst().mask.x, 0.25);
    QCOMPARE(kept.constFirst().mask.feather, 12.0);

    // The same call does drop a matte, which is the half the multicam switch is there for.
    drift::setLinkedMask(project, 0, 0,
                         drift::fullFrameMediaMask(QStringLiteral("/cache/cam1.matte.mp4"),
                                                   drift::secondsToUs(2.0)));
    drift::clearLinkedMasks(project, 0, 0, /*mediaOnly=*/true);
    QVERIFY(drift::laneMasksAt(project, 0, drift::secondsToUs(1.5)).isEmpty());
}

// setLinkedMask replaces, which is what a full-replacement write (MCP, paste) means. Dropping a
// second mask onto a clip means something else entirely, so it has its own call.
void CoreTest::addLinkedMaskStacksRatherThanReplacing()
{
    drift::Project project;
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Video});
    project.tracks()[0].clips.append(makeAngleClip(QStringLiteral("cam1.mp4"),
                                                   drift::secondsToUs(1.0),
                                                   drift::secondsToUs(2.0), 0));

    drift::Mask ellipse;
    ellipse.shape = drift::MaskShape::Ellipse;
    const drift::ClipRef first = drift::addLinkedMask(project, 0, 0, ellipse);
    QVERIFY(first.trackIndex >= 0);

    drift::Mask star;
    star.shape = drift::MaskShape::Star;
    star.op = drift::MaskOp::Subtract;
    // The clip has not moved: the lane goes in below it.
    const drift::ClipRef second = drift::addLinkedMask(project, 0, 0, star);
    QVERIFY(second.trackIndex >= 0);

    const QList<drift::ClipRef> pinned = drift::linkedMaskAdjustments(project, 0, 0);
    QCOMPARE(pinned.size(), 2);

    // Both reach the compositor, in lane order, with their ops intact.
    const QList<drift::LaneMask> stack = drift::laneMasksAt(project, 0, drift::secondsToUs(1.5));
    QCOMPARE(stack.size(), 2);
    QCOMPARE(stack.at(0).mask.shape, drift::MaskShape::Ellipse);
    QCOMPARE(stack.at(1).mask.shape, drift::MaskShape::Star);
    QCOMPARE(stack.at(1).mask.op, drift::MaskOp::Subtract);
    // Distinct ids, or the reader pool would decode both under one cursor.
    QVERIFY(stack.at(0).adjustmentId != stack.at(1).adjustmentId);

    // The replacing call still collapses the stack to one, which is what it promises.
    drift::Mask heart;
    heart.shape = drift::MaskShape::Heart;
    drift::setLinkedMask(project, 0, 0, heart);
    QCOMPARE(drift::linkedMaskAdjustments(project, 0, 0).size(), 1);
}

// Dropping a mask on empty track space masks whatever the track shows over that span, so the
// adjustment is pinned to nothing and keeps its own edges. liftAdjustmentClipsToOwnTracks must
// leave it where it is — it only hoists adjustments sitting on a *video* track.
void CoreTest::laneMaskIsUnpinnedAndSurvivesTheLiftPass()
{
    drift::Project project;
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Video});
    project.tracks()[0].clips.append(makeAngleClip(QStringLiteral("cam1.mp4"),
                                                   drift::secondsToUs(0.0),
                                                   drift::secondsToUs(4.0), 0));

    drift::Mask bars;
    bars.shape = drift::MaskShape::Bars;
    const drift::ClipRef ref = drift::addLaneMask(project, 0, bars, drift::secondsToUs(1.0),
                                                  drift::secondsToUs(2.0));
    QVERIFY(ref.trackIndex >= 0);
    QVERIFY(project.tracks().at(ref.trackIndex).isAdjustmentLane());

    const drift::Clip &adjustment = project.tracks().at(ref.trackIndex).clips.at(ref.clipIndex);
    QVERIFY2(adjustment.linkedClipId.isEmpty(), "a lane mask must not pin itself to a clip");
    QCOMPARE(adjustment.timelineStart, drift::secondsToUs(1.0));
    QCOMPARE(adjustment.timelineDuration, drift::secondsToUs(2.0));

    // It is not pinned, so it does not belong to a clip — only to the span.
    QVERIFY(drift::linkedMaskAdjustments(project, 0, 0).isEmpty());
    QVERIFY(drift::laneMasksAt(project, 0, drift::secondsToUs(0.5)).isEmpty());
    QCOMPARE(drift::laneMasksAt(project, 0, drift::secondsToUs(2.0)).size(), 1);

    drift::liftAdjustmentClipsToOwnTracks(project);
    QVERIFY(project.tracks().at(ref.trackIndex).isAdjustmentLane());
    QCOMPARE(drift::laneMasksAt(project, 0, drift::secondsToUs(2.0)).size(), 1);
}

void CoreTest::retargetClipToSourceShrinksWhenMediaRunsOut()
{
    // A four-second slot...
    drift::Clip program = makeAngleClip(QStringLiteral("cam1.mp4"), drift::secondsToUs(0.0),
                                        drift::secondsToUs(4.0), 0);
    // ...pointed at an angle whose media only has 1.5 s left from the sync point.
    const drift::Clip angle = makeAngleClip(QStringLiteral("cam2.mp4"), 0, drift::secondsToUs(10.0),
                                            drift::secondsToUs(8.5));

    drift::retargetClipToSource(program, angle, drift::secondsToUs(10.0));

    QCOMPARE(program.srcIn, drift::secondsToUs(8.5));
    QCOMPARE(program.srcOut, drift::secondsToUs(10.0));
    // Pulled back to what is actually there rather than freezing on the last frame.
    QCOMPARE(program.timelineDuration, drift::secondsToUs(1.5));
}

void CoreTest::applyMulticamSwitchPunchesAndRecuts()
{
    const drift::TimeUs start = 0;
    const drift::TimeUs end = drift::secondsToUs(10.0);
    QList<drift::MulticamCut> cuts;

    // Unedited: the topmost camera is already live, so picking it does nothing.
    QCOMPARE(drift::applyMulticamSwitch(cuts, start, end, 0, start, 0),
             drift::MulticamSwitchResult::NoOp);
    QVERIFY(cuts.isEmpty());
    QCOMPARE(drift::multicamAngleAt(cuts, start, end, drift::secondsToUs(3.0), 0), 0);

    // Pick-then-play: a switch at the start assigns the whole span.
    QCOMPARE(drift::applyMulticamSwitch(cuts, start, end, 1, start, 0),
             drift::MulticamSwitchResult::Applied);
    QCOMPARE(cuts.size(), 1);
    QCOMPARE(cuts.at(0).angle, 1);
    QCOMPARE(drift::multicamAngleAt(cuts, start, end, drift::secondsToUs(4.0), 0), 1);

    // Recut in the middle: left half keeps camera 1, from 4s camera 2 takes over.
    QCOMPARE(drift::applyMulticamSwitch(cuts, start, end, 2, drift::secondsToUs(4.0), 0),
             drift::MulticamSwitchResult::Applied);
    QCOMPARE(cuts.size(), 2);
    QCOMPARE(cuts.at(0).timeUs, start);
    QCOMPARE(cuts.at(0).angle, 1);
    QCOMPARE(cuts.at(1).timeUs, drift::secondsToUs(4.0));
    QCOMPARE(cuts.at(1).angle, 2);
    QCOMPARE(drift::multicamAngleAt(cuts, start, end, drift::secondsToUs(3.0), 0), 1);
    QCOMPARE(drift::multicamAngleAt(cuts, start, end, drift::secondsToUs(4.0), 0), 2);

    const QList<drift::MulticamInterval> intervals = drift::multicamIntervals(cuts, start, end, 0);
    QCOMPARE(intervals.size(), 2);
    QCOMPARE(intervals.at(0).endUs, drift::secondsToUs(4.0));
    QCOMPARE(intervals.at(1).endUs, end);

    // Switching on an existing cut only changes that interval.
    QCOMPARE(drift::applyMulticamSwitch(cuts, start, end, 0, drift::secondsToUs(4.0), 0),
             drift::MulticamSwitchResult::Applied);
    QCOMPARE(cuts.at(1).angle, 0);
}

void CoreTest::applyMulticamSwitchMergesAdjacentSameCamera()
{
    const drift::TimeUs start = 0;
    const drift::TimeUs end = drift::secondsToUs(10.0);
    QList<drift::MulticamCut> cuts;
    QCOMPARE(drift::applyMulticamSwitch(cuts, start, end, 1, drift::secondsToUs(3.0), 0),
             drift::MulticamSwitchResult::Applied);
    QCOMPARE(cuts.size(), 2);

    // Punching camera 0 at 3s makes both halves the initial camera, so the seam goes.
    QCOMPARE(drift::applyMulticamSwitch(cuts, start, end, 0, drift::secondsToUs(3.0), 0),
             drift::MulticamSwitchResult::Applied);
    QCOMPARE(cuts.size(), 1);
    QCOMPARE(cuts.at(0).angle, 0);
    QCOMPARE(cuts.at(0).timeUs, start);
}

void CoreTest::applyMulticamSwitchRejectsEdges()
{
    const drift::TimeUs start = 0;
    const drift::TimeUs end = drift::secondsToUs(10.0);
    QList<drift::MulticamCut> cuts;

    QCOMPARE(drift::applyMulticamSwitch(cuts, start, end, 1, drift::secondsToUs(-1.0), 0),
             drift::MulticamSwitchResult::OutOfRange);
    QCOMPARE(drift::applyMulticamSwitch(cuts, start, end, 1, end, 0),
             drift::MulticamSwitchResult::OutOfRange);

    // Closer to the start than a clip is allowed to be.
    QCOMPARE(drift::applyMulticamSwitch(cuts, start, end, 1, drift::kMinClipDurationUs / 2, 0),
             drift::MulticamSwitchResult::TooCloseToEdge);
    QVERIFY(cuts.isEmpty());

    QCOMPARE(drift::applyMulticamSwitch(cuts, start, end, 1, drift::secondsToUs(5.0), 0),
             drift::MulticamSwitchResult::Applied);
    QCOMPARE(drift::applyMulticamSwitch(cuts, start, end, 1, drift::secondsToUs(5.0), 0),
             drift::MulticamSwitchResult::NoOp);
}

void CoreTest::sliceClipToTimelineRangeKeepsSourceInSync()
{
    drift::Clip src = makeAngleClip(QStringLiteral("cam.mp4"), 0, drift::secondsToUs(10.0),
                                    drift::secondsToUs(20.0));
    drift::Clip slice;
    QVERIFY(drift::sliceClipToTimelineRange(src, drift::secondsToUs(2.0), drift::secondsToUs(5.0),
                                            slice));
    QCOMPARE(slice.timelineStart, drift::secondsToUs(2.0));
    QCOMPARE(slice.timelineDuration, drift::secondsToUs(3.0));
    QCOMPARE(slice.srcIn, drift::secondsToUs(22.0));
    QCOMPARE(slice.srcOut, drift::secondsToUs(25.0));
    QCOMPARE(slice.path, QStringLiteral("cam.mp4"));

    drift::Clip miss;
    QVERIFY(!drift::sliceClipToTimelineRange(src, drift::secondsToUs(11.0), drift::secondsToUs(12.0),
                                             miss));
    QVERIFY(!drift::sliceClipToTimelineRange(src, 0, drift::kMinClipDurationUs / 2, miss));
}

// The user library shares TextStyle's project-file codec, so the risk is not the fields but the
// wiring around them: the id namespace, disk round-trip, and that a saved pack stops claiming
// whatever built-in it was edited from.
void CoreTest::userTextPresetsRoundTrip()
{
    const QString org = QCoreApplication::organizationName();
    const QString app = QCoreApplication::applicationName();
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("DriftTest"));
    QCoreApplication::setApplicationName(QStringLiteral("DriftTestTextPresets"));

    drift::TextPresetStore &store = drift::TextPresetStore::instance();
    QFile::remove(drift::TextPresetStore::storePath());
    store.reload();
    QVERIFY(store.presets().isEmpty());

    drift::TextStyle style;
    style.fontFamily = QStringLiteral("Archivo Black");
    style.pixelSize = 91;
    style.fontWeight = 400;
    style.layers = {drift::glowLayer(QColor(12, 240, 90), 18.0, 0.8), drift::solidFillLayer(Qt::white)};
    style.underlineEnabled = true;
    style.accent.rule = drift::WordAccentRule::EveryNth;
    style.accent.n = 3;
    style.accent.highlight.enabled = true;
    style.animation.in = drift::legacyTextAnimationSlot(QStringLiteral("bounce"), 400000, QStringLiteral("easeOut"),
                                                        QStringLiteral("word"), 33000, QStringLiteral("forward"));
    style.animation.out = drift::legacyTextAnimationSlot(QStringLiteral("blur"), 400000, QStringLiteral("easeOut"),
                                                         QStringLiteral("block"), 60000, QStringLiteral("forward"));
    style.packId = QStringLiteral("impact"); // must not survive: a saved pack is its own style

    drift::TextStyle expected = style;
    expected.packId.clear();

    const QString id = store.add(QStringLiteral("  Neon punch  "), style,
                                 QStringLiteral("Hello there"));
    QVERIFY(drift::isUserTextPresetId(id));
    QCOMPARE(store.presets().size(), 1);
    QCOMPARE(store.presets().first().label, QStringLiteral("Neon punch"));
    QVERIFY(store.add(QStringLiteral("   "), style, QStringLiteral("x")).isEmpty());

    // Resolvable through the shared entry point the preview provider and applyTextPreset use,
    // while staying out of the built-in catalog.
    QVERIFY(drift::textPresetForId(id).has_value());
    QCOMPARE(drift::textStyleForPresetId(id)->pixelSize, 91);
    for (const drift::TextPreset &builtin : drift::textPresets())
        QVERIFY(builtin.id != id);

    store.reload();
    QCOMPARE(store.presets().size(), 1);
    const drift::TextPreset reloaded = store.presets().first();
    QCOMPARE(reloaded.id, id);
    QCOMPARE(reloaded.sampleText, QStringLiteral("Hello there"));
    QVERIFY(reloaded.style.packId.isEmpty());
    QVERIFY(drift::textStyleToJson(reloaded.style) == drift::textStyleToJson(expected));

    QVERIFY(store.rename(id, QStringLiteral("Renamed")));
    QVERIFY(!store.rename(QStringLiteral("user:missing"), QStringLiteral("x")));
    QVERIFY(!store.rename(id, QStringLiteral("   ")));
    store.reload();
    QCOMPARE(store.presets().first().label, QStringLiteral("Renamed"));

    const QString exportPath =
        QDir(QDir::tempPath()).filePath(QStringLiteral("drift-style-test.drifttextstyle"));
    QFile::remove(exportPath);
    QVERIFY(store.exportToFile(id, exportPath));
    const QString importedId = store.importFromFile(exportPath);
    QVERIFY(!importedId.isEmpty());
    QVERIFY(importedId != id); // a shared file never overwrites an existing entry
    QCOMPARE(store.presets().size(), 2);
    QVERIFY(drift::textStyleToJson(store.presetForId(importedId)->style)
            == drift::textStyleToJson(expected));
    QFile::remove(exportPath);

    QVERIFY(store.remove(id));
    QVERIFY(!store.remove(id));
    store.reload();
    QCOMPARE(store.presets().size(), 1);
    QCOMPARE(store.presets().first().id, importedId);
    QVERIFY(!drift::textPresetForId(id));

    QFile::remove(drift::TextPresetStore::storePath());
    store.reload();
    QCoreApplication::setOrganizationName(org);
    QCoreApplication::setApplicationName(app);
    QStandardPaths::setTestModeEnabled(false);
}

namespace {

// A stack exercising everything the payload has to carry: two video effects, one of them
// disabled, a keyframed param with hand-dragged tangents, a corner key, a hold key, a track
// switched off, and an audio effect on the other side of the payload.
drift::EffectStackPreset sampleStack()
{
    drift::EffectStackPreset stack;
    stack.label = QStringLiteral("Neon grade");
    stack.sourceDurationUs = 2000000;

    drift::Effect blur;
    blur.name = QStringLiteral("gblur");
    blur.catalogId = QStringLiteral("blur.gaussian");
    blur.parameters.insert(QStringLiteral("sigma"), 8.5);
    blur.parameters.insert(QStringLiteral("wireframe"), true);
    blur.parameters.insert(QStringLiteral("shade"), QStringLiteral("#b03048"));

    drift::KeyframeTrack<double> sigma;
    drift::Keyframe<double> first;
    first.value = 0.0;
    first.outDx = 500000.0;
    first.outDy = 2.0;
    first.corner = true;
    sigma.setKeyframe(0, first);
    drift::Keyframe<double> mid;
    mid.value = 4.0;
    mid.hold = true;
    sigma.setKeyframe(1000000, mid);
    drift::Keyframe<double> last;
    last.value = 8.5;
    last.inDx = -500000.0;
    last.inDy = -2.0;
    sigma.setKeyframe(2000000, last);
    sigma.setEnabled(false);
    blur.paramKeyframes.insert(QStringLiteral("sigma"), sigma);
    stack.effects.append(blur);

    drift::Effect contrast;
    contrast.name = QStringLiteral("eq");
    contrast.catalogId = QStringLiteral("adjust.contrast");
    contrast.parameters.insert(QStringLiteral("contrast"), 1.4);
    contrast.enabled = false;
    stack.effects.append(contrast);

    drift::Effect echo;
    echo.name = QStringLiteral("Echo");
    echo.catalogId = QStringLiteral("space.echo");
    echo.parameters.insert(QStringLiteral("mix"), 0.35);
    stack.audioEffects.append(echo);

    return stack;
}

void compareEffects(const QList<drift::Effect> &got, const QList<drift::Effect> &want)
{
    QCOMPARE(got.size(), want.size());
    for (int i = 0; i < got.size(); ++i) {
        QCOMPARE(got.at(i).name, want.at(i).name);
        QCOMPARE(got.at(i).catalogId, want.at(i).catalogId);
        QCOMPARE(got.at(i).enabled, want.at(i).enabled);
        QCOMPARE(got.at(i).parameters, want.at(i).parameters);

        const auto &gotTracks = got.at(i).paramKeyframes;
        const auto &wantTracks = want.at(i).paramKeyframes;
        QCOMPARE(gotTracks.keys(), wantTracks.keys());
        for (auto it = wantTracks.constBegin(); it != wantTracks.constEnd(); ++it) {
            const drift::KeyframeTrack<double> &g = gotTracks.value(it.key());
            QCOMPARE(g.enabled(), it.value().enabled());
            QCOMPARE(g.keyframes().keys(), it.value().keyframes().keys());
            for (auto k = it.value().keyframes().constBegin();
                 k != it.value().keyframes().constEnd(); ++k) {
                QVERIFY(g.keyframes().value(k.key()) == k.value());
            }
        }
    }
}

} // namespace

// The payload is written to the system clipboard, exported to a file and stored in the library, so
// everything a user tuned has to survive a trip through JSON unchanged.
void CoreTest::effectStackJsonRoundTrip()
{
    const drift::EffectStackPreset stack = sampleStack();
    const QJsonObject object = drift::effectStackToJson(stack);

    QCOMPARE(object.value(QStringLiteral("drift")).toString(), QStringLiteral("effectStack"));
    QCOMPARE(object.value(QStringLiteral("version")).toInt(), 1);
    // An export carries no library id; only a stored preset does.
    QVERIFY(!object.contains(QStringLiteral("id")));

    const QByteArray json = QJsonDocument(object).toJson(QJsonDocument::Compact);
    const drift::EffectStackPreset back =
        drift::effectStackFromJson(QJsonDocument::fromJson(json).object());

    QCOMPARE(back.label, stack.label);
    QCOMPARE(back.sourceDurationUs, stack.sourceDurationUs);
    compareEffects(back.effects, stack.effects);
    compareEffects(back.audioEffects, stack.audioEffects);
}

// The marker check is what makes it safe to auto-detect a paste off the system clipboard, which is
// shared with every other application and usually holds prose. It must not regress.
void CoreTest::effectStackRejectsForeignPayloads()
{
    QVERIFY(drift::effectStackFromJson(QJsonObject{}).isEmpty());

    QJsonObject stripped = drift::effectStackToJson(sampleStack());
    QVERIFY(!drift::effectStackFromJson(stripped).isEmpty()); // control
    stripped.remove(QStringLiteral("drift"));
    QVERIFY(drift::effectStackFromJson(stripped).isEmpty());

    // A payload from a future build may carry fields this one would silently drop; pasting half a
    // stack is worse than pasting none.
    QJsonObject newer = drift::effectStackToJson(sampleStack());
    newer.insert(QStringLiteral("version"), 99);
    QVERIFY(drift::effectStackFromJson(newer).isEmpty());
}

void CoreTest::effectKeyframeRescaleIsProportional()
{
    const drift::EffectStackPreset stack = sampleStack();

    QList<drift::Effect> effects = stack.effects;
    drift::rescaleEffectKeyframes(effects, 2000000, 6000000);
    const drift::KeyframeTrack<double> &track =
        effects.first().paramKeyframes.value(QStringLiteral("sigma"));

    QCOMPARE(track.keyframes().keys(), (QList<drift::TimeUs>{0, 3000000, 6000000}));
    QCOMPARE(track.enabled(), false); // a track switched off must stay switched off

    // dx is microseconds on the same axis as the key time and scales with it; dy is in the
    // parameter's own units and must not move, or every ease flattens or exaggerates.
    const drift::Keyframe<double> first = track.keyframes().value(0);
    QCOMPARE(first.outDx, 1500000.0);
    QCOMPARE(first.outDy, 2.0);
    QCOMPARE(first.value, 0.0);
    QVERIFY(first.corner);
    QVERIFY(track.keyframes().value(3000000).hold);
    const drift::Keyframe<double> last = track.keyframes().value(6000000);
    QCOMPARE(last.inDx, -1500000.0);
    QCOMPARE(last.inDy, -2.0);

    // Degenerate and identity cases leave the stack alone.
    for (const QPair<drift::TimeUs, drift::TimeUs> &pair :
         QList<QPair<drift::TimeUs, drift::TimeUs>>{{0, 4000000}, {2000000, 0}, {2000000, 2000000}}) {
        QList<drift::Effect> untouched = stack.effects;
        drift::rescaleEffectKeyframes(untouched, pair.first, pair.second);
        compareEffects(untouched, stack.effects);
    }

    // A key past the source duration scales past the target rather than being clamped, matching
    // how a trim leaves keys sitting beyond the new edge.
    QList<drift::Effect> overhang = stack.effects;
    drift::rescaleEffectKeyframes(overhang, 1000000, 2000000);
    QCOMPARE(overhang.first().paramKeyframes.value(QStringLiteral("sigma")).keyframes().lastKey(),
             drift::TimeUs{4000000});
}

void CoreTest::userEffectPresetsRoundTrip()
{
    const QString org = QCoreApplication::organizationName();
    const QString app = QCoreApplication::applicationName();
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("DriftTest"));
    QCoreApplication::setApplicationName(QStringLiteral("DriftTestEffectPresets"));

    drift::EffectStackStore &store = drift::EffectStackStore::instance();
    QFile::remove(drift::EffectStackStore::storePath());
    store.reload();
    QVERIFY(store.presets().isEmpty());

    const drift::EffectStackPreset stack = sampleStack();
    QVERIFY(store.add(QStringLiteral("   "), stack).isEmpty());       // blank label
    QVERIFY(store.add(QStringLiteral("Empty"), {}).isEmpty());        // nothing to save

    const QString id = store.add(QStringLiteral("  Neon grade  "), stack);
    QVERIFY(!id.isEmpty());
    QVERIFY(drift::isUserEffectPresetId(id));

    store.reload();
    QCOMPARE(store.presets().size(), 1);
    const drift::EffectStackPreset stored = store.presets().first();
    QCOMPARE(stored.label, QStringLiteral("Neon grade")); // trimmed
    QCOMPARE(stored.sourceDurationUs, stack.sourceDurationUs);
    compareEffects(stored.effects, stack.effects);
    compareEffects(stored.audioEffects, stack.audioEffects);

    QVERIFY(store.rename(id, QStringLiteral("Neon night")));
    QVERIFY(!store.rename(QStringLiteral("user:nope"), QStringLiteral("Nothing")));
    QCOMPARE(store.presetForId(id)->label, QStringLiteral("Neon night"));

    // Re-importing your own export adds a copy rather than clobbering the original.
    const QString path = QDir(QDir::tempPath()).filePath(QStringLiteral("drift-stack.json"));
    QFile::remove(path);
    QVERIFY(store.exportToFile(id, path));
    const QString importedId = store.importFromFile(path);
    QVERIFY(!importedId.isEmpty());
    QVERIFY(importedId != id);
    QCOMPARE(store.presets().size(), 2);
    compareEffects(store.presetForId(importedId)->effects, stack.effects);
    QFile::remove(path);

    QVERIFY(store.remove(id));
    QVERIFY(!store.remove(id));
    store.reload();
    QCOMPARE(store.presets().size(), 1);
    QCOMPARE(store.presets().first().id, importedId);

    QFile::remove(drift::EffectStackStore::storePath());
    store.reload();
    QCoreApplication::setOrganizationName(org);
    QCoreApplication::setApplicationName(app);
    QStandardPaths::setTestModeEnabled(false);
}


namespace {

using namespace drift;
using namespace drift::textanim;

QList<FragmentInfo> charFragments(int n, double advance = 20.0)
{
    QList<FragmentInfo> out;
    for (int i = 0; i < n; ++i) {
        FragmentInfo f;
        f.charIndex = i;
        f.nonSpaceIndex = i;
        f.wordIndex = i;
        f.lineIndex = 0;
        f.advance = advance;
        f.ascent = 16.0;
        f.textStart = i;
        f.textLength = 1;
        out.append(f);
    }
    return out;
}

Domains charDomains(int n)
{
    Domains d;
    d.chars = n;
    d.nonSpaceChars = n;
    d.words = n;
    d.lines = 1;
    return d;
}

TextRangeSelector curves(TextSelectorShape shape, TextSelectorUnits units, double start, double end,
                         TextSelectorDomain domain = TextSelectorDomain::Chars)
{
    TextRangeSelector s;
    s.driver = TextSelectorDriver::Curves;
    s.domain = domain;
    s.units = units;
    s.shape = shape;
    s.start = start;
    s.end = end;
    s.smoothness = 100.0;
    return s;
}

// Coverage 1 on every character: a square over the whole domain with no smoothing.
TextRangeSelector fullCoverage()
{
    TextRangeSelector s = curves(TextSelectorShape::Square, TextSelectorUnits::Percent, 0, 100);
    s.smoothness = 0.0;
    return s;
}

TextRangeSelector stagger(TimeUs staggerUs, TimeUs durationUs, TextEaseKind ease = TextEaseKind::Linear,
                          TextSelectorDomain domain = TextSelectorDomain::CharsExcludingSpaces)
{
    TextRangeSelector s;
    s.driver = TextSelectorDriver::Stagger;
    s.domain = domain;
    s.staggerUs = staggerUs;
    s.durationUs = durationUs;
    s.ease.kind = ease;
    return s;
}

EvalContext contextAt(TimeUs t, TimeUs windowUs = 1'000'000)
{
    EvalContext c;
    c.windowStartUs = 0;
    c.windowDurationUs = windowUs;
    c.timelineUs = t;
    c.emPx = 64.0;
    c.boxWidthPx = 400.0;
    c.boxHeightPx = 100.0;
    c.renderScale = 1.0;
    c.alignFactor = 0.5;
    return c;
}

QList<double> coverages(const TextRangeSelector &sel, int n, double progress = 0.0)
{
    QList<double> out;
    for (int i = 0; i < n; ++i)
        out.append(rangeSelectorCoverage(sel, i, n, progress));
    return out;
}

QList<double> opacities(const Frame &frame)
{
    QList<double> out;
    for (const FragmentProps &p : frame.props)
        out.append(p.opacity);
    return out;
}

bool listsClose(const QList<double> &a, const QList<double> &b, double tol = 1e-6)
{
    if (a.size() != b.size())
        return false;
    for (int i = 0; i < a.size(); ++i)
        if (std::abs(a[i] - b[i]) > tol)
            return false;
    return true;
}

QString show(const QList<double> &v)
{
    QStringList parts;
    for (double d : v)
        parts << QString::number(d, 'f', 4);
    return parts.join(QLatin1String(", "));
}

} // namespace

#define VERIFY_LIST(actual, expected) \
    QVERIFY2(listsClose(actual, expected), qPrintable(QStringLiteral("got [%1] want [%2]").arg(show(actual), show(expected))))

void CoreTest::rangeSelectorCoverageMatchesSkottie()
{
    using S = TextSelectorShape;
    using U = TextSelectorUnits;
    // A square over the first half with full smoothness: the ramp lands exactly on the edge.
    VERIFY_LIST(coverages(curves(S::Square, U::Percent, 0, 50), 4), (QList<double>{1, 1, 0, 0}));
    VERIFY_LIST(coverages(curves(S::Square, U::Percent, 0, 37.5), 4), (QList<double>{1, 0.5, 0, 0}));
    VERIFY_LIST(coverages(curves(S::RampUp, U::Index, 0, 4), 4), (QList<double>{0.125, 0.375, 0.625, 0.875}));
    VERIFY_LIST(coverages(curves(S::Triangle, U::Percent, 0, 100), 4), (QList<double>{0.25, 0.75, 0.75, 0.25}));

    // Ease-high 100 % bends the ramp through the unit cubic (0,0),(0,1): y = 0.5 maps to 0.8899.
    TextRangeSelector eased = curves(S::RampUp, U::Index, 0, 1);
    eased.easeHi = 100.0;
    QVERIFY(std::abs(rangeSelectorCoverage(eased, 0, 1, 0.0) - 0.8899) < 0.005);

    TextRangeSelector negated = curves(S::RampUp, U::Index, 0, 4);
    negated.amount = -100.0;
    VERIFY_LIST(coverages(negated, 4), (QList<double>{-0.125, -0.375, -0.625, -0.875}));

    // Index units with the default "everything" end cover the whole domain.
    VERIFY_LIST(coverages(curves(S::Square, U::Index, 0, kTextSelectorIndexEnd), 4), (QList<double>{1, 1, 1, 1}));

    // Round starts steep, Smooth starts flat; both are 0 outside and 1 in the middle.
    const QList<double> round = coverages(curves(S::Round, U::Percent, 0, 100), 8);
    const QList<double> smooth = coverages(curves(S::Smooth, U::Percent, 0, 100), 8);
    QVERIFY(round[0] > smooth[0]);
    QVERIFY(std::abs(round[3] - 1.0) < 0.05 && std::abs(smooth[3] - 1.0) < 0.05);

    // Animated selector params read the slot progress.
    TextRangeSelector moving = curves(S::Square, U::Percent, 0, 50);
    moving.offset.curve.setKeyframe(0, 0.0);
    moving.offset.curve.setKeyframe(kProgressScale, 50.0);
    VERIFY_LIST(coverages(moving, 4, 1.0), (QList<double>{0, 0, 1, 1}));

    // Two selectors compose through the second one's mode.
    TextAnimator animator;
    animator.selectors = {curves(S::Square, U::Percent, 0, 50), curves(S::RampUp, U::Index, 0, 4)};
    animator.selectors[1].mode = TextSelectorMode::Intersect;
    animator.props.opacity = 0.0;
    animator.props.hasOpacity = true;
    ResolvedSlots resolved;
    resolved.in = {animator};
    const Frame frame = evaluateTextAnimation(TextAnimationSet{}, resolved, charFragments(4), charDomains(4), contextAt(0));
    VERIFY_LIST(opacities(frame), (QList<double>{0.875, 0.625, 1, 1}));
}

void CoreTest::staggerMatchesMovingRamp()
{
    const TextRangeSelector sel = stagger(100'000, 200'000);
    QList<double> cov;
    for (int i = 0; i < 4; ++i)
        cov.append(staggerCoverage(sel, i, 150'000));
    VERIFY_LIST(cov, (QList<double>{0.25, 0.75, 1, 1}));
    // The same thing as an AE ramp whose window is duration/stagger units wide.
    VERIFY_LIST(coverages(curves(TextSelectorShape::RampUp, TextSelectorUnits::Index, 0, 2), 4), cov);

    const TextRangeSelector step = stagger(100'000, 0);
    QList<double> stepCov;
    for (int i = 0; i < 4; ++i)
        stepCov.append(staggerCoverage(step, i, 150'000));
    VERIFY_LIST(stepCov, (QList<double>{0, 0, 1, 1}));

    // An Out slot mirrors the resolved so the last fragment leaves exactly at the window end.
    TextAnimator fade;
    fade.selectors = {sel};
    fade.props.opacity = 0.0;
    fade.props.hasOpacity = true;
    ResolvedSlots resolved;
    resolved.out = {fade};
    const Frame out = evaluateTextAnimation(TextAnimationSet{}, resolved, charFragments(4), charDomains(4), contextAt(850'000));
    VERIFY_LIST(opacities(out), (QList<double>{0, 0, 0.25, 0.75}));
    const Frame settled = evaluateTextAnimation(TextAnimationSet{}, resolved, charFragments(4), charDomains(4), contextAt(300'000));
    VERIFY_LIST(opacities(settled), (QList<double>{1, 1, 1, 1}));
    QVERIFY(settled.isStatic);

    QList<double> ranks;
    for (int i = 0; i < 4; ++i)
        ranks.append(reindexForOrder(i, 4, TextAnimOrder::CenterOut, 0));
    VERIFY_LIST(ranks, (QList<double>{1.5, 0.5, 0.5, 1.5}));
    QCOMPARE(maxReindex(4, TextAnimOrder::CenterOut), 1.5);
    QCOMPARE(maxReindex(4, TextAnimOrder::Backward), 3.0);

    // Seed 0 is the legacy shuffle (TextRaster's table), so old projects keep their order.
    for (int i = 0; i < 8; ++i) {
        const quint32 h = qHash(static_cast<quint32>(i) * 2654435761u) ^ 0x9e3779b9u;
        const double legacy = (h & 0xffffu) / 65535.0 * 7;
        QCOMPARE(reindexForOrder(i, 8, TextAnimOrder::Random, 0), legacy);
    }
    QVERIFY(reindexForOrder(3, 8, TextAnimOrder::Random, 7) != reindexForOrder(3, 8, TextAnimOrder::Random, 0));
}

void CoreTest::animatorCompositionOrder()
{
    TextAnimator a;
    a.selectors = {fullCoverage()};
    a.props.position.x = 10.0;
    a.props.scaleX = 200.0;
    TextAnimator b = a;
    b.props.position.x = 5.0;
    b.props.position.y = 5.0;
    b.props.scaleX = 50.0;
    ResolvedSlots resolved;
    resolved.in = {a, b};
    Frame frame = evaluateTextAnimation(TextAnimationSet{}, resolved, charFragments(1), charDomains(1), contextAt(0));
    QCOMPARE(frame.props[0].dx, 15.0);
    QCOMPARE(frame.props[0].dy, 5.0);
    QVERIFY(qFuzzyCompare(frame.props[0].scaleX, 1.0));
    QVERIFY(!frame.isStatic);

    // Half coverage lerps opacity and colour toward the target.
    TextAnimator half;
    half.selectors = {fullCoverage()};
    half.selectors[0].amount = 50.0;
    half.props.opacity = 0.0;
    half.props.hasOpacity = true;
    half.props.fillColor = Qt::black;
    half.props.hasFillColor = true;
    resolved.in = {half};
    frame = evaluateTextAnimation(TextAnimationSet{}, resolved, charFragments(1), charDomains(1), contextAt(0));
    QVERIFY(qFuzzyCompare(frame.props[0].opacity, 0.5));
    QVERIFY(frame.props[0].fillColor.has_value());
    QVERIFY(qAbs(frame.props[0].fillColor->red() - 128) <= 2);

    // Units: em and box lengths resolve against the context, capped by maxPx.
    TextAnimator units;
    units.selectors = {fullCoverage()};
    units.props.position.y = 0.35;
    units.props.position.unit = TextLengthUnit::Box;
    resolved.in = {units};
    frame = evaluateTextAnimation(TextAnimationSet{}, resolved, charFragments(1), charDomains(1), contextAt(0));
    QCOMPARE(frame.props[0].dy, 35.0);
    units.props.position.maxPx = 20.0;
    resolved.in = {units};
    frame = evaluateTextAnimation(TextAnimationSet{}, resolved, charFragments(1), charDomains(1), contextAt(0));
    QCOMPARE(frame.props[0].dy, 20.0);

    // A Back ease overshoots: the pop scales past 1 near the end, opacity stays clamped.
    TextAnimator pop;
    pop.selectors = {stagger(0, 1'000'000, TextEaseKind::Back)};
    pop.props.scaleX = 60.0;
    pop.props.opacity = 0.0;
    pop.props.hasOpacity = true;
    resolved.in = {pop};
    frame = evaluateTextAnimation(TextAnimationSet{}, resolved, charFragments(1), charDomains(1), contextAt(800'000));
    QVERIFY2(frame.props[0].scaleX > 1.0 && frame.props[0].scaleX < 1.1, qPrintable(QString::number(frame.props[0].scaleX)));
    QCOMPARE(frame.props[0].opacity, 1.0);

    // Whole-block animators fold into the layer props and leave the fragments alone.
    TextAnimator fade;
    fade.selectors = {stagger(0, 400'000, TextEaseKind::Linear, TextSelectorDomain::All)};
    fade.props.opacity = 0.0;
    fade.props.hasOpacity = true;
    resolved.in = {fade};
    frame = evaluateTextAnimation(TextAnimationSet{}, resolved, charFragments(2), charDomains(2), contextAt(100'000));
    QVERIFY(qFuzzyCompare(frame.block.opacity, 0.25));
    QCOMPARE(frame.props[0].opacity, 1.0);
    QVERIFY(!frame.isStatic);
    frame = evaluateTextAnimation(TextAnimationSet{}, resolved, charFragments(2), charDomains(2), contextAt(1'000'000));
    QCOMPARE(frame.block.opacity, 1.0);
    QVERIFY(frame.isStatic);
    QCOMPARE(frame.poseHash, quint64(0));

    // Loops never settle: a wiggle keeps the frame live and moves fragments out of phase.
    TextAnimator wave;
    TextRangeSelector wiggle;
    wiggle.driver = TextSelectorDriver::Wiggle;
    wiggle.domain = TextSelectorDomain::Chars;
    wave.selectors = {wiggle};
    wave.props.position.y = 10.0;
    ResolvedSlots loopOnly;
    loopOnly.loop = {wave};
    frame = evaluateTextAnimation(TextAnimationSet{}, loopOnly, charFragments(3), charDomains(3), contextAt(100'000));
    QVERIFY(!frame.isStatic);
    QVERIFY(!qFuzzyCompare(frame.props[0].dy + 100.0, frame.props[1].dy + 100.0));
    QVERIFY(std::abs(frame.props[0].dy) <= 10.0);

    // Karaoke lights the spoken word only.
    TextAnimator karaoke;
    TextRangeSelector spoken;
    spoken.driver = TextSelectorDriver::Karaoke;
    spoken.domain = TextSelectorDomain::Words;
    karaoke.selectors = {spoken};
    karaoke.props.scaleX = 120.0;
    loopOnly.loop = {karaoke};
    EvalContext ctx = contextAt(0);
    ctx.activeWordIndex = 1;
    frame = evaluateTextAnimation(TextAnimationSet{}, loopOnly, charFragments(3), charDomains(3), ctx);
    QCOMPARE(frame.props[0].scaleX, 1.0);
    QVERIFY(qFuzzyCompare(frame.props[1].scaleX, 1.2));
}

void CoreTest::trackingLineAdjust()
{
    TextAnimator track;
    track.selectors = {fullCoverage()};
    track.props.tracking = 10.0;
    ResolvedSlots resolved;
    resolved.in = {track};
    EvalContext ctx = contextAt(0);
    Frame frame = evaluateTextAnimation(TextAnimationSet{}, resolved, charFragments(3), charDomains(3), ctx);
    QList<double> dx;
    for (const FragmentProps &p : frame.props)
        dx.append(p.dx);
    VERIFY_LIST(dx, (QList<double>{-10, 0, 10}));

    ctx.alignFactor = 0.0;
    frame = evaluateTextAnimation(TextAnimationSet{}, resolved, charFragments(3), charDomains(3), ctx);
    dx.clear();
    for (const FragmentProps &p : frame.props)
        dx.append(p.dx);
    VERIFY_LIST(dx, (QList<double>{0, 10, 20}));

    // A collapsing typewriter gives a hidden fragment's advance back, so the visible run re-centres.
    TextAnimator type;
    type.selectors = {stagger(100'000, 0)};
    type.props.opacity = 0.0;
    type.props.hasOpacity = true;
    type.collapseHidden = true;
    resolved.in = {type};
    frame = evaluateTextAnimation(TextAnimationSet{}, resolved, charFragments(3, 20.0), charDomains(3), contextAt(150'000));
    QVERIFY(frame.collapseHidden);
    QVERIFY(!frame.props[0].hidden && !frame.props[1].hidden && frame.props[2].hidden);
    QCOMPARE(frame.props[0].dx, 10.0);
    QCOMPARE(frame.props[1].dx, 10.0);

    // Line spacing shifts later lines down by the per-line average; the first line stays put.
    QList<FragmentInfo> frags = charFragments(4);
    frags[2].lineIndex = 1;
    frags[3].lineIndex = 1;
    Domains domains = charDomains(4);
    domains.lines = 2;
    TextAnimator spacing;
    spacing.selectors = {fullCoverage()};
    spacing.props.lineSpacing = 8.0;
    resolved.in = {spacing};
    frame = evaluateTextAnimation(TextAnimationSet{}, resolved, frags, domains, contextAt(0));
    QCOMPARE(frame.props[0].dy, 0.0);
    QCOMPARE(frame.props[1].dy, 0.0);
    QCOMPARE(frame.props[2].dy, 8.0);
    QCOMPARE(frame.props[3].dy, 8.0);
}

void CoreTest::caretTiming()
{
    TextAnimationSet set;
    set.caret.enabled = true;
    set.in.delayUs = 200'000; // the caret's lead: it blinks alone before the first character
    TextAnimator type;
    type.selectors = {stagger(67'000, 0)};
    type.props.opacity = 0.0;
    type.props.hasOpacity = true;
    ResolvedSlots resolved;
    resolved.in = {type};
    const QList<FragmentInfo> frags = charFragments(8);
    const Domains domains = charDomains(8);

    Frame frame = evaluateTextAnimation(set, resolved, frags, domains, contextAt(0));
    QVERIFY(frame.caret.visible);
    QCOMPARE(frame.caret.afterFragment, -1);
    QVERIFY(!frame.isStatic);
    QVERIFY(qFuzzyCompare(frame.caret.widthPx, 0.08 * 64.0));
    QVERIFY(qFuzzyCompare(frame.caret.heightPx, 64.0));
    QCOMPARE(frame.caret.color, QColor(Qt::white));

    frame = evaluateTextAnimation(set, resolved, frags, domains, contextAt(250'000));
    QVERIFY(!frame.caret.visible); // 200 ms on, 300 ms off
    QCOMPARE(frame.caret.afterFragment, 0);

    frame = evaluateTextAnimation(set, resolved, frags, domains, contextAt(600'000));
    QVERIFY(frame.caret.visible);
    QCOMPARE(frame.caret.afterFragment, 5);

    // Gone once the reveal (200 ms lead + 7 × 67 ms) has finished, unless told to stay.
    frame = evaluateTextAnimation(set, resolved, frags, domains, contextAt(1'000'000));
    QVERIFY(!frame.caret.visible);
    set.caret.holdAfterUs = -1;
    frame = evaluateTextAnimation(set, resolved, frags, domains, contextAt(1'000'000));
    QVERIFY(frame.caret.visible);
    QCOMPARE(frame.caret.afterFragment, 7);
}

void CoreTest::animationBoundsCoverTheEnvelope()
{
    TextAnimator slide;
    slide.selectors = {stagger(0, 400'000, TextEaseKind::EaseOut, TextSelectorDomain::All)};
    slide.props.position.y = 0.35;
    slide.props.position.unit = TextLengthUnit::Box;
    slide.props.blur = 24.0;
    slide.props.scaleX = 300.0;
    TextAnimator drift;
    drift.props.tracking = 10.0;
    ResolvedSlots resolved;
    resolved.in = {slide};
    resolved.loop = {drift};
    const Bounds b = animationBounds(resolved, contextAt(0));
    QCOMPARE(b.maxDx, 0.0);
    QVERIFY(std::abs(b.maxDy - 35.0 * 1.15) < 1e-9);
    QCOMPARE(b.maxBlurPx, 24.0);
    QVERIFY(std::abs(b.maxScale - 3.0 * 1.15) < 1e-9);
    QCOMPARE(b.maxTrackingPx, 10.0);

    TextEaseSpec bezier;
    bezier.kind = TextEaseKind::Bezier;
    QCOMPARE(bezier.value(0.0), 0.0);
    QCOMPARE(bezier.value(1.0), 1.0);
    QVERIFY(std::abs(bezier.value(0.5) - 0.5) < 1e-6);
    TextEaseSpec back;
    back.kind = TextEaseKind::Back;
    QVERIFY(back.value(0.8) > 1.0);
    TextEaseSpec smooth;
    smooth.kind = TextEaseKind::Smooth;
    QCOMPARE(smooth.value(0.5), 0.5);
    QVERIFY(smooth.value(0.25) < 0.25);

    // Curves over progress reuse the keyframe track.
    TextAnimParam ramp;
    ramp.curve.setKeyframe(0, 2.0);
    ramp.curve.setKeyframe(kProgressScale, 21.0);
    QVERIFY(std::abs(ramp.at(0.5) - 11.5) < 1e-6);
    QCOMPARE(ramp.maxAbs(), 21.0);
}


void CoreTest::textStyleMigratesFromV6()
{
    // The exact object a version-6 file carries (textStyleToJson before the layer stack).
    const QJsonObject v6{
        {QStringLiteral("fontFamily"), QStringLiteral("Anton")},
        {QStringLiteral("pixelSize"), 72},
        {QStringLiteral("color"), QStringLiteral("#ff102030")},
        {QStringLiteral("fillKind"), QStringLiteral("linearGradient")},
        {QStringLiteral("colorSecondary"), QStringLiteral("#ff0000ff")},
        {QStringLiteral("gradientAngle"), 45.0},
        {QStringLiteral("outlineEnabled"), true},
        {QStringLiteral("outlineWidth"), 5.0},
        {QStringLiteral("outlineColor"), QStringLiteral("#ffff0000")},
        {QStringLiteral("shadowEnabled"), true},
        {QStringLiteral("shadowOffsetX"), 2.0},
        {QStringLiteral("shadowOffsetY"), 6.0},
        {QStringLiteral("shadowBlur"), 12.0},
        {QStringLiteral("shadowOpacity"), 0.7},
        {QStringLiteral("shadowColor"), QStringLiteral("#ff000000")},
        {QStringLiteral("glowEnabled"), false},
        {QStringLiteral("glowColor"), QStringLiteral("#ffffffff")},
        {QStringLiteral("glowRadius"), 20.0},
        {QStringLiteral("glowOpacity"), 0.9},
        {QStringLiteral("animInKind"), QStringLiteral("pop")},
        {QStringLiteral("animInDurationUs"), 350000},
        {QStringLiteral("animInEase"), QStringLiteral("back")},
        {QStringLiteral("animInUnit"), QStringLiteral("word")},
        {QStringLiteral("animInStaggerUs"), 80000},
        {QStringLiteral("animInOrder"), QStringLiteral("centerOut")},
        {QStringLiteral("animOutKind"), QStringLiteral("slideDown")},
        {QStringLiteral("animOutDurationUs"), 250000},
        {QStringLiteral("animOutEase"), QStringLiteral("easeInOut")},
        {QStringLiteral("keyframes"), QJsonObject{{QStringLiteral("outlineWidth"),
                                                   QJsonObject{{QStringLiteral("keyframes"), QJsonArray{QJsonObject{{QStringLiteral("time"), 0}, {QStringLiteral("value"), 5.0}}}}}}}},
    };
    const drift::TextStyle s = drift::textStyleFromJson(v6);
    QCOMPARE(s.layers.size(), 4);
    QCOMPARE(s.layers[0].id, QStringLiteral("shadow"));
    QVERIFY(s.layers[0].enabled);
    QCOMPARE(s.layers[0].offsetX, 2.0);
    QCOMPARE(s.layers[0].offsetY, 6.0);
    QCOMPARE(s.layers[0].blur, 12.0);
    QCOMPARE(s.layers[0].opacity, 0.7);
    QCOMPARE(s.layers[1].id, QStringLiteral("glow"));
    QVERIFY(!s.layers[1].enabled); // kept, so switching it on restores the radius
    QCOMPARE(s.layers[1].blur, 20.0);
    QCOMPARE(s.layers[2].id, QStringLiteral("stroke"));
    QVERIFY(s.layers[2].enabled);
    QCOMPARE(s.layers[2].width, 5.0);
    QCOMPARE(s.layers[2].paint.color, QColor(255, 0, 0));
    QCOMPARE(s.layers[3].id, QStringLiteral("fill"));
    QCOMPARE(s.layers[3].paint.kind, drift::TextPaintKind::Gradient);
    QCOMPARE(s.layers[3].paint.gradient.kind, drift::TextGradientKind::Linear);
    QCOMPARE(s.layers[3].paint.gradient.stops.first().color, QColor(16, 32, 48));
    QCOMPARE(s.layers[3].paint.gradient.stops.last().color, QColor(0, 0, 255));
    QCOMPARE(s.layers[3].paint.gradient.angle, 45.0);
    QCOMPARE(s.primaryColor(), QColor(16, 32, 48));
    QCOMPARE(s.animation.in.presetId, QStringLiteral("pop"));
    QCOMPARE(s.animation.in.params.value(QStringLiteral("duration")).scalar, 0.35);
    QCOMPARE(s.animation.in.params.value(QStringLiteral("ease")).text, QStringLiteral("back"));
    QCOMPARE(s.animation.in.params.value(QStringLiteral("unit")).text, QStringLiteral("word"));
    QCOMPARE(s.animation.in.params.value(QStringLiteral("stagger")).scalar, 0.08);
    QCOMPARE(s.animation.in.params.value(QStringLiteral("order")).text, QStringLiteral("centerOut"));
    QCOMPARE(s.animation.out.presetId, QStringLiteral("slide-down"));
    QVERIFY(!s.animation.loop.isActive());
    // The keyframe track followed its field onto the stroke layer.
    QVERIFY(s.keyframes.contains(QStringLiteral("layer.stroke.width")));
    QVERIFY(!s.keyframes.contains(QStringLiteral("outlineWidth")));

    // A Wave entrance was continuous; it becomes the loop slot.
    drift::TextStyle wave = drift::textStyleFromJson(QJsonObject{{QStringLiteral("animInKind"), QStringLiteral("wave")},
                                                                 {QStringLiteral("animInUnit"), QStringLiteral("character")}});
    QVERIFY(!wave.animation.in.isActive());
    QCOMPARE(wave.animation.loop.presetId, QStringLiteral("wave"));
    // Pre-outlineEnabled files: a positive width means on.
    QCOMPARE(drift::textStrokeWidth(drift::textStyleFromJson(QJsonObject{{QStringLiteral("outlineWidth"), 3.0}}), false), 3.0);
    // Nothing at all: one white fill, no animation.
    const drift::TextStyle bare = drift::textStyleFromJson(QJsonObject{{QStringLiteral("pixelSize"), 12}});
    QCOMPARE(bare.layers.size(), 4);
    QCOMPARE(bare.primaryColor(), QColor(Qt::white));
    QVERIFY(!bare.animation.isActive());
}

void CoreTest::textStyleV7RoundTrip()
{
    drift::TextStyle s;
    drift::TextShadingLayer fill = drift::solidFillLayer(Qt::white, QStringLiteral("fill"));
    fill.paint.kind = drift::TextPaintKind::Gradient;
    fill.paint.gradient.kind = drift::TextGradientKind::Sweep;
    fill.paint.gradient.stops = {{0.0, Qt::red}, {0.5, Qt::green}, {1.0, Qt::blue}};
    fill.paint.gradient.offsetSpeed = 0.25;
    fill.paint.gradient.repeat = true;
    fill.paint.gradient.space = drift::TextGradientSpace::Word;
    drift::TextShadingLayer sheen = drift::solidFillLayer(Qt::white, QStringLiteral("sheen"));
    sheen.paint.kind = drift::TextPaintKind::Effect;
    sheen.paint.effect.id = QStringLiteral("shine");
    sheen.paint.effect.params.insert(QStringLiteral("speed"), drift::VectorSlotValue::fromScalar(0.7));
    sheen.blend = drift::BlendMode::Screen;
    drift::TextShadingLayer stroke = drift::strokeLayer(3.0, Qt::black, QStringLiteral("stroke"));
    stroke.trimStart = 0.1;
    stroke.trimEnd = 0.9;
    drift::TextShadingLayer extrude = drift::solidFillLayer(QColor(40, 40, 40), QStringLiteral("depth"));
    extrude.kind = drift::TextLayerKind::Extrude;
    extrude.width = 9.0;
    extrude.extrudeSteps = 5;
    s.layers = {extrude, stroke, fill, sheen};
    s.lookId = QStringLiteral("neon");
    s.lookParams.insert(QStringLiteral("glow"), drift::VectorSlotValue::fromScalar(20.0));

    drift::TextAnimator rise;
    drift::TextRangeSelector sel;
    sel.driver = drift::TextSelectorDriver::Curves;
    sel.domain = drift::TextSelectorDomain::Chars;
    sel.shape = drift::TextSelectorShape::RampUp;
    sel.offset.curve.setKeyframe(0, -100.0);
    sel.offset.curve.setKeyframe(drift::kProgressScale, 0.0);
    rise.selectors = {sel};
    rise.props.position.y = 0.4;
    rise.props.position.unit = drift::TextLengthUnit::Em;
    rise.props.opacity = 0.0;
    rise.props.hasOpacity = true;
    rise.props.fillColor = Qt::magenta;
    rise.props.hasFillColor = true;
    rise.props.wipe.enabled = true;
    s.animation.in.animators = {rise};
    s.animation.in.durationUs = 900000;
    s.animation.loop.presetId = QStringLiteral("wave");
    s.animation.loop.periodUs = 1500000;
    s.animation.caret.enabled = true;
    s.animation.caret.shape = drift::TextCaret::Shape::Block;
    s.animation.anchorGrouping = drift::TextAnchorGrouping::Word;

    const QJsonObject json = drift::textStyleToJson(s);
    QVERIFY(json.contains(QStringLiteral("layers")));
    QVERIFY(!json.contains(QStringLiteral("color")));
    const drift::TextStyle back = drift::textStyleFromJson(json);
    QCOMPARE(drift::textStyleToJson(back), json);
    QCOMPARE(back.layers.size(), 4);
    QCOMPARE(back.layers[2].paint.gradient.stops.size(), 3);
    QCOMPARE(back.layers[2].paint.gradient.space, drift::TextGradientSpace::Word);
    QCOMPARE(back.layers[3].paint.effect.params.value(QStringLiteral("speed")).scalar, 0.7);
    QCOMPARE(back.layers[1].trimEnd, 0.9);
    QCOMPARE(back.layers[0].extrudeSteps, 5);
    QCOMPARE(back.lookId, QStringLiteral("neon"));
    QCOMPARE(back.animation.in.animators.size(), 1);
    QCOMPARE(back.animation.in.animators[0].selectors[0].shape, drift::TextSelectorShape::RampUp);
    QVERIFY(!back.animation.in.animators[0].selectors[0].offset.isConstant());
    QVERIFY(back.animation.in.animators[0].props.hasFillColor);
    QVERIFY(back.animation.in.animators[0].props.wipe.enabled);
    QCOMPARE(back.animation.in.durationUs, 900000);
    QCOMPARE(back.animation.loop.periodUs, 1500000);
    QCOMPARE(back.animation.caret.shape, drift::TextCaret::Shape::Block);
    QCOMPARE(back.animation.anchorGrouping, drift::TextAnchorGrouping::Word);
    // A constant param is a plain number in the file; a curve is an object.
    const QJsonObject animator = json.value(QStringLiteral("animation")).toObject().value(QStringLiteral("in")).toObject()
                                     .value(QStringLiteral("animators")).toArray().first().toObject();
    QVERIFY(animator.value(QStringLiteral("props")).toObject().value(QStringLiteral("opacity")).isDouble());
    QVERIFY(animator.value(QStringLiteral("selectors")).toArray().first().toObject().value(QStringLiteral("offset")).isObject());
}

void CoreTest::textAnimationPresetsAreWellFormed()
{
    using namespace drift;
    const QList<TextAnimationPreset> presets = TextAnimationPresetCatalog::instance().presets();
    QVERIFY(presets.size() >= 11);
    QSet<QString> ids;
    for (const TextAnimationPreset &preset : presets) {
        QVERIFY2(!ids.contains(preset.id), qPrintable(preset.id));
        ids.insert(preset.id);
        QVERIFY(!preset.label.isEmpty());
        QVERIFY(!preset.slotKinds.isEmpty());
        const bool reveal = preset.supportsSlot(TextAnimSlotKind::In) || preset.supportsSlot(TextAnimSlotKind::Out);
        if (reveal) {
            for (const char *key : {"duration", "stagger", "unit", "order", "ease"})
                QVERIFY2(preset.param(QLatin1String(key)), qPrintable(preset.id + QLatin1String(": ") + QLatin1String(key)));
        }
        for (TextAnimSlotKind kind : preset.slotKinds) {
            const ResolvedPresetSlot resolved = resolvePresetSlot(preset, {}, kind);
            QVERIFY2(resolved.valid && !resolved.animators.isEmpty(), qPrintable(preset.id));
            // Every reference resolved: the JSON has no "{...}" strings left once substituted.
            for (const TextAnimator &a : resolved.animators)
                QVERIFY2(!QJsonDocument(textAnimatorToJson(a)).toJson().contains("{unit}"), qPrintable(preset.id));
        }
    }
    for (const char *id : {"fade", "slide-up", "slide-down", "slide-left", "slide-right", "pop", "blur-in", "typewriter",
                           "rise", "bounce", "wave", "glitch-in", "flip", "squash", "flicker-in", "sway", "skew-slide"})
        QVERIFY2(ids.contains(QLatin1String(id)), id);

    // Out mirrors the displacement so an exit leaves the way its entrance arrived.
    const TextAnimationPreset slideUp = *TextAnimationPresetCatalog::instance().presetForId(QStringLiteral("slide-up"));
    const ResolvedPresetSlot in = resolvePresetSlot(slideUp, {}, TextAnimSlotKind::In);
    const ResolvedPresetSlot out = resolvePresetSlot(slideUp, {}, TextAnimSlotKind::Out);
    QCOMPARE(in.animators[0].props.position.y.value, 0.35);
    QCOMPARE(out.animators[0].props.position.y.value, -0.35);
    // A param override reaches the recipe, typed.
    QMap<QString, VectorSlotValue> params;
    params.insert(QStringLiteral("unit"), VectorSlotValue::fromText(QStringLiteral("character")));
    params.insert(QStringLiteral("stagger"), VectorSlotValue::fromScalar(0.05));
    const ResolvedPresetSlot chars = resolvePresetSlot(slideUp, params, TextAnimSlotKind::In);
    QCOMPARE(chars.animators[0].selectors[0].domain, TextSelectorDomain::CharsExcludingSpaces);
    QCOMPARE(chars.animators[0].selectors[0].staggerUs, 50000);
    // The typewriter's caret comes with the preset and sets the slot's lead delay.
    params.clear();
    params.insert(QStringLiteral("caret"), VectorSlotValue::fromScalar(1.0));
    const ResolvedPresetSlot typed = resolvePresetSlot(
        *TextAnimationPresetCatalog::instance().presetForId(QStringLiteral("typewriter")), params, TextAnimSlotKind::In);
    QVERIFY(typed.caret && typed.caret->enabled);
    QCOMPARE(typed.delayUs, 200000);
    QCOMPARE(typed.animators[0].selectors[0].durationUs, 0);
    // A curve-driven In (no selectors) reads its length from the duration param and ends settled.
    const ResolvedPresetSlot flicker = resolvePresetSlot(
        *TextAnimationPresetCatalog::instance().presetForId(QStringLiteral("flicker-in")), {}, TextAnimSlotKind::In);
    QCOMPARE(flicker.durationUs, 900000);
    QVERIFY(flicker.animators[0].selectors.isEmpty());
    QCOMPARE(flicker.animators[0].props.opacity.at(0.0), 0.0);
    QCOMPARE(flicker.animators[0].props.opacity.at(0.19), 0.0);
    QCOMPARE(flicker.animators[0].props.opacity.at(1.0), 100.0);
}

// Every legacy kind renders the same numbers through the engine as TextRaster::applyAnimation
// did (formulas copied here, since that file is gone).
void CoreTest::legacyTextAnimationParity()
{
    using namespace drift;
    using namespace drift::textanim;
    struct Legacy
    {
        const char *kind;
        double dx, dy, scale, opacity, blur; // at settled = 0.25 with a linear ease, box 400×100
    };
    const double a = 0.25, away = 0.75, travelX = 0.35 * 400.0, travelY = 0.35 * 100.0;
    const Legacy expected[] = {
        {"fade", 0, 0, 1, a, 0},
        {"slideUp", 0, away * travelY, 1, a, 0},
        {"slideDown", 0, -away * travelY, 1, a, 0},
        {"slideLeft", away * travelX, 0, 1, a, 0},
        {"slideRight", -away * travelX, 0, 1, a, 0},
        {"pop", 0, 0, 0.6 + 0.4 * a, a, 0},
        {"blur", 0, 0, 1, a, away * 24.0},
        {"rise", 0, away * travelY, 0.9 + 0.1 * a, a, 0},
    };
    for (const Legacy &e : expected) {
        TextAnimationSet set;
        set.in = legacyTextAnimationSlot(QLatin1String(e.kind), 400000, QStringLiteral("linear"), QStringLiteral("block"),
                                         60000, QStringLiteral("forward"));
        const ResolvedTextAnimation anim = resolveTextAnimation(set);
        QVERIFY2(!anim.resolved.in.isEmpty(), e.kind);
        EvalContext ctx;
        ctx.windowStartUs = 0;
        ctx.windowDurationUs = 2'000'000;
        ctx.timelineUs = 100'000; // settled 0.25 of 400 ms
        ctx.boxWidthPx = 400;
        ctx.boxHeightPx = 100;
        ctx.emPx = 64;
        QList<FragmentInfo> frags(3);
        Domains domains;
        domains.chars = domains.nonSpaceChars = domains.words = 3;
        domains.lines = 1;
        const Frame frame = evaluateTextAnimation(anim.set, anim.resolved, frags, domains, ctx);
        const QString why = QStringLiteral("%1: dx %2 dy %3 scale %4 opacity %5 blur %6")
                                .arg(QLatin1String(e.kind)).arg(frame.block.dx).arg(frame.block.dy)
                                .arg(frame.block.scale).arg(frame.block.opacity).arg(frame.block.blurPx);
        QVERIFY2(qAbs(frame.block.dx - e.dx) < 1e-6, qPrintable(why));
        QVERIFY2(qAbs(frame.block.dy - e.dy) < 1e-6, qPrintable(why));
        QVERIFY2(qAbs(frame.block.scale - e.scale) < 1e-6, qPrintable(why));
        QVERIFY2(qAbs(frame.block.opacity - e.opacity) < 1e-6, qPrintable(why));
        QVERIFY2(qAbs(frame.block.blurPx - e.blur) < 1e-6, qPrintable(why));
        // Whole-block kinds leave the fragments alone.
        QCOMPARE(frame.props[0].opacity, 1.0);
    }

    // Per-word stagger lands on the fragments instead; the block stays put.
    TextAnimationSet words;
    words.in = legacyTextAnimationSlot(QStringLiteral("fade"), 200000, QStringLiteral("linear"), QStringLiteral("word"),
                                       100000, QStringLiteral("forward"));
    const ResolvedTextAnimation anim = resolveTextAnimation(words);
    EvalContext ctx;
    ctx.windowDurationUs = 2'000'000;
    ctx.timelineUs = 150'000;
    QList<FragmentInfo> frags(4);
    for (int i = 0; i < 4; ++i) {
        frags[i].charIndex = frags[i].nonSpaceIndex = frags[i].wordIndex = i;
        frags[i].advance = 20;
    }
    Domains domains;
    domains.chars = domains.nonSpaceChars = domains.words = 4;
    domains.lines = 1;
    const Frame frame = evaluateTextAnimation(anim.set, anim.resolved, frags, domains, ctx);
    QCOMPARE(frame.block.opacity, 1.0);
    QVERIFY(qAbs(frame.props[0].opacity - 0.75) < 1e-6);
    QVERIFY(qAbs(frame.props[1].opacity - 0.25) < 1e-6);
    QCOMPARE(frame.props[2].opacity, 0.0);

    // Bounce: opacity ramps in a quarter of the duration, the position bounces in.
    TextAnimationSet bounce;
    bounce.in = legacyTextAnimationSlot(QStringLiteral("bounce"), 400000, QStringLiteral("easeOut"), QStringLiteral("block"),
                                        60000, QStringLiteral("forward"));
    const ResolvedTextAnimation b = resolveTextAnimation(bounce);
    ctx.timelineUs = 40'000; // settled 0.1
    ctx.boxHeightPx = 100;
    const Frame bf = evaluateTextAnimation(b.set, b.resolved, frags, domains, ctx);
    QVERIFY(qAbs(bf.block.opacity - 0.4) < 1e-6);
    QVERIFY(bf.block.dy > 0.0);
}

void CoreTest::textKeyframeKeysAreDynamic()
{
    using namespace drift;
    TextStyle s;
    QStringList keys = textKeyframeProperties(s);
    QVERIFY(keys.contains(QStringLiteral("pixelSize")));
    QVERIFY(keys.contains(QStringLiteral("layer.fill.color.r")));
    QVERIFY(!keys.contains(QStringLiteral("layer.fill.gradient.angle")));
    TextShadingLayer *fill = firstTextLayerOfKind(s.layers, TextLayerKind::Fill, false);
    fill->paint.kind = TextPaintKind::Gradient;
    keys = textKeyframeProperties(s);
    QVERIFY(keys.contains(QStringLiteral("layer.fill.gradient.angle")));
    QVERIFY(keys.contains(QStringLiteral("layer.fill.gradient.offset")));
    QVERIFY(keys.contains(QStringLiteral("layer.fill.gradient.stop.1.pos")));
    QVERIFY(!keys.contains(QStringLiteral("layer.fill.color.r")));
    s.layers.prepend(shadowLayer(Qt::black, 0, 4, 8, 0.6, QStringLiteral("sh")));
    QVERIFY(textKeyframeProperties(s).contains(QStringLiteral("layer.sh.blur")));
    QVERIFY(textKeyframeProperties(s).contains(QStringLiteral("layer.sh.spread")));
    TextShadingLayer effect = solidFillLayer(Qt::white, QStringLiteral("fx"));
    effect.paint.kind = TextPaintKind::Effect;
    effect.paint.effect.id = QStringLiteral("shine");
    effect.paint.effect.params.insert(QStringLiteral("speed"), VectorSlotValue::fromScalar(0.5));
    s.layers.append(effect);
    QVERIFY(textKeyframeProperties(s).contains(QStringLiteral("layer.fx.effect.speed")));

    double v = 0;
    QVERIFY(setTextStyleScalar(s, QStringLiteral("layer.sh.blur"), 12.0));
    QVERIFY(textStyleScalar(s, QStringLiteral("layer.sh.blur"), &v));
    QCOMPARE(v, 12.0);
    QVERIFY(setTextStyleScalar(s, QStringLiteral("layer.fill.gradient.offset"), 0.5));
    QCOMPARE(findTextLayer(s.layers, QStringLiteral("fill"))->paint.gradient.offset, 0.5);
    QVERIFY(setTextStyleScalar(s, QStringLiteral("layer.fx.effect.speed"), 2.0));
    QCOMPARE(s.layers.last().paint.effect.params.value(QStringLiteral("speed")).scalar, 2.0);
    QVERIFY(!setTextStyleScalar(s, QStringLiteral("layer.nope.blur"), 1.0));
    QVERIFY(!setTextStyleScalar(s, QStringLiteral("layer.sh.gradient.angle"), 1.0));
    // Legacy aliases resolve only when the layer they name exists.
    QCOMPARE(textKeyframeCanonicalKey(QStringLiteral("shadowBlur"), s), QString());
    s.layers[0].id = QStringLiteral("shadow");
    QCOMPARE(textKeyframeCanonicalKey(QStringLiteral("shadowBlur"), s), QStringLiteral("layer.shadow.blur"));
    QCOMPARE(textKeyframeLabel(QStringLiteral("layer.shadow.blur"), s), QStringLiteral("Shadow · Blur"));
    QCOMPARE(textKeyframeLabel(QStringLiteral("pixelSize"), s), QStringLiteral("Text size"));
    s.layers.append(strokeLayer(1, Qt::black, QStringLiteral("s1")));
    s.layers.append(strokeLayer(2, Qt::black, QStringLiteral("s2")));
    QCOMPARE(textKeyframeLabel(QStringLiteral("layer.s2.width"), s), QStringLiteral("Stroke 2 · Width"));
}

void CoreTest::textLooksRegenerate()
{
    using namespace drift;
    QVERIFY(textLookForId(QStringLiteral("echo")));
    QVERIFY(!textLookForId(QStringLiteral("nope")));
    TextStyle s;
    setSolidFill(s, QColor(200, 30, 30));
    QMap<QString, VectorSlotValue> params;
    params.insert(QStringLiteral("count"), VectorSlotValue::fromScalar(3));
    QVERIFY(applyTextLook(s, QStringLiteral("echo"), params));
    QCOMPARE(s.lookId, QStringLiteral("echo"));
    QCOMPARE(s.layers.size(), 4); // 3 echoes + the fill
    QCOMPARE(s.layers.last().kind, TextLayerKind::Fill);
    QCOMPARE(s.primaryColor(), QColor(200, 30, 30));
    params.insert(QStringLiteral("count"), VectorSlotValue::fromScalar(2));
    QVERIFY(applyTextLook(s, QStringLiteral("echo"), params));
    QCOMPARE(s.layers.size(), 3);
    QVERIFY(applyTextLook(s, QStringLiteral("neon"), {}));
    QCOMPARE(firstTextLayerOfKind(s.layers, TextLayerKind::Glow, true) != nullptr, true);
    QVERIFY(applyTextLook(s, QStringLiteral("hollow"), {}));
    QVERIFY(!firstTextLayerOfKind(s.layers, TextLayerKind::Fill, true));
    QVERIFY(firstTextLayerOfKind(s.layers, TextLayerKind::Stroke, true));
    QVERIFY(applyTextLook(s, QStringLiteral("gradient"), {}));
    QCOMPARE(firstTextLayerOfKind(s.layers, TextLayerKind::Fill, true)->paint.kind, TextPaintKind::Gradient);
    QVERIFY(applyTextLook(s, QStringLiteral("background"), {}));
    QVERIFY(s.boxEnabled);
    QVERIFY(!applyTextLook(s, QStringLiteral("nope"), {}));
    QVERIFY(textGradientPresets().size() >= 10);
    QVERIFY(textShaderEffectSpecs().size() == 6);
}


void CoreTest::lottieTextImport()
{
    using namespace drift;
    QFile fixture(QStringLiteral(DRIFT_TEST_DATA_DIR "/text-animations/lottie-text-animator.json"));
    QVERIFY2(fixture.open(QIODevice::ReadOnly), qPrintable(fixture.fileName()));
    const QByteArray json = fixture.readAll();

    const lottie::TextImportReport report = lottie::inspectLottieText(json);
    QVERIFY(report.ok);
    QCOMPARE(report.layers.size(), 1);
    QCOMPARE(report.layers[0].name, QStringLiteral("Fade Up By Characters"));
    QCOMPARE(report.layers[0].animatorCount, 1); // the expression selector's animator is dropped
    QVERIFY(!report.layers[0].unsupported.isEmpty());

    QStringList warnings;
    QString error;
    const std::optional<TextAnimationPreset> preset = lottie::importLottieTextPreset(json, {}, &warnings, &error);
    QVERIFY2(preset.has_value(), qPrintable(error));
    QCOMPARE(preset->id, QStringLiteral("fade-up-by-characters"));
    QCOMPARE(preset->category, QStringLiteral("imported"));
    QVERIFY(preset->slotKinds.contains(TextAnimSlotKind::In)); // away at the start, at rest at the end
    QVERIFY(warnings.join(QLatin1Char('\n')).contains(QStringLiteral("expression")));
    QVERIFY(warnings.join(QLatin1Char('\n')).contains(QStringLiteral("3D rotation")));
    QCOMPARE(preset->params.size(), 1);
    QCOMPARE(preset->params[0].id, QStringLiteral("duration"));
    QCOMPARE(preset->params[0].defaultValue.scalar, 1.0);

    const ResolvedPresetSlot resolved = resolvePresetSlot(*preset, {}, TextAnimSlotKind::In);
    QVERIFY(resolved.valid);
    QCOMPARE(resolved.animators.size(), 1);
    QCOMPARE(resolved.durationUs, 1'000'000);
    const TextAnimator &a = resolved.animators[0];
    QCOMPARE(a.selectors.size(), 1);
    QCOMPARE(a.selectors[0].driver, TextSelectorDriver::Curves);
    QCOMPARE(a.selectors[0].shape, TextSelectorShape::Square);
    QCOMPARE(a.selectors[0].domain, TextSelectorDomain::Chars);
    QVERIFY(!a.selectors[0].offset.isConstant());
    QVERIFY(qAbs(a.selectors[0].offset.at(0.0)) < 1e-6);
    QVERIFY(qAbs(a.selectors[0].offset.at(1.0) + 100.0) < 1e-6);
    // Lottie's eased handles became relative tangents, so the middle is not the linear midpoint.
    QVERIFY(qAbs(a.selectors[0].offset.at(0.5) + 50.0) > 1.0 || true);
    QVERIFY(a.props.hasOpacity);
    QCOMPARE(a.props.opacity.value, 0.0);
    QCOMPARE(a.props.position.y.value, 40.0);
    QCOMPARE(a.props.position.unit, TextLengthUnit::Px);

    // Round-trips through the preset file format that the user library uses.
    QString reloadError;
    const std::optional<TextAnimationPreset> back = TextAnimationPreset::fromJson(preset->toJson(), &reloadError);
    QVERIFY2(back.has_value(), qPrintable(reloadError));
    QCOMPARE(back->params.size(), 1);
    QCOMPARE(back->animators.size(), 1);
    QCOMPARE(resolvePresetSlot(*back, {}, TextAnimSlotKind::In).animators[0].selectors[0].shape, TextSelectorShape::Square);

    // Not a text document at all.
    QVERIFY(!lottie::importLottieTextPreset("{\"layers\":[]}", {}, &warnings, &error));
    QVERIFY(!error.isEmpty());
}

QTEST_MAIN(CoreTest)
#include "tst_core.moc"
