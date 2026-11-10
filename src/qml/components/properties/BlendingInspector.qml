import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
Item {
    id: root

    property int clipDataRevision: 0
    readonly property var clipData: {
        void clipDataRevision
        return EditorState.selectedClipData
    }
    readonly property bool hasSelection: !!clipData && Object.keys(clipData).length > 0

    height: contentCol.height
    implicitHeight: contentCol.height

    function refreshFields() {}

    FontMetrics {
        id: nameMetrics
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    function syncActive() {
        const on = root.visible && root.hasSelection
        if (EditorState.blendPreview.active !== on)
            EditorState.blendPreview.active = on
        if (on)
            EditorState.blendPreview.request()
    }

    onVisibleChanged: syncActive()
    onHasSelectionChanged: syncActive()
    Component.onCompleted: syncActive()
    Component.onDestruction: EditorState.blendPreview.active = false

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
            text: qsTr("Blend mode")
            SearchAnchor {
                searchId: "blending-blend-mode"
                title: qsTr("Blend mode")
                keywords: "multiply screen overlay add difference exclusion dodge burn soft hard light hue saturation color luminosity"
            }
            color: Theme.c.blendingInspector.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        // The tab was a label plus one combo with no description.
        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            text: qsTr("How this clip's colours combine with the tracks beneath it.")
            color: Theme.c.blendingInspector.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            opacity: 0.8
        }

        Repeater {
            model: BlendModes.groups

            delegate: Column {
                id: group
                required property var modelData
                readonly property int columnCount: Math.max(2, Math.round(width / 112))
                readonly property real cellWidth: Math.floor(width / columnCount)
                width: contentCol.width
                spacing: Theme.spacingXs

                Text {
                    visible: group.modelData.label !== ""
                    text: group.modelData.label
                    color: Theme.c.blendingInspector.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }

                Flow {
                    width: parent.width

                    Repeater {
                        model: group.modelData.modes

                        delegate: Item {
                            id: card
                            required property var modelData
                            readonly property bool current: (root.clipData.blendMode || "normal") === modelData.id
                            readonly property real thumbWidth: group.cellWidth - Theme.spacingMd
                            width: group.cellWidth
                            height: Math.round(thumbWidth * 9 / 16) + Theme.spacingXs
                                    + Math.ceil(nameMetrics.height) + Theme.spacingMd

                            Rectangle {
                                id: thumb
                                width: card.thumbWidth
                                height: Math.round(width * 9 / 16)
                                radius: Theme.c.blendCard.radius
                                color: cardHover.hovered ? Theme.c.blendCard.backgroundHover : Theme.c.blendCard.background
                                border.width: card.current ? Theme.c.blendCard.selectedBorderWidth : 0
                                border.color: Theme.c.blendCard.selectedBorder
                                clip: true

                                Behavior on color {
                                    ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                                }

                                Image {
                                    anchors.fill: parent
                                    anchors.margins: card.current ? Theme.c.blendCard.selectedBorderWidth : 0
                                    asynchronous: true
                                    cache: false
                                    fillMode: Image.PreserveAspectCrop
                                    sourceSize.width: Math.min(card.thumbWidth, 160) * Screen.devicePixelRatio
                                    source: EditorState.blendPreview.serial > 0
                                            ? "image://color/blend/" + card.modelData.id
                                              + "?s=" + EditorState.blendPreview.serial
                                            : ""
                                    opacity: status === Image.Ready ? 1 : 0

                                    Behavior on opacity {
                                        NumberAnimation { duration: 120 }
                                    }
                                }
                            }

                            Text {
                                y: thumb.height + Theme.spacingXs
                                width: card.thumbWidth
                                elide: Text.ElideRight
                                text: card.modelData.label
                                color: card.current ? Theme.c.blendCard.labelSelected : Theme.c.blendCard.label
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeXs
                            }

                            HoverHandler {
                                id: cardHover
                                cursorShape: Qt.PointingHandCursor
                            }
                            ThemedToolTip {
                                visible: cardHover.hovered
                                text: card.modelData.label
                            }
                            TapHandler {
                                onTapped: EditorState.setClipBlendMode(
                                              EditorState.selectedTrack, EditorState.selectedClip,
                                              card.modelData.id)
                            }
                        }
                    }
                }
            }
        }

        ThemedButton {
            visible: (root.clipData.blendMode || "normal") !== "normal"
            text: qsTr("Reset to Normal")
            variant: "ghost"
            glyph: Theme.icons.reset
            onClicked: EditorState.setClipBlendMode(
                           EditorState.selectedTrack, EditorState.selectedClip, "normal")
        }
    }
}
