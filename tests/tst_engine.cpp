#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QProcess>
#include <QScopeGuard>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <atomic>

#include <cmath>
#include <random>

#include "core/Clip.h"
#include "core/Project.h"
#include "engine/AudioMixer.h"
#include "engine/ClipReader.h"
#include "engine/Exporter.h"
#include "engine/CompositorFrameHistory.h"
#include "engine/AudioEffectCatalog.h"
#include "engine/audio/AudioEffectFactory.h"
#include "engine/audio/AudioEffectRack.h"
#include "engine/audio/ClipAudioRetimer.h"
#include "engine/AudioFileWriter.h"
#include "engine/AudioOnsets.h"
#include "engine/DeepFilterDenoiser.h"
#include "engine/EffectCatalog.h"
#include "engine/EffectPackageLoader.h"
#include "engine/EffectProcessor.h"
#include "engine/FaceTrack.h"
#include "engine/EmojiCatalog.h"
#include "engine/FontCatalog.h"
#include "engine/FrameCompositor.h"
#include "engine/TextRaster.h"
#include "engine/GpuEffectExecutor.h"
#include "engine/GpuPackageParse.h"

#include <QJsonDocument>
#include "engine/MaskApplier.h"
#include "engine/MatteWriter.h"
#include "engine/ReverseProxyCache.h"
#include "engine/ReverseRenderer.h"
#include "engine/ClipReaderPool.h"
#include "engine/MediaProbe.h"
#include "engine/TransitionCatalog.h"
#include "core/Transition.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/pixfmt.h>
}

class EngineTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void matteWriterRoundTripsThroughClipReader();
    void reverseRendererPlaysSourceBackwards();
    void reverseProxyLookupIsByContainmentAndSourceIdentity();
    void resolveVideoReadMirrorsTheClipOntoTheProxy();
    void faceTrackRoundTripsAndInterpolates();
    void faceTrackV2CarriesContoursAndPose();
    void faceTrackV1FileStillLoads();
    void smoothFaceTrackHandlesMissingBlocks();
    void applyFaceUniformsEmitsContourArrays();
    void colorParametersParseAndResolve();
    void beautyEffectsPassThroughWithoutContours();
    void emojiCatalogNeedsFontAddon();
    void emojiRasterisesGlyph();
    void effectProcessorPassthroughWithoutEffects();
    void effectProcessorBrightness();
    void clipReaderSequentialAndSeek();
    void clipReaderAppliesDisplayRotation_data();
    void clipReaderAppliesDisplayRotation();
    void reverseProxyKeepsDisplayRotation();
    void clipReaderAudioSequential();
    void compositorDefaultRenderStaysFullResolution();
    void compositorPreviewScaleRendersLowerResolution();
    void compositorPreviewScaleMapsProjectPixelLayout();
    void compositorAppliesFaceWarpFromBakedTrack();
    void compositorAppliesMultiplyBlendMode();
    void compositorAnimatesKeyedEffectParam();
    void compositorRendersShapeClip();
    void compositorSkipsClipBeingEdited();
    void adjustmentEffectContrastCatalogEntry();
    void effectPresetStableIds();
    void effectPresetCatalogIncludesStylizePresets();
    void effectBrowserCategories();
    void effectGraphTemplateSubstitution();
    void compositorOnlyPresetsUseCompositorPath();
    void effectPackageLoaderParsesGaussianBlur();
    void effectPackageLoaderRejectsReservedUniform();
    void effectPackageLoaderRejectsMissingShader();
    void gpuGaussianBlurChangesImage();
    void gpuMultiPassPreservesVerticalOrientation();
    void gpuBrokenShaderPassthrough();
    void rgbSplitZeroAmountPassthrough();
    void rgbSplitShiftsColorChannels();
    void blockGlitchDeterministicForSameTimeAndSeed();
    void blockGlitchChangesWithTimelineTime();
    void scanlineGlitchZeroStrengthPassthrough();
    void scanlineGlitchDeterministicAtFixedTime();
    void scanlineGlitchVisualChangeAtNonzeroSettings();
    void vhsCrtZeroSettingsPassthrough();
    void vhsCrtNonzeroModifiesOutput();
    void vhsCrtDeterministicAtFixedTime();
    void bloomGlowZeroIntensityPassthrough();
    void bloomGlowDarkFrameUnchanged();
    void bloomGlowBrightSpotBleedsToNeighbors();
    void rippleWaterZeroAmplitudePassthrough();
    void rippleWaterNonzeroDisplacementChangesOutput();
    void edgeNeonZeroIntensityUnchanged();
    void edgeNeonHighContrastRectangleGlow();
    void digitalGlitchZeroIntensityUnchanged();
    void digitalGlitchDeterministicForFixedTimeAndSeed();
    void filmBurnZeroIntensityUnchanged();
    void filmBurnAddsWarmLeakContribution();
    void timeEchoBlendDeterministic();
    void timeEchoBlendIncludesHistoryContribution();
    void timeEchoDeterministicAtFixedTimelineTime();
    void timeEchoBlendsPriorVideoFrames();
    void shockwavePulseZeroStrengthPassthrough();
    void shockwavePulseChangesPixelsNearWavefront();
    void compositorCrossfadeBetweenShapeClips();
    void compositorDipToBlackMidpointIsBlack();
    void compositorWipeRightRevealsIncomingClip();
    void transitionCatalogLoadsAllPackages();
    void gpuTransitionBindsBothSources();
    void brokenTransitionShaderFallsBackToCrossfade();
    void transitionRenderingIsDeterministic();
    void textClipRendersInsideTransition();
    void fontCatalogLoadsFamilies();
    void fontForStyleResolvesRequestedFace();
    void textRasterIsCached();
    void textDecorationsAreNotCropped();
    void wordAccentRecoloursChosenWords();
    void karaokeAccentFollowsThePlayhead();
    void accentSizeScaleWidensTheBlock();
    void everyStylePackRenders();
    void heavyWeightsRenderSolidGlyphs();
    void textClipCarriesGpuEffects();
    void textAnimationFadesAndSlides();
    void clipBodyAnimationFadeRampsOpacity();
    void maskApplierEllipseMasksCorners();
    void maskFeatherProducesSoftEdge();
    void maskRotationAppliesToFreeform();
    void maskFreeformHonoursTangents();
    void compositorFoldsMaskStackLikeTheCpuPath();
    void maskImageMediaCoversByItsOwnRect();
    void compositorAnimatesMaskAcrossTheClip();
    void maskViewRendersCoverageAndNeverExports();
    void exporterProducesPlayableFileWithBackground();
    void exporterProducesAudioOnlyMp3();
    void exporterTagsSdrBt709ColorMetadata();
    void exporterDefaultCrfIsNearLosslessForH264();
    void exporterSettingsFromMapValidatesFrameRate();
    void exporterDefaultsToProjectFrameRate();
    void exporterHonoursExportFrameRateOverride();
    void exporterHonoursWorkAreaRange();
    void exporterProducesAnimatedGif();
    void exporterSupportsNtscFrameRates();
    void exporterFrameRateAddsRealDetailToSlowedClips();
    void mixerHasNoBlockBoundaryDropout();
    void mixerSurvivesConcurrentClipAudioReset();
    void retimedClipAudioIsNotSilent();
    void retimedAudioPreservesPitch();
    void retimedAudioLengthTracksTimeline();
    void retimedAudioSurvivesBlockSizeChanges();
    void reversedRetimedAudioIsNotSilent();
    void rampedSpeedCurveRetimesAudioClip();
    void clipAudioRetimerStreamsSyntheticSource();
    void audioEffectCatalogLoadsPackages();
    void audioEffectFactoryBuildsEveryCatalogEntry();
    void audioEffectChainAltersSignal();
    void audioEffectChainBypassesUnknownEffect();
    void audioEffectStreamIsContinuousAcrossBlocks();
    void audioEffectFlangerProcessesSignal();
    void audioEffectRackPrimingAlignsLatentStages();
    void pitchShiftMovesPitchInTheRightDirection();
    void audioEffectRackParameterChangeIsContinuous();
    void onsetsDetectClickTrackTempo();
    void onsetsIgnoreSilence();
    void denoiseAuxiliaryConstantsRoundTrip();
    void denoisePreservesLengthAndSilence();
    void denoiseRemovesBroadbandNoise();
    void denoiseHasNoSeamAcrossWindows();
    void audioFileWriterRoundTripsThroughClipReader();

private:
    static QString makeColorSegmentsVideo(QTemporaryDir &dir);
    static QString makeRotatedHalvesVideo(QTemporaryDir &dir, int displayDegrees);
    static QString makeToneAudio(QTemporaryDir &dir);
};

void EngineTest::initTestCase()
{
    // Several subsystems here write into QStandardPaths::AppDataLocation (reversed proxies, the
    // matte and denoise caches). Test mode keeps a test run out of the developer's real app data.
    QStandardPaths::setTestModeEnabled(true);

    const QString effectsDir = QString::fromUtf8(DRIFT_TEST_EFFECTS_DIR);
    QVERIFY2(QDir(effectsDir).exists(), qPrintable(effectsDir));
    QStringList effectRoots{effectsDir};
    const QString addonEffectsDir = QString::fromUtf8(DRIFT_TEST_ADDON_EFFECTS_DIR);
    if (QDir(addonEffectsDir).exists())
        effectRoots.append(addonEffectsDir);
    reloadEffectCatalog(effectRoots);

    const QString transitionsDir = QString::fromUtf8(DRIFT_TEST_TRANSITIONS_DIR);
    QVERIFY2(QDir(transitionsDir).exists(), qPrintable(transitionsDir));
    reloadTransitionCatalog({transitionsDir});

    // The font bundle is fetched rather than committed, so an offline checkout legitimately has
    // none. The font tests skip in that case rather than fail.
    reloadFontCatalog({QString::fromUtf8(DRIFT_TEST_FONTS_DIR)});

    const QString audioEffectsDir = QString::fromUtf8(DRIFT_TEST_AUDIO_EFFECTS_DIR);
    QVERIFY2(QDir(audioEffectsDir).exists(), qPrintable(audioEffectsDir));
    reloadAudioEffectCatalog({audioEffectsDir});
}

// Without the emoji-font addon there is nothing to draw with, and offering a picker full of tofu
// is worse than offering none — so the catalog stays empty rather than falling back to the system.
void EngineTest::emojiCatalogNeedsFontAddon()
{
    QTemporaryDir empty;
    QVERIFY(empty.isValid());
    reloadEmojiCatalog({empty.path()});

    QVERIFY(emojiFontFamily().isEmpty());
    QVERIFY(emojiCatalog().isEmpty());
    QVERIFY(emojiGroups().isEmpty());
    QVERIFY(emojiImagePath(QString::fromUtf8("\xF0\x9F\x98\x80")).isEmpty());
}

void EngineTest::emojiRasterisesGlyph()
{
    // Like the font bundle, the emoji font is an addon rather than a checked-in asset.
    reloadEmojiCatalog({QString::fromUtf8(DRIFT_TEST_EMOJI_FONT_DIR)});
    if (emojiFontFamily().isEmpty())
        QSKIP("No emoji font available");

    QVERIFY(!emojiCatalog().isEmpty());
    QVERIFY(!emojiGroups().isEmpty());

    const QString grinning = QString::fromUtf8("\xF0\x9F\x98\x80");
    const QString path = emojiImagePath(grinning);
    QVERIFY(!path.isEmpty());

    QImage image(path);
    QVERIFY(!image.isNull());
    QCOMPARE(image.size(), QSize(160, 160));

    // A glyph that failed to render still writes a valid, entirely transparent PNG.
    bool painted = false;
    for (int y = 0; y < image.height() && !painted; ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(image.pixel(x, y)) > 0) {
                painted = true;
                break;
            }
        }
    }
    QVERIFY(painted);

    reloadEmojiCatalog();
}

// The matte is written by us but read back by the ordinary video path, so the two ends have to
// agree on codec, pixel format and time base. A mismatch shows up as a mask that decodes black
// The sidecar is what preview and export both read, so a rounding or indexing slip here shows up
// as a warp that lags the face rather than as an error.
void EngineTest::faceTrackRoundTripsAndInterpolates()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("track.json"));

    drift::FaceTrack track;
    track.fps = 30;
    track.startSrcUs = drift::secondsToUs(1.0);
    for (int i = 0; i < 3; ++i) {
        drift::FaceAnchors a;
        a.valid = true;
        a.faceCenter = QPointF(0.25 + 0.25 * i, 0.5);
        a.leftEye = QPointF(0.2 + 0.25 * i, 0.4);
        a.faceRx = 0.1;
        a.faceRy = 0.12;
        a.angle = 0.2;
        a.eyeRadius = 0.02;
        a.score = 0.9;

        drift::FaceTrackFrame frame;
        frame.faces.append(a);
        // A second slot that drops out in the middle: sampling it there must report no face
        // rather than interpolating across the gap.
        drift::FaceAnchors second = a;
        second.valid = (i != 1);
        second.faceCenter = QPointF(0.8, 0.3);
        frame.faces.append(second);
        track.frames.append(frame);
    }

    QString error;
    QVERIFY2(drift::writeFaceTrack(path, track, &error), qPrintable(error));

    drift::FaceTrack loaded;
    QVERIFY2(drift::readFaceTrack(path, &loaded, &error), qPrintable(error));
    QCOMPARE(loaded.fps, 30);
    QCOMPARE(loaded.startSrcUs, drift::secondsToUs(1.0));
    QCOMPARE(loaded.frames.size(), 3);

    // Exactly on frame 1.
    const drift::FaceAnchors onFrame = loaded.sample(drift::kUsPerSecond / 30, 0);
    QVERIFY(onFrame.valid);
    QVERIFY(qAbs(onFrame.faceCenter.x() - 0.5) < 1e-4);

    // Halfway between frames 0 and 1 — the whole point of interpolating rather than snapping.
    const drift::FaceAnchors between = loaded.sample(drift::kUsPerSecond / 60, 0);
    QVERIFY(between.valid);
    QVERIFY(qAbs(between.faceCenter.x() - 0.375) < 1e-4);

    // Past the end clamps to the last frame instead of falling off.
    const drift::FaceAnchors after = loaded.sample(drift::secondsToUs(10.0), 0);
    QVERIFY(after.valid);
    QVERIFY(qAbs(after.faceCenter.x() - 0.75) < 1e-4);

    // The gap in slot 1: neither neighbour pair may produce a face.
    QVERIFY(!loaded.sample(drift::kUsPerSecond / 60, 1).valid);
    QVERIFY(!loaded.sample(drift::kUsPerSecond / 30, 1).valid);

    // A slot that was never baked is simply absent.
    QVERIFY(!loaded.sample(0, 3).valid);
}

namespace {

// Anchors with every field a v2 sidecar carries, so the round-trip tests actually exercise the
// contour and pose blocks rather than defaults.
drift::FaceAnchors makeFullAnchors(double shift)
{
    drift::FaceAnchors a;
    a.valid = true;
    a.faceCenter = QPointF(0.4 + shift, 0.5);
    a.leftEye = QPointF(0.35 + shift, 0.45);
    a.rightEye = QPointF(0.45 + shift, 0.45);
    a.faceRx = 0.1;
    a.faceRy = 0.12;
    a.angle = 0.1;
    a.eyeRadius = 0.02;
    a.score = 0.9;

    a.contour.reserve(drift::contour::kTotalPoints);
    for (int i = 0; i < drift::contour::kTotalPoints; ++i)
        a.contour.append(QPointF(0.3 + shift + i * 0.001, 0.4 + i * 0.002));
    a.hasContours = true;
    a.cheekLeft = QPointF(0.33 + shift, 0.52);
    a.cheekRight = QPointF(0.47 + shift, 0.52);

    a.hasPose = true;
    a.poseQx = 0.0;
    a.poseQy = 0.0;
    a.poseQz = std::sin(0.15);
    a.poseQw = std::cos(0.15);
    a.poseScale = 0.08;
    a.poseOx = 0.4 + shift;
    a.poseOy = 0.45;
    a.poseOz = 0.01;
    return a;
}

} // namespace

void EngineTest::faceTrackV2CarriesContoursAndPose()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("v2.json"));

    drift::FaceTrack track;
    track.fps = 30;
    for (int i = 0; i < 2; ++i) {
        drift::FaceTrackFrame frame;
        frame.faces.append(makeFullAnchors(0.1 * i));
        track.frames.append(frame);
    }

    QString error;
    QVERIFY2(drift::writeFaceTrack(path, track, &error), qPrintable(error));
    drift::FaceTrack loaded;
    QVERIFY2(drift::readFaceTrack(path, &loaded, &error), qPrintable(error));

    const drift::FaceAnchors &a = loaded.frames.at(0).faces.at(0);
    QVERIFY(a.hasContours);
    QCOMPARE(a.contour.size(), drift::contour::kTotalPoints);
    // The contour block is quantized to uint16 over a range of 4.0, so a point is good to about
    // 6e-5 — finer than the five-decimal rounding the plain fields already use.
    for (int i = 0; i < drift::contour::kTotalPoints; ++i) {
        QVERIFY(qAbs(a.contour.at(i).x() - (0.3 + i * 0.001)) < 1e-4);
        QVERIFY(qAbs(a.contour.at(i).y() - (0.4 + i * 0.002)) < 1e-4);
    }
    QVERIFY(qAbs(a.cheekLeft.x() - 0.33) < 1e-4);
    QVERIFY(a.hasPose);
    QVERIFY(qAbs(a.poseQz - std::sin(0.15)) < 1e-6);
    QVERIFY(qAbs(a.poseScale - 0.08) < 1e-6);

    // Interpolating between the two frames keeps both blocks and renormalizes the quaternion.
    const drift::FaceAnchors mid = loaded.sample(drift::kUsPerSecond / 60, 0);
    QVERIFY(mid.valid);
    QVERIFY(mid.hasContours);
    QCOMPARE(mid.contour.size(), drift::contour::kTotalPoints);
    QVERIFY(qAbs(mid.contour.at(0).x() - 0.35) < 1e-3);
    QVERIFY(mid.hasPose);
    const double norm = std::sqrt(mid.poseQx * mid.poseQx + mid.poseQy * mid.poseQy
                                 + mid.poseQz * mid.poseQz + mid.poseQw * mid.poseQw);
    QVERIFY(qAbs(norm - 1.0) < 1e-6);

    // Sidecars are embedded in every project bundle, so their size is a real cost. A minute of
    // single-face 30fps footage must stay near a megabyte; if this trips, something stopped being
    // rounded or the contour blob stopped being packed.
    drift::FaceTrack minute;
    minute.fps = 30;
    for (int i = 0; i < 1800; ++i) {
        drift::FaceTrackFrame frame;
        frame.faces.append(makeFullAnchors(0.0001 * i));
        minute.frames.append(frame);
    }
    const QString bigPath = dir.filePath(QStringLiteral("minute.json"));
    QVERIFY2(drift::writeFaceTrack(bigPath, minute, &error), qPrintable(error));
    const qint64 bytes = QFileInfo(bigPath).size();
    QVERIFY2(bytes < 2'400'000,
             qPrintable(QStringLiteral("sidecar grew to %1 bytes per minute per face").arg(bytes)));
}

// The reason the format bump is not a hard break: an existing sidecar still drives every warp
// effect, and only the makeup effects see that they have nothing to work with.
void EngineTest::faceTrackV1FileStillLoads()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("v1.json"));

    // Written by hand in the old format — a bare 24-number array per face — because the point is
    // to prove the reader copes with files this build can no longer produce.
    const QByteArray v1 =
        "{\"version\":1,\"fps\":30,\"startSrcUs\":0,\"frames\":["
        "[[1,0.2,0.4,0.3,0.4,0.25,0.45,0.25,0.5,0.22,0.5,0.28,0.5,0.25,0.6,0.25,0.3,0.25,0.5,"
        "0.1,0.12,0.2,0.02,0.9]],"
        "[[1,0.3,0.4,0.4,0.4,0.35,0.45,0.35,0.5,0.32,0.5,0.38,0.5,0.35,0.6,0.35,0.3,0.35,0.5,"
        "0.1,0.12,0.2,0.02,0.9]]]}";
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(v1);
    f.close();

    QString error;
    drift::FaceTrack loaded;
    QVERIFY2(drift::readFaceTrack(path, &loaded, &error), qPrintable(error));
    QCOMPARE(loaded.frames.size(), 2);

    const drift::FaceAnchors &a = loaded.frames.at(0).faces.at(0);
    QVERIFY(a.valid);
    QVERIFY(qAbs(a.faceCenter.x() - 0.25) < 1e-6);
    QVERIFY(!a.hasContours);
    QVERIFY(!a.hasPose);
    QVERIFY(a.contour.isEmpty());

    // Still interpolates, so the warp effects are unaffected.
    const drift::FaceAnchors mid = loaded.sample(drift::kUsPerSecond / 60, 0);
    QVERIFY(mid.valid);
    QVERIFY(qAbs(mid.faceCenter.x() - 0.30) < 1e-4);
    QVERIFY(!mid.hasContours);

    // A version from the future is still refused, since we cannot guess what it holds.
    QFile future(dir.filePath(QStringLiteral("future.json")));
    QVERIFY(future.open(QIODevice::WriteOnly));
    future.write("{\"version\":99,\"fps\":30,\"frames\":[]}");
    future.close();
    drift::FaceTrack unused;
    QVERIFY(!drift::readFaceTrack(dir.filePath(QStringLiteral("future.json")), &unused, &error));
}

// Contours and pose must average only across frames that have them, or a partly re-scanned clip
// produces a half-length mask.
void EngineTest::smoothFaceTrackHandlesMissingBlocks()
{
    drift::FaceTrack track;
    track.fps = 30;
    for (int i = 0; i < 5; ++i) {
        drift::FaceTrackFrame frame;
        drift::FaceAnchors a = makeFullAnchors(0.0);
        // Jitter the centre so smoothing has something to do.
        a.faceCenter = QPointF(0.4 + (i % 2 ? 0.02 : -0.02), 0.5);
        // The middle frame carries no contours and no pose, as a v1-era frame would.
        if (i == 2) {
            a.contour.clear();
            a.hasContours = false;
            a.hasPose = false;
        }
        frame.faces.append(a);
        track.frames.append(frame);
    }

    drift::smoothFaceTrack(&track);

    for (int i = 0; i < 5; ++i) {
        const drift::FaceAnchors &a = track.frames.at(i).faces.at(0);
        QVERIFY(a.valid);
        if (i == 2) {
            QVERIFY(!a.hasContours);
            QVERIFY(a.contour.isEmpty());
            QVERIFY(!a.hasPose);
        } else {
            QVERIFY(a.hasContours);
            QCOMPARE(a.contour.size(), drift::contour::kTotalPoints);
            QVERIFY(a.hasPose);
            const double norm = std::sqrt(a.poseQx * a.poseQx + a.poseQy * a.poseQy
                                         + a.poseQz * a.poseQz + a.poseQw * a.poseQw);
            QVERIFY(qAbs(norm - 1.0) < 1e-6);
        }
    }

    // The jitter is gone from the interior frames, which is what smoothing is for.
    QVERIFY(qAbs(track.frames.at(2).faces.at(0).faceCenter.x() - 0.4) < 0.015);
}

// Contour loops travel as array uniforms rather than 256 named scalars; a v1 anchor must emit none
// of them and must leave every pre-existing uniform exactly as it was.
void EngineTest::applyFaceUniformsEmitsContourArrays()
{
    QMap<QString, QVariant> params;
    params.insert(QStringLiteral("faceIndex"), 0);
    drift::applyFaceUniforms(&params, {makeFullAnchors(0.0)});

    QCOMPARE(params.value(QStringLiteral("u_faceValid")).toDouble(), 1.0);
    QCOMPARE(params.value(QStringLiteral("u_faceHasContours")).toDouble(), 1.0);
    // faceIndex selects a slot; it is not a uniform and must be consumed.
    QVERIFY(!params.contains(QStringLiteral("faceIndex")));

    const struct { const char *name; int count; } loops[] = {
        {"u_faceOval", 36},      {"u_faceLipOuter", 20}, {"u_faceLipInner", 20},
        {"u_faceEyeLeft", 16},   {"u_faceEyeRight", 16}, {"u_faceBrowLeft", 10},
        {"u_faceBrowRight", 10},
    };
    for (const auto &loop : loops) {
        const QVariant v = params.value(QLatin1String(loop.name));
        QVERIFY2(v.canConvert<drift::GpuFloatArray>(), loop.name);
        const auto array = v.value<drift::GpuFloatArray>();
        QCOMPARE(array.tupleSize, 2);
        QCOMPARE(array.values.size(), loop.count * 2);
    }

    QCOMPARE(params.value(QStringLiteral("u_facePoseValid")).toDouble(), 1.0);
    // The pose reaches shaders as a basis, and a frontal-ish head must not come back mirrored.
    QVERIFY(params.value(QStringLiteral("u_facePoseRightX")).toDouble() > 0.9);

    // A v1 anchor: the warp uniforms are all still there, the contour arrays are all absent.
    drift::FaceAnchors legacy;
    legacy.valid = true;
    legacy.faceCenter = QPointF(0.5, 0.5);
    legacy.faceRx = 0.1;
    QMap<QString, QVariant> legacyParams;
    legacyParams.insert(QStringLiteral("faceIndex"), 0);
    drift::applyFaceUniforms(&legacyParams, {legacy});

    QCOMPARE(legacyParams.value(QStringLiteral("u_faceValid")).toDouble(), 1.0);
    QCOMPARE(legacyParams.value(QStringLiteral("u_faceCenterX")).toDouble(), 0.5);
    QCOMPARE(legacyParams.value(QStringLiteral("u_faceRx")).toDouble(), 0.1);
    QCOMPARE(legacyParams.value(QStringLiteral("u_faceHasContours")).toDouble(), 0.0);
    QCOMPARE(legacyParams.value(QStringLiteral("u_facePoseValid")).toDouble(), 0.0);
    for (const auto &loop : loops)
        QVERIFY2(!legacyParams.contains(QLatin1String(loop.name)), loop.name);
}

void EngineTest::colorParametersParseAndResolve()
{
    const auto parse = [](const QByteArray &json, QList<drift::EffectParamSpec> *out,
                          QString *error) {
        const QJsonArray params = QJsonDocument::fromJson(json).array();
        return GpuPackageParse::parseParameters(params, out, /*gpuBackend=*/true, error);
    };

    QList<drift::EffectParamSpec> specs;
    QString error;
    QVERIFY2(parse(R"([{"identifier":"shade","type":"color","defaultValue":"#B03048"}])", &specs,
                   &error),
             qPrintable(error));
    QCOMPARE(specs.size(), 1);
    QVERIFY(specs.at(0).isColor());
    QVERIFY(!specs.at(0).isBoolean());
    // Normalized at parse time so the swatch, the project file and the uniform agree on one form.
    QCOMPARE(specs.at(0).defaultColorHex, QStringLiteral("#b03048"));
    QCOMPARE(specs.at(0).defaultVariant().toString(), QStringLiteral("#b03048"));
    QCOMPARE(specs.at(0).typeName(), QStringLiteral("color"));

    // Alpha is dropped rather than silently carried into a vec3.
    specs.clear();
    QVERIFY(parse(R"([{"identifier":"shade","type":"color","defaultValue":"#80b03048"}])", &specs,
                  &error));
    QCOMPARE(specs.at(0).defaultColorHex, QStringLiteral("#b03048"));

    // A malformed default is a package error, not a silent black.
    specs.clear();
    QVERIFY(!parse(R"([{"identifier":"shade","type":"color","defaultValue":"crimson"}])", &specs,
                   &error));
    QVERIFY(error.contains(QStringLiteral("invalid colour")));
    specs.clear();
    QVERIFY(!parse(R"([{"identifier":"shade","type":"color","defaultValue":0.5}])", &specs, &error));

    // A stale numeric value on a colour key — from a hand-edited project, or a package that changed
    // a parameter's type — must not reach the shader, where it would bind as black.
    const EffectPresetEntry *def = effectDefForId(QStringLiteral("face_lipstick"));
    if (!def)
        QSKIP("face_lipstick package not available (drift-addons staging missing)");
    drift::Effect effect;
    effect.catalogId = def->meta.id;
    effect.parameters.insert(QStringLiteral("shade"), 0.7);
    const QMap<QString, QVariant> resolved = resolvedEffectParameters(effect, *def);
    QCOMPARE(resolved.value(QStringLiteral("shade")).typeId(), QMetaType::QString);
    QCOMPARE(resolved.value(QStringLiteral("shade")).toString(), QStringLiteral("#b03048"));

    // A legitimate override still wins.
    effect.parameters.insert(QStringLiteral("shade"), QStringLiteral("#123456"));
    QCOMPARE(resolvedEffectParameters(effect, *def).value(QStringLiteral("shade")).toString(),
             QStringLiteral("#123456"));
}

// Every beauty package must pass the frame through untouched when the clip has no contours, or an
// un-rescanned clip looks broken rather than merely un-scanned.
void EngineTest::beautyEffectsPassThroughWithoutContours()
{
    if (!GpuEffectExecutor::instance().isAvailable())
        QSKIP("GPU effect executor unavailable");

    const QStringList ids = {QStringLiteral("face_lipstick"),   QStringLiteral("face_blush"),
                             QStringLiteral("face_teeth_whiten"), QStringLiteral("face_eyeliner"),
                             QStringLiteral("face_eyeshadow"),  QStringLiteral("face_brow_tint"),
                             QStringLiteral("face_eye_color"),  QStringLiteral("face_beautify")};
    if (!effectDefForId(ids.first()))
        QSKIP("beauty packages not available (drift-addons staging missing)");

    QImage source(64, 64, QImage::Format_RGBA8888);
    source.fill(QColor(180, 140, 130));

    // Valid, but from a v1 sidecar: no contours.
    drift::FaceAnchors legacy;
    legacy.valid = true;
    legacy.faceCenter = QPointF(0.5, 0.5);
    legacy.leftEye = QPointF(0.4, 0.4);
    legacy.rightEye = QPointF(0.6, 0.4);
    legacy.faceRx = 0.25;
    legacy.faceRy = 0.3;
    legacy.eyeRadius = 0.03;

    for (const QString &id : ids) {
        const EffectPresetEntry *def = effectDefForId(id);
        QVERIFY2(def, qPrintable(id));
        QVERIFY2(def->needsFace, qPrintable(id));

        drift::Effect effect;
        effect.catalogId = id;
        const QImage out = EffectProcessor::applyEffects(source, {effect}, 0, {legacy});
        QVERIFY2(!out.isNull(), qPrintable(id));
        QCOMPARE(out.size(), source.size());
        QVERIFY2(out == source, qPrintable(QStringLiteral("%1 altered a contour-less frame").arg(id)));
    }
}

// or lands on the wrong frame — silent, and only visible in the composite.
void EngineTest::matteWriterRoundTripsThroughClipReader()
{
    // MatteWriter encodes lossless H.264 and nothing else, so an LGPL FFmpeg (no x264) has
    // nothing to run this against. Drift's own packages ship a GPL build; this is for anyone
    // building against a distro's LGPL one.
    if (!Exporter::videoCodecById(QStringLiteral("h264")).value(QStringLiteral("available")).toBool())
        QSKIP("No H.264 encoder available in this FFmpeg build");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("matte.mp4"));
    const QSize size(320, 240);
    const int frames = 10;

    drift::MatteWriter writer;
    QString error;
    QVERIFY2(writer.open(path, size, 30, 1, &error), qPrintable(error));

    // Each frame covers a different horizontal band, so a frame-indexing error is detectable.
    for (int i = 0; i < frames; ++i) {
        QImage mask(size, QImage::Format_Grayscale8);
        mask.fill(0);
        QPainter p(&mask);
        p.fillRect(QRect(0, i * 20, size.width(), 20), Qt::white);
        p.end();
        QVERIFY2(writer.writeFrame(mask, &error), qPrintable(error));
    }
    QVERIFY2(writer.finish(&error), qPrintable(error));

    QVERIFY(QFileInfo::exists(path));
    QVERIFY(!QFileInfo::exists(path + QStringLiteral(".part")));

    for (int i = 0; i < frames; ++i) {
        // Sample the middle of each frame's interval: the boundary time can land a hair below it
        // and resolve to the previous frame.
        const drift::TimeUs us = (2 * drift::TimeUs(i) + 1) * drift::kUsPerSecond / 60;
        const QImage frame = ClipReaderPool::instance().readVideoFrame(path, us, 0, 0);
        QVERIFY2(!frame.isNull(), qPrintable(QStringLiteral("frame %1 did not decode").arg(i)));
        QCOMPARE(frame.size(), size);

        int band = -1;
        for (int b = 0; b < frames + 2; ++b) {
            if (qRed(frame.pixel(size.width() / 2, b * 20 + 10)) > 200) {
                band = b;
                break;
            }
        }
        QCOMPARE(band, i);
    }
}

// The whole point of a proxy is that reading it forwards shows the source backwards. An off-by-one
// or a batch stitched together in the wrong order is invisible in a still and obvious in motion,
// so the ordering is pinned here rather than left to the eye.
void EngineTest::reverseRendererPlaysSourceBackwards()
{
    if (!Exporter::videoCodecById(QStringLiteral("h264")).value(QStringLiteral("available")).toBool())
        QSKIP("No H.264 encoder available in this FFmpeg build");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString sourcePath = dir.filePath(QStringLiteral("forward.mp4"));
    const QString proxyPath = dir.filePath(QStringLiteral("reversed.mp4"));
    const QSize size(320, 240);
    const int frames = 10;
    const int fps = 30;

    // Same band-per-frame trick as the matte round-trip: frame i is the only one with a white band
    // at row i * 20, so a frame can be identified from its pixels alone.
    drift::MatteWriter writer;
    QString error;
    QVERIFY2(writer.open(sourcePath, size, fps, 1, &error), qPrintable(error));
    for (int i = 0; i < frames; ++i) {
        QImage mask(size, QImage::Format_Grayscale8);
        mask.fill(0);
        QPainter p(&mask);
        p.fillRect(QRect(0, i * 20, size.width(), 20), Qt::white);
        p.end();
        QVERIFY2(writer.writeFrame(mask, &error), qPrintable(error));
    }
    QVERIFY2(writer.finish(&error), qPrintable(error));

    const drift::TimeUs coverOut = drift::TimeUs(frames) * drift::kUsPerSecond / fps;
    QVERIFY2(drift::renderReversed(sourcePath, 0, coverOut, proxyPath, &error, {}),
             qPrintable(error));
    QVERIFY(QFileInfo::exists(proxyPath));
    QVERIFY(!QFileInfo::exists(proxyPath + QStringLiteral(".part")));

    // Each source frame keeps the mirror of its own timestamp, so source frame i lands at
    // coverOut - i frames into the proxy. Walking the proxy forwards must walk the source back.
    for (int j = 1; j <= frames; ++j) {
        const drift::TimeUs us = drift::TimeUs(j) * drift::kUsPerSecond / fps;
        const QImage frame = ClipReaderPool::instance().readVideoFrame(proxyPath, us, 0, 0);
        QVERIFY2(!frame.isNull(), qPrintable(QStringLiteral("proxy frame %1 did not decode").arg(j)));

        int band = -1;
        for (int b = 0; b < frames + 2; ++b) {
            if (qRed(frame.pixel(size.width() / 2, b * 20 + 10)) > 128) {
                band = b;
                break;
            }
        }
        QCOMPARE(band, frames - j);
    }
}

// A proxy stays usable while the clip it was rendered for is trimmed inward, split or copied, and
// stops being usable the moment the source underneath it changes. Both halves matter: the first is
// what keeps ordinary editing smooth, the second is what stops a stale render being served as if
// it were the current source.
void EngineTest::reverseProxyLookupIsByContainmentAndSourceIdentity()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString sourcePath = dir.filePath(QStringLiteral("source.mp4"));
    const QString proxyPath = dir.filePath(QStringLiteral("proxy.mp4"));

    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    source.write(QByteArray(1024, 'a'));
    source.close();
    QFile proxy(proxyPath);
    QVERIFY(proxy.open(QIODevice::WriteOnly));
    proxy.write(QByteArray(16, 'b'));
    proxy.close();

    const drift::TimeUs coverIn = 0;
    const drift::TimeUs coverOut = 10 * drift::kUsPerSecond;
    drift::ReverseProxyCache::instance().insert(sourcePath, coverIn, coverOut, proxyPath);

    drift::TimeUs coverEnd = 0;
    QCOMPARE(drift::ReverseProxyCache::instance().lookup(sourcePath, 2 * drift::kUsPerSecond,
                                                         8 * drift::kUsPerSecond, &coverEnd),
             proxyPath);
    QCOMPARE(coverEnd, coverOut);

    // Exactly the rendered range still counts as covered.
    QCOMPARE(drift::ReverseProxyCache::instance().lookup(sourcePath, coverIn, coverOut, &coverEnd),
             proxyPath);

    // Extending past what was rendered drops back to the live path rather than showing the wrong
    // frames at the ends.
    QVERIFY(drift::ReverseProxyCache::instance()
                .lookup(sourcePath, coverIn, 12 * drift::kUsPerSecond, &coverEnd)
                .isEmpty());

    // A source replaced in place keeps its path, so identity has to come from the file itself.
    QVERIFY(source.open(QIODevice::WriteOnly | QIODevice::Truncate));
    source.write(QByteArray(2048, 'c'));
    source.close();
    QVERIFY(drift::ReverseProxyCache::instance()
                .lookup(sourcePath, 2 * drift::kUsPerSecond, 8 * drift::kUsPerSecond, &coverEnd)
                .isEmpty());
}

void EngineTest::resolveVideoReadMirrorsTheClipOntoTheProxy()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString sourcePath = dir.filePath(QStringLiteral("clip.mp4"));
    const QString proxyPath = dir.filePath(QStringLiteral("clip-reversed.mp4"));

    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    source.write(QByteArray(512, 'a'));
    source.close();
    QFile proxy(proxyPath);
    QVERIFY(proxy.open(QIODevice::WriteOnly));
    proxy.write(QByteArray(16, 'b'));
    proxy.close();

    drift::Clip clip;
    clip.type = drift::ClipType::Video;
    clip.path = sourcePath;
    clip.timelineStart = 5 * drift::kUsPerSecond;
    clip.timelineDuration = 4 * drift::kUsPerSecond;
    clip.srcIn = 3 * drift::kUsPerSecond;
    clip.srcOut = 7 * drift::kUsPerSecond;

    // Without the reverse flag nothing is redirected, even with a proxy sitting in the cache.
    const drift::TimeUs coverOut = 9 * drift::kUsPerSecond;
    drift::ReverseProxyCache::instance().insert(sourcePath, drift::kUsPerSecond, coverOut, proxyPath);
    drift::VideoRead read = drift::resolveVideoRead(clip, clip.timelineStart);
    QCOMPARE(read.path, sourcePath);
    QCOMPARE(read.sourceUs, clip.srcIn);

    // Reversed, the clip's first timeline frame is the source's last, and that is the proxy frame
    // furthest from its start. Getting this backwards shows up as a clip that plays the right way
    // round but from the wrong end.
    clip.reverse = true;
    read = drift::resolveVideoRead(clip, clip.timelineStart);
    QCOMPARE(read.path, proxyPath);
    QCOMPARE(read.sourceUs, coverOut - clip.srcOut);

    read = drift::resolveVideoRead(clip, clip.timelineStart + clip.timelineDuration);
    QCOMPARE(read.path, proxyPath);
    QCOMPARE(read.sourceUs, coverOut - clip.srcIn);

    QCOMPARE(drift::videoReadPath(clip), proxyPath);
}

void EngineTest::effectProcessorPassthroughWithoutEffects()
{
    QImage image(64, 64, QImage::Format_RGBA8888);
    image.fill(Qt::red);
    const QImage out = EffectProcessor::applyEffects(image, {});
    QCOMPARE(out.size(), image.size());
}

void EngineTest::effectProcessorBrightness()
{
    QImage image(64, 64, QImage::Format_RGBA8888);
    image.fill(QColor(100, 100, 100));

    if (!GpuEffectExecutor::instance().isAvailable())
        QSKIP("OpenGL offscreen context unavailable");

    drift::Effect effect;
    effect.catalogId = QStringLiteral("adjust.brightness");
    effect.parameters.insert(QStringLiteral("brightness"), 0.2);

    const QImage out = EffectProcessor::applyEffects(image, {effect});
    QVERIFY(!out.isNull());
    const QRgb pixel = out.pixel(32, 32);
    QVERIFY(qRed(pixel) > 100 || qGreen(pixel) > 100 || qBlue(pixel) > 100);
}

// Builds a 3-second, 64x64 clip: red [0,1), green [1,2), blue [2,3), sparse
// keyframes so the sequential path differs meaningfully from a per-frame seek.
QString EngineTest::makeColorSegmentsVideo(QTemporaryDir &dir)
{
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty())
        return {};

    const QString out = dir.filePath(QStringLiteral("colors.mp4"));
    QStringList args{
        QStringLiteral("-y"),
        QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
        QStringLiteral("color=c=red:s=64x64:r=25:d=1"),
        QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
        QStringLiteral("color=c=lime:s=64x64:r=25:d=1"),
        QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
        QStringLiteral("color=c=blue:s=64x64:r=25:d=1"),
        QStringLiteral("-filter_complex"), QStringLiteral("[0][1][2]concat=n=3:v=1:a=0[v]"),
        QStringLiteral("-map"), QStringLiteral("[v]"),
        QStringLiteral("-c:v"), QStringLiteral("libx264"),
        QStringLiteral("-g"), QStringLiteral("25"),
        QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
        out,
    };

    QProcess proc;
    proc.start(ffmpeg, args);
    if (!proc.waitForFinished(30000) || proc.exitCode() != 0)
        return {};
    return QFileInfo::exists(out) ? out : QString{};
}

void EngineTest::clipReaderSequentialAndSeek()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = makeColorSegmentsVideo(dir);
    if (path.isEmpty())
        QSKIP("ffmpeg not available to generate a test clip");

    ClipReader reader;
    QVERIFY(reader.open(path));
    QVERIFY(reader.hasVideo());

    auto dominant = [&](drift::TimeUs us) -> QChar {
        QImage frame;
        if (!reader.readVideoFrameAt(us, frame, 64, 64) || frame.isNull())
            return QChar('?');
        const QRgb p = frame.pixel(32, 32);
        if (qRed(p) >= qGreen(p) && qRed(p) >= qBlue(p))
            return QChar('R');
        if (qGreen(p) >= qRed(p) && qGreen(p) >= qBlue(p))
            return QChar('G');
        return QChar('B');
    };

    // Forward sequential requests exercise the no-seek fast path.
    QCOMPARE(dominant(500'000), QChar('R'));   // 0.5s
    QCOMPARE(dominant(700'000), QChar('R'));   // 0.7s, small forward step
    QCOMPARE(dominant(1'500'000), QChar('G')); // 1.5s
    QCOMPARE(dominant(2'500'000), QChar('B')); // 2.5s
    // Backward jump forces a keyframe reseek and must not return a stale frame.
    QCOMPARE(dominant(500'000), QChar('R'));
    QCOMPARE(dominant(1'500'000), QChar('G'));
}

// 64x32 landscape, red left half / blue right half, tagged with a display matrix.
// `displayDegrees` is the clockwise turn a player should apply, i.e. what
// displayRotationOf() reports; the matrix stores its negation.
QString EngineTest::makeRotatedHalvesVideo(QTemporaryDir &dir, int displayDegrees)
{
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty())
        return {};

    const QString flat = dir.filePath(QStringLiteral("halves-flat.mp4"));
    QStringList makeArgs{
        QStringLiteral("-y"),
        QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
        QStringLiteral("color=c=red:s=32x32:r=25:d=1"),
        QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
        QStringLiteral("color=c=blue:s=32x32:r=25:d=1"),
        QStringLiteral("-filter_complex"), QStringLiteral("[0][1]hstack=inputs=2[v]"),
        QStringLiteral("-map"), QStringLiteral("[v]"),
        QStringLiteral("-c:v"), QStringLiteral("libx264"),
        QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
        flat,
    };

    QProcess make;
    make.start(ffmpeg, makeArgs);
    if (!make.waitForFinished(30000) || make.exitCode() != 0)
        return {};

    // -display_rotation is an input option, so tagging needs a second stream-copy pass.
    const QString out = dir.filePath(QStringLiteral("halves-rotated.mp4"));
    QStringList tagArgs{
        QStringLiteral("-y"),
        QStringLiteral("-display_rotation:v:0"), QString::number(-displayDegrees),
        QStringLiteral("-i"), flat,
        QStringLiteral("-c"), QStringLiteral("copy"),
        out,
    };

    QProcess tag;
    tag.start(ffmpeg, tagArgs);
    if (!tag.waitForFinished(30000) || tag.exitCode() != 0)
        return {};
    return QFileInfo::exists(out) ? out : QString{};
}

void EngineTest::clipReaderAppliesDisplayRotation_data()
{
    QTest::addColumn<int>("displayDegrees");
    QTest::addColumn<QSize>("expectedSize");
    // Where the source's red left half ends up once the frame is upright.
    QTest::addColumn<QPoint>("redAt");
    QTest::addColumn<QPoint>("blueAt");

    QTest::newRow("90cw") << 90 << QSize(32, 64) << QPoint(16, 8) << QPoint(16, 56);
    QTest::newRow("180") << 180 << QSize(64, 32) << QPoint(48, 16) << QPoint(16, 16);
    QTest::newRow("270cw") << 270 << QSize(32, 64) << QPoint(16, 56) << QPoint(16, 8);
}

void EngineTest::clipReaderAppliesDisplayRotation()
{
    QFETCH(int, displayDegrees);
    QFETCH(QSize, expectedSize);
    QFETCH(QPoint, redAt);
    QFETCH(QPoint, blueAt);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = makeRotatedHalvesVideo(dir, displayDegrees);
    if (path.isEmpty())
        QSKIP("ffmpeg not available to generate a rotated test clip");

    ClipReader reader;
    QVERIFY(reader.open(path));
    QVERIFY(reader.hasVideo());

    // The box is in display orientation: without the swap in decodeSizeFor a portrait
    // box fitted against the landscape source decodes at half size.
    QImage frame;
    QVERIFY(reader.readVideoFrameAt(500'000, frame, expectedSize.width(), expectedSize.height()));
    QCOMPARE(frame.size(), expectedSize);

    const QRgb red = frame.pixel(redAt);
    const QRgb blue = frame.pixel(blueAt);
    QVERIFY(qRed(red) > qBlue(red));
    QVERIFY(qBlue(blue) > qRed(blue));

    // The preview path converts to NV12 separately and needs the same treatment.
    Nv12Frame nv12;
    QVERIFY(reader.readVideoFrameAtNv12(500'000, nv12, expectedSize.width(), expectedSize.height()));
    QCOMPARE(QSize(nv12.width, nv12.height), expectedSize);
    // Red is markedly brighter than blue, so luma alone shows the halves are upright.
    const auto lumaAt = [&](QPoint p) {
        return uchar(nv12.data.at(qsizetype(p.y()) * nv12.width + p.x()));
    };
    QVERIFY(lumaAt(redAt) > lumaAt(blueAt));
}

// The proxy re-encodes the source's pixels untouched, so it has to re-emit the source's
// display matrix — otherwise reversing a rotated clip would play it back sideways.
void EngineTest::reverseProxyKeepsDisplayRotation()
{
    if (!Exporter::videoCodecById(QStringLiteral("h264")).value(QStringLiteral("available")).toBool())
        QSKIP("No H.264 encoder available in this FFmpeg build");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString sourcePath = makeRotatedHalvesVideo(dir, 90);
    if (sourcePath.isEmpty())
        QSKIP("ffmpeg not available to generate a rotated test clip");

    const QString proxyPath = dir.filePath(QStringLiteral("reversed.mp4"));
    QString error;
    QVERIFY2(drift::renderReversed(sourcePath, 0, drift::kUsPerSecond, proxyPath, &error, {}),
             qPrintable(error));

    const MediaInfo info = MediaProbe::probe(proxyPath);
    QVERIFY(info.ok);
    bool sawVideo = false;
    for (const StreamInfo &stream : info.streams) {
        if (stream.type != StreamInfo::Type::Video)
            continue;
        sawVideo = true;
        QCOMPARE(stream.rotationDegrees, 90);
    }
    QVERIFY(sawVideo);

    // And the reader applies it, so the proxy decodes upright like the original does.
    ClipReader reader;
    QVERIFY(reader.open(proxyPath));
    QImage frame;
    QVERIFY(reader.readVideoFrameAt(500'000, frame, 32, 64));
    QCOMPARE(frame.size(), QSize(32, 64));
    const QRgb top = frame.pixel(16, 8);
    QVERIFY(qRed(top) > qBlue(top));
}

QString EngineTest::makeToneAudio(QTemporaryDir &dir)
{
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty())
        return {};

    const QString out = dir.filePath(QStringLiteral("tone.wav"));
    QStringList args{
        QStringLiteral("-y"),
        QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
        QStringLiteral("sine=frequency=440:sample_rate=48000:duration=2"),
        QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"),
        out,
    };

    QProcess proc;
    proc.start(ffmpeg, args);
    if (!proc.waitForFinished(30000) || proc.exitCode() != 0)
        return {};
    return QFileInfo::exists(out) ? out : QString{};
}

// Sequential small buffers must reconstruct the same signal as one contiguous
// read. The old path re-seeked on every buffer, repeating/overlapping audio.
void EngineTest::clipReaderAudioSequential()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = makeToneAudio(dir);
    if (path.isEmpty())
        QSKIP("ffmpeg not available to generate a test clip");

    constexpr int kRate = 48000;
    constexpr int kChunk = 1024;
    constexpr int kChunks = 20;
    constexpr int kTotal = kChunk * kChunks;
    constexpr drift::TimeUs kStartUs = 200'000;

    ClipReader ref;
    QVERIFY(ref.open(path));
    QVERIFY(ref.hasAudio());
    QVector<float> refBuf(kTotal * 2, 0.0f);
    QCOMPARE(ref.readAudioInterleaved(kStartUs, kTotal, kRate, refBuf.data()), kTotal);

    double sumSq = 0.0;
    for (float s : refBuf)
        sumSq += static_cast<double>(s) * s;
    QVERIFY(std::sqrt(sumSq / refBuf.size()) > 0.05); // audibly non-silent

    ClipReader seq;
    QVERIFY(seq.open(path));
    QVector<float> seqBuf;
    seqBuf.reserve(kTotal * 2);
    QVector<float> chunkBuf(kChunk * 2);
    drift::TimeUs t = kStartUs;
    for (int c = 0; c < kChunks; ++c) {
        const int n = seq.readAudioInterleaved(t, kChunk, kRate, chunkBuf.data());
        QVERIFY(n > 0);
        for (int i = 0; i < n * 2; ++i)
            seqBuf.append(chunkBuf[i]);
        t += static_cast<drift::TimeUs>(n) * drift::kUsPerSecond / kRate;
    }

    const int cmp = qMin(refBuf.size(), seqBuf.size());
    QVERIFY(cmp >= kTotal * 2 - kChunk * 2);
    double err = 0.0;
    for (int i = 0; i < cmp; ++i) {
        const double d = static_cast<double>(refBuf[i]) - seqBuf[i];
        err += d * d;
    }
    QVERIFY(std::sqrt(err / cmp) < 0.02);
}

void EngineTest::compositorDefaultRenderStaysFullResolution()
{
    drift::Project project;
    project.setResolution(192, 108);
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Shape});

    drift::Clip clip;
    clip.id = QStringLiteral("shape");
    clip.type = drift::ClipType::Shape;
    clip.timelineStart = 0;
    clip.timelineDuration = drift::secondsToUs(1.0);
    clip.shapeStyle.kind = drift::ShapeKind::Rectangle;
    clip.shapeStyle.fill = Qt::red;
    project.tracks()[0].clips.append(clip);

    FrameCompositor compositor;
    compositor.setProject(&project);

    const QImage frame = compositor.compositeAt(0);
    QCOMPARE(frame.size(), QSize(192, 108));
}

void EngineTest::compositorPreviewScaleRendersLowerResolution()
{
    drift::Project project;
    project.setResolution(192, 108);
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Shape});

    drift::Clip clip;
    clip.id = QStringLiteral("shape");
    clip.type = drift::ClipType::Shape;
    clip.timelineStart = 0;
    clip.timelineDuration = drift::secondsToUs(1.0);
    clip.shapeStyle.kind = drift::ShapeKind::Rectangle;
    clip.shapeStyle.fill = Qt::red;
    project.tracks()[0].clips.append(clip);

    FrameCompositor compositor;
    compositor.setProject(&project);

    FrameCompositor::RenderOptions options;
    options.previewScale = 0.5;
    options.maxTimeEchoHistoryFrames = 1;
    const QImage frame = compositor.compositeAt(0, options);
    QCOMPARE(frame.size(), QSize(96, 54));

    const QImage fullFrame = compositor.compositeAt(0);
    QCOMPARE(fullFrame.size(), QSize(192, 108));
}

void EngineTest::compositorPreviewScaleMapsProjectPixelLayout()
{
    // Project-pixel layout must be scaled onto the preview canvas so WYSIWYG
    // handles (which map project px → widget) stay aligned with the frame.
    drift::Project project;
    project.setResolution(200, 100);
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Shape});

    drift::Clip clip;
    clip.id = QStringLiteral("shape");
    clip.type = drift::ClipType::Shape;
    clip.timelineStart = 0;
    clip.timelineDuration = drift::secondsToUs(1.0);
    clip.shapeStyle.kind = drift::ShapeKind::Rectangle;
    clip.shapeStyle.fill = Qt::red;
    clip.shapeStyle.strokeWidth = 0.0;
    clip.transformX.setKeyframe(0, 40.0);
    clip.transformY.setKeyframe(0, 20.0);
    clip.transformW.setKeyframe(0, 80.0);
    clip.transformH.setKeyframe(0, 40.0);
    project.tracks()[0].clips.append(clip);

    FrameCompositor compositor;
    compositor.setProject(&project);

    FrameCompositor::RenderOptions options;
    options.previewScale = 0.5;
    const QImage frame = compositor.compositeAt(0, options);
    QCOMPARE(frame.size(), QSize(100, 50));
    // Scaled layout: (20,10)-(60,30) on the half-res canvas.
    QVERIFY(frame.pixelColor(40, 20).red() > 200);
    QCOMPARE(frame.pixelColor(0, 0), QColor(0, 0, 0));
    QCOMPARE(frame.pixelColor(90, 40), QColor(0, 0, 0));
}

// Effects reach the screen through two different code paths — the CPU chain in EffectProcessor and
// the GPU chain in GpuCompositor — and only the CPU one was wired up at first, so face warps
// rendered in tools and exports while the preview showed nothing at all. This drives the whole
// compositor, which is what the preview uses.
void EngineTest::compositorAppliesFaceWarpFromBakedTrack()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // Diagonal wedges: a warp has to move visibly different pixels around, which a flat or
    // radially symmetric image would hide.
    QImage source(64, 64, QImage::Format_RGBA8888);
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x)
            source.setPixelColor(x, y, ((x / 8) + (y / 8)) % 2 ? Qt::white : QColor(20, 40, 200));
    }
    const QString imagePath = dir.filePath(QStringLiteral("src.png"));
    QVERIFY(source.save(imagePath, "PNG"));

    // A face filling most of the frame, so the warp covers a large share of the pixels.
    drift::FaceAnchors a;
    a.valid = true;
    a.faceCenter = QPointF(0.5, 0.5);
    a.leftEye = QPointF(0.38, 0.42);
    a.rightEye = QPointF(0.62, 0.42);
    a.noseTip = QPointF(0.5, 0.52);
    a.mouthCenter = QPointF(0.5, 0.66);
    a.mouthLeft = QPointF(0.42, 0.66);
    a.mouthRight = QPointF(0.58, 0.66);
    a.chin = QPointF(0.5, 0.8);
    a.forehead = QPointF(0.5, 0.2);
    a.faceRx = 0.3;
    a.faceRy = 0.35;
    a.angle = 0.0;
    a.eyeRadius = 0.05;
    a.score = 1.0;

    drift::FaceTrack track;
    track.fps = 30;
    drift::FaceTrackFrame frame;
    frame.faces.append(a);
    for (int i = 0; i < 4; ++i)
        track.frames.append(frame);

    const QString trackPath = dir.filePath(QStringLiteral("track.json"));
    QString error;
    QVERIFY2(drift::writeFaceTrack(trackPath, track, &error), qPrintable(error));

    auto composite = [&](bool attachTrack) {
        drift::Project project;
        project.setResolution(64, 64);
        project.tracks().clear();
        project.tracks().append(drift::Track{.type = drift::TrackType::Video});

        drift::Clip clip;
        clip.id = QStringLiteral("c");
        clip.type = drift::ClipType::Image;
        clip.path = imagePath;
        clip.timelineStart = 0;
        clip.timelineDuration = drift::secondsToUs(1.0);
        if (attachTrack)
            clip.faceTrackPath = trackPath;

        drift::Effect warp;
        warp.catalogId = QStringLiteral("face_swirl");
        warp.parameters.insert(QStringLiteral("twist"), 2.5);
        warp.parameters.insert(QStringLiteral("coverage"), 1.8);
        warp.parameters.insert(QStringLiteral("faceIndex"), 0);
        clip.effects.append(warp);

        project.tracks()[0].clips.append(clip);

        FrameCompositor compositor;
        compositor.setProject(&project);
        return compositor.compositeAt(0);
    };

    const QImage warped = composite(true);
    const QImage untracked = composite(false);
    QVERIFY(!warped.isNull());
    QVERIFY(!untracked.isNull());

    // Without a track the effect must be a clean pass-through, and with one it must actually bend
    // the picture. Comparing the two pins both directions at once.
    int differing = 0;
    for (int y = 0; y < warped.height(); ++y) {
        for (int x = 0; x < warped.width(); ++x)
            differing += warped.pixel(x, y) != untracked.pixel(x, y) ? 1 : 0;
    }
    QVERIFY2(differing > 200,
             qPrintable(QStringLiteral("face warp changed only %1 pixels — the compositor is not "
                                       "feeding anchors to the effect")
                            .arg(differing)));
}

void EngineTest::compositorAppliesMultiplyBlendMode()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    auto writeSolidImage = [&](const QString &name, QColor color) {
        QImage image(64, 64, QImage::Format_RGBA8888);
        image.fill(color);
        const QString path = dir.filePath(name);
        image.save(path, "PNG");
        return path;
    };

    auto compositeOverBackground = [&](QColor background) {
        drift::Project project;
        project.setResolution(64, 64);
        project.tracks().clear();
        project.tracks().append(drift::Track{.type = drift::TrackType::Shape});
        project.tracks().append(drift::Track{.type = drift::TrackType::Shape});

        // Index 0 is the topmost track and composites in front, so the
        // multiplied foreground goes on track 0 and the background on track 1.
        drift::Clip top;
        top.id = QStringLiteral("top");
        top.type = drift::ClipType::Image;
        top.path = writeSolidImage(QStringLiteral("top.png"), Qt::red);
        top.blendMode = drift::BlendMode::Multiply;
        top.timelineStart = 0;
        top.timelineDuration = drift::secondsToUs(1.0);
        project.tracks()[0].clips.append(top);

        drift::Clip bottom;
        bottom.id = QStringLiteral("bottom");
        bottom.type = drift::ClipType::Image;
        bottom.path = writeSolidImage(QStringLiteral("bottom.png"), background);
        bottom.timelineStart = 0;
        bottom.timelineDuration = drift::secondsToUs(1.0);
        project.tracks()[1].clips.append(bottom);

        FrameCompositor compositor;
        compositor.setProject(&project);
        return compositor.compositeAt(0);
    };

    const QImage overGreen = compositeOverBackground(Qt::green);
    QCOMPARE(overGreen.pixelColor(32, 32), QColor(0, 0, 0));

    const QImage overWhite = compositeOverBackground(Qt::white);
    QCOMPARE(overWhite.pixelColor(32, 32), QColor(255, 0, 0));
}

// A keyed effect parameter is resolved inside the compositor, which is the only place preview and
// export share. If the bake were done anywhere else the two could drift apart, so assert on the
// composited pixels rather than on the resolved parameter map.
void EngineTest::compositorAnimatesKeyedEffectParam()
{
    if (!GpuEffectExecutor::instance().isAvailable())
        QSKIP("OpenGL offscreen context unavailable");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QImage source(64, 64, QImage::Format_RGBA8888);
    source.fill(QColor(100, 100, 100));
    const QString path = dir.filePath(QStringLiteral("grey.png"));
    QVERIFY(source.save(path, "PNG"));

    drift::Project project;
    project.setResolution(64, 64);

    drift::Clip clip;
    clip.id = QStringLiteral("animated");
    clip.type = drift::ClipType::Image;
    clip.path = path;
    clip.timelineStart = 0;
    clip.timelineDuration = drift::secondsToUs(2.0);

    drift::Effect effect;
    effect.catalogId = QStringLiteral("adjust.brightness");
    // The static value is deliberately the *opposite* of the ramp, so a composite that ignored the
    // track would darken instead of brighten and the assertions below would fail.
    effect.parameters.insert(QStringLiteral("brightness"), -0.5);
    drift::KeyframeTrack<double> ramp;
    ramp.setKeyframe(0, 0.0);
    ramp.setKeyframe(drift::secondsToUs(2.0), 0.5);
    effect.paramKeyframes.insert(QStringLiteral("brightness"), ramp);
    clip.effects.append(effect);
    project.tracks()[0].clips.append(clip);

    FrameCompositor compositor;
    compositor.setProject(&project);

    const int atStart = qRed(compositor.compositeAt(0).pixel(32, 32));
    const int atMid = qRed(compositor.compositeAt(drift::secondsToUs(1.0)).pixel(32, 32));
    const int atEnd = qRed(compositor.compositeAt(drift::secondsToUs(1.999)).pixel(32, 32));

    QVERIFY(atStart < atMid);
    QVERIFY(atMid < atEnd);
    // brightness 0 at t=0 leaves the source untouched.
    QVERIFY(qAbs(atStart - 100) <= 2);
}

void EngineTest::compositorRendersShapeClip()
{
    drift::Project project;
    project.setResolution(128, 128);
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Shape});

    drift::Clip clip;
    clip.id = QStringLiteral("shape");
    clip.type = drift::ClipType::Shape;
    clip.name = QStringLiteral("triangle");
    clip.timelineStart = 0;
    clip.timelineDuration = drift::secondsToUs(1.0);
    clip.shapeStyle.kind = drift::ShapeKind::Triangle;
    clip.shapeStyle.fill = QColor(255, 0, 0);
    clip.shapeStyle.stroke = Qt::white;
    clip.shapeStyle.strokeWidth = 2.0;
    clip.transformX.setKeyframe(0, 32.0);
    clip.transformY.setKeyframe(0, 32.0);
    clip.transformW.setKeyframe(0, 64.0);
    clip.transformH.setKeyframe(0, 64.0);
    project.tracks()[0].clips.append(clip);

    FrameCompositor compositor;
    compositor.setProject(&project);
    const QImage frame = compositor.compositeAt(0);
    QVERIFY(!frame.isNull());
    QVERIFY(frame.pixelColor(64, 64).red() > 200);
    QVERIFY(frame.pixelColor(0, 0) == QColor(0, 0, 0));
}

// RenderOptions::skipClipId omits one clip from the frame. Used by in-place text
// editing on the preview, where the QML editor stands in for the baked raster.
void EngineTest::compositorSkipsClipBeingEdited()
{
    drift::Project project;
    project.setResolution(128, 128);
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Shape});

    drift::Clip clip;
    clip.id = QStringLiteral("edited");
    clip.type = drift::ClipType::Shape;
    clip.timelineStart = 0;
    clip.timelineDuration = drift::secondsToUs(1.0);
    clip.shapeStyle.kind = drift::ShapeKind::Rectangle;
    clip.shapeStyle.fill = QColor(255, 0, 0);
    clip.transformX.setKeyframe(0, 32.0);
    clip.transformY.setKeyframe(0, 32.0);
    clip.transformW.setKeyframe(0, 64.0);
    clip.transformH.setKeyframe(0, 64.0);
    project.tracks()[0].clips.append(clip);

    FrameCompositor compositor;
    compositor.setProject(&project);

    // Rendered normally the clip covers the centre.
    const QImage shown = compositor.compositeAt(0);
    QVERIFY(!shown.isNull());
    QVERIFY(shown.pixelColor(64, 64).red() > 200);

    // Skipping it leaves the background showing through.
    FrameCompositor::RenderOptions options;
    options.skipClipId = QStringLiteral("edited");
    const QImage hidden = compositor.compositeAt(0, options);
    QVERIFY(!hidden.isNull());
    QCOMPARE(hidden.pixelColor(64, 64), QColor(0, 0, 0));

    // An unrelated id must not hide anything.
    options.skipClipId = QStringLiteral("someone-else");
    const QImage untouched = compositor.compositeAt(0, options);
    QVERIFY(untouched.pixelColor(64, 64).red() > 200);
}

void EngineTest::adjustmentEffectContrastCatalogEntry()
{
    const EffectPresetEntry *def = effectDefForId(QStringLiteral("adjust.contrast"));
    QVERIFY(def);
    QVERIFY(def->isGpu);
    QCOMPARE(def->meta.parameters.size(), 1);
    QCOMPARE(def->meta.parameters[0].key, QStringLiteral("contrast"));

    if (!GpuEffectExecutor::instance().isAvailable())
        QSKIP("OpenGL offscreen context unavailable");

    QImage image(64, 64, QImage::Format_RGBA8888);
    image.fill(QColor(180, 180, 180));

    drift::Effect effect;
    effect.catalogId = def->meta.id;
    effect.parameters.insert(def->meta.parameters[0].key, 2.0);

    const QImage out = EffectProcessor::applyEffects(image, {effect});
    QVERIFY(!out.isNull());
    const QRgb pixel = out.pixel(32, 32);
    QVERIFY(qRed(pixel) > 180 || qGreen(pixel) > 180 || qBlue(pixel) > 180);
}

void EngineTest::effectPresetStableIds()
{
    const QStringList ids = effectPresetIds();
    QVERIFY(ids.size() >= 16);

    // Ids are persisted in project files, so they are API. New effects are namespaced
    // ("category.name"); the bare ids below predate that and can never be renamed. Adding to this
    // list has to be a deliberate act — that is the point of the test.
    static const QSet<QString> legacyBareIds = {
        QStringLiteral("beat_shake"),      QStringLiteral("bling_sparkle"),
        QStringLiteral("block_glitch"),    QStringLiteral("bloom_glow"),
        QStringLiteral("bokeh_dream"),     QStringLiteral("cinematic_grade"),
        QStringLiteral("digital_glitch"),  QStringLiteral("droste_zoom"),
        QStringLiteral("duotone"),         QStringLiteral("edge_neon"),
        QStringLiteral("film_burn"),       QStringLiteral("halation"),
        QStringLiteral("halftone_comic"),  QStringLiteral("kaleidoscope"),
        QStringLiteral("lens_flare"),      QStringLiteral("light_leak"),
        QStringLiteral("lightning_sky"),   QStringLiteral("motion_trail"),
        QStringLiteral("oil_paint"),       QStringLiteral("rgb_split"),
        QStringLiteral("ripple_water"),    QStringLiteral("scanline_glitch"),
        QStringLiteral("shockwave_pulse"), QStringLiteral("sketch_pencil"),
        QStringLiteral("spin_blur"),       QStringLiteral("star_filter"),
        QStringLiteral("strobe_flash"),    QStringLiteral("super8_film"),
        QStringLiteral("time_echo"),       QStringLiteral("vhs_crt"),
        QStringLiteral("wave_warp"),       QStringLiteral("zoom_pulse"),
    };

    // absolutePath() because the macro points out of the source tree with a ".." segment, while
    // the catalog stores what QFileInfo::absoluteFilePath() produced — already cleaned.
    const QString addonEffectsDir =
        QDir(QString::fromUtf8(DRIFT_TEST_ADDON_EFFECTS_DIR)).absolutePath() + QLatin1Char('/');

    QSet<QString> seen;
    for (const QString &id : ids) {
        QVERIFY2(!id.isEmpty(), "preset id must not be empty");
        QVERIFY2(!seen.contains(id), qPrintable(QStringLiteral("duplicate id: %1").arg(id)));
        seen.insert(id);

        const EffectPresetEntry *def = effectDefForId(id);
        QVERIFY2(def, qPrintable(QStringLiteral("missing catalog entry for %1").arg(id)));
        QCOMPARE(def->meta.id, id);
        QVERIFY2(!def->meta.displayName.isEmpty(),
                 qPrintable(QStringLiteral("display name missing for %1").arg(id)));
        QVERIFY2(!def->meta.category.isEmpty(),
                 qPrintable(QStringLiteral("category missing for %1").arg(id)));

        // effects.core shipped its catalog with bare ids from 1.0.0 on, so this rule cannot
        // retroactively rename them without breaking every project saved against it. The
        // namespacing requirement applies to what this repo bundles; addon roots are only
        // present when a sibling drift-addons checkout exists, and never on CI.
        if (def->isGpu && def->gpu.packageDir.startsWith(addonEffectsDir))
            continue;
        QVERIFY2(id.contains(QLatin1Char('.')) || legacyBareIds.contains(id)
                     || id.startsWith(QStringLiteral("face_")),
                 qPrintable(QStringLiteral("stable id: %1").arg(id)));
    }
}

void EngineTest::effectPresetCatalogIncludesStylizePresets()
{
    const auto requirePreset = [&](const char *id, const char *displayName, const char *category,
                                   bool isGpu) {
        const EffectPresetEntry *def = effectDefForId(QString::fromLatin1(id));
        QVERIFY2(def, id);
        QCOMPARE(def->meta.displayName, QString::fromLatin1(displayName));
        QCOMPARE(def->meta.category, QString::fromLatin1(category));
        QCOMPARE(def->isGpu, isGpu);
    };

    requirePreset("rgb_split", "RGB Split", "glitch", true);
    requirePreset("block_glitch", "Block Glitch", "glitch", true);
    requirePreset("scanline_glitch", "Scanline Glitch", "glitch", true);
    requirePreset("vhs_crt", "VHS / CRT", "retro", true);
    requirePreset("film_burn", "Film Burn / Light Leak", "retro", true);
    requirePreset("stylize.vhs", "VHS", "retro", true);
    requirePreset("stylize.bloom", "Bloom", "dreamy", true);
    requirePreset("bloom_glow", "Bloom / Glow", "dreamy", true);
    requirePreset("edge_neon", "Edge Glow / Neon", "dreamy", true);
    requirePreset("time_echo", "Time Echo / Trail", "dreamy", true);
    requirePreset("stylize.ripple", "Ripple", "glitch", true);
    requirePreset("ripple_water", "Ripple / Water", "glitch", true);
    requirePreset("shockwave_pulse", "Shockwave / Pulse", "glitch", true);
    requirePreset("digital_glitch", "Digital Glitch", "glitch", true);
    requirePreset("adjust.contrast", "Contrast", "color", true);
}

void EngineTest::effectBrowserCategories()
{
    const QList<QPair<QString, QString>> categories = effectCategories();
    QVERIFY(categories.size() >= 5);
    QCOMPARE(categories[0].first, QStringLiteral("color"));
    QCOMPARE(categories[0].second, QStringLiteral("Color"));
    QCOMPARE(categories[1].first, QStringLiteral("glitch"));
    QCOMPARE(categories[1].second, QStringLiteral("Glitch & Distortion"));
    QCOMPARE(categories[2].first, QStringLiteral("retro"));
    QCOMPARE(categories[3].first, QStringLiteral("dreamy"));
    QCOMPARE(categories[4].first, QStringLiteral("impact"));

    QSet<QString> knownCategories;
    for (const auto &category : categories)
        knownCategories.insert(category.first);

    for (const QString &id : effectPresetIds()) {
        const EffectPresetEntry *def = effectDefForId(id);
        QVERIFY(def);
        QVERIFY2(knownCategories.contains(def->meta.category),
                 qPrintable(QStringLiteral("unknown category for %1: %2").arg(id, def->meta.category)));
        QVERIFY(!effectCategoryLabel(def->meta.category).isEmpty());
    }
}

void EngineTest::effectGraphTemplateSubstitution()
{
    // VHS is a GPU package now — no libavfilter graph template.
    const EffectPresetEntry *def = effectDefForId(QStringLiteral("stylize.vhs"));
    QVERIFY(def);
    QVERIFY(def->isGpu);
    QVERIFY(def->gpu.valid);
    QCOMPARE(def->graphTemplate, QString());

    drift::Effect vhs;
    vhs.catalogId = QStringLiteral("stylize.vhs");
    vhs.parameters.insert(QStringLiteral("noise"), 30.0);
    QCOMPARE(buildFilterGraphForEffect(vhs), QString());
}

void EngineTest::compositorOnlyPresetsUseCompositorPath()
{
    const EffectPresetEntry *bloom = effectDefForId(QStringLiteral("stylize.bloom"));
    QVERIFY(bloom);
    QVERIFY(bloom->isGpu);
    QVERIFY(bloom->gpu.valid);
    QCOMPARE(buildFilterGraphForEffect({.catalogId = bloom->meta.id}), QString());

    if (!GpuEffectExecutor::instance().isAvailable())
        QSKIP("OpenGL offscreen context unavailable");

    QImage image(32, 32, QImage::Format_RGBA8888);
    image.fill(QColor(200, 120, 80));

    drift::Effect effect;
    effect.catalogId = bloom->meta.id;
    effect.parameters.insert(QStringLiteral("intensity"), 1.0);
    effect.parameters.insert(QStringLiteral("radius"), 4.0);

    const QImage out = EffectProcessor::applyEffects(image, {effect});
    QVERIFY(!out.isNull());
    QVERIFY(out.pixel(16, 16) != image.pixel(16, 16));
}

void EngineTest::effectPackageLoaderParsesGaussianBlur()
{
    const QString pkg =
        QDir(QString::fromUtf8(DRIFT_TEST_EFFECTS_DIR)).filePath(QStringLiteral("gaussian_blur"));
    QString error;
    const EffectPresetEntry entry = EffectPackageLoader::loadPackage(pkg, &error);
    QVERIFY2(entry.gpu.valid, qPrintable(error));
    QVERIFY(entry.isGpu);
    QCOMPARE(entry.meta.id, QStringLiteral("builtin.effects.gaussian_blur"));
    QCOMPARE(entry.meta.displayName, QStringLiteral("Gaussian Blur (GPU)"));
    QCOMPARE(entry.meta.category, QStringLiteral("dreamy"));
    QCOMPARE(entry.meta.parameters.size(), 1);
    QCOMPARE(entry.meta.parameters[0].key, QStringLiteral("u_blurRadius"));
    QCOMPARE(entry.gpu.passes.size(), 2);
    QCOMPARE(entry.gpu.intermediateBuffers.size(), 1);

    const EffectPresetEntry *cataloged = effectDefForId(QStringLiteral("builtin.effects.gaussian_blur"));
    QVERIFY(cataloged);
    QVERIFY(cataloged->isGpu);
    QVERIFY(cataloged->gpu.valid);
    QCOMPARE(buildFilterGraphForEffect({.catalogId = cataloged->meta.id}), QString());
}

void EngineTest::effectPackageLoaderRejectsReservedUniform()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString pkg = dir.filePath(QStringLiteral("bad_reserved"));
    QVERIFY(QDir().mkpath(pkg));
    QFile json(QDir(pkg).filePath(QStringLiteral("effect.json")));
    QVERIFY(json.open(QIODevice::WriteOnly | QIODevice::Text));
    json.write(R"({
      "id": "test.reserved",
      "displayName": "Bad",
      "category": "dreamy",
      "backend": "gpu",
      "parameters": [{"identifier": "u_resolution", "displayName": "Res", "type": "float",
                     "defaultValue": 1, "minValue": 0, "maxValue": 2}],
      "pipeline": {"intermediateBuffers": [], "passes": [
        {"passIndex": 0, "fragmentShader": "x.frag",
         "inputs": [{"type": "source_texture"}], "output": {"type": "canvas"}}
      ]}
    })");
    json.close();
    QFile frag(QDir(pkg).filePath(QStringLiteral("x.frag")));
    QVERIFY(frag.open(QIODevice::WriteOnly | QIODevice::Text));
    frag.write("#version 330 core\nin vec2 v_texCoord; out vec4 fragColor;\n"
               "uniform sampler2D u_currentTexture;\nvoid main(){ fragColor = texture(u_currentTexture, v_texCoord); }\n");
    frag.close();

    QString error;
    const EffectPresetEntry entry = EffectPackageLoader::loadPackage(pkg, &error);
    QVERIFY(!entry.gpu.valid);
    QVERIFY(error.contains(QStringLiteral("reserved")));
}

void EngineTest::effectPackageLoaderRejectsMissingShader()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString pkg = dir.filePath(QStringLiteral("missing_shader"));
    QVERIFY(QDir().mkpath(pkg));
    QFile json(QDir(pkg).filePath(QStringLiteral("effect.json")));
    QVERIFY(json.open(QIODevice::WriteOnly | QIODevice::Text));
    json.write(R"({
      "id": "test.missing",
      "displayName": "Missing",
      "category": "dreamy",
      "backend": "gpu",
      "parameters": [],
      "pipeline": {"intermediateBuffers": [], "passes": [
        {"passIndex": 0, "fragmentShader": "nope.frag",
         "inputs": [{"type": "source_texture"}], "output": {"type": "canvas"}}
      ]}
    })");
    json.close();

    QString error;
    const EffectPresetEntry entry = EffectPackageLoader::loadPackage(pkg, &error);
    QVERIFY(!entry.gpu.valid);
    QVERIFY(error.contains(QStringLiteral("missing shader")));
}

void EngineTest::gpuGaussianBlurChangesImage()
{
    if (!GpuEffectExecutor::instance().isAvailable())
        QSKIP("OpenGL offscreen context unavailable");

    QImage image(64, 64, QImage::Format_RGBA8888);
    image.fill(Qt::black);
    for (int y = 20; y < 44; ++y) {
        for (int x = 20; x < 44; ++x)
            image.setPixel(x, y, qRgba(255, 255, 255, 255));
    }

    drift::Effect effect;
    effect.catalogId = QStringLiteral("builtin.effects.gaussian_blur");
    effect.parameters.insert(QStringLiteral("u_blurRadius"), 8.0);

    const QImage out = EffectProcessor::applyEffects(image, {effect});
    QCOMPARE(out.size(), image.size());
    // Edge of the white square should pick up blur (not pure black outside).
    const QRgb outside = out.pixel(10, 32);
    QVERIFY2(qRed(outside) > 0 || qGreen(outside) > 0 || qBlue(outside) > 0,
             "expected blur bleed outside the white square");
}

void EngineTest::gpuMultiPassPreservesVerticalOrientation()
{
    if (!GpuEffectExecutor::instance().isAvailable())
        QSKIP("OpenGL offscreen context unavailable");

    // Red band at top, blue at bottom — multi-pass blur must not swap them.
    QImage image(32, 32, QImage::Format_RGBA8888);
    for (int y = 0; y < 32; ++y) {
        const QRgb color = (y < 16) ? qRgba(255, 0, 0, 255) : qRgba(0, 0, 255, 255);
        for (int x = 0; x < 32; ++x)
            image.setPixel(x, y, color);
    }

    drift::Effect blur;
    blur.catalogId = QStringLiteral("builtin.effects.gaussian_blur");
    blur.parameters.insert(QStringLiteral("u_blurRadius"), 2.0);

    const QImage blurred = EffectProcessor::applyEffects(image, {blur});
    QCOMPARE(blurred.size(), image.size());
    QVERIFY2(qRed(blurred.pixel(16, 4)) > qBlue(blurred.pixel(16, 4)),
             "blur: top should stay predominantly red");
    QVERIFY2(qBlue(blurred.pixel(16, 27)) > qRed(blurred.pixel(16, 27)),
             "blur: bottom should stay predominantly blue");

    // Bloom composites an FBO blur buffer with the source texture — both must share Y.
    QImage bloomSrc(32, 32, QImage::Format_RGBA8888);
    bloomSrc.fill(QColor(0, 0, 0));
    for (int x = 8; x < 24; ++x)
        bloomSrc.setPixel(x, 4, qRgba(255, 255, 255, 255)); // bright bar near top only

    drift::Effect bloom;
    bloom.catalogId = QStringLiteral("bloom_glow");
    bloom.parameters.insert(QStringLiteral("threshold"), 0.4);
    bloom.parameters.insert(QStringLiteral("intensity"), 1.5);
    bloom.parameters.insert(QStringLiteral("blurRadius"), 4.0);

    const QImage bloomed = EffectProcessor::applyEffects(bloomSrc, {bloom});
    QCOMPARE(bloomed.size(), bloomSrc.size());
    const int topGlow = qRed(bloomed.pixel(16, 6)) + qGreen(bloomed.pixel(16, 6))
                        + qBlue(bloomed.pixel(16, 6));
    const int bottomGlow = qRed(bloomed.pixel(16, 28)) + qGreen(bloomed.pixel(16, 28))
                           + qBlue(bloomed.pixel(16, 28));
    QVERIFY2(topGlow > bottomGlow + 20,
             qPrintable(QStringLiteral(
                 "bloom glow should stay near the bright top bar, not mirrored to the bottom "
                 "(top=%1 bottom=%2)")
                            .arg(topGlow)
                            .arg(bottomGlow)));
}

void EngineTest::gpuBrokenShaderPassthrough()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = dir.path();
    const QString pkg = QDir(root).filePath(QStringLiteral("broken_gpu"));
    QVERIFY(QDir().mkpath(pkg));
    QFile json(QDir(pkg).filePath(QStringLiteral("effect.json")));
    QVERIFY(json.open(QIODevice::WriteOnly | QIODevice::Text));
    json.write(R"({
      "id": "test.broken_shader",
      "displayName": "Broken",
      "category": "dreamy",
      "backend": "gpu",
      "parameters": [],
      "pipeline": {"intermediateBuffers": [], "passes": [
        {"passIndex": 0, "fragmentShader": "bad.frag",
         "inputs": [{"type": "source_texture"}], "output": {"type": "canvas"}}
      ]}
    })");
    json.close();
    QFile frag(QDir(pkg).filePath(QStringLiteral("bad.frag")));
    QVERIFY(frag.open(QIODevice::WriteOnly | QIODevice::Text));
    frag.write("#version 330 core\nthis is not valid glsl!!!\n");
    frag.close();

    reloadEffectCatalog({root, QString::fromUtf8(DRIFT_TEST_EFFECTS_DIR)});
    const EffectPresetEntry *def = effectDefForId(QStringLiteral("test.broken_shader"));
    QVERIFY(def);
    QVERIFY(def->isGpu);

    if (!GpuEffectExecutor::instance().isAvailable()) {
        reloadEffectCatalog({QString::fromUtf8(DRIFT_TEST_EFFECTS_DIR)});
        QSKIP("OpenGL offscreen context unavailable");
    }

    QImage image(32, 32, QImage::Format_RGBA8888);
    image.fill(QColor(12, 34, 56));

    drift::Effect effect;
    effect.catalogId = def->meta.id;

    const QImage out = EffectProcessor::applyEffects(image, {effect});
    QCOMPARE(out.size(), image.size());
    QCOMPARE(out.pixel(16, 16), image.pixel(16, 16));

    // Restore catalog for subsequent tests.
    reloadEffectCatalog({QString::fromUtf8(DRIFT_TEST_EFFECTS_DIR)});
}

static QImage makeRedBlueSplitTestImage()
{
    QImage image(64, 32, QImage::Format_RGBA8888);
    image.fill(Qt::transparent);
    for (int x = 0; x < 32; ++x) {
        for (int y = 0; y < 32; ++y)
            image.setPixel(x, y, qRgba(255, 0, 0, 255));
    }
    for (int x = 32; x < 64; ++x) {
        for (int y = 0; y < 32; ++y)
            image.setPixel(x, y, qRgba(0, 0, 255, 255));
    }
    return image;
}

void EngineTest::rgbSplitZeroAmountPassthrough()
{
    const QImage image = makeRedBlueSplitTestImage();

    drift::Effect effect;
    effect.catalogId = QStringLiteral("rgb_split");
    effect.parameters.insert(QStringLiteral("amount"), 0.0);
    effect.parameters.insert(QStringLiteral("angle"), 0.0);
    effect.parameters.insert(QStringLiteral("animated"), false);
    effect.parameters.insert(QStringLiteral("speed"), 1.0);

    const QImage out = EffectProcessor::applyEffects(image, {effect}, 0);
    QCOMPARE(out, image);
}

void EngineTest::rgbSplitShiftsColorChannels()
{
    const QImage image = makeRedBlueSplitTestImage();

    drift::Effect effect;
    effect.catalogId = QStringLiteral("rgb_split");
    effect.parameters.insert(QStringLiteral("amount"), 8.0);
    effect.parameters.insert(QStringLiteral("angle"), 0.0);
    effect.parameters.insert(QStringLiteral("animated"), false);

    const QImage out = EffectProcessor::applyEffects(image, {effect}, 0);
    QVERIFY(!out.isNull());

    const QRgb original = image.pixel(30, 16);
    QCOMPARE(qRed(original), 255);
    QCOMPARE(qGreen(original), 0);
    QCOMPARE(qBlue(original), 0);

    const QRgb shifted = out.pixel(30, 16);
    QVERIFY(shifted != original);
    QCOMPARE(qRed(shifted), 0);
    QCOMPARE(qGreen(shifted), 0);
    QCOMPARE(qBlue(shifted), 0);

    const EffectPresetEntry *def = effectDefForId(QStringLiteral("stylize.rgb_split"));
    QVERIFY(def);
    QCOMPARE(def->meta.id, QStringLiteral("rgb_split"));
    QCOMPARE(buildFilterGraphForEffect({.catalogId = QStringLiteral("rgb_split")}), QString());
}

static QImage makeBlockGlitchTestImage()
{
    QImage image(128, 64, QImage::Format_RGBA8888);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const int stripe = (x / 16) % 2;
            image.setPixel(x, y, qRgba(stripe ? 40 : 220, stripe ? 180 : 60, stripe ? 240 : 90, 255));
        }
    }
    return image;
}

static drift::Effect makeBlockGlitchEffect()
{
    drift::Effect effect;
    effect.catalogId = QStringLiteral("block_glitch");
    effect.parameters.insert(QStringLiteral("intensity"), 1.0);
    effect.parameters.insert(QStringLiteral("blockSize"), 16.0);
    effect.parameters.insert(QStringLiteral("shiftAmount"), 32.0);
    effect.parameters.insert(QStringLiteral("frequency"), 1.0);
    effect.parameters.insert(QStringLiteral("seed"), 42.0);
    return effect;
}

void EngineTest::blockGlitchDeterministicForSameTimeAndSeed()
{
    const QImage image = makeBlockGlitchTestImage();
    const drift::Effect effect = makeBlockGlitchEffect();
    constexpr drift::TimeUs timeUs = 750'000;

    const QImage first = EffectProcessor::applyEffects(image, {effect}, timeUs);
    const QImage second = EffectProcessor::applyEffects(image, {effect}, timeUs);
    QCOMPARE(first, second);

    const EffectPresetEntry *def = effectDefForId(QStringLiteral("block_glitch"));
    QVERIFY(def);
    QVERIFY(def->isGpu);
    QCOMPARE(buildFilterGraphForEffect({.catalogId = QStringLiteral("block_glitch")}), QString());
}

void EngineTest::blockGlitchChangesWithTimelineTime()
{
    const QImage image = makeBlockGlitchTestImage();
    const drift::Effect effect = makeBlockGlitchEffect();

    const QImage atT0 = EffectProcessor::applyEffects(image, {effect}, 0);
    const QImage atT1 = EffectProcessor::applyEffects(image, {effect}, 500'000);
    const QImage atT2 = EffectProcessor::applyEffects(image, {effect}, 1'000'000);

    QVERIFY(atT0 != atT1);
    QVERIFY(atT1 != atT2);

    drift::Effect otherSeed = effect;
    otherSeed.parameters.insert(QStringLiteral("seed"), 99.0);
    const QImage other = EffectProcessor::applyEffects(image, {otherSeed}, 500'000);
    QVERIFY(other != atT1);
}

static drift::Effect makeScanlineGlitchEffect()
{
    drift::Effect effect;
    effect.catalogId = QStringLiteral("scanline_glitch");
    effect.parameters.insert(QStringLiteral("jitter"), 0.5);
    effect.parameters.insert(QStringLiteral("lineStrength"), 0.5);
    effect.parameters.insert(QStringLiteral("colorShift"), 6.0);
    effect.parameters.insert(QStringLiteral("speed"), 2.0);
    return effect;
}

void EngineTest::scanlineGlitchZeroStrengthPassthrough()
{
    const QImage image = makeBlockGlitchTestImage();

    drift::Effect effect;
    effect.catalogId = QStringLiteral("scanline_glitch");
    effect.parameters.insert(QStringLiteral("jitter"), 0.0);
    effect.parameters.insert(QStringLiteral("lineStrength"), 0.0);
    effect.parameters.insert(QStringLiteral("colorShift"), 0.0);
    effect.parameters.insert(QStringLiteral("speed"), 2.0);

    const QImage out = EffectProcessor::applyEffects(image, {effect}, 500'000);
    QCOMPARE(out, image);
}

void EngineTest::scanlineGlitchDeterministicAtFixedTime()
{
    const QImage image = makeBlockGlitchTestImage();
    const drift::Effect effect = makeScanlineGlitchEffect();
    constexpr drift::TimeUs timeUs = 333'000;

    const QImage first = EffectProcessor::applyEffects(image, {effect}, timeUs);
    const QImage second = EffectProcessor::applyEffects(image, {effect}, timeUs);
    QCOMPARE(first, second);

    const EffectPresetEntry *def = effectDefForId(QStringLiteral("scanline_glitch"));
    QVERIFY(def);
    QVERIFY(def->isGpu);
    QCOMPARE(buildFilterGraphForEffect({.catalogId = QStringLiteral("scanline_glitch")}), QString());
}

void EngineTest::scanlineGlitchVisualChangeAtNonzeroSettings()
{
    const QImage image = makeBlockGlitchTestImage();
    const drift::Effect effect = makeScanlineGlitchEffect();

    const QImage out = EffectProcessor::applyEffects(image, {effect}, 250'000);
    QVERIFY(out != image);

    const QImage later = EffectProcessor::applyEffects(image, {effect}, 750'000);
    QVERIFY(later != out);
}

static QImage makeVhsCrtTestImage()
{
    QImage image(96, 64, QImage::Format_RGBA8888);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            image.setPixel(x, y, qRgba(40 + x, 80 + y * 2, 160 - x / 2, 255));
        }
    }
    return image;
}

static drift::Effect makeVhsCrtEffect()
{
    drift::Effect effect;
    effect.catalogId = QStringLiteral("vhs_crt");
    effect.parameters.insert(QStringLiteral("scanlines"), 0.5);
    effect.parameters.insert(QStringLiteral("noise"), 0.4);
    effect.parameters.insert(QStringLiteral("colorBleed"), 5.0);
    effect.parameters.insert(QStringLiteral("distortion"), 0.35);
    effect.parameters.insert(QStringLiteral("vignette"), 0.4);
    effect.parameters.insert(QStringLiteral("desaturation"), 0.25);
    return effect;
}

void EngineTest::vhsCrtZeroSettingsPassthrough()
{
    const QImage image = makeVhsCrtTestImage();

    drift::Effect effect;
    effect.catalogId = QStringLiteral("vhs_crt");
    effect.parameters.insert(QStringLiteral("scanlines"), 0.0);
    effect.parameters.insert(QStringLiteral("noise"), 0.0);
    effect.parameters.insert(QStringLiteral("colorBleed"), 0.0);
    effect.parameters.insert(QStringLiteral("distortion"), 0.0);
    effect.parameters.insert(QStringLiteral("vignette"), 0.0);
    effect.parameters.insert(QStringLiteral("desaturation"), 0.0);

    const QImage out = EffectProcessor::applyEffects(image, {effect}, 500'000);
    QCOMPARE(out, image);
}

void EngineTest::vhsCrtNonzeroModifiesOutput()
{
    const QImage image = makeVhsCrtTestImage();
    const drift::Effect effect = makeVhsCrtEffect();

    const QImage out = EffectProcessor::applyEffects(image, {effect}, 420'000);
    QVERIFY(out != image);

    const EffectPresetEntry *def = effectDefForId(QStringLiteral("vhs_crt"));
    QVERIFY(def);
    QVERIFY(def->isGpu);
    QCOMPARE(buildFilterGraphForEffect({.catalogId = QStringLiteral("vhs_crt")}), QString());
}

void EngineTest::vhsCrtDeterministicAtFixedTime()
{
    const QImage image = makeVhsCrtTestImage();
    const drift::Effect effect = makeVhsCrtEffect();
    constexpr drift::TimeUs timeUs = 420'000;

    const QImage first = EffectProcessor::applyEffects(image, {effect}, timeUs);
    const QImage second = EffectProcessor::applyEffects(image, {effect}, timeUs);
    QCOMPARE(first, second);

    const QImage otherTime = EffectProcessor::applyEffects(image, {effect}, 900'000);
    QVERIFY(otherTime != first);
}

static drift::Effect makeBloomGlowEffect()
{
    drift::Effect effect;
    effect.catalogId = QStringLiteral("bloom_glow");
    effect.parameters.insert(QStringLiteral("threshold"), 0.5);
    effect.parameters.insert(QStringLiteral("intensity"), 1.0);
    effect.parameters.insert(QStringLiteral("blurRadius"), 8.0);
    return effect;
}

void EngineTest::bloomGlowZeroIntensityPassthrough()
{
    QImage image(32, 32, QImage::Format_RGBA8888);
    image.fill(QColor(255, 255, 255));

    drift::Effect effect = makeBloomGlowEffect();
    effect.parameters.insert(QStringLiteral("intensity"), 0.0);

    const QImage out = EffectProcessor::applyEffects(image, {effect});
    QCOMPARE(out, image);
}

void EngineTest::bloomGlowDarkFrameUnchanged()
{
    QImage image(32, 32, QImage::Format_RGBA8888);
    image.fill(QColor(20, 25, 30));

    const drift::Effect effect = makeBloomGlowEffect();
    const QImage out = EffectProcessor::applyEffects(image, {effect});
    QCOMPARE(out, image);
}

void EngineTest::bloomGlowBrightSpotBleedsToNeighbors()
{
    if (!GpuEffectExecutor::instance().isAvailable())
        QSKIP("OpenGL offscreen context unavailable");

    QImage image(32, 32, QImage::Format_RGBA8888);
    image.fill(QColor(10, 10, 10));
    // A small bright block (not a single pixel) so separable blur keeps
    // measurable energy after H+V dilution in 8-bit.
    for (int y = 14; y <= 18; ++y) {
        for (int x = 14; x <= 18; ++x)
            image.setPixel(x, y, qRgba(255, 255, 255, 255));
    }

    const drift::Effect effect = makeBloomGlowEffect();
    const QImage out = EffectProcessor::applyEffects(image, {effect});
    QVERIFY(out != image);

    const QRgb center = out.pixel(16, 16);
    QVERIFY(qRed(center) > 200 || qGreen(center) > 200 || qBlue(center) > 200);

    // Glow should raise at least one pixel outside the bright block.
    bool bled = false;
    for (int dy = -6; dy <= 6 && !bled; ++dy) {
        for (int dx = -6; dx <= 6; ++dx) {
            const int x = 16 + dx;
            const int y = 16 + dy;
            if (x >= 14 && x <= 18 && y >= 14 && y <= 18)
                continue;
            const QRgb n = out.pixel(x, y);
            if (qRed(n) > 12 || qGreen(n) > 12 || qBlue(n) > 12) {
                bled = true;
                break;
            }
        }
    }
    QVERIFY2(bled, "expected bloom bleed into neighboring pixels");

    const EffectPresetEntry *def = effectDefForId(QStringLiteral("bloom_glow"));
    QVERIFY(def);
    QVERIFY(def->isGpu);
    QCOMPARE(buildFilterGraphForEffect({.catalogId = QStringLiteral("bloom_glow")}), QString());
}

static drift::Effect makeRippleWaterEffect()
{
    drift::Effect effect;
    effect.catalogId = QStringLiteral("ripple_water");
    effect.parameters.insert(QStringLiteral("amplitude"), 12.0);
    effect.parameters.insert(QStringLiteral("frequency"), 10.0);
    effect.parameters.insert(QStringLiteral("speed"), 1.0);
    effect.parameters.insert(QStringLiteral("centerX"), 0.5);
    effect.parameters.insert(QStringLiteral("centerY"), 0.5);
    return effect;
}

void EngineTest::rippleWaterZeroAmplitudePassthrough()
{
    const QImage image = makeBlockGlitchTestImage();

    drift::Effect effect = makeRippleWaterEffect();
    effect.parameters.insert(QStringLiteral("amplitude"), 0.0);

    const QImage out = EffectProcessor::applyEffects(image, {effect}, 500'000);
    QCOMPARE(out, image);
}

void EngineTest::rippleWaterNonzeroDisplacementChangesOutput()
{
    const QImage image = makeBlockGlitchTestImage();
    const drift::Effect effect = makeRippleWaterEffect();

    const QImage out = EffectProcessor::applyEffects(image, {effect}, 250'000);
    QVERIFY(out != image);

    const QImage later = EffectProcessor::applyEffects(image, {effect}, 750'000);
    QVERIFY(later != out);

    const EffectPresetEntry *def = effectDefForId(QStringLiteral("ripple_water"));
    QVERIFY(def);
    QVERIFY(def->isGpu);
    QCOMPARE(buildFilterGraphForEffect({.catalogId = QStringLiteral("ripple_water")}), QString());
}

static QImage makeHighContrastRectangleImage()
{
    QImage image(64, 64, QImage::Format_RGBA8888);
    image.fill(QColor(0, 0, 0));
    for (int y = 16; y < 48; ++y) {
        for (int x = 16; x < 48; ++x)
            image.setPixel(x, y, qRgba(255, 255, 255, 255));
    }
    return image;
}

static drift::Effect makeEdgeNeonEffect()
{
    drift::Effect effect;
    effect.catalogId = QStringLiteral("edge_neon");
    effect.parameters.insert(QStringLiteral("threshold"), 0.15);
    effect.parameters.insert(QStringLiteral("intensity"), 1.0);
    effect.parameters.insert(QStringLiteral("radius"), 4.0);
    effect.parameters.insert(QStringLiteral("color"), QStringLiteral("#00ffff"));
    return effect;
}

void EngineTest::edgeNeonZeroIntensityUnchanged()
{
    const QImage image = makeHighContrastRectangleImage();

    drift::Effect effect = makeEdgeNeonEffect();
    effect.parameters.insert(QStringLiteral("intensity"), 0.0);

    const QImage out = EffectProcessor::applyEffects(image, {effect});
    QCOMPARE(out, image);
}

void EngineTest::edgeNeonHighContrastRectangleGlow()
{
    const QImage image = makeHighContrastRectangleImage();
    const drift::Effect effect = makeEdgeNeonEffect();

    const QImage out = EffectProcessor::applyEffects(image, {effect});
    QVERIFY(out != image);

    const QRgb outside = out.pixel(14, 32);
    QVERIFY(qGreen(outside) > qGreen(image.pixel(14, 32)));
    QVERIFY(qBlue(outside) > qBlue(image.pixel(14, 32)));

    const QRgb inside = out.pixel(32, 32);
    QCOMPARE(qRed(inside), 255);
    QCOMPARE(qGreen(inside), 255);
    QCOMPARE(qBlue(inside), 255);

    const EffectPresetEntry *def = effectDefForId(QStringLiteral("edge_neon"));
    QVERIFY(def);
    QVERIFY(def->isGpu);
    QCOMPARE(def->fixedParams.value(QStringLiteral("color")).toString(), QStringLiteral("#00ffff"));
    QCOMPARE(buildFilterGraphForEffect({.catalogId = QStringLiteral("edge_neon")}), QString());
}

static drift::Effect makeDigitalGlitchEffect()
{
    drift::Effect effect;
    effect.catalogId = QStringLiteral("digital_glitch");
    effect.parameters.insert(QStringLiteral("intensity"), 0.75);
    effect.parameters.insert(QStringLiteral("frequency"), 0.5);
    effect.parameters.insert(QStringLiteral("rgbAmount"), 12.0);
    effect.parameters.insert(QStringLiteral("blockAmount"), 0.6);
    effect.parameters.insert(QStringLiteral("flashAmount"), 0.25);
    effect.parameters.insert(QStringLiteral("seed"), 42.0);
    return effect;
}

void EngineTest::digitalGlitchZeroIntensityUnchanged()
{
    const QImage image = makeBlockGlitchTestImage();

    drift::Effect effect = makeDigitalGlitchEffect();
    effect.parameters.insert(QStringLiteral("intensity"), 0.0);

    const QImage out = EffectProcessor::applyEffects(image, {effect}, 500'000);
    QCOMPARE(out, image);
}

void EngineTest::digitalGlitchDeterministicForFixedTimeAndSeed()
{
    const QImage image = makeBlockGlitchTestImage();
    const drift::Effect effect = makeDigitalGlitchEffect();
    constexpr drift::TimeUs timeUs = 620'000;

    const QImage first = EffectProcessor::applyEffects(image, {effect}, timeUs);
    const QImage second = EffectProcessor::applyEffects(image, {effect}, timeUs);
    QCOMPARE(first, second);
    QVERIFY(first != image);

    const EffectPresetEntry *def = effectDefForId(QStringLiteral("digital_glitch"));
    QVERIFY(def);
    QVERIFY(def->isGpu);
    QCOMPARE(buildFilterGraphForEffect({.catalogId = QStringLiteral("digital_glitch")}), QString());

    drift::Effect otherSeed = effect;
    otherSeed.parameters.insert(QStringLiteral("seed"), 99.0);
    const QImage other = EffectProcessor::applyEffects(image, {otherSeed}, timeUs);
    QVERIFY(other != first);
}

static drift::Effect makeFilmBurnEffect()
{
    drift::Effect effect;
    effect.catalogId = QStringLiteral("film_burn");
    effect.parameters.insert(QStringLiteral("intensity"), 0.8);
    effect.parameters.insert(QStringLiteral("warmth"), 0.85);
    effect.parameters.insert(QStringLiteral("flicker"), 0.4);
    effect.parameters.insert(QStringLiteral("position"), QStringLiteral("left"));
    effect.parameters.insert(QStringLiteral("seed"), 7.0);
    return effect;
}

void EngineTest::filmBurnZeroIntensityUnchanged()
{
    QImage image(64, 64, QImage::Format_RGBA8888);
    image.fill(QColor(30, 30, 40));

    drift::Effect effect = makeFilmBurnEffect();
    effect.parameters.insert(QStringLiteral("intensity"), 0.0);

    const QImage out = EffectProcessor::applyEffects(image, {effect}, 400'000);
    QCOMPARE(out, image);
}

void EngineTest::filmBurnAddsWarmLeakContribution()
{
    QImage image(64, 64, QImage::Format_RGBA8888);
    image.fill(QColor(20, 22, 35));

    const drift::Effect effect = makeFilmBurnEffect();
    constexpr drift::TimeUs timeUs = 400'000;

    const QImage out = EffectProcessor::applyEffects(image, {effect}, timeUs);
    QVERIFY(out != image);

    const QRgb edge = out.pixel(0, 32);
    const QRgb original = image.pixel(0, 32);
    QVERIFY(qRed(edge) > qRed(original));
    QVERIFY(qGreen(edge) > qGreen(original));
    QVERIFY(qRed(edge) > qBlue(edge));

    const QImage second = EffectProcessor::applyEffects(image, {effect}, timeUs);
    QCOMPARE(out, second);

    const EffectPresetEntry *def = effectDefForId(QStringLiteral("film_burn"));
    QVERIFY(def);
    QVERIFY(def->isGpu);
    QCOMPARE(def->fixedParams.value(QStringLiteral("position")).toString(), QStringLiteral("left"));
    QCOMPARE(buildFilterGraphForEffect({.catalogId = QStringLiteral("film_burn")}), QString());
}

void EngineTest::timeEchoBlendDeterministic()
{
    auto makeFrame = [](const QColor &color, int rectX) {
        QImage image(32, 32, QImage::Format_RGBA8888);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.fillRect(rectX, 10, 10, 10, color);
        return image;
    };

    const QList<QImage> samples = {makeFrame(Qt::red, 18), makeFrame(Qt::blue, 4)};
    const QImage first =
        CompositorFrameHistory::applyTimeEcho(samples, 0.55, CompositorFrameHistory::EchoBlendMode::Normal);
    const QImage second =
        CompositorFrameHistory::applyTimeEcho(samples, 0.55, CompositorFrameHistory::EchoBlendMode::Normal);
    QCOMPARE(first, second);
    QVERIFY(first != samples.first());
    QVERIFY(qBlue(first.pixel(8, 14)) > 0);
    QVERIFY(qRed(first.pixel(22, 14)) > 200);
}

void EngineTest::timeEchoBlendIncludesHistoryContribution()
{
    auto makeFrame = [](const QColor &color, int rectX) {
        QImage image(32, 32, QImage::Format_RGBA8888);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.fillRect(rectX, 10, 10, 10, color);
        return image;
    };

    const QList<QImage> samples = {makeFrame(Qt::red, 18), makeFrame(Qt::blue, 4)};
    const QImage normal =
        CompositorFrameHistory::applyTimeEcho(samples, 0.5, CompositorFrameHistory::EchoBlendMode::Normal);
    const QImage currentOnly =
        CompositorFrameHistory::applyTimeEcho({samples.first()}, 0.5, CompositorFrameHistory::EchoBlendMode::Normal);
    QVERIFY(normal != currentOnly);
    QVERIFY(qBlue(normal.pixel(8, 14)) > qBlue(currentOnly.pixel(8, 14)));
}

static drift::Effect makeTimeEchoEffect(const QString &blendMode = QStringLiteral("add"))
{
    drift::Effect effect;
    effect.catalogId = QStringLiteral("time_echo");
    effect.parameters.insert(QStringLiteral("frames"), 4);
    effect.parameters.insert(QStringLiteral("decay"), 0.55);
    effect.parameters.insert(QStringLiteral("blendMode"), blendMode);
    return effect;
}

void EngineTest::timeEchoDeterministicAtFixedTimelineTime()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = makeColorSegmentsVideo(dir);
    if (path.isEmpty())
        QSKIP("ffmpeg not available to generate a test clip");

    drift::Project project;
    project.setResolution(64, 64);
    project.setFps(25);
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Video});

    drift::Clip clip;
    clip.id = QStringLiteral("video");
    clip.type = drift::ClipType::Video;
    clip.path = path;
    clip.timelineStart = 0;
    clip.timelineDuration = drift::secondsToUs(3.0);
    clip.effects.append(makeTimeEchoEffect());
    project.tracks()[0].clips.append(clip);

    FrameCompositor compositor;
    compositor.setProject(&project);

    constexpr drift::TimeUs timeUs = drift::secondsToUs(2.1);
    const QImage first = compositor.compositeAt(timeUs);
    const QImage second = compositor.compositeAt(timeUs);
    QCOMPARE(first, second);
    QVERIFY(!first.isNull());
}

void EngineTest::timeEchoBlendsPriorVideoFrames()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = makeColorSegmentsVideo(dir);
    if (path.isEmpty())
        QSKIP("ffmpeg not available to generate a test clip");

    drift::Project project;
    project.setResolution(64, 64);
    project.setFps(25);
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Video});

    drift::Clip clip;
    clip.id = QStringLiteral("video");
    clip.type = drift::ClipType::Video;
    clip.path = path;
    clip.timelineStart = 0;
    clip.timelineDuration = drift::secondsToUs(3.0);
    project.tracks()[0].clips.append(clip);

    FrameCompositor compositor;
    compositor.setProject(&project);

    constexpr drift::TimeUs timeUs = drift::secondsToUs(2.1);
    const QImage withoutEcho = compositor.compositeAt(timeUs);

    clip.effects.append(makeTimeEchoEffect(QStringLiteral("add")));
    project.tracks()[0].clips.clear();
    project.tracks()[0].clips.append(clip);
    compositor.setProject(&project);

    const QImage withEcho = compositor.compositeAt(timeUs);
    QVERIFY(withEcho != withoutEcho);

    const EffectPresetEntry *def = effectDefForId(QStringLiteral("time_echo"));
    QVERIFY(def);
    QVERIFY(def->isGpu);
    QCOMPARE(def->fixedParams.value(QStringLiteral("blendMode")).toString(), QStringLiteral("normal"));
    QCOMPARE(buildFilterGraphForEffect({.catalogId = QStringLiteral("time_echo")}), QString());
}

static drift::Effect makeShockwavePulseEffect()
{
    drift::Effect effect;
    effect.catalogId = QStringLiteral("shockwave_pulse");
    effect.parameters.insert(QStringLiteral("centerX"), 0.5);
    effect.parameters.insert(QStringLiteral("centerY"), 0.5);
    effect.parameters.insert(QStringLiteral("radius"), 0.0);
    effect.parameters.insert(QStringLiteral("width"), 0.12);
    effect.parameters.insert(QStringLiteral("strength"), 0.6);
    effect.parameters.insert(QStringLiteral("speed"), 1.0);
    return effect;
}

void EngineTest::shockwavePulseZeroStrengthPassthrough()
{
    const QImage image = makeBlockGlitchTestImage();

    drift::Effect effect = makeShockwavePulseEffect();
    effect.parameters.insert(QStringLiteral("strength"), 0.0);

    const QImage out = EffectProcessor::applyEffects(image, {effect}, 500'000);
    QCOMPARE(out, image);
}

void EngineTest::shockwavePulseChangesPixelsNearWavefront()
{
    const QImage image = makeBlockGlitchTestImage();
    const drift::Effect effect = makeShockwavePulseEffect();

    // speed=1 => wave radius 0.233 at t=233ms; pixel (80,32) lies on that ring from center (64,32).
    constexpr drift::TimeUs timeUs = 233'000;
    const QImage out = EffectProcessor::applyEffects(image, {effect}, timeUs);
    QVERIFY(out != image);
    QVERIFY(out.pixel(80, 32) != image.pixel(80, 32));

    const QImage awayFromWave = EffectProcessor::applyEffects(image, {effect}, 50'000);
    QVERIFY(awayFromWave.pixel(80, 32) != out.pixel(80, 32));

    const EffectPresetEntry *def = effectDefForId(QStringLiteral("shockwave_pulse"));
    QVERIFY(def);
    QVERIFY(def->isGpu);
    QCOMPARE(buildFilterGraphForEffect({.catalogId = QStringLiteral("shockwave_pulse")}), QString());
}

void EngineTest::compositorCrossfadeBetweenShapeClips()
{
    drift::Project project;
    project.setResolution(128, 128);
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Video});

    drift::Clip clipA;
    clipA.id = QStringLiteral("a");
    clipA.type = drift::ClipType::Shape;
    clipA.timelineStart = 0;
    clipA.timelineDuration = drift::secondsToUs(2.0);
    clipA.shapeStyle.kind = drift::ShapeKind::Rectangle;
    clipA.shapeStyle.fill = Qt::red;

    drift::Clip clipB;
    clipB.id = QStringLiteral("b");
    clipB.type = drift::ClipType::Shape;
    clipB.timelineStart = drift::secondsToUs(2.0);
    clipB.timelineDuration = drift::secondsToUs(2.0);
    clipB.shapeStyle.kind = drift::ShapeKind::Rectangle;
    clipB.shapeStyle.fill = Qt::blue;

    project.tracks()[0].clips.append(clipA);
    project.tracks()[0].clips.append(clipB);

    drift::Transition transition;
    transition.id = QStringLiteral("tr");
    transition.fromClipId = clipA.id;
    transition.toClipId = clipB.id;
    transition.kindId = QStringLiteral("crossfade");
    transition.durationUs = drift::secondsToUs(1.0);
    project.tracks()[0].transitions.append(transition);

    FrameCompositor compositor;
    compositor.setProject(&project);

    const QImage redOnly = compositor.compositeAt(drift::secondsToUs(1.0));
    const QImage blueOnly = compositor.compositeAt(drift::secondsToUs(3.0));
    const QImage mid = compositor.compositeAt(drift::secondsToUs(2.0));
    QVERIFY(!mid.isNull());
    const QRgb center = mid.pixel(64, 64);
    const QRgb redCenter = redOnly.pixel(64, 64);
    const QRgb blueCenter = blueOnly.pixel(64, 64);
    QVERIFY(center != redCenter);
    QVERIFY(center != blueCenter);
    QVERIFY(qRed(center) > 0);
    QVERIFY(qBlue(center) > 0);
}

static void appendRedBlueShapeTransition(drift::Project &project, const QString &kindId)
{
    project.setResolution(128, 128);
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Video});

    drift::Clip clipA;
    clipA.id = QStringLiteral("a");
    clipA.type = drift::ClipType::Shape;
    clipA.timelineStart = 0;
    clipA.timelineDuration = drift::secondsToUs(2.0);
    clipA.shapeStyle.kind = drift::ShapeKind::Rectangle;
    clipA.shapeStyle.fill = Qt::red;

    drift::Clip clipB;
    clipB.id = QStringLiteral("b");
    clipB.type = drift::ClipType::Shape;
    clipB.timelineStart = drift::secondsToUs(2.0);
    clipB.timelineDuration = drift::secondsToUs(2.0);
    clipB.shapeStyle.kind = drift::ShapeKind::Rectangle;
    clipB.shapeStyle.fill = Qt::blue;

    project.tracks()[0].clips.append(clipA);
    project.tracks()[0].clips.append(clipB);

    drift::Transition transition;
    transition.id = QStringLiteral("tr");
    transition.fromClipId = clipA.id;
    transition.toClipId = clipB.id;
    transition.kindId = kindId;
    transition.durationUs = drift::secondsToUs(1.0);
    project.tracks()[0].transitions.append(transition);
}

void EngineTest::compositorDipToBlackMidpointIsBlack()
{
    drift::Project project;
    appendRedBlueShapeTransition(project, QStringLiteral("dip"));

    FrameCompositor compositor;
    compositor.setProject(&project);

    const QImage mid = compositor.compositeAt(drift::secondsToUs(2.0));
    QVERIFY(!mid.isNull());
    const QRgb center = mid.pixel(64, 64);
    QVERIFY(qRed(center) < 30);
    QVERIFY(qGreen(center) < 30);
    QVERIFY(qBlue(center) < 30);
}

void EngineTest::compositorWipeRightRevealsIncomingClip()
{
    drift::Project project;
    appendRedBlueShapeTransition(project, QStringLiteral("wipe_right"));

    FrameCompositor compositor;
    compositor.setProject(&project);

    const QImage early = compositor.compositeAt(drift::secondsToUs(1.75));
    const QImage late = compositor.compositeAt(drift::secondsToUs(2.25));
    QVERIFY(!early.isNull());
    QVERIFY(!late.isNull());
    // Shape clips are small and centered; sample canvas center, not edges.
    QVERIFY(qRed(early.pixel(64, 64)) > qBlue(early.pixel(64, 64)));
    QVERIFY(qBlue(late.pixel(64, 64)) > qRed(late.pixel(64, 64)));
}

void EngineTest::transitionCatalogLoadsAllPackages()
{
    const QStringList ids = transitionPresetIds();
    QCOMPARE(ids.size(), 28);

    // The nine ids the pre-shader enum serialized must all still resolve.
    for (const char *legacy : {"crossfade", "dip", "dip_white", "wipe_left", "wipe_right",
                               "wipe_up", "wipe_down", "push_left", "zoom_in"}) {
        const TransitionPresetEntry *def = transitionDefForId(QString::fromUtf8(legacy));
        QVERIFY2(def, legacy);
        QVERIFY2(def->gpu.valid, legacy);
    }

    QCOMPARE(transitionDefForId(QStringLiteral("dip"))->audioCurve, QStringLiteral("dip"));
    QCOMPARE(transitionDefForId(QStringLiteral("crossfade"))->audioCurve,
             QStringLiteral("crossfade"));

    // matrix_rain is the one package with a static texture asset.
    const TransitionPresetEntry *rain = transitionDefForId(QStringLiteral("matrix_rain"));
    QVERIFY(rain);
    QCOMPARE(rain->gpu.textures.size(), 1);
    QVERIFY(QFileInfo::exists(rain->gpu.textures.first().path));
}

// Two solid layers through the real GPU path: the shader must actually receive source 1 as
// u_toTexture, not a second copy of source 0.
void EngineTest::gpuTransitionBindsBothSources()
{
    if (!GpuEffectExecutor::instance().isAvailable())
        QSKIP("no OpenGL context available");

    QImage red(64, 64, QImage::Format_RGBA8888);
    red.fill(Qt::red);
    QImage blue(64, 64, QImage::Format_RGBA8888);
    blue.fill(Qt::blue);

    const TransitionPresetEntry *def = transitionDefForId(QStringLiteral("crossfade"));
    QVERIFY(def);

    // Returns a null image if the pipeline reported failure, so the QVERIFYs stay in the test body.
    auto run = [&](double p) -> QImage {
        bool ok = false;
        const QImage out = GpuEffectExecutor::instance().apply(
            QLatin1String(kTransitionCacheKeyPrefix) + def->meta.id, def->gpu, {red, blue}, {}, 0, p,
            &ok);
        return ok ? out : QImage();
    };

    const QImage atStart = run(0.0);
    const QImage atEnd = run(1.0);
    const QImage atMid = run(0.5);
    QVERIFY(!atStart.isNull() && !atEnd.isNull() && !atMid.isNull());

    const QRgb start = atStart.pixel(32, 32);
    QVERIFY(qRed(start) > 240 && qBlue(start) < 15);

    const QRgb end = atEnd.pixel(32, 32);
    QVERIFY(qBlue(end) > 240 && qRed(end) < 15);

    const QRgb mid = atMid.pixel(32, 32);
    QVERIFY(qRed(mid) > 100 && qRed(mid) < 155);
    QVERIFY(qBlue(mid) > 100 && qBlue(mid) < 155);
}

// A broken shader must fall back to a CPU crossfade, never to a black frame or to clip A alone.
void EngineTest::brokenTransitionShaderFallsBackToCrossfade()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString pkg = QDir(dir.path()).filePath(QStringLiteral("broken"));
    QVERIFY(QDir().mkpath(pkg));

    QFile frag(QDir(pkg).filePath(QStringLiteral("main.frag")));
    QVERIFY(frag.open(QIODevice::WriteOnly));
    frag.write("#version 330 core\nthis is not glsl\n");
    frag.close();

    QFile json(QDir(pkg).filePath(QStringLiteral("transition.json")));
    QVERIFY(json.open(QIODevice::WriteOnly));
    json.write(R"({"id":"broken","displayName":"Broken","pipeline":{"passes":[{"passIndex":0,
        "fragmentShader":"main.frag","inputs":[{"type":"source_texture","index":0},
        {"type":"source_texture","index":1}],"output":{"type":"canvas"}}]}})");
    json.close();

    reloadTransitionCatalog({dir.path()});

    drift::Project project;
    appendRedBlueShapeTransition(project, QStringLiteral("broken"));
    FrameCompositor compositor;
    compositor.setProject(&project);

    const QImage mid = compositor.compositeAt(drift::secondsToUs(2.0));
    QVERIFY(!mid.isNull());
    const QRgb center = mid.pixel(64, 64);
    QVERIFY(qRed(center) > 0);
    QVERIFY(qBlue(center) > 0);

    reloadTransitionCatalog({QString::fromUtf8(DRIFT_TEST_TRANSITIONS_DIR)});
}

// The old CPU path never handled ClipType::Text inside drawTransitionFrame, and the main draw
// loop skipped both transition clips — so a text clip in a transition simply disappeared.
// Rendering each side into its own full-canvas layer routes text through the normal path.
void EngineTest::textClipRendersInsideTransition()
{
    drift::Project project;
    project.setResolution(128, 128);
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Video});

    drift::Clip text;
    text.id = QStringLiteral("a");
    text.type = drift::ClipType::Text;
    text.timelineStart = 0;
    text.timelineDuration = drift::secondsToUs(2.0);
    text.textContent = QStringLiteral("HELLO");
    text.textStyle.color = Qt::white;
    text.textStyle.pixelSize = 28;

    drift::Clip shape;
    shape.id = QStringLiteral("b");
    shape.type = drift::ClipType::Shape;
    shape.timelineStart = drift::secondsToUs(2.0);
    shape.timelineDuration = drift::secondsToUs(2.0);
    shape.shapeStyle.kind = drift::ShapeKind::Rectangle;
    shape.shapeStyle.fill = Qt::blue;

    project.tracks()[0].clips.append(text);
    project.tracks()[0].clips.append(shape);

    drift::Transition transition;
    transition.id = QStringLiteral("tr");
    transition.fromClipId = text.id;
    transition.toClipId = shape.id;
    transition.kindId = QStringLiteral("crossfade");
    transition.durationUs = drift::secondsToUs(1.0);
    project.tracks()[0].transitions.append(transition);

    FrameCompositor compositor;
    compositor.setProject(&project);

    // Early in the window the text still dominates: some pixel must be lit by the glyphs.
    const QImage frame = compositor.compositeAt(drift::secondsToUs(1.6));
    QVERIFY(!frame.isNull());

    int lit = 0;
    for (int y = 0; y < frame.height(); ++y) {
        for (int x = 0; x < frame.width(); ++x) {
            const QRgb px = frame.pixel(x, y);
            if (qRed(px) > 150 && qGreen(px) > 150 && qBlue(px) > 150)
                ++lit;
        }
    }
    QVERIFY2(lit > 0, "text clip vanished inside the transition window");
}

// Transitions must be pure functions of (A, B, progress). If any shader smuggled in
// frame-to-frame state, rendering a later frame first would change this frame's output —
// which would also make the exporter disagree with the preview.
void EngineTest::transitionRenderingIsDeterministic()
{
    if (!GpuEffectExecutor::instance().isAvailable())
        QSKIP("no OpenGL context available");

    for (const QString &kindId : transitionPresetIds()) {
        drift::Project project;
        appendRedBlueShapeTransition(project, kindId);

        FrameCompositor compositor;
        compositor.setProject(&project);

        const QImage first = compositor.compositeAt(drift::secondsToUs(1.75));

        // Jump forward, then scrub back to the same time.
        compositor.compositeAt(drift::secondsToUs(2.4));
        const QImage rescrubbed = compositor.compositeAt(drift::secondsToUs(1.75));

        QVERIFY2(first == rescrubbed, qPrintable(kindId));
    }
}

namespace {

// The bundle is fetched, not committed, so an offline checkout legitimately has no fonts.
#define SKIP_WITHOUT_FONTS()                                                                        \
    do {                                                                                            \
        if (fontCatalog().isEmpty())                                                                \
            QSKIP("font bundle not present — see recipes/fetch-fonts.py in drift-addons");          \
    } while (false)

drift::Clip makeTextClip(const QString &text, const QRectF &rect)
{
    drift::Clip clip;
    clip.id = QStringLiteral("text");
    clip.type = drift::ClipType::Text;
    clip.textContent = text;
    clip.timelineStart = 0;
    clip.timelineDuration = drift::secondsToUs(4.0);
    clip.transformX.setKeyframe(0, rect.x());
    clip.transformY.setKeyframe(0, rect.y());
    clip.transformW.setKeyframe(0, rect.width());
    clip.transformH.setKeyframe(0, rect.height());
    return clip;
}

int litPixels(const QImage &image)
{
    int lit = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(image.pixel(x, y)) > 8)
                ++lit;
        }
    }
    return lit;
}

double meanLuminance(const QImage &image)
{
    double sum = 0.0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QRgb px = image.pixel(x, y);
            sum += 0.2126 * qRed(px) + 0.7152 * qGreen(px) + 0.0722 * qBlue(px);
        }
    }
    return sum / (image.width() * image.height());
}

// Vertical centre of mass of the lit pixels — how the slide is observed.
double litCentroidY(const QImage &image)
{
    double weighted = 0.0;
    double total = 0.0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QRgb px = image.pixel(x, y);
            const double lum = 0.2126 * qRed(px) + 0.7152 * qGreen(px) + 0.0722 * qBlue(px);
            weighted += lum * y;
            total += lum;
        }
    }
    return total > 0.0 ? weighted / total : 0.0;
}

} // namespace

void EngineTest::fontCatalogLoadsFamilies()
{
    SKIP_WITHOUT_FONTS();

    const QList<FontFamilyEntry> &families = fontCatalog();
    QVERIFY2(families.size() >= 20, qPrintable(QString::number(families.size())));

    for (const FontFamilyEntry &entry : families) {
        QVERIFY2(!entry.qtFamily.isEmpty(), qPrintable(entry.family));
        QVERIFY2(!entry.faces.isEmpty(), qPrintable(entry.family));
        QVERIFY2(!entry.category.isEmpty(), qPrintable(entry.family));
    }

    const FontFamilyEntry *montserrat = fontFamilyForName(QStringLiteral("Montserrat"));
    QVERIFY(montserrat);
    QVERIFY(montserrat->weights().size() >= 6);
    QVERIFY(montserrat->hasItalic());

    // Display faces really do ship a single weight — the picker must not invent more.
    const FontFamilyEntry *anton = fontFamilyForName(QStringLiteral("Anton"));
    QVERIFY(anton);
    QCOMPARE(anton->weights().size(), 1);
    QVERIFY(!anton->hasItalic());

    QVERIFY(fontFamilyForName(QStringLiteral("Pacifico")));
    QVERIFY(!fontFamilyForName(QStringLiteral("No Such Family")));
}

void EngineTest::fontForStyleResolvesRequestedFace()
{
    SKIP_WITHOUT_FONTS();

    drift::TextStyle style;
    style.fontFamily = QStringLiteral("Montserrat");
    style.fontWeight = 900;
    QCOMPARE(fontForStyle(style, 40).styleName(), QStringLiteral("Black"));

    style.fontWeight = 400;
    QCOMPARE(fontForStyle(style, 40).styleName(), QStringLiteral("Regular"));

    // 250 is not a real face; the nearest one in the requested direction wins.
    style.fontWeight = 250;
    QCOMPARE(fontForStyle(style, 40).styleName(), QStringLiteral("ExtraLight"));

    // Anton has no italic, so the upright face stands in rather than Qt faking an oblique.
    style.fontFamily = QStringLiteral("Anton");
    style.fontWeight = 400;
    style.italic = true;
    QVERIFY(!fontForStyle(style, 40).italic());

    // Families outside the bundle still resolve through the system database.
    style.fontFamily = QStringLiteral("Sans Serif");
    QCOMPARE(fontForStyle(style, 40).pixelSize(), 40);
}

void EngineTest::textRasterIsCached()
{
    SKIP_WITHOUT_FONTS();

    const QRectF rect(0, 0, 400, 200);
    drift::Clip clip = makeTextClip(QStringLiteral("Cache me"), rect);
    clip.textStyle.fontFamily = QStringLiteral("Inter");

    const TextRasterResult first = rasterizeText(clip, rect, 1.0);
    const TextRasterResult second = rasterizeText(clip, rect, 1.0);
    QVERIFY(!first.image.isNull());
    // Same underlying QImage, so the second frame did no rasterization at all.
    QCOMPARE(first.image.cacheKey(), second.image.cacheKey());

    // The animation is applied to the layer, never to the pixels, so it must not evict the raster.
    clip.textStyle.animIn = {drift::TextAnimKind::Fade, drift::secondsToUs(1.0), drift::TextEase::EaseOut};
    const TextRasterResult animated = rasterizeText(clip, rect, 1.0);
    QCOMPARE(animated.image.cacheKey(), first.image.cacheKey());

    clip.textStyle.color = Qt::red;
    QVERIFY(rasterizeText(clip, rect, 1.0).image.cacheKey() != first.image.cacheKey());
}

void EngineTest::textDecorationsAreNotCropped()
{
    SKIP_WITHOUT_FONTS();

    const QRectF rect(100, 100, 300, 80);
    drift::Clip clip = makeTextClip(QStringLiteral("Edge"), rect);
    clip.textStyle.fontFamily = QStringLiteral("Anton");
    clip.textStyle.pixelSize = 64;

    const TextRasterResult plain = rasterizeText(clip, rect, 1.0);
    QVERIFY(!plain.image.isNull());

    clip.textStyle.outlineWidth = 12.0;
    clip.textStyle.outlineEnabled = true;
    clip.textStyle.shadowEnabled = true;
    clip.textStyle.shadowBlur = 10.0;
    clip.textStyle.shadowOffsetY = 8.0;
    const TextRasterResult decorated = rasterizeText(clip, rect, 1.0);

    // The raster grows past the layout rect on every side, so nothing is clipped at the edge...
    QVERIFY(decorated.rect.width() > rect.width());
    QVERIFY(decorated.rect.height() > rect.height());
    QVERIFY(decorated.rect.left() < rect.left());
    QVERIFY(decorated.rect.top() < rect.top());
    // ...and the destination rect follows the image, so it still lands where the user put it.
    QCOMPARE(decorated.rect.center(), rect.center());
    QCOMPARE(decorated.image.width(), qRound(decorated.rect.width()));
    QCOMPARE(decorated.image.height(), qRound(decorated.rect.height()));

    // The stroke and shadow really do add ink.
    QVERIFY(litPixels(decorated.image) > litPixels(plain.image));

    // The same has to hold for the decorations a style pack adds, which sit outside the glyphs:
    // a glow bleeds outward, a highlight pill sits behind the word and the rule sits under it.
    clip.textStyle.outlineWidth = 0.0;
    clip.textStyle.shadowEnabled = false;
    clip.textStyle.glowEnabled = true;
    clip.textStyle.glowRadius = 14.0;
    clip.textStyle.wordHighlight.enabled = true;
    clip.textStyle.wordHighlight.padding = 10.0;
    clip.textStyle.underlineEnabled = true;
    clip.textStyle.underlineOffset = 10.0;
    clip.textStyle.underlineWidth = 8.0;
    const TextRasterResult packed = rasterizeText(clip, rect, 1.0);
    QCOMPARE(packed.rect.center(), rect.center());
    QCOMPARE(packed.image.width(), qRound(packed.rect.width()));
    QCOMPARE(packed.image.height(), qRound(packed.rect.height()));
    QVERIFY(litPixels(packed.image) > litPixels(plain.image));
}

namespace {

// Pixels close enough to pure red to have come from the accent colour rather than antialiasing.
int redPixels(const QImage &image)
{
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QRgb px = image.pixel(x, y);
            if (qAlpha(px) > 200 && qRed(px) > 200 && qGreen(px) < 80 && qBlue(px) < 80)
                ++count;
        }
    }
    return count;
}

QRect inkBounds(const QImage &image)
{
    int left = image.width(), right = -1, top = image.height(), bottom = -1;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(image.pixel(x, y)) <= 8)
                continue;
            left = qMin(left, x);
            right = qMax(right, x);
            top = qMin(top, y);
            bottom = qMax(bottom, y);
        }
    }
    return right < 0 ? QRect() : QRect(QPoint(left, top), QPoint(right, bottom));
}

} // namespace

void EngineTest::wordAccentRecoloursChosenWords()
{
    SKIP_WITHOUT_FONTS();

    const QRectF rect(0, 0, 700, 200);
    drift::Clip clip = makeTextClip(QStringLiteral("Number of thumbnails that"), rect);
    clip.textStyle.fontFamily = QStringLiteral("Inter");
    clip.textStyle.fontWeight = 800;
    clip.textStyle.pixelSize = 48;

    const QImage plain = rasterizeText(clip, rect, 1.0).image;
    QVERIFY(!plain.isNull());
    QCOMPARE(redPixels(plain), 0);

    clip.textStyle.accent.rule = drift::WordAccentRule::FirstWord;
    clip.textStyle.accent.colorEnabled = true;
    clip.textStyle.accent.color = QColor(255, 0, 0);
    const QImage accented = rasterizeText(clip, rect, 1.0).image;

    // Only the picked word changes colour: some ink is red, most of it is not.
    const int red = redPixels(accented);
    QVERIFY(red > 0);
    QVERIFY(red < litPixels(accented) / 2);

    // A rule that picks nothing leaves the block exactly as it was.
    clip.textStyle.accent.rule = drift::WordAccentRule::None;
    QCOMPARE(redPixels(rasterizeText(clip, rect, 1.0).image), 0);
}

void EngineTest::karaokeAccentFollowsThePlayhead()
{
    SKIP_WITHOUT_FONTS();

    const QRectF rect(0, 0, 700, 200);
    const QString text = QStringLiteral("Number of thumbnails that");
    drift::Clip clip = makeTextClip(text, rect);
    clip.textStyle.fontFamily = QStringLiteral("Inter");
    clip.textStyle.fontWeight = 800;
    clip.textStyle.pixelSize = 48;
    clip.textStyle.accent.rule = drift::WordAccentRule::Karaoke;
    clip.textStyle.accent.colorEnabled = true;
    clip.textStyle.accent.color = QColor(255, 0, 0);

    const QImage first = rasterizeText(clip, text, rect, 1.0, 0).image;
    const QImage third = rasterizeText(clip, text, rect, 1.0, 2).image;
    QVERIFY(!first.isNull());
    QVERIFY(redPixels(first) > 0);
    QVERIFY(redPixels(third) > 0);
    // Different word lit, so genuinely different pixels — not just a different cache slot.
    QVERIFY(first != third);

    // The spoken word still only costs one raster: the same index hits the cache.
    QCOMPARE(rasterizeText(clip, text, rect, 1.0, 0).image.cacheKey(), first.cacheKey());
}

void EngineTest::accentSizeScaleWidensTheBlock()
{
    SKIP_WITHOUT_FONTS();

    const QRectF rect(0, 0, 900, 240);
    drift::Clip clip = makeTextClip(QStringLiteral("Number of thumbnails"), rect);
    clip.textStyle.fontFamily = QStringLiteral("Inter");
    clip.textStyle.fontWeight = 800;
    clip.textStyle.pixelSize = 40;

    const QRect plain = inkBounds(rasterizeText(clip, rect, 1.0).image);
    QVERIFY(plain.isValid());

    clip.textStyle.accent.rule = drift::WordAccentRule::FirstWord;
    clip.textStyle.accent.sizeScale = 2.0;
    const QRect scaled = inkBounds(rasterizeText(clip, rect, 1.0).image);

    // The scaled word takes more room on the line and stands taller than the rest.
    QVERIFY(scaled.width() > plain.width());
    QVERIFY(scaled.height() > plain.height());
}

void EngineTest::everyStylePackRenders()
{
    SKIP_WITHOUT_FONTS();

    // A pack naming a font that is not bundled, or an all-transparent colour combination, would
    // ship a card and a caption that render as nothing at all.
    const QRectF rect(0, 0, 900, 300);
    const QString text = QStringLiteral("Number of thumbnails that");
    for (const drift::TextPreset &preset : drift::textPresets()) {
        drift::Clip clip = makeTextClip(text, rect);
        clip.textStyle = preset.style;
        const int activeWord =
            preset.style.accent.rule == drift::WordAccentRule::Karaoke ? 1 : -1;
        const QImage image = rasterizeText(clip, text, rect, 1.0, activeWord).image;
        QVERIFY2(!image.isNull(), qPrintable(preset.id));
        QVERIFY2(litPixels(image) > 0, qPrintable(preset.id));
    }
}

void EngineTest::heavyWeightsRenderSolidGlyphs()
{
    SKIP_WITHOUT_FONTS();

    // Where two glyph contours overlap — which heavy weights and tight spacing make common —
    // QPainterPath's odd-even default punches the overlap out into a transparent hole.
    // "W" has no counter, so in "WWWW" *any* enclosed transparent region is such a hole.
    const QRectF rect(0, 0, 900, 200);
    drift::Clip clip = makeTextClip(QStringLiteral("WWWW"), rect);
    clip.textStyle.fontFamily = QStringLiteral("Montserrat");
    clip.textStyle.fontWeight = 900;
    clip.textStyle.pixelSize = 80;
    clip.textStyle.letterSpacing = -30.0; // force the glyphs to overlap each other

    const QImage image = rasterizeText(clip, rect, 1.0).image;
    QVERIFY(!image.isNull());

    // Flood the transparent background inward from the border; whatever transparent pixels it
    // cannot reach are enclosed by ink, i.e. holes.
    const int w = image.width();
    const int h = image.height();
    const auto transparent = [&](int x, int y) { return qAlpha(image.pixel(x, y)) < 128; };

    QVector<bool> reached(w * h, false);
    QVector<QPoint> stack;
    for (int x = 0; x < w; ++x) {
        stack.append({x, 0});
        stack.append({x, h - 1});
    }
    for (int y = 0; y < h; ++y) {
        stack.append({0, y});
        stack.append({w - 1, y});
    }
    while (!stack.isEmpty()) {
        const QPoint p = stack.takeLast();
        if (p.x() < 0 || p.y() < 0 || p.x() >= w || p.y() >= h)
            continue;
        const int i = p.y() * w + p.x();
        if (reached[i] || !transparent(p.x(), p.y()))
            continue;
        reached[i] = true;
        stack.append({p.x() + 1, p.y()});
        stack.append({p.x() - 1, p.y()});
        stack.append({p.x(), p.y() + 1});
        stack.append({p.x(), p.y() - 1});
    }

    int enclosed = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (transparent(x, y) && !reached[y * w + x])
                ++enclosed;
        }
    }

    // Winding fill leaves exactly zero here; odd-even leaves hundreds.
    QVERIFY2(enclosed < 20,
             qPrintable(QStringLiteral("%1 enclosed transparent px — overlapping glyph contours "
                                       "are being punched out (fill rule regression)")
                            .arg(enclosed)));
}

void EngineTest::textClipCarriesGpuEffects()
{
    SKIP_WITHOUT_FONTS();

    drift::Project project;
    project.setResolution(128, 128);
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Text});

    drift::Clip clip = makeTextClip(QStringLiteral("FX"), QRectF(0, 0, 128, 128));
    clip.textStyle.fontFamily = QStringLiteral("Anton");
    clip.textStyle.pixelSize = 48;
    clip.textStyle.color = QColor(120, 120, 120);
    project.tracks()[0].clips.append(clip);

    FrameCompositor compositor;
    compositor.setProject(&project);
    const QImage plain = compositor.compositeAt(drift::secondsToUs(1.0));
    QVERIFY(!plain.isNull());

    drift::Effect brightness;
    brightness.catalogId = QStringLiteral("adjust.brightness");
    brightness.parameters.insert(QStringLiteral("brightness"), 0.9);
    project.tracks()[0].clips[0].effects.append(brightness);

    const QImage brightened = compositor.compositeAt(drift::secondsToUs(1.0));
    QVERIFY(!brightened.isNull());
    // Effects used to be dropped for text clips: the layer only got them in the video branch.
    QVERIFY2(meanLuminance(brightened) > meanLuminance(plain) + 1.0,
             "GPU effect had no visible effect on a text clip");
}

void EngineTest::textAnimationFadesAndSlides()
{
    SKIP_WITHOUT_FONTS();

    drift::Project project;
    project.setResolution(128, 128);
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Text});

    drift::Clip clip = makeTextClip(QStringLiteral("IN"), QRectF(0, 0, 128, 128));
    clip.textStyle.fontFamily = QStringLiteral("Anton");
    clip.textStyle.pixelSize = 48;
    clip.textStyle.animIn = {drift::TextAnimKind::Fade, drift::secondsToUs(1.0), drift::TextEase::Linear};
    project.tracks()[0].clips.append(clip);

    FrameCompositor compositor;
    compositor.setProject(&project);

    const double early = meanLuminance(compositor.compositeAt(drift::secondsToUs(0.05)));
    const double mid = meanLuminance(compositor.compositeAt(drift::secondsToUs(0.5)));
    const double settled = meanLuminance(compositor.compositeAt(drift::secondsToUs(2.0)));
    QVERIFY2(early < mid && mid < settled, "fade-in did not ramp up");

    // Once past the entrance the text holds steady rather than continuing to change.
    const double later = meanLuminance(compositor.compositeAt(drift::secondsToUs(3.0)));
    QVERIFY(qAbs(later - settled) < 0.5);

    // A slide-up entrance arrives from below, so the glyphs start lower than they finish.
    project.tracks()[0].clips[0].textStyle.animIn = {drift::TextAnimKind::SlideUp,
                                                     drift::secondsToUs(1.0), drift::TextEase::Linear};
    const double startY = litCentroidY(compositor.compositeAt(drift::secondsToUs(0.1)));
    const double endY = litCentroidY(compositor.compositeAt(drift::secondsToUs(2.0)));
    QVERIFY2(startY > endY + 1.0, "slide-up entrance did not travel upward");
}

void EngineTest::clipBodyAnimationFadeRampsOpacity()
{
    drift::Project project;
    project.setResolution(128, 128);
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Shape});

    drift::Clip clip;
    clip.id = QStringLiteral("body");
    clip.type = drift::ClipType::Shape;
    clip.timelineStart = 0;
    clip.timelineDuration = drift::secondsToUs(2.0);
    clip.shapeStyle.kind = drift::ShapeKind::Rectangle;
    clip.shapeStyle.fill = Qt::white;
    clip.animIn = {drift::ClipAnimKind::Fade, drift::secondsToUs(1.0), drift::ClipAnimEase::Linear,
                   drift::FadeCurve::Linear};
    project.tracks()[0].clips.append(clip);

    FrameCompositor compositor;
    compositor.setProject(&project);

    const double early = meanLuminance(compositor.compositeAt(drift::secondsToUs(0.05)));
    const double mid = meanLuminance(compositor.compositeAt(drift::secondsToUs(0.5)));
    const double settled = meanLuminance(compositor.compositeAt(drift::secondsToUs(1.5)));
    QVERIFY2(early < mid && mid < settled, "clip body fade-in did not ramp up");
}

void EngineTest::maskApplierEllipseMasksCorners()
{
    QImage image(64, 64, QImage::Format_RGBA8888);
    image.fill(Qt::white);

    drift::Mask mask;
    mask.shape = drift::MaskShape::Ellipse;
    mask.x = 0.5;
    mask.y = 0.5;
    mask.w = 0.5;
    mask.h = 0.5;

    const QImage masked = drift::applyMask(image, mask, 64, 64);
    QVERIFY(qAlpha(masked.pixel(32, 32)) > 200);
    QVERIFY(qAlpha(masked.pixel(0, 0)) < 20);
}

// Feather is a separable box blur; the guarantee is a monotonic ramp across the edge with the
// interior and exterior left saturated, which is what the (much slower) 2D window produced.
void EngineTest::maskFeatherProducesSoftEdge()
{
    drift::Mask mask;
    mask.shape = drift::MaskShape::Rectangle;
    mask.x = 0.5;
    mask.y = 0.5;
    mask.w = 0.5;
    mask.h = 0.5;
    mask.feather = 8.0;

    const QImage alpha = drift::maskAlphaMap(mask, 128, 128);
    QCOMPARE(alpha.format(), QImage::Format_Grayscale8);
    QCOMPARE(alpha.size(), QSize(128, 128));

    // The rectangle spans x in [32, 96); sample the ramp across its left edge at mid-height.
    const auto at = [&alpha](int x) { return int(alpha.constScanLine(64)[x]); };
    QVERIFY2(at(64) > 250, "mask interior was not opaque");
    QVERIFY2(at(4) < 5, "mask exterior was not clear");
    QVERIFY2(at(32) > 100 && at(32) < 155, "edge did not land near half coverage");
    for (int x = 24; x < 40; ++x)
        QVERIFY2(at(x) <= at(x + 1), "feather ramp was not monotonic");
}

// Rotation used to be skipped for freeform, silently making the slider a no-op.
void EngineTest::maskRotationAppliesToFreeform()
{
    drift::Mask mask;
    mask.shape = drift::MaskShape::Freeform;
    // A thin horizontal bar through the middle; rotating it 90° should stand it upright.
    mask.points = {drift::MaskPoint{QPointF(0.1, 0.45)}, drift::MaskPoint{QPointF(0.9, 0.45)},
                   drift::MaskPoint{QPointF(0.9, 0.55)}, drift::MaskPoint{QPointF(0.1, 0.55)}};

    const QImage flat = drift::maskAlphaMap(mask, 64, 64);
    mask.rotation = 90.0;
    const QImage upright = drift::maskAlphaMap(mask, 64, 64);

    QVERIFY2(flat.constScanLine(32)[8] > 200, "flat bar did not cover the left edge");
    QVERIFY2(upright.constScanLine(32)[8] < 50, "rotation did not move the freeform shape");
    QVERIFY2(upright.constScanLine(8)[32] > 200, "rotated bar did not cover the top edge");
}

// Tangents are carried on every vertex so curved masks are additive later. The rasterizer already
// honours them, which is what keeps that a UI change rather than a second data migration.
void EngineTest::maskFreeformHonoursTangents()
{
    drift::Mask straight;
    straight.shape = drift::MaskShape::Freeform;
    // A triangle with its apex at the top; the bottom edge runs flat across y = 0.8.
    straight.points = {drift::MaskPoint{QPointF(0.5, 0.2)}, drift::MaskPoint{QPointF(0.9, 0.8)},
                       drift::MaskPoint{QPointF(0.1, 0.8)}};

    drift::Mask curved = straight;
    // Bow the bottom edge (vertex 1 -> vertex 2) downward, well past the straight line.
    curved.points[1].outTan = QPointF(0.0, 0.18);
    curved.points[2].inTan = QPointF(0.0, 0.18);

    const QImage flat = drift::maskAlphaMap(straight, 100, 100);
    const QImage bowed = drift::maskAlphaMap(curved, 100, 100);
    QVERIFY(!flat.isNull());
    QVERIFY(!bowed.isNull());

    // Just below the straight edge: outside the flat triangle, inside the bowed one.
    QVERIFY2(flat.constScanLine(88)[50] < 40, "straight edge leaked below y=0.8");
    QVERIFY2(bowed.constScanLine(88)[50] > 200, "tangents did not bow the edge outward");
    // Well inside both.
    QVERIFY2(flat.constScanLine(70)[50] > 200, "triangle interior was not covered");
    QVERIFY2(bowed.constScanLine(70)[50] > 200, "curved interior was not covered");
}

namespace {

// A full-canvas white image clip, so whatever survives to the canvas is the mask coverage.
drift::Project maskStackProject(const QString &imagePath, const QList<drift::Mask> &masks)
{
    drift::Project project;
    project.setResolution(64, 64);
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Shape});

    drift::Clip clip;
    clip.id = QStringLiteral("masked");
    clip.type = drift::ClipType::Image;
    clip.path = imagePath;
    clip.timelineStart = 0;
    clip.timelineDuration = drift::secondsToUs(1.0);
    project.tracks()[0].clips.append(clip);

    // Masks live on the track's lane. Each fixture mask spans the clip unless it already carries
    // its own timing, so the tests below are unaffected by the move.
    project.tracks()[0].masks = masks;
    for (drift::Mask &mask : project.tracks()[0].masks) {
        if (mask.timelineDuration <= 0) {
            mask.timelineStart = clip.timelineStart;
            mask.timelineDuration = clip.timelineDuration;
        }
    }
    return project;
}

} // namespace

// The GPU fold and drift::maskAlphaMap implement the same op semantics in two places. They have
// to agree, or the preview and the CPU fallback would disagree about what a stack means.
void EngineTest::compositorFoldsMaskStackLikeTheCpuPath()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString whitePath = dir.filePath(QStringLiteral("white.png"));
    {
        QImage white(64, 64, QImage::Format_RGBA8888);
        white.fill(Qt::white);
        QVERIFY(white.save(whitePath, "PNG"));
    }

    drift::Mask base;
    base.shape = drift::MaskShape::Rectangle;
    base.w = 0.8;
    base.h = 0.8;

    drift::Mask hole;
    hole.shape = drift::MaskShape::Ellipse;
    hole.op = drift::MaskOp::Subtract;
    hole.w = 0.3;
    hole.h = 0.3;

    drift::Mask trim;
    trim.shape = drift::MaskShape::Rectangle;
    trim.op = drift::MaskOp::Intersect;
    trim.x = 0.4;
    trim.w = 0.6;
    trim.h = 1.0;

    const QList<drift::Mask> masks{base, hole, trim};

    const drift::Project project = maskStackProject(whitePath, masks);
    FrameCompositor compositor;
    compositor.setProject(&project);
    const QImage composed = compositor.compositeAt(0);
    QVERIFY(!composed.isNull());

    const QImage expected = drift::maskAlphaMap(masks, 64, 64);
    QVERIFY(!expected.isNull());

    // The clip covers the canvas, so canvas pixel == layer pixel and the two maps line up.
    // Antialiasing and the GPU's filtering differ slightly at edges, hence the tolerance.
    int checked = 0;
    for (int y = 4; y < 60; y += 4) {
        for (int x = 4; x < 60; x += 4) {
            const int want = expected.constScanLine(y)[x];
            const int got = qRed(composed.pixel(x, y));
            QVERIFY2(qAbs(want - got) <= 24,
                     qPrintable(QStringLiteral("mask mismatch at %1,%2: cpu %3 gpu %4")
                                        .arg(x).arg(y).arg(want).arg(got)));
            ++checked;
        }
    }
    QVERIFY(checked > 100);

    // Sanity that the three ops each did something distinguishable. On a 64px canvas: base
    // spans x 6.4..57.6, the subtracted ellipse spans 22.4..41.6, and the intersect keeps
    // 6.4..44.8.
    QVERIFY2(qRed(composed.pixel(12, 32)) > 200, "base rectangle was lost");
    QVERIFY2(qRed(composed.pixel(32, 32)) < 40, "subtract did not punch the centre");
    QVERIFY2(qRed(composed.pixel(52, 32)) < 40, "intersect did not trim past x=44.8");
}

// The compositor bakes animated mask properties at the frame's clip time, the way it already does
// for effect params. Both preview and export come through here, so a mask cannot animate in one
// and sit still in the other.
void EngineTest::compositorAnimatesMaskAcrossTheClip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString whitePath = dir.filePath(QStringLiteral("white.png"));
    {
        QImage white(64, 64, QImage::Format_RGBA8888);
        white.fill(Qt::white);
        QVERIFY(white.save(whitePath, "PNG"));
    }

    // A narrow rectangle that slides from the left edge to the right across one second.
    drift::Mask sliding;
    sliding.shape = drift::MaskShape::Rectangle;
    sliding.w = 0.25;
    sliding.h = 1.0;
    sliding.keyframes[QStringLiteral("x")].setKeyframe(0, 0.15);
    sliding.keyframes[QStringLiteral("x")].setKeyframe(drift::secondsToUs(1.0), 0.85);

    const drift::Project project = maskStackProject(whitePath, {sliding});
    FrameCompositor compositor;
    compositor.setProject(&project);

    const QImage atStart = compositor.compositeAt(0);
    QVERIFY2(qRed(atStart.pixel(10, 32)) > 200, "mask was not at the left at t=0");
    QVERIFY2(qRed(atStart.pixel(54, 32)) < 40, "mask covered the right at t=0");

    // Just inside the clip: containsTime is end-exclusive, so t=1.0 renders nothing at all.
    const QImage atEnd = compositor.compositeAt(drift::secondsToUs(0.99));
    QVERIFY2(qRed(atEnd.pixel(54, 32)) > 200, "mask did not reach the right by the clip's end");
    QVERIFY2(qRed(atEnd.pixel(10, 32)) < 40, "mask still covered the left at the clip's end");

    // And in between, somewhere in the middle.
    const QImage atMid = compositor.compositeAt(drift::secondsToUs(0.5));
    QVERIFY2(qRed(atMid.pixel(32, 32)) > 200, "mask was not mid-frame at t=0.5");
}

// An image mask's pixels are its coverage, placed by its own rect. This is the drop-a-PNG path,
// and it decodes through the still-image cache rather than the video reader.
void EngineTest::maskImageMediaCoversByItsOwnRect()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString whitePath = dir.filePath(QStringLiteral("white.png"));
    {
        QImage white(64, 64, QImage::Format_RGBA8888);
        white.fill(Qt::white);
        QVERIFY(white.save(whitePath, "PNG"));
    }
    // The mask media: white on the left half, black on the right.
    const QString maskPath = dir.filePath(QStringLiteral("halves.png"));
    {
        QImage halves(64, 64, QImage::Format_RGBA8888);
        halves.fill(Qt::black);
        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x < 32; ++x)
                halves.setPixelColor(x, y, Qt::white);
        }
        QVERIFY(halves.save(maskPath, "PNG"));
    }

    // Full frame: the media's own halves decide coverage.
    {
        const drift::Project project =
            maskStackProject(whitePath, {drift::fullFrameMediaMask(maskPath)});
        FrameCompositor compositor;
        compositor.setProject(&project);
        const QImage out = compositor.compositeAt(0);
        QVERIFY2(qRed(out.pixel(16, 32)) > 200, "left half of the media did not show through");
        QVERIFY2(qRed(out.pixel(48, 32)) < 40, "right half of the media was not masked out");
    }

    // Half width, centred: the media is squeezed into the middle, so its own boundary lands at
    // the centre of that box and everything outside the box is uncovered.
    {
        drift::Mask placed = drift::fullFrameMediaMask(maskPath);
        placed.w = 0.5;
        placed.h = 1.0;
        const drift::Project project = maskStackProject(whitePath, {placed});
        FrameCompositor compositor;
        compositor.setProject(&project);
        const QImage out = compositor.compositeAt(0);
        QVERIFY2(qRed(out.pixel(4, 32)) < 40, "coverage leaked left of the media's rect");
        QVERIFY2(qRed(out.pixel(60, 32)) < 40, "coverage leaked right of the media's rect");
        QVERIFY2(qRed(out.pixel(20, 32)) > 200, "media's white half did not land inside the rect");
        QVERIFY2(qRed(out.pixel(40, 32)) < 40, "media's black half did not land inside the rect");
    }
}

// Mask view is a preview affordance. It rides on RenderOptions precisely so the exporter, which
// builds its own, cannot render one by accident.
void EngineTest::maskViewRendersCoverageAndNeverExports()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString redPath = dir.filePath(QStringLiteral("red.png"));
    {
        QImage red(64, 64, QImage::Format_RGBA8888);
        red.fill(Qt::red);
        QVERIFY(red.save(redPath, "PNG"));
    }

    drift::Mask ellipse;
    ellipse.shape = drift::MaskShape::Ellipse;
    ellipse.w = 0.5;
    ellipse.h = 0.5;

    const drift::Project project = maskStackProject(redPath, {ellipse});
    FrameCompositor compositor;
    compositor.setProject(&project);

    FrameCompositor::RenderOptions options;
    options.maskViewClipId = QStringLiteral("masked");
    const QImage view = compositor.compositeAt(0, options);
    QVERIFY(!view.isNull());

    // Coverage is monochrome: white inside the ellipse, black outside, and no trace of the red.
    const QColor inside = view.pixelColor(32, 32);
    QVERIFY2(inside.red() > 200 && inside.green() > 200 && inside.blue() > 200,
             qPrintable(QStringLiteral("inside was %1").arg(inside.name())));
    const QColor outside = view.pixelColor(2, 2);
    QVERIFY2(outside.red() < 40 && outside.green() < 40 && outside.blue() < 40,
             qPrintable(QStringLiteral("outside was %1").arg(outside.name())));

    // The default options are what the exporter uses: the picture comes back.
    const QImage normal = compositor.compositeAt(0);
    const QColor normalInside = normal.pixelColor(32, 32);
    QVERIFY2(normalInside.red() > 200 && normalInside.green() < 60,
             qPrintable(QStringLiteral("export path was not the picture: %1")
                                .arg(normalInside.name())));
}

void EngineTest::exporterProducesPlayableFileWithBackground()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    drift::Project project;
    project.setResolution(160, 90);
    project.setFps(25);
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Shape});

    // A small centered shape so the canvas corners show the background.
    drift::Clip clip;
    clip.id = QStringLiteral("shape");
    clip.type = drift::ClipType::Shape;
    clip.timelineStart = 0;
    clip.timelineDuration = drift::secondsToUs(1.0);
    clip.shapeStyle.kind = drift::ShapeKind::Rectangle;
    clip.shapeStyle.fill = Qt::red;
    clip.shapeStyle.strokeWidth = 0.0;
    clip.transformX.setKeyframe(0, 70.0);
    clip.transformY.setKeyframe(0, 35.0);
    clip.transformW.setKeyframe(0, 20.0);
    clip.transformH.setKeyframe(0, 20.0);
    project.tracks()[0].clips.append(clip);

    // Non-default background must be baked into the exported frames.
    drift::Background background;
    background.kind = drift::BackgroundKind::Color;
    background.color = QColor(Qt::blue);
    project.setBackground(background);

    ExportSettings settings = Exporter::defaultSettings();
    settings.targetHeight = 0;
    settings.videoCodecId = QStringLiteral("h264");
    settings.audioCodecId = QStringLiteral("aac");
    settings.rateControl = QStringLiteral("crf");
    settings.crf = 23;

    // Prefer an available video codec if h264 is missing.
    if (!Exporter::videoCodecById(settings.videoCodecId).value(QStringLiteral("available")).toBool()) {
        bool found = false;
        for (const QVariant &v : Exporter::videoCodecs()) {
            const QVariantMap m = v.toMap();
            if (m.value(QStringLiteral("available")).toBool()) {
                settings.videoCodecId = m.value(QStringLiteral("id")).toString();
                found = true;
                break;
            }
        }
        if (!found)
            QSKIP("No video encoder available in this FFmpeg build");
    }
    if (!Exporter::audioCodecById(settings.audioCodecId).value(QStringLiteral("available")).toBool()) {
        bool found = false;
        for (const QVariant &v : Exporter::audioCodecs()) {
            const QVariantMap m = v.toMap();
            if (m.value(QStringLiteral("available")).toBool()) {
                settings.audioCodecId = m.value(QStringLiteral("id")).toString();
                found = true;
                break;
            }
        }
        if (!found)
            QSKIP("No audio encoder available in this FFmpeg build");
    }

    // The fallback codecs need not be mp4-muxable (an LGPL FFmpeg has no x264 and lands on ffv1),
    // so let the pair pick the container rather than hardcoding one.
    const QString out = dir.filePath(
        QStringLiteral("out.") + Exporter::defaultSuffix(
            Exporter::preferredContainer(settings.videoCodecId, settings.audioCodecId)));

    QString error;
    const bool ok = Exporter::run(project, settings, out, &error);
    if (!ok && error.contains(QStringLiteral("encoder")))
        QSKIP("Selected encoder not available in this FFmpeg build");
    QVERIFY2(ok, qPrintable(error));
    QVERIFY(QFileInfo(out).size() > 0);

    ClipReader reader;
    QVERIFY(reader.open(out));
    QVERIFY(reader.hasVideo());

    QImage frame;
    QVERIFY(reader.readVideoFrameAt(drift::secondsToUs(0.5), frame, 160, 90));
    QVERIFY(!frame.isNull());

    // A corner is background (blue), away from the centered red shape.
    const QRgb corner = frame.pixel(6, 6);
    QVERIFY(qBlue(corner) > 150);
    QVERIFY(qBlue(corner) > qRed(corner));
}

void EngineTest::exporterProducesAudioOnlyMp3()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    drift::Project project;
    project.setResolution(160, 90);
    project.setFps(25);
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Shape});

    drift::Clip clip;
    clip.id = QStringLiteral("shape");
    clip.type = drift::ClipType::Shape;
    clip.timelineStart = 0;
    clip.timelineDuration = drift::secondsToUs(1.0);
    clip.shapeStyle.kind = drift::ShapeKind::Rectangle;
    clip.shapeStyle.fill = Qt::red;
    project.tracks()[0].clips.append(clip);

    ExportSettings settings = Exporter::defaultSettings();
    settings.audioOnly = true;
    settings.audioCodecId = QStringLiteral("mp3");
    settings.audioBitrateKbps = 192;

    if (!Exporter::audioCodecById(settings.audioCodecId).value(QStringLiteral("available")).toBool())
        QSKIP("MP3 encoder not available in this FFmpeg build");

    const QString out = dir.filePath(QStringLiteral("out.mp3"));
    QString error;
    const bool ok = Exporter::run(project, settings, out, &error);
    if (!ok && error.contains(QStringLiteral("encoder")))
        QSKIP("MP3 encoder not available in this FFmpeg build");
    QVERIFY2(ok, qPrintable(error));
    QVERIFY(QFileInfo(out).size() > 0);

    ClipReader reader;
    QVERIFY(reader.open(out));
    QVERIFY(reader.hasAudio());
    QVERIFY(!reader.hasVideo());
}

void EngineTest::exporterTagsSdrBt709ColorMetadata()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    drift::Project project;
    project.setResolution(160, 90);
    project.setFps(25);
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Shape});

    drift::Clip clip;
    clip.id = QStringLiteral("shape");
    clip.type = drift::ClipType::Shape;
    clip.timelineStart = 0;
    clip.timelineDuration = drift::secondsToUs(0.5);
    clip.shapeStyle.kind = drift::ShapeKind::Rectangle;
    clip.shapeStyle.fill = Qt::green;
    project.tracks()[0].clips.append(clip);

    ExportSettings settings = Exporter::defaultSettings();
    settings.targetHeight = 0;
    settings.videoCodecId = QStringLiteral("h264");
    settings.audioCodecId = QStringLiteral("aac");
    settings.rateControl = QStringLiteral("crf");
    settings.crf = 18;

    if (!Exporter::videoCodecById(settings.videoCodecId).value(QStringLiteral("available")).toBool())
        QSKIP("H.264 encoder not available in this FFmpeg build");
    if (!Exporter::audioCodecById(settings.audioCodecId).value(QStringLiteral("available")).toBool())
        QSKIP("AAC encoder not available in this FFmpeg build");

    const QString out = dir.filePath(QStringLiteral("color.mp4"));
    QString error;
    const bool ok = Exporter::run(project, settings, out, &error);
    if (!ok && error.contains(QStringLiteral("encoder")))
        QSKIP("Selected encoder not available in this FFmpeg build");
    QVERIFY2(ok, qPrintable(error));

    AVFormatContext *fmt = nullptr;
    QVERIFY(avformat_open_input(&fmt, out.toUtf8().constData(), nullptr, nullptr) == 0);
    QVERIFY(avformat_find_stream_info(fmt, nullptr) >= 0);

    const AVStream *vstream = nullptr;
    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            vstream = fmt->streams[i];
            break;
        }
    }
    QVERIFY(vstream);
    QCOMPARE(vstream->codecpar->color_range, AVCOL_RANGE_MPEG);
    QCOMPARE(vstream->codecpar->color_primaries, AVCOL_PRI_BT709);
    QCOMPARE(vstream->codecpar->color_trc, AVCOL_TRC_BT709);
    QCOMPARE(vstream->codecpar->color_space, AVCOL_SPC_BT709);

    avformat_close_input(&fmt);
}

void EngineTest::exporterDefaultCrfIsNearLosslessForH264()
{
    const QVariantMap h264 = Exporter::videoCodecById(QStringLiteral("h264"));
    if (!h264.value(QStringLiteral("available")).toBool())
        QSKIP("H.264 encoder not available in this FFmpeg build");
    QCOMPARE(h264.value(QStringLiteral("defaultCrf")).toInt(), 18);

    const ExportSettings defaults = Exporter::defaultSettings();
    if (defaults.videoCodecId == QLatin1String("h264"))
        QCOMPARE(defaults.crf, 18);
}

namespace {

// One-second red-on-blue canvas; enough for the muxer to report a stable rate.
drift::Project frameRateTestProject(int projectFps)
{
    drift::Project project;
    project.setResolution(160, 90);
    project.setFps(projectFps);
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Shape});

    drift::Clip clip;
    clip.id = QStringLiteral("shape");
    clip.type = drift::ClipType::Shape;
    clip.timelineStart = 0;
    clip.timelineDuration = drift::secondsToUs(1.0);
    clip.shapeStyle.kind = drift::ShapeKind::Rectangle;
    clip.shapeStyle.fill = Qt::red;
    clip.shapeStyle.strokeWidth = 0.0;
    clip.transformX.setKeyframe(0, 70.0);
    clip.transformY.setKeyframe(0, 35.0);
    clip.transformW.setKeyframe(0, 20.0);
    clip.transformH.setKeyframe(0, 20.0);
    project.tracks()[0].clips.append(clip);
    return project;
}

// Swaps in whatever encoders this FFmpeg build actually has; false means none.
bool useAvailableCodecs(ExportSettings &settings)
{
    const auto pick = [](const QVariantList &catalog, QString &id) {
        for (const QVariant &v : catalog) {
            const QVariantMap m = v.toMap();
            if (m.value(QStringLiteral("available")).toBool()) {
                id = m.value(QStringLiteral("id")).toString();
                return true;
            }
        }
        return false;
    };
    if (!Exporter::videoCodecById(settings.videoCodecId).value(QStringLiteral("available")).toBool()
        && !pick(Exporter::videoCodecs(), settings.videoCodecId)) {
        return false;
    }
    if (!Exporter::audioCodecById(settings.audioCodecId).value(QStringLiteral("available")).toBool()
        && !pick(Exporter::audioCodecs(), settings.audioCodecId)) {
        return false;
    }
    return true;
}

// Frame rate the demuxer reports, plus a demuxed packet count (nb_frames is 0 on
// some muxers, so count rather than trust it).
bool probeVideoRate(const QString &path, AVRational &rate, int64_t &frameCount)
{
    rate = AVRational{0, 1};
    frameCount = 0;

    AVFormatContext *fmt = nullptr;
    if (avformat_open_input(&fmt, path.toUtf8().constData(), nullptr, nullptr) < 0)
        return false;
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        avformat_close_input(&fmt);
        return false;
    }
    const int idx = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (idx < 0) {
        avformat_close_input(&fmt);
        return false;
    }
    const AVStream *st = fmt->streams[idx];
    // r_frame_rate, not avg_frame_rate: the latter divides the frame count by the
    // span to the *last frame's start*, so a 1s/25fps file reads back as 26.04.
    rate = st->r_frame_rate.num > 0 ? st->r_frame_rate : st->avg_frame_rate;

    AVPacket *pkt = av_packet_alloc();
    while (pkt && av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == idx)
            ++frameCount;
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    avformat_close_input(&fmt);
    return rate.num > 0 && rate.den > 0;
}

// Decodes every frame and reports how many differ from the frame before them.
// A frame that merely repeats its predecessor scores ~0 mean-absolute-difference
// on the luma plane, so this separates real temporal detail from duplication.
bool countDistinctFrames(const QString &path, int &total, int &changed, double threshold = 1.0)
{
    total = 0;
    changed = 0;

    AVFormatContext *fmt = nullptr;
    if (avformat_open_input(&fmt, path.toUtf8().constData(), nullptr, nullptr) < 0)
        return false;
    const auto closeFmt = qScopeGuard([&] { avformat_close_input(&fmt); });
    if (avformat_find_stream_info(fmt, nullptr) < 0)
        return false;

    const int idx = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (idx < 0)
        return false;
    const AVCodec *dec = avcodec_find_decoder(fmt->streams[idx]->codecpar->codec_id);
    if (!dec)
        return false;
    AVCodecContext *ctx = avcodec_alloc_context3(dec);
    if (!ctx)
        return false;
    const auto freeCtx = qScopeGuard([&] { avcodec_free_context(&ctx); });
    if (avcodec_parameters_to_context(ctx, fmt->streams[idx]->codecpar) < 0)
        return false;
    if (avcodec_open2(ctx, dec, nullptr) < 0)
        return false;

    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    if (!pkt || !frame) {
        av_packet_free(&pkt);
        av_frame_free(&frame);
        return false;
    }
    const auto freeAv = qScopeGuard([&] {
        av_packet_free(&pkt);
        av_frame_free(&frame);
    });

    QByteArray previous;
    const auto consume = [&]() {
        while (avcodec_receive_frame(ctx, frame) >= 0) {
            QByteArray luma;
            luma.resize(frame->width * frame->height);
            for (int y = 0; y < frame->height; ++y) {
                std::memcpy(luma.data() + y * frame->width, frame->data[0] + y * frame->linesize[0],
                            frame->width);
            }
            if (!previous.isEmpty() && previous.size() == luma.size()) {
                double sum = 0.0;
                const auto *a = reinterpret_cast<const uint8_t *>(previous.constData());
                const auto *b = reinterpret_cast<const uint8_t *>(luma.constData());
                for (int i = 0; i < luma.size(); ++i)
                    sum += std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i]));
                if (sum / luma.size() > threshold)
                    ++changed;
            }
            previous = luma;
            ++total;
        }
    };

    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == idx) {
            // EAGAIN means the decoder wants its output read before it will take
            // more; dropping the packet there would silently lose a frame.
            int rc = avcodec_send_packet(ctx, pkt);
            while (rc == AVERROR(EAGAIN)) {
                consume();
                rc = avcodec_send_packet(ctx, pkt);
            }
            consume();
        }
        av_packet_unref(pkt);
    }
    avcodec_send_packet(ctx, nullptr);
    consume();
    return total > 0;
}

// Exports `project` at the given rate and reports what landed in the file.
// Returns false only when this FFmpeg build cannot encode at all.
bool exportAtRate(const drift::Project &project, int fpsNum, int fpsDen, const QString &dirPath,
                  const QString &name, AVRational &rate, int64_t &frameCount,
                  QString *outPathOut = nullptr, drift::TimeUs startUs = 0,
                  drift::TimeUs endUs = 0)
{
    ExportSettings settings = Exporter::defaultSettings();
    settings.videoCodecId = QStringLiteral("h264");
    settings.audioCodecId = QStringLiteral("aac");
    settings.rateControl = QStringLiteral("crf");
    settings.crf = 18;
    settings.fpsNum = fpsNum;
    settings.fpsDen = fpsDen;
    if (startUs > 0 || endUs > 0) {
        settings.startUs = startUs;
        settings.endUs = endUs;
    }
    if (!useAvailableCodecs(settings))
        return false;

    const QString out = dirPath + QLatin1Char('/') + name + QLatin1Char('.')
        + Exporter::defaultSuffix(
                             Exporter::preferredContainer(settings.videoCodecId, settings.audioCodecId));

    QString error;
    if (!Exporter::run(project, settings, out, &error)) {
        if (error.contains(QStringLiteral("encoder")))
            return false;
        qWarning("export failed: %s", qPrintable(error));
        return false;
    }
    if (outPathOut)
        *outPathOut = out;
    return probeVideoRate(out, rate, frameCount);
}

// 1 second of 120 fps footage — the high-frame-rate source a slow-motion edit is
// built on. Every frame is a flat grey stepping by 11 levels, so "is this frame
// new or a repeat?" is unambiguous however the frame is scaled or colour-converted
// (testsrc is too nearly-static at 64x64 to tell the two apart).
QString makeHighRateVideo(QTemporaryDir &dir)
{
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty())
        return {};

    const QString out = dir.filePath(QStringLiteral("fast.mp4"));
    const QStringList args{
        QStringLiteral("-y"),
        QStringLiteral("-f"), QStringLiteral("lavfi"),
        QStringLiteral("-i"), QStringLiteral("color=c=black:s=64x64:r=120:d=1"),
        // Escaped comma: the filtergraph parser would read a bare one as a filter break.
        QStringLiteral("-vf"), QStringLiteral("geq=lum='mod(N*11\\,256)':cb=128:cr=128"),
        QStringLiteral("-c:v"), QStringLiteral("libx264"),
        QStringLiteral("-crf"), QStringLiteral("12"),
        QStringLiteral("-g"), QStringLiteral("12"),
        QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
        out,
    };

    QProcess proc;
    proc.start(ffmpeg, args);
    if (!proc.waitForFinished(30000) || proc.exitCode() != 0)
        return {};
    return QFileInfo::exists(out) ? out : QString{};
}

} // namespace

// Pure validation — runs even on an FFmpeg build with no encoders at all.
void EngineTest::exporterSettingsFromMapValidatesFrameRate()
{
    // Unset means "follow the project".
    const ExportSettings none = Exporter::settingsFromMap({});
    QCOMPARE(none.fpsNum, 0);
    QCOMPARE(none.fpsDen, 1);

    // A negative numerator or a zero denominator would produce a time_base the
    // muxer rejects, so both fall back rather than propagating.
    const ExportSettings negative = Exporter::settingsFromMap(
        {{QStringLiteral("fpsNum"), -5}, {QStringLiteral("fpsDen"), 1}});
    QCOMPARE(negative.fpsNum, 0);
    QCOMPARE(negative.fpsDen, 1);

    const ExportSettings zeroDen = Exporter::settingsFromMap(
        {{QStringLiteral("fpsNum"), 30}, {QStringLiteral("fpsDen"), 0}});
    QCOMPARE(zeroDen.fpsNum, 0);
    QCOMPARE(zeroDen.fpsDen, 1);

    const ExportSettings tooFast = Exporter::settingsFromMap(
        {{QStringLiteral("fpsNum"), 9999}, {QStringLiteral("fpsDen"), 1}});
    QCOMPARE(tooFast.fpsNum, kMaxExportFps);
    QCOMPARE(tooFast.fpsDen, 1);

    // NTSC rates must survive untouched — this is the whole point of keeping it rational.
    const ExportSettings ntsc = Exporter::settingsFromMap(
        {{QStringLiteral("fpsNum"), 30000}, {QStringLiteral("fpsDen"), 1001}});
    QCOMPARE(ntsc.fpsNum, 30000);
    QCOMPARE(ntsc.fpsDen, 1001);
}

void EngineTest::exporterDefaultsToProjectFrameRate()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    AVRational rate{};
    int64_t frames = 0;
    if (!exportAtRate(frameRateTestProject(25), 0, 1, dir.path(), QStringLiteral("project"), rate, frames))
        QSKIP("No usable encoder in this FFmpeg build");

    QVERIFY2(std::abs(av_q2d(rate) - 25.0) < 0.5, qPrintable(QStringLiteral("got %1").arg(av_q2d(rate))));
    QVERIFY2(std::llabs(frames - 25) <= 1, qPrintable(QStringLiteral("got %1 frames").arg(frames)));
}

void EngineTest::exporterHonoursExportFrameRateOverride()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // 25 fps project, 50 fps delivery: the export rate must win, and the file must
    // hold twice the frames over the same one-second timeline.
    AVRational rate{};
    int64_t frames = 0;
    if (!exportAtRate(frameRateTestProject(25), 50, 1, dir.path(), QStringLiteral("fast"), rate, frames))
        QSKIP("No usable encoder in this FFmpeg build");

    QVERIFY2(std::abs(av_q2d(rate) - 50.0) < 0.5, qPrintable(QStringLiteral("got %1").arg(av_q2d(rate))));
    QVERIFY2(std::llabs(frames - 50) <= 1, qPrintable(QStringLiteral("got %1 frames").arg(frames)));
}

void EngineTest::exporterHonoursWorkAreaRange()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    drift::Project project = frameRateTestProject(25);
    project.tracks()[0].clips[0].timelineDuration = drift::secondsToUs(2.0);

    AVRational rate{};
    int64_t frames = 0;
    if (!exportAtRate(project, 0, 1, dir.path(), QStringLiteral("slice"), rate, frames, nullptr,
                      drift::secondsToUs(0.5), drift::secondsToUs(1.5))) {
        QSKIP("No usable encoder in this FFmpeg build");
    }

    QVERIFY2(std::llabs(frames - 25) <= 1, qPrintable(QStringLiteral("got %1 frames").arg(frames)));
}

void EngineTest::exporterProducesAnimatedGif()
{
    if (!Exporter::gifAvailable())
        QSKIP("GIF encoder is not available in this FFmpeg build");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    drift::Project project = frameRateTestProject(25);
    project.tracks()[0].clips[0].timelineDuration = drift::secondsToUs(0.4);

    ExportSettings settings = Exporter::defaultSettings();
    settings.gifExport = true;
    settings.fpsNum = 10;
    settings.fpsDen = 1;

    const QString out = dir.filePath(QStringLiteral("loop.gif"));
    QString error;
    const bool ok = Exporter::run(project, settings, out, &error);
    QVERIFY2(ok, qPrintable(error));
    QVERIFY(QFileInfo(out).size() > 100);
}

void EngineTest::exporterSupportsNtscFrameRates()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    AVRational rate{};
    int64_t frames = 0;
    if (!exportAtRate(frameRateTestProject(30), 30000, 1001, dir.path(), QStringLiteral("ntsc"), rate,
                      frames)) {
        QSKIP("No usable encoder in this FFmpeg build");
    }

    // Tolerance is deliberately tighter than the 0.03 gap between 29.97 and 30:
    // an integer-fps exporter would land on 30.0 and fail here.
    const double expected = 30000.0 / 1001.0;
    QVERIFY2(std::abs(av_q2d(rate) - expected) < 0.01,
             qPrintable(QStringLiteral("got %1, expected %2").arg(av_q2d(rate)).arg(expected)));
    QVERIFY2(std::llabs(frames - 30) <= 1, qPrintable(QStringLiteral("got %1 frames").arg(frames)));
}

// The point of the feature: a higher export rate must yield genuinely new frames
// for a slowed clip, not duplicates — but only while the source still has them.
// 120 fps footage at 0.5x advances source time at 60 source-fps, so 60 fps of
// export is exactly the ceiling and 240 fps is past it.
void EngineTest::exporterFrameRateAddsRealDetailToSlowedClips()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString source = makeHighRateVideo(dir);
    if (source.isEmpty())
        QSKIP("ffmpeg not available to generate a test clip");

    // Fixture sanity: the whole test is meaningless unless the source really does
    // change every frame.
    int sourceTotal = 0;
    int sourceChanged = 0;
    QVERIFY(countDistinctFrames(source, sourceTotal, sourceChanged));
    QVERIFY2(sourceChanged > 100,
             qPrintable(QStringLiteral("source only had %1/%2 changing frames")
                            .arg(sourceChanged)
                            .arg(sourceTotal)));

    drift::Project project;
    project.setResolution(64, 64);
    project.setFps(30);
    project.tracks().clear();
    project.tracks().append(drift::Track{.type = drift::TrackType::Video});

    drift::Clip clip;
    clip.id = QStringLiteral("slowmo");
    clip.type = drift::ClipType::Video;
    clip.path = source;
    clip.timelineStart = 0;
    clip.speed = 0.5;
    clip.srcIn = 0;
    // 1s of source stretched over 2s of timeline.
    clip.timelineDuration = drift::secondsToUs(2.0);
    clip.srcOut = clip.sourceSpanUs();
    project.tracks()[0].clips.append(clip);

    struct Result
    {
        int64_t encoded = 0; // packets written by the exporter
        int total = 0;       // frames the decoder handed back
        int changed = 0;
    };
    const auto exportAndCount = [&](int fps, const QString &name, Result &result) -> bool {
        AVRational rate{};
        QString path;
        if (!exportAtRate(project, fps, 1, dir.path(), name, rate, result.encoded, &path))
            return false;
        return countDistinctFrames(path, result.total, result.changed);
    };

    Result slow;
    Result fast;
    Result beyond;
    if (!exportAndCount(30, QStringLiteral("at30"), slow))
        QSKIP("No usable encoder in this FFmpeg build");
    QVERIFY(exportAndCount(60, QStringLiteral("at60"), fast));
    QVERIFY(exportAndCount(240, QStringLiteral("at240"), beyond));

    QCOMPARE(slow.encoded, 60);
    QCOMPARE(fast.encoded, 120);
    QCOMPARE(beyond.encoded, 480);

    // Decoded count can trail the packet count by one when the mp4 edit list makes
    // the demuxer drop the first frame; the packet counts above are the exact check.
    QVERIFY2(std::abs(slow.total - 60) <= 1, qPrintable(QStringLiteral("decoded %1").arg(slow.total)));
    QVERIFY2(std::abs(fast.total - 120) <= 1, qPrintable(QStringLiteral("decoded %1").arg(fast.total)));
    QVERIFY2(std::abs(beyond.total - 480) <= 1,
             qPrintable(QStringLiteral("decoded %1").arg(beyond.total)));

    // Under the ceiling, essentially every frame is new: the extra frames are real
    // temporal detail pulled from the source, which is what makes slow-mo smooth.
    QVERIFY2(slow.changed >= 55, qPrintable(QStringLiteral("30fps: %1/60 new").arg(slow.changed)));
    QVERIFY2(fast.changed >= 110, qPrintable(QStringLiteral("60fps: %1/120 new").arg(fast.changed)));
    QVERIFY2(fast.changed > slow.changed * 1.5,
             qPrintable(QStringLiteral("60fps gave %1 new frames vs %2 at 30fps")
                            .arg(fast.changed)
                            .arg(slow.changed)));

    // Past the ceiling the source has nothing left to give, so frames repeat —
    // asking for 4x the rate does not buy 4x the detail.
    QVERIFY2(beyond.changed < beyond.total / 2,
             qPrintable(QStringLiteral("240fps: %1/480 new").arg(beyond.changed)));
    QVERIFY2(beyond.changed < fast.changed * 1.5,
             qPrintable(QStringLiteral("240fps gave %1 new frames vs %2 at 60fps")
                            .arg(beyond.changed)
                            .arg(fast.changed)));
}

// The audio-effects addon content must parse into a usable catalog: known ids resolve, categories
// are discovered, and every manifest carries a chain. A broken manifest would be skipped silently,
// so assert the expected count rather than merely "non-empty".
namespace {

// Build a rack for `effects` and run the whole buffer through it in one pass. The mixer does this
// block by block; a single pass is the reference those blocks must agree with.
QVector<float> runRack(const QList<drift::Effect> &effects, const float *interleavedStereo,
                       int frames, int sampleRate)
{
    QVector<float> out(frames * 2, 0.0f);
    if (interleavedStereo)
        std::memcpy(out.data(), interleavedStereo, static_cast<size_t>(frames) * 2 * sizeof(float));

    drift::AudioEffectRack rack;
    if (rack.configure(audioEffectSpecsFor(effects), sampleRate))
        rack.process(out.data(), frames);
    return out;
}

QVector<float> stereoTone(int frames, double hz, int sampleRate, float amplitude = 1.0f)
{
    QVector<float> tone(frames * 2);
    for (int i = 0; i < frames; ++i) {
        const auto s = static_cast<float>(amplitude * std::sin(2.0 * M_PI * hz * i / sampleRate));
        tone[i * 2] = s;
        tone[i * 2 + 1] = s;
    }
    return tone;
}

double rms(const float *samples, int count)
{
    double sum = 0.0;
    for (int i = 0; i < count; ++i)
        sum += static_cast<double>(samples[i]) * samples[i];
    return std::sqrt(sum / std::max(1, count));
}
// Naive DFT at one frequency. Enough to ask "is the energy where it should be".
double toneEnergy(const float *interleaved, int frames, double hz, int rate)
{
    const double w = 2.0 * M_PI * hz / rate;
    double re = 0.0;
    double im = 0.0;
    for (int i = 0; i < frames; ++i) {
        re += interleaved[i * 2] * std::cos(w * i);
        im += interleaved[i * 2] * std::sin(w * i);
    }
    return std::sqrt(re * re + im * im) / frames;
}

} // namespace

// A single sample dropped once per mix block is a periodic impulse: a buzz at rate/block with
// harmonics all the way to Nyquist, which is what it looks like on a spectrogram. It came from
// deriving the source frame count through microseconds — 1024 frames at 48 kHz is 21333.33 us, and
// truncating into µs and back out again asks for 1023 frames to fill 1024, leaving the last one at
// the buffer's initial zero. Only block sizes lasting a whole number of µs escaped it, and audio
// device periods are powers of two.
void EngineTest::mixerHasNoBlockBoundaryDropout()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = makeToneAudio(dir);
    if (path.isEmpty())
        QSKIP("ffmpeg not available to generate a test clip");

    constexpr int kRate = 48000;
    constexpr double kToneHz = 440.0;
    constexpr drift::TimeUs kDurationUs = 1'500'000;

    drift::Project project;
    project.setSampleRate(kRate);
    drift::Track track{.type = drift::TrackType::Audio};
    drift::Clip clip;
    clip.id = QStringLiteral("tone");
    clip.type = drift::ClipType::Audio;
    clip.path = path;
    clip.timelineStart = 0;
    clip.timelineDuration = kDurationUs;
    clip.srcIn = 0;
    clip.srcOut = kDurationUs;
    track.clips.append(clip);
    project.tracks().append(track);

    AudioMixer mixer;
    mixer.setProject(&project);

    const int total = static_cast<int>((kDurationUs * kRate) / drift::kUsPerSecond);

    // 480 lasts exactly 10000 us and always worked; the rest do not divide evenly and did not.
    for (const int block : {1024, 512, 1000, 480}) {
        QVector<float> out(total * 2, 0.0f);
        for (int offset = 0; offset < total; offset += block) {
            const int count = std::min(block, total - offset);
            const auto startUs =
                static_cast<drift::TimeUs>((static_cast<int64_t>(offset) * drift::kUsPerSecond) / kRate);
            mixer.mix(startUs, count, kRate, out.data() + static_cast<size_t>(offset) * 2);
        }

        const int skip = kRate / 10; // let the decoder settle
        float amplitude = 0.0f;
        for (int i = skip; i < total; ++i)
            amplitude = std::max(amplitude, std::abs(out[i * 2]));
        QVERIFY2(amplitude > 0.01f, "mixed tone is silent");

        // A band-limited sine cannot step by more than this between adjacent samples. Anything
        // beyond it is a splice, not signal.
        const auto bound = static_cast<float>(amplitude * 2.0 * std::sin(M_PI * kToneHz / kRate));

        float worst = 0.0f;
        int worstIndex = 0;
        for (int i = skip + 1; i < total; ++i) {
            const float delta = std::abs(out[i * 2] - out[(i - 1) * 2]);
            if (delta > worst) {
                worst = delta;
                worstIndex = i;
            }
        }

        QVERIFY2(worst < bound * 1.5f,
                 qPrintable(QStringLiteral("block=%1: step %2 at frame %3 (phase %4) exceeds the %5 "
                                           "a %6 Hz tone can produce")
                                .arg(block).arg(worst).arg(worstIndex)
                                .arg(worstIndex % block).arg(bound).arg(kToneHz)));
    }
}

// PlaybackEngine clears the effect racks from the GUI thread on every seek, play and pause, while
// mix() is running on the audio thread. Taking a reference into the hash instead of a strong
// reference — and touching the hash at all without a lock — segfaults inside the rack's buffers
// once the timing lines up, which is what "crashed after a while" looks like from the outside.
namespace {

constexpr int kToneRate = 48000;
constexpr drift::TimeUs kToneSourceUs = 2'000'000;
constexpr double kTonePi = 3.14159265358979323846;

// One audio clip covering the whole tone, retimed either by a constant speed or by a curve.
drift::Project makeRetimedToneProject(const QString &path, double speed, bool reverse,
                                      const drift::SpeedCurve &curve = {})
{
    drift::Project project;
    project.setSampleRate(kToneRate);
    project.tracks().clear(); // drop the default timeline so the clip is the only thing in the mix
    drift::Track track{.type = drift::TrackType::Audio};
    drift::Clip clip;
    clip.id = QStringLiteral("tone");
    clip.type = drift::ClipType::Audio;
    clip.path = path;
    clip.timelineStart = 0;
    clip.srcIn = 0;
    clip.srcOut = kToneSourceUs;
    clip.speed = speed;
    clip.reverse = reverse;
    clip.speedCurve = curve;
    clip.timelineDuration = static_cast<drift::TimeUs>(kToneSourceUs / speed);
    if (!curve.isEmpty())
        clip.syncDurationFromSpeedCurve();
    track.clips.append(clip);
    project.tracks().append(track);
    return project;
}

double blockRms(const QVector<float> &interleaved, int frames)
{
    double sumSq = 0.0;
    for (int i = 0; i < frames * 2; ++i)
        sumSq += static_cast<double>(interleaved[i]) * interleaved[i];
    return std::sqrt(sumSq / (frames * 2));
}

// Mixes `blocks` contiguous buffers and returns each one's RMS, appending the samples to `collected`
// when it is given. A whole-run RMS would pass a design that emits one good buffer in four, which is
// exactly what a stretcher without a FIFO produces — the per-block figures are the point.
QList<double> mixBlockRms(AudioMixer &mixer, int blocks, int frames, QVector<float> *collected = nullptr)
{
    QVector<float> buffer(frames * 2);
    QList<double> rms;
    for (int b = 0; b < blocks; ++b) {
        const drift::TimeUs t =
            static_cast<drift::TimeUs>(b) * frames * drift::kUsPerSecond / kToneRate;
        mixer.mix(t, frames, kToneRate, buffer.data());
        rms.append(blockRms(buffer, frames));
        if (collected)
            collected->append(buffer);
    }
    return rms;
}

double goertzelMagnitude(const QVector<float> &interleaved, double frequency)
{
    const int frames = interleaved.size() / 2;
    const double w = 2.0 * kTonePi * frequency / kToneRate;
    const double coeff = 2.0 * std::cos(w);
    double s1 = 0.0;
    double s2 = 0.0;
    for (int i = 0; i < frames; ++i) {
        const double s = interleaved[i * 2] + coeff * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    return std::sqrt(qMax(0.0, s1 * s1 + s2 * s2 - coeff * s1 * s2));
}

} // namespace

// Regression gate for the silence a stateless per-block stretcher produced: it rebuilt its filter
// from scratch every buffer, and a WSOLA stretcher fed one short buffer with no history emits
// nothing at all.
void EngineTest::retimedClipAudioIsNotSilent()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = makeToneAudio(dir);
    if (path.isEmpty())
        QSKIP("ffmpeg not available to generate a test clip");

    struct Case
    {
        const char *name;
        double speed;
        drift::SpeedCurve curve;
    };
    const QList<Case> cases{
        {"0.5x", 0.5, {}},
        {"2.0x", 2.0, {}},
        {"flat curve 0.75x", 1.0, drift::SpeedCurve::flat(0.75)},
    };

    for (const Case &c : cases) {
        drift::Project project = makeRetimedToneProject(path, c.speed, false, c.curve);
        AudioMixer mixer;
        mixer.setProject(&project);

        constexpr int kFrames = 1024;
        const drift::TimeUs durationUs = project.tracks().at(0).clips.at(0).timelineDuration;
        const int blocks = static_cast<int>(durationUs * kToneRate / drift::kUsPerSecond / kFrames) - 2;
        QVERIFY(blocks > 20);

        QVector<float> collected;
        const QList<double> rms = mixBlockRms(mixer, blocks, kFrames, &collected);
        QVERIFY2(blockRms(collected, collected.size() / 2) > 0.05, c.name);
        for (int b = 2; b < rms.size(); ++b) {
            QVERIFY2(rms.at(b) > 0.02,
                     qPrintable(QStringLiteral("%1: block %2 rms %3")
                                    .arg(QString::fromUtf8(c.name))
                                    .arg(b)
                                    .arg(rms.at(b))));
        }
    }
}

// A tempo change that took the pitch with it would be a resample, not a stretch.
void EngineTest::retimedAudioPreservesPitch()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = makeToneAudio(dir);
    if (path.isEmpty())
        QSKIP("ffmpeg not available to generate a test clip");

    for (double speed : {0.5, 2.0}) {
        drift::Project project = makeRetimedToneProject(path, speed, false);
        AudioMixer mixer;
        mixer.setProject(&project);

        QVector<float> collected;
        mixBlockRms(mixer, 40, 1024, &collected);

        const double tone = goertzelMagnitude(collected, 440.0);
        QVERIFY2(tone > 10.0 * goertzelMagnitude(collected, 220.0), qPrintable(QString::number(speed)));
        QVERIFY2(tone > 10.0 * goertzelMagnitude(collected, 880.0), qPrintable(QString::number(speed)));
    }
}

// The retimer walks the source itself, so a cursor that ran fast or slow would show up as audio
// that ends early or keeps going past the clip.
void EngineTest::retimedAudioLengthTracksTimeline()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = makeToneAudio(dir);
    if (path.isEmpty())
        QSKIP("ffmpeg not available to generate a test clip");

    drift::Project project = makeRetimedToneProject(path, 0.5, false);
    AudioMixer mixer;
    mixer.setProject(&project);

    constexpr int kFrames = 1024;
    const int blocks = static_cast<int>(5'000'000LL * kToneRate / drift::kUsPerSecond / kFrames);
    QVector<float> collected;
    mixBlockRms(mixer, blocks, kFrames, &collected);

    int lastAudible = -1;
    for (int i = 0; i < collected.size() / 2; ++i) {
        if (std::fabs(collected[i * 2]) > 0.01)
            lastAudible = i;
    }
    QVERIFY(lastAudible > 0);
    const drift::TimeUs endUs =
        static_cast<drift::TimeUs>(lastAudible) * drift::kUsPerSecond / kToneRate;
    QVERIFY2(std::llabs(endUs - 4'000'000) < 150'000, qPrintable(QString::number(endUs)));
}

// Preview asks for whatever the sink wants; export asks for one video frame's worth. Nothing in the
// pipeline may be sized off a single block length.
void EngineTest::retimedAudioSurvivesBlockSizeChanges()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = makeToneAudio(dir);
    if (path.isEmpty())
        QSKIP("ffmpeg not available to generate a test clip");

    drift::Project project = makeRetimedToneProject(path, 0.5, false);
    AudioMixer mixer;
    mixer.setProject(&project);

    const QList<int> sizes{1024, 1600, 256};
    QVector<float> buffer(1600 * 2);
    drift::TimeUs t = 0;
    for (int b = 0; b < 90; ++b) {
        const int frames = sizes.at(b % sizes.size());
        mixer.mix(t, frames, kToneRate, buffer.data());
        if (b >= 2) {
            QVERIFY2(blockRms(buffer, frames) > 0.02,
                     qPrintable(QStringLiteral("block %1 of %2 frames").arg(b).arg(frames)));
        }
        t += static_cast<drift::TimeUs>(frames) * drift::kUsPerSecond / kToneRate;
    }
}

void EngineTest::reversedRetimedAudioIsNotSilent()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = makeToneAudio(dir);
    if (path.isEmpty())
        QSKIP("ffmpeg not available to generate a test clip");

    drift::Project project = makeRetimedToneProject(path, 0.5, true);
    AudioMixer mixer;
    mixer.setProject(&project);

    constexpr int kFrames = 1024;
    const QList<double> rms = mixBlockRms(mixer, 120, kFrames);
    for (int b = 2; b < rms.size(); ++b)
        QVERIFY2(rms.at(b) > 0.02, qPrintable(QStringLiteral("block %1 rms %2").arg(b).arg(rms.at(b))));
}

// A ramp on an audio-track clip, which is the case with no picture to fall back on: the mixer has
// to change tempo every block and still come out with continuous sound over the whole retimed
// length. A flat curve would not exercise the per-block tempo at all.
void EngineTest::rampedSpeedCurveRetimesAudioClip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = makeToneAudio(dir);
    if (path.isEmpty())
        QSKIP("ffmpeg not available to generate a test clip");

    QList<drift::SpeedPoint> points;
    points.append(drift::SpeedPoint{.pos = 0.0, .speed = 0.5});
    points.append(drift::SpeedPoint{.pos = 1.0, .speed = 2.0});
    drift::SpeedCurve curve;
    curve.setPoints(points);

    drift::Project project = makeRetimedToneProject(path, 1.0, false, curve);
    const drift::Clip &clip = project.tracks().at(0).clips.at(0);
    QCOMPARE(clip.type, drift::ClipType::Audio);
    QVERIFY(clip.hasSpeedCurve());
    // Timeline length is the integral of 1/speed over the source: (2/3)·ln4 ≈ 0.924 of it here.
    // Asserting the number rather than just "it changed" is what would catch the ramp being read
    // as its endpoint value or its average.
    QVERIFY2(std::llabs(clip.timelineDuration - 1'848'000) < 60'000,
             qPrintable(QString::number(clip.timelineDuration)));

    AudioMixer mixer;
    mixer.setProject(&project);

    constexpr int kFrames = 1024;
    const int blocks = static_cast<int>(clip.timelineDuration * kToneRate / drift::kUsPerSecond / kFrames) - 2;
    QVERIFY(blocks > 20);

    QVector<float> collected;
    const QList<double> rms = mixBlockRms(mixer, blocks, kFrames, &collected);
    for (int b = 2; b < rms.size(); ++b)
        QVERIFY2(rms.at(b) > 0.02, qPrintable(QStringLiteral("block %1 rms %2").arg(b).arg(rms.at(b))));

    // Pitch has to hold across the whole ramp, not just at the ends.
    const double tone = goertzelMagnitude(collected, 440.0);
    QVERIFY(tone > 10.0 * goertzelMagnitude(collected, 220.0));
    QVERIFY(tone > 10.0 * goertzelMagnitude(collected, 880.0));
}

// The retimer on its own, with a generated source: no ffmpeg, no decoder timing, and the source
// frame count is observable, which is what pins the input and output rates together.
void EngineTest::clipAudioRetimerStreamsSyntheticSource()
{
    constexpr int kFrames = 1024;
    constexpr int kBlocks = 400;

    qint64 pulled = 0;
    auto tonePull = [&pulled](drift::TimeUs startUs, int frames, float *dst) {
        const qint64 startFrame = startUs * kToneRate / drift::kUsPerSecond;
        for (int i = 0; i < frames; ++i) {
            const double phase = 2.0 * kTonePi * 440.0 * static_cast<double>(startFrame + i) / kToneRate;
            dst[i * 2] = dst[i * 2 + 1] = static_cast<float>(std::sin(phase));
        }
        pulled += frames;
        return frames;
    };

    QVector<float> out(kFrames * 2);
    for (double tempo : {0.5, 1.5, 4.0}) {
        drift::ClipAudioRetimer retimer;
        pulled = 0;
        for (int b = 0; b < kBlocks; ++b) {
            drift::ClipAudioBlock block;
            block.identity = 1;
            block.sampleRate = kToneRate;
            block.timelineStartUs =
                static_cast<drift::TimeUs>(b) * kFrames * drift::kUsPerSecond / kToneRate;
            block.tempo = tempo;
            retimer.process(block, tonePull, kFrames, out.data());
            if (b >= 2) {
                QVERIFY2(blockRms(out, kFrames) > 0.2,
                         qPrintable(QStringLiteral("tempo %1 block %2").arg(tempo).arg(b)));
            }
        }
        // Consumption is what proves the loop is closed: a stretcher fed the wrong amount either
        // starves or piles up a backlog, and both are silent failures over a short run.
        const double expected = static_cast<double>(kBlocks) * kFrames * tempo;
        QVERIFY2(std::fabs(pulled - expected) / expected < 0.1,
                 qPrintable(QStringLiteral("tempo %1 pulled %2, expected %3")
                                .arg(tempo)
                                .arg(pulled)
                                .arg(expected)));
    }

    // A ramp changes tempo every block; the total source consumed must still match its integral.
    {
        drift::ClipAudioRetimer retimer;
        pulled = 0;
        double integral = 0.0;
        for (int b = 0; b < kBlocks; ++b) {
            const double tempo = 0.5 + 1.5 * static_cast<double>(b) / (kBlocks - 1);
            integral += tempo * kFrames;
            drift::ClipAudioBlock block;
            block.identity = 2;
            block.sampleRate = kToneRate;
            block.timelineStartUs =
                static_cast<drift::TimeUs>(b) * kFrames * drift::kUsPerSecond / kToneRate;
            block.tempo = tempo;
            retimer.process(block, tonePull, kFrames, out.data());
            if (b >= 2)
                QVERIFY2(blockRms(out, kFrames) > 0.2, qPrintable(QStringLiteral("ramp block %1").arg(b)));
        }
        QVERIFY2(std::fabs(pulled - integral) / integral < 0.1,
                 qPrintable(QStringLiteral("ramp pulled %1, expected %2").arg(pulled).arg(integral)));
    }

    // A source that gives nothing must produce silence and stop asking, not spin.
    {
        drift::ClipAudioRetimer retimer;
        int calls = 0;
        auto emptyPull = [&calls](drift::TimeUs, int, float *) {
            ++calls;
            return 0;
        };
        drift::ClipAudioBlock block;
        block.identity = 3;
        block.sampleRate = kToneRate;
        block.tempo = 0.5;
        retimer.process(block, emptyPull, kFrames, out.data());
        QCOMPARE(blockRms(out, kFrames), 0.0);
        QCOMPARE(calls, 1);
    }
}

void EngineTest::mixerSurvivesConcurrentClipAudioReset()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = makeToneAudio(dir);
    if (path.isEmpty())
        QSKIP("ffmpeg not available to generate a test clip");

    constexpr int kRate = 48000;
    constexpr drift::TimeUs kDurationUs = 2'000'000;

    drift::Project project;
    project.setSampleRate(kRate);
    drift::Track track{.type = drift::TrackType::Audio};
    drift::Clip clip;
    clip.id = QStringLiteral("tone");
    clip.type = drift::ClipType::Audio;
    clip.path = path;
    clip.timelineStart = 0;
    clip.timelineDuration = kDurationUs;
    clip.srcIn = 0;
    clip.srcOut = kDurationUs;
    drift::Effect echo;
    echo.catalogId = QStringLiteral("space.echo");
    clip.audioEffects.append(echo);
    track.clips.append(clip);
    project.tracks().append(track);

    // A retimed clip with no effect chain reaches the same per-clip state through a different door:
    // it needs its stretcher whether or not it has a rack.
    drift::Track retimedTrack{.type = drift::TrackType::Audio};
    drift::Clip retimed = clip;
    retimed.id = QStringLiteral("tone-slow");
    retimed.audioEffects.clear();
    retimed.speed = 0.5;
    retimed.timelineDuration = kDurationUs * 2;
    retimedTrack.clips.append(retimed);
    project.tracks().append(retimedTrack);

    AudioMixer mixer;
    mixer.setProject(&project);

    std::atomic<bool> stop{false};
    QScopedPointer<QThread> mixThread(QThread::create([&mixer, &stop] {
        QVector<float> buffer(1024 * 2);
        drift::TimeUs t = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            mixer.mix(t, 1024, kRate, buffer.data());
            t = (t + 21333) % 1'500'000;
        }
    }));
    QScopedPointer<QThread> resetThread(QThread::create([&mixer, &stop] {
        while (!stop.load(std::memory_order_relaxed))
            mixer.resetClipAudioState();
    }));

    mixThread->start();
    resetThread->start();
    QThread::msleep(2000);
    stop.store(true);
    QVERIFY(mixThread->wait(10000));
    QVERIFY(resetThread->wait(10000));
}

void EngineTest::audioEffectCatalogLoadsPackages()
{
    const QList<AudioEffectEntry> &catalog = audioEffectCatalog();
    QVERIFY2(catalog.size() >= 20,
             qPrintable(QStringLiteral("only %1 audio effects loaded").arg(catalog.size())));

    const AudioEffectEntry *telephone = audioEffectDefForId(QStringLiteral("transmission.telephone"));
    QVERIFY(telephone);
    QCOMPARE(telephone->displayName, QStringLiteral("Telephone"));
    QCOMPARE(telephone->category, QStringLiteral("transmission"));
    QCOMPARE(telephone->processorId, QStringLiteral("bandlimit"));
    QCOMPARE(telephone->icon, QStringLiteral("phone"));
    QVERIFY2(!telephone->thumbnailPath.isEmpty(), "telephone package should ship thumbnail.png");
    QVERIFY(QFileInfo::exists(telephone->thumbnailPath));

    const AudioEffectEntry *chipmunk = audioEffectDefForId(QStringLiteral("voice.chipmunk"));
    QVERIFY(chipmunk);
    QCOMPARE(chipmunk->processorId, QStringLiteral("pitch"));
    QCOMPARE(chipmunk->parameters.size(), 1);
    QCOMPARE(chipmunk->parameters[0].key, QStringLiteral("pitch"));

    const QList<QPair<QString, QString>> categories = audioEffectCategories();
    QSet<QString> slugs;
    for (const auto &c : categories)
        slugs.insert(c.first);
    QVERIFY(slugs.contains(QStringLiteral("voice")));
    QVERIFY(slugs.contains(QStringLiteral("transmission")));

    // Every catalog entry must name a processor, or the mixer has nothing to run.
    for (const AudioEffectEntry &entry : catalog)
        QVERIFY2(!entry.processorId.isEmpty(), qPrintable(entry.id));
}

void EngineTest::audioEffectFactoryBuildsEveryCatalogEntry()
{
    // A manifest naming a processor nobody implements used to be discoverable only by hearing
    // nothing. The catalog rejects those at load, so every entry that survived must build.
    const QList<AudioEffectEntry> &catalog = audioEffectCatalog();
    QVERIFY(!catalog.isEmpty());

    for (const AudioEffectEntry &entry : catalog) {
        QVERIFY2(drift::audiofx::hasProcessor(entry.processorId),
                 qPrintable(QStringLiteral("%1 -> %2").arg(entry.id, entry.processorId)));

        // configure() only reports true once the factory has actually built a chain, so this is
        // what proves the processor exists rather than the effect quietly becoming a passthrough.
        // Run it at every rate the mixer uses: 8 kHz for the subtitle waveform, 22050 for beat
        // detection, 48 kHz for playback and export.
        drift::Effect effect;
        effect.catalogId = entry.id;
        const QVector<drift::AudioEffectSpec> specs = audioEffectSpecsFor({effect});
        QCOMPARE(specs.size(), 1);

        for (const int rate : {8000, 22050, 48000}) {
            constexpr int kFrames = 512;
            QVector<float> buffer = stereoTone(kFrames, 440.0, rate, 0.5f);

            drift::AudioEffectRack rack;
            QVERIFY2(rack.configure(specs, rate), qPrintable(entry.id));
            rack.process(buffer.data(), kFrames);

            QCOMPARE(buffer.size(), kFrames * 2);
            for (int i = 0; i < buffer.size(); ++i) {
                QVERIFY2(std::isfinite(buffer[i]),
                         qPrintable(QStringLiteral("%1 @%2Hz produced a non-finite sample")
                                        .arg(entry.id).arg(rate)));
            }
        }
    }
}

void EngineTest::audioEffectChainAltersSignal()
{
    // A 4 kHz tone pushed through the telephone band-limit (300-3400 Hz) must come back quieter,
    // finite, and the right length — a real filter pass, not a passthrough.
    constexpr int kRate = 48000;
    constexpr int kFrames = 4096;
    const QVector<float> tone = stereoTone(kFrames, 4000.0, kRate);
    const double inRms = rms(tone.constData(), tone.size());

    drift::Effect telephone;
    telephone.catalogId = QStringLiteral("transmission.telephone");
    const QVector<float> out = runRack({telephone}, tone.constData(), kFrames, kRate);

    QCOMPARE(out.size(), kFrames * 2);
    for (float s : out)
        QVERIFY(std::isfinite(s));
    const double outRms = rms(out.constData(), out.size());

    // 4 kHz sits above the 3400 Hz cutoff, so the band-limited output is markedly attenuated.
    QVERIFY2(outRms < inRms * 0.6,
             qPrintable(QStringLiteral("in=%1 out=%2").arg(inRms).arg(outRms)));
    QVERIFY2(outRms > 1e-4, "output is silent — the rack likely failed to build");
}

void EngineTest::audioEffectChainBypassesUnknownEffect()
{
    // An effect whose id is not in the catalog (e.g. an addon the user hasn't installed) must be a
    // clean passthrough, never a dropout.
    constexpr int kRate = 48000;
    constexpr int kFrames = 1024;
    const QVector<float> tone = stereoTone(kFrames, 440.0, kRate);

    drift::Effect unknown;
    unknown.catalogId = QStringLiteral("does.not.exist");
    const QVector<float> out = runRack({unknown}, tone.constData(), kFrames, kRate);

    QCOMPARE(out.size(), kFrames * 2);
    for (int i = 0; i < out.size(); ++i)
        QCOMPARE(out[i], tone[i]);
}

void EngineTest::audioEffectStreamIsContinuousAcrossBlocks()
{
    // Tremolo's LFO phase must carry across blocks; resetting every buffer causes audible jitter.
    constexpr int kRate = 48000;
    constexpr int kBlock = 1024;
    constexpr int kBlocks = 8;
    constexpr int kTotal = kBlock * kBlocks;

    const QVector<float> tone = stereoTone(kTotal, 440.0, kRate);

    drift::Effect tremolo;
    tremolo.catalogId = QStringLiteral("space.tremolo");
    tremolo.parameters.insert(QStringLiteral("rate"), 8.0);
    tremolo.parameters.insert(QStringLiteral("depth"), 0.9);

    const QVector<drift::AudioEffectSpec> specs = audioEffectSpecsFor({tremolo});
    drift::AudioEffectRack rack;
    QVERIFY(rack.configure(specs, kRate));

    QVector<float> streamed(kTotal * 2);
    std::memcpy(streamed.data(), tone.constData(), static_cast<size_t>(kTotal) * 2 * sizeof(float));
    for (int block = 0; block < kBlocks; ++block)
        rack.process(streamed.data() + block * kBlock * 2, kBlock);

    const QVector<float> reference = runRack({tremolo}, tone.constData(), kTotal, kRate);
    QCOMPARE(reference.size(), streamed.size());

    // Block-by-block must be bit-comparable to one pass: the sub-block loop inside the rack means
    // the caller's block size cannot change the result.
    double maxDiff = 0.0;
    for (int i = 0; i < streamed.size(); ++i)
        maxDiff = std::max(maxDiff, static_cast<double>(std::abs(streamed[i] - reference[i])));
    QVERIFY2(maxDiff < 1e-6,
             qPrintable(QStringLiteral("block boundary discontinuity maxDiff=%1").arg(maxDiff)));
}

void EngineTest::audioEffectFlangerProcessesSignal()
{
    constexpr int kRate = 48000;
    constexpr int kFrames = 4096;
    const QVector<float> tone = stereoTone(kFrames, 440.0, kRate);

    const AudioEffectEntry *flanger = audioEffectDefForId(QStringLiteral("space.flanger"));
    QVERIFY(flanger);
    QCOMPARE(flanger->parameters.size(), 7);
    QCOMPARE(flanger->processorId, QStringLiteral("flanger"));

    drift::Effect effect;
    effect.catalogId = flanger->id;
    effect.parameters.insert(QStringLiteral("rate"), 0.8);
    effect.parameters.insert(QStringLiteral("phase"), 180.0);
    effect.parameters.insert(QStringLiteral("mix"), 80.0);
    effect.parameters.insert(QStringLiteral("invert"), 1.0);

    const QVector<float> out = runRack({effect}, tone.constData(), kFrames, kRate);
    QCOMPARE(out.size(), kFrames * 2);

    int changed = 0;
    for (int i = 0; i < kFrames * 2; ++i) {
        QVERIFY(std::isfinite(out[i]));
        if (std::abs(out[i] - tone[i]) > 1e-4)
            ++changed;
    }
    const double inRms = rms(tone.constData(), kFrames * 2);
    const double outRms = rms(out.constData(), kFrames * 2);

    QVERIFY2(changed > kFrames, "flanger output matches input — the rack likely failed");
    QVERIFY2(outRms > 1e-4, "flanger output is silent");
    QVERIFY2(outRms < inRms * 2.0,
             qPrintable(QStringLiteral("flanger blew up: in=%1 out=%2").arg(inRms).arg(outRms)));
}

void EngineTest::audioEffectRackPrimingAlignsLatentStages()
{
    // The pitch shifter reads out of a delay line, so it has real latency. The libavfilter path
    // zero-filled what the graph had not produced yet, which is why a pitch-shifted clip opened
    // with silence and then stayed offset. Priming on the audio that precedes the block is the fix.
    constexpr int kRate = 48000;
    constexpr int kBlock = 2048;

    drift::Effect chipmunk;
    chipmunk.catalogId = QStringLiteral("voice.chipmunk");
    chipmunk.parameters.insert(QStringLiteral("pitch"), 1.0);

    const QVector<drift::AudioEffectSpec> specs = audioEffectSpecsFor({chipmunk});
    drift::AudioEffectRack primed;
    QVERIFY(primed.configure(specs, kRate));

    const int primeFrames = primed.primeFrames();
    QVERIFY2(primeFrames > 0, "a latent stage must ask for priming");

    const QVector<float> continuous = stereoTone(primeFrames + kBlock, 440.0, kRate);

    primed.warmUp(continuous.constData(), primeFrames);
    QVector<float> primedOut(kBlock * 2);
    std::memcpy(primedOut.data(), continuous.constData() + primeFrames * 2,
                static_cast<size_t>(kBlock) * 2 * sizeof(float));
    primed.process(primedOut.data(), kBlock);

    drift::AudioEffectRack cold;
    QVERIFY(cold.configure(specs, kRate));
    QVector<float> coldOut(kBlock * 2);
    std::memcpy(coldOut.data(), continuous.constData() + primeFrames * 2,
                static_cast<size_t>(kBlock) * 2 * sizeof(float));
    cold.process(coldOut.data(), kBlock);

    // The opening of the block is the part latency eats. Primed, it carries signal; cold, it is
    // the silence users heard at the head of every pitch-shifted clip.
    constexpr int kHead = 512;
    const double primedHead = rms(primedOut.constData(), kHead * 2);
    const double coldHead = rms(coldOut.constData(), kHead * 2);

    QVERIFY2(primedHead > 0.1,
             qPrintable(QStringLiteral("primed head is quiet: %1").arg(primedHead)));
    QVERIFY2(primedHead > coldHead * 4.0,
             qPrintable(QStringLiteral("priming changed nothing: primed=%1 cold=%2")
                            .arg(primedHead).arg(coldHead)));
}

// A pitch shifter has exactly one job and "the output is finite" does not check it. Chipmunk must
// raise the pitch and Deep Voice must lower it, by the ratio the manifest asks for.
//
// The granular shifter reads two taps out of one delay line. juce's popSample only advances the
// read pointer when told to, and leaving it frozen for both taps pinned the read position while
// the write position kept moving: the traversal rate collapsed from `ratio` to `ratio - 1`, so
// 1.5 came out an octave down and 0.7 came out reversed.
void EngineTest::pitchShiftMovesPitchInTheRightDirection()
{
    constexpr int kRate = 48000;
    constexpr double kToneHz = 440.0;
    constexpr int kMeasure = 24000; // half a second is plenty of resolution

    struct Case
    {
        const char *id;
        double ratio;
    };

    for (const Case &testCase : {Case{"voice.chipmunk", 1.5}, Case{"voice.deep", 0.7}}) {
        drift::Effect effect;
        effect.catalogId = QString::fromLatin1(testCase.id);
        effect.parameters.insert(QStringLiteral("pitch"), testCase.ratio);

        const QVector<drift::AudioEffectSpec> specs = audioEffectSpecsFor({effect});
        QCOMPARE(specs.size(), 1);

        drift::AudioEffectRack rack;
        QVERIFY(rack.configure(specs, kRate));

        const int prime = rack.primeFrames();
        const QVector<float> tone = stereoTone(prime + kMeasure, kToneHz, kRate);
        rack.warmUp(tone.constData(), prime);

        QVector<float> out(kMeasure * 2);
        std::memcpy(out.data(), tone.constData() + prime * 2,
                    static_cast<size_t>(kMeasure) * 2 * sizeof(float));
        rack.process(out.data(), kMeasure);

        const double shifted = kToneHz * testCase.ratio;
        const double atShifted = toneEnergy(out.constData(), kMeasure, shifted, kRate);
        const double atOriginal = toneEnergy(out.constData(), kMeasure, kToneHz, kRate);
        // Where the frozen read pointer used to put it.
        const double atBroken = toneEnergy(out.constData(), kMeasure,
                                           std::abs(kToneHz * (testCase.ratio - 1.0)), kRate);

        QVERIFY2(atShifted > atOriginal * 4.0,
                 qPrintable(QStringLiteral("%1: energy at the shifted %2 Hz (%3) does not dominate "
                                           "the unshifted %4 Hz (%5)")
                                .arg(testCase.id).arg(shifted).arg(atShifted).arg(kToneHz).arg(atOriginal)));
        QVERIFY2(atShifted > atBroken * 4.0,
                 qPrintable(QStringLiteral("%1: energy at %2 Hz (%3) does not dominate the "
                                           "ratio-minus-one artefact at %4 Hz (%5)")
                                .arg(testCase.id).arg(shifted).arg(atShifted)
                                .arg(std::abs(kToneHz * (testCase.ratio - 1.0))).arg(atBroken)));

        // And it must still be a tone, not a smear: the shifted partial should carry real level.
        QVERIFY2(atShifted > 0.02,
                 qPrintable(QStringLiteral("%1: shifted tone is weak (%2)").arg(testCase.id).arg(atShifted)));
    }
}

void EngineTest::audioEffectRackParameterChangeIsContinuous()
{
    // The libavfilter graph rebuilt itself whenever any value changed, so every slider tick
    // restarted the DSP from zero — the click users heard while dragging. Values now ramp.
    constexpr int kRate = 48000;
    constexpr int kBlock = 4096;

    // Constant input: any jump in the output is the parameter, not the signal.
    QVector<float> first(kBlock * 2, 0.5f);
    QVector<float> second(kBlock * 2, 0.5f);

    drift::Effect muffled;
    muffled.catalogId = QStringLiteral("transmission.muffled");
    muffled.parameters.insert(QStringLiteral("cutoff"), 4000.0);
    muffled.parameters.insert(QStringLiteral("gain"), 0.5);

    drift::AudioEffectRack rack;
    QVERIFY(rack.configure(audioEffectSpecsFor({muffled}), kRate));
    rack.process(first.data(), kBlock);

    muffled.parameters.insert(QStringLiteral("gain"), 2.0);
    QVERIFY(rack.configure(audioEffectSpecsFor({muffled}), kRate));
    rack.process(second.data(), kBlock);

    const float boundaryStep = std::abs(second[0] - first[(kBlock - 1) * 2]);
    // An unsmoothed 0.5 -> 2.0 gain change on a 0.5 input steps by 0.75 in one sample.
    QVERIFY2(boundaryStep < 0.05f,
             qPrintable(QStringLiteral("parameter change stepped by %1").arg(boundaryStep)));

    // It must still actually arrive at the new value.
    const float settled = second[(kBlock - 1) * 2];
    QVERIFY2(std::abs(settled - 1.0f) < 0.05f,
             qPrintable(QStringLiteral("gain never reached its target: %1").arg(settled)));
}

// ---- DeepFilterNet3 denoiser -------------------------------------------------------------
//
// The model directory is gitignored, so every case here skips when it is absent. Point
// DRIFT_DENOISE_MODEL_DIR elsewhere to test an installed addon instead.
namespace {

bool denoiseModelAvailable()
{
    const QString dir = QString::fromUtf8(DRIFT_TEST_DENOISE_MODEL_DIR);
    if (QDir(dir).exists())
        qputenv("DRIFT_DENOISE_MODEL_DIR", dir.toUtf8());
    return drift::DeepFilterDenoiser::modelPresent();
}

double rms(const std::vector<float> &x, size_t from = 0, size_t to = SIZE_MAX)
{
    const size_t end = std::min(to, x.size());
    if (end <= from)
        return 0.0;
    double acc = 0.0;
    for (size_t i = from; i < end; ++i)
        acc += double(x[i]) * x[i];
    return std::sqrt(acc / double(end - from));
}

std::vector<float> whiteNoise(int samples, float amplitude, uint32_t seed)
{
    std::mt19937 rng(seed);
    std::normal_distribution<float> gauss(0.0f, amplitude);
    std::vector<float> out(size_t(samples), 0.0f);
    for (int i = 0; i < samples; ++i)
        out[size_t(i)] = gauss(rng);
    return out;
}

} // namespace

// The auxiliary blob is the model's own ERB geometry. If the forward and inverse matrices do not
// describe the same 32 bands, every gain lands on the wrong frequencies and the result is
// plausible-sounding rubbish rather than an obvious failure.
void EngineTest::denoiseAuxiliaryConstantsRoundTrip()
{
    const QString path =
        QDir(QString::fromUtf8(DRIFT_TEST_DENOISE_MODEL_DIR)).filePath(QStringLiteral("deepfilter-auxiliary.bin"));
    if (!QFile::exists(path))
        QSKIP("DeepFilterNet3 model not installed");

    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    constexpr int kBins = 481;
    constexpr int kBands = 32;
    constexpr int kFft = 960;
    QCOMPARE(f.size(), qint64(kBins * kBands + kBands * kBins + kFft) * 4);

    const QByteArray blob = f.readAll();
    const auto *fwd = reinterpret_cast<const float *>(blob.constData());
    const float *inv = fwd + kBins * kBands;
    const float *win = inv + kBands * kBins;

    // Every bin belongs to exactly one band, in both directions.
    for (int k = 0; k < kBins; ++k) {
        int owners = 0;
        for (int b = 0; b < kBands; ++b) {
            if (fwd[k * kBands + b] != 0.0f)
                ++owners;
            QCOMPARE(inv[b * kBins + k] != 0.0f, fwd[k * kBands + b] != 0.0f);
        }
        QCOMPARE(owners, 1);
    }

    // The forward weights are 1/bandwidth, so a flat unit spectrum must average to 1 per band.
    for (int b = 0; b < kBands; ++b) {
        double acc = 0.0;
        for (int k = 0; k < kBins; ++k)
            acc += fwd[k * kBands + b];
        QVERIFY2(std::abs(acc - 1.0) < 1e-5,
                 qPrintable(QStringLiteral("band %1 forward weights sum to %2").arg(b).arg(acc)));
    }

    // Vorbis window: zero at the edges, unity at the centre, and Princen-Bradley (w^2 sums to 1
    // across a 50% overlap) — the property the overlap-add synthesis relies on.
    QCOMPARE(win[0], 0.0f);
    for (int n = 0; n < kFft / 2; ++n) {
        const double a = win[n];
        const double b = win[n + kFft / 2];
        QVERIFY2(std::abs(a * a + b * b - 1.0) < 1e-4,
                 qPrintable(QStringLiteral("window fails Princen-Bradley at %1").arg(n)));
    }
}

// Length must survive exactly (the clip on the timeline depends on it), and digital silence must
// come back as silence rather than as the model's idea of a noise floor.
void EngineTest::denoisePreservesLengthAndSilence()
{
    if (!denoiseModelAvailable())
        QSKIP("DeepFilterNet3 model not installed");

    drift::DeepFilterDenoiser &dn = drift::DeepFilterDenoiser::instance();
    if (!dn.available())
        QSKIP(qPrintable(dn.lastError()));

    const int rate = drift::DeepFilterDenoiser::sampleRate();
    const std::vector<float> silence(size_t(rate) * 2, 0.0f);
    const std::vector<float> out = dn.denoise(silence, {});

    QCOMPARE(out.size(), silence.size());
    QVERIFY2(rms(out) < 1e-6, qPrintable(QStringLiteral("silence came back at %1").arg(rms(out))));

    // An odd, non-frame-aligned length must round-trip too.
    const std::vector<float> odd(size_t(rate) + 137, 0.0f);
    QCOMPARE(dn.denoise(odd, {}).size(), odd.size());
}

// The point of the feature. Speech-free broadband noise is the one input whose correct handling
// can be asserted without shipping an audio fixture: the model must recognise that none of it is
// speech and pull it a long way down.
//
// Note that a synthesised "voice" (a harmonic stack, say) is NOT a useful test signal here — the
// model correctly declines to treat it as speech and suppresses it too, so a test built on one
// measures nothing. The end-to-end quality check is SI-SDR against a real reference recording;
// see the plan's verification notes.
void EngineTest::denoiseRemovesBroadbandNoise()
{
    if (!denoiseModelAvailable())
        QSKIP("DeepFilterNet3 model not installed");

    drift::DeepFilterDenoiser &dn = drift::DeepFilterDenoiser::instance();
    if (!dn.available())
        QSKIP(qPrintable(dn.lastError()));

    const int rate = drift::DeepFilterDenoiser::sampleRate();
    const std::vector<float> noise = whiteNoise(rate * 4, 0.1f, 1234);

    const std::vector<float> out = dn.denoise(noise, {});
    QCOMPARE(out.size(), noise.size());
    for (float s : out)
        QVERIFY(std::isfinite(s));

    // Skip the first half second: the model is still settling into the signal there.
    const double in = rms(noise, size_t(rate) / 2);
    const double got = rms(out, size_t(rate) / 2);
    QVERIFY2(got < in * 0.25,
             qPrintable(QStringLiteral("noise only fell from %1 to %2").arg(in).arg(got)));
}

// Audio longer than one inference window is stitched from several ONNX runs. The run-up frames and
// the carried normalisation state exist so those joins are inaudible; this is what catches it if
// they regress. A window that started cold would need time to recognise the noise, so the samples
// just after the boundary would come through markedly louder than those just before it.
void EngineTest::denoiseHasNoSeamAcrossWindows()
{
    if (!denoiseModelAvailable())
        QSKIP("DeepFilterNet3 model not installed");

    drift::DeepFilterDenoiser &dn = drift::DeepFilterDenoiser::instance();
    if (!dn.available())
        QSKIP(qPrintable(dn.lastError()));

    const int rate = drift::DeepFilterDenoiser::sampleRate();
    // 25 s crosses the 20 s window boundary once, with room either side of the join.
    const std::vector<float> noise = whiteNoise(rate * 25, 0.1f, 99);
    const std::vector<float> out = dn.denoise(noise, {});
    QCOMPARE(out.size(), noise.size());
    for (float s : out)
        QVERIFY(std::isfinite(s));

    const size_t seam = size_t(rate) * 20;
    const size_t win = size_t(rate) / 5; // 200 ms
    const double before = rms(out, seam - win, seam);
    const double after = rms(out, seam, seam + win);
    const double ratio = after / std::max(before, 1e-12);
    QVERIFY2(ratio > 0.4 && ratio < 2.5,
             qPrintable(QStringLiteral("energy steps across the window seam: %1 -> %2 (x%3)")
                            .arg(before)
                            .arg(after)
                            .arg(ratio)));
}

// The denoised render is only useful if the rest of the app can read it back as ordinary media.
void EngineTest::audioFileWriterRoundTripsThroughClipReader()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("denoised.flac"));

    constexpr int kRate = 48000;
    constexpr int kFrames = kRate; // 1 s
    std::vector<float> tone(size_t(kFrames) * 2);
    for (int i = 0; i < kFrames; ++i) {
        const float s = 0.5f * std::sin(2.0 * M_PI * 440.0 * i / kRate);
        tone[size_t(i) * 2] = s;
        tone[size_t(i) * 2 + 1] = s;
    }

    QString error;
    drift::AudioFileWriter writer;
    QVERIFY2(writer.open(path, kRate, 2, &error), qPrintable(error));
    // Deliberately not a multiple of the encoder frame size, to exercise the partial-frame buffer.
    QVERIFY2(writer.writeFrames(tone.data(), 1000, &error), qPrintable(error));
    QVERIFY2(writer.writeFrames(tone.data() + 2000, kFrames - 1000, &error), qPrintable(error));
    QVERIFY2(writer.finish(&error), qPrintable(error));
    QVERIFY(QFileInfo::exists(path));
    QVERIFY(!QFileInfo::exists(path + QStringLiteral(".part")));

    std::vector<float> read(size_t(kFrames) * 2, 0.0f);
    const int got = ClipReaderPool::instance().readAudioInterleaved(path, 0, kFrames, kRate,
                                                                    read.data());
    QVERIFY2(got > kFrames / 2, qPrintable(QStringLiteral("decoded only %1 frames").arg(got)));

    double acc = 0.0;
    for (int i = 0; i < got * 2; ++i)
        acc += double(read[size_t(i)]) * read[size_t(i)];
    const double outRms = std::sqrt(acc / (got * 2));
    // 0.5 amplitude sine -> 0.3536 RMS. FLAC is lossless, so this is tight.
    QVERIFY2(std::abs(outRms - 0.3536) < 0.02,
             qPrintable(QStringLiteral("round-tripped RMS %1").arg(outRms)));
}

void EngineTest::onsetsDetectClickTrackTempo()
{
    constexpr int kRate = 22050;
    constexpr double kPeriod = 0.5; // 120 BPM
    constexpr int kClicks = 20;
    constexpr int kFrames = int(kRate * kPeriod * kClicks);

    // Exponentially decaying noise bursts — broadband, so every FFT bin jumps at once.
    std::vector<float> pcm(kFrames, 0.0f);
    std::mt19937 rng(1234);
    std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
    for (int c = 0; c < kClicks; ++c) {
        const int at = int(c * kPeriod * kRate);
        for (int i = 0; i < kRate / 20 && at + i < kFrames; ++i)
            pcm[size_t(at + i)] = noise(rng) * std::exp(-i / (kRate * 0.01f));
    }

    const AudioBeatAnalysis a = AudioOnsets::analyze(pcm.data(), kFrames, kRate, 0.0);

    QVERIFY2(std::abs(a.bpm - 120.0) < 2.0,
             qPrintable(QStringLiteral("bpm %1").arg(a.bpm)));
    QVERIFY2(a.confidence > 0.5, qPrintable(QStringLiteral("confidence %1").arg(a.confidence)));
    // Including the click at sample 0 — flux only sees it because analyze() pads the front.
    QCOMPARE(a.onsets.size(), kClicks);
    for (int i = 0; i < a.onsets.size(); ++i) {
        const double expected = i * kPeriod;
        QVERIFY2(std::abs(a.onsets[i].seconds - expected) < 0.025,
                 qPrintable(QStringLiteral("onset %1 at %2, expected %3")
                                .arg(i).arg(a.onsets[i].seconds).arg(expected)));
    }

    // The grid must line up with the clicks, not merely have the right spacing.
    QVERIFY(!a.beats.isEmpty());
    for (double b : a.beats) {
        const double offset = std::fmod(b + kPeriod / 2, kPeriod) - kPeriod / 2;
        QVERIFY2(std::abs(offset) < 0.03, qPrintable(QStringLiteral("beat at %1").arg(b)));
    }

    // Times are absolute: the same PCM offset into the timeline shifts everything.
    const AudioBeatAnalysis shifted = AudioOnsets::analyze(pcm.data(), kFrames, kRate, 7.5);
    QVERIFY(std::abs(shifted.onsets.first().seconds - 7.5) < 0.025);
}

void EngineTest::onsetsIgnoreSilence()
{
    constexpr int kRate = 22050;
    const std::vector<float> silence(kRate * 5, 0.0f);

    const AudioBeatAnalysis a = AudioOnsets::analyze(silence.data(), int(silence.size()), kRate, 0.0);
    QVERIFY(a.onsets.isEmpty());
    QCOMPARE(a.bpm, 0.0);
    QVERIFY(a.beats.isEmpty());

    // Too short to say anything about tempo, even with content.
    std::vector<float> blip(kRate, 0.0f);
    for (int i = 0; i < kRate / 40; ++i)
        blip[size_t(i + 1000)] = 0.8f;
    const AudioBeatAnalysis b = AudioOnsets::analyze(blip.data(), int(blip.size()), kRate, 0.0);
    QCOMPARE(b.bpm, 0.0);
}

QTEST_MAIN(EngineTest)
#include "tst_engine.moc"
