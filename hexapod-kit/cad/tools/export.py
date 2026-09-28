#!/usr/bin/env python3
"""Export every printable part to a binary STL (hexapod-kit/cad/stl/) and print volumes.

    python3 hexapod-kit/cad/tools/export.py

OpenSCAD writes ASCII STL; binary STL is ~5x smaller and is what JLC's upload page likes best.
Fails on any OpenSCAD warning (like `--hardwarnings` in CI).

Where two cuts cross almost on top of each other (a lightening hole grazing a rounded keep-out),
CGAL leaves edges a few micrometres long. JLC's upload check can flag those as bad edges, so
vertices closer than WELD mm are merged and the triangles that collapse are dropped, and
needle triangles (a corner less than NEEDLE mm off the opposite edge) are removed by splitting
their neighbour at that corner; check_stl.py then proves the mesh is still closed and consistently wound.
"""
import os
import re
import struct
import subprocess
import sys
import tempfile

CAD = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WELD = 0.01
NEEDLE = 0.001
PARTS = ["coxa", "femur", "tibia", "body_front_left", "body_front_right", "body_rear_left", "body_rear_right", "splice_long", "splice_short", "deck", "lidar_mount"]


def weld(tris):
    """Merge vertices closer than WELD, drop collapsed triangles, recompute normals."""
    cell = {}
    rep = {}

    def snap(p):
        if p in rep:
            return rep[p]
        g = tuple(int(x // WELD) for x in p)
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for dz in (-1, 0, 1):
                    for q in cell.get((g[0] + dx, g[1] + dy, g[2] + dz), ()):
                        if sum((a - b) ** 2 for a, b in zip(p, q)) < WELD * WELD:
                            rep[p] = q
                            return q
        cell.setdefault(g, []).append(p)
        rep[p] = p
        return p
    out = []
    for t in tris:
        a, b, c = (snap(tuple(t[i:i + 3])) for i in (0, 3, 6))
        if a != b and b != c and c != a:
            out.append((a, b, c))
    return out


def cross(a, b, c):
    u = [b[k] - a[k] for k in range(3)]
    v = [c[k] - a[k] for k in range(3)]
    return [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]]


def length(v):
    return sum(x * x for x in v) ** 0.5


def drop_needles(tris):
    """Needle (a, b, c) with c on the edge ab: delete it and split the neighbour (b, a, x) at c."""
    tris = list(tris)
    while True:
        edge = {}
        for i, t in enumerate(tris):
            if t:
                for k in range(3):
                    edge[(t[k], t[(k + 1) % 3])] = i
        for i, t in enumerate(tris):
            if not t:
                continue
            k = max(range(3), key=lambda k: length([t[(k + 1) % 3][m] - t[k][m] for m in range(3)]))
            a, b, c = t[k], t[(k + 1) % 3], t[(k + 2) % 3]
            if length(cross(a, b, c)) >= NEEDLE * length([b[m] - a[m] for m in range(3)]):
                continue
            j = edge.get((b, a))
            if j is None:
                continue
            n = tris[j]
            x = n[(n.index(a) + 1) % 3]
            if x == c:
                continue
            tris[i], tris[j] = None, (b, c, x)
            tris.append((c, a, x))
            break
        else:
            return [t for t in tris if t]


def with_normals(tris):
    out = []
    for a, b, c in tris:
        n = cross(a, b, c)
        m = length(n) or 1.0
        out.append([x / m for x in n] + list(a) + list(b) + list(c))
    return out


def ascii_to_binary(src, dst):
    text = open(src).read()
    tris = re.findall(r"facet normal\s+\S+ \S+ \S+\s+outer loop\s+"
                      r"vertex\s+(\S+) (\S+) (\S+)\s+vertex\s+(\S+) (\S+) (\S+)\s+vertex\s+(\S+) (\S+) (\S+)", text)
    # weld in a fixed order, then sort: CGAL's triangle order changes from run to run
    tris = with_normals(drop_needles(weld(sorted([float(x) for x in t] for t in tris))))
    tris = sorted(tris, key=lambda t: t[3:])
    vol = 0.0
    lo, hi = [1e9] * 3, [-1e9] * 3
    with open(dst, "wb") as f:
        f.write(b"hexapod-kit printed body".ljust(80, b" "))
        f.write(struct.pack("<I", len(tris)))
        for t in tris:
            f.write(struct.pack("<12fH", *t, 0))
            a, b, c = t[3:6], t[6:9], t[9:12]
            vol += (a[0] * (b[1] * c[2] - b[2] * c[1]) - a[1] * (b[0] * c[2] - b[2] * c[0])
                    + a[2] * (b[0] * c[1] - b[1] * c[0])) / 6.0
            for p in (a, b, c):
                for k in range(3):
                    lo[k], hi[k] = min(lo[k], p[k]), max(hi[k], p[k])
    return len(tris), abs(vol), [h - l for l, h in zip(lo, hi)]


def main():
    ok = True
    for name in PARTS:
        with tempfile.TemporaryDirectory() as tmp:
            asc = os.path.join(tmp, name + ".stl")
            out = subprocess.run(["openscad", "--hardwarnings", "-o", asc, os.path.join(CAD, name + ".scad")],
                                 capture_output=True, text=True)
            if out.returncode != 0 or not os.path.exists(asc):
                print(out.stdout + out.stderr)
                ok = False
                continue
            n, vol, size = ascii_to_binary(asc, os.path.join(CAD, "stl", name + ".stl"))
        print(f"{name:11s} {n:6d} triangles  {vol / 1000:6.1f} cm3  "
              f"{size[0]:.1f} x {size[1]:.1f} x {size[2]:.1f} mm")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
