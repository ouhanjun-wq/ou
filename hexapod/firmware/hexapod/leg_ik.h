// Inverse / forward kinematics of one 3-servo leg (coxa, femur, tibia). Pure C++.
//
// Leg frame: origin on the coxa axis at the height of the femur axis, x pointing straight out
// from the body along the leg's mounting direction, y = z × x, z up.
//
//   coxa  q0: leg swings sideways, 0 = straight out, > 0 = counter-clockwise seen from above
//   femur q1: angle of the femur above horizontal (0 = level, > 0 = up)
//   tibia q2: 0 = tibia at 90° to the femur, > 0 = the knee opens (foot moves outward)
//
// The calibration pose (all three at 0) is "femur level, tibia straight down":
// the foot is at x = L1 + L2, z = -L3. Derivation: docs/gait-and-gestures.md, section 4.
#pragma once
#include <math.h>

namespace hx {

constexpr float DEG = 57.29578f;   // degrees per radian

struct Vec3 {
  float x, y, z;
};

struct LegGeo {
  float l1, l2, l3;   // coxa, femur, tibia length (mm)
};

// Foot position (leg frame, mm) -> joint angles (deg). Returns false if the point is out of
// reach; the angles are then those of the nearest reachable point in the same direction.
inline bool legIK(const LegGeo& g, const Vec3& p, float q[3]) {
  q[0] = atan2f(p.y, p.x) * DEG;
  const float r = sqrtf(p.x * p.x + p.y * p.y) - g.l1;
  float d = sqrtf(r * r + p.z * p.z);
  const float dmax = g.l2 + g.l3 - 0.5f;
  const float dmin = fabsf(g.l2 - g.l3) + 0.5f;
  bool ok = true;
  if (d > dmax) { d = dmax; ok = false; }
  if (d < dmin) { d = dmin; ok = false; }
  const float a1 = atan2f(p.z, r);
  float c2 = (g.l2 * g.l2 + d * d - g.l3 * g.l3) / (2 * g.l2 * d);
  float ck = (g.l2 * g.l2 + g.l3 * g.l3 - d * d) / (2 * g.l2 * g.l3);
  c2 = c2 > 1 ? 1 : (c2 < -1 ? -1 : c2);
  ck = ck > 1 ? 1 : (ck < -1 ? -1 : ck);
  q[1] = (a1 + acosf(c2)) * DEG;
  q[2] = acosf(ck) * DEG - 90.0f;
  return ok;
}

// Joint angles (deg) -> foot position (leg frame, mm).
inline Vec3 legFK(const LegGeo& g, const float q[3]) {
  const float t0 = q[0] / DEG, t1 = q[1] / DEG;
  const float tib = t1 - (90.0f - q[2]) / DEG;   // tibia direction above horizontal
  const float r = g.l1 + g.l2 * cosf(t1) + g.l3 * cosf(tib);
  const float z = g.l2 * sinf(t1) + g.l3 * sinf(tib);
  return Vec3{r * cosf(t0), r * sinf(t0), z};
}

// Body frame (x forward, y left, z up, origin at the body centre on the coxa plane) -> leg frame
// of a leg mounted at (mx, my) and pointing in direction `ang` (deg, 0 = forward, 90 = left).
inline Vec3 bodyToLeg(const Vec3& p, float mx, float my, float ang) {
  const float a = ang / DEG, c = cosf(a), s = sinf(a);
  const float dx = p.x - mx, dy = p.y - my;
  return Vec3{dx * c + dy * s, -dx * s + dy * c, p.z};
}

inline Vec3 legToBody(const Vec3& p, float mx, float my, float ang) {
  const float a = ang / DEG, c = cosf(a), s = sinf(a);
  return Vec3{mx + p.x * c - p.y * s, my + p.x * s + p.y * c, p.z};
}

}  // namespace hx
