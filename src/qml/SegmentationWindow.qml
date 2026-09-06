import QtQuick
import QtQuick.Controls
import QtQuick.Window
import Drift 1.0
import "components"

// Cutout surface, for whichever backend is installed. Opened for one clip: pick a reference frame,
// mark the subject if the backend takes prompts, then run the pass over the whole clip.
//
// SAM2 is prompted and cuts out anything; RVM finds people on its own and has nothing to click, so
// the prompt overlay is bound to segmentBackendUsesPoints rather than shown unconditionally.
Window {
    id: root

    property int trackIndex: -1
    property int clipIndex: -1
    property real clipStartSeconds: 0
    property real clipDurationSeconds: 0

    width: 1100
    height: 720
    minimumWidth: 780
    minimumHeight: 520
    title: qsTr("Cut out subject")
    color: Theme.appBackground

    function openFor(track, clip, startSeconds, durationSeconds, sessionAlreadyStarted) {
        root.trackIndex = track
        root.clipIndex = clip
        root.clipStartSeconds = startSeconds
        root.clipDurationSeconds = durationSeconds
        frameSlider.value = 0
        if (!sessionAlreadyStarted)
            EditorState.beginSegmentationSession(track, clip, startSeconds)
        root.show()
        root.raise()
        root.requestActivate()
    }

    onClosing: EditorState.endSegmentationSession()

    // Rebuilt rather than bound: segmentationBackends() and rvmQualities() are plain calls, so
    // nothing would re-evaluate them when an addon is installed while this window is open.
    property var backendModel: []
    property var qualityModel: []

    function refreshBackends() {
        const installed = EditorState.segmentationBackends()
        const backends = []
        if (installed.indexOf("sam2") >= 0)
            backends.push({ label: qsTr("Anything (click to pick)"), value: "sam2" })
        if (installed.indexOf("rvm") >= 0)
            backends.push({ label: qsTr("People (automatic)"), value: "rvm" })
        root.backendModel = backends

        const qualities = []
        const variants = EditorState.rvmQualities()
        for (let i = 0; i < variants.length; ++i) {
            qualities.push({
                label: variants[i] === "resnet50" ? qsTr("Best quality (slower)") : qsTr("Fast"),
                value: variants[i]
            })
        }
        root.qualityModel = qualities
    }

    Component.onCompleted: root.refreshBackends()

    Connections {
        target: Addons
        function onKindChanged(kind) {
            if (kind === "sam2-model" || kind === "rvm-model")
                root.refreshBackends()
        }
    }

    Connections {
        target: EditorState
        function onSegmentationFinished(ok, message) {
            if (ok)
                root.close()
        }
        // The session corrects a remembered backend that is no longer installed, so the control
        // follows the session rather than the other way round.
        function onSegmentSessionChanged() {
            backendBox.currentIndex = backendBox.indexOfValue(EditorState.segmentBackend)
        }
    }

    Row {
        anchors.fill: parent
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingLg

        // ----- Frame + prompt overlay ------------------------------------------------------
        Column {
            width: parent.width - sidebar.width - Theme.spacingLg
            height: parent.height
            spacing: Theme.spacingMd

            Item {
                id: stage
                width: parent.width
                height: parent.height - scrubRow.height - Theme.spacingMd

                readonly property real frameW: EditorState.segmentFrameSize.width
                readonly property real frameH: EditorState.segmentFrameSize.height
                readonly property real aspect: frameH > 0 ? frameW / frameH : 16 / 9

                // Letterboxed fit, and the single source of truth for mapping clicks back into
                // normalized frame coordinates.
                readonly property real fitW: Math.min(width, height * aspect)
                readonly property real fitH: fitW / aspect
                readonly property real fitX: (width - fitW) / 2
                readonly property real fitY: (height - fitH) / 2

                Rectangle {
                    anchors.fill: parent
                    color: Theme.panelBackground
                    radius: Theme.radiusMd
                }

                Image {
                    id: frameImage
                    x: stage.fitX
                    y: stage.fitY
                    width: stage.fitW
                    height: stage.fitH
                    fillMode: Image.Stretch
                    cache: false
                    // The revision defeats QML's URL-keyed image cache; the pixels behind these
                    // URLs change on every prompt edit.
                    source: EditorState.segmentSessionActive
                            ? "image://segment/frame?rev=" + EditorState.segmentRevision
                            : ""
                }

                Image {
                    id: maskImage
                    x: stage.fitX
                    y: stage.fitY
                    width: stage.fitW
                    height: stage.fitH
                    fillMode: Image.Stretch
                    cache: false
                    opacity: 0.45
                    source: EditorState.segmentSessionActive
                            ? "image://segment/mask?rev=" + EditorState.segmentRevision
                            : ""
                }

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    enabled: EditorState.segmentSessionActive && EditorState.segmentBackendUsesPoints
                             && !EditorState.segmentEncoding && !EditorState.segmenting
                    onClicked: function (mouse) {
                        const nx = (mouse.x - stage.fitX) / stage.fitW
                        const ny = (mouse.y - stage.fitY) / stage.fitH
                        if (nx < 0 || nx > 1 || ny < 0 || ny > 1)
                            return
                        EditorState.addSegmentationPoint(nx, ny, mouse.button === Qt.LeftButton)
                    }
                }

                // Prompt markers. Green includes the subject, red carves it out; click one to drop it.
                Repeater {
                    model: EditorState.segmentBackendUsesPoints ? EditorState.segmentPoints : []
                    delegate: Rectangle {
                        required property int index
                        required property var modelData

                        width: 14
                        height: 14
                        radius: 7
                        x: stage.fitX + modelData.x * stage.fitW - 7
                        y: stage.fitY + modelData.y * stage.fitH - 7
                        color: modelData.include ? Theme.constructive : Theme.destructive
                        border.width: 2
                        border.color: Theme.primaryForeground

                        MouseArea {
                            anchors.fill: parent
                            enabled: !EditorState.segmenting
                            onClicked: EditorState.removeSegmentationPoint(parent.index)
                        }
                    }
                }

                Rectangle {
                    anchors.centerIn: parent
                    visible: EditorState.segmentEncoding
                    width: encodingLabel.width + Theme.spacingXl
                    height: encodingLabel.height + Theme.spacingLg
                    radius: Theme.radiusMd
                    color: Theme.scrimStrong

                    ThemedLabel {
                        id: encodingLabel
                        anchors.centerIn: parent
                        text: qsTr("Looking at this moment…")
                    }
                }
            }

            Row {
                id: scrubRow
                width: parent.width
                spacing: Theme.spacingMd

                ThemedLabel {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Frame")
                }

                ThemedSlider {
                    id: frameSlider
                    label: qsTr("Frame")
                    width: parent.width - 220
                    anchors.verticalCenter: parent.verticalCenter
                    from: 0
                    to: Math.max(0.001, root.clipDurationSeconds)
                    enabled: !EditorState.segmentEncoding && !EditorState.segmenting
                    // Re-encoding on every slider tick would queue seconds of work per drag.
                    onPressedChanged: {
                        if (!pressed)
                            EditorState.setSegmentationFrame(root.clipStartSeconds + value)
                    }
                }

                ThemedLabel {
                    anchors.verticalCenter: parent.verticalCenter
                    text: frameSlider.value.toFixed(2) + qsTr("s")
                }
            }
        }

        // ----- Controls --------------------------------------------------------------------
        Column {
            id: sidebar
            width: 260
            height: parent.height
            spacing: Theme.spacingLg

            ThemedLabel {
                width: parent.width
                wrapMode: Text.WordWrap
                text: EditorState.segmentBackendUsesPoints
                      ? qsTr("Left-click marks the subject, right-click marks what to exclude. Click a marker to remove it.")
                      : qsTr("Everyone in the shot is cut out automatically — there is nothing to click.")
            }

            // Only worth a control when there is a choice to make. The list is what is installed,
            // so this disappears entirely on a machine with one cutout addon.
            Column {
                width: parent.width
                spacing: Theme.spacingSm
                visible: backendBox.count > 1

                ThemedLabel { text: qsTr("Cut out") }

                ThemedComboBox {
                    id: backendBox
                    width: parent.width
                    enabled: !EditorState.segmenting && !EditorState.segmentEncoding
                    textRole: "label"
                    valueRole: "value"
                    model: root.backendModel
                    onActivated: EditorState.setSegmentationBackend(currentValue, qualityBox.currentValue || "")
                }
            }

            Column {
                width: parent.width
                spacing: Theme.spacingSm
                visible: !EditorState.segmentBackendUsesPoints && qualityBox.count > 1

                ThemedLabel { text: qsTr("Quality") }

                ThemedComboBox {
                    id: qualityBox
                    width: parent.width
                    enabled: !EditorState.segmenting && !EditorState.segmentEncoding
                    textRole: "label"
                    valueRole: "value"
                    model: root.qualityModel
                    onActivated: EditorState.setSegmentationBackend(EditorState.segmentBackend, currentValue)
                }
            }

            ThemedLabel {
                width: parent.width
                text: qsTr("AI: %1").arg(EditorState.segmentationModelVariant() || qsTr("not installed"))
            }

            Column {
                width: parent.width
                spacing: Theme.spacingSm

                ThemedLabel { text: qsTr("Result") }

                ThemedComboBox {
                    id: outputBox
                    width: parent.width
                    visible: !EditorState.segmentationForTemplate
                    enabled: !EditorState.segmenting
                    textRole: "label"
                    valueRole: "value"
                    model: [
                        { label: qsTr("Two clips (subject + background)"), value: "clips" },
                        { label: qsTr("Hide everything except the subject"), value: "mask" }
                    ]
                }

                ThemedLabel {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    visible: EditorState.segmentationForTemplate
                    text: qsTr("The cutout is only for this effect — no extra tracks are added.")
                }
            }

            ThemedButton {
                width: parent.width
                visible: EditorState.segmentBackendUsesPoints
                variant: "secondary"
                text: qsTr("Clear points")
                enabled: EditorState.segmentPoints.length > 0 && !EditorState.segmenting
                onClicked: EditorState.clearSegmentationPoints()
            }

            ThemedButton {
                width: parent.width
                variant: "primary"
                text: EditorState.segmenting
                      ? qsTr("Cutting out… %1%").arg(Math.round(EditorState.segmentProgress * 100))
                      : (EditorState.segmentationForTemplate
                         ? qsTr("Cut out & apply effect")
                         : qsTr("Cut out subject"))
                enabled: (!EditorState.segmentBackendUsesPoints
                          || EditorState.segmentPoints.length > 0)
                         && !EditorState.segmenting && !EditorState.segmentEncoding
                onClicked: EditorState.runSegmentationSession(
                    EditorState.segmentationForTemplate ? "template" : outputBox.currentValue)
            }

            Column {
                width: parent.width
                spacing: Theme.spacingSm
                visible: EditorState.segmenting

                LabelledProgressRing {
                    anchors.horizontalCenter: parent.horizontalCenter
                    value: EditorState.segmentProgress
                    indeterminate: EditorState.segmentProgress <= 0
                }

                ThemedLabel {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: EditorState.segmentStatus
                }

                ThemedButton {
                    width: parent.width
                    variant: "destructive"
                    text: qsTr("Cancel")
                    onClicked: EditorState.cancelSegmentation()
                }
            }

            ThemedLabel {
                width: parent.width
                wrapMode: Text.WordWrap
                visible: !EditorState.segmenting
                text: qsTr("Each moment is processed, so longer clips take longer.")
            }
        }
    }
}
