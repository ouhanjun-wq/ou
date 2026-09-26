// Host-side unit tests for the hardware-independent arm code.
//   g++ -std=gnu++11 -O1 -Wall -Wextra -Werror -I firmware/arm_uno firmware/tests/test_core.cpp -o test_core
//   ./test_core
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "commands.h"
#include "pgm.h"
#include "kinematics.h"
#include "motion.h"
#include "params.h"
#include "pca9685.h"

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

// RAM waypoint store (the Uno uses EEPROM).
class RamStore : public WaypointStore {
 public:
  float q[60][NJ];
  int n = 0;
  int count() const override { return n; }
  int capacity() const override { return 60; }
  void get(int i, float* out) const override { memcpy(out, q[i], sizeof(q[i])); }
  bool append(const float* in) override {
    if (n >= 60) return false;
    memcpy(q[n++], in, sizeof(q[0]));
    return true;
  }
  void removeLast() override { if (n) --n; }
  void clear() override { n = 0; }
};

// Collects the reply lines; lastReply = the last complete line.
static char lastReply[200];
static char allReplies[600];
class BufOut : public Out {
 public:
  char cur[200] = "";
  void add(const char* s) { strncat(cur, s, sizeof(cur) - strlen(cur) - 1); }
  void text(const char* s) override { add(s); }
  void num(long v) override { char b[24]; snprintf(b, sizeof(b), "%ld", v); add(b); }
  void dec(float v, uint8_t d) override { char b[32]; snprintf(b, sizeof(b), "%.*f", d, v); add(b); }
  void end() override {
    snprintf(lastReply, sizeof(lastReply), "%s", cur);
    strncat(allReplies, cur, sizeof(allReplies) - strlen(allReplies) - 2);
    strcat(allReplies, "\n");
    cur[0] = 0;
  }
};
static BufOut bufOut;
static int saves = 0;
static bool fakeSave(const Params&) { ++saves; return true; }

struct Rig {
  Params P = defaults();
  RamStore store;
  ArmCore c;
  PadInput in;
  Sensors s;
  Rig() : c(P, store) { c.reset(); s.ok = true; s.volts = 6.0f; }
  void run(float seconds) {
    const int n = (int)lroundf(seconds / DT);
    for (int i = 0; i < n; ++i) c.update(in, s, DT);
  }
  void tap(PadButton b, float hold = 0.1f) {
    in.valid = true;
    in.btn[b] = true;
    run(hold);
    in.btn[b] = false;
    run(0.1f);
  }
  const char* text(const char* line) {
    char buf[128];
    snprintf(buf, sizeof(buf), "%s", line);
    lastReply[0] = 0;
    allReplies[0] = 0;
    runCommand(c, s, buf, bufOut, fakeSave);
    return lastReply;
  }
  void on() { in.valid = true; c.enable(); run(0.5f); }
};

static kin::Geometry geoOf(const Params& P) { return {(float)P.d1, (float)P.l2, (float)P.l3, (float)P.l4}; }
struct F6 {
  float v[NJ];
  explicit F6(const int16_t* a) { for (int j = 0; j < NJ; ++j) v[j] = a[j]; }
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
  const kin::Geometry g = geoOf(P);
  const kin::Pose p = kin::forward(g, F6(P.home).v);
  CHECK(fabsf(p.x - 310) < 0.01f && fabsf(p.y) < 0.01f && fabsf(p.z - 195) < 0.01f && fabsf(p.pitch) < 0.01f,
        "home pose %.2f %.2f %.2f pitch %.2f (want 310 0 195 0)", p.x, p.y, p.z, p.pitch);
  const float left[5] = {90, 90, -90, 0, 0};
  const kin::Pose l = kin::forward(g, left);
  CHECK(fabsf(l.x) < 0.01f && fabsf(l.y - 310) < 0.01f, "J1 +90 turns left (y+): %.2f %.2f", l.x, l.y);
}

static void testIkRoundTrip() {
  Params P;
  paramsDefaults(P);
  const kin::Geometry g = geoOf(P);
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
  const kin::Geometry g = geoOf(P);
  float s[5];
  CHECK(kin::inverse(g, {600, 0, 100, 0, 0}, P.r_min, s) == kin::UNREACHABLE, "far pose must be unreachable");
  CHECK(kin::inverse(g, {20, 10, 100, -90, 0}, P.r_min, s) == kin::TOO_CLOSE, "pose at the base axis rejected");
  CHECK(kin::inverse(g, {220, 0, 40, -60, 0}, P.r_min, s) == kin::OK, "pick pose (tool tilted down) reachable");
}

// ---------------- motion primitives ----------------
static void testShapeAndQuintic() {
  CHECK(shape(0.05f, 8, 30) == 0, "inside dead zone");
  CHECK(fabsf(shape(1.0f, 8, 30) - 1.0f) < 1e-5f && fabsf(shape(-1.0f, 8, 30) + 1.0f) < 1e-5f,
        "full deflection = +-1");
  CHECK(shape(0.5f, 8, 30) < 0.5f && shape(0.5f, 8, 30) > 0.3f, "expo softens the middle");
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
  const float sp = speedScale(r.c.speedLvl);
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
  r.tap(PB_MENU);
  CHECK(r.c.power && r.c.state == ST_ON, "Menu turns servo power on");
  CHECK(maxAbsDiff(r.c.q, F6(r.P.park).v, NJ) < 1e-3f, "enable starts at the park pose");
  r.tap(PB_Y);
  r.run(4);
  CHECK(maxAbsDiff(r.c.q, F6(r.P.home).v, NJ) < 0.01f, "Y goes home");
  r.tap(PB_MENU);
  CHECK(r.c.activity == ACT_PARKING && r.c.power, "Menu parks first");
  r.run(6);
  CHECK(!r.c.power && r.c.state == ST_OFF, "power off after parking");
  CHECK(maxAbsDiff(r.c.q, F6(r.P.park).v, NJ) < 0.6f, "arm is at the park pose when power goes off");
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
  r.tap(PB_Y);
  r.run(4);
  r.tap(PB_VIEW);
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
  r.tap(PB_Y);
  r.run(4);
  r.tap(PB_VIEW);
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
  const float pts[3][NJ] = {{0, 90, -90, 0, 0, 0}, {40, 70, -60, -30, 20, 80}, {-30, 100, -120, 10, -20, 10}};
  float want[3][NJ];
  for (int i = 0; i < 3; ++i) {
    CHECK(r.c.moveJoints(pts[i]), "move to waypoint %d", i);
    r.run(5);
    memcpy(want[i], r.c.q, sizeof(want[i]));
    r.tap(PB_A);                   // A (short) records
  }
  CHECK(r.store.n == 3, "3 waypoints recorded (%d)", r.store.n);
  r.tap(PB_X);                     // X (short) plays once
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
  r.in.btn[PB_X] = true;
  r.run(1.2f);
  r.in.btn[PB_X] = false;
  r.run(0.5f);
  CHECK(r.c.activity == ACT_PLAY && r.c.loop, "hold X = loop playback");
  r.run(20);
  CHECK(r.c.activity == ACT_PLAY, "loop keeps playing");
  r.in.rx = 0.8f;
  r.run(0.1f);
  r.in.rx = 0;
  CHECK(r.c.activity == ACT_IDLE, "a stick takes over from playback");
  // hold A = clear
  r.in.btn[PB_A] = true;
  r.run(2.2f);
  r.in.btn[PB_A] = false;
  r.run(0.1f);
  CHECK(r.store.n == 0, "hold A clears the waypoints (%d)", r.store.n);
}

// ---------------- protection ----------------
static void testEmergencyStop() {
  Rig r;
  r.on();
  r.s.volts = 0.2f;                     // mushroom switch pressed
  r.run(0.3f);
  CHECK(r.c.state == ST_ESTOP && !r.c.power, "servo supply lost -> E-STOP, pulses off");
  r.s.volts = 6.0f;
  r.run(0.5f);
  CHECK(!r.c.power, "releasing the E-stop does not restart the servos by itself");
  r.tap(PB_MENU);
  CHECK(r.c.power && r.c.state == ST_ON, "Menu re-enables after E-STOP");
  r.P.estop_mv = 0;                     // detection switched off (USB-only bench test)
  r.s.volts = 0;
  r.run(1);
  CHECK(r.c.state == ST_ON, "estop_mv = 0 disables the detection");
}

static void testGripBackoff() {
  Rig r;
  r.on();
  r.in.rt = 1.0f;                       // close
  r.run(0.4f);
  const float closing = r.c.qt[GRIP];
  CHECK(closing > 20, "RT closes the gripper (%.1f)", closing);
  r.in.rt = 0;                          // release: back off a little
  r.run(0.1f);
  CHECK(fabsf(r.c.qt[GRIP] - (closing - r.P.grip_backoff)) < 0.01f, "release backs off %d %% (%.1f -> %.1f)",
        r.P.grip_backoff, closing, r.c.qt[GRIP]);
  r.run(1);
  CHECK(fabsf(r.c.qt[GRIP] - (closing - r.P.grip_backoff)) < 0.01f, "backoff happens only once");
  r.in.rt = 1.0f;
  r.run(3);
  r.in.rt = 0;
  r.run(0.5f);
  CHECK(r.c.q[GRIP] > 100 - r.P.grip_backoff - 0.1f && r.c.q[GRIP] < 100, "full close then backoff (%.1f)",
        r.c.q[GRIP]);
}

static void testCalibration() {
  Rig r;
  r.on();
  CHECK(!strncmp(r.text("PULSE 2 1520"), "ok", 2), "PULSE");
  CHECK(fabsf(r.c.pulseUs(1) - 1520) < 0.01f, "raw pulse goes out as-is");
  CHECK(!strncmp(r.text("MARK 2 90"), "ok", 2), "first MARK");
  r.text("PULSE 2 1020");
  CHECK(strstr(r.text("MARK 2 45"), "calibrated") != nullptr, "second MARK calibrates: %s", lastReply);
  CHECK(fabsf(r.P.usdeg[1] - 500.0f / 45.0f) < 1e-3f, "usdeg %.4f", r.P.usdeg[1]);
  CHECK(fabsf(r.P.us0[1] - (1520 - 90 * 500.0f / 45.0f)) < 0.01f, "us0 %.2f", r.P.us0[1]);
  CHECK(fabsf(r.c.q[1] - 45) < 0.01f, "joint synced to 45 deg");
  r.text("PULSE OFF");
  CHECK(fabsf(r.c.pulseUs(1) - 1020) < 0.05f, "same pulse after PULSE OFF (%.2f)", r.c.pulseUs(1));
  r.text("PULSE 3 1500");
  CHECK(strstr(r.text("MARK 3 -90"), "ok") != nullptr, "mark J3");
  CHECK(strstr(r.text("MARK 3 -85"), "calibrated") == nullptr, "marks < 20 deg apart are not used");
  r.text("PULSE 3 1510");
  CHECK(strstr(r.text("MARK 3 -45"), "not plausible") != nullptr, "implausible slope rejected");
  r.text("PULSE OFF");
  CHECK(!strncmp(r.text("SAVE"), "ok", 2) && saves == 1, "SAVE calls the save hook");
  Rig z;                                // CENTER before ON: the first pulses are 1500 us
  z.text("CENTER");
  z.text("ON");
  z.run(0.2f);
  bool centred = true;
  for (int j = 0; j < NJ; ++j) centred = centred && z.c.pulseUs(j) == 1500;
  CHECK(centred && z.c.power, "CENTER + ON sends 1500 us to every servo");
  z.text("PULSE OFF");
  CHECK(fabsf(z.c.pulseUs(1) - 1500) < 0.05f && fabsf(z.c.q[1] - 90) < 0.05f, "PULSE OFF keeps them (J2 %.2f deg)",
        z.c.q[1]);
  CHECK(!strncmp(r.text("DEFAULTS"), "error", 5), "DEFAULTS refused while on");
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
  CHECK(!strncmp(r.text("MOVE 220 0 40 -60"), "ok", 2), "documented pick pose: %s", lastReply);
  CHECK(strstr(r.text("MOVE 700 0 100"), "out of reach") != nullptr, "far MOVE rejected");
  CHECK(strstr(r.text("MOVE 200 0 0 -90"), "z_min") != nullptr, "MOVE into the table rejected");
  CHECK(!strncmp(r.text("J 2 175"), "error", 5), "J beyond the limit rejected");
  CHECK(!strncmp(r.text("J 7 0"), "error", 5), "joint 7 rejected");
  CHECK(!strncmp(r.text("J 2 abc"), "error", 5), "bad number rejected");
  CHECK(!strncmp(r.text("GRIP CLOSE"), "ok", 2), "GRIP CLOSE");
  r.run(3);
  CHECK(r.c.q[GRIP] > 99, "gripper closed");
  CHECK(!strncmp(r.text("OPEN"), "ok", 2), "OPEN");
  CHECK(strstr(r.text("frobnicate"), "unknown") != nullptr, "unknown command");
  CHECK(!strncmp(r.text("LIM 3 -150 0"), "ok", 2) && r.P.qmin[2] == -150 && r.P.qmax[2] == 0, "LIM");
  CHECK(!strncmp(r.text("GEO 78 104 98 125"), "ok", 2) && r.P.l4 == 125, "GEO");
  r.text("GEO 75 105 100 120");
  CHECK(!strncmp(r.text("POSE PARK HERE"), "ok", 2) && r.P.park[5] == (int)lroundf(r.c.q[5]), "POSE PARK HERE");
  CHECK(!strncmp(r.text("mode xyz"), "ok", 2) && r.c.mode == CART_MODE, "MODE XYZ");
  r.text("STATUS");
  CHECK(strstr(allReplies, "XYZ") != nullptr && strstr(allReplies, "6.00 V") != nullptr, "STATUS: %s", allReplies);
  CHECK(!strncmp(r.text("SPEED 1"), "ok", 2) && r.c.speedLvl == 1, "SPEED");
  CHECK(!strncmp(r.text("OFF"), "ok", 2) && r.c.activity == ACT_PARKING, "OFF parks");
}

// ---------------- parameters ----------------
static void testParams() {
  Params P;
  paramsDefaults(P);
  CHECK(sizeof(Params) <= 250, "Params fits the EEPROM slot (%u bytes)", (unsigned)sizeof(Params));
  bool homeOk = true, parkOk = true;
  for (int j = 0; j < NJ; ++j) {
    homeOk = homeOk && P.home[j] >= P.qmin[j] && P.home[j] <= P.qmax[j];
    parkOk = parkOk && P.park[j] >= P.qmin[j] && P.park[j] <= P.qmax[j];
  }
  CHECK(homeOk && parkOk, "home and park inside limits");
  float pk[NJ];
  for (int j = 0; j < NJ; ++j) pk[j] = P.park[j];
  const kin::Pose p = kin::forward({(float)P.d1, (float)P.l2, (float)P.l3, (float)P.l4}, pk);
  CHECK(p.z > P.z_min, "park pose above the table (z %.1f)", p.z);
  float v = 0;
  CHECK(parseNum("-12.5", v) && v == -12.5f && parseNum("300", v) && v == 300 && parseNum("+7", v) && v == 7,
        "parseNum numbers");
  CHECK(!parseNum("abc", v) && !parseNum("1.2.3", v) && !parseNum("-", v) && !parseNum("", v), "parseNum rejects");
}

static void testServoDriver() {
  CHECK(pca::usToCount(1500, 25000) == 307, "1500 us = 307 counts at 25 MHz (%u)", pca::usToCount(1500, 25000));
  CHECK(pca::usToCount(0, 25000) == 0 && pca::usToCount(30000, 25000) == 4095, "counts clamp to 0..4095");
  CHECK(labs(pca::oscFromFrameHz(50.03f) - 25000) <= 2, "nominal frame rate = 25 MHz");
  Rig r;
  CHECK(!strncmp(r.text("OSC 53.5"), "ok", 2), "OSC");
  const float us = pca::usToCount(1500, r.P.osc_khz) * 1e6f / (4096.0f * 53.5f);   // what the servo sees
  CHECK(fabsf(us - 1500) < 4, "after OSC the pulse is right on a fast clone (%.1f us)", us);
  const uint16_t keep = r.P.osc_khz;
  CHECK(!strncmp(r.text("OSC 5"), "error", 5) && r.P.osc_khz == keep, "OSC rejects implausible readings");
  Params P;
  paramsDefaults(P);
  CHECK(P.osc_khz == 25000 && P.vdiv_x100 == 500, "defaults: 25 MHz, voltage sensor module 5:1");
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
  testEmergencyStop();
  testGripBackoff();
  testCalibration();
  testTextCommands();
  testParams();
  testServoDriver();
  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
