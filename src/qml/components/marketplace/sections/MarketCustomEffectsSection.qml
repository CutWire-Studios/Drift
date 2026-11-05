import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window
import Drift
// The user's own effects, transitions and audio effects: every package folder under
// <AppData>/effects, transitions and audio-effects, whether imported or copied in by hand.
Item {
    id: root

    required property var section
    required property var host

    property int tick: 0
    readonly property var items: { void tick; return Addons.customEffects() }
    readonly property var groups: [
        { kind: "effects", title: qsTr("Effects") },
        { kind: "transitions", title: qsTr("Transitions") },
        { kind: "audio-effects", title: qsTr("Audio effects") }
    ]

    Connections {
        target: Addons
        function onKindChanged(kind) {
            if (kind === "effects" || kind === "transitions" || kind === "audio-effects")
                root.tick++
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: root.host.sidePadding
            spacing: Theme.spacingSm

            Text {
                text: root.items.length > 0 ? qsTr("%n custom effect(s)", "", root.items.length)
                                            : qsTr("No custom effects")
                color: Theme.c.marketplace.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeBase
                font.weight: Font.DemiBold
            }

            Item { Layout.fillWidth: true }

            ThemedButton {
                visible: root.host.compact
                variant: "ghost"
                glyph: Theme.icons.refresh
                text: qsTr("Refresh")
                onClicked: Addons.reloadCustomEffects()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.c.marketplace.borderWidth
            color: Theme.c.marketplace.border
        }

        EmptyState {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.items.length === 0
            glyph: Theme.icons.wand
            title: qsTr("No custom effects yet")
            hint: qsTr("Install a .driftfx or a zip made in Drift Forge, or copy its folder into the custom effects folder and refresh.")
            actionText: qsTr("Install from file…")
            onActionTriggered: ShellBridge.main.importUserPackage("")
        }

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.items.length > 0
            clip: true
            contentHeight: groupColumn.height
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar { }

            Column {
                id: groupColumn
                x: root.host.sidePadding
                width: parent.width - root.host.sidePadding * 2
                topPadding: Theme.spacingLg
                bottomPadding: Theme.spacingLg
                spacing: Theme.spacingXl

                Repeater {
                    model: root.groups

                    delegate: Column {
                        id: group
                        required property var modelData
                        readonly property var entries: root.items.filter(i => i.kind === group.modelData.kind)

                        visible: entries.length > 0
                        width: parent.width
                        spacing: Theme.spacingSm

                        ThemedLabel {
                            text: group.modelData.title
                            tone: "default"
                            size: "sm"
                            font.weight: Font.DemiBold
                        }

                        Repeater {
                            model: group.entries

                            delegate: Rectangle {
                                id: row
                                required property var modelData

                                width: parent.width
                                height: Theme.controlHeight * 2
                                radius: Theme.c.marketplace.cardRadius
                                color: rowHover.hovered ? Theme.c.marketplace.cardBackgroundHover
                                                        : Theme.c.marketplace.cardBackground

                                HoverHandler { id: rowHover }

                                Rectangle {
                                    id: thumb
                                    anchors.left: parent.left
                                    anchors.leftMargin: Theme.spacingMd
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.height - Theme.spacingMd * 2
                                    height: width
                                    radius: Theme.c.marketplace.previewRadius
                                    color: Theme.c.marketplace.previewBackground
                                    clip: true

                                    Image {
                                        anchors.fill: parent
                                        visible: row.modelData.thumbnail.length > 0
                                        source: row.modelData.thumbnail
                                        sourceSize.width: width * Screen.devicePixelRatio
                                        sourceSize.height: height * Screen.devicePixelRatio
                                        fillMode: Image.PreserveAspectCrop
                                        asynchronous: true
                                    }
                                    IconGlyph {
                                        anchors.centerIn: parent
                                        visible: row.modelData.thumbnail.length === 0
                                        glyph: row.modelData.kind === "audio-effects" ? Theme.icons.music : Theme.icons.wand
                                        iconColor: Theme.c.marketplace.cardIcon
                                    }
                                }

                                Column {
                                    anchors.left: thumb.right
                                    anchors.leftMargin: Theme.spacingLg
                                    anchors.right: removeButton.left
                                    anchors.rightMargin: Theme.spacingLg
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: Theme.spacingXs

                                    ThemedLabel {
                                        width: parent.width
                                        text: row.modelData.name
                                        tone: "default"
                                        size: "sm"
                                        elide: Text.ElideRight
                                    }
                                    ThemedLabel {
                                        width: parent.width
                                        visible: text.length > 0
                                        text: row.modelData.description || row.modelData.category
                                        size: "xs"
                                        elide: Text.ElideRight
                                    }
                                }

                                ThemedButton {
                                    id: removeButton
                                    anchors.right: parent.right
                                    anchors.rightMargin: Theme.spacingMd
                                    anchors.verticalCenter: parent.verticalCenter
                                    variant: "ghost"
                                    glyph: Theme.icons.trash
                                    text: qsTr("Remove")
                                    onClicked: confirmRemove.openFor(row.modelData)
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    ThemedDialog {
        id: confirmRemove

        property var entry: ({})

        function openFor(item) {
            entry = item
            open()
        }

        title: qsTr("Remove “%1”?").arg(confirmRemove.entry.name || "")
        acceptText: qsTr("Remove")
        acceptVariant: "destructive"
        acceptOnReturn: false
        preferredWidth: Theme.dialogWidthSm
        onAccepted: {
            const name = confirmRemove.entry.name
            if (Addons.removeCustomEffect(confirmRemove.entry.kind, confirmRemove.entry.folder))
                Toasts.success(qsTr("Removed “%1”").arg(name))
            else
                Toasts.error(qsTr("Could not remove “%1”").arg(name))
        }

        contentItem: ThemedLabel {
            width: parent ? parent.width : Theme.dialogWidthSm
            wrapMode: Text.WordWrap
            size: "sm"
            text: qsTr("Its folder is deleted. Clips that use it will play without it.")
        }
    }
}
