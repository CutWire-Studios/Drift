import QtQuick
import QtQuick.Controls.Basic
import Drift

PanelFrame {
    id: root
    SearchAnchor {
        searchId: "sourcelab-export"
        title: qsTr("Export trimmed clip")
        keywords: "export clip file save render container codec quality stream copy replace bin add copy"
    }

    property bool shown: false
    property string summaryMode: "replace"
    property string exportedPath: ""

    readonly property var lab: EditorState.sourceLab
    readonly property bool hasAsset: root.lab.currentAssetId.length > 0
    readonly property bool processable: root.lab.kind === "video" || root.lab.kind === "audio"
    readonly property bool idle: root.hasAsset && !root.lab.busy
    readonly property bool fullCrop: {
        const c = root.lab.recipe.crop
        return !c || (c.x === 0 && c.y === 0 && c.width === 1 && c.height === 1)
    }
    readonly property var plan: {
        void root.lab.recipe
        void root.lab.currentAssetId
        return root.hasAsset ? root.lab.plan(root.summaryMode, exportOptions.options) : ({})
    }
    readonly property int timelineUses: {
        void EditorState.tracksRevision
        const index = AssetLibrary.indexOfId(root.lab.currentAssetId)
        return index >= 0 ? EditorState.clipCountForAsset(index) : 0
    }

    function run(mode) {
        root.exportedPath = ""
        if (mode === "export" && exportOptions.path.length === 0 && !exportOptions.choosePath())
            return
        root.lab.apply(mode, mode === "export" ? exportOptions.options : ({}))
    }

    function folderOf(path) {
        return path.substring(0, path.lastIndexOf("/"))
    }

    Connections {
        target: root.lab
        function onExported(path) {
            root.exportedPath = path
        }
        function onCurrentAssetIdChanged() {
            root.exportedPath = ""
        }
    }
    AssetsPanelHeader {
        id: header
        width: parent.width
        height: Theme.panelHeaderHeight
        title: qsTr("Output")
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
        contentHeight: content.height
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar { }

        Column {
            id: content
            width: scroller.width - Theme.spacingMd
            spacing: Theme.spacingLg

            Flow {
                width: parent.width
                spacing: Theme.spacingSm

                ThemedLabel {
                    height: Theme.controlHeightSm
                    verticalAlignment: Text.AlignVCenter
                    text: qsTr("Summary for")
                }
                Repeater {
                    model: [
                        { id: "replace", label: qsTr("Replace") },
                        { id: "copy", label: qsTr("Copy") },
                        { id: "export", label: qsTr("Export") }
                    ]
                    ThemedChip {
                        required property var modelData
                        text: modelData.label
                        selected: root.summaryMode === modelData.id
                        onClicked: root.summaryMode = modelData.id
                    }
                }
            }

            SourceLabPlanCard {
                width: parent.width
                plan: root.plan
                fps: root.lab.fps > 0 ? root.lab.fps : 30
                useTimecode: root.lab.kind !== "audio"
            }

            Column {
                width: parent.width
                spacing: Theme.spacingSm

                ThemedButton {
                    width: parent.width
                    variant: "primary"
                    glyph: Theme.icons.refresh
                    text: qsTr("Replace in bin")
                    tooltip: qsTr("The source becomes the result; clips on the timeline follow it")
                    enabled: root.idle && root.lab.recipeDirty
                    onClicked: root.run("replace")
                }
                ThemedLabel {
                    width: parent.width
                    visible: root.timelineUses > 0
                    wrapMode: Text.WordWrap
                    text: qsTr("%n clip(s) on the timeline will be rebased onto the result.", "", root.timelineUses)
                }
                ThemedButton {
                    width: parent.width
                    variant: "secondary"
                    glyph: Theme.icons.copyPlus
                    text: qsTr("Add as copy")
                    tooltip: qsTr("Add the result to the media bin as a new item")
                    enabled: root.idle && root.lab.recipeDirty && root.lab.kind !== "image"
                    onClicked: root.run("copy")
                }
                ThemedButton {
                    width: parent.width
                    variant: "secondary"
                    glyph: Theme.icons.upload
                    text: qsTr("Export file...")
                    tooltip: qsTr("Write the result to a file, leaving the project untouched")
                    enabled: root.idle && root.processable
                    onClicked: root.run("export")
                }
            }

            SourceLabProgressCard {
                width: parent.width
                visible: root.lab.busy
            }

            ThemedLabel {
                width: parent.width
                visible: root.lab.lastError.length > 0
                wrapMode: Text.WordWrap
                color: Theme.c.sourceLabPanel.error
                text: root.lab.lastError
            }

            Row {
                width: parent.width
                visible: root.exportedPath.length > 0 && !root.lab.busy
                spacing: Theme.spacingMd

                ThemedLabel {
                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.c.sourceLabPanel.success
                    text: qsTr("Exported")
                }
                ThemedButton {
                    anchors.verticalCenter: parent.verticalCenter
                    variant: "ghost"
                    glyph: Theme.icons.folder
                    text: qsTr("Show in folder")
                    onClicked: Qt.openUrlExternally("file://" + root.folderOf(root.exportedPath))
                }
            }

            SourceLabSection {
                title: qsTr("Export options")
                expanded: root.processable
                visible: root.processable
                resettable: false

                SourceLabExportOptions {
                    id: exportOptions
                    width: parent.width
                    isVideo: root.lab.kind === "video"
                    canStreamCopy: root.lab.recipeIsPureTrimCrop && root.fullCrop
                }
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
        glyph: Theme.icons.upload
        title: qsTr("Select a clip in the media bin")
    }
}
