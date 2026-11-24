import QtQuick
import Drift
// Panel title and the media-tab import actions. Catalog tabs hide this bar:
// their search field names the tab and carries its actions.
Rectangle {
    id: root

    property string title: ""
    property bool showTitle: true
    property bool showActions: false
    property bool importing: false

    signal newFolderRequested()
    signal newTimelineRequested()
    signal importFilesRequested()
    signal recordScreenRequested()
    signal recordCameraRequested()
    signal importFolderRequested()
    signal collectMediaRequested()

    // Matches the surrounding PanelFrame; it used to paint the app
    // background, so the header read as a different surface than the
    // panel it belongs to.
    color: Theme.c.assetsPanelHeader.background
    // Narrow panels must not paint the actions over neighbouring chrome (the tab rail).
    clip: true

    // Width of the actions with labels, remembered while they show, so dropping the labels
    // does not feed back into the decision to drop them.
    property real _fullActionsWidth: 0
    readonly property bool _compactActions: _fullActionsWidth
        > width - actions.anchors.rightMargin - Theme.pagePadding
          - (showTitle ? titleText.implicitWidth + Theme.spacingLg : 0)

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: Theme.c.assetsPanelHeader.borderWidth
        color: Theme.c.assetsPanelHeader.border
    }

    Text {
        id: titleText
        anchors.left: parent.left
        anchors.leftMargin: Theme.pagePadding
        anchors.right: root.showActions ? actions.left : parent.right
        anchors.rightMargin: root.showActions ? Theme.spacingLg : Theme.pagePadding
        anchors.verticalCenter: parent.verticalCenter
        elide: Text.ElideRight
        // Sheet chrome already shows the tab title.
        visible: root.showTitle
        text: root.title
        color: Theme.c.assetsPanelHeader.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeSm
    }

    Row {
        id: actions
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        spacing: 6
        visible: root.showActions
        onWidthChanged: if (!root._compactActions) root._fullActionsWidth = width

        ThemedButton {
            text: root._compactActions ? "" : qsTr("New Folder")
            variant: "ghost"
            glyph: Theme.icons.folder
            leftPadding: root._compactActions ? Theme.spacingLg : Theme.spacingXl
            rightPadding: root._compactActions ? Theme.spacingLg : Theme.spacing2xl - Theme.spacingXs
            tooltip: qsTr("Create a new folder here")
            anchors.verticalCenter: parent.verticalCenter
            onClicked: root.newFolderRequested()
        }

        ThemedButton {
            text: root._compactActions ? "" : qsTr("New timeline")
            variant: "ghost"
            glyph: Theme.icons.layers
            leftPadding: root._compactActions ? Theme.spacingLg : Theme.spacingXl
            rightPadding: root._compactActions ? Theme.spacingLg : Theme.spacing2xl - Theme.spacingXs
            tooltip: qsTr("Create an empty timeline and open it")
            anchors.verticalCenter: parent.verticalCenter
            onClicked: root.newTimelineRequested()
        }

        AssetsPanelImportSplit {
            anchors.verticalCenter: parent.verticalCenter
            importing: root.importing
            compact: root._compactActions
            onImportFilesRequested: root.importFilesRequested()
            onRecordScreenRequested: root.recordScreenRequested()
            onRecordCameraRequested: root.recordCameraRequested()
            onImportFolderRequested: root.importFolderRequested()
            onCollectMediaRequested: root.collectMediaRequested()
        }
    }
}
