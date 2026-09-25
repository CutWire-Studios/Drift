#include "FileDialogs.h"

#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMimeDatabase>
#include <QMimeType>
#include <QSettings>
#include <QWindow>

#ifdef Q_OS_ANDROID
#include <QJniEnvironment>
#include <QJniObject>
#include <QtCore/private/qjnihelpers_p.h>
#include <QtCore/qcoreapplication_platform.h>
#endif

#ifdef Q_OS_ANDROID
namespace {
FileDialogs *g_newIntentOwner = nullptr;

QJniObject javaString(const char *value)
{
    return QJniObject::fromString(QString::fromLatin1(value));
}

// Reads an Intent into the map QML consumes and strips what it consumed.
//
// Clearing is not tidiness: a configuration change re-reads getIntent(), so a SEND whose extras
// survived would hand back the same clip and import it again on every rotation. VIEW got away
// with setData(null) alone because its payload *is* the data URI; SEND carries its payload in the
// extras, and the action has to go too or the next read still routes down the send branch.
QVariantMap describeIntent(const QJniObject &intent)
{
    QVariantMap out;
    if (!intent.isValid())
        return out;

    const QString action = intent.callObjectMethod("getAction", "()Ljava/lang/String;").toString();
    const QString mimeType = intent.callObjectMethod("getType", "()Ljava/lang/String;").toString();

    if (action == QLatin1String("android.intent.action.VIEW")) {
        const QJniObject data = intent.callObjectMethod("getData", "()Landroid/net/Uri;");
        if (!data.isValid())
            return out;
        out.insert(QStringLiteral("kind"), QStringLiteral("view"));
        out.insert(QStringLiteral("urls"), QVariantList{QUrl(data.toString())});
        out.insert(QStringLiteral("mimeType"), mimeType);
        intent.callObjectMethod("setData", "(Landroid/net/Uri;)Landroid/content/Intent;",
                                static_cast<jobject>(nullptr));
        QJniEnvironment().checkAndClearExceptions();
        return out;
    }

    const bool isSend = action == QLatin1String("android.intent.action.SEND");
    const bool isSendMultiple = action == QLatin1String("android.intent.action.SEND_MULTIPLE");
    if (!isSend && !isSendMultiple)
        return out;

    QVariantList urls;
    QString text;

    if (isSend && mimeType.startsWith(QLatin1String("text/"))) {
        text = intent
                   .callObjectMethod("getStringExtra",
                                     "(Ljava/lang/String;)Ljava/lang/String;",
                                     javaString("android.intent.extra.TEXT").object<jstring>())
                   .toString();
    } else if (isSend) {
        // The one-argument overload on purpose. getParcelableExtra(String, Class) is API 33 and
        // throws NoSuchMethodError at runtime on this app's min SDK 28 — which no build step
        // catches, because the call is resolved by signature at run time.
        const QJniObject uri =
            intent.callObjectMethod("getParcelableExtra",
                                    "(Ljava/lang/String;)Landroid/os/Parcelable;",
                                    javaString("android.intent.extra.STREAM").object<jstring>());
        if (uri.isValid())
            urls.append(QUrl(uri.toString()));
    } else {
        const QJniObject list = intent.callObjectMethod(
            "getParcelableArrayListExtra", "(Ljava/lang/String;)Ljava/util/ArrayList;",
            javaString("android.intent.extra.STREAM").object<jstring>());
        if (list.isValid()) {
            const jint count = list.callMethod<jint>("size", "()I");
            for (jint i = 0; i < count; ++i) {
                const QJniObject uri =
                    list.callObjectMethod("get", "(I)Ljava/lang/Object;", i);
                if (uri.isValid())
                    urls.append(QUrl(uri.toString()));
            }
        }
    }
    QJniEnvironment().checkAndClearExceptions();

    if (urls.isEmpty() && text.isEmpty())
        return out;

    intent.callMethod<void>("removeExtra", "(Ljava/lang/String;)V",
                            javaString("android.intent.extra.STREAM").object<jstring>());
    intent.callMethod<void>("removeExtra", "(Ljava/lang/String;)V",
                            javaString("android.intent.extra.TEXT").object<jstring>());
    intent.callObjectMethod("setAction", "(Ljava/lang/String;)Landroid/content/Intent;",
                            static_cast<jobject>(nullptr));
    QJniEnvironment().checkAndClearExceptions();

    out.insert(QStringLiteral("kind"),
               urls.isEmpty() ? QStringLiteral("sendText") : QStringLiteral("sendMedia"));
    out.insert(QStringLiteral("urls"), urls);
    out.insert(QStringLiteral("text"), text);
    out.insert(QStringLiteral("mimeType"), mimeType);
    return out;
}
}

// A warm-start ACTION_VIEW never shows up in getIntent(): QtActivityBase::onNewIntent forwards
// the intent to QtNative without calling setIntent(), so the activity keeps returning the intent
// it was cold-started with. Qt's private new-intent listener is the only place it surfaces.
class FileDialogs::NewIntentBridge : public QtAndroidPrivate::NewIntentListener
{
public:
    explicit NewIntentBridge(FileDialogs *owner) : m_owner(owner) {}

    bool handleNewIntent(JNIEnv *env, jobject intent) override
    {
        Q_UNUSED(env);
        // No action filter here any more. Sharing into an already-running Drift arrives through
        // this path and nowhere else, so rejecting everything but VIEW meant a share to a warm
        // process silently did nothing at all.
        const QVariantMap described = describeIntent(QJniObject(intent));
        if (described.isEmpty())
            return false;

        // Called on the Android UI thread; everything downstream of the signal (loadProject and
        // the QML it drives) is GUI-thread only.
        FileDialogs *owner = m_owner;
        QMetaObject::invokeMethod(
            owner,
            [owner, described] {
                if (described.value(QStringLiteral("kind")).toString()
                    == QLatin1String("view")) {
                    const QVariantList urls =
                        described.value(QStringLiteral("urls")).toList();
                    if (!urls.isEmpty()) {
                        const QUrl url = urls.first().toUrl();
                        owner->m_pendingLaunchUrl = url;
                        emit owner->launchUrlReceived(url);
                    }
                }
                emit owner->incomingIntent(described);
            },
            Qt::QueuedConnection);
        return true;
    }

private:
    FileDialogs *const m_owner;
};

// The photo picker answers on the activity result, so the call and its result are two separate
// events and something has to bridge them. One request code, because only one picker can be up.
class FileDialogs::PickResultBridge : public QtAndroidPrivate::ActivityResultListener
{
public:
    static constexpr jint kRequestCode = 0x4472; // 'Dr'

    explicit PickResultBridge(FileDialogs *owner) : m_owner(owner) {}

    bool handleActivityResult(jint requestCode, jint resultCode, jobject data) override
    {
        if (requestCode != kRequestCode)
            return false;

        QList<QUrl> urls;
        // Activity.RESULT_OK
        if (resultCode == -1 && data) {
            const QJniObject intent(data);
            // Multi-select arrives as a ClipData; a single tap sets the data URI instead.
            const QJniObject clip =
                intent.callObjectMethod("getClipData", "()Landroid/content/ClipData;");
            if (clip.isValid()) {
                const jint count = clip.callMethod<jint>("getItemCount", "()I");
                for (jint i = 0; i < count; ++i) {
                    const QJniObject item = clip.callObjectMethod(
                        "getItemAt", "(I)Landroid/content/ClipData$Item;", i);
                    if (!item.isValid())
                        continue;
                    const QJniObject uri =
                        item.callObjectMethod("getUri", "()Landroid/net/Uri;");
                    if (uri.isValid())
                        urls.append(QUrl(uri.toString()));
                }
            } else {
                const QJniObject uri = intent.callObjectMethod("getData", "()Landroid/net/Uri;");
                if (uri.isValid())
                    urls.append(QUrl(uri.toString()));
            }
            QJniEnvironment().checkAndClearExceptions();
        }

        // Called on the Android UI thread; the import it starts is GUI-thread only.
        FileDialogs *owner = m_owner;
        QMetaObject::invokeMethod(
            owner, [owner, urls] { emit owner->visualMediaPicked(urls); }, Qt::QueuedConnection);
        return true;
    }

private:
    FileDialogs *const m_owner;
};
#endif

FileDialogs::FileDialogs(QObject *parent) : QObject(parent)
{
#ifdef Q_OS_ANDROID
    // AppController builds throwaway FileDialogs temporaries just to call shareFile(); only the
    // process-wide instance main.cpp exposes to QML is worth wiring up, and a listener registered
    // by a temporary would be called back on an object that no longer exists.
    if (!g_newIntentOwner) {
        g_newIntentOwner = this;
        m_newIntentBridge = new NewIntentBridge(this);
        QtAndroidPrivate::registerNewIntentListener(m_newIntentBridge);
        m_pickBridge = new PickResultBridge(this);
        QtAndroidPrivate::registerActivityResultListener(m_pickBridge);
    }
#endif
}

FileDialogs::~FileDialogs()
{
#ifdef Q_OS_ANDROID
    if (g_newIntentOwner == this) {
        QtAndroidPrivate::unregisterNewIntentListener(m_newIntentBridge);
        delete m_newIntentBridge;
        QtAndroidPrivate::unregisterActivityResultListener(m_pickBridge);
        delete m_pickBridge;
        g_newIntentOwner = nullptr;
    }
#endif
}

namespace {

// Defined only where shouldForceNonNativeDialog() below actually calls it — every other platform
// returns false from a branch that never reaches here, and an unconditional definition is an
// unused static function (and a -Wunused-function warning) on all of them. Q_OS_ANDROID has to be
// excluded explicitly: Qt defines Q_OS_LINUX there too.
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
// A sandboxed Linux build has no broad filesystem permission (see the Flatpak manifest's
// finish-args) and depends on the native dialog to hand it access to whatever the user picks
// through xdg-desktop-portal.
bool sandboxed()
{
    return qEnvironmentVariableIsSet("FLATPAK_ID") || QFile::exists(QStringLiteral("/.flatpak-info"))
        || qEnvironmentVariableIsSet("SNAP");
}
#endif

// Whether Qt's own in-app dialog should replace the platform's native one. Only unsandboxed
// desktop Linux gets this: Android has no filesystem access outside its sandbox except through
// the SAF picker the native dialog wraps — Qt's own dialog cannot reach a user's content:// URIs
// at all, so it must stay native there regardless of `sandboxed()`. A sandboxed Linux build
// (Flatpak/Snap) needs the native dialog for the same reason — see `sandboxed()` above. Only an
// unsandboxed Linux build has no such dependency, and forcing Qt's own dialog there avoids a real
// failure mode: the native GTK dialog can open without ever mapping a visible, focused window
// under some Wayland/GNOME setups, leaving the app blocked inside gtk_dialog_run() with nothing
// on screen for the user to interact with.
bool shouldForceNonNativeDialog()
{
#if defined(Q_OS_ANDROID)
    return false;
#elif defined(Q_OS_LINUX)
    return !sandboxed();
#else
    return false;
#endif
}

// The window a dialog should hang off. QFileDialog is a QWidget and this app's window is a
// QQuickWindow, so QDialog's usual route finds nothing: QApplication::activeWindow() only ever
// looks at widget windows and is always null here. Qt then centres the dialog on the primary
// screen with no transient-parent relationship at all, which was invisible while the platform
// helper was doing the work and is not any more — see parentDialogToApp() below.
QWindow *appWindow()
{
    // The focused window is the one the user just acted in, which is the right parent even when
    // that is a secondary window (the multicam or curve editor) rather than the main one.
    if (QWindow *focus = QGuiApplication::focusWindow())
        return focus;
    for (QWindow *window : QGuiApplication::topLevelWindows()) {
        // Qt::Window skips tooltips, menus and other popups, which are top-level windows too and
        // would make a hopeless parent for something modal.
        if (window->isVisible() && window->type() == Qt::Window)
            return window;
    }
    return nullptr;
}

// Ties `dialog` to the app's window before it is shown. Two things go wrong without this, both
// only on the non-native path this file now takes on desktop Linux:
//
//   - With no transient parent, a Wayland compositor has no parent/child relationship to enforce,
//     so the main window can be raised over a dialog that is blocking inside exec(). The result is
//     an app that ignores all input with no dialog in sight — the same symptom, and the same
//     support report, that forcing the non-native dialog was meant to put an end to.
//   - QDialog::adjustPosition() centres on the primary screen when it cannot find a parent, so on
//     a two-monitor desk the picker opens on the wrong one.
//
// Must run before exec(): winId() realises the handle early so setTransientParent() is in place by
// the time the window is mapped, which is the only moment the compositor reads it.
//
// Deliberately confined to the non-native path. Realising the handle on a dialog that is about to
// be replaced by a platform helper means creating a native window the helper never shows, and how
// each platform's helper reacts to a widget that already has one is not something this can be
// tested against from a Linux desk. The native dialogs are parented by their platform today and
// nobody has reported otherwise, so there is nothing here to fix on those paths and no reason to
// take the risk on them.
void parentDialogToApp(QFileDialog &dialog)
{
    if (!shouldForceNonNativeDialog())
        return;
    QWindow *parent = appWindow();
    if (!parent)
        return;
    dialog.winId();
    QWindow *handle = dialog.windowHandle();
    if (!handle)
        return;
    handle->setTransientParent(parent);
    handle->setScreen(parent->screen());
}

// Where the last import came from. Qt's own dialog has no memory across launches — it falls back
// to QFileDialogPrivate::lastVisitedDir(), a process-global static that starts empty and resolves
// to the working directory, so launching Drift from a terminal used to open the picker on whatever
// directory that shell happened to be in. The portal used to remember this per app on desktop
// Linux; now that it is out of the loop on the non-native path, this has to be kept here instead.
const char *kLastImportFolderKey = "import/lastFolder";

QString lastImportFolder()
{
    const QString folder = QSettings().value(QLatin1String(kLastImportFolderKey)).toString();
    if (folder.isEmpty() || !QDir(folder).exists())
        return {};
    return folder;
}

void rememberImportFolder(const QList<QUrl> &urls)
{
    // Only a real path is worth storing: an Android content:// URI has no directory to reopen, and
    // its grant does not survive the process anyway.
    if (urls.isEmpty() || !urls.first().isLocalFile())
        return;
    const QString folder = QFileInfo(urls.first().toLocalFile()).absolutePath();
    if (folder.isEmpty() || !QDir(folder).exists())
        return;
    QSettings().setValue(QLatin1String(kLastImportFolderKey), folder);
}

// Seeds an open dialog with that folder. Skipped on Android, where the SAF picker owns its own
// navigation and a filesystem path means nothing to it.
void applyLastImportFolder(QFileDialog &dialog)
{
#ifndef Q_OS_ANDROID
    const QString folder = lastImportFolder();
    if (!folder.isEmpty())
        dialog.setDirectory(folder);
#else
    Q_UNUSED(dialog);
#endif
}

void applyFilters(QFileDialog &dialog, const QStringList &nameFilters,
                  const QStringList &mimeTypeFilters)
{
    if (!mimeTypeFilters.isEmpty()) {
        QMimeDatabase db;
        bool allKnown = true;
        for (const QString &mime : mimeTypeFilters) {
            if (db.mimeTypeForName(mime).isValid())
                continue;
            allKnown = false;
            break;
        }
        if (allKnown) {
            dialog.setMimeTypeFilters(mimeTypeFilters);
            return;
        }
    }
    if (!nameFilters.isEmpty())
        dialog.setNameFilters(nameFilters);
}

void applyOpenFilters(QFileDialog &dialog, const QStringList &nameFilters,
                      const QStringList &mimeTypeFilters)
{
#ifdef Q_OS_ANDROID
    Q_UNUSED(nameFilters);
    Q_UNUSED(mimeTypeFilters);
    // Android's SAF picker understands MIME types, not "*.mp4" name filters. Broad media MIME
    // types keep the system document UI usable; no READ_MEDIA_* / storage permission is needed
    // because access is granted per URI when the user picks a file.
    dialog.setMimeTypeFilters({
        QStringLiteral("video/*"),
        QStringLiteral("audio/*"),
        QStringLiteral("image/*"),
        // Subtitle import. Providers disagree on what an .srt is — SubRip proper, WebVTT's type,
        // or just text — and a project file has no registered type at all, hence the catch-all.
        QStringLiteral("application/x-subrip"),
        QStringLiteral("text/vtt"),
        QStringLiteral("text/plain"),
        QStringLiteral("*/*"),
    });
#else
    applyFilters(dialog, nameFilters, mimeTypeFilters);
#endif
}

} // namespace

QUrl FileDialogs::openFile(const QString &title, const QStringList &nameFilters,
                           const QStringList &mimeTypeFilters) const
{
    QFileDialog dialog;
    dialog.setOption(QFileDialog::DontUseNativeDialog, shouldForceNonNativeDialog());
    dialog.setWindowTitle(title);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::ExistingFile);
    applyOpenFilters(dialog, nameFilters, mimeTypeFilters);
    applyLastImportFolder(dialog);
    parentDialogToApp(dialog);
    if (dialog.exec() != QDialog::Accepted)
        return {};
    const QList<QUrl> urls = dialog.selectedUrls();
    rememberImportFolder(urls);
    return urls.isEmpty() ? QUrl() : urls.first();
}

QList<QUrl> FileDialogs::openFiles(const QString &title, const QStringList &nameFilters) const
{
    QFileDialog dialog;
    dialog.setOption(QFileDialog::DontUseNativeDialog, shouldForceNonNativeDialog());
    dialog.setWindowTitle(title);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::ExistingFiles);
    applyOpenFilters(dialog, nameFilters, {});
    applyLastImportFolder(dialog);
    parentDialogToApp(dialog);
    if (dialog.exec() != QDialog::Accepted)
        return {};
    const QList<QUrl> urls = dialog.selectedUrls();
    rememberImportFolder(urls);
    return urls;
}

bool FileDialogs::supportsDirectoryPicker() const
{
#ifdef Q_OS_ANDROID
    return false;
#else
    return true;
#endif
}

QUrl FileDialogs::openDirectory(const QString &title, const QUrl &startDir) const
{
#ifdef Q_OS_ANDROID
    Q_UNUSED(title);
    Q_UNUSED(startDir);
    return {};
#else
    QFileDialog dialog;
    dialog.setOption(QFileDialog::DontUseNativeDialog, shouldForceNonNativeDialog());
    dialog.setWindowTitle(title);
    if (startDir.isValid() && !startDir.isEmpty())
        dialog.setDirectoryUrl(startDir);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::Directory);
    dialog.setOption(QFileDialog::ShowDirsOnly, true);
    parentDialogToApp(dialog);
    if (dialog.exec() != QDialog::Accepted)
        return {};
    const QList<QUrl> urls = dialog.selectedUrls();
    return urls.isEmpty() ? QUrl() : urls.first();
#endif
}

QUrl FileDialogs::saveFile(const QString &title, const QStringList &nameFilters,
                           const QString &suggestedName, const QString &suffix,
                           const QString &initialDirectory, const QStringList &mimeTypeFilters) const
{
    QFileDialog dialog;
    dialog.setOption(QFileDialog::DontUseNativeDialog, shouldForceNonNativeDialog());
    dialog.setWindowTitle(title);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
#ifdef Q_OS_ANDROID
    // On a name clash the storage provider only keeps the extension ahead of " (1)" when the
    // requested MIME type matches that extension in Android's own MimeTypeMap. Qt sends "*/*"
    // whenever more than one filter is set, and its MIME names do not always match Android's,
    // so ask Android for the one type and clear the name filters Qt would add on top of it.
    // An unmapped extension gets octet-stream, which the provider treats as matching too.
    if (!suffix.isEmpty()) {
        const QJniObject mimeMap = QJniObject::callStaticObjectMethod(
            "android/webkit/MimeTypeMap", "getSingleton", "()Landroid/webkit/MimeTypeMap;");
        const QJniObject mime = mimeMap.callObjectMethod(
            "getMimeTypeFromExtension", "(Ljava/lang/String;)Ljava/lang/String;",
            QJniObject::fromString(suffix.toLower()).object<jstring>());
        dialog.setMimeTypeFilters({mime.isValid() ? mime.toString()
                                                  : QStringLiteral("application/octet-stream")});
        dialog.setNameFilters({});
    } else {
        applyFilters(dialog, nameFilters, mimeTypeFilters);
    }
#else
    applyFilters(dialog, nameFilters, mimeTypeFilters);
#endif

    if (!initialDirectory.isEmpty() && QDir(initialDirectory).exists())
        dialog.setDirectory(initialDirectory);

    // The extension is put in the suggested name instead of QFileDialog::setDefaultSuffix: a file
    // exported through the documents portal must not be renamed afterwards, and appending the
    // suffix to what the portal returned writes to a path the portal never registered — the data
    // lands next to the picked file as a hidden entry instead of at the chosen name.
    QString name = suggestedName.trimmed();
    name.replace(QLatin1Char('/'), QLatin1Char('_'));
    name.replace(QLatin1Char('\\'), QLatin1Char('_'));
    if (name.isEmpty())
        name = tr("Untitled");
    if (!suffix.isEmpty())
        name += QLatin1Char('.') + suffix;
    dialog.selectFile(name);

    parentDialogToApp(dialog);
    if (dialog.exec() != QDialog::Accepted)
        return {};
    const QList<QUrl> urls = dialog.selectedUrls();
    return urls.isEmpty() ? QUrl() : urls.first();
}

bool FileDialogs::pickVisualMedia(bool allowMultiple)
{
#ifdef Q_OS_ANDROID
    // MediaStore.ACTION_PICK_IMAGES is API 33. Below that there is no photo picker to fall back
    // *to* — the caller falls back to SAF instead, which is why this reports rather than throws.
    if (QtAndroidPrivate::androidSdkVersion() < 33)
        return false;

    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (!activity.isValid())
        return false;

    QJniObject intent("android/content/Intent", "(Ljava/lang/String;)V",
                      QJniObject::fromString(QStringLiteral("android.provider.action.PICK_IMAGES"))
                          .object<jstring>());
    if (!intent.isValid())
        return false;

    if (allowMultiple) {
        // Without a max the picker is single-select. 100 is the documented ceiling below
        // MediaStore.getPickImagesMaxLimit(), which is itself only readable on API 33+.
        intent.callObjectMethod("putExtra", "(Ljava/lang/String;I)Landroid/content/Intent;",
                                QJniObject::fromString(
                                    QStringLiteral("android.provider.extra.PICK_IMAGES_MAX"))
                                    .object<jstring>(),
                                jint(100));
    }

    activity.callMethod<void>("startActivityForResult", "(Landroid/content/Intent;I)V",
                              intent.object(), PickResultBridge::kRequestCode);
    // A device that reports API 33 but has no picker activity throws ActivityNotFoundException
    // here; clearing it and reporting false is what sends the caller to SAF.
    return !QJniEnvironment().checkAndClearExceptions();
#else
    Q_UNUSED(allowMultiple);
    return false;
#endif
}

bool FileDialogs::viewFile(const QUrl &url, const QString &mimeType) const
{
#ifdef Q_OS_ANDROID
    if (url.scheme().compare(QLatin1String("content"), Qt::CaseInsensitive) != 0)
        return false;

    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (!activity.isValid())
        return false;

    QJniObject uri = QJniObject::callStaticObjectMethod(
        "android/net/Uri", "parse", "(Ljava/lang/String;)Landroid/net/Uri;",
        QJniObject::fromString(url.toString(QUrl::FullyEncoded)).object<jstring>());
    if (!uri.isValid())
        return false;

    QString type = mimeType;
    if (type.isEmpty()) {
        QJniObject resolver =
            activity.callObjectMethod("getContentResolver", "()Landroid/content/ContentResolver;");
        if (resolver.isValid()) {
            type = resolver
                       .callObjectMethod("getType", "(Landroid/net/Uri;)Ljava/lang/String;",
                                         uri.object())
                       .toString();
        }
        if (type.isEmpty())
            type = QStringLiteral("video/*");
    }

    QJniObject intent("android/content/Intent", "(Ljava/lang/String;)V",
                      QJniObject::fromString(QStringLiteral("android.intent.action.VIEW"))
                          .object<jstring>());
    intent.callObjectMethod("setDataAndType",
                            "(Landroid/net/Uri;Ljava/lang/String;)Landroid/content/Intent;",
                            uri.object(), QJniObject::fromString(type).object<jstring>());
    // FLAG_GRANT_READ_URI_PERMISSION, or the player gets a URI it cannot open.
    intent.callObjectMethod("addFlags", "(I)Landroid/content/Intent;", jint(0x00000001));

    QJniObject chooser = QJniObject::callStaticObjectMethod(
        "android/content/Intent", "createChooser",
        "(Landroid/content/Intent;Ljava/lang/CharSequence;)Landroid/content/Intent;",
        intent.object(), QJniObject::fromString(tr("Play")).object());
    if (!chooser.isValid())
        return false;

    activity.callMethod<void>("startActivity", "(Landroid/content/Intent;)V", chooser.object());
    return !QJniEnvironment().checkAndClearExceptions();
#else
    Q_UNUSED(url);
    Q_UNUSED(mimeType);
    return false;
#endif
}

bool FileDialogs::shareFile(const QUrl &url, const QString &mimeType) const
{
#ifdef Q_OS_ANDROID
    if (url.scheme().compare(QLatin1String("content"), Qt::CaseInsensitive) != 0)
        return false;

    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (!activity.isValid())
        return false;

    QJniObject uri = QJniObject::callStaticObjectMethod(
        "android/net/Uri", "parse", "(Ljava/lang/String;)Landroid/net/Uri;",
        QJniObject::fromString(url.toString(QUrl::FullyEncoded)).object<jstring>());
    if (!uri.isValid())
        return false;

    QString type = mimeType;
    if (type.isEmpty()) {
        QJniObject resolver =
            activity.callObjectMethod("getContentResolver", "()Landroid/content/ContentResolver;");
        if (resolver.isValid()) {
            type = resolver
                       .callObjectMethod("getType", "(Landroid/net/Uri;)Ljava/lang/String;",
                                         uri.object())
                       .toString();
        }
        if (type.isEmpty())
            type = QStringLiteral("*/*");
    }

    QJniObject intent("android/content/Intent", "(Ljava/lang/String;)V",
                      QJniObject::fromString(QStringLiteral("android.intent.action.SEND"))
                          .object<jstring>());
    intent.callObjectMethod("setType", "(Ljava/lang/String;)Landroid/content/Intent;",
                            QJniObject::fromString(type).object<jstring>());
    intent.callObjectMethod(
        "putExtra", "(Ljava/lang/String;Landroid/os/Parcelable;)Landroid/content/Intent;",
        QJniObject::fromString(QStringLiteral("android.intent.extra.STREAM")).object<jstring>(),
        uri.object());
    // Intent.FLAG_GRANT_READ_URI_PERMISSION — without it the receiving app gets a URI it cannot open.
    intent.callObjectMethod("addFlags", "(I)Landroid/content/Intent;", jint(0x00000001));

    QJniObject chooser = QJniObject::callStaticObjectMethod(
        "android/content/Intent", "createChooser",
        "(Landroid/content/Intent;Ljava/lang/CharSequence;)Landroid/content/Intent;",
        intent.object(), QJniObject::fromString(tr("Share")).object());
    if (!chooser.isValid())
        return false;

    activity.callMethod<void>("startActivity", "(Landroid/content/Intent;)V", chooser.object());
    return !QJniEnvironment().checkAndClearExceptions();
#else
    Q_UNUSED(url);
    Q_UNUSED(mimeType);
    return false;
#endif
}

QVariantMap FileDialogs::takeLaunchIntent()
{
#ifdef Q_OS_ANDROID
    // A warm-start intent outranks the launch one: getIntent() still holds whatever the process
    // was started with, which by then is stale.
    if (!m_pendingLaunchUrl.isEmpty()) {
        const QUrl pending = m_pendingLaunchUrl;
        m_pendingLaunchUrl.clear();
        return QVariantMap{{QStringLiteral("kind"), QStringLiteral("view")},
                           {QStringLiteral("urls"), QVariantList{pending}},
                           {QStringLiteral("mimeType"), QString()}};
    }

    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (!activity.isValid())
        return {};

    const QJniObject intent =
        activity.callObjectMethod("getIntent", "()Landroid/content/Intent;");
    return describeIntent(intent);
#else
    return {};
#endif
}

QUrl FileDialogs::takeLaunchUrl()
{
    // Kept so the desktop path and anything still asking only about a launched .drift do not have
    // to learn the intent map. Consuming form, as before.
    const QVariantMap intent = takeLaunchIntent();
    if (intent.value(QStringLiteral("kind")).toString() != QLatin1String("view"))
        return {};
    const QVariantList urls = intent.value(QStringLiteral("urls")).toList();
    return urls.isEmpty() ? QUrl() : urls.first().toUrl();
}
