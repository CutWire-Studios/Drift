#include <QtTest>

#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QWheelEvent>
#include <memory>

// The toast card's input floor (ToastInputShield.qml, #574): presses, drags and wheel over it must
// not reach a preview-like scene underneath, and the swipe must still be reported.
class ToastInputTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void harnessReachesBelow();
    void pressAndDragStayOnTheToast();
    void wheelStaysOnTheToast();
    void disabledSwipeKeepsTheDrag();
    void clickStaysOnTheToast();
    void otherButtonsStayOnTheToast();

private:
    int belowInt(const char *name) const { return m_scene->property(name).toInt(); }
    void dragFrom(const QPoint &start, Qt::MouseButton button = Qt::LeftButton);
    void dragInside() { dragFrom(m_inside); }

    std::unique_ptr<QQmlEngine> m_engine;
    std::unique_ptr<QQuickWindow> m_window;
    QQuickItem *m_scene = nullptr;
    QQuickItem *m_shield = nullptr;
    const QPoint m_inside{150, 130};
    const QPoint m_outside{20, 20};
};

void ToastInputTest::init()
{
    m_engine = std::make_unique<QQmlEngine>();
    m_window = std::make_unique<QQuickWindow>();
    m_window->resize(400, 300);

    QQmlComponent sceneComponent(m_engine.get());
    sceneComponent.setData(R"(
import QtQuick
Item {
    id: root
    width: 400; height: 300
    property int belowPresses: 0
    property int belowWheels: 0
    property int belowTaps: 0
    property bool belowDragged: false
    Item {
        anchors.fill: parent
        TapHandler { onTapped: root.belowTaps++ }
        // A property of its own makes it its own QML type, as the preview's bodyDrag and
        // marqueeDrag are, so Qt counts it as a different handler type from the swipe and, by
        // default, lets it take the drag from it. That is how the clip moved under a toast.
        DragHandler {
            target: null
            property bool started: false
            onActiveChanged: if (active) root.belowDragged = true
        }
        WheelHandler { onWheel: root.belowWheels++ }
    }
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onPressed: root.belowPresses++
        onWheel: (wheel) => { root.belowWheels++; wheel.accepted = true }
    }
}
)", QUrl());
    m_scene = qobject_cast<QQuickItem *>(sceneComponent.create());
    QVERIFY2(m_scene, qPrintable(sceneComponent.errorString()));
    m_scene->setParentItem(m_window->contentItem());

    QQmlComponent shieldComponent(m_engine.get(),
                                  QUrl::fromLocalFile(QStringLiteral(DRIFT_TOAST_INPUT_SHIELD_QML)));
    m_shield = qobject_cast<QQuickItem *>(shieldComponent.create());
    QVERIFY2(m_shield, qPrintable(shieldComponent.errorString()));
    m_shield->setParentItem(m_scene);
    m_shield->setPosition(QPointF(100, 100));
    m_shield->setSize(QSizeF(200, 60));
    m_shield->setZ(10);

    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
}

void ToastInputTest::cleanup()
{
    m_window.reset();
    m_engine.reset();
}

void ToastInputTest::dragFrom(const QPoint &start, Qt::MouseButton button)
{
    QTest::mousePress(m_window.get(), button, Qt::NoModifier, start);
    for (int i = 1; i <= 10; ++i)
        QTest::mouseMove(m_window.get(), start + QPoint(i * 10, 0));
    QTest::mouseRelease(m_window.get(), button, Qt::NoModifier, start + QPoint(100, 0));
}

void ToastInputTest::harnessReachesBelow()
{
    QTest::mouseClick(m_window.get(), Qt::LeftButton, Qt::NoModifier, m_outside);
    QCOMPARE(belowInt("belowPresses"), 1);
    // The stand-in's DragHandler takes a drag that starts beside the toast.
    dragFrom(m_outside);
    QVERIFY(m_scene->property("belowDragged").toBool());
}

void ToastInputTest::pressAndDragStayOnTheToast()
{
    QSignalSpy moved(m_shield, SIGNAL(swipeMoved(qreal, qreal)));
    QSignalSpy released(m_shield, SIGNAL(swipeReleased(qreal, qreal)));
    dragInside();
    QCOMPARE(belowInt("belowPresses"), 0);
    QVERIFY(!m_scene->property("belowDragged").toBool());
    QVERIFY(moved.count() > 0);
    QVERIFY(moved.last().at(0).toDouble() > 50);
    QCOMPARE(released.count(), 1);
}

void ToastInputTest::wheelStaysOnTheToast()
{
    auto wheel = [this](const QPoint &at) {
        QWheelEvent event(QPointF(at), QPointF(m_window->mapToGlobal(at)), QPoint(), QPoint(0, 120),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QGuiApplication::sendEvent(m_window.get(), &event);
    };
    wheel(m_inside);
    QCOMPARE(belowInt("belowWheels"), 0);
    wheel(m_outside);
    QVERIFY(belowInt("belowWheels") > 0);
}

void ToastInputTest::disabledSwipeKeepsTheDrag()
{
    m_shield->setProperty("swipeEnabled", false);
    QSignalSpy released(m_shield, SIGNAL(swipeReleased(qreal, qreal)));
    dragInside();
    QVERIFY(!m_scene->property("belowDragged").toBool());
    QCOMPARE(belowInt("belowPresses"), 0);
    QCOMPARE(released.count(), 0);
}

void ToastInputTest::clickStaysOnTheToast()
{
    QTest::mouseClick(m_window.get(), Qt::LeftButton, Qt::NoModifier, m_inside);
    QCOMPARE(belowInt("belowTaps"), 0);
}

// Right and middle presses (and drags) over the card don't reach the preview either.
void ToastInputTest::otherButtonsStayOnTheToast()
{
    dragFrom(m_inside, Qt::RightButton);
    QTest::mouseClick(m_window.get(), Qt::MiddleButton, Qt::NoModifier, m_inside);
    QCOMPARE(belowInt("belowPresses"), 0);
}

QTEST_MAIN(ToastInputTest)
#include "tst_toastinput.moc"
