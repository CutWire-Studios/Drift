pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Drift

Item {
    id: root

    property url source
    property int imageWidth: 0
    property int imageHeight: 0
    property string path: ""
    property string heading: ""
    property string details: ""
    property real seekSeconds: -1
    property string saveName: "image"

    property bool fit: true
    property real manualZoom: 1
    readonly property real fitZoom: imageWidth > 0 && imageHeight > 0 && flick.width > 0 && flick.height > 0
                                    ? Math.min(flick.width / imageWidth, flick.height / imageHeight, 1) : 1
    readonly property real shownZoom: fit ? fitZoom : manualZoom

    function zoomAt(factor, vx, vy) {
        const cur = root.shownZoom
        const next = Math.max(0.05, Math.min(16, cur * factor))
        const ix = (flick.contentX + vx - img.x) / cur
        const iy = (flick.contentY + vy - img.y) / cur
        root.fit = false
        root.manualZoom = next
        flick.contentX = Math.max(0, Math.min(flick.contentWidth - flick.width, img.x + ix * next - vx))
        flick.contentY = Math.max(0, Math.min(flick.contentHeight - flick.height, img.y + iy * next - vy))
    }

    function actualSize() {
        root.zoomAt(1 / root.shownZoom, flick.width / 2, flick.height / 2)
    }

    function saveAs() {
        const dot = root.path.lastIndexOf(".")
        const suffix = dot > 0 ? root.path.substring(dot + 1) : "png"
        const url = FileDialogs.saveFile(qsTr("Save image"), [
            suffix === "png" ? qsTr("PNG image (*.png)") : qsTr("JPEG image (*.jpg *.jpeg)")
        ], root.saveName, suffix)
        if (!url || url.toString() === "")
            return
        if (EditorState.mcp.imageLog.saveCopy(root.path, url))
            Toasts.success(qsTr("Image saved."))
        else
            Toasts.error(qsTr("Couldn’t save that image."))
    }

    onSourceChanged: root.fit = true

    Item {
        id: header
        width: parent.width
        height: Math.max(titleColumn.height, actions.height) + Theme.spacingSm * 2

        Column {
            id: titleColumn
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingSm
            anchors.right: actions.left
            anchors.rightMargin: Theme.spacingSm
            anchors.verticalCenter: parent.verticalCenter

            Text {
                width: parent.width
                text: root.heading
                visible: text.length > 0
                elide: Text.ElideRight
                color: Theme.c.imageViewer.foreground
                font.family: Theme.monoFontFamily
                font.pixelSize: Theme.fontSizeSm
                font.weight: Font.Medium
            }

            Text {
                width: parent.width
                text: root.details
                visible: text.length > 0
                elide: Text.ElideRight
                color: Theme.c.imageViewer.foregroundMuted
                font.family: Theme.monoFontFamily
                font.pixelSize: Theme.fontSizeXs
            }
        }

        Row {
            id: actions
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingSm
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingXs

            ThemedButton {
                anchors.verticalCenter: parent.verticalCenter
                visible: root.seekSeconds >= 0
                text: qsTr("Go to")
                variant: "secondary"
                tooltip: qsTr("Move the playhead to where this image starts")
                onClicked: EditorState.playheadSeconds = root.seekSeconds
            }

            IconButton {
                glyph: Theme.icons.maximize
                variant: "text"
                active: root.fit
                tooltip: qsTr("Fit")
                onClicked: root.fit = true
            }

            IconButton {
                glyph: Theme.icons.zoomIn
                variant: "text"
                tooltip: qsTr("Actual size")
                onClicked: root.actualSize()
            }

            IconButton {
                glyph: Theme.icons.folder
                variant: "text"
                tooltip: qsTr("Open file location")
                enabled: root.path.length > 0
                onClicked: Qt.openUrlExternally(EditorState.mcp.imageLog.folderUrl(root.path))
            }

            IconButton {
                glyph: Theme.icons.copy
                variant: "text"
                tooltip: qsTr("Copy path")
                enabled: root.path.length > 0
                onClicked: {
                    EditorState.mcp.imageLog.copyPath(root.path)
                    Toasts.success(qsTr("Copied to clipboard"))
                }
            }

            IconButton {
                glyph: Theme.icons.save
                variant: "text"
                tooltip: qsTr("Save as…")
                enabled: root.path.length > 0
                onClicked: root.saveAs()
            }
        }

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.c.imageViewer.border
        }
    }

    Rectangle {
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        width: parent.width
        color: Theme.c.imageViewer.canvas
        clip: true

        Flickable {
            id: flick
            anchors.fill: parent
            boundsBehavior: Flickable.StopAtBounds
            contentWidth: Math.max(width, img.width)
            contentHeight: Math.max(height, img.height)
            ScrollBar.vertical: AppScrollBar {}
            ScrollBar.horizontal: AppScrollBar {}

            Image {
                id: img
                x: (flick.contentWidth - width) / 2
                y: (flick.contentHeight - height) / 2
                width: root.imageWidth * root.shownZoom
                height: root.imageHeight * root.shownZoom
                source: root.source
                asynchronous: true
                smooth: true
                mipmap: true
            }

            WheelHandler {
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onWheel: (event) => {
                    const p = point.position
                    root.zoomAt(Math.pow(1.0015, event.angleDelta.y), p.x - flick.contentX, p.y - flick.contentY)
                }
            }

            PinchHandler {
                id: pinch
                target: null
                property real startZoom: 1
                onActiveChanged: if (active) startZoom = root.shownZoom
                onActiveScaleChanged: {
                    if (active) {
                        const p = centroid.position
                        root.zoomAt(startZoom * activeScale / root.shownZoom, p.x - flick.contentX, p.y - flick.contentY)
                    }
                }
            }
        }

        BusyIndicator {
            anchors.centerIn: parent
            running: img.status === Image.Loading
            visible: running
        }
    }
}
