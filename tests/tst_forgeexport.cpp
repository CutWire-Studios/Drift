#include "app/forge/compiler/PackageFiles.h"
#include "app/forge/compiler/Manifest.h"
#include "app/forge/doc/Util.h"

#include <QDir>
#include <QJsonDocument>
#include <QtTest>

using namespace drift::forge;

namespace {

const QString kRoot = QStringLiteral(DRIFT_TEST_DATA_DIR "/forge");

QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

ForgeDoc load(const QString &name)
{
    QString error;
    const ForgeDoc doc = ForgeDoc::fromJsonText(readFile(kRoot + "/" + name + "/forge.json"), &error);
    Q_ASSERT(error.isEmpty());
    return doc;
}

QByteArray fileData(const PackageContents &c, const QString &path)
{
    for (const PackageFile &f : c.files)
        if (f.path == path)
            return f.data;
    return {};
}

}

class TstForgeExport : public QObject
{
    Q_OBJECT

private slots:
    void fileSetPerKind_data()
    {
        QTest::addColumn<QString>("fixture");
        QTest::addColumn<QString>("manifestName");
        QTest::newRow("effect") << "starter_effect_dreamy_glow" << "effect.json";
        QTest::newRow("transition") << "starter_transition_cube" << "transition.json";
        QTest::newRow("animation") << "starter_animation_fade_rise" << "animation.json";
    }

    void fileSetPerKind()
    {
        QFETCH(QString, fixture);
        QFETCH(QString, manifestName);
        const ForgeDoc doc = load(fixture);
        QCOMPARE(packageManifestName(doc), manifestName);
        const CompileResult r = compile(doc);
        QVERIFY(r.ok);
        const PackageContents c = packageContents(doc, r);
        QCOMPARE(c.manifest, packageManifest(doc, r));
        QVERIFY(!c.manifest.isEmpty());

        const QDir exportDir(kRoot + "/" + fixture + "/export");
        for (const QString &frag : exportDir.entryList({"*.frag"}))
            QCOMPARE(fileData(c, frag), readFile(exportDir.filePath(frag)));
        QVERIFY(!exportDir.entryList({"*.frag"}).isEmpty());

        const QByteArray forge = fileData(c, "forge.json");
        QVERIFY(!forge.isEmpty());
        QString error;
        const ForgeDoc again = ForgeDoc::fromJsonText(forge, &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(again.toJson(), doc.toJson());
        QVERIFY(exportProblem(doc).isEmpty());
    }

    void textureAssetsAreDecoded()
    {
        const ForgeDoc doc = load("node_image");
        const CompileResult r = compile(doc);
        QVERIFY(r.ok);
        QCOMPARE(r.textures.size(), 1);
        const PackageContents c = packageContents(doc, r);
        const QByteArray png = fileData(c, "tex0.png");
        QVERIFY(png.startsWith("\x89PNG"));
        QCOMPARE(png, assetBytes(doc, r.textures.at(0).toObject().value("assetId").toString()));
    }

    void exportProblems()
    {
        ForgeDoc doc = emptyDoc(Kind::Effect);
        QVERIFY(exportProblem(doc).isEmpty());
        doc.meta.id = "Bad Id";
        QVERIFY(!exportProblem(doc).isEmpty());
        doc.meta.id = "ok_id.1";
        QVERIFY(exportProblem(doc).isEmpty());
        doc.meta.displayName = "  ";
        QVERIFY(!exportProblem(doc).isEmpty());
        QVERIFY(exportProblem(emptyDoc(Kind::Audio)).isEmpty());
    }

    void idDefaulting()
    {
        ForgeDoc doc = emptyDoc(Kind::Effect, "My Cool Look!");
        QCOMPARE(exportId(doc), doc.meta.id);
        doc.meta.id.clear();
        const QString id = exportId(doc);
        QVERIFY2(QRegularExpression("^forge_my_cool_look_[a-z0-9]{4}$").match(id).hasMatch(), qPrintable(id));
        doc.meta.id = id;
        QVERIFY(exportProblem(doc).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TstForgeExport)
#include "tst_forgeexport.moc"
