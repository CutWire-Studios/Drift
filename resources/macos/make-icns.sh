#!/usr/bin/env bash
# Regenerate resources/macos/Drift.icns from Drift_icon.png.
# Checked in like resources/windows/drift.ico, because sips and iconutil are macOS-only and a
# Linux or Windows checkout has no way to build it. Run this after changing the icon, and commit.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$ROOT/Drift_icon.png"
OUT="$ROOT/resources/macos/Drift.icns"

if [[ ! -f "$SRC" ]]; then
  echo "Icon source not found at: $SRC" >&2
  exit 1
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
ICONSET="$WORK/Drift.iconset"
mkdir -p "$ICONSET"

# Each size needs both its 1x and 2x file, or Finder upscales the 1x on Retina displays.
for PT in 16 32 128 256 512; do
  sips -z "$PT" "$PT" "$SRC" --out "$ICONSET/icon_${PT}x${PT}.png" >/dev/null
  sips -z "$((PT * 2))" "$((PT * 2))" "$SRC" --out "$ICONSET/icon_${PT}x${PT}@2x.png" >/dev/null
done

iconutil --convert icns --output "$OUT" "$ICONSET"
echo "Wrote $OUT"
