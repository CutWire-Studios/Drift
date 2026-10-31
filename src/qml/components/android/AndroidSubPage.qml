import QtQuick
import Drift

// A full-screen phone page pushed onto AndroidMain's StackView: back arrow, title, then the
// content below. Settings and Extras use it in place of their desktop dialogs.
Item {
    id: root

    property string title: ""
    default property alias content: host.data

    signal backRequested()

    Rectangle {
        anchors.fill: parent
        color: Theme.c.androidToolPage.background
    }

    Item {
        id: bar
        width: parent.width
        height: Theme.androidTopBarHeight + SafeArea.margins.top

        Rectangle {
            anchors.fill: parent
            color: Theme.c.androidTopBar.background
        }

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: Theme.c.androidTopBar.borderWidth
            color: Theme.c.androidTopBar.border
        }

        IconButton {
            id: backBtn
            anchors.left: parent.left
            anchors.leftMargin: SafeArea.margins.left + Theme.spacingSm
            anchors.bottom: parent.bottom
            anchors.bottomMargin: (Theme.androidTopBarHeight - height) / 2
            buttonSize: Theme.androidIconButtonSize
            iconSize: Theme.iconSizeLg
            glyph: Theme.icons.chevronLeft
            variant: "text"
            tooltip: qsTr("Back")
            onClicked: root.backRequested()
        }

        ThemedLabel {
            anchors.left: backBtn.right
            anchors.leftMargin: Theme.spacingSm
            anchors.right: parent.right
            anchors.rightMargin: SafeArea.margins.right + Theme.spacingLg
            anchors.verticalCenter: backBtn.verticalCenter
            text: root.title
            tone: "default"
            size: "base"
            font.weight: Font.Medium
            wrapMode: Text.NoWrap
            elide: Text.ElideRight
        }
    }

    Item {
        id: host
        anchors.top: bar.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: SafeArea.margins.left
        anchors.rightMargin: SafeArea.margins.right
        anchors.bottomMargin: SafeArea.margins.bottom
    }
}
