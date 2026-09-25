// Tunable parameters. Units: degrees, Hz, seconds unless noted.
// Change at runtime:  USB  "set rate_p_roll 0.1"   or ground station  "SET rate_p_roll 0.1"
// Persist with "save". Struct layout change => bump PARAMS_VERSION (old flash data is discarded).
#pragma once
#include <stddef.h>

constexpr unsigned PARAMS_VERSION = 1;

struct Params {
  // ---- flapping waveform ----
  float f_min, f_max;          // flap frequency range mapped from throttle (Hz)
  float amp_min, amp_max;      // half-stroke amplitude range (deg)
  float center;                // stroke centre / glide dihedral (deg, + = wings up)
  float squareness;            // 0 = sine, 1 = near-square (more power, harsher)
  float thr_idle;              // throttle below this = glide, no flapping

  // ---- manual authority (deg of wing offset at full stick) ----
  float man_roll, man_pitch, man_yaw;

  // ---- mixer direction (+1 / -1): flip if the butterfly turns the wrong way ----
  float mix_roll_dir, mix_pitch_dir, mix_yaw_dir;

  // ---- servos ----
  float servo_us_per_deg;      // 2000 us / 180 deg = 11.11 for most servos
  float servo_dir_l, servo_dir_r;
  float trim_l, trim_r;        // deg
  float wing_limit;            // max |wing angle| from servo centre (deg)

  // ---- IMU mounting ----
  float imu_yaw_quarter;       // 0..3: board rotated by n * 90 deg (clockwise seen from above)
  float imu_flip;              // 1 = board mounted upside down

  // ---- filters ----
  float gyro_lpf_hz, acc_lpf_hz, dterm_lpf_hz;
  float notch_q;
  float notch_on, stroke_avg_on;

  // ---- attitude estimator ----
  float ahrs_kp, ahrs_ki;

  // ---- stabilize / heading hold ----
  float max_roll, max_pitch, max_yaw_rate, pitch_trim;
  float ang_p_roll, ang_p_pitch, head_p;
  float rate_p_roll, rate_i_roll, rate_d_roll;
  float rate_p_pitch, rate_i_pitch, rate_d_pitch;
  float rate_p_yaw, rate_i_yaw, rate_d_yaw;
  float i_limit;               // integrator limit (deg of wing offset)
  float ff;                    // stick feed-forward in stabilized modes (0..1)

  // ---- safety / misc ----
  float fs_timeout_ms;         // link loss -> failsafe (glide + level)
  float cells;                 // LiPo cell count
  float vbat_ratio;            // battery divider ratio (Vbat / Vpin)
  float vcell_warn;            // per-cell low-battery warning (V)
  float net_id;                // radio network id, must match ground station
};

inline void paramsDefaults(Params& p) {
  p.f_min = 2.5f;   p.f_max = 5.0f;
  p.amp_min = 20.0f; p.amp_max = 40.0f;
  p.center = 10.0f; p.squareness = 0.3f; p.thr_idle = 0.05f;

  p.man_roll = 15.0f; p.man_pitch = 12.0f; p.man_yaw = 8.0f;
  p.mix_roll_dir = 1.0f; p.mix_pitch_dir = 1.0f; p.mix_yaw_dir = 1.0f;

  p.servo_us_per_deg = 11.11f;
  p.servo_dir_l = 1.0f; p.servo_dir_r = -1.0f;
  p.trim_l = 0.0f; p.trim_r = 0.0f;
  p.wing_limit = 60.0f;

  p.imu_yaw_quarter = 0.0f; p.imu_flip = 0.0f;

  p.gyro_lpf_hz = 50.0f; p.acc_lpf_hz = 15.0f; p.dterm_lpf_hz = 20.0f;
  p.notch_q = 3.0f; p.notch_on = 1.0f; p.stroke_avg_on = 1.0f;

  p.ahrs_kp = 0.5f; p.ahrs_ki = 0.02f;

  p.max_roll = 30.0f; p.max_pitch = 25.0f; p.max_yaw_rate = 90.0f; p.pitch_trim = 0.0f;
  p.ang_p_roll = 3.0f; p.ang_p_pitch = 3.0f; p.head_p = 2.0f;
  p.rate_p_roll = 0.08f;  p.rate_i_roll = 0.05f;  p.rate_d_roll = 0.0f;
  p.rate_p_pitch = 0.08f; p.rate_i_pitch = 0.05f; p.rate_d_pitch = 0.0f;
  p.rate_p_yaw = 0.06f;   p.rate_i_yaw = 0.03f;   p.rate_d_yaw = 0.0f;
  p.i_limit = 8.0f; p.ff = 0.5f;

  p.fs_timeout_ms = 500.0f;
  p.cells = 2.0f; p.vbat_ratio = 3.0f; p.vcell_warn = 3.5f;
  p.net_id = 1.0f;
}

struct ParamInfo {
  const char* name;
  size_t offset;   // offsetof(Params, field)
  float minV, maxV;
};

// Implemented in params.cpp
extern Params P;
extern volatile bool gParamsDirty;   // set when a parameter changes (filters re-init)
const ParamInfo* paramTable(size_t& count);
const ParamInfo* paramFind(const char* name);
bool paramGet(const char* name, float& v);
bool paramSet(const char* name, float v);   // false if unknown or out of range
bool paramsLoad();
bool paramsSave();
void paramsReset();
