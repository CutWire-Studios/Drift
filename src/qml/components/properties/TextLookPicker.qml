import QtQuick
import QtQuick.Controls.Basic
import Drift
import ".."

// Text-effect selector for the Style page: shows the active look (Shadow, Neon, …) rendered on
// the clip's own text, and opens a dialog with a roomy grid of every look to switch. Same
// trigger shape as TextStylePackPicker so the two pickers read as siblings.
Item {
    id: root

    property string lookId: ""
    property string sampleText: "Aa"
    property string fontFamily: ""
    property int fontWeight: 400
    property bool italic: false

    signal lookPicked(string lookId)

    readonly property var looks: EditorState.textLooks()
    readonly property string displayLabel: {
        for (let i = 0; i < root.looks.length; ++i) {
            if (root.looks[i].id === root.lookId)
                return root.looks[i].label
        }
        return qsTr("Custom")
    }

    function previewSource(id) {
        return "image://textlook/" + id
               + "?text=" + encodeURIComponent(root.sampleText)
               + "&font=" + encodeURIComponent(root.fontFamily)
               + "&weight=" + root.fontWeight
               + "&italic=" + (root.italic ? 1 : 0)
    }

    implicitHeight: 56
    implicitWidth: 200

    Rectangle {
        id: trigger
        anchors.fill: parent
        radius: Theme.radiusSm
        color: Theme.panelAccent
        border.width: (dialog.visible || root.activeFocus) ? 1 : 0
        border.color: root.activeFocus ? Theme.primary : Theme.panelSecondaryBorder

        Rectangle {
            id: triggerThumb
            anchors.left: parent.left
            anchors.leftMargin: 6
            anchors.top: parent.top
            anchors.topMargin: 6
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 6
            width: Math.round(height * 2.0)
            radius: Theme.radiusSm
            color: Theme.textStylePreviewBg
            border.width: Theme.borderWidth
            border.color: Theme.textStylePreviewBorder
            clip: true

            Image {
                anchors.fill: parent
                anchors.margins: 2
                visible: root.lookId.length > 0
                asynchronous: true
                cache: false
                fillMode: Image.PreserveAspectFit
                source: root.lookId.length > 0 ? root.previewSource(root.lookId) : ""
                sourceSize.width: 176
                sourceSize.height: 88
            }

            Text {
                anchors.centerIn: parent
                visible: root.lookId.length === 0
                text: qsTr("Aa")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
            }
        }

        Text {
            anchors.left: triggerThumb.right
            anchors.leftMargin: 8
            anchors.right: chevron.left
            anchors.rightMargin: 4
            anchors.verticalCenter: parent.verticalCenter
            text: root.displayLabel
            elide: Text.ElideRight
            color: Theme.panelForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
        }

        IconGlyph {
            id: chevron
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            glyph: Theme.icons.chevronRight
            iconSize: 12
            iconColor: Theme.mutedForeground
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                root.forceActiveFocus()
                dialog.open()
            }
        }
    }

    ThemedDialog {
        id: dialog
        title: qsTr("Text effect")
        preferredWidth: Theme.dialogWidthLg
        showAccept: false
        rejectText: qsTr("Close")

        contentItem: Flickable {
            id: flick
            clip: true
            implicitWidth: parent ? parent.width : 320
            implicitHeight: Math.min(dialog.availableContentHeight, lookGrid.height)
            contentHeight: lookGrid.height
            ScrollBar.vertical: AppScrollBar { }

            Grid {
                id: lookGrid
                width: flick.width
                readonly property real gap: Theme.spacingLg
                columns: Math.max(2, Math.floor((width + gap) / (150 + gap)))
                columnSpacing: gap
                rowSpacing: gap
                readonly property real tileWidth: Math.floor((width - gap * (columns - 1)) / columns)

                Repeater {
                    model: root.looks
                    delegate: Column {
                        id: lookTile
                        required property var modelData
                        readonly property bool selected: root.lookId === modelData.id
                        width: lookGrid.tileWidth
                        spacing: 4

                        Rectangle {
                            width: parent.width
                            height: Math.round(width * 0.56)
                            radius: Theme.radiusSm
                            color: Theme.textStylePreviewBg
                            border.width: lookTile.selected ? Theme.borderWidthFocus : Theme.borderWidth
                            border.color: lookTile.selected ? Theme.primary
                                                            : (lookHover.hovered ? Theme.panelMuted : Theme.textStylePreviewBorder)
                            clip: true

                            Behavior on border.color {
                                ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                            }

                            Image {
                                anchors.fill: parent
                                anchors.margins: 2
                                asynchronous: true
                                cache: false
                                fillMode: Image.PreserveAspectFit
                                source: root.previewSource(lookTile.modelData.id)
                                sourceSize.width: 300
                                sourceSize.height: 168
                            }

                            HoverHandler { id: lookHover }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    Haptics.select()
                                    root.lookPicked(lookTile.modelData.id)
                                    dialog.close()
                                }
                            }
                        }

                        Text {
                            width: parent.width
                            text: lookTile.modelData.label
                            elide: Text.ElideRight
                            horizontalAlignment: Text.AlignHCenter
                            color: lookTile.selected ? Theme.primary : Theme.panelForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeXs
                        }
                    }
                }
            }
        }
    }
}
