#include "engine/packages/animations/AnimationCatalog.h"
#include "engine/packages/animations/AnimationPackageLoader.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

namespace {

void copyDir(const QString &from, const QString &to)
{
    QDir().mkpath(to);
    for (const QFileInfo &info : QDir(from).entryInfoList(QDir::Files))
        QVERIFY(QFile::copy(info.absoluteFilePath(), QDir(to).filePath(info.fileName())));
}

} // namespace

class TstAnimationPackage : public QObject
{
    Q_OBJECT
private slots:
    void sampleLoads()
    {
        const QString dir = QString::fromUtf8(DRIFT_TEST_ANIMATIONS_DIR) + "/forge_sample_dissolve";
        QString error;
        const AnimationPresetEntry entry = AnimationPackageLoader::loadPackage(dir, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(entry.meta.id, QString("forge_sample_dissolve"));
        QVERIFY(entry.gpu.valid);
        QCOMPARE(entry.directions, QStringList({"in", "out"}));
        QCOMPARE(entry.defaultDurationMs, 700);
        QCOMPARE(entry.meta.parameters.size(), 2);
        QCOMPARE(entry.meta.parameters.at(0).key, QString("softness"));
        QCOMPARE(entry.meta.parameters.at(1).key, QString("scale"));
        QCOMPARE(entry.meta.parameters.at(1).defaultValue, 12.0);
        QCOMPARE(entry.gpu.passes.size(), 1);

        const QMap<QString, QVariant> params = resolvedAnimationParameters({{"scale", 30.0}}, entry);
        QCOMPARE(params.value("scale").toDouble(), 30.0);
        QCOMPARE(params.value("softness").toDouble(), 0.1);
    }

    void catalogFindsSample()
    {
        reloadAnimationCatalog({QString::fromUtf8(DRIFT_TEST_ANIMATIONS_DIR)});
        QVERIFY(animationDefForId("forge_sample_dissolve") != nullptr);
        QVERIFY(animationDefForId("slideUp") == nullptr);
    }

    void directionsAreRead()
    {
        QTemporaryDir tmp;
        const QString dir = tmp.path() + "/pkg";
        copyDir(QString::fromUtf8(DRIFT_TEST_ANIMATIONS_DIR) + "/forge_sample_dissolve", dir);
        QFile f(dir + "/animation.json");
        QVERIFY(f.open(QIODevice::ReadOnly));
        QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
        root.insert("directions", QJsonArray{"out"});
        root.remove("defaultDurationMs");
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(QJsonDocument(root).toJson());
        f.close();

        QString error;
        const AnimationPresetEntry entry = AnimationPackageLoader::loadPackage(dir, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(entry.directions, QStringList({"out"}));
        QCOMPARE(entry.defaultDurationMs, 500);
    }

    void secondSourceIsRejected()
    {
        QTemporaryDir tmp;
        const QString dir = tmp.path() + "/pkg";
        copyDir(QString::fromUtf8(DRIFT_TEST_ANIMATIONS_DIR) + "/forge_sample_dissolve", dir);
        QFile f(dir + "/animation.json");
        QVERIFY(f.open(QIODevice::ReadOnly));
        QByteArray json = f.readAll();
        f.close();
        json.replace("\"index\": 0", "\"index\": 1");
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(json);
        f.close();

        QString error;
        AnimationPackageLoader::loadPackage(dir, &error);
        QVERIFY(!error.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TstAnimationPackage)
#include "tst_animationpackage.moc"
