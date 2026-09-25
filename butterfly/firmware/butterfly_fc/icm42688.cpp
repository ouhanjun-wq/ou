#include "icm42688.h"

namespace {
// Bank 0
constexpr uint8_t REG_DEVICE_CONFIG = 0x11;
constexpr uint8_t REG_ACCEL_DATA_X1 = 0x1F;  // 6 x accel + 6 x gyro bytes, big-endian
constexpr uint8_t REG_PWR_MGMT0     = 0x4E;
constexpr uint8_t REG_GYRO_CONFIG0  = 0x4F;
constexpr uint8_t REG_ACCEL_CONFIG0 = 0x50;
constexpr uint8_t REG_WHO_AM_I      = 0x75;
constexpr uint8_t REG_BANK_SEL      = 0x76;
// Bank 1 (gyro anti-alias filter)
constexpr uint8_t REG_GYRO_CONFIG_STATIC3 = 0x0C;
constexpr uint8_t REG_GYRO_CONFIG_STATIC4 = 0x0D;
constexpr uint8_t REG_GYRO_CONFIG_STATIC5 = 0x0E;
// Bank 2 (accel anti-alias filter)
constexpr uint8_t REG_ACCEL_CONFIG_STATIC2 = 0x03;
constexpr uint8_t REG_ACCEL_CONFIG_STATIC3 = 0x04;
constexpr uint8_t REG_ACCEL_CONFIG_STATIC4 = 0x05;

constexpr uint8_t WHO_AM_I_VALUE = 0x47;
constexpr uint8_t FS_2000DPS = 0 << 5, FS_16G = 0 << 5, ODR_1KHZ = 0x06;
constexpr float GYRO_LSB_PER_DPS = 16.4f;
constexpr float ACCEL_LSB_PER_G  = 2048.0f;

// Anti-alias filter ~258 Hz (datasheet section 5.3 table): DELT, DELTSQR, BITSHIFT
constexpr uint8_t AAF_DELT = 6;
constexpr uint16_t AAF_DELTSQR = 36;
constexpr uint8_t AAF_BITSHIFT = 10;
}  // namespace

void ICM42688::writeReg(uint8_t reg, uint8_t val) {
  spi_->beginTransaction(settings_);
  digitalWrite(cs_, LOW);
  spi_->transfer(reg & 0x7F);
  spi_->transfer(val);
  digitalWrite(cs_, HIGH);
  spi_->endTransaction();
}

uint8_t ICM42688::readReg(uint8_t reg) {
  uint8_t v;
  readRegs(reg, &v, 1);
  return v;
}

void ICM42688::readRegs(uint8_t reg, uint8_t* buf, size_t n) {
  spi_->beginTransaction(settings_);
  digitalWrite(cs_, LOW);
  spi_->transfer(reg | 0x80);
  for (size_t i = 0; i < n; ++i) buf[i] = spi_->transfer(0x00);
  digitalWrite(cs_, HIGH);
  spi_->endTransaction();
}

void ICM42688::bank(uint8_t b) { writeReg(REG_BANK_SEL, b); }

uint8_t ICM42688::whoAmI() { return readReg(REG_WHO_AM_I); }

bool ICM42688::begin(SPIClass& spi, int csPin) {
  spi_ = &spi;
  cs_ = csPin;
  pinMode(cs_, OUTPUT);
  digitalWrite(cs_, HIGH);
  settings_ = SPISettings(1000000, MSBFIRST, SPI_MODE0);  // slow during setup
  delay(10);

  bank(0);
  writeReg(REG_DEVICE_CONFIG, 0x01);  // soft reset
  delay(5);
  if (whoAmI() != WHO_AM_I_VALUE) return false;

  // Sensors are off after reset: configure filters first.
  bank(1);
  writeReg(REG_GYRO_CONFIG_STATIC3, AAF_DELT);
  writeReg(REG_GYRO_CONFIG_STATIC4, AAF_DELTSQR & 0xFF);
  writeReg(REG_GYRO_CONFIG_STATIC5, (AAF_DELTSQR >> 8) | (AAF_BITSHIFT << 4));
  bank(2);
  writeReg(REG_ACCEL_CONFIG_STATIC2, AAF_DELT << 1);
  writeReg(REG_ACCEL_CONFIG_STATIC3, AAF_DELTSQR & 0xFF);
  writeReg(REG_ACCEL_CONFIG_STATIC4, (AAF_DELTSQR >> 8) | (AAF_BITSHIFT << 4));
  bank(0);

  writeReg(REG_GYRO_CONFIG0, FS_2000DPS | ODR_1KHZ);
  writeReg(REG_ACCEL_CONFIG0, FS_16G | ODR_1KHZ);
  writeReg(REG_PWR_MGMT0, 0x0F);      // gyro + accel in low-noise mode
  delay(50);                          // gyro start-up time

  settings_ = SPISettings(8000000, MSBFIRST, SPI_MODE0);
  return whoAmI() == WHO_AM_I_VALUE;
}

bool ICM42688::read(float accG[3], float gyroDps[3]) {
  uint8_t b[12];
  readRegs(REG_ACCEL_DATA_X1, b, sizeof(b));
  int16_t raw[6];
  bool allSame = true;
  for (int i = 0; i < 6; ++i) {
    raw[i] = (int16_t)((b[2 * i] << 8) | b[2 * i + 1]);
    if (raw[i] == -32768) { ++errors_; return false; }  // "no data" marker
    if (raw[i] != raw[0]) allSame = false;
  }
  if (allSame) { ++errors_; return false; }  // bus stuck at 0x00 / 0xFF
  for (int i = 0; i < 3; ++i) {
    accG[i] = raw[i] / ACCEL_LSB_PER_G;
    gyroDps[i] = raw[i + 3] / GYRO_LSB_PER_DPS;
  }
  return true;
}
