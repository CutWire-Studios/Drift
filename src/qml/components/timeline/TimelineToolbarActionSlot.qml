import QtQuick
import Drift

// One toolbar cell: a hairline separator, or the button for one edit action.
Item {
    id: slot

    property var host
    required property string modelData
    required property int index
    readonly property bool isSeparator: modelData === "separator"

    visible: isSeparator
             ? host.separatorNeeded(host.layout.toolbar, index, host.actionShownOnToolbar)
             : host.actionShownOnToolbar(modelData)
    width: isSeparator ? Theme.c.timelineToolbarActionSlot.borderWidth : actionButton.width
    height: isSeparator ? Theme.spacing3xl : actionButton.height
    anchors.verticalCenter: parent ? parent.verticalCenter : undefined

    Rectangle {
        visible: slot.isSeparator
        anchors.fill: parent
        color: Theme.c.timelineToolbarActionSlot.foreground
    }

    TimelineToolbarToolButton {
        id: actionButton
        visible: !slot.isSeparator
        glyph: slot.isSeparator ? "" : slot.host.actionMeta[slot.modelData].glyph
        tooltip: slot.isSeparator ? "" : slot.host.actionTooltip(slot.modelData)
        tooltipIllustration: slot.isSeparator ? null : (slot.host.actionMeta[slot.modelData].illustration ?? null)
        tooltipBody: slot.isSeparator ? "" : (slot.host.actionMeta[slot.modelData].body ?? "")
        tooltipActionId: slot.isSeparator ? "" : (slot.host.actionMeta[slot.modelData].shortcut ?? "")
        active: slot.host.actionActive(slot.modelData)
        enabled: slot.host.actionEnabled(slot.modelData)
        onClicked: slot.host.triggerAction(slot.modelData)
    }
}
