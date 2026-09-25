// Tunable parameters of the arm. Every value is a float so the serial CLI can
// list / get / set them by name; SAVE writes the whole struct to NVS flash.
//
// Joint numbering (index 0..5 in code, J1..J6 in the docs and CLI):
//   J1 base yaw | J2 shoulder | J3 elbow | J4 wrist pitch | J5 wrist roll | J6 gripper
// Angles (deg) follow the conventions in docs/motion-control.md:
//   J1 0 = straight forward, + = turn left (seen from above)
//   J2 angle of the upper arm above horizontal (90 = vertical)
//   J3 elbow bend relative to the upper arm (0 = straight, negative = forearm bends down)
//   J4 wrist bend relative to the forearm; tool pitch = J2 + J3 + J4 (0 = level, -90 = down)
//   J5 wrist roll; J6 gripper 0 = open .. 100 = closed (percent, not degrees)
// Servo pulse for joint j:  us = us0[j] + usdeg[j] * angle   (two-point calibration: MARK)
#pragma once
#include <stddef.h>
#include <stdint.h>

constexpr int NJ = 6;          // J1..J6
constexpr int NARM = 5;        // J1..J5 take part in the kinematics
constexpr int GRIP = 5;        // index of the gripper

constexpr uint32_t PARAMS_MAGIC = 0x41524D31;   // "ARM1"
constexpr uint32_t PARAMS_VERSION = 1;

struct Params {
  uint32_t magic;
  uint32_t version;

  // per joint (index 0..5)
  float us0[NJ];      // pulse (us) at angle 0 (gripper: fully open)
  float usdeg[NJ];    // us per degree (gripper: us per percent); sign = direction
  float qmin[NJ];     // soft limits (deg / %)
  float qmax[NJ];
  float vmax[NJ];     // speed limit (deg/s, gripper %/s)
  float amax[NJ];     // acceleration limit (deg/s^2, gripper %/s^2)
  float home[NJ];     // Y button / HOME
  float park[NJ];     // power-up and power-down pose (arm folded, resting)

  // geometry (mm), measured on your arm: docs/assembly-guide.md step 6
  float d1;           // table -> shoulder axis height
  float l2;           // shoulder axis -> elbow axis
  float l3;           // elbow axis -> wrist-pitch axis
  float l4;           // wrist-pitch axis -> tool point (middle of the gripper fingers)
  float z_min;        // tool point never goes below this height (mm)
  float r_min;        // tool point stays this far from the base axis (mm)

  // gamepad
  float deadband;     // stick dead zone 0..0.3
  float expo;         // 0 = linear, 1 = cubic
  float v_lin;        // Cartesian jog speed at top speed level (mm/s)
  float v_ang;        // pitch / roll jog speed at top speed level (deg/s)
  float speed_lvl;    // speed level at power-up: 1 slow, 2 medium, 3 fast

  // protection
  float over_amps;    // total servo current limit (A)
  float over_s;       // ... sustained this long -> OVERLOAD (motion frozen)
  float fault_s;      // ... still over after this long in OVERLOAD -> servo power off
  float estop_v;      // servo bus below this while powered = emergency stop pressed (V)
  float low_v;        // warning threshold for the servo bus (V)
  float grip_amps;    // extra current while closing that means "object gripped" (A)
  float grip_backoff; // open this much after a grip is detected (%)

  // playback
  float dwell_s;      // pause at every waypoint (s)

  // hardware
  float pulse_min;    // absolute pulse limits sent to any servo (us)
  float pulse_max;
  float pwm_osc_hz;   // PCA9685 oscillator (nominal 25 MHz, real chips 24-27 MHz)
  float shunt_ohm;    // INA226 shunt resistor
};

inline void paramsDefaults(Params& p) {
  p = Params();
  p.magic = PARAMS_MAGIC;
  p.version = PARAMS_VERSION;
  // Placeholder calibration: each servo at 1500 us in the HOME pose, 180 deg = 2000 us.
  // Run MARK calibration (firmware/README.md) before trusting any angle.
  const float k = 2000.0f / 180.0f;
  const float us0[NJ] = {1500, 1500 - 90 * k, 1500 + 90 * k, 1500, 1500, 1000};
  const float usdeg[NJ] = {k, k, k, k, k, 10.0f};
  const float qmin[NJ] = {-90, 10, -160, -100, -90, 0};
  const float qmax[NJ] = {90, 170, 10, 100, 90, 100};
  const float vmax[NJ] = {90, 60, 90, 120, 150, 150};
  const float amax[NJ] = {180, 120, 180, 300, 400, 400};
  const float home[NJ] = {0, 90, -90, 0, 0, 0};
  const float park[NJ] = {0, 100, -140, 40, 0, 0};
  for (int j = 0; j < NJ; ++j) {
    p.us0[j] = us0[j];
    p.usdeg[j] = usdeg[j];
    p.qmin[j] = qmin[j];
    p.qmax[j] = qmax[j];
    p.vmax[j] = vmax[j];
    p.amax[j] = amax[j];
    p.home[j] = home[j];
    p.park[j] = park[j];
  }
  p.d1 = 75;
  p.l2 = 105;
  p.l3 = 100;
  p.l4 = 120;
  p.z_min = 10;
  p.r_min = 60;
  p.deadband = 0.08f;
  p.expo = 0.3f;
  p.v_lin = 80;
  p.v_ang = 60;
  p.speed_lvl = 2;
  p.over_amps = 6.0f;
  p.over_s = 0.5f;
  p.fault_s = 3.0f;
  p.estop_v = 3.0f;
  p.low_v = 5.3f;
  p.grip_amps = 0.6f;
  p.grip_backoff = 3.0f;
  p.dwell_s = 0.3f;
  p.pulse_min = 500;
  p.pulse_max = 2500;
  p.pwm_osc_hz = 25000000.0f;
  p.shunt_ohm = 0.01f;
}

// ---------------- name table (CLI: LIST / GET / SET) ----------------
struct ParamInfo {
  char name[16];
  size_t offset;
  float lo, hi;
};

#define ARM_P(field, lo, hi) {#field, offsetof(Params, field), lo, hi}
#define ARM_J(field, j, lo, hi) {"j" #j "_" #field, offsetof(Params, field) + (j - 1) * sizeof(float), lo, hi}
#define ARM_JOINT(j)                                                                        \
  ARM_J(us0, j, -2000.0f, 5000.0f), ARM_J(usdeg, j, -50.0f, 50.0f),                         \
  ARM_J(qmin, j, -180.0f, 180.0f), ARM_J(qmax, j, -180.0f, 180.0f),                         \
  ARM_J(vmax, j, 1.0f, 720.0f), ARM_J(amax, j, 1.0f, 5000.0f),                              \
  ARM_J(home, j, -180.0f, 180.0f), ARM_J(park, j, -180.0f, 180.0f)

inline const ParamInfo* paramTable(size_t& count) {
  static const ParamInfo kTable[] = {
    ARM_JOINT(1), ARM_JOINT(2), ARM_JOINT(3), ARM_JOINT(4), ARM_JOINT(5), ARM_JOINT(6),
    ARM_P(d1, 0.0f, 500.0f),        ARM_P(l2, 10.0f, 500.0f),       ARM_P(l3, 10.0f, 500.0f),
    ARM_P(l4, 0.0f, 500.0f),        ARM_P(z_min, -200.0f, 300.0f),  ARM_P(r_min, 0.0f, 300.0f),
    ARM_P(deadband, 0.0f, 0.3f),    ARM_P(expo, 0.0f, 1.0f),
    ARM_P(v_lin, 5.0f, 300.0f),     ARM_P(v_ang, 5.0f, 360.0f),     ARM_P(speed_lvl, 1.0f, 3.0f),
    ARM_P(over_amps, 0.5f, 20.0f),  ARM_P(over_s, 0.05f, 5.0f),     ARM_P(fault_s, 0.5f, 30.0f),
    ARM_P(estop_v, 0.0f, 6.0f),     ARM_P(low_v, 0.0f, 8.0f),
    ARM_P(grip_amps, 0.05f, 5.0f),  ARM_P(grip_backoff, 0.0f, 20.0f),
    ARM_P(dwell_s, 0.0f, 10.0f),
    ARM_P(pulse_min, 400.0f, 1500.0f), ARM_P(pulse_max, 1500.0f, 2700.0f),
    ARM_P(pwm_osc_hz, 20e6f, 30e6f), ARM_P(shunt_ohm, 0.0005f, 1.0f),
  };
  count = sizeof(kTable) / sizeof(kTable[0]);
  return kTable;
}

#undef ARM_JOINT
#undef ARM_J
#undef ARM_P

inline float* paramPtr(Params& p, const ParamInfo& pi) {
  return reinterpret_cast<float*>(reinterpret_cast<char*>(&p) + pi.offset);
}

inline const ParamInfo* paramFind(const char* name) {
  size_t n;
  const ParamInfo* t = paramTable(n);
  for (size_t i = 0; i < n; ++i) {
    const char* a = t[i].name;
    const char* b = name;
    while (*a && *a == *b) { ++a; ++b; }
    if (*a == 0 && *b == 0) return &t[i];
  }
  return nullptr;
}

// Returns false (and leaves p unchanged) for an unknown name or out-of-range value.
inline bool paramSet(Params& p, const char* name, float v) {
  const ParamInfo* pi = paramFind(name);
  if (!pi || !(v >= pi->lo && v <= pi->hi)) return false;
  *paramPtr(p, *pi) = v;
  return true;
}
