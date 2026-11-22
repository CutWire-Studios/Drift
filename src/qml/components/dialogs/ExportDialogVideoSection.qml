import QtQuick
import Drift
import QtQuick.Layouts

// ---- Video mode ----
Column {
    id: section
    required property var dialog
    property alias frameRateCombo: frameRateCombo
    property alias videoCodecCombo: videoCodecCombo
    property alias audioCodecCombo: audioCodecCombo
    property alias audioBitrateCombo: audioBitrateCombo
    property alias presetCombo: presetCombo
    readonly property bool phone: Theme.touchInput && Theme.compact

    width: parent.width
    spacing: Theme.spacingXl
    visible: dialog.exportMode === "video"
    height: visible ? implicitHeight : 0
    clip: true

    ThemedLabel {
        width: parent.width
        size: "sm"
        visible: !section.phone
        text: qsTr("Saves what you see in the preview. Pick a size — the picture shape stays the same.")
    }

    Column {
        width: parent.width
        spacing: Theme.spacingSm

        ThemedLabel {
            text: qsTr("Format")
        }

        ThemedLabel {
            width: parent.width
            size: "sm"
            wrapMode: Text.WordWrap
            text: {
                void EditorState.tracksRevision
                return (section.phone ? qsTr("%1×%2") : qsTr("%1×%2. Vertical for TikTok, Reels and Shorts, wide for YouTube."))
                    .arg(EditorState.projectFile.projectWidth()).arg(EditorState.projectFile.projectHeight())
            }
        }

        Row {
            spacing: Theme.spacingMd

            ThemedButton {
                text: qsTr("Choose layout…")
                tooltip: qsTr("Pick a platform template (YouTube, Instagram, TikTok, …) and quality")
                onClicked: {
                    dialog.close()
                    ShellBridge.main.openLayoutChooser()
                }
            }

            ThemedButton {
                readonly property var timeline: ShellBridge.main ? ShellBridge.main.timelinePanel : null
                visible: !!timeline
                enabled: EditorState.selectedTrack >= 0 && EditorState.selectedClip >= 0
                text: qsTr("Auto reframe…")
                tooltip: enabled ? qsTr("Crop the selected clip to follow its subject in a new shape")
                                 : qsTr("Select a video clip on the timeline first")
                onClicked: {
                    dialog.close()
                    timeline.requestAutoReframe(EditorState.selectedTrack, EditorState.selectedClip)
                }
            }
        }
    }

    ThemedLabel {
        text: qsTr("Downscale")
    }

    Flow {
        width: parent.width
        spacing: Theme.spacingMd

        Repeater {
            model: dialog.scaleOptions

            delegate: ThemedChip {
                required property var modelData
                width: section.phone ? Math.floor((parent.width - Theme.spacingMd) / 2) : implicitWidth
                text: (Theme.touchInput && Theme.compact) ? modelData.label.replace(" · ", "\n") : modelData.label
                selected: dialog.scaleId === modelData.id
                tooltip: qsTr("Export at %1×%2").arg(modelData.width).arg(modelData.height)
                onClicked: {
                    dialog.scaleId = modelData.id
                    dialog.targetHeight = modelData.targetHeight
                    if (modelData.videoBitrateKbps)
                        dialog.videoBitrateKbps = modelData.videoBitrateKbps
                }
            }
        }
    }

    Column {
        width: parent.width
        spacing: Theme.spacingSm

        ThemedLabel {
            text: qsTr("Frame rate")
        }

        RowLayout {
            width: parent.width
            spacing: Theme.spacingMd

            ThemedComboBox {
                id: frameRateCombo
                Layout.fillWidth: true
                textRole: "label"
                valueRole: "id"
                model: dialog.frameRateModel
                onActivated: function (index) {
                    dialog.applyFrameRateChoice(dialog.frameRateModel[index])
                }
            }

            ThemedNumberField {
                Layout.preferredWidth: 120
                visible: dialog.frameRateCustom
                from: 1
                to: 480
                step: 1
                unit: qsTr("fps")
                value: dialog.fpsNum > 0 ? dialog.fpsNum : EditorState.projectFile.projectFps()
                // Whole numbers only here; every fractional rate worth
                // delivering (23.976 / 29.97 / 59.94) is already a preset.
                onEdited: function (v) {
                    dialog.fpsNum = Math.round(v)
                    dialog.fpsDen = 1
                }
            }
        }

        ThemedLabel {
            width: parent.width
            size: "sm"
            visible: dialog.fpsNum > 0
            text: section.phone ? qsTr("Above the project rate, extra frames come from the source footage, which keeps slowed clips smooth.")
                                : qsTr("Exporting above the project rate pulls extra frames from the source footage where it has them — this is what makes slowed clips look smooth. Where it doesn't, frames repeat.")
        }
    }

    ThemedLabel {
        width: parent.width
        size: "sm"
        visible: !!(dialog.currentVideoCodec && dialog.currentVideoCodec.hardware)
        text: qsTr("Hardware-accelerated export: %1").arg(dialog.currentVideoCodec.label || "")
    }

    Item {
        width: parent.width
        height: section.phone ? Theme.androidRowHeight : advancedHeader.implicitHeight

        Row {
            id: advancedHeader
            spacing: Theme.spacingSm
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter

            IconGlyph {
                anchors.verticalCenter: parent.verticalCenter
                glyph: Theme.icons.chevronRight
                iconSize: Theme.iconSizeSm
                iconColor: Theme.c.exportDialogVideoSection.icon
                rotation: dialog.advancedOpen ? 90 : 0
                Behavior on rotation {
                    NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                }
            }

            ThemedLabel {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Advanced")
            }
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: dialog.advancedOpen = !dialog.advancedOpen
        }

        Accessible.role: Accessible.Button
        Accessible.name: qsTr("Advanced")
        Accessible.checkable: true
        Accessible.checked: dialog.advancedOpen
        Accessible.onPressAction: dialog.advancedOpen = !dialog.advancedOpen
    }

    Column {
        width: parent.width
        spacing: Theme.spacingXl
        visible: dialog.advancedOpen
        height: visible ? implicitHeight : 0
        clip: true

        Column {
            width: parent.width
            spacing: Theme.spacingSm

            ThemedLabel {
                text: qsTr("Video encoder")
                size: "sm"
            }

            ThemedComboBox {
                id: videoCodecCombo
                width: parent.width
                textRole: "label"
                valueRole: "id"
                model: dialog.videoCodecs
                onActivated: function (index) {
                    var item = dialog.videoCodecs[index]
                    if (!item || !item.available) {
                        currentIndex = Math.max(0, indexOfValue(dialog.videoCodecId))
                        return
                    }
                    dialog.videoCodecId = item.id
                    dialog.applyVideoCodecDefaults(item)
                    dialog.refreshCodecMeta()
                    dialog.syncComboIndices()
                }
            }

            ThemedLabel {
                width: parent.width
                visible: dialog.videoHasAlpha
                wrapMode: Text.WordWrap
                text: qsTr("Keeps a transparent canvas. Set the project background to Transparent so holes stay empty.")
            }
        }

        Column {
            width: parent.width
            spacing: Theme.spacingMd
            visible: !dialog.videoLossless

            Row {
                id: rateRow
                width: parent.width
                spacing: Theme.spacingMd
                readonly property real halfWidth: section.phone ? Math.floor((width - spacing) / 2) : -1

                ThemedToggleButton {
                    width: rateRow.halfWidth > 0 ? rateRow.halfWidth : implicitWidth
                    text: section.phone ? qsTr("Quality") : qsTr("Constant Quality")
                    checked: dialog.rateControl === "crf"
                    enabled: dialog.videoSupportsCrf
                    onClicked: {
                        if (dialog.videoSupportsCrf)
                            dialog.rateControl = "crf"
                    }
                }

                ThemedToggleButton {
                    width: rateRow.halfWidth > 0 ? rateRow.halfWidth : implicitWidth
                    text: qsTr("Bitrate")
                    checked: dialog.rateControl === "bitrate"
                    enabled: dialog.videoSupportsBitrate
                    onClicked: {
                        if (dialog.videoSupportsBitrate)
                            dialog.rateControl = "bitrate"
                    }
                }
            }

            Column {
                width: parent.width
                spacing: Theme.spacingSm
                visible: dialog.rateControl === "crf" && dialog.videoSupportsCrf

                Row {
                    width: parent.width
                    ThemedLabel {
                        text: qsTr("RF %1").arg(dialog.crf)
                        size: "sm"
                    }
                }

                ThemedSlider {
                    width: parent.width
                    label: qsTr("Quality (RF)")
                    // H.264 High profile rejects lossless RF 0; RF 1 is its
                    // highest compatible quality setting.
                    from: dialog.videoCodecId === "h264" ? 1 : 0
                    to: 51
                    stepSize: 1
                    value: dialog.crf
                    valueFormatter: function (v) { return qsTr("RF %1").arg(Math.round(v)) }
                    onMoved: dialog.crf = Math.round(value)
                }

                RowLayout {
                    width: parent.width

                    ThemedLabel {
                        text: qsTr("Higher quality")
                        size: "xs"
                    }

                    Item { Layout.fillWidth: true }

                    ThemedLabel {
                        text: qsTr("Lower quality")
                        size: "xs"
                    }
                }
            }

            RowLayout {
                width: parent.width
                spacing: Theme.spacingMd
                visible: dialog.rateControl === "bitrate" && dialog.videoSupportsBitrate

                ThemedLabel {
                    text: qsTr("Bitrate (kbps)")
                    size: "sm"
                }

                ThemedNumberField {
                    Layout.preferredWidth: 120
                    from: 100
                    to: 100000
                    step: 100
                    unit: qsTr("kbps")
                    value: dialog.videoBitrateKbps
                    onEdited: function (v) { dialog.videoBitrateKbps = Math.round(v) }
                }
            }
        }

        Column {
            width: parent.width
            spacing: Theme.spacingSm
            visible: dialog.videoSupportsPreset && dialog.videoPresetList.length > 0

            ThemedLabel {
                text: qsTr("Preset")
                size: "sm"
            }

            ThemedComboBox {
                id: presetCombo
                width: parent.width
                model: dialog.videoPresetList
                onActivated: function (index) {
                    if (index >= 0 && index < dialog.videoPresetList.length)
                        dialog.videoPreset = dialog.videoPresetList[index]
                }
            }
        }

        GridLayout {
            width: parent.width
            columns: 2
            columnSpacing: Theme.spacingLg
            rowSpacing: Theme.spacingSm

            ThemedLabel {
                text: qsTr("Audio encoder")
                size: "sm"
                Layout.fillWidth: true
            }

            ThemedLabel {
                text: qsTr("Bitrate")
                size: "sm"
                Layout.fillWidth: true
                visible: !dialog.audioLossless
            }

            ThemedComboBox {
                id: audioCodecCombo
                Layout.fillWidth: true
                textRole: "label"
                valueRole: "id"
                model: dialog.audioCodecs
                onActivated: function (index) {
                    var item = dialog.audioCodecs[index]
                    if (!item || !item.available) {
                        currentIndex = Math.max(0, indexOfValue(dialog.audioCodecId))
                        return
                    }
                    dialog.audioCodecId = item.id
                    dialog.refreshCodecMeta()
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm
                visible: !dialog.audioLossless

                ThemedComboBox {
                    id: audioBitrateCombo
                    Layout.fillWidth: true
                    textRole: "label"
                    valueRole: "value"
                    model: dialog.audioBitrateModel
                    onActivated: function (index) {
                        var item = dialog.audioBitrateModel[index]
                        if (!item)
                            return
                        dialog.applyAudioBitrateChoice(item.value)
                        if (audioOnlyBitrateCombo)
                            audioOnlyBitrateCombo.currentIndex = currentIndex
                    }
                }

                ThemedNumberField {
                    Layout.fillWidth: true
                    visible: dialog.audioBitrateChoice === "custom"
                    from: 32
                    to: 512
                    step: 16
                    unit: qsTr("kbps")
                    value: dialog.audioBitrateKbps
                    onEdited: function (v) { dialog.audioBitrateKbps = Math.round(v) }
                }
            }
        }
    }
    }
