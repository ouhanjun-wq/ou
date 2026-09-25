// Forward / inverse kinematics of a 5-axis arm (J1 yaw, J2-J4 pitch in one vertical
// plane, J5 roll) — the usual "6-DOF" hobby arm: 5 joints + gripper.
// Math and sign conventions: docs/motion-control.md section 2.
// Header-only and free of Arduino code, so it is unit-tested on the PC.
#pragma once
#include <math.h>

#include "pgm.h"

namespace kin {

constexpr float DEG = 3.14159265f / 180.0f;

struct Geometry {
  float d1, l2, l3, l4;   // mm, see params.h
};

// Tool pose: tool point position (mm, base frame: x forward, y left, z up from the table),
// tool pitch (deg, 0 = level, -90 = pointing down) and wrist roll (deg).
struct Pose {
  float x, y, z, pitch, roll;
};

enum Result { OK = 0, UNREACHABLE, TOO_CLOSE };

inline float wrap180(float a) {
  while (a > 180.0f) a -= 360.0f;
  while (a <= -180.0f) a += 360.0f;
  return a;
}

// q[0..4] = J1..J5 in degrees.
ARM_NOINLINE inline Pose forward(const Geometry& g, const float* q) {
  const float a2 = q[1] * DEG;
  const float a23 = (q[1] + q[2]) * DEG;
  const float phi = (q[1] + q[2] + q[3]) * DEG;
  const float r = g.l2 * cosf(a2) + g.l3 * cosf(a23) + g.l4 * cosf(phi);
  const float z = g.d1 + g.l2 * sinf(a2) + g.l3 * sinf(a23) + g.l4 * sinf(phi);
  Pose p;
  p.x = r * cosf(q[0] * DEG);
  p.y = r * sinf(q[0] * DEG);
  p.z = z;
  p.pitch = wrap180(q[1] + q[2] + q[3]);
  p.roll = q[4];
  return p;
}

// Elbow-up solution. rMin keeps the tool away from the base axis, where the base angle
// is undefined. Joint limits are checked by the caller.
ARM_NOINLINE inline Result inverse(const Geometry& g, const Pose& p, float rMin, float* q) {
  const float r = sqrtf(p.x * p.x + p.y * p.y);
  if (r < rMin) return TOO_CLOSE;
  const float phi = p.pitch * DEG;
  const float rw = r - g.l4 * cosf(phi);             // wrist-pitch axis in the arm plane
  const float zw = p.z - g.d1 - g.l4 * sinf(phi);
  const float c3 = (rw * rw + zw * zw - g.l2 * g.l2 - g.l3 * g.l3) / (2.0f * g.l2 * g.l3);
  if (c3 > 1.0f || c3 < -1.0f) return UNREACHABLE;
  const float t3 = -acosf(c3);                        // negative = elbow above the wrist line
  const float t2 = atan2f(zw, rw) - atan2f(g.l3 * sinf(t3), g.l2 + g.l3 * cosf(t3));
  q[0] = atan2f(p.y, p.x) / DEG;
  q[1] = t2 / DEG;
  q[2] = t3 / DEG;
  q[3] = wrap180(p.pitch - q[1] - q[2]);
  q[4] = p.roll;
  return OK;
}

}  // namespace kin
