pragma ComponentBehavior: Bound
import QtQuick
import Drift

// Right-click context menu for selecting fade and transition curves (Linear, Fast, Slow, Smooth, Sharp, Natural).
ThemedContextMenu {
    id: root

    property string titleText: qsTr("Fade Curve")
    property string currentCurve: "smooth"

    signal curveSelected(string curve)
    signal openEditorRequested()

    ThemedMenuItem {
        sectionHeader: true
        text: root.titleText
    }

    ThemedMenuItem {
        text: qsTr("Linear")
        icon.name: root.currentCurve === "linear" ? Theme.icons.check : ""
        onTriggered: root.curveSelected("linear")
    }

    ThemedMenuItem {
        text: qsTr("Fast")
        icon.name: root.currentCurve === "fast" ? Theme.icons.check : ""
        onTriggered: root.curveSelected("fast")
    }

    ThemedMenuItem {
        text: qsTr("Slow")
        icon.name: root.currentCurve === "slow" ? Theme.icons.check : ""
        onTriggered: root.curveSelected("slow")
    }

    ThemedMenuItem {
        text: qsTr("Smooth (S-Curve)")
        icon.name: root.currentCurve === "smooth" ? Theme.icons.check : ""
        onTriggered: root.curveSelected("smooth")
    }

    ThemedMenuItem {
        text: qsTr("Sharp")
        icon.name: root.currentCurve === "sharp" ? Theme.icons.check : ""
        onTriggered: root.curveSelected("sharp")
    }

    ThemedMenuItem {
        text: qsTr("Natural (Equal Power)")
        icon.name: root.currentCurve === "equalPower" ? Theme.icons.check : ""
        onTriggered: root.curveSelected("equalPower")
    }

    ThemedMenuSeparator {}

    ThemedMenuItem {
        text: qsTr("Curve Editor…")
        icon.name: Theme.icons.spline
        onTriggered: root.openEditorRequested()
    }
}
