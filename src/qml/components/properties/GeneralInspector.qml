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
    readonly property string clipKind: hasSelection ? (clipData.kind || "") : ""

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

    function refreshFields() {
        if (!root.hasSelection)
            return
        if (nameField && !nameField.activeFocus)
            nameField.text = root.clipData.name || ""
        if (startField && !startField.activeFocus)
            startField.value = root.clipData.start
        if (durationField && !durationField.activeFocus)
            durationField.value = root.clipData.duration
        if (inPointField && !inPointField.activeFocus)
            inPointField.value = root.clipData.inPoint
        if (outPointField && !outPointField.activeFocus)
            outPointField.value = root.clipData.outPoint
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

        ThemedTextField {
            id: nameField
            width: parent.width
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeBase
            font.weight: Font.Medium
            placeholderText: qsTr("Untitled clip")
            onEditingFinished: {
                const label = text.trim()
                if (label.length === 0 || !root.hasSelection)
                    return
                if (label === (root.clipData.name || ""))
                    return
                EditorState.setClipName(EditorState.selectedTrack, EditorState.selectedClip, label)
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
}
