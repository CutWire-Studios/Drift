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
    // The tab is only offered for a mask adjustment (this is its payload) or a video-effects one
    // (where a mask scopes where the chain lands). Masks are added from the assets panel and
    // edited by selecting the mask clip, so a media clip never reaches here.
    readonly property string maskShape: (clipData.mask && clipData.mask.shape) || "none"
    // Media is a raster mask whose pixels are the coverage map (see core/Mask.h). The parametric
    // geometry does place it, but a segmentation matte is full-frame by construction and nudging
    // its rect only ever crops the subject, so the sliders stay hidden and invert plus removal
    // are what is offered.
    readonly property bool isMedia: maskShape === "media"
    readonly property bool isSequence: maskShape === "sequence"
    readonly property bool hasBox: ["rectangle", "ellipse", "star", "heart"].indexOf(maskShape) >= 0
    readonly property bool aspectLocked: !!(clipData.mask && clipData.mask.lockAspect)

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

        // The tab used to open with a lone unlabelled combo box
        // and no explanation of what a mask does.
        Text {
            visible: maskShapeBox.visible
            width: parent.width
            text: qsTr("Shape")
            color: Theme.c.masksInspector.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        ThemedComboBox {
            id: maskShapeBox
            // Hidden for a media mask: "media" is not one of the shapes below, so currentIndex
            // would clamp to 0 and the control would read "None" next to an applied cutout.
            visible: !root.isMedia && !root.isSequence
            width: parent.width
            model: ["none", "rectangle", "ellipse", "star", "heart", "bars", "split", "freeform"]
            // Human labels — the raw ids were shown to the user.
            readonly property var labels: ({
                "none": qsTr("None"),
                "rectangle": qsTr("Rectangle"),
                "ellipse": qsTr("Ellipse"),
                "star": qsTr("Star"),
                "heart": qsTr("Heart"),
                "bars": qsTr("Bars"),
                "split": qsTr("Split"),
                "freeform": qsTr("Polygon")
            })
            displayText: labels[model[currentIndex]] || model[currentIndex]
            currentIndex: Math.max(0, model.indexOf((root.clipData.mask && root.clipData.mask.shape) || "none"))
            onActivated: {
                const mask = Object.assign({}, root.clipData.mask || {})
                mask.shape = model[currentIndex]
                EditorState.setClipMask(EditorState.selectedTrack, EditorState.selectedClip, mask)
            }
        }

        // How this mask folds into the ones stacked before it on the same track. Only meaningful
        // once there is more than one: the compositor ignores the first enabled entry's op and
        // lets it seed the coverage, because a lone Subtract or Intersect would blank the layer.
        Column {
            visible: root.maskShape !== "none"
            width: parent.width
            spacing: Theme.spacingSm

            Text {
                width: parent.width
                text: qsTr("Combine")
                SearchAnchor {
                    searchId: "masks-combine"
                    title: qsTr("Combine")
                    keywords: "add subtract intersect"
                }
                color: Theme.c.masksInspector.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            ThemedComboBox {
                id: maskOpBox
                width: parent.width
                model: ["add", "subtract", "intersect"]
                readonly property var labels: ({
                    "add": qsTr("Add"),
                    "subtract": qsTr("Subtract"),
                    "intersect": qsTr("Intersect")
                })
                displayText: labels[model[currentIndex]] || model[currentIndex]
                currentIndex: Math.max(0, model.indexOf((root.clipData.mask && root.clipData.mask.op) || "add"))
                onActivated: {
                    const mask = Object.assign({}, root.clipData.mask || {})
                    mask.op = model[currentIndex]
                    EditorState.setClipMask(EditorState.selectedTrack, EditorState.selectedClip, mask)
                }
            }
        }

        // Clearing a mask previously required knowing to reselect
        // "none" in the combo above.
        ThemedButton {
            visible: root.maskShape !== "none"
            text: root.isMedia || root.isSequence ? qsTr("Remove cutout layer") : qsTr("Remove mask")
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
        SearchAnchor {
            searchId: "masks-feather"
            title: qsTr("Feather")
            keywords: "soft edge blur"
        }
        SearchAnchor {
            searchId: "masks-roundness"
            title: qsTr("Roundness")
            keywords: "rounded corners radius pill"
        }
        Repeater {
            // `shapes` is what the rasterizer actually reads for each shape (see
            // MaskApplier::maskPath): Bars derives both bands from `h` alone and ignores the
            // rest, and a Freeform's vertices *are* the shape, so its rect and rotation do
            // nothing. Offering those sliders anyway just invites dragging something inert.
            model: [
                { key: "mask.x", label: qsTr("Center X"), def: 0.5, decimals: 3, min: 0, max: 1,
                  shapes: ["rectangle", "ellipse", "star", "heart", "split"] },
                { key: "mask.y", label: qsTr("Center Y"), def: 0.5, decimals: 3, min: 0, max: 1,
                  shapes: ["rectangle", "ellipse", "star", "heart", "split"] },
                { key: "mask.w", label: qsTr("Width"), def: 0.6, decimals: 3, min: 0.05, max: 1,
                  shapes: ["rectangle", "ellipse", "star", "heart"] },
                { key: "mask.h", label: qsTr("Height"), def: 0.6, decimals: 3, min: 0.05, max: 1,
                  shapes: ["rectangle", "ellipse", "star", "heart", "bars"] },
                { key: "mask.rotation", label: qsTr("Rotation"), def: 0, decimals: 1,
                  min: -180, max: 180, unit: "°",
                  shapes: ["rectangle", "ellipse", "star", "heart", "split"] },
                { key: "mask.roundness", label: qsTr("Roundness"), def: 0, decimals: 2,
                  min: 0, max: 1, percent: true, shapes: ["rectangle"] },
                // Feather blurs the finished coverage map, so it applies to every shape.
                { key: "mask.feather", label: qsTr("Feather"), def: 0, decimals: 0,
                  min: 0, max: 64, unit: "px",
                  shapes: ["rectangle", "ellipse", "star", "heart", "bars", "split", "freeform", "sequence"] }
            ]
            delegate: PropertyKeyframeRow {
                required property var modelData
                width: parent.width
                visible: !root.isMedia
                         && modelData.shapes.indexOf(root.maskShape) >= 0
                propDef: modelData
                // Key times come back on the timeline already, keyed by the bare scalar name.
                keyframeList: {
                    const keys = root.clipData.mask && root.clipData.mask.keyframes
                    const entry = keys && keys[modelData.key.substring(5)]
                    return (entry && entry.points) || []
                }
                useSlider: true
                percent: modelData.percent === true
                sliderFrom: modelData.min
                sliderTo: modelData.max
                unit: modelData.unit || ""
                aspectPartnerKey: root.aspectLocked && modelData.key === "mask.w" ? "mask.h"
                                  : (root.aspectLocked && modelData.key === "mask.h" ? "mask.w" : "")
                aspectPartnerDef: 0.6
            }
        }

        Row {
            width: parent.width
            spacing: 8
            visible: root.hasBox
            Text {
                text: qsTr("Lock aspect ratio")
                SearchAnchor {
                    searchId: "masks-lock-aspect"
                    title: qsTr("Lock aspect ratio")
                    keywords: "proportional link width height"
                }
                color: Theme.c.masksInspector.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                anchors.verticalCenter: parent.verticalCenter
            }
            ThemedSwitch {
                checked: root.aspectLocked
                onToggled: {
                    const mask = Object.assign({}, root.clipData.mask || {})
                    mask.lockAspect = checked
                    EditorState.setClipMask(EditorState.selectedTrack, EditorState.selectedClip, mask)
                }
            }
        }

        Column {
            visible: root.isSequence
            width: parent.width
            spacing: Theme.spacingSm

            Text {
                width: parent.width
                text: qsTr("Matte channel")
                SearchAnchor {
                    searchId: "masks-matte-channel"
                    title: qsTr("Matte channel")
                    keywords: "alpha luma transparency brightness matte"
                }
                color: Theme.c.masksInspector.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            ThemedComboBox {
                id: matteChannelBox
                width: parent.width
                model: ["alpha", "luma"]
                readonly property var labels: ({
                    "alpha": qsTr("Alpha (transparency)"),
                    "luma": qsTr("Luma (brightness)")
                })
                displayText: labels[model[currentIndex]] || model[currentIndex]
                currentIndex: Math.max(0, model.indexOf((root.clipData.mask && root.clipData.mask.mediaChannel) || model[0]))
                onActivated: {
                    const mask = Object.assign({}, root.clipData.mask || {})
                    mask.mediaChannel = model[currentIndex]
                    EditorState.setClipMask(EditorState.selectedTrack, EditorState.selectedClip, mask)
                }
            }
        }

        Column {
            visible: root.isSequence
            width: parent.width
            spacing: Theme.spacingSm

            Text {
                width: parent.width
                text: qsTr("Fit")
                SearchAnchor {
                    searchId: "masks-sequence-fit"
                    title: qsTr("Fit")
                    keywords: "stretch fill fit scale matte"
                }
                color: Theme.c.masksInspector.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            ThemedComboBox {
                id: sequenceFitBox
                width: parent.width
                model: ["stretch", "fit", "fill"]
                readonly property var labels: ({
                    "stretch": qsTr("Stretch"),
                    "fit": qsTr("Fit"),
                    "fill": qsTr("Fill")
                })
                displayText: labels[model[currentIndex]] || model[currentIndex]
                currentIndex: Math.max(0, model.indexOf((root.clipData.mask && root.clipData.mask.mediaFit) || model[0]))
                onActivated: {
                    const mask = Object.assign({}, root.clipData.mask || {})
                    mask.mediaFit = model[currentIndex]
                    EditorState.setClipMask(EditorState.selectedTrack, EditorState.selectedClip, mask)
                }
            }
        }

        ThemedButton {
            visible: root.isSequence
            text: qsTr("Edit mask timeline")
            glyph: Theme.icons.layers
            SearchAnchor {
                searchId: "masks-edit-timeline"
                title: qsTr("Edit mask timeline")
                keywords: "open nested sequence use as mask"
            }
            onClicked: EditorState.openSequence(EditorState.masking.maskSequenceId(EditorState.selectedTrack,
                                                                                    EditorState.selectedClip))
        }

        Row {
            width: parent.width
            spacing: 8
            visible: root.maskShape !== "none"
            Text {
                text: qsTr("Invert")
                SearchAnchor {
                    searchId: "masks-invert"
                    title: qsTr("Invert")
                    keywords: "reverse"
                }
                color: Theme.c.masksInspector.foregroundMuted
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
