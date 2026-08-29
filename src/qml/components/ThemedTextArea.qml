import QtQuick
import QtQuick.Controls.Basic
import Drift

// Multi-line text field matching ThemedTextField chrome.
TextArea {
    id: root

    property string errorText: ""
    readonly property bool hasError: errorText.length > 0

    color: Theme.panelForeground
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontSizeSm
    wrapMode: TextArea.Wrap
    selectByMouse: true
    selectedTextColor: Theme.primaryForeground
    selectionColor: Theme.primary
    leftPadding: Theme.spacingLg
    rightPadding: Theme.spacingLg
    topPadding: Theme.spacingMd
    bottomPadding: Theme.spacingMd
    hoverEnabled: true
    opacity: enabled ? 1 : 0.5

    Accessible.role: Accessible.EditableText
    Accessible.description: errorText

    background: Rectangle {
        radius: Theme.radiusSm
        color: Theme.panelAccent
        border.width: root.hasError || root.activeFocus ? Theme.borderWidthFocus
                                                        : (root.hovered ? Theme.borderWidth : 0)
        border.color: root.hasError ? Theme.destructive
                                    : (root.activeFocus ? Theme.focusRing : Theme.panelMuted)

        Behavior on border.width {
            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }
        Behavior on border.color {
            ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }
    }
}
