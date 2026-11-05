import QtQuick
import QtQuick.Controls.Basic
import Drift

// Dropdown for inserting an empty timeline track by type.
Popup {
    id: root

    width: 180
    padding: Theme.spacingMd
    margins: Theme.spacingSm
    modal: true
    dim: false
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    // Arrow keys move the highlight, Enter adds the highlighted track type. The
    // menu used to be mouse-only despite already handling Escape.
    property int highlightIndex: 0

    onOpened: {
        highlightIndex = 0
        // Popup itself is not an Item — Keys.* on the root fails with
        // "Could not attach Keys property". Focus the content column instead.
        contentItem.forceActiveFocus()
        Haptics.press()
    }

    // Adjustment tracks carry a kind, so they go through their own call rather than widening
    // addTrack()'s type whitelist with compound strings.
    function addTrackOfType(type) {
        // The camera lane carries a clip from the moment it is made — an empty one would frame
        // nothing — so it goes through its own call rather than addTrack's bare-lane path.
        if (type === "camera")
            EditorState.addCameraTrack()
        else if (type.indexOf("adjustment:") === 0)
            EditorState.addAdjustmentTrack(type.substring("adjustment:".length))
        else
            EditorState.addTrack(type)
    }

    function addHighlighted() {
        if (highlightIndex < 0 || highlightIndex >= trackTypes.length)
            return
        Haptics.confirm()
        root.addTrackOfType(trackTypes[highlightIndex].type)
        root.close()
    }

    // Opacity plus scale, matching ThemedContextMenu.
    enter: Transition {
        ParallelAnimation {
            NumberAnimation {
                property: "opacity"
                from: 0.0
                to: 1.0
                duration: Theme.durationBase
                easing.type: Theme.easing
            }
            NumberAnimation {
                property: "scale"
                from: 0.96
                to: 1.0
                duration: Theme.durationBase
                easing.type: Theme.easing
            }
        }
    }

    exit: Transition {
        ParallelAnimation {
            NumberAnimation {
                property: "opacity"
                from: 1.0
                to: 0.0
                duration: Theme.durationFast
                easing.type: Theme.easing
            }
            NumberAnimation {
                property: "scale"
                from: 1.0
                to: 0.96
                duration: Theme.durationFast
                easing.type: Theme.easing
            }
        }
    }

    readonly property bool hasVisualTrack: {
        const tracks = EditorState.tracks
        for (let i = 0; i < tracks.length; ++i) {
            if (["video", "text", "subtitle", "shape"].indexOf(tracks[i].type) !== -1)
                return true
        }
        return false
    }
    // A sequence has one viewer, so the camera is offered only until it exists.
    readonly property bool hasCamera: {
        const tracks = EditorState.tracks
        for (let i = 0; i < tracks.length; ++i) {
            if (tracks[i].isCameraLayer === true)
                return true
        }
        return false
    }
    readonly property var trackTypes: allTrackTypes.filter(
        (t) => (!t.needsVisualTrack || hasVisualTrack) && !(t.oncePerSequence && hasCamera))
    readonly property var allTrackTypes: [
        { type: "video", label: qsTr("Video"), icon: Theme.icons.film },
        { type: "audio", label: qsTr("Audio"), icon: Theme.icons.music },
        { type: "text", label: qsTr("Text"), icon: Theme.icons.type },
        { type: "subtitle", label: qsTr("Subtitle"), icon: Theme.icons.captions },
        { type: "shape", label: qsTr("Graphic"), icon: Theme.icons.shapes },
        // Standalone adjustment tracks. These apply to everything composited below them; drag
        // one onto a track to nest it there instead and scope it to that track alone.
        { type: "adjustment:videoEffects", label: qsTr("Adjustment"), icon: Theme.icons.wand },
        { type: "adjustment:audioEffects", label: qsTr("Audio adjustment"),
          icon: Theme.icons.audioLines },
        // A transform layer moves the tracks below it, so it is only offered once there is one.
        { type: "adjustment:transform", label: qsTr("Transform"), icon: Theme.icons.maximize,
          needsVisualTrack: true },
        // The scene camera: one viewpoint everything below is seen through. Same reasoning as the
        // transform layer for needing something to film, and only ever one per sequence.
        { type: "camera", label: qsTr("Camera"), icon: Theme.icons.video,
          needsVisualTrack: true, oncePerSequence: true },
    ]

    background: Rectangle {
        color: Theme.c.newTrackMenu.background
        border.width: Theme.c.newTrackMenu.borderWidth
        border.color: Theme.c.newTrackMenu.border
        radius: Theme.c.newTrackMenu.radius
    }

    contentItem: Column {
        spacing: 2
        focus: true

        Keys.onUpPressed: function(event) {
            root.highlightIndex = (root.highlightIndex - 1 + root.trackTypes.length)
                                  % root.trackTypes.length
            event.accepted = true
        }
        Keys.onDownPressed: function(event) {
            root.highlightIndex = (root.highlightIndex + 1) % root.trackTypes.length
            event.accepted = true
        }
        Keys.onReturnPressed: function(event) {
            root.addHighlighted()
            event.accepted = true
        }
        Keys.onEnterPressed: function(event) {
            root.addHighlighted()
            event.accepted = true
        }

        Text {
            width: parent.width
            leftPadding: 8
            rightPadding: 8
            topPadding: 4
            bottomPadding: 6
            text: qsTr("New track")
            color: Theme.c.newTrackMenu.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            font.weight: Font.Medium
        }

        Rectangle {
            width: parent.width
            height: 1
            color: Theme.c.newTrackMenu.separator
            opacity: 0.5
        }

        Repeater {
            model: root.trackTypes

            delegate: NewTrackMenuEntry {
                menu: root
            }
        }
    }
}
