import QtQuick
import Drift
// Category chips for a source. Picking one sets the category of the grid this section targets;
// a category with children (sound effect groups) shows them as a second row.
Item {
    id: root

    required property var section
    required property var host

    readonly property var categories: Marketplace.source((root.section.source || ({})).id || "").categories || []
    readonly property string selected: root.host.categoryFor(root.section.target)
    readonly property string topSelected: selected.split("/")[0]
    readonly property var subcategories: {
        for (let i = 0; i < categories.length; ++i) {
            if (categories[i].id === topSelected)
                return categories[i].children || []
        }
        return []
    }

    visible: categories.length > 0
    implicitHeight: categories.length > 0 ? column.implicitHeight : 0

    function childValue(child) {
        return child.id.indexOf("/") >= 0 ? child.id : root.topSelected + "/" + child.id
    }

    Column {
        id: column
        width: parent.width
        spacing: Theme.spacingSm

        ScrollStrip {
            width: parent.width
            height: Theme.controlHeightSm
            padding: root.host.sidePadding

            Row {
                height: parent.height
                spacing: Theme.spacingSm

                ThemedChip {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("All")
                    variant: "secondary"
                    selected: root.selected === ""
                    onClicked: root.host.setCategory(root.section.target, "")
                }
                Repeater {
                    model: root.categories
                    delegate: ThemedChip {
                        required property var modelData
                        required property int index
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.label
                        variant: "secondary"
                        accentColor: Theme.categoryColor(index)
                        selected: root.topSelected === modelData.id
                        onClicked: root.host.setCategory(root.section.target, modelData.id)
                    }
                }
            }
        }

        ScrollStrip {
            width: parent.width
            height: Theme.controlHeightSm
            visible: root.subcategories.length > 1
            padding: root.host.sidePadding

            Row {
                height: parent.height
                spacing: Theme.spacingSm

                ThemedChip {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("All")
                    variant: "secondary"
                    selected: root.selected === root.topSelected
                    onClicked: root.host.setCategory(root.section.target, root.topSelected)
                }
                Repeater {
                    model: root.subcategories
                    delegate: ThemedChip {
                        required property var modelData
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.label
                        variant: "secondary"
                        selected: root.selected === root.childValue(modelData)
                        onClicked: root.host.setCategory(root.section.target, root.childValue(modelData))
                    }
                }
            }
        }
    }
}
