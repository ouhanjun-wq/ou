#!/usr/bin/env python3
"""Interference check for the printed hexapod body (hexapod-kit/cad).

    python3 hexapod-kit/cad/tools/check_clearance.py

Every case builds two groups of solids in OpenSCAD and renders their intersection.
OpenSCAD reports an empty top-level object when nothing overlaps; anything else is a
collision and the script fails. Cases:
  * each leg's coxa bracket + femur servo swept to coxa -60 / 0 / +60 deg (the firmware
    coxa_lim) against the body plate, coxa servos, standoffs and the deck,
  * the femur / tibia range of motion (femur -70..80, tibia -60..70) against the hip. The
    printed legs need tibia_min = -60 in the firmware (the default -80 folds the knee into
    the femur servo),
  * neighbouring legs turned 30 deg towards each other at the stand pose (walking uses about
    +-20 deg; at +-40 the feet of neighbouring legs meet).
"""
import itertools
import math
import os
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
CAD = os.path.dirname(HERE)

# Lightening holes only remove material, so the check uses the solid parts: same answer, much faster.
HEADER = """include <%s/assembly.scad>
lighten = false;
""" % CAD.replace("\\", "/")

LEG_TMPL = """
module grp_{name}() {{ {body} }}
"""


def leg(i, c, f, t, part):
    return (f'at_leg(legs[{i}]) leg_moving([{c}, {f}, {t}], "{part}");')


def case(name, a, b):
    src = HEADER + "show = \"none\";\n" + f"intersection() {{ union() {{ {a} }} union() {{ {b} }} }}\n"
    return name, src


FRAME = "frame_fixed();"
STAND_F, STAND_T = 17.3119, -14.0519

cases = []
for i, c in itertools.product(range(6), (-60, 0, 60)):
    cases.append(case(f"leg {i} coxa {c:+d} vs body", leg(i, c, STAND_F, STAND_T, "coxa"), FRAME))
def foot_r(f, t, coxa=28, femur=50, tibia=75):
    """Horizontal foot distance from the hip-yaw axis (forward kinematics, as in kinematics.h)."""
    return coxa + femur * math.cos(math.radians(f)) + tibia * math.cos(math.radians(f + t - 90))


for f, t in itertools.product((-70, -30, 0, 40, 80), (-60, -30, 0, 40, 70)):
    if foot_r(f, t) < 20:   # foot under the body: the IK never asks for this (stance >= 20 mm)
        print(f"skip      femur {f:+d} tibia {t:+d}: foot under the body (r = {foot_r(f, t):.0f} mm)")
        continue
    hip = leg(0, 0, f, t, "coxa") + "frame_fixed();"
    moving = leg(0, 0, f, t, "femur") + leg(0, 0, f, t, "tibia")
    cases.append(case(f"femur {f:+d} tibia {t:+d} vs hip/body", moving, hip))
    cases.append(case(f"femur {f:+d} tibia {t:+d} tibia vs femur", leg(0, 0, f, t, "tibia"), leg(0, 0, f, t, "femur")))
# neighbours on one side turned towards each other (LF+LM, LM+LR, RF+RM, RM+RR) at the stand pose
for a, b in ((0, 1), (1, 2), (3, 4), (4, 5)):
    for c in (20, 30):
        sa, sb = (c, -c) if a < 3 else (-c, c)
        cases.append(case(f"legs {a}/{b} coxa {sa:+d}/{sb:+d}", leg(a, sa, STAND_F, STAND_T, "all"),
                          leg(b, sb, STAND_F, STAND_T, "all")))


def run(item):
    name, src = item
    with tempfile.NamedTemporaryFile("w", suffix=".scad", delete=False, dir=CAD) as fh:
        fh.write(src)
        path = fh.name
    try:
        out = subprocess.run(["openscad", "-o", path + ".stl", path], capture_output=True, text=True)
        log = out.stdout + out.stderr
        empty = "Current top level object is empty" in log
        err = "ERROR" in log and not empty
        return name, empty, err, log
    finally:
        for p in (path, path + ".stl"):
            if os.path.exists(p):
                os.remove(p)


def main():
    bad = 0
    with ThreadPoolExecutor(max_workers=os.cpu_count() or 2) as ex:
        for name, empty, err, log in ex.map(run, cases):
            ok = empty and not err
            print(("ok        " if ok else "COLLISION ") + name, flush=True)
            if not ok:
                bad += 1
                if err:
                    print(log[-800:])
    print(f"{len(cases) - bad} / {len(cases)} cases clear")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
