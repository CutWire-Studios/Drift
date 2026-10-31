import QtQuick
import QtQuick.Layouts
import Drift

Rectangle {
    id: row

    required property var page
    property var entry
    property bool optional: false
    property bool checked: false
    readonly property string state: entry ? page.rowState(entry) : "queued"
    readonly property bool pending: !optional || checked
    readonly property real fraction: entry && page.transfers[entry.id] ? page.transfers[entry.id].fraction : 0

    signal toggled(bool on)

    implicitHeight: body.implicitHeight + (page.mobile ? Theme.spacing2xl : Theme.spacingXl) * 2
    radius: Theme.c.quickSetupCard.radius
    color: Theme.c.quickSetupCard.background
    border.width: Theme.c.quickSetupCard.borderWidth
    border.color: row.state === "failed" && pending ? Theme.c.quickSetup.danger : Theme.c.quickSetupCard.border

    ColumnLayout {
        id: body

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.margins: page.mobile ? Theme.spacingXl : Theme.spacingXl
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
                    size: page.mobile ? "base" : "sm"
                    tone: "default"
                    font.weight: Font.Medium
                    text: row.entry ? (row.entry.name || row.entry.id) : ""
                }

                ThemedLabel {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    size: page.mobile ? "sm" : "xs"
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
                    visible: row.state === "installed" || row.state === "failed"
                    glyph: row.state === "installed" ? Theme.icons.success : Theme.icons.error
                    iconSize: page.mobile ? 22 : 18
                    iconColor: row.state === "installed" ? Theme.c.quickSetup.success : Theme.c.quickSetup.danger
                }

                ThemedLabel {
                    size: page.mobile ? "sm" : "xs"
                    tone: row.state === "installed" ? "default" : "muted"
                    wrapMode: Text.NoWrap
                    text: {
                        switch (row.state) {
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
            visible: row.pending && (row.state === "downloading" || row.state === "installing")
            barHeight: Theme.spacingXs
            value: row.state === "installing" ? 1 : row.fraction
        }

        ThemedLabel {
            Layout.fillWidth: true
            visible: row.pending && row.state === "failed"
            size: page.mobile ? "sm" : "xs"
            text: row.entry ? page.rowError(row.entry) : ""
            color: Theme.c.quickSetup.danger
        }
    }
}
