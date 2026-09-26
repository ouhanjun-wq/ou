// Hexapod gait engine: body velocity / pose in, 18 joint angles out. Pure math, no Arduino.
//
// Legs are numbered 0..5 = LF LM LR RF RM RR (left front ... right rear). Servo number of a joint
// = leg * 3 + joint (0 coxa, 1 femur, 2 tibia), so servos 0..17.
//
// Walking: every leg runs through a cycle of "stance" (foot on the ground, sliding backwards
// relative to the body) and "swing" (foot lifted and carried forwards). The gait decides which
// legs swing together:
//   TRIPOD  3 legs swing at a time  - fastest
//   RIPPLE  2 legs swing at a time
//   WAVE    1 leg  swings at a time - slowest, most stable
#pragma once
#include "kinematics.h"

namespace gait {

enum Leg { LF, LM, LR, RF, RM, RR, NUM_LEGS };
enum Type { TRIPOD, RIPPLE, WAVE, NUM_TYPES };

inline const char* typeName(int t) {
  static const char* n[] = {"tripod", "ripple", "wave"};
  return (t >= 0 && t < NUM_TYPES) ? n[t] : "?";
}
inline const char* legName(int l) {
  static const char* n[] = {"LF", "LM", "LR", "RF", "RM", "RR"};
  return (l >= 0 && l < NUM_LEGS) ? n[l] : "?";
}

// Fraction of the cycle a foot is on the ground, and each leg's phase offset.
struct Pattern {
  float duty;
  float offset[NUM_LEGS];
  float period_scale;       // cycle time relative to the tripod cycle
};

inline const Pattern& pattern(int t) {
  static const Pattern p[NUM_TYPES] = {
      // LF    LM    LR    RF    RM    RR
      {0.5f, {0.0f, 0.5f, 0.0f, 0.5f, 0.0f, 0.5f}, 1.0f},                       // tripod
      {2.0f / 3, {1.0f / 3, 2.0f / 3, 0.0f, 5.0f / 6, 1.0f / 6, 0.5f}, 1.5f},    // ripple
      {5.0f / 6, {2.0f / 6, 1.0f / 6, 0.0f, 5.0f / 6, 4.0f / 6, 3.0f / 6}, 2.25f},  // wave
  };
  return p[(t >= 0 && t < NUM_TYPES) ? t : 0];
}

struct Config {
  kin::LegGeom geom;
  float front_x, front_y, front_angle;  // front legs mount (mm, mm, deg); right side is mirrored
  float mid_y;                          // middle legs mount at (0, +-mid_y), pointing +-90 deg
  float rear_x, rear_y, rear_angle;     // rear legs at (-rear_x, +-rear_y)
  float cycle_s;                        // tripod cycle time (s); ripple / wave are slower
  float step_h;                         // foot lift during swing (mm)
  float stride_max;                     // longest foot travel per stance (mm)
  float accel;                          // body acceleration limit (mm/s^2)
  float yaw_accel;                      // turn-rate acceleration limit (deg/s^2)
  float move_rate;                      // height / stance / translation slew (mm/s)
  float tilt_rate;                      // body roll / pitch / yaw slew (deg/s)
};

struct Mount {
  float x, y, a;
};

inline Mount mount(const Config& c, int leg) {
  switch (leg) {
    case LF: return {c.front_x, c.front_y, c.front_angle};
    case LM: return {0, c.mid_y, 90};
    case LR: return {-c.rear_x, c.rear_y, c.rear_angle};
    case RF: return {c.front_x, -c.front_y, -c.front_angle};
    case RM: return {0, -c.mid_y, -90};
    default: return {-c.rear_x, -c.rear_y, -c.rear_angle};
  }
}

inline float approach(float cur, float target, float max_step) {
  if (target > cur + max_step) return cur + max_step;
  if (target < cur - max_step) return cur - max_step;
  return target;
}

struct Pose {
  float height = 0;          // body height above the feet (mm)
  float stance = 0;          // foot distance out from the hip-yaw axis (mm)
  float tx = 0, ty = 0;      // body shift (mm)
  float roll = 0, pitch = 0, yaw = 0;   // body tilt / twist (deg)
};

class Hexapod {
 public:
  explicit Hexapod(const Config& c) : cfg_(c) {}

  Config& config() { return cfg_; }
  const Config& config() const { return cfg_; }

  // Target body velocity: vx forward, vy left (mm/s), wz counter-clockwise (deg/s).
  void setVelocity(float vx, float vy, float wz) { tvx_ = vx; tvy_ = vy; twz_ = wz; }
  void setPose(const Pose& p) { target_ = p; }
  void jumpTo(const Pose& p) { target_ = p; pose_ = p; }   // no slewing (boot)
  // A gait change while walking waits until the robot has stopped.
  void setGait(int t) {
    if (t < 0 || t >= NUM_TYPES) return;
    next_ = t;
    if (state_ == IDLE) type_ = t;
  }
  int gaitType() const { return type_; }
  void stopNow() { tvx_ = tvy_ = twz_ = 0; vx_ = vy_ = wz_ = 0; }

  const Pose& pose() const { return pose_; }
  bool walking() const { return state_ != IDLE; }
  float vx() const { return vx_; }
  float vy() const { return vy_; }
  float wz() const { return wz_; }
  float period() const { return cfg_.cycle_s * pattern(type_).period_scale; }

  bool inStance(int leg) const { return lp_[leg] < pattern(type_).duty; }
  const kin::Vec3& foot(int leg) const { return foot_[leg]; }        // body frame
  const kin::Joints& joints(int leg) const { return joints_[leg]; }
  bool reachable() const { return reachable_; }

  // Advance by dt seconds and recompute every foot and joint.
  void update(float dt) {
    const Pattern& pat = pattern(type_);
    const float T = period();
    const float stanceTime = pat.duty * T;

    // 1. Scale the requested velocity so no foot has to travel further than stride_max.
    float sx = tvx_, sy = tvy_, sw = twz_;
    const float need = maxStride(sx, sy, sw, stanceTime);
    if (need > cfg_.stride_max && need > 0) {
      const float k = cfg_.stride_max / need;
      sx *= k; sy *= k; sw *= k;
    }
    // 2. Acceleration limits. While landing the body holds still (parked feet must not slide).
    const bool wants = fabsf(sx) > 0.5f || fabsf(sy) > 0.5f || fabsf(sw) > 0.5f;
    const float hold = state_ == LAND ? 0.0f : 1.0f;
    vx_ = approach(vx_, sx * hold, cfg_.accel * dt);
    vy_ = approach(vy_, sy * hold, cfg_.accel * dt);
    wz_ = approach(wz_, sw * hold, cfg_.yaw_accel * dt);

    // 3. Body pose slewing.
    const float m = cfg_.move_rate * dt, a = cfg_.tilt_rate * dt;
    pose_.height = approach(pose_.height, target_.height, m);
    pose_.stance = approach(pose_.stance, target_.stance, m);
    pose_.tx = approach(pose_.tx, target_.tx, m);
    pose_.ty = approach(pose_.ty, target_.ty, m);
    pose_.roll = approach(pose_.roll, target_.roll, a);
    pose_.pitch = approach(pose_.pitch, target_.pitch, a);
    pose_.yaw = approach(pose_.yaw, target_.yaw, a);

    // 4. Leg phases: 0..duty = stance (foot on the ground), duty..1 = swing (foot in the air).
    //   IDLE  feet parked at their neutral spot.
    //   WALK  every phase advances together, so the gait's offsets stay intact. The swing lift
    //         fades in over half a cycle so a leg that starts mid-swing does not jerk up.
    //   LAND  the stick was released: each leg that is not yet back at its neutral spot finishes
    //         one more swing, lands there and waits. When all are home -> IDLE.
    //         (A new command during LAND waits for IDLE, then restarts the gait cleanly.)
    const bool commanded = wants || fabsf(vx_) > 0.5f || fabsf(vy_) > 0.5f || fabsf(wz_) > 0.5f;
    if (state_ == IDLE && commanded) {
      for (int leg = 0; leg < NUM_LEGS; ++leg) {
        lp_[leg] = pat.offset[leg];
        startSwing(leg, lp_[leg] < pat.duty ? 0 : (lp_[leg] - pat.duty) / (1 - pat.duty));
      }
      phase_ = 0;
      lift_ = 0;
      state_ = WALK;
    } else if (state_ == WALK && !commanded) {
      for (int leg = 0; leg < NUM_LEGS; ++leg)
        done_[leg] = lp_[leg] < pat.duty && hypotf(d_[leg].x, d_[leg].y) < 0.5f;
      state_ = LAND;
    }
    if (state_ == WALK) {
      lift_ = fminf(1.0f, lift_ + dt / (0.5f * T));
      phase_ += dt / T;
      phase_ -= floorf(phase_);
    }

    // 5. Feet and joints. d_ = foot offset from its neutral spot (body frame, mm).
    const float wr = wz_ / kin::kDeg;
    bool allDone = true;
    reachable_ = true;
    for (int leg = 0; leg < NUM_LEGS; ++leg) {
      const Mount mt = mount(cfg_, leg);
      const kin::Vec3 n = kin::legToBody({pose_.stance, 0, -pose_.height}, mt.x, mt.y, mt.a);
      float z = 0;
      if (state_ != IDLE) {
        if (state_ == WALK || !done_[leg]) {
          // While walking all legs follow one master phase (no drift between them).
          const bool wasStance = lp_[leg] < pat.duty;
          if (state_ == WALK) {
            lp_[leg] = phase_ + pat.offset[leg];
            lp_[leg] -= floorf(lp_[leg]);
          } else {
            lp_[leg] += dt / T;
            if (lp_[leg] >= 1.0f) { lp_[leg] = 0; done_[leg] = true; }   // touched down at home
          }
          if (wasStance && lp_[leg] >= pat.duty) startSwing(leg, 0);   // lift off
        }
        if (lp_[leg] < pat.duty) {
          // Stance: the foot stays put on the ground, so relative to the body it moves opposite
          // to the body's motion (translation plus rotation about the body centre).
          const float fx = n.x + d_[leg].x, fy = n.y + d_[leg].y;
          d_[leg].x -= (vx_ - wr * fy) * dt;
          d_[leg].y -= (vy_ + wr * fx) * dt;
        } else {
          // Swing: carry the foot from where it lifted off to half a stride ahead of neutral.
          const float strideX = (vx_ - wr * n.y) * stanceTime;
          const float strideY = (vy_ + wr * n.x) * stanceTime;
          const float w = (lp_[leg] - pat.duty) / (1.0f - pat.duty);
          const float e0 = ease(w0_[leg]);
          const float k = e0 < 1.0f ? (ease(w) - e0) / (1.0f - e0) : 1.0f;
          d_[leg].x = d0_[leg].x + (0.5f * strideX - d0_[leg].x) * k;
          d_[leg].y = d0_[leg].y + (0.5f * strideY - d0_[leg].y) * k;
          z = cfg_.step_h * lift_ * sinf(kin::kPi * w);
        }
        if (state_ == LAND && !done_[leg]) allDone = false;
      }
      const kin::Vec3 f = {n.x + d_[leg].x, n.y + d_[leg].y, n.z + z};
      foot_[leg] = f;
      const kin::Vec3 b = kin::applyBodyPose(f, pose_.tx, pose_.ty, 0, pose_.roll, pose_.pitch, pose_.yaw);
      const kin::Vec3 l = kin::bodyToLeg(b, mt.x, mt.y, mt.a);
      if (!kin::inverse(cfg_.geom, l, joints_[leg])) reachable_ = false;
    }
    if (state_ == LAND && allDone) {
      state_ = IDLE;
      type_ = next_;
    }
  }

 private:
  enum State { IDLE, WALK, LAND };
  static float ease(float w) { return w * w * (3 - 2 * w); }    // smooth start and stop
  void startSwing(int leg, float w) { d0_[leg] = d_[leg]; w0_[leg] = w; }
  float maxStride(float vx, float vy, float wz, float stanceTime) const {
    float best = 0;
    const float wr = wz / kin::kDeg;
    for (int leg = 0; leg < NUM_LEGS; ++leg) {
      const Mount mt = mount(cfg_, leg);
      const kin::Vec3 n = kin::legToBody({target_.stance, 0, 0}, mt.x, mt.y, mt.a);
      const float dx = (vx - wr * n.y) * stanceTime, dy = (vy + wr * n.x) * stanceTime;
      const float d = sqrtf(dx * dx + dy * dy);
      if (d > best) best = d;
    }
    return best;
  }

  Config cfg_;
  int type_ = TRIPOD;
  float tvx_ = 0, tvy_ = 0, twz_ = 0;
  float vx_ = 0, vy_ = 0, wz_ = 0;
  State state_ = IDLE;
  int next_ = TRIPOD;
  float phase_ = 0;            // master gait phase while walking          // gait to switch to once stopped
  float lp_[NUM_LEGS] = {};    // per-leg phase 0..1: stance first, then swing
  bool done_[NUM_LEGS] = {};   // LAND: leg is back at neutral
  kin::Vec3 d_[NUM_LEGS] = {}, d0_[NUM_LEGS] = {};   // foot offset now / at lift-off
  float w0_[NUM_LEGS] = {};    // swing progress at lift-off (0 unless started mid-swing)
  float lift_ = 0;             // swing-lift fade-in 0..1
  bool reachable_ = true;
  Pose pose_, target_;
  kin::Vec3 foot_[NUM_LEGS] = {};
  kin::Joints joints_[NUM_LEGS] = {};
};

}  // namespace gait
