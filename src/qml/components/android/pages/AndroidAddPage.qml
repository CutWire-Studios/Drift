import QtQuick
import QtQuick.Controls.Basic
import Drift
// Shared body of the add-item pages (sticker, shape, text, mask). Opening one adds nothing; the
// first pick adds a clip at the playhead and selects it, and each later pick replaces it. All of
// it sits inside the scaffold's edit group, so Cancel removes the clip and Apply leaves one step.
// Children go in the picker slot. `propertiesComponent` is the inspector shown behind the
// Properties chip once a clip exists.
Item {
    id: root

    property var scaffold: null
    property int trackIndex: -1
    property int clipIndex: -1
    // Id of the clip this page added; indices shift as clips come and go, ids do not.
    property string clipId: ""
    property Component propertiesComponent: null
    default property alias picker: pickerHost.data

    readonly property bool hasClip: root.clipId !== "" && root.clipIndex >= 0
    property bool showProperties: false

    // Call after a tab has added a clip: the new clip is the selection.
    function adopt() {
        const added = EditorState.clipAt(EditorState.selectedTrack, EditorState.selectedClip)
        if (!added.id)
            return
        if (root.clipId !== "" && root.clipId !== added.id) {
            root.discard()
            root.select(added.id)
        }
        root.clipId = added.id
        root.follow()
    }

    function select(id) {
        const at = EditorState.locateClip(id)
        if (at.length === 2)
            EditorState.selectClip(at[0], at[1])
    }

    function discard() {
        if (root.clipId === "")
            return
        const at = EditorState.locateClip(root.clipId)
        if (at.length === 2) {
            EditorState.selectClip(at[0], at[1])
            EditorState.deleteSelectedClip()
        }
        root.clipId = ""
    }

    // Points the scaffold's range bar and preview range at the current selection.
    function follow() {
        if (!root.scaffold)
            return
        root.scaffold.trackIndex = EditorState.selectedTrack
        root.scaffold.clipIndex = EditorState.selectedClip
        EditorState.previewClipRange(EditorState.selectedTrack, EditorState.selectedClip)
    }

    Row {
        id: chips
        visible: root.hasClip && root.propertiesComponent !== null
        x: Theme.pagePadding
        height: visible ? Theme.controlHeight : 0
        spacing: Theme.spacingSm

        ThemedChip {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Library")
            variant: "outline"
            selected: !root.showProperties
            onClicked: root.showProperties = false
        }

        ThemedChip {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Properties")
            variant: "outline"
            selected: root.showProperties
            onClicked: root.showProperties = true
        }
    }

    Item {
        id: pickerHost
        anchors.top: chips.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        visible: !(root.showProperties && chips.visible)
    }

    Flickable {
        id: propsFlick
        anchors.top: chips.bottom
        anchors.topMargin: Theme.spacingSm
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        visible: root.showProperties && chips.visible
        clip: true
        contentWidth: width
        contentHeight: propsLoader.item ? propsLoader.item.implicitHeight + Theme.pagePadding : 0
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar { }

        Loader {
            id: propsLoader
            x: Theme.pagePadding
            width: propsFlick.width - Theme.pagePadding * 2
            active: propsFlick.visible
            sourceComponent: root.propertiesComponent
        }
    }
}
