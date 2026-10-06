import QtQuick
import QtQuick.Controls.Basic
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
    readonly property bool canTrack: !!(clipData && clipData.canObjectTrack)
    readonly property bool canFollow: !!(clipData && clipData.canFollowObject)
    readonly property bool hasTrack: !!(clipData && clipData.hasObjectTrack)
    readonly property bool stale: !!(clipData && clipData.objectTrackStale)
    readonly property bool locked: !!(clipData && clipData.objectLockApplied)
    readonly property bool lockStale: !!(clipData && clipData.objectLockStale)
    readonly property bool trackingThis: EditorState.objectTracking
                                         && !!clipData
                                         && EditorState.objectTrackingClipId === clipData.id

    readonly property var followRows: {
        void clipDataRevision
        return EditorState.objectTrackTargets()
    }
    readonly property var followIds: {
        const ids = [""]
        for (let i = 0; i < followRows.length; ++i)
            ids.push(followRows[i].id)
        return ids
    }
    readonly property var followLabels: {
        const labels = [qsTr("None")]
        for (let i = 0; i < followRows.length; ++i)
            labels.push(followRows[i].name || qsTr("Video"))
        return labels
    }

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

        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            visible: root.canTrack
            text: qsTr("Draw a circle on the thing you want to follow, then track it. Drift can zoom the shot so that point stays put, or pin text and stickers so they move with it.")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        Column {
            width: parent.width
            spacing: Theme.spacingSm
            visible: root.canTrack

            Text {
                text: qsTr("Circle")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                font.weight: Font.Medium
            }

            ThemedSlider {
                id: seedXSlider
                label: qsTr("Horizontal")
                width: parent.width
                from: 0
                to: 1
                stepSize: 0.005
                enabled: !root.trackingThis
                valueFormatter: function (v) { return Math.round(v * 100) + "%" }
                Binding on value {
                    when: !seedXSlider.pressed
                    value: (root.clipData && root.clipData.objectTrackX !== undefined)
                           ? root.clipData.objectTrackX : 0.5
                }
                onPressedChanged: if (!pressed) root.commitSeed()
            }

            ThemedSlider {
                id: seedYSlider
                label: qsTr("Vertical")
                width: parent.width
                from: 0
                to: 1
                stepSize: 0.005
                enabled: !root.trackingThis
                valueFormatter: function (v) { return Math.round(v * 100) + "%" }
                Binding on value {
                    when: !seedYSlider.pressed
                    value: (root.clipData && root.clipData.objectTrackY !== undefined)
                           ? root.clipData.objectTrackY : 0.5
                }
                onPressedChanged: if (!pressed) root.commitSeed()
            }

            ThemedSlider {
                id: radiusSlider
                label: qsTr("Radius")
                width: parent.width
                from: 0.03
                to: 0.4
                stepSize: 0.005
                enabled: !root.trackingThis
                valueFormatter: function (v) { return Math.round(v * 100) + "%" }
                Binding on value {
                    when: !radiusSlider.pressed
                    value: (root.clipData && root.clipData.objectTrackRadius !== undefined)
                           ? root.clipData.objectTrackRadius : 0.12
                }
                onPressedChanged: if (!pressed) root.commitSeed()
            }

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("You can also drag the circle on the preview. Park the playhead on a frame where the object is easy to see before tracking.")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
        }

        Column {
            width: parent.width
            spacing: Theme.spacingSm
            visible: root.canTrack

            Text {
                text: qsTr("Framing")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                font.weight: Font.Medium
            }

            ThemedSlider {
                id: zoomSlider
                label: qsTr("Zoom")
                width: parent.width
                from: 1
                to: 4
                stepSize: 0.05
                enabled: !root.trackingThis
                valueFormatter: function (v) { return v.toFixed(2) + "×" }
                Binding on value {
                    when: !zoomSlider.pressed
                    value: (root.clipData && root.clipData.objectTrackZoom !== undefined)
                           ? root.clipData.objectTrackZoom : 2
                }
                onPressedChanged: {
                    if (!pressed && root.clipData) {
                        EditorState.setObjectTrackZoom(
                                    EditorState.selectedTrack, EditorState.selectedClip, value)
                    }
                }
            }

            ThemedSlider {
                id: holdXSlider
                label: qsTr("Keep it horizontally")
                width: parent.width
                from: 0
                to: 1
                stepSize: 0.01
                enabled: !root.trackingThis
                valueFormatter: function (v) { return Math.round(v * 100) + "%" }
                Binding on value {
                    when: !holdXSlider.pressed
                    value: (root.clipData && root.clipData.objectTrackHoldX !== undefined)
                           ? root.clipData.objectTrackHoldX : 0.5
                }
                onPressedChanged: if (!pressed) root.commitHold()
            }

            ThemedSlider {
                id: holdYSlider
                label: qsTr("Keep it vertically")
                width: parent.width
                from: 0
                to: 1
                stepSize: 0.01
                enabled: !root.trackingThis
                valueFormatter: function (v) { return Math.round(v * 100) + "%" }
                Binding on value {
                    when: !holdYSlider.pressed
                    value: (root.clipData && root.clipData.objectTrackHoldY !== undefined)
                           ? root.clipData.objectTrackHoldY : 0.5
                }
                onPressedChanged: if (!pressed) root.commitHold()
            }

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("Zoom 1 shows the whole frame, so there is no room to pan. Higher zoom crops in and slides the picture to hold the object. Near the edge it stays as close as it can without showing empty background. Applying replaces the clip's position and size keys.")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
        }

        Row {
            width: parent.width
            spacing: 8
            visible: root.canTrack && root.stale && !root.trackingThis

            IconGlyph {
                glyph: Theme.icons.warning
                iconSize: Theme.iconSizeMd
                iconColor: Theme.warning
                anchors.verticalCenter: parent.verticalCenter
            }

            Text {
                width: parent.width - Theme.iconSizeMd - parent.spacing
                wrapMode: Text.WordWrap
                text: qsTr("The circle has moved since the last track. Track again before locking the frame.")
                color: Theme.warning
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        Row {
            width: parent.width
            spacing: 8
            visible: root.canTrack && !root.trackingThis

            ThemedButton {
                text: root.hasTrack ? qsTr("Track again") : qsTr("Track object")
                variant: (!root.hasTrack || root.stale) ? "primary" : "secondary"
                onClicked: EditorState.trackObjectForClip(
                               EditorState.selectedTrack, EditorState.selectedClip)
            }

            ThemedButton {
                text: root.locked ? qsTr("Update framing") : qsTr("Keep in frame")
                variant: (root.hasTrack && !root.stale && (!root.locked || root.lockStale))
                         ? "primary" : "secondary"
                enabled: root.hasTrack && !root.stale
                onClicked: EditorState.applyObjectLock(
                               EditorState.selectedTrack, EditorState.selectedClip)
            }
        }

        Row {
            width: parent.width
            spacing: 8
            visible: root.canTrack && !root.trackingThis && (root.hasTrack || root.locked)

            ThemedButton {
                text: qsTr("Clear track")
                variant: "ghost"
                visible: root.hasTrack
                onClicked: EditorState.clearObjectTrack(
                               EditorState.selectedTrack, EditorState.selectedClip)
            }

            ThemedButton {
                text: qsTr("Remove framing")
                variant: "ghost"
                visible: root.locked
                onClicked: EditorState.removeObjectLock(
                               EditorState.selectedTrack, EditorState.selectedClip)
            }
        }

        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            visible: root.trackingThis
            text: EditorState.objectTrackStatus || qsTr("Tracking…")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        ThemedProgressBar {
            visible: root.trackingThis
            width: parent.width
            value: EditorState.objectTrackProgress
        }

        ThemedButton {
            visible: root.trackingThis
            width: parent.width
            text: qsTr("Cancel")
            variant: "ghost"
            onClicked: EditorState.cancelObjectTracking()
        }

        Column {
            width: parent.width
            spacing: Theme.spacingSm
            visible: root.canFollow

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("Pin this clip to an object that has already been tracked. Its centre follows that point. Position keys are left alone and take over again when you unpin.")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            Text {
                text: qsTr("Follow")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                font.weight: Font.Medium
            }

            ThemedComboBox {
                width: parent.width
                model: root.followLabels
                currentIndex: {
                    const id = root.clipData ? (root.clipData.objectFollowClipId || "") : ""
                    const at = root.followIds.indexOf(id)
                    return at < 0 ? 0 : at
                }
                enabled: root.followRows.length > 0 || (root.clipData && root.clipData.objectFollowClipId)
                onActivated: (index) => {
                    const id = root.followIds[index] || ""
                    EditorState.setObjectFollowClip(
                                EditorState.selectedTrack, EditorState.selectedClip, id)
                }
            }

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                visible: root.followRows.length === 0
                text: qsTr("Track an object on a video clip first. It will show up here.")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
        }
    }

    function commitSeed() {
        if (!root.clipData)
            return
        EditorState.setObjectTrackSeed(
                    EditorState.selectedTrack, EditorState.selectedClip,
                    seedXSlider.value, seedYSlider.value, radiusSlider.value)
    }

    function commitHold() {
        if (!root.clipData)
            return
        EditorState.setObjectTrackHold(
                    EditorState.selectedTrack, EditorState.selectedClip,
                    holdXSlider.value, holdYSlider.value)
    }
}
