#!/usr/bin/env python3
"""Convert GPUPixel's lipstick and blush templates into straight-alpha masks for Face Retouch.

GPUPixel's mouth.png and blusher.png are fully opaque: the makeup is painted as how far each
pixel is from white, which only works under a fixed multiply blend. Face Retouch also offers a
custom colour, which needs the coverage on its own, so each pixel t is split into a tint c and an
alpha a with

    t = 1 - a * (1 - c)

so mix(bg, bg * c, a) — the shader's blend — reproduces GPUPixel's bg * t exactly. a is the
pixel's darkness (1 - its lowest channel) over the template's 99th-percentile darkness, clamped to
1, so the painted shading lives in the alpha and a custom colour keeps it. White becomes a = 0.

The originals come from GPUPixel at a pinned commit, Copyright (c) 2021 PixPark, Apache License
2.0; see effects/face_retouch/NOTICE. Needs Pillow.

    python3 tools/convert_makeup_templates.py
"""

from __future__ import annotations

import io
import urllib.request
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = ROOT / "effects" / "face_retouch"

COMMIT = "ef552bf8ce2d0d41fa9b979bfb5c7cf79374ca88"
SOURCE_URL = "https://raw.githubusercontent.com/pixpark/gpupixel/{commit}/src/res/{name}"
TEMPLATES = ("mouth.png", "blusher.png")
PERCENTILE = 0.99


def fetch(name: str) -> Image.Image:
    url = SOURCE_URL.format(commit=COMMIT, name=name)
    with urllib.request.urlopen(url, timeout=60) as response:
        return Image.open(io.BytesIO(response.read())).convert("RGB")


def convert(source: Image.Image) -> tuple[Image.Image, float]:
    raw = source.tobytes()
    pixels = [tuple(raw[i:i + 3]) for i in range(0, len(raw), 3)]
    darkness = sorted(1.0 - min(p) / 255.0 for p in pixels)
    reference = darkness[min(len(darkness) - 1, int(len(darkness) * PERCENTILE))]
    if reference <= 0.0:
        raise SystemExit("template is blank")

    out = []
    for r, g, b in pixels:
        t = (r / 255.0, g / 255.0, b / 255.0)
        d = 1.0 - min(t)
        a = min(d / reference, 1.0)
        if a <= 0.0:
            out.append((255, 255, 255, 0))
            continue
        c = [min(max(1.0 - (1.0 - v) / a, 0.0), 1.0) for v in t]
        out.append(tuple(round(v * 255.0) for v in c) + (round(a * 255.0),))

    mask = Image.new("RGBA", source.size)
    mask.frombytes(bytes(v for p in out for v in p))
    return mask, reference


def main() -> None:
    for name in TEMPLATES:
        mask, reference = convert(fetch(name))
        target = OUT_DIR / name
        mask.save(target, optimize=True)
        print(f"{target.relative_to(ROOT)}: {mask.size[0]}x{mask.size[1]}, "
              f"alpha 1 at darkness {reference:.4f}")


if __name__ == "__main__":
    main()
