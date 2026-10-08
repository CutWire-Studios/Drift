#!/usr/bin/env bash
# Regenerate the static fallback icon from the Icon Composer document.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/resources/macos/Drift.icns"
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
cp "$WORK/Drift.icns" "$OUT"
echo "Wrote $OUT"
