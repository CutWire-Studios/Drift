pragma ComponentBehavior: Bound
import QtQuick
import Drift

// Right-click menu for one keyframe marker drawn on a timeline clip.
//
// A marker stands for every property keyed at that time, and so does this menu: picking a curve
// shapes all of them in one undo step, the way the Keyframes panel shapes a whole selection. The
// inline picks are the common ones; the full catalogue, saved presets included, is behind
// Easing…, which opens the very picker the Keyframes panel uses.
Item {
    id: root

    property int trackIndex: -1
    property int clipIndex: -1
    property real keySeconds: 0
    // {prop, seconds} for every property keyed at keySeconds. Resolved when the menu opens, not
    // bound: the menu acts on the keys that were there when it was asked for.
    //
    // Empty for a marker that stands for an animated mask shape alone: a path key holds a whole
    // vertex list and interpolates linearly between neighbours, so it has no curve to shape and
    // no scalar ref to name. The marker still earns its place by saying where the shape turns.
    property var refs: []

    signal unloadRequested()

    readonly property var quickPresetIds: ["linear", "hold", "ease", "cubicIn", "cubicOut", "cubicInOut"]
    // Labels and specs come from the shared preset catalogue, so this menu and the picker never
    // disagree about what "Ease" is.
    readonly property var quickPresets: {
        const all = EditorState.easingPresets()
        const out = []
        for (const id of root.quickPresetIds) {
            const found = all.find(p => p.id === id)
            if (found)
                out.push({ "id": found.id, "label": found.label, "spec": found.spec })
        }
        return out
    }

    function openFor(clip, seconds) {
        clipIndex = clip
        keySeconds = seconds
        refs = EditorState.keyframes.keysAt(trackIndex, clip, seconds)
        menu.popup()
    }

    function setEasing(spec) {
        if (refs.length > 0)
            EditorState.keyframes.setSegmentsEasing(trackIndex, clipIndex, refs, spec)
    }

    function openEasing() {
        if (refs.length === 0)
            return
        anchorItem.x = menu.x
        anchorItem.y = menu.y
        popupLoader.active = true
        popupLoader.item.anchorItem = anchorItem
        popupLoader.item.openFor(trackIndex, clipIndex, refs[0].prop, refs[0].seconds)
    }

    // The picker only ever shapes the one property it was opened for, so whatever it settled on
    // is carried to the rest of the group once it closes.
    function applyEasingToRest() {
        if (refs.length < 2)
            return
        const first = refs[0]
        const segment = EditorState.keyframeSegmentEasing(trackIndex, clipIndex, first.prop, first.seconds)
        if (segment && segment.spec)
            EditorState.keyframes.setSegmentsEasing(trackIndex, clipIndex, refs.slice(1), segment.spec)
    }

    function openKeyframesTab() {
        EditorState.selectClip(trackIndex, clipIndex)
        Workspaces.activeId = "keyframes"
    }

    Item {
        id: anchorItem
        width: 1
        height: 1
    }

    ThemedContextMenu {
        id: menu

        // Easing… opens a picker that outlives the menu, so the host only goes away once that
        // picker is gone too.
        onClosed: {
            if (!popupLoader.active)
                root.unloadRequested()
        }

        Instantiator {
            model: root.quickPresets
            delegate: ThemedMenuItem {
                required property var modelData
                text: modelData.label
                enabled: root.refs.length > 0
                onTriggered: root.setEasing(modelData.spec)
            }
            onObjectAdded: (i, object) => menu.insertItem(i, object)
            onObjectRemoved: (i, object) => menu.removeItem(object)
        }
        ThemedMenuItem {
            text: qsTr("Easing…")
            icon.name: Theme.icons.spline
            enabled: root.refs.length > 0
            onTriggered: root.openEasing()
        }
        ThemedMenuSeparator { }
        ThemedMenuItem {
            text: qsTr("Delete %n keyframe(s) here", "", root.refs.length)
            icon.name: Theme.icons.trash
            enabled: root.refs.length > 0
            onTriggered: EditorState.keyframes.removeKeys(root.trackIndex, root.clipIndex, root.refs)
        }
        ThemedMenuSeparator { }
        ThemedMenuItem {
            text: qsTr("Set playhead here")
            icon.name: Theme.icons.crosshair
            onTriggered: EditorState.playheadSeconds = root.keySeconds
        }
        ThemedMenuItem {
            text: qsTr("Edit keyframes…")
            icon.name: Theme.icons.diamondPlus
            onTriggered: root.openKeyframesTab()
        }
    }

    Loader {
        id: popupLoader
        active: false
        sourceComponent: EasingPickerPopup { }
    }

    Connections {
        target: popupLoader.item
        ignoreUnknownSignals: true

        function onClosed() {
            root.applyEasingToRest()
            // Deferred: dropping the Loader's item inside the Popup's own close path would
            // destroy an object still unwinding.
            Qt.callLater(function() {
                popupLoader.active = false
                root.unloadRequested()
            })
        }
    }
}
