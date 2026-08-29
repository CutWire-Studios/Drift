import QtQuick
import QtQuick.Controls.Basic
import Drift
import ".."

// Scenes tab: shot boundaries found in the selected video clip, ranked by how much is
// going on. Clicking a row seeks to that shot.
//
// The analysis describes the source media rather than the project, so it is never saved
// into the project file — it is cached on disk and re-published when the same clip is
// scanned again. See engine/SceneDetect.h.
Item {
    id: root

    readonly property int selectedTrack: EditorState.selectedTrack
    readonly property int selectedClip: EditorState.selectedClip
    readonly property bool hasVideoClip: EditorState.selectedClipData.kind === "video"
    // The live analysis belongs to whichever clip was scanned, which is not necessarily
    // the one selected now.
    readonly property bool showingSelected:
        EditorState.sceneClipId !== "" && EditorState.sceneClipId === EditorState.selectedClipData.id

    property bool sortByScore: false
    property string labelFilter: ""

    // Scene rows in display order. Sorting by score is a view concern — the underlying
    // list stays in time order so `index` still addresses the right scene.
    readonly property var visibleScenes: {
        let rows = EditorState.scenes.slice()
        if (root.labelFilter !== "")
            rows = rows.filter(s => (s.labels || []).indexOf(root.labelFilter) >= 0)
        if (root.sortByScore)
            rows.sort((a, b) => b.score - a.score)
        return rows
    }

    // Every object class seen anywhere in the analysis, for the filter chips. Empty until
    // the object pass has run.
    readonly property var allLabels: {
        const seen = {}
        for (const scene of EditorState.scenes) {
            for (const label of (scene.labels || []))
                seen[label] = true
        }
        return Object.keys(seen).sort()
    }

    // Tiles arrive asynchronously; nudge the delegates to re-ask when one lands.
    property int tileRevision: 0
    Connections {
        target: EditorState
        function onFilmstripTileReady(path) {
            if (path === EditorState.sceneClipPath)
                root.tileRevision++
        }
    }

    Column {
        id: header
        x: Theme.pagePadding
        width: parent.width - Theme.pagePadding * 2
        topPadding: Theme.pagePadding
        spacing: Theme.spacingMd

        Text {
            width: header.width
            wrapMode: Text.WordWrap
            text: qsTr("Finds where the picture cuts in the selected video clip, and ranks each shot by movement and loudness. Click a shot to jump to it.")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        // --- controls -------------------------------------------------------
        Row {
            spacing: Theme.spacingMd
            visible: !EditorState.sceneDetecting

            ThemedButton {
                text: qsTr("Find scenes")
                variant: "primary"
                glyph: Theme.icons.listVideo
                enabled: root.hasVideoClip
                tooltip: root.hasVideoClip
                         ? qsTr("Scan the selected clip for shot boundaries")
                         : qsTr("Select a video clip first")
                onClicked: EditorState.detectScenesForClip(root.selectedTrack, root.selectedClip,
                                                           objectToggle.checked)
            }

            ThemedButton {
                text: qsTr("Clear")
                variant: "secondary"
                visible: EditorState.scenes.length > 0
                onClicked: {
                    root.labelFilter = ""
                    EditorState.clearScenes()
                }
            }
        }

        ThemedSwitch {
            id: objectToggle
            text: qsTr("Identify objects")
            enabled: EditorState.objectDetectionAvailable()
            visible: !EditorState.sceneDetecting
            tooltip: enabled
                     ? qsTr("Also label each shot with what is in it. Slower.")
                     : qsTr("Needs the Scene Labels add-on — install it from Extras")
        }

        // Sensitivity. Lower finds more cuts; the engine still falls back to an adaptive
        // threshold when this one finds implausibly little on flat or graded footage.
        Column {
            width: header.width
            spacing: Theme.spacingXs
            visible: !EditorState.sceneDetecting

            Text {
                text: qsTr("Sensitivity")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            ThemedSlider {
                id: sensitivity
                width: header.width
                label: qsTr("Scene detection sensitivity")
                // Inverted: dragging right should mean "find more", but a lower threshold
                // is what does that.
                from: 4
                to: 60
                value: 64 - EditorState.sceneThreshold()
                valueFormatter: function (v) { return Number(64 - v).toFixed(0) }
                onMoved: EditorState.setSceneThreshold(64 - value)
            }
        }

        // --- running --------------------------------------------------------
        Column {
            width: header.width
            spacing: Theme.spacingSm
            visible: EditorState.sceneDetecting

            Text {
                text: EditorState.sceneDetectStatus
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                elide: Text.ElideRight
                width: header.width
            }

            ThemedProgressBar {
                width: header.width
                value: EditorState.sceneDetectProgress
            }

            ThemedButton {
                text: qsTr("Cancel")
                variant: "secondary"
                onClicked: EditorState.cancelSceneDetection()
            }
        }

        // --- filters --------------------------------------------------------
        Row {
            spacing: Theme.spacingMd
            visible: EditorState.scenes.length > 0 && !EditorState.sceneDetecting

            ThemedToggleButton {
                checked: root.sortByScore
                text: root.sortByScore ? qsTr("Most active first") : qsTr("In order")
                tooltip: qsTr("Switch between timeline order and activity ranking")
                onClicked: root.sortByScore = !root.sortByScore
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("%1 scenes").arg(root.visibleScenes.length)
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
        }

        Flow {
            width: header.width
            spacing: Theme.spacingXs
            visible: root.allLabels.length > 0 && !EditorState.sceneDetecting

            Repeater {
                model: root.allLabels
                ThemedChip {
                    text: modelData
                    selected: root.labelFilter === modelData
                    onClicked: root.labelFilter = (root.labelFilter === modelData) ? "" : modelData
                }
            }
        }

        // A stale analysis is worth flagging rather than silently showing another clip's
        // shots against the current selection.
        Text {
            width: header.width
            wrapMode: Text.WordWrap
            visible: EditorState.scenes.length > 0 && !root.showingSelected
                     && !EditorState.sceneDetecting
            text: qsTr("These scenes are from another clip. Select it again, or run Find scenes on the current one.")
            color: Theme.warning
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
    }

    EmptyState {
        anchors.centerIn: parent
        width: parent.width
        visible: EditorState.scenes.length === 0 && !EditorState.sceneDetecting
        glyph: Theme.icons.listVideo
        title: root.hasVideoClip ? qsTr("No scenes yet") : qsTr("No video clip selected")
        hint: root.hasVideoClip
              ? qsTr("Run Find scenes to split this clip into its shots.")
              : qsTr("Select a video clip on the timeline to scan it.")
    }

    ListView {
        id: sceneList
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        anchors.topMargin: Theme.spacingMd
        anchors.leftMargin: Theme.pagePadding
        anchors.rightMargin: Theme.pagePadding
        clip: true
        spacing: Theme.spacingXs
        visible: EditorState.scenes.length > 0 && !EditorState.sceneDetecting
        model: root.visibleScenes
        ScrollBar.vertical: AppScrollBar { }

        delegate: Rectangle {
            id: row
            width: sceneList.width
            height: 56
            radius: Theme.radiusSm
            color: rowMouse.containsMouse ? Theme.panelMuted : "transparent"
            border.width: Theme.borderWidth
            border.color: rowMouse.containsMouse ? Theme.panelBorder : "transparent"

            required property var modelData

            Row {
                anchors.fill: parent
                anchors.margins: Theme.spacingXs
                spacing: Theme.spacingMd

                Rectangle {
                    width: 80
                    height: parent.height
                    radius: Theme.radiusXs
                    color: Theme.skeletonColor
                    clip: true

                    Image {
                        anchors.fill: parent
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        cache: true
                        // Level 0 tiles cover one source second each, so the tile index is
                        // simply the whole second the representative frame falls in.
                        source: {
                            root.tileRevision // re-evaluate when a decode lands
                            return EditorState.sceneClipPath === ""
                                ? ""
                                : EditorState.filmstripTileUrl(
                                      EditorState.sceneClipPath, 0,
                                      Math.floor(row.modelData.thumbnailSeconds))
                        }
                    }
                }

                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2

                    Text {
                        text: qsTr("Scene %1").arg(row.modelData.index + 1)
                        color: Theme.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                        font.bold: true
                    }

                    Text {
                        text: qsTr("%1 – %2  ·  %3s")
                            .arg(Number(row.modelData.sourceStart).toFixed(2))
                            .arg(Number(row.modelData.sourceEnd).toFixed(2))
                            .arg(Number(row.modelData.duration).toFixed(2))
                        color: Theme.mutedForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                    }

                    Text {
                        visible: (row.modelData.labels || []).length > 0
                        text: (row.modelData.labels || []).slice(0, 4).join(", ")
                        color: Theme.mutedForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                        elide: Text.ElideRight
                        width: Math.max(0, row.width - 200)
                    }
                }
            }

            // Signal bars, right-aligned: movement over loudness. Kept separate rather
            // than folded into one number so the ranking is explainable at a glance.
            Column {
                anchors.right: parent.right
                anchors.rightMargin: Theme.spacingMd
                anchors.verticalCenter: parent.verticalCenter
                spacing: 3

                Repeater {
                    model: [
                        { value: row.modelData.motion, tint: Theme.primary },
                        { value: row.modelData.loudness, tint: Theme.beatOnsetColor }
                    ]
                    Rectangle {
                        required property var modelData
                        width: 48
                        height: 4
                        radius: 2
                        color: Theme.panelMuted

                        Rectangle {
                            width: parent.width * Math.max(0, Math.min(1, parent.modelData.value))
                            height: parent.height
                            radius: parent.radius
                            color: parent.modelData.tint
                        }
                    }
                }
            }

            MouseArea {
                id: rowMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: EditorState.seekToScene(row.modelData.index)
            }
        }
    }
}
