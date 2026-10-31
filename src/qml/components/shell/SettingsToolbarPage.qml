import QtQuick
import Drift

SettingsPage {
    id: page

    pageId: "toolbar"
    title: qsTr("Timeline toolbar")

    SettingsRow {
        stacked: true
        title: qsTr("Toolbar layout")
        searchId: "toolbar-layout"
        description: qsTr("Choose which actions appear on the toolbar above the timeline and which go in its overflow menu.")
        keywords: "buttons actions overflow menu customize"
        query: page.query

        TimelineToolbarCustomizer {
            autoSave: true
            width: parent.width
        }
    }
}
