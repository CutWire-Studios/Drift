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
    property string linkedPropKey: ""   // partner property whose keyframes stay in lockstep (e.g. x with y)
    // The phone shell opens easing in a bottom sheet instead of the popup.
    property bool useSheet: ShellBridge.phoneShell

    // When true, the value is edited through a slider + readout instead of a
    // numeric text field. Drags are coalesced into a single undo entry via
    // begin/commit preview drag.
    property bool useSlider: false
    property real sliderFrom: 0
    property real sliderTo: 1
    // Snaps the slider to multiples of this (whole-number effect params); 0 is continuous.
    property real sliderStep: 0
    property var sliderGradient: []
    // Static values can be dragged during playback. Keyed ones (or any with Auto keyframes on)
    // stay locked: the write lands at the moving playhead, dropping a key per move.
    property bool editableWhilePlaying: false
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
    // Set while another control (a preview handle, a twin slider) is dragging this property.
    // Those drags skip tracksChanged, so propDef.def is stale until they commit and the value
    // has to come from the engine.
    property bool liveSynced: false

    readonly property var activeKey: {
        void valueRevision
        return keyframeAtPlayhead()
    }
    readonly property real currentValue: {
        void valueRevision
        void liveSynced
        void keyframeList
        return valueAtPlayhead()
    }
    readonly property real displayedValue: editing ? liveValue : currentValue

    function bumpValue() {
        liveSynced = false
        valueRevision++
        syncEditors()
    }

    function matchesKey(keys) {
        const key = propDef.key
        for (let i = 0; i < keys.length; ++i) {
            const k = keys[i]
            if (k === key || (k.endsWith(".*") && key.startsWith(k.slice(0, -1))))
                return true
        }
        return false
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
    //
    // The playhead is read only past the early returns, and as the inspector's throttled copy:
    // a binding depends on what it actually read, so a row with no keys never re-evaluates for
    // playback at all.
    function valueAtPlayhead() {
        if (!liveSynced && (!keyframeList || keyframeList.length === 0))
            return propDef.def
        return EditorState.propertyValueAt(EditorState.selectedTrack, EditorState.selectedClip,
                                           propDef.key, EditorState.inspectorPlayheadSeconds,
                                           propDef.def)
    }

    function keyframeAtPlayhead() {
        if (!keyframeList || keyframeList.length === 0)
            return null

        const t = EditorState.inspectorPlayheadSeconds
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

    // Value per pixel of scrub and per arrow/wheel nudge. A continuous slider crosses its range
    // in about 200 px; a typed field moves by its last displayed digit.
    readonly property real scrubStep: {
        const precision = percent ? 0.01 : Math.pow(10, -propDef.decimals)
        if (!useSlider)
            return precision
        return sliderStep > 0 ? sliderStep : Math.max(precision, (sliderTo - sliderFrom) / 200)
    }
    readonly property bool scrubLocked: EditorState.playing
                                        && (!editableWhilePlaying || animated || EditorState.autoKeyEnabled)
    property real scrubStart: 0

    function limitValue(v) {
        if (useSlider) {
            v = Math.max(sliderFrom, Math.min(sliderTo, v))
            if (sliderStep > 0)
                v = sliderFrom + Math.round((v - sliderFrom) / sliderStep) * sliderStep
        }
        return Number(v.toFixed(percent ? 2 : propDef.decimals))
    }

    function beginScrub() {
        editing = true
        liveValue = currentValue
        scrubStart = currentValue
        EditorState.showKeyframeGraphProperty(propDef.key)
        EditorState.beginPreviewDrag(qsTr("Edit %1").arg(propDef.label))
    }

    function scrubTo(steps) {
        liveValue = limitValue(scrubStart + steps * scrubStep)
        if (!useSlider)
            valueField.text = formatValue(liveValue)
        EditorState.previewSetClipKeyframe(EditorState.selectedTrack, EditorState.selectedClip,
                                           propDef.key, EditorState.playheadSeconds, liveValue)
    }

    function endScrub() {
        EditorState.commitPreviewDrag()
        editing = false
        syncEditors()
    }

    // Arrow keys and the wheel while a field is focused: step from what is typed and commit.
    function nudge(field, steps) {
        const typed = parseInput(field.text)
        const v = limitValue((isNaN(typed) ? currentValue : typed) + steps * scrubStep)
        liveValue = v
        field.text = formatValue(v)
        commitValue(v)
    }

    function commitValue(v) {
        if (isNaN(v))
            return
        EditorState.showKeyframeGraphProperty(root.propDef.key)
        EditorState.setClipKeyframe(
            EditorState.selectedTrack, EditorState.selectedClip, root.propDef.key,
            EditorState.playheadSeconds, v)
    }

    // Read from the engine: the segment's easing is not a field on the key, and the preset
    // match is what the button's tooltip names.
    readonly property var easingState: {
        void valueRevision
        if (!activeKey || keyframeList.length < 2)
            return ({})
        return EditorState.keyframeSegmentEasing(EditorState.selectedTrack, EditorState.selectedClip,
                                                 propDef.key, activeKey.seconds)
    }
    readonly property string easingLabel: {
        void valueRevision
        const id = easingState.presetId
        if (!id)
            return qsTr("Custom")
        const presets = EditorState.easingPresets()
        for (let i = 0; i < presets.length; ++i) {
            if (presets[i].id === id)
                return presets[i].label
        }
        return qsTr("Custom")
    }

    function openEasingPicker() {
        EditorState.showKeyframeGraphProperty(root.propDef.key)
        if (root.useSheet) {
            ShellBridge.easingSheetRequested(EditorState.selectedTrack, EditorState.selectedClip,
                                             root.propDef.key, root.propDef.label, activeKey.seconds)
            return
        }
        easingPopupLoader.active = true
        easingPopupLoader.item.openFor(EditorState.selectedTrack, EditorState.selectedClip,
                                       root.propDef.key, activeKey.seconds)
    }

    readonly property bool hasPrevKeyframe: {
        if (!keyframeList || keyframeList.length === 0)
            return false
        const t = EditorState.inspectorPlayheadSeconds
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
        const t = EditorState.inspectorPlayheadSeconds
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
        function onTracksChanged() { root.bumpValue() }
        function onEasingPresetsChanged() { root.valueRevision++ }
        function onClipPropertiesPreviewed(trackIndex, clipIndex, keys) {
            if (root.editing || !root.matchesKey(keys))
                return
            root.liveSynced = true
            root.valueRevision++
            root.syncEditors()
        }
    }

    Component.onDestruction: {
        if (valueSlider.pressed)
            EditorState.commitPreviewDrag()
    }

    Component.onCompleted: syncEditors()
    onKeyframeListChanged: bumpValue()
    onCurrentValueChanged: syncEditors()

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
                width: Math.max(24, parent.width - diamond.width
                                      - (easingButton.visible ? easingButton.width + parent.spacing : 0)
                                      - parent.spacing)

                Text {
                    id: labelText
                    text: root.propDef.label
                    color: root.animated && root.keyframesEnabled ? Theme.c.propertyKeyframeRow.foreground
                                                                  : Theme.c.propertyKeyframeRow.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    font.weight: root.animated && root.keyframesEnabled ? Font.Medium : Font.Normal
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    elide: Text.ElideRight
                    width: Math.min(implicitWidth + 2, parent.width)
                }
            }

            Item {
                id: easingButton
                readonly property real buttonSize: Theme.touchInput ? 28 : 22
                visible: root.activeKey !== null && root.keyframeList.length >= 2
                anchors.verticalCenter: parent.verticalCenter
                width: buttonSize
                height: buttonSize

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.c.propertyKeyframeRow.radius
                    color: easingArea.containsMouse ? Theme.c.propertyKeyframeRow.backgroundHover
                                                    : "transparent"

                    Behavior on color {
                        ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                    }
                }

                EasingThumb {
                    anchors.centerIn: parent
                    width: easingButton.buttonSize - 4
                    height: easingButton.buttonSize - 4
                    showBackground: false
                    samples: root.easingState.samples ? root.easingState.samples : []
                    curveColor: root.graphColor
                }

                MouseArea {
                    id: easingArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.openEasingPicker()
                    onWheel: (wheel) => { wheel.accepted = false }
                }

                ThemedToolTip {
                    text: qsTr("Easing: %1 — click to change").arg(root.easingLabel)
                    visible: easingArea.containsMouse
                }

                Loader {
                    id: easingPopupLoader
                    active: false
                    sourceComponent: EasingPickerPopup {
                        anchorItem: easingButton
                        propLabel: root.propDef.label
                    }
                }
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
                Keys.onUpPressed: event => root.nudge(valueField, event.modifiers & Qt.ShiftModifier ? 10 : 1)
                Keys.onDownPressed: event => root.nudge(valueField, event.modifiers & Qt.ShiftModifier ? -10 : -1)

                ScrubArea {
                    anchors.fill: parent
                    enabled: !root.scrubLocked
                    editing: valueField.activeFocus
                    onScrubStarted: root.beginScrub()
                    onScrubbed: steps => root.scrubTo(steps)
                    onScrubFinished: root.endScrub()
                    onActivated: {
                        valueField.forceActiveFocus()
                        valueField.selectAll()
                    }
                    onWheelStepped: steps => root.nudge(valueField, steps)
                }
            }

            Text {
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                visible: root.unit.length > 0 || root.percent
                text: root.percent ? "%" : root.unit
                color: Theme.c.propertyKeyframeRow.foregroundMuted
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
                lockWhilePlaying: !root.editableWhilePlaying || root.animated || EditorState.autoKeyEnabled
                label: root.propDef.label
                width: parent.width - readoutBox.width - parent.spacing
                anchors.verticalCenter: parent.verticalCenter
                from: root.sliderFrom
                to: root.sliderTo
                stepSize: root.sliderStep
                // Keep the playhead/model binding off while pressed — same as
                // PreviewPanel scrub — so preview ticks cannot fight the drag.
                // displayedValue, so a scrub on the readout moves the thumb along with it.
                Binding on value {
                    when: !valueSlider.pressed
                    value: root.displayedValue
                }
                Canvas {
                    id: gradientTrack
                    x: valueSlider.leftPadding
                    y: valueSlider.topPadding + valueSlider.availableHeight / 2 - height / 2
                    width: valueSlider.availableWidth
                    height: Theme.spacingSm
                    z: -0.5
                    visible: root.sliderGradient.length >= 2
                    onWidthChanged: requestPaint()
                    onHeightChanged: requestPaint()
                    Connections {
                        target: root
                        function onSliderGradientChanged() { gradientTrack.requestPaint() }
                    }
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.reset()
                        const stops = root.sliderGradient
                        if (!stops || stops.length < 2 || width < 2)
                            return
                        const g = ctx.createLinearGradient(0, 0, width, 0)
                        for (let i = 0; i < stops.length; ++i)
                            g.addColorStop(i / (stops.length - 1), String(stops[i]))
                        const r = height / 2
                        ctx.fillStyle = g
                        ctx.beginPath()
                        ctx.moveTo(r, 0)
                        ctx.lineTo(width - r, 0)
                        ctx.arc(width - r, r, r, -Math.PI / 2, Math.PI / 2)
                        ctx.lineTo(r, height)
                        ctx.arc(r, r, r, Math.PI / 2, 3 * Math.PI / 2)
                        ctx.closePath()
                        ctx.fill()
                    }
                }
                onMoved: {
                    root.liveValue = value
                    // Keyboard nudges arrive without a press.
                    if (!pressed)
                        EditorState.showKeyframeGraphProperty(root.propDef.key)
                    EditorState.previewSetClipKeyframe(
                        EditorState.selectedTrack, EditorState.selectedClip, root.propDef.key,
                        EditorState.playheadSeconds, value)
                }
                onPressedChanged: {
                    if (pressed) {
                        root.editing = true
                        root.liveValue = value
                        EditorState.showKeyframeGraphProperty(root.propDef.key)
                        EditorState.beginPreviewDrag(qsTr("Edit %1").arg(root.propDef.label))
                    } else {
                        EditorState.commitPreviewDrag()
                        root.editing = false
                    }
                }
            }

            // The readout scrubs like a number field: drag to adjust, click to type an exact
            // value in place (a slider cannot land on one). Enter or clicking away commits,
            // Escape discards.
            Item {
                id: readoutBox
                // The dB readout's width swings with the value ("0.0 dB (100%)" vs "-∞ dB
                // (0%)"), and the slider is sized off this — so hold a floor wide enough for
                // the longest form, or the track resizes under the thumb mid-drag.
                width: Math.max(root.decibels ? 112 : 48,
                                readout.implicitWidth + Theme.spacingMd * 2)
                height: Theme.controlHeightSm
                anchors.verticalCenter: parent.verticalCenter

                property bool typing: false

                function startTyping() {
                    editField.text = root.formatValue(root.currentValue)
                    typing = true
                    editField.forceActiveFocus()
                    editField.selectAll()
                }

                function finishTyping(apply) {
                    if (!typing)
                        return
                    typing = false
                    if (apply) {
                        const v = root.parseInput(editField.text)
                        if (!isNaN(v))
                            root.commitValue(root.limitValue(v))
                    }
                }

                Rectangle {
                    anchors.fill: parent
                    visible: !readoutBox.typing
                    radius: Theme.c.propertyKeyframeRow.radius
                    color: readoutArea.scrubbing
                           ? Theme.c.propertyKeyframeRow.backgroundHover : "transparent"

                    Behavior on color {
                        ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                    }
                }

                Text {
                    id: readout
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingMd
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !readoutBox.typing
                    horizontalAlignment: Text.AlignRight
                    text: root.sliderReadout(root.displayedValue)
                    color: Theme.c.propertyKeyframeRow.foreground
                    font.family: Theme.monoFontFamily
                    font.pixelSize: Theme.fontSizeSm
                }

                ThemedTextField {
                    id: editField
                    anchors.fill: parent
                    visible: readoutBox.typing
                    implicitHeight: Theme.controlHeightSm
                    topPadding: 0
                    bottomPadding: 0
                    leftPadding: Theme.spacingMd
                    rightPadding: Theme.spacingMd
                    horizontalAlignment: TextInput.AlignRight
                    verticalAlignment: TextInput.AlignVCenter
                    onAccepted: readoutBox.finishTyping(true)
                    onActiveFocusChanged: if (!activeFocus)
                        readoutBox.finishTyping(true)
                    Keys.onEscapePressed: readoutBox.finishTyping(false)
                    Keys.onUpPressed: event => root.nudge(editField, event.modifiers & Qt.ShiftModifier ? 10 : 1)
                    Keys.onDownPressed: event => root.nudge(editField, event.modifiers & Qt.ShiftModifier ? -10 : -1)
                }

                ScrubArea {
                    id: readoutArea
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: valueSlider.enabled
                    editing: readoutBox.typing
                    onScrubStarted: root.beginScrub()
                    onScrubbed: steps => root.scrubTo(steps)
                    onScrubFinished: root.endScrub()
                    onActivated: readoutBox.startTyping()
                    onWheelStepped: steps => root.nudge(editField, steps)
                }

                ThemedToolTip {
                    text: qsTr("Drag to adjust, click to type an exact %1").arg(root.propDef.label.toLowerCase())
                    visible: readoutArea.containsMouse && !readoutBox.typing && !readoutArea.scrubbing
                }
            }
        }
    }
}
