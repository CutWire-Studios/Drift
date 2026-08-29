import QtQuick
import QtQuick.Controls.Basic
import Drift
import ".."

// Text tab: click a style pack to drop a styled text clip on the timeline
// (placeholder copy + inline edit). Timed captions live under Subtitles.
// Styles the user saved from the properties Text tab head the list under "My styles".
Item {
    id: root

    // A clip landed on the timeline. The phone shell closes the sheet on this —
    // the thing you came for is behind it.
    signal added()

    readonly property var presets: EditorState.textPresets()

    // A QVariantList from an invokable is not reactive, so the section is refreshed by poking
    // this counter from the controller's signal.
    property int userPresetsTick: 0
    readonly property var userPresets: {
        void root.userPresetsTick
        return EditorState.userTextPresets()
    }

    Connections {
        target: EditorState
        function onUserTextPresetsChanged() { root.userPresetsTick++ }
    }

    readonly property string styleFileFilter: qsTr("Drift text style (*.drifttextstyle)")

    function importStyle() {
        const url = FileDialogs.openFile(qsTr("Import text style"), [root.styleFileFilter])
        if (url.toString().length > 0)
            EditorState.importUserTextPreset(url)
    }

    function exportStyle(preset) {
        const url = FileDialogs.saveFile(qsTr("Export text style"), [root.styleFileFilter],
                                         preset.label, "drifttextstyle")
        if (url.toString().length > 0)
            EditorState.exportUserTextPreset(preset.id, url)
    }

    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: textColumn.height + Theme.spacing3xl
        clip: true
        ScrollBar.vertical: AppScrollBar { }

        Column {
            id: textColumn
            x: Theme.pagePadding
            width: parent.width - Theme.pagePadding * 2
            spacing: Theme.spacingMd
            topPadding: Theme.pagePadding

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("Click a style to add text at the playhead. Double-click it on the preview to edit.")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            Item {
                width: parent.width
                height: Math.max(myStylesLabel.implicitHeight, importButton.height)

                Text {
                    id: myStylesLabel
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("My styles")
                    color: Theme.panelForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    font.weight: Font.Medium
                }

                IconButton {
                    id: importButton
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: Theme.icons.folderInput
                    variant: "ghost"
                    tooltip: qsTr("Import a text style…")
                    onClicked: root.importStyle()
                }
            }

            Text {
                width: parent.width
                visible: root.userPresets.length === 0
                wrapMode: Text.WordWrap
                text: qsTr("Style some text, then use “Save style…” in the properties Text tab to keep it here.")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            Grid {
                id: userGrid
                width: parent.width
                visible: root.userPresets.length > 0
                columns: Math.max(1, Math.floor((width + Theme.assetCardGap)
                                                / (Theme.assetCardWidth + Theme.assetCardGap)))
                columnSpacing: Theme.assetCardGap
                rowSpacing: Theme.assetCardGap

                Repeater {
                    model: root.userPresets
                    delegate: Column {
                        id: userCard
                        required property var modelData
                        width: Theme.assetCardWidth
                        spacing: Theme.spacingSm

                        scale: userPress.pressed ? 0.97 : (userHover.hovered ? 1.02 : 1.0)
                        Behavior on scale {
                            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                        }

                        TextStylePackThumb {
                            width: parent.width
                            height: Math.round(width * 0.55)
                            presetId: userCard.modelData.id
                            hovered: userHover.hovered

                            HoverHandler {
                                id: userHover
                            }

                            TapHandler {
                                id: userPress
                                gesturePolicy: TapHandler.ReleaseWithinBounds
                                onTapped: {
                                    EditorState.addTextClip("", -1, userCard.modelData.id)
                                    root.added()
                                }
                            }

                            TapHandler {
                                acceptedButtons: Qt.RightButton
                                onTapped: cardMenu.popup()
                            }

                            IconButton {
                                anchors.top: parent.top
                                anchors.right: parent.right
                                anchors.margins: 2
                                visible: userHover.hovered || cardMenu.visible
                                glyph: Theme.icons.ellipsis
                                variant: "ghost"
                                buttonSize: 20
                                iconSize: 12
                                tooltip: qsTr("Style options")
                                onClicked: cardMenu.popup()
                            }

                            ThemedContextMenu {
                                id: cardMenu

                                ThemedMenuItem {
                                    text: qsTr("Rename…")
                                    icon.name: Theme.icons.pencil
                                    onTriggered: renameDialog.openFor(userCard.modelData)
                                }
                                ThemedMenuItem {
                                    text: qsTr("Export…")
                                    icon.name: Theme.icons.folderOutput
                                    onTriggered: root.exportStyle(userCard.modelData)
                                }
                                ThemedMenuSeparator { }
                                ThemedMenuItem {
                                    text: qsTr("Delete")
                                    icon.name: Theme.icons.trash
                                    onTriggered: deleteDialog.openFor(userCard.modelData)
                                }
                            }
                        }

                        Text {
                            width: parent.width
                            text: userCard.modelData.label
                            elide: Text.ElideRight
                            horizontalAlignment: Text.AlignHCenter
                            color: userHover.hovered ? Theme.panelForeground : Theme.mutedForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeXs

                            Behavior on color {
                                ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                            }
                        }
                    }
                }
            }

            Rectangle {
                width: parent.width
                height: Theme.borderWidth
                color: Theme.panelBorder
            }

            Text {
                width: parent.width
                text: qsTr("Built-in")
                color: Theme.panelForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                font.weight: Font.Medium
            }

            Grid {
                id: packGrid
                width: parent.width
                columns: Math.max(1, Math.floor((width + Theme.assetCardGap)
                                                / (Theme.assetCardWidth + Theme.assetCardGap)))
                columnSpacing: Theme.assetCardGap
                rowSpacing: Theme.assetCardGap

                Repeater {
                    model: root.presets
                    delegate: Column {
                        id: packCard
                        required property var modelData
                        width: Theme.assetCardWidth
                        spacing: Theme.spacingSm

                        scale: packPress.pressed ? 0.97 : (packHover.hovered ? 1.02 : 1.0)
                        Behavior on scale {
                            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                        }

                        TextStylePackThumb {
                            width: parent.width
                            height: Math.round(width * 0.55)
                            presetId: packCard.modelData.id
                            hovered: packHover.hovered

                            HoverHandler {
                                id: packHover
                            }

                            TapHandler {
                                id: packPress
                                gesturePolicy: TapHandler.ReleaseWithinBounds
                                onTapped: {
                                    EditorState.addTextClip("", -1, packCard.modelData.id)
                                    root.added()
                                }
                            }
                        }

                        Text {
                            width: parent.width
                            text: packCard.modelData.label
                            elide: Text.ElideRight
                            horizontalAlignment: Text.AlignHCenter
                            color: packHover.hovered ? Theme.panelForeground : Theme.mutedForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeXs

                            Behavior on color {
                                ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                            }
                        }
                    }
                }
            }
        }
    }

    NameDialog {
        id: renameDialog

        property string presetId: ""

        function openFor(preset) {
            presetId = preset.id
            openWith(qsTr("Rename text style"), preset.label)
        }

        onSubmitted: name => EditorState.renameUserTextPreset(renameDialog.presetId, name)
    }

    ThemedDialog {
        id: deleteDialog

        property string presetId: ""
        property string presetLabel: ""

        title: qsTr("Delete text style")
        acceptText: qsTr("Delete")
        acceptVariant: "destructive"
        preferredWidth: Theme.dialogWidthSm
        acceptOnReturn: false

        function openFor(preset) {
            presetId = preset.id
            presetLabel = preset.label
            open()
        }

        onAccepted: EditorState.deleteUserTextPreset(deleteDialog.presetId)

        contentItem: Text {
            width: parent ? parent.width : 320
            wrapMode: Text.WordWrap
            text: qsTr("Remove “%1” from your saved styles? Clips already using it keep their look.")
                      .arg(deleteDialog.presetLabel)
            color: Theme.panelForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
        }
    }
}
