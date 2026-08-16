#!/usr/bin/env bash
# Build Drift.app and wrap it in a self-contained, signed .dmg.
#
#   scripts/package-macos.sh
#   scripts/package-macos.sh --identity "Developer ID Application: ..."
#   scripts/package-macos.sh --build-dir build-macos --skip-build
#
# Signing is ad-hoc unless --identity is given. Notarisation is not done here; run notarytool on
# the .dmg afterwards.
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
    -h|--help)    sed -n '2,9p' "$0"; exit 0 ;;
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
  # No inference runtime ships, as on Linux and Windows; the user installs an Acceleration addon.
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

# -qmldir: the imported Qt Quick modules are separate plugins, found by scanning the sources.
"$MACDEPLOYQT" "$APP" -qmldir="$ROOT/src/qml" -no-codesign -verbose=1

# macdeployqt leaves the build tree's rpaths in place, and dyld searches those before the
# @loader_path entries in the frameworks, so the host's Qt would win over the bundled one.
EXE="$APP/Contents/MacOS/Drift"
rpaths() { otool -l "$EXE" | awk '/LC_RPATH/{f=1} f&&/ path /{print $2; f=0}'; }

while IFS= read -r RPATH; do
  [[ "$RPATH" == "@executable_path/../Frameworks" ]] && continue
  install_name_tool -delete_rpath "$RPATH" "$EXE" 2>/dev/null || true
done < <(rpaths)

# LC_RPATH only: the dependencies are spelled the same way, so a plain grep always matches.
if ! rpaths | grep -qx "@executable_path/../Frameworks"; then
  install_name_tool -add_rpath "@executable_path/../Frameworks" "$EXE"
fi

# macdeployqt copies every plugin in a category, including ones belonging to Qt modules Drift does
# not link — the virtual keyboard is one. Their frameworks are never deployed, so the plugin can
# only fail to load, and the "Cannot resolve rpath" errors macdeployqt printed above are it saying
# so. Drop them rather than sign and ship a binary that cannot resolve.
while IFS= read -r -d '' PLUGIN; do
  while IFS= read -r DEP; do
    [[ -e "$APP/Contents/Frameworks/$DEP" ]] && continue
    echo "Dropping ${PLUGIN#"$APP/Contents/"}: needs $DEP"
    rm -f "$PLUGIN"
    break
  done < <(otool -L "$PLUGIN" | awk -F'@rpath/' '/@rpath\//{print $2}' | awk '{print $1}')
done < <(find "$APP/Contents/PlugIns" -name "*.dylib" -print0)

# Those same modules also get a QML module directory laid down with a symlink to the plugin that
# was never copied. A dangling symlink is enough on its own to fail codesign --deep --strict, and
# a module directory without its plugin is unusable anyway, so the directory goes with it.
DANGLING="$(find "$APP/Contents" -type l ! -exec test -e {} \; -print)"
while IFS= read -r LINK; do
  [[ -n "$LINK" ]] || continue
  MODULE="$(dirname "$LINK")"
  [[ -d "$MODULE" ]] || continue
  echo "Dropping ${MODULE#"$APP/Contents/"}: plugin was never deployed"
  rm -rf "$MODULE"
done <<< "$DANGLING"

find "$APP/Contents/PlugIns" "$APP/Contents/Resources/qml" -type d -empty -delete 2>/dev/null || true

# After install_name_tool, which invalidates any signature. Apple Silicon will not run an
# unsigned binary at all, so "-" (ad-hoc) is the floor rather than an option.
CODESIGN_ARGS=(--force --sign "${IDENTITY:--}" --timestamp=none)
if [[ -n "$IDENTITY" ]]; then
  # Required for notarisation, meaningless ad-hoc.
  CODESIGN_ARGS+=(--options runtime)
fi

# Deepest first: signing the bundle seals its contents.
while IFS= read -r -d '' NESTED; do
  codesign "${CODESIGN_ARGS[@]}" "$NESTED" 2>/dev/null || true
done < <(find "$APP/Contents" \( -name "*.dylib" -o -name "*.framework" \) -print0)
codesign "${CODESIGN_ARGS[@]}" "$APP"
codesign --verify --deep --strict "$APP"

mkdir -p "$DIST_DIR"
rm -f "$DMG"

STAGING="$(mktemp -d)"
trap 'rm -rf "$STAGING"' EXIT
cp -R "$APP" "$STAGING/Drift.app"
ln -s /Applications "$STAGING/Applications"

hdiutil create -volname "Drift $VERSION" -srcfolder "$STAGING" \
  -ov -format UDZO -quiet "$DMG"

echo "Built $DMG ($(du -h "$DMG" | cut -f1))"
