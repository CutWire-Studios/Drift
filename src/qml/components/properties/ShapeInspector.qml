import QtQuick
import QtQuick.Controls.Basic
import Drift
// The shape clip's inspector: which shape, its shading stack (the same layer rows text uses,
// keyed "shape.layer.<id>.<field>") and the keyframable geometry knobs.
Item {
    id: root

    // The pen tool lives on the desktop preview; the phone shell has no tool to enter.
    property bool canEditPath: true
    property int clipDataRevision: 0
    readonly property var clipData: {
        void clipDataRevision
        return EditorState.selectedClipData
    }
    readonly property bool hasSelection: !!clipData && Object.keys(clipData).length > 0
    readonly property string clipKind: hasSelection ? (clipData.kind || "") : ""

    readonly property bool hasShapeStyle: hasSelection && clipKind === "shape" && !!clipData.shapeStyle
    readonly property var shapeStyle: hasShapeStyle ? clipData.shapeStyle : ({
                                                                       "kind": "rectangle",
                                                                       "layers": [],
                                                                       "cornerRadius": 0,
                                                                       "points": 5,
                                                                       "innerRatio": 0.5,
                                                                       "headSize": 0.4,
                                                                       "thickness": 0.4,
                                                                       "tailX": 0.25,
                                                                       "tailSize": 0.2
                                                                   })
    readonly property var layers: (shapeStyle && shapeStyle.layers) || []
    readonly property var shapeCatalog: EditorState.builtinShapes()
    // Which rows are open, keyed by layer id so it survives reorders and delegate rebuilds.
    property var expandedLayerIds: ({})

    // Which geometry controls apply to the selected kind. The catalog id and the stored kind are
    // not always the same word ("circle" is an ellipse), so match on the stored kind.
    readonly property var shapeFamilies: ({
        "points": ["star", "burst", "gear", "seal", "polygon"],
        "inner": ["star", "burst", "gear", "seal"],
        "sweep": ["pie", "arc"],
        "arrow": ["arrow", "double-arrow", "block-arrow", "chevron", "banner"],
        "shaft": ["arrow", "double-arrow", "chevron", "cross", "curved-arrow", "ring", "frame", "arc",
                  "checkmark", "x-mark"],
        "bubble": ["speech-bubble", "speech-bubble-rect", "thought-bubble", "callout"]
    })
    readonly property bool isPath: shapeStyle.kind === "path"
    readonly property var pathKeyTimes: shapeStyle.pathKeyTimes || []
    readonly property string pointsLabel: shapeStyle.kind === "polygon" ? qsTr("Sides")
                                          : shapeStyle.kind === "gear" ? qsTr("Teeth") : qsTr("Points")
    function shapeHas(family) {
        return root.shapeFamilies[family].indexOf(root.shapeStyle.kind) >= 0
    }

    function setShapeKey(key, value) {
        const patch = {}
        patch[key] = value
        EditorState.setShapeStyle(EditorState.selectedTrack, EditorState.selectedClip, patch)
    }
    function knob(key, label, decimals) {
        return { "key": "shape." + key, "label": label, "def": Number(root.shapeStyle[key]) || 0, "decimals": decimals }
    }
    function knobKeyframes(key) {
        const keys = root.shapeStyle && root.shapeStyle.keyframes
        const entry = keys && keys[key]
        return (entry && entry.points) || []
    }
    function setLayerExpanded(id, on) {
        const next = Object.assign({}, root.expandedLayerIds)
        if (on)
            next[id] = true
        else
            delete next[id]
        root.expandedLayerIds = next
    }
    function addLayer(kind) {
        const id = EditorState.addStyleLayer(EditorState.selectedTrack, EditorState.selectedClip, kind)
        if (id && id.length > 0)
            root.setLayerExpanded(id, true)
    }

    height: shapeTabColumn.height
    implicitHeight: shapeTabColumn.height

    Connections {
        target: EditorState
        function onSelectionChanged() { root.clipDataRevision++ }
        function onSelectedClipDataChanged() { root.clipDataRevision++ }
        function onTracksChanged() { root.clipDataRevision++ }
    }

    Column {
        id: shapeTabColumn
        width: root.width
        spacing: Theme.spacingXl

        // One entry per ShapeKind: the catalog lists "circle" and "ellipse"
        // separately so each gets its own default aspect, but they are one kind.
        readonly property var kindOptions: {
            const seen = ({})
            const out = []
            for (let i = 0; i < root.shapeCatalog.length; ++i) {
                const entry = root.shapeCatalog[i]
                if (seen[entry.kind] || entry.user === true || entry.kind === "path")
                    continue
                seen[entry.kind] = true
                out.push(entry)
            }
            if (root.isPath)
                out.push({ "id": "path", "kind": "path", "label": qsTr("Custom path") })
            return out
        }

        Column {
            width: parent.width
            spacing: Theme.spacingXs

            ThemedLabel {
                text: qsTr("Shape")
                SearchAnchor {
                    searchId: "shape-style"
                    title: qsTr("Shape style")
                    keywords: "fill stroke"
                }
            }

            ThemedLabel {
                width: parent.width
                opacity: 0.8
                text: qsTr("Swapping the shape keeps its position, size, style and effects.")
            }

            ThemedComboBox {
                id: shapeKindBox
                width: parent.width
                model: shapeTabColumn.kindOptions
                textRole: "label"
                valueRole: "id"
                tooltip: qsTr("Shape drawn by this clip")
                currentIndex: {
                    const options = shapeTabColumn.kindOptions
                    for (let i = 0; i < options.length; ++i) {
                        if (options[i].kind === root.shapeStyle.kind)
                            return i
                    }
                    return 0
                }
                onActivated: {
                    if (currentValue !== "path")
                        root.setShapeKey("kind", currentValue)
                }
            }
        }

        // ----- Custom path ------------------------------------------------
        Column {
            width: parent.width
            spacing: Theme.spacingMd

            Column {
                width: parent.width
                spacing: Theme.spacingXs
                visible: !root.isPath && root.canEditPath

                ThemedButton {
                    variant: "secondary"
                    glyph: Theme.icons.penTool
                    text: qsTr("Make editable")
                    tooltip: qsTr("Turn this shape into a path you can reshape point by point")
                    onClicked: {
                        EditorState.convertShapeToPath(EditorState.selectedTrack, EditorState.selectedClip)
                        EditorState.preview.shapePathMode = "edit"
                    }

                    SearchAnchor {
                        searchId: "shape-make-editable"
                        title: qsTr("Make shape editable")
                        keywords: "path bezier points convert vector"
                    }
                }
                ThemedLabel {
                    width: parent.width
                    opacity: 0.8
                    text: qsTr("Its size, style and effects stay; knob animation on the shape is dropped.")
                }
            }

            Column {
                width: parent.width
                spacing: Theme.spacingMd
                visible: root.isPath

                ThemedButton {
                    visible: root.canEditPath
                    variant: "secondary"
                    glyph: Theme.icons.penTool
                    text: qsTr("Edit path")
                    tooltip: qsTr("Edit this path's points and handles in the preview")
                    onClicked: EditorState.preview.shapePathMode = "edit"

                    SearchAnchor {
                        searchId: "shape-edit-path"
                        title: qsTr("Edit path")
                        keywords: "pen bezier points handles vector"
                    }
                }

                PathPointModeRow { }

                Column {
                    width: parent.width
                    spacing: Theme.spacingXs

                    ThemedLabel { text: qsTr("Fill rule") }
                    ThemedComboBox {
                        width: parent.width
                        model: [qsTr("Non-zero"), qsTr("Even-odd")]
                        tooltip: qsTr("How overlapping or nested contours are filled")
                        currentIndex: root.shapeStyle.fillRule === "evenodd" ? 1 : 0
                        onActivated: root.setShapeKey("fillRule", currentIndex === 1 ? "evenodd" : "nonzero")
                    }
                }

                Row {
                    width: parent.width
                    spacing: Theme.spacingSm

                    Text {
                        width: parent.width - pathKeyButton.width - parent.spacing
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.shapeStyle.pathAnimated
                              ? qsTr("Path animation: %n key(s)", "", root.pathKeyTimes.length)
                              : qsTr("Path animation")
                        color: Theme.c.shapeInspector.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSm
                    }
                    ThemedButton {
                        id: pathKeyButton
                        variant: root.shapeStyle.pathAnimated ? "primary" : "secondary"
                        glyph: root.shapeStyle.pathAnimated ? Theme.icons.diamondMinus : Theme.icons.diamondPlus
                        text: root.shapeStyle.pathAnimated ? qsTr("Stop") : qsTr("Animate")
                        tooltip: root.shapeStyle.pathAnimated
                                 ? qsTr("Remove all path keys and keep the current outline")
                                 : qsTr("Key the whole outline at the playhead, then edit it at other times")
                        onClicked: EditorState.setShapePathKeyed(EditorState.selectedTrack, EditorState.selectedClip,
                                                                 !root.shapeStyle.pathAnimated)

                        SearchAnchor {
                            searchId: "shape-path-animation"
                            title: qsTr("Animate path")
                            keywords: "keyframe morph path shape"
                        }
                    }
                }

                Repeater {
                    model: root.pathKeyTimes.length
                    delegate: Row {
                        id: keyRow
                        required property int index
                        readonly property real keyTime: root.pathKeyTimes[index] || 0
                        width: parent.width
                        spacing: Theme.spacingSm

                        ThemedButton {
                            width: parent.width - removeKeyButton.width - parent.spacing
                            variant: "ghost"
                            text: qsTr("Key at %1 s").arg(keyRow.keyTime.toFixed(2))
                            tooltip: qsTr("Move the playhead to this key")
                            onClicked: EditorState.playheadSeconds = keyRow.keyTime
                        }
                        ThemedButton {
                            id: removeKeyButton
                            variant: "ghost"
                            glyph: Theme.icons.trash
                            tooltip: qsTr("Remove this key")
                            onClicked: EditorState.removeShapePathKey(EditorState.selectedTrack, EditorState.selectedClip,
                                                                      keyRow.keyTime)
                        }
                    }
                }
            }

            ThemedButton {
                variant: "secondary"
                glyph: Theme.icons.shapes
                text: qsTr("Save to My shapes")
                tooltip: qsTr("Keep this shape's outline in the Shapes tab to reuse it")
                onClicked: saveShapeDialog.openWith(qsTr("Save to My shapes"), qsTr("My shape"))

                SearchAnchor {
                    searchId: "shape-save-to-library"
                    title: qsTr("Save shape to My shapes")
                    keywords: "library custom svg"
                }
            }
        }

        // ----- Layers -----------------------------------------------------
        Column {
            width: parent.width
            spacing: Theme.spacingMd

            Row {
                width: parent.width
                spacing: Theme.spacingSm

                Text {
                    width: parent.width - addLayerButton.width - parent.spacing
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Layers")
                    color: Theme.c.shapeInspector.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSm
                    font.weight: Font.DemiBold
                }
                ThemedButton {
                    id: addLayerButton
                    variant: "secondary"
                    glyph: Theme.icons.plus
                    text: qsTr("Add layer")
                    tooltip: qsTr("Add a fill, stroke, shadow, glow or extrude layer")
                    onClicked: addLayerMenu.popup()

                    ThemedContextMenu {
                        id: addLayerMenu
                        ThemedMenuItem { text: qsTr("Fill"); onTriggered: root.addLayer("fill") }
                        ThemedMenuItem { text: qsTr("Stroke"); onTriggered: root.addLayer("stroke") }
                        ThemedMenuItem { text: qsTr("Shadow"); onTriggered: root.addLayer("shadow") }
                        ThemedMenuItem { text: qsTr("Glow"); onTriggered: root.addLayer("glow") }
                        ThemedMenuItem { text: qsTr("Extrude"); onTriggered: root.addLayer("extrude") }
                    }
                }
            }

            Text {
                visible: root.layers.length === 0
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("No layers. Add a fill to start.")
                color: Theme.c.shapeInspector.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            // Integer model so rows survive the fresh QVariantList every edit produces; the list
            // is walked from the back so the front-most layer sits on top.
            Repeater {
                model: root.layers.length
                delegate: ShadingLayerRow {
                    required property int index
                    width: parent.width
                    layerData: root.layers[root.layers.length - 1 - index] || ({})
                    styleData: root.shapeStyle
                    keyPrefix: "shape"
                    position: index
                    count: root.layers.length
                    expanded: root.expandedLayerIds[layerId] === true
                    onToggleRequested: root.setLayerExpanded(layerId, !expanded)
                }
            }
        }

        // ----- Geometry ---------------------------------------------------
        CollapsibleSection {
            width: parent.width
            title: qsTr("Geometry")

            Column {
                width: parent.width
                spacing: Theme.spacingMd

                PropertyKeyframeRow {
                    width: parent.width
                    visible: root.shapeStyle.kind !== "ellipse"
                    propDef: root.knob("cornerRadius", qsTr("Corner radius"), 0)
                    keyframeList: root.knobKeyframes("cornerRadius")
                    useSlider: true
                    sliderFrom: 0
                    sliderTo: 400
                    unit: "px"
                }

                Row {
                    width: parent.width
                    spacing: 8
                    visible: root.shapeHas("points")

                    PropertyKeyframeRow {
                        width: root.shapeHas("inner") ? (parent.width - parent.spacing) / 2 : parent.width
                        propDef: root.knob("points", root.pointsLabel, 0)
                        keyframeList: root.knobKeyframes("points")
                        useSlider: true
                        sliderFrom: 3
                        sliderTo: 60
                    }
                    PropertyKeyframeRow {
                        width: (parent.width - parent.spacing) / 2
                        visible: root.shapeHas("inner")
                        propDef: root.knob("innerRatio", qsTr("Inner radius"), 2)
                        keyframeList: root.knobKeyframes("innerRatio")
                        useSlider: true
                        sliderFrom: 0.05
                        sliderTo: 0.95
                        percent: true
                    }
                }

                PropertyKeyframeRow {
                    width: parent.width
                    visible: root.shapeHas("sweep")
                    propDef: root.knob("sweep", qsTr("Sweep"), 0)
                    keyframeList: root.knobKeyframes("sweep")
                    useSlider: true
                    sliderFrom: 1
                    sliderTo: 359
                    unit: "°"
                }

                Row {
                    width: parent.width
                    spacing: 8
                    visible: root.shapeHas("arrow") || root.shapeHas("shaft")

                    PropertyKeyframeRow {
                        width: (parent.width - parent.spacing) / 2
                        visible: root.shapeHas("arrow")
                        propDef: root.knob("headSize", qsTr("Head size"), 2)
                        keyframeList: root.knobKeyframes("headSize")
                        useSlider: true
                        sliderFrom: 0.05
                        sliderTo: 0.9
                        percent: true
                    }
                    PropertyKeyframeRow {
                        width: (parent.width - parent.spacing) / 2
                        visible: root.shapeHas("shaft")
                        propDef: root.knob("thickness", qsTr("Thickness"), 2)
                        keyframeList: root.knobKeyframes("thickness")
                        useSlider: true
                        sliderFrom: 0.05
                        sliderTo: 1
                        percent: true
                    }
                }

                Row {
                    width: parent.width
                    spacing: 8
                    visible: root.shapeHas("bubble")

                    PropertyKeyframeRow {
                        width: (parent.width - parent.spacing) / 2
                        propDef: root.knob("tailX", qsTr("Tail position"), 2)
                        keyframeList: root.knobKeyframes("tailX")
                        useSlider: true
                        sliderFrom: 0.08
                        sliderTo: 0.92
                        percent: true
                    }
                    PropertyKeyframeRow {
                        width: (parent.width - parent.spacing) / 2
                        propDef: root.knob("tailSize", qsTr("Tail size"), 2)
                        keyframeList: root.knobKeyframes("tailSize")
                        useSlider: true
                        sliderFrom: 0.05
                        sliderTo: 0.5
                        percent: true
                    }
                }
            }
        }
    }

    NameDialog {
        id: saveShapeDialog
        onSubmitted: name => EditorState.saveShapeToLibrary(EditorState.selectedTrack, EditorState.selectedClip, name)
    }
}
