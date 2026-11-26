#include "app/surfaces/ControlSurfaceManager.h"
#include "app/surfaces/midi/MidiIo.h"
#include "app/surfaces/midi/MidiProtocols.h"
#include "app/surfaces/midi/MidiSurfaceService.h"

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
        if (id.startsWith(QLatin1String("mixer.volume")) || id.startsWith(QLatin1String("mixer.pan")))
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

    QList<QPair<QString, double>> sets;
    QList<QPair<QString, double>> nudges;
    QStringList triggered;
    QHash<QString, double> values;
};

class FakeIo : public MidiIo {
public:
    using MidiIo::MidiIo;
    QStringList inputPorts() override { return ports; }
    bool open(const QString &name) override
    {
        opened << name;
        return true;
    }
    void close(const QString &name) override { closed << name; }
    void send(const QString &name, const QByteArray &bytes) override { sent.append({name, bytes}); }

    QStringList ports;
    QStringList opened;
    QStringList closed;
    QList<QPair<QString, QByteArray>> sent;
};

QByteArray msg(std::initializer_list<int> v)
{
    QByteArray out;
    for (int b : v)
        out.append(static_cast<char>(b));
    return out;
}

} // namespace

class TestMidiSurfaces : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("drift-test"));
        QCoreApplication::setApplicationName(QStringLiteral("midisurfaces"));
        QSettings().clear();
    }

    void splitsBatchedMessages()
    {
        const QList<QByteArray> parts = splitMidiMessages(msg({0x90, 60, 100, 0xB0, 7, 64, 0xC0, 5, 0xF0, 1, 2, 0xF7}));
        QCOMPARE(parts.size(), 4);
        QCOMPARE(parts.at(2), msg({0xC0, 5}));
        QCOMPARE(parts.at(3), msg({0xF0, 1, 2, 0xF7}));
    }

    void genericLearnsNotesAndControllers()
    {
        FakeTargets t;
        ControlSurfaceManager m(&t);
        GenericMidiProtocol p(&m, "midi:x");
        m.registerDevice(p.describe("x"));
        m.bind("midi:x", "note.1.60", "play");
        m.bind("midi:x", "cc.1.7", "mixer.volume.1");
        m.bind("midi:x", "cc14.1.1", "mixer.pan.1");
        m.bind("midi:x", "pb.2", "mixer.volume.2");

        p.handle(msg({0x90, 60, 100}));
        p.handle(msg({0x80, 60, 0}));
        QCOMPARE(t.triggered, QStringList{"play"});

        p.handle(msg({0xB0, 7, 127}));
        QCOMPARE(t.sets.size(), 1);
        QCOMPARE(t.sets.last().second, 1.0);

        p.handle(msg({0xB0, 1, 64}));
        p.handle(msg({0xB0, 33, 0}));
        QCOMPARE(t.sets.last().first, QString("mixer.pan.1"));
        QVERIFY(std::abs(t.sets.last().second - (64 << 7) / 16383.0) < 1e-9);

        p.handle(msg({0xE1, 0x7F, 0x7F}));
        QCOMPARE(t.sets.last().first, QString("mixer.volume.2"));
        QCOMPARE(t.sets.last().second, 1.0);

        bool sawNote = false;
        for (const QVariant &c : m.devices().first().toMap().value("controls").toList())
            sawNote |= c.toMap().value("id").toString() == "note.1.60";
        QVERIFY(sawNote);
    }

    void genericDecodesNrpn()
    {
        FakeTargets t;
        ControlSurfaceManager m(&t);
        GenericMidiProtocol p(&m, "midi:x");
        m.registerDevice(p.describe("x"));
        m.bind("midi:x", "nrpn.1.130", "mixer.volume.1");
        p.handle(msg({0xB0, 99, 1}));
        p.handle(msg({0xB0, 98, 2}));
        p.handle(msg({0xB0, 6, 127}));
        QCOMPARE(t.sets.size(), 1);
        p.handle(msg({0xB0, 38, 127}));
        QCOMPARE(t.sets.last().second, 1.0);
    }

    void mackieMapsInputs()
    {
        FakeTargets t;
        ControlSurfaceManager m(&t);
        MackieProtocol p(&m, "midi:mcu");
        m.registerDevice(p.describe("mcu"));
        QCOMPARE(m.bindings("midi:mcu").size() > 20, true);

        p.handle(msg({0xE2, 0x7F, 0x7F}));
        QCOMPARE(t.sets.last().first, QString("mixer.volume.3"));
        QCOMPARE(t.sets.last().second, 1.0);

        p.handle(msg({0xB0, 0x3C, 0x41}));
        QCOMPARE(t.nudges.last().first, QString("playhead.jog"));
        QCOMPARE(t.nudges.last().second, -1.0);
        p.handle(msg({0xB0, 0x3C, 0x03}));
        QCOMPARE(t.nudges.last().second, 3.0);

        p.handle(msg({0x90, 0x10 + 4, 0x7F}));
        QCOMPARE(t.triggered.last(), QString("mixer.mute.5"));
        p.handle(msg({0x90, 0x5E, 0x7F}));
        QCOMPARE(t.triggered.last(), QString("playPause"));
    }

    void mackieFeedbackAndScribble()
    {
        FakeTargets t;
        ControlSurfaceManager m(&t);
        MackieProtocol p(&m, "midi:mcu");
        QList<QByteArray> out;
        p.send = [&](const QByteArray &b) { out << b; };

        p.feedback("fader2", 1.0);
        QCOMPARE(out.last(), msg({0xE1, 0x7F, 0x7F}));
        p.feedback("mute1", 1.0);
        QCOMPARE(out.last(), msg({0x90, 0x10, 0x7F}));
        p.feedback("solo3", 0.0);
        QCOMPARE(out.last(), msg({0x90, 0x08 + 2, 0x00}));
        p.feedback("play", 1.0);
        QCOMPARE(out.last(), msg({0x90, 0x5E, 0x7F}));
        p.feedback("record", 0.0);
        QCOMPARE(out.last(), msg({0x90, 0x5F, 0x00}));

        p.showLabels({"Music", "Voiceover"});
        const QByteArray sysex = out.last();
        QCOMPARE(static_cast<quint8>(sysex.at(0)), quint8(0xF0));
        QCOMPARE(static_cast<quint8>(sysex.at(sysex.size() - 1)), quint8(0xF7));
        QCOMPARE(sysex.mid(7, 7), QByteArray("Music  "));
        QCOMPARE(sysex.mid(14, 7), QByteArray("Voiceo "));
        QCOMPARE(sysex.size(), 7 + 56 + 1);
    }

    void mackieAnswersHostQuery()
    {
        FakeTargets t;
        ControlSurfaceManager m(&t);
        MackieProtocol p(&m, "midi:mcu");
        QList<QByteArray> out;
        p.send = [&](const QByteArray &b) { out << b; };
        const QByteArray query = msg({0xF0, 0, 0, 0x66, 0x14, 0x01, 1, 2, 3, 4, 5, 6, 7, 10, 20, 30, 40, 0xF7});
        QCOMPARE(query.size(), 18);
        p.handle(query);
        QCOMPARE(out.size(), 1);
        QCOMPARE(out.first().size(), 18);
        QCOMPARE(static_cast<quint8>(out.first().at(5)), quint8(0x02));
        QCOMPARE(out.first().mid(6, 7), query.mid(6, 7));
        for (char c : out.first().mid(6, 11))
            QVERIFY(static_cast<quint8>(c) < 0x80);
    }

    void huiPingFadersAndSwitches()
    {
        FakeTargets t;
        ControlSurfaceManager m(&t);
        HuiProtocol p(&m, "midi:hui");
        QList<QByteArray> out;
        p.send = [&](const QByteArray &b) { out << b; };
        m.registerDevice(p.describe("hui"));

        p.handle(msg({0x90, 0x00, 0x00}));
        QCOMPARE(out.last(), msg({0x90, 0x00, 0x7F}));

        p.handle(msg({0xB0, 0x01, 0x7F}));
        p.handle(msg({0xB0, 0x21, 0x7F}));
        QCOMPARE(t.sets.last().first, QString("mixer.volume.2"));
        QCOMPARE(t.sets.last().second, 1.0);

        p.handle(msg({0xB0, 0x0F, 0x03}));
        p.handle(msg({0xB0, 0x2F, 0x42}));
        QCOMPARE(t.triggered.last(), QString("mixer.mute.4"));
        p.handle(msg({0xB0, 0x0F, 0x0E}));
        p.handle(msg({0xB0, 0x2F, 0x43}));
        QCOMPARE(t.triggered.last(), QString("playPause"));

        out.clear();
        p.feedback("solo2", 1.0);
        QCOMPARE(out.size(), 2);
        QCOMPARE(out.at(0), msg({0xB0, 0x0C, 0x01}));
        QCOMPARE(out.at(1), msg({0xB0, 0x2C, 0x43}));
        out.clear();
        p.feedback("fader1", 1.0);
        QCOMPARE(out.at(0), msg({0xB0, 0x00, 0x7F}));
        QCOMPARE(out.at(1), msg({0xB0, 0x20, 0x7F}));
    }

    void serviceFollowsHotPlug()
    {
        FakeTargets t;
        t.values["mixer.volume.1"] = 0.5;
        ControlSurfaceManager m(&t);
        FakeIo io;
        io.ports = {"Behringer X-Touch"};
        MidiSurfaceService s(&m, &io);
        s.poll();
        QCOMPARE(io.opened, QStringList{"Behringer X-Touch"});
        QCOMPARE(s.protocolFor("Behringer X-Touch"), QString("mackie"));
        QCOMPARE(m.devices().size(), 1);
        QVERIFY(m.devices().first().toMap().value("connected").toBool());

        QTRY_VERIFY(!io.sent.isEmpty());
        bool fader = false;
        for (const auto &entry : io.sent)
            fader |= static_cast<quint8>(entry.second.at(0)) == 0xE0;
        QVERIFY(fader);

        emit io.received("Behringer X-Touch", msg({0xE0, 0x7F, 0x7F}));
        QCOMPARE(t.sets.last().first, QString("mixer.volume.1"));

        io.ports.clear();
        s.poll();
        QCOMPARE(io.closed, QStringList{"Behringer X-Touch"});
        QVERIFY(!m.devices().first().toMap().value("connected").toBool());

        io.ports = {"Launchpad"};
        s.poll();
        QCOMPARE(s.protocolFor("Launchpad"), QString("generic"));
        s.setProtocol("Launchpad", "hui");
        QCOMPARE(io.closed.size(), 2);
        QCOMPARE(io.opened.last(), QString("Launchpad"));
    }
};

QTEST_MAIN(TestMidiSurfaces)
#include "tst_midisurfaces.moc"
