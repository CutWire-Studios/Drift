#include "app/online/marketplace/MarketTransport.h"

#include "app/online/market/MarketEndpoint.h"
#include "app/online/market/MarketIdentity.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QTimeZone>

#include <cstdlib>

namespace drift::market {

namespace {

constexpr int kApiTimeoutMs = 20000;
constexpr int kFileTimeoutMs = 10 * 60 * 1000;
constexpr qint64 kSkewToleranceSecs = 60;

QString tr(const char *text)
{
    return QCoreApplication::translate("MarketClient", text);
}

} // namespace

Problem Response::problem() const
{
    Problem p;
    p.message = MarketTransport::parseProblem(body, status, &p.code, &p.reason, &p.retryable,
                                              int(error));
    return p;
}

void Call::abort()
{
    m_aborted = true;
    if (m_reply)
        m_reply->abort();
}

MarketTransport::MarketTransport(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
    auto *cache = new QNetworkDiskCache(this);
    cache->setCacheDirectory(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                             + QStringLiteral("/marketplace"));
    cache->setMaximumCacheSize(256 * 1024 * 1024);
    m_network->setCache(cache);
}

MarketTransport *MarketTransport::instance()
{
    static MarketTransport *shared = new MarketTransport(QCoreApplication::instance());
    return shared;
}

QString MarketTransport::baseUrl()
{
    const QString override = qEnvironmentVariable("DRIFT_MARKET_API_URL");
    return override.isEmpty() ? kApiUrl : override;
}

bool MarketTransport::configured()
{
    return !baseUrl().isEmpty()
        && (!kClientKey.isEmpty() || !qEnvironmentVariable("DRIFT_MARKET_API_URL").isEmpty());
}

QUrl MarketTransport::apiUrl(const QString &path, const Query &query)
{
    QString base = baseUrl();
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    QString rel = path;
    if (!rel.startsWith(QLatin1Char('/')))
        rel.prepend(QLatin1Char('/'));
    QUrl url(base + rel);
    if (!query.isEmpty()) {
        QStringList parts;
        for (const auto &item : query)
            parts.append(QString::fromLatin1(QUrl::toPercentEncoding(item.first)) + QLatin1Char('=')
                         + QString::fromLatin1(QUrl::toPercentEncoding(item.second)));
        url.setQuery(parts.join(QLatin1Char('&')), QUrl::StrictMode);
    }
    return url;
}

void MarketTransport::sign(QNetworkRequest *request, const QByteArray &method,
                           const QByteArray &body) const
{
    const QString timestamp =
        QString::number(QDateTime::currentSecsSinceEpoch() + m_clockOffset);
    const QString nonce = makeNonce();
    const QString id = clientId();
    const QString target = pathAndQuery(request->url());
    const QString signature =
        signRequest(hmacKeyBytes(), timestamp, nonce, id, method, target, body);
    request->setRawHeader("X-Cutwire-Client", id.toUtf8());
    request->setRawHeader("X-Cutwire-Timestamp", timestamp.toUtf8());
    request->setRawHeader("X-Cutwire-Nonce", nonce.toUtf8());
    request->setRawHeader("X-Cutwire-Signature", signature.toUtf8());
    request->setRawHeader("X-Cutwire-App", appHeader().toUtf8());
    request->setRawHeader("X-Cutwire-Distribution", distributionId().toUtf8());
    request->setHeader(QNetworkRequest::UserAgentHeader, appHeader());
}

QNetworkRequest MarketTransport::buildRequest(const QUrl &url, const QByteArray &method,
                                              const QByteArray &body, bool json) const
{
    QNetworkRequest request(url);
    if (json) {
        request.setRawHeader("Accept", "application/json");
        request.setTransferTimeout(kApiTimeoutMs);
        if (method == "POST")
            request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    } else {
        request.setTransferTimeout(kFileTimeoutMs);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::NoLessSafeRedirectPolicy);
    }
    sign(&request, method, body);
    return request;
}

QNetworkReply *MarketTransport::dispatch(const QNetworkRequest &request, const QByteArray &method,
                                         const QByteArray &body)
{
    QNetworkReply *reply = method == "POST" ? m_network->post(request, body)
                                            : m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { learnSkew(reply); });
    return reply;
}

bool MarketTransport::learnSkew(QNetworkReply *reply)
{
    if (m_offsetLearned
        || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 401)
        return false;
    const QByteArray date = reply->rawHeader("Date");
    if (date.isEmpty())
        return false;
    QDateTime server = QLocale::c().toDateTime(QString::fromLatin1(date),
                                               QStringLiteral("ddd, dd MMM yyyy HH:mm:ss 'GMT'"));
    server.setTimeZone(QTimeZone::UTC);
    if (!server.isValid())
        return false;
    const QByteArray body = reply->peek(reply->bytesAvailable());
    if (QJsonDocument::fromJson(body).object().value(QStringLiteral("code")).toString()
        != QLatin1String("invalid_client"))
        return false;
    const qint64 offset = server.toSecsSinceEpoch() - QDateTime::currentSecsSinceEpoch();
    if (std::llabs(offset) <= kSkewToleranceSecs)
        return false;
    m_clockOffset = offset;
    m_offsetLearned = true;
    return true;
}

QNetworkReply *MarketTransport::get(const QUrl &url)
{
    return dispatch(buildRequest(url, "GET", {}, true), "GET", {});
}

QNetworkReply *MarketTransport::get(const QString &path, const Query &query)
{
    return get(apiUrl(path, query));
}

QNetworkReply *MarketTransport::post(const QUrl &url, const QByteArray &body)
{
    return dispatch(buildRequest(url, "POST", body, true), "POST", body);
}

QNetworkReply *MarketTransport::fetchSigned(const QUrl &url)
{
    if (url.host() == QUrl(baseUrl()).host())
        return dispatch(buildRequest(url, "GET", {}, false), "GET", {});
    QNetworkRequest request(url);
    request.setTransferTimeout(kFileTimeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    return m_network->get(request);
}

Call *MarketTransport::send(const QByteArray &method, const QUrl &url, const QByteArray &body,
                            const QByteArray &etag)
{
    auto *call = new Call(this);
    run(call, method, url, body, etag, true);
    return call;
}

Call *MarketTransport::send(const QByteArray &method, const QString &path, const Query &query,
                            const QByteArray &body, const QByteArray &etag)
{
    return send(method, apiUrl(path, query), body, etag);
}

void MarketTransport::run(Call *call, const QByteArray &method, const QUrl &url,
                          const QByteArray &body, const QByteArray &etag, bool mayRetry)
{
    QNetworkRequest request = buildRequest(url, method, body, true);
    if (!etag.isEmpty()) {
        request.setRawHeader("If-None-Match", etag);
        request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                             QNetworkRequest::AlwaysNetwork);
    }
    QNetworkReply *reply = method == "POST" ? m_network->post(request, body)
                                            : m_network->get(request);
    call->m_reply = reply;
    connect(reply, &QNetworkReply::finished, this,
            [this, call, reply, method, url, body, etag, mayRetry] {
        reply->deleteLater();
        if (mayRetry && !call->m_aborted && learnSkew(reply)) {
            run(call, method, url, body, etag, false);
            return;
        }
        Response response;
        response.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        response.error = reply->error();
        response.body = reply->readAll();
        response.etag = reply->rawHeader("ETag");
        call->deleteLater();
        if (!call->m_aborted)
            emit call->finished(response);
    });
}

QString MarketTransport::fallbackMessageForCode(const QString &code)
{
    if (code == QLatin1String("rate_limited"))
        return tr("Daily limit reached for this source. Try again later.");
    if (code == QLatin1String("provider_unavailable"))
        return tr("This source is temporarily unavailable.");
    if (code == QLatin1String("not_found"))
        return tr("That item is no longer available.");
    if (code == QLatin1String("invalid_client"))
        return tr("Could not reach the marketplace.");
    if (code == QLatin1String("download_failed"))
        return tr("Could not prepare that file.");
    return tr("Could not complete that request.");
}

bool MarketTransport::isRetryable(const QString &code, const QString &reason)
{
    // Reason first: it is the finer cause, and one code covers both a transient source
    // failure and a permanent one. An unrecognised reason falls through to the code, per
    // the contract — a value added later must not be read as "never retry".
    if (reason == QLatin1String("source_blocked") || reason == QLatin1String("source_unreachable")
        || reason == QLatin1String("source_timeout") || reason == QLatin1String("source_error")
        || reason == QLatin1String("download_error"))
        return true;
    // An operator switched the source off, the item is gone or was never fetchable, or the
    // file cannot be produced at all. Retrying changes nothing.
    if (reason == QLatin1String("source_disabled") || reason == QLatin1String("item_removed")
        || reason == QLatin1String("item_private") || reason == QLatin1String("item_geoblocked")
        || reason == QLatin1String("item_missing") || reason == QLatin1String("link_no_media")
        || reason == QLatin1String("link_unsupported") || reason == QLatin1String("file_too_large")
        || reason == QLatin1String("live_stream"))
        return false;

    // No reason, or one this build does not know.
    if (code == QLatin1String("not_found"))
        return false;
    // Rate limits do lift, but not now, and a button that fails again on sight reads as
    // broken. reset_at carries the real answer.
    if (code == QLatin1String("rate_limited"))
        return false;
    // Signing is a build/deployment mismatch and does not change on a second press.
    if (code == QLatin1String("invalid_client"))
        return false;
    return true;
}

QString MarketTransport::parseProblem(const QByteArray &body, int httpStatus, QString *codeOut,
                                   QString *reasonOut, bool *retryableOut, int networkError)
{
    const QJsonObject obj = QJsonDocument::fromJson(body).object();
    QString code = obj.value(QStringLiteral("code")).toString();
    const QString reason = obj.value(QStringLiteral("reason")).toString();

    // No HTTP status means the request never got an answer: DNS, TLS, refused, timed out.
    // That is not a service failure and none of the service's codes describe it.
    const bool transportFailure = httpStatus == 0 && networkError != 0;

    if (code.isEmpty()) {
        if (transportFailure)
            code = QStringLiteral("provider_unavailable");
        else if (httpStatus == 429)
            code = QStringLiteral("rate_limited");
        else if (httpStatus == 404)
            code = QStringLiteral("not_found");
        else if (httpStatus == 503)
            code = QStringLiteral("provider_unavailable");
        else
            code = QStringLiteral("download_failed");
    }
    if (codeOut)
        *codeOut = code;
    if (reasonOut)
        *reasonOut = reason;
    if (retryableOut)
        *retryableOut = transportFailure ? true : isRetryable(code, reason);

    if (transportFailure) {
        if (networkError == QNetworkReply::OperationCanceledError
            || networkError == QNetworkReply::TimeoutError)
            return tr("The marketplace took too long to answer. Try again.");
        return tr("Couldn’t reach the marketplace. Check your connection and try again.");
    }

    // The service writes this sentence per reason and sanitizes it on the way out, so it
    // is both more specific than anything derivable from the code and safe to show as-is.
    const QString detail = obj.value(QStringLiteral("detail")).toString().trimmed();
    if (!detail.isEmpty())
        return detail;
    return fallbackMessageForCode(code);
}

} // namespace drift::market
