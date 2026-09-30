// Hardware-independent core of the bionic hand firmware (unit-tested on the host).
//
// Channel order everywhere: 0 index, 1 middle, 2 ring, 3 pinky, 4 thumb flex, 5 thumb rotation.
// "Position" n is 0 .. 1000: 0 = finger open / thumb beside the palm, 1000 = closed / opposed.
//
//   glove pot (ADC 0..1023) --calibrate--> n --EMA + deadband--> target n
//     --per-servo limits--> target us --slew limit--> servo pulse
#pragma once
#include <stdint.h>

namespace hc {

constexpr uint8_t N = 6;
constexpr int16_t N_MAX = 1000;
constexpr int16_t MIN_SPAN = 60;        // ADC counts between open and closed for a valid calibration
constexpr int16_t US_MIN = 500, US_MAX = 2500;
constexpr int16_t UNPLUG_RAW = 1015;    // INPUT_PULLUP: a glove cable that is not plugged in reads ~1023
constexpr uint16_t UNPLUG_MS = 200;     // all channels high this long = glove unplugged
constexpr uint16_t HOLD_MS = 500;       // then hold the last pose this long, then ease to open
constexpr uint8_t EMA_SHIFT = 2;        // EMA alpha = 1/4 (at 50 Hz: time constant ~70 ms)
constexpr int16_t DEADBAND = 6;         // output moves only when the filtered value moves more than this

template <class T> inline T clampv(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ---------------- calibration: raw ADC -> 0..1000 ----------------
// Three points (open, half, closed); piecewise linear, works for pots turning either way.
struct Cal {
  int16_t open, half, closed;
};

inline bool calValid(const Cal& c) {
  const int32_t a = (int32_t)c.half - c.open, b = (int32_t)c.closed - c.half;
  const int32_t span = (int32_t)c.closed - c.open;
  return a * b > 0 && (span >= MIN_SPAN || span <= -MIN_SPAN);
}

// Linear map of x on [x0, x1] to [y0, y1], clamped (x0 != x1).
inline int16_t lerp(int32_t x, int32_t x0, int32_t x1, int32_t y0, int32_t y1) {
  int32_t num = (x - x0) * (y1 - y0), d = x1 - x0;
  if (d < 0) num = -num, d = -d;
  const int32_t y = y0 + (num >= 0 ? (num + d / 2) / d : -((-num + d / 2) / d));   // rounded
  const int32_t lo = y0 < y1 ? y0 : y1, hi = y0 < y1 ? y1 : y0;
  return (int16_t)clampv<int32_t>(y, lo, hi);
}

inline int16_t normalize(const Cal& c, int16_t raw) {
  if (!calValid(c)) return 0;
  const bool up = c.closed > c.open;
  const bool firstHalf = up ? raw <= c.half : raw >= c.half;
  return firstHalf ? lerp(raw, c.open, c.half, 0, N_MAX / 2) : lerp(raw, c.half, c.closed, N_MAX / 2, N_MAX);
}

// ---------------- filter: EMA + hysteresis deadband ----------------
struct Smooth {
  int32_t acc = -1;   // EMA state << EMA_SHIFT; -1 = not started
  int16_t out = 0;
  int16_t update(int16_t x) {
    if (acc < 0) {
      acc = (int32_t)x << EMA_SHIFT;
      out = x;
      return out;
    }
    acc += x - (acc >> EMA_SHIFT);
    const int16_t y = (int16_t)(acc >> EMA_SHIFT);
    // follow big moves at once; small ones only when the EMA has settled on the input
    if (y - out > DEADBAND || out - y > DEADBAND || y == x) out = y;
    return out;
  }
};

// ---------------- servo limits: n -> pulse ----------------
// us_open / us_closed are the pulses at n = 0 / 1000 (either order: that is the "invert").
struct Lim {
  int16_t us_open, us_closed;
};

inline int16_t toUs(const Lim& l, int16_t n) {
  n = clampv<int16_t>(n, 0, N_MAX);
  return clampv<int16_t>(lerp(n, 0, N_MAX, l.us_open, l.us_closed), US_MIN, US_MAX);
}

// Move cur towards target by at most step.
inline int16_t slew(int16_t cur, int16_t target, int16_t step) {
  if (target > cur + step) return cur + step;
  if (target < cur - step) return cur - step;
  return target;
}

// ---------------- gestures ----------------
struct Gesture {
  const char* name;
  int16_t n[N];
};
// clang-format off
constexpr Gesture GESTURES[] = {
  {"paper",    {   0,    0,    0,    0,    0,    0}},   // open hand / 5
  {"rock",     {1000, 1000, 1000, 1000,  800,  700}},   // fist
  {"scissors", {   0,    0, 1000, 1000,  800,  800}},
  {"ok",       { 650,    0,    0,    0,  600, 1000}},
  {"like",     {1000, 1000, 1000, 1000,    0,    0}},   // thumbs up
  {"one",      {   0, 1000, 1000, 1000,  900, 1000}},
  {"two",      {   0,    0, 1000, 1000,  900, 1000}},
  {"three",    {   0,    0,    0, 1000,  900, 1000}},
  {"four",     {   0,    0,    0,    0,  900, 1000}},
  {"rock_on",  {   0, 1000, 1000,    0,  800,  800}},
};
// clang-format on
constexpr uint8_t N_GESTURES = sizeof(GESTURES) / sizeof(GESTURES[0]);

inline bool streq(const char* a, const char* b) {
  while (*a && *a == *b) ++a, ++b;
  return *a == *b;
}
inline int8_t findGesture(const char* name) {
  for (uint8_t i = 0; i < N_GESTURES; ++i)
    if (streq(GESTURES[i].name, name)) return (int8_t)i;
  return -1;
}

// ---------------- settings (stored in EEPROM with a CRC) ----------------
constexpr uint8_t SETTINGS_MAGIC = 0xB5, SETTINGS_VERSION = 1;

struct Settings {
  uint8_t magic, version;
  Cal cal[N];
  Lim lim[N];
  int16_t slew_us;   // max pulse change per 20 ms tick
  uint16_t crc;
};

inline uint16_t crc16(const uint8_t* p, uint16_t n) {   // CRC-16/CCITT-FALSE
  uint16_t c = 0xFFFF;
  while (n--) {
    c ^= (uint16_t)(*p++) << 8;
    for (uint8_t i = 0; i < 8; ++i) c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
  }
  return c;
}
inline uint16_t settingsCrc(const Settings& s) {
  return crc16(reinterpret_cast<const uint8_t*>(&s), (uint16_t)(sizeof(Settings) - sizeof(s.crc)));
}
inline void seal(Settings& s) { s.crc = settingsCrc(s); }
inline bool settingsOk(const Settings& s) {
  return s.magic == SETTINGS_MAGIC && s.version == SETTINGS_VERSION && s.crc == settingsCrc(s);
}

inline Settings defaults() {
  Settings s{};
  s.magic = SETTINGS_MAGIC;
  s.version = SETTINGS_VERSION;
  for (uint8_t i = 0; i < N; ++i) {
    s.cal[i] = {300, 450, 600};   // placeholder until the glove is calibrated
    s.lim[i] = {1500, 1500};      // servos stay centred until limits are set (safe for assembly)
  }
  s.slew_us = 40;                 // 40 us / 20 ms = 2000 us/s, about 200 deg/s
  seal(s);
  return s;
}

// ---------------- the controller ----------------
enum Mode : uint8_t { MIRROR = 0, GESTURE = 1, DEMO = 2, MANUAL = 3 };

class Controller {
 public:
  Settings set = defaults();
  Mode mode = MIRROR;
  int8_t gesture = 0;
  int16_t n[N] = {0};        // target positions after filtering (MIRROR) or from the gesture
  int16_t us[N] = {0};       // pulses sent to the servos
  int16_t manual_us[N] = {0};
  bool unplugged = false;
  uint8_t demo_step = 0;

  void begin() {
    for (uint8_t i = 0; i < N; ++i) {
      us[i] = toUs(set.lim[i], 0);
      manual_us[i] = us[i];
    }
  }

  // Glove connected? (every channel pulled high means the cable is out)
  static bool allHigh(const int16_t raw[N]) {
    for (uint8_t i = 0; i < N; ++i)
      if (raw[i] < UNPLUG_RAW) return false;
    return true;
  }

  // One 20 ms tick. raw = ADC readings in channel order.
  void update(const int16_t raw[N], uint32_t now_ms) {
    // glove plug detection with a debounce
    if (allHigh(raw)) {
      if (!high_) {
        high_ = true;
        high_since_ = now_ms;
      }
      if (!unplugged && now_ms - high_since_ >= UNPLUG_MS) {
        unplugged = true;
        unplug_ms_ = now_ms;
      }
    } else {
      high_ = false;
      if (unplugged) {
        unplugged = false;
        for (uint8_t i = 0; i < N; ++i) smooth_[i] = Smooth();
      }
    }

    switch (mode) {
      case MIRROR:
        if (unplugged) {
          if (now_ms - unplug_ms_ >= HOLD_MS)
            for (uint8_t i = 0; i < N; ++i) n[i] = 0;   // ease to the open hand
        } else if (!allHigh(raw)) {
          for (uint8_t i = 0; i < N; ++i) n[i] = smooth_[i].update(normalize(set.cal[i], raw[i]));
        }
        break;
      case DEMO:
        if (now_ms - demo_ms_ >= 1500) {
          demo_ms_ = now_ms;
          gesture = (int8_t)(demo_step++ % N_GESTURES);
        }
        // fall through
      case GESTURE:
        for (uint8_t i = 0; i < N; ++i) n[i] = GESTURES[gesture].n[i];
        break;
      case MANUAL:
        break;
    }

    for (uint8_t i = 0; i < N; ++i) {
      const int16_t target = mode == MANUAL ? clampv(manual_us[i], US_MIN, US_MAX) : toUs(set.lim[i], n[i]);
      us[i] = slew(us[i], target, set.slew_us);
    }
  }

  void setMode(Mode m) {
    mode = m;
    demo_ms_ = 0;
    demo_step = 0;
    if (m == MANUAL)
      for (uint8_t i = 0; i < N; ++i) manual_us[i] = us[i];
  }

 private:
  Smooth smooth_[N];
  bool high_ = false;
  uint32_t high_since_ = 0, unplug_ms_ = 0, demo_ms_ = 0;
};

}  // namespace hc
