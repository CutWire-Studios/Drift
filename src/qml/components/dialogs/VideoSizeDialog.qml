import QtQuick
import Drift

// Header Video button. Canvas size, frame rate and layout templates.
ThemedDialog {
    id: root

    title: qsTr("Video")
    preferredWidth: Theme.dialogWidthMd
    showAccept: false
    rejectText: qsTr("Close")

    function openDialog() {
        open()
    }

    contentItem: VideoSizeControls {
        width: parent ? parent.width : Theme.dialogWidthMd
        onLayoutChooserOpened: root.close()
    }
}
