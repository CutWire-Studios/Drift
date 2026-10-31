import QtQuick
import Drift
// Crop tool page. "Clip" crops the selected clip's source frame; "Video" crops the canvas itself.
// Both drive a gizmo on the shared preview; ✓ commits the canvas crop (the clip crop lands as it is
// dragged) and everything inside the scaffold's edit group is undone by Cancel. Open with
// props {mode: "video"} to skip the toggle's default.
Item {
    id: root

    property var scaffold: null
    property int trackIndex: -1
    property int clipIndex: -1
    // "clip" or "video"; empty until the first pass picks the default.
    property string mode: ""

    // Aspect chip: ratio is w / h, 0 is free, -1 is "Original" (the source or the current canvas).
    property real ratio: 0

    readonly property var preview: scaffold ? scaffold.previewItem : null
    readonly property var overlay: preview ? preview.toolHost.item : null
    readonly property var clipData: EditorState.selectedClipData
    readonly property bool canCropClip: !!clipData && (clipData.kind === "video" || clipData.kind === "image")
    readonly property bool clipMode: mode === "clip"

    readonly property var ratios: [
        { label: qsTr("Free"), value: 0 },
        { label: qsTr("Original"), value: -1 },
        { label: "1:1", value: 1 },
        { label: "16:9", value: 16 / 9 },
        { label: "9:16", value: 9 / 16 },
        { label: "4:5", value: 4 / 5 },
        { label: "3:4", value: 3 / 4 }
    ]

    function effectiveRatio(value) {
        if (value >= 0)
            return value
        if (root.clipMode)
            return root.overlay ? root.overlay.sourceAspect : 0
        const w = EditorState.projectFile.projectWidth()
        const h = EditorState.projectFile.projectHeight()
        return h > 0 ? w / h : 0
    }

    function pushOverlay() {
        const o = root.overlay
        if (!o)
            return
        if (!root.clipMode)
            o.hostedByPage = true
        o.aspect = root.effectiveRatio(root.ratio)
    }

    function enter(next) {
        root.mode = next
        root.ratio = 0
        const p = EditorState.preview
        if (next === "clip") {
            p.clipCropMode = true
        } else {
            p.canvasCropMode = true
        }
        root.pushOverlay()
    }

    function leave() {
        EditorState.preview.clipCropMode = false
        EditorState.preview.canvasCropMode = false
    }

    function resetAll() {
        root.ratio = 0
        if (root.overlay) {
            root.overlay.aspect = 0
            root.overlay.reset()
        }
        if (root.clipMode)
            EditorState.setClipRotationSnap(EditorState.selectedTrack, EditorState.selectedClip, 0)
    }

    Component.onCompleted: Qt.callLater(function () {
        if (root.mode === "")
            root.mode = root.canCropClip ? "clip" : "video"
        root.enter(root.mode)
    })

    Component.onDestruction: root.leave()

    Connections {
        target: root.preview ? root.preview.toolHost : null
        function onItemChanged() { root.pushOverlay() }
    }

    Connections {
        target: root.scaffold
        function onAboutToApply() {
            if (!root.clipMode && root.overlay)
                root.overlay.commit()
        }
    }

    Column {
        x: Theme.pagePadding
        y: Theme.pagePadding
        width: root.width - Theme.pagePadding * 2
        spacing: Theme.spacingLg

        Row {
            spacing: Theme.spacingSm

            ThemedChip {
                text: qsTr("Clip")
                variant: "outline"
                enabled: root.canCropClip
                selected: root.clipMode
                onClicked: root.enter("clip")
            }

            ThemedChip {
                text: qsTr("Video")
                variant: "outline"
                selected: !root.clipMode
                onClicked: root.enter("video")
            }
        }

        ScrollStrip {
            width: parent.width
            height: Theme.controlHeight

            Row {
                spacing: Theme.spacingSm
                anchors.verticalCenter: parent.verticalCenter

                Repeater {
                    model: root.ratios

                    delegate: ThemedChip {
                        required property var modelData
                        text: modelData.label
                        variant: "outline"
                        selected: root.ratio === modelData.value
                        onClicked: {
                            root.ratio = modelData.value
                            root.pushOverlay()
                        }
                    }
                }
            }
        }

        Item {
            id: rulerRow
            width: parent.width
            height: Theme.controlHeight + Theme.spacingLg

            Item {
                id: ruler
                anchors.left: parent.left
                anchors.right: resetChip.left
                anchors.rightMargin: Theme.spacingMd
                anchors.verticalCenter: parent.verticalCenter
                height: parent.height
                clip: true
                visible: root.clipMode
                enabled: root.clipMode

                readonly property real pxPerDeg: 6
                readonly property real limit: 45
                property bool dragging: false
                property real live: 0
                property real startValue: 0
                readonly property real shown: dragging ? live : EditorState.selectedClipRotationAtPlayhead

                Repeater {
                    model: 91

                    delegate: Rectangle {
                        required property int index
                        readonly property real deg: index - ruler.limit
                        readonly property bool major: index % 5 === 0
                        x: ruler.width / 2 + (deg - ruler.shown) * ruler.pxPerDeg - width / 2
                        anchors.verticalCenter: parent.verticalCenter
                        width: 1
                        height: major ? Theme.spacing2xl : Theme.spacingMd
                        color: Theme.c.androidToolPage.foregroundMuted
                        visible: x > -2 && x < ruler.width
                    }
                }

                Rectangle {
                    anchors.centerIn: parent
                    width: 2
                    height: parent.height * 0.8
                    radius: 1
                    color: Theme.c.androidToolPage.foreground
                }

                Text {
                    anchors.top: parent.top
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: Math.round(ruler.shown * 10) / 10 + "°"
                    color: Theme.c.androidToolPage.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }

                DragHandler {
                    target: null
                    xAxis.enabled: true
                    yAxis.enabled: false
                    onActiveChanged: {
                        if (active) {
                            ruler.startValue = EditorState.selectedClipRotationAtPlayhead
                            ruler.live = ruler.startValue
                            ruler.dragging = true
                            EditorState.beginPreviewDrag(qsTr("Rotate"))
                        } else if (ruler.dragging) {
                            ruler.dragging = false
                            EditorState.commitPreviewDrag()
                        }
                    }
                    onTranslationChanged: {
                        if (!ruler.dragging)
                            return
                        let v = ruler.startValue - translation.x / ruler.pxPerDeg
                        v = Math.max(-ruler.limit, Math.min(ruler.limit, v))
                        if (Math.abs(v) < 0.5)
                            v = 0
                        ruler.live = v
                        EditorState.previewSetClipRotation(EditorState.selectedTrack, EditorState.selectedClip, v)
                    }
                }
            }

            ThemedChip {
                id: resetChip
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Reset")
                variant: "outline"
                onClicked: root.resetAll()
            }
        }
    }
}
