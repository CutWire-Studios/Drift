import QtQuick
import QtQuick.Controls.Basic
import Drift

AbstractButton {
    id: root

    property bool selected: false
    default property alias content: slot.data

    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    checkable: false
    scale: down ? Theme.pressScale : 1.0

    Behavior on scale {
        NumberAnimation { duration: Theme.durationPress; easing.type: Theme.easing }
    }

    Accessible.role: Accessible.RadioButton
    Accessible.checked: selected
    Accessible.name: text

    background: Rectangle {
        radius: Theme.c.quickSetupCard.radius
        color: root.selected ? Theme.c.quickSetupCard.backgroundSelected
               : root.hovered ? Theme.c.quickSetupCard.backgroundHover
               : Theme.c.quickSetupCard.background
        border.width: root.selected ? Theme.c.quickSetupCard.borderWidthSelected
                                    : Theme.c.quickSetupCard.borderWidth
        border.color: root.selected || root.visualFocus ? Theme.c.quickSetupCard.borderSelected
                                                        : Theme.c.quickSetupCard.border

        Behavior on color {
            ColorAnimation { duration: Theme.durationFast }
        }
    }

    contentItem: Item {
        id: slot
    }
}
