#include "ina226.h"

namespace {
constexpr uint8_t REG_CONFIG = 0x00, REG_SHUNT = 0x01, REG_BUS = 0x02, REG_MFG_ID = 0xFE;
// 4 samples averaged, 1.1 ms conversions, shunt + bus continuous (~9 ms per result)
constexpr uint16_t CONFIG = 0x4000 | (1 << 9) | (4 << 6) | (4 << 3) | 7;
}  // namespace

bool INA226::read16(uint8_t reg, uint16_t& v) {
  w_->beginTransmission(addr_);
  w_->write(reg);
  if (w_->endTransmission(false) != 0) return false;
  if (w_->requestFrom((int)addr_, 2) != 2) return false;
  v = (uint16_t)(w_->read() << 8);
  v |= (uint16_t)w_->read();
  return true;
}

void INA226::write16(uint8_t reg, uint16_t v) {
  w_->beginTransmission(addr_);
  w_->write(reg);
  w_->write(v >> 8);
  w_->write(v & 0xFF);
  w_->endTransmission();
}

bool INA226::begin(TwoWire& wire, uint8_t addr) {
  w_ = &wire;
  addr_ = addr;
  uint16_t id = 0;
  ok_ = read16(REG_MFG_ID, id) && id == 0x5449;   // "TI"
  if (ok_) write16(REG_CONFIG, CONFIG);
  return ok_;
}

bool INA226::read(float shuntOhm, float& amps, float& volts) {
  if (!ok_) return false;
  uint16_t sh, bus;
  if (!read16(REG_SHUNT, sh) || !read16(REG_BUS, bus)) return false;
  amps = (int16_t)sh * 2.5e-6f / shuntOhm;   // 2.5 uV per bit
  volts = bus * 1.25e-3f;                     // 1.25 mV per bit
  return true;
}
