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

    PreviewOptionsDecodeDialog {
        id: decodeGpuDialog
        onRestoreSelection: decodeCombo.currentIndex = Math.max(0, decodeCombo.values.indexOf(EditorState.playback.decodeMode))
    }

    SettingsRow {
        title: qsTr("Faster preview (experimental)")
        searchId: "preview-faster-vaapi"
        description: qsTr("Can make playback smoother by keeping video on the graphics card. Turn it off if the picture looks wrong. Takes effect after restart.")
        keywords: "zero copy vaapi gpu hardware"
        available: EditorState.preferences.vaapiZeroCopySupported
        query: page.query

        ThemedSwitch {
            checked: EditorState.preferences.vaapiZeroCopy
            onToggled: EditorState.preferences.vaapiZeroCopy = checked
        }
    }

    SettingsRow {
        title: qsTr("Faster preview (experimental)")
        searchId: "preview-faster-mediacodec"
        description: qsTr("Can make playback smoother by keeping video on the graphics card. Turn it off if the picture looks wrong. Takes effect after restart.")
        keywords: "zero copy mediacodec gpu hardware"
        available: EditorState.preferences.mediaCodecZeroCopySupported
        query: page.query

        ThemedSwitch {
            checked: EditorState.preferences.mediaCodecZeroCopy
            onToggled: EditorState.preferences.mediaCodecZeroCopy = checked
        }
    }

    SettingsRow {
        title: qsTr("Graphics card")
        searchId: "preview-graphics-card"
        description: qsTr("Which graphics card Drift runs on. High performance keeps video decoded on an NVIDIA card on that card; power saving uses less battery. Takes effect after restart.")
        keywords: "gpu integrated discrete nvidia power battery"
        available: EditorState.preferences.gpuPreferenceSupported
        query: page.query

        ThemedComboBox {
            width: Theme.c.settingsRow.controlWidth
            textRole: "label"
            valueRole: "id"
            model: [
                { id: "auto", label: qsTr("Windows default") },
                { id: "integrated", label: qsTr("Power saving (integrated GPU)") },
                { id: "discrete", label: qsTr("High performance (discrete GPU)") }
            ]
            currentIndex: {
                for (var i = 0; i < model.length; ++i) {
                    if (model[i].id === EditorState.preferences.preferredGpu)
                        return i
                }
                return 0
            }
            onActivated: EditorState.preferences.preferredGpu = model[currentIndex].id
        }
    }

    SettingsRow {
        title: qsTr("Graphics card")
        searchId: "preview-graphics-card-system-settings"
        description: qsTr("Choose which graphics card Drift runs on in Windows Settings, under Display > Graphics. Takes effect after restart.")
        keywords: "gpu windows display nvidia"
        available: EditorState.preferences.gpuPreferenceInSystemSettings
        query: page.query

        ThemedButton {
            variant: "secondary"
            text: qsTr("Open graphics settings")
            onClicked: Qt.openUrlExternally("ms-settings:display-advancedgraphics")
        }
    }

    SettingsGroupHeader {
        text: qsTr("Proxies")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Use proxies for preview")
        searchId: "preview-use-proxies"
        description: qsTr("Play clips from their low-resolution proxies where one exists. Export always uses the original media.")
        keywords: "proxy low resolution optimized"
        query: page.query

        ThemedSwitch {
            checked: EditorState.playback.useProxies
            onToggled: EditorState.playback.useProxies = checked
        }
    }

    SettingsRow {
        title: qsTr("Proxy resolution")
        searchId: "preview-proxy-resolution"
        description: qsTr("Size of new proxies. Proxies made at another size are not used until you create them again.")
        keywords: "proxy size 720p 1080p"
        query: page.query

        ThemedComboBox {
            width: Theme.c.settingsRow.controlWidth
            textRole: "label"
            valueRole: "id"
            model: [
                { id: 360, label: qsTr("360p") },
                { id: 540, label: qsTr("540p") },
                { id: 720, label: qsTr("720p") },
                { id: 1080, label: qsTr("1080p") }
            ]
            currentIndex: {
                for (var i = 0; i < model.length; ++i) {
                    if (model[i].id === EditorState.playback.proxySize)
                        return i
                }
                return 2
            }
            onActivated: EditorState.playback.proxySize = model[currentIndex].id
        }
    }

    SettingsRow {
        title: qsTr("Proxy name patterns")
        searchId: "preview-proxy-name-patterns"
        description: qsTr("Linking a proxy teaches Drift its name pattern. Proxies next to your media that match a pattern below link automatically.")
        keywords: "proxy naming file name regex"
        stacked: true
        query: page.query

        Column {
            width: parent.width
            spacing: Theme.spacingSm

            Repeater {
                model: EditorState.preferences.proxyNamePatterns

                Row {
                    id: patternRow
                    required property string modelData
                    width: parent.width
                    spacing: Theme.spacingSm

                    ThemedLabel {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - removePattern.width - parent.spacing
                        text: patternRow.modelData
                        font.family: Theme.monoFontFamily
                        elide: Text.ElideMiddle
                    }
                    ThemedButton {
                        id: removePattern
                        anchors.verticalCenter: parent.verticalCenter
                        variant: "ghost"
                        glyph: Theme.icons.x
                        tooltip: qsTr("Remove pattern")
                        onClicked: EditorState.preferences.proxyNamePatterns =
                            EditorState.preferences.proxyNamePatterns.filter(p => p !== patternRow.modelData)
                    }
                }
            }

            ThemedButton {
                variant: "secondary"
                text: qsTr("Reset to defaults")
                onClicked: EditorState.preferences.resetProxyNamePatterns()
            }
        }
    }
}
