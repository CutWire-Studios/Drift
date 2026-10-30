#include "app/online/marketplace/MarketplaceManifest.h"

#include "app/online/marketplace/MarketTransport.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>

using namespace drift::market;

namespace {

const QSet<QString> kSectionTypes = {
    QStringLiteral("hero"),         QStringLiteral("notice"),      QStringLiteral("shelf"),
    QStringLiteral("grid"),         QStringLiteral("chips"),       QStringLiteral("stock-browser"),
    QStringLiteral("sfx-browser"),  QStringLiteral("downloads"),
};
const QSet<QString> kSourceBound = {
    QStringLiteral("shelf"), QStringLiteral("grid"), QStringLiteral("chips"),
    QStringLiteral("stock-browser"), QStringLiteral("sfx-browser"),
};
const QSet<QString> kReservedPages = {QStringLiteral("item"), QStringLiteral("downloads")};

QString normalizedLanguage(QString code)
{
    return code.replace(QLatin1Char('_'), QLatin1Char('-')).toLower();
}

QString localizedTitle(const QJsonObject &page, const QString &language)
{
    const QString fallback = page.value(QStringLiteral("title")).toString();
    const QString wanted = normalizedLanguage(language);
    if (wanted.isEmpty())
        return fallback;
    const QJsonObject titles = page.value(QStringLiteral("titles")).toObject();
    QString primary;
    for (auto it = titles.begin(); it != titles.end(); ++it) {
        const QString key = normalizedLanguage(it.key());
        if (key == wanted)
            return it.value().toString();
        if (key == wanted.section(QLatin1Char('-'), 0, 0))
            primary = it.value().toString();
    }
    return primary.isEmpty() ? fallback : primary;
}

QJsonObject readJson(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

} // namespace

MarketplaceManifest::MarketplaceManifest(QObject *parent, MarketTransport *transport,
                                         const QString &cacheDir)
    : QObject(parent)
    , m_transport(transport ? transport : MarketTransport::instance())
    , m_cacheDir(cacheDir.isEmpty()
                     ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                           + QStringLiteral("/marketplace")
                     : cacheDir)
{
    loadLocal();
}

QString MarketplaceManifest::cachePath() const
{
    return m_cacheDir + QStringLiteral("/manifest.json");
}

QString MarketplaceManifest::etagPath() const
{
    return m_cacheDir + QStringLiteral("/manifest.etag");
}

QString MarketplaceManifest::uiLanguage()
{
    const QString stored = QSettings().value(QStringLiteral("ui/language")).toString().trimmed();
    return stored.isEmpty() ? QLocale::system().name() : stored;
}

QVariantMap MarketplaceManifest::prune(const QJsonObject &manifest, const QString &language)
{
    if (manifest.value(QStringLiteral("schema")).toInt() != kSupportedSchema)
        return {};

    const QJsonObject sources = manifest.value(QStringLiteral("sources")).toObject();
    QVariantList pages;
    QSet<QString> keptPages;
    QJsonArray prunedPages;

    for (const QJsonValue &pageValue : manifest.value(QStringLiteral("pages")).toArray()) {
        QJsonObject page = pageValue.toObject();
        const QString pageId = page.value(QStringLiteral("id")).toString();
        if (pageId.isEmpty() || kReservedPages.contains(pageId) || keptPages.contains(pageId))
            continue;

        QJsonArray kept;
        QSet<QString> grids;
        for (const QJsonValue &sectionValue : page.value(QStringLiteral("sections")).toArray()) {
            const QJsonObject section = sectionValue.toObject();
            const QString type = section.value(QStringLiteral("type")).toString();
            if (!kSectionTypes.contains(type))
                continue;
            if (kSourceBound.contains(type)) {
                const QString sourceId = section.value(QStringLiteral("source")).toObject()
                                             .value(QStringLiteral("id")).toString();
                if (!sources.contains(sourceId))
                    continue;
            }
            if (type == QLatin1String("grid"))
                grids.insert(section.value(QStringLiteral("id")).toString());
            kept.append(section);
        }
        QJsonArray sections;
        for (const QJsonValue &value : kept) {
            const QJsonObject section = value.toObject();
            if (section.value(QStringLiteral("type")).toString() == QLatin1String("chips")
                && !grids.contains(section.value(QStringLiteral("target")).toString()))
                continue;
            sections.append(section);
        }
        if (sections.isEmpty())
            continue;

        page.remove(QStringLiteral("titles"));
        page.insert(QStringLiteral("title"), localizedTitle(pageValue.toObject(), language));
        page.insert(QStringLiteral("sections"), sections);
        keptPages.insert(pageId);
        prunedPages.append(page);
    }

    for (const QJsonValue &pageValue : std::as_const(prunedPages)) {
        QJsonObject page = pageValue.toObject();
        QJsonArray sections;
        for (const QJsonValue &sectionValue : page.value(QStringLiteral("sections")).toArray()) {
            QJsonObject section = sectionValue.toObject();
            const QJsonObject seeAll = section.value(QStringLiteral("seeAll")).toObject();
            if (!seeAll.isEmpty() && !keptPages.contains(seeAll.value(QStringLiteral("page")).toString()))
                section.remove(QStringLiteral("seeAll"));
            const QJsonObject action = section.value(QStringLiteral("action")).toObject();
            if (action.value(QStringLiteral("type")).toString() == QLatin1String("open-page")
                && !keptPages.contains(action.value(QStringLiteral("page")).toString()))
                section.remove(QStringLiteral("action"));
            sections.append(section);
        }
        page.insert(QStringLiteral("sections"), sections);
        pages.append(page.toVariantMap());
    }

    if (pages.isEmpty())
        return {};
    QVariantMap out;
    out.insert(QStringLiteral("schema"), kSupportedSchema);
    out.insert(QStringLiteral("features"), manifest.value(QStringLiteral("features")).toObject().toVariantMap());
    out.insert(QStringLiteral("sources"), sources.toVariantMap());
    out.insert(QStringLiteral("pages"), pages);
    return out;
}

bool MarketplaceManifest::applyRaw(const QJsonObject &raw)
{
    const QVariantMap pruned = prune(raw, uiLanguage());
    if (pruned.isEmpty())
        return false;
    m_raw = raw;
    m_pages = pruned.value(QStringLiteral("pages")).toList();
    m_features = pruned.value(QStringLiteral("features")).toMap();
    m_sources = pruned.value(QStringLiteral("sources")).toMap();
    ++m_revision;
    emit changed();
    return true;
}

void MarketplaceManifest::loadLocal()
{
    const QJsonObject cached = readJson(cachePath());
    if (!cached.isEmpty() && applyRaw(cached)) {
        QFile etag(etagPath());
        if (etag.open(QIODevice::ReadOnly))
            m_etag = etag.readAll().trimmed();
        return;
    }
    applyRaw(readJson(QStringLiteral(":/marketplace/default-manifest.json")));
}

void MarketplaceManifest::writeCache(const QByteArray &body, const QByteArray &etag) const
{
    QDir().mkpath(m_cacheDir);
    QSaveFile manifest(cachePath());
    if (manifest.open(QIODevice::WriteOnly)) {
        manifest.write(body);
        manifest.commit();
    }
    QSaveFile tag(etagPath());
    if (tag.open(QIODevice::WriteOnly)) {
        tag.write(etag);
        tag.commit();
    }
}

void MarketplaceManifest::setLoading(bool loading)
{
    if (m_loading == loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}

void MarketplaceManifest::setStatus(const QString &error, bool offline)
{
    if (m_error == error && m_offline == offline)
        return;
    m_error = error;
    m_offline = offline;
    emit errorChanged();
}

void MarketplaceManifest::refresh()
{
    if (m_loading)
        return;
    if (!MarketTransport::configured()) {
        setStatus(MarketTransport::fallbackMessageForCode(QStringLiteral("invalid_client")), true);
        return;
    }
    setLoading(true);
    Call *call = m_transport->send("GET", QStringLiteral("/marketplace"), {}, {}, m_etag);
    m_call = call;
    connect(call, &Call::finished, this, [this](const Response &response) {
        setLoading(false);
        if (response.notModified()) {
            applyRaw(m_raw);
            setStatus({}, false);
            return;
        }
        const QJsonObject raw = QJsonDocument::fromJson(response.body).object();
        if (!response.ok() || raw.isEmpty()) {
            setStatus(response.problem().message, true);
            return;
        }
        if (applyRaw(raw)) {
            m_etag = response.etag;
            writeCache(response.body, m_etag);
            setStatus({}, false);
            return;
        }
        m_etag.clear();
        m_raw = {};
        applyRaw(readJson(QStringLiteral(":/marketplace/default-manifest.json")));
        setStatus({}, false);
    });
}

QVariantMap MarketplaceManifest::source(const QString &id) const
{
    return m_sources.value(id).toMap();
}

QVariant MarketplaceManifest::feature(const QString &key, const QVariant &fallback) const
{
    return m_features.contains(key) ? m_features.value(key) : fallback;
}

QVariantMap MarketplaceManifest::page(const QString &id) const
{
    for (const QVariant &row : m_pages) {
        const QVariantMap page = row.toMap();
        if (page.value(QStringLiteral("id")).toString() == id)
            return page;
    }
    return {};
}

void MarketplaceManifest::requestOpen(const QString &page, const QString &tab,
                                      const QString &source, const QString &itemId)
{
    emit openRequested(page, tab, source, itemId);
}
