// Servo outputs for the two kinds of ESP32 servo boards (Arduino only, not unit-tested):
//
//   PCA9685  the board has one or two PCA9685 chips on I2C (0x40, 0x41). Channel 0..15 = 0x40,
//            16..31 = 0x41. Most 18-servo ESP32 boards are built like this.
//   GPIO     every servo header is wired straight to an ESP32 pin. The ESP32 only has 16 LEDC
//            PWM channels, so the pulses come from one hardware timer instead: 3 lanes x 6
//            slots of 2.6 ms, i.e. up to 3 pulses at a time, every servo refreshed every 20 ms.
//
// BACKEND AUTO (default) uses PCA9685 if a chip answers at 0x40, otherwise GPIO.
#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <soc/gpio_reg.h>

#include "params.h"

namespace servo {

// ---------------------------------------------------------------- PCA9685
namespace pca {

constexpr uint8_t PRESCALE = 121;   // 25 MHz / 4096 / (121 + 1) = 50.0 Hz
constexpr float TICK_US = (PRESCALE + 1) / 25.0f;

inline bool present(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

inline void write8(uint8_t addr, uint8_t reg, uint8_t v) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(v);
  Wire.endTransmission();
}

inline void begin(uint8_t addr) {
  write8(addr, 0x00, 0x10);          // MODE1: sleep (needed to set the prescaler)
  write8(addr, 0xFE, PRESCALE);
  write8(addr, 0x00, 0x20);          // wake, register auto-increment
  delay(1);
  write8(addr, 0x00, 0xA0);          // restart
  write8(addr, 0x01, 0x04);          // MODE2: totem-pole outputs
  Wire.beginTransmission(addr);      // all channels fully off
  Wire.write(0xFA);
  Wire.write(0);
  Wire.write(0);
  Wire.write(0);
  Wire.write(0x10);
  Wire.endTransmission();
}

// us = 0: output off (servo relaxed)
inline void set(uint8_t addr, uint8_t ch, uint16_t us) {
  const uint16_t off = us ? (uint16_t)(us / TICK_US + 0.5f) : 0;
  Wire.beginTransmission(addr);
  Wire.write((uint8_t)(0x06 + 4 * ch));
  Wire.write(0);
  Wire.write(0);
  Wire.write((uint8_t)(off & 0xFF));
  Wire.write((uint8_t)(us ? (off >> 8) : 0x10));   // 0x10 = full off
  Wire.endTransmission();
}

}  // namespace pca

// ---------------------------------------------------------------- direct GPIO
namespace gpio {

constexpr int N = hx::NSERVO, LANES = 3, SLOTS = N / LANES;
constexpr uint32_t SLOT_US = 2600, FRAME_US = 20000;

static int8_t pin_[N];
static volatile uint16_t width_[N];   // us, 0 = no pulse
static hw_timer_t* timer_ = nullptr;
static uint64_t frame0_ = 0;          // start of the current frame (timer ticks = us)
static uint8_t slot_ = 0;
static uint8_t pending_[LANES];       // channels whose pulse is still high, by end time
static uint16_t end_[LANES];
static uint8_t npend_ = 0, next_ = 0;

inline bool pinOk(int p) {
  // not the flash pins, not the USB serial pins, not the input-only pins
  return p >= 0 && p <= 33 && !(p >= 6 && p <= 11) && p != 1 && p != 3;
}

static inline void IRAM_ATTR pinHigh(int p) {
  if (p < 32) REG_WRITE(GPIO_OUT_W1TS_REG, 1u << p);
  else REG_WRITE(GPIO_OUT1_W1TS_REG, 1u << (p - 32));
}

static inline void IRAM_ATTR pinLow(int p) {
  if (p < 32) REG_WRITE(GPIO_OUT_W1TC_REG, 1u << p);
  else REG_WRITE(GPIO_OUT1_W1TC_REG, 1u << (p - 32));
}

static inline void IRAM_ATTR alarmAt(uint64_t t) {
  const uint64_t now = timerRead(timer_);
  if (t < now + 5) t = now + 5;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  timerAlarm(timer_, t, false, 0);
#else
  timerAlarmWrite(timer_, t, false);
  timerAlarmEnable(timer_);
#endif
}

static void IRAM_ATTR onTimer() {
  const uint64_t slotStart = frame0_ + (uint64_t)slot_ * SLOT_US;
  if (next_ < npend_) {
    // a pulse (or several with the same width) ends now
    const uint16_t w = end_[next_];
    while (next_ < npend_ && end_[next_] <= w) pinLow(pin_[pending_[next_++]]);
    if (next_ < npend_) {
      alarmAt(slotStart + end_[next_]);
      return;
    }
    // slot done: next slot, or next frame
    if (++slot_ >= SLOTS) {
      slot_ = 0;
      frame0_ += FRAME_US;
    }
    alarmAt(frame0_ + (uint64_t)slot_ * SLOT_US);
    npend_ = next_ = 0;
    return;
  }
  // start of a slot: raise up to 3 pulses (channels slot, slot + 6, slot + 12)
  npend_ = next_ = 0;
  for (int l = 0; l < LANES; ++l) {
    const int ch = slot_ + l * SLOTS;
    const uint16_t w = width_[ch];
    if (pin_[ch] < 0 || w == 0) continue;
    pinHigh(pin_[ch]);
    int k = npend_++;
    while (k > 0 && end_[k - 1] > w) {   // insertion sort by end time
      end_[k] = end_[k - 1];
      pending_[k] = pending_[k - 1];
      --k;
    }
    end_[k] = w;
    pending_[k] = (uint8_t)ch;
  }
  if (npend_ > 0) {
    alarmAt(slotStart + end_[0]);
  } else {
    if (++slot_ >= SLOTS) {
      slot_ = 0;
      frame0_ += FRAME_US;
    }
    alarmAt(frame0_ + (uint64_t)slot_ * SLOT_US);
  }
}

inline void begin(const uint8_t ch[N]) {
  for (int i = 0; i < N; ++i) {
    pin_[i] = pinOk(ch[i]) ? (int8_t)ch[i] : (int8_t)-1;
    width_[i] = 0;
    if (pin_[i] >= 0) {
      pinMode(pin_[i], OUTPUT);
      digitalWrite(pin_[i], LOW);
    }
  }
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  timer_ = timerBegin(1000000);   // 1 tick = 1 us
  timerAttachInterrupt(timer_, &onTimer);
#else
  timer_ = timerBegin(0, 80, true);
  timerAttachInterrupt(timer_, &onTimer, true);
#endif
  frame0_ = timerRead(timer_) + 1000;
  slot_ = 0;
  alarmAt(frame0_);
}

inline void set(int i, uint16_t us) { width_[i] = us; }

}  // namespace gpio

// ---------------------------------------------------------------- front end
static uint8_t backend_ = hx::BACKEND_PCA9685;
static bool have41_ = false;
static uint8_t ch_[hx::NSERVO];
static uint16_t last_[hx::NSERVO];

// Returns the backend in use.
inline uint8_t begin(const hx::Params& p, int sda, int scl) {
  Wire.begin(sda, scl, 400000);
  memcpy(ch_, p.ch, sizeof(ch_));
  backend_ = p.backend;
  if (backend_ == hx::BACKEND_AUTO) backend_ = pca::present(0x40) ? hx::BACKEND_PCA9685 : hx::BACKEND_GPIO;
  if (backend_ == hx::BACKEND_PCA9685) {
    pca::begin(0x40);
    have41_ = pca::present(0x41);
    if (have41_) pca::begin(0x41);
  } else {
    gpio::begin(ch_);
  }
  for (int s = 0; s < hx::NSERVO; ++s) last_[s] = 0xFFFF;
  return backend_;
}

inline bool board41() { return have41_; }

// us = 0 turns the servo off.
inline void write(int s, uint16_t us) {
  if (last_[s] == us) return;
  last_[s] = us;
  if (backend_ == hx::BACKEND_PCA9685) {
    const uint8_t c = ch_[s];
    if (c < 16) pca::set(0x40, c, us);
    else if (c < 32 && have41_) pca::set(0x41, c - 16, us);
  } else {
    gpio::set(s, us);
  }
}

}  // namespace servo
