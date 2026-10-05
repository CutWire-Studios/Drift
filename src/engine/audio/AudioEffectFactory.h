#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace drift {

class ChainProcessor;

namespace audiofx {

// Build the stage chain a manifest's "processor" key names, with every parameter identifier that
// manifest can carry bound to a stage setter. Returns nullptr for an id the factory does not know.
//
// Values are not applied here — the caller seeds them from resolvedAudioEffectParameters().
std::unique_ptr<ChainProcessor> createProcessor(std::string_view processorId);

// The catalog checks this at load time so a manifest naming a processor that does not exist is
// rejected with a warning instead of turning into a silent no-op effect.
bool hasProcessor(std::string_view processorId);

std::vector<std::string> processorIds();

// Whether `processorId`'s chain binds `paramId` to a setter.
bool bindsParameter(std::string_view processorId, std::string_view paramId);

} // namespace audiofx
} // namespace drift
