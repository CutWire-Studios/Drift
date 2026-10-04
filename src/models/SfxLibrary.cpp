#include "SfxLibrary.h"

#include "AssetLibrary.h"
#include "MarketClient.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QStandardPaths>

#include <algorithm>

namespace {

constexpr int kFileTimeoutMs = 60000;

bool validId(const QString &id)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_-]{0,127}$"));
    return re.match(id).hasMatch();
}

bool validName(const QString &name)
{
    return !name.isEmpty() && !name.contains(QLatin1Char('/')) && !name.contains(QLatin1Char('\\'))
        && name != QLatin1String(".") && name != QLatin1String("..");
}

} // namespace

SfxLibrary::SfxLibrary(MarketClient *market, AssetLibrary *library, QObject *parent)
    : QObject(parent)
    , m_market(market)
    , m_library(library)
    , m_network(new QNetworkAccessManager(this))
{
}

QString SfxLibrary::installRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/sfx");
}

void SfxLibrary::refresh()
{
    if (m_loading || !m_market || !m_market->configured())
        return;
    m_loading = true;
    emit loadingChanged();
    QNetworkReply *reply = m_market->signedGet(QStringLiteral("/sfx"));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        m_loading = false;
        emit loadingChanged();
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        const QString error = reply->error() == QNetworkReply::NoError && doc.isObject()
            ? QString()
            : tr("Could not load sound effects. Check your connection and try again.");
        if (error != m_error) {
            m_error = error;
            emit errorChanged();
        }
        if (error.isEmpty())
            applyLibrary(doc.object());
    });
}

void SfxLibrary::applyLibrary(const QJsonObject &library)
{
    m_categories.clear();
    m_sounds.clear();
    m_index.clear();
    for (const QJsonValue &c : library.value(QStringLiteral("categories")).toArray())
        m_categories.append(c.toObject().toVariantMap());
    for (const QJsonValue &v : library.value(QStringLiteral("sounds")).toArray()) {
        QVariantMap sound = v.toObject().toVariantMap();
        const QString id = sound.value(QStringLiteral("id")).toString();
        if (!validId(id) || !validName(sound.value(QStringLiteral("file")).toString())
            || m_index.contains(id))
            continue;
        // The wire carries 64 base64 bytes; QML draws 0-1 levels.
        QVariantList peaks;
        for (const char b : QByteArray::fromBase64(sound.value(QStringLiteral("peaks")).toString().toLatin1()))
            peaks.append(static_cast<unsigned char>(b) / 255.0);
        sound.insert(QStringLiteral("peaks"), peaks);
        m_index.insert(id, m_sounds.size());
        m_sounds.append(sound);
    }
    m_loaded = true;
    emit libraryChanged();
    ++m_revision;
    emit revisionChanged();
}

QVariantList SfxLibrary::filter(const QString &query, const QString &category,
                                const QString &subcategory) const
{
    const QStringList words = query.toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QVariantList out;
    for (const QVariant &v : m_sounds) {
        const QVariantMap sound = v.toMap();
        const QString cat = sound.value(QStringLiteral("category")).toString();
        const QString sub = sound.value(QStringLiteral("subcategory")).toString();
        if ((!category.isEmpty() && cat != category) || (!subcategory.isEmpty() && sub != subcategory))
            continue;
        const QString hay = (sound.value(QStringLiteral("name")).toString() + QLatin1Char(' ') + cat
                             + QLatin1Char(' ') + sub)
                                .toLower();
        if (std::all_of(words.cbegin(), words.cend(), [&](const QString &w) { return hay.contains(w); }))
            out.append(sound);
    }
    return out;
}

QVariantMap SfxLibrary::soundById(const QString &id) const
{
    const int i = m_index.value(id, -1);
    return i < 0 ? QVariantMap() : m_sounds.at(i).toMap();
}

QString SfxLibrary::pathFor(const QVariantMap &sound) const
{
    return installRoot() + QLatin1Char('/') + sound.value(QStringLiteral("id")).toString()
        + QLatin1Char('/') + sound.value(QStringLiteral("file")).toString();
}

bool SfxLibrary::isInstalled(const QVariantMap &sound) const
{
    const QFileInfo info(pathFor(sound));
    return info.isFile() && info.size() == sound.value(QStringLiteral("size")).toLongLong();
}

QString SfxLibrary::state(const QString &id) const
{
    const QString s = m_states.value(id);
    if (!s.isEmpty() && s != QLatin1String("installed"))
        return s;
    const QVariantMap sound = soundById(id);
    return !sound.isEmpty() && isInstalled(sound) ? QStringLiteral("installed") : QStringLiteral("none");
}

QString SfxLibrary::localPath(const QString &id) const
{
    const QVariantMap sound = soundById(id);
    return !sound.isEmpty() && isInstalled(sound) ? pathFor(sound) : QString();
}

void SfxLibrary::setState(const QString &id, const QString &state)
{
    m_states.insert(id, state);
    ++m_revision;
    emit revisionChanged();
}

void SfxLibrary::install(const QString &id)
{
    const QVariantMap sound = soundById(id);
    if (sound.isEmpty() || m_downloads.contains(id))
        return;
    if (isInstalled(sound)) {
        finishInstall(id);
        return;
    }

    const QString target = pathFor(sound);
    if (!QDir().mkpath(QFileInfo(target).absolutePath())) {
        failInstall(id, tr("Could not write to the app data folder."));
        return;
    }
    QNetworkRequest request(sound.value(QStringLiteral("download_url")).toUrl());
    request.setTransferTimeout(kFileTimeoutMs);
    QNetworkReply *reply = m_network->get(request);
    m_downloads.insert(id, reply);
    setState(id, QStringLiteral("installing"));
    connect(reply, &QNetworkReply::finished, this, [this, id, sound, target, reply]() {
        reply->deleteLater();
        if (!m_downloads.contains(id))
            return;
        const QByteArray data = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            failInstall(id, tr("Could not download that sound. Check your connection and try again."));
            return;
        }
        const QString sha = QString::fromLatin1(
            QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
        const QString want = sound.value(QStringLiteral("sha256")).toString();
        if (data.size() != sound.value(QStringLiteral("size")).toLongLong()
            || (!want.isEmpty() && sha != want)) {
            failInstall(id, tr("That download was damaged. Try again."));
            return;
        }
        const QString partial = target + QStringLiteral(".partial");
        QFile out(partial);
        if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size()) {
            failInstall(id, tr("Could not write to the app data folder."));
            return;
        }
        out.close();
        QFile::remove(target);
        if (!QFile::rename(partial, target)) {
            QFile::remove(partial);
            failInstall(id, tr("Could not write to the app data folder."));
            return;
        }
        m_downloads.remove(id);
        finishInstall(id);
    });
}

void SfxLibrary::finishInstall(const QString &id)
{
    const QStringList ids = m_library ? m_library->importLocalPaths({localPath(id)}) : QStringList();
    if (ids.isEmpty()) {
        failInstall(id, tr("Could not add that sound to the media bin."));
        return;
    }
    setState(id, QStringLiteral("installed"));
    emit ready(id, ids.first());
}

void SfxLibrary::failInstall(const QString &id, const QString &message)
{
    if (const QPointer<QNetworkReply> reply = m_downloads.take(id); reply)
        reply->abort();
    setState(id, QStringLiteral("failed"));
    emit failed(id, message);
}
