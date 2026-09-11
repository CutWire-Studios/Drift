#pragma once

#include <QString>

namespace drift {

enum class BlendMode { Normal, Multiply, Screen, Overlay, Add, Darken, Lighten };

QString blendModeToString(BlendMode mode);
BlendMode blendModeFromString(const QString &mode);

} // namespace drift
