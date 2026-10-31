import QtQuick
import Drift

CollapsibleSection {
    id: root
    property var inspector
    width: parent.width
    title: inspector.mode === "settings" ? qsTr("Alignment") : qsTr("Font")
    collapsible: false
    visible: inspector.hasTextStyle

    Text {
        visible: inspector.mode !== "settings"
        text: qsTr("Font")
        SearchAnchor {
            searchId: "text-font"
            title: qsTr("Font")
            keywords: "typeface"
        }
        color: Theme.c.textInspectorFontSection.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    FontPicker {
        visible: inspector.mode !== "settings"
        width: parent.width
        family: inspector.textStyle.fontFamily
        onFamilyPicked: family => inspector.setTextStyleKey("fontFamily", family)
    }

    Row {
        visible: inspector.mode !== "settings"
        width: parent.width
        spacing: 8

        Column {
            width: (parent.width - parent.spacing) / 2
            spacing: 4
            Text {
                text: qsTr("Weight")
                color: Theme.c.textInspectorFontSection.foregroundMuted
                font.pixelSize: Theme.fontSizeXs
                font.family: Theme.fontFamily
            }
            ThemedComboBox {
                id: fontWeightBox
                width: parent.width
                model: inspector.availableWeights.map(
                           w => inspector.weightLabels[w] || String(w))
                currentIndex: Math.max(0, inspector.availableWeights.indexOf(inspector.textStyle.fontWeight))
                onActivated: inspector.setTextStyleKey("fontWeight", inspector.availableWeights[currentIndex])
            }
        }

        PropertyKeyframeRow {
            width: (parent.width - parent.spacing) / 2
            propDef: inspector.textProp("pixelSize", qsTr("Size"), 0)
            keyframeList: inspector.textKeyframes("pixelSize")
            useSlider: true
            sliderFrom: 1
            sliderTo: 500
            unit: "px"
        }
    }

    Row {
        visible: inspector.mode !== "settings"
        width: parent.width
        spacing: 8

        Column {
            width: (parent.width - parent.spacing) / 2
            spacing: 4
            Text {
                text: qsTr("Colour")
                SearchAnchor {
                    searchId: "text-style"
                    title: qsTr("Text style")
                    keywords: "color outline shadow"
                }
                color: Theme.c.textInspectorFontSection.foregroundMuted
                font.pixelSize: Theme.fontSizeXs
                font.family: Theme.fontFamily
            }
            ColorSwatchField {
                visible: inspector.frontFillPaintKind === "solid" || inspector.frontFillPaintKind === "gradient"
                hex: inspector.frontFillColor
                tooltip: inspector.frontFillPaintKind === "gradient"
                         ? qsTr("Choose the gradient's first colour")
                         : qsTr("Choose text colour")
                onEdited: value => inspector.setTextStyleKey("color", value)
            }
            Text {
                visible: inspector.frontFillPaintKind === "gradient"
                width: parent.width
                text: qsTr("Edits the first gradient stop")
                wrapMode: Text.WordWrap
                color: Theme.c.textInspectorFontSection.foregroundMuted
                font.pixelSize: Theme.fontSizeXs
                font.family: Theme.fontFamily
            }
            ThemedChip {
                visible: inspector.frontFillPaintKind === "texture" || inspector.frontFillPaintKind === "effect"
                text: qsTr("Edit in Style")
                tooltip: inspector.frontFillPaintKind === "texture"
                         ? qsTr("The text is painted with an image; change it on the Style page")
                         : qsTr("The text is painted with an effect; change it on the Style page")
                onClicked: {
                    inspector.textTab = "style"
                    if (inspector.frontFill)
                        inspector.setLayerExpanded(inspector.frontFill.id, true)
                }
            }
        }

        Column {
            width: (parent.width - parent.spacing) / 2
            spacing: 4
            Text {
                text: qsTr("Style")
                color: Theme.c.textInspectorFontSection.foregroundMuted
                font.pixelSize: Theme.fontSizeXs
                font.family: Theme.fontFamily
            }
            Row {
                spacing: 4
                ThemedToggleButton {
                    width: 60
                    text: qsTr("Italic")
                    checked: inspector.textStyle.italic
                    enabled: inspector.familyHasItalic
                    tooltip: inspector.familyHasItalic
                             ? qsTr("Italicise the text")
                             : qsTr("%1 has no italic face").arg(inspector.textStyle.fontFamily)
                    onClicked: inspector.setTextStyleKey("italic", !inspector.textStyle.italic)
                }
                ThemedToggleButton {
                    width: 60
                    text: qsTr("Strike")
                    checked: inspector.textStyle.strikethroughEnabled
                    tooltip: qsTr("Draw a line through the text")
                    onClicked: inspector.setTextStyleKey("strikethroughEnabled", !inspector.textStyle.strikethroughEnabled)
                }
            }
        }
    }

    Row {
        visible: inspector.mode === ""
        width: parent.width
        spacing: 8

        Column {
            width: (parent.width - parent.spacing) / 2
            spacing: 4
            Text {
                text: qsTr("Case")
                color: Theme.c.textInspectorFontSection.foregroundMuted
                font.pixelSize: Theme.fontSizeXs
                font.family: Theme.fontFamily
            }
            ThemedComboBox {
                width: parent.width
                model: inspector.caseLabels
                currentIndex: Math.max(0, inspector.caseOptions.indexOf(inspector.textStyle.textCase))
                onActivated: inspector.setTextStyleKey("textCase", inspector.caseOptions[currentIndex])
            }
        }
    }

    Row {
        visible: inspector.mode !== "style"
        width: parent.width
        spacing: Theme.spacingMd

        Repeater {
            model: [
                { value: "left",   glyph: Theme.icons.alignLeft,   label: qsTr("Align left") },
                { value: "center", glyph: Theme.icons.alignCenter, label: qsTr("Align centre") },
                { value: "right",  glyph: Theme.icons.alignRight,  label: qsTr("Align right") }
            ]
            delegate: ThemedToggleButton {
                required property var modelData
                width: 34
                glyph: modelData.glyph
                checked: inspector.textStyle.align === modelData.value
                tooltip: modelData.label
                onClicked: inspector.setTextStyleKey("align", modelData.value)
            }
        }

        TextAlignDivider { }

        Repeater {
            model: [
                { value: "top",    glyph: Theme.icons.alignTop,    label: qsTr("Align top") },
                { value: "middle", glyph: Theme.icons.alignMiddle, label: qsTr("Align middle") },
                { value: "bottom", glyph: Theme.icons.alignBottom, label: qsTr("Align bottom") }
            ]
            delegate: ThemedToggleButton {
                required property var modelData
                width: 34
                glyph: modelData.glyph
                checked: inspector.textStyle.valign === modelData.value
                tooltip: modelData.label
                onClicked: inspector.setTextStyleKey("valign", modelData.value)
            }
        }
    }
}
