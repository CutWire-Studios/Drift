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

    // Bytes are shown at one decimal from MB up: "12.4 MB" reads as a size, "12 MB" reads
    // as a rounding, and the extra digit is what tells you a transfer is moving.
    function formatBytes(n) {
        const b = Number(n)
        if (!isFinite(b) || b <= 0)
            return ""
        if (b < 1024)
            return qsTr("%1 B").arg(b)
        if (b < 1024 * 1024)
            return qsTr("%1 KB").arg(Math.round(b / 1024))
        if (b < 1024 * 1024 * 1024)
            return qsTr("%1 MB").arg((b / (1024 * 1024)).toFixed(1))
        return qsTr("%1 GB").arg((b / (1024 * 1024 * 1024)).toFixed(2))
    }

    function formatSpeed(bytesPerSec) {
        const s = Number(bytesPerSec)
        if (!isFinite(s) || s <= 0)
            return ""
        return qsTr("%1/s").arg(formatBytes(s))
    }

    // Only meaningful while bytes are actually moving and the total is known; a source that
    // sends no Content-Length gets no guess rather than a wrong one.
    function formatRemaining(job) {
        const total = Number(job.bytesTotal)
        const done = Number(job.bytesReceived)
        const speed = Number(job.speed)
        if (job.status !== "downloading" || total <= 0 || speed <= 0 || done >= total)
            return ""
        const secs = Math.round((total - done) / speed)
        if (secs < 60)
            return qsTr("%n second(s) left", "", secs)
        return qsTr("%n minute(s) left", "", Math.round(secs / 60))
    }

    // What the file is, from the catalog item. Falls back to a generic file for a job
    // started before the kind was recorded, or by anything that does not pass one.
    function kindGlyph(job) {
        const kind = String(job.mediaKind || "")
        if (kind === "audio")
            return Theme.icons.music
        if (kind === "photo" || kind === "image")
            return Theme.icons.image
        if (kind === "video")
            return Theme.icons.film
        return Theme.icons.fileText
    }

    function statusGlyph(job) {
        if (job.status === "done")
            return Theme.icons.check
        if (job.status === "failed")
            return Theme.icons.error
        if (job.status === "cancelled")
            return Theme.icons.x
        if (job.status === "waiting")
            return Theme.icons.clock
        return Theme.icons.download
    }

    function statusColor(job) {
        if (job.status === "failed")
            return Theme.destructive
        if (job.status === "done")
            return Theme.primary
        return Theme.mutedForeground
    }

    // The line under the title. Errors win: a failed job's reason is the whole reason the
    // row is still on screen.
    function detailLine(job) {
        if (job.status === "failed")
            return job.errorMessage
        if (job.status === "cancelled")
            return qsTr("Cancelled")
        if (job.status === "waiting")
            return qsTr("Waiting for a free slot")
        if (job.status === "done") {
            const size = formatBytes(job.bytesReceived)
            return size.length > 0 ? qsTr("%1 · in the media bin").arg(size)
                                   : qsTr("In the media bin")
        }
        if (job.status === "downloading") {
            const parts = []
            const total = formatBytes(job.bytesTotal)
            const done = formatBytes(job.bytesReceived)
            if (total.length > 0)
                parts.push(qsTr("%1 of %2").arg(done).arg(total))
            else if (done.length > 0)
                parts.push(done)
            const speed = formatSpeed(job.speed)
            if (speed.length > 0)
                parts.push(speed)
            const left = formatRemaining(job)
            if (left.length > 0)
                parts.push(left)
            return parts.join(" · ")
        }
        return job.phase
    }

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
