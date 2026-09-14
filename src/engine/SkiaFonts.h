#pragma once

#include "include/core/SkFontMgr.h"
#include "include/core/SkRefCnt.h"

// Skia font managers. Lottie text layers and SVG <text> resolve families through one of these;
// the text renderer (R4) adds the addon-font manager and typeface lookup here.

namespace drift::skia {

// The platform's font manager: fontconfig on Linux, the system font list on Android, an empty
// manager where no port is compiled in. Created once, shared, thread-safe.
sk_sp<SkFontMgr> systemFontMgr();

} // namespace drift::skia
