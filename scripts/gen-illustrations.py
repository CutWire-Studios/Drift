#!/usr/bin/env python3
"""Convert resources/illustrations/src/*.json (Lottie) into dependency-free QML.

lottietoqml output needs QtQuick.Timeline, QtQuick.VectorImage.Helpers and Qt.labs.lottieqt;
this script strips those so the result imports only QtQuick and QtQuick.Shapes, and rebinds the
placeholder colours to Theme.c.illustration.*.

    gen-illustrations.py            regenerate QML
    gen-illustrations.py --render   also write GIFs and a contact sheet to /tmp/drift-illustrations
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC_DIR = ROOT / "resources" / "illustrations" / "src"
OUT_DIR = ROOT / "src" / "qml" / "components" / "illustrations"
DARK_PATH = ROOT / "resources" / "themes" / "dark.json"
RENDER_DIR = Path("/tmp/drift-illustrations")
LOTTIETOQML = "/usr/lib/qt6/bin/lottietoqml"
LOTTIE_VENV_PYTHON = Path.home() / ".venvs" / "lottie" / "bin" / "python"

PLACEHOLDERS = {
    "surface": "#FE0001",
    "foreground": "#02FE01",
    "accent": "#0102FE",
    "muted": "#FDFE02",
}
KEEP_IMPORTS = {"QtQuick", "QtQuick.Shapes"}


def pascal(name: str) -> str:
    return "".join(part[:1].upper() + part[1:] for part in re.split(r"[-_]", name))


def match_brace(text: str, open_index: int) -> int:
    depth = 0
    for i in range(open_index, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return i
    raise ValueError("unbalanced braces")


def remove_block(text: str, header: str) -> str:
    """Delete every block that starts with `header` (a regex ending just before its '{')."""
    while True:
        m = re.search(header + r"\s*\{", text)
        if not m:
            return text
        end = match_brace(text, m.end() - 1)
        line_start = text.rfind("\n", 0, m.start()) + 1
        text = text[:line_start] + text[end + 1:].lstrip(" ").removeprefix("\n")


def flatten_transform_groups(text: str) -> str:
    while True:
        m = re.search(r"transform: TransformGroup\s*\{", text)
        if not m:
            return text
        end = match_brace(text, m.end() - 1)
        items = re.findall(r"(?:Scale|Rotation|Translate)\s*\{[^}]*\}", text[m.end():end])
        indent = re.search(r"[ \t]*$", text[:m.start()]).group(0)
        flat = "transform: [\n" + ",\n".join(f"{indent}    {i}" for i in items) + f"\n{indent}]"
        text = text[:m.start()] + flat + text[end + 1:]


def rebind_colors(text: str, name: str) -> str:
    by_rgb = {v.lstrip("#").lower(): k for k, v in PLACEHOLDERS.items()}

    def sub(m: re.Match) -> str:
        value = m.group(2).lstrip("#").lower()
        if value in ("transparent", "00000000"):
            return m.group(0)
        if len(value) != 8 or value[2:] not in by_rgb:
            raise SystemExit(f"{name}: colour {m.group(2)} is not a placeholder")
        role = by_rgb[value[2:]]
        alpha = int(value[:2], 16)
        ref = f"Theme.c.illustration.{role}"
        if alpha == 255:
            return f"{m.group(1)}: {ref}"
        return f"{m.group(1)}: Qt.alpha({ref}, {round(alpha / 255, 2):g})"

    return re.sub(r'(fillColor|strokeColor): "(#?\w+)"', sub, text)


def timelines_to_animation(text: str, fps: float, total_frames: float) -> str:
    groups = []
    while True:
        m = re.search(r"\n[ \t]*Timeline\s*\{", text)
        if not m:
            break
        end = match_brace(text, m.end() - 1)
        body = text[m.end():end]
        for g in re.finditer(
            r'KeyframeGroup\s*\{\s*target:\s*([^\n]+?)\s*property:\s*"(\w+)"\s*((?:Keyframe\s*\{[^}]*\}\s*)+)\}', body
        ):
            keys = []
            for k in re.finditer(r"Keyframe\s*\{\s*frame:\s*([\d.]+);\s*value:\s*([^;}]+?)\s*(?:;\s*([^}]*?))?\s*\}", g.group(3)):
                keys.append((float(k.group(1)), k.group(2), (k.group(3) or "").strip()))
            groups.append((g.group(1), g.group(2), keys))
        text = text[:m.start()] + text[end + 1:]
    if not groups:
        return text

    def ms(frame: float) -> int:
        return round(frame * 1000 / fps)

    lines = ["    ParallelAnimation {", "        running: visible", "        loops: Animation.Infinite"]
    for target, prop, keys in groups:
        lines.append("        SequentialAnimation {")
        lines.append(f"            PropertyAction {{ target: {target}; property: \"{prop}\"; value: {keys[0][1]} }}")
        elapsed = 0
        if keys[0][0] > 0:
            elapsed = ms(keys[0][0])
            lines.append(f"            PauseAnimation {{ duration: {elapsed} }}")
        for prev, key in zip(keys, keys[1:]):
            duration = ms(key[0]) - ms(prev[0])
            easing = f"; {key[2]}" if key[2] else ""
            lines.append(
                f"            NumberAnimation {{ target: {target}; property: \"{prop}\"; to: {key[1]}; duration: {duration}{easing} }}"
            )
            elapsed += duration
        if ms(total_frames) - ms(keys[-1][0]) > 0:
            lines.append(f"            PauseAnimation {{ duration: {ms(total_frames) - ms(keys[-1][0])} }}")
        lines.append("        }")
    lines.append("    }")
    block = "\n".join(lines) + "\n"
    anchor = re.search(r"\n[ \t]*readonly property easingCurve", text)
    if anchor:
        return text[:anchor.start() + 1] + block + text[anchor.start() + 1:]
    last = text.rstrip().rfind("}")
    return text[:last] + block + text[last:]


def convert(src: Path) -> str:
    name = src.stem
    tmp = Path("/tmp") / f"gen-illustration-{name}.qml"
    subprocess.run([LOTTIETOQML, "-c", "-p", str(src), str(tmp)], check=True, capture_output=True)
    text = tmp.read_text(encoding="utf-8")
    tmp.unlink()

    fps = float(re.search(r"property real frameRate: ([\d.]+)", text).group(1))
    total = float(re.search(r"property real endFrame: ([\d.]+)", text).group(1))

    text = re.sub(r"^//.*\n", "", text)
    imports = [l for l in text.splitlines() if l.startswith("import ")]
    for line in imports:
        if line.split()[1] not in KEEP_IMPORTS:
            text = text.replace(line + "\n", "")
    text = text.replace("import QtQuick.Shapes\n", "import QtQuick.Shapes\nimport Drift\n", 1)

    text = remove_block(text, r"component AnimationsInfo\s*:\s*QtObject")
    text = re.sub(r"[ \t]*property AnimationsInfo animations[^\n]*\n", "", text)
    text = re.sub(r"\n[ \t]*Item\s*\{\s*width: 1\s*height: 1\s*ItemSpy[^\n]*\n\s*\}", "", text)
    text = remove_block(text, r"NumberAnimation on frameCounter")
    text = re.sub(r"[ \t]*property real (startFrame|endFrame|frameRate|frameCounter)[^\n]*\n", "", text)
    text = re.sub(r"[ \t]*visible: (_qt_node0\.)?frameCounter[^\n]*\n", "", text)
    text = text.replace("LayerItem {", "Item {")
    text = flatten_transform_groups(text)
    text = timelines_to_animation(text, fps, total)
    text = rebind_colors(text, name)

    leftovers = [w for w in ("LayerItem", "ItemSpy", "Timeline", "TransformGroup", "frameCounter", "PathInterpolated", "lottieqt") if w in text]
    if leftovers:
        raise SystemExit(f"{name}: unsupported construct left in output: {', '.join(leftovers)}")
    header = f"// Generated by scripts/gen-illustrations.py from resources/illustrations/src/{name}.json. Do not edit.\n"
    return header + text


def generate() -> list[Path]:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    written = []
    wanted = set()
    for src in sorted(SRC_DIR.glob("*.json")):
        out = OUT_DIR / f"{pascal(src.stem)}Illustration.qml"
        out.write_text(convert(src), encoding="utf-8")
        wanted.add(out)
        written.append(out)
        print(f"wrote {out.relative_to(ROOT)}")
    for stale in OUT_DIR.glob("*Illustration.qml"):
        if stale not in wanted:
            stale.unlink()
            print(f"removed {stale.relative_to(ROOT)}")
    return written


def resolve_token(doc: dict, ref: str) -> str:
    while isinstance(ref, str) and ref.startswith("$"):
        node = doc
        for part in ref[1:].split("."):
            node = node[part]
        ref = node
    return ref


def dark_illustration_colors() -> dict[str, list[float]]:
    doc = json.loads(DARK_PATH.read_text(encoding="utf-8"))
    colors = {}
    for role in PLACEHOLDERS:
        h = resolve_token(doc, doc["components"]["illustration"][role]).lstrip("#")
        colors[role] = [int(h[i:i + 2], 16) / 255 for i in (0, 2, 4)] + [1]
    return colors


def render() -> None:
    if Path(sys.executable).resolve() != LOTTIE_VENV_PYTHON.resolve():
        if not LOTTIE_VENV_PYTHON.exists():
            raise SystemExit("missing venv: uv venv ~/.venvs/lottie && uv pip install --python ~/.venvs/lottie/bin/python 'lottie[all]'")
        os.execv(str(LOTTIE_VENV_PYTHON), [str(LOTTIE_VENV_PYTHON), __file__, "--render-only"])

    import io

    import cairosvg
    from PIL import Image
    from lottie import objects
    from lottie.exporters.svg import export_svg

    def hex_to_floats(h: str) -> list[float]:
        h = h.lstrip("#")
        return [round(int(h[i:i + 2], 16) / 255, 4) for i in (0, 2, 4)]

    dark = dark_illustration_colors()
    swap = {tuple(hex_to_floats(PLACEHOLDERS[r])): dark[r][:3] for r in PLACEHOLDERS}

    def recolor(node):
        if isinstance(node, dict):
            c = node.get("c")
            if node.get("ty") in ("fl", "st") and isinstance(c, dict) and c.get("a") == 0:
                key = tuple(round(v, 4) for v in c["k"][:3])
                if key not in swap:
                    raise SystemExit(f"stray colour {key}")
                c["k"] = swap[key] + [1]
            for v in node.values():
                recolor(v)
        elif isinstance(node, list):
            for v in node:
                recolor(v)

    RENDER_DIR.mkdir(parents=True, exist_ok=True)
    scale = 4

    def frame_image(an, frame: int) -> Image.Image:
        svg = io.BytesIO()
        export_svg(an, svg, frame, pretty=False)
        png = cairosvg.svg2png(bytestring=svg.getvalue(), scale=scale)
        return Image.open(io.BytesIO(png)).convert("RGB")

    frames = []
    for src in sorted(SRC_DIR.glob("*.json")):
        doc = json.loads(src.read_text(encoding="utf-8"))
        recolor(doc["layers"])
        an = objects.Animation.load(doc)
        total = int(an.out_point)
        steps = [frame_image(an, round(i * an.frame_rate / 25)) for i in range(int(total / an.frame_rate * 25))]
        steps[0].save(RENDER_DIR / f"{src.stem}.gif", save_all=True, append_images=steps[1:], duration=40, loop=0, optimize=True)
        frames.append(frame_image(an, int(total * 0.8)))
        print(f"rendered {src.stem}")

    if frames:
        cols = 4
        w = max(im.size[0] for im in frames)
        h = max(im.size[1] for im in frames)
        pad = 12
        rows = (len(frames) + cols - 1) // cols
        sheet = Image.new("RGB", (cols * (w + pad) + pad, rows * (h + pad) + pad), (64, 64, 64))
        for i, im in enumerate(frames):
            sheet.paste(im, (pad + (i % cols) * (w + pad), pad + (i // cols) * (h + pad)))
        sheet.save(RENDER_DIR / "contact-sheet.png")
        print(f"wrote {RENDER_DIR / 'contact-sheet.png'}")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--render", action="store_true", help="also render GIFs and a contact sheet")
    ap.add_argument("--render-only", action="store_true", help=argparse.SUPPRESS)
    args = ap.parse_args()
    if not args.render_only:
        generate()
    if args.render or args.render_only:
        render()


if __name__ == "__main__":
    main()
