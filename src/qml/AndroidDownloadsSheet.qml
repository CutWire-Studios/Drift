import QtQuick
import QtQuick.Controls.Basic
import Drift
// Every download at once, on the phone.
//
// Deliberately not auto-raised. The per-tile ring in MarketTab is the primary surface and the
// whole point of it is that browsing continues while things download; a sheet that opened itself
// the moment a job started would undo exactly that. This is reached from the badge, when the user
// asks "what is happening".
AndroidBottomSheet {
    id: root

    title: qsTr("Downloads")

    readonly property var jobs: {
        void Downloads.revision
        return Downloads.jobs
    }

    readonly property bool anyFinished: {
        for (let i = 0; i < root.jobs.length; ++i) {
            if (root.jobs[i].finished === true)
                return true
        }
        return false
    }

    Item {
        anchors.fill: parent

        ThemedLabel {
            anchors.centerIn: parent
            width: parent.width - Theme.androidPagePadding * 2
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            visible: root.jobs.length === 0
            text: qsTr("Nothing downloading right now.")
        }

        ListView {
            id: list
            anchors.fill: parent
            anchors.bottomMargin: clearRow.visible ? clearRow.height : 0
            clip: true
            model: root.jobs
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar { }

            delegate: AndroidDownloadJobRow {
                width: list.width
                sideInset: root.safeLeft
                sideInsetRight: root.safeRight
            }
        }

        Item {
            id: clearRow
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: Theme.androidMinTouchTarget + Theme.spacingLg * 2
            visible: root.anyFinished

            ThemedButton {
                anchors.centerIn: parent
                variant: "secondary"
                text: qsTr("Clear finished")
                onClicked: Downloads.clearFinished()
            }
        }
    }
}
