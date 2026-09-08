#!/usr/bin/env bash
# Signs the APK with the Android debug key and installs it.
#
#   scripts/deploy.sh [abi]
#
# androiddeployqt emits an unsigned release APK for a RelWithDebInfo/Release build, which will not
# install. Debug-signing it here keeps the build type at RelWithDebInfo — a Debug build is not a
# usable alternative, because software video decode in an unoptimised build is far too slow to
# judge playback by.
set -euo pipefail

ABI="${1:-arm64-v8a}"
# Matches the local default in scripts/build.sh; override in step with it.
PKG="${DRIFT_ANDROID_PACKAGE_NAME:-org.cutwire.drift.debug}"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APK="$ROOT/build/android-$ABI/android-build/drift.apk"
SIGNED="$ROOT/build/android-$ABI/drift-signed.apk"

[ -f "$APK" ] || { echo "no APK at $APK — run scripts/build.sh $ABI first" >&2; exit 1; }

APKSIGNER="$(ls "$HOME"/Android/Sdk/build-tools/*/apksigner 2>/dev/null | sort -V | tail -1)"
[ -n "$APKSIGNER" ] || { echo "no apksigner in the SDK build-tools" >&2; exit 1; }

KS="$HOME/.android/debug.keystore"
if [ ! -f "$KS" ]; then
    keytool -genkeypair -keystore "$KS" -storepass android -keypass android \
        -alias androiddebugkey -keyalg RSA -keysize 2048 -validity 10000 \
        -dname "CN=Android Debug,O=Android,C=US"
fi

"$APKSIGNER" sign --ks "$KS" --ks-pass pass:android --key-pass pass:android \
    --ks-key-alias androiddebugkey --out "$SIGNED" "$APK"
"$APKSIGNER" verify "$SIGNED" >/dev/null && echo "signed: $SIGNED"

adb install -r "$SIGNED"
echo "installed $PKG"
