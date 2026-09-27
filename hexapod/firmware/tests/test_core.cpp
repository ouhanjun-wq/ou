// Host-side unit tests for the hardware-independent code of the robot and the glove.
//   g++ -std=c++17 -O1 -Wall -Wextra -Werror -I hexapod/firmware/hexapod -I hexapod/firmware/glove
//       hexapod/firmware/tests/test_core.cpp -o test_core && ./test_core
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>

#include "commands.h"
#include "gait.h"
#include "gesture.h"
#include "leg_ik.h"
#include "params.h"
#include "protocol.h"
#include "robot.h"

using namespace hx;

static int failures = 0, checks = 0;
#define CHECK(cond, ...)                          \
  do {                                            \
    ++checks;                                     \
    if (!(cond)) {                                \
      ++failures;                                 \
      printf("FAIL %s:%d  ", __FILE__, __LINE__); \
      printf(__VA_ARGS__);                        \
      printf("\n");                               \
    }                                             \
  } while (0)

constexpr float DT = 0.02f;   // 50 Hz, as on the ESP32

struct BufOut : Out {
  std::string s;
  void write(const char* t) override { s += t; }
};

static Params defaults() {
  Params p;
  paramsDefaults(p);
  return p;
}

// Runs the robot for `sec` seconds; `now` advances with it.
static void run(Robot& r, uint32_t& now, float sec) {
  for (int k = 0; k < (int)lroundf(sec / DT); ++k) {
    now += 20;
    r.update(DT, now);
  }
}

static void standUp(Robot& r, uint32_t& now) {
  r.standUp();
  run(r, now, 2.0f);
}

static Input walk(float x, float y, float turn) {
  Input in;
  in.mode = MODE_WALK;
  in.x = x;
  in.y = y;
  in.turn = turn;
  return in;
}

// ------------------------------------------------------------------------------ protocol
static void testProtocol() {
  CHECK(sizeof(GlovePacket) == 15, "glove packet %d bytes", (int)sizeof(GlovePacket));
  CHECK(sizeof(StatusPacket) == 11, "status packet %d bytes", (int)sizeof(StatusPacket));
  GlovePacket g;
  memset(&g, 0, sizeof(g));
  g.mode = MODE_CRAB;
  g.x = 55;
  g.y = -20;
  seal(g, PKT_GLOVE, 7, 42);
  GlovePacket o;
  const uint8_t* b = reinterpret_cast<const uint8_t*>(&g);
  CHECK(open(b, sizeof(g), PKT_GLOVE, 7, o) && o.x == 55 && o.y == -20 && o.h.seq == 42, "round trip");
  CHECK(!open(b, sizeof(g), PKT_GLOVE, 8, o), "other link id rejected");
  CHECK(!open(b, sizeof(g) - 1, PKT_GLOVE, 7, o), "short frame rejected");
  StatusPacket st;
  CHECK(!open(b, sizeof(g), PKT_STATUS, 7, st), "wrong type rejected");
  uint8_t bad[sizeof(g)];
  memcpy(bad, &g, sizeof(g));
  bad[7] ^= 0x10;
  CHECK(!open(bad, sizeof(g), PKT_GLOVE, 7, o), "corrupted frame rejected");
}

// ------------------------------------------------------------------------------ kinematics
static void testIK() {
  const LegGeo g{28, 45, 75};
  float q[3];
  CHECK(legIK(g, Vec3{28 + 45, 0, -75}, q) && fabsf(q[0]) < 0.01f && fabsf(q[1]) < 0.01f && fabsf(q[2]) < 0.01f,
        "calibration pose = all zero: %.3f %.3f %.3f", q[0], q[1], q[2]);
  float worst = 0;
  srand(1);
  for (int k = 0; k < 2000; ++k) {
    const float qq[3] = {(float)(rand() % 90 - 45), (float)(rand() % 120 - 60), (float)(rand() % 110 - 55)};
    const Vec3 p = legFK(g, qq);
    float qi[3];
    if (!legIK(g, p, qi)) continue;
    const Vec3 p2 = legFK(g, qi);
    const float e = sqrtf((p.x - p2.x) * (p.x - p2.x) + (p.y - p2.y) * (p.y - p2.y) + (p.z - p2.z) * (p.z - p2.z));
    if (e > worst) worst = e;
  }
  CHECK(worst < 0.05f, "IK/FK round trip error %.4f mm", worst);
  // femur up -> foot higher; knee opens -> foot further out
  const float a[3] = {0, 20, 0}, b[3] = {0, 0, 20}, z[3] = {0, 0, 0};
  CHECK(legFK(g, a).z > legFK(g, z).z, "femur + lifts the foot");
  CHECK(legFK(g, b).x > legFK(g, z).x, "tibia + moves the foot out");
  CHECK(!legIK(g, Vec3{300, 0, 0}, q), "out of reach detected");
  const Vec3 p{37, -12, -40};
  const Vec3 l = bodyToLeg(p, 60, -40, -45);
  const Vec3 back = legToBody(l, 60, -40, -45);
  CHECK(fabsf(back.x - p.x) < 1e-3f && fabsf(back.y - p.y) < 1e-3f, "body <-> leg frame");
  const Vec3 out = bodyToLeg(Vec3{0, 150, 0}, 0, 50, 90);
  CHECK(fabsf(out.x - 100) < 1e-3f && fabsf(out.y) < 1e-3f, "left middle leg points +y");
}

// ------------------------------------------------------------------------------ robot
static void testStandSit() {
  Params p = defaults();
  Robot r;
  r.begin(&p);
  uint32_t now = 1000;
  run(r, now, 0.5f);
  CHECK(r.state == RS_RELAXED && !r.servosOn, "starts relaxed, servos off");
  standUp(r, now);
  CHECK(r.state == RS_READY && r.servosOn, "stands up within 2 s (state %d)", r.state);
  CHECK(fabsf(r.height - p.height) < 0.01f, "at standing height");
  CHECK(!r.limited, "default posture within joint limits");
  for (int s = 0; s < NSERVO; ++s) CHECK(r.us[s] >= 900 && r.us[s] <= 2100, "servo %d at %.0f us", s, r.us[s]);
  r.sitDown();
  run(r, now, 2.5f);
  CHECK(r.state == RS_RELAXED && !r.servosOn, "sits down and relaxes");
}

struct WalkStats {
  int minDown = 6;
  bool stanceWrongWay = false;
  float maxFromHome = 0;
  float meanStanceVx = 0;
  int n = 0;
};

static WalkStats walkFor(Robot& r, uint32_t& now, const Input& in, float sec, bool checkX) {
  WalkStats w;
  const float skip = 1.0f + 1.5f * r.gait.period(*r.P);
  for (int k = 0; k < (int)(sec / DT); ++k) {
    Vec3 before[NLEG];
    bool down[NLEG];
    for (int i = 0; i < NLEG; ++i) {
      before[i] = r.gait.foot[i];
      down[i] = !r.gait.air[i];
    }
    r.setInput(in, now, 300);
    now += 20;
    r.update(DT, now);
    if (k * DT < skip) continue;   // speed ramp and first step
    const int d = r.gait.feetDown();
    if (d < w.minDown) w.minDown = d;
    for (int i = 0; i < NLEG; ++i) {
      const Vec3 f = r.gait.foot[i];
      const float hx_ = f.x - r.home(i).x, hy = f.y - r.home(i).y;
      const float dist = sqrtf(hx_ * hx_ + hy * hy);
      if (dist > w.maxFromHome) w.maxFromHome = dist;
      if (down[i] && !r.gait.air[i]) {
        const float vx = (f.x - before[i].x) / DT;
        if (checkX && vx > 0.01f) w.stanceWrongWay = true;
        w.meanStanceVx += vx;
        ++w.n;
      }
    }
  }
  if (w.n) w.meanStanceVx /= w.n;
  return w;
}

static void testWalking() {
  const int needDown[GAIT_COUNT] = {3, 4, 5};
  for (uint8_t g = 0; g < GAIT_COUNT; ++g) {
    Params p = defaults();
    p.gait = g;
    Robot r;
    r.begin(&p);
    uint32_t now = 0;
    standUp(r, now);
    const WalkStats w = walkFor(r, now, walk(1, 0, 0), 9, true);
    CHECK(w.minDown >= needDown[g], "%s: at least %d feet down (min %d)", gaitName(g), needDown[g], w.minDown);
    CHECK(!w.stanceWrongWay, "%s: feet on the ground slide backwards when walking forward", gaitName(g));
    const float vmax = r.gait.vmax(p);
    CHECK(fabsf(-w.meanStanceVx - vmax) < 0.05f * vmax, "%s: body speed %.1f mm/s, expected %.1f",
          gaitName(g), -w.meanStanceVx, vmax);
    CHECK(w.maxFromHome <= p.stride / 2 + 2, "%s: feet stay within the stride (%.1f mm)", gaitName(g), w.maxFromHome);
    CHECK(!r.limited, "%s: full speed within joint limits", gaitName(g));
    // stop: the input expires, the speed ramps down, the feet return home and the cycle stops
    run(r, now, 4.0f);
    CHECK(!r.gait.running, "%s: gait stops after the input ends", gaitName(g));
    float off = 0;
    for (int i = 0; i < NLEG; ++i) {
      const float d = fabsf(r.gait.foot[i].x - r.home(i).x) + fabsf(r.gait.foot[i].y - r.home(i).y) + fabsf(r.gait.foot[i].z);
      if (d > off) off = d;
    }
    CHECK(off < 0.01f, "%s: all feet down at home (%.2f)", gaitName(g), off);
  }
}

static void testTurnAndCrab() {
  Params p = defaults();
  Robot r;
  r.begin(&p);
  uint32_t now = 0;
  standUp(r, now);
  // turn left (counter-clockwise): feet on the ground move clockwise around the centre
  bool wrong = false;
  for (int k = 0; k < 200; ++k) {
    Vec3 b[NLEG];
    for (int i = 0; i < NLEG; ++i) b[i] = r.gait.foot[i];
    bool down[NLEG];
    for (int i = 0; i < NLEG; ++i) down[i] = !r.gait.air[i];
    r.setInput(walk(0, 0, 1), now, 300);
    now += 20;
    r.update(DT, now);
    if (k < 40) continue;
    for (int i = 0; i < NLEG; ++i) {
      if (!down[i] || r.gait.air[i]) continue;
      const float cross = b[i].x * r.gait.foot[i].y - b[i].y * r.gait.foot[i].x;
      if (cross > 1e-3f) wrong = true;
    }
  }
  CHECK(!wrong, "turning left: feet on the ground rotate clockwise");
  CHECK(!r.limited, "turning within limits");
  run(r, now, 4);
  // crab right: feet on the ground slide left (+y)
  const WalkStats w = walkFor(r, now, walk(0, -1, 0), 4, false);
  CHECK(w.maxFromHome <= p.stride / 2 + 2, "crab stride %.1f", w.maxFromHome);
  bool leftward = true;
  for (int k = 0; k < 50; ++k) {
    Vec3 b = r.gait.foot[0];
    const bool d = !r.gait.air[0];
    r.setInput(walk(0, -1, 0), now, 300);
    now += 20;
    r.update(DT, now);
    if (d && !r.gait.air[0] && r.gait.foot[0].y < b.y - 1e-3f) leftward = false;
  }
  CHECK(leftward, "crab right: grounded feet slide to +y");
}

static void testGloveLink() {
  Params p = defaults();
  Robot r;
  r.begin(&p);
  uint32_t now = 5000;
  GlovePacket g;
  memset(&g, 0, sizeof(g));
  g.flags = GF_ARMED;
  g.event = EV_STAND_TOGGLE;
  g.event_seq = 9;
  r.onGlove(g, now);   // first packet: only syncs the event counter
  run(r, now, 0.1f);
  CHECK(r.state == RS_RELAXED, "first packet does not replay an old event");
  g.event_seq = 10;
  r.onGlove(g, now);
  CHECK(r.state == RS_STANDING_UP, "new event_seq -> stand up");
  r.onGlove(g, now + 20);
  run(r, now, 2);
  CHECK(r.state == RS_READY, "same event_seq -> no second toggle (state %d)", r.state);

  // disarmed glove: no events, no motion
  g.flags = 0;
  g.event_seq = 11;
  g.mode = MODE_WALK;
  g.x = 100;
  for (int k = 0; k < 50; ++k) {
    r.onGlove(g, now);
    now += 20;
    r.update(DT, now);
  }
  CHECK(r.state == RS_READY && r.cx == 0, "disarmed: ignored");

  // armed: walks; then the glove goes silent -> stops within timeout + ramp
  g.flags = GF_ARMED;
  for (int k = 0; k < 50; ++k) {
    r.onGlove(g, now);
    now += 20;
    r.update(DT, now);
  }
  CHECK(r.cx > 0.9f, "armed WALK packets drive forward (%.2f)", r.cx);
  CHECK(r.linkOk(now), "link ok");
  run(r, now, 0.3f + 1.0f / p.accel);
  CHECK(r.cx == 0 && !r.linkOk(now), "silent glove -> stop (cx %.2f)", r.cx);

  // gait change event takes effect when standing still
  g.mode = MODE_STOP;
  g.event = EV_NEXT_GAIT;
  g.event_seq = 12;
  r.onGlove(g, now);
  CHECK(r.gait.nextGait == GAIT_RIPPLE, "rock -> next gait");
  run(r, now, 3);
  CHECK(r.gait.gait == GAIT_RIPPLE, "gait applied once stopped");

  // BODY mode tilts the body, feet stay put
  g.mode = MODE_BODY;
  g.pitch = 100;
  g.roll = -50;
  for (int k = 0; k < 60; ++k) {
    r.onGlove(g, now);
    now += 20;
    r.update(DT, now);
  }
  CHECK(fabsf(r.pitch - p.body_tilt) < 0.01f && fabsf(r.roll + p.body_tilt / 2) < 0.01f, "body tilt %.1f %.1f",
        r.pitch, r.roll);
  CHECK(!r.gait.running, "BODY mode does not walk");
  CHECK(!r.limited, "full body tilt within limits");
  // nose down: the front feet are closer below the coxa plane than the rear ones
  const float qFront = r.q[0 * 3 + 1], qRear = r.q[2 * 3 + 1];
  CHECK(qFront > qRear, "nose down: front femurs higher (%.1f vs %.1f)", qFront, qRear);
}

static void testSafety() {
  Params p = defaults();
  Robot r;
  r.begin(&p);
  uint32_t now = 0;
  standUp(r, now);
  // obstacle: no forward, backward still allowed
  r.distMm = 100;
  walkFor(r, now, walk(1, 0, 0), 1, false);
  CHECK(r.cx == 0 && r.obstacle, "obstacle blocks forward");
  walkFor(r, now, walk(-1, 0, 0), 1, false);
  CHECK(r.cx < -0.9f, "backing away allowed");
  r.distMm = 0;
  run(r, now, 3);
  // battery: warn, then sit down and relax after 3 s below the cut-off
  r.vbatMv = 6600;
  run(r, now, 0.2f);
  CHECK((r.status().flags & SF_LOW_BATTERY) && r.state == RS_READY, "low battery warning only");
  r.vbatMv = 6000;
  run(r, now, 2.5f);
  CHECK(r.state == RS_READY, "short dip tolerated");
  run(r, now, 4);
  CHECK(r.lowBattery && r.state == RS_RELAXED, "flat battery -> sat down, relaxed (state %d)", r.state);
  r.standUp();
  run(r, now, 0.5f);
  CHECK(r.state == RS_RELAXED, "no standing up on a flat battery");
}

static void testPulses() {
  Params p = defaults();
  Robot r;
  r.begin(&p);
  CHECK(fabsf(r.pulseFor(0, 0) - 1500) < 0.01f, "0 deg = 1500 us");
  CHECK(fabsf(r.pulseFor(0, 45) - 2000) < 0.01f, "45 deg = 2000 us");
  p.dir[0] = -1;
  p.trim[0] = 30;
  CHECK(fabsf(r.pulseFor(0, 45) - 1030) < 0.01f, "dir -1 and trim");
  CHECK(r.pulseFor(1, 200) == p.pulse_max, "pulse clamped");
  r.calibrate();
  uint32_t now = 0;
  run(r, now, 0.1f);
  CHECK(r.servosOn && r.state == RS_RELAXED, "CAL powers the servos");
  CHECK(fabsf(r.us[1] - 1500) < 0.01f && fabsf(r.us[0] - 1530) < 0.01f, "CAL = angle 0 + trim");
}

static bool saved = false;
static bool fakeSave(const Params&) {
  saved = true;
  return true;
}

static CmdResult cmd(Robot& r, Params& p, BufOut& o, const char* s, uint32_t now = 0) {
  char b[200];
  snprintf(b, sizeof(b), "%s", s);
  Hooks h;
  h.save = fakeSave;
  o.s.clear();
  return runCommand(b, r, p, h, o, now);
}

static void testCommands() {
  Params p = defaults();
  Robot r;
  r.begin(&p);
  BufOut o;
  CHECK(cmd(r, p, o, "walk 50 0 0") == CMD_ERROR, "WALK needs STAND");
  CHECK(cmd(r, p, o, "trim 3 1 -40") == CMD_OK && p.trim[10] == -40, "TRIM");
  CHECK(cmd(r, p, o, "DIR 5 2 -1") == CMD_OK && p.dir[17] == -1, "DIR");
  CHECK(cmd(r, p, o, "DIR 5 2 3") == CMD_ERROR, "DIR only +-1");
  CHECK(cmd(r, p, o, "TRIM 6 0 10") == CMD_ERROR, "leg 6 does not exist");
  CHECK(cmd(r, p, o, "GEO 30 50 80") == CMD_OK && p.l2 == 50, "GEO");
  CHECK(cmd(r, p, o, "MOUNT 1 0 55 90") == CMD_OK && p.mount[1][1] == 55, "MOUNT");
  CHECK(cmd(r, p, o, "CHMAP 1 2 3") == CMD_ERROR, "CHMAP needs 18");
  CHECK(cmd(r, p, o, "CHMAP 13 12 14 27 26 25 33 32 15 4 16 17 5 18 19 23 2 0") == CMD_OK && p.ch[0] == 13 &&
            p.ch[17] == 0, "CHMAP");
  CHECK(cmd(r, p, o, "BACKEND gpio") == CMD_OK && p.backend == BACKEND_GPIO, "BACKEND");
  CHECK(cmd(r, p, o, "GAIT wave") == CMD_OK && r.gait.nextGait == GAIT_WAVE && p.gait == GAIT_WAVE, "GAIT");
  CHECK(cmd(r, p, o, "HEIGHT 500") == CMD_ERROR, "HEIGHT range");
  CHECK(cmd(r, p, o, "JOINT 0 1 20") == CMD_ERROR, "JOINT needs CAL");
  CHECK(cmd(r, p, o, "CAL") == CMD_OK && cmd(r, p, o, "JOINT 0 1 20") == CMD_OK && r.calQ[1] == 20, "JOINT");
  CHECK(cmd(r, p, o, "SAVE") == CMD_OK && saved, "SAVE");
  CHECK(cmd(r, p, o, "SHOW") == CMD_OK && o.s.find("MOUNT 5") != std::string::npos, "SHOW");
  CHECK(cmd(r, p, o, "FLY") == CMD_UNKNOWN, "unknown command");
  CHECK(cmd(r, p, o, "DEFAULTS") == CMD_OK && p.trim[10] == 0 && p.l2 == 45, "DEFAULTS");
  uint32_t now = 0;
  CHECK(cmd(r, p, o, "STAND") == CMD_OK, "STAND");
  run(r, now, 2);
  CHECK(cmd(r, p, o, "WALK 100 0 0 1000", now) == CMD_OK, "WALK");
  run(r, now, 0.8f);
  CHECK(r.cx > 0.9f && r.gait.running, "serial WALK moves");
  run(r, now, 3);
  CHECK(r.cx == 0 && !r.gait.running, "serial WALK ends after its duration");
  CHECK(cmd(r, p, o, "STATUS") == CMD_OK && o.s.find("READY") != std::string::npos, "STATUS");
}

// ------------------------------------------------------------------------------ glove
static GloveParams gloveP() {
  GloveParams p;
  gloveDefaults(p);
  return p;
}

static uint8_t shape(bool t, bool i, bool m, bool r, bool l) {
  const bool s[NF] = {t, i, m, r, l};
  return classify(s);
}

static void testGestures() {
  CHECK(fabsf(bendOf(2400, 2400, 1400)) < 1e-6f && fabsf(bendOf(1400, 2400, 1400) - 1) < 1e-6f, "bend 0 / 1");
  CHECK(fabsf(bendOf(1900, 2400, 1400) - 0.5f) < 1e-6f, "bend 0.5");
  CHECK(bendOf(3000, 2400, 1400) == -0.2f, "bend clamped");

  CHECK(shape(1, 1, 1, 1, 1) == G_OPEN && shape(0, 1, 1, 1, 1) == G_OPEN, "open hand");
  CHECK(shape(0, 1, 0, 0, 0) == G_POINT, "point");
  CHECK(shape(1, 1, 1, 0, 0) == G_VICTORY, "victory");
  CHECK(shape(0, 1, 0, 0, 1) == G_ROCK, "rock");
  CHECK(shape(0, 0, 0, 0, 0) == G_FIST, "fist");
  CHECK(shape(1, 0, 0, 0, 0) == G_STAND && shape(0, 0, 0, 0, 1) == G_STAND, "thumb up / little finger");
  CHECK(shape(0, 0, 1, 1, 0) == G_NONE, "unknown");

  GloveParams p = gloveP();
  Fingers f;
  float b[NF] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
  f.update(b, p);
  CHECK(f.straight[INDEX], "0.5 is inside the hysteresis band: stays straight");
  b[INDEX] = 0.7f;
  f.update(b, p);
  CHECK(!f.straight[INDEX], "0.7 -> bent");
  b[INDEX] = 0.4f;
  f.update(b, p);
  CHECK(!f.straight[INDEX], "0.4 -> still bent");
  b[INDEX] = 0.3f;
  f.update(b, p);
  CHECK(f.straight[INDEX], "0.3 -> straight");
  p.thumb = 0;
  b[THUMB] = 0;
  f.update(b, p);
  CHECK(!f.straight[THUMB], "no thumb sensor: thumb counts as bent");

  GestureFilter g;
  uint32_t t = 0;
  g.update(G_OPEN, t);
  g.update(G_OPEN, t += 100);
  CHECK(g.active == G_NONE, "open needs 150 ms");
  g.update(G_OPEN, t += 60);
  CHECK(g.active == G_OPEN, "open after 160 ms");
  g.update(G_FIST, t += 10);
  CHECK(g.active == G_FIST, "fist stops at once");
  int events = 0;
  for (int k = 0; k < 100; ++k) events += g.update(G_ROCK, t += 10);
  CHECK(events == 1 && g.event == EV_NEXT_GAIT, "rock held 1 s: one gait event (%d)", events);
  const uint8_t seq = g.eventSeq;
  for (int k = 0; k < 50; ++k) events += g.update(G_STAND, t += 10);
  CHECK(g.eventSeq == seq, "thumb up 0.5 s: nothing yet");
  for (int k = 0; k < 60; ++k) events += g.update(G_STAND, t += 10);
  CHECK(g.eventSeq == seq + 1 && g.event == EV_STAND_TOGGLE, "thumb up 1 s: stand toggle");
  for (int k = 0; k < 300; ++k) g.update(G_STAND, t += 10);
  CHECK(g.eventSeq == seq + 1, "keeping the thumb up does not repeat");
}

static void testTilt() {
  const float d = 3.14159265f / 180;
  Tilt t;
  // hand pitched 20 deg nose down: gravity (up) seen in the hand frame = (-sin, 0, cos)
  for (int k = 0; k < 400; ++k) t.update(-sinf(20 * d), 0, cosf(20 * d), 0, 0, 0.01f);
  CHECK(fabsf(t.pitch - 20) < 0.1f && fabsf(t.roll) < 0.1f, "pitch %.2f roll %.2f", t.pitch, t.roll);
  Tilt r;
  // rolled 30 deg right side down: gravity = (0, sin, cos)
  for (int k = 0; k < 400; ++k) r.update(0, sinf(30 * d), cosf(30 * d), 0, 0, 0.01f);
  CHECK(fabsf(r.roll - 30) < 0.1f, "roll %.2f", r.roll);
  // gyro integrates between accelerometer corrections
  Tilt g;
  g.update(0, 0, 1, 0, 0, 0.01f);
  for (int k = 0; k < 10; ++k) g.update(0, 0, 1, 0, 100, 0.01f);
  CHECK(g.pitch > 5, "gyro pitch rate integrates (%.2f)", g.pitch);

  CHECK(tiltToCmd(5, 8, 35, 1.5f) == 0, "dead zone");
  CHECK(tiltToCmd(35, 8, 35, 1.5f) == 1 && tiltToCmd(-60, 8, 35, 1.5f) == -1, "full scale");
  const float half = tiltToCmd(21.5f, 8, 35, 1.5f);
  CHECK(fabsf(half - powf(0.5f, 1.5f)) < 1e-4f, "expo %.3f", half);

  GloveParams p = gloveP();
  GlovePacket k;
  makeCommand(MODE_WALK, 35, 35, p, k);
  CHECK(k.x == 100 && k.turn == -100 && k.y == 0, "walk: forward, roll right = turn right");
  makeCommand(MODE_CRAB, -35, 35, p, k);
  CHECK(k.x == -100 && k.y == -100 && k.turn == 0, "crab: back, roll right = move right");
  makeCommand(MODE_BODY, 17.5f, -35, p, k);
  CHECK(k.pitch == 50 && k.roll == -100 && k.x == 0, "body mode copies tilt");
  makeCommand(MODE_STOP, 35, 35, p, k);
  CHECK(k.x == 0 && k.turn == 0, "stop sends nothing");
}

// glove -> radio bytes -> robot
static void testEndToEnd() {
  GloveParams gp = gloveP();
  GlovePacket g;
  memset(&g, 0, sizeof(g));
  makeCommand(gestureMode(G_OPEN), 30, 0, gp, g);
  g.flags = GF_ARMED;
  seal(g, PKT_GLOVE, gp.link_id, 1);

  Params p = defaults();
  Robot r;
  r.begin(&p);
  uint32_t now = 0;
  standUp(r, now);
  GlovePacket rx;
  const bool ok = open(reinterpret_cast<const uint8_t*>(&g), sizeof(g), PKT_GLOVE, p.link_id, rx);
  CHECK(ok, "robot accepts the glove's packet");
  for (int k = 0; k < 100; ++k) {
    r.onGlove(rx, now);
    now += 20;
    r.update(DT, now);
  }
  CHECK(r.cx > 0.5f && r.gait.running, "tilting the open hand forward walks the robot forward (%.2f)", r.cx);
}

int main() {
  testProtocol();
  testIK();
  testStandSit();
  testWalking();
  testTurnAndCrab();
  testGloveLink();
  testSafety();
  testPulses();
  testCommands();
  testGestures();
  testTilt();
  testEndToEnd();
  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
