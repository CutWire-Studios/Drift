import QtQuick
import QtQuick.Controls.Basic
import Drift

// Compact style-pack selector for the properties Style page: shows the active pack
// and opens a dialog grid to switch. Hand-edited styles (empty packId) show as Custom.
Item {
    id: root

    property string packId: ""
    signal packPicked(string packId)

    readonly property var presets: EditorState.textPresets()

    // A QVariantList from an invokable is not reactive, so saving or deleting a style is picked
    // up by poking this counter from the controller's signal.
    property int userPresetsTick: 0
    readonly property var userPresets: {
        void root.userPresetsTick
        return EditorState.userTextPresets()
    }

    Connections {
        target: EditorState
        function onUserTextPresetsChanged() { root.userPresetsTick++ }
    }

    readonly property string displayLabel: {
        if (root.packId.length === 0)
            return qsTr("Custom")
        for (let i = 0; i < root.userPresets.length; ++i) {
            if (root.userPresets[i].id === root.packId)
                return root.userPresets[i].label
        }
        for (let j = 0; j < root.presets.length; ++j) {
            if (root.presets[j].id === root.packId)
                return root.presets[j].label
        }
        return qsTr("Custom")
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

        TextStylePackThumb {
            id: triggerThumb
            anchors.left: parent.left
            anchors.leftMargin: 6
            anchors.top: parent.top
            anchors.topMargin: 6
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 6
            width: Math.round(height * 2.0)
            presetId: root.packId
            selected: false
            hovered: triggerHover.hovered
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

        HoverHandler {
            id: triggerHover
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

    Component {
        id: packDelegate

        Column {
            id: packCard
            required property var modelData
            readonly property bool selected: root.packId === modelData.id
            width: packColumn.cellWidth
            spacing: 4

            TextStylePackThumb {
                width: parent.width
                height: Math.round(width * 0.56)
                presetId: packCard.modelData.id
                selected: packCard.selected
                hovered: packHover.hovered

                HoverHandler {
                    id: packHover
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        Haptics.select()
                        root.packPicked(packCard.modelData.id)
                        dialog.close()
                    }
                }
            }

            Text {
                width: parent.width
                text: packCard.modelData.label
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
                color: packCard.selected ? Theme.primary : Theme.panelForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
        }
    }

    ThemedDialog {
        id: dialog
        title: qsTr("Text preset")
        preferredWidth: Theme.dialogWidthLg
        showAccept: false
        rejectText: qsTr("Close")

        contentItem: Flickable {
            id: flick
            clip: true
            implicitWidth: parent ? parent.width : 320
            implicitHeight: Math.min(dialog.availableContentHeight, packColumn.height)
            contentHeight: packColumn.height
            ScrollBar.vertical: AppScrollBar { }

            Column {
                id: packColumn
                width: flick.width
                spacing: 8
                // Both grids are the same shape, so the cell size is computed once here rather
                // than per grid — the shared delegate has one thing to bind to.
                readonly property real gridSpacing: Theme.spacingLg
                readonly property int columns: Math.max(2, Math.floor((width + gridSpacing) / (150 + gridSpacing)))
                readonly property real cellWidth:
                    (width - gridSpacing * (columns - 1)) / columns

                Text {
                    width: parent.width
                    visible: root.userPresets.length > 0
                    text: qsTr("My styles")
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }

                Grid {
                    id: userGrid
                    width: parent.width
                    visible: root.userPresets.length > 0
                    columns: packColumn.columns
                    spacing: packColumn.gridSpacing

                    Repeater {
                        model: root.userPresets
                        delegate: packDelegate
                    }
                }

                Text {
                    width: parent.width
                    visible: root.userPresets.length > 0
                    text: qsTr("Built-in")
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }

                Grid {
                    id: packGrid
                    width: parent.width
                    columns: packColumn.columns
                    spacing: packColumn.gridSpacing

                    Repeater {
                        model: root.presets
                        delegate: packDelegate
                    }
                }
            }
        }
    }
}
