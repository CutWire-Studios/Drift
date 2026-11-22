import QtQuick
import Drift

Column {
    id: root
    property var inspector
    property alias boxPaddingField: lookSection.boxPaddingField
    property alias boxRadiusField: lookSection.boxRadiusField
    property alias wordHighlightPaddingField: lookSection.wordHighlightPaddingField
    property alias wordHighlightRadiusField: lookSection.wordHighlightRadiusField
    property alias underlineWidthField: lookSection.underlineWidthField
    property alias underlineOffsetField: lookSection.underlineOffsetField
    property alias accentEveryNField: accentSection.accentEveryNField
    property alias accentSizeScaleField: accentSection.accentSizeScaleField
    property alias accentOutlineWidthField: accentSection.accentOutlineWidthField
    width: parent ? parent.width : implicitWidth
    spacing: Theme.spacingMd
    visible: inspector.showStyle && inspector.hasTextStyle

    TextInspectorPresetSection {
        visible: inspector.showPreset && inspector.hasTextStyle
        width: parent.width
        inspector: root.inspector
    }
    TextInspectorLookSection {
        id: lookSection
        visible: inspector.showLook && inspector.hasTextStyle
        width: parent.width
        inspector: root.inspector
    }
    TextInspectorAccentSection {
        id: accentSection
        visible: inspector.showLook && inspector.hasTextStyle
        width: parent.width
        inspector: root.inspector
    }
}
