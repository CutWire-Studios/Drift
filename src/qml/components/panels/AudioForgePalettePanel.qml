pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Drift

// Every pedal, split and modulator, grouped by category. Clicking one drops it at the centre
// of the graph view.
PanelFrame {
    id: root
    SearchAnchor {
        searchId: "audio-forge-palette"
        title: qsTr("Audio Forge pedals")
        keywords: "audio forge pedals palette add filter reverb delay split modulator lfo envelope"
    }

    property bool shown: true
    property string query: ""
    property var collapsed: ({})

    readonly property var rows: {
        const entries = AudioForge.quickAddEntries(query, "", false)
        const searching = query.trim().length > 0
        const order = []
        const groups = {}
        for (const e of entries) {
            if (!groups[e.category]) {
                groups[e.category] = []
                order.push(e.category)
            }
            groups[e.category].push(e)
        }
        const out = []
        for (const c of order) {
            const open = searching || !collapsed[c]
            out.push({ "header": true, "category": c, "count": groups[c].length, "open": open,
                       "accent": groups[c][0].accent })
            if (open)
                for (const e of groups[c])
                    out.push({ "header": false, "entry": e })
        }
        return out
    }

    function toggle(category) {
        const next = Object.assign({}, collapsed)
        next[category] = !next[category]
        collapsed = next
    }

    function capitalized(s) {
        return s.charAt(0).toUpperCase() + s.slice(1)
    }

    ThemedTextField {
        id: search
        anchors.top: parent.top
        anchors.margins: Theme.spacingMd
        anchors.left: parent.left
        anchors.right: parent.right
        font.family: Theme.fontFamily
        placeholderText: qsTr("Search pedals")
        onTextChanged: root.query = text
    }

    ListView {
        anchors.top: search.bottom
        anchors.topMargin: Theme.spacingMd
        anchors.bottom: parent.bottom
        width: parent.width
        clip: true
        model: root.rows
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar { }

        delegate: Rectangle {
            id: row
            required property var modelData
            readonly property var accentTokens: Theme.c.nodeDelegate.accent
            width: ListView.view.width
            height: Theme.controlHeight
            color: modelData.header ? Theme.c.forgePanel.headerBackground : "transparent"

            MouseArea {
                anchors.fill: parent
                onClicked: row.modelData.header ? root.toggle(row.modelData.category)
                                                : AudioForge.requestAddNode(row.modelData.entry.type)
            }

            Row {
                anchors.fill: parent
                anchors.leftMargin: row.modelData.header ? Theme.spacingMd : Theme.spacing2xl
                spacing: Theme.spacingMd

                IconGlyph {
                    visible: row.modelData.header
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: row.modelData.open ? Theme.icons.chevronDown : Theme.icons.chevronRight
                    iconSize: Theme.iconSizeSm
                    iconColor: Theme.c.forgePanel.foregroundMuted
                }
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 8
                    height: 8
                    radius: 4
                    color: row.accentTokens[row.modelData.header ? row.modelData.accent : row.modelData.entry.accent]
                           ?? Theme.c.forgePanel.foregroundMuted
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - x - Theme.spacingMd
                    text: row.modelData.header ? root.capitalized(row.modelData.category) + "  " + row.modelData.count
                                               : row.modelData.entry.label
                    elide: Text.ElideRight
                    color: row.modelData.header ? Theme.c.forgePanel.foregroundMuted : Theme.c.forgePanel.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSm
                }
            }
        }
    }
}
