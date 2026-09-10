import QtQuick
import QtQuick.Controls.Basic
import Drift

ThemedDialog {
    id: root

    title: qsTr("Choose Sequence to Import")
    acceptText: qsTr("Import")
    rejectText: qsTr("Cancel")
    showAccept: true
    showReject: true
    preferredWidth: Theme.dialogWidthMd
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    property url projectUrl
    property var sequences: []
    property string selectedSequence: ""

    function openFor(url, seqList) {
        projectUrl = url
        sequences = seqList || []
        if (sequences.length > 0) {
            // Default to the first sequence matching "final" or index 0
            var bestIdx = 0
            for (var i = 0; i < sequences.length; ++i) {
                if (sequences[i].toLowerCase().indexOf("final") !== -1) {
                    bestIdx = i
                    break
                }
            }
            selectedSequence = sequences[bestIdx]
            sequenceList.currentIndex = bestIdx
        } else {
            selectedSequence = ""
        }
        open()
    }

    onAccepted: {
        if (projectUrl != "") {
            EditorState.loadPremiereProject(projectUrl, selectedSequence)
        }
    }

    contentItem: Column {
        spacing: Theme.spacingXl
        width: parent ? parent.width : 400

        ThemedLabel {
            width: parent.width
            size: "sm"
            wrapMode: Text.WordWrap
            text: qsTr("This Premiere Pro project contains multiple sequences. Select which sequence you want to import:")
        }

        Rectangle {
            width: parent.width
            height: Math.min(sequenceList.contentHeight + 2,
                             Math.min(280, root.availableContentHeight - 48))
            radius: Theme.radiusSm
            color: Theme.appBackground
            border.width: Theme.borderWidth
            border.color: Theme.panelBorder
            clip: true

            ListView {
                id: sequenceList
                anchors.fill: parent
                anchors.margins: 1
                clip: true
                model: root.sequences
                interactive: contentHeight > height
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: AppScrollBar { }

                delegate: ItemDelegate {
                    id: row
                    required property var modelData
                    required property int index
                    width: sequenceList.width
                    height: 44
                    highlighted: modelData === root.selectedSequence
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

                        IconGlyph {
                            id: seqIcon
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            glyph: Theme.icons.film
                            iconSize: 16
                            iconColor: row.highlighted
                                       ? Theme.panelSecondaryForeground
                                       : Theme.mutedForeground
                        }

                        Text {
                            anchors.left: seqIcon.right
                            anchors.leftMargin: Theme.spacingSm
                            anchors.right: checkIcon.left
                            anchors.rightMargin: Theme.spacingLg
                            anchors.verticalCenter: parent.verticalCenter
                            text: row.modelData
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
                        root.selectedSequence = modelData
                    }

                    onDoubleClicked: {
                        root.selectedSequence = modelData
                        root.accept()
                    }
                }
            }
        }
    }
}
