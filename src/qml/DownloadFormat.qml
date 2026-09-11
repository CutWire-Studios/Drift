pragma Singleton
import QtQuick
import Drift

// How a download job renders as words. Shared, because there are two hosts for the same jobs —
// the desktop DownloadsWindow and the phone's AndroidDownloadsSheet — and a size or a status
// that reads differently in the two is a bug that nothing would catch.
QtObject {
    id: downloadFormat

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
        return qsTr("%1/s").arg(downloadFormat.formatBytes(s))
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
            const size = downloadFormat.formatBytes(job.bytesReceived)
            return size.length > 0 ? qsTr("%1 · in the media bin").arg(size)
                                   : qsTr("In the media bin")
        }
        if (job.status === "downloading") {
            const parts = []
            const total = downloadFormat.formatBytes(job.bytesTotal)
            const done = downloadFormat.formatBytes(job.bytesReceived)
            if (total.length > 0)
                parts.push(qsTr("%1 of %2").arg(done).arg(total))
            else if (done.length > 0)
                parts.push(done)
            const speed = downloadFormat.formatSpeed(job.speed)
            if (speed.length > 0)
                parts.push(speed)
            const left = downloadFormat.formatRemaining(job)
            if (left.length > 0)
                parts.push(left)
            return parts.join(" · ")
        }
        return job.phase
    }
}
