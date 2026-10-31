import QtQuick
import QtQuick.Controls.Basic
import Drift

PanelFrame {
    id: root
    SearchAnchor {
        searchId: "sourcelab-denoise-stabilize"
        title: qsTr("Denoise and stabilize source")
        keywords: "denoise noise hiss clean stabilize shake steady"
    }

    property bool shown: false

    readonly property var lab: EditorState.sourceLab
    readonly property bool hasAsset: root.lab.currentAssetId.length > 0
    readonly property string kind: root.lab.kind
    readonly property bool isVideo: root.kind === "video"
    readonly property bool processable: root.isVideo || root.kind === "audio"

    AssetsPanelHeader {
        id: header
        width: parent.width
        height: Theme.panelHeaderHeight
        title: qsTr("Process")

        ThemedButton {
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingMd
            anchors.verticalCenter: parent.verticalCenter
            visible: root.hasAsset
            variant: "ghost"
            text: qsTr("Reset all")
            enabled: root.lab.recipeDirty
            onClicked: root.lab.resetRecipe("all")
        }
    }

    Flickable {
        id: scroller
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spacingMd
        visible: root.hasAsset
        clip: true
        contentWidth: width
        contentHeight: sections.height
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar { }

        Column {
            id: sections
            width: scroller.width - Theme.spacingMd

            SourceLabCropSection {
                visible: root.isVideo || root.kind === "image"
            }
            SourceLabSpeedSection {
                visible: root.processable
            }
            SourceLabAudioSection {
                visible: root.processable && root.lab.hasAudio
            }
            SourceLabStabilizeSection {
                visible: root.isVideo
            }
            SourceLabEnhanceSection {
                visible: root.isVideo
            }
        }
    }

    EmptyState {
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spacingLg
        visible: !root.hasAsset
        glyph: Theme.icons.sliders
        title: qsTr("Select a clip in the media bin")
    }
}
