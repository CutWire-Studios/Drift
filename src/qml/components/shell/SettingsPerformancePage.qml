import QtQuick
import Drift

SettingsPage {
    id: page

    pageId: "performance"
    title: qsTr("Performance")

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

    SettingsRow {
        title: qsTr("For advanced users")
        searchId: "performance-advanced-users"
        description: qsTr("These tune memory use and playback behaviour. The defaults suit most machines; changing them can make playback stutter or use far more memory. Most changes apply immediately.")
        keywords: "performance advanced tuning power user memory cache"
        query: page.query

        ThemedButton {
            variant: "secondary"
            text: qsTr("Reset to defaults")
            onClicked: EditorState.preferences.resetPerformanceDefaults()
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
        text: qsTr("Playback")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Drop late frames")
        searchId: "performance-drop-late-frames"
        description: qsTr("Skip preview frames that finish this far behind the playhead, instead of showing them late. Can reduce slow-motion at clip cuts on slow machines. Off never drops a frame for being late.")
        keywords: "stale lag late drop skip frames stutter slow motion cut"
        stacked: true
        query: page.query

        TunableSlider {
            width: parent.width
            label: qsTr("Drop frames lagging behind playback by more than")
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
        title: qsTr("Read-ahead")
        searchId: "performance-read-ahead"
        description: qsTr("How much video each clip decodes ahead of the playhead during playback. More absorbs slow-to-decode footage but competes with the frames on screen. Default 2000 ms.")
        keywords: "buffer prefetch decode ahead playback"
        stacked: true
        query: page.query

        TunableSlider {
            width: parent.width
            label: qsTr("Read-ahead")
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
        title: qsTr("Continuous read-ahead")
        searchId: "performance-continuous-read-ahead"
        description: qsTr("Keep decoding until the whole read-ahead window is full. Off decodes one frame ahead per displayed frame, which leaves more decoder time for the clip on screen but buffers less.")
        keywords: "prefetch buffer decode ahead"
        query: page.query

        ThemedSwitch {
            checked: EditorState.preferences.continuousReadAhead
            onToggled: EditorState.preferences.continuousReadAhead = checked
        }
    }

    SettingsGroupHeader {
        text: qsTr("Memory & caches")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Decoded frame cache")
        searchId: "performance-decoded-frame-cache"
        description: qsTr("Memory shared by all clips for decoded preview frames. Automatic uses an eighth of system memory (a sixteenth on Android), at least 128 MiB.")
        keywords: "ram memory preview frames buffer"
        stacked: true
        query: page.query

        TunableSlider {
            width: parent.width
            label: qsTr("Decoded frame cache")
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
        title: qsTr("CPU frame history per clip")
        searchId: "performance-cpu-frame-history"
        description: qsTr("Limit the decoded images each clip keeps in memory for scrubbing and effects. Unlimited keeps up to 16 frames per clip, which is large for 4K footage.")
        keywords: "ram memory history reader image"
        stacked: true
        query: page.query

        TunableSlider {
            width: parent.width
            label: qsTr("CPU frame history per clip")
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
        title: qsTr("Render target pool: count")
        searchId: "performance-render-target-pool-count"
        description: qsTr("Idle GPU render targets kept for reuse. Fewer saves video memory; more avoids reallocating while effects and transitions play.")
        keywords: "gpu vram framebuffer texture pool"
        stacked: true
        query: page.query

        TunableSlider {
            width: parent.width
            label: qsTr("Render target pool: count")
            from: 4
            to: 64
            step: 1
            unit: qsTr("targets")
            current: EditorState.preferences.targetPoolMax
            onPicked: (v) => EditorState.preferences.targetPoolMax = v
        }
    }

    SettingsRow {
        title: qsTr("Render target pool: size")
        searchId: "performance-render-target-pool-size"
        description: qsTr("Cap on the video memory held by idle render targets, in addition to the count.")
        keywords: "gpu vram framebuffer texture pool bytes"
        stacked: true
        query: page.query

        TunableSlider {
            width: parent.width
            label: qsTr("Render target pool: size")
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
        title: qsTr("Image upload cache: count")
        searchId: "performance-image-upload-cache-count"
        description: qsTr("Stills and converted frames kept on the GPU so they are not uploaded again every frame.")
        keywords: "gpu vram texture upload images"
        stacked: true
        query: page.query

        TunableSlider {
            width: parent.width
            label: qsTr("Image upload cache: count")
            from: 8
            to: 96
            step: 1
            unit: qsTr("images")
            current: EditorState.preferences.uploadCacheMax
            onPicked: (v) => EditorState.preferences.uploadCacheMax = v
        }
    }

    SettingsRow {
        title: qsTr("Image upload cache: size")
        searchId: "performance-image-upload-cache-size"
        description: qsTr("Cap on the video memory used by the image upload cache, in addition to the count. 48 uploads of 4K frames take about 1.5 GiB.")
        keywords: "gpu vram texture upload images bytes"
        stacked: true
        query: page.query

        TunableSlider {
            width: parent.width
            label: qsTr("Image upload cache: size")
            from: 0
            to: 4096
            step: 64
            unit: qsTr("MiB")
            zeroText: qsTr("Unlimited")
            current: EditorState.preferences.uploadCacheMiB
            onPicked: (v) => EditorState.preferences.uploadCacheMiB = v
        }
    }

    SettingsGroupHeader {
        text: qsTr("Export")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Prefer hardware encoder for new exports")
        searchId: "performance-hardware-encoder-exports"
        description: qsTr("Start new exports on a GPU H.264 encoder when one is available. Faster, but quality per bitrate is usually lower than the default software encoder.")
        keywords: "nvenc qsv amf vaapi videotoolbox mediacodec gpu encode h264 x264"
        query: page.query

        ThemedSwitch {
            checked: EditorState.preferences.preferHardwareExport
            onToggled: EditorState.preferences.preferHardwareExport = checked
        }
    }
}
