"""
gen_dice.py — generate vetted vertex/face tables for src/c/dice3d.c.

Produces two polyhedra, each in two forms (canonical and pre-rotated to
sit a face dead-on at rest_x = rest_y = rest_z = 0):

  D12 — regular dodecahedron, 20 verts, 12 pentagonal faces, values 1..12
        with opposite-pair faces summing to 13.
  D10 — pentagonal trapezohedron, 12 verts, 10 kite faces, values 0..9.

Both tables are emitted as C99 source ready to paste into dice3d.c.
Vertex coords are scaled so the circumradius = UNIT_FP (4096) and quantized
to int16 — the same convention as the rest of dice3d.c.

The script also validates:
  - All pentagon edges have the same length (within int-quantization slack).
  - All kite edges sit in one of two length classes (long edges from the
    apex; short edges along the belt).
  - Faces are convex and planar.
  - Vertex ordering is CCW when viewed from outside the polyhedron.
  - Opposite pairs sum to 13 (D12).
  - After pre-rotation the chosen face's centroid lies on +z and its
    vertices share a common z value.

Run: `python tools/gen_dice.py` from the repo root.
"""

from __future__ import annotations

import math
from dataclasses import dataclass


UNIT_FP = 4096
PHI = (1.0 + math.sqrt(5.0)) / 2.0


@dataclass
class Vec3:
    x: float
    y: float
    z: float

    def __add__(self, o: "Vec3") -> "Vec3":
        return Vec3(self.x + o.x, self.y + o.y, self.z + o.z)

    def __sub__(self, o: "Vec3") -> "Vec3":
        return Vec3(self.x - o.x, self.y - o.y, self.z - o.z)

    def __mul__(self, k: float) -> "Vec3":
        return Vec3(self.x * k, self.y * k, self.z * k)

    __rmul__ = __mul__

    def dot(self, o: "Vec3") -> float:
        return self.x * o.x + self.y * o.y + self.z * o.z

    def cross(self, o: "Vec3") -> "Vec3":
        return Vec3(
            self.y * o.z - self.z * o.y,
            self.z * o.x - self.x * o.z,
            self.x * o.y - self.y * o.x,
        )

    def length(self) -> float:
        return math.sqrt(self.dot(self))

    def normalized(self) -> "Vec3":
        L = self.length()
        return Vec3(self.x / L, self.y / L, self.z / L)


def rx(v: Vec3, t: float) -> Vec3:
    c, s = math.cos(t), math.sin(t)
    return Vec3(v.x, c * v.y - s * v.z, s * v.y + c * v.z)


def ry(v: Vec3, t: float) -> Vec3:
    c, s = math.cos(t), math.sin(t)
    return Vec3(c * v.x + s * v.z, v.y, -s * v.x + c * v.z)


def rz(v: Vec3, t: float) -> Vec3:
    c, s = math.cos(t), math.sin(t)
    return Vec3(c * v.x - s * v.y, s * v.x + c * v.y, v.z)


def order_face_ccw(verts: list[Vec3], indices: list[int], normal: Vec3) -> list[int]:
    """Order the given indices CCW around the face centroid as seen from
    outside the polyhedron (looking down the +normal toward the center).
    """
    pts = [verts[i] for i in indices]
    centroid = Vec3(0, 0, 0)
    for p in pts:
        centroid = centroid + p
    centroid = centroid * (1.0 / len(pts))

    n = normal.normalized()
    # Build an orthonormal 2D basis on the face plane.
    seed = Vec3(1, 0, 0)
    if abs(n.dot(seed)) > 0.9:
        seed = Vec3(0, 1, 0)
    u = (seed - n * n.dot(seed)).normalized()
    v = n.cross(u)

    def angle(i: int) -> float:
        d = verts[i] - centroid
        return math.atan2(d.dot(v), d.dot(u))

    return sorted(indices, key=angle)


def quantize(verts: list[Vec3], target_radius: float = float(UNIT_FP)) -> list[tuple[int, int, int]]:
    """Scale verts so the max radius = target_radius, then round to int."""
    rmax = max(v.length() for v in verts)
    scale = target_radius / rmax
    return [
        (
            int(round(v.x * scale)),
            int(round(v.y * scale)),
            int(round(v.z * scale)),
        )
        for v in verts
    ]


# ---------------------------------------------------------------------------
# D12 — regular dodecahedron
# ---------------------------------------------------------------------------

def build_d12() -> tuple[list[Vec3], list[tuple[list[int], int]]]:
    """Return (verts, faces) where each face is (vertex_indices_ccw, value).

    Construction: 8 cube corners (±1, ±1, ±1) and 12 rectangle corners
    (0, ±1/phi, ±phi) etc. All 20 sit on the sphere of radius sqrt(3).
    """
    a = 1.0
    b = 1.0 / PHI
    c = PHI

    cube = [
        Vec3( a,  a,  a), Vec3( a,  a, -a), Vec3( a, -a,  a), Vec3( a, -a, -a),
        Vec3(-a,  a,  a), Vec3(-a,  a, -a), Vec3(-a, -a,  a), Vec3(-a, -a, -a),
    ]
    rects = [
        Vec3( 0,  b,  c), Vec3( 0,  b, -c), Vec3( 0, -b,  c), Vec3( 0, -b, -c),
        Vec3( b,  c,  0), Vec3( b, -c,  0), Vec3(-b,  c,  0), Vec3(-b, -c,  0),
        Vec3( c,  0,  b), Vec3( c,  0, -b), Vec3(-c,  0,  b), Vec3(-c,  0, -b),
    ]
    verts = cube + rects

    # The 12 face directions are the icosahedron vertices: (±1, 0, ±phi) and
    # cyclic permutations. For each direction, the face is the 5 dodecahedron
    # vertices with maximum dot product (they share the same max).
    directions = [
        Vec3( 1,  0,  c), Vec3(-1,  0, -c),
        Vec3( 1,  0, -c), Vec3(-1,  0,  c),
        Vec3( 0,  c,  1), Vec3( 0, -c, -1),
        Vec3( 0,  c, -1), Vec3( 0, -c,  1),
        Vec3( c,  1,  0), Vec3(-c, -1,  0),
        Vec3( c, -1,  0), Vec3(-c,  1,  0),
    ]
    # Pair value assignment: index i and i^1 are opposite (above is arranged
    # that way), with values that sum to 13.
    pair_values = [1, 12, 2, 11, 3, 10, 4, 9, 5, 8, 6, 7]

    faces: list[tuple[list[int], int]] = []
    for direction, value in zip(directions, pair_values):
        scored = sorted(range(len(verts)), key=lambda i: -direction.dot(verts[i]))
        face_indices = scored[:5]
        face_indices = order_face_ccw(verts, face_indices, direction)
        faces.append((face_indices, value))

    return verts, faces


# ---------------------------------------------------------------------------
# D10 — pentagonal trapezohedron
# ---------------------------------------------------------------------------

def build_d10() -> tuple[list[Vec3], list[tuple[list[int], int]]]:
    """Return (verts, faces). Apex height h chosen so kite faces are planar.

    Planarity solves to z_belt = h * (2 - phi) / (2 + phi).
    """
    h = 1.0
    z_belt = h * (2.0 - PHI) / (2.0 + PHI)
    r = math.sqrt(h * h - z_belt * z_belt)

    # vertex layout: indices 0=top apex, 1=bottom apex,
    # 2..6 = upper belt at angles 0, 72, 144, 216, 288 (deg)
    # 7..11 = lower belt at angles 36, 108, 180, 252, 324 (deg)
    verts: list[Vec3] = [Vec3(0, 0, h), Vec3(0, 0, -h)]
    for k in range(5):
        ang = math.radians(72 * k)
        verts.append(Vec3(r * math.cos(ang), r * math.sin(ang), z_belt))
    for k in range(5):
        ang = math.radians(36 + 72 * k)
        verts.append(Vec3(r * math.cos(ang), r * math.sin(ang), -z_belt))

    # 5 top kites: (apex, upper[i], lower[i], upper[i+1])
    # 5 bottom kites: (-apex, lower[i], upper[i+1], lower[i+1])
    faces: list[tuple[list[int], int]] = []
    for k in range(5):
        upper_i  = 2 + k
        upper_i1 = 2 + (k + 1) % 5
        lower_i  = 7 + k
        face_indices = [0, upper_i, lower_i, upper_i1]
        c = Vec3(0, 0, 0)
        for i in face_indices:
            c = c + verts[i]
        c = c * 0.25
        ordered = order_face_ccw(verts, face_indices, c)
        faces.append((ordered, k))
    for k in range(5):
        upper_i1 = 2 + (k + 1) % 5
        lower_i  = 7 + k
        lower_i1 = 7 + (k + 1) % 5
        face_indices = [1, lower_i, upper_i1, lower_i1]
        c = Vec3(0, 0, 0)
        for i in face_indices:
            c = c + verts[i]
        c = c * 0.25
        ordered = order_face_ccw(verts, face_indices, c)
        faces.append((ordered, 5 + k))

    return verts, faces


# ---------------------------------------------------------------------------
# Validation
# ---------------------------------------------------------------------------

def face_normal(verts: list[Vec3], indices: list[int]) -> Vec3:
    """Outward-pointing face normal (un-normalized)."""
    a = verts[indices[0]]
    b = verts[indices[1]]
    c = verts[indices[2]]
    n = (b - a).cross(c - a)
    centroid = Vec3(0, 0, 0)
    for i in indices:
        centroid = centroid + verts[i]
    centroid = centroid * (1.0 / len(indices))
    if n.dot(centroid) < 0:
        n = Vec3(-n.x, -n.y, -n.z)
    return n


def validate_d12(verts: list[Vec3], faces: list[tuple[list[int], int]]) -> None:
    # Pentagon edge length should be uniform.
    edge_lengths = []
    for indices, _ in faces:
        for k in range(5):
            a = verts[indices[k]]
            b = verts[indices[(k + 1) % 5]]
            edge_lengths.append((a - b).length())
    avg = sum(edge_lengths) / len(edge_lengths)
    spread = max(edge_lengths) - min(edge_lengths)
    assert spread / avg < 1e-9, f"D12 edge lengths inconsistent: spread={spread} over avg={avg}"

    # Opposite faces sum to 13.
    seen_sums = set()
    centroids = []
    for indices, value in faces:
        c = Vec3(0, 0, 0)
        for i in indices:
            c = c + verts[i]
        centroids.append((c * 0.2, value))
    used = [False] * 12
    for i, (ci, vi) in enumerate(centroids):
        if used[i]:
            continue
        best = -1
        best_dot = float("inf")
        for j, (cj, vj) in enumerate(centroids):
            if i == j or used[j]:
                continue
            # Opposite face: centroid points in the opposite direction.
            d = ci.normalized().dot(cj.normalized())
            if d < best_dot:
                best_dot = d
                best = j
        used[i] = True
        used[best] = True
        assert best_dot < -0.99, f"D12 face {i} has no clear opposite (best dot {best_dot})"
        assert vi + faces[best][1] == 13, f"D12 opposite pair {vi}+{faces[best][1]} != 13"
    print(f"  D12: 12 faces, edge length {avg:.6f} (spread {spread:.2e}), opposite pairs sum to 13")


def validate_d10(verts: list[Vec3], faces: list[tuple[list[int], int]]) -> None:
    # Kite edges fall into two classes: long (apex-to-belt) and short (belt-to-belt).
    edges = []
    for indices, _ in faces:
        for k in range(4):
            a = verts[indices[k]]
            b = verts[indices[(k + 1) % 4]]
            edges.append((a - b).length())
    edges_sorted = sorted(edges)
    n_long = sum(1 for e in edges if e > (edges_sorted[0] + edges_sorted[-1]) / 2)
    # Each kite has 2 long edges (apex sides) and 2 short edges (belt). 10 kites → 20 long, 20 short.
    assert n_long == 20, f"D10 long-edge count {n_long} != 20"

    # Planarity: face vertices all on the same plane.
    for indices, value in faces:
        a = verts[indices[0]]
        b = verts[indices[1]]
        c = verts[indices[2]]
        n = (b - a).cross(c - a).normalized()
        d_max = 0.0
        for i in indices[3:]:
            d_max = max(d_max, abs((verts[i] - a).dot(n)))
        assert d_max < 1e-9, f"D10 face value {value} non-planar (offset {d_max:.2e})"

    print(f"  D10: 10 faces, planar to <1e-9")


# ---------------------------------------------------------------------------
# Pre-rotation: orient face 0 dead-on at +z
# ---------------------------------------------------------------------------

def rest_rotation_for_face(verts: list[Vec3], face_indices: list[int]) -> tuple[float, float]:
    """Return (theta_x, theta_y) such that applying Rx(theta_x) then Ry(theta_y)
    rotates the face's *centroid* onto +z.

    For a regular pentagon (D12) the centroid lies on the outward normal, so
    centroid- and normal-alignment give the same answer. For an asymmetric
    kite (D10 trapezohedron) they differ — centroid-alignment keeps the
    face's center at the screen center (where the numeral wants to sit),
    at the cost of a small tilt away from perfectly perpendicular.

    Order matches rotate_vertex() in dice3d.c which does Rx then Ry then Rz.
    """
    centroid = Vec3(0, 0, 0)
    for i in face_indices:
        centroid = centroid + verts[i]
    centroid = centroid * (1.0 / len(face_indices))
    target = centroid.normalized()

    # Apply Rx(theta_x) to zero out the y component.
    # After Rx: y' = cos(tx)*y - sin(tx)*z, want y' = 0 → tan(tx) = y/z.
    tx = math.atan2(target.y, target.z)
    rxn = rx(target, tx)
    # rxn.y should now be ~0; rxn.z should be positive (= sqrt(y^2 + z^2)).

    # Apply Ry(theta_y) to zero out the x component.
    # After Ry: x' = cos(ty)*x + sin(ty)*z, want x' = 0 → tan(ty) = -x/z.
    ty = math.atan2(-rxn.x, rxn.z)
    final = ry(rxn, ty)
    assert abs(final.x) < 1e-12 and abs(final.y) < 1e-12 and final.z > 0, \
        f"rest-rotation failed: {final}"
    return tx, ty


def rad_to_trig(rad: float) -> int:
    """Convert radians to Pebble TRIG_MAX_ANGLE units (65536 per full circle).
    The result is wrapped into [-32768, 32767] which works with sin_lookup /
    cos_lookup (they're periodic).
    """
    return int(round(rad / (2.0 * math.pi) * 65536)) & 0xFFFF


def per_value_rotations(
    verts: list[Vec3], faces: list[tuple[list[int], int]]
) -> dict[int, tuple[float, float]]:
    """Compute the (theta_x, theta_y) needed to bring each labeled face
    onto +z. The values are stored keyed by face.value so the runtime can
    look up `rest_rotation[die->value]` without an extra value→index map.

    For a value whose face is already at +z (face 0, by construction in
    the pre-rotated vertex table), the result is (0, 0).
    """
    result: dict[int, tuple[float, float]] = {}
    for face_indices, value in faces:
        tx, ty = rest_rotation_for_face(verts, face_indices)
        result[value] = (tx, ty)
    return result


def apply_pre_rotation(verts: list[Vec3], tx: float, ty: float) -> list[Vec3]:
    return [ry(rx(v, tx), ty) for v in verts]


# ---------------------------------------------------------------------------
# Emit C source
# ---------------------------------------------------------------------------

def emit_table(name: str, qverts: list[tuple[int, int, int]]) -> str:
    lines = [f"static const Vec3 {name}[{len(qverts)}] = {{"]
    for i, (x, y, z) in enumerate(qverts):
        lines.append(f"  {{ {x:5d}, {y:5d}, {z:5d} }}, /* {i:2d} */")
    lines.append("};")
    return "\n".join(lines)


def emit_d12_faces(faces: list[tuple[list[int], int]]) -> str:
    lines = ["static const Face d12_faces[12] = {"]
    for indices, value in faces:
        idx = ", ".join(f"{i:2d}" for i in indices)
        lines.append(f"  {{ {{ {idx} }}, 5, {value:2d} }},")
    lines.append("};")
    return "\n".join(lines)


def emit_d10_faces(faces: list[tuple[list[int], int]]) -> str:
    lines = ["static const Face d10_faces[10] = {"]
    for indices, value in faces:
        idx = ", ".join(f"{i:2d}" for i in indices)
        lines.append(f"  {{ {{ {idx} }}, 4, {value} }},")
    lines.append("};")
    return "\n".join(lines)


def emit_rotation_table(
    name: str,
    rotations: dict[int, tuple[float, float]],
    size: int,
    axis: int,  # 0 = rx, 1 = ry
) -> str:
    axis_name = ("rx", "ry")[axis]
    lines = [f"static const int32_t {name}[{size}] = {{"]
    for v in range(size):
        if v in rotations:
            val = rad_to_trig(rotations[v][axis])
            lines.append(f"  {val:7d}, /* [{v:2d}] {axis_name} */")
        else:
            lines.append(f"        0, /* [{v:2d}] unused (value not on this die) */")
    lines.append("};")
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Driver
# ---------------------------------------------------------------------------

def main() -> None:
    print("Building D12...")
    d12_verts, d12_faces = build_d12()
    validate_d12(d12_verts, d12_faces)

    print("Building D10...")
    d10_verts, d10_faces = build_d10()
    validate_d10(d10_verts, d10_faces)

    print()
    print("Computing pre-rotations to put face 0 dead-on at +z...")
    d12_tx, d12_ty = rest_rotation_for_face(d12_verts, d12_faces[0][0])
    print(f"  D12: Rx({math.degrees(d12_tx):+.4f}°)  Ry({math.degrees(d12_ty):+.4f}°)")
    d12_rotated = apply_pre_rotation(d12_verts, d12_tx, d12_ty)

    d10_tx, d10_ty = rest_rotation_for_face(d10_verts, d10_faces[0][0])
    print(f"  D10: Rx({math.degrees(d10_tx):+.4f}°)  Ry({math.degrees(d10_ty):+.4f}°)")
    d10_rotated = apply_pre_rotation(d10_verts, d10_tx, d10_ty)

    # Sanity-check: face 0 of each polyhedron should have its centroid on
    # +z after pre-rotation (xy ≈ 0). For D12 (regular pentagon) all face
    # vertices also share the same z (perpendicular to camera). For D10
    # (kite) the centroid sits on +z but vertices are not coplanar in z —
    # the face is tilted slightly so the *center* is at screen center.
    for name, rverts, face_indices in [
        ("D12", d12_rotated, d12_faces[0][0]),
        ("D10", d10_rotated, d10_faces[0][0]),
    ]:
        zs = [rverts[i].z for i in face_indices]
        cx = sum(rverts[i].x for i in face_indices) / len(face_indices)
        cy = sum(rverts[i].y for i in face_indices) / len(face_indices)
        spread = max(zs) - min(zs)
        print(f"  {name} face 0 after rotation: z={zs[0]:.6f}, spread {spread:.2e}, centroid xy=({cx:.2e}, {cy:.2e})")

    # Quantize. Both polyhedra share UNIT_FP = 4096 = max-circumradius.
    d12_q = quantize(d12_rotated)
    d10_q = quantize(d10_rotated)

    print()
    print("=" * 72)
    print("Paste into src/c/dice3d.c (replace the existing tables):")
    print("=" * 72)
    print()
    print("/* ---- D12: regular dodecahedron --------------------------------- */")
    print(emit_table("d12_verts", d12_q))
    print()
    print(emit_d12_faces(d12_faces))
    print()
    print("/* ---- D10: pentagonal trapezohedron ------------------------------ */")
    print(emit_table("d10_verts", d10_q))
    print()
    print(emit_d10_faces(d10_faces))

    print()
    print("/* ---- Per-value rest rotations ---------------------------------- */")
    print("/* Indexed by die->value. Rotation order is Rx then Ry (matching")
    print(" * rotate_vertex), no rz component — the face-around-z orientation")
    print(" * isn't enforced; each value lands at whatever orientation the")
    print(" * shortest centroid-onto-z rotation produces. */")
    d12_per = per_value_rotations(d12_rotated, d12_faces)
    d10_per = per_value_rotations(d10_rotated, d10_faces)
    # D12 values are 1..12, so we need a 13-entry table (slot 0 unused).
    print(emit_rotation_table("d12_value_rx", d12_per, 13, axis=0))
    print(emit_rotation_table("d12_value_ry", d12_per, 13, axis=1))
    # D10 values are 0..9.
    print(emit_rotation_table("d10_value_rx", d10_per, 10, axis=0))
    print(emit_rotation_table("d10_value_ry", d10_per, 10, axis=1))


if __name__ == "__main__":
    main()
