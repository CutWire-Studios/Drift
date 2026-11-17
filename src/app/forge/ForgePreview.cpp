#include "app/forge/ForgePreview.h"

#include "core/project/Project.h"
#include "engine/packages/animations/AnimationCatalog.h"
#include "engine/packages/audio/AudioEffectCatalog.h"
#include "engine/packages/effects/EffectCatalog.h"
#include "engine/packages/transitions/TransitionCatalog.h"

namespace drift::forge {

namespace {

template <typename Fn>
void forClip(drift::Project &project, const QString &clipId, Fn &&fn)
{
    project.forEachTrackList([&](QList<drift::Track> &tracks) {
        for (drift::Track &track : tracks) {
            for (drift::Clip &clip : track.clips) {
                if (clip.id == clipId)
                    fn(clip);
            }
        }
    });
}

} // namespace

PreviewPatch patchAddEffect(const QString &clipId, const QMap<QString, QVariant> &params)
{
    return [clipId, params](drift::Project &project) {
        forClip(project, clipId, [&](drift::Clip &clip) {
            drift::Effect effect;
            effect.name = kDraftEffectId;
            effect.catalogId = kDraftEffectId;
            effect.parameters = params;
            clip.effects.append(effect);
        });
    };
}

PreviewPatch patchTransition(const QString &fromClipId, const QString &toClipId, const QMap<QString, QVariant> &params,
                             drift::TimeUs durationUs)
{
    return [=](drift::Project &project) {
        project.forEachTrackList([&](QList<drift::Track> &tracks) {
            for (drift::Track &track : tracks) {
                if (!drift::clipById(track, fromClipId) || !drift::clipById(track, toClipId))
                    continue;
                drift::Transition *target = nullptr;
                for (drift::Transition &t : track.transitions) {
                    if (t.fromClipId == fromClipId && t.toClipId == toClipId)
                        target = &t;
                }
                if (!target) {
                    track.transitions.append(drift::Transition{});
                    target = &track.transitions.last();
                    target->id = QStringLiteral("forge.draft");
                    target->fromClipId = fromClipId;
                    target->toClipId = toClipId;
                }
                target->kindId = kDraftTransitionId;
                target->parameters = params;
                target->durationUs = durationUs;
            }
        });
    };
}

PreviewPatch patchAnimation(const QString &clipId, bool in, const QMap<QString, QVariant> &params,
                            drift::TimeUs durationUs)
{
    return [=](drift::Project &project) {
        forClip(project, clipId, [&](drift::Clip &clip) {
            drift::ClipAnimation &anim = in ? clip.animIn : clip.animOut;
            anim.kindId = kDraftAnimationId;
            anim.parameters = params;
            anim.durationUs = durationUs;
        });
    };
}

PreviewPatch patchAudioEffect(const QString &clipId, const QMap<QString, QVariant> &params)
{
    return [clipId, params](drift::Project &project) {
        forClip(project, clipId, [&](drift::Clip &clip) {
            drift::Effect effect;
            effect.name = kDraftAudioId;
            effect.catalogId = kDraftAudioId;
            effect.parameters = params;
            clip.audioEffects.append(effect);
        });
    };
}

} // namespace drift::forge
