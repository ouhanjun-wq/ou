#include "servo_out.h"

#include "config.h"

static uint32_t usToDuty(int us) {
  us = constrain(us, SERVO_US_MIN, SERVO_US_MAX);
  return (uint32_t)((uint64_t)us * SERVO_PWM_HZ * (1u << SERVO_PWM_BITS) / 1000000u);
}

bool ServoOut::begin(int pinL, int pinR) {
  pinL_ = pinL;
  pinR_ = pinR;
  bool ok = ledcAttach(pinL_, SERVO_PWM_HZ, SERVO_PWM_BITS);
  ok = ledcAttach(pinR_, SERVO_PWM_HZ, SERVO_PWM_BITS) && ok;
  writeUs(SERVO_US_MID, SERVO_US_MID);
  return ok;
}

void ServoOut::writeUs(int usL, int usR) {
  ledcWrite(pinL_, usToDuty(usL));
  ledcWrite(pinR_, usToDuty(usR));
}

void ServoOut::writeWings(float wingL, float wingR, const Params& p) {
  const float l = p.servo_dir_l * (wingL + p.trim_l) * p.servo_us_per_deg;
  const float r = p.servo_dir_r * (wingR + p.trim_r) * p.servo_us_per_deg;
  writeUs(SERVO_US_MID + (int)lroundf(l), SERVO_US_MID + (int)lroundf(r));
}
