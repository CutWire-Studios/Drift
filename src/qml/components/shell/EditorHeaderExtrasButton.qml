import QtQuick
import Drift
import QtQuick.Window

// Extras. Pulses with a red shockwave while essential packs or updates need
// attention — the dialog itself never opens on its own (see UpdateDialog).
Item {
    id: extrasButton
    implicitWidth: extrasBtn.implicitWidth
    implicitHeight: extrasBtn.implicitHeight
    width: implicitWidth
    height: implicitHeight
    anchors.verticalCenter: parent.verticalCenter

    readonly property bool attention: {
        const win = ShellBridge.main
        return win && win.addonAttentionNeeded
    }

    // Expanding rings behind the icon — reads as a soft shockwave, not a badge.
    Repeater {
        model: 2
        delegate: EditorHeaderExtrasWave {
            attention: extrasButton.attention
        }
    }

    IconButton {
        id: extrasBtn
        anchors.fill: parent
        glyph: Theme.icons.puzzle
        variant: "ghost"
        text: qsTr("Extras")
        tooltip: extrasButton.attention
                 ? qsTr("Recommended packs and updates")
                 : qsTr("Extras")
        tooltipBody: qsTr("Optional AI engines, models and add-ons that unlock more features")
        onClicked: ShellBridge.main.openExtras()
    }
}
