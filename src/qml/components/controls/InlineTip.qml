import QtQuick
import Drift

// One-line, dismissible hint. Dismissal is remembered under QSettings "tips/<tipId>".
Item {
    id: root

    property string tipId: ""
    property string text: ""
    property string actionText: ""
    property bool shown: true
    property bool dismissed: EditorState.preferences.tipDismissed(tipId)

    signal actionTriggered()

    visible: shown && !dismissed && EditorState.preferences.showTips && !ShellBridge.phoneShell && !Theme.phone
    width: parent ? parent.width : implicitWidth
    implicitHeight: visible ? Math.max(tipText.implicitHeight, closeButton.height) : 0
    height: implicitHeight

    Text {
        id: tipText
        anchors.left: parent.left
        anchors.right: closeButton.left
        anchors.rightMargin: Theme.spacingMd
        anchors.verticalCenter: parent.verticalCenter
        wrapMode: Text.WordWrap
        textFormat: Text.RichText
        text: root.actionText.length > 0
              ? root.text + " <a href=\"action\">" + root.actionText + "</a>"
              : root.text
        color: Theme.c.inlineTip.foregroundMuted
        linkColor: Theme.c.inlineTip.link
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
        onLinkActivated: root.actionTriggered()
    }

    IconButton {
        id: closeButton
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        buttonSize: Theme.controlHeightSm
        iconSize: Theme.iconSizeSm
        variant: "ghost"
        glyph: Theme.icons.x
        tooltip: qsTr("Hide this tip")
        onClicked: {
            EditorState.preferences.dismissTip(root.tipId)
            root.dismissed = true
        }
    }
}
