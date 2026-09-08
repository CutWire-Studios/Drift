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
    function pickAndImport(onFinished) {
        const urls = FileDialogs.openFiles(qsTr("Import Media"), [AssetLibrary.mediaNameFilter()])
        if (!urls || urls.length === 0)
            return false
        return mediaImport.importUrls(urls, false, onFinished)
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

            const done = mediaImport._onFinished
            mediaImport._onFinished = null
            if (done)
                done(added)
        }
    }
}
