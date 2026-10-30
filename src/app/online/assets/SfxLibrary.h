#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <memory>

class DownloadCenter;

namespace drift::market {
class FileFetch;
class MarketTransport;
} // namespace drift::market

// Installs CutWire's sound effects through POST /marketplace/installs. Listing and previews
// come from MarketSourceBrowser items; this only installs what it is handed. A sound is
// sha256-checked, kept in app data and imported into the media bin from there. Reported to the
// DownloadCenter as kind "sfx".
class SfxLibrary : public QObject
{
    Q_OBJECT
    // Bumped whenever any sound's install state changes; QML bindings on state() read it.
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

public:
    using Importer = std::function<QStringList(const QStringList &)>;

    explicit SfxLibrary(QObject *parent = nullptr,
                        drift::market::MarketTransport *transport = nullptr,
                        DownloadCenter *center = nullptr);

    void setImporter(Importer importer) { m_importer = std::move(importer); }

    int revision() const { return m_revision; }

    static QString installRoot();
    static QString jobId(const QString &id);

    // none | installing | installed | failed
    Q_INVOKABLE QString state(const QString &id) const;
    // Downloads the sound if needed, imports it into the media bin and fires ready(id, binAssetId).
    // Safe to call again while a download is running.
    Q_INVOKABLE void install(const QVariantMap &item);
    // Absolute path of the downloaded FLAC, empty when it is not on disk.
    Q_INVOKABLE QString localPath(const QString &id) const;
    // item.extra.peaks is 64 base64 bytes; QML draws 0-1 levels.
    Q_INVOKABLE QVariantList peaksOf(const QVariantMap &item) const;

signals:
    void revisionChanged();
    void stateChanged(const QString &id);
    void ready(const QString &id, const QString &binAssetId);
    void failed(const QString &id, const QString &message);

private:
    struct Install;

    void finish(const std::shared_ptr<Install> &install);
    void fail(const std::shared_ptr<Install> &install, const QString &code, const QString &message,
              bool retryable);
    void cancel(const QString &id);
    void publishStatus(const std::shared_ptr<Install> &install, const QString &status,
                       const QString &phase, double progress, qint64 bytes = 0, qint64 total = 0,
                       const QVariantMap &result = {}, const QString &code = {},
                       const QString &error = {}, bool retryable = false);
    void setState(const QString &id, const QString &state);

    Importer m_importer;
    drift::market::MarketTransport *m_transport = nullptr;
    DownloadCenter *m_center = nullptr;
    QHash<QString, QString> m_states;
    QHash<QString, std::shared_ptr<Install>> m_installs;
    QHash<QString, QVariantMap> m_items;
    int m_revision = 0;
};
