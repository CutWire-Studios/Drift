#include "app/online/market/MarketIdentity.h"
#include "app/online/marketplace/MarketSourceBrowser.h"
#include "app/online/marketplace/MarketTransport.h"
#include "app/online/marketplace/MarketplaceManifest.h"

#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUrlQuery>
#include <QtTest>

#include <functional>

using namespace drift::market;

namespace {

struct FakeRequest
{
    QByteArray method;
    QByteArray target;
    QMap<QByteArray, QByteArray> headers;
    QByteArray body;
};

class FakeServer : public QObject
{
public:
    QTcpServer server;
    QList<FakeRequest> requests;
    std::function<QByteArray(const FakeRequest &)> handler;

    QString base() const { return QStringLiteral("http://127.0.0.1:%1/api/v1").arg(server.serverPort()); }

    bool start()
    {
        if (!server.listen(QHostAddress::LocalHost))
            return false;
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *sock = server.nextPendingConnection()) {
                connect(sock, &QTcpSocket::readyRead, this, [this, sock] { read(sock); });
                connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
            }
        });
        return true;
    }

    static QByteArray reply(int status, const QByteArray &body, const QByteArray &extra = {})
    {
        return "HTTP/1.1 " + QByteArray::number(status) + " X\r\nContent-Type: application/json\r\n"
            "Content-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n"
            + extra + "\r\n" + body;
    }

    static QByteArray json(int status, const QJsonObject &object, const QByteArray &extra = {})
    {
        return reply(status, QJsonDocument(object).toJson(QJsonDocument::Compact), extra);
    }

private:
    void read(QTcpSocket *sock)
    {
        const QByteArray data = sock->property("buf").toByteArray() + sock->readAll();
        const int end = data.indexOf("\r\n\r\n");
        if (end < 0) {
            sock->setProperty("buf", data);
            return;
        }
        FakeRequest req;
        const QList<QByteArray> lines = data.left(end).split('\n');
        const QList<QByteArray> first = lines.first().trimmed().split(' ');
        req.method = first.value(0);
        req.target = first.value(1);
        for (int i = 1; i < lines.size(); ++i) {
            const int colon = lines[i].indexOf(':');
            if (colon > 0)
                req.headers.insert(lines[i].left(colon).trimmed().toLower(), lines[i].mid(colon + 1).trimmed());
        }
        const int length = req.headers.value("content-length", "0").toInt();
        if (data.size() - end - 4 < length) {
            sock->setProperty("buf", data);
            return;
        }
        req.body = data.mid(end + 4, length);
        sock->setProperty("buf", QByteArray());
        requests.append(req);
        sock->write(handler ? handler(req) : reply(404, "{}"));
        sock->disconnectFromHost();
    }
};

QJsonObject manifestJson()
{
    return QJsonDocument::fromJson(R"({
      "schema": 1,
      "features": {"community": true},
      "sources": {"drift-assets": {"kind":"drift-assets","label":"Drift Assets"},
                  "sfx": {"kind":"sfx","label":"SFX"}},
      "pages": [
        {"id":"home","title":"Home","titles":{"fr":"Accueil","pt-BR":"Inicio"},"sections":[
          {"id":"a","type":"shelf","title":"A","source":{"id":"drift-assets"},"seeAll":{"page":"gone"}},
          {"id":"b","type":"shelf","title":"B","source":{"id":"community.effect"}},
          {"id":"c","type":"future-widget"}]}
      ]})").object();
}

} // namespace

class TestMarketplace : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void pruneDropsUnknownTypesAndMissingSources();
    void pruneDropsChipsWithoutTarget();
    void pruneDropsReservedAndEmptyPages();
    void pruneRejectsNewerSchema();
    void pruneLocalizesTitles();

    void bundledDefaultLoadsOffline();
    void cacheServedWhenOffline();
    void etagNotModifiedKeepsCache();
    void okReplacesAndWritesCache();
    void newerSchemaFallsBackToBundled();

    void signingMatchesIdentity();
    void clockSkewIsCorrectedOnce();
    void problemParsing();

    void browserPagesWithCursor();
    void browserDebouncesAndCancels();
    void browserEncodesQuery();
    void browserReportsErrors();

private:
    FakeServer m_server;
    MarketTransport *m_transport = nullptr;
};

void TestMarketplace::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QVERIFY(m_server.start());
    qputenv("DRIFT_MARKET_API_URL", m_server.base().toUtf8());
    m_transport = new MarketTransport(this);
}

void TestMarketplace::init()
{
    m_server.requests.clear();
    m_server.handler = nullptr;
    m_transport->setClockOffsetSecs(0);
}

void TestMarketplace::pruneDropsUnknownTypesAndMissingSources()
{
    const QVariantMap out = MarketplaceManifest::prune(manifestJson());
    const QVariantList pages = out.value("pages").toList();
    QCOMPARE(pages.size(), 1);
    const QVariantList sections = pages.first().toMap().value("sections").toList();
    QCOMPARE(sections.size(), 1);
    QCOMPARE(sections.first().toMap().value("id").toString(), QStringLiteral("a"));
    QVERIFY(!sections.first().toMap().contains("seeAll"));
    QVERIFY(out.value("features").toMap().value("community").toBool());
}

void TestMarketplace::pruneDropsChipsWithoutTarget()
{
    QJsonObject m = manifestJson();
    m["pages"] = QJsonDocument::fromJson(R"([
      {"id":"p","title":"P","sections":[
        {"id":"chips1","type":"chips","target":"grid1","source":{"id":"drift-assets"}},
        {"id":"grid1","type":"grid","source":{"id":"community.effect"}},
        {"id":"chips2","type":"chips","target":"grid2","source":{"id":"drift-assets"}},
        {"id":"grid2","type":"grid","source":{"id":"drift-assets"}}]}])").array();
    const QVariantList sections =
        MarketplaceManifest::prune(m).value("pages").toList().first().toMap().value("sections").toList();
    QStringList ids;
    for (const QVariant &s : sections)
        ids << s.toMap().value("id").toString();
    QCOMPARE(ids, (QStringList{"chips2", "grid2"}));
}

void TestMarketplace::pruneDropsReservedAndEmptyPages()
{
    QJsonObject m = manifestJson();
    m["pages"] = QJsonDocument::fromJson(R"([
      {"id":"item","title":"I","sections":[{"id":"n","type":"notice","text":"x"}]},
      {"id":"downloads","title":"D","sections":[{"id":"d","type":"downloads"}]},
      {"id":"empty","title":"E","sections":[{"id":"z","type":"future-widget"}]},
      {"id":"ok","title":"OK","sections":[{"id":"n","type":"notice","text":"x"}]}])").array();
    const QVariantList pages = MarketplaceManifest::prune(m).value("pages").toList();
    QCOMPARE(pages.size(), 1);
    QCOMPARE(pages.first().toMap().value("id").toString(), QStringLiteral("ok"));
}

void TestMarketplace::pruneRejectsNewerSchema()
{
    QJsonObject m = manifestJson();
    m["schema"] = 2;
    QVERIFY(MarketplaceManifest::prune(m).isEmpty());
}

void TestMarketplace::pruneLocalizesTitles()
{
    auto title = [](const QString &lang) {
        return MarketplaceManifest::prune(manifestJson(), lang).value("pages").toList().first()
            .toMap().value("title").toString();
    };
    QCOMPARE(title("fr_CA"), QStringLiteral("Accueil"));
    QCOMPARE(title("pt_BR"), QStringLiteral("Inicio"));
    QCOMPARE(title("de"), QStringLiteral("Home"));
    QCOMPARE(title(""), QStringLiteral("Home"));
}

void TestMarketplace::bundledDefaultLoadsOffline()
{
    QTemporaryDir dir;
    MarketplaceManifest manifest(nullptr, m_transport, dir.path());
    QVERIFY(!manifest.pages().isEmpty());
    QVERIFY(!manifest.source("drift-assets").isEmpty());
    QVERIFY(manifest.source("stock.video").isEmpty());
    QVERIFY(!manifest.page("assets").isEmpty());
    QVERIFY(manifest.page("community").isEmpty());
    QCOMPARE(manifest.feature("nope", 7).toInt(), 7);
}

void TestMarketplace::cacheServedWhenOffline()
{
    QTemporaryDir dir;
    QFile cache(dir.filePath("manifest.json"));
    QVERIFY(cache.open(QIODevice::WriteOnly));
    cache.write(QJsonDocument(manifestJson()).toJson());
    cache.close();
    m_server.handler = [](const FakeRequest &) { return FakeServer::reply(503, "{}"); };

    MarketplaceManifest manifest(nullptr, m_transport, dir.path());
    QCOMPARE(manifest.pages().size(), 1);
    QVERIFY(manifest.feature("community").toBool());
    QSignalSpy done(&manifest, &MarketplaceManifest::loadingChanged);
    manifest.refresh();
    QTRY_VERIFY(!manifest.loading() && done.size() >= 2);
    QVERIFY(manifest.offline());
    QVERIFY(!manifest.error().isEmpty());
    QCOMPARE(manifest.pages().size(), 1);
}

void TestMarketplace::etagNotModifiedKeepsCache()
{
    QTemporaryDir dir;
    QFile cache(dir.filePath("manifest.json"));
    QVERIFY(cache.open(QIODevice::WriteOnly));
    cache.write(QJsonDocument(manifestJson()).toJson());
    cache.close();
    QFile tag(dir.filePath("manifest.etag"));
    QVERIFY(tag.open(QIODevice::WriteOnly));
    tag.write("W/\"abc\"");
    tag.close();
    m_server.handler = [](const FakeRequest &r) {
        return r.headers.value("if-none-match") == "W/\"abc\"" ? FakeServer::reply(304, {})
                                                                 : FakeServer::reply(200, "{}");
    };

    MarketplaceManifest manifest(nullptr, m_transport, dir.path());
    const int revision = manifest.revision();
    manifest.refresh();
    QTRY_VERIFY(!manifest.loading());
    QTRY_COMPARE(m_server.requests.size(), 1);
    QVERIFY(!manifest.offline());
    QVERIFY(manifest.error().isEmpty());
    QCOMPARE(manifest.pages().size(), 1);
    QVERIFY(manifest.revision() >= revision);
}

void TestMarketplace::okReplacesAndWritesCache()
{
    QTemporaryDir dir;
    QJsonObject fresh = manifestJson();
    fresh["pages"] = QJsonDocument::fromJson(R"([
      {"id":"one","title":"One","sections":[{"id":"n","type":"notice","text":"x"}]},
      {"id":"two","title":"Two","sections":[{"id":"n","type":"notice","text":"y"}]}])").array();
    m_server.handler = [fresh](const FakeRequest &) {
        return FakeServer::json(200, fresh, "ETag: W/\"v2\"\r\n");
    };

    MarketplaceManifest manifest(nullptr, m_transport, dir.path());
    QVERIFY(manifest.pages().size() != 2);
    manifest.refresh();
    QTRY_COMPARE(manifest.pages().size(), 2);
    QVERIFY(!manifest.offline());
    QVERIFY(QFile::exists(dir.filePath("manifest.json")));
    QFile tag(dir.filePath("manifest.etag"));
    QVERIFY(tag.open(QIODevice::ReadOnly));
    QCOMPARE(tag.readAll(), QByteArray("W/\"v2\""));

    MarketplaceManifest again(nullptr, m_transport, dir.path());
    QCOMPARE(again.pages().size(), 2);
}

void TestMarketplace::newerSchemaFallsBackToBundled()
{
    QTemporaryDir dir;
    QJsonObject future = manifestJson();
    future["schema"] = 9;
    m_server.handler = [future](const FakeRequest &) { return FakeServer::json(200, future); };
    MarketplaceManifest manifest(nullptr, m_transport, dir.path());
    const int bundled = manifest.pages().size();
    manifest.refresh();
    QTRY_VERIFY(!manifest.loading() && !m_server.requests.isEmpty());
    QCOMPARE(manifest.pages().size(), bundled);
    QVERIFY(!QFile::exists(dir.filePath("manifest.json")));
}

void TestMarketplace::signingMatchesIdentity()
{
    QByteArray body = R"({"a":1})";
    m_server.handler = [](const FakeRequest &) { return FakeServer::reply(200, "{}"); };
    const Query query{{"q", "a b"}};
    QNetworkReply *reply = m_transport->post(MarketTransport::apiUrl("/x", query), body);
    QSignalSpy spy(reply, &QNetworkReply::finished);
    QVERIFY(spy.wait());
    reply->deleteLater();

    QCOMPARE(m_server.requests.size(), 1);
    const FakeRequest &r = m_server.requests.first();
    QCOMPARE(r.target, QByteArray("/api/v1/x?q=a%20b"));
    const QString expected = signRequest(hmacKeyBytes(), r.headers["x-cutwire-timestamp"],
                                         r.headers["x-cutwire-nonce"], clientId(), "POST",
                                         QString::fromLatin1(r.target), body);
    QCOMPARE(QString::fromLatin1(r.headers["x-cutwire-signature"]), expected);
    QCOMPARE(QString::fromLatin1(r.headers["x-cutwire-client"]), clientId());
    QCOMPARE(QString::fromLatin1(r.headers["x-cutwire-app"]), appHeader());
    QCOMPARE(QString::fromLatin1(r.headers["x-cutwire-distribution"]), distributionId());
}

void TestMarketplace::clockSkewIsCorrectedOnce()
{
    const qint64 serverNow = QDateTime::currentSecsSinceEpoch() + 3600;
    m_server.handler = [serverNow](const FakeRequest &r) {
        if (qAbs(r.headers["x-cutwire-timestamp"].toLongLong() - serverNow) <= 60)
            return FakeServer::reply(200, "{\"ok\":true}");
        const QByteArray date = QLocale::c()
            .toString(QDateTime::fromSecsSinceEpoch(serverNow, QTimeZone::utc()),
                      u"ddd, dd MMM yyyy HH:mm:ss 'GMT'").toLatin1();
        return FakeServer::json(401, {{"code", "invalid_client"}}, "Date: " + date + "\r\n");
    };
    Call *call = m_transport->send("GET", QStringLiteral("/ping"), {});
    QSignalSpy spy(call, &Call::finished);
    QVERIFY(spy.wait());
    const Response response = qvariant_cast<Response>(spy.first().first());
    QCOMPARE(response.status, 200);
    QCOMPARE(m_server.requests.size(), 2);
    QVERIFY(qAbs(m_transport->clockOffsetSecs() - 3600) < 5);
}

void TestMarketplace::problemParsing()
{
    QString code;
    QString reason;
    bool retryable = true;
    const QString msg = MarketTransport::parseProblem(
        R"({"code":"not_found","detail":"Gone."})", 404, &code, &reason, &retryable);
    QCOMPARE(msg, QStringLiteral("Gone."));
    QCOMPARE(code, QStringLiteral("not_found"));
    QVERIFY(!retryable);
    MarketTransport::parseProblem({}, 429, &code);
    QCOMPARE(code, QStringLiteral("rate_limited"));
}

void TestMarketplace::browserPagesWithCursor()
{
    m_server.handler = [](const FakeRequest &r) {
        const QUrlQuery q(QUrl(QString::fromLatin1(r.target)));
        const bool second = q.queryItemValue("cursor") == "c2";
        QJsonObject page;
        page["items"] = QJsonArray{QJsonObject{{"id", second ? "b" : "a"}, {"title", "T"}}};
        if (!second)
            page["nextCursor"] = "c2";
        page["warnings"] = QJsonArray{"prov:rate_limited"};
        return FakeServer::json(200, page);
    };
    MarketSourceBrowser browser;
    browser.setTransport(m_transport);
    browser.setSource("community.effect");
    browser.setCategory("blur");
    browser.setLimit(2);
    QTRY_VERIFY(browser.items().size() == 1);
    QVERIFY(browser.hasMore());
    QCOMPARE(browser.warnings(), QStringList{"prov:rate_limited"});
    QVERIFY(m_server.requests.first().target.startsWith("/api/v1/marketplace/sources/community.effect/items?"));
    const QUrlQuery first(QUrl(QString::fromLatin1(m_server.requests.first().target)));
    QCOMPARE(first.queryItemValue("category"), QStringLiteral("blur"));
    QCOMPARE(first.queryItemValue("limit"), QStringLiteral("2"));
    QVERIFY(!first.hasQueryItem("cursor"));

    browser.loadMore();
    QTRY_COMPARE(browser.items().size(), 2);
    QVERIFY(!browser.hasMore());
    QCOMPARE(browser.items().at(1).toMap().value("id").toString(), QStringLiteral("b"));
    const QUrlQuery second(QUrl(QString::fromLatin1(m_server.requests.last().target)));
    QCOMPARE(second.queryItemValue("cursor"), QStringLiteral("c2"));
}

void TestMarketplace::browserDebouncesAndCancels()
{
    m_server.handler = [](const FakeRequest &) {
        return FakeServer::json(200, {{"items", QJsonArray{QJsonObject{{"id", "a"}}}}});
    };
    MarketSourceBrowser browser;
    browser.setTransport(m_transport);
    browser.setSource("sfx");
    browser.setQuery("d");
    browser.setQuery("dr");
    browser.setQuery("dru");
    QTest::qWait(60);
    QVERIFY(m_server.requests.isEmpty());
    QTRY_COMPARE(browser.items().size(), 1);
    QCOMPARE(m_server.requests.size(), 1);

    browser.setQuery("drum");
    browser.cancel();
    QTest::qWait(MarketSourceBrowser::kDebounceMs + 100);
    QCOMPARE(m_server.requests.size(), 1);
    QVERIFY(!browser.loading());
}

void TestMarketplace::browserEncodesQuery()
{
    m_server.handler = [](const FakeRequest &) { return FakeServer::json(200, {}); };
    MarketSourceBrowser browser;
    browser.setTransport(m_transport);
    browser.setSource("stock.video");
    browser.setQuery("a+b&c%41 é");
    browser.setFilters({{"orientation", "wide"}});
    QTRY_VERIFY(!m_server.requests.isEmpty());
    const QByteArray target = m_server.requests.first().target;
    QVERIFY2(target.contains("q=a%2Bb%26c%2541%20%C3%A9"), target.constData());
    QVERIFY(target.contains("orientation=wide"));
}

void TestMarketplace::browserReportsErrors()
{
    m_server.handler = [](const FakeRequest &) {
        return FakeServer::json(404, {{"code", "unknown_source"}, {"detail", "No such source."}});
    };
    MarketSourceBrowser browser;
    browser.setTransport(m_transport);
    browser.setSource("nope");
    QTRY_VERIFY(!browser.error().isEmpty());
    QCOMPARE(browser.errorCode(), QStringLiteral("unknown_source"));
    QCOMPARE(browser.error(), QStringLiteral("No such source."));
    QVERIFY(browser.items().isEmpty());
}

QTEST_MAIN(TestMarketplace)
#include "tst_marketplace.moc"
