import QtQuick
import Drift

// Inspector for a built-in generator clip (solid, gradient, noise, test patterns, countdown,
// particles): its parameters as the effect param controls, addressed as "proc.<key>" on the
// keyframe stack, plus the random seed. A countdown's number is styled on the Text tab.
Item {
    id: root

    property int clipDataRevision: 0
    readonly property var clipData: {
        void clipDataRevision
        return EditorState.selectedClipData
    }
    readonly property bool hasGenerator: !!clipData && clipData.kind === "generator" && !!clipData.generator
    readonly property var generator: hasGenerator ? clipData.generator : ({ "kindId": "", "kindName": "", "description": "", "seed": 0, "params": [] })
    readonly property var params: generator.params || []
    readonly property bool seeded: generator.kindId === "gen.noise" || generator.kindId === "gen.particles"
    property var closedGroups: ({})

    function refresh() {
        if (seedField && !seedField.activeFocus)
            seedField.value = root.generator.seed
    }
    function groupOf(i) { return i >= 0 && i < root.params.length ? (root.params[i].group || "") : "" }
    function isOpen(group) { return group === "" || root.closedGroups[group] !== true }
    function toggleGroup(group) {
        const next = Object.assign({}, root.closedGroups)
        next[group] = root.isOpen(group)
        root.closedGroups = next
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

        Column {
            width: parent.width
            spacing: Theme.spacingXs

            ThemedLabel {
                text: root.generator.kindName
                font.weight: Font.Medium
                SearchAnchor {
                    searchId: "generator-kind"
                    title: qsTr("Generator")
                    keywords: "solid gradient noise bars grid checkerboard countdown particles confetti snow"
                }
            }
            ThemedLabel {
                width: parent.width
                opacity: 0.8
                wrapMode: Text.Wrap
                visible: text.length > 0
                text: root.generator.description
            }
        }

        Column {
            width: parent.width
            spacing: Theme.spacingXs
            visible: root.seeded

            ThemedLabel {
                text: qsTr("Seed")
                SearchAnchor {
                    searchId: "generator-seed"
                    title: qsTr("Generator seed")
                    keywords: "random noise particles"
                }
            }

            ThemedNumberField {
                id: seedField
                width: parent.width
                from: 0
                to: 2147483647
                onEdited: v => EditorState.setProceduralSeed(EditorState.selectedTrack, EditorState.selectedClip,
                                                              Math.round(v))
            }
        }

        Column {
            width: parent.width
            spacing: 6
            visible: root.params.length > 0

            ThemedLabel {
                text: qsTr("Parameters")
                SearchAnchor {
                    searchId: "generator-params"
                    title: qsTr("Generator parameters")
                    keywords: "keyframe colour color emitter rate gravity"
                }
            }

            Repeater {
                model: root.params.length
                delegate: Column {
                    id: paramRow
                    required property int index
                    readonly property var paramData: root.params[index] || ({})
                    readonly property string group: paramData.group || ""
                    readonly property bool groupStart: group !== "" && root.groupOf(index - 1) !== group
                    width: root.width
                    spacing: 4
                    visible: group === "" || groupStart || root.isOpen(group)

                    EffectsParamGroupHeader {
                        visible: paramRow.groupStart
                        groupOpen: root.isOpen(paramRow.group)
                        groupTitle: paramRow.group
                        onToggled: root.toggleGroup(paramRow.group)
                    }

                    Column {
                        width: parent.width
                        spacing: 4
                        visible: paramRow.group === "" || root.isOpen(paramRow.group)

                        EffectsBoolParam {
                            row: paramRow
                            procedural: true
                        }

                        EffectsColorParam {
                            row: paramRow
                            inspector: root
                            procedural: true
                        }

                        Vec2Param {
                            visible: paramRow.paramData.type === "vec2"
                            width: parent.width
                            paramData: paramRow.paramData
                            effectIndex: -1
                            procedural: true
                        }

                        EffectsEnumParam {
                            row: paramRow
                            procedural: true
                        }

                        Column {
                            width: parent.width
                            spacing: Theme.spacingXs
                            visible: paramRow.paramData.type === "file"

                            ThemedLabel {
                                text: paramRow.paramData.label || ""
                            }
                            ThemedTextField {
                                width: parent.width
                                text: paramRow.paramData.value || ""
                                placeholderText: qsTr("Emoji or path to an image")
                                onEditingFinished: EditorState.setProceduralTextParam(
                                    EditorState.selectedTrack, EditorState.selectedClip, paramRow.paramData.key, text)
                            }
                        }

                        PropertyKeyframeRow {
                            visible: paramRow.paramData.type === "int" || paramRow.paramData.type === "float"
                            width: parent.width
                            propDef: ({
                                key: paramRow.paramData.prop || "",
                                label: paramRow.paramData.label,
                                def: paramRow.paramData.value,
                                decimals: paramRow.paramData.type === "int" ? 0
                                          : (paramRow.paramData.step || 0) >= 1 ? 0
                                          : Math.abs(paramRow.paramData.max - paramRow.paramData.min) >= 10 ? 1 : 2
                            })
                            keyframeList: (paramRow.paramData.keyframes && paramRow.paramData.keyframes.points) || []
                            useSlider: true
                            editableWhilePlaying: true
                            sliderFrom: paramRow.paramData.min
                            sliderTo: paramRow.paramData.max
                            sliderStep: paramRow.paramData.step || 0
                        }
                    }
                }
            }
        }
    }
}
