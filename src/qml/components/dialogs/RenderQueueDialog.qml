pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Drift

// The render queue and the farm monitor. Jobs are frozen copies of the timeline, so this stays
// usable while the project is edited or closed.
ThemedDialog {
    id: root

    title: qsTr("Render queue")
    preferredWidth: Theme.dialogWidthLg
    showAccept: false
    rejectText: qsTr("Close")

    property string tab: "queue"
    property string expandedJob: ""

    function openDialog() {
        open()
    }

    function stateLabel(state) {
        switch (state) {
        case "held": return qsTr("Waiting")
        case "queued": return qsTr("Queued")
        case "running": return qsTr("Rendering")
        case "done": return qsTr("Done")
        case "failed": return qsTr("Failed")
        case "cancelled": return qsTr("Cancelled")
        }
        return state
    }

    function stateColor(state) {
        return state === "done" ? Theme.c.renderQueueRow.done
             : state === "failed" ? Theme.c.renderQueueRow.failed
             : state === "held" ? Theme.c.renderQueueRow.held
             : Theme.c.themedLabel.foreground
    }

    function fileName(path) {
        return path.substring(path.lastIndexOf("/") + 1)
    }

    contentItem: Column {
        width: parent ? parent.width : Theme.dialogWidthLg
        spacing: Theme.spacingXl

        Row {
            spacing: 0
            ThemedToggleButton {
                text: qsTr("Queue")
                checked: root.tab === "queue"
                onClicked: root.tab = "queue"
            }
            ThemedToggleButton {
                text: qsTr("Farm")
                checked: root.tab === "farm"
                onClicked: root.tab = "farm"
            }
        }

        // --- Queue ----------------------------------------------------------------------------
        Column {
            visible: root.tab === "queue"
            width: parent.width
            spacing: Theme.spacingLg

            Flow {
                width: parent.width
                spacing: Theme.spacingLg

                ThemedButton {
                    variant: "primary"
                    text: qsTr("Start all")
                    enabled: RenderQueue.count > 0
                    onClicked: RenderQueue.startAll()
                }
                ThemedButton {
                    text: RenderQueue.paused ? qsTr("Resume") : qsTr("Pause")
                    tooltip: qsTr("Finish the current render, then hold the rest")
                    onClicked: RenderQueue.paused = !RenderQueue.paused
                }
                ThemedButton {
                    text: qsTr("Clear finished")
                    onClicked: RenderQueue.clearFinished()
                }
                ThemedSwitch {
                    text: qsTr("Start immediately")
                    checked: RenderQueue.autoStart
                    onToggled: RenderQueue.autoStart = checked
                }
                ThemedSwitch {
                    text: qsTr("Render in a separate process")
                    checked: RenderQueue.useWorkerProcess
                    onToggled: RenderQueue.useWorkerProcess = checked
                }
            }

            ThemedLabel {
                visible: RenderQueue.count === 0
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                size: "sm"
                topPadding: Theme.spacing2xl
                bottomPadding: Theme.spacing2xl
                text: qsTr("Nothing queued. Choose Add to queue in the Export dialog to render in the background.")
            }

            ListView {
                id: jobList
                visible: RenderQueue.count > 0
                width: parent.width
                height: Math.min(contentHeight, 360)
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: RenderQueue
                ScrollBar.vertical: AppScrollBar { }

                delegate: Column {
                    id: row
                    required property string jobId
                    required property string name
                    required property string jobState
                    required property double progress
                    required property string error
                    required property string output
                    required property string target
                    required property string log
                    required property string worker
                    required property int chunkDone
                    required property int chunkTotal
                    readonly property bool expanded: root.expandedJob === jobId

                    width: ListView.view.width

                    Rectangle {
                        width: parent.width
                        height: 1
                        color: Theme.c.renderQueueRow.divider
                    }

                    Rectangle {
                        width: parent.width
                        height: rowContent.implicitHeight + 2 * Theme.spacingMd
                        radius: Theme.c.renderQueueRow.radius
                        color: rowMouse.containsMouse ? Theme.c.renderQueueRow.backgroundHover : "transparent"

                        MouseArea {
                            id: rowMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: root.expandedJob = row.expanded ? "" : row.jobId
                        }

                        ColumnLayout {
                            id: rowContent
                            anchors.fill: parent
                            anchors.margins: Theme.spacingMd
                            spacing: Theme.spacingSm

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spacingMd

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    ThemedLabel {
                                        Layout.fillWidth: true
                                        tone: "default"
                                        elide: Text.ElideRight
                                        wrapMode: Text.NoWrap
                                        text: row.name
                                    }
                                    ThemedLabel {
                                        Layout.fillWidth: true
                                        size: "xs"
                                        elide: Text.ElideMiddle
                                        wrapMode: Text.NoWrap
                                        text: row.output
                                    }
                                }

                                ThemedLabel {
                                    color: root.stateColor(row.jobState)
                                    wrapMode: Text.NoWrap
                                    text: row.jobState === "running"
                                          ? qsTr("%1 %2%").arg(root.stateLabel(row.jobState)).arg(Math.round(row.progress * 100))
                                          : root.stateLabel(row.jobState)
                                }

                                ThemedLabel {
                                    visible: row.target === "farm"
                                    wrapMode: Text.NoWrap
                                    text: row.chunkTotal > 0 ? qsTr("Farm %1/%2").arg(row.chunkDone).arg(row.chunkTotal)
                                                             : qsTr("Farm")
                                }

                                IconButton {
                                    visible: row.jobState === "held"
                                    glyph: Theme.icons.play
                                    tooltip: qsTr("Start")
                                    onClicked: RenderQueue.start(row.jobId)
                                }
                                IconButton {
                                    visible: row.jobState === "queued"
                                    glyph: Theme.icons.pause
                                    tooltip: qsTr("Hold")
                                    onClicked: RenderQueue.hold(row.jobId)
                                }
                                IconButton {
                                    visible: row.jobState === "failed" || row.jobState === "cancelled"
                                    glyph: Theme.icons.refresh
                                    tooltip: qsTr("Retry")
                                    onClicked: RenderQueue.retry(row.jobId)
                                }
                                IconButton {
                                    visible: row.jobState === "done"
                                    glyph: Theme.icons.folderOutput
                                    tooltip: qsTr("Show the file")
                                    onClicked: RenderQueue.showOutputFolder(row.jobId)
                                }
                                IconButton {
                                    visible: row.jobState === "held" || row.jobState === "queued" || row.jobState === "running"
                                    glyph: Theme.icons.x
                                    tooltip: qsTr("Cancel")
                                    onClicked: RenderQueue.cancel(row.jobId)
                                }
                                IconButton {
                                    glyph: Theme.icons.chevronUp
                                    tooltip: qsTr("Move up")
                                    onClicked: RenderQueue.move(row.jobId, -1)
                                }
                                IconButton {
                                    glyph: Theme.icons.chevronDown
                                    tooltip: qsTr("Move down")
                                    onClicked: RenderQueue.move(row.jobId, 1)
                                }
                                IconButton {
                                    visible: row.jobState !== "running"
                                    glyph: Theme.icons.trash
                                    tooltip: qsTr("Remove")
                                    onClicked: RenderQueue.remove(row.jobId)
                                }
                            }

                            ThemedProgressBar {
                                Layout.fillWidth: true
                                visible: row.jobState === "running"
                                value: row.progress
                            }

                            ThemedLabel {
                                Layout.fillWidth: true
                                visible: row.error.length > 0
                                size: "xs"
                                color: Theme.c.renderQueueRow.failed
                                text: row.error
                            }

                            ThemedLabel {
                                Layout.fillWidth: true
                                visible: row.expanded
                                size: "xs"
                                wrapMode: Text.Wrap
                                text: (row.worker.length > 0 ? row.worker + "\n" : "") + row.log
                            }
                        }
                    }
                }
            }
        }

        // --- Farm -----------------------------------------------------------------------------
        Column {
            visible: root.tab === "farm"
            width: parent.width
            spacing: Theme.spacingLg

            ThemedLabel {
                width: parent.width
                size: "sm"
                tone: "default"
                wrapMode: Text.Wrap
                text: RenderFarm.running
                      ? qsTr("Coordinator listening on %1").arg(RenderFarm.address)
                      : qsTr("The render farm is off. Turn it on in Settings, Render farm.")
            }

            RowLayout {
                visible: RenderFarm.running
                width: parent.width
                spacing: Theme.spacingMd

                ThemedLabel {
                    Layout.fillWidth: true
                    wrapMode: Text.NoWrap
                    elide: Text.ElideRight
                    text: qsTr("Token: %1").arg(RenderFarm.token)
                }
                ThemedButton {
                    text: qsTr("Copy token")
                    onClicked: EditorState.copyText(RenderFarm.token)
                }
            }

            ThemedLabel {
                visible: RenderFarm.running && RenderFarm.workers.length === 0
                width: parent.width
                size: "sm"
                wrapMode: Text.Wrap
                text: qsTr("No workers yet. On another computer run: drift --headless --farm-worker %1 --token <token>")
                      .arg(RenderFarm.address)
            }

            ListView {
                visible: RenderFarm.running && RenderFarm.workers.length > 0
                width: parent.width
                height: Math.min(contentHeight, 260)
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: RenderFarm.workers

                delegate: Column {
                    id: workerRow
                    required property var modelData
                    width: ListView.view.width

                    Rectangle {
                        width: parent.width
                        height: 1
                        color: Theme.c.renderQueueRow.divider
                    }
                    ColumnLayout {
                        width: parent.width
                        spacing: Theme.spacingSm

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.spacingMd
                            spacing: Theme.spacingMd

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0
                                ThemedLabel {
                                    Layout.fillWidth: true
                                    tone: "default"
                                    elide: Text.ElideRight
                                    wrapMode: Text.NoWrap
                                    text: workerRow.modelData.name
                                }
                                ThemedLabel {
                                    Layout.fillWidth: true
                                    size: "xs"
                                    elide: Text.ElideRight
                                    wrapMode: Text.NoWrap
                                    text: workerRow.modelData.gpu
                                }
                            }
                            ThemedLabel {
                                wrapMode: Text.NoWrap
                                text: workerRow.modelData.state === "busy"
                                      ? qsTr("%1 · %2%").arg(workerRow.modelData.task).arg(Math.round(workerRow.modelData.fraction * 100))
                                      : qsTr("Idle")
                            }
                            ThemedLabel {
                                visible: workerRow.modelData.fps > 0
                                wrapMode: Text.NoWrap
                                text: qsTr("%1 fps").arg(workerRow.modelData.fps.toFixed(1))
                            }
                            ThemedLabel {
                                wrapMode: Text.NoWrap
                                text: qsTr("%n done", "", workerRow.modelData.tasksDone)
                            }
                        }
                        ThemedProgressBar {
                            Layout.fillWidth: true
                            visible: workerRow.modelData.state === "busy"
                            value: workerRow.modelData.fraction
                        }
                        Item { Layout.preferredHeight: Theme.spacingSm }
                    }
                }
            }
        }
    }
}
