#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QVariantList>

#include "core/FadeShape.h"

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

        // Not roles: typed copies for the scene-graph renderer, derived from the fields above so
        // any change to them already arrives as a changed row.
        drift::FadeCurve fadeCurveTyped = drift::FadeCurve::Smooth;
        drift::FadeShape fadeShapeTyped;
        // The effect stack as one line, "(off)" marks included; empty when there are none.
        QString effectsLabel;
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
        double start = 0.0;
        double end = 0.0;
        bool hasTransition = false;
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
