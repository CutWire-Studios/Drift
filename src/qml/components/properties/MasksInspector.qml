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

    height: contentCol.height
    implicitHeight: contentCol.height

    function refreshFields() {}

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

        // The tab used to open with a lone unlabelled combo box
        // and no explanation of what a mask does.
        Text {
            visible: maskShapeBox.visible
            width: parent.width
            text: qsTr("Cutout shape")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        Text {
            visible: maskShapeBox.visible
            width: parent.width
            wrapMode: Text.WordWrap
            text: qsTr("Coming soon — shape masks are still under development.")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            opacity: 0.8
        }

        ThemedComboBox {
            id: maskShapeBox
            visible: root.clipKind !== "audio" && root.clipKind !== "text"
                     && root.clipKind !== "subtitle"
            width: parent.width
            // Shape masks are unfinished — keep the control in the layout so
            // the Cutouts tab still shows what is coming, but do not let it open.
            enabled: false
            model: ["none", "rectangle", "ellipse", "star", "heart", "bars", "freeform"]
            // Human labels — the raw ids were shown to the user.
            readonly property var labels: ({
                "none": qsTr("None"),
                "rectangle": qsTr("Rectangle"),
                "ellipse": qsTr("Ellipse"),
                "star": qsTr("Star"),
                "heart": qsTr("Heart"),
                "bars": qsTr("Bars"),
                "freeform": qsTr("Freeform")
            })
            displayText: labels[model[currentIndex]] || model[currentIndex]
            tooltip: qsTr("Shape masks are still under development")
            currentIndex: Math.max(0, model.indexOf((root.clipData.mask && root.clipData.mask.shape) || "none"))
            onActivated: {
                const mask = Object.assign({}, root.clipData.mask || {})
                mask.shape = model[currentIndex]
                EditorState.setClipMask(EditorState.selectedTrack, EditorState.selectedClip, mask)
            }
        }

        // Clearing a mask previously required knowing to reselect
        // "none" in the combo above.
        ThemedButton {
            visible: maskShapeBox.visible
                     && ((root.clipData.mask && root.clipData.mask.shape) || "none") !== "none"
            text: qsTr("Remove cutout")
            variant: "destructive"
            glyph: Theme.icons.trash
            onClicked: {
                const mask = Object.assign({}, root.clipData.mask || {})
                mask.shape = "none"
                EditorState.setClipMask(EditorState.selectedTrack, EditorState.selectedClip, mask)
            }
        }

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
                visible: root.clipKind !== "audio" && root.clipKind !== "text"
                     && root.clipKind !== "subtitle"
                         && !!root.clipData.mask && root.clipData.mask.shape !== "none"
                         && (modelData.key !== "rotation" || root.clipData.mask.shape !== "bars")

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
                        value: (root.clipData.mask && root.clipData.mask[modelData.key]) || 0
                    }
                    onMoved: {
                        const mask = Object.assign({}, root.clipData.mask || {})
                        mask[modelData.key] = value
                        EditorState.previewSetClipMask(
                            EditorState.selectedTrack, EditorState.selectedClip, mask)
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
            visible: root.clipKind !== "audio" && root.clipKind !== "text"
                     && root.clipKind !== "subtitle"
                     && !!root.clipData.mask && root.clipData.mask.shape !== "none"
            Text {
                text: qsTr("Invert")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                anchors.verticalCenter: parent.verticalCenter
            }
            ThemedSwitch {
                checked: !!(root.clipData.mask && root.clipData.mask.invert)
                onToggled: {
                    const mask = Object.assign({}, root.clipData.mask || {})
                    mask.invert = checked
                    EditorState.setClipMask(EditorState.selectedTrack, EditorState.selectedClip, mask)
                }
            }
        }
    }
}
