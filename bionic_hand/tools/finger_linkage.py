#!/usr/bin/env python3
"""Finger linkage design check for the bionic hand (single source of the linkage numbers).

Every finger is two printed segments driven by one servo:

    palm --MCP pin-- proximal phalanx P --PIP pin-- distal segment M (middle + tip, one piece)

* a coupling link (palm pivot G -> pivot C on M) makes the PIP joint bend together with the
  MCP joint (a crossed four-bar, so one servo curls the whole finger);
* a drive rod (servo horn tip H -> drive pin D on P) turns P about the MCP pin.
  The thumb has no rod: its proximal segment is screwed straight onto the servo horn.

The glove (exoskeleton on the operator's hand) is a third four-bar per finger:
  pot shaft P on the back-of-hand plate --crank-- link -- ring pin R on the finger.
It is checked for a monotonic pot angle, enough pot travel (ADC resolution) and transmission.

The script solves the four-bars over the whole stroke and checks them:
  - the linkage never locks (a solution exists, the motion is monotonic),
  - transmission angles stay inside [TRANS_MIN, 180 - TRANS_MIN],
  - the servo sweep fits the 180 deg servo,
  - the servo torque needed for a fingertip force F_TIP is below TORQUE_USE of the MG90S stall torque.

    python3 tools/finger_linkage.py          # print the table and check (exit 1 on failure)
    python3 tools/finger_linkage.py --scad   # also rewrite ../cad/linkage_params.scad

Plane of each finger: z = along the straight finger (distal +), y = dorsal (back of hand) +.
Flexion angle theta > 0 curls the finger towards the palm. Units: mm, degrees.
"""
import cmath
import math
import os
import sys

# ---- shared geometry ---------------------------------------------------------------------
C_OFF = complex(-7.0, 6.0)   # coupling pivot C on M, in M's frame (7 mm behind PIP, 6 mm dorsal)
G_OFF = complex(-3.0, -8.0)  # coupling ground pivot G on the palm (3 mm behind MCP, 8 mm palmar)
D_R = 12.0                   # drive pin D sits D_R from the MCP pin (angle: see Finger.__init__)
HORN_R = 10.0                # servo horn hole radius (MG90S single arm: use the 2nd hole)
SERVO_Z = -40.0              # servo output shaft, behind the MCP pins
LAYER_Y = 8.0                # dorsal-layer shafts at +LAYER_Y, palmar-layer shafts at -LAYER_Y
THETA_MAX = 85.0             # MCP flexion at a full fist
TRANS_MIN = 30.0             # smallest allowed transmission angle
SWEEP_MAX = 150.0            # servo travel we allow (keep off the 0 / 180 end stops)
F_TIP = 1.0                  # N, fingertip force the check must sustain (about 100 g per finger)
MG90S_STALL = 0.20           # N*m (2.0 kg*cm at 4.8 V; 2.2 kg*cm at 6 V)
TORQUE_USE = 0.7             # use at most 70 % of the stall torque

# ---- glove (operator's finger plane, origin on the operator's MCP axis) --------------------
GLOVE_RING = complex(20.0, 16.0)  # ring pin on the proximal phalanx: 20 mm out, 16 mm above the MCP axis
GLOVE_POT = complex(0.0, 26.0)    # pot shaft on the plate, straight above the MCP axis
GLOVE_CRANK = 18.0                # crank radius (pot shaft -> link pin)
GLOVE_BETA0 = 90.0                # crank angle with the finger straight (pointing up)
GLOVE_TMAX = 90.0                 # operator MCP flexion covered
GLOVE_SWEEP_MIN = 60.0            # pot travel needed: 60/300 of 1023 counts ~ 200 counts
POT_TRAVEL = 300.0                # WH148 electrical travel, degrees

# name: (proximal length L1, distal length L2, servo layer)
# Index and pinky servos lie in the dorsal layer (rod pushes to curl), middle and ring in the
# palmar layer (rod pulls to curl). Thumb: direct drive on the servo horn.
FINGERS = {
    "index":  (42.0, 46.0, "dorsal"),
    "middle": (46.0, 50.0, "palmar"),
    "ring":   (44.0, 48.0, "palmar"),
    "pinky":  (34.0, 38.0, "dorsal"),
    "thumb":  (36.0, 40.0, "direct"),
}


def rot(theta_deg):
    """Unit vector for a segment flexed by theta (curling towards -y)."""
    return cmath.exp(-1j * math.radians(theta_deg))


def solve(f, x0, lo, hi, step=0.5):
    """Root of f nearest x0 inside [lo, hi] (bracket outward from x0, then bisect)."""
    f0 = f(x0)
    if f0 == 0:
        return x0
    k = 1
    while True:
        moved = False
        for b in (x0 + k * step, x0 - k * step):
            if lo <= b <= hi:
                moved = True
                fb = f(b)
                if (fb < 0) != (f0 < 0):
                    a, fa = x0, f0
                    for _ in range(60):
                        m = 0.5 * (a + b)
                        fm = f(m)
                        if (fm < 0) == (fa < 0):
                            a, fa = m, fm
                        else:
                            b = m
                    return 0.5 * (a + b)
        if not moved:
            return None
        k += 1


def angle_between(u, v):
    """Unsigned angle between two plane vectors, degrees."""
    return math.degrees(abs(cmath.phase(v / u)))


class Finger:
    def __init__(self, name, l1, l2, layer):
        self.name, self.l1, self.l2, self.layer = name, l1, l2, layer
        self.G = G_OFF
        self.link = abs(l1 + C_OFF - self.G)  # coupling link length (straight finger)
        if layer != "direct":
            sign = 1.0 if layer == "dorsal" else -1.0
            self.S = complex(SERVO_Z, sign * LAYER_Y)
            # The drive arm sweeps THETA_MAX; centre that sweep on the perpendicular to the rod
            # (the rod runs roughly along z), so the transmission angle is best at both ends.
            self.D0 = D_R * cmath.exp(1j * math.radians(sign * 90.0 + THETA_MAX / 2))
            self.phi0 = self._best_phi0()
            self.rod = abs(self.D0 - self.horn(self.phi0))

    # --- coupling four-bar: MCP angle -> absolute angle of M ------------------------------
    def theta_m(self, t1, guess=None):
        b = self.l1 * rot(t1)
        f = lambda tm: abs(b + rot(tm) * C_OFF - self.G) - self.link
        return solve(f, t1 if guess is None else guess, t1 - 5.0, 200.0)

    def tip(self, t1, tm):
        return self.l1 * rot(t1) + self.l2 * rot(tm)

    # --- drive four-bar: servo horn angle -> MCP angle -------------------------------------
    def horn(self, phi):
        return self.S + HORN_R * cmath.exp(1j * math.radians(phi))

    def drive_pin(self, t1):
        return rot(t1) * self.D0

    def phi_for(self, t1, phi0=None, rod=None, guess=None):
        """Horn angle that puts the rod on D(t1). Both branches exist; take the one nearest guess."""
        phi0 = self.phi0 if phi0 is None else phi0
        rod = self.rod if rod is None else rod
        d = self.drive_pin(t1)
        f = lambda p: abs(d - self.horn(p)) - rod
        g = phi0 if guess is None else guess
        return solve(f, g, g - 179.0, g + 179.0, step=0.25)

    def _best_phi0(self):
        """Horn angle at the straight finger that maximises the worst transmission angle."""
        best, best_phi = -1.0, None
        for phi0 in range(-180, 180, 2):
            rod = abs(self.D0 - self.horn(phi0))
            if rod < 20.0:  # rod must clear the servo body
                continue
            worst, phi, ok = 180.0, phi0, True
            for t in range(0, int(THETA_MAX) + 1, 5):
                phi = self.phi_for(t, phi0, rod, phi)
                if phi is None or abs(phi - phi0) > SWEEP_MAX:
                    ok = False
                    break
                worst = min(worst, self._transmission(t, phi, rod))
            if ok and worst > best:
                best, best_phi = worst, float(phi0)
        if best_phi is None:
            raise SystemExit(f"{self.name}: no horn angle gives a working drive linkage")
        return best_phi

    def _transmission(self, t1, phi, rod=None):
        d, h = self.drive_pin(t1), self.horn(phi)
        rod_v = d - h
        a1 = angle_between(h - self.S, rod_v)  # at the horn
        a2 = angle_between(d, rod_v)           # at the drive pin
        return min(a1, 180.0 - a1, a2, 180.0 - a2)

    # --- the whole stroke -------------------------------------------------------------------
    def sweep(self, step=5):
        rows, tm, phi = [], None, None
        for t in range(0, int(THETA_MAX) + 1, step):
            tm = self.theta_m(t, tm)
            if tm is None:
                return None
            if self.layer == "direct":
                phi, trans = float(t), 90.0
            else:
                phi = self.phi_for(t, guess=phi)
                if phi is None:
                    return None
                trans = self._transmission(t, phi)
            rows.append((float(t), tm, tm - t, phi, trans, self.tip(t, tm)))
        return rows

    def torque(self, rows):
        """Worst servo torque (N*m) for F_TIP normal to M, by virtual work between rows."""
        worst = 0.0
        for a, b in zip(rows, rows[1:]):
            dphi = math.radians(abs(b[3] - a[3]))
            if dphi == 0:
                continue
            n = rot(a[1]) * 1j  # normal to M (dorsal side)
            dtip = b[5] - a[5]
            work = F_TIP * abs((dtip * n.conjugate()).real) / 1000.0  # N*m
            worst = max(worst, work / dphi)
        return worst


def check(verbose=True):
    ok = True
    for name, (l1, l2, layer) in FINGERS.items():
        f = Finger(name, l1, l2, layer)
        rows = f.sweep()
        if rows is None:
            print(f"FAIL {name}: linkage locks inside the stroke")
            ok = False
            continue
        pip = [r[2] for r in rows]
        phis = [r[3] for r in rows]
        sweep = abs(phis[-1] - phis[0])
        mono_pip = all(b > a for a, b in zip(pip, pip[1:]))
        mono_phi = all((b - a) * (phis[-1] - phis[0]) > 0 for a, b in zip(phis, phis[1:]))
        trans = min(r[4] for r in rows)
        tq = f.torque(rows)
        if verbose:
            print(f"\n== {name}: L1 {l1:.0f}  L2 {l2:.0f}  layer {layer}  coupling link {f.link:.2f} mm", end="")
            if layer != "direct":
                print(f"  rod {f.rod:.2f} mm  horn at straight {f.phi0:.0f} deg", end="")
            print()
            print("   MCP   PIP(rel)   servo   trans    tip z,y")
            for t, tm, rel, phi, tr, tip in rows[::3] + ([rows[-1]] if (len(rows) - 1) % 3 else []):
                print(f"  {t:4.0f}   {rel:6.1f}   {phi - phis[0]:6.1f}   {tr:5.1f}   {tip.real:6.1f},{tip.imag:6.1f}")
            print(f"   servo sweep {sweep:.0f} deg, min transmission {trans:.0f} deg, "
                  f"torque for {F_TIP:.0f} N at tip {tq * 100 / 9.81:.2f} kg*cm (MG90S {MG90S_STALL * 100 / 9.81:.1f})")
        problems = []
        if not mono_pip:
            problems.append("PIP not monotonic")
        if not mono_phi:
            problems.append("servo angle not monotonic")
        if sweep > SWEEP_MAX:
            problems.append(f"servo sweep {sweep:.0f} > {SWEEP_MAX:.0f}")
        if trans < TRANS_MIN:
            problems.append(f"transmission {trans:.0f} < {TRANS_MIN:.0f}")
        if pip[-1] < 0.7 * THETA_MAX:
            problems.append(f"PIP only {pip[-1]:.0f} deg at a fist")
        if tq > TORQUE_USE * MG90S_STALL:
            problems.append(f"torque {tq:.3f} N*m > {TORQUE_USE:.0%} of stall")
        for p in problems:
            print(f"FAIL {name}: {p}")
        ok = ok and not problems
    return ok


def glove_rows(step=5):
    """Pot (crank) angle and transmission over the operator's MCP flexion."""
    k0 = GLOVE_POT + GLOVE_CRANK * cmath.exp(1j * math.radians(GLOVE_BETA0))
    link = abs(k0 - GLOVE_RING)
    beta, rows = GLOVE_BETA0, []
    for t in range(0, int(GLOVE_TMAX) + 1, step):
        r = rot(t) * GLOVE_RING
        f = lambda b: abs(GLOVE_POT + GLOVE_CRANK * cmath.exp(1j * math.radians(b)) - r) - link
        beta = solve(f, beta, beta - 179.0, beta + 179.0, step=0.25)
        if beta is None:
            return link, None
        k = GLOVE_POT + GLOVE_CRANK * cmath.exp(1j * math.radians(beta))
        a1, a2 = angle_between(k - GLOVE_POT, r - k), angle_between(r, r - k)
        rows.append((float(t), beta, min(a1, 180 - a1, a2, 180 - a2)))
    return link, rows


def check_glove(verbose=True):
    link, rows = glove_rows()
    if rows is None:
        print("FAIL glove: linkage locks")
        return False
    betas = [r[1] for r in rows]
    sweep = betas[-1] - betas[0]
    mono = all((b - a) * sweep > 0 for a, b in zip(betas, betas[1:]))
    trans = min(r[2] for r in rows)
    if verbose:
        print(f"\n== glove: crank {GLOVE_CRANK:.0f}  link {link:.2f} mm  ring pin {GLOVE_RING.real:.0f},{GLOVE_RING.imag:.0f}")
        print("   MCP    pot    trans")
        for t, b, tr in rows[::3]:
            print(f"  {t:4.0f}  {b - betas[0]:6.1f}  {tr:5.1f}")
        print(f"   pot travel {abs(sweep):.0f} deg = {abs(sweep) / POT_TRAVEL * 1023:.0f} ADC counts, "
              f"min transmission {trans:.0f} deg")
    problems = []
    if not mono:
        problems.append("pot angle not monotonic")
    if abs(sweep) < GLOVE_SWEEP_MIN:
        problems.append(f"pot travel {abs(sweep):.0f} < {GLOVE_SWEEP_MIN:.0f}")
    if abs(sweep) > POT_TRAVEL - 40:
        problems.append("pot travel too close to the end stops")
    if trans < TRANS_MIN:
        problems.append(f"transmission {trans:.0f} < {TRANS_MIN:.0f}")
    for p in problems:
        print(f"FAIL glove: {p}")
    return not problems


def write_scad(path):
    lines = [
        "// GENERATED by tools/finger_linkage.py --scad. Do not edit: change the script and re-run.",
        f"C_OFF = [{C_OFF.real}, {C_OFF.imag}];   // coupling pivot on M (behind PIP, dorsal)",
        f"G_OFF = [{G_OFF.real}, {G_OFF.imag}];   // coupling pivot on the palm (behind MCP, palmar)",
        f"D_R = {D_R};",
        f"HORN_R = {HORN_R};",
        f"SERVO_Z = {SERVO_Z};",
        f"LAYER_Y = {LAYER_Y};",
    ]
    lines.append("// Per finger: [L1, L2, coupling link, drive rod, layer, drive-pin angle, horn angle when straight]")
    lines.append("// Angles in the finger plane, degrees from +z towards +y (dorsal).")
    for name, (l1, l2, layer) in FINGERS.items():
        f = Finger(name, l1, l2, layer)
        if layer == "direct":
            rod, d_ang, phi0 = "0", "0", "0"
        else:
            rod, d_ang, phi0 = f"{f.rod:.2f}", f"{math.degrees(cmath.phase(f.D0)):.1f}", f"{f.phi0:.0f}"
        lines.append(f'FINGER_{name.upper()} = [{l1}, {l2}, {f.link:.2f}, {rod}, "{layer}", {d_ang}, {phi0}];')
    link, _ = glove_rows()
    lines += [
        "// Glove: operator's finger plane, origin on the operator's MCP axis ([z, y]).",
        f"GLOVE_RING = [{GLOVE_RING.real}, {GLOVE_RING.imag}];",
        f"GLOVE_POT = [{GLOVE_POT.real}, {GLOVE_POT.imag}];",
        f"GLOVE_CRANK = {GLOVE_CRANK};",
        f"GLOVE_LINK = {link:.2f};",
    ]
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines) + "\n")
    print(f"wrote {path}")


if __name__ == "__main__":
    good = check()
    good = check_glove() and good
    if "--scad" in sys.argv:
        write_scad(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "cad", "linkage_params.scad"))
    print("\nOK" if good else "\nLINKAGE CHECK FAILED")
    sys.exit(0 if good else 1)
