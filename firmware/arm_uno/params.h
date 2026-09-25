// Tunable parameters of the arm, stored in the Uno's EEPROM (SAVE).
// Kept compact (int16 where possible) because the ATmega328P has 2 KB of RAM.
//
// Joint numbering (index 0..5 in code, J1..J6 in the docs and commands):
//   J1 base yaw | J2 shoulder | J3 elbow | J4 wrist pitch | J5 wrist roll | J6 gripper
// Angle conventions (deg), see docs/motion-control.md:
//   J1 0 = straight forward, + = turn left (seen from above)
//   J2 angle of the upper arm above horizontal (90 = vertical)
//   J3 elbow bend relative to the upper arm (0 = straight, negative = forearm bends down)
//   J4 wrist bend relative to the forearm; tool pitch = J2 + J3 + J4 (0 = level, -90 = down)
//   J5 wrist roll; J6 gripper 0 = open .. 100 = closed (percent)
// Servo pulse for joint j:  us = us0[j] + usdeg[j] * angle   (two-point calibration: MARK)
#pragma once
#include <stdint.h>
#include <string.h>

constexpr int NJ = 6;          // J1..J6
constexpr int NARM = 5;        // J1..J5 take part in the kinematics
constexpr int GRIP = 5;        // index of the gripper

constexpr uint16_t PARAMS_MAGIC = 0xA7E1;
constexpr uint8_t PARAMS_VERSION = 2;

struct Params {
  uint16_t magic;
  uint8_t version;

  // per joint
  float us0[NJ];        // pulse (us) at angle 0 (gripper: fully open)
  float usdeg[NJ];      // us per degree (gripper: per percent); the sign is the direction
  int16_t qmin[NJ];     // soft limits (deg / %)
  int16_t qmax[NJ];
  int16_t vmax[NJ];     // speed limit (deg/s)
  int16_t amax[NJ];     // acceleration limit (deg/s^2)
  int16_t home[NJ];     // Y button / HOME
  int16_t park[NJ];     // pose at power-on and before power-off (arm folded)

  // geometry (mm), measured on your arm (assembly guide step 6)
  int16_t d1, l2, l3, l4;
  int16_t z_min;        // tool point never lower than this (mm)
  int16_t r_min;        // tool point this far from the base axis at least (mm)

  // gamepad
  uint8_t deadband;     // stick dead zone, % of full scale
  uint8_t expo;         // 0 = linear .. 100 = cubic
  int16_t v_lin;        // XYZ jog speed at the top speed level (mm/s)
  int16_t v_ang;        // pitch / roll jog speed at the top speed level (deg/s)
  uint8_t speed_lvl;    // 1 slow, 2 medium, 3 fast at power-up
  uint8_t grip_backoff; // % the gripper opens again when RT is released (less stall)

  // playback / safety / hardware
  uint16_t dwell_ms;    // pause at every waypoint
  uint16_t estop_mv;    // servo supply below this while on = E-stop pressed (0 = off)
  uint16_t vdiv_x100;   // divider ratio x100 on A3 (10k + 10k = 200)
  int16_t pulse_min;    // absolute pulse limits (us)
  int16_t pulse_max;
};

// Defaults. Placeholder calibration: every servo at 1500 us in the HOME pose, 180 deg = 2000 us.
// Run the MARK calibration (docs/assembly-guide.md step 6) before trusting any angle.
#define ARM_K (2000.0f / 180.0f)
#if defined(__AVR__)
#include <avr/pgmspace.h>
#define ARM_PROGMEM PROGMEM
#else
#define ARM_PROGMEM
#endif
static const Params kParamDefaults ARM_PROGMEM = {
    PARAMS_MAGIC, PARAMS_VERSION,
    {1500, 1500 - 90 * ARM_K, 1500 + 90 * ARM_K, 1500, 1500, 1000},   // us0
    {ARM_K, ARM_K, ARM_K, ARM_K, ARM_K, 10.0f},                         // usdeg
    {-90, 40, -160, -90, -90, 0},                                       // qmin (J2 40: an MG996R cannot hold the
                                                                        // arm stretched out; with a DS3225: LIM 2 10 170)
    {90, 170, 0, 90, 90, 100},                                          // qmax
    {90, 60, 90, 120, 150, 150},                                        // vmax
    {180, 120, 180, 300, 400, 400},                                     // amax
    {0, 90, -90, 0, 0, 0},                                              // home
    {0, 100, -140, 40, 0, 0},                                           // park
    75, 120, 130, 180,                                                  // d1 l2 l3 l4 (common aluminium 6-DOF kit, measure yours)
    10, 60,                                                             // z_min r_min
    8, 30,                                                              // deadband expo (%)
    80, 60,                                                             // v_lin v_ang
    2,                                                                  // speed_lvl
    3,                                                                  // grip_backoff
    300,                                                                // dwell_ms
    3000,                                                               // estop_mv
    200,                                                                // vdiv_x100
    500, 2500,                                                          // pulse_min pulse_max
};
#undef ARM_K

inline void paramsDefaults(Params& p) {
#if defined(__AVR__)
  memcpy_P(&p, &kParamDefaults, sizeof(Params));
#else
  p = kParamDefaults;
#endif
}
