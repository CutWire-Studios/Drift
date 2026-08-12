#!/usr/bin/env bash
# Sync Lucide icons used by Drift from a local lucide-icons checkout.
# Copies SVG sources and rasterises 48px PNGs for QML (Qt Image has no SVG decoder here).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${LUCIDE_ICONS_DIR:-$ROOT/../lucide-icons-1.25.0/icons}"
DEST="$ROOT/resources/icons"

if [[ ! -d "$SRC" ]]; then
  echo "Lucide icons not found at: $SRC" >&2
  echo "Set LUCIDE_ICONS_DIR to the icons/ directory of lucide-icons." >&2
  exit 1
fi

if ! command -v rsvg-convert >/dev/null 2>&1; then
  echo "rsvg-convert is required to build PNG icons." >&2
  exit 1
fi

mkdir -p "$DEST"
icons=(
  scissors chevrons-left undo redo clipboard-paste copy-plus copy trash-2 snowflake
  bookmark layers magnet link-2 unlink-2 fold-horizontal zoom-out zoom-in gauge play pause
  maximize folder headphones type smile wand-sparkles sparkles sliders-horizontal settings upload plus
  volume-2 volume-off eye eye-off film music image shapes chevron-down chevron-up chevrons-right x
  message-square moon sun grid-3x3 list arrow-down-a-z grip-vertical
  # Fit timeline in view (manual zoom calibration)
  chevrons-left-right-ellipsis
  save arrow-right-to-line arrow-left-to-line tags blend option square-dashed puzzle info
  # Project bundle: package/properties entries in the project menu
  package file-text
  # Status / feedback (toasts, inline errors, empty states)
  triangle-alert circle-check circle-x loader-circle refresh-cw download
  # Affordances (reset, search, disclosure, editing, locking)
  rotate-ccw search chevron-right chevron-left check pencil clock lock lock-open
  # Text alignment controls (properties panel)
  text-align-start text-align-center text-align-end
  # Lucide names these *-horizontal; they are used for vertical text alignment.
  align-start-horizontal align-center-horizontal align-end-horizontal
  # Timeline trim tools — vertical marks read as “keep from here”.
  align-start-vertical align-end-vertical
  # Preview transport: frame step and time jump, either side of play/pause.
  step-back step-forward rewind fast-forward
  # Timeline / media
  captions list-video move-horizontal mouse-pointer audio-lines video
  smartphone monitor square ratio
  # Shortcuts tab and canvas crop tool
  keyboard crop minimize
  # Keyframe row: add/remove a key at the playhead
  diamond-plus diamond-minus
  # Audio-effect Sounds browser (per-preset Lucide glyphs)
  phone bot radio-receiver megaphone droplets volume-x binary cpu gem disc-3
  cassette-tape orbit rabbit arrow-down-wide-narrow shield activity audio-waveform
  repeat reply layers circle-dot-dashed circle-dashed unfold-horizontal door-closed
  ear equal maximize
  # Masking: coverage view, polygon authoring, image/video mask sources
  contrast pen-tool image-play
)

for name in "${icons[@]}"; do
  cp "$SRC/$name.svg" "$DEST/$name.svg"
  sed 's/stroke="currentColor"/stroke="#ffffff"/g' "$DEST/$name.svg" \
    | rsvg-convert -w 48 -h 48 -f png -o "$DEST/$name.png"
done

echo "Synced ${#icons[@]} icons (SVG + PNG) to $DEST"
