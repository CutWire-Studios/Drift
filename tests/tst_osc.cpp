#include "app/surfaces/ControlSurfaceManager.h"
#include "app/surfaces/osc/OscCodec.h"
#include "app/surfaces/osc/OscSurfaceService.h"

#include <QCoreApplication>
#include <QNetworkDatagram>
#include <QSettings>
#include <QStandardPaths>
#include <QTest>
#include <QUdpSocket>

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
    void setAbsolute(const QString &id, double n) override { sets.append({id, n}); }
    void nudge(const QString &id, double ticks) override { nudges.append({id, ticks}); }
    double current(const QString &id) const override
    {
        const auto it = values.constFind(id);
        return it == values.constEnd() ? std::nan("") : it.value();
    }
    double playheadSeconds() const override { return playhead; }
    void seek(double seconds) override { seeks << seconds; }

    QList<QPair<QString, double>> sets;
    QList<QPair<QString, double>> nudges;
    QStringList triggered;
    QList<double> seeks;
    QHash<QString, double> values;
    double playhead = 0.0;
};

} // namespace

class TestOsc : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("drift-test"));
        QCoreApplication::setApplicationName(QStringLiteral("osc"));
        QSettings().clear();
    }

    void codecRoundTrip()
    {
        const OscMessage in{QStringLiteral("/drift/param/mixer.volume.1"),
                            {0.5, 7, QStringLiteral("héllo"), true, false, QByteArray("\x01\x02\x03", 3)}};
        const QByteArray bytes = encodeOscMessage(in);
        QCOMPARE(bytes.size() % 4, 0);
        const QList<OscMessage> out = parseOscPacket(bytes);
        QCOMPARE(out.size(), 1);
        QCOMPARE(out.first().address, in.address);
        QCOMPARE(out.first().args.size(), 6);
        QCOMPARE(out.first().args.at(0).toDouble(), 0.5);
        QCOMPARE(out.first().args.at(1).toInt(), 7);
        QCOMPARE(out.first().args.at(2).toString(), QString("héllo"));
        QVERIFY(out.first().args.at(3).toBool());
        QVERIFY(!out.first().args.at(4).toBool());
        QCOMPARE(out.first().args.at(5).toByteArray(), QByteArray("\x01\x02\x03", 3));
    }

    void parsesKnownBytes()
    {
        // The example from the OSC 1.0 spec text: "/oscillator/4/frequency" ,f 440.0
        QByteArray packet("/oscillator/4/frequency\0,f\0\0", 28);
        const unsigned char f440[] = {0x43, 0xDC, 0x00, 0x00};
        packet.append(reinterpret_cast<const char *>(f440), 4);
        const QList<OscMessage> out = parseOscPacket(packet);
        QCOMPARE(out.size(), 1);
        QCOMPARE(out.first().address, QString("/oscillator/4/frequency"));
        QCOMPARE(out.first().args.first().toDouble(), 440.0);
    }

    void parsesBundles()
    {
        const QByteArray a = encodeOscMessage({"/a", {1}});
        const QByteArray b = encodeOscMessage({"/b", {}});
        QByteArray bundle("#bundle\0", 8);
        bundle.append(QByteArray(8, '\0'));
        for (const QByteArray &element : {a, b}) {
            QByteArray size(4, '\0');
            size[3] = static_cast<char>(element.size());
            bundle.append(size);
            bundle.append(element);
        }
        const QList<OscMessage> out = parseOscPacket(bundle);
        QCOMPARE(out.size(), 2);
        QCOMPARE(out.at(0).address, QString("/a"));
        QCOMPARE(out.at(1).address, QString("/b"));
    }

    void rejectsGarbage()
    {
        QVERIFY(parseOscPacket(QByteArray("nonsense")).isEmpty());
        QVERIFY(parseOscPacket(QByteArray()).isEmpty());
        QVERIFY(parseOscPacket(QByteArray("/x\0\0,ii\0\0\0\0\x01", 12)).isEmpty());
    }

    void hostRules()
    {
        QVERIFY(OscSurfaceService::hostAllowed(QHostAddress("127.0.0.1"), {}));
        QVERIFY(OscSurfaceService::hostAllowed(QHostAddress("192.168.4.20"), {}));
        QVERIFY(OscSurfaceService::hostAllowed(QHostAddress("10.1.2.3"), {}));
        QVERIFY(OscSurfaceService::hostAllowed(QHostAddress("172.20.0.5"), {}));
        QVERIFY(!OscSurfaceService::hostAllowed(QHostAddress("8.8.8.8"), {}));
        QVERIFY(!OscSurfaceService::hostAllowed(QHostAddress("172.32.0.5"), {}));
        QVERIFY(OscSurfaceService::hostAllowed(QHostAddress("8.8.8.8"), "8.8.8.8"));
        QVERIFY(OscSurfaceService::hostAllowed(QHostAddress("203.0.113.9"), "203.0.113.0/24\n10.0.0.1"));
        QVERIFY(!OscSurfaceService::hostAllowed(QHostAddress("192.168.4.20"), "203.0.113.0/24"));
        QVERIFY(OscSurfaceService::hostAllowed(QHostAddress("::ffff:192.168.1.5"), {}));
    }

    void driftAddressSpace()
    {
        FakeTargets t;
        ControlSurfaceManager m(&t);
        OscSurfaceService s(&m);
        const QHostAddress from("127.0.0.1");
        s.handle({"/drift/transport/play", {}}, from);
        s.handle({"/drift/transport/stop", {1}}, from);
        s.handle({"/drift/transport/play", {0.0}}, from);
        QCOMPARE(t.triggered, (QStringList{"playPause", "stop"}));
        s.handle({"/drift/action/split", {}}, from);
        QCOMPARE(t.triggered.last(), QString("split"));
        s.handle({"/drift/param/mixer.volume.2", {0.75}}, from);
        QCOMPARE(t.sets.last().first, QString("mixer.volume.2"));
        QCOMPARE(t.sets.last().second, 0.75);
        s.handle({"/drift/param/playhead.jog", {3}}, from);
        QCOMPARE(t.nudges.last().second, 3.0);
        s.handle({"/drift/playhead", {12.5}}, from);
        QCOMPARE(t.seeks, QList<double>{12.5});
    }

    void unknownAddressesBecomeControls()
    {
        FakeTargets t;
        ControlSurfaceManager m(&t);
        OscSurfaceService s(&m);
        s.setEnabled(true);
        if (!s.error().isEmpty())
            QSKIP("cannot bind a UDP port here");
        const QHostAddress from("127.0.0.1");
        m.bind("osc", "/1/fader1", "mixer.volume.1");
        m.bind("osc", "/1/push1", "playPause");
        s.handle({"/1/fader1", {0.25}}, from);
        QCOMPARE(t.sets.last().second, 0.25);
        s.handle({"/1/push1", {1}}, from);
        QCOMPARE(t.triggered.last(), QString("playPause"));
        bool seen = false;
        for (const QVariant &c : m.devices().first().toMap().value("controls").toList())
            seen |= c.toMap().value("id").toString() == "/1/fader1";
        QVERIFY(seen);
    }

    void receivesOverUdpAndAnswers()
    {
        FakeTargets t;
        ControlSurfaceManager m(&t);
        OscSurfaceService s(&m);
        s.setPort(18471);
        s.setFeedbackPort(18472);
        s.setEnabled(true);
        if (!s.error().isEmpty())
            QSKIP("cannot bind a UDP port here");
        QUdpSocket client;
        QVERIFY(client.bind(QHostAddress::LocalHost, 18472));
        client.writeDatagram(encodeOscMessage({"/drift/ping", {}}), QHostAddress::LocalHost, 18471);
        QTRY_VERIFY(client.hasPendingDatagrams());
        const QList<OscMessage> reply = parseOscPacket(client.receiveDatagram().data());
        QCOMPARE(reply.size(), 1);
        QCOMPARE(reply.first().address, QString("/drift/pong"));
    }
};

QTEST_MAIN(TestOsc)
#include "tst_osc.moc"
