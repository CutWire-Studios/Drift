import QtQuick
import Drift

Rectangle {
    id: root

    required property var store
    required property var row

    readonly property bool compact: store.compact
    readonly property bool selected: !!store.selection[row.id]
    readonly property bool selecting: store.selecting
    readonly property bool selectable: store.isSelectable(row)
    readonly property var transfer: store.transfers[row.id]
    readonly property string installState: row.state || ""
    readonly property bool active: installState === "downloading" || installState === "installing"
    readonly property bool showCheck: selectable && (selecting || mouseHover.hovered)

    signal openRequested()
    signal selectionToggled()

    implicitHeight: compact ? 168 : 120
    radius: Theme.c.extrasStore.cardRadius
    color: mouseHover.hovered ? Theme.c.extrasStore.cardBackgroundHover : Theme.c.extrasStore.cardBackground
    border.width: Theme.c.extrasStore.borderWidth
    border.color: selected ? Theme.c.extrasStore.cardBorderSelected : Theme.c.extrasStore.cardBorder

    Accessible.role: Accessible.Button
    Accessible.name: row.name || ""

    Behavior on color {
        ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }

    HoverHandler {
        id: mouseHover
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        onTapped: (point) => {
            const local = check.mapFromItem(root, point.position.x, point.position.y)
            if (check.visible && check.contains(local))
                return
            if (root.selecting) {
                if (root.selectable)
                    root.selectionToggled()
            } else {
                root.openRequested()
            }
        }
    }

    TapHandler {
        acceptedDevices: PointerDevice.TouchScreen
        onLongPressed: if (root.selectable) root.selectionToggled()
    }

    ExtrasIconTile {
        id: tile
        x: Theme.spacingXl
        y: Theme.spacingXl
        tileSize: root.compact ? 40 : 44
        icon: root.row.icon || ""
        accent: root.row.accent || ""
        label: root.row.name || ""
    }

    ThemedCheckBox {
        id: check
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: Theme.spacingMd
        visible: root.showCheck
        checked: root.selected
        tooltip: qsTr("Select")
        onToggled: {
            root.selectionToggled()
            checked = Qt.binding(() => root.selected)
        }
    }

    Column {
        anchors.left: root.compact ? parent.left : tile.right
        anchors.right: parent.right
        anchors.top: root.compact ? tile.bottom : tile.top
        anchors.leftMargin: root.compact ? Theme.spacingXl : Theme.spacingLg
        anchors.rightMargin: Theme.spacingXl + (root.compact ? 0 : Theme.spacing3xl)
        anchors.topMargin: root.compact ? Theme.spacingMd : 0
        spacing: Theme.spacingXs

        ThemedLabel {
            width: parent.width
            text: root.row.name || ""
            tone: "default"
            size: "sm"
            font.weight: Font.Medium
            elide: Text.ElideRight
        }

        ThemedLabel {
            width: parent.width
            text: root.row.tagline || root.row.description || ""
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
    }

    Item {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spacingXl
        height: statusRow.height

        ThemedProgressBar {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: statusRow.top
            anchors.bottomMargin: Theme.spacingXs
            visible: root.active
            value: root.transfer ? root.transfer.fraction : 0
        }

        Row {
            id: statusRow
            spacing: Theme.spacingSm

            IconGlyph {
                id: stateIcon
                anchors.verticalCenter: parent.verticalCenter
                visible: glyph.length > 0
                iconSize: Theme.iconSizeSm
                iconColor: statusLabel.color
                spinning: root.active
                glyph: {
                    switch (root.installState) {
                    case "installed": return Theme.icons.success
                    case "update-available": return Theme.icons.download
                    case "failed": return Theme.icons.error
                    case "queued": return Theme.icons.clock
                    case "downloading":
                    case "installing": return Theme.icons.spinner
                    case "needs-newer-app": return Theme.icons.warning
                    }
                    return ""
                }
            }

            ThemedLabel {
                id: statusLabel
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(implicitWidth, root.width - Theme.spacingXl * 2
                                - (stateIcon.visible ? Theme.iconSizeSm + parent.spacing : 0))
                elide: Text.ElideRight
                color: {
                    switch (root.installState) {
                    case "installed": return Theme.c.extrasStore.stateInstalled
                    case "update-available": return Theme.c.extrasStore.stateUpdate
                    case "failed":
                    case "needs-newer-app": return Theme.c.extrasStore.stateFailed
                    }
                    return Theme.c.extrasStore.foregroundMuted
                }
                text: {
                    switch (root.installState) {
                    case "queued": return qsTr("Waiting…")
                    case "downloading":
                    case "installing":
                        return root.transfer && root.transfer.fraction > 0
                               ? qsTr("%1 %2%").arg(root.transfer.phase).arg(Math.round(root.transfer.fraction * 100))
                               : (root.installState === "installing" ? qsTr("Installing…") : qsTr("Downloading…"))
                    case "failed": return qsTr("Failed · open to retry")
                    case "needs-newer-app": return qsTr("Needs Drift %1").arg(root.row.minAppVersion)
                    case "installed": return root.row.custom ? qsTr("Installed from file") : qsTr("Installed")
                    case "update-available": return qsTr("Update · %1").arg(DownloadFormat.formatBytes(root.row.downloadSize))
                    }
                    return DownloadFormat.formatBytes(root.row.downloadSize)
                }
            }
        }
    }
}
