import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
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
    readonly property bool maskable: clipKind !== "" && clipKind !== "audio"
                                     && clipKind !== "text" && clipKind !== "subtitle"
    readonly property var masks: (clipData && clipData.masks) || []

    // Which stack row is expanded. Kept here rather than per-delegate so opening one closes
    // the others — a column of six sliders per entry would otherwise bury the list.
    property int expandedIndex: 0

    height: contentCol.height
    implicitHeight: contentCol.height

    function refreshFields() {}

    function writeMask(index, changes) {
        const mask = Object.assign({}, root.masks[index] || {}, changes)
        EditorState.setClipMaskAt(EditorState.selectedTrack, EditorState.selectedClip, index, mask)
    }

    function maskLabel(mask, index) {
        if (mask.name)
            return mask.name
        if (mask.shape === "matte")
            return qsTr("Cutout %1").arg(index + 1)
        const shapeLabels = {
            "rectangle": qsTr("Rectangle"), "ellipse": qsTr("Ellipse"), "star": qsTr("Star"),
            "heart": qsTr("Heart"), "bars": qsTr("Bars"), "freeform": qsTr("Polygon")
        }
        return shapeLabels[mask.shape] || mask.shape
    }

    Connections {
        target: EditorState
        function onSelectionChanged() { root.clipDataRevision++; root.expandedIndex = 0 }
        function onSelectedClipDataChanged() { root.clipDataRevision++ }
        function onTracksChanged() { root.clipDataRevision++ }
    }

    Column {
        id: contentCol
        width: root.width
        spacing: Theme.spacingXl

        EmptyState {
            visible: root.clipKind === "audio" || root.clipKind === "text"
                     || root.clipKind === "subtitle"
            width: parent.width
            compact: true
            glyph: Theme.icons.mask
            title: qsTr("Not available")
            hint: qsTr("Cutouts apply to visual clips.")
        }

        // Segmentation produces a matte — a per-frame mask — so it belongs beside
        // the parametric shapes rather than in a tab of its own. It needs a
        // prompting surface, so it opens a window instead of running from here.
        Column {
            id: segmentSection
            visible: root.clipKind === "video"
            width: parent.width
            spacing: Theme.spacingSm

            // The model is an addon, but it can equally come from a bundled
            // models/sam2 or DRIFT_SAM2_MODEL_DIR, so ask the engine rather than
            // the addon registry. That answer is not a binding, hence the reset
            // below when an addon of this kind appears.
            property bool segmentReady: EditorState.segmentationAvailable()
            property bool runtimeReady: Addons.runtimeAvailable()

            Connections {
                target: Addons
                function onKindChanged(kind) {
                    if (kind === "sam2-model")
                        segmentSection.segmentReady = EditorState.segmentationAvailable()
                    else if (kind === "onnxruntime")
                        segmentSection.runtimeReady = Addons.runtimeAvailable()
                }
            }

            Text {
                width: parent.width
                text: qsTr("Subject")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            ThemedButton {
                visible: segmentSection.segmentReady && segmentSection.runtimeReady
                width: parent.width
                text: qsTr("Cut out subject…")
                enabled: !EditorState.segmenting
                onClicked: {
                    const data = EditorState.selectedClipData
                    root.Window.window.openSegmentation(
                        EditorState.selectedTrack, EditorState.selectedClip,
                        data.start !== undefined ? data.start : 0,
                        data.duration !== undefined ? data.duration : 0)
                }
            }

            ThemedButton {
                visible: !segmentSection.segmentReady || !segmentSection.runtimeReady
                width: parent.width
                text: segmentSection.runtimeReady
                      ? qsTr("Download cutout AI (about 190 MB)")
                      : qsTr("Install AI engine first")
                variant: "primary"
                onClicked: root.Window.window.openAddonManager(
                    segmentSection.runtimeReady ? "sam2-model" : "onnxruntime")
            }
        }

        Column {
            width: parent.width
            spacing: Theme.spacingSm
            visible: root.maskable

            Text {
                width: parent.width
                text: qsTr("Cutout stack")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("Entries stack top to bottom. The first one sets the visible area; "
                           + "the rest add to it, cut into it, or trim it.")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                opacity: 0.8
            }

            EmptyState {
                visible: root.masks.length === 0
                width: parent.width
                compact: true
                glyph: Theme.icons.mask
                title: qsTr("No cutouts")
                hint: qsTr("Add a shape to hide everything outside it.")
            }
        }

        Column {
            width: parent.width
            spacing: Theme.spacingSm
            visible: root.maskable

            Repeater {
                model: root.masks

                delegate: Column {
                    id: maskRow
                    required property var modelData
                    required property int index

                    readonly property bool expanded: root.expandedIndex === index
                    readonly property bool isMatte: modelData.shape === "matte"

                    width: parent.width
                    spacing: Theme.spacingSm

                    Rectangle {
                        width: parent.width
                        height: header.height + Theme.spacingSm * 2
                        radius: Theme.radiusSm
                        color: maskRow.expanded ? Theme.panelMuted : "transparent"
                        border.width: 1
                        border.color: maskRow.expanded ? Theme.panelBorder : "transparent"

                        MouseArea {
                            anchors.fill: parent
                            onClicked: root.expandedIndex = maskRow.expanded ? -1 : maskRow.index
                        }

                        Row {
                            id: header
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.leftMargin: Theme.spacingSm
                            anchors.rightMargin: Theme.spacingSm
                            spacing: Theme.spacingSm

                            IconGlyph {
                                anchors.verticalCenter: parent.verticalCenter
                                glyph: maskRow.modelData.enabled ? Theme.icons.eye : Theme.icons.eyeOff
                                iconColor: maskRow.modelData.enabled ? Theme.foreground
                                                                     : Theme.mutedForeground
                                MouseArea {
                                    anchors.fill: parent
                                    anchors.margins: -4
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.writeMask(maskRow.index,
                                                              { "enabled": !maskRow.modelData.enabled })
                                }
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width - opBox.width - removeGlyph.width
                                       - Theme.spacingSm * 4 - Theme.spacingLg
                                elide: Text.ElideRight
                                text: root.maskLabel(maskRow.modelData, maskRow.index)
                                color: maskRow.modelData.enabled ? Theme.foreground : Theme.mutedForeground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeSm
                            }

                            // The first entry seeds the coverage, so its op has nothing to
                            // combine with and would only mislead.
                            ThemedComboBox {
                                id: opBox
                                anchors.verticalCenter: parent.verticalCenter
                                width: Theme.spacing2xl * 3
                                visible: maskRow.index > 0
                                model: ["add", "subtract", "intersect"]
                                readonly property var labels: ({
                                    "add": qsTr("Add"),
                                    "subtract": qsTr("Subtract"),
                                    "intersect": qsTr("Intersect")
                                })
                                displayText: labels[model[currentIndex]] || model[currentIndex]
                                tooltip: qsTr("How this cutout combines with the ones above it")
                                currentIndex: Math.max(0, model.indexOf(maskRow.modelData.op || "add"))
                                onActivated: root.writeMask(maskRow.index, { "op": model[currentIndex] })
                            }

                            IconGlyph {
                                id: removeGlyph
                                anchors.verticalCenter: parent.verticalCenter
                                glyph: Theme.icons.trash
                                iconColor: Theme.mutedForeground
                                MouseArea {
                                    anchors.fill: parent
                                    anchors.margins: -4
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: EditorState.removeClipMask(
                                        EditorState.selectedTrack, EditorState.selectedClip,
                                        maskRow.index)
                                }
                            }
                        }
                    }

                    Column {
                        width: parent.width
                        spacing: Theme.spacingSm
                        visible: maskRow.expanded

                        Repeater {
                            model: [
                                { key: "x", label: qsTr("Center X"), min: 0, max: 1 },
                                { key: "y", label: qsTr("Center Y"), min: 0, max: 1 },
                                { key: "w", label: qsTr("Width"), min: 0.05, max: 1 },
                                { key: "h", label: qsTr("Height"), min: 0.05, max: 1 },
                                { key: "rotation", label: qsTr("Rotation"), min: -180, max: 180 },
                                { key: "feather", label: qsTr("Feather"), min: 0, max: 64 }
                            ]
                            delegate: Column {
                                required property var modelData
                                width: parent.width
                                spacing: 4
                                // A matte's coverage comes from its video frames, so only
                                // feather and invert mean anything for it. Bars spans the
                                // frame, so rotating it only crops.
                                visible: (!maskRow.isMatte || modelData.key === "feather")
                                         && (modelData.key !== "rotation"
                                             || maskRow.modelData.shape !== "bars")

                                Text {
                                    text: modelData.label
                                    color: Theme.mutedForeground
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSizeXs
                                }
                                ThemedSlider {
                                    id: maskParamSlider
                                    label: modelData.label
                                    width: parent.width
                                    from: modelData.min
                                    to: modelData.max
                                    stepSize: modelData.key === "feather" ? 1 : 0.01
                                    Binding on value {
                                        when: !maskParamSlider.pressed
                                        value: maskRow.modelData[modelData.key] || 0
                                    }
                                    onMoved: {
                                        const mask = Object.assign({}, maskRow.modelData)
                                        mask[modelData.key] = value
                                        EditorState.previewSetClipMaskAt(
                                            EditorState.selectedTrack, EditorState.selectedClip,
                                            maskRow.index, mask)
                                    }
                                    onPressedChanged: {
                                        if (pressed)
                                            EditorState.beginPreviewDrag(qsTr("Mask changed"))
                                        else
                                            EditorState.commitPreviewDrag()
                                    }
                                }
                            }
                        }

                        Row {
                            width: parent.width
                            spacing: 8
                            Text {
                                text: qsTr("Invert")
                                color: Theme.mutedForeground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeXs
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            ThemedSwitch {
                                checked: !!maskRow.modelData.invert
                                onToggled: root.writeMask(maskRow.index, { "invert": checked })
                            }
                        }
                    }
                }
            }
        }

        Column {
            width: parent.width
            spacing: Theme.spacingSm
            visible: root.maskable

            Text {
                width: parent.width
                text: qsTr("Add cutout")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            Flow {
                width: parent.width
                spacing: Theme.spacingSm

                Repeater {
                    model: [
                        { shape: "rectangle", label: qsTr("Rectangle") },
                        { shape: "ellipse", label: qsTr("Ellipse") },
                        { shape: "star", label: qsTr("Star") },
                        { shape: "heart", label: qsTr("Heart") },
                        { shape: "bars", label: qsTr("Bars") },
                        { shape: "freeform", label: qsTr("Polygon") }
                    ]
                    delegate: ThemedButton {
                        required property var modelData
                        text: modelData.label
                        onClicked: {
                            const added = EditorState.addClipMask(
                                EditorState.selectedTrack, EditorState.selectedClip,
                                modelData.shape)
                            if (added >= 0)
                                root.expandedIndex = added
                        }
                    }
                }
            }
        }
    }
}
