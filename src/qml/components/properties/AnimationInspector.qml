import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
import ".."

// CapCut-style Animation: In / Out only.
// Fade is one kind among slide/zoom/pop — same duration + style (Linear/Smooth/Natural/Custom).
Item {
    id: root

    property int clipDataRevision: 0
    readonly property var clipData: {
        void clipDataRevision
        return EditorState.selectedClipData
    }
    readonly property bool hasSelection: !!clipData && Object.keys(clipData).length > 0
    readonly property string clipKind: hasSelection ? (clipData.kind || "") : ""
    readonly property bool supportsBodyAnim: clipKind === "video" || clipKind === "image"
                                             || clipKind === "shape" || clipKind === "text"
    readonly property bool supportsFade: supportsBodyAnim || clipKind === "audio"

    readonly property var animKinds: [
        "none", "fade", "slideUp", "slideDown", "slideLeft", "slideRight",
        "zoomIn", "zoomOut", "pop", "spinCW", "spinCCW", "bounce"
    ]
    readonly property var animKindLabels: [
        qsTr("None"), qsTr("Fade"), qsTr("Slide up"), qsTr("Slide down"),
        qsTr("Slide left"), qsTr("Slide right"), qsTr("Zoom in"), qsTr("Zoom out"),
        qsTr("Pop"), qsTr("Spin CW"), qsTr("Spin CCW"), qsTr("Bounce")
    ]
    readonly property var styleIds: ["linear", "smooth", "equalPower", "custom"]
    readonly property var styleLabels: [qsTr("Linear"), qsTr("Smooth"), qsTr("Natural"), qsTr("Custom")]

    readonly property var animIn: {
        void clipDataRevision
        return (clipData && clipData.animIn)
               ? clipData.animIn
               : { kind: "none", duration: 0.5, curve: "smooth" }
    }
    readonly property var animOut: {
        void clipDataRevision
        return (clipData && clipData.animOut)
               ? clipData.animOut
               : { kind: "none", duration: 0.5, curve: "smooth" }
    }

    height: column.height
    implicitHeight: column.height

    function setAnim(which, patch) {
        EditorState.setClipAnimation(EditorState.selectedTrack, EditorState.selectedClip, which, patch)
    }

    function applyBoth(kind) {
        const curve = kind === "none" ? "smooth" : (root.animIn.curve || "smooth")
        const duration = 0.5
        root.setAnim("animIn", { kind: kind, duration: duration, curve: curve })
        root.setAnim("animOut", { kind: kind, duration: duration, curve: curve })
    }

    function styleIndexFor(anim) {
        const id = (anim && anim.curve) ? anim.curve : (root.clipData.fadeCurve || "smooth")
        return Math.max(0, root.styleIds.indexOf(id))
    }

    function setStyle(which, index) {
        const id = root.styleIds[index]
        if (id === "custom") {
            root.setAnim(which, { curve: "custom" })
            root.Window.window.openFadeCurve(EditorState.selectedTrack, EditorState.selectedClip)
            return
        }
        root.setAnim(which, { curve: id })
    }

    function refreshFields() {
        if (root.supportsBodyAnim) {
            if (animInDurationField && !animInDurationField.activeFocus)
                animInDurationField.value = root.animIn.duration || 0.5
            if (animOutDurationField && !animOutDurationField.activeFocus)
                animOutDurationField.value = root.animOut.duration || 0.5
        }
        if (root.clipKind === "audio") {
            if (audioFadeInField && !audioFadeInField.activeFocus)
                audioFadeInField.value = root.clipData.fadeIn || 0
            if (audioFadeOutField && !audioFadeOutField.activeFocus)
                audioFadeOutField.value = root.clipData.fadeOut || 0
        }
    }

    Connections {
        target: EditorState
        function onSelectionChanged() { root.clipDataRevision++; root.refreshFields() }
        function onSelectedClipDataChanged() { root.clipDataRevision++; root.refreshFields() }
        function onTracksChanged() { root.clipDataRevision++; root.refreshFields() }
    }

    Component.onCompleted: refreshFields()

    Column {
        id: column
        width: root.width
        spacing: Theme.spacingXl

        EmptyState {
            visible: !root.supportsFade
            width: parent.width
            compact: true
            glyph: Theme.icons.sparkles
            title: qsTr("Not available")
            hint: qsTr("Animation applies to video, image, shape, text, and audio.")
        }

        // ----- Audio: volume fades only ----------------------------------------------------
        Text {
            visible: root.clipKind === "audio"
            text: qsTr("Fade in / out (volume)")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        Row {
            visible: root.clipKind === "audio"
            width: parent.width
            spacing: 6

            ThemedNumberField {
                id: audioFadeInField
                to: Math.max(0.1, root.clipData.duration || 1)
                unit: "s"
                width: (parent.width - parent.spacing) / 2
                decimals: 2
                step: 0.05
                from: 0
                onEdited: v => EditorState.setClipFade(
                               EditorState.selectedTrack, EditorState.selectedClip,
                               v, root.clipData.fadeOut || 0)
            }
            ThemedNumberField {
                id: audioFadeOutField
                to: Math.max(0.1, root.clipData.duration || 1)
                unit: "s"
                width: (parent.width - parent.spacing) / 2
                decimals: 2
                step: 0.05
                from: 0
                onEdited: v => EditorState.setClipFade(
                               EditorState.selectedTrack, EditorState.selectedClip,
                               root.clipData.fadeIn || 0, v)
            }
        }

        Text {
            visible: root.clipKind === "audio"
            text: qsTr("Style")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        ThemedComboBox {
            visible: root.clipKind === "audio"
            width: parent.width
            model: root.styleLabels
            currentIndex: Math.max(0, root.styleIds.indexOf(root.clipData.fadeCurve || "smooth"))
            onActivated: (index) => {
                const id = root.styleIds[index]
                if (id === "custom") {
                    root.Window.window.openFadeCurve(
                                EditorState.selectedTrack, EditorState.selectedClip)
                    return
                }
                EditorState.setClipFadeCurve(
                            EditorState.selectedTrack, EditorState.selectedClip, id)
            }
        }

        // ----- Visual: CapCut In / Out -----------------------------------------------------
        Text {
            visible: root.supportsBodyAnim
            width: parent.width
            wrapMode: Text.WordWrap
            text: qsTr("Pick how the clip enters and leaves. Fade is one option — same style controls as slide or zoom.")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        Flow {
            visible: root.supportsBodyAnim
            width: parent.width
            spacing: 6

            Repeater {
                model: [
                    { label: qsTr("Fade"), kind: "fade" },
                    { label: qsTr("Slide up"), kind: "slideUp" },
                    { label: qsTr("Pop"), kind: "pop" },
                    { label: qsTr("Zoom in"), kind: "zoomIn" },
                    { label: qsTr("Bounce"), kind: "bounce" },
                    { label: qsTr("Clear"), kind: "none" }
                ]
                delegate: ThemedChip {
                    required property var modelData
                    text: modelData.label
                    selected: modelData.kind !== "none"
                              && root.animIn.kind === modelData.kind
                              && root.animOut.kind === modelData.kind
                    onClicked: root.applyBoth(modelData.kind)
                }
            }
        }

        Text {
            visible: root.supportsBodyAnim
            text: qsTr("In")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        Row {
            visible: root.supportsBodyAnim
            width: parent.width
            spacing: 6

            ThemedComboBox {
                width: Math.max(40, (parent.width - parent.spacing * 2 - 56) / 2)
                model: root.animKindLabels
                currentIndex: Math.max(0, root.animKinds.indexOf(root.animIn.kind || "none"))
                onActivated: root.setAnim("animIn", { kind: root.animKinds[currentIndex] })
            }
            ThemedComboBox {
                width: Math.max(40, (parent.width - parent.spacing * 2 - 56) / 2)
                model: root.styleLabels
                currentIndex: root.styleIndexFor(root.animIn)
                onActivated: (index) => root.setStyle("animIn", index)
            }
            ThemedNumberField {
                id: animInDurationField
                to: 10
                unit: "s"
                width: 56
                decimals: 2
                step: 0.05
                from: 0
                onEdited: v => root.setAnim("animIn", { duration: v })
            }
        }

        Text {
            visible: root.supportsBodyAnim
            text: qsTr("Out")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        Row {
            visible: root.supportsBodyAnim
            width: parent.width
            spacing: 6

            ThemedComboBox {
                width: Math.max(40, (parent.width - parent.spacing * 2 - 56) / 2)
                model: root.animKindLabels
                currentIndex: Math.max(0, root.animKinds.indexOf(root.animOut.kind || "none"))
                onActivated: root.setAnim("animOut", { kind: root.animKinds[currentIndex] })
            }
            ThemedComboBox {
                width: Math.max(40, (parent.width - parent.spacing * 2 - 56) / 2)
                model: root.styleLabels
                currentIndex: root.styleIndexFor(root.animOut)
                onActivated: (index) => root.setStyle("animOut", index)
            }
            ThemedNumberField {
                id: animOutDurationField
                to: 10
                unit: "s"
                width: 56
                decimals: 2
                step: 0.05
                from: 0
                onEdited: v => root.setAnim("animOut", { duration: v })
            }
        }

        ThemedButton {
            visible: root.supportsBodyAnim
                     && ((root.animIn.curve || "") === "custom"
                         || (root.animOut.curve || "") === "custom"
                         || (root.clipData.fadeCurve || "") === "custom")
            width: parent.width
            text: qsTr("Edit custom curve…")
            variant: "secondary"
            onClicked: root.Window.window.openFadeCurve(
                           EditorState.selectedTrack, EditorState.selectedClip)
        }

        Text {
            visible: root.supportsBodyAnim && root.clipKind === "text"
            width: parent.width
            wrapMode: Text.WordWrap
            text: qsTr("Text tab has letter/word animations (typewriter, stagger). This moves the whole clip.")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
    }
}
