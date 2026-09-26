// Tunable parameters (mm, deg, s). Change at runtime over USB serial:  "set femur 52"  then "save".
// Struct layout change => bump PARAMS_VERSION (old flash data is discarded).
#pragma once
#include <stddef.h>
#include "gait.h"
#include "teleop.h"

constexpr unsigned PARAMS_VERSION = 1;

struct Params {
  // ---- leg geometry: MEASURE your kit (joint axis to joint axis, knee axis to foot tip) ----
  float coxa, femur, tibia;
  // ---- where the legs attach to the body (hip-yaw axes, body centre = 0,0) ----
  float front_x, front_y, front_angle;   // left front leg; right side is mirrored
  float mid_y;                           // middle legs at (0, +-mid_y)
  float rear_x, rear_y, rear_angle;      // left rear leg at (-rear_x, rear_y)
  // ---- standing / sitting ----
  float stance;                          // foot distance out from the hip-yaw axis
  float height, height_min, height_max;  // body height above the feet
  float sit_height, sit_stance;
  // ---- gait ----
  float gait;                            // default gait: 0 tripod, 1 ripple, 2 wave
  float cycle_s;                         // tripod cycle time
  float step_h, step_min, step_max;      // leg lift
  float stride_max;                      // longest foot travel per step
  float accel, yaw_accel, move_rate, tilt_rate;
  // ---- gamepad ----
  float max_vx, max_vy, max_wz;          // top speed level, full stick
  float tilt_max, twist_max, shift_max;  // Y-button body mode
  float deadband;
  // ---- servos ----
  float us_per_deg;                      // 2000 us / 180 deg = 11.11 for MG90S
  float coxa_lim;                        // |coxa| limit
  float femur_min, femur_max, tibia_min, tibia_max;
  float i2c_sda, i2c_scl;                // I2C pins (PCA9685 boards)
};

inline void paramsDefaults(Params& p) {
  // Placeholder geometry of a typical MG90S 18-servo acrylic hexapod: measure yours and "set".
  p.coxa = 28; p.femur = 50; p.tibia = 75;
  p.front_x = 60; p.front_y = 40; p.front_angle = 45;
  p.mid_y = 55;
  p.rear_x = 60; p.rear_y = 40; p.rear_angle = 135;

  p.stance = 80; p.height = 60; p.height_min = 35; p.height_max = 90;
  p.sit_height = 15; p.sit_stance = 95;

  p.gait = 0; p.cycle_s = 0.8f;
  p.step_h = 30; p.step_min = 10; p.step_max = 50;
  p.stride_max = 50;
  p.accel = 250; p.yaw_accel = 180; p.move_rate = 60; p.tilt_rate = 45;

  p.max_vx = 120; p.max_vy = 80; p.max_wz = 60;
  p.tilt_max = 12; p.twist_max = 15; p.shift_max = 20;
  p.deadband = 0.1f;

  p.us_per_deg = 11.11f;
  p.coxa_lim = 60; p.femur_min = -70; p.femur_max = 80; p.tibia_min = -80; p.tibia_max = 70;
  p.i2c_sda = 21; p.i2c_scl = 22;
}

struct ParamInfo {
  const char* name;
  size_t offset;
  float lo, hi;
};

extern Params P;

const ParamInfo* paramTable(size_t& count);
const ParamInfo* paramFind(const char* name);
bool paramGet(const char* name, float& v);
bool paramSet(const char* name, float v);     // clamps to the allowed range; false if unknown
bool paramsLoad();                            // false = no saved data (defaults used)
bool paramsSave();

inline gait::Config gaitConfig(const Params& p) {
  gait::Config c;
  c.geom = {p.coxa, p.femur, p.tibia};
  c.front_x = p.front_x; c.front_y = p.front_y; c.front_angle = p.front_angle;
  c.mid_y = p.mid_y;
  c.rear_x = p.rear_x; c.rear_y = p.rear_y; c.rear_angle = p.rear_angle;
  c.cycle_s = p.cycle_s;
  c.step_h = p.step_h;
  c.stride_max = p.stride_max;
  c.accel = p.accel; c.yaw_accel = p.yaw_accel;
  c.move_rate = p.move_rate; c.tilt_rate = p.tilt_rate;
  return c;
}

inline teleop::Limits teleopLimits(const Params& p) {
  teleop::Limits l;
  l.max_vx = p.max_vx; l.max_vy = p.max_vy; l.max_wz = p.max_wz;
  l.height = p.height; l.height_min = p.height_min; l.height_max = p.height_max;
  l.sit_height = p.sit_height;
  l.stance = p.stance; l.sit_stance = p.sit_stance;
  l.step_min = p.step_min; l.step_max = p.step_max;
  l.tilt_max = p.tilt_max; l.twist_max = p.twist_max; l.shift_max = p.shift_max;
  l.deadband = p.deadband;
  return l;
}
