// Arm behaviour: gamepad jogging (joint or Cartesian), smooth moves, teach & playback,
// grip detection, over-current and emergency-stop handling.
// Header-only, no Arduino code: the whole state machine is unit-tested on the PC
// (firmware/tests/test_core.cpp). The .ino feeds it gamepad + current sensor data at 50 Hz
// and writes q[] to the servos.
#pragma once
#include <math.h>
#include <string.h>

#include "kinematics.h"
#include "params.h"

namespace arm {

enum State : uint8_t { DISABLED, ENABLED, OVERLOAD, ESTOP, FAULT };
enum Activity : uint8_t { IDLE, MOVE, PLAY, PARKING };
enum Mode : uint8_t { JOINT_MODE, CART_MODE };

constexpr int MAX_WP = 64;
constexpr float OVERRIDE = 0.25f;     // stick deflection that cancels a move / playback
constexpr float LONG_A_S = 2.0f;      // hold A: clear all waypoints
constexpr float LONG_S = 1.0f;        // hold X: loop playback, hold D-pad up: save
constexpr float SPEED_SCALE[3] = {0.25f, 0.5f, 1.0f};

// Gamepad, already scaled: sticks -1..1 (ly / ry + = pushed up), triggers 0..1.
struct Input {
  bool valid = false;
  float lx = 0, ly = 0, rx = 0, ry = 0, lt = 0, rt = 0;
  bool a = false, b = false, x = false, y = false, lb = false, rb = false;
  bool up = false, down = false, left = false, right = false, view = false, menu = false;
};

// INA226 on the servo supply (after the relay). ok = false: no sensor, protection off.
struct Sensors {
  bool ok = false;
  float amps = 0, volts = 0;
};

struct Waypoint {
  float q[NJ];
};

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Stick dead zone + expo.
inline float shape(float v, float deadband, float expo) {
  const float a = fabsf(v);
  if (a <= deadband) return 0;
  float d = clampf((a - deadband) / (1.0f - deadband), 0.0f, 1.0f);
  d = (1.0f - expo) * d + expo * d * d * d;
  return v > 0 ? d : -d;
}

// Quintic 0..1 with zero speed and acceleration at both ends.
inline float quintic(float u) {
  u = clampf(u, 0.0f, 1.0f);
  return u * u * u * (10.0f + u * (-15.0f + 6.0f * u));
}

// Duration of a synchronised quintic move: every joint stays inside its speed and
// acceleration limit (peak speed 1.875 d/T, peak acceleration 5.7735 d/T^2).
inline float moveTime(const Params& P, const float* from, const float* to, float scale) {
  float T = 0.3f;
  for (int j = 0; j < NJ; ++j) {
    const float d = fabsf(to[j] - from[j]);
    const float v = P.vmax[j] * scale, a = P.amax[j] * scale;
    const float tj = fmaxf(1.875f * d / v, sqrtf(5.7735f * d / a));
    if (tj > T) T = tj;
  }
  return T;
}

struct Button {
  bool prev = false, longFired = false;
  float held = 0;
  bool pressed = false, shortRelease = false, longHit = false;
  void update(bool now, float dt, float longS) {
    pressed = now && !prev;
    shortRelease = !now && prev && !longFired;
    longHit = false;
    if (now) {
      held = pressed ? 0 : held + dt;
      if (!longFired && held >= longS) { longFired = true; longHit = true; }
    } else {
      held = 0;
      longFired = false;
    }
    prev = now;
  }
};

struct ArmCore {
  Params& P;
  explicit ArmCore(Params& p) : P(p) { reset(); }

  // ---- outputs (read after update) ----
  float q[NJ];                 // commanded angles sent to the servos
  bool power = false;          // servo power relay
  uint16_t rumbleMs = 0;       // gamepad feedback for this tick (0 = none)
  uint8_t beeps = 0;           // buzzer feedback for this tick
  const char* event = nullptr; // one-line log message for this tick
  bool saveRequest = false;    // .ino writes the waypoints to flash and clears this

  // ---- state ----
  State state = DISABLED;
  Activity activity = IDLE;
  Mode mode = JOINT_MODE;
  int speedLvl = 2;            // 1..3
  float qt[NJ], v[NJ];         // target and tracker speed
  kin::Pose pt;                // Cartesian target (CART mode)
  Waypoint seq[MAX_WP];
  int seqLen = 0, playIdx = 0;
  bool loop = false;
  float segFrom[NJ], segTo[NJ], segT = 0, segDur = 0, dwellT = 0;
  bool dwelling = false;
  float gripLimit = 100;       // gripper may not close past this (after a grip was detected)
  float iLp = 0, iBase = 0, stallT = 0, overT = 0, faultT = 0, powerT = 0, lowT = 0, lowWarnT = 0;
  bool gripTracking = false;
  float t = 0, lastLimitT = -10;
  Button bA, bB, bX, bY, bLB, bRB, bUp, bDown, bView, bMenu;

  kin::Geometry geo() const { return {P.d1, P.l2, P.l3, P.l4}; }
  float speed() const { return SPEED_SCALE[speedLvl - 1]; }
  kin::Pose pose() const { return kin::forward(geo(), q); }

  void reset() {
    for (int j = 0; j < NJ; ++j) { q[j] = qt[j] = P.park[j]; v[j] = 0; }
    state = DISABLED;
    activity = IDLE;
    power = false;
    speedLvl = (int)clampf(P.speed_lvl, 1, 3);
    gripLimit = P.qmax[GRIP];
    pt = kin::forward(geo(), qt);
  }

  void note(const char* msg, uint16_t rumble = 0, uint8_t beep = 0) {
    event = msg;
    if (rumble > rumbleMs) rumbleMs = rumble;
    if (beep > beeps) beeps = beep;
  }

  bool inLimits(const float* qq) const {
    for (int j = 0; j < NJ; ++j)
      if (qq[j] < P.qmin[j] - 1e-3f || qq[j] > P.qmax[j] + 1e-3f) return false;
    return true;
  }

  // Pose -> joint angles J1..J5 with workspace and joint-limit checks.
  const char* solve(const kin::Pose& p, float* out) const {
    if (p.z < P.z_min) return "below z_min (table)";
    float s[NARM];
    const kin::Result r = kin::inverse(geo(), p, P.r_min, s);
    if (r == kin::UNREACHABLE) return "out of reach";
    if (r == kin::TOO_CLOSE) return "too close to the base";
    for (int j = 0; j < NARM; ++j)
      if (s[j] < P.qmin[j] - 1e-3f || s[j] > P.qmax[j] + 1e-3f) return "joint limit";
    for (int j = 0; j < NARM; ++j) out[j] = s[j];
    return nullptr;
  }

  void holdHere() {
    for (int j = 0; j < NJ; ++j) qt[j] = q[j];
    pt = kin::forward(geo(), qt);
    activity = IDLE;
    dwelling = false;
  }

  // ---------------- commands (gamepad buttons and text) ----------------
  bool enable() {
    if (state == ENABLED || state == OVERLOAD) return true;
    for (int j = 0; j < NJ; ++j) { q[j] = qt[j] = P.park[j]; v[j] = 0; }
    state = ENABLED;
    activity = IDLE;
    power = true;
    powerT = lowT = overT = faultT = stallT = 0;
    gripLimit = P.qmax[GRIP];
    pt = kin::forward(geo(), qt);
    note("servo power ON (arm goes to the park pose)", 150, 1);
    return true;
  }

  void powerOff(State s, const char* why) {
    power = false;
    state = s;
    activity = IDLE;
    for (int j = 0; j < NJ; ++j) { q[j] = qt[j] = P.park[j]; v[j] = 0; }
    pt = kin::forward(geo(), qt);
    note(why, s == DISABLED ? 300 : 800, s == DISABLED ? 2 : 5);
  }

  void startMove(const float* goal, Activity act) {
    for (int j = 0; j < NJ; ++j) {
      segFrom[j] = q[j];
      segTo[j] = clampf(goal[j], P.qmin[j], P.qmax[j]);
    }
    segDur = moveTime(P, segFrom, segTo, speed());
    segT = 0;
    dwelling = false;
    activity = act;
  }

  void park() {
    if (state == OVERLOAD) { powerOff(DISABLED, "servo power OFF"); return; }
    if (state != ENABLED) return;
    startMove(P.park, PARKING);
    note("parking, then servo power off", 150);
  }

  void stop() {
    if (state == OVERLOAD) { state = ENABLED; overT = faultT = 0; }
    if (state != ENABLED) return;
    for (int j = 0; j < NJ; ++j) v[j] = 0;
    holdHere();
    note("stop", 120);
  }

  bool moveJoints(const float* goal) {
    if (state != ENABLED || !inLimits(goal)) return false;
    startMove(goal, MOVE);
    return true;
  }

  const char* movePose(const kin::Pose& p) {
    if (state != ENABLED) return "not enabled";
    float goal[NJ];
    const char* err = solve(p, goal);
    if (err) return err;
    goal[GRIP] = qt[GRIP];
    startMove(goal, MOVE);
    return nullptr;
  }

  void home() { if (state == ENABLED) startMove(P.home, MOVE); }

  bool record() {
    if (state != ENABLED || seqLen >= MAX_WP) return false;
    memcpy(seq[seqLen].q, qt, sizeof(qt));
    ++seqLen;
    return true;
  }

  bool play(bool loopIt) {
    if (state != ENABLED || seqLen == 0) return false;
    loop = loopIt;
    playIdx = 0;
    startMove(seq[0].q, PLAY);
    return true;
  }

  void setMode(Mode m) {
    mode = m;
    pt = kin::forward(geo(), qt);
  }

  // Calibration helper: joint j now physically at angle a (after raw pulse tests).
  void syncJoint(int j, float a) {
    q[j] = qt[j] = clampf(a, P.qmin[j], P.qmax[j]);
    v[j] = 0;
    pt = kin::forward(geo(), qt);
  }

  // ---------------- main loop ----------------
  void update(const Input& in, const Sensors& s, float dt) {
    t += dt;
    rumbleMs = 0;
    beeps = 0;
    event = nullptr;
    sense(s, dt);
    buttons(in, dt);

    if (state == ENABLED) {
      const float sp = speed();
      Input sh = in;
      sh.lx = shape(in.lx, P.deadband, P.expo);
      sh.ly = shape(in.ly, P.deadband, P.expo);
      sh.rx = shape(in.rx, P.deadband, P.expo);
      sh.ry = shape(in.ry, P.deadband, P.expo);
      sh.lt = in.lt < 0.05f ? 0 : in.lt;
      sh.rt = in.rt < 0.05f ? 0 : in.rt;
      if (!in.valid) sh = Input();
      if (activity != IDLE && touched(sh)) {
        holdHere();
        note("manual override", 100);
      }
      if (activity == IDLE) jog(sh, sp, dt);
      else runActivity(dt);
    }

    gripGuard(s, dt);
    if (power) track(dt);
    if (activity == PARKING && !dwelling && segT >= segDur && settled())
      powerOff(DISABLED, "parked, servo power OFF");
  }

  bool touched(const Input& sh) const {
    return fabsf(sh.lx) > OVERRIDE || fabsf(sh.ly) > OVERRIDE || fabsf(sh.rx) > OVERRIDE ||
           fabsf(sh.ry) > OVERRIDE || sh.lt > 0.1f || sh.rt > 0.1f || sh.left || sh.right;
  }

  bool settled() const {
    for (int j = 0; j < NJ; ++j)
      if (fabsf(q[j] - qt[j]) > 0.5f) return false;
    return true;
  }

  void sense(const Sensors& s, float dt) {
    if (!s.ok || !power) {
      iLp = 0;
      overT = faultT = lowT = 0;
      return;
    }
    iLp += (s.amps - iLp) * clampf(dt / 0.05f, 0, 1);
    powerT += dt;
    // Emergency stop: the mushroom switch cut the servo supply while the relay is on.
    if (powerT > 0.3f && s.volts < P.estop_v) {
      lowT += dt;
      if (lowT >= 0.1f) { powerOff(ESTOP, "EMERGENCY STOP: servo supply lost, relay off"); return; }
    } else {
      lowT = 0;
    }
    if (powerT > 0.3f && s.volts >= P.estop_v && s.volts < P.low_v && t - lowWarnT > 10) {
      lowWarnT = t;
      note("warning: servo supply voltage low");
    }
    // Over-current: freeze, then cut the power if it does not recover.
    if (iLp > P.over_amps) {
      if (state == ENABLED) {
        overT += dt;
        if (overT >= P.over_s) {
          state = OVERLOAD;
          for (int j = 0; j < NJ; ++j) v[j] = 0;
          holdHere();
          faultT = 0;
          note("OVERLOAD: motion frozen (B = resume, move away from the obstacle)", 600, 3);
        }
      } else if (state == OVERLOAD) {
        faultT += dt;
        if (faultT >= P.fault_s) powerOff(FAULT, "FAULT: over-current did not stop, servo power OFF");
      }
    } else {
      overT = 0;
      faultT = 0;
    }
  }

  void buttons(const Input& in, float dt) {
    Input b = in.valid ? in : Input();
    bA.update(b.a, dt, LONG_A_S);
    bB.update(b.b, dt, 1e9f);
    bX.update(b.x, dt, LONG_S);
    bY.update(b.y, dt, 1e9f);
    bLB.update(b.lb, dt, 1e9f);
    bRB.update(b.rb, dt, 1e9f);
    bUp.update(b.up, dt, LONG_S);
    bDown.update(b.down, dt, 1e9f);
    bView.update(b.view, dt, 1e9f);
    bMenu.update(b.menu, dt, 1e9f);
    if (!in.valid) return;

    if (bMenu.pressed) {
      if (state == ENABLED) park();
      else if (state == OVERLOAD) powerOff(DISABLED, "servo power OFF");
      else enable();
    }
    if (bB.pressed) stop();
    if (bLB.pressed && speedLvl > 1) { --speedLvl; note("speed -", 80); }
    if (bRB.pressed && speedLvl < 3) { ++speedLvl; note("speed +", 80); }
    if (state != ENABLED) return;

    if (bView.pressed) {
      setMode(mode == JOINT_MODE ? CART_MODE : JOINT_MODE);
      note(mode == CART_MODE ? "mode: CARTESIAN (XYZ)" : "mode: JOINT", 200);
    }
    if (bY.pressed) { home(); note("home", 100); }
    if (activity == IDLE && bA.shortRelease) {
      if (record()) note("waypoint recorded", 100, 1);
      else note("waypoint list full", 500);
    }
    if (bA.longHit) { seqLen = 0; note("all waypoints cleared", 500, 2); }
    if (bX.shortRelease && !play(false)) note("no waypoints recorded", 500);
    if (bX.longHit && play(true)) note("playback: loop", 200);
    if (bDown.pressed && seqLen > 0) { --seqLen; note("last waypoint deleted", 150); }
    if (bUp.longHit) { saveRequest = true; note("waypoints saved", 300, 1); }
  }

  void jog(const Input& in, float sp, float dt) {
    // gripper: RT closes, LT opens (never slower than half speed)
    const float g = (in.rt - in.lt) * P.vmax[GRIP] * fmaxf(sp, 0.5f);
    qt[GRIP] = clampf(qt[GRIP] + g * dt, P.qmin[GRIP], fminf(P.qmax[GRIP], gripLimit));
    const float roll = (in.right ? 1.0f : 0.0f) - (in.left ? 1.0f : 0.0f);

    if (mode == JOINT_MODE) {
      const float rate[NARM] = {-in.lx, in.ly, in.ry, -in.rx, roll};
      bool hit = false;
      for (int j = 0; j < NARM; ++j) {
        if (rate[j] == 0) continue;
        const float n = qt[j] + rate[j] * P.vmax[j] * sp * dt;
        qt[j] = clampf(n, P.qmin[j], P.qmax[j]);
        if (qt[j] != n) hit = true;
      }
      if (hit) limitFeedback();
      pt = kin::forward(geo(), qt);
      return;
    }

    // Cartesian: sticks move the tool point in straight lines.
    const float d[5] = {in.ly * P.v_lin * sp * dt, -in.lx * P.v_lin * sp * dt, in.ry * P.v_lin * sp * dt,
                        -in.rx * P.v_ang * sp * dt, roll * P.v_ang * sp * dt};
    if (d[0] == 0 && d[1] == 0 && d[2] == 0 && d[3] == 0 && d[4] == 0) return;
    kin::Pose c = pt;
    c.x += d[0]; c.y += d[1]; c.z += d[2]; c.pitch += d[3]; c.roll += d[4];
    float sol[NARM];
    if (!solve(c, sol) && nearTarget(sol)) {
      pt = c;
      for (int j = 0; j < NARM; ++j) qt[j] = sol[j];
      return;
    }
    // Blocked: keep the axes that still work, so the tool slides along the boundary.
    for (int k = 0; k < 5; ++k) {
      if (d[k] == 0) continue;
      kin::Pose o = pt;
      float* f[5] = {&o.x, &o.y, &o.z, &o.pitch, &o.roll};
      *f[k] += d[k];
      if (!solve(o, sol) && nearTarget(sol)) {
        pt = o;
        for (int j = 0; j < NARM; ++j) qt[j] = sol[j];
      }
    }
    limitFeedback();
  }

  void limitFeedback() {
    if (t - lastLimitT > 0.6f) { lastLimitT = t; note("limit", 120); }
  }

  // A Cartesian step must not flip the arm into another configuration.
  bool nearTarget(const float* sol) const {
    for (int j = 0; j < NARM; ++j)
      if (fabsf(sol[j] - qt[j]) > 15.0f) return false;
    return true;
  }

  void runActivity(float dt) {
    if (dwelling) {
      dwellT += dt;
      if (dwellT < P.dwell_s) return;
      dwelling = false;
      if (++playIdx >= seqLen) {
        if (!loop) { holdHere(); note("playback done", 200, 1); return; }
        playIdx = 0;
      }
      startMove(seq[playIdx].q, PLAY);
      return;
    }
    segT += dt;
    const float s = quintic(segDur > 0 ? segT / segDur : 1);
    for (int j = 0; j < NJ; ++j) qt[j] = segFrom[j] + (segTo[j] - segFrom[j]) * s;
    if (segT < segDur) return;
    if (activity == MOVE) { holdHere(); return; }
    if (activity == PLAY) { dwelling = true; dwellT = 0; }
    // PARKING finishes in update() once the tracker has arrived
  }

  // Per-joint speed / acceleration limiter (time-optimal, no overshoot).
  void track(float dt) {
    for (int j = 0; j < NJ; ++j) {
      const float e = qt[j] - q[j];
      const float a = P.amax[j];
      const float vd = copysignf(fminf(P.vmax[j], sqrtf(2.0f * a * fabsf(e))), e);
      v[j] += clampf(vd - v[j], -a * dt, a * dt);
      const float step = v[j] * dt;
      if (fabsf(step) >= fabsf(e) || e == 0) {
        v[j] = clampf(e / dt, -P.vmax[j], P.vmax[j]);
        q[j] = qt[j];
      } else {
        q[j] += step;
      }
    }
  }

  // Gripper: stop closing when the current jumps (object held), then back off a little.
  void gripGuard(const Sensors& s, float dt) {
    if (qt[GRIP] > gripLimit) qt[GRIP] = gripLimit;
    if (qt[GRIP] < gripLimit - 5.0f) gripLimit = P.qmax[GRIP];   // opening again: forget the grip
    if (!s.ok || !power || state != ENABLED) { gripTracking = false; return; }
    bool armStill = true;
    for (int j = 0; j < NARM; ++j)
      if (fabsf(v[j]) > 10.0f) armStill = false;
    const bool closing = v[GRIP] > 1.0f && qt[GRIP] > q[GRIP] - 0.01f;
    if (!closing || !armStill) {
      gripTracking = false;
      stallT = 0;
      return;
    }
    if (!gripTracking) { gripTracking = true; iBase = iLp; stallT = 0; }
    if (iLp - iBase > P.grip_amps) {
      stallT += dt;
      if (stallT >= 0.15f) {
        gripLimit = clampf(q[GRIP] - P.grip_backoff, P.qmin[GRIP], P.qmax[GRIP]);
        qt[GRIP] = gripLimit;
        v[GRIP] = 0;
        gripTracking = false;
        note("object gripped", 150, 1);
      }
    } else {
      stallT = 0;
    }
  }
};

inline const char* stateName(State s) {
  switch (s) {
    case DISABLED: return "OFF";
    case ENABLED: return "ON";
    case OVERLOAD: return "OVERLOAD";
    case ESTOP: return "E-STOP";
    default: return "FAULT";
  }
}

inline const char* activityName(Activity a) {
  switch (a) {
    case MOVE: return "MOVE";
    case PLAY: return "PLAY";
    case PARKING: return "PARKING";
    default: return "IDLE";
  }
}

}  // namespace arm
