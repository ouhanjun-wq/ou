// Text commands: one command per line, from the USB serial port, an offline voice module
// (UART2), the phone web page or a PC / AI agent (tools/arm_client.py).
// Units: mm and degrees; gripper 0 = open .. 100 = closed.
//
//   ON | OFF (= PARK) | HOME | STOP | MODE JOINT|CART | SPEED 1..3
//   J <1..6> <deg>            JOINTS <j1> <j2> <j3> <j4> <j5> [grip]
//   MOVE <x> <y> <z> [pitch [roll]]      MOVEBY <dx> <dy> <dz>
//   UP | DOWN | LEFT | RIGHT | FORWARD | BACK [mm, default 20]
//   TURN <deg> (base, relative) | PITCH <deg> | ROLL <deg> (tool, absolute)
//   GRIP <0..100> | OPEN | CLOSE
//   REC | DELETE | CLEAR | PLAY [LOOP] | SAVE | WHERE | STATUS
#pragma once
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "motion.h"

namespace arm {

inline bool parseNum(const char* s, float& out) {
  char* end = nullptr;
  out = strtof(s, &end);
  return end != s && *end == '\0';
}

inline void statusLine(const ArmCore& c, const Sensors& s, char* out, size_t n) {
  const kin::Pose p = c.pose();
  int k = snprintf(out, n, "%s %s %s speed %d | J %.1f %.1f %.1f %.1f %.1f grip %.0f | XYZ %.0f %.0f %.0f pitch %.0f"
                   " roll %.0f | wp %d",
                   stateName(c.state), activityName(c.activity), c.mode == CART_MODE ? "CART" : "JOINT", c.speedLvl,
                   c.q[0], c.q[1], c.q[2], c.q[3], c.q[4], c.q[5], p.x, p.y, p.z, p.pitch, p.roll, c.seqLen);
  if (s.ok && k > 0 && (size_t)k < n) snprintf(out + k, n - k, " | %.2f A %.2f V", s.amps, s.volts);
}

// Returns false if the first word is not a motion command (the caller may try its own).
// The line is modified (tokenised, upper-cased): pass a copy if the caller needs it again.
inline bool textCommand(ArmCore& c, const Sensors& sens, char* line, char* reply, size_t n) {
  char* argv[8];
  int argc = 0;
  for (char* tok = strtok(line, " \t,"); tok && argc < 8; tok = strtok(nullptr, " \t,")) argv[argc++] = tok;
  reply[0] = '\0';
  if (argc == 0) return true;
  for (int i = 0; i < argc; ++i)
    for (char* p = argv[i]; *p; ++p) *p = (char)toupper((unsigned char)*p);
  const char* cmd = argv[0];
  static const char* const kKnown[] = {"ON", "ENABLE", "OFF", "PARK", "DISABLE", "STOP", "HOME", "MODE", "SPEED",
                                       "J", "JOINTS", "MOVE", "MOVEBY", "UP", "DOWN", "LEFT", "RIGHT", "FORWARD",
                                       "BACK", "PITCH", "ROLL", "TURN", "GRIP", "OPEN", "CLOSE", "REC", "DELETE",
                                       "CLEAR", "PLAY", "SAVE", "WHERE", "STATUS"};
  bool known = false;
  for (const char* k : kKnown) known = known || strcmp(cmd, k) == 0;
  if (!known) return false;
  float num[7] = {0};
  int nnum = 0;
  bool numsOk = true;
  for (int i = 1; i < argc && nnum < 7; ++i) {
    if (parseNum(argv[i], num[nnum])) ++nnum;
    else if (i > 1 || (strcmp(cmd, "GRIP") && strcmp(cmd, "MODE") && strcmp(cmd, "PLAY") && strcmp(cmd, "SAVE"))) numsOk = false;
  }
  auto is = [&](const char* w) { return strcmp(cmd, w) == 0; };
  auto done = [&](const char* msg) { snprintf(reply, n, "%s", msg); return true; };
  auto needOn = [&]() { return c.state == ENABLED; };
  if (!numsOk) return done("error: bad number");
  const kin::Pose now = kin::forward(c.geo(), c.qt);

  auto goPose = [&](const kin::Pose& p) {
    if (!needOn()) return done("error: servo power is off (ON)");
    const char* err = c.movePose(p);
    if (err) { snprintf(reply, n, "error: %s", err); return true; }
    snprintf(reply, n, "ok move to %.0f %.0f %.0f pitch %.0f roll %.0f", p.x, p.y, p.z, p.pitch, p.roll);
    return true;
  };

  if (is("ON") || is("ENABLE")) { c.enable(); return done("ok servo power on"); }
  if (is("OFF") || is("PARK") || is("DISABLE")) {
    if (c.state == DISABLED) return done("ok already off");
    c.park();
    return done("ok parking, then power off");
  }
  if (is("STOP")) { c.stop(); return done("ok stopped"); }
  if (is("HOME")) {
    if (!needOn()) return done("error: servo power is off (ON)");
    c.home();
    return done("ok home");
  }
  if (is("MODE") && argc == 2) {
    if (!strcmp(argv[1], "JOINT")) c.setMode(JOINT_MODE);
    else if (!strcmp(argv[1], "CART") || !strcmp(argv[1], "XYZ")) c.setMode(CART_MODE);
    else return done("error: MODE JOINT|CART");
    return done(c.mode == CART_MODE ? "ok mode CART" : "ok mode JOINT");
  }
  if (is("SPEED") && nnum == 1) {
    if (num[0] < 1 || num[0] > 3) return done("error: SPEED 1..3");
    c.speedLvl = (int)num[0];
    snprintf(reply, n, "ok speed %d", c.speedLvl);
    return true;
  }
  if (is("J") && nnum == 2) {
    const int j = (int)num[0] - 1;
    if (j < 0 || j >= NJ) return done("error: joint 1..6");
    if (!needOn()) return done("error: servo power is off (ON)");
    float goal[NJ];
    memcpy(goal, c.qt, sizeof(goal));
    goal[j] = num[1];
    if (!c.moveJoints(goal)) {
      snprintf(reply, n, "error: J%d limit %.0f..%.0f", j + 1, c.P.qmin[j], c.P.qmax[j]);
      return true;
    }
    snprintf(reply, n, "ok J%d -> %.1f", j + 1, num[1]);
    return true;
  }
  if (is("JOINTS") && (nnum == 5 || nnum == 6)) {
    if (!needOn()) return done("error: servo power is off (ON)");
    float goal[NJ];
    memcpy(goal, num, sizeof(float) * NARM);
    goal[GRIP] = nnum == 6 ? num[5] : c.qt[GRIP];
    if (!c.moveJoints(goal)) return done("error: joint limit");
    return done("ok joints");
  }
  if (is("MOVE") && nnum >= 3) {
    kin::Pose p = now;
    p.x = num[0]; p.y = num[1]; p.z = num[2];
    if (nnum >= 4) p.pitch = num[3];
    if (nnum >= 5) p.roll = num[4];
    return goPose(p);
  }
  if (is("MOVEBY") && nnum == 3) {
    kin::Pose p = now;
    p.x += num[0]; p.y += num[1]; p.z += num[2];
    return goPose(p);
  }
  const char* dirs[6] = {"UP", "DOWN", "LEFT", "RIGHT", "FORWARD", "BACK"};
  for (int d = 0; d < 6; ++d) {
    if (!is(dirs[d])) continue;
    const float mm = nnum >= 1 ? num[0] : 20.0f;
    kin::Pose p = now;
    if (d == 0) p.z += mm;
    if (d == 1) p.z -= mm;
    if (d == 2) p.y += mm;
    if (d == 3) p.y -= mm;
    if (d == 4) p.x += mm;
    if (d == 5) p.x -= mm;
    // LEFT / RIGHT / FORWARD / BACK keep the height; they are relative to the arm's base frame.
    return goPose(p);
  }
  if (is("PITCH") && nnum == 1) { kin::Pose p = now; p.pitch = num[0]; return goPose(p); }
  if (is("ROLL") && nnum == 1) { kin::Pose p = now; p.roll = num[0]; return goPose(p); }
  if (is("TURN") && nnum == 1) {
    if (!needOn()) return done("error: servo power is off (ON)");
    float goal[NJ];
    memcpy(goal, c.qt, sizeof(goal));
    goal[0] += num[0];
    if (!c.moveJoints(goal)) return done("error: J1 limit");
    return done("ok turn");
  }
  if (is("GRIP") || is("OPEN") || is("CLOSE")) {
    float g;
    if (is("OPEN") || (argc == 2 && !strcmp(argv[1], "OPEN"))) g = c.P.qmin[GRIP];
    else if (is("CLOSE") || (argc == 2 && !strcmp(argv[1], "CLOSE"))) g = c.P.qmax[GRIP];
    else if (nnum == 1) g = num[0];
    else return done("error: GRIP OPEN|CLOSE|0..100");
    if (!needOn()) return done("error: servo power is off (ON)");
    float goal[NJ];
    memcpy(goal, c.qt, sizeof(goal));
    goal[GRIP] = fminf(fmaxf(g, c.P.qmin[GRIP]), c.P.qmax[GRIP]);
    c.moveJoints(goal);
    snprintf(reply, n, "ok grip %.0f", goal[GRIP]);
    return true;
  }
  if (is("REC")) {
    if (!c.record()) return done(c.state != ENABLED ? "error: servo power is off (ON)" : "error: list full");
    snprintf(reply, n, "ok waypoint %d", c.seqLen);
    return true;
  }
  if (is("DELETE")) {
    if (c.seqLen > 0) --c.seqLen;
    snprintf(reply, n, "ok %d waypoints", c.seqLen);
    return true;
  }
  if (is("CLEAR")) { c.seqLen = 0; return done("ok waypoints cleared"); }
  if (is("PLAY")) {
    const bool lp = argc == 2 && !strcmp(argv[1], "LOOP");
    if (!c.play(lp)) return done(c.seqLen == 0 ? "error: no waypoints" : "error: servo power is off (ON)");
    return done(lp ? "ok playing (loop)" : "ok playing");
  }
  if (is("SAVE") && argc == 1) { c.saveRequest = true; return done("ok saving waypoints"); }
  if (is("WHERE") || is("STATUS")) { statusLine(c, sens, reply, n); return true; }
  return false;
}

}  // namespace arm
