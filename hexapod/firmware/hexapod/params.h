// Tunable parameters of the hexapod, stored in the ESP32's flash (SAVE).
//
// Legs (index 0..5):  0 LF left front | 1 LM left middle | 2 LR left rear
//                     3 RF right front | 4 RM right middle | 5 RR right rear
// Joints (0..2):      0 coxa | 1 femur | 2 tibia        servo index s = leg * 3 + joint
// Servo pulse:        us = 1500 + trim[s] + dir[s] * us_per_deg * angle
// Angle conventions:  leg_ik.h. Body frame: x forward, y left, z up (mm).
#pragma once
#include <stdint.h>
#include <string.h>

#include "protocol.h"

namespace hx {

constexpr int NLEG = 6;
constexpr int NSERVO = 18;
constexpr uint16_t PARAMS_MAGIC = 0x4E58;
constexpr uint8_t PARAMS_VERSION = 1;

enum Backend : uint8_t { BACKEND_AUTO = 0, BACKEND_PCA9685 = 1, BACKEND_GPIO = 2 };
enum GaitId : uint8_t { GAIT_TRIPOD = 0, GAIT_RIPPLE = 1, GAIT_WAVE = 2, GAIT_COUNT = 3 };

struct Params {
  uint16_t magic;
  uint8_t version;

  // geometry (mm, deg), measured on your robot (assembly guide step 5)
  float l1, l2, l3;              // coxa, femur, tibia
  float mount[NLEG][3];          // coxa axis x, y and the leg's outward direction (deg)

  // servos
  int16_t trim[NSERVO];          // us offset so that angle 0 is exact (calibration pose)
  int8_t dir[NSERVO];            // +1 / -1
  uint8_t ch[NSERVO];            // PCA9685: 0..15 on board 0x40, 16..31 on board 0x41
                                 // direct GPIO boards: the GPIO number of each servo header
  uint8_t backend;               // Backend
  float us_per_deg;              // MG90S: 2000 us for 180 deg
  int16_t pulse_min, pulse_max;  // absolute limits (us)
  int16_t qmin[3], qmax[3];      // joint limits (deg) for coxa, femur, tibia

  // posture and gait
  float height;                  // standing: coxa plane above the ground (mm)
  float sit_height;              // sitting: body resting on the ground
  float reach;                   // foot distance from the coxa axis, horizontally (mm)
  float lift;                    // foot lift during a step (mm)
  float stride;                  // longest step (mm); sets the top speed
  float period[GAIT_COUNT];      // step cycle (s) of each gait
  uint8_t gait;                  // GaitId at power-up
  float accel;                   // 0 -> top speed in 1 / accel seconds
  float body_tilt;               // largest body pitch / roll in BODY mode (deg)

  // link, battery, options
  uint8_t link_id;               // must match the glove (LINK command on both)
  uint16_t vbat_warn_mv;         // below this: glove buzzes
  uint16_t vbat_cut_mv;          // below this for 3 s: sit down, servos off
  float vdiv;                    // voltage sensor module ratio (30k + 7.5k = 5.0)
  uint16_t obstacle_mm;          // HC-SR04: no walking forward closer than this (0 = off)
};

inline void paramsDefaults(Params& p) {
  memset(&p, 0, sizeof(p));
  p.magic = PARAMS_MAGIC;
  p.version = PARAMS_VERSION;
  // Placeholders for a small MG90S hexapod: measure yours and use GEO / MOUNT.
  p.l1 = 28;
  p.l2 = 45;
  p.l3 = 75;
  const float m[NLEG][3] = {
      {60, 40, 45},  {0, 50, 90},  {-60, 40, 135},       // LF LM LR
      {60, -40, -45}, {0, -50, -90}, {-60, -40, -135},   // RF RM RR
  };
  memcpy(p.mount, m, sizeof(m));
  for (int s = 0; s < NSERVO; ++s) {
    p.trim[s] = 0;
    p.dir[s] = 1;
    // left legs on board 0x40 channels 0..8, right legs on board 0x41 channels 0..8
    p.ch[s] = (uint8_t)(s < 9 ? s : 16 + (s - 9));
  }
  p.backend = BACKEND_AUTO;
  p.us_per_deg = 2000.0f / 180.0f;
  p.pulse_min = 500;
  p.pulse_max = 2500;
  const int16_t qmin[3] = {-45, -60, -60}, qmax[3] = {45, 80, 60};
  memcpy(p.qmin, qmin, sizeof(qmin));
  memcpy(p.qmax, qmax, sizeof(qmax));
  p.height = 55;
  p.sit_height = 15;
  p.reach = 75;
  p.lift = 22;
  p.stride = 34;
  p.period[GAIT_TRIPOD] = 0.9f;
  p.period[GAIT_RIPPLE] = 1.2f;
  p.period[GAIT_WAVE] = 2.0f;
  p.gait = GAIT_TRIPOD;
  p.accel = 2.0f;
  p.body_tilt = 10;
  p.link_id = DEFAULT_LINK_ID;
  p.vbat_warn_mv = 6800;
  p.vbat_cut_mv = 6300;
  p.vdiv = 5.0f;
  p.obstacle_mm = 150;
}

inline bool paramsValid(const Params& p) {
  return p.magic == PARAMS_MAGIC && p.version == PARAMS_VERSION && p.l2 > 0 && p.l3 > 0 &&
         p.gait < GAIT_COUNT;
}

inline const char* gaitName(uint8_t g) {
  return g == GAIT_TRIPOD ? "TRIPOD" : g == GAIT_RIPPLE ? "RIPPLE" : g == GAIT_WAVE ? "WAVE" : "?";
}

}  // namespace hx
