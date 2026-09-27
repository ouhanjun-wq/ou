#!/usr/bin/env python3
"""Export every printable part to a binary STL (hexapod-kit/cad/stl/) and print volumes.

    python3 hexapod-kit/cad/tools/export.py

OpenSCAD writes ASCII STL; binary STL is ~5x smaller and is what JLC's upload page likes best.
Fails on any OpenSCAD warning (like `--hardwarnings` in CI).
"""
import os
import re
import struct
import subprocess
import sys
import tempfile

CAD = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PARTS = ["coxa", "femur", "tibia", "body_plate", "deck"]


def ascii_to_binary(src, dst):
    text = open(src).read()
    tris = re.findall(r"facet normal\s+(\S+) (\S+) (\S+)\s+outer loop\s+"
                      r"vertex\s+(\S+) (\S+) (\S+)\s+vertex\s+(\S+) (\S+) (\S+)\s+vertex\s+(\S+) (\S+) (\S+)", text)
    vol = 0.0
    lo, hi = [1e9] * 3, [-1e9] * 3
    with open(dst, "wb") as f:
        f.write(b"hexapod-kit printed body".ljust(80, b" "))
        f.write(struct.pack("<I", len(tris)))
        for t in tris:
            v = [float(x) for x in t]
            f.write(struct.pack("<12fH", *v, 0))
            a, b, c = v[3:6], v[6:9], v[9:12]
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
