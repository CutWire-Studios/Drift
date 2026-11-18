import QtQuick
import QtQuick.Controls.Basic
import Drift

PanelFrame {
    id: root

    readonly property var entries: [qsTr("Start")].concat(EditorState.undoHistory)
    readonly property int current: EditorState.undoIndex

    property var snapshots: []
    property var stats: ({})
    property var snapshotLabels: ({})
    property var historySettings: ({})

    function followCurrent() {
        list.positionViewAtIndex(root.current, ListView.Contain)
    }

    function refreshStats() {
        root.stats = EditorState.historyStats(snapshotsSection.expanded)
        root.historySettings = EditorState.historySettings()
    }

    function refreshSnapshots() {
        const reply = EditorState.listSnapshots()
        root.snapshots = reply.ok ? reply.snapshots : []
    }

    function report(reply) {
        if (!reply.ok)
            EditorState.setLastMessage(reply.detail || reply.error)
    }

    readonly property string statsText: {
        const s = root.stats
        if (!s || !s.ok)
            return ""
        let text = qsTr("%n step(s)", "", s.steps) + " · " + qsTr("%n state(s) in memory", "", s.snapshots)
        if (s.json_bytes !== undefined)
            text += " · " + DownloadFormat.formatBytes(s.json_bytes)
        return text
    }

    onCurrentChanged: Qt.callLater(root.followCurrent)
    Component.onCompleted: root.refreshStats()

    Connections {
        target: EditorState
        function onUndoHistoryChanged() { statsTimer.restart() }
    }

    Timer {
        id: statsTimer
        interval: 500
        onTriggered: root.refreshStats()
    }

    AssetsPanelHeader {
        id: header
        width: parent.width
        height: Theme.panelHeaderHeight
        title: qsTr("History")
    }

    ThemedButton {
        anchors.right: header.right
        anchors.rightMargin: Theme.spacingSm
        anchors.verticalCenter: header.verticalCenter
        text: qsTr("Clear history…")
        glyph: Theme.icons.trash
        variant: "ghost"
        enabled: EditorState.undoHistory.length > 0
        onClicked: {
            clearDialog.keepLast = 0
            keepField.value = 0
            clearDialog.open()
        }
    }

    ThemedDialog {
        id: clearDialog

        property int keepLast: 0

        title: qsTr("Clear history")
        acceptText: qsTr("Clear")
        acceptVariant: "destructive"
        preferredWidth: Theme.dialogWidthSm
        acceptOnReturn: false

        onAccepted: EditorState.clearUndoHistory(clearDialog.keepLast)

        contentItem: Column {
            spacing: Theme.spacingLg
            width: parent ? parent.width : 320

            ThemedLabel {
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("Frees undo memory. Undone steps (redo) are dropped too, and cleared steps can't be restored.")
            }
            ThemedLabel {
                text: qsTr("Keep last steps")
            }
            ThemedNumberField {
                id: keepField
                width: parent.width
                from: 0
                to: Math.max(1, EditorState.undoIndex)
                onEdited: v => clearDialog.keepLast = Math.round(v)
            }
        }
    }

    Item {
        id: extras
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: infoLine.height + snapshotsSection.height + Theme.spacingSm

        ThemedLabel {
            id: infoLine
            anchors.top: parent.top
            anchors.topMargin: Theme.spacingSm
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: Theme.pagePadding
            anchors.rightMargin: Theme.pagePadding
            text: root.statsText
            elide: Text.ElideRight
            wrapMode: Text.NoWrap
        }

        CollapsibleSection {
            id: snapshotsSection
            anchors.top: infoLine.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: Theme.pagePadding
            anchors.rightMargin: Theme.pagePadding
            title: qsTr("Snapshots")
            tooltip: qsTr("Named copies of the project at this step, kept outside the undo stack")
            expanded: false
            onExpandedChanged: {
                if (expanded)
                    root.refreshSnapshots()
                root.refreshStats()
            }

            Row {
                width: parent.width
                spacing: Theme.spacingSm

                ThemedButton {
                    variant: "secondary"
                    glyph: Theme.icons.save
                    text: qsTr("Save snapshot…")
                    onClicked: snapshotNameDialog.openWith(qsTr("Save snapshot"), "")
                }

                ThemedButton {
                    variant: "ghost"
                    glyph: Theme.icons.settings
                    text: qsTr("This project…")
                    tooltip: qsTr("Undo history settings for this project")
                    onClicked: projectHistoryDialog.openDialog()
                }
            }

            ThemedLabel {
                width: parent.width
                visible: root.snapshots.length === 0
                text: qsTr("No snapshots yet.")
            }

            Repeater {
                model: root.snapshots

                delegate: Row {
                    id: snapshotRow

                    required property var modelData

                    width: parent.width
                    spacing: Theme.spacingSm

                    ThemedLabel {
                        width: snapshotRow.width - restoreButton.width - snapshotRow.spacing
                        anchors.verticalCenter: parent.verticalCenter
                        tone: "default"
                        size: "sm"
                        wrapMode: Text.NoWrap
                        elide: Text.ElideRight
                        text: (root.snapshotLabels[snapshotRow.modelData.hash] || snapshotRow.modelData.short)
                              + " · " + Qt.formatDateTime(new Date(snapshotRow.modelData.savedAt), Locale.ShortFormat)
                              + " · " + DownloadFormat.formatBytes(snapshotRow.modelData.bytes)
                    }

                    ThemedButton {
                        id: restoreButton
                        anchors.verticalCenter: parent.verticalCenter
                        variant: "ghost"
                        text: qsTr("Restore")
                        onClicked: {
                            restoreDialog.hash = snapshotRow.modelData.hash
                            restoreDialog.open()
                        }
                    }
                }
            }
        }
    }

    NameDialog {
        id: snapshotNameDialog
        placeholder: qsTr("Before colour pass")
        onSubmitted: function (name) {
            const reply = EditorState.takeSnapshot(name)
            root.report(reply)
            if (reply.ok) {
                const labels = root.snapshotLabels
                labels[reply.hash] = reply.label
                root.snapshotLabels = labels
                root.refreshSnapshots()
            }
        }
    }

    ThemedDialog {
        id: restoreDialog

        property string hash

        title: qsTr("Restore snapshot")
        acceptText: qsTr("Restore")
        acceptVariant: "destructive"
        preferredWidth: Theme.dialogWidthSm
        acceptOnReturn: false

        onAccepted: {
            root.report(EditorState.restoreSnapshot(restoreDialog.hash))
            root.refreshStats()
        }

        contentItem: ThemedLabel {
            width: parent ? parent.width : 320
            text: qsTr("Replaces the project with this snapshot. A snapshot that is no longer in the undo history starts a new history, so steps after it can't be restored.")
        }
    }

    ThemedDialog {
        id: projectHistoryDialog

        title: qsTr("History settings for this project")
        acceptText: qsTr("Apply")
        preferredWidth: Theme.dialogWidthSm
        acceptOnReturn: false

        property bool stepsEdited: false
        property bool persistEdited: false
        property int steps: -1
        property bool persist: false

        function openDialog() {
            const s = EditorState.historySettings()
            root.historySettings = s
            steps = s.steps
            persist = s.mode === "project"
            stepsEdited = false
            persistEdited = false
            stepsField.value = s.steps
            open()
        }

        onAccepted: {
            if (stepsEdited)
                root.report(EditorState.setProjectHistorySteps(steps))
            if (persistEdited)
                root.report(EditorState.setProjectHistoryPersist(persist))
            root.refreshStats()
        }

        contentItem: Column {
            spacing: Theme.spacingLg
            width: parent ? parent.width : 320

            ThemedLabel {
                width: parent.width
                text: root.historySettings.source === "project"
                      ? qsTr("This project overrides the app's undo history settings.")
                      : qsTr("Following the app's undo history settings (Settings, General).")
            }
            ThemedLabel {
                text: qsTr("Undo steps kept (-1 unlimited, 0 off)")
            }
            ThemedNumberField {
                id: stepsField
                width: parent.width
                from: -1
                to: 100000
                onEdited: v => {
                    projectHistoryDialog.steps = Math.round(v)
                    projectHistoryDialog.stepsEdited = true
                }
            }
            ThemedCheckBox {
                text: qsTr("Save history with the project")
                checked: projectHistoryDialog.persist
                onToggled: {
                    projectHistoryDialog.persist = checked
                    projectHistoryDialog.persistEdited = true
                }
            }
            ThemedButton {
                visible: root.historySettings.source === "project"
                variant: "ghost"
                text: qsTr("Use app settings")
                onClicked: {
                    root.report(EditorState.resetProjectHistory())
                    projectHistoryDialog.close()
                    root.refreshStats()
                }
            }
        }
    }

    EmptyState {
        anchors.top: extras.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spacingLg
        visible: EditorState.undoHistory.length === 0
        glyph: Theme.icons.undo
        title: qsTr("Nothing to undo yet. Your edits will be listed here.")
    }

    ListView {
        id: list
        anchors.top: extras.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        visible: EditorState.undoHistory.length > 0
        clip: true
        model: root.entries
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar {}
        Component.onCompleted: root.followCurrent()

        delegate: Rectangle {
            required property int index
            required property string modelData
            readonly property bool isCurrent: index === root.current

            width: ListView.view.width
            height: 28
            color: isCurrent ? Theme.accent : (hover.hovered ? Theme.popoverHover : "transparent")

            Text {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: Theme.pagePadding
                anchors.rightMargin: Theme.pagePadding
                anchors.verticalCenter: parent.verticalCenter
                text: modelData
                elide: Text.ElideRight
                color: isCurrent ? Theme.accentForeground : Theme.foreground
                opacity: index > root.current ? 0.45 : 1
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                font.italic: index === 0
            }

            HoverHandler { id: hover }
            TapHandler {
                enabled: !isCurrent
                onTapped: EditorState.setUndoIndex(index)
            }
        }
    }
}
