import QtQuick
import Drift
IconButton {
    glyph: Theme.icons.grid
    variant: "text"
    tooltip: qsTr("Guides")
    tooltipActionId: "toggleGuides"
    tooltipIllustration: Component { GuidesIllustration { } }
    tooltipBody: qsTr("Overlay safe areas and grids so titles and subjects stay framed.")
    active: EditorState.preview.guidesEnabled
    onClicked: EditorState.preview.guidesEnabled = !EditorState.preview.guidesEnabled
}
