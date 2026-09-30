// The hexapod's brain: states (relaxed / standing up / ready / sitting down), glove input with a
// link timeout, velocity smoothing, gait, body tilt, leg IK and servo pulses. Pure C++, no
// hardware: the sketch feeds it inputs and sends `us[]` to the servos.
#pragma once
#include <math.h>
#include <stdint.h>

#include "gait.h"
#include "leg_ik.h"
#include "params.h"
#include "protocol.h"

namespace hx {

constexpr uint32_t LINK_TIMEOUT_MS = 300;    // no glove packet for this long: stand still
constexpr uint32_t LOWBAT_DELAY_MS = 3000;

// What the robot is asked to do, normalised to -1 .. 1.
struct Input {
  uint8_t mode = MODE_STOP;
  float x = 0, y = 0, turn = 0;   // walking
  float pitch = 0, roll = 0;      // BODY mode
};

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

inline float approach(float v, float target, float step) {
  if (v < target) return v + step < target ? v + step : target;
  return v - step > target ? v - step : target;
}

// Signed difference of two millis() values, correct across the 49-day wrap.
inline int32_t msSince(uint32_t now, uint32_t then) { return (int32_t)(now - then); }

class Robot {
 public:
  Params* P = nullptr;
  uint8_t state = RS_RELAXED;
  Gait gait;

  // outputs
  float q[NSERVO];          // joint angles (deg)
  float us[NSERVO];         // servo pulses (us)
  bool servosOn = false;
  bool limited = false;     // a joint hit its limit or a foot was out of reach this tick

  // sensors (set by the sketch)
  uint16_t vbatMv = 0;      // 0 = no voltage sensor
  uint16_t distMm = 0;      // 0 = no HC-SR04 / no echo

  // calibration: CAL puts every joint at 0 (or at calQ) with the servos on
  bool cal = false;
  float calQ[NSERVO];

  bool lowBattery = false;  // latched until reset
  bool obstacle = false;
  float height = 0;         // current body height (mm)
  float pitch = 0, roll = 0;          // current body tilt (deg)
  float cx = 0, cy = 0, ct = 0;       // smoothed command (-1 .. 1)
  Input in;                 // last input (cleared when it expires)

  void begin(Params* p) {
    P = p;
    state = RS_RELAXED;
    servosOn = false;
    cal = false;
    for (int s = 0; s < NSERVO; ++s) calQ[s] = 0;
    height = P->sit_height;
    pitch = roll = 0;
    cx = cy = ct = 0;
    gait.nextGait = P->gait;
    homes(home_);
    gait.reset(home_);
    compute();
  }

  // --- inputs -----------------------------------------------------------------------------

  // Input valid until now + hold_ms (glove: LINK_TIMEOUT_MS, serial WALK: its duration).
  void setInput(const Input& i, uint32_t now, uint32_t hold_ms) {
    in = i;
    inUntil_ = now + hold_ms;
    haveInput_ = true;
  }

  void onGlove(const GlovePacket& g, uint32_t now) {
    Input i;
    const bool armed = (g.flags & GF_ARMED) != 0;
    i.mode = armed ? g.mode : (uint8_t)MODE_STOP;
    i.x = clampf(g.x / 100.0f, -1, 1);
    i.y = clampf(g.y / 100.0f, -1, 1);
    i.turn = clampf(g.turn / 100.0f, -1, 1);
    i.pitch = clampf(g.pitch / 100.0f, -1, 1);
    i.roll = clampf(g.roll / 100.0f, -1, 1);
    // Events: act once per new event_seq. The first packet after a link loss only syncs the
    // counter, so a reconnecting glove does not replay its last event.
    const bool fresh = !gloveSeen_ || msSince(now, lastGloveMs_) > 1000;
    if (!fresh && g.event_seq != lastEventSeq_ && armed) {
      if (g.event == EV_STAND_TOGGLE) standToggle();
      if (g.event == EV_NEXT_GAIT) nextGait();
    }
    lastEventSeq_ = g.event_seq;
    gloveSeen_ = true;
    lastGloveMs_ = now;
    setInput(i, now, LINK_TIMEOUT_MS);
  }

  bool linkOk(uint32_t now) const { return gloveSeen_ && msSince(now, lastGloveMs_) < (int32_t)LINK_TIMEOUT_MS; }

  // --- actions ----------------------------------------------------------------------------

  void standUp() {
    if (lowBattery) return;
    cal = false;
    if (state == RS_RELAXED || state == RS_SITTING_DOWN) {
      if (state == RS_RELAXED) {
        height = P->sit_height;
        pitch = roll = 0;
        homes(home_);
        gait.reset(home_);
      }
      state = RS_STANDING_UP;
      stateT_ = 0;
    }
  }

  void sitDown() {
    if (state == RS_READY || state == RS_STANDING_UP) {
      state = RS_SITTING_DOWN;
      stateT_ = 0;
    }
  }

  void standToggle() {
    if (state == RS_RELAXED || state == RS_SITTING_DOWN) standUp();
    else sitDown();
  }

  void relax() {
    state = RS_RELAXED;
    cal = false;
    cx = cy = ct = 0;
  }

  void calibrate() {
    state = RS_RELAXED;
    cal = true;
    for (int s = 0; s < NSERVO; ++s) calQ[s] = 0;
  }

  void nextGait() { gait.nextGait = (uint8_t)((gait.nextGait + 1) % GAIT_COUNT); }
  void setGait(uint8_t g) { gait.nextGait = g < GAIT_COUNT ? g : 0; }

  // --- control loop -------------------------------------------------------------------------

  void update(float dt, uint32_t now) {
    if (haveInput_ && msSince(now, inUntil_) >= 0) {
      in = Input();
      haveInput_ = false;
    }
    batteryCheck(now);

    // velocity command, smoothed
    float tx = 0, ty = 0, tt = 0;
    const bool moveMode = in.mode == MODE_WALK || in.mode == MODE_CRAB;
    if (state == RS_READY && moveMode) {
      tx = in.x;
      ty = in.y;
      tt = in.turn;
    }
    obstacle = P->obstacle_mm > 0 && distMm > 0 && distMm < P->obstacle_mm;
    if (obstacle && tx > 0) tx = 0;
    const float a = P->accel * dt;
    cx = approach(cx, tx, a);
    cy = approach(cy, ty, a);
    ct = approach(ct, tt, a);

    // body tilt (BODY mode), at most 40 deg/s
    float tp = 0, tr = 0;
    if (state == RS_READY && in.mode == MODE_BODY) {
      tp = in.pitch * P->body_tilt;
      tr = in.roll * P->body_tilt;
    }
    pitch = approach(pitch, tp, 40 * dt);
    roll = approach(roll, tr, 40 * dt);

    stateT_ += dt;
    const float rise = (P->height - P->sit_height) / 1.2f * dt;   // stand up / sit in 1.2 s
    switch (state) {
      case RS_RELAXED:
        servosOn = cal;
        break;
      case RS_STANDING_UP:
        servosOn = true;
        if (stateT_ > 0.4f) height = approach(height, P->height, rise);   // first 0.4 s: legs into place
        if (height == P->height) state = RS_READY;
        break;
      case RS_READY:
        servosOn = true;
        height = approach(height, P->height, rise);   // follows HEIGHT changes
        break;
      case RS_SITTING_DOWN:
        servosOn = true;
        if (!gait.running && pitch == 0 && roll == 0) {
          height = approach(height, P->sit_height, rise);
          if (height == P->sit_height) {
            if (holdT_ < 0) holdT_ = stateT_;
            if (stateT_ - holdT_ > 0.3f) {
              state = RS_RELAXED;
              holdT_ = -1;
            }
          }
        }
        break;
    }
    if (state != RS_SITTING_DOWN) holdT_ = -1;

    homes(home_);
    const float vmax = gait.vmax(*P);
    float vx = cx * vmax, vy = cy * vmax, w = ct * vmax / reachRadius();
    // keep every foot's stride within `stride`
    float worst = 0;
    for (int i = 0; i < NLEG; ++i) {
      const float ux = vx - w * home_[i].y, uy = vy + w * home_[i].x;
      const float u = sqrtf(ux * ux + uy * uy);
      if (u > worst) worst = u;
    }
    if (worst > vmax) {
      const float k = vmax / worst;
      vx *= k;
      vy *= k;
      w *= k;
    }
    if (state == RS_RELAXED) gait.reset(home_);
    else gait.update(*P, home_, vx, vy, w, dt);
    compute();
  }

  StatusPacket status() const {
    StatusPacket s;
    memset(&s, 0, sizeof(s));
    s.state = state;
    s.gait = gait.nextGait;
    s.vbat_mv = vbatMv;
    s.flags = (uint8_t)((lowBattery || lowNow_ ? SF_LOW_BATTERY : 0) | (obstacle ? SF_OBSTACLE : 0) |
                        (limited ? SF_LIMIT : 0));
    return s;
  }

  const Vec3& home(int leg) const { return home_[leg]; }

  // Servo pulse for joint angle `deg` of servo s (clamped to the pulse limits).
  float pulseFor(int s, float deg) const {
    const float v = 1500.0f + P->trim[s] + P->dir[s] * P->us_per_deg * deg;
    return clampf(v, P->pulse_min, P->pulse_max);
  }

 private:
  Vec3 home_[NLEG];
  uint32_t inUntil_ = 0;
  bool haveInput_ = false;
  bool gloveSeen_ = false;
  uint32_t lastGloveMs_ = 0;
  uint8_t lastEventSeq_ = 0;
  float stateT_ = 0;
  float holdT_ = -1;
  uint32_t lowSince_ = 0;
  bool lowNow_ = false;

  // Home position of each foot (body frame, xy): `reach` straight out from its coxa.
  void homes(Vec3 h[NLEG]) const {
    for (int i = 0; i < NLEG; ++i)
      h[i] = legToBody(Vec3{P->reach, 0, 0}, P->mount[i][0], P->mount[i][1], P->mount[i][2]);
  }

  float reachRadius() const {
    float r = 1;
    for (int i = 0; i < NLEG; ++i) {
      const float d = sqrtf(home_[i].x * home_[i].x + home_[i].y * home_[i].y);
      if (d > r) r = d;
    }
    return r;
  }

  void batteryCheck(uint32_t now) {
    lowNow_ = vbatMv > 1000 && vbatMv < P->vbat_warn_mv;
    if (vbatMv > 1000 && vbatMv < P->vbat_cut_mv) {
      if (lowSince_ == 0) lowSince_ = now | 1;
      if (msSince(now, lowSince_) > (int32_t)LOWBAT_DELAY_MS && !lowBattery) {
        lowBattery = true;
        sitDown();
      }
    } else {
      lowSince_ = 0;
    }
  }

  // Feet -> body tilt -> leg frames -> IK -> joint limits -> pulses.
  void compute() {
    limited = false;
    const float cr = cosf(-roll / DEG), sr = sinf(-roll / DEG);
    const float cp = cosf(-pitch / DEG), sp = sinf(-pitch / DEG);
    for (int i = 0; i < NLEG; ++i) {
      float qq[3];
      if (cal) {
        for (int j = 0; j < 3; ++j) qq[j] = calQ[i * 3 + j];
      } else {
        // foot relative to the body centre, ground-aligned frame
        const Vec3 f = gait.foot[i];
        const float x = f.x, y = f.y, z = f.z - height;
        // p_body = R_y(-pitch) R_x(-roll) p   (body pitched nose-down / rolled right-down)
        const float y1 = y * cr - z * sr, z1 = y * sr + z * cr;
        const float x2 = x * cp + z1 * sp, z2 = -x * sp + z1 * cp;
        const Vec3 pl = bodyToLeg(Vec3{x2, y1, z2}, P->mount[i][0], P->mount[i][1], P->mount[i][2]);
        if (!legIK(LegGeo{P->l1, P->l2, P->l3}, pl, qq)) limited = true;
        for (int j = 0; j < 3; ++j) {
          const float c = clampf(qq[j], P->qmin[j], P->qmax[j]);
          if (c != qq[j]) limited = true;
          qq[j] = c;
        }
      }
      for (int j = 0; j < 3; ++j) {
        const int s = i * 3 + j;
        q[s] = qq[j];
        us[s] = pulseFor(s, qq[j]);
      }
    }
  }
};

}  // namespace hx
