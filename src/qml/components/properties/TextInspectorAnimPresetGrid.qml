import QtQuick
import Drift

Grid {
    property var inspector
    id: root
    width: parent.width
    readonly property real gap: Theme.spacingLg
    columns: Theme.touchUi
             ? Math.max(2, Math.min(3, Math.floor((width + gap) / (104 + gap))))
             : Math.max(2, Math.floor((width + gap) / (104 + gap)))
    columnSpacing: gap
    rowSpacing: gap
    readonly property real tileWidth: Math.floor((width - gap * (columns - 1)) / columns)
    Repeater {
        model: inspector.animTileModel
        delegate: TextAnimPresetTile {
            required property var modelData
            required property int index
            slot: inspector.animSlot
            presetId: modelData.id
            label: modelData.label
            tileWidth: root.tileWidth
            selected: modelData.id.length === 0
                      ? (!inspector.slotHasPreset && !inspector.slotData.custom)
                      : inspector.slotData.preset === modelData.id
            playing: inspector.showAnimate && inspector.visible && !EditorState.playing
                     && modelData.id.length > 0
                     && (Theme.touchUi ? selected : hovered)
            onClicked: inspector.pickPreset(modelData.id)
        }
    }
}
