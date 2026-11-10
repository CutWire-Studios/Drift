pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Drift

ComboBox {
    id: root

    property string tooltip: ""
    property var sections: []

    // Analogue of implicitContentWidth, measured across every entry instead of only the current
    // one. A width derived from implicitContentWidth changes as the selection does, and since the
    // popup takes the control's width, anything sized to a short entry elides the whole list.
    // Padded exactly like contentItem below, so the two are interchangeable in a width binding.
    readonly property real widestContentWidth:
        widestTextWidth + leftPadding + root.indicator.width + root.spacing

    readonly property real widestTextWidth: {
        void root.model // a replaced model is a new set of widths
        void root.textRole
        void root.font // FontMetrics is queried by call, so the font is not tracked on its own
        let widest = 0
        for (let i = 0; i < root.count; ++i)
            widest = Math.max(widest, textMetrics.advanceWidth(String(root.textAt(i))))
        return Math.ceil(widest)
    }

    FontMetrics {
        id: textMetrics
        font: root.font
    }

    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontSizeSm
    implicitHeight: Theme.controlHeight
    leftPadding: Theme.spacingLg
    rightPadding: Theme.spacing3xl + Theme.spacingSm
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    Accessible.role: Accessible.ComboBox
    Accessible.name: displayText

    contentItem: Text {
        leftPadding: root.leftPadding
        rightPadding: root.indicator.width + root.spacing
        text: root.displayText
        font: root.font
        color: Theme.c.themedComboBox.foreground
        opacity: root.enabled ? 1 : 0.5
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: ThemeSlot {
        slot: "ThemedComboBoxBackground"
        control: root
        fallback: Component {
            Rectangle {
                radius: Theme.c.themedComboBox.radius
                color: Theme.c.themedComboBox.background
                // Disabled used to be visually identical to enabled, so `enabled:` bindings
                // on combo boxes had no perceptible effect.
                opacity: root.enabled ? 1 : 0.5
                border.width: root.activeFocus || root.down ? Theme.c.themedComboBox.borderWidthFocus
                                                            : (root.hovered ? Theme.c.themedComboBox.borderWidth : 0)
                border.color: root.activeFocus ? Theme.c.themedComboBox.borderFocus
                                               : Theme.c.themedComboBox.border

                Behavior on border.width {
                    NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                }
                Behavior on border.color {
                    ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                }
                Behavior on opacity {
                    NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                }
            }
        }
    }

    indicator: IconGlyph {
        x: root.width - width - Theme.spacingLg
        y: root.topPadding + (root.availableHeight - height) / 2
        glyph: Theme.icons.chevronDown
        iconSize: Theme.iconSizeSm
        iconColor: Theme.c.themedComboBox.icon
        opacity: root.enabled ? 1 : 0.5

        // Points up while the list is open.
        rotation: root.popup.visible ? 180 : 0
        Behavior on rotation {
            NumberAnimation { duration: Theme.durationBase; easing.type: Theme.easing }
        }
    }

    ThemedToolTip {
        text: root.tooltip
        visible: root.tooltip.length > 0 && !root.popup.visible
                 && (root.hovered || root.visualFocus)
    }

    onPressedChanged: if (pressed) Haptics.press()
    onActivated: Haptics.select()

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        cursorShape: root.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onWheel: (wheel) => { wheel.accepted = false }
    }

    popup: Popup {
        y: root.height + Theme.spacingXs
        width: root.width
        implicitHeight: Math.min(240, contentItem.implicitHeight + 2)
        padding: 1

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: root.popup.visible ? root.delegateModel : null
            currentIndex: root.highlightedIndex
            // Arrow keys move the highlight and keep it on screen.
            keyNavigationEnabled: true
            highlightMoveDuration: Theme.durationFast
            highlightRangeMode: ListView.ApplyRange
            preferredHighlightBegin: 0
            preferredHighlightEnd: height
            ScrollBar.vertical: AppScrollBar { }
        }

        background: ThemeSlot {
            slot: "ThemedComboBoxPopupBackground"
            control: root
            fallback: Component {
                Rectangle {
                    radius: Theme.c.themedComboBox.popupRadius
                    color: Theme.c.themedComboBox.popupBackground
                    border.width: Theme.c.themedComboBox.popupBorderWidth
                    border.color: Theme.c.themedComboBox.popupBorder
                }
            }
        }

        // Opacity plus scale, matching ThemedDialog and ThemedContextMenu, so the
        // list arrives as an object rather than materialising out of nothing.
        enter: Transition {
            ParallelAnimation {
                NumberAnimation {
                    property: "opacity"
                    from: 0.0
                    to: 1.0
                    duration: Theme.durationBase
                    easing.type: Theme.easing
                }
                NumberAnimation {
                    property: "scale"
                    from: 0.96
                    to: 1.0
                    duration: Theme.durationBase
                    easing.type: Theme.easing
                }
            }
        }

        exit: Transition {
            ParallelAnimation {
                NumberAnimation {
                    property: "opacity"
                    from: 1.0
                    to: 0.0
                    duration: Theme.durationFast
                    easing.type: Theme.easing
                }
                NumberAnimation {
                    property: "scale"
                    from: 1.0
                    to: 0.96
                    duration: Theme.durationFast
                    easing.type: Theme.easing
                }
            }
        }
    }

    delegate: ThemedComboBoxDelegate { combo: root }
}
