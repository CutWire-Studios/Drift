import QtQuick
import QtQuick.Controls.Basic
import Drift

// Renders the open template folder once per row of a data file, in a headless Drift process, so
// editing carries on meanwhile.
ThemedDialog {
    id: root

    readonly property var folder: EditorState.projectFile.folder
    property var info: ({})
    property url dataFile
    readonly property int rowCount: info.rowCount || 0

    title: qsTr("Render with data")
    acceptText: root.folder.rendering ? qsTr("Rendering…") : qsTr("Render")
    preferredWidth: Theme.dialogWidthMd

    function openDialog() {
        dataFile = ""
        refresh()
        outputField.text = info.output || ""
        allRows.checked = true
        rowField.text = "1"
        open()
    }

    function refresh() {
        info = root.folder.templateInfo(dataFile)
    }

    onAccepted: {
        if (!root.folder.rendering)
            root.folder.renderTemplate(root.dataFile, outputField.text, parseInt(rowField.text) || 0,
                                       root.rowCount === 0 || allRows.checked)
    }

    contentItem: Column {
        spacing: Theme.spacingLg
        width: parent ? parent.width : 420

        ThemedLabel {
            width: parent.width
            size: "sm"
            wrapMode: Text.WordWrap
            text: root.rowCount > 0
                  ? qsTr("%n row(s) of data: one video per row.", "", root.rowCount)
                  : (root.info.dataError ? root.info.dataError
                                         : qsTr("No data file: the template renders once with its default parameters."))
        }

        Row {
            spacing: Theme.spacingLg
            ThemedButton {
                text: qsTr("Choose data file…")
                variant: "secondary"
                onClicked: {
                    const url = FileDialogs.openFile(qsTr("Data file"), [qsTr("CSV or JSON (*.csv *.json)")],
                                                     ["text/csv", "application/json"])
                    if (url != "") {
                        root.dataFile = url
                        root.refresh()
                    }
                }
            }
            ThemedLabel {
                anchors.verticalCenter: parent.verticalCenter
                size: "xs"
                text: root.dataFile.toString() !== ""
                      ? decodeURIComponent(root.dataFile.toString()).split("/").pop()
                      : (root.info.dataFile || "")
            }
        }

        ThemedCheckBox {
            id: allRows
            width: parent.width
            visible: root.rowCount > 0
            text: qsTr("Every row")
        }

        Row {
            spacing: Theme.spacingLg
            visible: root.rowCount > 0 && !allRows.checked
            ThemedLabel {
                anchors.verticalCenter: parent.verticalCenter
                size: "sm"
                text: qsTr("Row")
            }
            ThemedTextField {
                id: rowField
                width: 80
                inputMethodHints: Qt.ImhDigitsOnly
            }
        }

        ThemedLabel {
            size: "sm"
            text: qsTr("Output file")
        }

        ThemedTextField {
            id: outputField
            width: parent.width
            placeholderText: qsTr("/path/to/renders/{row}.mp4")
        }

        ThemedLabel {
            width: parent.width
            size: "xs"
            wrapMode: Text.WordWrap
            text: qsTr("{row} is the row number; {name} is any template parameter.")
        }

        ThemedLabel {
            width: parent.width
            visible: root.folder.renderLog.length > 0
            size: "xs"
            wrapMode: Text.WrapAnywhere
            maximumLineCount: 8
            elide: Text.ElideLeft
            text: root.folder.renderLog
        }
    }
}
