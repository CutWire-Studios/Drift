import QtQuick
import QtQuick.Controls.Basic
import Drift

// One animatable-property control for the clip inspector: keyframe diamond +
// label on top, value field / slider below. Tracks playhead and project edits
// (including WYSIWYG preview drags) so the readout stays live.
Column {
    id: root

    required property var propDef       // {key, label, def, decimals}
    required property var keyframeList  // [{seconds, value}, ...] for this property
    // Easing is a property of the key at the playhead, not of the whole track. Hand-dragged
    // tangents match no preset, which is what leaves every chip unlit.
    readonly property string interpolationMode:
        (activeKey && !activeKey.custom) ? (activeKey.easing || "linear") : ""

    // When true, the value is edited through a slider + readout instead of a
    // numeric text field. Drags are coalesced into a single undo entry via
    // begin/commit preview drag.
    property bool useSlider: false
    property real sliderFrom: 0
    property real sliderTo: 1
    property bool percent: false
    property string unit: "" // e.g. "°" — appended to the numeric readout
    // Show the slider readout in decibels with the percentage after it, for gain properties
    // where dB is the scale the value is actually read on. Only meaningful with `percent`,
    // and deliberately display-only: the stored value, the slider taper and typed input all
    // stay linear amplitude.
    property bool decibels: false

    // An animated property is tinted with its curve colour so the row, its chip and its curve on
    // the keyframe strip read as one series. Clicking the label switches the animation off without
    // discarding the keys — the property then holds its first key's value.
    readonly property bool animated: !!keyframeList && keyframeList.length > 0
    readonly property bool keyframesEnabled: {
        void valueRevision
        return EditorState.clipPropertyKeyframesEnabled(
                   EditorState.selectedTrack, EditorState.selectedClip, propDef.key)
    }
    readonly property color graphColor: Theme.keyframeCurveColor(propDef.key)

    spacing: 4

    // Bumped whenever external state that affects the displayed value changes.
    // TextField/Slider bindings are fragile once the user interacts; this forces
    // a refresh from WYSIWYG preview drags and playhead moves.
    property int valueRevision: 0
    property bool editing: false
    property real liveValue: 0

    readonly property var activeKey: {
        void valueRevision
        return keyframeAtPlayhead()
    }
    readonly property real currentValue: {
        void valueRevision
        void keyframeList
        void EditorState.playheadSeconds
        return valueAtPlayhead()
    }
    readonly property real displayedValue: editing ? liveValue : currentValue

    function bumpValue() {
        valueRevision++
        syncEditors()
    }

    function syncEditors() {
        if (editing)
            return
        // Slider value tracks currentValue via Binding when !pressed; assigning
        // here would break that Binding the same way a live `value:` does.
        if (!useSlider && !valueField.activeFocus)
            valueField.text = formatValue(currentValue)
    }

    function formatValue(v) {
        if (percent)
            return Math.round(v * 100).toString()
        return Number(v).toFixed(propDef.decimals)
    }

    function parseInput(text) {
        const v = parseFloat(text)
        if (isNaN(v))
            return NaN
        return percent ? v / 100 : v
    }

    function displayReadout(v) {
        if (percent)
            return Math.round(v * 100) + "%"
        const body = Number(v).toFixed(propDef.decimals)
        return unit.length > 0 ? body + unit : body
    }

    function formatDb(v) {
        if (v <= 0)
            return "-∞ dB"
        const db = 20 * Math.log10(v)
        return (db > 0.05 ? "+" : "") + db.toFixed(1) + " dB"
    }

    // The slider's own readout. Kept separate from displayReadout because the typed-value
    // popup uses that one for its range label, where "-∞ dB (0%)–+6.0 dB (200%)" is unreadable.
    function sliderReadout(v) {
        if (!decibels)
            return displayReadout(v)
        return formatDb(v) + " (" + Math.round(v * 100) + "%)"
    }

    // Asks the engine rather than reimplementing evaluateAt here. This used to be a JS mirror
    // of the interpolation math that had to be kept in sync with Keyframe.h by hand — which
    // stopped being tractable once keys grew individual bezier tangents.
    function valueAtPlayhead() {
        if (!keyframeList || keyframeList.length === 0)
            return propDef.def
        return EditorState.propertyValueAt(EditorState.selectedTrack, EditorState.selectedClip,
                                           propDef.key, EditorState.playheadSeconds, propDef.def)
    }

    function keyframeAtPlayhead() {
        if (!keyframeList)
            return null

        const t = EditorState.playheadSeconds
        const tolerance = 1 / 30
        let best = null
        for (let i = 0; i < keyframeList.length; ++i) {
            const kf = keyframeList[i]
            const delta = Math.abs(kf.seconds - t)
            if (delta <= tolerance && (!best || delta < Math.abs(best.seconds - t)))
                best = kf
        }
        return best
    }

    function commitValue(v) {
        if (isNaN(v))
            return
        EditorState.showKeyframeGraphProperty(root.propDef.key)
        EditorState.setClipKeyframe(
            EditorState.selectedTrack, EditorState.selectedClip, root.propDef.key,
            EditorState.playheadSeconds, v)
    }

    function setInterpolation(mode) {
        EditorState.showKeyframeGraphProperty(root.propDef.key)
        EditorState.setKeyframeInterpolation(
            EditorState.selectedTrack, EditorState.selectedClip, root.propDef.key, mode)
    }

    readonly property bool hasPrevKeyframe: {
        if (!keyframeList || keyframeList.length === 0)
            return false
        const t = EditorState.playheadSeconds
        const tolerance = 1 / 30
        for (let i = 0; i < keyframeList.length; ++i) {
            if (keyframeList[i].seconds < t - tolerance)
                return true
        }
        return false
    }

    readonly property bool hasNextKeyframe: {
        if (!keyframeList || keyframeList.length === 0)
            return false
        const t = EditorState.playheadSeconds
        const tolerance = 1 / 30
        for (let i = 0; i < keyframeList.length; ++i) {
            if (keyframeList[i].seconds > t + tolerance)
                return true
        }
        return false
    }

    function goToPrevKeyframe() {
        if (!keyframeList || keyframeList.length === 0)
            return
        const t = EditorState.playheadSeconds
        const tolerance = 1 / 30
        let targetKf = null
        for (let i = keyframeList.length - 1; i >= 0; --i) {
            const kf = keyframeList[i]
            if (kf.seconds < t - tolerance) {
                targetKf = kf
                break
            }
        }
        if (targetKf) {
            EditorState.playheadSeconds = targetKf.seconds
        }
    }

    function goToNextKeyframe() {
        if (!keyframeList || keyframeList.length === 0)
            return
        const t = EditorState.playheadSeconds
        const tolerance = 1 / 30
        let targetKf = null
        for (let i = 0; i < keyframeList.length; ++i) {
            const kf = keyframeList[i]
            if (kf.seconds > t + tolerance) {
                targetKf = kf
                break
            }
        }
        if (targetKf) {
            EditorState.playheadSeconds = targetKf.seconds
        }
    }

    Connections {
        target: EditorState
        function onSelectedClipDataChanged() { root.bumpValue() }
        function onPlayheadSecondsChanged() { root.bumpValue() }
        function onTracksChanged() { root.bumpValue() }
    }

    Component.onCompleted: syncEditors()
    onKeyframeListChanged: bumpValue()

    Item {
        width: root.width
        height: headerRow.height

        Row {
            id: headerRow
            width: parent.width
            spacing: 6

            KeyframeDiamond {
                id: diamond
                anchors.verticalCenter: parent.verticalCenter
                accentColor: root.graphColor
                animated: root.animated
                keysEnabled: root.keyframesEnabled
                tooltip: !root.animated
                         ? qsTr("%1 has no keyframes yet").arg(root.propDef.label)
                         : (root.keyframesEnabled
                            ? qsTr("Turn off %1's keyframes — they are kept, but stop animating")
                              .arg(root.propDef.label)
                            : qsTr("Turn %1's keyframes back on").arg(root.propDef.label))
                onToggled: EditorState.toggleClipPropertyKeyframesEnabled(
                               EditorState.selectedTrack, EditorState.selectedClip, root.propDef.key)
            }

            Item {
                id: labelZone
                anchors.verticalCenter: parent.verticalCenter
                height: labelText.implicitHeight
                width: Math.max(24, parent.width - diamond.width - linChip.width - easeChip.width
                                      - holdChip.width - parent.spacing * 4)

                Text {
                    id: labelText
                    text: root.propDef.label
                    color: root.animated && root.keyframesEnabled ? Theme.panelForeground
                                                                  : Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    font.weight: root.animated && root.keyframesEnabled ? Font.Medium : Font.Normal
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    elide: Text.ElideRight
                    width: Math.min(implicitWidth + 2, parent.width)
                }
            }

            ThemedChip {
                id: linChip
                text: qsTr("Straight")
                tooltip: qsTr("Straight — changes at a steady rate between keyframes")
                selected: root.interpolationMode === "linear"
                enabled: root.activeKey !== null
                chipHeight: 18
                horizontalPadding: Theme.spacingSm
                width: 28
                anchors.verticalCenter: parent.verticalCenter
                onClicked: root.setInterpolation("linear")
            }

            ThemedChip {
                id: easeChip
                text: qsTr("Ease")
                tooltip: qsTr("Ease — accelerates out and decelerates in")
                selected: root.interpolationMode === "ease"
                enabled: root.activeKey !== null
                chipHeight: 18
                horizontalPadding: Theme.spacingSm
                width: 36
                anchors.verticalCenter: parent.verticalCenter
                onClicked: root.setInterpolation("ease")
            }

            ThemedChip {
                id: holdChip
                text: qsTr("Jump")
                tooltip: qsTr("Jump — holds the value until the next keyframe")
                selected: root.interpolationMode === "hold"
                enabled: root.activeKey !== null
                chipHeight: 18
                horizontalPadding: Theme.spacingSm
                width: 34
                anchors.verticalCenter: parent.verticalCenter
                onClicked: root.setInterpolation("hold")
            }
        }
    }

    // --- Value row: add/remove a key at the playhead, then the editor ---------
    Row {
        id: valueRow
        width: root.width
        spacing: 6
        // Width left for the editor once the key button has taken its share.
        readonly property real editorWidth: width - (root.animated ? keyframeNavRow.width : singleKeyButton.width) - spacing

        Row {
            id: keyframeNavRow
            spacing: 2
            anchors.verticalCenter: parent.verticalCenter
            visible: root.animated

            IconButton {
                id: prevKeyButton
                buttonSize: 20
                iconSize: 12
                glyph: Theme.icons.chevronLeft
                tooltip: qsTr("Previous keyframe")
                enabled: root.hasPrevKeyframe
                onClicked: {
                    EditorState.showKeyframeGraphProperty(root.propDef.key)
                    root.goToPrevKeyframe()
                }
            }

            IconButton {
                id: keyButton
                buttonSize: 24
                iconSize: 16
                glyph: root.activeKey ? Theme.icons.diamondMinus : Theme.icons.diamondPlus
                tooltip: root.activeKey
                         ? qsTr("Remove %1's keyframe at the playhead").arg(root.propDef.label)
                         : qsTr("Add a keyframe for %1 at the playhead").arg(root.propDef.label)
                onClicked: {
                    EditorState.showKeyframeGraphProperty(root.propDef.key)
                    if (root.activeKey) {
                        EditorState.removeClipKeyframe(
                            EditorState.selectedTrack, EditorState.selectedClip, root.propDef.key,
                            root.activeKey.seconds)
                    } else {
                        root.commitValue(root.currentValue)
                    }
                }
            }

            IconButton {
                id: nextKeyButton
                buttonSize: 20
                iconSize: 12
                glyph: Theme.icons.chevronRight
                tooltip: qsTr("Next keyframe")
                enabled: root.hasNextKeyframe
                onClicked: {
                    EditorState.showKeyframeGraphProperty(root.propDef.key)
                    root.goToNextKeyframe()
                }
            }
        }

        IconButton {
            id: singleKeyButton
            visible: !root.animated
            anchors.verticalCenter: parent.verticalCenter
            buttonSize: 24
            iconSize: 16
            glyph: Theme.icons.diamondPlus
            tooltip: qsTr("Add a keyframe for %1 at the playhead").arg(root.propDef.label)
            onClicked: {
                EditorState.showKeyframeGraphProperty(root.propDef.key)
                root.commitValue(root.currentValue)
            }
        }

        Item {
            visible: !root.useSlider
            width: valueRow.editorWidth
            height: 30

            ThemedTextField {
                id: valueField
                anchors.fill: parent
                verticalAlignment: TextInput.AlignVCenter
                text: root.formatValue(root.currentValue)
                rightPadding: root.unit.length > 0 || root.percent ? 22 : 8
                onEditingFinished: {
                    const v = root.parseInput(text)
                    root.editing = false
                    if (!isNaN(v))
                        root.commitValue(v)
                    text = root.formatValue(root.currentValue)
                }
                onActiveFocusChanged: {
                    if (activeFocus) {
                        root.editing = true
                        root.liveValue = root.currentValue
                    } else {
                        root.editing = false
                        text = root.formatValue(root.currentValue)
                    }
                }
                onTextEdited: {
                    const v = root.parseInput(text)
                    if (!isNaN(v))
                        root.liveValue = v
                }
            }

            Text {
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                visible: root.unit.length > 0 || root.percent
                text: root.percent ? "%" : root.unit
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                z: 1
            }
        }

        // Slider (opt-in), in place of the numeric field.
        Row {
            visible: root.useSlider
            width: valueRow.editorWidth
            spacing: 8

            ThemedSlider {
                id: valueSlider
                label: root.propDef.label
                width: parent.width - readoutBox.width - parent.spacing
                anchors.verticalCenter: parent.verticalCenter
                from: root.sliderFrom
                to: root.sliderTo
                // Keep the playhead/model binding off while pressed — same as
                // PreviewPanel scrub — so preview ticks cannot fight the drag.
                Binding on value {
                    when: !valueSlider.pressed && !root.editing
                    value: root.currentValue
                }
                onMoved: {
                    root.liveValue = value
                    EditorState.showKeyframeGraphProperty(root.propDef.key)
                    EditorState.previewSetClipKeyframe(
                        EditorState.selectedTrack, EditorState.selectedClip, root.propDef.key,
                        EditorState.playheadSeconds, value)
                }
                onPressedChanged: {
                    if (pressed) {
                        root.editing = true
                        root.liveValue = value
                        EditorState.beginPreviewDrag(qsTr("Edit %1").arg(root.propDef.label))
                    } else {
                        EditorState.commitPreviewDrag()
                        root.editing = false
                    }
                }
            }

            // The readout doubles as a button: a slider cannot land on an exact
            // value, so clicking it opens a small editor to type one.
            Item {
                id: readoutBox
                // The dB readout's width swings with the value ("0.0 dB (100%)" vs "-∞ dB
                // (0%)"), and the slider is sized off this — so hold a floor wide enough for
                // the longest form, or the track resizes under the thumb mid-drag.
                width: Math.max(root.decibels ? 112 : 48,
                                readout.implicitWidth + Theme.spacingMd * 2)
                height: Theme.controlHeightSm
                anchors.verticalCenter: parent.verticalCenter

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radiusSm
                    color: readoutArea.containsMouse || valuePopup.opened
                           ? Theme.panelAccent : "transparent"

                    Behavior on color {
                        ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                    }
                }

                Text {
                    id: readout
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingMd
                    anchors.verticalCenter: parent.verticalCenter
                    horizontalAlignment: Text.AlignRight
                    text: root.sliderReadout(root.displayedValue)
                    color: Theme.panelForeground
                    font.family: Theme.monoFontFamily
                    font.pixelSize: Theme.fontSizeSm
                }

                MouseArea {
                    id: readoutArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: valuePopup.openEditor()
                    onWheel: (wheel) => { wheel.accepted = false }
                }

                ThemedToolTip {
                    text: qsTr("Click to type an exact %1").arg(root.propDef.label.toLowerCase())
                    visible: readoutArea.containsMouse && !valuePopup.opened
                }

                Popup {
                    id: valuePopup

                    // Right-aligned under the readout, which keeps it inside the
                    // inspector panel however narrow the panel is docked.
                    x: readoutBox.width - width
                    y: readoutBox.height + Theme.spacingXs
                    width: 172
                    padding: Theme.spacingLg
                    modal: false
                    dim: false
                    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

                    // Escape and clicking away discard; only Enter writes a key.
                    function openEditor() {
                        editField.text = root.formatValue(root.currentValue)
                        open()
                        editField.forceActiveFocus()
                        editField.selectAll()
                    }

                    function apply() {
                        const v = root.parseInput(editField.text)
                        if (!isNaN(v))
                            root.commitValue(Math.max(root.sliderFrom,
                                                      Math.min(root.sliderTo, v)))
                        close()
                    }

                    background: Rectangle {
                        color: Theme.panelBackground
                        border.width: Theme.borderWidth
                        border.color: Theme.panelBorder
                        radius: Theme.radiusMd
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

                    contentItem: Column {
                        spacing: Theme.spacingSm

                        Row {
                            width: parent.width

                            Text {
                                width: parent.width / 2
                                text: root.propDef.label
                                color: Theme.panelForeground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeXs
                                font.weight: Font.Medium
                                elide: Text.ElideRight
                            }

                            Text {
                                width: parent.width / 2
                                horizontalAlignment: Text.AlignRight
                                text: root.displayReadout(root.sliderFrom) + "–"
                                      + root.displayReadout(root.sliderTo)
                                color: Theme.mutedForeground
                                font.family: Theme.monoFontFamily
                                font.pixelSize: Theme.fontSizeTiny
                            }
                        }

                        Item {
                            width: parent.width
                            height: editField.height

                            ThemedTextField {
                                id: editField
                                width: parent.width
                                verticalAlignment: TextInput.AlignVCenter
                                rightPadding: root.unit.length > 0 || root.percent
                                              ? Theme.spacingLg + 14 : Theme.spacingLg
                                onAccepted: valuePopup.apply()
                            }

                            Text {
                                anchors.right: parent.right
                                anchors.rightMargin: Theme.spacingLg
                                anchors.verticalCenter: parent.verticalCenter
                                visible: root.unit.length > 0 || root.percent
                                text: root.percent ? "%" : root.unit
                                color: Theme.mutedForeground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeXs
                            }
                        }
                    }
                }
            }
        }
    }
}
