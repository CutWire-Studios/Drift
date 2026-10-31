#pragma once

#include <QByteArray>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>
#include <QList>
#include <QPair>

class QNetworkAccessManager;

namespace drift::market {

// Raw key/value pairs. They are percent-encoded when the URL is built; QUrlQuery is avoided
// because it decodes "%41" in a value and leaves "+" alone.
using Query = QList<QPair<QString, QString>>;

struct Problem
{
    QString code;
    QString reason;
    QString message;
    bool retryable = true;
};

struct Response
{
    int status = 0;
    QNetworkReply::NetworkError error = QNetworkReply::NoError;
    QByteArray body;
    QByteArray etag;
    bool notModified() const { return status == 304; }
    bool ok() const { return status >= 200 && status < 300; }
    Problem problem() const;
};

// One in-flight request. Deletes itself after finished() so a caller only keeps a QPointer to
// abort it. A request that fails with a skewed-clock 401 is re-signed and sent once more
// before finished() is emitted.
class Call : public QObject
{
    Q_OBJECT
public:
    void abort();

signals:
    void finished(const drift::market::Response &response);

private:
    friend class MarketTransport;
    explicit Call(QObject *parent) : QObject(parent) {}

    QPointer<QNetworkReply> m_reply;
    bool m_aborted = false;
};

// Shared HTTP stack for everything that talks to the Drift marketplace API: one network
// manager and disk cache, one base URL, and HMAC signing with the client identity. Request
// signing lives here so every market client signs identically.
class MarketTransport : public QObject
{
    Q_OBJECT
public:
    explicit MarketTransport(QObject *parent = nullptr);

    static MarketTransport *instance();

    QNetworkAccessManager *network() const { return m_network; }

    // DRIFT_MARKET_API_URL in the environment overrides the compiled-in URL so tests and
    // staging can point elsewhere without a rebuild. The compiled-in key still signs.
    static QString baseUrl();
    static bool configured();
    static QUrl apiUrl(const QString &path, const Query &query = {});

    // Signed, reply-returning requests. A skewed-clock 401 is learned from (later requests
    // are corrected) but not retried; use send() for that.
    QNetworkReply *get(const QUrl &url);
    QNetworkReply *get(const QString &path, const Query &query = {});
    QNetworkReply *post(const QUrl &url, const QByteArray &body);
    // Signed GET of an absolute URL, for thumbnails and files. Only signed when the host
    // matches the API host; a URL on another host goes out unsigned.
    QNetworkReply *fetchSigned(const QUrl &url);

    // Request with one-time clock-skew retry and optional conditional GET. etag is sent as
    // If-None-Match and the cache is bypassed so a 304 reaches the caller.
    Call *send(const QByteArray &method, const QUrl &url, const QByteArray &body = {},
               const QByteArray &etag = {});
    Call *send(const QByteArray &method, const QString &path, const Query &query,
               const QByteArray &body = {}, const QByteArray &etag = {});

    void sign(QNetworkRequest *request, const QByteArray &method, const QByteArray &body) const;

    qint64 clockOffsetSecs() const { return m_clockOffset; }
    void setClockOffsetSecs(qint64 secs) { m_clockOffset = secs; m_offsetLearned = secs != 0; }

    static QString fallbackMessageForCode(const QString &code);
    static bool isRetryable(const QString &code, const QString &reason);
    static QString parseProblem(const QByteArray &body, int httpStatus, QString *codeOut,
                                QString *reasonOut = nullptr, bool *retryableOut = nullptr,
                                int networkError = 0);

private:
    QNetworkRequest buildRequest(const QUrl &url, const QByteArray &method, const QByteArray &body,
                                 bool json) const;
    QNetworkReply *dispatch(const QNetworkRequest &request, const QByteArray &method,
                            const QByteArray &body);
    bool learnSkew(QNetworkReply *reply);
    void run(Call *call, const QByteArray &method, const QUrl &url, const QByteArray &body,
             const QByteArray &etag, bool mayRetry);

    QNetworkAccessManager *m_network = nullptr;
    qint64 m_clockOffset = 0;
    bool m_offsetLearned = false;
};

} // namespace drift::market

Q_DECLARE_METATYPE(drift::market::Response)
