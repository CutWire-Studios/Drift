import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift

// Header bug button. Decoder/encoder capability plus host facts to paste into a GitHub issue.
ThemedDialog {
    id: root

    title: qsTr("Debug info")
    preferredWidth: Theme.dialogWidthLg
    acceptText: qsTr("Copy report")
    rejectText: qsTr("Close")

    property var info: ({ codecs: [], encoders: [], system: [], hints: [] })
    property var playback: ({ rows: [], hints: [] })
    // The sweep's own sections, kept apart from the live rows above. Copying re-collects the
    // rows in C++ so both halves of the pasted report describe the same instant, and only this
    // travels across.
    property var benchmark: ({})
    property int activeTab: 0

    onOpened: {
        info = EditorState.debugInfo()
        playback = EditorState.playbackDiagnostics()
    }
    onAccepted: {
        // Both tabs, always. Which one the reporter was looking at says nothing about which
        // one holds the answer.
        EditorState.copyDiagnosticsReport(benchmark)
        Toasts.success(qsTr("Copied to clipboard"))
    }

    Connections {
        target: EditorState
        function onPlaybackBenchmarkFinished(result) {
            root.benchmark = result
            // The sweep decoded for a couple of seconds against the same counters these rows
            // come from, so what was collected on open is no longer what the report would say.
            root.playback = EditorState.playbackDiagnostics()
        }
    }

    // One measured stage of a benchmark sweep. Everything addresses `benchRow` by id rather
    // than through `parent`, which inside a nested Repeater delegate means several different
    // things depending on where it is read from.

    function ms(v) {
        return (v === undefined || v === null) ? "\u2014" : v.toFixed(2) + " ms"
    }

    function cellFill(state, striped) {
        if (state === "unavailable")
            return striped ? Theme.c.debugInfoDialog.rowStripe : "transparent"
        const c = state === "supported" ? Theme.c.debugInfoDialog.cellSupported
                                        : Theme.c.debugInfoDialog.cellUnsupported
        return Qt.alpha(c, striped ? Theme.c.debugInfoDialog.cellOpacityStriped : Theme.c.debugInfoDialog.cellOpacity)
    }

    function cellTextColor(state) {
        if (state === "unavailable")
            return Theme.c.debugInfoDialog.foregroundMuted
        return state === "supported" ? Theme.c.debugInfoDialog.cellSupported
                                     : Theme.c.debugInfoDialog.cellUnsupported
    }

    function cellLabel(state) {
        if (state === "unavailable")
            return qsTr("Unavailable")
        return state === "supported" ? qsTr("Supported") : qsTr("Not supported")
    }

    function cellState(row, hardwareColumn) {
        if (hardwareColumn && row && row.hardwareUnavailable)
            return "unavailable"
        const ok = hardwareColumn ? !!(row && row.hardware) : !!(row && row.software)
        return ok ? "supported" : "unsupported"
    }

    contentItem: Flickable {
        id: contentFlick
        width: parent ? parent.width : Theme.dialogWidthLg
        implicitHeight: Math.min(body.height, root.availableContentHeight)
        contentWidth: width
        contentHeight: body.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        interactive: contentHeight > height
        ScrollBar.vertical: AppScrollBar {
            policy: contentFlick.contentHeight > contentFlick.height
                    ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
        }

        Column {
            id: body
            width: contentFlick.width
            spacing: Theme.spacingXl

            // Two tabs rather than two dialogs: the header has room for one diagnostics
            // button, and a reporter who has to find the right dialog usually sends the
            // wrong one. Copy Report takes both regardless of which is on screen.
            Row {
                width: parent.width
                spacing: Theme.spacingSm

                Repeater {
                    model: [qsTr("System"), qsTr("Playback")]

                    ThemedButton {
                        required property var modelData
                        required property int index
                        text: modelData
                        variant: root.activeTab === index ? "primary" : "ghost"
                        onClicked: root.activeTab = index
                    }
                }
            }

            DebugInfoSystemTab {
                dialog: root
            }

            DebugInfoPlaybackTab {
                dialog: root
            }


            Rectangle {
                width: parent.width
                height: Theme.c.debugInfoDialog.borderWidth
                color: Theme.c.debugInfoDialog.divider
            }

            Column {
                width: parent.width
                spacing: Theme.spacingSm

                ThemedLabel {
                    width: parent.width
                    size: "sm"
                    text: qsTr("Need help? Copy the report above when you file an issue.")
                }

                ThemedLabel {
                    width: parent.width
                    size: "sm"
                    tone: "default"
                    textFormat: Text.RichText
                    linkColor: Theme.c.debugInfoDialog.link
                    text: "<a href=\"https://github.com/CutWire-Studios/Drift/issues\">%1</a>"
                          .arg(qsTr("Report a bug"))
                    onLinkActivated: (link) => Qt.openUrlExternally(link)
                    HoverHandler {
                        cursorShape: parent.hoveredLink ? Qt.PointingHandCursor : Qt.ArrowCursor
                    }
                }

                ThemedLabel {
                    width: parent.width
                    size: "sm"
                    tone: "default"
                    textFormat: Text.RichText
                    linkColor: Theme.c.debugInfoDialog.link
                    text: "<a href=\"https://docs.cutwire.org/drift\">%1</a>"
                          .arg(qsTr("Documentation"))
                    onLinkActivated: (link) => Qt.openUrlExternally(link)
                    HoverHandler {
                        cursorShape: parent.hoveredLink ? Qt.PointingHandCursor : Qt.ArrowCursor
                    }
                }

                ThemedLabel {
                    width: parent.width
                    size: "sm"
                    tone: "default"
                    textFormat: Text.RichText
                    linkColor: Theme.c.debugInfoDialog.link
                    text: "<a href=\"https://cutwire.org/discord\">%1</a>"
                          .arg(qsTr("Questions and support on Discord"))
                    onLinkActivated: (link) => Qt.openUrlExternally(link)
                    HoverHandler {
                        cursorShape: parent.hoveredLink ? Qt.PointingHandCursor : Qt.ArrowCursor
                    }
                }
            }
        }
    }
}
