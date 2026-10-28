pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Drift

PanelFrame {
    id: root

    property bool shown: false
    property bool held: false
    property string mode: "all"

    readonly property var scopes: EditorState.color.scopes
    readonly property var modes: [
        { id: "all", label: qsTr("All"), shortLabel: qsTr("All") },
        { id: "histogram", label: qsTr("Histogram"), shortLabel: qsTr("Histo") },
        { id: "waveform", label: qsTr("Waveform"), shortLabel: qsTr("Wave") },
        { id: "parade", label: qsTr("Parade"), shortLabel: qsTr("Parade") },
        { id: "vectorscope", label: qsTr("Vectorscope"), shortLabel: qsTr("Vector") }
    ]
    readonly property var visibleKinds: {
        if (mode === "all")
            return ["histogram", "waveform", "vectorscope"]
        return [mode]
    }
    readonly property bool showParade: mode === "parade"

    function syncHeld() {
        if (root.shown && !root.held) {
            scopes.acquire()
            root.held = true
        } else if (!root.shown && root.held) {
            scopes.release()
            root.held = false
        }
    }

    onShownChanged: syncHeld()
    Component.onCompleted: syncHeld()
    Component.onDestruction: {
        if (held)
            scopes.release()
    }

    function applyParade() {
        const want = mode === "parade"
        if (scopes.parade !== want)
            scopes.parade = want
    }
    onModeChanged: applyParade()

    AssetsPanelHeader {
        id: header
        width: parent.width
        height: Theme.panelHeaderHeight
        title: qsTr("Scopes")
    }

    Flow {
        id: selector
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spacingMd
        spacing: Theme.spacingSm

        Repeater {
            model: root.modes
            delegate: ThemedChip {
                required property var modelData
                text: root.width < 260 ? modelData.shortLabel : modelData.label
                tooltip: root.width < 260 ? modelData.label : ""
                selected: root.mode === modelData.id
                onClicked: root.mode = modelData.id
            }
        }
    }

    Text {
        anchors.top: selector.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spacingLg
        visible: root.scopes.serial === 0
        verticalAlignment: Text.AlignVCenter
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        text: qsTr("Scopes update from the preview. Play or move the playhead.")
        color: Theme.c.scopes.label
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeSm
    }

    GridLayout {
        id: grid
        readonly property real minTile: 72
        readonly property bool twoCol: columns === 2
        anchors.top: selector.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spacingMd
        visible: root.scopes.serial > 0
        columns: root.mode !== "all" ? 1
                 : (width >= height * 1.5 || height < 3 * minTile + 2 * rowSpacing) ? 3
                 : width >= height * 0.8 ? 2 : 1
        rowSpacing: Theme.spacingMd
        columnSpacing: Theme.spacingMd

        Repeater {
            model: root.visibleKinds
            delegate: Rectangle {
                id: tile
                required property string modelData
                required property int index
                readonly property bool isVector: modelData === "vectorscope"
                readonly property bool hasScale: root.showParade || modelData === "waveform"
                Layout.fillWidth: !(grid.columns === 3 && isVector && root.mode === "all")
                Layout.fillHeight: true
                Layout.minimumHeight: 40
                Layout.preferredWidth: Layout.fillWidth ? -1 : height
                Layout.row: grid.twoCol ? (isVector ? 0 : index) : -1
                Layout.column: grid.twoCol ? (isVector ? 1 : 0) : -1
                Layout.rowSpan: grid.twoCol && isVector ? 2 : 1
                radius: Theme.radiusMd
                color: Theme.c.scopes.background
                clip: true

                Image {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingSm
                    source: "image://color/scope/" + (root.showParade ? "waveform" : tile.modelData)
                            + "?s=" + root.scopes.serial
                    cache: false
                    fillMode: tile.isVector ? Image.PreserveAspectFit : Image.Stretch
                    smooth: true
                }

                Repeater {
                    model: tile.hasScale ? [0, 25, 50, 75, 100] : []
                    delegate: Text {
                        required property int modelData
                        x: Theme.spacingSm + 2
                        y: Math.min(tile.height - Theme.spacingSm - height,
                                    Math.max(Theme.spacingSm,
                                             Theme.spacingSm + (1 - modelData / 100)
                                             * (tile.height - Theme.spacingSm * 2 - 1) - height / 2))
                        text: modelData
                        color: Theme.c.scopes.label
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                    }
                }

                Text {
                    anchors.left: tile.modelData === "waveform" ? undefined : parent.left
                    anchors.right: tile.modelData === "waveform" ? parent.right : undefined
                    anchors.top: parent.top
                    anchors.margins: Theme.spacingSm
                    visible: root.mode === "all"
                    text: tile.modelData === "histogram" ? qsTr("Histogram")
                          : tile.modelData === "waveform" ? qsTr("Waveform") : qsTr("Vectorscope")
                    color: Theme.c.scopes.label
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
            }
        }
    }
}
