import QtQuick
import QtQuick.Controls.Basic
import Drift

// Prominent drag header: handle + title + one dismissal.
Rectangle {
    id: header

    property string title: ""
    property string doneText: ""
    property real safeLeft: 0
    property real safeRight: 0
    // beginDrag / updateDrag / finishDrag / collapse / dismiss / doneRequested.
    required property var sheet
    property alias actions: actionRow.data

    anchors.left: parent.left
    anchors.right: parent.right
    anchors.top: parent.top
    height: Theme.androidSheetHeaderHeight
    color: Theme.c.androidSheetHeader.background
    z: 2

    AndroidSheetHandle {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 10
    }

    Text {
        anchors.left: parent.left
        // Landscape cutout: the header spans the panel edge to edge, so
        // without this the title ran under the notch on one rotation and
        // the Close button under the nav bar on the other.
        anchors.leftMargin: Theme.pagePadding + header.safeLeft
        anchors.right: actionRow.visible ? actionRow.left : (doneBtn.visible ? doneBtn.left : closeBtn.left)
        anchors.rightMargin: Theme.spacingMd
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 10
        text: header.title.length > 0 ? header.title : qsTr("Sheet")
        color: Theme.c.androidSheetHeader.foreground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeBase
        font.weight: Font.DemiBold
        elide: Text.ElideRight
    }

    ThemedButton {
        id: doneBtn
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingSm + header.safeRight
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 6
        visible: header.doneText.length > 0
        variant: "primary"
        text: header.doneText
        onClicked: header.sheet.doneRequested()
    }

    // Only when there is no Done. Two dismissals side by side asked the user to
    // pick between them, and on the sheets that have both they do the same thing —
    // Done commits and closes, X closes. Back, a drag down and a tap on the scrim
    // all still dismiss, so nothing becomes unreachable by losing the button.
    IconButton {
        id: closeBtn
        visible: !doneBtn.visible
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingSm + header.safeRight
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 4
        buttonSize: Theme.androidIconButtonSize
        iconSize: Theme.iconSizeLg
        glyph: Theme.icons.x
        variant: "text"
        tooltip: qsTr("Close")
        haptic: "none"
        onClicked: header.sheet.dismiss()
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: Theme.c.androidSheetHeader.borderWidth
        color: Theme.c.androidSheetHeader.border
    }

    // Whole header is the primary drag affordance (except close).
    AndroidSheetDragArea {
        anchors.fill: parent
        anchors.rightMargin: closeBtn.width + Theme.spacingSm + header.safeRight
                             + (doneBtn.visible
                                ? doneBtn.width + Theme.spacingSm : 0)
        stealTouches: true
        sheet: header.sheet
    }

    Row {
        id: actionRow
        anchors.right: doneBtn.visible ? doneBtn.left : closeBtn.left
        anchors.rightMargin: Theme.spacingXs
        anchors.verticalCenter: closeBtn.verticalCenter
        spacing: Theme.spacingXs
        visible: children.length > 0 && Theme.phone
    }
}
