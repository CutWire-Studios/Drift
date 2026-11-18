#include "app/forge/doc/Kinds.h"

namespace drift::forge {

const KindInfo &kindInfo(Kind kind)
{
    static const KindInfo effect{"effect", "Effect", "an effect", "My effect", "artistic",
        {"color", "glitch", "retro", "dreamy", "impact", "artistic", "funny", "blurs"},
        "effects", "effect.json", "thumbnail.png", "effects", "effect_output", true};
    static const KindInfo transition{"transition", "Transition", "a transition", "My transition", "basic",
        {"basic", "geometric", "distortion", "liquid", "stylized", "glitch", "cinematic"},
        "transitions", "transition.json", "preview_strip.png", "transitions", "transition_output", false};
    static const KindInfo animation{"animation", "Animation", "an animation", "My animation", "basic",
        {"basic", "slide", "zoom", "reveal", "glitch", "stylized"},
        "animations", "animation.json", "preview_strip.png", "animations", "animation_output", false};
    static const KindInfo audio{"audio", "Audio effect", "an audio effect", "My audio effect", "space",
        {"space", "texture", "transmission", "utility", "voice"},
        "audio-effects", "audio-effect.json", "preview_strip.png", "audio effects", "transition_output", false};
    switch (kind) {
    case Kind::Effect: return effect;
    case Kind::Transition: return transition;
    case Kind::Animation: return animation;
    case Kind::Audio: return audio;
    }
    return effect;
}

bool kindFromName(const QString &name, Kind *out)
{
    for (Kind k : {Kind::Effect, Kind::Transition, Kind::Animation, Kind::Audio}) {
        if (kindInfo(k).name == name) {
            if (out)
                *out = k;
            return true;
        }
    }
    return false;
}

}
