#!/usr/bin/env python3
"""Write resources/search-index.json, the global search index the app loads from qrc.

Entries are the SettingsRow, SearchAnchor and CollapsibleSectionHeader blocks with a searchId
under src/qml. Strings stay as source text; the app looks them up with
QCoreApplication::translate(context, source). Run with --check in CI or before committing.
It exits 1 when the file on disk is stale or an annotation is invalid.
"""

from __future__ import annotations

import json
import re
import sys
from dataclasses import dataclass
from itertools import accumulate
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
QML_ROOT = ROOT / "src" / "qml"
PROPERTIES_DIR = QML_ROOT / "components" / "properties"
SETTINGS_PANE_PATH = QML_ROOT / "components" / "shell" / "SettingsPane.qml"
PROPERTIES_PANEL_PATH = QML_ROOT / "PropertiesPanel.qml"
OUT_PATH = ROOT / "resources" / "search-index.json"

FILE_TARGETS = {
    "SettingsGeneralPage.qml": "settings:general",
    "SettingsAppearancePage.qml": "settings:appearance",
    "SettingsWorkspacesPage.qml": "settings:workspaces",
    "SettingsTimelinePage.qml": "settings:timeline",
    "SettingsToolbarPage.qml": "settings:toolbar",
    "SettingsKeymapPage.qml": "settings:keymap",
    "SettingsPreviewPage.qml": "settings:preview",
    "SettingsPerformancePage.qml": "settings:performance",
    "SettingsAudioPage.qml": "settings:audio",
    "SettingsAiPage.qml": "settings:ai",
    "SettingsNetworkPage.qml": "settings:network",
    "SettingsMarketplacePage.qml": "settings:marketplace",
    "SettingsAboutPage.qml": "settings:about",
    "ColorPanel.qml": "panel:color",
    "LooksPanel.qml": "panel:looks",
    "ScopesPanel.qml": "panel:scopes",
    "KeyframesPanel.qml": "panel:keyframes",
    "MaskingPanel.qml": "panel:masking",
    "TrackingPanel.qml": "panel:tracking",
    "SubtitlesPanel.qml": "panel:subtitles",
    "SubtitleLanePanel.qml": "panel:subtitleLane",
    "SubtitleTextPanel.qml": "panel:subtitleText",
    "SubtitleTimingPanel.qml": "panel:subtitleTiming",
    "SourceLabBinPanel.qml": "panel:sourceLab.bin",
    "SourceLabViewerPanel.qml": "panel:sourceLab.viewer",
    "SourceLabTrimPanel.qml": "panel:sourceLab.trim",
    "SourceLabRangesPanel.qml": "panel:sourceLab.ranges",
    "SourceLabProcessPanel.qml": "panel:sourceLab.process",
    "SourceLabOutputPanel.qml": "panel:sourceLab.output",
    "SourceLabCropSection.qml": "panel:sourceLab.process",
    "SourceLabSpeedSection.qml": "panel:sourceLab.process",
    "SourceLabAudioSection.qml": "panel:sourceLab.process",
    "SourceLabStabilizeSection.qml": "panel:sourceLab.process",
    "SourceLabEnhanceSection.qml": "panel:sourceLab.process",
}

INSPECTOR_PREFIXES = (
    ("GeneralInspector", "general"),
    ("Transform", "transform"),
    ("StabilizeInspector", "stabilize"),
    ("AnimationInspector", "animation"),
    ("AudioEffects", "audioEffects"),
    ("AudioInspector", "audio"),
    ("SpeedFadeInspector", "speed"),
    ("BlendingInspector", "blending"),
    ("MasksInspector", "masks"),
    ("Effects", "effects"),
    ("TransitionInspector", "transition"),
    ("TextInspector", "text"),
    ("ShapeInspector", "shape"),
    ("CameraInspector", "camera"),
    ("PdfInspector", "pdf"),
    ("Model3DInspector", "model3d"),
    ("VectorInspector", "vector"),
)

PROPERTIES_PREFIX = "panel:properties."
SIMPLE_ESCAPES = {"n": "\n", "t": "\t", "r": "\r", "b": "\b", "f": "\f", "v": "\v", "0": "\0"}
CLOSERS = {"{": "}", "[": "]"}

TOKEN = re.compile(r"//[^\n]*|/\*.*?\*/|\"(?:[^\"\\\n]|\\.)*\"|'(?:[^'\\\n]|\\.)*'|`(?:[^`\\]|\\.)*`", re.DOTALL)
BLOCK = re.compile(r"\b(SettingsRow|SearchAnchor|CollapsibleSectionHeader)\s*\{")
KEY = re.compile(r"([A-Za-z_]\w*)\s*:")
WS = re.compile(r"\s*")
STRING = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
QSTR = re.compile(r'qsTr\(\s*"((?:[^"\\\n]|\\.)*)"\s*\)')
ESCAPE = re.compile(r"\\(u[0-9a-fA-F]{4}|x[0-9a-fA-F]{2}|.)", re.DOTALL)


class GenError(Exception):
    pass


@dataclass
class QmlSource:
    path: Path
    text: str
    masked: str
    prefix: list[int]

    def where(self, pos: int) -> str:
        line = self.text.count("\n", 0, pos) + 1
        return f"{self.path.relative_to(ROOT).as_posix()}:{line}"

    def fail(self, pos: int, message: str) -> GenError:
        return GenError(f"{self.where(pos)}: {message}")


def blank(chunk: str) -> str:
    return "".join("\n" if char == "\n" else " " for char in chunk)


def mask(text: str) -> str:
    def hide(match: re.Match) -> str:
        token = match.group()
        if token[0] in "\"'`":
            return token[0] + blank(token[1:-1]) + token[-1]
        return blank(token)

    return TOKEN.sub(hide, text)


def load(path: Path) -> QmlSource:
    text = path.read_text(encoding="utf-8")
    masked = mask(text)
    steps = (1 if char == "{" else -1 if char == "}" else 0 for char in masked)
    return QmlSource(path, text, masked, list(accumulate(steps, initial=0)))


def closing(qml: QmlSource, open_pos: int) -> int:
    opener = qml.masked[open_pos]
    depth = 0
    for index in range(open_pos, len(qml.masked)):
        if qml.masked[index] == opener:
            depth += 1
        elif qml.masked[index] == CLOSERS[opener]:
            depth -= 1
            if depth == 0:
                return index
    raise qml.fail(open_pos, f"unclosed {opener}")


def starts_property(masked: str, pos: int, floor: int) -> bool:
    before = masked[floor:pos]
    if before.split("\n")[-1].strip() == "":
        return True
    return before.rstrip()[-1:] in ("{", "}", ";", ",")


def object_keys(qml: QmlSource, open_pos: int, close_pos: int) -> dict[str, int]:
    keys: dict[str, int] = {}
    depth = qml.prefix[open_pos] + 1
    for match in KEY.finditer(qml.masked, open_pos + 1, close_pos):
        if qml.prefix[match.start()] != depth:
            continue
        if not starts_property(qml.masked, match.start(), open_pos):
            continue
        keys[match.group(1)] = match.end()
    return keys


def array_items(qml: QmlSource, name: str) -> list[tuple[int, dict[str, int]]]:
    match = re.search(rf"\b{name}\s*:\s*\[", qml.masked)
    if match is None:
        raise GenError(f"{qml.path.relative_to(ROOT).as_posix()}: no `{name}: [...]` array")
    open_pos = match.end() - 1
    end = closing(qml, open_pos)
    items = []
    for index in range(open_pos + 1, end):
        if qml.masked[index] == "{" and qml.prefix[index] == qml.prefix[open_pos]:
            items.append((index, object_keys(qml, index, closing(qml, index))))
    return items


def annotated_blocks(qml: QmlSource):
    for match in BLOCK.finditer(qml.masked):
        open_pos = match.end() - 1
        yield match.group(1), match.start(), object_keys(qml, open_pos, closing(qml, open_pos))


def decode(body: str) -> str:
    def unescape(match: re.Match) -> str:
        code = match.group(1)
        if code[0] in "ux" and len(code) > 1:
            return chr(int(code[1:], 16))
        return SIMPLE_ESCAPES.get(code, code)

    return ESCAPE.sub(unescape, body)


def ends_value(masked: str, end: int) -> bool:
    return masked[end:].lstrip(" \t")[:1] in ("", "\n", "\r", ";", ",", "}")


def literal_at(qml: QmlSource, pos: int, pattern: re.Pattern) -> str | None:
    match = pattern.match(qml.text, WS.match(qml.text, pos).end())
    if match is None or not ends_value(qml.masked, match.end()):
        return None
    return decode(match.group(1))


def string_at(qml: QmlSource, pos: int) -> str:
    value = literal_at(qml, pos, STRING)
    if value is None:
        raise qml.fail(pos, 'expected a "string" literal')
    return value


def qstr_at(qml: QmlSource, pos: int) -> str:
    value = literal_at(qml, pos, QSTR)
    if value is None:
        raise qml.fail(pos, 'expected a plain qsTr("...") literal')
    return value


def string_list_at(qml: QmlSource, pos: int) -> list[str]:
    start = WS.match(qml.text, pos).end()
    if qml.masked[start:start + 1] != "[":
        raise qml.fail(pos, "expected a [...] list")
    end = closing(qml, start)
    return [decode(match.group(1)) for match in STRING.finditer(qml.text, start, end)]


def need(qml: QmlSource, keys: dict[str, int], name: str, at: int) -> int:
    if name not in keys:
        raise qml.fail(at, f"missing {name}")
    return keys[name]


def page_entry(pane: QmlSource, at: int, keys: dict[str, int]) -> dict:
    return {
        "id": string_at(pane, need(pane, keys, "id", at)),
        "context": pane.path.stem,
        "source": qstr_at(pane, need(pane, keys, "title", at)),
        "keywords": string_at(pane, keys["keywords"]) if "keywords" in keys else "",
    }


def tab_entry(panel: QmlSource, at: int, keys: dict[str, int]) -> dict:
    return {
        "id": string_at(panel, need(panel, keys, "id", at)),
        "context": panel.path.stem,
        "source": qstr_at(panel, need(panel, keys, "label", at)),
        "needs": string_list_at(panel, need(panel, keys, "needs", at)),
    }


def base_target(qml: QmlSource, at: int) -> str:
    name = qml.path.name
    if name in FILE_TARGETS:
        return FILE_TARGETS[name]
    if qml.path.parent == PROPERTIES_DIR:
        for prefix, tab in INSPECTOR_PREFIXES:
            if name.startswith(prefix):
                return f"{PROPERTIES_PREFIX}{tab}"
    raise qml.fail(at, "no search target for this file: add it to FILE_TARGETS or set searchTarget")


def block_entry(
    qml: QmlSource,
    kind: str,
    at: int,
    keys: dict[str, int],
    page_ids: set[str],
    tab_needs: dict[str, list[str]],
    seen: dict[str, str],
) -> dict:
    search_id = string_at(qml, need(qml, keys, "searchId", at))
    if search_id in seen:
        raise qml.fail(at, f"duplicate searchId {search_id!r}, also at {seen[search_id]}")
    seen[search_id] = qml.where(at)

    title = qstr_at(qml, need(qml, keys, "title", at))
    keywords = string_at(qml, keys["keywords"]) if "keywords" in keys else ""
    if "searchTarget" in keys:
        target = string_at(qml, keys["searchTarget"])
    else:
        target = f"{base_target(qml, at)}#{search_id}"

    base = target.split("#", 1)[0]
    needs: list[str] = []
    if base.startswith("settings:") and base.removeprefix("settings:") not in page_ids:
        raise qml.fail(at, f"unknown settings page in {base!r}")
    if base.startswith(PROPERTIES_PREFIX):
        tab = base.removeprefix(PROPERTIES_PREFIX)
        if tab not in tab_needs:
            raise qml.fail(at, f"unknown properties tab {tab!r}")
        needs = list(tab_needs[tab])

    entry = {
        "id": search_id,
        "target": target,
        "context": qml.path.stem,
        "source": title,
        "keywords": keywords,
        "needs": needs,
    }
    if kind == "SettingsRow" and "description" in keys:
        description = literal_at(qml, keys["description"], QSTR)
        if description is not None:
            entry["descriptionSource"] = description
    return entry


def build() -> dict:
    pane = load(SETTINGS_PANE_PATH)
    panel = load(PROPERTIES_PANEL_PATH)
    pages = [page_entry(pane, at, keys) for at, keys in array_items(pane, "pages")]
    tabs = [tab_entry(panel, at, keys) for at, keys in array_items(panel, "tabs")]
    page_ids = {page["id"] for page in pages}
    tab_needs = {tab["id"]: tab["needs"] for tab in tabs}

    entries = []
    seen: dict[str, str] = {}
    for path in sorted(QML_ROOT.rglob("*.qml"), key=lambda found: found.as_posix()):
        qml = load(path)
        for kind, at, keys in annotated_blocks(qml):
            if "searchId" in keys:
                entries.append(block_entry(qml, kind, at, keys, page_ids, tab_needs, seen))
    return {"entries": entries, "settingsPages": pages, "propertiesTabs": tabs}


def main() -> int:
    try:
        data = build()
    except GenError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    text = json.dumps(data, indent=2, ensure_ascii=False) + "\n"
    if "--check" in sys.argv[1:]:
        if not OUT_PATH.exists() or OUT_PATH.read_text(encoding="utf-8") != text:
            print(f"stale: {OUT_PATH.relative_to(ROOT)} (run scripts/gen-search-index.py)", file=sys.stderr)
            return 1
        return 0
    OUT_PATH.write_text(text, encoding="utf-8")
    print(
        f"wrote {OUT_PATH.relative_to(ROOT)}: {len(data['entries'])} entries, "
        f"{len(data['settingsPages'])} settings pages, {len(data['propertiesTabs'])} properties tabs"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
