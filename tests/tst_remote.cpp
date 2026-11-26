#include "app/surfaces/ControlSurfaceManager.h"
#include "app/surfaces/remote/PhoneRemoteClient.h"
#include "app/surfaces/remote/PhoneRemoteServer.h"
#include "app/surfaces/remote/QrCode.h"
#include "app/surfaces/remote/RemoteProtocol.h"

#include <QCoreApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QTest>

#include <cmath>

using namespace drift;

namespace {

class FakeTargets : public SurfaceTargets {
public:
    QList<SurfaceTarget> targets() const override { return {}; }
    SurfaceTarget describe(const QString &id) const override
    {
        SurfaceTarget t;
        t.id = id;
        t.label = id;
        if (id.startsWith(QLatin1String("mixer.volume")))
            t.kind = SurfaceTarget::Absolute;
        else if (id == QLatin1String("playhead.jog"))
            t.kind = SurfaceTarget::Relative;
        return t;
    }
    bool trigger(const QString &id) override
    {
        triggered << id;
        return true;
    }
    void setAbsolute(const QString &id, double n) override
    {
        sets.append({id, n});
        values[id] = n;
    }
    void nudge(const QString &id, double ticks) override { nudges.append({id, ticks}); }
    double current(const QString &id) const override
    {
        const auto it = values.constFind(id);
        return it == values.constEnd() ? std::nan("") : it.value();
    }
    double playheadSeconds() const override { return 1.5; }

    QList<QPair<QString, double>> sets;
    QList<QPair<QString, double>> nudges;
    QStringList triggered;
    QHash<QString, double> values;
};

} // namespace

class TestRemote : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("drift-test"));
        QCoreApplication::setApplicationName(QStringLiteral("remote"));
        QSettings().clear();
    }

    void framesRoundTripAcrossChunks()
    {
        const QByteArray a = remote::encodeFrame({{"t", "btn"}, {"c", "play"}});
        const QByteArray b = remote::encodeFrame({{"t", "abs"}, {"v", 0.5}});
        const QByteArray all = a + b;
        remote::FrameReader reader;
        QJsonObject out;
        reader.append(all.left(3));
        QVERIFY(!reader.take(&out));
        reader.append(all.mid(3, a.size() + 2 - 3));
        QVERIFY(reader.take(&out));
        QCOMPARE(out.value("c").toString(), QString("play"));
        QVERIFY(!reader.take(&out));
        reader.append(all.mid(a.size() + 2));
        QVERIFY(reader.take(&out));
        QCOMPARE(out.value("v").toDouble(), 0.5);
        QVERIFY(!reader.broken());
    }

    void oversizeFrameBreaksTheStream()
    {
        remote::FrameReader reader;
        reader.append(QByteArray("\x7f\xff\xff\xff", 4));
        QJsonObject out;
        QVERIFY(!reader.take(&out));
        QVERIFY(reader.broken());
    }

    void parsesPairingCodes()
    {
        QString host;
        int port = 0;
        QString token;
        QVERIFY(PhoneRemoteServer::parsePairingCode("192.168.1.20:47890/abcd2345", &host, &port, &token));
        QCOMPARE(host, QString("192.168.1.20"));
        QCOMPARE(port, 47890);
        QCOMPARE(token, QString("ABCD2345"));
        QVERIFY(PhoneRemoteServer::parsePairingCode(PhoneRemoteServer::pairingLink("10.0.0.2", 5, "XYZ"), &host, &port, &token));
        QCOMPARE(host, QString("10.0.0.2"));
        QVERIFY(!PhoneRemoteServer::parsePairingCode("nonsense", &host, &port, &token));
        QVERIFY(!PhoneRemoteServer::parsePairingCode("1.2.3.4:99999/AAAA", &host, &port, &token));
    }

    void qrCodeHasFinders()
    {
        const QrCode code = QrCode::encode("drift://pair/192.168.1.20:47890/ABCD2345");
        QVERIFY(code.isValid());
        QCOMPARE(code.size(), code.version() * 4 + 17);
        // The three finder patterns have a dark centre and a light ring around it.
        for (const QPoint c : {QPoint(3, 3), QPoint(code.size() - 4, 3), QPoint(3, code.size() - 4)}) {
            QVERIFY(code.dark(c.x(), c.y()));
            QVERIFY(!code.dark(c.x() + 2, c.y()));
            QVERIFY(code.dark(c.x() + 3, c.y()));
        }
        QVERIFY(!QrCode::encode(QByteArray(400, 'x')).isValid());
    }

    void pairsAndDrivesTheEditor()
    {
        FakeTargets t;
        ControlSurfaceManager manager(&t);
        PhoneRemoteServer server(&manager);
        server.startPairing();
        QVERIFY2(server.error().isEmpty(), qPrintable(server.error()));
        QVERIFY(server.pairing());
        const QString code = server.pairingCode();
        QVERIFY(!code.isEmpty());

        // The client is told to dial the address in the code; the test overrides it to loopback.
        QString host;
        int port = 0;
        QString token;
        QVERIFY(PhoneRemoteServer::parsePairingCode(code, &host, &port, &token));

        PhoneRemoteClient client;
        QVERIFY(client.pair(QStringLiteral("127.0.0.1:%1/%2").arg(port).arg(token)));
        QTRY_COMPARE_WITH_TIMEOUT(client.state(), QString("connected"), 5000);
        QVERIFY(client.paired());
        QVERIFY(!server.pairing());
        QCOMPARE(server.phones().size(), 1);

        // The phone's controls bind to the default targets.
        QString deviceId;
        for (const QVariant &d : manager.devices()) {
            if (d.toMap().value("id").toString().startsWith("phone:"))
                deviceId = d.toMap().value("id").toString();
        }
        QVERIFY(!deviceId.isEmpty());

        client.button("play", true);
        QTRY_VERIFY(t.triggered.contains("playPause"));
        client.relative("jog", 2.0);
        QTRY_COMPARE(t.nudges.size(), 1);
        client.absolute("fader1", 0.6);
        QTRY_VERIFY(!t.sets.isEmpty());
        QCOMPARE(t.sets.last().first, QString("mixer.volume.1"));
        QCOMPARE(t.sets.last().second, 0.6);

        // Feedback: the target moved, the phone is told.
        t.values["mixer.volume.1"] = 0.9;
        QTRY_VERIFY(client.values().contains("fader1"));
        QTRY_COMPARE(client.values().value("fader1").toDouble(), 0.9);
        QTRY_COMPARE(client.playhead(), 1.5);

        // A paired phone reconnects with its key and no token.
        client.disconnectFromDesktop();
        QTRY_COMPARE(client.state(), QString("disconnected"));
        client.reconnect();
        QTRY_COMPARE_WITH_TIMEOUT(client.state(), QString("connected"), 5000);
        QCOMPARE(server.phones().size(), 1);
    }

    void wrongTokenIsDenied()
    {
        FakeTargets t;
        ControlSurfaceManager manager(&t);
        PhoneRemoteServer server(&manager);
        server.startPairing();
        QString host;
        int port = 0;
        QString token;
        QVERIFY(PhoneRemoteServer::parsePairingCode(server.pairingCode(), &host, &port, &token));
        PhoneRemoteClient client;
        QVERIFY(client.pair(QStringLiteral("127.0.0.1:%1/WRONG123").arg(port)));
        QTRY_VERIFY(!client.error().isEmpty());
        QCOMPARE(server.phones().size(), 0);
        QVERIFY(server.pairing());
    }
};

QTEST_MAIN(TestRemote)
#include "tst_remote.moc"
