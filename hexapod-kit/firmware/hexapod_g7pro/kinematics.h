// 3-DOF hexapod leg kinematics. Pure math, no Arduino: unit-tested on the PC (tests/test_core.cpp).
//
// Leg frame: origin on the coxa (hip yaw) axis at femur-pivot height, +x straight out along the
// leg's mount direction, +y to the left of it (counter-clockwise seen from above), +z up.
//
// Joint angles (degrees), all 0 in the "assembly pose" = every servo at its 1500 us centre:
//   coxa  : 0 = leg points straight out,  + = swings counter-clockwise (seen from above)
//   femur : 0 = femur horizontal,          + = femur up
//   tibia : 0 = tibia at 90 deg to femur (straight down when the femur is horizontal),
//           + = knee opens (foot moves outwards)
#pragma once
#include <math.h>

namespace kin {

constexpr float kPi = 3.14159265358979f;
constexpr float kDeg = 180.0f / kPi;

struct Vec3 {
  float x, y, z;
};

struct LegGeom {
  float coxa, femur, tibia;   // mm: hip-yaw axis -> femur pivot, femur pivot -> knee, knee -> foot tip
};

struct Joints {
  float coxa, femur, tibia;   // degrees
};

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Foot position (leg frame) -> joint angles. Returns false when the target is out of reach;
// the angles then point at the nearest reachable spot in the same direction.
inline bool inverse(const LegGeom& g, const Vec3& p, Joints& j) {
  j.coxa = atan2f(p.y, p.x) * kDeg;
  const float r = sqrtf(p.x * p.x + p.y * p.y) - g.coxa;   // horizontal distance femur pivot -> foot
  const float z = p.z;
  float d = sqrtf(r * r + z * z);
  const float dmin = fabsf(g.femur - g.tibia) + 1.0f;
  const float dmax = g.femur + g.tibia - 0.5f;
  const bool ok = d >= dmin && d <= dmax;
  d = clampf(d, dmin, dmax);
  const float a1 = atan2f(z, r);
  const float a2 = acosf(clampf((g.femur * g.femur + d * d - g.tibia * g.tibia) / (2 * g.femur * d), -1, 1));
  const float knee = acosf(clampf((g.femur * g.femur + g.tibia * g.tibia - d * d) / (2 * g.femur * g.tibia), -1, 1));
  j.femur = (a1 + a2) * kDeg;
  j.tibia = knee * kDeg - 90.0f;
  return ok;
}

// Joint angles -> foot position (leg frame).
inline Vec3 forward(const LegGeom& g, const Joints& j) {
  const float f = j.femur / kDeg;
  const float t = (j.femur + j.tibia - 90.0f) / kDeg;          // tibia direction from horizontal
  const float r = g.coxa + g.femur * cosf(f) + g.tibia * cosf(t);
  const float z = g.femur * sinf(f) + g.tibia * sinf(t);
  const float c = j.coxa / kDeg;
  return {r * cosf(c), r * sinf(c), z};
}

// Body frame (x forward, y left, z up, origin at the body centre, femur-pivot height) ->
// leg frame of a leg mounted at (mx, my) pointing at angle a (degrees, counter-clockwise from +x).
inline Vec3 bodyToLeg(const Vec3& p, float mx, float my, float a_deg) {
  const float a = a_deg / kDeg, c = cosf(a), s = sinf(a);
  const float dx = p.x - mx, dy = p.y - my;
  return {c * dx + s * dy, -s * dx + c * dy, p.z};
}

inline Vec3 legToBody(const Vec3& p, float mx, float my, float a_deg) {
  const float a = a_deg / kDeg, c = cosf(a), s = sinf(a);
  return {mx + c * p.x - s * p.y, my + s * p.x + c * p.y, p.z};
}

// Body pose: move the body by (tx, ty, tz) and rotate it by roll / pitch / yaw (degrees) while
// the feet stay put. Returns where a foot at `f` (neutral body frame) is, seen from the moved body.
inline Vec3 applyBodyPose(const Vec3& f, float tx, float ty, float tz, float roll, float pitch, float yaw) {
  float x = f.x - tx, y = f.y - ty, z = f.z - tz;
  // inverse rotation R^T = Rx(-roll) Ry(-pitch) Rz(-yaw), applied yaw first
  const float cy = cosf(-yaw / kDeg), sy = sinf(-yaw / kDeg);
  float x1 = cy * x - sy * y, y1 = sy * x + cy * y, z1 = z;
  const float cp = cosf(-pitch / kDeg), sp = sinf(-pitch / kDeg);
  float x2 = cp * x1 + sp * z1, y2 = y1, z2 = -sp * x1 + cp * z1;
  const float cr = cosf(-roll / kDeg), sr = sinf(-roll / kDeg);
  return {x2, cr * y2 - sr * z2, sr * y2 + cr * z2};
}

}  // namespace kin
