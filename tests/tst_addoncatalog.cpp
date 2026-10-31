#include "app/online/addons/AddonCatalog.h"

#include <QJsonDocument>
#include <QtTest>

using drift::addon::AddonCatalog;

namespace {

AddonCatalog make(const QByteArray &json)
{
    return AddonCatalog::parse(QJsonDocument::fromJson(json).object());
}

const QByteArray kCatalog = R"({
  "schema": 1, "revision": "abc", "futureField": {"x": 1},
  "categories": [{"id": "ai", "title": "AI", "order": 1, "surprise": true}],
  "collections": [{"id": "start", "title": "Start", "addons": ["a"]}],
  "entries": [
    {"id": "model.a", "tagline": "A", "somethingNew": 1,
     "requires": [{"kind": "runtime",
                   "prefer": {"linux-x64": "rt.cpu.linux-x64", "win-x64": "rt.cpu.win-x64"},
                   "default": "rt.cpu.default", "reason": "needs engine"}]},
    {"id": "model.b",
     "requires": [{"kind": "runtime", "prefer": {}, "default": null}]},
    {"id": "model.c", "requires": [{"id": "model.a", "reason": "uses a"}]},
    {"id": "model.d", "requires": [{"id": "ghost"}]},
    {"id": "loop.x", "requires": [{"id": "loop.y"}]},
    {"id": "loop.y", "requires": [{"id": "loop.x"}]},
    {"match": "rt.cpu.*", "tagline": "Engine"},
    {"match": "rt.*", "tagline": "Any engine"}
  ]
})";

QList<AddonCatalog::IndexRow> rows()
{
    return {
        {QStringLiteral("model.a"), {QStringLiteral("model")}},
        {QStringLiteral("model.b"), {QStringLiteral("model")}},
        {QStringLiteral("model.c"), {QStringLiteral("model")}},
        {QStringLiteral("model.d"), {QStringLiteral("model")}},
        {QStringLiteral("loop.x"), {QStringLiteral("loop")}},
        {QStringLiteral("loop.y"), {QStringLiteral("loop")}},
        {QStringLiteral("rt.gpu.linux-x64"), {QStringLiteral("runtime")}},
        {QStringLiteral("rt.cpu.default"), {QStringLiteral("runtime")}},
        {QStringLiteral("rt.cpu.linux-x64"), {QStringLiteral("runtime")}},
    };
}

} // namespace

class TestAddonCatalog : public QObject
{
    Q_OBJECT
private slots:
    void parseIgnoresUnknownFields();
    void newerSchemaIsInvalid();
    void entryMatchesIdThenGlob();
    void kindSatisfiedByInstalled();
    void kindSatisfiedByRequestedProvider();
    void prefersPlatformBuild();
    void fallsBackToDefaultThenFirst();
    void transitiveDedupeAndOrder();
    void cyclesTerminate();
    void missingRequirements();
};

void TestAddonCatalog::parseIgnoresUnknownFields()
{
    const AddonCatalog catalog = make(kCatalog);
    QVERIFY(catalog.valid());
    QCOMPARE(catalog.revision(), QStringLiteral("abc"));
    QCOMPARE(catalog.categories().size(), 1);
    QCOMPARE(catalog.collections().size(), 1);
    QCOMPARE(catalog.entryFor(QStringLiteral("model.a")).value(QStringLiteral("tagline")).toString(),
             QStringLiteral("A"));
}

void TestAddonCatalog::newerSchemaIsInvalid()
{
    QVERIFY(!make(R"({"schema": 2, "entries": []})").valid());
    QVERIFY(!make(R"({"entries": []})").valid());
    QVERIFY(!make(R"({"schema": 1})").valid());
    QVERIFY(make(R"({"schema": 1, "entries": []})").valid());
}

void TestAddonCatalog::entryMatchesIdThenGlob()
{
    const AddonCatalog catalog = make(kCatalog);
    QCOMPARE(catalog.entryFor(QStringLiteral("rt.cpu.win-x64")).value(QStringLiteral("tagline")).toString(),
             QStringLiteral("Engine"));
    QCOMPARE(catalog.entryFor(QStringLiteral("rt.gpu.win-x64")).value(QStringLiteral("tagline")).toString(),
             QStringLiteral("Any engine"));
    QVERIFY(catalog.entryFor(QStringLiteral("rtXcpu")).isEmpty());
    QVERIFY(catalog.entryFor(QStringLiteral("unknown")).isEmpty());
}

void TestAddonCatalog::kindSatisfiedByInstalled()
{
    const AddonCatalog catalog = make(kCatalog);
    const auto plan = catalog.resolve({QStringLiteral("model.a")}, rows(), {QStringLiteral("rt.gpu.linux-x64")},
                                      {QStringLiteral("runtime")}, QStringLiteral("linux-x64"));
    QCOMPARE(plan.ids, QStringList{QStringLiteral("model.a")});
    QVERIFY(plan.added.isEmpty());
    QVERIFY(plan.missing.isEmpty());
}

void TestAddonCatalog::kindSatisfiedByRequestedProvider()
{
    const AddonCatalog catalog = make(kCatalog);
    const auto plan = catalog.resolve({QStringLiteral("model.a"), QStringLiteral("rt.gpu.linux-x64")}, rows(), {}, {},
                                      QStringLiteral("linux-x64"));
    QCOMPARE(plan.ids, (QStringList{QStringLiteral("rt.gpu.linux-x64"), QStringLiteral("model.a")}));
    QVERIFY(plan.added.isEmpty());
    QCOMPARE(plan.needs.value(QStringLiteral("model.a")), QStringList{QStringLiteral("rt.gpu.linux-x64")});
}

void TestAddonCatalog::prefersPlatformBuild()
{
    const AddonCatalog catalog = make(kCatalog);
    const auto plan = catalog.resolve({QStringLiteral("model.a")}, rows(), {}, {}, QStringLiteral("linux-x64"));
    QCOMPARE(plan.ids, (QStringList{QStringLiteral("rt.cpu.linux-x64"), QStringLiteral("model.a")}));
    QCOMPARE(plan.added.size(), 1);
    QCOMPARE(plan.added.first().id, QStringLiteral("rt.cpu.linux-x64"));
    QCOMPARE(plan.added.first().forId, QStringLiteral("model.a"));
    QCOMPARE(plan.added.first().reason, QStringLiteral("needs engine"));
}

void TestAddonCatalog::fallsBackToDefaultThenFirst()
{
    const AddonCatalog catalog = make(kCatalog);
    // win-x64 prefers a build the index does not list on this platform: use default.
    const auto viaDefault = catalog.resolve({QStringLiteral("model.a")}, rows(), {}, {}, QStringLiteral("win-x64"));
    QCOMPARE(viaDefault.ids.first(), QStringLiteral("rt.cpu.default"));
    // model.b names neither: the first row providing the kind.
    const auto viaFirst = catalog.resolve({QStringLiteral("model.b")}, rows(), {}, {}, QStringLiteral("linux-x64"));
    QCOMPARE(viaFirst.ids.first(), QStringLiteral("rt.gpu.linux-x64"));
}

void TestAddonCatalog::transitiveDedupeAndOrder()
{
    const AddonCatalog catalog = make(kCatalog);
    const auto plan = catalog.resolve({QStringLiteral("model.c"), QStringLiteral("model.a"), QStringLiteral("model.b")},
                                      rows(), {}, {}, QStringLiteral("linux-x64"));
    QCOMPARE(plan.ids, (QStringList{QStringLiteral("rt.cpu.linux-x64"), QStringLiteral("model.a"),
                                    QStringLiteral("model.c"), QStringLiteral("model.b")}));
    QCOMPARE(plan.added.size(), 1);
    QCOMPARE(plan.needs.value(QStringLiteral("model.c")), QStringList{QStringLiteral("model.a")});
    QCOMPARE(plan.needs.value(QStringLiteral("model.b")), QStringList{QStringLiteral("rt.cpu.linux-x64")});
}

void TestAddonCatalog::cyclesTerminate()
{
    const AddonCatalog catalog = make(kCatalog);
    const auto plan = catalog.resolve({QStringLiteral("loop.x")}, rows(), {}, {}, QStringLiteral("linux-x64"));
    QCOMPARE(plan.ids.size(), 2);
    QVERIFY(plan.ids.contains(QStringLiteral("loop.x")));
    QVERIFY(plan.ids.contains(QStringLiteral("loop.y")));
}

void TestAddonCatalog::missingRequirements()
{
    const AddonCatalog catalog = make(kCatalog);
    const auto ghost = catalog.resolve({QStringLiteral("model.d")}, rows(), {}, {}, QStringLiteral("linux-x64"));
    QCOMPARE(ghost.ids, QStringList{QStringLiteral("model.d")});
    QCOMPARE(ghost.missing.size(), 1);
    QCOMPARE(ghost.missing.first().forId, QStringLiteral("model.d"));
    QCOMPARE(ghost.missing.first().requirement, QStringLiteral("ghost"));

    const auto noRuntime = catalog.resolve({QStringLiteral("model.a")}, {{QStringLiteral("model.a"), {QStringLiteral("model")}}},
                                           {}, {}, QStringLiteral("linux-x64"));
    QCOMPARE(noRuntime.missing.size(), 1);
    QCOMPARE(noRuntime.missing.first().requirement, QStringLiteral("runtime"));
}

QTEST_APPLESS_MAIN(TestAddonCatalog)
#include "tst_addoncatalog.moc"
