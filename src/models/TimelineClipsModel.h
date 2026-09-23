#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QVariantList>

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
    };

    explicit TimelineClipsModel(QObject *parent = nullptr);

    // Replaces the contents. Same clips in the same order (compared by id) updates in place and
    // emits dataChanged for the differing rows only; anything else is a reset.
    void setRows(QList<Row> rows);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    QList<Row> m_rows;
};
