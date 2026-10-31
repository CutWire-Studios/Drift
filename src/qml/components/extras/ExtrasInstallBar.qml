import QtQuick
import QtQuick.Layouts
import Drift

Rectangle {
    id: root

    required property var store

    readonly property bool installMode: store.selecting && !store.detailOpen
    readonly property bool queueMode: !installMode && Addons.queue.count > 0
    readonly property bool shown: installMode || queueMode
    readonly property var plan: {
        void Addons.catalog
        return installMode ? Addons.planInstall(store.selectedIds) : ({})
    }
    readonly property var added: (plan.added || []).filter(a => store.selectedIds.indexOf(a.id) === -1)

    property var seen: ({})
    property int batchTotal: 0

    readonly property var queueIds: Addons.queue.ids || []
    readonly property string currentId: queueIds.length > 0 ? queueIds[0] : ""
    readonly property var currentRow: currentId.length > 0 ? Addons.entry(currentId) : ({})
    readonly property real currentFraction: store.transfers[currentId] ? store.transfers[currentId].fraction : 0
    readonly property int doneCount: Math.max(0, batchTotal - queueIds.length)

    Connections {
        target: Addons
        function onQueueChanged() {
            const ids = Addons.queue.ids || []
            if (ids.length === 0) {
                root.seen = {}
                root.batchTotal = 0
                return
            }
            const next = Object.assign({}, root.seen)
            let fresh = 0
            for (const id of ids) {
                if (!next[id]) {
                    next[id] = true
                    ++fresh
                }
            }
            root.seen = next
            root.batchTotal += fresh
        }
    }

    height: shown ? implicitHeight : 0
    implicitHeight: row.implicitHeight + Theme.spacingXl * 2
    visible: height > 0
    clip: true
    color: Theme.c.extrasStore.barBackground

    Behavior on height {
        NumberAnimation { duration: Theme.durationBase; easing.type: Theme.easing }
    }

    Rectangle {
        anchors.top: parent.top
        width: parent.width
        height: Theme.c.extrasStore.borderWidth
        color: Theme.c.extrasStore.barBorder
    }

    RowLayout {
        id: row
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: Theme.spacingXl
        anchors.rightMargin: Theme.spacingXl
        spacing: Theme.spacingXl

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingXs

            ThemedLabel {
                Layout.fillWidth: true
                tone: "default"
                size: "sm"
                font.weight: Font.Medium
                elide: Text.ElideRight
                text: {
                    if (root.installMode) {
                        const size = DownloadFormat.formatBytes(root.plan.downloadSize)
                        const base = qsTr("Install %n extra(s)", "", root.plan.count || root.store.selectedIds.length)
                        return size.length > 0 ? qsTr("%1 · %2").arg(base).arg(size) : base
                    }
                    const name = root.currentRow.name || ""
                    return root.batchTotal > 1
                           ? qsTr("Installing %1 of %2 · %3").arg(Math.min(root.doneCount + 1, root.batchTotal))
                                 .arg(root.batchTotal).arg(name)
                           : qsTr("Installing %1").arg(name)
                }
            }

            ThemedLabel {
                Layout.fillWidth: true
                visible: text.length > 0
                elide: Text.ElideRight
                text: {
                    if (!root.installMode)
                        return ""
                    if (root.added.length > 0)
                        return qsTr("Also installs: %1").arg(root.added.map(a => a.name).join(", "))
                    return ""
                }
            }

            ThemedLabel {
                Layout.fillWidth: true
                visible: root.installMode && !!root.plan.missing && root.plan.missing.length > 0
                color: Theme.c.extrasStore.stateFailed
                elide: Text.ElideRight
                text: root.plan.missing && root.plan.missing.length > 0
                      ? qsTr("%1 needs %2, which isn't available for this device")
                            .arg(root.plan.missing[0].forName).arg(root.plan.missing[0].requirement)
                      : ""
            }

            ThemedProgressBar {
                Layout.fillWidth: true
                visible: root.queueMode
                value: root.batchTotal > 0 ? Math.min(1, (root.doneCount + root.currentFraction) / root.batchTotal) : 0
            }
        }

        ThemedButton {
            visible: root.installMode
            variant: "ghost"
            text: qsTr("Clear")
            onClicked: root.store.clearSelection()
        }

        ThemedButton {
            visible: root.installMode
            variant: "primary"
            text: qsTr("Install")
            onClicked: {
                Addons.installMany(root.store.selectedIds)
                root.store.clearSelection()
            }
        }
    }
}
