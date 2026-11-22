import QtQuick
import QtQuick.Controls.Basic
import Drift
// Lottie / SVG clip inspector: what the document is, how it plays, and the slots it exposes.
Item {
    id: root

    property int clipDataRevision: 0
    readonly property real labelSize: Theme.touchInput && Theme.compact ? Theme.fontSizeSm : Theme.fontSizeXs
    readonly property var clipData: {
        void clipDataRevision
        return EditorState.selectedClipData
    }
    readonly property bool hasSelection: !!clipData && Object.keys(clipData).length > 0
    readonly property bool hasVector: hasSelection && clipData.kind === "vector" && !!clipData.vector
    readonly property var vector: hasVector ? clipData.vector : ({
                                                  "kind": "lottie", "inline": true, "path": "",
                                                  "width": 0, "height": 0, "fps": 0, "durationSec": 0,
                                                  "title": "", "fit": "contain", "loop": "hold",
                                                  "offset": 0, "slots": ({})
                                              })
    // Declared slots and the parse report are read once per change, not per binding: both parse
    // the document.
    property var slotRows: []
    property var report: ({})
    readonly property bool isSvg: root.vector.kind === "svg"
    // SVG appearance: which element the rows edit ("" = the whole drawing).
    property string svgTarget: ""
    readonly property var svgElements: (root.isSvg && root.report.elements) ? root.report.elements : []
    readonly property var svgElement: {
        for (let i = 0; i < root.svgElements.length; ++i)
            if (root.svgElements[i].id === root.svgTarget)
                return root.svgElements[i]
        return null
    }
    readonly property var svgSlots: (root.vector && root.vector.slots) || ({})
    readonly property var svgKeyframes: (root.vector && root.vector.keyframes) || ({})
    // The slot editor takes plain values keyed by id; a slot with no override stays absent.
    readonly property var slotValues: {
        const out = {}
        for (let i = 0; i < root.slotRows.length; ++i) {
            if (root.slotRows[i].value !== undefined)
                out[root.slotRows[i].id] = root.slotRows[i].value
        }
        return out
    }

    function refresh() {
        if (!root.hasVector) {
            root.slotRows = []
            root.report = ({})
            return
        }
        root.slotRows = EditorState.vectorSlots(EditorState.selectedTrack, EditorState.selectedClip)
        root.report = EditorState.inspectVectorClip(EditorState.selectedTrack, EditorState.selectedClip)
        if (offsetField && !offsetField.activeFocus)
            offsetField.value = root.vector.offset
    }

    function setOption(key, value) {
        const patch = {}
        patch[key] = value
        EditorState.setVectorOptions(EditorState.selectedTrack, EditorState.selectedClip, patch)
    }

    function setSlot(id, value) {
        const error = EditorState.setVectorSlot(EditorState.selectedTrack, EditorState.selectedClip, id, value)
        if (error.length > 0)
            EditorState.setLastMessage(error, "error")
    }
    function svgSlotKey(prop) {
        return "svg." + (root.svgTarget.length > 0 ? root.svgTarget + "." : "") + prop
    }
    function svgKeyframeList(prop, channel) {
        const entry = root.svgKeyframes[root.svgSlotKey(prop) + (channel ? "." + channel : "")]
        return (entry && entry.points) || []
    }
    // The element's own attribute as written, for the row's fallback when nothing overrides it.
    function svgDocumentValue(attr) {
        return root.svgElement ? String(root.svgElement[attr] || "") : ""
    }
    function resetSvgTarget() {
        const prefix = root.svgSlotKey("")
        for (const key in root.svgSlots) {
            if (key.startsWith(prefix) && key.substring(prefix.length).indexOf(".") < 0)
                root.setSlot(key, null)
        }
    }
    function resetSvgAll() {
        for (const key in root.svgSlots) {
            if (key.startsWith("svg."))
                root.setSlot(key, null)
        }
    }

    function replaceDocument() {
        const url = FileDialogs.openFile(qsTr("Replace Animation"),
                                         [qsTr("Lottie or SVG (*.json *.svg)")])
        if (url == "")
            return
        const path = url.toString().replace(/^file:\/\//, "")
        const reply = EditorState.setVectorSource(EditorState.selectedTrack, EditorState.selectedClip, path, {})
        if (!reply.ok)
            EditorState.setLastMessage(reply.error || qsTr("Could not load the document"), "error")
    }

    height: contentCol.height
    implicitHeight: contentCol.height

    Connections {
        target: EditorState
        function onSelectionChanged() { root.clipDataRevision++; root.refresh() }
        function onSelectedClipDataChanged() { root.clipDataRevision++; root.refresh() }
        function onTracksChanged() { root.clipDataRevision++; root.refresh() }
    }

    Component.onCompleted: refresh()

    Column {
        id: contentCol
        width: root.width
        spacing: Theme.spacingXl

        // ----- Document ----------------------------------------------------
        Column {
            width: parent.width
            spacing: Theme.spacingXs

            ThemedLabel { text: root.vector.kind === "svg" ? qsTr("SVG drawing") : qsTr("Lottie animation") }

            ThemedLabel {
                width: parent.width
                opacity: 0.8
                elide: Text.ElideMiddle
                text: {
                    const v = root.vector
                    const size = v.width > 0 ? qsTr("%1×%2").arg(v.width).arg(v.height) : ""
                    const timing = v.durationSec > 0
                                   ? qsTr("%1 s at %2 fps").arg(Number(v.durationSec).toFixed(2)).arg(Math.round(v.fps))
                                   : qsTr("still")
                    const where = v.inline ? qsTr("inline document") : v.path
                    return [v.title, size, timing, where].filter(s => s && s.length > 0).join(" · ")
                }
            }

            ThemedButton {
                text: qsTr("Replace document…")
                tooltip: qsTr("Load another .json or .svg; position, length, fit and loop stay")
                onClicked: root.replaceDocument()
            }
        }

        // ----- Playback ----------------------------------------------------
        Column {
            width: parent.width
            spacing: Theme.spacingSm

            ThemedLabel { text: qsTr("Playback") }

            Row {
                width: parent.width
                spacing: 8

                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: 4
                    Text {
                        text: qsTr("Fit")
                        color: Theme.c.vectorInspector.foregroundMuted
                        font.pixelSize: root.labelSize
                        font.family: Theme.fontFamily
                    }
                    ThemedComboBox {
                        width: parent.width
                        readonly property var ids: ["contain", "cover", "stretch"]
                        model: [qsTr("Contain"), qsTr("Cover"), qsTr("Stretch")]
                        tooltip: qsTr("How the drawing fills the clip box")
                        currentIndex: Math.max(0, ids.indexOf(root.vector.fit))
                        onActivated: root.setOption("fit", ids[currentIndex])
                    }
                }

                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: 4
                    visible: root.vector.durationSec > 0
                    Text {
                        text: qsTr("After the end")
                        color: Theme.c.vectorInspector.foregroundMuted
                        font.pixelSize: root.labelSize
                        font.family: Theme.fontFamily
                    }
                    ThemedComboBox {
                        width: parent.width
                        readonly property var ids: ["hold", "loop", "pingpong", "hide"]
                        model: [qsTr("Hold last frame"), qsTr("Loop"), qsTr("Ping-pong"), qsTr("Hide")]
                        tooltip: qsTr("What plays once the animation has run its length")
                        currentIndex: Math.max(0, ids.indexOf(root.vector.loop))
                        onActivated: root.setOption("loop", ids[currentIndex])
                    }
                }
            }

            Column {
                width: parent.width
                spacing: 4
                visible: root.vector.durationSec > 0
                Text {
                    text: qsTr("Start offset")
                    color: Theme.c.vectorInspector.foregroundMuted
                    font.pixelSize: root.labelSize
                    font.family: Theme.fontFamily
                }
                ThemedNumberField {
                    id: offsetField
                    width: parent.width
                    unit: "s"
                    decimals: 2
                    step: 0.1
                    from: -3600
                    to: 3600
                    onEdited: v => root.setOption("offset", v)
                }
            }
        }

        // ----- Appearance (SVG) -------------------------------------------
        Column {
            width: parent.width
            spacing: Theme.spacingMd
            visible: root.isSvg

            ThemedLabel { text: qsTr("Appearance") }
            ThemedLabel {
                width: parent.width
                opacity: 0.8
                wrapMode: Text.Wrap
                text: qsTr("Recolour the whole drawing, or one element the file names by id. Drawing-wide colours replace paints the file already has; outlines drawn with no fill stay hollow.")
            }

            Column {
                width: parent.width
                spacing: 4
                Text {
                    text: qsTr("Target")
                    color: Theme.c.vectorInspector.foregroundMuted
                    font.pixelSize: root.labelSize
                    font.family: Theme.fontFamily
                }
                ThemedComboBox {
                    id: svgTargetBox
                    width: parent.width
                    readonly property var ids: [""].concat(root.svgElements.map(e => e.id))
                    model: [qsTr("Whole drawing")].concat(root.svgElements.map(e => {
                        const classes = e.classes && e.classes.length > 0 ? " ." + e.classes.join(" .") : ""
                        return "#" + e.id + " · " + e.tag + classes + (e.inDefs ? qsTr(" (defs)") : "")
                    }))
                    tooltip: qsTr("Which part of the drawing the rows below restyle")
                    currentIndex: Math.max(0, ids.indexOf(root.svgTarget))
                    onActivated: root.svgTarget = ids[currentIndex]
                }
            }

            VectorSvgColorRow { host: root; prop: "fill"; label: qsTr("Fill") }
            VectorSvgColorRow { host: root; prop: "stroke"; label: qsTr("Stroke") }

            Row {
                width: parent.width
                spacing: 8

                PropertyKeyframeRow {
                    width: (parent.width - parent.spacing) / 2
                    propDef: { "key": "vector." + root.svgSlotKey("strokeWidth"), "label": qsTr("Stroke width"),
                               "def": root.svgSlots[root.svgSlotKey("strokeWidth")] !== undefined
                                      ? Number(root.svgSlots[root.svgSlotKey("strokeWidth")])
                                      : (parseFloat(root.svgDocumentValue("strokeWidth")) || 1),
                               "decimals": 1 }
                    keyframeList: root.svgKeyframeList("strokeWidth", "")
                    useSlider: true
                    sliderFrom: 0
                    sliderTo: 50
                }
                PropertyKeyframeRow {
                    width: (parent.width - parent.spacing) / 2
                    propDef: { "key": "vector." + root.svgSlotKey("opacity"), "label": qsTr("Opacity"),
                               "def": root.svgSlots[root.svgSlotKey("opacity")] !== undefined
                                      ? Number(root.svgSlots[root.svgSlotKey("opacity")])
                                      : (root.svgDocumentValue("opacity").length > 0 ? parseFloat(root.svgDocumentValue("opacity")) : 1),
                               "decimals": 2 }
                    keyframeList: root.svgKeyframeList("opacity", "")
                    useSlider: true
                    sliderFrom: 0
                    sliderTo: 1
                    percent: true
                }
            }

            Row {
                width: parent.width
                spacing: 8

                ThemedCheckBox {
                    visible: root.svgTarget.length > 0
                    text: qsTr("Visible")
                    checked: root.svgSlots[root.svgSlotKey("visible")] === undefined
                             || Number(root.svgSlots[root.svgSlotKey("visible")]) >= 0.5
                    onToggled: root.setSlot(root.svgSlotKey("visible"), checked ? 1 : 0)
                }
                ThemedButton {
                    variant: "secondary"
                    text: root.svgTarget.length > 0 ? qsTr("Reset element") : qsTr("Reset drawing")
                    tooltip: qsTr("Drop every override on this target")
                    onClicked: root.resetSvgTarget()
                }
                ThemedButton {
                    variant: "secondary"
                    text: qsTr("Reset all")
                    onClicked: root.resetSvgAll()
                }
            }
        }

        // ----- Slots (Lottie) ---------------------------------------------
        Column {
            width: parent.width
            spacing: Theme.spacingSm
            visible: !root.isSvg && root.slotRows.length > 0

            ThemedLabel { text: qsTr("Slots") }
            ThemedLabel {
                width: parent.width
                opacity: 0.8
                text: qsTr("Template inputs the animation declares. Overrides are per clip.")
            }

            TextParamSlots {
                width: parent.width
                specs: root.slotRows
                values: root.slotValues
                showReset: true
                onChanged: (id, value) => root.setSlot(id, value)
                onReset: id => root.setSlot(id, null)
            }
        }

        // ----- Report ------------------------------------------------------
        Column {
            width: parent.width
            spacing: Theme.spacingXs
            visible: !!((root.report.unsupported && root.report.unsupported.length > 0)
                        || (root.report.expressions && root.report.expressions.length > 0))

            ThemedLabel { text: qsTr("Not rendered") }
            Repeater {
                model: (root.report.expressions || []).map(p => qsTr("Expression on %1 (drawn static)").arg(p))
                       .concat(root.report.unsupported || [])
                delegate: ThemedLabel {
                    required property string modelData
                    width: parent.width
                    opacity: 0.8
                    wrapMode: Text.Wrap
                    text: "• " + modelData
                }
            }
        }
    }
}
