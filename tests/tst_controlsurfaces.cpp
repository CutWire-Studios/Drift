#include "app/surfaces/ControlSurfaceManager.h"
#include "app/surfaces/FaderScale.h"

#include <QCoreApplication>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

#include <cmath>

using namespace drift;

namespace {

class FakeTargets : public SurfaceTargets {
public:
    QList<SurfaceTarget> targets() const override
    {
        return {{"vol", "Volume", "Mixer", SurfaceTarget::Absolute},
                {"jog", "Jog", "Playhead", SurfaceTarget::Relative},
                {"play", "Play", "Actions", SurfaceTarget::Action},
                {"mix.mute", "Mute", "Mixer", SurfaceTarget::Action}};
    }
    SurfaceTarget describe(const QString &id) const override
    {
        for (const SurfaceTarget &t : targets()) {
            if (t.id == id)
                return t;
        }
        return {id, id, {}, SurfaceTarget::Action};
    }
    bool trigger(const QString &id) override
    {
        triggered << id;
        return id.startsWith(QLatin1String("mix."));
    }
    void setAbsolute(const QString &id, double n) override
    {
        Q_UNUSED(id)
        value = n;
        sets << n;
    }
    void nudge(const QString &id, double ticks) override
    {
        Q_UNUSED(id)
        nudges << ticks;
    }
    double current(const QString &id) const override { return id == QLatin1String("vol") ? value : std::nan(""); }

    double value = 0.8;
    QList<double> sets;
    QList<double> nudges;
    QStringList triggered;
};

SurfaceDevice device(const QString &id = QStringLiteral("dev"))
{
    SurfaceDevice d;
    d.id = id;
    d.name = QStringLiteral("Test device");
    d.controls = {{"btn", "Button", SurfaceControl::Button},
                  {"fader", "Fader", SurfaceControl::Absolute},
                  {"enc", "Encoder", SurfaceControl::Relative}};
    return d;
}

} // namespace

class TestControlSurfaces : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("drift-test"));
        QCoreApplication::setApplicationName(QStringLiteral("surfaces"));
        QSettings().clear();
    }

    void buttonTriggersAction()
    {
        FakeTargets t;
        ControlSurfaceManager m(&t);
        QSignalSpy requested(&m, &ControlSurfaceManager::actionRequested);
        m.registerDevice(device());
        m.bind("dev", "btn", "play");
        m.button("dev", "btn", true);
        m.button("dev", "btn", false);
        QCOMPARE(t.triggered, QStringList{"play"});
        QCOMPARE(requested.count(), 1);
        m.bind("dev", "btn", "mix.mute");
        m.button("dev", "btn", true);
        QCOMPARE(requested.count(), 1);
    }

    void absolutePicksUpBeforeFollowing()
    {
        FakeTargets t;
        ControlSurfaceManager m(&t);
        m.registerDevice(device());
        m.bind("dev", "fader", "vol");
        m.absolute("dev", "fader", 0.1);
        m.absolute("dev", "fader", 0.3);
        QVERIFY(t.sets.isEmpty());
        m.absolute("dev", "fader", 0.85);
        QCOMPARE(t.sets.size(), 1);
        m.absolute("dev", "fader", 0.9);
        QCOMPARE(t.sets.size(), 2);
        QCOMPARE(t.sets.last(), 0.9);
        t.value = 0.2;
        m.resetPickup();
        m.absolute("dev", "fader", 0.9);
        QCOMPARE(t.sets.size(), 2);
    }

    void motorizedFollowsImmediately()
    {
        FakeTargets t;
        ControlSurfaceManager m(&t);
        SurfaceDevice d = device();
        d.motorized = true;
        m.registerDevice(d);
        m.bind("dev", "fader", "vol");
        m.absolute("dev", "fader", 0.1);
        QCOMPARE(t.sets.size(), 1);
    }

    void relativeMapsToTargets()
    {
        FakeTargets t;
        ControlSurfaceManager m(&t);
        m.registerDevice(device());
        m.bind("dev", "enc", "jog");
        m.setBindingOption("dev", "enc", "sensitivity", 2.0);
        m.relative("dev", "enc", 3);
        QCOMPARE(t.nudges, QList<double>{6.0});
        m.setBindingOption("dev", "enc", "invert", true);
        m.relative("dev", "enc", 1);
        QCOMPARE(t.nudges.last(), -2.0);
        m.bind("dev", "enc", "vol");
        t.value = 0.5;
        m.relative("dev", "enc", 5);
        QVERIFY(std::abs(t.sets.last() - (0.5 - 5 * 0.01 * 2.0)) < 1e-9);
    }

    void encoderModesReadCounters()
    {
        FakeTargets t;
        ControlSurfaceManager m(&t);
        m.registerDevice(device());
        m.bind("dev", "fader", "jog");
        const auto raw = [&](int value) { m.absolute("dev", "fader", value / 127.0); };

        m.setBindingOption("dev", "fader", "encoder", "twos");
        raw(1);
        QCOMPARE(t.nudges.last(), 1.0);
        raw(127);
        QCOMPARE(t.nudges.last(), -1.0);
        raw(3);
        QCOMPARE(t.nudges.last(), 3.0);

        m.setBindingOption("dev", "fader", "encoder", "signmag");
        raw(0x41);
        QCOMPARE(t.nudges.last(), -1.0);
        raw(0x02);
        QCOMPARE(t.nudges.last(), 2.0);

        m.setBindingOption("dev", "fader", "encoder", "offset");
        raw(65);
        QCOMPARE(t.nudges.last(), 1.0);
        raw(63);
        QCOMPARE(t.nudges.last(), -1.0);
        const int before = t.nudges.size();
        raw(64);
        QCOMPARE(t.nudges.size(), before);

        QVERIFY(m.exportJson("dev").contains("\"encoder\": \"offset\""));
    }

    void absoluteToRelativeUsesTravel()
    {
        FakeTargets t;
        ControlSurfaceManager m(&t);
        m.registerDevice(device());
        m.bind("dev", "fader", "jog");
        m.absolute("dev", "fader", 0.5);
        m.absolute("dev", "fader", 0.52);
        QCOMPARE(t.nudges.size(), 1);
        QVERIFY(std::abs(t.nudges.first() - 2.0) < 1e-9);
    }

    void learnBindsNextControl()
    {
        FakeTargets t;
        ControlSurfaceManager m(&t);
        QSignalSpy learned(&m, &ControlSurfaceManager::learned);
        m.registerDevice(device());
        m.armLearn("vol", "Volume");
        QCOMPARE(m.learnTarget(), QString("vol"));
        m.button("dev", "btn", false);
        QCOMPARE(learned.count(), 0);
        m.absolute("dev", "fader", 0.4);
        QCOMPARE(learned.count(), 1);
        QVERIFY(m.learnTarget().isEmpty());
        QVERIFY(t.sets.isEmpty());
        const QVariantList b = m.bindings("dev");
        QCOMPARE(b.size(), 1);
        QCOMPARE(b.first().toMap().value("target").toString(), QString("vol"));
        m.removeBinding("dev", "fader");
        QCOMPARE(m.bindings("dev").size(), 0);
    }

    void jsonRoundTripAndPersistence()
    {
        FakeTargets t;
        QString json;
        {
            ControlSurfaceManager m(&t);
            m.registerDevice(device());
            m.bind("dev", "fader", "vol");
            m.bind("dev", "btn", "play");
            m.setBindingOption("dev", "fader", "invert", true);
            json = m.exportJson("dev");
        }
        ControlSurfaceManager reloaded(&t);
        QCOMPARE(reloaded.bindings("dev").size(), 2);
        QVERIFY(reloaded.bindings("dev").first().toMap().value("invert").toBool());

        ControlSurfaceManager other(&t);
        other.registerDevice(device("other"));
        QCOMPARE(other.importJson(json, "other"), 2);
        QCOMPARE(other.bindings("other").size(), 2);
        QCOMPARE(other.importJson("not json"), -1);
    }

    void defaultsApplyOnce()
    {
        FakeTargets t;
        SurfaceDevice d = device("mcu");
        d.defaults = {{"fader", "vol", "Volume", false, 1.0}};
        {
            ControlSurfaceManager m(&t);
            m.registerDevice(d);
            QCOMPARE(m.bindings("mcu").size(), 1);
            m.clearBindings("mcu");
        }
        ControlSurfaceManager again(&t);
        again.registerDevice(d);
        QCOMPARE(again.bindings("mcu").size(), 0);
    }

    void feedbackReportsChangesOnly()
    {
        FakeTargets t;
        ControlSurfaceManager m(&t);
        SurfaceDevice d = device();
        d.feedback = true;
        QSignalSpy fb(&m, &ControlSurfaceManager::feedback);
        m.registerDevice(d);
        m.bind("dev", "fader", "vol");
        QTRY_VERIFY(fb.count() >= 1);
        const int n = fb.count();
        QTest::qWait(150);
        QCOMPARE(fb.count(), n);
        t.value = 0.3;
        QTRY_COMPARE(fb.count(), n + 1);
        QCOMPARE(fb.last().at(2).toDouble(), 0.3);
    }

    void faderScale()
    {
        QCOMPARE(surface::gainFromFader(0.0), 0.0);
        QVERIFY(std::abs(surface::gainFromFader(0.75) - 1.0) < 1e-9);
        QVERIFY(std::abs(surface::gainFromFader(1.0) - std::pow(10.0, 6.0 / 20.0)) < 1e-9);
        for (double f : {0.1, 0.4, 0.75, 0.9})
            QVERIFY(std::abs(surface::faderFromGain(surface::gainFromFader(f)) - f) < 1e-9);
    }
};

QTEST_MAIN(TestControlSurfaces)
#include "tst_controlsurfaces.moc"
