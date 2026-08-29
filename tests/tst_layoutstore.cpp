#include <QtTest>

#include <QGuiApplication>
#include <QScopeGuard>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>

#include "models/LayoutStore.h"

class LayoutStoreTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void windowGeometryRoundTrip();
    void offscreenGeometryIsRejected();
    void geometrySmallerThanMinimumGrows();
    void maximizedFlagRoundTrip();
    void emptyGeometryKeepsThePreviousOne();
    void panelFractionRoundTrip();
    void panelFractionRejectsOutOfBandValues();

private:
    QString m_org;
    QString m_app;
};

// Every test writes real settings, so they go to a throwaway organisation under the
// test-mode config root rather than over the user's own Drift layout.
void LayoutStoreTest::init()
{
    m_org = QCoreApplication::organizationName();
    m_app = QCoreApplication::applicationName();
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("DriftTest"));
    QCoreApplication::setApplicationName(QStringLiteral("DriftTest"));
    QSettings().remove(QStringLiteral("ui/window"));
    QSettings().remove(QStringLiteral("ui/layout"));
}

void LayoutStoreTest::cleanup()
{
    QSettings().remove(QStringLiteral("ui/window"));
    QSettings().remove(QStringLiteral("ui/layout"));
    QCoreApplication::setOrganizationName(m_org);
    QCoreApplication::setApplicationName(m_app);
    QStandardPaths::setTestModeEnabled(false);
}

void LayoutStoreTest::windowGeometryRoundTrip()
{
    LayoutStore store;
    // Sized off the actual screen so the rect is reachable wherever this runs,
    // including the offscreen platform plugin in CI.
    const QRect available = QGuiApplication::primaryScreen()->availableGeometry();
    const QRect saved(available.x() + 20, available.y() + 20,
                      available.width() / 2, available.height() / 2);

    store.saveWindowState(saved, false);
    QCOMPARE(store.savedWindowGeometry(200, 150), saved);
    QVERIFY(!store.savedWindowMaximized());
}

void LayoutStoreTest::offscreenGeometryIsRejected()
{
    LayoutStore store;
    // The monitor this was saved on is gone: restoring it would open the window
    // somewhere the user cannot reach, so the defaults have to win instead.
    store.saveWindowState(QRect(100000, 100000, 1280, 800), false);
    QVERIFY(store.savedWindowGeometry(200, 150).isEmpty());
}

void LayoutStoreTest::geometrySmallerThanMinimumGrows()
{
    LayoutStore store;
    const QRect available = QGuiApplication::primaryScreen()->availableGeometry();
    store.saveWindowState(QRect(available.x(), available.y(), 100, 80), false);

    const QRect restored = store.savedWindowGeometry(400, 300);
    QCOMPARE(restored.width(), 400);
    QCOMPARE(restored.height(), 300);
}

void LayoutStoreTest::maximizedFlagRoundTrip()
{
    LayoutStore store;
    const QRect available = QGuiApplication::primaryScreen()->availableGeometry();
    const QRect windowed(available.x(), available.y(), 800, 600);

    store.saveWindowState(windowed, true);
    QVERIFY(store.savedWindowMaximized());
    // A maximized session still remembers the size to restore down to.
    QCOMPARE(store.savedWindowGeometry(200, 150), windowed);

    store.saveWindowState(windowed, false);
    QVERIFY(!store.savedWindowMaximized());
}

void LayoutStoreTest::emptyGeometryKeepsThePreviousOne()
{
    LayoutStore store;
    const QRect available = QGuiApplication::primaryScreen()->availableGeometry();
    const QRect windowed(available.x(), available.y(), 800, 600);
    store.saveWindowState(windowed, false);

    // A session spent entirely maximized never samples a windowed rect. That must
    // not erase the one from the session before it.
    store.saveWindowState(QRect(), true);
    QCOMPARE(store.savedWindowGeometry(200, 150), windowed);
    QVERIFY(store.savedWindowMaximized());
}

void LayoutStoreTest::panelFractionRoundTrip()
{
    LayoutStore store;
    QCOMPARE(store.panelFraction(QStringLiteral("landscape/assets"), 0.25), 0.25);

    store.setPanelFraction(QStringLiteral("landscape/assets"), 0.4);
    QCOMPARE(store.panelFraction(QStringLiteral("landscape/assets"), 0.25), 0.4);
    // Each workspace keeps its own arrangement.
    QCOMPARE(store.panelFraction(QStringLiteral("portrait/assets"), 0.5), 0.5);
}

void LayoutStoreTest::panelFractionRejectsOutOfBandValues()
{
    LayoutStore store;
    // A panel restored as a sliver or as the whole window is a layout the user
    // cannot drag their way out of, so neither is written or served.
    store.setPanelFraction(QStringLiteral("landscape/properties"), 0.0);
    QCOMPARE(store.panelFraction(QStringLiteral("landscape/properties"), 0.25), 0.25);

    store.setPanelFraction(QStringLiteral("landscape/properties"), 1.5);
    QCOMPARE(store.panelFraction(QStringLiteral("landscape/properties"), 0.25), 0.25);

    // And a value already on disk from an older build is discarded on read.
    QSettings().setValue(QStringLiteral("ui/layout/landscape/properties"), 0.99);
    QCOMPARE(store.panelFraction(QStringLiteral("landscape/properties"), 0.25), 0.25);
}

QTEST_MAIN(LayoutStoreTest)
#include "tst_layoutstore.moc"
