import QtQuick
import Drift

CollapsibleSection {
    id: root
    property var inspector
    property alias boxPaddingField: boxPaddingField
    property alias boxRadiusField: boxRadiusField
    property alias wordHighlightPaddingField: wordHighlightPaddingField
    property alias wordHighlightRadiusField: wordHighlightRadiusField
    property alias underlineWidthField: underlineWidthField
    property alias underlineOffsetField: underlineOffsetField
    width: parent.width
    title: qsTr("Decorations")
    tooltip: qsTr("Boxes and rules drawn around the text rather than on it")
    expanded: false

    CollapsibleSection {
        width: parent.width
        title: qsTr("Background")
        tooltip: qsTr("Draw a filled box behind the text")
        collapsible: false
        showSeparator: false
        showSwitch: true
        switchChecked: inspector.textStyle.boxEnabled
        switchTooltip: qsTr("Draw a filled box behind the text")
        onSwitchToggled: on => inspector.setTextStyleKey("boxEnabled", on)

        ColorSwatchField {
            hex: inspector.textStyle.boxColor
            tooltip: qsTr("Choose background colour")
            livePreview: true
            onPreviewed: value => inspector.previewTextStyle({ "boxColor": value })
            onEdited: value => inspector.setTextStyleKey("boxColor", value)
        }

        Row {
            width: parent.width
            spacing: 8

            Column {
                width: (parent.width - parent.spacing) / 2
                spacing: 4
                Text {
                    text: qsTr("Padding")
                    HoverHandler { id: tipHover1088 }
                    TapHandler {
                        id: tipTap1088
                        acceptedDevices: PointerDevice.TouchScreen
                        onLongPressed: tip1088.held = true
                        onPressedChanged: if (!pressed) tip1088.held = false
                    }
                    ThemedToolTip { id: tip1088; property bool held: false; text: qsTr("Space between the text and the edge of its background box"); visible: tipHover1088.hovered || held }
                    color: Theme.c.textInspectorDecorations.foregroundMuted
                    font.pixelSize: Theme.fontSizeXs
                    font.family: Theme.fontFamily
                }
                ThemedNumberField {
                    id: boxPaddingField
                    to: 500
                    unit: "px"
                    width: parent.width
                    decimals: 1
                    step: 1
                    from: 0
                    onEdited: v => inspector.setTextStyleKey("boxPadding", v)
                }
            }

            Column {
                width: (parent.width - parent.spacing) / 2
                spacing: 4
                Text {
                    text: qsTr("Corner radius")
                    HoverHandler { id: tipHover1109 }
                    TapHandler {
                        id: tipTap1109
                        acceptedDevices: PointerDevice.TouchScreen
                        onLongPressed: tip1109.held = true
                        onPressedChanged: if (!pressed) tip1109.held = false
                    }
                    ThemedToolTip { id: tip1109; property bool held: false; text: qsTr("Roundness of the background box corners"); visible: tipHover1109.hovered || held }
                    color: Theme.c.textInspectorDecorations.foregroundMuted
                    font.pixelSize: Theme.fontSizeXs
                    font.family: Theme.fontFamily
                }
                ThemedNumberField {
                    id: boxRadiusField
                    to: 500
                    unit: "px"
                    width: parent.width
                    decimals: 1
                    step: 1
                    from: 0
                    onEdited: v => inspector.setTextStyleKey("boxRadius", v)
                }
            }
        }
    }

    CollapsibleSection {
        width: parent.width
        title: qsTr("Word highlight")
        tooltip: qsTr("Filled pill behind every word, sized to the word itself")
        collapsible: false
        showSeparator: false
        showSwitch: true
        switchChecked: inspector.textStyle.wordHighlight.enabled
        switchTooltip: qsTr("Filled pill behind every word, sized to the word itself")
        onSwitchToggled: on => inspector.setTextGroupKey("wordHighlight", "enabled", on)

        Row {
            width: parent.width
            spacing: 8

            Column {
                width: (parent.width - parent.spacing) / 2
                spacing: 4
                Text {
                    text: qsTr("Thickness")
                    HoverHandler { id: tipHoverHlPad }
                    TapHandler {
                        id: tipTapHlPad
                        acceptedDevices: PointerDevice.TouchScreen
                        onLongPressed: tipHlPad.held = true
                        onPressedChanged: if (!pressed) tipHlPad.held = false
                    }
                    ThemedToolTip { id: tipHlPad; property bool held: false; text: qsTr("How far the pill extends past the word"); visible: tipHoverHlPad.hovered || held }
                    color: Theme.c.textInspectorDecorations.foregroundMuted
                    font.pixelSize: Theme.fontSizeXs
                    font.family: Theme.fontFamily
                }
                ThemedNumberField {
                    id: wordHighlightPaddingField
                    to: 200
                    unit: "px"
                    width: parent.width
                    decimals: 1
                    step: 1
                    from: 0
                    onEdited: v => inspector.setTextGroupKey("wordHighlight", "padding", v)
                }
            }

            Column {
                width: (parent.width - parent.spacing) / 2
                spacing: 4
                Text {
                    text: qsTr("Corner radius")
                    color: Theme.c.textInspectorDecorations.foregroundMuted
                    font.pixelSize: Theme.fontSizeXs
                    font.family: Theme.fontFamily
                }
                ThemedNumberField {
                    id: wordHighlightRadiusField
                    to: 200
                    unit: "px"
                    width: parent.width
                    decimals: 1
                    step: 1
                    from: 0
                    onEdited: v => inspector.setTextGroupKey("wordHighlight", "radius", v)
                }
            }
        }

        Column {
            width: parent.width
            spacing: 4
            Text {
                text: qsTr("Highlight colour")
                color: Theme.c.textInspectorDecorations.foregroundMuted
                font.pixelSize: Theme.fontSizeXs
                font.family: Theme.fontFamily
            }
            ColorSwatchField {
                hex: inspector.textStyle.wordHighlight.color
                tooltip: qsTr("Choose highlight colour")
                livePreview: true
                onPreviewed: value => inspector.previewTextStyle({ "wordHighlight": { "color": value } })
                onEdited: value => inspector.setTextGroupKey("wordHighlight", "color", value)
            }
        }
    }

    CollapsibleSection {
        width: parent.width
        title: qsTr("Underline")
        tooltip: qsTr("Draw a rule under each line of text")
        collapsible: false
        showSeparator: false
        showSwitch: true
        switchChecked: inspector.textStyle.underlineEnabled
        switchTooltip: qsTr("Draw a rule under each line of text")
        onSwitchToggled: on => inspector.setTextStyleKey("underlineEnabled", on)

        Row {
            width: parent.width
            spacing: 8

            Column {
                width: (parent.width - parent.spacing) / 2
                spacing: 4
                Text {
                    text: qsTr("Thickness")
                    color: Theme.c.textInspectorDecorations.foregroundMuted
                    font.pixelSize: Theme.fontSizeXs
                    font.family: Theme.fontFamily
                }
                ThemedNumberField {
                    id: underlineWidthField
                    to: 100
                    unit: "px"
                    width: parent.width
                    decimals: 1
                    step: 0.5
                    from: 0
                    onEdited: v => inspector.setTextStyleKey("underlineWidth", v)
                }
            }

            Column {
                width: (parent.width - parent.spacing) / 2
                spacing: 4
                Text {
                    text: qsTr("Offset")
                    HoverHandler { id: tipHoverUnderlineOffset }
                    TapHandler {
                        id: tipTapUnderlineOffset
                        acceptedDevices: PointerDevice.TouchScreen
                        onLongPressed: tipUnderlineOffset.held = true
                        onPressedChanged: if (!pressed) tipUnderlineOffset.held = false
                    }
                    ThemedToolTip { id: tipUnderlineOffset; property bool held: false; text: qsTr("Gap between the baseline and the rule"); visible: tipHoverUnderlineOffset.hovered || held }
                    color: Theme.c.textInspectorDecorations.foregroundMuted
                    font.pixelSize: Theme.fontSizeXs
                    font.family: Theme.fontFamily
                }
                ThemedNumberField {
                    id: underlineOffsetField
                    to: 200
                    unit: "px"
                    width: parent.width
                    decimals: 1
                    step: 1
                    from: -200
                    onEdited: v => inspector.setTextStyleKey("underlineOffset", v)
                }
            }
        }

        Column {
            width: parent.width
            spacing: 4
            Text {
                text: qsTr("Underline colour")
                color: Theme.c.textInspectorDecorations.foregroundMuted
                font.pixelSize: Theme.fontSizeXs
                font.family: Theme.fontFamily
            }
            ColorSwatchField {
                hex: inspector.textStyle.underlineColor
                tooltip: qsTr("Choose underline colour")
                livePreview: true
                onPreviewed: value => inspector.previewTextStyle({ "underlineColor": value })
                onEdited: value => inspector.setTextStyleKey("underlineColor", value)
            }
        }
    }
}
