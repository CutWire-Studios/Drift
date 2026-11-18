#include "app/forge/compiler/Compiler.h"
#include "app/forge/compiler/Manifest.h"
#include "app/forge/doc/ForgeDoc.h"
#include "app/forge/nodes/Registry.h"

#include <QDir>
#include <QJsonDocument>
#include <QtTest>

using namespace drift::forge;

namespace {

const QString kRoot = QStringLiteral(DRIFT_TEST_DATA_DIR "/forge");

QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QJsonObject readJson(const QString &path)
{
    return QJsonDocument::fromJson(readFile(path)).object();
}

QString firstDifference(const QString &expected, const QString &actual)
{
    const QStringList a = expected.split(QLatin1Char('\n'));
    const QStringList b = actual.split(QLatin1Char('\n'));
    for (int i = 0; i < qMax(a.size(), b.size()); ++i) {
        const QString x = i < a.size() ? a[i] : QStringLiteral("<end>");
        const QString y = i < b.size() ? b[i] : QStringLiteral("<end>");
        if (x != y)
            return QStringLiteral("line %1\n  expected: %2\n  actual:   %3").arg(i + 1).arg(x, y);
    }
    return QStringLiteral("no difference");
}

QString dump(const QJsonObject &o)
{
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Indented));
}

// The web key lists a node's parameter-bound inputs in insertion order; ours are sorted by name.
QJsonArray canonicalKey(const QString &key)
{
    QJsonArray root = QJsonDocument::fromJson(key.toUtf8()).array();
    QJsonArray nodes = root.at(1).toArray();
    for (int i = 0; i < nodes.size(); ++i) {
        QJsonArray n = nodes.at(i).toArray();
        QList<QJsonValue> refs;
        for (const QJsonValue &r : n.at(3).toArray())
            refs << r;
        std::sort(refs.begin(), refs.end(), [](const QJsonValue &a, const QJsonValue &b) {
            return a.toArray().at(0).toString() < b.toArray().at(0).toString();
        });
        QJsonArray sorted;
        for (const QJsonValue &r : refs)
            sorted.append(r);
        n[3] = sorted;
        nodes[i] = n;
    }
    root[1] = nodes;
    return root;
}

void compareSources(const QString &where, const QString &dir, const CompileResult &r, QStringList &problems)
{
    const QStringList files = QDir(dir).entryList({QStringLiteral("*.frag")}, QDir::Files);
    if (files.size() != r.passes.size())
        problems << QStringLiteral("%1: expected %2 passes, got %3").arg(where).arg(files.size()).arg(r.passes.size());
    for (const QString &file : files) {
        const QString expected = QString::fromUtf8(readFile(dir + QLatin1Char('/') + file));
        const CompiledPass *pass = nullptr;
        for (const CompiledPass &p : r.passes)
            if (p.file == file)
                pass = &p;
        if (!pass)
            problems << QStringLiteral("%1/%2: not produced").arg(where, file);
        else if (pass->source != expected)
            problems << QStringLiteral("%1/%2: %3").arg(where, file, firstDifference(expected, pass->source));
    }
}

}

// Ordered NODE_DEFS of web/packages/forge-core; regenerate when the web registry changes.
static const char *const kWebNodeTypes[] = {
    "video",
    "from",
    "to",
    "progress",
    "direction",
    "time",
    "uv",
    "resolution",
    "number",
    "switch_value",
    "color_value",
    "point_value",
    "image",
    "choice",
    "pick",
    "region",
    "seed",
    "other_clip",
    "track_mask",
    "audio_track",
    "depth",
    "face",
    "clip_mask",
    "audio",
    "brightness_contrast",
    "saturation",
    "hue_shift",
    "levels",
    "temperature",
    "tint",
    "gradient_map",
    "invert",
    "posterize",
    "threshold",
    "lut",
    "exposure",
    "tone",
    "selective_color",
    "channel_mixer",
    "lift_gamma_gain",
    "vibrance",
    "color_replace",
    "chroma_key",
    "gradient_ramp",
    "transform",
    "zoom",
    "twirl",
    "wave",
    "ripple",
    "pixelate",
    "mirror",
    "kaleidoscope",
    "lens",
    "displace",
    "shake",
    "tile",
    "corner_pin",
    "polar",
    "hex_pixelate",
    "rotate_3d",
    "sphere",
    "card_flip",
    "cube",
    "doors",
    "page_curl",
    "blur",
    "glow",
    "directional_blur",
    "zoom_blur",
    "chromatic",
    "vignette",
    "sharpen",
    "edges",
    "spin_blur",
    "lens_blur",
    "streaks",
    "emboss",
    "oil_paint",
    "grain",
    "scanlines",
    "halftone",
    "glitch_blocks",
    "vhs",
    "noise",
    "linear_gradient",
    "radial_gradient",
    "shape",
    "stripes",
    "checker",
    "rain",
    "snow",
    "film_damage",
    "grid_cells",
    "mix",
    "blend",
    "over",
    "luma_mask",
    "set_alpha",
    "mask_ops",
    "switch_image",
    "wipe_mask",
    "iris_mask",
    "dissolve_mask",
    "blinds_mask",
    "edge_glow",
    "push",
    "reveal",
    "clock_wipe",
    "dip",
    "lfo",
    "curve",
    "ease",
    "flicker",
    "steps",
    "math",
    "func",
    "remap",
    "smoothstep",
    "split_color",
    "combine_color",
    "split_point",
    "combine_point",
    "mix_value",
    "vector_math",
    "vector_measure",
    "linear_transform",
    "matrix_warp",
    "sample_at",
    "complex_math",
    "derivative",
    "jacobian",
    "custom",
    "effect_output",
    "transition_output",
    "animation_output",
};


class TstForgeCompiler : public QObject
{
    Q_OBJECT
private slots:
    void registryMatchesWeb()
    {
        QStringList expected;
        for (const char *t : kWebNodeTypes)
            expected << QLatin1String(t);
        QStringList actual;
        for (const NodeDef *d : allNodeDefs())
            actual << d->type;
        QCOMPARE(actual, expected);
    }

    void goldenFixtures()
    {
        const QStringList dirs = QDir(kRoot).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        QVERIFY(dirs.size() >= 150);
        int checked = 0;
        int skipped = 0;
        QStringList problems;
        QStringList skippedNames;
        for (const QString &name : dirs) {
            const QString base = kRoot + QLatin1Char('/') + name;
            const QJsonObject json = readJson(base + QStringLiteral("/forge.json"));
            if (json.value("kind").toString() == QLatin1String("audio")) {
                ++skipped;
                continue;
            }
            QString error;
            const ForgeDoc doc = ForgeDoc::fromJson(json, &error);
            QVERIFY2(error.isEmpty(), qPrintable(name + ": " + error));
            QString missing;
            for (const ForgeNode &n : doc.nodes) {
                if (!nodeDef(n.type)) {
                    missing = n.type;
                    break;
                }
            }
            if (!missing.isEmpty()) {
                ++skipped;
                skippedNames << name + QStringLiteral(" (") + missing + QLatin1Char(')');
                continue;
            }
            ++checked;
            const int before = problems.size();

            CompileOptions exportOpts;
            const CompileResult exported = compile(doc, exportOpts);
            if (!exported.ok) {
                problems << name + QStringLiteral(": export compile failed: ") + (exported.errors.isEmpty() ? QString() : exported.errors.first().message);
                continue;
            }
            compareSources(name + QStringLiteral("/export"), base + QStringLiteral("/export"), exported, problems);
            const QString manifestFile = packageManifestName(doc);
            const QJsonObject expectedManifest = readJson(base + QStringLiteral("/export/") + manifestFile);
            const QJsonObject manifest = packageManifest(doc, exported);
            if (manifest != expectedManifest)
                problems << QStringLiteral("%1/export/%2: %3").arg(name, manifestFile, firstDifference(dump(expectedManifest), dump(manifest)));

            CompileOptions previewOpts;
            previewOpts.mode = CompileOptions::Preview;
            const CompileResult preview = compile(doc, previewOpts);
            if (!preview.ok) {
                problems << name + QStringLiteral(": preview compile failed");
                continue;
            }
            compareSources(name + QStringLiteral("/preview"), base + QStringLiteral("/preview"), preview, problems);
            QJsonObject expectedPreview = readJson(base + QStringLiteral("/preview/preview.json"));
            QJsonObject actualPreview = previewJson(doc, preview);
            const QJsonArray expectedKey = canonicalKey(expectedPreview.take("structureKey").toString());
            const QJsonArray actualKey = canonicalKey(actualPreview.take("structureKey").toString());
            if (expectedKey != actualKey)
                problems << name + QStringLiteral("/preview/preview.json: structureKey differs");
            if (actualPreview != expectedPreview)
                problems << QStringLiteral("%1/preview/preview.json: %2").arg(name, firstDifference(dump(expectedPreview), dump(actualPreview)));
            Q_UNUSED(before);
        }
        qInfo().noquote() << "forge compiler fixtures: checked" << checked << "skipped" << skipped;
        for (const QString &s : skippedNames)
            qInfo().noquote() << "  skipped (unregistered):" << s;
        for (const QString &p : problems)
            qWarning().noquote() << p;
        QVERIFY2(problems.isEmpty(), qPrintable(QStringLiteral("%1 mismatches, first: %2").arg(problems.size()).arg(problems.value(0))));
        QVERIFY2(skippedNames.isEmpty(), qPrintable(skippedNames.join(", ")));
        QVERIFY(checked > 0);
    }
};

QTEST_APPLESS_MAIN(TstForgeCompiler)
#include "tst_forgecompiler.moc"
