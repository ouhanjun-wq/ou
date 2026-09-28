#!/usr/bin/env python3
"""Check every STL in hexapod-kit/cad/stl against JLC's free-printing rules (嘉立创免费打样):

    python3 hexapod-kit/cad/tools/check_stl.py

Per model:
  * size <= 100 x 100 x 100 mm
  * closed, manifold mesh: every edge is shared by exactly two triangles ("坏边 / 孔洞")
  * no reversed triangles ("反向三角面"): the two triangles on an edge walk it in opposite
    directions, the signed volume is positive (normals point outwards) and each stored
    normal agrees with its triangle's winding
  * no slivers or zero-area triangles (an edge shorter than 0.001 mm, or a corner closer than
    0.001 mm to the opposite edge): repair tools report these as bad edges / degenerate faces
  * one shell ("多壳体结构")
Each order (a coupon) may hold at most 70 cm3 in total: ORDERS below groups the parts.
"""
import os
import struct
import sys
from collections import defaultdict

CAD = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STL = os.path.join(CAD, "stl")
MAX_SIZE = 100.0
MAX_ORDER_CM3 = 70.0

# file -> quantity, grouped into orders that each stay under 70 cm3
ORDERS = {
    "order 1 (hips)": {"coxa.stl": 6, "hip_cradle.stl": 6, "pivot_bushing.stl": 6},
    "order 2 (legs + small plates)": {"femur.stl": 6, "tibia.stl": 6, "splice_long.stl": 1, "splice_short.stl": 2,
                                      "lidar_mount.stl": 1},
    "order 3 (body + deck)": {"body_front_left.stl": 1, "body_front_right.stl": 1, "body_rear_left.stl": 1,
                              "body_rear_right.stl": 1, "deck.stl": 1},
}


def read_binary_stl(path):
    data = open(path, "rb").read()
    n = struct.unpack("<I", data[80:84])[0]
    if len(data) != 84 + 50 * n:
        raise ValueError("not a binary STL (size mismatch)")
    tris = []
    for i in range(n):
        v = struct.unpack("<12f", data[84 + 50 * i:84 + 50 * i + 48])
        tris.append((v[0:3], v[3:6], v[6:9], v[9:12]))
    return tris


def check(path):
    tris = read_binary_stl(path)
    problems = []
    key = lambda p: p          # exact: rounding would hide slivers
    ids = {}
    faces = []
    for normal, a, b, c in tris:
        f = tuple(ids.setdefault(key(p), len(ids)) for p in (a, b, c))
        faces.append(f)
    # edges
    directed = defaultdict(int)
    edge_faces = defaultdict(list)
    for fi, (a, b, c) in enumerate(faces):
        for u, v in ((a, b), (b, c), (c, a)):
            directed[(u, v)] += 1
            edge_faces[frozenset((u, v))].append(fi)
    open_edges = sum(1 for e, fs in edge_faces.items() if len(fs) == 1)
    nonmanifold = sum(1 for e, fs in edge_faces.items() if len(fs) > 2)
    flipped = sum(1 for (u, v), n in directed.items() if n > 1 or directed.get((v, u), 0) != 1)
    slivers = 0
    for normal, a, b, c in tris:
        if min(sum((p[k] - q[k]) ** 2 for k in range(3)) for p, q in ((a, b), (b, c), (c, a))) < 1e-6:
            slivers += 1                       # an edge shorter than 0.001 mm
    if slivers:
        problems.append(f"{slivers} sliver triangles (edge < 0.001 mm)")
    needles = 0
    for normal, a, b, c in tris:
        u = [b[k] - a[k] for k in range(3)]
        v = [c[k] - a[k] for k in range(3)]
        w = [c[k] - b[k] for k in range(3)]
        cr = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
        longest = max(sum(x * x for x in e) for e in (u, v, w)) ** 0.5
        if sum(x * x for x in cr) ** 0.5 < 0.001 * longest:
            needles += 1                       # a corner less than 0.001 mm from the opposite edge
    if needles:
        problems.append(f"{needles} degenerate (zero-area) triangles")
    if open_edges:
        problems.append(f"{open_edges} open edges (holes)")
    if nonmanifold:
        problems.append(f"{nonmanifold} non-manifold edges")
    if flipped:
        problems.append(f"{flipped} edges with inconsistent winding (reversed triangles)")
    # normals and volume
    vol = 0.0
    bad_normals = 0
    for normal, a, b, c in tris:
        ux, uy, uz = b[0] - a[0], b[1] - a[1], b[2] - a[2]
        vx, vy, vz = c[0] - a[0], c[1] - a[1], c[2] - a[2]
        cx, cy, cz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
        if cx * normal[0] + cy * normal[1] + cz * normal[2] < 0:
            bad_normals += 1
        vol += (a[0] * (b[1] * c[2] - b[2] * c[1]) - a[1] * (b[0] * c[2] - b[2] * c[0])
                + a[2] * (b[0] * c[1] - b[1] * c[0])) / 6.0
    if vol <= 0:
        problems.append("negative volume: the whole mesh is inside out")
    if bad_normals:
        problems.append(f"{bad_normals} stored normals disagree with the winding")
    # shells
    parent = list(range(len(faces)))

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x
    for fs in edge_faces.values():
        for f in fs[1:]:
            parent[find(f)] = find(fs[0])
    shells = len({find(f) for f in range(len(faces))})
    if shells != 1:
        problems.append(f"{shells} separate shells")
    # size
    lo = [min(p[k] for t in tris for p in t[1:]) for k in range(3)]
    hi = [max(p[k] for t in tris for p in t[1:]) for k in range(3)]
    size = [h - l for l, h in zip(lo, hi)]
    if max(size) > MAX_SIZE:
        problems.append("larger than 100 mm: %.1f x %.1f x %.1f" % tuple(size))
    return abs(vol) / 1000.0, size, problems


def main():
    bad = 0
    vols = {}
    for name in sorted(os.listdir(STL)):
        if not name.endswith(".stl"):
            continue
        vol, size, problems = check(os.path.join(STL, name))
        vols[name] = vol
        status = "ok " if not problems else "BAD"
        print(f"{status} {name:18s} {size[0]:6.1f} x {size[1]:6.1f} x {size[2]:6.1f} mm  {vol:5.1f} cm3"
              + ("" if not problems else "  <- " + "; ".join(problems)))
        bad += bool(problems)
    listed = set()
    for order, parts in ORDERS.items():
        total = 0.0
        for name, qty in parts.items():
            listed.add(name)
            if name not in vols:
                print(f"BAD {order}: {name} missing")
                bad += 1
                continue
            total += qty * vols[name]
        ok = total <= MAX_ORDER_CM3
        print(f"{'ok ' if ok else 'BAD'} {order}: {total:.1f} cm3 (limit {MAX_ORDER_CM3:.0f})")
        bad += not ok
    for name in vols:
        if name not in listed:
            print(f"BAD {name} is not in any order")
            bad += 1
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
