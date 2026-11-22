import QtQuick
import Drift

// Aspect presets and the thirds toggle as one row of chips. The ratio is w / h; 0 is free.
Row {
    id: presets

    property real aspect: 0
    // What "Original" stands for, as w / h.
    property real originalAspect: 0
    // False leaves just the thirds toggle, for a crop whose ratio something else sets.
    property bool aspectChips: true
    property bool thirds: true
    property real chipHeight: Theme.controlHeightSm
    property string variant: "secondary"

    signal picked(real ratio)
    signal thirdsToggled()

    readonly property var entries: [
        { label: qsTr("Free"), ratio: 0 },
        { label: qsTr("Original"), ratio: -1 },
        { label: "16:9", ratio: 16 / 9 },
        { label: "9:16", ratio: 9 / 16 },
        { label: "1:1", ratio: 1 },
        { label: "4:5", ratio: 4 / 5 },
        { label: "4:3", ratio: 4 / 3 }
    ]

    function ratioOf(entry) {
        return entry.ratio < 0 ? presets.originalAspect : entry.ratio
    }

    // The first entry whose ratio the crop holds, so Original wins over a same-shaped preset.
    readonly property int activeIndex: {
        if (aspect <= 0)
            return 0
        for (let i = 1; i < entries.length; ++i) {
            const r = ratioOf(entries[i])
            if (r > 0 && Math.abs(r - aspect) < 0.002)
                return i
        }
        return -1
    }

    spacing: Theme.spacingSm

    Repeater {
        model: presets.aspectChips ? presets.entries : []

        ThemedChip {
            required property var modelData
            required property int index
            anchors.verticalCenter: parent.verticalCenter
            chipHeight: presets.chipHeight
            variant: presets.variant
            text: modelData.label
            selected: presets.activeIndex === index
            onClicked: presets.picked(presets.ratioOf(modelData))
        }
    }

    ThemedChip {
        anchors.verticalCenter: parent.verticalCenter
        chipHeight: presets.chipHeight
        variant: presets.variant
        text: qsTr("Thirds")
        selected: presets.thirds
        onClicked: presets.thirdsToggled()
    }
}
