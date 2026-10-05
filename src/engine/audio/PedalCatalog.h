#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace drift::audiofx {

// One control on a pedal. Ranges are the widest any bundled manifest uses, so a manifest's own
// slider range always fits inside; modulation normalises against these.
struct KnobSpec
{
    // Choice knobs carry the option index as their value.
    enum class Scale { Linear, Log, Toggle, Choice };

    const char *id;
    const char *label;
    float min;
    float max;
    float defaultValue;
    Scale scale = Scale::Linear;
    const char *unit = "";
    // Other identifiers manifests use for the same setter ("speed" on voice.alien's flanger).
    std::vector<const char *> aliases = {};
    std::vector<const char *> options = {};

    // Toggles and choices switch; only continuous knobs take modulation.
    bool continuous() const { return scale == Scale::Linear || scale == Scale::Log; }
};

// A pedal Forge can put on the board. `classic.<processor>` pedals are the factory's fixed chains,
// so every legacy audio-effect.json is a one-pedal board. Modulators reuse the shape: a type, a
// label and knobs, with category "modulator".
struct PedalSpec
{
    const char *type;
    const char *label;
    const char *category;
    int prerollMs;
    std::vector<KnobSpec> knobs;
};

const std::vector<PedalSpec> &pedalSpecs();
const PedalSpec *pedalSpec(std::string_view type);
const std::vector<PedalSpec> &modulatorSpecs();
const PedalSpec *modulatorSpec(std::string_view type);
int knobIndex(const PedalSpec &pedal, std::string_view idOrAlias);
const KnobSpec *findKnob(const PedalSpec &pedal, std::string_view idOrAlias);

// "classic.echo" -> "echo"; empty for anything else.
std::string_view classicProcessorId(std::string_view type);

// The catalog as JSON, which Forge's build script writes to src/audio/pedals.json.
std::string pedalCatalogJson();

} // namespace drift::audiofx
