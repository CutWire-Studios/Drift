#include <QtTest>

#include <QColor>
#include <QDir>
#include <QByteArray>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariantList>

#include <zlib.h>

#include "ui/theme/ThemeManager.h"

class ThemeManagerTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void builtinsLoadDarkAndLight();
    void graphiteAndFrostAreBuiltins();
    void resolvesReferences();
    void extendsMergesOverBase();
    void referenceCycleDoesNotHang();
    void extendsCycleDoesNotHang();
    void invalidThemeFallsBack();
    void singletonRegistrationExposesThemeManager();
    void mainRegistersThemeManager();
    void liveBindingUpdates();
    void componentTokenUpdatesLive();
    void themeSlotOverrideFollowsTrust();
    void untrustedThemeHasNoOverride();
    void trustedThemeOverrideUrl();
    void missingOverrideSlotIsEmpty();
    void selectionPersistsAndFallsBack();
    void hotReloadPersists();
    void dualVariantFollowsToggle();
    void singleVariantLocksAppearance();
    void importJsonFolderZipAndRejectsTraversal();
    void removeAndExportRoundTrip();
    void exampleThemesLoad();
    void schemaVersionMigratesAndWarns();
    void templateExportAndReplaceImport();
    void sessionOverrideIsNotPersisted();
    void darkModeBindingDoesNotLoop();

private:
    QString themesDir() const;
    bool writeJson(const QString &relativePath, const char *json) const;
    bool installOverrideTheme() const;
    static QColor tokenColor(const ThemeManager &manager, const char *path);
    // qmlRegisterSingletonInstance pins ThemeManager to the first engine that
    // touches it. Later engines get a null singleton, so every QML case shares this one.
    static QQmlEngine *sharedEngine();
};

void ThemeManagerTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("DriftThemeTest"));
    QCoreApplication::setApplicationName(QStringLiteral("DriftThemeTest"));
}

void ThemeManagerTest::init()
{
    QDir dir(themesDir());
    if (dir.exists())
        QVERIFY(dir.removeRecursively());
    QSettings settings;
    settings.beginGroup(QStringLiteral("Theme"));
    settings.remove(QStringLiteral("trusted"));
    settings.remove(QStringLiteral("selection"));
    settings.remove(QStringLiteral("hotReload"));
    settings.sync();
}

QString ThemeManagerTest::themesDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/themes");
}

bool ThemeManagerTest::writeJson(const QString &relativePath, const char *json) const
{
    const QString path = themesDir() + QLatin1Char('/') + relativePath;
    if (!QDir().mkpath(QFileInfo(path).path()))
        return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const QByteArray bytes(json);
    return file.write(bytes) == bytes.size();
}

bool ThemeManagerTest::installOverrideTheme() const
{
    if (!writeJson("pack/theme.json", R"({
        "name": "pack",
        "extends": "dark"
    })"))
        return false;
    const QString qmlPath = themesDir() + QStringLiteral("/pack/qml/ThemedButtonBackground.qml");
    if (!QDir().mkpath(QFileInfo(qmlPath).path()))
        return false;
    QFile file(qmlPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const QByteArray body = "import QtQuick\nItem { property var control }\n";
    return file.write(body) == body.size();
}

QColor ThemeManagerTest::tokenColor(const ThemeManager &manager, const char *path)
{
    return manager.token(QString::fromLatin1(path)).value<QColor>();
}

QQmlEngine *ThemeManagerTest::sharedEngine()
{
    static QQmlEngine *engine = new QQmlEngine;
    return engine;
}

void ThemeManagerTest::builtinsLoadDarkAndLight()
{
    ThemeManager manager;
    QCOMPARE(manager.currentTheme(), QStringLiteral("dark"));
    QVERIFY(manager.availableThemes().contains(QStringLiteral("dark")));
    QVERIFY(manager.availableThemes().contains(QStringLiteral("light")));
    QCOMPARE(tokenColor(manager, "semantic.surface.app"), QColor(QStringLiteral("#0d0d0d")));
    QCOMPARE(tokenColor(manager, "semantic.primary"), QColor(QStringLiteral("#F8B81C")));
    QCOMPARE(tokenColor(manager, "semantic.scrim"), QColor(QStringLiteral("#66000000")));
    QCOMPARE(tokenColor(manager, "semantic.clip.audio"), QColor(QStringLiteral("#8F5DBA")));
    QCOMPARE(tokenColor(manager, "semantic.gizmo.x"), QColor(QStringLiteral("#f0475a")));
    QCOMPARE(tokenColor(manager, "semantic.clip.waveform"), QColor(QStringLiteral("#ffffffb3")));
    QCOMPARE(tokenColor(manager, "semantic.panel.sliderTrack"), QColor(QStringLiteral("#505050")));
    QCOMPARE(manager.token(QStringLiteral("metrics.radius.sm")).toDouble(), 5.6);
    QCOMPARE(manager.token(QStringLiteral("metrics.controlHeight.pointer")).toDouble(), 30.0);
    QCOMPARE(manager.token(QStringLiteral("metrics.controlHeight.touch")).toDouble(), 44.0);
    QCOMPARE(manager.token(QStringLiteral("motion.easing")).toString(), QStringLiteral("OutQuint"));
    QCOMPARE(manager.token(QStringLiteral("motion.easingInOut")).toString(), QStringLiteral("InOutQuart"));
    QCOMPARE(manager.token(QStringLiteral("typography.fontSize.base")).toDouble(), 14.72);
    QCOMPARE(manager.token(QStringLiteral("typography.fontWeight.semibold")).toInt(), 600);
    QCOMPARE(manager.token(QStringLiteral("window.translucent")).toBool(), false);
    QCOMPARE(manager.token(QStringLiteral("window.backdrop")).toString(), QStringLiteral("none"));
    QCOMPARE(manager.token(QStringLiteral("window.backgroundOpacity")).toDouble(), 1.0);
    // Built-in documents carry one group per visual component. A missing key
    // stays undefined in QML and never starts updating, so the map must not be empty.
    QVERIFY(!manager.components()->isEmpty());
    QCOMPARE(tokenColor(manager, "components.themedButton.background"), QColor(QStringLiteral("#262626")));
    QCOMPARE(tokenColor(manager, "components.appWindow.background"), QColor(QStringLiteral("#0d0d0d")));
    QVERIFY(manager.overrideUrl(QStringLiteral("button")).isEmpty());
    QVERIFY(manager.overrideSlots(QStringLiteral("dark")).isEmpty());
    QVERIFY(!manager.isThemeTrusted(QStringLiteral("dark")));

    const QVariantList ramp = manager.token(QStringLiteral("semantic.keyframe.ramp")).toList();
    QCOMPARE(ramp.size(), 8);
    QCOMPARE(ramp.at(0).value<QColor>(), QColor(QStringLiteral("#38bdf8")));
    QCOMPARE(ramp.at(1).value<QColor>(), QColor(QStringLiteral("#fb923c")));

    QSignalSpy spy(&manager, &ThemeManager::themeChanged);
    manager.setVariant(QStringLiteral("light"));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(manager.currentTheme(), QStringLiteral("light"));
    QCOMPARE(tokenColor(manager, "semantic.surface.app"), QColor(QStringLiteral("#ffffff")));
    QCOMPARE(tokenColor(manager, "semantic.panel.sliderTrack"), tokenColor(manager, "semantic.panel.muted"));
    QCOMPARE(tokenColor(manager, "semantic.panel.muted"), QColor(QStringLiteral("#d4d4d4")));
    QCOMPARE(tokenColor(manager, "semantic.accentOnPanel"), QColor(QStringLiteral("#8a6300")));
    QCOMPARE(tokenColor(manager, "semantic.panel.scrollbar.handlePressed"), tokenColor(manager, "semantic.foreground"));
    QCOMPARE(tokenColor(manager, "semantic.skeleton"), QColor(QStringLiteral("#e8e8e8")));
    QCOMPARE(tokenColor(manager, "semantic.clip.beat.bar"), QColor(QStringLiteral("#1f6fb24d")));
    QCOMPARE(tokenColor(manager, "semantic.preview.textStyleBorder"), QColor(QStringLiteral("#2a2a2a")));
    // Built-in documents stay readable after the live theme swaps.
    QCOMPARE(manager.builtinToken(QStringLiteral("dark"), QStringLiteral("semantic.surface.app")).value<QColor>(),
             QColor(QStringLiteral("#0d0d0d")));

    manager.setVariant(QStringLiteral("light"));
    QCOMPARE(spy.count(), 1);
    manager.setVariant(QStringLiteral("nope"));
    QCOMPARE(manager.currentTheme(), QStringLiteral("light"));
}

void ThemeManagerTest::graphiteAndFrostAreBuiltins()
{
    QVERIFY(writeJson("graphite-copy/theme.json", R"({
        "name": "graphite",
        "extends": "dark"
    })"));
    ThemeManager manager;
    const QStringList available = manager.availableThemes();
    QCOMPARE(available.mid(0, 4), (QStringList{QStringLiteral("dark"), QStringLiteral("light"),
                                               QStringLiteral("graphite"), QStringLiteral("frost")}));
    QCOMPARE(available.count(QStringLiteral("graphite")), 1);

    QStringList builtInIds;
    for (const QVariant &row : manager.themes()) {
        const QVariantMap map = row.toMap();
        if (map.value(QStringLiteral("builtIn")).toBool())
            builtInIds.append(map.value(QStringLiteral("id")).toString());
        else
            QVERIFY(map.value(QStringLiteral("id")).toString() != QLatin1String("graphite"));
    }
    QCOMPARE(builtInIds, (QStringList{QStringLiteral("dark"), QStringLiteral("light"),
                                      QStringLiteral("graphite"), QStringLiteral("frost")}));

    const struct {
        const char *id;
        const char *variant;
        const char *primary;
    } cases[] = {{"graphite", "dark", "#f2f2f2"}, {"frost", "light", "#111111"}};
    for (const auto &c : cases) {
        const QString id = QString::fromLatin1(c.id);
        QVERIFY(manager.select(id));
        QCOMPARE(manager.selection(), id);
        QCOMPARE(manager.currentTheme(), id);
        QCOMPARE(manager.effectiveVariant(), QString::fromLatin1(c.variant));
        QCOMPARE(manager.token(QStringLiteral("window.translucent")).toBool(), true);
        QCOMPARE(tokenColor(manager, "semantic.primary"), QColor(QString::fromLatin1(c.primary)));
        QVERIFY(!manager.previewColors(id).isEmpty());
        manager.setVariant(QStringLiteral("light"));
        QCOMPARE(manager.currentTheme(), id);
        manager.setVariant(QStringLiteral("dark"));
        QCOMPARE(manager.currentTheme(), id);
        QVERIFY(!manager.removeTheme(id));
    }
}

void ThemeManagerTest::resolvesReferences()
{
    ThemeManager manager;
    QCOMPARE(tokenColor(manager, "semantic.focusRing"), tokenColor(manager, "semantic.primary"));
    QCOMPARE(tokenColor(manager, "semantic.clip.adjustment.video"), tokenColor(manager, "semantic.clip.effect"));
    QCOMPARE(tokenColor(manager, "semantic.clip.effect"), QColor(QStringLiteral("#5d93ba")));
    QCOMPARE(tokenColor(manager, "semantic.keyframe.curves.positionX"), QColor(QStringLiteral("#16a9f3")));
    QCOMPARE(tokenColor(manager, "semantic.keyframe.curves.volume"), QColor(QStringLiteral("#2dd4bf")));
    QCOMPARE(tokenColor(manager, "semantic.accentOnPanel"), tokenColor(manager, "semantic.primary"));

    manager.setVariant(QStringLiteral("light"));
    QCOMPARE(tokenColor(manager, "semantic.focusRing"), tokenColor(manager, "semantic.primary"));
    QCOMPARE(tokenColor(manager, "semantic.panel.scrollbar.track"), tokenColor(manager, "semantic.panel.border"));
    QCOMPARE(tokenColor(manager, "semantic.panel.scrollbar.handle"), tokenColor(manager, "semantic.panel.muted"));
    QCOMPARE(tokenColor(manager, "semantic.panel.scrollbar.handleHover"), tokenColor(manager, "semantic.mutedForeground"));
}

void ThemeManagerTest::extendsMergesOverBase()
{
    QVERIFY(writeJson("base.json", R"({
        "name": "base",
        "variant": "dark",
        "palette": { "red": "#ff0000", "blue": "#0000ff" },
        "semantic": { "danger": "$palette.red", "info": "$palette.blue" },
        "metrics": { "size": 10, "nested": { "a": 1, "b": 2 } },
        "components": {}
    })"));
    QVERIFY(writeJson("child.json", R"({
        "name": "child",
        "extends": "base",
        "palette": { "red": "#00ff00" },
        "metrics": { "nested": { "b": 9 } },
        "components": { "button": { "primary": { "background": "$semantic.danger" } } }
    })"));
    QVERIFY(writeJson("packaged/theme.json", R"({
        "name": "packaged",
        "extends": "base",
        "palette": { "blue": "#0000aa" }
    })"));

    ThemeManager manager;
    const QStringList themes = manager.availableThemes();
    QVERIFY(themes.contains(QStringLiteral("base")));
    QVERIFY(themes.contains(QStringLiteral("child")));
    QVERIFY(themes.contains(QStringLiteral("packaged")));

    QVERIFY(manager.loadTheme(QStringLiteral("child")));
    QCOMPARE(manager.currentTheme(), QStringLiteral("child"));
    QCOMPARE(tokenColor(manager, "semantic.danger"), QColor(QStringLiteral("#00ff00")));
    QCOMPARE(tokenColor(manager, "semantic.info"), QColor(QStringLiteral("#0000ff")));
    QCOMPARE(manager.token(QStringLiteral("metrics.size")).toDouble(), 10.0);
    QCOMPARE(manager.token(QStringLiteral("metrics.nested.a")).toDouble(), 1.0);
    QCOMPARE(manager.token(QStringLiteral("metrics.nested.b")).toDouble(), 9.0);
    QCOMPARE(tokenColor(manager, "components.button.primary.background"), QColor(QStringLiteral("#00ff00")));

    QVERIFY(manager.loadTheme(themesDir() + QStringLiteral("/packaged")));
    QCOMPARE(manager.currentTheme(), QStringLiteral("packaged"));
    QCOMPARE(tokenColor(manager, "semantic.info"), QColor(QStringLiteral("#0000aa")));
    QCOMPARE(tokenColor(manager, "semantic.danger"), QColor(QStringLiteral("#ff0000")));

    // A user theme stays put when the app toggles dark/light.
    manager.setVariant(QStringLiteral("light"));
    QCOMPARE(manager.currentTheme(), QStringLiteral("packaged"));
    QCOMPARE(tokenColor(manager, "semantic.info"), QColor(QStringLiteral("#0000aa")));
}

void ThemeManagerTest::referenceCycleDoesNotHang()
{
    QVERIFY(writeJson("refcycle.json", R"({
        "name": "refcycle",
        "variant": "dark",
        "palette": { "a": "$palette.b", "b": "$palette.a", "ok": "#112233" },
        "semantic": { "fine": "$palette.ok", "bad": "$palette.a" }
    })"));

    ThemeManager manager;
    QElapsedTimer timer;
    timer.start();
    QVERIFY(manager.loadTheme(QStringLiteral("refcycle")));
    QVERIFY(timer.elapsed() < 1000);
    QCOMPARE(manager.currentTheme(), QStringLiteral("refcycle"));
    QCOMPARE(tokenColor(manager, "semantic.fine"), QColor(QStringLiteral("#112233")));
    QVERIFY(manager.token(QStringLiteral("semantic.bad")).typeId() == QMetaType::QString);
}

void ThemeManagerTest::extendsCycleDoesNotHang()
{
    QVERIFY(writeJson("cycleA.json", R"({
        "name": "cycleA",
        "extends": "cycleB",
        "palette": { "red": "#ff0000" }
    })"));
    QVERIFY(writeJson("cycleB.json", R"({
        "name": "cycleB",
        "extends": "cycleA",
        "palette": { "red": "#00ff00" }
    })"));

    ThemeManager manager;
    QCOMPARE(tokenColor(manager, "semantic.primary"), QColor(QStringLiteral("#F8B81C")));
    QElapsedTimer timer;
    timer.start();
    QVERIFY(!manager.loadTheme(QStringLiteral("cycleA")));
    QVERIFY(timer.elapsed() < 1000);
    QCOMPARE(manager.currentTheme(), QStringLiteral("dark"));
    QCOMPARE(tokenColor(manager, "semantic.primary"), QColor(QStringLiteral("#F8B81C")));
}

void ThemeManagerTest::invalidThemeFallsBack()
{
    ThemeManager manager;
    QSignalSpy spy(&manager, &ThemeManager::themeChanged);
    QVERIFY(!manager.loadTheme(QStringLiteral("/no/such/drift-theme.json")));
    QVERIFY(!manager.loadTheme(QStringLiteral("")));
    QCOMPARE(spy.count(), 0);
    QCOMPARE(manager.currentTheme(), QStringLiteral("dark"));

    QVERIFY(writeJson("broken.json", "{"));
    QVERIFY(!manager.loadTheme(QStringLiteral("broken")));
    QCOMPARE(manager.currentTheme(), QStringLiteral("dark"));
    QCOMPARE(tokenColor(manager, "semantic.surface.app"), QColor(QStringLiteral("#0d0d0d")));

    QVERIFY(writeJson("wrong-type.json", R"({
        "name": "wrong-type",
        "palette": ["not", "an", "object"]
    })"));
    QVERIFY(!manager.loadTheme(QStringLiteral("wrong-type")));
    QCOMPARE(manager.currentTheme(), QStringLiteral("dark"));
    QCOMPARE(tokenColor(manager, "semantic.primary"), QColor(QStringLiteral("#F8B81C")));

    QVERIFY(writeJson("bad-color.json", R"({
        "name": "bad-color",
        "variant": "dark",
        "palette": { "x": "#gg0000", "ok": "#abcdef" },
        "semantic": { "fine": "$palette.ok" }
    })"));
    QVERIFY(manager.loadTheme(QStringLiteral("bad-color")));
    QCOMPARE(tokenColor(manager, "semantic.fine"), QColor(QStringLiteral("#abcdef")));
    manager.setVariant(QStringLiteral("dark"));
    // The bad theme is a user theme, so the variant request must not clobber it.
    QCOMPARE(manager.currentTheme(), QStringLiteral("bad-color"));
    QVERIFY(manager.loadTheme(QStringLiteral("dark")));
    QCOMPARE(manager.currentTheme(), QStringLiteral("dark"));
}

void ThemeManagerTest::singletonRegistrationExposesThemeManager()
{
    // Same call main() makes. This test does not register the type itself.
    ThemeManager::registerSingleton();
    QVERIFY(ThemeManager::shared()->loadTheme(QStringLiteral("dark")));

    QQmlEngine &engine = *sharedEngine();
    QQmlComponent component(&engine);
    component.setData(R"(import QtQuick
import Drift
QtObject {
    property color app: ThemeManager.semantic.surface.app
})", QUrl());
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    QObject *object = component.create();
    QVERIFY(object);
    QCOMPARE(object->property("app").value<QColor>(), QColor(QStringLiteral("#0d0d0d")));
    delete object;
}

void ThemeManagerTest::mainRegistersThemeManager()
{
    // Drift is an executable-backed module, so a test process cannot import the
    // app's plugin. The guard is that main() goes through registerSingleton(),
    // which singletonRegistrationExposesThemeManager() actually loads QML through.
    QFile mainFile(QStringLiteral(DRIFT_MAIN_CPP));
    QVERIFY2(mainFile.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(mainFile.errorString()));
    const QString source = QString::fromUtf8(mainFile.readAll());
    QVERIFY(source.contains(QStringLiteral("ThemeManager::registerSingleton()")));
    QVERIFY(source.contains(QStringLiteral("qEnvironmentVariable(\"DRIFT_THEME\")")));
    QVERIFY(source.contains(QStringLiteral("applySessionOverride")));
    const int registerAt = source.indexOf(QStringLiteral("ThemeManager::registerSingleton()"));
    const int overrideAt = source.indexOf(QStringLiteral("applySessionOverride"));
    QVERIFY(registerAt >= 0);
    QVERIFY(overrideAt > registerAt);
}

void ThemeManagerTest::liveBindingUpdates()
{
    ThemeManager::registerSingleton();
    ThemeManager *manager = ThemeManager::shared();
    QVERIFY(manager->loadTheme(QStringLiteral("dark")));

    QQmlEngine &engine = *sharedEngine();
    QQmlComponent component(&engine);
    component.setData(R"(import QtQuick
import Drift
QtObject {
    property color app: ThemeManager.semantic.surface.app
    property color border: ThemeManager.semantic.panel.border
    property real radius: ThemeManager.metrics.radius.sm
})", QUrl());
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    QObject *object = component.create();
    QVERIFY(object);
    QCOMPARE(object->property("app").value<QColor>(), QColor(QStringLiteral("#0d0d0d")));
    QCOMPARE(object->property("border").value<QColor>(), QColor(QStringLiteral("#2e2e2e")));

    manager->setVariant(QStringLiteral("light"));
    QCOMPARE(object->property("app").value<QColor>(), QColor(QStringLiteral("#ffffff")));
    QCOMPARE(object->property("border").value<QColor>(), QColor(QStringLiteral("#dedede")));
    QCOMPARE(object->property("radius").toDouble(), 5.6);

    QVERIFY(writeJson("button-red.json", R"({
        "name": "button-red",
        "extends": "dark",
        "components": { "button": { "primary": { "background": "#ff0000" } } }
    })"));
    QVERIFY(manager->loadTheme(QStringLiteral("button-red")));

    QQmlComponent buttonComponent(&engine);
    buttonComponent.setData(R"(import QtQuick
import Drift
QtObject {
    property color background: ThemeManager.components.button.primary.background
})", QUrl());
    QVERIFY2(!buttonComponent.isError(), qPrintable(buttonComponent.errorString()));
    QObject *button = buttonComponent.create();
    QVERIFY(button);
    QCOMPARE(button->property("background").value<QColor>(), QColor(QStringLiteral("#ff0000")));

    QVERIFY(writeJson("button-green.json", R"({
        "name": "button-green",
        "extends": "dark",
        "components": { "button": { "primary": { "background": "#00ff00" } } }
    })"));
    QVERIFY(manager->loadTheme(QStringLiteral("button-green")));
    QCOMPARE(button->property("background").value<QColor>(), QColor(QStringLiteral("#00ff00")));
    // The shell binding is still attached to the same nested map.
    QCOMPARE(object->property("app").value<QColor>(), QColor(QStringLiteral("#0d0d0d")));

    delete button;
    delete object;
}

void ThemeManagerTest::componentTokenUpdatesLive()
{
    ThemeManager::registerSingleton();
    ThemeManager *manager = ThemeManager::shared();
    QVERIFY(manager->loadTheme(QStringLiteral("dark")));

    QQmlEngine &engine = *sharedEngine();
    QQmlComponent component(&engine);
    component.setData(R"(import QtQuick
import Drift
QtObject {
    property color background: ThemeManager.components.themedButton.background
    property color windowBackground: ThemeManager.components.appWindow.background
})", QUrl());
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    QObject *object = component.create();
    QVERIFY(object);
    QCOMPARE(object->property("background").value<QColor>(), QColor(QStringLiteral("#262626")));
    QCOMPARE(object->property("windowBackground").value<QColor>(), QColor(QStringLiteral("#0d0d0d")));

    QVERIFY(writeJson("translucent.json", R"({
        "name": "translucent",
        "extends": "dark",
        "components": {
            "themedButton": { "background": "#ff0000" },
            "appWindow": { "background": "#00000000" }
        }
    })"));
    QVERIFY(manager->loadTheme(QStringLiteral("translucent")));
    QCOMPARE(object->property("background").value<QColor>(), QColor(QStringLiteral("#ff0000")));
    QCOMPARE(object->property("windowBackground").value<QColor>(), QColor(QStringLiteral("#00000000")));
    QCOMPARE(object->property("windowBackground").value<QColor>().alpha(), 0);
    // Untouched tokens stay on the dark base.
    QCOMPARE(tokenColor(*manager, "semantic.surface.app"), QColor(QStringLiteral("#0d0d0d")));
    QCOMPARE(tokenColor(*manager, "components.themedButton.foreground"),
             tokenColor(*manager, "semantic.panel.foreground"));

    delete object;
}

void ThemeManagerTest::themeSlotOverrideFollowsTrust()
{
    QVERIFY(writeJson("slotpack/theme.json", R"({
        "name": "slotpack",
        "extends": "dark",
        "components": { "themedButton": { "background": "#ff0000" } }
    })"));
    const QString qmlPath = themesDir() + QStringLiteral("/slotpack/qml/ThemedButtonBackground.qml");
    QVERIFY(QDir().mkpath(QFileInfo(qmlPath).path()));
    QFile file(qmlPath);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QByteArray body =
        "import QtQuick\n"
        "Rectangle {\n"
        "    property var control\n"
        "    objectName: \"override\"\n"
        "    color: \"#ff00ff\"\n"
        "}\n";
    QCOMPARE(file.write(body), body.size());
    file.close();

    ThemeManager::registerSingleton();
    ThemeManager *manager = ThemeManager::shared();
    QVERIFY(manager->loadTheme(QStringLiteral("slotpack")));
    QVERIFY(!manager->isThemeTrusted(QStringLiteral("slotpack")));
    QVERIFY(manager->overrideUrl(QStringLiteral("ThemedButtonBackground")).isEmpty());

    QQmlEngine &engine = *sharedEngine();
    QQmlComponent component(&engine);
    // tst_thememanager registers ThemeManager itself and does not link the Drift
    // QML module, so ThemeSlot is loaded from its source file.
    const QString slotDir = QFileInfo(QStringLiteral(DRIFT_MAIN_CPP)).dir().filePath(
        QStringLiteral("../qml/components/controls"));
    const QByteArray qml = QStringLiteral(
        "import QtQuick\n"
        "import Drift\n"
        "import \"%1\" as Controls\n"
        "Item {\n"
        "    objectName: \"host\"\n"
        "    Controls.ThemeSlot {\n"
        "        objectName: \"slot\"\n"
        "        slot: \"ThemedButtonBackground\"\n"
        "        fallback: Component {\n"
        "            Rectangle { objectName: \"fallback\"; color: \"#0000ff\" }\n"
        "        }\n"
        "    }\n"
        "}\n").arg(QUrl::fromLocalFile(slotDir).toString()).toUtf8();
    component.setData(qml, QUrl::fromLocalFile(slotDir + QStringLiteral("/theme-slot-test.qml")));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    QObject *host = component.create();
    QVERIFY(host);
    QObject *slot = host->findChild<QObject *>(QStringLiteral("slot"));
    QVERIFY(slot);
    QCoreApplication::processEvents();

    auto loadedName = [&]() {
        QObject *loaded = slot->property("loadedItem").value<QObject *>();
        return loaded ? loaded->objectName() : QString();
    };
    auto loadedColor = [&]() {
        QObject *loaded = slot->property("loadedItem").value<QObject *>();
        return loaded ? loaded->property("color").value<QColor>() : QColor();
    };

    QCOMPARE(loadedName(), QStringLiteral("fallback"));
    QCOMPARE(loadedColor(), QColor(QStringLiteral("#0000ff")));

    manager->setThemeTrusted(QStringLiteral("slotpack"), true);
    QCoreApplication::processEvents();
    QCOMPARE(loadedName(), QStringLiteral("override"));
    QCOMPARE(loadedColor(), QColor(QStringLiteral("#ff00ff")));

    manager->setThemeTrusted(QStringLiteral("slotpack"), false);
    QCoreApplication::processEvents();
    QCOMPARE(loadedName(), QStringLiteral("fallback"));
    QCOMPARE(loadedColor(), QColor(QStringLiteral("#0000ff")));

    delete host;
}

void ThemeManagerTest::untrustedThemeHasNoOverride()
{
    QVERIFY(installOverrideTheme());
    ThemeManager manager;
    QVERIFY(manager.loadTheme(QStringLiteral("pack")));
    QVERIFY(!manager.isThemeTrusted(QStringLiteral("pack")));
    QVERIFY(manager.overrideUrl(QStringLiteral("ThemedButtonBackground")).isEmpty());

    QSignalSpy spy(&manager, &ThemeManager::themeChanged);
    manager.setThemeTrusted(QStringLiteral("other"), true);
    QCOMPARE(spy.count(), 0);
    QVERIFY(manager.overrideUrl(QStringLiteral("ThemedButtonBackground")).isEmpty());
}

void ThemeManagerTest::trustedThemeOverrideUrl()
{
    QVERIFY(installOverrideTheme());
    const QString qmlPath = themesDir() + QStringLiteral("/pack/qml/ThemedButtonBackground.qml");
    const QUrl expected = QUrl::fromLocalFile(QFileInfo(qmlPath).absoluteFilePath());

    ThemeManager manager;
    QVERIFY(manager.loadTheme(QStringLiteral("pack")));
    QSignalSpy spy(&manager, &ThemeManager::themeChanged);
    manager.setThemeTrusted(QStringLiteral("pack"), true);
    QCOMPARE(spy.count(), 1);
    manager.setThemeTrusted(QStringLiteral("pack"), true);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(manager.overrideUrl(QStringLiteral("ThemedButtonBackground")), expected);
    QCOMPARE(manager.overrideSlots(QStringLiteral("pack")),
             QStringList{QStringLiteral("ThemedButtonBackground")});

    ThemeManager again;
    QVERIFY(again.loadTheme(QStringLiteral("pack")));
    QVERIFY(again.isThemeTrusted(QStringLiteral("pack")));
    QCOMPARE(again.overrideUrl(QStringLiteral("ThemedButtonBackground")), expected);

    QSignalSpy revoke(&again, &ThemeManager::themeChanged);
    again.setThemeTrusted(QStringLiteral("pack"), false);
    QCOMPARE(revoke.count(), 1);
    QVERIFY(!again.isThemeTrusted(QStringLiteral("pack")));
    QVERIFY(again.overrideUrl(QStringLiteral("ThemedButtonBackground")).isEmpty());
}

void ThemeManagerTest::missingOverrideSlotIsEmpty()
{
    QVERIFY(installOverrideTheme());
    ThemeManager manager;
    QVERIFY(manager.loadTheme(QStringLiteral("pack")));
    manager.setThemeTrusted(QStringLiteral("pack"), true);
    QVERIFY(manager.overrideUrl(QStringLiteral("MissingSlot")).isEmpty());
    QVERIFY(manager.overrideUrl(QStringLiteral("../ThemedButtonBackground")).isEmpty());
    QVERIFY(manager.overrideUrl(QString()).isEmpty());

    QVERIFY(writeJson("flat.json", R"({
        "name": "flat",
        "extends": "dark"
    })"));
    QVERIFY(manager.loadTheme(QStringLiteral("flat")));
    manager.setThemeTrusted(QStringLiteral("flat"), true);
    QVERIFY(manager.isThemeTrusted(QStringLiteral("flat")));
    QVERIFY(manager.overrideUrl(QStringLiteral("ThemedButtonBackground")).isEmpty());
    QVERIFY(manager.overrideSlots(QStringLiteral("flat")).isEmpty());
}

namespace {

void writeU16(QByteArray &out, quint16 value)
{
    out.append(char(value & 0xff));
    out.append(char((value >> 8) & 0xff));
}

void writeU32(QByteArray &out, quint32 value)
{
    out.append(char(value & 0xff));
    out.append(char((value >> 8) & 0xff));
    out.append(char((value >> 16) & 0xff));
    out.append(char((value >> 24) & 0xff));
}

bool writeStoredZip(const QString &path, const QList<QPair<QString, QByteArray>> &files, bool symlink)
{
    QByteArray local;
    QList<quint32> offsets;
    QList<quint32> crcs;
    offsets.reserve(files.size());
    crcs.reserve(files.size());
    for (const auto &file : files) {
        offsets.append(static_cast<quint32>(local.size()));
        const QByteArray name = file.first.toUtf8();
        const quint32 crc = static_cast<quint32>(crc32(
            0, reinterpret_cast<const Bytef *>(file.second.constData()), static_cast<uInt>(file.second.size())));
        crcs.append(crc);
        writeU32(local, 0x04034b50);
        writeU16(local, 20);
        writeU16(local, 0);
        writeU16(local, 0);
        writeU16(local, 0);
        writeU16(local, 0);
        writeU32(local, crc);
        writeU32(local, static_cast<quint32>(file.second.size()));
        writeU32(local, static_cast<quint32>(file.second.size()));
        writeU16(local, static_cast<quint16>(name.size()));
        writeU16(local, 0);
        local.append(name);
        local.append(file.second);
    }

    QByteArray central;
    for (int i = 0; i < files.size(); ++i) {
        const QByteArray name = files.at(i).first.toUtf8();
        writeU32(central, 0x02014b50);
        writeU16(central, symlink ? 0x0314 : 20);
        writeU16(central, 20);
        writeU16(central, 0);
        writeU16(central, 0);
        writeU16(central, 0);
        writeU16(central, 0);
        writeU32(central, crcs.at(i));
        writeU32(central, static_cast<quint32>(files.at(i).second.size()));
        writeU32(central, static_cast<quint32>(files.at(i).second.size()));
        writeU16(central, static_cast<quint16>(name.size()));
        writeU16(central, 0);
        writeU16(central, 0);
        writeU16(central, 0);
        writeU16(central, 0);
        const quint32 mode = symlink ? (0120000u << 16) : 0u;
        writeU32(central, mode);
        writeU32(central, offsets.at(i));
        central.append(name);
    }
    const quint32 cdSize = static_cast<quint32>(central.size());
    const quint32 cdOffset = static_cast<quint32>(local.size());
    writeU32(central, 0x06054b50);
    writeU16(central, 0);
    writeU16(central, 0);
    writeU16(central, static_cast<quint16>(files.size()));
    writeU16(central, static_cast<quint16>(files.size()));
    writeU32(central, cdSize);
    writeU32(central, cdOffset);
    writeU16(central, 0);

    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const QByteArray bytes = local + central;
    return out.write(bytes) == bytes.size();
}

QString repoThemes()
{
    const QDir srcDir(QFileInfo(QStringLiteral(DRIFT_MAIN_CPP)).dir().absolutePath());
    return QDir(srcDir.filePath(QStringLiteral("../.."))).filePath(QStringLiteral("docs/themes"));
}

} // namespace

void ThemeManagerTest::selectionPersistsAndFallsBack()
{
    {
        ThemeManager first;
        QCOMPARE(first.selection(), QStringLiteral("system"));
        QCOMPARE(first.effectiveVariant(), QStringLiteral("dark"));
        QVERIFY(!first.appearanceFollowsTheme());
        QVERIFY(first.select(QStringLiteral("light")));
        QCOMPARE(first.selection(), QStringLiteral("light"));
        QCOMPARE(first.currentTheme(), QStringLiteral("light"));
        QCOMPARE(first.effectiveVariant(), QStringLiteral("light"));
    }
    {
        ThemeManager second;
        QCOMPARE(second.selection(), QStringLiteral("light"));
        QCOMPARE(second.currentTheme(), QStringLiteral("light"));
        second.setVariant(QStringLiteral("dark"));
        QCOMPARE(second.currentTheme(), QStringLiteral("light"));
        QVERIFY(second.select(QStringLiteral("system")));
        QCOMPARE(second.selection(), QStringLiteral("system"));
        QCOMPARE(second.currentTheme(), QStringLiteral("dark"));
    }
    {
        QSettings settings;
        settings.beginGroup(QStringLiteral("Theme"));
        settings.setValue(QStringLiteral("selection"), QStringLiteral("missing-theme"));
        settings.sync();
    }
    ThemeManager fallback;
    QCOMPARE(fallback.selection(), QStringLiteral("system"));
    QCOMPARE(fallback.currentTheme(), QStringLiteral("dark"));
    QCOMPARE(fallback.effectiveVariant(), QStringLiteral("dark"));
}

void ThemeManagerTest::hotReloadPersists()
{
    {
        ThemeManager first;
        QCOMPARE(first.hotReload(), false);
        first.setHotReload(true);
        QCOMPARE(first.hotReload(), true);
    }
    ThemeManager second;
    QCOMPARE(second.hotReload(), true);
    second.setHotReload(false);
    ThemeManager third;
    QCOMPARE(third.hotReload(), false);
}

void ThemeManagerTest::dualVariantFollowsToggle()
{
    QVERIFY(writeJson("dual.json", R"({
        "name": "dual",
        "extends": "dark",
        "variants": {
            "dark": { "palette": { "appBackground": "#111111" } },
            "light": { "palette": { "appBackground": "#eeeeee" } }
        }
    })"));
    QVERIFY(writeJson("sided/theme.json", R"({
        "name": "sided",
        "extends": "dark"
    })"));
    QVERIFY(writeJson("sided/theme.dark.json", R"({
        "palette": { "appBackground": "#010101" }
    })"));
    QVERIFY(writeJson("sided/theme.light.json", R"({
        "palette": { "appBackground": "#fefefe" }
    })"));

    ThemeManager manager;
    QVERIFY(manager.select(QStringLiteral("dual")));
    QCOMPARE(manager.currentTheme(), QStringLiteral("dual"));
    QVERIFY(!manager.appearanceFollowsTheme());
    QCOMPARE(manager.effectiveVariant(), QStringLiteral("dark"));
    QCOMPARE(tokenColor(manager, "semantic.surface.app"), QColor(QStringLiteral("#111111")));
    const QVariantMap themes = [&]() {
        for (const QVariant &row : manager.themes()) {
            const QVariantMap map = row.toMap();
            if (map.value(QStringLiteral("id")).toString() == QLatin1String("dual"))
                return map;
        }
        return QVariantMap{};
    }();
    QCOMPARE(themes.value(QStringLiteral("hasVariants")).toBool(), true);

    manager.setVariant(QStringLiteral("light"));
    QCOMPARE(manager.selection(), QStringLiteral("dual"));
    QCOMPARE(manager.currentTheme(), QStringLiteral("dual"));
    QCOMPARE(manager.effectiveVariant(), QStringLiteral("light"));
    QCOMPARE(tokenColor(manager, "semantic.surface.app"), QColor(QStringLiteral("#eeeeee")));
    QCOMPARE(manager.previewColors(QStringLiteral("dual")).value(QStringLiteral("surface")).value<QColor>(),
             QColor(QStringLiteral("#eeeeee")));

    manager.setVariant(QStringLiteral("dark"));
    QCOMPARE(tokenColor(manager, "semantic.surface.app"), QColor(QStringLiteral("#111111")));

    QVERIFY(manager.select(QStringLiteral("sided")));
    QCOMPARE(manager.effectiveVariant(), QStringLiteral("dark"));
    QCOMPARE(tokenColor(manager, "semantic.surface.app"), QColor(QStringLiteral("#010101")));
    manager.setVariant(QStringLiteral("light"));
    QCOMPARE(manager.currentTheme(), QStringLiteral("sided"));
    QCOMPARE(manager.effectiveVariant(), QStringLiteral("light"));
    QCOMPARE(tokenColor(manager, "semantic.surface.app"), QColor(QStringLiteral("#fefefe")));
}

void ThemeManagerTest::singleVariantLocksAppearance()
{
    QVERIFY(writeJson("only-light.json", R"({
        "name": "only-light",
        "extends": "light",
        "variant": "light",
        "palette": { "appBackground": "#f4f4f4" }
    })"));
    ThemeManager manager;
    manager.setVariant(QStringLiteral("dark"));
    QVERIFY(manager.select(QStringLiteral("only-light")));
    QCOMPARE(manager.effectiveVariant(), QStringLiteral("light"));
    QVERIFY(manager.appearanceFollowsTheme());
    QCOMPARE(tokenColor(manager, "semantic.surface.app"), QColor(QStringLiteral("#f4f4f4")));
    manager.setVariant(QStringLiteral("dark"));
    QCOMPARE(manager.currentTheme(), QStringLiteral("only-light"));
    QCOMPARE(manager.effectiveVariant(), QStringLiteral("light"));
    QCOMPARE(manager.selection(), QStringLiteral("only-light"));
}

void ThemeManagerTest::importJsonFolderZipAndRejectsTraversal()
{
    QTemporaryDir outside;
    QVERIFY(outside.isValid());
    const QString jsonPath = outside.filePath(QStringLiteral("imported.json"));
    {
        QFile file(jsonPath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const QByteArray body = R"({
            "name": "imported",
            "extends": "dark",
            "palette": { "appBackground": "#222222" }
        })";
        QCOMPARE(file.write(body), body.size());
    }
    const QString folder = outside.filePath(QStringLiteral("folder-theme"));
    QVERIFY(QDir().mkpath(folder + QStringLiteral("/qml")));
    {
        QFile file(folder + QStringLiteral("/theme.json"));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const QByteArray body = R"({
            "name": "folder-theme",
            "extends": "dark",
            "palette": { "appBackground": "#333333" }
        })";
        QCOMPARE(file.write(body), body.size());
    }
    {
        QFile file(folder + QStringLiteral("/qml/ThemedButtonBackground.qml"));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const QByteArray body = "import QtQuick\nRectangle { property var control }\n";
        QCOMPARE(file.write(body), body.size());
    }

    const QByteArray zipTheme = R"({
        "name": "zipped",
        "extends": "dark",
        "palette": { "appBackground": "#444444" }
    })";
    const QString zipPath = outside.filePath(QStringLiteral("zipped.zip"));
    QVERIFY(writeStoredZip(zipPath, {{QStringLiteral("pkg/theme.json"), zipTheme}}, false));

    const QString slipPath = outside.filePath(QStringLiteral("slip.zip"));
    QVERIFY(writeStoredZip(slipPath,
                           {{QStringLiteral("theme.json"), zipTheme},
                            {QStringLiteral("../evil.json"), QByteArray("{}")}},
                           false));
    const QString absPath = outside.filePath(QStringLiteral("abs.zip"));
    QVERIFY(writeStoredZip(absPath, {{QStringLiteral("/tmp/abs.json"), QByteArray("{}")}}, false));
    const QString linkPath = outside.filePath(QStringLiteral("link.zip"));
    QVERIFY(writeStoredZip(linkPath, {{QStringLiteral("theme.json"), zipTheme}}, true));

    ThemeManager manager;
    QCOMPARE(manager.importTheme(QUrl::fromLocalFile(jsonPath)), QStringLiteral("imported"));
    QVERIFY(manager.select(QStringLiteral("imported")));
    QCOMPARE(tokenColor(manager, "semantic.surface.app"), QColor(QStringLiteral("#222222")));

    QCOMPARE(manager.importTheme(QUrl::fromLocalFile(folder)), QStringLiteral("folder-theme"));
    QCOMPARE(manager.overrideSlots(QStringLiteral("folder-theme")),
             QStringList{QStringLiteral("ThemedButtonBackground")});
    QVERIFY(!manager.isThemeTrusted(QStringLiteral("folder-theme")));

    QCOMPARE(manager.importTheme(QUrl::fromLocalFile(zipPath)), QStringLiteral("zipped"));
    QVERIFY(manager.select(QStringLiteral("zipped")));
    QCOMPARE(tokenColor(manager, "semantic.surface.app"), QColor(QStringLiteral("#444444")));

    QCOMPARE(manager.importTheme(QUrl::fromLocalFile(slipPath)), QString());
    QVERIFY(!manager.lastError().isEmpty());
    QVERIFY(!manager.availableThemes().contains(QStringLiteral("evil")));
    QVERIFY(!QFile::exists(outside.filePath(QStringLiteral("evil.json"))));

    QCOMPARE(manager.importTheme(QUrl::fromLocalFile(absPath)), QString());
    QVERIFY(manager.lastError().contains(QStringLiteral("not allowed")));
    QCOMPARE(manager.importTheme(QUrl::fromLocalFile(linkPath)), QString());
    QVERIFY(manager.lastError().contains(QStringLiteral("symlink")));

    const QString link = outside.filePath(QStringLiteral("linked.json"));
    QVERIFY(QFile::link(jsonPath, link));
    QCOMPARE(manager.importTheme(QUrl::fromLocalFile(link)), QString());
#ifndef Q_OS_WIN
    // On Windows QFile::link makes a .lnk shortcut, which the import reads as (invalid) JSON.
    QVERIFY(manager.lastError().contains(QStringLiteral("symlink")));
#endif
}

void ThemeManagerTest::removeAndExportRoundTrip()
{
    QVERIFY(writeJson("round.json", R"({
        "name": "round",
        "extends": "dark",
        "palette": { "primary": "#abcdef" },
        "semantic": { "focusRing": "$palette.primary" }
    })"));
    ThemeManager manager;
    QVERIFY(manager.select(QStringLiteral("round")));
    const QColor app = tokenColor(manager, "semantic.surface.app");
    const QColor primary = tokenColor(manager, "semantic.primary");
    const QColor focus = tokenColor(manager, "semantic.focusRing");
    const double radius = manager.token(QStringLiteral("metrics.radius.sm")).toDouble();
    const QString easing = manager.token(QStringLiteral("motion.easing")).toString();
    QCOMPARE(primary, QColor(QStringLiteral("#abcdef")));
    QCOMPARE(focus, primary);

    QTemporaryDir outside;
    QVERIFY(outside.isValid());
    const QString exported = outside.filePath(QStringLiteral("round-export.json"));
    QVERIFY(manager.exportTheme(QUrl::fromLocalFile(exported)));
    QFile exportedFile(exported);
    QVERIFY(exportedFile.open(QIODevice::ReadOnly));
    const QJsonObject object = QJsonDocument::fromJson(exportedFile.readAll()).object();
    QVERIFY(!object.contains(QStringLiteral("extends")));
    QCOMPARE(object.value(QStringLiteral("name")).toString(), QStringLiteral("round"));

    QVERIFY(manager.removeTheme(QStringLiteral("round")));
    QCOMPARE(manager.selection(), QStringLiteral("system"));
    QVERIFY(!manager.availableThemes().contains(QStringLiteral("round")));
    QVERIFY(!manager.removeTheme(QStringLiteral("dark")));

    QCOMPARE(manager.importTheme(QUrl::fromLocalFile(exported)), QStringLiteral("round"));
    QVERIFY(manager.select(QStringLiteral("round")));
    QCOMPARE(tokenColor(manager, "semantic.surface.app"), app);
    QCOMPARE(tokenColor(manager, "semantic.primary"), primary);
    QCOMPARE(tokenColor(manager, "semantic.focusRing"), focus);
    QCOMPARE(manager.token(QStringLiteral("metrics.radius.sm")).toDouble(), radius);
    QCOMPARE(manager.token(QStringLiteral("motion.easing")).toString(), easing);
    QCOMPARE(tokenColor(manager, "components.themedButton.background"), QColor(QStringLiteral("#262626")));
}

void ThemeManagerTest::exampleThemesLoad()
{
    const QString root = repoThemes();
    ThemeManager manager;
    QCOMPARE(manager.importTheme(QUrl::fromLocalFile(root + QStringLiteral("/frosted"))),
             QStringLiteral("frosted"));
    QVERIFY(manager.select(QStringLiteral("frosted")));
    QCOMPARE(manager.token(QStringLiteral("window.translucent")).toBool(), true);
    QCOMPARE(manager.token(QStringLiteral("window.backdrop")).toString(), QStringLiteral("blur"));
    QVERIFY(tokenColor(manager, "semantic.surface.app").alpha() < 255);
    QVERIFY(tokenColor(manager, "semantic.surface.panel").alpha() < 255);
    QVERIFY(manager.appearanceFollowsTheme());
    QCOMPARE(manager.effectiveVariant(), QStringLiteral("dark"));

    QCOMPARE(manager.importTheme(QUrl::fromLocalFile(root + QStringLiteral("/high-contrast"))),
             QStringLiteral("high-contrast"));
    QVERIFY(manager.select(QStringLiteral("high-contrast")));
    QCOMPARE(tokenColor(manager, "semantic.foreground"), QColor(QStringLiteral("#ffffff")));
    QCOMPARE(tokenColor(manager, "semantic.primary"), QColor(QStringLiteral("#ffff00")));

    QCOMPARE(manager.importTheme(QUrl::fromLocalFile(root + QStringLiteral("/override-example"))),
             QStringLiteral("override-example"));
    QCOMPARE(manager.overrideSlots(QStringLiteral("override-example")),
             QStringList{QStringLiteral("ThemedButtonBackground")});
    QVERIFY(manager.select(QStringLiteral("override-example")));
    QCOMPARE(tokenColor(manager, "components.themedButton.background"), QColor(QStringLiteral("#ff00aa")));
    QVERIFY(manager.overrideUrl(QStringLiteral("ThemedButtonBackground")).isEmpty());
    manager.setThemeTrusted(QStringLiteral("override-example"), true);
    QVERIFY(!manager.overrideUrl(QStringLiteral("ThemedButtonBackground")).isEmpty());
}

void ThemeManagerTest::schemaVersionMigratesAndWarns()
{
    // Pre-versioning standalone theme: no extends, no schemaVersion, most tokens missing.
    QVERIFY(writeJson("legacy.json", R"({
        "name": "legacy",
        "variant": "light",
        "palette": { "primary": "#123456" }
    })"));
    QVERIFY(writeJson("future.json", R"({
        "name": "future",
        "schemaVersion": 999,
        "extends": "dark",
        "somethingNew": { "x": 1 }
    })"));
    QVERIFY(writeJson("badversion.json", R"({
        "name": "badversion",
        "schemaVersion": "two"
    })"));

    ThemeManager manager;
    QSignalSpy warnings(&manager, &ThemeManager::lastWarningChanged);
    QVERIFY(manager.loadTheme(QStringLiteral("legacy")));
    QVERIFY(manager.lastWarning().isEmpty());
    QCOMPARE(tokenColor(manager, "semantic.primary"), QColor(QStringLiteral("#123456")));
    QCOMPARE(manager.token(QStringLiteral("metrics.radius.sm")),
             manager.builtinToken(QStringLiteral("light"), QStringLiteral("metrics.radius.sm")));
    QCOMPARE(tokenColor(manager, "components.themedButton.background"),
             manager.builtinToken(QStringLiteral("light"), QStringLiteral("components.themedButton.background"))
                 .value<QColor>());
    QCOMPARE(manager.token(QStringLiteral("schemaVersion")).toInt(), 1);

    QVERIFY(manager.loadTheme(QStringLiteral("future")));
    QCOMPARE(manager.currentTheme(), QStringLiteral("future"));
    QVERIFY(!manager.lastWarning().isEmpty());
    QCOMPARE(warnings.count(), 1);

    QVERIFY(manager.loadTheme(QStringLiteral("legacy")));
    QVERIFY(manager.lastWarning().isEmpty());
    QCOMPARE(warnings.count(), 2);

    QVERIFY(!manager.loadTheme(QStringLiteral("badversion")));

    QTemporaryDir outside;
    QVERIFY(outside.isValid());
    const QString exported = outside.filePath(QStringLiteral("legacy-export.json"));
    QVERIFY(manager.exportTheme(QUrl::fromLocalFile(exported)));
    QFile exportedFile(exported);
    QVERIFY(exportedFile.open(QIODevice::ReadOnly));
    const QJsonObject object = QJsonDocument::fromJson(exportedFile.readAll()).object();
    QCOMPARE(object.value(QStringLiteral("schemaVersion")).toInt(), 1);
}

void ThemeManagerTest::templateExportAndReplaceImport()
{
    ThemeManager manager;
    QString error;
    QVERIFY(manager.themeTemplate(QStringLiteral("dark"), QStringLiteral("light"), false, &error).isEmpty());

    QTemporaryDir outside;
    QVERIFY(outside.isValid());
    const QString starterPath = outside.filePath(QStringLiteral("ocean.json"));
    const QByteArray starter = manager.themeTemplate(QStringLiteral("dark"), QStringLiteral("ocean"), true, &error);
    QJsonObject object = QJsonDocument::fromJson(starter).object();
    QCOMPARE(object.value(QStringLiteral("extends")).toString(), QStringLiteral("dark"));
    QCOMPARE(object.value(QStringLiteral("name")).toString(), QStringLiteral("ocean"));
    QVERIFY(object.value(QStringLiteral("palette")).toObject().contains(QStringLiteral("primary")));
    QVERIFY(!object.value(QStringLiteral("palette")).toObject().contains(QStringLiteral("curveX")));
    QVERIFY(!object.contains(QStringLiteral("semantic")));
    QVERIFY(!object.contains(QStringLiteral("components")));
    QVERIFY(object.contains(QStringLiteral("metrics")));
    QVERIFY(object.contains(QStringLiteral("typography")));
    QVERIFY(object.contains(QStringLiteral("motion")));
    QCOMPARE(object.value(QStringLiteral("window")).toObject().value(QStringLiteral("translucent")).toBool(), false);

    QJsonObject palette = object.value(QStringLiteral("palette")).toObject();
    palette.insert(QStringLiteral("primary"), QStringLiteral("#00aaff"));
    object.insert(QStringLiteral("palette"), palette);
    QFile file(starterPath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(object).toJson());
    file.close();
    QCOMPARE(manager.importTheme(QUrl::fromLocalFile(starterPath)), QStringLiteral("ocean"));
    QVERIFY(manager.select(QStringLiteral("ocean")));
    QCOMPARE(tokenColor(manager, "semantic.primary"), QColor(QStringLiteral("#00aaff")));

    palette.insert(QStringLiteral("primary"), QStringLiteral("#ff5500"));
    object.insert(QStringLiteral("palette"), palette);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(QJsonDocument(object).toJson());
    file.close();
    QVERIFY(manager.importTheme(QUrl::fromLocalFile(starterPath)).isEmpty());
    QCOMPARE(manager.importTheme(QUrl::fromLocalFile(starterPath), true), QStringLiteral("ocean"));
    QCOMPARE(tokenColor(manager, "semantic.primary"), QColor(QStringLiteral("#ff5500")));

    const QByteArray copy = manager.themeTemplate(QStringLiteral("ocean"), QStringLiteral("ocean2"), false, &error);
    object = QJsonDocument::fromJson(copy).object();
    QVERIFY(!object.contains(QStringLiteral("extends")));
    QCOMPARE(object.value(QStringLiteral("schemaVersion")).toInt(), ThemeManager::kSchemaVersion);
    QCOMPARE(object.value(QStringLiteral("palette")).toObject().value(QStringLiteral("primary")).toString().toLower(),
             QStringLiteral("#ff5500"));
    QVERIFY(object.contains(QStringLiteral("components")));
}

void ThemeManagerTest::sessionOverrideIsNotPersisted()
{
    QVERIFY(writeJson("session-only.json", R"({
        "name": "session-only",
        "extends": "dark",
        "variant": "dark",
        "palette": { "appBackground": "#123456" }
    })"));
    QVERIFY(writeJson("session-dual.json", R"({
        "name": "session-dual",
        "extends": "dark",
        "variants": {
            "dark": { "palette": { "appBackground": "#111111" } },
            "light": { "palette": { "appBackground": "#eeeeee" } }
        }
    })"));

    {
        ThemeManager manager;
        QCOMPARE(manager.selection(), QStringLiteral("system"));
        QVERIFY(manager.select(QStringLiteral("light")));
        QVERIFY(manager.applySessionOverride(QStringLiteral("session-only")));
        QCOMPARE(manager.currentTheme(), QStringLiteral("session-only"));
        QCOMPARE(manager.selection(), QStringLiteral("light"));
        QCOMPARE(tokenColor(manager, "semantic.surface.app"), QColor(QStringLiteral("#123456")));
        manager.setVariant(QStringLiteral("light"));
        QCOMPARE(manager.currentTheme(), QStringLiteral("session-only"));
        QCOMPARE(manager.selection(), QStringLiteral("light"));
        QSettings settings;
        settings.beginGroup(QStringLiteral("Theme"));
        QCOMPARE(settings.value(QStringLiteral("selection")).toString(), QStringLiteral("light"));
    }
    {
        ThemeManager again;
        QCOMPARE(again.selection(), QStringLiteral("light"));
        QCOMPARE(again.currentTheme(), QStringLiteral("light"));
    }

    {
        QSettings settings;
        settings.beginGroup(QStringLiteral("Theme"));
        settings.setValue(QStringLiteral("selection"), QStringLiteral("system"));
        settings.sync();
    }

    // A built-in override survives setVariant. select() ends it and persists.
    {
        ThemeManager pinned;
        QCOMPARE(pinned.selection(), QStringLiteral("system"));
        QCOMPARE(pinned.currentTheme(), QStringLiteral("dark"));
        QVERIFY(pinned.applySessionOverride(QStringLiteral("light")));
        QCOMPARE(pinned.selection(), QStringLiteral("system"));
        pinned.setVariant(QStringLiteral("dark"));
        QCOMPARE(pinned.currentTheme(), QStringLiteral("light"));
        QVERIFY(pinned.select(QStringLiteral("system")));
        QCOMPARE(pinned.currentTheme(), QStringLiteral("dark"));
        pinned.setVariant(QStringLiteral("light"));
        QCOMPARE(pinned.currentTheme(), QStringLiteral("light"));
    }

    // A failed override leaves the loaded theme, so setVariant still applies.
    {
        ThemeManager failed;
        QCOMPARE(failed.currentTheme(), QStringLiteral("dark"));
        QVERIFY(!failed.applySessionOverride(QStringLiteral("/no/such/drift-theme.json")));
        QCOMPARE(failed.currentTheme(), QStringLiteral("dark"));
        QVERIFY(!failed.applySessionOverride(QString()));
        QCOMPARE(failed.currentTheme(), QStringLiteral("dark"));
        failed.setVariant(QStringLiteral("light"));
        QCOMPARE(failed.currentTheme(), QStringLiteral("light"));
    }

    // A dual-variant override follows the side and does not write the selection.
    {
        ThemeManager dual;
        QVERIFY(dual.select(QStringLiteral("system")));
        QVERIFY(dual.applySessionOverride(QStringLiteral("session-dual")));
        QCOMPARE(dual.selection(), QStringLiteral("system"));
        dual.setVariant(QStringLiteral("light"));
        QCOMPARE(dual.currentTheme(), QStringLiteral("session-dual"));
        QCOMPARE(dual.effectiveVariant(), QStringLiteral("light"));
        QCOMPARE(tokenColor(dual, "semantic.surface.app"), QColor(QStringLiteral("#eeeeee")));
        QSettings settings;
        settings.beginGroup(QStringLiteral("Theme"));
        QCOMPARE(settings.value(QStringLiteral("selection")).toString(), QStringLiteral("system"));
    }
}

void ThemeManagerTest::darkModeBindingDoesNotLoop()
{
    QVERIFY(writeJson("only-light.json", R"({
        "name": "only-light",
        "extends": "light",
        "variant": "light",
        "palette": { "appBackground": "#f4f4f4" }
    })"));
    QVERIFY(writeJson("only-dark.json", R"({
        "name": "only-dark",
        "extends": "dark",
        "variant": "dark",
        "palette": { "appBackground": "#111111" }
    })"));
    QVERIFY(writeJson("dual.json", R"({
        "name": "dual",
        "extends": "dark",
        "variants": {
            "dark": { "palette": { "appBackground": "#111111" } },
            "light": { "palette": { "appBackground": "#eeeeee" } }
        }
    })"));

    // Dedicated notifiers fire only when the value changes. themeChanged still
    // fires for a same-variant token reload.
    {
        ThemeManager manager;
        QSignalSpy variantSpy(&manager, &ThemeManager::effectiveVariantChanged);
        QSignalSpy followsSpy(&manager, &ThemeManager::appearanceFollowsThemeChanged);

        manager.setVariant(QStringLiteral("dark"));
        QCOMPARE(variantSpy.count(), 0);
        QCOMPARE(followsSpy.count(), 0);

        manager.setVariant(QStringLiteral("light"));
        QCOMPARE(variantSpy.count(), 1);
        QCOMPARE(followsSpy.count(), 0);
        QCOMPARE(manager.effectiveVariant(), QStringLiteral("light"));
        manager.setVariant(QStringLiteral("light"));
        QCOMPARE(variantSpy.count(), 1);

        QVERIFY(manager.select(QStringLiteral("light")));
        QCOMPARE(variantSpy.count(), 1);
        QCOMPARE(followsSpy.count(), 0);
        QVERIFY(manager.select(QStringLiteral("dark")));
        QCOMPARE(variantSpy.count(), 2);
        QCOMPARE(manager.effectiveVariant(), QStringLiteral("dark"));
        manager.setVariant(QStringLiteral("light"));
        QCOMPARE(variantSpy.count(), 2);
        QCOMPARE(manager.currentTheme(), QStringLiteral("dark"));
        QCOMPARE(manager.selection(), QStringLiteral("dark"));

        QVERIFY(manager.select(QStringLiteral("only-light")));
        QCOMPARE(manager.effectiveVariant(), QStringLiteral("light"));
        QCOMPARE(variantSpy.count(), 3);
        QCOMPARE(followsSpy.count(), 1);
        QVERIFY(manager.appearanceFollowsTheme());
        manager.setVariant(QStringLiteral("dark"));
        QCOMPARE(variantSpy.count(), 3);
        QCOMPARE(followsSpy.count(), 1);
        QCOMPARE(manager.currentTheme(), QStringLiteral("only-light"));
        QVERIFY(manager.select(QStringLiteral("only-light")));
        QCOMPARE(variantSpy.count(), 3);
        QCOMPARE(followsSpy.count(), 1);

        QVERIFY(manager.select(QStringLiteral("dual")));
        QCOMPARE(manager.effectiveVariant(), QStringLiteral("dark"));
        QCOMPARE(variantSpy.count(), 4);
        QCOMPARE(followsSpy.count(), 2);
        QVERIFY(!manager.appearanceFollowsTheme());
        manager.setVariant(QStringLiteral("dark"));
        QCOMPARE(variantSpy.count(), 4);
        QCOMPARE(followsSpy.count(), 2);
        manager.setVariant(QStringLiteral("light"));
        QCOMPARE(variantSpy.count(), 5);
        QCOMPARE(manager.effectiveVariant(), QStringLiteral("light"));
        manager.setVariant(QStringLiteral("light"));
        QCOMPARE(variantSpy.count(), 5);
        QCOMPARE(followsSpy.count(), 2);

        QVERIFY(manager.applySessionOverride(QStringLiteral("only-dark")));
        QCOMPARE(manager.currentTheme(), QStringLiteral("only-dark"));
        QCOMPARE(manager.effectiveVariant(), QStringLiteral("dark"));
        QCOMPARE(variantSpy.count(), 6);
        QCOMPARE(followsSpy.count(), 3);
        manager.setVariant(QStringLiteral("light"));
        QCOMPARE(variantSpy.count(), 6);
        QCOMPARE(followsSpy.count(), 3);
        QCOMPARE(manager.currentTheme(), QStringLiteral("only-dark"));
        QCOMPARE(manager.selection(), QStringLiteral("dual"));

        QVERIFY(manager.select(QStringLiteral("system")));
        QCOMPARE(manager.selection(), QStringLiteral("system"));
        QCOMPARE(manager.currentTheme(), QStringLiteral("light"));
        QCOMPARE(manager.effectiveVariant(), QStringLiteral("light"));
        QCOMPARE(variantSpy.count(), 7);
        QCOMPARE(followsSpy.count(), 4);
        QVERIFY(manager.select(QStringLiteral("system")));
        QCOMPARE(variantSpy.count(), 7);
        QCOMPARE(followsSpy.count(), 4);

        QSignalSpy themeSpy(&manager, &ThemeManager::themeChanged);
        manager.reload();
        QCOMPARE(themeSpy.count(), 1);
        QCOMPARE(variantSpy.count(), 7);
        QCOMPARE(followsSpy.count(), 4);
    }

    ThemeManager::registerSingleton();
    ThemeManager *manager = ThemeManager::shared();
    QVERIFY(manager->select(QStringLiteral("system")));
    manager->setVariant(QStringLiteral("dark"));
    QCOMPARE(manager->currentTheme(), QStringLiteral("dark"));
    QCOMPARE(manager->selection(), QStringLiteral("system"));

    QSignalSpy variantSpy(manager, &ThemeManager::effectiveVariantChanged);
    QSignalSpy followsSpy(manager, &ThemeManager::appearanceFollowsThemeChanged);
    QTest::failOnWarning(QRegularExpression(QStringLiteral("Binding loop")));

    QQmlEngine &engine = *sharedEngine();
    const char *qml = R"(import QtQuick
import Drift
QtObject {
    // Stand-in for Qt.styleHints.colorScheme. The offscreen platform theme
    // ignores QStyleHints::setColorScheme, so the test sets this directly.
    // Theme.qml reads the same branch: preference when overridden, otherwise OS.
    property bool systemPrefersDark: true
    property bool darkModeOverridden: false
    property bool darkModePreferred: true
    readonly property bool darkMode: {
        if (ThemeManager.appearanceFollowsTheme)
            return ThemeManager.effectiveVariant === "dark"
        if (ThemeManager.selection === "dark" || ThemeManager.selection === "light")
            return ThemeManager.selection === "dark"
        return darkModeOverridden ? darkModePreferred : systemPrefersDark
    }
    function syncVariant() {
        ThemeManager.setVariant(darkMode ? "dark" : "light")
    }
    Component.onCompleted: syncVariant()
    onDarkModeChanged: Qt.callLater(syncVariant)
})";

    auto settle = [] {
        for (int i = 0; i < 6; ++i)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    };

    QQmlComponent component(&engine);
    component.setData(qml, QUrl());
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));

    auto makeHost = [&](bool systemPrefersDark) {
        return component.createWithInitialProperties(
            {{QStringLiteral("systemPrefersDark"), systemPrefersDark}});
    };

    {
        QObject *host = makeHost(true);
        QVERIFY(host);
        settle();
        QCOMPARE(host->property("darkMode").toBool(), true);
        QCOMPARE(manager->selection(), QStringLiteral("system"));
        QCOMPARE(manager->currentTheme(), QStringLiteral("dark"));
        QCOMPARE(manager->effectiveVariant(), QStringLiteral("dark"));
        QCOMPARE(variantSpy.count(), 0);
        QCOMPARE(followsSpy.count(), 0);
        delete host;
        settle();
    }

    QObject *host = makeHost(false);
    QVERIFY(host);
    settle();
    QCOMPARE(host->property("systemPrefersDark").toBool(), false);
    QCOMPARE(host->property("darkMode").toBool(), false);
    QCOMPARE(manager->selection(), QStringLiteral("system"));
    QCOMPARE(manager->currentTheme(), QStringLiteral("light"));
    QCOMPARE(manager->effectiveVariant(), QStringLiteral("light"));
    QCOMPARE(variantSpy.count(), 1);
    QCOMPARE(followsSpy.count(), 0);

    host->setProperty("darkModeOverridden", true);
    host->setProperty("darkModePreferred", true);
    settle();
    QCOMPARE(host->property("darkMode").toBool(), true);
    QCOMPARE(manager->currentTheme(), QStringLiteral("dark"));
    QCOMPARE(manager->effectiveVariant(), QStringLiteral("dark"));

    host->setProperty("darkModePreferred", false);
    settle();
    QCOMPARE(host->property("darkMode").toBool(), false);
    QCOMPARE(manager->currentTheme(), QStringLiteral("light"));
    QCOMPARE(manager->selection(), QStringLiteral("system"));

    QVERIFY(manager->select(QStringLiteral("dark")));
    settle();
    QCOMPARE(manager->selection(), QStringLiteral("dark"));
    QCOMPARE(manager->currentTheme(), QStringLiteral("dark"));
    QCOMPARE(host->property("darkMode").toBool(), true);

    QVERIFY(manager->select(QStringLiteral("light")));
    settle();
    QCOMPARE(manager->selection(), QStringLiteral("light"));
    QCOMPARE(manager->currentTheme(), QStringLiteral("light"));
    QCOMPARE(host->property("darkMode").toBool(), false);

    QVERIFY(manager->select(QStringLiteral("only-light")));
    settle();
    QCOMPARE(manager->currentTheme(), QStringLiteral("only-light"));
    QCOMPARE(manager->effectiveVariant(), QStringLiteral("light"));
    QVERIFY(manager->appearanceFollowsTheme());
    QCOMPARE(host->property("darkMode").toBool(), false);

    QVERIFY(manager->select(QStringLiteral("only-dark")));
    settle();
    QCOMPARE(manager->currentTheme(), QStringLiteral("only-dark"));
    QCOMPARE(manager->effectiveVariant(), QStringLiteral("dark"));
    QVERIFY(manager->appearanceFollowsTheme());
    QCOMPARE(host->property("darkMode").toBool(), true);

    QVERIFY(manager->select(QStringLiteral("dual")));
    settle();
    QCOMPARE(manager->currentTheme(), QStringLiteral("dual"));
    QVERIFY(!manager->appearanceFollowsTheme());
    QCOMPARE(manager->effectiveVariant(), QStringLiteral("light"));
    QCOMPARE(host->property("darkMode").toBool(), false);

    host->setProperty("darkModePreferred", true);
    settle();
    QCOMPARE(manager->currentTheme(), QStringLiteral("dual"));
    QCOMPARE(manager->effectiveVariant(), QStringLiteral("dark"));
    QCOMPARE(host->property("darkMode").toBool(), true);

    QVERIFY(manager->select(QStringLiteral("system")));
    settle();
    QCOMPARE(manager->selection(), QStringLiteral("system"));
    QCOMPARE(manager->currentTheme(), QStringLiteral("dark"));
    QCOMPARE(manager->effectiveVariant(), QStringLiteral("dark"));
    QVERIFY(!manager->appearanceFollowsTheme());
    QCOMPARE(host->property("darkMode").toBool(), true);

    const int variants = variantSpy.count();
    const int follows = followsSpy.count();
    const QString theme = manager->currentTheme();
    settle();
    QCOMPARE(variantSpy.count(), variants);
    QCOMPARE(followsSpy.count(), follows);
    QCOMPARE(manager->currentTheme(), theme);
    QCOMPARE(host->property("darkMode").toBool(), true);

    delete host;
}

QTEST_MAIN(ThemeManagerTest)
#include "tst_thememanager.moc"
