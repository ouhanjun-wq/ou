// Digital filters for the gyro / attitude chain. Header-only, no Arduino dependency
// (unit-tested on the host, see firmware/tests).
#pragma once
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// First-order low-pass:  y += k (x - y),  k = dt / (RC + dt),  RC = 1 / (2 pi fc)
struct PT1 {
  float y = 0.0f, k = 1.0f;
  bool primed = false;

  void setCutoff(float fc, float fs) {
    if (fc <= 0.0f) { k = 1.0f; return; }  // pass-through
    const float dt = 1.0f / fs, rc = 1.0f / (2.0f * (float)M_PI * fc);
    k = dt / (rc + dt);
  }
  float apply(float x) {
    if (!primed) { y = x; primed = true; }
    y += k * (x - y);
    return y;
  }
  void reset() { primed = false; }
};

// Second-order notch (RBJ cookbook), Direct Form I. Coefficients may be retuned
// every cycle to follow the flapping frequency; state is kept across retunes.
struct Notch {
  float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
  float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
  bool bypass = true;

  void set(float f0, float fs, float q) {
    if (f0 < 0.5f || f0 > 0.45f * fs || q <= 0.0f) { bypass = true; return; }
    const float w0 = 2.0f * (float)M_PI * f0 / fs;
    const float alpha = sinf(w0) / (2.0f * q);
    const float n = 1.0f / (1.0f + alpha);
    b0 = b2 = n;
    b1 = a1 = -2.0f * cosf(w0) * n;
    a2 = (1.0f - alpha) * n;
    bypass = false;
  }
  float apply(float x) {
    float y = bypass ? x : b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1; x1 = x;
    y2 = y1; y1 = y;
    return y;
  }
};

// Moving average whose window equals one flapping period (N = fs / f).
// Its zeros sit exactly on f, 2f, 3f ... so every flapping harmonic is removed.
// Window changes are glitch-free: the history of the last MAXN samples is kept.
template <int MAXN>
struct StrokeAvg {
  float h[MAXN];
  int head = 0, n = 1, sinceRecompute = 0;
  float sum = 0.0f;
  bool primed = false;

  void setWindow(int newN) {
    if (newN < 1) newN = 1;
    if (newN > MAXN) newN = MAXN;
    if (newN == n) return;
    n = newN;
    recompute();
  }
  int window() const { return n; }

  float apply(float x) {
    if (!primed) {
      for (int i = 0; i < MAXN; ++i) h[i] = x;
      sum = x * n;
      primed = true;
    }
    const int leaving = (head - n + MAXN) % MAXN;
    sum += x - h[leaving];
    h[head] = x;
    head = (head + 1) % MAXN;
    if (++sinceRecompute >= 4 * MAXN) recompute();  // bound float drift
    return sum / n;
  }

 private:
  void recompute() {
    sum = 0.0f;
    for (int i = 1; i <= n; ++i) sum += h[(head - i + MAXN) % MAXN];
    sinceRecompute = 0;
  }
};

// Boxcar decimator: averages R input samples into one output sample.
// Zeros at fs/R, 2fs/R ... suppress aliasing when going 1 kHz -> 200 Hz.
struct Decimator {
  float acc = 0.0f;
  int count = 0;
  bool push(float x, int r, float& out) {
    acc += x;
    if (++count < r) return false;
    out = acc / r;
    acc = 0.0f;
    count = 0;
    return true;
  }
};

// Accelerometer trust in [0, 1]: 1 when |a| ~= 1 g, 0 when flapping accelerations dominate.
inline float accTrust(float ax, float ay, float az) {
  const float err = fabsf(sqrtf(ax * ax + ay * ay + az * az) - 1.0f);
  if (err < 0.1f) return 1.0f;
  if (err > 0.3f) return 0.0f;
  return (0.3f - err) / 0.2f;
}
