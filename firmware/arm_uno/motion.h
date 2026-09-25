// Arm behaviour: gamepad jogging (joint or XYZ), smooth moves, teach & playback,
// E-stop detection and calibration overrides.
// Header-only and free of Arduino code: the whole state machine is unit-tested on a PC
// (firmware/tests/test_core.cpp). The .ino feeds it the gamepad and the servo-supply
// voltage at 50 Hz and writes pulseUs(j) to the servos.
#pragma once
#include <math.h>
#include <string.h>

#include "kinematics.h"
#include "params.h"
#include "pgm.h"

namespace arm {

enum ArmState : uint8_t { ST_OFF, ST_ON, ST_ESTOP };
enum ArmActivity : uint8_t { ACT_IDLE, ACT_MOVE, ACT_PLAY, ACT_PARKING };
enum ArmMode : uint8_t { JOINT_MODE, CART_MODE };

constexpr float OVERRIDE = 0.25f;     // stick deflection that cancels a move / playback
constexpr float LONG_A_S = 2.0f;      // hold A: clear all waypoints
constexpr float LONG_X_S = 1.0f;      // hold X: loop playback

inline float speedScale(int lvl) { return lvl <= 1 ? 0.25f : (lvl == 2 ? 0.5f : 1.0f); }

// Gamepad, already scaled: sticks -1..1 (ly / ry + = pushed up), triggers 0..1.
enum PadButton : uint8_t { PB_A, PB_B, PB_X, PB_Y, PB_LB, PB_RB, PB_UP, PB_DOWN, PB_LEFT, PB_RIGHT, PB_VIEW, PB_MENU,
                           PB_COUNT };
struct PadInput {
  bool valid = false;
  float lx = 0, ly = 0, rx = 0, ry = 0, lt = 0, rt = 0;
  bool btn[PB_COUNT] = {};
};

// Servo supply voltage after the E-stop switch (divider on A3). ok = false: not measured.
struct Sensors {
  bool ok = false;
  float volts = 0;
};

// Taught waypoints. On the Uno they live in EEPROM (the RAM is too small);
// the unit tests use an array.
class WaypointStore {
 public:
  virtual int count() const = 0;
  virtual int capacity() const = 0;
  virtual void get(int i, float* q) const = 0;
  virtual bool append(const float* q) = 0;
  virtual void removeLast() = 0;
  virtual void clear() = 0;
};

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Stick dead zone + expo (both in %).
ARM_NOINLINE inline float shape(float v, float deadbandPct, float expoPct) {
  const float db = deadbandPct / 100.0f, e = expoPct / 100.0f;
  const float a = fabsf(v);
  if (a <= db) return 0;
  float d = clampf((a - db) / (1.0f - db), 0.0f, 1.0f);
  d = (1.0f - e) * d + e * d * d * d;
  return v > 0 ? d : -d;
}

// Quintic 0..1 with zero speed and acceleration at both ends.
inline float quintic(float u) {
  u = clampf(u, 0.0f, 1.0f);
  return u * u * u * (10.0f + u * (-15.0f + 6.0f * u));
}

// Duration of a synchronised quintic move: every joint stays inside its speed and
// acceleration limit (peak speed 1.875 d/T, peak acceleration 5.7735 d/T^2).
ARM_NOINLINE inline float moveTime(const Params& P, const float* from, const float* to, float scale) {
  float T = 0.3f;
  for (int j = 0; j < NJ; ++j) {
    const float d = fabsf(to[j] - from[j]);
    const float v = P.vmax[j] * scale, a = P.amax[j] * scale;
    const float tj = fmaxf(1.875f * d / v, sqrtf(5.7735f * d / a));
    if (tj > T) T = tj;
  }
  return T;
}

struct BtnState {             // zero-initialised (global or BtnState())
  bool prev, longFired;
  float held;
  bool pressed, shortRelease, longHit;
  ARM_NOINLINE void update(bool now, float dt, float longS) {
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
  WaypointStore& wp;
  // Call reset() once the parameters are loaded.
  ArmCore(Params& p, WaypointStore& w) : P(p), wp(w) {}

  // ---- outputs (read after update) ----
  float q[NJ];                  // commanded angles
  bool power;           // servos attached (pulses on)
  uint16_t rumbleMs;        // gamepad feedback for this tick
  const char* event;  // one-line log message for this tick (PSTR)
  float rawUs[NJ];              // > 0: calibration pulse sent as-is (PULSE command)

  // ---- state ----
  ArmState state;
  ArmActivity activity;
  ArmMode mode;
  int speedLvl;
  float qt[NJ], v[NJ];
  kin::Pose pt;
  int playIdx;
  bool loop;
  float segFrom[NJ], segTo[NJ], segT, segDur, dwellT;
  bool dwelling;
  float t, lastLimitT, powerT, lowT;
  bool rtHeld;
  // calibration marks (MARK command)
  bool markSet[NJ];
  float markAngle[NJ], markUs[NJ];
  BtnState bA, bB, bX, bY, bLB, bRB, bDown, bView, bMenu;

  kin::Geometry geo() const { return {(float)P.d1, (float)P.l2, (float)P.l3, (float)P.l4}; }
  float speed() const { return speedScale(speedLvl); }
  kin::Pose pose() const { return kin::forward(geo(), q); }

  ARM_NOINLINE void reset() {
    for (int j = 0; j < NJ; ++j) {
      q[j] = qt[j] = P.park[j];
      v[j] = 0;
      rawUs[j] = 0;
      markSet[j] = false;
    }
    state = ST_OFF;
    activity = ACT_IDLE;
    mode = JOINT_MODE;
    power = false;
    rtHeld = dwelling = loop = false;
    t = segT = segDur = dwellT = powerT = lowT = 0;
    lastLimitT = -10;
    playIdx = 0;
    rumbleMs = 0;
    event = nullptr;
    bA = bB = bX = bY = bLB = bRB = bDown = bView = bMenu = BtnState();
    speedLvl = (int)clampf(P.speed_lvl, 1, 3);
    pt = kin::forward(geo(), qt);
  }

  void note(const char* msg, uint16_t rumble = 0) {
    event = msg;
    if (rumble > rumbleMs) rumbleMs = rumble;
  }

  ARM_NOINLINE bool inLimits(const float* qq) const {
    for (int j = 0; j < NJ; ++j)
      if (qq[j] < P.qmin[j] - 1e-3f || qq[j] > P.qmax[j] + 1e-3f) return false;
    return true;
  }

  // Pulse sent to joint j (us).
  ARM_NOINLINE float pulseUs(int j) const {
    if (rawUs[j] > 0) return rawUs[j];
    return clampf(P.us0[j] + P.usdeg[j] * q[j], P.pulse_min, P.pulse_max);
  }

  // Pose -> joint angles J1..J5 with workspace and joint-limit checks.
  // Returns nullptr or a PSTR error message.
  ARM_NOINLINE const char* solve(const kin::Pose& p, float* out) const {
    if (p.z < P.z_min) return PSTR("below z_min (table)");
    float s[NARM];
    const kin::Result r = kin::inverse(geo(), p, P.r_min, s);
    if (r == kin::UNREACHABLE) return PSTR("out of reach");
    if (r == kin::TOO_CLOSE) return PSTR("too close to the base");
    for (int j = 0; j < NARM; ++j)
      if (s[j] < P.qmin[j] - 1e-3f || s[j] > P.qmax[j] + 1e-3f) return PSTR("joint limit");
    for (int j = 0; j < NARM; ++j) out[j] = s[j];
    return nullptr;
  }

  ARM_NOINLINE void holdHere() {
    for (int j = 0; j < NJ; ++j) qt[j] = q[j];
    pt = kin::forward(geo(), qt);
    activity = ACT_IDLE;
    dwelling = false;
  }

  // ---------------- commands (gamepad buttons and text) ----------------
  ARM_NOINLINE void enable() {
    if (state == ST_ON) return;
    for (int j = 0; j < NJ; ++j) { q[j] = qt[j] = P.park[j]; v[j] = 0; }
    state = ST_ON;
    activity = ACT_IDLE;
    power = true;
    powerT = lowT = 0;
    pt = kin::forward(geo(), qt);
    note(PSTR("servos ON (arm goes to the park pose)"), 150);
  }

  ARM_NOINLINE void powerOff(ArmState s, const char* why) {
    power = false;
    state = s;
    activity = ACT_IDLE;
    for (int j = 0; j < NJ; ++j) { q[j] = qt[j] = P.park[j]; v[j] = 0; rawUs[j] = 0; }
    pt = kin::forward(geo(), qt);
    note(why, s == ST_OFF ? 300 : 800);
  }

  ARM_NOINLINE void startMove(const float* goal, ArmActivity act) {
    for (int j = 0; j < NJ; ++j) {
      segFrom[j] = q[j];
      segTo[j] = clampf(goal[j], P.qmin[j], P.qmax[j]);
    }
    segDur = moveTime(P, segFrom, segTo, speed());
    segT = 0;
    dwelling = false;
    activity = act;
  }

  ARM_NOINLINE void park() {
    if (state != ST_ON) return;
    float goal[NJ];
    for (int j = 0; j < NJ; ++j) goal[j] = P.park[j];
    startMove(goal, ACT_PARKING);
    note(PSTR("parking, then servos off"), 150);
  }

  ARM_NOINLINE void stop() {
    if (state != ST_ON) return;
    for (int j = 0; j < NJ; ++j) v[j] = 0;
    holdHere();
    note(PSTR("stop"), 120);
  }

  ARM_NOINLINE bool moveJoints(const float* goal) {
    if (state != ST_ON || !inLimits(goal)) return false;
    startMove(goal, ACT_MOVE);
    return true;
  }

  ARM_NOINLINE const char* movePose(const kin::Pose& p) {
    float goal[NJ];
    const char* err = solve(p, goal);
    if (err) return err;
    goal[GRIP] = qt[GRIP];
    startMove(goal, ACT_MOVE);
    return nullptr;
  }

  ARM_NOINLINE void home() {
    if (state != ST_ON) return;
    float goal[NJ];
    for (int j = 0; j < NJ; ++j) goal[j] = P.home[j];
    startMove(goal, ACT_MOVE);
  }

  ARM_NOINLINE bool record() {
    if (state != ST_ON || wp.count() >= wp.capacity()) return false;
    return wp.append(qt);
  }

  ARM_NOINLINE bool play(bool loopIt) {
    if (state != ST_ON || wp.count() == 0) return false;
    loop = loopIt;
    playIdx = 0;
    float goal[NJ];
    wp.get(0, goal);
    startMove(goal, ACT_PLAY);
    return true;
  }

  ARM_NOINLINE void setMode(ArmMode m) {
    mode = m;
    pt = kin::forward(geo(), qt);
  }

  // Calibration helper: joint j is now physically at angle a.
  ARM_NOINLINE void syncJoint(int j, float a) {
    q[j] = qt[j] = clampf(a, P.qmin[j], P.qmax[j]);
    v[j] = 0;
    pt = kin::forward(geo(), qt);
  }

  // ---------------- main loop ----------------
  void update(const PadInput& in, const Sensors& s, float dt) {
    t += dt;
    rumbleMs = 0;
    event = nullptr;
    sense(s, dt);
    buttons(in, dt);

    if (state == ST_ON) {
      PadInput sh = in;
      sh.lx = shape(in.lx, P.deadband, P.expo);
      sh.ly = shape(in.ly, P.deadband, P.expo);
      sh.rx = shape(in.rx, P.deadband, P.expo);
      sh.ry = shape(in.ry, P.deadband, P.expo);
      sh.lt = in.lt < 0.05f ? 0 : in.lt;
      sh.rt = in.rt < 0.05f ? 0 : in.rt;
      if (!in.valid) sh = PadInput();
      if (activity != ACT_IDLE && touched(sh)) {
        holdHere();
        note(PSTR("manual override"), 100);
      }
      if (activity == ACT_IDLE) jog(sh, speed(), dt);
      else runActivity(dt);
    }

    if (power) track(dt);
    if (activity == ACT_PARKING && !dwelling && segT >= segDur && settled())
      powerOff(ST_OFF, PSTR("parked, servos OFF"));
  }

  bool touched(const PadInput& sh) const {
    return fabsf(sh.lx) > OVERRIDE || fabsf(sh.ly) > OVERRIDE || fabsf(sh.rx) > OVERRIDE ||
           fabsf(sh.ry) > OVERRIDE || sh.lt > 0.1f || sh.rt > 0.1f || sh.btn[PB_LEFT] || sh.btn[PB_RIGHT];
  }

  bool settled() const {
    for (int j = 0; j < NJ; ++j)
      if (fabsf(q[j] - qt[j]) > 0.5f) return false;
    return true;
  }

  // Emergency stop: the mushroom switch cut the servo supply while the servos are on.
  // The pulses are switched off too, so releasing the switch does not make the arm jump.
  ARM_NOINLINE void sense(const Sensors& s, float dt) {
    if (!s.ok || !power || P.estop_mv == 0) { lowT = 0; return; }
    powerT += dt;
    if (powerT > 0.3f && s.volts * 1000.0f < P.estop_mv) {
      lowT += dt;
      if (lowT >= 0.1f) powerOff(ST_ESTOP, PSTR("EMERGENCY STOP: servo supply lost, pulses off"));
    } else {
      lowT = 0;
    }
  }

  ARM_NOINLINE void buttons(const PadInput& in, float dt) {
    PadInput b = in.valid ? in : PadInput();
    bA.update(b.btn[PB_A], dt, LONG_A_S);
    bB.update(b.btn[PB_B], dt, 1e9f);
    bX.update(b.btn[PB_X], dt, LONG_X_S);
    bY.update(b.btn[PB_Y], dt, 1e9f);
    bLB.update(b.btn[PB_LB], dt, 1e9f);
    bRB.update(b.btn[PB_RB], dt, 1e9f);
    bDown.update(b.btn[PB_DOWN], dt, 1e9f);
    bView.update(b.btn[PB_VIEW], dt, 1e9f);
    bMenu.update(b.btn[PB_MENU], dt, 1e9f);
    if (!in.valid) return;

    if (bMenu.pressed) {
      if (state == ST_ON) park();
      else enable();
    }
    if (bB.pressed) stop();
    if (bLB.pressed && speedLvl > 1) { --speedLvl; note(PSTR("speed -"), 80); }
    if (bRB.pressed && speedLvl < 3) { ++speedLvl; note(PSTR("speed +"), 80); }
    if (state != ST_ON) return;

    if (bView.pressed) {
      setMode(mode == JOINT_MODE ? CART_MODE : JOINT_MODE);
      note(mode == CART_MODE ? PSTR("mode: XYZ") : PSTR("mode: JOINT"), 200);
    }
    if (bY.pressed) { home(); note(PSTR("home"), 100); }
    if (activity == ACT_IDLE && bA.shortRelease) {
      if (record()) note(PSTR("waypoint recorded"), 100);
      else note(PSTR("waypoint memory full"), 500);
    }
    if (bA.longHit) { wp.clear(); note(PSTR("all waypoints cleared"), 500); }
    if (bX.shortRelease && !play(false)) note(PSTR("no waypoints recorded"), 500);
    if (bX.longHit && play(true)) note(PSTR("playback: loop"), 200);
    if (bDown.pressed && wp.count() > 0) { wp.removeLast(); note(PSTR("last waypoint deleted"), 150); }
  }

  ARM_NOINLINE void jog(const PadInput& in, float sp, float dt) {
    // Gripper: RT closes, LT opens. Releasing RT opens it a little again, so a servo that
    // squeezes an object does not stay stalled (there is no current sensor on the Uno).
    const float g = (in.rt - in.lt) * P.vmax[GRIP] * fmaxf(sp, 0.5f);
    qt[GRIP] = clampf(qt[GRIP] + g * dt, P.qmin[GRIP], P.qmax[GRIP]);
    if (in.rt > 0) {
      rtHeld = true;
    } else if (rtHeld) {
      rtHeld = false;
      qt[GRIP] = clampf(qt[GRIP] - P.grip_backoff, P.qmin[GRIP], P.qmax[GRIP]);
    }
    const float roll = (in.btn[PB_RIGHT] ? 1.0f : 0.0f) - (in.btn[PB_LEFT] ? 1.0f : 0.0f);

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

    // XYZ: the sticks move the tool point in straight lines (up/down, left/right, forward/back).
    const float k = sp * dt;
    const float d[5] = {in.ly * P.v_lin * k, -in.lx * P.v_lin * k, in.ry * P.v_lin * k, -in.rx * P.v_ang * k,
                        roll * P.v_ang * k};
    if (d[0] == 0 && d[1] == 0 && d[2] == 0 && d[3] == 0 && d[4] == 0) return;
    if (tryStep(d, 0x1F)) return;
    // Blocked: keep the axes that still work, so the tool slides along the boundary.
    for (uint8_t m = 1; m < 0x20; m <<= 1) tryStep(d, m);
    limitFeedback();
  }

  // Moves the XYZ target by the components of d selected in mask (bit 0 = x .. bit 4 = roll).
  ARM_NOINLINE bool tryStep(const float* d, uint8_t mask) {
    kin::Pose c = pt;
    float* f[5] = {&c.x, &c.y, &c.z, &c.pitch, &c.roll};
    for (int i = 0; i < 5; ++i)
      if (mask & (1 << i)) *f[i] += d[i];
    float sol[NARM];
    if (solve(c, sol) || !nearTarget(sol)) return false;
    pt = c;
    for (int j = 0; j < NARM; ++j) qt[j] = sol[j];
    return true;
  }

  void limitFeedback() {
    if (t - lastLimitT > 0.6f) { lastLimitT = t; note(PSTR("limit"), 120); }
  }

  // An XYZ step must not flip the arm into another configuration.
  bool nearTarget(const float* sol) const {
    for (int j = 0; j < NARM; ++j)
      if (fabsf(sol[j] - qt[j]) > 15.0f) return false;
    return true;
  }

  ARM_NOINLINE void runActivity(float dt) {
    if (dwelling) {
      dwellT += dt;
      if (dwellT * 1000.0f < P.dwell_ms) return;
      dwelling = false;
      if (++playIdx >= wp.count()) {
        if (!loop) { holdHere(); note(PSTR("playback done"), 200); return; }
        playIdx = 0;
      }
      float goal[NJ];
      wp.get(playIdx, goal);
      startMove(goal, ACT_PLAY);
      return;
    }
    segT += dt;
    const float s = quintic(segDur > 0 ? segT / segDur : 1);
    for (int j = 0; j < NJ; ++j) qt[j] = segFrom[j] + (segTo[j] - segFrom[j]) * s;
    if (segT < segDur) return;
    if (activity == ACT_MOVE) { holdHere(); return; }
    if (activity == ACT_PLAY) { dwelling = true; dwellT = 0; }
    // ACT_PARKING finishes in update() once the tracker has arrived
  }

  // Per-joint speed / acceleration limiter (time-optimal, no overshoot).
  ARM_NOINLINE void track(float dt) {
    for (int j = 0; j < NJ; ++j) {
      const float e = qt[j] - q[j];
      const float a = P.amax[j];
      const float vd = copysignf(fminf((float)P.vmax[j], sqrtf(2.0f * a * fabsf(e))), e);
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
};

inline const char* stateName(ArmState s) {
  return s == ST_ON ? PSTR("ON") : (s == ST_ESTOP ? PSTR("E-STOP") : PSTR("OFF"));
}

inline const char* activityName(ArmActivity a) {
  switch (a) {
    case ACT_MOVE: return PSTR("MOVE");
    case ACT_PLAY: return PSTR("PLAY");
    case ACT_PARKING: return PSTR("PARKING");
    default: return PSTR("IDLE");
  }
}

}  // namespace arm
