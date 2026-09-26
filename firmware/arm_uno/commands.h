// Text commands: one per line on the serial port (PC / AI over USB, or an offline voice
// module on RX). Upper or lower case. Units: mm and degrees; gripper 0 = open .. 100 = closed.
//
// Motion:  ON | OFF | HOME | STOP | MODE JOINT|XYZ | SPEED 1..3 | J <1..6> <deg>
//          MOVE x y z [pitch] | UP | DOWN | LEFT | RIGHT | FORWARD | BACK [mm]
//          GRIP 0..100 | OPEN | CLOSE | REC | CLEAR | PLAY [LOOP] | STATUS | HELP
// Setup (only in the calibration build, SETUP_MODE 1 in config.h, because the Uno's 32 KB of
// flash cannot hold them next to the gamepad library):
//          CENTER (all servos 1500 us, before ON) | PULSE j us | PULSE OFF | MARK j deg
//          LIM j min max | GEO d1 l2 l3 l4
//          POSE HOME|PARK (store the current angles) | SHOW | SAVE | DEFAULTS
#pragma once

#ifndef ARM_SETUP_COMMANDS
#define ARM_SETUP_COMMANDS 1
#endif
#include <ctype.h>
#include <string.h>

#include "motion.h"
#include "pca9685.h"
#include "pgm.h"

namespace arm {

// Called by SAVE; set by the sketch (EEPROM) or the tests.
typedef bool (*SaveFn)(const Params& p);

ARM_NOINLINE inline void okLine(Out& o, const char* pgm) {
  o.text(PSTR("ok "));
  o.text(pgm);
  o.end();
}

ARM_NOINLINE inline void errLine(Out& o, const char* pgm) {
  o.text(PSTR("error: "));
  o.text(pgm);
  o.end();
}

ARM_NOINLINE inline void numbers(Out& o, const float* v, int n) {
  for (int i = 0; i < n; ++i) {
    o.text(PSTR(" "));
    o.num(lroundf(v[i]));
  }
}

ARM_NOINLINE inline void statusLines(const ArmCore& c, const Sensors& s, Out& o) {
  o.text(stateName(c.state));
  o.text(PSTR(" "));
  o.text(activityName(c.activity));
  o.text(c.mode == CART_MODE ? PSTR(" XYZ speed ") : PSTR(" JOINT speed "));
  o.num(c.speedLvl);
  o.text(PSTR(" | waypoints "));
  o.num(c.wp.count());
  o.text(PSTR(" | "));
  o.dec(s.volts, 2);
  o.text(PSTR(" V"));
  o.end();
  const kin::Pose p = c.pose();
  const float xyz[5] = {p.x, p.y, p.z, p.pitch, p.roll};
  o.text(PSTR("J"));
  numbers(o, c.q, NJ);
  o.text(PSTR(" | XYZ pitch roll"));
  numbers(o, xyz, 5);
  o.end();
}

#if ARM_SETUP_COMMANDS
ARM_NOINLINE inline void showParams(const Params& P, Out& o) {
  for (int j = 0; j < NJ; ++j) {
    o.text(PSTR("J"));
    o.num(j + 1);
    o.text(PSTR(" us0 "));
    o.dec(P.us0[j], 1);
    o.text(PSTR(" usdeg "));
    o.dec(P.usdeg[j], 3);
    o.text(PSTR(" lim "));
    o.num(P.qmin[j]);
    o.text(PSTR(" "));
    o.num(P.qmax[j]);
    o.text(PSTR(" home "));
    o.num(P.home[j]);
    o.text(PSTR(" park "));
    o.num(P.park[j]);
    o.end();
  }
  const float g[4] = {(float)P.d1, (float)P.l2, (float)P.l3, (float)P.l4};
  o.text(PSTR("GEO"));
  numbers(o, g, 4);
  o.end();
}

#endif

// Executes one line. The line is modified (tokenised, upper-cased).
ARM_NOINLINE inline void runCommand(ArmCore& c, const Sensors& sens, char* line, Out& o, SaveFn save) {
  char* argv[7];
  int argc = 0;
  for (char* tok = strtok(line, " \t,"); tok && argc < 7; tok = strtok(nullptr, " \t,")) argv[argc++] = tok;
  if (argc == 0) return;
  for (int i = 0; i < argc; ++i)
    for (char* p = argv[i]; *p; ++p) *p = (char)toupper((unsigned char)*p);
  const char* cmd = argv[0];
  float num[5] = {0};
  int nnum = 0;
  for (int i = 1; i < argc && nnum < 5; ++i)
    if (parseNum(argv[i], num[nnum])) ++nnum;
  Params& P = c.P;
  (void)save;
  const int j = (int)num[0] - 1;                 // joint number for J / PULSE / MARK / LIM
  const bool jOk = nnum >= 1 && j >= 0 && j < NJ;
  const bool on = c.state == ST_ON;
#define IS(w) (strcmp_P(cmd, PSTR(w)) == 0)
#define ARG1(w) (argc > 1 && strcmp_P(argv[1], PSTR(w)) == 0)

  // ---------------- work with the servos on or off ----------------
  if (IS("ON")) { c.enable(); okLine(o, PSTR("servos on")); return; }
  if (IS("OFF")) {
    if (on) { c.park(); okLine(o, PSTR("parking, then servos off")); }
    else { c.powerOff(ST_OFF, PSTR("servos OFF")); okLine(o, PSTR("off")); }
    return;
  }
  if (IS("STOP")) { c.stop(); okLine(o, PSTR("stopped")); return; }
  if (IS("STATUS")) { statusLines(c, sens, o); return; }
  if (IS("HELP")) {
    o.text(PSTR("ON OFF HOME STOP MODE JOINT|XYZ SPEED J MOVE UP DOWN LEFT RIGHT FORWARD BACK GRIP OPEN CLOSE REC "
                "CLEAR PLAY STATUS"));
#if ARM_SETUP_COMMANDS
    o.text(PSTR(" | setup: CENTER PULSE OSC MARK LIM GEO POSE SHOW SAVE DEFAULTS"));
#endif
    o.end();
    return;
  }
  if (IS("MODE")) {
    if (ARG1("JOINT")) c.setMode(JOINT_MODE);
    else if (ARG1("XYZ")) c.setMode(CART_MODE);
    else { errLine(o, PSTR("MODE JOINT|XYZ")); return; }
    okLine(o, PSTR("mode"));
    return;
  }
  if (IS("SPEED")) {
    if (nnum != 1 || num[0] < 1 || num[0] > 3) { errLine(o, PSTR("SPEED 1..3")); return; }
    c.speedLvl = (int)num[0];
    okLine(o, PSTR("speed"));
    return;
  }
  if (IS("CLEAR")) { c.wp.clear(); okLine(o, PSTR("waypoints cleared")); return; }
#if ARM_SETUP_COMMANDS
  if (IS("SHOW")) { showParams(P, o); return; }
  if (IS("SAVE")) {
    if (save && save(P)) okLine(o, PSTR("parameters saved"));
    else errLine(o, PSTR("save failed"));
    return;
  }
  if (IS("DEFAULTS")) {
    if (c.power) { errLine(o, PSTR("turn the servos OFF first")); return; }
    paramsDefaults(P);
    c.reset();
    okLine(o, PSTR("defaults loaded (SAVE to keep them)"));
    return;
  }
  if (IS("LIM")) {
    if (nnum != 3 || !jOk || num[1] >= num[2]) { errLine(o, PSTR("LIM j min max")); return; }
    P.qmin[j] = (int16_t)num[1];
    P.qmax[j] = (int16_t)num[2];
    okLine(o, PSTR("limits set"));
    return;
  }
  if (IS("GEO")) {
    if (nnum != 4 || num[1] < 10 || num[2] < 10) { errLine(o, PSTR("GEO d1 l2 l3 l4 (mm)")); return; }
    P.d1 = (int16_t)num[0];
    P.l2 = (int16_t)num[1];
    P.l3 = (int16_t)num[2];
    P.l4 = (int16_t)num[3];
    c.setMode(c.mode);                            // refresh the XYZ target
    okLine(o, PSTR("geometry set"));
    return;
  }
  if (IS("POSE")) {
    int16_t* dst = ARG1("HOME") ? P.home : (ARG1("PARK") ? P.park : nullptr);
    if (!dst) { errLine(o, PSTR("POSE HOME|PARK")); return; }
    for (int k = 0; k < NJ; ++k) dst[k] = (int16_t)lroundf(c.q[k]);
    okLine(o, PSTR("pose stored from the current angles (SAVE to keep it)"));
    return;
  }
  if (IS("MARK")) {
    if (nnum != 2 || !jOk) { errLine(o, PSTR("MARK j deg")); return; }
    const float a = num[1], us = c.pulseUs(j);
    const bool second = c.markSet[j] && fabsf(a - c.markAngle[j]) >= 20.0f;
    const float a0 = c.markAngle[j], us0 = c.markUs[j];
    c.markSet[j] = true;
    c.markAngle[j] = a;
    c.markUs[j] = us;
    if (!second) { okLine(o, PSTR("mark stored; move >= 20 deg away and MARK again")); return; }
    const float k = (us - us0) / (a - a0);
    if (fabsf(k) < 2.0f || fabsf(k) > 40.0f) { errLine(o, PSTR("slope not plausible, check both poses")); return; }
    P.usdeg[j] = k;
    P.us0[j] = us - k * a;
    if (c.rawUs[j] > 0) c.syncJoint(j, a);
    o.text(PSTR("ok calibrated: us0 "));
    o.dec(P.us0[j], 1);
    o.text(PSTR(" usdeg "));
    o.dec(P.usdeg[j], 3);
    o.end();
    return;
  }
  if (IS("CENTER")) {   // every servo at 1500 us = the centre pose used during assembly
    for (int k = 0; k < NJ; ++k) c.rawUs[k] = 1500;
    okLine(o, PSTR("all servos 1500 us (the assembly centre pose); ON, then PULSE / MARK"));
    return;
  }
  if (IS("OSC")) {      // OSC <Hz measured on a servo signal pin> corrects the PCA9685 clock
    const long k = nnum == 1 ? pca::oscFromFrameHz(num[0]) : 0;
    if (k < 20000 || k > 30000) { errLine(o, PSTR("OSC Hz (40..60, measured with the multimeter)")); return; }
    P.osc_khz = (uint16_t)k;
    o.text(PSTR("ok osc_khz "));
    o.num(k);
    o.text(PSTR(" (SAVE to keep it)"));
    o.end();
    return;
  }
  if (IS("PULSE")) {
    if (ARG1("OFF")) {
      for (int k = 0; k < NJ; ++k) {
        if (c.rawUs[k] <= 0) continue;
        if (fabsf(P.usdeg[k]) > 1e-3f) c.syncJoint(k, (c.rawUs[k] - P.us0[k]) / P.usdeg[k]);
        c.rawUs[k] = 0;
      }
      okLine(o, PSTR("raw pulses off"));
      return;
    }
    if (nnum != 2 || !jOk || num[1] < 400 || num[1] > 2700) {
      errLine(o, PSTR("PULSE j 400..2700 | PULSE OFF"));
      return;
    }
    if (!on || c.activity != ACT_IDLE) { errLine(o, PSTR("servos must be ON and idle")); return; }
    c.rawUs[j] = num[1];
    okLine(o, PSTR("raw pulse (hands off the sticks)"));
    return;
  }

#endif  // ARM_SETUP_COMMANDS

  // ---------------- everything below moves the arm ----------------
  if (!on) { errLine(o, PSTR("servos are off (ON)")); return; }
  if (IS("HOME")) { c.home(); okLine(o, PSTR("home")); return; }
  if (IS("REC")) {
    if (!c.record()) { errLine(o, PSTR("waypoint memory full")); return; }
    o.text(PSTR("ok waypoint "));
    o.num(c.wp.count());
    o.end();
    return;
  }
  if (IS("PLAY")) {
    if (!c.play(ARG1("LOOP"))) { errLine(o, PSTR("no waypoints")); return; }
    okLine(o, PSTR("playing"));
    return;
  }
  float goal[NJ];
  memcpy(goal, c.qt, sizeof(goal));
  if (IS("J")) {
    if (nnum != 2 || !jOk) { errLine(o, PSTR("J j deg")); return; }
    goal[j] = num[1];
    if (!c.moveJoints(goal)) { errLine(o, PSTR("joint limit")); return; }
    okLine(o, PSTR("joint"));
    return;
  }
  if (IS("GRIP") || IS("OPEN") || IS("CLOSE")) {
    if (IS("OPEN") || ARG1("OPEN")) goal[GRIP] = P.qmin[GRIP];
    else if (IS("CLOSE") || ARG1("CLOSE")) goal[GRIP] = P.qmax[GRIP];
    else if (nnum == 1) goal[GRIP] = clampf(num[0], P.qmin[GRIP], P.qmax[GRIP]);
    else { errLine(o, PSTR("GRIP OPEN|CLOSE|0..100")); return; }
    c.moveJoints(goal);
    okLine(o, PSTR("grip"));
    return;
  }
  kin::Pose p = kin::forward(c.geo(), c.qt);
  const float mm = nnum >= 1 ? num[0] : 20.0f;
  if (IS("MOVE")) {
    if (nnum < 3) { errLine(o, PSTR("MOVE x y z [pitch]")); return; }
    p.x = num[0];
    p.y = num[1];
    p.z = num[2];
    if (nnum >= 4) p.pitch = num[3];
  } else if (IS("UP")) {
    p.z += mm;
  } else if (IS("DOWN")) {
    p.z -= mm;
  } else if (IS("LEFT")) {
    p.y += mm;
  } else if (IS("RIGHT")) {
    p.y -= mm;
  } else if (IS("FORWARD")) {
    p.x += mm;
  } else if (IS("BACK")) {
    p.x -= mm;
  } else {
    errLine(o, PSTR("unknown command (HELP)"));
    return;
  }
  const char* err = c.movePose(p);
  if (err) { errLine(o, err); return; }
  const float xyz[4] = {p.x, p.y, p.z, p.pitch};
  o.text(PSTR("ok move to"));
  numbers(o, xyz, 4);
  o.end();
#undef IS
#undef ARG1
}

}  // namespace arm
