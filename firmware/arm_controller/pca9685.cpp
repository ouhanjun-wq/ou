#include "pca9685.h"

namespace {
constexpr uint8_t MODE1 = 0x00, MODE2 = 0x01, LED0_ON_L = 0x06, ALL_LED_OFF_H = 0xFD, PRESCALE = 0xFE;
constexpr uint8_t MODE1_SLEEP = 0x10, MODE1_AI = 0x20, MODE1_RESTART = 0x80, MODE2_OUTDRV = 0x04;
}  // namespace

void PCA9685::write8(uint8_t reg, uint8_t v) {
  w_->beginTransmission(addr_);
  w_->write(reg);
  w_->write(v);
  w_->endTransmission();
}

bool PCA9685::begin(TwoWire& wire, uint8_t addr, float oscHz, float pwmHz) {
  w_ = &wire;
  addr_ = addr;
  w_->beginTransmission(addr_);
  ok_ = w_->endTransmission() == 0;
  if (!ok_) return false;
  int pre = (int)lroundf(oscHz / (4096.0f * pwmHz)) - 1;
  pre = constrain(pre, 3, 255);
  write8(MODE1, MODE1_SLEEP);            // prescaler can only be written while asleep
  write8(PRESCALE, (uint8_t)pre);
  write8(MODE2, MODE2_OUTDRV);           // totem-pole outputs
  write8(MODE1, MODE1_AI);               // wake up, register auto-increment
  delay(1);
  write8(MODE1, MODE1_AI | MODE1_RESTART);
  const float realHz = oscHz / (4096.0f * (pre + 1));
  ticksPerUs_ = 4096.0f * realHz / 1e6f;
  allOff();
  return true;
}

void PCA9685::writeUs(uint8_t ch, float us) {
  if (!ok_ || ch > 15) return;
  const uint16_t off = (uint16_t)constrain(lroundf(us * ticksPerUs_), 0L, 4095L);
  w_->beginTransmission(addr_);
  w_->write(LED0_ON_L + 4 * ch);
  w_->write(0);                          // on at tick 0
  w_->write(0);
  w_->write(off & 0xFF);                 // off after the pulse width
  w_->write(off >> 8);
  w_->endTransmission();
}

void PCA9685::off(uint8_t ch) {
  if (!ok_ || ch > 15) return;
  w_->beginTransmission(addr_);
  w_->write(LED0_ON_L + 4 * ch);
  w_->write(0);
  w_->write(0);
  w_->write(0);
  w_->write(0x10);                       // full off: no pulses, the servo goes limp
  w_->endTransmission();
}

void PCA9685::allOff() {
  if (!ok_) return;
  write8(ALL_LED_OFF_H, 0x10);
}
