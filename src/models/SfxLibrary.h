#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QVariantList>

class AssetLibrary;
class MarketClient;
class QNetworkAccessManager;
class QNetworkReply;

// CutWire's sound-effects library (GET /api/v1/sfx). Like Drift Assets it is CutWire's own and
// free, so there is no consent gate, quota or folder prompt: a sound is sha256-checked, kept in
// app data and imported into the media bin from there. The server leaves out categories hidden
// for this build's distribution; when nothing is left, `available` is false and the Market drops
// the tab.
class SfxLibrary : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(bool loaded READ loaded NOTIFY libraryChanged)
    Q_PROPERTY(bool available READ available NOTIFY libraryChanged)
    Q_PROPERTY(QVariantList categories READ categories NOTIFY libraryChanged)
    Q_PROPERTY(QVariantList sounds READ sounds NOTIFY libraryChanged)
    // Bumped whenever any sound's install state changes; QML bindings on state() read it.
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

public:
    SfxLibrary(MarketClient *market, AssetLibrary *library, QObject *parent = nullptr);

    bool loading() const { return m_loading; }
    QString error() const { return m_error; }
    bool loaded() const { return m_loaded; }
    bool available() const { return !m_sounds.isEmpty(); }
    QVariantList categories() const { return m_categories; }
    QVariantList sounds() const { return m_sounds; }
    int revision() const { return m_revision; }

    Q_INVOKABLE void refresh();
    // Sounds in a category and subcategory (either empty for all) whose name, category and
    // subcategory contain every word of the query. Library order.
    Q_INVOKABLE QVariantList filter(const QString &query, const QString &category,
                                    const QString &subcategory) const;
    // none | installing | installed | failed
    Q_INVOKABLE QString state(const QString &id) const;
    // Downloads the sound if needed, imports it into the media bin and fires ready(id, binAssetId).
    // Safe to call again while a download is running.
    Q_INVOKABLE void install(const QString &id);
    // Absolute path of the downloaded FLAC, empty when it is not on disk.
    Q_INVOKABLE QString localPath(const QString &id) const;

    // Test seam and the network reply's destination: replaces the library.
    void applyLibrary(const QJsonObject &library);
    static QString installRoot();

signals:
    void loadingChanged();
    void errorChanged();
    void libraryChanged();
    void revisionChanged();
    void ready(const QString &id, const QString &binAssetId);
    void failed(const QString &id, const QString &message);

private:
    QVariantMap soundById(const QString &id) const;
    QString pathFor(const QVariantMap &sound) const;
    bool isInstalled(const QVariantMap &sound) const;
    void finishInstall(const QString &id);
    void failInstall(const QString &id, const QString &message);
    void setState(const QString &id, const QString &state);

    QPointer<MarketClient> m_market;
    QPointer<AssetLibrary> m_library;
    QNetworkAccessManager *m_network = nullptr;

    bool m_loading = false;
    bool m_loaded = false;
    QString m_error;
    QVariantList m_categories;
    QVariantList m_sounds;
    QHash<QString, int> m_index;
    QHash<QString, QString> m_states;
    QHash<QString, QPointer<QNetworkReply>> m_downloads;
    int m_revision = 0;
};
