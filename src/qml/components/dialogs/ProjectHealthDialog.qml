pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Drift

// What is wrong with the project (missing media, unused assets, broken links) with the fixes that
// have one: clearing unused assets and pointing moved media at its new place.
ThemedDialog {
    id: root

    property var problems: []
    property int problemCount: 0
    property var unusedAssets: []
    property var missingMedia: []

    readonly property var kindNames: ({
        "missing_media": qsTr("Missing media"),
        "missing_asset": qsTr("Missing asset"),
        "asset_mismatch": qsTr("Asset mismatch"),
        "range_past_media": qsTr("Past end of media"),
        "orphaned_effect": qsTr("Orphaned effect"),
        "broken_link": qsTr("Broken link"),
        "dangling_transition": qsTr("Dangling transition"),
        "empty_lane": qsTr("Empty lane"),
        "overlap": qsTr("Overlap"),
        "unused_asset": qsTr("Unused asset")
    })
    readonly property var rows: {
        const list = root.problems.slice()
        for (const asset of root.unusedAssets)
            list.push({ kind: "unused_asset", message: asset.name })
        return list
    }

    title: qsTr("Project health")
    preferredWidth: Theme.dialogWidthLg
    showAccept: false
    rejectText: qsTr("Close")

    function openDialog() {
        refresh()
        open()
    }

    function refresh() {
        const validation = EditorState.bin.validateProject(500)
        root.problems = validation.problems || []
        root.problemCount = validation.n || 0
        const unused = EditorState.bin.removeUnusedAssets(true)
        root.unusedAssets = unused.ok ? unused.assets : []
        const missing = EditorState.bin.missingMedia()
        root.missingMedia = missing.ok ? missing.missing : []
    }

    function finish(reply, doneText) {
        EditorState.setLastMessage(reply.ok ? doneText(reply) : (reply.detail || reply.error))
        root.refresh()
    }

    function relinkText(reply) {
        return reply.still_missing_n > 0
               ? qsTr("Relinked %n file(s), %1 still missing", "", reply.n).arg(reply.still_missing_n)
               : qsTr("Relinked %n file(s)", "", reply.n)
    }

    function searchFolder() {
        const url = FileDialogs.openDirectory(qsTr("Search folder for missing media"))
        if (!url || url.toString() === "")
            return
        root.finish(EditorState.bin.relinkSearchFolder(url, false), root.relinkText)
    }

    function openRelink() {
        const first = root.missingMedia.length > 0 ? root.missingMedia[0].path : ""
        fromField.text = first.substring(0, Math.max(0, first.replace(/\\/g, "/").lastIndexOf("/")))
        toField.text = ""
        relinkDialog.open()
    }

    ThemedDialog {
        id: removeDialog

        title: qsTr("Remove unused assets")
        acceptText: qsTr("Remove")
        acceptVariant: "destructive"
        preferredWidth: Theme.dialogWidthSm
        acceptOnReturn: false

        onAccepted: {
            const reply = EditorState.bin.removeUnusedAssets(false)
            root.finish(reply, r => qsTr("Removed %n asset(s)", "", r.removed))
        }

        contentItem: Column {
            spacing: Theme.spacingLg
            width: parent ? parent.width : 320

            ThemedLabel {
                width: parent.width
                text: qsTr("These %n asset(s) are not used by any clip. Removing them can be undone.", "",
                           root.unusedAssets.length)
            }

            ListView {
                width: parent.width
                height: Math.min(contentHeight, 200)
                clip: true
                model: root.unusedAssets
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: AppScrollBar {}
                delegate: ThemedLabel {
                    required property var modelData
                    width: ListView.view.width
                    tone: "default"
                    size: "sm"
                    wrapMode: Text.NoWrap
                    elide: Text.ElideRight
                    text: modelData.name
                }
            }
        }
    }

    ThemedDialog {
        id: relinkDialog

        title: qsTr("Relink folder")
        acceptText: qsTr("Relink")
        preferredWidth: Theme.dialogWidthMd
        acceptOnReturn: false

        onAccepted: root.finish(EditorState.bin.relinkFolder(fromField.text, toField.text, false), root.relinkText)

        contentItem: Column {
            spacing: Theme.spacingLg
            width: parent ? parent.width : 400

            ThemedLabel {
                width: parent.width
                text: qsTr("Missing files under the old folder are pointed at the same place under the new one.")
            }
            ThemedLabel { text: qsTr("Old folder") }
            ThemedTextField {
                id: fromField
                width: parent.width
                placeholderText: qsTr("D:/footage")
            }
            ThemedLabel { text: qsTr("New folder") }
            Row {
                width: parent.width
                spacing: Theme.spacingSm

                ThemedTextField {
                    id: toField
                    width: parent.width - browseButton.width - parent.spacing
                }
                ThemedButton {
                    id: browseButton
                    variant: "secondary"
                    text: qsTr("Browse…")
                    visible: FileDialogs.supportsDirectoryPicker()
                    onClicked: {
                        const url = FileDialogs.openDirectory(qsTr("New folder"))
                        if (url && url.toString() !== "")
                            toField.text = decodeURIComponent(url.toString())
                                                   .replace(/^file:\/\/\/(?=[A-Za-z]:)/, "").replace(/^file:\/\//, "")
                    }
                }
            }
        }
    }

    contentItem: Column {
        spacing: Theme.spacingLg
        width: parent ? parent.width : Theme.dialogWidthLg

        ThemedLabel {
            width: parent.width
            tone: root.rows.length === 0 ? "default" : "muted"
            size: "sm"
            text: root.rows.length === 0
                  ? qsTr("No problems found.")
                  : qsTr("%n finding(s)", "", root.problemCount + root.unusedAssets.length)
                    + (root.problemCount > root.problems.length ? " " + qsTr("(first %1 listed)").arg(root.problems.length) : "")
        }

        ListView {
            id: list
            width: parent.width
            height: Math.min(contentHeight, 260)
            visible: root.rows.length > 0
            clip: true
            model: root.rows
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar {}

            delegate: Row {
                id: row

                required property var modelData

                width: ListView.view.width
                height: 26
                spacing: Theme.spacingMd

                ThemedLabel {
                    width: 140
                    anchors.verticalCenter: parent.verticalCenter
                    size: "sm"
                    wrapMode: Text.NoWrap
                    elide: Text.ElideRight
                    color: row.modelData.kind === "missing_media" || row.modelData.kind === "missing_asset"
                           ? Theme.warning : Theme.c.themedLabel.foregroundMuted
                    text: root.kindNames[row.modelData.kind] || row.modelData.kind.replace(/_/g, " ")
                }
                ThemedLabel {
                    width: row.width - 140 - row.spacing
                    anchors.verticalCenter: parent.verticalCenter
                    tone: "default"
                    size: "sm"
                    wrapMode: Text.NoWrap
                    elide: Text.ElideMiddle
                    text: row.modelData.message
                }
            }
        }

        Row {
            spacing: Theme.spacingSm

            ThemedButton {
                variant: "secondary"
                text: qsTr("Remove unused assets…")
                enabled: root.unusedAssets.length > 0
                onClicked: removeDialog.open()
            }
            ThemedButton {
                variant: "secondary"
                text: qsTr("Relink folder…")
                enabled: root.missingMedia.length > 0
                onClicked: root.openRelink()
            }
            ThemedButton {
                variant: "secondary"
                text: qsTr("Search folder for missing media…")
                enabled: root.missingMedia.length > 0 && FileDialogs.supportsDirectoryPicker()
                onClicked: root.searchFolder()
            }
        }
    }
}
