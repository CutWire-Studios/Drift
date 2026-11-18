pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Drift

// Preview controls on top, then the Node, Sliders, Details and Code tabs of the Forge document.
PanelFrame {
    id: root
    SearchAnchor {
        searchId: "forge-inspector"
        title: qsTr("Forge inspector")
        keywords: "forge inspector node sliders parameters details code glsl preview loop target"
    }

    property bool shown: true
    property string currentTab: "node"

    readonly property var tabs: [
        { "id": "node", "label": qsTr("Node") },
        { "id": "sliders", "label": qsTr("Sliders") },
        { "id": "details", "label": qsTr("Details") },
        { "id": "code", "label": qsTr("Code") }
    ]

    function applyRequest() {
        const r = ShellBridge.panelRequest
        if (r && r.panel === "forgeInspector" && r.tab)
            currentTab = r.tab
    }

    Component.onCompleted: applyRequest()

    Connections {
        target: ShellBridge
        function onPanelRequestChanged() { root.applyRequest() }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ForgePreviewBar {
            Layout.fillWidth: true
        }

        Row {
            Layout.fillWidth: true
            Layout.margins: Theme.spacingMd
            spacing: Theme.spacingSm

            Repeater {
                model: root.tabs

                ThemedChip {
                    required property var modelData
                    text: modelData.label
                    selected: root.currentTab === modelData.id
                    variant: "outline"
                    onClicked: root.currentTab = modelData.id
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: {
                for (let i = 0; i < root.tabs.length; ++i)
                    if (root.tabs[i].id === root.currentTab)
                        return i
                return 0
            }

            ForgeNodeTab { }
            ForgeSlidersTab { }
            ForgeDetailsTab { }
            ForgeCodeTab { }
        }
    }
}
