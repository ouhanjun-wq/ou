#include "storage.h"

#include <Preferences.h>

static const char* NS = "arm";

bool loadParams(Params& p) {
  Preferences prefs;
  if (!prefs.begin(NS, true)) return false;
  Params tmp;
  const size_t n = prefs.getBytes("params", &tmp, sizeof(tmp));
  prefs.end();
  if (n != sizeof(tmp) || tmp.magic != PARAMS_MAGIC || tmp.version != PARAMS_VERSION) return false;
  p = tmp;
  return true;
}

bool saveParams(const Params& p) {
  Preferences prefs;
  if (!prefs.begin(NS, false)) return false;
  const bool ok = prefs.putBytes("params", &p, sizeof(p)) == sizeof(p);
  prefs.end();
  return ok;
}

int loadWaypoints(arm::Waypoint* wp, int max) {
  Preferences prefs;
  if (!prefs.begin(NS, true)) return 0;
  const int n = prefs.getInt("wp_n", 0);
  int got = 0;
  if (n > 0 && n <= max) {
    const size_t bytes = sizeof(arm::Waypoint) * n;
    if (prefs.getBytes("wp", wp, bytes) == bytes) got = n;
  }
  prefs.end();
  return got;
}

bool saveWaypoints(const arm::Waypoint* wp, int n) {
  Preferences prefs;
  if (!prefs.begin(NS, false)) return false;
  bool ok = prefs.putInt("wp_n", n) > 0;
  if (n > 0) ok = ok && prefs.putBytes("wp", wp, sizeof(arm::Waypoint) * n) == sizeof(arm::Waypoint) * n;
  else prefs.remove("wp");
  prefs.end();
  return ok;
}
