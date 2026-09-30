#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QVariantList>

#include <memory>

class AppController;
class AssetLibrary;
class MarketClient;
class QNetworkAccessManager;
class QNetworkReply;

// Drift Assets: CutWire's own Lottie animations, face props and 3D objects, served by the
// marketplace as a versioned pack (GET /api/v1/assets). This is the Market's first page, so
// unlike the stock sources it needs no consent gate, no quota and no folder prompt: files are
// sha256-checked and installed into app data, then used straight from there.
//
// Lottie and 3D objects go into the media bin; face props are installed through
// AppController::importFaceProps so the Face Props picker lists them.
class DriftAssetStore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QVariantList categories READ categories NOTIFY packChanged)
    Q_PROPERTY(QVariantList assets READ assets NOTIFY packChanged)
    // Bumped whenever any asset's install state changes; QML bindings on state() read it.
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

public:
    DriftAssetStore(MarketClient *market, AssetLibrary *library, AppController *app,
                    QObject *parent = nullptr);

    bool loading() const { return m_loading; }
    QString error() const { return m_error; }
    QVariantList categories() const { return m_categories; }
    QVariantList assets() const { return m_assets; }
    int revision() const { return m_revision; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE QVariantList assetsIn(const QString &categoryId) const;
    // Name, tags and description, every word must match. Pack order.
    Q_INVOKABLE QVariantList search(const QString &query) const;
    Q_INVOKABLE QVariantMap assetById(const QString &id) const;
    // none | installing | installed | failed. `id` is the pack asset id, meaning its default design.
    Q_INVOKABLE QString state(const QString &id) const;
    // Same as state(), for one design. An empty variantId is the default design.
    Q_INVOKABLE QString variantState(const QString &id, const QString &variantId) const;
    // Install directory key: the asset id for its default design, otherwise "<id>--<variant>".
    Q_INVOKABLE QString installKey(const QString &id, const QString &variantId) const;
    // Installs the default design if needed, then: Lottie and objects are imported into the media
    // bin and `ready(id, binAssetId)` fires; face props are added to the prop library and
    // `ready(id, "")` fires. Safe to call again while an install is running.
    Q_INVOKABLE void install(const QString &id);
    // Installs one design. variantId empty, or the default design's id, is install().
    // ready() reports installKey(), which is also the face-prop id the library lists.
    Q_INVOKABLE void installVariant(const QString &id, const QString &variantId);
    // Absolute path of the installed main file (.json / .glb), empty when not installed.
    // `id` may be a pack asset id or an installKey().
    Q_INVOKABLE QString localPath(const QString &id) const;

    // Test seam and the network reply's destination: replaces the pack.
    void applyPack(const QJsonObject &pack);
    static QString installRoot();

signals:
    void loadingChanged();
    void errorChanged();
    void packChanged();
    void revisionChanged();
    void ready(const QString &id, const QString &binAssetId);
    void failed(const QString &id, const QString &message);

private:
    struct Install;

    QString assetDir(const QVariantMap &asset) const;
    bool isInstalled(const QVariantMap &asset) const;
    QString defaultVariantId(const QVariantMap &asset) const;
    // The asset with one design's files overlaid. Empty when that design is not in the pack.
    // The map's id is the install key.
    QVariantMap viewFor(const QString &assetId, const QString &variantId) const;
    QVariantMap viewForKey(const QString &key) const;
    QString assetIdForKey(const QString &key) const;
    void fetchNext(const QString &key);
    void finishInstall(const QString &key);
    void failInstall(const QString &key, const QString &message);
    void setState(const QString &key, const QString &state);

    QPointer<MarketClient> m_market;
    QPointer<AssetLibrary> m_library;
    QPointer<AppController> m_app;
    QNetworkAccessManager *m_network = nullptr;

    bool m_loading = false;
    QString m_error;
    QVariantList m_categories;
    QVariantList m_assets;
    QHash<QString, int> m_index;
    QHash<QString, QString> m_states;
    QHash<QString, std::shared_ptr<Install>> m_installs;
    int m_revision = 0;
};
