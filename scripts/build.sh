#!/usr/bin/env bash
# Configures and builds the APK or AAB. Bootstraps the android/ package overlay from the Qt kit's template
# on first run, and the native prebuilt dependencies if they are not there yet.
#
#   scripts/build.sh [abi] [build-type]
#     abi         arm64-v8a (default), armeabi-v7a or x86_64
#     build-type  RelWithDebInfo (default), Debug, Release
#
# Environment:
#   QT_ANDROID_ROOT  Qt for Android kit, e.g. ~/Qt/6.11.1/android_arm64_v8a
#   QT_HOST_PATH     Host Qt of the SAME version, for moc/rcc/androiddeployqt. Defaults to
#                    /usr/lib/qt6 — override if the distro layout trips Qt6HostInfo.
#   ANDROID_SDK_ROOT ~/Android/Sdk
#   ANDROID_NDK_ROOT the NDK version the Qt kit was built against, NOT simply the newest installed.
#                    Mixing NDK majors between Qt, FFmpeg and the app produces libc++ symbol
#                    errors and dlopen failures that get misattributed to something else.
#   DRIFT_ANDROID_PACKAGE_NAME  application id (default org.cutwire.drift; CI uses .ci)
#   DRIFT_ANDROID_APP_NAME      launcher label (default Drift)
#   DRIFT_ANDROID_VERSION_CODE  optional Play-Store integer; unset → CMake derives from
#                               PROJECT_VERSION (same semver as desktop)
#   QT_ANDROID_ABIS             semicolon-separated ABI list packaged into the APK/AAB.
#                               Defaults to the abi argument. A Play Store AAB wants every
#                               ABI in one bundle, with matching Qt kits as siblings of
#                               QT_ANDROID_ROOT.
#   BUILD_AAB=1                 package an Android App Bundle (cmake --target aab) instead of an APK
#   SKIP_APK=1                  compile the native library only (no APK/AAB packaging)
set -euo pipefail

ABI="${1:-arm64-v8a}"
BUILD_TYPE="${2:-RelWithDebInfo}"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build/android-$ABI"

QT_VERSION="6.11.1"
case "$ABI" in
  arm64-v8a)   QT_ABI_DIR="android_arm64_v8a" ;;
  # Qt names the 32-bit ARM kit after the architecture, not after the Android ABI.
  armeabi-v7a) QT_ABI_DIR="android_armv7" ;;
  x86_64)      QT_ABI_DIR="android_x86_64" ;;
  *) echo "unsupported ABI: $ABI (expected arm64-v8a, armeabi-v7a or x86_64)" >&2; exit 1 ;;
esac

: "${QT_ANDROID_ROOT:=$HOME/Qt/$QT_VERSION/$QT_ABI_DIR}"
: "${ANDROID_SDK_ROOT:=$HOME/Android/Sdk}"
# The version every module manifest in the Qt kit names. Not the newest installed: mixing NDK
# majors between Qt, FFmpeg and the app gives libc++ symbol errors and dlopen failures that look
# like something else entirely. Re-derive after a Qt upgrade with:
#   grep -rhoE '"[0-9]+\.[0-9]+\.[0-9]{7,}"' $QT_ANDROID_ROOT/modules/*.json | sort -u
: "${ANDROID_NDK_ROOT:=$ANDROID_SDK_ROOT/ndk/27.2.12479018}"
# The kit ships its own matching host Qt, which beats guessing at a distro layout.
: "${QT_HOST_PATH:=$HOME/Qt/$QT_VERSION/gcc_64}"

# Package identity. The defaults are the release app; CI overrides PACKAGE_NAME / APP_NAME so
# test APKs install alongside a release build rather than being refused for a signature mismatch.
# VERSION_CODE defaults in CMake from PROJECT_VERSION (same semver as desktop); set it only when
# you need a different integer (e.g. github.run_number for successive CI side-loads).
# SKIP_APK=1 builds the native library only — used by the push smoke test.
# BUILD_AAB=1 is the Play Store package; QT_ANDROID_ABIS defaults to the abi argument.
: "${DRIFT_ANDROID_PACKAGE_NAME:=org.cutwire.drift}"
: "${DRIFT_ANDROID_APP_NAME:=Drift}"
: "${QT_ANDROID_ABIS:=$ABI}"
: "${BUILD_AAB:=0}"
: "${SKIP_APK:=0}"

[ -d "$ANDROID_NDK_ROOT" ] || { echo "no NDK at $ANDROID_NDK_ROOT" >&2; exit 1; }
export ANDROID_NDK_ROOT ANDROID_SDK_ROOT

QT_CMAKE="$QT_ANDROID_ROOT/bin/qt-cmake"
[ -x "$QT_CMAKE" ] || { echo "no qt-cmake at $QT_CMAKE" >&2; exit 1; }

# --- android/ overlay --------------------------------------------------------
# Must come from the kit's own template: it declares QtActivity and the android.app.lib_name
# meta-data that tells QtLoader which .so to dlopen. A manifest naming a plain Activity installs
# fine and then dies with ClassNotFoundException.
if [ ! -f "$ROOT/android/AndroidManifest.xml" ]; then
    TEMPLATE="$QT_ANDROID_ROOT/src/android/templates/AndroidManifest.xml"
    [ -f "$TEMPLATE" ] || { echo "no manifest template at $TEMPLATE" >&2; exit 1; }
    mkdir -p "$ROOT/android"
    cp "$TEMPLATE" "$ROOT/android/AndroidManifest.xml"
    echo "bootstrapped android/AndroidManifest.xml from the Qt template."
    echo "Edit it before shipping: label, icon, and"
    echo "  <uses-feature android:glEsVersion=\"0x00030000\" android:required=\"true\"/>"
    echo "Declare no storage permissions — the SAF picker grants per-URI access without them."
fi

# --- native dependencies -----------------------------------------------------
IFS=';' read -ra _abis <<< "$QT_ANDROID_ABIS"
for _abi in "${_abis[@]}"; do
    if [ ! -f "$ROOT/third_party/prebuilt/android/$_abi/lib/libavcodec.a" ]; then
        echo "==> no prebuilt dependencies for $_abi; building them first"
        "$ROOT/third_party/build-android.sh" "$_abi"
    fi
done

# --- configure and build -----------------------------------------------------
CMAKE_ARGS=(
    -G Ninja -S "$ROOT" -B "$BUILD"
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    -DQT_HOST_PATH="$QT_HOST_PATH"
    -DANDROID_SDK_ROOT="$ANDROID_SDK_ROOT"
    -DANDROID_NDK_ROOT="$ANDROID_NDK_ROOT"
    -DQT_ANDROID_ABIS="$QT_ANDROID_ABIS"
    -DDRIFT_BUNDLE_ONNXRUNTIME=OFF
    -DDRIFT_ANDROID_PACKAGE_NAME="$DRIFT_ANDROID_PACKAGE_NAME"
    -DDRIFT_ANDROID_APP_NAME="$DRIFT_ANDROID_APP_NAME"
)
if [ -n "${DRIFT_ANDROID_VERSION_CODE:-}" ]; then
    CMAKE_ARGS+=(-DDRIFT_ANDROID_VERSION_CODE="$DRIFT_ANDROID_VERSION_CODE")
fi

"$QT_CMAKE" "${CMAKE_ARGS[@]}"

cmake --build "$BUILD" --parallel
if [ "$SKIP_APK" = "1" ]; then
    echo "==> SKIP_APK=1: native build only (no androiddeployqt / gradle package)"
    exit 0
fi
if [ "$BUILD_AAB" = "1" ]; then
    cmake --build "$BUILD" --target aab
    echo
    echo "==> AAB:"
    find "$BUILD/android-build" -name '*.aab' -newer "$BUILD/CMakeCache.txt" 2>/dev/null || true
else
    cmake --build "$BUILD" --target apk
    echo
    echo "==> APK:"
    find "$BUILD/android-build" -name '*.apk' -newer "$BUILD/CMakeCache.txt" 2>/dev/null || true
fi
