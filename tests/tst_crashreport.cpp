#include "app/crash/CrashBundle.h"
#include "app/crash/CrashMeta.h"
#include "app/crash/CrashReason.h"
#include "app/crash/CrashStore.h"
#include "app/crash/LogScrubber.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

#include <zstd.h>
#include <QtTest>

using namespace drift::crash;

namespace {

void writeFile(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(data);
}

void writeCrash(const QString &root, const QString &id, const QString &crashedAt)
{
    const QJsonObject json{{"code", "0xC0000005"}, {"module", "drift.exe"}, {"offset", "0x10"},
                           {"crashedAt", crashedAt}};
    writeFile(root + "/crashes/" + id + "/crash.json", QJsonDocument(json).toJson());
}

} // namespace

class TestCrashReport : public QObject
{
    Q_OBJECT

private slots:
    void reasonCrt_data()
    {
        QTest::addColumn<QString>("module");
        QTest::newRow("msvcp") << "MSVCP140.dll";
        QTest::newRow("msvcp-1") << "msvcp140_1.dll";
        QTest::newRow("vcruntime") << "VCRUNTIME140_1.dll";
        QTest::newRow("ucrt") << "ucrtbase.dll";
    }
    void reasonCrt()
    {
        QFETCH(QString, module);
        const Reason reason = mapReason("0xC0000005", module, {}, "C:/addons");
        QCOMPARE(reason.key, QString("crt-outdated"));
        QCOMPARE(reason.linkUrl, QString("https://aka.ms/vs/17/release/vc_redist.x64.exe"));
    }

    void reasonGpu_data()
    {
        QTest::addColumn<QString>("module");
        for (const char *name : {"nvoglv64.dll", "nvwgf2umx.dll", "atio6axx.dll", "atiumd64.dll", "amdxx64.dll",
                                 "igxelpicd64.dll", "ig9icd64.dll", "igd10iumd64.dll", "igc64.dll",
                                 "libnvidia-glcore.so.550.54.14", "libGLX_nvidia.so.0", "radeonsi_dri.so",
                                 "iris_dri.so", "swrast_dri.so", "libgallium-25.0.3.so", "libvulkan_radeon.so",
                                 "AGXMetalG14G", "AppleMetalOpenGLRenderer", "AMDMTLBronzeDriver",
                                 "GLEngine"})
            QTest::newRow(name) << name;
    }
    void reasonGpu()
    {
        QFETCH(QString, module);
        QCOMPARE(mapReason("0xC0000005", module, {}, "C:/addons").key, QString("gpu-driver"));
    }

    void reasonLinuxAddon()
    {
        Reason reason = mapReason("0xB", "libfoo.so", "/home/a/.local/share/Drift/addons/Whisper/bin/libfoo.so",
                                  "/home/a/.local/share/Drift/addons");
        QCOMPARE(reason.key, QString("addon"));
        QCOMPARE(reason.addonId, QString("Whisper"));
        QCOMPARE(mapReason("0xB", "libonnxruntime.so.1.20", "/usr/lib/libonnxruntime.so.1.20", {}).key,
                 QString("addon"));
        QCOMPARE(mapReason("0xB", "libonnxruntime.1.20.dylib", {}, {}).key, QString("addon"));
        QCOMPARE(mapReason("0xB", "drift", "/usr/bin/drift", "/x").key, QString("unknown"));
    }

    void reasonAddon()
    {
        Reason reason = mapReason("0xC0000005", "foo.dll", "C:\\Users\\a\\AppData\\Local\\Drift\\addons\\Whisper\\bin\\foo.dll",
                                  "C:/Users/a/AppData/Local/Drift/addons");
        QCOMPARE(reason.key, QString("addon"));
        QCOMPARE(reason.addonId, QString("Whisper"));
        QVERIFY(reason.text.contains("Whisper"));

        reason = mapReason("0xC0000005", "onnxruntime.dll", "C:/elsewhere/onnxruntime.dll", "C:/addons");
        QCOMPARE(reason.key, QString("addon"));
        QVERIFY(reason.addonId.isEmpty());

        QCOMPARE(mapReason("0xC0000005", "foo.dll", "C:/addons-other/x/foo.dll", "C:/addons").key, QString("unknown"));
    }

    void reasonOutOfMemory()
    {
        QCOMPARE(mapReason("0xc0000017", "ntdll.dll", {}, {}).key, QString("out-of-memory"));
        QCOMPARE(mapReason("0xC0000017", "ucrtbase.dll", {}, {}).key, QString("out-of-memory"));
    }

    void reasonUnknown()
    {
        QCOMPARE(mapReason("0xC0000005", "drift.exe", "C:/Drift/drift.exe", "C:/addons").key, QString("unknown"));
    }

    void scrubUserAndEmail()
    {
        const ScrubContext context{"C:\\Users\\alice", "alice", "C:\\Program Files\\Drift"};
        const QString out = scrubLog("opening C:\\Users\\alice\\Videos\\Holiday\\trip.mp4 for alice@example.com", context);
        QCOMPARE(out, QString("opening <user>/.../<file>.mp4 for <email>"));
        QVERIFY(!out.contains("alice"));
    }

    void scrubUsernameElsewhere()
    {
        const ScrubContext context{"/home/alice", "alice", "/opt/drift"};
        QCOMPARE(scrubLog("hello alice, home is /home/alice", context), QString("hello <user>, home is <user>"));
    }

    void scrubKeepsAppDirStructure()
    {
        const ScrubContext context{"C:\\Users\\alice", "alice", "C:\\Program Files\\Drift"};
        const QString out = scrubLog("loaded C:\\Program Files\\Drift\\effects\\blur\\blur.glsl ok", context);
        QCOMPARE(out, QString("loaded C:\\Program Files\\Drift\\effects\\blur\\<file>.glsl ok"));
    }

    void scrubOtherPaths()
    {
        const ScrubContext context{"C:\\Users\\alice", "alice", "C:\\Drift"};
        QCOMPARE(scrubLog("read D:\\Clients\\Acme\\cut.mov and /mnt/data/proj/clip.mp4", context),
                 QString("read <path>/<file>.mov and <path>/<file>.mp4"));
    }

    void scrubLeavesUrlsAndQrc()
    {
        const ScrubContext context{"/home/alice", "alice", "/opt/drift"};
        const QString line = "qrc:/qt/qml/Drift/Foo.qml:12 fetching https://cutwire.org/a/b.json and/or more";
        QCOMPARE(scrubLog(line, context), line);
    }

    void scrubAppUnderHome()
    {
        const ScrubContext context{"C:\\Users\\alice", "alice", "C:\\Users\\alice\\Drift"};
        const QString out = scrubLog("at C:\\Users\\alice\\Drift\\x\\y.dll", context);
        QVERIFY(!out.contains("alice"));
        QVERIFY(out.contains("<file>.dll"));
    }

    void tailStartsOnLine()
    {
        const QString text = "first line\nsecond line\nthird line\n";
        QCOMPARE(tailOfLog(text, 1000), text.toUtf8());
        QCOMPARE(tailOfLog(text, 15), QByteArray("third line\n"));
    }

    void metaShape()
    {
        CrashInfo crash;
        crash.id = "11111111-2222-4333-8444-555555555555";
        crash.code = "0xC0000005";
        crash.module = "MSVCP140.dll";
        crash.moduleVersion = "14.29.30133.0";
        crash.offset = "0x13020";
        crash.crashedAt = QDateTime::fromString("2026-11-13T10:00:00Z", Qt::ISODate);
        SystemInfo system;
        system.appVersion = "1.4.2";
        system.channel = "stable";
        system.arch = "x64";
        system.osName = "windows";
        system.osVersion = "10.0.19042";
        system.graphicsApi = "opengl";
        system.locale = "es-ES";
        system.addons = {{"onnxruntime", "1.22.0"}};
        for (int i = 0; i < 6; ++i)
            system.gpus.append({"NVIDIA", QString(400, 'x'), {}});

        const QJsonObject meta = buildMeta(crash, "crt-outdated", system, 412);
        QCOMPARE(meta["schema"].toInt(), 1);
        QCOMPARE(meta["reportId"].toString(), crash.id);
        QCOMPARE(meta["crashedAt"].toString(), QString("2026-11-13T10:00:00Z"));
        QCOMPARE(meta["uptimeSeconds"].toInt(), 412);
        QCOMPARE(meta["app"]["name"].toString(), QString("drift"));
        QCOMPARE(meta["crash"]["reasonKey"].toString(), QString("crt-outdated"));
        QCOMPARE(meta["crash"]["moduleVersion"].toString(), QString("14.29.30133.0"));
        QVERIFY(!meta["crash"].toObject().contains("modulePath"));
        QCOMPARE(meta["gpu"].toArray().size(), 4);
        QCOMPARE(meta["gpu"][0]["name"].toString().size(), 256);
        QVERIFY(!meta["gpu"][0].toObject().contains("driver"));
        QCOMPARE(meta["addons"][0]["id"].toString(), QString("onnxruntime"));
        QCOMPARE(meta["locale"].toString(), QString("es-ES"));
    }

    void channelAndArch()
    {
        QCOMPARE(channelFor("1.4.2", "windows"), QString("stable"));
        QCOMPARE(channelFor("1.4.2-nightly.7", "windows"), QString("nightly"));
        QCOMPARE(channelFor("1.4.2", "windows-store"), QString("msstore"));
        QCOMPARE(archFor("x86_64"), QString("x64"));
        QCOMPARE(archFor("arm64"), QString("arm64"));
        QCOMPARE(osNameFor("winnt"), QString("windows"));
        QCOMPARE(osNameFor("linux"), QString("linux"));
        QCOMPARE(osNameFor("darwin"), QString("macos"));
    }

    void appleReportPickup()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writeFile(dir.path() + "/Other-2026.ips", "x");
        writeFile(dir.path() + "/drift-old.ips", "old");
        QFile old(dir.path() + "/drift-old.ips");
        QVERIFY(old.open(QIODevice::ReadWrite));
        QVERIFY(old.setFileTime(QDateTime::currentDateTime().addSecs(-3600), QFileDevice::FileModificationTime));
        old.close();
        const QDateTime since = QDateTime::currentDateTime().addSecs(-60);
        QVERIFY(newestAppleReport(dir.path(), since).isEmpty());
        writeFile(dir.path() + "/Drift-2026-11-13.ips", "new");
        QCOMPARE(QFileInfo(newestAppleReport(dir.path(), since)).fileName(), QString("Drift-2026-11-13.ips"));
        QVERIFY(newestAppleReport(dir.path() + "/missing", since).isEmpty());
    }

    void bundleCarriesStackAndAppleReport()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writeFile(dir.path() + "/crashes/abc/crash.json", "{}");
        writeFile(dir.path() + "/crashes/abc/stack.txt", "frame");
        writeFile(dir.path() + "/apple/Drift-1.ips", "ips");
        const auto items = bundleItems(dir.path() + "/crashes/abc", dir.path() + "/logs", "info",
                                       dir.path() + "/apple/Drift-1.ips");
        QStringList names;
        for (const auto &item : items)
            names << item.name;
        QCOMPARE(names, QStringList({"crash.json", "stack.txt", "Drift-1.ips", "system-info.txt"}));
    }

    void tarZstRoundTrip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writeFile(dir.path() + "/crashes/abc/full.dmp", QByteArray(3 * 1024 * 1024, 'd'));
        writeFile(dir.path() + "/crashes/abc/crash.json", "{}");
        writeFile(dir.path() + "/logs/drift.log", "log\n");
        const auto items = bundleItems(dir.path() + "/crashes/abc", dir.path() + "/logs", "info");
        QStringList names;
        for (const auto &item : items)
            names << item.name;
        QCOMPARE(names, QStringList({"crash.json", "full.dmp", "drift.log", "system-info.txt"}));

        const QString path = dir.path() + "/out.tar.zst";
        QString error;
        QVERIFY2(writeTarZst(path, items, &error), qPrintable(error));

        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray packed = file.readAll();
        QVERIFY(packed.size() < 64 * 1024);
        QByteArray tar;
        ZSTD_DCtx *ctx = ZSTD_createDCtx();
        ZSTD_inBuffer in{packed.constData(), size_t(packed.size()), 0};
        QByteArray chunk(1 << 16, Qt::Uninitialized);
        size_t pending = 1;
        while (pending != 0) {
            ZSTD_outBuffer out{chunk.data(), size_t(chunk.size()), 0};
            pending = ZSTD_decompressStream(ctx, &out, &in);
            QVERIFY(!ZSTD_isError(pending));
            tar.append(chunk.constData(), qsizetype(out.pos));
        }
        ZSTD_freeDCtx(ctx);

        QStringList tarNames;
        QList<qint64> sizes;
        qint64 at = 0;
        QByteArray last;
        while (at + 512 <= tar.size() && tar.at(at) != '\0') {
            const QByteArray header = tar.mid(at, 512);
            QCOMPARE(header.mid(257, 5), QByteArray("ustar"));
            tarNames << QString::fromUtf8(header.constData());
            const qint64 size = header.mid(124, 11).toLongLong(nullptr, 8);
            sizes << size;
            last = tar.mid(at + 512, size);
            at += 512 + (size + 511) / 512 * 512;
        }
        QCOMPARE(tarNames, names);
        QCOMPARE(sizes[1], qint64(3 * 1024 * 1024));
        QCOMPARE(last, QByteArray("info"));
        QCOMPARE(tar.size(), at + 1024);
    }

    void storeFlow()
    {
        QTemporaryDir dir;
        const CrashStore store(dir.path());
        const auto alive = [](qint64 pid) { return pid == 111; };

        store.beginSession(111);
        store.beginSession(222);
        auto startup = store.scan(alive);
        QCOMPARE(startup.uncleanSessions, 1);
        QVERIFY(startup.crashes.isEmpty());
        QVERIFY(QFile::exists(dir.path() + "/sessions/111.running"));
        QVERIFY(!QFile::exists(dir.path() + "/sessions/222.running"));
        store.endSession(111);
        QCOMPARE(store.scan(alive).uncleanSessions, 0);

        writeCrash(dir.path(), "old", "2026-11-01T00:00:00Z");
        writeCrash(dir.path(), "new", "2026-11-12T00:00:00Z");
        QDir().mkpath(dir.path() + "/crashes/empty");
        startup = store.scan(alive);
        QCOMPARE(startup.crashes.size(), 2);
        QCOMPARE(startup.crashes[0].id, QString("new"));
        store.markShown(startup.crashes[0]);
        QCOMPARE(store.scan(alive).crashes.size(), 1);
    }

    void uploadLifecycle()
    {
        QTemporaryDir dir;
        const CrashStore store(dir.path());
        const QDateTime now = QDateTime::fromString("2026-11-13T00:00:00Z", Qt::ISODate);
        writeCrash(dir.path(), "a", "2026-11-12T00:00:00Z");
        writeCrash(dir.path(), "stale", "2026-09-01T00:00:00Z");
        writeCrash(dir.path(), "tired", "2026-11-12T00:00:00Z");
        writeCrash(dir.path(), "sent", "2026-11-12T00:00:00Z");

        const CrashInfo tired = readCrashInfo(dir.path() + "/crashes/tired");
        for (int i = 0; i < kMaxUploadAttempts; ++i)
            store.recordAttempt(tired);
        store.markDone(readCrashInfo(dir.path() + "/crashes/sent"));

        const auto pending = store.pendingUploads(now);
        QCOMPARE(pending.size(), 1);
        QCOMPARE(pending[0].id, QString("a"));
        QCOMPARE(store.attempts(pending[0]), 0);

        // Not shown yet, so nothing is swept.
        store.sweep(now);
        QCOMPARE(QDir(dir.path() + "/crashes").entryList(QDir::Dirs | QDir::NoDotAndDotDot).size(), 4);
        for (const QString &id : {"a", "stale", "tired", "sent"})
            store.markShown(readCrashInfo(dir.path() + "/crashes/" + id));
        store.sweep(now);
        QCOMPARE(QDir(dir.path() + "/crashes").entryList(QDir::Dirs | QDir::NoDotAndDotDot), QStringList({"a"}));
    }
};

QTEST_APPLESS_MAIN(TestCrashReport)
#include "tst_crashreport.moc"
