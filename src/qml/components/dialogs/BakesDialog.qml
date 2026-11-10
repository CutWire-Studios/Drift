pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Drift

ThemedDialog {
    id: root

    title: qsTr("Bakes")
    preferredWidth: Theme.dialogWidthLg
    showAccept: false
    rejectText: qsTr("Close")

    readonly property var bakeList: EditorState.bakes.bakes
    readonly property bool hasWorkArea: EditorState.workAreaInSeconds >= 0
                                        && EditorState.workAreaOutSeconds > EditorState.workAreaInSeconds
    readonly property var qualityNames: [qsTr("Compact"), qsTr("Balanced"), qsTr("High")]

    function openDialog() {
        open()
    }

    function timecode(seconds) {
        void EditorState.tracksRevision
        const fps = Math.max(1, EditorState.projectFile.projectFps())
        const frames = Math.round(seconds * fps)
        const h = Math.floor(frames / (fps * 3600))
        const m = Math.floor(frames / (fps * 60)) % 60
        const s = Math.floor(frames / fps) % 60
        const f = frames % fps
        const pad = n => n.toString().padStart(2, "0")
        return pad(h) + ":" + pad(m) + ":" + pad(s) + ":" + pad(f)
    }

    function duration(seconds) {
        const total = Math.max(0, seconds)
        const m = Math.floor(total / 60)
        return m > 0 ? qsTr("%1m %2s").arg(m).arg(Math.round(total - m * 60))
                     : qsTr("%1s").arg(total.toFixed(1))
    }

    function stateLabel(bake) {
        return bake.state === "baking" ? qsTr("Baking %1%").arg(Math.round(bake.progress * 100))
               : bake.state === "stale" ? qsTr("Stale") : qsTr("Fresh")
    }

    function sizeLabel(bytes) {
        return bytes > 0 ? DownloadFormat.formatBytes(bytes) : "—"
    }

    ThemedDialog {
        id: deleteAllConfirm
        title: qsTr("Delete all bakes?")
        preferredWidth: Theme.dialogWidthSm
        acceptText: qsTr("Delete")
        acceptVariant: "destructive"
        acceptOnReturn: false
        onAccepted: EditorState.bakes.deleteAllBakes()

        contentItem: ThemedLabel {
            width: parent ? parent.width : Theme.dialogWidthSm
            size: "sm"
            tone: "default"
            text: qsTr("Playback and export will render these ranges live again. Your project is not changed.")
        }
    }

    ThemedContextMenu {
        id: rowMenu
        property string bakeId: ""
        property bool real: bakeId !== "" && bakeId !== "pending"

        ThemedMenuItem {
            text: qsTr("Re-bake at current quality")
            icon.name: Theme.icons.refresh
            enabled: rowMenu.real && !EditorState.bakes.busy
            onTriggered: EditorState.bakes.rebake(rowMenu.bakeId)
        }
        ThemedMenuItem {
            text: qsTr("Set work area to bake")
            enabled: rowMenu.real
            onTriggered: EditorState.bakes.setWorkAreaToBake(rowMenu.bakeId)
        }
        ThemedMenuSeparator { }
        ThemedMenuItem {
            text: qsTr("Delete")
            icon.name: Theme.icons.trash
            enabled: rowMenu.real
            onTriggered: EditorState.bakes.deleteBake(rowMenu.bakeId)
        }
    }

    readonly property real colRange: 170
    readonly property real colDuration: 70
    readonly property real colQuality: 150
    readonly property real colSize: 70
    readonly property real colState: 80
    readonly property real colActions: 120

    contentItem: Column {
        width: parent ? parent.width : Theme.dialogWidthLg
        spacing: Theme.spacingXl

        RowLayout {
            width: parent.width
            spacing: Theme.spacingLg

            Column {
                Layout.fillWidth: true
                spacing: Theme.spacingSm

                ThemedLabel { text: qsTr("Quality for new bakes") }

                Row {
                    spacing: 0
                    Repeater {
                        model: root.qualityNames
                        delegate: ThemedToggleButton {
                            required property int index
                            required property string modelData
                            text: modelData
                            checked: EditorState.bakes.bakeQuality === index
                            enabled: !EditorState.bakes.busy
                            onClicked: EditorState.bakes.bakeQuality = index
                        }
                    }
                }

                ThemedLabel {
                    text: qsTr("≈ %1 per minute").arg(
                              DownloadFormat.formatBytes(
                                  EditorState.bakes.estimateBytesPerMinute(EditorState.bakes.bakeQuality)))
                }
            }

            Row {
                Layout.alignment: Qt.AlignTop
                spacing: Theme.spacingMd

                ThemedLabel {
                    visible: EditorState.bakes.busy
                    anchors.verticalCenter: parent.verticalCenter
                    tone: "default"
                    text: qsTr("Baking %1%").arg(Math.round(EditorState.bakes.progress * 100))
                }

                ThemedButton {
                    variant: EditorState.bakes.busy ? "secondary" : "primary"
                    text: EditorState.bakes.busy ? qsTr("Cancel") : qsTr("Bake work area")
                    enabled: EditorState.bakes.busy || root.hasWorkArea
                    tooltip: root.hasWorkArea || EditorState.bakes.busy ? ""
                             : qsTr("Set a work area on the timeline first")
                    onClicked: EditorState.bakes.busy ? EditorState.bakes.cancelBake()
                                                      : EditorState.bakes.bakeWorkArea()
                }
            }
        }

        Column {
            id: emptyState
            visible: root.bakeList.length === 0
            width: parent.width
            spacing: Theme.spacingSm
            topPadding: Theme.spacing2xl
            bottomPadding: Theme.spacing2xl

            ThemedLabel {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                size: "sm"
                text: {
                    const inKeys = EditorState.shortcutFor("markIn")
                    const outKeys = EditorState.shortcutFor("markOut")
                    const i = inKeys.length > 0 ? Theme.shortcutDisplay(inKeys) : qsTr("I")
                    const o = outKeys.length > 0 ? Theme.shortcutDisplay(outKeys) : qsTr("O")
                    return qsTr("Bake a finished part of your timeline to play and export it smoothly. Set a work area with %1 and %2, then press Bake.")
                           .arg(i).arg(o)
                }
            }
        }

        Column {
            visible: root.bakeList.length > 0
            width: parent.width
            spacing: 0

            Row {
                width: parent.width
                spacing: Theme.spacingMd
                leftPadding: Theme.spacingMd
                height: Theme.controlHeightSm

                ThemedLabel { width: root.colRange; text: qsTr("Range"); anchors.verticalCenter: parent.verticalCenter }
                ThemedLabel { width: root.colDuration; text: qsTr("Duration"); anchors.verticalCenter: parent.verticalCenter }
                ThemedLabel { width: root.colQuality; text: qsTr("Quality"); anchors.verticalCenter: parent.verticalCenter }
                ThemedLabel { width: root.colSize; text: qsTr("Size"); anchors.verticalCenter: parent.verticalCenter }
                ThemedLabel { width: root.colState; text: qsTr("State"); anchors.verticalCenter: parent.verticalCenter }
            }

            Rectangle {
                width: parent.width
                height: 1
                color: Theme.c.bakesDialogRow.divider
            }

            ListView {
                id: table
                width: parent.width
                height: Math.min(contentHeight, 260)
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: root.bakeList
                ScrollBar.vertical: AppScrollBar { }

                delegate: Rectangle {
                    id: row
                    required property var modelData
                    readonly property bool real: modelData.id !== "pending"

                    width: ListView.view.width
                    height: Theme.controlHeight
                    radius: Theme.c.bakesDialogRow.radius
                    color: rowMouse.containsMouse ? Theme.c.bakesDialogRow.backgroundHover : "transparent"

                    MouseArea {
                        id: rowMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: row.real ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: {
                            if (!row.real)
                                return
                            EditorState.playheadSeconds = row.modelData.start
                            EditorState.bakes.setWorkAreaToBake(row.modelData.id)
                        }
                    }

                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacingMd
                        spacing: Theme.spacingMd

                        ThemedLabel {
                            width: root.colRange
                            anchors.verticalCenter: parent.verticalCenter
                            tone: "default"
                            wrapMode: Text.NoWrap
                            elide: Text.ElideRight
                            text: root.timecode(row.modelData.start) + " – " + root.timecode(row.modelData.end)
                        }
                        ThemedLabel {
                            width: root.colDuration
                            anchors.verticalCenter: parent.verticalCenter
                            tone: "default"
                            wrapMode: Text.NoWrap
                            text: root.duration(row.modelData.end - row.modelData.start)
                        }
                        ThemedLabel {
                            width: root.colQuality
                            anchors.verticalCenter: parent.verticalCenter
                            tone: "default"
                            wrapMode: Text.NoWrap
                            elide: Text.ElideRight
                            text: row.modelData.qualityLabel + " · " + row.modelData.width + "×" + row.modelData.height
                        }
                        ThemedLabel {
                            width: root.colSize
                            anchors.verticalCenter: parent.verticalCenter
                            tone: "default"
                            wrapMode: Text.NoWrap
                            text: root.sizeLabel(row.modelData.bytes)
                        }
                        ThemedLabel {
                            width: root.colState
                            anchors.verticalCenter: parent.verticalCenter
                            tone: row.modelData.state === "fresh" ? "muted" : "default"
                            wrapMode: Text.NoWrap
                            text: root.stateLabel(row.modelData)
                        }
                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: Theme.spacingXs

                            ThemedButton {
                                visible: row.modelData.state === "stale"
                                text: qsTr("Re-bake")
                                enabled: !EditorState.bakes.busy
                                onClicked: EditorState.bakes.rebake(row.modelData.id)
                            }
                            IconButton {
                                glyph: Theme.icons.ellipsis
                                tooltip: qsTr("More")
                                enabled: row.real
                                onClicked: {
                                    rowMenu.bakeId = row.modelData.id
                                    rowMenu.popup()
                                }
                            }
                            IconButton {
                                glyph: Theme.icons.trash
                                tooltip: qsTr("Delete bake")
                                enabled: row.real
                                onClicked: EditorState.bakes.deleteBake(row.modelData.id)
                            }
                        }
                    }
                }
            }
        }

        RowLayout {
            width: parent.width
            spacing: Theme.spacingMd

            ThemedLabel {
                Layout.fillWidth: true
                tone: "default"
                text: qsTr("%n bake(s) · %1", "", root.bakeList.length)
                      .arg(DownloadFormat.formatBytes(EditorState.bakes.totalBytes) || qsTr("0 B"))
            }
            ThemedButton {
                text: qsTr("Re-bake all stale")
                enabled: EditorState.bakes.staleCount > 0 && !EditorState.bakes.busy
                onClicked: EditorState.bakes.rebakeAllStale()
            }
            ThemedButton {
                text: qsTr("Delete all bakes")
                enabled: root.bakeList.length > 0
                onClicked: deleteAllConfirm.open()
            }
        }
    }
}
