#!/usr/bin/env bash
# Regenerate the static fallback icon from the Icon Composer document.
# Usage: make-icns.sh [output.icns]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="${1:-$ROOT/resources/macos/Drift.icns}"
ICON="$ROOT/resources/macos/Drift.icon"
DEVELOPER_DIR="${DEVELOPER_DIR:-$(/usr/bin/xcode-select -p 2>/dev/null || true)}"

if [[ ! -x "$DEVELOPER_DIR/usr/bin/actool" \
   && -x "/Applications/Xcode.app/Contents/Developer/usr/bin/actool" ]]; then
  DEVELOPER_DIR="/Applications/Xcode.app/Contents/Developer"
fi

ACTOOL="$DEVELOPER_DIR/usr/bin/actool"
if [[ ! -x "$ACTOOL" ]]; then
  echo "actool not found; select Xcode 26 or newer with xcode-select." >&2
  exit 1
fi
export DEVELOPER_DIR

ICTOOL="$DEVELOPER_DIR/../Applications/Icon Composer.app/Contents/Executables/ictool"
if [[ ! -x "$ICTOOL" ]]; then
  echo "Icon Composer's ictool not found in the selected Xcode installation." >&2
  exit 1
fi
# Xcode 26 releases used --export-preview; newer versions use named options.
ICTOOL_HELP="$("$ICTOOL" --help)"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

"$ACTOOL" "$ICON" \
  --compile "$WORK" \
  --app-icon Drift \
  --enable-on-demand-resources NO \
  --target-device mac \
  --platform macosx \
  --minimum-deployment-target 12.0 \
  --output-partial-info-plist "$WORK/partial-info.plist" \
  --output-format human-readable-text \
  --errors --warnings
# actool puts the larger renditions in Assets.car, leaving only small images in
# its .icns. A standalone fallback needs the complete set through 512pt @2x.
# Keep actool's small renditions and render missing sizes from the vector source.
ICONSET="$WORK/Drift.iconset"
iconutil --convert iconset --output "$ICONSET" "$WORK/Drift.icns"
for PT in 16 32 128 256 512; do
  for SCALE in 1 2; do
    SUFFIX=""
    if [[ "$SCALE" -eq 2 ]]; then SUFFIX="@2x"; fi
    PNG="$ICONSET/icon_${PT}x${PT}${SUFFIX}.png"
    if [[ -f "$PNG" ]]; then continue; fi
    RAW="$WORK/icon_${PT}x${PT}${SUFFIX}.png"
    if [[ "$ICTOOL_HELP" == *--export-image* ]]; then
      "$ICTOOL" "$ICON" --export-image --output-file "$RAW" \
        --platform macOS --rendition Default --width "$PT" --height "$PT" --scale "$SCALE"
    else
      "$ICTOOL" "$ICON" --export-preview macOS Light "$PT" "$PT" "$SCALE" "$RAW"
    fi
    sips --js "$ROOT/resources/macos/pad-icon.js" "$RAW" --out "$ICONSET" >/dev/null
    test -s "$PNG"
  done
done
iconutil --convert icns --output "$OUT" "$ICONSET"
echo "Wrote $OUT"
