#!/usr/bin/env python3
"""Regenerate the static UI font faces in resources/fonts/.

The UI chrome used to ship the *variable* Inter. On Windows Qt has no way to add a font to
the DirectWrite system collection, so it registers application fonts through GDI and
converts the resulting HFONT to a DirectWrite face — and GDI cannot express a variable
instance. For some (weight, size) pairs the face Qt rasterised with was not the face it had
looked the glyph indices up in, and labels came out with neighbouring glyphs and stray
diacritics (issues #115, #162). Static instances remove that path entirely.

The faces are renamed to the family "Inter UI" so the Essential Fonts addon's "Inter" can
never shadow them, whichever registers last. Inter is OFL with no Reserved Font Name, so
renaming a derivative is permitted; resources/licenses/LICENSE-inter.txt still applies.

Usage:
    scripts/gen-ui-font.py <Inter[Variable].ttf> <Inter-Italic.ttf> [-o resources/fonts]

Both inputs come from the Inter package in the drift-addons repo (fonts/inter/). The
variable Inter has no italic axis, so the italic face is renamed from a static source
instead of being instanced.
"""

import argparse
import pathlib
import sys

from fontTools.ttLib import TTFont
from fontTools.varLib import instancer

FAMILY = "Inter UI"
PS_FAMILY = "InterUI"

# (weight, typographic style, name ID 1, name ID 2) — the ID1/ID2 split is the RIBBI
# convention: only Regular/Bold/Italic/Bold Italic may live in ID2, so Medium and SemiBold
# get their own ID1 family and are reunited under FAMILY by ID16/ID17.
FACES = [
    (400, "Regular", FAMILY, "Regular"),
    (500, "Medium", f"{FAMILY} Medium", "Regular"),
    (600, "SemiBold", f"{FAMILY} SemiBold", "Regular"),
    (700, "Bold", FAMILY, "Bold"),
]

MANAGED_IDS = (1, 2, 3, 4, 6, 16, 17, 21, 22, 25)

FS_ITALIC = 1 << 0
FS_BOLD = 1 << 5
FS_REGULAR = 1 << 6

MAC_BOLD = 1 << 0
MAC_ITALIC = 1 << 1


def rename(font, style, id1, id2):
    name = font["name"]
    for nid in MANAGED_IDS:
        name.removeNames(nameID=nid)

    ps_name = f"{PS_FAMILY}-{style.replace(' ', '')}"
    version = font["head"].fontRevision
    values = {
        1: id1,
        2: id2,
        3: f"{version:.3f};DRFT;{ps_name}",
        4: f"{FAMILY} {style}",
        6: ps_name,
        16: FAMILY,
        17: style,
    }
    for nid, value in values.items():
        name.setName(value, nid, 3, 1, 0x409)
        name.setName(value, nid, 1, 0, 0)


def set_style_bits(font, weight, italic, bold):
    os2 = font["OS/2"]
    os2.usWeightClass = weight
    os2.fsSelection &= ~(FS_ITALIC | FS_BOLD | FS_REGULAR)
    if italic:
        os2.fsSelection |= FS_ITALIC
    elif bold:
        os2.fsSelection |= FS_BOLD
    else:
        os2.fsSelection |= FS_REGULAR

    head = font["head"]
    head.macStyle &= ~(MAC_BOLD | MAC_ITALIC)
    if italic:
        head.macStyle |= MAC_ITALIC
    if bold:
        head.macStyle |= MAC_BOLD


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("variable", type=pathlib.Path, help="variable Inter .ttf")
    parser.add_argument("italic", type=pathlib.Path, help="static Inter Italic .ttf")
    parser.add_argument("-o", "--out-dir", type=pathlib.Path,
                        default=pathlib.Path(__file__).resolve().parent.parent / "resources/fonts")
    args = parser.parse_args()

    source = TTFont(args.variable)
    if "fvar" not in source:
        sys.exit(f"{args.variable} is not a variable font")
    axes = {axis.axisTag for axis in source["fvar"].axes}
    if not {"wght", "opsz"} <= axes:
        sys.exit(f"{args.variable} lacks the wght/opsz axes this script pins ({sorted(axes)})")

    args.out_dir.mkdir(parents=True, exist_ok=True)

    for weight, style, id1, id2 in FACES:
        # opsz 14 is the axis default and the right optical size for 11-15px UI text.
        font = instancer.instantiateVariableFont(TTFont(args.variable),
                                                 {"wght": weight, "opsz": 14},
                                                 updateFontNames=True)
        rename(font, style, id1, id2)
        set_style_bits(font, weight, italic=False, bold=weight >= 700)
        out = args.out_dir / f"{PS_FAMILY}-{style}.ttf"
        font.save(out)
        print(f"{out.name}  wght={weight}  {out.stat().st_size // 1024} KB")

    font = TTFont(args.italic)
    if "fvar" in font:
        sys.exit(f"{args.italic} is variable; a static italic is required")
    rename(font, "Italic", FAMILY, "Italic")
    set_style_bits(font, 400, italic=True, bold=False)
    out = args.out_dir / f"{PS_FAMILY}-Italic.ttf"
    font.save(out)
    print(f"{out.name}  wght=400 italic  {out.stat().st_size // 1024} KB")


if __name__ == "__main__":
    main()
