// Host-side unit tests for the hardware-independent hexapod code.
//   g++ -std=c++17 -O1 -Wall -Wextra -I../hexapod_g7pro test_core.cpp ../hexapod_g7pro/params.cpp -o test_core && ./test_core
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <initializer_list>

#include "gait.h"
#include "kinematics.h"
#include "params.h"
#include "servo_out.h"
#include "teleop.h"

static int failures = 0;
#define CHECK(cond, ...)                          \
  do {                                            \
    if (!(cond)) {                                \
      ++failures;                                 \
      printf("FAIL %s:%d  ", __FILE__, __LINE__); \
      printf(__VA_ARGS__);                        \
      printf("\n");                               \
    }                                             \
  } while (0)

static float dist(const kin::Vec3& a, const kin::Vec3& b) {
  return sqrtf((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
}

static void testInverseForward() {
  const kin::LegGeom g{28, 50, 75};
  int n = 0;
  for (float x = 40; x <= 140; x += 10)
    for (float y = -60; y <= 60; y += 20)
      for (float z = -100; z <= 20; z += 15) {
        kin::Joints j;
        if (!kin::inverse(g, {x, y, z}, j)) continue;
        const kin::Vec3 back = kin::forward(g, j);
        CHECK(dist(back, {x, y, z}) < 0.05f, "IK/FK round trip (%.0f %.0f %.0f) off by %.3f", x, y, z,
              dist(back, {x, y, z}));
        ++n;
      }
  CHECK(n > 300, "too few reachable test points: %d", n);

  // Assembly pose: coxa out, femur level, tibia straight down -> all joints 0.
  kin::Joints j;
  CHECK(kin::inverse(g, {g.coxa + g.femur, 0, -g.tibia}, j), "assembly pose unreachable");
  CHECK(fabsf(j.coxa) < 0.01f && fabsf(j.femur) < 0.01f && fabsf(j.tibia) < 0.01f,
        "assembly pose joints %.2f %.2f %.2f", j.coxa, j.femur, j.tibia);

  // Out of reach reports false.
  CHECK(!kin::inverse(g, {400, 0, 0}, j), "far target should be unreachable");
}

static void testFrames() {
  const kin::Vec3 p{100, 30, -50};
  const kin::Vec3 l = kin::bodyToLeg(p, 60, 40, 45);
  const kin::Vec3 b = kin::legToBody(l, 60, 40, 45);
  CHECK(dist(p, b) < 1e-3f, "body<->leg frame round trip");
  // Pure yaw of the body by +10 deg: a foot straight ahead appears rotated -10 deg.
  const kin::Vec3 r = kin::applyBodyPose({100, 0, -60}, 0, 0, 0, 0, 0, 10);
  CHECK(fabsf(atan2f(r.y, r.x) * kin::kDeg + 10) < 0.01f, "yaw pose");
  // Pitch nose down (+): a front foot appears higher relative to the body.
  const kin::Vec3 pd = kin::applyBodyPose({100, 0, -60}, 0, 0, 0, 0, 10, 0);
  CHECK(pd.z > -60, "pitch + should lower the nose (front feet closer): z=%.1f", pd.z);
}

static gait::Hexapod makeHex(const Params& p) {
  gait::Hexapod h(gaitConfig(p));
  gait::Pose stand;
  stand.height = p.height;
  stand.stance = p.stance;
  h.jumpTo(stand);
  return h;
}

static bool jointsWithinLimits(const gait::Hexapod& h, const Params& p) {
  for (int leg = 0; leg < gait::NUM_LEGS; ++leg) {
    const kin::Joints& j = h.joints(leg);
    if (fabsf(j.coxa) > p.coxa_lim || j.femur < p.femur_min || j.femur > p.femur_max ||
        j.tibia < p.tibia_min || j.tibia > p.tibia_max)
      return false;
  }
  return true;
}

static void walk(const Params& p, int type, float vx, float vy, float wz, const char* what) {
  gait::Hexapod h = makeHex(p);
  h.setGait(type);
  h.update(0.02f);
  kin::Vec3 prev[gait::NUM_LEGS];
  for (int l = 0; l < gait::NUM_LEGS; ++l) prev[l] = h.foot(l);
  h.setVelocity(vx, vy, wz);
  float maxJump = 0;
  int minStance = 6, maxStance = 0;
  bool reach = true, limits = true;
  for (int i = 0; i < 1000; ++i) {                 // 20 s
    h.update(0.02f);
    int stance = 0;
    for (int l = 0; l < gait::NUM_LEGS; ++l) {
      maxJump = fmaxf(maxJump, dist(prev[l], h.foot(l)));
      prev[l] = h.foot(l);
      if (h.inStance(l)) {
        ++stance;
        CHECK(fabsf(h.foot(l).z + p.height) < 1e-3f, "%s: stance foot off the ground", what);
      }
    }
    if (i > 50) {
      minStance = stance < minStance ? stance : minStance;
      maxStance = stance > maxStance ? stance : maxStance;
    }
    reach = reach && h.reachable();
    limits = limits && jointsWithinLimits(h, p);
  }
  CHECK(maxJump < 12.0f, "%s: foot jumped %.1f mm in one 20 ms tick", what, maxJump);
  CHECK(reach, "%s: a foot target was out of reach", what);
  CHECK(limits, "%s: a joint went past its limit", what);
  const int expect = type == gait::TRIPOD ? 3 : (type == gait::RIPPLE ? 4 : 5);
  CHECK(minStance >= expect, "%s: only %d feet on the ground (expected >= %d)", what, minStance, expect);

  // Release the stick: the robot stops with every foot back at its neutral spot.
  // Halfway through, push the stick again for a moment (restart while landing).
  h.setVelocity(0, 0, 0);
  maxJump = 0;
  for (int i = 0; i < 300; ++i) {
    if (i == 20) h.setVelocity(vx, vy, wz);
    if (i == 25) h.setVelocity(0, 0, 0);
    h.update(0.02f);
    for (int l = 0; l < gait::NUM_LEGS; ++l) {
      maxJump = fmaxf(maxJump, dist(prev[l], h.foot(l)));
      prev[l] = h.foot(l);
    }
    reach = reach && h.reachable();
  }
  CHECK(maxJump < 12.0f, "%s: foot jumped %.1f mm while stopping", what, maxJump);
  CHECK(reach, "%s: a foot target was out of reach while stopping", what);
  CHECK(!h.walking(), "%s: did not stop", what);
  gait::Hexapod ref = makeHex(p);
  ref.update(0.02f);
  for (int l = 0; l < gait::NUM_LEGS; ++l)
    CHECK(dist(h.foot(l), ref.foot(l)) < 0.5f, "%s: leg %s not back at neutral (%.1f mm)", what,
          gait::legName(l), dist(h.foot(l), ref.foot(l)));
}

static void testGaits() {
  Params p;
  paramsDefaults(p);
  for (int t = 0; t < gait::NUM_TYPES; ++t) {
    char what[64];
    snprintf(what, sizeof what, "%s forward", gait::typeName(t));
    walk(p, t, p.max_vx, 0, 0, what);
    snprintf(what, sizeof what, "%s sidestep", gait::typeName(t));
    walk(p, t, 0, -p.max_vy, 0, what);
    snprintf(what, sizeof what, "%s turn", gait::typeName(t));
    walk(p, t, 0, 0, p.max_wz, what);
    snprintf(what, sizeof what, "%s diagonal+turn", gait::typeName(t));
    walk(p, t, p.max_vx * 0.7f, p.max_vy * 0.7f, -p.max_wz * 0.5f, what);
  }

  // Adjacent legs on one side never swing together (ripple / wave / tripod).
  for (int t = 0; t < gait::NUM_TYPES; ++t) {
    gait::Hexapod h = makeHex(p);
    h.setGait(t);
    h.setVelocity(p.max_vx, 0, 0);
    bool ok = true;
    for (int i = 0; i < 500; ++i) {
      h.update(0.02f);
      for (int side = 0; side < 2; ++side) {
        const int a = side * 3;
        if ((!h.inStance(a) && !h.inStance(a + 1)) || (!h.inStance(a + 1) && !h.inStance(a + 2))) ok = false;
      }
    }
    CHECK(ok, "%s: two neighbouring legs lifted at once", gait::typeName(t));
  }
}

static void testStrideLimit() {
  Params p;
  paramsDefaults(p);
  gait::Hexapod h = makeHex(p);
  h.setVelocity(2000, 0, 0);                      // way beyond what the legs can do
  for (int i = 0; i < 200; ++i) h.update(0.02f);
  const float stanceTime = gait::pattern(gait::TRIPOD).duty * h.period();
  CHECK(h.vx() * stanceTime <= p.stride_max + 0.01f, "stride not limited: %.1f mm", h.vx() * stanceTime);
}

static void testPoses() {
  Params p;
  paramsDefaults(p);
  gait::Hexapod h = makeHex(p);
  gait::Pose sit;
  sit.height = p.sit_height;
  sit.stance = p.sit_stance;
  h.jumpTo(sit);
  h.update(0.02f);
  CHECK(h.reachable(), "sit pose unreachable");
  CHECK(jointsWithinLimits(h, p), "sit pose past joint limits");

  for (float roll : {-p.tilt_max, p.tilt_max})
    for (float pitch : {-p.tilt_max, p.tilt_max})
      for (float yaw : {-p.twist_max, p.twist_max}) {
        gait::Pose q;
        q.height = p.height;
        q.stance = p.stance;
        q.roll = roll;
        q.pitch = pitch;
        q.yaw = yaw;
        q.tx = p.shift_max;
        h.jumpTo(q);
        h.update(0.02f);
        CHECK(h.reachable() && jointsWithinLimits(h, p), "body pose r%.0f p%.0f y%.0f not reachable", roll,
              pitch, yaw);
      }

  for (float height : {p.height_min, p.height_max}) {
    gait::Pose q;
    q.height = height;
    q.stance = p.stance;
    h.jumpTo(q);
    h.update(0.02f);
    CHECK(h.reachable() && jointsWithinLimits(h, p), "height %.0f not reachable", height);
  }
}

static void testTeleop() {
  Params p;
  paramsDefaults(p);
  gait::Hexapod h = makeHex(p);
  teleop::Teleop t(teleopLimits(p));
  teleop::PadInput in;
  in.connected = true;

  // Sticks do nothing while the servos are off.
  in.ly = 1;
  t.update(0.02f, in, h);
  CHECK(t.mode() == teleop::OFF && h.pose().height == p.height, "moved while off");

  // A powers on: jumps to the sit pose and rises to stand.
  in = teleop::PadInput();
  in.connected = true;
  in.a = true;
  unsigned ev = t.update(0.02f, in, h);
  CHECK((ev & teleop::EV_POWER_ON) && t.mode() == teleop::STAND, "A from off should power on and stand");
  in.a = false;
  for (int i = 0; i < 200; ++i) { t.update(0.02f, in, h); h.update(0.02f); }
  CHECK(fabsf(h.pose().height - p.height) < 0.5f, "did not rise to stand height: %.1f", h.pose().height);

  // Left stick up = forward, right = sidestep right, right stick right = turn clockwise.
  in.ly = 1; in.lx = 1; in.rx = 1;
  for (int i = 0; i < 100; ++i) { t.update(0.02f, in, h); h.update(0.02f); }
  CHECK(h.vx() > 0 && h.vy() < 0 && h.wz() < 0, "stick directions vx %.1f vy %.1f wz %.1f", h.vx(), h.vy(), h.wz());

  // B: emergency stop, stays stopped until the sticks are released.
  in.b = true;
  ev = t.update(0.02f, in, h);
  CHECK((ev & teleop::EV_ESTOP) && h.vx() == 0, "B should stop at once");
  in.b = false;
  for (int i = 0; i < 20; ++i) { t.update(0.02f, in, h); h.update(0.02f); }
  CHECK(h.vx() == 0 && t.estopped(), "should stay stopped while sticks are held");
  in.lx = in.ly = in.rx = 0;
  t.update(0.02f, in, h);
  CHECK(!t.estopped(), "estop should clear once sticks are centred");

  // X changes the gait only once the robot has stopped.
  in.ly = 1;
  for (int i = 0; i < 50; ++i) { t.update(0.02f, in, h); h.update(0.02f); }
  in.x = true;
  t.update(0.02f, in, h);
  in.x = false;
  h.update(0.02f);
  CHECK(h.gaitType() == gait::TRIPOD && t.pendingGait() == gait::RIPPLE, "gait switched mid-walk");
  in.ly = 0;
  for (int i = 0; i < 200; ++i) { t.update(0.02f, in, h); h.update(0.02f); }
  CHECK(h.gaitType() == gait::RIPPLE, "gait not applied after stopping");

  // Speed levels, D-pad height with limits.
  in.rb = true; t.update(0.02f, in, h); in.rb = false; t.update(0.02f, in, h);
  CHECK(t.speedLevel() == 3, "RB should raise speed level");
  for (int i = 0; i < 5; ++i) { in.lb = true; t.update(0.02f, in, h); in.lb = false; t.update(0.02f, in, h); }
  CHECK(t.speedLevel() == 1, "LB should stop at level 1");
  for (int i = 0; i < 40; ++i) { in.up = true; t.update(0.02f, in, h); in.up = false; t.update(0.02f, in, h); }
  CHECK(fabsf(t.heightSetting() - p.height_max) < 0.01f, "height should clamp at max");

  // Gamepad lost: sticks read as released, robot stops.
  in = teleop::PadInput();
  in.connected = true;
  in.ly = 1;
  for (int i = 0; i < 50; ++i) { t.update(0.02f, in, h); h.update(0.02f); }
  in.connected = false;
  for (int i = 0; i < 300; ++i) { t.update(0.02f, in, h); h.update(0.02f); }
  CHECK(!h.walking() && t.mode() == teleop::STAND, "should stand still after the gamepad drops");

  // Deadband.
  CHECK(teleop::deadband(0.05f, 0.1f) == 0 && fabsf(teleop::deadband(1.0f, 0.1f) - 1) < 1e-6f, "deadband");
}

static void testParams() {
  paramsDefaults(P);
  float v = 0;
  CHECK(paramSet("femur", 52) && paramGet("femur", v) && v == 52, "set/get");
  CHECK(paramSet("tilt_max", 99) && paramGet("tilt_max", v) && v == 30, "clamp to range");
  CHECK(!paramSet("nope", 1), "unknown param");
  size_t n = 0;
  const ParamInfo* t = paramTable(n);
  CHECK(n * sizeof(float) == sizeof(Params), "param table covers %zu of %zu fields", n, sizeof(Params) / sizeof(float));
  for (size_t i = 0; i < n; ++i) {
    float d = 0;
    Params def;
    paramsDefaults(def);
    memcpy(&d, reinterpret_cast<const char*>(&def) + t[i].offset, sizeof d);
    CHECK(d >= t[i].lo && d <= t[i].hi, "default %s = %.2f outside [%.2f, %.2f]", t[i].name, d, t[i].lo, t[i].hi);
  }
}

static void testServoMap() {
  servo::Map m = {servo::GPIO, 13, 0, 1, 0};
  CHECK(servo::pulseUs(0, m, 11.11f) == 1500, "centre pulse");
  CHECK(servo::pulseUs(45, m, 11.11f) == 2000, "+45 deg: %d", servo::pulseUs(45, m, 11.11f));
  m.dir = -1;
  m.trim_us = 20;
  CHECK(servo::pulseUs(45, m, 11.11f) == 1020, "dir -1 + trim: %d", servo::pulseUs(45, m, 11.11f));
  CHECK(servo::pulseUs(200, m, 11.11f) == servo::US_MIN, "pulse clamped");

  // Default maps: no output used twice, GPIO pins all probe-safe.
  servo::Table t;
  for (int kind = 0; kind < 2; ++kind) {
    if (kind == 0) servo::defaultPca(t);
    else servo::defaultGpio(t);
    for (int a = 0; a < servo::NUM; ++a) {
      for (int b = a + 1; b < servo::NUM; ++b)
        CHECK(!(t.m[a].a == t.m[b].a && t.m[a].b == t.m[b].b), "default map: servos %d and %d share an output", a, b);
      if (t.m[a].kind == servo::GPIO) {
        bool safe = false;
        for (uint8_t p : servo::kProbePins) safe = safe || p == t.m[a].a;
        CHECK(safe, "default GPIO %d is not an output-safe pin", t.m[a].a);
      }
    }
  }
}

int main() {
  testInverseForward();
  testFrames();
  testGaits();
  testStrideLimit();
  testPoses();
  testTeleop();
  testParams();
  testServoMap();
  if (failures) {
    printf("%d check(s) failed\n", failures);
    return 1;
  }
  printf("all tests passed\n");
  return 0;
}
