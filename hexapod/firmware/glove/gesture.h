// Gesture glove logic: finger bend, gesture recognition, hand tilt and the command it sends.
// Pure C++ (no Arduino), unit-tested in firmware/tests. Theory: docs/gait-and-gestures.md.
#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "protocol.h"

namespace hx {

constexpr int NF = 5;
enum Finger { THUMB = 0, INDEX = 1, MIDDLE = 2, RING = 3, PINKY = 4 };

enum Gesture : uint8_t {
  G_NONE = 0,     // not a known shape: stop
  G_OPEN,         // all fingers straight                     -> WALK
  G_POINT,        // index straight, the other three bent     -> CRAB
  G_VICTORY,      // index + middle straight                  -> BODY
  G_FIST,         // all bent (thumb too)                     -> stop
  G_ROCK,         // index + little finger straight, 0.6 s    -> next gait
  G_STAND,        // thumb up (or little finger only), 1 s    -> stand up / sit down
  G_COUNT
};

inline const char* gestureName(uint8_t g) {
  static const char* n[G_COUNT] = {"NONE", "OPEN", "POINT", "VICTORY", "FIST", "ROCK", "STAND"};
  return g < G_COUNT ? n[g] : "?";
}

inline uint8_t gestureMode(uint8_t g) {
  return g == G_OPEN ? MODE_WALK : g == G_POINT ? MODE_CRAB : g == G_VICTORY ? MODE_BODY : MODE_STOP;
}

constexpr uint16_t GLOVE_MAGIC = 0x6C4F;
constexpr uint8_t GLOVE_VERSION = 1;

struct GloveParams {
  uint16_t magic;
  uint8_t version;
  float open[NF], fist[NF];   // raw ADC with the hand open / in a fist (CAL)
  float pitch0, roll0;        // hand angles in the neutral pose (CAL, open hand flat)
  float gbias[3];             // gyro bias (deg/s)
  uint8_t thumb;              // 1 = thumb sensor fitted
  int8_t pitch_sign, roll_sign;   // AXIS: flip if the robot goes the wrong way
  uint8_t link_id;
  float dead, full;           // tilt: no motion below `dead`, full speed at `full` (deg)
  float expo;                 // 1 = linear, 2 = gentle near the middle
  float straight, bent;       // bend thresholds with hysteresis (0 = open, 1 = fist)
};

inline void gloveDefaults(GloveParams& p) {
  memset(&p, 0, sizeof(p));
  p.magic = GLOVE_MAGIC;
  p.version = GLOVE_VERSION;
  for (int f = 0; f < NF; ++f) {
    p.open[f] = 2400;   // placeholders until CAL
    p.fist[f] = 1400;
  }
  p.thumb = 1;
  p.pitch_sign = 1;
  p.roll_sign = 1;
  p.link_id = DEFAULT_LINK_ID;
  p.dead = 8;
  p.full = 35;
  p.expo = 1.5f;
  p.straight = 0.35f;
  p.bent = 0.60f;
}

inline bool gloveValid(const GloveParams& p) { return p.magic == GLOVE_MAGIC && p.version == GLOVE_VERSION; }

// 0 = as when calibrated open, 1 = as in the fist (can go a little beyond).
inline float bendOf(float raw, float open, float fist) {
  const float span = fist - open;
  if (fabsf(span) < 1) return 0;
  float b = (raw - open) / span;
  return b < -0.2f ? -0.2f : (b > 1.2f ? 1.2f : b);
}

// Straight / bent per finger with hysteresis: a finger changes only when it crosses the far
// threshold, so a finger hovering near one threshold does not flicker.
struct Fingers {
  bool straight[NF] = {true, true, true, true, true};

  void update(const float bend[NF], const GloveParams& p) {
    for (int f = 0; f < NF; ++f) {
      if (straight[f] && bend[f] > p.bent) straight[f] = false;
      else if (!straight[f] && bend[f] < p.straight) straight[f] = true;
    }
    if (!p.thumb) straight[THUMB] = false;
  }
};

inline uint8_t classify(const bool s[NF]) {
  const bool i = s[INDEX], m = s[MIDDLE], r = s[RING], l = s[PINKY], t = s[THUMB];
  if (i && m && r && l) return G_OPEN;
  if (i && !m && !r && !l) return G_POINT;
  if (i && m && !r && !l) return G_VICTORY;
  if (i && !m && !r && l) return G_ROCK;
  if (!i && !m && !r && l) return G_STAND;          // little finger only
  if (!i && !m && !r && !l) return t ? G_STAND : G_FIST;   // thumb up / fist
  return G_NONE;
}

// Debounce + hold events.
//  - A walking gesture (OPEN, POINT, VICTORY) must be steady for `settle_ms` before it counts.
//  - Every other shape counts at once, so closing the hand stops the robot immediately.
//  - ROCK held `rock_ms` and STAND held `stand_ms` each fire one event per hold.
struct GestureFilter {
  uint8_t active = G_NONE;
  uint8_t candidate = G_NONE;
  uint32_t candSince = 0, activeSince = 0;
  bool fired = false;
  uint8_t event = EV_NONE;     // last event (sent with event_seq)
  uint8_t eventSeq = 0;
  uint32_t settle_ms = 150, rock_ms = 600, stand_ms = 1000;

  // Returns true when an event fires.
  bool update(uint8_t g, uint32_t now) {
    if (g != candidate) {
      candidate = g;
      candSince = now;
    }
    const bool walking = gestureMode(g) != MODE_STOP;
    if (candidate != active && (!walking || now - candSince >= settle_ms)) {
      active = candidate;
      activeSince = now;
      fired = false;
    }
    if (!fired) {
      uint8_t ev = EV_NONE;
      if (active == G_ROCK && now - activeSince >= rock_ms) ev = EV_NEXT_GAIT;
      if (active == G_STAND && now - activeSince >= stand_ms) ev = EV_STAND_TOGGLE;
      if (ev != EV_NONE) {
        fired = true;
        event = ev;
        ++eventSeq;
        return true;
      }
    }
    return false;
  }
};

// Complementary filter for hand pitch / roll (deg). Axes: x to the fingertips, y to the thumb
// side of a right hand held palm-down (left), z out of the back of the hand.
struct Tilt {
  float pitch = 0, roll = 0;
  bool init = false;

  // a: accelerometer (g), gx / gy: gyro about x / y (deg/s), dt (s)
  void update(float ax, float ay, float az, float gx, float gy, float dt, float alpha = 0.98f) {
    const float r = 57.29578f;
    const float ra = atan2f(ay, az) * r;
    const float pa = atan2f(-ax, sqrtf(ay * ay + az * az)) * r;
    if (!init) {
      roll = ra;
      pitch = pa;
      init = true;
      return;
    }
    roll = alpha * (roll + gx * dt) + (1 - alpha) * ra;
    pitch = alpha * (pitch + gy * dt) + (1 - alpha) * pa;
  }
};

// Tilt (deg from the neutral pose) -> -1 .. 1 with a dead zone and an expo curve.
inline float tiltToCmd(float deg, float dead, float full, float expo) {
  const float a = fabsf(deg);
  if (a <= dead) return 0;
  float u = (a - dead) / (full - dead);
  if (u > 1) u = 1;
  u = powf(u, expo);
  return deg < 0 ? -u : u;
}

inline int8_t pct(float v) {
  const float c = v < -1 ? -1 : (v > 1 ? 1 : v);
  return (int8_t)lroundf(c * 100);
}

// Fills mode / x / y / turn / pitch / roll of the packet.
//   WALK: pitch (fingertips down) = forward, roll right = turn right
//   CRAB: pitch = forward, roll right = move right
//   BODY: the body copies the hand's pitch / roll
inline void makeCommand(uint8_t mode, float pitchRel, float rollRel, const GloveParams& p, GlovePacket& g) {
  g.mode = mode;
  g.x = g.y = g.turn = g.pitch = g.roll = 0;
  const float pc = tiltToCmd(pitchRel, p.dead, p.full, p.expo);
  const float rc = tiltToCmd(rollRel, p.dead, p.full, p.expo);
  if (mode == MODE_WALK) {
    g.x = pct(pc);
    g.turn = pct(-rc);
  } else if (mode == MODE_CRAB) {
    g.x = pct(pc);
    g.y = pct(-rc);
  } else if (mode == MODE_BODY) {
    g.pitch = pct(pitchRel / p.full);
    g.roll = pct(rollRel / p.full);
  }
}

}  // namespace hx
