#!/usr/bin/env bash
# Builds Qt Multimedia's FFmpeg backend (libffmpegmediaplugin.dylib) against Homebrew's Qt and
# FFmpeg, and installs it into Homebrew's Qt plugin directory, where macdeployqt picks it up.
#
# Why: Homebrew configures qtmultimedia with -DQT_FEATURE_ffmpeg=OFF on macOS, so its Qt has only
# the AVFoundation backend. Every QtMultimedia player in Drift (the media preview / trimmer above
# all) then fails on anything AVFoundation cannot open — AVI, MKV, WebM, rawvideo — while the
# timeline, which decodes through Drift's own FFmpeg, plays the same file fine. Building against
# Homebrew's FFmpeg keeps a single libavcodec in the process. Only the plugin is taken: the
# QtMultimedia library has no FFmpeg-dependent code, so Homebrew's same-version build serves it.
#
# Usage: third_party/build-qt-ffmpeg-plugin.sh
#
# Environment:
#   JOBS     ninja parallelism (default: hw.ncpu)
#   OUT_DIR  where the built plugin is kept (default third_party/prebuilt/qt-ffmpeg-plugin/<arch>).
#            A plugin already there for the same qtmultimedia and FFmpeg versions is installed
#            without rebuilding, which is how CI uses its cache.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="${OUT_DIR:-$HERE/prebuilt/qt-ffmpeg-plugin/$(uname -m)}"
JOBS="${JOBS:-$(sysctl -n hw.ncpu)}"
BREW_PREFIX="$(brew --prefix)"

installed_version() { brew list --versions "$1" | awk '{print $NF}'; }
QTMM_VERSION="$(installed_version qtmultimedia)"
FFMPEG_VERSION="$(installed_version ffmpeg)"
STAMP="qtmultimedia $QTMM_VERSION / ffmpeg $FFMPEG_VERSION"

# The plugin uses QtMultimedia's private API, so it must come from exactly the installed version's
# source; brew only fetches the formula's current one.
if [[ -n "$(brew outdated --quiet qtmultimedia)" ]]; then
  echo "Installed qtmultimedia $QTMM_VERSION is outdated; run: brew upgrade qtmultimedia" >&2
  exit 1
fi

if [[ ! -f "$OUT/stamp" || "$(cat "$OUT/stamp")" != "$STAMP" ]]; then
  WORK="$(mktemp -d)"
  trap 'rm -rf "$WORK"' EXIT

  # A build-only dependency of the formula: Homebrew's QtGui CMake package requires the headers.
  brew install --quiet vulkan-headers
  brew fetch --build-from-source qtmultimedia
  tar -xf "$(brew --cache --build-from-source qtmultimedia)" -C "$WORK"
  SRC="$(find "$WORK" -mindepth 1 -maxdepth 1 -type d -name 'qtmultimedia-*')"

  # The same arguments the formula uses, except FFmpeg. FFMPEG_DIR makes FFmpeg required, so a
  # failure to find it stops here instead of silently configuring without the backend again.
  cmake -S "$SRC" -B "$WORK/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_FIND_FRAMEWORK=FIRST \
    -DCMAKE_PREFIX_PATH="$BREW_PREFIX" \
    -DCMAKE_INSTALL_PREFIX="$BREW_PREFIX" \
    -DQT_NO_APPLE_SDK_AND_XCODE_CHECK=ON \
    -DQT_FEATURE_ffmpeg=ON \
    -DFFMPEG_DIR="$BREW_PREFIX/opt/ffmpeg"
  cmake --build "$WORK/build" --parallel "$JOBS" --target QFFmpegMediaPlugin
  DESTDIR="$WORK/stage" cmake --install "$WORK/build/src/plugins/multimedia/ffmpeg"

  # The staged path is Qt's own plugin directory under DESTDIR; keep it so a cached plugin is
  # installed to the same place.
  STAGED="$(find "$WORK/stage" -name libffmpegmediaplugin.dylib)"
  rm -rf "$OUT"
  mkdir -p "$OUT"
  cp "$STAGED" "$OUT/"
  echo "${STAGED#"$WORK/stage"}" > "$OUT/destination"
  echo "$STAMP" > "$OUT/stamp"
fi

DEST="$(cat "$OUT/destination")"
mkdir -p "$(dirname "$DEST")"
cp "$OUT/libffmpegmediaplugin.dylib" "$DEST"
echo "Installed $DEST ($STAMP)"
