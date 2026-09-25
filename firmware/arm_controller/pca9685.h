// Minimal PCA9685 16-channel PWM driver for hobby servos (no external library).
#pragma once
#include <Arduino.h>
#include <Wire.h>

class PCA9685 {
 public:
  // oscHz: the chip's internal oscillator (nominal 25 MHz; see param pwm_osc_hz).
  bool begin(TwoWire& wire, uint8_t addr, float oscHz, float pwmHz);
  void writeUs(uint8_t ch, float us);
  void off(uint8_t ch);
  void allOff();
  bool ok() const { return ok_; }

 private:
  void write8(uint8_t reg, uint8_t v);
  TwoWire* w_ = nullptr;
  uint8_t addr_ = 0x40;
  float ticksPerUs_ = 0.2048f;
  bool ok_ = false;
};
