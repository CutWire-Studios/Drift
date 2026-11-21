import QtQuick
import Drift

Item {
    id: videoButton

    property bool dialogShown: false

    signal bakesRequested()
    signal layoutRequested()

    implicitWidth: videoBtn.implicitWidth
    implicitHeight: videoBtn.implicitHeight
    width: implicitWidth
    height: implicitHeight
    anchors.verticalCenter: parent.verticalCenter

    function toggleCrop() {
        EditorState.preview.canvasCropMode = !EditorState.preview.canvasCropMode
    }

    IconButton {
        id: videoBtn
        anchors.fill: parent
        glyph: Theme.icons.ratio
        variant: "ghost"
        text: qsTr("Video")
        active: videoMenu.opened || EditorState.preview.canvasCropMode || videoButton.dialogShown
        tooltip: qsTr("Video size, crop, multicam and pre-rendered sections")
        onClicked: videoMenu.popup(0, videoButton.height + Theme.spacingMd)
    }

    VideoMenu {
        id: videoMenu
        onBakesRequested: videoButton.bakesRequested()
        onCropRequested: videoButton.toggleCrop()
        onLayoutRequested: videoButton.layoutRequested()
        onMulticamRequested: ShellBridge.main.openMulticam()
    }
}
