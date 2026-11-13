import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Shapes
import QtQuick.Window
import Drift
// One mask shape in the masks browser.
Column {
    id: root
    required property var modelData
    required property real cardSize
    property bool hasVisualSelection: false
    signal applyRequested(string maskId)

    width: cardSize
    spacing: Theme.spacingSm

    // Lift on grab: the card dims and grows slightly, so it reads as picked
    // up rather than merely faded.
    opacity: maskDrag.active ? Theme.c.maskPresetCard.dragOpacity : 1
    scale: maskDrag.active ? 1.04 : (maskDrag.pressed ? 0.97 : 1.0)

    Behavior on opacity {
        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }
    Behavior on scale {
        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }

    Rectangle {
        width: cardSize
        height: cardSize
        radius: Theme.c.maskPresetCard.radius
        color: maskDrag.hovered ? Theme.c.maskPresetCard.backgroundHover : Theme.c.maskPresetCard.background
        border.width: maskDrag.active ? Theme.c.maskPresetCard.borderWidth : 0
        border.color: Theme.c.maskPresetCard.border
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
                        strokeColor: Theme.c.maskPresetCard.shapeStroke
                        strokeWidth: Theme.c.maskPresetCard.shapeStrokeWidth
                        fillColor: Theme.c.maskPresetCard.shapeFill
                        PathSvg { path: EditorState.maskShapeSvgPath(root.modelData.id) }
                    }
                }
            }
        }

        ThemedToolTip {
            text: hasVisualSelection
                  ? qsTr("%1 — click to apply, or drag onto a clip").arg(root.modelData.label)
                  : qsTr("%1 — drag onto a clip").arg(root.modelData.label)
            visible: maskDrag.hovered && !maskDrag.active
        }

        AssetDragSource {
            id: maskDrag
            hasContextMenu: true
            onContextMenuRequested: cardMenu.popup()
            anchors.fill: parent
            kind: "mask"
            payload: root.modelData.id
            label: root.modelData.label
            glyph: Theme.icons.mask
            onTapped: {
                if (hasVisualSelection)
                    applyRequested(root.modelData.id)
                else
                    Toasts.info(qsTr("Select a clip first, or drag the mask onto one"))
            }
        }

        AssetQuickAddButton {
            z: 5
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 4
            revealed: maskDrag.hovered
            tooltip: qsTr("Apply to selected clip")
            enabled: hasVisualSelection
            onClicked: maskDrag.tapped()
        }

        ThemedContextMenu {
            id: cardMenu

            ThemedMenuItem {
                text: qsTr("Apply to selected clip")
                icon.name: Theme.icons.plus
                enabled: hasVisualSelection
                onTriggered: maskDrag.tapped()
            }
        }

        AssetFavoriteButton {
            id: favoriteButton
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: 3
            visible: favoriteButton.favorited || maskDrag.hovered || Theme.touchUi
            tabId: "masks"
            itemId: root.modelData.id
        }
    }

    Text {
        width: parent.width
        text: root.modelData.label
        color: Theme.c.maskPresetCard.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeCard
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
    }
}
