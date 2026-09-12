import QtQuick
import QtQuick.Controls.Basic
import Drift
import ".."

// Lottie / SVG clip inspector: what the document is, how it plays, and the slots it exposes.
Item {
    id: root

    property int clipDataRevision: 0
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

    // A colour override row: swatch, keyframe diamond and reset. The four channel tracks are one
    // series to the user, so the diamond lights when any of them has keys.
    component SvgColorRow: Column {
        id: colorRow
        property string prop: "fill"
        property string label: ""
        readonly property string slotKey: root.svgSlotKey(prop)
        readonly property string keyframeKey: "vector." + slotKey
        readonly property bool animated: ["r", "g", "b", "a"].some(c => root.svgKeyframeList(prop, c).length > 0)
        readonly property bool keysEnabled: {
            void root.clipDataRevision
            return !animated || EditorState.clipPropertyKeyframesEnabled(EditorState.selectedTrack, EditorState.selectedClip,
                                                                         keyframeKey + ".r")
        }
        readonly property bool overridden: root.svgSlots[slotKey] !== undefined
        readonly property string hex: {
            void root.clipDataRevision
            void EditorState.playheadSeconds
            if (animated) {
                const ch = c => EditorState.propertyValueAt(EditorState.selectedTrack, EditorState.selectedClip,
                                                            keyframeKey + "." + c, EditorState.playheadSeconds, 0)
                return Qt.rgba(ch("r"), ch("g"), ch("b"), ch("a")).toString()
            }
            if (overridden)
                return String(root.svgSlots[slotKey])
            const own = root.svgDocumentValue(prop)
            return own.length > 0 && own !== "none" && own.indexOf("url(") < 0 ? own : "#ff000000"
        }
        width: parent.width
        spacing: 4

        function commit(value) {
            if (colorRow.animated || EditorState.autoKeyEnabled) {
                EditorState.showKeyframeGraphProperty(colorRow.keyframeKey + ".r")
                EditorState.setClipColorKeyframe(EditorState.selectedTrack, EditorState.selectedClip, colorRow.keyframeKey,
                                                 EditorState.playheadSeconds, value)
            } else {
                root.setSlot(colorRow.slotKey, value)
            }
        }

        Row {
            width: parent.width
            spacing: 4
            KeyframeDiamond {
                anchors.verticalCenter: parent.verticalCenter
                accentColor: Theme.keyframeCurveColor(colorRow.keyframeKey)
                animated: colorRow.animated
                keysEnabled: colorRow.keysEnabled
                tooltip: colorRow.animated ? qsTr("Toggle %1's keyframes").arg(colorRow.label)
                                           : qsTr("Key %1 at the playhead").arg(colorRow.label)
                onToggled: {
                    if (colorRow.animated) {
                        for (const c of ["r", "g", "b", "a"])
                            EditorState.toggleClipPropertyKeyframesEnabled(EditorState.selectedTrack, EditorState.selectedClip,
                                                                           colorRow.keyframeKey + "." + c)
                    } else {
                        EditorState.setClipColorKeyframe(EditorState.selectedTrack, EditorState.selectedClip,
                                                         colorRow.keyframeKey, EditorState.playheadSeconds, colorRow.hex)
                    }
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: colorRow.label
                color: colorRow.overridden || colorRow.animated ? Theme.panelForeground : Theme.mutedForeground
                font.pixelSize: Theme.fontSizeXs
                font.family: Theme.fontFamily
            }
            Item { width: 1; height: 1 }
        }
        Row {
            width: parent.width
            spacing: 4
            ColorSwatchField {
                width: parent.width - resetButton.width - parent.spacing
                hex: colorRow.hex
                tooltip: qsTr("Override the %1 colour").arg(colorRow.label.toLowerCase())
                onEdited: value => colorRow.commit(value)
            }
            IconButton {
                id: resetButton
                glyph: Theme.icons.x
                variant: "ghost"
                buttonSize: Theme.controlHeightSm
                iconSize: 12
                anchors.verticalCenter: parent.verticalCenter
                enabled: colorRow.overridden || colorRow.animated
                tooltip: qsTr("Back to the drawing's own %1").arg(colorRow.label.toLowerCase())
                onClicked: root.setSlot(colorRow.slotKey, null)
            }
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
                        color: Theme.mutedForeground
                        font.pixelSize: Theme.fontSizeXs
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
                        color: Theme.mutedForeground
                        font.pixelSize: Theme.fontSizeXs
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
                    color: Theme.mutedForeground
                    font.pixelSize: Theme.fontSizeXs
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
                    color: Theme.mutedForeground
                    font.pixelSize: Theme.fontSizeXs
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

            SvgColorRow { prop: "fill"; label: qsTr("Fill") }
            SvgColorRow { prop: "stroke"; label: qsTr("Stroke") }

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
