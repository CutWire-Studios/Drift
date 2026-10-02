#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QUrl>
#include <QPointer>
#include <QVariantList>

#include <memory>

class QNetworkAccessManager;

// Downloads and installs addons. Lives in the app layer, not driftengine, so tools/ and tests/
// keep working with no network stack — they read the registry (drift::addon::AddonRegistry) and
// never write it.
//
// The remote index is the source of truth for what exists and at which version; installed.json is
// the source of truth for what is here. A row's state is derived by comparing the two.
class AddonManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList catalog READ catalog NOTIFY catalogChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool refreshing READ refreshing NOTIFY refreshingChanged)
    // Header attention nudge: offer the core video / transitions / audio packs when they are not
    // yet installed as addons (bundled copies still work; installing unlocks the update channel).
    Q_PROPERTY(bool remindEssential READ remindEssential WRITE setRemindEssential
                   NOTIFY remindEssentialChanged)
    // Header attention nudge: mention installed packs that have a newer version on the store.
    Q_PROPERTY(bool remindUpdates READ remindUpdates WRITE setRemindUpdates
                   NOTIFY remindUpdatesChanged)

public:
    explicit AddonManager(QObject *parent = nullptr);
    ~AddonManager() override;

    // One row per known addon: id, name, description, details, author, license, kind, version,
    // installedVersion, downloadSize, installedSize, items, state.
    // state is one of: available, downloading, installing, installed, update-available, failed,
    // needs-newer-app (minAppVersion is above this build; also sets minAppVersion on the row).
    QVariantList catalog() const;
    QString status() const;
    bool refreshing() const;

    bool remindEssential() const;
    void setRemindEssential(bool remind);
    bool remindUpdates() const;
    void setRemindUpdates(bool remind);

    // Uses the on-disk copy of the index when it is younger than six hours unless forced, so
    // launching offline is quiet rather than an error.
    Q_INVOKABLE void refresh(bool force = false);

    Q_INVOKABLE void install(const QString &id);
    Q_INVOKABLE void cancel(const QString &id);
    Q_INVOKABLE void uninstall(const QString &id);

    // True when an addon providing this kind is installed — drives the empty states in
    // FontPicker, the stickers tab and subtitle generation.
    Q_INVOKABLE bool hasKind(const QString &kind) const;
    Q_INVOKABLE QString firstAddonForKind(const QString &kind) const;

    // Essential packs (effects / transitions / audio) that exist on the store but are not in
    // installed.json. Empty when the index has not been loaded or every essential is present.
    Q_INVOKABLE QVariantList missingEssentialAddons() const;
    // Installed packs with a newer version on the store.
    Q_INVOKABLE QVariantList updatableAddons() const;

    // --- User packages (.driftfx from Drift Forge) ---------------------------------------
    // Copies the picked file (a content:// URI on Android) into the cache and reads its manifest.
    // Returns name, kind ("effects" | "transitions"), version, author and description, or a map
    // with only `error`. Nothing is installed until installUserPackage(), which acts on the file
    // inspected last and answers on userPackageInstalled.
    Q_INVOKABLE QVariantMap inspectUserPackage(const QUrl &url);
    // Sniffs the magic, since a content:// URI from "Open with" rarely carries the file name.
    Q_INVOKABLE bool isUserPackage(const QUrl &url) const;
    Q_INVOKABLE void installUserPackage();

    // --- Acceleration ------------------------------------------------------------------
    // Whether an ONNX Runtime is installed at all. Every AI feature needs one, so this gates
    // their empty states alongside the model each of them wants.
    Q_INVOKABLE bool runtimeAvailable() const;

    // One row per selectable option: value, label, available. Always contains "auto" and "cpu".
    Q_INVOKABLE QVariantList accelerationOptions() const;
    Q_INVOKABLE QString acceleration() const;
    Q_INVOKABLE void setAcceleration(const QString &variant);

    // True once a runtime addon has been installed or removed in a session that had already
    // loaded one. The library is loaded once per process, so the change only takes effect on the
    // next launch and the dialog has to say so.
    Q_INVOKABLE bool runtimeRestartRequired() const;

signals:
    void catalogChanged();
    void statusChanged();
    void refreshingChanged();
    void remindEssentialChanged();
    void remindUpdatesChanged();
    // Emitted continuously during download and extraction; the dialog binds this rather than
    // rebuilding the whole catalogue list several times a second.
    void progressChanged(const QString &id, double fraction, const QString &phase);
    // A kind's content changed on disk and its catalog has already been reloaded.
    void kindChanged(const QString &kind);
    // Per-transfer outcome. `status` is a mixed-purpose display string — it carries
    // store errors, per-transfer failures and the empty success state alike — so
    // these exist to let callers react to an outcome without reading its prose.
    void transferFailed(const QString &id, const QString &reason);
    void transferSucceeded(const QString &id);
    // Outcome of installUserPackage(); `error` is empty on success.
    void userPackageInstalled(const QString &name, const QString &error);

private:
    struct Transfer;

    void setStatus(const QString &status);
    void setRefreshing(bool refreshing);
    void applyIndex(const QByteArray &json, bool fromCache);
    void startDownload(const QString &id);
    void finishDownload(const QString &id);
    void beginExtract(const QString &id, const QString &packagePath);
    void failTransfer(const QString &id, const QString &message);
    void reloadForKinds(const QStringList &kinds);
    void sweepDownloadCache();

    QString m_status;
    bool m_refreshing = false;
    QList<QJsonObject> m_remote;
    QHash<QString, std::shared_ptr<Transfer>> m_transfers;
    QHash<QString, QString> m_failures;
    // Download links expire after an hour, so an install started from a stale cached index gets
    // rejected. Those ids are re-driven once a fresh index lands; the set stops a rejected-twice
    // addon looping forever.
    QStringList m_awaitingFreshIndex;
    QSet<QString> m_retried;
    bool m_runtimeRestartRequired = false;
    QString m_userPackagePath;
    QNetworkAccessManager *m_network = nullptr;
};
