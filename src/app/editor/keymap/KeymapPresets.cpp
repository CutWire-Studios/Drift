#include "app/editor/keymap/KeymapPresets.h"
#include "app/editor/keymap/KeymapCatalog.h"

#include <QCoreApplication>

namespace drift::keymap {

namespace {

QHash<QString, QStringList> driftChords()
{
    return {
        {QStringLiteral("newProject"), {QStringLiteral("Ctrl+N")}},
        {QStringLiteral("open"), {QStringLiteral("Ctrl+O")}},
        {QStringLiteral("save"), {QStringLiteral("Ctrl+S")}},
        // Not the usual Ctrl+Shift+S — separateAudio has held that since before Save As existed,
        // and moving a binding people already have in their fingers is the worse trade.
        {QStringLiteral("saveAs"), {QStringLiteral("Ctrl+Alt+S")}},
        {QStringLiteral("playPause"), {QStringLiteral("Space")}},
        {QStringLiteral("delete"), {QStringLiteral("Delete")}},
        // No default: Shift+Delete is ripple delete in many editors. Listed so it can be bound.
        {QStringLiteral("deleteWithEffects"), {}},
        // No default: Shift+E is toggleTrackHidden. Listed so it can be bound.
        {QStringLiteral("toggleClipEnabled"), {}},
        {QStringLiteral("undo"), {QStringLiteral("Ctrl+Z")}},
        {QStringLiteral("redo"), {QStringLiteral("Ctrl+Shift+Z")}},
        {QStringLiteral("clearSelection"), {QStringLiteral("Escape")}},
        {QStringLiteral("selectAll"), {QStringLiteral("Ctrl+A")}},
        {QStringLiteral("duplicate"), {QStringLiteral("Ctrl+D")}},
        // Premiere's Copy/Paste Attributes bindings, and clear of the clip clipboard on Ctrl+C/V.
        {QStringLiteral("copyEffects"), {QStringLiteral("Ctrl+Alt+C")}},
        {QStringLiteral("pasteEffects"), {QStringLiteral("Ctrl+Shift+V")}},
        {QStringLiteral("pasteAttributes"), {QStringLiteral("Ctrl+Alt+V")}},
        {QStringLiteral("addTransitionAtPlayhead"), {QStringLiteral("Ctrl+Shift+D")}},
        // S splits the selection when there is one and every track when there is not, so the
        // common case needs no modifier either way. Shift+S is the explicit selection-only cut,
        // and splitAllTracks is the explicit all-tracks one — unbound, since S already is it
        // whenever nothing is selected.
        {QStringLiteral("split"), {QStringLiteral("S")}},
        {QStringLiteral("splitSelection"), {QStringLiteral("Shift+S")}},
        {QStringLiteral("splitAllTracks"), {}},
        {QStringLiteral("merge"), {QStringLiteral("Ctrl+M")}},
        {QStringLiteral("unlink"), {QStringLiteral("Ctrl+Shift+U")}},
        {QStringLiteral("link"), {QStringLiteral("Ctrl+Shift+L")}},
        {QStringLiteral("separateAudio"), {QStringLiteral("Ctrl+Shift+S")}},
        {QStringLiteral("copy"), {QStringLiteral("Ctrl+C")}},
        {QStringLiteral("cut"), {QStringLiteral("Ctrl+X")}},
        {QStringLiteral("paste"), {QStringLiteral("Ctrl+V")}},
        {QStringLiteral("nudgeLeft"), {QStringLiteral("Alt+Left")}},
        {QStringLiteral("nudgeRight"), {QStringLiteral("Alt+Right")}},
        // Cut-point navigation joins the nudge pair on Alt+arrow rather than taking the bare
        // Up/Down that Premiere uses. Arrow chords are dispatched from the editor FocusScope
        // after the focused item, so number-field spinners and list navigation still see
        // unmodified arrows; ApplicationShortcut would fire first and swallow them.
        {QStringLiteral("previousEdit"), {QStringLiteral("Alt+Up")}},
        {QStringLiteral("nextEdit"), {QStringLiteral("Alt+Down")}},
        // Timeline jog: one frame, one second, ten seconds. These used to live on
        // Shift+Left/Right as ApplicationShortcut, which never fired on several
        // Wayland compositors (notably Hyprland / Omarchy) and left the keys inert.
        {QStringLiteral("stepBack"), {QStringLiteral("Left")}},
        {QStringLiteral("stepForward"), {QStringLiteral("Right")}},
        {QStringLiteral("jumpBack"), {QStringLiteral("Shift+Left")}},
        {QStringLiteral("jumpForward"), {QStringLiteral("Shift+Right")}},
        {QStringLiteral("jumpBackFar"), {QStringLiteral("Ctrl+Left")}},
        {QStringLiteral("jumpForwardFar"), {QStringLiteral("Ctrl+Right")}},
        {QStringLiteral("goToStart"), {QStringLiteral("Home")}},
        {QStringLiteral("goToEnd"), {QStringLiteral("End")}},
        // Premiere's ripple-trim-to-playhead keys, which is exactly what these do.
        {QStringLiteral("deleteLeft"), {QStringLiteral("Q")}},
        {QStringLiteral("deleteRight"), {QStringLiteral("W")}},
        // Shuttle-style speed keys for reviewing long footage. Audio keeps its pitch at every
        // rate, so the material stays listenable while it runs fast.
        {QStringLiteral("speedUp"), {QStringLiteral("L")}},
        {QStringLiteral("speedDown"), {QStringLiteral("J")}},
        {QStringLiteral("toggleGuides"), {QStringLiteral("G")}},
        {QStringLiteral("toggleTrackHidden"), {QStringLiteral("Shift+E")}},
        {QStringLiteral("addSpeedPoint"), {QStringLiteral("Shift+K")}},
        {QStringLiteral("toggleBookmark"), {QStringLiteral("M")}},
        {QStringLiteral("nextBookmark"), {QStringLiteral("Shift+M")}},
        {QStringLiteral("previousBookmark"), {QStringLiteral("Ctrl+Shift+M")}},
        {QStringLiteral("markIn"), {QStringLiteral("I")}},
        {QStringLiteral("markOut"), {QStringLiteral("O")}},
        {QStringLiteral("goToIn"), {QStringLiteral("Shift+I")}},
        {QStringLiteral("goToOut"), {QStringLiteral("Shift+O")}},
        {QStringLiteral("clearInOut"), {QStringLiteral("Alt+X")}},
        {QStringLiteral("toggleLoop"), {QStringLiteral("Ctrl+L")}},
        // The timeline tool modes live in QML, so Main.qml intercepts these rather
        // than triggerAction dispatching them. They are registered all the same, so
        // they show up in the shortcut list and can be rebound — hardcoded in
        // TimelinePanel they were neither.
        {QStringLiteral("selectTool"), {QStringLiteral("V")}},
        {QStringLiteral("bladeTool"), {QStringLiteral("B")}},
        // Timeline zoom is QML state too, so these take the same route as the tool modes.
        {QStringLiteral("zoomIn"), {QStringLiteral("Ctrl+=")}},
        {QStringLiteral("zoomOut"), {QStringLiteral("Ctrl+-")}},
        // The global search field lives in EditorHeader, so this is QML-only as well.
        {QStringLiteral("search"), {QStringLiteral("Ctrl+K")}},
        {QStringLiteral("askCommand"), {QStringLiteral("Ctrl+Shift+K")}},
        // QML owns the window, so triggerAction only raises the request — same shape as the
        // file actions above.
        {QStringLiteral("multicam"), {QStringLiteral("Ctrl+Shift+C")}},
        {QStringLiteral("transformTogether"), {QStringLiteral("Ctrl+G")}},
        {QStringLiteral("selectTransformLayer"), {QStringLiteral("Shift+G")}},
        {QStringLiteral("workspace.restoreWindows"), {}},
        {QStringLiteral("workspace.next"), {QStringLiteral("Ctrl+Alt+Right")}},
        {QStringLiteral("workspace.prev"), {QStringLiteral("Ctrl+Alt+Left")}},
        {QStringLiteral("workspace.1"), {QStringLiteral("Alt+1")}},
        {QStringLiteral("workspace.2"), {QStringLiteral("Alt+2")}},
        {QStringLiteral("workspace.3"), {QStringLiteral("Alt+3")}},
        {QStringLiteral("workspace.4"), {QStringLiteral("Alt+4")}},
        {QStringLiteral("workspace.5"), {QStringLiteral("Alt+5")}},
        {QStringLiteral("workspace.6"), {QStringLiteral("Alt+6")}},
        {QStringLiteral("workspace.7"), {QStringLiteral("Alt+7")}},
        {QStringLiteral("workspace.8"), {QStringLiteral("Alt+8")}},
        {QStringLiteral("workspace.9"), {QStringLiteral("Alt+9")}},
        // Panel scopes. Every chord below is only live while its panel is engaged (focused, hovered
        // or showing), so a panel may reuse a global chord or another panel's.
        // Keyframes panel (focused).
        {QStringLiteral("keyframes.addKey"), {QStringLiteral("K")}},
        {QStringLiteral("keyframes.fitSelection"), {QStringLiteral("F")}},
        {QStringLiteral("keyframes.fitAll"), {QStringLiteral("Home")}},
        {QStringLiteral("keyframes.prevKey"), {QStringLiteral("Alt+Left")}},
        {QStringLiteral("keyframes.nextKey"), {QStringLiteral("Alt+Right")}},
        {QStringLiteral("keyframes.nudgeLeft"), {QStringLiteral("Left")}},
        {QStringLiteral("keyframes.nudgeRight"), {QStringLiteral("Right")}},
        {QStringLiteral("keyframes.nudgeLeftFar"), {QStringLiteral("Shift+Left")}},
        {QStringLiteral("keyframes.nudgeRightFar"), {QStringLiteral("Shift+Right")}},
        // Masking tools. The panel claims these only while the pointer is over the preview or it has
        // focus or hover, so they stay free on the timeline.
        {QStringLiteral("masking.selectTool"), {QStringLiteral("V")}},
        {QStringLiteral("masking.rectTool"), {QStringLiteral("R")}},
        {QStringLiteral("masking.ellipseTool"), {QStringLiteral("E")}},
        {QStringLiteral("masking.polygonTool"), {QStringLiteral("P")}},
        {QStringLiteral("masking.brushTool"), {QStringLiteral("B")}},
        {QStringLiteral("masking.lassoTool"), {QStringLiteral("F")}},
        {QStringLiteral("masking.scissorsTool"), {QStringLiteral("C")}},
        {QStringLiteral("masking.wandTool"), {QStringLiteral("U")}},
        {QStringLiteral("masking.aiTool"), {QStringLiteral("A")}},
        {QStringLiteral("masking.brushSmaller"), {QStringLiteral("[")}},
        {QStringLiteral("masking.brushLarger"), {QStringLiteral("]")}},
        {QStringLiteral("masking.brushToggleErase"), {QStringLiteral("X")}},
        // Tracker runs. Ctrl+Shift+Arrow is free: Ctrl+Arrow is the far jump and Shift+Arrow the 1 s jump.
        {QStringLiteral("tracking.trackerTool"), {QStringLiteral("T")}},
        {QStringLiteral("tracking.trackForward"), {QStringLiteral("Ctrl+T")}},
        {QStringLiteral("tracking.trackBackward"), {QStringLiteral("Ctrl+Shift+T")}},
        {QStringLiteral("tracking.trackFrameForward"), {QStringLiteral("Ctrl+Shift+Right")}},
        {QStringLiteral("tracking.trackFrameBackward"), {QStringLiteral("Ctrl+Shift+Left")}},
        {QStringLiteral("tracking.trackBoth"), {}},
        {QStringLiteral("subtitles.nudgeLeft"), {QStringLiteral("Alt+Left")}},
        {QStringLiteral("subtitles.nudgeRight"), {QStringLiteral("Alt+Right")}},
        {QStringLiteral("subtitles.nudgeLeftCoarse"), {QStringLiteral("Alt+Shift+Left")}},
        {QStringLiteral("subtitles.nudgeRightCoarse"), {QStringLiteral("Alt+Shift+Right")}},
        {QStringLiteral("subtitles.findCues"), {QStringLiteral("Ctrl+F")}},
        {QStringLiteral("sourceLab.shuttleStop"), {QStringLiteral("K")}},
        {QStringLiteral("sourceLab.toggleCompare"), {QStringLiteral("\\")}},
        {QStringLiteral("sourceLabRanges.favoriteRange"), {QStringLiteral("F")}},
        {QStringLiteral("sourceLabRanges.rejectRange"), {QStringLiteral("Delete"), QStringLiteral("Backspace")}},
        {QStringLiteral("sourceLabRanges.removeRange"), {QStringLiteral("Shift+Delete")}},
        {QStringLiteral("color.toggleBypass"), {QStringLiteral("\\")}},
        // The 3D viewport's gizmo tools. Blender's G/R/S and W/E/R are taken by guides, split and the
        // ripple deletes, so these sit on T(ranslate), R(otate) and E (next to R). The view keys
        // follow Blender's.
        {QStringLiteral("viewport3d.gizmoMove"), {QStringLiteral("T")}},
        {QStringLiteral("viewport3d.gizmoRotate"), {QStringLiteral("R")}},
        {QStringLiteral("viewport3d.gizmoScale"), {QStringLiteral("E")}},
        {QStringLiteral("viewport3d.gizmoOrientation"), {QStringLiteral("Shift+T")}},
        {QStringLiteral("viewport3d.frameSelected"), {QStringLiteral("F")}},
        {QStringLiteral("viewport3d.frameAll"), {QStringLiteral("Home")}},
        {QStringLiteral("viewport3d.resetView"), {QStringLiteral("Shift+Home")}},
        {QStringLiteral("viewport3d.toggleFly"), {QStringLiteral("Shift+F")}},
        {QStringLiteral("viewport3d.toggleOrthographic"), {QStringLiteral("Num+5")}},
        {QStringLiteral("viewport3d.lookThrough"), {QStringLiteral("Num+0")}},
        {QStringLiteral("viewport3d.viewFront"), {QStringLiteral("Num+1")}},
        {QStringLiteral("viewport3d.viewBack"), {QStringLiteral("Ctrl+Num+1")}},
        {QStringLiteral("viewport3d.viewRight"), {QStringLiteral("Num+3")}},
        {QStringLiteral("viewport3d.viewLeft"), {QStringLiteral("Ctrl+Num+3")}},
        {QStringLiteral("viewport3d.viewTop"), {QStringLiteral("Num+7")}},
        {QStringLiteral("viewport3d.viewBottom"), {QStringLiteral("Ctrl+Num+7")}},
        {QStringLiteral("viewport3d.orbitLeft"), {QStringLiteral("Num+4")}},
        {QStringLiteral("viewport3d.orbitRight"), {QStringLiteral("Num+6")}},
        {QStringLiteral("viewport3d.orbitUp"), {QStringLiteral("Num+8")}},
        {QStringLiteral("viewport3d.orbitDown"), {QStringLiteral("Num+2")}},
        {QStringLiteral("viewport3d.dollyIn"), {QStringLiteral("Num++")}},
        {QStringLiteral("viewport3d.dollyOut"), {QStringLiteral("Num+-")}},
        // Held while flying. Shift (hurry) stays a plain modifier. Its own scope because E is also
        // the gizmo's scale tool.
        {QStringLiteral("fly3d.forward"), {QStringLiteral("W")}},
        {QStringLiteral("fly3d.back"), {QStringLiteral("S")}},
        {QStringLiteral("fly3d.left"), {QStringLiteral("A")}},
        {QStringLiteral("fly3d.right"), {QStringLiteral("D")}},
        {QStringLiteral("fly3d.down"), {QStringLiteral("Q")}},
        {QStringLiteral("fly3d.up"), {QStringLiteral("E")}},
        {QStringLiteral("nodes.undo"), {QStringLiteral("Ctrl+Z")}},
        {QStringLiteral("nodes.redo"), {QStringLiteral("Ctrl+Shift+Z")}},
        {QStringLiteral("nodes.redoAlt"), {QStringLiteral("Ctrl+Y")}},
        {QStringLiteral("nodes.selectAll"), {QStringLiteral("Ctrl+A")}},
        {QStringLiteral("nodes.copy"), {QStringLiteral("Ctrl+C")}},
        {QStringLiteral("nodes.paste"), {QStringLiteral("Ctrl+V")}},
        {QStringLiteral("nodes.duplicate"), {QStringLiteral("Ctrl+D")}},
        {QStringLiteral("nodes.delete"), {QStringLiteral("Delete"), QStringLiteral("Backspace")}},
        {QStringLiteral("nodes.cancel"), {QStringLiteral("Escape")}},
        {QStringLiteral("nodes.frameSelected"), {QStringLiteral("F")}},
        {QStringLiteral("nodes.fitAll"), {QStringLiteral("Ctrl+0")}},
        {QStringLiteral("nodes.zoomIn"), {QStringLiteral("Ctrl++")}},
        {QStringLiteral("nodes.zoomInAlt"), {QStringLiteral("Ctrl+=")}},
        {QStringLiteral("nodes.zoomOut"), {QStringLiteral("Ctrl+-")}},
        {QStringLiteral("nodes.pan"), {QStringLiteral("Space")}},
        {QStringLiteral("multicam.angle1"), {QStringLiteral("1"), QStringLiteral("Num+1")}},
        {QStringLiteral("multicam.angle2"), {QStringLiteral("2"), QStringLiteral("Num+2")}},
        {QStringLiteral("multicam.angle3"), {QStringLiteral("3"), QStringLiteral("Num+3")}},
        {QStringLiteral("multicam.angle4"), {QStringLiteral("4"), QStringLiteral("Num+4")}},
        {QStringLiteral("multicam.angle5"), {QStringLiteral("5"), QStringLiteral("Num+5")}},
        {QStringLiteral("multicam.angle6"), {QStringLiteral("6"), QStringLiteral("Num+6")}},
        {QStringLiteral("multicam.angle7"), {QStringLiteral("7"), QStringLiteral("Num+7")}},
        {QStringLiteral("multicam.angle8"), {QStringLiteral("8"), QStringLiteral("Num+8")}},
        {QStringLiteral("multicam.angle9"), {QStringLiteral("9"), QStringLiteral("Num+9")}},
        {QStringLiteral("mediaPreview.playPause"), {QStringLiteral("Space")}},
        {QStringLiteral("mediaPreview.markIn"), {QStringLiteral("I")}},
        {QStringLiteral("mediaPreview.markOut"), {QStringLiteral("O")}},
    };
}

QList<Preset> buildPresets()
{
    return {
        {QStringLiteral("drift"), QT_TRANSLATE_NOOP("Keymap", "Drift"), driftChords()},
    };
}

} // namespace

const QList<Preset> &presets()
{
    static const QList<Preset> all = buildPresets();
    return all;
}

const Preset *findPreset(const QString &id)
{
    for (const Preset &preset : presets()) {
        if (preset.id == id)
            return &preset;
    }
    return nullptr;
}

QString translatedName(const Preset &preset)
{
    return QCoreApplication::translate("Keymap", preset.name);
}

QStringList presetChords(const Preset &preset, const QString &actionId, const QString &scope)
{
    const auto it = preset.chords.constFind(actionId);
    if (it != preset.chords.cend())
        return *it;
    const Preset *base = findPreset(QLatin1String(kDefaultPresetId));
    if (base == &preset)
        return {};
    if (scope == QLatin1String(kGlobalScope))
        return {};
    return base ? base->chords.value(actionId) : QStringList{};
}

void splitChord(const QString &chord, QStringList *modifiers, QString *key)
{
    modifiers->clear();
    QString rest = chord;
    if (rest.endsWith(QLatin1Char('+'))) {
        *key = QStringLiteral("+");
        rest.chop(1);
        if (rest.endsWith(QLatin1Char('+')))
            rest.chop(1);
    } else {
        const qsizetype cut = rest.lastIndexOf(QLatin1Char('+'));
        *key = rest.mid(cut + 1);
        rest = cut < 0 ? QString() : rest.left(cut);
    }
    if (!rest.isEmpty())
        *modifiers = rest.split(QLatin1Char('+'));
}

QString translateForPlatform(const QString &chord, bool mac)
{
    if (mac)
        return chord;
    QStringList modifiers;
    QString key;
    splitChord(chord, &modifiers, &key);
    if (!modifiers.contains(QLatin1String("Meta")))
        return chord;
    modifiers.removeAll(QStringLiteral("Meta"));
    modifiers << QStringLiteral("Ctrl") << QStringLiteral("Alt");
    QStringList ordered;
    for (const char *mod : {"Ctrl", "Alt", "Shift", "Num"}) {
        if (modifiers.contains(QLatin1String(mod)))
            ordered.append(QLatin1String(mod));
    }
    ordered.append(key);
    return ordered.join(QLatin1Char('+'));
}

} // namespace drift::keymap
