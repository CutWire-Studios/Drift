import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Drift 1.0
import "components"

// Marketplace downloads: what is running, what is waiting behind the concurrency cap, and
// what this session already finished. A window rather than a dialog because a download
// outlives the panel that started it — the Market tab can be closed, or the app switched to
// another project, while three clips are still being fetched.
//
// History is session-only by design. The files themselves land in the media bin and in the
// folder the user picked, so the list is a progress view, not a record to preserve.
Window {
    id: root

    width: 620
    height: 460
    minimumWidth: 460
    minimumHeight: 320
    title: qsTr("Downloads")
    color: Theme.appBackground

    readonly property var jobs: Market.downloads
    readonly property int activeCount: Market.activeDownloadCount
    readonly property bool hasFinished: {
        const list = Market.downloads
        for (var i = 0; i < list.length; ++i) {
            if (list[i].finished)
                return true
        }
        return false
    }

    function show() {
        root.visible = true
        root.raise()
        root.requestActivate()
    }

    // The seven formatters moved to components/DownloadFormat.qml when the phone gained a
    // downloads sheet of its own; these forward so every call site in this file is unchanged.
    function formatBytes(n) { return DownloadFormat.formatBytes(n) }
    function formatSpeed(bytesPerSec) { return DownloadFormat.formatSpeed(bytesPerSec) }
    function formatRemaining(job) { return DownloadFormat.formatRemaining(job) }
    function kindGlyph(job) { return DownloadFormat.kindGlyph(job) }
    function statusGlyph(job) { return DownloadFormat.statusGlyph(job) }
    function statusColor(job) { return DownloadFormat.statusColor(job) }
    function detailLine(job) { return DownloadFormat.detailLine(job) }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.pagePadding
            spacing: Theme.spacingSm

            Text {
                text: root.activeCount > 0
                      ? qsTr("%n active", "", root.activeCount)
                      : qsTr("No downloads running")
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeBase
                font.weight: Font.DemiBold
            }

            // Says why something sits at Waiting, so the cap does not read as a stall.
            ThemedLabel {
                visible: root.activeCount > Market.maxConcurrentDownloads
                text: qsTr("%1 at a time").arg(Market.maxConcurrentDownloads)
            }

            Item { Layout.fillWidth: true }

            ThemedButton {
                text: qsTr("Clear finished")
                variant: "ghost"
                enabled: root.hasFinished
                onClicked: Market.clearFinishedDownloads()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.borderWidth
            color: Theme.border
        }

        EmptyState {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.jobs.length === 0
            glyph: Theme.icons.download
            title: qsTr("Nothing downloaded yet")
            hint: qsTr("Downloads from the Market tab show up here while they run.")
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.jobs.length > 0
            clip: true
            model: root.jobs
            spacing: 0
            ScrollBar.vertical: AppScrollBar { }

            delegate: Rectangle {
                id: row
                required property var modelData
                width: list.width
                height: rowLayout.implicitHeight + Theme.spacingXl * 2
                color: rowHover.hovered ? Theme.popoverHover : "transparent"

                Behavior on color {
                    ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                }

                HoverHandler { id: rowHover }

                RowLayout {
                    id: rowLayout
                    anchors.fill: parent
                    anchors.margins: Theme.spacingXl
                    spacing: Theme.spacingXl

                    // The file leads and the status rides on it as an emblem, the way a
                    // download list is normally read: what it is first, how it is going second.
                    Rectangle {
                        Layout.alignment: Qt.AlignTop
                        implicitWidth: Theme.spacing3xl + Theme.spacingLg
                        implicitHeight: Theme.spacing3xl + Theme.spacingLg
                        radius: Theme.radiusSm
                        color: Theme.panelAccent

                        IconGlyph {
                            anchors.centerIn: parent
                            glyph: root.kindGlyph(row.modelData)
                            iconSize: Theme.iconSizeBase
                            iconColor: Theme.mutedForeground
                        }

                        // Painted on the window background rather than the tile so the
                        // emblem stays legible where it overhangs the corner.
                        Rectangle {
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.margins: -Theme.spacingXs
                            width: Theme.spacingXl + Theme.spacingXs
                            height: width
                            radius: width / 2
                            color: Theme.appBackground

                            IconGlyph {
                                anchors.centerIn: parent
                                glyph: root.statusGlyph(row.modelData)
                                iconSize: Theme.iconSizeMd
                                iconColor: root.statusColor(row.modelData)
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSm

                        Text {
                            Layout.fillWidth: true
                            text: row.modelData.title
                            color: Theme.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeSm
                            elide: Text.ElideRight
                        }

                        // Shown only once the length is known: a bar parked at 0 through
                        // "Preparing…" reads as a stall, and the phase line says more.
                        ThemedProgressBar {
                            Layout.fillWidth: true
                            visible: row.modelData.status === "downloading"
                                     && Number(row.modelData.bytesTotal) > 0
                            value: Number(row.modelData.progress || 0)
                        }

                        Text {
                            Layout.fillWidth: true
                            text: root.detailLine(row.modelData)
                            color: row.modelData.status === "failed" ? Theme.destructive
                                                                    : Theme.mutedForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeXs
                            elide: Text.ElideRight
                            wrapMode: Text.WordWrap
                            maximumLineCount: 2
                        }

                        // Where the file went. Worth showing because the user chose it, and
                        // a download that landed somewhere they did not expect is the thing
                        // they will come here to check.
                        Text {
                            Layout.fillWidth: true
                            visible: row.modelData.status === "done"
                                     && String(row.modelData.destinationDir).length > 0
                            text: row.modelData.destinationDir
                            color: Theme.mutedForeground
                            font.family: Theme.monoFontFamily
                            font.pixelSize: Theme.fontSizeXs
                            elide: Text.ElideLeft
                        }
                    }

                    Text {
                        Layout.alignment: Qt.AlignVCenter
                        visible: row.modelData.status === "downloading"
                                 && Number(row.modelData.bytesTotal) > 0
                        text: Math.round(Number(row.modelData.progress || 0) * 100) + "%"
                        color: Theme.mutedForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                    }

                    IconButton {
                        Layout.alignment: Qt.AlignVCenter
                        visible: row.modelData.retryable
                        glyph: Theme.icons.refresh
                        tooltip: qsTr("Try again")
                        onClicked: Market.retryDownload(row.modelData.itemId)
                    }

                    IconButton {
                        Layout.alignment: Qt.AlignVCenter
                        visible: !row.modelData.finished
                        glyph: Theme.icons.x
                        tooltip: qsTr("Cancel")
                        onClicked: Market.cancelDownload(row.modelData.itemId)
                    }
                }
            }
        }
    }
}
