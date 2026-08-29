#!/usr/bin/env python3
"""Remap eos sfm_reference.obj into Drift head space (sfm_face.bin).
"""

from __future__ import annotations

import struct
import time
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = ROOT / "effects" / "face_mesh_3d"
OUT_BIN = OUT_DIR / "sfm_face.bin"

EOS_SHARE = "https://raw.githubusercontent.com/patrikhuber/eos/master/share/"
SOURCES = ("sfm_reference.obj", "ibug_to_sfm.txt")

# ibug 1-based → MediaPipe 468 index. Skip ibug 1–8 and 10–17 (silhouette): eos
# maps those to vertex ids > 845, which the public reference does not have.
# 61 and 65 are omitted — they are not on the SFM.
IBUG_TO_MP = {
    9: 152,  # chin
    18: 70,
    19: 63,
    20: 105,
    21: 66,
    22: 107,  # right brow (subject right = image left)
    23: 336,
    24: 296,
    25: 334,
    26: 293,
    27: 300,  # left brow
    28: 168,
    29: 197,
    30: 5,
    31: 4,
    32: 98,
    33: 2,
    34: 326,
    35: 327,
    36: 294,  # nose (32–36 approx)
    37: 33,
    38: 160,
    39: 158,
    40: 133,
    41: 153,
    42: 144,  # right eye
    43: 362,
    44: 385,
    45: 387,
    46: 263,
    47: 373,
    48: 380,  # left eye
    49: 61,
    50: 40,
    51: 37,
    52: 0,
    53: 267,
    54: 269,
    55: 291,
    56: 321,
    57: 405,
    58: 314,
    59: 17,
    60: 84,  # outer mouth
    62: 82,
    63: 13,
    64: 312,
    66: 317,
    67: 14,
    68: 87,  # inner mouth
}

# Known SFM vertex ids used for axis checks (ibug 37 / 46 / 40 / 43).
SFM_RIGHT_EYE_OUTER = 177
SFM_LEFT_EYE_OUTER = 610
SFM_RIGHT_EYE_INNER = 181
SFM_LEFT_EYE_INNER = 614


def download(name: str, dest: Path) -> None:
    url = EOS_SHARE + name
    last = None
    for attempt in range(6):
        try:
            req = urllib.request.Request(url, headers={"User-Agent": "Drift-sfm-convert/1"})
            with urllib.request.urlopen(req, timeout=60) as resp:
                dest.write_bytes(resp.read())
            if dest.stat().st_size > 0:
                return
            last = RuntimeError(f"empty download: {name}")
        except (urllib.error.URLError, TimeoutError, OSError, RuntimeError) as exc:
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
                    raw = tok.split("/")[0]
                    idx = int(raw)
                    if idx < 0:
                        idx = len(verts) + idx
                    else:
                        idx -= 1
                    ids.append(idx)
                if len(ids) < 3:
                    continue
                # Fan-triangulate quads / n-gons.
                for i in range(1, len(ids) - 1):
                    faces.append([ids[0], ids[i], ids[i + 1]])
    if not verts or not faces:
        raise SystemExit("empty OBJ")
    return verts, faces


def parse_ibug_to_sfm(path: Path) -> dict[int, int]:
    """TOML [landmark_mappings] ibug = vertex (0-based SFM). Stdlib only."""
    mapping = {}
    in_section = False
    with path.open() as f:
        for raw in f:
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            if line.startswith("["):
                in_section = line == "[landmark_mappings]"
                continue
            if not in_section or "=" not in line:
                continue
            lhs, rhs = line.split("=", 1)
            mapping[int(lhs.strip())] = int(rhs.strip())
    if not mapping:
        raise SystemExit("no [landmark_mappings] in ibug_to_sfm.txt")
    return mapping


def is_silhouette_ibug(ibug: int) -> bool:
    return ibug <= 8 or 10 <= ibug <= 17


def interior_sfm_ids(ibug_to_sfm: dict[int, int], nverts: int) -> list[int]:
    ids = []
    seen = set()
    for ibug, vid in sorted(ibug_to_sfm.items()):
        if is_silhouette_ibug(ibug) or vid < 0 or vid >= nverts:
            continue
        if vid not in seen:
            seen.add(vid)
            ids.append(vid)
    return ids


def aabb_axis(verts, ids, axis: int):
    vals = [verts[i][axis] for i in ids]
    return min(vals), max(vals)


def scale_uniform(verts, s: float) -> None:
    for v in verts:
        v[0] *= s
        v[1] *= s
        v[2] *= s


def remap_axes(verts, ibug_to_sfm: dict[int, int]) -> int:
    """Y-up SFM → Drift head space. Returns number of axis negations (winding)."""
    n = len(verts)
    flips = 0

    def vid(ibug: int, fallback: int) -> int:
        v = ibug_to_sfm.get(ibug, fallback)
        return v if 0 <= v < n else fallback

    right_outer = vid(37, SFM_RIGHT_EYE_OUTER)
    left_outer = vid(46, SFM_LEFT_EYE_OUTER)
    nose = vid(31, 114)
    eye_l = vid(40, SFM_RIGHT_EYE_INNER)
    eye_r = vid(43, SFM_LEFT_EYE_INNER)

    # Subject-right outer eye must land image-left (x < 0).
    if verts[right_outer][0] >= 0 or verts[left_outer][0] <= 0:
        for v in verts:
            v[0] = -v[0]
        flips += 1

    # Nose in front of the eyes means +Z toward the viewer. Raw SFM often has the
    # face looking −Z (eyes more +Z than the nose, or simply both negative with
    # the nose more negative). Compare nose.z to the inner-eye midpoint.
    eye_z = 0.5 * (verts[eye_l][2] + verts[eye_r][2])
    if verts[nose][2] < eye_z:
        for v in verts:
            v[2] = -v[2]
        flips += 1

    if verts[right_outer][0] >= 0 or verts[left_outer][0] <= 0:
        raise SystemExit(
            f"axis remap failed: v{right_outer}.x={verts[right_outer][0]} "
            f"v{left_outer}.x={verts[left_outer][0]}"
        )
    return flips


def pivot_to_eyes(verts, ibug_to_sfm: dict[int, int]) -> None:
    n = len(verts)
    a = ibug_to_sfm.get(40, SFM_RIGHT_EYE_INNER)
    b = ibug_to_sfm.get(43, SFM_LEFT_EYE_INNER)
    if a >= n or b >= n:
        raise SystemExit("inner-eye vertices missing")
    ox = 0.5 * (verts[a][0] + verts[b][0])
    oy = 0.5 * (verts[a][1] + verts[b][1])
    oz = 0.5 * (verts[a][2] + verts[b][2])
    for v in verts:
        v[0] -= ox
        v[1] -= oy
        v[2] -= oz


def triangle_front_z(verts, tri) -> float:
    a, b, c = verts[tri[0]], verts[tri[1]], verts[tri[2]]
    e1 = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
    e2 = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
    return e1[0] * e2[1] - e1[1] * e2[0]


def ensure_ccw(verts, tris, axis_flips: int) -> None:
    # Odd number of axis negations reverses winding.
    if axis_flips % 2 == 1:
        for t in tris:
            t[1], t[2] = t[2], t[1]
    # Front faces (centroid +Z) should have a +Z normal under the right-hand rule.
    pos = neg = 0
    for t in tris:
        zmid = (verts[t[0]][2] + verts[t[1]][2] + verts[t[2]][2]) / 3.0
        if zmid <= 0:
            continue
        nz = triangle_front_z(verts, t)
        if nz > 0:
            pos += 1
        else:
            neg += 1
    if neg > pos:
        for t in tris:
            t[1], t[2] = t[2], t[1]


def write_bin(path: Path, verts, tris, handles) -> None:
    nv = len(verts)
    indices = [i for t in tris for i in t]
    ni = len(indices)
    nh = len(handles)
    blob = struct.pack("<4sIIII", b"DRFM", 1, nv, ni, nh)
    blob += struct.pack("<" + "f" * (nv * 3), *[c for v in verts for c in v])
    blob += struct.pack("<" + "I" * ni, *indices) if ni else b""
    for mp, vi in handles:
        blob += struct.pack("<HH", mp, vi)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(blob)


def build_handles(ibug_to_sfm: dict[int, int], nverts: int) -> list[tuple[int, int]]:
    out = []
    for ibug, mp in IBUG_TO_MP.items():
        if is_silhouette_ibug(ibug) or ibug not in ibug_to_sfm:
            continue
        vid = ibug_to_sfm[ibug]
        if 0 <= vid < nverts:
            out.append((mp, vid))
    return out


def add_forehead_handle(verts, handles: list[tuple[int, int]]) -> None:
    """Pin the rest-mesh hairline to MediaPipe 10 (top of forehead).

    ibug has no hairline on this 845-vert reference. Without this, the SFM
    forehead (well above the brows) only interpolates from brow handles and
    overshoots into the hair.
    """
    used_mp = {mp for mp, _ in handles}
    if 10 in used_mp:
        return
    cands = [i for i, v in enumerate(verts) if abs(v[0]) < 0.2]
    if not cands:
        return
    top = max(cands, key=lambda i: verts[i][1])
    handles.append((10, top))


def add_ear_handles(verts, handles: list[tuple[int, int]]) -> None:
    """Pin ear-front verts to MediaPipe oval 234 / 454.

    ibug 1–8 / 10–17 are not on the public 845-vert reference. Without these, the
    wrap toward the ears only interpolates from the outer eye corners.

    Restrict to z ≥ −0.3 so the handle is the tragus/cheek, not the rear rim of
    the face mesh (most-lateral verts sit around z ≈ −0.56).
    """
    used_mp = {mp for mp, _ in handles}
    cands = [i for i, v in enumerate(verts) if -0.5 <= v[1] <= 0.2 and v[2] >= -0.3]
    if len(cands) < 2:
        return
    left = min(cands, key=lambda i: verts[i][0])  # image-left = subject-right
    right = max(cands, key=lambda i: verts[i][0])
    for mp, vid in ((234, left), (454, right)):
        if mp not in used_mp:
            handles.append((mp, vid))


def convert(obj_path: Path, map_path: Path):
    verts, faces = parse_obj(obj_path)
    ibug_to_sfm = parse_ibug_to_sfm(map_path)
    interior = interior_sfm_ids(ibug_to_sfm, len(verts))
    if not interior:
        raise SystemExit("no interior landmark vertices on this mesh")

    flips = remap_axes(verts, ibug_to_sfm)
    pivot_to_eyes(verts, ibug_to_sfm)

    xmin, xmax = aabb_axis(verts, interior, 0)
    extent = xmax - xmin
    if extent < 1e-8:
        raise SystemExit("degenerate interior AABB")
    # Interior ibug span = one head-width; ears extend past ±0.5.
    scale_uniform(verts, 1.0 / extent)

    handles = build_handles(ibug_to_sfm, len(verts))
    if len(handles) < 20:
        raise SystemExit(f"too few warp handles: {len(handles)}")
    add_ear_handles(verts, handles)
    add_forehead_handle(verts, handles)

    ensure_ccw(verts, faces, flips)
    write_bin(OUT_BIN, verts, faces, handles)

    xs = [v[0] for v in verts]
    print(f"verts: {len(verts)}")
    print(f"tris: {len(faces)}")
    print(f"handles: {len(handles)}")
    print(f"indices: {len(faces) * 3}")
    print(f"x range: {min(xs):.3f} .. {max(xs):.3f}")
    print(f"wrote: {OUT_BIN}")


def main() -> None:
    import tempfile

    with tempfile.TemporaryDirectory(prefix="sfm_face_") as tmp:
        tmp_path = Path(tmp)
        for name in SOURCES:
            print(f"download {name}")
            download(name, tmp_path / name)
        convert(tmp_path / SOURCES[0], tmp_path / SOURCES[1])


if __name__ == "__main__":
    main()
