import QtQuick
import Drift

// Holds the shared timeline override cursor while `active`. The token keeps neighbouring items
// from clearing each other's cursor when the pointer crosses between them. Touch never gets one.
QtObject {
    id: request

    property string kind: ""
    property bool active: false
    property int height: 0

    property int token: 0
    readonly property bool wanted: active && kind !== "" && !Theme.touchInput

    function apply() {
        if (wanted) {
            if (token === 0)
                token = EditorState.acquireTrimCursorToken()
            EditorState.setTimelineCursor(kind, height, token)
        } else if (token !== 0) {
            EditorState.setTimelineCursor("", 0, token)
            token = 0
        }
    }

    onWantedChanged: apply()
    onKindChanged: if (wanted) apply()
    onHeightChanged: if (wanted) apply()
    Component.onDestruction: if (token !== 0) EditorState.setTimelineCursor("", 0, token)
}
