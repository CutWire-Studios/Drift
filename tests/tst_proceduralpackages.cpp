#include "engine/packages/procedurals/ProceduralCatalog.h"
#include "engine/packages/procedurals/ProceduralPackageLoader.h"
#include "engine/procedural/ProceduralEvaluator.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

namespace {

void writeFile(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}

const char *kManifest = R"({
  "id": "test.waves", "displayName": "Waves", "category": "Test",
  "parameters": [ { "identifier": "amount", "type": "float", "min": 0, "max": 10, "default": 2 } ]
})";

void makePackage(const QString &dir, const QByteArray &manifest = kManifest)
{
    writeFile(dir + "/procedural.json", manifest);
    writeFile(dir + "/main.lua", "scene:add(geo.box(1,1,1), 'main')");
    writeFile(dir + "/main.vert", "#version 330 core\nvoid main(){}");
    writeFile(dir + "/main.frag", "#version 330 core\nvoid main(){}");
}

} // namespace

class TstProceduralPackages : public QObject
{
    Q_OBJECT
private slots:
    void validPackageLoads()
    {
        QTemporaryDir tmp;
        makePackage(tmp.path() + "/waves");
        QString err;
        const auto pkg = ProceduralPackageLoader::loadPackage(tmp.path() + "/waves", &err);
        QVERIFY2(pkg.valid, qPrintable(err));
        QCOMPARE(pkg.id, QString("test.waves"));
        QCOMPARE(pkg.displayName, QString("Waves"));
        QCOMPARE(pkg.category, QString("test"));
        QCOMPARE(pkg.parameters.size(), 1);
        QCOMPARE(pkg.parameters.at(0).key, QString("amount"));
        QCOMPARE(pkg.parameters.at(0).defaultValue, 2.0);
        QVERIFY(pkg.luaSource.contains("scene:add"));
        QCOMPARE(pkg.shaders.size(), 1);
        QVERIFY(pkg.shaders.contains("main"));
        QVERIFY(!pkg.shaders["main"].vert.isEmpty());
        QVERIFY(!pkg.shaders["main"].frag.isEmpty());
        QCOMPARE(pkg.sourceHash.size(), 64);
        const auto budgets = drift::ProceduralBudgets::forPlatform();
        QCOMPARE(pkg.maxVertices, budgets.maxVertices);
        QCOMPARE(pkg.maxInstances, budgets.maxInstances);
    }

    void missingPartnerRejected_data()
    {
        QTest::addColumn<QString>("remove");
        QTest::newRow("no frag") << "main.frag";
        QTest::newRow("no vert") << "main.vert";
    }
    void missingPartnerRejected()
    {
        QFETCH(QString, remove);
        QTemporaryDir tmp;
        const QString dir = tmp.path() + "/p";
        makePackage(dir);
        writeFile(dir + "/other.vert", "x");
        writeFile(dir + "/other.frag", "x");
        QVERIFY(QFile::remove(dir + "/" + remove));
        QString err;
        const auto pkg = ProceduralPackageLoader::loadPackage(dir, &err);
        QVERIFY(!pkg.valid);
        QVERIFY2(err.contains("main."), qPrintable(err));
    }

    void missingLuaRejected()
    {
        QTemporaryDir tmp;
        const QString dir = tmp.path() + "/p";
        makePackage(dir);
        QVERIFY(QFile::remove(dir + "/main.lua"));
        QString err;
        QVERIFY(!ProceduralPackageLoader::loadPackage(dir, &err).valid);
        QVERIFY2(err.contains("main.lua"), qPrintable(err));
    }

    void noShadersRejected()
    {
        QTemporaryDir tmp;
        const QString dir = tmp.path() + "/p";
        makePackage(dir);
        QFile::remove(dir + "/main.vert");
        QFile::remove(dir + "/main.frag");
        QVERIFY(!ProceduralPackageLoader::loadPackage(dir).valid);
    }

    void oversizedFileRejected()
    {
        QTemporaryDir tmp;
        const QString dir = tmp.path() + "/p";
        makePackage(dir);
        writeFile(dir + "/main.frag", QByteArray(ProceduralPackageLoader::kMaxShaderBytes + 1, 'a'));
        QString err;
        QVERIFY(!ProceduralPackageLoader::loadPackage(dir, &err).valid);
        QVERIFY2(err.contains("main.frag"), qPrintable(err));

        makePackage(dir);
        writeFile(dir + "/main.lua", QByteArray(ProceduralPackageLoader::kMaxLuaBytes + 1, 'a'));
        QVERIFY(!ProceduralPackageLoader::loadPackage(dir).valid);
    }

    void tooManyShaderPairsRejected()
    {
        QTemporaryDir tmp;
        const QString dir = tmp.path() + "/p";
        makePackage(dir);
        for (int i = 0; i < ProceduralPackageLoader::kMaxShaderPairs; ++i) {
            writeFile(QString("%1/s%2.vert").arg(dir).arg(i), "x");
            writeFile(QString("%1/s%2.frag").arg(dir).arg(i), "x");
        }
        QVERIFY(!ProceduralPackageLoader::loadPackage(dir).valid);
    }

    void sourceHashTracksEveryFile()
    {
        QTemporaryDir tmp;
        const QString dir = tmp.path() + "/p";
        makePackage(dir);
        const QString base = ProceduralPackageLoader::loadPackage(dir).sourceHash;
        QCOMPARE(ProceduralPackageLoader::loadPackage(dir).sourceHash, base);

        for (const QString &file : {"main.lua", "main.vert", "main.frag", "procedural.json"}) {
            makePackage(dir);
            QFile f(dir + "/" + file);
            QVERIFY(f.open(QIODevice::Append));
            f.write("\n ");
            f.close();
            const auto pkg = ProceduralPackageLoader::loadPackage(dir);
            QVERIFY2(pkg.valid, qPrintable(pkg.errorMessage));
            QVERIFY2(pkg.sourceHash != base, qPrintable(file));
        }
    }

    void maxVerticesClamped()
    {
        const auto budgets = drift::ProceduralBudgets::forPlatform();
        QTemporaryDir tmp;
        const QString dir = tmp.path() + "/p";
        makePackage(dir, R"({"id":"a","maxVertices":99999999999,"maxInstances":99999999999})");
        auto pkg = ProceduralPackageLoader::loadPackage(dir);
        QVERIFY(pkg.valid);
        QCOMPARE(pkg.maxVertices, budgets.maxVertices);
        QCOMPARE(pkg.maxInstances, budgets.maxInstances);

        makePackage(dir, R"({"id":"a","maxVertices":1234,"maxInstances":56})");
        pkg = ProceduralPackageLoader::loadPackage(dir);
        QCOMPARE(pkg.maxVertices, std::size_t(1234));
        QCOMPARE(pkg.maxInstances, std::size_t(56));
    }

    void catalogScansAndReloads()
    {
        QTemporaryDir tmp;
        makePackage(tmp.path() + "/a");
        makePackage(tmp.path() + "/broken");
        QFile::remove(tmp.path() + "/broken/main.lua");
        reloadProceduralCatalog({tmp.path()});
        QCOMPARE(proceduralIds(), QStringList{"test.waves"});
        QVERIFY(hasProcedural("test.waves"));
        QCOMPARE(proceduralForId("test.waves").displayName, QString("Waves"));
        QVERIFY(!proceduralForId("nope").valid);

        QTemporaryDir empty;
        reloadProceduralCatalog({empty.path()});
        QVERIFY(proceduralCatalog().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TstProceduralPackages)
#include "tst_proceduralpackages.moc"
