// Parameters and the taught waypoints live in NVS flash (survive power-off and re-flashing).
#pragma once
#include "motion.h"
#include "params.h"

bool loadParams(Params& p);          // false: nothing valid stored (defaults are used)
bool saveParams(const Params& p);
int loadWaypoints(arm::Waypoint* wp, int max);
bool saveWaypoints(const arm::Waypoint* wp, int n);
