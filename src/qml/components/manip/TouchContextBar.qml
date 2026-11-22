import QtQuick
import Drift

// The phone's stand-in for held modifier keys and right-click: sticky chips whose state reads
// exactly like ManipModifiers, so a tool merges `modifiers` with the event's own. Tool-specific
// buttons go in as children and sit after the chips.
Rectangle {
    id: bar

    // Any of "lockRatio", "constrain", "fromCenter", "snap", "breakHandle", "exclude", in the order shown.
    property var chips: ["lockRatio", "fromCenter", "snap"]
    property bool lockRatio: false
    property bool constrain: false
    property bool fromCenter: false
    property bool snap: true
    property bool breakHandle: false
    property bool exclude: false

    // A ManipModifiers-compatible override: the same five fields, plain values.
    readonly property var modifiers: ({
        constrain: lockRatio || constrain,
        fromCenter: fromCenter,
        breakTangent: breakHandle,
        noSnap: !snap,
        exclude: exclude
    })

    default property alias actions: actionRow.data

    readonly property var labels: ({
        lockRatio: qsTr("Lock ratio"),
        constrain: qsTr("Constrain"),
        fromCenter: qsTr("From centre"),
        snap: qsTr("Snap"),
        breakHandle: qsTr("Break handle"),
        exclude: qsTr("Exclude")
    })

    function toggle(id) {
        if (id === "lockRatio") lockRatio = !lockRatio
        else if (id === "constrain") constrain = !constrain
        else if (id === "fromCenter") fromCenter = !fromCenter
        else if (id === "snap") snap = !snap
        else if (id === "breakHandle") breakHandle = !breakHandle
        else if (id === "exclude") exclude = !exclude
    }

    function isOn(id) {
        return id === "lockRatio" ? lockRatio : id === "constrain" ? constrain : id === "fromCenter" ? fromCenter : id === "snap" ? snap
             : id === "breakHandle" ? breakHandle : exclude
    }

    width: Math.min(parent ? parent.width - Theme.spacingLg * 2 : implicitWidth, implicitWidth)
    implicitWidth: row.implicitWidth + Theme.spacingMd * 2
    implicitHeight: Theme.androidMinTouchTarget + Theme.spacingSm * 2
    radius: Theme.c.manipContextBar.radius
    color: Theme.c.manipContextBar.background
    border.width: Theme.c.manipContextBar.borderWidth
    border.color: Theme.c.manipContextBar.border

    MouseArea {
        anchors.fill: parent
    }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: Theme.spacingSm

        Repeater {
            model: bar.chips

            ThemedChip {
                required property string modelData
                anchors.verticalCenter: parent.verticalCenter
                text: bar.labels[modelData]
                selected: bar.isOn(modelData)
                onClicked: bar.toggle(modelData)
            }
        }

        Row {
            id: actionRow
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingSm
        }
    }
}
