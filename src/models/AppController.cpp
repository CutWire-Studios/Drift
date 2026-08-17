#include "AppController.h"

#include "AssetLibrary.h"
#include "core/Clip.h"
#include "core/Mask.h"
#include "core/SpeedCurve.h"
#include "core/ShapePath.h"
#include "core/SubtitleCue.h"
#include "core/SrtIO.h"
#include "core/TimelineOps.h"
#include "core/Transition.h"
#include "core/commands/ProjectCommands.h"
#include "engine/AddonRegistry.h"
#include "engine/AudioMixer.h"
#include "engine/ClipReaderPool.h"
#include "engine/ProjectDependencies.h"
#include "engine/AudioEffectCatalog.h"
#include "engine/EffectCatalog.h"
#include "engine/EffectTemplateCatalog.h"
#include "engine/Exporter.h"
#include "engine/EmojiCatalog.h"
#include "engine/FontCatalog.h"
#include "engine/FrameCompositor.h"
#include "engine/MediaThumbnail.h"
#include "engine/AudioFileWriter.h"
#include "engine/DeepFilterDenoiser.h"
#include "engine/MatteWriter.h"
#include "engine/AudioOnsets.h"
#include "engine/MediaWaveform.h"
#include "engine/FaceLandmarker.h"
#include "engine/FaceTrack.h"
#include "engine/ReverseProxyCache.h"
#include "engine/ReverseRenderer.h"
#include "engine/Sam2Segmenter.h"
#include "engine/StickerCatalog.h"
#include "SegmentImageStore.h"
#include "engine/TransitionCatalog.h"
#include "engine/WhisperTranscriber.h"
#include "mcp/McpCatalog.h"
#include "mcp/McpJson.h"
#include "mcp/McpServer.h"

#include <QBuffer>
#include <QClipboard>
#include <QColor>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QCursor>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QSettings>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QDebug>
#include <QUuid>
#include <QVector>
#include <QtConcurrent>
#include <QtMath>
#include <algorithm>
#include <climits>
#include <cmath>
#include <optional>
#include <utility>

namespace {
QHash<QString, QString> defaultShortcuts();

QCursor timelineTrimCursor(int side, int heightPx)
{
    // Premiere Selection-tool trim pointer: vertical bar sized to the
    // clip/track, with a solid triangle pointing toward the clip interior.
    const int h = qBound(18, heightPx, 160);
    const int w = 22;
    QPixmap pixmap(w, h);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Hotspot sits on the bar so it lines up with the clip edge.
    const qreal barX = side < 0 ? 14.0 : 7.0;
    const qreal top = 1.0;
    const qreal bot = h - 2.0;
    const qreal midY = h * 0.5;
    // Arrow points into the clip (right on left edge, left on right edge).
    const qreal tipX = side < 0 ? w - 2.0 : 2.0;
    const qreal baseX = side < 0 ? barX + 1.0 : barX - 1.0;
    const qreal arrowH = qBound(5.0, h * 0.18, 9.0);

    QPainterPath arrow;
    arrow.moveTo(tipX, midY);
    arrow.lineTo(baseX, midY - arrowH);
    arrow.lineTo(baseX, midY + arrowH);
    arrow.closeSubpath();

    // White outline (Premiere contrast on dark/light filmstrips).
    painter.setPen(QPen(QColor(255, 255, 255), 3.2, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin));
    painter.drawLine(QPointF(barX, top), QPointF(barX, bot));
    painter.setPen(QPen(QColor(255, 255, 255), 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(QColor(255, 255, 255));
    painter.drawPath(arrow);

    // Black fill — Premiere’s classic trim pointer.
    painter.setPen(QPen(QColor(20, 20, 20), 1.5, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin));
    painter.drawLine(QPointF(barX, top), QPointF(barX, bot));
    painter.setBrush(QColor(20, 20, 20));
    painter.setPen(Qt::NoPen);
    painter.drawPath(arrow);

    return QCursor(pixmap, qRound(barX), qRound(midY));
}
}

AppController::~AppController()
{
    if (m_mcp)
        m_mcp->stop();
    // ~QUndoStack clears the stack, which emits indexChanged into the lambda
    // below — but by then the members it touches (m_selection, the models) are
    // already gone. Cut the signals before any member is destroyed.
    m_undoStack.disconnect(this);
    if (m_timelineTrimCursorSide != 0) {
        QGuiApplication::restoreOverrideCursor();
        m_timelineTrimCursorSide = 0;
        m_timelineTrimCursorHeight = 0;
    }
}

AppController::AppController(AssetLibrary *assetLibrary, QObject *parent)
    : QObject(parent)
    , m_assetLibrary(assetLibrary)
{
    m_project.resetToDefaultTimeline();
    m_project.setAuthor(QSettings().value(QStringLiteral("authorName")).toString());
    if (m_assetLibrary)
        m_assetLibrary->setProject(&m_project);

    m_timelineModel.setProject(&m_project);
    m_clipListModel.setProject(&m_project);

    // selectedClipData reflects the current clip's live values, so it must
    // refresh on both selection changes and any edit to the timeline (e.g. a
    // WYSIWYG preview drag emits only tracksChanged). This keeps the Clip
    // Properties panel in sync with the preview in both directions.
    connect(this, &AppController::selectionChanged, this, &AppController::selectedClipDataChanged);
    connect(this, &AppController::selectionChanged, this, &AppController::editCapabilitiesChanged);
    connect(this, &AppController::tracksChanged, this, &AppController::editCapabilitiesChanged);
    connect(this, &AppController::tracksChanged, this, &AppController::selectedClipDataChanged);
    if (m_assetLibrary) {
        connect(m_assetLibrary, &AssetLibrary::assetMetadataChanged, this,
                &AppController::editCapabilitiesChanged);
    }

    connect(&m_filmstripTiles, &FilmstripTileCache::tileReady, this,
            &AppController::filmstripTileReady);
    connect(&m_waveformBlocks, &WaveformBlockCache::rangeReady, this,
            &AppController::waveformRangeReady);

    if (m_assetLibrary) {
        connect(m_assetLibrary, &AssetLibrary::assetSourceProbed, this,
                &AppController::finalizeAssetReplace);
    }

    m_undoStack.setUndoLimit(kMaxUndoSteps);

    m_mcp = std::make_unique<drift::mcp::McpServer>(this);
    connect(m_mcp.get(), &drift::mcp::McpServer::runningChanged, this,
            &AppController::mcpRunningChanged);
    connect(m_mcp.get(), &drift::mcp::McpServer::errorChanged, this, &AppController::mcpErrorChanged);
    connect(&m_undoStack, &QUndoStack::indexChanged, this, &AppController::undoStackChanged);
    connect(&m_undoStack, &QUndoStack::indexChanged, this, [this] {
        m_timelineModel.refresh();
        m_clipListModel.refresh();
        // Undo/redo swaps the whole project, including the asset table the
        // media bin reads through; without this an undone removal leaves the
        // model with a stale row count.
        if (m_assetLibrary)
            m_assetLibrary->syncToProject();
        normalizeSelection();
        setDirty(true);
        emit tracksChanged();
        emit bookmarksChanged();
        emit workAreaChanged();
        emit projectNameChanged();
        emit selectionChanged();
        emit backgroundChanged();
    });

    // The speed-curve window's player is independent of the timeline; it only ever reports on
    // the one clip being retimed.
    connect(&m_speedCurvePlayer, &ClipPreviewPlayer::frameChanged, this, [this] {
        ++m_speedCurveRevision;
        emit speedCurveFrameChanged();
    });
    connect(&m_speedCurvePlayer, &ClipPreviewPlayer::frameSizeChanged, this,
            &AppController::speedCurveFrameChanged);
    connect(&m_speedCurvePlayer, &ClipPreviewPlayer::positionChanged, this,
            &AppController::speedCurvePositionChanged);
    connect(&m_speedCurvePlayer, &ClipPreviewPlayer::playingChanged, this,
            &AppController::speedCurvePlayingChanged);
    connect(&m_speedCurvePlayer, &ClipPreviewPlayer::durationChanged, this,
            &AppController::speedCurveChanged);

    m_playback.setProject(&m_project);
    connect(&m_playback, &PlaybackEngine::playheadUsChanged, this, [this](quint64 us) {
        if (!m_playing)
            return;
        const drift::TimeUs newUs = static_cast<drift::TimeUs>(us);
        if (m_playheadUs == newUs)
            return;
        m_playheadUs = newUs;
        emit playheadSecondsChanged();
    });
    connect(&m_playback, &PlaybackEngine::playingChanged, this, [this] {
        if (!m_playback.isPlaying() && m_playing) {
            m_playing = false;
            emit playingChanged();
        }
    });
    connect(this, &AppController::tracksChanged, this, [this] {
        m_timelineModel.refresh();
        m_clipListModel.refresh();
        m_playback.setProject(&m_project);
        emit selectedTransitionDataChanged();
    });
    connect(this, &AppController::selectionChanged, this, [this] {
        m_clipListModel.setTrackIndex(m_selectedTrack >= 0 ? m_selectedTrack : 0);
    });

    m_shortcuts = defaultShortcuts();
    QSettings settings;
    settings.beginGroup(QStringLiteral("shortcuts"));
    for (auto it = m_shortcuts.begin(); it != m_shortcuts.end(); ++it) {
        const QString stored = settings.value(it.key(), it.value()).toString();
        if (!stored.isEmpty())
            it.value() = stored;
    }
    settings.endGroup();
    m_guidesEnabled = settings.value(QStringLiteral("preview/guidesEnabled"), false).toBool();
    m_guideType = settings.value(QStringLiteral("preview/guideType"), QStringLiteral("thirds")).toString();
    m_loopWorkAreaEnabled = settings.value(QStringLiteral("playback/loopWorkArea"), false).toBool();
    m_playback.setLoopWorkArea(m_loopWorkAreaEnabled);
    // Off by default: with it on, nudging a clip while the playhead sits anywhere writes a
    // keyframe, and an animation appears where the user only meant to reposition something.
    m_autoKeyEnabled = settings.value(QStringLiteral("editor/autoKeyEnabled"), false).toBool();
    m_reopenLastProject = settings.value(QStringLiteral("editor/reopenLastProject"), false).toBool();
    // Unset means the user has never toggled the theme, so the UI keeps tracking the OS.
    const QVariant storedDarkMode = settings.value(QStringLiteral("ui/darkMode"));
    m_darkModeOverridden = storedDarkMode.isValid();
    m_darkModePreferred = storedDarkMode.toBool();
    // Likewise unset means the workspace still follows the canvas orientation.
    const QVariant storedWorkspace = settings.value(QStringLiteral("ui/workspaceLayout"));
    m_workspaceLayoutOverridden = storedWorkspace.isValid();
    if (m_workspaceLayoutOverridden) {
        m_workspaceLayoutPreferred = storedWorkspace.toString() == QStringLiteral("portrait")
            ? QStringLiteral("portrait")
            : QStringLiteral("landscape");
    }
    loadAssetFavorites();

    // Periodically snapshot unsaved work to a recovery file so a crash doesn't
    // lose progress. The file is removed only when the user saves, loads another
    // project, starts fresh, or discards recovery — not on a clean quit, so the
    // next launch can always ask whether to restore.
    m_autosaveTimer = new QTimer(this);
    m_autosaveTimer->setInterval(kAutosaveIntervalMs);
    connect(m_autosaveTimer, &QTimer::timeout, this, [this] {
        if (m_dirty)
            writeRecoveryFile();
    });
    m_autosaveTimer->start();
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
        // Remember the open project so opt-in reopen can load a clean .drift next launch.
        QSettings().setValue(QStringLiteral("lastSessionPath"), m_currentProjectPath);
        if (m_dirty)
            writeRecoveryFile();
    });

    detectRecoveryFile();
    sweepExtractionDirs();
}

// Every packaged project ever opened leaves its media unpacked under <AppData>/projects/<id>. Drop
// the ones no project in the recents list can still be pointing at.
void AppController::sweepExtractionDirs()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        return;
    QDir root(QDir(base).filePath(QStringLiteral("projects")));
    if (!root.exists())
        return;

    QSet<QString> live;
    for (const QVariant &entry : recentProjects()) {
        const QString path = entry.toMap().value(QStringLiteral("path")).toString();
        QString error;
        if (const auto info = drift::bundle::readManifest(path, &error))
            live.insert(info->projectId);
    }

    const QFileInfoList dirs = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &dir : dirs) {
        if (!live.contains(dir.fileName()))
            QDir(dir.absoluteFilePath()).removeRecursively();
    }
}

namespace {

// Whether a baked sidecar carries the contour loops the makeup effects need. A track written
// before contours existed still drives every warp effect, so it cannot simply be treated as
// missing — but the beauty packages will pass straight through it, and saying so is what stops
// that reading as a broken effect.
//
// loadFaceTrackCached is a hash lookup once the file has been parsed, so this is cheap enough for
// the clip map; only the first valid frame is inspected, since a bake never mixes the two.
bool faceTrackHasContours(const QString &path)
{
    if (path.isEmpty())
        return false;
    const auto track = drift::loadFaceTrackCached(path);
    if (!track)
        return false;
    for (const drift::FaceTrackFrame &frame : track->frames) {
        for (const drift::FaceAnchors &face : frame.faces) {
            if (face.valid)
                return face.hasContours;
        }
    }
    return false;
}

QVariantMap transitionToMap(const drift::Track &track, const drift::Transition &t);
QVariantMap maskToMap(const drift::Mask &m);
drift::Mask maskFromMap(const QVariantMap &m);

int findTransitionPartnerIndex(const drift::Track &track, int fromIndex)
{
    if (fromIndex < 0 || fromIndex >= track.clips.size())
        return -1;

    const drift::Clip &fromClip = track.clips.at(fromIndex);
    int best = -1;
    drift::TimeUs bestStart = std::numeric_limits<drift::TimeUs>::max();
    for (int i = 0; i < track.clips.size(); ++i) {
        if (i == fromIndex)
            continue;
        const drift::Clip &candidate = track.clips.at(i);
        if (!drift::clipsEligibleForTransition(fromClip, candidate))
            continue;
        if (candidate.timelineStart < bestStart) {
            bestStart = candidate.timelineStart;
            best = i;
        }
    }
    return best;
}

bool trackAllowsTransitions(drift::TrackType type)
{
    return type == drift::TrackType::Video || type == drift::TrackType::Shape
           || type == drift::TrackType::Text;
}

void syncOverlapTransitions(drift::Project &project)
{
    for (drift::Track &track : project.tracks()) {
        if (!trackAllowsTransitions(track.type))
            continue;

        QList<int> order;
        order.reserve(track.clips.size());
        for (int i = 0; i < track.clips.size(); ++i)
            order.append(i);
        std::sort(order.begin(), order.end(), [&track](int a, int b) {
            const drift::Clip &ca = track.clips.at(a);
            const drift::Clip &cb = track.clips.at(b);
            if (ca.timelineStart != cb.timelineStart)
                return ca.timelineStart < cb.timelineStart;
            return ca.id < cb.id;
        });

        for (int i = 0; i + 1 < order.size(); ++i) {
            const int fromIndex = order.at(i);
            const int toIndex = order.at(i + 1);
            const drift::Clip &fromClip = track.clips.at(fromIndex);
            const drift::Clip &toClip = track.clips.at(toIndex);
            if (!drift::clipsPhysicallyOverlap(fromClip, toClip))
                continue;

            const drift::TimeUs overlapUs = drift::physicalOverlapDurationUs(fromClip, toClip);
            if (overlapUs < drift::secondsToUs(0.05))
                continue;

            drift::Transition *existing = nullptr;
            for (drift::Transition &transition : track.transitions) {
                if (transition.fromClipId == fromClip.id && transition.toClipId == toClip.id) {
                    existing = &transition;
                    break;
                }
            }

            if (existing) {
                existing->durationUs = overlapUs;
                continue;
            }

            drift::Transition transition;
            transition.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            transition.fromClipId = fromClip.id;
            transition.toClipId = toClip.id;
            transition.kindId = QStringLiteral("crossfade");
            transition.durationUs = overlapUs;
            track.transitions.append(transition);
        }

        for (int i = track.transitions.size() - 1; i >= 0; --i) {
            const drift::Transition &transition = track.transitions.at(i);
            const drift::Clip *fromClip = drift::clipById(track, transition.fromClipId);
            const drift::Clip *toClip = drift::clipById(track, transition.toClipId);
            if (!fromClip || !toClip || !drift::clipsEligibleForTransition(*fromClip, *toClip))
                track.transitions.removeAt(i);
        }
    }
}

} // namespace

QVariantList AppController::tracks() const
{
    QVariantList result;
    result.reserve(m_project.tracks().size());

    for (const drift::Track &track : m_project.tracks()) {
        QVariantList clips;
        clips.reserve(track.clips.size());

        for (const drift::Clip &clip : track.clips)
            clips.append(clipToMap(clip));

        QVariantList transitions;
        transitions.reserve(track.transitions.size());
        for (const drift::Transition &transition : track.transitions)
            transitions.append(transitionToMap(track, transition));

        result.append(QVariantMap{
            {QStringLiteral("type"), drift::trackTypeToString(track.type)},
            {QStringLiteral("clips"), clips},
            {QStringLiteral("transitions"), transitions},
            {QStringLiteral("muted"), track.muted},
            {QStringLiteral("hidden"), track.hidden},
            {QStringLiteral("showWaveform"), track.showWaveform},
            {QStringLiteral("heightScale"), track.heightScale},
        });
    }

    return result;
}

namespace {

void applyTextAnimationPatch(drift::TextAnimation *anim, const QVariantMap &m)
{
    if (m.isEmpty())
        return;
    if (m.contains(QStringLiteral("kind")))
        anim->kind = drift::textAnimKindFromString(m.value(QStringLiteral("kind")).toString());
    if (m.contains(QStringLiteral("duration")))
        anim->durationUs = drift::secondsToUs(qBound(0.0, m.value(QStringLiteral("duration")).toDouble(), 10.0));
    if (m.contains(QStringLiteral("ease")))
        anim->ease = drift::textEaseFromString(m.value(QStringLiteral("ease")).toString());
    if (m.contains(QStringLiteral("unit")))
        anim->unit = drift::textAnimUnitFromString(m.value(QStringLiteral("unit")).toString());
    if (m.contains(QStringLiteral("stagger")))
        anim->staggerUs = drift::secondsToUs(qBound(0.0, m.value(QStringLiteral("stagger")).toDouble(), 2.0));
    if (m.contains(QStringLiteral("order")))
        anim->order = drift::textAnimOrderFromString(m.value(QStringLiteral("order")).toString());
}

QVariantMap textAnimationToMap(const drift::TextAnimation &a)
{
    return {
        {QStringLiteral("kind"), drift::textAnimKindToString(a.kind)},
        {QStringLiteral("duration"), drift::usToSeconds(a.durationUs)},
        {QStringLiteral("ease"), drift::textEaseToString(a.ease)},
        {QStringLiteral("unit"), drift::textAnimUnitToString(a.unit)},
        {QStringLiteral("stagger"), drift::usToSeconds(a.staggerUs)},
        {QStringLiteral("order"), drift::textAnimOrderToString(a.order)},
    };
}

QVariantMap textHighlightToMap(const drift::TextHighlight &h)
{
    return {
        {QStringLiteral("enabled"), h.enabled},
        {QStringLiteral("color"), h.color.name(QColor::HexArgb)},
        {QStringLiteral("padding"), h.padding},
        {QStringLiteral("radius"), h.radius},
    };
}

void applyTextHighlightPatch(drift::TextHighlight *highlight, const QVariantMap &m)
{
    if (m.contains(QStringLiteral("enabled")))
        highlight->enabled = m.value(QStringLiteral("enabled")).toBool();
    if (m.contains(QStringLiteral("color")))
        highlight->color = QColor(m.value(QStringLiteral("color")).toString());
    if (m.contains(QStringLiteral("padding")))
        highlight->padding = qMax(0.0, m.value(QStringLiteral("padding")).toDouble());
    if (m.contains(QStringLiteral("radius")))
        highlight->radius = qMax(0.0, m.value(QStringLiteral("radius")).toDouble());
}

QVariantMap wordAccentToMap(const drift::WordAccent &a)
{
    return {
        {QStringLiteral("rule"), drift::wordAccentRuleToString(a.rule)},
        {QStringLiteral("n"), a.n},
        {QStringLiteral("phase"), a.phase},
        {QStringLiteral("colorEnabled"), a.colorEnabled},
        {QStringLiteral("color"), a.color.name(QColor::HexArgb)},
        {QStringLiteral("sizeScale"), a.sizeScale},
        {QStringLiteral("outlineEnabled"), a.outlineEnabled},
        {QStringLiteral("outlineWidth"), a.outlineWidth},
        {QStringLiteral("outlineColor"), a.outlineColor.name(QColor::HexArgb)},
        {QStringLiteral("highlight"), textHighlightToMap(a.highlight)},
    };
}

void applyWordAccentPatch(drift::WordAccent *accent, const QVariantMap &m)
{
    if (m.contains(QStringLiteral("rule")))
        accent->rule = drift::wordAccentRuleFromString(m.value(QStringLiteral("rule")).toString());
    if (m.contains(QStringLiteral("n")))
        accent->n = qBound(1, m.value(QStringLiteral("n")).toInt(), 16);
    if (m.contains(QStringLiteral("phase")))
        accent->phase = qBound(0, m.value(QStringLiteral("phase")).toInt(), 16);
    if (m.contains(QStringLiteral("colorEnabled")))
        accent->colorEnabled = m.value(QStringLiteral("colorEnabled")).toBool();
    if (m.contains(QStringLiteral("color")))
        accent->color = QColor(m.value(QStringLiteral("color")).toString());
    if (m.contains(QStringLiteral("sizeScale")))
        accent->sizeScale = qBound(0.25, m.value(QStringLiteral("sizeScale")).toDouble(), 4.0);
    if (m.contains(QStringLiteral("outlineEnabled")))
        accent->outlineEnabled = m.value(QStringLiteral("outlineEnabled")).toBool();
    if (m.contains(QStringLiteral("outlineWidth")))
        accent->outlineWidth = qMax(0.0, m.value(QStringLiteral("outlineWidth")).toDouble());
    if (m.contains(QStringLiteral("outlineColor")))
        accent->outlineColor = QColor(m.value(QStringLiteral("outlineColor")).toString());
    applyTextHighlightPatch(&accent->highlight, m.value(QStringLiteral("highlight")).toMap());
}

QVariantMap textStyleToMap(const drift::TextStyle &s)
{
    return {
        {QStringLiteral("packId"), s.packId},
        {QStringLiteral("fontFamily"), s.fontFamily},
        {QStringLiteral("pixelSize"), s.pixelSize},
        {QStringLiteral("fontWeight"), s.fontWeight},
        {QStringLiteral("italic"), s.italic},
        {QStringLiteral("color"), s.color.name(QColor::HexArgb)},
        {QStringLiteral("align"), drift::textAlignToString(s.align)},
        {QStringLiteral("valign"), drift::textVAlignToString(s.valign)},
        {QStringLiteral("wordWrap"), s.wordWrap},
        {QStringLiteral("lineHeight"), s.lineHeight},
        {QStringLiteral("letterSpacing"), s.letterSpacing},
        {QStringLiteral("outlineEnabled"), s.outlineEnabled},
        {QStringLiteral("outlineWidth"), s.outlineWidth},
        {QStringLiteral("outlineColor"), s.outlineColor.name(QColor::HexArgb)},
        {QStringLiteral("shadowEnabled"), s.shadowEnabled},
        {QStringLiteral("shadowOffsetX"), s.shadowOffsetX},
        {QStringLiteral("shadowOffsetY"), s.shadowOffsetY},
        {QStringLiteral("shadowBlur"), s.shadowBlur},
        {QStringLiteral("shadowOpacity"), s.shadowOpacity},
        {QStringLiteral("shadowColor"), s.shadowColor.name(QColor::HexArgb)},
        {QStringLiteral("glowEnabled"), s.glowEnabled},
        {QStringLiteral("glowColor"), s.glowColor.name(QColor::HexArgb)},
        {QStringLiteral("glowRadius"), s.glowRadius},
        {QStringLiteral("glowOpacity"), s.glowOpacity},
        {QStringLiteral("boxEnabled"), s.boxEnabled},
        {QStringLiteral("boxColor"), s.boxColor.name(QColor::HexArgb)},
        {QStringLiteral("boxPadding"), s.boxPadding},
        {QStringLiteral("boxRadius"), s.boxRadius},
        {QStringLiteral("wordHighlight"), textHighlightToMap(s.wordHighlight)},
        {QStringLiteral("underlineEnabled"), s.underlineEnabled},
        {QStringLiteral("underlineColor"), s.underlineColor.name(QColor::HexArgb)},
        {QStringLiteral("underlineWidth"), s.underlineWidth},
        {QStringLiteral("underlineOffset"), s.underlineOffset},
        {QStringLiteral("accent"), wordAccentToMap(s.accent)},
        {QStringLiteral("animIn"), textAnimationToMap(s.animIn)},
        {QStringLiteral("animOut"), textAnimationToMap(s.animOut)},
    };
}

QVariantList subtitleCuesToMap(const QList<drift::SubtitleCue> &cues)
{
    QVariantList out;
    for (const drift::SubtitleCue &cue : cues) {
        out.append(QVariantMap{
            {QStringLiteral("start"), drift::usToSeconds(cue.startUs)},
            {QStringLiteral("end"), drift::usToSeconds(cue.endUs)},
            {QStringLiteral("text"), cue.text},
        });
    }
    return out;
}

QList<drift::SubtitleCue> subtitleCuesFromMap(const QVariantList &list)
{
    QList<drift::SubtitleCue> cues;
    cues.reserve(list.size());
    for (const QVariant &value : list) {
        const QVariantMap map = value.toMap();
        drift::SubtitleCue cue;
        cue.startUs = drift::secondsToUs(map.value(QStringLiteral("start")).toDouble());
        cue.endUs = drift::secondsToUs(map.value(QStringLiteral("end")).toDouble());
        cue.text = map.value(QStringLiteral("text")).toString();
        cues.append(cue);
    }
    drift::sortSubtitleCues(cues);
    return cues;
}

constexpr drift::TimeUs kDefaultSubtitleCueDurationUs = 3 * drift::kUsPerSecond;

QVariantMap shapeStyleToMap(const drift::ShapeStyle &s)
{
    return {
        {QStringLiteral("kind"), drift::shapeKindToString(s.kind)},
        {QStringLiteral("fillKind"), drift::shapeFillKindToString(s.fillKind)},
        {QStringLiteral("fill"), s.fill.name(QColor::HexArgb)},
        {QStringLiteral("fillSecondary"), s.fillSecondary.name(QColor::HexArgb)},
        {QStringLiteral("gradientAngle"), s.gradientAngle},
        {QStringLiteral("stroke"), s.stroke.name(QColor::HexArgb)},
        {QStringLiteral("strokeWidth"), s.strokeWidth},
        {QStringLiteral("strokeStyle"), drift::shapeStrokeStyleToString(s.strokeStyle)},
        {QStringLiteral("cornerRadius"), s.cornerRadius},
        {QStringLiteral("points"), s.points},
        {QStringLiteral("innerRatio"), s.innerRatio},
        {QStringLiteral("headSize"), s.headSize},
        {QStringLiteral("thickness"), s.thickness},
        {QStringLiteral("tailX"), s.tailX},
        {QStringLiteral("tailSize"), s.tailSize},
    };
}

QVariantMap maskToMap(const drift::Mask &m)
{
    QVariantList points;
    for (const QPointF &pt : m.points)
        points.append(QVariantList{pt.x(), pt.y()});

    return {
        {QStringLiteral("shape"), drift::maskShapeToString(m.shape)},
        {QStringLiteral("x"), m.x},
        {QStringLiteral("y"), m.y},
        {QStringLiteral("w"), m.w},
        {QStringLiteral("h"), m.h},
        {QStringLiteral("rotation"), m.rotation},
        {QStringLiteral("feather"), m.feather},
        {QStringLiteral("invert"), m.invert},
        {QStringLiteral("points"), points},
    };
}

drift::Mask maskFromMap(const QVariantMap &m)
{
    drift::Mask mask;
    mask.shape = drift::maskShapeFromString(m.value(QStringLiteral("shape")).toString());
    mask.x = m.value(QStringLiteral("x"), mask.x).toDouble();
    mask.y = m.value(QStringLiteral("y"), mask.y).toDouble();
    mask.w = m.value(QStringLiteral("w"), mask.w).toDouble();
    mask.h = m.value(QStringLiteral("h"), mask.h).toDouble();
    mask.rotation = m.value(QStringLiteral("rotation"), mask.rotation).toDouble();
    mask.feather = m.value(QStringLiteral("feather"), mask.feather).toDouble();
    mask.invert = m.value(QStringLiteral("invert"), mask.invert).toBool();
    const QVariantList points = m.value(QStringLiteral("points")).toList();
    for (const QVariant &value : points) {
        const QVariantList pair = value.toList();
        if (pair.size() >= 2)
            mask.points.append(QPointF(pair.at(0).toDouble(), pair.at(1).toDouble()));
    }
    return mask;
}

QVariantMap transitionToMap(const drift::Track &track, const drift::Transition &t)
{
    drift::TimeUs startUs = 0;
    drift::TimeUs endUs = 0;
    const bool hasWindow = drift::transitionWindow(track, t, startUs, endUs);
    const drift::Clip *fromClip = drift::clipById(track, t.fromClipId);
    const drift::Clip *toClip = drift::clipById(track, t.toClipId);
    const bool overlapping = fromClip && toClip && drift::clipsPhysicallyOverlap(*fromClip, *toClip);
    const drift::TimeUs durationUs = hasWindow ? (endUs - startUs) : t.durationUs;

    const TransitionPresetEntry *def = transitionDefForId(t.kindId);

    // Current value per parameter, so the properties panel can build its sliders.
    QVariantList params;
    if (def) {
        const QMap<QString, QVariant> resolved = resolvedTransitionParameters(t, *def);
        for (const drift::EffectParamSpec &p : def->meta.parameters) {
            params.append(QVariantMap{
                {QStringLiteral("key"), p.key},
                {QStringLiteral("label"), p.label},
                {QStringLiteral("min"), p.min},
                {QStringLiteral("max"), p.max},
                {QStringLiteral("default"), p.defaultValue},
                {QStringLiteral("isBoolean"), p.isBoolean()},
                {QStringLiteral("type"), p.typeName()},
                {QStringLiteral("value"), resolved.value(p.key, p.defaultValue)},
            });
        }
    }

    return {
        {QStringLiteral("id"), t.id},
        {QStringLiteral("fromClipId"), t.fromClipId},
        {QStringLiteral("toClipId"), t.toClipId},
        {QStringLiteral("kind"), t.kindId},
        {QStringLiteral("duration"), drift::usToSeconds(durationUs)},
        {QStringLiteral("start"), hasWindow ? drift::usToSeconds(startUs) : 0.0},
        {QStringLiteral("end"), hasWindow ? drift::usToSeconds(endUs) : 0.0},
        {QStringLiteral("overlapping"), overlapping},
        {QStringLiteral("label"), def ? def->meta.displayName : t.kindId},
        {QStringLiteral("params"), params},
    };
}

bool isSyntheticTimelineClip(drift::ClipType type)
{
    return type == drift::ClipType::Text || type == drift::ClipType::Subtitle
           || type == drift::ClipType::Shape || type == drift::ClipType::Image;
}

drift::TimeUs syntheticClipMaxDurationUs()
{
    return drift::secondsToUs(300.0);
}

void syncSyntheticSourceRange(drift::Clip &clip)
{
    clip.srcIn = 0;
    clip.srcOut = qMin(clip.sourceSpanUs(), syntheticClipMaxDurationUs());
}

bool clipAcceptsPreviewTransform(const drift::Clip &clip)
{
    return clip.type == drift::ClipType::Shape || clip.type == drift::ClipType::Image
           || clip.type == drift::ClipType::Text || clip.type == drift::ClipType::Subtitle
           || clip.type == drift::ClipType::Video;
}

double clipTransformValue(const drift::KeyframeTrack<double> &track, drift::TimeUs relative, double defaultValue)
{
    if (track.isEmpty())
        return defaultValue;
    return track.evaluateAt(relative);
}

drift::ShapeStyle shapeStyleForKind(const QString &shapeId)
{
    if (const drift::ShapeCatalogEntry *entry = drift::shapeCatalogEntry(shapeId))
        return entry->style;

    drift::ShapeStyle style;
    style.kind = drift::shapeKindFromString(shapeId);
    return style;
}

// Effect parameters are addressed as "fx.<effectIndex>.<paramKey>" so the whole generic keyframe
// API — set / remove / move / interpolation / keyframe-strip selection — reaches them without a
// parallel set of invokables. Indices match the effectIndex the effect invokables already take.
bool parseEffectProp(const QString &prop, int *effectIndex, QString *paramKey)
{
    if (!prop.startsWith(QLatin1String("fx.")))
        return false;
    const int dot = prop.indexOf(QLatin1Char('.'), 3);
    if (dot < 0 || dot == 3 || dot + 1 >= prop.size())
        return false;
    bool ok = false;
    const int index = QStringView(prop).mid(3, dot - 3).toInt(&ok);
    if (!ok || index < 0)
        return false;
    *effectIndex = index;
    *paramKey = prop.mid(dot + 1);
    return true;
}

drift::KeyframeTrack<double> *transformTrackForProp(drift::Clip &clip, const QString &prop)
{
    if (prop == QStringLiteral("opacity"))
        return &clip.opacity;
    if (prop == QStringLiteral("x") || prop == QStringLiteral("posX"))
        return &clip.transformX;
    if (prop == QStringLiteral("y") || prop == QStringLiteral("posY"))
        return &clip.transformY;
    if (prop == QStringLiteral("width") || prop == QStringLiteral("scale"))
        return &clip.transformW;
    if (prop == QStringLiteral("height"))
        return &clip.transformH;
    if (prop == QStringLiteral("rotation"))
        return &clip.rotation;
    if (prop == QStringLiteral("volume"))
        return &clip.volume;
    return nullptr;
}

// createIfMissing must stay false on read paths: an effect param's track is created lazily, and
// QMap::operator[] would otherwise insert an empty one into the project behind a const accessor.
drift::KeyframeTrack<double> *keyframeTrackForProp(drift::Clip &clip, const QString &prop,
                                                   bool createIfMissing)
{
    int effectIndex = -1;
    QString paramKey;
    if (!parseEffectProp(prop, &effectIndex, &paramKey))
        return transformTrackForProp(clip, prop);

    if (effectIndex >= clip.effects.size())
        return nullptr;

    drift::Effect &effect = clip.effects[effectIndex];

    // Colour params are not animatable: the track type is double all the way down. Refusing here
    // as well as withholding the `prop` in effectToMap means a hand-edited project cannot conjure
    // one either.
    if (const EffectPresetEntry *def = effectDefForId(effect.catalogId)) {
        for (const drift::EffectParamSpec &spec : def->meta.parameters) {
            if (spec.key == paramKey && spec.isColor())
                return nullptr;
        }
    }

    if (createIfMissing)
        return &effect.paramKeyframes[paramKey];
    const auto it = effect.paramKeyframes.find(paramKey);
    return it == effect.paramKeyframes.end() ? nullptr : &it.value();
}

const drift::KeyframeTrack<double> *keyframeTrackForProp(const drift::Clip &clip, const QString &prop)
{
    return keyframeTrackForProp(const_cast<drift::Clip &>(clip), prop, /*createIfMissing=*/false);
}

// Well-formed enough to keep in the keyframe-strip selection. Effect params can't be validated
// against a clip here — the selection outlives any particular clip — so only the shape is checked.
bool isKnownKeyframeProp(const QString &prop)
{
    int effectIndex = -1;
    QString paramKey;
    if (parseEffectProp(prop, &effectIndex, &paramKey))
        return true;
    drift::Clip probe;
    return transformTrackForProp(probe, prop) != nullptr;
}

// Transform prop names are lower-case by convention, but effect param keys are verbatim
// identifiers from the effect manifest and are frequently camelCase ("u_blurRadius"), so the
// compound form must survive normalization untouched.
QString normalizeKeyframeProp(const QString &prop)
{
    const QString trimmed = prop.trimmed();
    return trimmed.startsWith(QLatin1String("fx.")) ? trimmed : trimmed.toLower();
}

constexpr drift::TimeUs kKeyframeToleranceUs = drift::kUsPerSecond / 30;

// force=true (diamond click) always writes. Otherwise auto-key or an existing
// key at/near the playhead is required. Empty tracks get a constant key at 0
// when auto-key is off so static layout edits still work; a track with a single
// key retargets that key from anywhere so single-keyframe clips still edit freely.
bool writeKeyframeValue(drift::KeyframeTrack<double> &track, drift::TimeUs relative, double value,
                        bool autoKey, bool force)
{
    // With the animation switched off the property reads as its first key, so that is the key an
    // ordinary value edit has to land on — anything else would type into a value nothing shows.
    if (!track.enabled() && !track.isEmpty() && !force) {
        track.setKeyframe(track.keyframes().firstKey(), value);
        return true;
    }
    if (force || autoKey) {
        track.setKeyframe(relative, value);
        return true;
    }
    if (track.isEmpty()) {
        track.setKeyframe(0, value);
        return true;
    }
    if (track.keyframes().size() == 1) {
        track.setKeyframe(track.keyframes().firstKey(), value);
        return true;
    }
    const drift::TimeUs nearest = track.nearestKeyframe(relative, kKeyframeToleranceUs);
    if (nearest < 0)
        return false;
    track.setKeyframe(nearest, value);
    return true;
}

// Writes `value` for `prop`, returning false when the write was refused (auto-key off with no key
// near the playhead to retarget).
//
// Effect params differ from transform props in one way: they already have a static home in
// Effect::parameters, so silently minting a keyframe track on an ordinary slider drag would make
// every param "animated". An un-keyed param therefore only becomes animated on an explicit
// diamond click (force) or with auto-key on; otherwise the write lands on the static value. Keyed
// params mirror into the static value too, so deleting the last key leaves the param where the
// user last put it rather than snapping back to the catalog default.
bool writeClipPropValue(drift::Clip &clip, const QString &prop, drift::TimeUs relative, double value,
                        bool autoKey, bool force)
{
    int effectIndex = -1;
    QString paramKey;
    if (!parseEffectProp(prop, &effectIndex, &paramKey)) {
        drift::KeyframeTrack<double> *kt = transformTrackForProp(clip, prop);
        return kt && writeKeyframeValue(*kt, relative, value, autoKey, force);
    }

    if (effectIndex >= clip.effects.size())
        return false;

    drift::Effect &effect = clip.effects[effectIndex];
    const auto existing = effect.paramKeyframes.constFind(paramKey);
    const bool keyed = existing != effect.paramKeyframes.constEnd() && !existing->isEmpty();
    if (!keyed && !force && !autoKey) {
        effect.parameters.insert(paramKey, value);
        return true;
    }
    if (!writeKeyframeValue(effect.paramKeyframes[paramKey], relative, value, autoKey, force))
        return false;
    effect.parameters.insert(paramKey, value);
    return true;
}

QVariantList keyframeListToVariant(const drift::KeyframeTrack<double> &track, drift::TimeUs timelineStart)
{
    QVariantList out;
    for (auto it = track.keyframes().constBegin(); it != track.keyframes().constEnd(); ++it) {
        const drift::Keyframe<double> &key = it.value();
        // Handle dx reaches QML in seconds, matching `seconds`, so the curve editor can work
        // in one unit throughout instead of converting on every drag.
        out.append(QVariantMap{
            {QStringLiteral("seconds"), drift::usToSeconds(timelineStart + it.key())},
            {QStringLiteral("value"), key.value},
            {QStringLiteral("inDx"), drift::usToSeconds(static_cast<drift::TimeUs>(key.inDx))},
            {QStringLiteral("inDy"), key.inDy},
            {QStringLiteral("outDx"), drift::usToSeconds(static_cast<drift::TimeUs>(key.outDx))},
            {QStringLiteral("outDy"), key.outDy},
            {QStringLiteral("corner"), key.corner},
            {QStringLiteral("hold"), key.hold},
            {QStringLiteral("easing"), drift::interpolationToString(track.easingAt(it.key()))},
            {QStringLiteral("custom"), track.hasCustomTangents(it.key())},
        });
    }
    return out;
}

QVariantMap keyframeTrackToMap(const drift::KeyframeTrack<double> &track, drift::TimeUs timelineStart)
{
    // `interpolation` is per-key now and travels inside each point; the map keeps its shape so
    // the inspector's bindings do not all have to change at once.
    return {
        {QStringLiteral("points"), keyframeListToVariant(track, timelineStart)},
    };
}

// `effectIndex` and `timelineStart` are only needed to describe the params' keyframe tracks: the
// inspector addresses them as "fx.<index>.<key>", and key times are reported on the timeline.
QVariantMap effectToMap(const drift::Effect &effect, int effectIndex, drift::TimeUs timelineStart)
{
    const EffectPresetEntry *def = effectDefForId(effect.catalogId);
    QVariantList params;
    if (def) {
        for (const drift::EffectParamSpec &paramDef : def->meta.parameters) {
            QVariant value = effect.parameters.value(paramDef.key);
            if (!value.isValid())
                value = paramDef.defaultVariant();

            // `value` stays the static value; the row falls back to it whenever the track is empty.
            QVariantMap param{
                {QStringLiteral("key"), paramDef.key},
                {QStringLiteral("label"), paramDef.label},
                {QStringLiteral("min"), paramDef.min},
                {QStringLiteral("max"), paramDef.max},
                {QStringLiteral("isBoolean"), paramDef.isBoolean()},
                {QStringLiteral("type"), paramDef.typeName()},
                {QStringLiteral("value"), value},
            };
            // Colours carry no `prop`, which is what the keyframe strip addresses a parameter by.
            // Withholding it makes an animated colour structurally unreachable rather than merely
            // discouraged — the whole keyframe stack is typed double, and packing a shade into one
            // would interpolate through desaturated mud.
            if (!paramDef.isColor()) {
                param.insert(QStringLiteral("prop"),
                             QStringLiteral("fx.%1.%2").arg(effectIndex).arg(paramDef.key));
                param.insert(QStringLiteral("keyframes"),
                             keyframeTrackToMap(effect.paramKeyframes.value(paramDef.key),
                                                timelineStart));
            }
            params.append(param);
        }
    }
    return {
        {QStringLiteral("catalogId"), effect.catalogId},
        {QStringLiteral("label"), def ? def->meta.displayName : effect.name},
        {QStringLiteral("params"), params},
        {QStringLiteral("compositorOnly"), def ? def->meta.compositorOnly : false},
        {QStringLiteral("enabled"), effect.enabled},
    };
}

// Audio effects use the same per-instance shape as video effects, but read from the audio catalog.
QVariantMap audioEffectToMap(const drift::Effect &effect)
{
    const AudioEffectEntry *def = audioEffectDefForId(effect.catalogId);
    QVariantList params;
    if (def) {
        for (const drift::EffectParamSpec &paramDef : def->parameters) {
            QVariant value = effect.parameters.value(paramDef.key);
            if (!value.isValid()) {
                value = paramDef.defaultVariant();
            }
            params.append(QVariantMap{
                {QStringLiteral("key"), paramDef.key},
                {QStringLiteral("label"), paramDef.label},
                {QStringLiteral("min"), paramDef.min},
                {QStringLiteral("max"), paramDef.max},
                {QStringLiteral("isBoolean"), paramDef.isBoolean()},
                {QStringLiteral("type"), paramDef.typeName()},
                {QStringLiteral("value"), value},
            });
        }
    }
    return {
        {QStringLiteral("catalogId"), effect.catalogId},
        {QStringLiteral("label"), def ? def->displayName : effect.name},
        {QStringLiteral("icon"), def ? def->icon : QString()},
        {QStringLiteral("thumbnailPath"), def ? def->thumbnailPath : QString()},
        {QStringLiteral("params"), params},
        {QStringLiteral("missing"), def == nullptr},
        {QStringLiteral("enabled"), effect.enabled},
    };
}

QVariantMap keyframesToMap(const drift::Clip &clip)
{
    return {
        {QStringLiteral("opacity"), keyframeTrackToMap(clip.opacity, clip.timelineStart)},
        {QStringLiteral("x"), keyframeTrackToMap(clip.transformX, clip.timelineStart)},
        {QStringLiteral("y"), keyframeTrackToMap(clip.transformY, clip.timelineStart)},
        {QStringLiteral("width"), keyframeTrackToMap(clip.transformW, clip.timelineStart)},
        {QStringLiteral("height"), keyframeTrackToMap(clip.transformH, clip.timelineStart)},
        {QStringLiteral("rotation"), keyframeTrackToMap(clip.rotation, clip.timelineStart)},
        {QStringLiteral("volume"), keyframeTrackToMap(clip.volume, clip.timelineStart)},
    };
}

// Keyframe times are clip-relative, so a clip that changes duration would leave its animation
// sliding against the picture. Both clips cover the same source range, so each key is carried
// across through the moment of source it was sitting on.
template<typename T>
void remapKeyframeTrack(drift::KeyframeTrack<T> &dst, const drift::KeyframeTrack<T> &src,
                        const drift::Clip &from, const drift::Clip &to)
{
    if (src.isEmpty())
        return;

    const drift::TimeUs span = from.srcOut - from.srcIn;
    drift::KeyframeTrack<T> out;
    // Tangents travel inside each key, so remapping the times carries the shape with them.
    for (auto it = src.keyframes().constBegin(); it != src.keyframes().constEnd(); ++it) {
        const drift::TimeUs sourceOffset =
            from.hasSpeedCurve() ? from.speedCurve.sourceOffsetForTimelineOffset(it.key(), span)
                                 : from.sourceDeltaForTimelineDelta(it.key());
        out.setKeyframe(to.speedCurve.timelineOffsetForSourceOffset(sourceOffset, span), it.value());
    }
    dst = out;
}

// How much source a trim of `delta` timeline µs eats into the clip's edge.
//
// A ramp is normalised over the clip's source range, so a trim rescales it and the duration has
// to be re-derived afterwards (syncDurationFromSpeedCurve) rather than simply shifted by delta.
// Extending past an edge is outside anything the curve describes, so the rate at that end stands
// in for it.
drift::TimeUs trimSourceDelta(const drift::Clip &clip, drift::TimeUs delta, bool extending,
                              bool atTail)
{
    if (!clip.hasSpeedCurve())
        return clip.sourceDeltaForTimelineDelta(delta);

    const drift::TimeUs span = clip.srcOut - clip.srcIn;
    if (extending) {
        const double edgeSpeed = clip.speedCurve.speedAt(atTail ? 1.0 : 0.0);
        return static_cast<drift::TimeUs>(llround(static_cast<double>(delta) * edgeSpeed));
    }
    return clip.speedCurve.sourceOffsetForTimelineOffset(delta, span);
}

void remapKeyframesForRetime(drift::Clip &dst, const drift::Clip &src)
{
    remapKeyframeTrack(dst.opacity, src.opacity, src, dst);
    remapKeyframeTrack(dst.transformX, src.transformX, src, dst);
    remapKeyframeTrack(dst.transformY, src.transformY, src, dst);
    remapKeyframeTrack(dst.transformW, src.transformW, src, dst);
    remapKeyframeTrack(dst.transformH, src.transformH, src, dst);
    remapKeyframeTrack(dst.rotation, src.rotation, src, dst);
    remapKeyframeTrack(dst.volume, src.volume, src, dst);

    for (int i = 0; i < dst.effects.size() && i < src.effects.size(); ++i) {
        const QMap<QString, drift::KeyframeTrack<double>> &srcParams = src.effects.at(i).paramKeyframes;
        for (auto it = srcParams.constBegin(); it != srcParams.constEnd(); ++it)
            remapKeyframeTrack(dst.effects[i].paramKeyframes[it.key()], it.value(), src, dst);
    }
}

void setClipLayoutPixels(drift::Clip &clip, double x, double y, double w, double h)
{
    clip.transformX = {};
    clip.transformY = {};
    clip.transformW = {};
    clip.transformH = {};
    clip.transformX.setKeyframe(0, x);
    clip.transformY.setKeyframe(0, y);
    clip.transformW.setKeyframe(0, qMax(1.0, w));
    clip.transformH.setKeyframe(0, qMax(1.0, h));
}

void fitClipLayoutToCanvas(drift::Clip &clip, int mediaW, int mediaH, int canvasW, int canvasH)
{
    canvasW = qMax(1, canvasW);
    canvasH = qMax(1, canvasH);
    if (mediaW <= 0 || mediaH <= 0) {
        setClipLayoutPixels(clip, 0, 0, canvasW, canvasH);
        return;
    }
    const double scale = qMin(static_cast<double>(canvasW) / mediaW, static_cast<double>(canvasH) / mediaH);
    setClipLayoutPixels(clip, 0, 0, mediaW * scale, mediaH * scale);
}

void applyAssetLayout(drift::Clip &clip, const QVariantMap &asset, int canvasW, int canvasH)
{
    int mediaW = asset.value(QStringLiteral("width")).toInt();
    int mediaH = asset.value(QStringLiteral("height")).toInt();
    const int rotation = asset.value(QStringLiteral("rotationDegrees")).toInt();
    if (rotation == 90 || rotation == 270)
        std::swap(mediaW, mediaH);
    fitClipLayoutToCanvas(clip, mediaW, mediaH, canvasW, canvasH);
}

// shapeAspect is the catalog's default width/height for the shape being added; a square shape must
// get a square box, since every shape now simply fills its layout rect.
void applyDefaultVisualLayout(drift::Clip &clip, int canvasW, int canvasH, double shapeAspect = 1.6)
{
    canvasW = qMax(1, canvasW);
    canvasH = qMax(1, canvasH);
    if (clip.type == drift::ClipType::Shape) {
        const double aspect = shapeAspect > 0.01 ? shapeAspect : 1.6;
        double h = canvasH * 0.30;
        double w = h * aspect;
        if (w > canvasW * 0.6) {
            w = canvasW * 0.6;
            h = w / aspect;
        }
        setClipLayoutPixels(clip, 0, 0, w, h);
        return;
    }
    if (clip.type == drift::ClipType::Text) {
        setClipLayoutPixels(clip, 0, canvasH * 0.35, canvasW, canvasH * 0.30);
        return;
    }
    if (clip.type == drift::ClipType::Subtitle) {
        setClipLayoutPixels(clip, 0, canvasH * 0.78, canvasW, canvasH * 0.18);
        return;
    }
    // Stickers / generic images without metadata: modest top-left box.
    const double side = qMin(canvasW, canvasH) * 0.25;
    setClipLayoutPixels(clip, 0, 0, side, side);
}

bool assetHasAudioStreams(const drift::Project &project, AssetLibrary *library, const QString &assetId)
{
    const drift::MediaAsset *asset = project.asset(assetId);
    if (!asset)
        return false;

    if (asset->hasAudioKnown)
        return asset->hasAudio;
    if (asset->channels > 0 || asset->sampleRate > 0)
        return true;
    if (library)
        library->ensureAudioPresence(assetId);
    return false;
}

bool clipHasEmbeddedAudio(const drift::Project &project, AssetLibrary *library, const drift::Clip &clip)
{
    if (clip.type != drift::ClipType::Video || clip.suppressEmbeddedAudio || clip.path.isEmpty())
        return false;
    return assetHasAudioStreams(project, library, clip.assetId);
}

drift::Clip makeAudioCompanionFromVideo(const drift::Clip &videoClip, const QString &linkId = {})
{
    drift::Clip audio;
    audio.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    audio.linkId = linkId;
    audio.assetId = videoClip.assetId;
    audio.type = drift::ClipType::Audio;
    audio.name = videoClip.name;
    audio.path = videoClip.path;
    audio.timelineStart = videoClip.timelineStart;
    audio.timelineDuration = videoClip.timelineDuration;
    audio.srcIn = videoClip.srcIn;
    audio.srcOut = videoClip.srcOut;
    audio.speed = videoClip.speed;
    audio.reverse = videoClip.reverse;
    audio.fadeInUs = videoClip.fadeInUs;
    audio.fadeOutUs = videoClip.fadeOutUs;
    audio.fadeCurve = videoClip.fadeCurve;
    audio.fadeShape = videoClip.fadeShape;
    audio.volume = videoClip.volume;
    return audio;
}

// Split embedded audio onto the audio track (video keeps picture only).
// CapCut-style: the new audio clip stays linked to the video so they move together
// until the user explicitly unlinks.
bool detachEmbeddedAudioFromVideo(drift::Project &project, AssetLibrary *library, drift::Clip &videoClip)
{
    if (!clipHasEmbeddedAudio(project, library, videoClip))
        return false;

    const QString linkId = videoClip.linkId.isEmpty()
        ? QUuid::createUuid().toString(QUuid::WithoutBraces)
        : videoClip.linkId;
    videoClip.linkId = linkId;
    videoClip.suppressEmbeddedAudio = true;

    const int audioTrack = drift::ensureTrackForClipType(project, drift::ClipType::Audio, false);
    project.tracks()[audioTrack].clips.append(makeAudioCompanionFromVideo(videoClip, linkId));
    return true;
}

QList<QPair<int, int>> selectionWithLinkedPartners(const drift::Project &project, int trackIndex, int clipIndex)
{
    QList<QPair<int, int>> pairs;
    if (trackIndex < 0 || trackIndex >= project.tracks().size())
        return pairs;
    const drift::Track &track = project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return pairs;

    pairs.append(qMakePair(trackIndex, clipIndex));
    for (const drift::ClipRef &ref : drift::linkedPartners(project, track.clips.at(clipIndex))) {
        const QPair<int, int> linked(ref.trackIndex, ref.clipIndex);
        if (!pairs.contains(linked))
            pairs.append(linked);
    }
    return pairs;
}

void syncLinkedPartnersFrom(drift::Project &project, const drift::Clip &source,
                            const QSet<QString> &skipClipIds = {})
{
    for (const drift::ClipRef &ref : drift::linkedPartners(project, source)) {
        const drift::Clip &partner = project.tracks().at(ref.trackIndex).clips.at(ref.clipIndex);
        if (skipClipIds.contains(partner.id))
            continue;
        drift::syncLinkedTiming(project.tracks()[ref.trackIndex].clips[ref.clipIndex], source);
    }
}

void splitLinkedPartnerAt(drift::Project &project, const drift::Clip &sourceHead, drift::TimeUs playheadUs,
                          const QString &tailLinkId)
{
    if (sourceHead.linkId.isEmpty())
        return;

    for (const drift::ClipRef &ref : drift::linkedPartners(project, sourceHead)) {
        drift::Track &track = project.tracks()[ref.trackIndex];
        if (ref.clipIndex < 0 || ref.clipIndex >= track.clips.size())
            continue;

        drift::Clip &partner = track.clips[ref.clipIndex];
        if (!partner.containsTime(playheadUs) || playheadUs == partner.timelineStart)
            continue;

        const drift::TimeUs offset = playheadUs - partner.timelineStart;
        drift::Clip partnerTail;
        if (!drift::splitClipAtOffset(partner, partnerTail, offset))
            continue;

        partnerTail.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (!tailLinkId.isEmpty())
            partnerTail.linkId = tailLinkId;
        else
            drift::assignSplitLinkIds(partner, partnerTail);
        track.clips.insert(ref.clipIndex + 1, partnerTail);
    }
}

void expandSelectionWithLinkedPartners(const drift::Project &project, QList<QPair<int, int>> &pairs)
{
    QList<QPair<int, int>> expanded = pairs;
    for (const QPair<int, int> &pair : pairs) {
        if (pair.first < 0 || pair.first >= project.tracks().size())
            continue;
        const drift::Track &track = project.tracks().at(pair.first);
        if (pair.second < 0 || pair.second >= track.clips.size())
            continue;

        for (const drift::ClipRef &ref : drift::linkedPartners(project, track.clips.at(pair.second))) {
            const QPair<int, int> linked(ref.trackIndex, ref.clipIndex);
            if (!expanded.contains(linked))
                expanded.append(linked);
        }
    }
    pairs = expanded;
}

QHash<QString, QString> defaultShortcuts()
{
    return {
        {QStringLiteral("newProject"), QStringLiteral("Ctrl+N")},
        {QStringLiteral("open"), QStringLiteral("Ctrl+O")},
        {QStringLiteral("save"), QStringLiteral("Ctrl+S")},
        {QStringLiteral("playPause"), QStringLiteral("Space")},
        {QStringLiteral("delete"), QStringLiteral("Delete")},
        {QStringLiteral("undo"), QStringLiteral("Ctrl+Z")},
        {QStringLiteral("redo"), QStringLiteral("Ctrl+Shift+Z")},
        {QStringLiteral("clearSelection"), QStringLiteral("Escape")},
        {QStringLiteral("selectAll"), QStringLiteral("Ctrl+A")},
        {QStringLiteral("duplicate"), QStringLiteral("Ctrl+D")},
        {QStringLiteral("split"), QStringLiteral("S")},
        {QStringLiteral("merge"), QStringLiteral("Ctrl+M")},
        {QStringLiteral("unlink"), QStringLiteral("Ctrl+Shift+U")},
        {QStringLiteral("separateAudio"), QStringLiteral("Ctrl+Shift+S")},
        {QStringLiteral("copy"), QStringLiteral("Ctrl+C")},
        {QStringLiteral("cut"), QStringLiteral("Ctrl+X")},
        {QStringLiteral("paste"), QStringLiteral("Ctrl+V")},
        {QStringLiteral("nudgeLeft"), QStringLiteral("Alt+Left")},
        {QStringLiteral("nudgeRight"), QStringLiteral("Alt+Right")},
        {QStringLiteral("toggleGuides"), QStringLiteral("G")},
        {QStringLiteral("toggleBookmark"), QStringLiteral("M")},
        {QStringLiteral("nextBookmark"), QStringLiteral("Shift+M")},
        {QStringLiteral("previousBookmark"), QStringLiteral("Ctrl+Shift+M")},
        {QStringLiteral("markIn"), QStringLiteral("I")},
        {QStringLiteral("markOut"), QStringLiteral("O")},
        {QStringLiteral("goToIn"), QStringLiteral("Shift+I")},
        {QStringLiteral("goToOut"), QStringLiteral("Shift+O")},
        {QStringLiteral("clearInOut"), QStringLiteral("Alt+X")},
        {QStringLiteral("toggleLoop"), QStringLiteral("Ctrl+L")},
        // The timeline tool modes live in QML, so Main.qml intercepts these rather
        // than triggerAction dispatching them. They are registered all the same, so
        // they show up in the shortcut list and can be rebound — hardcoded in
        // TimelinePanel they were neither.
        {QStringLiteral("selectTool"), QStringLiteral("V")},
        {QStringLiteral("bladeTool"), QStringLiteral("B")},
    };
}

} // namespace

QVariantMap AppController::clipToMap(const drift::Clip &clip) const
{
    QVariantList effects;
    for (int i = 0; i < clip.effects.size(); ++i)
        effects.append(effectToMap(clip.effects.at(i), i, clip.timelineStart));

    QVariantList audioEffects;
    for (const drift::Effect &effect : clip.audioEffects)
        audioEffects.append(audioEffectToMap(effect));

    QVariantList fadeShape;
    for (const QPointF &pt : clip.fadeShape.points()) {
        fadeShape.append(QVariantMap{
            {QStringLiteral("t"), pt.x()},
            {QStringLiteral("g"), pt.y()},
        });
    }

    // Full source length backs the filmstrip's timestamp mapping (the strip is sampled across
    // the whole source, so tiles need the total to place srcIn/srcOut within it).
    const drift::MediaAsset *sourceAsset = m_project.asset(clip.assetId);

    return {
        {QStringLiteral("id"), clip.id},
        {QStringLiteral("name"), clip.name},
        {QStringLiteral("path"), clip.path},
        {QStringLiteral("kind"), drift::clipTypeToString(clip.type)},
        {QStringLiteral("thumbnailPath"), clip.thumbnailPath},
        {QStringLiteral("filmstripPath"), clip.filmstripPath},
        {QStringLiteral("textContent"), clip.textContent},
        {QStringLiteral("textStyle"), textStyleToMap(clip.textStyle)},
        {QStringLiteral("subtitleCues"), subtitleCuesToMap(clip.subtitleCues)},
        {QStringLiteral("shapeStyle"), shapeStyleToMap(clip.shapeStyle)},
        {QStringLiteral("blendMode"), drift::blendModeToString(clip.blendMode)},
        {QStringLiteral("speed"), clip.speed},
        {QStringLiteral("hasSpeedCurve"), clip.hasSpeedCurve()},
        {QStringLiteral("reverse"), clip.reverse},
        {QStringLiteral("flipH"), clip.flipH},
        {QStringLiteral("flipV"), clip.flipV},
        {QStringLiteral("mask"), maskToMap(clip.mask)},
        {QStringLiteral("hasFaceTrack"), !clip.faceTrackPath.isEmpty()},
        {QStringLiteral("faceTrackHasContours"), faceTrackHasContours(clip.faceTrackPath)},
        {QStringLiteral("start"), drift::usToSeconds(clip.timelineStart)},
        {QStringLiteral("duration"), drift::usToSeconds(clip.timelineDuration)},
        {QStringLiteral("inPoint"), drift::usToSeconds(clip.srcIn)},
        {QStringLiteral("outPoint"), drift::usToSeconds(clip.srcOut)},
        {QStringLiteral("sourceDuration"), sourceAsset ? drift::usToSeconds(sourceAsset->durationUs) : 0.0},
        {QStringLiteral("assetId"), clip.assetId},
        {QStringLiteral("assetIndex"), assetIndexForClip(clip)},
        {QStringLiteral("linked"), !clip.linkId.isEmpty()},
        {QStringLiteral("volume"), clip.volume.isEmpty() ? 1.0 : clip.volume.evaluateAt(0)},
        {QStringLiteral("fadeIn"), drift::usToSeconds(clip.fadeInUs)},
        {QStringLiteral("fadeOut"), drift::usToSeconds(clip.fadeOutUs)},
        {QStringLiteral("fadeCurve"), drift::fadeCurveToString(clip.fadeCurve)},
        {QStringLiteral("fadeShape"), fadeShape},
        {QStringLiteral("animIn"), QVariantMap{
             {QStringLiteral("kind"), drift::clipAnimKindToString(clip.animIn.kind)},
             {QStringLiteral("duration"), drift::usToSeconds(clip.animIn.durationUs)},
             {QStringLiteral("ease"), drift::clipAnimEaseToString(clip.animIn.ease)},
             {QStringLiteral("curve"), drift::fadeCurveToString(clip.animIn.curve)},
         }},
        {QStringLiteral("animOut"), QVariantMap{
             {QStringLiteral("kind"), drift::clipAnimKindToString(clip.animOut.kind)},
             {QStringLiteral("duration"), drift::usToSeconds(clip.animOut.durationUs)},
             {QStringLiteral("ease"), drift::clipAnimEaseToString(clip.animOut.ease)},
             {QStringLiteral("curve"), drift::fadeCurveToString(clip.animOut.curve)},
         }},
        {QStringLiteral("effects"), effects},
        {QStringLiteral("audioEffects"), audioEffects},
        {QStringLiteral("keyframes"), keyframesToMap(clip)},
    };
}

int AppController::clipCountForAsset(int assetIndex) const
{
    if (!m_assetLibrary)
        return 0;
    const QString assetId = m_assetLibrary->assetIdAt(assetIndex);
    if (assetId.isEmpty())
        return 0;

    int count = 0;
    for (const drift::Track &track : m_project.tracks()) {
        for (const drift::Clip &clip : track.clips) {
            if (clip.assetId == assetId)
                ++count;
        }
    }
    return count;
}

bool AppController::removeAsset(int assetIndex)
{
    // Clips keep their own copy of the source path, so an orphaned assetId
    // would still play but silently lose trim-past-the-cut, merge and
    // separate-audio. Refuse instead; the caller reports the usage count.
    if (!m_assetLibrary || clipCountForAsset(assetIndex) > 0)
        return false;

    const drift::Project before = m_project;
    if (!m_assetLibrary->removeAssetAt(assetIndex))
        return false;

    // Rows after the removed one shift down, so any index captured at drag
    // start now points at the wrong asset.
    setDraggingAssetIndex(-1);
    pushProjectEdit(before, tr("Media removed"));
    return true;
}

bool AppController::renameAsset(int assetIndex, const QString &name)
{
    if (!m_assetLibrary)
        return false;

    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return false;

    const QVariantMap current = m_assetLibrary->assetAt(assetIndex);
    if (current.isEmpty() || current.value(QStringLiteral("name")).toString() == trimmed)
        return false;

    const drift::Project before = m_project.detachedCopy();
    if (!m_assetLibrary->setAssetName(assetIndex, trimmed))
        return false;

    pushProjectEdit(before, tr("Rename media"));
    finishEdit(tr("Media renamed"));
    return true;
}

bool AppController::replaceAssetSource(int assetIndex, const QUrl &url)
{
    if (!m_assetLibrary)
        return false;

    const QString assetId = m_assetLibrary->assetIdAt(assetIndex);
    if (assetId.isEmpty())
        return false;

    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    const QFileInfo fileInfo(path);
    if (path.isEmpty() || !fileInfo.isFile()) {
        emit assetReplaceFinished(false, tr("That file could not be read."), 0);
        return false;
    }

    // Two bin rows sharing a path would break import de-duplication, which resolves a path to
    // whichever row holds it first.
    const QString absolutePath = fileInfo.absoluteFilePath();
    const int existing = m_assetLibrary->indexOfPath(absolutePath);
    if (existing >= 0 && existing != assetIndex) {
        emit assetReplaceFinished(false, tr("That file is already in this project."), 0);
        return false;
    }

    if (!m_assetLibrary->startReplaceProbe(assetIndex, absolutePath)) {
        emit assetReplaceFinished(false, tr("That file could not be read."), 0);
        return false;
    }

    // Reading a file off a slow disk is the one part of this the user waits on with nothing to
    // show for it, so the row it belongs to goes busy until the probe lands.
    m_replacingAssetId = assetId;
    emit replacingAssetIdChanged();
    return true;
}

bool AppController::exportAssetImage(int assetIndex, const QUrl &url)
{
    if (!m_assetLibrary)
        return false;

    const QVariantMap asset = m_assetLibrary->assetAt(assetIndex);
    if (asset.value(QStringLiteral("kind")).toString()
        != drift::mediaKindToString(drift::MediaKind::Image)) {
        return false;
    }

    const QString sourcePath = asset.value(QStringLiteral("path")).toString();
    if (sourcePath.isEmpty() || !QFileInfo(sourcePath).isFile())
        return false;

    // The path the picker returned is written to exactly as given — see FileDialogs::saveFile for
    // why appending a suffix to it would write somewhere the document portal never registered.
    const QString destPath = url.isLocalFile() ? url.toLocalFile() : QString();
    if (destPath.isEmpty())
        return false;

    const QString suffix = QFileInfo(destPath).suffix().toLower();
    const bool jpeg = suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg");

    // Same format in and out: copy the bytes rather than decode and re-encode, so a freeze frame
    // saved as PNG comes out pixel-for-pixel what the compositor produced.
    if (!jpeg && suffix == QFileInfo(sourcePath).suffix().toLower()) {
        QFile::remove(destPath); // The picker already confirmed the overwrite; copy() won't clobber.
        return QFile::copy(sourcePath, destPath);
    }

    QImage image(sourcePath);
    if (image.isNull())
        return false;

    if (jpeg) {
        // JPEG has no alpha, and Qt writes transparent pixels as black without this.
        if (image.hasAlphaChannel())
            image = image.convertToFormat(QImage::Format_RGB32);
        return image.save(destPath, "JPG", 95);
    }

    return image.save(destPath, "PNG");
}

void AppController::finalizeAssetReplace(const QString &assetId, const drift::MediaAsset &filled,
                                         bool ok)
{
    // The probe has landed, so the row stops being busy whatever the outcome below turns out to
    // be — including the refusals, which never touch the project at all.
    if (m_replacingAssetId == assetId) {
        m_replacingAssetId.clear();
        emit replacingAssetIdChanged();
    }

    const drift::MediaAsset *current = m_project.asset(assetId);
    if (!current) {
        // The row was removed while the probe ran.
        emit assetReplaceFinished(false, tr("That media is no longer in this project."), 0);
        return;
    }

    if (!ok) {
        emit assetReplaceFinished(false, tr("That file could not be read."), 0);
        return;
    }

    // A clip's type is fixed when it is created and decides which track it may sit on, so media
    // of another kind would leave e.g. a video clip on a video track with nothing to draw.
    if (filled.kind != current->kind) {
        emit assetReplaceFinished(false,
                                  tr("“%1” is %2, but this slot holds %3.")
                                      .arg(filled.name, drift::mediaKindToString(filled.kind),
                                           drift::mediaKindToString(current->kind)),
                                  0);
        return;
    }

    const QString newName = filled.name;
    const drift::Project before = m_project;
    if (!m_assetLibrary->applyProbedSource(assetId, filled)) {
        emit assetReplaceFinished(false, tr("That media is no longer in this project."), 0);
        return;
    }

    const int adjusted = rebindClipsToAsset(assetId, filled);
    pushProjectEdit(before, tr("Media replaced"));
    finishEdit(tr("Media replaced"));
    emit assetReplaceFinished(true, newName, adjusted);
}

int AppController::rebindClipsToAsset(const QString &assetId, const drift::MediaAsset &asset)
{
    int adjusted = 0;
    for (drift::Track &track : m_project.tracks()) {
        for (drift::Clip &clip : track.clips) {
            if (clip.assetId != assetId)
                continue;

            // Rewriting the clip's own copy of the path is what actually moves the pixels. It
            // also keys the reverse-proxy and audio-block caches, so both fall out of scope on
            // their own rather than needing to be invalidated here.
            clip.path = asset.path;
            clip.thumbnailPath = asset.thumbnailPath;
            clip.filmstripPath = asset.filmstripPath;
            // Landmarks are baked against the old pixels. Left in place they would keep the face
            // warps tracking a face the new footage never had, and render without erroring.
            clip.faceTrackPath.clear();
            clip.faceTrackSrcOffsetUs = 0;

            // Stills have no source range to fit.
            if (asset.durationUs <= 0 || clip.srcOut <= asset.durationUs)
                continue;

            // The framed window starts past the end of the new file, so there is nothing about
            // it worth preserving; rebase to the head and keep as much of the span as fits.
            if (clip.srcIn >= asset.durationUs)
                clip.srcIn = 0;

            if (clip.hasSpeedCurve()) {
                // Curved clips derive their timeline duration from the source range, so the
                // range is clamped first and the ramp re-timed against what is left.
                clip.srcOut = asset.durationUs;
                clip.syncDurationFromSpeedCurve();
            } else {
                // Pulls timelineDuration along with the shortened range, so the clip never
                // addresses frames past the end of its media.
                clip.syncSrcOutFromSpeed(asset.durationUs);
            }
            ++adjusted;
        }
    }
    return adjusted;
}

int AppController::assetIndexForClip(const drift::Clip &clip) const
{
    if (clip.assetId.isEmpty())
        return -1;
    return m_project.assetIndex(clip.assetId);
}

double AppController::playheadSeconds() const
{
    return drift::usToSeconds(m_playheadUs);
}

double AppController::durationSeconds() const
{
    return drift::usToSeconds(m_project.durationUs());
}

QString AppController::projectName() const
{
    return m_project.name();
}

QVariantMap AppController::selectedClipData() const
{
    if (m_selectedTrack < 0 || m_selectedClip < 0)
        return {};

    auto enrich = [this](QVariantMap data, int trackIndex, int clipIndex) -> QVariantMap {
        if (data.isEmpty() || !isValidClipIndex(trackIndex, clipIndex))
            return data;
        const drift::Clip &clip = m_project.tracks().at(trackIndex).clips.at(clipIndex);
        const drift::TimeUs relative = qMax<drift::TimeUs>(0, m_playheadUs - clip.timelineStart);
        data.insert(QStringLiteral("rotationAtPlayhead"),
                    clipTransformValue(clip.rotation, relative, 0.0));
        return data;
    };

    QVariantMap data = enrich(clipAt(m_selectedTrack, m_selectedClip), m_selectedTrack, m_selectedClip);
    if (!data.isEmpty())
        return data;

    // Primary indices can lag m_selection after structural edits; fall back.
    for (const QPair<int, int> &pair : m_selection) {
        data = enrich(clipAt(pair.first, pair.second), pair.first, pair.second);
        if (!data.isEmpty())
            return data;
    }
    return {};
}

QVariantList AppController::selectedClipEffects() const
{
    const QVariantMap clip = selectedClipData();
    return clip.value(QStringLiteral("effects")).toList();
}

QVariantList AppController::selectedClipAudioEffects() const
{
    const QVariantMap clip = selectedClipData();
    return clip.value(QStringLiteral("audioEffects")).toList();
}

QVariantList AppController::selection() const
{
    QVariantList out;
    for (const QPair<int, int> &pair : m_selection) {
        out.append(QVariantMap{
            {QStringLiteral("track"), pair.first},
            {QStringLiteral("clip"), pair.second},
        });
    }
    return out;
}

QVariantList AppController::actions() const
{
    auto action = [this](const QString &id, const QString &label) {
        return QVariantMap{
            {QStringLiteral("id"), id},
            {QStringLiteral("label"), label},
            {QStringLiteral("shortcut"), m_shortcuts.value(id)},
        };
    };

    return {
        action(QStringLiteral("newProject"), tr("New project")),
        action(QStringLiteral("open"), tr("Open project")),
        action(QStringLiteral("save"), tr("Save project")),
        action(QStringLiteral("playPause"), tr("Play/Pause")),
        action(QStringLiteral("delete"), tr("Delete selection")),
        action(QStringLiteral("undo"), tr("Undo")),
        action(QStringLiteral("redo"), tr("Redo")),
        action(QStringLiteral("copy"), tr("Copy selection")),
        action(QStringLiteral("cut"), tr("Cut selection")),
        action(QStringLiteral("paste"), tr("Paste at current time")),
        action(QStringLiteral("duplicate"), tr("Duplicate selected clip")),
        action(QStringLiteral("split"), tr("Split at current time")),
        action(QStringLiteral("merge"), tr("Merge adjacent clips")),
        action(QStringLiteral("separateAudio"), tr("Separate audio")),
        action(QStringLiteral("unlink"), tr("Unlink audio")),
        action(QStringLiteral("clearSelection"), tr("Clear selection")),
        action(QStringLiteral("selectAll"), tr("Select all clips")),
        action(QStringLiteral("nudgeLeft"), tr("Move selection left a little")),
        action(QStringLiteral("nudgeRight"), tr("Move selection right a little")),
        action(QStringLiteral("toggleGuides"), tr("Toggle guides")),
        action(QStringLiteral("toggleBookmark"), tr("Add/remove bookmark at current time")),
        action(QStringLiteral("nextBookmark"), tr("Go to next bookmark")),
        action(QStringLiteral("previousBookmark"), tr("Go to previous bookmark")),
        action(QStringLiteral("markIn"), tr("Mark work area in")),
        action(QStringLiteral("markOut"), tr("Mark work area out")),
        action(QStringLiteral("goToIn"), tr("Go to work area in")),
        action(QStringLiteral("goToOut"), tr("Go to work area out")),
        action(QStringLiteral("clearInOut"), tr("Clear work area")),
        action(QStringLiteral("toggleLoop"), tr("Loop work area playback")),
        action(QStringLiteral("selectTool"), tr("Select tool")),
        action(QStringLiteral("bladeTool"), tr("Cut tool")),
    };
}

void AppController::setPlayheadUs(drift::TimeUs us)
{
    const drift::TimeUs clamped = qBound<drift::TimeUs>(0, us, qMax(m_project.durationUs(), drift::TimeUs{0}));
    if (m_playheadUs == clamped)
        return;

    m_playheadUs = clamped;
    m_playback.setPlayheadUs(clamped);
    emit playheadSecondsChanged();
    if (!m_playing)
        syncTextOverlaySkip();
}

void AppController::setPlayheadSeconds(double seconds)
{
    setPlayheadUs(drift::secondsToUs(seconds));
}

void AppController::setPlaying(bool playing)
{
    if (m_playing == playing)
        return;

    m_playing = playing;
    if (m_playing) {
        const drift::TimeUs durationUs = m_project.durationUs();
        if (m_loopWorkAreaEnabled && m_project.hasWorkArea()) {
            const drift::TimeUs loopIn = m_project.workAreaInUs();
            const drift::TimeUs loopOut = m_project.workAreaOutUs();
            if (m_playheadUs >= loopOut || m_playheadUs < loopIn)
                setPlayheadUs(loopIn);
        } else if (m_playheadUs >= durationUs && durationUs > 0) {
            setPlayheadUs(0);
        }
        m_playback.setPlayheadUs(m_playheadUs);
        m_playback.play();
    } else {
        m_playback.pause();
    }
    emit playingChanged();
    syncTextOverlaySkip();
}

void AppController::togglePlayback()
{
    setPlaying(!m_playback.isPlaying());
}

void AppController::stepFrames(int frames)
{
    if (frames == 0)
        return;

    // Stepping is a paused-only operation: leaving playback running would have the clock overwrite
    // the stepped position on its next tick.
    setPlaying(false);

    const drift::TimeUs step = drift::frameDurationUs(projectFps());
    const int64_t frame = (m_playheadUs + step / 2) / step;
    setPlayheadUs((frame + frames) * step);
}

void AppController::jumpSeconds(double seconds)
{
    setPlayheadUs(m_playheadUs + drift::secondsToUs(seconds));
}

int AppController::keyboardModifiers() const
{
    return static_cast<int>(QGuiApplication::keyboardModifiers());
}

void AppController::setSnapEnabled(bool enabled)
{
    if (m_snapEnabled == enabled)
        return;

    m_snapEnabled = enabled;
    emit snapEnabledChanged();
}

void AppController::setRippleEnabled(bool enabled)
{
    if (m_rippleEnabled == enabled)
        return;
    m_rippleEnabled = enabled;
    emit rippleEnabledChanged();
}

void AppController::setAllowClipOverlap(bool enabled)
{
    if (m_allowClipOverlap == enabled)
        return;
    m_allowClipOverlap = enabled;
    emit allowClipOverlapChanged();
}

void AppController::setDarkModePreference(bool enabled)
{
    if (m_darkModeOverridden && m_darkModePreferred == enabled)
        return;
    m_darkModeOverridden = true;
    m_darkModePreferred = enabled;
    QSettings settings;
    settings.setValue(QStringLiteral("ui/darkMode"), m_darkModePreferred);
    emit darkModePreferenceChanged();
}

void AppController::clearDarkModePreference()
{
    if (!m_darkModeOverridden)
        return;
    m_darkModeOverridden = false;
    QSettings settings;
    settings.remove(QStringLiteral("ui/darkMode"));
    emit darkModePreferenceChanged();
}

void AppController::setWorkspaceLayoutPreference(const QString &layout)
{
    const QString normalized = layout == QStringLiteral("portrait")
        ? QStringLiteral("portrait")
        : QStringLiteral("landscape");
    if (m_workspaceLayoutOverridden && m_workspaceLayoutPreferred == normalized)
        return;
    m_workspaceLayoutOverridden = true;
    m_workspaceLayoutPreferred = normalized;
    QSettings settings;
    settings.setValue(QStringLiteral("ui/workspaceLayout"), m_workspaceLayoutPreferred);
    emit workspaceLayoutPreferenceChanged();
}

void AppController::clearWorkspaceLayoutPreference()
{
    if (!m_workspaceLayoutOverridden)
        return;
    m_workspaceLayoutOverridden = false;
    QSettings settings;
    settings.remove(QStringLiteral("ui/workspaceLayout"));
    emit workspaceLayoutPreferenceChanged();
}

void AppController::setMediaGridMode(bool enabled)
{
    if (m_mediaGridMode == enabled)
        return;
    m_mediaGridMode = enabled;
    emit mediaGridModeChanged();
}

void AppController::setAutoKeyEnabled(bool enabled)
{
    if (m_autoKeyEnabled == enabled)
        return;
    m_autoKeyEnabled = enabled;
    QSettings settings;
    settings.setValue(QStringLiteral("editor/autoKeyEnabled"), m_autoKeyEnabled);
    emit autoKeyEnabledChanged();
}

void AppController::setReopenLastProject(bool enabled)
{
    if (m_reopenLastProject == enabled)
        return;
    m_reopenLastProject = enabled;
    QSettings settings;
    settings.setValue(QStringLiteral("editor/reopenLastProject"), m_reopenLastProject);
    emit reopenLastProjectChanged();
}

void AppController::toggleKeyframeGraphPropertyVisible(const QString &prop)
{
    const QString key = normalizeKeyframeProp(prop);
    if (!isKnownKeyframeProp(key))
        return;
    if (m_keyframeGraphHiddenProperties.contains(key))
        m_keyframeGraphHiddenProperties.removeAll(key);
    else
        m_keyframeGraphHiddenProperties.append(key);
    emit keyframeGraphVisibilityChanged();
}

void AppController::showKeyframeGraphProperty(const QString &prop)
{
    const QString key = normalizeKeyframeProp(prop);
    if (!m_keyframeGraphHiddenProperties.removeAll(key))
        return;
    emit keyframeGraphVisibilityChanged();
}

// Effect props are addressed by index, so removing an effect would leave a hidden flag attached to
// some other effect's parameter. Drop the removed effect's entries and renumber everything above it.
void AppController::dropKeyframeGraphPropertiesForEffect(int removedIndex)
{
    QStringList next;
    for (const QString &prop : std::as_const(m_keyframeGraphHiddenProperties)) {
        int effectIndex = -1;
        QString paramKey;
        if (!parseEffectProp(prop, &effectIndex, &paramKey)) {
            next.append(prop);
            continue;
        }
        if (effectIndex == removedIndex)
            continue;
        next.append(effectIndex > removedIndex
                        ? QStringLiteral("fx.%1.%2").arg(effectIndex - 1).arg(paramKey)
                        : prop);
    }
    if (next == m_keyframeGraphHiddenProperties)
        return;
    m_keyframeGraphHiddenProperties = next;
    emit keyframeGraphVisibilityChanged();
}

// Moving an effect between slots is the same addressing problem as remove: rewrite every
// fx.N.* entry so the hidden set still points at the same parameters after the swap.
void AppController::remapKeyframeGraphPropertiesForEffectMove(int fromIndex, int toIndex)
{
    if (fromIndex == toIndex)
        return;
    QStringList next;
    next.reserve(m_keyframeGraphHiddenProperties.size());
    for (const QString &prop : std::as_const(m_keyframeGraphHiddenProperties)) {
        int effectIndex = -1;
        QString paramKey;
        if (!parseEffectProp(prop, &effectIndex, &paramKey)) {
            next.append(prop);
            continue;
        }
        int mapped = effectIndex;
        if (effectIndex == fromIndex) {
            mapped = toIndex;
        } else if (fromIndex < toIndex) {
            // Shifted left toward the vacated slot.
            if (effectIndex > fromIndex && effectIndex <= toIndex)
                mapped = effectIndex - 1;
        } else {
            // Shifted right toward the vacated slot.
            if (effectIndex >= toIndex && effectIndex < fromIndex)
                mapped = effectIndex + 1;
        }
        next.append(mapped == effectIndex
                        ? prop
                        : QStringLiteral("fx.%1.%2").arg(mapped).arg(paramKey));
    }
    if (next == m_keyframeGraphHiddenProperties)
        return;
    m_keyframeGraphHiddenProperties = next;
    emit keyframeGraphVisibilityChanged();
}

void AppController::setSubtitleEditing(bool editing)
{
    if (m_subtitleEditing == editing)
        return;
    m_subtitleEditing = editing;
    emit subtitleEditingChanged();
}

void AppController::setSelectedSubtitleCue(int index)
{
    if (m_selectedSubtitleCue == index)
        return;
    m_selectedSubtitleCue = index;
    emit selectedSubtitleCueChanged();
}

void AppController::setDraggingAssetIndex(int index)
{
    if (m_draggingAssetIndex == index)
        return;
    m_draggingAssetIndex = index;
    emit draggingAssetIndexChanged();
}

void AppController::setProjectName(const QString &name)
{
    if (m_project.name() == name)
        return;

    m_project.setName(name);
    setDirty(true);
    emit projectNameChanged();
    emit projectMetadataChanged();
}

QVariantMap AppController::projectMetadata() const
{
    return QVariantMap{
        {QStringLiteral("title"), m_project.name()},
        {QStringLiteral("author"), m_project.author()},
        {QStringLiteral("description"), m_project.description()},
        {QStringLiteral("createdAt"), m_project.createdAt().toLocalTime()},
        {QStringLiteral("modifiedAt"), m_project.modifiedAt().toLocalTime()},
    };
}

void AppController::setProjectMetadata(const QString &title, const QString &author,
                                       const QString &description)
{
    const bool nameChanged = m_project.name() != title;
    if (!nameChanged && m_project.author() == author && m_project.description() == description)
        return;

    if (nameChanged)
        m_project.setName(title);
    m_project.setAuthor(author);
    m_project.setDescription(description);

    // The next project starts from whoever the user is now, so they only type it once.
    QSettings().setValue(QStringLiteral("authorName"), author);

    setDirty(true);
    if (nameChanged)
        emit projectNameChanged();
    emit projectMetadataChanged();
}

void AppController::setGuidesEnabled(bool enabled)
{
    if (m_guidesEnabled == enabled)
        return;
    m_guidesEnabled = enabled;
    QSettings settings;
    settings.setValue(QStringLiteral("preview/guidesEnabled"), m_guidesEnabled);
    emit guidesChanged();
}

void AppController::setGuideType(const QString &type)
{
    const QString normalized = type.trimmed().isEmpty() ? QStringLiteral("thirds") : type.trimmed();
    if (m_guideType == normalized)
        return;
    m_guideType = normalized;
    QSettings settings;
    settings.setValue(QStringLiteral("preview/guideType"), m_guideType);
    emit guidesChanged();
}

void AppController::setLastMessage(const QString &message, const QString &severity)
{
    if (m_lastMessage == message && m_lastMessageSeverity == severity)
        return;

    m_lastMessage = message;
    m_lastMessageSeverity = severity;
    emit lastMessageChanged();
}

QUrl AppController::fileUrl(const QString &path) const
{
    if (path.isEmpty())
        return {};
    return QUrl::fromLocalFile(path);
}

QString AppController::imageUrl(const QString &path) const
{
    if (path.isEmpty())
        return {};
    return QStringLiteral("image://drift/") + QString::fromUtf8(QUrl::toPercentEncoding(path));
}

QString AppController::filmstripFrameUrl(const QString &path, int frame, int count) const
{
    if (path.isEmpty())
        return {};
    return imageUrl(path) + QStringLiteral("?frame=%1&count=%2").arg(frame).arg(count);
}

QString AppController::filmstripTileUrl(const QString &path, int level, double index) const
{
    if (path.isEmpty())
        return {};
    const QString tile = m_filmstripTiles.tile(path, level, static_cast<qint64>(index));
    return tile.isEmpty() ? QString() : imageUrl(tile);
}

double AppController::snapTime(double seconds) const
{
    return drift::usToSeconds(drift::snapTime(m_project, drift::secondsToUs(seconds), m_snapEnabled,
                                              m_playheadUs, extraSnapTargets()));
}

drift::TimeUs AppController::clipDurationForAssetIndex(int assetIndex) const
{
    if (!m_assetLibrary)
        return drift::kImageClipDurationUs;
    return drift::clipDurationForAsset(m_project.asset(m_assetLibrary->assetIdAt(assetIndex)));
}

drift::TimeUs AppController::sourceDurationForClip(const drift::Clip &clip) const
{
    return drift::sourceDurationForClip(m_project, clip);
}

QVariantMap AppController::clipAt(int trackIndex, int clipIndex) const
{
    const QList<drift::Track> &tracks = m_project.tracks();
    if (trackIndex < 0 || trackIndex >= tracks.size())
        return {};
    if (clipIndex < 0 || clipIndex >= tracks[trackIndex].clips.size())
        return {};

    return clipToMap(tracks[trackIndex].clips.at(clipIndex));
}

QVariantMap AppController::activeVideoClipAtPlayhead() const
{
    QVariantMap result;
    for (const drift::Track &track : m_project.tracks()) {
        if (track.type != drift::TrackType::Video || track.hidden)
            continue;

        for (const drift::Clip &clip : track.clips) {
            if (clip.containsTime(m_playheadUs))
                result = clipToMap(clip);
        }
    }
    return result;
}

QVariantMap AppController::activeAudioClipAtPlayhead() const
{
    QVariantMap result;
    for (const drift::Track &track : m_project.tracks()) {
        if (track.type != drift::TrackType::Audio || track.muted || track.hidden)
            continue;

        for (const drift::Clip &clip : track.clips) {
            if (clip.containsTime(m_playheadUs))
                result = clipToMap(clip);
        }
    }
    return result;
}

double AppController::sourceTimeForClip(const QVariantMap &clip) const
{
    if (clip.isEmpty())
        return 0.0;

    const double start = clip.value(QStringLiteral("start")).toDouble();
    const double inPoint = clip.value(QStringLiteral("inPoint")).toDouble();
    const double outPoint = clip.value(QStringLiteral("outPoint")).toDouble();
    const double speed = clip.value(QStringLiteral("speed"), 1.0).toDouble();
    const double effectiveSpeed = speed <= 0.0 ? 1.0 : speed;
    const double offset = (playheadSeconds() - start) * effectiveSpeed;
    if (clip.value(QStringLiteral("reverse")).toBool())
        return outPoint - offset;
    return inPoint + offset;
}

double AppController::sourceTimeAtPlayhead() const
{
    return sourceTimeForClip(activeVideoClipAtPlayhead());
}

void AppController::pushProjectEdit(const drift::Project &before, const QString &text)
{
    if (m_mcpUndoSuspended)
        return;
    m_undoStack.push(new drift::ProjectSnapshotCommand(&m_project, before, m_project, text));
}

void AppController::finishEdit(const QString &message)
{
    syncOverlapTransitions(m_project);
    normalizeSelection();
    if (m_selectedTransitionTrack >= 0) {
        const QVariantMap selected = selectedTransitionData();
        if (selected.isEmpty())
            clearTransitionSelection();
    }
    // During playback the engine clock owns the playhead. Seeking here would
    // PlaybackClock::reset() and (historically) stop the clock while audio kept
    // pulling — freezing A/V at one spot after drops like adding an effect.
    if (!m_playback.isPlaying())
        m_playback.setPlayheadUs(m_playheadUs);
    // Underlying audio may have moved; force the subtitle-lane waveform to recompute.
    m_subtitleWaveformCache.clear();
    // Beats are expensive and explicitly requested, so they are dropped only when the mix
    // itself changed — not on every edit. Keyframing is the whole point of having the grid
    // up, and it runs through here too.
    if (!m_beatAnalysis.isEmpty() && audioLayoutFingerprint() != m_beatAudioFingerprint)
        clearBeatAnalysis();
    emit tracksChanged();
    emit selectionChanged();
    emit selectedClipDataChanged();
    // Routine edits used to announce themselves here ("Clip moved", "Split
    // clip", ...), which surfaced as a toast for every drag and cut. The
    // timeline already shows the result and the label lives on in the undo
    // stack, so an edit now only clears a stale warning from an earlier attempt.
    Q_UNUSED(message)
    setLastMessage(QString());
    ++m_mcpEditRevision;
}

void AppController::applyRippleShift(drift::Track &track, int fromClipIndex, drift::TimeUs delta)
{
    if (!m_rippleEnabled || delta == 0)
        return;

    for (int i = fromClipIndex + 1; i < track.clips.size(); ++i)
        track.clips[i].timelineStart = qMax<drift::TimeUs>(0, track.clips[i].timelineStart + delta);
}

void AppController::addClipFromAsset(int assetIndex)
{
    const QVariantMap asset = m_assetLibrary ? m_assetLibrary->assetAt(assetIndex) : QVariantMap{};
    if (asset.isEmpty())
        return;

    const QString kind = asset.value(QStringLiteral("kind")).toString();
    const drift::ClipType clipType = drift::clipTypeFromString(kind);
    const drift::Project before = m_project;
    int trackIndex = drift::defaultTrackForClipType(m_project, clipType);
    if (trackIndex < 0)
        trackIndex = drift::ensureTrackForClipType(m_project, clipType, false);

    m_assetLibrary->ensureMedia(assetIndex);
    const QString thumbnailPath = m_assetLibrary->thumbnailAt(assetIndex);
    const QString filmstripPath = m_assetLibrary->filmstripAt(assetIndex);

    drift::Track &track = m_project.tracks()[trackIndex];
    if (!track.allowsClipType(clipType))
        return;

    const drift::TimeUs duration = clipDurationForAssetIndex(assetIndex);
    const drift::TimeUs start = drift::resolveClipStart(m_project, track, -1, m_playheadUs, duration, m_snapEnabled,
                                                        m_playheadUs);

    drift::Clip clip;
    clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    clip.assetId = m_assetLibrary->assetIdAt(assetIndex);
    clip.type = clipType;
    clip.name = asset.value(QStringLiteral("name")).toString();
    clip.path = asset.value(QStringLiteral("path")).toString();
    clip.thumbnailPath = thumbnailPath;
    clip.filmstripPath = filmstripPath;
    clip.timelineStart = start;
    clip.timelineDuration = duration;
    clip.srcIn = 0;
    clip.srcOut = duration;
    applyAssetLayout(clip, asset, m_project.width(), m_project.height());

    track.clips.append(clip);
    pushProjectEdit(before, tr("Clip added"));
    finishEdit(tr("Clip added"));
    selectClip(trackIndex, track.clips.size() - 1);
}

bool AppController::trackAcceptsAsset(int trackIndex, int assetIndex) const
{
    if (!m_assetLibrary || trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return false;

    const QVariantMap asset = m_assetLibrary->assetAt(assetIndex);
    if (asset.isEmpty())
        return false;

    const drift::ClipType clipType = drift::clipTypeFromString(asset.value(QStringLiteral("kind")).toString());
    return m_project.tracks().at(trackIndex).allowsClipType(clipType);
}

QString AppController::trackTypeForAsset(int assetIndex) const
{
    if (!m_assetLibrary)
        return QStringLiteral("video");

    const QVariantMap asset = m_assetLibrary->assetAt(assetIndex);
    if (asset.isEmpty())
        return QStringLiteral("video");

    const drift::ClipType clipType = drift::clipTypeFromString(asset.value(QStringLiteral("kind")).toString());
    return drift::trackTypeToString(drift::trackTypeForClipType(clipType));
}

void AppController::addClipFromAssetOnNewTrack(int assetIndex, double atSeconds)
{
    addClipFromAssetOnNewTrackAt(assetIndex, 0, atSeconds);
}

void AppController::addClipFromAssetOnNewTrackAt(int assetIndex, int insertIndex, double atSeconds)
{
    if (!m_assetLibrary)
        return;

    const QVariantMap asset = m_assetLibrary->assetAt(assetIndex);
    if (asset.isEmpty())
        return;

    const drift::ClipType clipType = drift::clipTypeFromString(asset.value(QStringLiteral("kind")).toString());
    const drift::Project before = m_project;
    const int trackIndex = drift::insertTrackAboveForClipType(m_project, insertIndex, clipType);

    m_assetLibrary->ensureMedia(assetIndex);
    const QString thumbnailPath = m_assetLibrary->thumbnailAt(assetIndex);
    const QString filmstripPath = m_assetLibrary->filmstripAt(assetIndex);

    drift::Track &track = m_project.tracks()[trackIndex];
    const drift::TimeUs duration = clipDurationForAssetIndex(assetIndex);
    const drift::TimeUs start = drift::resolveClipStart(m_project, track, -1, drift::secondsToUs(atSeconds),
                                                        duration, m_snapEnabled, m_playheadUs);

    drift::Clip clip;
    clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    clip.assetId = m_assetLibrary->assetIdAt(assetIndex);
    clip.type = clipType;
    clip.name = asset.value(QStringLiteral("name")).toString();
    clip.path = asset.value(QStringLiteral("path")).toString();
    clip.thumbnailPath = thumbnailPath;
    clip.filmstripPath = filmstripPath;
    clip.timelineStart = start;
    clip.timelineDuration = duration;
    clip.srcIn = 0;
    clip.srcOut = duration;
    applyAssetLayout(clip, asset, m_project.width(), m_project.height());

    track.clips.append(clip);
    pushProjectEdit(before, tr("Clip added on new track"));
    finishEdit(tr("Clip added on new track"));
    selectClip(trackIndex, track.clips.size() - 1);
}

void AppController::addClipFromAssetAt(int assetIndex, int trackIndex, double atSeconds)
{
    if (!m_assetLibrary || trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    const QVariantMap asset = m_assetLibrary->assetAt(assetIndex);
    if (asset.isEmpty())
        return;

    m_assetLibrary->ensureMedia(assetIndex);
    const QString thumbnailPath = m_assetLibrary->thumbnailAt(assetIndex);
    const QString filmstripPath = m_assetLibrary->filmstripAt(assetIndex);
    const QString kind = asset.value(QStringLiteral("kind")).toString();
    const drift::ClipType clipType = drift::clipTypeFromString(kind);

    drift::Track &track = m_project.tracks()[trackIndex];
    if (!track.allowsClipType(clipType))
        return;

    const drift::Project before = m_project;
    const drift::TimeUs duration = clipDurationForAssetIndex(assetIndex);
    const drift::TimeUs start = drift::resolveClipStart(m_project, track, -1, drift::secondsToUs(atSeconds),
                                                        duration, m_snapEnabled, m_playheadUs);

    drift::Clip clip;
    clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    clip.assetId = m_assetLibrary->assetIdAt(assetIndex);
    clip.type = clipType;
    clip.name = asset.value(QStringLiteral("name")).toString();
    clip.path = asset.value(QStringLiteral("path")).toString();
    clip.thumbnailPath = thumbnailPath;
    clip.filmstripPath = filmstripPath;
    clip.timelineStart = start;
    clip.timelineDuration = duration;
    clip.srcIn = 0;
    clip.srcOut = duration;
    applyAssetLayout(clip, asset, m_project.width(), m_project.height());

    track.clips.append(clip);
    pushProjectEdit(before, tr("Clip added"));
    finishEdit(tr("Clip added"));
    selectClip(trackIndex, track.clips.size() - 1);
}

void AppController::selectClip(int trackIndex, int clipIndex)
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return;

    m_selectedTrack = trackIndex;
    m_selectedClip = clipIndex;
    m_selection = selectionWithLinkedPartners(m_project, trackIndex, clipIndex);
    m_selectedTransitionTrack = -1;
    m_selectedTransitionLeftClip = -1;
    emit selectionChanged();
    emit selectedTransitionDataChanged();
    syncTextOverlaySkip();
}

void AppController::addToSelection(int trackIndex, int clipIndex)
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return;

    for (const QPair<int, int> &pair : selectionWithLinkedPartners(m_project, trackIndex, clipIndex)) {
        if (!m_selection.contains(pair))
            m_selection.append(pair);
    }
    m_selectedTrack = trackIndex;
    m_selectedClip = clipIndex;
    emit selectionChanged();
}

void AppController::setSelection(const QVariantList &pairs)
{
    QList<QPair<int, int>> next;
    for (const QVariant &value : pairs) {
        const QVariantMap map = value.toMap();
        const int trackIndex = map.value(QStringLiteral("track")).toInt();
        const int clipIndex = map.value(QStringLiteral("clip")).toInt();
        // Match click-select: bring linked A/V partners along.
        for (const QPair<int, int> &pair : selectionWithLinkedPartners(m_project, trackIndex, clipIndex)) {
            if (!next.contains(pair))
                next.append(pair);
        }
    }
    m_selection = next;
    m_selectedTransitionTrack = -1;
    m_selectedTransitionLeftClip = -1;
    if (m_selection.isEmpty()) {
        m_selectedTrack = -1;
        m_selectedClip = -1;
    } else {
        m_selectedTrack = m_selection.constLast().first;
        m_selectedClip = m_selection.constLast().second;
    }
    emit selectionChanged();
    emit selectedTransitionDataChanged();
    syncTextOverlaySkip();
}

void AppController::selectAllClips()
{
    QVariantList pairs;
    for (int t = 0; t < m_project.tracks().size(); ++t) {
        const int clipCount = m_project.tracks().at(t).clips.size();
        for (int c = 0; c < clipCount; ++c) {
            pairs.append(QVariantMap{
                {QStringLiteral("track"), t},
                {QStringLiteral("clip"), c},
            });
        }
    }
    setSelection(pairs);
}

void AppController::clearSelection()
{
    if (m_selectedTrack < 0 && m_selectedClip < 0 && m_selection.isEmpty() && m_selectedTransitionTrack < 0)
        return;

    m_selectedTrack = -1;
    m_selectedClip = -1;
    m_selection.clear();
    m_selectedTransitionTrack = -1;
    m_selectedTransitionLeftClip = -1;
    emit selectionChanged();
    emit selectedTransitionDataChanged();
    syncTextOverlaySkip();
}

void AppController::deleteSelectedClip()
{
    if (m_selection.isEmpty() && m_selectedTrack >= 0 && m_selectedClip >= 0)
        m_selection = {qMakePair(m_selectedTrack, m_selectedClip)};
    if (m_selection.isEmpty())
        return;

    const drift::Project before = m_project;
    QList<QPair<int, int>> pairs = m_selection;
    expandSelectionWithLinkedPartners(m_project, pairs);
    QSet<QString> removedClipIds;
    std::sort(pairs.begin(), pairs.end(), [](const QPair<int, int> &a, const QPair<int, int> &b) {
        if (a.first != b.first)
            return a.first > b.first;
        return a.second > b.second;
    });
    for (const QPair<int, int> &pair : pairs) {
        if (!isValidClipIndex(pair.first, pair.second))
            continue;
        removedClipIds.insert(m_project.tracks().at(pair.first).clips.at(pair.second).id);
        m_project.tracks()[pair.first].clips.removeAt(pair.second);
    }
    for (drift::Track &track : m_project.tracks()) {
        for (int i = track.transitions.size() - 1; i >= 0; --i) {
            const drift::Transition &transition = track.transitions.at(i);
            if (removedClipIds.contains(transition.fromClipId) || removedClipIds.contains(transition.toClipId))
                track.transitions.removeAt(i);
        }
    }
    pushProjectEdit(before, tr("Clip deleted"));
    clearSelection();
    finishEdit(tr("Clip deleted"));
}

void AppController::moveClip(int trackIndex, int clipIndex, double newStart)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    const QPair<int, int> requested(trackIndex, clipIndex);
    QList<QPair<int, int>> targets = m_selection.contains(requested) ? m_selection
                                                                      : QList<QPair<int, int>>{requested};
    const drift::Project before = m_project;
    const drift::TimeUs desiredUs = drift::secondsToUs(newStart);
    const drift::TimeUs baseUs = m_project.tracks().at(trackIndex).clips.at(clipIndex).timelineStart;
    const drift::TimeUs delta = desiredUs - baseUs;
    QSet<QString> movedIds;
    for (const QPair<int, int> &pair : targets) {
        if (!isValidClipIndex(pair.first, pair.second))
            continue;
        drift::Clip &clip = m_project.tracks()[pair.first].clips[pair.second];
        clip.timelineStart = qMax<drift::TimeUs>(0, clip.timelineStart + delta);
        movedIds.insert(clip.id);
    }
    if (!m_allowClipOverlap) {
        for (const QPair<int, int> &pair : targets) {
            if (!isValidClipIndex(pair.first, pair.second))
                continue;
            drift::Track &targetTrack = m_project.tracks()[pair.first];
            drift::Clip &clip = targetTrack.clips[pair.second];
            clip.timelineStart = drift::clampClipStartNoOverlap(targetTrack, movedIds, clip.timelineStart,
                                                                clip.timelineDuration);
        }
    }
    for (const QPair<int, int> &pair : targets) {
        if (!isValidClipIndex(pair.first, pair.second))
            continue;
        const drift::Clip &clip = m_project.tracks().at(pair.first).clips.at(pair.second);
        syncLinkedPartnersFrom(m_project, clip, movedIds);
    }
    pushProjectEdit(before, tr("Clip moved"));
    finishEdit(tr("Clip moved"));
}

void AppController::splitAtPlayhead()
{
    const drift::Project before = m_project;
    bool splitAny = false;
    QSet<QString> handledLinkIds;

    for (drift::Track &track : m_project.tracks()) {
        for (int clipIndex = 0; clipIndex < track.clips.size(); ++clipIndex) {
            drift::Clip &clip = track.clips[clipIndex];
            if (!clip.containsTime(m_playheadUs))
                continue;
            if (m_playheadUs == clip.timelineStart)
                continue;
            if (!clip.linkId.isEmpty() && handledLinkIds.contains(clip.linkId))
                continue;

            const drift::TimeUs offset = m_playheadUs - clip.timelineStart;
            drift::Clip tail;
            if (!drift::splitClipAtOffset(clip, tail, offset))
                continue;

            tail.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            const QString tailLinkId = drift::assignSplitLinkIds(clip, tail);
            if (!clip.linkId.isEmpty())
                handledLinkIds.insert(clip.linkId);
            splitLinkedPartnerAt(m_project, clip, m_playheadUs, tailLinkId);
            track.clips.insert(clipIndex + 1, tail);
            splitAny = true;
            ++clipIndex;
        }
    }

    if (splitAny) {
        pushProjectEdit(before, tr("Split at current time"));
        finishEdit(tr("Split at current time"));
    } else {
        setLastMessage(tr("Nothing to split here — move to a clip first"), QStringLiteral("warning"));
    }
}

void AppController::splitClipAt(int trackIndex, int clipIndex, double seconds)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    const drift::TimeUs atUs = drift::secondsToUs(seconds);
    if (!clip.containsTime(atUs) || atUs == clip.timelineStart)
        return;

    const drift::Project before = m_project;
    const drift::TimeUs offset = atUs - clip.timelineStart;
    drift::Clip tail;
    if (!drift::splitClipAtOffset(clip, tail, offset))
        return;

    tail.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString tailLinkId = drift::assignSplitLinkIds(clip, tail);
    splitLinkedPartnerAt(m_project, clip, atUs, tailLinkId);
    track.clips.insert(clipIndex + 1, tail);

    pushProjectEdit(before, tr("Split clip"));
    finishEdit(tr("Split clip"));
}

void AppController::splitClipLeftAt(int trackIndex, int clipIndex, double seconds)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    const drift::TimeUs atUs = drift::secondsToUs(seconds);
    if (!clip.containsTime(atUs) || atUs == clip.timelineStart)
        return;

    const drift::TimeUs offset = atUs - clip.timelineStart;
    const drift::Project before = m_project;

    drift::Clip right;
    if (!drift::splitClipAtOffset(clip, right, offset))
        return;

    right.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    // Keep only the right half — everything left of the cut is dropped.
    track.clips[clipIndex] = right;
    // Close the leading gap: keep the left edge put and pull followers.
    if (m_rippleEnabled) {
        track.clips[clipIndex].timelineStart -= offset;
        applyRippleShift(track, clipIndex, -offset);
    }

    pushProjectEdit(before, tr("Split left"));
    finishEdit(tr("Split left"));
    selectClip(trackIndex, clipIndex);
}

void AppController::splitClipRightAt(int trackIndex, int clipIndex, double seconds)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    const drift::TimeUs atUs = drift::secondsToUs(seconds);
    if (!clip.containsTime(atUs))
        return;

    const drift::TimeUs offset = atUs - clip.timelineStart;
    const drift::Project before = m_project;
    const drift::TimeUs oldDuration = clip.timelineDuration;

    drift::Clip discardedTail;
    if (!drift::splitClipAtOffset(clip, discardedTail, offset))
        return;

    // Keep only the left half — everything right of the cut is dropped.
    applyRippleShift(track, clipIndex, clip.timelineDuration - oldDuration);
    pushProjectEdit(before, tr("Split right"));
    finishEdit(tr("Split right"));
    selectClip(trackIndex, clipIndex);
}

void AppController::trimClipLeft(int trackIndex, int clipIndex, double newStart)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    drift::TimeUs snappedStart = drift::snapTime(m_project, drift::secondsToUs(newStart), m_snapEnabled,
                                                 m_playheadUs, extraSnapTargets());
    // Extending left can create a new overlap; clamp against neighbors when overlap is off.
    if (!m_allowClipOverlap && snappedStart < clip.timelineStart) {
        const QSet<QString> exclude{clip.id};
        snappedStart = drift::clampClipStartAgainstLeftNeighbors(track, exclude, clip.timelineStart,
                                                                 snappedStart);
    }
    const drift::TimeUs delta = snappedStart - clip.timelineStart;
    if (delta == 0)
        return;

    if (isSyntheticTimelineClip(clip.type)) {
        if (delta > 0) {
            if (clip.timelineDuration - delta < drift::kMinClipDurationUs)
                return;
            clip.timelineStart += delta;
            clip.timelineDuration -= delta;
        } else {
            const drift::TimeUs extendBy = -delta;
            if (clip.timelineDuration + extendBy > syntheticClipMaxDurationUs())
                return;
            clip.timelineStart = snappedStart;
            clip.timelineDuration += extendBy;
        }
        // Cue times are relative to the clip's timeline start, so they have to travel with it or
        // every subtitle would slide by the trim amount. Cues pushed outside the clip keep their
        // (possibly negative) offsets so dragging the edge back restores them.
        for (drift::SubtitleCue &cue : clip.subtitleCues) {
            cue.startUs -= delta;
            cue.endUs -= delta;
        }
        syncSyntheticSourceRange(clip);
        syncLinkedPartnersFrom(m_project, clip);
        syncOverlapTransitions(m_project);
        emit tracksChanged();
        return;
    }

    if (delta > 0) {
        if (clip.timelineDuration - delta < drift::kMinClipDurationUs)
            return;
        const drift::TimeUs sourceDelta = trimSourceDelta(clip, delta, false, false);
        if (sourceDelta <= 0)
            return;
        if (clip.reverse) {
            if (clip.srcOut <= clip.srcIn + sourceDelta + drift::kMinClipDurationUs)
                return;
        } else if (clip.srcIn + sourceDelta > clip.srcOut - drift::kMinClipDurationUs) {
            return;
        }

        clip.timelineStart += delta;
        clip.timelineDuration -= delta;
        if (clip.reverse)
            clip.srcOut -= sourceDelta;
        else
            clip.srcIn += sourceDelta;
    } else {
        const drift::TimeUs extendBy = -delta;
        const drift::TimeUs sourceExtend = trimSourceDelta(clip, extendBy, true, clip.reverse);
        if (clip.reverse) {
            const drift::TimeUs maxSource = sourceDurationForClip(clip);
            if (clip.srcOut + sourceExtend > maxSource)
                return;
            clip.timelineStart = snappedStart;
            clip.srcOut += sourceExtend;
            clip.timelineDuration += extendBy;
        } else {
            if (sourceExtend > clip.srcIn)
                return;

            clip.timelineStart = snappedStart;
            clip.srcIn -= sourceExtend;
            clip.timelineDuration += extendBy;
        }
    }

    clip.syncDurationFromSpeedCurve();
    syncLinkedPartnersFrom(m_project, clip);
    syncOverlapTransitions(m_project);
    emit tracksChanged();
}

void AppController::trimClipRight(int trackIndex, int clipIndex, double newEnd)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    drift::TimeUs snappedEnd = drift::snapTime(m_project, drift::secondsToUs(newEnd), m_snapEnabled,
                                               m_playheadUs, extraSnapTargets());
    if (!m_allowClipOverlap && snappedEnd > clip.timelineEnd()) {
        const QSet<QString> exclude{clip.id};
        snappedEnd = drift::clampClipEndNoOverlap(track, exclude, clip.timelineEnd(), snappedEnd);
    }
    drift::TimeUs newDuration = snappedEnd - clip.timelineStart;

    const bool syntheticVisual = isSyntheticTimelineClip(clip.type);
    const drift::TimeUs maxSource = sourceDurationForClip(clip);
    const drift::TimeUs maxSourceSpan =
        clip.reverse ? clip.srcOut : (maxSource > clip.srcIn ? maxSource - clip.srcIn : 0);
    const drift::TimeUs mediaMaxDuration =
        clip.effectiveSpeed() > 0.0
            ? static_cast<drift::TimeUs>(llround(static_cast<double>(maxSourceSpan) / clip.effectiveSpeed()))
            : maxSourceSpan;
    const drift::TimeUs maxDuration =
        syntheticVisual ? drift::secondsToUs(300.0) : mediaMaxDuration;
    newDuration = qBound(drift::kMinClipDurationUs, newDuration, maxDuration);

    clip.timelineDuration = newDuration;
    const drift::TimeUs span =
        clip.hasSpeedCurve() ? trimSourceDelta(clip, newDuration, false, true) : clip.sourceSpanUs();
    const drift::TimeUs maxSrcOut = syntheticVisual ? drift::secondsToUs(300.0) : maxSource;
    if (clip.reverse) {
        clip.srcIn = qMax<drift::TimeUs>(0, clip.srcOut - span);
    } else {
        clip.srcOut = qMin(clip.srcIn + span, maxSrcOut);
    }
    clip.syncDurationFromSpeedCurve();
    syncLinkedPartnersFrom(m_project, clip);
    syncOverlapTransitions(m_project);
    emit tracksChanged();
}

void AppController::setClipTrim(int trackIndex, int clipIndex, double inPoint, double outPoint)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    const drift::TimeUs sourceDuration = sourceDurationForClip(clip);
    const drift::TimeUs clampedIn = qBound<drift::TimeUs>(0, drift::secondsToUs(inPoint),
                                                          sourceDuration - drift::kMinClipDurationUs);
    const drift::TimeUs clampedOut = qBound(clampedIn + drift::kMinClipDurationUs, drift::secondsToUs(outPoint),
                                            sourceDuration);
    const drift::TimeUs newDuration = clampedOut - clampedIn;
    const double speed = clip.effectiveSpeed();

    const drift::Project before = m_project;
    clip.srcIn = clampedIn;
    clip.srcOut = clampedOut;
    clip.timelineDuration = static_cast<drift::TimeUs>(llround(static_cast<double>(newDuration) / speed));
    clip.timelineDuration = qMax(clip.timelineDuration, drift::kMinClipDurationUs);
    // A ramp re-derives the duration from the range that survived the trim.
    clip.syncDurationFromSpeedCurve();
    pushProjectEdit(before, tr("Trim updated"));
    finishEdit(tr("Trim updated"));
}

void AppController::duplicateSelectedClip()
{
    if (m_selectedTrack < 0 || m_selectedClip < 0)
        return;

    drift::Track &track = m_project.tracks()[m_selectedTrack];
    if (m_selectedClip >= track.clips.size())
        return;

    const drift::Project before = m_project;
    const drift::Clip original = track.clips.at(m_selectedClip);
    drift::Clip copy = original;
    copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    copy.timelineStart = drift::resolveClipStart(
        m_project, track, -1, original.timelineEnd(), original.timelineDuration, m_snapEnabled, m_playheadUs);

    track.clips.append(copy);
    pushProjectEdit(before, tr("Clip duplicated"));
    finishEdit(tr("Clip duplicated"));
    selectClip(m_selectedTrack, track.clips.size() - 1);
}

void AppController::alignSelectedClipLeft()
{
    splitSelectedClipLeft();
}

void AppController::alignSelectedClipRight()
{
    splitSelectedClipRight();
}

void AppController::splitSelectedClipLeft()
{
    if (m_selectedTrack < 0 || m_selectedClip < 0)
        return;

    drift::Track &track = m_project.tracks()[m_selectedTrack];
    if (m_selectedClip >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[m_selectedClip];
    if (!clip.containsTime(m_playheadUs) || m_playheadUs == clip.timelineStart)
        return;

    const drift::TimeUs offset = m_playheadUs - clip.timelineStart;
    const drift::Project before = m_project;

    drift::Clip right;
    if (!drift::splitClipAtOffset(clip, right, offset))
        return;

    right.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    // Keep only the right half (discard left) — same as previous "split left" behavior.
    track.clips[m_selectedClip] = right;

    pushProjectEdit(before, tr("Split left"));
    finishEdit(tr("Split left"));
    selectClip(m_selectedTrack, m_selectedClip);
}

void AppController::splitSelectedClipRight()
{
    if (m_selectedTrack < 0 || m_selectedClip < 0)
        return;

    drift::Track &track = m_project.tracks()[m_selectedTrack];
    if (m_selectedClip >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[m_selectedClip];
    if (!clip.containsTime(m_playheadUs) || m_playheadUs == clip.timelineEnd())
        return;

    const drift::TimeUs offset = m_playheadUs - clip.timelineStart;
    const drift::Project before = m_project;

    drift::Clip discardedTail;
    if (!drift::splitClipAtOffset(clip, discardedTail, offset))
        return;

    // Keep only the left half (discard right) — same as previous "split right" behavior.
    pushProjectEdit(before, tr("Split right"));
    finishEdit(tr("Split right"));
    selectClip(m_selectedTrack, m_selectedClip);
}

void AppController::moveClipToTrack(int trackIndex, int clipIndex, int newTrackIndex, double newStart)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    if (newTrackIndex < 0 || newTrackIndex >= m_project.tracks().size())
        return;

    drift::Track &fromTrack = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= fromTrack.clips.size())
        return;

    drift::Track &toTrack = m_project.tracks()[newTrackIndex];
    const drift::Clip clip = fromTrack.clips.at(clipIndex);
    if (!toTrack.allowsClipType(clip.type))
        return;

    const drift::Project before = m_project;
    fromTrack.clips.removeAt(clipIndex);

    // Source-track indices after the hole shift down. Drop the moved slot and
    // remap anything that pointed past it, otherwise finishEdit's normalize
    // would keep the old (track, index) and light up the wrong clip.
    for (int i = m_selection.size() - 1; i >= 0; --i) {
        QPair<int, int> &pair = m_selection[i];
        if (pair.first != trackIndex)
            continue;
        if (pair.second == clipIndex)
            m_selection.removeAt(i);
        else if (pair.second > clipIndex)
            --pair.second;
    }

    drift::Clip moved = clip;
    moved.timelineStart = drift::resolveClipStart(m_project, toTrack, -1, drift::secondsToUs(newStart),
                                                  moved.timelineDuration, m_snapEnabled, m_playheadUs,
                                                  extraSnapTargets());
    toTrack.clips.append(moved);
    const int newClipIndex = toTrack.clips.size() - 1;

    syncLinkedPartnersFrom(m_project, moved);

    // Selection follows the clip to its new track before tracksChanged fires.
    m_selectedTrack = newTrackIndex;
    m_selectedClip = newClipIndex;
    m_selection = selectionWithLinkedPartners(m_project, newTrackIndex, newClipIndex);
    m_selectedTransitionTrack = -1;
    m_selectedTransitionLeftClip = -1;

    pushProjectEdit(before, tr("Clip moved"));
    finishEdit(tr("Clip moved"));
}

void AppController::addTextClip(const QString &text, double atSeconds, const QString &presetId)
{
    const QString trimmed = text.trimmed();
    // Adding with no text is the "drop it in, then type on the preview" path:
    // the clip gets placeholder words and the preview opens an inline editor on
    // it. Passing text keeps the original behaviour.
    const bool placeholder = trimmed.isEmpty();
    const QString content = placeholder ? tr("Your text here") : trimmed;

    const drift::Project before = m_project;
    const int trackIndex = drift::ensureTrackForClipType(m_project, drift::ClipType::Text, true);
    if (trackIndex < 0)
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    const drift::TimeUs startSeconds = atSeconds < 0.0 ? m_playheadUs : drift::secondsToUs(atSeconds);
    const drift::TimeUs start = drift::resolveClipStart(m_project, track, -1, startSeconds,
                                                        drift::kTextClipDurationUs, m_snapEnabled, m_playheadUs);

    drift::Clip clip;
    clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    clip.type = drift::ClipType::Text;
    clip.name = content.left(32);
    clip.textContent = content;
    clip.timelineStart = start;
    clip.timelineDuration = drift::kTextClipDurationUs;
    clip.srcIn = 0;
    clip.srcOut = drift::kTextClipDurationUs;
    if (!presetId.isEmpty()) {
        if (const drift::TextStyle *preset = drift::textStyleForPresetId(presetId)) {
            clip.textStyle = *preset;
            clip.textStyle.packId = presetId;
        }
    }
    applyDefaultVisualLayout(clip, m_project.width(), m_project.height());

    track.clips.append(clip);
    const int newClipIndex = track.clips.size() - 1;
    pushProjectEdit(before, tr("Text clip added"));
    finishEdit(tr("Text clip added"));
    selectClip(trackIndex, newClipIndex);

    if (placeholder) {
        // resolveClipStart pushes the clip past anything already occupying the
        // playhead, so park the playhead on it: the preview can only show (and
        // edit) a clip that spans the current time.
        if (m_playheadUs < start || m_playheadUs >= start + drift::kTextClipDurationUs)
            setPlayheadSeconds(drift::usToSeconds(start));
        emit inlineTextEditRequested(trackIndex, newClipIndex);
    }
}

void AppController::addSubtitleClip(double atSeconds)
{
    const drift::Project before = m_project;
    const int trackIndex =
        drift::ensureTrackForClipType(m_project, drift::ClipType::Subtitle, true);
    if (trackIndex < 0)
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    const drift::TimeUs startSeconds = atSeconds < 0.0 ? m_playheadUs : drift::secondsToUs(atSeconds);
    const drift::TimeUs start = drift::resolveClipStart(m_project, track, -1, startSeconds,
                                                        drift::kSubtitleClipDurationUs, m_snapEnabled,
                                                        m_playheadUs);

    drift::Clip clip;
    clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    clip.type = drift::ClipType::Subtitle;
    clip.name = tr("Subtitles");
    clip.timelineStart = start;
    clip.timelineDuration = drift::kSubtitleClipDurationUs;
    clip.srcIn = 0;
    clip.srcOut = drift::kSubtitleClipDurationUs;
    if (const drift::TextStyle *preset = drift::textStyleForPresetId(QStringLiteral("subtitle")))
        clip.textStyle = *preset;
    applyDefaultVisualLayout(clip, m_project.width(), m_project.height());

    track.clips.append(clip);
    pushProjectEdit(before, tr("Subtitle clip added"));
    finishEdit(tr("Subtitle clip added"));
    selectClip(trackIndex, track.clips.size() - 1);
}

namespace {

drift::TimeUs subtitleClipDurationForCues(const QList<drift::SubtitleCue> &cues)
{
    drift::TimeUs duration = drift::kSubtitleClipDurationUs;
    for (const drift::SubtitleCue &cue : cues)
        duration = qMax(duration, cue.endUs);
    return duration;
}

} // namespace

bool AppController::importSubtitleFile(const QUrl &url, double atSeconds)
{
    const QString path = url.toLocalFile();
    if (path.isEmpty()) {
        setLastMessage(tr("No subtitle file selected"));
        return false;
    }

    QList<drift::SubtitleCue> cues;
    QString error;
    if (!drift::parseSrtFile(path, &cues, &error)) {
        setLastMessage(error.isEmpty() ? tr("Could not read subtitle file") : error, QStringLiteral("error"));
        return false;
    }

    const drift::TimeUs duration = subtitleClipDurationForCues(cues);
    const drift::Project before = m_project;
    const int trackIndex =
        drift::ensureTrackForClipType(m_project, drift::ClipType::Subtitle, true);
    if (trackIndex < 0)
        return false;

    drift::Track &track = m_project.tracks()[trackIndex];
    const drift::TimeUs startSeconds = atSeconds < 0.0 ? m_playheadUs : drift::secondsToUs(atSeconds);
    const drift::TimeUs start =
        drift::resolveClipStart(m_project, track, -1, startSeconds, duration, m_snapEnabled,
                                m_playheadUs);

    drift::Clip clip;
    clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    clip.type = drift::ClipType::Subtitle;
    clip.timelineStart = start;
    clip.timelineDuration = duration;
    clip.srcIn = 0;
    clip.srcOut = duration;
    clip.subtitleCues = cues;
    clip.name = drift::subtitleClipName(cues);
    if (const drift::TextStyle *preset = drift::textStyleForPresetId(QStringLiteral("subtitle")))
        clip.textStyle = *preset;
    applyDefaultVisualLayout(clip, m_project.width(), m_project.height());

    track.clips.append(clip);
    pushProjectEdit(before, tr("Subtitles imported"));
    finishEdit(tr("Subtitles imported"));
    selectClip(trackIndex, track.clips.size() - 1);
    setLastMessage(tr("Imported %1 subtitle(s)").arg(cues.size()));
    return true;
}

bool AppController::importSubtitleFileIntoClip(int trackIndex, int clipIndex, const QUrl &url)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return false;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return false;

    drift::Clip &clip = track.clips[clipIndex];
    if (clip.type != drift::ClipType::Subtitle) {
        setLastMessage(tr("Select a subtitle clip to import into"), QStringLiteral("warning"));
        return false;
    }

    const QString path = url.toLocalFile();
    if (path.isEmpty()) {
        setLastMessage(tr("No subtitle file selected"));
        return false;
    }

    QList<drift::SubtitleCue> cues;
    QString error;
    if (!drift::parseSrtFile(path, &cues, &error)) {
        setLastMessage(error.isEmpty() ? tr("Could not read subtitle file") : error, QStringLiteral("error"));
        return false;
    }

    const drift::TimeUs duration = subtitleClipDurationForCues(cues);
    const drift::Project before = m_project;
    clip.subtitleCues = cues;
    clip.name = drift::subtitleClipName(cues);
    if (duration > clip.timelineDuration) {
        clip.timelineDuration = duration;
        clip.srcOut = duration;
    }
    pushProjectEdit(before, tr("Subtitles imported"));
    finishEdit(tr("Subtitles imported"));
    setLastMessage(tr("Imported %1 subtitle(s)").arg(cues.size()));
    return true;
}

bool AppController::exportSubtitleFile(int trackIndex, int clipIndex, const QUrl &url)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return false;

    const drift::Track &track = m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return false;

    const drift::Clip &clip = track.clips.at(clipIndex);
    if (clip.type != drift::ClipType::Subtitle) {
        setLastMessage(tr("Select a subtitle clip to export"), QStringLiteral("warning"));
        return false;
    }
    if (clip.subtitleCues.isEmpty()) {
        setLastMessage(tr("This subtitle clip has no captions"), QStringLiteral("warning"));
        return false;
    }

    const QString path = url.toLocalFile();
    if (path.isEmpty()) {
        setLastMessage(tr("No save location selected"));
        return false;
    }

    QString error;
    if (!drift::writeSrtFile(path, clip.subtitleCues, &error)) {
        setLastMessage(error.isEmpty() ? tr("Could not write subtitle file") : error, QStringLiteral("error"));
        return false;
    }

    setLastMessage(tr("Subtitles saved"), QStringLiteral("success"));
    return true;
}

void AppController::cancelSubtitleGeneration()
{
    if (m_subtitleGenerating)
        m_subtitleGenCancel.storeRelaxed(1);
}

QVariantList AppController::whisperLanguages()
{
    QVariantList out;
    QVariantMap autoRow;
    autoRow.insert(QStringLiteral("code"), QString());
    autoRow.insert(QStringLiteral("label"), tr("Auto-detect"));
    out.append(autoRow);

    const QVariantList langs = drift::WhisperTranscriber::instance().supportedLanguages();
    for (const QVariant &row : langs)
        out.append(row);
    return out;
}

void AppController::generateSubtitlesForClip(int trackIndex, int clipIndex, const QString &language)
{
    if (m_subtitleGenerating) {
        setLastMessage(tr("Subtitle generation already in progress"), QStringLiteral("warning"));
        return;
    }
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    const drift::Track &track = m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    const drift::Clip clip = track.clips.at(clipIndex);
    if (clip.type != drift::ClipType::Video && clip.type != drift::ClipType::Audio) {
        setLastMessage(tr("Select a video or audio clip to create captions"), QStringLiteral("warning"));
        return;
    }
    if (clip.path.isEmpty() || clip.srcOut <= clip.srcIn) {
        setLastMessage(tr("This clip has no sound"), QStringLiteral("warning"));
        return;
    }

    setPlaying(false);

    m_subtitleGenCancel.storeRelaxed(0);
    m_subtitleGenProgress = 0.0;
    emit subtitleGenProgressChanged();
    m_subtitleGenStatus = tr("Starting…");
    emit subtitleGenStatusChanged();
    m_subtitleGenerating = true;
    emit subtitleGeneratingChanged();
    setLastMessage(tr("Creating captions..."));

    const QString path = clip.path;
    const drift::TimeUs srcIn = clip.srcIn;
    const drift::TimeUs srcOut = clip.srcOut;
    const drift::TimeUs timelineStart = clip.timelineStart;
    const drift::TimeUs timelineDuration = clip.timelineDuration;
    const double speed = clip.effectiveSpeed();
    const bool reverse = clip.reverse;
    const QString languageCode = language.trimmed().toLower();

    (void)QtConcurrent::run([this, path, srcIn, srcOut, timelineStart, timelineDuration, speed,
                             reverse, languageCode]() {
        auto setProgress = [this](double fraction, const QString &status) {
            QMetaObject::invokeMethod(
                this,
                [this, fraction, status]() {
                    m_subtitleGenProgress = fraction;
                    emit subtitleGenProgressChanged();
                    if (!status.isEmpty() && status != m_subtitleGenStatus) {
                        m_subtitleGenStatus = status;
                        emit subtitleGenStatusChanged();
                    }
                },
                Qt::QueuedConnection);
        };

        auto finish = [this, timelineStart, timelineDuration](bool ok, const QString &message,
                                                              const QList<drift::SubtitleCue> &cues) {
            QMetaObject::invokeMethod(
                this,
                [this, ok, message, cues, timelineStart, timelineDuration]() {
                    m_subtitleGenerating = false;
                    emit subtitleGeneratingChanged();
                    m_subtitleGenProgress = ok ? 1.0 : 0.0;
                    emit subtitleGenProgressChanged();
                    m_subtitleGenStatus = ok ? tr("Done") : message;
                    emit subtitleGenStatusChanged();
                    if (!ok || cues.isEmpty()) {
                        setLastMessage(message, QStringLiteral("error"));
                        emit subtitleGenerationFinished(false, message);
                        return;
                    }
                    finalizeGeneratedSubtitles(timelineStart, timelineDuration, cues);
                },
                Qt::QueuedConnection);
        };

        setProgress(0.02, tr("Getting speech recognition ready…"));
        drift::WhisperTranscriber &whisper = drift::WhisperTranscriber::instance();
        if (!whisper.available()) {
            qWarning() << "[subtitles] whisper unavailable:" << whisper.lastError();
            finish(false, whisper.lastError(), {});
            return;
        }

        // Decode the clip's raw source audio over [srcIn, srcOut] at 16 kHz mono.
        setProgress(0.05, tr("Reading audio…"));
        const int rate = 16000;
        const int chunkFrames = 30 * rate;
        std::vector<float> mono;
        drift::TimeUs pos = srcIn;
        const drift::TimeUs spanUs = std::max<drift::TimeUs>(1, srcOut - srcIn);
        while (pos < srcOut) {
            if (m_subtitleGenCancel.loadRelaxed()) {
                finish(false, tr("Subtitle generation cancelled"), {});
                return;
            }
            const drift::TimeUs remainUs = srcOut - pos;
            const int frames =
                qMin<int64_t>(chunkFrames, (remainUs * rate) / drift::kUsPerSecond + 1);
            if (frames <= 0)
                break;
            QVector<float> stereo(static_cast<qsizetype>(frames) * 2);
            const int got =
                ClipReaderPool::instance().readAudioInterleaved(path, pos, frames, rate,
                                                                stereo.data());
            if (got <= 0)
                break;
            const size_t base = mono.size();
            mono.resize(base + got);
            for (int i = 0; i < got; ++i)
                mono[base + i] = 0.5f * (stereo[i * 2] + stereo[i * 2 + 1]);
            pos += static_cast<drift::TimeUs>((static_cast<int64_t>(got) * drift::kUsPerSecond) / rate);
            const double decodeFrac = static_cast<double>(pos - srcIn) / static_cast<double>(spanUs);
            setProgress(0.05 + 0.10 * std::min(1.0, decodeFrac),
                        tr("Reading audio… %1%")
                            .arg(qRound(100.0 * std::min(1.0, decodeFrac))));
        }

        qWarning() << "[subtitles] decoded mono samples:" << mono.size()
                   << "seconds:" << (mono.size() / 16000.0) << "language:"
                   << (languageCode.isEmpty() ? QStringLiteral("auto") : languageCode);
        if (mono.empty()) {
            finish(false, tr("No audio decoded"), {});
            return;
        }

        setProgress(0.15, languageCode.isEmpty()
                              ? tr("Transcribing…")
                              : tr("Transcribing (%1)…").arg(languageCode));

        const drift::WhisperResult res = whisper.transcribe(
            mono,
            [this, setProgress](double fraction, const QString &status) {
                // Map Whisper's 0–1 into the remaining 15%–95% of the overall bar.
                setProgress(0.15 + 0.80 * fraction, status);
                return m_subtitleGenCancel.loadRelaxed() == 0;
            },
            languageCode);

        qWarning() << "[subtitles] transcribe done. ok:" << res.ok << "cancelled:" << res.cancelled
                   << "cues:" << res.cues.size() << "error:" << res.error;

        if (res.cancelled) {
            finish(false, tr("Subtitle generation cancelled"), {});
            return;
        }
        if (!res.ok) {
            finish(false, res.error, {});
            return;
        }

        setProgress(0.96, tr("Building caption track…"));

        // Map source-relative cue times onto clip-relative timeline time (accounts for
        // speed and reverse), clamped to the clip's duration.
        const double spanSec = drift::usToSeconds(srcOut - srcIn);
        QList<drift::SubtitleCue> mapped;
        for (const drift::SubtitleCue &cue : res.cues) {
            const double srcStart = drift::usToSeconds(cue.startUs);
            const double srcEnd = drift::usToSeconds(cue.endUs);
            double tlStart = reverse ? (spanSec - srcEnd) / speed : srcStart / speed;
            double tlEnd = reverse ? (spanSec - srcStart) / speed : srcEnd / speed;
            drift::TimeUs s = qBound<drift::TimeUs>(0, drift::secondsToUs(tlStart), timelineDuration);
            drift::TimeUs e = qBound<drift::TimeUs>(0, drift::secondsToUs(tlEnd), timelineDuration);
            if (e > s) {
                drift::SubtitleCue m;
                m.startUs = s;
                m.endUs = e;
                m.text = cue.text;
                mapped.append(m);
            }
        }
        drift::sortSubtitleCues(mapped);

        qWarning() << "[subtitles] mapped cues:" << mapped.size() << "spanSec:" << spanSec
                   << "timelineDuration us:" << timelineDuration << "speed:" << speed;

        if (mapped.isEmpty()) {
            finish(false, tr("No speech detected"), {});
            return;
        }
        finish(true, tr("Subtitles generated"), mapped);
    });
}

bool AppController::segmentationAvailable()
{
    // Deliberately only checks that the model files exist. This is reached from a QML binding, and
    // loading the sessions here would block the GUI thread for seconds.
    return drift::Sam2Segmenter::modelPresent();
}

QString AppController::segmentationModelVariant()
{
    return drift::Sam2Segmenter::installedVariant();
}

void AppController::cancelSegmentation()
{
    if (m_segmenting)
        m_segmentCancel.storeRelaxed(1);
}

void AppController::beginSegmentationSession(int trackIndex, int clipIndex, double seconds,
                                             bool forTemplate)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    const drift::Track &track = m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;
    if (track.clips.at(clipIndex).type != drift::ClipType::Video) {
        setLastMessage(tr("Select a video clip to cut out"), QStringLiteral("warning"));
        return;
    }

    m_segTrack = trackIndex;
    m_segClip = clipIndex;
    m_segPoints.clear();
    m_segForTemplate = forTemplate;
    m_segSessionActive = true;
    emit segmentSessionChanged();
    setSegmentationFrame(seconds);
}

void AppController::beginSpeedCurveSession(int trackIndex, int clipIndex)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    const drift::Track &track = m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    const drift::Clip &clip = track.clips.at(clipIndex);
    if (clip.type != drift::ClipType::Video && clip.type != drift::ClipType::Audio) {
        setLastMessage(tr("Custom speed works on video and audio clips"), QStringLiteral("warning"));
        return;
    }
    if (clip.path.isEmpty() || clip.srcOut <= clip.srcIn) {
        setLastMessage(tr("This clip has no media to speed up or slow down"), QStringLiteral("warning"));
        return;
    }

    // The preview drives ClipReaderPool from its own threads; leaving timeline playback running
    // would have both walking the same decode workers.
    setPlaying(false);

    m_speedCurveTrack = trackIndex;
    m_speedCurveClipIndex = clipIndex;
    m_speedCurveClip = clip;
    // An existing ramp is what the editor should open on; otherwise start flat at the clip's
    // current constant speed so the graph begins where the clip already plays.
    m_speedCurve = clip.hasSpeedCurve() ? clip.speedCurve : drift::SpeedCurve::flat(clip.effectiveSpeed());
    m_speedCurveClip.speedCurve = m_speedCurve;
    m_speedCurveActive = true;

    m_speedCurvePlayer.setClip(m_speedCurveClip, m_project.sampleRate(), m_project.fps());

    emit speedCurveSessionChanged();
    emit speedCurveChanged();
}

void AppController::endSpeedCurveSession()
{
    if (!m_speedCurveActive)
        return;

    m_speedCurvePlayer.clear();
    m_speedCurveActive = false;
    m_speedCurveTrack = -1;
    m_speedCurveClipIndex = -1;
    m_speedCurveClip = drift::Clip{};
    m_speedCurve.clear();
    emit speedCurveSessionChanged();
    emit speedCurveChanged();
}

QVariantList AppController::speedCurvePoints() const
{
    QVariantList out;
    for (const drift::SpeedPoint &point : m_speedCurve.points()) {
        out.append(QVariantMap{
            {QStringLiteral("pos"), point.pos},
            {QStringLiteral("speed"), point.speed},
            {QStringLiteral("inDx"), point.inDx},
            {QStringLiteral("inDy"), point.inDy},
            {QStringLiteral("outDx"), point.outDx},
            {QStringLiteral("outDy"), point.outDy},
            {QStringLiteral("corner"), point.corner},
        });
    }
    return out;
}

void AppController::setSpeedCurvePoints(const QVariantList &points)
{
    if (!m_speedCurveActive)
        return;

    QList<drift::SpeedPoint> parsed;
    parsed.reserve(points.size());
    for (const QVariant &entry : points) {
        const QVariantMap map = entry.toMap();
        drift::SpeedPoint point;
        point.pos = map.value(QStringLiteral("pos")).toDouble();
        point.speed = map.value(QStringLiteral("speed"), 1.0).toDouble();
        point.inDx = map.value(QStringLiteral("inDx")).toDouble();
        point.inDy = map.value(QStringLiteral("inDy")).toDouble();
        point.outDx = map.value(QStringLiteral("outDx")).toDouble();
        point.outDy = map.value(QStringLiteral("outDy")).toDouble();
        point.corner = map.value(QStringLiteral("corner")).toBool();
        parsed.append(point);
    }

    m_speedCurve.setPoints(parsed);
    m_speedCurveClip.speedCurve = m_speedCurve;
    m_speedCurvePlayer.setSpeedCurve(m_speedCurve);
    emit speedCurveChanged();
}

double AppController::speedCurveSourceStart() const
{
    return drift::usToSeconds(m_speedCurveClip.srcIn);
}

double AppController::speedCurveMediaDuration() const
{
    return drift::usToSeconds(sourceDurationForClip(m_speedCurveClip));
}

double AppController::speedCurveSourceDuration() const
{
    return drift::usToSeconds(m_speedCurveClip.srcOut - m_speedCurveClip.srcIn);
}

double AppController::speedCurveRetimedDuration() const
{
    return drift::usToSeconds(m_speedCurvePlayer.durationUs());
}

double AppController::speedCurvePosition() const
{
    return drift::usToSeconds(m_speedCurvePlayer.positionUs());
}

void AppController::playSpeedCurvePreview()
{
    if (!m_speedCurveActive)
        return;
    setPlaying(false);
    m_speedCurvePlayer.play();
}

void AppController::pauseSpeedCurvePreview()
{
    m_speedCurvePlayer.pause();
}

void AppController::seekSpeedCurvePreview(double seconds)
{
    if (!m_speedCurveActive)
        return;
    m_speedCurvePlayer.seek(drift::secondsToUs(seconds));
}

double AppController::speedCurveSourcePosition() const
{
    const drift::TimeUs span = m_speedCurveClip.srcOut - m_speedCurveClip.srcIn;
    if (span <= 0)
        return 0.0;
    const drift::TimeUs offset =
        m_speedCurve.sourceOffsetForTimelineOffset(m_speedCurvePlayer.positionUs(), span);
    return static_cast<double>(offset) / span;
}

void AppController::seekSpeedCurvePreviewAtSource(double position)
{
    if (!m_speedCurveActive)
        return;
    const drift::TimeUs span = m_speedCurveClip.srcOut - m_speedCurveClip.srcIn;
    if (span <= 0)
        return;
    const drift::TimeUs offset = static_cast<drift::TimeUs>(qBound(0.0, position, 1.0) * span);
    m_speedCurvePlayer.seek(m_speedCurve.timelineOffsetForSourceOffset(offset, span));
}

void AppController::applySpeedCurve()
{
    if (!m_speedCurveActive)
        return;
    if (m_speedCurveTrack < 0 || m_speedCurveTrack >= m_project.tracks().size())
        return;
    const drift::Track &track = m_project.tracks().at(m_speedCurveTrack);
    if (m_speedCurveClipIndex < 0 || m_speedCurveClipIndex >= track.clips.size())
        return;

    m_speedCurvePlayer.pause();

    const drift::Clip source = track.clips.at(m_speedCurveClipIndex);
    // The timeline stays editable while the window is open, so the indices captured at the start
    // of the session can point at a different clip by now.
    if (source.id != m_speedCurveClip.id) {
        setLastMessage(tr("That clip moved — open Custom speed again"), QStringLiteral("warning"));
        return;
    }

    const drift::Project before = m_project;
    drift::Clip retimed = source;
    retimed.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    retimed.speedCurve = m_speedCurve;
    retimed.syncDurationFromSpeedCurve();
    // The copy stands on its own: it carries its own retimed audio rather than staying paired
    // with a companion clip that is still playing at the original rate.
    retimed.linkId.clear();
    retimed.suppressEmbeddedAudio = false;
    retimed.name = (source.name.isEmpty() ? QStringLiteral("Clip") : source.name)
                   + QStringLiteral(" (retimed)");
    remapKeyframesForRetime(retimed, source);

    // The retimed copy replaces the clip it was made from rather than joining it. Left in place the
    // original keeps playing underneath at the original rate: its audio sums into the mix, and it
    // shows through wherever the retimed duration differs. A detached audio companion goes with it
    // for the same reason — the copy carries its own audio.
    QSet<QString> replacedIds{source.id};
    for (const drift::ClipRef &ref : drift::linkedPartners(m_project, source))
        replacedIds.insert(m_project.tracks().at(ref.trackIndex).clips.at(ref.clipIndex).id);

    const int newTrack =
        drift::insertTrackAboveForClipType(m_project, m_speedCurveTrack, source.type);
    m_project.tracks()[newTrack].clips.append(retimed);

    for (drift::Track &t : m_project.tracks()) {
        for (int i = t.clips.size() - 1; i >= 0; --i) {
            if (replacedIds.contains(t.clips.at(i).id))
                t.clips.removeAt(i);
        }
        for (int i = t.transitions.size() - 1; i >= 0; --i) {
            const drift::Transition &transition = t.transitions.at(i);
            if (replacedIds.contains(transition.fromClipId) || replacedIds.contains(transition.toClipId))
                t.transitions.removeAt(i);
        }
    }

    pushProjectEdit(before, tr("Custom speed applied"));
    finishEdit(tr("Custom speed applied"));
    selectClip(newTrack, m_project.tracks().at(newTrack).clips.size() - 1);
    emit speedCurveApplied();
}

void AppController::clearClipSpeedCurve(int trackIndex, int clipIndex)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;
    drift::Clip &clip = track.clips[clipIndex];
    if (!clip.hasSpeedCurve())
        return;

    const drift::Project before = m_project;
    clip.speedCurve.clear();
    // Keep the source range the user framed and let the scalar speed decide how long it takes,
    // rather than syncSrcOutFromSpeed's other direction — the retimed duration it would read
    // from is exactly the thing being discarded.
    clip.timelineDuration = qMax<drift::TimeUs>(
        1, llround(static_cast<double>(clip.srcOut - clip.srcIn) / clip.effectiveSpeed()));
    pushProjectEdit(before, tr("Speed curve removed"));
    finishEdit(tr("Speed curve removed"));
}

void AppController::beginFadeCurveSession(int trackIndex, int clipIndex)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    const drift::Track &track = m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    if (m_fadeCurveActive)
        endFadeCurveSession();

    drift::Clip &clip = m_project.tracks()[trackIndex].clips[clipIndex];
    m_fadeCurveTrack = trackIndex;
    m_fadeCurveClipIndex = clipIndex;
    m_fadeCurveClipId = clip.id;
    m_fadeCurveClipName = clip.name;
    m_fadeCurveBefore = clip.fadeCurve;
    m_fadeShapeBefore = clip.fadeShape;
    m_fadeCurveApplied = false;

    if (clip.fadeCurve == drift::FadeCurve::Custom && !clip.fadeShape.isEmpty())
        m_fadeShape = clip.fadeShape;
    else if (clip.fadeCurve == drift::FadeCurve::Linear)
        m_fadeShape = drift::FadeShape::linearPreset();
    else if (clip.fadeCurve == drift::FadeCurve::EqualPower)
        m_fadeShape = drift::FadeShape::equalPowerPreset();
    else
        m_fadeShape = drift::FadeShape::smoothPreset();

    clip.fadeCurve = drift::FadeCurve::Custom;
    clip.fadeShape = m_fadeShape;
    syncLinkedPartnersFrom(m_project, clip);
    m_fadeCurveActive = true;
    emit fadeCurveSessionChanged();
    emit fadeCurveChanged();
    emitPreviewFrame();
}

void AppController::endFadeCurveSession()
{
    if (!m_fadeCurveActive)
        return;

    if (!m_fadeCurveApplied
        && m_fadeCurveTrack >= 0 && m_fadeCurveTrack < m_project.tracks().size()) {
        drift::Track &track = m_project.tracks()[m_fadeCurveTrack];
        if (m_fadeCurveClipIndex >= 0 && m_fadeCurveClipIndex < track.clips.size()
            && track.clips.at(m_fadeCurveClipIndex).id == m_fadeCurveClipId) {
            drift::Clip &clip = track.clips[m_fadeCurveClipIndex];
            clip.fadeCurve = m_fadeCurveBefore;
            clip.fadeShape = m_fadeShapeBefore;
            syncLinkedPartnersFrom(m_project, clip);
            emitPreviewFrame();
        }
    }

    m_fadeCurveActive = false;
    m_fadeCurveTrack = -1;
    m_fadeCurveClipIndex = -1;
    m_fadeCurveClipId.clear();
    m_fadeCurveClipName.clear();
    m_fadeShape.clear();
    m_fadeShapeBefore.clear();
    m_fadeCurveApplied = false;
    emit fadeCurveSessionChanged();
    emit fadeCurveChanged();
}

QVariantList AppController::fadeCurvePoints() const
{
    QVariantList out;
    for (const QPointF &pt : m_fadeShape.points()) {
        out.append(QVariantMap{
            {QStringLiteral("t"), pt.x()},
            {QStringLiteral("g"), pt.y()},
        });
    }
    return out;
}

void AppController::setFadeCurvePoints(const QVariantList &points)
{
    if (!m_fadeCurveActive)
        return;
    if (m_fadeCurveTrack < 0 || m_fadeCurveTrack >= m_project.tracks().size())
        return;
    drift::Track &track = m_project.tracks()[m_fadeCurveTrack];
    if (m_fadeCurveClipIndex < 0 || m_fadeCurveClipIndex >= track.clips.size())
        return;
    drift::Clip &clip = track.clips[m_fadeCurveClipIndex];
    if (clip.id != m_fadeCurveClipId)
        return;

    QList<QPointF> parsed;
    parsed.reserve(points.size());
    for (const QVariant &entry : points) {
        const QVariantMap map = entry.toMap();
        parsed.append(QPointF(map.value(QStringLiteral("t")).toDouble(),
                              map.value(QStringLiteral("g")).toDouble()));
    }
    m_fadeShape.setPoints(parsed);
    clip.fadeCurve = drift::FadeCurve::Custom;
    clip.fadeShape = m_fadeShape;
    if (clip.animIn.kind == drift::ClipAnimKind::Fade || clip.animIn.curve == drift::FadeCurve::Custom) {
        clip.animIn.curve = drift::FadeCurve::Custom;
        clip.animIn.shape = m_fadeShape;
    }
    if (clip.animOut.kind == drift::ClipAnimKind::Fade || clip.animOut.curve == drift::FadeCurve::Custom) {
        clip.animOut.curve = drift::FadeCurve::Custom;
        clip.animOut.shape = m_fadeShape;
    }
    syncLinkedPartnersFrom(m_project, clip);
    emit fadeCurveChanged();
    emitPreviewFrame();
}

void AppController::resetFadeCurvePreset(const QString &preset)
{
    if (!m_fadeCurveActive)
        return;
    if (preset == QLatin1String("linear"))
        m_fadeShape = drift::FadeShape::linearPreset();
    else if (preset == QLatin1String("equalPower") || preset == QLatin1String("natural"))
        m_fadeShape = drift::FadeShape::equalPowerPreset();
    else
        m_fadeShape = drift::FadeShape::smoothPreset();

    QVariantList points;
    for (const QPointF &pt : m_fadeShape.points()) {
        points.append(QVariantMap{
            {QStringLiteral("t"), pt.x()},
            {QStringLiteral("g"), pt.y()},
        });
    }
    setFadeCurvePoints(points);
}

void AppController::applyFadeCurve()
{
    if (!m_fadeCurveActive)
        return;
    if (m_fadeCurveTrack < 0 || m_fadeCurveTrack >= m_project.tracks().size())
        return;
    drift::Track &track = m_project.tracks()[m_fadeCurveTrack];
    if (m_fadeCurveClipIndex < 0 || m_fadeCurveClipIndex >= track.clips.size())
        return;
    drift::Clip &clip = track.clips[m_fadeCurveClipIndex];
    if (clip.id != m_fadeCurveClipId) {
        setLastMessage(tr("That clip moved — open Custom fade again"), QStringLiteral("warning"));
        return;
    }

    // Rebuild the "before" snapshot: restore prior fade fields on a copy of the current project.
    drift::Project before = m_project;
    if (m_fadeCurveTrack < before.tracks().size()
        && m_fadeCurveClipIndex < before.tracks().at(m_fadeCurveTrack).clips.size()) {
        drift::Clip &beforeClip = before.tracks()[m_fadeCurveTrack].clips[m_fadeCurveClipIndex];
        beforeClip.fadeCurve = m_fadeCurveBefore;
        beforeClip.fadeShape = m_fadeShapeBefore;
        syncLinkedPartnersFrom(before, beforeClip);
    }

    clip.fadeCurve = drift::FadeCurve::Custom;
    clip.fadeShape = m_fadeShape;
    if (clip.animIn.kind == drift::ClipAnimKind::Fade) {
        clip.animIn.curve = drift::FadeCurve::Custom;
        clip.animIn.shape = m_fadeShape;
        clip.animIn.ease = drift::clipAnimCurveToEase(drift::FadeCurve::Custom);
    }
    if (clip.animOut.kind == drift::ClipAnimKind::Fade) {
        clip.animOut.curve = drift::FadeCurve::Custom;
        clip.animOut.shape = m_fadeShape;
        clip.animOut.ease = drift::clipAnimCurveToEase(drift::FadeCurve::Custom);
    }
    // Motion Custom styles also share this curve editor session.
    if (clip.animIn.curve == drift::FadeCurve::Custom)
        clip.animIn.shape = m_fadeShape;
    if (clip.animOut.curve == drift::FadeCurve::Custom)
        clip.animOut.shape = m_fadeShape;
    syncLinkedPartnersFrom(m_project, clip);
    pushProjectEdit(before, tr("Custom fade applied"));
    m_fadeCurveApplied = true;
    finishEdit(tr("Custom fade applied"));
    emit fadeCurveApplied();
    endFadeCurveSession();
}

void AppController::endSegmentationSession()
{
    if (m_segForTemplate)
        m_pendingEffectTemplate.reset();
    m_segSessionActive = false;
    m_segForTemplate = false;
    m_segEncoding = false;
    m_segTrack = -1;
    m_segClip = -1;
    m_segPoints.clear();
    m_segFrame = QImage();
    m_segEmbedding = drift::Sam2Embedding{};
    ++m_segGeneration;
    ++m_segSeedGeneration;
    m_segSeedRunning = false;
    SegmentImageStore::clear();
    ++m_segRevision;
    emit segmentSessionChanged();
}

void AppController::setSegmentationFrame(double seconds)
{
    if (!m_segSessionActive || m_segEncoding)
        return;
    if (m_segTrack < 0 || m_segTrack >= m_project.tracks().size())
        return;
    const drift::Track &track = m_project.tracks().at(m_segTrack);
    if (m_segClip < 0 || m_segClip >= track.clips.size())
        return;

    const drift::Clip clip = track.clips.at(m_segClip);
    m_segSeconds = seconds;

    const drift::TimeUs timelineUs =
        qBound(clip.timelineStart, drift::secondsToUs(seconds),
               clip.timelineStart + clip.timelineDuration - 1);
    const drift::TimeUs sourceUs = clip.timelineToSourceUs(timelineUs);
    const QString path = clip.path;
    const int canvasW = m_project.width();
    const int canvasH = m_project.height();

    m_segEncoding = true;
    m_segPoints.clear();
    const int generation = ++m_segGeneration;
    emit segmentSessionChanged();

    // The encoder is the expensive half (seconds per frame on a CPU provider), so it runs off the
    // GUI thread. Decodes after this are milliseconds and stay inline.
    (void)QtConcurrent::run([this, path, sourceUs, canvasW, canvasH, generation]() {
        const QImage frame =
            ClipReaderPool::instance().readVideoFrame(path, sourceUs, canvasW, canvasH);
        drift::Sam2Embedding embedding;
        if (!frame.isNull())
            embedding = drift::Sam2Segmenter::instance().encode(frame);

        QMetaObject::invokeMethod(
            this,
            [this, frame, embedding, generation]() {
                // Dropped when the window closed, reopened, or the user scrubbed again while
                // this encode was running — otherwise a stale frame would land on a live session.
                if (generation != m_segGeneration)
                    return;
                m_segEncoding = false;
                if (!m_segSessionActive)
                    return;
                m_segFrame = frame;
                m_segEmbedding = embedding;
                SegmentImageStore::setFrame(frame);
                SegmentImageStore::setMask(QImage());
                ++m_segRevision;
                if (frame.isNull() || !embedding.valid)
                    setLastMessage(drift::Sam2Segmenter::instance().lastError(), QStringLiteral("error"));
                emit segmentSessionChanged();
            },
            Qt::QueuedConnection);
    });
}

void AppController::addSegmentationPoint(double x, double y, bool include)
{
    if (!m_segSessionActive || m_segEncoding)
        return;
    QVariantMap point;
    point.insert(QStringLiteral("x"), x);
    point.insert(QStringLiteral("y"), y);
    point.insert(QStringLiteral("include"), include);
    m_segPoints.append(point);
    refreshSegmentationPreview();
}

void AppController::removeSegmentationPoint(int index)
{
    if (!m_segSessionActive || index < 0 || index >= m_segPoints.size())
        return;
    m_segPoints.removeAt(index);
    refreshSegmentationPreview();
}

void AppController::clearSegmentationPoints()
{
    if (!m_segSessionActive)
        return;
    m_segPoints.clear();
    refreshSegmentationPreview();
}

void AppController::refreshSegmentationPreview()
{
    if (m_segPoints.isEmpty() || !m_segEmbedding.valid) {
        ++m_segSeedGeneration;
        SegmentImageStore::setMask(QImage());
        ++m_segRevision;
        emit segmentSessionChanged();
        return;
    }

    // Coalesce rapid point edits onto one ONNX seed at a time — sessions are not
    // safe to call concurrently, and only the latest prompt matters.
    ++m_segSeedGeneration;
    if (m_segSeedRunning)
        return;

    m_segSeedRunning = true;
    const int generation = m_segSeedGeneration;
    runSegmentationSeed(generation);
}

void AppController::runSegmentationSeed(int generation)
{
    drift::Sam2Prompt prompt;
    for (const QVariant &entry : std::as_const(m_segPoints)) {
        const QVariantMap map = entry.toMap();
        prompt.points.append(QPointF(map.value(QStringLiteral("x")).toDouble() * m_segFrame.width(),
                                     map.value(QStringLiteral("y")).toDouble() * m_segFrame.height()));
        prompt.labels.append(map.value(QStringLiteral("include")).toBool() ? 1 : 0);
    }

    const drift::Sam2Embedding embedding = m_segEmbedding;
    (void)QtConcurrent::run([this, embedding, prompt, generation]() {
        const drift::Sam2Result result = drift::Sam2Segmenter::instance().segmentSeed(embedding, prompt);
        QMetaObject::invokeMethod(
            this,
            [this, result, generation]() {
                if (!m_segSessionActive) {
                    m_segSeedRunning = false;
                    return;
                }
                if (generation == m_segSeedGeneration) {
                    SegmentImageStore::setMask(result.ok ? result.mask : QImage());
                    if (!result.ok)
                        setLastMessage(result.error, QStringLiteral("error"));
                    ++m_segRevision;
                    emit segmentSessionChanged();
                    m_segSeedRunning = false;
                    return;
                }
                // A newer prompt arrived while we were seeding — run again with the latest.
                runSegmentationSeed(m_segSeedGeneration);
            },
            Qt::QueuedConnection);
    });
}

void AppController::runSegmentationSession(const QString &outputMode)
{
    if (!m_segSessionActive || m_segPoints.isEmpty())
        return;
    QString mode = outputMode;
    if (m_segForTemplate && m_pendingEffectTemplate && m_pendingEffectTemplate->valid())
        mode = QStringLiteral("template");
    segmentClip(m_segTrack, m_segClip, m_segPoints, mode);
}

void AppController::openSegmentationForTemplate(int trackIndex, int clipIndex)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    const drift::Track &track = m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    const drift::Clip &clip = track.clips.at(clipIndex);
    if (clip.type != drift::ClipType::Video)
        return;

    const double startSeconds = drift::usToSeconds(clip.timelineStart);
    const double durationSeconds = drift::usToSeconds(clip.timelineDuration);
    beginSegmentationSession(trackIndex, clipIndex, startSeconds, true);
    emit openSegmentationWindowRequested(trackIndex, clipIndex, startSeconds, durationSeconds);
}

void AppController::segmentClip(int trackIndex, int clipIndex, const QVariantList &points,
                                const QString &outputMode)
{
    if (m_segmenting) {
        setLastMessage(tr("Cutout is already running"), QStringLiteral("warning"));
        return;
    }
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    const drift::Track &track = m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    const drift::Clip clip = track.clips.at(clipIndex);
    if (clip.type != drift::ClipType::Video) {
        setLastMessage(tr("Select a video clip to cut out"), QStringLiteral("warning"));
        return;
    }
    if (clip.path.isEmpty() || clip.srcOut <= clip.srcIn) {
        setLastMessage(tr("This clip has no video to cut out"), QStringLiteral("warning"));
        return;
    }
    if (points.isEmpty()) {
        setLastMessage(tr("Click the subject first"), QStringLiteral("warning"));
        return;
    }

    // Kept normalized: the decoded frame size depends on the project canvas, and the prompt has
    // to be scaled to whatever each frame actually comes back as.
    drift::Sam2Prompt normalized;
    for (const QVariant &entry : points) {
        const QVariantMap map = entry.toMap();
        normalized.points.append(QPointF(map.value(QStringLiteral("x")).toDouble(),
                                         map.value(QStringLiteral("y")).toDouble()));
        normalized.labels.append(map.value(QStringLiteral("include"), true).toBool() ? 1 : 0);
    }

    // Same reason as the export and subtitle jobs: playback would drive the decode pool from a
    // second thread while this job walks it frame by frame.
    setPlaying(false);

    m_segmentCancel.storeRelaxed(0);
    m_segmentProgress = 0.0;
    emit segmentProgressChanged();
    m_segmentStatus = tr("Getting ready…");
    emit segmentStatusChanged();
    m_segmenting = true;
    emit segmentingChanged();
    setLastMessage(tr("Cutting out subject..."));

    const QString path = clip.path;
    const drift::TimeUs srcIn = clip.srcIn;
    const drift::TimeUs srcOut = clip.srcOut;
    const int fps = qMax(1, m_project.fps());
    const int canvasW = m_project.width();
    const int canvasH = m_project.height();
    const QString mode = outputMode.isEmpty() ? QStringLiteral("clips") : outputMode;
    // Resolved by id at the end rather than by index: the timeline can be edited while the job
    // runs, and stale indices would apply the matte to the wrong clip.
    const QString clipId = clip.id;

    (void)QtConcurrent::run([this, path, srcIn, srcOut, fps, canvasW, canvasH, normalized, mode,
                             clipId]() {
        auto setProgress = [this](double fraction, const QString &status) {
            QMetaObject::invokeMethod(
                this,
                [this, fraction, status]() {
                    m_segmentProgress = fraction;
                    emit segmentProgressChanged();
                    if (!status.isEmpty() && status != m_segmentStatus) {
                        m_segmentStatus = status;
                        emit segmentStatusChanged();
                    }
                },
                Qt::QueuedConnection);
        };

        auto finish = [this, clipId, srcIn, mode](bool ok, const QString &message,
                                                  const QString &mattePath) {
            QMetaObject::invokeMethod(
                this,
                [this, ok, message, mattePath, clipId, srcIn, mode]() {
                    m_segmenting = false;
                    emit segmentingChanged();
                    m_segmentProgress = ok ? 1.0 : 0.0;
                    emit segmentProgressChanged();
                    m_segmentStatus = ok ? tr("Done") : message;
                    emit segmentStatusChanged();
                    if (!ok) {
                        setLastMessage(message, QStringLiteral("error"));
                        emit segmentationFinished(false, message);
                        return;
                    }
                    if (mode == QLatin1String("template")) {
                        if (m_pendingEffectTemplate && m_pendingEffectTemplate->valid()) {
                            const EffectTemplateEntry *entry =
                                effectTemplateForId(m_pendingEffectTemplate->templateId);
                            const PendingEffectTemplate pending = *m_pendingEffectTemplate;
                            m_pendingEffectTemplate.reset();
                            if (entry) {
                                applyEffectTemplateInternal(pending.trackIndex, pending.clipIndex,
                                                            *entry, mattePath, srcIn);
                            }
                        }
                        setLastMessage(message);
                        emit segmentationFinished(true, message);
                        return;
                    }
                    finalizeSegmentation(clipId, mattePath, srcIn, mode);
                    setLastMessage(message);
                    emit segmentationFinished(true, message);
                },
                Qt::QueuedConnection);
        };

        drift::Sam2Segmenter &sam = drift::Sam2Segmenter::instance();
        if (!sam.available()) {
            finish(false, sam.lastError(), {});
            return;
        }

        const drift::TimeUs step = drift::kUsPerSecond / fps;
        const int totalFrames = int((srcOut - srcIn + step - 1) / step);
        if (totalFrames <= 0) {
            finish(false, tr("Clip is too short to cut out"), {});
            return;
        }

        const QString mattePath = drift::newMattePath();
        if (mattePath.isEmpty()) {
            finish(false, tr("Could not create a cutout file"), {});
            return;
        }

        drift::MatteWriter writer;
        bool writerOpen = false;
        std::unique_ptr<drift::Sam2Segmenter::Track> track = sam.newTrack();
        if (!track) {
            finish(false, sam.lastError(), {});
            return;
        }
        int occludedFrames = 0;
        QString error;

        for (int i = 0; i < totalFrames; ++i) {
            if (m_segmentCancel.loadRelaxed() != 0) {
                writer.abort();
                finish(false, tr("Cutout cancelled"), {});
                return;
            }

            const drift::TimeUs sourceUs = srcIn + drift::TimeUs(i) * step;
            const QImage frame =
                ClipReaderPool::instance().readVideoFrame(path, sourceUs, canvasW, canvasH);
            if (frame.isNull()) {
                writer.abort();
                finish(false, tr("Could not decode frame %1").arg(i), {});
                return;
            }

            if (!writerOpen) {
                if (!writer.open(mattePath, frame.size(), fps, 1, &error)) {
                    finish(false, error, {});
                    return;
                }
                writerOpen = true;
            }

            const drift::Sam2Embedding embedding = sam.encode(frame);
            if (!embedding.valid) {
                writer.abort();
                finish(false, sam.lastError(), {});
                return;
            }

            // The first frame is prompted; every later frame is propagated purely from the
            // model's memory bank, so no prompt is carried forward by hand.
            drift::Sam2Result result;
            if (i == 0) {
                drift::Sam2Prompt prompt;
                for (int p = 0; p < normalized.points.size(); ++p) {
                    prompt.points.append(QPointF(normalized.points.at(p).x() * frame.width(),
                                                 normalized.points.at(p).y() * frame.height()));
                    prompt.labels.append(normalized.labels.at(p));
                }
                result = track->seed(embedding, prompt);
            } else {
                result = track->step(embedding);
            }

            if (!result.ok) {
                writer.abort();
                finish(false, result.error, {});
                return;
            }
            if (result.occluded)
                ++occludedFrames;

            if (!writer.writeFrame(result.mask, &error)) {
                writer.abort();
                finish(false, error, {});
                return;
            }

            setProgress(double(i + 1) / totalFrames,
                        tr("Processing frame %1 of %2\u2026").arg(i + 1).arg(totalFrames));
        }

        if (!writer.finish(&error)) {
            writer.abort();
            finish(false, error, {});
            return;
        }

        // Occlusion is the model's own call rather than a tracking failure — it recovers by
        // itself — but a clip that is mostly occluded usually means the wrong subject was marked.
        finish(true,
               occludedFrames > 0
                   ? tr("Cutout complete — subject cut out on %1 of %2 frames")
                         .arg(occludedFrames)
                         .arg(totalFrames)
                   : tr("Cutout complete"),
               mattePath);
    });
}

bool AppController::faceDetectionAvailable()
{
    return drift::FaceLandmarker::modelPresent();
}

void AppController::cancelFaceDetection()
{
    if (m_faceDetecting)
        m_faceDetectCancel.storeRelaxed(1);
}

void AppController::clearFaceTrack(int trackIndex, int clipIndex)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    if (clipIndex < 0 || clipIndex >= m_project.tracks().at(trackIndex).clips.size())
        return;
    if (m_project.tracks().at(trackIndex).clips.at(clipIndex).faceTrackPath.isEmpty())
        return;

    // The sidecar itself is left on disk: undo has to be able to bring the track back, and these
    // files are small and live in a cache directory.
    const drift::Project before = m_project;
    m_project.tracks()[trackIndex].clips[clipIndex].faceTrackPath.clear();
    m_project.tracks()[trackIndex].clips[clipIndex].faceTrackSrcOffsetUs = 0;
    pushProjectEdit(before, tr("Clear Face Track"));
    finishEdit(tr("Clear Face Track"));
}

void AppController::detectFacesForClip(int trackIndex, int clipIndex)
{
    if (m_faceDetecting) {
        setLastMessage(tr("Face detection already in progress"), QStringLiteral("warning"));
        return;
    }
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    const drift::Track &track = m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    const drift::Clip clip = track.clips.at(clipIndex);
    if (clip.type != drift::ClipType::Video && clip.type != drift::ClipType::Image) {
        setLastMessage(tr("Select a video clip to detect faces in"), QStringLiteral("warning"));
        return;
    }
    if (clip.path.isEmpty() || clip.srcOut <= clip.srcIn) {
        setLastMessage(tr("Clip has no video to scan"), QStringLiteral("warning"));
        return;
    }

    // Same reason as the export and segmentation jobs: playback would drive the decode pool from a
    // second thread while this job walks it frame by frame.
    setPlaying(false);

    m_faceDetectCancel.storeRelaxed(0);
    m_faceDetectProgress = 0.0;
    emit faceDetectProgressChanged();
    m_faceDetectStatus = tr("Getting ready…");
    emit faceDetectStatusChanged();
    m_faceDetecting = true;
    emit faceDetectingChanged();
    setLastMessage(tr("Detecting faces..."));

    const QString path = clip.path;
    const drift::TimeUs srcIn = clip.srcIn;
    const drift::TimeUs srcOut = clip.srcOut;
    const int fps = qMax(1, m_project.fps());
    const int canvasW = m_project.width();
    const int canvasH = m_project.height();
    // Resolved by id at the end rather than by index: the timeline can be edited while the job
    // runs, and stale indices would attach the track to the wrong clip.
    const QString clipId = clip.id;

    (void)QtConcurrent::run([this, path, srcIn, srcOut, fps, canvasW, canvasH, clipId]() {
        auto setProgress = [this](double fraction, const QString &status) {
            QMetaObject::invokeMethod(
                this,
                [this, fraction, status]() {
                    m_faceDetectProgress = fraction;
                    emit faceDetectProgressChanged();
                    if (!status.isEmpty() && status != m_faceDetectStatus) {
                        m_faceDetectStatus = status;
                        emit faceDetectStatusChanged();
                    }
                },
                Qt::QueuedConnection);
        };

        auto finish = [this, clipId, srcIn](bool ok, const QString &message,
                                            const QString &trackPath) {
            QMetaObject::invokeMethod(
                this,
                [this, ok, message, trackPath, clipId, srcIn]() {
                    m_faceDetecting = false;
                    emit faceDetectingChanged();
                    m_faceDetectProgress = ok ? 1.0 : 0.0;
                    emit faceDetectProgressChanged();
                    m_faceDetectStatus = ok ? tr("Done") : message;
                    emit faceDetectStatusChanged();
                    if (!ok) {
                        setLastMessage(message, QStringLiteral("error"));
                        emit faceDetectionFinished(false, message);
                        return;
                    }
                    finalizeFaceDetection(clipId, trackPath, srcIn);
                    setLastMessage(message);
                    emit faceDetectionFinished(true, message);
                },
                Qt::QueuedConnection);
        };

        drift::FaceLandmarker &landmarker = drift::FaceLandmarker::instance();
        if (!landmarker.available()) {
            finish(false, landmarker.lastError(), {});
            return;
        }

        const drift::TimeUs step = drift::kUsPerSecond / fps;
        const int totalFrames = int((srcOut - srcIn + step - 1) / step);
        if (totalFrames <= 0) {
            finish(false, tr("Clip is too short to scan"), {});
            return;
        }

        drift::FaceTrack faceTrack;
        faceTrack.fps = fps;
        faceTrack.startSrcUs = srcIn;
        faceTrack.frames.reserve(totalFrames);

        QList<drift::FaceAnchors> previous;
        int framesWithFace = 0;

        for (int i = 0; i < totalFrames; ++i) {
            if (m_faceDetectCancel.loadRelaxed() != 0) {
                finish(false, tr("Face detection cancelled"), {});
                return;
            }

            const drift::TimeUs sourceUs = srcIn + drift::TimeUs(i) * step;
            const QImage frame =
                ClipReaderPool::instance().readVideoFrame(path, sourceUs, canvasW, canvasH);
            if (frame.isNull()) {
                finish(false, tr("Could not decode frame %1").arg(i), {});
                return;
            }

            // The previous frame seeds the next one's ROI, which is what keeps a face in the same
            // slot for the whole clip and skips the detector while tracking holds.
            QList<drift::FaceAnchors> faces =
                landmarker.detect(frame, previous.isEmpty() ? nullptr : &previous);
            previous = faces;

            drift::FaceTrackFrame baked;
            baked.faces = faces;
            faceTrack.frames.append(baked);

            for (const drift::FaceAnchors &a : faces) {
                if (a.valid) {
                    ++framesWithFace;
                    break;
                }
            }

            setProgress(double(i + 1) / totalFrames,
                        tr("Scanning frame %1 of %2…").arg(i + 1).arg(totalFrames));
        }

        if (framesWithFace == 0) {
            finish(false, tr("No faces found in this clip"), {});
            return;
        }

        drift::smoothFaceTrack(&faceTrack);

        const QString trackPath = drift::newFaceTrackPath();
        QString error;
        if (trackPath.isEmpty() || !drift::writeFaceTrack(trackPath, faceTrack, &error)) {
            finish(false, error.isEmpty() ? tr("Could not write the face track") : error,
                   {});
            return;
        }

        finish(true,
               framesWithFace < totalFrames
                   ? tr("Face detection complete — a face was visible in %1 of %2 frames")
                         .arg(framesWithFace)
                         .arg(totalFrames)
                   : tr("Face detection complete"),
               trackPath);
    });
}

void AppController::finalizeFaceDetection(const QString &clipId, const QString &trackPath,
                                          drift::TimeUs srcOffsetUs)
{
    int trackIndex = -1;
    int clipIndex = -1;
    for (int t = 0; t < m_project.tracks().size() && trackIndex < 0; ++t) {
        const drift::Track &track = m_project.tracks().at(t);
        for (int c = 0; c < track.clips.size(); ++c) {
            if (track.clips.at(c).id == clipId) {
                trackIndex = t;
                clipIndex = c;
                break;
            }
        }
    }
    if (trackIndex < 0) {
        // The clip was deleted while the job ran; the track has nothing to attach to.
        QFile::remove(trackPath);
        setLastMessage(tr("Scanned clip no longer exists"), QStringLiteral("warning"));
        return;
    }

    const drift::Project before = m_project;
    m_project.tracks()[trackIndex].clips[clipIndex].faceTrackPath = trackPath;
    m_project.tracks()[trackIndex].clips[clipIndex].faceTrackSrcOffsetUs = srcOffsetUs;
    pushProjectEdit(before, tr("Detect Faces"));
    finishEdit(tr("Detect Faces"));
    selectClip(trackIndex, clipIndex);
}

void AppController::finalizeSegmentation(const QString &clipId, const QString &mattePath,
                                         drift::TimeUs matteSrcOffsetUs, const QString &outputMode)
{
    int trackIndex = -1;
    int clipIndex = -1;
    for (int t = 0; t < m_project.tracks().size() && trackIndex < 0; ++t) {
        const drift::Track &track = m_project.tracks().at(t);
        for (int c = 0; c < track.clips.size(); ++c) {
            if (track.clips.at(c).id == clipId) {
                trackIndex = t;
                clipIndex = c;
                break;
            }
        }
    }
    if (trackIndex < 0) {
        // The clip was deleted while the job ran; the matte has nothing to attach to.
        QFile::remove(mattePath);
        setLastMessage(tr("That clip no longer exists"), QStringLiteral("warning"));
        return;
    }

    const drift::Project before = m_project;
    const drift::Clip source = m_project.tracks().at(trackIndex).clips.at(clipIndex);

    drift::Mask matte;
    matte.shape = drift::MaskShape::Matte;
    matte.mattePath = mattePath;
    matte.matteSrcOffsetUs = matteSrcOffsetUs;

    if (outputMode == QStringLiteral("mask")) {
        m_project.tracks()[trackIndex].clips[clipIndex].mask = matte;
        pushProjectEdit(before, tr("Cut out subject"));
        finishEdit(tr("Cut out subject"));
        selectClip(trackIndex, clipIndex);
        return;
    }

    // Two clips, both referencing the original media: no pixels are re-encoded, and the pair
    // composites back to the original because they differ only by mask inversion. The original
    // clip is deliberately left in place.
    auto derive = [&source, &matte](bool invert, const QString &suffix) {
        drift::Clip clip = source;
        clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        clip.linkId.clear();
        clip.mask = matte;
        clip.mask.invert = invert;
        clip.name = (source.name.isEmpty() ? tr("Clip") : source.name) + suffix;
        return clip;
    };

    // Prepended in reverse so the foreground ends up on top (index 0 is the topmost track).
    const int bgTrack = drift::insertTrackAtTopForClipType(m_project, drift::ClipType::Video);
    m_project.tracks()[bgTrack].clips.append(derive(true, QStringLiteral(" (background)")));

    const int fgTrack = drift::insertTrackAtTopForClipType(m_project, drift::ClipType::Video);
    m_project.tracks()[fgTrack].clips.append(derive(false, QStringLiteral(" (foreground)")));

    pushProjectEdit(before, tr("Cut out subject"));
    finishEdit(tr("Cut out subject"));
    selectClip(fgTrack, m_project.tracks().at(fgTrack).clips.size() - 1);
}

// ---- Noise removal ----------------------------------------------------------------------

bool AppController::denoiseAvailable()
{
    // Deliberately only checks that the model files exist. This is reached from a QML binding, and
    // loading the session here would block the GUI thread.
    return drift::DeepFilterDenoiser::modelPresent();
}

void AppController::cancelDenoise()
{
    if (m_denoising)
        m_denoiseCancel.storeRelaxed(1);
}

bool AppController::renderDenoisedAudio(const QString &path, drift::TimeUs srcIn,
                                        drift::TimeUs span, const QString &outPath,
                                        const QString &originalPath, double progressFrom,
                                        double progressTo, QString *errorOut)
{
    const auto fail = [&](const QString &message) {
        if (errorOut)
            *errorOut = message;
        return false;
    };
    const auto setProgress = [this](double fraction, const QString &status) {
        QMetaObject::invokeMethod(
            this,
            [this, fraction, status]() {
                m_denoiseProgress = fraction;
                emit denoiseProgressChanged();
                if (!status.isEmpty() && status != m_denoiseStatus) {
                    m_denoiseStatus = status;
                    emit denoiseStatusChanged();
                }
            },
            Qt::QueuedConnection);
    };
    const auto span01 = [progressFrom, progressTo](double f) {
        return progressFrom + (progressTo - progressFrom) * std::clamp(f, 0.0, 1.0);
    };

    drift::DeepFilterDenoiser &dn = drift::DeepFilterDenoiser::instance();
    setProgress(span01(0.0), tr("Getting noise removal ready…"));
    if (!dn.available())
        return fail(dn.lastError());

    const int rate = drift::DeepFilterDenoiser::sampleRate();
    const int64_t totalFrames = (span * rate) / drift::kUsPerSecond;
    if (totalFrames <= 0)
        return fail(tr("Clip is too short to process"));

    // Decode the raw source window. Speed and reverse are deliberately not applied: the derived
    // clip inherits them, so the render has to stay in the source's own time base.
    setProgress(span01(0.05), tr("Reading audio…"));
    std::vector<float> interleaved(size_t(totalFrames) * 2, 0.0f);
    int64_t have = 0;
    const int chunkFrames = rate * 10;
    while (have < totalFrames) {
        if (m_denoiseCancel.loadRelaxed())
            return fail(tr("Noise removal cancelled"));
        const drift::TimeUs at = srcIn + drift::TimeUs((have * drift::kUsPerSecond) / rate);
        const int want = int(std::min<int64_t>(chunkFrames, totalFrames - have));
        const int got = ClipReaderPool::instance().readAudioInterleaved(
            path, at, want, rate, interleaved.data() + size_t(have) * 2);
        if (got <= 0)
            break;
        have += got;
        setProgress(span01(0.05 + 0.20 * double(have) / double(totalFrames)),
                    tr("Reading audio…"));
    }
    if (have <= 0)
        return fail(tr("No audio decoded"));
    interleaved.resize(size_t(have) * 2);

    // The A/B source for the preview window, written before the model runs so a cancel still
    // leaves nothing half-made.
    if (!originalPath.isEmpty()) {
        drift::AudioFileWriter orig;
        QString error;
        if (!orig.open(originalPath, rate, 2, &error))
            return fail(error);
        if (!orig.writeFrames(interleaved.data(), int(have), &error) || !orig.finish(&error)) {
            orig.abort();
            return fail(error);
        }
    }

    // The model is mono, so each channel goes through separately and the stereo image is kept.
    std::vector<float> mono(size_t(have), 0.0f);
    for (int ch = 0; ch < 2; ++ch) {
        for (int64_t i = 0; i < have; ++i)
            mono[size_t(i)] = interleaved[size_t(i) * 2 + ch];

        const double base = 0.25 + 0.35 * ch;
        const std::vector<float> clean = dn.denoise(mono, [&](double f) {
            setProgress(span01(base + 0.35 * f),
                        ch == 0 ? tr("Removing noise (left)…")
                                : tr("Removing noise (right)…"));
            return m_denoiseCancel.loadRelaxed() == 0;
        });
        if (clean.empty()) {
            return fail(m_denoiseCancel.loadRelaxed() ? tr("Noise removal cancelled")
                                                      : dn.lastError());
        }
        for (int64_t i = 0; i < have; ++i)
            interleaved[size_t(i) * 2 + ch] = clean[size_t(i)];
    }

    setProgress(span01(0.95), tr("Writing audio…"));
    drift::AudioFileWriter writer;
    QString error;
    if (!writer.open(outPath, rate, 2, &error))
        return fail(error);
    if (!writer.writeFrames(interleaved.data(), int(have), &error) || !writer.finish(&error)) {
        writer.abort();
        return fail(error);
    }

    setProgress(span01(1.0), QString());
    return true;
}

// Both entry points share this: validate the clip, latch the busy state, and hand the work to a
// pool thread. `preview` bounds the render to a short window and reports the pair of files rather
// than touching the project.
void AppController::previewDenoise(int trackIndex, int clipIndex, double atSeconds)
{
    if (m_denoising) {
        setLastMessage(tr("Noise removal already in progress"), QStringLiteral("warning"));
        return;
    }
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    const drift::Track &track = m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    const drift::Clip clip = track.clips.at(clipIndex);
    if (clip.type != drift::ClipType::Audio && clip.type != drift::ClipType::Video) {
        setLastMessage(tr("Select a video or audio clip"), QStringLiteral("warning"));
        return;
    }
    if (clip.path.isEmpty() || clip.srcOut <= clip.srcIn) {
        setLastMessage(tr("Clip has no audio"), QStringLiteral("warning"));
        return;
    }

    // Same reason as the export, subtitle and segmentation jobs: playback would drive the decode
    // pool from a second thread while this walks it.
    setPlaying(false);

    // Eight seconds is enough to judge the result and short enough that dragging the window along
    // the clip stays responsive.
    constexpr drift::TimeUs kPreviewSpan = 8 * drift::kUsPerSecond;
    const drift::TimeUs clipSpan = clip.srcOut - clip.srcIn;
    const drift::TimeUs offset =
        qBound<drift::TimeUs>(0, drift::secondsToUs(atSeconds) * clip.effectiveSpeed(),
                              std::max<drift::TimeUs>(0, clipSpan - 1));
    const drift::TimeUs from = clip.srcIn + offset;
    const drift::TimeUs span = std::min(kPreviewSpan, clip.srcOut - from);

    m_denoiseCancel.storeRelaxed(0);
    m_denoiseProgress = 0.0;
    emit denoiseProgressChanged();
    m_denoiseStatus = tr("Starting…");
    emit denoiseStatusChanged();
    m_denoising = true;
    emit denoisingChanged();

    const QString path = clip.path;
    const QString cleanPath = drift::newDenoisePath(QStringLiteral("-preview"));
    const QString origPath = drift::newDenoisePath(QStringLiteral("-original"));
    if (cleanPath.isEmpty() || origPath.isEmpty()) {
        m_denoising = false;
        emit denoisingChanged();
        setLastMessage(tr("Could not create a preview file"), QStringLiteral("error"));
        return;
    }

    (void)QtConcurrent::run([this, path, from, span, cleanPath, origPath]() {
        QString error;
        const bool ok = renderDenoisedAudio(path, from, span, cleanPath, origPath, 0.0, 1.0, &error);
        QMetaObject::invokeMethod(
            this,
            [this, ok, error, cleanPath, origPath]() {
                m_denoising = false;
                emit denoisingChanged();
                m_denoiseProgress = ok ? 1.0 : 0.0;
                emit denoiseProgressChanged();
                m_denoiseStatus = ok ? tr("Ready") : error;
                emit denoiseStatusChanged();
                if (!ok) {
                    QFile::remove(cleanPath);
                    QFile::remove(origPath);
                    setLastMessage(error, QStringLiteral("error"));
                    emit denoiseFinished(false, error);
                    return;
                }
                // Retire the pair this one replaces, only once the new pair is safely on disk.
                if (!m_denoisePreviewClean.isEmpty())
                    QFile::remove(m_denoisePreviewClean);
                if (!m_denoisePreviewOriginal.isEmpty())
                    QFile::remove(m_denoisePreviewOriginal);
                m_denoisePreviewClean = cleanPath;
                m_denoisePreviewOriginal = origPath;
                emit denoisePreviewReady(origPath, cleanPath);
            },
            Qt::QueuedConnection);
    });
}

void AppController::applyDenoise(int trackIndex, int clipIndex)
{
    if (m_denoising) {
        setLastMessage(tr("Noise removal already in progress"), QStringLiteral("warning"));
        return;
    }
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    const drift::Track &track = m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    const drift::Clip clip = track.clips.at(clipIndex);
    if (clip.type != drift::ClipType::Audio && clip.type != drift::ClipType::Video) {
        setLastMessage(tr("Select a video or audio clip"), QStringLiteral("warning"));
        return;
    }
    if (clip.path.isEmpty() || clip.srcOut <= clip.srcIn) {
        setLastMessage(tr("Clip has no audio"), QStringLiteral("warning"));
        return;
    }

    setPlaying(false);

    m_denoiseCancel.storeRelaxed(0);
    m_denoiseProgress = 0.0;
    emit denoiseProgressChanged();
    m_denoiseStatus = tr("Starting…");
    emit denoiseStatusChanged();
    m_denoising = true;
    emit denoisingChanged();
    setLastMessage(tr("Removing noise..."));

    const QString path = clip.path;
    const drift::TimeUs srcIn = clip.srcIn;
    const drift::TimeUs span = clip.srcOut - clip.srcIn;
    // Resolved by id at the end rather than by index: the timeline can be edited while the job
    // runs, and a stale index would attach the audio to the wrong clip.
    const QString clipId = clip.id;
    const QString outPath = drift::newDenoisePath();
    if (outPath.isEmpty()) {
        m_denoising = false;
        emit denoisingChanged();
        setLastMessage(tr("Could not create an output file"), QStringLiteral("error"));
        return;
    }

    (void)QtConcurrent::run([this, path, srcIn, span, outPath, clipId]() {
        QString error;
        const bool ok = renderDenoisedAudio(path, srcIn, span, outPath, QString(), 0.0, 1.0, &error);
        QMetaObject::invokeMethod(
            this,
            [this, ok, error, outPath, clipId]() {
                m_denoising = false;
                emit denoisingChanged();
                m_denoiseProgress = ok ? 1.0 : 0.0;
                emit denoiseProgressChanged();
                m_denoiseStatus = ok ? tr("Done") : error;
                emit denoiseStatusChanged();
                if (!ok) {
                    QFile::remove(outPath);
                    setLastMessage(error, QStringLiteral("error"));
                    emit denoiseFinished(false, error);
                    return;
                }
                finalizeDenoise(clipId, outPath);
            },
            Qt::QueuedConnection);
    });
}

void AppController::finalizeDenoise(const QString &clipId, const QString &audioPath)
{
    int trackIndex = -1;
    int clipIndex = -1;
    for (int t = 0; t < m_project.tracks().size() && trackIndex < 0; ++t) {
        const drift::Track &track = m_project.tracks().at(t);
        for (int c = 0; c < track.clips.size(); ++c) {
            if (track.clips.at(c).id == clipId) {
                trackIndex = t;
                clipIndex = c;
                break;
            }
        }
    }
    if (trackIndex < 0) {
        // The clip was deleted while the job ran; the audio has nothing to attach to.
        QFile::remove(audioPath);
        const QString message = QStringLiteral("The clip no longer exists");
        setLastMessage(message, QStringLiteral("error"));
        emit denoiseFinished(false, message);
        return;
    }

    const drift::Project before = m_project;
    const drift::Clip source = m_project.tracks().at(trackIndex).clips.at(clipIndex);

    // Everything about the clip carries over except the media it points at. srcIn/srcOut rebase
    // because the render already baked in the source window, while speed and reverse stay — the
    // render is in the source's time base, so the new clip lines up with the original.
    drift::Clip clip = source;
    clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    clip.linkId.clear();
    clip.assetId.clear();
    clip.path = audioPath;
    clip.thumbnailPath.clear();
    clip.filmstripPath.clear();
    clip.type = drift::ClipType::Audio;
    clip.mask = drift::Mask{};
    clip.srcIn = 0;
    clip.srcOut = source.srcOut - source.srcIn;
    clip.name = (source.name.isEmpty() ? tr("Clip") : source.name)
                + tr(" (denoised)");

    const int newTrack =
        drift::insertTrackAboveForClipType(m_project, trackIndex, drift::ClipType::Audio);
    m_project.tracks()[newTrack].clips.append(clip);

    pushProjectEdit(before, tr("Remove noise"));
    finishEdit(tr("Noise removed"));
    selectClip(newTrack, m_project.tracks().at(newTrack).clips.size() - 1);
    setLastMessage(tr("Noise removed"), QStringLiteral("success"));
    emit denoiseFinished(true, tr("Noise removed"));
}

void AppController::finalizeGeneratedSubtitles(drift::TimeUs timelineStart,
                                               drift::TimeUs timelineDuration,
                                               const QList<drift::SubtitleCue> &cues)
{
    const drift::Project before = m_project;
    const int trackIndex = drift::ensureTrackForClipType(m_project, drift::ClipType::Subtitle, true);
    qWarning() << "[subtitles] finalize: trackIndex" << trackIndex << "cues" << cues.size()
               << "start" << timelineStart << "dur" << timelineDuration;
    if (trackIndex < 0)
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    drift::Clip clip;
    clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    clip.type = drift::ClipType::Subtitle;
    clip.timelineStart = timelineStart;
    clip.timelineDuration = timelineDuration;
    clip.srcIn = 0;
    clip.srcOut = timelineDuration;
    if (const drift::TextStyle *preset = drift::textStyleForPresetId(QStringLiteral("subtitle")))
        clip.textStyle = *preset;
    applyDefaultVisualLayout(clip, m_project.width(), m_project.height());
    clip.subtitleCues = cues;
    clip.name = drift::subtitleClipName(cues);

    track.clips.append(clip);
    pushProjectEdit(before, tr("Subtitles generated"));
    finishEdit(tr("Subtitles generated"));
    selectClip(trackIndex, track.clips.size() - 1);
    setLastMessage(tr("Subtitles generated"), QStringLiteral("success"));
    emit subtitleGenerationFinished(true, QStringLiteral("Subtitles generated"));
}

void AppController::reportMissingCatalogEntries()
{
    QSet<QString> missingEffects;
    QSet<QString> missingTransitions;

    for (const drift::Track &track : m_project.tracks()) {
        for (const drift::Clip &clip : track.clips) {
            for (const drift::Effect &effect : clip.effects) {
                if (!effect.catalogId.isEmpty() && !effectDefForId(effect.catalogId))
                    missingEffects.insert(effect.catalogId);
            }
        }
        for (const drift::Transition &transition : track.transitions) {
            if (!transition.kindId.isEmpty() && !transitionDefForId(transition.kindId))
                missingTransitions.insert(transition.kindId);
        }
    }

    const int total = missingEffects.size() + missingTransitions.size();
    if (total == 0)
        return;

    QStringList names = QStringList(missingEffects.begin(), missingEffects.end())
                        + QStringList(missingTransitions.begin(), missingTransitions.end());
    names.sort();
    const QString sample = names.mid(0, 3).join(QStringLiteral(", "));

    setLastMessage(total == 1
                       ? tr("This project uses \"%1\", which isn’t installed — it "
                            "won’t show. Open Extras to install it.").arg(sample)
                       : tr("This project uses %1 effects or transitions that aren’t "
                            "installed (%2%3) — they won’t show. Open Extras to install them.")
                             .arg(total)
                             .arg(sample, names.size() > 3 ? tr(", …") : QString()));
}

QVariantList AppController::builtinStickers() const
{
    QVariantList out;
    for (const StickerEntry &entry : ::stickers()) {
        out.append(QVariantMap{
            {QStringLiteral("id"), entry.id},
            {QStringLiteral("label"), entry.label},
            {QStringLiteral("category"), entry.category},
            {QStringLiteral("path"), entry.path},
        });
    }
    return out;
}

QVariantList AppController::builtinStickerCategories() const
{
    QVariantList out;
    for (const StickerCategory &category : ::stickerCategories()) {
        out.append(QVariantMap{
            {QStringLiteral("id"), category.id},
            {QStringLiteral("label"), category.label},
        });
    }
    return out;
}

QVariantList AppController::builtinShapes() const
{
    QVariantList out;
    for (const drift::ShapeCatalogEntry &entry : drift::shapeCatalog()) {
        out.append(QVariantMap{
            {QStringLiteral("id"), entry.id},
            {QStringLiteral("label"), entry.label},
            {QStringLiteral("category"), entry.category},
            // Several ids share a kind, so the inspector matches a clip's stored kind on this.
            {QStringLiteral("kind"), drift::shapeKindToString(entry.style.kind)},
        });
    }
    return out;
}

QVariantList AppController::builtinShapeCategories() const
{
    QVariantList out;
    for (const drift::ShapeCategory &category : drift::shapeCategories()) {
        out.append(QVariantMap{
            {QStringLiteral("id"), category.id},
            {QStringLiteral("label"), category.label},
        });
    }
    return out;
}

QString AppController::shapeSvgPath(const QString &shapeId) const
{
    const drift::ShapeCatalogEntry *entry = drift::shapeCatalogEntry(shapeId);
    drift::ShapeStyle style = entry ? entry->style : shapeStyleForKind(shapeId);
    const double aspect = entry ? entry->aspect : 1.0;

    // Thumbnails are authored on the 0..100 grid ShapePreview.qml scales from, inset so the 2px
    // preview stroke is not clipped by the item edge.
    constexpr double kGrid = 100.0;
    constexpr double kInset = 3.0;
    const double boxW = aspect >= 1.0 ? kGrid : kGrid * aspect;
    const double boxH = aspect >= 1.0 ? kGrid / aspect : kGrid;
    const QRectF bounds =
        QRectF((kGrid - boxW) / 2.0, (kGrid - boxH) / 2.0, boxW, boxH)
            .adjusted(kInset, kInset, -kInset, -kInset);

    // Corner radius is in project pixels; on a 100-unit grid a 32px radius would swallow the shape.
    style.cornerRadius = style.cornerRadius > 0.0 ? 12.0 : 0.0;
    return drift::shapeSvgPath(style, bounds);
}

void AppController::addShapeClip(const QString &shapeKind, double atSeconds)
{
    addShapeClipAt(shapeKind, -1, atSeconds);
}

void AppController::addShapeClipAt(const QString &shapeId, int trackIndex, double atSeconds)
{
    const drift::ShapeCatalogEntry *entry = drift::shapeCatalogEntry(shapeId);
    const drift::ShapeStyle style = entry ? entry->style : shapeStyleForKind(shapeId);
    const drift::Project before = m_project;

    int target = trackIndex;
    if (target < 0 || target >= m_project.tracks().size()
        || !m_project.tracks().at(target).allowsClipType(drift::ClipType::Shape)) {
        target = drift::ensureTrackForClipType(m_project, drift::ClipType::Shape, true);
    }
    if (target < 0)
        return;

    drift::Track &track = m_project.tracks()[target];
    const drift::TimeUs startSeconds = atSeconds < 0.0 ? m_playheadUs : drift::secondsToUs(atSeconds);
    const drift::TimeUs start = drift::resolveClipStart(m_project, track, -1, startSeconds,
                                                        drift::kImageClipDurationUs, m_snapEnabled, m_playheadUs);

    drift::Clip clip;
    clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    clip.type = drift::ClipType::Shape;
    clip.name = entry ? entry->label : drift::shapeKindToString(style.kind);
    clip.shapeStyle = style;
    clip.timelineStart = start;
    clip.timelineDuration = drift::kImageClipDurationUs;
    clip.srcIn = 0;
    clip.srcOut = drift::kImageClipDurationUs;
    applyDefaultVisualLayout(clip, m_project.width(), m_project.height(),
                             entry ? entry->aspect : 1.6);

    track.clips.append(clip);
    pushProjectEdit(before, tr("Shape added"));
    finishEdit(tr("Shape added"));
    selectClip(target, track.clips.size() - 1);
}

void AppController::addStickerClip(const QString &stickerId, double atSeconds)
{
    QString path;
    QString label;
    for (const QVariant &item : builtinStickers()) {
        const QVariantMap sticker = item.toMap();
        if (sticker.value(QStringLiteral("id")).toString() == stickerId) {
            path = sticker.value(QStringLiteral("path")).toString();
            label = sticker.value(QStringLiteral("label")).toString();
            break;
        }
    }
    if (path.isEmpty())
        return;

    addImageOverlayClip(path, label.isEmpty() ? stickerId : label, QString(), atSeconds,
                        QStringLiteral("Sticker added"));
}

QVariantList AppController::emojiCatalog() const
{
    QVariantList out;
    for (const EmojiEntry &entry : ::emojiCatalog()) {
        out.append(QVariantMap{
            {QStringLiteral("emoji"), entry.emoji},
            {QStringLiteral("name"), entry.name},
            {QStringLiteral("group"), entry.group},
            {QStringLiteral("keywords"), entry.keywords},
        });
    }
    return out;
}

QStringList AppController::emojiGroups() const
{
    return ::emojiGroups();
}

QString AppController::emojiFontFamily() const
{
    return ::emojiFontFamily();
}

void AppController::addEmojiClip(const QString &emoji, const QString &name, double atSeconds)
{
    const QString path = emojiImagePath(emoji);
    if (path.isEmpty()) {
        setLastMessage(tr("Install the emoji sticker pack to add emoji"), QStringLiteral("warning"));
        return;
    }
    addImageOverlayClip(path, name.isEmpty() ? emoji : name, emoji, atSeconds,
                        QStringLiteral("Emoji added"));
}

void AppController::addImageOverlayClip(const QString &path, const QString &name,
                                        const QString &emoji, double atSeconds,
                                        const QString &undoText)
{
    const drift::Project before = m_project;
    const int trackIndex = drift::ensureTrackForClipType(m_project, drift::ClipType::Image, true);
    if (trackIndex < 0)
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    const drift::TimeUs startSeconds = atSeconds < 0.0 ? m_playheadUs : drift::secondsToUs(atSeconds);
    const drift::TimeUs start = drift::resolveClipStart(m_project, track, -1, startSeconds,
                                                        drift::kImageClipDurationUs, m_snapEnabled, m_playheadUs);

    drift::Clip clip;
    clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    clip.type = drift::ClipType::Image;
    clip.name = name;
    clip.path = path;
    clip.thumbnailPath = path;
    clip.filmstripPath = path;
    clip.emoji = emoji;
    clip.timelineStart = start;
    clip.timelineDuration = drift::kImageClipDurationUs;
    clip.srcIn = 0;
    clip.srcOut = drift::kImageClipDurationUs;
    applyDefaultVisualLayout(clip, m_project.width(), m_project.height());

    track.clips.append(clip);
    pushProjectEdit(before, undoText);
    finishEdit(undoText);
    selectClip(trackIndex, track.clips.size() - 1);
}

QVariantList AppController::previewClipsAtPlayhead() const
{
    QVariantList out;
    const int canvasWidth = m_project.width();
    const int canvasHeight = m_project.height();
    if (canvasWidth <= 0 || canvasHeight <= 0)
        return out;

    const QList<drift::Track> &tracks = m_project.tracks();
    for (int trackIndex = 0; trackIndex < tracks.size(); ++trackIndex) {
        const drift::Track &track = tracks.at(trackIndex);
        if (track.hidden)
            continue;
        if (track.type != drift::TrackType::Video && track.type != drift::TrackType::Shape
            && track.type != drift::TrackType::Text && track.type != drift::TrackType::Subtitle)
            continue;

        for (int clipIndex = 0; clipIndex < track.clips.size(); ++clipIndex) {
            const drift::Clip &clip = track.clips.at(clipIndex);
            if (!clip.containsTime(m_playheadUs) || !clipAcceptsPreviewTransform(clip))
                continue;

            const drift::TimeUs relative = m_playheadUs - clip.timelineStart;
            const double x = clipTransformValue(clip.transformX, relative, 0.0);
            const double y = clipTransformValue(clip.transformY, relative, 0.0);
            const double w = clipTransformValue(clip.transformW, relative, static_cast<double>(canvasWidth));
            const double h = clipTransformValue(clip.transformH, relative, static_cast<double>(canvasHeight));
            const double rotation = clipTransformValue(clip.rotation, relative, 0.0);

            out.append(QVariantMap{
                {QStringLiteral("track"), trackIndex},
                {QStringLiteral("clip"), clipIndex},
                {QStringLiteral("kind"), drift::clipTypeToString(clip.type)},
                {QStringLiteral("name"), clip.name},
                {QStringLiteral("pixelSize"), clip.textStyle.pixelSize},
                {QStringLiteral("x"), x},
                {QStringLiteral("y"), y},
                {QStringLiteral("width"), w},
                {QStringLiteral("height"), h},
                {QStringLiteral("rotation"), rotation},
                {QStringLiteral("canvasWidth"), canvasWidth},
                {QStringLiteral("canvasHeight"), canvasHeight},
            });
        }
    }
    return out;
}

int AppController::projectWidth() const
{
    return m_project.width();
}

int AppController::projectHeight() const
{
    return m_project.height();
}

int AppController::projectFps() const
{
    return m_project.fps();
}

void AppController::setProjectResolution(int width, int height)
{
    setProjectSetup(width, height, m_project.fps());
}

void AppController::setProjectSetup(int width, int height, int fps)
{
    width = qBound(16, width, 7680);
    height = qBound(16, height, 4320);
    fps = qBound(1, fps, 240);
    if (m_project.width() == width && m_project.height() == height && m_project.fps() == fps)
        return;

    // Answering the first-run layout chooser is not an edit — it is the project taking its initial
    // shape. Pushing an undo command here is what used to leave a brand-new project dirty with one
    // entry on a freshly cleared stack. ProjectSetupDialog runs after the first clip was added, so
    // the stack is non-empty by then and it still gets its undo step.
    const bool pristine =
        m_undoStack.count() == 0 && !m_dirty && m_currentProjectPath.isEmpty();

    const drift::Project before = m_project;
    if (m_project.width() != width || m_project.height() != height)
        drift::rebaseClipLayout(m_project, m_project.width(), m_project.height(), 0.0, 0.0);
    m_project.setResolution(width, height);
    m_project.setFps(fps);
    if (!pristine)
        pushProjectEdit(before, tr("Project setup"));
    finishEdit(tr("Project setup updated"));
}

// Crop rect is given in current-canvas pixels; it may extend outside the canvas
// (negative origin / oversized extent) to grow the frame on that side.
void AppController::applyCanvasCrop(double x, double y, double width, double height)
{
    const int newWidth = qBound(16, qRound(width), 7680);
    const int newHeight = qBound(16, qRound(height), 4320);
    if (newWidth == m_project.width() && newHeight == m_project.height()
        && qFuzzyIsNull(x) && qFuzzyIsNull(y))
        return;

    const drift::Project before = m_project;
    drift::rebaseClipLayout(m_project, m_project.width(), m_project.height(), x, y);
    m_project.setResolution(newWidth, newHeight);
    pushProjectEdit(before, tr("Crop canvas"));
    finishEdit(tr("Video size cropped to %1×%2").arg(newWidth).arg(newHeight));
}

void AppController::setCanvasCropMode(bool active)
{
    if (m_canvasCropMode == active)
        return;
    m_canvasCropMode = active;
    emit canvasCropModeChanged();
}

QVariantMap AppController::background() const
{
    const drift::Background &bg = m_project.background();
    QVariantMap map;
    map.insert(QStringLiteral("kind"),
               bg.kind == drift::BackgroundKind::Blur ? QStringLiteral("blur") : QStringLiteral("color"));
    map.insert(QStringLiteral("color"), bg.color.name(QColor::HexArgb));
    map.insert(QStringLiteral("blurStrength"), bg.blurStrength);
    return map;
}

void AppController::setBackground(const QVariantMap &background)
{
    drift::Background bg = m_project.background();
    if (background.contains(QStringLiteral("kind"))) {
        bg.kind = background.value(QStringLiteral("kind")).toString() == QStringLiteral("blur")
                      ? drift::BackgroundKind::Blur
                      : drift::BackgroundKind::Color;
    }
    if (background.contains(QStringLiteral("color"))) {
        const QColor color(background.value(QStringLiteral("color")).toString());
        if (color.isValid())
            bg.color = color;
    }
    if (background.contains(QStringLiteral("blurStrength")))
        bg.blurStrength = qBound(0.0, background.value(QStringLiteral("blurStrength")).toDouble(), 200.0);

    const drift::Background &current = m_project.background();
    if (current.kind == bg.kind && current.color == bg.color
        && qFuzzyCompare(current.blurStrength + 1.0, bg.blurStrength + 1.0))
        return;

    const drift::Project before = m_project;
    m_project.setBackground(bg);
    pushProjectEdit(before, tr("Change background"));
    finishEdit(tr("Background updated"));
    emit backgroundChanged();
    emitPreviewFrame();
}

bool AppController::timelineHasVisualClips() const
{
    for (const drift::Track &track : m_project.tracks()) {
        for (const drift::Clip &clip : track.clips) {
            if (clip.type == drift::ClipType::Video || clip.type == drift::ClipType::Image
                || clip.type == drift::ClipType::Text || clip.type == drift::ClipType::Subtitle
                || clip.type == drift::ClipType::Shape) {
                return true;
            }
        }
    }
    return false;
}

bool AppController::shouldConfigureProjectForAsset(int assetIndex) const
{
    if (m_projectLayoutChosen)
        return false;
    if (!m_assetLibrary)
        return false;
    const QVariantMap asset = m_assetLibrary->assetAt(assetIndex);
    if (asset.isEmpty())
        return false;
    const QString kind = asset.value(QStringLiteral("kind")).toString();
    if (kind != QStringLiteral("video") && kind != QStringLiteral("image"))
        return false;

    // Offer setup only for the first video/image clip (text/shapes alone don't count).
    for (const drift::Track &track : m_project.tracks()) {
        for (const drift::Clip &clip : track.clips) {
            if (clip.type == drift::ClipType::Video || clip.type == drift::ClipType::Image)
                return false;
        }
    }
    return true;
}

void AppController::markProjectLayoutChosen()
{
    setProjectLayoutChosen(true);
}

void AppController::setProjectLayoutChosen(bool chosen)
{
    if (m_projectLayoutChosen == chosen)
        return;
    m_projectLayoutChosen = chosen;
    emit projectLayoutChosenChanged();
}

QVariantMap AppController::suggestedProjectSetupForAsset(int assetIndex) const
{
    QVariantMap out{
        {QStringLiteral("width"), m_project.width()},
        {QStringLiteral("height"), m_project.height()},
        {QStringLiteral("fps"), m_project.fps()},
        {QStringLiteral("aspect"), QStringLiteral("custom")},
    };
    if (!m_assetLibrary)
        return out;

    const QVariantMap asset = m_assetLibrary->assetAt(assetIndex);
    if (asset.isEmpty())
        return out;

    int w = asset.value(QStringLiteral("width")).toInt();
    int h = asset.value(QStringLiteral("height")).toInt();
    const int rotation = asset.value(QStringLiteral("rotationDegrees")).toInt();
    if (rotation == 90 || rotation == 270)
        std::swap(w, h);
    if (w > 0 && h > 0) {
        out.insert(QStringLiteral("width"), w);
        out.insert(QStringLiteral("height"), h);
        const double ratio = static_cast<double>(w) / static_cast<double>(h);
        if (qAbs(ratio - 16.0 / 9.0) < 0.02)
            out.insert(QStringLiteral("aspect"), QStringLiteral("16:9"));
        else if (qAbs(ratio - 9.0 / 16.0) < 0.02)
            out.insert(QStringLiteral("aspect"), QStringLiteral("9:16"));
        else if (qAbs(ratio - 4.0 / 3.0) < 0.02)
            out.insert(QStringLiteral("aspect"), QStringLiteral("4:3"));
        else if (qAbs(ratio - 1.0) < 0.02)
            out.insert(QStringLiteral("aspect"), QStringLiteral("1:1"));
        else
            out.insert(QStringLiteral("aspect"), QStringLiteral("source"));
    }
    const double fps = asset.value(QStringLiteral("fps")).toDouble();
    if (fps >= 1.0)
        out.insert(QStringLiteral("fps"), qRound(fps));
    out.insert(QStringLiteral("name"), asset.value(QStringLiteral("name")).toString());
    out.insert(QStringLiteral("kind"), asset.value(QStringLiteral("kind")).toString());
    return out;
}

void AppController::beginPreviewDrag(const QString &undoText)
{
    m_previewDragBefore = m_project;
    m_previewDragActive = true;
    m_previewDragText = undoText.isEmpty() ? tr("Edit clip") : undoText;
}

void AppController::emitPreviewFrame()
{
    // Same rule as finishEdit: never seek the live clock for a preview refresh.
    if (!m_playback.isPlaying())
        m_playback.setPlayheadUs(m_playheadUs);
    emit tracksChanged(); // also notifies selectedClipDataChanged via connection
    m_playback.refreshFrame();
}

void AppController::previewSetClipPosition(int trackIndex, int clipIndex, double xPixels, double yPixels)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    const drift::TimeUs relative = qMax<drift::TimeUs>(0, m_playheadUs - clip.timelineStart);
    const bool wroteX = writeKeyframeValue(clip.transformX, relative, xPixels, m_autoKeyEnabled, false);
    const bool wroteY = writeKeyframeValue(clip.transformY, relative, yPixels, m_autoKeyEnabled, false);
    if (!wroteX && !wroteY) {
        emit transformBlocked(tr("Turn on Auto keyframes to move this"));
        return;
    }

    if (!m_previewDragActive)
        beginPreviewDrag(tr("Move clip"));

    emitPreviewFrame();
}

void AppController::previewSetClipSize(int trackIndex, int clipIndex, double widthPixels, double heightPixels)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    const drift::TimeUs relative = qMax<drift::TimeUs>(0, m_playheadUs - clip.timelineStart);
    const bool wroteW =
        writeKeyframeValue(clip.transformW, relative, qMax(1.0, widthPixels), m_autoKeyEnabled, false);
    const bool wroteH =
        writeKeyframeValue(clip.transformH, relative, qMax(1.0, heightPixels), m_autoKeyEnabled, false);
    if (!wroteW && !wroteH) {
        emit transformBlocked(tr("Turn on Auto keyframes to resize this"));
        return;
    }

    if (!m_previewDragActive)
        beginPreviewDrag(tr("Resize clip"));

    emitPreviewFrame();
}

void AppController::previewSetClipRect(int trackIndex, int clipIndex, double xPixels, double yPixels,
                                       double widthPixels, double heightPixels)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    const drift::TimeUs relative = qMax<drift::TimeUs>(0, m_playheadUs - clip.timelineStart);
    bool wrote = false;
    wrote = writeKeyframeValue(clip.transformX, relative, xPixels, m_autoKeyEnabled, false) || wrote;
    wrote = writeKeyframeValue(clip.transformY, relative, yPixels, m_autoKeyEnabled, false) || wrote;
    wrote = writeKeyframeValue(clip.transformW, relative, qMax(1.0, widthPixels), m_autoKeyEnabled, false)
            || wrote;
    wrote = writeKeyframeValue(clip.transformH, relative, qMax(1.0, heightPixels), m_autoKeyEnabled, false)
            || wrote;
    if (!wrote) {
        emit transformBlocked(tr("Turn on Auto keyframes to change this"));
        return;
    }

    if (!m_previewDragActive)
        beginPreviewDrag(tr("Transform clip"));

    emitPreviewFrame();
}

void AppController::previewSetClipRotation(int trackIndex, int clipIndex, double degrees)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    const drift::TimeUs relative = qMax<drift::TimeUs>(0, m_playheadUs - clip.timelineStart);
    if (!writeKeyframeValue(clip.rotation, relative, degrees, m_autoKeyEnabled, false)) {
        emit transformBlocked(tr("Turn on Auto keyframes to rotate this"));
        return;
    }

    if (!m_previewDragActive)
        beginPreviewDrag(tr("Rotate clip"));

    emitPreviewFrame();
}

void AppController::previewSetClipKeyframe(int trackIndex, int clipIndex, const QString &prop,
                                           double atSeconds, double value)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    const drift::TimeUs rel = qMax<drift::TimeUs>(0, drift::secondsToUs(atSeconds) - clip.timelineStart);
    if (!writeClipPropValue(clip, prop, rel, value, m_autoKeyEnabled, /*force=*/false)) {
        emit transformBlocked(tr("Turn on Auto keyframes to edit this"));
        return;
    }

    if (!m_previewDragActive)
        beginPreviewDrag(tr("Edit keyframe"));

    emitPreviewFrame();
}

void AppController::previewSetEffectParam(int trackIndex, int clipIndex, int effectIndex,
                                          const QString &key, double value)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    if (effectIndex < 0 || effectIndex >= clip.effects.size())
        return;
    if (key.isEmpty())
        return;

    if (!m_previewDragActive)
        beginPreviewDrag(tr("Edit effect"));

    const EffectPresetEntry *def = effectDefForId(clip.effects[effectIndex].catalogId);
    bool asBoolean = false;
    if (def) {
        for (const drift::EffectParamSpec &param : def->meta.parameters) {
            if (param.key == key) {
                asBoolean = param.isBoolean();
                break;
            }
        }
    }
    if (asBoolean)
        clip.effects[effectIndex].parameters.insert(key, value > 0.5);
    else
        clip.effects[effectIndex].parameters.insert(key, value);
    emitPreviewFrame();
}

void AppController::previewSetClipSpeed(int trackIndex, int clipIndex, double speed)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    if (clip.type != drift::ClipType::Video && clip.type != drift::ClipType::Audio)
        return;

    if (!m_previewDragActive)
        beginPreviewDrag(tr("Speed changed"));

    clip.speed = qBound(0.25, speed, 4.0);
    clip.syncSrcOutFromSpeed(sourceDurationForClip(clip));
    syncLinkedPartnersFrom(m_project, clip);
    emitPreviewFrame();
}

void AppController::previewSetClipFade(int trackIndex, int clipIndex, double fadeInSeconds, double fadeOutSeconds)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    if (!m_previewDragActive)
        beginPreviewDrag(tr("Adjust fade"));

    drift::Clip &clip = track.clips[clipIndex];
    drift::TimeUs fin = qMax<drift::TimeUs>(0, drift::secondsToUs(fadeInSeconds));
    drift::TimeUs fout = qMax<drift::TimeUs>(0, drift::secondsToUs(fadeOutSeconds));
    fin = qMin(fin, clip.timelineDuration);
    fout = qMin(fout, clip.timelineDuration - fin);
    clip.fadeInUs = fin;
    clip.fadeOutUs = fout;
    if (clip.type != drift::ClipType::Audio && clip.type != drift::ClipType::Subtitle) {
        if (fin > 0) {
            clip.animIn.kind = drift::ClipAnimKind::Fade;
            clip.animIn.durationUs = fin;
            clip.animIn.curve = clip.fadeCurve;
            clip.animIn.shape = clip.fadeShape;
        } else if (clip.animIn.kind == drift::ClipAnimKind::Fade) {
            clip.animIn.kind = drift::ClipAnimKind::None;
        }
        if (fout > 0) {
            clip.animOut.kind = drift::ClipAnimKind::Fade;
            clip.animOut.durationUs = fout;
            clip.animOut.curve = clip.fadeCurve;
            clip.animOut.shape = clip.fadeShape;
        } else if (clip.animOut.kind == drift::ClipAnimKind::Fade) {
            clip.animOut.kind = drift::ClipAnimKind::None;
        }
    }
    syncLinkedPartnersFrom(m_project, clip);
    emitPreviewFrame();
}

void AppController::previewSetClipMask(int trackIndex, int clipIndex, const QVariantMap &maskMap)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    if (!m_previewDragActive)
        beginPreviewDrag(tr("Mask changed"));

    track.clips[clipIndex].mask = maskFromMap(maskMap);
    emitPreviewFrame();
}

void AppController::commitPreviewDrag()
{
    if (!m_previewDragActive)
        return;

    const QString text = m_previewDragText.isEmpty() ? tr("Edit clip") : m_previewDragText;
    if (!m_mcpUndoSuspended)
        m_undoStack.push(new drift::ProjectSnapshotCommand(&m_project, m_previewDragBefore, m_project, text));
    m_previewDragActive = false;
    finishEdit(text);
}

void AppController::cancelPreviewDrag()
{
    if (!m_previewDragActive)
        return;

    m_project = m_previewDragBefore;
    m_previewDragActive = false;
    emitPreviewFrame();
}

void AppController::setClipStart(int trackIndex, int clipIndex, double start)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    const drift::Project before = m_project;
    drift::Clip &clip = track.clips[clipIndex];
    const drift::TimeUs oldStart = clip.timelineStart;
    clip.timelineStart = drift::resolveClipStart(m_project, track, clipIndex, drift::secondsToUs(start),
                                                 clip.timelineDuration, m_snapEnabled, m_playheadUs,
                                                 extraSnapTargets());
    applyRippleShift(track, clipIndex, clip.timelineStart - oldStart);
    pushProjectEdit(before, tr("Start updated"));
    finishEdit(tr("Start updated"));
}

void AppController::setClipDuration(int trackIndex, int clipIndex, double duration)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    const drift::Project before = m_project;
    drift::Clip &clip = track.clips[clipIndex];
    const drift::TimeUs maxSource = sourceDurationForClip(clip);
    const bool syntheticVisual = isSyntheticTimelineClip(clip.type);
    const drift::TimeUs maxSourceSpan =
        syntheticVisual ? syntheticClipMaxDurationUs()
                        : (clip.reverse ? clip.srcOut : (maxSource > clip.srcIn ? maxSource - clip.srcIn : 0));
    const drift::TimeUs maxDuration =
        syntheticVisual
            ? maxSourceSpan
            : (clip.effectiveSpeed() > 0.0
                   ? static_cast<drift::TimeUs>(
                         llround(static_cast<double>(maxSourceSpan) / clip.effectiveSpeed()))
                   : maxSourceSpan);
    clip.timelineDuration = qBound(drift::kMinClipDurationUs, drift::secondsToUs(duration), maxDuration);
    if (syntheticVisual) {
        syncSyntheticSourceRange(clip);
    } else {
        const drift::TimeUs span = clip.sourceSpanUs();
        if (clip.reverse)
            clip.srcIn = qMax<drift::TimeUs>(0, clip.srcOut - span);
        else
            clip.srcOut = qMin(clip.srcIn + span, maxSource);
    }
    pushProjectEdit(before, tr("Duration updated"));
    finishEdit(tr("Duration updated"));
}

void AppController::setClipTextContent(int trackIndex, int clipIndex, const QString &text)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    if (clip.type != drift::ClipType::Text)
        return;

    const drift::Project before = m_project;
    clip.textContent = text.trimmed();
    clip.name = clip.textContent.left(32);
    pushProjectEdit(before, tr("Text updated"));
    finishEdit(tr("Text updated"));
}

void AppController::setClipName(int trackIndex, int clipIndex, const QString &name)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    if (clip.name == trimmed)
        return;

    // Clip nests Qt-implicitly-shared maps (keyframes, effects). A plain Project
    // copy can still alias those payloads across undo snapshots; detach first.
    const drift::Project before = m_project.detachedCopy();
    clip.name = trimmed;
    pushProjectEdit(before, tr("Rename clip"));
    finishEdit(tr("Clip renamed"));
}

void AppController::previewSetClipTextContent(int trackIndex, int clipIndex, const QString &text)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    if (clip.type != drift::ClipType::Text)
        return;

    if (clip.textContent == text && clip.name == text.left(32))
        return;

    clip.textContent = text;
    clip.name = text.left(32);

    if (!m_previewDragActive)
        beginPreviewDrag(tr("Edit text"));

    emitPreviewFrame();
}

void AppController::commitTextEdit(int trackIndex, int clipIndex, const QString &text)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    if (clip.type != drift::ClipType::Text)
        return;

    const QString trimmed = text.trimmed();
    if (m_previewDragActive) {
        clip.textContent = trimmed;
        clip.name = trimmed.left(32);
        commitPreviewDrag();
        return;
    }

    if (clip.textContent == trimmed && clip.name == trimmed.left(32))
        return;

    const drift::Project before = m_project;
    clip.textContent = trimmed;
    clip.name = trimmed.left(32);
    pushProjectEdit(before, tr("Text updated"));
    finishEdit(tr("Text updated"));
}

void AppController::beginTextEdit(int trackIndex, int clipIndex)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    const drift::Track &track = m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;
    if (track.clips.at(clipIndex).type != drift::ClipType::Text)
        return;

    if (!m_previewDragActive)
        beginPreviewDrag(tr("Edit text"));

    // Hide this clip from the composited frame; the QML inline editor stands in
    // for it while the user types. Committing goes through commitTextEdit.
    m_playback.setEditingClipId(track.clips.at(clipIndex).id);
    if (!m_inlineTextEditing) {
        m_inlineTextEditing = true;
        emit inlineTextEditingChanged();
    }
}

void AppController::endTextEdit()
{
    if (m_inlineTextEditing) {
        m_inlineTextEditing = false;
        emit inlineTextEditingChanged();
    }
    syncTextOverlaySkip();
}

void AppController::syncTextOverlaySkip()
{
    // Inline edit owns the skip id until it ends; then keep the composited raster
    // hidden for the selected text clip so the QML overlay stays crisp (the baked
    // preview texture is downscaled and looks soft when upscaled).
    if (m_inlineTextEditing)
        return;

    QString id;
    if (!m_playing && isValidClipIndex(m_selectedTrack, m_selectedClip)) {
        const drift::Clip &clip = m_project.tracks().at(m_selectedTrack).clips.at(m_selectedClip);
        if (clip.type == drift::ClipType::Text && clip.containsTime(m_playheadUs))
            id = clip.id;
    }
    m_playback.setEditingClipId(id);
}

void AppController::setSubtitleCues(int trackIndex, int clipIndex, const QVariantList &cues)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    if (clip.type != drift::ClipType::Subtitle)
        return;

    const drift::Project before = m_project;
    clip.subtitleCues = subtitleCuesFromMap(cues);
    clip.name = drift::subtitleClipName(clip.subtitleCues);
    pushProjectEdit(before, tr("Subtitles updated"));
    finishEdit(tr("Subtitles updated"));
}

void AppController::previewSetSubtitleCues(int trackIndex, int clipIndex, const QVariantList &cues)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    if (clip.type != drift::ClipType::Subtitle)
        return;

    if (!m_previewDragActive)
        beginPreviewDrag(tr("Adjust subtitle timing"));

    clip.subtitleCues = subtitleCuesFromMap(cues);
    clip.name = drift::subtitleClipName(clip.subtitleCues);
    emitPreviewFrame();
}

double AppController::subtitleLocalPlayheadSeconds(int trackIndex, int clipIndex) const
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return -1.0;

    const drift::Track &track = m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return -1.0;

    const drift::Clip &clip = track.clips.at(clipIndex);
    if (clip.type != drift::ClipType::Subtitle || !clip.containsTime(m_playheadUs))
        return -1.0;

    return drift::usToSeconds(m_playheadUs - clip.timelineStart);
}

void AppController::upsertSubtitleCueAtPlayhead(int trackIndex, int clipIndex, const QString &text)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    if (clip.type != drift::ClipType::Subtitle)
        return;

    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return;

    const drift::TimeUs localUs =
        qBound(drift::TimeUs{0}, m_playheadUs - clip.timelineStart, clip.timelineDuration);
    const int existingIndex = drift::subtitleCueIndexAt(clip.subtitleCues, localUs);

    const drift::Project before = m_project;
    if (existingIndex >= 0) {
        clip.subtitleCues[existingIndex].text = trimmed;
    } else {
        drift::SubtitleCue cue;
        cue.startUs = localUs;

        drift::TimeUs nextStart = clip.timelineDuration;
        for (const drift::SubtitleCue &existing : clip.subtitleCues) {
            if (existing.startUs > localUs)
                nextStart = qMin(nextStart, existing.startUs);
        }
        cue.endUs = qMin(clip.timelineDuration, qMax(localUs + kDefaultSubtitleCueDurationUs, localUs + 1));
        cue.endUs = qMin(cue.endUs, nextStart);
        if (cue.endUs <= cue.startUs)
            cue.endUs = qMin(clip.timelineDuration, cue.startUs + 1);
        cue.text = trimmed;
        clip.subtitleCues.append(cue);
        drift::sortSubtitleCues(clip.subtitleCues);
    }

    clip.name = drift::subtitleClipName(clip.subtitleCues);
    pushProjectEdit(before, tr("Subtitle cue updated"));
    finishEdit(tr("Subtitle cue updated"));
}

void AppController::seekToSubtitleCue(int trackIndex, int clipIndex, int cueIndex)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    const drift::Track &track = m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    const drift::Clip &clip = track.clips.at(clipIndex);
    if (clip.type != drift::ClipType::Subtitle || cueIndex < 0 || cueIndex >= clip.subtitleCues.size())
        return;

    setPlayheadSeconds(drift::usToSeconds(clip.timelineStart + clip.subtitleCues.at(cueIndex).startUs));
}

void AppController::setTextStyle(int trackIndex, int clipIndex, const QVariantMap &m)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    if (clip.type != drift::ClipType::Text && clip.type != drift::ClipType::Subtitle)
        return;

    const drift::Project before = m_project;
    drift::TextStyle &s = clip.textStyle;
    if (m.contains(QStringLiteral("fontFamily")))
        s.fontFamily = m.value(QStringLiteral("fontFamily")).toString();
    if (m.contains(QStringLiteral("pixelSize")))
        s.pixelSize = qBound(8, m.value(QStringLiteral("pixelSize")).toInt(), 800);
    if (m.contains(QStringLiteral("fontWeight")))
        s.fontWeight = qBound(100, m.value(QStringLiteral("fontWeight")).toInt(), 900);
    if (m.contains(QStringLiteral("italic")))
        s.italic = m.value(QStringLiteral("italic")).toBool();
    if (m.contains(QStringLiteral("color")))
        s.color = QColor(m.value(QStringLiteral("color")).toString());
    if (m.contains(QStringLiteral("align")))
        s.align = drift::textAlignFromString(m.value(QStringLiteral("align")).toString());
    if (m.contains(QStringLiteral("valign")))
        s.valign = drift::textVAlignFromString(m.value(QStringLiteral("valign")).toString());
    if (m.contains(QStringLiteral("wordWrap")))
        s.wordWrap = m.value(QStringLiteral("wordWrap")).toBool();
    if (m.contains(QStringLiteral("lineHeight")))
        s.lineHeight = qBound(0.5, m.value(QStringLiteral("lineHeight")).toDouble(), 4.0);
    if (m.contains(QStringLiteral("letterSpacing")))
        s.letterSpacing = m.value(QStringLiteral("letterSpacing")).toDouble();
    if (m.contains(QStringLiteral("outlineEnabled")))
        s.outlineEnabled = m.value(QStringLiteral("outlineEnabled")).toBool();
    if (m.contains(QStringLiteral("outlineWidth")))
        s.outlineWidth = qMax(0.0, m.value(QStringLiteral("outlineWidth")).toDouble());
    if (m.contains(QStringLiteral("outlineColor")))
        s.outlineColor = QColor(m.value(QStringLiteral("outlineColor")).toString());
    if (m.contains(QStringLiteral("shadowEnabled")))
        s.shadowEnabled = m.value(QStringLiteral("shadowEnabled")).toBool();
    if (m.contains(QStringLiteral("shadowOffsetX")))
        s.shadowOffsetX = m.value(QStringLiteral("shadowOffsetX")).toDouble();
    if (m.contains(QStringLiteral("shadowOffsetY")))
        s.shadowOffsetY = m.value(QStringLiteral("shadowOffsetY")).toDouble();
    if (m.contains(QStringLiteral("shadowBlur")))
        s.shadowBlur = qMax(0.0, m.value(QStringLiteral("shadowBlur")).toDouble());
    if (m.contains(QStringLiteral("shadowOpacity")))
        s.shadowOpacity = qBound(0.0, m.value(QStringLiteral("shadowOpacity")).toDouble(), 1.0);
    if (m.contains(QStringLiteral("shadowColor")))
        s.shadowColor = QColor(m.value(QStringLiteral("shadowColor")).toString());
    if (m.contains(QStringLiteral("glowEnabled")))
        s.glowEnabled = m.value(QStringLiteral("glowEnabled")).toBool();
    if (m.contains(QStringLiteral("glowColor")))
        s.glowColor = QColor(m.value(QStringLiteral("glowColor")).toString());
    if (m.contains(QStringLiteral("glowRadius")))
        s.glowRadius = qMax(0.0, m.value(QStringLiteral("glowRadius")).toDouble());
    if (m.contains(QStringLiteral("glowOpacity")))
        s.glowOpacity = qBound(0.0, m.value(QStringLiteral("glowOpacity")).toDouble(), 1.0);
    if (m.contains(QStringLiteral("boxEnabled")))
        s.boxEnabled = m.value(QStringLiteral("boxEnabled")).toBool();
    if (m.contains(QStringLiteral("boxColor")))
        s.boxColor = QColor(m.value(QStringLiteral("boxColor")).toString());
    if (m.contains(QStringLiteral("boxPadding")))
        s.boxPadding = qMax(0.0, m.value(QStringLiteral("boxPadding")).toDouble());
    if (m.contains(QStringLiteral("boxRadius")))
        s.boxRadius = qMax(0.0, m.value(QStringLiteral("boxRadius")).toDouble());
    if (m.contains(QStringLiteral("underlineEnabled")))
        s.underlineEnabled = m.value(QStringLiteral("underlineEnabled")).toBool();
    if (m.contains(QStringLiteral("underlineColor")))
        s.underlineColor = QColor(m.value(QStringLiteral("underlineColor")).toString());
    if (m.contains(QStringLiteral("underlineWidth")))
        s.underlineWidth = qMax(0.0, m.value(QStringLiteral("underlineWidth")).toDouble());
    if (m.contains(QStringLiteral("underlineOffset")))
        s.underlineOffset = m.value(QStringLiteral("underlineOffset")).toDouble();
    applyTextHighlightPatch(&s.wordHighlight, m.value(QStringLiteral("wordHighlight")).toMap());
    applyWordAccentPatch(&s.accent, m.value(QStringLiteral("accent")).toMap());
    applyTextAnimationPatch(&s.animIn, m.value(QStringLiteral("animIn")).toMap());
    applyTextAnimationPatch(&s.animOut, m.value(QStringLiteral("animOut")).toMap());
    // A hand-edited style is no longer the pack it came from, so the picker stops showing one as
    // selected. Alignment and wrapping are layout, not look, and leave the pack intact.
    static const QSet<QString> kLayoutOnlyKeys = {QStringLiteral("align"), QStringLiteral("valign"),
                                                  QStringLiteral("wordWrap")};
    for (auto it = m.constBegin(); it != m.constEnd(); ++it) {
        if (!kLayoutOnlyKeys.contains(it.key())) {
            s.packId.clear();
            break;
        }
    }
    pushProjectEdit(before, tr("Edit text style"));
    finishEdit(tr("Text style updated"));
}

void AppController::applyTextPreset(int trackIndex, int clipIndex, const QString &presetId)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    if (clip.type != drift::ClipType::Text && clip.type != drift::ClipType::Subtitle)
        return;

    const drift::TextStyle *preset = drift::textStyleForPresetId(presetId);
    if (!preset)
        return;

    const drift::Project before = m_project;
    clip.textStyle = *preset;
    clip.textStyle.packId = presetId;
    pushProjectEdit(before, tr("Apply text preset"));
    finishEdit(tr("Text preset applied"));
}

QVariantList AppController::textPresets() const
{
    QVariantList out;
    for (const drift::TextPreset &preset : drift::textPresets()) {
        out.append(QVariantMap{
            {QStringLiteral("id"), preset.id},
            {QStringLiteral("label"), preset.label},
            {QStringLiteral("style"), textStyleToMap(preset.style)},
        });
    }
    return out;
}

QVariantList AppController::fontCategories() const
{
    QVariantList out;
    for (const auto &category : ::fontCategories()) {
        out.append(QVariantMap{
            {QStringLiteral("id"), category.first},
            {QStringLiteral("label"), category.second},
        });
    }
    return out;
}

QVariantList AppController::fontCatalog() const
{
    QVariantList out;
    QMap<QString, QString> labels;
    for (const auto &category : ::fontCategories())
        labels.insert(category.first, category.second);

    for (const FontFamilyEntry &entry : ::fontCatalog()) {
        QVariantList weights;
        for (int weight : entry.weights())
            weights.append(weight);

        out.append(QVariantMap{
            {QStringLiteral("id"), entry.id},
            {QStringLiteral("family"), entry.family},
            {QStringLiteral("qtFamily"), entry.qtFamily},
            {QStringLiteral("category"), entry.category},
            {QStringLiteral("categoryLabel"), labels.value(entry.category, entry.category)},
            {QStringLiteral("weights"), weights},
            {QStringLiteral("hasItalic"), entry.hasItalic()},
        });
    }
    return out;
}

void AppController::previewSetTextRect(int trackIndex, int clipIndex, double xPixels, double yPixels,
                                       double widthPixels, double heightPixels, int pixelSize)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    if (clip.type != drift::ClipType::Text && clip.type != drift::ClipType::Subtitle)
        return;

    // The rect follows the same auto-key rules as previewSetClipRect. The glyph size is a plain
    // style field rather than a keyframed track, so it is always applied — the two move together
    // under one undo entry, because resizing a text clip should scale what you see, not just the
    // invisible wrap container.
    const drift::TimeUs relative = qMax<drift::TimeUs>(0, m_playheadUs - clip.timelineStart);
    bool wrote = false;
    wrote = writeKeyframeValue(clip.transformX, relative, xPixels, m_autoKeyEnabled, false) || wrote;
    wrote = writeKeyframeValue(clip.transformY, relative, yPixels, m_autoKeyEnabled, false) || wrote;
    wrote = writeKeyframeValue(clip.transformW, relative, qMax(1.0, widthPixels), m_autoKeyEnabled, false)
            || wrote;
    wrote = writeKeyframeValue(clip.transformH, relative, qMax(1.0, heightPixels), m_autoKeyEnabled, false)
            || wrote;

    const int clamped = qBound(8, pixelSize, 800);
    if (clip.textStyle.pixelSize != clamped) {
        clip.textStyle.pixelSize = clamped;
        wrote = true;
    }
    if (!wrote)
        return;

    if (!m_previewDragActive)
        beginPreviewDrag(tr("Resize text"));

    emitPreviewFrame();
}

void AppController::setClipBlendMode(int trackIndex, int clipIndex, const QString &mode)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    const drift::Project before = m_project;
    track.clips[clipIndex].blendMode = drift::blendModeFromString(mode);
    pushProjectEdit(before, tr("Blend mode changed"));
    finishEdit(tr("Blend mode updated"));
}

void AppController::setClipSpeed(int trackIndex, int clipIndex, double speed)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    if (clip.type != drift::ClipType::Video && clip.type != drift::ClipType::Audio)
        return;

    const drift::Project before = m_project;
    clip.speed = qBound(0.25, speed, 4.0);
    clip.syncSrcOutFromSpeed(sourceDurationForClip(clip));
    syncLinkedPartnersFrom(m_project, clip);
    pushProjectEdit(before, tr("Speed changed"));
    finishEdit(tr("Clip speed updated"));
}

void AppController::setClipReverse(int trackIndex, int clipIndex, bool reverse)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    if (clip.type != drift::ClipType::Video && clip.type != drift::ClipType::Audio)
        return;
    if (clip.reverse == reverse)
        return;

    const drift::Project before = m_project;
    clip.reverse = reverse;
    pushProjectEdit(before, reverse ? tr("Reverse on") : tr("Reverse off"));
    finishEdit(reverse ? tr("Clip reversed") : tr("Clip forward"));
}

void AppController::requestClipReverse(int trackIndex, int clipIndex)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    const drift::Track &track = m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    const drift::Clip clip = track.clips.at(clipIndex);
    const bool needsRender =
        clip.type == drift::ClipType::Video && !clip.path.isEmpty() && clip.srcOut > clip.srcIn
        && drift::ReverseProxyCache::instance()
               .lookup(clip.path, clip.srcIn, clip.srcOut, nullptr)
               .isEmpty();
    if (!needsRender) {
        setClipReverse(trackIndex, clipIndex, true);
        return;
    }

    emit reverseConfirmRequested(trackIndex, clipIndex,
                                 drift::usToSeconds(clip.srcOut - clip.srcIn));
}

void AppController::applyClipReverse(int trackIndex, int clipIndex)
{
    if (m_reverseRendering) {
        setLastMessage(tr("A clip is already being reversed"), QStringLiteral("warning"));
        return;
    }
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    const drift::Track &track = m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    // By value: setClipReverse below runs a full edit cycle, and reading the clip back out of the
    // project afterwards would mean trusting indices across it.
    const drift::Clip clip = track.clips.at(clipIndex);
    const drift::TimeUs sourceDuration = sourceDurationForClip(clip);

    setClipReverse(trackIndex, clipIndex, true);

    // Audio reverses by flipping decoded blocks, which is already cheap. Only video pays the
    // per-frame keyframe seek a proxy exists to avoid.
    if (clip.type != drift::ClipType::Video || clip.path.isEmpty() || clip.srcOut <= clip.srcIn)
        return;
    if (!drift::ReverseProxyCache::instance()
             .lookup(clip.path, clip.srcIn, clip.srcOut, nullptr)
             .isEmpty())
        return;

    // Padding either side so ordinary trim-handle nudges stay inside the rendered range instead
    // of dropping the clip back onto the live path.
    constexpr drift::TimeUs kPadUs = 2 * drift::kUsPerSecond;
    const drift::TimeUs coverIn = qMax<drift::TimeUs>(0, clip.srcIn - kPadUs);
    drift::TimeUs coverOut = clip.srcOut + kPadUs;
    if (sourceDuration > 0)
        coverOut = qMin(coverOut, sourceDuration);

    startReverseRender(clip.path, coverIn, qMax(coverOut, clip.srcOut));
}

void AppController::startReverseRender(const QString &sourcePath, drift::TimeUs coverInUs,
                                       drift::TimeUs coverOutUs)
{
    m_reverseCancel.storeRelaxed(0);
    m_reverseProgress = 0.0;
    emit reverseRenderProgressChanged();
    m_reverseStatus = QStringLiteral("Getting ready…");
    emit reverseRenderStatusChanged();
    m_reverseRendering = true;
    emit reverseRenderingChanged();

    // Playback deliberately keeps running: renderReversed opens its own demuxer rather than
    // driving the shared decode pool, so the clip stays watchable (on the slow path) meanwhile.
    (void)QtConcurrent::run([this, sourcePath, coverInUs, coverOutUs]() {
        auto setProgress = [this](double fraction, const QString &status) {
            QMetaObject::invokeMethod(
                this,
                [this, fraction, status]() {
                    m_reverseProgress = fraction;
                    emit reverseRenderProgressChanged();
                    if (!status.isEmpty() && status != m_reverseStatus) {
                        m_reverseStatus = status;
                        emit reverseRenderStatusChanged();
                    }
                },
                Qt::QueuedConnection);
        };

        auto finish = [this, sourcePath, coverInUs, coverOutUs](bool ok, const QString &message,
                                                                const QString &proxyPath) {
            QMetaObject::invokeMethod(
                this,
                [this, ok, message, proxyPath, sourcePath, coverInUs, coverOutUs]() {
                    m_reverseRendering = false;
                    emit reverseRenderingChanged();
                    m_reverseProgress = ok ? 1.0 : 0.0;
                    emit reverseRenderProgressChanged();
                    m_reverseStatus = ok ? QStringLiteral("Done") : message;
                    emit reverseRenderStatusChanged();
                    if (ok) {
                        drift::ReverseProxyCache::instance().insert(sourcePath, coverInUs,
                                                                    coverOutUs, proxyPath);
                    }
                    setLastMessage(message, ok ? QStringLiteral("info")
                                              : QStringLiteral("error"));
                    // No pushProjectEdit: the proxy is a cache, not project content, and it must
                    // not land in the undo stack. This only asks the compositor to re-read, which
                    // now resolves to the proxy.
                    emitPreviewFrame();
                    emit reverseRenderFinished(ok, message);
                },
                Qt::QueuedConnection);
        };

        const QString proxyPath = drift::newReversePath();
        if (proxyPath.isEmpty()) {
            finish(false, tr("Could not create a reversed file"), {});
            return;
        }

        QString error;
        const bool ok = drift::renderReversed(
            sourcePath, coverInUs, coverOutUs, proxyPath, &error, [&](double fraction) {
                setProgress(fraction, tr("Reversing video…"));
                return m_reverseCancel.loadRelaxed() == 0;
            });
        finish(ok, ok ? QStringLiteral("Reversed clip ready") : error, proxyPath);
    });
}

void AppController::cancelReverseRender()
{
    if (m_reverseRendering)
        m_reverseCancel.storeRelaxed(1);
}

bool AppController::clipHasReverseProxy(int trackIndex, int clipIndex) const
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return true;
    const drift::Track &track = m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return true;

    // Anything with nothing to render reports true, so the "not rendered" hint stays hidden for
    // clips a proxy would do nothing for.
    const drift::Clip &clip = track.clips.at(clipIndex);
    if (!clip.reverse || clip.type != drift::ClipType::Video || clip.path.isEmpty())
        return true;
    return !drift::ReverseProxyCache::instance()
                .lookup(clip.path, clip.srcIn, clip.srcOut, nullptr)
                .isEmpty();
}

void AppController::setClipFlip(int trackIndex, int clipIndex, bool flipH, bool flipV)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    if (clip.type == drift::ClipType::Audio)
        return;
    if (clip.flipH == flipH && clip.flipV == flipV)
        return;

    const drift::Project before = m_project;
    clip.flipH = flipH;
    clip.flipV = flipV;
    pushProjectEdit(before, tr("Flip changed"));
    finishEdit(tr("Clip flip updated"));
}

void AppController::setClipRotationSnap(int trackIndex, int clipIndex, double degrees)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    if (clip.type == drift::ClipType::Audio)
        return;

    // Normalize to (-180, 180]
    double snapped = degrees;
    while (snapped > 180.0)
        snapped -= 360.0;
    while (snapped <= -180.0)
        snapped += 360.0;

    const drift::Project before = m_project;
    // Snap replaces the rotation curve with a single constant key so inspector
    // chips and preview stay in lockstep (no leftover mid-curve keys).
    clip.rotation = {};
    clip.rotation.setKeyframe(0, snapped);
    pushProjectEdit(before, tr("Rotation snapped"));
    finishEdit(tr("Rotation set to %1°").arg(snapped, 0, 'f', 0));
}

bool AppController::canMergeSelection() const
{
    int leftTrack = -1;
    int leftClip = -1;
    int rightTrack = -1;
    int rightClip = -1;

    QList<QPair<int, int>> pairs = m_selection;
    if (pairs.isEmpty() && m_selectedTrack >= 0 && m_selectedClip >= 0)
        pairs.append(qMakePair(m_selectedTrack, m_selectedClip));

    if (pairs.size() == 2) {
        leftTrack = pairs[0].first;
        leftClip = pairs[0].second;
        rightTrack = pairs[1].first;
        rightClip = pairs[1].second;
        if (leftTrack != rightTrack || !isValidClipIndex(leftTrack, leftClip)
            || !isValidClipIndex(rightTrack, rightClip))
            return false;
        const drift::Clip &a = m_project.tracks().at(leftTrack).clips.at(leftClip);
        const drift::Clip &b = m_project.tracks().at(rightTrack).clips.at(rightClip);
        if (a.timelineStart <= b.timelineStart)
            return drift::clipsCanMerge(a, b);
        return drift::clipsCanMerge(b, a);
    }

    if (pairs.size() == 1) {
        const int trackIndex = pairs[0].first;
        const int clipIndex = pairs[0].second;
        if (!isValidClipIndex(trackIndex, clipIndex))
            return false;
        const drift::Track &track = m_project.tracks().at(trackIndex);
        const drift::Clip &left = track.clips.at(clipIndex);
        // Prefer merging with the clip that starts at this clip's end.
        for (int i = 0; i < track.clips.size(); ++i) {
            if (i == clipIndex)
                continue;
            if (drift::clipsCanMerge(left, track.clips.at(i)))
                return true;
        }
        return false;
    }

    return false;
}

void AppController::mergeSelectedClips()
{
    int trackIndex = -1;
    int leftIndex = -1;
    int rightIndex = -1;

    QList<QPair<int, int>> pairs = m_selection;
    if (pairs.isEmpty() && m_selectedTrack >= 0 && m_selectedClip >= 0)
        pairs.append(qMakePair(m_selectedTrack, m_selectedClip));

    if (pairs.size() == 2) {
        if (pairs[0].first != pairs[1].first)
            return;
        trackIndex = pairs[0].first;
        if (!isValidClipIndex(trackIndex, pairs[0].second) || !isValidClipIndex(trackIndex, pairs[1].second))
            return;
        const drift::Clip &a = m_project.tracks().at(trackIndex).clips.at(pairs[0].second);
        const drift::Clip &b = m_project.tracks().at(trackIndex).clips.at(pairs[1].second);
        if (a.timelineStart <= b.timelineStart) {
            leftIndex = pairs[0].second;
            rightIndex = pairs[1].second;
        } else {
            leftIndex = pairs[1].second;
            rightIndex = pairs[0].second;
        }
    } else if (pairs.size() == 1) {
        trackIndex = pairs[0].first;
        leftIndex = pairs[0].second;
        if (!isValidClipIndex(trackIndex, leftIndex))
            return;
        const drift::Track &track = m_project.tracks().at(trackIndex);
        const drift::Clip &left = track.clips.at(leftIndex);
        for (int i = 0; i < track.clips.size(); ++i) {
            if (i == leftIndex)
                continue;
            if (drift::clipsCanMerge(left, track.clips.at(i))) {
                rightIndex = i;
                break;
            }
        }
        if (rightIndex < 0)
            return;
    } else {
        return;
    }

    drift::Track &track = m_project.tracks()[trackIndex];
    const drift::Clip &left = track.clips.at(leftIndex);
    const drift::Clip &right = track.clips.at(rightIndex);
    if (!drift::clipsCanMerge(left, right))
        return;

    const drift::Project before = m_project;
    drift::Clip merged = drift::mergeClips(left, right);
    // Remove right first if its index is higher so leftIndex stays valid.
    if (rightIndex > leftIndex) {
        track.clips.removeAt(rightIndex);
        track.clips[leftIndex] = merged;
    } else {
        track.clips.removeAt(leftIndex);
        track.clips[rightIndex] = merged;
        leftIndex = rightIndex;
    }

    pushProjectEdit(before, tr("Clips merged"));
    finishEdit(tr("Clips merged"));
    selectClip(trackIndex, leftIndex);
}

bool AppController::canSeparateAudioSelection() const
{
    QList<QPair<int, int>> pairs = m_selection;
    if (pairs.isEmpty() && m_selectedTrack >= 0 && m_selectedClip >= 0)
        pairs.append(qMakePair(m_selectedTrack, m_selectedClip));

    for (const QPair<int, int> &pair : pairs) {
        if (!isValidClipIndex(pair.first, pair.second))
            continue;
        if (clipHasEmbeddedAudio(m_project, m_assetLibrary,
                                 m_project.tracks().at(pair.first).clips.at(pair.second)))
            return true;
    }
    return false;
}

bool AppController::canUnlinkSelection() const
{
    QList<QPair<int, int>> pairs = m_selection;
    if (pairs.isEmpty() && m_selectedTrack >= 0 && m_selectedClip >= 0)
        pairs.append(qMakePair(m_selectedTrack, m_selectedClip));

    for (const QPair<int, int> &pair : pairs) {
        if (!isValidClipIndex(pair.first, pair.second))
            continue;
        if (!m_project.tracks().at(pair.first).clips.at(pair.second).linkId.isEmpty())
            return true;
    }
    return false;
}

void AppController::separateAudioFromSelection()
{
    QList<QPair<int, int>> pairs = m_selection;
    if (pairs.isEmpty() && m_selectedTrack >= 0 && m_selectedClip >= 0)
        pairs.append(qMakePair(m_selectedTrack, m_selectedClip));
    if (pairs.isEmpty())
        return;

    const drift::Project before = m_project;
    bool changed = false;
    QSet<QString> detachedVideoIds;
    for (const QPair<int, int> &pair : pairs) {
        if (!isValidClipIndex(pair.first, pair.second))
            continue;

        drift::Clip &clip = m_project.tracks()[pair.first].clips[pair.second];
        if (clip.type != drift::ClipType::Video || detachedVideoIds.contains(clip.id))
            continue;
        if (detachEmbeddedAudioFromVideo(m_project, m_assetLibrary, clip)) {
            detachedVideoIds.insert(clip.id);
            changed = true;
        }
    }

    if (!changed)
        return;

    if (m_selectedTrack >= 0 && m_selectedClip >= 0)
        m_selection = selectionWithLinkedPartners(m_project, m_selectedTrack, m_selectedClip);

    pushProjectEdit(before, tr("Audio separated"));
    finishEdit(tr("Audio separated"));
}

void AppController::unlinkSelectedClips()
{
    QList<QPair<int, int>> pairs = m_selection;
    if (pairs.isEmpty() && m_selectedTrack >= 0 && m_selectedClip >= 0)
        pairs.append(qMakePair(m_selectedTrack, m_selectedClip));
    if (pairs.isEmpty())
        return;

    const drift::Project before = m_project;
    bool changed = false;
    QSet<QString> clearedLinkIds;
    for (const QPair<int, int> &pair : pairs) {
        if (!isValidClipIndex(pair.first, pair.second))
            continue;

        drift::Clip &clip = m_project.tracks()[pair.first].clips[pair.second];
        if (clip.linkId.isEmpty() || clearedLinkIds.contains(clip.linkId))
            continue;

        clearedLinkIds.insert(clip.linkId);
        for (drift::Track &track : m_project.tracks()) {
            for (drift::Clip &candidate : track.clips) {
                if (candidate.linkId == clip.linkId)
                    candidate.linkId.clear();
            }
        }
        changed = true;
    }

    if (!changed)
        return;

    if (m_selectedTrack >= 0 && m_selectedClip >= 0)
        m_selection = {qMakePair(m_selectedTrack, m_selectedClip)};

    pushProjectEdit(before, tr("Clips unlinked"));
    finishEdit(tr("Audio unlinked"));
}

void AppController::setClipFade(int trackIndex, int clipIndex, double fadeInSeconds, double fadeOutSeconds)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    drift::TimeUs fin = qMax<drift::TimeUs>(0, drift::secondsToUs(fadeInSeconds));
    drift::TimeUs fout = qMax<drift::TimeUs>(0, drift::secondsToUs(fadeOutSeconds));
    fin = qMin(fin, clip.timelineDuration);
    fout = qMin(fout, clip.timelineDuration - fin);
    if (clip.fadeInUs == fin && clip.fadeOutUs == fout)
        return;

    const drift::Project before = m_project;
    // First fade on an audio clip defaults to equal-power (constant loudness).
    const bool hadFade = clip.fadeInUs > 0 || clip.fadeOutUs > 0;
    if (!hadFade && (fin > 0 || fout > 0) && clip.type == drift::ClipType::Audio)
        clip.fadeCurve = drift::FadeCurve::EqualPower;
    clip.fadeInUs = fin;
    clip.fadeOutUs = fout;

    // Timeline fade handles are the CapCut Fade In/Out animation for visual clips.
    if (clip.type != drift::ClipType::Audio && clip.type != drift::ClipType::Subtitle) {
        if (fin > 0) {
            clip.animIn.kind = drift::ClipAnimKind::Fade;
            clip.animIn.durationUs = fin;
            clip.animIn.curve = clip.fadeCurve;
            clip.animIn.shape = clip.fadeShape;
            clip.animIn.ease = drift::clipAnimCurveToEase(clip.fadeCurve);
        } else if (clip.animIn.kind == drift::ClipAnimKind::Fade) {
            clip.animIn.kind = drift::ClipAnimKind::None;
        }
        if (fout > 0) {
            clip.animOut.kind = drift::ClipAnimKind::Fade;
            clip.animOut.durationUs = fout;
            clip.animOut.curve = clip.fadeCurve;
            clip.animOut.shape = clip.fadeShape;
            clip.animOut.ease = drift::clipAnimCurveToEase(clip.fadeCurve);
        } else if (clip.animOut.kind == drift::ClipAnimKind::Fade) {
            clip.animOut.kind = drift::ClipAnimKind::None;
        }
    }

    syncLinkedPartnersFrom(m_project, clip);
    pushProjectEdit(before, tr("Fade updated"));
    finishEdit(tr("Fade updated"));
}

void AppController::setClipFadeCurve(int trackIndex, int clipIndex, const QString &curve)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    const drift::FadeCurve newCurve = drift::fadeCurveFromString(curve);
    if (clip.fadeCurve == newCurve
        && (newCurve != drift::FadeCurve::Custom || !clip.fadeShape.isEmpty()))
        return;

    const drift::Project before = m_project;
    clip.fadeCurve = newCurve;
    if (newCurve == drift::FadeCurve::Custom && clip.fadeShape.isEmpty())
        clip.fadeShape = drift::FadeShape::smoothPreset();
    // Keep CapCut Fade animations on the same style as the timeline curve editor.
    if (clip.animIn.kind == drift::ClipAnimKind::Fade) {
        clip.animIn.curve = newCurve;
        clip.animIn.shape = clip.fadeShape;
        clip.animIn.ease = drift::clipAnimCurveToEase(newCurve);
    }
    if (clip.animOut.kind == drift::ClipAnimKind::Fade) {
        clip.animOut.curve = newCurve;
        clip.animOut.shape = clip.fadeShape;
        clip.animOut.ease = drift::clipAnimCurveToEase(newCurve);
    }
    syncLinkedPartnersFrom(m_project, clip);
    pushProjectEdit(before, tr("Fade curve changed"));
    finishEdit(tr("Fade curve updated"));
}

void AppController::setClipAnimation(int trackIndex, int clipIndex, const QString &which,
                                     const QVariantMap &patch)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    const drift::Clip &clip = track.clips.at(clipIndex);
    if (clip.type == drift::ClipType::Audio || clip.type == drift::ClipType::Subtitle)
        return;
    if (patch.isEmpty())
        return;

    const bool isIn = which == QLatin1String("animIn");
    const bool isOut = which == QLatin1String("animOut");
    if (!isIn && !isOut)
        return;

    drift::ClipAnimation next = isIn ? clip.animIn : clip.animOut;
    if (patch.contains(QStringLiteral("kind")))
        next.kind = drift::clipAnimKindFromString(patch.value(QStringLiteral("kind")).toString());
    if (patch.contains(QStringLiteral("duration")))
        next.durationUs =
            drift::secondsToUs(qBound(0.0, patch.value(QStringLiteral("duration")).toDouble(), 10.0));
    if (patch.contains(QStringLiteral("curve"))) {
        next.curve = drift::fadeCurveFromString(patch.value(QStringLiteral("curve")).toString());
        next.ease = drift::clipAnimCurveToEase(next.curve);
        if (next.curve == drift::FadeCurve::Custom && next.shape.isEmpty())
            next.shape = clip.fadeShape.isEmpty() ? drift::FadeShape::smoothPreset() : clip.fadeShape;
    } else if (patch.contains(QStringLiteral("ease"))) {
        next.ease = drift::clipAnimEaseFromString(patch.value(QStringLiteral("ease")).toString());
        next.curve = drift::clipAnimEaseToCurve(next.ease);
    }

    if (next.kind != drift::ClipAnimKind::None && next.durationUs <= 0)
        next.durationUs = 500000;
    next.durationUs = qMin(next.durationUs, clip.timelineDuration);

    const drift::ClipAnimation &current = isIn ? clip.animIn : clip.animOut;
    if (next.kind == current.kind && next.durationUs == current.durationUs
        && next.curve == current.curve && next.ease == current.ease)
        return;

    const drift::Project before = m_project;
    drift::Clip &mutableClip = m_project.tracks()[trackIndex].clips[clipIndex];
    if (isIn)
        mutableClip.animIn = next;
    else
        mutableClip.animOut = next;

    // CapCut: Fade kind owns the timeline edge fade on that side; motion clears it.
    if (isIn) {
        if (next.kind == drift::ClipAnimKind::Fade) {
            mutableClip.fadeInUs = next.durationUs;
            mutableClip.fadeCurve = next.curve;
            if (next.curve == drift::FadeCurve::Custom)
                mutableClip.fadeShape = next.shape;
        } else {
            mutableClip.fadeInUs = 0;
        }
    } else {
        if (next.kind == drift::ClipAnimKind::Fade) {
            mutableClip.fadeOutUs = next.durationUs;
            mutableClip.fadeCurve = next.curve;
            if (next.curve == drift::FadeCurve::Custom)
                mutableClip.fadeShape = next.shape;
        } else {
            mutableClip.fadeOutUs = 0;
        }
    }

    pushProjectEdit(before, tr("Clip animation changed"));
    finishEdit(tr("Clip animation updated"));
}

void AppController::setShapeStyle(int trackIndex, int clipIndex, const QVariantMap &m)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    if (clipIndex < 0 || clipIndex >= m_project.tracks().at(trackIndex).clips.size())
        return;
    if (m_project.tracks().at(trackIndex).clips.at(clipIndex).type != drift::ClipType::Shape)
        return;

    // Snapshot before taking any reference into m_project: Project holds an implicitly shared
    // QList, so writing through a reference obtained before the copy would mutate the snapshot too
    // and the undo step would be a no-op.
    const drift::Project before = m_project;
    drift::Clip &clip = m_project.tracks()[trackIndex].clips[clipIndex];
    drift::ShapeStyle &s = clip.shapeStyle;
    if (m.contains(QStringLiteral("kind"))) {
        // The inspector addresses shapes by catalog id, so "circle" resolves to Ellipse here.
        const QString id = m.value(QStringLiteral("kind")).toString();
        s.kind = drift::shapeKindFromString(id);
        if (const drift::ShapeCatalogEntry *entry = drift::shapeCatalogEntry(id))
            clip.name = entry->label;
    }
    if (m.contains(QStringLiteral("fillKind")))
        s.fillKind = drift::shapeFillKindFromString(m.value(QStringLiteral("fillKind")).toString());
    if (m.contains(QStringLiteral("fill")))
        s.fill = QColor(m.value(QStringLiteral("fill")).toString());
    if (m.contains(QStringLiteral("fillSecondary")))
        s.fillSecondary = QColor(m.value(QStringLiteral("fillSecondary")).toString());
    if (m.contains(QStringLiteral("gradientAngle")))
        s.gradientAngle = m.value(QStringLiteral("gradientAngle")).toDouble();
    if (m.contains(QStringLiteral("stroke")))
        s.stroke = QColor(m.value(QStringLiteral("stroke")).toString());
    if (m.contains(QStringLiteral("strokeWidth")))
        s.strokeWidth = qBound(0.0, m.value(QStringLiteral("strokeWidth")).toDouble(), 200.0);
    if (m.contains(QStringLiteral("strokeStyle")))
        s.strokeStyle =
            drift::shapeStrokeStyleFromString(m.value(QStringLiteral("strokeStyle")).toString());
    if (m.contains(QStringLiteral("cornerRadius")))
        s.cornerRadius = qBound(0.0, m.value(QStringLiteral("cornerRadius")).toDouble(), 2000.0);
    if (m.contains(QStringLiteral("points")))
        s.points = qBound(3, m.value(QStringLiteral("points")).toInt(), 60);
    if (m.contains(QStringLiteral("innerRatio")))
        s.innerRatio = qBound(0.05, m.value(QStringLiteral("innerRatio")).toDouble(), 0.95);
    if (m.contains(QStringLiteral("headSize")))
        s.headSize = qBound(0.05, m.value(QStringLiteral("headSize")).toDouble(), 0.9);
    if (m.contains(QStringLiteral("thickness")))
        s.thickness = qBound(0.05, m.value(QStringLiteral("thickness")).toDouble(), 1.0);
    if (m.contains(QStringLiteral("tailX")))
        s.tailX = qBound(0.08, m.value(QStringLiteral("tailX")).toDouble(), 0.92);
    if (m.contains(QStringLiteral("tailSize")))
        s.tailSize = qBound(0.05, m.value(QStringLiteral("tailSize")).toDouble(), 0.5);

    // Slider drags wrap their stream of updates in beginPreviewDrag/commitPreviewDrag, which
    // already holds the "before" snapshot — pushing here too would give one undo step per frame.
    if (m_previewDragActive) {
        emitPreviewFrame();
        return;
    }

    pushProjectEdit(before, tr("Shape style changed"));
    finishEdit(tr("Shape style updated"));
}

void AppController::setClipMask(int trackIndex, int clipIndex, const QVariantMap &maskMap)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    const drift::Project before = m_project;
    track.clips[clipIndex].mask = maskFromMap(maskMap);
    pushProjectEdit(before, tr("Mask changed"));
    finishEdit(tr("Clip mask updated"));
}

void AppController::addTransition(int trackIndex, int clipIndex, const QString &kind, double durationSeconds)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (!trackAllowsTransitions(track.type))
        return;

    const int partnerIndex = findTransitionPartnerIndex(track, clipIndex);
    if (partnerIndex < 0)
        return;

    const drift::Clip &fromClip = track.clips.at(clipIndex);
    const drift::Clip &toClip = track.clips.at(partnerIndex);
    const drift::TimeUs overlapUs = drift::physicalOverlapDurationUs(fromClip, toClip);
    const drift::TimeUs requestedUs = qMax<drift::TimeUs>(drift::secondsToUs(0.1), drift::secondsToUs(durationSeconds));
    const drift::TimeUs durationUs = overlapUs > 0 ? overlapUs : requestedUs;
    const QString kindId = transitionDefForId(kind) ? kind : QStringLiteral("crossfade");

    for (drift::Transition &existing : track.transitions) {
        if (existing.fromClipId == fromClip.id && existing.toClipId == toClip.id) {
            const drift::Project before = m_project;
            existing.kindId = kindId;
            existing.parameters.clear(); // overrides belong to the old package
            existing.durationUs = durationUs;
            pushProjectEdit(before, tr("Replace transition"));
            finishEdit(tr("Transition updated"));
            selectTransition(trackIndex, clipIndex);
            return;
        }
    }

    drift::Transition transition;
    transition.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    transition.fromClipId = fromClip.id;
    transition.toClipId = toClip.id;
    transition.kindId = kindId;
    transition.durationUs = durationUs;

    const drift::Project before = m_project;
    track.transitions.append(transition);
    pushProjectEdit(before, tr("Add transition"));
    finishEdit(tr("Transition added"));
    selectTransition(trackIndex, clipIndex);
}

void AppController::removeTransition(int trackIndex, const QString &transitionId)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    const drift::Project before = m_project;
    for (int i = 0; i < track.transitions.size(); ++i) {
        if (track.transitions.at(i).id != transitionId)
            continue;

        const drift::Transition transition = track.transitions.at(i);
        if (m_selectedTransitionTrack == trackIndex && m_selectedTransitionLeftClip >= 0) {
            const QString fromId = track.clips.value(m_selectedTransitionLeftClip).id;
            if (transition.fromClipId == fromId)
                clearTransitionSelection();
        }

        // Physical overlaps auto-sync a crossfade; separate the clips so removal sticks.
        drift::Clip *fromClip = nullptr;
        drift::Clip *toClip = nullptr;
        for (drift::Clip &clip : track.clips) {
            if (clip.id == transition.fromClipId)
                fromClip = &clip;
            else if (clip.id == transition.toClipId)
                toClip = &clip;
        }
        if (fromClip && toClip && drift::clipsPhysicallyOverlap(*fromClip, *toClip))
            toClip->timelineStart = fromClip->timelineEnd();

        track.transitions.removeAt(i);
        pushProjectEdit(before, tr("Remove transition"));
        finishEdit(tr("Transition removed"));
        return;
    }
}

void AppController::setTransitionDuration(int trackIndex, const QString &transitionId, double durationSeconds)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    const drift::Project before = m_project;
    for (drift::Transition &transition : track.transitions) {
        if (transition.id != transitionId)
            continue;

        const drift::TimeUs durationUs =
            qMax<drift::TimeUs>(drift::secondsToUs(0.1), drift::secondsToUs(durationSeconds));
        drift::Clip *fromClip = nullptr;
        drift::Clip *toClip = nullptr;
        for (drift::Clip &clip : track.clips) {
            if (clip.id == transition.fromClipId)
                fromClip = &clip;
            else if (clip.id == transition.toClipId)
                toClip = &clip;
        }

        if (fromClip && toClip && drift::clipsPhysicallyOverlap(*fromClip, *toClip)) {
            const drift::TimeUs maxOverlap =
                qMin(fromClip->timelineDuration, toClip->timelineDuration) - drift::secondsToUs(0.05);
            const drift::TimeUs clamped = qBound(drift::secondsToUs(0.1), durationUs, qMax(drift::secondsToUs(0.1), maxOverlap));
            toClip->timelineStart = fromClip->timelineEnd() - clamped;
            transition.durationUs = clamped;
        } else {
            transition.durationUs = durationUs;
        }

        pushProjectEdit(before, tr("Transition duration"));
        finishEdit(tr("Transition duration updated"));
        emit selectedTransitionDataChanged();
        return;
    }
}

void AppController::setTransitionKind(int trackIndex, const QString &transitionId, const QString &kind)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    if (!transitionDefForId(kind))
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    const drift::Project before = m_project;
    for (drift::Transition &transition : track.transitions) {
        if (transition.id == transitionId) {
            if (transition.kindId == kind)
                return;
            transition.kindId = kind;
            transition.parameters.clear(); // overrides belong to the old package
            pushProjectEdit(before, tr("Transition kind"));
            finishEdit(tr("Transition kind updated"));
            emit selectedTransitionDataChanged();
            return;
        }
    }
}

namespace {

// Transition parameters are declared floats or bools, matching the effect parameter UI.
QVariant coerceTransitionParam(const TransitionPresetEntry *def, const QString &key, double value)
{
    if (def) {
        for (const drift::EffectParamSpec &param : def->meta.parameters) {
            if (param.key == key)
                return param.isBoolean() ? QVariant(value > 0.5) : QVariant(value);
        }
    }
    return value;
}

drift::Transition *findTransition(drift::Track &track, const QString &transitionId)
{
    for (drift::Transition &transition : track.transitions) {
        if (transition.id == transitionId)
            return &transition;
    }
    return nullptr;
}

} // namespace

void AppController::previewSetTransitionParam(int trackIndex, const QString &transitionId,
                                              const QString &key, double value)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size() || key.isEmpty())
        return;

    drift::Transition *transition = findTransition(m_project.tracks()[trackIndex], transitionId);
    if (!transition)
        return;

    if (!m_previewDragActive)
        beginPreviewDrag(tr("Edit transition"));

    transition->parameters.insert(
        key, coerceTransitionParam(transitionDefForId(transition->kindId), key, value));
    emitPreviewFrame();
}

void AppController::setTransitionParam(int trackIndex, const QString &transitionId, const QString &key,
                                       double value)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size() || key.isEmpty())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    drift::Transition *transition = findTransition(track, transitionId);
    if (!transition)
        return;

    const drift::Project before = m_project;
    transition->parameters.insert(
        key, coerceTransitionParam(transitionDefForId(transition->kindId), key, value));
    pushProjectEdit(before, tr("Edit transition"));
    finishEdit(tr("Transition updated"));
    emit selectedTransitionDataChanged();
}

QVariantMap AppController::transitionBetweenClips(int trackIndex, int clipIndex) const
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return {};

    const drift::Track &track = m_project.tracks().at(trackIndex);
    const int partnerIndex = findTransitionPartnerIndex(track, clipIndex);
    if (partnerIndex < 0)
        return {};

    const QString fromId = track.clips.at(clipIndex).id;
    const QString toId = track.clips.at(partnerIndex).id;
    for (const drift::Transition &transition : track.transitions) {
        if (transition.fromClipId == fromId && transition.toClipId == toId)
            return transitionToMap(track, transition);
    }
    return {};
}

QVariantList AppController::transitionKinds() const
{
    const QList<TransitionPresetEntry> &catalog = transitionCatalog();

    QVariantList result;
    result.reserve(catalog.size());
    for (const TransitionPresetEntry &def : catalog) {
        QVariantList params;
        for (const drift::EffectParamSpec &p : def.meta.parameters) {
            params.append(QVariantMap{
                {QStringLiteral("key"), p.key},
                {QStringLiteral("label"), p.label},
                {QStringLiteral("min"), p.min},
                {QStringLiteral("max"), p.max},
                {QStringLiteral("default"), p.defaultValue},
                {QStringLiteral("isBoolean"), p.isBoolean()},
                {QStringLiteral("type"), p.typeName()},
            });
        }
        result.append(QVariantMap{
            {QStringLiteral("kind"), def.meta.id},
            {QStringLiteral("label"), def.meta.displayName},
            {QStringLiteral("category"), def.meta.category},
            {QStringLiteral("previewStripPath"), def.previewStripPath},
            {QStringLiteral("previewFrames"), def.previewFrames},
            {QStringLiteral("params"), params},
        });
    }
    return result;
}

QVariantList AppController::transitionCategories() const
{
    QVariantList out;
    for (const auto &entry : ::transitionCategories()) {
        out.append(QVariantMap{
            {QStringLiteral("id"), entry.first},
            {QStringLiteral("label"), entry.second},
        });
    }
    return out;
}

QVariantMap AppController::selectedTransitionData() const
{
    if (m_selectedTransitionTrack < 0 || m_selectedTransitionLeftClip < 0)
        return {};
    return transitionBetweenClips(m_selectedTransitionTrack, m_selectedTransitionLeftClip);
}

void AppController::selectTransition(int trackIndex, int leftClipIndex)
{
    if (transitionBetweenClips(trackIndex, leftClipIndex).isEmpty())
        return;

    m_selectedTransitionTrack = trackIndex;
    m_selectedTransitionLeftClip = leftClipIndex;
    m_selectedTrack = trackIndex;
    m_selectedClip = leftClipIndex;
    m_selection = {qMakePair(trackIndex, leftClipIndex)};
    emit selectionChanged();
    emit selectedTransitionDataChanged();
}

void AppController::clearTransitionSelection()
{
    if (m_selectedTransitionTrack < 0 && m_selectedTransitionLeftClip < 0)
        return;

    m_selectedTransitionTrack = -1;
    m_selectedTransitionLeftClip = -1;
    emit selectedTransitionDataChanged();
}

void AppController::setClipKeyframe(int trackIndex, int clipIndex, const QString &prop, double atSeconds,
                                    double value)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    const drift::Project before = m_project;
    const drift::TimeUs rel = qMax<drift::TimeUs>(0, drift::secondsToUs(atSeconds) - clip.timelineStart);
    if (!writeClipPropValue(clip, prop, rel, value, m_autoKeyEnabled, /*force=*/true))
        return;
    pushProjectEdit(before, tr("Add keyframe"));
    finishEdit(tr("Keyframe set"));
}

void AppController::removeClipKeyframe(int trackIndex, int clipIndex, const QString &prop, double atSeconds)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    drift::KeyframeTrack<double> *kt = keyframeTrackForProp(clip, prop, /*createIfMissing=*/false);
    if (!kt)
        return;

    const drift::Project before = m_project;
    const drift::TimeUs rel = qMax<drift::TimeUs>(0, drift::secondsToUs(atSeconds) - clip.timelineStart);
    const drift::TimeUs nearest = kt->nearestKeyframe(rel, kKeyframeToleranceUs);
    if (nearest < 0)
        return;
    kt->removeKeyframe(nearest);
    pushProjectEdit(before, tr("Remove keyframe"));
    finishEdit(tr("Keyframe removed"));
}

void AppController::previewMoveClipKeyframe(int trackIndex, int clipIndex, const QString &prop,
                                            double fromSeconds, double toSeconds, double value)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    drift::KeyframeTrack<double> *kt = keyframeTrackForProp(clip, prop, /*createIfMissing=*/false);
    if (!kt)
        return;

    if (!m_previewDragActive)
        beginPreviewDrag(tr("Move keyframe"));

    const drift::TimeUs fromRel = qMax<drift::TimeUs>(0, drift::secondsToUs(fromSeconds) - clip.timelineStart);
    const drift::TimeUs toRel = qMax<drift::TimeUs>(0, drift::secondsToUs(toSeconds) - clip.timelineStart);
    const drift::TimeUs nearest = kt->nearestKeyframe(fromRel, kKeyframeToleranceUs);
    if (nearest >= 0)
        kt->removeKeyframe(nearest);
    kt->setKeyframe(toRel, value);
    emitPreviewFrame();
}

// Locates a single key for the tangent editors. `atSeconds` is a timeline position; keys are
// stored clip-relative, and the strip reports them on the timeline, so it converts back here.
drift::Keyframe<double> *AppController::keyframeAt(int trackIndex, int clipIndex,
                                                   const QString &prop, double atSeconds)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return nullptr;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return nullptr;

    drift::Clip &clip = track.clips[clipIndex];
    drift::KeyframeTrack<double> *kt = keyframeTrackForProp(clip, prop, /*createIfMissing=*/false);
    if (!kt || kt->isEmpty())
        return nullptr;

    const drift::TimeUs local = drift::secondsToUs(atSeconds) - clip.timelineStart;
    const drift::TimeUs at = kt->nearestKeyframe(local, drift::kUsPerSecond / 60);
    if (at < 0)
        return nullptr;
    return kt->keyframeRef(at);
}

// Handles are authored in seconds on the QML side and stored in µs. Directions are enforced
// here rather than trusted from the caller: an out-handle reaching backwards (or an in-handle
// forwards) would fold the segment and make the curve multi-valued in time.
void AppController::applyTangents(drift::Keyframe<double> &key, double inDx, double inDy,
                                  double outDx, double outDy, bool corner)
{
    key.inDx = qMin(0.0, static_cast<double>(drift::secondsToUs(inDx)));
    key.outDx = qMax(0.0, static_cast<double>(drift::secondsToUs(outDx)));
    key.inDy = inDy;
    key.outDy = outDy;
    key.corner = corner;
    // A key can be shaped by hand and holding at the same time in the data, but the hold wins
    // when evaluated, which would silently discard the drag. Dropping it is the honest move.
    key.hold = false;
}

// The inspector's live readout. Exposed so QML does not have to carry its own copy of the
// interpolation math — which is no longer a two-line lerp now that keys have tangents.
double AppController::propertyValueAt(int trackIndex, int clipIndex, const QString &prop,
                                      double atSeconds, double fallback) const
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return fallback;

    const drift::Track &track = m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return fallback;

    const drift::Clip &clip = track.clips.at(clipIndex);
    const drift::KeyframeTrack<double> *kt = keyframeTrackForProp(clip, prop);
    if (!kt || kt->isEmpty())
        return propertyBaseValue(trackIndex, clipIndex, prop, fallback);

    return kt->evaluateAt(drift::secondsToUs(atSeconds) - clip.timelineStart);
}

// What an unkeyed property evaluates to. These mirror the defaults the compositor passes to
// transformValue (FrameCompositor.cpp) — an unkeyed clip fills the canvas at full opacity —
// so a curve drawn from these sits where the clip actually is rather than at zero.
double AppController::propertyBaseValue(int trackIndex, int clipIndex, const QString &prop,
                                        double fallback) const
{
    if (prop == QLatin1String("width"))
        return m_project.width();
    if (prop == QLatin1String("height"))
        return m_project.height();
    if (prop == QLatin1String("opacity") || prop == QLatin1String("volume"))
        return 1.0;
    if (prop == QLatin1String("x") || prop == QLatin1String("y")
        || prop == QLatin1String("rotation")) {
        return 0.0;
    }

    // Effect params fall back to the effect's own static value, which is what the compositor
    // reads for an unkeyed param.
    if (trackIndex >= 0 && trackIndex < m_project.tracks().size()) {
        const drift::Track &track = m_project.tracks().at(trackIndex);
        if (clipIndex >= 0 && clipIndex < track.clips.size()) {
            const drift::Clip &clip = track.clips.at(clipIndex);
            int effectIndex = -1;
            QString paramKey;
            if (parseEffectProp(prop, &effectIndex, &paramKey)
                && effectIndex >= 0 && effectIndex < clip.effects.size()) {
                const QVariant value = clip.effects.at(effectIndex).parameters.value(paramKey);
                if (value.isValid())
                    return value.toDouble();
            }
        }
    }
    return fallback;
}

QVariantList AppController::clipKeyframes(int trackIndex, int clipIndex, const QString &prop) const
{
    QVariantList out;
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return out;

    const drift::Track &track = m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return out;

    const drift::Clip &clip = track.clips.at(clipIndex);
    const drift::KeyframeTrack<double> *kt = keyframeTrackForProp(clip, prop);
    if (!kt)
        return out;

    return keyframeListToVariant(*kt, clip.timelineStart);
}

bool AppController::clipPropertyKeyframesEnabled(int trackIndex, int clipIndex,
                                                 const QString &prop) const
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return true;

    const drift::Track &track = m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return true;

    const drift::KeyframeTrack<double> *kt =
        keyframeTrackForProp(track.clips.at(clipIndex), prop);
    return kt ? kt->enabled() : true;
}

void AppController::setClipPropertyKeyframesEnabled(int trackIndex, int clipIndex,
                                                    const QString &prop, bool enabled)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    if (clipIndex < 0 || clipIndex >= m_project.tracks().at(trackIndex).clips.size())
        return;

    {
        // With no keys there is no animation to switch off, and minting an empty track just to
        // hold the flag would make an untouched property look animated.
        const drift::KeyframeTrack<double> *kt =
            keyframeTrackForProp(m_project.tracks().at(trackIndex).clips.at(clipIndex), prop);
        if (!kt || kt->isEmpty() || kt->enabled() == enabled)
            return;
    }

    // The snapshot is taken before any mutable reference into the project: QList is implicitly
    // shared, so writing through a reference obtained earlier would land in the copy as well and
    // leave undo with nothing to restore.
    const drift::Project before = m_project;
    drift::Clip &clip = m_project.tracks()[trackIndex].clips[clipIndex];
    keyframeTrackForProp(clip, prop, /*createIfMissing=*/false)->setEnabled(enabled);
    pushProjectEdit(before, enabled ? tr("Enable keyframes")
                                    : tr("Disable keyframes"));
    finishEdit(enabled ? tr("Keyframes enabled") : tr("Keyframes disabled"));
}

void AppController::toggleClipPropertyKeyframesEnabled(int trackIndex, int clipIndex,
                                                       const QString &prop)
{
    setClipPropertyKeyframesEnabled(trackIndex, clipIndex, prop,
                                    !clipPropertyKeyframesEnabled(trackIndex, clipIndex, prop));
}

// Every property of the clip that carries an animation — the keyframe strip's series list.
//
// A lone key at the clip's start is how a *static* transform value is stored (every clip gets one
// for x/y/w/h when it is added), so that alone is not an animation. Effect params keep their static
// value in Effect::parameters, so for those any key at all means animated.
QStringList AppController::clipAnimatedProperties(int trackIndex, int clipIndex) const
{
    QStringList out;
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return out;

    const drift::Track &track = m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return out;

    const drift::Clip &clip = track.clips.at(clipIndex);

    static const QStringList transformProps = {
        QStringLiteral("x"),       QStringLiteral("y"),       QStringLiteral("width"),
        QStringLiteral("height"),  QStringLiteral("rotation"), QStringLiteral("opacity"),
        QStringLiteral("volume"),
    };
    for (const QString &prop : transformProps) {
        const drift::KeyframeTrack<double> *kt = keyframeTrackForProp(clip, prop);
        if (!kt || kt->isEmpty())
            continue;
        if (kt->keyframes().size() == 1 && kt->keyframes().firstKey() == 0)
            continue;
        out.append(prop);
    }

    for (int i = 0; i < clip.effects.size(); ++i) {
        const QMap<QString, drift::KeyframeTrack<double>> &params = clip.effects.at(i).paramKeyframes;
        for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
            if (!it.value().isEmpty())
                out.append(QStringLiteral("fx.%1.%2").arg(i).arg(it.key()));
        }
    }
    return out;
}

void AppController::setKeyframeInterpolation(int trackIndex, int clipIndex, const QString &prop,
                                             const QString &mode)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    drift::KeyframeTrack<double> *kt = keyframeTrackForProp(clip, prop, /*createIfMissing=*/true);
    if (!kt || kt->isEmpty())
        return;

    // Presets act on the key at the playhead. Without one there is nothing to shape — the mode
    // is no longer a property-wide setting that can be armed ahead of the first key.
    const drift::TimeUs local = m_playheadUs - clip.timelineStart;
    const drift::TimeUs at = kt->nearestKeyframe(local, drift::kUsPerSecond / 30);
    if (at < 0)
        return;

    const drift::Project before = m_project;
    kt->setEasing(at, drift::interpolationFromString(mode));
    pushProjectEdit(before, tr("Keyframe easing changed"));
    finishEdit(tr("Keyframe easing updated"));
}

void AppController::setKeyframeTangents(int trackIndex, int clipIndex, const QString &prop,
                                        double atSeconds, double inDx, double inDy, double outDx,
                                        double outDy, bool corner)
{
    drift::Keyframe<double> *key = keyframeAt(trackIndex, clipIndex, prop, atSeconds);
    if (!key)
        return;

    const drift::Project before = m_project;
    applyTangents(*key, inDx, inDy, outDx, outDy, corner);
    pushProjectEdit(before, tr("Keyframe curve changed"));
    finishEdit(tr("Keyframe curve updated"));
}

void AppController::previewSetKeyframeTangents(int trackIndex, int clipIndex, const QString &prop,
                                               double atSeconds, double inDx, double inDy,
                                               double outDx, double outDy, bool corner)
{
    drift::Keyframe<double> *key = keyframeAt(trackIndex, clipIndex, prop, atSeconds);
    if (!key)
        return;

    applyTangents(*key, inDx, inDy, outDx, outDy, corner);
    emit tracksChanged();
    emit selectedClipDataChanged();
    emit projectMutated();
}

void AppController::setKeyframeHold(int trackIndex, int clipIndex, const QString &prop,
                                    double atSeconds, bool hold)
{
    drift::Keyframe<double> *key = keyframeAt(trackIndex, clipIndex, prop, atSeconds);
    if (!key || key->hold == hold)
        return;

    const drift::Project before = m_project;
    key->hold = hold;
    pushProjectEdit(before, tr("Keyframe hold changed"));
    finishEdit(hold ? tr("Keyframe holds") : tr("Keyframe interpolates"));
}

void AppController::resetClipTransform(int trackIndex, int clipIndex)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    if (clip.type == drift::ClipType::Audio)
        return;

    const drift::Project before = m_project;
    clip.opacity = {};
    clip.transformX = {};
    clip.transformY = {};
    clip.transformW = {};
    clip.transformH = {};
    clip.rotation = {};
    clip.flipH = false;
    clip.flipV = false;
    setClipLayoutPixels(clip, 0, 0, m_project.width(), m_project.height());
    pushProjectEdit(before, tr("Reset transform"));
    finishEdit(tr("Transform reset"));
}

QVariantList AppController::effectCatalog() const
{
    QVariantList out;
    for (const EffectPresetEntry &def : ::effectCatalog()) {
        QVariantList params;
        for (const drift::EffectParamSpec &p : def.meta.parameters) {
            params.append(QVariantMap{
                {QStringLiteral("key"), p.key},
                {QStringLiteral("label"), p.label},
                {QStringLiteral("min"), p.min},
                {QStringLiteral("max"), p.max},
                {QStringLiteral("default"), p.defaultValue},
                {QStringLiteral("isBoolean"), p.isBoolean()},
                {QStringLiteral("type"), p.typeName()},
            });
        }
        out.append(QVariantMap{
            {QStringLiteral("id"), def.meta.id},
            {QStringLiteral("label"), def.meta.displayName},
            {QStringLiteral("displayName"), def.meta.displayName},
            {QStringLiteral("category"), def.meta.category},
            {QStringLiteral("categoryLabel"), effectCategoryLabel(def.meta.category)},
            {QStringLiteral("compositorOnly"), def.meta.compositorOnly},
            {QStringLiteral("thumbnailPath"), def.thumbnailPath},
            {QStringLiteral("params"), params},
        });
    }
    return out;
}

QVariantList AppController::effectCategories() const
{
    QVariantList out;
    for (const auto &entry : ::effectCategories()) {
        out.append(QVariantMap{
            {QStringLiteral("id"), entry.first},
            {QStringLiteral("label"), entry.second},
        });
    }
    return out;
}

void AppController::addEffect(int trackIndex, int clipIndex, const QString &effectId)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    const EffectPresetEntry *def = effectDefForId(effectId);
    if (!def)
        return;

    drift::Effect effect;
    effect.name = def->filterName;
    effect.catalogId = def->meta.id;
    for (auto it = def->fixedParams.constBegin(); it != def->fixedParams.constEnd(); ++it)
        effect.parameters.insert(it.key(), it.value());
    for (const drift::EffectParamSpec &p : def->meta.parameters)
        effect.parameters.insert(p.key, p.defaultVariant());

    const drift::Project before = m_project;
    track.clips[clipIndex].effects.append(effect);
    m_selectedTrack = trackIndex;
    m_selectedClip = clipIndex;
    m_selection = {qMakePair(trackIndex, clipIndex)};
    pushProjectEdit(before, tr("Add effect"));
    finishEdit(tr("Effect added"));
}

namespace {

drift::Effect effectFromCatalogEntry(const EffectPresetEntry &def,
                                     const QMap<QString, QVariant> &overrides)
{
    drift::Effect effect;
    effect.name = def.filterName;
    effect.catalogId = def.meta.id;
    for (auto it = def.fixedParams.constBegin(); it != def.fixedParams.constEnd(); ++it)
        effect.parameters.insert(it.key(), it.value());
    for (const drift::EffectParamSpec &p : def.meta.parameters) {
        const auto overrideIt = overrides.constFind(p.key);
        if (overrideIt != overrides.constEnd())
            effect.parameters.insert(p.key, overrideIt.value());
        else
            effect.parameters.insert(p.key, p.defaultVariant());
    }
    return effect;
}

bool templateSyncNeedsBeats(const QString &sync)
{
    return sync == QLatin1String("onset") || sync == QLatin1String("beat")
           || sync == QLatin1String("bar");
}

bool clipHasMatte(const drift::Clip &clip)
{
    return clip.mask.shape == drift::MaskShape::Matte && !clip.mask.mattePath.isEmpty();
}

drift::Clip deriveMaskedClip(const drift::Clip &source, const drift::Mask &matte, bool invert,
                             const QString &suffix)
{
    drift::Clip clip = source;
    clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    clip.linkId.clear();
    clip.effects.clear();
    clip.audioEffects.clear();
    clip.mask = matte;
    clip.mask.invert = invert;
    clip.name = (source.name.isEmpty()
                     ? QCoreApplication::translate("AppController", "Clip")
                     : source.name)
                + suffix;
    return clip;
}

void applyTemplateLayersToClip(drift::Clip &clip, const QList<EffectTemplateLayer> &layers,
                               const QString &sync, const QList<drift::TimeUs> &syncPoints)
{
    const int baseEffectIndex = clip.effects.size();
    for (const EffectTemplateLayer &layer : layers) {
        const EffectPresetEntry *def = effectDefForId(layer.effectId);
        if (!def)
            continue;
        clip.effects.append(effectFromCatalogEntry(*def, layer.params));
    }

    for (int layerIndex = 0; layerIndex < layers.size(); ++layerIndex) {
        const EffectTemplateLayer &layer = layers.at(layerIndex);
        if (!layer.pulse.valid)
            continue;

        const int effectIndex = baseEffectIndex + layerIndex;
        if (effectIndex >= clip.effects.size())
            continue;

        const QString prop =
            QStringLiteral("fx.%1.%2").arg(effectIndex).arg(layer.pulse.param);
        const drift::TimeUs decayUs =
            static_cast<drift::TimeUs>(qMax(layer.pulse.decayMs, 0)) * 1000;
        if (sync == QLatin1String("clip") && syncPoints.size() >= 2) {
            writeClipPropValue(clip, prop, syncPoints.first(), layer.pulse.peak, false, true);
            writeClipPropValue(clip, prop, syncPoints.last(), layer.pulse.rest, false, true);
            continue;
        }
        for (drift::TimeUs t : syncPoints) {
            writeClipPropValue(clip, prop, t, layer.pulse.peak, false, true);
            writeClipPropValue(clip, prop, t + decayUs, layer.pulse.rest, false, true);
        }
    }
}

void applyTemplateSpeedPulse(drift::Clip &clip, const EffectTemplateSpeedPulse &pulse,
                             const QString &sync, const QList<drift::TimeUs> &syncPoints)
{
    if (!pulse.valid || clip.timelineDuration <= 0)
        return;

    const double baseSpeed = clip.effectiveSpeed();
    QList<drift::SpeedPoint> points;
    points.append({0.0, baseSpeed, 0.0, 0.0, 0.0, 0.0, false});
    points.append({1.0, baseSpeed, 0.0, 0.0, 0.0, 0.0, false});

    auto addPoint = [&](double pos, double speed) {
        pos = qBound(0.0, pos, 1.0);
        for (int i = 0; i < points.size(); ++i) {
            if (qFuzzyCompare(points[i].pos, pos)) {
                points[i].speed = speed;
                return;
            }
        }
        points.append({pos, speed, 0.0, 0.0, 0.0, 0.0, true});
    };

    const drift::TimeUs decayUs =
        static_cast<drift::TimeUs>(qMax(pulse.decayMs, 0)) * 1000;
    const double dur = static_cast<double>(clip.timelineDuration);

    if (sync == QLatin1String("clip") && syncPoints.size() >= 2) {
        addPoint(0.0, pulse.peak);
        addPoint(static_cast<double>(syncPoints.last()) / dur, pulse.rest);
    } else {
        for (drift::TimeUs t : syncPoints) {
            addPoint(static_cast<double>(t) / dur, pulse.peak);
            addPoint(static_cast<double>(t + decayUs) / dur, pulse.rest);
        }
    }

    clip.speedCurve.setPoints(points);
    clip.syncDurationFromSpeedCurve();
}

const EffectTemplateTrack *trackForRole(const EffectTemplateEntry &entry, const QString &role)
{
    for (const EffectTemplateTrack &track : entry.tracks) {
        if (track.role == role)
            return &track;
    }
    return nullptr;
}

bool clipsShareTemplateSource(const drift::Clip &a, const drift::Clip &b)
{
    return a.path == b.path && a.srcIn == b.srcIn && a.srcOut == b.srcOut
           && a.timelineStart == b.timelineStart && a.timelineDuration == b.timelineDuration;
}

bool isDerivedTemplateClipName(const QString &name)
{
    return name.endsWith(QStringLiteral(" (fg)")) || name.endsWith(QStringLiteral(" (bg)"))
           || name.endsWith(QStringLiteral(" (clone)"));
}

struct TemplateStackRefs
{
    int fgTrack = -1;
    int fgClip = -1;
    int bgTrack = -1;
    int bgClip = -1;
    QList<QPair<int, int>> clones;

    bool valid() const { return fgTrack >= 0 && bgTrack >= 0; }
};

TemplateStackRefs findExistingTemplateStack(const drift::Project &project, const drift::Clip &source)
{
    TemplateStackRefs stack;
    for (int t = 0; t < project.tracks().size(); ++t) {
        const drift::Track &track = project.tracks().at(t);
        for (int c = 0; c < track.clips.size(); ++c) {
            const drift::Clip &clip = track.clips.at(c);
            if (!clipsShareTemplateSource(clip, source))
                continue;
            if (clip.name.endsWith(QStringLiteral(" (fg)"))) {
                stack.fgTrack = t;
                stack.fgClip = c;
            } else if (clip.name.endsWith(QStringLiteral(" (bg)"))) {
                stack.bgTrack = t;
                stack.bgClip = c;
            } else if (clip.name.endsWith(QStringLiteral(" (clone)"))) {
                stack.clones.append({t, c});
            }
        }
    }
    return stack;
}

void resetTemplateDerivedClip(drift::Clip &clip, double opacity)
{
    clip.effects.clear();
    clip.opacity.setKeyframe(0, opacity);
    clip.speedCurve = drift::SpeedCurve::flat(1.0);
    clip.syncDurationFromSpeedCurve();
}

} // namespace

QVariantList AppController::effectTemplateCatalog() const
{
    QVariantList out;
    for (const EffectTemplateEntry &entry : ::effectTemplateCatalog()) {
        QVariantList layers;
        for (const EffectTemplateLayer &layer : entry.layers) {
            layers.append(QVariantMap{
                {QStringLiteral("effectId"), layer.effectId},
            });
        }

        QVariantList effectThumbnails;
        QSet<QString> seenEffectIds;
        const auto appendEffectThumb = [&](const QString &effectId) {
            if (effectId.isEmpty() || seenEffectIds.contains(effectId))
                return;
            const EffectPresetEntry *def = effectDefForId(effectId);
            if (!def || def->thumbnailPath.isEmpty())
                return;
            seenEffectIds.insert(effectId);
            effectThumbnails.append(def->thumbnailPath);
        };
        for (const EffectTemplateLayer &layer : entry.layers)
            appendEffectThumb(layer.effectId);
        for (const EffectTemplateTrack &track : entry.tracks) {
            for (const EffectTemplateLayer &layer : track.layers)
                appendEffectThumb(layer.effectId);
        }

        out.append(QVariantMap{
            {QStringLiteral("id"), entry.id},
            {QStringLiteral("label"), entry.displayName},
            {QStringLiteral("displayName"), entry.displayName},
            {QStringLiteral("category"), entry.category},
            {QStringLiteral("categoryLabel"), effectTemplateCategoryLabel(entry.category)},
            {QStringLiteral("sync"), entry.sync},
            {QStringLiteral("thumbnailPath"), entry.thumbnailPath},
            {QStringLiteral("effectThumbnails"), effectThumbnails},
            {QStringLiteral("effectCount"), seenEffectIds.size()},
            {QStringLiteral("requiresSegmentation"), entry.requiresSegmentation},
            {QStringLiteral("layers"), layers},
        });
    }
    return out;
}

QVariantList AppController::effectTemplateCategories() const
{
    QVariantList out;
    for (const auto &entry : ::effectTemplateCategories()) {
        out.append(QVariantMap{
            {QStringLiteral("id"), entry.first},
            {QStringLiteral("label"), entry.second},
        });
    }
    return out;
}

bool AppController::beatAnalysisReadyForClip(const drift::Clip &clip, const QString &sync) const
{
    if (sync == QLatin1String("clip"))
        return true;
    if (m_beatAnalysis.isEmpty() || audioLayoutFingerprint() != m_beatAudioFingerprint)
        return false;

    const double rangeStart = m_beatAnalysis.value(QStringLiteral("rangeStart")).toDouble();
    const double rangeDur = m_beatAnalysis.value(QStringLiteral("rangeDuration")).toDouble();
    const double clipStart = drift::usToSeconds(clip.timelineStart);
    const double clipEnd =
        drift::usToSeconds(clip.timelineStart + clip.timelineDuration);
    if (clipStart < rangeStart - 0.001 || clipEnd > rangeStart + rangeDur + 0.001)
        return false;

    if (sync == QLatin1String("onset"))
        return !m_beatAnalysisRaw.onsets.isEmpty();
    return !m_beatAnalysisRaw.beats.isEmpty();
}

bool AppController::resolveTemplateApplyTarget(int *trackIndex, int *clipIndex) const
{
    if (!trackIndex || !clipIndex)
        return false;
    if (*trackIndex < 0 || *trackIndex >= m_project.tracks().size())
        return false;
    const drift::Track &track = m_project.tracks().at(*trackIndex);
    if (*clipIndex < 0 || *clipIndex >= track.clips.size())
        return false;

    const drift::Clip &selected = track.clips.at(*clipIndex);
    if (!isDerivedTemplateClipName(selected.name))
        return false;

    for (int t = 0; t < m_project.tracks().size(); ++t) {
        const drift::Track &candidateTrack = m_project.tracks().at(t);
        for (int c = 0; c < candidateTrack.clips.size(); ++c) {
            const drift::Clip &candidate = candidateTrack.clips.at(c);
            if (!clipsShareTemplateSource(candidate, selected))
                continue;
            if (isDerivedTemplateClipName(candidate.name))
                continue;
            *trackIndex = t;
            *clipIndex = c;
            return true;
        }
    }
    return false;
}

void AppController::applyEffectTemplateInternal(int trackIndex, int clipIndex,
                                                const EffectTemplateEntry &entry,
                                                const QString &mattePath,
                                                drift::TimeUs matteSrcOffsetUs)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    const drift::Clip sourceClip = track.clips[clipIndex];
    const drift::Project before = m_project;

    drift::Mask matte;
    if (!mattePath.isEmpty()) {
        matte.shape = drift::MaskShape::Matte;
        matte.mattePath = mattePath;
        matte.matteSrcOffsetUs = matteSrcOffsetUs;
    } else if (clipHasMatte(sourceClip)) {
        matte = sourceClip.mask;
    }

    const bool segmented = entry.requiresSegmentation || entry.usesMultiTrack();
    const bool haveMatte = matte.shape == drift::MaskShape::Matte && !matte.mattePath.isEmpty();

    QList<drift::TimeUs> syncPoints;
    const drift::TimeUs clipStart = sourceClip.timelineStart;
    const drift::TimeUs clipEnd = clipStart + sourceClip.timelineDuration;
    if (entry.sync == QLatin1String("clip")) {
        syncPoints.append(0);
        if (sourceClip.timelineDuration > 0)
            syncPoints.append(sourceClip.timelineDuration);
    } else if (entry.sync == QLatin1String("onset")) {
        for (const AudioOnset &onset : std::as_const(m_beatAnalysisRaw.onsets)) {
            const drift::TimeUs at = drift::secondsToUs(onset.seconds);
            if (at >= clipStart && at < clipEnd)
                syncPoints.append(at - clipStart);
        }
    } else {
        int beatIndex = 0;
        for (double beatSeconds : std::as_const(m_beatAnalysisRaw.beats)) {
            const drift::TimeUs at = drift::secondsToUs(beatSeconds);
            if (at >= clipStart && at < clipEnd) {
                if (entry.sync == QLatin1String("bar")) {
                    const int rel = beatIndex - m_beatAnalysisRaw.firstDownbeat;
                    if (rel >= 0 && rel % m_beatAnalysisRaw.beatsPerBar == 0)
                        syncPoints.append(at - clipStart);
                } else {
                    syncPoints.append(at - clipStart);
                }
            }
            ++beatIndex;
        }
    }

    auto applyTrackLayers = [&](drift::Clip &clip, const EffectTemplateTrack &trackDef) {
        applyTemplateLayersToClip(clip, trackDef.layers, entry.sync, syncPoints);
        if (trackDef.opacity < 0.999)
            clip.opacity.setKeyframe(0, trackDef.opacity);
        applyTemplateSpeedPulse(clip, trackDef.speedPulse, entry.sync, syncPoints);
    };

    int selectTrack = trackIndex;
    int selectClip = clipIndex;

    if (segmented && haveMatte) {
        track.clips[clipIndex].mask = matte;

        const bool sourceHidden = sourceClip.opacity.evaluateAt(0) < 0.05;
        const TemplateStackRefs existingStack = findExistingTemplateStack(m_project, sourceClip);
        const bool reuseStack =
            sourceHidden && existingStack.valid()
            && existingStack.clones.size() == entry.clones.count;

        if (reuseStack) {
            track.clips[clipIndex].opacity.setKeyframe(0, 0.0);

            drift::Clip &fgClip = m_project.tracks()[existingStack.fgTrack].clips[existingStack.fgClip];
            drift::Clip &bgClip = m_project.tracks()[existingStack.bgTrack].clips[existingStack.bgClip];
            resetTemplateDerivedClip(fgClip, 1.0);
            resetTemplateDerivedClip(bgClip, 1.0);
            fgClip.mask = matte;
            bgClip.mask = matte;
            bgClip.mask.invert = true;

            selectTrack = existingStack.fgTrack;
            selectClip = existingStack.fgClip;

            if (const EffectTemplateTrack *bgDef = trackForRole(entry, QStringLiteral("background")))
                applyTrackLayers(bgClip, *bgDef);
            if (const EffectTemplateTrack *fgDef = trackForRole(entry, QStringLiteral("foreground")))
                applyTrackLayers(fgClip, *fgDef);

            for (int i = 0; i < existingStack.clones.size(); ++i) {
                const auto &ref = existingStack.clones.at(i);
                drift::Clip &clone = m_project.tracks()[ref.first].clips[ref.second];
                const double opacity = i < entry.clones.opacities.size()
                                           ? entry.clones.opacities.at(i)
                                           : 0.25;
                resetTemplateDerivedClip(clone, opacity);
                clone.mask = matte;
                if (i < entry.clones.scales.size()) {
                    const double scale = entry.clones.scales.at(i);
                    const double w = sourceClip.transformW.isEmpty()
                                           ? static_cast<double>(m_project.width())
                                           : sourceClip.transformW.evaluateAt(0);
                    const double h = sourceClip.transformH.isEmpty()
                                           ? static_cast<double>(m_project.height())
                                           : sourceClip.transformH.evaluateAt(0);
                    clone.transformW.setKeyframe(0, w * scale);
                    clone.transformH.setKeyframe(0, h * scale);
                }
                if (const EffectTemplateTrack *cloneDef =
                        trackForRole(entry, QStringLiteral("clone"))) {
                    applyTrackLayers(clone, *cloneDef);
                } else if (i == entry.clones.count - 1) {
                    const EffectPresetEntry *trail = effectDefForId(QStringLiteral("motion_trail"));
                    if (trail)
                        clone.effects.append(effectFromCatalogEntry(*trail, {}));
                }
            }
        } else {
            // Hide the untouched source; fg/bg/clone layers replace it visually.
            track.clips[clipIndex].opacity.setKeyframe(0, 0.0);

            const int fgTrack =
                drift::insertTrackAboveForClipType(m_project, trackIndex, drift::ClipType::Video);
            m_project.tracks()[fgTrack].clips.append(
                deriveMaskedClip(sourceClip, matte, false, QStringLiteral(" (fg)")));

            const int bgTrack =
                drift::insertTrackAboveForClipType(m_project, fgTrack + 1, drift::ClipType::Video);
            m_project.tracks()[bgTrack].clips.append(
                deriveMaskedClip(sourceClip, matte, true, QStringLiteral(" (bg)")));

            selectTrack = fgTrack;
            selectClip = 0;

            if (const EffectTemplateTrack *bgDef = trackForRole(entry, QStringLiteral("background")))
                applyTrackLayers(m_project.tracks()[bgTrack].clips[0], *bgDef);
            if (const EffectTemplateTrack *fgDef = trackForRole(entry, QStringLiteral("foreground")))
                applyTrackLayers(m_project.tracks()[fgTrack].clips[0], *fgDef);

            if (entry.clones.count > 0) {
                int insertAbove = fgTrack + 1;
                for (int i = 0; i < entry.clones.count; ++i) {
                    const int cloneTrack = drift::insertTrackAboveForClipType(
                        m_project, insertAbove, drift::ClipType::Video);
                    drift::Clip clone =
                        deriveMaskedClip(sourceClip, matte, false, QStringLiteral(" (clone)"));
                    const double opacity = i < entry.clones.opacities.size()
                                               ? entry.clones.opacities.at(i)
                                               : 0.25;
                    clone.opacity.setKeyframe(0, opacity);
                    if (i < entry.clones.scales.size()) {
                        const double scale = entry.clones.scales.at(i);
                        const double w = sourceClip.transformW.isEmpty()
                                               ? static_cast<double>(m_project.width())
                                               : sourceClip.transformW.evaluateAt(0);
                        const double h = sourceClip.transformH.isEmpty()
                                               ? static_cast<double>(m_project.height())
                                               : sourceClip.transformH.evaluateAt(0);
                        clone.transformW.setKeyframe(0, w * scale);
                        clone.transformH.setKeyframe(0, h * scale);
                    }
                    if (const EffectTemplateTrack *cloneDef =
                            trackForRole(entry, QStringLiteral("clone"))) {
                        applyTrackLayers(clone, *cloneDef);
                    } else if (i == entry.clones.count - 1) {
                        const EffectPresetEntry *trail = effectDefForId(QStringLiteral("motion_trail"));
                        if (trail)
                            clone.effects.append(effectFromCatalogEntry(*trail, {}));
                    }
                    m_project.tracks()[cloneTrack].clips.append(clone);
                    insertAbove = cloneTrack + 1;
                }
            }
        }
    } else if (entry.usesMultiTrack()) {
        if (const EffectTemplateTrack *fgDef = trackForRole(entry, QStringLiteral("foreground")))
            applyTrackLayers(track.clips[clipIndex], *fgDef);
    } else {
        applyTemplateLayersToClip(track.clips[clipIndex], entry.layers, entry.sync, syncPoints);
        if (entry.speedPulse.valid)
            applyTemplateSpeedPulse(track.clips[clipIndex], entry.speedPulse, entry.sync,
                                    syncPoints);
    }

    m_selectedTrack = selectTrack;
    m_selectedClip = selectClip;
    m_selection = {qMakePair(selectTrack, selectClip)};
    pushProjectEdit(before, tr("Apply effect template"));
    finishEdit(tr("Template applied"));
}

void AppController::applyEffectTemplate(int trackIndex, int clipIndex, const QString &templateId)
{
    const EffectTemplateEntry *entry = effectTemplateForId(templateId);
    if (!entry)
        return;

    resolveTemplateApplyTarget(&trackIndex, &clipIndex);

    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    const drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    const drift::Clip &clip = track.clips[clipIndex];

    if (templateSyncNeedsBeats(entry->sync) && !beatAnalysisReadyForClip(clip, entry->sync)) {
        m_pendingEffectTemplate = PendingEffectTemplate{trackIndex, clipIndex, templateId};
        if (!m_beatAnalysisRunning) {
            analyzeBeats(drift::usToSeconds(clip.timelineStart),
                         drift::usToSeconds(clip.timelineDuration));
        }
        return;
    }

    const bool needsSegment =
        (entry->requiresSegmentation || entry->usesMultiTrack()) && !clipHasMatte(clip);
    if (needsSegment) {
        if (!segmentationAvailable()) {
            setLastMessage(
                tr("This effect needs a subject cutout — open Extras to install it"),
                QStringLiteral("warning"));
            return;
        }
        if (m_segmenting) {
            m_pendingEffectTemplate = PendingEffectTemplate{trackIndex, clipIndex, templateId};
            return;
        }
        m_pendingEffectTemplate = PendingEffectTemplate{trackIndex, clipIndex, templateId};
        openSegmentationForTemplate(trackIndex, clipIndex);
        return;
    }

    m_pendingEffectTemplate.reset();
    applyEffectTemplateInternal(trackIndex, clipIndex, *entry);
}

void AppController::removeEffect(int trackIndex, int clipIndex, int effectIndex)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    if (effectIndex < 0 || effectIndex >= clip.effects.size())
        return;

    const drift::Project before = m_project;
    clip.effects.removeAt(effectIndex);
    dropKeyframeGraphPropertiesForEffect(effectIndex);
    pushProjectEdit(before, tr("Remove effect"));
    finishEdit(tr("Effect removed"));
}

void AppController::setEffectEnabled(int trackIndex, int clipIndex, int effectIndex, bool enabled)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    if (effectIndex < 0 || effectIndex >= clip.effects.size())
        return;
    if (clip.effects[effectIndex].enabled == enabled)
        return;

    const drift::Project before = m_project;
    clip.effects[effectIndex].enabled = enabled;
    pushProjectEdit(before, enabled ? tr("Enable effect")
                                    : tr("Disable effect"));
    finishEdit(enabled ? tr("Effect enabled") : tr("Effect disabled"));
}

void AppController::moveEffect(int trackIndex, int clipIndex, int fromIndex, int toIndex)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    if (fromIndex < 0 || fromIndex >= clip.effects.size())
        return;
    toIndex = qBound(0, toIndex, clip.effects.size() - 1);
    if (fromIndex == toIndex)
        return;

    const drift::Project before = m_project;
    clip.effects.move(fromIndex, toIndex);
    remapKeyframeGraphPropertiesForEffectMove(fromIndex, toIndex);
    pushProjectEdit(before, tr("Reorder effect"));
    finishEdit(tr("Effect reordered"));
}

void AppController::setEffectParam(int trackIndex, int clipIndex, int effectIndex, const QString &key,
                                   double value)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    if (effectIndex < 0 || effectIndex >= clip.effects.size())
        return;

    const drift::Project before = m_project;
    const EffectPresetEntry *def = effectDefForId(clip.effects[effectIndex].catalogId);
    bool asBoolean = false;
    if (def) {
        for (const drift::EffectParamSpec &param : def->meta.parameters) {
            if (param.key == key) {
                asBoolean = param.isBoolean();
                break;
            }
        }
    }
    if (asBoolean)
        clip.effects[effectIndex].parameters.insert(key, value > 0.5);
    else
        clip.effects[effectIndex].parameters.insert(key, value);
    pushProjectEdit(before, tr("Edit effect"));
    finishEdit(tr("Effect updated"));
}

// Colour params take this path rather than widening setEffectParam, which every existing QML call
// site passes a double to. There is no preview variant on purpose: a swatch commits once, so there
// is no drag stream to coalesce the way a slider needs.
void AppController::setEffectColorParam(int trackIndex, int clipIndex, int effectIndex,
                                        const QString &key, const QString &value)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    if (effectIndex < 0 || effectIndex >= clip.effects.size())
        return;

    const EffectPresetEntry *def = effectDefForId(clip.effects[effectIndex].catalogId);
    if (!def)
        return;
    const auto specIt = std::find_if(def->meta.parameters.cbegin(), def->meta.parameters.cend(),
                                     [&](const drift::EffectParamSpec &p) { return p.key == key; });
    if (specIt == def->meta.parameters.cend() || !specIt->isColor())
        return;

    // Normalized to the same six-digit form the catalog default carries, so what lands in the
    // project matches what the parser would have produced.
    const QColor color(value);
    if (!color.isValid())
        return;

    const drift::Project before = m_project;
    clip.effects[effectIndex].parameters.insert(key, color.name(QColor::HexRgb));
    pushProjectEdit(before, tr("Edit effect"));
    finishEdit(tr("Effect updated"));
}

QVariantList AppController::audioEffectCatalog() const
{
    QVariantList out;
    for (const AudioEffectEntry &def : ::audioEffectCatalog()) {
        QVariantList params;
        for (const drift::EffectParamSpec &p : def.parameters) {
            params.append(QVariantMap{
                {QStringLiteral("key"), p.key},
                {QStringLiteral("label"), p.label},
                {QStringLiteral("min"), p.min},
                {QStringLiteral("max"), p.max},
                {QStringLiteral("default"), p.defaultValue},
                {QStringLiteral("isBoolean"), p.isBoolean()},
                {QStringLiteral("type"), p.typeName()},
            });
        }
        out.append(QVariantMap{
            {QStringLiteral("id"), def.id},
            {QStringLiteral("label"), def.displayName},
            {QStringLiteral("displayName"), def.displayName},
            {QStringLiteral("category"), def.category},
            {QStringLiteral("categoryLabel"), audioEffectCategoryLabel(def.category)},
            {QStringLiteral("icon"), def.icon},
            {QStringLiteral("thumbnailPath"), def.thumbnailPath},
            {QStringLiteral("params"), params},
        });
    }
    return out;
}

QVariantList AppController::audioEffectCategories() const
{
    QVariantList out;
    for (const auto &entry : ::audioEffectCategories()) {
        out.append(QVariantMap{
            {QStringLiteral("id"), entry.first},
            {QStringLiteral("label"), entry.second},
        });
    }
    return out;
}

void AppController::addAudioEffect(int trackIndex, int clipIndex, const QString &effectId)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    const AudioEffectEntry *def = audioEffectDefForId(effectId);
    if (!def)
        return;

    drift::Effect effect;
    effect.name = def->displayName;
    effect.catalogId = def->id;
    for (const drift::EffectParamSpec &p : def->parameters)
        effect.parameters.insert(p.key, p.defaultValue);

    const drift::Project before = m_project;
    track.clips[clipIndex].audioEffects.append(effect);
    m_selectedTrack = trackIndex;
    m_selectedClip = clipIndex;
    m_selection = {qMakePair(trackIndex, clipIndex)};
    pushProjectEdit(before, tr("Add audio effect"));
    finishEdit(tr("Audio effect added"));
}

void AppController::removeAudioEffect(int trackIndex, int clipIndex, int effectIndex)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    if (effectIndex < 0 || effectIndex >= clip.audioEffects.size())
        return;

    const drift::Project before = m_project;
    clip.audioEffects.removeAt(effectIndex);
    pushProjectEdit(before, tr("Remove audio effect"));
    finishEdit(tr("Audio effect removed"));
}

void AppController::setAudioEffectEnabled(int trackIndex, int clipIndex, int effectIndex, bool enabled)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    if (effectIndex < 0 || effectIndex >= clip.audioEffects.size())
        return;
    if (clip.audioEffects[effectIndex].enabled == enabled)
        return;

    const drift::Project before = m_project;
    clip.audioEffects[effectIndex].enabled = enabled;
    pushProjectEdit(before, enabled ? tr("Enable audio effect")
                                    : tr("Disable audio effect"));
    finishEdit(enabled ? tr("Audio effect enabled")
                       : tr("Audio effect disabled"));
}

void AppController::moveAudioEffect(int trackIndex, int clipIndex, int fromIndex, int toIndex)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    if (fromIndex < 0 || fromIndex >= clip.audioEffects.size())
        return;
    toIndex = qBound(0, toIndex, clip.audioEffects.size() - 1);
    if (fromIndex == toIndex)
        return;

    const drift::Project before = m_project;
    clip.audioEffects.move(fromIndex, toIndex);
    pushProjectEdit(before, tr("Reorder audio effect"));
    finishEdit(tr("Audio effect reordered"));
}

void AppController::previewSetAudioEffectParam(int trackIndex, int clipIndex, int effectIndex,
                                               const QString &key, double value)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    if (effectIndex < 0 || effectIndex >= clip.audioEffects.size())
        return;
    if (key.isEmpty())
        return;

    if (!m_previewDragActive)
        beginPreviewDrag(tr("Edit audio effect"));

    clip.audioEffects[effectIndex].parameters.insert(key, value);
    // Audio effects are heard, not seen: a preview frame won't reflect the change, but keeping the
    // project mutated live means the next playback buffer picks it up without a commit.
    emitPreviewFrame();
}

void AppController::setAudioEffectParam(int trackIndex, int clipIndex, int effectIndex,
                                        const QString &key, double value)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    drift::Track &track = m_project.tracks()[trackIndex];
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;

    drift::Clip &clip = track.clips[clipIndex];
    if (effectIndex < 0 || effectIndex >= clip.audioEffects.size())
        return;

    const drift::Project before = m_project;
    clip.audioEffects[effectIndex].parameters.insert(key, value);
    pushProjectEdit(before, tr("Edit audio effect"));
    finishEdit(tr("Audio effect updated"));
}

void AppController::setTrackMuted(int trackIndex, bool muted)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    if (m_project.tracks()[trackIndex].muted == muted)
        return;

    const drift::Project before = m_project;
    m_project.tracks()[trackIndex].muted = muted;
    pushProjectEdit(before, tr("Track mute"));
    finishEdit(muted ? tr("Track muted") : tr("Track unmuted"));
}

void AppController::setTrackHidden(int trackIndex, bool hidden)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    if (m_project.tracks()[trackIndex].hidden == hidden)
        return;

    const drift::Project before = m_project;
    m_project.tracks()[trackIndex].hidden = hidden;
    pushProjectEdit(before, tr("Track visibility"));
    finishEdit(hidden ? tr("Track hidden") : tr("Track shown"));
}

bool AppController::trackMuted(int trackIndex) const
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return false;
    return m_project.tracks().at(trackIndex).muted;
}

bool AppController::trackHidden(int trackIndex) const
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return false;
    return m_project.tracks().at(trackIndex).hidden;
}

void AppController::setTrackShowWaveform(int trackIndex, bool show)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    if (m_project.tracks()[trackIndex].showWaveform == show)
        return;

    // View-only preference: mutate and refresh without an undo entry.
    m_project.tracks()[trackIndex].showWaveform = show;
    emit tracksChanged();
}

bool AppController::trackShowWaveform(int trackIndex) const
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return false;
    return m_project.tracks().at(trackIndex).showWaveform;
}

void AppController::setTrackHeightScale(int trackIndex, double scale)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    const qreal clamped = qBound(trackHeightScaleMin(), scale, trackHeightScaleMax());
    if (qFuzzyCompare(m_project.tracks()[trackIndex].heightScale, clamped))
        return;

    // View-only preference, like showWaveform: no undo entry.
    m_project.tracks()[trackIndex].heightScale = clamped;
    emit tracksChanged();
}

double AppController::trackHeightScale(int trackIndex) const
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return 1.0;
    return m_project.tracks().at(trackIndex).heightScale;
}

void AppController::nudgeTrackHeightScale(int trackIndex, int steps)
{
    if (steps == 0)
        return;
    setTrackHeightScale(trackIndex,
                        trackHeightScale(trackIndex) * std::pow(1.18, steps));
}

void AppController::moveTrack(int fromIndex, int toIndex)
{
    const int trackCount = m_project.tracks().size();
    if (fromIndex < 0 || fromIndex >= trackCount || toIndex < 0 || toIndex >= trackCount)
        return;
    if (fromIndex == toIndex)
        return;

    const drift::Project before = m_project;
    m_project.tracks().move(fromIndex, toIndex);

    auto remap = [fromIndex, toIndex](int index) -> int {
        if (index < 0)
            return index;
        if (index == fromIndex)
            return toIndex;
        if (fromIndex < toIndex) {
            if (index > fromIndex && index <= toIndex)
                return index - 1;
        } else if (index >= toIndex && index < fromIndex) {
            return index + 1;
        }
        return index;
    };

    m_selectedTrack = remap(m_selectedTrack);
    m_selectedTransitionTrack = remap(m_selectedTransitionTrack);
    for (QPair<int, int> &pair : m_selection)
        pair.first = remap(pair.first);

    pushProjectEdit(before, tr("Move track"));
    finishEdit(tr("Track moved"));
}

void AppController::removeTrack(int trackIndex)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    const drift::Project before = m_project;
    m_project.tracks().removeAt(trackIndex);

    // Indices at or after the removed track shift down by one; anything that
    // pointed at the removed track itself is now dangling and gets cleared.
    auto remap = [trackIndex](int index) -> int {
        if (index < 0)
            return index;
        if (index == trackIndex)
            return -1;
        if (index > trackIndex)
            return index - 1;
        return index;
    };

    m_selectedTransitionTrack = remap(m_selectedTransitionTrack);
    for (int i = m_selection.size() - 1; i >= 0; --i) {
        const int mapped = remap(m_selection.at(i).first);
        if (mapped < 0)
            m_selection.removeAt(i);
        else
            m_selection[i].first = mapped;
    }
    if (m_selection.isEmpty()) {
        m_selectedTrack = -1;
        m_selectedClip = -1;
    } else {
        m_selectedTrack = m_selection.constLast().first;
        m_selectedClip = m_selection.constLast().second;
    }

    pushProjectEdit(before, tr("Delete track"));
    finishEdit(tr("Track deleted"));
}

void AppController::addTrack(const QString &type)
{
    const QString normalized = type.trimmed().toLower();
    if (normalized != QLatin1String("video") && normalized != QLatin1String("audio")
        && normalized != QLatin1String("text") && normalized != QLatin1String("subtitle")
        && normalized != QLatin1String("shape")) {
        return;
    }

    const drift::Project before = m_project;
    m_project.tracks().prepend(drift::Track{.type = drift::trackTypeFromString(normalized)});
    pushProjectEdit(before, tr("Add track"));
    finishEdit(tr("Track added"));
}

QVariantList AppController::bookmarks() const
{
    QVariantList result;
    for (const drift::Bookmark &bookmark : m_project.bookmarks()) {
        result.append(QVariantMap{
            {QStringLiteral("seconds"), drift::usToSeconds(bookmark.timeUs)},
            {QStringLiteral("label"), bookmark.label},
        });
    }
    return result;
}

double AppController::workAreaInSeconds() const
{
    return m_project.workAreaInUs() >= 0 ? drift::usToSeconds(m_project.workAreaInUs()) : -1.0;
}

double AppController::workAreaOutSeconds() const
{
    return m_project.workAreaOutUs() >= 0 ? drift::usToSeconds(m_project.workAreaOutUs()) : -1.0;
}

void AppController::setLoopWorkAreaEnabled(bool enabled)
{
    if (m_loopWorkAreaEnabled == enabled)
        return;

    m_loopWorkAreaEnabled = enabled;
    m_playback.setLoopWorkArea(enabled);
    QSettings().setValue(QStringLiteral("playback/loopWorkArea"), enabled);
    emit loopWorkAreaEnabledChanged();
}

void AppController::markWorkAreaIn()
{
    const drift::Project before = m_project;
    const drift::TimeUs at = qBound<drift::TimeUs>(0, m_playheadUs, qMax(m_project.durationUs(), drift::TimeUs{0}));
    m_project.setWorkAreaInUs(at);
    if (m_project.workAreaOutUs() >= 0 && m_project.workAreaOutUs() <= at)
        m_project.setWorkAreaOutUs(-1);
    pushProjectEdit(before, tr("Mark work area in"));
    finishEdit(tr("Work area in marked"));
    emit workAreaChanged();
}

void AppController::markWorkAreaOut()
{
    const drift::Project before = m_project;
    const drift::TimeUs at = qBound<drift::TimeUs>(0, m_playheadUs, qMax(m_project.durationUs(), drift::TimeUs{0}));
    m_project.setWorkAreaOutUs(at);
    if (m_project.workAreaInUs() < 0 || m_project.workAreaInUs() >= at)
        m_project.setWorkAreaInUs(0);
    if (!m_project.hasWorkArea())
        m_project.clearWorkArea();
    pushProjectEdit(before, tr("Mark work area out"));
    finishEdit(tr("Work area out marked"));
    emit workAreaChanged();
}

void AppController::goToWorkAreaIn()
{
    if (!m_project.hasWorkArea())
        return;
    setPlayheadUs(m_project.workAreaInUs());
}

void AppController::goToWorkAreaOut()
{
    if (!m_project.hasWorkArea())
        return;
    setPlayheadUs(m_project.workAreaOutUs());
}

void AppController::clearWorkArea()
{
    if (!m_project.hasWorkArea() && m_project.workAreaInUs() < 0 && m_project.workAreaOutUs() < 0)
        return;

    const drift::Project before = m_project;
    m_project.clearWorkArea();
    pushProjectEdit(before, tr("Clear work area"));
    finishEdit(tr("Work area cleared"));
    emit workAreaChanged();
}

void AppController::toggleLoopWorkArea()
{
    setLoopWorkAreaEnabled(!m_loopWorkAreaEnabled);
}

void AppController::addBookmark(double seconds, const QString &label)
{
    const drift::Project before = m_project;
    m_project.bookmarks().append({
        .timeUs = qMax<drift::TimeUs>(0, drift::secondsToUs(seconds)),
        .label = label.isEmpty() ? QStringLiteral("Bookmark") : label,
    });
    pushProjectEdit(before, tr("Add bookmark"));
    finishEdit(tr("Bookmark added"));
}

void AppController::removeBookmark(int index)
{
    if (index < 0 || index >= m_project.bookmarks().size())
        return;

    const drift::Project before = m_project;
    m_project.bookmarks().removeAt(index);
    pushProjectEdit(before, tr("Remove bookmark"));
    finishEdit(tr("Bookmark removed"));
}

void AppController::updateBookmark(int index, double seconds, const QString &label)
{
    if (index < 0 || index >= m_project.bookmarks().size())
        return;

    const drift::TimeUs timeUs = qMax<drift::TimeUs>(0, drift::secondsToUs(seconds));
    const QString resolvedLabel = label.isEmpty() ? QStringLiteral("Bookmark") : label;
    drift::Bookmark &bookmark = m_project.bookmarks()[index];
    if (bookmark.timeUs == timeUs && bookmark.label == resolvedLabel)
        return;

    const drift::Project before = m_project;
    bookmark.timeUs = timeUs;
    bookmark.label = resolvedLabel;
    pushProjectEdit(before, tr("Edit bookmark"));
    finishEdit(tr("Bookmark updated"));
}

void AppController::goToBookmark(int index)
{
    if (index < 0 || index >= m_project.bookmarks().size())
        return;
    setPlayheadUs(m_project.bookmarks().at(index).timeUs);
}

namespace {

int nearestBookmarkIndex(const QList<drift::Bookmark> &bookmarks, drift::TimeUs timeUs,
                         drift::TimeUs maxDistanceUs)
{
    int bestIndex = -1;
    drift::TimeUs bestDistance = maxDistanceUs;
    for (int i = 0; i < bookmarks.size(); ++i) {
        const drift::TimeUs distance = qAbs(bookmarks.at(i).timeUs - timeUs);
        if (distance <= bestDistance) {
            bestDistance = distance;
            bestIndex = i;
        }
    }
    return bestIndex;
}

int nextBookmarkIndex(const QList<drift::Bookmark> &bookmarks, drift::TimeUs playheadUs)
{
    int bestIndex = -1;
    drift::TimeUs bestTime = 0;
    int earliestIndex = -1;
    drift::TimeUs earliestTime = 0;
    for (int i = 0; i < bookmarks.size(); ++i) {
        const drift::TimeUs timeUs = bookmarks.at(i).timeUs;
        if (earliestIndex < 0 || timeUs < earliestTime) {
            earliestIndex = i;
            earliestTime = timeUs;
        }
        if (timeUs <= playheadUs)
            continue;
        if (bestIndex < 0 || timeUs < bestTime) {
            bestIndex = i;
            bestTime = timeUs;
        }
    }
    return bestIndex >= 0 ? bestIndex : earliestIndex;
}

int previousBookmarkIndex(const QList<drift::Bookmark> &bookmarks, drift::TimeUs playheadUs)
{
    int bestIndex = -1;
    drift::TimeUs bestTime = 0;
    int latestIndex = -1;
    drift::TimeUs latestTime = 0;
    for (int i = 0; i < bookmarks.size(); ++i) {
        const drift::TimeUs timeUs = bookmarks.at(i).timeUs;
        if (latestIndex < 0 || timeUs > latestTime) {
            latestIndex = i;
            latestTime = timeUs;
        }
        if (timeUs >= playheadUs)
            continue;
        if (bestIndex < 0 || timeUs > bestTime) {
            bestIndex = i;
            bestTime = timeUs;
        }
    }
    return bestIndex >= 0 ? bestIndex : latestIndex;
}

} // namespace

void AppController::goToNextBookmark()
{
    const int index = nextBookmarkIndex(m_project.bookmarks(), m_playheadUs);
    if (index >= 0)
        goToBookmark(index);
}

void AppController::goToPreviousBookmark()
{
    const int index = previousBookmarkIndex(m_project.bookmarks(), m_playheadUs);
    if (index >= 0)
        goToBookmark(index);
}

void AppController::toggleBookmarkAtPlayhead()
{
    const int near = nearestBookmarkIndex(m_project.bookmarks(), m_playheadUs,
                                          drift::kSnapThresholdUs);
    if (near >= 0) {
        removeBookmark(near);
        return;
    }

    const int markNumber = m_project.bookmarks().size() + 1;
    addBookmark(playheadSeconds(), QStringLiteral("Mark %1").arg(markNumber));
}

void AppController::removeBookmarkNearPlayhead()
{
    const int near = nearestBookmarkIndex(m_project.bookmarks(), m_playheadUs,
                                          drift::kSnapThresholdUs);
    if (near >= 0)
        removeBookmark(near);
}

namespace {

// Freeze frames are project media, not cache: they live under the project's own media dir so a
// save bundles them and a cache sweep can't delete the file a saved clip points at.
QString newFreezeFramePath(const QString &projectId)
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        return {};

    const QString dir = QDir(base).filePath(QStringLiteral("projects/%1/media").arg(projectId));
    if (!QDir().mkpath(dir))
        return {};

    return QDir(dir).filePath(QStringLiteral("freeze-%1.png")
                                  .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
}

} // namespace

void AppController::freezeFrameAtPlayhead()
{
    if (!m_assetLibrary)
        return;

    if (activeVideoClipAtPlayhead().isEmpty()) {
        setLastMessage(tr("No video at the current time"), QStringLiteral("warning"));
        return;
    }

    const QString outPath = newFreezeFramePath(m_project.id());
    if (outPath.isEmpty()) {
        setLastMessage(tr("Couldn’t capture a still frame"), QStringLiteral("error"));
        return;
    }

    // Compositing here drives the shared decoder pool, which playback is also driving with its
    // own set of retained paths and its own read-ahead. Same reason export stops playback first.
    setPlaying(false);

    setLastMessage(tr("Capturing freeze frame…"));
    const drift::TimeUs playheadUs = m_playheadUs;
    // The worker composites off the GUI thread while editing continues, so it gets a detached
    // copy rather than a pointer into the live project.
    const auto snapshot = std::make_shared<const drift::Project>(m_project.detachedCopy());

    (void)QtConcurrent::run([this, snapshot, playheadUs, outPath]() {
        FrameCompositor compositor;
        compositor.setProject(snapshot.get());

        // Full canvas scale: this is the finished frame — every track, effect, transition and
        // overlay — not a preview of one clip's source.
        const QImage frame = compositor.compositeAt(playheadUs);
        const bool ok = !frame.isNull() && frame.save(outPath, "PNG");
        const QString thumb =
            ok ? MediaThumbnail::generate(outPath, drift::mediaKindToString(drift::MediaKind::Image))
               : QString();
        const QSize size = frame.size();

        QMetaObject::invokeMethod(
            this,
            [this, ok, outPath, thumb, size, playheadUs]() {
                if (!ok) {
                    setLastMessage(tr("Couldn’t capture a still frame"),
                                   QStringLiteral("error"));
                    return;
                }

                const drift::Project before = m_project;

                drift::MediaAsset asset;
                asset.name = QStringLiteral("Freeze frame");
                asset.path = outPath;
                asset.kind = drift::MediaKind::Image;
                asset.width = size.width();
                asset.height = size.height();
                asset.thumbnailPath = thumb;
                asset.filmstripPath = thumb;
                asset.hasAudio = false;
                asset.hasAudioKnown = true;
                const QString assetId = m_assetLibrary->addGeneratedAsset(asset);

                // Inserted at the top: track 0 composites in front, so a freeze frame on a freshly
                // appended track would sit behind the video it was captured from and show nothing.
                const int trackIndex = drift::ensureTrackForClipType(m_project, drift::ClipType::Image, true);

                drift::Track &track = m_project.tracks()[trackIndex];
                const drift::TimeUs start = drift::resolveClipStart(m_project, track, -1, playheadUs,
                                                                    drift::kImageClipDurationUs, m_snapEnabled,
                                                                    playheadUs);

                drift::Clip freezeClip;
                freezeClip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                freezeClip.assetId = assetId;
                freezeClip.type = drift::ClipType::Image;
                freezeClip.name = QStringLiteral("Freeze frame");
                freezeClip.path = outPath;
                freezeClip.thumbnailPath = thumb;
                freezeClip.filmstripPath = thumb;
                freezeClip.timelineStart = start;
                freezeClip.timelineDuration = drift::kImageClipDurationUs;
                freezeClip.srcIn = 0;
                freezeClip.srcOut = drift::kImageClipDurationUs;
                fitClipLayoutToCanvas(freezeClip, size.width(), size.height(), m_project.width(),
                                      m_project.height());

                track.clips.append(freezeClip);
                pushProjectEdit(before, tr("Freeze frame added"));
                finishEdit(tr("Freeze frame added"));
                selectClip(trackIndex, track.clips.size() - 1);
            },
            Qt::QueuedConnection);
    });
}

void AppController::copySelection()
{
    m_clipboard.clear();
    QList<QPair<int, int>> pairs = m_selection;
    if (pairs.isEmpty() && m_selectedTrack >= 0 && m_selectedClip >= 0)
        pairs.append(qMakePair(m_selectedTrack, m_selectedClip));
    for (const QPair<int, int> &pair : pairs) {
        if (!isValidClipIndex(pair.first, pair.second))
            continue;
        ClipboardItem item;
        item.clip = m_project.tracks().at(pair.first).clips.at(pair.second);
        item.trackType = m_project.tracks().at(pair.first).type;
        m_clipboard.append(item);
    }
}

void AppController::cutSelection()
{
    copySelection();
    deleteSelectedClip();
}

void AppController::pasteAtPlayhead()
{
    if (m_clipboard.isEmpty())
        return;
    const drift::Project before = m_project;
    drift::TimeUs anchor = LLONG_MAX;
    for (const ClipboardItem &item : m_clipboard)
        anchor = qMin(anchor, item.clip.timelineStart);
    const drift::TimeUs shift = m_playheadUs - anchor;
    QList<QPair<int, int>> inserted;

    for (const ClipboardItem &item : m_clipboard) {
        drift::Clip clip = item.clip;
        clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        clip.timelineStart = qMax<drift::TimeUs>(0, clip.timelineStart + shift);

        int targetTrack = -1;
        for (int i = 0; i < m_project.tracks().size(); ++i) {
            if (m_project.tracks().at(i).type == item.trackType && m_project.tracks().at(i).allowsClipType(clip.type)) {
                targetTrack = i;
                break;
            }
        }
        if (targetTrack < 0)
            targetTrack = drift::ensureTrackForClipType(m_project, clip.type, true);
        if (targetTrack < 0 || !m_project.tracks()[targetTrack].allowsClipType(clip.type))
            continue;
        drift::Track &track = m_project.tracks()[targetTrack];
        track.clips.append(clip);
        inserted.append(qMakePair(targetTrack, track.clips.size() - 1));
    }

    if (inserted.isEmpty())
        return;
    pushProjectEdit(before, tr("Paste"));
    m_selection = inserted;
    m_selectedTrack = inserted.constLast().first;
    m_selectedClip = inserted.constLast().second;
    finishEdit(tr("Pasted %1 clip(s)").arg(inserted.size()));
}

void AppController::nudgeSelection(double deltaSeconds)
{
    if (qFuzzyIsNull(deltaSeconds))
        return;
    QList<QPair<int, int>> pairs = m_selection;
    if (pairs.isEmpty() && m_selectedTrack >= 0 && m_selectedClip >= 0)
        pairs.append(qMakePair(m_selectedTrack, m_selectedClip));
    if (pairs.isEmpty())
        return;
    const drift::Project before = m_project;
    const drift::TimeUs deltaUs = drift::secondsToUs(deltaSeconds);
    QSet<QString> movedIds;
    for (const QPair<int, int> &pair : pairs) {
        if (!isValidClipIndex(pair.first, pair.second))
            continue;
        drift::Clip &clip = m_project.tracks()[pair.first].clips[pair.second];
        clip.timelineStart = qMax<drift::TimeUs>(0, clip.timelineStart + deltaUs);
        movedIds.insert(clip.id);
    }
    if (!m_allowClipOverlap) {
        for (const QPair<int, int> &pair : pairs) {
            if (!isValidClipIndex(pair.first, pair.second))
                continue;
            drift::Track &track = m_project.tracks()[pair.first];
            drift::Clip &clip = track.clips[pair.second];
            clip.timelineStart = drift::clampClipStartNoOverlap(track, movedIds, clip.timelineStart,
                                                                clip.timelineDuration);
        }
    }
    pushProjectEdit(before, tr("Nudge selection"));
    finishEdit(tr("Selection nudged"));
}

bool AppController::selectionContains(int trackIndex, int clipIndex) const
{
    return m_selection.contains(qMakePair(trackIndex, clipIndex));
}

void AppController::setTimelineTrimCursor(int side, int heightPx)
{
    side = qBound(-1, side, 1);
    heightPx = qMax(0, heightPx);
    if (side == m_timelineTrimCursorSide && (side == 0 || heightPx == m_timelineTrimCursorHeight))
        return;

    if (m_timelineTrimCursorSide != 0)
        QGuiApplication::restoreOverrideCursor();

    m_timelineTrimCursorSide = side;
    m_timelineTrimCursorHeight = heightPx;
    if (side != 0)
        QGuiApplication::setOverrideCursor(timelineTrimCursor(side, heightPx > 0 ? heightPx : 28));
}

QString AppController::shortcutFor(const QString &actionId) const
{
    return m_shortcuts.value(actionId);
}

QString AppController::setShortcut(const QString &actionId, const QString &keys)
{
    if (!m_shortcuts.contains(actionId))
        return {};

    // Refuse a chord that is already spoken for. Clearing (empty keys) is always
    // allowed, and rebinding an action to what it already has is a no-op, not a
    // conflict with itself.
    if (!keys.isEmpty()) {
        for (auto it = m_shortcuts.cbegin(); it != m_shortcuts.cend(); ++it) {
            if (it.key() == actionId || it.value() != keys)
                continue;
            const QString holder = it.key();
            for (const QVariant &entry : actions()) {
                const QVariantMap map = entry.toMap();
                if (map.value(QStringLiteral("id")).toString() == holder)
                    return map.value(QStringLiteral("label")).toString();
            }
            return holder;
        }
    }

    m_shortcuts[actionId] = keys;
    QSettings settings;
    settings.beginGroup(QStringLiteral("shortcuts"));
    settings.setValue(actionId, keys);
    settings.endGroup();
    emit shortcutsChanged();
    return {};
}

void AppController::resetShortcuts()
{
    m_shortcuts = defaultShortcuts();
    QSettings settings;
    settings.beginGroup(QStringLiteral("shortcuts"));
    for (auto it = m_shortcuts.cbegin(); it != m_shortcuts.cend(); ++it)
        settings.setValue(it.key(), it.value());
    settings.endGroup();
    emit shortcutsChanged();
}

void AppController::loadAssetFavorites()
{
    m_assetFavorites.clear();
    QSettings settings;
    settings.beginGroup(QStringLiteral("assetFavorites"));
    const QStringList keys = settings.allKeys();
    for (const QString &tabId : keys) {
        const QStringList ids = settings.value(tabId).toStringList();
        if (!ids.isEmpty())
            m_assetFavorites.insert(tabId, QSet<QString>(ids.cbegin(), ids.cend()));
    }
    settings.endGroup();
}

void AppController::saveAssetFavorites(const QString &tabId)
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("assetFavorites"));
    const QSet<QString> ids = m_assetFavorites.value(tabId);
    if (ids.isEmpty())
        settings.remove(tabId);
    else
        settings.setValue(tabId, QStringList(ids.cbegin(), ids.cend()));
    settings.endGroup();
}

bool AppController::isAssetFavorite(const QString &tabId, const QString &itemId) const
{
    return m_assetFavorites.value(tabId).contains(itemId);
}

void AppController::toggleAssetFavorite(const QString &tabId, const QString &itemId)
{
    if (tabId.isEmpty() || itemId.isEmpty())
        return;
    QSet<QString> &ids = m_assetFavorites[tabId];
    if (ids.contains(itemId))
        ids.remove(itemId);
    else
        ids.insert(itemId);
    if (ids.isEmpty())
        m_assetFavorites.remove(tabId);
    saveAssetFavorites(tabId);
    emit assetFavoritesChanged();
}

void AppController::triggerAction(const QString &actionId)
{
    // File ops need QML (dialogs + unsaved-change confirm). Emit so the header
    // can gate them the same way the project menu does.
    if (actionId == QStringLiteral("newProject"))
        emit newProjectRequested();
    else if (actionId == QStringLiteral("open"))
        emit openRequested();
    else if (actionId == QStringLiteral("save"))
        emit saveRequested();
    else if (actionId == QStringLiteral("playPause"))
        togglePlayback();
    else if (actionId == QStringLiteral("delete"))
        deleteSelectedClip();
    else if (actionId == QStringLiteral("undo"))
        undo();
    else if (actionId == QStringLiteral("redo"))
        redo();
    else if (actionId == QStringLiteral("clearSelection"))
        clearSelection();
    else if (actionId == QStringLiteral("selectAll"))
        selectAllClips();
    else if (actionId == QStringLiteral("duplicate"))
        duplicateSelectedClip();
    else if (actionId == QStringLiteral("split"))
        splitAtPlayhead();
    else if (actionId == QStringLiteral("merge"))
        mergeSelectedClips();
    else if (actionId == QStringLiteral("separateAudio"))
        separateAudioFromSelection();
    else if (actionId == QStringLiteral("unlink"))
        unlinkSelectedClips();
    else if (actionId == QStringLiteral("copy"))
        copySelection();
    else if (actionId == QStringLiteral("cut"))
        cutSelection();
    else if (actionId == QStringLiteral("paste"))
        pasteAtPlayhead();
    else if (actionId == QStringLiteral("nudgeLeft"))
        nudgeSelection(-0.1);
    else if (actionId == QStringLiteral("nudgeRight"))
        nudgeSelection(0.1);
    else if (actionId == QStringLiteral("toggleGuides"))
        setGuidesEnabled(!guidesEnabled());
    else if (actionId == QStringLiteral("toggleBookmark"))
        toggleBookmarkAtPlayhead();
    else if (actionId == QStringLiteral("nextBookmark"))
        goToNextBookmark();
    else if (actionId == QStringLiteral("previousBookmark"))
        goToPreviousBookmark();
    else if (actionId == QStringLiteral("markIn"))
        markWorkAreaIn();
    else if (actionId == QStringLiteral("markOut"))
        markWorkAreaOut();
    else if (actionId == QStringLiteral("goToIn"))
        goToWorkAreaIn();
    else if (actionId == QStringLiteral("goToOut"))
        goToWorkAreaOut();
    else if (actionId == QStringLiteral("clearInOut"))
        clearWorkArea();
    else if (actionId == QStringLiteral("toggleLoop"))
        toggleLoopWorkArea();
}

void AppController::undo()
{
    if (!m_undoStack.canUndo())
        return;
    m_undoStack.undo();
    ++m_mcpEditRevision;
}

void AppController::redo()
{
    if (!m_undoStack.canRedo())
        return;
    m_undoStack.redo();
    ++m_mcpEditRevision;
}

namespace {

// Raw max-abs amplitude wastes almost all of the lane: ordinary dialogue peaks around
// -18 dBFS, i.e. 0.12 linear, which is a 4px sliver in a 40px lane. Plot dB over this window
// instead — the scale audio is actually read on — so quiet material is legible without
// clipping loud material. Full scale still maps to 1.0.
constexpr double kWaveformFloorDb = 48.0;

double waveformDisplayLevel(float peak)
{
    if (peak <= 0.0f)
        return 0.0;
    const double db = 20.0 * std::log10(static_cast<double>(peak));
    return qBound(0.0, 1.0 + db / kWaveformFloorDb, 1.0);
}

// Max-reduce dense peaks over [first, last) into `buckets` display values. The 0.05 floor
// keeps silent stretches drawn as a hairline rather than vanishing; a negative input marks a
// stretch that hasn't been decoded yet and comes back as 0 so nothing is drawn for it.
QVariantList reduceDensePeaks(const QVector<float> &dense, int first, int last, int buckets)
{
    QVariantList result;
    if (dense.isEmpty() || buckets <= 0)
        return result;

    first = qBound(0, first, dense.size());
    last = qBound(first, last, dense.size());
    if (last <= first)
        return result;

    const int span = last - first;
    result.reserve(buckets);
    for (int b = 0; b < buckets; ++b) {
        int i0 = first + static_cast<int>((static_cast<int64_t>(b) * span) / buckets);
        int i1 = first + static_cast<int>((static_cast<int64_t>(b + 1) * span) / buckets);
        if (i1 <= i0)
            i1 = qMin(last, i0 + 1);
        float peak = -1.0f;
        for (int i = i0; i < i1; ++i)
            peak = qMax(peak, dense[i]);
        result.append(peak < 0.0f ? 0.0 : qMax(0.05, waveformDisplayLevel(peak)));
    }
    return result;
}

} // namespace

const MediaWaveform::Dense *AppController::densePeaksFor(const QString &path) const
{
    if (path.isEmpty())
        return nullptr;

    const auto cached = m_waveformCache.constFind(path);
    if (cached != m_waveformCache.constEnd())
        return &cached.value();

    if (!m_waveformPending.contains(path)) {
        m_waveformPending.insert(path);
        AppController *self = const_cast<AppController *>(this);
        (void)QtConcurrent::run([self, path] {
            const MediaWaveform::Dense dense = MediaWaveform::densePeaks(path);
            QMetaObject::invokeMethod(
                self,
                [self, path, dense] {
                    self->m_waveformCache.insert(path, dense);
                    self->m_waveformPending.remove(path);
                    emit self->waveformReady(path);
                },
                Qt::QueuedConnection);
        });
    }

    return nullptr;
}

QVariantList AppController::waveformPeaks(const QString &path) const
{
    const MediaWaveform::Dense *dense = densePeaksFor(path);
    if (!dense)
        return {};

    // Whole file at dialog resolution (Denoise / Speed Curve canvases).
    const int buckets = qMin(2000, dense->peaks.size());
    return reduceDensePeaks(dense->peaks, 0, dense->peaks.size(), buckets);
}

QVariantList AppController::waveformPeaksForSourceRange(const QString &path, double startSeconds,
                                                        double durSeconds) const
{
    const MediaWaveform::Dense *dense = densePeaksFor(path);
    if (!dense || dense->durationSeconds <= 0.0 || durSeconds <= 0.0)
        return {};

    const double peaksPerSecond = dense->peaks.size() / dense->durationSeconds;
    const int first = static_cast<int>(startSeconds * peaksPerSecond);
    const int last = static_cast<int>((startSeconds + durSeconds) * peaksPerSecond);
    // reduceDensePeaks clamps the range, so a window running past the decoded end is safe.
    return reduceDensePeaks(dense->peaks, first, last, qMin(2000, qMax(1, last - first)));
}

QVariantList AppController::waveformPeaksRange(const QString &path, double startSeconds,
                                               double durSeconds, int buckets) const
{
    if (path.isEmpty() || durSeconds <= 0.0 || buckets <= 0)
        return {};

    // Block-backed: only the source span the clip's visible pixels cover is ever decoded, so
    // a three-hour file costs a viewport's worth of audio instead of the whole timeline.
    const int outCount = qBound(1, buckets, 4096);
    const QVector<float> span = m_waveformBlocks.range(path, startSeconds, durSeconds, outCount);
    if (span.isEmpty())
        return {};

    return reduceDensePeaks(span, 0, span.size(), span.size());
}

QVariantList AppController::subtitleWaveformPeaks(double startSeconds, double durSeconds,
                                                  int sampleCount) const
{
    if (durSeconds <= 0.0 || sampleCount <= 0)
        return {};

    // Cap so extreme zoom doesn't spawn multi-megabyte peak lists / mix jobs.
    const int buckets = qBound(1, sampleCount, 8192);

    const drift::TimeUs startUs = drift::secondsToUs(startSeconds);
    const drift::TimeUs durUs = drift::secondsToUs(durSeconds);
    const QString key = QStringLiteral("%1:%2:%3").arg(startUs).arg(durUs).arg(buckets);

    const auto cached = m_subtitleWaveformCache.constFind(key);
    if (cached != m_subtitleWaveformCache.constEnd())
        return cached.value();

    if (!m_subtitleWaveformPending.contains(key)) {
        m_subtitleWaveformPending.insert(key);
        AppController *self = const_cast<AppController *>(this);
        // Snapshot the project so the off-thread mixer never races the live one.
        const drift::Project snap = m_project;
        (void)QtConcurrent::run([self, snap, startUs, durUs, buckets, key, startSeconds, durSeconds] {
            const int rate = 8000; // enough for voice; keeps the render cheap
            const qint64 frames =
                static_cast<qint64>((static_cast<double>(durUs) / 1'000'000.0) * rate);
            QVariantList peaks;
            if (frames > 0) {
                AudioMixer mixer;
                mixer.setProject(&snap);
                // Never ask for more buckets than PCM frames — extras would be empty.
                const int peakBuckets = static_cast<int>(qMin<qint64>(buckets, frames));
                // Mixed a window at a time. The lane spans every A/V clip on the timeline, so
                // mixing it in one go meant holding the whole thing as PCM — ~400 MB for a
                // feature-length movie, to draw at most 8192 columns. The windows are
                // contiguous and in order, which is the pattern playback already uses, so the
                // readers decode forward instead of re-seeking.
                peaks = MediaWaveform::voicePeaks(
                    frames, rate, peakBuckets,
                    [&mixer, startUs, rate](float *out, qint64 frameOffset, int maxFrames) {
                        const drift::TimeUs at =
                            startUs + frameOffset * drift::kUsPerSecond / rate;
                        mixer.mix(at, maxFrames, rate, out);
                        return maxFrames;
                    });
            }
            QMetaObject::invokeMethod(
                self,
                [self, key, peaks, startSeconds, durSeconds, buckets] {
                    self->m_subtitleWaveformCache.insert(key, peaks);
                    self->m_subtitleWaveformPending.remove(key);
                    emit self->subtitleWaveformReady(startSeconds, durSeconds, buckets);
                },
                Qt::QueuedConnection);
        });
    }

    return {};
}

// Everything the AudioMixer reads, and nothing else — so a transform keyframe edit does not
// invalidate a beat grid while moving, trimming, retiming or muting the music does.
QByteArray AppController::audioLayoutFingerprint() const
{
    QCryptographicHash hash(QCryptographicHash::Sha1);
    for (const drift::Track &track : m_project.tracks()) {
        if (track.type != drift::TrackType::Audio && track.type != drift::TrackType::Video)
            continue;
        hash.addData(track.muted ? "m" : "-");
        for (const drift::Clip &clip : track.clips) {
            const QString row = QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|%9")
                                    .arg(clip.assetId)
                                    .arg(clip.timelineStart)
                                    .arg(clip.timelineDuration)
                                    .arg(clip.srcIn)
                                    .arg(clip.srcOut)
                                    .arg(clip.speed)
                                    .arg(clip.reverse ? 1 : 0)
                                    .arg(clip.suppressEmbeddedAudio ? 1 : 0)
                                    .arg(clip.audioEffects.size());
            hash.addData(row.toUtf8());
            // Tangents shape the volume ramp, so they belong in the digest alongside the values.
            for (const auto &kv : clip.volume.keyframes().asKeyValueRange()) {
                hash.addData(QStringLiteral("v%1:%2:%3:%4:%5:%6:%7")
                                 .arg(kv.first)
                                 .arg(kv.second.value)
                                 .arg(kv.second.inDx)
                                 .arg(kv.second.inDy)
                                 .arg(kv.second.outDx)
                                 .arg(kv.second.outDy)
                                 .arg(kv.second.hold ? 1 : 0)
                                 .toUtf8());
            }
            if (clip.hasSpeedCurve())
                hash.addData("c");
        }
    }
    return hash.result();
}

void AppController::analyzeBeats(double startSeconds, double durSeconds)
{
    if (durSeconds <= 0.0 || m_beatAnalysisRunning)
        return;

    // Already showing this exact range — nothing to redo.
    if (!m_beatAnalysis.isEmpty()
        && qFuzzyCompare(m_beatAnalysis.value(QStringLiteral("rangeStart")).toDouble() + 1.0,
                         startSeconds + 1.0)
        && qFuzzyCompare(m_beatAnalysis.value(QStringLiteral("rangeDuration")).toDouble() + 1.0,
                         durSeconds + 1.0)) {
        return;
    }

    const drift::TimeUs startUs = drift::secondsToUs(startSeconds);
    const drift::TimeUs durUs = drift::secondsToUs(durSeconds);
    const quint64 generation = ++m_beatAnalysisGeneration;

    m_beatAnalysisRunning = true;
    emit beatAnalysisChanged();

    // Snapshot the project so the off-thread mixer never races the live one — same
    // contract as subtitleWaveformPeaks. The fingerprint is taken from that same snapshot,
    // so an edit landing mid-analysis is caught by the staleness check rather than being
    // baked in as the state the result supposedly describes.
    const drift::Project snap = m_project;
    const QByteArray fingerprint = audioLayoutFingerprint();
    (void)QtConcurrent::run([this, snap, startUs, durUs, startSeconds, durSeconds, generation,
                             fingerprint] {
        // 22050 rather than the 8000 used for voice peaks: hats and cymbals, the sharpest
        // onset cues in music, live above 4 kHz.
        const int rate = 22050;
        const int frames = static_cast<int>((static_cast<double>(durUs) / 1'000'000.0) * rate);
        AudioBeatAnalysis analysis;
        if (frames > 0) {
            QVector<float> stereo(static_cast<qsizetype>(frames) * 2, 0.0f);
            AudioMixer mixer;
            mixer.setProject(&snap);
            mixer.mix(startUs, frames, rate, stereo.data());

            QVector<float> mono(frames, 0.0f);
            for (int i = 0; i < frames; ++i)
                mono[i] = 0.5f * (stereo[i * 2] + stereo[i * 2 + 1]);

            analysis = AudioOnsets::analyze(mono.constData(), frames, rate, startSeconds);
        }
        QMetaObject::invokeMethod(
            this,
            [this, analysis, startSeconds, durSeconds, generation, fingerprint] {
                if (generation != m_beatAnalysisGeneration)
                    return; // the user moved on; this result is for a range nobody is looking at
                applyBeatAnalysis(analysis, startSeconds, durSeconds, fingerprint);
            },
            Qt::QueuedConnection);
    });
}

void AppController::applyBeatAnalysis(const AudioBeatAnalysis &analysis, double startSeconds,
                                      double durSeconds, const QByteArray &fingerprint)
{
    QVariantList beats;
    for (double b : analysis.beats)
        beats.append(b);

    QVariantList onsets;
    for (const AudioOnset &o : analysis.onsets) {
        onsets.append(QVariantMap{{QStringLiteral("seconds"), o.seconds},
                                  {QStringLiteral("strength"), o.strength}});
    }

    m_beatAnalysis = QVariantMap{
        {QStringLiteral("rangeStart"), startSeconds},
        {QStringLiteral("rangeDuration"), durSeconds},
        {QStringLiteral("bpm"), analysis.bpm},
        {QStringLiteral("confidence"), analysis.confidence},
        {QStringLiteral("beatsPerBar"), analysis.beatsPerBar},
        {QStringLiteral("firstDownbeat"), analysis.firstDownbeat},
        {QStringLiteral("beats"), beats},
        {QStringLiteral("onsets"), onsets},
    };

    m_beatAnalysisRaw = analysis;
    rebuildBeatSnapTargets();

    m_beatAudioFingerprint = fingerprint;
    m_beatAnalysisRunning = false;
    emit beatAnalysisChanged();

    if (m_pendingEffectTemplate && m_pendingEffectTemplate->valid()) {
        const PendingEffectTemplate pending = *m_pendingEffectTemplate;
        const EffectTemplateEntry *entry = effectTemplateForId(pending.templateId);
        if (entry && pending.trackIndex >= 0 && pending.trackIndex < m_project.tracks().size()) {
            const drift::Track &track = m_project.tracks()[pending.trackIndex];
            if (pending.clipIndex >= 0 && pending.clipIndex < track.clips.size()
                && beatAnalysisReadyForClip(track.clips[pending.clipIndex], entry->sync)) {
                const drift::Clip &clip = track.clips[pending.clipIndex];
                const bool needsSegment =
                    (entry->requiresSegmentation || entry->usesMultiTrack()) && !clipHasMatte(clip);
                if (needsSegment) {
                    if (segmentationAvailable() && !m_segmenting)
                        openSegmentationForTemplate(pending.trackIndex, pending.clipIndex);
                } else {
                    m_pendingEffectTemplate.reset();
                    applyEffectTemplateInternal(pending.trackIndex, pending.clipIndex, *entry);
                }
            }
        }
    }
}

// Beats first — they are the musically meaningful grid — then onsets that do not already
// sit within a snap threshold of an accepted target. Without the thinning, a dense onset
// list would leave no un-snapped position on the timeline.
void AppController::rebuildBeatSnapTargets()
{
    m_beatSnapTargets.clear();

    if (m_beatGridVisible) {
        for (double b : std::as_const(m_beatAnalysisRaw.beats))
            m_beatSnapTargets.append(drift::secondsToUs(b));
    }
    if (!m_onsetsVisible)
        return;

    for (const AudioOnset &o : std::as_const(m_beatAnalysisRaw.onsets)) {
        const drift::TimeUs at = drift::secondsToUs(o.seconds);
        bool crowded = false;
        for (drift::TimeUs existing : std::as_const(m_beatSnapTargets)) {
            if (qAbs(existing - at) < drift::kSnapThresholdUs) {
                crowded = true;
                break;
            }
        }
        if (!crowded)
            m_beatSnapTargets.append(at);
    }
}

QList<drift::TimeUs> AppController::extraSnapTargets() const
{
    QList<drift::TimeUs> targets = m_beatSnapTargets;
    for (const drift::Bookmark &bookmark : m_project.bookmarks())
        targets.append(bookmark.timeUs);
    if (m_project.hasWorkArea()) {
        targets.append(m_project.workAreaInUs());
        targets.append(m_project.workAreaOutUs());
    }
    return targets;
}

void AppController::setBeatGridVisible(bool visible)
{
    if (m_beatGridVisible == visible)
        return;
    m_beatGridVisible = visible;
    rebuildBeatSnapTargets();
    emit beatAnalysisChanged();
}

void AppController::setOnsetsVisible(bool visible)
{
    if (m_onsetsVisible == visible)
        return;
    m_onsetsVisible = visible;
    rebuildBeatSnapTargets();
    emit beatAnalysisChanged();
}

void AppController::clearBeatAnalysis()
{
    // Bump the generation so an in-flight job cannot repopulate what we just cleared.
    ++m_beatAnalysisGeneration;
    if (m_beatAnalysis.isEmpty() && !m_beatAnalysisRunning && !m_beatGridVisible
        && !m_onsetsVisible) {
        return;
    }
    m_beatAnalysis.clear();
    m_beatAnalysisRaw = {};
    m_beatSnapTargets.clear();
    m_beatAudioFingerprint.clear();
    m_beatAnalysisRunning = false;
    // The layer toggles come down with the data. Leaving them lit over an empty result
    // would show two active buttons and nothing drawn, and re-analyzing automatically
    // would mean a full mix render on every clip nudge.
    m_beatGridVisible = false;
    m_onsetsVisible = false;
    emit beatAnalysisChanged();
}

void AppController::restoreFilmstripsAfterLoad()
{
    if (!m_assetLibrary)
        return;

    for (drift::Track &track : m_project.tracks()) {
        for (drift::Clip &clip : track.clips) {
            if (!clip.filmstripPath.isEmpty())
                continue;
            const int assetIndex = assetIndexForClip(clip);
            if (assetIndex >= 0) {
                m_assetLibrary->ensureMedia(assetIndex);
                clip.filmstripPath = m_assetLibrary->filmstripAt(assetIndex);
            }
            if (clip.filmstripPath.isEmpty())
                clip.filmstripPath = clip.thumbnailPath;
        }
    }
}

bool AppController::isValidClipIndex(int trackIndex, int clipIndex) const
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return false;
    return clipIndex >= 0 && clipIndex < m_project.tracks().at(trackIndex).clips.size();
}

void AppController::normalizeSelection()
{
    const QList<QPair<int, int>> selection = m_selection;
    QList<QPair<int, int>> kept;
    kept.reserve(selection.size());
    for (const QPair<int, int> &pair : selection) {
        if (isValidClipIndex(pair.first, pair.second) && !kept.contains(pair))
            kept.append(pair);
    }
    m_selection = kept;
    if (m_selectedTransitionTrack >= 0) {
        const QVariantMap selected = selectedTransitionData();
        if (selected.isEmpty())
            clearTransitionSelection();
    }
    if (m_selection.isEmpty()) {
        m_selectedTrack = -1;
        m_selectedClip = -1;
        return;
    }
    if (!isValidClipIndex(m_selectedTrack, m_selectedClip)) {
        m_selectedTrack = m_selection.constLast().first;
        m_selectedClip = m_selection.constLast().second;
    }
}

QByteArray AppController::serializeProjectJson() const
{
    QJsonObject root = m_project.toJson();
    root.insert(QStringLiteral("playheadUs"), static_cast<double>(m_playheadUs));
    root.insert(QStringLiteral("snapEnabled"), m_snapEnabled);
    root.insert(QStringLiteral("rippleEnabled"), m_rippleEnabled);
    root.insert(QStringLiteral("allowClipOverlap"), m_allowClipOverlap);
    root.insert(QStringLiteral("mediaGridMode"), m_mediaGridMode);
    root.insert(QStringLiteral("loopWorkArea"), m_loopWorkAreaEnabled);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

void AppController::resetSessionState()
{
    // These end up editing the project, so they have to run while it is still the one they were
    // opened against.
    endSpeedCurveSession();
    endFadeCurveSession();
    endSegmentationSession();

    m_clipboard.clear();
    // Keyed by timeline position rather than by source, so entries from the old project would be
    // served for the same span in the new one.
    m_subtitleWaveformCache.clear();
    m_subtitleWaveformPending.clear();
    // Beats are timeline-absolute for the same reason.
    clearBeatAnalysis();
    // Also drops the failed-source blacklist, so media that was missing gets another chance.
    m_filmstripTiles.clear();

    m_replacingAssetId.clear();
    m_pendingEffectTemplate.reset();
    m_previewDragActive = false;
    m_keyframeGraphHiddenProperties.clear();
    setCanvasCropMode(false);
    setSubtitleEditing(false);
    setSelectedSubtitleCue(-1);

    emit projectReset();
}

bool AppController::applyProjectJson(const QByteArray &data, QString *error)
{
    const QJsonDocument document = QJsonDocument::fromJson(data);
    if (!document.isObject()) {
        if (error)
            *error = QStringLiteral("Invalid project file");
        return false;
    }

    const QJsonObject root = document.object();

    // Parse into a local and check before touching anything: a project we end up rejecting must
    // leave the open one exactly as it was, session state included.
    QString parseError;
    drift::Project parsed = drift::Project::fromJson(root, &parseError);
    if (!parseError.isEmpty()) {
        if (error)
            *error = parseError;
        return false;
    }

    resetSessionState();
    m_project = std::move(parsed);

    // Stickers moved out of the QRC and into an addon, so projects saved before that store paths
    // like ":/qt/qml/Drift/resources/stickers/grinning.png" that no longer resolve. Repoint them
    // at the installed pack; a sticker with no installed pack keeps its old path and simply fails
    // to load, which is the same outcome as a missing media file.
    for (drift::MediaAsset &asset : m_project.assets()) {
        const QString migrated = resolveLegacyStickerPath(asset.path);
        if (!migrated.isEmpty())
            asset.path = migrated;
    }

    // Media that travelled inside the bundle now lives in this machine's extraction directory, so
    // the saved absolute paths have to be repointed before anything probes them. Same class of
    // fix-up as the two migrations around it.
    remapProjectPaths(m_pendingPathRemap);
    m_pendingPathRemap.clear();

    // Emoji clips point at a raster in this machine's app data cache, which a project opened
    // elsewhere will not have. The glyph sequence is what was saved, so re-derive the path — the
    // render is cached, and without the font addon it comes back empty and the clip fails to load
    // like any other missing file.
    for (drift::Track &track : m_project.tracks()) {
        for (drift::Clip &clip : track.clips) {
            if (clip.emoji.isEmpty())
                continue;
            const QString path = emojiImagePath(clip.emoji);
            if (path.isEmpty())
                continue;
            clip.path = path;
            clip.thumbnailPath = path;
            clip.filmstripPath = path;
        }
    }

    if (m_assetLibrary)
        m_assetLibrary->setProject(&m_project);

    reportMissingCatalogEntries();

    setPlaying(false);
    m_snapEnabled = root.value(QStringLiteral("snapEnabled")).toBool(true);
    m_rippleEnabled = root.value(QStringLiteral("rippleEnabled")).toBool(false);
    m_allowClipOverlap = root.value(QStringLiteral("allowClipOverlap")).toBool(false);

    if (root.contains(QStringLiteral("mediaGridMode"))) {
        m_mediaGridMode = root.value(QStringLiteral("mediaGridMode")).toBool(true);
        emit mediaGridModeChanged();
    }

    if (root.contains(QStringLiteral("loopWorkArea"))) {
        setLoopWorkAreaEnabled(root.value(QStringLiteral("loopWorkArea")).toBool(false));
    } else {
        m_playback.setLoopWorkArea(m_loopWorkAreaEnabled);
    }

    if (root.contains(QStringLiteral("playheadUs"))) {
        setPlayheadUs(static_cast<drift::TimeUs>(root.value(QStringLiteral("playheadUs")).toDouble()));
    } else {
        setPlayheadSeconds(root.value(QStringLiteral("playheadSeconds")).toDouble());
    }

    restoreFilmstripsAfterLoad();
    m_playback.setProject(&m_project);
    m_undoStack.clear();
    clearSelection();
    setDirty(false);
    emit snapEnabledChanged();
    emit rippleEnabledChanged();
    emit allowClipOverlapChanged();
    emit tracksChanged();
    emit bookmarksChanged();
    emit workAreaChanged();
    emit projectNameChanged();
    emit projectMetadataChanged();
    emit backgroundChanged();
    return true;
}

drift::bundle::WriteRequest AppController::buildWriteRequest(bool embedSource) const
{
    drift::bundle::WriteRequest request;
    request.document = QJsonDocument::fromJson(serializeProjectJson()).object();
    request.projectId = m_project.id();
    request.title = m_project.name();
    request.author = m_project.author();
    request.description = m_project.description();
    request.createdAt = m_project.createdAt();
    request.modifiedAt = m_project.modifiedAt();
    request.addons = drift::bundle::collectAddons(m_project);
    request.media = drift::bundle::collectMedia(m_project, embedSource);

    // Without this a plain Save of a project that arrived as a package would drop its media back
    // to references into the extraction dir, which the startup sweep is free to delete.
    if (!embedSource) {
        for (drift::bundle::MediaEntry &entry : request.media) {
            if (m_embeddedSources.contains(entry.originalPath))
                entry.embedded = true;
        }
    }
    return request;
}

void AppController::rememberEmbeddedSources(const QList<drift::bundle::MediaEntry> &media)
{
    m_embeddedSources.clear();
    for (const drift::bundle::MediaEntry &entry : media) {
        if (entry.embedded)
            m_embeddedSources.insert(entry.originalPath);
    }
}

void AppController::saveProject(const QUrl &url)
{
    const QString path = url.toLocalFile();
    if (path.isEmpty()) {
        setLastMessage(tr("That save location isn’t valid"), QStringLiteral("error"));
        return;
    }
    if (m_packaging) {
        setLastMessage(tr("Already saving"), QStringLiteral("warning"));
        return;
    }

    m_project.setModifiedAt(QDateTime::currentDateTimeUtc());

    const drift::bundle::WriteRequest request = buildWriteRequest(/*embedSource=*/false);
    QString error;
    if (!drift::bundle::write(path, request, {}, &error)) {
        setLastMessage(error, QStringLiteral("error"));
        return;
    }
    rememberEmbeddedSources(request.media);

    setCurrentProjectPath(path);
    addRecentProject(path);
    setDirty(false);
    deleteRecoveryFile();
    emit projectMetadataChanged();
    setLastMessage(tr("Project saved"), QStringLiteral("success"));
}

void AppController::packageProject(const QUrl &url)
{
    const QString path = url.toLocalFile();
    if (path.isEmpty()) {
        setLastMessage(tr("That save location isn’t valid"), QStringLiteral("error"));
        return;
    }
    if (m_packaging)
        return;

    m_project.setModifiedAt(QDateTime::currentDateTimeUtc());
    m_packageCancel = 0;
    m_packaging = true;
    m_packageProgress = 0.0;
    emit packagingChanged();
    emit packageProgressChanged();

    // The whole request is built here, on the GUI thread: the worker copies gigabytes and must not
    // be reading the project while the timeline is free to change under it.
    const drift::bundle::WriteRequest request = buildWriteRequest(/*embedSource=*/true);

    (void)QtConcurrent::run([this, path, request]() {
        QString error;
        const auto progress = [this](qint64 done, qint64 total) {
            if (m_packageCancel.loadRelaxed())
                return false;
            const double fraction = total > 0 ? double(done) / double(total) : 0.0;
            QMetaObject::invokeMethod(
                this,
                [this, fraction]() {
                    m_packageProgress = fraction;
                    emit packageProgressChanged();
                },
                Qt::QueuedConnection);
            return true;
        };
        const bool ok = drift::bundle::write(path, request, progress, &error);
        QMetaObject::invokeMethod(
            this,
            [this, ok, error, path, request]() {
                m_packaging = false;
                emit packagingChanged();
                if (!ok) {
                    setLastMessage(error, QStringLiteral("error"));
                    emit packageFinished(false, error);
                    return;
                }
                rememberEmbeddedSources(request.media);
                m_packageProgress = 1.0;
                emit packageProgressChanged();
                setCurrentProjectPath(path);
                addRecentProject(path);
                setDirty(false);
                deleteRecoveryFile();
                emit projectMetadataChanged();
                setLastMessage(tr("Shareable copy ready"), QStringLiteral("success"));
                emit packageFinished(true, QStringLiteral("Shareable copy ready"));
            },
            Qt::QueuedConnection);
    });
}

void AppController::cancelPackage()
{
    m_packageCancel = 1;
}

void AppController::loadProject(const QUrl &url)
{
    const QString path = url.toLocalFile();
    if (path.isEmpty()) {
        setLastMessage(tr("That project location isn’t valid"), QStringLiteral("error"));
        return;
    }

    QString error;
    const std::optional<drift::bundle::BundleInfo> info =
        drift::bundle::readManifest(path, &error);
    if (!info) {
        setLastMessage(error, QStringLiteral("error"));
        return;
    }

    // Named by the project's own id, which survives the round-trip, so reopening the same bundle
    // lands on the files it already unpacked.
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString destDir =
        QDir(base).filePath(QStringLiteral("projects/%1/media").arg(info->projectId));

    const int generation = ++m_loadGeneration;
    const drift::bundle::BundleInfo bundle = *info;

    auto finishLoad = [this, path, bundle, generation](const QHash<QString, QString> &remap,
                                                       const QString &extractError, bool extractOk) {
        if (generation != m_loadGeneration)
            return;
        if (!extractOk) {
            setLastMessage(extractError, QStringLiteral("error"));
            return;
        }

        m_pendingPathRemap = remap;
        QString applyError;
        if (!applyProjectJson(QJsonDocument(bundle.document).toJson(QJsonDocument::Compact),
                              &applyError)) {
            m_pendingPathRemap.clear();
            setLastMessage(applyError, QStringLiteral("error"));
            return;
        }

        m_embeddedSources.clear();
        for (const drift::bundle::MediaEntry &entry : bundle.media) {
            if (entry.embedded && entry.role == drift::bundle::MediaRole::Source)
                m_embeddedSources.insert(remap.value(entry.originalPath, entry.originalPath));
        }

        setCurrentProjectPath(path);
        addRecentProject(path);
        deleteRecoveryFile();
        setProjectLayoutChosen(true);
        setLastMessage(tr("Project loaded"), QStringLiteral("success"));
        reportMissingAddons(bundle.addons);
    };

    if (bundle.embeddedBytes <= 0) {
        finishLoad({}, {}, true);
        return;
    }

    setLastMessage(tr("Unpacking project media…"));
    (void)QtConcurrent::run([this, path, destDir, generation, finishLoad]() {
        QString error;
        QHash<QString, QString> remap;
        const bool ok = drift::bundle::extract(path, destDir, {}, &remap, &error);
        QMetaObject::invokeMethod(
            this,
            [finishLoad, remap, error, ok, generation, this]() {
                if (generation != m_loadGeneration)
                    return;
                finishLoad(remap, error, ok);
            },
            Qt::QueuedConnection);
    });
}

void AppController::reportMissingAddons(const QList<drift::bundle::AddonRef> &addons)
{
    QVariantList missing;
    for (const drift::bundle::AddonRef &addon : addons) {
        if (drift::addon::installedAddon(addon.id))
            continue;
        missing.append(QVariantMap{
            {QStringLiteral("id"), addon.id},
            {QStringLiteral("name"), addon.name.isEmpty() ? addon.id : addon.name},
            {QStringLiteral("version"), addon.version},
            {QStringLiteral("kinds"), addon.kinds},
        });
    }
    if (!missing.isEmpty())
        emit missingAddons(missing);
}

void AppController::remapProjectPaths(const QHash<QString, QString> &remap)
{
    if (remap.isEmpty())
        return;

    // Tiles are keyed on the old paths, and a source that was missing is blacklisted until
    // this runs — without it a relinked clip's filmstrip never comes back.
    m_filmstripTiles.clear();

    const auto repoint = [&remap](QString &path) {
        const auto it = remap.constFind(path);
        if (it == remap.constEnd())
            return false;
        path = it.value();
        return true;
    };

    for (drift::MediaAsset &asset : m_project.assets()) {
        if (repoint(asset.path)) {
            asset.thumbnailPath.clear();
            asset.filmstripPath.clear();
        }
    }

    for (drift::Track &track : m_project.tracks()) {
        for (drift::Clip &clip : track.clips) {
            repoint(clip.mask.mattePath);
            repoint(clip.faceTrackPath);
            if (repoint(clip.path)) {
                // Cache renders keyed on the old path; AssetLibrary and
                // restoreFilmstripsAfterLoad regenerate them for the new one.
                clip.thumbnailPath.clear();
                clip.filmstripPath.clear();
            }
        }
    }
}

void AppController::newProject()
{
    setPlaying(false);
    resetSessionState();
    // Whole-document replacement rather than resetToDefaultTimeline(), which only clears the
    // tracks — the asset pool, name, canvas size, bookmarks, work area and background all used to
    // survive into the "new" project.
    m_project = drift::Project{};
    m_project.setAuthor(QSettings().value(QStringLiteral("authorName")).toString());
    m_embeddedSources.clear();
    if (m_assetLibrary)
        m_assetLibrary->setProject(&m_project);
    m_playback.setProject(&m_project);
    m_undoStack.clear();
    clearSelection();
    setPlayheadUs(0);
    setCurrentProjectPath(QString());
    // Per-project editor prefs: these travel in the project file, so they belong to the document
    // that was just discarded. Same defaults applyProjectJson falls back to.
    m_snapEnabled = true;
    m_rippleEnabled = false;
    m_allowClipOverlap = false;
    setLoopWorkAreaEnabled(false);
    setMediaGridMode(true);
    setDirty(false);
    deleteRecoveryFile();
    // Always notify — even when already false — so the layout chooser reopens
    // after "Decide later" + New Project.
    m_projectLayoutChosen = false;
    emit projectLayoutChosenChanged();
    emit snapEnabledChanged();
    emit rippleEnabledChanged();
    emit allowClipOverlapChanged();
    emit tracksChanged();
    emit bookmarksChanged();
    emit workAreaChanged();
    emit projectNameChanged();
    emit projectMetadataChanged();
    emit backgroundChanged();
    setLastMessage(tr("New project"));
}

void AppController::openRecentProject(const QString &path)
{
    if (path.isEmpty())
        return;
    loadProject(QUrl::fromLocalFile(path));
}

QVariantList AppController::recentProjects() const
{
    QSettings settings;
    const QStringList paths = settings.value(QStringLiteral("recentProjects")).toStringList();
    QVariantList out;
    for (const QString &path : paths) {
        const QFileInfo info(path);
        out.append(QVariantMap{
            {QStringLiteral("path"), path},
            {QStringLiteral("name"), info.fileName()},
            {QStringLiteral("exists"), info.exists()},
        });
    }
    return out;
}

void AppController::addRecentProject(const QString &path)
{
    if (path.isEmpty())
        return;
    QSettings settings;
    QStringList paths = settings.value(QStringLiteral("recentProjects")).toStringList();
    paths.removeAll(path);
    paths.prepend(path);
    while (paths.size() > kMaxRecentProjects)
        paths.removeLast();
    settings.setValue(QStringLiteral("recentProjects"), paths);
    emit recentProjectsChanged();
}

void AppController::clearRecentProjects()
{
    QSettings settings;
    settings.remove(QStringLiteral("recentProjects"));
    emit recentProjectsChanged();
}

void AppController::removeRecentProject(const QString &path)
{
    if (path.isEmpty())
        return;
    QSettings settings;
    QStringList paths = settings.value(QStringLiteral("recentProjects")).toStringList();
    if (paths.removeAll(path) == 0)
        return;
    settings.setValue(QStringLiteral("recentProjects"), paths);
    emit recentProjectsChanged();
}

void AppController::setDirty(bool dirty)
{
    if (m_dirty == dirty)
        return;
    m_dirty = dirty;
    emit dirtyChanged();
}

void AppController::setCurrentProjectPath(const QString &path)
{
    if (m_currentProjectPath == path)
        return;
    m_currentProjectPath = path;
    // Cleared by newProject(); set after load/save/package so next launch can reopen.
    QSettings().setValue(QStringLiteral("lastSessionPath"), path);
    emit currentProjectPathChanged();
}

QString AppController::recoveryFilePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    // Plain JSON, not a bundle: this is an internal crash snapshot written every few seconds and
    // never opened through the file dialog, so it must not repack the project's media.
    return dir + QStringLiteral("/recovery/autosave.json");
}

void AppController::writeRecoveryFile()
{
    const QString path = recoveryFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QJsonObject root = QJsonDocument::fromJson(serializeProjectJson()).object();
    QJsonObject meta;
    meta.insert(QStringLiteral("originalPath"), m_currentProjectPath);
    meta.insert(QStringLiteral("projectName"), m_project.name());
    meta.insert(QStringLiteral("savedAt"), QDateTime::currentDateTime().toString(Qt::ISODate));
    root.insert(QStringLiteral("__recovery"), meta);

    // Write to a temp sibling and rename so a crash mid-write can't corrupt the
    // recovery file itself.
    const QString tmpPath = path + QStringLiteral(".tmp");
    QFile file(tmpPath);
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
    if (QFile::exists(path))
        QFile::remove(path);
    QFile::rename(tmpPath, path);
}

void AppController::deleteRecoveryFile()
{
    const QString path = recoveryFilePath();
    if (QFile::exists(path))
        QFile::remove(path);
    if (m_recoveryAvailable) {
        m_recoveryAvailable = false;
        m_recoveryInfo.clear();
        emit recoveryChanged();
    }
}

void AppController::detectRecoveryFile()
{
    const QString path = recoveryFilePath();
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly))
        return;

    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonObject meta = root.value(QStringLiteral("__recovery")).toObject();
    m_recoveryInfo = QVariantMap{
        {QStringLiteral("originalPath"), meta.value(QStringLiteral("originalPath")).toString()},
        {QStringLiteral("projectName"), meta.value(QStringLiteral("projectName")).toString()},
        {QStringLiteral("savedAt"), meta.value(QStringLiteral("savedAt")).toString()},
    };
    m_recoveryAvailable = true;
    emit recoveryChanged();
}

void AppController::restoreAutosave()
{
    const QString path = recoveryFilePath();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setLastMessage(tr("No recovery file found"), QStringLiteral("warning"));
        return;
    }

    const QByteArray data = file.readAll();
    file.close();

    const QString originalPath = m_recoveryInfo.value(QStringLiteral("originalPath")).toString();
    QString error;
    if (!applyProjectJson(data, &error)) {
        setLastMessage(error, QStringLiteral("error"));
        return;
    }

    // Restore the association with the original file (if any) and mark unsaved so
    // the user is nudged to re-save; keep the recovery file until the next save.
    setCurrentProjectPath(originalPath);
    setDirty(true);
    m_recoveryAvailable = false;
    m_recoveryInfo.clear();
    emit recoveryChanged();
    setProjectLayoutChosen(true);
    setLastMessage(tr("Recovered unsaved work"), QStringLiteral("success"));
}

void AppController::discardAutosave()
{
    // Fresh timeline and clear the autosave snapshot from the previous session.
    newProject();
    setLastMessage(tr("Started new session"));
}

bool AppController::restoreLastSessionIfEnabled()
{
    if (!m_reopenLastProject)
        return false;

    // Unsaved (or crashed) session takes priority over the last clean .drift path.
    if (m_recoveryAvailable) {
        restoreAutosave();
        return true;
    }

    const QString path = QSettings().value(QStringLiteral("lastSessionPath")).toString();
    if (path.isEmpty() || !QFileInfo::exists(path))
        return false;

    loadProject(QUrl::fromLocalFile(path));
    return true;
}

void AppController::discardUnsavedChanges()
{
    // Don't Save before quit: clear dirty so aboutToQuit does not write a
    // recovery file the user just chose to throw away. Timeline is left alone —
    // the window is closing (or the caller is about to replace the project).
    setDirty(false);
    deleteRecoveryFile();
}

QVariantList AppController::exportPresets() const
{
    QVariantList out;
    for (const ExportScalePreset &preset : Exporter::scalePresets()) {
        out.append(QVariantMap{
            {QStringLiteral("id"), preset.id},
            {QStringLiteral("label"), preset.label},
        });
    }
    return out;
}

QVariantList AppController::exportScaleOptions() const
{
    return Exporter::scaleOptions(m_project.width(), m_project.height());
}

QVariantList AppController::exportFrameRateOptions() const
{
    return Exporter::frameRateOptions(m_project.fps());
}

QVariantList AppController::exportVideoCodecs() const
{
    return Exporter::videoCodecs();
}

QVariantList AppController::exportAudioCodecs() const
{
    return Exporter::audioCodecs();
}

bool AppController::exportGifAvailable() const
{
    return Exporter::gifAvailable();
}

QVariantMap AppController::exportDefaultSettings() const
{
    const ExportSettings s = Exporter::defaultSettings();
    return QVariantMap{
        {QStringLiteral("targetHeight"), s.targetHeight},
        {QStringLiteral("fpsNum"), s.fpsNum},
        {QStringLiteral("fpsDen"), s.fpsDen},
        {QStringLiteral("videoCodecId"), s.videoCodecId},
        {QStringLiteral("rateControl"), s.rateControl},
        {QStringLiteral("crf"), s.crf},
        {QStringLiteral("videoBitrateKbps"), s.videoBitrateKbps},
        {QStringLiteral("videoPreset"), s.videoPreset},
        {QStringLiteral("audioCodecId"), s.audioCodecId},
        {QStringLiteral("audioBitrateKbps"), s.audioBitrateKbps},
        {QStringLiteral("audioOnly"), s.audioOnly},
        {QStringLiteral("gifExport"), s.gifExport},
    };
}

QVariantMap AppController::lastExportSettings() const
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("export"));
    if (!settings.contains(QStringLiteral("videoCodecId"))
        && !settings.contains(QStringLiteral("audioCodecId"))
        && !settings.contains(QStringLiteral("scaleId"))) {
        return {};
    }

    // A relaunch re-parses the INI and hands every value back as a QString, so the
    // types have to be restored here. QML sees these directly, and in JavaScript the
    // string "false" is truthy — an untyped audioOnly would put the export dialog in
    // audio mode on every launch after the first.
    QVariantMap out;
    const auto takeString = [&](const QString &key) {
        if (settings.contains(key))
            out.insert(key, settings.value(key).toString());
    };
    const auto takeInt = [&](const QString &key) {
        if (settings.contains(key))
            out.insert(key, settings.value(key).toInt());
    };
    const auto takeBool = [&](const QString &key) {
        if (settings.contains(key))
            out.insert(key, settings.value(key).toBool());
    };
    takeString(QStringLiteral("scaleId"));
    takeInt(QStringLiteral("targetHeight"));
    takeInt(QStringLiteral("fpsNum"));
    takeInt(QStringLiteral("fpsDen"));
    takeString(QStringLiteral("videoCodecId"));
    takeString(QStringLiteral("rateControl"));
    takeInt(QStringLiteral("crf"));
    takeInt(QStringLiteral("videoBitrateKbps"));
    takeString(QStringLiteral("videoPreset"));
    takeString(QStringLiteral("audioCodecId"));
    takeInt(QStringLiteral("audioBitrateKbps"));
    takeBool(QStringLiteral("audioOnly"));
    takeBool(QStringLiteral("gifExport"));
    takeBool(QStringLiteral("exportWorkAreaOnly"));
    return out;
}

QString AppController::lastExportFolder() const
{
    const QString folder = QSettings().value(QStringLiteral("export/lastFolder")).toString();
    if (folder.isEmpty() || !QDir(folder).exists())
        return {};
    return folder;
}

void AppController::rememberExportChoice(const QString &outputPath, const QVariantMap &settings)
{
    QSettings store;
    if (!outputPath.isEmpty()) {
        const QFileInfo info(outputPath);
        const QString folder = info.absolutePath();
        if (!folder.isEmpty() && QDir(folder).exists())
            store.setValue(QStringLiteral("export/lastFolder"), folder);
    }

    store.beginGroup(QStringLiteral("export"));
    const auto put = [&](const QString &key) {
        if (settings.contains(key))
            store.setValue(key, settings.value(key));
    };
    put(QStringLiteral("scaleId"));
    put(QStringLiteral("targetHeight"));
    put(QStringLiteral("fpsNum"));
    put(QStringLiteral("fpsDen"));
    put(QStringLiteral("videoCodecId"));
    put(QStringLiteral("rateControl"));
    put(QStringLiteral("crf"));
    put(QStringLiteral("videoBitrateKbps"));
    put(QStringLiteral("videoPreset"));
    put(QStringLiteral("audioCodecId"));
    put(QStringLiteral("audioBitrateKbps"));
    put(QStringLiteral("audioOnly"));
    put(QStringLiteral("gifExport"));
    put(QStringLiteral("exportWorkAreaOnly"));
    store.endGroup();
}

QString AppController::exportPreferredAudioOnlyContainer(const QString &audioCodecId) const
{
    return Exporter::preferredAudioOnlyContainer(audioCodecId);
}

QString AppController::exportPreferredContainer(const QString &videoCodecId,
                                                const QString &audioCodecId) const
{
    return Exporter::preferredContainer(videoCodecId, audioCodecId);
}

QStringList AppController::exportSaveFilters(const QString &container, bool audioOnly) const
{
    return Exporter::saveFilters(container, audioOnly);
}

QString AppController::exportDefaultSuffix(const QString &container, bool audioOnly) const
{
    return Exporter::defaultSuffix(container, audioOnly);
}

double AppController::exportProgress() const
{
    return m_exportProgress;
}

void AppController::cancelExport()
{
    if (m_exportInProgress)
        m_exportCancel.storeRelaxed(1);
}

void AppController::exportProject(const QUrl &outputUrl)
{
    exportWithSettings(outputUrl, exportDefaultSettings());
}

void AppController::exportWithPreset(const QUrl &outputUrl, const QString &presetId)
{
    QVariantMap map = exportDefaultSettings();
    if (const ExportScalePreset *preset = Exporter::scalePresetById(presetId)) {
        map.insert(QStringLiteral("scaleId"), preset->id);
        map.insert(QStringLiteral("targetHeight"), preset->targetHeight);
        map.insert(QStringLiteral("videoBitrateKbps"), preset->videoBitrateKbps);
    }
    exportWithSettings(outputUrl, map);
}

void AppController::exportWithSettings(const QUrl &outputUrl, const QVariantMap &settings)
{
    const QString outputPath = outputUrl.toLocalFile();
    if (outputPath.isEmpty()) {
        setLastMessage(tr("That save location isn’t valid"), QStringLiteral("error"));
        emit exportFinished(false);
        return;
    }

    if (m_exportInProgress) {
        setLastMessage(tr("Export already in progress"), QStringLiteral("warning"));
        return;
    }

    rememberExportChoice(outputPath, settings);

    const ExportSettings exportSettings = Exporter::settingsFromMap(settings);

    // Stop playback so the decode pool isn't driven from two threads at once.
    setPlaying(false);

    m_exportCancel.storeRelaxed(0);
    m_exportProgress = 0.0;
    emit exportProgressChanged();
    m_exportInProgress = true;
    emit exportInProgressChanged();
    setLastMessage(tr("Exporting..."));

    // Snapshot the project so edits during export can't race the encoder.
    const drift::Project snapshot = m_project;

    (void)QtConcurrent::run([this, snapshot, exportSettings, outputPath]() {
        QString error;
        const bool ok = Exporter::run(
            snapshot, exportSettings, outputPath, &error, [this](double fraction) {
                QMetaObject::invokeMethod(
                    this,
                    [this, fraction]() {
                        m_exportProgress = fraction;
                        emit exportProgressChanged();
                    },
                    Qt::QueuedConnection);
                return m_exportCancel.loadRelaxed() == 0;
            });

        QMetaObject::invokeMethod(
            this,
            [this, ok, error]() {
                m_exportInProgress = false;
                m_exportProgress = ok ? 1.0 : 0.0;
                emit exportProgressChanged();
                emit exportInProgressChanged();
                setLastMessage(ok ? tr("Export complete") : error,
                               ok ? QStringLiteral("success") : QStringLiteral("error"));
                emit exportFinished(ok);
            },
            Qt::QueuedConnection);
    });
}

bool AppController::mcpRunning() const
{
    return m_mcp && m_mcp->running();
}

QString AppController::mcpUrl() const
{
    return m_mcp ? m_mcp->url() : QString();
}

QString AppController::mcpToken() const
{
    return m_mcp ? m_mcp->token() : QString();
}

int AppController::mcpPort() const
{
    return m_mcp ? int(m_mcp->port()) : 0;
}

QString AppController::mcpError() const
{
    return m_mcp ? m_mcp->error() : QString();
}

QString AppController::mcpCursorSnippet() const
{
    return m_mcp ? m_mcp->cursorSnippet() : QString();
}

QString AppController::mcpClaudeCommand() const
{
    return m_mcp ? m_mcp->claudeCommand() : QString();
}

QString AppController::mcpStdioSnippet() const
{
    const QJsonObject server{
        {QStringLiteral("command"), QCoreApplication::applicationFilePath()},
        {QStringLiteral("args"), QJsonArray{QStringLiteral("--mcp-stdio")}},
    };
    const QJsonObject root{
        {QStringLiteral("mcpServers"), QJsonObject{{QStringLiteral("drift"), server}}},
    };
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

void AppController::setMcpEnabled(bool enabled)
{
    if (!m_mcp)
        return;
    if (enabled)
        m_mcp->start();
    else
        m_mcp->stop();
}

namespace {

void copyToClipboard(const QString &text)
{
    if (text.isEmpty())
        return;
    if (QClipboard *clip = QGuiApplication::clipboard())
        clip->setText(text);
}

} // namespace

void AppController::copyMcpCursorSnippet()
{
    copyToClipboard(mcpCursorSnippet());
}

void AppController::copyMcpClaudeCommand()
{
    copyToClipboard(mcpClaudeCommand());
}

void AppController::copyMcpStdioSnippet()
{
    copyToClipboard(mcpStdioSnippet());
}

QString AppController::mcpAgentGuide() const
{
    QString guide = drift::mcp::agentGuideText();
    if (m_mcp && m_mcp->running()) {
        guide += QStringLiteral("\nThis session:\nURL: %1\nToken: %2\n")
                     .arg(mcpUrl(), mcpToken());
    }
    return guide;
}

void AppController::copyMcpAgentGuide()
{
    copyToClipboard(mcpAgentGuide());
}

void AppController::rebuildMcpClipIndexIfNeeded() const
{
    if (m_mcpClipIndexRevision == m_mcpEditRevision)
        return;
    m_mcpClipIndex.clear();
    const QList<drift::Track> &tracks = m_project.tracks();
    for (int t = 0; t < tracks.size(); ++t) {
        const QList<drift::Clip> &clips = tracks.at(t).clips;
        for (int c = 0; c < clips.size(); ++c)
            m_mcpClipIndex.insert(clips.at(c).id, {t, c});
    }
    m_mcpClipIndexRevision = m_mcpEditRevision;
}

QPair<int, int> AppController::mcpLocateClip(const QString &id) const
{
    if (id.isEmpty())
        return {-1, -1};
    rebuildMcpClipIndexIfNeeded();
    return m_mcpClipIndex.value(id, {-1, -1});
}

QString AppController::mcpClipId(int trackIndex, int clipIndex) const
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return {};
    return m_project.tracks().at(trackIndex).clips.at(clipIndex).id;
}

QVariantMap AppController::mcpCompactClip(int trackIndex, int clipIndex, bool includeCanvas) const
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return {};
    const drift::Clip &clip = m_project.tracks().at(trackIndex).clips.at(clipIndex);
    QVariantMap out{
        {QStringLiteral("id"), clip.id},
        {QStringLiteral("kind"), drift::clipTypeToString(clip.type)},
        {QStringLiteral("name"), clip.name},
        {QStringLiteral("start"), drift::usToSeconds(clip.timelineStart)},
        {QStringLiteral("duration"), drift::usToSeconds(clip.timelineDuration)},
        {QStringLiteral("inPoint"), drift::usToSeconds(clip.srcIn)},
        {QStringLiteral("outPoint"), drift::usToSeconds(clip.srcOut)},
        {QStringLiteral("assetId"), clip.assetId},
    };
    if (!includeCanvas)
        return out;

    const double at = playheadSeconds();
    out.insert(QStringLiteral("x"), propertyValueAt(trackIndex, clipIndex, QStringLiteral("x"), at, 0));
    out.insert(QStringLiteral("y"), propertyValueAt(trackIndex, clipIndex, QStringLiteral("y"), at, 0));
    out.insert(QStringLiteral("w"), propertyValueAt(trackIndex, clipIndex, QStringLiteral("width"), at, 0));
    out.insert(QStringLiteral("h"), propertyValueAt(trackIndex, clipIndex, QStringLiteral("height"), at, 0));
    out.insert(QStringLiteral("rotation"),
               propertyValueAt(trackIndex, clipIndex, QStringLiteral("rotation"), at, 0));
    out.insert(QStringLiteral("opacity"),
               propertyValueAt(trackIndex, clipIndex, QStringLiteral("opacity"), at, 1));
    return out;
}

QJsonObject AppController::mcpInspect(bool includeClips, int sinceRevision, bool detail) const
{
    using namespace drift::mcp;
    if (sinceRevision >= 0 && sinceRevision == m_mcpEditRevision)
        return ok({{QStringLiteral("unchanged"), true}, {QStringLiteral("revision"), m_mcpEditRevision}});

    int clipCount = 0;
    QJsonArray trackRows;
    const QList<drift::Track> &projectTracks = m_project.tracks();
    const QVariantList trackModels = detail ? tracks() : QVariantList{};
    for (int t = 0; t < projectTracks.size(); ++t) {
        const drift::Track &track = projectTracks.at(t);
        clipCount += track.clips.size();
        QJsonObject row{
            {QStringLiteral("i"), t},
            {QStringLiteral("type"), drift::trackTypeToString(track.type)},
            {QStringLiteral("clips"), track.clips.size()},
            {QStringLiteral("muted"), track.muted},
            {QStringLiteral("hidden"), track.hidden},
        };
        if (detail && t < trackModels.size()) {
            const QVariantMap tm = trackModels.at(t).toMap();
            row.insert(QStringLiteral("showWaveform"), tm.value(QStringLiteral("showWaveform")).toBool());
            row.insert(QStringLiteral("heightScale"), tm.value(QStringLiteral("heightScale")).toDouble());
            const QVariantList transitions = tm.value(QStringLiteral("transitions")).toList();
            QJsonArray trJson;
            for (const QVariant &tr : transitions)
                trJson.append(QJsonObject::fromVariantMap(tr.toMap()));
            if (!trJson.isEmpty())
                row.insert(QStringLiteral("transitions"), trJson);
        }
        if (includeClips) {
            QJsonArray clips;
            for (int c = 0; c < track.clips.size(); ++c) {
                if (detail && t < trackModels.size()) {
                    const QVariantList clipList = trackModels.at(t).toMap().value(QStringLiteral("clips")).toList();
                    if (c < clipList.size())
                        clips.append(QJsonObject::fromVariantMap(clipList.at(c).toMap()));
                } else {
                    const QVariantMap compact = mcpCompactClip(t, c, false);
                    clips.append(QJsonObject::fromVariantMap(compact));
                }
            }
            row.insert(QStringLiteral("items"), clips);
        }
        trackRows.append(row);
    }

    QJsonArray assets;
    if (m_assetLibrary) {
        for (int i = 0; i < m_assetLibrary->count(); ++i) {
            const QVariantMap a = m_assetLibrary->assetAt(i);
            assets.append(QJsonObject{
                {QStringLiteral("index"), i},
                {QStringLiteral("id"), a.value(QStringLiteral("id")).toString()},
                {QStringLiteral("name"), a.value(QStringLiteral("name")).toString()},
                {QStringLiteral("kind"), a.value(QStringLiteral("kind")).toString()},
                {QStringLiteral("dur"), a.value(QStringLiteral("durationSeconds")).toDouble()},
            });
        }
    }

    QJsonObject extra{
        {QStringLiteral("revision"), m_mcpEditRevision},
        {QStringLiteral("name"), m_project.name()},
        {QStringLiteral("w"), m_project.width()},
        {QStringLiteral("h"), m_project.height()},
        {QStringLiteral("fps"), m_project.fps()},
        {QStringLiteral("dur"), drift::usToSeconds(m_project.durationUs())},
        {QStringLiteral("playhead"), playheadSeconds()},
        {QStringLiteral("playing"), playing()},
        {QStringLiteral("overlap"), m_allowClipOverlap},
        {QStringLiteral("clips"), clipCount},
        {QStringLiteral("tracks"), trackRows},
        {QStringLiteral("assets"), assets},
        {QStringLiteral("path"), m_currentProjectPath},
        {QStringLiteral("dirty"), m_dirty},
        {QStringLiteral("background"), QJsonObject::fromVariantMap(background())},
        {QStringLiteral("export"),
         QJsonObject{{QStringLiteral("active"), m_exportInProgress},
                     {QStringLiteral("progress"), m_exportProgress}}},
    };
    if (detail) {
        QJsonArray marks;
        for (const QVariant &v : bookmarks()) {
            const QVariantMap b = v.toMap();
            marks.append(QJsonObject{
                {QStringLiteral("at"), b.value(QStringLiteral("seconds")).toDouble()},
                {QStringLiteral("label"), b.value(QStringLiteral("label")).toString()},
            });
        }
        extra.insert(QStringLiteral("bookmarks"), marks);
        extra.insert(QStringLiteral("package"),
                     QJsonObject{{QStringLiteral("active"), packaging()},
                                 {QStringLiteral("progress"), packageProgress()}});
        extra.insert(QStringLiteral("subtitleGen"),
                     QJsonObject{{QStringLiteral("active"), subtitleGenerating()},
                                 {QStringLiteral("progress"), subtitleGenProgress()},
                                 {QStringLiteral("status"), subtitleGenStatus()}});
        extra.insert(QStringLiteral("reverseRender"),
                     QJsonObject{{QStringLiteral("active"), reverseRendering()},
                                 {QStringLiteral("progress"), reverseRenderProgress()},
                                 {QStringLiteral("status"), reverseRenderStatus()}});
    }
    if (m_project.hasWorkArea()) {
        extra.insert(QStringLiteral("work_in"), workAreaInSeconds());
        extra.insert(QStringLiteral("work_out"), workAreaOutSeconds());
    }
    return ok(extra);
}

bool AppController::mcpSetWorkArea(double inSeconds, double outSeconds)
{
    const drift::TimeUs inUs = qMax<drift::TimeUs>(0, drift::secondsToUs(inSeconds));
    const drift::TimeUs outUs = drift::secondsToUs(outSeconds);
    if (outUs <= inUs)
        return false;

    const drift::Project before = m_project;
    m_project.setWorkAreaInUs(inUs);
    m_project.setWorkAreaOutUs(outUs);
    pushProjectEdit(before, QStringLiteral("Work area"));
    finishEdit(QStringLiteral("Work area set"));
    emit workAreaChanged();
    return true;
}

void AppController::mcpRememberExportSettings(const QVariantMap &settings)
{
    rememberExportChoice({}, settings);
}

bool AppController::mcpSetClipCanvas(int trackIndex, int clipIndex, const QVariantMap &patch)
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return false;

    drift::Track &track = m_project.tracks()[trackIndex];
    drift::Clip &clip = track.clips[clipIndex];
    if (clip.type == drift::ClipType::Audio)
        return false;

    const drift::Project before = m_project;
    const drift::TimeUs relative = qMax<drift::TimeUs>(0, m_playheadUs - clip.timelineStart);
    bool any = false;
    auto write = [&](const QString &patchKey, const QString &prop) {
        if (!patch.contains(patchKey))
            return;
        any = writeClipPropValue(clip, prop, relative, patch.value(patchKey).toDouble(), true, true)
              || any;
    };
    write(QStringLiteral("x"), QStringLiteral("x"));
    write(QStringLiteral("y"), QStringLiteral("y"));
    write(QStringLiteral("w"), QStringLiteral("width"));
    write(QStringLiteral("h"), QStringLiteral("height"));
    write(QStringLiteral("rotation"), QStringLiteral("rotation"));
    write(QStringLiteral("opacity"), QStringLiteral("opacity"));
    if (!any)
        return false;

    if (!m_mcpUndoSuspended) {
        pushProjectEdit(before, QStringLiteral("MCP canvas"));
        finishEdit(QStringLiteral("MCP canvas"));
    } else {
        emitPreviewFrame();
    }
    return true;
}

QJsonObject AppController::mcpCaptureFrame(double atSeconds, bool full)
{
    using namespace drift::mcp;
    setPlaying(false);

    const drift::TimeUs timeUs =
        atSeconds < 0.0 ? m_playheadUs : qMax<drift::TimeUs>(0, drift::secondsToUs(atSeconds));
    const auto snapshot = std::make_shared<const drift::Project>(m_project.detachedCopy());
    FrameCompositor::RenderOptions options;
    if (!full) {
        const int longEdge = qMax(snapshot->width(), snapshot->height());
        if (longEdge > 1280)
            options.previewScale = 1280.0 / double(longEdge);
    }

    auto frame = std::make_shared<QImage>();
    QEventLoop loop;
    (void)QtConcurrent::run([snapshot, timeUs, options, frame, &loop]() {
        FrameCompositor compositor;
        compositor.setProject(snapshot.get());
        *frame = compositor.compositeAt(timeUs, options);
        QMetaObject::invokeMethod(&loop, &QEventLoop::quit, Qt::QueuedConnection);
    });
    loop.exec();

    if (frame->isNull())
        return textResult(err("capture_failed", QStringLiteral("Compositor returned no frame")), true);

    const QJsonObject meta = ok({
        {QStringLiteral("at"), drift::usToSeconds(timeUs)},
        {QStringLiteral("w"), frame->width()},
        {QStringLiteral("h"), frame->height()},
        {QStringLiteral("full"), full},
    });

    if (full) {
        const QString outPath = newFreezeFramePath(m_project.id());
        if (outPath.isEmpty() || !frame->save(outPath, "PNG"))
            return textResult(err("capture_failed", QStringLiteral("Could not write PNG")), true);
        QJsonObject withPath = meta;
        withPath.insert(QStringLiteral("path"), outPath);
        return textResult(withPath);
    }

    QByteArray jpeg;
    QBuffer buffer(&jpeg);
    buffer.open(QIODevice::WriteOnly);
    if (!frame->save(&buffer, "JPEG", 80))
        return textResult(err("capture_failed", QStringLiteral("Could not encode JPEG")), true);

    QJsonArray content;
    content.append(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("text")},
        {QStringLiteral("text"), QString::fromUtf8(QJsonDocument(meta).toJson(QJsonDocument::Compact))},
    });
    content.append(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("image")},
        {QStringLiteral("mimeType"), QStringLiteral("image/jpeg")},
        {QStringLiteral("data"), QString::fromLatin1(jpeg.toBase64())},
    });
    return {{QStringLiteral("content"), content}, {QStringLiteral("isError"), false}};
}

void AppController::mcpBeginBatch()
{
    if (m_mcpBatchDepth++ == 0) {
        m_mcpBatchBefore = m_project.detachedCopy();
        m_mcpUndoSuspended = true;
    }
}

void AppController::mcpEndBatch(const QString &text, bool pushUndo)
{
    if (m_mcpBatchDepth <= 0)
        return;
    if (--m_mcpBatchDepth > 0)
        return;
    m_mcpUndoSuspended = false;
    if (m_previewDragActive)
        m_previewDragActive = false;
    if (pushUndo)
        pushProjectEdit(m_mcpBatchBefore, text);
    finishEdit(text);
    m_mcpBatchBefore = {};
}
