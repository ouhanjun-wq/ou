// Gait engine: moves the six feet so that the body walks with a given velocity. Pure C++.
//
// A step cycle has phase 0..1. Leg i is at phase (phase + offset[i]) mod 1: below `duty` its foot
// is on the ground (stance) and slides backwards under the body; above it the foot is in the air
// (swing), lifts on a half-sine and lands ahead of its home position, so that it passes home in
// the middle of the next stance. Details and figures: docs/gait-and-gestures.md, section 5.
#pragma once
#include <math.h>

#include "leg_ik.h"
#include "params.h"

namespace hx {

struct GaitTable {
  float duty;           // fraction of the cycle a foot is on the ground
  float offset[NLEG];   // LF LM LR RF RM RR
};

inline const GaitTable& gaitTable(uint8_t g) {
  static const GaitTable T[GAIT_COUNT] = {
      // tripod: {LF, LR, RM} and {LM, RF, RR} step in turn, 3 feet always down
      {0.5f, {0.0f, 0.5f, 0.0f, 0.5f, 0.0f, 0.5f}},
      // ripple: on each side the rear, middle, front leg step in turn; the sides half a cycle
      // apart. 4 feet always down.
      {2.0f / 3, {0.0f, 1.0f / 3, 2.0f / 3, 0.5f, 5.0f / 6, 1.0f / 6}},
      // wave: one leg at a time, LR LM LF RR RM RF. 5 feet always down.
      {5.0f / 6, {3.0f / 6, 4.0f / 6, 5.0f / 6, 0.0f, 1.0f / 6, 2.0f / 6}},
  };
  return T[g < GAIT_COUNT ? g : 0];
}

class Gait {
 public:
  Vec3 foot[NLEG];      // x, y in the body frame (mm); z = height above the ground (0 = down)
  bool air[NLEG];       // foot in the air
  float phase = 0;
  bool running = false; // false = all feet down at home, the cycle is paused
  uint8_t gait = GAIT_TRIPOD;
  uint8_t nextGait = GAIT_TRIPOD;   // applied when the robot next stands still

  void reset(const Vec3 home[NLEG]) {
    for (int i = 0; i < NLEG; ++i) {
      foot[i] = Vec3{home[i].x, home[i].y, 0};
      air[i] = false;
    }
    phase = 0;
    running = false;
    gait = nextGait;
    syncWindows();
  }

  float period(const Params& p) const { return p.period[gait]; }
  float duty() const { return gaitTable(gait).duty; }

  // Largest body speed (mm/s) for the gait: one stance moves a foot by at most `stride`.
  float vmax(const Params& p) const { return p.stride / (p.period[gait] * duty()); }

  // vx, vy (mm/s) and w (rad/s, > 0 = counter-clockwise): body velocity. dt in seconds.
  void update(const Params& p, const Vec3 home[NLEG], float vx, float vy, float w, float dt) {
    const bool cmd = fabsf(vx) > 0.5f || fabsf(vy) > 0.5f || fabsf(w) > 0.005f;
    if (!running) {
      if (!cmd) {
        for (int i = 0; i < NLEG; ++i) foot[i] = Vec3{home[i].x, home[i].y, 0};
        if (nextGait != gait) {
          gait = nextGait;
          syncWindows();
        }
        return;
      }
      running = true;
    }
    const GaitTable& t = gaitTable(gait);
    const float T = p.period[gait];
    phase += dt / T;
    if (phase >= 1) phase -= 1;

    const float c = cosf(-w * dt), s = sinf(-w * dt);
    bool settled = !cmd;
    for (int i = 0; i < NLEG; ++i) {
      float ph = phase + t.offset[i];
      if (ph >= 1) ph -= 1;
      const bool swing = ph >= t.duty;
      if (swing && !inSwing_[i]) {       // this foot's swing starts now
        const float dx = foot[i].x - home[i].x, dy = foot[i].y - home[i].y;
        // when stopping, a foot that is already home stays down
        air[i] = cmd || dx * dx + dy * dy > 1.5f * 1.5f;
        start_[i] = foot[i];
      }
      inSwing_[i] = swing;
      if (!swing) air[i] = false;

      if (air[i]) {
        // Land where the foot will pass home in mid-stance: home + u * (stance time / 2),
        // u = velocity of the ground under that foot = v + w x home.
        const float half = T * t.duty * 0.5f;
        const float tx = home[i].x + (vx - w * home[i].y) * half;
        const float ty = home[i].y + (vy + w * home[i].x) * half;
        const float k = (ph - t.duty) / (1 - t.duty);     // 0 .. 1 through the swing
        const float b = 0.5f - 0.5f * cosf(3.14159265f * k);
        foot[i].x = start_[i].x + (tx - start_[i].x) * b;
        foot[i].y = start_[i].y + (ty - start_[i].y) * b;
        foot[i].z = p.lift * sinf(3.14159265f * k);
        settled = false;
      } else {
        // On the ground: the body moves by (v, w) dt, so the foot moves the other way.
        const float x = foot[i].x, y = foot[i].y;
        foot[i].x = x * c - y * s - vx * dt;
        foot[i].y = x * s + y * c - vy * dt;
        foot[i].z = 0;
        const float dx = foot[i].x - home[i].x, dy = foot[i].y - home[i].y;
        if (dx * dx + dy * dy > 1.5f * 1.5f) settled = false;
      }
    }
    if (settled) {
      running = false;
      for (int i = 0; i < NLEG; ++i) foot[i] = Vec3{home[i].x, home[i].y, 0};
    }
  }

  int feetDown() const {
    int n = 0;
    for (int i = 0; i < NLEG; ++i) n += air[i] ? 0 : 1;
    return n;
  }

 private:
  // A foot whose swing window is already open when the cycle (re)starts stays down until its
  // next window, instead of jumping into the air half way through a step.
  void syncWindows() {
    const GaitTable& t = gaitTable(gait);
    for (int i = 0; i < NLEG; ++i) {
      float ph = phase + t.offset[i];
      if (ph >= 1) ph -= 1;
      inSwing_[i] = ph >= t.duty + 0.02f;   // a window opening right now still counts
    }
  }

  Vec3 start_[NLEG];
  bool inSwing_[NLEG] = {false, false, false, false, false, false};
};

}  // namespace hx
