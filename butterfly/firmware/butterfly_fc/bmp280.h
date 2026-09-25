// Minimal BMP280 (or BME280) barometer driver on SPI, shared bus with the IMU.
// Normal mode, pressure x16 / temperature x2 oversampling, IIR x4 (~22 Hz output).
#pragma once
#include <Arduino.h>
#include <SPI.h>

class BMP280 {
 public:
  bool begin(SPIClass& spi, int csPin);
  // Pressure in Pa, temperature in deg C. Returns false on a bad read.
  bool read(float& pressurePa, float& tempC);
  uint8_t chipId() const { return chipId_; }

 private:
  void writeReg(uint8_t reg, uint8_t val);
  void readRegs(uint8_t reg, uint8_t* buf, size_t n);

  SPIClass* spi_ = nullptr;
  int cs_ = -1;
  SPISettings settings_{4000000, MSBFIRST, SPI_MODE0};
  uint8_t chipId_ = 0;
  uint16_t T1_ = 0, P1_ = 0;
  int16_t T2_ = 0, T3_ = 0, P2_ = 0, P3_ = 0, P4_ = 0, P5_ = 0, P6_ = 0, P7_ = 0, P8_ = 0, P9_ = 0;
};

// Pressure (Pa) -> altitude (m) relative to reference pressure p0 (Pa).
inline float pressureToAltitude(float p, float p0) {
  return 44330.0f * (1.0f - powf(p / p0, 0.190295f));
}
