import QtQuick
import QtQuick.Controls.Basic
import Drift
import ".."

Item {
    id: root

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
                                                                       "fillKind": "solid",
                                                                       "fill": "#ff00b4ff",
                                                                       "fillSecondary": "#ff7a00ff",
                                                                       "gradientAngle": 90,
                                                                       "stroke": "#ffffffff",
                                                                       "strokeWidth": 4,
                                                                       "strokeStyle": "solid",
                                                                       "cornerRadius": 0,
                                                                       "points": 5,
                                                                       "innerRatio": 0.5,
                                                                       "headSize": 0.4,
                                                                       "thickness": 0.4,
                                                                       "tailX": 0.25,
                                                                       "tailSize": 0.2
                                                                   })
    readonly property var shapeCatalog: EditorState.builtinShapes()

    // Which geometry controls apply to the selected kind. The catalog id and the stored kind are
    // not always the same word ("circle" is an ellipse), so match on the stored kind.
    readonly property var shapeFamilies: ({
        "corner": ["rounded-rectangle", "speech-bubble-rect", "callout"],
        "star": ["star", "burst"],
        "arrow": ["arrow", "double-arrow", "block-arrow", "chevron", "banner"],
        "shaft": ["arrow", "double-arrow", "chevron", "cross", "curved-arrow"],
        "bubble": ["speech-bubble", "speech-bubble-rect", "thought-bubble", "callout"]
    })
    function shapeHas(family) {
        return root.shapeFamilies[family].indexOf(root.shapeStyle.kind) >= 0
    }

    function setShapeKey(key, value) {
        const patch = {}
        patch[key] = value
        EditorState.setShapeStyle(EditorState.selectedTrack, EditorState.selectedClip, patch)
    }

    height: shapeTabColumn.height
    implicitHeight: shapeTabColumn.height

    function refreshFields() {
        if (!root.hasShapeStyle)
            return
        const sh = root.shapeStyle
        if (gradientAngleField && !gradientAngleField.activeFocus)
            gradientAngleField.value = sh.gradientAngle
        if (strokeWidthField && !strokeWidthField.activeFocus)
            strokeWidthField.value = sh.strokeWidth
        if (cornerRadiusField && !cornerRadiusField.activeFocus)
            cornerRadiusField.value = sh.cornerRadius
        if (shapePointsField && !shapePointsField.activeFocus)
            shapePointsField.value = sh.points
    }

    Connections {
        target: EditorState
        function onSelectionChanged() { root.clipDataRevision++; root.refreshFields() }
        function onSelectedClipDataChanged() { root.clipDataRevision++; root.refreshFields() }
        function onTracksChanged() { root.clipDataRevision++; root.refreshFields() }
    }

    Component.onCompleted: refreshFields()

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
                if (seen[entry.kind])
                    continue
                seen[entry.kind] = true
                out.push(entry)
            }
            return out
        }

        Column {
            width: parent.width
            spacing: Theme.spacingXs

            ThemedLabel { text: qsTr("Shape") }

            ThemedLabel {
                width: parent.width
                opacity: 0.8
                text: qsTr("Swapping the shape keeps its position, size and effects.")
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
                onActivated: root.setShapeKey("kind", currentValue)
            }
        }

        // ----- Fill -------------------------------------------------------
        Column {
            width: parent.width
            spacing: Theme.spacingSm

            ThemedLabel { text: qsTr("Fill") }

            ThemedComboBox {
                id: fillKindBox
                width: parent.width
                model: ["none", "solid", "linear", "radial"]
                readonly property var labels: ({
                    "none": qsTr("None"),
                    "solid": qsTr("Solid"),
                    "linear": qsTr("Linear gradient"),
                    "radial": qsTr("Radial gradient")
                })
                displayText: labels[model[currentIndex]] || model[currentIndex]
                tooltip: qsTr("How the shape's interior is painted")
                currentIndex: Math.max(0, model.indexOf(root.shapeStyle.fillKind))
                onActivated: root.setShapeKey("fillKind", model[currentIndex])
            }

            ColorSwatchField {
                visible: root.shapeStyle.fillKind !== "none"
                hex: root.shapeStyle.fill
                tooltip: root.shapeStyle.fillKind === "solid"
                         ? qsTr("Choose fill colour")
                         : qsTr("Choose the gradient's start colour")
                onEdited: value => root.setShapeKey("fill", value)
            }

            ColorSwatchField {
                visible: root.shapeStyle.fillKind === "linear"
                         || root.shapeStyle.fillKind === "radial"
                hex: root.shapeStyle.fillSecondary
                tooltip: qsTr("Choose the gradient's end colour")
                onEdited: value => root.setShapeKey("fillSecondary", value)
            }

            Column {
                visible: root.shapeStyle.fillKind === "linear"
                width: parent.width
                spacing: Theme.spacingXs

                ThemedLabel { text: qsTr("Gradient angle") }

                ThemedNumberField {
                    id: gradientAngleField
                    width: parent.width
                    unit: "°"
                    decimals: 0
                    step: 15
                    from: -360
                    to: 360
                    onEdited: v => root.setShapeKey("gradientAngle", v)
                }
            }
        }

        // ----- Stroke -----------------------------------------------------
        Column {
            width: parent.width
            spacing: Theme.spacingSm

            ThemedLabel { text: qsTr("Stroke") }

            ThemedComboBox {
                id: strokeStyleBox
                width: parent.width
                model: ["none", "solid", "dash", "dot", "dashdot"]
                readonly property var labels: ({
                    "none": qsTr("None"),
                    "solid": qsTr("Solid"),
                    "dash": qsTr("Dashed"),
                    "dot": qsTr("Dotted"),
                    "dashdot": qsTr("Dash-dot")
                })
                displayText: labels[model[currentIndex]] || model[currentIndex]
                tooltip: qsTr("Outline style")
                currentIndex: Math.max(0, model.indexOf(root.shapeStyle.strokeStyle))
                onActivated: root.setShapeKey("strokeStyle", model[currentIndex])
            }

            ColorSwatchField {
                visible: root.shapeStyle.strokeStyle !== "none"
                hex: root.shapeStyle.stroke
                tooltip: qsTr("Choose stroke colour")
                onEdited: value => root.setShapeKey("stroke", value)
            }

            Column {
                visible: root.shapeStyle.strokeStyle !== "none"
                width: parent.width
                spacing: Theme.spacingXs

                ThemedLabel { text: qsTr("Stroke width") }

                ThemedNumberField {
                    id: strokeWidthField
                    width: parent.width
                    unit: "px"
                    decimals: 0
                    step: 1
                    from: 0
                    to: 200
                    onEdited: v => root.setShapeKey("strokeWidth", v)
                }
            }
        }

        // ----- Geometry ---------------------------------------------------
        Column {
            visible: root.shapeHas("corner")
            width: parent.width
            spacing: Theme.spacingXs

            ThemedLabel { text: qsTr("Corner radius") }

            ThemedNumberField {
                id: cornerRadiusField
                width: parent.width
                unit: "px"
                decimals: 0
                step: 4
                from: 0
                to: 2000
                onEdited: v => root.setShapeKey("cornerRadius", v)
            }
        }

        Column {
            visible: root.shapeHas("star")
            width: parent.width
            spacing: Theme.spacingSm

            ThemedLabel { text: qsTr("Points") }

            ThemedNumberField {
                id: shapePointsField
                width: parent.width
                decimals: 0
                step: 1
                from: 3
                to: 60
                onEdited: v => root.setShapeKey("points", v)
            }

            ThemedLabel { text: qsTr("Inner radius") }

            ThemedLabel {
                width: parent.width
                opacity: 0.8
                text: qsTr("How deep the notches cut between the points.")
            }

            ThemedSlider {
                id: innerRatioSlider
                label: qsTr("Inner radius")
                width: parent.width
                from: 0.05
                to: 0.95
                stepSize: 0.01
                Binding on value {
                    when: !innerRatioSlider.pressed
                    value: root.shapeStyle.innerRatio
                }
                onMoved: root.setShapeKey("innerRatio", value)
                onPressedChanged: {
                    if (pressed)
                        EditorState.beginPreviewDrag(qsTr("Shape style changed"))
                    else
                        EditorState.commitPreviewDrag()
                }
            }
        }

        Column {
            visible: root.shapeHas("arrow")
            width: parent.width
            spacing: Theme.spacingXs

            ThemedLabel { text: qsTr("Head size") }

            ThemedSlider {
                id: headSizeSlider
                label: qsTr("Head size")
                width: parent.width
                from: 0.05
                to: 0.9
                stepSize: 0.01
                Binding on value {
                    when: !headSizeSlider.pressed
                    value: root.shapeStyle.headSize
                }
                onMoved: root.setShapeKey("headSize", value)
                onPressedChanged: {
                    if (pressed)
                        EditorState.beginPreviewDrag(qsTr("Shape style changed"))
                    else
                        EditorState.commitPreviewDrag()
                }
            }
        }

        Column {
            visible: root.shapeHas("shaft")
            width: parent.width
            spacing: Theme.spacingXs

            ThemedLabel { text: qsTr("Thickness") }

            ThemedSlider {
                id: thicknessSlider
                label: qsTr("Thickness")
                width: parent.width
                from: 0.05
                to: 1.0
                stepSize: 0.01
                Binding on value {
                    when: !thicknessSlider.pressed
                    value: root.shapeStyle.thickness
                }
                onMoved: root.setShapeKey("thickness", value)
                onPressedChanged: {
                    if (pressed)
                        EditorState.beginPreviewDrag(qsTr("Shape style changed"))
                    else
                        EditorState.commitPreviewDrag()
                }
            }
        }

        Column {
            visible: root.shapeHas("bubble")
            width: parent.width
            spacing: Theme.spacingSm

            ThemedLabel { text: qsTr("Tail position") }

            ThemedLabel {
                width: parent.width
                opacity: 0.8
                text: qsTr("Where the tail meets the bottom of the bubble.")
            }

            ThemedSlider {
                id: tailXSlider
                label: qsTr("Tail position")
                width: parent.width
                from: 0.08
                to: 0.92
                stepSize: 0.01
                Binding on value {
                    when: !tailXSlider.pressed
                    value: root.shapeStyle.tailX
                }
                onMoved: root.setShapeKey("tailX", value)
                onPressedChanged: {
                    if (pressed)
                        EditorState.beginPreviewDrag(qsTr("Shape style changed"))
                    else
                        EditorState.commitPreviewDrag()
                }
            }

            ThemedLabel { text: qsTr("Tail size") }

            ThemedSlider {
                id: tailSizeSlider
                label: qsTr("Tail size")
                width: parent.width
                from: 0.05
                to: 0.5
                stepSize: 0.01
                Binding on value {
                    when: !tailSizeSlider.pressed
                    value: root.shapeStyle.tailSize
                }
                onMoved: root.setShapeKey("tailSize", value)
                onPressedChanged: {
                    if (pressed)
                        EditorState.beginPreviewDrag(qsTr("Shape style changed"))
                    else
                        EditorState.commitPreviewDrag()
                }
            }
        }
    }
}
