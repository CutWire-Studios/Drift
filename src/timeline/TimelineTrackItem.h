#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QPointer>
#include <QQuickItem>
#include <QSGGeometry>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QTimer>

#include "TimelineLayout.h"
#include "TimelineViewState.h"

class AppController;
class AssetLibrary;
class Haptics;
class TimelineClipsModel;

// The clips of one track, filtered to those that need a QML overlay right now: the selected ones
// (trim handles, fade dots, the context menu) and, with a pointer, the one under it (so its trim
// strips can be approached). Everything else is drawn by TimelineTrackItem alone.
class ActiveClipsModel : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    enum { SourceIndexRole = Qt::UserRole + 1000 };

    explicit ActiveClipsModel(QObject *parent = nullptr);

    void setSourceClips(TimelineClipsModel *model);
    void setActiveIds(const QSet<QString> &ids);
    const QSet<QString> &activeIds() const { return m_ids; }

    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    void announceSourceIndexes();

    QSet<QString> m_ids;
};

// One track row's clips, drawn straight into the scene graph.
//
// Replaces a QML delegate per clip — ~45 objects and a few hundred bindings each, all of them
// alive for every clip in the project whether on screen or not. This builds nodes for the clips
// inside the viewport only, rebuilds them when something they show actually changed, and handles
// the body gestures (select, move, context menu) itself. Trim handles and fade dots stay in QML,
// on an overlay that only exists for the clips in activeClips.
class TimelineTrackItem : public QQuickItem
{
    Q_OBJECT

    Q_PROPERTY(TimelineViewState *viewState READ viewState WRITE setViewState NOTIFY viewStateChanged)
    Q_PROPERTY(QObject *clipsModel READ clipsModel WRITE setClipsModel NOTIFY clipsModelChanged)
    Q_PROPERTY(int trackIndex READ trackIndex WRITE setTrackIndex NOTIFY trackChanged)
    Q_PROPERTY(QString trackType READ trackType WRITE setTrackType NOTIFY trackChanged)
    Q_PROPERTY(int clipDisplay READ clipDisplay WRITE setClipDisplay NOTIFY trackChanged)
    Q_PROPERTY(bool showChannelWaveforms READ showChannelWaveforms WRITE setShowChannelWaveforms NOTIFY trackChanged)
    Q_PROPERTY(QObject *editor READ editor WRITE setEditor NOTIFY servicesChanged)
    Q_PROPERTY(QObject *assets READ assets WRITE setAssets NOTIFY servicesChanged)
    Q_PROPERTY(QObject *haptics READ haptics WRITE setHaptics NOTIFY servicesChanged)
    Q_PROPERTY(QAbstractItemModel *activeClips READ activeClips CONSTANT)
    Q_PROPERTY(bool dragging READ dragging NOTIFY draggingChanged)
    Q_PROPERTY(bool touchDragging READ touchDragging NOTIFY draggingChanged)
    // Touch only: a clip is picked up (held, or being dragged). The panel stops panning for it.
    Q_PROPERTY(bool lifted READ lifted NOTIFY draggingChanged)
    Q_PROPERTY(QPointF dragPointer READ dragPointer NOTIFY dragPointerChanged)
    Q_PROPERTY(QString hoverToolTip READ hoverToolTip NOTIFY hoverToolTipChanged)
    Q_PROPERTY(QRectF hoverToolTipRect READ hoverToolTipRect NOTIFY hoverToolTipChanged)

public:
    explicit TimelineTrackItem(QQuickItem *parent = nullptr);
    ~TimelineTrackItem() override;

    TimelineViewState *viewState() const { return m_viewState; }
    void setViewState(TimelineViewState *state);
    QObject *clipsModel() const;
    void setClipsModel(QObject *model);
    int trackIndex() const { return m_trackIndex; }
    void setTrackIndex(int index);
    QString trackType() const { return m_trackType; }
    void setTrackType(const QString &type);
    int clipDisplay() const { return m_clipDisplay; }
    void setClipDisplay(int display);
    bool showChannelWaveforms() const { return m_showChannelWaveforms; }
    void setShowChannelWaveforms(bool show);
    QObject *editor() const;
    void setEditor(QObject *editor);
    QObject *assets() const;
    void setAssets(QObject *assets);
    QObject *haptics() const;
    void setHaptics(QObject *haptics);
    QAbstractItemModel *activeClips() const { return m_activeClips; }
    bool dragging() const { return m_drag.moving; }
    bool touchDragging() const;
    bool lifted() const;
    QPointF dragPointer() const { return m_drag.pointer; }
    QString hoverToolTip() const { return m_hoverToolTip; }
    QRectF hoverToolTipRect() const { return m_hoverToolTipRect; }

    // The overlay's trim preview. Trims never touch the project until release, so the body has to
    // be told where the edge currently is.
    Q_INVOKABLE void setLivePreview(const QString &clipId, bool active, double start,
                                    double duration, double inPoint, double outPoint);
    // Re-applies the drag at the last pointer position, after the view scrolled under it.
    Q_INVOKABLE void refreshDrag();
    // What the panel's drop rules need about a clip: id, start, duration, kind.
    Q_INVOKABLE QVariantMap clipInfo(int index) const;

signals:
    void viewStateChanged();
    void clipsModelChanged();
    void trackChanged();
    void servicesChanged();
    void draggingChanged();
    void dragPointerChanged();
    void hoverToolTipChanged();

    // Body gestures that need the panel's policy (landing preview, cross-track drop rules).
    // Rects are the dragged clip's, in this item's coordinates.
    void moveStarted(int clipIndex);
    void moveUpdated(int clipIndex, const QRectF &rect, qreal dx, qreal dy);
    void moveFinished(int clipIndex, const QRectF &rect);
    void moveCanceled();
    void contextMenuRequested(int clipIndex);
    void toggleSelectionRequested(int clipIndex);

protected:
    void updatePolish() override;
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;
    void releaseResources() override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    bool contains(const QPointF &point) const override;

    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseUngrabEvent() override;
    void hoverEnterEvent(QHoverEvent *event) override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void hoverLeaveEvent(QHoverEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

public:
    struct Label
    {
        QString text;
        QPointF origin; // top-left of the layout, relative to the node origin
        double pixelSize = 10.0;
        QColor color;
        bool bold = false;
    };
    struct Tile
    {
        QString key;
        QImage image;
        QRectF target;
        QRectF source;
    };

private:
    struct ClipVisual
    {
        int index = -1;
        QString id;
        QRectF rect;
        bool selected = false;
        QList<QPair<QRectF, QString>> toolTips;
    };

    struct DragState
    {
        bool pressed = false;
        bool moving = false;
        bool armed = false;
        bool didDrag = false;
        bool rightButton = false;
        int clipIndex = -1;
        QString clipId;
        QRectF pressRect;
        QPointF pressItemPos;
        QPointF lastScenePos;
        QPointF pointer;
        QRectF rect;
        double minX = 0.0;
        double minY = 0.0;
        double maxY = 0.0;
        bool wasSelected = false;
    };

    struct LivePreview
    {
        double start = 0.0;
        double duration = 0.0;
        double inPoint = 0.0;
        double outPoint = 0.0;
    };

    struct WaveCacheEntry
    {
        QString key;
        QVector<float> peaks;       // merged lane
        QList<QVector<float>> lanes; // per channel, when split
        QStringList names;
    };

    void scheduleRebuild();
    void onViewChanged();
    void updateActiveSet();
    void build();
    void appendClip(int index, const QRectF &rect, bool selected, bool lit, bool lifted);
    void buildWaveform(int index, const QRectF &area, double clipX, double clipWidth,
                       double inPoint, double outPoint, bool bar);
    void buildFilmstrip(int index, const QRectF &body, double inPoint, double outPoint);
    QRectF effectiveRect(int index, double *inPoint = nullptr, double *outPoint = nullptr) const;
    int clipAt(const QPointF &point, timelinelayout::Zone *zone = nullptr) const;
    double edgeMargin(double clipWidth) const;
    double hotspotExtra() const;
    bool isSelected(int index) const;
    void setHovered(const QString &clipId, bool body, const QPointF &point);
    void beginMove();
    void applyDrag(const QPointF &scenePos);
    void finishPress(bool canceled);
    void setToolTip(const QString &text, const QRectF &rect);
    void callHaptic(const char *method);

    QPointer<TimelineViewState> m_viewState;
    QPointer<TimelineClipsModel> m_clips;
    QPointer<AppController> m_editor;
    QPointer<AssetLibrary> m_assets;
    QPointer<QObject> m_haptics;
    ActiveClipsModel *m_activeClips = nullptr;
    int m_trackIndex = -1;
    QString m_trackType;
    int m_clipDisplay = 1;
    bool m_showChannelWaveforms = false;

    timelinelayout::Window m_window;
    double m_builtPxPerSecond = 0.0;
    QHash<QString, LivePreview> m_livePreviews;
    QHash<QString, WaveCacheEntry> m_waveCache;
    QString m_hoverId;
    bool m_hoverBody = false;
    QString m_hoverToolTip;
    QRectF m_hoverToolTipRect;
    DragState m_drag;
    QTimer m_longPress;

    // Built on the GUI thread in updatePolish, consumed by updatePaintNode. Coordinates are
    // relative to m_originX so vertices stay small on a long timeline.
    double m_originX = 0.0;
    QVector<QSGGeometry::ColoredPoint2D> m_under;
    QVector<QSGGeometry::ColoredPoint2D> m_over;
    QVector<Tile> m_tiles;
    QVector<Label> m_labels;
    QList<ClipVisual> m_visible;
    QString m_fontFamily;
    int m_frameCounter = 0;
};
