import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift

// Results for the header search field. The field keeps focus and forwards Up/Down/Enter here;
// the popup never takes focus itself. The index is rebuilt on every open, so addon installs and
// new presets show up without listening to each catalog's change signal.
Popup {
    id: root

    property int highlightIndex: 0
    property string query: ""
    property bool asking: false

    signal finished()
    signal queryPicked(string text)

    padding: Theme.spacingMd
    modal: false
    dim: false
    focus: false
    closePolicy: Popup.CloseOnPressOutsideParent

    readonly property var _kindLabels: ({
        "action": qsTr("Action"),
        "command": qsTr("Command"),
        "panel": qsTr("Panel"),
        "effect": qsTr("Effect"),
        "template": qsTr("Template"),
        "stack": qsTr("Effect stack"),
        "audioEffect": qsTr("Audio FX"),
        "transition": qsTr("Transition"),
        "mask": qsTr("Mask"),
        "shape": qsTr("Shape"),
        "sticker": qsTr("Sticker"),
        "textStyle": qsTr("Text style"),
        "addon": qsTr("Add-on"),
        "control": qsTr("Control"),
        "ask": qsTr("Ask"),
        "suggestion": qsTr("Suggestion")
    })

    // The model takes a few seconds to load; start it now so the command is quick if one follows.
    onOpened: if (EditorState.preferences.commandBarEnabled) EditorState.commandBar.prewarm()

    Connections {
        target: SearchIndex
        function onNavigateRequested(target) {
            ShellBridge.navigate(target)
        }
    }

    function search(query) {
        root.query = query
        asking = false
        if (!opened)
            SearchIndex.rebuild()
        SearchIndex.setQuery(query)
        highlightIndex = 0
        list.positionViewAtBeginning()
        if (!opened)
            open()
    }

    function ask(text) {
        asking = true
        if (!opened)
            SearchIndex.rebuild()
        SearchIndex.setAskQuery(text)
        highlightIndex = 0
        list.positionViewAtBeginning()
        if (!opened)
            open()
    }

    function moveHighlight(step) {
        if (SearchIndex.count === 0)
            return
        highlightIndex = (highlightIndex + step + SearchIndex.count) % SearchIndex.count
        list.positionViewAtIndex(highlightIndex, ListView.Contain)
    }

    function activate(index) {
        const row = list.itemAtIndex(index)
        if (!row || !row.runnable)
            return
        close()
        SearchIndex.activate(index)
        root.finished()
    }

    background: Rectangle {
        color: Theme.c.globalSearchPopup.background
        border.width: Theme.c.globalSearchPopup.borderWidth
        border.color: Theme.c.globalSearchPopup.border
        radius: Theme.c.globalSearchPopup.radius
    }

    contentItem: Item {
        implicitHeight: SearchIndex.count > 0 ? Math.min(list.contentHeight, 420) : empty.implicitHeight

        Column {
            id: empty
            visible: SearchIndex.count === 0
            width: parent.width
            padding: Theme.spacingLg
            spacing: Theme.spacingMd

            Text {
                width: parent.width - empty.padding * 2
                wrapMode: Text.Wrap
                text: root.asking || root.query.length === 0 ? qsTr("No matches")
                                                              : qsTr("No matches for “%1”").arg(root.query)
                color: Theme.c.globalSearchPopup.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
            }

            Text {
                visible: !root.asking
                text: qsTr("Popular searches")
                color: Theme.c.globalSearchPopup.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            Flow {
                visible: !root.asking
                width: parent.width - empty.padding * 2
                spacing: Theme.spacingSm

                Repeater {
                    model: [qsTr("Captions"), qsTr("Remove background"), qsTr("Speed"), qsTr("Crop"),
                            qsTr("Stabilize"), qsTr("Export")]

                    ThemedChip {
                        required property string modelData
                        text: modelData
                        variant: "outline"
                        focusPolicy: Qt.NoFocus
                        onClicked: root.queryPicked(modelData)
                    }
                }
            }
        }

        ListView {
            id: list
            anchors.fill: parent
            clip: true
            spacing: 2
            boundsBehavior: Flickable.StopAtBounds
            model: SearchIndex
            ScrollBar.vertical: AppScrollBar { }

            delegate: GlobalSearchResultRow {
                highlightIndex: root.highlightIndex
                kindLabels: root._kindLabels
                onActivated: (i) => root.activate(i)
            }
        }
    }
}
