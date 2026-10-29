import QtQuick
import Drift
import "SourceLabTime.js" as Time

SourceLabSection {
    id: root

    readonly property var lab: EditorState.sourceLab
    readonly property var recipe: root.lab.recipe
    readonly property real speed: root.recipe.speed ?? 1
    readonly property real keptSeconds: {
        const out = root.recipe.trimOut ?? -1
        return (out < 0 ? root.lab.durationSeconds : out) - (root.recipe.trimIn ?? 0)
    }

    title: qsTr("Speed")
    dirty: root.speed !== 1 || !(root.recipe.keepPitch ?? true)
    onResetRequested: root.lab.resetRecipe("speed")

    SourceLabSliderRow {
        label: qsTr("Speed")
        SearchAnchor {
            searchId: "sourcelab-speed"
            title: qsTr("Speed and loudness")
            keywords: "speed slow fast motion gain volume loudness normalize"
        }
        value: root.speed
        from: 0.25
        to: 4
        decimals: 2
        step: 0.05
        unit: "x"
        logScale: true
        detents: [0.5, 1, 2]
        onMoved: v => root.lab.setRecipeValue("speed", v)
    }

    ThemedCheckBox {
        text: qsTr("Keep pitch")
        visible: root.lab.hasAudio
        checked: root.recipe.keepPitch ?? true
        onToggled: root.lab.setRecipeValue("keepPitch", checked)
    }

    ThemedLabel {
        width: parent.width
        text: qsTr("Result: %1").arg(Time.formatClock(root.keptSeconds / root.speed, 1))
    }
}
