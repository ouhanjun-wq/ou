// Minimal INA226 current / voltage monitor on the servo supply (no external library).
#pragma once
#include <Arduino.h>
#include <Wire.h>

class INA226 {
 public:
  bool begin(TwoWire& wire, uint8_t addr);
  // amps from the shunt voltage (shuntOhm = 0.01 on the usual "R010" modules), volts = bus.
  bool read(float shuntOhm, float& amps, float& volts);
  bool ok() const { return ok_; }

 private:
  bool read16(uint8_t reg, uint16_t& v);
  void write16(uint8_t reg, uint16_t v);
  TwoWire* w_ = nullptr;
  uint8_t addr_ = 0x40;
  bool ok_ = false;
};
