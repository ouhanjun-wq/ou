// Mahony complementary filter. Header-only, no Arduino dependency.
//
// Body frame: FRD (x forward, y right, z down). Earth frame: NED.
// Positive roll = right wing down, positive pitch = nose up, positive yaw = nose right.
// Accelerometer input is specific force in g (level and still: (0, 0, -1)).
#pragma once
#include <math.h>

struct Mahony {
  float q0 = 1, q1 = 0, q2 = 0, q3 = 0;
  float ix = 0, iy = 0, iz = 0;          // integral feedback = gyro bias estimate (rad/s)
  float roll = 0, pitch = 0, yaw = 0;    // degrees

  void reset() {
    q0 = 1; q1 = q2 = q3 = 0;
    ix = iy = iz = 0;
    computeEuler();
  }

  // Initialise roll / pitch from a still accelerometer reading, yaw = 0.
  void initFromAccel(const float a[3]) {
    const float r = atan2f(-a[1], -a[2]);
    const float p = atan2f(a[0], sqrtf(a[1] * a[1] + a[2] * a[2]));
    const float cr = cosf(r * 0.5f), sr = sinf(r * 0.5f);
    const float cp = cosf(p * 0.5f), sp = sinf(p * 0.5f);
    q0 = cr * cp; q1 = sr * cp; q2 = cr * sp; q3 = -sr * sp;
    ix = iy = iz = 0;
    computeEuler();
  }

  // g: rad/s, a: g, accWeight: 0..1 (see accTrust), dt: s
  void update(const float g[3], const float a[3], float accWeight, float kp, float ki, float dt) {
    float gx = g[0], gy = g[1], gz = g[2];
    const float an = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);

    if (accWeight > 0.0f && an > 0.1f) {
      // measured "down" direction in body frame
      const float mx = -a[0] / an, my = -a[1] / an, mz = -a[2] / an;
      // estimated "down" direction: R^T * (0, 0, 1)
      const float vx = 2.0f * (q1 * q3 - q0 * q2);
      const float vy = 2.0f * (q0 * q1 + q2 * q3);
      const float vz = q0 * q0 - q1 * q1 - q2 * q2 + q3 * q3;
      // error = measured x estimated, weighted by accelerometer trust
      const float ex = (my * vz - mz * vy) * accWeight;
      const float ey = (mz * vx - mx * vz) * accWeight;
      const float ez = (mx * vy - my * vx) * accWeight;
      if (ki > 0.0f) {
        ix = clampf(ix + ki * ex * dt, 0.1f);
        iy = clampf(iy + ki * ey * dt, 0.1f);
        iz = clampf(iz + ki * ez * dt, 0.1f);
      }
      gx += kp * ex + ix;
      gy += kp * ey + iy;
      gz += kp * ez + iz;
    }

    // q_dot = 0.5 * q (x) (0, g)
    const float h = 0.5f * dt;
    const float a0 = q0, a1 = q1, a2 = q2, a3 = q3;
    q0 += (-a1 * gx - a2 * gy - a3 * gz) * h;
    q1 += ( a0 * gx + a2 * gz - a3 * gy) * h;
    q2 += ( a0 * gy - a1 * gz + a3 * gx) * h;
    q3 += ( a0 * gz + a1 * gy - a2 * gx) * h;
    const float n = 1.0f / sqrtf(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
    q0 *= n; q1 *= n; q2 *= n; q3 *= n;
    computeEuler();
  }

 private:
  static float clampf(float v, float lim) { return v > lim ? lim : (v < -lim ? -lim : v); }

  void computeEuler() {
    const float r2d = 57.29577951f;
    roll = atan2f(2.0f * (q0 * q1 + q2 * q3), 1.0f - 2.0f * (q1 * q1 + q2 * q2)) * r2d;
    float s = 2.0f * (q0 * q2 - q3 * q1);
    s = s > 1.0f ? 1.0f : (s < -1.0f ? -1.0f : s);
    pitch = asinf(s) * r2d;
    yaw = atan2f(2.0f * (q0 * q3 + q1 * q2), 1.0f - 2.0f * (q2 * q2 + q3 * q3)) * r2d;
  }
};
