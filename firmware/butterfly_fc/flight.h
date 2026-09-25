// Flight logic: arming state machine, modes, cascaded PID, flapping waveform, mixer.
// Header-only and free of Arduino calls so it can be unit-tested on the host.
#pragma once
#include <math.h>
#include <stdint.h>

#include <initializer_list>

#include "filters.h"
#include "params.h"
#include "protocol.h"

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float wrap180(float d) {
  while (d > 180.0f) d -= 360.0f;
  while (d < -180.0f) d += 360.0f;
  return d;
}

// Rate PID: D on measurement (no kick on set-point steps), filtered D, conditional integration.
struct RatePID {
  float integ = 0.0f, prevMeas = 0.0f;
  bool first = true;
  PT1 dlpf;

  void reset() { integ = 0.0f; first = true; dlpf.reset(); }

  float update(float sp, float meas, float kp, float ki, float kd,
               float iLim, float outLim, float dt, bool integrate) {
    const float e = sp - meas;
    float d = 0.0f;
    if (!first) d = dlpf.apply(-(meas - prevMeas) / dt);
    first = false;
    prevMeas = meas;
    const float unsat = kp * e + integ + kd * d;
    const bool pushingFurther = (unsat > outLim && e > 0.0f) || (unsat < -outLim && e < 0.0f);
    if (integrate && !pushingFurther) integ = clampf(integ + ki * e * dt, -iLim, iLim);
    return clampf(kp * e + integ + kd * d, -outLim, outLim);
  }
};

struct FlightInputs {
  float thr = 0;                  // 0..1
  float roll = 0, pitch = 0, yaw = 0;  // -1..1
  uint8_t mode = proto::MODE_MANUAL;
  bool armReq = false;
  bool linkOk = false;
  bool bench = false;             // USB bench test: armed without radio
  bool charging = false;          // Type-C charger plugged in: never arm
  float turnDeg = 0;              // pending CMD_TURN (consumed by step)
  float altDelta = 0;             // pending CMD_ALT in metres (consumed by step)
};

// AUTO mode reads the throttle channel as a centred climb command:
// 0.5 = hold altitude, 1 = full climb, 0 = full descent.
inline float climbCommand(float thr) {
  float c = clampf((thr - 0.5f) * 2.0f, -1.0f, 1.0f);
  if (fabsf(c) < 0.1f) return 0.0f;
  return (c - (c > 0 ? 0.1f : -0.1f)) / 0.9f;
}

struct FlightSensors {
  float rollAvg = 0, pitchAvg = 0;  // stroke-averaged attitude (deg)
  float yaw = 0;                    // heading (deg)
  float p = 0, q = 0, r = 0;        // notched body rates (deg/s)
  bool imuOk = false;
  float alt = 0, vz = 0;            // stroke-averaged baro altitude (m), climb rate (m/s, up +)
  bool baroOk = false;
};

struct FlightOutput {
  float wingL = 0, wingR = 0;     // wing angles (deg, + = up), before servo mapping
  float ur = 0, up = 0, uy = 0;   // control outputs (deg)
  float flapHz = 0;
  float thrCmd = 0;               // effective throttle after the altitude controller
  float altTarget = 0;
  uint8_t state = proto::ST_DISARMED;
  uint8_t mode = proto::MODE_MANUAL;
  bool armBlocked = false;
};

class FlightCore {
 public:
  void reinit(const Params& p, float ctrlHz) {
    for (RatePID* pid : {&pidR_, &pidP_, &pidY_}) pid->dlpf.setCutoff(p.dterm_lpf_hz, ctrlHz);
  }

  FlightOutput step(const FlightInputs& inRaw, const FlightSensors& s, const Params& p, float dt) {
    FlightOutput o;
    FlightInputs in = inRaw;
    updateState(in, p);

    if (state_ == proto::ST_FAILSAFE) {       // glide and level until the link returns
      in.thr = 0; in.roll = in.pitch = in.yaw = 0;
      in.mode = proto::MODE_STABILIZE;
    }
    uint8_t mode = s.imuOk ? in.mode : (uint8_t)proto::MODE_MANUAL;
    if (mode > proto::MODE_AUTO) mode = proto::MODE_MANUAL;
    if (mode == proto::MODE_AUTO && !s.baroOk) mode = proto::MODE_HEADING_HOLD;  // no altitude sensor

    const bool armed = state_ != proto::ST_DISARMED;

    // ---- altitude hold (AUTO) ----
    float thr = in.thr;
    if (mode == proto::MODE_AUTO && armed) {
      if (lastMode_ != proto::MODE_AUTO) {            // entering AUTO (or back from failsafe)
        autoEngaged_ = autoEngaged_ || lastFlapHz_ > 0;  // already flying -> hold from here
        altTarget_ = s.alt;
        vzI_ = 0;
      }
      thr = altitudeControl(in, s, p, dt);
    } else {
      // Keep the engaged flag through a failsafe glide so AUTO resumes when the link returns.
      if (!armed || state_ != proto::ST_FAILSAFE) autoEngaged_ = false;
      altTarget_ = s.alt;
    }
    lastMode_ = armed ? mode : (uint8_t)0xFF;

    // ---- throttle -> flapping frequency and amplitude ----
    float f = 0, amp = 0;
    if (armed && thr > p.thr_idle) {
      const float t = clampf((thr - p.thr_idle) / (1.0f - p.thr_idle), 0.0f, 1.0f);
      f = p.f_min + t * (p.f_max - p.f_min);
      amp = p.amp_min + t * (p.amp_max - p.amp_min);
    }
    phase_ += 2.0f * (float)M_PI * f * dt;
    if (phase_ > 2.0f * (float)M_PI) phase_ -= 2.0f * (float)M_PI;
    if (f == 0) phase_ = 0;

    // ---- control ----
    float ur = 0, up = 0, uy = 0;
    const bool integrate = armed && thr > p.thr_idle + 0.1f;
    if (!armed) {
      resetControllers(s);
    } else if (mode == proto::MODE_MANUAL) {
      ur = in.roll * p.man_roll;
      up = in.pitch * p.man_pitch;
      uy = in.yaw * p.man_yaw;
      resetControllers(s);
    } else {
      const float rollSp = in.roll * p.max_roll;
      const float pitchSp = p.pitch_trim + in.pitch * p.max_pitch;
      const float pSp = clampf(p.ang_p_roll * (rollSp - s.rollAvg), -200.0f, 200.0f);
      const float qSp = clampf(p.ang_p_pitch * (pitchSp - s.pitchAvg), -200.0f, 200.0f);

      float rSp;
      const bool holdHeading = mode == proto::MODE_HEADING_HOLD || mode == proto::MODE_AUTO;
      if (holdHeading && fabsf(in.yaw) < 0.05f) {
        headingTarget_ = wrap180(headingTarget_ + in.turnDeg);
        rSp = clampf(p.head_p * wrap180(headingTarget_ - s.yaw), -p.max_yaw_rate, p.max_yaw_rate);
      } else {
        rSp = in.yaw * p.max_yaw_rate;
        headingTarget_ = s.yaw;
      }

      ur = pidR_.update(pSp, s.p, p.rate_p_roll, p.rate_i_roll, p.rate_d_roll,
                        p.i_limit, 1.5f * p.man_roll, dt, integrate) + p.ff * in.roll * p.man_roll;
      up = pidP_.update(qSp, s.q, p.rate_p_pitch, p.rate_i_pitch, p.rate_d_pitch,
                        p.i_limit, 1.5f * p.man_pitch, dt, integrate) + p.ff * in.pitch * p.man_pitch;
      uy = pidY_.update(rSp, s.r, p.rate_p_yaw, p.rate_i_yaw, p.rate_d_yaw,
                        p.i_limit, 1.5f * p.man_yaw, dt, integrate) + p.ff * in.yaw * p.man_yaw;
    }

    mix(p, amp, ur, up, uy, o);
    o.ur = ur; o.up = up; o.uy = uy;
    o.flapHz = f;
    o.thrCmd = thr;
    o.altTarget = altTarget_;
    lastFlapHz_ = f;
    o.state = state_;
    o.mode = mode;
    o.armBlocked = armBlocked_;
    return o;
  }

  uint8_t state() const { return state_; }
  float phase() const { return phase_; }

 private:
  void updateState(const FlightInputs& in, const Params& p) {
    armBlocked_ = false;
    if (in.charging) {           // charge lock beats everything, including bench mode
      state_ = proto::ST_DISARMED;
      armBlocked_ = in.armReq || in.bench;
      prevBench_ = false;
      linkWasLost_ = true;       // after unplugging, the arm switch must be flipped again
      return;
    }
    if (in.bench) { state_ = proto::ST_ARMED; prevBench_ = true; return; }
    if (prevBench_) {            // leaving bench mode always disarms
      prevBench_ = false;
      state_ = proto::ST_DISARMED;
      linkWasLost_ = true;
    }
    if (!in.linkOk) {
      if (state_ == proto::ST_ARMED) state_ = proto::ST_FAILSAFE;
      linkWasLost_ = true;
      return;
    }
    if (linkWasLost_) {
      // First packet after boot / link loss is not an arm edge: a switch that is
      // already on must be flipped off and on again before the butterfly arms.
      linkWasLost_ = false;
      if (state_ == proto::ST_DISARMED) prevArmReq_ = in.armReq;
    }
    if (!in.armReq) {
      state_ = proto::ST_DISARMED;
    } else if (state_ == proto::ST_FAILSAFE) {
      state_ = proto::ST_ARMED;   // pilot regains control
    } else if (state_ == proto::ST_DISARMED) {
      // Throttle must be "safe": low in normal modes, centred (= hold) in AUTO.
      const bool thrSafe = in.mode == proto::MODE_AUTO ? fabsf(in.thr - 0.5f) < 0.1f
                                                       : in.thr <= p.thr_idle + 0.02f;
      if (!prevArmReq_ && thrSafe) state_ = proto::ST_ARMED;
      else armBlocked_ = true;    // switch was already on, or throttle not low
    }
    prevArmReq_ = in.armReq;
  }

  // Outer loop: altitude error -> climb-rate set-point. Inner loop: climb-rate PI -> throttle.
  // After arming in AUTO the wings stay in glide pose until the climb stick is pushed
  // past half-way once ("launch gesture"), so the butterfly never flaps in your hand by surprise.
  float altitudeControl(const FlightInputs& in, const FlightSensors& s, const Params& p, float dt) {
    const float c = climbCommand(in.thr);
    if (!autoEngaged_) {
      if (c <= 0.5f) return 0.0f;
      autoEngaged_ = true;
      altTarget_ = s.alt;
      vzI_ = 0;
    }
    altTarget_ += in.altDelta;
    float vzSp;
    if (c != 0.0f) {
      vzSp = c > 0 ? c * p.max_climb : c * p.max_descent;
      altTarget_ = s.alt;                       // hold wherever the stick is released
    } else {
      vzSp = clampf(p.alt_p * (altTarget_ - s.alt), -p.max_descent, p.max_climb);
    }
    const float e = vzSp - s.vz;
    const float out = p.thr_hover + p.vz_p * e + vzI_;
    const bool saturated = (out >= 1.0f && e > 0) || (out <= p.thr_idle + 0.05f && e < 0);
    if (!saturated) vzI_ = clampf(vzI_ + p.vz_i * e * dt, -0.3f, 0.3f);
    return clampf(p.thr_hover + p.vz_p * e + vzI_, p.thr_idle + 0.05f, 1.0f);
  }

  void resetControllers(const FlightSensors& s) {
    pidR_.reset(); pidP_.reset(); pidY_.reset();
    headingTarget_ = s.yaw;
  }

  // theta_L = center + dp + dr + (A + dA) w(phase)
  // theta_R = center + dp - dr + (A - dA) w(phase)
  void mix(const Params& p, float amp, float ur, float up, float uy, FlightOutput& o) const {
    const float gain = 1.0f + 3.0f * p.squareness;           // sine -> clipped (square-ish)
    const float w = clampf(sinf(phase_) * gain, -1.0f, 1.0f);
    const float dr = p.mix_roll_dir * ur;
    const float dp = p.mix_pitch_dir * up;
    const float dA = amp > 0 ? p.mix_yaw_dir * uy : 0.0f;    // differential needs flapping
    const float aL = clampf(amp + dA, 0.0f, p.wing_limit);
    const float aR = clampf(amp - dA, 0.0f, p.wing_limit);
    o.wingL = clampf(p.center + dp + dr + aL * w, -p.wing_limit, p.wing_limit);
    o.wingR = clampf(p.center + dp - dr + aR * w, -p.wing_limit, p.wing_limit);
  }

  RatePID pidR_, pidP_, pidY_;
  float headingTarget_ = 0;
  float altTarget_ = 0, vzI_ = 0, lastFlapHz_ = 0;
  bool autoEngaged_ = false;
  uint8_t lastMode_ = 0xFF;
  float phase_ = 0;
  uint8_t state_ = proto::ST_DISARMED;
  bool prevArmReq_ = false;
  bool prevBench_ = false;
  bool linkWasLost_ = true;
  bool armBlocked_ = false;
};
