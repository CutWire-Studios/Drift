import QtQuick
import Drift

// Context-menu entry that opens a workspace through ShellBridge.goDeeper(); `target` is its context.
ThemedMenuItem {
    property var target: ({})

    onTriggered: ShellBridge.goDeeper(target)
}
