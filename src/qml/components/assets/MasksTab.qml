import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Shapes
import QtQuick.Window
import Drift
import ".."
import "."

// Masks tab. A mask is a Mask-kind adjustment clip on a nested lane, so adding one is the same
// gesture as adding an effect: drag a card onto a clip, or onto empty track space to mask
// everything that track shows over that span. Editing one is what the properties panel is for,
// and only ever with the mask clip itself selected.
Item {
    id: root

    // A mask landed on the timeline. The phone shell closes the sheet on this.
    signal added()

    // selectedClipData is not re-notified when the selection moves, so the binding has to be
    // poked by hand — the same dance the properties inspectors do.
    property int clipDataRevision: 0
    readonly property var clipData: {
        void clipDataRevision
        return EditorState.selectedClipData
    }

    Connections {
        target: EditorState
        function onSelectionChanged() { root.clipDataRevision++ }
        function onSelectedClipDataChanged() { root.clipDataRevision++ }
        function onTracksChanged() { root.clipDataRevision++ }
    }

    readonly property string clipKind: (clipData && clipData.kind) || ""
    // Audio has no picture to cut into, and text and subtitles are composited from their own
    // styling rather than a masked layer.
    readonly property bool hasVisualSelection: EditorState.selectedClip >= 0
                                               && clipKind !== "audio" && clipKind !== "text"
                                               && clipKind !== "subtitle"

    readonly property var masks: EditorState.maskCatalog()

    function applyMask(maskId) {
        if (!root.hasVisualSelection)
            return
        EditorState.addMaskToClip(EditorState.selectedTrack, EditorState.selectedClip, maskId)
        root.added()
    }

    Column {
        anchors.fill: parent
        spacing: 0

        Text {
            id: tip
            width: parent.width - Theme.pagePadding * 2
            x: Theme.pagePadding
            topPadding: Theme.spacingSm
            bottomPadding: Theme.spacingSm
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            text: root.hasVisualSelection
                  ? qsTr("Click to apply to the selection, or drag onto a clip")
                  : qsTr("Select a clip, or drag a mask onto one")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        // Segmentation needs a source clip and a prompting surface, so it stays a button that
        // acts on the selection and opens a window — it is the one mask you cannot drag.
        Column {
            id: segmentSection
            x: Theme.pagePadding
            width: parent.width - Theme.pagePadding * 2
            spacing: Theme.spacingSm

            // The models are addons, but either can equally come from a bundled models/ directory
            // or a DRIFT_*_MODEL_DIR override, so ask the engine rather than the addon registry.
            // That answer is not a binding, hence the reset below when an addon of either kind
            // appears.
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
                enabled: !EditorState.segmenting && root.clipKind === "video"
                tooltip: root.clipKind === "video"
                         ? qsTr("Trace the subject and pin the result as a mask layer")
                         : qsTr("Select a video clip first")
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

            ThemedButton {
                width: parent.width
                variant: "secondary"
                text: qsTr("Image or video as mask…")
                enabled: root.hasVisualSelection
                tooltip: qsTr("Use a file's own pixels as the coverage map")
                onClicked: {
                    const url = FileDialogs.openFile(
                        qsTr("Choose a mask image or video"),
                        [qsTr("Media files (*.png *.jpg *.jpeg *.webp *.mp4 *.mov *.mkv *.webm)"),
                         qsTr("All files (*)")])
                    if (!url || url.toString() === "")
                        return
                    EditorState.addMediaMaskToClip(EditorState.selectedTrack,
                                                   EditorState.selectedClip, url)
                    root.added()
                }
            }
        }

        Item {
            width: 1
            height: Theme.spacingLg
        }

        Flickable {
            width: parent.width
            height: Math.max(0, parent.height - tip.height - segmentSection.height
                             - Theme.spacingLg)
            contentHeight: maskGrid.height + Theme.pagePadding * 2
            clip: true
            ScrollBar.vertical: AppScrollBar { }

            Grid {
                id: maskGrid
                x: Theme.pagePadding
                y: Theme.pagePadding
                width: parent.width - Theme.pagePadding * 2
                columns: Math.max(1, Math.floor((width + Theme.assetCardGap) / (Theme.assetCardWidth + Theme.assetCardGap)))
                columnSpacing: Theme.assetCardGap
                rowSpacing: Theme.assetCardGap

                Repeater {
                    model: root.masks
                    delegate: Column {
                        id: maskCard
                        required property var modelData
                        width: Theme.assetCardWidth
                        spacing: Theme.spacingSm

                        // Lift on grab: the card dims and grows slightly, so it reads as picked
                        // up rather than merely faded.
                        opacity: maskDrag.active ? 0.85 : 1
                        scale: maskDrag.active ? 1.04 : 1.0

                        Behavior on opacity {
                            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                        }
                        Behavior on scale {
                            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                        }

                        Drag.active: maskDrag.active
                        Drag.dragType: Drag.Automatic
                        Drag.supportedActions: Qt.CopyAction
                        Drag.keys: ["application/x-drift-mask"]
                        Drag.mimeData: ({ "application/x-drift-mask": maskCard.modelData.id })
                        Drag.hotSpot.x: width / 2
                        Drag.hotSpot.y: Theme.assetCardWidth / 2

                        Rectangle {
                            width: Theme.assetCardWidth
                            height: Theme.assetCardWidth
                            radius: Theme.radiusSm
                            color: maskHover.hovered ? Theme.popoverHover : Theme.panelAccent
                            border.width: maskDrag.active ? 1 : 0
                            border.color: Theme.primary
                            clip: true

                            Behavior on color {
                                ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                            }
                            Behavior on border.width {
                                NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                            }

                            // The coverage the mask actually produces, drawn from the same path
                            // the compositor rasterizes: white shows through, the rest is cut.
                            Item {
                                anchors.fill: parent
                                anchors.margins: Theme.spacingSm

                                readonly property real side: Math.min(width, height)

                                Item {
                                    width: 100
                                    height: 100
                                    anchors.centerIn: parent
                                    scale: parent.side / 100

                                    Shape {
                                        anchors.fill: parent
                                        antialiasing: true

                                        ShapePath {
                                            strokeColor: Theme.onMedia
                                            strokeWidth: 1
                                            fillColor: Theme.clipAdjustmentMask
                                            PathSvg { path: EditorState.maskShapeSvgPath(maskCard.modelData.id) }
                                        }
                                    }
                                }
                            }

                            HoverHandler {
                                id: maskHover
                                cursorShape: Qt.PointingHandCursor
                            }

                            ThemedToolTip {
                                text: root.hasVisualSelection
                                      ? qsTr("%1 — click to apply, or drag onto a clip").arg(maskCard.modelData.label)
                                      : qsTr("%1 — drag onto a clip").arg(maskCard.modelData.label)
                                visible: maskHover.hovered
                            }

                            TapHandler {
                                enabled: !maskDrag.active && root.hasVisualSelection
                                onTapped: root.applyMask(maskCard.modelData.id)
                            }
                            DragHandler {
                                id: maskDrag
                                target: null
                                // Touch applies to the selection and closes the sheet; a platform
                                // drag has no gesture there and only competes for the grab.
                                enabled: !Theme.touchUi
                                acceptedButtons: Qt.LeftButton
                            }
                        }

                        Text {
                            width: parent.width
                            text: maskCard.modelData.label
                            color: Theme.mutedForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeCard
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
    }
}
