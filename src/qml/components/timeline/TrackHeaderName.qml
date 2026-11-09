import QtQuick
import Drift

// Track name, level with the toggles on the header's first line. Double-click edits it in place.
Item {
    id: nameRoot

    property string label: ""
    property bool dimmed: false
    property int trackIndex: -1
    property string customName: ""
    property bool editing: false

    function commit() {
        if (!editing)
            return
        editing = false
        EditorState.renameTrack(trackIndex, editor.item.text)
    }

    Text {
        anchors.fill: parent
        visible: !nameRoot.editing
        verticalAlignment: Text.AlignVCenter
        text: nameRoot.label
        color: nameRoot.dimmed ? Theme.c.trackHeaderName.foregroundMuted : Theme.c.trackHeaderName.foreground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
        elide: Text.ElideRight
    }

    TapHandler {
        enabled: nameRoot.trackIndex >= 0 && !nameRoot.editing
        acceptedButtons: Qt.LeftButton
        acceptedDevices: PointerDevice.Mouse
        onDoubleTapped: nameRoot.editing = true
    }

    Loader {
        id: editor
        anchors.fill: parent
        active: nameRoot.editing
        sourceComponent: Component {
            ThemedTextField {
                text: nameRoot.customName
                placeholderText: nameRoot.label
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                leftPadding: Theme.spacingSm
                rightPadding: Theme.spacingSm
                topPadding: 0
                bottomPadding: 0
                implicitHeight: nameRoot.height
                Component.onCompleted: {
                    forceActiveFocus()
                    selectAll()
                }
                onAccepted: nameRoot.commit()
                onActiveFocusChanged: {
                    if (!activeFocus)
                        nameRoot.commit()
                }
                Keys.onEscapePressed: nameRoot.editing = false
            }
        }
    }
}
