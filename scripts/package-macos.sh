#!/usr/bin/env bash
# Build Drift.app and wrap it in a distributable .dmg.
# macdeployqt copies Qt, FFmpeg, zstd, OpenSSL and SoundTouch into Contents/Frameworks and
# rewrites the install names, so the bundle no longer depends on the build machine's libraries.
#
#   scripts/package-macos.sh
#   scripts/package-macos.sh --identity "Developer ID Application: ..."
#   scripts/package-macos.sh --build-dir build-macos --skip-build
#
# Notarisation needs an Apple Developer account, so it is left out: sign with a Developer ID
# above, then run notarytool on the .dmg.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT/build-macos"
DIST_DIR="$ROOT/dist"
IDENTITY=""
SKIP_BUILD=0
QT_PREFIX=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --identity)   IDENTITY="$2"; shift 2 ;;
    --build-dir)  BUILD_DIR="$ROOT/$2"; shift 2 ;;
    --qt-prefix)  QT_PREFIX="$2"; shift 2 ;;
    --skip-build) SKIP_BUILD=1; shift ;;
    -h|--help)    sed -n '2,11p' "$0"; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done

BREW_PREFIX="$(brew --prefix 2>/dev/null || echo /opt/homebrew)"
QT_PREFIX="${QT_PREFIX:-$BREW_PREFIX/opt/qt6}"
MACDEPLOYQT="$QT_PREFIX/bin/macdeployqt"

if [[ ! -x "$MACDEPLOYQT" ]]; then
  echo "macdeployqt not found at: $MACDEPLOYQT" >&2
  echo "Pass --qt-prefix with the path to your Qt installation." >&2
  exit 1
fi

VERSION="$(sed -n 's/^project(Drift VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"
ARCH="$(uname -m)"
APP="$BUILD_DIR/Drift.app"
DMG="$DIST_DIR/Drift-$VERSION-$ARCH.dmg"

if [[ $SKIP_BUILD -eq 0 ]]; then
  # DRIFT_BUNDLE_ONNXRUNTIME=OFF as in the Linux and Windows packaging: a shipped build carries no
  # inference runtime, the user installs one from the Acceleration addons.
  cmake -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$QT_PREFIX;$BREW_PREFIX/opt/openssl@3;$BREW_PREFIX" \
    -DDRIFT_BUNDLE_ONNXRUNTIME=OFF
  cmake --build "$BUILD_DIR" --target drift --parallel "$(sysctl -n hw.ncpu)"
fi

if [[ ! -d "$APP" ]]; then
  echo "No bundle at: $APP" >&2
  exit 1
fi

# -qmldir: Drift's own QML is compiled into the binary, but the Qt Quick modules it imports are
# separate plugins, and qmlimportscanner finds them by reading the sources.
"$MACDEPLOYQT" "$APP" -qmldir="$ROOT/src/qml" -no-codesign -verbose=1

# macdeployqt rewrites the dependencies but leaves the build tree's rpaths in place. dyld searches
# the executable's rpaths before the @loader_path entries in the nested frameworks, so a bundle
# still listing /opt/homebrew/opt/qt6/lib picks up the host's Qt on any Mac that has one.
EXE="$APP/Contents/MacOS/Drift"
rpaths() { otool -l "$EXE" | awk '/LC_RPATH/{f=1} f&&/ path /{print $2; f=0}'; }

while IFS= read -r RPATH; do
  [[ "$RPATH" == "@executable_path/../Frameworks" ]] && continue
  install_name_tool -delete_rpath "$RPATH" "$EXE" 2>/dev/null || true
done < <(rpaths)

# Matched against the LC_RPATH list, not all of `otool -l`: every bundled dependency is already
# spelled @executable_path/../Frameworks/..., so a plain grep always matches and never adds it.
if ! rpaths | grep -qx "@executable_path/../Frameworks"; then
  install_name_tool -add_rpath "@executable_path/../Frameworks" "$EXE"
fi

# Signed after everything is in place, and not with macdeployqt's -codesign: install_name_tool
# invalidates a signature, and Apple Silicon will not run an unsigned binary at all. "-" is the
# ad-hoc identity, enough to launch locally.
CODESIGN_ARGS=(--force --sign "${IDENTITY:--}" --timestamp=none)
if [[ -n "$IDENTITY" ]]; then
  # Hardened runtime is a precondition for notarisation, and meaningless when signing ad-hoc.
  CODESIGN_ARGS+=(--options runtime)
fi

# Deepest first: signing a bundle seals what is inside it, so anything signed afterwards
# invalidates the enclosing signature.
while IFS= read -r -d '' NESTED; do
  codesign "${CODESIGN_ARGS[@]}" "$NESTED" 2>/dev/null || true
done < <(find "$APP/Contents" \( -name "*.dylib" -o -name "*.framework" \) -print0)
codesign "${CODESIGN_ARGS[@]}" "$APP"
codesign --verify --deep --strict "$APP"

mkdir -p "$DIST_DIR"
rm -f "$DMG"

# Staged so the .dmg holds the app and a symlink to /Applications to drag it onto.
STAGING="$(mktemp -d)"
trap 'rm -rf "$STAGING"' EXIT
cp -R "$APP" "$STAGING/Drift.app"
ln -s /Applications "$STAGING/Applications"

hdiutil create -volname "Drift $VERSION" -srcfolder "$STAGING" \
  -ov -format UDZO -quiet "$DMG"

echo "Built $DMG ($(du -h "$DMG" | cut -f1))"
