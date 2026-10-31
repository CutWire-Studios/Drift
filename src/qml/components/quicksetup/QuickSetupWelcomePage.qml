import QtQuick
import QtQuick.Controls.Basic
import Drift

QuickSetupPage {
    id: root

    title: qsTr("Welcome to Drift")
    subtitle: qsTr("Let's get you set up. First, pick your language.")
    glyph: Theme.icons.languages
    contentMaxWidth: 520

    readonly property var languages: {
        const all = EditorState.preferences.uiLanguages
        const out = []
        for (let i = 0; i < all.length; ++i) {
            if (all[i].id !== "")
                out.push(all[i])
        }
        return out
    }

    Rectangle {
        width: parent.width
        height: root.mobile ? 360 : 264
        radius: Theme.c.languageChooserDialog.radius
        color: Theme.c.languageChooserDialog.background
        border.width: Theme.c.languageChooserDialog.borderWidth
        border.color: Theme.c.languageChooserDialog.border
        clip: true

        ListView {
            id: languageList

            anchors.fill: parent
            anchors.margins: 1
            clip: true
            model: root.languages
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar { }

            Component.onCompleted: {
                for (let i = 0; i < root.languages.length; ++i) {
                    if (root.languages[i].id === EditorState.preferences.uiLanguage) {
                        positionViewAtIndex(i, ListView.Center)
                        break
                    }
                }
            }

            delegate: ItemDelegate {
                id: row

                required property var modelData

                width: languageList.width
                height: root.mobile ? 56 : 44
                highlighted: modelData.id === EditorState.preferences.uiLanguage
                hoverEnabled: true

                background: Rectangle {
                    color: row.highlighted ? Theme.c.languageChooserDialog.backgroundSelected
                           : row.hovered ? Theme.c.languageChooserDialog.backgroundHover
                           : "transparent"
                }

                contentItem: Item {
                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: Theme.spacingXl
                        anchors.right: checkIcon.left
                        anchors.rightMargin: Theme.spacingLg
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.modelData.label
                        elide: Text.ElideRight
                        color: row.highlighted ? Theme.c.languageChooserDialog.foregroundSelected
                                               : Theme.c.languageChooserDialog.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSm
                        font.weight: row.highlighted ? Font.Medium : Font.Normal
                    }

                    IconGlyph {
                        id: checkIcon

                        anchors.right: parent.right
                        anchors.rightMargin: Theme.spacingXl
                        anchors.verticalCenter: parent.verticalCenter
                        glyph: Theme.icons.check
                        iconSize: 16
                        iconColor: Theme.c.languageChooserDialog.icon
                        visible: row.highlighted
                    }
                }

                onClicked: EditorState.preferences.chooseUiLanguage(modelData.id)
            }
        }
    }
}
