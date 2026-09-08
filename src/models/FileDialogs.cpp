#include "FileDialogs.h"

#include <QDir>
#include <QFileDialog>
#include <QMimeDatabase>
#include <QMimeType>

#ifdef Q_OS_ANDROID
#include <QJniEnvironment>
#include <QJniObject>
#include <QtCore/private/qjnihelpers_p.h>
#include <QtCore/qcoreapplication_platform.h>
#endif

#ifdef Q_OS_ANDROID
namespace {
FileDialogs *g_newIntentOwner = nullptr;
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
        const QJniObject in(intent);
        const QString action = in.callObjectMethod("getAction", "()Ljava/lang/String;").toString();
        if (action != QLatin1String("android.intent.action.VIEW"))
            return false;

        const QJniObject data = in.callObjectMethod("getData", "()Landroid/net/Uri;");
        if (!data.isValid())
            return false;

        // Called on the Android UI thread; everything downstream of the signal (loadProject and
        // the QML it drives) is GUI-thread only.
        const QUrl url(data.toString());
        FileDialogs *owner = m_owner;
        QMetaObject::invokeMethod(
            owner,
            [owner, url] {
                owner->m_pendingLaunchUrl = url;
                emit owner->launchUrlReceived(url);
            },
            Qt::QueuedConnection);
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
    }
#endif
}

FileDialogs::~FileDialogs()
{
#ifdef Q_OS_ANDROID
    if (g_newIntentOwner == this) {
        QtAndroidPrivate::unregisterNewIntentListener(m_newIntentBridge);
        delete m_newIntentBridge;
        g_newIntentOwner = nullptr;
    }
#endif
}

namespace {

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
    dialog.setWindowTitle(title);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::ExistingFile);
    applyOpenFilters(dialog, nameFilters, mimeTypeFilters);
    if (dialog.exec() != QDialog::Accepted)
        return {};
    const QList<QUrl> urls = dialog.selectedUrls();
    return urls.isEmpty() ? QUrl() : urls.first();
}

QList<QUrl> FileDialogs::openFiles(const QString &title, const QStringList &nameFilters) const
{
    QFileDialog dialog;
    dialog.setWindowTitle(title);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::ExistingFiles);
    applyOpenFilters(dialog, nameFilters, {});
    if (dialog.exec() != QDialog::Accepted)
        return {};
    return dialog.selectedUrls();
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
    dialog.setWindowTitle(title);
    if (startDir.isValid() && !startDir.isEmpty())
        dialog.setDirectoryUrl(startDir);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::Directory);
    dialog.setOption(QFileDialog::ShowDirsOnly, true);
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
    dialog.setWindowTitle(title);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    applyFilters(dialog, nameFilters, mimeTypeFilters);

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

    if (dialog.exec() != QDialog::Accepted)
        return {};
    const QList<QUrl> urls = dialog.selectedUrls();
    return urls.isEmpty() ? QUrl() : urls.first();
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

QUrl FileDialogs::takeLaunchUrl()
{
#ifdef Q_OS_ANDROID
    // A warm-start intent outranks the launch one: getIntent() still holds whatever the process
    // was started with, which by then is stale.
    if (!m_pendingLaunchUrl.isEmpty()) {
        const QUrl pending = m_pendingLaunchUrl;
        m_pendingLaunchUrl.clear();
        return pending;
    }

    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (!activity.isValid())
        return {};

    QJniObject intent = activity.callObjectMethod("getIntent", "()Landroid/content/Intent;");
    if (!intent.isValid())
        return {};

    const QString action =
        intent.callObjectMethod("getAction", "()Ljava/lang/String;").toString();
    if (action != QLatin1String("android.intent.action.VIEW"))
        return {};

    QJniObject data = intent.callObjectMethod("getData", "()Landroid/net/Uri;");
    if (!data.isValid())
        return {};

    intent.callObjectMethod("setData", "(Landroid/net/Uri;)Landroid/content/Intent;",
                            static_cast<jobject>(nullptr));
    QJniEnvironment().checkAndClearExceptions();
    return QUrl(data.toString());
#else
    return {};
#endif
}
