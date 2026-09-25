import QtQuick
import QtQuick.Controls.Basic
import Drift
import ".."

// Prop grid for the Face Props effect: a tap applies the prop with the placement it was fitted
// with. Import takes a .zip (or, on desktop, a folder) holding one or many props; props the user
// imported can be deleted again from a tile's context menu or long-press.
Column {
    id: root

    property int effectIndex: -1
    property var effectParams: []

    property var props: EditorState.facePropLibrary()
    property string menuPropId: ""
    property string menuPropName: ""

    readonly property var modelParam: {
        for (let i = 0; i < effectParams.length; i++) {
            if (effectParams[i].key === "model")
                return effectParams[i]
        }
        return ({})
    }
    readonly property string modelPath: modelParam.value || ""
    readonly property bool modelIsProp: {
        for (let i = 0; i < props.length; i++) {
            if (props[i].modelPath === modelPath)
                return true
        }
        return false
    }

    spacing: 6

    function refresh() {
        root.props = EditorState.facePropLibrary()
    }

    // Everything that opens a dialog or edits the project runs through Qt.callLater, touching
    // only singletons and captured values: the native dialog spins a nested event loop and an
    // edit can rebuild the inspector, either of which may destroy this picker while a handler of
    // it is still on the stack (Qt aborts on that).
    function pickZip() {
        const title = qsTr("Import Face Props")
        const filters = [qsTr("Zip archives (*.zip)")]
        Qt.callLater(() => {
            const url = FileDialogs.openFile(title, filters)
            if (url && url.toString() !== "")
                EditorState.importFaceProps(url)
        })
    }

    function pickFolder() {
        const title = qsTr("Import Face Props")
        Qt.callLater(() => {
            const url = FileDialogs.openDirectory(title)
            if (url && url.toString() !== "")
                EditorState.importFaceProps(url)
        })
    }

    Connections {
        target: EditorState
        function onFacePropsChanged() {
            root.refresh()
        }
    }

    Connections {
        target: Addons
        function onKindChanged(kind) {
            if (kind === "face-props")
                root.refresh()
        }
    }

    Row {
        width: parent.width
        spacing: 8

        Text {
            width: parent.width - importButton.width - 8
            elide: Text.ElideRight
            text: qsTr("Prop")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            anchors.verticalCenter: parent.verticalCenter
        }

        ThemedButton {
            id: importButton
            text: qsTr("Import")
            glyph: Theme.icons.upload
            variant: "secondary"
            tooltip: qsTr("Import face props from a .zip")
            onClicked: {
                if (FileDialogs.supportsDirectoryPicker())
                    importMenu.popup(0, height)
                else
                    root.pickZip()
            }

            ThemedContextMenu {
                id: importMenu

                ThemedMenuItem {
                    text: qsTr("Import zip…")
                    icon.name: Theme.icons.upload
                    onTriggered: root.pickZip()
                }
                ThemedMenuItem {
                    text: qsTr("Import folder…")
                    icon.name: Theme.icons.folderInput
                    onTriggered: root.pickFolder()
                }
            }
        }
    }

    Text {
        visible: root.props.length === 0
        width: parent.width
        wrapMode: Text.WordWrap
        text: qsTr("No face props installed. Import a .zip from Drift-Assets, or any folder of props that carry a prop.json.")
        color: Theme.mutedForeground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    Grid {
        id: propGrid
        visible: root.props.length > 0
        width: parent.width
        readonly property real gap: Theme.spacingSm
        columns: Math.max(1, Math.floor((width + gap) / (64 + gap)))
        columnSpacing: gap
        rowSpacing: gap
        readonly property real tileWidth: Math.floor((width - gap * (columns - 1)) / columns)

        Repeater {
            model: root.props
            delegate: Column {
                id: propTile
                required property var modelData
                readonly property bool selected: modelData.modelPath === root.modelPath
                width: propGrid.tileWidth
                spacing: 2

                Rectangle {
                    width: parent.width
                    height: width
                    radius: Theme.radiusSm
                    color: tileMouse.containsMouse ? Theme.popoverHover : Theme.panelAccent
                    border.width: propTile.selected ? Theme.borderWidthFocus : 0
                    border.color: Theme.primary
                    clip: true

                    Image {
                        id: propImage
                        anchors.fill: parent
                        anchors.margins: 4
                        visible: !!propTile.modelData.thumbnailPath
                        source: propTile.modelData.thumbnailPath
                                ? EditorState.imageUrl(propTile.modelData.thumbnailPath) : ""
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        // A re-import replaces the file at the same path.
                        cache: false
                        sourceSize.width: 128
                        sourceSize.height: 128
                    }

                    IconGlyph {
                        anchors.centerIn: parent
                        visible: !propTile.modelData.thumbnailPath || propImage.status === Image.Error
                        glyph: Theme.icons.package
                        iconSize: Theme.iconSizeLg
                        iconColor: Theme.mutedForeground
                    }

                    ThemedToolTip {
                        text: propTile.modelData.description
                              ? propTile.modelData.name + "\n" + propTile.modelData.description
                              : propTile.modelData.name
                        visible: tileMouse.containsMouse
                    }

                    MouseArea {
                        id: tileMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        pressAndHoldInterval: 450
                        property bool heldMenu: false

                        function openMenu() {
                            if (!propTile.modelData.removable)
                                return
                            root.menuPropId = propTile.modelData.id
                            root.menuPropName = propTile.modelData.name
                            propMenu.popup()
                        }

                        onPressed: heldMenu = false
                        onPressAndHold: {
                            heldMenu = true
                            openMenu()
                        }
                        onClicked: (mouse) => {
                            if (mouse.button === Qt.RightButton) {
                                openMenu()
                                return
                            }
                            if (heldMenu)
                                return
                            Haptics.select()
                            const track = EditorState.selectedTrack
                            const clip = EditorState.selectedClip
                            const effect = root.effectIndex
                            const propId = propTile.modelData.id
                            Qt.callLater(() => EditorState.applyFaceProp(track, clip, effect, propId))
                        }
                    }
                }

                Text {
                    width: parent.width
                    text: propTile.modelData.name
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter
                    color: propTile.selected ? Theme.primary : Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
            }
        }
    }

    // A model picked outside the library (an older project, or a prop since deleted).
    Text {
        visible: root.modelPath !== "" && !root.modelIsProp
        width: parent.width
        elide: Text.ElideMiddle
        text: {
            const parts = root.modelPath.split(/[/\\]/)
            const name = parts[parts.length - 1] || root.modelPath
            return root.modelParam.missing ? qsTr("Custom model: %1 (missing)").arg(name)
                                           : qsTr("Custom model: %1").arg(name)
        }
        color: root.modelParam.missing ? Theme.destructive : Theme.mutedForeground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    ThemedContextMenu {
        id: propMenu

        ThemedMenuItem {
            text: qsTr("Delete prop…")
            icon.name: Theme.icons.trash
            onTriggered: confirmRemoval.open()
        }
    }

    ThemedDialog {
        id: confirmRemoval
        title: qsTr("Delete this prop?")
        acceptText: qsTr("Delete")
        acceptVariant: "destructive"
        preferredWidth: Theme.dialogWidthSm
        // Enter must not commit a destructive action.
        acceptOnReturn: false

        contentItem: ThemedLabel {
            width: parent ? parent.width : Theme.dialogWidthSm
            wrapMode: Text.WordWrap
            size: "sm"
            text: qsTr("“%1” will be removed from your face props. Effects using it will show it as missing until it is imported again.")
                  .arg(root.menuPropName)
        }

        onAccepted: {
            const propId = root.menuPropId
            Qt.callLater(() => EditorState.removeFaceProp(propId))
        }
    }
}
