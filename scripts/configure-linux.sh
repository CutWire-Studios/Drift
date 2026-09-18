#!/usr/bin/env bash
# Configures a desktop Linux build, catching the handful of dependency issues that a plain
# `cmake -B build` does not surface clearly on a fresh machine (or after `rm -rf build`, which
# throws away any Skia this project built for itself — nothing restores it automatically).
#
#   scripts/configure-linux.sh [build-type] [build-dir]
#     build-type  Debug (default), Release, RelWithDebInfo
#     build-dir   build (default)
#
# Environment:
#   QT_DIR             Qt 6 "gcc_64" kit directory, e.g. ~/Qt/6.8.3/gcc_64.
#                       Auto-detected under ~/Qt/*/gcc_64 (newest version) if unset.
#   PKG_CONFIG_PATH     Set this yourself to point pkg-config at an FFmpeg 8.x you built or
#                       downloaded, if your distro's packaged FFmpeg is older (see below).
#   DRIFT_SKIA_TARGET   Overrides the linux-x64 / linux-arm64 auto-detect from `uname -m`.
#   DRIFT_EXTRA_CMAKE_ARGS  Extra args appended verbatim to the cmake invocation.
#
# What this catches that a bare configure does not:
#   - Qt6QuickEffects: some Qt 6 kits (the online installer's default Desktop kit among them)
#     ship Qt6QuickEffectsPrivate but never a public Qt6QuickEffects config, so
#     find_package(Qt6 ... COMPONENTS QuickEffects) fails outright. Falls back to the shim at
#     cmake/qt-shims/Qt6QuickEffects, which aliases the private target, when the real module
#     is missing from the detected kit.
#   - FFmpeg too old: Debian/Ubuntu's packaged FFmpeg lags the 8.x this project needs (see
#     docs/BUILDING.md) — pkg-config still reports success against it, and the failure only
#     shows up as a cryptic "avcodec_get_supported_config not declared" deep into the build of
#     src/engine/Exporter.cpp. Checked here up front instead, against whatever libavcodec
#     pkg-config currently resolves (respecting a PKG_CONFIG_PATH you've already set).
#   - Skia: has no distro package and, unlike everything else here, is not restored by
#     reconfiguring — its prebuilt output lives under third_party/prebuilt/skia/<target>, next to
#     the source tree rather than inside build/, but nothing regenerates it if that directory is
#     missing or was deleted along with build/. Builds it from source (20-40 minutes) if absent.
set -euo pipefail

BUILD_TYPE="${1:-Debug}"
BUILD_DIR_NAME="${2:-build}"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/$BUILD_DIR_NAME"

# --- Qt ----------------------------------------------------------------------------------------
if [ -z "${QT_DIR:-}" ]; then
    # Newest version wins when more than one kit is installed side by side.
    QT_DIR=$(find "$HOME/Qt" -mindepth 2 -maxdepth 2 -type d -name gcc_64 2>/dev/null \
             | sort -t/ -k5 -V | tail -1)
fi
[ -n "$QT_DIR" ] && [ -d "$QT_DIR" ] || {
    echo "no Qt 6 gcc_64 kit found; set QT_DIR to e.g. ~/Qt/6.8.3/gcc_64" >&2
    exit 1
}
echo "==> Qt: $QT_DIR"

CMAKE_ARGS=(-B "$BUILD" -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DCMAKE_PREFIX_PATH="$QT_DIR")

if [ ! -f "$QT_DIR/lib/cmake/Qt6QuickEffects/Qt6QuickEffectsConfig.cmake" ]; then
    echo "==> this Qt kit has no public Qt6QuickEffects module; using cmake/qt-shims instead"
    CMAKE_ARGS+=(-DQt6QuickEffects_DIR="$ROOT/cmake/qt-shims/Qt6QuickEffects")
fi

# --- FFmpeg --------------------------------------------------------------------------------
# Version-number checks are brittle against repackaging; check for the actual symbol this
# project needs instead, in whatever libavcodec pkg-config currently resolves.
if command -v pkg-config >/dev/null && pkg-config --exists libavcodec 2>/dev/null; then
    _avcodec_inc=$(pkg-config --variable=includedir libavcodec 2>/dev/null)
    if [ -z "$_avcodec_inc" ] || ! grep -q "avcodec_get_supported_config" \
        "$_avcodec_inc/libavcodec/avcodec.h" 2>/dev/null; then
        cat >&2 <<EOF
==> libavcodec found by pkg-config ($(pkg-config --modversion libavcodec 2>/dev/null)) is too old
    (missing avcodec_get_supported_config, added in FFmpeg 8.x). This project needs FFmpeg 8.x
    (see docs/BUILDING.md); Debian/Ubuntu's packaged version usually is not there yet.
    Point PKG_CONFIG_PATH at a newer FFmpeg's lib/pkgconfig directory and re-run, e.g. one built
    from https://github.com/CutWire-Studios/FFmpeg-Builds or from source.
EOF
        exit 1
    fi
else
    echo "no libavcodec found by pkg-config; install FFmpeg 8.x dev packages (see docs/BUILDING.md)" >&2
    exit 1
fi
echo "==> FFmpeg: $(pkg-config --modversion libavcodec) (libavcodec, via pkg-config)"

# --- SoundTouch ------------------------------------------------------------------------------
if ! { command -v pkg-config >/dev/null && pkg-config --exists soundtouch 2>/dev/null; }; then
    echo "no soundtouch found by pkg-config; install libsoundtouch-dev (Debian/Ubuntu)," >&2
    echo "soundtouch (Arch), or sound-touch (Homebrew) — see docs/BUILDING.md" >&2
    exit 1
fi

# --- Skia --------------------------------------------------------------------------------------
if [ -z "${DRIFT_SKIA_TARGET:-}" ]; then
    case "$(uname -m)" in
        x86_64)  DRIFT_SKIA_TARGET=linux-x64 ;;
        aarch64) DRIFT_SKIA_TARGET=linux-arm64 ;;
        *) echo "no Skia target mapping for $(uname -m); set DRIFT_SKIA_TARGET" >&2; exit 1 ;;
    esac
fi
if [ ! -f "$ROOT/third_party/prebuilt/skia/$DRIFT_SKIA_TARGET/SkiaConfig.cmake" ]; then
    echo "==> no prebuilt Skia for $DRIFT_SKIA_TARGET; building it from source (20-40 minutes)"
    "$ROOT/third_party/build-skia.sh" "$DRIFT_SKIA_TARGET"
fi

# --- configure -----------------------------------------------------------------------------
if [ -n "${DRIFT_EXTRA_CMAKE_ARGS:-}" ]; then
    # Intentionally unquoted: this is a space-separated list of args, not one path.
    CMAKE_ARGS+=($DRIFT_EXTRA_CMAKE_ARGS)
fi

cmake "${CMAKE_ARGS[@]}"

echo
echo "==> configured. Build with:"
echo "    cmake --build $BUILD_DIR_NAME -j\$(nproc)"
