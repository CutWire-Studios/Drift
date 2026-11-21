#include "app/editor/keymap/KeymapCatalog.h"

#include <QCoreApplication>

namespace drift::keymap {

namespace {

// Labels keep the AppController translation context so strings translated before the keymap
// moved out of AppController stay translated.
const QList<CategoryDef> kCategories = {
    {"app", QT_TRANSLATE_NOOP("Keymap", "File & app"), kGlobalScope},
    {"edit", QT_TRANSLATE_NOOP("Keymap", "Edit"), kGlobalScope},
    {"playback", QT_TRANSLATE_NOOP("Keymap", "Playback"), kGlobalScope},
    {"navigation", QT_TRANSLATE_NOOP("Keymap", "Navigation"), kGlobalScope},
    {"marking", QT_TRANSLATE_NOOP("Keymap", "Marking & work area"), kGlobalScope},
    {"markers", QT_TRANSLATE_NOOP("Keymap", "Markers"), kGlobalScope},
    {"timelineEditing", QT_TRANSLATE_NOOP("Keymap", "Timeline editing"), kGlobalScope},
    {"clips", QT_TRANSLATE_NOOP("Keymap", "Clips"), kGlobalScope},
    {"tracks", QT_TRANSLATE_NOOP("Keymap", "Tracks & audio"), kGlobalScope},
    {"tools", QT_TRANSLATE_NOOP("Keymap", "Tools"), kGlobalScope},
    {"view", QT_TRANSLATE_NOOP("Keymap", "Preview & view"), kGlobalScope},
    {"workspaces", QT_TRANSLATE_NOOP("Keymap", "Workspaces"), kGlobalScope},
};

const QList<ActionDef> kActions = {
    {"newProject", QT_TRANSLATE_NOOP("AppController", "New project"), "app"},
    {"open", QT_TRANSLATE_NOOP("AppController", "Open project"), "app"},
    {"save", QT_TRANSLATE_NOOP("AppController", "Save project"), "app"},
    {"saveAs", QT_TRANSLATE_NOOP("AppController", "Save project as…"), "app"},
    {"search", QT_TRANSLATE_NOOP("AppController", "Search…"), "app"},
    {"askCommand", QT_TRANSLATE_NOOP("AppController", "Ask (editing command)…"), "app"},
    {"undo", QT_TRANSLATE_NOOP("AppController", "Undo"), "edit"},
    {"redo", QT_TRANSLATE_NOOP("AppController", "Redo"), "edit"},
    {"copy", QT_TRANSLATE_NOOP("AppController", "Copy selection"), "edit"},
    {"cut", QT_TRANSLATE_NOOP("AppController", "Cut selection"), "edit"},
    {"paste", QT_TRANSLATE_NOOP("AppController", "Paste at current time"), "edit"},
    {"duplicate", QT_TRANSLATE_NOOP("AppController", "Duplicate selected clip"), "edit"},
    {"copyEffects", QT_TRANSLATE_NOOP("AppController", "Copy effects from clip"), "edit"},
    {"pasteEffects", QT_TRANSLATE_NOOP("AppController", "Paste effects onto clip"), "edit"},
    {"pasteAttributes", QT_TRANSLATE_NOOP("AppController", "Paste attributes…"), "edit"},
    {"delete", QT_TRANSLATE_NOOP("AppController", "Delete selection"), "edit"},
    {"deleteWithEffects", QT_TRANSLATE_NOOP("AppController", "Delete selection with its effects"), "edit"},
    {"selectAll", QT_TRANSLATE_NOOP("AppController", "Select all clips"), "edit"},
    {"clearSelection", QT_TRANSLATE_NOOP("AppController", "Clear selection"), "edit"},
    {"playPause", QT_TRANSLATE_NOOP("AppController", "Play/Pause"), "playback"},
    {"speedUp", QT_TRANSLATE_NOOP("AppController", "Increase playback speed"), "playback"},
    {"speedDown", QT_TRANSLATE_NOOP("AppController", "Decrease playback speed"), "playback"},
    {"stepBack", QT_TRANSLATE_NOOP("AppController", "Step back one frame"), "navigation"},
    {"stepForward", QT_TRANSLATE_NOOP("AppController", "Step forward one frame"), "navigation"},
    {"jumpBack", QT_TRANSLATE_NOOP("AppController", "Jump back 1 second"), "navigation"},
    {"jumpForward", QT_TRANSLATE_NOOP("AppController", "Jump forward 1 second"), "navigation"},
    {"jumpBackFar", QT_TRANSLATE_NOOP("AppController", "Jump back 10 seconds"), "navigation"},
    {"jumpForwardFar", QT_TRANSLATE_NOOP("AppController", "Jump forward 10 seconds"), "navigation"},
    {"goToStart", QT_TRANSLATE_NOOP("AppController", "Go to start of timeline"), "navigation"},
    {"goToEnd", QT_TRANSLATE_NOOP("AppController", "Go to end of timeline"), "navigation"},
    {"previousEdit", QT_TRANSLATE_NOOP("AppController", "Go to previous cut point"), "navigation"},
    {"nextEdit", QT_TRANSLATE_NOOP("AppController", "Go to next cut point"), "navigation"},
    {"markIn", QT_TRANSLATE_NOOP("AppController", "Mark work area in"), "marking"},
    {"markOut", QT_TRANSLATE_NOOP("AppController", "Mark work area out"), "marking"},
    {"goToIn", QT_TRANSLATE_NOOP("AppController", "Go to work area in"), "marking"},
    {"goToOut", QT_TRANSLATE_NOOP("AppController", "Go to work area out"), "marking"},
    {"clearInOut", QT_TRANSLATE_NOOP("AppController", "Clear work area"), "marking"},
    {"toggleLoop", QT_TRANSLATE_NOOP("AppController", "Loop work area playback"), "marking"},
    {"toggleBookmark", QT_TRANSLATE_NOOP("AppController", "Add/remove bookmark at current time"), "markers"},
    {"nextBookmark", QT_TRANSLATE_NOOP("AppController", "Go to next bookmark"), "markers"},
    {"previousBookmark", QT_TRANSLATE_NOOP("AppController", "Go to previous bookmark"), "markers"},
    {"split", QT_TRANSLATE_NOOP("AppController", "Split selection, or every track, at current time"), "timelineEditing"},
    {"splitSelection", QT_TRANSLATE_NOOP("AppController", "Split selected clip at current time"), "timelineEditing"},
    {"splitAllTracks", QT_TRANSLATE_NOOP("AppController", "Split every track at current time"), "timelineEditing"},
    {"deleteLeft", QT_TRANSLATE_NOOP("AppController", "Delete left of the playhead"), "timelineEditing"},
    {"deleteRight", QT_TRANSLATE_NOOP("AppController", "Delete right of the playhead"), "timelineEditing"},
    {"addTransitionAtPlayhead", QT_TRANSLATE_NOOP("AppController", "Add transition at playhead"), "timelineEditing"},
    {"addSpeedPoint", QT_TRANSLATE_NOOP("AppController", "Add speed point at playhead"), "timelineEditing"},
    {"merge", QT_TRANSLATE_NOOP("AppController", "Merge adjacent clips"), "timelineEditing"},
    {"nudgeLeft", QT_TRANSLATE_NOOP("AppController", "Move selection left a little"), "timelineEditing"},
    {"nudgeRight", QT_TRANSLATE_NOOP("AppController", "Move selection right a little"), "timelineEditing"},
    {"toggleClipEnabled", QT_TRANSLATE_NOOP("AppController", "Enable or disable selected clips"), "clips"},
    {"transformTogether", QT_TRANSLATE_NOOP("AppController", "Transform selection together"), "clips"},
    {"selectTransformLayer", QT_TRANSLATE_NOOP("AppController", "Select transform layer"), "clips"},
    {"link", QT_TRANSLATE_NOOP("AppController", "Link clips"), "clips"},
    {"unlink", QT_TRANSLATE_NOOP("AppController", "Unlink audio"), "clips"},
    {"separateAudio", QT_TRANSLATE_NOOP("AppController", "Separate audio"), "tracks"},
    {"toggleTrackHidden", QT_TRANSLATE_NOOP("AppController", "Hide/show selected clip's track"), "tracks"},
    {"selectTool", QT_TRANSLATE_NOOP("AppController", "Select tool"), "tools"},
    {"bladeTool", QT_TRANSLATE_NOOP("AppController", "Cut tool / Brush mask"), "tools"},
    {"gizmoMove", QT_TRANSLATE_NOOP("AppController", "3D gizmo: move / Tracker tool"), "tools"},
    {"gizmoRotate", QT_TRANSLATE_NOOP("AppController", "3D gizmo: rotate / Rectangle mask"), "tools"},
    {"gizmoScale", QT_TRANSLATE_NOOP("AppController", "3D gizmo: scale / Ellipse mask"), "tools"},
    {"gizmoOrientation", QT_TRANSLATE_NOOP("AppController", "3D gizmo: cycle world/local/view axes"), "tools"},
    {"maskingPolygonTool", QT_TRANSLATE_NOOP("AppController", "Polygon mask tool"), "tools"},
    {"maskingAiTool", QT_TRANSLATE_NOOP("AppController", "AI object mask tool"), "tools"},
    {"maskingLassoTool", QT_TRANSLATE_NOOP("AppController", "Lasso mask tool"), "tools"},
    {"maskingScissorsTool", QT_TRANSLATE_NOOP("AppController", "Intelligent scissors mask tool"), "tools"},
    {"maskingWandTool", QT_TRANSLATE_NOOP("AppController", "Magic wand mask tool"), "tools"},
    {"maskingBrushSmaller", QT_TRANSLATE_NOOP("AppController", "Decrease brush size"), "tools"},
    {"maskingBrushLarger", QT_TRANSLATE_NOOP("AppController", "Increase brush size"), "tools"},
    {"maskingBrushToggleErase", QT_TRANSLATE_NOOP("AppController", "Brush: switch paint / erase"), "tools"},
    {"trackForward", QT_TRANSLATE_NOOP("AppController", "Track forward"), "tools"},
    {"trackBackward", QT_TRANSLATE_NOOP("AppController", "Track backward"), "tools"},
    {"trackFrameForward", QT_TRANSLATE_NOOP("AppController", "Track one frame forward"), "tools"},
    {"trackFrameBackward", QT_TRANSLATE_NOOP("AppController", "Track one frame backward"), "tools"},
    {"trackBoth", QT_TRANSLATE_NOOP("AppController", "Track whole clip"), "tools"},
    {"zoomIn", QT_TRANSLATE_NOOP("AppController", "Zoom in"), "view"},
    {"zoomOut", QT_TRANSLATE_NOOP("AppController", "Zoom out"), "view"},
    {"toggleGuides", QT_TRANSLATE_NOOP("AppController", "Toggle guides"), "view"},
    {"multicam", QT_TRANSLATE_NOOP("AppController", "Multicam window"), "workspaces"},
    {"workspace.restoreWindows", QT_TRANSLATE_NOOP("AppController", "Restore Workspace Windows"), "workspaces"},
    {"workspace.next", QT_TRANSLATE_NOOP("AppController", "Next workspace"), "workspaces"},
    {"workspace.prev", QT_TRANSLATE_NOOP("AppController", "Previous workspace"), "workspaces"},
    {"workspace.1", QT_TRANSLATE_NOOP("AppController", "Workspace 1"), "workspaces"},
    {"workspace.2", QT_TRANSLATE_NOOP("AppController", "Workspace 2"), "workspaces"},
    {"workspace.3", QT_TRANSLATE_NOOP("AppController", "Workspace 3"), "workspaces"},
    {"workspace.4", QT_TRANSLATE_NOOP("AppController", "Workspace 4"), "workspaces"},
    {"workspace.5", QT_TRANSLATE_NOOP("AppController", "Workspace 5"), "workspaces"},
    {"workspace.6", QT_TRANSLATE_NOOP("AppController", "Workspace 6"), "workspaces"},
    {"workspace.7", QT_TRANSLATE_NOOP("AppController", "Workspace 7"), "workspaces"},
    {"workspace.8", QT_TRANSLATE_NOOP("AppController", "Workspace 8"), "workspaces"},
    {"workspace.9", QT_TRANSLATE_NOOP("AppController", "Workspace 9"), "workspaces"},
};

} // namespace

const QList<CategoryDef> &categories()
{
    return kCategories;
}

const QList<ActionDef> &actions()
{
    return kActions;
}

const ActionDef *findAction(const QString &id)
{
    for (const ActionDef &action : kActions) {
        if (id == QLatin1String(action.id))
            return &action;
    }
    return nullptr;
}

const CategoryDef *findCategory(const QString &id)
{
    for (const CategoryDef &category : kCategories) {
        if (id == QLatin1String(category.id))
            return &category;
    }
    return nullptr;
}

QString scopeOf(const ActionDef &action)
{
    const CategoryDef *category = findCategory(QLatin1String(action.category));
    return QLatin1String(category ? category->scope : kGlobalScope);
}

QString translatedLabel(const ActionDef &action)
{
    return QCoreApplication::translate("AppController", action.label);
}

QString translatedCategoryLabel(const CategoryDef &category)
{
    return QCoreApplication::translate("Keymap", category.label);
}

} // namespace drift::keymap
