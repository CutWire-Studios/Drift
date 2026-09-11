import QtQuick
import QtQuick.Controls.Basic
import Drift
import "components"

// CapCut-style bottom tool rail: four destinations either side of a centred Add
// button.
//
// This used to be all eleven asset tabs in a horizontally scrolling strip. Five of
// them fit a 411dp phone, the other six were behind a scroll with no affordance
// saying so, and every one of the five "put something new on the timeline" tabs
// spent a permanent slot on an action taken once or twice per session. The adds
// collapse into AndroidAddMenu behind the [+]; what stays here is the four things
// you come back to while editing.
Item {
    id: root

    // "" | "edit" | "effects" | "sounds" | "transitions", plus any tab id the
    // Add menu routes to — an open Media sheet still lights nothing here, which is
    // correct: it is not one of the four destinations.
    property string activeId: ""
    signal tabRequested(string tabId)
    signal editRequested()

    readonly property bool hasSelection: {
        void EditorState.selection
        return EditorState.selectedTrack >= 0 && EditorState.selectedClip >= 0
    }
    signal addRequested()

    // Clear the system gesture/nav bar without leaving a dead strip under the rail.
    // The side insets matter in landscape, where a cutout or the gesture pill sits
    // beside the rail and would otherwise swallow the first and last destinations.
    readonly property real bottomInset: SafeArea.margins.bottom
    readonly property real leftInset: SafeArea.margins.left
    readonly property real rightInset: SafeArea.margins.right

    height: Theme.androidBottomRailHeight + bottomInset
    width: parent ? parent.width : 0

    // Ids and labels mirror AssetsPanel.qml's tabsModel so the rail and the sheet
    // header cannot disagree; "edit" is the extra Android entry that opens
    // PropertiesPanel. Order puts the two clip-shaping destinations left of the
    // Add button and the two timing/sound ones right of it.
    readonly property var items: [
        { id: "edit", label: qsTr("Edit"), icon: Theme.icons.sliders },
        { id: "effects", label: qsTr("Effects"), icon: Theme.icons.wand },
        { id: "sounds", label: qsTr("Audio FX"), icon: Theme.icons.audioLines },
        { id: "transitions", label: qsTr("Transitions"), icon: Theme.icons.chevronsRight }
    ]

    Rectangle {
        anchors.fill: parent
        color: Theme.panelBackground
    }

    Rectangle {
        anchors.top: parent.top
        width: parent.width
        height: 1
        color: Theme.panelBorder
        z: 1
    }

    Item {
        id: railBody
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.bottomInset
        anchors.leftMargin: root.leftInset
        anchors.rightMargin: root.rightInset

        // Five equal slots, so nothing scrolls off and every target is a fifth of
        // the rail wide — far past the 48dp floor even on a 360dp phone.
        readonly property real slotWidth: width / 5

        component RailButton: AbstractButton {
            id: railBtn
            required property var entry
            property bool selected: false

            height: railBody.height
            hoverEnabled: true

            Accessible.role: Accessible.Button
            Accessible.name: entry.label
            // Nav state has to reach assistive tech too, not just the tint below.
            Accessible.checkable: true
            Accessible.checked: railBtn.selected

            onClicked: Haptics.select()

            // Press feedback. On the root rather than the background so the label
            // dips with the icon, and via scale so packed slots do not reflow.
            scale: railBtn.down ? Theme.pressScale : 1.0

            Behavior on scale {
                NumberAnimation { duration: Theme.durationPress; easing.type: Theme.easing }
            }

            background: Item { }

            contentItem: Item {
                anchors.fill: parent

                Column {
                    anchors.centerIn: parent
                    spacing: 3

                    // Selection pill behind the glyph. The old rail signalled the
                    // active destination with tint alone — and the tint was
                    // Theme.primary, which sits at 1.69:1 on the light panel and so
                    // said nothing at all in light mode.
                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: Theme.iconSizeLg + Theme.spacing2xl
                        height: Theme.iconSizeLg + Theme.spacingLg
                        radius: Theme.radiusPill
                        color: railBtn.selected ? Theme.panelSecondaryBg
                                                : (railBtn.down ? Theme.panelAccent : "transparent")

                        Behavior on color {
                            ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                        }

                        IconGlyph {
                            anchors.centerIn: parent
                            glyph: railBtn.entry.icon
                            iconSize: Theme.iconSizeLg
                            iconColor: railBtn.selected ? Theme.accentOnPanel
                                                        : Theme.mutedForeground
                        }
                    }

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: railBtn.entry.label
                        color: railBtn.selected ? Theme.accentOnPanel : Theme.mutedForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                        font.weight: railBtn.selected ? Font.Medium : Font.Normal
                    }
                }
            }
        }

        // Edit is the one slot that needs something selected. Dimmed and answered with a
        // toast rather than silently opening an empty sheet: a rail slot that does nothing
        // when tapped reads as a broken app.
        RailButton {
            x: 0
            width: railBody.slotWidth
            entry: root.items[0]
            selected: root.activeId === entry.id
            opacity: root.hasSelection ? 1 : 0.45
            onClicked: {
                Haptics.select()
                if (!root.hasSelection) {
                    Toasts.info(qsTr("Tap a clip to edit it"))
                    return
                }
                root.editRequested()
            }
        }

        RailButton {
            x: railBody.slotWidth
            width: railBody.slotWidth
            entry: root.items[1]
            selected: root.activeId === entry.id
            onClicked: root.tabRequested(entry.id)
        }

        // Centre slot: everything that puts a new clip on the timeline.
        AbstractButton {
            id: addButton
            x: railBody.slotWidth * 2
            width: railBody.slotWidth
            height: railBody.height
            hoverEnabled: true

            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Add to timeline")

            scale: addButton.down ? Theme.pressScale : 1.0

            Behavior on scale {
                NumberAnimation { duration: Theme.durationPress; easing.type: Theme.easing }
            }

            background: Item { }

            contentItem: Item {
                anchors.fill: parent

                Rectangle {
                    anchors.centerIn: parent
                    width: Theme.androidRailFabSize
                    height: Theme.androidRailFabSize
                    radius: Theme.radiusLg
                    // The one filled control in the shell, so "add something" reads
                    // as the rail's primary action rather than a fifth tab. `primary`
                    // is a fill here, not a foreground, so it needs no light-mode
                    // variant — primaryForeground is 9.8:1 on it in both themes.
                    color: addButton.down ? Qt.darker(Theme.primary, 1.12) : Theme.primary

                    Behavior on color {
                        ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                    }

                    IconGlyph {
                        anchors.centerIn: parent
                        glyph: Theme.icons.plus
                        iconSize: Theme.iconSizeXl
                        iconColor: Theme.primaryForeground
                    }
                }
            }

            onClicked: {
                Haptics.confirm()
                root.addRequested()
            }
        }

        RailButton {
            x: railBody.slotWidth * 3
            width: railBody.slotWidth
            entry: root.items[2]
            selected: root.activeId === entry.id
            onClicked: root.tabRequested(entry.id)
        }

        RailButton {
            x: railBody.slotWidth * 4
            width: railBody.slotWidth
            entry: root.items[3]
            selected: root.activeId === entry.id
            onClicked: root.tabRequested(entry.id)
        }
    }
}
