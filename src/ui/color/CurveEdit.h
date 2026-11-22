#pragma once

#include "engine/gpu/effects/LookupTextures.h"

#include <QString>

#include <vector>

// Stored colour curves are JSON; the editing itself lives in drift::manip::CurveGraphModel.
namespace drift::curveedit {

using Points = std::vector<CurvePoint>;

QString toJson(const Points &pts);

} // namespace drift::curveedit
