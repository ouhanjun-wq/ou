#include "bmp280.h"

namespace {
constexpr uint8_t REG_CALIB    = 0x88;  // 24 bytes: T1..T3, P1..P9 (little-endian)
constexpr uint8_t REG_CHIP_ID  = 0xD0;  // 0x58 = BMP280, 0x60 = BME280
constexpr uint8_t REG_RESET    = 0xE0;
constexpr uint8_t REG_CTRL_MEAS = 0xF4;
constexpr uint8_t REG_CONFIG   = 0xF5;
constexpr uint8_t REG_PRESS_MSB = 0xF7; // 6 bytes: press[19:0], temp[19:0]
// osrs_t = x2 (010), osrs_p = x16 (101), normal mode (11)
constexpr uint8_t CTRL_MEAS = (0b010 << 5) | (0b101 << 2) | 0b11;
// t_sb = 0.5 ms (000), IIR filter x4 (010), SPI 4-wire
constexpr uint8_t CONFIG = (0b000 << 5) | (0b010 << 2);
}  // namespace

void BMP280::writeReg(uint8_t reg, uint8_t val) {
  spi_->beginTransaction(settings_);
  digitalWrite(cs_, LOW);
  spi_->transfer(reg & 0x7F);
  spi_->transfer(val);
  digitalWrite(cs_, HIGH);
  spi_->endTransaction();
}

void BMP280::readRegs(uint8_t reg, uint8_t* buf, size_t n) {
  spi_->beginTransaction(settings_);
  digitalWrite(cs_, LOW);
  spi_->transfer(reg | 0x80);
  for (size_t i = 0; i < n; ++i) buf[i] = spi_->transfer(0x00);
  digitalWrite(cs_, HIGH);
  spi_->endTransaction();
}

bool BMP280::begin(SPIClass& spi, int csPin) {
  spi_ = &spi;
  cs_ = csPin;
  pinMode(cs_, OUTPUT);
  digitalWrite(cs_, HIGH);
  delay(2);
  readRegs(REG_CHIP_ID, &chipId_, 1);   // first CS edge also latches the SPI interface
  readRegs(REG_CHIP_ID, &chipId_, 1);
  if (chipId_ != 0x58 && chipId_ != 0x60) return false;

  writeReg(REG_RESET, 0xB6);
  delay(5);

  uint8_t c[24];
  readRegs(REG_CALIB, c, sizeof(c));
  auto u16 = [&](int i) { return (uint16_t)(c[i] | (c[i + 1] << 8)); };
  auto s16 = [&](int i) { return (int16_t)(c[i] | (c[i + 1] << 8)); };
  T1_ = u16(0);  T2_ = s16(2);  T3_ = s16(4);
  P1_ = u16(6);  P2_ = s16(8);  P3_ = s16(10); P4_ = s16(12); P5_ = s16(14);
  P6_ = s16(16); P7_ = s16(18); P8_ = s16(20); P9_ = s16(22);
  if (T1_ == 0 || P1_ == 0) return false;

  writeReg(REG_CONFIG, CONFIG);         // config must be written in sleep mode
  writeReg(REG_CTRL_MEAS, CTRL_MEAS);
  delay(50);
  float p, t;
  return read(p, t);
}

// Floating-point compensation from the BMP280 datasheet (section 8.1).
bool BMP280::read(float& pressurePa, float& tempC) {
  uint8_t b[6];
  readRegs(REG_PRESS_MSB, b, sizeof(b));
  const int32_t adcP = ((int32_t)b[0] << 12) | ((int32_t)b[1] << 4) | (b[2] >> 4);
  const int32_t adcT = ((int32_t)b[3] << 12) | ((int32_t)b[4] << 4) | (b[5] >> 4);
  if (adcP == 0x80000 || adcT == 0x80000 || adcP == 0 || adcP == 0xFFFFF) return false;  // skipped / stuck

  double v1 = (adcT / 16384.0 - T1_ / 1024.0) * T2_;
  double v2 = (adcT / 131072.0 - T1_ / 8192.0) * (adcT / 131072.0 - T1_ / 8192.0) * T3_;
  const double tFine = v1 + v2;
  tempC = (float)(tFine / 5120.0);

  v1 = tFine / 2.0 - 64000.0;
  v2 = v1 * v1 * P6_ / 32768.0;
  v2 = v2 + v1 * P5_ * 2.0;
  v2 = v2 / 4.0 + P4_ * 65536.0;
  v1 = (P3_ * v1 * v1 / 524288.0 + P2_ * v1) / 524288.0;
  v1 = (1.0 + v1 / 32768.0) * P1_;
  if (v1 == 0.0) return false;
  double p = 1048576.0 - adcP;
  p = (p - v2 / 4096.0) * 6250.0 / v1;
  v1 = P9_ * p * p / 2147483648.0;
  v2 = p * P8_ / 32768.0;
  p = p + (v1 + v2 + P7_) / 16.0;
  if (p < 30000.0 || p > 110000.0) return false;
  pressurePa = (float)p;
  return true;
}
