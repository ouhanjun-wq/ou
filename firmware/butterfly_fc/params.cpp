#include "params.h"

#include <Preferences.h>
#include <string.h>

Params P;
volatile bool gParamsDirty = true;

#define PARAM(field, lo, hi) {#field, offsetof(Params, field), lo, hi}

static const ParamInfo kTable[] = {
  PARAM(f_min, 0.5f, 10.0f),       PARAM(f_max, 0.5f, 12.0f),
  PARAM(amp_min, 0.0f, 70.0f),     PARAM(amp_max, 0.0f, 70.0f),
  PARAM(center, -30.0f, 40.0f),    PARAM(squareness, 0.0f, 1.0f),
  PARAM(thr_idle, 0.0f, 0.3f),
  PARAM(man_roll, 0.0f, 40.0f),    PARAM(man_pitch, 0.0f, 40.0f), PARAM(man_yaw, 0.0f, 30.0f),
  PARAM(mix_roll_dir, -1.0f, 1.0f), PARAM(mix_pitch_dir, -1.0f, 1.0f), PARAM(mix_yaw_dir, -1.0f, 1.0f),
  PARAM(servo_us_per_deg, 5.0f, 20.0f),
  PARAM(servo_dir_l, -1.0f, 1.0f), PARAM(servo_dir_r, -1.0f, 1.0f),
  PARAM(trim_l, -30.0f, 30.0f),    PARAM(trim_r, -30.0f, 30.0f),
  PARAM(wing_limit, 10.0f, 85.0f),
  PARAM(imu_yaw_quarter, 0.0f, 3.0f), PARAM(imu_flip, 0.0f, 1.0f),
  PARAM(gyro_lpf_hz, 0.0f, 200.0f), PARAM(acc_lpf_hz, 0.0f, 100.0f), PARAM(dterm_lpf_hz, 0.0f, 90.0f),
  PARAM(notch_q, 0.5f, 10.0f),     PARAM(notch_on, 0.0f, 1.0f),   PARAM(stroke_avg_on, 0.0f, 1.0f),
  PARAM(ahrs_kp, 0.0f, 5.0f),      PARAM(ahrs_ki, 0.0f, 1.0f),
  PARAM(max_roll, 0.0f, 60.0f),    PARAM(max_pitch, 0.0f, 60.0f),
  PARAM(max_yaw_rate, 0.0f, 360.0f), PARAM(pitch_trim, -30.0f, 30.0f),
  PARAM(ang_p_roll, 0.0f, 20.0f),  PARAM(ang_p_pitch, 0.0f, 20.0f), PARAM(head_p, 0.0f, 20.0f),
  PARAM(rate_p_roll, 0.0f, 2.0f),  PARAM(rate_i_roll, 0.0f, 2.0f),  PARAM(rate_d_roll, 0.0f, 0.5f),
  PARAM(rate_p_pitch, 0.0f, 2.0f), PARAM(rate_i_pitch, 0.0f, 2.0f), PARAM(rate_d_pitch, 0.0f, 0.5f),
  PARAM(rate_p_yaw, 0.0f, 2.0f),   PARAM(rate_i_yaw, 0.0f, 2.0f),   PARAM(rate_d_yaw, 0.0f, 0.5f),
  PARAM(i_limit, 0.0f, 30.0f),     PARAM(ff, 0.0f, 1.0f),
  PARAM(thr_hover, 0.1f, 1.0f),    PARAM(max_climb, 0.1f, 3.0f),  PARAM(max_descent, 0.1f, 3.0f),
  PARAM(alt_p, 0.0f, 5.0f),        PARAM(vz_p, 0.0f, 1.0f),       PARAM(vz_i, 0.0f, 1.0f),
  PARAM(baro_lpf_hz, 0.1f, 10.0f),
  PARAM(fs_timeout_ms, 100.0f, 5000.0f),
  PARAM(cells, 1.0f, 3.0f),        PARAM(vbat_ratio, 1.0f, 10.0f), PARAM(vcell_warn, 3.0f, 4.0f),
  PARAM(net_id, 0.0f, 255.0f),
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
  if (!pi || !(v >= pi->minV && v <= pi->maxV)) return false;
  *fieldPtr(*pi) = v;   // 32-bit aligned float store is atomic on ESP32
  gParamsDirty = true;
  return true;
}

bool paramsLoad() {
  paramsDefaults(P);
  Preferences prefs;
  if (!prefs.begin("butterfly", true)) return false;
  bool ok = prefs.getUInt("ver", 0) == PARAMS_VERSION &&
            prefs.getBytesLength("p") == sizeof(Params);
  if (ok) {
    Params tmp;
    ok = prefs.getBytes("p", &tmp, sizeof(tmp)) == sizeof(tmp);
    if (ok) P = tmp;
  }
  prefs.end();
  gParamsDirty = true;
  return ok;
}

bool paramsSave() {
  Preferences prefs;
  if (!prefs.begin("butterfly", false)) return false;
  bool ok = prefs.putBytes("p", &P, sizeof(P)) == sizeof(P);
  ok = ok && prefs.putUInt("ver", PARAMS_VERSION) > 0;
  prefs.end();
  return ok;
}

void paramsReset() {
  paramsDefaults(P);
  gParamsDirty = true;
}
