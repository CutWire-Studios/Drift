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
    {"keyframes", QT_TRANSLATE_NOOP("Keymap", "Keyframes panel"), "keyframes"},
    {"masking", QT_TRANSLATE_NOOP("Keymap", "Masking"), "masking"},
    {"path", QT_TRANSLATE_NOOP("Keymap", "Path editing"), "path"},
    {"tracking", QT_TRANSLATE_NOOP("Keymap", "Tracking"), "tracking"},
    {"subtitles", QT_TRANSLATE_NOOP("Keymap", "Subtitles"), "subtitles"},
    {"sourceLab", QT_TRANSLATE_NOOP("Keymap", "Source Lab viewer"), "sourceLab"},
    {"sourceLabRanges", QT_TRANSLATE_NOOP("Keymap", "Source Lab ranges"), "sourceLabRanges", true},
    {"color", QT_TRANSLATE_NOOP("Keymap", "Color panel"), "color"},
    {"viewport3d", QT_TRANSLATE_NOOP("Keymap", "3D viewport"), "viewport3d"},
    {"fly3d", QT_TRANSLATE_NOOP("Keymap", "3D fly mode"), "fly3d", true},
    {"nodes", QT_TRANSLATE_NOOP("Keymap", "Node editor"), "nodes", true},
    {"multicam", QT_TRANSLATE_NOOP("Keymap", "Multicam window"), "multicam", true},
    {"mediaPreview", QT_TRANSLATE_NOOP("Keymap", "Media preview"), "mediaPreview", true},
};

const QList<ActionDef> kActions = {
    {"newProject", QT_TRANSLATE_NOOP("AppController", "New project"), "app"},
    {"importMedia", QT_TRANSLATE_NOOP("AppController", "Import media to timeline…"), "app"},
    {"export", QT_TRANSLATE_NOOP("AppController", "Export…"), "app"},
    {"openSettings", QT_TRANSLATE_NOOP("AppController", "Open settings"), "app"},
    {"openKeymap", QT_TRANSLATE_NOOP("AppController", "Open keyboard shortcuts"), "app"},
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
    {"deselectAll", QT_TRANSLATE_NOOP("AppController", "Deselect all"), "edit"},
    {"selectClipAtPlayhead", QT_TRANSLATE_NOOP("AppController", "Select clip at playhead"), "edit"},
    {"selectNextClip", QT_TRANSLATE_NOOP("AppController", "Select next clip"), "edit"},
    {"selectPreviousClip", QT_TRANSLATE_NOOP("AppController", "Select previous clip"), "edit"},
    {"rippleDelete", QT_TRANSLATE_NOOP("AppController", "Ripple delete selection"), "edit"},
    {"removeAllEffects", QT_TRANSLATE_NOOP("AppController", "Remove all effects from clip"), "edit"},
    {"renameClip", QT_TRANSLATE_NOOP("AppController", "Rename clip…"), "edit"},
    {"playPause", QT_TRANSLATE_NOOP("AppController", "Play/Pause"), "playback"},
    {"stop", QT_TRANSLATE_NOOP("AppController", "Stop and reset speed"), "playback"},
    {"speedUp", QT_TRANSLATE_NOOP("AppController", "Increase playback speed"), "playback"},
    {"speedDown", QT_TRANSLATE_NOOP("AppController", "Decrease playback speed"), "playback"},
    {"stepBack", QT_TRANSLATE_NOOP("AppController", "Step back one frame"), "navigation"},
    {"stepForward", QT_TRANSLATE_NOOP("AppController", "Step forward one frame"), "navigation"},
    {"stepBack5", QT_TRANSLATE_NOOP("AppController", "Step back five frames"), "navigation"},
    {"stepForward5", QT_TRANSLATE_NOOP("AppController", "Step forward five frames"), "navigation"},
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
    {"markClip", QT_TRANSLATE_NOOP("AppController", "Mark work area to selected clip"), "marking"},
    {"clearIn", QT_TRANSLATE_NOOP("AppController", "Clear work area in"), "marking"},
    {"clearOut", QT_TRANSLATE_NOOP("AppController", "Clear work area out"), "marking"},
    {"toggleLoop", QT_TRANSLATE_NOOP("AppController", "Loop work area playback"), "marking"},
    {"toggleBookmark", QT_TRANSLATE_NOOP("AppController", "Add/remove bookmark at current time"), "markers"},
    {"nextBookmark", QT_TRANSLATE_NOOP("AppController", "Go to next bookmark"), "markers"},
    {"previousBookmark", QT_TRANSLATE_NOOP("AppController", "Go to previous bookmark"), "markers"},
    {"editBookmark", QT_TRANSLATE_NOOP("AppController", "Rename bookmark at playhead"), "markers"},
    {"deleteBookmark", QT_TRANSLATE_NOOP("AppController", "Delete bookmark at playhead"), "markers"},
    {"deleteAllBookmarks", QT_TRANSLATE_NOOP("AppController", "Delete all bookmarks"), "markers"},
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
    {"nudgeLeftFar", QT_TRANSLATE_NOOP("AppController", "Move selection left one second"), "timelineEditing"},
    {"nudgeRightFar", QT_TRANSLATE_NOOP("AppController", "Move selection right one second"), "timelineEditing"},
    {"trimStart", QT_TRANSLATE_NOOP("AppController", "Trim start tool"), "timelineEditing"},
    {"trimEnd", QT_TRANSLATE_NOOP("AppController", "Trim end tool"), "timelineEditing"},
    {"freezeFrame", QT_TRANSLATE_NOOP("AppController", "Freeze frame at playhead"), "timelineEditing"},
    {"splitOnBeats", QT_TRANSLATE_NOOP("AppController", "Split selected clip on beats"), "timelineEditing"},
    {"swapWithPrevious", QT_TRANSLATE_NOOP("AppController", "Swap with previous clip"), "timelineEditing"},
    {"swapWithNext", QT_TRANSLATE_NOOP("AppController", "Swap with next clip"), "timelineEditing"},
    {"addAdjustmentLayer", QT_TRANSLATE_NOOP("AppController", "Add adjustment layer"), "timelineEditing"},
    {"addCamera", QT_TRANSLATE_NOOP("AppController", "Add camera"), "timelineEditing"},
    {"addText", QT_TRANSLATE_NOOP("AppController", "Add text at playhead"), "timelineEditing"},
    {"toggleClipEnabled", QT_TRANSLATE_NOOP("AppController", "Enable or disable selected clips"), "clips"},
    {"transformTogether", QT_TRANSLATE_NOOP("AppController", "Transform selection together"), "clips"},
    {"selectTransformLayer", QT_TRANSLATE_NOOP("AppController", "Select transform layer"), "clips"},
    {"link", QT_TRANSLATE_NOOP("AppController", "Link clips"), "clips"},
    {"unlink", QT_TRANSLATE_NOOP("AppController", "Unlink audio"), "clips"},
    {"replaceClip", QT_TRANSLATE_NOOP("AppController", "Replace clip with…"), "clips"},
    {"moveToOwnTrack", QT_TRANSLATE_NOOP("AppController", "Move adjustment to its own track"), "clips"},
    {"makeComposite", QT_TRANSLATE_NOOP("AppController", "Make composite from selection"), "clips"},
    {"openComposite", QT_TRANSLATE_NOOP("AppController", "Open composite"), "clips"},
    {"flattenComposite", QT_TRANSLATE_NOOP("AppController", "Flatten composite"), "clips"},
    {"clipSpeed", QT_TRANSLATE_NOOP("AppController", "Clip speed"), "clips"},
    {"reverseClip", QT_TRANSLATE_NOOP("AppController", "Reverse clip"), "clips"},
    {"crop", QT_TRANSLATE_NOOP("AppController", "Crop…"), "clips"},
    {"colorGrade", QT_TRANSLATE_NOOP("AppController", "Color grade"), "clips"},
    {"editKeyframes", QT_TRANSLATE_NOOP("AppController", "Edit keyframes"), "clips"},
    {"maskAndTrack", QT_TRANSLATE_NOOP("AppController", "Mask & track"), "clips"},
    {"clipProperties", QT_TRANSLATE_NOOP("AppController", "Clip properties"), "clips"},
    {"removeSilence", QT_TRANSLATE_NOOP("AppController", "Remove silence"), "clips"},
    {"autoDuck", QT_TRANSLATE_NOOP("AppController", "Auto duck"), "clips"},
    {"syncByAudio", QT_TRANSLATE_NOOP("AppController", "Sync by audio"), "clips"},
    {"separateAudio", QT_TRANSLATE_NOOP("AppController", "Separate audio"), "tracks"},
    {"toggleTrackHidden", QT_TRANSLATE_NOOP("AppController", "Hide/show selected clip's track"), "tracks"},
    {"toggleTrackMuted", QT_TRANSLATE_NOOP("AppController", "Mute/unmute selected clip's track"), "tracks"},
    {"toggleTrackSolo", QT_TRANSLATE_NOOP("AppController", "Solo/unsolo selected clip's track"), "tracks"},
    {"toggleTrackLocked", QT_TRANSLATE_NOOP("AppController", "Lock selected clip's track"), "tracks"},
    {"addVideoTrack", QT_TRANSLATE_NOOP("AppController", "Add video track"), "tracks"},
    {"addAudioTrack", QT_TRANSLATE_NOOP("AppController", "Add audio track"), "tracks"},
    {"trackTaller", QT_TRANSLATE_NOOP("AppController", "Make selected track taller"), "tracks"},
    {"trackShorter", QT_TRANSLATE_NOOP("AppController", "Make selected track shorter"), "tracks"},
    {"recordVoiceover", QT_TRANSLATE_NOOP("AppController", "Start/stop voiceover recording"), "tracks"},
    {"selectTool", QT_TRANSLATE_NOOP("AppController", "Select tool"), "tools"},
    {"bladeTool", QT_TRANSLATE_NOOP("AppController", "Cut tool"), "tools"},
    {"zoomIn", QT_TRANSLATE_NOOP("AppController", "Zoom in"), "view"},
    {"zoomOut", QT_TRANSLATE_NOOP("AppController", "Zoom out"), "view"},
    {"zoomToFit", QT_TRANSLATE_NOOP("AppController", "Zoom timeline to fit project"), "view"},
    {"toggleSnapping", QT_TRANSLATE_NOOP("AppController", "Toggle snapping"), "view"},
    {"toggleRipple", QT_TRANSLATE_NOOP("AppController", "Toggle ripple editing"), "view"},
    {"toggleOverlap", QT_TRANSLATE_NOOP("AppController", "Toggle clip overlap"), "view"},
    {"toggleAutoSelect", QT_TRANSLATE_NOOP("AppController", "Toggle auto-select under playhead"), "view"},
    {"toggleMixerStrip", QT_TRANSLATE_NOOP("AppController", "Toggle audio mixer strip"), "view"},
    {"toggleKeyframeStrip", QT_TRANSLATE_NOOP("AppController", "Toggle keyframe strip"), "view"},
    {"toggleOverview", QT_TRANSLATE_NOOP("AppController", "Toggle timeline overview"), "view"},
    {"togglePreviewFullscreen", QT_TRANSLATE_NOOP("AppController", "Toggle preview fullscreen"), "view"},
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
    {"keyframes.addKey", QT_TRANSLATE_NOOP("AppController", "Add or remove keyframe at playhead"), "keyframes"},
    {"keyframes.fitSelection", QT_TRANSLATE_NOOP("AppController", "Fit view to the selected keys, or all"), "keyframes"},
    {"keyframes.fitAll", QT_TRANSLATE_NOOP("AppController", "Fit view to the whole clip"), "keyframes"},
    {"keyframes.prevKey", QT_TRANSLATE_NOOP("AppController", "Go to previous keyframe"), "keyframes"},
    {"keyframes.nextKey", QT_TRANSLATE_NOOP("AppController", "Go to next keyframe"), "keyframes"},
    {"keyframes.nudgeLeft", QT_TRANSLATE_NOOP("AppController", "Nudge selected keys one frame earlier"), "keyframes"},
    {"keyframes.nudgeRight", QT_TRANSLATE_NOOP("AppController", "Nudge selected keys one frame later"), "keyframes"},
    {"keyframes.nudgeLeftFar", QT_TRANSLATE_NOOP("AppController", "Nudge selected keys ten frames earlier"), "keyframes"},
    {"keyframes.nudgeRightFar", QT_TRANSLATE_NOOP("AppController", "Nudge selected keys ten frames later"), "keyframes"},
    {"keyframes.nudgeUp", QT_TRANSLATE_NOOP("AppController", "Nudge selected keys up in the curves"), "keyframes"},
    {"keyframes.nudgeDown", QT_TRANSLATE_NOOP("AppController", "Nudge selected keys down in the curves"), "keyframes"},
    {"keyframes.nudgeUpFar", QT_TRANSLATE_NOOP("AppController", "Nudge selected keys ten steps up in the curves"), "keyframes"},
    {"keyframes.nudgeDownFar", QT_TRANSLATE_NOOP("AppController", "Nudge selected keys ten steps down in the curves"), "keyframes"},
    {"masking.selectTool", QT_TRANSLATE_NOOP("AppController", "Select tool"), "masking"},
    {"masking.rectTool", QT_TRANSLATE_NOOP("AppController", "Rectangle mask tool"), "masking"},
    {"masking.ellipseTool", QT_TRANSLATE_NOOP("AppController", "Ellipse mask tool"), "masking"},
    {"masking.polygonTool", QT_TRANSLATE_NOOP("AppController", "Polygon mask tool"), "masking"},
    {"masking.brushTool", QT_TRANSLATE_NOOP("AppController", "Brush mask tool"), "masking"},
    {"masking.lassoTool", QT_TRANSLATE_NOOP("AppController", "Lasso mask tool"), "masking"},
    {"masking.scissorsTool", QT_TRANSLATE_NOOP("AppController", "Intelligent scissors mask tool"), "masking"},
    {"masking.wandTool", QT_TRANSLATE_NOOP("AppController", "Magic wand mask tool"), "masking"},
    {"masking.aiTool", QT_TRANSLATE_NOOP("AppController", "AI object mask tool"), "masking"},
    {"masking.brushSmaller", QT_TRANSLATE_NOOP("AppController", "Decrease brush size"), "masking"},
    {"masking.brushLarger", QT_TRANSLATE_NOOP("AppController", "Increase brush size"), "masking"},
    {"masking.brushSofter", QT_TRANSLATE_NOOP("AppController", "Decrease brush hardness"), "masking"},
    {"masking.brushHarder", QT_TRANSLATE_NOOP("AppController", "Increase brush hardness"), "masking"},
    {"masking.brushToggleErase", QT_TRANSLATE_NOOP("AppController", "Brush: switch between paint and erase"), "masking"},
    {"path.penTool", QT_TRANSLATE_NOOP("AppController", "Pen tool: add points to a path"), "path"},
    {"path.convertPoint", QT_TRANSLATE_NOOP("AppController", "Convert point between corner and smooth"), "path"},
    {"path.deleteNoHeal", QT_TRANSLATE_NOOP("AppController", "Delete point without keeping the curve"), "path"},
    {"tracking.trackerTool", QT_TRANSLATE_NOOP("AppController", "Tracker tool"), "tracking"},
    {"tracking.trackForward", QT_TRANSLATE_NOOP("AppController", "Track forward"), "tracking"},
    {"tracking.trackBackward", QT_TRANSLATE_NOOP("AppController", "Track backward"), "tracking"},
    {"tracking.trackFrameForward", QT_TRANSLATE_NOOP("AppController", "Track one frame forward"), "tracking"},
    {"tracking.trackFrameBackward", QT_TRANSLATE_NOOP("AppController", "Track one frame backward"), "tracking"},
    {"tracking.trackBoth", QT_TRANSLATE_NOOP("AppController", "Track whole clip"), "tracking"},
    {"subtitles.nudgeLeft", QT_TRANSLATE_NOOP("AppController", "Nudge selected cues earlier"), "subtitles"},
    {"subtitles.nudgeRight", QT_TRANSLATE_NOOP("AppController", "Nudge selected cues later"), "subtitles"},
    {"subtitles.nudgeLeftCoarse", QT_TRANSLATE_NOOP("AppController", "Nudge selected cues 0.1 s earlier"), "subtitles"},
    {"subtitles.nudgeRightCoarse", QT_TRANSLATE_NOOP("AppController", "Nudge selected cues 0.1 s later"), "subtitles"},
    {"subtitles.findCues", QT_TRANSLATE_NOOP("AppController", "Find in cues"), "subtitles"},
    {"sourceLab.shuttleStop", QT_TRANSLATE_NOOP("AppController", "Stop shuttle"), "sourceLab"},
    {"sourceLab.toggleCompare", QT_TRANSLATE_NOOP("AppController", "Toggle before/after compare"), "sourceLab"},
    {"sourceLabRanges.favoriteRange", QT_TRANSLATE_NOOP("AppController", "Favorite the selected ranges"), "sourceLabRanges"},
    {"sourceLabRanges.rejectRange", QT_TRANSLATE_NOOP("AppController", "Reject the selected ranges"), "sourceLabRanges"},
    {"sourceLabRanges.removeRange", QT_TRANSLATE_NOOP("AppController", "Remove the selected ranges"), "sourceLabRanges"},
    {"color.toggleBypass", QT_TRANSLATE_NOOP("AppController", "Bypass color grade"), "color"},
    {"viewport3d.gizmoMove", QT_TRANSLATE_NOOP("AppController", "Gizmo: move"), "viewport3d"},
    {"viewport3d.gizmoRotate", QT_TRANSLATE_NOOP("AppController", "Gizmo: rotate"), "viewport3d"},
    {"viewport3d.gizmoScale", QT_TRANSLATE_NOOP("AppController", "Gizmo: scale"), "viewport3d"},
    {"viewport3d.gizmoOrientation", QT_TRANSLATE_NOOP("AppController", "Gizmo: cycle world/local/view axes"), "viewport3d"},
    {"viewport3d.frameSelected", QT_TRANSLATE_NOOP("AppController", "Frame the selection"), "viewport3d"},
    {"viewport3d.frameAll", QT_TRANSLATE_NOOP("AppController", "Frame everything"), "viewport3d"},
    {"viewport3d.resetView", QT_TRANSLATE_NOOP("AppController", "Reset the view"), "viewport3d"},
    {"viewport3d.toggleFly", QT_TRANSLATE_NOOP("AppController", "Fly mode"), "viewport3d"},
    {"viewport3d.toggleOrthographic", QT_TRANSLATE_NOOP("AppController", "Toggle orthographic projection"), "viewport3d"},
    {"viewport3d.lookThrough", QT_TRANSLATE_NOOP("AppController", "Look through the scene camera"), "viewport3d"},
    {"viewport3d.viewFront", QT_TRANSLATE_NOOP("AppController", "Front view"), "viewport3d"},
    {"viewport3d.viewBack", QT_TRANSLATE_NOOP("AppController", "Back view"), "viewport3d"},
    {"viewport3d.viewRight", QT_TRANSLATE_NOOP("AppController", "Right view"), "viewport3d"},
    {"viewport3d.viewLeft", QT_TRANSLATE_NOOP("AppController", "Left view"), "viewport3d"},
    {"viewport3d.viewTop", QT_TRANSLATE_NOOP("AppController", "Top view"), "viewport3d"},
    {"viewport3d.viewBottom", QT_TRANSLATE_NOOP("AppController", "Bottom view"), "viewport3d"},
    {"viewport3d.orbitLeft", QT_TRANSLATE_NOOP("AppController", "Orbit left"), "viewport3d"},
    {"viewport3d.orbitRight", QT_TRANSLATE_NOOP("AppController", "Orbit right"), "viewport3d"},
    {"viewport3d.orbitUp", QT_TRANSLATE_NOOP("AppController", "Orbit up"), "viewport3d"},
    {"viewport3d.orbitDown", QT_TRANSLATE_NOOP("AppController", "Orbit down"), "viewport3d"},
    {"viewport3d.dollyIn", QT_TRANSLATE_NOOP("AppController", "Dolly in"), "viewport3d"},
    {"viewport3d.dollyOut", QT_TRANSLATE_NOOP("AppController", "Dolly out"), "viewport3d"},
    {"fly3d.forward", QT_TRANSLATE_NOOP("AppController", "Fly forward"), "fly3d"},
    {"fly3d.back", QT_TRANSLATE_NOOP("AppController", "Fly backward"), "fly3d"},
    {"fly3d.left", QT_TRANSLATE_NOOP("AppController", "Fly left"), "fly3d"},
    {"fly3d.right", QT_TRANSLATE_NOOP("AppController", "Fly right"), "fly3d"},
    {"fly3d.down", QT_TRANSLATE_NOOP("AppController", "Fly down"), "fly3d"},
    {"fly3d.up", QT_TRANSLATE_NOOP("AppController", "Fly up"), "fly3d"},
    {"fly3d.land", QT_TRANSLATE_NOOP("AppController", "Land (finish flying)"), "fly3d"},
    {"nodes.undo", QT_TRANSLATE_NOOP("AppController", "Undo"), "nodes"},
    {"nodes.redo", QT_TRANSLATE_NOOP("AppController", "Redo"), "nodes"},
    {"nodes.redoAlt", QT_TRANSLATE_NOOP("AppController", "Redo (alternate)"), "nodes"},
    {"nodes.selectAll", QT_TRANSLATE_NOOP("AppController", "Select all nodes"), "nodes"},
    {"nodes.copy", QT_TRANSLATE_NOOP("AppController", "Copy nodes"), "nodes"},
    {"nodes.paste", QT_TRANSLATE_NOOP("AppController", "Paste nodes"), "nodes"},
    {"nodes.duplicate", QT_TRANSLATE_NOOP("AppController", "Duplicate nodes"), "nodes"},
    {"nodes.delete", QT_TRANSLATE_NOOP("AppController", "Delete selection"), "nodes"},
    {"nodes.cancel", QT_TRANSLATE_NOOP("AppController", "Cancel the pending connection"), "nodes"},
    {"nodes.frameSelected", QT_TRANSLATE_NOOP("AppController", "Frame the selection"), "nodes"},
    {"nodes.fitAll", QT_TRANSLATE_NOOP("AppController", "Fit all nodes"), "nodes"},
    {"nodes.zoomIn", QT_TRANSLATE_NOOP("AppController", "Zoom in"), "nodes"},
    {"nodes.zoomInAlt", QT_TRANSLATE_NOOP("AppController", "Zoom in (alternate)"), "nodes"},
    {"nodes.zoomOut", QT_TRANSLATE_NOOP("AppController", "Zoom out"), "nodes"},
    {"nodes.pan", QT_TRANSLATE_NOOP("AppController", "Hold to pan, tap for quick add"), "nodes"},
    {"multicam.angle1", QT_TRANSLATE_NOOP("AppController", "Switch to angle 1"), "multicam"},
    {"multicam.angle2", QT_TRANSLATE_NOOP("AppController", "Switch to angle 2"), "multicam"},
    {"multicam.angle3", QT_TRANSLATE_NOOP("AppController", "Switch to angle 3"), "multicam"},
    {"multicam.angle4", QT_TRANSLATE_NOOP("AppController", "Switch to angle 4"), "multicam"},
    {"multicam.angle5", QT_TRANSLATE_NOOP("AppController", "Switch to angle 5"), "multicam"},
    {"multicam.angle6", QT_TRANSLATE_NOOP("AppController", "Switch to angle 6"), "multicam"},
    {"multicam.angle7", QT_TRANSLATE_NOOP("AppController", "Switch to angle 7"), "multicam"},
    {"multicam.angle8", QT_TRANSLATE_NOOP("AppController", "Switch to angle 8"), "multicam"},
    {"multicam.angle9", QT_TRANSLATE_NOOP("AppController", "Switch to angle 9"), "multicam"},
    {"mediaPreview.playPause", QT_TRANSLATE_NOOP("AppController", "Play/Pause"), "mediaPreview"},
    {"mediaPreview.markIn", QT_TRANSLATE_NOOP("AppController", "Set in point"), "mediaPreview"},
    {"mediaPreview.markOut", QT_TRANSLATE_NOOP("AppController", "Set out point"), "mediaPreview"},
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

bool isLocalScope(const QString &scope)
{
    for (const CategoryDef &category : kCategories) {
        if (scope == QLatin1String(category.scope))
            return category.local;
    }
    return false;
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
