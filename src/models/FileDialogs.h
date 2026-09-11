#pragma once

#include <QList>
#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

// QML-facing wrapper around QFileDialog so file pickers use the native
// platform dialog (xdg-desktop-portal under Flatpak; Android Storage Access
// Framework / ACTION_OPEN_DOCUMENT on Android) instead of the QtQuick.Dialogs
// QML fallback. On Android, selected URLs are content:// URIs — callers must
// materialize them to a real path before handing them to FFmpeg.
class FileDialogs : public QObject
{
    Q_OBJECT

public:
    explicit FileDialogs(QObject *parent = nullptr);
    ~FileDialogs() override;

    Q_INVOKABLE QUrl openFile(const QString &title, const QStringList &nameFilters,
                              const QStringList &mimeTypeFilters = QStringList()) const;
    Q_INVOKABLE QList<QUrl> openFiles(const QString &title, const QStringList &nameFilters) const;
    // Native directory picker, for importing a folder's contents with its structure preserved.
    // Empty URL when cancelled. Not offered on Android: SAF's tree picker (ACTION_OPEN_DOCUMENT_TREE)
    // is a different flow than the document picker this class wraps, and isn't wired up.
    // startDir seeds the dialog's location; empty opens wherever the platform defaults to.
    Q_INVOKABLE QUrl openDirectory(const QString &title, const QUrl &startDir = QUrl()) const;
    // False where there is no directory picker at all (Android), so a caller can skip the
    // prompt instead of reading its empty result as the user cancelling.
    Q_INVOKABLE bool supportsDirectoryPicker() const;
    // `suffix` is appended to `suggestedName` for the picker's initial file name; the path the
    // dialog returns is used exactly as given. `initialDirectory` opens the picker in that folder
    // when it exists (e.g. the last export location). `mimeTypeFilters` are used when those types
    // are in the MIME database (so the portal can label a new .drift); otherwise `nameFilters`.
    Q_INVOKABLE QUrl saveFile(const QString &title, const QStringList &nameFilters,
                              const QString &suggestedName = QString(),
                              const QString &suffix = QString(),
                              const QString &initialDirectory = QString(),
                              const QStringList &mimeTypeFilters = QStringList()) const;

    // Android share sheet (ACTION_SEND) for a content:// URI — a finished export as
    // Exporter::publishToGallery left it in the media library. `mimeType` is read from the
    // provider when empty. False on desktop, and for anything that is not a content:// URI:
    // the sheet can only hand another app a URI it is allowed to read.
    Q_INVOKABLE bool shareFile(const QUrl &url, const QString &mimeType = QString()) const;

    // Hands a content:// URI to whatever the device plays it with (ACTION_VIEW), as opposed to
    // shareFile's ACTION_SEND. Same constraint and the same reason: only a content:// URI carries
    // a grant the receiving app can act on. False on desktop.
    Q_INVOKABLE bool viewFile(const QUrl &url, const QString &mimeType = QString()) const;

    // The file the app was launched with (ACTION_VIEW on a .drift project from a file manager),
    // or an empty URL. Consumed by the first call: the activity keeps its launch intent for the
    // life of the process, so an unconsumed one would reopen the project on every check.
    Q_INVOKABLE QUrl takeLaunchUrl();

    // The launch intent in full, for the share targets. Same consume-once rule as
    // takeLaunchUrl(), and the same empty result on desktop. Shape:
    //
    //   kind      "view" | "sendMedia" | "sendText"
    //   urls      list of QUrl — one for VIEW and ACTION_SEND, several for SEND_MULTIPLE
    //   text      the shared text, for "sendText" only
    //   mimeType  the intent's type, or empty
    //
    // An empty map means there was nothing to act on. Every consumed extra is stripped from the
    // activity's intent before returning: a configuration change re-reads getIntent(), and a
    // SEND that still carried its payload would import the same clip again on every rotation.
    Q_INVOKABLE QVariantMap takeLaunchIntent();

    // Android 13+ photo picker (MediaStore.ACTION_PICK_IMAGES). Permission-free and scoped to
    // what the user taps, like SAF, but a media grid rather than a file browser.
    //
    // Asynchronous, which is the whole reason it is not a drop-in for openFiles(): that one
    // blocks on QFileDialog::exec() and returns the result, this one returns immediately and
    // answers on visualMediaPicked. False means it could not be started at all — below API 33,
    // or no activity — and the caller should fall back to openFiles().
    //
    // The grants it hands back are NOT persistable, so this cannot replace SAF everywhere: an
    // asset that has to survive a restart still needs a document the app can re-acquire.
    Q_INVOKABLE bool pickVisualMedia(bool allowMultiple = true);

signals:
    // A .drift tapped in a file manager while this process was already running. Nothing polls
    // for that case — takeLaunchUrl() only runs once, at QML startup — so the warm-start intent
    // is pushed instead. Never emitted on desktop.
    void launchUrlReceived(const QUrl &url);

    // The warm-start counterpart of takeLaunchIntent(): a share that arrived while the process
    // was already running. Carries the same map. Never emitted on desktop.
    void incomingIntent(const QVariantMap &intent);

    // Result of pickVisualMedia(). An empty list means the user backed out.
    void visualMediaPicked(const QList<QUrl> &urls);

private:
#ifdef Q_OS_ANDROID
    class NewIntentBridge;
    NewIntentBridge *m_newIntentBridge = nullptr;
    class PickResultBridge;
    PickResultBridge *m_pickBridge = nullptr;
    QUrl m_pendingLaunchUrl;
#endif
};
