import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import Drift
import ".."

Item {
    id: root

    property int clipDataRevision: 0
    property bool sourceBoxRatioLocked: true
    readonly property var clipData: {
        void clipDataRevision
        return EditorState.selectedClipData
    }
    readonly property bool hasSelection: !!clipData && Object.keys(clipData).length > 0
    readonly property string clipKind: hasSelection ? (clipData.kind || "") : ""
    readonly property int projectW: {
        void EditorState.tracks
        return Math.max(1, EditorState.projectWidth())
    }
    readonly property int projectH: {
        void EditorState.tracks
        return Math.max(1, EditorState.projectHeight())
    }
    readonly property int sourceDisplayW: {
        if (!root.hasSelection)
            return 0
        const rot = Math.abs(root.clipData.sourceRotation || 0) % 180
        return rot === 90 ? (root.clipData.sourceHeight || 0) : (root.clipData.sourceWidth || 0)
    }
    readonly property int sourceDisplayH: {
        if (!root.hasSelection)
            return 0
        const rot = Math.abs(root.clipData.sourceRotation || 0) % 180
        return rot === 90 ? (root.clipData.sourceWidth || 0) : (root.clipData.sourceHeight || 0)
    }
    readonly property bool sourceFrameChanged: {
        const frame = root.currentSourceFrame()
        return Math.abs(frame.x) > 0.0005 || Math.abs(frame.y) > 0.0005
                || Math.abs(frame.width - 1) > 0.0005
                || Math.abs(frame.height - 1) > 0.0005
    }
    readonly property bool showSourceBox: root.clipKind === "video" && root.sourceDisplayW > 0
                                      && root.sourceDisplayH > 0
                                      && (root.sourceDisplayW > root.projectW
                                          || root.sourceDisplayH > root.projectH
                                          || root.sourceFrameChanged)

    height: contentCol.height
    implicitHeight: contentCol.height

    // Human label for a clip kind. The raw id was shown to the user.
    function clipKindLabel(kind) {
        switch (kind) {
        case "video": return qsTr("Video")
        case "audio": return qsTr("Audio")
        case "image": return qsTr("Image")
        case "text": return qsTr("Text")
        case "subtitle": return qsTr("Subtitle")
        case "shape": return qsTr("Shape")
        case "sticker": return qsTr("Sticker")
        case "adjustment": return qsTr("Adjustment")
        }
        return kind.length > 0 ? kind : "—"
    }

    function applyTrim(inPoint, outPoint) {
        if (!root.hasSelection || isNaN(inPoint) || isNaN(outPoint))
            return
        EditorState.setClipTrim(EditorState.selectedTrack, EditorState.selectedClip, inPoint, outPoint)
    }

    function currentSourceFrame() {
        if (!root.hasSelection || !root.clipData.sourceFrame)
            return { "x": 0, "y": 0, "width": 1, "height": 1 }
        const frame = root.clipData.sourceFrame
        return {
            "x": Number(frame.x || 0),
            "y": Number(frame.y || 0),
            "width": Number(frame.width || 1),
            "height": Number(frame.height || 1)
        }
    }

    function framePixels() {
        const frame = root.currentSourceFrame()
        const sw = Math.max(1, root.sourceDisplayW)
        const sh = Math.max(1, root.sourceDisplayH)
        return {
            "x": Math.round(frame.x * sw),
            "y": Math.round(frame.y * sh),
            "width": Math.round(frame.width * sw),
            "height": Math.round(frame.height * sh)
        }
    }

    function fieldValue(field, fallback) {
        return field ? Number(field.value || 0) : fallback
    }

    function applySourceBoxPixels(x, y, w, h) {
        if (!root.hasSelection || root.clipKind !== "video")
            return
        const sw = Math.max(1, root.sourceDisplayW)
        const sh = Math.max(1, root.sourceDisplayH)
        const boxW = Math.max(1, Math.min(sw, Math.round(w)))
        const boxH = Math.max(1, Math.min(sh, Math.round(h)))
        const boxX = Math.max(0, Math.min(sw - boxW, Math.round(x)))
        const boxY = Math.max(0, Math.min(sh - boxH, Math.round(y)))
        EditorState.setClipSourceFrame(root.clipData.id,
                                       boxX / sw, boxY / sh,
                                       boxW / sw, boxH / sh)
        // The selected-clip model updates asynchronously. Keep every inspector field in sync
        // now, particularly the paired dimension changed by the ratio lock.
        if (sourceBoxXField)
            sourceBoxXField.value = boxX
        if (sourceBoxYField)
            sourceBoxYField.value = boxY
        if (sourceBoxWField)
            sourceBoxWField.value = boxW
        if (sourceBoxHField)
            sourceBoxHField.value = boxH
    }

    function applySourceBoxWidth(width) {
        const box = root.framePixels()
        if (!root.sourceBoxRatioLocked) {
            root.applySourceBoxPixels(box.x, box.y, width, box.height)
            return
        }
        const aspect = root.sourceDisplayW / Math.max(1, root.sourceDisplayH)
        let w = Math.max(1, Math.round(width))
        let h = Math.max(1, Math.round(w / aspect))
        if (box.y + h > root.sourceDisplayH) {
            h = Math.max(1, root.sourceDisplayH - box.y)
            w = Math.max(1, Math.round(h * aspect))
        }
        if (box.x + w > root.sourceDisplayW) {
            w = Math.max(1, root.sourceDisplayW - box.x)
            h = Math.max(1, Math.round(w / aspect))
        }
        root.applySourceBoxPixels(box.x, box.y, w, h)
    }

    function applySourceBoxHeight(height) {
        const box = root.framePixels()
        if (!root.sourceBoxRatioLocked) {
            root.applySourceBoxPixels(box.x, box.y, box.width, height)
            return
        }
        const aspect = root.sourceDisplayW / Math.max(1, root.sourceDisplayH)
        let h = Math.max(1, Math.round(height))
        let w = Math.max(1, Math.round(h * aspect))
        if (box.x + w > root.sourceDisplayW) {
            w = Math.max(1, root.sourceDisplayW - box.x)
            h = Math.max(1, Math.round(w / aspect))
        }
        if (box.y + h > root.sourceDisplayH) {
            h = Math.max(1, root.sourceDisplayH - box.y)
            w = Math.max(1, Math.round(h * aspect))
        }
        root.applySourceBoxPixels(box.x, box.y, w, h)
    }

    function refreshSourceBoxFields() {
        if (!root.hasSelection || !root.showSourceBox)
            return
        const box = root.framePixels()
        if (sourceBoxXField && !sourceBoxXField.activeFocus)
            sourceBoxXField.value = box.x
        if (sourceBoxYField && !sourceBoxYField.activeFocus)
            sourceBoxYField.value = box.y
        if (sourceBoxWField && !sourceBoxWField.activeFocus)
            sourceBoxWField.value = box.width
        if (sourceBoxHField && !sourceBoxHField.activeFocus)
            sourceBoxHField.value = box.height
    }

    function refreshFields() {
        if (!root.hasSelection)
            return
        if (startField && !startField.activeFocus)
            startField.value = root.clipData.start
        if (durationField && !durationField.activeFocus)
            durationField.value = root.clipData.duration
        if (inPointField && !inPointField.activeFocus)
            inPointField.value = root.clipData.inPoint
        if (outPointField && !outPointField.activeFocus)
            outPointField.value = root.clipData.outPoint
        refreshSourceBoxFields()
    }

    Connections {
        target: EditorState
        function onSelectionChanged() { root.clipDataRevision++; root.refreshFields() }
        function onSelectedClipDataChanged() { root.clipDataRevision++; root.refreshFields() }
        function onTracksChanged() { root.clipDataRevision++; root.refreshFields() }
    }

    Component.onCompleted: refreshFields()

    Column {
        id: contentCol
        width: root.width
        spacing: Theme.spacingXl

        // Read-only name plus a rename dialog: an editable field at the top of the panel kept
        // being mistaken for the text clip's content box.
        Column {
            width: root.width
            spacing: 4
            Text {
                text: qsTr("Clip name")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
            Row {
                width: parent.width
                spacing: Theme.spacingSm
                Text {
                    width: parent.width - renameButton.width - parent.spacing
                    anchors.verticalCenter: parent.verticalCenter
                    text: (root.hasSelection && root.clipData.name) || qsTr("Untitled clip")
                    color: root.hasSelection && root.clipData.name ? Theme.panelForeground : Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeBase
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                }
                IconButton {
                    id: renameButton
                    glyph: Theme.icons.pencil
                    variant: "ghost"
                    buttonSize: Theme.controlHeightSm
                    tooltip: qsTr("Rename clip")
                    onClicked: renameDialog.openWith(qsTr("Rename clip"), root.clipData.name || "")
                }
            }
        }

        Column {
            width: root.width
            spacing: 4
            Text {
                text: qsTr("Type")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
            Text {
                // Human label rather than the raw internal id.
                text: root.clipKindLabel(root.clipKind)
                color: Theme.panelForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                elide: Text.ElideRight
                width: parent.width - x
            }
        }

        Column {
            width: parent.width
            spacing: Theme.spacingSm
            visible: root.clipKind === "video" || root.clipKind === "image"
            ThemedLabel {
                text: qsTr("Original dimensions: %1 × %2")
                    .arg(root.clipData.sourceWidth || 0).arg(root.clipData.sourceHeight || 0)
            }
            Column {
                width: parent.width
                spacing: 8
                visible: root.showSourceBox

                Row {
                    width: parent.width
                    spacing: Theme.spacingSm

                    Text {
                        text: qsTr("Source frame box")
                        color: Theme.mutedForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                        font.weight: Font.Medium
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - ratioLockButton.width - parent.spacing
                        elide: Text.ElideRight
                    }

                    IconButton {
                        id: ratioLockButton
                        glyph: root.sourceBoxRatioLocked ? Theme.icons.lock : Theme.icons.lockOpen
                        tooltip: root.sourceBoxRatioLocked
                                 ? qsTr("Unlock source frame ratio")
                                 : qsTr("Lock source frame ratio")
                        active: root.sourceBoxRatioLocked
                        buttonSize: 28
                        iconSize: Theme.iconSizeSm
                        variant: "ghost"
                        onClicked: {
                            root.sourceBoxRatioLocked = !root.sourceBoxRatioLocked
                            // Re-locking uses the width as the authoritative value, matching the
                            // preview crop control and immediately updating the paired height.
                            if (root.sourceBoxRatioLocked)
                                root.applySourceBoxWidth(root.framePixels().width)
                        }
                    }
                }

                Row {
                    width: parent.width
                    spacing: 8

                    Column {
                        width: (parent.width - parent.spacing) / 2
                        spacing: 4
                        Text {
                            text: "X"
                            color: Theme.mutedForeground
                            font.pixelSize: Theme.fontSizeXs
                            font.family: Theme.fontFamily
                        }
                        ThemedNumberField {
                            id: sourceBoxXField
                            width: parent.width
                            unit: "px"
                            from: 0
                            to: Math.max(0, root.sourceDisplayW
                                         - Math.max(1, root.fieldValue(sourceBoxWField, root.framePixels().width)))
                            step: 1
                            onEdited: v => {
                                const box = root.framePixels()
                                root.applySourceBoxPixels(v, box.y, box.width, box.height)
                            }
                        }
                    }

                    Column {
                        width: (parent.width - parent.spacing) / 2
                        spacing: 4
                        Text {
                            text: "Y"
                            color: Theme.mutedForeground
                            font.pixelSize: Theme.fontSizeXs
                            font.family: Theme.fontFamily
                        }
                        ThemedNumberField {
                            id: sourceBoxYField
                            width: parent.width
                            unit: "px"
                            from: 0
                            to: Math.max(0, root.sourceDisplayH
                                         - Math.max(1, root.fieldValue(sourceBoxHField, root.framePixels().height)))
                            step: 1
                            onEdited: v => {
                                const box = root.framePixels()
                                root.applySourceBoxPixels(box.x, v, box.width, box.height)
                            }
                        }
                    }
                }

                Row {
                    width: parent.width
                    spacing: 8

                    Column {
                        width: (parent.width - parent.spacing) / 2
                        spacing: 4
                        Text {
                            text: qsTr("Width")
                            color: Theme.mutedForeground
                            font.pixelSize: Theme.fontSizeXs
                            font.family: Theme.fontFamily
                        }
                        ThemedNumberField {
                            id: sourceBoxWField
                            width: parent.width
                            unit: "px"
                            from: 1
                            to: Math.max(1, root.sourceDisplayW
                                         - Math.max(0, root.fieldValue(sourceBoxXField, root.framePixels().x)))
                            step: 1
                            onEdited: v => root.applySourceBoxWidth(v)
                        }
                    }

                    Column {
                        width: (parent.width - parent.spacing) / 2
                        spacing: 4
                        Text {
                            text: qsTr("Height")
                            color: Theme.mutedForeground
                            font.pixelSize: Theme.fontSizeXs
                            font.family: Theme.fontFamily
                        }
                        ThemedNumberField {
                            id: sourceBoxHField
                            width: parent.width
                            unit: "px"
                            from: 1
                            to: Math.max(1, root.sourceDisplayH
                                         - Math.max(0, root.fieldValue(sourceBoxYField, root.framePixels().y)))
                            step: 1
                            onEdited: v => root.applySourceBoxHeight(v)
                        }
                    }
                }
            }
            ThemedButton {
                text: qsTr("Edit source frame…")
                visible: root.clipKind === "video" && !!root.Window.window.openSourceFrame
                onClicked: root.Window.window.openSourceFrame(EditorState.selectedTrack, EditorState.selectedClip)
            }
        }

        Row {
            width: parent.width
            spacing: 8

            Column {
                width: (parent.width - parent.spacing) / 2
                spacing: 4
                Text {
                    text: qsTr("Starts at")
                    color: Theme.mutedForeground
                    font.pixelSize: Theme.fontSizeXs
                    font.family: Theme.fontFamily
                }
                ThemedNumberField {
                    id: startField
                    to: 86400
                    unit: "s"
                    width: parent.width
                    decimals: 2
                    step: 0.1
                    from: 0
                    onEdited: v => EditorState.setClipStart(
                                      EditorState.selectedTrack, EditorState.selectedClip, v)
                }
            }

            Column {
                width: (parent.width - parent.spacing) / 2
                spacing: 4
                Text {
                    text: qsTr("Duration")
                    color: Theme.mutedForeground
                    font.pixelSize: Theme.fontSizeXs
                    font.family: Theme.fontFamily
                }
                ThemedNumberField {
                    id: durationField
                    to: 86400
                    unit: "s"
                    width: parent.width
                    decimals: 2
                    step: 0.1
                    from: 0.1
                    onEdited: v => EditorState.setClipDuration(
                                        EditorState.selectedTrack, EditorState.selectedClip, v)
                }
            }
        }

        Column {
            width: root.width
            spacing: 8
            visible: root.clipKind !== "text" && root.clipKind !== "subtitle"
                     && root.clipKind !== "adjustment" && root.clipKind !== "shape"

            Text {
                text: qsTr("Trim")
                HoverHandler { id: tipHover1217 }
                ThemedToolTip { text: qsTr("Which part of the original file this clip plays"); visible: tipHover1217.hovered }
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            Row {
                width: parent.width
                spacing: 8

                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: 4
                    Text {
                        text: qsTr("From")
                        HoverHandler { id: tipHover1231 }
                        ThemedToolTip { text: qsTr("Seconds into the file where this clip starts"); visible: tipHover1231.hovered }
                        color: Theme.mutedForeground
                        font.pixelSize: Theme.fontSizeXs
                        font.family: Theme.fontFamily
                    }
                    ThemedNumberField {
                        id: inPointField
                        to: 86400
                        unit: "s"
                        width: parent.width
                        decimals: 2
                        step: 0.1
                        from: 0
                        onEdited: v => root.applyTrim(v, root.clipData.outPoint)
                    }
                }

                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: 4
                    Text {
                        text: qsTr("To")
                        HoverHandler { id: tipHover1252 }
                        ThemedToolTip { text: qsTr("Seconds into the file where this clip ends"); visible: tipHover1252.hovered }
                        color: Theme.mutedForeground
                        font.pixelSize: Theme.fontSizeXs
                        font.family: Theme.fontFamily
                    }
                    ThemedNumberField {
                        id: outPointField
                        to: 86400
                        unit: "s"
                        width: parent.width
                        decimals: 2
                        step: 0.1
                        from: 0
                        onEdited: v => root.applyTrim(root.clipData.inPoint, v)
                    }
                }
            }
        }

        Column {
            width: root.width
            spacing: 4
            visible: root.clipData.path !== undefined && root.clipData.path.length > 0

            Text {
                text: qsTr("File")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            Text {
                id: clipPathLabel
                text: root.clipData.path || "—"
                color: Theme.panelForeground
                font.family: Theme.monoFontFamily
                font.pixelSize: Theme.fontSizeSm
                width: parent.width
                wrapMode: Text.WrapAnywhere
                // Capped: a deep path used to wrap unbounded and
                // dominate the whole General tab.
                maximumLineCount: 3
                elide: Text.ElideRight

                HoverHandler { id: pathHover }

                ThemedToolTip {
                    text: root.clipData.path || ""
                    visible: pathHover.hovered && (root.clipData.path || "").length > 0
                }
            }
        }
    }

    NameDialog {
        id: renameDialog
        acceptText: qsTr("Rename")
        placeholder: qsTr("Clip name")
        onSubmitted: name => EditorState.setClipName(EditorState.selectedTrack, EditorState.selectedClip, name)
    }
}
