#pragma once

#include <QList>
#include <QObject>
#include <QStringList>
#include <QUrl>

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

    // The file the app was launched with (ACTION_VIEW on a .drift project from a file manager),
    // or an empty URL. Consumed by the first call: the activity keeps its launch intent for the
    // life of the process, so an unconsumed one would reopen the project on every check.
    Q_INVOKABLE QUrl takeLaunchUrl();

signals:
    // A .drift tapped in a file manager while this process was already running. Nothing polls
    // for that case — takeLaunchUrl() only runs once, at QML startup — so the warm-start intent
    // is pushed instead. Never emitted on desktop.
    void launchUrlReceived(const QUrl &url);

private:
#ifdef Q_OS_ANDROID
    class NewIntentBridge;
    NewIntentBridge *m_newIntentBridge = nullptr;
    QUrl m_pendingLaunchUrl;
#endif
};
