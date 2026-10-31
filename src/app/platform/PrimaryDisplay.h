#pragma once

#include <optional>

namespace drift::display {

struct PrimaryDisplay
{
    int width = 0;  // physical pixels
    int height = 0; // physical pixels
    double scale = 1.0; // the OS display scale, 1.25 for 125%
};

// Safe before QApplication exists. Windows only; nullopt elsewhere or when it cannot be read.
std::optional<PrimaryDisplay> primaryDisplay();

} // namespace drift::display
