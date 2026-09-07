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
    readonly property bool isVisualClip: clipKind !== "audio" && clipKind !== "text"
                                         && clipKind !== "subtitle"
    // The mask reported for the selected clip. For a media clip that is the mask on the adjustment
    // pinned to it (clipToMap redirects); for a mask adjustment it is its own payload. Either way
    // setClipMask writes back to the right place.
    readonly property string maskShape: (clipData.mask && clipData.mask.shape) || "none"
    // Media is a raster mask whose pixels are the coverage map (see core/Mask.h). The parametric
    // geometry does place it, but a segmentation matte is full-frame by construction and nudging
    // its rect only ever crops the subject, so the sliders stay hidden and invert plus removal
    // are what is offered.
    readonly property bool isMedia: maskShape === "media"

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

            // The models are addons, but either can equally come from a bundled
            // models/ directory or a DRIFT_*_MODEL_DIR override, so ask the engine
            // rather than the addon registry. That answer is not a binding, hence
            // the reset below when an addon of either kind appears.
            property bool segmentReady: EditorState.segmentationAvailable()
            property bool runtimeReady: Addons.runtimeAvailable()
            // Either cutout model unlocks the window, so the download prompt below points at
            // RVM — the smaller one — and SAM2 is offered separately as an extra capability.
            property bool hasSam2: EditorState.segmentationBackends().indexOf("sam2") >= 0

            Connections {
                target: Addons
                function onKindChanged(kind) {
                    if (kind === "sam2-model" || kind === "rvm-model") {
                        segmentSection.segmentReady = EditorState.segmentationAvailable()
                        segmentSection.hasSam2 = EditorState.segmentationBackends().indexOf("sam2") >= 0
                    } else if (kind === "onnxruntime") {
                        segmentSection.runtimeReady = Addons.runtimeAvailable()
                    }
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
                      ? qsTr("Download people cutout (about 20 MB)")
                      : qsTr("Install AI engine first")
                variant: "primary"
                onClicked: root.Window.window.openAddonManager(
                    segmentSection.runtimeReady ? "rvm-model" : "onnxruntime")
            }

            // Offered separately once people cutout works: clicking a specific subject is a
            // different capability, not a better version of the same one, and it is ten times
            // the download.
            ThemedButton {
                visible: segmentSection.segmentReady && segmentSection.runtimeReady
                         && !segmentSection.hasSam2
                width: parent.width
                variant: "secondary"
                text: qsTr("Add click-to-pick cutout (about 190 MB)")
                onClicked: root.Window.window.openAddonManager("sam2-model")
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

        ThemedComboBox {
            id: maskShapeBox
            // Hidden for a media mask: "media" is not one of the shapes below, so currentIndex
            // would clamp to 0 and the control would read "None" next to an applied cutout.
            visible: root.isVisualClip && !root.isMedia
            width: parent.width
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
            visible: root.isVisualClip && root.maskShape !== "none"
            text: root.isMedia ? qsTr("Remove cutout layer") : qsTr("Remove mask")
            variant: "destructive"
            glyph: Theme.icons.trash
            onClicked: {
                const mask = Object.assign({}, root.clipData.mask || {})
                mask.shape = "none"
                EditorState.setClipMask(EditorState.selectedTrack, EditorState.selectedClip, mask)
            }
        }

        // Mask scalars animate through the same generic keyframe API as a clip's transform, so
        // they get the same row: diamond, prev/next key navigation, easing chips. The prop ids are
        // "mask.<key>"; redirectToKeyframeHost is what walks from the selected media clip to the
        // adjustment actually carrying the mask.
        Repeater {
            // `shapes` is what the rasterizer actually reads for each shape (see
            // MaskApplier::maskPath): Bars derives both bands from `h` alone and ignores the
            // rest, and a Freeform's vertices *are* the shape, so its rect and rotation do
            // nothing. Offering those sliders anyway just invites dragging something inert.
            model: [
                { key: "mask.x", label: qsTr("Center X"), def: 0.5, decimals: 3, min: 0, max: 1,
                  shapes: ["rectangle", "ellipse", "star", "heart"] },
                { key: "mask.y", label: qsTr("Center Y"), def: 0.5, decimals: 3, min: 0, max: 1,
                  shapes: ["rectangle", "ellipse", "star", "heart"] },
                { key: "mask.w", label: qsTr("Width"), def: 0.6, decimals: 3, min: 0.05, max: 1,
                  shapes: ["rectangle", "ellipse", "star", "heart"] },
                { key: "mask.h", label: qsTr("Height"), def: 0.6, decimals: 3, min: 0.05, max: 1,
                  shapes: ["rectangle", "ellipse", "star", "heart", "bars"] },
                { key: "mask.rotation", label: qsTr("Rotation"), def: 0, decimals: 1,
                  min: -180, max: 180, unit: "°",
                  shapes: ["rectangle", "ellipse", "star", "heart"] },
                // Feather blurs the finished coverage map, so it applies to every shape.
                { key: "mask.feather", label: qsTr("Feather"), def: 0, decimals: 0,
                  min: 0, max: 64, unit: "px",
                  shapes: ["rectangle", "ellipse", "star", "heart", "bars", "freeform"] }
            ]
            delegate: PropertyKeyframeRow {
                required property var modelData
                width: parent.width
                visible: root.isVisualClip && !root.isMedia
                         && modelData.shapes.indexOf(root.maskShape) >= 0
                propDef: modelData
                // Key times come back on the timeline already, keyed by the bare scalar name.
                keyframeList: {
                    const keys = root.clipData.mask && root.clipData.mask.keyframes
                    const entry = keys && keys[modelData.key.substring(5)]
                    return (entry && entry.points) || []
                }
                useSlider: true
                sliderFrom: modelData.min
                sliderTo: modelData.max
                unit: modelData.unit || ""
            }
        }

        Row {
            width: parent.width
            spacing: 8
            visible: root.isVisualClip && root.maskShape !== "none"
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
