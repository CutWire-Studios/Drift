import QtQuick
import Drift

CollapsibleSection {
    id: root
    property var inspector
    property alias accentEveryNField: accentEveryNField
    property alias accentSizeScaleField: accentSizeScaleField
    property alias accentOutlineWidthField: accentOutlineWidthField
    width: parent.width
    title: qsTr("Word accent")
    tooltip: qsTr("Style some words differently from the rest, chosen by rule")
    expanded: false
    visible: inspector.hasTextStyle

    Row {
        width: parent.width
        spacing: 8

        ThemedComboBox {
            id: accentRuleBox
            width: inspector.textStyle.accent.rule === "everyNth"
                   ? (parent.width - parent.spacing) * 0.66 : parent.width
            model: inspector.accentRuleLabels
            currentIndex: Math.max(0, inspector.accentRules.indexOf(inspector.textStyle.accent.rule))
            onActivated: inspector.setTextGroupKey("accent", "rule",
                                              inspector.accentRules[currentIndex])
        }

        ThemedNumberField {
            id: accentEveryNField
            visible: inspector.textStyle.accent.rule === "everyNth"
            width: (parent.width - parent.spacing) * 0.34
            to: 16
            from: 1
            decimals: 0
            step: 1
            onEdited: v => inspector.setTextGroupKey("accent", "n", v)
        }
    }

    Column {
        width: parent.width
        spacing: Theme.spacingMd
        visible: inspector.textStyle.accent.rule !== "none"

        CollapsibleSection {
            width: parent.width
            title: qsTr("Accent colour")
            tooltip: qsTr("Recolour the words the rule picks out")
            collapsible: false
            showSeparator: false
            showSwitch: true
            switchChecked: inspector.textStyle.accent.colorEnabled
            switchTooltip: qsTr("Recolour the words the rule picks out")
            onSwitchToggled: on => inspector.setTextGroupKey("accent", "colorEnabled", on)

            ColorSwatchField {
                hex: inspector.textStyle.accent.color
                tooltip: qsTr("Choose accent colour")
                livePreview: true
                onPreviewed: value => inspector.previewTextStyle({ "accent": { "color": value } })
                onEdited: value => inspector.setTextGroupKey("accent", "color", value)
            }
        }

        Column {
            width: (parent.width - 8) / 2
            spacing: 4
            Text {
                text: qsTr("Accent size")
                HoverHandler { id: tipHoverAccentSize }
                TapHandler {
                    id: tipTapAccentSize
                    acceptedDevices: PointerDevice.TouchScreen
                    onLongPressed: tipAccentSize.held = true
                    onPressedChanged: if (!pressed) tipAccentSize.held = false
                }
                ThemedToolTip {
                    id: tipAccentSize
                    property bool held: false
                    text: qsTr("Size of the accented words relative to the rest of the line")
                    visible: tipHoverAccentSize.hovered || held
                }
                color: Theme.c.textInspectorAccentSection.foregroundMuted
                font.pixelSize: Theme.fontSizeXs
                font.family: Theme.fontFamily
            }
            ThemedNumberField {
                id: accentSizeScaleField
                to: 4
                unit: "x"
                width: parent.width
                decimals: 2
                step: 0.05
                from: 0.25
                onEdited: v => inspector.setTextGroupKey("accent", "sizeScale", v)
            }
        }

        CollapsibleSection {
            width: parent.width
            title: qsTr("Accent outline")
            tooltip: qsTr("Give the accented words their own outline")
            collapsible: false
            showSeparator: false
            showSwitch: true
            switchChecked: inspector.textStyle.accent.outlineEnabled
            switchTooltip: qsTr("Give the accented words their own outline")
            onSwitchToggled: on => inspector.setTextGroupKey("accent", "outlineEnabled", on)

            ColorSwatchField {
                hex: inspector.textStyle.accent.outlineColor
                tooltip: qsTr("Choose accent outline colour")
                livePreview: true
                onPreviewed: value => inspector.previewTextStyle({ "accent": { "outlineColor": value } })
                onEdited: value => inspector.setTextGroupKey("accent", "outlineColor", value)
            }

            Column {
                width: (parent.width - 8) / 2
                spacing: 4
                Text {
                    text: qsTr("Width")
                    color: Theme.c.textInspectorAccentSection.foregroundMuted
                    font.pixelSize: Theme.fontSizeXs
                    font.family: Theme.fontFamily
                }
                ThemedNumberField {
                    id: accentOutlineWidthField
                    to: 100
                    unit: "px"
                    width: parent.width
                    decimals: 1
                    step: 0.5
                    from: 0
                    onEdited: v => inspector.setTextGroupKey("accent", "outlineWidth", v)
                }
            }
        }

        CollapsibleSection {
            width: parent.width
            title: qsTr("Accent pill")
            tooltip: qsTr("Highlight only the accented words, instead of every word")
            collapsible: false
            showSeparator: false
            showSwitch: true
            switchChecked: inspector.textStyle.accent.highlight.enabled
            switchTooltip: qsTr("Highlight only the accented words, instead of every word")
            onSwitchToggled: on => inspector.setTextAccentHighlightKey("enabled", on)

            ColorSwatchField {
                hex: inspector.textStyle.accent.highlight.color
                tooltip: qsTr("Choose accent highlight colour")
                livePreview: true
                onPreviewed: value => inspector.previewTextStyle({ "accent": { "highlight": { "color": value } } })
                onEdited: value => inspector.setTextAccentHighlightKey("color", value)
            }
        }
    }
}
