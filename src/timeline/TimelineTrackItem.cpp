#include "TimelineTrackItem.h"

#include "ClipThumbnailStore.h"
#include "TimelineViewState.h"
#include "models/AppController.h"
#include "models/AssetLibrary.h"
#include "models/TimelineClipsModel.h"
#include "playback/PlaybackEngine.h"

#include <QAccessible>
#include <QAccessibleObject>
#include <QFont>
#include <QFontMetricsF>
#include <QCursor>
#include <QGuiApplication>
#include <QMatrix4x4>
#include <QQuickWindow>
#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>
#include <QSGImageNode>
#include <QSGTextNode>
#include <QSGTransformNode>
#include <QSGVertexColorMaterial>
#include <QStyleHints>
#include <QTextLayout>

#include <cmath>

using timelinelayout::Zone;

namespace {

using Vertex = QSGGeometry::ColoredPoint2D;

constexpr int kCornerSegments = 5;
constexpr double kWaveformWindowMax = 4096.0;
constexpr double kFilmstripTileWidth = 120.0;
constexpr int kFilmstripFrameCount = 8;
constexpr int kMaxCachedTextures = 160;

Vertex vertex(double x, double y, const QColor &c, double alphaScale = 1.0)
{
    const double a = c.alphaF() * alphaScale;
    Vertex v;
    v.set(float(x), float(y), uchar(std::lround(c.redF() * a * 255.0)),
          uchar(std::lround(c.greenF() * a * 255.0)), uchar(std::lround(c.blueF() * a * 255.0)),
          uchar(std::lround(a * 255.0)));
    return v;
}

void appendTriangle(QVector<Vertex> &out, const Vertex &a, const Vertex &b, const Vertex &c)
{
    out.append(a);
    out.append(b);
    out.append(c);
}

void appendQuad(QVector<Vertex> &out, const Vertex &tl, const Vertex &tr, const Vertex &br,
                const Vertex &bl)
{
    appendTriangle(out, tl, tr, br);
    appendTriangle(out, tl, br, bl);
}

void appendRect(QVector<Vertex> &out, const QRectF &r, const QColor &c, double ox)
{
    if (r.width() <= 0.0 || r.height() <= 0.0 || c.alpha() == 0)
        return;
    const double l = r.left() - ox;
    const double rr = r.right() - ox;
    appendQuad(out, vertex(l, r.top(), c), vertex(rr, r.top(), c), vertex(rr, r.bottom(), c),
               vertex(l, r.bottom(), c));
}

// Clockwise outline of a rounded rect.
QVector<QPointF> roundedPolygon(const QRectF &r, double radius)
{
    radius = std::clamp(radius, 0.0, std::min(r.width(), r.height()) / 2.0);
    QVector<QPointF> pts;
    if (radius < 0.5) {
        pts = {r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft()};
        return pts;
    }
    const QPointF centers[4] = {
        {r.right() - radius, r.top() + radius},
        {r.right() - radius, r.bottom() - radius},
        {r.left() + radius, r.bottom() - radius},
        {r.left() + radius, r.top() + radius},
    };
    // Start angles, clockwise in screen space (y down): top-right corner sweeps from -90 to 0.
    const double starts[4] = {-M_PI / 2.0, 0.0, M_PI / 2.0, M_PI};
    pts.reserve(4 * (kCornerSegments + 1));
    for (int c = 0; c < 4; ++c) {
        for (int i = 0; i <= kCornerSegments; ++i) {
            const double a = starts[c] + (M_PI / 2.0) * i / kCornerSegments;
            pts.append({centers[c].x() + radius * std::cos(a), centers[c].y() + radius * std::sin(a)});
        }
    }
    return pts;
}

// Convex fill with a half-pixel feathered edge, which is all the antialiasing a clip corner needs.
void appendConvex(QVector<Vertex> &out, const QVector<QPointF> &pts, const QColor &c, double ox)
{
    const int n = pts.size();
    if (n < 3 || c.alpha() == 0)
        return;
    QPointF center;
    for (const QPointF &p : pts)
        center += p;
    center /= n;

    QVector<QPointF> inner(n);
    QVector<QPointF> outer(n);
    for (int i = 0; i < n; ++i) {
        QPointF d = pts[i] - center;
        const double len = std::hypot(d.x(), d.y());
        if (len > 1e-6)
            d /= len;
        inner[i] = pts[i] - d * 0.5;
        outer[i] = pts[i] + d * 0.5;
    }
    const Vertex vc = vertex(center.x() - ox, center.y(), c);
    for (int i = 0; i < n; ++i) {
        const int j = (i + 1) % n;
        const Vertex ai = vertex(inner[i].x() - ox, inner[i].y(), c);
        const Vertex aj = vertex(inner[j].x() - ox, inner[j].y(), c);
        appendTriangle(out, vc, ai, aj);
        const Vertex bi = vertex(outer[i].x() - ox, outer[i].y(), c, 0.0);
        const Vertex bj = vertex(outer[j].x() - ox, outer[j].y(), c, 0.0);
        appendQuad(out, ai, aj, bj, bi);
    }
}

void appendRoundedRect(QVector<Vertex> &out, const QRectF &r, double radius, const QColor &c,
                       double ox)
{
    if (r.width() <= 0.0 || r.height() <= 0.0)
        return;
    appendConvex(out, roundedPolygon(r, radius), c, ox);
}

// A border drawn inside the rect, like a QML Rectangle's.
void appendOutline(QVector<Vertex> &out, const QRectF &r, double radius, double width,
                   const QColor &c, double ox)
{
    if (width <= 0.0 || r.width() <= 2.0 * width || r.height() <= 2.0 * width)
        return;
    const QVector<QPointF> o = roundedPolygon(r, radius);
    const QVector<QPointF> i =
        roundedPolygon(r.adjusted(width, width, -width, -width), std::max(0.0, radius - width));
    if (o.size() != i.size())
        return;
    const int n = o.size();
    for (int k = 0; k < n; ++k) {
        const int j = (k + 1) % n;
        appendQuad(out, vertex(o[k].x() - ox, o[k].y(), c), vertex(o[j].x() - ox, o[j].y(), c),
                   vertex(i[j].x() - ox, i[j].y(), c), vertex(i[k].x() - ox, i[k].y(), c));
    }
}

// A polyline stroked as quads; GL line widths above 1 are not portable.
void appendStroke(QVector<Vertex> &out, const QVector<QPointF> &pts, double width,
                  const QColor &c, double ox)
{
    const double half = width / 2.0;
    for (int k = 0; k + 1 < pts.size(); ++k) {
        const QPointF a = pts[k];
        const QPointF b = pts[k + 1];
        QPointF d = b - a;
        const double len = std::hypot(d.x(), d.y());
        if (len < 1e-6)
            continue;
        const QPointF nrm(-d.y() / len * half, d.x() / len * half);
        appendQuad(out, vertex(a.x() + nrm.x() - ox, a.y() + nrm.y(), c),
                   vertex(b.x() + nrm.x() - ox, b.y() + nrm.y(), c),
                   vertex(b.x() - nrm.x() - ox, b.y() - nrm.y(), c),
                   vertex(a.x() - nrm.x() - ox, a.y() - nrm.y(), c));
    }
}

QFont labelFont(const QString &family, double pixelSize, bool bold)
{
    QFont font(family);
    // QML's font.pixelSize is an int; the theme's fractional sizes truncate there too.
    font.setPixelSize(qMax(1, int(pixelSize)));
    if (bold)
        font.setWeight(QFont::DemiBold);
    return font;
}

QString labelKey(const TimelineTrackItem::Label &l)
{
    return QStringLiteral("%1|%2|%3|%4|%5|%6")
        .arg(l.text)
        .arg(qRound(l.origin.x() * 4))
        .arg(qRound(l.origin.y() * 4))
        .arg(l.pixelSize)
        .arg(l.color.rgba())
        .arg(l.bold);
}

// Crops `source` to the aspect of `target` about its centre, like Image.PreserveAspectCrop.
QRectF aspectCrop(const QRectF &source, const QSizeF &target)
{
    if (source.isEmpty() || target.isEmpty())
        return source;
    const double sa = source.width() / source.height();
    const double ta = target.width() / target.height();
    if (sa > ta) {
        const double w = source.height() * ta;
        return QRectF(source.center().x() - w / 2.0, source.top(), w, source.height());
    }
    const double h = source.width() / ta;
    return QRectF(source.left(), source.center().y() - h / 2.0, source.width(), h);
}

// Clips `target` to `bounds` and trims the matching share off `source`.
bool clipTile(QRectF &target, QRectF &source, const QRectF &bounds)
{
    const QRectF clipped = target.intersected(bounds);
    if (clipped.isEmpty())
        return false;
    const double sx = source.width() / target.width();
    const double sy = source.height() / target.height();
    source = QRectF(source.left() + (clipped.left() - target.left()) * sx,
                    source.top() + (clipped.top() - target.top()) * sy, clipped.width() * sx,
                    clipped.height() * sy);
    target = clipped;
    return true;
}

class TrackRootNode : public QSGTransformNode
{
public:
    TrackRootNode()
    {
        under = makeGeometryNode();
        appendChildNode(under);
        tiles = new QSGNode;
        appendChildNode(tiles);
        over = makeGeometryNode();
        appendChildNode(over);
        texts = new QSGNode;
        appendChildNode(texts);
    }
    ~TrackRootNode() override { qDeleteAll(textures); }

    static QSGGeometryNode *makeGeometryNode()
    {
        auto *node = new QSGGeometryNode;
        auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
        geometry->setDrawingMode(QSGGeometry::DrawTriangles);
        node->setGeometry(geometry);
        node->setMaterial(new QSGVertexColorMaterial);
        node->setFlags(QSGNode::OwnsGeometry | QSGNode::OwnsMaterial);
        return node;
    }

    static void fill(QSGGeometryNode *node, const QVector<Vertex> &vertices)
    {
        QSGGeometry *g = node->geometry();
        g->allocate(vertices.size());
        if (!vertices.isEmpty())
            std::memcpy(g->vertexDataAsColoredPoint2D(), vertices.constData(),
                        size_t(vertices.size()) * sizeof(Vertex));
        node->markDirty(QSGNode::DirtyGeometry);
    }

    QSGGeometryNode *under = nullptr;
    QSGNode *tiles = nullptr;
    QSGGeometryNode *over = nullptr;
    QSGNode *texts = nullptr;
    QHash<QString, QSGTexture *> textures;
    QHash<QString, int> textureStamps;
    QList<QSGImageNode *> images;
    QHash<QString, QSGTextNode *> labels;
};

} // namespace

// --- ActiveClipsModel ----------------------------------------------------------------------------

ActiveClipsModel::ActiveClipsModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
}

void ActiveClipsModel::setSourceClips(TimelineClipsModel *model)
{
    if (sourceModel())
        disconnect(sourceModel(), nullptr, this, nullptr);
    setSourceModel(model);
    if (!model)
        return;
    // A clip inserted or removed ahead of an active one shifts its index without changing the
    // row the overlay is bound to, so say so explicitly.
    connect(model, &QAbstractItemModel::rowsInserted, this, &ActiveClipsModel::announceSourceIndexes);
    connect(model, &QAbstractItemModel::rowsRemoved, this, &ActiveClipsModel::announceSourceIndexes);
    connect(model, &QAbstractItemModel::rowsMoved, this, &ActiveClipsModel::announceSourceIndexes);
    connect(model, &QAbstractItemModel::modelReset, this, &ActiveClipsModel::announceSourceIndexes);
}

void ActiveClipsModel::setActiveIds(const QSet<QString> &ids)
{
    if (ids == m_ids)
        return;
    beginFilterChange();
    m_ids = ids;
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
}

QVariant ActiveClipsModel::data(const QModelIndex &index, int role) const
{
    if (role == SourceIndexRole)
        return mapToSource(index).row();
    return QSortFilterProxyModel::data(index, role);
}

QHash<int, QByteArray> ActiveClipsModel::roleNames() const
{
    QHash<int, QByteArray> roles = QSortFilterProxyModel::roleNames();
    roles.insert(SourceIndexRole, "sourceIndex");
    return roles;
}

bool ActiveClipsModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const QModelIndex idx = sourceModel()->index(sourceRow, 0, sourceParent);
    return m_ids.contains(idx.data(TimelineClipsModel::IdRole).toString());
}

void ActiveClipsModel::announceSourceIndexes()
{
    if (rowCount() > 0)
        emit dataChanged(index(0, 0), index(rowCount() - 1, 0), {SourceIndexRole});
}

// --- TimelineTrackItem ---------------------------------------------------------------------------

TimelineTrackItem::TimelineTrackItem(QQuickItem *parent)
    : QQuickItem(parent)
    , m_activeClips(new ActiveClipsModel(this))
{
    setFlag(ItemHasContents);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
    setAcceptHoverEvents(true);

    m_longPress.setSingleShot(true);
    connect(&m_longPress, &QTimer::timeout, this, [this] {
        if (!m_drag.pressed || m_drag.moving || m_drag.didDrag || m_drag.rightButton)
            return;
        if (!m_viewState || !m_viewState->touchMode() || m_viewState->multiSelectActive())
            return;
        m_drag.armed = true;
        // The Flickable may no longer steal this gesture: the clip is picked up.
        setKeepMouseGrab(true);
        setKeepTouchGrab(true);
        callHaptic("pickUp");
        if (m_editor && !isSelected(m_drag.clipIndex))
            m_editor->selectClip(m_trackIndex, m_drag.clipIndex);
        emit draggingChanged();
        scheduleRebuild();
    });

    connect(&ClipThumbnailStore::instance(), &ClipThumbnailStore::imageReady, this,
            [this](const QString &) { scheduleRebuild(); });
}

TimelineTrackItem::~TimelineTrackItem() = default;

void TimelineTrackItem::setViewState(TimelineViewState *state)
{
    if (m_viewState == state)
        return;
    if (m_viewState)
        disconnect(m_viewState, nullptr, this, nullptr);
    m_viewState = state;
    if (m_viewState) {
        connect(m_viewState, &TimelineViewState::viewChanged, this, &TimelineTrackItem::onViewChanged);
        connect(m_viewState, &TimelineViewState::gestureChanged, this,
                &TimelineTrackItem::scheduleRebuild);
        connect(m_viewState, &TimelineViewState::styleChanged, this,
                &TimelineTrackItem::scheduleRebuild);
        setAcceptHoverEvents(!m_viewState->touchMode());
    }
    emit viewStateChanged();
    scheduleRebuild();
}

QObject *TimelineTrackItem::clipsModel() const
{
    return m_clips.data();
}

void TimelineTrackItem::setClipsModel(QObject *model)
{
    auto *clips = qobject_cast<TimelineClipsModel *>(model);
    if (m_clips == clips)
        return;
    if (m_clips)
        disconnect(m_clips, nullptr, this, nullptr);
    m_clips = clips;
    m_activeClips->setSourceClips(clips);
    if (m_clips) {
        const auto rebuild = [this] {
            updateActiveSet();
            scheduleRebuild();
        };
        connect(m_clips, &QAbstractItemModel::dataChanged, this, rebuild);
        connect(m_clips, &QAbstractItemModel::rowsInserted, this, rebuild);
        connect(m_clips, &QAbstractItemModel::rowsRemoved, this, rebuild);
        connect(m_clips, &QAbstractItemModel::modelReset, this, rebuild);
    }
    emit clipsModelChanged();
    updateActiveSet();
    scheduleRebuild();
}

void TimelineTrackItem::setTrackIndex(int index)
{
    if (m_trackIndex == index)
        return;
    m_trackIndex = index;
    emit trackChanged();
    updateActiveSet();
    scheduleRebuild();
}

void TimelineTrackItem::setTrackType(const QString &type)
{
    if (m_trackType == type)
        return;
    m_trackType = type;
    emit trackChanged();
    scheduleRebuild();
}

void TimelineTrackItem::setClipDisplay(int display)
{
    if (m_clipDisplay == display)
        return;
    m_clipDisplay = display;
    emit trackChanged();
    scheduleRebuild();
}

void TimelineTrackItem::setShowChannelWaveforms(bool show)
{
    if (m_showChannelWaveforms == show)
        return;
    m_showChannelWaveforms = show;
    emit trackChanged();
    scheduleRebuild();
}

QObject *TimelineTrackItem::editor() const
{
    return m_editor.data();
}

void TimelineTrackItem::setEditor(QObject *editor)
{
    auto *controller = qobject_cast<AppController *>(editor);
    if (m_editor == controller)
        return;
    if (m_editor)
        disconnect(m_editor, nullptr, this, nullptr);
    m_editor = controller;
    if (m_editor) {
        connect(m_editor, &AppController::selectionChanged, this, [this] {
            updateActiveSet();
            scheduleRebuild();
        });
        connect(m_editor, &AppController::waveformRangeReady, this, [this](const QString &path) {
            for (auto it = m_waveCache.begin(); it != m_waveCache.end();) {
                if (it->key.startsWith(path + QLatin1Char('|')))
                    it = m_waveCache.erase(it);
                else
                    ++it;
            }
            scheduleRebuild();
        });
        connect(m_editor, &AppController::filmstripTileReady, this, &TimelineTrackItem::scheduleRebuild);
        if (PlaybackEngine *playback = m_editor->playback()) {
            connect(playback, &PlaybackEngine::useProxiesChanged, this,
                    &TimelineTrackItem::scheduleRebuild);
            connect(playback, &PlaybackEngine::proxySizeChanged, this,
                    &TimelineTrackItem::scheduleRebuild);
        }
    }
    emit servicesChanged();
    updateActiveSet();
    scheduleRebuild();
}

QObject *TimelineTrackItem::assets() const
{
    return m_assets.data();
}

void TimelineTrackItem::setAssets(QObject *assets)
{
    auto *library = qobject_cast<AssetLibrary *>(assets);
    if (m_assets == library)
        return;
    if (m_assets)
        disconnect(m_assets, nullptr, this, nullptr);
    m_assets = library;
    if (m_assets)
        connect(m_assets, &AssetLibrary::badgeRevisionChanged, this, &TimelineTrackItem::scheduleRebuild);
    emit servicesChanged();
    scheduleRebuild();
}

QObject *TimelineTrackItem::haptics() const
{
    return m_haptics.data();
}

void TimelineTrackItem::setHaptics(QObject *haptics)
{
    if (m_haptics == haptics)
        return;
    m_haptics = haptics;
    emit servicesChanged();
}

bool TimelineTrackItem::touchDragging() const
{
    return m_drag.moving && m_viewState && m_viewState->touchMode();
}

bool TimelineTrackItem::lifted() const
{
    return (m_drag.moving || m_drag.armed) && m_viewState && m_viewState->touchMode();
}

QVariantMap TimelineTrackItem::clipInfo(int index) const
{
    if (!m_clips || index < 0 || index >= m_clips->rows().size())
        return {};
    const TimelineClipsModel::Row &row = m_clips->rows().at(index);
    return {
        {QStringLiteral("id"), row.id},
        {QStringLiteral("start"), row.start},
        {QStringLiteral("duration"), row.duration},
        {QStringLiteral("kind"), row.kind},
    };
}

void TimelineTrackItem::setLivePreview(const QString &clipId, bool active, double start,
                                       double duration, double inPoint, double outPoint)
{
    if (active)
        m_livePreviews.insert(clipId, {start, duration, inPoint, outPoint});
    else if (!m_livePreviews.remove(clipId))
        return;
    scheduleRebuild();
}

void TimelineTrackItem::scheduleRebuild()
{
    polish();
}

void TimelineTrackItem::onViewChanged()
{
    if (!m_viewState)
        return;
    setAcceptHoverEvents(!m_viewState->touchMode());
    const timelinelayout::Window window =
        timelinelayout::contentWindow(m_viewState->viewX(), m_viewState->viewW());
    // Scrolling inside the built chunk moves nothing: the nodes ride along with the Flickable.
    if (window == m_window && qFuzzyCompare(m_builtPxPerSecond, m_viewState->pxPerSecond()))
        return;
    scheduleRebuild();
}

void TimelineTrackItem::itemChange(ItemChange change, const ItemChangeData &value)
{
    if (change == ItemSceneChange) {
        // Textures belong to the window's scene graph; a new window needs new ones.
        m_frameCounter = 0;
        scheduleRebuild();
    }
    QQuickItem::itemChange(change, value);
}

void TimelineTrackItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size())
        scheduleRebuild();
}

void TimelineTrackItem::releaseResources()
{
    // The root node owns the textures and is torn down with the scene graph.
    m_frameCounter = 0;
}

bool TimelineTrackItem::isSelected(int index) const
{
    return m_editor && m_editor->selectionContains(m_trackIndex, index);
}

double TimelineTrackItem::edgeMargin(double clipWidth) const
{
    if (!m_viewState)
        return 14.0;
    const TimelineViewState::Style &s = m_viewState->styleValues();
    return m_viewState->touchMode() ? std::min(s.edgeMarginTouch, clipWidth / 4.0)
                                    : s.edgeMarginDesktop;
}

double TimelineTrackItem::hotspotExtra() const
{
    if (!m_viewState)
        return 10.0;
    const TimelineViewState::Style &s = m_viewState->styleValues();
    return m_viewState->touchMode() ? s.trimHotspotExtraTouch : s.trimHotspotExtraDesktop;
}

QRectF TimelineTrackItem::effectiveRect(int index, double *inPoint, double *outPoint) const
{
    const TimelineClipsModel::Row &row = m_clips->rows().at(index);
    const TimelineViewState::Style &s = m_viewState->styleValues();
    const double pps = m_viewState->pxPerSecond();

    double start = row.start;
    double duration = row.duration;
    double in = row.inPoint;
    double out = row.outPoint;
    if (const auto live = m_livePreviews.constFind(row.id); live != m_livePreviews.constEnd()) {
        start = live->start;
        duration = live->duration;
        in = live->inPoint;
        out = live->outPoint;
    } else if (m_viewState->trimFollowActive() && !row.linkId.isEmpty()
               && row.linkId == m_viewState->trimFollowLinkId()
               && row.id != m_viewState->trimFollowClipId()) {
        // The A/V partner of the clip being trimmed takes the same live timing.
        start = m_viewState->trimFollowStart();
        duration = m_viewState->trimFollowDuration();
        in = m_viewState->trimFollowIn();
        out = m_viewState->trimFollowOut();
    }
    if (inPoint)
        *inPoint = in;
    if (outPoint)
        *outPoint = out;

    double offsetX = 0.0;
    double offsetY = 0.0;
    const bool leader = m_viewState->moveLeaderTrack() == m_trackIndex
                        && m_viewState->moveLeaderClip() == index;
    if (m_viewState->moveFollowActive() && !leader && isSelected(index)) {
        offsetX = m_viewState->moveFollowDeltaX();
        if (m_trackIndex == m_viewState->moveLeaderTrack())
            offsetY = m_viewState->moveFollowDeltaY();
    }
    return timelinelayout::clipRect(start, duration, pps, height(), s.ringWidth, s.clipMinWidth,
                                     offsetX, offsetY);
}

void TimelineTrackItem::updateActiveSet()
{
    QSet<QString> ids;
    if (m_clips && m_editor) {
        const QList<TimelineClipsModel::Row> &rows = m_clips->rows();
        for (int i = 0; i < rows.size(); ++i) {
            if (isSelected(i))
                ids.insert(rows.at(i).id);
        }
    }
    if (!m_hoverId.isEmpty())
        ids.insert(m_hoverId);
    for (auto it = m_livePreviews.cbegin(); it != m_livePreviews.cend(); ++it)
        ids.insert(it.key());
    m_activeClips->setActiveIds(ids);
}

void TimelineTrackItem::updatePolish()
{
    const bool accessible = QAccessible::isActive();
    QStringList shownIds;
    if (accessible) {
        for (const ClipVisual &visual : std::as_const(m_visible))
            shownIds.append(visual.id);
    }
    build();
    if (accessible) {
        QStringList ids;
        for (const ClipVisual &visual : std::as_const(m_visible))
            ids.append(visual.id);
        if (ids != shownIds) {
            QAccessibleEvent event(this, QAccessible::ObjectReorder);
            QAccessible::updateAccessibility(&event);
        }
    }
    update();
}

void TimelineTrackItem::build()
{
    m_under.clear();
    m_over.clear();
    m_tiles.clear();
    m_labels.clear();
    m_visible.clear();
    if (!m_viewState || !m_clips || width() <= 0.0 || height() <= 0.0)
        return;

    m_window = timelinelayout::contentWindow(m_viewState->viewX(), m_viewState->viewW());
    m_builtPxPerSecond = m_viewState->pxPerSecond();
    m_originX = m_window.left;
    m_fontFamily = m_viewState->styleValues().fontFamily;

    const QList<TimelineClipsModel::Row> &rows = m_clips->rows();
    const bool touch = m_viewState->touchMode();
    if (m_waveCache.size() > rows.size()) {
        QSet<QString> ids;
        for (const TimelineClipsModel::Row &row : rows)
            ids.insert(row.id);
        for (auto it = m_waveCache.begin(); it != m_waveCache.end();)
            it = ids.contains(it.key()) ? std::next(it) : m_waveCache.erase(it);
    }
    for (int i = 0; i < rows.size(); ++i) {
        if (m_drag.moving && i == m_drag.clipIndex)
            continue;
        const QRectF rect = effectiveRect(i);
        const bool selected = isSelected(i);
        const bool follower = m_viewState->moveFollowActive() && selected;
        if (!follower && !m_window.contains(rect.left(), rect.right()))
            continue;
        const bool lit = !touch && m_hoverBody && rows.at(i).id == m_hoverId;
        appendClip(i, rect, selected, lit, false);
    }
    // The dragged clip draws last, over its neighbours.
    if (m_drag.moving && m_drag.clipIndex >= 0 && m_drag.clipIndex < rows.size())
        appendClip(m_drag.clipIndex, m_drag.rect, isSelected(m_drag.clipIndex), true, touch);
    else if (m_drag.armed && m_drag.clipIndex >= 0 && m_drag.clipIndex < rows.size())
        appendClip(m_drag.clipIndex, effectiveRect(m_drag.clipIndex), true, true, true);
}

void TimelineTrackItem::appendClip(int index, const QRectF &baseRect, bool selected, bool lit,
                                   bool lifted)
{
    const TimelineClipsModel::Row &row = m_clips->rows().at(index);
    const TimelineViewState::Style &s = m_viewState->styleValues();
    const double pps = m_viewState->pxPerSecond();
    const double ox = m_originX;

    QRectF rect = baseRect;
    if (lifted) {
        // The touch lift: the held clip grows a little, from its centre.
        const QPointF c = rect.center();
        rect = QRectF(c.x() - rect.width() * 0.515, c.y() - rect.height() * 0.515,
                      rect.width() * 1.03, rect.height() * 1.03);
    }

    double inPoint = row.inPoint;
    double outPoint = row.outPoint;
    effectiveRect(index, &inPoint, &outPoint);

    ClipVisual visual;
    visual.index = index;
    visual.id = row.id;
    visual.rect = rect;
    visual.selected = selected;

    // --- body --------------------------------------------------------------------------------
    const bool isAdjustment = row.kind == QLatin1String("adjustment");
    QColor base;
    if (isAdjustment) {
        base = row.adjustmentKind == QLatin1String("audioEffects") ? s.adjustmentAudio
               : row.adjustmentKind == QLatin1String("mask")       ? s.adjustmentMask
                                                                   : s.adjustmentVideo;
    } else {
        const QString type = row.kind == QLatin1String("composite") ? QStringLiteral("composite")
                             : m_trackType == QLatin1String("shape") ? QStringLiteral("graphic")
                                                                     : m_trackType;
        base = type == QLatin1String("text")        ? s.clipText
               : type == QLatin1String("subtitle")  ? s.clipSubtitle
               : type == QLatin1String("audio")     ? s.clipAudio
               : type == QLatin1String("graphic")   ? s.clipGraphic
               : type == QLatin1String("effect")    ? s.clipEffect
               : type == QLatin1String("adjustment") ? s.clipEffect
               : type == QLatin1String("composite") ? s.clipComposite
                                                    : s.clipVideo;
    }
    if (lit)
        base = base.lighter(115);
    appendRoundedRect(m_under, rect, s.radiusSm, base, ox);

    const bool bandTrack = m_trackType == QLatin1String("video")
                           || m_trackType == QLatin1String("audio")
                           || m_trackType == QLatin1String("shape");
    const double band = std::min(s.headerBandHeight, std::max(0.0, rect.height() * 0.5));
    const bool showWaveform = m_clipDisplay == 2;
    const bool showWaveformBar = m_trackType == QLatin1String("video") && m_clipDisplay == 1
                                 && row.hasEmbeddedAudio && rect.height() - band >= 30.0;
    const double barHeight = std::max(12.0, std::round((rect.height() - band) * 0.3));

    // --- filmstrip ---------------------------------------------------------------------------
    const bool filmstrip = !row.filmstripPath.isEmpty() && !showWaveform
                           && (m_trackType == QLatin1String("video")
                               || m_trackType == QLatin1String("shape")
                               || row.kind == QLatin1String("image"));
    if (filmstrip) {
        const QRectF body = rect.adjusted(0.0, bandTrack ? band : 0.0, 0.0,
                                          showWaveformBar ? -barHeight : 0.0);
        buildFilmstrip(index, body, inPoint, outPoint);
    }

    // --- waveform ----------------------------------------------------------------------------
    const bool waveform = m_trackType == QLatin1String("audio")
                          || (m_trackType == QLatin1String("video") && showWaveform)
                          || showWaveformBar;
    if (waveform) {
        const QRectF area(rect.left(), showWaveformBar ? rect.bottom() - barHeight : rect.top() + band,
                          rect.width(),
                          showWaveformBar ? barHeight : std::max(0.0, rect.height() - band));
        if (showWaveformBar)
            appendRect(m_over, area, QColor(0, 0, 0, 89), ox);
        buildWaveform(index, area, rect.left(), rect.width(), inPoint, outPoint, showWaveformBar);
    }

    // --- header band and labels --------------------------------------------------------------
    const QFont tinyFont = labelFont(m_fontFamily, s.fontSizeTiny, false);
    const QFontMetricsF tinyMetrics(tinyFont);
    if (bandTrack && band > 0.0) {
        const bool proxy = m_assets && m_editor && m_editor->playback()
                           && m_editor->playback()->useProxies() && row.kind == QLatin1String("video")
                           && m_assets->hasProxyForPath(row.path);
        const QRectF bandRect(rect.left(), rect.top(), rect.width(), band);
        appendRect(m_over, bandRect, proxy ? s.proxyBand : s.scrim, ox);

        QString assetId;
        bool editFriendly = false;
        bool vfr = false;
        if (m_assets && row.kind == QLatin1String("video")) {
            assetId = m_assets->assetIdForPath(row.path);
            editFriendly = !assetId.isEmpty() && m_assets->isEditFriendly(assetId);
            vfr = !assetId.isEmpty() && !editFriendly && m_assets->isVariableFrameRate(assetId);
        }

        const double rowLeft = rect.left() + 6.0;
        const double rowWidth = std::max(0.0, rect.width() - 12.0);
        const double centerY = rect.top() + band / 2.0;
        const double spacing = 4.0;

        const QFont pillFont = labelFont(m_fontFamily, s.fontSizeTiny, true);
        const QFontMetricsF pillMetrics(pillFont);
        const auto pillSize = [&](const QString &text) {
            return QSizeF(pillMetrics.horizontalAdvance(text) + s.spacingMd,
                          std::min(pillMetrics.height() + 2.0, band - 2.0));
        };
        const QSizeF proxySize = pillSize(s.proxyLabel);
        const QSizeF editSize = pillSize(s.editFriendlyLabel);
        const bool showProxyPill = proxy && rowWidth > proxySize.width() * 2.5;
        const bool showEditPill =
            editFriendly
            && rowWidth > editSize.width() * 2.5 + (showProxyPill ? proxySize.width() : 0.0);
        const double pillsWidth = (showProxyPill ? proxySize.width() + spacing : 0.0)
                                  + (showEditPill ? editSize.width() + spacing : 0.0);

        double x = rowLeft;
        if (vfr) {
            const double size = std::min(s.iconSizeSm, band - 2.0);
            const QRectF badge(x, centerY - size / 2.0, size, size);
            appendRoundedRect(m_over, badge, size / 2.0, s.warning, ox);
            const QFont bangFont = labelFont(m_fontFamily, size * 0.75, true);
            const QFontMetricsF bangMetrics(bangFont);
            m_labels.append({QStringLiteral("!"),
                             QPointF(badge.center().x() - bangMetrics.horizontalAdvance(QLatin1Char('!')) / 2.0 - ox,
                                     badge.center().y() - bangMetrics.height() / 2.0),
                             size * 0.75, s.onMedia, true});
            visual.toolTips.append({badge, s.vfrTooltip});
            x += size + spacing;
        }

        const double nameRoom = std::max(0.0, rowLeft + rowWidth - x - pillsWidth);
        const QString name = tinyMetrics.elidedText(row.name, Qt::ElideRight, nameRoom);
        if (!name.isEmpty()) {
            m_labels.append({name, QPointF(x - ox, centerY - tinyMetrics.height() / 2.0),
                             s.fontSizeTiny, s.onMedia, false});
            x += tinyMetrics.horizontalAdvance(name) + spacing;
        } else {
            x += spacing;
        }

        const auto appendPill = [&](const QString &text, const QSizeF &size, const QColor &fill,
                                    const QColor &fg, const QString &tip) {
            const QRectF pill(x, centerY - size.height() / 2.0, size.width(), size.height());
            appendRoundedRect(m_over, pill, s.radiusXs, fill, ox);
            m_labels.append({text,
                             QPointF(pill.center().x() - pillMetrics.horizontalAdvance(text) / 2.0 - ox,
                                     pill.center().y() - pillMetrics.height() / 2.0),
                             s.fontSizeTiny, fg, true});
            visual.toolTips.append({pill, tip});
            x += size.width() + spacing;
        };
        if (showProxyPill)
            appendPill(s.proxyLabel, proxySize, s.proxyPill, s.proxyPillForeground, s.proxyTooltip);
        if (showEditPill)
            appendPill(s.editFriendlyLabel, editSize, s.editFriendlyPill,
                       s.editFriendlyPillForeground, s.editFriendlyTooltip);
    }

    if (isAdjustment) {
        // An adjustment is its stack: it shows the effects it carries, and only names its kind
        // while it is still empty.
        QString text = row.effectsLabel;
        if (text.isEmpty())
            text = row.name;
        if (text.isEmpty()) {
            text = row.adjustmentKind == QLatin1String("audioEffects") ? tr("Audio adjustment")
                   : row.adjustmentKind == QLatin1String("mask")       ? tr("Mask")
                                                                       : tr("Adjustment");
        }
        const QString elided = tinyMetrics.elidedText(text, Qt::ElideRight,
                                                      std::max(0.0, rect.width() - 12.0));
        if (!elided.isEmpty())
            m_labels.append({elided,
                             QPointF(rect.left() + 6.0 - ox,
                                     rect.center().y() - tinyMetrics.height() / 2.0),
                             s.fontSizeTiny, s.onMedia, false});
    }

    if (m_trackType == QLatin1String("text") || m_trackType == QLatin1String("subtitle")) {
        const QString text = m_trackType == QLatin1String("subtitle")
                                 ? (row.name.isEmpty() ? tr("Subtitles") : row.name)
                                 : (row.textContent.isEmpty() ? row.name : row.textContent);
        const QFont xsFont = labelFont(m_fontFamily, s.fontSizeXs, false);
        const QFontMetricsF xsMetrics(xsFont);
        const double margin = std::min(s.spacingLg, rect.width() / 4.0);
        const QString elided = xsMetrics.elidedText(QString(text).replace(QLatin1Char('\n'), QLatin1Char(' ')),
                                                    Qt::ElideRight,
                                                    std::max(0.0, rect.width() - 2.0 * margin));
        if (!elided.isEmpty())
            m_labels.append({elided,
                             QPointF(rect.left() + margin - ox,
                                     rect.center().y() - xsMetrics.height() / 2.0),
                             s.fontSizeXs, s.onMedia, false});
    }

    // --- fades -------------------------------------------------------------------------------
    const auto gain = [&row](double t) {
        return drift::shapedProgress(t, row.fadeCurveTyped, row.fadeShapeTyped);
    };
    const QColor fadeFill(0, 0, 0, 97);
    const QColor fadeStroke(255, 255, 255, 230);
    const auto appendFade = [&](double left, double w, bool out) {
        if (w <= 0.5 || rect.height() <= 0.5)
            return;
        const int steps = std::clamp(int(std::ceil(w / 2.0)), 8, 64);
        QVector<QPointF> curve;
        curve.reserve(steps + 1);
        for (int i = 0; i <= steps; ++i) {
            const double t = double(i) / steps;
            const double g = gain(out ? 1.0 - t : t);
            curve.append({left + t * w, rect.top() + rect.height() * (1.0 - g)});
        }
        for (int i = 0; i < steps; ++i) {
            const QPointF a = curve[i];
            const QPointF b = curve[i + 1];
            appendQuad(m_over, vertex(a.x() - ox, rect.top(), fadeFill),
                       vertex(b.x() - ox, rect.top(), fadeFill), vertex(b.x() - ox, b.y(), fadeFill),
                       vertex(a.x() - ox, a.y(), fadeFill));
        }
        appendStroke(m_over, curve, 1.5, fadeStroke, ox);
    };
    const double fadeInW = std::clamp(row.fadeIn * pps, 0.0, rect.width());
    const double fadeOutW = std::clamp(row.fadeOut * pps, 0.0, rect.width());
    appendFade(rect.left(), fadeInW, false);
    appendFade(rect.right() - fadeOutW, fadeOutW, true);

    // --- effect drop and selection -----------------------------------------------------------
    const bool dropTarget = m_viewState->effectDropTrack() == m_trackIndex
                            && m_viewState->effectDropClip() == index;
    if (dropTarget) {
        QColor tint = s.clipEffect;
        tint.setAlphaF(0.28);
        appendRoundedRect(m_over, rect, s.radiusSm, tint, ox);
        appendOutline(m_over, rect, s.radiusSm, s.borderWidthFocus, s.clipEffect, ox);
    } else if (selected) {
        appendOutline(m_over, rect, s.radiusSm, s.ringWidth, s.primary, ox);
    }

    m_visible.append(visual);
}

void TimelineTrackItem::buildWaveform(int index, const QRectF &area, double clipX,
                                      double clipWidth, double inPoint, double outPoint, bool bar)
{
    const TimelineClipsModel::Row &row = m_clips->rows().at(index);
    if (!m_editor || row.path.isEmpty() || area.height() <= 0.0)
        return;
    const TimelineViewState::Style &s = m_viewState->styleValues();
    const double ox = m_originX;

    const timelinelayout::Span span =
        timelinelayout::waveformSpan(clipX, clipWidth, m_window, kWaveformWindowMax);
    const double srcPerPx = clipWidth > 0.0 && outPoint > inPoint ? (outPoint - inPoint) / clipWidth : 0.0;
    if (span.width < 1.0 || srcPerPx <= 0.0)
        return;

    int channels = 0;
    if (m_showChannelWaveforms && !bar)
        channels = m_editor->waveformChannelCount(row.path, row.audioStreamIndex);
    const int laneCount = channels >= 2 && area.height() / channels >= 8.0 ? channels : 1;

    const int buckets = int(std::ceil(span.width));
    const double start = inPoint + span.left * srcPerPx;
    const double dur = span.width * srcPerPx;
    const QString key = QStringLiteral("%1|%2|%3|%4|%5|%6")
                            .arg(row.path)
                            .arg(start, 0, 'g', 12)
                            .arg(dur, 0, 'g', 12)
                            .arg(buckets)
                            .arg(row.audioStreamIndex)
                            .arg(laneCount);
    WaveCacheEntry &cached = m_waveCache[row.id];
    if (cached.key != key) {
        cached = WaveCacheEntry{};
        cached.key = key;
        if (laneCount > 1) {
            for (int c = 0; c < laneCount; ++c)
                cached.lanes.append(m_editor->waveformDisplayPeaks(row.path, start, dur, buckets,
                                                                  row.audioStreamIndex, c));
            cached.names = m_editor->waveformChannelNames(row.path, row.audioStreamIndex);
        } else {
            cached.peaks = m_editor->waveformDisplayPeaks(row.path, start, dur, buckets,
                                                          row.audioStreamIndex);
        }
        // Nothing decoded yet: do not remember the empty answer, rangeReady brings us back.
        const bool empty = laneCount > 1 ? std::any_of(cached.lanes.cbegin(), cached.lanes.cend(),
                                                       [](const QVector<float> &l) { return l.isEmpty(); })
                                         : cached.peaks.isEmpty();
        if (empty)
            cached.key.clear();
    }

    const double left = area.left() + span.left;
    const int w = std::max(1, int(std::floor(span.width)));
    const QColor color = s.waveform;
    const auto columnPeak = [w](const QVector<float> &values, int x) {
        const int n = values.size();
        int i0 = int(qint64(x) * n / w);
        int i1 = int(qint64(x + 1) * n / w);
        if (i1 <= i0)
            i1 = std::min(n, i0 + 1);
        float peak = 0.0f;
        for (int i = i0; i < i1; ++i)
            peak = std::max(peak, values[i]);
        return double(peak);
    };

    if (laneCount > 1 && cached.lanes.size() == laneCount) {
        const double laneH = area.height() / laneCount;
        for (int c = 0; c < laneCount; ++c) {
            const QVector<float> &values = cached.lanes.at(c);
            if (values.isEmpty())
                continue;
            const double mid = area.top() + c * laneH + laneH / 2.0;
            const double half = (laneH / 2.0) * 0.85;
            for (int x = 0; x < w; ++x) {
                const double amp = std::max(0.5, columnPeak(values, x) * half);
                const double x0 = left + x - ox;
                appendQuad(m_over, vertex(x0, mid - amp, color), vertex(x0 + 1.0, mid - amp, color),
                           vertex(x0 + 1.0, mid + amp, color), vertex(x0, mid + amp, color));
            }
        }
        QColor divider = s.panelBorder;
        divider.setAlphaF(divider.alphaF() * 0.5);
        for (int d = 1; d < laneCount; ++d)
            appendRect(m_over, QRectF(left, area.top() + std::round(d * laneH), w, 1.0), divider, ox);
        if (laneH >= 14.0) {
            const QFontMetricsF metrics(labelFont(m_fontFamily, s.fontSizeTiny, false));
            for (int c = 0; c < laneCount; ++c) {
                const QString name = c < cached.names.size() && !cached.names.at(c).isEmpty()
                                         ? cached.names.at(c)
                                         : QString::number(c + 1);
                m_labels.append({name,
                                 QPointF(area.left() + 3.0 - ox,
                                         area.top() + c * laneH + (laneH - metrics.height()) / 2.0),
                                 s.fontSizeTiny, s.mutedForeground, false});
            }
        }
        return;
    }

    if (cached.peaks.isEmpty())
        return;
    const double mid = area.top() + area.height() / 2.0;
    const double halfHeight = area.height() / 2.0;
    for (int x = 0; x < w; ++x) {
        const double amp = columnPeak(cached.peaks, x) * halfHeight * 0.9;
        if (amp <= 0.5)
            continue;
        const double x0 = left + x - ox;
        appendQuad(m_over, vertex(x0, mid - amp, color), vertex(x0 + 1.0, mid - amp, color),
                   vertex(x0 + 1.0, mid + amp, color), vertex(x0, mid + amp, color));
    }
}

void TimelineTrackItem::buildFilmstrip(int index, const QRectF &body, double inPoint,
                                       double outPoint)
{
    const TimelineClipsModel::Row &row = m_clips->rows().at(index);
    if (body.width() <= 0.0 || body.height() <= 0.0)
        return;

    const bool singleFrame = row.kind == QLatin1String("image") || row.kind == QLatin1String("vector")
                             || row.kind == QLatin1String("model3d");
    const int frameCount = singleFrame ? 1 : kFilmstripFrameCount;
    const QString sourcePath = row.kind == QLatin1String("video") ? row.path : QString();
    const qreal dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
    const int decodeHeight = int(std::ceil(68.0 * dpr));

    const timelinelayout::TileGrid grid =
        timelinelayout::tileGrid(body.left(), body.width(), inPoint, outPoint, row.sourceDuration,
                                 m_window, kFilmstripTileWidth);
    if (grid.lastTile < grid.firstTile)
        return;

    const QImage strip = ClipThumbnailStore::instance().image(row.filmstripPath, decodeHeight);
    const bool tilesEnabled = !sourcePath.isEmpty() && grid.sourceMapped && grid.pxPerSourceSec > 0.0;
    const int level = tilesEnabled ? timelinelayout::tileLevel(grid) : 0;
    const double interval = std::pow(2.0, level);

    for (int t = grid.firstTile; t <= grid.lastTile; ++t) {
        const QRectF target(body.left() + grid.stripOriginX + t * grid.tileWidth, body.top(),
                            grid.tileWidth, body.height());

        // The tile's own frame, decoded on demand, replaces the coarse strip frame once it lands.
        if (tilesEnabled && m_editor) {
            const double srcSec = timelinelayout::tileSourceSeconds(grid, t);
            if (srcSec >= 0.0 && srcSec < row.sourceDuration) {
                const QString file = m_editor->filmstripTilePath(
                    sourcePath, level, qint64(std::floor(srcSec / interval)), row.rotationCorrection);
                if (!file.isEmpty()) {
                    const QImage image = ClipThumbnailStore::instance().image(file, decodeHeight);
                    if (!image.isNull()) {
                        QRectF tgt = target;
                        QRectF src = aspectCrop(QRectF(QPointF(0, 0), image.size()), tgt.size());
                        if (clipTile(tgt, src, body))
                            m_tiles.append({file, image, tgt.translated(-m_originX, 0), src});
                        continue;
                    }
                }
            }
        }

        if (strip.isNull() || strip.width() < frameCount)
            continue;
        const int frame = timelinelayout::frameForTile(grid, t, row.sourceDuration, frameCount);
        const double frameW = double(strip.width()) / frameCount;
        QRectF src = aspectCrop(QRectF(frame * frameW, 0, frameW, strip.height()), target.size());
        QRectF tgt = target;
        if (clipTile(tgt, src, body))
            m_tiles.append({row.filmstripPath, strip, tgt.translated(-m_originX, 0), src});
    }
}

QSGNode *TimelineTrackItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    auto *root = static_cast<TrackRootNode *>(oldNode);
    if (!root)
        root = new TrackRootNode;
    ++m_frameCounter;

    QMatrix4x4 matrix;
    matrix.translate(float(m_originX), 0.0f);
    root->setMatrix(matrix);

    TrackRootNode::fill(root->under, m_under);
    TrackRootNode::fill(root->over, m_over);

    // Tiles: recycle image nodes, share one texture per decoded file.
    QQuickWindow *win = window();
    while (root->images.size() > m_tiles.size()) {
        QSGImageNode *node = root->images.takeLast();
        root->tiles->removeChildNode(node);
        delete node;
    }
    for (int i = 0; i < m_tiles.size(); ++i) {
        const Tile &tile = m_tiles.at(i);
        QSGTexture *texture = root->textures.value(tile.key);
        if (!texture && win) {
            texture = win->createTextureFromImage(tile.image, QQuickWindow::TextureCanUseAtlas);
            if (!texture)
                continue;
            texture->setFiltering(QSGTexture::Linear);
            root->textures.insert(tile.key, texture);
        }
        if (!texture)
            continue;
        root->textureStamps.insert(tile.key, m_frameCounter);
        QSGImageNode *node = nullptr;
        if (i < root->images.size()) {
            node = root->images.at(i);
        } else {
            node = win->createImageNode();
            node->setOwnsTexture(false);
            node->setFiltering(QSGTexture::Linear);
            root->tiles->appendChildNode(node);
            root->images.append(node);
        }
        node->setTexture(texture);
        node->setRect(tile.target);
        node->setSourceRect(tile.source);
    }
    if (root->textures.size() > kMaxCachedTextures) {
        for (auto it = root->textures.begin(); it != root->textures.end();) {
            if (root->textureStamps.value(it.key()) != m_frameCounter) {
                root->textureStamps.remove(it.key());
                delete it.value();
                it = root->textures.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Labels: a node per distinct label, kept while it stays identical.
    QHash<QString, QSGTextNode *> kept;
    for (const Label &label : std::as_const(m_labels)) {
        const QString key = labelKey(label);
        if (kept.contains(key))
            continue;
        QSGTextNode *node = root->labels.take(key);
        if (!node && win) {
            node = win->createTextNode();
            node->setColor(label.color);
            node->setRenderType(QSGTextNode::QtRendering);
            QTextLayout layout(label.text, labelFont(m_fontFamily, label.pixelSize, label.bold));
            layout.beginLayout();
            QTextLine line = layout.createLine();
            if (line.isValid()) {
                line.setLineWidth(1e6);
                line.setPosition(QPointF(0, 0));
            }
            layout.endLayout();
            node->addTextLayout(label.origin, &layout);
            root->texts->appendChildNode(node);
        }
        if (node)
            kept.insert(key, node);
    }
    for (QSGTextNode *stale : std::as_const(root->labels)) {
        root->texts->removeChildNode(stale);
        delete stale;
    }
    root->labels = kept;

    return root;
}

// --- input ---------------------------------------------------------------------------------------

int TimelineTrackItem::clipAt(const QPointF &point, Zone *zone) const
{
    if (!m_clips)
        return -1;
    for (auto it = m_visible.crbegin(); it != m_visible.crend(); ++it) {
        const Zone z = timelinelayout::hitZone(it->rect, point, edgeMargin(it->rect.width()),
                                               hotspotExtra());
        if (z == Zone::None)
            continue;
        if (zone)
            *zone = z;
        return it->index;
    }
    if (zone)
        *zone = Zone::None;
    return -1;
}

bool TimelineTrackItem::contains(const QPointF &point) const
{
    // Only where a clip is. Empty track space belongs to the row underneath (drops, the marquee,
    // deselecting), so it must not be claimed here.
    return m_drag.pressed || clipAt(point) >= 0;
}

void TimelineTrackItem::callHaptic(const char *method)
{
    if (m_haptics)
        QMetaObject::invokeMethod(m_haptics, method);
}

void TimelineTrackItem::mousePressEvent(QMouseEvent *event)
{
    Zone zone = Zone::None;
    const int index = clipAt(event->position(), &zone);
    if (index < 0 || zone != Zone::Body || !m_editor || !m_viewState) {
        event->ignore();
        return;
    }

    forceActiveFocus(Qt::MouseFocusReason);
    const bool touch = m_viewState->touchMode();
    const bool wasSelected = isSelected(index);
    callHaptic("reset");

    m_drag = DragState{};
    m_drag.pressed = true;
    m_drag.clipIndex = index;
    m_drag.clipId = m_clips->rows().at(index).id;
    m_drag.pressRect = effectiveRect(index);
    m_drag.rect = m_drag.pressRect;
    m_drag.pressItemPos = event->position();
    m_drag.lastScenePos = event->scenePosition();
    m_drag.pointer = event->position();
    m_drag.wasSelected = wasSelected;
    m_drag.rightButton = event->button() == Qt::RightButton;

    if (m_drag.rightButton) {
        if (!wasSelected)
            m_editor->selectClip(m_trackIndex, index);
        updateActiveSet();
        m_drag.pressed = false;
        emit contextMenuRequested(index);
        event->accept();
        return;
    }

    // On a pointer the clip owns the press outright; on touch the Flickable may still take it
    // for a pan until a press-and-hold picks the clip up.
    setKeepMouseGrab(!touch);
    m_longPress.start(touch ? 450 : 800);

    if (!m_viewState->multiSelectActive()) {
        if (event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier))
            m_editor->addToSelection(m_trackIndex, index);
        else if (!wasSelected)
            m_editor->selectClip(m_trackIndex, index);
        if (!wasSelected)
            callHaptic("select");
    }
    event->accept();
}

void TimelineTrackItem::beginMove()
{
    const TimelineViewState::Style &s = m_viewState->styleValues();
    const double pps = m_viewState->pxPerSecond();
    const TimelineClipsModel::Row &row = m_clips->rows().at(m_drag.clipIndex);

    m_drag.moving = true;
    m_drag.didDrag = true;
    m_drag.minX = s.ringWidth;
    if (isSelected(m_drag.clipIndex) && m_editor->selectionCount() > 1) {
        const double earliest = m_editor->selectionEarliestStartSeconds();
        m_drag.minX = std::max(0.0, row.start - earliest) * pps + s.ringWidth;
    }
    const double total = m_viewState->totalTracksHeight();
    m_drag.minY = -std::max(height(), total);
    m_drag.maxY = std::max(height() * 2.0, total);
    m_longPress.stop();
    setKeepMouseGrab(true);
    emit moveStarted(m_drag.clipIndex);
    emit draggingChanged();
}

void TimelineTrackItem::applyDrag(const QPointF &scenePos)
{
    m_drag.lastScenePos = scenePos;
    const QPointF pos = mapFromScene(scenePos);
    m_drag.pointer = pos;
    emit dragPointerChanged();
    if (!m_drag.moving)
        return;

    const TimelineViewState::Style &s = m_viewState->styleValues();
    const QPointF delta = pos - m_drag.pressItemPos;
    m_drag.rect.moveLeft(std::max(m_drag.minX, m_drag.pressRect.left() + delta.x()));
    m_drag.rect.moveTop(std::clamp(m_drag.pressRect.top() + delta.y(), m_drag.minY, m_drag.maxY));

    const double originX = m_clips->rows().at(m_drag.clipIndex).start * m_viewState->pxPerSecond()
                           + s.ringWidth;
    emit moveUpdated(m_drag.clipIndex, m_drag.rect, m_drag.rect.left() - originX,
                     m_drag.rect.top() - s.ringWidth);
    scheduleRebuild();
}

void TimelineTrackItem::refreshDrag()
{
    if (m_drag.moving)
        applyDrag(m_drag.lastScenePos);
}

void TimelineTrackItem::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_drag.pressed || !m_clips || m_drag.clipIndex >= m_clips->rows().size()) {
        event->ignore();
        return;
    }
    const QPointF delta = mapFromScene(event->scenePosition()) - m_drag.pressItemPos;
    if (!m_drag.moving) {
        const bool touch = m_viewState->touchMode();
        const bool pastThreshold = std::abs(delta.x()) > 2.0 || std::abs(delta.y()) > 2.0;
        if (touch) {
            if (!m_drag.armed) {
                // Not picked up yet: this is someone panning. Let it go, and do not let the
                // long-press fire under a moving finger.
                if (delta.manhattanLength() > QGuiApplication::styleHints()->startDragDistance())
                    m_longPress.stop();
                event->ignore();
                return;
            }
        } else if (!pastThreshold || m_viewState->multiSelectActive()) {
            m_drag.lastScenePos = event->scenePosition();
            event->accept();
            return;
        }
        beginMove();
    }
    applyDrag(event->scenePosition());
    event->accept();
}

void TimelineTrackItem::finishPress(bool canceled)
{
    const DragState drag = m_drag;
    m_drag = DragState{};
    m_longPress.stop();
    setKeepMouseGrab(false);
    setKeepTouchGrab(false);
    if (drag.moving || drag.armed)
        emit draggingChanged();

    if (drag.moving) {
        if (canceled) {
            emit moveCanceled();
            callHaptic("reset");
        } else {
            emit moveFinished(drag.clipIndex, drag.rect);
            callHaptic("drop");
        }
        scheduleRebuild();
        return;
    }
    scheduleRebuild();
    if (canceled || !m_editor)
        return;

    // Held and let go without moving: the hold was a request for the menu.
    if (drag.armed) {
        updateActiveSet();
        emit contextMenuRequested(drag.clipIndex);
        return;
    }

    if (m_viewState && m_viewState->multiSelectActive()) {
        emit toggleSelectionRequested(drag.clipIndex);
        callHaptic("select");
        return;
    }
    if (QGuiApplication::keyboardModifiers() & (Qt::ShiftModifier | Qt::ControlModifier))
        return; // the press already added it
    m_editor->selectClip(m_trackIndex, drag.clipIndex);
}

void TimelineTrackItem::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_drag.pressed) {
        event->ignore();
        return;
    }
    if (m_drag.moving)
        applyDrag(event->scenePosition());
    finishPress(false);
    event->accept();
}

void TimelineTrackItem::mouseUngrabEvent()
{
    if (m_drag.pressed)
        finishPress(true);
}

void TimelineTrackItem::mouseDoubleClickEvent(QMouseEvent *event)
{
    Zone zone = Zone::None;
    const int index = clipAt(event->position(), &zone);
    if (index < 0 || zone != Zone::Body || event->button() != Qt::LeftButton || !m_editor) {
        event->ignore();
        return;
    }
    if (m_clips->rows().at(index).kind == QLatin1String("composite"))
        m_editor->openCompositeClip(m_trackIndex, index);
    event->accept();
}

void TimelineTrackItem::setToolTip(const QString &text, const QRectF &rect)
{
    if (m_hoverToolTip == text && m_hoverToolTipRect == rect)
        return;
    m_hoverToolTip = text;
    m_hoverToolTipRect = rect;
    emit hoverToolTipChanged();
}

void TimelineTrackItem::setHovered(const QString &clipId, bool body, const QPointF &point)
{
    const bool changed = clipId != m_hoverId || body != m_hoverBody;
    m_hoverId = clipId;
    m_hoverBody = body;

    QString tip;
    QRectF tipRect;
    for (const ClipVisual &visual : std::as_const(m_visible)) {
        if (visual.id != clipId)
            continue;
        for (const auto &entry : visual.toolTips) {
            if (entry.first.contains(point) && !entry.second.isEmpty()) {
                tip = entry.second;
                tipRect = entry.first;
            }
        }
    }
    setToolTip(tip, tipRect);

    if (body && !m_drag.pressed)
        setCursor(Qt::OpenHandCursor);
    else if (m_drag.moving)
        setCursor(Qt::ClosedHandCursor);
    else
        unsetCursor();

    if (changed) {
        updateActiveSet();
        scheduleRebuild();
    }
}

void TimelineTrackItem::hoverEnterEvent(QHoverEvent *event)
{
    hoverMoveEvent(event);
}

void TimelineTrackItem::hoverMoveEvent(QHoverEvent *event)
{
    Zone zone = Zone::None;
    const int index = clipAt(event->position(), &zone);
    setHovered(index >= 0 ? m_clips->rows().at(index).id : QString(), zone == Zone::Body,
               event->position());
    event->ignore();
}

void TimelineTrackItem::hoverLeaveEvent(QHoverEvent *event)
{
    event->ignore();
    // A leave also arrives when the pointer moves onto the overlay's trim strip, which sits above
    // this item. Dropping the hover then would remove that very overlay out from under the
    // pointer, so ask where the pointer really is before letting go.
    QTimer::singleShot(0, this, [this] {
        const QPointF point = mapFromGlobal(QCursor::pos());
        Zone zone = Zone::None;
        const int index = clipAt(point, &zone);
        if (index >= 0 && m_clips && m_clips->rows().at(index).id == m_hoverId)
            setHovered(m_hoverId, false, point);
        else
            setHovered(QString(), false, QPointF());
    });
}

void TimelineTrackItem::keyPressEvent(QKeyEvent *event)
{
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && m_editor
        && m_editor->selectionCount() > 0) {
        m_editor->deleteSelectedClip();
        event->accept();
        return;
    }
    event->ignore();
}

// --- accessibility -------------------------------------------------------------------------------

// One drawn clip. It has no QObject of its own, so it is addressed by clip id and reads the index
// fresh from the model, the same way the gestures do.
class TimelineClipAccessible : public QAccessibleInterface, public QAccessibleActionInterface
{
public:
    TimelineClipAccessible(TimelineTrackItem *item, const QString &clipId)
        : m_item(item)
        , m_clipId(clipId)
    {
    }

    const QString &clipId() const { return m_clipId; }

    bool isValid() const override { return rowIndex() >= 0; }
    QObject *object() const override { return nullptr; }
    QWindow *window() const override { return m_item ? m_item->window() : nullptr; }
    QAccessibleInterface *parent() const override
    {
        return m_item ? QAccessible::queryAccessibleInterface(m_item.data()) : nullptr;
    }
    QAccessibleInterface *child(int) const override { return nullptr; }
    int childCount() const override { return 0; }
    int indexOfChild(const QAccessibleInterface *) const override { return -1; }
    QAccessibleInterface *childAt(int, int) const override { return nullptr; }

    QString text(QAccessible::Text t) const override
    {
        const int index = rowIndex();
        if (t != QAccessible::Name || index < 0)
            return {};
        return TimelineTrackItem::tr("%1, track %2")
            .arg(m_item->m_clips->rows().at(index).name)
            .arg(m_item->m_trackIndex + 1);
    }
    void setText(QAccessible::Text, const QString &) override {}

    QRect rect() const override
    {
        if (!m_item)
            return {};
        for (const TimelineTrackItem::ClipVisual &visual : std::as_const(m_item->m_visible)) {
            if (visual.id == m_clipId)
                return QRectF(m_item->mapToGlobal(visual.rect.topLeft()), visual.rect.size())
                    .toAlignedRect();
        }
        return {};
    }
    QAccessible::Role role() const override { return QAccessible::Button; }
    QAccessible::State state() const override
    {
        QAccessible::State s;
        s.selectable = true;
        const int index = rowIndex();
        s.selected = index >= 0 && m_item->isSelected(index);
        return s;
    }

    void *interface_cast(QAccessible::InterfaceType type) override
    {
        return type == QAccessible::ActionInterface ? static_cast<QAccessibleActionInterface *>(this)
                                                     : nullptr;
    }

    QStringList actionNames() const override { return {pressAction()}; }
    void doAction(const QString &actionName) override
    {
        const int index = rowIndex();
        if (actionName == pressAction() && index >= 0 && m_item->m_editor)
            m_item->m_editor->selectClip(m_item->m_trackIndex, index);
    }
    QStringList keyBindingsForAction(const QString &) const override { return {}; }

private:
    int rowIndex() const
    {
        if (!m_item || !m_item->m_clips)
            return -1;
        const QList<TimelineClipsModel::Row> &rows = m_item->m_clips->rows();
        for (int i = 0; i < rows.size(); ++i) {
            if (rows.at(i).id == m_clipId)
                return i;
        }
        return -1;
    }

    QPointer<TimelineTrackItem> m_item;
    QString m_clipId;
};

// The track item, with the clips it currently draws as children. The Qt Quick factory would only
// see an item with no child items.
class TimelineTrackAccessible : public QAccessibleObject
{
public:
    explicit TimelineTrackAccessible(TimelineTrackItem *item)
        : QAccessibleObject(item)
    {
    }

    ~TimelineTrackAccessible() override
    {
        for (QAccessible::Id id : std::as_const(m_children))
            QAccessible::deleteAccessibleInterface(id);
    }

    QWindow *window() const override { return item()->window(); }

    QAccessibleInterface *parent() const override
    {
        // The nearest ancestor Qt Quick exposes; the factory answers null for the rest.
        for (QQuickItem *p = item()->parentItem(); p; p = p->parentItem()) {
            if (QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(p))
                return iface;
        }
        return item()->window() ? QAccessible::queryAccessibleInterface(item()->window()) : nullptr;
    }

    int childCount() const override { return int(item()->m_visible.size()); }

    QAccessibleInterface *child(int index) const override
    {
        if (index < 0 || index >= item()->m_visible.size())
            return nullptr;
        const QString &clipId = item()->m_visible.at(index).id;
        QAccessible::Id id = m_children.value(clipId);
        if (!id) {
            id = QAccessible::registerAccessibleInterface(
                new TimelineClipAccessible(item(), clipId));
            m_children.insert(clipId, id);
        }
        return QAccessible::accessibleInterface(id);
    }

    int indexOfChild(const QAccessibleInterface *child) const override
    {
        const auto *clip = dynamic_cast<const TimelineClipAccessible *>(child);
        if (!clip)
            return -1;
        const QList<TimelineTrackItem::ClipVisual> &visible = item()->m_visible;
        for (int i = 0; i < visible.size(); ++i) {
            if (visible.at(i).id == clip->clipId())
                return i;
        }
        return -1;
    }

    QAccessibleInterface *childAt(int x, int y) const override
    {
        for (int i = childCount() - 1; i >= 0; --i) {
            QAccessibleInterface *iface = child(i);
            if (iface && iface->rect().contains(x, y))
                return iface;
        }
        return nullptr;
    }

    QRect rect() const override
    {
        const QRectF local = item()->boundingRect();
        return QRectF(item()->mapToGlobal(local.topLeft()), local.size()).toAlignedRect();
    }

    QString text(QAccessible::Text) const override { return {}; }
    QAccessible::Role role() const override { return QAccessible::Grouping; }

    QAccessible::State state() const override
    {
        QAccessible::State s;
        s.invisible = !item()->isVisible();
        return s;
    }

private:
    TimelineTrackItem *item() const { return static_cast<TimelineTrackItem *>(object()); }

    mutable QHash<QString, QAccessible::Id> m_children;
};

void TimelineTrackItem::installAccessibility()
{
    QAccessible::installFactory([](const QString &className, QObject *object)
                                    -> QAccessibleInterface * {
        if (className == QLatin1String("TimelineTrackItem"))
            return new TimelineTrackAccessible(static_cast<TimelineTrackItem *>(object));
        return nullptr;
    });
}
