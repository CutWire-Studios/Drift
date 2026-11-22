pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Drift

TextField {
    id: root

    // Set to a non-empty string to mark the field invalid: the border turns
    // destructive and the message renders below the field.
    property string errorText: ""
    readonly property bool hasError: errorText.length > 0
    // ThemedNumberField points these at its own group so the two fields theme apart.
    property var colorTokens: Theme.c.themedTextField
    property string backgroundSlot: "ThemedTextFieldBackground"

    color: colorTokens.foreground
    // The Basic style defaults placeholderTextColor to a fixed dark gray that
    // vanishes against panelAccent in dark mode; track the theme instead.
    placeholderTextColor: colorTokens.placeholderForeground
    font.family: Theme.monoFontFamily
    font.pixelSize: Theme.fontSizeSm
    selectByMouse: true
    selectedTextColor: colorTokens.selectionForeground
    selectionColor: colorTokens.selectionBackground
    leftPadding: Theme.spacingLg
    rightPadding: Theme.spacingLg
    topPadding: Theme.spacingMd
    bottomPadding: Theme.spacingMd
    implicitHeight: Theme.controlHeight
    containmentMask: Theme.phone ? fieldHit : null

    Item {
        id: fieldHit
        anchors.fill: parent
        anchors.topMargin: -Math.max(0, (Theme.androidMinTouchTarget - root.height) / 2)
        anchors.bottomMargin: anchors.topMargin
    }
    hoverEnabled: true
    opacity: enabled ? 1 : 0.5

    Accessible.role: Accessible.EditableText
    Accessible.description: errorText

    background: ThemeSlot {
        slot: root.backgroundSlot
        control: root
        fallback: Component {
            Rectangle {
                radius: root.colorTokens.radius
                color: root.colorTokens.background
                // The focus ring used to be panelSecondaryBorder (#002b47 in dark), which
                // was near-invisible against panelAccent (#262626).
                border.width: root.hasError || root.activeFocus ? root.colorTokens.borderWidthFocus
                                                                : (root.hovered ? root.colorTokens.borderWidth : 0)
                border.color: root.hasError ? root.colorTokens.borderError
                                            : (root.activeFocus ? root.colorTokens.borderFocus
                                                                : root.colorTokens.border)

                Behavior on border.width {
                    NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                }
                Behavior on border.color {
                    ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                }
            }
        }
    }

    // Inline validation message. Positioned outside the field's own height so it
    // never shifts the control itself.
    Text {
        id: errorLabel
        anchors.top: parent.bottom
        anchors.topMargin: Theme.spacingXs
        anchors.left: parent.left
        width: parent.width
        text: root.errorText
        visible: root.hasError
        color: root.colorTokens.errorForeground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeTiny
        wrapMode: Text.WordWrap

        opacity: visible ? 1 : 0
        Behavior on opacity {
            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }
    }
}
