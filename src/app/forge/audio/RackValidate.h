#pragma once

#include "app/forge/audio/Rack.h"

namespace drift::forge {

// Problems Drift would reject the board's graph for, in words. Empty when it will load.
QStringList validateRack(const ForgeDoc &doc);

}
