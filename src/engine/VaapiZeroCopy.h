#pragma once

// Preview zero-copy VAAPI. Resolved from DRIFT_VAAPI_ZEROCOPY (wins, unless the value is "0")
// then QSettings("preview/vaapiZeroCopy"). Unset in both is Auto, which engages the path only
// on the driver combination it has actually been proven on — see GlRuntime's import for why
// that is not simply "on". The settings half is cached after the first call so the GL import
// path never touches QSettings per frame.
//
// applyVaapiZeroCopyXcbEgl() must run after organization/application names are set
// and before QApplication, so Qt's xcb plugin can pick EGL instead of GLX. It acts only on an
// explicit On: switching a user's X11 GL integration is not something a default should do.

namespace drift {

enum class VaapiZeroCopyMode {
    Auto, // nothing configured: let the import decide from the driver
    On,   // explicitly requested, whatever the driver
    Off,  // explicitly refused
};

VaapiZeroCopyMode vaapiZeroCopyMode();
void applyVaapiZeroCopyXcbEgl();

// Preview zero-copy for D3D11VA on Windows. On unless DRIFT_D3D11_ZEROCOPY=0 (wins) or
// preview/d3d11ZeroCopy is false. It needs no driver allow-list the way VAAPI's Auto does: the
// planes stay YUV and go through Drift's own convert shader, so no driver colour conversion is
// involved. The settings half is cached like vaapiZeroCopyMode()'s.
bool d3d11ZeroCopyEnabled();

} // namespace drift
