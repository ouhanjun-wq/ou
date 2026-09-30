#!/usr/bin/env python3
"""Build the print-service pack: binary STLs named NN_part_xQTY.stl + a zip.

    cad/export_all.sh                      # first: re-export cad/stl/ after changing the models
    python3 print/make_print_pack.py       # then: rebuild print/stl/ and print/bionic_hand_print_files.zip

Pure standard library. Checks every mesh is closed (each edge shared by exactly two triangles)
and prints the size / solid volume table used in 下单清单.md.
"""
import collections
import os
import re
import struct
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "cad", "stl")
OUT = os.path.join(HERE, "stl")
ZIP = os.path.join(HERE, "bionic_hand_print_files.zip")

# (source file, quantity per hand)
PARTS = [
    ("palm", 1), ("cover_dorsal", 1), ("cover_palmar", 2), ("thenar_cover", 1), ("thumb_bracket", 1),
    ("index_proximal", 1), ("index_distal", 1), ("index_link", 1), ("index_rod", 1),
    ("middle_proximal", 1), ("middle_distal", 1), ("middle_link", 1), ("middle_rod", 1),
    ("ring_proximal", 1), ("ring_distal", 1), ("ring_link", 1), ("ring_rod", 1),
    ("pinky_proximal", 1), ("pinky_distal", 1), ("pinky_link", 1), ("pinky_rod", 1),
    ("thumb_proximal", 1), ("thumb_distal", 1), ("thumb_link", 1),
    ("glove_plate", 1), ("glove_crank", 6), ("glove_link", 6), ("glove_ring_M", 4), ("glove_thumb_ring", 1),
]

FACET = re.compile(r"facet normal\s+(\S+)\s+(\S+)\s+(\S+)\s+outer loop" + r"\s+vertex\s+(\S+)\s+(\S+)\s+(\S+)" * 3)


def read_ascii(path):
    with open(path, encoding="ascii") as fh:
        return [[float(x) for x in m.groups()] for m in FACET.finditer(fh.read())]


def closed(tris):
    edges = collections.Counter()
    for t in tris:
        v = [tuple(round(c, 5) for c in t[i:i + 3]) for i in (3, 6, 9)]
        for a, b in ((v[0], v[1]), (v[1], v[2]), (v[2], v[0])):
            edges[(a, b) if a < b else (b, a)] += 1
    return all(n == 2 for n in edges.values())


def volume_cm3(tris):
    vol = 0.0
    for t in tris:
        a, b, c = t[3:6], t[6:9], t[9:12]
        vol += (a[0] * (b[1] * c[2] - b[2] * c[1]) - a[1] * (b[0] * c[2] - b[2] * c[0])
                + a[2] * (b[0] * c[1] - b[1] * c[0])) / 6.0
    return abs(vol) / 1000.0


def write_binary(path, tris, name):
    header = f"bionic_hand {name} (mm)".encode("ascii").ljust(80, b" ")[:80]
    with open(path, "wb") as fh:
        fh.write(header)
        fh.write(struct.pack("<I", len(tris)))
        for t in tris:
            fh.write(struct.pack("<12fH", *t, 0))


def main():
    os.makedirs(OUT, exist_ok=True)
    for f in os.listdir(OUT):
        if f.endswith(".stl"):
            os.remove(os.path.join(OUT, f))
    ok, files, total = True, [], 0.0
    for i, (name, qty) in enumerate(PARTS, 1):
        tris = read_ascii(os.path.join(SRC, name + ".stl"))
        if not tris or not closed(tris):
            print(f"FAIL {name}: empty or not a closed mesh")
            ok = False
            continue
        out = f"{i:02d}_{name}_x{qty}.stl"
        write_binary(os.path.join(OUT, out), tris, name)
        xs = [t[k] for t in tris for k in (3, 6, 9)]
        ys = [t[k] for t in tris for k in (4, 7, 10)]
        zs = [t[k] for t in tris for k in (5, 8, 11)]
        vol = volume_cm3(tris)
        total += qty * vol
        files.append(out)
        print(f"{out:30s} x{qty}  {max(xs) - min(xs):5.0f} x {max(ys) - min(ys):4.0f} x {max(zs) - min(zs):4.0f} mm"
              f"  {vol:6.1f} cm3")
    print(f"{sum(q for _, q in PARTS)} pieces, {total:.0f} cm3 solid")
    with zipfile.ZipFile(ZIP, "w", zipfile.ZIP_DEFLATED) as z:
        for f in files:
            z.write(os.path.join(OUT, f), "stl/" + f)
        z.write(os.path.join(HERE, "下单清单.md"), "下单清单.md")
    print(f"wrote {ZIP}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
