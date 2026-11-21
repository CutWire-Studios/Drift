#include <QtTest>

#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include <iostream>

#include "app/commandbar/CommandCatalog.h"
#include "app/commandbar/CommandPrompt.h"
#include "app/commandbar/CommandRetriever.h"
#include "engine/ml/llm/LlamaServer.h"

using namespace drift::commandbar;

namespace {

QJsonArray loadArray(const QString &name)
{
    QFile f(QStringLiteral(DRIFT_TEST_DATA_DIR "/commandbar/") + name);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).array();
}

CommandContext contextOf(const QJsonObject &c)
{
    CommandContext ctx;
    ctx.playhead = c.value(QStringLiteral("playhead")).toDouble();
    const QJsonObject s = c.value(QStringLiteral("selected")).toObject();
    ctx.hasSelection = !s.isEmpty();
    ctx.clip = s.value(QStringLiteral("clip")).toString();
    ctx.track = s.value(QStringLiteral("track")).toInt();
    ctx.index = s.value(QStringLiteral("index")).toInt();
    return ctx;
}

QStringList strings(const QJsonArray &a)
{
    QStringList out;
    for (const QJsonValue &v : a)
        out.append(v.toString());
    return out;
}

QString firstDifference(const QString &a, const QString &b)
{
    int i = 0;
    while (i < a.size() && i < b.size() && a.at(i) == b.at(i))
        ++i;
    return QStringLiteral("first difference at %1: got ...%2 | want ...%3").arg(i).arg(a.mid(qMax(0, i - 40), 100), b.mid(qMax(0, i - 40), 100));
}

// eval_needle's lenient comparison: gold must be contained in the prediction, strings compare
// case-insensitively, numbers to 1e-6.
bool contains(const QJsonValue &gold, const QJsonValue &got)
{
    if (gold.isObject()) {
        if (!got.isObject())
            return false;
        const QJsonObject g = gold.toObject(), p = got.toObject();
        for (auto it = g.begin(); it != g.end(); ++it) {
            if (!p.contains(it.key()) || !contains(it.value(), p.value(it.key())))
                return false;
        }
        return true;
    }
    if (gold.isArray()) {
        const QJsonArray g = gold.toArray(), p = got.toArray();
        if (!got.isArray() || g.size() != p.size())
            return false;
        for (int i = 0; i < g.size(); ++i) {
            if (!contains(g.at(i), p.at(i)))
                return false;
        }
        return true;
    }
    if (gold.isString())
        return got.isString() && gold.toString().trimmed().toLower() == got.toString().trimmed().toLower();
    if (gold.isDouble())
        return got.isDouble() && qAbs(gold.toDouble() - got.toDouble()) < 1e-6;
    return gold == got;
}

bool exactMatch(const QJsonArray &gold, const ParsedCalls &got)
{
    if (gold.size() != got.calls.size())
        return false;
    for (int i = 0; i < gold.size(); ++i) {
        const QJsonObject g = gold.at(i).toObject();
        if (g.value(QStringLiteral("name")).toString() != got.calls.at(i).name
            || !contains(g.value(QStringLiteral("arguments")), got.calls.at(i).arguments))
            return false;
    }
    return true;
}

} // namespace

class CommandBarTest : public QObject
{
    Q_OBJECT

private slots:
    void contextLine();
    void toolJsonParity();
    void promptParity();
    void retrievalParity();
    void parserCases();
    void liveCatalogMatchesSnapshot();
    void liveModel();
};

void CommandBarTest::contextLine()
{
    CommandContext c;
    c.playhead = 18;
    QCOMPARE(commandContextLine(c), QStringLiteral("No clip is selected. Playhead: 18.0 seconds."));
    c.hasSelection = true;
    c.clip = QStringLiteral("259955bd-f569-48ee-81b0-b7bb4f2cba16");
    c.track = 1;
    c.index = 3;
    c.playhead = 7.254;
    QCOMPARE(commandContextLine(c),
             QStringLiteral("Selected clip: 259955bd-f569-48ee-81b0-b7bb4f2cba16, track 1, index 3. Playhead: 7.25 seconds."));
}

void CommandBarTest::toolJsonParity()
{
    const CommandCatalog catalog = CommandCatalog::fromJson(loadArray(QStringLiteral("ops_snapshot.json")));
    QVERIFY(catalog.ops().size() > 300);
    const QJsonArray items = loadArray(QStringLiteral("prompts.json"));
    QVERIFY(!items.isEmpty());
    int checked = 0;
    for (const QJsonValue &v : items) {
        const QJsonObject item = v.toObject();
        const QStringList names = strings(item.value(QStringLiteral("tools")).toArray());
        QStringList want = strings(item.value(QStringLiteral("toolJson")).toArray());
        for (QString &w : want)
            w = QStringLiteral("{\"type\": \"function\", \"function\": ") + w + QLatin1Char('}');
        for (int i = 0; i < names.size(); ++i) {
            const CommandOp *op = catalog.find(names.at(i));
            QVERIFY2(op, qPrintable(names.at(i)));
            const QString got = commandToolJson(*op);
            QVERIFY2(got == want.at(i), qPrintable(names.at(i) + QLatin1Char(' ') + firstDifference(got, want.at(i))));
            ++checked;
        }
    }
    std::cerr << "tool entries checked: " << checked << "\n";
}

void CommandBarTest::promptParity()
{
    const CommandCatalog catalog = CommandCatalog::fromJson(loadArray(QStringLiteral("ops_snapshot.json")));
    const QJsonArray items = loadArray(QStringLiteral("prompts.json"));
    int pass = 0;
    for (const QJsonValue &v : items) {
        const QJsonObject item = v.toObject();
        const QString got = commandPrompt(item.value(QStringLiteral("query")).toString(),
                                          contextOf(item.value(QStringLiteral("context")).toObject()),
                                          catalog.pick(strings(item.value(QStringLiteral("tools")).toArray())));
        const QString want = item.value(QStringLiteral("prompt")).toString();
        QVERIFY2(got == want, qPrintable(item.value(QStringLiteral("id")).toString() + QLatin1Char(' ') + firstDifference(got, want)));
        ++pass;
    }
    std::cerr << "prompt parity: " << pass << "/" << items.size() << "\n";
    QCOMPARE(pass, int(items.size()));
}

void CommandBarTest::retrievalParity()
{
    const CommandCatalog catalog = CommandCatalog::fromJson(loadArray(QStringLiteral("ops_snapshot.json")));
    const CommandRetriever retriever(catalog.ops());
    const QJsonArray cases = loadArray(QStringLiteral("retrieval.json"));
    QVERIFY(!cases.isEmpty());
    int top10 = 0, top30 = 0;
    for (const QJsonValue &v : cases) {
        const QJsonObject c = v.toObject();
        const QString query = c.value(QStringLiteral("query")).toString();
        QCOMPARE(CommandRetriever::tokens(CommandRetriever::expand(query)), strings(c.value(QStringLiteral("tokens")).toArray()));
        const QStringList want = strings(c.value(QStringLiteral("top")).toArray());
        const QStringList got = retriever.rank(query).mid(0, want.size());
        if (got.mid(0, 10) == want.mid(0, 10))
            ++top10;
        else
            std::cerr << "top-10 mismatch for \"" << qPrintable(query) << "\"\n  got  " << qPrintable(got.mid(0, 10).join(' '))
                      << "\n  want " << qPrintable(want.mid(0, 10).join(' ')) << "\n";
        if (got == want)
            ++top30;
    }
    std::cerr << "retrieval: top-10 exact " << top10 << "/" << cases.size() << ", top-30 exact " << top30 << "/" << cases.size() << "\n";
    QCOMPARE(top10, int(cases.size()));
    QCOMPARE(top30, int(cases.size()));
}

void CommandBarTest::parserCases()
{
    const QJsonArray cases = loadArray(QStringLiteral("parser.json"));
    QVERIFY(!cases.isEmpty());
    for (const QJsonValue &v : cases) {
        const QJsonObject c = v.toObject();
        const QString text = c.value(QStringLiteral("text")).toString();
        const ParsedCalls got = parseCommandCalls(text);
        const QJsonArray want = c.value(QStringLiteral("calls")).toArray();
        QVERIFY2(got.refusal == c.value(QStringLiteral("refusal")).toBool(), qPrintable(text));
        QVERIFY2(got.calls.size() == want.size(), qPrintable(text));
        for (int i = 0; i < want.size(); ++i) {
            QCOMPARE(got.calls.at(i).name, want.at(i).toObject().value(QStringLiteral("name")).toString());
            QCOMPARE(QJsonDocument(got.calls.at(i).arguments).toJson(QJsonDocument::Compact),
                     QJsonDocument(want.at(i).toObject().value(QStringLiteral("arguments")).toObject()).toJson(QJsonDocument::Compact));
        }
    }
}

void CommandBarTest::liveCatalogMatchesSnapshot()
{
    const CommandCatalog snapshot = CommandCatalog::fromJson(loadArray(QStringLiteral("ops_snapshot.json")));
    const CommandCatalog live = CommandCatalog::live();
    QVERIFY(live.ops().size() > 100);
    QSet<QString> used;
    for (const QJsonValue &v : loadArray(QStringLiteral("prompts.json"))) {
        for (const QString &n : strings(v.toObject().value(QStringLiteral("tools")).toArray()))
            used.insert(n);
    }
    // Warn-only: Drift's catalog moves on after the model's training snapshot; the point is to see how far.
    int drifted = 0;
    for (const QString &name : used) {
        const CommandOp *a = snapshot.find(name);
        const CommandOp *b = live.find(name);
        if (!b) {
            std::cerr << "WARN live catalog lacks op " << qPrintable(name) << "\n";
            ++drifted;
            continue;
        }
        if (a->description != b->description || commandToolJson(*a) != commandToolJson(*b)) {
            std::cerr << "WARN op " << qPrintable(name) << " differs from the training snapshot"
                      << (a->description != b->description ? " (description)" : "")
                      << (commandToolJson(*a) != commandToolJson(*b) ? " (compact schema)" : "") << "\n";
            ++drifted;
        }
    }
    std::cerr << "live vs snapshot: " << drifted << "/" << used.size() << " fixture ops differ; live has " << live.ops().size()
              << " ops, snapshot " << snapshot.ops().size() << "\n";
    // Retrieval over the live catalog must at least run and answer something.
    QVERIFY(!CommandRetriever(live.ops()).top(QStringLiteral("split here")).isEmpty());
}

void CommandBarTest::liveModel()
{
    const QString model = qEnvironmentVariable("DRIFT_COMMANDBAR_GGUF");
    if (model.isEmpty() || qEnvironmentVariableIsEmpty("DRIFT_LLAMA_RUNTIME"))
        QSKIP("set DRIFT_COMMANDBAR_GGUF and DRIFT_LLAMA_RUNTIME to run the end-to-end model test");
    QVERIFY(drift::llm::LlamaServer::available());

    const CommandCatalog catalog = CommandCatalog::fromJson(loadArray(QStringLiteral("ops_snapshot.json")));
    drift::llm::LlamaServer server;
    QString failure;
    connect(&server, &drift::llm::LlamaServer::failed, this, [&](const QString &r) { failure = r; });
    server.setIdleTimeoutSeconds(0);
    server.start(model);

    int exact = 0, total = 0;
    for (const QJsonValue &v : loadArray(QStringLiteral("prompts.json"))) {
        const QJsonObject item = v.toObject();
        const QJsonArray gold = item.value(QStringLiteral("answers")).toArray();
        if (gold.isEmpty())
            continue;
        if (total == 10)
            break;
        ++total;
        const QString prompt = commandPrompt(item.value(QStringLiteral("query")).toString(),
                                             contextOf(item.value(QStringLiteral("context")).toObject()),
                                             catalog.pick(strings(item.value(QStringLiteral("tools")).toArray())));
        drift::llm::Completion result;
        QEventLoop loop;
        QTimer::singleShot(180000, &loop, &QEventLoop::quit);
        server.complete(prompt, 512, [&](const drift::llm::Completion &c) {
            result = c;
            loop.quit();
        });
        loop.exec();
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        QVERIFY2(result.ok, qPrintable(result.error));
        const ParsedCalls parsed = parseCommandCalls(result.content);
        const bool ok = exactMatch(gold, parsed);
        exact += ok;
        std::cerr << (ok ? "EXACT " : "MISS  ") << qPrintable(item.value(QStringLiteral("query")).toString()) << " -> "
                  << qPrintable(result.content.simplified().left(160)) << "  [" << result.totalMs << " ms]\n";
    }
    server.stop();
    std::cerr << "live model: " << exact << "/" << total << " exact\n";
    QVERIFY2(exact >= 7, qPrintable(QString::number(exact) + QStringLiteral("/10 exact")));
}

QTEST_MAIN(CommandBarTest)
#include "tst_commandbar.moc"
