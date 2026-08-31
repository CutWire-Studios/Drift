import QtQuick
import QtQuick.Window
import Drift

// Canvas size, frame rate, and crop. Shared by the header Video dialog and Android Settings.
Column {
    id: root

    spacing: Theme.spacingLg

    signal cropStarted()
    signal layoutChooserOpened()

    property int canvasW: { void EditorState.tracks; return EditorState.projectWidth() }
    property int canvasH: { void EditorState.tracks; return EditorState.projectHeight() }
    property int canvasFps: { void EditorState.tracks; return EditorState.projectFps() }

    readonly property var canvasPresets: [
        { label: qsTr("Custom"), w: 0, h: 0 },
        { label: "1920×1080 (16:9)", w: 1920, h: 1080 },
        { label: "3840×2160 (4K)", w: 3840, h: 2160 },
        { label: "1080×1920 (9:16)", w: 1080, h: 1920 },
        { label: "1080×1080 (1:1)", w: 1080, h: 1080 },
        { label: "1440×1080 (4:3)", w: 1440, h: 1080 }
    ]

    ThemedButton {
        width: parent.width
        variant: "secondary"
        glyph: Theme.icons.ratio
        text: qsTr("Choose layout…")
        tooltip: qsTr("Pick a platform template (YouTube, Instagram, TikTok, …) and quality")
        enabled: !EditorState.canvasCropMode
        onClicked: {
            if (typeof Window !== "undefined" && Window.window && Window.window.openLayoutChooser)
                Window.window.openLayoutChooser()
            root.layoutChooserOpened()
        }
    }

    ThemedComboBox {
        width: parent.width
        enabled: !EditorState.canvasCropMode
        model: root.canvasPresets.map(function (p) { return p.label })
        tooltip: qsTr("Change the video size. Clips keep their current size and position.")
        currentIndex: {
            const presets = root.canvasPresets
            for (var i = 1; i < presets.length; ++i) {
                if (presets[i].w === root.canvasW && presets[i].h === root.canvasH)
                    return i
            }
            return 0
        }
        onActivated: {
            const preset = root.canvasPresets[currentIndex]
            if (preset.w > 0)
                EditorState.setProjectResolution(preset.w, preset.h)
        }
    }

    Row {
        width: parent.width
        spacing: Theme.spacingLg

        Column {
            width: (parent.width - parent.spacing) / 2
            spacing: Theme.spacingSm
            ThemedLabel { text: qsTr("Width") }
            ThemedNumberField {
                width: parent.width
                enabled: !EditorState.canvasCropMode
                from: 16
                to: 7680
                step: 2
                unit: "px"
                value: root.canvasW
                onEdited: v => EditorState.setProjectResolution(v, root.canvasH)
            }
        }

        Column {
            width: (parent.width - parent.spacing) / 2
            spacing: Theme.spacingSm
            ThemedLabel { text: qsTr("Height") }
            ThemedNumberField {
                width: parent.width
                enabled: !EditorState.canvasCropMode
                from: 16
                to: 4320
                step: 2
                unit: "px"
                value: root.canvasH
                onEdited: v => EditorState.setProjectResolution(root.canvasW, v)
            }
        }
    }

    Column {
        width: (parent.width - Theme.spacingLg) / 2
        spacing: Theme.spacingSm
        ThemedLabel { text: qsTr("Frames per second") }
        ThemedNumberField {
            width: parent.width
            enabled: !EditorState.canvasCropMode
            from: 1
            to: 240
            unit: "fps"
            value: root.canvasFps
            onEdited: v => EditorState.setProjectFps(v)
        }
    }

    ThemedButton {
        width: parent.width
        variant: EditorState.canvasCropMode ? "primary" : "secondary"
        glyph: Theme.icons.crop
        text: EditorState.canvasCropMode ? qsTr("Cancel crop") : qsTr("Crop video size")
        tooltip: qsTr("Drag the preview edges to change what’s included")
        onClicked: {
            const starting = !EditorState.canvasCropMode
            EditorState.canvasCropMode = starting
            if (starting)
                root.cropStarted()
        }
    }

    ThemedLabel {
        width: parent.width
        wrapMode: Text.WordWrap
        text: qsTr("Changing size doesn’t shrink your clips — anything outside the new edges is cut off.")
    }

    ThemedLabel {
        width: parent.width
        wrapMode: Text.WordWrap
        text: qsTr("Clips keep their length. A higher rate samples more pictures per second from the same footage.")
    }
}
