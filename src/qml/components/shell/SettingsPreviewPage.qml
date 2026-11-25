import QtQuick
import Drift

SettingsPage {
    id: page

    pageId: "preview"
    title: qsTr("Preview")

    readonly property var previewQualities: ["full", "half", "quarter", "eighth", "auto"]

    SettingsGroupHeader {
        text: qsTr("Guides")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Show guides")
        searchId: "preview-show-guides"
        description: qsTr("Alignment guides drawn over the preview.")
        keywords: "alignment grid lines"
        query: page.query

        ThemedSwitch {
            checked: EditorState.preview.guidesEnabled
            onToggled: EditorState.preview.guidesEnabled = checked
        }
    }

    SettingsRow {
        title: qsTr("Guide sets")
        searchId: "preview-guide-sets"
        description: qsTr("Choose which guide sets are drawn over the preview.")
        keywords: "alignment grid lines"
        stacked: true
        available: EditorState.preview.guidesEnabled
        query: page.query

        Column {
            width: parent.width
            spacing: Theme.spacingSm

            Repeater {
                model: EditorState.guideSets

                ThemedCheckBox {
                    required property var modelData
                    width: parent.width
                    text: modelData.name
                    checked: modelData.active
                    onToggled: EditorState.setGuideSetActive(modelData.id, checked)
                }
            }
        }
    }

    SettingsGroupHeader {
        text: qsTr("Background")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Background")
        searchId: "preview-background"
        description: qsTr("Fill behind clips that don’t cover the whole screen.")
        keywords: "solid color blur transparent fill"
        query: page.query

        ThemedComboBox {
            width: Theme.c.settingsRow.controlWidth
            textRole: "label"
            valueRole: "id"
            model: [
                { id: "color", label: qsTr("Solid color") },
                { id: "blur", label: qsTr("Blur") },
                { id: "transparent", label: qsTr("Transparent") }
            ]
            currentIndex: {
                for (var i = 0; i < model.length; ++i) {
                    if (model[i].id === EditorState.projectFile.background.kind)
                        return i
                }
                return 0
            }
            onActivated: EditorState.projectFile.setBackground({ kind: model[currentIndex].id })
        }
    }

    SettingsRow {
        title: qsTr("Background colour")
        searchId: "preview-background-colour"
        description: qsTr("Colour of the solid background.")
        keywords: "fill color"
        available: EditorState.projectFile.background.kind === "color"
        query: page.query

        ColorSwatchField {
            hex: EditorState.projectFile.background.color || "#ff000000"
            onEdited: value => EditorState.projectFile.setBackground({ kind: "color", color: value })
        }
    }

    SettingsRow {
        title: qsTr("Blur strength")
        searchId: "preview-blur-strength"
        description: qsTr("How much the background is blurred behind clips.")
        keywords: "fill blur"
        stacked: true
        available: EditorState.projectFile.background.kind === "blur"
        query: page.query

        ThemedSlider {
            width: parent.width
            label: qsTr("Blur strength")
            from: 1
            to: 100
            stepSize: 1
            valueFormatter: function (v) { return Math.round(v) }
            value: EditorState.projectFile.background.blurStrength || 20
            onPressedChanged: {
                if (!pressed)
                    EditorState.projectFile.setBackground({ kind: "blur", blurStrength: value })
            }
        }
    }

    SettingsGroupHeader {
        text: qsTr("Playback quality")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Preview quality")
        searchId: "preview-quality"
        description: qsTr("Lower is smoother while editing. Full matches an export exactly; Auto renders only the pixels on screen.")
        keywords: "resolution eco lower smoother"
        query: page.query

        ThemedComboBox {
            width: Theme.c.settingsRow.controlWidth
            textRole: "label"
            model: [qsTr("Full"), qsTr("Half"), qsTr("Quarter"), qsTr("1/8 (Eco)"), qsTr("Auto")]
            currentIndex: Math.max(0, page.previewQualities.indexOf(EditorState.playback.previewQuality))
            onActivated: EditorState.playback.previewQuality = page.previewQualities[currentIndex]
        }
    }

    SettingsRow {
        title: qsTr("Decoding")
        searchId: "preview-decoding"
        description: qsTr("How video is decoded for preview. Software is smoother for most clips but uses more CPU. Hardware suits high-quality 4K.")
        keywords: "decoder hardware software gpu stutter"
        query: page.query

        Row {
            spacing: Theme.spacingSm

            ThemedComboBox {
                id: decodeCombo
                width: Theme.c.settingsRow.controlWidth
                textRole: "label"
                readonly property var modes: EditorState.playback.decodeModes
                readonly property var values: modes.map(function (m) { return m.id })
                model: modes
                currentIndex: Math.max(0, values.indexOf(EditorState.playback.decodeMode))
                onActivated: {
                    const mode = modes[currentIndex]
                    if (mode && mode.warn)
                        decodeGpuDialog.confirm(mode)
                    else
                        EditorState.playback.decodeMode = mode.id
                }
            }

            PreviewOptionsDecodeWarning {
                anchors.verticalCenter: parent.verticalCenter
                mode: decodeCombo.modes[decodeCombo.currentIndex]
            }
        }
    }

    SettingsGroupHeader {
        text: qsTr("3D view")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Fly look")
        searchId: "preview-fly-look"
        description: qsTr("How the mouse turns the view in fly mode. Mouse look locks the pointer; steering turns toward the pointer and suits touchpads.")
        keywords: "fly mode mouse look pointer lock capture steer touchpad first person camera"
        query: page.query

        ThemedComboBox {
            readonly property var ids: ["mouse", "steer"]
            width: Theme.c.settingsRow.controlWidth
            model: [qsTr("Mouse look (locked pointer)"), qsTr("Steer toward pointer")]
            currentIndex: Math.max(0, ids.indexOf(EditorState.preview.flyLook))
            onActivated: EditorState.preview.flyLook = ids[currentIndex]
        }
    }

    PreviewOptionsDecodeDialog {
        id: decodeGpuDialog
        onRestoreSelection: decodeCombo.currentIndex = Math.max(0, decodeCombo.values.indexOf(EditorState.playback.decodeMode))
    }
}
