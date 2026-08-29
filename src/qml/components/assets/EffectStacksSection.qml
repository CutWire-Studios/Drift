import QtQuick
import QtQuick.Controls.Basic
import Drift
import ".."

// Effect stacks the user saved from the effects inspector or the timeline clip menu. Lives beside
// the built-in effect templates because those are multi-effect stacks too — and because a saved
// stack can span video *and* audio effects, which the video-only effects browser has no room for.
Column {
    id: root
    spacing: Theme.spacingMd

    // A QVariantList from an invokable is not reactive, so the section is refreshed by poking this
    // counter from the controller's signal.
    property int presetsTick: 0
    readonly property var presets: {
        void root.presetsTick
        return EditorState.userEffectPresets()
    }

    Connections {
        target: EditorState
        function onUserEffectPresetsChanged() { root.presetsTick++ }
    }

    readonly property string stackFileFilter: qsTr("Drift effect stack (*.drifteffects)")

    function importStack() {
        const url = FileDialogs.openFile(qsTr("Import effect stack"), [root.stackFileFilter])
        if (url.toString().length > 0)
            EditorState.importUserEffectPreset(url)
    }

    function exportStack(preset) {
        const url = FileDialogs.saveFile(qsTr("Export effect stack"), [root.stackFileFilter],
                                         preset.label, "drifteffects")
        if (url.toString().length > 0)
            EditorState.exportUserEffectPreset(preset.id, url)
    }

    function applyStack(preset) {
        if (EditorState.selectedClip < 0)
            return
        EditorState.applyEffectPreset(EditorState.selectedTrack, EditorState.selectedClip, preset.id)
    }

    Item {
        width: parent.width - 24
        x: 12
        height: Math.max(myPresetsLabel.implicitHeight, importButton.height)

        Text {
            id: myPresetsLabel
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("My presets")
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
            tooltip: qsTr("Import an effect stack…")
            onClicked: root.importStack()
        }
    }

    Text {
        width: parent.width - 24
        x: 12
        visible: root.presets.length === 0
        wrapMode: Text.WordWrap
        text: qsTr("Tune a clip's effects, then use “Save as preset…” in the properties Effects tab to keep them here.")
        color: Theme.mutedForeground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    Grid {
        width: parent.width - 24
        x: 12
        visible: root.presets.length > 0
        columns: Math.max(1, Math.floor((width + Theme.assetCardGap)
                                        / (Theme.assetCardWidth + Theme.assetCardGap)))
        columnSpacing: Theme.assetCardGap
        rowSpacing: Theme.assetCardGap

        Repeater {
            model: root.presets
            delegate: Column {
                id: stackCard
                required property var modelData
                width: Theme.assetCardWidth
                spacing: Theme.spacingSm

                scale: cardPress.pressed ? 0.97 : (cardHover.hovered ? 1.02 : 1.0)
                Behavior on scale {
                    NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                }

                Rectangle {
                    width: parent.width
                    height: Math.round(width * 0.55)
                    radius: Theme.radiusSm
                    color: cardHover.hovered ? Theme.panelAccent : Theme.panelBackground
                    border.width: Theme.borderWidth
                    border.color: Theme.panelBorder

                    Behavior on color {
                        ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                    }

                    HoverHandler {
                        id: cardHover
                    }

                    TapHandler {
                        id: cardPress
                        gesturePolicy: TapHandler.ReleaseWithinBounds
                        onTapped: root.applyStack(stackCard.modelData)
                    }

                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: stackMenu.popup()
                    }

                    // There is no rendered preview for a stack, so the card says what is in it.
                    Column {
                        anchors.fill: parent
                        anchors.margins: Theme.spacingSm
                        spacing: 2

                        Repeater {
                            model: Math.min(3, (stackCard.modelData.labels || []).length)
                            delegate: Text {
                                required property int index
                                width: parent.width
                                text: stackCard.modelData.labels[index]
                                elide: Text.ElideRight
                                color: Theme.mutedForeground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeXs
                            }
                        }

                        Text {
                            width: parent.width
                            visible: (stackCard.modelData.labels || []).length > 3
                            text: qsTr("+%1 more").arg(
                                      (stackCard.modelData.labels || []).length - 3)
                            color: Theme.mutedForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeXs
                        }
                    }

                    IconButton {
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.margins: 2
                        visible: cardHover.hovered || stackMenu.visible
                        glyph: Theme.icons.ellipsis
                        variant: "ghost"
                        buttonSize: 20
                        iconSize: 12
                        tooltip: qsTr("Preset options")
                        onClicked: stackMenu.popup()
                    }

                    ThemedContextMenu {
                        id: stackMenu

                        ThemedMenuItem {
                            text: qsTr("Rename…")
                            icon.name: Theme.icons.pencil
                            onTriggered: renameStackDialog.openFor(stackCard.modelData)
                        }
                        ThemedMenuItem {
                            text: qsTr("Export…")
                            icon.name: Theme.icons.folderOutput
                            onTriggered: root.exportStack(stackCard.modelData)
                        }
                        ThemedMenuSeparator { }
                        ThemedMenuItem {
                            text: qsTr("Delete")
                            icon.name: Theme.icons.trash
                            onTriggered: deleteStackDialog.openFor(stackCard.modelData)
                        }
                    }
                }

                Text {
                    width: parent.width
                    text: stackCard.modelData.label
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter
                    color: cardHover.hovered ? Theme.panelForeground : Theme.mutedForeground
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

    NameDialog {
        id: renameStackDialog

        property string presetId: ""

        function openFor(preset) {
            presetId = preset.id
            openWith(qsTr("Rename effect preset"), preset.label)
        }

        onSubmitted: name => EditorState.renameUserEffectPreset(renameStackDialog.presetId, name)
    }

    ThemedDialog {
        id: deleteStackDialog

        property string presetId: ""
        property string presetLabel: ""

        title: qsTr("Delete effect preset")
        acceptText: qsTr("Delete")
        acceptVariant: "destructive"
        preferredWidth: Theme.dialogWidthSm
        acceptOnReturn: false

        function openFor(preset) {
            presetId = preset.id
            presetLabel = preset.label
            open()
        }

        onAccepted: EditorState.deleteUserEffectPreset(deleteStackDialog.presetId)

        contentItem: Text {
            width: parent ? parent.width : 320
            wrapMode: Text.WordWrap
            text: qsTr("Remove “%1” from your saved presets? Clips already using it keep their effects.")
                      .arg(deleteStackDialog.presetLabel)
            color: Theme.panelForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
        }
    }
}
