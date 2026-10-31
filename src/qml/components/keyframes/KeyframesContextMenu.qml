pragma ComponentBehavior: Bound
import QtQuick
import Drift

Item {
    id: root

    required property var panel

    property Item anchorItem: null
    property var hit: null
    property var easingRefs: []

    readonly property bool hasSelection: panel.selection.count > 0

    function openAt(x, y, newHit) {
        hit = newHit
        menu.popup(anchorItem.parent, x, y)
    }

    function setEasing(spec) {
        EditorState.keyframes.setSegmentsEasing(panel.track, panel.clip, panel.selection.refs(), spec)
    }

    function openEasing() {
        const refs = panel.selection.refs()
        if (refs.length === 0)
            return
        easingRefs = refs
        anchorItem.x = menu.x
        anchorItem.y = menu.y
        popupLoader.active = true
        popupLoader.item.anchorItem = anchorItem
        popupLoader.item.propLabel = hit && hit.rowData && hit.rowData.label ? hit.rowData.label : ""
        popupLoader.item.openFor(panel.track, panel.clip, refs[0].prop, refs[0].seconds)
    }

    function applyEasingToRest() {
        if (easingRefs.length < 2)
            return
        const first = easingRefs[0]
        const segment = EditorState.keyframeSegmentEasing(panel.track, panel.clip, first.prop, first.seconds)
        if (segment && segment.spec)
            EditorState.keyframes.setSegmentsEasing(panel.track, panel.clip, easingRefs.slice(1), segment.spec)
        easingRefs = []
    }

    function selectRow() {
        if (!hit)
            return
        panel.selection.replace(panel.rowsModel.rowRefs(hit.row))
    }

    visible: false

    ThemedContextMenu {
        id: menu

        ThemedMenuItem {
            text: qsTr("Straight")
            enabled: root.hasSelection
            onTriggered: root.setEasing({ "kind": "linear" })
        }
        ThemedMenuItem {
            text: qsTr("Jump")
            enabled: root.hasSelection
            onTriggered: root.setEasing({ "kind": "hold" })
        }
        ThemedMenuItem {
            text: qsTr("Easing…")
            enabled: root.hasSelection
            onTriggered: root.openEasing()
        }
        ThemedMenuSeparator { }
        ThemedMenuItem {
            text: qsTr("Copy")
            enabled: root.hasSelection
            onTriggered: root.panel.handleAction("copy", true)
        }
        ThemedMenuItem {
            text: qsTr("Paste at playhead")
            enabled: EditorState.keyframes.clipboardCount > 0
            onTriggered: root.panel.handleAction("paste", true)
        }
        ThemedMenuItem {
            text: qsTr("Delete")
            enabled: root.hasSelection
            onTriggered: root.panel.handleAction("delete", true)
        }
        ThemedMenuSeparator { }
        ThemedMenuItem {
            text: qsTr("Select all keys in row")
            enabled: !!root.hit
            onTriggered: root.selectRow()
        }
        ThemedMenuItem {
            text: qsTr("Set playhead here")
            enabled: !!root.hit
            onTriggered: EditorState.playheadSeconds = root.hit.seconds
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

        function onClosed() { root.applyEasingToRest() }
    }
}
