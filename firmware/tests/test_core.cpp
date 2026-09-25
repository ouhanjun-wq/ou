// Host-side unit tests for the hardware-independent arm code.
//   g++ -std=c++17 -O1 -Wall -Wextra -Werror -I firmware/arm_controller firmware/tests/test_core.cpp -o test_core
//   ./test_core
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "commands.h"
#include "kinematics.h"
#include "motion.h"
#include "params.h"

static int failures = 0, checks = 0;
#define CHECK(cond, ...)                              \
  do {                                                \
    ++checks;                                         \
    if (!(cond)) {                                    \
      ++failures;                                     \
      printf("FAIL %s:%d  ", __FILE__, __LINE__);     \
      printf(__VA_ARGS__);                            \
      printf("\n");                                   \
    }                                                 \
  } while (0)

using namespace arm;
constexpr float DT = 0.02f;   // 50 Hz, as on the ESP32

static Params defaults() {
  Params p;
  paramsDefaults(p);
  return p;
}

struct Rig {
  Params P = defaults();
  ArmCore c;
  PadInput in;
  Sensors s;
  Rig() : c(P) { s.ok = true; s.volts = 6.0f; s.amps = 0.4f; }
  void run(float seconds) {
    const int n = (int)lroundf(seconds / DT);
    for (int i = 0; i < n; ++i) c.update(in, s, DT);
  }
  void tap(bool PadInput::*btn, float hold = 0.1f) {
    in.valid = true;
    in.*btn = true;
    run(hold);
    in.*btn = false;
    run(0.1f);
  }
  const char* text(const char* line) {
    static char buf[128], reply[200];
    snprintf(buf, sizeof(buf), "%s", line);
    if (!textCommand(c, s, buf, reply, sizeof(reply))) return "(unknown)";
    return reply;
  }
  void on() { in.valid = true; c.enable(); run(0.5f); }
};

static float maxAbsDiff(const float* a, const float* b, int n) {
  float m = 0;
  for (int i = 0; i < n; ++i) m = fmaxf(m, fabsf(a[i] - b[i]));
  return m;
}

// ---------------- kinematics ----------------
static void testForwardHome() {
  Params P;
  paramsDefaults(P);
  const kin::Geometry g{P.d1, P.l2, P.l3, P.l4};
  const kin::Pose p = kin::forward(g, P.home);
  CHECK(fabsf(p.x - 220) < 0.01f && fabsf(p.y) < 0.01f && fabsf(p.z - 180) < 0.01f && fabsf(p.pitch) < 0.01f,
        "home pose %.2f %.2f %.2f pitch %.2f (want 220 0 180 0)", p.x, p.y, p.z, p.pitch);
  const float left[5] = {90, 90, -90, 0, 0};
  const kin::Pose l = kin::forward(g, left);
  CHECK(fabsf(l.x) < 0.01f && fabsf(l.y - 220) < 0.01f, "J1 +90 turns left (y+): %.2f %.2f", l.x, l.y);
}

static void testIkRoundTrip() {
  Params P;
  paramsDefaults(P);
  const kin::Geometry g{P.d1, P.l2, P.l3, P.l4};
  srand(7);
  int tested = 0;
  float worst = 0;
  for (int i = 0; i < 20000 && tested < 2000; ++i) {
    float q[5];
    for (int j = 0; j < 5; ++j) q[j] = P.qmin[j] + (P.qmax[j] - P.qmin[j]) * (rand() / (float)RAND_MAX);
    if (q[2] > -2.0f) continue;                          // elbow-up solutions only
    const kin::Pose p = kin::forward(g, q);
    const float sr = p.x * cosf(q[0] * kin::DEG) + p.y * sinf(q[0] * kin::DEG);   // signed reach
    if (sr < P.r_min) continue;                          // tool behind the base axis: other solution
    float s[5];
    if (kin::inverse(g, p, P.r_min, s) != kin::OK) {
      CHECK(false, "IK failed for q %.1f %.1f %.1f %.1f", q[0], q[1], q[2], q[3]);
      if (failures > 5) break;
      continue;
    }
    worst = fmaxf(worst, maxAbsDiff(q, s, 5));
    ++tested;
  }
  CHECK(tested > 500, "IK round trip: only %d samples", tested);
  CHECK(worst < 0.05f, "IK round trip worst error %.4f deg", worst);
}

static void testIkRejects() {
  Params P;
  paramsDefaults(P);
  const kin::Geometry g{P.d1, P.l2, P.l3, P.l4};
  float s[5];
  CHECK(kin::inverse(g, {600, 0, 100, 0, 0}, P.r_min, s) == kin::UNREACHABLE, "far pose must be unreachable");
  CHECK(kin::inverse(g, {20, 10, 100, -90, 0}, P.r_min, s) == kin::TOO_CLOSE, "pose at the base axis rejected");
  CHECK(kin::inverse(g, {150, 0, 50, -90, 0}, P.r_min, s) == kin::OK, "pick pose (tool down) reachable");
}

// ---------------- motion primitives ----------------
static void testShapeAndQuintic() {
  CHECK(shape(0.05f, 0.08f, 0.3f) == 0, "inside dead zone");
  CHECK(fabsf(shape(1.0f, 0.08f, 0.3f) - 1.0f) < 1e-5f && fabsf(shape(-1.0f, 0.08f, 0.3f) + 1.0f) < 1e-5f,
        "full deflection = +-1");
  CHECK(shape(0.5f, 0.08f, 0.3f) < 0.5f && shape(0.5f, 0.08f, 0.3f) > 0.3f, "expo softens the middle");
  CHECK(quintic(0) == 0 && fabsf(quintic(1) - 1) < 1e-6f && fabsf(quintic(0.5f) - 0.5f) < 1e-6f, "quintic ends");
}

static void testTrackerLimits() {
  Rig r;
  r.on();
  float goal[NJ];
  memcpy(goal, r.c.qt, sizeof(goal));
  goal[1] = 150;                       // shoulder +50 deg
  r.c.startMove(goal, ACT_MOVE);
  float prevQ = r.c.q[1], prevV = 0, vPeak = 0, aPeak = 0;
  for (int i = 0; i < 400; ++i) {
    r.c.update(r.in, r.s, DT);
    const float vv = (r.c.q[1] - prevQ) / DT;
    vPeak = fmaxf(vPeak, fabsf(vv));
    if (i > 0) aPeak = fmaxf(aPeak, fabsf(vv - prevV) / DT);
    prevQ = r.c.q[1];
    prevV = vv;
  }
  const float sp = SPEED_SCALE[r.c.speedLvl - 1];
  CHECK(fabsf(r.c.q[1] - 150) < 0.01f, "move reached 150 (got %.2f)", r.c.q[1]);
  CHECK(r.c.activity == ACT_IDLE, "move finished");
  CHECK(vPeak <= r.P.vmax[1] * sp * 1.05f, "peak speed %.1f > %.1f", vPeak, r.P.vmax[1] * sp);
  CHECK(aPeak <= r.P.amax[1] * sp * 1.15f, "peak accel %.1f > %.1f", aPeak, r.P.amax[1] * sp);
}

static void testTrackerNoOvershoot() {
  Rig r;
  r.on();
  r.c.qt[3] = r.c.q[3] + 80;           // step: the limiter alone must not overshoot
  float peak = -1e9f;
  for (int i = 0; i < 300; ++i) {
    r.c.update(r.in, r.s, DT);
    peak = fmaxf(peak, r.c.q[3]);
  }
  CHECK(fabsf(r.c.q[3] - r.c.qt[3]) < 1e-3f, "tracker settles");
  CHECK(peak <= r.c.qt[3] + 1e-3f, "tracker overshoot %.3f", peak - r.c.qt[3]);
}

// ---------------- power, parking ----------------
static void testEnableAndPark() {
  Rig r;
  r.in.valid = true;
  CHECK(!r.c.power && r.c.state == ST_OFF, "starts with servo power off");
  r.tap(&PadInput::menu);
  CHECK(r.c.power && r.c.state == ST_ON, "Menu turns servo power on");
  CHECK(maxAbsDiff(r.c.q, r.P.park, NJ) < 1e-3f, "enable starts at the park pose");
  r.tap(&PadInput::y);
  r.run(4);
  CHECK(maxAbsDiff(r.c.q, r.P.home, NJ) < 0.01f, "Y goes home");
  r.tap(&PadInput::menu);
  CHECK(r.c.activity == ACT_PARKING && r.c.power, "Menu parks first");
  r.run(6);
  CHECK(!r.c.power && r.c.state == ST_OFF, "power off after parking");
  CHECK(maxAbsDiff(r.c.q, r.P.park, NJ) < 0.6f, "arm is at the park pose when power goes off");
}

// ---------------- jogging ----------------
static void testJointJog() {
  Rig r;
  r.on();
  const float j2 = r.c.q[1];
  r.in.ly = 1.0f;
  r.run(0.5f);
  r.in.ly = 0;
  r.run(1.0f);
  CHECK(r.c.q[1] > j2 + 5, "left stick up raises the shoulder (%.1f -> %.1f)", j2, r.c.q[1]);
  r.in.lx = 1.0f;                       // stick right: base turns right (J1 negative)
  r.run(0.5f);
  r.in.lx = 0;
  r.run(1.0f);
  CHECK(r.c.q[0] < -3, "stick right turns the base right (%.1f)", r.c.q[0]);
  r.in.ly = 1.0f;
  r.run(10);
  r.in.ly = 0;
  r.run(1);
  CHECK(fabsf(r.c.q[1] - r.P.qmax[1]) < 0.01f, "jog stops at the soft limit (%.2f)", r.c.q[1]);
}

static void testCartesianJog() {
  Rig r;
  r.on();
  r.tap(&PadInput::y);
  r.run(4);
  r.tap(&PadInput::view);
  CHECK(r.c.mode == CART_MODE, "View switches to Cartesian mode");
  const kin::Pose a = r.c.pose();
  r.in.ly = 1.0f;                       // forward
  r.run(0.5f);
  r.in.ly = 0;
  r.run(1.0f);
  const kin::Pose b = r.c.pose();
  CHECK(b.x > a.x + 10, "forward moves +x (%.1f -> %.1f)", a.x, b.x);
  CHECK(fabsf(b.y - a.y) < 0.5f && fabsf(b.z - a.z) < 0.5f && fabsf(b.pitch - a.pitch) < 0.5f,
        "straight line: y %.2f z %.2f pitch %.2f drift", b.y - a.y, b.z - a.z, b.pitch - a.pitch);
  r.in.ry = -1.0f;                      // down
  r.run(0.5f);
  r.in.ry = 0;
  r.run(1.0f);
  const kin::Pose c = r.c.pose();
  CHECK(c.z < b.z - 10 && fabsf(c.x - b.x) < 0.5f, "right stick down lowers the tool (%.1f -> %.1f)", b.z, c.z);
}

static void testCartesianBoundary() {
  Rig r;
  r.on();
  r.tap(&PadInput::y);
  r.run(4);
  r.tap(&PadInput::view);
  r.in.ly = 1.0f;
  r.run(15);                            // push forward far past the reach
  r.in.ly = 0;
  r.run(1);
  const kin::Pose p = r.c.pose();
  bool finite = true;
  for (int j = 0; j < NJ; ++j) finite = finite && isfinite(r.c.q[j]);
  CHECK(finite, "no NaN at the workspace boundary");
  CHECK(r.c.inLimits(r.c.q), "joints stay inside limits at the boundary");
  CHECK(p.x < r.P.l2 + r.P.l3 + r.P.l4 + 1 && p.x > 250, "stopped near full reach (x %.1f)", p.x);
  r.in.ry = -1.0f;                      // push into the table
  r.run(15);
  r.in.ry = 0;
  r.run(1);
  CHECK(r.c.pose().z >= r.P.z_min - 0.5f, "tool never goes below z_min (%.1f)", r.c.pose().z);
}

// ---------------- teach & playback ----------------
static void testTeachPlayback() {
  Rig r;
  r.on();
  const char* pts[3] = {"JOINTS 0 90 -90 0 0 0", "JOINTS 40 70 -60 -30 20 80", "JOINTS -30 100 -120 10 -20 10"};
  float want[3][NJ];
  for (int i = 0; i < 3; ++i) {
    CHECK(!strncmp(r.text(pts[i]), "ok", 2), "%s", pts[i]);
    r.run(5);
    memcpy(want[i], r.c.q, sizeof(want[i]));
    r.tap(&PadInput::a);                   // A (short) records
  }
  CHECK(r.c.seqLen == 3, "3 waypoints recorded (%d)", r.c.seqLen);
  r.tap(&PadInput::x);                     // X (short) plays once
  CHECK(r.c.activity == ACT_PLAY, "X starts playback");
  bool visited[3] = {false, false, false};
  for (int i = 0; i < 1500 && r.c.activity == ACT_PLAY; ++i) {
    r.c.update(r.in, r.s, DT);
    for (int k = 0; k < 3; ++k)
      if (maxAbsDiff(r.c.q, want[k], NJ) < 0.05f) visited[k] = true;
  }
  CHECK(visited[0] && visited[1] && visited[2], "playback visits every waypoint (%d %d %d)", visited[0], visited[1],
        visited[2]);
  CHECK(r.c.activity == ACT_IDLE, "single playback ends");
  // loop + manual override
  r.in.x = true;
  r.run(1.2f);
  r.in.x = false;
  r.run(0.5f);
  CHECK(r.c.activity == ACT_PLAY && r.c.loop, "hold X = loop playback");
  r.run(20);
  CHECK(r.c.activity == ACT_PLAY, "loop keeps playing");
  r.in.rx = 0.8f;
  r.run(0.1f);
  r.in.rx = 0;
  CHECK(r.c.activity == ACT_IDLE, "a stick takes over from playback");
  // hold A = clear
  r.in.a = true;
  r.run(2.2f);
  r.in.a = false;
  r.run(0.1f);
  CHECK(r.c.seqLen == 0, "hold A clears the waypoints (%d)", r.c.seqLen);
}

// ---------------- protection ----------------
static void testOverload() {
  Rig r;
  r.on();
  r.s.amps = 8.0f;                      // stalled joint
  r.run(0.3f);
  CHECK(r.c.state == ST_ON, "short current peak tolerated");
  r.run(0.5f);
  CHECK(r.c.state == ST_OVERLOAD && r.c.power, "sustained over-current -> OVERLOAD");
  r.s.amps = 0.5f;
  r.run(0.2f);
  r.tap(&PadInput::b);
  CHECK(r.c.state == ST_ON, "B resumes after OVERLOAD");
  r.s.amps = 8.0f;
  r.run(0.7f + r.P.fault_s + 0.3f);
  CHECK(r.c.state == ST_FAULT && !r.c.power, "still over-current -> FAULT, power off");
}

static void testEmergencyStop() {
  Rig r;
  r.on();
  r.s.volts = 0.2f;                     // mushroom switch pressed
  r.run(0.3f);
  CHECK(r.c.state == ST_ESTOP && !r.c.power, "servo supply lost -> E-STOP, relay off");
  r.s.volts = 6.0f;
  r.run(0.5f);
  CHECK(!r.c.power, "releasing the E-stop does not re-power by itself");
  r.tap(&PadInput::menu);
  CHECK(r.c.power && r.c.state == ST_ON, "Menu re-enables after E-STOP");
  r.s.ok = false;                       // no INA226: no false E-stop
  r.s.volts = 0;
  r.run(1);
  CHECK(r.c.state == ST_ON, "without the sensor nothing trips");
}

static void testGripDetect() {
  Rig r;
  r.on();
  r.run(0.5f);
  r.in.rt = 1.0f;                       // close
  float gAtContact = -1;
  for (int i = 0; i < 300; ++i) {
    if (r.c.q[GRIP] > 55 && gAtContact < 0) gAtContact = r.c.q[GRIP];
    r.s.amps = gAtContact >= 0 ? 1.6f : 0.5f;   // fingers touch the object at ~55 %
    r.c.update(r.in, r.s, DT);
  }
  r.in.rt = 0;
  r.run(0.5f);
  CHECK(r.c.gripLimit < 70 && r.c.gripLimit > 45, "grip detected near contact (limit %.1f)", r.c.gripLimit);
  CHECK(r.c.q[GRIP] <= r.c.gripLimit + 0.01f, "gripper stopped closing (%.1f)", r.c.q[GRIP]);
  r.s.amps = 0.5f;
  r.in.lt = 1.0f;                       // open again: limit forgotten
  r.run(1.0f);
  r.in.lt = 0;
  r.run(0.2f);
  CHECK(r.c.gripLimit == r.P.qmax[GRIP], "opening resets the grip limit");
  // without contact the gripper closes fully
  r.in.rt = 1.0f;
  r.run(3);
  r.in.rt = 0;
  CHECK(r.c.q[GRIP] > 99, "free close reaches 100 (%.1f)", r.c.q[GRIP]);
}

// ---------------- text commands ----------------
static void testTextCommands() {
  Rig r;
  CHECK(!strncmp(r.text("MOVE 200 0 100"), "error", 5), "motion refused while off");
  CHECK(!strncmp(r.text("on"), "ok", 2), "ON (lower case)");
  r.run(0.5f);
  CHECK(!strncmp(r.text("MOVE 200 50 120 -45"), "ok", 2), "MOVE reachable: %s", r.text("STATUS"));
  r.run(5);
  const kin::Pose p = r.c.pose();
  CHECK(fabsf(p.x - 200) < 0.2f && fabsf(p.y - 50) < 0.2f && fabsf(p.z - 120) < 0.2f && fabsf(p.pitch + 45) < 0.2f,
        "MOVE arrived at %.1f %.1f %.1f pitch %.1f", p.x, p.y, p.z, p.pitch);
  CHECK(!strncmp(r.text("UP 30"), "ok", 2), "UP");
  r.run(4);
  CHECK(fabsf(r.c.pose().z - 150) < 0.3f, "UP 30 -> z %.1f", r.c.pose().z);
  CHECK(strstr(r.text("MOVE 700 0 100"), "out of reach") != nullptr, "far MOVE rejected");
  CHECK(strstr(r.text("MOVE 200 0 0 -90"), "z_min") != nullptr, "MOVE into the table rejected");
  CHECK(!strncmp(r.text("J 2 175"), "error", 5), "J beyond the limit rejected");
  CHECK(!strncmp(r.text("J 7 0"), "error", 5), "joint 7 rejected");
  CHECK(!strncmp(r.text("J 2 abc"), "error", 5), "bad number rejected");
  CHECK(!strncmp(r.text("GRIP CLOSE"), "ok", 2), "GRIP CLOSE");
  r.run(3);
  CHECK(r.c.q[GRIP] > 99, "gripper closed");
  CHECK(!strncmp(r.text("OPEN"), "ok", 2), "OPEN");
  CHECK(!strcmp(r.text("set j1_us0 1500"), "(unknown)"), "CLI commands are passed through");
  CHECK(!strcmp(r.text("SAVE PARAMS"), "(unknown)"), "SAVE PARAMS goes to the CLI");
  CHECK(!strncmp(r.text("SAVE"), "ok", 2) && r.c.saveRequest, "SAVE = waypoints");
  CHECK(strstr(r.text("STATUS"), "ON") != nullptr, "STATUS");
  CHECK(!strncmp(r.text("SPEED 1"), "ok", 2) && r.c.speedLvl == 1, "SPEED");
  CHECK(!strncmp(r.text("OFF"), "ok", 2) && r.c.activity == ACT_PARKING, "OFF parks");
}

// ---------------- parameters ----------------
static void testParams() {
  Params P;
  paramsDefaults(P);
  const ParamInfo* pi = paramFind("j3_usdeg");
  CHECK(pi && paramPtr(P, *pi) == &P.usdeg[2], "j3_usdeg maps to usdeg[2]");
  CHECK(paramFind("j6_park") && paramPtr(P, *paramFind("j6_park")) == &P.park[5], "j6_park");
  CHECK(paramSet(P, "l2", 110) && P.l2 == 110, "set l2");
  CHECK(!paramSet(P, "l2", -5) && P.l2 == 110, "out-of-range value refused");
  CHECK(!paramSet(P, "nope", 1), "unknown name refused");
  size_t n;
  const ParamInfo* t = paramTable(n);
  bool unique = true;
  for (size_t i = 0; i < n; ++i)
    for (size_t k = i + 1; k < n; ++k)
      if (!strcmp(t[i].name, t[k].name)) unique = false;
  CHECK(unique && n == 6 * 8 + 23, "table: %zu unique names", n);
  CHECK(P.qmin[1] <= P.home[1] && P.home[2] <= P.qmax[2], "home inside limits");
  bool parkOk = true;
  for (int j = 0; j < NJ; ++j) parkOk = parkOk && P.park[j] >= P.qmin[j] && P.park[j] <= P.qmax[j];
  CHECK(parkOk, "park inside limits");
  const kin::Pose pk = kin::forward({P.d1, P.l2, P.l3, P.l4}, P.park);
  CHECK(pk.z > P.z_min, "park pose above the table (z %.1f)", pk.z);
}

int main() {
  testForwardHome();
  testIkRoundTrip();
  testIkRejects();
  testShapeAndQuintic();
  testTrackerLimits();
  testTrackerNoOvershoot();
  testEnableAndPark();
  testJointJog();
  testCartesianJog();
  testCartesianBoundary();
  testTeachPlayback();
  testOverload();
  testEmergencyStop();
  testGripDetect();
  testTextCommands();
  testParams();
  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
