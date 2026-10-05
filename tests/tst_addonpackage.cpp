#include "engine/AddonPackage.h"
#include "engine/AddonRegistry.h"
#include "engine/AudioEffectCatalog.h"
#include "engine/GpuPackageParse.h"
#include "engine/audio/AudioEffectRack.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include <QScopeGuard>

#include <cmath>

using namespace drift::addon;

// The fixture is a real package signed with the production key (tests/data/, built by the
// packer in the drift-addons repo). Damaged variants are produced by mutating bytes, so the
// tests exercise the actual trust root rather than a test-only key.

class TestAddonPackage : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void readsManifestWithoutVerifying();
    void installsAndVerifies();
    void rejectsTamperedPayload();
    void rejectsTamperedSignature();
    void rejectsTruncatedFile();
    void rejectsBadMagic();
    void leavesNoStagingBehindOnFailure();
    void reportsCancellation();
    void installedAddonOutranksBundledContent();
    void installsForgeAudioEffectPackage();
    void installsForgeAudioGraphPackage();
    void writesUserPackageThatInstalls();
    void classifiesSignatures();
    void installsUnverifiedWhenAllowed();
    void parsesFolderManifest();

private:
    // Copy the fixture and flip one byte at `offset` (negative counts back from the end).
    QString corruptedCopy(const QString &name, qint64 offset) const;

    QString m_fixture;
    QTemporaryDir m_tmp;
};

void TestAddonPackage::initTestCase()
{
    m_fixture = QStringLiteral(DRIFT_TEST_DATA_DIR "/test.fixture-1.2.3.driftpkg");
    QVERIFY2(QFile::exists(m_fixture), qPrintable(m_fixture));
    QVERIFY(m_tmp.isValid());
}

QString TestAddonPackage::corruptedCopy(const QString &name, qint64 offset) const
{
    const QString path = m_tmp.filePath(name);
    QFile::remove(path);
    if (!QFile::copy(m_fixture, path))
        return {};

    QFile file(path);
    if (!file.open(QIODevice::ReadWrite))
        return {};
    const qint64 pos = offset < 0 ? file.size() + offset : offset;
    file.seek(pos);
    char byte = 0;
    file.read(&byte, 1);
    byte = char(byte ^ 0xff);
    file.seek(pos);
    file.write(&byte, 1);
    file.close();
    return path;
}

void TestAddonPackage::readsManifestWithoutVerifying()
{
    QString error;
    const auto info = readManifest(m_fixture, &error);
    QVERIFY2(info.has_value(), qPrintable(error));
    QCOMPARE(info->id, QStringLiteral("test.fixture"));
    QCOMPARE(info->version, QStringLiteral("1.2.3"));
    QCOMPARE(info->provides.size(), 1);
    QCOMPARE(info->provides.first().kind, QStringLiteral("fonts"));
    QCOMPARE(info->provides.first().root, QStringLiteral("fonts"));
    QCOMPARE(info->files.size(), 3);
}

// tests/data/forge_fixture_echo.driftfx was exported by Drift Forge's audio editor, so this is the
// contract between the two repos: the user layout check, the install, and the audio catalog.
void TestAddonPackage::installsForgeAudioGraphPackage()
{
    // Exported by Drift Forge (scripts/make-drift-fixtures.ts): a filter swept by an LFO and followed
    // by an envelope, then a blend into a convolution room (its IR shipped in the package) and a
    // delay with a slider-bound footswitch.
    const QString path = QStringLiteral(DRIFT_TEST_DATA_DIR "/forge_fixture_rack.driftfx");
    QString error;
    const auto manifest = readManifest(path, &error, Container::User);
    QVERIFY2(manifest.has_value(), qPrintable(error));
    QCOMPARE(manifest->minAppVersion, QStringLiteral("0.8.0"));

    const QString dest = m_tmp.filePath(QStringLiteral("forge-graph"));
    PackageInfo info;
    QVERIFY2(install(path, dest, {}, &info, &error, Container::User), qPrintable(error));
    const QString root = dest + QStringLiteral("/audio-effects");
    QVERIFY(QFile::exists(root + QStringLiteral("/forge_fixture_rack/ir/room.wav")));

    reloadAudioEffectCatalog({root});
    const auto restore = qScopeGuard([] { reloadAudioEffectCatalog(); });
    const AudioEffectEntry *entry = audioEffectDefForId(QStringLiteral("forge_fixture_rack"));
    QVERIFY(entry);
    QCOMPARE(entry->processorId, QStringLiteral("graph"));
    QVERIFY(entry->graph);
    QCOMPARE(entry->graph->chain.size(), size_t(2));
    QCOMPARE(entry->graph->modulators.size(), size_t(2));
    const auto &lane = entry->graph->chain[1].lanes[1];
    QCOMPARE(lane.front().type, std::string("convolution"));
    QVERIFY(lane.front().ir && lane.front().ir->frames > 30000);
    QVERIFY(entry->prerollMs >= 700); // the room's tail, measured by Drift itself

    QStringList keys;
    for (const drift::EffectParamSpec &p : entry->parameters)
        keys << p.key;
    QCOMPARE(keys, (QStringList{QStringLiteral("tone"), QStringLiteral("space"), QStringLiteral("echo_off")}));

    // And it plays: through the same rack the mixer uses, loud enough to hear and never broken.
    drift::Effect effect;
    effect.catalogId = entry->id;
    const QVector<drift::AudioEffectSpec> specs = audioEffectSpecsFor({effect});
    drift::AudioEffectRack rack;
    QVERIFY(rack.configure(specs, 48000));
    rack.reset(0);
    QVector<float> audio(48000 * 2);
    for (int i = 0; i < 48000; ++i)
        audio[i * 2] = audio[i * 2 + 1] = 0.4f * float(std::sin(2.0 * M_PI * 440.0 * i / 48000.0));
    rack.process(audio.data(), 48000, 0);
    float peak = 0.0f;
    for (float v : audio) {
        QVERIFY(std::isfinite(v));
        peak = std::max(peak, std::abs(v));
    }
    QVERIFY2(peak > 0.01f, qPrintable(QString::number(peak)));
}

void TestAddonPackage::installsForgeAudioEffectPackage()
{
    const QString path = QStringLiteral(DRIFT_TEST_DATA_DIR "/forge_fixture_echo.driftfx");
    QString error;
    const auto manifest = readManifest(path, &error, Container::User);
    QVERIFY2(manifest.has_value(), qPrintable(error));
    QCOMPARE(manifest->provides.size(), 1);
    QCOMPARE(manifest->provides.first().kind, QStringLiteral("audio-effects"));

    const QString dest = m_tmp.filePath(QStringLiteral("forge-audio"));
    PackageInfo info;
    QVERIFY2(install(path, dest, {}, &info, &error, Container::User), qPrintable(error));
    const QString root = dest + QStringLiteral("/audio-effects");
    QVERIFY(QFile::exists(root + QStringLiteral("/forge_fixture_echo/audio-effect.json")));

    reloadAudioEffectCatalog({root});
    const AudioEffectEntry *entry = audioEffectDefForId(QStringLiteral("forge_fixture_echo"));
    QVERIFY(entry);
    QCOMPARE(entry->processorId, QStringLiteral("echo"));
    QCOMPARE(entry->parameters.first().defaultValue, 120.0);
    reloadAudioEffectCatalog();
}

void TestAddonPackage::writesUserPackageThatInstalls()
{
    const QString src = m_tmp.filePath(QStringLiteral("authored/tint"));
    QVERIFY(QDir().mkpath(src + QStringLiteral("/sub")));
    const QByteArray json = R"({"id":"user.tint","backend":"gpu"})";
    const QByteArray frag = "void main() {}\n";
    QFile a(src + QStringLiteral("/effect.json"));
    QVERIFY(a.open(QIODevice::WriteOnly));
    a.write(json);
    a.close();
    QFile b(src + QStringLiteral("/sub/main.frag"));
    QVERIFY(b.open(QIODevice::WriteOnly));
    b.write(frag);
    b.close();

    const QString out = m_tmp.filePath(QStringLiteral("tint.driftfx"));
    QString error;
    QVERIFY2(writeUserPackage(src, QStringLiteral("effects"),
                              QJsonObject{{QStringLiteral("id"), QStringLiteral("user.tint")},
                                          {QStringLiteral("name"), QStringLiteral("Tint")},
                                          {QStringLiteral("version"), QStringLiteral("1.0.0")}},
                              out, &error),
             qPrintable(error));
    QVERIFY(!QFile::exists(out + QStringLiteral(".partial")));

    const auto manifest = readManifest(out, &error, Container::User);
    QVERIFY2(manifest.has_value(), qPrintable(error));
    QCOMPARE(manifest->id, QStringLiteral("user.tint"));
    QCOMPARE(manifest->files.size(), 2);

    const QString dest = m_tmp.filePath(QStringLiteral("authored-install"));
    QVERIFY2(install(out, dest, {}, nullptr, &error, Container::User), qPrintable(error));
    QFile installed(dest + QStringLiteral("/effects/tint/sub/main.frag"));
    QVERIFY(installed.open(QIODevice::ReadOnly));
    QCOMPARE(installed.readAll(), frag);
}

void TestAddonPackage::classifiesSignatures()
{
    QString error;
    QCOMPARE(checkSignature(m_fixture, &error), SignatureCheck::Official);

    const QString sig = corruptedCopy(QStringLiteral("classify-sig.driftpkg"), -8);
    QCOMPARE(checkSignature(sig, &error), SignatureCheck::Unverified);

    const QString payload = corruptedCopy(QStringLiteral("classify-payload.driftpkg"), -160);
    QCOMPARE(checkSignature(payload, &error), SignatureCheck::Corrupt);
    QVERIFY(!error.isEmpty());
}

void TestAddonPackage::installsUnverifiedWhenAllowed()
{
    const QString sig = corruptedCopy(QStringLiteral("allowed-sig.driftpkg"), -8);
    const QString dest = m_tmp.filePath(QStringLiteral("allowed-sig"));
    QString error;
    QVERIFY2(install(sig, dest, {}, nullptr, &error, Container::Signed, false), qPrintable(error));
    QVERIFY(QFile::exists(dest + QStringLiteral("/fonts/testfamily/family.json")));

    // Lifting the signature requirement must not lift the content checks.
    const QString payload = corruptedCopy(QStringLiteral("allowed-payload.driftpkg"), -160);
    QVERIFY(!install(payload, m_tmp.filePath(QStringLiteral("allowed-payload")), {}, nullptr, &error,
                     Container::Signed, false));
}

void TestAddonPackage::parsesFolderManifest()
{
    QString error;
    const auto info = parseFolderManifest(
        R"({"schema":1,"id":"my.fonts","version":"1.0.0","provides":[{"kind":"fonts","root":"fonts"}]})", &error);
    QVERIFY2(info.has_value(), qPrintable(error));
    QCOMPARE(info->id, QStringLiteral("my.fonts"));
    QVERIFY(info->files.isEmpty());
    QVERIFY(!hasNativeCode(*info));

    QVERIFY(!parseFolderManifest(
        R"({"schema":1,"id":"../escape","version":"1.0.0","provides":[{"kind":"fonts","root":"fonts"}]})", &error));
    QVERIFY(!parseFolderManifest(
        R"({"schema":1,"id":"a/b","version":"1.0.0","provides":[{"kind":"fonts","root":"fonts"}]})", &error));
}

void TestAddonPackage::installsAndVerifies()
{
    const QString dest = m_tmp.filePath(QStringLiteral("install"));
    PackageInfo info;
    QString error;
    QVERIFY2(install(m_fixture, dest, {}, &info, &error), qPrintable(error));

    QCOMPARE(info.id, QStringLiteral("test.fixture"));
    QVERIFY(QFile::exists(dest + QStringLiteral("/fonts/testfamily/family.json")));
    QVERIFY(QFile::exists(dest + QStringLiteral("/fonts/testfamily/Test-Regular.ttf")));
    QVERIFY(!QDir(dest + QStringLiteral(".partial")).exists());

    QFile family(dest + QStringLiteral("/fonts/testfamily/family.json"));
    QVERIFY(family.open(QIODevice::ReadOnly));
    QCOMPARE(family.readAll(), QByteArray(R"({"id":"testfamily","family":"Test Family"})"));
    // Closed before reinstalling: Windows refuses to unlink an open file, so holding this handle
    // failed the reinstall below in removeRecursively() rather than in anything it means to test.
    family.close();

    // Installing again over an existing directory must succeed, not trip over the leftovers.
    QVERIFY2(install(m_fixture, dest, {}, &info, &error), qPrintable(error));
}

void TestAddonPackage::rejectsTamperedPayload()
{
    // Well inside the zstd frame: the per-file hash or the frame checksum must catch it, and
    // either way the signature over the whole prefix no longer matches.
    const QString path = corruptedCopy(QStringLiteral("payload.driftpkg"), -160);
    QVERIFY(!path.isEmpty());

    QString error;
    QVERIFY(!install(path, m_tmp.filePath(QStringLiteral("bad-payload")), {}, nullptr, &error));
    QVERIFY(!error.isEmpty());
}

void TestAddonPackage::rejectsTamperedSignature()
{
    const QString path = corruptedCopy(QStringLiteral("sig.driftpkg"), -8);
    QVERIFY(!path.isEmpty());

    QString error;
    QVERIFY(!install(path, m_tmp.filePath(QStringLiteral("bad-sig")), {}, nullptr, &error));
    QVERIFY2(error.contains(QStringLiteral("signature")), qPrintable(error));
}

void TestAddonPackage::rejectsTruncatedFile()
{
    const QString path = m_tmp.filePath(QStringLiteral("short.driftpkg"));
    QVERIFY(QFile::copy(m_fixture, path));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.resize(file.size() - 40));
    file.close();

    QString error;
    QVERIFY(!install(path, m_tmp.filePath(QStringLiteral("truncated")), {}, nullptr, &error));
    QVERIFY(!error.isEmpty());
}

void TestAddonPackage::rejectsBadMagic()
{
    const QString path = corruptedCopy(QStringLiteral("magic.driftpkg"), 2);
    QVERIFY(!path.isEmpty());

    QString error;
    QVERIFY(!readManifest(path, &error).has_value());
    QVERIFY2(error.contains(QStringLiteral("magic")), qPrintable(error));
}

void TestAddonPackage::leavesNoStagingBehindOnFailure()
{
    const QString path = corruptedCopy(QStringLiteral("staging.driftpkg"), -8);
    QVERIFY(!path.isEmpty());
    const QString dest = m_tmp.filePath(QStringLiteral("staging-dest"));

    QString error;
    QVERIFY(!install(path, dest, {}, nullptr, &error));
    QVERIFY(!QDir(dest).exists());
    QVERIFY(!QDir(dest + QStringLiteral(".partial")).exists());
}

void TestAddonPackage::reportsCancellation()
{
    const QString dest = m_tmp.filePath(QStringLiteral("cancelled"));
    QString error;
    QVERIFY(!install(m_fixture, dest, [](qint64, qint64) { return false; }, nullptr, &error));
    QCOMPARE(error, QStringLiteral("cancelled"));
    QVERIFY(!QDir(dest).exists());
    QVERIFY(!QDir(dest + QStringLiteral(".partial")).exists());
}

// Effects and transitions ship with the build *and* exist as addons, so that a shader fix can be
// pushed without an app release. That only works if an installed package outranks the bundled one
// of the same id, and the catalogs resolve duplicates first-root-wins — so the ordering here is
// load-bearing, not cosmetic.
void TestAddonPackage::installedAddonOutranksBundledContent()
{
    QStandardPaths::setTestModeEnabled(true);
    auto restore = qScopeGuard([] { QStandardPaths::setTestModeEnabled(false); });

    const QString installDir = drift::addon::addonInstallDir(QStringLiteral("test.fixture"));
    QDir(installDir).removeRecursively();

    PackageInfo info;
    QString error;
    QVERIFY2(install(m_fixture, installDir, {}, &info, &error), qPrintable(error));
    QVERIFY2(recordInstalledAddon(info, &error), qPrintable(error));
    reloadAddonRegistry();

    const QStringList roots = GpuPackageParse::defaultSearchPaths(
        QStringLiteral("DRIFT_UNSET_FOR_TEST"), QStringLiteral("fonts"), QStringLiteral("fonts"));

    const QString addonRoot = QDir(installDir).filePath(QStringLiteral("fonts"));
    const QString bundledRoot =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("fonts"));

    QVERIFY2(roots.contains(addonRoot), qPrintable(roots.join(QStringLiteral(" | "))));
    QVERIFY2(roots.contains(bundledRoot), qPrintable(roots.join(QStringLiteral(" | "))));
    QVERIFY2(roots.indexOf(addonRoot) < roots.indexOf(bundledRoot),
             qPrintable(QStringLiteral("addon must outrank bundled: %1")
                            .arg(roots.join(QStringLiteral(" | ")))));

    QVERIFY2(forgetInstalledAddon(QStringLiteral("test.fixture"), &error), qPrintable(error));
    QDir(installDir).removeRecursively();
    reloadAddonRegistry();
}

QTEST_MAIN(TestAddonPackage)
#include "tst_addonpackage.moc"
