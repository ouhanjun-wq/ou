// Host-side unit tests for the hardware-independent flight code.
//   g++ -std=c++17 -O1 -Wall -Wextra -I../butterfly_fc test_core.cpp -o test_core && ./test_core
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ahrs.h"
#include "filters.h"
#include "flight.h"
#include "gps.h"
#include "params.h"
#include "protocol.h"

static int failures = 0;
#define CHECK(cond, ...)                                  \
  do {                                                    \
    if (!(cond)) {                                        \
      ++failures;                                         \
      printf("FAIL %s:%d  ", __FILE__, __LINE__);         \
      printf(__VA_ARGS__);                                \
      printf("\n");                                       \
    }                                                     \
  } while (0)

static const float PI_F = 3.14159265f;

// Steady-state amplitude of a filter for a sine input at frequency f.
template <class F>
static float gainAt(F& filt, float f, float fs, float seconds = 6.0f) {
  const int n = (int)(seconds * fs);
  float peak = 0;
  for (int i = 0; i < n; ++i) {
    const float y = filt.apply(sinf(2 * PI_F * f * i / fs));
    if (i > n / 2) peak = fmaxf(peak, fabsf(y));
  }
  return peak;
}

static void testPT1() {
  PT1 f;
  f.setCutoff(10, 1000);
  const float g = gainAt(f, 10, 1000);
  CHECK(fabsf(g - 0.707f) < 0.03f, "PT1 gain at fc = %.3f (want ~0.707)", g);
}

static void testNotch() {
  Notch n;
  n.set(4.0f, 200.0f, 3.0f);
  const float atF = gainAt(n, 4.0f, 200.0f, 20.0f);
  Notch m;
  m.set(4.0f, 200.0f, 3.0f);
  const float away = gainAt(m, 20.0f, 200.0f, 20.0f);
  CHECK(atF < 0.02f, "notch gain at f0 = %.4f", atF);
  CHECK(away > 0.9f, "notch gain at 5*f0 = %.3f", away);
  Notch b;
  b.set(0.0f, 200.0f, 3.0f);
  CHECK(b.bypass && b.apply(1.23f) == 1.23f, "notch bypass when f0 = 0");
}

static void testStrokeAvg() {
  // Window = one period removes the fundamental and every harmonic.
  StrokeAvg<128> s;
  const float fs = 200, f = 4;
  s.setWindow((int)lroundf(fs / f));
  float peak = 0;
  for (int i = 0; i < 2000; ++i) {
    const float t = i / fs;
    const float x = 10 + 5 * sinf(2 * PI_F * f * t) + 3 * sinf(2 * PI_F * 2 * f * t + 1) +
                    2 * sinf(2 * PI_F * 3 * f * t);
    const float y = s.apply(x);
    if (i > 200) peak = fmaxf(peak, fabsf(y - 10));
  }
  CHECK(peak < 0.05f, "stroke average residual ripple = %.4f", peak);

  // Changing the window must not produce a jump for a constant signal.
  StrokeAvg<128> c;
  c.setWindow(50);
  for (int i = 0; i < 300; ++i) c.apply(7.0f);
  c.setWindow(40);
  CHECK(fabsf(c.apply(7.0f) - 7.0f) < 1e-4f, "window change glitch");
}

static void testDecimator() {
  Decimator d;
  float out = 0;
  int produced = 0;
  for (int i = 0; i < 10; ++i)
    if (d.push((float)i, 5, out)) ++produced;
  CHECK(produced == 2 && fabsf(out - 7.0f) < 1e-5f, "decimator mean = %.2f, count %d", out, produced);
}

static void testAccTrust() {
  CHECK(accTrust(0, 0, -1) == 1.0f, "trust at 1 g");
  CHECK(accTrust(0, 0, -1.5f) == 0.0f, "trust at 1.5 g");
  CHECK(fabsf(accTrust(0, 0, -1.2f) - 0.5f) < 1e-3f, "trust at 1.2 g");
}

// Specific force (g) seen by a still body at the given roll / pitch (deg), FRD.
static void stillAccel(float rollDeg, float pitchDeg, float a[3]) {
  const float r = rollDeg * PI_F / 180, p = pitchDeg * PI_F / 180;
  a[0] = sinf(p);
  a[1] = -sinf(r) * cosf(p);
  a[2] = -cosf(r) * cosf(p);
}

static void testAhrsSigns() {
  Mahony m;
  float a[3];
  stillAccel(20, 0, a);
  m.initFromAccel(a);
  CHECK(fabsf(m.roll - 20) < 0.1f && fabsf(m.pitch) < 0.1f, "init roll: %.2f %.2f", m.roll, m.pitch);
  stillAccel(0, 15, a);
  m.initFromAccel(a);
  CHECK(fabsf(m.pitch - 15) < 0.1f && fabsf(m.roll) < 0.1f, "init pitch: %.2f %.2f", m.roll, m.pitch);

  // Pure gyro integration: +p (right wing down), +q (nose up), +r (nose right)
  const float d2r = PI_F / 180;
  struct { int axis; const char* name; } cases[] = {{0, "roll"}, {1, "pitch"}, {2, "yaw"}};
  for (auto& c : cases) {
    Mahony g;
    g.reset();
    float w[3] = {0, 0, 0};
    w[c.axis] = 30 * d2r;
    const float lvl[3] = {0, 0, -1};
    for (int i = 0; i < 1000; ++i) g.update(w, lvl, 0.0f, 0, 0, 0.001f);  // 1 s, no accel
    const float got = c.axis == 0 ? g.roll : (c.axis == 1 ? g.pitch : g.yaw);
    CHECK(fabsf(got - 30) < 0.5f, "gyro %s integration = %.2f (want +30)", c.name, got);
  }

  // Accelerometer correction pulls a wrong estimate back to the truth.
  Mahony k;
  k.reset();                    // believes level
  stillAccel(25, -10, a);       // truth: roll 25, pitch -10
  const float zero[3] = {0, 0, 0};
  for (int i = 0; i < 20000; ++i) k.update(zero, a, 1.0f, 1.0f, 0.0f, 0.001f);
  CHECK(fabsf(k.roll - 25) < 0.5f && fabsf(k.pitch + 10) < 0.5f,
        "accel convergence: roll %.2f pitch %.2f", k.roll, k.pitch);

  // Gyro bias is learned by the integral term.
  Mahony b;
  float lvl[3];
  stillAccel(0, 0, lvl);
  b.initFromAccel(lvl);
  const float bias[3] = {2 * d2r, -1.5f * d2r, 0};
  for (int i = 0; i < 60000; ++i) b.update(bias, lvl, 1.0f, 0.5f, 0.05f, 0.001f);
  CHECK(fabsf(b.roll) < 0.5f && fabsf(b.pitch) < 0.5f, "bias rejection: roll %.2f pitch %.2f", b.roll, b.pitch);
}

// End-to-end: body rocks +/-10 deg at the flapping frequency around a 5 deg mean roll,
// the accelerometer sees +/-1.5 g of flapping acceleration. The stroke-averaged estimate
// must still recover the 5 deg mean.
static void testFlappingAttitudeChain() {
  const float fs = 1000, f = 4, meanRoll = 5, rock = 10, d2r = PI_F / 180;
  Mahony m;
  float a0[3];
  stillAccel(meanRoll, 0, a0);
  m.initFromAccel(a0);
  PT1 lp[3], alp[3];
  for (int k = 0; k < 3; ++k) { lp[k].setCutoff(50, fs); alp[k].setCutoff(15, fs); }
  Decimator dec;
  StrokeAvg<128> avg;
  avg.setWindow((int)lroundf(200 / f));
  float worst = 0;
  for (int i = 0; i < 20000; ++i) {
    const float t = i / fs, w = 2 * PI_F * f;
    const float roll = meanRoll + rock * sinf(w * t);
    const float p = rock * w * cosf(w * t);                 // deg/s
    float a[3];
    stillAccel(roll, 0, a);
    a[2] += -1.5f * sinf(w * t + 0.7f);                      // flapping heave
    const float g[3] = {lp[0].apply(p) * d2r, lp[1].apply(0), lp[2].apply(0)};
    const float af[3] = {alp[0].apply(a[0]), alp[1].apply(a[1]), alp[2].apply(a[2])};
    m.update(g, af, accTrust(af[0], af[1], af[2]), 0.5f, 0.02f, 1 / fs);
    float dummy;
    if (dec.push(0, 5, dummy)) {
      const float r = avg.apply(m.roll);
      if (i > 5000) worst = fmaxf(worst, fabsf(r - meanRoll));
    }
  }
  CHECK(worst < 1.5f, "stroke-averaged roll error %.2f deg (want < 1.5)", worst);
}

// ---------------- flight core ----------------
static FlightInputs sticks(float thr, float roll, float pitch, float yaw, uint8_t mode, bool arm) {
  FlightInputs in;
  in.thr = thr; in.roll = roll; in.pitch = pitch; in.yaw = yaw;
  in.mode = mode; in.armReq = arm; in.linkOk = true;
  return in;
}

static void testArming() {
  Params p;
  paramsDefaults(p);
  FlightCore fc;
  fc.reinit(p, 200);
  FlightSensors s;
  s.imuOk = true;
  const float dt = 0.005f;

  // Switch already on at power-up -> must not arm.
  FlightOutput o = fc.step(sticks(0, 0, 0, 0, 0, true), s, p, dt);
  CHECK(o.state == proto::ST_DISARMED && o.armBlocked, "armed with switch already on");
  fc.step(sticks(0, 0, 0, 0, 0, false), s, p, dt);
  // Throttle high on the arm edge -> refuse.
  o = fc.step(sticks(0.5f, 0, 0, 0, 0, true), s, p, dt);
  CHECK(o.state == proto::ST_DISARMED, "armed with throttle high");
  fc.step(sticks(0, 0, 0, 0, 0, false), s, p, dt);
  o = fc.step(sticks(0, 0, 0, 0, 0, true), s, p, dt);
  CHECK(o.state == proto::ST_ARMED, "did not arm with low throttle");

  // Link loss -> failsafe, glide (no flapping).
  FlightInputs lost = sticks(0.8f, 0.5f, 0, 0, 0, true);
  lost.linkOk = false;
  o = fc.step(lost, s, p, dt);
  CHECK(o.state == proto::ST_FAILSAFE && o.flapHz == 0 && o.mode == proto::MODE_STABILIZE,
        "failsafe state %d flap %.2f mode %d", o.state, o.flapHz, o.mode);
  o = fc.step(sticks(0.3f, 0, 0, 0, 0, true), s, p, dt);
  CHECK(o.state == proto::ST_ARMED, "did not recover from failsafe");
  o = fc.step(sticks(0.3f, 0, 0, 0, 0, false), s, p, dt);
  CHECK(o.state == proto::ST_DISARMED, "did not disarm");

  // Link lost while disarmed, returns with the switch on -> stays disarmed.
  FlightInputs gone = sticks(0, 0, 0, 0, 0, false);
  gone.linkOk = false;
  fc.step(gone, s, p, dt);
  o = fc.step(sticks(0, 0, 0, 0, 0, true), s, p, dt);
  CHECK(o.state == proto::ST_DISARMED, "armed on link return with switch already on");

  // Bench mode arms without radio; leaving it disarms (not failsafe).
  FlightInputs bench = sticks(0.3f, 0, 0, 0, 0, false);
  bench.linkOk = false;
  bench.bench = true;
  o = fc.step(bench, s, p, dt);
  CHECK(o.state == proto::ST_ARMED && o.flapHz > 0, "bench did not arm");
  bench.bench = false;
  o = fc.step(bench, s, p, dt);
  CHECK(o.state == proto::ST_DISARMED, "bench off -> state %d", o.state);

  // Charge lock: plugging the charger disarms, blocks bench mode, and needs a fresh
  // arm-switch flip after unplugging.
  fc.step(sticks(0, 0, 0, 0, 0, false), s, p, dt);
  o = fc.step(sticks(0, 0, 0, 0, 0, true), s, p, dt);
  CHECK(o.state == proto::ST_ARMED, "re-arm before charge test");
  FlightInputs chg = sticks(0.5f, 0, 0, 0, 0, true);
  chg.charging = true;
  o = fc.step(chg, s, p, dt);
  CHECK(o.state == proto::ST_DISARMED && o.flapHz == 0 && o.armBlocked, "charging did not disarm");
  chg.bench = true;
  o = fc.step(chg, s, p, dt);
  CHECK(o.state == proto::ST_DISARMED, "bench armed while charging");
  o = fc.step(sticks(0, 0, 0, 0, 0, true), s, p, dt);   // unplugged, switch still on
  CHECK(o.state == proto::ST_DISARMED && o.armBlocked, "armed right after unplugging");
  fc.step(sticks(0, 0, 0, 0, 0, false), s, p, dt);
  o = fc.step(sticks(0, 0, 0, 0, 0, true), s, p, dt);
  CHECK(o.state == proto::ST_ARMED, "cannot arm after charge + switch flip");
  fc.step(sticks(0, 0, 0, 0, 0, false), s, p, dt);

  // IMU fault forces MANUAL.
  FlightSensors bad;
  bad.imuOk = false;
  o = fc.step(sticks(0, 0, 0, 0, proto::MODE_STABILIZE, false), bad, p, dt);
  CHECK(o.mode == proto::MODE_MANUAL, "IMU fault did not force MANUAL");
}

static void testFlappingAndMixer() {
  Params p;
  paramsDefaults(p);
  FlightCore fc;
  fc.reinit(p, 200);
  FlightSensors s;
  s.imuOk = true;
  const float dt = 0.005f;
  fc.step(sticks(0, 0, 0, 0, 0, false), s, p, dt);
  fc.step(sticks(0, 0, 0, 0, 0, true), s, p, dt);

  // Idle: glide at stroke centre.
  FlightOutput o = fc.step(sticks(0.0f, 0, 0, 0, 0, true), s, p, dt);
  CHECK(o.flapHz == 0 && o.wingL == p.center && o.wingR == p.center, "glide pose L %.1f R %.1f", o.wingL, o.wingR);

  // Full throttle: f_max, symmetric stroke reaching centre +/- amp_max.
  float lo = 1e9, hi = -1e9, maxAsym = 0;
  for (int i = 0; i < 400; ++i) {
    o = fc.step(sticks(1.0f, 0, 0, 0, 0, true), s, p, dt);
    lo = fminf(lo, o.wingL); hi = fmaxf(hi, o.wingL);
    maxAsym = fmaxf(maxAsym, fabsf(o.wingL - o.wingR));
  }
  CHECK(fabsf(o.flapHz - p.f_max) < 1e-4f, "flap freq %.2f", o.flapHz);
  CHECK(fabsf(hi - (p.center + p.amp_max)) < 0.5f && fabsf(lo - (p.center - p.amp_max)) < 0.5f,
        "stroke range %.1f .. %.1f", lo, hi);
  CHECK(maxAsym < 1e-4f, "wings not symmetric with centred sticks");

  // MANUAL roll: centre offsets move in opposite directions by man_roll.
  // thr 0.62 -> f = 4 Hz, amp = 32 deg: stays inside wing_limit, 1 s = 4 whole strokes.
  const float thr = 0.62f;
  float sumL = 0, sumR = 0;
  const int n = 200;
  for (int i = 0; i < n; ++i) {
    o = fc.step(sticks(thr, 1.0f, 0, 0, 0, true), s, p, dt);
    sumL += o.wingL; sumR += o.wingR;
  }
  CHECK(fabsf(sumL / n - (p.center + p.man_roll)) < 1.0f && fabsf(sumR / n - (p.center - p.man_roll)) < 1.0f,
        "roll offsets L %.2f R %.2f", sumL / n, sumR / n);

  // MANUAL yaw: amplitude differential.
  float loL = 1e9, hiL = -1e9, loR = 1e9, hiR = -1e9;
  for (int i = 0; i < n; ++i) {
    o = fc.step(sticks(thr, 0, 0, 1.0f, 0, true), s, p, dt);
    loL = fminf(loL, o.wingL); hiL = fmaxf(hiL, o.wingL);
    loR = fminf(loR, o.wingR); hiR = fmaxf(hiR, o.wingR);
  }
  const float ampL = (hiL - loL) / 2, ampR = (hiR - loR) / 2;
  const float amp = 32.0f;
  CHECK(fabsf(ampL - (amp + p.man_yaw)) < 0.5f && fabsf(ampR - (amp - p.man_yaw)) < 0.5f,
        "yaw amplitudes L %.2f R %.2f", ampL, ampR);

  // Wing limit is never exceeded.
  p.center = 40; p.man_pitch = 40;
  for (int i = 0; i < n; ++i) {
    o = fc.step(sticks(1.0f, 1.0f, 1.0f, 1.0f, 0, true), s, p, dt);
    CHECK(fabsf(o.wingL) <= p.wing_limit + 1e-4f && fabsf(o.wingR) <= p.wing_limit + 1e-4f,
          "wing limit exceeded L %.1f R %.1f", o.wingL, o.wingR);
  }
}

static void testStabilizeDirection() {
  Params p;
  paramsDefaults(p);
  FlightCore fc;
  fc.reinit(p, 200);
  const float dt = 0.005f;
  FlightSensors s;
  s.imuOk = true;
  fc.step(sticks(0, 0, 0, 0, 0, false), s, p, dt);
  fc.step(sticks(0, 0, 0, 0, proto::MODE_STABILIZE, true), s, p, dt);

  // Rolled right 20 deg with centred sticks -> corrective (negative) roll output.
  s.rollAvg = 20;
  FlightOutput o = fc.step(sticks(0.5f, 0, 0, 0, proto::MODE_STABILIZE, true), s, p, dt);
  CHECK(o.ur < 0, "roll correction sign ur = %.3f", o.ur);
  s.rollAvg = 0;
  s.pitchAvg = -15;   // nose down -> positive pitch output
  o = fc.step(sticks(0.5f, 0, 0, 0, proto::MODE_STABILIZE, true), s, p, dt);
  CHECK(o.up > 0, "pitch correction sign up = %.3f", o.up);

  // Heading hold: a +30 deg turn command produces a positive (right) yaw output.
  s.pitchAvg = 0;
  s.yaw = 0;
  fc.step(sticks(0.5f, 0, 0, 0, proto::MODE_HEADING_HOLD, true), s, p, dt);
  FlightInputs in = sticks(0.5f, 0, 0, 0, proto::MODE_HEADING_HOLD, true);
  in.turnDeg = 30;
  o = fc.step(in, s, p, dt);
  CHECK(o.uy > 0, "heading-hold turn sign uy = %.3f", o.uy);

  // Integrator is bounded.
  s.rollAvg = 60;
  for (int i = 0; i < 4000; ++i) o = fc.step(sticks(1.0f, 0, 0, 0, proto::MODE_STABILIZE, true), s, p, dt);
  CHECK(fabsf(o.ur) <= 1.5f * p.man_roll + 1e-3f, "roll output bounded: %.2f", o.ur);
}

static void testAutoMode() {
  Params p;
  paramsDefaults(p);
  FlightCore fc;
  fc.reinit(p, 200);
  const float dt = 0.005f;
  FlightSensors s;
  s.imuOk = true;
  s.baroOk = true;
  const uint8_t AUTO = proto::MODE_AUTO;

  CHECK(climbCommand(0.5f) == 0 && climbCommand(0.53f) == 0, "climb deadband");
  CHECK(fabsf(climbCommand(1.0f) - 1) < 1e-5f && fabsf(climbCommand(0.0f) + 1) < 1e-5f, "climb range");

  // In AUTO the "safe" throttle for arming is the centred stick, not zero.
  fc.step(sticks(0.5f, 0, 0, 0, AUTO, false), s, p, dt);
  FlightOutput o = fc.step(sticks(0.0f, 0, 0, 0, AUTO, true), s, p, dt);
  CHECK(o.state == proto::ST_DISARMED, "AUTO armed with climb stick fully down");
  fc.step(sticks(0.5f, 0, 0, 0, AUTO, false), s, p, dt);
  o = fc.step(sticks(0.5f, 0, 0, 0, AUTO, true), s, p, dt);
  CHECK(o.state == proto::ST_ARMED, "AUTO did not arm with centred stick");

  // Armed but not launched: glide pose, no flapping, even with a small climb input.
  o = fc.step(sticks(0.6f, 0, 0, 0, AUTO, true), s, p, dt);
  CHECK(o.flapHz == 0, "flapping before launch gesture (f = %.2f)", o.flapHz);

  // Launch gesture: push climb past half-way -> flapping above hover throttle.
  o = fc.step(sticks(1.0f, 0, 0, 0, AUTO, true), s, p, dt);
  CHECK(o.flapHz > 0 && o.thrCmd > p.thr_hover, "launch: f %.2f thr %.2f", o.flapHz, o.thrCmd);

  // Climbing with the stick up until 2 m, then released: target locks at 2 m.
  s.alt = 2.0f; s.vz = 0.8f;
  fc.step(sticks(1.0f, 0, 0, 0, AUTO, true), s, p, dt);
  s.vz = 0;
  o = fc.step(sticks(0.5f, 0, 0, 0, AUTO, true), s, p, dt);
  CHECK(fabsf(o.altTarget - 2.0f) < 1e-4f, "altitude target %.2f", o.altTarget);
  s.alt = 1.5f; s.vz = -0.2f;
  o = fc.step(sticks(0.5f, 0, 0, 0, AUTO, true), s, p, dt);
  CHECK(o.thrCmd > p.thr_hover, "below target but thr %.3f <= hover", o.thrCmd);
  s.alt = 2.5f; s.vz = 0.2f;
  o = fc.step(sticks(0.5f, 0, 0, 0, AUTO, true), s, p, dt);
  CHECK(o.thrCmd < p.thr_hover + 0.05f, "above target but thr %.3f", o.thrCmd);

  // CMD_ALT raises the target.
  FlightInputs up = sticks(0.5f, 0, 0, 0, AUTO, true);
  up.altDelta = 1.0f;
  o = fc.step(up, s, p, dt);
  CHECK(fabsf(o.altTarget - 3.0f) < 1e-4f, "CMD_ALT target %.2f (want 3.0)", o.altTarget);

  // Throttle command always stays in the valid range.
  s.alt = -50; s.vz = -5;
  for (int i = 0; i < 2000; ++i) o = fc.step(sticks(0.5f, 0, 0, 0, AUTO, true), s, p, dt);
  CHECK(o.thrCmd <= 1.0f && o.thrCmd >= p.thr_idle, "thr out of range %.3f", o.thrCmd);

  // Link loss -> glide; link back -> AUTO resumes without a new launch gesture.
  s.alt = 2.0f; s.vz = 0;
  FlightInputs lost = sticks(0.5f, 0, 0, 0, AUTO, true);
  lost.linkOk = false;
  o = fc.step(lost, s, p, dt);
  CHECK(o.state == proto::ST_FAILSAFE && o.flapHz == 0, "AUTO failsafe");
  o = fc.step(sticks(0.5f, 0, 0, 0, AUTO, true), s, p, dt);
  CHECK(o.state == proto::ST_ARMED && o.flapHz > 0, "AUTO did not resume after failsafe");

  // No barometer: AUTO degrades to HEADING_HOLD.
  s.baroOk = false;
  o = fc.step(sticks(0.5f, 0, 0, 0, AUTO, true), s, p, dt);
  CHECK(o.mode == proto::MODE_HEADING_HOLD, "no-baro fallback mode %d", o.mode);
}

static void testReturnToHome() {
  Params p;
  paramsDefaults(p);
  const float dt = 0.005f;
  CHECK(fabsf(bearingToHome(50, 0) - 180) < 0.01f || fabsf(bearingToHome(50, 0) + 180) < 0.01f,
        "home due south: %.1f", bearingToHome(50, 0));
  CHECK(fabsf(bearingToHome(0, -30) - 90) < 0.01f, "home due east: %.1f", bearingToHome(0, -30));

  auto flyingCore = [&](FlightCore& fc, FlightSensors& s) {
    fc.reinit(p, 200);
    fc.step(sticks(0, 0, 0, 0, 0, false), s, p, dt);
    fc.step(sticks(0, 0, 0, 0, 0, true), s, p, dt);
    // Straight flight: gyro yaw 10 deg while GPS course is 100 deg -> offset 90 deg.
    s.gpsCourse = 100; s.yaw = 10; s.gpsSpeed = 3;
    for (int i = 0; i < 1000; ++i) fc.step(sticks(0.7f, 0, 0, 0, proto::MODE_STABILIZE, true), s, p, dt);
  };

  FlightSensors s;
  s.imuOk = true; s.baroOk = true; s.gpsOk = true; s.homeSet = true; s.alt = 5;
  FlightCore fc;
  flyingCore(fc, s);
  CHECK(fc.northValid() && fabsf(fc.northOffset() - 90) < 1.0f, "north offset %.1f valid %d",
        fc.northOffset(), fc.northValid());

  // Link lost 50 m north of home: RTH in AUTO, still flapping, target yaw = 180 - 90 = 90.
  s.north = 50; s.east = 0; s.yaw = 0;
  FlightInputs lost = sticks(0.7f, 0, 0, 0, proto::MODE_STABILIZE, true);
  lost.linkOk = false;
  FlightOutput o = fc.step(lost, s, p, dt);
  CHECK(o.state == proto::ST_FAILSAFE && o.rth && o.mode == proto::MODE_AUTO && o.flapHz > 0,
        "failsafe RTH: state %d rth %d mode %d f %.2f", o.state, o.rth, o.mode, o.flapHz);
  CHECK(o.uy > 0, "RTH should turn right towards yaw 90, uy = %.3f", o.uy);

  // Arrived inside the radius: failsafe glides down (no flapping).
  s.north = 5;
  o = fc.step(lost, s, p, dt);
  CHECK(!o.rth && o.flapHz == 0, "inside radius should glide: rth %d f %.2f", o.rth, o.flapHz);

  // Give up after rth_max_s.
  s.north = 80;
  for (int i = 0; i < (int)(p.rth_max_s / dt) + 10; ++i) o = fc.step(lost, s, p, dt);
  CHECK(!o.rth && o.flapHz == 0, "RTH should time out, rth %d", o.rth);

  // No GPS or low battery: plain glide.
  FlightCore fc2;
  FlightSensors s2 = s;
  flyingCore(fc2, s2);
  s2.north = 50; s2.gpsOk = false;
  o = fc2.step(lost, s2, p, dt);
  CHECK(!o.rth && o.flapHz == 0, "no GPS should glide");
  s2.gpsOk = true;
  FlightInputs lostLow = lost;
  lostLow.lowBatt = true;
  o = fc2.step(lostLow, s2, p, dt);
  CHECK(!o.rth && o.flapHz == 0, "low battery should glide");

  // Commanded RTH (link OK): flies home; close to home it circles instead of gliding.
  FlightCore fc3;
  FlightSensors s3 = s;
  flyingCore(fc3, s3);
  s3.north = 40; s3.east = 0;
  o = fc3.step(sticks(0.5f, 0, 0, 0, proto::MODE_RTH, true), s3, p, dt);
  CHECK(o.rth && o.state == proto::ST_ARMED && o.flapHz > 0, "commanded RTH");
  s3.north = 3;
  o = fc3.step(sticks(0.5f, 0, 0, 0, proto::MODE_RTH, true), s3, p, dt);
  CHECK(o.rth && o.flapHz > 0, "commanded RTH should loiter near home");

  // Without north alignment (never flew straight with GPS) RTH is not attempted.
  FlightCore fc4;
  FlightSensors s4;
  s4.imuOk = true; s4.baroOk = true; s4.gpsOk = true; s4.homeSet = true; s4.north = 50;
  fc4.reinit(p, 200);
  fc4.step(sticks(0, 0, 0, 0, 0, false), s4, p, dt);
  fc4.step(sticks(0, 0, 0, 0, 0, true), s4, p, dt);
  fc4.step(sticks(0.7f, 0, 0, 0, proto::MODE_STABILIZE, true), s4, p, dt);
  o = fc4.step(lost, s4, p, dt);
  CHECK(!o.rth, "RTH without north alignment");
}

static void feedLine(NmeaParser& g, const char* body, bool goodChecksum = true) {
  uint8_t sum = 0;
  for (const char* p = body; *p; ++p) sum ^= (uint8_t)*p;
  if (!goodChecksum) sum ^= 0x55;
  char line[128];
  snprintf(line, sizeof(line), "$%s*%02X\r\n", body, sum);
  for (const char* p = line; *p; ++p) g.feed(*p, 1234);
}

static void testGps() {
  NmeaParser g;
  // Known sentence from the NMEA spec examples (checksum 47).
  const char* spec = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n";
  for (const char* p = spec; *p; ++p) g.feed(*p, 1000);
  const GpsFix& f = g.fix();
  CHECK(f.valid && f.quality == 1 && f.sats == 8, "GGA fix: valid %d q %d sats %d", f.valid, f.quality, f.sats);
  CHECK(fabs(f.lat - (48 + 7.038 / 60)) < 1e-7 && fabs(f.lon - (11 + 31.0 / 60)) < 1e-7,
        "GGA position %.7f %.7f", f.lat, f.lon);
  CHECK(fabsf(f.altMsl - 545.4f) < 1e-3f && fabsf(f.hdop - 0.9f) < 1e-4f, "GGA alt %.1f hdop %.2f", f.altMsl, f.hdop);

  // Southern / western hemisphere, GN talker, RMC speed and course.
  feedLine(g, "GNRMC,081836,A,3751.65,S,14507.36,W,10.0,054.7,191194,020.3,E,A");
  CHECK(g.fix().lat < -37.86 && g.fix().lat > -37.87 && g.fix().lon < -145.12 && g.fix().lon > -145.13,
        "RMC S/W position %.5f %.5f", g.fix().lat, g.fix().lon);
  CHECK(fabsf(g.fix().speed - 5.14444f) < 1e-3f && fabsf(g.fix().course - 54.7f) < 1e-3f,
        "RMC speed %.3f course %.1f", g.fix().speed, g.fix().course);

  // Bad checksum is rejected and counted; no-fix GGA clears validity.
  const uint32_t before = g.sentences();
  feedLine(g, "GPGGA,000000,0000.000,N,00000.000,E,1,05,1.0,1.0,M,,M,,", false);
  CHECK(g.sentences() == before && g.checksumErrors() == 1, "bad checksum accepted");
  feedLine(g, "GPGGA,000001,,,,,0,00,99.9,,M,,M,,");
  CHECK(!g.fix().valid && g.fix().sats == 0, "no-fix GGA still valid");

  // Garbage and overlong lines don't crash or produce sentences.
  for (int i = 0; i < 500; ++i) g.feed((char)('A' + i % 26), 0);
  g.feed('\n', 0);
  CHECK(g.checksumErrors() == 1, "garbage produced a checksum error");
}

static void testProtocol() {
  proto::ControlPacket c = {};
  c.thr = 500; c.roll = -100; c.mode = 1; c.armed = 1;
  proto::seal(c, proto::PKT_CONTROL, 7, 42);
  proto::ControlPacket out;
  const uint8_t* raw = reinterpret_cast<const uint8_t*>(&c);
  CHECK(proto::open(raw, sizeof(c), proto::PKT_CONTROL, 7, out) && out.thr == 500 && out.roll == -100,
        "control packet round trip");
  CHECK(!proto::open(raw, sizeof(c), proto::PKT_CONTROL, 8, out), "wrong net id accepted");
  uint8_t bad[sizeof(c)];
  memcpy(bad, &c, sizeof(c));
  bad[5] ^= 0x01;
  CHECK(!proto::open(bad, sizeof(c), proto::PKT_CONTROL, 7, out), "corrupted packet accepted");
  CHECK(sizeof(proto::TelemetryPacket) <= 250 && sizeof(proto::ParamPacket) <= 250, "ESP-NOW payload limit");
}

int main() {
  testPT1();
  testNotch();
  testStrokeAvg();
  testDecimator();
  testAccTrust();
  testAhrsSigns();
  testFlappingAttitudeChain();
  testArming();
  testFlappingAndMixer();
  testStabilizeDirection();
  testAutoMode();
  testGps();
  testReturnToHome();
  testProtocol();
  if (failures == 0) printf("all tests passed\n");
  return failures == 0 ? 0 : 1;
}
