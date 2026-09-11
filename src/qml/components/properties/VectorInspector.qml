import QtQuick
import QtQuick.Controls.Basic
import Drift
import ".."

// Lottie / SVG clip inspector: what the document is, how it plays, and the slots it exposes.
Item {
    id: root

    property int clipDataRevision: 0
    readonly property var clipData: {
        void clipDataRevision
        return EditorState.selectedClipData
    }
    readonly property bool hasSelection: !!clipData && Object.keys(clipData).length > 0
    readonly property bool hasVector: hasSelection && clipData.kind === "vector" && !!clipData.vector
    readonly property var vector: hasVector ? clipData.vector : ({
                                                  "kind": "lottie", "inline": true, "path": "",
                                                  "width": 0, "height": 0, "fps": 0, "durationSec": 0,
                                                  "title": "", "fit": "contain", "loop": "hold",
                                                  "offset": 0, "slots": ({})
                                              })
    // Declared slots and the parse report are read once per change, not per binding: both parse
    // the document.
    property var slotRows: []
    property var report: ({})

    function refresh() {
        if (!root.hasVector) {
            root.slotRows = []
            root.report = ({})
            return
        }
        root.slotRows = EditorState.vectorSlots(EditorState.selectedTrack, EditorState.selectedClip)
        root.report = EditorState.inspectVectorClip(EditorState.selectedTrack, EditorState.selectedClip)
        if (offsetField && !offsetField.activeFocus)
            offsetField.value = root.vector.offset
    }

    function setOption(key, value) {
        const patch = {}
        patch[key] = value
        EditorState.setVectorOptions(EditorState.selectedTrack, EditorState.selectedClip, patch)
    }

    function setSlot(id, value) {
        const error = EditorState.setVectorSlot(EditorState.selectedTrack, EditorState.selectedClip, id, value)
        if (error.length > 0)
            EditorState.setLastMessage(error, "error")
    }

    function replaceDocument() {
        const url = FileDialogs.openFile(qsTr("Replace Animation"),
                                         [qsTr("Lottie or SVG (*.json *.svg)")])
        if (url == "")
            return
        const path = url.toString().replace(/^file:\/\//, "")
        const reply = EditorState.setVectorSource(EditorState.selectedTrack, EditorState.selectedClip, path, {})
        if (!reply.ok)
            EditorState.setLastMessage(reply.error || qsTr("Could not load the document"), "error")
    }

    height: contentCol.height
    implicitHeight: contentCol.height

    Connections {
        target: EditorState
        function onSelectionChanged() { root.clipDataRevision++; root.refresh() }
        function onSelectedClipDataChanged() { root.clipDataRevision++; root.refresh() }
        function onTracksChanged() { root.clipDataRevision++; root.refresh() }
    }

    Component.onCompleted: refresh()

    Column {
        id: contentCol
        width: root.width
        spacing: Theme.spacingXl

        // ----- Document ----------------------------------------------------
        Column {
            width: parent.width
            spacing: Theme.spacingXs

            ThemedLabel { text: root.vector.kind === "svg" ? qsTr("SVG drawing") : qsTr("Lottie animation") }

            ThemedLabel {
                width: parent.width
                opacity: 0.8
                elide: Text.ElideMiddle
                text: {
                    const v = root.vector
                    const size = v.width > 0 ? qsTr("%1×%2").arg(v.width).arg(v.height) : ""
                    const timing = v.durationSec > 0
                                   ? qsTr("%1 s at %2 fps").arg(Number(v.durationSec).toFixed(2)).arg(Math.round(v.fps))
                                   : qsTr("still")
                    const where = v.inline ? qsTr("inline document") : v.path
                    return [v.title, size, timing, where].filter(s => s && s.length > 0).join(" · ")
                }
            }

            ThemedButton {
                text: qsTr("Replace document…")
                tooltip: qsTr("Load another .json or .svg; position, length, fit and loop stay")
                onClicked: root.replaceDocument()
            }
        }

        // ----- Playback ----------------------------------------------------
        Column {
            width: parent.width
            spacing: Theme.spacingSm

            ThemedLabel { text: qsTr("Playback") }

            Row {
                width: parent.width
                spacing: 8

                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: 4
                    Text {
                        text: qsTr("Fit")
                        color: Theme.mutedForeground
                        font.pixelSize: Theme.fontSizeXs
                        font.family: Theme.fontFamily
                    }
                    ThemedComboBox {
                        width: parent.width
                        readonly property var ids: ["contain", "cover", "stretch"]
                        model: [qsTr("Contain"), qsTr("Cover"), qsTr("Stretch")]
                        tooltip: qsTr("How the drawing fills the clip box")
                        currentIndex: Math.max(0, ids.indexOf(root.vector.fit))
                        onActivated: root.setOption("fit", ids[currentIndex])
                    }
                }

                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: 4
                    visible: root.vector.durationSec > 0
                    Text {
                        text: qsTr("After the end")
                        color: Theme.mutedForeground
                        font.pixelSize: Theme.fontSizeXs
                        font.family: Theme.fontFamily
                    }
                    ThemedComboBox {
                        width: parent.width
                        readonly property var ids: ["hold", "loop", "pingpong", "hide"]
                        model: [qsTr("Hold last frame"), qsTr("Loop"), qsTr("Ping-pong"), qsTr("Hide")]
                        tooltip: qsTr("What plays once the animation has run its length")
                        currentIndex: Math.max(0, ids.indexOf(root.vector.loop))
                        onActivated: root.setOption("loop", ids[currentIndex])
                    }
                }
            }

            Column {
                width: parent.width
                spacing: 4
                visible: root.vector.durationSec > 0
                Text {
                    text: qsTr("Start offset")
                    color: Theme.mutedForeground
                    font.pixelSize: Theme.fontSizeXs
                    font.family: Theme.fontFamily
                }
                ThemedNumberField {
                    id: offsetField
                    width: parent.width
                    unit: "s"
                    decimals: 2
                    step: 0.1
                    from: -3600
                    to: 3600
                    onEdited: v => root.setOption("offset", v)
                }
            }
        }

        // ----- Slots -------------------------------------------------------
        Column {
            width: parent.width
            spacing: Theme.spacingSm
            visible: root.slotRows.length > 0

            ThemedLabel { text: qsTr("Slots") }
            ThemedLabel {
                width: parent.width
                opacity: 0.8
                text: qsTr("Template inputs the animation declares. Overrides are per clip.")
            }

            Repeater {
                model: root.slotRows
                delegate: Column {
                    required property var modelData
                    width: parent.width
                    spacing: 4

                    Row {
                        width: parent.width
                        spacing: 6
                        Text {
                            text: modelData.id + " · " + modelData.type
                            color: Theme.mutedForeground
                            font.pixelSize: Theme.fontSizeXs
                            font.family: Theme.fontFamily
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        ThemedChip {
                            visible: modelData.value !== undefined
                            text: qsTr("Reset")
                            onClicked: root.setSlot(modelData.id, null)
                        }
                    }

                    ColorSwatchField {
                        visible: modelData.type === "color"
                        hex: modelData.value !== undefined ? modelData.value : "#ffffffff"
                        tooltip: qsTr("Slot colour")
                        onEdited: value => root.setSlot(modelData.id, value)
                    }
                    ThemedNumberField {
                        visible: modelData.type === "scalar"
                        width: parent.width
                        decimals: 2
                        step: 0.1
                        value: modelData.value !== undefined ? Number(modelData.value) : 0
                        onEdited: v => root.setSlot(modelData.id, v)
                    }
                    ThemedTextField {
                        visible: modelData.type === "text"
                        width: parent.width
                        text: modelData.value !== undefined ? modelData.value : ""
                        placeholderText: qsTr("Text for this slot")
                        onEditingFinished: root.setSlot(modelData.id, text)
                    }
                    Row {
                        visible: modelData.type === "vec2"
                        width: parent.width
                        spacing: 8
                        ThemedNumberField {
                            id: vecX
                            width: (parent.width - parent.spacing) / 2
                            decimals: 2
                            step: 1
                            value: modelData.value !== undefined ? Number(modelData.value[0]) : 0
                            onEdited: v => root.setSlot(modelData.id, [v, vecY.value])
                        }
                        ThemedNumberField {
                            id: vecY
                            width: (parent.width - parent.spacing) / 2
                            decimals: 2
                            step: 1
                            value: modelData.value !== undefined ? Number(modelData.value[1]) : 0
                            onEdited: v => root.setSlot(modelData.id, [vecX.value, v])
                        }
                    }
                    ThemedButton {
                        visible: modelData.type === "image"
                        text: modelData.value !== undefined ? qsTr("Change image…") : qsTr("Choose image…")
                        onClicked: {
                            const url = FileDialogs.openFile(qsTr("Slot Image"),
                                                             [qsTr("Images (*.png *.jpg *.jpeg *.webp)")])
                            if (url != "")
                                root.setSlot(modelData.id, url.toString().replace(/^file:\/\//, ""))
                        }
                    }
                }
            }
        }

        // ----- Report ------------------------------------------------------
        Column {
            width: parent.width
            spacing: Theme.spacingXs
            visible: (root.report.unsupported && root.report.unsupported.length > 0)
                     || (root.report.expressions && root.report.expressions.length > 0)

            ThemedLabel { text: qsTr("Not rendered") }
            Repeater {
                model: (root.report.expressions || []).map(p => qsTr("Expression on %1 (drawn static)").arg(p))
                       .concat(root.report.unsupported || [])
                delegate: ThemedLabel {
                    required property string modelData
                    width: parent.width
                    opacity: 0.8
                    wrapMode: Text.Wrap
                    text: "• " + modelData
                }
            }
        }
    }
}
