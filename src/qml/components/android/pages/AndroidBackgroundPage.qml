import QtQuick
import Drift
// Canvas background. The project has one background (solid colour, blur or transparent) behind every
// clip, so there is no "apply to all". There is no image or gradient background in the engine: an
// image is added as a clip on a new bottom track covering the timeline, scaled to fill the canvas.
Item {
    id: root

    property var scaffold: null
    property int trackIndex: -1
    property int clipIndex: -1

    // "color", "blur" or "image"; the page opens on the project's current kind.
    property string section: EditorState.projectFile.background.kind === "blur" ? "blur" : "color"
    // Id of the image clip this page added; a second pick replaces it.
    property string imageClipId: ""

    readonly property var swatches: [
        "#000000", "#ffffff", "#8e8e93", "#ff3b30", "#ff9500", "#ffcc00",
        "#34c759", "#00c7be", "#0a84ff", "#5e5ce6", "#bf5af2", "#ff2d55",
        "#1c1c1e", "#f2f2f7", "#5d4037", "#1b5e20", "#0d47a1", "#4a148c"
    ]
    readonly property var background: EditorState.projectFile.background
    readonly property string currentHex: background.color || "#ff000000"

    function setColor(hex) {
        EditorState.projectFile.setBackground({ kind: "color", color: hex })
    }

    function discardImage() {
        if (root.imageClipId === "")
            return
        const at = EditorState.locateClip(root.imageClipId)
        if (at.length === 2) {
            EditorState.selectClip(at[0], at[1])
            EditorState.deleteSelectedClip()
        }
        root.imageClipId = ""
    }

    function pickImage() {
        const url = FileDialogs.openFile(qsTr("Choose background image"),
                                         [qsTr("Images (*.png *.jpg *.jpeg *.webp *.bmp)")])
        if (url === "")
            return
        const duration = EditorState.durationSeconds
        MediaImport.importUrls([url], false, function (added) {
            if (added > 0)
                root.placeImage(AssetLibrary.count - added, duration)
        })
    }

    function placeImage(assetIndex, duration) {
        root.discardImage()
        EditorState.addClipFromAssetOnNewTrackAt(assetIndex, EditorState.tracks.length, 0)
        const t = EditorState.selectedTrack
        const c = EditorState.selectedClip
        const clip = EditorState.clipAt(t, c)
        if (!clip.id)
            return
        root.imageClipId = clip.id
        if (duration > 0)
            EditorState.setClipDuration(t, c, duration)
        const asset = AssetLibrary.assetAt(assetIndex)
        const cw = EditorState.projectFile.projectWidth()
        const ch = EditorState.projectFile.projectHeight()
        if (asset.width > 0 && asset.height > 0) {
            const s = Math.max(cw / asset.width, ch / asset.height)
            const w = asset.width * s
            const h = asset.height * s
            EditorState.beginPreviewDrag(qsTr("Fill canvas"))
            EditorState.previewSetClipRect(t, c, (cw - w) / 2, (ch - h) / 2, w, h)
            EditorState.commitPreviewDrag()
        }
    }

    Column {
        x: Theme.pagePadding
        y: Theme.pagePadding
        width: root.width - Theme.pagePadding * 2
        spacing: Theme.spacingLg

        Row {
            spacing: Theme.spacingSm

            Repeater {
                model: [
                    { id: "color", label: qsTr("Solid") },
                    { id: "blur", label: qsTr("Blur") },
                    { id: "image", label: qsTr("Image") }
                ]

                delegate: ThemedChip {
                    required property var modelData
                    text: modelData.label
                    variant: "outline"
                    selected: root.section === modelData.id
                    onClicked: {
                        root.section = modelData.id
                        if (modelData.id === "blur" && root.background.kind !== "blur")
                            EditorState.projectFile.setBackground({ kind: "blur" })
                        else if (modelData.id === "color" && root.background.kind !== "color")
                            EditorState.projectFile.setBackground({ kind: "color" })
                    }
                }
            }
        }

        Column {
            width: parent.width
            spacing: Theme.spacingMd
            visible: root.section === "color"

            ScrollStrip {
                width: parent.width
                height: 44

                Row {
                    spacing: Theme.spacingMd
                    anchors.verticalCenter: parent.verticalCenter

                    Repeater {
                        model: root.swatches

                        delegate: Rectangle {
                            required property string modelData
                            readonly property bool current: root.background.kind === "color"
                                                            && Qt.colorEqual(modelData, root.currentHex)
                            width: 36
                            height: 36
                            radius: width / 2
                            color: modelData
                            border.width: current ? 3 : Theme.c.androidToolPage.borderWidth
                            border.color: current ? Theme.c.androidToolPage.foreground
                                                  : Theme.c.androidToolPage.border

                            TapHandler {
                                onTapped: {
                                    Haptics.press()
                                    root.setColor(parent.modelData)
                                }
                            }
                        }
                    }
                }
            }

            ColorSwatchField {
                hex: root.currentHex
                onEdited: value => root.setColor(value)
            }
        }

        ThemedSlider {
            width: parent.width
            visible: root.section === "blur"
            label: qsTr("Blur strength")
            from: 1
            to: 100
            stepSize: 1
            valueFormatter: function (v) { return Math.round(v) }
            value: root.background.blurStrength || 20
            onPressedChanged: {
                if (!pressed)
                    EditorState.projectFile.setBackground({ kind: "blur", blurStrength: value })
            }
        }

        Column {
            width: parent.width
            spacing: Theme.spacingMd
            visible: root.section === "image"

            ThemedButton {
                text: root.imageClipId === "" ? qsTr("Choose image") : qsTr("Choose another image")
                variant: "secondary"
                onClicked: root.pickImage()
            }

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("The image is added on a new bottom track for the length of the video, scaled to fill the frame.")
                color: Theme.c.androidToolPage.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
        }
    }
}
