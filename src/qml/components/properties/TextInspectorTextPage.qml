import QtQuick
import Drift

Column {
    id: root
    property var inspector
    property alias contentField: contentSection.contentField
    width: parent ? parent.width : implicitWidth
    spacing: Theme.spacingMd
    visible: inspector.showFont || inspector.showSpacing || inspector.showContent

    TextInspectorContentSection {
        id: contentSection
        visible: inspector.showContent
        width: parent.width
        inspector: root.inspector
    }
    TextInspectorFontSection {
        visible: inspector.showFont && inspector.hasTextStyle
        width: parent.width
        inspector: root.inspector
    }
    TextInspectorSpacingSection {
        visible: inspector.showSpacing && inspector.hasTextStyle
        expanded: inspector.mode === "settings"
        width: parent.width
        inspector: root.inspector
    }
}
