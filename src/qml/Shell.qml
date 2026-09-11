import QtQuick
import Drift

// The application root. It is deliberately NOT a window.
//
// loadFromModule() picks one root object at startup and cannot re-pick, so as long as the root
// was a shell the choice was frozen at compile time. Building the shell window from here instead
// makes it a runtime decision, which is what lets the phone layout be run and iterated on a
// desktop machine (--shell=mobile) rather than only through an APK build and deploy.
//
// The choice is made ONCE, at startup. An earlier revision re-evaluated it as the window crossed
// a width breakpoint and swapped shells live; that was removed deliberately — see build().
//
// Both shells stay ApplicationWindows. That matters more than it looks: roughly twenty call
// sites reach the root by name through Window.window.* (openSegmentation, configureAndAddAsset,
// pushOverlayModal, openLayoutChooser, …). Demoting the shells to Items under one shared window
// would route all of those through a forwarding layer, and a missed name is a runtime TypeError,
// not a compile error.
//
// Root is an Item rather than a QtObject purely so the Components below can be declared the
// ordinary way: QtObject has no default property. It is never shown and never sized — the shells
// are their own windows.
Item {
    id: shell

    // Set from main.cpp via setInitialProperties: "mobile", "desktop", or "auto".
    property string shellPreference: "auto"

    // "auto" is platform-derived. Window width deliberately does not enter into it: see build().
    readonly property bool mobile: shellPreference === "mobile" ? true
                                 : shellPreference === "desktop" ? false
                                 : (Qt.platform.os === "android" || Qt.platform.os === "ios")

    Component { id: desktopShell; Main { } }
    Component { id: mobileShell; AndroidMain { } }

    property var host: null

    // Created with createObject(null) rather than through a Loader: a Loader wants to parent what
    // it builds into a visual scene, and this root has no scene to offer — the window was
    // constructed and then never shown, with no error reported anywhere. A top-level window has
    // no visual parent by definition, so building it explicitly is the honest expression of it.
    //
    // Called once. Swapping shells live on a window resize was built and then removed, because:
    //
    //   - The two shells do not expose the same API. persistLayout exists only on the desktop
    //     root; inEditor, showEditor, overlayModalCount and toolWindowOpen only on the touch one.
    //     A swap can only cope by probing for each name at runtime, and nothing enforces the
    //     parity that shared components currently happen to rely on.
    //   - It is destructive for something the user need not have meant as a mode change. The
    //     preview renderer cannot be handed between trees, so a swap stops playback and resets
    //     timeline zoom, scroll and navigation depth — triggered by dragging a window edge,
    //     tiling with a keyboard shortcut, or attaching a monitor.
    //   - Width is a poor proxy for "wants a touch UI" in both directions: a mouse user on a
    //     scaled display can fall under the breakpoint, and a tablet can sit above it.
    //
    // Foldables and split-screen genuinely do cross that boundary, but they deserve a designed
    // answer rather than a side effect of a width binding.
    function build() {
        shell.host = (shell.mobile ? mobileShell : desktopShell).createObject(null)
    }

    Component.onCompleted: shell.build()
}
