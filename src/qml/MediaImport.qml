pragma Singleton
import QtQuick
import Drift

// Import policy in one place: run the import, then say what happened.
//
// This lived inside AssetsPanel, which is constructed lazily inside a Popup on the phone — so
// nothing could import media before the assets sheet had been opened at least once. The home
// screen needs to import before any panel exists (New project goes straight to the picker), and
// so does the Android share target, which receives clips while the app may not even be in the
// editor. Neither has an AssetsPanel to borrow.
//
// The reporting is the whole reason this is not just a call to AssetLibrary: a count-before /
// count-after comparison is the only way to tell how many files were rejected, and the wording
// differs for the Flatpak drag case, where the sandbox hands over a host path it cannot open.
QtObject {
    id: mediaImport

    property int _requested: 0
    property int _countBefore: 0
    property bool _fromDrop: false
    // Invoked with the number of assets actually added, once the import settles.
    property var _onFinished: null

    // Asset ids still being probed, and what to run once every one of them is done.
    property var _awaitIds: []
    property var _awaitCallback: null

    readonly property bool importing: AssetLibrary.importing || EditorState.importingFolder

    function importUrls(urls, fromDrop, onFinished) {
        if (!urls || urls.length === 0)
            return false
        // .mogrt is a template package, not media: it goes to the project rather than the
        // asset bin and never reaches AssetLibrary. Split here rather than in AssetsPanel
        // so the home screen and the share target route it the same way.
        let media = []
        for (let i = 0; i < urls.length; ++i) {
            const u = urls[i]
            const name = (typeof u === "string" ? u : u.toString()).toLowerCase()
            if (name.endsWith(".mogrt"))
                EditorState.importMogrt(u)
            else
                media.push(u)
        }
        if (media.length === 0)
            return false
        urls = media
        // Async, because on Android reading a picked file means copying it out of the SAF
        // stream first. Run inline, that copy blocked the GUI thread for the whole transfer —
        // which also meant the "Importing…" overlay was set and cleared inside one JS turn and
        // never painted at all.
        const before = AssetLibrary.count
        if (!AssetLibrary.importUrlsAsync(urls)) {
            Toasts.warning(qsTr("An import is already running."))
            return false
        }
        mediaImport._requested = urls.length
        mediaImport._countBefore = before
        mediaImport._fromDrop = !!fromDrop
        mediaImport._onFinished = onFinished || null
        return true
    }

    // The picker-then-import path the home screen and the add menu both want, so neither has to
    // know which picker is right for this platform.
    //
    // Android 13+ gets the system photo picker: a media grid rather than a file browser, and the
    // right shape for "add a video". It answers asynchronously, which is why this hands its result
    // to the same callback rather than returning it — openFiles() blocks and returns, the picker
    // does not, and no caller can be written against both shapes at once.
    //
    // SAF stays the fallback, and stays reachable in its own right as browseAndImport(): the photo
    // picker only offers images and video, and its grants are not persistable, so audio, subtitles
    // and anything that has to survive a restart still need a document.
    property var _pickCallback: null

    function pickAndImport(onFinished) {
        if (FileDialogs.pickVisualMedia(true)) {
            mediaImport._pickCallback = onFinished || null
            return true
        }
        return mediaImport.browseAndImport(onFinished)
    }

    function browseAndImport(onFinished) {
        const urls = FileDialogs.openFiles(qsTr("Import Media"), [AssetLibrary.mediaNameFilter()])
        if (!urls || urls.length === 0)
            return false
        return mediaImport.importUrls(urls, false, onFinished)
    }

    property Connections _pickWatch: Connections {
        target: FileDialogs
        function onVisualMediaPicked(urls) {
            const done = mediaImport._pickCallback
            mediaImport._pickCallback = null
            // Backed out of the picker. The callers treat "nothing added" as a reason to stop,
            // so answer with the same zero an empty import would have produced.
            if (!urls || urls.length === 0) {
                if (done)
                    done(0)
                return
            }
            if (!mediaImport.importUrls(urls, false, done) && done)
                done(0)
        }
    }

    // importFiles() inserts each asset as a placeholder — id, name, path, kind — and starts the
    // probe on a worker thread; applyImportResult() posts the real width/height/fps/duration back
    // with a queued connection. importFinished is emitted before any of that can run, and queued
    // means it cannot run until the current handler returns, so an asset is *always* unprobed at
    // that moment — deterministically, not as a race.
    //
    // Everything a caller does with a new asset needs those numbers: the canvas is inferred from
    // its resolution and frame rate, and the clip's length is its duration. Reading them one turn
    // too early gave a canvas of the project defaults (locked in by markProjectLayoutChosen, so
    // nothing corrected it later) and a clip cut to the 5s image fallback.
    //
    // Exposed rather than kept private to the import path: a marketplace download lands through
    // AssetLibrary::importLocalPaths and inherits exactly the same placeholder-then-probe shape,
    // so it needs the same wait.
    function awaitProbes(assetIds, callback) {
        mediaImport._awaitIds = assetIds || []
        mediaImport._awaitCallback = callback || null
        if (mediaImport._probesSettled()) {
            mediaImport._flushAwait()
            return
        }
        probeTimeout.restart()
    }

    function _probesSettled() {
        const ids = mediaImport._awaitIds
        for (let i = 0; i < ids.length; ++i) {
            // A probe that fails removes its row, which also drops it from the pending set — so
            // a vanished asset reads as settled rather than hanging the wait forever.
            if (ids[i].length > 0 && AssetLibrary.isImportPending(ids[i]))
                return false
        }
        return true
    }

    function _flushAwait() {
        probeTimeout.stop()
        mediaImport._awaitIds = []
        const done = mediaImport._awaitCallback
        mediaImport._awaitCallback = null
        if (done)
            done()
    }

    property Connections _probeWatch: Connections {
        target: AssetLibrary
        function onAssetMetadataChanged(assetId) {
            if (mediaImport._awaitIds.length > 0 && mediaImport._probesSettled())
                mediaImport._flushAwait()
        }
    }

    // A probe that wedges emits nothing at all, and the caller must not be stranded — on the
    // phone that is a home screen that never opens the editor. applyInferredSetup() re-checks the
    // asset, so arriving here early leaves the canvas unset rather than wrong.
    property Timer _probeTimeout: Timer {
        id: probeTimeout
        interval: 15000
        onTriggered: if (mediaImport._awaitIds.length > 0) mediaImport._flushAwait()
    }

    function openFailedMessage(requested) {
        if (mediaImport._fromDrop && AssetLibrary.sandboxed) {
            return requested === 1
                ? qsTr("Could not open that file. This package cannot read files dropped from other apps — use Import to pick them instead.")
                : qsTr("Could not open those files. This package cannot read files dropped from other apps — use Import to pick them instead.")
        }
        return requested === 1
            ? qsTr("Could not open that file. It may have been moved, or you may not have permission to read it.")
            : qsTr("Could not open any of the selected files.")
    }

    property Connections _finishWatch: Connections {
        target: AssetLibrary
        function onImportFinished(materialized, failed) {
            const requested = mediaImport._requested
            if (requested <= 0)
                return
            mediaImport._requested = 0
            const added = AssetLibrary.count - mediaImport._countBefore
            const skipped = requested - added
            if (added > 0 && skipped > 0) {
                if (mediaImport._fromDrop && AssetLibrary.sandboxed)
                    Toasts.warning(qsTr("Imported %1 of %2 files. The rest could not be opened — this package cannot read files dropped from other apps. Use Import instead.")
                                   .arg(added).arg(requested))
                else
                    Toasts.warning(qsTr("Imported %1 of %2 files. %3 could not be read.")
                                   .arg(added).arg(requested).arg(skipped))
            } else if (added > 0) {
                Toasts.success(qsTr("Imported %n files.", "", added))
            } else if (failed > 0) {
                Toasts.error(mediaImport.openFailedMessage(requested))
            } else if (materialized > 0) {
                Toasts.success(qsTr("Imported %n files.", "", requested))
            } else if (requested === 1) {
                Toasts.error(qsTr("Could not import that file — the format may be unsupported."))
            } else {
                Toasts.error(qsTr("Could not import any of the %n selected files.", "", requested))
            }

            if (!mediaImport._onFinished)
                return

            let ids = []
            for (let i = mediaImport._countBefore; i < AssetLibrary.count; ++i)
                ids.push(AssetLibrary.assetIdAt(i))

            mediaImport.awaitProbes(ids, function () {
                // Recounted rather than reusing `added` above: a probe that fails removes its
                // row, so the real count can be lower by now — and the callers turn this number
                // back into an index as `AssetLibrary.count - added`.
                const settled = Math.max(0, AssetLibrary.count - mediaImport._countBefore)
                const done = mediaImport._onFinished
                mediaImport._onFinished = null
                if (done)
                    done(settled)
            })
        }
    }
}
