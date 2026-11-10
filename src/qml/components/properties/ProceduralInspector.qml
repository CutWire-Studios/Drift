import QtQuick
import Drift

// Procedural 3D clip inspector: which package builds the scene, that package's parameters (the
// effect param controls, addressed as "proc.<key>" on the keyframe stack), the seed, and the
// latest evaluation error for the clip.
Item {
    id: root

    property int clipDataRevision: 0
    readonly property var clipData: {
        void clipDataRevision
        return EditorState.selectedClipData
    }
    readonly property bool hasProcedural: !!clipData && clipData.kind === "procedural" && !!clipData.procedural
    readonly property var procedural: hasProcedural ? clipData.procedural : ({
                                                          "packageId": "", "packageName": "", "seed": 0,
                                                          "params": [], "packageMissing": false
                                                      })
    readonly property var params: procedural.params || []
    property var packages: []
    readonly property int packageIndex: {
        for (let i = 0; i < packages.length; i++) {
            if (packages[i].id === procedural.packageId)
                return i
        }
        return -1
    }
    readonly property string packageDescription: packageIndex >= 0 ? packages[packageIndex].description : ""
    property var diagnostics: ({})
    readonly property string errorText: {
        if (!hasProcedural)
            return ""
        if (procedural.packageMissing)
            return qsTr("Package \"%1\" is not installed; the clip renders nothing until it returns.")
                .arg(procedural.packageId)
        if (diagnostics.known !== true || diagnostics.packageId !== procedural.packageId)
            return ""
        const lines = [diagnostics.error].concat(diagnostics.errors || []).filter(l => l && l.length > 0)
        return lines.slice(0, 3).join("\n")
    }

    function refresh() {
        if (seedField && !seedField.activeFocus)
            seedField.value = root.procedural.seed
    }

    function pollDiagnostics() {
        if (hasProcedural && clipData.id)
            diagnostics = EditorState.proceduralDiagnostics(clipData.id)
    }

    height: contentCol.height
    implicitHeight: contentCol.height

    Connections {
        target: EditorState
        function onSelectionChanged() { root.clipDataRevision++; root.refresh(); root.pollDiagnostics() }
        function onSelectedClipDataChanged() { root.clipDataRevision++; root.refresh() }
        function onTracksChanged() { root.clipDataRevision++; root.refresh() }
    }

    Connections {
        target: ProceduralEditor
        function onCatalogChanged() { root.packages = EditorState.proceduralPackages() }
    }

    // Evaluation happens on the compositor worker and has no signal to the UI, so the report is polled.
    Timer {
        interval: 500
        repeat: true
        running: root.visible && root.hasProcedural
        onTriggered: root.pollDiagnostics()
    }

    Component.onCompleted: {
        packages = EditorState.proceduralPackages()
        refresh()
        pollDiagnostics()
    }

    Column {
        id: contentCol
        width: root.width
        spacing: Theme.spacingXl

        Row {
            width: parent.width
            spacing: Theme.spacingXs
            visible: root.errorText.length > 0
            IconGlyph {
                glyph: Theme.icons.warning
                iconSize: 12
                iconColor: Theme.c.proceduralInspector.iconError
                anchors.top: parent.top
                anchors.topMargin: 2
            }
            ThemedLabel {
                width: parent.width - 12 - parent.spacing
                wrapMode: Text.Wrap
                color: Theme.c.proceduralInspector.foregroundError
                text: root.errorText
            }
        }

        Column {
            width: parent.width
            spacing: Theme.spacingSm

            ThemedLabel {
                text: qsTr("Package")
                SearchAnchor {
                    searchId: "procedural-package"
                    title: qsTr("Procedural package")
                    keywords: "lua scene generator"
                }
            }

            ThemedComboBox {
                width: parent.width
                model: root.packages.map(p => p.name)
                currentIndex: root.packageIndex
                tooltip: qsTr("The package that builds this clip's scene. Changing it resets the parameters.")
                onActivated: {
                    if (!EditorState.setProceduralPackage(EditorState.selectedTrack, EditorState.selectedClip,
                                                          root.packages[currentIndex].id))
                        EditorState.setLastMessage(qsTr("Could not change the package"), "error")
                }
            }

            ThemedLabel {
                width: parent.width
                opacity: 0.8
                wrapMode: Text.Wrap
                visible: text.length > 0
                text: root.packageDescription
            }
        }

        Column {
            width: parent.width
            spacing: Theme.spacingXs

            ThemedLabel {
                text: qsTr("Seed")
                SearchAnchor {
                    searchId: "procedural-seed"
                    title: qsTr("Procedural seed")
                    keywords: "random"
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
                    searchId: "procedural-params"
                    title: qsTr("Procedural parameters")
                    keywords: "keyframe"
                }
            }

            Repeater {
                model: root.params.length
                delegate: Column {
                    id: paramRow
                    required property int index
                    readonly property var paramData: root.params[index] || ({})
                    width: root.width
                    spacing: 4

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
