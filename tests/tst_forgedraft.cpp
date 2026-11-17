#include "app/forge/ForgeDraft.h"
#include "app/forge/ForgePreview.h"
#include "core/project/Project.h"
#include "engine/gpu/effects/GpuEffectExecutor.h"
#include "engine/packages/PackageThumbnails.h"
#include "engine/packages/animations/AnimationCatalog.h"
#include "engine/packages/audio/AudioEffectCatalog.h"
#include "engine/packages/effects/EffectCatalog.h"
#include "engine/packages/transitions/TransitionCatalog.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QStandardPaths>
#include <QtTest>

namespace {

QJsonObject effectManifest(double defaultValue)
{
    return QJsonObject{
        {"id", "my.draft"},
        {"displayName", "Draft"},
        {"category", "color"},
        {"backend", "gpu"},
        {"parameters",
         QJsonArray{QJsonObject{{"identifier", "amount"},
                                {"displayName", "Amount"},
                                {"type", "float"},
                                {"minValue", 0},
                                {"maxValue", 1},
                                {"defaultValue", defaultValue}}}},
        {"pipeline",
         QJsonObject{{"intermediateBuffers", QJsonArray{}},
                     {"passes",
                      QJsonArray{QJsonObject{
                          {"passIndex", 0},
                          {"fragmentShader", "main.frag"},
                          {"inputs", QJsonArray{QJsonObject{{"type", "source_texture"}}}},
                          {"output", QJsonObject{{"type", "canvas"}}}}}}}}};
}

const QByteArray kFrag =
    "#version 330 core\nin vec2 v_texCoord; out vec4 fragColor;\nuniform sampler2D u_currentTexture; uniform float amount;\n"
    "void main() { fragColor = texture(u_currentTexture, v_texCoord) * amount; }\n";

drift::Project oneClipProject()
{
    drift::Project project;
    project.tracks().append(drift::Track{.type = drift::TrackType::Video});
    drift::Clip clip;
    clip.id = "clip1";
    project.tracks()[0].clips.append(clip);
    return project;
}

const drift::Clip &firstClip(const drift::Project &project)
{
    return project.tracks().at(0).clips.at(0);
}

} // namespace

class TstForgeDraft : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    void effectDraftLifecycle()
    {
        using namespace drift::forge;
        const QMap<QString, QByteArray> files{{"main.frag", kFrag}};
        const DraftResult first = stageDraft("effect", effectManifest(0.25), files);
        QVERIFY2(first.ok, qPrintable(first.error));

        const EffectPresetEntry *def = effectDefForId(kDraftEffectId);
        QVERIFY(def);
        QCOMPARE(def->meta.id, QString(kDraftEffectId));
        QCOMPARE(def->meta.parameters.at(0).defaultValue, 0.25);
        for (const EffectPresetEntry &entry : effectCatalog())
            QVERIFY(entry.meta.id != kDraftEffectId);

        const DraftResult second = stageDraft("effect", effectManifest(0.75), files);
        QVERIFY2(second.ok, qPrintable(second.error));
        def = effectDefForId(kDraftEffectId);
        QVERIFY(def);
        QCOMPARE(def->meta.parameters.at(0).defaultValue, 0.75);

        clearDraft("effect");
        QVERIFY(effectDefForId(kDraftEffectId) == nullptr);
    }

    void invalidKeepsPrevious()
    {
        using namespace drift::forge;
        const QMap<QString, QByteArray> files{{"main.frag", kFrag}};
        QVERIFY(stageDraft("effect", effectManifest(0.5), files).ok);

        QJsonObject bad = effectManifest(0.1);
        bad.remove("pipeline");
        const DraftResult result = stageDraft("effect", bad, files);
        QVERIFY(!result.ok);
        QVERIFY(!result.error.isEmpty());

        const EffectPresetEntry *def = effectDefForId(kDraftEffectId);
        QVERIFY(def);
        QCOMPARE(def->meta.parameters.at(0).defaultValue, 0.5);

        const DraftResult badFile = stageDraft("effect", effectManifest(0.1), {{"../x.frag", kFrag}});
        QVERIFY(!badFile.ok);
        clearDraft("effect");
    }

    void brokenShaderKeepsPrevious()
    {
        using namespace drift::forge;
        QVERIFY(stageDraft("effect", effectManifest(0.5), {{"main.frag", kFrag}}).ok);
        const DraftResult result = stageDraft("effect", effectManifest(0.9), {{"main.frag", "not glsl"}});
        if (result.shaderSkipped)
            QSKIP("GL unavailable, shaders were not compiled");
        QVERIFY(!result.ok);
        QVERIFY(!result.shaderLog.isEmpty());
        QCOMPARE(effectDefForId(kDraftEffectId)->meta.parameters.at(0).defaultValue, 0.5);
        clearDraft("effect");
    }

    void patchesApplyToCopyOnly()
    {
        const drift::Project original = oneClipProject();

        drift::Project copy = original.detachedCopy();
        drift::forge::patchAddEffect("clip1", {{"amount", 0.3}})(copy);
        QCOMPARE(firstClip(copy).effects.size(), 1);
        QCOMPARE(firstClip(copy).effects.at(0).catalogId, QString(kDraftEffectId));
        QCOMPARE(firstClip(copy).effects.at(0).parameters.value("amount").toDouble(), 0.3);
        QCOMPARE(firstClip(original).effects.size(), 0);

        drift::Project animCopy = original.detachedCopy();
        drift::forge::patchAnimation("clip1", false, {{"scale", 4.0}}, 800000)(animCopy);
        QCOMPARE(firstClip(animCopy).animOut.kindId, QString(kDraftAnimationId));
        QCOMPARE(firstClip(animCopy).animOut.durationUs, drift::TimeUs(800000));
        QVERIFY(firstClip(animCopy).animIn.isNone());
        QVERIFY(firstClip(original).animOut.isNone());

        drift::Project audioCopy = original.detachedCopy();
        drift::forge::patchAudioEffect("clip1", {})(audioCopy);
        QCOMPARE(firstClip(audioCopy).audioEffects.at(0).catalogId, QString(kDraftAudioId));
        QCOMPARE(firstClip(original).audioEffects.size(), 0);
    }

    void effectThumbnailRenders()
    {
        using namespace drift::forge;
        if (!GpuEffectExecutor::instance().isAvailable())
            QSKIP("GL unavailable");
        QVERIFY(stageDraft("effect", effectManifest(1.0), {{"main.frag", kFrag}}).ok);
        const EffectPresetEntry *def = effectDefForId(kDraftEffectId);
        QVERIFY(def);
        const QImage thumb = renderEffectThumbnail(kDraftEffectId, def->gpu, {{"amount", 1.0}}, QSize(64, 64));
        QVERIFY(!thumb.isNull());
        QCOMPARE(thumb.size(), QSize(64, 64));
        clearDraft("effect");
    }

    void transitionPatchInsertsAtBareCut()
    {
        drift::Project original = oneClipProject();
        drift::Clip second;
        second.id = "clip2";
        original.tracks()[0].clips.append(second);

        drift::Project copy = original.detachedCopy();
        drift::forge::patchTransition("clip1", "clip2", {{"k", 1}}, 400000)(copy);
        QCOMPARE(copy.tracks().at(0).transitions.size(), 1);
        QCOMPARE(copy.tracks().at(0).transitions.at(0).kindId, QString(kDraftTransitionId));
        QCOMPARE(copy.tracks().at(0).transitions.at(0).durationUs, drift::TimeUs(400000));
        QCOMPARE(original.tracks().at(0).transitions.size(), 0);

        drift::forge::patchTransition("clip1", "clip2", {}, 200000)(copy);
        QCOMPARE(copy.tracks().at(0).transitions.size(), 1);
    }
};

QTEST_MAIN(TstForgeDraft)
#include "tst_forgedraft.moc"
