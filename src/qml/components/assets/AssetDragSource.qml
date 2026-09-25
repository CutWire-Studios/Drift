import QtQuick
import Drift
import ".."

// Makes whatever it covers draggable as an asset: a platform drag on the desktop (the mime type
// comes from AssetDrag), a press-and-hold lift into TouchDrag on the phone, and a plain tap on
// either — which the card uses for its add-at-playhead action.
//
// The Drag attached properties sit on this item and the handlers on a child: a handler on the item
// that owns Drag.Automatic re-enters QDrag::exec through its own active binding.
Item {
    id: root

    required property string kind
    required property var payload
    property string label: ""
    property string thumbnail: ""
    property string glyph: ""
    property alias liftEnabled: lift.liftEnabled

    readonly property bool active: mouseDrag.active || lift.lifted
    readonly property bool pressed: tap.pressed || lift.pressed
    readonly property bool hovered: hover.hovered

    signal tapped()

    Drag.active: mouseDrag.active
    Drag.dragType: Drag.Automatic
    Drag.supportedActions: Qt.CopyAction
    Drag.keys: [AssetDrag.mimeKey(root.kind)]
    Drag.mimeData: AssetDrag.mimeData(root.kind, root.payload, root.label)
    // Centred, so the drop lands under the pointer rather than a card's width to its left.
    Drag.hotSpot.x: width / 2
    Drag.hotSpot.y: height / 2

    Item {
        anchors.fill: parent

        HoverHandler {
            id: hover
            enabled: !Theme.touchUi
            cursorShape: mouseDrag.active ? Qt.ClosedHandCursor : Qt.PointingHandCursor
        }

        TapHandler {
            id: tap
            enabled: !Theme.touchUi
            onTapped: root.tapped()
        }

        DragHandler {
            id: mouseDrag
            target: null
            enabled: !Theme.touchUi
            acceptedButtons: Qt.LeftButton
            // Not the touchscreen: a finger on a desktop touchscreen scrolls the grid.
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
            onActiveChanged: {
                if (active)
                    AssetDrag.begin(root.kind, root.payload, root.label)
                else
                    AssetDrag.end()
            }
        }

        TouchLiftArea {
            id: lift
            dragKind: root.kind
            payload: root.payload
            label: root.label
            thumbnail: root.thumbnail
            glyph: root.glyph
            onLiftTapped: root.tapped()
        }
    }
}
