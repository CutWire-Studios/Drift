pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Drift

// Audition controls on top, then the Node, Sliders and Details tabs of the audio effect.
PanelFrame {
    id: root
    SearchAnchor {
        searchId: "audio-forge-inspector"
        title: qsTr("Audio Forge inspector")
        keywords: "audio forge inspector pedal knob sliders details impulse response modulation steps loop audition"
    }

    property bool shown: true
    property string currentTab: "node"

    readonly property var tabs: [
        { "id": "node", "label": qsTr("Node") },
        { "id": "sliders", "label": qsTr("Sliders") },
        { "id": "details", "label": qsTr("Details") }
    ]

    function applyRequest() {
        const r = ShellBridge.panelRequest
        if (r && r.panel === "audioForgeInspector" && r.tab)
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

        AudioForgeAuditionBar {
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

            AudioForgeNodeTab { }
            ForgeSlidersTab { forge: AudioForge }
            ForgeDetailsTab { forge: AudioForge }
        }
    }
}
