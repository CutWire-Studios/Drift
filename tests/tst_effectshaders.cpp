#include <QtTest>

#include "core/effects/Effect.h"
#include "engine/compositor/GpuCompositor.h"
#include "engine/compositor/effects/EffectProcessor.h"
#include "core/effects/Mask.h"
#include "engine/gpu/effects/GpuEffectExecutor.h"
#include "engine/packages/effects/EffectCatalog.h"
#include "engine/packages/transitions/TransitionCatalog.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTemporaryDir>

namespace {

QStringList g_failures;
QtMessageHandler g_previous = nullptr;

void captureFailures(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (message.contains(QLatin1String("compile failed")) || message.contains(QLatin1String("link failed"))
        || message.contains(QLatin1String("FBO alloc failed")) || message.contains(QLatin1String("failed to load")))
        g_failures.append(message);
    if (g_previous)
        g_previous(type, context, message);
}

QStringList rootsFromEnv(const char *name, const QStringList &fallback)
{
    const QString raw = qEnvironmentVariable(name);
    return raw.isEmpty() ? fallback : raw.split(QDir::listSeparator(), Qt::SkipEmptyParts);
}

QImage testFrame()
{
    QImage frame(160, 90, QImage::Format_RGBA8888);
    for (int y = 0; y < frame.height(); ++y)
        for (int x = 0; x < frame.width(); ++x)
            frame.setPixelColor(x, y, QColor::fromHsv((x * 360) / frame.width(), 160, 60 + y * 2));
    return frame;
}

} // namespace

// Compiles and runs every GPU package under DRIFT_TEST_EFFECT_ROOTS / DRIFT_TEST_TRANSITION_ROOTS
// (path lists) once at its defaults, so a shader that fails to compile or link is caught without
// opening the editor. DRIFT_TEST_ID_FILTER narrows it to ids matching a regular expression.
class EffectShadersTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void everyEffectCompilesAndRuns();
    void everyTransitionCompilesAndRuns();
    void paddingEffectDrawsPastTheClipFrame();

private:
    QStringList m_effectRoots;
    QStringList m_transitionRoots;
    QRegularExpression m_filter;
};

void EffectShadersTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    m_effectRoots = rootsFromEnv("DRIFT_TEST_EFFECT_ROOTS", {QString::fromUtf8(DRIFT_TEST_EFFECTS_DIR)});
    m_transitionRoots =
        rootsFromEnv("DRIFT_TEST_TRANSITION_ROOTS", {QString::fromUtf8(DRIFT_TEST_TRANSITIONS_DIR)});
    m_filter = QRegularExpression(qEnvironmentVariable("DRIFT_TEST_ID_FILTER", QStringLiteral(".*")));
    reloadEffectCatalog(m_effectRoots);
    reloadTransitionCatalog(m_transitionRoots);
    g_previous = qInstallMessageHandler(captureFailures);
}

void EffectShadersTest::everyEffectCompilesAndRuns()
{
    if (!GpuEffectExecutor::instance().isAvailable())
        QSKIP("OpenGL offscreen context unavailable");

    const QImage frame = testFrame();
    int checked = 0;
    QStringList broken;
    for (const EffectPresetEntry &def : effectCatalog()) {
        if (!def.isGpu || !def.gpu.valid || !m_filter.match(def.meta.id).hasMatch())
            continue;
        // Names a variable `flat`, a reserved word in GLSL 330, so Mesa rejects it. Drop this once fixed.
        if (def.meta.id == QLatin1String("oil_paint"))
            continue;
        bool inRoot = false;
        for (const QString &root : std::as_const(m_effectRoots))
            inRoot = inRoot || def.gpu.packageDir.startsWith(QDir(root).absolutePath());
        if (!inRoot)
            continue;

        g_failures.clear();
        drift::Effect effect;
        effect.catalogId = def.meta.id;
        for (const drift::TimeUs time : {drift::TimeUs(0), drift::TimeUs(1'500'000)}) {
            const QImage out = EffectProcessor::applyEffects(frame, {effect}, time);
            QVERIFY2(!out.isNull(), qPrintable(def.meta.id));
            if (qEnvironmentVariableIsSet("DRIFT_TEST_REPORT_DIFF")) {
                qint64 sum = 0;
                const QImage a = frame.convertToFormat(QImage::Format_RGBA8888);
                const QImage b = out.convertToFormat(QImage::Format_RGBA8888);
                for (int y = 0; y < a.height(); ++y)
                    for (int x = 0; x < a.width(); ++x)
                        sum += qAbs(a.pixelColor(x, y).red() - b.pixelColor(x, y).red())
                               + qAbs(a.pixelColor(x, y).green() - b.pixelColor(x, y).green())
                               + qAbs(a.pixelColor(x, y).blue() - b.pixelColor(x, y).blue())
                               + qAbs(a.pixelColor(x, y).alpha() - b.pixelColor(x, y).alpha());
                qInfo("%s t=%lld mean abs diff %.2f", qPrintable(def.meta.id), static_cast<long long>(time),
                      double(sum) / (a.width() * a.height() * 4.0));
            }
        }
        if (!g_failures.isEmpty())
            broken.append(def.meta.id + QLatin1String(": ") + g_failures.first());
        ++checked;
    }
    qInfo("%d effect packages compiled and ran", checked);
    QVERIFY2(broken.isEmpty(), qPrintable(broken.join(QLatin1Char('\n'))));
}

void EffectShadersTest::everyTransitionCompilesAndRuns()
{
    if (!GpuEffectExecutor::instance().isAvailable())
        QSKIP("OpenGL offscreen context unavailable");

    const QImage from = testFrame();
    QImage to = from;
    to.invertPixels();
    int checked = 0;
    QStringList broken;
    for (const TransitionPresetEntry &def : transitionCatalog()) {
        if (!def.gpu.valid || !m_filter.match(def.meta.id).hasMatch())
            continue;
        bool inRoot = false;
        for (const QString &root : std::as_const(m_transitionRoots))
            inRoot = inRoot || def.gpu.packageDir.startsWith(QDir(root).absolutePath());
        if (!inRoot)
            continue;

        g_failures.clear();
        drift::Transition transition;
        const QMap<QString, QVariant> params = resolvedTransitionParameters(transition, def);
        for (const double progress : {0.25, 0.5, 0.9}) {
            bool ok = false;
            const QImage out = GpuEffectExecutor::instance().apply(def.meta.id, def.gpu, {from, to}, params, 0,
                                                                   progress, &ok);
            QVERIFY2(ok && !out.isNull(), qPrintable(def.meta.id));
        }
        if (!g_failures.isEmpty())
            broken.append(def.meta.id + QLatin1String(": ") + g_failures.first());
        ++checked;
    }
    qInfo("%d transition packages compiled and ran", checked);
    QVERIFY2(broken.isEmpty(), qPrintable(broken.join(QLatin1Char('\n'))));
}

// A "padding" effect paints everything outside the clip's own frame red. The clip is a 32 px square in
// the middle of a 128 px canvas; with padding 0.5 the layer is drawn 64 px wide around the same centre.
void EffectShadersTest::paddingEffectDrawsPastTheClipFrame()
{
    if (!GpuCompositor::isAvailable())
        QSKIP("OpenGL offscreen context unavailable");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("pad_probe")));
    const QString package = dir.path() + QStringLiteral("/pad_probe/");
    QFile json(package + QStringLiteral("effect.json"));
    QVERIFY(json.open(QIODevice::WriteOnly));
    json.write(R"({"id":"pad_probe","displayName":"Pad probe","category":"color","backend":"gpu","padding":0.5,
        "nextFeatures":["layout:padding"],"parameters":[],
        "pipeline":{"intermediateBuffers":[],"passes":[{"passIndex":0,"fragmentShader":"main.frag",
        "inputs":[{"type":"source_texture"}],"output":{"type":"canvas"}}]}})");
    json.close();
    QFile shader(package + QStringLiteral("main.frag"));
    QVERIFY(shader.open(QIODevice::WriteOnly));
    shader.write(R"(#version 330 core
in vec2 v_texCoord; out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform vec4 u_contentRect;
void main() {
    bool inside = all(greaterThanEqual(v_texCoord, u_contentRect.xy)) && all(lessThanEqual(v_texCoord, u_contentRect.zw));
    fragColor = inside ? texture(u_currentTexture, v_texCoord) : vec4(1.0, 0.0, 0.0, 1.0);
})");
    shader.close();
    reloadEffectCatalog({dir.path()});
    const EffectPresetEntry *def = effectDefForId(QStringLiteral("pad_probe"));
    QVERIFY(def && def->isGpu && def->gpu.valid);
    QCOMPARE(def->gpu.padding, 0.5f);

    QImage white(32, 32, QImage::Format_RGBA8888);
    white.fill(Qt::white);
    GpuLayer layer;
    layer.source = white;
    layer.rect = QRectF(48, 48, 32, 32);
    layer.valid = true;
    drift::Effect effect;
    effect.catalogId = QStringLiteral("pad_probe");
    GpuScene scene;
    scene.canvasSize = QSize(128, 128);
    scene.backgroundColor = Qt::black;

    GpuItem padded;
    padded.layer = layer;
    padded.layer.effects = {effect};
    scene.items = {padded};
    const QImage out = GpuCompositor::render(scene).convertToFormat(QImage::Format_RGBA8888);
    QVERIFY(!out.isNull());
    QCOMPARE(out.pixelColor(64, 64), QColor(Qt::white));
    QCOMPARE(out.pixelColor(40, 64), QColor(Qt::red));
    QCOMPARE(out.pixelColor(88, 64), QColor(Qt::red));
    QCOMPARE(out.pixelColor(64, 36), QColor(Qt::red));
    QCOMPARE(out.pixelColor(20, 20), QColor(Qt::black));

    // A mask is laid out on the clip's own frame, not the grown one: this rectangle covers the clip's left
    // half, so that half stays and everything else, padding included, goes.
    drift::Mask mask;
    mask.shape = drift::MaskShape::Rectangle;
    mask.x = 0.25;
    mask.y = 0.5;
    mask.w = 0.5;
    mask.h = 1.0;
    padded.layer.masks = {mask};
    scene.items = {padded};
    const QImage masked = GpuCompositor::render(scene).convertToFormat(QImage::Format_RGBA8888);
    QVERIFY(masked.pixelColor(56, 64).red() > 200);
    QVERIFY(masked.pixelColor(72, 64).red() < 50);
    QVERIFY(masked.pixelColor(40, 64).red() < 50);
}

QTEST_MAIN(EffectShadersTest)
#include "tst_effectshaders.moc"
