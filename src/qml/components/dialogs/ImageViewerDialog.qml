import QtQuick
import Drift

ThemedDialog {
    id: root

    property var entry: ({})

    title: qsTr("Agent image")
    preferredWidth: 1280
    showAccept: false
    rejectText: qsTr("Close")

    function show(entryId) {
        root.entry = EditorState.mcp.imageLog.entry(entryId)
        root.open()
    }

    contentItem: ImageViewer {
        implicitHeight: Math.min(820, root.availableContentHeight)
        source: root.entry.url ?? ""
        imageWidth: root.entry.imageWidth ?? 0
        imageHeight: root.entry.imageHeight ?? 0
        path: root.entry.path ?? ""
        heading: root.entry.tool ?? ""
        details: root.entry.args ?? ""
        seekSeconds: root.entry.rangeStart ?? -1
        saveName: root.entry.tool ?? "image"
    }
}
