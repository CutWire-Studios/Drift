import QtQuick
import QtQuick.Controls.Basic
import Drift

// The text clip's editing sections on one phone page: the text itself on top (tap to type it on
// the preview), then Font / Style / Effects / Animation / Layout tabs. One TextInspector serves
// every tab, so switching tabs changes what is shown and nothing else. `embedded` drops the inner
// scroll for a host that scrolls already.
Item {
    id: root

    property string tab: "font"
    property bool embedded: false

    readonly property var clipData: EditorState.selectedClipData
    readonly property bool isText: !!clipData && clipData.kind === "text"
    readonly property string content: isText ? String(clipData.textContent || "") : ""

    readonly property var tabs: [
        { id: "font", label: qsTr("Font") },
        { id: "style", label: qsTr("Style") },
        { id: "effects", label: qsTr("Effects") },
        { id: "animate", label: qsTr("Animation") },
        { id: "settings", label: qsTr("Layout") }
    ]

    implicitHeight: header.height + body.height

    Column {
        id: header
        width: parent.width
        spacing: Theme.spacingSm

        ThemedButton {
            visible: root.isText
            x: Theme.pagePadding
            width: parent.width - Theme.pagePadding * 2
            height: visible ? Theme.androidControlHeight : 0
            variant: "secondary"
            glyph: Theme.icons.type
            text: root.content.length > 0 ? root.content.replace(/\s+/g, " ") : qsTr("Tap to type text")
            onClicked: ShellBridge.main.editorPage.startInlineTextEdit(
                           EditorState.selectedTrack, EditorState.selectedClip, false)
        }

        AndroidTabStrip {
            width: parent.width
            tabs: root.tabs
            currentId: root.tab
            onTabSelected: tabId => root.tab = tabId
        }
    }

    Flickable {
        id: body
        anchors.top: header.bottom
        anchors.topMargin: Theme.spacingSm
        width: parent.width
        height: root.embedded ? contentHeight : Math.max(0, root.height - header.height - Theme.spacingSm)
        readonly property real inset: Theme.phone ? Theme.spacingMd : Theme.pagePadding
        contentWidth: width
        contentHeight: inspector.height + body.inset * 2
        interactive: !root.embedded
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar { }

        TextInspector {
            id: inspector
            x: Theme.pagePadding
            y: body.inset
            width: body.width - Theme.pagePadding * 2
            mode: root.tab
        }
    }
}
