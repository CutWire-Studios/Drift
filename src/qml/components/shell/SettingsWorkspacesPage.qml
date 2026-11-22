import QtQuick
import Drift
import "workspaces/designer"

SettingsPage {
    id: page

    pageId: "workspaces"
    title: qsTr("Workspaces")

    Text {
        width: parent.width
        visible: page.query.length === 0
        wrapMode: Text.WordWrap
        text: qsTr("Arrange panels into layouts, optionally across several windows. Drag a panel onto an edge of another block to split it, or onto its middle to add a tab. Nothing changes in the editor until you save.")
        color: Theme.c.settingsRow.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    SettingsGroupHeader {
        text: qsTr("Orientation")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Workspace orientation")
        searchId: "workspaces-orientation"
        description: qsTr("Landscape or portrait workspace. Auto follows the shape of the canvas.")
        keywords: "landscape portrait rotate layout"
        query: page.query

        ThemedComboBox {
            width: Theme.c.settingsRow.controlWidth
            textRole: "label"
            valueRole: "id"
            model: [
                { id: "auto",      label: qsTr("Auto (follow canvas)") },
                { id: "landscape", label: qsTr("Landscape") },
                { id: "portrait",  label: qsTr("Portrait") }
            ]
            currentIndex: {
                if (!EditorState.preferences.workspaceLayoutOverridden)
                    return 0
                return EditorState.preferences.workspaceLayoutPreferred === "portrait" ? 2 : 1
            }
            onActivated: {
                const id = model[currentIndex].id
                if (id === "auto")
                    EditorState.preferences.clearWorkspaceLayoutPreference()
                else
                    EditorState.preferences.setWorkspaceLayoutPreference(id)
            }
        }
    }

    SettingsGroupHeader {
        text: qsTr("Layouts")
        query: page.query
    }

    SettingsRow {
        stacked: true
        title: qsTr("Layout designer")
        searchId: "workspaces-layout-designer"
        description: qsTr("Built-in presets keep a copy of the original, so Reset brings them back.")
        keywords: "workspace layout panel window monitor screen dock preset split tab"
        query: page.query

        WorkspaceDesigner {
            width: parent.width
            height: implicitHeight
        }
    }
}
