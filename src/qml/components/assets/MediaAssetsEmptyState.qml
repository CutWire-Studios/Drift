import QtQuick
import Drift
// First-run screen for a project with no media at all — folders included. This area used
// to render as a blank rectangle, with no hint that the panel accepts drops or that an
// Import button exists. Hand-built rather than the shared EmptyState: it carries two
// actions and the supported-format table, which no other empty state needs, and it
// centres in the whole panel instead of sitting against the top edge.
Item {
    id: root

    property bool importing: false
    signal importRequested()
    signal importFolderRequested()

    anchors.fill: parent
    visible: AssetLibrary.count === 0 && BinFolderModel.count === 0 && !importing

    Column {
        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.spacing3xl * 2, 300)
        spacing: Theme.spacingXl

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 48
            height: 48
            radius: Theme.c.mediaAssetsEmptyState.iconRadius
            color: Theme.c.mediaAssetsEmptyState.iconBackground

            IconGlyph {
                anchors.centerIn: parent
                glyph: Theme.icons.film
                iconSize: Theme.iconSizeXl
                iconColor: Theme.c.mediaAssetsEmptyState.icon
            }
        }

        Text {
            width: parent.width
            text: qsTr("No media yet")
            color: Theme.c.mediaAssetsEmptyState.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
            font.weight: Font.Medium
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        Text {
            width: parent.width
            // The formats table below now carries the kind list, so the hint only has to
            // say how media gets in and what to do with it once it is here.
            text: qsTr("Import files or drop them here, then drag them onto the timeline. Right-click a clip to preview and trim it first.")
            color: Theme.c.mediaAssetsEmptyState.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: Theme.spacingLg

            ThemedButton {
                text: qsTr("Import media")
                variant: "primary"
                glyph: Theme.icons.upload
                onClicked: importRequested()
            }

            ThemedButton {
                visible: FileDialogs.supportsDirectoryPicker()
                text: qsTr("Import folder")
                glyph: Theme.icons.folderInput
                onClicked: importFolderRequested()
            }
        }

        ThemedButton {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: !!ShellBridge.main && !!ShellBridge.main.openMarketplace
            text: qsTr("Browse the Marketplace")
            glyph: Theme.icons.store
            onClicked: ShellBridge.main.openMarketplace("")
        }

        Rectangle {
            width: parent.width
            height: formatColumn.height + Theme.spacingXl * 2
            radius: Theme.c.mediaAssetsEmptyState.formatsRadius
            color: "transparent"
            border.width: Theme.c.mediaAssetsEmptyState.formatsBorderWidth
            border.color: Theme.c.mediaAssetsEmptyState.formatsBorder

            Column {
                id: formatColumn
                anchors.centerIn: parent
                width: parent.width - Theme.spacingXl * 2
                spacing: Theme.spacingMd

                Text {
                    width: parent.width
                    text: qsTr("Supported formats")
                    color: Theme.c.mediaAssetsEmptyState.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeTiny
                    font.weight: Font.Medium
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 0.5
                }

                Repeater {
                    model: [
                        { glyph: Theme.icons.video, label: qsTr("Video"),
                          formats: "MP4 · MOV · MKV · WEBM · AVI" },
                        { glyph: Theme.icons.music, label: qsTr("Audio"),
                          formats: "MP3 · WAV · FLAC · AAC · OGG" },
                        { glyph: Theme.icons.image, label: qsTr("Images"),
                          formats: "PNG · JPG · WEBP · HEIC · GIF" },
                        { glyph: Theme.icons.shapes, label: qsTr("Vector"),
                          formats: "SVG · Lottie (.json, .lottie)" },
                        { glyph: Theme.icons.box, label: qsTr("3D"),
                          formats: qsTr("glTF binary (.glb) · Gaussian splats (.sog, .ply, .zip)") },
                        { glyph: Theme.icons.fileText, label: qsTr("PDF"),
                          formats: qsTr("PDF (needs the PDF Viewer addon)") }
                    ]

                    delegate: MediaAssetsFormatRow {
                        columnWidth: formatColumn.width
                    }
                }
            }
        }
    }
}
