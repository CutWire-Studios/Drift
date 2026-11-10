import QtQuick
import QtQuick.Controls.Basic
import Drift

ItemDelegate {
    id: comboDelegate
    required property int index
    required property var model
    required property var modelData
    required property ComboBox combo
    width: combo.width
    readonly property string sectionLabel: {
        const s = combo.sections
        if (!s || s.length === 0 || !s[index])
            return ""
        return s[index] !== s[index - 1] ? s[index] : ""
    }
    readonly property real sectionHeight: sectionLabel !== "" ? Theme.iconButtonSize * 0.75 : 0
    height: Theme.iconButtonSize + sectionHeight
    topInset: sectionHeight
    topPadding: sectionHeight
    highlighted: combo.highlightedIndex === index
    // ItemDelegate defaults to hoverEnabled: false in Controls.Basic, so the
    // hover fill below never rendered.
    hoverEnabled: true
    // Objects may expose `available: false` (export codecs) to gray out rows.
    enabled: {
        if (modelData && modelData.available !== undefined)
            return !!modelData.available
        if (model && model.available !== undefined)
            return !!model.available
        return true
    }

    // Objects may expose `warn: true` (a decoder that runs on the wrong GPU) to flag a row
    // that works but costs something. Unlike `available` it leaves the row selectable.
    readonly property bool warned: (modelData && modelData.warn === true)
                                   || (model && model.warn === true)

    contentItem: Text {
        // modelData is undefined for multi-role item models (VoiceLibrary), so guard it.
        text: combo.textRole ? (modelData && modelData[combo.textRole] !== undefined
                               ? modelData[combo.textRole]
                               : (model[combo.textRole] !== undefined ? model[combo.textRole] : modelData))
                            : (combo.labels ? (combo.labels[modelData] || modelData) : modelData)
        color: Theme.c.themedComboBoxDelegate.foreground
        opacity: comboDelegate.enabled ? 1 : 0.4
        font: combo.font
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
        leftPadding: Theme.spacingLg
        rightPadding: comboDelegate.warned ? Theme.iconSizeSm + Theme.spacingLg * 2
                                           : Theme.spacingLg
    }

    Text {
        visible: comboDelegate.sectionLabel !== ""
        x: Theme.spacingLg
        y: 0
        height: comboDelegate.sectionHeight
        text: comboDelegate.sectionLabel
        color: Theme.c.themedComboBoxDelegate.sectionForeground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
        verticalAlignment: Text.AlignBottom
        bottomPadding: Theme.spacingXs
    }

    // The header is part of this delegate; swallow its presses so it does not pick the item.
    MouseArea {
        visible: comboDelegate.sectionLabel !== ""
        width: parent.width
        height: comboDelegate.sectionHeight
    }

    IconGlyph {
        visible: comboDelegate.warned
        glyph: Theme.icons.warning
        iconSize: Theme.iconSizeSm
        iconColor: Theme.c.themedComboBoxDelegate.icon
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingLg
        anchors.verticalCenter: parent.verticalCenter
    }

    background: Rectangle {
        color: comboDelegate.highlighted ? Theme.c.themedComboBoxDelegate.backgroundHighlighted
                                         : (comboDelegate.hovered ? Theme.c.themedComboBoxDelegate.backgroundHover : "transparent")

        Behavior on color {
            ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        cursorShape: comboDelegate.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onWheel: (wheel) => { wheel.accepted = false }
    }
    }
