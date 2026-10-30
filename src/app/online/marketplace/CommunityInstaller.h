#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QVariantMap>

#include <functional>
#include <memory>

class DownloadCenter;

namespace drift::market {
class Call;
class FileFetch;
class MarketTransport;
} // namespace drift::market

// Installs community items (community.<kind>) through POST /marketplace/installs. The file is
// downloaded and its sha256 checked into the cache, then handed to the handler for its kind:
// effect, transition and audio-effect packages, themes, looks and effect templates each land
// in their own catalog. The installed version of every item is remembered so a newer one can
// be offered as an update. Reported to the DownloadCenter as kind "community".
class CommunityInstaller : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

public:
    // Installs the file at `path`. Returns the id the item has in its catalog (empty when the
    // catalog has none to report) and sets *error on failure.
    using Handler = std::function<QString(const QString &path, QString *error)>;

    explicit CommunityInstaller(QObject *parent = nullptr,
                                drift::market::MarketTransport *transport = nullptr,
                                DownloadCenter *center = nullptr);

    // kind is the part after "community.": effect, transition, audio-effect, theme, look or
    // effect-template.
    void setHandler(const QString &kind, Handler handler);

    int revision() const { return m_revision; }

    static QString kindOf(const QString &source);
    static QString jobId(const QString &source, const QString &itemId);

    // none | installing | installed | update | failed
    Q_INVOKABLE QString state(const QVariantMap &item) const;
    Q_INVOKABLE QString installedVersion(const QString &itemId) const;
    Q_INVOKABLE QVariantMap result(const QVariantMap &item) const;
    Q_INVOKABLE void install(const QVariantMap &item);

signals:
    void revisionChanged();
    void stateChanged(const QString &source, const QString &itemId);
    void installed(const QString &source, const QString &itemId, const QVariantMap &result);
    void failed(const QString &source, const QString &itemId, const QString &code,
                const QString &message);

private:
    struct Install;

    void finishDownload(const std::shared_ptr<Install> &install, const QString &path);
    void fail(const std::shared_ptr<Install> &install, const QString &code, const QString &message,
              bool retryable);
    void cancel(const QString &id);
    void publish(const std::shared_ptr<Install> &install, const QString &status, const QString &phase,
                 double progress, qint64 bytes = 0, qint64 total = 0, const QVariantMap &result = {},
                 const QString &code = {}, const QString &error = {}, bool retryable = false);
    void setState(const std::shared_ptr<Install> &install, const QString &state);

    drift::market::MarketTransport *m_transport = nullptr;
    DownloadCenter *m_center = nullptr;
    QHash<QString, Handler> m_handlers;
    QHash<QString, std::shared_ptr<Install>> m_installs;
    QHash<QString, QString> m_states;
    QHash<QString, QVariantMap> m_results;
    QHash<QString, QVariantMap> m_items;
    int m_revision = 0;
};
