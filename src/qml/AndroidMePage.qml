import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
import "components"

// The home screen's third destination: everything about the app rather than about a project.
//
// It exists because the editor's overflow menu is going away, and most of what that menu held
// was not project work at all — theme, packs, updates, settings, diagnostics. Those had no home
// outside the editor, so on a first run, before any project existed, they were unreachable.
Item {
    id: root

    readonly property bool marketLinked: Market.configured && Market.authenticated

    function host() { return root.Window.window }

    Flickable {
        anchors.fill: parent
        anchors.topMargin: root.SafeArea.margins.top
        anchors.leftMargin: root.SafeArea.margins.left
        anchors.rightMargin: root.SafeArea.margins.right
        contentWidth: width
        contentHeight: pageColumn.implicitHeight + Theme.spacing2xl
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick

        Column {
            id: pageColumn
            width: parent.width
            spacing: Theme.spacingXl
            topPadding: Theme.spacing2xl
            leftPadding: Theme.androidPagePadding
            rightPadding: Theme.androidPagePadding

            readonly property real contentWidth: width - leftPadding - rightPadding

            ThemedLabel {
                text: qsTr("Me")
                size: "lg"
            }

            // Account state, when there is one. Per the marketplace contract Drift never shows
            // a sign-in prompt or the store's URL, so this is a status line and nothing more.
            Rectangle {
                width: pageColumn.contentWidth
                height: accountCol.implicitHeight + Theme.spacingXl * 2
                radius: Theme.radiusMd
                color: Theme.panelBackground
                border.width: Theme.borderWidth
                border.color: Theme.panelBorder
                visible: root.marketLinked

                Column {
                    id: accountCol
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.margins: Theme.spacingXl
                    spacing: Theme.spacingXs

                    ThemedLabel {
                        text: Market.accountName
                        size: "sm"
                    }

                    ThemedLabel {
                        text: qsTr("%n coin(s)", "", Market.coins)
                        tone: "muted"
                        size: "sm"
                    }
                }
            }

            Column {
                width: pageColumn.contentWidth
                spacing: Theme.androidTouchGap

                Repeater {
                    model: [
                        { id: "theme", label: Theme.darkMode ? qsTr("Light mode") : qsTr("Dark mode"),
                          icon: Theme.darkMode ? Theme.icons.sun : Theme.icons.moon, shown: true },
                        { id: "settings", label: qsTr("Settings"), icon: Theme.icons.settings, shown: true },
                        { id: "extras", label: qsTr("Extras"), icon: Theme.icons.package, shown: true },
                        { id: "update", label: qsTr("Update available"), icon: Theme.icons.download,
                          shown: Updates.updateAvailable },
                        { id: "debug", label: qsTr("Debug info"), icon: Theme.icons.info, shown: true }
                    ]

                    delegate: AbstractButton {
                        id: row
                        required property var modelData

                        width: pageColumn.contentWidth
                        height: Theme.androidAddRowHeight
                        visible: modelData.shown
                        hoverEnabled: true

                        Accessible.role: Accessible.Button
                        Accessible.name: modelData.label

                        onClicked: {
                            Haptics.select()
                            const win = root.host()
                            if (modelData.id === "theme")
                                Theme.toggleDarkMode()
                            else if (modelData.id === "settings")
                                win.openSettings()
                            else if (modelData.id === "extras")
                                win.openExtras()
                            else if (modelData.id === "update")
                                win.openUpdateDialog()
                            else if (modelData.id === "debug")
                                win.openDebugInfo()
                        }

                        background: Rectangle {
                            radius: Theme.radiusMd
                            color: row.down ? Theme.panelMuted : Theme.panelBackground
                            border.width: Theme.borderWidth
                            border.color: Theme.panelBorder
                        }

                        contentItem: Row {
                            spacing: Theme.spacingLg
                            leftPadding: Theme.spacingXl
                            rightPadding: Theme.spacingXl

                            IconGlyph {
                                anchors.verticalCenter: parent.verticalCenter
                                glyph: row.modelData.icon
                                iconSize: Theme.iconSizeLg
                                iconColor: Theme.panelForeground
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: row.modelData.label
                                color: Theme.panelForeground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeBase
                            }
                        }
                    }
                }
            }
        }
    }
}
