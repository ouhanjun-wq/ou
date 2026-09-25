// Minimal ICM-42688-P SPI driver: 1 kHz ODR, gyro ±2000 dps, accel ±16 g,
// on-chip anti-alias filters at ~258 Hz. Output is in the chip frame.
#pragma once
#include <Arduino.h>
#include <SPI.h>

class ICM42688 {
 public:
  bool begin(SPIClass& spi, int csPin);
  // Returns false if the sample is invalid (bus error / not ready).
  bool read(float accG[3], float gyroDps[3]);
  uint8_t whoAmI();
  uint32_t errors() const { return errors_; }

 private:
  void writeReg(uint8_t reg, uint8_t val);
  uint8_t readReg(uint8_t reg);
  void readRegs(uint8_t reg, uint8_t* buf, size_t n);
  void bank(uint8_t b);

  SPIClass* spi_ = nullptr;
  int cs_ = -1;
  SPISettings settings_{1000000, MSBFIRST, SPI_MODE0};
  uint32_t errors_ = 0;
};
