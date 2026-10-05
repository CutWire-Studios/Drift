#include "engine/audio/PedalCatalog.h"

#include <juce_core/juce_core.h>

namespace drift::audiofx {

namespace {

using Scale = KnobSpec::Scale;

KnobSpec lin(const char *id, const char *label, float min, float max, float def, const char *unit = "")
{
    return {id, label, min, max, def, Scale::Linear, unit};
}

KnobSpec log(const char *id, const char *label, float min, float max, float def, const char *unit = "")
{
    return {id, label, min, max, def, Scale::Log, unit};
}

std::vector<PedalSpec> buildSpecs()
{
    std::vector<PedalSpec> specs{
        // ---- utility ----
        {"classic.eq3", "3-Band EQ", "utility", 50,
         {lin("low", "Low", -12, 12, 0, "dB"), lin("mid", "Mid", -12, 12, 0, "dB"),
          lin("high", "High", -12, 12, 0, "dB")}},
        {"classic.compressor", "Compressor", "utility", 150,
         {lin("threshold", "Threshold", -40, -2, -18, "dB"), lin("ratio", "Ratio", 1.5f, 12, 3, ":1"),
          log("attack", "Attack", 1, 200, 20, "ms"), log("release", "Release", 20, 1000, 250, "ms"),
          lin("makeup", "Makeup Gain", 1, 4, 2, "x")}},
        {"classic.limiter", "Limiter", "utility", 150,
         {lin("drive", "Drive", 1, 4, 1, "x"), lin("ceiling", "Ceiling", 0.5f, 1, 0.95f)}},
        {"classic.gate", "Noise Gate", "utility", 150,
         {log("threshold", "Threshold", 0.001f, 0.2f, 0.02f), lin("ratio", "Strength", 1, 9, 3),
          log("attack", "Attack", 1, 100, 10, "ms"), log("release", "Release", 20, 1000, 250, "ms")}},
        {"classic.deesser", "De-esser", "utility", 50,
         {lin("intensity", "Intensity", 0, 1, 0.35f), lin("amount", "Max Reduction", 0, 1, 0.5f),
          lin("frequency", "Frequency", 0, 1, 0.5f)}},
        {"classic.leveler", "Voice Leveler", "utility", 300,
         {lin("strength", "Strength", 1, 20, 6), lin("peak", "Target Peak", 0.5f, 1, 0.95f)}},

        // ---- voice ----
        {"classic.pitch", "Pitch Shift", "voice", 300, {log("pitch", "Pitch", 0.5f, 2, 1, "x")}},
        {"classic.darklord", "Dark Lord", "voice", 600,
         {lin("pitch", "Pitch", 0.6f, 0.95f, 0.8f, "x"), lin("echo_delay", "Echo Delay", 10, 100, 40, "ms"),
          lin("echo_decay", "Echo Decay", 0.1f, 0.9f, 0.4f), lin("grit", "Distortion", 0, 1, 0.3f)}},
        {"classic.vibrato", "Vibrato", "voice", 100,
         {log("rate", "Rate", 0.1f, 20, 4, "Hz"), lin("depth", "Depth", 0, 1, 0.6f)}},

        // ---- space ----
        {"classic.tremolo", "Tremolo", "space", 0,
         {{"rate", "Rate", 0.1f, 200, 5, Scale::Log, "Hz", {"freq"}}, lin("depth", "Depth", 0, 1, 0.7f)}},
        {"classic.flanger", "Flanger", "space", 150,
         {{"rate", "Rate", 0.1f, 10, 0.5f, Scale::Log, "Hz", {"speed"}},
          lin("phase", "Stereo Phase", 0, 360, 90, "deg"), lin("delay", "Delay", 0, 30, 5, "ms"),
          lin("depth", "Depth", 0, 20, 2, "ms"), lin("regen", "Feedback", -95, 95, 0, "%"),
          lin("mix", "Wet Mix", 0, 100, 71, "%"), {"invert", "Invert Phase", 0, 1, 0, Scale::Toggle}}},
        {"classic.autopan", "Auto-Pan", "space", 0,
         {log("rate", "Rate", 0.01f, 10, 0.5f, "Hz"), lin("amount", "Amount", 0, 1, 1),
          lin("level_in", "Input Level", 0, 4, 1, "x"), lin("level_out", "Output Level", 0, 4, 1, "x")}},
        {"classic.chorus", "Chorus", "space", 150,
         {lin("in_gain", "Input", 0, 1, 0.6f), lin("out_gain", "Output", 0, 1, 0.9f),
          lin("delay", "Delay", 1, 100, 55, "ms"), lin("decay", "Decay", 0, 1, 0.4f),
          log("speed", "Speed", 0.1f, 5, 0.25f, "Hz"), lin("depth", "Depth", 0, 10, 2, "ms")}},
        {"classic.echo", "Echo", "space", 800,
         {log("delay", "Delay", 5, 900, 60, "ms"), lin("decay", "Decay", 0.1f, 0.9f, 0.4f),
          lin("in_gain", "Input", 0, 1, 0.8f), lin("out_gain", "Wet Mix", 0, 1, 0.88f)}},
        {"classic.phaser", "Phaser", "space", 150,
         {log("speed", "Speed", 0.1f, 2, 0.5f, "Hz"), lin("in_gain", "Input", 0, 1, 0.6f),
          lin("out_gain", "Output", 0, 1, 0.7f), lin("delay", "Delay", 0, 5, 3, "ms"),
          lin("decay", "Decay", 0, 1, 0.4f)}},
        {"classic.stereowiden", "Stereo Widen", "space", 50,
         {lin("drymix", "Dry Mix", 0, 1, 0.8f), lin("delay", "Delay", 1, 100, 20, "ms"),
          lin("feedback", "Feedback", 0, 1, 0.3f), lin("crossfeed", "Crossfeed", 0, 1, 0.3f)}},

        // ---- transmission ----
        {"classic.bandlimit", "Band Limit", "transmission", 50,
         {log("low_cut", "High-Pass", 100, 800, 300, "Hz"), log("high_cut", "Low-Pass", 2000, 6000, 3400, "Hz")}},
        {"classic.walkie", "Walkie-Talkie", "transmission", 50,
         {lin("grit", "Grit", 0, 1, 0.4f), log("low_cut", "High-Pass", 200, 800, 400, "Hz"),
          log("high_cut", "Low-Pass", 1500, 5000, 3000, "Hz")}},
        {"classic.megaphone", "Megaphone", "transmission", 50,
         {log("center", "Center Freq", 300, 4000, 1500, "Hz"), lin("width", "Band Width", 200, 3000, 1200, "Hz"),
          lin("grit", "Distortion", 0, 1, 0.5f)}},
        {"classic.underwater", "Underwater", "transmission", 150,
         {log("cutoff", "Cutoff", 100, 2000, 500, "Hz"), lin("wet", "Chorus", 0, 1, 0.6f),
          log("motion", "Motion", 0.1f, 2, 0.25f, "Hz")}},
        {"classic.muffled", "Muffled", "transmission", 50,
         {log("cutoff", "Cutoff", 200, 4000, 800, "Hz"), lin("gain", "Gain", 0.1f, 2, 1, "x")}},

        // ---- texture ----
        {"classic.tape", "Tape Saturation", "texture", 200,
         {lin("ratio", "Ratio", 1, 20, 4, ":1"), log("threshold", "Threshold", 0.01f, 1, 0.2f),
          log("attack", "Attack", 1, 200, 5, "ms"), log("release", "Release", 10, 1000, 50, "ms")}},
        {"classic.bitcrush", "Bitcrush", "texture", 0,
         {lin("bits", "Bits", 1, 16, 6), lin("samples", "Sample Reduction", 1, 64, 16, "x"),
          lin("mix", "Mix", 0, 1, 0.7f)}},
        {"classic.bitcrush_log", "8-Bit", "texture", 0,
         {lin("bits", "Bits", 1, 16, 4), lin("samples", "Sample Reduction", 1, 64, 8, "x"),
          lin("mix", "Mix", 0, 1, 1)}},
        {"classic.vinyl", "Vinyl", "texture", 100,
         {lin("flutter", "Flutter", 0, 0.5f, 0.15f), log("highpass", "Rumble", 50, 500, 200, "Hz"),
          log("lowpass", "Cutoff", 2000, 12000, 6000, "Hz"), log("wobble", "Wobble Rate", 0.1f, 5, 0.5f, "Hz")}},
        {"classic.crystalizer", "Crystalize", "texture", 0,
         {lin("intensity", "Intensity", 0, 10, 2), lin("colors", "Colors", 2, 64, 8)}},
    };
    return specs;
}

const char *scaleName(Scale scale)
{
    switch (scale) {
    case Scale::Log: return "log";
    case Scale::Toggle: return "toggle";
    case Scale::Linear: break;
    }
    return "linear";
}

} // namespace

const std::vector<PedalSpec> &pedalSpecs()
{
    static const std::vector<PedalSpec> specs = buildSpecs();
    return specs;
}

const PedalSpec *pedalSpec(std::string_view type)
{
    for (const PedalSpec &spec : pedalSpecs()) {
        if (type == spec.type)
            return &spec;
    }
    return nullptr;
}

const KnobSpec *findKnob(const PedalSpec &pedal, std::string_view idOrAlias)
{
    for (const KnobSpec &knob : pedal.knobs) {
        if (idOrAlias == knob.id)
            return &knob;
        for (const char *alias : knob.aliases) {
            if (idOrAlias == alias)
                return &knob;
        }
    }
    return nullptr;
}

std::string_view classicProcessorId(std::string_view type)
{
    constexpr std::string_view prefix = "classic.";
    return type.substr(0, prefix.size()) == prefix ? type.substr(prefix.size()) : std::string_view{};
}

std::string pedalCatalogJson()
{
    juce::Array<juce::var> pedals;
    for (const PedalSpec &spec : pedalSpecs()) {
        juce::Array<juce::var> knobs;
        for (const KnobSpec &knob : spec.knobs) {
            auto *k = new juce::DynamicObject;
            k->setProperty("id", knob.id);
            k->setProperty("label", knob.label);
            k->setProperty("min", knob.min);
            k->setProperty("max", knob.max);
            k->setProperty("default", knob.defaultValue);
            k->setProperty("scale", scaleName(knob.scale));
            k->setProperty("unit", knob.unit);
            if (!knob.aliases.empty()) {
                juce::Array<juce::var> aliases;
                for (const char *alias : knob.aliases)
                    aliases.add(alias);
                k->setProperty("aliases", aliases);
            }
            knobs.add(juce::var(k));
        }
        auto *p = new juce::DynamicObject;
        p->setProperty("type", spec.type);
        p->setProperty("label", spec.label);
        p->setProperty("category", spec.category);
        p->setProperty("prerollMs", spec.prerollMs);
        p->setProperty("knobs", knobs);
        pedals.add(juce::var(p));
    }
    auto *root = new juce::DynamicObject;
    root->setProperty("version", 1);
    root->setProperty("pedals", pedals);
    return juce::JSON::toString(juce::var(root)).toStdString();
}

} // namespace drift::audiofx
