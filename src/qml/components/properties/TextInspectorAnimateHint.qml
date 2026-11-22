import QtQuick
import Drift

Text {
    property var inspector
    visible: inspector.clipKind === "subtitle"
    width: parent.width
    text: qsTr("Plays for every caption")
    color: Theme.c.textInspectorAnimateHint.foregroundMuted
    font.family: Theme.fontFamily
    font.pixelSize: Theme.touchInput && Theme.compact ? Theme.fontSizeSm : Theme.fontSizeXs
}
