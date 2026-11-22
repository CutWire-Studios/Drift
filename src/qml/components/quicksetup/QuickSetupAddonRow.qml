import QtQuick
import QtQuick.Layouts
import Drift

Rectangle {
    id: row

    required property var downloads
    property bool mobile: false
    property var entry
    property bool optional: false
    property bool checked: false
    readonly property string installState: entry ? downloads.rowState(entry) : "queued"
    readonly property bool pending: !optional || checked
    readonly property real fraction: entry && downloads.transfers[entry.id] ? downloads.transfers[entry.id].fraction : 0

    signal toggled(bool on)

    implicitHeight: body.implicitHeight + (row.mobile ? Theme.spacing2xl : Theme.spacingXl) * 2
    radius: Theme.c.quickSetupCard.radius
    color: Theme.c.quickSetupCard.background
    border.width: Theme.c.quickSetupCard.borderWidth
    border.color: row.installState === "failed" && pending ? Theme.c.quickSetup.danger : Theme.c.quickSetupCard.border

    ColumnLayout {
        id: body

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.margins: row.mobile ? Theme.spacingXl : Theme.spacingXl
        spacing: Theme.spacingMd

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingLg

            ThemedCheckBox {
                visible: row.optional
                checked: row.checked
                enabled: !(row.entry && row.entry.installed)
                onToggled: row.toggled(checked)
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingXs

                ThemedLabel {
                    Layout.fillWidth: true
                    size: row.mobile ? "base" : "sm"
                    tone: "default"
                    font.weight: Font.Medium
                    text: row.entry ? (row.entry.name || row.entry.id) : ""
                }

                ThemedLabel {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    size: row.mobile ? "sm" : "xs"
                    text: {
                        if (!row.entry)
                            return ""
                        const size = DownloadFormat.formatBytes(row.entry.sizeBytes)
                        return size.length > 0 ? qsTr("%1 · %2").arg(row.entry.description).arg(size)
                                               : row.entry.description
                    }
                }
            }

            RowLayout {
                visible: row.pending
                spacing: Theme.spacingSm

                IconGlyph {
                    visible: row.installState === "installed" || row.installState === "failed"
                    glyph: row.installState === "installed" ? Theme.icons.success : Theme.icons.error
                    iconSize: row.mobile ? 22 : 18
                    iconColor: row.installState === "installed" ? Theme.c.quickSetup.success : Theme.c.quickSetup.danger
                }

                ThemedLabel {
                    size: row.mobile ? "sm" : "xs"
                    tone: row.installState === "installed" ? "default" : "muted"
                    wrapMode: Text.NoWrap
                    text: {
                        switch (row.installState) {
                        case "installed": return qsTr("Installed")
                        case "installing": return qsTr("Installing…")
                        case "downloading": return qsTr("%1%").arg(Math.round(row.fraction * 100))
                        case "failed": return qsTr("Failed")
                        default: return qsTr("Queued")
                        }
                    }
                }
            }
        }

        ThemedProgressBar {
            Layout.fillWidth: true
            visible: row.pending && (row.installState === "downloading" || row.installState === "installing")
            barHeight: Theme.spacingXs
            value: row.installState === "installing" ? 1 : row.fraction
        }

        ThemedLabel {
            Layout.fillWidth: true
            visible: row.pending && row.installState === "failed"
            size: row.mobile ? "sm" : "xs"
            text: row.entry ? downloads.rowError(row.entry) : ""
            color: Theme.c.quickSetup.danger
        }
    }
}
