#!/usr/bin/env python3
"""Bake MediaPipe's canonical face model into Drift head space (mediapipe_face.bin).

The Face Swap effect draws MediaPipe's own 468-vertex tessellation with the tracked
mesh supplying vertex positions, so only the triangle list is read at runtime. The
rest positions are written anyway: the format carries them, they make a bare mesh
renderable for debugging, and putting them in the same head space as sfm_face.bin
means the two bins are interchangeable.
"""

from __future__ import annotations

import struct
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = ROOT / "effects" / "face_swap"
OUT_BIN = OUT_DIR / "mediapipe_face.bin"

SOURCE_URL = (
    "https://raw.githubusercontent.com/google-ai-edge/mediapipe/master/"
    "mediapipe/modules/face_geometry/data/canonical_face_model.obj"
)

MESH_POINTS = 468

# Inner eye corners: the head-space origin, matching sfm_face.bin's ibug 40 / 43 pivot.
EYE_INNER_L = 133
EYE_INNER_R = 362

# The MediaPipe points sfm_face.bin's IBUG_TO_MP maps ibug 9-68 onto. Their x-span is
# what "one head width" means in head space, so scaling to it puts both meshes on one
# ruler. Ear and hairline points are deliberately absent — the span is the interior
# face, and the oval reaches past +/-0.5.
INTERIOR_MP = (
    0, 2, 4, 5, 13, 14, 17, 33, 37, 40, 61, 63, 66, 70, 82, 84, 87, 98, 105, 107,
    133, 144, 152, 153, 158, 160, 168, 197, 263, 267, 269, 291, 293, 294, 296, 300,
    312, 314, 317, 321, 326, 327, 334, 336, 362, 373, 380, 385, 387, 405,
)


def download(url: str, dest: Path) -> None:
    if dest.exists():
        return
    dest.parent.mkdir(parents=True, exist_ok=True)
    last = None
    for attempt in range(3):
        try:
            with urllib.request.urlopen(url, timeout=30) as response:
                dest.write_bytes(response.read())
            return
        except (urllib.error.URLError, TimeoutError) as exc:
            last = exc
            time.sleep(1.0 + attempt)
    raise SystemExit(f"failed to download {url}: {last}")


def parse_obj(path: Path):
    verts = []
    faces = []
    with path.open() as f:
        for line in f:
            if line.startswith("v "):
                parts = line.split()
                verts.append([float(parts[1]), float(parts[2]), float(parts[3])])
            elif line.startswith("f "):
                ids = []
                for tok in line.split()[1:]:
                    idx = int(tok.split("/")[0])
                    ids.append(len(verts) + idx if idx < 0 else idx - 1)
                if len(ids) < 3:
                    continue
                for i in range(1, len(ids) - 1):
                    faces.append([ids[0], ids[i], ids[i + 1]])
    return verts, faces


def check_axes(verts) -> None:
    """The canonical model already uses Drift head space axes. Assert it rather than
    remap, so a changed upstream export fails loudly instead of rendering mirrored."""
    if verts[33][0] >= 0 or verts[263][0] <= 0:
        raise SystemExit("x is not image-right: eye outer corners 33/263 are wrong-signed")
    if verts[10][1] <= verts[152][1]:
        raise SystemExit("y is not toward the forehead: vertex 10 is below the chin")
    if verts[1][2] <= verts[234][2]:
        raise SystemExit("z is not toward the viewer: the nose tip is behind the ear")


def normalize(verts) -> None:
    ox = 0.5 * (verts[EYE_INNER_L][0] + verts[EYE_INNER_R][0])
    oy = 0.5 * (verts[EYE_INNER_L][1] + verts[EYE_INNER_R][1])
    oz = 0.5 * (verts[EYE_INNER_L][2] + verts[EYE_INNER_R][2])
    for v in verts:
        v[0] -= ox
        v[1] -= oy
        v[2] -= oz

    xs = [verts[i][0] for i in INTERIOR_MP]
    span = max(xs) - min(xs)
    if span <= 0:
        raise SystemExit("interior x-span is zero")
    s = 1.0 / span
    for v in verts:
        v[0] *= s
        v[1] *= s
        v[2] *= s


def ensure_ccw(verts, tris) -> None:
    """Front-facing triangles (centroid toward the viewer) get a +Z normal under the
    right-hand rule. The swap draws with cull disabled, but the format promises CCW."""
    pos = neg = 0
    for t in tris:
        if (verts[t[0]][2] + verts[t[1]][2] + verts[t[2]][2]) / 3.0 <= 0:
            continue
        a, b, c = verts[t[0]], verts[t[1]], verts[t[2]]
        nz = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
        pos, neg = (pos + 1, neg) if nz > 0 else (pos, neg + 1)
    if neg > pos:
        for t in tris:
            t[1], t[2] = t[2], t[1]


def write_bin(path: Path, verts, tris) -> None:
    indices = [i for t in tris for i in t]
    blob = struct.pack("<4sIIII", b"DRFM", 1, len(verts), len(indices), 0)
    blob += struct.pack("<" + "f" * (len(verts) * 3), *[c for v in verts for c in v])
    blob += struct.pack("<" + "I" * len(indices), *indices)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(blob)


def main() -> None:
    # Cached outside the package: everything under effects/ is copied into the build
    # tree wholesale, and the source OBJ is not part of the shipped effect.
    cache = Path(tempfile.gettempdir()) / "drift-canonical_face_model.obj"
    download(SOURCE_URL, cache)
    verts, tris = parse_obj(cache)

    if len(verts) != MESH_POINTS:
        raise SystemExit(f"expected {MESH_POINTS} vertices, got {len(verts)}")
    for t in tris:
        if len(set(t)) != 3:
            raise SystemExit(f"degenerate triangle {t}")
        for i in t:
            if not 0 <= i < MESH_POINTS:
                raise SystemExit(f"index {i} out of range")
    if len({tuple(sorted(t)) for t in tris}) != len(tris):
        raise SystemExit("duplicate triangles")

    check_axes(verts)
    normalize(verts)
    ensure_ccw(verts, tris)
    write_bin(OUT_BIN, verts, tris)

    print(f"{OUT_BIN.relative_to(ROOT)}: {len(verts)} verts, {len(tris)} tris, "
          f"{OUT_BIN.stat().st_size} bytes")


if __name__ == "__main__":
    main()
