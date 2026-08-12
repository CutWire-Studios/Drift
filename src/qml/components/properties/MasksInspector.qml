import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
import ".."

// Parameters for the cutout selected on the timeline's mask lane. Creating, ordering, timing and
// selecting cutouts all happen on the lane itself — this panel only shapes whichever one is
// selected there.
Item {
    id: root

    property int revision: 0
    readonly property var mask: {
        void revision
        return EditorState.selectedMaskData()
    }
    readonly property bool hasMask: !!mask && Object.keys(mask).length > 0
    readonly property int maskTrack: hasMask ? mask.track : -1
    readonly property int maskIndex: hasMask ? mask.index : -1
    readonly property bool isMedia: hasMask && mask.shape === "media"
    readonly property bool isBars: hasMask && mask.shape === "bars"
    readonly property bool isFreeform: hasMask && mask.shape === "freeform"

    readonly property string clipKind: {
        void revision
        const data = EditorState.selectedClipData
        return (data && data.kind) || ""
    }

    height: contentCol.height
    implicitHeight: contentCol.height

    function refreshFields() {}

    function writeMask(changes) {
        if (!root.hasMask)
            return
        const next = Object.assign({}, root.mask, changes)
        EditorState.setTrackMaskAt(root.maskTrack, root.maskIndex, next)
    }

    Connections {
        target: EditorState
        function onSelectedMaskChanged() { root.revision++ }
        function onSelectionChanged() { root.revision++ }
        function onSelectedClipDataChanged() { root.revision++ }
        function onTracksChanged() { root.revision++ }
    }

    Column {
        id: contentCol
        width: root.width
        spacing: Theme.spacingXl

        // Segmentation produces a cutout that follows the subject frame by frame, so it belongs
        // beside the shapes rather than in a tab of its own. It needs a prompting surface, so it
        // opens a window instead of running from here.
        Column {
            id: segmentSection
            visible: root.clipKind === "video"
            width: parent.width
            spacing: Theme.spacingSm

            // The model is an addon, but it can equally come from a bundled models/sam2 or
            // DRIFT_SAM2_MODEL_DIR, so ask the engine rather than the addon registry. That answer
            // is not a binding, hence the reset below when an addon of this kind appears.
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

        EmptyState {
            visible: !root.hasMask
            width: parent.width
            compact: true
            glyph: Theme.icons.mask
            title: qsTr("No cutout selected")
            hint: qsTr("Cutouts live on a track's own lane. Right-click a track's name to add "
                       + "one, or drop an image or video onto its lane, then click it to shape "
                       + "it here.")
        }

        Column {
            width: parent.width
            spacing: Theme.spacingSm
            visible: root.hasMask

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("This cutout masks the clips beneath it, for as long as its bar runs. "
                           + "Outside that stretch the clips are untouched.")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                opacity: 0.8
            }

            Text {
                text: qsTr("Shape")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            ThemedComboBox {
                id: shapeBox
                width: parent.width
                // Media is not offered here: it comes from a file, so it is created by dropping
                // one on the lane rather than by picking it from a list.
                model: ["rectangle", "ellipse", "star", "heart", "bars", "freeform"]
                readonly property var labels: ({
                    "rectangle": qsTr("Rectangle"), "ellipse": qsTr("Ellipse"),
                    "star": qsTr("Star"), "heart": qsTr("Heart"), "bars": qsTr("Bars"),
                    "freeform": qsTr("Polygon")
                })
                visible: !root.isMedia
                displayText: labels[model[currentIndex]] || model[currentIndex]
                currentIndex: Math.max(0, model.indexOf(root.hasMask ? root.mask.shape : ""))
                onActivated: root.writeMask({ "shape": model[currentIndex] })
            }

            Text {
                visible: root.isMedia
                width: parent.width
                elide: Text.ElideMiddle
                text: root.hasMask ? (root.mask.mediaPath || "") : ""
                color: Theme.mutedForeground
                font.family: Theme.monoFontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            Text {
                text: qsTr("Combines by")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            ThemedComboBox {
                id: opBox
                width: parent.width
                model: ["add", "subtract", "intersect"]
                readonly property var labels: ({
                    "add": qsTr("Revealing"),
                    "subtract": qsTr("Cutting out"),
                    "intersect": qsTr("Trimming")
                })
                displayText: labels[model[currentIndex]] || model[currentIndex]
                tooltip: qsTr("How this cutout combines with the ones on lower lanes")
                currentIndex: Math.max(0, model.indexOf(root.hasMask ? (root.mask.op || "add") : "add"))
                onActivated: root.writeMask({ "op": model[currentIndex] })
            }

            ThemedButton {
                width: parent.width
                text: EditorState.maskEditMode ? qsTr("Done editing in preview")
                                               : qsTr("Edit in preview")
                variant: EditorState.maskEditMode ? "primary" : "secondary"
                glyph: Theme.icons.maximize
                onClicked: EditorState.maskEditMode = !EditorState.maskEditMode
            }

            ThemedButton {
                width: parent.width
                text: root.showingMaskView ? qsTr("Hide cutout view") : qsTr("Show cutout view")
                variant: root.showingMaskView ? "primary" : "secondary"
                glyph: Theme.icons.contrast
                tooltip: qsTr("Show what the cutouts cover, in black and white. Preview only — "
                              + "exports are unaffected.")
                onClicked: EditorState.toggleMaskViewForSelectedClip()
            }
        }

        Column {
            width: parent.width
            spacing: Theme.spacingSm
            visible: root.hasMask

            Repeater {
                model: [
                    { key: "x", label: qsTr("Center X"), min: 0, max: 1 },
                    { key: "y", label: qsTr("Center Y"), min: 0, max: 1 },
                    { key: "w", label: qsTr("Width"), min: 0.05, max: 1 },
                    { key: "h", label: qsTr("Height"), min: 0.05, max: 1 },
                    { key: "rotation", label: qsTr("Rotation"), min: -180, max: 180 },
                    { key: "feather", label: qsTr("Feather"), min: 0, max: 64 }
                ]

                // Addressed as "mask.<index>.<param>", the same shape effect params use, so the
                // whole generic keyframe API — diamonds, easing chips, the curve strip — reaches
                // these with no parallel plumbing. Key times are relative to the cutout's own bar.
                delegate: PropertyKeyframeRow {
                    required property var modelData
                    width: parent.width
                    // A polygon's points are its shape, so its box is derived rather than set.
                    // Bars spans the frame, so rotating it only crops.
                    visible: !(root.isFreeform && modelData.key !== "feather"
                               && modelData.key !== "rotation")
                             && !(root.isBars && modelData.key === "rotation")

                    propDef: ({
                        key: "mask." + root.maskIndex + "." + modelData.key,
                        label: modelData.label,
                        def: modelData.key === "w" || modelData.key === "h" ? 0.6
                             : (modelData.key === "x" || modelData.key === "y" ? 0.5 : 0),
                        decimals: modelData.key === "feather" || modelData.key === "rotation"
                                  ? 0 : 2
                    })
                    keyframeList: {
                        void root.revision
                        const kf = root.hasMask ? root.mask.keyframes : null
                        const track = kf ? kf[modelData.key] : null
                        return (track && track.points) ? track.points : []
                    }
                    useSlider: true
                    sliderFrom: modelData.min
                    sliderTo: modelData.max
                    unit: modelData.key === "rotation" ? "°" : ""
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
                    checked: root.hasMask && !!root.mask.invert
                    onToggled: root.writeMask({ "invert": checked })
                }
            }
        }

        // Media-only controls: how the file is fitted into the cutout's box, which of its channels
        // carries the coverage, and what happens past its end.
        Column {
            width: parent.width
            spacing: Theme.spacingSm
            visible: root.isMedia

            Text {
                text: qsTr("Fit")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
            ThemedComboBox {
                width: parent.width
                model: ["stretch", "fit", "fill"]
                readonly property var labels: ({
                    "stretch": qsTr("Stretch"), "fit": qsTr("Fit inside"), "fill": qsTr("Fill")
                })
                displayText: labels[model[currentIndex]] || model[currentIndex]
                tooltip: qsTr("How the file is sized into the cutout's box")
                currentIndex: Math.max(0, model.indexOf(
                    root.hasMask ? (root.mask.mediaFit || "stretch") : "stretch"))
                onActivated: root.writeMask({ "mediaFit": model[currentIndex] })
            }

            Text {
                text: qsTr("Coverage from")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
            ThemedComboBox {
                width: parent.width
                model: ["luma", "alpha"]
                readonly property var labels: ({
                    "luma": qsTr("Brightness"), "alpha": qsTr("Transparency")
                })
                displayText: labels[model[currentIndex]] || model[currentIndex]
                tooltip: qsTr("Brightness suits black-and-white mattes; transparency suits a "
                              + "cut-out PNG")
                currentIndex: Math.max(0, model.indexOf(
                    root.hasMask ? (root.mask.mediaChannel || "luma") : "luma"))
                onActivated: root.writeMask({ "mediaChannel": model[currentIndex] })
            }

            Row {
                width: parent.width
                spacing: 8
                Text {
                    text: qsTr("Loop")
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    anchors.verticalCenter: parent.verticalCenter
                }
                ThemedSwitch {
                    checked: root.hasMask && !!root.mask.mediaLoop
                    onToggled: root.writeMask({ "mediaLoop": checked })
                }
            }
        }

        ThemedButton {
            visible: root.hasMask
            width: parent.width
            text: qsTr("Delete cutout")
            variant: "destructive"
            glyph: Theme.icons.trash
            onClicked: EditorState.removeTrackMask(root.maskTrack, root.maskIndex)
        }
    }

    readonly property bool showingMaskView: {
        void revision
        const data = EditorState.selectedClipData
        return EditorState.maskViewClipId !== ""
               && !!data && EditorState.maskViewClipId === (data.id || "")
    }
}
