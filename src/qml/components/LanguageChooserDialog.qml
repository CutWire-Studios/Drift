import QtQuick
import QtQuick.Controls.Basic
import Drift

// First launch after install, and the header Language button afterwards.
// English is selected by default on first launch. The header reopens the same
// list (including System default) so Settings does not have to host a dropdown.
ThemedDialog {
    id: root

    title: fromHeader ? qsTr("Language") : qsTr("Choose your language")
    acceptText: qsTr("Continue")
    rejectText: qsTr("Close")
    showAccept: !fromHeader
    showReject: fromHeader
    preferredWidth: Theme.dialogWidthSm
    closePolicy: fromHeader ? Popup.CloseOnEscape | Popup.CloseOnPressOutside
                            : Popup.NoAutoClose

    property string selectedId: "en"
    // Header reopen vs the blocking first-launch prompt. Separate instances
    // (Main vs EditorHeader) so closing the header picker cannot resume startup.
    property bool fromHeader: false

    function selectId(id) {
        selectedId = id
        const langs = pickerLanguages
        for (let i = 0; i < langs.length; ++i) {
            if (langs[i].id === id) {
                languageList.currentIndex = i
                languageList.positionViewAtIndex(i, ListView.Contain)
                return
            }
        }
        languageList.currentIndex = 0
    }

    function openChooser() {
        fromHeader = false
        selectId("en")
        open()
    }

    function openFromHeader() {
        fromHeader = true
        selectId(EditorState.preferences.uiLanguage)
        open()
    }

    // Concrete languages only — "System default" is a later choice from the header
    // (desktop) or Settings (Android). First launch always writes an explicit code.
    readonly property var chooserLanguages: {
        const all = EditorState.preferences.uiLanguages
        const out = []
        for (let i = 0; i < all.length; ++i) {
            if (all[i].id !== "")
                out.push(all[i])
        }
        return out
    }

    readonly property var pickerLanguages: fromHeader ? EditorState.preferences.uiLanguages
                                                       : chooserLanguages

    onAccepted: {
        if (!fromHeader)
            EditorState.preferences.chooseUiLanguage(selectedId)
    }

    contentItem: Column {
        spacing: Theme.spacingXl
        width: parent ? parent.width : 320

        ThemedLabel {
            width: parent.width
            size: "sm"
            wrapMode: Text.WordWrap
            text: root.fromHeader
                  ? qsTr("Language for menus and labels. Takes effect immediately.")
                  : qsTr("Pick the language for menus and labels. You can change this later.")
        }

        Rectangle {
            width: parent.width
            height: Math.min(languageList.contentHeight + 2,
                             Math.min(280, root.availableContentHeight - 48))
            radius: Theme.radiusSm
            color: Theme.appBackground
            border.width: Theme.borderWidth
            border.color: Theme.panelBorder
            clip: true

            ListView {
                id: languageList
                anchors.fill: parent
                anchors.margins: 1
                clip: true
                model: root.pickerLanguages
                interactive: contentHeight > height
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: AppScrollBar { }

                delegate: ItemDelegate {
                    id: row
                    required property var modelData
                    required property int index
                    width: languageList.width
                    height: 44
                    highlighted: modelData.id === root.selectedId
                    hoverEnabled: true

                    background: Rectangle {
                        color: {
                            if (row.highlighted)
                                return Theme.panelSecondaryBg
                            if (row.hovered)
                                return Theme.popoverHover
                            return "transparent"
                        }
                    }

                    contentItem: Item {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12

                        Text {
                            anchors.left: parent.left
                            anchors.right: checkIcon.left
                            anchors.rightMargin: Theme.spacingLg
                            anchors.verticalCenter: parent.verticalCenter
                            text: row.modelData.label
                            elide: Text.ElideRight
                            color: row.highlighted
                                   ? Theme.panelSecondaryForeground
                                   : Theme.panelForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeSm
                            font.weight: row.highlighted ? Font.Medium : Font.Normal
                        }

                        IconGlyph {
                            id: checkIcon
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            glyph: Theme.icons.check
                            iconSize: 16
                            iconColor: Theme.panelSecondaryForeground
                            visible: row.highlighted
                        }
                    }

                    onClicked: {
                        root.selectedId = modelData.id
                        // Apply now so the dialog (and the rest of the UI) switches
                        // before Continue — same live retranslate as the header.
                        EditorState.preferences.uiLanguage = modelData.id
                        if (root.fromHeader)
                            root.close()
                    }
                }
            }
        }
    }
}
