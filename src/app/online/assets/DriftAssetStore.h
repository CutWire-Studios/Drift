#pragma once

#include <QHash>
#include <QObject>
#include <QPair>
#include <QPointer>
#include <QUrl>
#include <QVariantMap>

#include <functional>
#include <memory>

class DownloadCenter;

namespace drift::market {
class FileFetch;
class MarketTransport;
struct InstallJob;
} // namespace drift::market

// Installs Drift Assets (CutWire's own Lottie animations, face props and 3D objects) through
// POST /marketplace/installs. The listing comes from MarketSourceBrowser; this only knows
// items it is handed. Files are sha256-checked and installed into app data, then used straight
// from there. Reported to the DownloadCenter as kind "drift-asset".
//
// Lottie and 3D objects go into the media bin; face props are installed through
// the prop importer so the Face Props picker lists them.
class DriftAssetStore : public QObject
{
    Q_OBJECT
    // Bumped whenever any asset's install state changes; QML bindings on state() read it.
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

public:
    using Importer = std::function<QStringList(const QStringList &)>;
    // Installs a staged face-prop folder and returns the ids it added.
    using PropImporter = std::function<QStringList(const QString &dir)>;

    explicit DriftAssetStore(QObject *parent = nullptr,
                             drift::market::MarketTransport *transport = nullptr,
                             DownloadCenter *center = nullptr);

    void setImporter(Importer importer) { m_importer = std::move(importer); }
    void setPropImporter(PropImporter importer) { m_propImporter = std::move(importer); }
    // Whether a face prop with this id is already in the prop library.
    void setPropLookup(std::function<bool(const QString &id)> lookup) { m_propLookup = std::move(lookup); }

    int revision() const { return m_revision; }

    static QString installRoot();
    static QString jobId(const QString &key);

    // Install directory key: the item id for its default (first) design, otherwise
    // "<id>--<variant>".
    Q_INVOKABLE QString installKey(const QVariantMap &item, const QString &variantId = QString()) const;
    // none | installing | installed | failed
    Q_INVOKABLE QString state(const QString &key) const;
    Q_INVOKABLE QString itemState(const QVariantMap &item, const QString &variantId = QString()) const;
    // Absolute path of the installed main file (.json / .glb), empty when not installed.
    Q_INVOKABLE QString localPath(const QString &key) const;
    // Lottie and objects are imported into the media bin and ready(key, binAssetId) fires; face
    // props are added to the prop library and ready(key, "") fires. Safe to call again while an
    // install is running.
    Q_INVOKABLE void install(const QVariantMap &item, const QString &variantId = QString());

signals:
    void revisionChanged();
    void stateChanged(const QString &key);
    void ready(const QString &key, const QString &binAssetId);
    void failed(const QString &key, const QString &message);

private:
    struct Install;

    QString kindDir(const QString &key) const;
    void begin(const std::shared_ptr<Install> &install);
    void fetchNext(const std::shared_ptr<Install> &install);
    void finish(const std::shared_ptr<Install> &install);
    void fail(const std::shared_ptr<Install> &install, const QString &code, const QString &message,
              bool retryable);
    void cancel(const QString &key);
    void publishStatus(const std::shared_ptr<Install> &install, const QString &status,
                       const QString &phase, double progress, qint64 bytes = 0, qint64 total = 0,
                       const QVariantMap &result = {}, const QString &code = {},
                       const QString &error = {}, bool retryable = false);
    void setState(const QString &key, const QString &state);

    Importer m_importer;
    PropImporter m_propImporter;
    std::function<bool(const QString &)> m_propLookup;
    drift::market::MarketTransport *m_transport = nullptr;
    DownloadCenter *m_center = nullptr;
    QHash<QString, QString> m_states;
    QHash<QString, std::shared_ptr<Install>> m_installs;
    QHash<QString, QPair<QVariantMap, QString>> m_retry;
    int m_revision = 0;
};
