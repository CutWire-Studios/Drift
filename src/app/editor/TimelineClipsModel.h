#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QVariantList>

#include "core/animation/FadeShape.h"
#include "core/animation/Keyframe.h"

// One track's clips, exposed as roles.
//
// The timeline delegate used to read its clip out of AppController::tracks() — a QVariantList of
// nested QVariantMaps rebuilt in full on every edit, and deep-converted to JS in full every time
// QML read it. Both costs scale with the whole project, for an edit that touched one clip.
//
// Rows here are plain values that compare by field, so a rebuild notifies only the clips that
// actually changed, and only the roles that changed on them. Delegates bind to roles, which QML
// pulls per row on demand instead of converting the project up front.
class TimelineClipsModel : public QAbstractListModel
{
    Q_OBJECT
    // Per-track decorations derived from the clips. Computed in C++ once per edit, and only
    // re-announced when they actually differ, so the Repeaters over them do not rebuild on every
    // edit the way the JS-array models they replace did.
    Q_PROPERTY(QVariantList gaps READ gaps NOTIFY decorationsChanged)
    Q_PROPERTY(QVariantList transitionRegions READ transitionRegions NOTIFY decorationsChanged)
    Q_PROPERTY(QVariantList adjustmentLanes READ adjustmentLanes NOTIFY decorationsChanged)

public:
    // Role names are the clip-map keys the timeline strip already read, so TimelineClipItem's
    // `clipData.<key>` reads carry over unchanged.
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        PathRole,
        KindRole,
        AdjustmentKindRole,
        LinkedClipIdRole,
        LinkIdRole,
        LinkedRole,
        FilmstripPathRole,
        TextContentRole,
        RotationCorrectionRole,
        StartRole,
        DurationRole,
        InPointRole,
        OutPointRole,
        SourceDurationRole,
        AudioStreamIndexRole,
        HasEmbeddedAudioRole,
        FadeInRole,
        FadeOutRole,
        FadeCurveRole,
        FadeShapeRole,
        FadeHandlesRole,
        EffectsRole,
        AudioEffectsRole,
        VolumeKeysRole,
        LoopRole,
        LoopPeriodRole,
        LoopPhaseRole,
        HiddenInLaneRole,
        FxLayerRole,
    };
    Q_ENUM(Role)

    struct Row
    {
        QString id;
        QString name;
        QString path;
        QString kind;
        QString adjustmentKind;
        QString linkedClipId;
        QString linkId;
        bool linked = false;
        QString filmstripPath;
        QString textContent;
        int rotationCorrection = 0;
        double start = 0.0;
        double duration = 0.0;
        double inPoint = 0.0;
        double outPoint = 0.0;
        double sourceDuration = 0.0;
        int audioStreamIndex = 0;
        bool hasEmbeddedAudio = false;
        double fadeIn = 0.0;
        double fadeOut = 0.0;
        QString fadeCurve;
        QVariantList fadeShape;
        QVariantList fadeHandles;
        QVariantList effects;
        QVariantList audioEffects;
        QVariantList volumeKeys;
        // A looping clip: the timeline length of one pass through inPoint..outPoint, and how far
        // into the first pass the clip starts (both seconds, both 0 when not looping).
        bool loop = false;
        double loopPeriod = 0.0;
        double loopPhase = 0.0;
        // An adjustment tucked out of its lane (not drawn, not hit-tested).
        bool hiddenInLane = false;
        // On a media clip, its linked effects layer: 0 none with effects, 1 tucked away, 2 shown
        // in the lane.
        int fxLayer = 0;

        // Not roles: typed copies for the scene-graph renderer, derived from the fields above so
        // any change to them already arrives as a changed row.
        drift::FadeCurve fadeCurveTyped = drift::FadeCurve::Smooth;
        drift::FadeShape fadeShapeTyped;
        drift::KeyframeTrack<double> volumeTyped;
        // The effect stack as one line, "(off)" marks included; empty when there are none.
        QString effectsLabel;
        // "-6.0 dB" for a constant gain off unity; empty when keyframed or at unity.
        QString volumeLabel;
        // A procedural clip whose package is not in the catalog; it renders nothing but keeps its data.
        bool packageMissing = false;
    };

    // Empty time between two clips, in seconds.
    struct Gap
    {
        double start = 0.0;
        double end = 0.0;
        bool operator==(const Gap &) const = default;
    };

    // An overlap or transition between a clip and its transition partner, in seconds.
    struct TransitionRegion
    {
        int leftClip = -1;
        int rightClip = -1;
        double start = 0.0;
        double end = 0.0;
        // Touching clips: the cut. Overlapping clips: the midpoint of the overlap. Always set, so
        // a cut with no transition yet (start == end) still has a position.
        double cut = 0.0;
        bool hasTransition = false;
        // The clips physically overlap, so the window is the overlap and only its start can move.
        bool overlap = false;
        // Longest duration setTransitionDuration / a project reload will keep.
        double maxDuration = 0.0;
        QString transitionId;
        QString label;
        QString kind;
        bool operator==(const TransitionRegion &) const = default;
    };

    struct Decorations
    {
        QList<Gap> gaps;
        QList<TransitionRegion> transitions;
        QList<int> adjustmentLanes;
        bool operator==(const Decorations &) const = default;
    };

    explicit TimelineClipsModel(QObject *parent = nullptr);

    // Replaces the contents, diffed by clip id. Clips kept in the same relative order update in
    // place (dataChanged, only the differing roles); clips that appear or disappear are inserted
    // or removed around them. Only a reorder resets.
    void setRows(QList<Row> rows);
    const QList<Row> &rows() const { return m_rows; }

    void setDecorations(Decorations decorations);
    const Decorations &decorations() const { return m_decorations; }
    QVariantList gaps() const;
    QVariantList transitionRegions() const;
    QVariantList adjustmentLanes() const;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void decorationsChanged();

private:
    QList<Row> m_rows;
    Decorations m_decorations;
};
