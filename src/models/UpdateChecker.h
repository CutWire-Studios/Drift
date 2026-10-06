#pragma once

#include <QObject>
#include <QString>
#include <QElapsedTimer>
#include <QStringList>
#include <QVariantMap>

#include <functional>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;
class QCryptographicHash;

// Once a day, reads the newest version from the TXT record on drift-version.cutwire.org and
// raises a badge in the header when it is newer than this build.
//
// The Windows installer, macOS disk image and AppImage builds download their own replacement
// (Drift-Setup-*-x64.exe, Drift-*-arm64.dmg, Drift-*-x86_64.AppImage), but only once the user
// presses Update, and never quit on their own: the user either restarts from the prompt or lets
// the install run when they next close Drift. Either way a helper waits until this process is gone
// before running the installer, copying the app into /Applications or replacing the AppImage.
// Other packages have nothing in common to install into, so their button opens the release page.
//
// Builds that a package manager owns are configured with an empty DRIFT_UPDATE_FEED_URL and get
// none of this — see supported(). Nagging a Flatpak or pacman user about a version their package
// manager will fetch on its own schedule is noise they cannot act on.
class UpdateChecker : public QObject
{
    Q_OBJECT
    // False when the build was configured with an empty feed URL. The badge and the settings row
    // are both hidden then, rather than showing a control that can never do anything.
    Q_PROPERTY(bool supported READ supported CONSTANT)
    // True for the Windows installer, macOS disk-image and AppImage builds, which can apply an
    // update themselves. The dialog uses this to open on its own the first time a version is found.
    Q_PROPERTY(bool installSupported READ installSupported CONSTANT)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool checking READ checking NOTIFY activityChanged)
    Q_PROPERTY(bool downloading READ downloading NOTIFY activityChanged)
    Q_PROPERTY(bool preparing READ preparing NOTIFY activityChanged)
    Q_PROPERTY(bool readyToInstall READ readyToInstall NOTIFY activityChanged)
    // The staged update will be installed when Drift is closed.
    Q_PROPERTY(bool installScheduled READ installScheduled NOTIFY activityChanged)
    // 0..1 while downloading. Stays at 1 while the disk image is being unpacked.
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY resultChanged)
    // A direct installer with a known SHA-256 was found for this platform. False falls back to
    // the release page.
    Q_PROPERTY(bool canInstall READ canInstall NOTIFY resultChanged)
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY resultChanged)
    // The release's Markdown body, rendered as Markdown by the dialog.
    Q_PROPERTY(QString releaseNotes READ releaseNotes NOTIFY resultChanged)
    Q_PROPERTY(QString releaseUrl READ releaseUrl NOTIFY resultChanged)
    // Result of the last check in one line, for the settings row. Empty until something is worth
    // saying; a failed background check says nothing at all.
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    // Download or install failure. Empty while a check or transfer is healthy.
    Q_PROPERTY(QString error READ error NOTIFY statusChanged)
    // The update download as a row for the Downloads window, in the same shape as a marketplace
    // job. Empty when there is nothing to show.
    Q_PROPERTY(QVariantMap downloadJob READ downloadJob NOTIFY downloadJobChanged)

public:
    explicit UpdateChecker(QObject *parent = nullptr);
    ~UpdateChecker() override;

    bool supported() const;
    bool installSupported() const;
    bool enabled() const;
    void setEnabled(bool enabled);
    bool checking() const;
    bool downloading() const;
    bool preparing() const;
    bool readyToInstall() const;
    bool installScheduled() const;
    qreal progress() const;
    bool updateAvailable() const;
    bool canInstall() const;
    QString currentVersion() const;
    QString latestVersion() const;
    QString releaseNotes() const;
    QString releaseUrl() const;
    QString status() const;
    QString error() const;
    QVariantMap downloadJob() const;

    // User pressed "Check now": ignores the once-a-day throttle and the skipped version, and
    // reports failures and "you are up to date" through status().
    Q_INVOKABLE void checkNow();

    // Stops this version being advertised again. The next release clears it by being newer.
    Q_INVOKABLE void skipVersion();

    Q_INVOKABLE void openDownloadPage();

    // Downloads the installer for this platform in the background. With installOnQuit it installs
    // when Drift is next closed; without, installReady() asks what to do once it is staged.
    Q_INVOKABLE void downloadAndInstall(bool installOnQuit);
    Q_INVOKABLE void retryDownload();
    Q_INVOKABLE void cancelDownload();
    // Forgets a failed or cancelled download so the Downloads window can drop its row.
    Q_INVOKABLE void clearDownloadState();

    // Quits so a download that already finished can install, then starts the new version.
    // The unsaved-changes prompt still runs.
    Q_INVOKABLE void requestQuit();

    // Installs the staged update when Drift is next closed, without starting it again.
    Q_INVOKABLE void scheduleInstallOnQuit();

    // Remember that this version's dialog has been shown, so the next launch does not open it again.
    Q_INVOKABLE void markAnnounced();

signals:
    void enabledChanged();
    void activityChanged();
    void progressChanged();
    void resultChanged();
    void statusChanged();
    void downloadJobChanged();
    void downloadStarted();
    // A newer release is available and this build can install it, and the dialog has not been
    // shown for this version yet.
    void updateReady();
    // The installer is staged and the user has not said when to install it.
    void installReady();
    // The user chose to restart. QML closes the window, which is what runs the unsaved-changes prompt.
    void quitRequested();

private:
    void scheduleStartup();
    void check(bool manual);
    void fetchRelease(const QString &version, bool manual);
    void publishFromVersion(const QString &version, bool manual);
    bool applyRelease(const QByteArray &json, bool manual, const QString &expectedVersion);
    void runStageStep(const QString &program, const QStringList &arguments,
                      const std::function<void(int, const QString &)> &done);
    void restorePending();
    void storePending();
    void clearPending();
    void publishRelease(const QString &version, const QString &notes, const QString &htmlUrl,
                        const QString &assetName, const QString &assetUrl, const QString &sha256,
                        qint64 assetSize);
    void clearRelease();
    void finishCheck(const QString &version, bool manual);
    void announceIfNeeded();
    void setChecking(bool checking);
    void setDownloading(bool downloading);
    void setPreparing(bool preparing);
    void setReadyToInstall(bool ready);
    void setProgress(qreal progress);
    void setStatus(const QString &status);
    void setError(const QString &error);
    void beginDownload();
    void finishDownload();
    void stagePackage();
    void stageMacApp();
    void stageAppImage();
    void copyMacApp();
    void detachMacImage(bool thenInstall);
    void failPrepare(const QString &message);
    void finishStage();
    void launchStagedInstall();
    QString updatesDir() const;
    QString macBundleName() const;
    QString appImagePath() const;

    bool m_checking = false;
    bool m_downloading = false;
    bool m_preparing = false;
    bool m_readyToInstall = false;
    bool m_installOnQuit = false;
    bool m_relaunch = false;
    bool m_downloadCancelled = false;
    // "failed" or "cancelled" after a download that did not finish; empty otherwise.
    QString m_downloadOutcome;
    qint64 m_bytesReceived = 0;
    qint64 m_bytesTotal = 0;
    QElapsedTimer m_downloadClock;
    qreal m_progress = 0;
    QString m_latestVersion;
    QString m_releaseNotes;
    QString m_releaseUrl;
    QString m_assetName;
    QString m_assetUrl;
    QString m_assetSha256;
    qint64 m_assetSize = 0;
    QString m_packagePath;
    QString m_stagedPath;
    QString m_mountPoint;
    QString m_skippedVersion;
    QString m_announcedVersion;
    QString m_status;
    QString m_error;
    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_reply = nullptr;
    QFile *m_downloadFile = nullptr;
    QCryptographicHash *m_hasher = nullptr;
    QString m_partialPath;
};
