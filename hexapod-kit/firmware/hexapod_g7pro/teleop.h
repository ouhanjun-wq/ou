// Gamepad -> hexapod commands. Pure logic, no Arduino / Bluepad32 (the .ino fills PadInput).
//
// G7 Pro (Xbox layout):
//   Left stick          walk forward / back, sidestep left / right
//   Right stick L / R   turn on the spot
//   A                   stand up / sit down (from "off" it first powers the servos)
//   B                   emergency stop: stand still until both sticks are released
//   X                   next gait: tripod -> ripple -> wave (applied once the robot stops)
//   Y (hold)            body mode: left stick tilts the body, right stick twists / shifts it,
//                       feet stay on the ground
//   LB / RB             speed level down / up (3 levels)
//   D-pad up / down     body higher / lower;  left / right: lower / higher leg lift
//   pad disconnected    stop and stand still
#pragma once
#include <math.h>
#include "gait.h"

namespace teleop {

struct PadInput {
  bool connected = false;
  float lx = 0, ly = 0, rx = 0, ry = 0;   // -1..1, x right = +, y up = +
  bool a = false, b = false, x = false, y = false, lb = false, rb = false;
  bool up = false, down = false, left = false, right = false;
};

struct Limits {
  float max_vx, max_vy;           // mm/s at full stick, top speed level
  float max_wz;                   // deg/s
  float height, height_min, height_max, sit_height;   // mm
  float stance, sit_stance;       // mm
  float step_min, step_max;       // leg lift range (mm)
  float tilt_max, twist_max;      // deg
  float shift_max;                // mm
  float deadband;                 // stick dead zone (0..1)
};

enum Mode { OFF, SIT, STAND };

// Things the .ino reports on serial / with a short rumble.
enum Event : unsigned {
  EV_NONE = 0,
  EV_MODE = 1 << 0,
  EV_GAIT = 1 << 1,
  EV_SPEED = 1 << 2,
  EV_HEIGHT = 1 << 3,
  EV_STEP = 1 << 4,
  EV_ESTOP = 1 << 5,
  EV_POWER_ON = 1 << 6,
};

inline float deadband(float v, float db) {
  if (fabsf(v) <= db) return 0;
  return (v > 0 ? v - db : v + db) / (1.0f - db);
}

class Teleop {
 public:
  explicit Teleop(const Limits& l) : lim_(l), height_(l.height) {}

  Limits& limits() { return lim_; }
  Mode mode() const { return mode_; }
  int speedLevel() const { return speed_; }
  float heightSetting() const { return height_; }
  float stepSetting() const { return step_; }
  bool estopped() const { return estop_; }
  int pendingGait() const { return pendingGait_; }
  bool bodyMode() const { return bodyMode_; }

  void setMode(Mode m) { mode_ = m; }
  void setStep(float s) { step_ = s; }
  void setHeight(float h) { height_ = h; }
  void setGait(int g) { if (g >= 0 && g < gait::NUM_TYPES) pendingGait_ = g; }

  // One control tick. Returns Event bits describing what changed.
  unsigned update(float dt, const PadInput& in, gait::Hexapod& hex) {
    unsigned ev = EV_NONE;
    PadInput p = in;
    if (!p.connected) p = PadInput();     // released sticks and buttons
    p.lx = deadband(p.lx, lim_.deadband);
    p.ly = deadband(p.ly, lim_.deadband);
    p.rx = deadband(p.rx, lim_.deadband);
    p.ry = deadband(p.ry, lim_.deadband);

    auto edge = [](bool now, bool& prev) { const bool e = now && !prev; prev = now; return e; };

    if (edge(p.a, prevA_)) {
      if (mode_ == OFF) {            // power on: start from the sit pose, then rise
        mode_ = STAND;
        ev |= EV_POWER_ON;
        hex.jumpTo(sitPose());
      } else if (mode_ == SIT) {
        mode_ = STAND;
      } else {
        mode_ = SIT;
        hex.stopNow();
      }
      ev |= EV_MODE;
    }
    if (edge(p.b, prevB_)) {
      estop_ = true;
      hex.stopNow();
      ev |= EV_ESTOP;
    }
    const bool centred = p.lx == 0 && p.ly == 0 && p.rx == 0 && p.ry == 0;
    if (estop_ && centred && !p.b) estop_ = false;

    if (edge(p.x, prevX_)) {
      pendingGait_ = (pendingGait_ + 1) % gait::NUM_TYPES;
      ev |= EV_GAIT;
    }
    if (pendingGait_ != hex.gaitType() && !hex.walking()) hex.setGait(pendingGait_);

    if (edge(p.lb, prevLb_) && speed_ > 1) { --speed_; ev |= EV_SPEED; }
    if (edge(p.rb, prevRb_) && speed_ < 3) { ++speed_; ev |= EV_SPEED; }

    // D-pad with auto-repeat while held.
    repeat_ -= dt;
    const bool anyPad = p.up || p.down || p.left || p.right;
    const bool padEdge = anyPad && !prevDpad_;
    prevDpad_ = anyPad;
    if (padEdge || (anyPad && repeat_ <= 0)) {
      repeat_ = padEdge ? 0.4f : 0.15f;
      if (p.up || p.down) {
        height_ += p.up ? 5.0f : -5.0f;
        if (height_ > lim_.height_max) height_ = lim_.height_max;
        if (height_ < lim_.height_min) height_ = lim_.height_min;
        ev |= EV_HEIGHT;
      }
      if (p.left || p.right) {
        step_ += p.right ? 5.0f : -5.0f;
        if (step_ > lim_.step_max) step_ = lim_.step_max;
        if (step_ < lim_.step_min) step_ = lim_.step_min;
        ev |= EV_STEP;
      }
    }
    hex.config().step_h = step_;

    bodyMode_ = p.y && mode_ == STAND;
    gait::Pose target = mode_ == STAND ? standPose() : sitPose();
    float vx = 0, vy = 0, wz = 0;
    if (mode_ == STAND && !estop_) {
      if (bodyMode_) {
        target.pitch = p.ly * lim_.tilt_max;       // stick up = nose down
        target.roll = p.lx * lim_.tilt_max;        // stick right = lean right
        target.yaw = -p.rx * lim_.twist_max;       // stick right = twist clockwise
        target.tx = p.ry * lim_.shift_max;         // stick up = body forward
      } else {
        const float k = speedScale();
        vx = p.ly * lim_.max_vx * k;
        vy = -p.lx * lim_.max_vy * k;
        wz = -p.rx * lim_.max_wz * k;
      }
    }
    hex.setVelocity(vx, vy, wz);
    hex.setPose(target);
    return ev;
  }

  float speedScale() const { return speed_ == 1 ? 0.4f : (speed_ == 2 ? 0.7f : 1.0f); }

  gait::Pose standPose() const {
    gait::Pose p;
    p.height = height_;
    p.stance = lim_.stance;
    return p;
  }
  gait::Pose sitPose() const {
    gait::Pose p;
    p.height = lim_.sit_height;
    p.stance = lim_.sit_stance;
    return p;
  }

 private:
  Limits lim_;
  Mode mode_ = OFF;
  int speed_ = 2;
  int pendingGait_ = gait::TRIPOD;
  float height_;
  float step_ = 30;
  float repeat_ = 0;
  bool estop_ = false, bodyMode_ = false;
  bool prevA_ = false, prevB_ = false, prevX_ = false, prevLb_ = false, prevRb_ = false;
  bool prevDpad_ = false;
};

}  // namespace teleop
