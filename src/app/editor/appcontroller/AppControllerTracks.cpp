#include "app/editor/appcontroller/AppControllerInternal.h"
#include "engine/decode/probe/MediaProbe.h"
#include "app/editor/sessions/MaskingController.h"
#include "app/mcp/McpController.h"
#include "app/jobs/vision/SegmentationController.h"
#include "app/editor/ProjectFileController.h"
#include "ui/preview/PreviewController.h"

#include "engine/decode/readers/ClipReaderPool.h"
#include "engine/packages/effects/EffectTemplateCatalog.h"
// Engine-internal by its own header comment, and included here only for releaseCaches(): the GPU
// caches have no other owner outside src/engine to ask. A one-line forwarder on GpuCompositor
// would restore the boundary.
#include "engine/encode/proxies/ReverseProxyCache.h"
#include "engine/render2d/vector/VectorClipRenderer.h"
#include "ui/timeline/ClipThumbnailStore.h"

#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>

#include <QSettings>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QDir>
using namespace drift::appdetail;
using drift::mcpdetail::findClipById;
using drift::mcpdetail::runBeatAnalysis;
using namespace drift::appcontroller_detail;

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

// Lives in the "shortcuts" group alongside the bindings themselves. Absent means 0.
const char *const kShortcutsVersionKey = "schemaVersion";

// Bump whenever a release moves a default onto a chord an older release used for
// something else. A stored binding is the user's and always wins — but one that is
// only in QSettings because it mirrored the default it replaced is not a choice, and
// leaving it there means the new layout never arrives. See loadShortcuts().
constexpr int kShortcutsSchemaVersion = 1;

// v0 → v1 (arrow-key jog, #203): stepBack/stepForward moved off Shift+Left/Right onto
// the bare arrows, and jumpBack/jumpForward took the chords they vacated. Everyone who
// had ever used "Reset shortcuts" — which writes every binding, not just changed ones —
// carried the old pair in QSettings, which left two actions claiming Shift+Left and
// nothing at all on Left/Right: exactly the dead arrow keys #203 was about.
const QHash<QString, QString> &supersededShortcutDefaults(int fromVersion)
{
    static const QHash<QString, QString> upToV0 = {
        {QStringLiteral("stepBack"), QStringLiteral("Shift+Left")},
        {QStringLiteral("stepForward"), QStringLiteral("Shift+Right")},
    };
    static const QHash<QString, QString> empty;
    return fromVersion < 1 ? upToV0 : empty;
}

QHash<QString, QString> defaultShortcuts()
{
    return {
        {QStringLiteral("newProject"), QStringLiteral("Ctrl+N")},
        {QStringLiteral("open"), QStringLiteral("Ctrl+O")},
        {QStringLiteral("save"), QStringLiteral("Ctrl+S")},
        // Not the usual Ctrl+Shift+S — separateAudio has held that since before Save As existed,
        // and moving a binding people already have in their fingers is the worse trade.
        {QStringLiteral("saveAs"), QStringLiteral("Ctrl+Alt+S")},
        {QStringLiteral("playPause"), QStringLiteral("Space")},
        {QStringLiteral("delete"), QStringLiteral("Delete")},
        // No default: Shift+Delete is ripple delete in many editors. Listed so it can be bound.
        {QStringLiteral("deleteWithEffects"), QString()},
        {QStringLiteral("undo"), QStringLiteral("Ctrl+Z")},
        {QStringLiteral("redo"), QStringLiteral("Ctrl+Shift+Z")},
        {QStringLiteral("clearSelection"), QStringLiteral("Escape")},
        {QStringLiteral("selectAll"), QStringLiteral("Ctrl+A")},
        {QStringLiteral("duplicate"), QStringLiteral("Ctrl+D")},
        // Premiere's Copy/Paste Attributes bindings, and clear of the clip clipboard on Ctrl+C/V.
        {QStringLiteral("copyEffects"), QStringLiteral("Ctrl+Alt+C")},
        {QStringLiteral("pasteEffects"), QStringLiteral("Ctrl+Shift+V")},
        {QStringLiteral("pasteAttributes"), QStringLiteral("Ctrl+Alt+V")},
        {QStringLiteral("split"), QStringLiteral("S")},
        {QStringLiteral("merge"), QStringLiteral("Ctrl+M")},
        {QStringLiteral("unlink"), QStringLiteral("Ctrl+Shift+U")},
        {QStringLiteral("link"), QStringLiteral("Ctrl+Shift+L")},
        {QStringLiteral("separateAudio"), QStringLiteral("Ctrl+Shift+S")},
        {QStringLiteral("copy"), QStringLiteral("Ctrl+C")},
        {QStringLiteral("cut"), QStringLiteral("Ctrl+X")},
        {QStringLiteral("paste"), QStringLiteral("Ctrl+V")},
        {QStringLiteral("nudgeLeft"), QStringLiteral("Alt+Left")},
        {QStringLiteral("nudgeRight"), QStringLiteral("Alt+Right")},
        // Cut-point navigation joins the nudge pair on Alt+arrow rather than taking the bare
        // Up/Down that Premiere uses. Arrow chords are dispatched from the editor FocusScope
        // after the focused item, so number-field spinners and list navigation still see
        // unmodified arrows; ApplicationShortcut would fire first and swallow them.
        {QStringLiteral("previousEdit"), QStringLiteral("Alt+Up")},
        {QStringLiteral("nextEdit"), QStringLiteral("Alt+Down")},
        // Timeline jog: one frame, one second, ten seconds. These used to live on
        // Shift+Left/Right as ApplicationShortcut, which never fired on several
        // Wayland compositors (notably Hyprland / Omarchy) and left the keys inert.
        {QStringLiteral("stepBack"), QStringLiteral("Left")},
        {QStringLiteral("stepForward"), QStringLiteral("Right")},
        {QStringLiteral("jumpBack"), QStringLiteral("Shift+Left")},
        {QStringLiteral("jumpForward"), QStringLiteral("Shift+Right")},
        {QStringLiteral("jumpBackFar"), QStringLiteral("Ctrl+Left")},
        {QStringLiteral("jumpForwardFar"), QStringLiteral("Ctrl+Right")},
        {QStringLiteral("goToStart"), QStringLiteral("Home")},
        {QStringLiteral("goToEnd"), QStringLiteral("End")},
        // Premiere's ripple-trim-to-playhead keys, which is exactly what these do.
        {QStringLiteral("deleteLeft"), QStringLiteral("Q")},
        {QStringLiteral("deleteRight"), QStringLiteral("W")},
        // Shuttle-style speed keys for reviewing long footage. Audio keeps its pitch at every
        // rate, so the material stays listenable while it runs fast.
        {QStringLiteral("speedUp"), QStringLiteral("L")},
        {QStringLiteral("speedDown"), QStringLiteral("J")},
        {QStringLiteral("toggleGuides"), QStringLiteral("G")},
        {QStringLiteral("toggleTrackHidden"), QStringLiteral("Shift+E")},
        // The 3D gizmo's tools. Blender's G/R/S and W/E/R are taken by guides, split and the
        // ripple deletes, so these sit on T(ranslate), R(otate) and E (next to R).
        {QStringLiteral("gizmoMove"), QStringLiteral("T")},
        {QStringLiteral("gizmoRotate"), QStringLiteral("R")},
        {QStringLiteral("gizmoScale"), QStringLiteral("E")},
        {QStringLiteral("gizmoOrientation"), QStringLiteral("Shift+T")},
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
        // Masking workspace tools. The masking panel claims these only while the pointer is over the
        // preview or it has focus or hover (never in 3D mode), so they stay free on the timeline.
        {QStringLiteral("maskingPolygonTool"), QStringLiteral("P")},
        {QStringLiteral("maskingAiTool"), QStringLiteral("A")},
        {QStringLiteral("maskingBrushSmaller"), QStringLiteral("[")},
        {QStringLiteral("maskingBrushLarger"), QStringLiteral("]")},
        {QStringLiteral("maskingBrushToggleErase"), QStringLiteral("X")},
        // Tracker runs. Ctrl+Shift+Arrow is free: Ctrl+Arrow is the far jump and Shift+Arrow the 1 s jump.
        {QStringLiteral("trackForward"), QStringLiteral("Ctrl+T")},
        {QStringLiteral("trackBackward"), QStringLiteral("Ctrl+Shift+T")},
        {QStringLiteral("trackFrameForward"), QStringLiteral("Ctrl+Shift+Right")},
        {QStringLiteral("trackFrameBackward"), QStringLiteral("Ctrl+Shift+Left")},
        // Timeline zoom is QML state too, so these take the same route as the tool modes.
        {QStringLiteral("zoomIn"), QStringLiteral("Ctrl+=")},
        {QStringLiteral("zoomOut"), QStringLiteral("Ctrl+-")},
        // The global search field lives in EditorHeader, so this is QML-only as well.
        {QStringLiteral("search"), QStringLiteral("Ctrl+K")},
        // QML owns the window, so triggerAction only raises the request — same shape as the
        // file actions above.
        {QStringLiteral("multicam"), QStringLiteral("Ctrl+Shift+C")},
        {QStringLiteral("transformTogether"), QStringLiteral("Ctrl+G")},
        {QStringLiteral("selectTransformLayer"), QStringLiteral("Shift+G")},
        {QStringLiteral("workspace.next"), QStringLiteral("Ctrl+Alt+Right")},
        {QStringLiteral("workspace.prev"), QStringLiteral("Ctrl+Alt+Left")},
        {QStringLiteral("workspace.1"), QStringLiteral("Alt+1")},
        {QStringLiteral("workspace.2"), QStringLiteral("Alt+2")},
        {QStringLiteral("workspace.3"), QStringLiteral("Alt+3")},
        {QStringLiteral("workspace.4"), QStringLiteral("Alt+4")},
        {QStringLiteral("workspace.5"), QStringLiteral("Alt+5")},
        {QStringLiteral("workspace.6"), QStringLiteral("Alt+6")},
        {QStringLiteral("workspace.7"), QStringLiteral("Alt+7")},
        {QStringLiteral("workspace.8"), QStringLiteral("Alt+8")},
        {QStringLiteral("workspace.9"), QStringLiteral("Alt+9")},
    };
}

} // namespace

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

bool AppController::renameTrack(int trackIndex, const QString &name)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return false;

    // Unlike renameAsset, an empty result is allowed through rather than refused — it clears
    // the custom name back to the type+position fallback ("Video 1"), which is a real,
    // reachable state (Track::name's own default) rather than an invalid one.
    const QString trimmed = name.trimmed();
    if (m_project.tracks()[trackIndex].name == trimmed)
        return false;

    const drift::Project before = m_project;
    m_project.tracks()[trackIndex].name = trimmed;
    pushProjectEdit(before, tr("Track renamed"));
    finishEdit(tr("Track renamed"));
    return true;
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

void AppController::setTrackSolo(int trackIndex, bool solo)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    if (m_project.tracks()[trackIndex].solo == solo)
        return;

    const drift::Project before = m_project;
    m_project.tracks()[trackIndex].solo = solo;
    pushProjectEdit(before, tr("Track solo"));
    finishEdit(solo ? tr("Track soloed") : tr("Track unsoloed"));
}

bool AppController::trackSolo(int trackIndex) const
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return false;
    return m_project.tracks().at(trackIndex).solo;
}

void AppController::setTrackLocked(int trackIndex, bool locked)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    if (m_project.tracks()[trackIndex].locked == locked)
        return;

    const drift::Project before = m_project;
    m_project.tracks()[trackIndex].locked = locked;
    pushProjectEdit(before, tr("Track lock"));
    finishEdit(locked ? tr("Track locked") : tr("Track unlocked"));
}

bool AppController::trackLocked(int trackIndex) const
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return false;
    return m_project.tracks().at(trackIndex).locked;
}

void AppController::previewTrackVolume(int trackIndex, double volume)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    volume = qMax(0.0, volume);
    if (!m_previewDragActive)
        beginPreviewDrag(tr("Track volume"));

    m_project.tracks()[trackIndex].volume = volume;
    emitPreviewFrame();
}

void AppController::setTrackVolume(int trackIndex, double volume)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    volume = qMax(0.0, volume);
    if (m_previewDragActive) {
        m_project.tracks()[trackIndex].volume = volume;
        commitPreviewDrag();
        return;
    }
    if (qFuzzyCompare(m_project.tracks()[trackIndex].volume, volume))
        return;

    const drift::Project before = m_project;
    m_project.tracks()[trackIndex].volume = volume;
    pushProjectEdit(before, tr("Track volume"));
    finishEdit(tr("Track volume changed"));
}

double AppController::trackVolume(int trackIndex) const
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return 1.0;
    return m_project.tracks().at(trackIndex).volume;
}

void AppController::previewTrackPan(int trackIndex, double pan)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    pan = std::clamp(pan, -1.0, 1.0);
    if (!m_previewDragActive)
        beginPreviewDrag(tr("Track pan"));

    m_project.tracks()[trackIndex].pan = pan;
    emitPreviewFrame();
}

void AppController::setTrackPan(int trackIndex, double pan)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    pan = std::clamp(pan, -1.0, 1.0);
    if (m_previewDragActive) {
        m_project.tracks()[trackIndex].pan = pan;
        commitPreviewDrag();
        return;
    }
    if (qFuzzyCompare(m_project.tracks()[trackIndex].pan, pan))
        return;

    const drift::Project before = m_project;
    m_project.tracks()[trackIndex].pan = pan;
    pushProjectEdit(before, tr("Track pan"));
    finishEdit(tr("Track pan changed"));
}

double AppController::trackPan(int trackIndex) const
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return 0.0;
    return m_project.tracks().at(trackIndex).pan;
}

QVariantMap AppController::trackAudioLevels(int trackIndex) const
{
    float left = 0.0f;
    float right = 0.0f;
    if (m_audioRecorder.isRecording() && m_audioRecorder.recordingTrackIndex() == trackIndex) {
        left = m_audioRecorder.audioLevel();
        right = left;
    } else if (m_playback.isPlaying()) {
        const auto levels = m_playback.trackAudioLevels(trackIndex);
        left = levels.first;
        right = levels.second;
    }
    return {
        {QStringLiteral("left"), left},
        {QStringLiteral("right"), right}
    };
}

QList<float> AppController::meterLevels(const QList<int> &trackIndexes) const
{
    QList<float> out = m_playback.takeMeterPeaks(trackIndexes);
    if (!m_audioRecorder.isRecording())
        return out;
    const float level = m_audioRecorder.audioLevel();
    if (!m_playback.isPlaying()) {
        out[0] = level;
        out[1] = level;
    }
    const int recordingTrack = m_audioRecorder.recordingTrackIndex();
    for (int i = 0; i < trackIndexes.size(); ++i) {
        if (trackIndexes.at(i) == recordingTrack) {
            out[2 + i * 2] = level;
            out[3 + i * 2] = level;
        }
    }
    return out;
}

QVariantMap AppController::masterAudioLevels() const
{
    float left = 0.0f;
    float right = 0.0f;
    if (m_playback.isPlaying()) {
        const auto levels = m_playback.masterAudioLevels();
        left = levels.first;
        right = levels.second;
    } else if (m_audioRecorder.isRecording()) {
        left = m_audioRecorder.audioLevel();
        right = left;
    }
    return {
        {QStringLiteral("left"), left},
        {QStringLiteral("right"), right}
    };
}

void AppController::setTrackClipDisplay(int trackIndex, int mode)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    const auto display = static_cast<drift::Track::ClipDisplay>(qBound(0, mode, 2));
    if (m_project.tracks()[trackIndex].clipDisplay == display)
        return;

    // View-only preference: mutate and refresh without an undo entry.
    m_project.tracks()[trackIndex].clipDisplay = display;
    notifyTracksChanged();
}

int AppController::trackClipDisplay(int trackIndex) const
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return static_cast<int>(drift::Track::ClipDisplay::Both);
    return static_cast<int>(m_project.tracks().at(trackIndex).clipDisplay);
}

void AppController::setTrackShowChannelWaveforms(int trackIndex, bool show)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    if (m_project.tracks()[trackIndex].showChannelWaveforms == show)
        return;

    // View-only preference: mutate and refresh without an undo entry.
    m_project.tracks()[trackIndex].showChannelWaveforms = show;
    notifyTracksChanged();
}

bool AppController::trackShowChannelWaveforms(int trackIndex) const
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return false;
    return m_project.tracks().at(trackIndex).showChannelWaveforms;
}

void AppController::setTrackHeightScale(int trackIndex, double scale)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    const qreal clamped = qBound(trackHeightScaleMin(), scale, trackHeightScaleMax());
    if (qFuzzyCompare(m_project.tracks()[trackIndex].heightScale, clamped))
        return;

    // View-only preference, like clipDisplay: no undo entry.
    m_project.tracks()[trackIndex].heightScale = clamped;
    notifyTracksChanged();
}

int AppController::trackRowHeight(int trackIndex, const QVariantMap &metrics) const
{
    const auto metric = [&metrics](const char *key, double fallback) {
        const QVariant value = metrics.value(QLatin1String(key));
        return value.isValid() ? value.toDouble() : fallback;
    };
    const double baseVideo = metric("video", 65.0);

    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return qRound(baseVideo);

    const drift::Track &track = m_project.tracks().at(trackIndex);
    // A lane is drawn inside its parent's row, so it takes no row of its own. Returning 0 rather
    // than filtering lanes out of the model is what keeps the flat (trackIndex, clipIndex)
    // addressing the rest of the app is built on.
    if (track.isAdjustmentLane())
        return 0;

    double base = baseVideo;
    switch (track.type) {
    case drift::TrackType::Audio:
        base = metric("audio", 50.0);
        break;
    case drift::TrackType::Text:
        base = metric("text", 25.0);
        break;
    case drift::TrackType::Subtitle:
        base = metric("subtitle", 25.0);
        break;
    case drift::TrackType::Shape:
        base = metric("shape", 50.0);
        break;
    case drift::TrackType::Adjustment:
        // Nothing to show but the effects it carries, so it gets a label's worth of height
        // rather than a video track's.
        base = metric("adjustment", 28.0);
        break;
    case drift::TrackType::Video:
        break;
    }

    const double scale = track.heightScale > 0 ? track.heightScale : 1.0;
    const double lanes =
        drift::adjustmentLaneIndexes(m_project, trackIndex).size() * metric("lane", 20.0);
    return qRound(qMax(20.0, base * scale + lanes));
}

int AppController::adjustmentLaneCount(int trackIndex) const
{
    return drift::adjustmentLaneIndexes(m_project, trackIndex).size();
}

QVariantList AppController::adjustmentLanes(int trackIndex) const
{
    QVariantList out;
    for (const int laneIndex : drift::adjustmentLaneIndexes(m_project, trackIndex))
        out.append(laneIndex);
    return out;
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

void AppController::nudgeAllTrackHeightScales(int steps)
{
    if (steps == 0)
        return;

    // Multiplicative, and per lane rather than one shared multiplier, so lanes that already differ
    // keep their proportions and a lane sitting on a limit simply stops while the others carry on.
    // A bigger step than the per-lane nudge: this is a button pressed a couple of times to reframe
    // the whole timeline, not a wheel rolled under a pointer.
    const qreal factor = std::pow(1.35, steps);
    bool changed = false;
    for (drift::Track &track : m_project.tracks()) {
        const qreal clamped = qBound(trackHeightScaleMin(), track.heightScale * factor,
                                     trackHeightScaleMax());
        if (qFuzzyCompare(track.heightScale, clamped))
            continue;
        track.heightScale = clamped;
        changed = true;
    }

    // View-only preference, like setTrackHeightScale: no undo entry.
    if (changed)
        notifyTracksChanged();
}

bool AppController::canGrowTrackHeights() const
{
    // One lane still off the ceiling is enough: the sweep clamps per lane, so it does something
    // even when the tallest lane is already there.
    for (const drift::Track &track : m_project.tracks()) {
        if (!qFuzzyCompare(track.heightScale, trackHeightScaleMax()))
            return true;
    }
    return false;
}

bool AppController::canShrinkTrackHeights() const
{
    for (const drift::Track &track : m_project.tracks()) {
        if (!qFuzzyCompare(track.heightScale, trackHeightScaleMin()))
            return true;
    }
    return false;
}

void AppController::moveTrack(int fromIndex, int toIndex)
{
    const int trackCount = m_project.tracks().size();
    if (fromIndex < 0 || fromIndex >= trackCount || toIndex < 0 || toIndex >= trackCount)
        return;
    if (fromIndex == toIndex)
        return;

    const drift::Project before = m_project;
    m_project.ensureTrackIds();

    // Captured by id, not remapped by index: normalizeAdjustmentLanes re-gathers each track's
    // nested lanes around it afterwards, so the destination index no longer describes where any
    // particular track ended up. Moving a track takes its lanes with it, which is the point.
    const QList<QPair<QString, int>> selection = captureSelectionByTrackId();
    const QString transitionTrackId = trackIdAt(m_selectedTransitionTrack);

    m_project.tracks().move(fromIndex, toIndex);
    normalizeAdjustmentLanes(m_project);

    restoreSelectionByTrackId(selection);
    m_selectedTransitionTrack = m_project.trackIndexById(transitionTrackId);

    pushProjectEdit(before, tr("Move track"));
    finishEdit(tr("Track moved"));
}

void AppController::removeTrack(int trackIndex)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    const drift::Project before = m_project;
    m_project.ensureTrackIds();

    const QList<QPair<QString, int>> selection = captureSelectionByTrackId();
    const QString transitionTrackId = trackIdAt(m_selectedTransitionTrack);

    // Nested lanes go with the track. They exist only to modify it, so leaving them behind would
    // strand effects with nothing to apply to — and ensureTrackIds() would then quietly promote
    // them to standalone adjustments affecting the whole canvas.
    const QString removedId = m_project.tracks().at(trackIndex).id;
    for (int i = m_project.tracks().size() - 1; i >= 0; --i) {
        const drift::Track &track = m_project.tracks().at(i);
        if (track.id == removedId
            || (track.isAdjustmentLane() && track.parentTrackId == removedId)) {
            m_project.tracks().removeAt(i);
        }
    }
    normalizeAdjustmentLanes(m_project);

    restoreSelectionByTrackId(selection);
    m_selectedTransitionTrack = m_project.trackIndexById(transitionTrackId);

    pushProjectEdit(before, tr("Delete track"));
    finishEdit(tr("Track deleted"));
}

QList<int> AppController::removableEmptyTrackIndices() const
{
    const QList<drift::Track> &tracks = m_project.tracks();
    QList<int> result;
    int topLevelCount = 0;
    for (int i = 0; i < tracks.size(); ++i) {
        const drift::Track &track = tracks.at(i);
        if (track.isAdjustmentLane())
            continue;
        ++topLevelCount;
        bool empty = track.clips.isEmpty();
        for (int j = 0; empty && j < tracks.size(); ++j) {
            const drift::Track &lane = tracks.at(j);
            if (lane.isAdjustmentLane() && lane.parentTrackId == track.id && !lane.clips.isEmpty())
                empty = false;
        }
        if (empty)
            result.append(i);
    }
    // The project always keeps a track; when everything is empty the lowest one stays.
    if (!result.isEmpty() && result.size() == topLevelCount)
        result.removeFirst();
    return result;
}

bool AppController::hasEmptyTracks() const
{
    return !removableEmptyTrackIndices().isEmpty();
}

int AppController::removeEmptyTracks()
{
    if (removableEmptyTrackIndices().isEmpty())
        return 0;

    const drift::Project before = m_project;
    m_project.ensureTrackIds();

    const QList<QPair<QString, int>> selection = captureSelectionByTrackId();
    const QString transitionTrackId = trackIdAt(m_selectedTransitionTrack);

    const QList<int> indices = removableEmptyTrackIndices();
    QSet<QString> removedIds;
    for (const int index : indices)
        removedIds.insert(m_project.tracks().at(index).id);
    for (int i = m_project.tracks().size() - 1; i >= 0; --i) {
        const drift::Track &track = m_project.tracks().at(i);
        if (removedIds.contains(track.id)
            || (track.isAdjustmentLane() && removedIds.contains(track.parentTrackId))) {
            m_project.tracks().removeAt(i);
        }
    }
    normalizeAdjustmentLanes(m_project);

    restoreSelectionByTrackId(selection);
    m_selectedTransitionTrack = m_project.trackIndexById(transitionTrackId);

    pushProjectEdit(before, tr("Delete empty tracks"));
    finishEdit(tr("Empty tracks deleted"));
    return indices.size();
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
    // Snapshot before binding the reference: bookmarks are a plain QList, so a later copy would
    // share the buffer the reference writes into.
    const drift::Project before = m_project;
    drift::Bookmark &bookmark = m_project.bookmarks()[index];
    if (bookmark.timeUs == timeUs && bookmark.label == resolvedLabel)
        return;

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

// Nearest cut point strictly after (direction > 0) or strictly before (direction < 0)
// `fromUs`, or `fromUs` itself when there is none. The candidates are every clip edge on
// every track plus both ends of the timeline — the same set snapTime() collects, so
// walking the cuts by keyboard stops exactly where a dragged clip would snap. Clips are
// not stored in time order, so this compares values rather than trusting indices.
drift::TimeUs adjacentEditPoint(const drift::Project &project, drift::TimeUs fromUs, int direction)
{
    drift::TimeUs best = fromUs;
    bool found = false;

    auto consider = [&](drift::TimeUs candidate) {
        if (direction > 0 ? candidate <= fromUs : candidate >= fromUs)
            return;
        if (!found || (direction > 0 ? candidate < best : candidate > best)) {
            best = candidate;
            found = true;
        }
    };

    consider(0);
    consider(project.durationUs());
    for (const drift::Track &track : project.tracks()) {
        for (const drift::Clip &clip : track.clips) {
            consider(clip.timelineStart);
            consider(clip.timelineEnd());
        }
    }
    return found ? best : fromUs;
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

void AppController::goToNextEdit()
{
    setPlayheadUs(adjacentEditPoint(m_project, m_playheadUs, 1));
}

void AppController::goToPreviousEdit()
{
    setPlayheadUs(adjacentEditPoint(m_project, m_playheadUs, -1));
}

void AppController::toggleBookmarkAtPlayhead()
{
    // The timeline's 150 ms snapping radius is much wider than a frame. Using it here
    // deletes a nearby, distinct bookmark when the user is trying to add another one.
    // Toggle only a bookmark on the same project frame as the playhead.
    const int fps = m_projectFile->projectFps();
    const qint64 playheadFrame = qRound64(drift::usToSeconds(m_playheadUs) * fps);
    const auto &marks = m_project.bookmarks();
    for (int i = 0; i < marks.size(); ++i) {
        if (qRound64(drift::usToSeconds(marks.at(i).timeUs) * fps) == playheadFrame) {
            removeBookmark(i);
            return;
        }
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

// Freeze frames are project media, not cache: they live under the project's own media dir so a
// save bundles them and a cache sweep can't delete the file a saved clip points at.
QString AppController::projectMediaDir(const QString &projectId)
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        return {};
    const QString dir = QDir(base).filePath(QStringLiteral("projects/%1/media").arg(projectId));
    return QDir().mkpath(dir) ? dir : QString();
}

QString AppController::newFreezeFramePath(const QString &projectId)
{
    const QString dir = projectMediaDir(projectId);
    if (dir.isEmpty())
        return {};

    return QDir(dir).filePath(QStringLiteral("freeze-%1.png")
                                  .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
}

void AppController::previewClipRange(int trackIndex, int clipIndex)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;
    const drift::Track &track = m_project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return;
    const drift::Clip &clip = track.clips.at(clipIndex);
    const drift::TimeUs startUs = clip.timelineStart;
    const drift::TimeUs endUs = clip.timelineStart + clip.timelineDuration;
    m_playback.setPreviewRange(startUs, endUs);
    if (m_playheadUs < startUs || m_playheadUs >= endUs)
        setPlayheadUs(startUs);
}

void AppController::clearPreviewRange()
{
    m_playback.clearPreviewRange();
}

QVariantMap AppController::clipTimelineRange(int track, int clip) const
{
    if (track < 0 || track >= m_project.tracks().size())
        return {};
    const drift::Track &t = m_project.tracks().at(track);
    if (clip < 0 || clip >= t.clips.size())
        return {};
    const drift::Clip &c = t.clips.at(clip);
    return {{QStringLiteral("startSeconds"), drift::usToSeconds(c.timelineStart)},
            {QStringLiteral("endSeconds"), drift::usToSeconds(c.timelineStart + c.timelineDuration)}};
}

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
                                                                    drift::imageClipDurationUs(), m_snapEnabled,
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
                freezeClip.timelineDuration = drift::imageClipDurationUs();
                freezeClip.srcIn = 0;
                freezeClip.srcOut = drift::imageClipDurationUs();
                fitClipLayoutToCanvas(freezeClip, size.width(), size.height(), m_project.width(),
                                      m_project.height());

                track.clips.append(freezeClip);
                const int newClipIndex = track.clips.size() - 1;
                pushProjectEdit(before, tr("Freeze frame added"));
                finishEdit(tr("Freeze frame added"));
                selectClip(trackIndex, newClipIndex);
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
        for (const drift::Transition &tr : m_project.tracks().at(pair.first).transitions) {
            if (tr.fromClipId == item.clip.id || tr.toClipId == item.clip.id)
                item.transitions.append(tr);
        }
        for (const drift::ClipRef &ref :
             drift::linkedMaskAdjustments(m_project, pair.first, pair.second)) {
            item.masks.append(m_project.tracks().at(ref.trackIndex).clips.at(ref.clipIndex).mask);
        }
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
    // By id: a later clip can insert a track above an earlier one, and normalizing the edit can
    // reorder tracks, so positions taken here go stale.
    QStringList inserted;
    // Pasted clips stay linked to each other under a fresh id, never to the clips they were
    // copied from; one pasted without any of its partners comes in unlinked.
    QHash<QString, int> linkCounts;
    for (const ClipboardItem &item : m_clipboard) {
        if (!item.clip.linkId.isEmpty())
            ++linkCounts[item.clip.linkId];
    }
    QHash<QString, QString> pastedLinkIds;
    // An extracted audio clip follows its video to the pasted copy; one pasted without it loses
    // the reference rather than pointing at the original.
    QHash<QString, QString> pastedClipIds;

    for (const ClipboardItem &item : m_clipboard) {
        // Composites live on the main timeline only, and never outlive their sequence.
        if (!item.clip.sequenceId.isEmpty()
            && (!m_project.activeSequenceId().isEmpty() || !m_project.hasSequence(item.clip.sequenceId)))
            continue;
        drift::Clip clip = item.clip;
        clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        pastedClipIds.insert(item.clip.id, clip.id);
        clip.timelineStart = qMax<drift::TimeUs>(0, clip.timelineStart + shift);
        if (linkCounts.value(clip.linkId) > 1) {
            auto it = pastedLinkIds.find(clip.linkId);
            if (it == pastedLinkIds.end())
                it = pastedLinkIds.insert(clip.linkId, newClipId());
            clip.linkId = *it;
        } else {
            clip.linkId.clear();
            clip.linkLoose = false;
        }

        int targetTrack = -1;
        for (int i = 0; i < m_project.tracks().size(); ++i) {
            if (m_project.tracks().at(i).type == item.trackType && m_project.tracks().at(i).acceptsClip(clip)) {
                targetTrack = i;
                break;
            }
        }
        if (targetTrack < 0 && clip.type == drift::ClipType::Adjustment
            && clip.adjustmentKind == drift::AdjustmentKind::Transform) {
            QString lowest;
            for (const drift::Track &t : m_project.tracks()) {
                if (drift::isTransformableTrack(t))
                    lowest = t.id;
            }
            targetTrack = drift::insertTransformTrack(m_project.tracks(), 0, lowest);
        }
        if (targetTrack < 0)
            targetTrack = drift::ensureTrackForClipType(m_project, clip.type, true);
        if (targetTrack < 0 || !m_project.tracks()[targetTrack].acceptsClip(clip))
            continue;
        m_project.tracks()[targetTrack].clips.append(clip);
        inserted.append(clip.id);
    }

    if (inserted.isEmpty())
        return;
    // Second pass: the audio can sit before its video in the clipboard, and a video that was
    // skipped above has no copy to point at.
    const QSet<QString> insertedIds(inserted.begin(), inserted.end());
    for (const QString &id : std::as_const(inserted)) {
        int trackIndex = -1;
        int clipIndex = -1;
        if (!findClipById(m_project, id, &trackIndex, &clipIndex))
            continue;
        QString &source = m_project.tracks()[trackIndex].clips[clipIndex].sourceVideoClipId;
        if (source.isEmpty())
            continue;
        const QString mapped = pastedClipIds.value(source);
        source = insertedIds.contains(mapped) ? mapped : QString();
    }
    pushProjectEdit(before, tr("Paste"));
    m_selection.clear();
    for (const QString &id : inserted) {
        int trackIndex = -1;
        int clipIndex = -1;
        if (findClipById(m_project, id, &trackIndex, &clipIndex))
            m_selection.append(qMakePair(trackIndex, clipIndex));
    }
    if (!m_selection.isEmpty()) {
        m_selectedTrack = m_selection.constLast().first;
        m_selectedClip = m_selection.constLast().second;
    }
    finishEdit(tr("Pasted %n clips", "", int(inserted.size())));
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

    drift::TimeUs minGroupStartUs = -1;
    for (const QPair<int, int> &pair : pairs) {
        if (isValidClipIndex(pair.first, pair.second)) {
            const drift::TimeUs s = m_project.tracks().at(pair.first).clips.at(pair.second).timelineStart;
            if (minGroupStartUs < 0 || s < minGroupStartUs)
                minGroupStartUs = s;
        }
    }
    drift::TimeUs clampedDeltaUs = deltaUs;
    if (clampedDeltaUs < 0 && minGroupStartUs >= 0 && -clampedDeltaUs > minGroupStartUs) {
        clampedDeltaUs = -minGroupStartUs;
    }

    QSet<QString> movedIds;
    for (const QPair<int, int> &pair : pairs) {
        if (!isValidClipIndex(pair.first, pair.second))
            continue;
        drift::Clip &clip = m_project.tracks()[pair.first].clips[pair.second];
        clip.timelineStart = qMax<drift::TimeUs>(0, clip.timelineStart + clampedDeltaUs);
        movedIds.insert(clip.id);
    }
    if (!m_allowClipOverlap) {
        drift::TimeUs maxPushRight = 0;
        for (const QPair<int, int> &pair : pairs) {
            if (!isValidClipIndex(pair.first, pair.second))
                continue;
            const drift::Track &track = m_project.tracks().at(pair.first);
            const drift::Clip &clip = track.clips.at(pair.second);
            const drift::TimeUs clampedStart = drift::clampClipStartNoOverlap(track, movedIds, clip.timelineStart,
                                                                              clip.timelineDuration);
            if (clampedStart > clip.timelineStart) {
                const drift::TimeUs push = clampedStart - clip.timelineStart;
                if (push > maxPushRight)
                    maxPushRight = push;
            }
        }
        if (maxPushRight > 0) {
            for (const QPair<int, int> &pair : pairs) {
                if (!isValidClipIndex(pair.first, pair.second))
                    continue;
                drift::Clip &clip = m_project.tracks()[pair.first].clips[pair.second];
                clip.timelineStart += maxPushRight;
            }
        }
    }
    if (editCreatesCompleteOverlap(before, m_project)) {
        m_project = before;
        setLastMessage(tr("A clip can't completely cover another"), QStringLiteral("warning"));
        return;
    }
    pushProjectEdit(before, tr("Nudge selection"));
    finishEdit(tr("Selection nudged"));
}

bool AppController::selectionContains(int trackIndex, int clipIndex) const
{
    return m_selection.contains(qMakePair(trackIndex, clipIndex));
}

double AppController::selectionEarliestStartSeconds() const
{
    drift::TimeUs minStart = -1;
    for (const QPair<int, int> &pair : m_selection) {
        if (!isValidClipIndex(pair.first, pair.second))
            continue;
        const drift::TimeUs s = m_project.tracks().at(pair.first).clips.at(pair.second).timelineStart;
        if (minStart < 0 || s < minStart)
            minStart = s;
    }
    return minStart >= 0 ? drift::usToSeconds(minStart) : 0.0;
}

QCursor AppController::trimCursorFor(int side, int heightPx) const
{
    const int h = qBound(18, heightPx, 160);
    const int key = side * 1000 + h;
    const auto it = m_trimCursorCache.constFind(key);
    if (it != m_trimCursorCache.constEnd())
        return *it;
    return *m_trimCursorCache.insert(key, timelineTrimCursor(side, h));
}

void AppController::setTimelineTrimCursor(int side, int heightPx, int owner)
{
    side = qBound(-1, side, 1);
    heightPx = qMax(0, heightPx);

    if (side == 0) {
        // Only whoever put the cursor up may take it down. Clip delegates sit edge to edge, so
        // crossing from one handle to its neighbour raises the new one before the old one clears
        // — and without this that stale clear would strip the cursor that had just been set.
        if (owner != 0 && m_timelineTrimCursorOwner != 0 && owner != m_timelineTrimCursorOwner)
            return;
        if (m_timelineTrimCursorSide != 0)
            QGuiApplication::restoreOverrideCursor();
        m_timelineTrimCursorSide = 0;
        m_timelineTrimCursorHeight = 0;
        m_timelineTrimCursorOwner = 0;
        return;
    }

    if (side == m_timelineTrimCursorSide && heightPx == m_timelineTrimCursorHeight
        && owner == m_timelineTrimCursorOwner) {
        return;
    }

    // The override stack stays exactly one deep: pop whatever is up before pushing.
    if (m_timelineTrimCursorSide != 0)
        QGuiApplication::restoreOverrideCursor();

    m_timelineTrimCursorSide = side;
    m_timelineTrimCursorHeight = heightPx;
    m_timelineTrimCursorOwner = owner;
    QGuiApplication::setOverrideCursor(trimCursorFor(side, heightPx > 0 ? heightPx : 28));
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

void AppController::loadShortcuts()
{
    m_shortcuts = defaultShortcuts();

    QSettings settings;
    settings.beginGroup(QStringLiteral("shortcuts"));
    const int version = settings.value(QLatin1String(kShortcutsVersionKey), 0).toInt();
    const QHash<QString, QString> &superseded = supersededShortcutDefaults(version);

    QHash<QString, QString> stored;
    for (auto it = m_shortcuts.cbegin(); it != m_shortcuts.cend(); ++it) {
        // An empty stored value falls through to the default rather than clearing the
        // binding — setShortcut persists "" for a cleared chord, but a cleared chord has
        // never survived a restart and making it do so is a separate change.
        const QString value = settings.value(it.key()).toString();
        if (value.isEmpty())
            continue;
        if (superseded.value(it.key()) == value) {
            settings.remove(it.key());
            continue;
        }
        stored.insert(it.key(), value);
    }
    if (version != kShortcutsSchemaVersion)
        settings.setValue(QLatin1String(kShortcutsVersionKey), kShortcutsSchemaVersion);
    settings.endGroup();

    QStringList storedIds = stored.keys();
    storedIds.sort();
    for (const QString &actionId : std::as_const(storedIds))
        m_shortcuts[actionId] = stored.value(actionId);

    QStringList defaultedIds;
    for (auto it = m_shortcuts.cbegin(); it != m_shortcuts.cend(); ++it) {
        if (!stored.contains(it.key()))
            defaultedIds.append(it.key());
    }
    defaultedIds.sort();

    // No chord may end up on two actions: actionForArrowChord and the Shortcut items in
    // Main.qml both resolve a chord to a single action, so with two claimants which one
    // answers comes down to QHash order. setShortcut refuses a duplicate, but nothing
    // re-checked the map after a release moved a default onto a chord an older one used
    // elsewhere. Stored ids go first so a binding the user chose always keeps its chord;
    // the loser is cleared in memory only, so nothing of theirs is overwritten, and the
    // sorted walk makes the same settings produce the same map on every launch.
    QSet<QString> taken;
    const auto claimChords = [&](const QStringList &actionIds) {
        for (const QString &actionId : actionIds) {
            const QString chord = m_shortcuts.value(actionId);
            if (chord.isEmpty())
                continue;
            if (taken.contains(chord))
                m_shortcuts[actionId].clear();
            else
                taken.insert(chord);
        }
    };
    claimChords(storedIds);
    claimChords(defaultedIds);
}

void AppController::resetShortcuts()
{
    m_shortcuts = defaultShortcuts();
    QSettings settings;
    settings.beginGroup(QStringLiteral("shortcuts"));
    for (auto it = m_shortcuts.cbegin(); it != m_shortcuts.cend(); ++it)
        settings.setValue(it.key(), it.value());
    settings.setValue(QLatin1String(kShortcutsVersionKey), kShortcutsSchemaVersion);
    settings.endGroup();
    emit shortcutsChanged();
}

QString AppController::shortcutChord(int key, int modifiers) const
{
    QString name;
    switch (key) {
    case Qt::Key_Escape:    name = QStringLiteral("Escape"); break;
    case Qt::Key_Backspace: name = QStringLiteral("Backspace"); break;
    case Qt::Key_Return:    name = QStringLiteral("Return"); break;
    case Qt::Key_Enter:     name = QStringLiteral("Enter"); break;
    case Qt::Key_Insert:    name = QStringLiteral("Insert"); break;
    case Qt::Key_Delete:    name = QStringLiteral("Delete"); break;
    case Qt::Key_Home:      name = QStringLiteral("Home"); break;
    case Qt::Key_End:       name = QStringLiteral("End"); break;
    case Qt::Key_Left:      name = QStringLiteral("Left"); break;
    case Qt::Key_Up:        name = QStringLiteral("Up"); break;
    case Qt::Key_Right:     name = QStringLiteral("Right"); break;
    case Qt::Key_Down:      name = QStringLiteral("Down"); break;
    case Qt::Key_PageUp:    name = QStringLiteral("PageUp"); break;
    case Qt::Key_PageDown:  name = QStringLiteral("PageDown"); break;
    case Qt::Key_Space:     name = QStringLiteral("Space"); break;
    case Qt::Key_Tab:       name = QStringLiteral("Tab"); break;
    case Qt::Key_Shift:
    case Qt::Key_Control:
    case Qt::Key_Alt:
    case Qt::Key_Meta:
        return {};
    default:
        if (key >= Qt::Key_F1 && key <= Qt::Key_F12)
            name = QStringLiteral("F%1").arg(key - Qt::Key_F1 + 1);
        else if (key >= Qt::Key_0 && key <= Qt::Key_9)
            name = QChar(QLatin1Char(char(key)));
        else if (key >= Qt::Key_A && key <= Qt::Key_Z)
            name = QChar(QLatin1Char(char(key)));
        break;
    }
    if (name.isEmpty())
        return {};

    QStringList parts;
    const auto mods = Qt::KeyboardModifiers(modifiers);
    if (mods & Qt::ControlModifier)
        parts << QStringLiteral("Ctrl");
    if (mods & Qt::AltModifier)
        parts << QStringLiteral("Alt");
    if (mods & Qt::ShiftModifier)
        parts << QStringLiteral("Shift");
    if (mods & Qt::MetaModifier)
        parts << QStringLiteral("Meta");
    parts << name;
    return parts.join(QLatin1Char('+'));
}

QString AppController::actionForArrowChord(int key, int modifiers) const
{
    if (key != Qt::Key_Left && key != Qt::Key_Right && key != Qt::Key_Up && key != Qt::Key_Down)
        return {};

    const QString chord = shortcutChord(key, modifiers);
    if (chord.isEmpty())
        return {};

    // loadShortcuts() and setShortcut() both keep the map free of duplicate chords,
    // so the first match is the only match and QHash order cannot decide the outcome.
    for (auto it = m_shortcuts.cbegin(); it != m_shortcuts.cend(); ++it) {
        if (it.value() == chord)
            return it.key();
    }
    return {};
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
        emit m_projectFile->newProjectRequested();
    else if (actionId == QStringLiteral("open"))
        emit m_projectFile->openRequested();
    else if (actionId == QStringLiteral("save"))
        emit m_projectFile->saveRequested();
    else if (actionId == QStringLiteral("saveAs"))
        emit m_projectFile->saveAsRequested();
    else if (actionId == QStringLiteral("playPause"))
        togglePlayback();
    else if (actionId == QStringLiteral("multicam"))
        emit openMulticamWindowRequested();
    else if (actionId == QStringLiteral("delete"))
        deleteSelectedClip();
    else if (actionId == QStringLiteral("deleteWithEffects"))
        deleteSelectedClipWithEffects();
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
    else if (actionId == QStringLiteral("transformTogether"))
        addTransformLayerForSelection();
    else if (actionId == QStringLiteral("selectTransformLayer"))
        selectTransformParent();
    else if (actionId == QStringLiteral("copyEffects"))
        copyClipEffectsToClipboard(m_selectedTrack, m_selectedClip);
    else if (actionId == QStringLiteral("pasteEffects"))
        pasteEffectsFromClipboard(m_selectedTrack, m_selectedClip);
    else if (actionId == QStringLiteral("pasteAttributes"))
        requestPasteAttributes();
    else if (actionId == QStringLiteral("split"))
        splitAtPlayhead();
    else if (actionId == QStringLiteral("merge"))
        mergeSelectedClips();
    else if (actionId == QStringLiteral("separateAudio"))
        separateAudioFromSelection();
    else if (actionId == QStringLiteral("unlink"))
        unlinkSelectedClips();
    else if (actionId == QStringLiteral("link"))
        linkSelectedClips();
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
        m_preview->setGuidesEnabled(!m_preview->guidesEnabled());
    else if (actionId == QStringLiteral("toggleTrackHidden"))
        setTrackHidden(m_selectedTrack, !trackHidden(m_selectedTrack));
    else if (actionId == QStringLiteral("gizmoMove"))
        m_preview->setGizmoTool(QStringLiteral("move"));
    else if (actionId == QStringLiteral("gizmoRotate"))
        m_preview->setGizmoTool(QStringLiteral("rotate"));
    else if (actionId == QStringLiteral("gizmoScale"))
        m_preview->setGizmoTool(QStringLiteral("scale"));
    else if (actionId == QStringLiteral("gizmoOrientation"))
        m_preview->setGizmoOrientation(m_preview->gizmoOrientation() == QLatin1String("world")   ? QStringLiteral("local")
                                       : m_preview->gizmoOrientation() == QLatin1String("local") ? QStringLiteral("view")
                                                                                                 : QStringLiteral("world"));
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
    else if (actionId == QStringLiteral("previousEdit"))
        goToPreviousEdit();
    else if (actionId == QStringLiteral("nextEdit"))
        goToNextEdit();
    else if (actionId == QStringLiteral("stepBack"))
        stepFrames(-1);
    else if (actionId == QStringLiteral("stepForward"))
        stepFrames(1);
    else if (actionId == QStringLiteral("jumpBack"))
        jumpSeconds(-1.0);
    else if (actionId == QStringLiteral("jumpForward"))
        jumpSeconds(1.0);
    else if (actionId == QStringLiteral("jumpBackFar"))
        jumpSeconds(-10.0);
    else if (actionId == QStringLiteral("jumpForwardFar"))
        jumpSeconds(10.0);
    else if (actionId == QStringLiteral("goToStart"))
        setPlayheadUs(0);
    else if (actionId == QStringLiteral("goToEnd"))
        setPlayheadUs(m_project.durationUs());
    else if (actionId == QStringLiteral("deleteLeft"))
        splitSelectedClipLeft();
    else if (actionId == QStringLiteral("deleteRight"))
        splitSelectedClipRight();
    else if (actionId == QStringLiteral("speedUp")) {
        m_playback.stepPlaybackRate(1);
        // Reviewing long footage is the whole point of the key, so from a stopped transport
        // the first press starts rolling rather than only arming a rate for later. This has
        // to follow the step: setPlaybackRate restarts the transport itself when it was
        // already playing, and would otherwise undo the resume.
        setPlaying(true);
    } else if (actionId == QStringLiteral("speedDown"))
        m_playback.stepPlaybackRate(-1);
}

void AppController::undo()
{
    // A stroke still being written would push its edit after the undo and clear the redo stack.
    if (m_masking)
        m_masking->waitForBrushSaves(2000);
    if (!m_undoStack.canUndo())
        return;
    captureSelectionClipIds();
    m_undoStack.undo();
    m_undoSelectionCaptured = false;
    m_mcpController->bumpEditRevision();
}

QStringList AppController::undoHistory() const
{
    QStringList texts;
    for (int i = 0; i < m_undoStack.count(); ++i)
        texts.append(m_undoStack.text(i));
    return texts;
}

void AppController::setUndoIndex(int index)
{
    index = qBound(0, index, m_undoStack.count());
    while (m_undoStack.index() > index && m_undoStack.canUndo())
        undo();
    while (m_undoStack.index() < index && m_undoStack.canRedo())
        redo();
}

void AppController::redo()
{
    if (m_masking)
        m_masking->waitForBrushSaves(2000);
    if (!m_undoStack.canRedo())
        return;
    captureSelectionClipIds();
    m_undoStack.redo();
    m_undoSelectionCaptured = false;
    m_mcpController->bumpEditRevision();
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
                    self->m_waveformPending.remove(path);
                    // Decoded from the file this path held before it changed; the next ask
                    // decodes it again.
                    if (!self->m_waveformDiscard.remove(path))
                        self->m_waveformCache.insert(path, dense);
                    emit self->waveformReady(path);
                },
                Qt::QueuedConnection);
        });
    }

    return nullptr;
}

void AppController::forgetChangedMediaSource(const QString &path, bool beforeReprobe)
{
    if (path.isEmpty())
        return;
    // Skips a decoder that is mid-read; the second call, after the re-probe, catches it.
    ClipReaderPool::instance().releasePath(path);
    m_waveformBlocks.forgetSource(path);
    m_filmstripTiles.forgetSource(path);
    m_waveformCache.remove(path);
    if (m_waveformPending.contains(path))
        m_waveformDiscard.insert(path);
    // Cached files are named by path, so the regenerated ones keep their names.
    ClipThumbnailStore::instance().forgetPrefix(MediaThumbnail::cachePrefixFor(path));
    if (!beforeReprobe)
        return;
    // These would hand the re-probe the old pictures back, so they go before it runs.
    MediaThumbnail::forgetSource(path);
    drift::ReverseProxyCache::instance().removePreview(path);
    // Keyed by the content hash recorded at the last probe, which a clip still carries; all or
    // nothing, and cheap to rebuild — parsed documents only.
    drift::vec::clearVectorDocumentCache();
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
                                               double durSeconds, int buckets, int audioStreamIndex) const
{
    if (path.isEmpty() || durSeconds <= 0.0 || buckets <= 0)
        return {};

    // Block-backed: only the source span the clip's visible pixels cover is ever decoded, so
    // a three-hour file costs a viewport's worth of audio instead of the whole timeline.
    const int outCount = qBound(1, buckets, 4096);
    const QVector<float> span = m_waveformBlocks.range(path, startSeconds, durSeconds, outCount, audioStreamIndex);
    if (span.isEmpty())
        return {};

    return reduceDensePeaks(span, 0, span.size(), span.size());
}

QVariantMap AppController::waveformChannelPeaksRange(const QString &path, double startSeconds,
                                                     double durSeconds, int buckets,
                                                     int audioStreamIndex) const
{
    QVariantMap result;
    result.insert(QStringLiteral("channels"), 0);
    result.insert(QStringLiteral("buckets"), 0);
    if (path.isEmpty() || durSeconds <= 0.0 || buckets <= 0)
        return result;

    const int channels = m_waveformBlocks.channelCount(path, audioStreamIndex);
    if (channels <= 0) {
        // Nothing decoded yet. Ask for the merged envelope anyway so the block gets queued and
        // rangeReady() brings the caller back once the layout is known.
        m_waveformBlocks.range(path, startSeconds, durSeconds, qBound(1, buckets, 4096),
                               audioStreamIndex);
        return result;
    }

    const int outCount = qBound(1, buckets, 4096);
    QVariantList peaks;
    peaks.reserve(channels * outCount);
    int decodedBuckets = -1;
    for (int c = 0; c < channels; ++c) {
        const QVector<float> span =
            m_waveformBlocks.range(path, startSeconds, durSeconds, outCount, audioStreamIndex, c);
        // Same 48 dB display curve and visibility floor as the merged lane, deliberately not
        // renormalized per channel: a silent surround has to read as quieter than the dialogue
        // rather than being stretched to look like it.
        const QVariantList reduced = reduceDensePeaks(span, 0, span.size(), span.size());
        // The caller indexes this as peaks[c * buckets + b], which is only meaningful if every
        // channel came back the same length. They always do — range() returns exactly outCount
        // or nothing — but a ragged result would be read as garbage rather than as missing data.
        if (decodedBuckets >= 0 && reduced.size() != decodedBuckets)
            return result;
        decodedBuckets = reduced.size();
        peaks.append(reduced);
    }

    if (decodedBuckets <= 0)
        return result;

    result.insert(QStringLiteral("channels"), channels);
    result.insert(QStringLiteral("buckets"), decodedBuckets);
    result.insert(QStringLiteral("names"), m_waveformBlocks.channelNames(path, audioStreamIndex));
    result.insert(QStringLiteral("peaks"), peaks);
    return result;
}

QVector<float> AppController::waveformDisplayPeaks(const QString &path, double startSeconds,
                                                   double durSeconds, int buckets,
                                                   int audioStreamIndex, int channel) const
{
    QVector<float> out;
    if (path.isEmpty() || durSeconds <= 0.0 || buckets <= 0)
        return out;
    const QVector<float> span = m_waveformBlocks.range(path, startSeconds, durSeconds,
                                                       qBound(1, buckets, 4096), audioStreamIndex,
                                                       channel);
    out.reserve(span.size());
    for (float peak : span)
        out.append(peak < 0.0f ? 0.0f : float(qMax(0.05, waveformDisplayLevel(peak))));
    return out;
}

QStringList AppController::waveformChannelNames(const QString &path, int audioStreamIndex) const
{
    return m_waveformBlocks.channelNames(path, audioStreamIndex);
}

QString AppController::filmstripTilePath(const QString &path, int level, qint64 index,
                                         int rotationCorrection) const
{
    if (path.isEmpty())
        return {};
    return m_filmstripTiles.tile(path, level, index, rotationCorrection);
}

int AppController::waveformChannelCount(const QString &path, int audioStreamIndex) const
{
    if (path.isEmpty())
        return 0;
    return m_waveformBlocks.channelCount(path, audioStreamIndex);
}

int AppController::trackMaxChannelCount(int trackIndex) const
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return 0;

    int widest = 0;
    for (const drift::Clip &clip : m_project.tracks().at(trackIndex).clips) {
        if (clip.path.isEmpty())
            continue;
        // From the container's own metadata, not from the block cache: the cache only learns the
        // layout once a block has decoded, which is after the header has already been built and
        // is signalled by waveformRangeReady rather than tracksChanged — so a control bound to
        // this would stay hidden until some unrelated edit happened to re-evaluate it. The probe
        // is memoized, so asking per clip costs one container open per distinct file.
        const QList<StreamInfo> streams = cachedAudioStreams(clip.path);
        if (clip.audioStreamIndex >= 0 && clip.audioStreamIndex < streams.size())
            widest = qMax(widest, streams.at(clip.audioStreamIndex).channels);
    }
    return widest;
}

QList<float> AppController::subtitleWaveformColumns(double startSeconds, double durSeconds,
                                                    int sampleCount, int columns) const
{
    const QVariantList peaks = subtitleWaveformPeaks(startSeconds, durSeconds, sampleCount);
    const int n = static_cast<int>(peaks.size());
    if (n == 0 || columns <= 0)
        return {};
    QList<float> out;
    out.reserve(columns);
    for (int x = 0; x < columns; ++x) {
        const int i0 = static_cast<int>(static_cast<qint64>(x) * n / columns);
        int i1 = static_cast<int>(static_cast<qint64>(x + 1) * n / columns);
        if (i1 <= i0)
            i1 = qMin(n, i0 + 1);
        float peak = 0.0f;
        for (int i = i0; i < i1; ++i)
            peak = qMax(peak, peaks.at(i).toFloat());
        out.append(peak);
    }
    return out;
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
            const QString row = QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|%9|%10")
                                    .arg(clip.assetId)
                                    .arg(clip.timelineStart)
                                    .arg(clip.timelineDuration)
                                    .arg(clip.srcIn)
                                    .arg(clip.srcOut)
                                    .arg(clip.speed)
                                    .arg(clip.reverse ? 1 : 0)
                                    .arg(clip.suppressEmbeddedAudio ? 1 : 0)
                                    .arg(clip.audioEffects.size())
                                    .arg(clip.pan);
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
        const AudioBeatAnalysis analysis = runBeatAnalysis(snap, startUs, durUs, startSeconds);
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
        const auto next = std::lower_bound(analysis.beats.cbegin(), analysis.beats.cend(),
                                           o.seconds - 0.06);
        const bool nearBeat = next != analysis.beats.cend() && std::abs(*next - o.seconds) < 0.06;
        onsets.append(QVariantMap{{QStringLiteral("seconds"), o.seconds},
                                  {QStringLiteral("strength"), o.strength},
                                  {QStringLiteral("nearBeat"), nearBeat}});
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
                const bool needsSegment =
                    (entry->requiresSegmentation || entry->usesMultiTrack())
                    && !clipHasMatte(m_project, pending.trackIndex, pending.clipIndex);
                if (needsSegment) {
                    if (m_segmentation->available() && !m_segmentation->running())
                        m_segmentation->openSegmentationForTemplate(pending.trackIndex, pending.clipIndex);
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

void AppController::captureSelectionClipIds()
{
    m_undoSelectionIds.clear();
    for (const QPair<int, int> &pair : std::as_const(m_selection)) {
        if (isValidClipIndex(pair.first, pair.second))
            m_undoSelectionIds.append(m_project.tracks().at(pair.first).clips.at(pair.second).id);
    }
    m_undoPrimaryId = isValidClipIndex(m_selectedTrack, m_selectedClip)
        ? m_project.tracks().at(m_selectedTrack).clips.at(m_selectedClip).id
        : QString();
    const bool transition = isValidClipIndex(m_selectedTransitionTrack, m_selectedTransitionLeftClip);
    m_undoTransitionLeftClipId = transition
        ? m_project.tracks().at(m_selectedTransitionTrack).clips.at(m_selectedTransitionLeftClip).id
        : QString();
    m_undoSelectionCaptured = true;
}

void AppController::restoreSelectionClipIds()
{
    if (!m_undoSelectionCaptured)
        return;
    QList<QPair<int, int>> restored;
    for (const QString &id : std::as_const(m_undoSelectionIds)) {
        int t = -1;
        int c = -1;
        if (findClipById(m_project, id, &t, &c))
            restored.append(qMakePair(t, c));
    }
    int t = -1;
    int c = -1;
    const bool primaryFound = !m_undoPrimaryId.isEmpty() && findClipById(m_project, m_undoPrimaryId, &t, &c);
    if (restored.isEmpty() && primaryFound)
        restored.append(qMakePair(t, c));
    // Nothing selected survives: clear it. Keeping the old indices would leave them pointing at
    // whatever clip now sits there -- a neighbour's grade, say, after undoing a removal before it.
    m_selection = restored;
    if (primaryFound) {
        m_selectedTrack = t;
        m_selectedClip = c;
    } else if (!restored.isEmpty()) {
        m_selectedTrack = restored.constLast().first;
        m_selectedClip = restored.constLast().second;
    } else {
        m_selectedTrack = -1;
        m_selectedClip = -1;
    }

    // Before normalizeSelection, which would otherwise judge the transition by stale indices.
    if (!m_undoTransitionLeftClipId.isEmpty()) {
        int track = -1;
        int left = -1;
        if (!findClipById(m_project, m_undoTransitionLeftClipId, &track, &left))
            track = left = -1;
        m_selectedTransitionTrack = track;
        m_selectedTransitionLeftClip = left;
    }
}

void AppController::normalizeSelection()
{
    const QList<QPair<int, int>> selection = m_selection;
    QList<QPair<int, int>> kept;
    kept.reserve(selection.size());
    for (const QPair<int, int> &pair : selection) {
        if (isValidClipIndex(pair.first, pair.second) && !m_project.tracks().at(pair.first).locked
            && !kept.contains(pair))
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
