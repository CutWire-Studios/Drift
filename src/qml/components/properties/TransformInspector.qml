import QtQuick
import QtQuick.Controls.Basic
import Drift
Item {
    id: root

    property int clipDataRevision: 0
    readonly property var clipData: {
        void clipDataRevision
        return EditorState.selectedClipData
    }
    readonly property bool hasSelection: !!clipData && Object.keys(clipData).length > 0
    readonly property string clipKind: hasSelection ? (clipData.kind || "") : ""
    // A model clip is always a 3D layer: its rect is the model's bounding-box front face.
    readonly property bool isModel3d: clipKind === "model3d"
    readonly property bool isTransformLayer: clipKind === "adjustment"
                                             && clipData.adjustmentKind === "transform"
    // The transform clips over this one, innermost first; the nearest names its frame.
    readonly property var transformParents: hasSelection ? (clipData.transformParents || []) : []
    readonly property string parentName: transformParents.length > 0
                                         ? (transformParents[0].name || qsTr("Transform")) : ""
    readonly property int canvasW: {
        void EditorState.tracksRevision
        return Math.max(1, EditorState.projectFile.projectWidth())
    }
    readonly property int canvasH: {
        void EditorState.tracksRevision
        return Math.max(1, EditorState.projectFile.projectHeight())
    }

    readonly property var propOpacity: { "key": "opacity", "label": qsTr("Opacity"), "def": 1.0, "decimals": 2 }
    readonly property real frameW: hasSelection && clipData.frameW > 0 ? clipData.frameW : canvasW
    readonly property real frameH: hasSelection && clipData.frameH > 0 ? clipData.frameH : canvasH
    readonly property var propX: { "key": "positionX", "label": "X", "def": root.canvasW / 2, "decimals": 0 }
    readonly property var propY: { "key": "positionY", "label": "Y", "def": root.canvasH / 2, "decimals": 0 }
    readonly property var propWidth: { "key": "scaleX", "label": qsTr("Width"), "def": 1.0, "decimals": 0 }
    readonly property var propHeight: { "key": "scaleY", "label": qsTr("Height"), "def": 1.0, "decimals": 0 }
    readonly property var propRotation: ({ "key": "rotation",
                                           "label": (clipData && clipData.layer3d) ? qsTr("Rotation Z") : qsTr("Rotation"),
                                           "def": 0.0, "decimals": 1 })

    // Link X and Y keyframes and motion curves together for synchronized 2D paths
    property bool positionLinked: EditorState.positionLinked
    onPositionLinkedChanged: {
        if (EditorState.positionLinked !== positionLinked)
            EditorState.positionLinked = positionLinked
    }

    // One slider that scales width and height together about the box centre; the link button
    // swaps it for the separate Width/Height rows.
    property bool sizeLinked: EditorState.sizeLinked
    readonly property bool isText: clipKind === "text" || clipKind === "subtitle"

    // 100% is the clip's fit size. With a squished clip the slider shows the X scale and a drag
    // multiplies both axes by the same factor.
    property int liveRevision: 0
    readonly property real scaleAtPlayhead: {
        void clipDataRevision
        void liveRevision
        if (!hasSelection)
            return 1
        return transformAt("scaleX", 1)
    }

    // Scale is presented as a single control, but the project stores its animation as two
    // independent scaleX/scaleY curves. Keep the navigation and add/remove button in sync
    // with either curve, including clips that were keyed while the dimensions were unlinked.
    readonly property var scaleWidthKeys: (clipData && clipData.keyframes && clipData.keyframes.scaleX
                                           && clipData.keyframes.scaleX.points) || []
    readonly property var scaleHeightKeys: (clipData && clipData.keyframes && clipData.keyframes.scaleY
                                            && clipData.keyframes.scaleY.points) || []
    readonly property var scaleKeyTimes: {
        const times = scaleWidthKeys.concat(scaleHeightKeys).map(k => Number(k.seconds)).filter(t => isFinite(t))
        times.sort((a, b) => a - b)
        return times.filter((t, i) => i === 0 || t - times[i - 1] > 1 / 30)
    }
    readonly property bool hasPreviousScaleKey: {
        const t = EditorState.inspectorPlayheadSeconds
        return scaleKeyTimes.some(k => k < t - 1 / 30)
    }
    readonly property bool hasNextScaleKey: {
        const t = EditorState.inspectorPlayheadSeconds
        return scaleKeyTimes.some(k => k > t + 1 / 30)
    }

    function moveToScaleKey(previous) {
        const t = EditorState.playheadSeconds
        const candidates = scaleKeyTimes.filter(k => previous ? k < t - 1 / 30 : k > t + 1 / 30)
        if (candidates.length > 0)
            EditorState.playheadSeconds = previous ? candidates[candidates.length - 1] : candidates[0]
        EditorState.showKeyframeGraphProperty("scaleX")
    }

    property var scaleDragStart: null
    function transformAt(key, def) {
        return EditorState.propertyValueAt(EditorState.selectedTrack, EditorState.selectedClip,
                                           key, EditorState.inspectorPlayheadSeconds, def)
    }

    function keyedAtPlayhead(key) {
        const points = (clipData.keyframes && clipData.keyframes[key] && clipData.keyframes[key].points) || []
        const t = EditorState.inspectorPlayheadSeconds
        return points.some(p => Math.abs(p.seconds - t) <= 1 / 30)
    }

    readonly property bool widthKeyed: keyedAtPlayhead("scaleX")
    readonly property bool heightKeyed: keyedAtPlayhead("scaleY")

    function beginScaleDrag() {
        scaleDragStart = {
            "scaleX": transformAt("scaleX", 1),
            "scaleY": transformAt("scaleY", 1)
        }
    }

    function applyScale(scale) {
        const s = scaleDragStart
        if (!s || s.scaleX === 0)
            return
        EditorState.previewSetClipScale(EditorState.selectedTrack, EditorState.selectedClip,
                                        scale, scale * s.scaleY / s.scaleX)
    }

    height: contentCol.height
    implicitHeight: contentCol.height

    function refreshFields() {}

    Connections {
        target: EditorState
        function onSelectionChanged() { root.clipDataRevision++ }
        function onSelectedClipDataChanged() { root.clipDataRevision++ }
        function onTracksChanged() { root.clipDataRevision++ }
        function onClipPropertiesPreviewed(trackIndex, clipIndex, keys) {
            if (!scaleSlider.pressed && (keys.indexOf("scaleX") >= 0 || keys.indexOf("scaleY") >= 0))
                root.liveRevision++
        }
    }

    Column {
        id: contentCol
        width: root.width
        spacing: Theme.spacingXl

        Column {
            width: root.width
            spacing: 10

            TransformLayerCard {
                visible: root.isTransformLayer
                width: parent.width
                clipData: root.clipData
            }

            Row {
                visible: root.transformParents.length > 0 && !root.isTransformLayer
                width: parent.width
                spacing: Theme.spacingMd

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - selectParentButton.width - parent.spacing
                    wrapMode: Text.WordWrap
                    text: qsTr("Also moved by %1").arg(root.parentName)
                    color: Theme.c.transformInspector.accent
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
                ThemedButton {
                    id: selectParentButton
                    text: qsTr("Select")
                    onClicked: EditorState.selectTransformParent()
                }
            }

            Text {
                visible: !Theme.phone
                width: parent.width
                wrapMode: Text.WordWrap
                text: Theme.touchInput ? qsTr("Move the playhead, set a value, then tap the diamond to keyframe it. Auto keyframes does this as you drag.")
                                     : qsTr("Move to a time, set a value, then click the diamond to add a keyframe. With Auto keyframes on, dragging a slider or the preview also creates them.")
                color: Theme.c.transformInspector.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.touchInput ? Theme.fontSizeSm : Theme.fontSizeXs
            }
            Row {
                width: parent.width
                spacing: Theme.spacingMd
                ThemedChip {
                    id: autoKeyChip
                    text: qsTr("Auto keyframes")
                    selected: EditorState.autoKeyEnabled
                    onClicked: EditorState.autoKeyEnabled = !EditorState.autoKeyEnabled
                }
                Item {
                    visible: Theme.phone
                    width: parent.width - autoKeyChip.width - infoButton.width - parent.spacing * 2
                    height: 1
                }
                AndroidInfoButton {
                    id: infoButton
                    anchors.verticalCenter: parent.verticalCenter
                    hint: qsTr("Move the playhead, set a value, then tap the diamond to keyframe it. Auto keyframes does this as you drag.")
                }
            }
            Item {
                visible: !Theme.phone
                width: parent.width
                height: posLinkButton.height
                SearchAnchor {
                    searchId: "transform-position"
                    title: qsTr("Position")
                    keywords: "move x y"
                }

                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.positionLinked ? qsTr("Position (px, linked)") : qsTr("Position (px)")
                    color: Theme.touchInput ? Theme.c.transformInspector.foreground : Theme.c.transformInspector.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.touchInput ? Theme.fontSizeSm : Theme.fontSizeXs
                    font.weight: Font.Medium
                }

                IconButton {
                    id: posResetButton
                    anchors.right: posLinkButton.left
                    anchors.rightMargin: Theme.touchInput ? 0 : 4
                    anchors.verticalCenter: parent.verticalCenter
                    buttonSize: Theme.touchInput ? Theme.androidMinTouchTarget : 24
                    iconSize: Theme.touchInput ? Theme.iconSizeBase : 14
                    glyph: Theme.icons.reset
                    tooltip: qsTr("Reset position")
                    onClicked: EditorState.resetClipTransformGroup(
                                   EditorState.selectedTrack, EditorState.selectedClip, "position")
                }

                IconButton {
                    id: posLinkButton
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    buttonSize: Theme.touchInput ? Theme.androidMinTouchTarget : 24
                    iconSize: Theme.touchInput ? Theme.iconSizeBase : 14
                    glyph: root.positionLinked ? Theme.icons.linkTwo : Theme.icons.unlink
                    active: root.positionLinked
                    tooltip: root.positionLinked ? qsTr("Separate X and Y keyframes")
                                                 : qsTr("Link X and Y keyframes and motion curves")
                    onClicked: root.positionLinked = !root.positionLinked
                }
            }

            Text {
                visible: root.transformParents.length > 0 && !root.isTransformLayer
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("Measured inside %1's frame").arg(root.parentName)
                color: Theme.c.transformInspector.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            PropertyKeyframeRow {
                width: parent.width
                propDef: root.propX
                keyframeList: (root.clipData.keyframes && root.clipData.keyframes.positionX && root.clipData.keyframes.positionX.points) || []
                linkedPropKey: root.positionLinked ? "positionY" : ""
                useSlider: true
                sliderFrom: -root.canvasW
                sliderTo: root.canvasW * 2
                unit: "px"
                IconButton {
                    glyph: root.positionLinked ? Theme.icons.linkTwo : Theme.icons.unlink
                    active: root.positionLinked
                    iconSize: Theme.iconSizeBase
                    Accessible.name: root.positionLinked ? qsTr("Separate X and Y keyframes")
                                                         : qsTr("Link X and Y keyframes and motion curves")
                    onClicked: root.positionLinked = !root.positionLinked
                }
            }
            PropertyKeyframeRow {
                width: parent.width
                propDef: root.propY
                keyframeList: (root.clipData.keyframes && root.clipData.keyframes.positionY && root.clipData.keyframes.positionY.points) || []
                linkedPropKey: root.positionLinked ? "positionX" : ""
                resetGroup: Theme.phone ? "position" : ""
                useSlider: true
                sliderFrom: -root.canvasH
                sliderTo: root.canvasH * 2
                unit: "px"
            }

            WorkspaceLink {
                readonly property var keys: (root.clipData.keyframes && root.clipData.keyframes.positionX
                                             && root.clipData.keyframes.positionX.points) || []
                active: keys.length >= 2 && !!root.clipData.id
                label: qsTr("Follow an object…")
                target: ({ panel: "tracking", followerId: root.clipData.id })
            }

            Item {
                visible: !(Theme.phone && root.sizeLinked)
                width: parent.width
                height: sizeLinkButton.height
                SearchAnchor {
                    searchId: "transform-scale"
                    title: qsTr("Scale")
                    keywords: "size zoom resize ken burns slow zoom"
                }

                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.sizeLinked ? qsTr("Scale") : qsTr("Size (px)")
                    color: Theme.touchInput ? Theme.c.transformInspector.foreground : Theme.c.transformInspector.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.touchInput ? Theme.fontSizeSm : Theme.fontSizeXs
                    font.weight: Font.Medium
                }

                Text {
                    visible: Theme.touchInput && root.sizeLinked
                    anchors.right: sizeResetButton.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: Math.round((scaleSliderPhone.pressed ? scaleSliderPhone.value : root.scaleAtPlayhead) * 100) + "%"
                    color: Theme.c.transformInspector.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSm
                }

                IconButton {
                    id: sizeResetButton
                    anchors.right: sizeLinkButton.left
                    anchors.rightMargin: Theme.touchInput ? 0 : 4
                    anchors.verticalCenter: parent.verticalCenter
                    buttonSize: Theme.touchInput ? Theme.androidMinTouchTarget : 24
                    iconSize: Theme.touchInput ? Theme.iconSizeBase : 14
                    glyph: Theme.icons.reset
                    tooltip: qsTr("Reset size")
                    onClicked: EditorState.resetClipTransformGroup(
                                   EditorState.selectedTrack, EditorState.selectedClip, "size")
                }

                IconButton {
                    id: sizeLinkButton
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    buttonSize: Theme.touchInput ? Theme.androidMinTouchTarget : 24
                    iconSize: Theme.touchInput ? Theme.iconSizeBase : 14
                    glyph: root.sizeLinked ? Theme.icons.linkTwo : Theme.icons.unlink
                    active: root.sizeLinked
                    tooltip: root.sizeLinked ? qsTr("Edit width and height separately")
                                             : qsTr("Scale width and height together")
                    onClicked: EditorState.sizeLinked = !EditorState.sizeLinked
                }
            }

            InlineTip {
                tipId: "transform.scaleKeyframes"
                shown: !EditorState.autoKeyEnabled
                text: qsTr("To animate Scale, move to a time and change it. Auto keyframes adds the keyframes for you.")
                actionText: qsTr("Turn on Auto keyframes")
                onActionTriggered: EditorState.autoKeyEnabled = true
            }

            Row {
                visible: root.sizeLinked && !Theme.touchInput
                width: parent.width
                spacing: 4

                IconButton {
                    buttonSize: 20
                    iconSize: 12
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: Theme.icons.chevronLeft
                    enabled: root.hasPreviousScaleKey
                    tooltip: qsTr("Previous scale keyframe")
                    onClicked: root.moveToScaleKey(true)
                }

                IconButton {
                    buttonSize: 20
                    iconSize: 12
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: Theme.icons.chevronRight
                    enabled: root.hasNextScaleKey
                    tooltip: qsTr("Next scale keyframe")
                    onClicked: root.moveToScaleKey(false)
                }

                IconButton {
                    buttonSize: 20
                    iconSize: 12
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: Theme.icons.trash
                    enabled: root.scaleKeyTimes.length > 0
                    tooltip: qsTr("Clear all scale keyframes, keeping the current scale")
                    onClicked: EditorState.keyframes.clearProperties(
                                   EditorState.selectedTrack, EditorState.selectedClip,
                                   ["scaleX", "scaleY"])
                }

                ThemedSlider {
                    id: scaleSlider
                    lockWhilePlaying: true
                    label: qsTr("Scale")
                    width: parent.width - scaleReadout.width - scaleKey.width - 20 * 3 - parent.spacing * 5
                    anchors.verticalCenter: parent.verticalCenter
                    from: 0.05
                    to: 4
                    valueFormatter: function (v) { return Math.round(v * 100) + "%" }
                    Binding on value {
                        when: !scaleSlider.pressed
                        value: root.scaleAtPlayhead
                    }
                    onPressedChanged: {
                        if (pressed) {
                            root.beginScaleDrag()
                            EditorState.beginPreviewDrag(qsTr("Scale clip"))
                        } else {
                            EditorState.commitPreviewDrag()
                        }
                    }
                    onMoved: root.applyScale(value)
                }

                Text {
                    id: scaleReadout
                    width: 48
                    anchors.verticalCenter: parent.verticalCenter
                    horizontalAlignment: Text.AlignRight
                    text: Math.round((scaleSlider.pressed ? scaleSlider.value : root.scaleAtPlayhead) * 100) + "%"
                    color: Theme.c.transformInspector.foreground
                    font.family: Theme.monoFontFamily
                    font.pixelSize: Theme.fontSizeSm
                }

                IconButton {
                    id: scaleKey
                    anchors.verticalCenter: parent.verticalCenter
                    buttonSize: 24
                    iconSize: 16
                    // Half-ghosted when only one of the two axes has a key here.
                    opacity: root.widthKeyed !== root.heightKeyed ? 0.5 : 1
                    glyph: root.widthKeyed && root.heightKeyed ? Theme.icons.diamondMinus : Theme.icons.diamondPlus
                    active: root.widthKeyed && root.heightKeyed
                    tooltip: active ? qsTr("Remove scale keyframe") : qsTr("Add scale keyframe")
                    onClicked: {
                        if (active) {
                            EditorState.keyframes.removeKeysAt(EditorState.selectedTrack, EditorState.selectedClip,
                                                               ["scaleX", "scaleY"], EditorState.inspectorPlayheadSeconds)
                        } else {
                            EditorState.keyframes.addKeysAt(EditorState.selectedTrack, EditorState.selectedClip,
                                                            ["scaleX", "scaleY"], EditorState.inspectorPlayheadSeconds)
                        }
                    }
                }
            }

            Row {
                visible: root.sizeLinked && Theme.touchInput
                width: parent.width
                spacing: 0

                IconButton {
                    visible: root.scaleKeyTimes.length > 0
                    buttonSize: Theme.androidMinTouchTarget - Theme.spacingMd
                    iconSize: Theme.iconSizeBase
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: Theme.icons.chevronLeft
                    enabled: root.hasPreviousScaleKey
                    onClicked: root.moveToScaleKey(true)
                    Accessible.name: qsTr("Previous scale keyframe")
                }

                IconButton {
                    buttonSize: Theme.androidMinTouchTarget
                    iconSize: Theme.iconSizeLg
                    anchors.verticalCenter: parent.verticalCenter
                    opacity: root.widthKeyed !== root.heightKeyed ? 0.5 : 1
                    glyph: root.widthKeyed && root.heightKeyed ? Theme.icons.diamondMinus : Theme.icons.diamondPlus
                    active: root.widthKeyed && root.heightKeyed
                    Accessible.name: active ? qsTr("Remove scale keyframe") : qsTr("Add scale keyframe")
                    onClicked: {
                        if (active)
                            EditorState.keyframes.removeKeysAt(EditorState.selectedTrack, EditorState.selectedClip,
                                                               ["scaleX", "scaleY"], EditorState.inspectorPlayheadSeconds)
                        else
                            EditorState.keyframes.addKeysAt(EditorState.selectedTrack, EditorState.selectedClip,
                                                            ["scaleX", "scaleY"], EditorState.inspectorPlayheadSeconds)
                    }
                }

                IconButton {
                    visible: root.scaleKeyTimes.length > 0
                    buttonSize: Theme.androidMinTouchTarget - Theme.spacingMd
                    iconSize: Theme.iconSizeBase
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: Theme.icons.chevronRight
                    enabled: root.hasNextScaleKey
                    onClicked: root.moveToScaleKey(false)
                    Accessible.name: qsTr("Next scale keyframe")
                }

                Text {
                    visible: Theme.phone
                    width: visible ? Theme.phoneLabelWidth + 12 : 0
                    anchors.verticalCenter: parent.verticalCenter
                    leftPadding: 6
                    text: qsTr("Scale")
                    color: Theme.c.transformInspector.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSm
                }
                ThemedSlider {
                    id: scaleSliderPhone
                    lockWhilePlaying: true
                    label: qsTr("Scale")
                    width: parent.width - x - (clearScaleKeys.visible ? clearScaleKeys.width : 0)
                           - (Theme.phone ? phoneScaleTail.width : 0)
                    height: Theme.phone ? Theme.androidControlHeight : Theme.androidRowHeight
                    anchors.verticalCenter: parent.verticalCenter
                    from: 0.05
                    to: 4
                    valueFormatter: function (v) { return Math.round(v * 100) + "%" }
                    Binding on value {
                        when: !scaleSliderPhone.pressed
                        value: root.scaleAtPlayhead
                    }
                    onPressedChanged: {
                        if (pressed) {
                            root.beginScaleDrag()
                            EditorState.beginPreviewDrag(qsTr("Scale clip"))
                        } else {
                            EditorState.commitPreviewDrag()
                        }
                    }
                    onMoved: root.applyScale(value)
                }

                Row {
                    id: phoneScaleTail
                    visible: Theme.phone
                    width: visible ? implicitWidth : 0
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 48
                        horizontalAlignment: Text.AlignRight
                        rightPadding: Theme.spacingSm
                        text: Math.round((scaleSliderPhone.pressed ? scaleSliderPhone.value : root.scaleAtPlayhead) * 100) + "%"
                        color: Theme.c.transformInspector.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSm
                    }
                    IconButton {
                        anchors.verticalCenter: parent.verticalCenter
                        glyph: Theme.icons.reset
                        iconSize: Theme.iconSizeBase
                        Accessible.name: qsTr("Reset size")
                        onClicked: EditorState.resetClipTransformGroup(
                                       EditorState.selectedTrack, EditorState.selectedClip, "size")
                    }
                    IconButton {
                        anchors.verticalCenter: parent.verticalCenter
                        glyph: Theme.icons.linkTwo
                        active: true
                        iconSize: Theme.iconSizeBase
                        Accessible.name: qsTr("Edit width and height separately")
                        onClicked: EditorState.sizeLinked = false
                    }
                }
                IconButton {
                    id: clearScaleKeys
                    visible: root.scaleKeyTimes.length > 0
                    buttonSize: Theme.androidMinTouchTarget - Theme.spacingMd
                    iconSize: Theme.iconSizeBase
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: Theme.icons.trash
                    Accessible.name: qsTr("Clear all scale keyframes, keeping the current scale")
                    onClicked: EditorState.keyframes.clearProperties(
                                   EditorState.selectedTrack, EditorState.selectedClip,
                                   ["scaleX", "scaleY"])
                }
            }

            PropertyKeyframeRow {
                visible: !root.sizeLinked
                width: parent.width
                propDef: root.propWidth
                keyframeList: (root.clipData.keyframes && root.clipData.keyframes.scaleX && root.clipData.keyframes.scaleX.points) || []
                useSlider: true
                displayFactor: root.frameW
                sliderFrom: 1 / root.frameW
                sliderTo: Math.max(root.canvasW * 2, 2) / root.frameW
                unit: "px"
            }
            PropertyKeyframeRow {
                visible: !root.sizeLinked
                width: parent.width
                propDef: root.propHeight
                keyframeList: (root.clipData.keyframes && root.clipData.keyframes.scaleY && root.clipData.keyframes.scaleY.points) || []
                useSlider: true
                displayFactor: root.frameH
                sliderFrom: 1 / root.frameH
                sliderTo: Math.max(root.canvasH * 2, 2) / root.frameH
                unit: "px"
            }

            Item {
                visible: !Theme.touchInput
                width: parent.width
                height: rotationResetButton.height

                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Opacity & rotation")
                    color: Theme.touchInput ? Theme.c.transformInspector.foreground : Theme.c.transformInspector.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.touchInput ? Theme.fontSizeSm : Theme.fontSizeXs
                    font.weight: Font.Medium
                }

                IconButton {
                    id: opacityResetButton
                    anchors.right: rotationResetButton.left
                    anchors.rightMargin: Theme.touchInput ? 0 : 4
                    anchors.verticalCenter: parent.verticalCenter
                    buttonSize: Theme.touchInput ? Theme.androidMinTouchTarget : 24
                    iconSize: Theme.touchInput ? Theme.iconSizeBase : 14
                    glyph: Theme.icons.reset
                    tooltip: qsTr("Reset opacity")
                    onClicked: EditorState.resetClipTransformGroup(
                                   EditorState.selectedTrack, EditorState.selectedClip, "opacity")
                }

                IconButton {
                    id: rotationResetButton
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.touchInput ? 0 : 4
                    anchors.verticalCenter: parent.verticalCenter
                    buttonSize: Theme.touchInput ? Theme.androidMinTouchTarget : 24
                    iconSize: Theme.touchInput ? Theme.iconSizeBase : 14
                    glyph: Theme.icons.reset
                    tooltip: qsTr("Reset rotation")
                    onClicked: EditorState.resetClipTransformGroup(
                                   EditorState.selectedTrack, EditorState.selectedClip, "rotation")
                }
            }

            SearchAnchor {
                searchId: "transform-opacity"
                title: qsTr("Opacity")
                keywords: "transparency alpha fade"
                target: transformOpacityRow
            }
            PropertyKeyframeRow {
                id: transformOpacityRow
                width: parent.width
                propDef: root.propOpacity
                resetGroup: "opacity"
                keyframeList: (root.clipData.keyframes && root.clipData.keyframes.opacity && root.clipData.keyframes.opacity.points) || []
                useSlider: true
                sliderFrom: 0
                sliderTo: 1
                percent: true
            }

            SearchAnchor {
                searchId: "transform-rotation"
                title: qsTr("Rotation")
                keywords: "angle rotate 90 rotation z"
                target: transformRotationRow
            }
            PropertyKeyframeRow {
                id: transformRotationRow
                width: parent.width
                propDef: root.propRotation
                resetGroup: "rotation"
                keyframeList: (root.clipData.keyframes && root.clipData.keyframes.rotation && root.clipData.keyframes.rotation.points) || []
                useSlider: true
                sliderFrom: -180
                sliderTo: 180
                unit: "°"
            }

            Text {
                text: qsTr("Rotate 90°")
                color: Theme.touchInput ? Theme.c.transformInspector.foreground : Theme.c.transformInspector.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.touchInput ? Theme.fontSizeSm : Theme.fontSizeXs
                font.weight: Font.Medium
            }

            Flow {
                width: parent.width
                spacing: 6
                Repeater {
                    // Plain ints avoid JS-object model role quirks (e.g. "value").
                    model: [0, 90, 180, -90]
                    delegate: ThemedChip {
                        required property int modelData
                        text: modelData + "°"
                        selected: {
                            const cur = EditorState.selectedClipRotationAtPlayhead
                            return Math.abs(cur - modelData) < 0.5
                        }
                        onClicked: EditorState.setClipRotationSnap(
                                       EditorState.selectedTrack, EditorState.selectedClip,
                                       modelData)
                    }
                }
            }

            ThemedSwitch {
                visible: !root.isModel3d
                text: qsTr("3D layer")
                tooltip: root.isTransformLayer
                         ? qsTr("Rotate the whole group as one flat card")
                         : qsTr("Rotate the clip in 3D and move it in depth, with 3D grips on the preview. Turning it off flattens the clip again.")
                // A Binding rather than a plain one: the click itself assigns `checked`, which
                // would otherwise sever it and leave the switch stale on the next clip.
                Binding on checked {
                    value: {
                        void root.clipDataRevision
                        return !!root.clipData.layer3d
                    }
                }
                onToggled: EditorState.setClipLayer3d(EditorState.selectedTrack,
                                                      EditorState.selectedClip, checked)
            }

            WorkspaceLink {
                active: !!root.clipData.layer3d || root.isModel3d
                label: qsTr("Open 3D workspace ↗")
                target: ({ panel: "viewport3d" })
            }

            Text {
                visible: root.clipKind === "video"
                text: qsTr("Fix orientation")
                SearchAnchor {
                    searchId: "transform-fix-orientation"
                    title: qsTr("Fix orientation")
                    keywords: "rotate sideways"
                }
                color: Theme.touchInput ? Theme.c.transformInspector.foreground : Theme.c.transformInspector.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.touchInput ? Theme.fontSizeSm : Theme.fontSizeXs
                font.weight: Font.Medium
            }

            Text {
                visible: root.clipKind === "video"
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("Corrects the source's own rotation losslessly — unlike Rotation above, this changes decoding, not just the on-screen box.")
                color: Theme.c.transformInspector.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            Flow {
                visible: root.clipKind === "video"
                width: parent.width
                spacing: 6
                Repeater {
                    model: [0, 90, 180, 270]
                    delegate: ThemedChip {
                        required property int modelData
                        text: modelData + "°"
                        selected: {
                            void root.clipDataRevision
                            return Number(root.clipData.orientation) === modelData
                        }
                        onClicked: EditorState.setClipOrientation(
                                       EditorState.selectedTrack, EditorState.selectedClip,
                                       modelData)
                    }
                }
            }

            Text {
                text: qsTr("Flip")
                SearchAnchor {
                    searchId: "transform-flip"
                    title: qsTr("Flip")
                    keywords: "mirror horizontal vertical"
                }
                color: Theme.touchInput ? Theme.c.transformInspector.foreground : Theme.c.transformInspector.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.touchInput ? Theme.fontSizeSm : Theme.fontSizeXs
                font.weight: Font.Medium
            }

            Flow {
                width: parent.width
                spacing: 6
                ThemedChip {
                    text: qsTr("Flip H")
                    selected: {
                        void root.clipDataRevision
                        return !!root.clipData.flipH
                    }
                    onClicked: EditorState.setClipFlip(
                                   EditorState.selectedTrack, EditorState.selectedClip,
                                   !root.clipData.flipH, !!root.clipData.flipV)
                }
                ThemedChip {
                    text: qsTr("Flip V")
                    selected: {
                        void root.clipDataRevision
                        return !!root.clipData.flipV
                    }
                    onClicked: EditorState.setClipFlip(
                                   EditorState.selectedTrack, EditorState.selectedClip,
                                   !!root.clipData.flipH, !root.clipData.flipV)
                }
            }

            ShapeCropSection {
                visible: root.clipKind === "video" || root.clipKind === "image"
                width: parent.width
            }

            SearchAnchor {
                searchId: "transform-reset-size"
                title: qsTr("Reset all")
                keywords: "fit position size transform opacity rotation flip"
                target: transformResetButton
            }
            ThemedButton {
                id: transformResetButton
                text: qsTr("Reset all")
                onClicked: EditorState.resetClipTransform(
                               EditorState.selectedTrack, EditorState.selectedClip)
            }
        }
    }
}
