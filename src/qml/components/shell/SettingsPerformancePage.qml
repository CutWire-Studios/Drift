import QtQuick
import Drift

SettingsPage {
    id: page

    pageId: "performance"
    title: qsTr("Performance")

    property bool showAdvanced: false
    readonly property bool advancedShown: showAdvanced || query.length > 0

    // A slider with its current value spelled out beside it. zeroText replaces the readout at 0
    // for settings where 0 means off, unlimited or automatic.
    component TunableSlider: Row {
        id: tunable

        property string label: ""
        property int from: 0
        property int to: 100
        property int step: 1
        property int current: 0
        property string unit: ""
        property string zeroText: ""
        signal picked(int value)

        spacing: Theme.spacingMd

        ThemedSlider {
            id: slider
            width: tunable.width - readout.width - tunable.spacing
            anchors.verticalCenter: parent.verticalCenter
            label: tunable.label
            from: tunable.from
            to: tunable.to
            stepSize: tunable.step
            showValueTooltip: false
            onMoved: tunable.picked(Math.round(value))
        }

        // A plain value binding is dropped once the user drags the slider.
        Binding {
            target: slider
            property: "value"
            value: tunable.current
        }

        Text {
            id: readout
            width: Theme.spacing3xl * 3
            anchors.verticalCenter: parent.verticalCenter
            horizontalAlignment: Text.AlignRight
            text: Math.round(slider.value) === 0 && tunable.zeroText.length > 0
                ? tunable.zeroText
                : Math.round(slider.value) + " " + tunable.unit
            color: Theme.c.settingsRow.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
        }
    }

    SettingsGroupHeader {
        text: qsTr("Interface")
        query: page.query
        available: EditorState.preferences.uiGraphicsApis.length > 1
    }

    SettingsRow {
        title: qsTr("Interface graphics API")
        searchId: "performance-graphics-api"
        description: {
            const text = qsTr("What draws panels, the timeline and menus. OpenGL hands the preview over without a copy. The others can make the interface smoother but copy every preview frame through the CPU, so playback may get slower. If Drift does not start with your choice, it goes back to OpenGL. Takes effect after restart.")
            return EditorState.preferences.uiGraphicsApiRevertedFrom.length > 0
                ? text + " " + qsTr("The last choice did not start, so OpenGL is in use.")
                : text
        }
        keywords: "renderer rhi opengl direct3d d3d11 d3d12 vulkan metal gpu backend slow ui"
        available: EditorState.preferences.uiGraphicsApis.length > 1
        query: page.query

        ThemedComboBox {
            width: Theme.c.settingsRow.controlWidth
            textRole: "label"
            valueRole: "id"
            model: {
                const labels = {
                    opengl: qsTr("OpenGL (recommended)"),
                    d3d11: qsTr("Direct3D 11 (Windows default)"),
                    d3d12: "Direct3D 12",
                    vulkan: "Vulkan",
                    metal: qsTr("Metal (macOS default)")
                }
                return EditorState.preferences.uiGraphicsApis.map(function (id) {
                    return { id: id, label: labels[id] || id }
                })
            }
            currentIndex: Math.max(0, EditorState.preferences.uiGraphicsApis.indexOf(EditorState.preferences.uiGraphicsApi))
            onActivated: EditorState.preferences.uiGraphicsApi = model[currentIndex].id
        }
    }

    SettingsGroupHeader {
        text: qsTr("Graphics")
        query: page.query
        available: EditorState.preferences.vaapiZeroCopySupported || EditorState.preferences.mediaCodecZeroCopySupported
                   || EditorState.preferences.gpuPreferenceSupported || EditorState.preferences.gpuPreferenceInSystemSettings
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
        text: qsTr("Advanced tuning")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Advanced tuning")
        searchId: "performance-advanced-users"
        description: qsTr("Memory use and playback behaviour. The defaults suit most machines; changing them can make playback stutter or use far more memory. Most changes apply immediately.")
        keywords: "performance advanced tuning power user memory cache buffer"
        query: page.query

        Row {
            spacing: Theme.spacingMd

            ThemedButton {
                variant: "secondary"
                text: qsTr("Reset to defaults")
                visible: page.showAdvanced
                onClicked: EditorState.preferences.resetPerformanceDefaults()
            }

            ThemedButton {
                variant: "secondary"
                text: page.showAdvanced ? qsTr("Hide") : qsTr("Show")
                onClicked: page.showAdvanced = !page.showAdvanced
            }
        }
    }

    SettingsGroupHeader {
        text: qsTr("Playback")
        available: page.showAdvanced
        query: page.query
    }

    SettingsRow {
        title: qsTr("Skip frames when playback falls behind")
        searchId: "performance-drop-late-frames"
        description: qsTr("Skip preview frames that finish this far behind the playhead, instead of showing them late. Can reduce slow-motion at clip cuts on slow machines. Off never drops a frame for being late.")
        keywords: "stale lag late drop skip frames stutter slow motion cut"
        stacked: true
        available: page.advancedShown
        query: page.query

        TunableSlider {
            width: parent.width
            label: qsTr("Skip frames that are late by more than")
            from: 0
            to: 1000
            step: 50
            unit: qsTr("ms")
            zeroText: qsTr("Off")
            current: EditorState.preferences.staleFrameLagMs
            onPicked: (v) => EditorState.preferences.staleFrameLagMs = v
        }
    }

    SettingsRow {
        title: qsTr("Playback buffer")
        searchId: "performance-read-ahead"
        description: qsTr("How much video each clip decodes ahead of the playhead during playback. More absorbs slow-to-decode footage but competes with the frames on screen. Default 2000 ms.")
        keywords: "buffer prefetch decode ahead playback"
        stacked: true
        available: page.advancedShown
        query: page.query

        TunableSlider {
            width: parent.width
            label: qsTr("Playback buffer")
            from: 0
            to: 5000
            step: 100
            unit: qsTr("ms")
            zeroText: qsTr("Off")
            current: EditorState.preferences.readAheadMs
            onPicked: (v) => EditorState.preferences.readAheadMs = v
        }
    }

    SettingsRow {
        title: qsTr("Keep the playback buffer full")
        searchId: "performance-continuous-read-ahead"
        description: qsTr("Keep decoding until the whole read-ahead window is full. Off decodes one frame ahead per displayed frame, which leaves more decoder time for the clip on screen but buffers less.")
        keywords: "prefetch buffer decode ahead"
        available: page.advancedShown
        query: page.query

        ThemedSwitch {
            checked: EditorState.preferences.continuousReadAhead
            onToggled: EditorState.preferences.continuousReadAhead = checked
        }
    }

    SettingsGroupHeader {
        text: qsTr("Memory & caches")
        available: page.showAdvanced
        query: page.query
    }

    SettingsRow {
        title: qsTr("Preview memory")
        searchId: "performance-decoded-frame-cache"
        description: qsTr("Memory shared by all clips for decoded preview frames. Automatic uses an eighth of system memory (a sixteenth on Android), at least 128 MiB.")
        keywords: "ram memory preview frames buffer"
        stacked: true
        available: page.advancedShown
        query: page.query

        TunableSlider {
            width: parent.width
            label: qsTr("Preview memory")
            from: 0
            to: 8192
            step: 128
            unit: qsTr("MiB")
            zeroText: qsTr("Automatic")
            current: EditorState.preferences.frameCacheMiB
            onPicked: (v) => EditorState.preferences.frameCacheMiB = v
        }
    }

    SettingsRow {
        title: qsTr("Scrubbing memory per clip")
        searchId: "performance-cpu-frame-history"
        description: qsTr("Limit the decoded images each clip keeps in memory for scrubbing and effects. Unlimited keeps up to 16 frames per clip, which is large for 4K footage.")
        keywords: "ram memory history reader image"
        stacked: true
        available: page.advancedShown
        query: page.query

        TunableSlider {
            width: parent.width
            label: qsTr("Scrubbing memory per clip")
            from: 0
            to: 1024
            step: 32
            unit: qsTr("MiB")
            zeroText: qsTr("Unlimited")
            current: EditorState.preferences.readerHistoryMiB
            onPicked: (v) => EditorState.preferences.readerHistoryMiB = v
        }
    }

    SettingsRow {
        title: qsTr("Effects workspace: buffer count")
        searchId: "performance-render-target-pool-count"
        description: qsTr("Idle GPU render targets kept for reuse. Fewer saves video memory; more avoids reallocating while effects and transitions play.")
        keywords: "gpu vram framebuffer texture pool"
        stacked: true
        available: page.advancedShown
        query: page.query

        TunableSlider {
            width: parent.width
            label: qsTr("Effects workspace: buffer count")
            from: 4
            to: 64
            step: 1
            unit: qsTr("targets")
            current: EditorState.preferences.targetPoolMax
            onPicked: (v) => EditorState.preferences.targetPoolMax = v
        }
    }

    SettingsRow {
        title: qsTr("Effects workspace: video memory limit")
        searchId: "performance-render-target-pool-size"
        description: qsTr("Cap on the video memory held by idle render targets, in addition to the count.")
        keywords: "gpu vram framebuffer texture pool bytes"
        stacked: true
        available: page.advancedShown
        query: page.query

        TunableSlider {
            width: parent.width
            label: qsTr("Effects workspace: video memory limit")
            from: 0
            to: 4096
            step: 64
            unit: qsTr("MiB")
            zeroText: qsTr("Unlimited")
            current: EditorState.preferences.targetPoolMiB
            onPicked: (v) => EditorState.preferences.targetPoolMiB = v
        }
    }

    SettingsRow {
        title: qsTr("Graphics card image cache: image count")
        searchId: "performance-image-upload-cache-count"
        description: qsTr("Stills and converted frames kept on the GPU so they are not uploaded again every frame.")
        keywords: "gpu vram texture upload images"
        stacked: true
        available: page.advancedShown
        query: page.query

        TunableSlider {
            width: parent.width
            label: qsTr("Graphics card image cache: image count")
            from: 8
            to: 96
            step: 1
            unit: qsTr("images")
            current: EditorState.preferences.uploadCacheMax
            onPicked: (v) => EditorState.preferences.uploadCacheMax = v
        }
    }

    SettingsRow {
        title: qsTr("Graphics card image cache: video memory limit")
        searchId: "performance-image-upload-cache-size"
        description: qsTr("Cap on the video memory used by the image upload cache, in addition to the count. 48 uploads of 4K frames take about 1.5 GiB.")
        keywords: "gpu vram texture upload images bytes"
        stacked: true
        available: page.advancedShown
        query: page.query

        TunableSlider {
            width: parent.width
            label: qsTr("Graphics card image cache: video memory limit")
            from: 0
            to: 4096
            step: 64
            unit: qsTr("MiB")
            zeroText: qsTr("Unlimited")
            current: EditorState.preferences.uploadCacheMiB
            onPicked: (v) => EditorState.preferences.uploadCacheMiB = v
        }
    }
}
