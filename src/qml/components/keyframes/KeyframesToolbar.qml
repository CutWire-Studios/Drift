import QtQuick
import Drift

Item {
    id: root

    property string mode: "lanes"
    property bool normalize: false
    property bool animatedOnly: false
    property bool hasTargets: false
    property bool allTargetsKeyed: false
    property bool canClear: false
    property bool canFitSelection: false

    signal prevKey()
    signal nextKey()
    signal toggleKey()
    signal clearKeys()
    signal modeRequested(string mode)
    signal normalizeToggled()
    signal fitAll()
    signal fitSelection()
    signal animatedOnlyToggled()

    implicitHeight: Theme.controlHeight + Theme.spacingMd * 2

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: Theme.c.panelFrame.borderWidth
        color: Theme.c.keyframesToolbar.border
    }

    Flickable {
        id: flick
        anchors.fill: parent
        clip: true
        interactive: contentWidth > width
        contentWidth: row.width + Theme.spacingMd * 2
        contentHeight: height
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds

        Row {
            id: row
            x: Theme.spacingMd
            y: (flick.height - height) / 2
            spacing: Theme.spacingXs

            IconButton {
                glyph: Theme.icons.chevronLeft
                tooltip: qsTr("Previous keyframe (Alt+Left)")
                onClicked: root.prevKey()
            }

            IconButton {
                glyph: root.hasTargets && root.allTargetsKeyed ? Theme.icons.diamondMinus
                                                               : Theme.icons.diamondPlus
                enabled: root.hasTargets
                tooltip: root.hasTargets && root.allTargetsKeyed
                         ? qsTr("Remove keyframes at the playhead (K)")
                         : qsTr("Add keyframes at the playhead (K)")
                onClicked: root.toggleKey()
            }

            IconButton {
                glyph: Theme.icons.chevronRight
                tooltip: qsTr("Next keyframe (Alt+Right)")
                onClicked: root.nextKey()
            }

            IconButton {
                glyph: Theme.icons.trash
                enabled: root.canClear
                tooltip: qsTr("Clear all keyframes of the selected properties")
                onClicked: root.clearKeys()
            }

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.c.panelFrame.borderWidth
                height: Theme.controlHeightSm
                color: Theme.c.keyframesToolbar.separator
            }

            ThemedToggleButton {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Lanes")
                checked: root.mode === "lanes"
                tooltip: qsTr("Show keyframes as diamonds on lanes")
                onClicked: root.modeRequested("lanes")
            }

            ThemedToggleButton {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Curves")
                checked: root.mode === "curves"
                tooltip: qsTr("Show keyframes as value curves")
                onClicked: root.modeRequested("curves")
            }

            ThemedToggleButton {
                visible: root.mode === "curves"
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Normalize")
                checked: root.normalize
                tooltip: qsTr("Scale every curve to fill the height")
                onClicked: root.normalizeToggled()
            }

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.c.panelFrame.borderWidth
                height: Theme.controlHeightSm
                color: Theme.c.keyframesToolbar.separator
            }

            IconButton {
                glyph: Theme.icons.zoomFit
                tooltip: qsTr("Fit the clip in view")
                onClicked: root.fitAll()
            }

            IconButton {
                glyph: Theme.icons.maximize
                enabled: root.canFitSelection
                tooltip: qsTr("Fit the selected keyframes in view")
                onClicked: root.fitSelection()
            }

            ThemedChip {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Animated only")
                selected: root.animatedOnly
                tooltip: qsTr("Hide properties that have no keyframes")
                onClicked: root.animatedOnlyToggled()
            }
        }
    }
}
