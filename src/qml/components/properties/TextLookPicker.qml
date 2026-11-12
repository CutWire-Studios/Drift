import QtQuick
import QtQuick.Controls.Basic
import Drift
// Text-effect selector for the Style page: shows the active look (Shadow, Neon, …) rendered on
// the clip's own text, and opens a dialog with a roomy grid of every look to switch. Same
// trigger shape as TextStylePackPicker so the two pickers read as siblings.
Item {
    id: root

    property string lookId: ""
    property string sampleText: "Aa"
    property string fontFamily: ""
    property int fontWeight: 400
    property bool italic: false

    signal lookPicked(string lookId)

    readonly property var looks: EditorState.textLooks()
    property int favoritesTick: 0
    readonly property var orderedLooks: {
        void root.favoritesTick
        const fav = root.looks.filter(l => EditorState.isAssetFavorite("textLooks", l.id))
        return fav.concat(root.looks.filter(l => !EditorState.isAssetFavorite("textLooks", l.id)))
    }

    Connections {
        target: EditorState
        function onAssetFavoritesChanged() { root.favoritesTick++ }
    }

    readonly property string displayLabel: {
        for (let i = 0; i < root.looks.length; ++i) {
            if (root.looks[i].id === root.lookId)
                return root.looks[i].label
        }
        return qsTr("Custom")
    }

    function previewSource(id) {
        return "image://textlook/" + id
               + "?text=" + encodeURIComponent(root.sampleText)
               + "&font=" + encodeURIComponent(root.fontFamily)
               + "&weight=" + root.fontWeight
               + "&italic=" + (root.italic ? 1 : 0)
    }

    implicitHeight: 56
    implicitWidth: 200

    Rectangle {
        id: trigger
        anchors.fill: parent
        radius: Theme.c.textLookPicker.radius
        color: Theme.c.textLookPicker.background
        border.width: (dialog.visible || root.activeFocus) ? Theme.c.textLookPicker.borderWidth : 0
        border.color: root.activeFocus ? Theme.c.textLookPicker.borderFocus : Theme.c.textLookPicker.borderSecondary

        Rectangle {
            id: triggerThumb
            anchors.left: parent.left
            anchors.leftMargin: 6
            anchors.top: parent.top
            anchors.topMargin: 6
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 6
            width: Math.round(height * 2.0)
            radius: Theme.c.textLookPicker.radius
            color: Theme.c.textLookPicker.thumbBackground
            border.width: Theme.c.textLookPicker.borderWidth
            border.color: Theme.c.textLookPicker.thumbBorder
            clip: true

            Image {
                anchors.fill: parent
                anchors.margins: 2
                visible: root.lookId.length > 0
                asynchronous: true
                cache: false
                fillMode: Image.PreserveAspectFit
                source: root.lookId.length > 0 ? root.previewSource(root.lookId) : ""
                sourceSize.width: 176
                sourceSize.height: 88
            }

            Text {
                anchors.centerIn: parent
                visible: root.lookId.length === 0
                text: qsTr("Aa")
                color: Theme.c.textLookPicker.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
            }
        }

        Text {
            anchors.left: triggerThumb.right
            anchors.leftMargin: 8
            anchors.right: chevron.left
            anchors.rightMargin: 4
            anchors.verticalCenter: parent.verticalCenter
            text: root.displayLabel
            elide: Text.ElideRight
            color: Theme.c.textLookPicker.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
        }

        IconGlyph {
            id: chevron
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            glyph: Theme.icons.chevronRight
            iconSize: 12
            iconColor: Theme.c.textLookPicker.icon
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                root.forceActiveFocus()
                dialog.open()
            }
        }
    }

    ThemedDialog {
        id: dialog
        title: qsTr("Text effect")
        preferredWidth: Theme.dialogWidthLg
        showAccept: false
        rejectText: qsTr("Close")

        contentItem: Flickable {
            id: flick
            clip: true
            implicitWidth: parent ? parent.width : 320
            implicitHeight: Math.min(dialog.availableContentHeight, lookGrid.height)
            contentHeight: lookGrid.height
            ScrollBar.vertical: AppScrollBar { }

            Grid {
                id: lookGrid
                width: flick.width
                readonly property real gap: Theme.spacingLg
                columns: Math.max(2, Math.floor((width + gap) / (150 + gap)))
                columnSpacing: gap
                rowSpacing: gap
                readonly property real tileWidth: Math.floor((width - gap * (columns - 1)) / columns)

                Repeater {
                    // Popup content exists while closed; building the tiles only while open keeps
                    // their preview renders from running on every edit behind a closed dialog.
                    model: dialog.visible ? root.orderedLooks : []
                    delegate: TextLookTile {
                        tileWidth: lookGrid.tileWidth
                        host: root
                        onChosen: function(lookId) {
                            root.lookPicked(lookId)
                            dialog.close()
                        }
                    }
                }
            }
        }
    }
}
