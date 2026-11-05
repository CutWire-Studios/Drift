pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Drift
// The cameras, models and 3D layers under the playhead, grouped. Clicking a row selects its clip
// (the timeline selection follows), double-clicking frames it in the 3D viewport. A clip has no
// visibility or lock of its own, so the eye hides the track it sits on.
PanelFrame {
    id: root
    SearchAnchor {
        searchId: "outliner3d-panel"
        title: qsTr("3D Outliner")
        keywords: "3d scene list cameras models layers objects hierarchy visibility"
    }

    property bool shown: false

    readonly property var groupLabels: [qsTr("Cameras"), qsTr("Models"), qsTr("Layers")]
    readonly property var groupGlyphs: [Theme.icons.video, Theme.icons.box, Theme.icons.layers]

    AssetsPanelHeader {
        id: header
        width: parent.width
        height: Theme.panelHeaderHeight
        title: qsTr("3D Outliner")
    }

    EmptyState {
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spacingLg
        visible: list.count === 0
        glyph: Theme.icons.box
        title: qsTr("No cameras, models or 3D layers at the playhead.")
    }

    ListView {
        id: list
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        visible: count > 0
        clip: true
        model: EditorState.preview.outliner
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar {}

        section.property: "group"
        section.delegate: Item {
            id: sec
            required property string section
            width: ListView.view.width
            height: 26

            Text {
                anchors.left: parent.left
                anchors.leftMargin: Theme.pagePadding
                anchors.verticalCenter: parent.verticalCenter
                text: root.groupLabels[parseInt(sec.section)]
                color: Theme.c.outliner3dPanel.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                font.weight: Font.Medium
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.c.outliner3dPanel.divider
            }
        }

        delegate: Rectangle {
            id: row
            required property int group
            required property string name
            required property int trackIndex
            required property int clipIndex
            required property bool selected
            required property bool hidden

            width: ListView.view.width
            height: 30
            color: row.selected ? Theme.c.outliner3dPanel.rowSelected
                                : (rowHover.hovered ? Theme.c.outliner3dPanel.rowHover : "transparent")

            HoverHandler { id: rowHover }
            TapHandler {
                onTapped: EditorState.selectClip(row.trackIndex, row.clipIndex)
                onDoubleTapped: {
                    EditorState.selectClip(row.trackIndex, row.clipIndex)
                    EditorState.preview.frameSelection()
                }
            }

            IconGlyph {
                id: kindIcon
                anchors.left: parent.left
                anchors.leftMargin: Theme.pagePadding
                anchors.verticalCenter: parent.verticalCenter
                glyph: root.groupGlyphs[row.group]
                iconSize: 14
                iconColor: Theme.c.outliner3dPanel.foregroundMuted
            }

            Text {
                anchors.left: kindIcon.right
                anchors.leftMargin: Theme.spacingMd
                anchors.right: eye.left
                anchors.rightMargin: Theme.spacingSm
                anchors.verticalCenter: parent.verticalCenter
                text: row.name.length > 0 ? row.name : root.groupLabels[row.group]
                elide: Text.ElideRight
                opacity: row.hidden ? 0.5 : 1
                color: Theme.c.outliner3dPanel.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
            }

            IconButton {
                id: eye
                anchors.right: parent.right
                anchors.rightMargin: Theme.spacingSm
                anchors.verticalCenter: parent.verticalCenter
                buttonSize: 22
                iconSize: 14
                glyph: row.hidden ? Theme.icons.eyeOff : Theme.icons.eye
                tooltip: row.hidden ? qsTr("Show this clip's track") : qsTr("Hide this clip's track")
                onClicked: EditorState.setTrackHidden(row.trackIndex, !row.hidden)
            }
        }
    }
}
