#pragma once

#include "core/base/Time.h"

#include <QImage>
#include <QObject>
#include <QPair>
#include <QPointF>
#include <QSize>
#include <QString>
#include <QThreadPool>
#include <QVariant>
#include <QVariantList>
#include <QVector>

class AppController;

namespace drift {
struct Mask;
struct MediaAsset;
}

// "Use as mask" for the media bin and the timeline, reached from QML as EditorState.masking. A bin
// asset flagged as a matte source becomes a media mask when dropped on a clip or on a track's empty
// space; a timeline picture clip can be converted into a matte for the video track below it. Every
// op is one undo step.
class MaskingController : public QObject
{
    Q_OBJECT

    // Brush settings for the session; not saved with the project.
    // Size is the diameter as a fraction of the frame width.
    Q_PROPERTY(double brushSize READ brushSize WRITE setBrushSize NOTIFY brushSettingsChanged)
    Q_PROPERTY(double brushHardness READ brushHardness WRITE setBrushHardness NOTIFY brushSettingsChanged)
    Q_PROPERTY(double brushOpacity READ brushOpacity WRITE setBrushOpacity NOTIFY brushSettingsChanged)
    Q_PROPERTY(bool brushErase READ brushErase WRITE setBrushErase NOTIFY brushSettingsChanged)

public:
    explicit MaskingController(AppController *app, QObject *parent = nullptr);
    ~MaskingController() override;

    double brushSize() const { return m_brushSize; }
    void setBrushSize(double size);
    double brushHardness() const { return m_brushHardness; }
    void setBrushHardness(double hardness);
    double brushOpacity() const { return m_brushOpacity; }
    void setBrushOpacity(double opacity);
    bool brushErase() const { return m_brushErase; }
    void setBrushErase(bool erase);

    Q_INVOKABLE QString assetMaskRole(int assetIndex) const;
    Q_INVOKABLE void setAssetMaskRole(int assetIndex, const QString &role);

    Q_INVOKABLE void addAssetMaskToClip(int trackIndex, int clipIndex, int assetIndex);
    Q_INVOKABLE void addAssetLaneMask(int trackIndex, int assetIndex, double atSeconds);
    Q_INVOKABLE bool canConvertClipToMask(int trackIndex, int clipIndex) const;
    Q_INVOKABLE void convertClipToMask(int trackIndex, int clipIndex);

    // How long a lane matte from this asset lasts: a video's own length, else a still's default.
    drift::TimeUs assetMaskDurationUs(int assetIndex) const;

    // What the Masking panel edits for the current selection:
    // {"kind": "clip"|"adjustment"|"none", "track": int, "clip": int, "name": QString}
    // - selected media (non-adjustment) clip -> kind "clip", itself
    // - selected Mask-kind adjustment on a lane -> kind "clip": if it has linkedClipId, the clip with that id
    //   (search the parent track = adjustmentLaneParentIndex); otherwise the parent track's non-adjustment
    //   clip under the playhead (m_app->m_playheadUs, Clip::containsTime); "none" if there is none
    // - selected adjustment on a standalone adjustment track (Mask or VideoEffects kind) -> kind "adjustment", itself
    // - anything else / no selection -> kind "none"
    Q_INVOKABLE QVariantMap maskTarget() const;

    // The masks that apply to the target, in stack order. For kind "clip": every Mask-kind adjustment
    // on the clip's track's lanes that is either pinned to it (linkedClipId == clip.id) or unpinned and
    // overlapping the clip's [timelineStart, timelineEnd). For kind "adjustment": the clip itself if its
    // mask.shape != None. Each entry: {track, clip, id, name, shape, op, invert, enabled, feather,
    // mediaChannel, isMedia, pinned, pathAnimated (mask.pathKeys non-empty), pathKeyCount, selected (== current selection)}.
    Q_INVOKABLE QVariantList maskStack(int trackIndex, int clipIndex) const;

    // Draws a new parametric mask on the target. kind "clip": addLinkedMask, then select the new
    // adjustment (m_app->selectClipById). kind "adjustment": replace its mask via writeClipMask
    // (keep the adjustment selected). shape: "rectangle" | "ellipse". w/h clamped to >= 0.01.
    // One undo step tr("Draw mask").
    Q_INVOKABLE void addShapeMask(int trackIndex, int clipIndex, const QString &shape, double x, double y,
                                  double w, double h);

    // Creates a Freeform mask from >= 3 normalized vertices (QVariantList of {x, y} maps) on the
    // target, exactly like addShapeMask does for kinds "clip" / "adjustment" (clip: addLinkedMask +
    // select new adjustment; adjustment: assign its mask directly). Undo text tr("Draw polygon mask").
    Q_INVOKABLE void addPolygonMask(int trackIndex, int clipIndex, const QVariantList &points);

    // Paints one stroke (normalized {x,y} points in the target clip's frame) into the target's brush
    // matte and saves it. `erase` true removes coverage (right-button strokes pass true regardless of
    // brushErase). Target kind must be "clip" (a media clip); otherwise no-op.
    // If the currently SELECTED clip is a Mask adjustment pinned to this clip whose mask isMedia() and
    // whose mediaPath ends with ".png" and lies inside matteCacheDir(), the stroke paints into that
    // mask (its image is the starting point). Otherwise the first stroke creates a new mask:
    // fullFrameMediaMask(newPath), name tr("Brush"), added with addLinkedMask, and the new
    // adjustment gets selected. Updating an existing brush mask sets its mediaPath to the new file.
    // One undo step per stroke: tr("Brush stroke").
    Q_INVOKABLE void brushStroke(int trackIndex, int clipIndex, const QVariantList &points, bool erase);
    // Fill the selected brush mask entirely (coverage 1) or clear it (0); same target rules, creates
    // the mask if needed. Undo tr("Fill mask") / tr("Clear mask").
    Q_INVOKABLE void brushFill(int trackIndex, int clipIndex, bool clear);
    // Test hook: waits until every queued brush save has been applied to the project.
    bool waitForBrushSaves(int timeoutMs = 5000);

    // Path keys of the mask on adjustment clip (track, clip), as seconds on the TIMELINE
    // (adjustment.timelineStart + key time), ascending.
    Q_INVOKABLE QVariantList maskPathKeyTimes(int trackIndex, int clipIndex) const;

    // Turns shape animation on: adds the first key at the playhead holding the mask's current
    // points (resolved at the playhead). Off: clears pathKeys and keeps the points resolved at the
    // playhead as the static shape. One undo step each (tr("Animate mask shape") / tr("Stop animating mask shape")).
    Q_INVOKABLE void setMaskPathAnimated(int trackIndex, int clipIndex, bool animated);

    // Writes the vertex list as a key at timeline time `atSeconds` (converted to mask time =
    // secondsToUs(atSeconds) - adjustment.timelineStart). If an existing key lies within half a frame,
    // that key is replaced instead of adding a new one. Also sets mask.points to the same list when the
    // mask has only that one key (so the static shape and the single key agree).
    Q_INVOKABLE void setMaskPathKey(int trackIndex, int clipIndex, double atSeconds, const QVariantList &points);
    // Same, as a live drag: preview edit pattern, committed by EditorState.commitPreviewDrag().
    Q_INVOKABLE void previewSetMaskPathKey(int trackIndex, int clipIndex, double atSeconds,
                                           const QVariantList &points);
    // Removes the key within half a frame of atSeconds.
    Q_INVOKABLE void removeMaskPathKey(int trackIndex, int clipIndex, double atSeconds);

    // Edits one property of the mask on adjustment clip (track, clip). key is one of {"name","op","invert",
    // "enabled","feather","mediaChannel"}; anything else is ignored. One undo step tr("Edit mask").
    Q_INVOKABLE void setMaskProperty(int trackIndex, int clipIndex, const QString &key, const QVariant &value);
    // Same, as a live drag (feather slider): begins a preview drag if none is active, writes, emits a
    // preview frame. QML calls EditorState.commitPreviewDrag() on release.
    Q_INVOKABLE void previewSetMaskProperty(int trackIndex, int clipIndex, const QString &key, const QVariant &value);

    // Mask-kind adjustment: removes the clip (one undo step tr("Delete mask")). VideoEffects
    // adjustment: sets its mask shape to None via writeClipMask.
    Q_INVOKABLE void removeMask(int trackIndex, int clipIndex);

signals:
    void brushSettingsChanged();
    // One per brush save, in stroke order, whether it succeeded or not.
    void brushApplied(const QString &clipId);

private:
    const drift::MediaAsset *assetAt(int assetIndex) const;
    // The brush matte's pixel size for a clip: its asset's size (else the canvas), longest side <= 2048.
    QSize brushMatteSize(int trackIndex, int clipIndex) const;
    // The (track, clip) of the selected brush mask pinned to the target, or {-1, -1}.
    QPair<int, int> selectedBrushMask(int trackIndex, int clipIndex) const;
    // The matte to paint on: the selected brush mask's image, else a black one of the matte's size.
    QImage brushImage(int trackIndex, int clipIndex) const;
    // Saves `image` as a new matte file on m_brushPool and then points the selected brush mask at it, or
    // creates the mask (finishBrushSave).
    void commitBrushMatte(int trackIndex, int clipIndex, const QImage &image, const QString &undoText);
    void finishBrushSave(bool ok, const QString &path, const QString &clipId, const QString &undoText);
    bool isBrushTarget(int trackIndex, int clipIndex) const;
    // The mask label: its own name, else a human label for its shape.
    QString maskLabel(const drift::Mask &mask) const;
    bool canEditMask(int trackIndex, int clipIndex, const QString &key) const;
    void applyMaskProperty(int trackIndex, int clipIndex, const QString &key, const QVariant &value);
    // An adjustment whose mask is a Freeform: the only thing the polygon operations act on.
    bool isPolygonAdjustment(int trackIndex, int clipIndex) const;
    // Half a frame at the project rate: the tolerance for "the same key".
    drift::TimeUs halfFrameUs() const;

    AppController *m_app = nullptr;

    double m_brushSize = 0.05;
    double m_brushHardness = 0.7;
    double m_brushOpacity = 1.0;
    bool m_brushErase = false;
    // The matte last painted or loaded, and the file it was (or is being) saved to. Mattes are never
    // rewritten in place, so a path match means the image is still current. While saves are in flight
    // the project lags m_brushImage, so brushImage() trusts m_brushImage for m_brushClipId instead.
    QImage m_brushImage;
    QString m_brushPendingPath;
    QString m_brushClipId;
    int m_brushSavesInFlight = 0;
    // The newest save failed: m_brushImage holds strokes no file has, and the next stroke's save retries them.
    bool m_brushSaveFailed = false;
    // Single thread: saves finish in stroke order, so the first commit creates the mask and later ones update it.
    QThreadPool m_brushPool;
};

namespace drift::masking {

// Stamps a stroke of round dabs onto `image`: white paints, black erases. `hardness` is the fraction
// of the radius that stays fully opaque.
void paintBrushStroke(QImage &image, const QVector<QPointF> &pixelPoints, double radiusPx, double hardness,
                      double opacity, bool erase);

} // namespace drift::masking
