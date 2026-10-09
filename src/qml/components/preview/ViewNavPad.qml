import QtQuick
import Drift
import ".."

// Discrete steps for the free 3D view, under the axis widget. The four orbits and the two zooms
// are Blender's numpad 4/6/8/2 and +/−. Holding a button keeps stepping.
Rectangle {
    id: pad

    // Called with "left", "right", "up", "down", "in" or "out".
    property var navigate

    function withShortcut(label, key) {
        return qsTr("%1 (%2)").arg(label).arg(key)
    }

    width: grid.implicitWidth + Theme.spacingXs * 2
    height: grid.implicitHeight + Theme.spacingXs * 2
    radius: Theme.radiusMd
    color: Theme.panelBackground
    border.width: Theme.borderWidth
    border.color: Theme.panelBorder

    Grid {
        id: grid
        anchors.centerIn: parent
        columns: 3
        rowSpacing: 0
        columnSpacing: 0

        Repeater {
            model: [
                { action: "left", glyph: Theme.icons.chevronLeft,
                  label: qsTr("Orbit left"), key: qsTr("Numpad 4") },
                { action: "up", glyph: Theme.icons.chevronUp,
                  label: qsTr("Orbit up"), key: qsTr("Numpad 8") },
                { action: "right", glyph: Theme.icons.chevronRight,
                  label: qsTr("Orbit right"), key: qsTr("Numpad 6") },
                { action: "out", glyph: Theme.icons.zoomOut,
                  label: qsTr("Zoom out"), key: qsTr("Numpad -") },
                { action: "down", glyph: Theme.icons.chevronDown,
                  label: qsTr("Orbit down"), key: qsTr("Numpad 2") },
                { action: "in", glyph: Theme.icons.zoomIn,
                  label: qsTr("Zoom in"), key: qsTr("Numpad +") }
            ]
            delegate: IconButton {
                required property var modelData
                glyph: modelData.glyph
                variant: "text"
                tooltip: pad.withShortcut(modelData.label, modelData.key)
                Accessible.name: modelData.label
                // A hold is many clicks; a haptic on each one would buzz.
                haptic: "none"
                autoRepeat: true
                autoRepeatDelay: 400
                autoRepeatInterval: 80
                onClicked: {
                    if (pad.navigate)
                        pad.navigate(modelData.action)
                }
            }
        }
    }
}
