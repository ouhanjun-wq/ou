// 18 servo outputs. Two kinds of expansion board are supported, chosen per servo at runtime:
//   PCA  - a PCA9685 16-channel PWM chip on I2C (boards with 1 or 2 of these chips)
//   GPIO - the servo signal goes straight to an ESP32 pin (16 LEDC channels + 6 MCPWM channels)
//
// The servo map (which output drives which joint), each servo's direction and centre trim are kept
// in flash and edited over USB serial ("map", "dir", "trim", "save" - see the .ino).
// With nothing saved yet the map is guessed at boot: PCA9685 found -> kDefaultPca, else kDefaultGpio.
#pragma once
#include <stdint.h>

namespace servo {

constexpr int NUM = 18;              // servo n = leg * 3 + joint (0 coxa, 1 femur, 2 tibia)
constexpr int US_MIN = 500, US_MID = 1500, US_MAX = 2500;
constexpr uint16_t TABLE_VERSION = 1;

enum Kind : uint8_t { NONE = 0, PCA = 1, GPIO = 2 };

struct Map {
  uint8_t kind;      // Kind
  uint8_t a;         // PCA: I2C address (0x40..0x47)   GPIO: pin number
  uint8_t b;         // PCA: channel 0..15              GPIO: unused
  int8_t dir;        // +1 / -1: flips the servo's direction
  int16_t trim_us;   // centre correction (us), about +-10 us per degree
};

struct Table {
  uint16_t version;
  Map m[NUM];
};

// Output-capable ESP32 pins that are safe to probe (no flash / UART / input-only pins).
constexpr uint8_t kProbePins[] = {2, 4, 5, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33};

// Guess for boards with two PCA9685 chips: left legs on 0x40 channels 0-8, right legs on 0x41.
inline void defaultPca(Table& t) {
  t.version = TABLE_VERSION;
  for (int n = 0; n < NUM; ++n) t.m[n] = {PCA, uint8_t(n < 9 ? 0x40 : 0x41), uint8_t(n % 9), 1, 0};
}

// Guess for boards that wire the servos straight to ESP32 pins (LF LM LR RF RM RR, coxa femur tibia).
inline void defaultGpio(Table& t) {
  static const uint8_t pins[NUM] = {13, 12, 14, 27, 26, 25, 33, 32, 15,
                                    2, 4, 16, 17, 5, 18, 19, 21, 23};
  t.version = TABLE_VERSION;
  for (int n = 0; n < NUM; ++n) t.m[n] = {GPIO, pins[n], 0, 1, 0};
}

// Joint angle (deg, 0 = assembly pose) -> pulse width (us).
inline int pulseUs(float deg, const Map& m, float us_per_deg) {
  float us = US_MID + m.dir * deg * us_per_deg + m.trim_us;
  if (us < US_MIN) us = US_MIN;
  if (us > US_MAX) us = US_MAX;
  return int(us + 0.5f);
}

extern Table T;

#ifdef ARDUINO
// Start I2C and look for PCA9685 chips; loads the saved map (or guesses one). Outputs stay off.
void begin(int sda, int scl);
bool load();                           // false = nothing saved
bool save();
void writeUs(int n, int us);           // one servo, raw pulse
void writeDeg(int n, float deg, float us_per_deg);
void off(int n);                       // stop pulses: the servo goes limp
void allOff();                         // all servos limp, all pins released
int lastUs(int n);                     // 0 = off
bool pcaFound(uint8_t addr);           // PCA9685 answered at this address during begin()
int scanI2c(void (*found)(uint8_t addr));

// Probe outputs that are not (yet) in the map, to find where each servo plug goes.
void probeGpio(uint8_t pin, int us);   // us = 0 releases the pin
void probePca(uint8_t addr, uint8_t ch, int us);
#endif

}  // namespace servo
