import QtQuick
import Drift
import "components"

// Bottom navigation for the home screen.
//
// It belongs to Home rather than to AndroidMain's StackView on purpose. That StackView treats
// Home as the initialItem at depth 0 and pushes the editor over it; window.inEditor, homePage,
// showHome()/showEditor() and the whole Back chain are written against that shape. A nav bar
// owned by the StackView would have to animate itself away on every push and would leak
// knowledge of itself into AndroidEditor and AndroidMediaPreview, neither of which has a
// destination to be on.
Item {
    id: root

    property string current: "projects"
    property bool attention: false

    signal selected(string destinationId)

    readonly property real bottomInset: root.SafeArea.margins.bottom
    readonly property real leftInset: root.SafeArea.margins.left
    readonly property real rightInset: root.SafeArea.margins.right

    // Market drops out entirely in a build without a marketplace, rather than showing a
    // destination whose only content is "unavailable". The slot width divides by this list's
    // length, so the two that remain simply take half the bar each. AndroidHome keeps all three
    // pages in its StackLayout so the indices behind `current` do not shift with it.
    readonly property var destinations: {
        const all = [
            { id: "projects", label: qsTr("Projects"), icon: Theme.icons.film },
            { id: "market", label: qsTr("Market"), icon: Theme.icons.store },
            { id: "me", label: qsTr("Me"), icon: Theme.icons.settings }
        ]
        return Market.configured ? all : all.filter(d => d.id !== "market")
    }

    implicitHeight: Theme.androidBottomRailHeight + bottomInset

    Rectangle {
        anchors.fill: parent
        color: Theme.panelBackground

        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: Theme.borderWidth
            color: Theme.panelBorder
        }
    }

    Item {
        id: navBody
        anchors.fill: parent
        anchors.bottomMargin: root.bottomInset
        anchors.leftMargin: root.leftInset
        anchors.rightMargin: root.rightInset

        readonly property real slotWidth: width / root.destinations.length

        Repeater {
            model: root.destinations

            delegate: NavRailButton {
                required property var modelData
                required property int index

                x: index * navBody.slotWidth
                width: navBody.slotWidth
                height: navBody.height
                entry: modelData
                selected: root.current === modelData.id
                onClicked: root.selected(modelData.id)

                // The "something needs you" signal the editor's Extras button carries as a
                // pulse. It lands on Me because that is where updates and packs now live.
                Rectangle {
                    visible: root.attention && modelData.id === "me"
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.horizontalCenterOffset: Theme.iconSizeLg / 2
                    anchors.top: parent.top
                    anchors.topMargin: Theme.spacingMd
                    width: 8
                    height: 8
                    radius: 4
                    color: Theme.destructive
                }
            }
        }
    }
}
