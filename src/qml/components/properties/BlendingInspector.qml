import QtQuick
import QtQuick.Controls.Basic
import Drift
import ".."

Item {
    id: root

    property int clipDataRevision: 0
    readonly property var clipData: {
        void clipDataRevision
        return EditorState.selectedClipData
    }
    readonly property bool hasSelection: !!clipData && Object.keys(clipData).length > 0
    readonly property string clipKind: hasSelection ? (clipData.kind || "") : ""

    height: contentCol.height
    implicitHeight: contentCol.height

    function refreshFields() {}

    Connections {
        target: EditorState
        function onSelectionChanged() { root.clipDataRevision++ }
        function onSelectedClipDataChanged() { root.clipDataRevision++ }
        function onTracksChanged() { root.clipDataRevision++ }
    }

    Column {
        id: contentCol
        width: root.width
        spacing: Theme.spacingXl

        EmptyState {
            visible: root.clipKind === "audio"
            width: parent.width
            compact: true
            glyph: Theme.icons.film
            title: qsTr("Video only")
            hint: qsTr("This tab does not apply to audio clips.")
        }

        Text {
            visible: root.clipKind !== "audio"
            text: qsTr("Blend mode")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        // The tab was a label plus one combo with no description.
        Text {
            visible: root.clipKind !== "audio"
            width: parent.width
            wrapMode: Text.WordWrap
            text: qsTr("How this clip's colours combine with the tracks beneath it.")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            opacity: 0.8
        }

        ThemedComboBox {
            id: blendModeBox
            visible: root.clipKind !== "audio"
            width: parent.width
            model: ["normal", "multiply", "screen", "overlay", "add", "darken", "lighten"]
            // Human labels — raw ids were shown to the user.
            property var labels: ({
                "normal": qsTr("Normal"),
                "multiply": qsTr("Multiply"),
                "screen": qsTr("Screen"),
                "overlay": qsTr("Overlay"),
                "add": qsTr("Add"),
                "darken": qsTr("Darken"),
                "lighten": qsTr("Lighten")
            })
            displayText: labels[model[currentIndex]] || model[currentIndex]
            tooltip: qsTr("How this clip blends with the layers below")
            currentIndex: Math.max(0, model.indexOf(root.clipData.blendMode || "normal"))
            onActivated: EditorState.setClipBlendMode(
                             EditorState.selectedTrack, EditorState.selectedClip, model[currentIndex])
        }

        ThemedButton {
            visible: root.clipKind !== "audio" && (root.clipData.blendMode || "normal") !== "normal"
            text: qsTr("Reset to Normal")
            variant: "ghost"
            glyph: Theme.icons.reset
            onClicked: EditorState.setClipBlendMode(
                           EditorState.selectedTrack, EditorState.selectedClip, "normal")
        }
    }
}
