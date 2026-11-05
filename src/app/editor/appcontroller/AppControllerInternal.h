#pragma once

#include "app/editor/AppController.h"
#include "app/editor/AppControllerDetail.h"
#include "app/bin/AssetLibrary.h"
#include "core/text/animation/TextAnimationPreset.h"

#include "app/mcp/McpDetail.h"

struct StreamInfo;
namespace drift {
struct EffectParamSpec;
struct EffectPresetMeta;
}

QString writeTargetPath(const QUrl &url, const QString &suffix);
QString readTargetPath(const QUrl &url);
QString exportTargetProblem(const QUrl &url, const QString &path);
bool commitWriteTarget(const QString &staged, const QUrl &url, const std::function<bool(qint64, qint64)> &progress, QString *error);
void discardWriteTarget(const QString &staged, const QUrl &url, bool deleteDestination);
QString exportDisplayName(const QUrl &url);

namespace drift::appcontroller_detail {
using namespace drift::appdetail;
using drift::mcpdetail::findClipById;
using drift::mcpdetail::runBeatAnalysis;
} // namespace drift::appcontroller_detail

namespace drift::appcontroller_detail {

// Bezier handles as [x1, y1, x2, y2]; any other length leaves the shape's handles as they were.
// A Bezier curve always ends up with handles set, so they are written out with the project.
inline drift::FadeShape shapeWithHandles(drift::FadeShape shape, const QVariantList &handles,
                                         bool bezier)
{
    if (handles.size() == 4)
        shape.setHandles(QPointF(handles.at(0).toDouble(), handles.at(1).toDouble()),
                         QPointF(handles.at(2).toDouble(), handles.at(3).toDouble()));
    else if (bezier && !shape.hasHandles())
        shape.setHandles(shape.handle1(), shape.handle2());
    return shape;
}

inline bool sameHandles(const drift::FadeShape &a, const drift::FadeShape &b)
{
    return a.hasHandles() == b.hasHandles() && a.handle1() == b.handle1()
           && a.handle2() == b.handle2();
}

inline QVariantList handlesToVariant(const drift::FadeShape &shape)
{
    return {shape.handle1().x(), shape.handle1().y(), shape.handle2().x(), shape.handle2().y()};
}

// Packages the user brought themselves (an imported .driftfx, or a folder dropped in by hand) live
// under <AppData>/<subdir>; signed addons install to their own roots, and the bundled ones sit
// beside the binary. That location is what the "My effects" chip filters on.
bool isUserPackageDir(const QString &packageDir, const QString &subdir);
// One clip covering the whole of another leaves nothing to cross between them.
bool clipsOverlapCompletely(const drift::Clip &a, const drift::Clip &b);

// Pairs that already overlapped completely in `before` (an older project) are left alone.
bool editCreatesCompleteOverlap(const drift::Project &before, const drift::Project &after);

int findTransitionPartnerIndex(const drift::Track &track, int fromIndex);

bool trackAllowsTransitions(drift::TrackType type);

void syncOverlapTransitionsOnTrack(drift::Track &track);


// A slot value as the plain QML/MCP value: colours as hex, scalars as numbers, vectors as
// [x, y]. The typed {type, value} object form is accepted back too.
QVariant slotValueToVariant(const drift::VectorSlotValue &v);

drift::VectorSlotValue slotValueFromVariant(const QVariant &v, drift::VectorSlotValue::Type hint);

drift::VectorSlotValue::Type slotTypeForSpec(const drift::TextAnimParamSpec &spec);

drift::VectorSlotValue::Type guessSlotType(const QVariant &v);

QVariantMap paramsToMap(const QMap<QString, drift::VectorSlotValue> &params);

// Typed by the spec when the param is declared, guessed from the value otherwise.
void mergeParamsFromMap(QMap<QString, drift::VectorSlotValue> *params, const QVariantMap &m,
                        const QList<drift::TextAnimParamSpec> &specs);

QList<drift::TextAnimParamSpec> specsForSlot(const drift::TextAnimationSlot &slot);

extern const char *const kSlotShortcuts[7];

QVariantMap textAnimationSlotToMap(const drift::TextAnimationSlot &slot);

QVariantMap textAnimationSetToMap(const drift::TextAnimationSet &set);

// Partial patch of one slot. Choosing a preset resets its params to the preset's defaults
// (the reveal controls are kept when `keepControls` is set); `animators` makes the slot inline.
void applyTextAnimationSlotPatch(drift::TextAnimationSlot *slot, const QVariantMap &m);

void applyTextCaretPatch(drift::TextCaret *c, const QVariantMap &m);

void applyTextAnimationSetPatch(drift::TextAnimationSet *set, const QVariantMap &m);

// The legacy animIn/animOut shape: {kind, duration, ease, unit, stagger, order}. Read back from
// the slot for old callers, and accepted as a patch by rebuilding the slot from the kind table.
const QMap<QString, QString> &legacyKindForPreset();

QVariantMap legacyTextAnimationMap(const drift::TextAnimationSlot &slot);

QVariantMap textLayerToMap(const drift::TextShadingLayer &layer);

QJsonObject deepMergeJson(QJsonObject base, const QJsonObject &patch);

void applyTextLayerPatch(drift::TextShadingLayer *layer, const QVariantMap &patch);

// The "layers" (replace the stack) and "layer" (patch one, by id or index) keys a style patch
// may carry. Returns true when either applied.
bool applyLayerStackPatch(QList<drift::TextShadingLayer> &stack, const QVariantMap &m,
                          const drift::TextShadingLayer &fallback);

// Where a new layer of this kind goes: shadows and glows behind, strokes under the fills, fills
// on top — what a user expects to see without reordering.
int naturalLayerIndex(const QList<drift::TextShadingLayer> &layers, drift::TextLayerKind kind);

QVariantMap textHighlightToMap(const drift::TextHighlight &h);

void applyTextHighlightPatch(drift::TextHighlight *highlight, const QVariantMap &m);

QVariantMap wordAccentToMap(const drift::WordAccent &a);

// `timelineStart` places the keyframe times on the timeline for the inspector rows and the
// keyframe graph, which read them the same way they read a mask's.
QVariantMap textStyleToMap(const drift::TextStyle &s, drift::TimeUs timelineStart);

QVariant vectorSlotValueToVariant(const drift::VectorSlotValue &v);

QVariantMap pdfSourceToMap(const drift::PdfSource &s, drift::TimeUs timelineStart);

// A Model3d clip's file is a .glb or a Gaussian splat input, which never ends in .glb; judged by
// name so building a clip's map never opens the file.
bool isSplatSourcePath(const QString &path);

QVariantMap model3dSourceToMap(const drift::Model3dSource &m, drift::TimeUs timelineStart);

// Before format 13 a model kept its pose on the model ("scale", "depth", "rotX/Y/Z"), and scripts
// written then still address it that way. Each legacy key becomes the transform writes it now
// means, at `relative`; empty for anything else. `projectHeight` turns the old fractions into px.
QList<QPair<QString, double>> legacyModel3dPoseWrites(const drift::Clip &clip, const QString &key,
                                                      double value, drift::TimeUs relative,
                                                      double projectHeight);

bool isLegacyModel3dPoseKey(const QString &key);


drift::Mask maskFromMap(const QVariantMap &m);

QVariantMap transitionToMap(const drift::Project &project, const drift::Track &track, const drift::Transition &t);

bool isSyntheticTimelineClip(drift::ClipType type);

drift::TimeUs syntheticClipMaxDurationUs();

void syncSyntheticSourceRange(drift::Clip &clip);


bool parseIndexedProp(const QString &prop, QLatin1String prefix, int *effectIndex, QString *paramKey);

// Effect parameters are addressed as "fx.<effectIndex>.<paramKey>" so the whole generic keyframe
// API — set / remove / move / interpolation / keyframe-strip selection — reaches them without a
// parallel set of invokables. Indices match the effectIndex the effect invokables already take.
bool parseEffectProp(const QString &prop, int *effectIndex, QString *paramKey);

// Audio effect parameters, the same way: "afx.<audioEffectIndex>.<paramKey>".
bool parseAudioEffectProp(const QString &prop, int *effectIndex, QString *paramKey);

// A mask scalar is addressed as "mask.<key>" — no index, because a Mask adjustment carries
// exactly one mask. The prop resolves against the adjustment clip itself; redirectToKeyframeHost
// is what walks there from the media clip the user actually has selected.
bool parseMaskProp(const QString &prop, QString *key);

// A text style scalar is addressed as "text.<key>" (pixelSize, layer.shadow.blur, color.r, …)
// and lives on the text clip itself, so no host redirect applies. The style resolves the key to
// its canonical spelling (legacy names map onto the layers they became).
bool parseTextProp(const drift::TextStyle &style, const QString &prop, QString *key);

// A shape style scalar is addressed as "shape.<key>" (cornerRadius, layer.stroke.width, …) and
// lives on the shape clip itself.
bool parseShapeProp(const drift::Clip &clip, const QString &prop, QString *key);

// An SVG override scalar is addressed as "vector.<slot>[.r|.g|.b|.a]" ("vector.svg.logo.fill.r",
// "vector.svg.strokeWidth") and lives on the vector clip itself. Only the svg.* keys are
// keyframable: Lottie slots are baked into the parsed animation.
bool parseVectorProp(const drift::Clip &clip, const QString &prop, QString *key);

// A model pose/light scalar is addressed as "model3d.<key>" ("model3d.rotY", "model3d.scale").
bool parseModel3dProp(const drift::Clip &clip, const QString &prop, QString *key);

// A PDF viewport scalar is addressed as "pdf.<key>" ("pdf.scrollY", "pdf.zoom").
bool parsePdfProp(const drift::Clip &clip, const QString &prop, QString *key);

void clearClipPose3d(drift::Clip &clip);

drift::KeyframeTrack<double> *transformTrackForProp(drift::Clip &clip, const QString &prop);

// createIfMissing must stay false on read paths: an effect param's track is created lazily, and
// QMap::operator[] would otherwise insert an empty one into the project behind a const accessor.
drift::KeyframeTrack<double> *keyframeTrackForProp(drift::Clip &clip, const QString &prop,
                                                   bool createIfMissing);

const drift::KeyframeTrack<double> *keyframeTrackForProp(const drift::Clip &clip, const QString &prop);

inline constexpr drift::TimeUs kKeyframeToleranceUs = drift::kUsPerSecond / 30;


// Writes `value` for `prop`, returning false when the write was refused (auto-key off with no key
// near the playhead to retarget).
//
// Effect params differ from transform props in one way: they already have a static home in
// Effect::parameters, so silently minting a keyframe track on an ordinary slider drag would make
// every param "animated". An un-keyed param therefore only becomes animated on an explicit
// diamond click (force) or with auto-key on; otherwise the write lands on the static value. Keyed
// params mirror into the static value too, so deleting the last key leaves the param where the
// user last put it rather than snapping back to the catalog default.
bool writeMaskScalar(drift::Mask &mask, const QString &key, double value);

bool writeClipPropValue(drift::Clip &clip, const QString &prop, drift::TimeUs relative, double value,
                        bool autoKey, bool force);

drift::EasingSpec easingSpecFromVariant(const QVariantMap &spec);

QVariantList keyframeListToVariant(const drift::KeyframeTrack<double> &track, drift::TimeUs timelineStart);

QVariantMap keyframeTrackToMap(const drift::KeyframeTrack<double> &track, drift::TimeUs timelineStart);


void fitClipLayoutToCanvas(drift::Clip &clip, int mediaW, int mediaH, int canvasW, int canvasH);

// A new model clip's rect is its bounding box's front face, with the largest rest extent half the
// canvas height across, centred. Models are always 3D layers.
void placeModelClipLayout(drift::Clip &clip, int canvasW, int canvasH);

// Fills what the inspector and the overlay need from the .glb (animation list, rest bounds) so
// neither ever parses on the GUI thread; the loader's cache makes this a hit after the first.
// A splat has no animations and is bounded by its robust fit box. The import probe already loaded
// it into the renderer's cache; a path added without importing pays the load here.
void probeModel3dSource(drift::Model3dSource &source);

// Whether `asset` carries a bin-preview trim at all (AssetLibrary::setAssetTrim) — trimOutSeconds
// < 0 alone does not mean "no trim": an in-only trim (start moved, end left alone) stores exactly
// that, so a bare "is there an out point" check drops it. Shared by every place below that needs
// to agree on what a trimmed asset's clip duration actually is.
bool assetHasTrim(const QVariantMap &asset);

// The clip duration a bin-preview trim actually produces out of `fullDurationUs` — needed both by
// applyAssetLayout (to set srcIn/srcOut) and by placement math (resolveClipStart, the batch
// cursor) *before* the clip exists for applyAssetLayout to derive it the usual way. One function
// so every add-from-asset site agrees with applyAssetLayout on what "the duration" means.
drift::TimeUs trimmedClipDurationUs(const QVariantMap &asset, drift::TimeUs fullDurationUs);

void applyAssetLayout(drift::Clip &clip, const QVariantMap &asset, int canvasW, int canvasH);

// shapeAspect is the catalog's default width/height for the shape being added; a square shape must
// get a square box, since every shape now simply fills its layout rect.
void applyDefaultVisualLayout(drift::Clip &clip, int canvasW, int canvasH, double shapeAspect = 1.6);

bool assetHasAudioStreams(const drift::Project &project, AssetLibrary *library, const QString &assetId);

// MediaProbe::audioStreams reopens the container and runs avformat_find_stream_info every call,
// and the callers below include QML bindings that re-evaluate on any timeline change — a clip's
// context menu was re-probing its file just to decide whether to show a menu item.
//
// Keyed on size and mtime as well as path, so a file replaced on disk re-probes rather than
// serving stale streams. Mutexed because the detach helpers run on the GUI thread while the
// asset probes do not, and this is cheap enough that proving which is which is not worth it.
QList<StreamInfo> cachedAudioStreams(const QString &path);

bool clipHasEmbeddedAudio(const drift::Project &project, AssetLibrary *library, const drift::Clip &clip);

drift::Clip makeAudioCompanionFromVideo(const drift::Clip &videoClip, const QString &linkId = {},
                                        int audioStreamIndex = 0, const QString &customName = {});

// Move the audio-effect adjustments pinned to a video clip onto a lane of the audio track that
// has just taken over its audio.
//
// A lane's effects reach the mix through the clips of the track the lane hangs off
// (AudioMixer's laneAudioEffects), and separating audio sets suppressEmbeddedAudio so the mixer
// stops reading that video clip at all. Leaving the adjustments on the video track's lane would
// therefore silently stop them applying: the video clip they modify no longer contributes audio,
// and the freshly created audio track has no lanes of its own.
void migrateAudioEffectsToCompanion(drift::Project &project, const QString &videoClipId,
                                    const QString &audioClipId);

// Relative position of a video track inside the video-track group.
//
// Example:
//
//   project tracks: Video, Video, Video, Audio, Audio
//                   0      1      2
//
// The result is independent of absolute project-track indexes so audio tracks can
// mirror the video hierarchy without interleaving video and audio tracks.
int videoTrackOrdinal(const drift::Project &project, int trackIndex);

// Returns the source video-track ordinal of an extracted audio track, without making
// its clip editing behavior depend on the source. Older linked companions still work.
int ownerVideoOrdinalForAudioTrack(const drift::Project &project, int audioTrackIndex);

// Find where a newly detached audio track belongs.
//
// Rules:
//   1. Video tracks remain together.
//   2. Detached audio tracks remain together below the video group.
//   3. Their relative order mirrors their source video tracks.
//   4. Unrelated audio tracks are kept below the linked companion group.
//   5. Existing companion tracks are shifted down instead of receiving an
//      overlapping clip.
int audioTrackInsertIndexForVideoTrack(const drift::Project &project,
                                       int sourceVideoTrackIndex);

// Split embedded audio onto its own audio track (video keeps picture only).
//
// The audio clip is independently editable and receives a dedicated track
// whose relative position mirrors the source video track.
bool detachEmbeddedAudioFromVideo(drift::Project &project,
                                  AssetLibrary *library,
                                  int videoTrackIndex,
                                  int videoClipIndex);

QList<QPair<int, int>> selectionWithLinkedPartners(const drift::Project &project, int trackIndex, int clipIndex);

// A clip's effect stack does not live on the clip: it lives on an adjustment clip pinned to it by
// linkedClipId, which is a different mechanism from the linkId that pairs audio with video. Split
// handles linkId (splitLinkedPartnerAt above) and used to ignore this one, so the tail of a split
// came back with no grade at all. Clone each pinned adjustment onto the tail and carry its
// parameter curves across; syncLinkedAdjustments re-spans both halves afterwards.
// True when a catalog entry actually declares `key`. Writing a parameter an effect does not have
// used to succeed silently: the value went into the project file, the renderer ignored it, and the
// caller had no way to tell a typo from a working edit.
bool declaresParam(const QList<drift::EffectParamSpec> &parameters, const QString &key);

bool declaresParam(const drift::EffectPresetMeta &meta, const QString &key);

int clipIndexById(const drift::Track &track, const QString &id);

QString newClipId();


// The layered patch, then the flat keys projects before format 8 and older agents still send:
// fill / fillKind / fillSecondary / gradientAngle land on the front-most fill layer, stroke /
// strokeWidth / strokeStyle on the first stroke layer, either created when missing.
void applyShapeStylePatch(drift::ShapeStyle &s, const QVariantMap &m);

// The matte already pinned to a clip, or a default-constructed Mask when it has none. Masks live
// on the adjustments linked to the clip, so this has to go through the project rather than reading
// the clip on its own.
drift::Mask clipMatte(const drift::Project &project, int trackIndex, int clipIndex);

bool clipHasMatte(const drift::Project &project, int trackIndex, int clipIndex);
} // namespace drift::appcontroller_detail
