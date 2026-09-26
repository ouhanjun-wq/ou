#include "params.h"

#include <string.h>

Params P;

#define PARAM(field, lo, hi) {#field, offsetof(Params, field), lo, hi}

static const ParamInfo kTable[] = {
  PARAM(coxa, 5, 80),            PARAM(femur, 20, 150),         PARAM(tibia, 20, 200),
  PARAM(front_x, 0, 200),        PARAM(front_y, 0, 150),        PARAM(front_angle, 0, 90),
  PARAM(mid_y, 0, 150),
  PARAM(rear_x, 0, 200),         PARAM(rear_y, 0, 150),         PARAM(rear_angle, 90, 180),
  PARAM(stance, 20, 250),        PARAM(height, 10, 200),
  PARAM(height_min, 5, 200),     PARAM(height_max, 10, 250),
  PARAM(sit_height, 0, 100),     PARAM(sit_stance, 20, 250),
  PARAM(gait, 0, 2),             PARAM(cycle_s, 0.3f, 4.0f),
  PARAM(step_h, 0, 100),         PARAM(step_min, 0, 100),       PARAM(step_max, 0, 120),
  PARAM(stride_max, 5, 150),
  PARAM(accel, 10, 2000),        PARAM(yaw_accel, 10, 1000),
  PARAM(move_rate, 5, 300),      PARAM(tilt_rate, 5, 300),
  PARAM(max_vx, 0, 500),         PARAM(max_vy, 0, 500),         PARAM(max_wz, 0, 360),
  PARAM(tilt_max, 0, 30),        PARAM(twist_max, 0, 40),       PARAM(shift_max, 0, 60),
  PARAM(deadband, 0, 0.5f),
  PARAM(us_per_deg, 5, 20),      PARAM(coxa_lim, 10, 90),
  PARAM(femur_min, -90, 0),      PARAM(femur_max, 0, 90),
  PARAM(tibia_min, -90, 0),      PARAM(tibia_max, 0, 90),
  PARAM(i2c_sda, 0, 39),         PARAM(i2c_scl, 0, 39),
};

#undef PARAM

static float* fieldPtr(const ParamInfo& pi) {
  return reinterpret_cast<float*>(reinterpret_cast<char*>(&P) + pi.offset);
}

const ParamInfo* paramTable(size_t& count) {
  count = sizeof(kTable) / sizeof(kTable[0]);
  return kTable;
}

const ParamInfo* paramFind(const char* name) {
  for (const auto& pi : kTable)
    if (strcmp(pi.name, name) == 0) return &pi;
  return nullptr;
}

bool paramGet(const char* name, float& v) {
  const ParamInfo* pi = paramFind(name);
  if (!pi) return false;
  v = *fieldPtr(*pi);
  return true;
}

bool paramSet(const char* name, float v) {
  const ParamInfo* pi = paramFind(name);
  if (!pi) return false;
  if (v < pi->lo) v = pi->lo;
  if (v > pi->hi) v = pi->hi;
  *fieldPtr(*pi) = v;
  return true;
}

#ifdef ARDUINO
#include <Preferences.h>

bool paramsLoad() {
  paramsDefaults(P);
  Preferences prefs;
  if (!prefs.begin("hexapod", true)) return false;
  bool ok = prefs.getUInt("pver", 0) == PARAMS_VERSION &&
            prefs.getBytesLength("params") == sizeof(Params);
  if (ok) prefs.getBytes("params", &P, sizeof(Params));
  prefs.end();
  return ok;
}

bool paramsSave() {
  Preferences prefs;
  if (!prefs.begin("hexapod", false)) return false;
  const bool ok = prefs.putBytes("params", &P, sizeof(Params)) == sizeof(Params);
  prefs.putUInt("pver", PARAMS_VERSION);
  prefs.end();
  return ok;
}
#else
bool paramsLoad() { paramsDefaults(P); return false; }
bool paramsSave() { return true; }
#endif
