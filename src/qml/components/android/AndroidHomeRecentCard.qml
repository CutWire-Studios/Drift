import QtQuick
import Drift

// One recent project, as a card on Home's strip or (row) a line in the Projects list.
// Long-press and the corner button open the same menu; the button is the visible
// equivalent of the gesture.
Rectangle {
    id: card

    required property var modelData
    property bool row: false

    signal openRequested(string path)

    readonly property real margin: row ? Theme.spacingMd : Theme.spacingSm

    height: row ? Theme.androidHomeRecentCardHeight
                : thumb.height + textColumn.implicitHeight + margin * 2 + Theme.spacingSm
    radius: Theme.c.androidHomeRecentCard.radius
    color: Theme.c.androidHomeRecentCard.background
    border.width: Theme.c.androidHomeRecentCard.borderWidth
    border.color: Theme.c.androidHomeRecentCard.border
    opacity: modelData.exists === false ? 0.55 : 1

    Accessible.role: Accessible.Button
    Accessible.name: card.projectLabel

    readonly property string projectLabel: {
        const n = card.modelData.name || ""
        return n.replace(/\.drift$/i, "") || qsTr("Untitled")
    }

    readonly property string detailText: {
        const parts = []
        const m = card.modelData.modified
        if (m && !isNaN(m.getTime()))
            parts.push(Qt.formatDateTime(m, Locale.ShortFormat))
        if (card.modelData.sizeBytes > 0)
            parts.push(DownloadFormat.formatBytes(card.modelData.sizeBytes))
        return parts.join(" · ")
    }

    Rectangle {
        id: thumb
        x: card.margin
        y: card.margin
        height: card.row ? card.height - card.margin * 2 : width * 9 / 16
        width: card.row ? height * 16 / 9 : card.width - card.margin * 2
        radius: Theme.c.androidHomeRecentCard.thumbRadius
        color: Theme.c.androidHomeRecentCard.thumbBackground
        clip: true

        IconGlyph {
            anchors.centerIn: parent
            visible: image.status !== Image.Ready
            glyph: Theme.icons.film
            iconSize: 18
            iconColor: Theme.c.androidHomeRecentCard.icon
        }

        // The thumbnail file is overwritten in place on every save, so the same URL must be
        // re-read rather than served from the image cache.
        Image {
            id: image
            anchors.fill: parent
            source: card.modelData.thumbnail || ""
            cache: false
            asynchronous: true
            fillMode: Image.PreserveAspectCrop
        }

        // Same on-disk signal the desktop recents list carries;
        // the card's dimming alone did not say what was wrong.
        AndroidAttentionDot {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 4
            dotColor: card.modelData.exists === false
                      ? Theme.c.androidHomeRecentCard.missingDot
                      : Theme.c.androidHomeRecentCard.presentDot
        }
    }

    Column {
        id: textColumn
        spacing: 2
        x: card.row ? thumb.x + thumb.width + Theme.spacingMd : card.margin
        y: card.row ? (card.height - implicitHeight) / 2 : thumb.y + thumb.height + Theme.spacingSm
        width: card.width - x - (card.row ? Theme.iconButtonSize + Theme.spacingXs : card.margin)

        Text {
            width: parent.width
            text: card.projectLabel
            color: Theme.c.androidHomeRecentCard.foreground
            font.family: Theme.fontFamily
            font.pixelSize: card.row ? Theme.fontSizeSm : Theme.fontSizeXs
            font.weight: Font.Medium
            elide: Text.ElideMiddle
        }

        Text {
            width: parent.width
            visible: text.length > 0
            text: card.detailText
            color: Theme.c.androidHomeRecentCard.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            elide: Text.ElideRight
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        pressAndHoldInterval: 450
        property bool heldMenu: false
        onPressed: heldMenu = false
        // Only a finger long-presses; a real mouse has the right button and the corner button.
        onPressAndHold: (mouse) => {
            if (mouse.source === Qt.MouseEventNotSynthesized)
                return
            heldMenu = true
            Haptics.pickUp()
            cardMenu.popup()
        }
        onClicked: (mouse) => {
            if (heldMenu)
                return
            if (mouse.button === Qt.RightButton) {
                cardMenu.popup()
                return
            }
            if (card.modelData.exists === false) {
                Toasts.warning(qsTr("That project file is missing."))
                return
            }
            Haptics.select()
            card.openRequested(card.modelData.path)
        }
    }

    IconButton {
        anchors.right: parent.right
        anchors.top: card.row ? undefined : parent.top
        anchors.verticalCenter: card.row ? parent.verticalCenter : undefined
        anchors.margins: Theme.spacingXs
        buttonSize: Theme.iconButtonSize
        iconSize: Theme.iconSizeMd
        glyph: Theme.icons.ellipsis
        variant: "text"
        tooltip: qsTr("Project actions")
        onClicked: cardMenu.popup()
    }

    ThemedContextMenu {
        id: cardMenu

        ThemedMenuItem {
            text: qsTr("Remove from recents")
            icon.name: Theme.icons.trash
            onTriggered: EditorState.projectFile.removeRecentProject(card.modelData.path)
        }
    }
}
