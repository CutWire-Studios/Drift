import QtQuick
import Drift

CollapsibleSection {
    id: root
    property var inspector
    width: parent.width
    title: qsTr("Spacing")
    tooltip: qsTr("Line height, letter spacing, wrapping and bend")
    expanded: false
    visible: inspector.hasTextStyle
    readonly property bool phone: Theme.touchInput && Theme.compact

    Grid {
        width: parent.width
        columns: root.phone ? 1 : 2
        columnSpacing: 8
        rowSpacing: Theme.spacingMd

        PropertyKeyframeRow {
            width: root.phone ? parent.width : (parent.width - parent.columnSpacing) / 2
            propDef: inspector.textProp("lineHeight", qsTr("Line height"), 2)
            keyframeList: inspector.textKeyframes("lineHeight")
            useSlider: true
            sliderFrom: 0.5
            sliderTo: 4
        }

        PropertyKeyframeRow {
            width: root.phone ? parent.width : (parent.width - parent.columnSpacing) / 2
            propDef: inspector.textProp("letterSpacing", qsTr("Letter spacing"), 1)
            keyframeList: inspector.textKeyframes("letterSpacing")
            useSlider: true
            sliderFrom: -100
            sliderTo: 200
            unit: "px"
        }
    }

    Grid {
        width: parent.width
        columns: root.phone ? 1 : 2
        columnSpacing: 8
        rowSpacing: Theme.spacingMd

        Column {
            width: root.phone ? parent.width : (parent.width - parent.columnSpacing) / 2
            spacing: 4
            Text {
                text: qsTr("Wrapping")
                color: Theme.c.textInspectorSpacingSection.foregroundMuted
                font.pixelSize: root.phone ? Theme.fontSizeSm : Theme.fontSizeXs
                font.family: Theme.fontFamily
            }
            ThemedToggleButton {
                width: 96
                text: qsTr("Word wrap")
                checked: inspector.textStyle.wordWrap
                tooltip: qsTr("Wrap long lines inside the text box instead of overflowing")
                onClicked: inspector.setTextStyleKey("wordWrap", !inspector.textStyle.wordWrap)
            }
        }

        PropertyKeyframeRow {
            width: root.phone ? parent.width : (parent.width - parent.columnSpacing) / 2
            propDef: inspector.textProp("pathBend", qsTr("Bend"), 0)
            keyframeList: inspector.textKeyframes("pathBend")
            useSlider: true
            sliderFrom: -100
            sliderTo: 100
        }
    }
}
