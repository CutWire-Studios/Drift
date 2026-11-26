import QtQuick
import Drift

CollapsibleSection {
    id: root
    property var inspector
    width: parent.width
    title: qsTr("Scroll")
    tooltip: qsTr("Roll the text up the screen like end credits, or crawl it sideways like a ticker")
    expanded: (inspector.textStyle.scroll || "none") !== "none"
    visible: inspector.hasTextStyle && inspector.clipKind === "text"

    readonly property var modes: ["none", "rollUp", "crawlLeft", "crawlRight"]
    readonly property var modeLabels: [qsTr("None"), qsTr("Roll up"), qsTr("Crawl left"), qsTr("Crawl right")]
    readonly property bool scrolling: (inspector.textStyle.scroll || "none") !== "none"

    ThemedComboBox {
        width: parent.width
        model: root.modeLabels
        currentIndex: Math.max(0, root.modes.indexOf(inspector.textStyle.scroll || "none"))
        onActivated: inspector.setTextStyleKey("scroll", root.modes[currentIndex])
    }

    ThemedLabel {
        width: parent.width
        visible: root.scrolling
        size: "xs"
        wrapMode: Text.WordWrap
        text: qsTr("The text crosses the screen over the clip's length: a longer clip scrolls slower.")
    }

    Row {
        visible: root.scrolling
        spacing: Theme.spacingMd

        ThemedToggleButton {
            text: qsTr("Enter off-screen")
            checked: inspector.textStyle.scrollEnterOffscreen !== false
            tooltip: qsTr("Start with the text fully outside the frame; off, it starts where the box lays it out")
            onClicked: inspector.setTextStyleKey("scrollEnterOffscreen", inspector.textStyle.scrollEnterOffscreen === false)
        }

        ThemedToggleButton {
            text: qsTr("Exit off-screen")
            checked: inspector.textStyle.scrollExitOffscreen !== false
            tooltip: qsTr("End with the text fully outside the frame; off, it settles where the box lays it out")
            onClicked: inspector.setTextStyleKey("scrollExitOffscreen", inspector.textStyle.scrollExitOffscreen === false)
        }
    }
}
