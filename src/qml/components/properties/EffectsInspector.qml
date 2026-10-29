import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
Item {
    id: root

    // Raised by the empty state; Main wires it to the assets panel so
    // "Browse effects" actually takes the user somewhere.
    signal browseEffectsRequested()

    // The name prompt lives in PropertiesPanel, so the inspector only says which stack the
    // user asked to save. -1 means the whole clip rather than one effect.
    signal saveEffectPresetRequested(int effectIndex)

    property int clipDataRevision: 0
    readonly property var clipData: {
        void clipDataRevision
        return EditorState.selectedClipData
    }
    readonly property bool hasSelection: !!clipData && Object.keys(clipData).length > 0
    readonly property string clipKind: hasSelection ? (clipData.kind || "") : ""
    // Depend on clipDataRevision the same way clipData does: the Repeater is keyed on
    // selectedEffects.length, so a same-length param edit would otherwise leave stale delegates.
    readonly property var selectedEffects: {
        void clipDataRevision
        return EditorState.selectedClipEffects
    }

    height: contentCol.height
    implicitHeight: contentCol.height

    function refreshFields() {}

    // An eyedropper aimed at the preview would otherwise sample this effect's own output, which
    // for a chroma key is the very colour already keyed out. Undone by cancelPreviewDrag.
    function bypassForEyedropper(effectIndex) {
        EditorState.beginPreviewDrag()
        EditorState.previewSetEffectEnabled(EditorState.selectedTrack, EditorState.selectedClip,
                                            effectIndex, false)
    }

    // Hue params (effectToMap's `hue` flag) are degrees on the keyframe stack but are picked as a
    // colour. Only the hue survives the round trip: saturation and brightness are the shader's
    // business (chroma key's Tolerance), so the swatch always shows the pure, fully saturated hue.
    function hueToHex(hue) {
        return Qt.hsva((((hue % 360) + 360) % 360) / 360, 1, 1, 1).toString()
    }

    // Returns NaN for a grey, which has no hue to key on — the caller keeps the current value
    // rather than snapping the key to red.
    function hexToHue(hex) {
        // Qt.lighter(…, 1) is just string -> color; Qt.color() needs a newer Qt than we require.
        const c = Qt.lighter(hex, 1)
        if (c.hsvHue < 0)
            return NaN
        return c.hsvHue * 360
    }

    Connections {
        target: EditorState
        function onSelectionChanged() { root.clipDataRevision++ }
        function onSelectedClipDataChanged() { root.clipDataRevision++ }
        function onTracksChanged() { root.clipDataRevision++ }
    }

    Column {
        id: contentCol
        width: root.width
        spacing: Theme.spacingXl
        EffectsFaceSection {
            width: parent.width
            clipDataRevision: root.clipDataRevision
        }

        EffectsDepthSection {
            width: parent.width
            clipDataRevision: root.clipDataRevision
        }

        EffectsAutoKeyHint {
            width: parent.width
            inspector: root
        }

        // Has a CTA now: the copy told the user to go to the
        // Effects library but gave them no way to get there.
        EmptyState {
            width: parent.width
            visible: root.selectedEffects.length === 0
            glyph: Theme.icons.wand
            title: qsTr("No effects yet")
            hint: qsTr("Drag a preset from the Effects library onto this clip, or click a preset card.")
            actionText: qsTr("Browse effects")
            onActionTriggered: root.browseEffectsRequested()
        }

        // Integer models keep delegates alive across preview ticks that rebuild
        // selectedClipEffects as a fresh QVariantList (same as AudioEffectsInspector).
        SearchAnchor {
            searchId: "effects-settings"
            title: qsTr("Effect settings")
            keywords: "parameters"
        }
        Repeater {
            model: root.selectedEffects.length
            delegate: Column {
                id: effectCard
                required property int index
                readonly property var effectData: root.selectedEffects[index] || ({})
                readonly property var effectParams: effectData.params || []
                readonly property bool effectEnabled: effectData.enabled !== false
                width: root.width
                spacing: 6

                // Which parameter groups are unfolded. Only the first group starts open, so a
                // package with several lights shows one and keeps the rest a click away. A group
                // declared "groupCollapsed" starts folded even when it comes first.
                property var openGroups: ({})
                readonly property string firstGroup: {
                    for (let i = 0; i < effectParams.length; i++) {
                        if (effectParams[i].group)
                            return effectParams[i].group
                    }
                    return ""
                }
                function isGroupOpen(group) {
                    if (group === "")
                        return true
                    const state = openGroups[group]
                    if (state !== undefined)
                        return state
                    for (let i = 0; i < effectParams.length; i++) {
                        if (effectParams[i].group === group && effectParams[i].groupCollapsed)
                            return false
                    }
                    return group === firstGroup
                }
                function toggleGroup(group) {
                    const next = Object.assign({}, openGroups)
                    next[group] = !isGroupOpen(group)
                    openGroups = next
                }

                EffectsEffectHeader {
                    card: effectCard
                    inspector: root
                }

                Loader {
                    active: effectCard.effectData.catalogId === "face_props"
                    visible: active
                    width: parent.width
                    opacity: effectCard.effectEnabled ? 1 : 0.45
                    sourceComponent: FacePropPicker {
                        effectIndex: effectCard.index
                        effectParams: effectCard.effectParams
                    }
                }

                Column {
                    width: parent.width
                    spacing: 6
                    opacity: effectCard.effectEnabled ? 1 : 0.45

                    Repeater {
                        model: effectCard.effectParams.length
                        delegate: Column {
                            id: paramRow
                            required property int index
                            readonly property var paramData: effectCard.effectParams[index] || ({})
                            width: root.width
                            spacing: 4
                            // Out of the layout entirely when folded away, or the outer column
                            // would still space out every hidden row.
                            visible: (group === "" || groupStart || groupOpen)
                                     && !(paramData.key === "model"
                                          && effectCard.effectData.catalogId === "face_props")
                            // Folding for packages that declare "group" on their parameters (the
                            // relight effect's four lights): the group's first row carries the
                            // header, and every row of a closed group collapses to nothing.
                            readonly property string group: paramData.group || ""
                            readonly property bool groupStart: group !== ""
                                && (index === 0
                                    || ((effectCard.effectParams[index - 1] || {}).group || "") !== group)
                            readonly property bool groupOpen: effectCard.isGroupOpen(group)

                            EffectsParamGroupHeader {
                                visible: paramRow.groupStart
                                groupOpen: paramRow.groupOpen
                                groupTitle: paramRow.group
                                onToggled: effectCard.toggleGroup(paramRow.group)
                            }

                            Column {
                                width: parent.width
                                spacing: 4
                                visible: paramRow.group === "" || paramRow.groupOpen

                                EffectsBoolParam {
                                    row: paramRow
                                    effectIndex: effectCard.index
                                }

                                EffectsColorParam {
                                    row: paramRow
                                    effectIndex: effectCard.index
                                    inspector: root
                                }

                                EffectsClipParam {
                                    row: paramRow
                                    effectIndex: effectCard.index
                                    inspector: root
                                }

                                EffectsFileParam {
                                    row: paramRow
                                    effectIndex: effectCard.index
                                }

                                EffectsCurveParam {
                                    row: paramRow
                                    effectIndex: effectCard.index
                                }

                                EffectsLookParam {
                                    row: paramRow
                                    effectIndex: effectCard.index
                                }

                                Vec2Param {
                                    visible: paramRow.paramData.type === "vec2"
                                    width: parent.width
                                    paramData: paramRow.paramData
                                    effectIndex: effectCard.index
                                }

                                EffectsEnumParam {
                                    row: paramRow
                                    effectIndex: effectCard.index
                                }

                                EffectsHueParam {
                                    row: paramRow
                                    effectIndex: effectCard.index
                                    inspector: root
                                }

                                EffectsFaceIndexParam {
                                    row: paramRow
                                    effectIndex: effectCard.index
                                    inspector: root
                                }

                                PropertyKeyframeRow {
                                    visible: paramRow.paramData.type === "int"
                                              || (paramRow.paramData.type === "float"
                                                  && paramRow.paramData.key !== "faceIndex")
                                    width: parent.width
                                    // `def` is the param's static value, which the row falls
                                    // back to whenever the track holds no keys.
                                    propDef: ({
                                        // Colours and files carry no prop; the row is hidden for them
                                        // but still built, and its graph colour hashes the key.
                                        key: paramRow.paramData.prop || "",
                                        label: paramRow.paramData.label,
                                        def: paramRow.paramData.value,
                                        decimals: paramRow.paramData.type === "int" ? 0
                                                  : (paramRow.paramData.step || 0) >= 1 ? 0
                                                  : Math.abs(paramRow.paramData.max
                                                             - paramRow.paramData.min) >= 10 ? 1 : 2
                                    })
                                    keyframeList: (paramRow.paramData.keyframes
                                                   && paramRow.paramData.keyframes.points) || []
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

                EffectsOccludeSection {
                    card: effectCard
                    inspector: root
                }
            }
        }

        EffectsStackActions {
            width: parent.width
            inspector: root
        }

    }
}
