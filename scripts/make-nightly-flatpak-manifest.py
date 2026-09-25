#!/usr/bin/env python3
"""Derive the nightly channel's Flatpak manifest from the local (working-tree) one.

    scripts/make-nightly-flatpak-manifest.py <build-id> [output]

Generated rather than committed: the real manifest carries the pinned Skia commit, the JUCE tag
and the whole build recipe, and a second copy would go stale the first time one of those moved.

Only two things differ. The app id, because installing the nightly bundle under
org.cutwire.Drift would replace the user's Flathub install; and DRIFT_CHANNEL, which is what
makes CMake install the .desktop, icon, mime and AppStream files under the nightly id — flatpak's
build-export drops anything in /app/share not named after the app id, so without it the bundle
would export no launcher entry at all.

Text edits, not a YAML round-trip: PyYAML is not guaranteed on a runner, and rewriting the file
through a parser would strip every comment in it.
"""

import sys

SRC = "flatpak/org.cutwire.Drift.yml"
DEFAULT_OUT = "flatpak/org.cutwire.Drift.Nightly.yml"
APP_ID = "org.cutwire.Drift.Nightly"

# The last flag of the manifest's multi-line `cmake -S . -B build` command; the channel flags are
# appended after it with the same continuation indent.
ANCHOR = "        -DDRIFT_AUTO_UPDATE_TRANSLATIONS=OFF\n"


def main() -> int:
    if not 2 <= len(sys.argv) <= 3:
        print(f"usage: {sys.argv[0]} <build-id> [output]", file=sys.stderr)
        return 2
    build_id = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) == 3 else DEFAULT_OUT

    with open(SRC, encoding="utf-8") as f:
        text = f.read()

    if "app-id: org.cutwire.Drift\n" not in text:
        print(f"{SRC}: no 'app-id: org.cutwire.Drift' line to rewrite", file=sys.stderr)
        return 1
    text = text.replace("app-id: org.cutwire.Drift\n", f"app-id: {APP_ID}\n", 1)

    if ANCHOR not in text:
        print(f"{SRC}: cmake flag list no longer ends with {ANCHOR.strip()}", file=sys.stderr)
        return 1
    text = text.replace(
        ANCHOR,
        ANCHOR + "        -DDRIFT_CHANNEL=nightly\n" + f"        -DDRIFT_BUILD_ID={build_id}\n",
        1,
    )

    with open(out, "w", encoding="utf-8") as f:
        f.write(text)
    print(f"Wrote {out} ({APP_ID}, build {build_id})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
